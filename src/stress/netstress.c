/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 * Copyright (C) by University of Illinois 2024
 */

#include "benvconf.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>
#include <sys/time.h>
#include "mpi.h"
#ifdef USE_OLD
#include "hwdesc.h"
#include "nodeinfo.h"
#else
#include "hwdescnew.h"
#endif
#include "getsizes.h"
#include "benvdbg.h"

CDBGDECL(NETSTRESS);
#if 0
static int verbose = 0;
static int dbg_wrank = -1;
#endif

typedef struct {
    int verbose;     /* Provide more information about the operation of code */
    int debug;       /* Use an artificial mesh topology for debugging */
    int progress;    /* If true, show progress while running tests */
    int printMap;    /* Print the mapping of processes to the topology */
    int nMsgSizes;   /* Number of message sizes provided */
    int *msgsizes;   /* Message sizes to use */
    int runsecs;     /* Number of sections to run test */
    char *outName;  /* Name for output file.  If null, use stdout */
    char *progressFile; /* Name of file for progress.  If null, use stdout */
} options_t;

/*
 * netstress - Stress the communication network using data patterns that
 *             stress the data paths
 *
 * This is an update to the original "stress" program from the late 1980's
 *
 * Steps:
 * 1. Get the hardware hierarchy. The expectation is that different hardware
 *    is used for communication at different levels in the hierarchy. E.g.,
 *    shared memory within a node and high-performance interconnect between
 *    nodes.
 * 2. Determine communicatino partners at each level
 * 3. Determine communication members, i.e., the set of sending and of
 *    receiving processes. Note there are both each-to-all and all-to-all
 *    tests
 * 4. Allocate and initialize send and receive buffers (check stress for how
 *    the length is set - note that we need to cover different communication
 *    protocols such as eager and rendezvous.
 *
 * For the implementation, also need
 * 1. Routines to provide progress: progressInit(output loc),
 *    progressStart(), progressTick(), progressFinish()
 * 2. Routines to provide a timeout check: timeoutInit(), timeoutStart(),
 *    timeoutCheck(). Query: Provide an option for a timeout signal?
 */

void printUsage(void);
int getOptions(int argc, char *argv[], options_t *options);
void checkMem(void *ptr, size_t len, const char *msg);
int CheckBuffer(uint32_t *buf, int size, int pattern);
void SetBuffer(uint32_t *buf, int size, int pattern);
int ErrTest(MPI_Status *status, int partner, int bufsize,
	    uint32_t *buffer, int pattern);
void initProgress(const char *fname, int show, int major, int flush);
void showProgress(int it);
void endProgress(void);

/*
 * Rethinking the network test, now that there is not 1-1
 * correspondence between nodes and NICs, or even a single network
 * interface, as inter- and intra-node communication may use different
 * methods.
 */

/* ToDo: Take node hierarchy into accound:
   1. One process per node, each-to-all and all-to-all tests
   2. One process at each level in the hierarchy, same tests
   3. Need to include any accelerators (see Mert's tests)
*/

#define NPATTERNS 12

