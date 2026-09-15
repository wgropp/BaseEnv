/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 * Copyright (C) by University of Illinois 2022
 */
#include "benvconf.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "mpi.h"
#include "benvutil.h"
#include "topometrics.h"

/* This file contains routines to measure topology-related metrics, including
   how local communication is. Routines typically require some information
   about the process topology */

/* This routine takes two communicators and ranks in the first of processes
   being communicated with, and returns the counts of communication that
   are within the processes defined by the second communicator.
   This can be used, for example, to determine on-node vs. off-node, given
   a communicator of processes on the same node, but can also be used at
   other levels, such as NUMA or socket.

 */
/* FIXME: Add optional const int *weights, of nr values, so that communication
   cost can be weighted. If not provided, it is the same as if weights[i] == 1
*/
/*@ BENV_GetNonLocalCounts - Determine what communication is not local to a given communicator


Given a communicator, ranks within that communicator, and a 'local' communicator, return the number and ranks of communication within and not within that local communicator

  Input Parameters:
+ comm - Communicator of communicating processes
. nr - Number of communicating process (number of entries in 'ranks')
. ranks - Ranks of processes that the calling process communicates with; ranks are in 'comm'
- localcomm - Communicator of local processes. Output results are with respect to this communicator

  Output Parameters:
+ nlocal - Number of ranks in 'ranks' that refer to processes in 'localcomm'
. nnonlocal - Number of ranks in 'ranks' that refer to processes not in 'localcomm'
. localranks - If both 'localranks' and 'nonlocalranks' are non-null,
 returns an integer array of length 'nlocal' with the ranks from 'ranks'
 that correspond to processes in 'localcomm'
- nonlocalranks - If both 'localranks' and 'nonlocalranks' are non-null,
 returns an integer array of length 'nnonlocal' with the ranks from 'ranks'
 that correspond to processes not in 'localcomm'.

.N returnvalue

  Notes:
  This routine is local. If '*nlocal' is 0, 'localranks' will be NULL.
  If '*nnonlocal' is 0, 'nonlocalranks' will be NULL.
  @*/
int BENV_GetNonLocalCounts(MPI_Comm comm, int nr, const int ranks[],
			   MPI_Comm localcomm, int *nlocal, int *nnonlocal,
			   int **localranks, int **nonlocalranks)
{
    int *nranks;
    int on, off, i;
    MPI_Group cgroup, lgroup;

    /* Handle the case of no communication partners */
    if (nr == 0) {
	*nlocal    = 0;
	*nnonlocal = 0;
	if (localranks && nonlocalranks) {
	    *localranks    = 0;
	    *nonlocalranks = 0;
	}
	return 0;
    }

    nranks = (int *)malloc(nr * sizeof(int));
    if (!nranks) {
	BENVi_MallocErr("nranks", nr, "int");
    }
    MPI_Comm_group(comm, &cgroup);
    MPI_Comm_group(localcomm, &lgroup);
    MPI_Group_translate_ranks(cgroup, nr, ranks, lgroup, nranks);
    on  = 0;
    off = 0;
    for (i=0; i<nr; i++) {
	/* if the input rank is proc_null, there is no communication */
	if (ranks[i] == MPI_PROC_NULL) continue;
	/* if the output rank if proc_null or undefined, the target
	   process is not in the local comm */
	if (nranks[i] == MPI_PROC_NULL || nranks[i] == MPI_UNDEFINED) off++;
	else on ++;
    }

    /* If requested, partition the ranks into the local and nonlocal ranks.
       This allocates space as required, now that we know on and off */
    if (localranks && nonlocalranks) {
	int nlr, lr;
	if (on)
	    *localranks    = (int *)malloc(on * sizeof(int));
	else
	    *localranks    = 0;
	if (off)
	    *nonlocalranks = (int *)malloc(off * sizeof(int));
	else
	    *nonlocalranks = 0;
	nlr = 0;
	lr  = 0;
	for (i=0; i<nr; i++) {
	    if (ranks[i] == MPI_PROC_NULL) continue;
	    /* if the output rank if proc_null or undefined, the target
	       process is not in the local comm */
	    if (nranks[i] == MPI_PROC_NULL || nranks[i] == MPI_UNDEFINED) {
		(*nonlocalranks)[nlr++] = ranks[i];
	    }
	    else {
		(*localranks)[lr++] = ranks[i];
	    }
	}
    }

    free(nranks);
    MPI_Group_free(&cgroup);
    MPI_Group_free(&lgroup);

    *nlocal    = on;
    *nnonlocal = off;

    return MPI_SUCCESS;
}

