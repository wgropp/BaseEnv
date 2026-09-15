/*
 * This program tests the implementation of the cuda memory routines,
 * as part of the memObj interface
 */
#include "benvconf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define NO_MPI_INCLUDE
#include "benvutil.h"
#include "benvmem.h"
#include "benvdbg.h"

typedef struct {
    int verbose;     /* Provide more information about the operation of code */
    MemObj_type mtype; /* Memory type (e.g., malloc on CPU or GPU) */
} options_t;

#if 0
int dbg_wrank=0;
int verbose=0;
#endif
CDBGDECL(DEVMEM);

int getOptions(int argc, char **argv, options_t *options);
void printUsage(void);

int main(int argc, char **argv)
{
    MemObj_t *mo;
    size_t memlen, memlenbytes;
    double value;
    int rc, offset;
    options_t options;
    static int mlens[] = { 1024, 4096, 4097, -1}; /* -1 is no more values */
    /* loctest gives indices with the memory. Values of -1 and -2
       are relative to the memlen. -3 means no more values */
    static int loctest[] = { 0, 200, 2048, -1, -2, -3};

    /* Process command line */
    getOptions(argc, argv, &options);

    /* Make sure to allocate more elements than there are threads in a
       single block. Also allocate sizes that are and are not multiples
       of two */
    for (int i=0; mlens[i] > 0; i++) {
	memlen = mlens[i];
	memlenbytes = memlen*sizeof(double);

	CDBG(DEVMEM,BASIC,"About to init device");
	/* Any device initialization for the given memory type */
	BENV_MemDeviceInit(options.mtype);

	CDBGV(DEVMEM,BASIC,"About to alloc memory of %ld bytes\n",(long)memlenbytes);
	mo = BENV_MemAlloc(memlenbytes, options.mtype);

	/* First, check that an init and check gives the expected value */
	CDBG(DEVMEM,BASIC,"About to init memory on device (1D)");
	BENV_MemInitValue1D(mo, memlen, 200, 0, memlen, 0.0, 1.0);

	CDBG(DEVMEM,BASIC,"About to check values on device");
	rc = BENV_MemCheckValue1D(mo, memlen, 200, 0, memlen, 0.0, 1.0);
	if (rc != 0) {
	    fprintf(stderr, "1st check value failed with rc = %d\n", rc);
	}
	CDBG(DEVMEM,BASIC,"About to copy to host and check locally");
	/* Make a copy and check locally */
	rc = BENV_MemCheckValue1DWithHost(mo, memlen, 200, 0, memlen, 0.0, 1.0);
	if (rc != 0) {
	    fprintf(stderr, "2nd check value failed with rc = %d\n", rc);
	}

	/* Now, update the values on the device, and verify that the checks
	   identify the update */
	for (int j=0; loctest[j] != -3; j++) {
	    CDBG(DEVMEM,BASIC,"About to set one element in memory on device");
	    value = -100.0;
	    if (loctest[j] > 0)
		offset = loctest[j];
	    else
		offset = memlen + loctest[j];
	    if (offset >= memlen) continue;
	    mo->memtodev((double *)(mo->memptr) + offset, &value, sizeof(double));

	    CDBG(DEVMEM,BASIC,"About to check updated values on device");
	    rc = BENV_MemCheckValue1D(mo, memlen, 200, 0, memlen, 0.0, 1.0);
	    if (rc != offset + 1) {
		fprintf(stderr, "1st error check failed, expected %d, got %d\n",
			offset + 1, rc);
	    }

	    /* Make a copy and check locally */
	    CDBG(DEVMEM,BASIC,"About to check value by copying to host");
	    rc = BENV_MemCheckValue1DWithHost(mo, memlen, 200, 0, memlen, 0.0, 1.0);
	    if (rc != offset + 1) {
		fprintf(stderr, "2nd error check failed, expected %d, got %d\n",
			offset + 1, rc);
	    }

	    /* Restore old value */
	    value = 200 + offset;
	    mo->memtodev((double *)(mo->memptr) + offset, &value, sizeof(double));
	}

	/* ToDo: Add 2D and 3D init/check */

	/* Clean up */
	BENV_MemFree(mo);
    }

    /* Other tests: test lstart != 0; test 3d versions */
    fprintf(stdout, "memcudatest completed\n");

    return 0;
}

int getOptions(int argc, char **argv, options_t *options)
{
    /* Set defaults */
    options->verbose       = 0;
    options->mtype         = MEMOBJ_MALLOC;

    for (int i=1; i<argc; i++) {
	int rc;
	/* Look for common options */
	rc = BENV_MemArg(argc, argv, &i, &options->mtype);
	if (rc == -1) {
	    fprintf(stderr, "error in memobj options\n");
	    fflush(stderr);
	    return 1;
	}
	if (rc) continue;

	if      (strcmp(argv[i], "-v") == 0)  {
	    /* This allows multiple -v options to set higher levels of
	       debugging */
	    options->verbose++;
	}
	else if (strcmp(argv[i], "-help") == 0 ||
		 strcmp(argv[i], "-usage") == 0) {
	    printUsage();
	    return 1;
	}
	else {
	    fprintf(stderr, "Unrecognized option %s\n", argv[i]);
	    printUsage();
	    fflush(stderr);
	    return 1;
	}
    }

    /* Set debug levels here and in the mem code */
    CDBGSETVAL(DEVMEM,options->verbose);
    if (options->verbose > 0) BENV_MemDebug(options->verbose, 0);

    return 0;
}

void printUsage(void)
{
    fprintf(stderr, "\
memcudatest - Test the device memory routines\n");
    BENV_MemPrintUsage(stderr);
    fprintf(stderr, "\
 -v - Set verbose output\n\
 -help or -usage - Print this information\n");
}
