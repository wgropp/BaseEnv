/* */

#include "benvconf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mpi.h"
#include "benvutil.h"
#include "ntest.h"
#include "tarray.h"
#include "benvmem.h"
#ifdef USE_OLD
#include "hwdesc.h"
#else
#include "hwdescnew.h"
#endif

typedef struct {
    int verbose;     /* Provide more information about the operation of code */
//    int printMap;    /* Print the mapping of processes to the topology */
    int printNtest;
    int nMsgSizes;   /* Number of message sizes provided */
    int nTrials;     /* Repeat the test this many times */
    int checkData;   /* If true, check that data is communicated correctly */
    int checkOnHost; /* If true, copy data from device to host and check there */
    MemObj_type mtype; /* Memory type (e.g., malloc on CPU or GPU) */
    ntestctx_t *ntestctx;
#ifdef USE_OLD
    hwdescParms_t hwparms; /* Parameters for hwdesc creation */
#else
    hwdescParms hwparms; /* Parameters for hwdesc creation */
#endif
    int *msgsizes;   /* Message sizes to use */
    const char *outName;   /* Name for output file.  If null, use stdout */
    const char *outRawName; /* Name for output of raw date. If null, no output */
//    const char *mapname;   /* Name for output of process map */
} options_t;

int getOptions(int argc, char **argv, options_t *options);
void printUsage(void);
void checkData(options_t *options, MemObj_t *morecv, int, int, int,
	       long msglen);
void printDoubleArray(FILE *fp, const double *mem, int n);
void reportOnErrorValue(MemObj_t *mo, int rc, long msglen, int trial);

/* Working file for example code that performs pingpong communication
   that might use memory in annother device (accelerator)
*/
int main(int argc, char **argv)
{
    int provided, wrank, srcrank, destrank;
    size_t maxlenbytes;
    options_t options;
    MemObj_t *mosend, *morecv;
    TActx *ta;

    /* Initialize MPI */
    MPI_Init_thread(&argc, &argv, MPI_THREAD_FUNNELED, &provided);
    MPI_Comm_rank(MPI_COMM_WORLD, &wrank);

    /* Create and initialize ntest count */
    options.ntestctx = BENV_NtestInit(MPI_COMM_WORLD);

    /* Process command line */
    getOptions(argc, argv, &options);

    BENV_NtestInitInfo(options.ntestctx, options.nMsgSizes);

    /* Determine active processes and get hw info */
    srcrank  = 0;
    destrank = 1;
    if (options.hwparms.printMap) {
#ifdef USE_OLD
	BENV_HwdescPrintInfo(options.hwparms.mapname, 0);
#else
	fprintf(stderr, "printmat not supported!\n");
	/* This should really set a flag, then get the hwdescCtx, then
	   do a print all */
#endif
    }

    /* Any device initialization for the given memory type */
    BENV_MemDeviceInit(options.mtype);

    /* Create and initialize timing data */
    ta = BENV_TAInit(1, options.nMsgSizes, options.nTrials);

    /* Allocate largest memory objects (send and recv) */
    maxlenbytes = options.msgsizes[options.nMsgSizes-1] * sizeof(double);
    mosend = BENV_MemAlloc(maxlenbytes, options.mtype);
    morecv = BENV_MemAlloc(maxlenbytes, options.mtype);

    /* Iterate over tests, message sizes (? which order?), recording
       timing data. Note distribute tests with the same message length
       in time to get better average, e.g., avoid sampling during the same
       disturbance */
    /* By starting trials at -1 and only recording timing starting with
       trial 0, we include a startup run. In practice (on some systems),
       the first run (trial -1) takes significantly longer than the
       subsequent runs */
    for (int trial=-1; trial<options.nTrials; trial++) {
	for (int lidx=0; lidx<options.nMsgSizes; lidx++) {
	    long msglen;
	    int  ntest;
	    double t0;
	    msglen = options.msgsizes[lidx];
	    /* Determine the number of times to run this test */
	    ntest = BENV_NtestGetVal(options.ntestctx, msglen, 0);
	    /* Initialize data starting with rank*msglen */
	    BENV_MemInitValue1D(mosend, 2*msglen, wrank*msglen, 0, msglen,
				0.0, 1.0);
	    /* Init the receive buffer so we can spot unset entries */
	    BENV_MemInitValue1D(morecv, 2*msglen, wrank*msglen, 0, msglen,
				-1.0, -1.0);
	    MPI_Barrier(MPI_COMM_WORLD);
	    t0 = MPI_Wtime();
	    for (int iter=0; iter<ntest; iter++) {
		if (wrank == srcrank) {
		    MPI_Send(mosend->memptr, msglen, MPI_DOUBLE, destrank, iter,
			     MPI_COMM_WORLD);
		    MPI_Recv(morecv->memptr, msglen, MPI_DOUBLE, destrank, iter,
			     MPI_COMM_WORLD, MPI_STATUS_IGNORE);
		}
		else if (wrank == destrank) {
		    MPI_Recv(morecv->memptr, msglen, MPI_DOUBLE, srcrank, iter,
			     MPI_COMM_WORLD, MPI_STATUS_IGNORE);
		    MPI_Send(mosend->memptr, msglen, MPI_DOUBLE, srcrank, iter,
			     MPI_COMM_WORLD);
		}
	    }
	    t0 = MPI_Wtime() - t0;

	    /* Option to check values in receive buffer */
	    checkData(&options, morecv, srcrank, destrank, trial, msglen);

	    /* Save ntest info (keep only the largest time) */
	    BENV_NtestSaveInfo(options.ntestctx, lidx, ntest, t0);

	    /* Save timing info */
	    t0 = t0 / ntest;
	    if (trial >= 0) /* Only after the first run */
		BENV_TASetVal3(ta, 0, lidx, trial, t0);
	}
    }

    /* Process timing data */
    /* Noting to do here */
    /* Output data to file */
    if (wrank == srcrank) {
	/* temp while figuring out what to do */
	FILE *outfp;

	/* Output all of the data if requested */
	if (options.outRawName) {
	    outfp = fopen(options.outRawName, "w");
	    BENV_TAPrintRaw(outfp, ta /* Add option for format, including binary*/);
	    fclose(outfp);
	}

	outfp = fopen(options.outName, "w");
	double *q = (double *)malloc(options.nMsgSizes*5*sizeof(double));
	int offset =0;
	for (int lidx=0; lidx<options.nMsgSizes; lidx++) {
	    BENV_TAFindQuartiles(options.nTrials, 1, ta->data + offset, q + 5*lidx);
	    offset += options.nTrials;
	}
	BENV_TAPrintCandlestickData(outfp, options.nMsgSizes, options.msgsizes,
				    q);
	free(q);

	/* ? gnuplot instructions? */
	/* */
	if (options.printNtest) {
	    /* Query: to a separate file? */
	    BENV_NtestPrintInfo(outfp, options.ntestctx, options.nMsgSizes,
				options.msgsizes);
	}
	fclose(outfp);
    }

    /* Free objects and exit */
    BENV_TAFree(ta);
    BENV_MemFree(mosend);
    BENV_MemFree(morecv);

    return 0;
}

