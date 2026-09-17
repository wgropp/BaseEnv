/*
 * Copyright (C) by University of Illinois 2026
 */

#include "benvconf.h"

#if defined(HAVE_GETCPU) || defined(HAVE_SCHED_GETCPU)
/* This definition is needed for the getcpu and sched_getcpu options */
#define _GNU_SOURCE
#endif

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "mpi.h"
#include "benvutil.h"
#include "benvmpiutil.h"
#include "benvdbg.h"
#include "hwdescnew.h"
#include "hwdescimpl2.h"

CDBGFCALLDECL;
CDBGDECL(GETNODEINFO);
CDBGEDECL(ARGV);
CDBGEDECL(GETDESC);

/* Add info to objinfo entry (collinfo is unchanged) */
static void addnodeconfignew(int nodensock, int nodennuma, int nodencore,
			     int nsock, int nnuma, int ncore,
			     int rawnsock, int rawnnuma, int rawncore,
			     hwdescCtx *hwc, hwdescConfigSrc csrc);
void addfullobj(hwdescCtx *hwc, hwdescKind kind, int rawidx, int rawnobj,
		int idx, int nobj, int nodeidx, int nodenobj,
		hwdescConfigSrc csrc, hwdescAssignSrc asrc);

/* BENVi_HwdescNodeArgDebug - Look for debug options for hwdesc routinaes

This is an internal routine, called by 'BENV_HwdescArgDebug'

Input Parameters:
+ argc - Argument count
. argv - Argument vector
- prefix - Arguments have this prefix; may be null. See below

Input/Output Parameter:
. argcnt - pointer to the index of the current argument. Will be updated
 if an hwdesc parameter is found by the number of values read, not counting
 the argument itself.

Return Value:
Returns 1 if a know arghument value is found, zero otherwise.

Notes:
Recognizes two debug classes - 'getdesc' and 'printhw'. Recognizes
'-debugclass' as the argument name. Currently, the prefix is ignored.
The class may be followed with ':b', ':d', or ':a' for basic, detail, or all
debug information respectively.

See also:
BENV_DebugArgClass, BENV_DebugArgRank, BENV_HwdessArgDebug
  */
int BENVi_HwdescNodeArgDebug(int argc, char **argv, int *argcnt,
			     const char *prefix)
{
    int rc=0;
    static const char *classes[] = { "getnodeinfo", };
    static int *classval[] = { &cvar_benv_GETNODEINFO_verbose,
			       };

    CDBGV(ARGV,BASIC,"looking for debugclass in %s %s\n",
	  argv[*argcnt], argv[*argcnt+1]);

    /* FIXME: Ignore prefix or allow but not require? */
    rc = BENV_DebugArgClass(argc, argv, argcnt, 1, classes, classval);
    if (rc == -1) {
	/* DebugArgClass returns -1 if class not recognized. Ignore */
	rc = 0;
    }
    if (rc == 1) {
	CDBGV(ARGV,DETAIL,"New value of cvar is %d\n",
	      cvar_benv_GETNODEINFO_verbose);
    }
    return rc;
}

/* Specific methods to get configuration and assignment information */
#include <errno.h>

#ifdef HAVE_SYSCTLBYNAME
#include <sys/sysctl.h>

/*@ BENV_HwdescNodeConfigSysctl - Use sysctx to update hwdesc configuration

Input Parameters:
. context - Ignored in this routine

Input/Output Parameters
- hwc - An hwdescCtx. It may already contain some information about the job.
   See notes below.

.N returnvalue

Notes:
The configuration is added at the end of the 'hwnode' context. This makes it
easy to use MPI calls to get job and node information and then use this routine
(and similar ones) to add more detailed node information.
  @*/
int BENV_HwdescNodeConfigSysctl(void *context, hwdescCtx *hwc)
{
    int    rc, totalcores = -1, totalpackages=-1, mcores;
    size_t nlen=sizeof(int);

    CDBGFCALLENTER;
    /* Use machdep first, then hw */
    rc = sysctlbyname("machdep.cpu.core_count", &totalcores, &nlen, NULL, 0);
    if (rc) {
	fprintf(stderr, "machdep.cpu.core_count: %s\n", strerror(errno));
	rc = sysctlbyname("hw.physicalcpu", &totalcores, &nlen, NULL, 0);
    }

    /* determine the number of sockets (packages) */
    rc = sysctlbyname("machdep.cpu.cores_per_package", &mcores, &nlen, NULL, 0);
    if (rc) {
	fprintf(stderr, "machdep.cpu.cores_per_package: %s\n", strerror(errno));
	rc = sysctlbyname("hw.packages", &totalpackages, &nlen, NULL, 0);
	if (rc) {
	    fprintf(stderr, "hw.packages: %s\n", strerror(errno));
	}
    }
    else {
	totalpackages = totalcores / mcores;
    }

    rc = 1;
//    printf("found %d total cores and %d total packages\n", totalcores, totalpackages);
    fflush(stdout);
    if (totalcores > 0 && totalpackages > 0) {
	/* Add to hwc */
	if (hwc->nlevel + 2 > hwc->nAllocated) {
	    fprintf(stderr, "Out of levels in hwdesc (%d allocated)\n",
		    hwc->nAllocated);
	    rc = -1;
	}
	else {
	    /* Note collinfo is unset */
	    addnodeconfignew(totalpackages, -1, totalcores,
			     totalpackages, -1, totalcores / totalpackages,
			     totalpackages, -1, totalcores,
			     hwc, BENV_HWDESC_CONFIG_SYSCTL);
	    rc = 0;
	}
    }
    CDBGFCALLEXIT;
    return rc;
}
#endif

/*@ BENV_HwdescNodeConfigEnv - Use environment variables to update hwdesc configuration

Input Parameters:
. context - Ignored in this routine

Input/Output Parameters
- hwc - An hwdescCtx. It may already contain some information about the job.
   See notes below.

.N returnvalue

Notes:
The configuration is added at the end of the 'hwnode' context. This makes it
easy to use MPI calls to get job and node information and then use this routine
(and similar ones) to add more detailed node information.

Environment variables that define the configuration\:
.n
.n TOPO_SOCKETSPERNODE - Number of sockets per node
.n TOPO_CHIPSPERNODE - Alternate name for sockets per node
.n TOPO_NUMASPERCHIP - Number of NUMA regions per socket
.n TOPO_CORESPERCHIP - Number of cores per socket (not per NUMA region)

  @*/
int BENV_HwdescNodeConfigEnv(void *context, hwdescCtx *hwc)
{
    int nsocket=1, /* Default is one socket */
        ncore=-1,  /* Default is unknown number of cores */
	nnuma=-1;  /* Default is unknown number of NUMA regions */
    int rc;

    CDBGFCALLENTER;
    /* Get external configuration */
    if (BENV_GetIntFromEnv("TOPO_SOCKETSPERNODE", &nsocket) != 0) {
        BENV_GetIntFromEnv("TOPO_CHIPSPERNODE", &nsocket);
    }
    BENV_GetIntFromEnv("TOPO_CORESPERCHIP", &ncore);
    BENV_GetIntFromEnv("TOPO_NUMASPERCHIP", &nnuma);

    CDBGV(GETNODEINFO,DETAIL,"getNsocketNcore: sockets=%d, numa=%d, cores=%d\n",
	  nsocket, nnuma, ncore);
    rc = 1;

    if (ncore > 0 && nsocket > 0) {
//	printf("NodeConfigEnv: ncore =%d nsocket=%d\n", ncore, nsocket);
	/* Add to hwc. Allow room for a NUMA region */
	if (hwc->nlevel + 3 > hwc->nAllocated) {
	    fprintf(stderr, "Out of levels in hwdesc (%d allocated)\n",
		    hwc->nAllocated);
	    rc = -1;
	}
	else {
	    /* Note collinfo is unset */
	    int ncoretonuma=ncore, nodenuma=-1;
	    if (nnuma > 0) {
		ncoretonuma = ncore/nnuma;
		nodenuma    = nnuma * nsocket;
	    }
	    /* If nnuma is -1, this will NOT add a NUMA region */
	    addnodeconfignew(nsocket, nodenuma,  nsocket*ncore,   /* Node */
			     nsocket, nnuma, ncoretonuma,         /* Parent */
			     nsocket, nnuma, ncore,               /* Raw */
			     hwc, BENV_HWDESC_CONFIG_ENV);
	    rc = 0;
	}
    }
    CDBGFCALLEXIT;
    return rc;
}

#ifdef HAVE_HWLOC
#include "hwloc.h"
static hwloc_topology_t hwtopology;
static int hwloc_topo_init=0;
void addobj(hwdescCtx *hwc, hwdescKind kind, int osidx, int idx, int nobj,
	    hwdescConfigSrc csrc, hwdescAssignSrc asrc);
int BENVi_HwdescNodeHwlocSiblingCount(hwloc_obj_t obj);
int BENVi_HwdescNodeHwlocCousinCount(hwloc_obj_t obj);
int BENVi_HwdescNodeHwlocCousinIdxCheck(hwloc_obj_t obj);

/*@ BENV_HwdescNodeConfigHwloc - Use hwloc to update hwdesc configuration

Input Parameters:
. context - Ignored in this routine

Input/Output Parameters
- hwc - An hwdescCtx. It may already contain some information about the job.
   See notes below.

.N returnvalue

Notes:
The configuration is added at the end of the 'hwnode' context. This makes it
easy to use MPI calls to get job and node information and then use this routine
(and similar ones) to add more detailed node information.

If 'sched_getcpu' is available, this routine will also use hwloc to determine
assignment information. This is important, because the number of objects
determined by hwloc may represent what is allocated, e.g., by SLURM, rather
than the physical number of objects such as cores. As a result, it may not be
possible to determine the assignment to a socket based on the core number
provided by hwloc.
  @*/
