#ifndef TARRAY_H_INCLUDED
#define TARRAY_H_INCLUDED 1

#define MAX_TA_DIMS 3

typedef struct {
    double *data;
    int    ndims, dims[MAX_TA_DIMS];
} TActx;

TActx *BENV_TAInit(int, int, int);
int BENV_TASetVal3(TActx *, int, int, int, double);
void BENV_TAFree(TActx *);

int BENV_TAPrintRaw(FILE *fp, TActx *ta);

int BENV_TAFindQuartiles(int n, int stride, const double *vals,
			 double quartiles[5]);

/* Graphing routines */
int BENV_TAPrintCandlestickData(FILE *fp, int n, const int *xval,
			const double *qvals);
int BENV_TAPlotCandlestickData(FILE *datafp, FILE *cmdfp, const char *dfname,
			       int n, const int *xval, const double *qvals);

#endif
