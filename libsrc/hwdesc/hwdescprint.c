/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 * Copyright (C) by University of Illinois 2023
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

static const char *getNodeName(const char *desc, MPI_Comm comm);
static const char *hwdescToStr(MPI_Comm pcomm, hwdescObjInfo *objinfo,
			       hwdescMPIInfo *mpiinfo,
			       int hwdepth, int depth);
static char *indentstring(const char *str, int indent);
static const char *hwdescGetRankList(MPI_Comm comm, MPI_Comm refcomm,
				     int depth, const char *desc);
static const char *hwdescline(int indent, int csize, int wrank, int idx,
			      const char *descstr);
static const char *getNodeobjidxList(MPI_Comm pcomm, hwdescObjInfo *objinfo);
static const char *getSiblingRankList(MPI_Comm comm);

CDBGFCALLDECL;
CDBGEDECL(PRINTHW);

/*@
 BENV_HwdescPrintAll - Generate a printed description of the hwdecsCtx
  description of the system

Input Parameters:
+ fp - FILE pointer for output
. pcomm - Communicator for all processes. Call is collective over pcomm.
  This is the parent of the communicator for 'hw[0]'. If this is
  'MPI_COMM_WORLD', this should be 'MPI_COMM_NULL'
. hwc - HwdescCtx from 'BENV_HwdescGetDescFromMPI' or similar
- depth - Used to control indentation of output. Use '0' unless a different
 indentation is desired

.N returnvalue

Notes:
This is a collective call over 'pcomm'. However, only the process with
rank 0 in 'pcomm' performs output. The implementation of this routine
recursively calls an internal routine to output information about each
level in 'hw'.  The 'depth' parameter is used to ensure that the
output indents each level with respect to the previous level.
  @*/
int BENV_HwdescPrintAll(FILE *fp, MPI_Comm pcomm, hwdescCtx *hwc,
			int depth)
{
    static const char *sblock = 0;

    CDBGFCALLENTER;
    CDBGV(PRINTHW,BASIC,"HwdescPrintAll(%d): Enter hwdepth=%d\n",
	  depth, hwc->nlevel);
    sblock = hwdescToStr(pcomm, hwc->objinfo, hwc->collinfo,
			 hwc->nlevel, depth);
    if (sblock) {
	fputs(sblock, fp);
	free((void *)sblock);
	/* Only print source if there as something to print */
	if (hwc->source) {
	    fprintf(fp, "HWInfo from: %s\n", hwc->source);
	}
	fflush(fp);
    }
    CDBGV(PRINTHW,BASIC,"HwdescPrintAll(%d): return\n", depth);
    CDBGFCALLEXIT;
    return 0;
}

/*@
 BENV_HwdescPrintLocal - Print the elements of an hwdescCtx

Input parameters:
+ fp - File pointer for output
. hwc - hwdescCtx pointer
- prefix - String to prefix each output line

.N returnvalue

Notes:
This is a local routine. Different processes should output to separate files.
If 'stdout' or 'stderr' is used as the file pointer, output may be lost or
interleaved. It is better to send output to a separate file for each process,
or ensure output is serialized to a single file (and due to buffering in
parallel runtime systems, this can be hard to accomplish for 'stdout' or
'stderr').
 @*/