int main(int argc, char **argv)
{
    int i;
    struct timeval currenttime, starttime;
    int     runsecs;
    int    *msgsizes, nsizes, pattern, sizeidx, maxsize, bufsize;
#ifdef USE_OLD
    hwdescCtx_t *hwc=0;
#else
    hwdescCtx *hwc=0;
#endif
    int      wsize, wrank, nrank, tag, npartners, *pranks, p, arank;
    int      err=0;
    options_t options;
    uint32_t **rbuf, **sbuf;
    MPI_Request *req;
    MPI_Status *stats;
    MPI_Comm    activecomm; /* Communicator of active processes */
    int done=0;
    long wordsSent = 0;

    MPI_Init(&argc, &argv);
    MPI_Comm_size(MPI_COMM_WORLD, &wsize);
    MPI_Comm_rank(MPI_COMM_WORLD, &wrank);
#if 0
    dbg_wrank = wrank;
#endif

    /* Command line arguments */
    getOptions(argc, argv, &options);

    /* Get a range of message sizes that are likely to cover the range of
       protocols (e.g., eager and rendezvous) */
    if (!options.msgsizes) {
	options.msgsizes = BENV_GetSizesMult(128, 2*1024*1024, 4.0,
					     &options.nMsgSizes);
    }
    nsizes   = options.nMsgSizes;
    msgsizes = options.msgsizes;
    runsecs  = options.runsecs;
    if (options.progress) initProgress(options.progressFile, 1, 0, 0);

    /* Find the maximum message size (may be the same as msgsizes[nsizes-1],
       but because we permit a general list of sizes in getoptions, the
       maximum may be elsewhere in the list */
    maxsize = 0;
    for (i=0; i<nsizes; i++) {
	if (msgsizes[i] > maxsize) maxsize = msgsizes[i];
    }

    /* Determine partners, with nconcur per hierarchy element (e.g., node or
       socket).
       Get hwdesc.
       Get coordtuple based on hwdesc
       Communicate within levels, with an option to only use a few processes
       per level.

       smptest uses BENV_NodeGetComms, followed by a routine for just
       smptest, getPartners, that communicates the corresponding partners.
    */
#if 0
    BENV_NodeGetComms(MPI_COMM_WORLD, hw, MAX_HW_DEPTH, &hwdepth);
#else
    BENV_HwdescGetDescGeneral(MPI_COMM_WORLD, BENV_HWDESC_USE_ALL, 0, &hwc);
#endif
    /* For a first pass, can use the leaders in MPI_COMM_WORLD for the nodes
       as the possible partners. If need more processes per node, then will
       need to communicate them to all processes */

    /* Ensure that we have at least node information */
#ifdef USE_OLD
    if (hwc->hwlevel <= 1)
#else
    if (hwc->nlevel <= 1)
#endif
    {
	if (wrank == 0) {
	    fprintf(stderr, "No node information available, aborting\n");
	    fflush(stderr);
	    MPI_Abort(MPI_COMM_WORLD, 1);
	}
	/* TODO: ensure hw[1] has node information, and not some higher-level
	   information (e.g., rack or switch) */
    }

    /* Determine the partners.
       Leaders of nodes are hw[1].leaders and there are hw[1].nDistinct
       of them. Leader ranks relative to hw[0].comm, which is MPI_COMM_WORLD
    */
    /* Get the rank in the local (node) communicator */
#ifdef USE_OLD
    MPI_Comm_rank(hwc->hw[1].comm, &nrank);
#else
    MPI_Comm_rank(hwc->collinfo[1].objcomm, &nrank);
#endif
    if (nrank == 0) {
	int j = 0;
#ifdef USE_OLD
	npartners = hwc->hw[1].nDistinct-1;
#else
	npartners = hwc->objinfo[1].nobj-1;
#endif
	pranks    = (int *)malloc(npartners * sizeof(int));
	checkMem(pranks, npartners*sizeof(int), "pranks");
#ifdef USE_OLD
	for (i=0; i<hwc->hw[1].nDistinct; i++) {
	    if (wrank != hwc->hw[1].leaders[i])
		pranks[j++] = hwc->hw[1].leaders[i];
	}
#else
	for (i=0; i<hwc->objinfo[1].nobj; i++) {
	    if (wrank != hwc->collinfo[1].leadersInParent[i])
		pranks[j++] = hwc->collinfo[1].leadersInParent[i];
	}
#endif
	CDBGV(NETSTRESS,BASIC,"Communicating process with worldrank %d\n", wrank);
    }

    /* Create a communicator of the active processes */
    MPI_Comm_split(MPI_COMM_WORLD, (nrank == 0) ? 0 : MPI_UNDEFINED, wrank,
		   &activecomm);
    /* Need to know if we are a leader. We are if rank in hw[1].comm is 0. */
    /* TODO: If need more than one process, try to take one from each of the
       next level of hw hierarchy, if available */

    /* TODO: This is really "is this process communicating?" */
    if (nrank == 0) {
	int loopcount = 0;
	double totalTime;

	/* Allocate buffers - use the maximum size */
	req = (MPI_Request *)malloc(2*npartners * sizeof(MPI_Request));
	sbuf = (uint32_t **)malloc(npartners * sizeof(uint32_t *));
	rbuf = (uint32_t **)malloc(npartners * sizeof(uint32_t *));
        stats = (MPI_Status *)malloc(2*npartners * sizeof(MPI_Status));
	checkMem(req, 2*npartners*sizeof(MPI_Request), "MPI Requests");
	checkMem(sbuf, npartners*sizeof(long*), "sbuf array");
	checkMem(rbuf, npartners*sizeof(long*), "rbuf array");
        checkMem(stats, npartners*sizeof(MPI_Status), "status array");
	for (i=0; i<npartners; i++) {
	    sbuf[i] = (uint32_t *)malloc(maxsize*sizeof(uint32_t));
	    rbuf[i] = (uint32_t *)malloc(maxsize*sizeof(uint32_t));
	    checkMem(sbuf[i], maxsize*sizeof(uint32_t), "send buffer");
	    checkMem(rbuf[i], maxsize*sizeof(uint32_t), "recv buffer");
	}

	/* Is this the master process of the active processes? */
	MPI_Comm_rank(activecomm, &arank);

	/* Loop for a given number of iterations or a fixed amount of time */
	gettimeofday( &starttime, (struct timezone *)0 );
	totalTime = MPI_Wtime();
	/* Loop over patterns */
	do {
	    for (pattern=0; pattern<=NPATTERNS; pattern++) {
		for (sizeidx=0; sizeidx<nsizes; sizeidx++) {
		    bufsize = msgsizes[sizeidx];
		    for (p=0; p<npartners; p++) {
			SetBuffer(sbuf[p], bufsize, pattern);
		    }
		    /* default is each to all: Here is the old code from
		       stress */
		    /* TODO: Replace this with several options of
		       communication calls */
		    for (p=0; p<npartners; p++) {
			tag = pattern;
			CDBGV(NETSTRESS,DETAIL,"exchanges with %d",pranks[p]);
			MPI_Irecv(rbuf[p], bufsize, MPI_UINT32_T,
				  pranks[p], tag, MPI_COMM_WORLD, &req[p]);
			MPI_Isend(sbuf[p], bufsize, MPI_UINT32_T,
				  pranks[p], tag, MPI_COMM_WORLD, &req[npartners+p]);
			wordsSent += bufsize;
		    }
		    MPI_Waitall(2*npartners, req, stats);
		    /* check received buffer */
		    for (p=0; i<npartners; p++) {
			err += ErrTest(&stats[p], pranks[p], bufsize, rbuf[p],
				       pattern);
		    }
		}
	    }
	    totalTime = MPI_Wtime() - totalTime;

	    /* The master process makes this check and broadcasts the value
	       of done to all others */
	    /* Check elapsed time or number of iterations */
	    loopcount ++;
	    if (arank == 0) {
		showProgress(loopcount);
		gettimeofday(&currenttime, (struct timezone *)0);
		/* Ignore the nsec field */
		if (currenttime.tv_sec - starttime.tv_sec > runsecs) done = 1;
	    }
	    MPI_Bcast(&done, 1, MPI_INT, 0, activecomm);
	} while (!done);

	/* Report results, including achieved communication performance */
	if (arank == 0) {
	    FILE *fp;
	    int hrs, mins, secs;
	    double rate;

	    /* */
	    endProgress();

	    secs = (currenttime.tv_sec - starttime.tv_sec);
	    hrs  = secs / 3600;
	    secs = secs - 3600 * hrs;
	    mins = secs / 60;
	    secs = secs - 60 *mins;
	    if (options.outName) {
		fp = fopen(options.outName, "w");
		if (!fp) {
		    fprintf(stderr, "Could not open %s for output\n",
			    options.outName);
		    MPI_Abort(MPI_COMM_WORLD, 1);
		}
	    }
	    else fp = stdout;

	    fprintf(fp, "In %d iterations and %d:%02d:%02d (h:m:s) time, found %d errors\n",
		    loopcount, hrs, mins, secs, err);
	    rate = wordsSent * sizeof(uint32_t) / totalTime;
	    fprintf(fp, "Per process rate (process %d) = %.2e GB/sec\n",
		    wrank, rate * 1.0e-9);
	    fflush(fp);
	    if (options.outName)
		fclose(fp);
	}

	/* Free buffers */
	free(req); free(stats);
	/* free elements of sbuf/rbuf */
	for (p=0; p<npartners; p++) {
	    free(sbuf[p]);
	    free(rbuf[p]);
	}
	free(sbuf); free(rbuf);
    }

    /* Free objects */
    if (activecomm != MPI_COMM_NULL) {
        MPI_Comm_free(&activecomm);
    }

    MPI_Finalize();

    return 0;
}

