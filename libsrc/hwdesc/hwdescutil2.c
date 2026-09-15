/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 * Copyright (C) by University of Illinois 2026
 */
#include "benvconf.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include "mpi.h"
#include "benvutil.h"
#include "benvmpiutil.h"
#include "hwdescnew.h"
#include "mstring.h"
#include "benvdbg.h"
#include "hwdescimpl2.h"

CDBGGDECL(GETDESC);
CDBGGDECL(PRINTHW);
CDBGEDECL(COMM);
CDBGEDECL(MEM);
CDBGFCALLDECL;

/* Default mask is to allow all flags */
int BENVi_HwdescFlagMask=-1;

static void hwdescMPIInit(hwdescMPIInfo *mpiinfo);
static void hwdescObjInit(hwdescObjInfo *objinfo);
static void settopobject(MPI_Comm comm, hwdescObjInfo *objinfo,
			 hwdescMPIInfo *collinfo, hwdescConfigSrc csrc,
			 hwdescAssignSrc asrc);

/* Create and free hwdesc structures */

/*@ BENV_HwdescCreateCtx - Create an hwdescCtx structure

Input Parameter:
. nlevels - the number of levels to allocate for the hardware levels

Notes:
This creates an empty structure - there is no hardware information included.
That needs to be added by other routines, such as 'BENV_HwdescGetDescGeneral'.
'nlevels' provides a maximum number of levels. The actual number of levels
will be set by the routines that fill in the details.

Return Value:
Pointer to an 'hwdescCtx'. Null on failure.
@*/
hwdescCtx *BENV_HwdescCreateCtx(int nlevels)
{
    hwdescCtx *nctx;
    int i;

    CDBGFCALLENTER;
    nctx = (hwdescCtx *)malloc(sizeof(hwdescCtx));
    if (!nctx) {
	BENVi_MallocErr("HwdescCreateCtx", 1, "hwdescCtx");
	return 0;
    }

    nctx->source = 0;
    nctx->nlevel = 0;
    nctx->nAllocated = nlevels;
    nctx->objinfo = (hwdescObjInfo *)malloc(nlevels*sizeof(hwdescObjInfo));
    if (!nctx->objinfo) {
	BENVi_MallocErr("HwdescCreateCtx", nlevels, "hwdescObjInfo");
	free(nctx);
	return 0;
    }
    // FIXME: collinfo can be separate, allowing non-MPI structures
    //nctx->collinfo = 0;
    nctx->collinfo = (hwdescMPIInfo *)malloc(nlevels*sizeof(hwdescMPIInfo));
    if (!nctx->collinfo) {
	BENVi_MallocErr("HwdescCreateCtx", nlevels, "hwdescMPIInfo");
	free(nctx->objinfo);
	free(nctx);
	return 0;
    }

    // Initialize all elements
    for (i=0; i<nlevels; i++) {
	hwdescMPIInit(&nctx->collinfo[i]);
#if 0
	nctx->collinfo[i].objcomm = MPI_COMM_NULL;
	nctx->collinfo[i].leadersInParent = 0;
	nctx->collinfo[i].descstr = 0;
	nctx->collinfo[i].nSiblings = 0;
	nctx->collinfo[i].allsizeone = 0;
	nctx->collinfo[i].siblingNum = -1;
#endif

	hwdescObjInit(&nctx->objinfo[i]);
    }

    CDBGFCALLEXIT;
    return nctx;
}

/*@ BENV_HwdescFreeCtx - Frees an hwdesc context

Input Parameter:
. hwc - Hwdesc context

.N returnvalue
  @*/
int BENV_HwdescFreeCtx(hwdescCtx *hwc)
{
    CDBGFCALLENTER;
    CDBGV(MEM,DETAIL,"Enter FreeCtx with hwc=%p\n", hwc);
    if (!hwc->objinfo) {
	fprintf(stderr, "hwc->hw is null!!\n"); fflush(stderr);
    }
    else {
	// Need to free any information (const char source?)
	CDBGV(MEM,DETAIL,"about to free objinfo %p\n", hwc->objinfo);
	free(hwc->objinfo);
    }
    if (hwc->collinfo) {
	int i;
	// Need to free any information in the collinfo (use a free routine?)
	for (i=0; i<hwc->nlevel; i++) {
	    if (hwc->collinfo[i].objcomm != MPI_COMM_NULL &&
		hwc->collinfo[i].objcomm != MPI_COMM_WORLD) {
		MPI_Comm_free(&hwc->collinfo[i].objcomm);
	    }
	}
	CDBGV(MEM,DETAIL,"about to free collinfo %p\n", hwc->collinfo);
	free(hwc->collinfo);
    }
    if (hwc->source) {
	CDBGV(MEM,DETAIL,"about to free source %p\n", hwc->source);
	free((char *)hwc->source);
    }
    CDBGV(MEM,DETAIL,"about to free hwc %p\n", hwc);
    free(hwc);

    CDBG(MEM,DETAIL,"Done freeing hwdesc context");
    CDBGFCALLEXIT;
    return 0;
}

/* Routines to update an hwdescCtx with configuration and assignment (if
   available) */

/* Query: Move the routines requiring MPI to a separate file */

/*@
BENV_HwdescGetDescFromMPI - Using MPI, get information on the hardware hierarchy

Input parameters:
. comm - Communicator of processes

Input/Output Parameters:
. hwc - Pointer to hwdescCtx describing the hardware hierarchy

.N returnvalue

Notes:
While this routine returns only the local information for the calling rank,
it is a collective call over 'comm', and it includes information about
processes that are part of the same level of hierarchy, such as processes
with part of the same node.

This routine takes advantage of MPI Version of 4 or greater. For earlier
versions of MPI, a simplified hardware heirarchy is returned.

Routines to determine hardware description and process/thread assignment are
available and can be used to update an 'hwdescCtx', particularly with
information about the node, such as sockets, NUMA regions, and cores.
Depending on the MPI implementation, that information might not be included
in the output from this routine.
  @*/
