#ifndef MESHSUP_H_INCLUDED
#define MESHSUP_H_INCLUDED 1

int BENV_DarrayDecomposition(int arraylen, int nproc, int idxproc,
			     int distribkind, int *arrayidx, int *larraylen);
int BENV_DarrayFileType(MPI_Comm comm, int useDarray,
			int ndims, const int sizes[], const int psizes[],
			const int larraylen[], const int gindex[],
			MPI_Datatype basetype, MPI_Datatype *fileviewtype,
			MPI_Offset *disp);
int BENV_MeshHaloDatatypes(int ndims, int whichdim, int shift, int useSubarray,
			   MPI_Datatype basetype,
			   const int pcoords[], const int psizes[],
			   int halowidth, const int larraylen[],
			   const int larraylenhalo[],
			   MPI_Datatype *halotyperecv, MPI_Aint *recvoffset,
			   MPI_Datatype *halotypesend, MPI_Aint *sendoffset);

#endif
