/*
 * Program to test I/O from multiple processes to a shared parallel file system.
 * This program is node-aware, and provides information about multiple I/O
 * streams from nodes to a shared file.
 */

/* A test of concurrent I/O using the posix API.
   The goal is to gain insight into the performance of concurrent I/O operations
   from multiple processes on multiple nodes into a shared (or possibly local)
   file system.
*/
#include "benvconf.h"
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include "mpi.h"
#include "hwdescnew.h"
#include "getsizes.h"
#include "benvdbg.h"
#include "benvutil.h"
#include "benvmpiutil.h"

typedef struct {
    long totalsize;          /* Total size of file to write and/or read */
    int  filepathslen,       /* Number of file paths */
	blocksizeslen,       /* Number of block sizes */
	numprocesseslen,     /* Number of numbers of processes */
	ntests;               /* Number of times to repeat each test */
    int verbose;             /* Generate more detailed output while running */
    int leavefiles;          /* If true, leave the test files instead of
			        deleting them */
    int rdorwrite;           /* 0 for read and 1 for write */
    int  *blocksizes,        /* blocksizes */
	*numprocesses;       /* array of number of processes */
    const char *outname;     /* Output file name */
    const char *basename;    /* Name of file to use (optional) */
    const char **filepaths;  /* file paths (directory names) to use */
} options_t;

typedef enum { OP_MIN, OP_AVG, OP_MAX } op_t;

#define BIGVAL 1.0e10

/* Array indexing for a 5-dimensional array. Set the array sizes with ind5len */
static int _n5_0, _n5_1, _n5_2, _n5_3, _n5_4;
#define ind5(_i0,_i1,_i2,_i3,_i4) \
    (_i4+_n5_4*(_i3+_n5_3*(_i2+_n5_2*(_i1+_n5_1*(_i0)))))
#define ind5len(_i0,_i1,_i2,_i3,_i4) \
    do {_n5_0=_i0;_n5_1=_i1;_n5_2=_i2;_n5_3=_i3;_n5_4=_i4;} while(0)
static int _n2_0, _n2_1;
#define ind2(_i0,_i1) \
    (_i1 + _n2_1*(_i0))
#define ind2len(_i0,_i1) \
    do {_n2_0=_i0; _n2_1=_i1;} while(0)

void getOptions(int, char **, options_t *);
int runTest(MPI_Comm comm, MPI_Comm nodecomm,
	    const char *path, const char *basename, int node, int np, int t,
	    int bsize, long totsize, int dowrite, int verbose,
	    double testtimes[]);
/* Reduce test information to 2d array */
void computeTestTime(int n, int *nprocs, int nprocslen,
		     int *bsizes, int bsizelen,
		     long totalsize, int ntests, double *timings, op_t op,
		     double *mintimeopen, double *mintimeio,
		     double *mintimeclose);
void computeRate(int *nprocs, int nprocslen,
		 int *bsizes, int bsizelen,
		 long totalsize, double *timemin, double*rate);

/* Output 2d table */
void outputTable(FILE *fp, int *nprocs, int nprocslen,
		 int *bsizes, int bsizelen,
		 double *timings);
char *createFilename(const char *path, const char *basename, int t, int node,
		     int rank);
void cleanupFile(const char *path, const char *basenaem,
		 int nt, int nn, int nr);
int GetTimingMax(MPI_Comm comm, double *timings, double *timingmax,
		 int timinglen);
void OutputResultTable(FILE *fp, const char *desc, options_t *options,
		       double *topen, double *tio, double *tclose, double *rate);
void printUsage(FILE *);