int BENV_HwdescGetDescFromMPI(MPI_Comm comm, hwdescCtx *hwc)
{
    int hwlevel = 0;
    hwdescMPIInfo *mpiinfo;
    hwdescObjInfo *objinfo;

    CDBGFCALLENTER;
    CDBGV(GETDESC,BASIC,"Start HwdescGetDescFromMPI with comm %ld\n",
	  (long)comm);

    // Confirm available space
    if (hwc->nAllocated <= 2) {
    }

    if (hwc->nlevel != 0) {
	// Expect an empty context
    }

    mpiinfo = hwc->collinfo;
    if (mpiinfo == 0) {
	// Error and return?
    }

    objinfo = hwc->objinfo;
    if (objinfo == 0) {
	// Error and return?
    }

    // Set the top entry as comm
    hwdescMPIInit(&mpiinfo[0]);
    hwdescObjInit(&objinfo[0]);
    settopobject(comm, objinfo, mpiinfo, BENV_HWDESC_CONFIG_MPI4,
		 BENV_HWDESC_ASSIGN_MPI4);

    /* Extract the hw_unguided comms */
    while (mpiinfo[hwlevel].objcomm != MPI_COMM_NULL) {
	int      lrank;

	CDBGV(GETDESC,DETAIL,"While hw[%d]...\n", hwlevel);
	/* Get the number of distinct communicators at this level. This is
	   the number of processes that have rank 0 in a communicator at this
	   level.
	   Also gather the ranks in the parent communicator of the leaders
	   (processes with rank 0 in a communicator at this level)
	*/
	MPI_Comm_rank(mpiinfo[hwlevel].objcomm, &lrank);
	if (hwlevel == 0) {
	    /* Assume the input comm is for an entire job */
	    /* Note that the hwlevel==0 entries initialized above */
	    objinfo[0].kind = BENV_HWDESC_JOB;
	}
	else {
	    BENVi_FindLeadersInSplit(mpiinfo[hwlevel-1].objcomm,
				     mpiinfo[hwlevel].objcomm,
				     &objinfo[hwlevel],
				     &mpiinfo[hwlevel]);
	    CDBGV(GETDESC,DETAIL,"leaders for level %d\n", hwlevel);
	}

	CDBGV(GETDESC,BASIC,"Running split comm at level %d\n", hwlevel);
	/* This split can fail to produce a new communicator. */
	MPIX_Comm_split_unguided(mpiinfo[hwlevel].objcomm, lrank,
				 &mpiinfo[hwlevel+1].descstr,
				 &mpiinfo[hwlevel+1].objcomm);
	if (mpiinfo[hwlevel+1].objcomm == MPI_COMM_NULL) {
	    CDBG(GETDESC,BASIC,"Split returned MPI_COMM_NULL");
	}
	hwlevel ++;

	if (hwlevel >= hwc->nAllocated) {
	    BENV_ERRMSGV("Too many levels in hardware description");
	    return 1;
	}
    }
    hwc->nlevel = hwlevel;
    /* MPIX_Comm_split_unguided may use different methods. */
    if (hwlevel >= 1 && MPIX_Comm_split_method(mpiinfo[1].objcomm)) {
	/* This is only defined if split_unguided succeeded */
	hwc->source = strdup(MPIX_Comm_split_method(mpiinfo[1].objcomm));
    }
    else {
	hwc->source = strdup("From MPI (but no split possible)");
    }

    CDBGV(GETDESC,BASIC,"hwlevel = %d\n", hwlevel);
    CDBG(GETDESC,BASIC,"Exiting HwdescGetDescFromMPI");
    CDBGFCALLEXIT;
    return 0;
}

/*@
  BENV_HwdescGetDescFromShared - Create a description of the node hardware by
  using information on shared memory from MPI

Input Parameters:
. comm - Communicator of processes

Input/Output Parameters:
. hwc - Pointer to hwdescCtx describing the hardware hierarchy

.N returnvalue

Notes:
 This routine uses the 'MPI_COMM_TYPE_SHARED' split type to determine a two-level hierarchy of processes, with 'comm' as the top level and the communicator from 'MPI_Comm_split_type' with that type providing the second level.
  @*/
int BENV_HwdescGetDescFromShared(MPI_Comm comm, hwdescCtx *hwc)
{
    hwdescMPIInfo *mpiinfo;
    hwdescObjInfo *objinfo;

    CDBGFCALLENTER;
    // Confirm available space
    if (hwc->nAllocated <= 2) {
    }

    if (hwc->nlevel != 0) {
	// Expect an empty context
    }

    mpiinfo = hwc->collinfo;
    if (mpiinfo == 0) {
	// Error and return?
    }
    objinfo = hwc->objinfo;
    if (objinfo == 0) {
	// Error and return?
    }

    hwdescMPIInit(&mpiinfo[0]);
    hwdescObjInit(&objinfo[0]);
    // Set the top entry as comm
    settopobject(comm, objinfo, mpiinfo, BENV_HWDESC_CONFIG_MPI_SHARED,
	BENV_HWDESC_ASSIGN_MPI_SHARED);
    /* Try to get nodes based on type_shared */
    MPI_Comm_split_type(comm, MPI_COMM_TYPE_SHARED, 0,
			MPI_INFO_NULL, &mpiinfo[1].objcomm);
    /* We use node here because this is a common result and this matches
       expectations for other codes */
    mpiinfo[1].descstr = strdup("node");

    BENVi_FindLeadersInSplit(comm, mpiinfo[1].objcomm,
			     &objinfo[1], &mpiinfo[1]);
    hwc->nlevel = 2;
    hwc->source = strdup("Split on COMM_TYPE_SHARED");

    CDBGFCALLEXIT;
    return 0;
}

/*
 * Given the rank in COMM_WORLD and the scheduler's assignment strategy,
 * return a HW description.
 *
 * Known strategies:
 * B (Block): allocate in groups of consecutive ranks
 * C (Cyclic): allocate in a round-robin fashion across elements
 * C(n) (Cyclic with blocks): Like (C), but with blocks of size n.
 *       Cyclic is the same as C(1).  Block is the same as C(tot/nobj)
 *       where tot is the number of processes and nobj is the number
 *       of objects.
 *
 * These apply to nodes and to sockets. NUMA domains could be added if
 * needed.
 *
 * policy typically from getenv("SLURM_ASSIGNMENT_POLICY") (which needs
 * to be set by the user.
 */

/*@ BENV_HwdescGetDescFromPolicy - Create an hwdescCtx from a known
  scheduler policy and known rank in a communicator

Input Parameters:
+ comm - Communicator of processes. Typically 'MPI_COMM_WORLD'.
. policy - String with scheduler policy. See notes for valid policies.
. np - Number of processes
. rank - Rank (in 'MPI_COMM_WORLD') of this process
. nnobj - Number of defined objects - e.g., 'nobjs[0]' to 'nobjs[nnobj-1]'
  are valid. Must be less than 4.
- nobjs - There are 'nobjs[0]' nodes, 'nobjs[1]' sockets on each node,
 'nobjs[2]' NUMA regions in each socket, and 'nobjs[3]' cores.

Input/Output Parameters:
. hwc - Pointer to hwdescCtx describing the hardware hierarchy

.N returnvalue

Notes:
This routine provides an alternative way to create an 'hwdesc' description
of the hardware on which a parallel application is running. For example, an
MPI implementation may not provide MPI 4 features that provide this information,
or may not provide the level of detail needed. Applications have typically made
assumptions about the scheduler policy in assigning processes to nodes,
sockets, and cores, and this routine simply makes it easier to work with
node-aware and other approaches even when hardware information is not avaiable.

If there is a single socket or a single NUMA region, provide '1' in the
relevant entry of 'nobjs[]'.
  @*/
