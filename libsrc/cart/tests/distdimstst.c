/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 * Copyright (C) by University of Illinois 2024
 */

#include "benvconf.h"
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>
#define MPI_Comm int
#include "benvdbg.h"
#include "benvutil.h"
#include "cartimpl.h"

/*int readlistofints(const char *s, int **vals);*/
void printUsage(FILE *fp);
//hwlevs_t *createHwdescFromLevs(int nlevs, int *levsize, int rank);
//void printHWDesc(FILE *fp, hwlevs_t *hwlevs);
//void RankToCoords(int ndims, const int dims[], int rank, int coords[]);

CDBGDECL(DISTDIMS);  /* initialize to _CVAR_VERBOSITY_DETAIL */

int main(int argc, char **argv)
{
    int ndims=0, *dims=0, periods[10], nlevs=0, *levsize=0, *wranks=0, nranks=0;
    int i, np, k, r;
    cartHierarchy *carth_ptr;
    hwlevs_t *hwlevs;
    static int defhw[] = { 4, 2, 6 }, defdims[] = { 0, 6 };

    /* Read input for dims and for hw hierarchy */
    for (i=1; i<argc; i++) {
	if (strcmp(argv[i], "--dims") == 0) {
	    i++;
	    dims = BENV_RstringToArray(argv[i], &ndims);
	}
	else if (strcmp(argv[i], "--ndims") == 0) {
	    i++;
	    ndims = atoi(argv[i]);
	    dims  = (int *)calloc(ndims,sizeof(int));
	}
	else if (strcmp(argv[i], "--hw") == 0) {
	    i++;
	    levsize = BENV_RstringToArray(argv[i], &nlevs);
	}
	else if (strcmp(argv[i], "--default") == 0) {
	    nlevs = 3;
	    levsize = defhw;
	    ndims = 2;
	    dims = defdims;
	}
	else if (strcmp(argv[i], "--ranks") == 0) {
	    i++;
	    wranks = BENV_RstringToArray(argv[i], &nranks);
	}
	else if (strcmp(argv[i], "--allranks") == 0) {
	    nranks = -1;
	}
	else if (strcmp(argv[i], "-v") == 0) {
	    CDBGINCRVAL(DISTDIMS);
	}
	else {
	    fprintf(stderr, "Unrecognized argument %s\n", argv[i]);
	    printUsage(stderr);
	    return 1;
	}
    }

    if (!nlevs || !dims) {
	fprintf(stderr, "Missing hw and/or dims!\n");
	printUsage(stderr);
	return 1;
    }

    /* What is the test */
    /* Convert (nlevs,levsize) into the hwlevs structure, which includes
       information on "this process" */
    /* Total number of processes is produce of the levsizes */
    np = 1;
    for (k=0; k<nlevs; k++)
	np *= levsize[k];

    /* Handle no ranks specified */
    if (nranks == 0) {
	wranks = (int *)malloc(sizeof(int));
	wranks[0] = 0;
	nranks = 1;
    }
    else if (nranks < 0) {
	/* Handle --allranks */
	nranks = np;
	wranks = (int *)malloc(nranks*sizeof(int));
	for (i=0; i<nranks; i++) wranks[i] = i;
    }

    /* Initialize */
    for (i=0; i<10; i++) {
	periods[i] = 0;
    }

    for (r=0; r<nranks; r++) {
	printf("For rank %d:\n", wranks[r]);

	/* Form hwlevs from levels */
	hwlevs = BENVi_CreateHwdescFromLevs(nlevs, levsize, wranks[r]);
	CDBGV(DISTDIMS,BASIC,"hwlevs from %d levels\n",nlevs);
	CDBGCMD(DISTDIMS,BASIC,BENVi_PrintHWDesc(cvar_vfp,hwlevs));

	/* FIXME: Need a loop over hwlevs and/or dims */
	BENVi_DistributeDimsOverHW(hwlevs, ndims, dims, periods, &carth_ptr);

	fprintf(stdout, "Created carth:\n");
	BENVi_PrintCarth(stdout, wranks[r], carth_ptr);

	/* Output results */
	fprintf(stdout, "For input dims ");
	BENV_PrintIntTuple(stdout, ndims, dims, 1);

#if 0
	/* Note: Currently do not update hwlevs */
	fprintf(stdout, " updated hwlevs is:\n");
	BENVi_PrintHWDesc(stdout, hwlevs);
#endif

	BENVi_CarthFree(carth_ptr);
    }

    return 0;
}

void printUsage(FILE *fp)
{
    fprintf(fp, "distdimstst --dims n1,n2,... --hw m1,m2,... --ranks r1,r2,...  --default --ndims nd --ranks r1,r2,... --allranks -v\n");
}