/*D pio - Measure parallel I/O performance

Command line arguments:
+ -v  - Verbose mode. Multiple options may be used to produce more detailed\n\
       output
. -totsize n - Total file size is n bytes
. -blksize n - I/O transfer size is n bytes
. -np n - n processes perform I/O concurrently
. -leavefiles - pio with not remove files that it creates
. -read - pio will read instead of write
. -path path - Add path to the directory paths to test. Multiple -path options
              may be used to test different file systewms during the same
              run of pio
. -basename name - name if the filename to use (not including the directory
                  path)
. -outname oname - write results, such as timing data, to oname
. -ntests n - Run each test n times
- -usage or -help     - Generate this output

Notes:
This program tests concurrent POSIX I/O from multiple MPI processes to a
single file.
D*/
int main(int argc, char **argv)
{
    int wrank, p, n, b, t, rc;
    int nodenum, nodeidx, noderank;
    options_t options;
    MPI_Comm nodecomm;
    int    timinglen;
    double *timings, /* For a 5D array of times:
			paths,procs,sizes,fields,tests */
	*timingmin, *timingmax;

    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &wrank);

    /* Read command line: filepaths, blocksizes, totalsize,
       number of processes per node */
    getOptions(argc, argv, &options);
    /* Confirm that all values of options are the same */
    rc = BENV_CheckSameInts(MPI_COMM_WORLD, &options.filepathslen, 6);
    if (rc != 0) {
	if (wrank == 0) {
	    fprintf(stderr, "Error in reading args: not all processes have same ints (options[%d] different)\n", rc-1);
	    MPI_Abort(MPI_COMM_WORLD, 1);
	}
    }
    rc = BENV_CheckSameInts(MPI_COMM_WORLD, options.blocksizes,
			    options.blocksizeslen);
    if (rc != 0) {
	if (wrank == 0) {
	    fprintf(stderr, "Error in reading args: not all processes have same ints (blocksizes[%d] different)\n", rc-1);
	    MPI_Abort(MPI_COMM_WORLD, 1);
	}
    }

    /* Handle defaults (make into a routine) */
    if (options.totalsize <= 0)
	options.totalsize = 16*1024*1024;
    if (options.filepathslen == 0) {
	options.filepathslen = 1;
	options.filepaths = (const char **)malloc(sizeof(char *));
	options.filepaths[0] = ".";
    }
    if (options.basename == 0)
	options.basename = "piotmp";
    if (options.outname == 0)
	options.outname = "pioresults.txt";
    if (options.blocksizeslen == 0) {
	options.blocksizes = (int *)malloc(2*sizeof(int));
	options.blocksizes[0] = 1024;
	options.blocksizes[1] = 16*1024;
	options.blocksizeslen = 2;
    }
    if (options.numprocesseslen == 0) {
	options.numprocesses = BENV_RstringToArray("1:2",
						   &options.numprocesseslen);
    }

    /* Determine node and process on node: Get a communicator of just
       the local processes. Future: Separate out different sockets*/
    /* Query: Do we need a comm of node leaders? */
    hwdescCtx *hwc;
    int nodelevel, isexact;
    rc = BENV_HwdescGetDescGeneral(MPI_COMM_WORLD, BENV_HWDESC_USE_ALL, 0, &hwc);
    rc = BENV_HwdescFindObject(hwc, BENV_HWDESC_NODE, &nodelevel, &isexact);
    if (nodelevel < 0) {
	fprintf(stderr, "Could not find node!\n");
	MPI_Abort(MPI_COMM_WORLD, 1);
    }
    nodecomm = hwc->collinfo[nodelevel].objcomm;
    nodenum  = hwc->collinfo[nodelevel].nSiblings;
    nodeidx  = hwc->collinfo[nodelevel].siblingNum;
    MPI_Comm_rank(nodecomm, &noderank);

    /* Run tests */
    timinglen = options.ntests * options.filepathslen*
	options.numprocesseslen * options.blocksizeslen * 3;
    timings = (double *)malloc(timinglen*sizeof(double));
    if (!timings) {
	MPI_Abort(MPI_COMM_WORLD,1);
    }
    /* Set the dimensions of the times array */
    ind5len(
	options.filepathslen,
	options.numprocesseslen,
	options.blocksizeslen,
	2,
	options.ntests
	);

    /* Run each test multiple times. Run in outer loop to distribute across
       other effects */
    for (t=0; t<options.ntests; t++) {
	for (n=0; n<options.filepathslen; n++) {
	    const char *path = options.filepaths[n];
	    if (options.verbose && wrank == 0) {
		printf("Using path %s\n", path); fflush(stdout);
	    }
	    for (p=0; p<options.numprocesseslen; p++) {
		int np = options.numprocesses[p];
		if (options.verbose && wrank == 0) {
		    printf("Using np %d processes on each node\n", np);
		    fflush(stdout);
		}
		for (b=0; b<options.blocksizeslen; b++) {
		    int bsize = options.blocksizes[b];
		    long totsize = options.totalsize;
		    double testtimes[3];
		    if (options.verbose && wrank == 0) {
			printf("Using block size %d\n", bsize); fflush(stdout);
		    }
		    /* All processes call but only the first np on each node
		       will run */
		    rc = runTest(MPI_COMM_WORLD, nodecomm,
				 path, options.basename,
				 nodeidx, np, t, bsize, totsize,
				 options.rdorwrite, options.verbose,
				 testtimes);
		    if (rc) {
			fprintf(stderr, "[%d] runTest aborted!\n", wrank);
			fflush(stderr);
			MPI_Abort(MPI_COMM_WORLD, 1);
		    }
		    /* Save timing results */
		    timings[ind5(n,p,b,0,t)] = testtimes[0]; /* open time */
		    timings[ind5(n,p,b,1,t)] = testtimes[1]; /* write time */
		    timings[ind5(n,p,b,2,t)] = testtimes[2]; /* close time */
		    if (options.verbose && wrank == 0) {
			printf("Times are %.2e (open), %.2e (i/o), %.2e (close)\n",
			       testtimes[0], testtimes[1], testtimes[2]);
			fflush(stdout);
		    }
		}
	    }
	    /* To avoid using too much file system space, delete the test
	       files after they are written */
	    if (!options.leavefiles) {
		if (options.verbose && wrank == 0) {
		    printf("cleanup files for test %d\n", t);
		    fflush(stdout);
		}
		/* Files are test-node-noderank, so we can delete in parallel */
		cleanupFile(path, options.basename, t, nodeidx, noderank);
	    }
	}
    }

    /* Cleanup the files */
    /* QUESTION: are there files for different bsizes? */
    if (!options.leavefiles) {
	if (options.verbose && wrank == 0) {
	    printf("cleanup files for all tests\n");
	    fflush(stdout);
	}
	for (t=0; t<options.ntests; t++) {
	    /* Files are test-node-noderank, so we can delete in parallel */
	    for (n=0; n<options.filepathslen; n++) {
		const char *path = options.filepaths[n];
		cleanupFile(path, options.basename, t, nodeidx, noderank);
	    }
	}
    }

    /* Report results */
    if (options.verbose && wrank == 0) {
	printf("Create test results\n");
	fflush(stdout);
    }

    /* Get both min or max over processes. Note not all processes always
       participate (look into ordering of timings) */
    /* Get minimum time over processes that participated. We take advantage
       of useing BIGVAL for unset values */
    if (wrank == 0) {
	timingmin = (double *)malloc(2*timinglen*sizeof(double));
	if (!timingmin) {
	    MPI_Abort(MPI_COMM_WORLD,1);
	}
	timingmax = timingmin + timinglen;
    }
    else
	timingmin = 0;

    /* Min over all participating processes */
    MPI_Reduce(timings, timingmin, timinglen, MPI_DOUBLE, MPI_MIN,
	       0, MPI_COMM_WORLD);

    /* Max over all participating processes */
    GetTimingMax(MPI_COMM_WORLD, timings, timingmax, timinglen);

    if (options.verbose && wrank == 0) {
	printf("Output the results file");
	fflush(stdout);
    }
    if (wrank == 0) {
	FILE *fp = fopen(options.outname, "w");
	double *rate, *mintimeopen, *mintimeio, *mintimeclose;
	int ratelen;

	/* Information about run */
	BENV_PrintRunInfo(fp, argc, argv);
	fprintf(fp, "Number of nodes = %d\n", nodenum);
	fprintf(fp, "np is number of processes per node participating in I/O\n");

	ind2len(options.numprocesseslen,options.blocksizeslen);
	ratelen = options.numprocesseslen*options.blocksizeslen;
	rate = (double *)malloc(ratelen*sizeof(double));
	mintimeopen  = (double *)malloc(3*ratelen*sizeof(double));
	mintimeio    = mintimeopen + ratelen;
	mintimeclose = mintimeio + ratelen;

	fprintf(fp, "Total file size %ld\n", options.totalsize);
	for (n=0; n<options.filepathslen; n++) {
	    const char *path = options.filepaths[n];
	    /* Make a table for each filepath */
	    fprintf(fp, "File path %s\n", path);

	    computeTestTime(n, options.numprocesses, options.numprocesseslen,
			    options.blocksizes, options.blocksizeslen,
			    options.totalsize, options.ntests, timingmin,
			    OP_MIN, mintimeopen, mintimeio, mintimeclose);

	    OutputResultTable(fp, "min", &options, mintimeopen, mintimeio,
			      mintimeclose, rate);

#if 0
	    fputs("IO (write) Time per process (min)\n", fp);
	    outputTable(fp, options.numprocesses, options.numprocesseslen,
			options.blocksizes, options.blocksizeslen,
			mintimeio);

	    fputs("Close Time per process (min)\n", fp);
	    outputTable(fp, options.numprocesses, options.numprocesseslen,
			options.blocksizes, options.blocksizeslen,
			mintimeclose);

	    /* Compute rate and output that */
	    fputs("Rate per active process (min)\n", fp);
	    computeRate(options.numprocesses, options.numprocesseslen,
			options.blocksizes, options.blocksizeslen,
			options.totalsize, mintimeio, rate);
	    outputTable(fp, options.numprocesses, options.numprocesseslen,
			options.blocksizes, options.blocksizeslen,
			rate);
#endif
	    /* Same output, but this time for the average time across tests */
	    computeTestTime(n, options.numprocesses, options.numprocesseslen,
			    options.blocksizes, options.blocksizeslen,
			    options.totalsize, options.ntests, timingmin,
			    OP_AVG, mintimeopen, mintimeio, mintimeclose);

	    OutputResultTable(fp, "avg over procs of min",
			      &options, mintimeopen, mintimeio,
			      mintimeclose, rate);
#if 0
	    fputs("IO (write) Time per process (avg over procs of min)\n", fp);
	    outputTable(fp, options.numprocesses, options.numprocesseslen,
			options.blocksizes, options.blocksizeslen,
			mintimeio);

	    fputs("Close Time per process (avg over procs of min)\n", fp);
	    outputTable(fp, options.numprocesses, options.numprocesseslen,
			options.blocksizes, options.blocksizeslen,
			mintimeclose);

	    /* Compute rate and output that */
	    fputs("Rate per active process (avg over procs of min)\n", fp);
	    computeRate(options.numprocesses, options.numprocesseslen,
			options.blocksizes, options.blocksizeslen,
			options.totalsize, mintimeio, rate);
	    outputTable(fp, options.numprocesses, options.numprocesseslen,
			options.blocksizes, options.blocksizeslen,
			rate);
#endif
	    /* Starting here, mintimeio and mintimeclose are actually
	       the max times; we're just reusing the data area */
	    computeTestTime(n, options.numprocesses, options.numprocesseslen,
			    options.blocksizes, options.blocksizeslen,
			    options.totalsize, options.ntests, timingmax,
			    OP_MAX, mintimeopen, mintimeio, mintimeclose);

	    OutputResultTable(fp, "max", &options, mintimeopen, mintimeio,
			      mintimeclose, rate);

	    /* Same output, but this time for the average time across tests */
	    computeTestTime(n, options.numprocesses, options.numprocesseslen,
			    options.blocksizes, options.blocksizeslen,
			    options.totalsize, options.ntests, timingmax,
			    OP_AVG, mintimeopen, mintimeio, mintimeclose);

	    OutputResultTable(fp, "avg over procs of max",
			      &options, mintimeopen, mintimeio,
			      mintimeclose, rate);

	    /* Consider also: Per node rate, rate per job */
	}
	fclose(fp);
	free(timingmin);
	free(mintimeopen);
	free(rate);
    }

    MPI_Finalize();
    return 0;
}