int BENV_HwdescPrintLocal(FILE *fp, hwdescCtx *hwc, const char *prefix)
{
    int hwdepth = hwc->nlevel;
    hwdescMPIInfo *mpiinfo = hwc->collinfo;
    hwdescObjInfo *objinfo = hwc->objinfo;

    CDBGFCALLENTER;
    for (int i=0; i<hwdepth; i++) {
	int lsize, lrank;
	if (mpiinfo[i].objcomm != MPI_COMM_NULL) {
	    MPI_Comm_rank(mpiinfo[i].objcomm, &lrank);
	    MPI_Comm_size(mpiinfo[i].objcomm, &lsize);
	}
	else {
	    lrank = -1;
	    lsize = 0;
	}
	if (lrank >= 0) {
	    const char *desc;
	    if (mpiinfo[i].descstr) desc = mpiinfo[i].descstr;
	    else desc = BENV_HwdescKindStr(objinfo[i].kind);
	    fprintf(fp, "%s: [%d] comm(r=%d,size=%d): %s\n",
		    prefix, i, lrank, lsize, desc);
	}
	if (mpiinfo[i].nSiblings > 0) {
	    fprintf(fp, "%s: nleaders = %d, leader idx=%d, parent r=",
		    prefix, mpiinfo[i].nSiblings, mpiinfo[i].siblingNum);
	    if (mpiinfo[i].leadersInParent) {
		intlistPtr ilist;
		char *lstr;
		ilist = BENV_UtilCompressIntList(mpiinfo[i].nSiblings,
						 mpiinfo[i].leadersInParent, -1);
		lstr = BENV_UtilIntListToStr(ilist);
		BENV_UtilFreeIntList(ilist);
		fputs(lstr, fp);
		free(lstr);
		fputc('\n', fp);
	    }
	    else {
		fputs("(none)\n", fp);
	    }
	}
	/* This is additional information not provided by the older
	   HwdescPrintCtxLocal
	   As a special case, it outputs both the nodenobj and nodeobjidx,
	   if nodenobj is set. This is useful when using the nodeinfo routines
	   to configure the node portion of a hardware heirarchy.
	*/
	if (objinfo[i].nobj > 0 || objinfo[i].nodenobj > 0) {
	    if (objinfo[i].nodenobj > 0) {
		fprintf(fp, "%s: objinfo[%d]: node(nobj=%d, objidx=%d), nobjs=%d, objidx=%d, raw(nobj=%d, idx=%d)\n",
			prefix, i, objinfo[i].nodenobj, objinfo[i].nodeobjidx,
			objinfo[i].nobj, objinfo[i].objidx,
			objinfo[i].rawnobj, objinfo[i].rawobjidx);
	    }
	    else {
		fprintf(fp, "%s: objinfo[%d]: nobjs=%d, objidx=%d\n",
			prefix, i, objinfo[i].nobj, objinfo[i].objidx);
	    }
	    fprintf(fp, "\tkind = %s, config source = %s, assign source = %s\n",
		    BENV_HwdescKindStr(objinfo[i].kind),
		    BENV_HwdescConfigStr(objinfo[i].csrc),
		    BENV_HwdescAssignStr(objinfo[i].asrc));
	    if (objinfo[i].othersrc) {
		fprintf(fp, "\tother source = %s\n", objinfo[i].othersrc);
	    }
	}
	else {
	    fprintf(fp, "%s: objinfo[%d]: nobjs=%d, nodenobj=%d, rawnobj=%d for %s\n",
		    prefix, i, objinfo[i].nobj, objinfo[i].nodenobj,
		    objinfo[i].rawnobj, BENV_HwdescKindStr(objinfo[i].kind));
	}
    }
    fflush(fp);
    CDBGFCALLEXIT;
    return 0;
}

/*@ BENV_HwdescToStr - Create a string the describes the hardware hierarchy

Input Parameter:
. hwc - Pointer to hwdescCtx describing the hardware hierarchy

Return Value:
String describing the hardware hierarchy described by 'hwc'.

Notes:
This is a collective routine over the processes in 'hwc'
@*/
const char *BENV_HwdescToStr(hwdescCtx *hwc)
{
    return hwdescToStr(MPI_COMM_NULL, hwc->objinfo, hwc->collinfo,
		       hwc->nlevel, 0);
}



// Combined config+assign: MPI_SHARED, MPI_UNGUIDED, OMPI_SPLIT_TYPES
// hwloc

/*@ BENV_HwdescKindStr - Return a string for an hwdesckind

Input Parameter:
. kind - An hwdesc kind, such as 'BENV_HWDESC_NODE'

Return Value:
String with name corresponding to kind. The string is statid storage; do `not`
free it.
@*/
const char *BENV_HwdescKindStr(hwdescKind kind)
{
    const char *result="<UNKNOWN>";
    switch (kind) {
    case BENV_HWDESC_JOB: result = "Job"; break;
    case BENV_HWDESC_NODE: result = "Node"; break;
    case BENV_HWDESC_SOCKET: result = "Socket"; break;
    case BENV_HWDESC_NUMA: result = "NUMA"; break;
    case BENV_HWDESC_CORE: result = "Core"; break;
    case BENV_HWDESC_OTHER: result = "Other"; break;
    default:
    }
    return result;
}

