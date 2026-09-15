#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mpi.h"
#include "benvutil.h"
#include "benvmpiutil.h"
#include "seq.h"
#include "meshsup.h"
#include "arrindex.h"
#include "subarrays.h"
#include "decomp.h"

/*
 * This is an example program primarily for data read and write to a file
 * for a regular multidimensional mesh. As such, it needs an input file,
 * which can be created by ctestinput.
 * An example for mesh computation, including a Matrix-vector multiply sweep,
 * is in the stencil directory. That example includes options for different
 * I/O, decomposition, exchange, etc.
 *
 * To use this example (3d version with a 30x40x50 global grid):
 *   ./ctestinput --dims 3 --sizes 30 40 50
 *   mpiexec -n 8 ./meshio --dims 3 --sizes 30 40 50 -psizes 2 2 2
 */

typedef struct { double tcommstart, tcomminit, tcommwait, tcommwaitend,
	tsweepstart, tsweepend; } timingData;

static int debugMesh = 0;
static int debugDecomp = 0;
static int maxErrors = 10;

void printUsage(FILE *);
int compareMesh(int ndims, const int coords[], const int gsizes[],
		const int loffsets[], const int lsizes[],
		const int lsizesdecl[], const int *buf);

void sweep(int *, int *, int, int, int);

