/*
 * Copyright (C) by University of Illinois 2022
 */
#include "benvconf.h"
#include <stdlib.h>
#include <stdio.h>
#include "mpi.h"
#include "benvmpiutil.h"
//#define DO_DEBUG 1
#include "benvdbg.h"

/*@
  BENV_CheckSameInts - Check that all processes have the same values for an array of ints

Input Parameters:
+ comm - Communicator of processes. Call is collective over 'comm'
. arr  - Array of integers of length 'n'
- n - Length of 'arr'

Return values:
+ 0 - Values are the same; i.e., 'arr[i]' has the same value on all processes.
. >0 - Values are different for 'arr[return_value-1]'
- -1 - Internal error, likely unable to allocate temporary memory
  @*/
int BENV_CheckSameInts(MPI_Comm comm, const int *arr, int n)
{
    int *minvals, *maxvals, i, rc=0;
    minvals = (int *)malloc(2*n*sizeof(int));
    if (!minvals) return -1;
    maxvals = minvals + n;

    MPI_Allreduce(arr, minvals, n, MPI_INT, MPI_MIN, comm);
    MPI_Allreduce(arr, maxvals, n, MPI_INT, MPI_MAX, comm);
    for (i=0; i<n; i++) {
	if (minvals[i] != maxvals[i]) { rc = i+1; break; }
    }
    free(minvals);
    return rc;
}

int BENV_CheckNonNull(MPI_Comm comm, const void **arr, int n)
{
    int *nonnull, i, rc=0;

    nonnull = (int *)malloc(n*sizeof(int));
    if (!nonnull) return -1;
    for (i=0; i<n; i++)
	nonnull[i] = (arr[i] != 0);

    MPI_Allreduce(MPI_IN_PLACE, nonnull, n, MPI_INT, MPI_MIN, comm);
    for (i=0; i<n; i++) {
	if (nonnull[i] == 0) { rc = i+1; break; }
    }
    free(nonnull);
    return rc;
}
