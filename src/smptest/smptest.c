/*
 * Copyright (C) by University of Illinois 2025
 */
#include "benvconf.h"
#include "mpi.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _OPENMP
#include <omp.h>
#endif
#include "benvutil.h"
#include "benvmpiutil.h"
#include "ntest.h"
#ifdef USE_OLD
#include "hwdesc.h"
#include "nodeinfo.h"
#else
#include "hwdescnew.h"
#endif
#include "getsizes.h"
#include "benvdbg.h"
#include "arrindex.h"

CDBGDECL(SMPTEST);

/* FIXME: see baseenv/haloperf/nodecomm.c for some features not currently
   included here. These include:
   verbose - Provide more information. The verbose flag is recognized but
   no code takes advantage of it.
 */
/* OPTIONS_N_INTS is the number of integers in the options structure to
   broadcast to the other processes.  Make sure to update this define
   when any changes are made to the structure. Similar for the doubles */
#define OPTIONS_N_INTS /*17*/ 12
#define OPTIONS_N_DOUBLES /*4*/ 0
typedef struct {
    int verbose;     /* Provide more information about the operation of code */
    int debug;       /* Use an artificial mesh topology for debugging */
    int progress;    /* If true, show progress while running tests */
    int printMap;    /* Print the mapping of processes to the topology */
    int isThreaded;  /* Set if use multiple threads for communication */
    int isNonblocking; /* Set if use nonblocking communication */
    int nMsgSizes;   /* Number of message sizes provided */
    int nProcNum;    /* Number of elements in nprocarray */
    int nTrials;     /* Repeat the test this many times */
#if 0
    int ntestMIN;    /* ntest must be at least this size */
    int ntestMAX;    /* ntest much be no larger than this size */
    int ntestinfo;   /* if true, write ntestinfo to same output as outName */
#endif
    int isOverlapped;/* Set if communication is overlapped with work */
    int overlapWork; /* Determines the amount of work */
    int outtime;     /* Determines which output data to provide. Bit mask */
#if 0
    double ntestWT;  /* Multiple of Wtick to use in determining ntest */
    double ntestS;   /* Latency to use in determining ntest */
    double ntestR;   /* Inverse bandwidth to use in determining ntest */
    double ntestWtick; /* Use this value for wtick instead of MPI_Wtick */
#endif
    ntestctx_t *ntestctx;
    int *msgsizes;   /* Message sizes to use */
    int *nprocarray; /* Array of number of processes for tests */
    char *mapName;   /* Name for the mapping file. Only used on rank 0 */
    char *outName;   /* Name for output file.  If null, use stdout */
#ifdef USE_OLD
    hwdescParms_t parms;  /* Used to control how the hardware description is
			     determined */
#else
    hwdescParms parms;    /* Used to control how the hardware description is
			     determined */
#endif
} options_t;

#if 0
/* This type lets us save information about the computed values of ntest */
typedef struct ntestinfo_t {
    int    ntestval;      /* ntest value used */
    double acttime;       /* actual time for test */
} ntestinfo_t;
#endif

/* States for the communication call. */
enum cstate_t { IS_MASTER, IS_PARTNER, IS_IDLE };
typedef enum cstate_t commstate_t;

/* Communication test routine */
typedef double (commroutine_t)(MPI_Comm, commstate_t st, int mrank, int prank,
			       int msgsize, int ntest, int *sbuf, int *rbuf);

/* Routine to add computation during communication (non-blocking
   communication only) */
void dowork(int len);
void doworkinit(int maxlen, int parm);

#if 0
/* We use this to simplify two-dimensional indexing into an array */
#define IND2D(_i,_j,_ncol) ((_j)+(_i)*(_ncol))
#define IND3D(_i,_j,_k,_n2,_n3) ((_k)+((_n3)*((_j)+(_n2)*(_i))))
#endif

int getOptions(int argc, char *argv[], options_t *options);
#ifdef USE_OLD
int checkNodeDecomp(hwdescCtx_t *hwc);
int getPartners(hwdescCtx_t *hwc, int **masterranks, int **partnerranks,
		int *nranks);
int printReport(FILE *, MPI_Comm comm, hwdescCtx_t *hwc,
		int nranks, int *masterranks, int *partnerranks,
		int nprocnum, int *nprocarray,
		int nmsgsizes, int *msgsizes, double *timeArray);
#else
int checkNodeDecomp(hwdescCtx *hwc);
int getPartners(hwdescCtx *hwc, int **masterranks, int **partnerranks,
		int *nranks);
int printReport(FILE *, MPI_Comm comm, hwdescCtx *hwc,
		int nranks, int *masterranks, int *partnerranks,
		int nprocnum, int *nprocarray,
		int nmsgsizes, int *msgsizes, double *timeArray);
#endif
int runTests(MPI_Comm comm, int nmsgsizes, const int msgsizes[],
	     int nranks, const int masterranks[], const int partnerranks[],
	     int nprocnum, const int nprocarray[],
	     commroutine_t *commroutine, double *timeArray,
	     /*ntestinfo_t *ntestinfo*/ntestctx_t *nctx);
int examineTiming(MPI_Comm comm, double *timeArray, int nmsgsize, int nranks,
		  int nprocnum, int nTrials, double **tarray);

double commBlockingSend(MPI_Comm comm, commstate_t st, int mrank, int prank,
			int len, int ntest, int *sbuf, int *rbuf);
double commNonblockingSend(MPI_Comm comm, commstate_t st, int mrank, int prank,
			   int len, int ntest, int *sbuf, int *rbuf);
#ifdef _OPENMP
double commBlockingSendThreaded(MPI_Comm comm, commstate_t st,
				int mrank, int prank,
				int len, int ntest, int *sbuf, int *rbuf);
#endif
#if 0
int ntestInit(options_t *options, MPI_Comm comm);
int ntestGetVal(int msgsize, int nc);
double ntestTimeEst(int len);
int ntestPrintInfo(FILE *fp, int nmsgs, int msgsizes[], ntestinfo_t ntestinfo[]);
#endif
void printUsage(void);

#if 0
/* For simplicity in debugging */
static int verbose = 0, dbg_wrank;
#endif

/* If true (see getOptions --progress), wrank==0 gives indication of progress */
static int showProgress = 0;

/*
 * This is an updated version of mpptest.
 *
 * It adds support for measuring performance between all processes on a
 * node, communicating with their counterparts on another node.
 *
 * It does not include many of the adaptive features of mpptest
 */