int BENV_HwdescNodeConfigHwloc(void *context, hwdescCtx *hwc)
{
    int depth, nobj, rc=1, isset=0;
    int cpu = 0, hasAssignInfo=0;
    hwloc_obj_t obj, sockobj, coreobj, groupobj, numaobj;
    /* We need to keep track of number in parent and number on node.
       Number in parent is relative to the parent that is included
       in the hwdesc, and that might include multiple hwloc elements,
       such as several levels of cache */
    int nodencore=0, ncore=0, nnuma=0, nsocket=0;
    int ninparent;  /* Number of elements in hwloc elements not represented
		       in hwdesc (e.g., L2Cache. Used to compute number in
		       parent. */

    CDBGFCALLENTER;

    if (!hwloc_topo_init) {
	hwloc_topology_init(&hwtopology);
	hwloc_topology_load(hwtopology);
	hwloc_topo_init = 1;
    }

/* FIXME: An option is to pass the physical CPU as part of the context,
   which removes a dependency on sched_getcpu */
#ifdef HAVE_SCHED_GETCPU
    cpu = sched_getcpu(); // where *this thread* is right now
    CDBGV(GETNODEINFO,DETAIL,"OS cpu=%d\n", cpu);
    /* Hwloc routines need an hwloc logical index, not a CPU physical
       index. Sometimes these are the same, which is why some erroneous
       examples on the internet don't make this translation */
    cpu = BENVi_HwdescNodeHwlocCPUidToLogical(cpu);
    /* A logical index of 0 seems to work at least for configuration
       information */
    if (cpu < 0)
	cpu = 0;
    else {
	hasAssignInfo = 1; // Can use hwloc to get the assignment information
	CDBGV(GETNODEINFO,DETAIL,"Logical cpu=%d\n", cpu);
    }
#endif

    CDBGCMD(GETNODEINFO,ALL,BENVi_HwdescNodeHwlocPrintInfo(stdout, cpu));

    /* We need to walk up the hierarchy from our CPU object to get the
       number of elements in the parent (nobj) as well as the number in the
       node. */
    sockobj = coreobj = groupobj = 0;
    obj = hwloc_get_obj_by_type(hwtopology, HWLOC_OBJ_PU, cpu);
    ninparent = 1;
    while (obj) {
	const char *typename = hwloc_obj_type_string(obj->type);
	int nsib = BENVi_HwdescNodeHwlocSiblingCount(obj);
	int ncousin = BENVi_HwdescNodeHwlocCousinCount(obj);
	//nobj = hwloc_get_nbobjs_by_type(hwtopology, obj->type);
	CDBGV(GETNODEINFO,DETAIL,
	      "%s osindex=%d, logicalindex=%d, nsib=%d, ncousin=%d\n",
	      typename, obj->os_index, obj->logical_index, nsib, ncousin);

	// TIXME: TEMP: REMOVE WHEN DEBUGGED
	//printf("Number of siblings =%d, cousins=%d, for object %s\n",
	//       nsib, ncousin, hwloc_obj_type_string(obj->type));
	switch (obj->type) {
	case HWLOC_OBJ_SOCKET: sockobj = obj;
	    nsocket = nsib;
	    if (!groupobj) ncore = ninparent;
	    ninparent = 1;
	    //printf("in socket, ncore = %d, nsocket = %d\n", ncore, nsocket);
	    //trackingchild = -1;
	    break;
	case HWLOC_OBJ_CORE: coreobj = obj;
	    ninparent = nsib;
	    ncore     = nsib;
	    nodencore = ncousin;
	    //trackingchild = 0;
	    break;
	case HWLOC_OBJ_GROUP: groupobj = obj;
	    ncore = ninparent;
	    nnuma = nsib;
	    //printf("set ncore to %d and nnuma to %d\n", ncore, nnuma);
	    ninparent = nsib;
	    //trackingchild = 1;
	    break;
	default:
	    if (nsib > 1)
		ninparent *= nsib;
	}
	obj = obj->parent;
    }

    numaobj = 0;
    if (groupobj) {
	/* Try to find NUMA region */
	int gnum = hwloc_get_nbobjs_by_type(hwtopology, groupobj->type);
	//printf("Group count is %d\n", gnum);
    	numaobj = hwloc_get_obj_by_type(hwtopology, HWLOC_OBJ_NUMANODE,
					groupobj->logical_index);
#if 0
	if (!numaobj) printf("Did not find numaobj from groupobj (%d)\n",
			     groupobj->logical_index);
	else {
	    printf("NUMA os_index=%d\n", numaobj->os_index);
	}
#endif
    }

    /* The logical index is not (directly) related to the os index,
       so the logical number should not be used as an index for hwdesc */
    /* Add: raw, ralparent, relnode */
    if (sockobj) {
	// BENVi_HwdescNodeHwlocCousinIdxCheck(sockobj);
	nobj = hwloc_get_nbobjs_by_type(hwtopology, sockobj->type);
	isset=1;
	if (hasAssignInfo) {
	    /* Note that the os_index is not necessarily the socket
	       number (on DeltaAI, os_index was 2384 for a dual socket
	       Grace-Grace node */
	    addfullobj(hwc, BENV_HWDESC_SOCKET, sockobj->os_index, nobj,
		       -1, nobj, -1, nobj,
		       BENV_HWDESC_CONFIG_HWLOC, BENV_HWDESC_ASSIGN_HWLOC);
	}
	else {
	    addfullobj(hwc, BENV_HWDESC_SOCKET, -1, nobj,
		       -1, nobj, -1, nobj,
		       BENV_HWDESC_CONFIG_HWLOC, BENV_HWDESC_ASSIGN_UNKNOWN);
	}
    }
    if (numaobj) {
	// BENVi_HwdescNodeHwlocCousinIdxCheck(numaobj);
	/* NUMA is from NUMANODE, so number is relative to node, not socket */
	nobj = hwloc_get_nbobjs_by_type(hwtopology, numaobj->type);
	isset=1;
	if (hasAssignInfo) {
	    int pnuma = numaobj->os_index;
	    if (nnuma <= 0) {
		printf("Unexpected values for nnuma=%d, nsocket=%d\n",
		       nnuma, nsocket);
		printf("type = %d(%s), ", numaobj->type,
			hwloc_obj_type_string(numaobj->type));
	    }
	    else {
		pnuma = pnuma % nnuma;
		//printf("NUMA: nobj=%d, nnuma=%d\n", nobj, nnuma);
		addfullobj(hwc, BENV_HWDESC_NUMA, numaobj->os_index, nobj,
			   pnuma, nnuma, numaobj->os_index, nobj,
			   BENV_HWDESC_CONFIG_HWLOC, BENV_HWDESC_ASSIGN_HWLOC);
	    }
	}
	else {
	    addfullobj(hwc, BENV_HWDESC_NUMA, -1, nobj,
		       -1, nnuma, -1, nobj,
		       BENV_HWDESC_CONFIG_HWLOC, BENV_HWDESC_ASSIGN_UNKNOWN);
	}
	//printf("Setting numa in hwdesc with nobj=%d and nnuma=%d\n", nobj, nnuma);
    }
    if (coreobj) {
	/* hwloc doesn't provide an os_index over the node for core -
	   at least in some tests, it is over just the socket */
	// BENVi_HwdescNodeHwlocCousinIdxCheck(coreobj);
	/* Core is relative to parent, which may be a NUMA group */
	nobj = hwloc_get_nbobjs_by_type(hwtopology, coreobj->type);
	isset=1;
	/* nobj is almost certainly the same as nodencore */
	if (hasAssignInfo) {
	    /* Get cpu id relative to parent */
	    int pcpu = cpu;
	    if (ncore > 0) {
		pcpu = cpu % ncore;
	    }
	    else if (nodencore > 0 && nsocket > 0) {
		pcpu = cpu % (nodencore / nsocket);
	    }
	    else
		printf("Unexpected values for nnuma=%d, nsocket=%d, nodencore=%d\n",
		       nnuma, nsocket, nodencore);
	    addfullobj(hwc, BENV_HWDESC_CORE, cpu, nobj,
		       pcpu, ncore, cpu, nodencore,
		       BENV_HWDESC_CONFIG_HWLOC, BENV_HWDESC_ASSIGN_SCHEDGETCPU);
	}
	else {
	    addfullobj(hwc, BENV_HWDESC_CORE, -1, nobj,
		       -1, ncore, -1, nodencore,
		       BENV_HWDESC_CONFIG_HWLOC, BENV_HWDESC_ASSIGN_UNKNOWN);
	}
    }
    if (isset) {
	if (hwc->source) {
	    hwc->source = BENV_StringApp(hwc->source, 1, "; hwloc");
	}
	else {
	    hwc->source = strdup("hwloc");
	}
    }
    rc = 0;
    CDBGFCALLEXIT;
    return rc;
}