void getOptions(int argc, char **argv, options_t *options)
{
    int i, npaths=0;

    /* Set defaults */
    options->verbose         = 0;
    options->filepathslen    = 0;
    options->blocksizeslen   = 0;
    options->numprocesseslen = 0;
    options->totalsize       = 0;
    options->ntests          = 1;
    options->blocksizes      = 0;
    options->numprocesses    = 0;
    options->leavefiles      = 0;
    options->rdorwrite       = 1; /* Write by default */
    options->filepaths       = 0;
    options->basename        = 0;
    options->outname         = 0;

    for (i=1; i<argc; i++) {
	//printf("Processing arg %s\n", argv[i]);
	if      (strcmp(argv[i],"-v") == 0)  {
	    /* Verbose for debugging */
	    /* This allows multiple -v options to set higher levels of
	       debugging */
	    options->verbose++;
	}
	else if (strcmp(argv[i], "-totsize") == 0) {
	    /* Total size of file in bytes */
	    i++;
	    if (i < argc) {
		const char *p = argv[i];
		int  err, val;
		val  = BENV_ScanScaledInt(&p, 1, &err);

		if (err == 0)
		    options->totalsize = val;
	    }
	}
	else if (strcmp(argv[i], "-blksize") == 0) {
	    /* I/O block size (for each transfer) */
	    i++;
	    if (i < argc)
		options->blocksizes = BENV_RstringToArray(argv[i],
						  &options->blocksizeslen);
	}
	else if (strcmp(argv[i], "-np") == 0) {
	    /* Number of processes to use (may be a subset of MPI_COMM_WORLD) */
	    i++;
	    if (i < argc)
		options->numprocesses = BENV_RstringToArray(argv[i],
						    &options->numprocesseslen);
	}
	else if (strcmp(argv[i], "-leavefiles") == 0) {
	    /* Do not remove files that are written */
	    options->leavefiles      = 1;
	}
	else if (strcmp(argv[i], "-read") == 0) {
	    /* Read *instead* of write (file must exist) */
	    options->rdorwrite = 0;
	}
	else if (strcmp(argv[i], "-path") == 0) {
	    /* Directory path to use for file. Note may be used multiple
	       times in argument list to allow checking multiple file
	       systems during the same run */
	    i++;
	    if (i < argc) {
		if (!options->filepaths) {
		    npaths = 5;
		    options->filepaths = (const char **)malloc(npaths*sizeof(char *));
		}
		else if (npaths <= options->filepathslen) {
		    npaths += 5;
		    options->filepaths = (const char **)realloc(options->filepaths,
							  npaths*sizeof(char *));
		}
		options->filepaths[options->filepathslen++] = strdup(argv[i]);
	    }
	}
	else if (strcmp(argv[i], "-basename") == 0) {
	    /* Base name for files written for test */
	    i++;
	    if (i < argc)
		options->basename = argv[i];
	}
	else if (strcmp(argv[i], "-outname") == 0) {
	    /* Name of file to which results are written */
	    i++;
	    if (i < argc)
		options->outname = argv[i];
	}
	else if (strcmp(argv[i], "-ntests") == 0) {
	    /* Number of times each test is run */
	    i++;
	    options->ntests = atoi(argv[i]);
	}
	else if (strcmp(argv[i], "-usage") == 0 ||
		 strcmp(argv[i], "-help")  == 0) {
	    printUsage(stderr);
	}
	else {
	    /* Usage */
	    fprintf(stderr, "Unrecognized option %s\n", argv[i]);
	    printUsage(stderr);
	    fflush(stderr);
	    break;
	}
    }
}

