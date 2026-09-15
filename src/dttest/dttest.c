#include "benvconf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mpi.h"
#include "benvutil.h"
#include "ntest.h"
#ifdef USE_OLD
#include "hwdesc.h"
#else
#include "hwdescnew.h"
#endif
#include "tarray.h"
#include "getsizes.h"

typedef struct {
    int verbose;     /* Provide more information about the operation of code */
    int debug;       /* Use an artificial mesh topology for debugging */
    int progress;    /* If true, show progress while running tests */
    int printMap;    /* Print the mapping of processes to the topology */
    int nMsgSizes;   /* Number of message sizes provided */
    int nTrials;     /* Repeat the test this many times */
    ntestctx_t *nctx;  /* Ntest context */
    int outtime;     /* Determines which output data to provide. Bit mask */
    int *msgsizes;   /* Message sizes to use */
    char *mapName;   /* Name for the mapping file. Only used on rank 0 */
    char *outName;   /* Name for output file.  If null, use stdout */
} options_t;

int getOptions(int argc, char **argv, options_t *options);
void printUsage(void);

/*D dttest - Report on the performance of MPI datatypes for vector moves

Commandline options:

  D*/
int main(int argc, char **argv)
{
    int i, j, ntrials=10, nlen, provided, wrank;
    options_t options;
    TActx  *timing;
#ifdef USE_OLD
    hwdescCtx_t *hwc;
#else
    hwdescCtx *hwc;
#endif
    FILE *fp;

    MPI_Init_thread(&argc, &argv, MPI_THREAD_SINGLE, &provided);

    MPI_Comm_rank(MPI_COMM_WORLD, &wrank);

    /* Process command line */
    getOptions(argc, argv, &options);

    /* Determine HW and node/process topology */
    BENV_HwdescGetDescGeneral(MPI_COMM_WORLD, BENV_HWDESC_USE_ALL, 0, &hwc);

    /* Creating timing data structure */
    timing = BENV_TAInit(3, options.nMsgSizes, ntrials);

    /* Run tests and gather timing data:
       Collect information on different approaches:
       1. user code to pack/unpack
       2. subarray
       3. resized element for strided copy
       Tests are run multiple times because experience shows that the
       results are not idential from one run to the next.
    */
    for (i=0; i<ntrials; i++) {
	/* Each test is run ntrials times */

	for (j=0; j<options.nMsgSizes; j++) {
	    double t0;
	    int    niter;
	    /* Allocate memory for data, both source/dest and local buffer */
	    nlen = options.msgsizes[j];

	    /* Create the datatypes */

	    /* Run tests for this size information */
	    niter = BENV_NtestGetVal(options.nctx, nlen, 0);

	    MPI_Barrier(MPI_COMM_WORLD);
	    t0 = MPI_Wtime();
	    for (int m=0; m<niter; m++) {
	    }
	    t0 = (MPI_Wtime() - t0) / niter;
	    BENV_TASetVal3(timing, 0, j, i, t0);

	    MPI_Barrier(MPI_COMM_WORLD);
	    t0 = MPI_Wtime();
	    for (int m=0; m<niter; m++) {
	    }
	    t0 = (MPI_Wtime() - t0) / niter;
	    BENV_TASetVal3(timing, 1, j, i, t0);

	    MPI_Barrier(MPI_COMM_WORLD);
	    t0 = MPI_Wtime();
	    for (int m=0; m<niter; m++) {
	    }
	    t0 = (MPI_Wtime() - t0) / niter;
	    BENV_TASetVal3(timing, 2, j, i, t0);
	}
    }

    /* Analyze the results - e.g., look at data across processes,
       compute data quartiles for each size */

    /* Output results, including graph commands */
    fp = stdout;
    if (options.outName && wrank == 0) {
	fp = fopen(options.outName, "w");
	if (!fp) {
	    fprintf(stderr, "Could not open %s for output\n",
		    options.outName);
	    MPI_Abort(MPI_COMM_WORLD, 1);
	}
    }
    BENV_HwdescPrintAll(fp, MPI_COMM_NULL, hwc, 0);
    if (options.outName && wrank == 0) fclose(fp);

    /* Free objects and data */

    BENV_HwdescFreeCtx(hwc);
    BENV_TAFree(timing);
    MPI_Finalize();
}

/* ---------------------------------------------------------------------- */
int getOptions(int argc, char **argv, options_t *options)
{
    int i, rc;
    /* Set defaults */
    options->verbose       = 0;
    options->debug         = 0;
    options->progress      = 0;
//    BENV_PMapInit(&options->pmapctx);
/*
    options->printMap      = 0;
    options->mapName       = 0;
    options->outName       = 0;
*/
    options->nTrials       = 1;
    options->outtime       = 0; /* Use tmin */

    options->nctx = BENV_NtestInit(MPI_COMM_WORLD);

    options->nMsgSizes     = 0;
    options->msgsizes      = 0;

    for (i=1; i<argc; i++) {
	//printf("Processing arg %s\n", argv[i]);
	rc = BENV_NtestArg(argc, argv, &i, 0, options->nctx);
	BENV_ARGCHECK(rc,"error in ntest",return 1);

	if      (strcmp(argv[i],"-v") == 0)  {
	    /* This allows multiple -v options to set higher levels of
	       debugging */
	    options->verbose++;
	}
	else if (strcmp(argv[i], "-d") == 0) options->debug = 1;
	else if (strcmp(argv[i], "-progress") == 0) options->progress = 1;
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
#if 0
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
#endif
	else if (strcmp(argv[i], "-ntrials") == 0) {
	    i++;
	    if (BENV_ArgGetint(i, argc, argv, &options->nTrials))
		return 1;
	}
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

void printUsage(void)
{
fprintf(stderr, "dttest - Measure send/receive communication performance using MPI datatypes between two processes\n");

fprintf(stderr, "Command line arguments:\n");
fprintf(stderr, "\
 -sizes - Message sizes to use in testing. Can be a range, list, or arithmetic or geometric series\n\
 -o fname - Output file name for performance data\n\
 -pm - Print the process map\n\
 -no-pm - Do not print process map\n\
 -pname fname - Print the map to file fname\n\
\n\
 -ntrials n - Number of times to repeat each test\n\
\n\
 -v - Turn on verbose messaging. Multiple '-v' options increase verbosity\n\
 -d - Turn on debugging output\n\
 -progress - Show progress while running test\n");
    BENV_NtestArgPrintUsage(stderr, 0);
}
