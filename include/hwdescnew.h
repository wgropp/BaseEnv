#ifndef HWDESC_H_INCLUDED
#define HWDESC_H_INCLUDED 1

/* Temporary replacement for hwdesc public types and routines */

/* HW description (hwdesc/hwdescutil2.c) */
/* This is a distributed data structure for each process that identifies
   the process in a hierarchy of communicators, and provides information
   for communicating with the sibling communicators at each level.
   There is an hwdescCtx that containing pointers to arrays with all of the
   information.

   A typical description is:
   hwc.nlevel                     = 3
   hwc.nAllocated                 = 10
   hwc.objinfo[0].nobj            = 1
   hwc.objinfo[0].objidx          = 0
   hwc.objinfo[0].kind            = BENV_HWDESC_JOB
   hwc.objinfo[0].csrc            =
   hwc.objinfo[0].asrc            =
   hwc.objinfo[0].othersrc        = NULL
   hwc.mpiinfo[0].objcomm         = MPI_COMM_WORLD
   hwc.mpiinfo[0].nSiblings       = 1
   hwc.mpiinfo[0].rankInParent    = 0
   hwc.mpiinfo[0].siblingNum      = 0
   hwc.mpiinfo[0].leadersInParent = NULL
   hwc.mpiinfo[0].descstr         = "COMM WORLD"

   hwc.objinfo[1].nobj            = number of nodes
   hwc.objinfo[1].objidx          = number of this node (node of calling
                                    process)
   hwc.objinfo[1].kind            = BENV_HWDESC_NODE
   hwc.objinfo[1].csrc            =
   hwc.objinfo[1].asrc            =
   hwc.objinfo[1].othersrc        = NULL
   hwc.mpiinfo[1].objcomm         = Result from split COMM_TYPE_SHARED or
                                    unguided from comm
   hwc.mpiinfo[1].nSiblings       = number of nodes (use objinfo.nobj)
   hwc.mpiinfo[1].rankInParent    = number of this node (node of calling
                                    process) (use objinfo.objidx)
   hwc.mpiinfo[1].siblingNum      = 0 (what is this?? how is this different
                                    than objidx??)
   hwc.mpiinfo[1].leadersInParent = array of size nDistinct with ranks in the
                                    parent (comm_world in this case) of the
				    processes that have rank 0 in some
				    hwc.mpiinfo[1].objcomm
   hwc.mpiinfo[1].descstr         = NULL


   A typical description is:
   hw[0].comm       MPI_COMM_WORLD
        .nDistrinct 1
        .idx        0
        .leaders    0
        allsizeone  0
   hw[1].comm       Result from split COMM_TYPE_SHARED or unguided from comm
        .nDistinct  number of nodes
        .idx        number of this node (node of calling process)
        .leaders    array of size nDistinct with ranks in the parent (comm
	            world in this case) of the processes that have rank 0
		    in some hw[1].comm
        allsizeone  probably 0 (but may be 1 if heavily multithreaded code)
   And so on for NUMA (optional) and core

   Normally, users should work with the hwdescCtx_t, accessing the
   hwdesc_t element within that structure.

   Important note on the meaning of nobj
   Within a node the interpretation of "the number of objects" is complicated.
   Some counts are over the entire node; for example, NUMA regions.
   Some are relative to each socket, for example, common specification of
   cores/socket (rather than cores per NUMA region).
*/


typedef enum { BENV_HWDESC_JOB=1, BENV_HWDESC_NODE,
	       BENV_HWDESC_SOCKET, BENV_HWDESC_NUMA,
	       BENV_HWDESC_CORE, BENV_HWDESC_OTHER }
    hwdescKind;

// Combined config+assign: MPI_SHARED, MPI_UNGUIDED, OMPI_SPLIT_TYPES
// hwloc
// These are defined so that they can be combined into a bit vector to
// to select multiple choices, and so that they are unique among Config,
// Assign, and Use