/* Run a single test of I/O. This is collective over comm.
   Return 0 on success, -1 on failure (most likely, unable to open file) */
int runTest(MPI_Comm comm, MPI_Comm nodecomm,
	    const char *path, const char *basename, int node, int np, int t,
	    int bsize, long totsize, int dowrite, int verbose,
	    double testtimes[])
{
    char  *wbuf;
    int   fd=-1;
    int   i, nb, err, nrank, rc=0;

    MPI_Comm_rank(nodecomm, &nrank);

    /* Determine the number of blocks to read or write */
    nb = totsize / bsize;

    /* open or create file (write only for now). Only files accessing the file
       perform an open */
    /* ? any optional flags, e.g., O_NOATIME */
    if (nrank < np) {
	/* Create file name from path, node, rank in node */
	char *fname = createFilename(path, basename, t, node, nrank);

	if (dowrite)
	    fd = open(fname, O_CREAT | O_WRONLY, 0600);
	else
	    fd = open(fname, O_RDONLY);
	if (fd < 0) {
	    fprintf(stderr, "Unable to open file %s: %s\n", fname,
		    strerror(errno));
	    fflush(stderr);
	    free(fname);
	    rc = -1;
	    /* Note that this return means that the program needs to abort,
	       since this routine is collective over comm */
	    return rc;
	}
	free(fname);
    }
    else
	fd = -1;

    /* Perform the operation, including timing */

    /* Only allocate block size buffer?? */
    wbuf = (char *)malloc(bsize);
    for (i=0; i<bsize; i++) wbuf[i] = (char)i;

    MPI_Barrier(comm);
    if (fd >= 0) {
	double t0, t1, t2;
	t0 = MPI_Wtime();
	for (i=0; i<nb; i++) {
	    if (dowrite)
		err = write(fd, wbuf, bsize);
	    else
		err = read(fd, wbuf, bsize);
	    if (err != bsize) {
		/* CHECK ERR */
		fprintf(stderr, "write failed in %s: returned %d\n", path, err);
		if (err < 0) perror("write error: ");
		fflush(stderr);
		MPI_Abort(MPI_COMM_WORLD,1);
	    }
	}
	t1 = MPI_Wtime();

	/* Close the file. Query: how (or do we) ensure data written to
	   media? */
	close(fd);
	t2 = MPI_Wtime();

	/* Report timing data (only for processes that did I/O */
	testtimes[0] = t1-t0;  /* IO time */
	testtimes[1] = t2-t1;  /* File close time */
	testtimes[2] = 0;
    }
    else {
	for (i=0; i<3; i++) testtimes[i] = BIGVAL;
    }

    free(wbuf);
    return rc;
}

