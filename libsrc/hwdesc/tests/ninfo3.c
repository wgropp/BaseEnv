#include "benvconf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mpi.h"
#include "hwdescnew.h"
#include "seq.h"
#include "benvutil.h"
#include "benvmpiutil.h"

/* Test for the node info routines, both configuration and assignment

   For the various combinations, run the configuration and assignment,
   and print out, in compact form, the results for the selected choices.

   Uses the Hwdesc printall routine to provide a concise output
 */

int configCtx(hwdescCtx *hwc, int i, hwdescParms *parms);
int assignCtx(hwdescCtx *hwc, int j, hwdescParms *parms);
void addnode(hwdescCtx *hwc);
void clearassign(hwdescCtx *hwc);
void getOptions(int argc, char **argv, hwdescParms *parms, char **ofilename,
    char **odfilename);
const char *configmethod(int);
const char *assignmethod(int);
void printconfig(hwdescCtx *hwc);
void printassign(hwdescCtx *hwc);

#define MAX_ASSIGN 10
#define MAX_CONFIG 10
void printUsage(void);
static int showprenormalize=1;

int main(int argc, char **argv)
{
    int i, j, wrank;
    hwdescParms hwparm;
    char *ofilename = 0, *odfilename = 0;
    char *savesource=0;

    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &wrank);

    getOptions(argc, argv, &hwparm, &ofilename, &odfilename);

    /* Process arguments */
    for (i=0; i<MAX_CONFIG; i++) {
	hwdescCtx *hwc;
	int       rc;

	hwc = BENV_HwdescCreateCtx(8);
	addnode(hwc);
	rc = configCtx(hwc, i, &hwparm);
	if (rc != 0) {
	    BENV_HwdescFreeCtx(hwc);
	    if (rc == 1) continue;
	    else break;
	}
	if (showprenormalize) {
	    if (wrank == 0) {
		printf("Config (pre normalize) with %s\n", configmethod(i));
	    }
	    printconfig(hwc);
	}
	BENV_HwdescNodeNormalizeConfig(hwc);
	if (wrank == 0) {
	    printf("Config with %s\n", configmethod(i));
	}
	printconfig(hwc);
	if (hwc->source) {
	    if (savesource) free(savesource);
	    savesource = strdup(hwc->source);
	}
	else
	    savesource = 0;

	for (j=0; j<MAX_ASSIGN; j++) {
	    /* Special case. Hwloc can do both configure and assignment */
	    if (i != 0 || j != 0)
		clearassign(hwc);
	    rc = assignCtx(hwc, j, &hwparm);
	    if (rc == 1) continue;
	    else if (rc == -1) break;
	    if (showprenormalize) {
		if (wrank == 0) {
		    printf("Config with %s and assign (pre normalize) with %s\n",
			   configmethod(i), assignmethod(j));
		}
		printassign(hwc);
	    }
	    BENV_HwdescNodeNormalizeAssign(hwc);
	    if (wrank == 0) {
		printf("Config with %s and assign with %s\n",
		       configmethod(i), assignmethod(j));
	    }
	    printassign(hwc);
	    //BENV_HwdescPrintAll(stdout, MPI_COMM_WORLD, hwc, 0);
	    /* Clean up the hwc->source */
	    if (hwc->source) {
		free((char *)hwc->source);
		hwc->source = 0;
	    }
	    if (savesource) {
		hwc->source = savesource;
		savesource = 0;
	    }
	};
	BENV_HwdescFreeCtx(hwc);
    }
    MPI_Finalize();
    return 0;
}