int getOptions(int argc, char **argv, options_t *options)
{
    /* Set defaults */
    options->verbose       = 0;
//    options->printMap      = 0;
//    options->mapname       = 0;
    options->nTrials       = 10;
    options->nMsgSizes     = 0;
    options->msgsizes      = 0;
    options->outName       = 0;
    options->outRawName    = 0;
    options->checkData     = 0;
    options->checkOnHost   = 0;
    options->mtype         = MEMOBJ_MALLOC;

    BENV_HwdescCvarInit();

    for (int i=1; i<argc; i++) {
	int rc;
	/* Look for common options */
	rc = BENV_NtestArg(argc, argv, &i, 0, options->ntestctx);
	BENV_ARGCHECK(rc,"error in ntest options",return 1);
	rc = BENV_MemArg(argc, argv, &i, &options->mtype);
	BENV_ARGCHECK(rc,"error in memobj options",return 1);
	rc = BENV_HwdescArg(argc, argv, &i, 0, &options->hwparms);
	BENV_ARGCHECK(rc,"error in hwdesc options",return 1);

	if      (strcmp(argv[i], "-v") == 0)  {
	    /* This allows multiple -v options to set higher levels of
	       debugging */
	    options->verbose++;
	}
	else if (strcmp(argv[i], "-check") == 0) {
	    options->checkData = 1;
	}
	else if (strcmp(argv[i], "-checkonhost") == 0) {
	    options->checkOnHost = 1;
	}
	else if (strcmp(argv[i], "-sizes") == 0) {
	    i++;
	    if (i < argc) {
		options->msgsizes = BENV_RstringToArray(argv[i],
							&options->nMsgSizes);
	    }
	    else {
		fprintf(stderr, "-sizes missing value\n");
		fflush(stderr);
		return 1;
	    }
	}
	else if (strcmp(argv[i], "-ntrials") == 0) {
	    i++;
	    if (BENV_ArgGetint(i, argc, argv, &options->nTrials))
		return 1;
	}
	else if (strcmp(argv[i], "-o") == 0) {
	    i++;
	    if (i < argc)
		options->outName = strdup(argv[i]);
	    else {
		fprintf(stderr, "-o missing value\n");
		fflush(stderr);
		return 1;
	    }
	}
	else if (strcmp(argv[i], "-oraw") == 0) {
	    i++;
	    if (i < argc)
		options->outRawName = strdup(argv[i]);
	    else {
		fprintf(stderr, "-oraw missing value\n");
		fflush(stderr);
		return 1;
	    }
	}
	else if (strcmp(argv[i], "-help") == 0 ||
		 strcmp(argv[i], "-usage") == 0) {
	    printUsage();
	    return 1;
	}
	else {
	    int wrank;
	    MPI_Comm_rank(MPI_COMM_WORLD, &wrank);
	    if (wrank == 0) {
		fprintf(stderr, "Unrecognized option %s\n", argv[i]);
		printUsage();
		fflush(stderr);
	    }
	    return 1;
	}
    }

    /* Handle any defaults that are not set above */
    if (options->nMsgSizes == 0)
	options->msgsizes = BENV_GetSizesMult(1, 32*1024, 2.0,
					     &options->nMsgSizes);
    if (!options->outName)
	options->outName = strdup("pingpong.dat");
    if (options->hwparms.printMap && !options->hwparms.mapname)
	options->hwparms.mapname = strdup("pingpong.map");

    return 0;
}