/*@ BENV_PrintNonLocalCounts - Compute and print number of communications that
 are not within local communicators

Input Parameters:
+ fp - File pointer for output
+ comm - Communicator of communicating processes
. nr - Number of communicating process (number of entries in 'ranks')
. ranks - Ranks of processes that the calling process communicates with; ranks are in 'comm'
. nlocal - Number of local communicators.
- localcomms - Communicators of local processes. These should be nested in the sense that the processes in 'localcomms[i+1]' are also members of 'localcomms[i]'

.N returnvalue

Notes:
 Generates one line of output for each of 'localcomms'.

 Uses 'BENV_GetNonLocalCounts' to determine the local and nonlocal counts of
 communicating processes. Note that the terms local and nonlocal are used as
 this is the expected use of this routine. However, there is no requirement
 that any of the communicators are local or non local. However, there is the
 nesting requirement that the processes in 'localcomms[0]' are members of
 'comm' and that the processes in 'localcomms[i+1]' are members of
 'localcomms[i]' for i=0, ..., 'nlocal-1'.

 Each row of the output gives the min/max/avg number of communicating processes
 for these for cases (for the ith 'localcomm')\:
.vb
  local to        not local to  not local to      not local to
  localcomm[i]    localcomm[i]  localcomm[i]      localcomm[i]
                                but local to      and not local to
                                localcomm[i-1]    localcomm[i-1]
.ve
 This helps indicate just how non-local the communication is for communication
 not within the local communicator.

 This routine is collective over 'comm'.

See also:
 BENV_GetNonLocalCounts
  @*/
int BENV_PrintNonLocalCounts(FILE *fp, MPI_Comm comm,
			     int nr, const int ranks[],
			     int nlocal, MPI_Comm localcomms[])
{
    int i, crank;
    int *nl, *nnl, *npl, *npnl;

    nl = (int *)malloc(nlocal*4*sizeof(int));
    if (!nl) {
	BENVi_MallocErr("nl", nlocal*4, "int");
    }
    nnl  = nl + nlocal;
    npl  = nnl + nlocal;
    npnl = npl + nlocal;

    /* Get the counts for each level */
    for (i=0; i<nlocal; i++) {
	int      *localranks, *nlranks;
	MPI_Comm parent;
	/* Get local and nonlocal WRT localcommms[i]. Will use
	   nlranks to determine local/nonlocal WRT parent */
	BENV_GetNonLocalCounts(comm, nr, ranks,
			       localcomms[i], &nl[i], &nnl[i],
			       &localranks, &nlranks);

	/* Determine which ranks are local to the parent not not local
	   to the current localcomm */
	if (i == 0) parent = comm;
	else        parent = localcomms[i-1];
	BENV_GetNonLocalCounts(comm, nnl[i], nlranks,
			       parent, &npl[i], &npnl[i], 0, 0);
	if (localranks)
	    free(localranks);
	if (nlranks)
	    free(nlranks);
    }

    /* Query: any summary for comm - e.g., min/max/avg ranks. */
    MPI_Comm_rank(comm, &crank);

    /* Explanation of output */
    if (crank == 0) {
	fprintf(fp, "(min/max/avg) for local/nonlocal (wrt parent local/nonlocal)\n");
    }

    /* For each level, output min/max/average connections */
    for (i=0; i<nlocal; i++) {
	int    pranks[4], minvals[4], maxvals[4];
	double avgvals[4];
	char   label[MPI_MAX_OBJECT_NAME];
	int    resultlen;

	pranks[0] = nl[i];
	pranks[1] = nnl[i];
	pranks[2] = npl[i];
	pranks[3] = npnl[i];
	BENV_GetMinMaxAvg(comm, pranks, 4, minvals, maxvals, avgvals);
	if (crank == 0) {
	    MPI_Comm_get_name(localcomms[i], label, &resultlen);
	    if (resultlen <= 1) {
		snprintf(label, sizeof(label), "level %d", i);
	    }
	    fprintf(fp,
		    "\t%s\t%d/%d/%.2f\t%d/%d/%.2f\t%d/%d/%.2f\t%d/%d/%.2f\n",
		    label, minvals[0], maxvals[0], avgvals[0],
		    minvals[1], maxvals[1], avgvals[1],
		    minvals[2], maxvals[2], avgvals[2],
		    minvals[3], maxvals[3], avgvals[3]);
	}
    }

    free(nl);
    return MPI_SUCCESS;
}

/* Utility routines */

/*@
  BENV_GetMinMaxAvg - Determine the minimum, maximum, and average of
 integer values

Input Parameters:
+ comm - Communicator of processes
. val - Array of integer values
- nval - Number of values in 'val'

Output Parameters:
+ minval - The minimum value over all processes in 'comm' for each element of
 'val'
. maxval - The maximum value over all processes in 'comm' for each element of
 'val'
- avgval - The average value over all processes in 'comm', for each element of
 'val' and expressed as a double

.N returnvalue
  @*/
int BENV_GetMinMaxAvg(MPI_Comm comm, const int val[], int nval,
			  int minval[], int maxval[], double avgval[])
{
    int i, csize;
    int *sumval;

    sumval = (int *)malloc(nval*sizeof(int));
    if (!sumval)
	BENVi_MallocErr("sumval", nval, "int");
    MPI_Allreduce(val, minval, nval, MPI_INT, MPI_MIN, comm);
    MPI_Allreduce(val, maxval, nval, MPI_INT, MPI_MAX, comm);
    MPI_Allreduce(val, sumval, nval, MPI_INT, MPI_SUM, comm);
    MPI_Comm_size(comm, &csize);
    for (i=0; i<nval; i++) {
	avgval[i] = ((double)sumval[i]) / csize;
    }
    free(sumval);
    return MPI_SUCCESS;
}