/* Returns 0 on success, 1 on failure, -1 on no more methods available */
int configCtx(hwdescCtx *hwc, int i, hwdescParms *hwparm)
{
    int rc=1;

    switch (i) {
    case 0:
#ifdef HAVE_HWLOC
	rc = BENV_HwdescNodeConfigHwloc(0, hwc);
#endif
	break;
    case 1:
#ifdef HAVE_SYSCTLBYNAME
	rc = BENV_HwdescNodeConfigSysctl(0, hwc);
#endif
	break;
    case 2:
#ifdef HAVE_CPUINFO
	rc = BENV_HwdescNodeConfigCpuinfo(0, hwc);
#endif
	break;
    case 3:
	rc = BENV_HwdescNodeConfigEnv(0, hwc);
	break;
    case 4:
	rc = BENV_HwdescNodeConfigParm(hwparm, hwc);
	break;
    default:
	rc = -1;
    }
    return rc;
}
int assignCtx(hwdescCtx *hwc, int j, hwdescParms *hwparm)
{
    int rc=1;

    switch (j) {
    case 0:
#ifdef HAVE_HWLOC
/* Only provide hwloc assignment when hwloc provides the configuration */
/* FIXME: Look for asrc == BENV_HWDESC_ASSIGN_HWLOC */
	for (int i=1; i<hwc->nlevel; i++) {
	    if (hwc->objinfo[i].asrc == BENV_HWDESC_ASSIGN_HWLOC) {
		rc = 0;
		break;
	    }
	}
#endif
	break;
    case 1:
#ifdef HAVE_GETCPU
	rc = BENV_HwdescNodeInfoGetcpu(0, hwc);
#endif
	break;
    case 2:
#ifdef HAVE_SCHED_GETCPU
	rc = BENV_HwdescNodeInfoSchedgetcpu(0, hwc);
#endif
	break;
    case 3:
	rc = BENV_HwdescNodeInfoPolicy(hwparm, hwc);
	break;
    default:
	rc = -1;
    }
    return rc;
}

void addnode(hwdescCtx *hwc)
{
    hwc->objinfo[0].nobj             = 1;
    hwc->objinfo[0].objidx           = 0;
    hwc->objinfo[0].nodenobj         = 1;
    hwc->objinfo[0].nodeobjidx       = 0;
    hwc->objinfo[0].kind             = BENV_HWDESC_NODE;
//    hwc->objinfo[0].csrc            = 
//   hwc->objinfo[0].asrc            =
    hwc->objinfo[0].othersrc         = NULL;
    hwc->collinfo[0].objcomm         = MPI_COMM_WORLD;
    hwc->collinfo[0].nSiblings       = 1;
    hwc->collinfo[0].siblingNum      = 0;
    hwc->collinfo[0].leadersInParent = 0;
    hwc->collinfo[0].allsizeone      = 0;
    hwc->collinfo[0].descstr         = NULL;
    hwc->nlevel                      = 1;
}

void clearassign(hwdescCtx *hwc)
{
    int i;

    for (i=1; i<hwc->nlevel; i++) {
	hwc->objinfo[i].asrc = BENV_HWDESC_ASSIGN_UNKNOWN;
	hwc->objinfo[i].objidx = -1;
	hwc->objinfo[i].nodeobjidx = -1;
    }
}

void getOptions(int argc, char **argv, hwdescParms *hwparm, char **ofilename,
    char **odfilename)
{
    int rc, wrank, nonnode=1, nsocket=1, nnuma=1, ncore=1;
    static const char *policy = "B:B";

    MPI_Comm_rank(MPI_COMM_WORLD, &wrank);
    MPI_Comm_size(MPI_COMM_WORLD, &nonnode);
    for (int i=1; i<argc; i++) {
	rc = BENV_HwdescArg(argc, argv, &i, "-hw", 0/* &parms*/);
	BENV_ARGCHECK(rc,"error in hwdesc options\n",MPI_Abort(MPI_COMM_WORLD,1));
	rc = BENV_DebugArgCommon(argc, argv, &i);
	BENV_ARGCHECK(rc,"error in debug options\n",MPI_Abort(MPI_COMM_WORLD,1));

	if (strcmp(argv[i], "-o") == 0) {
	    i++;
	    if (i < argc)
		*ofilename = argv[i];
	    else {
		if (wrank == 0) {
		    fprintf(stderr, "-o missing value\n");
		    fflush(stderr);
		    MPI_Abort(MPI_COMM_WORLD, 1);
		}
	    }
	}
	else if (strcmp(argv[i], "-od") == 0) {
	    i++;
	    if (i < argc)
		*odfilename = argv[i];
	    else {
		if (wrank == 0) {
		    fprintf(stderr, "-od missing value\n");
		    fflush(stderr);
		    MPI_Abort(MPI_COMM_WORLD, 1);
		}
	    }
	}
	else if (strcmp(argv[i], "-nsocket") == 0) {
	    i++;
	    rc = BENV_ArgGetint(i, argc, argv, &nsocket);
	}
	else if (strcmp(argv[i], "-nnuma") == 0) {
	    i++;
	    rc = BENV_ArgGetint(i, argc, argv, &nnuma);
	}
	else if (strcmp(argv[i], "-ncore") == 0) {
	    i++;
	    rc = BENV_ArgGetint(i, argc, argv, &ncore);
	}
	else if (strcmp(argv[i], "-policy") == 0) {
	    i++;
	    policy = strdup(argv[i]);
	}
	else if (strcmp(argv[i], "-usage") == 0) {
	    if (wrank == 0)
		printUsage();
	}
	else if (strcmp(argv[i], "-showprenormalize") == 0) {
	    showprenormalize = 1;
	}
	else if (strcmp(argv[i], "-noshowprenormalize") == 0) {
	    showprenormalize = 0;
	}
	else {
	    if (wrank == 0) {
		fprintf(stderr, "Unrecognized option %s\n", argv[i]);
		fflush(stderr);
		MPI_Abort(MPI_COMM_WORLD, 1);
	    }
	}
    }

    hwparm->policy  = policy;
    hwparm->rank    = wrank;
    hwparm->nonnode = nonnode;
    hwparm->nobjs[1] = nsocket;
    hwparm->nobjs[2] = nnuma;
    hwparm->nobjs[3] = ncore;

}