typedef enum { BENV_HWDESC_CONFIG_UNKNOWN=0,
	       BENV_HWDESC_CONFIG_HWLOC=0x10,
	       BENV_HWDESC_CONFIG_SYSCTL=0x20,
	       BENV_HWDESC_CONFIG_CPUINFO=0x40,
	       BENV_HWDESC_CONFIG_NUMANUMNODE=0x80,
	       BENV_HWDESC_CONFIG_MPI4=0x100,
	       BENV_HWDESC_CONFIG_MPI_SHARED=0x200,
	       BENV_HWDESC_CONFIG_ENV=0x400,
	       BENV_HWDESC_CONFIG_GIVEN=0x800,
	       BENV_HWDESC_CONFIG_ALL=0xFF0,
} hwdescConfigSrc;

// getcpu sched_getcpu, sched_getaffinity
typedef enum { BENV_HWDESC_ASSIGN_UNKNOWN=0,
	       BENV_HWDESC_ASSIGN_GETCPU=0x1000,
	       BENV_HWDESC_ASSIGN_SCHEDGETCPU=0x2000,
	       BENV_HWDESC_ASSIGN_NUMAGETNODE=0x4000,
	       BENV_HWDESC_ASSIGN_MPI4=0x8000,
	       BENV_HWDESC_ASSIGN_MPI_SHARED=0x10000,
	       BENV_HWDESC_ASSIGN_POLICY=0x20000,
	       BENV_HWDESC_ASSIGN_HWLOC=0x40000,
	       BENV_HWDESC_ASSIGN_GIVEN=0x80000,
	       BENV_HWDESC_ASSIGN_ALL=0xFF000,
} hwdescAssignSrc;


/* Use these to control which methods BENV_Hwdesc_GetDescGeneral uses */
// Are these obsolete? See above options
// These are used in GetDescGeneral - should they be replaced??
// Or defined in terms of the above??
#define BENV_HWDESC_USE_DEBUG 0x1
#define BENV_HWDESC_USE_POLICY 0x2
#define BENV_HWDESC_USE_MPI4 0x4
#define BENV_HWDESC_USE_NODENAME 0x8
#define BENV_HWDESC_USE_ALL -1

typedef struct {
    int nobj,         /* Number of objects with the same parent */
	objidx;       /* Number of this object in [0,nobj) */
    /* The next two only apply to objects within a node, such as sockets,
       NUMA domains, or cores. Ignored if not within a node. */
    int nodenobj,     /* Number of objects on the same node */
	nodeobjidx;   /* Number of this object in [0,nodeobjidx) */
    int rawnobj,      /* Raw value provided by csrc */
	rawobjidx;    /* Raw value provided by asrc */
    hwdescKind kind;
    // does source include "see parent"? E.g, inherit if not specified?
    // For enums and especially othersrc?
    hwdescConfigSrc csrc;
    hwdescAssignSrc asrc;
    const char *othersrc;  /* Escape for other kinds of sources */
} hwdescObjInfo;

/* MPI Specific information. Note that nSiblings is objinfo.nobj and
   rankInParent is objidx */
typedef struct {
    MPI_Comm objcomm;       /* Communicator for all processes in the same
			       object */
    int nSiblings;          /* Number of objects with the same parent that
			       are part of this job. The number may be
			       less than the number of objects */
//    int rankInParent;       /* Rank of the process in the parent that has
//			       rank 0 in objcomm */
    int siblingNum;         /* Index of this communicator as one of the
			       siblings in the parent. Often this will
			       be the same as objidx, but may be different
			       if not all objects are being used in this job
			       Open question: or if the assignment isn't the
			       same. */
    int allsizeone;         /* True if all communicators at this level are
			       size 1 AND all are leaves (no elements
			       "below" this one); common for "core" hw type */
    int *leadersInParent;   /* For each sibling, rank in the parent of the
			       process with rank 0 in objcomm. Array of
			       size nSiblings */
    const char *descstr;    /* Name of MPI object, if not given by
			       objinfo.kind */
} hwdescMPIInfo;