/*---------------------------------------------------------------------------
  These routines set and check the buffers by setting the specified pattern
  and checking it.
 --------------------------------------------------------------------------- */
static uint32_t Patterns[NPATTERNS] = {
    0xffffffff, 0xaaaaaaaa, 0x88888888, 0x80808080, 0x80008000, 0x80000000,
    0x00000000, 0x55555555, 0x77777777, 0x7f7f7f7f, 0x7fff7fff, 0x7fffffff };

void SetBuffer(uint32_t *buf, int size, int pattern)
{
    uint32_t val;
    int  i;

    if (pattern < NPATTERNS) {
	val = Patterns[pattern];
	for (i=0; i<size; i++)
	    buf[i] = val;
    }
    else {
	for (i=0; i<size; i++) {
	    buf[i] = Patterns[pattern % NPATTERNS];
	}
    }
}

int CheckBuffer( uint32_t *buf, int size, int pattern )
{
    uint32_t val;
    int  i;

    if (pattern < NPATTERNS) {
	val = Patterns[pattern];
	for (i=0; i<size; i++)
	    if (buf[i] != val) return 1;
    }
    else {
	for (i=0; i<size; i++) {
	    if (buf[i] != Patterns[pattern % NPATTERNS]) return 1;
	}
    }
    return 0;
}