int main(int argc, char **argv)
{
#if 0
    hwdesc_t hw[MAX_HW_DEPTH];
#else
#ifdef USE_OLD
    hwdescCtx_t *hwc=0;
#else
    hwdescCtx *hwc=0;
#endif
//    hwdescParms_t parms;
    int      flag;
#endif
    int      rc, required, provided, wsize, wrank;
    int      *masterranks, *partnerranks, nranks;
    options_t options;
    double   *timeArray = 0;
    int      tsize; /* Number of elements in a single block of timeArray */
#if 0
    ntestinfo_t *ntestinfo=0;
#endif
    commroutine_t *commroutine = commBlockingSend;

    /* Create and initialize ntest count */
    options.ntestctx = BENV_NtestInit(MPI_COMM_NULL);

    /* Get command line information, including message size range */
    if (getOptions(argc, argv, &options)) {
	/* We can't use MPI_Abort until we call MPI_Init_thread */
	return 1;
    }

    if (options.isThreaded)
	required = MPI_THREAD_MULTIPLE;
    else
	required = MPI_THREAD_SINGLE;
    MPI_Init_thread(&argc, &argv, required, &provided);
    MPI_Comm_rank(MPI_COMM_WORLD, &wrank);
    MPI_Comm_size(MPI_COMM_WORLD, &wsize);

    if (options.isThreaded && provided != MPI_THREAD_MULTIPLE) {
	if (wrank == 0)
	    fprintf(stderr,
	    "MPI_THREAD_MULTIPLE not provided and required for thread tests\n");
	MPI_Abort(MPI_COMM_WORLD, 1);
    }
    /* In most MPI implementations, all processes will have the options.
       But for best portability, rank 0 will broadcast to the others. Note
       that mapName and outName are only used on rank 0, and is not broadcast.
       The values in options are assumed to be 7 consecutively stored
       integers */
    MPI_Bcast(&options, OPTIONS_N_INTS, MPI_INT, 0, MPI_COMM_WORLD);
#if 0
    MPI_Bcast(&options.ntestWT, OPTIONS_N_DOUBLES,
	      MPI_DOUBLE, 0, MPI_COMM_WORLD);
#endif
    /* FIXME: Still need to handle the hwdesc parms */

    /* Get the msg sizes if defined as an array of values */
    if (options.nMsgSizes > 0)
	MPI_Bcast(options.msgsizes, options.nMsgSizes, MPI_INT,
		  0, MPI_COMM_WORLD);
    else {
	/* Create the array of values from 1 to 32*1024 by powers of 2 */
	options.msgsizes = BENV_GetSizesMult(1, 32*1024, 2.0,
					     &options.nMsgSizes);
    }

    /* Complete the initialization of the ntest routines */
    BENV_NtestInitPostMPIInit(options.ntestctx, MPI_COMM_WORLD);
    BENV_NtestInitInfo(options.ntestctx, options.nMsgSizes);

    /* nprocarray set after getPartners */
#if 0
    ntestInit(&options, MPI_COMM_WORLD);
    ntestinfo = (ntestinfo_t *)malloc(options.nMsgSizes * sizeof(ntestinfo_t));
#endif

    /* Set debugging options */
#if 0
    verbose   = options.verbose;
    dbg_wrank = wrank;
#endif
    BENV_DebugPostMPIInit();
    if (options.progress && wrank == 0) showProgress = 1;

    /* Pick alternate test routines if requested */
    if (options.isNonblocking)
	commroutine = commNonblockingSend;
#ifdef _OPENMP
    else if (options.isThreaded)
	commroutine = commBlockingSendThreaded;
#endif

    CDBG(SMPTEST,BASIC,"Done with options");

    /* Get information on the nodes and processes, so that we can
       determine the communication partners */
    /* First set any debugging options */
#if 0
    if (options.debug) {
	BENV_NodeCvarSet("ppn", wsize/2);
	BENV_NodeCvarSet("debug", 1);
    }
    rc = BENV_NodeGetComms(MPI_COMM_WORLD, hw, MAX_HW_DEPTH, &hwdepth);
#else
    /* Set objs for a default of 2 nodes */
    options.parms.nobjs[0] = 2;
    options.parms.nobjs[1] = 1;
    options.parms.nobjs[2] = 1;
    options.parms.nnobj    = 3;
    options.parms.policy   = 0;
    flag = BENV_HWDESC_USE_ALL;
    if (options.debug) {
	flag = BENV_HWDESC_USE_DEBUG;
    }
    if (options.verbose)
	BENV_HwdescCvarSet("debug", 1);
    BENV_HwdescCvarInit();
    rc = BENV_HwdescGetDescGeneral(MPI_COMM_WORLD, flag, &options.parms, &hwc);
#endif

    CDBGV(SMPTEST,BASIC,"Done with GetDescGeneral (depth = %d)\n",
#ifdef USE_OLD
 hwc->hwlevel
#else
 hwc->nlevel
#endif
);

    /* Sanity check that we have a usable decomposition */
    rc = checkNodeDecomp(hwc);
    if (rc) {
	MPI_Abort(MPI_COMM_WORLD, 1);
    }

    /* Determine the communication partners */
    rc = getPartners(hwc, &masterranks, &partnerranks, &nranks);

    /* Check that nprocnum and nprocarray is consistent. If nprocnum
     was not set, set it here. */
    /* Get the number of processes to use for the tests */
    if (options.nProcNum > 0) {
	/* Validate values: in range and monotone increasing */
	if (wrank == 0) {
	    int prevval = -1;
	    for (int i=0; i<options.nProcNum; i++) {
		int curval = options.nprocarray[i];
		if (curval < 1 || curval > nranks) {
		    fprintf(stderr, "Invalid nprocs value[%d]=%d\n",
			    i, options.nprocarray[i]);
		    MPI_Abort(MPI_COMM_WORLD, 1);
		}
		if (curval <= prevval) {
		    fprintf(stderr, "Nonmonotone nprocs values %d <= %d\n",
			    curval, prevval);
		    MPI_Abort(MPI_COMM_WORLD, 1);
		}
		prevval = curval;
	    }
	}
	/* Values are valid; ensure all processes have them. */
	MPI_Bcast(options.nprocarray, options.nProcNum, MPI_INT,
		  0, MPI_COMM_WORLD);
    }
    else {
	/* Create the array of values from 1 to the number of processes
	   selected */
	options.nprocarray = BENV_GetSizesArith(1, nranks, 1,
						&options.nProcNum);
    }


    CDBGV(SMPTEST,BASIC,"Done with getPartners (nranks = %d)\n", nranks);
    tsize     = options.nProcNum * options.nMsgSizes;
    timeArray = (double *)malloc(tsize*options.nTrials*sizeof(double));
    if (!timeArray) {
	fprintf(stderr, "Unable to allocate timeArray\n");
	fflush(stderr);
	MPI_Abort(MPI_COMM_WORLD, 0);
    }
    /* Perform the tests, saving the timing data.
       Includes option to run tests multiple times, not just as an average
       over runs at a give size.
       Pass the function used to perform the communication. The outer framework
       is the same. On the process with rank 0 in hw[0].comm, fills in
       timerArray (nranks x nMsgSizes array of doubles) with the time
       returned by commroutine.
    */
    if (options.isOverlapped) {
	int maxlen = 0, i;
	for (i=0; i<options.nMsgSizes; i++) {
	    if (maxlen < options.msgsizes[i]) maxlen = options.msgsizes[i];
	}
	doworkinit(maxlen, options.overlapWork);
    }
    for (int trial=0; trial<options.nTrials; trial++) {
	CDBGV(SMPTEST,BASIC,"Running tests for trial %d\n", trial);
	if (showProgress) fprintf(stdout, "Trial %d\n", trial);
	rc = runTests(
#ifdef USE_OLD
	    hwc->hw[0].comm,
#else
	    hwc->collinfo[0].objcomm,
#endif
		      options.nMsgSizes, options.msgsizes,
		      nranks, masterranks, partnerranks,
		      options.nProcNum, options.nprocarray,
		      commroutine,
		      timeArray+trial*tsize, /*ntestinfo*/options.ntestctx);
    }
    CDBG(SMPTEST,BASIC,"Tests completed");

    /* Generate report */
    {
    FILE *fp;
    double *tarray=0;
    if (wrank == 0) {
	if (options.outName) {
	    fp = fopen(options.outName, "w");
	    if (!fp) {
		fprintf(stderr, "Could not open %s for output\n",
			options.outName);
		MPI_Abort(MPI_COMM_WORLD, 1);
	    }
	}
	else fp = stdout;
    }
    if (options.printMap) {
	FILE *fm = fp;
	if (options.mapName) {
	    fm = fopen(options.mapName, "w");
	}
#ifdef USE_OLD
	BENV_HwdescPrintAll(fm, hwc->hw[0].comm, hwc, 0);
#else
	BENV_HwdescPrintAll(fm, hwc->collinfo[0].objcomm, hwc, 0);
#endif
	CDBG(SMPTEST,BASIC,"Output Hwdesc map");
	if (options.mapName) {
	    fclose(fm);
	}
    }

    examineTiming(
#ifdef USE_OLD
	hwc->hw[0].comm,
#else
	hwc->collinfo[0].objcomm,
#endif
	timeArray, options.nMsgSizes, nranks,
		  options.nProcNum, options.nTrials, &tarray);
    if (wrank == 0) {
	/* Output the data. The default is just the min times (tarray+tsize),
	   but we can also output average and max times. */
	static int outflag[] = { 0x1, 0x2, 0x4 };
	int outoffset[3] = { tsize, 2*tsize, 0 };
	static const char *outlabel[] =
	    { "Min time", "Max time", "Average time" };
	outoffset[0] = tsize; outoffset[1] = 2*tsize; outoffset[2] = 0;
	for (int i=0; i<3; i++) {
	    if (options.outtime & outflag[i]) {
		fprintf(fp, "%s\n", outlabel[i]);
		printReport(fp,
#ifdef USE_OLD
			    hwc->hw[0].comm,
#else
			    hwc->collinfo[0].objcomm,
#endif
			    hwc,
			    nranks, masterranks, partnerranks,
			    options.nProcNum, options.nprocarray,
			    options.nMsgSizes, options.msgsizes,
			    tarray+outoffset[i]);
	    }
	}
	if (options.ntestctx->ntestinfo) {
	    fprintf(fp, "\nInformation on the selection of ntest used in these tests\n");
#if 0
	    ntestPrintInfo(fp, options.nMsgSizes, options.msgsizes, ntestinfo);
#else
	    BENV_NtestPrintInfo(fp, options.ntestctx, options.nMsgSizes,
				options.msgsizes);
#endif
	}
	if (options.outName) fclose(fp);
    }
    if (tarray) free(tarray);
    }
    /* Cleanup and free */
    free(timeArray);
    free(masterranks);
    free(partnerranks);

    MPI_Finalize();
    return 0;
}