int BENV_HwdescGetDescFromPolicy(MPI_Comm comm, const char *policy,
				 int np, int rank, int nnobj,
				 const int nobjs[], hwdescCtx *hwc)
{
//    static const char *hwdescstr[4] = { "Node", "Socket", "NUMA", "Core" };
    static const hwdescKind kinds[4] = {BENV_HWDESC_NODE, BENV_HWDESC_SOCKET,
					BENV_HWDESC_NUMA, BENV_HWDESC_CORE };
    int           loc, nl, defblock;
    hwdescMPIInfo *mpiinfo;
    hwdescObjInfo *objinfo;

    CDBGFCALLENTERV("policy=%s, np=%d, rank=%d, nnobj=%d, nobjs[0]=%d\n",
                    policy, np, rank, nnobj, nobjs[0]);
    /* Check that we have a policy to examine */
    if (!policy) return 1;

    CDBG(GETDESC,BASIC,"Starting GetDescFromPolicy");

    // Confirm available space
    if (hwc->nAllocated <= 2) {
    }

    if (hwc->nlevel != 0) {
	// Expect an empty context
        fprintf(stderr, "FromPolicy hwc->nlevel=%d\n", hwc->nlevel);
    }

    mpiinfo = hwc->collinfo;
    if (mpiinfo == 0) {
	// Error and return?
    }
    objinfo = hwc->objinfo;
    if (objinfo == 0) {
	// Error and return?
    }

    hwdescMPIInit(&mpiinfo[0]);
    hwdescObjInit(&objinfo[0]);

    // Set the top entry as comm
    settopobject(comm, objinfo, mpiinfo, BENV_HWDESC_CONFIG_GIVEN,
	BENV_HWDESC_ASSIGN_POLICY);

    loc = 0;
    for (nl=0; nl<nnobj && policy[loc]; nl++) {
	int bs, rc, objidx, rankinobj, prevloc;

	/* Set the default block size based on the number of available
	   processes and objects at this level */
	if (nobjs[nl] <= 0) {
	    fprintf(stderr, "nobjs[%d] = %d! Must be > 0\n", nl, nobjs[nl]);
	    return 1;
	}
	defblock = np / nobjs[nl];
	/* if np < nobjs[nl], set defblock to 1 */
	if (np < nobjs[nl]) defblock = 1;
	if (np == 0 || defblock == 0) {
	    fprintf(stderr, "Warning: defblock=%d (np=%d) at nl=%d\n",
		    defblock, np, nl);
	    fflush(stderr);
	}
	CDBGV(GETDESC,DETAIL,"nl=%d, policy[%d] = %s, defblock=%d\n",
	      nl, loc, &policy[loc], defblock);
	prevloc = loc;
	bs = BENVi_GetBlocksizeFromString(policy, &loc, defblock);
	if (bs <= 0) {
	    fprintf(stderr, "nl=%d, defblock =%d\n", nl, defblock);
	    fprintf(stderr, "breaking from GetBlocksize policy[%d] = %s = %d\n",
		    prevloc, &policy[prevloc], bs);
	    fflush(stderr);
	    break;
	}
	/* Skip : in policy string */
	if (policy[loc] == ':') loc++;
	/* If at end of policy string, don't advance loc */
	if (policy[loc] == 0) loc = prevloc;
	CDBGV(GETDESC,DETAIL,"Distribute %d process across %d objects with blocksize %d\n",
	      np, nobjs[nl], bs);
	rc = BENVi_DistribRankByPolicy(np, rank, nobjs[nl], bs,
				       &objidx, &rankinobj, &np);
	if (rc != 0) {
	    fprintf(stderr, "breaking from DistribRankByPolicy\n");
	    break;
	}
	hwdescMPIInit(&mpiinfo[nl+1]); //???
	hwdescObjInit(&objinfo[nl+1]);
	MPI_Comm_split(mpiinfo[nl].objcomm, objidx, rankinobj, &mpiinfo[nl+1].objcomm);
//	mpiinfo[nl+1].nSiblings       = nobjs[nl];
//	mpiinfo[nl+1].siblingNum      = objidx;
	mpiinfo[nl+1].leadersInParent = 0;
	mpiinfo[nl+1].allsizeone      = 0;
	objinfo[nl+1].kind            = kinds[nl];
	objinfo[nl+1].nobj            = nobjs[nl];
	objinfo[nl+1].objidx          = objidx;
	objinfo[nl+1].csrc            = BENV_HWDESC_CONFIG_GIVEN;
	objinfo[nl+1].asrc            = BENV_HWDESC_ASSIGN_POLICY;

	BENVi_FindLeadersInSplit(mpiinfo[nl].objcomm, mpiinfo[nl+1].objcomm,
				 &objinfo[nl+1], &mpiinfo[nl+1]);
	/* Update rank */
	rank = rankinobj;
    }

    hwc->nlevel = nl+1;
    hwc->source = BENV_StringCat2("Scheduler policy: ", policy);

    CDBG(GETDESC,BASIC,"Returning from GetDescFromPolicy");

    CDBGFCALLEXIT;
    return 0;
}

/*@
  BENV_HwdescGetDescFromDebug - Create an hwdescCtx for debugging

Input Parameters:
+ comm - Communicator of processes
. nnobj - The number of valid elements of 'nobjs'
. nobjs - There are 'nobjs[0]' nodes, 'nobjs[1]' sockets, and 'nobjs[2]' NUMA
 regions on each socket ('nnobj' indicates how many values are provided)
- policy - Scheduling policy. May be null, in which case BBBB is used

Input/Output Parameter:
. hwc - Pointer to 'hwdescCtx' describing the hardware hierarchy

.N returnvalue

Notes:
This uses values set for several CVARs, as well as values in 'nobjs', to
create an 'hwdescCtx'.

This is essentially a convienence routine for 'BENV_HwdescGetDescFromPolicy',
with the feature that it sets the 'source' field to indicate 'Debug using'
the hwdesc from policy.
  @*/
int BENV_HwdescGetDescFromDebug(MPI_Comm comm, int nnobj, const int nobjs[],
				const char *policy, hwdescCtx *hwc)
{
    int np, rank, rc;
    const char *oldsource;

    CDBGFCALLENTER;
    CDBG(GETDESC,BASIC,"Starting GetDescFromDebug");
    MPI_Comm_size(comm, &np);
    MPI_Comm_rank(comm, &rank);
    if (!policy) policy = "BBBB";
    rc = BENV_HwdescGetDescFromPolicy(comm, policy, np, rank, nnobj, nobjs,
				      hwc);
    /* Reset source */
    oldsource = hwc->source;
    hwc->source = BENV_StringCat2("Debug using ", oldsource);
    if (oldsource) free((void *)oldsource);

    CDBG(GETDESC,BASIC,"Ending GetDescFromDebug");
    CDBGFCALLEXIT;
    return rc;
}