/* Just the 2d values, with labels */
void outputTable(FILE *fp, int *nprocs, int nprocslen,
		 int *bsizes, int bsizelen, double *timings)
{
    int b, p;

    /* Headings */
    fputs("np", fp);
    for (b=0; b<bsizelen; b++) {
	int bsize = bsizes[b];
	fprintf(fp, "\t%d", bsize);
    }
    fputs("\n", fp);
    for (p=0; p<nprocslen; p++) {
	int np = nprocs[p];
	fprintf(fp,"%d", np);
	for (b=0; b<bsizelen; b++) {
	    /* Option to select other times */
	    fprintf(fp, "\t%.2e", timings[ind2(p,b)]);
	}
	fputs("\n", fp);
    }
}

void computeTestTime(int n, int *nprocs, int nprocslen,
		     int *bsizes, int bsizelen,
		     long totalsize, int ntests, double *timings, op_t op,
		     double *mintimeopen, double *mintimeio,
		     double *mintimeclose)
{
    int b, p, t;

    for (p=0; p<nprocslen; p++) {
	//int np = nprocs[p];
	for (b=0; b<bsizelen; b++) {
	    //int bsize = bsizes[b];
	    double tvalopen, tvalio, tvalclose;
	    if (op == OP_MIN) {
		tvalopen = timings[ind5(n,p,b,0,0)];
		tvalio = timings[ind5(n,p,b,0,1)];
		tvalclose = timings[ind5(n,p,b,2,0)];
		for (t=1; t<ntests; t++) {
		    if (timings[ind5(n,p,b,0,t)] < tvalopen)
			tvalopen = timings[ind5(n,p,b,0,t)];
		    if (timings[ind5(n,p,b,1,t)] < tvalio)
			tvalio = timings[ind5(n,p,b,1,t)];
		    if (timings[ind5(n,p,b,2,t)] < tvalclose)
			tvalclose = timings[ind5(n,p,b,2,t)];
		}
	    }
	    else if (op == OP_AVG) {
		tvalopen  = 0;
		tvalio    = 0;
		tvalclose = 0;
		int acttests = 0;
		for (t=0; t<ntests; t++) {
		    if (timings[ind5(n,p,b,1,t)] < BIGVAL) {
			tvalopen  += timings[ind5(n,p,b,0,t)];
			tvalio    += timings[ind5(n,p,b,1,t)];
			tvalclose += timings[ind5(n,p,b,2,t)];
			acttests++;
		    }
		}
		if (acttests > 1) {
		    tvalopen  /= acttests;
		    tvalio    /= acttests;
		    tvalclose /= acttests;
		}
	    }
	    else if (op == OP_MAX) {
		tvalopen  = timings[ind5(n,p,b,0,0)];
		tvalio    = timings[ind5(n,p,b,1,0)];
		tvalclose = timings[ind5(n,p,b,2,0)];
		for (t=1; t<ntests; t++) {
		    double tval = timings[ind5(n,p,b,0,t)];
		    if (tval > tvalopen && tval < BIGVAL)
			tvalopen = tval;
		    tval = timings[ind5(n,p,b,1,t)];
		    if (tval > tvalio && tval < BIGVAL)
			tvalio = tval;
		    tval = timings[ind5(n,p,b,2,t)];
		    if (tval > tvalclose && tval < BIGVAL)
			tvalclose = tval;
		}
	    }
	    mintimeopen[ind2(p,b)]  = tvalopen;
	    mintimeio[ind2(p,b)]    = tvalio;
	    mintimeclose[ind2(p,b)] = tvalclose;
	}
    }
}

