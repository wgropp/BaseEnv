/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 * Copyright (C) by University of Illinois 2022
 */
#include "benvconf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mpi.h"
#ifdef USE_OLD
#include "hwdesc.h"
#else
#include "hwdescnew.h"
#endif
#include "seq.h"
#include "benvutil.h"
#include "benvmpiutil.h"

int nodefirst(int nl, const int coords[], const int sizes[], int order);
void printUsage(void);

#define DEFAULT_DEPTH 32
int verbose=0;

/* Use MPI-4 features to find information about the hardware on which the
   MPI program is running */

/*D
  hwinfo - Example program to output the hardware hierarchy for an MPI program

Command Line Options:
+ -perrank - Provide additional output for each rank. See below
. -o fname - Send output to this filename instead of 'stdout'
. -hw-name n - Standard hwdesc options, with prefix '-hw'. These include
 '-hw-numnodes n' and '-hw-schedpolicy string'. See 'BENV_HwdescArg' for
 the full list
- -v - Enable debugging

  Notes:
  The output is written to 'stdout' by default and includes the MPI
  library version string and a description of the hardware hierarchy
  as produced by 'BENV_HwdescGetLocal'.

  If '-perrank' is selected, more data is written to a separate file for
  each rank. The file name is of the form 'hwdesc-out-nnn.txt', where 'nnn'
  is the rank in 'MPI_COMM_WORLD' of the process.

  This program requires an MPI version of at least 4.
  D*/