typedef struct {
    int nlevel,
	nAllocated;
    hwdescObjInfo *objinfo;
    hwdescMPIInfo *collinfo; // or a pointer for cross thread/process information,
    // could be MPI, could be threads. In C++ terms, common structure/operators,
    // especially printing
    const char *source;     // How the context was created (top level)
} hwdescCtx;


/* Argument classes */
#define BENV_HWDESCARG_CORE 0x1
#define BENV_HWDESCARG_POLICY 0x2
#define BENV_HWDESCARG_DEBUGDECOMP 0x4
#define BENV_HWDESCARG_DEBUG 0x8

/* This is a structure that can be set by Args initialization routine (or
   from environment variables) and then passed to the GetDescGeneral routine
*/
typedef struct {
    const char *policy;    /* Scheduler policy string */
    int   nnobj;           /* Number of valid entris in nobjs */
    int   nobjs[4];        /* Number of objects: node/sockets/numa/core,
			      relative to the level above */
                           /* QUERY: Why is this size 3 when there are 4
			      potential types (node through core?) */
    /* The next two are needed for some of the nodeinfo routines */
    int rank,              /* rank of process *on node* (not rank in world) */
	nonnode;           /* number of threads/processes on the node */
    int   forcedebug;      /* true if hw routines should create hwdesc from
			      nobjs and/or policy */
    int   printMap;        /* True if process map should be printed */
    const char *mapname;   /* Name of file for process map */
} hwdescParms;


hwdescCtx *BENV_HwdescCreateCtx(int nlevels);
int BENV_HwdescFreeCtx(hwdescCtx *hwc);
int BENV_HwdescValidate(MPI_Comm pcomm, hwdescCtx *hwc);

// Pass parms, even though not used?
int BENV_HwdescGetDescFromMPI(MPI_Comm comm, hwdescCtx *hwc);
int BENV_HwdescGetDescFromShared(MPI_Comm comm, hwdescCtx *hwc);
// Pass Parms instead???
int BENV_HwdescGetDescFromPolicy(MPI_Comm comm, const char *policy,
				 int np, int rank, int nnobj,
				 const int nobjs[], hwdescCtx *hwc);
// Pass Parms instead???
int BENV_HwdescGetDescFromDebug(MPI_Comm comm, int nvals, const int vals[],
				const char *policy, hwdescCtx *hwc);
int BENV_HwdescGetDescGeneral(MPI_Comm comm, int flags, hwdescParms *parms,
			      hwdescCtx **hwc);

int BENV_HwdescGetCoordTuple(MPI_Comm comm, hwdescCtx *hwc, int maxlevel,
			     int coords[], int sizes[], int *nlevels);

int BENV_HwdescFindCommonLevel(MPI_Comm comm, hwdescCtx *hwc, int maxlevel,
			       int *nlevels);
int BENV_HwdescFindObject(hwdescCtx *hwc, hwdescKind kind, int *objlevel,
			  int *isexact);
int BENV_HwdescCheckConsistentSizes(hwdescCtx *hwc, MPI_Comm comm,
				    int startlevel, int nlevel, int objormpi,
				    int *nvalid);

int BENV_HwdescPrintAll(FILE *fp, MPI_Comm pcomm, hwdescCtx *hwc, int depth);
int BENV_HwdescPrintLocal(FILE *fp, hwdescCtx *hwc, const char *prefix);
int BENV_HwdescPrintTuple(FILE *fp, MPI_Comm comm, hwdescCtx *hwc, int which);
const char *BENV_HwdescToStr(hwdescCtx *hwc);

const char *BENV_HwdescKindStr(hwdescKind kind);
const char *BENV_HwdescConfigStr(hwdescConfigSrc csrc);
const char *BENV_HwdescAssignStr(hwdescAssignSrc asrc);

int BENV_HwdescSaveDescToComm(hwdescCtx *hwc, MPI_Comm comm);
int BENV_HwdescGetDescFromComm(MPI_Comm comm, hwdescCtx **hwc);