/*@ BENV_HwdescGetDescGeneral - Create an hwdescCtx using a selected method

Input parameters:
+ comm - Communicator of processes
. flags - Flags that control which methods may be used to determine the
 output hardware description
- parms - 'hwdescParms' structure with optional values that some methods
 require

Output Parameters:
. hwc - Pointer to hwdescCtx describing the hardware hierarchy

.N returnvalue

Notes:
This is a convenience routine that provides a single interface to
different methods for creating an 'hwdescCtx' hardware context.
This routine is collective over 'MPI_COMM_WORLD'.

These values from the 'parms' argument are used.
+ policy - Used only for 'BENV_HWDESC_USE_POLICY' to provide the policy string.
 This is often determined from an enviroment variable.
- nobjs - Used to communicate some elements of the hardware that are needed
 to determine a decomposition when other methods are not available. The elements
 are

.n nobjs[0]: Number of nodes
.n nobjs[1]: Number of sockets on a node
.n nobjs[2]: Number of numa domains per socket
.n

The flag 'BENV_HWDESC_USE_DEBUG' is a special case. If this is provided,
then there is an additional check made. First, if the value of flag is
exactly 'BENV_HWDESC_USE_DEBUG', then the debug method is used, using
information passed in 'parms'. Otherwise, the environment variable
'BENV_HWDESC_GET_DEBUG' is checked. Only if that value is 'true' or 'yes'
(independent of letter case) is the debug method used. This choice is made
to enable debug testing that is turned on only when the environment variable
is set.

See also:
BENV_DebugFromEnv, BENV_HwdescGetDescFromDebug
  @*/
int BENV_HwdescGetDescGeneral(MPI_Comm comm, int flags, hwdescParms *parms,
			      hwdescCtx **hwc)
{
    hwdescCtx *hwcnew=0;
    int nodelevel, isexact, rc=0;

    CDBGFCALLENTER;
    CDBGV(GETDESC,BASIC,"Starting GetDescGeneral with flags %d\n", flags);

    /* Requires non-null parms */
    if (!parms) {
	fprintf(stderr, "HwdescGetDescGeneral: parms is null!\n");
	return -1;
    }

    /* Mask the flags */
    flags &= BENVi_HwdescFlagMask;

    /* Allocate an hwdescCtx */
    hwcnew = BENV_HwdescCreateCtx(8);

    CDBGV(GETDESC,ALL,"About to check policy %s\n",
	  parms->policy ? parms->policy : "No policy");
    if (flags & BENV_HWDESC_USE_DEBUG) {
	int hasval = 0;
	/* Special case. Only use debug if flags == DEBUG or
	   environment variable says use debug. This allows for
	   debug to be an option that is normally skipped, but can
	   be enabled by setting the environment variable */
	CDBG(GETDESC,DETAIL,"Checking for debug...");
	if (flags != BENV_HWDESC_USE_DEBUG) {
	    /* Check for BENV_HWDESC_GET_DEBUG */
	    hasval = 0;
	    BENV_DebugFromEnv("HWDESC_GET", 0, &hasval);
	    //printf("debug value was %d\n", hasval);
	    if (parms->forcedebug)
		hasval = 1;
	}
	else {
	    /* flags == BENV_HWDESC_USE_DEBUG */
	    hasval = 1;
	}
	if (hasval) {
	    CDBG(GETDESC,DETAIL,"Using GetDescFromDebug");
	    CDBGV(GETDESC,DETAIL,"nnobj=%d, nobj[0]=%d, nobj[1]=%d\n",
		  parms->nnobj, parms->nobjs[0], parms->nobjs[1]);
	    rc = BENV_HwdescGetDescFromDebug(comm, parms->nnobj, parms->nobjs,
					     parms->policy, hwcnew);
	}
    }
    if (hwcnew->nlevel == 0 && parms->policy &&
	(flags & BENV_HWDESC_USE_POLICY)) {
	int np, rank;
	MPI_Comm_size(comm, &np);
	MPI_Comm_rank(comm, &rank);
	if (parms->nobjs[1] < 1) parms->nobjs[1] = 1; /* TEMP FOR DEBUGGING */
	CDBGV(GETDESC,BASIC,"Check for hw by policy %s with objs (%d,%d)\n",
	      parms->policy, parms->nobjs[0], parms->nobjs[1]);
	rc = BENV_HwdescGetDescFromPolicy(comm, parms->policy, np, rank,
					  parms->nnobj, parms->nobjs, hwcnew);
    }
    if (hwcnew->nlevel == 0 && (flags & BENV_HWDESC_USE_MPI4)) {
	CDBG(GETDESC,BASIC,"Try to get hw from MPI");
	rc = BENV_HwdescGetDescFromMPI(comm, hwcnew);
    }
    if (hwcnew->nlevel == 0 && (flags & BENV_HWDESC_USE_NODENAME)) {
	/* Unimplemented */
	rc = -1;
    }
    if (hwcnew->nlevel == 0) {
	/* Query: What to do here? */
	rc = -1;
    }

    /* Now, see if we have node information. If not, try to add that, again,
       controlled by any flags */
    BENV_HwdescFindObject(hwcnew, BENV_HWDESC_NODE, &nodelevel, &isexact);
    if (hwcnew->nlevel == nodelevel+1) {
	CDBG(GETDESC,BASIC,"Using GetNodeDesc to supplement hwc");
	/* The node (or closest match) is the last level */
	rc = BENV_HwdescGetNodeDesc(hwcnew, parms, flags);
	if (rc == 0)
	    rc = BENV_HwdescNodeSetCollinfo(comm, hwcnew);
    }

    CDBG(GETDESC,BASIC,"Return from GetDescGeneral");
    *hwc = hwcnew;
    CDBGFCALLEXIT;
    return rc;
}

/* See src/hwinfo/hwinfo.c for a use of this routine */
/*@
 BENV_HwdescGetCoordTuple - Given an hwdescCtx hardware description, return a coordinate tuple for this process.

Input Parameters:
+ comm - Communicator of processes. Should be the same as used to create hw
. hwc - An hwdescCtx for the process
- maxlevel - size of coords and sizes; the maximum number of elements
 in the output tuples coords and sizes

Output Parameters:
+ coords - Coordinate of this process at each level
. sizes  - Size of this level (e.g., coordinates range from 0 to
- nlevels - Number of levels, i.e., number of elements in the tuple
 size-1)

.N returnvalue

Notes:
 This routine is collective over 'comm'.

 The coordinates are drawn from the 'collinfo' - that is, all numbers and
 coordinates with respect to the MPI processes. For example, if there are 64
 cores but only 8 processes on a chip, then the sizes at the core layer will
 be 8, not 64. This choice is made because the typical use is expected to be
 relative to the activee processes.

 OPTION: could optionally provide pointers to names at each level.

 The most likely use is with 'comm == hw->collinfo[0].objcomm == MPI_COMM_WORLD'.
  @*/