int main(int argc, char *argv[])
{
    int i, provided, wrank;
    int outperrank=0;
    char *outName = 0;
    FILE *outfp;
#ifdef USE_OLD
    hwdescCtx_t *hwc=0;
    hwdescParms_t parms;
#else
    hwdescCtx *hwc=0;
    hwdescParms parms;
#endif

    MPI_Init_thread(&argc, &argv, MPI_THREAD_SINGLE, &provided);
    MPI_Comm_rank(MPI_COMM_WORLD, &wrank);
#if MPI_VERSION < 4 && !defined(HAS_OMPI_HWSPLITTYPES) && !defined(HAS_MPI_COMM_TYPE_SHARED)
    if (wrank == 0) {
	fprintf(stderr,
	"This code requires MPI_VERSION 4 or MPI_Comm_split_type; this is version %d.%d\n",
	MPI_VERSION, MPI_SUBVERSION);
	fflush(stderr);
    }
    MPI_Finalize();
    return 0;
#else
    /* Process command line and environment options */
    BENV_HwdescCvarInit();
    /* Get default information from environment about hwdesc */
    BENV_HwdescParmInit(&parms);
    BENV_HwdescParmUpdateFromEnv(&parms, 0);
    for (i=1; i<argc; i++) {
	int rc;
	rc = BENV_HwdescArg(argc, argv, &i, "-hw", &parms);
	BENV_ARGCHECK(rc,"error in hwdesc options",return 1);

	if (strcmp(argv[i], "-perrank") == 0) {
	    outperrank = 1;
	}
	else if (strcmp(argv[i], "-o") == 0) {
	    i++;
	    if (i < argc)
		outName = strdup(argv[i]);
	    else {
		if (wrank == 0) {
		    fprintf(stderr, "-o missing value\n");
		    fflush(stderr);
		    MPI_Abort(MPI_COMM_WORLD,1);
		}
	    }
	}
	else if (strcmp(argv[i], "-v") == 0) {
	    verbose = 1;
	}
	else if (strcmp(argv[i], "-usage") == 0) {
	    if (wrank == 0)
		printUsage();
	}
	else {
	    if (wrank == 0) {
		fprintf(stderr, "Unrecognized option %s\n", argv[i]);
		fflush(stderr);
		MPI_Abort(MPI_COMM_WORLD, 1);
	    }
	    return 1;  /* should not reach here */
	}
    }

    /* If any cvars are set, update the parms */
    BENV_HwdescParmSetFromCvar(&parms);

    outfp = stdout;
    if (outName) {
	outfp = fopen(outName, "w");
    }

    /* Provide some info about the MPI version */
    if (wrank == 0) {
	char *v;
	int  vlen = MPI_MAX_LIBRARY_VERSION_STRING;
	int  vd, vsd;

	/* Get the string about the MPI implementation */
	v = (char *)malloc(vlen);
	MPI_Get_library_version(v, &vlen);
	/* Get the version from the library */
	MPI_Get_version(&vd, &vsd);
	fprintf(outfp, "MPI is %d.%d; %s\n", vd, vsd, v);
	fflush(outfp);
	free(v);
    }

    /* Try all methods to get the hw info */
    BENV_HwdescGetDescGeneral(MPI_COMM_WORLD, BENV_HWDESC_USE_ALL, &parms, &hwc);

#if 0
    {
	int wsize;
	MPI_Comm_size(MPI_COMM_WORLD, &wsize);
	int nobjs[2];
	const char *policy = getenv("SLURM_ASSIGNMENT_POLICY");
	/* Could create a routine to determine the number of nodes
	   using COMM_TYPE_SHARED */
	nobjs[0] = 6; /* nodes */ /* use -1 for "get from type shared? */
	/* Get sockets and policy from environment:
	   BENV_HWDESC_SOCKETSPERNODE . Can use other env for other hw
	   characteristics (allow ..._SPN for sockets-per-node?) */
	nobjs[1] = 2; /* sockets */
	/* This routine isn't ready... */
	BENV_HwdescGetLocalFromPolicy(policy, wsize, wrank, nobjs,
				      nobjs, hwdesc, hwdescLen, &hwlevel);
    }
#endif

    if (outperrank) {
	const char *fname;
	FILE *fp;
	fname = BENV_PerRankFilename("hwdesc-out-%d.txt", MPI_COMM_WORLD);
	fp = fopen(fname, "w");
	fprintf(fp, "PrintArray for %d\n", wrank);
#ifdef USE_OLD
	BENV_HwdescPrintCtxLocal(fp, hwc, "Array");
#else
	BENV_HwdescPrintLocal(outfp, hwc, "Array");
#endif
	{
#define MAX_LEVEL 5
	    int coords[MAX_LEVEL], sizes[MAX_LEVEL], nlevels;
	    BENV_HwdescGetCoordTuple(MPI_COMM_WORLD, hwc,
				     MAX_LEVEL, coords, sizes, &nlevels);
	    fprintf(fp, "Coords for %d: ", wrank);
	    BENV_PrintIntTuple(fp, nlevels, coords, 0);
	    fputs(" in ", fp);
	    BENV_PrintIntTuple(fp, nlevels, sizes, 1);
#ifdef USE_ODL
	    fprintf(fp, "\t%d and %d\n", wrank,
		    BENV_HwdescNodefirst(nlevels, coords, sizes, 0));
#else
	    fprintf(fp, "\t%d and %d\n", wrank,
		    BENV_ArrayComputeOffset(nlevels, coords, sizes, 0));
#endif
	}
	fclose(fp);
	free((void *)fname);
    }

    MPI_Barrier(MPI_COMM_WORLD);
    BENV_HwdescPrintAll(outfp, MPI_COMM_NULL, hwc, 0);

    if (verbose) {
	BENV_SeqBegin(MPI_COMM_WORLD);
#ifdef USE_OLD
	BENV_HwdescPrintCtxLocal(outfp, hwc, "");
#else
	BENV_HwdescPrintLocal(outfp, hwc, "");
#endif
	BENV_SeqEnd(MPI_COMM_WORLD);
    }

    if (outName) {
	fclose(outfp);
    }

    MPI_Finalize();

    return 0;
#endif /* MPI_VERSION < 4 && !defined(HAS_OMPI_HWSPLITTYPES) */
}

void printUsage(void)
{
    fprintf(stderr, "\
hwinfo - provide information about the hardware on which the program is\n\
         running\n\
 Command line arguments:\n\
 -perrank - output data to separate files for each rank\n\
 -o name - output file name\n\
 -v - verbose mode\n");
    BENV_HwdescArgPrintUsage(stderr, "-hw", -1);
}