int ErrTest(MPI_Status *status, int partner, int bufsize,
	    uint32_t *buffer, int pattern)
{
    int err = 0;
    int actsize;
    int from;

    from = status->MPI_SOURCE;
    MPI_Get_count(status, MPI_UINT32_T, &actsize);

    if (from != partner) {
	fprintf(stderr,
		"Message from %d should be from %d\n", from, partner);
	err++;
    }
    if (actsize != bufsize) {
	fprintf(stderr, "Message from %d is wrong size (%d != %d)\n",
		partner, actsize, bufsize );
	err++;
    }
    if (CheckBuffer( buffer, actsize, pattern)) {
	fprintf(stderr, "Message from %d is corrupt\n", partner);
	/* ToDo add error summary (which location and expected vs recved
	   data) */
	err++;
    }
    return err;
}

/* Command line processing */
void printUsage(void)
{
    fprintf(stderr, "netstress [-ttime x:y] [-help]\n");
}

int getOptions(int argc, char **argv, options_t *options)
{
    int i;

    /* Set defaults */
    options->runsecs      = 60;
    options->msgsizes     = 0;
    options->progress     = 0;
    options->progressFile = 0;
    options->outName      = 0;
    for (i=1; i<argc; i++) {
	if (strcmp(argv[i],"-v") == 0)  {
	    /* This allows multiple -v options to set higher levels of
	       debugging */
	    options->verbose++;
	}
	else if (strcmp(argv[i], "-ttime") == 0) {
	    int hrs, mins;
	    if (i < argc) {
		sscanf(argv[i+1], "%d:%d", &hrs, &mins);
		options->runsecs = 60 * (60 * hrs + mins);
	    }
	    else {
		fprintf(stderr, "-ttime missing value\n");
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
	else if (strcmp(argv[i], "-progress") == 0) options->progress = 1;
	else if (strcmp(argv[i], "-progressfile") == 0) {
	    i++;
	    if (i < argc)
		options->progressFile = strdup(argv[i]);
	    else {
		fprintf(stderr, "-progressfile missing value\n");
		fflush(stderr);
		return 1;
	    }
	}
	else if (strcmp(argv[i], "-help") == 0 ||
		 strcmp(argv[i], "-usage") == 0) {
	    printUsage();
	    return 1;
	}
    }
    return 0;
}

/* Memory Allocation */

/* Utility Routines */
void checkMem(void *ptr, size_t len, const char *msg)
{
    if (!ptr) {
	fprintf(stderr, "Unable to allocated %lu bytes for %s\n",
		(unsigned long)len, msg);
	fflush(stderr);
	MPI_Abort(MPI_COMM_WORLD, 1);
    }
}

/* Progress */
static int progressMajor = 10, progressFlush=70, progressShow=0;
FILE *fpprogress = 0;
void initProgress(const char *fname, int show, int major, int flush)
{
    if (!show) return;
    if (fname) {
	fpprogress = fopen(fname, "w");
	if (!fpprogress) {
	    fprintf(stderr, "Could not open %s for progress output\n", fname);
	    fflush(stderr);
	    MPI_Abort(MPI_COMM_WORLD, 1);
	}
    }
    else fpprogress = stdout;
    if (major > 0) progressMajor = major;
    if (flush > 0) progressFlush = flush;
    progressShow = show;
}

void showProgress(int it)
{
    if (!progressShow) return;
    if ( (it % progressFlush) == 0) {
	fputc('\n', fpprogress);
    } else if ( (it % progressMajor) == 0) {
	fputc('+', fpprogress);
    }
    else {
	fputc('.', fpprogress);
    }
    fflush(fpprogress);
}

void endProgress(void)
{
    if (!progressShow) return;
    fputc('\n', fpprogress);
    if (fpprogress != stdout) fclose(fpprogress);
}