/*@ BENV_HwdescConfigStr - Return a string for an hwdesc configure kind

Input Parameter:
. kind - An hwdescConfigSrc, such as 'BENV_HWDESC_CONFIG_MPI4'

Return Value:
String with name corresponding to kind. The string is statid storage; do `not`
free it.
@*/
const char *BENV_HwdescConfigStr(hwdescConfigSrc csrc)
{
    const char *result="<UNKNOWN>";
    switch (csrc) {
    case BENV_HWDESC_CONFIG_UNKNOWN: result = "Unknown"; break;
    case BENV_HWDESC_CONFIG_HWLOC: result = "hwloc"; break;
    case BENV_HWDESC_CONFIG_SYSCTL: result = "sysctl"; break;
    case BENV_HWDESC_CONFIG_CPUINFO: result = "cpuinfo"; break;
    case BENV_HWDESC_CONFIG_NUMANUMNODE: result = "numa_num_node"; break;
    case BENV_HWDESC_CONFIG_MPI4: result = "MPI4"; break;
    case BENV_HWDESC_CONFIG_MPI_SHARED: result = "MPI split on COMM_SHARED";
	break;
    case BENV_HWDESC_CONFIG_ENV: result = "environment variables"; break;
    case BENV_HWDESC_CONFIG_GIVEN: result = "user parameters"; break;
    default:
    }
    return result;
}

/*@ BENV_HwdescAssignStr - Return a string for an hwdescAssignSrc

Input Parameter:
. kind - An hwdescAssignSrc, such as 'BENV_HWDESC_ASSIGN_GETCPU'

Return Value:
String with name corresponding to kind. The string is statid storage; do `not`
free it.
@*/
const char *BENV_HwdescAssignStr(hwdescAssignSrc asrc)
{
    const char *result="<UNKNOWN>";
    switch (asrc) {
    case BENV_HWDESC_ASSIGN_UNKNOWN: result = "Unknown"; break;
    case BENV_HWDESC_ASSIGN_GETCPU: result = "getcpu"; break;
    case BENV_HWDESC_ASSIGN_SCHEDGETCPU: result = "schedgetcpu"; break;
    case BENV_HWDESC_ASSIGN_NUMAGETNODE: result = "numa_get_node"; break;
    case BENV_HWDESC_ASSIGN_MPI4: result = "MPI4"; break;
    case BENV_HWDESC_ASSIGN_MPI_SHARED: result = "MPI split on COMM_SHARED";
	break;
    case BENV_HWDESC_ASSIGN_POLICY: result = "scheduler policy"; break;
    case BENV_HWDESC_ASSIGN_HWLOC: result = "hwloc"; break;
    case BENV_HWDESC_ASSIGN_GIVEN: result = "user parameters"; break;
    default:
    }
    return result;
}


/* ------------------------------------------------------------------------ */

/* Get the name of the node and postpend it to desc. This is a local routine */
static const char *getNodeName(const char *desc, MPI_Comm comm)
{
    char nodename[MPI_MAX_PROCESSOR_NAME+1];
    const char *nname;
    int rlen, slen;

    MPI_Get_processor_name(nodename, &rlen);
    slen = rlen + 1 + strlen(desc) + 1;
    nname = (const char *)malloc(slen);
    if (!nname) {
	BENVi_MallocErr("node name", slen, "char");
    }
    snprintf((char *)nname, slen, "%s:%s", desc, nodename);

    return nname;
}

/* Only process with rank==0 in comm returns a string */
static const char *getSiblingRankList(MPI_Comm comm)
{
    int rank, size, *hwranks=0, wrank;
    const char *sblock=0;

    CDBGFCALLENTER;
    MPI_Comm_rank(comm, &rank);
    if (rank == 0) {
	MPI_Comm_size(comm, &size);
	hwranks = (int *)malloc(size * sizeof(int));
	if (!hwranks) {
	    BENVi_MallocErr("sibling rank list", size, "int");
	    return 0;
	}
    }

    /* Compress the representation of a collection of communicators,
       all of size one.*/
    MPI_Comm_rank(MPI_COMM_WORLD, &wrank);
    MPI_Gather(&wrank, 1, MPI_INT, hwranks, 1, MPI_INT, 0, comm);
    if (rank == 0) {
	intlistPtr ilist;
	ilist = BENV_UtilCompressIntList(size, hwranks, -1);
	sblock = BENV_UtilIntListToStr(ilist);
	BENV_UtilFreeIntList(ilist);
	free(hwranks);
    }
    CDBGFCALLEXITV("getSiblingRankList returning %s\n", sblock);
    return sblock;
}

