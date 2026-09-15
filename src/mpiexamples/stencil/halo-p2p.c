#include "mpi.h"
#include "meshsup.h"
#include "decomp.h"
#include "stencil.h"

MPI_Datatype halotype[2*MAX_DIMS], halotypesend[2*MAX_DIMS];
MPI_Aint halooffset[2*MAX_DIMS], halosendoffset[2*MAX_DIMS];
int partnerrank[2*MAX_DIMS];

/*
 * Create any datatypes or other items that will be used in the halo
 * exchange
 *
 * Parameters:
 * comm - (IN) Communicator of processes
 * psizes - (IN) Number of processes in each dimension of Cartesian array
 *          of processes
 * pcoords - (IN) Coordinates of this process in the Cartesian array of
 *          processes
 * halowidth - (IN) Width of halo (typically 1)
 * useSubarray - (IN) True if datatypes should be constructed with MPI
 *           subarray
 * larraylen - (IN) Local array size (all processes have the same size)
 * larraylenhalo - (IN) Local array size including halos (all processes
 *             have the same size)
 *
 */
void haloexchangeInit(cartdecompCtx *decompctx,
		      int halowidth, int useSubarray, const int larraylen[],
		      const int larraylenhalo[])
{
    int i, ndims=3;
    int nt=0;

    for (i=0; i<ndims; i++) {
	/* Left halo in each dimension */
	BENV_MeshHaloDatatypes(ndims, i, -1, useSubarray, MPI_INT,
			       decompctx->pcoords, decompctx->psizes,
			       halowidth, larraylen,
			       larraylenhalo, &halotype[nt], &halooffset[nt],
			       &halotypesend[nt], &halosendoffset[nt]);
	/* out-of-range coordinates -> MPI_PROC_NULL */
	BENV_CartDecompGetShift(decompctx, i, -1, &partnerrank[nt]);

	/* Right halo in each dimension */
	nt++;
	BENV_MeshHaloDatatypes(ndims, i,  1, useSubarray, MPI_INT,
			       decompctx->pcoords, decompctx->psizes,
			       halowidth, larraylen,
			       larraylenhalo, &halotype[nt], &halooffset[nt],
			       &halotypesend[nt], &halosendoffset[nt]);
	BENV_CartDecompGetShift(decompctx, i, +1, &partnerrank[nt]);

	nt++;
    }
}

/*
 * Perform a halo exchange
 *
 * Parameters:
 * comm - (IN) Communicator of processes
 * lmeshbuf - (INOUT) Local mesh, including halo
 * td - (OUT) Timing information for halo exchange
 *
 * FIXME: Could structure to allow computation/communication overlap
 */
void haloexchange(MPI_Comm comm, double *lmeshbuf, timingData *td)
{
    int i, nreq;
    MPI_Request r[2*MAX_DIMS];
    double t0, t1, t2;

    /* If not using subarray types, also need to compute index of offset,
       since in that case, the datatypes do not include the offset to the
       start of the halo */
    nreq=0;
    t0 = MPI_Wtime();
    td->commlen = 0;
    for (i=0; i<6; i++) {
	if (partnerrank[i] != MPI_PROC_NULL) {
	    MPI_Irecv(lmeshbuf+halooffset[i], 1, halotype[i],
		      partnerrank[i], 0, comm, &r[nreq]);
	    nreq++;
	}
    }
    for (i=0; i<6; i++) {
	int ts;
	if (partnerrank[i] != MPI_PROC_NULL) {
	    MPI_Isend(lmeshbuf+halosendoffset[i], 1, halotypesend[i],
		      partnerrank[i], 0, comm, &r[nreq]);
	    /* An int is large enough for a halo */
	    MPI_Type_size(halotypesend[i], &ts);
	    td->commlen += ts;
	    nreq++;
	}
    }
    t1 = MPI_Wtime();
    MPI_Waitall(nreq, r, MPI_STATUSES_IGNORE);
    t2 = MPI_Wtime();
    td->tcommPost += t1 - t0;
    td->tcommWait += t2 - t1;
}