int BENV_HwdescGetCoordTuple(MPI_Comm comm, hwdescCtx *hwc, int maxlevel,
			     int coords[], int sizes[], int *nlevels)
{
    int i, lsize, is, nl;
    hwdescMPIInfo *mpiinfo = hwc->collinfo;

    CDBGFCALLENTER;
    BENV_HwdescFindCommonLevel(comm, hwc, maxlevel, nlevels);

    /* Skip the top level if there is only a single communicator at this
       level */
    is = 0;
    nl = *nlevels;
    if (mpiinfo[0].nSiblings == 1) {
	is = 1;
	nl = nl - 1;
    }
    for (i=0; i<nl; i++) {
	coords[i] = mpiinfo[i+is].siblingNum;
	sizes[i]  = mpiinfo[i+is].nSiblings;
    }
    /* if the bottom communicator has size > 1, add another coordinate */
    MPI_Comm_size(mpiinfo[nl-1+is].objcomm, &lsize);
    if (lsize > 1 && nl < maxlevel) {
	sizes[nl] = lsize;
	MPI_Comm_rank(mpiinfo[nl-1+is].objcomm, &coords[nl]);
	nl++;
    }
    *nlevels = nl;

    /* Another options is to also return a char * array of the descriptions */
    CDBGFCALLEXIT;
    return 0;
}

/* Determine the level to which the HW decomp is consistent (same
 * name, same # of processes) */

/*@
  BENV_HwdescFindCommonLevel - Find the highest level where all procees have thesame hardware description

Input Parameters:
+ comm - Communicator of processes
. hwc - An hwdescCtx for the process
- maxlevel - The maximum level to consider.

Output Parameter:
. nlevels - All processes have consistent descriptions for levels 0 through
 'nlevels-1'

.N returnvalue
  @*/
int BENV_HwdescFindCommonLevel(MPI_Comm comm, hwdescCtx *hwc, int maxlevel,
			       int *nlevels)
{
    hwdescMPIInfo *mpiinfo;
    hwdescObjInfo *objinfo;
    int hwdepth = hwc->nlevel;
    int i, d;

    CDBGFCALLENTER;
    mpiinfo = hwc->collinfo;
    if (mpiinfo == 0) {
	// Error and return?
    }

    objinfo = hwc->objinfo;
    if (objinfo == 0) {
	// Error and return?
    }

    *nlevels = -1; /* set the default */
    d = hwdepth;
    if (d > maxlevel) d = maxlevel;

    for (i=0; i<d; i++) {
	int csame[2], lsize;
	MPI_Comm_size(mpiinfo[i].objcomm, &lsize);
	csame[0] = lsize;
	csame[1] = -lsize;
	MPI_Allreduce(MPI_IN_PLACE, csame, 2, MPI_INT, MPI_MAX, comm);
	if (csame[0] != -csame[1]) {
	    /* Objects on this level aren't the same size for everyone */
	    *nlevels = i;
	    break;
	}
	/* Are the objects the same kind? */
	csame[0] = objinfo[i].kind;
	csame[1] = -csame[0];
	MPI_Allreduce(MPI_IN_PLACE, csame, 2, MPI_INT, MPI_MAX, comm);
	if (csame[0] != -csame[1]) {
	    /* Objects on this level don't have the same kind */
	    *nlevels = i;
	    break;
	}
    }
    if (i == d) *nlevels = d;

    CDBGFCALLEXIT;
    return 0;
}

/* Initialize ta level of the hw description (for collinfo and objinfo)
*/
static void hwdescMPIInit(hwdescMPIInfo *mpiinfo)
{
    mpiinfo->objcomm         = MPI_COMM_NULL;
    mpiinfo->nSiblings       = 1;
    mpiinfo->siblingNum      = 0;
    mpiinfo->leadersInParent = 0;
    mpiinfo->descstr         = 0;
    mpiinfo->allsizeone      = 0;
}
static void hwdescObjInit(hwdescObjInfo *objinfo)
{
    objinfo->nobj       = 0;
    objinfo->objidx     = -1;
    objinfo->nodenobj   = 0;
    objinfo->nodeobjidx = -1;
    objinfo->rawnobj    = 0;
    objinfo->rawobjidx  = -1;
    objinfo->kind       = 0;
    objinfo->csrc       = BENV_HWDESC_CONFIG_UNKNOWN;
    objinfo->asrc       = BENV_HWDESC_ASSIGN_UNKNOWN;
    objinfo->othersrc   = 0;
}

/*
  Given a string of the form B or C(n), extract the block size:
  If B, blocksize = defblock
  If C, blocksize = n
  If unrecognized, return -1 (error)
  Read policy[*loc...]. On return, *loc points to the next character to
  process (should be either ':' or NULL)

  FIXME(?): skip over leading :
 */
// make static and local?
int BENVi_GetBlocksizeFromString(const char *policy, int *loc, int defblock)
{
    int l = *loc, block;

    CDBGFCALLENTER;
    /* defblock is usually nthings/ number_of_objects. If there are more
     objects than things, this will give 0, when we want at least 1 */
    if (defblock == 0) defblock = 1;
    if (policy[l] == 'B') {
	l++;
	block = defblock;
    }
    else if (policy[l] == 'C') {
	l++;
	if (policy[l] == '(') {
	    block = 0;
	    l++;
	    while (isdigit(policy[l])) {
		block = block * 10 + policy[l]-'0';
		l++;
	    }
	    if (policy[l] != ')') {
		return -1;
	    }
	    else
		l++;
	}
	else {
	    block = 1;
	}
    }
    else {
	return -1;
    }
    *loc = l;

    CDBGFCALLEXIT;
    return block;
}

/* Given a scheduler policy for assigning tasks to nodes and sockets, as
   well as the rank of a process in MPI_COMM_WORLD and the number of nodes
   nobjs[0] and sockets/node nobjs[1], return the node and socket of that
   process

   Given a character string 'policy', number of processes 'np', rank of
   this process 'rank', and an array 'nobjs' with the number of nodes and
   the number of sockets on each node, return the node number 'node', the
   socket number 'socket', and rank in socket (core) in 'rins'
*/

/* FIXME: use nodenum, socketnum, rins (rank in socket) */
/* Note that this requires having the numebr of nodes and sockets
   available in nobjs */
/* FIXME: separate routines for node and socket from policy. Return pointer
   to unused policy (at least from node) */
/* Given a policy for distributing process/threads (blocksize), the
   number of threads, the index (rank) of this thread, and the number of
   objects across which to distribute, return which object the thread is
   assigned to, the rank of that thread on that object, and the number
   of processes/threads assigned to that object (needed to recursively
   apply this routine)
   If blocksize is -1, instead use np/nobj as the size. Threads are
   distributed cyclically (a block distribution is just cyclic with size
   np/nobj)
 */