static const char *getNodeobjidxList(MPI_Comm pcomm, hwdescObjInfo *objinfo)
{
    int prank, psize, *hwranks=0, objidx;
    char *sblock = 0;

    CDBGFCALLENTER;
    MPI_Comm_rank(pcomm, &prank);
    if (prank == 0) {
	MPI_Comm_size(pcomm, &psize);
	hwranks = (int *)malloc(psize * sizeof(int));
	if (!hwranks) {
	    BENVi_MallocErr("sibling rank list", psize, "int");
	    return 0;
	}
    }
    objidx = objinfo->nodeobjidx;
    if (objidx < 0 && objinfo->objidx >= 0) objidx = objinfo->objidx;
    MPI_Gather(&objidx, 1, MPI_INT, hwranks, 1, MPI_INT, 0, pcomm);
    if (prank == 0) {
	intlistPtr ilist;
	ilist = BENV_UtilCompressIntList(psize, hwranks, -1);
	sblock = BENV_UtilIntListToStr(ilist);
	BENV_UtilFreeIntList(ilist);
	free(hwranks);
    }
    CDBGFCALLEXITV("Returning %s\n", sblock ? sblock : "<not root>");
    return sblock;
}

/*
 * Convert (part of) an hwdescCtx to a string.
 *
 * For the most part, at each level, return the description of the
 * process with rank 0 in the hw[].comm, concatenated with the description
 * of the children. A special case is to handle the case where the lowest
 * level consists of communicators of all size one. An example is a entries
 * for "core" - there's no real value in giving each of these a separate
 * line in the description; it is better to identify type as a range (or list)
 * of ranks in COMM_WORLD.
 *
 * Algorithm:
 * 1. rank in hw[0].comm is crank.
 * 2. If crank != 0, recurse into hw[1..]; return. Note that this process
 *    may need to communicate or create a description at a lower level
 * 3. Get the description of the children into cblock
 */
