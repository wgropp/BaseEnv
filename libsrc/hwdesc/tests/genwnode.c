/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 * Copyright (C) by University of Illinois 2022
 */
#include "benvconf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mpi.h"
#include "hwdescnew.h"
#include "seq.h"
#include "benvutil.h"
#include "benvmpiutil.h"

//int nodefirst(int nl, const int coords[], const int sizes[], int order);
void printUsage(void);

#define DEFAULT_DEPTH 32
int verbose=0;

/*
  Test GetDescGeneral along with routines to add additional node informatino
  if not present.

 */
/*
  genwnode - Example program to output the hardware hierarchy for an MPI program

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
  */
int main(int argc, char *argv[])
{
    int i, rc, provided, wrank;
    int nodelevel;
    int outperrank=0;
    char *outName = 0;
    FILE *outfp;
    int isexact;
    hwdescCtx *hwc=0;
    hwdescParms parms;

    MPI_Init_thread(&argc, &argv, MPI_THREAD_SINGLE, &provided);
    MPI_Comm_rank(MPI_COMM_WORLD, &wrank);

    BENV_DebugPostMPIInit();

    /* Process command line and environment options */
    BENV_HwdescCvarInit();
    /* Get default information from environment about hwdesc */
    BENV_HwdescParmInit(&parms);
    BENV_HwdescParmUpdateFromEnv(&parms, 0);
    for (i=1; i<argc; i++) {
	rc = BENV_DebugArgCommon(argc, argv, &i);
	BENV_ARGCHECK(rc,"error in debug options", return 1);
	rc = BENV_DebugArgRank(argc, argv, &i);
	BENV_ARGCHECK(rc,"error in debug rank", return 1);
	rc = BENV_HwdescArg(argc, argv, &i, "-hw", &parms);
	BENV_ARGCHECK(rc, "error in hwdesc options", return 1);

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
		printUsage();
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
    if (wrank == 0) {
	/* Print the values in parms */
	printf("Parms is:\n");
	BENV_HwdescParmPrint(stdout, &parms);
	fflush(stdout);
    }

    /* Try all methods to get the hw info */
    BENV_HwdescGetDescGeneral(MPI_COMM_WORLD, BENV_HWDESC_USE_ALL, &parms, &hwc);
    if (wrank == 0) printf("Validating hwc (%d levels) from config...\n",
			   hwc->nlevel);
    rc = BENV_HwdescValidate(MPI_COMM_WORLD, hwc);
    if (rc == 0) {
	if (wrank == 0) { printf("hwdesc is:\n"); }
	BENV_HwdescPrintAll(stdout, MPI_COMM_WORLD, hwc, 0);
    }
    else {
	printf("validate rc=%d\n", rc); fflush(stdout);
	BENV_HwdescPrintLocal(stdout, hwc, "hwc:");
    }

    if (wrank == 0) { printf("About to look for node info...\n"); fflush(stdout); }
    nodelevel = -1;
    BENV_HwdescFindObject(hwc, BENV_HWDESC_NODE, &nodelevel, &isexact);
    if (wrank == 0) {
	printf("Found %d; hwdesc has %d levels\n", nodelevel, hwc->nlevel);
	fflush(stdout);
    }
    if (nodelevel == -1 || nodelevel == hwc->nlevel - 1) {
	int flags;

	if (wrank == 0) {
	    printf("Getting separate node information...\n");
	    fflush(stdout);
	}
	/* Either no node or nothing below the node */
	flags = BENV_HWDESC_CONFIG_ALL;
	BENV_HwdescNodeGetConfig(flags, hwc);
	if (wrank == 0) {
	    printf("Got node config from %s:\n", hwc->source);
	    BENV_HwdescPrintLocal(stdout, hwc, "");
	}
	flags = BENV_HWDESC_ASSIGN_ALL;
	if (nodelevel == -1) {
	    /* Assume the lowest level is a node. This can happen if there
	       is only one node */
	    nodelevel = hwc->nlevel - 1;
	}
	{
	    MPI_Comm nodecomm = hwc->collinfo[nodelevel].objcomm;
	    int ronnode, nonnode;
	    MPI_Comm_rank(nodecomm, &ronnode);
	    MPI_Comm_size(nodecomm, &nonnode);
	    parms.rank    = ronnode;
	    parms.nonnode = nonnode;
	    /* FIXME: Compare with hwc for objects */
	    /* Need to look at hwc elements by type */
	    int socket, numa;
	    BENV_HwdescFindObject(hwc, BENV_HWDESC_SOCKET, &socket, &isexact);
	    if (isexact) parms.nobjs[1] = hwc->objinfo[socket].nobj;
	    BENV_HwdescFindObject(hwc, BENV_HWDESC_NUMA, &numa, &isexact);
	    if (isexact) parms.nobjs[2] = hwc->objinfo[numa].nobj;
	    fprintf(stdout, "\nAssignment from policy (policy=%s, nsocket=%d, nnuma=%d, np=%d):\n",
		parms.policy, parms.nobjs[1], parms.nobjs[2], parms.nonnode);
	    fflush(stdout);
	}

	BENV_HwdescNodeGetInfo(flags, &parms, hwc);
	BENV_HwdescNodeNormalize(hwc);
	if (wrank == 0) {
	    int socket, numa, core;
	    printf("Got node info:\n");
	    // objidx values for socket/numa/core
	    BENV_HwdescFindObject(hwc, BENV_HWDESC_SOCKET, &socket, &isexact);
	    if (isexact) printf("sockidx=%d, ", socket);
	    BENV_HwdescFindObject(hwc, BENV_HWDESC_NUMA, &numa, &isexact);
	    if (isexact) printf("numaidx=%d", numa);
	    BENV_HwdescFindObject(hwc, BENV_HWDESC_CORE, &core, &isexact);
	    if (isexact) printf("coreidx=%d", core);
	    fputc('\n', stdout);
	}
	if (wrank == 0) {
	    printf("Combined node info is:\n");
	}
	BENV_SeqBegin(MPI_COMM_WORLD);
	BENV_HwdescPrintLocal(stdout, hwc, "");
	fflush(stdout);
	BENV_SeqEnd(MPI_COMM_WORLD);
    }

    if (outperrank) {
	const char *fname;
	FILE *fp;
	fname = BENV_PerRankFilename("hwdesc-out-%d.txt", MPI_COMM_WORLD);
	fp = fopen(fname, "w");
	fprintf(fp, "PrintArray for %d\n", wrank);
	BENV_HwdescPrintLocal(fp, hwc, "Array");
	{
#define MAX_LEVEL 5
	    int coords[MAX_LEVEL], sizes[MAX_LEVEL], nlevels;
	    BENV_HwdescGetCoordTuple(MPI_COMM_WORLD, hwc,
				     MAX_LEVEL, coords, sizes, &nlevels);
	    fprintf(fp, "Coords for %d: ", wrank);
	    BENV_PrintIntTuple(fp, nlevels, coords, 0);
	    fputs(" in ", fp);
	    BENV_PrintIntTuple(fp, nlevels, sizes, 1);
	    fprintf(fp, "\t%d and %d\n", wrank,
		    BENV_ArrayComputeOffset(nlevels, coords, sizes, 0));
	}
	fclose(fp);
	free((void *)fname);
    }

    MPI_Barrier(MPI_COMM_WORLD);
    if (wrank == 0) printf("Validating hwc from config...\n");
    rc = BENV_HwdescValidate(MPI_COMM_WORLD, hwc);
    if (rc == 0)
	BENV_HwdescPrintAll(outfp, MPI_COMM_NULL, hwc, 0);
    else {
	printf("validate rc=%d\n", rc); fflush(stdout);
	BENV_HwdescPrintLocal(stdout, hwc, "hwc:");
    }

    if (verbose) {
	BENV_SeqBegin(MPI_COMM_WORLD);
	BENV_HwdescPrintLocal(outfp, hwc, "");
	BENV_SeqEnd(MPI_COMM_WORLD);
    }

    if (outName) {
	fclose(outfp);
    }

    MPI_Finalize();

    return 0;
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