void printUsage(void)
{
    fprintf(stderr, "\
pingpong - Test MPI communication from CPU and GPUs\n");
    BENV_NtestArgPrintUsage(stderr, 0);
    BENV_MemArgPrintUsage(stderr);
    BENV_HwdescArgPrintUsage(stderr, 0, -1);
    fprintf(stderr, "\
 -v - Set verbose output\n\
 -check - Check that the data is correctly received\n\
 -checkonhost - Move data from device to host and then check that the data\n\
                has been correctly received\n\
 -sizes sizearg - Specify the number of doubles to send. May be a range,\n\
                  list, or combination\n\
 -ntrials - Run each test this number of times\n\
 -o filename - Send output to the named file\n\
 -oraw filename - Send the raw data to the named file\n\
 -help or -usage - Print this information\n");
}

/* Temp for debugging */
void printDoubleArray(FILE *fp, const double *mem, int n)
{
    for (int i=0; i<n; i++) {
	fprintf(fp, "%e, ", mem[i]);
    }
    fputc('\n', fp);
}

/* Check the received data is correct */
void checkData(options_t *options, MemObj_t *morecv, int srcrank, int destrank,
	       int trial, long msglen)
{
    int wrank, lstart, rc=0;

    MPI_Comm_rank(MPI_COMM_WORLD, &wrank);

    /* Ignore ranks without data */
    if (wrank != srcrank && wrank != destrank) return;

    /* Handle options of where the check is performed */
    if (wrank == srcrank) lstart = destrank;
    else                  lstart = srcrank;
    /* Allow both check on device and check on host */
    if (options->checkData) {
	/* Check against the other process */
	rc = BENV_MemCheckValue1D(morecv, 2*msglen, lstart*msglen, 0,
				  msglen, 0.0, 1.0);
	if (rc > 0) {
	    fprintf(stderr, "Error found in check on device:\n");
	    reportOnErrorValue(morecv, rc, msglen, trial);
	}
    }
    if (options->checkOnHost) {
	rc = BENV_MemCheckValue1DWithHost(morecv, 2*msglen, lstart*msglen, 0,
					  msglen, 0.0, 1.0);
	if (rc > 0) {
	    fprintf(stderr, "Error found in check on host:\n");
	    reportOnErrorValue(morecv, rc, msglen, trial);
	}
    }
}

void reportOnErrorValue(MemObj_t *mo, int rc, long msglen, int trial)
{
    int low, nval, wrank;
    double *lmem;

    MPI_Comm_rank(MPI_COMM_WORLD, &wrank);
    fprintf(stderr, "For msglen = %ld and trial = %d\n",
	    msglen, trial);
    fprintf(stderr, "[%d] recv buffer value incorrect at %d\n",
	    wrank, rc - 1);
    low = rc-3;
    if (low < 0) low = 0;
    nval = 7;
    if (low+nval > msglen) nval = msglen-low;
    /* Need to copy memory if on device */
    if (mo->t != MEMOBJ_MALLOC) {
	lmem = (double *)malloc(nval * sizeof(double));
	mo->memfromdev(lmem, (double*)(mo->memptr) + low,
		       nval*sizeof(double));
    }
    else {
	lmem = (double *)(mo->memptr) + low;
    }
    for (int i=low; i<low+nval; i++)
	fprintf(stderr, "mem[%d]\t", i);
    fputc('\n', stderr);
    printDoubleArray(stderr, lmem, nval);
    if (mo->t != MEMOBJ_MALLOC) {
	free(lmem);
    }
}
