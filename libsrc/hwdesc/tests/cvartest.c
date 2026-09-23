/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 * Copyright (C) by University of Illinois 2026
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

int main(int argc, char **argv)
{
    hwdescParms hwparms;
    int rc, wrank;
    int nsock, nnuma, ppn, qaddnodename;
    MPI_Init(0,0);

    // Set defaults
    nsock = -1;
    nnuma = -1;
    ppn = -1;
    qaddnodename = 1;
    // Process command line to allow testing of different cvar values
    for (int i=1; i<argc; i++) {
	rc = BENV_DebugArgCommon(argc, argv, &i);
	BENV_ARGCHECK(rc,"error in debug options\n",return 1);
	if (strcmp(argv[i], "-nsocket") == 0) {
	    i++;
	    rc = BENV_ArgGetint(i, argc, argv, &nsock);
	}
	else if (strcmp(argv[i], "-nnuma") == 0) {
	    i++;
	    rc = BENV_ArgGetint(i, argc, argv, &nnuma);
	}
	else if (strcmp(argv[i], "-ppn") == 0) {
	    i++;
	    rc = BENV_ArgGetint(i, argc, argv, &ppn);
	}
	else if (strcmp(argv[i], "-nonodename") == 0) {
	    qaddnodename = 0;
	}
	else {
	    if (wrank == 0) {
		fprintf(stderr, "Unrecognized option %s\n", argv[i]);
		fflush(stderr);
		MPI_Abort(MPI_COMM_WORLD, 1);
	    }
	}
    }
    MPI_Comm_rank(MPI_COMM_WORLD, &wrank);
    if (wrank == 0) {
	// Only test from rank 0
	// Can get from the environment: BENV_HWDESC_DEBUG_<cvarname>
	rc = BENV_HwdescCvarInit();
	//BENV_HwdescCvarSet(const char *name, int value);
	//available cvars: ppn, debug, sockets, numa, from_cvar, addnodename
	if (ppn > 0)
	    BENV_HwdescCvarSet("ppn", ppn);
	if (nsock > 0)
	    BENV_HwdescCvarSet("sockets", nsock);
	if (nnuma > 0)
	    BENV_HwdescCvarSet("numa", nnuma);
	if (1)
	    BENV_HwdescCvarSet("from_cvar",1);
	if (1)
	    BENV_HwdescCvarSet("debug",1);
	BENV_HwdescCvarSet("addnodename",qaddnodename);
	printf("cvar values:\n");
	BENV_HwdescCvarPrint(stdout);

	// Create an hwparms from the cvars
	rc = BENV_HwdescParmInit(&hwparms);
	rc = BENV_HwdescParmSetFromCvar(&hwparms);
	printf("hwparm values:\n");
	rc = BENV_HwdescParmPrint(stdout, &hwparms);
    }
    MPI_Finalize();
    return 0;
}