/* Compute rate per active process. */
void computeRate(int *nprocs, int nprocslen,
		 int *bsizes, int bsizelen,
		 long totalsize, double *timemin, double*rate)
{
    int b, p;

    for (p=0; p<nprocslen; p++) {
	//int np = nprocs[p];
	for (b=0; b<bsizelen; b++) {
	    int bsize = bsizes[b];
	    int  nb = totalsize / bsize;
	    long acttotallen = nb * bsize;

	    /* Compute rate = actual total size * np / tvalio */
	    /* Option to select other times */
	    rate[ind2(p,b)] = acttotallen / timemin[ind2(p,b)];
	}
    }
}

/* Remove the generated files */
void cleanupFile(const char *path, const char *basename, int nt, int nn, int nr)
{
    char *fname = createFilename(path, basename, nt, nn, nr);
    unlink(fname);
    free(fname);
}

/* t is test #, node is node #, nrank is rank on node */
char *createFilename(const char *path, const char *basename,
		     int t, int node, int nrank)
{
    char *fname;
    int   fnamelen;

    fnamelen = strlen(path) + 4*11 + 20;
    fname = (char *)malloc(fnamelen);
    if (!fname) return 0;
    /* Create file name from path, node, rank in node */
    snprintf(fname, fnamelen, "%s/%s-%d-%d-%d.bin",
	     path, basename, t, node, nrank);

   return fname;
}