int BENVi_DistribRankByPolicy(int np, int rank, int nobj,
			      int blocksize, int *objidx, int *rankinobj,
			      int *npinobj)
{
    int idx, lrank, blocknum, npobj, k ;

    CDBGFCALLENTERV("np=%d,rank=%d,nobj=%d,blocksize=%d\n",np, rank, nobj, blocksize);
    CDBGV(GETDESC,ALL,"Calling %s with np=%d,  rank=%d, nobj=%d, blocksize=%d\n",
	   __func__, np, rank, nobj, blocksize);

    if (blocksize == -1) {
	/* Sanity check + if np < nobj, use blocksize of 1 */
	if (nobj == 0) {
	    fprintf(stderr, "nobj = 0 in DistribRanksByPolicy!\n");
	    blocksize = 0;
	}
	else if (np < nobj)
	    blocksize = 1;
	else
	    blocksize = np / nobj;
    }
    if (blocksize == 0) {
	fprintf(stderr, "Computed blocksize is 0! np = %d, nobj = %d\n",
		np, nobj);
	fflush(stderr);
    }
    /* cyclically distribute to nobj in blocks of blocksize */
    blocknum = rank / blocksize;
    idx      = blocknum % nobj;
    /* Determine my rank on the object */
    lrank = ((blocknum - idx) / nobj) * blocksize;
    lrank += (rank - idx * blocksize) - lrank*nobj;
    /* Determine the number of elements on the object.
       total number of blocks on obj with idx is given by:
       k = ((np-1)/blocksize-idx)/nobj */
    k = ((np-1)/blocksize - idx)/nobj;
    /* Number on object is (k-1)*bs + last block */
    if (k >= 0) {
	int firstrank, lastrank;
	npobj = (k)*blocksize;
	/* Rank of first thread in the last block is
	   (idx+(k-1))*blocksize. Rank of the last thread is the min of
	   (idx+(k-1))*blocksize+blocksize-1,np) and the number in the
	   block is the last */
	firstrank = (idx + k*nobj)*blocksize;
	lastrank  = firstrank + blocksize - 1;
	if (lastrank >= np) lastrank = np - 1;
	CDBGV(GETDESC,DETAIL,"rank = %d, firstrank = %d, lastrank = %d\n",
	      rank, firstrank, lastrank);
	npobj += lastrank - firstrank + 1;
    }
    else {
	/* This is the case when there are no threads at all on an object */
	CDBGV(GETDESC,DETAIL,"k = %d, setting npinobj to 0\n", k);
	npobj = 0;
    }

    if (npobj <= 0) {
	fprintf(stderr, "Found no threads: np=%d, rank=%d, nobj=%d, blocksize=%d\n",
		np, rank, nobj, blocksize);
	fflush(stderr);
    }
    else {
	CDBGV(GETDESC,ALL,"Returning idx = %d, rank = %d, np = %d\n",
		idx, lrank, npobj);
    }

    *objidx    = idx;
    *rankinobj = lrank;
    *npinobj   = npobj;
    CDBGFCALLEXITV("objidx=%d, rankinobj=%d, npinobj=%d\n", idx, lrank, npobj);
    return 0;
}

/* ------------------------------------------------------------------------ */

/* FIXME: Separate into determine nSiblings/idx and setup leaders/allsizeone */
/* This is an internal routine, available to other parts of baseenv, which
   fills in a single hwdesc entry with information about the split of
   the parent communicator pcomm into disjoint comms, of which this process
   is an element of comm. The call is collective over pcomm.
   It sets the elements:
     nSiblings - number of distinct comm
     leaders   - Array of ranks in pcomm of the leaders (rank 0 in comm)
                 of the communicators comm (that resulted from a split of
		 pcomm)
     allsizeone - True if all comm are of size one.

`   It uses as input:
    nSiblings  (or objinfo->nobj?)
    pcomm
*/
int BENVi_FindLeadersInSplit(MPI_Comm pcomm, MPI_Comm comm,
			     hwdescObjInfo *objinfo, hwdescMPIInfo *mpiinfo)
{
    int *allleaders, *leaders, p_size, p_rank, is_lead, lrank, crank, idx;

    CDBGFCALLENTER;
    MPI_Comm_rank(comm, &lrank);
    is_lead = lrank == 0;
    CDBGV(GETDESC,ALL,"find leaders; is_lead=%d\n", is_lead);
// Why is this different from nobjs???
    MPI_Allreduce(&is_lead, &mpiinfo->nSiblings, 1, MPI_INT,
		  MPI_SUM, pcomm);
    CDBGV(GETDESC,ALL,"Number of distinct comms is %d\n", mpiinfo->nSiblings);

    /* Gather up the rank in the parent of the leaders of the
       communicators at this level. Algorithm is a simple
       brute-force approach - gather ranks for *all* processes
       at this level (array size of the parent comm) and look for
       ranks >= 0 (use -1 if a process is not a leader on this
       level) */
    MPI_Comm_size(pcomm, &p_size);
    allleaders = (int *)malloc(p_size * sizeof(int));
    if (!allleaders) {
	BENVi_MallocErr("find leaders in split", p_size, "int");
	MPI_Abort(MPI_COMM_WORLD, 1);
    }
    if (is_lead)
	MPI_Comm_rank(pcomm, &p_rank);
    else
	p_rank = -1;
    CDBGV(GETDESC,ALL,"Gather ranks of leaders in parent; p_rank=%d\n",
	  p_rank);
    MPI_Gather(&p_rank, 1, MPI_INT, allleaders, 1, MPI_INT, 0, pcomm);

    /* On the root, compress this to remove the -1, then broadcast
       to the rest of the processes */
    CDBG(GETDESC,ALL,"Determine leaders in pcomm");
    leaders = (int *)malloc(mpiinfo->nSiblings * sizeof(int));
    if (!leaders) {
	BENVi_MallocErr("find leaders in split", mpiinfo->nSiblings, "int");
	MPI_Abort(MPI_COMM_WORLD, 0);
    }
    /* See above - if we did not already get the rank in parent of this
       process, get it now. */
    if (p_rank < 0)
	MPI_Comm_rank(pcomm, &p_rank);

    /* Only the rank=0 process in the parent has the gather results.
       Condense the information into just the ranks of the
       process leaders */
    if (p_rank == 0) {
	int j = 0;
	for (int i=0; i<p_size; i++) {
	    if (allleaders[i] >= 0) {
		if (j >= mpiinfo->nSiblings) {
		    fprintf(stderr,
		    "Found too many leaders at index %d; expected only %d\n",
			    i, mpiinfo->nSiblings);
		    fflush(stderr);
		    break;
		}
		CDBGV(GETDESC,ALL,"Found leader for rank %d with value %d\n",
		      i, allleaders[i]);
		leaders[j++] = allleaders[i];
	    }
	}
	CDBGV(GETDESC,ALL,"Found %d leaders in all leaders\n", j);
    }
    CDBGV(GETDESC,ALL,"Bcast the leaders array of size %d\n",
	 mpiinfo->nSiblings);
    MPI_Bcast(leaders, mpiinfo->nSiblings, MPI_INT, 0, pcomm);
    mpiinfo->leadersInParent = leaders;
    CDBG(GETDESC,ALL,"Bcast leaders complete; find my idx");
    if (lrank == 0) {
	/* Find my parent rank in leaders */
	for (idx=0; idx<mpiinfo->nSiblings; idx++)
	    if (leaders[idx] == p_rank) break;
	/* Sanity check */
	if (idx == mpiinfo->nSiblings) {
	    fprintf(stderr, "PANIC: Did not find leader\n");
	    CDBGFCALLEXIT;
	    MPI_Abort(comm, 1);
	    return 1;
	}
	CDBGV(GETDESC,ALL,"Found parent rank = %d in leaders\n", idx);
    }
    MPI_Comm_rank(comm, &crank);
    if (crank == 0) {
	CDBGV(GETDESC,ALL,"Bcast my idx %d to others in my comm\n", idx);
    }
    MPI_Bcast(&idx, 1, MPI_INT, 0, comm);
    mpiinfo->siblingNum = idx;

    /* If there are as many comms as processes in the parent,
       they all have to be of size one.
       Note that "allsizeone" ALSO requires that these are leaves.
       This cannot be determined yet, so there must be a final update
       pass over the hw array */
    if (mpiinfo->nSiblings == p_size) {
	mpiinfo->allsizeone = 1;
	CDBG(GETDESC,ALL,"Setting allsizeone to true");
    }
    else
	mpiinfo->allsizeone = 0;

    CDBGFCALLEXIT;
    return 0;
}