static const char *hwdescToStr(MPI_Comm pcomm, hwdescObjInfo *objinfo,
			       hwdescMPIInfo *mpiinfo,
			       int hwdepth, int depth)
{
    const char *sblock=0, *rmsg=0, *cmsg=0, *desc=0;
    const char *origdesc;
    int csize, crank, wrank, allleaf;

    CDBGFCALLENTERV("hwdepth=%d depth=%d\n", hwdepth, depth);
    CDBGV(PRINTHW,DETAIL,"HwdescToStr(%d): hwlevels=%d\n", depth, hwdepth);
    /* return a null pointer for an empty hwdesc list */
    if (hwdepth <= 0) {
	CDBGFCALLEXIT;
	return 0;
    }

    MPI_Comm_size(mpiinfo[0].objcomm, &csize);
    MPI_Comm_rank(mpiinfo[0].objcomm, &crank);
    MPI_Comm_rank(MPI_COMM_WORLD, &wrank);

    CDBGV(PRINTHW,DETAIL,"HwdescToStr(%d): crank=%d csize=%d\n",
	  depth, crank, csize);

    /* If we're not a lead process (rank 0 in hw[0].comm) in this level, just
       descend into the structure; that may result in some communication to
       siblings and/or parents at lower levels */
    if (crank != 0) {
	(void)hwdescToStr(mpiinfo[0].objcomm, objinfo+1, mpiinfo+1,
			  hwdepth-1, depth+1);
	CDBGFCALLEXIT;
	return 0;
    }

    /* crank == 0 for the rest of this routine */

    /* Before looking at children or description, are we an allleaf level */
    allleaf = 0;
    if (mpiinfo[0].allsizeone) {
	int isleaf;
	/* Is every process a leaf */
	isleaf = hwdepth == 1;
	MPI_Allreduce(&isleaf, &allleaf, 1, MPI_INT, MPI_LAND,
		      mpiinfo[0].objcomm);
	CDBGV(PRINTHW,ALL,"HwdescToStr(%d): isleaf=%d and allleaf=%d\n",
	      depth, isleaf, allleaf);
    }

    /* For lead process, get the description of the top element.
       There are two special cases:
       1. Handle the special case of "node", where we append the value of
       MPI_Get_processor_name.
       2. If all communicators at this level have size 1, gather up the
       information on all siblings as well. This improves the readability
       of the output where there is one MPI process per core.
       Note that the tests are done in this order. In the case of 1
       process per node, we still want the node names, rather than
       collecting all of the siblings.
    */
    origdesc = mpiinfo[0].descstr;
    if (!origdesc) origdesc = BENV_HwdescKindStr(objinfo[0].kind);
    CDBGV(PRINTHW,DETAIL,"HwdescToStr(%d): get description for %s\n",
	  depth, origdesc);
    if (cvar_hwdesc_addNodeName && objinfo[0].kind == BENV_HWDESC_NODE) {
	desc = getNodeName(origdesc, mpiinfo[0].objcomm);
	sblock = hwdescline(depth, csize, wrank, mpiinfo[0].siblingNum, desc);
	free((void *)desc);
    }
    else if (mpiinfo[0].allsizeone && allleaf && pcomm != MPI_COMM_NULL) {
	/* Form the description as:
	   ranks in COMM_WORLD of processes (use intlist)
	   (nodeobjidx for each), in parens (use intlist)
	   origdesc and newline
	*/
	/* Get the ranks in world of all of the siblings, in the sibling
	   order */
	CDBGV(PRINTHW,DETAIL,"HwdescToStr(%d): getSiblingRankList with comm=%ld for %s\n",
	      depth, (long)pcomm, origdesc);
	const char *rmsg1, *rmsg2;
	int prank;
	MPI_Comm_rank(pcomm, &prank);
	rmsg1 = getSiblingRankList(pcomm);
	rmsg2 = getNodeobjidxList(pcomm, &objinfo[0]);
	/* Only the leadercomm (rank == 0 in pcomm) has any string to return */
	if (prank == 0) {
	    rmsg1 = indentstring(rmsg1,depth);
	    sblock = BENV_StringApp(rmsg1, 5, " (hwid ", rmsg2, ") ", origdesc, "\n");
	    if (!sblock || mpiinfo[0].siblingNum > 0) {
		CDBGV(PRINTHW,ALL,"HwdescToStr(%d): **Returning in all size one with idx = %d (or memory null)\n",
		      depth, mpiinfo[0].siblingNum);
		CDBGFCALLEXIT;
		return 0;
	    }
	}
	else sblock = 0;

    }
    else if (objinfo[0].kind > BENV_HWDESC_NODE) {
	/* Add object number and index */
	char *newdesc;
	int odlen = strlen(origdesc), ndlen;
	ndlen = 2*10 + 7 + odlen;
	newdesc = (char *)malloc(ndlen);
	if (!newdesc) {}
	snprintf(newdesc, ndlen, "(%d of %d) %s",
		 objinfo[0].objidx, objinfo[0].nobj, origdesc ? origdesc : "");
	sblock = hwdescline(depth, csize, wrank, mpiinfo[0].siblingNum,
			    newdesc);
	free(newdesc);
    }
    else {
	sblock = hwdescline(depth, csize, wrank, mpiinfo[0].siblingNum,
			    origdesc);
    }

    CDBGV(PRINTHW,DETAIL,"HwdescToStr(%d): got description for %s\n",
	  depth, sblock);

    /* Get a description of the children and append that to rmsg */
    cmsg = 0;
    if (hwdepth > 1)
	cmsg = hwdescToStr(mpiinfo[0].objcomm, objinfo+1, mpiinfo+1,
			   hwdepth-1, depth+1);
    else if (csize > 1) {
	/* bottom since hwdepth == 1 in this case
	   (< 1 already handled above) */
	/* bottom comm has size > 1 and there are no children: generate a
	   list of the ranks in this "leaf" */
	/* Make relative to COMM_WORLD. */
	cmsg = hwdescGetRankList(mpiinfo[0].objcomm,
				 MPI_COMM_WORLD, depth, "(ranks in WORLD)");
	/* Add information about the object and add the newline */
	cmsg = BENV_StringApp(cmsg, 3, " ",
			      BENV_HwdescKindStr(objinfo[0].kind), "\n");
    }
    // else Nothing more to do.

    /* Append the description of the children */
    if (cmsg) {
	CDBGV(PRINTHW,DETAIL,"HwdescToStr(%d): child description for %s\n",
	      depth, cmsg);
	sblock = BENV_StringConcatFree(sblock, cmsg);
    }

    /* Gather sibling data to the lead process of the siblings - the process
       with idx 0 and rank 0 in hw[0].comm. Note that we've already
       handled the case of all siblings have size one */
    if ((!mpiinfo[0].allsizeone || !allleaf) && mpiinfo[0].nSiblings > 1) {
	int token=0;
	CDBGV(PRINTHW,DETAIL,"HwdescToStr(%d): Get Sibling description (pcomm=%ld)\n",
	      depth, (long)pcomm);
	if (mpiinfo[0].siblingNum > 0) {
	    /* Note crank == 0 already, so only the lead process in each
	       sibling is communicating */
	    /* Wait for request to send, then send to the level leader */
	    CDBGV(PRINTHW,DETAIL,"HwdescToStr(%d): about to send string %s\n",
		  depth, sblock);
	    MPI_Recv(&token, 1, MPI_INT, mpiinfo[0].leadersInParent[0],
		     0, pcomm, MPI_STATUS_IGNORE);
	    BENV_StringSend(sblock, mpiinfo[0].leadersInParent[0], 0, pcomm);
	    free((void *)sblock);
	    sblock = 0;
	}
	else if (mpiinfo[0].siblingNum == 0) {
	    CDBGV(PRINTHW,DETAIL,"HwdescToStr(%d): start receiving from %d siblings\n",
		  depth, mpiinfo[0].nSiblings-1);
	    for (int i=1; i<mpiinfo[0].nSiblings; i++) {
		MPI_Send(&token, 1, MPI_INT, mpiinfo[0].leadersInParent[i],
			 0, pcomm);
		rmsg = BENV_StringRecv(mpiinfo[0].leadersInParent[i], 0, pcomm);
		CDBGV(PRINTHW,DETAIL,"HwdescToStr(%d): received string %s\n",
		      depth, rmsg);
		sblock = BENV_StringConcatFree(sblock, rmsg);
	    }
	}
    }
    else if (sblock) {
	CDBGV(PRINTHW,ALL,"HwdescToStr(%d): **Have sblock %s but did not send to parent\n",
	      depth, sblock);
    }

    CDBGV(PRINTHW,BASIC,"HwdescToStr(%d): returing description %s\n",
	  depth, sblock);

    CDBGFCALLEXIT;
    /* Return the string pointer */
    return sblock;
}

