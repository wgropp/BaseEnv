#ifndef GETSIZES_H_INCLUDED
#define GETSIZES_H_INCLUDED 1

/* Common arg processing for message and other sizes (getsizes.c) */
int *BENV_RstringToArrayOLD(char *rstring, int *nsizes);
int *BENV_RstringToArray(const char *rstring, int *nsizes);
int *BENV_GetSizes(int argc, char *argv[], const char *argname, int *nsizes);
int *BENV_GetSizesArith(int start, int end, int stride, int *nsizes);
int *BENV_GetSizesMult(int start, int end, double factor, int *nsizes);
int *BENV_GetSizesMultDelta(int start, int end, double factor, int delta,
			    int ndelta, int *nsizes);

int BENV_GetNrepsEst(double deltat, double latency, double bandwidth,
		     int nmsgs, int msglen, int minreps);
int BENV_ScanScaledInt(const char **str, int usepwr2, int *err);

#endif
