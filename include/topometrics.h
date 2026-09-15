#ifndef TOPOMETRICS_H_INCLUDED
#define TOPOMETRICS_H_INCLUDED 1
int BENV_GetNonLocalCounts(MPI_Comm comm, int nr, const int ranks[],
			   MPI_Comm localcomm, int *nlocal, int *nnonlocal,
			   int **localranks, int **nonlocalranks);
int BENV_PrintNonLocalCounts(FILE *fp, MPI_Comm comm,
			     int nr, const int ranks[],
			     int nlocal, MPI_Comm localcomms[]);
int BENV_GetMinMaxAvg(MPI_Comm comm, const int val[], int nval,
		      int minval[], int maxval[], double avgval[]);

#endif