/* Routine to create a string of the process ranks in a communicator,
   relative to another, in the order that they occur in the first one.
   Process 0 in the communicator returns the string

   This routine can use grouptranslateranks
   Query: Can we use the same approach in HwdescPrintAll?

   Used by hwdescToStr
*/
static const char *hwdescGetRankList(MPI_Comm comm, MPI_Comm refcomm,
				     int depth, const char *desc)
{
    int crank;
    const char *str = 0;

    CDBGFCALLENTER;
    MPI_Comm_rank(comm, &crank);
    if (crank == 0) {
#if 0
	int range[3];
#else
	intlistPtr ilist;
#endif
	int *rranks, *cranks, csize;
	MPI_Group cgroup, rgroup;

	MPI_Comm_group(comm, &cgroup);
	MPI_Comm_group(refcomm, &rgroup);
	MPI_Comm_size(comm, &csize);
	cranks = (int *)malloc(csize*sizeof(int));
	rranks = (int *)malloc(csize*sizeof(int));
	if (!cranks || !rranks) {
	    BENVi_MallocErr("get rank list", csize, "int");
	}
	for (int i=0; i<csize; i++) cranks[i] = i;
	MPI_Group_translate_ranks(cgroup, csize, cranks, rgroup, rranks);
#if 0
	if (compressListToRange(csize, rranks, range) == 0) {
	    str = hwdescrange(depth, range, desc);
	}
	else {
	    str = hwdesclist(depth, csize, rranks, desc);
	}
#else
	ilist = BENV_UtilCompressIntList(csize, rranks, -1);
	str = BENV_UtilIntListToStr(ilist);
	fflush(stdout);
	str = BENV_StringApp(str, 2, " ", desc);
	if (depth)
	    str = indentstring(str, depth);
	BENV_UtilFreeIntList(ilist);
#endif
	free(rranks);
	free(cranks);
	MPI_Group_free(&rgroup);
	MPI_Group_free(&cgroup);
    }
    CDBGFCALLEXIT;
    return str;
}