/*@ BENV_HwdescFindObject - Find the index of an object in a hardware description

Input Parameters:
+. hwc - An hwdescCtx for the process
- kind - The kind of object to find

Output Parameters:
+ objlevel - Index of object in 'hwc'. -1 if none found
- isexact - True if the object is an exact match, false otherwise

.N returnvalue

Notes:
This returns the object requested or the closest larger object, defined as
one whose kind value is less than or equal to the reqeusted kind. The reason
for this is that MPI, in the unguided mode, only returns distinct objects.
Thus, MPI jobs running on a single node, for example, will not return a
separate node type. Similarly, if there is either only one NUMA region in a
chip, or the number is unknown, there will be no NUMA kind defined.

In cases where an exact match is required, the user can test the value or
check the 'isexact' output.
  @*/
int BENV_HwdescFindObject(hwdescCtx *hwc, hwdescKind kind, int *objlevel,
    int *isexact)
{
    hwdescObjInfo *objinfo;
    int i, nlevel=hwc->nlevel;;

    CDBGFCALLENTER;
    *objlevel = -1;
    *isexact  = 0;
    objinfo = hwc->objinfo;
    for (i=0; i<nlevel; i++) {
	if (objinfo[i].kind == kind) {
	    *objlevel = i;
	    *isexact = 1;
	    break;
	}
	if (objinfo[i].kind > kind) {
	    /* Objects are numbered from small (job) to large (core) */
	    break;
	}
    }
    /* Check for an object that is larger (smaller kind value) than the
       requested one */
    if (*objlevel == -1) {
	if (i > 0 && objinfo[i-1].kind < kind) {
	    *objlevel = i-1;
	}
    }

    CDBGFCALLEXIT;
    return 0;
}

/* TODO: Check other fields for consistency */
/* TODO: Decide what can be checked, and what must be set. E.g.,
   if only checking parts of objinfo, how to define and what communicators
   are used */
/*@ BENV_HwdescCheckConsistentSizes - Check whether the number of processes
 in the objects at each level are the same

Input Parameters:
+ hwc - hwdescCtx pointer
. comm - If not 'MPI_COMM_NULL', use this communicator to define the proceeses
 that are participating in this check. Otherwise, the communicatores defined
 in the 'collinfo' fields are used.
. startlevel - First level to check
. hwlevel - Number of levels to check
- objormpi - Indicates which values to compare. 0x1 for object, 0x2 for
   MPI, and 0x3 for both

Output Parmaeter:
. nvalid - Pointer to int giving the number of valid levels. See below.

Return Value:
 Zero on success, one on failure (not all consistent for all levels)

  Notes:
This routine checks that all of the processes in the top level of 'hw' (i.e.,
'hw[0].comm') have the same number of processes in the elements of 'hw' at
each level. I.e., that all processes have the same size for the communciators
in 'hw[i].comm'. The routine returns in 'nvalid' the number of valid levels.
For example, if 'hw[0].comm' is 'MPI_COMM_WORLD', and 'hwlevel' is '3', with
'hw[0].comm' being 'MPI_COMM_WORLD', 'hw[1].comm' the communicator for the
node, and 'hw[2].comm' being the communicator for the socket, this will
return '3' in 'nvalid' if each node has the same number of sockets and each
socket has the same number of processes. It will return '2' in 'nvalid' if
each node has the same number of sockets but not the same number of processes.
  @*/
int BENV_HwdescCheckConsistentSizes(hwdescCtx *hwc, MPI_Comm comm,
				    int startlevel, int nlevel, int objormpi,
				    int *nvalid)
{
    int i, sz[2];
    MPI_Comm lcomm;

    CDBGFCALLENTER;
    if (nlevel == -1) nlevel = hwc->nlevel;

    if (comm == MPI_COMM_NULL) {
	lcomm = hwc->collinfo[0].objcomm;
    }
    else {
	lcomm = comm;
    }
    if (objormpi & 0x1) {
	/* Check the values in objinfo */
	for (i=startlevel; i<nlevel; i++) {
	    /* Check size of this level (relative to what?) */
	    sz[0] = hwc->objinfo[i].nobj;
	    sz[1] = -sz[0];
	    MPI_Allreduce(MPI_IN_PLACE, sz, 2, MPI_INT, MPI_MAX, lcomm);
	    if (sz[0] != -sz[1]) {
		/* Not all processes in lcomm have the same size
		   object at level i, so number of valid levels is i */
		*nvalid = i;
		return 1;
	    }
	}
	*nvalid = nlevel;
    }

    if (objormpi & 0x2) {
	/* Check the values in collinfo */
	for (i=startlevel; i<nlevel; i++) {
	    /* Check size of this level (relative to what?) */
	    MPI_Comm_size(hwc->collinfo[i].objcomm, &sz[0]);
	    sz[1] = -sz[0];
	    MPI_Allreduce(MPI_IN_PLACE, sz, 2, MPI_INT, MPI_MAX, lcomm);
	    if (sz[0] != -sz[1]) {
		/* Not all processes in hw[0].comm have the same size
		   object at level i, so number of valid levels is i */
		*nvalid = i;
		return 1;
	    }
	}
	*nvalid = nlevel;
    }
    CDBGFCALLEXIT;
    return 0;
}

