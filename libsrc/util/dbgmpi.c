/*
 * Support routines for the debug macros in include/benvdbg.h
 * These are for options that only make sense when MPI is available
 *
 * Unified setting of debug flags from the command line or the
 * environment variables
 *
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include "mpi.h"
#include "benvdbg.h"
#include "benvmpiutil.h"

/* Used to indicate whether to update cvar_benv_wrank */
static int isInitialized=0;

/*@ BENV_DebugArgRank - Specify the rank of the process for debug output

Input Parameters:
+ argc - Argument count
- argv - Argument vector

Input/output Parameters:
. argcnt - pointer to the index of the current argument. Will be updated
 if an hwdesc parameter is found by the number of values read, not counting
 the argument itself.

Notes:
This provides common processing for arguments of the form '-debugrank n',
where 'n' is the rank in 'MPI_COMM_WORLD' that designates which process
should generate debug output.

See also:
BENV_DebugArg, BENV_DebugArgClass
  @*/
int BENV_DebugArgRank(int argc, char **argv, int *argcnt)
{
    int i = *argcnt;
    int rc = 0;

    if (strcmp(argv[i], "-debugrank") == 0) {
	i++;
	if (i < argc) {
	    int mpiinit;
	    cvar_benv_rank = atoi(argv[i]);
	    /* Need to check whether MPI is initialized. Set now if so
	       to allow control of which processes output in the event
	       that MPI has been initialized */
	    MPI_Initialized(&mpiinit);
	    if (!isInitialized && mpiinit) {
		isInitialized = 1;
		MPI_Comm_rank(MPI_COMM_WORLD, &cvar_benv_wrank);
	    }
	    rc = 1;
	}
	else {
	    return -1;
	}
    }

    if (rc == 1) *argcnt = i;
    return rc;
}

/*@
  BENV_DebugPostMPIInit - Perform any initialization for the debug options

Notes:

  @*/
int BENV_DebugPostMPIInit(void)
{
    if (!isInitialized) {
	MPI_Comm_rank(MPI_COMM_WORLD, &cvar_benv_wrank);
	isInitialized = 1;
    }
    return 0;
}