/* ----------------------------------------------------------------------- */
/* Routines to analyze the timing data and produce the output tables */

int GetTimingMax(MPI_Comm comm, double *timings, double *timingmax,
		 int timinglen)
{
    int i, r;
    double *sbuf;

    MPI_Comm_rank(comm, &r);
    if (r > 0) {
	sbuf = (double *)malloc(timinglen*sizeof(double));
	if (!sbuf) MPI_Abort(MPI_COMM_WORLD, 1);
    }
    else sbuf = timingmax;
    /* Copy timings to timingmax, but set BIGVAL entries to 0 */
    for (i=0; i<timinglen; i++) {
	if (timings[i] >= BIGVAL)
	    sbuf[i] = 0;
	else
	    sbuf[i] = timings[i];
    }

    /* Note that MPI_IN_PLACE can only be provided at the root. Other
       processes must provide the source buffer as the first (send)
       argument */
    if (r == 0) sbuf = MPI_IN_PLACE;
    MPI_Reduce(sbuf, timingmax, timinglen, MPI_DOUBLE, MPI_MAX,
	       0, MPI_COMM_WORLD);
    if (r > 0) free(sbuf);

    return 0;
}


void OutputResultTable(FILE *fp, const char *desc, options_t *options,
		       double *topen, double *tio, double *tclose, double *rate)
{
    fprintf(stdout, "starting output results table for %s\n", desc);
    fflush(stdout);
    fprintf(fp, "Open Time per process (%s)\n", desc);
    outputTable(fp, options->numprocesses, options->numprocesseslen,
		options->blocksizes, options->blocksizeslen, topen);

    fprintf(fp, "IO (write) Time per process (%s)\n", desc);
    outputTable(fp, options->numprocesses, options->numprocesseslen,
		options->blocksizes, options->blocksizeslen, tio);

    fprintf(fp, "Close Time per process (%s)\n", desc);
    outputTable(fp, options->numprocesses, options->numprocesseslen,
		options->blocksizes, options->blocksizeslen, tclose);

    /* Compute rate and output that */
    fprintf(fp, "Rate per active process (%s)\n", desc);
    computeRate(options->numprocesses, options->numprocesseslen,
		options->blocksizes, options->blocksizeslen,
		options->totalsize, tio, rate);
    outputTable(fp, options->numprocesses, options->numprocesseslen,
		options->blocksizes, options->blocksizeslen,
		rate);

    fprintf(stdout, "done output results tables for %s\n", desc);
    fflush(stdout);
}

/* Provide usage information on the program */
void printUsage(FILE *fp)
{
    fprintf(fp, "pio - Measure parallel I/O performance\n");
fprintf(stderr, "Command line arguments:\n");
fprintf(stderr, "\
 -v  - Verbose mode. Multiple options may be used to produce more detailed\n\
       output\n\
 -totsize n - Total file size is n bytes\n\
 -blksize n - I/O transfer size is n bytes\n\
 -np n - n processes perform I/O concurrently\n\
 -leavefiles - pio with not remove files that it creates\n\
 -read - pio will read instead of write\n\
 -path path - Add path to the directory paths to test. Multiple -path options\n\
              may be used to test different file systewms during the same\n\
              run of pio\n\
 -basename name - name if the filename to use (not including the directory\n\
                  path)\n\
 -outname oname - write results, such as timing data, to oname\n\
 -ntests n - Run each test n times\n\
 -usage\n\
 -help     - Generate this output\n");
}