int main(int argc, char **argv)
{
    int err, i, nt, ndims=2, sizes[MAX_DIMS], psizes[MAX_DIMS];
    const char *fnamein=0, *fnameout=0;
    int distribkind = 0;  /* 0 for (nearly)equal, 1 for "HDF-style" */
    int crank, provided, periods[MAX_DIMS], pcoords[MAX_DIMS];
    int gindex[MAX_DIMS], larraylen[MAX_DIMS], larraylenhalo[MAX_DIMS],
	larraystarts[MAX_DIMS];
    MPI_Comm cartcomm;
    int halowidth=1;
    int lmeshsize, *lmeshbuf;
    MPI_Offset disp;
    int useDarray = 0;
    MPI_Datatype fileviewtype, subarraytype;
    MPI_Status stat;
    MPI_File fhin, fhout;
    int useSubarray = 0;
    MPI_Datatype halotype[2*MAX_DIMS], halotypesend[2*MAX_DIMS];
    int partnerrank[2*MAX_DIMS];
    MPI_Aint halooffset[2*MAX_DIMS], halosendoffset[2*MAX_DIMS];
    cartdecompCtx *decompctx;

    MPI_Init_thread(0, 0, MPI_THREAD_SINGLE, &provided);

    for (i=0; i<MAX_DIMS; i++) {
	psizes[i] = 0;
	sizes[i]  = 1;
    }
    /* Get options */
    for (i=1; i<argc; i++) {
	if (strcmp("--dims", argv[i]) == 0) {
	    if (BENV_ArgCheckEnoughArgs(i, argc, argv[i], 1, printUsage))
		abort();
	    ndims = atoi(argv[++i]);
	    if (ndims <= 0 || ndims > MAX_DIMS) {
		fprintf(stderr, "--dims value out of range\n");
		abort();
	    }
	}
	else if (strcmp("--sizes", argv[i]) == 0) {
	    int k;
	    if (BENV_ArgCheckEnoughArgs(i, argc, argv[i], ndims, printUsage))
		abort();
	    for (k=0; k<ndims; k++) sizes[k] = atoi(argv[++i]);
	}
	else if (strcmp("--distrib-hdf", argv[i]) == 0) {
	    distribkind = 1;
	}
	else if (strcmp("--use-darray", argv[i]) == 0) {
	    useDarray = 1;
	}
	else if (strcmp("--use-subarray-halo", argv[i]) == 0) {
	    useSubarray = 1;
	}
	else if (strcmp("--halowidth", argv[i]) == 0) {
	    if (BENV_ArgCheckEnoughArgs(i, argc, argv[i], 1, printUsage))
		abort();
	    halowidth = atoi(argv[++i]);
	}
	else if (strcmp("--fnamein", argv[i]) == 0) {
	    if (BENV_ArgCheckEnoughArgs(i, argc, argv[i], 1, printUsage))
		abort();
	    fnamein = argv[++i];
	}
	else if (strcmp("--fnameout", argv[i]) == 0) {
	    if (BENV_ArgCheckEnoughArgs(i, argc, argv[i], 1, printUsage))
		abort();
	    fnameout = argv[++i];
	}
	else if (strcmp("--psizes", argv[i]) == 0) {
	    int k;
	    if (BENV_ArgCheckEnoughArgs(i, argc, argv[i], 1, printUsage))
		abort();
	    for (k=0; k<ndims; k++) psizes[k] = atoi(argv[++i]);
	}
	else {
	    fprintf(stderr, "Unrecognized argument %s\n", argv[i]);
	    printUsage(stderr);
	    abort();
	}
    }

    /* Sanity check */
    if (!fnamein) {
	fnamein = "mesh.bin";
    }
    if (!fnameout) {
	fnameout = "meshout.bin";
    }
    for (i=0; i<ndims; i++) {
	if (sizes[i] <= 0) {
	    fprintf(stderr, "sizes[%d] = %d <= 0!\n", i, sizes[i]);
	    abort();
	}
    }


    /* Get the process decomposition */
    for (i=0; i<ndims; i++) periods[i] = 0;
    BENV_CartDecompCreate(ndims, psizes, periods, MPI_COMM_WORLD, 1,
			  &decompctx);
    cartcomm = decompctx->cartcomm;
    for (i=0; i<ndims; i++) {
	psizes[i] = decompctx->psizes[i];
	pcoords[i] = decompctx->pcoords[i];
    }
    MPI_Comm_rank(cartcomm, &crank);
#if 0
    /* sizes are given above, or found with dims create */
    /* form a cartesian communicator, then extract the coordinates */
    MPI_Comm_size(MPI_COMM_WORLD, &wsize);
    /* At this point, an option is to use alternate implementations of
       the process topology functions, since many MPI implementations
       do not provide good implementations of these */
    MPI_Dims_create(wsize, ndims, psizes);
    MPI_Cart_create(MPI_COMM_WORLD, ndims, psizes, periods, 1, &cartcomm);
    MPI_Comm_rank(cartcomm, &crank);
    MPI_Cart_coords(cartcomm, crank, ndims, pcoords);
#endif

    /* Extend to 3d so that we can work with a 3d coordinate set even if
     ndims is 1 or 2 */
    for (i=ndims; i<MAX_DIMS; i++) {
	pcoords[i] = 0;
	psizes[i]  = 1;
    }

    /* Could compare to the "by hand" decomposition as a check */

    /* Define the global mesh */
    /* The mesh sizes are given by sizes */
    /* Nothing else to do here */

    /* Define the local mesh */
    /* Decompose the mesh in each direction, according to the process
       decomposition */
    for (i=0; i<ndims; i++) {
	BENV_DarrayDecomposition(sizes[i], psizes[i], pcoords[i],
				 distribkind, &gindex[i], &larraylen[i]);
    }

    /* Debugging: output the computed decomposition of the array */
    if (debugDecomp) {
	BENV_SeqBegin(cartcomm);
	fprintf(stdout, "%d: pcoords ", crank);
	BENV_PrintIntList(stdout, ndims, pcoords, 0);
	fputs(", array coords ", stdout);
	BENV_PrintIntList(stdout, ndims, gindex, 0);
	fputs(", sizes ", stdout);
	BENV_PrintIntList(stdout, ndims, larraylen, 1);
	fflush(stdout);
	BENV_SeqEnd(cartcomm);
    }

    /* Read in values (canonical order) */
    /* Create the datatype for the file view of the local part of the global
       array */
    /* C_ORDER */
    BENV_DarrayFileType(cartcomm, useDarray, ndims, sizes, psizes,
			larraylen, gindex, MPI_INT, &fileviewtype, &disp);
    if (debugDecomp) {
	MPI_Aint lb, extent;
	fprintf(stdout, "[%d] disp = %ld (%ld ints)\n", crank, (long)disp,
		(long)disp/sizeof(int));
	MPI_Type_get_true_extent(fileviewtype, &lb, &extent);
	fprintf(stdout, "[%d] fileviewtype [%ld,%ld (%ld ints)]\n",
		crank, (long)lb, (long)extent, (long)extent/sizeof(int));
    }

    /* Define a subarray that excludes the halo cells */
    /* Halo width of 1; all larrays have a halo on all sides; this might not
       be necessary on physical boundaries, but is done fo simplicity */
    for (i=0; i<ndims; i++) {
	larraylenhalo[i] = larraylen[i] + 2*halowidth;
	larraystarts[i]  = halowidth;
    }
    MPI_Type_create_subarray(ndims, larraylenhalo, larraylen, larraystarts,
			     MPI_ORDER_C, MPI_INT, &subarraytype);
    MPI_Type_commit(&subarraytype);

    /* Allocate the local mesh buffer (including the halo) */
    lmeshsize = 1;
    for (i=0; i<ndims; i++) lmeshsize *= larraylenhalo[i];
    lmeshbuf = (int *)malloc(lmeshsize * sizeof(int));
    if (!lmeshbuf) {
	if (crank == 0) {
	    BENVi_MallocErr("int", lmeshsize, "lmeshbuf");
	}
	MPI_Abort(cartcomm, 1);
    }
    /* Initialize lmeshbuf. Note: Use first touch if multithreaded. */
    for (i=0; i<2*lmeshsize; i++) lmeshbuf[i] = 0;

    /* Open file and read values */
    err = MPI_File_open(cartcomm, fnamein, MPI_MODE_RDONLY,
			MPI_INFO_NULL, &fhin);
    if (err != MPI_SUCCESS) {
	if (crank == 0) {
	    fprintf(stderr, "Unable to open file %s\n", fnamein);
	    BENV_MPIFileErr(err, "Opening file");
	}
	MPI_Abort(cartcomm, 1);
    }
    err = MPI_File_set_view(fhin, disp, MPI_INT, fileviewtype, "native",
			    MPI_INFO_NULL);
    if (err != MPI_SUCCESS) {
	BENV_MPIFileErr(err, "Unable to set file view!\n");
    }
    /* Read into the subarray (if halowidth==0, could use larraylen, MPI_INT
       as count and type) */
    MPI_File_read_all(fhin, lmeshbuf, 1, subarraytype, &stat);
    MPI_File_close(&fhin);

    /* Compare to expected values (based on values set in ctestinput) */
    /* Comparison is general for 3d. Set values for unused dimensions to
       describe actual data array */
    for (i=ndims; i<MAX_DIMS; i++) {
	larraylen[i]     = 1;
	larraystarts[i]  = 0;
	larraylenhalo[i] = 1;
    }
    for (i=0; i<ndims; i++) larraystarts[i] = halowidth;

    err = compareMesh(ndims, gindex, sizes,
		      larraystarts, larraylen, larraylenhalo, lmeshbuf);

    /* Create types for halo and perform a halo exchange.
       Create types for all cardinal directions in which there is a neighbor
     */
    for (i=0; i<2*ndims; i++) {
	halotype[i]     = MPI_DATATYPE_NULL;
	halotypesend[i] = MPI_DATATYPE_NULL;
	partnerrank[i]  = MPI_PROC_NULL;
    }
    nt = 0;                    /* Index of created datatypes */

    /* distribution and ordering follows process coords.
       In the descriptions below, halowidth is h. The number of mesh
       elements is n0,n1,n2 (larraylen[0] etc.), and the declared array
       size is 2h+ni (e.g., 2h+n0, 2h+n1, and 2h+n2).
    */
    for (i=0; i<ndims; i++) {
	/* Left halo in each dimension  (shift -1) */
	BENV_MeshHaloDatatypes(ndims, i, -1, useSubarray, MPI_INT,
			       pcoords, psizes, halowidth, larraylen,
			       larraylenhalo, &halotype[nt], &halooffset[nt],
			       &halotypesend[nt], &halosendoffset[nt]);
	BENV_CartDecompGetShift(decompctx, i, -1, &partnerrank[nt]);

	/* Right halo in each dimension (shift +1) */
	nt++;
	BENV_MeshHaloDatatypes(ndims, i,  1, useSubarray, MPI_INT,
			       pcoords, psizes, halowidth, larraylen,
			       larraylenhalo, &halotype[nt], &halooffset[nt],
			       &halotypesend[nt], &halosendoffset[nt]);
	BENV_CartDecompGetShift(decompctx, i, +1, &partnerrank[nt]);

	nt++;
    }

    /* FIXME: Consider breaking this out as a separate, check-and-exchange,
       test */
    /* Compare, including the halos (but not the boundaries */
    /* Comparison is general for 3d. Set values for unused dimensions to
       describe actual data array */
    /* FIXME: This comparison probably makes sense only if the sweep
       step does not update any values. */
    {
	int llen[MAX_DIMS], lstarts[MAX_DIMS], gstarts[MAX_DIMS];
	for (i=ndims; i<MAX_DIMS; i++) {
	    llen[i]     = 1;
	    lstarts[i]  = 0;
	    gstarts[i]  = 0;
	}
	/* Define the blocks to check. In the case of a plus stencil,
	   we need to check most of the halo's separately (we can check one
	   dimension at the same time we do the interior) */
	/* in the 0th dimension, define the block with the halo */
	for (i=0; i<ndims; i++) {
	    llen[i]    = larraylen[i];
	    gstarts[i] = gindex[i];
	    lstarts[i] = halowidth;
	}
	if (pcoords[0] > 0) {
	    gstarts[0] -= halowidth;
	    lstarts[0] -= halowidth;
	    llen[0]    += halowidth;
	}
	if (pcoords[0] + 1 < psizes[0]) {
	    llen[0]    += halowidth;
	}
	err = compareMesh(ndims, gstarts, sizes,
			  lstarts, llen, larraylenhalo, lmeshbuf);
	if (ndims > 1) {
	    /* Add the halo in the second (index of 1) dimension */
	    for (i=0; i<ndims; i++) {
		llen[i]    = larraylen[i];
		gstarts[i] = gindex[i];
		lstarts[i] = halowidth;
	    }
	    if (pcoords[1] > 0) {
		llen[1] = halowidth;
		gstarts[1] -= halowidth;
		lstarts[1] -= halowidth;
		err = compareMesh(ndims, gstarts, sizes,
				  lstarts, llen, larraylenhalo, lmeshbuf);
	    }
	    if (pcoords[1] + 1 < psizes[1]) {
		llen[1] = halowidth;
		gstarts[1] = gindex[1] + larraylen[1] - 1;
		lstarts[1] = halowidth + larraylen[1] - 1;
		err = compareMesh(ndims, gstarts, sizes,
				  lstarts, llen, larraylenhalo, lmeshbuf);
	    }
	}
	if (ndims > 2) {
	    /* Add the halo in the third (index of 2) dimension */
	    for (i=0; i<ndims; i++) {
		llen[i]    = larraylen[i];
		gstarts[i] = gindex[i];
		lstarts[i] = halowidth;
	    }
	    if (pcoords[2] > 0) {
		llen[2] = halowidth;
		gstarts[2] -= halowidth;
		lstarts[2] -= halowidth;
		err = compareMesh(ndims, gstarts, sizes,
				  lstarts, llen, larraylenhalo, lmeshbuf);
	    }
	    if (pcoords[2] + 1 < psizes[2]) {
		llen[2] = halowidth;
		gstarts[2] = gindex[2] + larraylen[2] - 1;
		lstarts[2] = halowidth + larraylen[2] - 1;
		err = compareMesh(ndims, gstarts, sizes,
				  lstarts, llen, larraylenhalo, lmeshbuf);
	    }
	}
    }

    /* Open a new file and write out values */
    err = MPI_File_open(cartcomm, fnameout, MPI_MODE_WRONLY + MPI_MODE_CREATE,
			MPI_INFO_NULL, &fhout);
    if (err != MPI_SUCCESS) {
	if (crank == 0) {
	    fprintf(stderr, "Unable to open file %s\n", fnameout);
	    BENV_MPIFileErr(err, "File open");
	}
	MPI_Abort(cartcomm, 1);
    }
    MPI_File_set_view(fhout, disp, MPI_INT, fileviewtype, "native",
		      MPI_INFO_NULL);
    MPI_File_write_all(fhout, lmeshbuf, 1, subarraytype, &stat);
    MPI_File_close(&fhout);

    /* Exit */
    MPI_Type_free(&subarraytype);
    MPI_Type_free(&fileviewtype);
    for (i=0; i<nt; i++) {
	if (halotype[i] != MPI_DATATYPE_NULL) {
	    MPI_Type_free(&halotype[i]);
	    MPI_Type_free(&halotypesend[i]);
	}
    }
    decompctx->free(decompctx);

    MPI_Finalize();
    return 0;
}