/* This routine can help diagnose issues with using hwloc. It outputs to fp
   the hardware hierarchy between a processing unit (or core) and the
   package/socket.
   cpu is the *logical* index of the CPU, as defined by HWLOC. This is not
   the same as the cpu value returned by an OS routine like sched_getcpu
*/
/* Used the current topology */
void BENVi_HwdescNodeHwlocPrintInfo(FILE *fp, int cpu)
{
    hwloc_obj_t obj,  sock;

    if (!hwloc_topo_init) {
	hwloc_topology_init(&hwtopology);
	hwloc_topology_load(hwtopology);
	hwloc_topo_init = 1;
    }

    /* Get a object from which to start. According to the documentation,
       there is always a PU entry, but this is not true on Delta, so
       look for a core if PU is not available */
    obj = hwloc_get_obj_by_type(hwtopology, HWLOC_OBJ_PU, cpu);
    if (!obj)
	obj  = hwloc_get_obj_by_type(hwtopology, HWLOC_OBJ_CORE, cpu);
    if (!obj) {
	fprintf(stderr, "Cannot find PU or Core in hwloc topology for cpu %d!\n", cpu);
	return;
    }

    fprintf(fp, "HWLOC for core %d\n", cpu);
    fprintf(fp, "Type for HWLOC_OBJ_NUMANODE = %d\n", HWLOC_OBJ_NUMANODE);

    // Walk upward to find socket/package.
    sock = obj;
    while (sock && sock->type != HWLOC_OBJ_SOCKET && sock->parent) {
	fprintf(fp, "type = %d(%s), ", sock->type, hwloc_obj_type_string(sock->type));
	if (sock->name) fprintf(fp, "name = %s, ", sock->name);
	fprintf(fp, "os_index = %d, logical_index = %d, #objs=%d\n",
		sock->os_index, sock->logical_index,
		hwloc_get_nbobjs_by_type(hwtopology,sock->type));
	int nsib = BENVi_HwdescNodeHwlocSiblingCount(sock);
	int nbobj = hwloc_get_nbobjs_by_type(hwtopology, sock->type);
	fprintf(fp, "Nsiblings = %d, nobjs by type = %d\n", nsib, nbobj);

	sock = sock->parent;
    }

    if (sock && sock->type == HWLOC_OBJ_SOCKET) {
	fprintf(fp, " -> SOCKET id=%u\n", sock->logical_index);
	fprintf(fp, "type = %d(%s), ", sock->type,
		hwloc_obj_type_string(sock->type));
	if (sock->name) fprintf(fp, "name = %s, ", sock->name);
	fprintf(fp, "os_index = %d, logical_index = %d, #objs=%d\n",
		sock->os_index, sock->logical_index,
		hwloc_get_nbobjs_by_type(hwtopology,sock->type));
    } else {
	fprintf(fp, " -> (no socket found)\n");
    }

    int    depth = hwloc_get_type_depth(hwtopology, HWLOC_OBJ_CORE);
    if (depth == HWLOC_TYPE_DEPTH_UNKNOWN) {
	fprintf(fp, "Could not find core\n");
    }
    else {
	int ncore = hwloc_get_nbobjs_by_depth(hwtopology, depth);
	fprintf(fp, "ncore from depth = %d\n", ncore);
    }
    depth = hwloc_get_type_depth(hwtopology, HWLOC_OBJ_NUMANODE);
    if (depth == HWLOC_TYPE_DEPTH_UNKNOWN) {
	fprintf(fp, "Could not find numa\n");
    }
    else {
	int nnuma = hwloc_get_nbobjs_by_depth(hwtopology, depth);
	fprintf(fp, "nnuma from depth = %d\n", nnuma);
    }
}

int BENVi_HwdescNodeHwlocCPUidToLogical(int cpu)
{
    int k=0;
    for (hwloc_obj_t pu = hwloc_get_obj_by_type(hwtopology, HWLOC_OBJ_PU, 0);
         pu != NULL;
         pu = hwloc_get_next_obj_by_type(hwtopology, HWLOC_OBJ_PU, pu))
    {
        if ((unsigned)pu->os_index == cpu) {
	    if (pu->logical_index >= 0)
		return pu->logical_index;
	    else
		return k;
	}
	k++;
    }
    return -1;
}
void addobj(hwdescCtx *hwc, hwdescKind kind, int osidx, int idx, int nobj,
	    hwdescConfigSrc csrc, hwdescAssignSrc asrc)
{
    CDBGFCALLENTERV("osidx=%d, idx=%d, nobj=%d\n", osidx, idx, nobj);
    hwc->objinfo[hwc->nlevel].nodenobj   = nobj;
    hwc->objinfo[hwc->nlevel].nodeobjidx = osidx;
    hwc->objinfo[hwc->nlevel].kind       = kind;
    hwc->objinfo[hwc->nlevel].csrc       = csrc;
    if (idx >= 0) {
	hwc->objinfo[hwc->nlevel].nobj   = nobj;
	hwc->objinfo[hwc->nlevel].objidx = idx;
	hwc->objinfo[hwc->nlevel].asrc   = asrc;
    }
    hwc->nlevel++;
    CDBGFCALLEXIT;
}
void addfullobj(hwdescCtx *hwc, hwdescKind kind, int rawidx, int rawnobj,
		int idx, int nobj, int nodeidx, int nodenobj,
		hwdescConfigSrc csrc, hwdescAssignSrc asrc)
{
    int n=hwc->nlevel;
    CDBGFCALLENTERV("raw(idx=%d,n=%d), idx=%d, nobj=%d, node(idx=%d,n=%d)\n",
		    rawidx, rawnobj, idx, nobj, nodeidx, nodenobj);
    hwc->objinfo[n].nodenobj   = nodenobj;
    hwc->objinfo[n].nobj       = nobj;
    hwc->objinfo[n].rawnobj    = rawnobj;
    hwc->objinfo[n].kind       = kind;
    hwc->objinfo[n].csrc       = csrc;
    if (rawidx >= 0) {
	hwc->objinfo[n].nodeobjidx = nodeidx;
	hwc->objinfo[n].rawobjidx  = rawidx;
    }
    if (idx >= 0) {
 	hwc->objinfo[n].objidx = idx;
    }
    if (idx >= 0 || rawidx >= 0 || nodeidx >= 0)
	hwc->objinfo[n].asrc   = asrc;
    hwc->nlevel++;
    CDBGFCALLEXIT;
}
#if 0
// Here are the fields in hwdesc to set:
    int nobj,         /* Number of objects with the same parent */
	objidx;       /* Number of this object in [0,nobj) */
    /* The next two only apply to objects within a node, such as sockets,
       NUMA domains, or cores. Ignored if not within a node. */
    int nodenobj,     /* Number of objects on the same node */
	nodeobjidx;   /* Number of this object in [0,nodeobjidx) */
    int rawnobj,      /* Raw value provided by csrc */
	rawobjidx;    /* Raw value provided by asrc */
#endif
int BENVi_HwdescNodeHwlocSiblingCount(hwloc_obj_t obj)
{
    hwloc_obj_t nobj;
    int sibcount = 1;
    nobj = obj->next_sibling;
    while (nobj) {
	sibcount++;
	nobj = nobj->next_sibling;
    }
    nobj = obj->prev_sibling;
    while (nobj) {
	sibcount++;
	nobj = nobj->prev_sibling;
    }
    return sibcount;
}
// Temp routine to look at os_index
int BENVi_HwdescNodeHwlocCousinIdxCheck(hwloc_obj_t obj)
{
    hwloc_obj_t nobj;
    int cousincount = BENVi_HwdescNodeHwlocCousinCount(obj);
    int *flags=0;
    int rc = 0;

    flags = calloc(cousincount,sizeof(int));

    if (obj->os_index >= 0 && obj->os_index < cousincount)
	flags[obj->os_index]++;
    else {
	fprintf(stderr, "Cousin os_index=%d, not in [0,%d)\n",
		obj->os_index, cousincount);
	rc++;
    }

    nobj = obj->next_cousin;
    while (nobj) {
	if (nobj->os_index >= 0 && nobj->os_index < cousincount)
	    flags[nobj->os_index]++;
	else {
	    fprintf(stderr, "Cousin os_index=%d, not in [0,%d)\n",
		    nobj->os_index, cousincount);
	    rc++;
	}
	nobj = nobj->next_cousin;
    }
    nobj = obj->prev_cousin;
    while (nobj) {
	if (nobj->os_index >= 0 && nobj->os_index < cousincount)
	    flags[nobj->os_index]++;
	else {
	    fprintf(stderr, "Cousin os_index=%d, not in [0,%d)\n",
		    nobj->os_index, cousincount);
	    rc++;
	}
	nobj = nobj->prev_cousin;
    }
    for (int i=0; i<cousincount; i++) {
	if (flags[i] != 1) {
	    fprintf(stderr, "flags[%d] = %d\n", i, flags[i]);
	    rc++;
	}
    }
    return rc;
}

int BENVi_HwdescNodeHwlocCousinCount(hwloc_obj_t obj)
{
    hwloc_obj_t nobj;
    int cousincount = 1;
    nobj = obj->next_cousin;
    while (nobj) {
	cousincount++;
	nobj = nobj->next_cousin;
    }
    nobj = obj->prev_cousin;
    while (nobj) {
	cousincount++;
	nobj = nobj->prev_cousin;
    }
    return cousincount;
}
#endif /* HWLOC */

#ifdef HAVE_NUMA_H
#include <numa.h>
static int numa_initialized=0;
/* Should check for HAVE_NUMA_NUM_CONFIGURED_NODES */
/* FIXME: Add as an option in NodeInfo, especially if no numa information
   available */
