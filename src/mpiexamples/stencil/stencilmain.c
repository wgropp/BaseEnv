#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mpi.h"
#include "benvutil.h"
#include "seq.h"
#include "decomp.h"
#include "meshsup.h"
#include "subarrays.h"
#include "stencil.h"

typedef enum { C_ORDER, F_ORDER } ordertype;

static int debugDecomp = 0;

void CheckEnoughArgs(int i, int argc, const char *argname);
void usage(FILE *);
void printIntArray(FILE *fp, int n, const int ar[]);
int compareMesh(int ndims, const int coords[], const int gsizes[],
		const int loffsets[], const int lsizes[],
		const int lsizesdecl[], const int *buf);

typedef struct {
    int sizes[MAX_DIMS],   /* Size of the global mesh */
	psizes[MAX_DIMS];  /* Nmber of processes in each dimension */
    ordertype order;       /* Storage order for the mesh */
    const char *fnamein,   /* Optional input file with initial data */
	*fnameout;         /* Output file */
    int distribkind;       /* 0 for (nearly)equal, 1 for "HDF-style" */
    int halowidth;         /* halowidth for stencil */
    int useDarray;         /* use TypeCreateDarray */
    int useSubarray;       /* use TypeCreateSubarray */
    int niter;             /* Number of iterations for sweep */
} options_t;

void getOptions(int argc, char **argv, options_t *options);

int main(int argc, char **argv)
{
    int provided;
    options_t options;
    int pcoords[MAX_DIMS], crank;
    int i, it, ndims=3;
    double *lmeshbuf, *lmeshbufnew;
    int gindex[MAX_DIMS], larraylen[MAX_DIMS], larraylenhalo[MAX_DIMS],
	larraystarts[MAX_DIMS], periods[MAX_DIMS];
    MPI_Comm cartcomm;
    timingData td;
    cartdecompCtx *decompctx;

    MPI_Init_thread(&argc, &argv, MPI_THREAD_FUNNELED, &provided);

    /* Get commandline and set any defaults */
    getOptions(argc, argv, &options);

    /*
      Decomposition. Returns a communicator and information about
      the process decomposition, including sizes and neighboring processes
        Options: Cart_create, user-defined, Ncart_create
    */

    /* Get the process decomposition */
    for (i=0; i<ndims; i++) periods[i] = 0;
    BENV_CartDecompCreate(ndims, options.psizes, periods, MPI_COMM_WORLD, 1,
			  &decompctx);
    cartcomm = decompctx->cartcomm;
    MPI_Comm_rank(cartcomm, &crank);

    /* Define the local mesh */
    /* Decompose the mesh in each direction, according to the process
       decomposition */
    for (i=0; i<ndims; i++) {
	BENV_DarrayDecomposition(options.sizes[i], options.psizes[i],
				 pcoords[i], options.distribkind,
				 &gindex[i], &larraylen[i]);
    }

    /* Debugging: output the computed decomposition of the array */
    if (debugDecomp) {
	BENV_SeqBegin(cartcomm);
	fprintf(stdout, "%d: pcoords ", crank);
	BENV_PrintIntList(stdout, ndims, pcoords, 0);
	fputs(", array coords ", stdout);
	BENV_PrintIntList(stdout, ndims, gindex, 0);
	fputs(", sizes ", stdout);
	BENV_PrintIntList(stdout, ndims, larraylen, 1);
	fflush(stdout);
	BENV_SeqEnd(cartcomm);
    }

    /*
      Datatypes for patches. These are for I/O support ONLY
      Options:
    */

    /* Do any initialization for the halo exchange */
    haloexchangeInit(decompctx,
		     options.halowidth, options.useSubarray, larraylen,
		     larraylenhalo);

    /*
      Initial data (read data or set values)
    */
    for (it=0; it<options.niter; it++) {
	double *tmp;
	/*
	  Halo exchange
	  Options: p2p, coll, nbrcoll, rma
	  ? nonblocking version with edges separate
	*/
	haloexchange(cartcomm, lmeshbuf, &td);
	if (crank == 0) {
	    /*fprintf(stdout, "Exchange completed\n");*/
	    fprintf(stdout, "Halo exchange:\n\tWords\t%ld\n\tPostComms\t%.2e\n\tWait\t%.2e\n\tRate (B/sec)\t%.2e\n",
		    td.commlen, td.tcommPost,td.tcommWait,
		    td.commlen*sizeof(int)/(td.tcommPost+td.tcommWait));
	}

	/*
	  Sweep
	  Options: loops, threaded, possible GPU
	  Option for separate loops over interior/edges
	*/
	sweep(lmeshbuf, lmeshbufnew, larraylen[0], larraylen[1], larraylen[2]);

	/* Swap buffers */
	tmp         = lmeshbuf;
	lmeshbuf    = lmeshbufnew;
	lmeshbufnew = tmp;

    } /* end of loop over it */

    /*
      Final output (and output of results)
    */

    /*
      For testing, comparison to standard solution (read data)
    */

    /* Cleanup */

    MPI_Finalize();
    return 0;
}