void printUsage(FILE *fp)
{
    fprintf(fp, "\
meshio [--dims n] [--sizes n1 ...]\n\
       [--distrib-hdf] [--use-darray] [--use-subarray-halo]\n\
       [--halowidth n] [--fnamein filename]\n\
       [--fnameout filename] [--psizes p1 ... ] \n");
}

/* Compare the section buf to the expected values (consequtive values in the
   global mesh) */
int compareMesh(int ndims, const int coords[], const int gsizes[],
		const int loffsets[], const int lsizes[],
		const int lsizesdecl[], const int *buf)
{
    int i, j, k;
    int gval, bval, errcnt=0;
    int ntested = 0;

    if (debugMesh) {
	fprintf(stdout, "lsizes = %d, %d, %d\n",
		lsizes[0], lsizes[1], lsizes[2]);
    }
    /* C Order */
    for (i=0; i<lsizes[0]; i++) {
	for (j=0; j<lsizes[1]; j++) {
	    for (k=0; k<lsizes[2]; k++) {
		/* Comparison value */
		gval = idx3(coords[0]+i,coords[1]+j,coords[2]+k,
			    gsizes[0],gsizes[1],gsizes[2]);
		bval = buf[idx3(loffsets[0]+i,loffsets[1]+j,loffsets[2]+k,
				lsizesdecl[0],lsizesdecl[1],lsizesdecl[2])];
		ntested++;
		if (gval != bval) {
		    errcnt++;
		    if (errcnt < maxErrors) {
			fprintf(stderr, "Value at global(%d,%d,%d), local (%d,%d,%d) = %d, expected %d\n",
				coords[0]+i, coords[1]+j, coords[2]+k,
				i, j, k, bval, gval);
		    }
		    else if (errcnt == maxErrors) {
			fprintf(stderr, "max errors reached; output suppressed\n");
		    }
		}
	    }
	}
    }
    if (debugMesh) {
	fprintf(stdout, "Tested %d values\n", ntested);
	fflush(stderr);
    }
    return errcnt;
}