int BENV_HwdescNodeConfigNumainfo(void *context, hwdescCtx *hwc)
{
    int totalnuma=-1, rc, i, j;

    CDBGFCALLENTER;
    if (numa_initialized == 0) {
	rc = numa_available();
	if (rc == -1) {
	    numa_initialized = -1;
	    return 1;
	}
    }
    else if (numa_initialized < 0) return 1;

    totalnuma = numa_num_configured_nodes();
    /* This is the absolute number of numa "nodes" - need to divide
       by the number of sockets, assuming each as the same number */
    if (totalnuma <= 0) return 1;

    /* Find the location for this information (between core and socket) */
    for (i=0; i<hwc->nlevel; i++) {
	if (hwc->objinfo[i].kind == BENV_HWDESC_NUMA) {
	    /* Already have an entry; check if count is available */
	    if (hwc->objinfo[i].nodenobj <= 0) {
		break;
	    }
	    else return 1;
	}
	else if (hwc->objinfo[i].kind == BENV_HWDESC_CORE) {
	    /* Found core but no numa. Insert space for a numa record */
	    /* FIXME: Need to check for enough available levels */
	    for (j=hwc->nlevel; j>=i; j--) {
		hwc->objinfo[j+1]  = hwc->objinfo[j];
		hwc->collinfo[j+1] = hwc->collinfo[j];
	    }
	    hwc->nlevel++;
	    break;
	}
    }
    /* Add NUMA regions. Note that the CORE (i+1 entry) must be updated
       to have the correct cores/parent, and compute numa relative to
       parent (i-1 entry). */
    hwc->objinfo[i].nodenobj   = totalnuma;
    hwc->objinfo[i].rawnobj    = totalnuma;
    hwc->objinfo[i].nobj       = totalnuma / hwc->objinfo[i-1].nobj;
    hwc->objinfo[i].nodeobjidx = -1;
    hwc->objinfo[i].objidx     = -1;
    hwc->objinfo[i].rawobjidx  = -1;
    hwc->objinfo[i].kind       = BENV_HWDESC_NUMA;
    hwc->objinfo[i].csrc       = BENV_HWDESC_CONFIG_NUMANUMNODE;

    /* Update the cores/parent */
    hwc->objinfo[i+1].nobj = hwc->objinfo[i+1].nobj / hwc->objinfo[i].nobj;

    /* Clear the collinfo (FIXME: Should be init routine) */
    // FIXME: Init should have done this, and we should use the MPIinit
    // routine for this.  Mostly, need objcomm == MPI_COMM_NULL;
    hwc->collinfo[i].objcomm         = MPI_COMM_NULL;
    hwc->collinfo[i].leadersInParent = 0;
    hwc->collinfo[i].nSiblings       = -1;
    hwc->collinfo[i].siblingNum      = -1;
    hwc->collinfo[i].allsizeone      = 0;
    hwc->collinfo[i].descstr         = 0;
    /* FIXME: entry i+1 is now out-of-date and needs to be fixed. Maybe
       generate an error if any are set */

    CDBGFCALLEXIT;
    return 0;
}
#endif

#ifdef HAVE_PROC_CPUINFO
/*@ BENV_HwdescNodeConfigCpuinfo - Use proc/cpuinfo to update hwdesc configuration

Input Parameters:
. context - Ignored in this routine

Input/Output Parameters
- hwc - An hwdescCtx. It may already contain some information about the job.
   See notes below.

.N returnvalue

Notes:
The configuration is added at the end of the 'hwnode' context. This makes it
easy to use MPI calls to get job and node information and then use this routine
(and similar ones) to add more detailed node information.

NOT IMPLEMENTED
  @*/
int BENV_HwdescNodeConfigCpuinfo(void *context, hwdescCtx *hwc)
{
    return -1;
}
#endif

int BENV_HwdescNodeConfigParm(hwdescParms *parms, hwdescCtx *hwc)
{
    int nsocket=1, /* Default is one socket */
	nnuma=1,   /* Default is one numa */
        ncore=-1;  /* Default is unknown number of cores */
    int rc;

    CDBGFCALLENTER;
    /* Get values from parms */
    nsocket = parms->nobjs[1];
    nnuma   = parms->nobjs[2];
    ncore   = parms->nobjs[3];

    CDBGV(GETNODEINFO,DETAIL,"getNsocketNcore: sockets=%d, numas=%d, cores=%d\n",
	  nsocket, nnuma, ncore);
    rc = 1;
    /* FIXME: replace with common routine */
    if (ncore > 0 && nsocket > 0) {
	/* Add to hwc */
	if (hwc->nlevel + 3 > hwc->nAllocated) {
	    fprintf(stderr, "Out of levels in hwdesc (%d allocated)\n",
		    hwc->nAllocated);
	    rc = 1;
	}
	else {
	    /* Note collinfo is unset */
	    int corepernuma = ncore;
	    addnodeconfignew(nsocket, nnuma*nsocket, ncore*nnuma*nsocket,
			     nsocket,  nnuma, ncore, // relative to parent
			     nsocket, nnuma, corepernuma,
			     hwc, BENV_HWDESC_CONFIG_GIVEN);
	    rc = 0;
	}
    }
    CDBGFCALLEXIT;
    return rc;
}

/*@
  BENV_HwdescNodeGetConfig - Determine the hardware configuration of a node

Input Parameters:
. flags - Flags that control which methods may be used to determine the
 output node configuration. See below.

Input/Output Parameters:
. hwnode - Description of a hardware level

.N returnvalue

Notes:
If hwnode already has any elements defined ('hwnode->nlevel > 0'), this
routine appends the node configuration to the end of that list.

The known flags follow. Note that while a flag might be defined, it can
only be used if the relevant routine or service is available. This is
determined at the time when Base Env is configured.

.n
.n BENV_HWDESC_CONFIG_HWLOC - Use the hwloc package
.n BENV_HWDESC_CONFIG_SYSCTL - Use the sysctl routine
.n BENV_HWDESC_CONFIG_CPUINFO - Use the cpuinfo routine
.n BENV_HWDESC_CONFIG_NUMANUMNODE - Use numa routines from numa.h
.n BENV_HWDESC_CONFIG_ENV - Use information provided through environment variables
.n BENV_HWDESC_CONFIG_ALL - Use any available method
.n
  @*/
int BENV_HwdescNodeGetConfig(int flags, hwdescCtx *hwnode)
{
    int rc=-1;
    CDBGFCALLENTER;
#ifdef HAVE_HWLOC
    if (rc < 0 && (flags & BENV_HWDESC_CONFIG_HWLOC)) {
	rc = BENV_HwdescNodeConfigHwloc(0, hwnode);
    }
#endif
#ifdef HAVE_SYSCTLBYNAME
    if (rc < 0 && (flags & BENV_HWDESC_CONFIG_SYSCTL)) {
	rc = BENV_HwdescNodeConfigSysctl(0, hwnode);
    }
#endif
#ifdef HAVE_PROC_CPUINFO
    if (rc < 0 && (flags & BENV_HWDESC_CONFIG_CPUINFO)) {
	rc = BENV_HwdescNodeConfigCpuinfo(0, hwnode);
    }
#endif
#ifdef HAVE_HUMA_H
    if (rc < 0 && (flags & BENV_HWDESC_CONFIG_NUMANUMNODE)) {
	rc = BENV_HwdescNodeConfigNumainfo(v0, hwnode);
    }
#endif

    if (rc < 0 && (flags & BENV_HWDESC_CONFIG_ENV)) {
	rc = BENV_HwdescNodeConfigEnv(0, hwnode);
    }
    // FIXME: Compute relative object numbers?
    CDBGFCALLEXIT;
    return rc;
}


#ifdef HAVE_GETCPU
#include <sched.h>
/*@ BENV_HwdescNodeInfoGetcpu - Use getcpu to update hwdesc information about
   the calling process

Input Parameter:
. parms - Ignored in this routine

Input/Output Parameter:
. hwc - An hwdescCtx. It may already contain some information about the job.
   See notes below.

.N returnvalue

Notes:
Adds information about the node resources (e.g., socket, NUMA, or core)
used by the process. There must already be configuration information for
those resources in the 'hwc' (see BENV_HwdescNodeConfigXXX routines).
Information is added to the 'objinfo' fields.

Only sets values is the assignment source is 'BENV_HWDESC_ASSIGN_UNKNOWN'.
  @*/
int BENV_HwdescNodeInfoGetcpu(hwdescParms *parms, hwdescCtx *hwc)
{
    int rc, isset=0;
    unsigned int cpunum, numanum;

    CDBGFCALLENTER;
    rc = getcpu(&cpunum, &numanum);
    CDBGV(GETNODEINFO,DETAIL,"Using getcpu: cpu # = %d\n", cpunum);

    if (rc == 0) {
	for (int i=0; i<hwc->nlevel; i++) {
	    if (hwc->objinfo[i].asrc != BENV_HWDESC_ASSIGN_UNKNOWN) continue;
	    switch (hwc->objinfo[i].kind) {
	    case BENV_HWDESC_NUMA:
		hwc->objinfo[i].nodeobjidx = numanum;
		hwc->objinfo[i].rawobjidx  = numanum;
		hwc->objinfo[i].asrc       = BENV_HWDESC_ASSIGN_GETCPU;
		isset = 1;
		break;
	    case BENV_HWDESC_CORE:
		hwc->objinfo[i].nodeobjidx = cpunum;
		hwc->objinfo[i].rawobjidx  = cpunum;
		hwc->objinfo[i].asrc       = BENV_HWDESC_ASSIGN_GETCPU;
		isset = 1;
		break;
	    default:
	    }
	}
	if (isset) {
	    if (hwc->source) {
		hwc->source = BENV_StringApp(hwc->source, 1, "; getcpu");
	    }
	    else {
		hwc->source = strdup("getcpu");
	    }
	}

    }

    CDBGFCALLEXIT;
    return rc;
}
#endif

#ifdef HAVE_SCHED_GETCPU
#ifndef HAVE_GETCPU
#include <sched.h>
#endif
/* <numa.h> included above */

/*@ BENV_HwdescNodeInfoSchedgetcpu - Use sched_getcpu to update hwdesc information about
   the calling process

Input Parameter:
. parms - Ignored in this routine

Input/Output Parameters:
. hwc - An hwdescCtx. It may already contain some information about the job.
   See notes below.

.N returnvalue

Notes:
Adds information about the node resources (e.g., socket, NUMA, or core)
used by the process. There must already be configuration information for
those resources in the 'hwc' (see BENV_HwdescNodeConfigXXX routines).
Information is added to the objinfo fields.

Only sets values is the assignment source is 'BENV_HWDESC_ASSIGN_UNKNOWN'.
  @*/
