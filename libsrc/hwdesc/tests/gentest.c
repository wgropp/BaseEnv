/* Test of the generic hw description routines */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mpi.h"
#include "benvconf.h"
#include "benvutil.h"
#include "benvmpiutil.h"
#include "hwdescnew.h"

void printUsage(void);

int main(int argc, char **argv)
{
    hwdescParms parms;
    hwdescCtx *hwc;
    int i, np = 128, rc, wrank, wsize;
    int doThreeObjTest=0;

    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &wrank);
    MPI_Comm_size(MPI_COMM_WORLD, &wsize);
    BENV_DebugPostMPIInit();

    /* Initialize parms */
    parms.policy     = "B:B";
    parms.nnobj      = 4;
    parms.nobjs[0]   = 1;
    parms.nobjs[1]   = 2;
    parms.nobjs[2]   = 4;   /*numa regions */
    parms.nobjs[3]   = 32;  /* 32 cores on a chip */
    parms.forcedebug = 0;
    parms.printMap   = 0;
    parms.mapname    = 0;

    /* Update the parms structure from the environment, if present */
    BENV_HwdescParmUpdateFromEnv(&parms, "");

    /* Get arguments */
    for (i=1; i<argc; i++) {
	rc = BENV_DebugArgCommon(argc, argv, &i);
	BENV_ARGCHECK(rc,"error in debug options", return 1);
	rc = BENV_DebugArgRank(argc, argv, &i);
	BENV_ARGCHECK(rc,"error in debug rank", return 1);
	rc = BENV_HwdescArg(argc, argv, &i, 0, &parms);
	BENV_ARGCHECK(rc,"Error in hwdesc options", return 1);
	if (strcmp(argv[i], "-usage") == 0) {
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
	}
    }

//    printf("About to get hwdesc...\n"); fflush(stdout);
    if (wrank == 0) {
	/* Print to values in parms */
	printf("Parms:\n");
	BENV_HwdescParmPrint(stdout, &parms);
	printf("Cvar values:\n");
	BENV_HwdescCvarPrint(stdout);
	fflush(stdout);
    }
    /* Get a hw context */
    rc = BENV_HwdescGetDescGeneral(MPI_COMM_WORLD, BENV_HWDESC_USE_ALL, &parms,
				   &hwc);
    if (rc) {
	fprintf(stderr, "Error return from GetDescGeneral: %d\n", rc);
	fflush(stderr);
    }

//    printf("HW desc created; about to print\n"); fflush(stdout);
    /* Print out the representation, including the source of the information */
    BENV_HwdescPrintAll(stdout, MPI_COMM_WORLD, hwc, 0);

//    printf("About to free context\n"); fflush(stdout);
    BENV_HwdescFreeCtx(hwc);

    if (doThreeObjTest) {
	/* Try with no numa regions defined */
	parms.policy     = "B:B";
	parms.nnobj      = 3;
	parms.nobjs[0]   = 1;
	parms.nobjs[1]   = 2;
	parms.nobjs[2]   = 32; /* 32 cores on a chip */
	parms.forcedebug = 0;
	parms.printMap   = 0;
	parms.mapname    = 0;

	if (wrank == 0) {
	    printf("Parms with no NUMA\n");
	    /* Print to values in parms */
	    BENV_HwdescParmPrint(stdout, &parms);
	    BENV_HwdescCvarPrint(stdout);
	}
	/* Get a hw context */
	rc = BENV_HwdescGetDescGeneral(MPI_COMM_WORLD, BENV_HWDESC_USE_ALL,
				       &parms, &hwc);
	if (rc) {
	    fprintf(stderr, "Error return from GetDescGeneral: %d\n", rc);
	    fflush(stderr);
	}

//    printf("HW desc created; about to print\n"); fflush(stdout);
	/* Print out the representation, including the source of the
	   information */
	BENV_HwdescPrintAll(stdout, MPI_COMM_WORLD, hwc, 0);

//    printf("About to free context\n"); fflush(stdout);
	BENV_HwdescFreeCtx(hwc);
    }

    if (wrank == 0) { printf("About to finalize\n"); fflush(stdout); }
    MPI_Finalize();
    return 0;
}

void printUsage(void)
{
    fprintf(stderr, "gentest [-usage]\n");
    BENV_HwdescArgPrintUsage(stderr, 0, -1);
}