/* Node information */
/* Lower level routines to determine information within a node */
#ifdef HAVE_HWLOC
int BENV_HwdescNodeConfigHwloc(void *context, hwdescCtx *hwc);
#endif
#ifdef HAVE_SYSCTLBYNAME
int BENV_HwdescNodeConfigSysctl(void *context, hwdescCtx *hwc);
#endif
#ifdef HAVE_PROC_CPUINFO
int BENV_HwdescNodeConfigCpuinfo(void *context, hwdescCtx *hwc);
#endif
int BENV_HwdescNodeConfigEnv(void *context, hwdescCtx *hwc);
int BENV_HwdescNodeConfigParm(hwdescParms *parms, hwdescCtx *hwc);
int BENV_HwdescNodeGetConfig(int flags, hwdescCtx *hwc);
#ifdef HAVE_GETCPU
int BENV_HwdescNodeInfoGetcpu(hwdescParms *parms, hwdescCtx *hwc);
#endif
#ifdef HAVE_SCHED_GETCPU
int BENV_HwdescNodeInfoSchedgetcpu(hwdescParms *parms, hwdescCtx *hwc);
#endif
//int BENV_HwdescNode_info_policy(void *context, hwdescNodeIdx_t *nidx);
int BENV_HwdescNodeInfoPolicy(hwdescParms *parms, hwdescCtx *hwc);
int BENV_HwdescNodeGetInfo(int flags, hwdescParms *parms, hwdescCtx *hwc);
int BENV_HwdescNodeGetSockNumaCore(hwdescCtx *hwc, int nobjs[3], int objidx[3]);
int BENV_HwdescGetNodeDesc(hwdescCtx *hwc, hwdescParms *parms, int flags);
int BENV_HwdescNodeSetCollinfo(MPI_Comm pcomm, hwdescCtx *hwc);

int BENV_HwdescNodeNormalize(hwdescCtx *hwc);
int BENV_HwdescNodeNormalizeConfig(hwdescCtx *hwc);
int BENV_HwdescNodeNormalizeAssign(hwdescCtx *hwc);

/* A version of MPI_Comm_split_unguided that may work even if MPI doesn't
   implment MPI_Comm_split_unguided */
int MPIX_Comm_split_unguided(MPI_Comm comm, int nrank, const char**desc,
			     MPI_Comm *newcomm);
const char *MPIX_Comm_split_method(MPI_Comm);

/* Standardized command line handling */
int BENV_HwdescArg(int argc, char **argv, int *argcnt, const char *prefix,
		   hwdescParms *parms);
int BENV_HwdescArgCore(int argc, char **argv, int *argcnt, const char *prefix,
		       int *printMap, const char **mapname);
void BENV_HwdescArgPrintUsage(FILE *fp, const char *prefix, int flags);
int BENV_HwdescArgConfig(const char *arg, const char *newname);
int BENV_HwdescArgPolicy(int argc, char **argv, int *argcnt,
			 const char *prefix, const char *envstr,
			 const char **policy);
int BENV_HwdescArgDebug(int argc, char **argv, int *argcnt,
			const char *prefix);
void BENV_HwdescGetPolicyArgUsage(FILE *fp, const char *prefix);

int BENV_HwdescArgDebugDecomp(int argc, char **argv, int *argcnt,
			      const char *prefix,
			      int *nnobj, int nobjs[], int *forcedebug);
void BENV_HwdescArgDebugDecompUsage(FILE *fp, const char *prefix);

int BENV_HwdescParmInit(hwdescParms *hwparm);
int BENV_HwdescParmUpdateFromEnv(hwdescParms *hwparm, const char *envbase);
int BENV_HwdescParmSetFromCvar(hwdescParms *hwparms);
int BENV_HwdescParmPrint(FILE *fp, hwdescParms *hwparm);
int BENV_HwdescCvarInit(void);
void BENV_HwdescCvarSet(const char *name, int value);
void BENV_HwdescCvarPrint(FILE *fp);


#endif