/*@ BENV_HwdescValidate - Check an hwdesc context for correctness

Input Parameters:
+ pcomm - The parent communicator of the context
- hwc - The hwdesc context to check

Return Value:
0 if the context is correct. Non-zero otherwise.
  @*/
int BENV_HwdescValidate(MPI_Comm pcomm, hwdescCtx *hwc)
{
    int rc, prank, minval, lev;
    const void *ptrs[2];

    CDBGFCALLENTER;
    MPI_Comm_rank(pcomm, &prank);

    /* Confirm valid entries:
       Basic:
       nlevel > 0
       objinfo, mpiinfo exist
       mpiinfo[0].comm exists

       Each level:
       objinfo[i].nobj > 0, objidx in [0, nobj-1]
                  kind value > kind of previous level (or UNKNOWN?)
		  Optional: csrc and asrc set
       mpiinfo[i].objcomm set
                  nsiblings > 0, siblingNum in [0, nsiblings-1]
		       ? compare to nobj??
		  allsizeone set, correct (compute related to parent comm)
		  leadersInParent allocated, values in range (0 to size of
		  parent comm)
     */
    /* Basic tests */
    MPI_Allreduce(&hwc->nlevel, &minval, 1, MPI_INT, MPI_MIN, pcomm);
    if (minval <= 0) {
	if (prank == 0) {
	    fprintf(stderr, "Some hwdescCtx have nlevel <= 0\n");
	    fflush(stderr);
	}
	goto fn_fail;
    }
    ptrs[0] = hwc->objinfo;
    ptrs[1] = hwc->collinfo;
    rc = BENV_CheckNonNull(pcomm, ptrs, 2);
    if (rc) {
	if (prank == 0) {
	    fprintf(stderr, "Some hwdescCtx has null object or mpi info\n");
	    fflush(stderr);
	}
	goto fn_fail;
    }

    /* For each level, check that the level is consistent WRT its parent */
    for (lev=0; lev<hwc->nlevel; lev++) {
	int ivals[2];
	/*
	   Each level:
       objinfo[i].nobj > 0, and same in parent, objidx in [0, nobj-1]
                  kind value > kind of previous level (or UNKNOWN?)
		  Optional: csrc and asrc set
       mpiinfo[i].objcomm set
                  nsiblings > 0, and same in parent,
		  siblingNum in [0, nsiblings-1]
		       ? compare to nobj??
		  allsizeone set, correct (compute related to parent comm)
		  leadersInParent allocated, values in range (0 to size of
		  parent comm)
	 */
	ivals[0] = hwc->objinfo[lev].nobj;
	ivals[1] = hwc->collinfo[lev].nSiblings;
	rc = BENV_CheckSameInts(pcomm, ivals, 2);
	if (rc) {
	    if (prank == 0) {
		fprintf(stderr, "Number of objects not the same at level %d\n",
			lev);
		fflush(stderr);
	    }
	    goto fn_fail;
	}
	if (ivals[0] <= 0 || ivals[1] <= 0) {
	    if (prank == 0) {
		fprintf(stderr, "Invalid number of objects (%d) or siblings (%d) at level %d\n",
			ivals[0], ivals[1], lev);
		fflush(stderr);
	    }
	    goto fn_fail;
	}

	/* Values are true (1) if valid, false (0) if not */
	ivals[0] = (hwc->objinfo[lev].objidx >= 0) &&
	    (hwc->objinfo[lev].objidx < hwc->objinfo[lev].nobj);
	ivals[1] = (hwc->collinfo[lev].siblingNum >= 0) &&
	    (hwc->collinfo[lev].siblingNum < hwc->collinfo[lev].nSiblings);
	rc = BENV_CheckSameInts(pcomm, ivals, 2);
	if (rc || ivals[0] == 0 || ivals[1] == 0) {
	    if (prank == 0) {
		fprintf(stderr, "Number of objects not in-range at level %d\n",
			lev);
		if (ivals[0] == 0) {
		    fprintf(stderr, "objidx = %d, nobj = %d\n",
			    hwc->objinfo[lev].objidx,
			    hwc->objinfo[lev].nobj);
		}
		if (ivals[1] == 0) {
		    fprintf(stderr, "siblingNum = %d, nsibs = %d\n",
			    hwc->collinfo[lev].siblingNum,
			    hwc->collinfo[lev].nSiblings);
		}
		fflush(stderr);
	    }
	    goto fn_fail;
	}

	/* Check this level's communicator and leaders array */
	ivals[0] = (hwc->collinfo[lev].objcomm != 0) &&
	    (hwc->collinfo[lev].objcomm != MPI_COMM_NULL);
	/* Leaders should be null for level zero and non-null otherwise */
	ivals[1] = ((lev > 0) && (hwc->collinfo[lev].leadersInParent != 0)) ||
	    ((lev == 0) && (hwc->collinfo[lev].leadersInParent == 0));
	rc = BENV_CheckSameInts(pcomm, ivals, 2);
	if (rc || ivals[0] == 0 || ivals[1] == 0) {
	    if (prank == 0) {
		if (ivals[0] == 0) {
		    fprintf(stderr, "Object communicator NULL at level %d\n",
			    lev);
		}
		if (ivals[1] == 0) {
		    if (lev > 0) {
			fprintf(stderr, "Leaders array NULL at level %d\n",
				lev);
		    }
		    else {
			fprintf(stderr, "Leaders array not NULL at level 0\n");
			fprintf(stderr, "At rank=0, leaders array - %p\n",
				hwc->collinfo[lev].leadersInParent);
		    }
		}
		fflush(stderr);
	    }
	    goto fn_fail;
	}

	/* Update parent and parent rank */
	pcomm = hwc->collinfo[lev].objcomm;
	MPI_Comm_rank(pcomm, &prank);
    }

    CDBGFCALLEXIT;
    return rc;

fn_fail:
    CDBGFCALLEXIT;
    return 1;

}

static void settopobject(MPI_Comm comm, hwdescObjInfo *objinfo,
			 hwdescMPIInfo *collinfo, hwdescConfigSrc csrc,
			 hwdescAssignSrc asrc)
{
    // Need to get the MPI info.
    collinfo[0].objcomm = comm;
    collinfo[0].nSiblings = 1;
    collinfo[0].siblingNum = 0;
    /* Try to get the communicator name as the description. */
    BENVi_GetCommName(comm, "Top level communicator", &collinfo[0].descstr);

    objinfo[0].nobj   = 1;
    objinfo[0].objidx = 0;
    objinfo[0].csrc   = csrc;
    objinfo[0].asrc   = asrc;
}