int BENV_HwdescNodeInfoSchedgetcpu(hwdescParms *parms, hwdescCtx *hwc)
{
    int i, cpunum, rc = 1, numanum = -1, isset=0;

    CDBGFCALLENTER;
    cpunum = sched_getcpu();
    CDBGV(GETNODEINFO,DETAIL,"Using sched_getcpu: cpu # = %d\n", cpunum);

#ifdef HAVE_NUMA_NODE_OF_CPU
    rc = 0;
    if (numa_initialized == 0) {
	rc = numa_available();
	if (rc == -1) {
	    fprintf(stderr, "numa_available returned rc=-1\n");
	    numa_initialized = -1;
	}
    }
    else if (numa_initialized < 0) rc =-1;
    if (rc == 0) {
	numanum = numa_node_of_cpu(cpunum);
	CDBGV(GETNODEINFO,DETAIL,"Using numa_node_of_cpu: numa # = %d\n",
	      numanum);
    }
#endif

    if (cpunum >= 0) {
	for (i=0; i<hwc->nlevel; i++) {
	    if (hwc->objinfo[i].asrc != BENV_HWDESC_ASSIGN_UNKNOWN) continue;
	    if (hwc->objinfo[i].kind == BENV_HWDESC_CORE) {
		hwc->objinfo[i].nodeobjidx = cpunum;
		hwc->objinfo[i].rawobjidx  = cpunum;
		hwc->objinfo[i].asrc = BENV_HWDESC_ASSIGN_SCHEDGETCPU;
		isset = 1;
		break;
	    }
	    if (hwc->objinfo[i].kind == BENV_HWDESC_NUMA) {
		hwc->objinfo[i].nodeobjidx = numanum;
		hwc->objinfo[i].rawobjidx  = numanum;
		hwc->objinfo[i].asrc = BENV_HWDESC_ASSIGN_NUMAGETNODE;
		isset = 1;
		/* NUMA ahead of core in list, so continue */
	    }
	}
	if (i >= hwc->nlevel && !isset) {
	    /* Error! Did not find core */
	    fprintf(stderr, "Did not find core in hwdesc! i=%d of %d, isset=%d\n",
		    i, hwc->nlevel, isset);
	    rc = -1;
	}
	else if (isset) {
	    rc = 0;
	    if (hwc->source) {
		hwc->source = BENV_StringApp(hwc->source, 1, "; sched_getcpu");
	    }
	    else {
		hwc->source = strdup("sched_getcpu");
	    }
	}
    }
    CDBGFCALLEXIT;
    return rc;
}
#endif

/* Given a rank and a policy, fill out the node info.
   Policy assumes just socket mapping?

   Input:
   policy - string, starting with policy for socket. No code for NUMA yet
   rank   - rank on node
   nonode - number of threads/processes on the node (needed to apply policy
            to rank)
   nsocket - number of sockets on node
   nnuma   - number of NUMA regions in a socket
*/

/*@ BENV_HwdescNodeInfoPolicy - Use scheduler policy to update hwdesc with information about the calling process placement within a node

Input Parameter:
. parms - Contains information about the node configuration and scheduler policy

Input/Output Parameter:
. hwc - An hwdescCtx. It may already contain some information about the job.
   See notes below.

.N returnvalue

Notes:
Adds information about the node resources (e.g., socket, NUMA, or core)
used by the process. There must already be configuration information for
those resources in the 'hwc' (see BENV_HwdescNodeConfigXXX routines).
Information is added to the objinfo fields.

Only sets values is the assignment source is 'BENV_HWDESC_ASSIGN_UNKNOWN'.
@*/
int BENV_HwdescNodeInfoPolicy(hwdescParms *parms, hwdescCtx *hwc)
{
    int rc=-1, loc, blocksocket, bs, np, isset=0;
    int sockidx, numaidx, coreidx;
    int rank = parms->rank;
    const char *policy=0;

    CDBGFCALLENTERV("parms.nonnode=%d, parms.rank=%d, parms.nobjs=%d,%d,%d,%d\n",
		    parms->nonnode, parms->rank, parms->nobjs[0],
		    parms->nobjs[1], parms->nobjs[2], parms->nobjs[3]);
    CDBGV(GETNODEINFO,BASIC,"Entering NodeInfoPolicy:\
parms.nonnode=%d, parms.rank=%d, parms.nobjs=%d,%d,%d,%d\n",
		    parms->nonnode, parms->rank, parms->nobjs[0],
		    parms->nobjs[1], parms->nobjs[2], parms->nobjs[3]);
    np = parms->nonnode;
    if (np <= 0) {
	int nodelevel, isexact;
	/* Try to get the information from the hwc */
	BENV_HwdescFindObject(hwc, BENV_HWDESC_NODE, &nodelevel, &isexact);
	if (nodelevel >= 0 &&
	    hwc->collinfo[nodelevel].objcomm != MPI_COMM_NULL) {
	    MPI_Comm_size(hwc->collinfo[nodelevel].objcomm, &np);
	}
	else {
	    fprintf(stderr, "Unable to determine the number of processes on the node!\n");
	    rc = -1;
	    goto fn_fail;
	}
    }
    /* Sanity check */
    if (rank >= np) {
	fprintf(stderr, "rank =%d > np=%d\n", rank, np);
	rc = -1;
	goto fn_fail;
    }

    loc = 0;
    /* Provide a default policy if policy is null */
    policy = parms->policy;
    if (!policy) {
	policy = "B:B";
    }
    /* Set the number to use based on parms. Ignore values for levels
       not present in hwc */
    /* Set defaults based on parms */
    int nsocket = 1;
    int nnuma   = 1;
    int ncore   = 1;
    /* Note: object counts are relative to the parent, not the socket or node */
    for (int i=0; i<hwc->nlevel; i++) {
	switch (hwc->objinfo[i].kind) {
	case BENV_HWDESC_SOCKET:
	    if (hwc->objinfo[i].nobj != parms->nobjs[1]) {
		fprintf(stderr, "Policy parms value for sockets = %d, config = %d\n",
			parms->nobjs[1], hwc->objinfo[i].nobj);
	    }
	    nsocket = parms->nobjs[1];
	    break;
	case BENV_HWDESC_NUMA:
	    if (hwc->objinfo[i].nobj != parms->nobjs[2]) {
		fprintf(stderr, "Policy parms value for NUMA = %d, config = %d\n",
			parms->nobjs[2], hwc->objinfo[i].nobj);
	    }
	    nnuma = parms->nobjs[2];
	    break;
	case BENV_HWDESC_CORE:
	    /* FIXME:!!! parns->nobjs[3] is ncore relative to WHAT?. This
	       looks like cores/socket, rather than cores/NUMA */
	    if (hwc->objinfo[i].nobj != parms->nobjs[3]) {
		fprintf(stderr, "Policy parms value for cores = %d, config = %d\n",
			parms->nobjs[3], hwc->objinfo[i].nobj);
	    }
	    ncore = parms->nobjs[3];
	    break;
	default:
	}
    }
    if (ncore < 1) ncore = 128;   /* Set a default */

    CDBGV(GETNODEINFO,DETAIL,"Determined nsocket=%d, nnuma=%d,ncore=%d\n",
	  nsocket, nnuma, ncore);
    CDBGV(GETNODEINFO,DETAIL,"policy[%d] = %s\n", loc, &policy[loc]);

    /* Check for valid nsocket and nnuma */
    if (nsocket <= 0 || nsocket > 128 || nnuma <=0) {
	fprintf(stderr, "Invalid number of sockets (%d) or NUMA regions (%d)\n",
		nsocket, nnuma);
	rc = 1;
	goto fn_fail;
    }

    blocksocket = BENVi_GetBlocksizeFromString(policy, &loc,
					       np/nsocket);
    if (blocksocket <= 0) {
	fprintf(stderr, "breaking from GetBlocksize policy[%d] = %s = %d\n",
		    loc, &policy[loc], blocksocket);
	rc = 1;
	goto fn_fail;
    }
    CDBGV(GETNODEINFO,DETAIL,"Determined blocksocket=%d from policy=%s, np=%d, nsocket=%d\n",
	  blocksocket, policy, np, nsocket);

    if (policy[loc] == ':') loc++;
    CDBGV(GETNODEINFO,DETAIL,"Distribute np=%d with rank=%d across %d sockets\n",
	  np, rank, nsocket);
    rc = BENVi_DistribRankByPolicy(np, rank, nsocket, blocksocket,
				   &sockidx, &rank, &np);
    if (rc != 0) {
	fprintf(stderr, "breaking from DistribRankByPolicy\n");
    }

    bs = BENVi_GetBlocksizeFromString(policy, &loc, np/nnuma);
    CDBGV(GETNODEINFO,DETAIL,"Determined blocksocket=%d from policy=%s, np=%d, nsocket=%d\n",
	  blocksocket, policy, np, nsocket);
    if (policy[loc] == ':') loc++;
    if (nnuma > 1) {
	CDBGV(GETNODEINFO,DETAIL,"Distribute np=%d with rank=%d across %d numa objects\n",
	  np, rank, nnuma);
	rc = BENVi_DistribRankByPolicy(np, rank, nnuma, bs,
				       &numaidx, &rank, &np);
	if (rc != 0) {
	    fprintf(stderr, "breaking from DistribRankByPolicy\n");
	}
    }
    else {
	numaidx = 0;
    }
    /* If there are more processes left than there are cores in a NUMA region,
       we need to distribute them as well, using cyclic by default */
    if (np > ncore) {
	/* FIXME: get policy; use C if blank */
	bs = 1;
	CDBGV(GETNODEINFO,DETAIL,"Distribute np=%d with rank=%d across %d cores using blocksize %d\n",
	      np, rank, ncore, bs);
	rc = BENVi_DistribRankByPolicy(np, rank, ncore, bs,
				       &coreidx, &rank, &np);
	if (rc != 0) {
	    fprintf(stderr, "DistribRankByPolicy returned rc=%d\n", rc);
	}
	CDBGV(GETNODEINFO,DETAIL,"Returned coreidx=%d\n", coreidx);
	fflush(stderr);
    }
    else coreidx = rank;

    CDBGV(GETNODEINFO,DETAIL,"Found sockidx=%d, numaidx=%d, coreidx=%d\n",
	  sockidx, numaidx, coreidx);
    for (int i=0; i<hwc->nlevel; i++) {
	if (hwc->objinfo[i].asrc != BENV_HWDESC_ASSIGN_UNKNOWN) continue;
	switch (hwc->objinfo[i].kind) {
	case BENV_HWDESC_SOCKET:
	    hwc->objinfo[i].objidx     = sockidx;
	    hwc->objinfo[i].rawobjidx  = sockidx;
	    hwc->objinfo[i].nodeobjidx = sockidx;
	    hwc->objinfo[i].asrc       = BENV_HWDESC_ASSIGN_POLICY;
	    isset = 1;
	    break;
	case BENV_HWDESC_NUMA:
	    hwc->objinfo[i].objidx    = numaidx;
	    hwc->objinfo[i].rawobjidx = numaidx;
	    /* FIXME: Need nodeobjidx; need socket idx */
	    hwc->objinfo[i].asrc      = BENV_HWDESC_ASSIGN_POLICY;
	    isset = 1;
	    break;
	case BENV_HWDESC_CORE:
	    /* FIXME: objidx needs to be relative to NUMA. Is it? */
	    /* FIXME: nodeobjidx needs numaidx and socket idx */
	    hwc->objinfo[i].objidx    = coreidx;
	    hwc->objinfo[i].rawobjidx = coreidx;
	    hwc->objinfo[i].asrc      = BENV_HWDESC_ASSIGN_POLICY;
	    isset = 1;
	    break;
	default:
	}
    }

    if (isset) {
	if (hwc->source) {
	    hwc->source = BENV_StringApp(hwc->source, 1,
					 "; assign by scheduler policy");
	}
	else {
	    hwc->source = strdup("assign by schedular policy");
	}
    }

    rc = 0;
    CDBG(GETNODEINFO,BASIC,"Leaving NodeInfoPolicy");

fn_fail:
    CDBGFCALLEXIT;
    return rc;
}