void getOptions(int argc, char **argv, options_t *options)
{
    int i, ndims=3;

    /* Set defaults */
    for (i=0; i<MAX_DIMS; i++) {
	options->psizes[i] = 0;
	options->sizes[i]  = 1;
    }
    options->order=C_ORDER;
    options->fnamein=0;
    options->fnameout=0;
    options->distribkind = 0;
    options->halowidth=1;
    options->useDarray = 0;
    options->useSubarray = 0;
    options->niter = 10;

    /* Get options */
    for (i=1; i<argc; i++) {
	if (strcmp("--sizes", argv[i]) == 0) {
	    int k;
	    CheckEnoughArgs(i+ndims-1, argc, "--sizes");
	    for (k=0; k<ndims; k++) options->sizes[k] = atoi(argv[++i]);
	}
	else if (strcmp("--psizes", argv[i]) == 0) {
	    int k;
	    CheckEnoughArgs(i+ndims-1, argc, "--psizes");
	    for (k=0; k<ndims; k++) options->psizes[k] = atoi(argv[++i]);
	}
	else if (strcmp("--use-subarray-halo", argv[i]) == 0) {
	    options->useSubarray = 1;
	}
	else if (strcmp("--halowidth", argv[i]) == 0) {
	    CheckEnoughArgs(i, argc, "--halowidth");
	    options->halowidth = atoi(argv[++i]);
	}
	else if (strcmp("--niter", argv[i]) == 0) {
	    CheckEnoughArgs(i, argc, "--niter");
	    options->niter = atoi(argv[++i]);
	}
	/* These are for I/O support */
	else if (strcmp("--fortran-order", argv[i]) == 0) {
	    options->order = F_ORDER;
	}
	else if (strcmp("--distrib-hdf", argv[i]) == 0) {
	    options->distribkind = 1;
	}
	else if (strcmp("--use-darray", argv[i]) == 0) {
	    options->useDarray = 1;
	}
	else if (strcmp("--fnamein", argv[i]) == 0) {
	    CheckEnoughArgs(i, argc, "--fnamein");
	    options->fnamein = argv[++i];
	}
	else if (strcmp("--fnameout", argv[i]) == 0) {
	    CheckEnoughArgs(i, argc, "--fnameout");
	    options->fnameout = argv[++i];
	}
	else {
	    fprintf(stderr, "Unrecognized argument %s\n", argv[i]);
	    usage(stderr);
	    abort();
	}
    }

    /* Sanity check */
    if (!options->fnamein) {
	options->fnamein = "mesh.bin";
    }
    if (!options->fnameout) {
	options->fnameout = "meshout.bin";
    }
}

void CheckEnoughArgs(int i, int argc, const char *argname)
{
    if (i <= 0 || i >= argc) {
	fprintf(stderr, "Not enough values for %s\n", argname);
	abort();
    }
}

void usage(FILE *fp)
{
    fprintf(fp, "\
stencil-... [--sizes n1 ...] [--psizes p1 ... ] [--niter n] [--halowidth h]\n\
            [--use-subarray-halo] [--fortran-order] [--distrib-hdf]\n\
            [--use-darray] [--fnamein filename] [--fnameout filename]\n");
}