const char *configmethod(int i)
{
    const char *method="<unknown>";
    switch (i) {
    case 0: method = "hwloc"; break;
    case 1: method = "sysctl"; break;
    case 2: method = "cpuinfo"; break;
    case 3: method = "env"; break;
    case 4: method = "Command line"; break;
    default: method = "Unknown!"; break;
    }
    return method;
}
const char *assignmethod(int j)
{
    const char *method="<unknown>";
    switch (j) {
    case 0: method = "hwloc"; break;
    case 1: method = "getcpu"; break;
    case 2: method = "sched_getcpu"; break;
    case 3: method = "policy"; break;
    default: method = "Unknown!"; break;
    }
    return method;
}

void printconfig(hwdescCtx *hwc)
{
    int i, rc, nvalid;
    rc = BENV_HwdescCheckConsistentSizes(hwc, MPI_COMM_WORLD,
					     0, -1, 1, &nvalid);
    if (rc != 0) return;
    //    printf("nvalid = %d, hwc->nlevel = %d\n", nvalid, hwc->nlevel);
    for (i=0; i<nvalid; i++) {
	BENV_CollPrintFmt(stdout, MPI_COMM_WORLD, 0,
			  "%d: %s (count) %d %d %d\n", i,
			  BENV_HwdescKindStr(hwc->objinfo[i].kind),
			  hwc->objinfo[i].nodenobj, hwc->objinfo[i].nobj,
			  hwc->objinfo[i].rawnobj);
    }
}
void printassign(hwdescCtx *hwc)
{
    int i, rc, nvalid, wrank;

    MPI_Comm_rank(MPI_COMM_WORLD, &wrank);
    rc = BENV_HwdescCheckConsistentSizes(hwc, MPI_COMM_WORLD,
					     0, -1, 1, &nvalid);
    if (rc != 0) return;
    //    printf("nvalid = %d, hwc->nlevel = %d\n", nvalid, hwc->nlevel);
    if (nvalid != hwc->nlevel && wrank == 0) {
	printf("Fewer consistent levels (%d) than total levels (%d) found\n",
	       nvalid, hwc->nlevel);
    }
    for (i=0; i<nvalid; i++) {
	BENV_CollPrintFmt(stdout, MPI_COMM_WORLD, 0,
			  "%d: %s (idx) %d %d %d\n", i,
			  BENV_HwdescKindStr(hwc->objinfo[i].kind),
			  hwc->objinfo[i].nodeobjidx, hwc->objinfo[i].objidx,
			  hwc->objinfo[i].rawobjidx);
	fflush(stdout);
    }
}

void printUsage(void)
{
    fprintf(stderr, "\
ninfo3 [ -nsocket n ] [ -nnuma n ] [ -ncore n] [-policy str ] [ -usage ]\n\
\t[ -o ofilenamem ] [ -od debugfilenaem ] [ -showprenormalize ]\n\
\t[ -noshowprenormalize ] \n");
// need to add BENV)DebugArgPrintUsage(stderr, prefix);
    // FIXME: I'm not sure this is the right set of flags
    BENV_HwdescArgPrintUsage(stderr, "", BENV_HWDESCARG_CORE);
}