/*@
  BENV_HwdescNodeGetInfo - Determine the assignent of the calling process to hardware elements on a node

Input Parameters:
+ flags - Flags that control which methods may be used to determine the
 output node process assignment
- parms - Provide information about the node configuration and scheduler
 policy. Only used when 'flags' includes 'BENV_HWDESC_ASSIGN_POLICY'

Input/Output Parameters:
. hwc - pointer to hwdescCtx that provides information about which node hardware elements (sockets, NUMA regions, cores) to which the calling process is mapped. Note that hwc must be a valid pointer.

.N returnvalue

Notes:
Valid flags include
.n BENV_HWDESC_ASSIGN_GETCPU - Use getcpu
.n BENV_HWDESC_ASSIGN_SCHEDGETCPU - Use sched_getcpu
.n BENV_HWDESC_ASSIGN_POLICY - Use the scheduler policy
.n
  @*/
int BENV_HwdescNodeGetInfo(int flags, hwdescParms *parms, hwdescCtx *hwc)
{
    int rc = -1;;
    CDBGFCALLENTER;
#ifdef HAVE_GETCPU
    if (rc < 0 && (flags & BENV_HWDESC_ASSIGN_GETCPU)) {
	CDBG(GETNODEINFO,DETAIL,"Trying NodeInfoGetcpu");
	rc = BENV_HwdescNodeInfoGetcpu(0, hwc);
    }
#endif
#ifdef HAVE_SCHED_GETCPU
    if (rc < 0 && (flags & BENV_HWDESC_ASSIGN_SCHEDGETCPU)) {
	CDBG(GETNODEINFO,DETAIL,"Trying NodeInfoSchedgetcpu");
	rc = BENV_HwdescNodeInfoSchedgetcpu(0, hwc);
    }
#endif
    /* FIXME: does HWLOC provide info information? */

    if (rc < 0 && parms != 0 && (flags & BENV_HWDESC_ASSIGN_POLICY)) {
	CDBG(GETNODEINFO,DETAIL,"Trying NodeInfoPolicy");
	rc = BENV_HwdescNodeInfoPolicy(parms, hwc);
    }

#if 0
    printf("rc = %d near end of get info\n", rc); fflush(stdout);
    if (rc == 0)
	rc = BENV_HwdescNodeSetCollinfo(hwc);
#endif
    CDBGFCALLEXIT;
    return rc;
}

#if 0
static void addnodeinfo(int nsocket, int nnuma, int ncore, hwdescCtx *hwc,
    hwdescConfigSrc csrc)
{
    /* Add socket */
    hwc->objinfo[hwc->nlevel].nobj = nsocket;
    hwc->objinfo[hwc->nlevel].objidx = -1;
    hwc->objinfo[hwc->nlevel].kind = BENV_HWDESC_SOCKET;
    hwc->objinfo[hwc->nlevel].csrc = csrc;
    hwc->nlevel++;

    if (nnuma > 0) {
	/* Add NUMA regions */
	/* NUMA information is optional. getcpu will return numa
	   idx */
	hwc->objinfo[hwc->nlevel].nobj = nnuma;
	hwc->objinfo[hwc->nlevel].objidx = -1;
	hwc->objinfo[hwc->nlevel].kind = BENV_HWDESC_NUMA;
	hwc->objinfo[hwc->nlevel].csrc = csrc;
	hwc->nlevel++;
    }

    /* Add cores/socket */
    hwc->objinfo[hwc->nlevel].nobj = ncore;
    hwc->objinfo[hwc->nlevel].objidx = -1;
    hwc->objinfo[hwc->nlevel].kind = BENV_HWDESC_CORE;
    hwc->objinfo[hwc->nlevel].csrc = csrc;
    hwc->nlevel++;

    const char *cname = BENV_HwdescConfigStr(csrc);
    if (hwc->source) {
	hwc->source = BENV_StringApp(hwc->source, 2, "; ", cname);
    }
    else {
	hwc->source = strdup(cname);
    }
}
#endif

/* Values are all per node, not per parent object.
   Add, not updates, entries */
/* Only add if "raw" value is > 0 */
static void addnodeconfignew(int nodensock, int nodennuma, int nodencore,
			     int nsock, int nnuma, int ncore,
			     int rawnsock, int rawnnuma, int rawncore,
			     hwdescCtx *hwc, hwdescConfigSrc csrc)
{
    CDBGFCALLENTERV("nsocket=%d, nnuma=%d, ncore=%d\n", rawnsock,
		    rawnnuma, rawncore);
    /* Add socket */
    if (rawnsock > 0) {
	hwc->objinfo[hwc->nlevel].rawnobj = rawnsock;
	if (nodensock > 0)
	    hwc->objinfo[hwc->nlevel].nodenobj = nodensock;
	if (nsock > 0)
	    hwc->objinfo[hwc->nlevel].nobj   = nsock;
	hwc->objinfo[hwc->nlevel].objidx     = -1;
	hwc->objinfo[hwc->nlevel].nodeobjidx = -1;
	hwc->objinfo[hwc->nlevel].rawobjidx  = -1;
	hwc->objinfo[hwc->nlevel].kind       = BENV_HWDESC_SOCKET;
	hwc->objinfo[hwc->nlevel].csrc       = csrc;
	hwc->nlevel++;
    }

    if (rawnnuma > 0) {
	/* Add NUMA regions */
	/* NUMA information is optional. getcpu will return numa
	   idx */
	hwc->objinfo[hwc->nlevel].rawnobj = rawnnuma;
	if (nodennuma > 0)
	    hwc->objinfo[hwc->nlevel].nodenobj   = nodennuma;
	if (nnuma > 0)
	    hwc->objinfo[hwc->nlevel].nobj   = nnuma;
	hwc->objinfo[hwc->nlevel].objidx     = -1;
	hwc->objinfo[hwc->nlevel].nodeobjidx = -1;
	hwc->objinfo[hwc->nlevel].rawobjidx  = -1;
	hwc->objinfo[hwc->nlevel].kind       = BENV_HWDESC_NUMA;
	hwc->objinfo[hwc->nlevel].csrc       = csrc;
	hwc->nlevel++;
    }

    /* Add cores/socket */
    if (rawncore > 0) {
	hwc->objinfo[hwc->nlevel].rawnobj = rawncore;
	if (nodencore > 0)
	    hwc->objinfo[hwc->nlevel].nodenobj   = nodencore;
	if (ncore > 0)
	    hwc->objinfo[hwc->nlevel].nobj   = ncore;
	hwc->objinfo[hwc->nlevel].objidx     = -1;
	hwc->objinfo[hwc->nlevel].nodeobjidx = -1;
	hwc->objinfo[hwc->nlevel].rawobjidx  = -1;
	hwc->objinfo[hwc->nlevel].kind       = BENV_HWDESC_CORE;
	hwc->objinfo[hwc->nlevel].csrc       = csrc;
	hwc->nlevel++;
    }

    const char *cname = BENV_HwdescConfigStr(csrc);
    if (hwc->source) {
	hwc->source = BENV_StringApp(hwc->source, 2, "; ", cname);
    }
    else {
	hwc->source = strdup(cname);
    }
    CDBGFCALLEXIT;
}
#if 0
static void addnodeconfig(int nsocket, int nnuma, int ncore, hwdescCtx *hwc,
    hwdescConfigSrc csrc)
{
    CDBGFCALLENTERV("nsocket=%d, nnuma=%d, ncore=%d\n", nsocket, nnuma, ncore);
    /* Add socket */
    if (nsocket > 0) {
	hwc->objinfo[hwc->nlevel].nodenobj   = nsocket;
	hwc->objinfo[hwc->nlevel].nodeobjidx = -1;
	hwc->objinfo[hwc->nlevel].kind       = BENV_HWDESC_SOCKET;
	hwc->objinfo[hwc->nlevel].csrc       = csrc;
	hwc->nlevel++;
    }

    if (nnuma > 0) {
	/* Add NUMA regions */
	/* NUMA information is optional. getcpu will return numa
	   idx */
	hwc->objinfo[hwc->nlevel].nodenobj   = nnuma;
	hwc->objinfo[hwc->nlevel].nodeobjidx = -1;
	hwc->objinfo[hwc->nlevel].kind       = BENV_HWDESC_NUMA;
	hwc->objinfo[hwc->nlevel].csrc       = csrc;
	hwc->nlevel++;
    }

    /* Add cores/socket */
    if (ncore > 0) {
	hwc->objinfo[hwc->nlevel].nodenobj   = ncore;
	hwc->objinfo[hwc->nlevel].nodeobjidx = -1;
	hwc->objinfo[hwc->nlevel].kind       = BENV_HWDESC_CORE;
	hwc->objinfo[hwc->nlevel].csrc       = csrc;
	hwc->nlevel++;
    }

    const char *cname = BENV_HwdescConfigStr(csrc);
    if (hwc->source) {
	hwc->source = BENV_StringApp(hwc->source, 2, "; ", cname);
    }
    else {
	hwc->source = strdup(cname);
    }
    CDBGFCALLEXIT;
}
#endif