/* ------------------------------------------------------------------------ */
static int argGetint(int i, int argc, char **argv, int *val);
//static int argGetdouble(int i, int argc, char **argv, double *val);


int getOptions(int argc, char *argv[], options_t *options)
{
    int i;
    /* Set defaults */
    options->verbose       = 0;
    options->debug         = 0;
    options->progress      = 0;
    options->printMap      = 0;
    options->isThreaded    = 0;
    options->isNonblocking = 0;
    options->isOverlapped  = 0;
    options->overlapWork   = 10; /* Default value */
    options->nTrials       = 1;
#if 0
    options->ntestMIN      = -1;
    options->ntestMAX      = -1;
#endif
    options->outtime       = 0; /* Use tmin */
#if 0
    options->ntestinfo     = 0;
    options->ntestWT       = 0.0;
    options->ntestS        = 0.0;
    options->ntestR        = 0.0;
    options->ntestWtick    = 0.0;
#endif
    options->mapName       = 0;
    options->outName       = 0;
    options->nMsgSizes     = 0;
    options->msgsizes      = 0;
    options->nProcNum      = 0;
    BENV_HwdescParmInit(&options->parms);

    for (i=1; i<argc; i++) {
	//printf("Processing arg %s\n", argv[i]);
	int rc;
	/* First, check for other options */
	rc = BENV_NtestArg(argc, argv, &i, 0, options->ntestctx);
	BENV_ARGCHECK(rc,"error in ntest options",return 1);
	rc = BENV_HwdescArg(argc, argv, &i, "-hw", &options->parms);
	BENV_ARGCHECK(rc,"error in hwdesc options",return 1);

	if      (strcmp(argv[i],"-v") == 0)  {
	    /* This allows multiple -v options to set higher levels of
	       debugging */
	    options->verbose++;
	}
	else if (strcmp(argv[i], "-d") == 0) options->debug = 1;
	else if (strcmp(argv[i], "-progress") == 0) options->progress = 1;
#ifdef _OPENMP
	else if (strcmp(argv[i], "-t") == 0) options->isThreaded = 1;
#endif
	else if (strcmp(argv[i], "-nb") == 0) options->isNonblocking = 1;
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
	else if (strcmp(argv[i],"-pm") == 0) options->printMap = 1;
	else if (strcmp(argv[i],"-no-pm") == 0) options->printMap = 0;
	else if (strcmp(argv[i],"-pname") == 0) {
	    i++;
	    if (i < argc)
		options->mapName = strdup(argv[i]);
	    else {
		fprintf(stderr, "-pname missing value\n");
		fflush(stderr);
		return 1;
	    }
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
	else if (strcmp(argv[i], "-nprocs") == 0) {
	    i++;
	    if (i < argc) {
		options->nprocarray = BENV_RstringToArray(argv[i],
							  &options->nProcNum);
	    }
	    else {
		fprintf(stderr, "-nprocs missing value\n");
		fflush(stderr);
		return 1;
	    }
	}
	else if (strcmp(argv[i], "-olap") == 0) {
	    options->isOverlapped  = 1;
	}
	else if (strcmp(argv[i], "-olapsize") == 0) {
	    i++;
	    if (argGetint(i, argc, argv, &options->overlapWork))
		return 1;
	}
	else if (strcmp(argv[i], "-ntrials") == 0) {
	    i++;
	    if (argGetint(i, argc, argv, &options->nTrials))
		return 1;
	}
#if 0
	else if (strcmp(argv[i], "-ntest-info") == 0)
	    options->ntestinfo = 1;
	else if (strcmp(argv[i], "-ntest-min") == 0) {
	    i++;
	    if (argGetint(i, argc, argv, &options->ntestMIN))
		return 1;
	}
	else if (strcmp(argv[i], "-ntest-max") == 0) {
	    i++;
	    if (argGetint(i, argc, argv, &options->ntestMAX))
		return 1;
	}
	else if (strcmp(argv[i], "-ntest-wt") == 0) {
	    i++;
	    if (argGetdouble(i, argc, argv, &options->ntestWT))
		return 1;
	}
	else if (strcmp(argv[i], "-ntest-s") == 0) {
	    i++;
	    if (argGetdouble(i, argc, argv, &options->ntestS))
		return 1;
	}
	else if (strcmp(argv[i], "-ntest-r") == 0) {
	    i++;
	    if (argGetdouble(i, argc, argv, &options->ntestR))
		return 1;
	}
	else if (strcmp(argv[i], "-ntest-wtick") == 0) {
	    i++;
	    if (argGetdouble(i, argc, argv, &options->ntestWtick))
		return 1;
	}
#endif
	else if (strcmp(argv[i], "-tmin") == 0) {
	    options->outtime |= 0x1;
	}
	else if (strcmp(argv[i], "-tmax") == 0) {
	    options->outtime |= 0x2;
	}
	else if (strcmp(argv[i], "-tavg") == 0) {
	    options->outtime |= 0x4;
	}

	else if (strcmp(argv[i], "-help") == 0 ||
		 strcmp(argv[i], "-usage") == 0) {
	    printUsage();
	    return 1;
	}
	else {
	    fprintf(stderr, "Unrecognized option %s\n", argv[i]);
	    fflush(stderr);
	    return 1;
	}
    }
    /* Set the default if no option specified */
    if (options->outtime == 0) options->outtime = 0x1;

    return 0;
}

static int argGetint(int i, int argc, char **argv, int *val)
{
    if (i < argc) {
	/* FIXME: This should check for valid input */
	*val = atoi(argv[i]);
    }
    else {
	fprintf(stderr, "%s missing value\n", argv[i-1]);
	fflush(stderr);
	return 1;
    }
    return 0;
}

#if 0
static int argGetdouble(int i, int argc, char **argv, double *val)
{
    if (i < argc) {
	/* FIXME: This should check for valid input */
	*val = strtod(argv[i],NULL);
    }
    else {
	fprintf(stderr, "%s missing value\n", argv[i-1]);
	fflush(stderr);
	return 1;
    }
    return 0;
}
#endif

/*
 * TODO: Add a way to run on a subset of nranks partners - note that
 * we still need the full set of partners, so that we can run with
 * that many partners.  But rather than, for example, running with 1,
 * 2, ... 128 concurrently communication processes, we'd like to have
 * the option to run with 1..16,18,20,..,32, 36, 40, .. 64, 80, 96,
 * .. 128 (eg, number of partners is 1:16,18:2:32,36:4:64,80:16:128)
 *
 * Input Parameters:
 */
int runTests(MPI_Comm comm, int nmsgsizes, const int msgsizes[],
	     int nranks, const int masterranks[], const int partnerranks[],
	     int nprocnum, const int nprocarray[],
	     commroutine_t *commroutine, double *timeArray,
	     /*ntestinfo_t *ntestinfo*/ntestctx_t *nctx)
{
    commstate_t state;
    int lrank, i, len, mrank, prank, nr, maxsize, ntest;
    int *sbuf, *rbuf;
    double t;
    int curprocnumidx;

    MPI_Comm_rank(comm, &lrank);

    maxsize = msgsizes[nmsgsizes-1];
    sbuf = (int *)malloc(maxsize * sizeof(int));
    rbuf = (int *)malloc(maxsize * sizeof(int));
    for (i=0; i<maxsize; i++) {
	sbuf[i] = i;
	rbuf[i] = -i;
    }

    if (showProgress) {
	fputs("\tsize:", stdout);
	fflush(stdout);
    }
    for (i=0; i<nmsgsizes; i++) {
	len   = msgsizes[i];
	if (showProgress) {
	    fprintf(stdout,"%d,", len);
	    fflush(stdout);
	}
	/* Move ntestGetVal into nr loop if ntestGetVal used nc */
	ntest = BENV_NtestGetVal(nctx, len, 1 /* ?? 0 */);
	state = IS_IDLE;
	curprocnumidx = 0;
	CDBGV(SMPTEST,BASIC,"About to run test %d with ntest=%d\n", i, ntest);
	for (nr=0; nr<nranks; nr++) {
	    /* Once a process becomes active, it stays active for this
	       message size */
	    if (state == IS_IDLE) {
		/* are we a master or partner or still idle? */
		if (lrank == masterranks[nr]) {
		    state = IS_MASTER;
		    mrank = lrank;
		    prank = partnerranks[nr];
		}
		else if (lrank == partnerranks[nr]) {
		    state = IS_PARTNER;
		    mrank = masterranks[nr];
		    prank = lrank;
		}
	    }

	    /* Only run the tests if the number of ranks is the current
	       value in nprocarray */
	    if (nr + 1 == nprocarray[curprocnumidx]) {
		if (nr == 0) t = MPI_Wtime();
		timeArray[idx2(i,curprocnumidx,nmsgsizes,nprocnum)] =
		    (*commroutine)(comm, state, mrank, prank, len, ntest,
				   sbuf, rbuf);
		/* For the first test (2 processes), record the ntest and
		   the actual time for the test */
		if (nr == 0) {
#if 1
		    double t0 = MPI_Wtime() - t;
		    printf("Saving %d %d %.2e\n", i, ntest, t0);
		    BENV_NtestSaveInfo(nctx, i, ntest, t0);
#else
		    ntestinfo[i].acttime  = MPI_Wtime() - t;
		    ntestinfo[i].ntestval = ntest;
#endif
		}
		curprocnumidx++;
	    }

	}
	CDBGV(SMPTEST,BASIC,"Done running test %d\n",i);
    }
    if (showProgress) {
	fputc('\n', stdout);
	fflush(stdout);
    }
    free(rbuf);
    free(sbuf);
    return 0;
}

/*
 * Sanity check on the decomposition. Return 0 on success, non-zero on
 * failure. Rank 0 in the hw[0].comm communicator prints messages to
 * stderr.
 */
#ifdef USE_OLD
int checkNodeDecomp(hwdescCtx_t *hwc)
{
    int wrank, nsize, psize;

    MPI_Comm_rank(hwc->hw[0].comm, &wrank);
    /* Things to check:
       2 distinct comms at level 1 (level 0 is MPI_COMM_WORLD)
       Both comms have the same size.
       ToDo: if depth > 2 and we take into account the lower levels,
       we will need to check them as well
    */
    if (hwc->hwlevel < 2) {
	if (wrank == 0) {
	    fprintf(stderr, "hwdepth = %d; need at least 2\n", hwc->hwlevel);
	    fflush(stderr);
	}
	return 1;
    }
    if (hwc->hw[1].nDistinct != 2) {
	if (wrank == 0) {
	    fprintf(stderr, "Expected 2 nodes, found %d %s\n",
		    hwc->hw[1].nDistinct, hwc->hw[1].descstr);
	    fflush(stderr);
	}
	return 1;
    }
    MPI_Comm_size(hwc->hw[1].comm, &nsize);
    MPI_Comm_size(hwc->hw[0].comm, &psize);
    if (nsize*2 != psize) {
	if (wrank == 0) {
	    fprintf(stderr, "Expected nodes to have same size; node on 0 size %d\n",
		    nsize);
	    fflush(stderr);
	}
	return 1;
    }

    return 0;
}
#else
int checkNodeDecomp(hwdescCtx *hwc)
{
    int wrank, nsize, psize;

    MPI_Comm_rank(hwc->collinfo[0].objcomm, &wrank);
    /* Things to check:
       2 distinct comms at level 1 (level 0 is MPI_COMM_WORLD)
       Both comms have the same size.
       ToDo: if depth > 2 and we take into account the lower levels,
       we will need to check them as well
    */
    if (hwc->nlevel < 2) {
	if (wrank == 0) {
	    fprintf(stderr, "hwdepth = %d; need at least 2\n", hwc->nlevel);
	    fflush(stderr);
	}
	return 1;
    }
    if (hwc->objinfo[1].nobj != 2) {
	if (wrank == 0) {
	    fprintf(stderr, "Expected 2 nodes, found %d %s\n",
		    hwc->objinfo[1].nobj,
		    BENV_HwdescKindStr(hwc->objinfo[1].kind));
	    fflush(stderr);
	}
	return 1;
    }
    MPI_Comm_size(hwc->collinfo[1].objcomm, &nsize);
    MPI_Comm_size(hwc->collinfo[0].objcomm, &psize);
    if (nsize*2 != psize) {
	if (wrank == 0) {
	    fprintf(stderr, "Expected nodes to have same size; node on 0 size %d\n",
		    nsize);
	    fflush(stderr);
	}
	return 1;
    }

    return 0;
}
#endif
/*
 * Get the ranks in hw[0].comm of the master (node 0) and partner (node 1)
 * processes. Assumes a correct decomposition (see checkNodeDecomp)
 *
 * UPDATE: If hwdepth >= 3, i.e., there is another non-trivial level
 * within the node, order partners to round-robin distribute ranks. E.g.,
 * if there are multiple sockets, distribute amongst the sockets
 */
#ifdef USE_OLD
int getPartners(hwdescCtx_t *hwc,
		int **masterranks, int **partnerranks, int *nranks)
{
    int       nrank, nsize, *mranks, *pranks;

    /* The leader processes in each comm will determine the ranks in
       hw[0].comm of the processes in the respective comm. These are then
       broadcast to *all* processes in hw[0].comm. */
    MPI_Comm_rank(hwc->hw[1].comm, &nrank);   /* nrank is rank-in-node */
    MPI_Comm_size(hwc->hw[1].comm, &nsize);
    mranks = (int *)malloc(nsize*sizeof(int));
    pranks = (int *)malloc(nsize*sizeof(int));
    if (!mranks || !pranks) {
	fprintf(stderr, "Unable to allocated %d words for ranks\n", 2*nsize);
	fflush(stderr);
	MPI_Abort(hwc->hw[0].comm, 1);
    }
    /* Are we a local leader? */
    if (nrank == 0) {
	MPI_Group pgroup, ngroup;
	int *ranks, *allranks;
	allranks = (int *)malloc(nsize*sizeof(int));
	if (!allranks) {
	    fprintf(stderr, "Unable to allocated %d words for ranks\n", nsize);
	    fflush(stderr);
	    MPI_Abort(hwc->hw[0].comm, 1);
	}
	if (hwc->hw[1].idx == 0) ranks = mranks;
	else                ranks = pranks;
	for (int i=0; i<nsize; i++) allranks[i] = i;
	MPI_Comm_group(hwc->hw[0].comm, &pgroup);
	MPI_Comm_group(hwc->hw[1].comm, &ngroup);
	MPI_Group_translate_ranks(ngroup, nsize, allranks, pgroup, ranks);
	MPI_Group_free(&ngroup);
	MPI_Group_free(&pgroup);
	free(allranks);
    }
    /* Broadcast the ranks */
    /* leaders[0] is node 0, leaders[1] is node 1 */
    MPI_Bcast(mranks, nsize, MPI_INT, hwc->hw[1].leaders[0], hwc->hw[0].comm);
    MPI_Bcast(pranks, nsize, MPI_INT, hwc->hw[1].leaders[1], hwc->hw[0].comm);

    *masterranks  = mranks;
    *partnerranks = pranks;
    *nranks       = nsize;

    return 0;
}
#else
int getPartners(hwdescCtx *hwc,
		int **masterranks, int **partnerranks, int *nranks)
{
    int       nrank, nsize, *mranks, *pranks;

    /* The leader processes in each comm will determine the ranks in
       hw[0].comm of the processes in the respective comm. These are then
       broadcast to *all* processes in hw[0].comm. */
    MPI_Comm_rank(hwc->collinfo[1].objcomm, &nrank);   /* nrank is rank-in-node */
    MPI_Comm_size(hwc->collinfo[1].objcomm, &nsize);
    mranks = (int *)malloc(nsize*sizeof(int));
    pranks = (int *)malloc(nsize*sizeof(int));
    if (!mranks || !pranks) {
	fprintf(stderr, "Unable to allocated %d words for ranks\n", 2*nsize);
	fflush(stderr);
	MPI_Abort(hwc->collinfo[0].objcomm, 1);
    }
    /* Are we a local leader? */
    if (nrank == 0) {
	MPI_Group pgroup, ngroup;
	int *ranks, *allranks;
	allranks = (int *)malloc(nsize*sizeof(int));
	if (!allranks) {
	    fprintf(stderr, "Unable to allocated %d words for ranks\n", nsize);
	    fflush(stderr);
#ifdef USE_OLD
	    MPI_Abort(hwc->hw[0].comm, 1);
#else
	    MPI_Abort(hwc->collinfo[0].objcomm, 1);
#endif
	}
	if (hwc->objinfo[1].objidx == 0) ranks = mranks;
	else                ranks = pranks;
	for (int i=0; i<nsize; i++) allranks[i] = i;
	MPI_Comm_group(hwc->collinfo[0].objcomm, &pgroup);
	MPI_Comm_group(hwc->collinfo[1].objcomm, &ngroup);
	MPI_Group_translate_ranks(ngroup, nsize, allranks, pgroup, ranks);
	MPI_Group_free(&ngroup);
	MPI_Group_free(&pgroup);
	free(allranks);
    }
    /* Broadcast the ranks */
    /* leaders[0] is node 0, leaders[1] is node 1 */
    MPI_Bcast(mranks, nsize, MPI_INT, hwc->collinfo[1].leadersInParent[0],
	      hwc->collinfo[0].objcomm);
    MPI_Bcast(pranks, nsize, MPI_INT, hwc->collinfo[1].leadersInParent[1],
	      hwc->collinfo[0].objcomm);

    *masterranks  = mranks;
    *partnerranks = pranks;
    *nranks       = nsize;

    return 0;
}
#endif

int examineTiming(MPI_Comm comm, double *timeArray, int nmsgsizes, int nranks,
		  int nprocnum, int nTrials, double **tarray)
{
    int    i, trial, tsize;
    double *tmin, *tmax, *tavg;
    double tminval, tmaxval, tavgval;
    *tarray = 0;

    /* Determine the time values to use, by computing the min, max,
       and average over the trials */
    tsize = nmsgsizes * nprocnum;
    tavg = (double *)malloc(3*tsize*sizeof(double));
    tmin = tavg + tsize;
    tmax = tmin + tsize;

    /* Compute the values for the current process, then look over
       all process. Note that in Runtests, the time is taken as the
       max across all communicating processes */

    /* We could flip this around so all accesses are stride 1 */
    for (i=0; i<tsize; i++) {
	double t;
	t = timeArray[i];
	tminval = tmaxval = tavgval = t;
	for (trial=1; trial<nTrials; trial++) {
	    t = timeArray[trial*tsize+i];
	    if (tminval > t) tminval = t;
	    if (tmaxval < t) tmaxval = t;
	    tavgval += t;
	}
	tmin[i] = tminval;
	tmax[i] = tmaxval;
	tavg[i] = tavgval / nTrials;
    }

    /* Note that the "other" values (min, max) are available at tarray + tsize
       and tarray + 2*tsize */
    *tarray = tavg;

    return 0;
}

/* Print the timing report. Only the process performing the output should
   call
*/
int printReport(FILE *fp, MPI_Comm comm,
#ifdef USE_OLD
		hwdescCtx_t *hwc,
#else
		hwdescCtx *hwc,
#endif
		int nranks, int *masterranks, int *partnerranks,
		int nprocnum, int *nprocarray,
		int nmsgsizes, int *msgsizes, double *timeArray)
{
    int    i, nr;
    /* Print the column headings */
    fprintf(fp, "\nInts");
    for (nr=0; nr<nprocnum; nr++)
	fprintf(fp, "\t%-8d", nprocarray[nr]);
    fprintf(fp, "\n");

    /* Print the timing data */
    for (i=0; i<nmsgsizes; i++) {
	fprintf(fp, "%d\t", msgsizes[i]);
	for (nr=0; nr<nprocnum; nr++) {
	    fprintf(fp, "%.2e\t", timeArray[idx2(i,nr,nmsgsizes,nprocnum)]);
	}
	fprintf(fp, "\n");
    }
    fflush(fp);
    return 0;
}

/* Communication timing routines
 */
double commBlockingSend(MPI_Comm comm, commstate_t st, int mrank, int prank,
			int len, int ntest, int *sbuf, int *rbuf)
{
    double t = -1.0;
    int k, tag = 1 + ntest;

    MPI_Barrier(comm);
    if (st == IS_MASTER) {
	t = MPI_Wtime();
	for (k=0; k<ntest; k++) {
	    MPI_Send(sbuf,len,MPI_INT,prank,tag+k,comm);
	    MPI_Recv(rbuf,len,MPI_INT,prank,tag+k,comm, MPI_STATUS_IGNORE);
	}
	t = (MPI_Wtime() - t)/ntest;
    }
    else if (st == IS_PARTNER) {
	t = MPI_Wtime();
	for (k=0; k<ntest; k++) {
	    MPI_Recv(rbuf,len,MPI_INT,mrank,tag+k, comm, MPI_STATUS_IGNORE);
	    MPI_Send(sbuf,len,MPI_INT,mrank,tag+k, comm);
	}
	t = (MPI_Wtime() - t)/ntest;
    }
    /* else state is IS_IDLE; ignore */
    MPI_Allreduce(MPI_IN_PLACE, &t, 1, MPI_DOUBLE, MPI_MAX, comm);
    return t;
}

double commNonblockingSend(MPI_Comm comm, commstate_t st, int mrank, int prank,
			   int len, int ntest, int *sbuf, int *rbuf)
{
    double t = -1.0;
    int k, tag = 1 + ntest;
    MPI_Request req[2];

    MPI_Barrier(comm);
    if (st == IS_MASTER) {
	t = MPI_Wtime();
	for (k=0; k<ntest; k++) {
	    MPI_Isend(sbuf,len,MPI_INT,prank,tag+k,comm, &req[0]);
	    MPI_Irecv(rbuf,len,MPI_INT,prank,tag+k,comm, &req[1]);
	    dowork(len);
	    MPI_Waitall(2, req, MPI_STATUSES_IGNORE);
	}
	t = (MPI_Wtime() - t)/ntest;
    }
    else if (st == IS_PARTNER) {
	t = MPI_Wtime();
	for (k=0; k<ntest; k++) {
	    MPI_Irecv(rbuf,len,MPI_INT,mrank,tag+k, comm, &req[0]);
	    MPI_Isend(sbuf,len,MPI_INT,mrank,tag+k, comm, &req[1]);
	    dowork(len);
	    MPI_Waitall(2, req, MPI_STATUSES_IGNORE);
	}
	t = (MPI_Wtime() - t)/ntest;
    }
    /* else state is IS_IDLE; ignore */

    MPI_Allreduce(MPI_IN_PLACE, &t, 1, MPI_DOUBLE, MPI_MAX, comm);
    return t;
}

#ifdef _OPENMP
double commBlockingSendThreaded(MPI_Comm comm, commstate_t st,
				int mrank, int prank,
				int len, int ntest, int *sbuf, int *rbuf)
{
    double t = -1.0;

#pragma omp parallel
    {
    int i, k;
    int tid=omp_get_thread_num();
    int tag = 1 + ntest +  tid*128;
    /* Reallocated the buffers so that there are per-thread buffers;
       Also touch them so that "first touch" has already happened */
    int *stbuf, *rtbuf;
    stbuf = (int *)malloc(len * sizeof(int));
    rtbuf = (int *)malloc(len * sizeof(int));

    for (i=0; i<len; i++) {
        stbuf[i] = i;
        rtbuf[i] = -i;
    }

#pragma omp master
    MPI_Barrier(comm);
#pragma omp barrier
    if (st == IS_MASTER) {
#pragma omp master
	t = MPI_Wtime();
	for (k=0; k<ntest; k++) {
	    MPI_Send(stbuf,len,MPI_INT,prank,tag+k,comm);
	    MPI_Recv(rtbuf,len,MPI_INT,prank,tag+k,comm, MPI_STATUS_IGNORE);
	}
#pragma omp barrier
#pragma omp master
	t = (MPI_Wtime() - t)/ntest;
    }
    else if (st == IS_PARTNER) {
#pragma omp master
	t = MPI_Wtime();
	for (k=0; k<ntest; k++) {
	    MPI_Recv(rtbuf,len,MPI_INT,mrank,tag+k, comm, MPI_STATUS_IGNORE);
	    MPI_Send(stbuf,len,MPI_INT,mrank,tag+k, comm);
	}
#pragma omp barrier
#pragma omp master
	t = (MPI_Wtime() - t)/ntest;
    }
    free(stbuf);
    free(rtbuf);
    } /* omp parallel */
    /* else state is IS_IDLE; ignore */
    MPI_Allreduce(MPI_IN_PLACE, &t, 1, MPI_DOUBLE, MPI_MAX, comm);
    return t;
}
#endif
#if 0
/* ----------------------------------------------------------------------- */
/* These routines determine the number of tests to perform. The goal is
   to ensure that the the tests take at least ntestWT times the resolution
   of the timer, which is given (we hope!) by MPI_Wtick. This is estimated
   by assuming the simple performance model, T = s = rn, where s is the latency
   and r is the inverse rate. Provisions are made to extend this to the maxrate
   model, T = s + (1/max(1/rmax,nc*1/r1))n, where nc is the number of
   concurrently communicating processes. Using this formula, ntest can
   be estimate as

   ntest * 2(s+rn) = ntestWT * MPI_Wtick

   (2 since the test is a ping-pong, so there are two communiations in each
   test).

   Thus,

       ntest = ntestWT * MPI_Wtick / (2 (s + rn))

   As a sanity check, ntest is also constrained to be between ntestMIN and
   ntestMAX.
*/
static double ntestWT, /* Multiple of Wtick to use in determining ntest */
    ntestS,            /* Latency to use in determining ntest */
    ntestR,            /* Inverse bandwidth to use in determining ntest */
    ntestWtick;        /* Use this value for wtick instead of MPI_Wtick */
static int ntestMIN,
    ntestMAX;          /* These provide a valid range for ntest values */

int ntestInit(options_t *options, MPI_Comm comm)
{
    /* Save the values from options, setting defaults if necessary */
    if (options->ntestMIN <= 0)
	ntestMIN = 10;
    else
	ntestMIN = options->ntestMIN;

    if (options->ntestMAX <= 0)
	ntestMAX = 10000;
    else
	ntestMAX = options->ntestMAX;

    if (options->ntestWT <= 0)
	ntestWT = 100;
    else
	ntestWT = options->ntestWT;

    if (options->ntestS <= 0)
	ntestS = 2.0e-6;
    else
	ntestS = options->ntestS;

    if (options->ntestR <= 0)
	/* Default is 1 gigaword/sec, or an inverse of 1e-9 */
	ntestR = 1.0e-9;
    else
	ntestR = options->ntestR;

    if (options->ntestWtick <= 0) {
	double wt = MPI_Wtick();
	/* Also examine the average cost of a call to
	   MPI_Wtime - which is also relevant to the accuracy of the
	   times from MPI_Wtime */
	double t, t1;
	t = MPI_Wtime();
	for (int i=0; i<20; i++) {
	    t1 = MPI_Wtime();
	}
	t = (t1 - t) / 20;
	if (wt < t) wt = t;

	MPI_Allreduce(MPI_IN_PLACE, &wt, 1, MPI_DOUBLE, MPI_MAX, comm);
	if  (wt > 1.0e-9)
	    ntestWtick = wt;
	else
	    /* FIXME: if wt is 0, MPI_Wtick is erroneous */
	    ntestWtick = 1.0e-9;
    }
    else
	ntestWtick = options->ntestWtick;

    //fprintf(stderr, "s = %.2e, r = %.2e, wt = %.2e, wtick = %.2e\n",
    //	    ntestS, ntestR, ntestWT , ntestWtick);
    //fflush(stderr);
    return 0;
}

int ntestGetVal(int msgsize, int nc)
{
    int ntest;

    ntest = ntestWT * ntestWtick / ( 2.0 * (ntestS + ntestR * msgsize));

    if (ntest < ntestMIN) ntest = ntestMIN;
    if (ntest > ntestMAX) ntest = ntestMAX;

    return ntest;
}

/* This gives the time that was estimated that each test would take */
double ntestTimeEst(int len)
{
    return 2.0 * (ntestS + ntestR * len);
}

int ntestPrintInfo(FILE *fp, int nmsgs, int msgsizes[], ntestinfo_t ntestinfo[])
{
    fprintf(fp, "Model ntest = %.2e * %.2e / (2 * (%.2e + %.2e * len))\n",
	    ntestWT, ntestWtick, ntestS, ntestR);
    fprintf(fp, "Ints\tntest\test time\tact time\n");
    for (int i=0; i<nmsgs; i++) {
	fprintf(fp, "%d\t%d\t%.2e\t%.2e\n", msgsizes[i], ntestinfo[i].ntestval,
		ntestTimeEst(msgsizes[i])*ntestinfo[i].ntestval,
		ntestinfo[i].acttime);
    }
    fflush(fp);
    return 0;
}
#endif

/* */
static double *workarray=0;
static int workarraymaxlen=0;
static int workparm = 10;
void doworkinit(int maxlen, int parm)
{
    if (!workarray || workarraymaxlen < maxlen*parm) {
	if (workarray) free(workarray);
	workarray = (double *)malloc(maxlen * parm * sizeof(double));
	if (!workarray) {
	    fprintf(stderr, "Unable to allocate %d doubles\n", maxlen);
	    fflush(stderr);
	    MPI_Abort(MPI_COMM_WORLD, 1);
	}
	workarraymaxlen = maxlen;
    }
    workparm        = parm;

}
/* dowork is always called in the nonblocking case; if -olap has *not* been
   selected, this routine will return immediately */
void dowork(int len)
{
    int i, totlen = len*workparm;
    if (!workarray) return;
    for (i=0; i<totlen; i++) workarray[i] += 1.0e-6;
}
/* */

/*D
  smptest - Measure send/receive communication performance between multicore
            nodes

Arguments:
+ -sizes - Message sizes to use in testing. Can be a range, list, or arithmetic
 or geometric series
. -nprocs - Number of processes to use. Can be a range, list, or arithmetic or
 geometric series

. -t - Run communication in all available OpenMP threads. Only available if
  OpenMP is available (otherwise, '-t' is not a valid argument)
. -nb - Use nonblocking rather than blocking communication
. -o fname - Output file name for performance data
. -pm - Print the process map (print map means??)
. -no-pm - Do not print process map
. -pname fname - Print the map to file fname

. -olap - Test communication/computation overlap with a work array whose
 size is proportional to the message length
. -olapsize n - Work size is 'n' time message length for computation

. -ntrials n - Number of times to repeat each test
. -ntest-info - Write information on the 'ntest' value used and the time that
 the tests took. This allows evaluation of how well the 'ntest' estimation is
 working.
. -ntest-min n - Minumum number for ntest
. -ntest-max n - Maximum number for ntest
. -ntest-wt f - Multiple of 'wtick' to use in estimating 'ntest'
. -ntest-s f - Latency (in seconds) to use in estimating 'ntest'
. -ntest-r f - Inverse bandwidth (in seconds/byte) to use in estimating 'ntest'
. -ntest-wtick f - Value of clock tick to use instead of value from 'MPI_Wtick'

. -v - Turn on verbose messaging. Multiple '-v' options increase verbosity
. -d - Turn on debugging output
- -progress - Show progress while running test

Notes:


Reference:
 An earlier version of this program was developed to explore the impact of
 having multiple threads or processes communicating at the same time between
 nodes, rather than a single process on each node.

 Modeling MPI Communication Performance on SMP Nodes: Is it Time to Retire
 the Ping Pong Test
 William Gropp, Luke N. Olson, and Philipp Samfass
 EuroMPI '16: Proceedings of the 23rd European MPI Users' Group Meeting,
 September 2016, Pages 41–50
 https://dl.acm.org/doi/10.1145/2966884.2966919
D*/

void printUsage(void)
{
fprintf(stderr, "smptest - Measure send/receive communication performance between multicore nodes\n");

fprintf(stderr, "Command line arguments:\n");
fprintf(stderr, "\
 -sizes - Message sizes to use in testing. Can be a range, list, or arithmetic or geometric series\n\
 -nprocs - Number of processes to use. Can be a range, list, or arithmetic or geometric series\n\
\n\
 -t - Run communication in all available OpenMP threads. Only available if\n\
  OpenMP is available (otherwise, '-t' is not a valid argument)\n\
 -nb - Use nonblocking rather than blocking communication\n\
 -o fname - Output file name for performance data\n\
 -pm - Print the process map\n\
 -no-pm - Do not print process map\n\
 -pname fname - Print the map to file fname\n\
\n\
 -olap - Test communication/computation overlap with a work array whose\n\
 size is proportional to the message length\n\
 -olapsize n - Work size is 'n' time message length for computation\n\
 -ntrials n - Number of times to repeat each test\n\
 -ntest-info - Write information on the 'ntest' value used and the time that\n\
 the tests took. This allows evaluation of how well the 'ntest' estimation is\n\
 working.\n\
 -ntest-min n - Minumum number for ntest\n\
 -ntest-max n - Maximum number for ntest\n\
 -ntest-wt f - Multiple of 'wtick' to use in estimating 'ntest'\n\
 -ntest-s f - Latency (in seconds) to use in estimating 'ntest'\n\
 -ntest-r f - Inverse bandwidth (in seconds/byte) to use in estimating 'ntest'\n\
 -ntest-wtick f - Value of clock tick to use instead of value from 'MPI_Wtick'\n\
\n\
 The options -tmin, -tmax, and -tavg may be used in combination, generating\n\
 output for each of the choices.\n\
 -tmin - Output the minimum of the times for each test (of the ntest trials)\n\
 -tmax - Output the maximum of the times for each test\n\
 -tavg - Output the average of the times for each test\n\
 -v - Turn on verbose messaging. Multiple '-v' options increase verbosity\n\
 -d - Turn on debugging output\n\
 -progress - Show progress while running test\n");
}


#if 0
/* Updated version that distributes partner ranks across components (e.g.,
 * sockets) on a node
 *
 * Algorithm:
 * Identify the first 2 nodes.
 * Check that each has a consistent decomposition (each subobject has the
 * same number of processes)
 */
/*
 * Get the ranks in hw[0].comm of the master (node 0) and partner (node 1)
 * processes. Assumes a correct decomposition (see checkNodeDecomp)
 *
 * UPDATE: If hwdepth >= 3, i.e., there is another non-trivial level
 * within the node, order partners to round-robin distribute ranks. E.g.,
 * if there are multiple sockets, distribute amongst the sockets
 */
int getPartners(hwdescCtx_t *hwc,
		int **masterranks, int **partnerranks, int *nranks)
{
    int       nrank, nsize, *mranks, *pranks, nvalid, *order=0;

    /* The leader processes in each comm will determine the ranks in
       hw[0].comm of the processes in the respective comm. These are then
       broadcast to *all* processes in hw[0].comm. */
    MPI_Comm_rank(hwc->hw[1].comm, &nrank);   /* nrank is rank-in-node */
    MPI_Comm_size(hwc->hw[1].comm, &nsize);
    mranks = (int *)malloc(nsize*sizeof(int));
    pranks = (int *)malloc(nsize*sizeof(int));
    if (!mranks || !pranks) {
	fprintf(stderr, "Unable to allocated %d words for ranks\n", 2*nsize);
	fflush(stderr);
	MPI_Abort(hwc->hw[0].comm, 1);
    }
    /* How many levels of consistent elements are there (e.g., multiple
       sockets with the same number of processes on each socket)? */
    BENV_HwdescCheckConsistentSizes(hw+1, hwlevel-1, &nvalid);
    if (CDBGGETVAL(SMPTEST)) {
	fprintf(stderr, "Number of valid consistent levels = %d\n", nvalid);
	fflush(stderr);
    }
    if (nvalid > 1) {
	int lidx;
	/* compute the ordering of processes on the node by using a
	   round-robin assignment of the processes. Gather this ordering
	   to the lead process on the node */
	if (nrank == 0) {
	    order = (int *)malloc(nsize*sizeof(int));
	    if (!order) {
		fprintf(stderr, "Unable to allocated %d words for ranks\n",
			nsize);
		fflush(stderr);
		MPI_Abort(hwc->hw[0].comm, 1);
	    }
	}
	lidx = hwc->hw[1].idx;
	for (int i=1; i<nvalid; i++) {
	    int sz;
	    /* Offset by 1 because nvalid relative to hw[1], not hw[0] */
	    MPI_Comm_size(hwc->hw[i+1].comm, &sz)
	    lidx = sz * lidx + hwc->hw[i+1].idx;
	}
	if (CDBGGETVAL(SMPTEST)) {
	    fprintf(stderr, "index of node rank %d is %d\n", nrank, lidx);
	    fflush(stderr);
	}
	MPI_Gather(&lidx, 1, MPI_INT, order, 1, MPI_INT, 0, hwc->hw[1].comm);
    }

    /* Are we a local leader? */
    if (nrank == 0) {
	MPI_Group pgroup, ngroup;
	int *ranks, *allranks;
	allranks = (int *)malloc(nsize*sizeof(int));
	if (!allranks) {
	    fprintf(stderr, "Unable to allocated %d words for ranks\n", nsize);
	    fflush(stderr);
	    MPI_Abort(hwc->hw[0].comm, 1);
	}
	if (hwc->hw[1].idx == 0) ranks = mranks;
	else                ranks = pranks;
	if (order)
	    for (int i=0; i<nsize; i++) allranks[order[i]]=i;
	else
	    for (int i=0; i<nsize; i++) allranks[i] = i;
	MPI_Comm_group(hwc->hw[0].comm, &pgroup);
	MPI_Comm_group(hwc->hw[1].comm, &ngroup);
	MPI_Group_translate_ranks(ngroup, nsize, allranks, pgroup, ranks);
	MPI_Group_free(&ngroup);
	MPI_Group_free(&pgroup);
	free(allranks);
	if (order) free(order);
    }
    /* Broadcast the ranks */
    /* leaders[0] is node 0, leaders[1] is node 1 */
    MPI_Bcast(mranks, nsize, MPI_INT, hwc->hw[1].leaders[0], hwc->hw[0].comm);
    MPI_Bcast(pranks, nsize, MPI_INT, hwc->hw[1].leaders[1], hwc->hw[0].comm);

    *masterranks  = mranks;
    *partnerranks = pranks;
    *nranks       = nsize;

    return 0;
}
#endif
