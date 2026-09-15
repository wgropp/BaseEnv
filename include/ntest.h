#ifndef NTEST_H_INCLUDED
#define NTEST_H_INCLUDED 1

/* ToDo: Make these opaque */
/* This type lets us save information about the computed values of ntest
   Typically used in an array, one for each varient (e.g., message size) */
typedef struct {
    int    ntestval;      /* ntest value used */
    double acttime;       /* actual time for test */
} ntestinfo_t;

typedef struct {
    double ntestWT,    /* Multiple of Wtick to use in determining ntest */
	ntestS,        /* Latency to use in determining ntest */
	ntestR,        /* Inverse bandwidth to use in determining ntest */
	ntestWtick;    /* Use this value for wtick instead of MPI_Wtick */
    int ntestMIN,
	ntestMAX;      /* These provide a valid range for ntest values */
    int ntestinfo;     /* if true, ntestinfo was also collected in ninfo */
    int ninfolen;      /* number of entries in ninfo */
    ntestinfo_t *ninfo;/* If allocated, save information about the tests */
} ntestctx_t;

ntestctx_t *BENV_NtestInit(MPI_Comm comm);
void BENV_NtestFree(ntestctx_t *);
int BENV_NtestInitPostMPIInit(ntestctx_t *ctx, MPI_Comm comm);
int BENV_NtestGetVal(ntestctx_t *ctx, int msgsize, int nc);
double BENV_NtestTimeEst(ntestctx_t *ctx, int len);
int BENV_NtestPrintInfo(FILE *fp, ntestctx_t *ctx, int nmsgs,
			const int msgsizes[]);
int BENV_NtestArg(int argc, char **argv, int *argcnt, const char *prefix,
		  ntestctx_t *ctx);
void BENV_NtestArgPrintUsage(FILE *fp, const char *prefix);
int BENV_NtestArgConfig(const char *arg, const char *newname);
int BENV_NtestInitInfo(ntestctx_t *ctx, int nmsgs);
int BENV_NtestSaveInfo(ntestctx_t *ctx, int midx, int nval, double t);

#endif