/*@
  BENV_HwdescNodeGetSockNumaCore - Get information on the socket, NUMA region, and core from and hwdescCtx

Input Parameter:
. hwc - An hwdescCtx for the process

Output Parameters:
+ nobjs - Number of objects
- objidx - Index of objects

.N returnvalue

Notes:
Returns in 'nobjs' and 'objidx' the number and index of sockets, NUMA regions,
 and cores, respectively, relative to the parent. If there is no
 information available, the values are unchanged.
  @*/
int BENV_HwdescNodeGetSockNumaCore(hwdescCtx *hwc, int nobjs[3], int objidx[3])
{
    CDBGFCALLENTER;
    for (int i=0; i<hwc->nlevel; i++) {
	switch (hwc->objinfo[i].kind) {
	case BENV_HWDESC_SOCKET:
	    nobjs[0]  = hwc->objinfo[i].nobj;
	    objidx[0] = hwc->objinfo[i].objidx;
	    break;
	case BENV_HWDESC_NUMA:
	    nobjs[1]  = hwc->objinfo[i].nobj;
	    objidx[1] = hwc->objinfo[i].objidx;
	    break;
	case BENV_HWDESC_CORE:
	    nobjs[2]  = hwc->objinfo[i].nobj;
	    objidx[2] = hwc->objinfo[i].objidx;
	    break;
	default:
	}
    }
    CDBGFCALLEXIT;
    return 0;
}

/*@
  BENV_HwdescGetNodeDesc - Update an hwdescCtx with information about the node

Input Parameters:
+ parms - Provides parameters that may be used to specify the node information
- flags - Indicates which methods may be used to provide node information. -1 allows all methods. Flags may be any values permitted by 'BENV_HwdescNodeGetConfig'
or 'BENV_HwdescNodeGetInfo'

Input/Output Parameter:
. hwc - An hwdescCtx for the process

Notes:
This routine adds information about the hardware on a node to an 'hwdescCtx'.


See also:
BENV_HwdescNodeGetConfig, BENV_HwdescNodeGetInfo
  @*/
int BENV_HwdescGetNodeDesc(hwdescCtx *hwc, hwdescParms *parms, int flags)
{
    int rc, nodelevel, isexact;
    hwdescParms nparms;

    CDBGFCALLENTERV("flags=%x\n", flags);

    /* Mask the flags */
    flags &= BENVi_HwdescFlagMask;

    /* Find the node hardware configuration */
    rc = BENV_HwdescNodeGetConfig(flags, hwc);
    if (!rc)
	rc = BENV_HwdescNodeNormalizeConfig(hwc);

    /* Find the node process assignment */
    /* Use an updated parms that includes the discovered configuration */
    nparms = *parms;
    BENV_HwdescFindObject(hwc, BENV_HWDESC_NODE, &nodelevel, &isexact);
    if (nodelevel >= 0 &&
	hwc->collinfo[nodelevel].objcomm != MPI_COMM_NULL) {
	MPI_Comm_rank(hwc->collinfo[nodelevel].objcomm, &nparms.rank);
	MPI_Comm_size(hwc->collinfo[nodelevel].objcomm, &nparms.nonnode);
	for (int i=nodelevel; i<hwc->nlevel; i++) {
	    switch (hwc->objinfo[i].kind) {
	    case BENV_HWDESC_SOCKET:
		nparms.nobjs[1] = hwc->objinfo[i].nobj;
		break;
	    case BENV_HWDESC_NUMA:
		nparms.nobjs[2] = hwc->objinfo[i].nobj;
		break;
	    case BENV_HWDESC_CORE:
		nparms.nobjs[3] = hwc->objinfo[i].nobj;
		break;
	    default:
	    }
	}
    }
    rc = BENV_HwdescNodeGetInfo(flags, &nparms, hwc);
    if (!rc)
	rc = BENV_HwdescNodeNormalizeAssign(hwc);

    CDBGFCALLEXIT;
    return rc;
}

/*
 * This routine sets the collective information in the hwdescCtx from
 * the objinfo. The hwc->collinfo for the object above those added for the
 * node must be valid
 *
 * A parent communicator 'pcomm' is provided in case none of the
 * entries has a communicator.
 */
int BENV_HwdescNodeSetCollinfo(MPI_Comm pcomm, hwdescCtx *hwc)
{
    int startlevel, i, rc = 0;

    CDBGFCALLENTER;
    CDBGV(GETDESC,DETAIL,"Add collinfo for node object info with %d levels\n",
	hwc->nlevel);
    /* Find the start level */
    for (startlevel=0; startlevel<hwc->nlevel; startlevel++) {
	if (hwc->collinfo[startlevel].objcomm == 0 ||
	    hwc->collinfo[startlevel].objcomm == MPI_COMM_NULL) break;
    }
    CDBGV(GETDESC,DETAIL,"Add collinfo starting at level %d using Comm_split\n",
	  startlevel);
    if (startlevel > 0) {
	CDBGV(GETDESC,DETAIL,"Updated pcomm to objcomm from level %d\n",
	      startlevel - 1);
	pcomm = hwc->collinfo[startlevel-1].objcomm;
    }
    for (i=startlevel; i<hwc->nlevel; i++) {
	int rank;
	/* Split based on the objidx */
	MPI_Comm_rank(pcomm, &rank);
	CDBGV(GETDESC,ALL,"Splitting parent comm on [%d]objidx=%d\n",
	      i, hwc->objinfo[i].objidx);
	MPI_Comm_split(pcomm, hwc->objinfo[i].objidx, rank,
		       &hwc->collinfo[i].objcomm);
	hwc->collinfo[i].nSiblings = hwc->objinfo[i].nobj;
	CDBGV(GETDESC,ALL,"FindLeaders for level %d with %d siblings\n", i,
	    hwc->collinfo[i].nSiblings);
	rc = BENVi_FindLeadersInSplit(pcomm,
				      hwc->collinfo[i].objcomm,
				      &hwc->objinfo[i], &hwc->collinfo[i]);
	pcomm = hwc->collinfo[i].objcomm;
    }

    CDBGFCALLEXIT;
    return rc;
}

/*@ BENV_HwdescNodeNormalize - Ensure node information is relative to parent

Input/Output Parameter:
hwc - An hwdescCtx context

.N returnvalue

Notes:
Many information routines provide information about objects such as NUMA
regions or cores in terms of the node. E.g., the total number of cores on
a node. The 'hwdescCtx' description requires that every level be defined
relative to the parent, e.g., the number of cores is relative to the
containing NUMA region (or socket if no NUMA regions defined). This routine
performs that normalization. Haing this step in a separate routine allows the
use of several different routines to gather information about the configuration
and assignment of resources.
  @*/
int BENV_HwdescNodeNormalize(hwdescCtx *hwc)
{
#if 1
    CDBGFCALLENTER;
//    printf("Running HwdescNodeNormalize\n"); fflush(stdout);
    BENV_HwdescNodeNormalizeConfig(hwc);
    BENV_HwdescNodeNormalizeAssign(hwc);
#else
    int numalev=-1, corelev=-1;
    int nsocket=0, nnuma=0, ncore=0, i;
    /* This relies on the object being correctly ordered within the
       hwc->objinfo description */
    for (i=0; i<hwc->nlevel; i++) {
	switch (hwc->objinfo[i].kind) {
	case BENV_HWDESC_SOCKET:
	    if (hwc->objinfo[i].nodenobj > 0)
		nsocket = hwc->objinfo[i].nodenobj;
	    else if (hwc->objinfo[i].nobj > 0)
		nsocket = hwc->objinfo[i].nobj;
	    break;
	case BENV_HWDESC_NUMA:
	    numalev = i;
	    if (hwc->objinfo[i].nodenobj > 0)
		nnuma = hwc->objinfo[i].nodenobj;
	    break;
	case BENV_HWDESC_CORE:
	    corelev = i;
	    if (hwc->objinfo[i].nodenobj > 0)
		ncore = hwc->objinfo[i].nodenobj;
	    break;
	default:
	}
    }
    /* Normalize the numa and core counts */
    if (nsocket < 0)
	return -1;
    if (numalev >= 0 && hwc->objinfo[numalev].nobj <= 0) {
	CDBGV(GETNODEINFO,DETAIL,"Normalizing nnuma=%d by %d sockets\n",
	      nnuma, nsocket);
	nnuma /= nsocket;
	hwc->objinfo[numalev].nobj = nnuma;
	if (hwc->objinfo[numalev].objidx <= 0)
	    hwc->objinfo[numalev].objidx =
		hwc->objinfo[numalev].nodeobjidx % nnuma;
    }
    if (corelev >= 0 && hwc->objinfo[corelev].nobj <= 0) {
	CDBGV(GETNODEINFO,DETAIL,"Normalizing ncore=%d by %d sockets\n",
	      ncore, nsocket);
	ncore /= nsocket;
	if (nnuma) {
	    CDBGV(GETNODEINFO,DETAIL,"Normalizing ncore=%d by %d NUMAs\n",
		  ncore, nnuma);
	    ncore /= nnuma;
	}
	hwc->objinfo[corelev].nobj = ncore;
	if (hwc->objinfo[corelev].objidx <= 0)
	    hwc->objinfo[corelev].objidx =
		hwc->objinfo[corelev].nodeobjidx % ncore;
    }
#endif
    CDBGFCALLEXIT;
    return 0;
}

