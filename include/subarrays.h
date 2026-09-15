#ifndef SUBARRAYS_H_INCLUDED
#define SUBARRAYS_H_INCLUDED
int BENV_CreateSubarray(int ndims, const int gsize[], const int lsize[],
			const int lstart[], int order, MPI_Datatype oldtype,
			MPI_Datatype *newtype, MPI_Aint *offset);
#endif