/* Return a string (allocated with malloc) with description of entry.
   Used in BENV_HwdescPrintAll */
static const char *hwdescline(int indent, int csize, int wrank, int idx,
			      const char *descstr)
{
    int dlen = strlen(descstr), totlen, rc;
    char *str, *p;

    /* This length is an estimate -
       21 char + 2 ints + 15 char + int + descstr + null */
    totlen = 21 + 10 + 10 + 15 + 10 + dlen + 1;
    str = (char *)malloc(totlen*sizeof(char));
    if (!str) {
	BENVi_MallocErr("hwdescline", totlen, "char");
	return 0;
    }

    p = str;
    for (int i=0; i<indent; i++)
	*p++ = ' ';
    rc = snprintf(p, totlen-indent,
		  "comm(size=%d,wrank=%d,idx in parent=%d) %s\n",
		  csize, wrank, idx, descstr);
    if (rc < 0) return 0;

    return (const char *)str;
}


static int printmembers(FILE *fp, const int *match, int nmatch,
			const int *idxvals, const int *maxidx,
			const hwdescKind *kind, int nlevel, int csize);

/*@ BENV_HwdescPrintTuple - Print tuples describing the hardware heirarchy

Input Parameters:
+ fp - FILE pointer for output
. comm - Communicator for all processes. Call is collective over comm.
. hwc - HwdescCtx from 'BENV_HwdescGetDescFromMPI' or similar
- which - Indicates which output formats to print. This is a bit mask,
          so multiple outputs can be specified. See below

Notes:
This is a collective call over 'comm'. It prints lists (tuples) of ranks and/or
object indicesfor all processes, based on the contents of 'hwc'.
.n
.n 0x1 - For each object, print the rank of the leader processes
.n 0x2 - For each process, print the objidx for each of the objects (e.g., node,
.n       socket, NUMA, or core)
.n

  @*/
int BENV_HwdescPrintTuple(FILE *fp, MPI_Comm comm, hwdescCtx *hwc, int which)
{
#define MAX_LEVELS 16
    int csize, crank, nlevs[2];
    int idxs[MAX_LEVELS], maxidxs[MAX_LEVELS], *idxvals=0, i, nlevels;

    CDBGFCALLENTER;
    MPI_Comm_size(comm, &csize);
    MPI_Comm_rank(comm, &crank);

    /* Ensure all have the same level */
    nlevs[0] = hwc->nlevel;
    nlevs[1] = - nlevs[0];
    MPI_Allreduce(MPI_IN_PLACE, nlevs, 2, MPI_INT, MPI_MAX, comm);

    if (nlevs[0] != -nlevs[1]) {
	if (crank == 0) {
	    fprintf(stderr, "hwdescCtx does not have the same number of levels on all processes\n");
	    fprintf(stderr, "levels in [%d,%d]\n", -nlevs[1], nlevs[0]);
	    fflush(stderr);
	    return 1;
	}
    }
    nlevels = nlevs[0];

    /* Gather information about each level from the processes, including
       the maximum object index at each level */
    /* FIXME: In some examples, some tuples had 0,0,0,i (rank 0, 8, 16, 24, ...
       Is problme the following (e.g, rank 1 is 0,1,1,0) */
    for (i=0; i<nlevels; i++) {
	idxs[i] = hwc->objinfo[i].objidx;
    }
    if (crank == 0) {
	idxvals = (int *)malloc(nlevels*csize*sizeof(int));
	MPI_Gather(idxs, nlevels, MPI_INT, idxvals, nlevels, MPI_INT, 0, comm);
	MPI_Reduce(idxs, maxidxs, nlevels, MPI_INT, MPI_MAX, 0, comm);
    }
    else {
	MPI_Gather(idxs, nlevels, MPI_INT, 0, 0, 0, 0, comm);
	MPI_Reduce(idxs, 0, nlevels, MPI_INT, MPI_MAX, 0, comm);
    }

    /* Generate the output */
    if (crank == 0) {
	/* idxvals[3*crank..3*crank+nlevels-1] are the object indices
	   for the process with rank crank */
	/* First, just output the tuples for each rank: */
	if (which & 0x1) {
	    fputs("Assignment tuple for each rank:\n",fp);
	    for (i=0; i<csize; i++) {
		fprintf(fp, "%d: ", i);
		BENV_PrintIntList(fp, nlevels, idxvals+nlevels*i, 1);
	    }
	}

	/* Second, try printing tuples for each object */
	if (which & 0x2) {
	    fputs("Ranks by object:\n", fp);
	    hwdescKind kinds[MAX_LEVELS];
	    int match[MAX_LEVELS];
	    for (int lev=0; lev<nlevels; lev++)
		kinds[lev] = hwc->objinfo[lev].kind;
	    for (int obj=0; obj<maxidxs[0]+1; obj++) {
		match[0] = obj;
		printmembers(fp, match, 1, idxvals, maxidxs, kinds,
			     nlevels, csize);
	    }
	}
	free(idxvals);
    }
    CDBGFCALLEXIT;
    return 0;
}