/*@ BENV_HwdescNodeNormalizeConfig - Ensure node configuration information is relative to parent

Input/Output Parameter:
hwc - An hwdescCtx context

.N returnvalue

Notes:
Many information routines provide information about objects such as NUMA
regions or cores in terms of the node. E.g., the total number of cores on
a node. The 'hwdescCtx' description requires that every level be defined
relative to the parent, e.g., the number of cores is relative to the
containing NUMA region (or socket if no NUMA regions defined). This routine
performs that normalization. Having this step in a separate routine allows the
use of several different routines to gather information about the configuration
and assignment of resources.
  @*/
int BENV_HwdescNodeNormalizeConfig(hwdescCtx *hwc)
{
    int numalev=-1, corelev=-1, socklev=-1;
    int nsocket=0, nnuma=0, ncore=0, i;

    CDBGFCALLENTER;
    /* This relies on the object being correctly ordered within the
       hwc->objinfo description */
    for (i=0; i<hwc->nlevel; i++) {
	switch (hwc->objinfo[i].kind) {
	case BENV_HWDESC_SOCKET:
	    socklev = i;
	    if (hwc->objinfo[i].nodenobj > 0)
		nsocket = hwc->objinfo[i].nodenobj;
	    else if (hwc->objinfo[i].nobj > 0)
		nsocket = hwc->objinfo[i].nobj;
	    break;
	case BENV_HWDESC_NUMA:
	    numalev = i;
	    if (hwc->objinfo[i].nodenobj > 0)
		nnuma = hwc->objinfo[i].nodenobj;
	    break;
	case BENV_HWDESC_CORE:
	    corelev = i;
	    if (hwc->objinfo[i].nodenobj > 0)
		ncore = hwc->objinfo[i].nodenobj;
	    break;
	default:
	}
    }
    /* Normalize the numa and core counts */
    { int orignsock=0, orignnuma=0, origncore=0;
	if (socklev >= 0) orignsock = hwc->objinfo[socklev].nobj;
	if (numalev >= 0) orignnuma = hwc->objinfo[numalev].nobj;
	if (corelev >= 0) origncore = hwc->objinfo[corelev].nobj;
	CDBGV(GETNODEINFO,DETAIL,"Config: object counts (objinfo.nobj) are sock=%d, numa=%d, core=%d\n",
	      orignsock, orignnuma, origncore);
    }
    if (nsocket < 0)
	return -1;
    if (hwc->objinfo[socklev].nobj <= 0) {
	CDBGV(GETNODEINFO,DETAIL,"Setting nsocket=%d\n", nsocket);
	hwc->objinfo[socklev].nobj = nsocket;
    }
    if (numalev >= 0 && hwc->objinfo[numalev].nobj <= 0) {
	CDBGV(GETNODEINFO,DETAIL,"Normalizing nnuma=%d by %d sockets\n",
	      nnuma, nsocket);
	nnuma /= nsocket;
	hwc->objinfo[numalev].nobj = nnuma;
    }
    if (corelev >= 0 && hwc->objinfo[corelev].nobj <= 0) {
	CDBGV(GETNODEINFO,DETAIL,"Normalizing ncore=%d by %d sockets\n",
	      ncore, nsocket);
	ncore /= nsocket;
	if (nnuma) {
	    CDBGV(GETNODEINFO,DETAIL,"Normalizing ncore=%d by %d NUMAs\n",
		  ncore, nnuma);
	    ncore /= nnuma;
	}
	hwc->objinfo[corelev].nobj = ncore;
    }

    CDBGFCALLEXIT;
    return 0;
}

/*@ BENV_HwdescNodeNormalizeAssign - Ensure node assignment information is relative to parent

Input/Output Parameter:
hwc - An hwdescCtx context

.N returnvalue

Notes:
Many information routines provide information about objects such as NUMA
regions or cores in terms of the node. E.g., the total number of cores on
a node. The 'hwdescCtx' description requires that every level be defined
relative to the parent, e.g., the number of cores is relative to the
containing NUMA region (or socket if no NUMA regions defined). This routine
performs that normalization. Having this step in a separate routine allows the
use of several different routines to gather information about the configuration
and assignment of resources.
  @*/
int BENV_HwdescNodeNormalizeAssign(hwdescCtx *hwc)
{
    int numalev=-1, corelev=-1, socklev=-1;
    int nsocket=0/*, nnuma=0, ncore=0*/, i;

    CDBGFCALLENTER;

    /* This relies on the object being correctly ordered within the
       hwc->objinfo description */
    for (i=0; i<hwc->nlevel; i++) {
	switch (hwc->objinfo[i].kind) {
	case BENV_HWDESC_SOCKET:
	    socklev = i;
	    if (hwc->objinfo[i].nodenobj > 0)
		nsocket = hwc->objinfo[i].nodenobj;
	    else if (hwc->objinfo[i].nobj > 0)
		nsocket = hwc->objinfo[i].nobj;
	    break;
	case BENV_HWDESC_NUMA:
	    numalev = i;
//	    if (hwc->objinfo[i].nodenobj > 0)
//		nnuma = hwc->objinfo[i].nodenobj;
	    break;
	case BENV_HWDESC_CORE:
	    corelev = i;
//	    if (hwc->objinfo[i].nodenobj > 0)
//		ncore = hwc->objinfo[i].nodenobj;
	    break;
	default:
	}
    }
    /* Normalize the numa and core counts */
    if (nsocket < 0)
	return -1;

    { int orignsock=0, orignnuma=0, origncore=0;
	if (socklev >= 0) orignsock = hwc->objinfo[socklev].nobj;
	if (numalev >= 0) orignnuma = hwc->objinfo[numalev].nobj;
	if (corelev >= 0) origncore = hwc->objinfo[corelev].nobj;
	CDBGV(GETNODEINFO,DETAIL,"Assign: object counts (objinfo.nobj) are sock=%d, numa=%d, core=%d\n",
	      orignsock, orignnuma, origncore);
    }
    if (hwc->objinfo[socklev].objidx < 0) {
	CDBG(GETNODEINFO,DETAIL,"Setting socket idx");
	if (hwc->objinfo[socklev].nodeobjidx >= 0) {
	    CDBGV(GETNODEINFO,DETAIL,"Setting socket idx = %d\n",
		  hwc->objinfo[socklev].nodeobjidx);
	    hwc->objinfo[socklev].objidx = hwc->objinfo[socklev].nodeobjidx;
	}
	else if (corelev >= 0) {
	    /* use the number of cores, the running core number, and the
	       number of sockets to determine the socket */
	    /* Corespersock must be the absolute number, so that the core
	     number (objidx) is in the range [0,nsockets*corespersock) */
	    int corespersock = hwc->objinfo[corelev].nodenobj /
		hwc->objinfo[socklev].nodenobj;
	    int socknum = hwc->objinfo[corelev].nodeobjidx / corespersock;
	    if (socknum >= hwc->objinfo[socklev].nobj) {
		fprintf(stderr, "Warning: Computed socket number %d >= number of sockets %d.\n", socknum, hwc->objinfo[socklev].nobj);
		fprintf(stderr, "cores nodeobjidx = %d, nodenobj = %d, sock nodenobj = %d\n",
			hwc->objinfo[corelev].nodeobjidx,
			hwc->objinfo[corelev].nodenobj,
			hwc->objinfo[socklev].nodenobj);
		fprintf(stderr, "Cores per socket = %d is probably wrong\nForcing socket number in range\n",
			corespersock);
		socknum = hwc->objinfo[socklev].nobj-1;
	    }
	    hwc->objinfo[socklev].objidx     = socknum;
	    hwc->objinfo[socklev].nodeobjidx = socknum;
	}
    }
    if (numalev >= 0 && hwc->objinfo[numalev].objidx < 0) {
	int numaidx = hwc->objinfo[numalev].nodeobjidx;
	CDBGV(GETNODEINFO,DETAIL,"Normalizing nnuma idx=%d by %d sockets\n",
	      numaidx, nsocket);
	if (hwc->objinfo[numalev].nobj <= 0) {
	    fprintf(stderr, "Number of NUMA regions %d is <= 0! (nodenobj=%d)\n",
		    hwc->objinfo[numalev].nobj, hwc->objinfo[numalev].nodenobj);
	}
	else {
	    hwc->objinfo[numalev].objidx = numaidx % hwc->objinfo[numalev].nobj;
	}
    }
    if (corelev >= 0 && hwc->objinfo[corelev].objidx < 0) {
	int coreidx = hwc->objinfo[corelev].nodeobjidx;
	CDBGV(GETNODEINFO,DETAIL,"Normalizing core idx=%d by %d sockets, modulo %d cores (nobj)\n",
	      coreidx, nsocket, hwc->objinfo[corelev].nobj);
	if (hwc->objinfo[corelev].nobj <= 0) {
	    fprintf(stderr, "Number of cores is %d <= 0! (nodenobj=%d)\n",
		    hwc->objinfo[corelev].nobj, hwc->objinfo[corelev].nodenobj);
	}
	else {
	    hwc->objinfo[corelev].objidx = coreidx % hwc->objinfo[corelev].nobj;
	    CDBGV(GETNODEINFO,DETAIL,"core objinfo[%d].objidx=%d\n",
		  corelev, hwc->objinfo[corelev].objidx);
	}
    }

    CDBGFCALLEXIT;
    return 0;
}