/* Routine called recursively to print the ranks of processes in
   a hierarchy, based on the object tuples of all processes */
static int printmembers(FILE *fp, const int *match, int nmatch,
			const int *idxvals, const int *maxidxs,
			const hwdescKind *kind, int nlevels, int csize)
{
    int i, j, nlen, child, newmatch[MAX_LEVELS];
    intarrayPtr iarr;
    intlistPtr  ilst;
    const char *istr;

    /* Check all processes for a match to this object */
    iarr = BENV_UtilCreateIntArray(csize);
    for (i=0; i<csize; i++) {
	for (j=0; j<nmatch; j++) {
	    if (match[j] != idxvals[i*nlevels+j]) break;
	}
	if (j == nmatch) BENV_UtilAppendIntArray(iarr, i);
	/*fprintf(fp, "%d,", i);*/
    }
    /* Does this object have any matches? If not, we don't print anything
       and return without descending into children (there can't be any) */
    nlen = BENV_UtilIntArrayLen(iarr);
    if (nlen == 0) {
	BENV_UtilFreeIntArray(iarr);
	CDBGFCALLEXIT;
	return 0;
    }

    ilst = BENV_UtilIntArrayToIntList(iarr);
    istr = BENV_UtilIntListToStr(ilst);
    /* Print the object, indented based on level */
    for (i=0; i<nmatch-1; i++) fputc(' ', fp);
    fprintf(fp, "%s %d: %s\n", BENV_HwdescKindStr(kind[nmatch-1]),
	    match[nmatch-1], istr);
    /* Flush to avoid possible problems with process-labeled output with,
       for example, mpiexec -l */
    fflush(fp);
    free((void *)istr);
    BENV_UtilFreeIntArray(iarr);
    BENV_UtilFreeIntList(ilst);

    /* Are we at this leaf */
    /* Fixme: ignore leaf */
    if (nmatch == nlevels) return 0;

    /* descend into all child objects */
    for (j=0; j<nmatch; j++) newmatch[j] = match[j];
    for (child=0; child<maxidxs[nmatch]+1; child++) {
	newmatch[nmatch] = child;
	printmembers(fp, newmatch, nmatch+1, idxvals, maxidxs, kind, nlevels,
		     csize);
    }
    return 0;
}

/* Internal routine to prepend blanks to a string */
static char *indentstring(const char *str, int indent)
{
    size_t slen;
    char *p, *snew;
    const char *pold;

    slen = strlen(str);
    snew = (char *)malloc(slen + indent + 1);
    if (!snew) {return 0;}

    p = snew;
    for (int i=0; i<indent; i++) *p++ = ' ';
    pold = str;
    while (*pold) *p++ = *pold++;
    *p = 0;

    free((char *)str);
    return snew;
}
