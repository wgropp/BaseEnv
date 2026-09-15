#include "benvconf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mpi.h"
#include "hwdescnew.h"
#include "seq.h"
#include "benvutil.h"
#include "benvmpiutil.h"

/* Tests for the node information routines.
   Tries all combinations of configuration and information calls.
   If the configuration is specified on the command line, that will be used
   instead of *any* of the available confiuration routines. This aids in
   testing other configurations.
 */

void printUsage(void);
void addnode(hwdescCtx *hwc);
hwdescCtx *copyhwdesc(hwdescCtx *hwc);
void hwdescprintinfo(FILE *fp, hwdescCtx *hwc);
void addInfoToConfig(FILE *ofile, FILE *odfile, hwdescParms *hwparm,
		     hwdescCtx *hwc);
void addinfofrompolicy(FILE *ofile, FILE *odfile, hwdescParms *hwparm,
		       hwdescCtx *hwc);
void AddConfigPreamble(FILE *ofile, const char *cname, hwdescCtx **hwnode);
void AddConfigPostable(FILE *ofile, FILE *odfile, const char *cname,
		       hwdescCtx *hwnode, int rc);
int updateafternodeandprint(FILE *fp, hwdescCtx *hwc);
void determinenodeparms(hwdescCtx *hwc, hwdescParms *parms);

/* Global for rank in COMM_WORLD */
int wrank;

int main(int argc, char **argv)
{
    int rc, wsize;
    void *context = 0;
    /* hwdesc contexts with node configurations */
    hwdescParms hwparm;
    hwdescCtx
#ifdef HAVE_SYSCTLBYNAME
	*hwnodesysctl=0,
#endif
#ifdef HAVE_HWLOC
	*hwnodehwloc=0,
#endif
#ifdef HAVE_CPUINFO
	*hwnodecpuinfo=0,
#endif
	*hwnodeenv=0, *hwnodecmdline=0;
    /* An array of the configured hwdesc */
    hwdescCtx *hwcconfig[10];
    static const char *policy = "B:B";
    int        onlypolicy=0;
    int        nonnode, nsocket, nnuma, ncore, configset=0;
    int        nconfl = 0;
    FILE       *ofile=0, *odfile=0;
    char       *ofilename=0, *odfilename=0;

    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &wrank);
    MPI_Comm_size(MPI_COMM_WORLD, &wsize);
    BENV_DebugPostMPIInit();

    /* TEMP: Need to know some things about configuration when using
       policy to assign processes */
    if (wsize >= 2)
	nonnode = wsize;
    else
	nonnode = 1;
    /* Set defaults, but update these */
    nsocket = 2;
    nnuma   = 1;
    ncore   = 8;

    /* get options */
    for (int i=1; i<argc; i++) {
	rc = BENV_HwdescArg(argc, argv, &i, "-hw", 0/* &parms*/);
	BENV_ARGCHECK(rc,"error in hwdesc options\n",return 1);
	rc = BENV_DebugArgCommon(argc, argv, &i);
	BENV_ARGCHECK(rc,"error in debug options\n",return 1);

	if (strcmp(argv[i], "-o") == 0) {
	    i++;
	    if (i < argc)
		ofilename = argv[i];
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
		odfilename = argv[i];
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
	    configset = 1;
	}
	else if (strcmp(argv[i], "-nnuma") == 0) {
	    i++;
	    rc = BENV_ArgGetint(i, argc, argv, &nnuma);
	    configset = 1;
	}
	else if (strcmp(argv[i], "-ncore") == 0) {
	    i++;
	    rc = BENV_ArgGetint(i, argc, argv, &ncore);
	    configset = 1;
	}
	else if (strcmp(argv[i], "-policy") == 0) {
	    i++;
	    policy = strdup(argv[i]);
	}
	else if (strcmp(argv[i], "-onlypolicy") == 0) {
	    onlypolicy = 1;
	}
	else {
	    if (wrank == 0) {
		fprintf(stderr, "Unrecognized option %s\n", argv[i]);
		fflush(stderr);
		MPI_Abort(MPI_COMM_WORLD, 1);
	    }
	}
    }

    /* process the options */
    if (wrank == 0) {
	if (ofilename) {
	    ofile = fopen(ofilename, "w");
	}
	else {
	    ofile = stdout;
	}
    }
    if (odfilename) {
	odfile = fopen(odfilename, "w");
    }
    else {
	odfile = stdout;
    }

/* ----------------------- Get Config -------------------- */

/* All configurations will start with a node entry */

#ifdef HAVE_HWLOC
    if (!configset) {
	AddConfigPreamble(ofile, "hwloc", &hwnodehwloc);
	rc = BENV_HwdescNodeConfigHwloc(context, hwnodehwloc);
	if (rc == 0) {
	    hwcconfig[nconfl++]     = hwnodehwloc;
	}
	AddConfigPostable(ofile, odfile, "hwloc", hwnodehwloc, rc);
    }
#endif

#ifdef HAVE_SYSCTLBYNAME
    if (!configset) {
	AddConfigPreamble(ofile, "sysctl", &hwnodesysctl);
	rc = BENV_HwdescNodeConfigSysctl(context, hwnodesysctl);
	if (rc == 0) {
	    hwcconfig[nconfl++]     = hwnodesysctl;
	}
	AddConfigPostable(ofile, odfile, "sysctl", hwnodesysctl, rc);
    }
#endif

#ifdef HAVE_CPUINFO
    if (!configset) {
	AddConfigPreamble(ofile, "cpuinfo", &hwnodecpuinfo);
	rc = BENV_HwdescNodeConfigCpuinfo(context, hwnodecpuinfo);
	if (rc == 0) {
	    hwcconfig[nconfl++]     = hwnodecpuinfo;
	}
	AddConfigPostable(ofile, odfile, "cpuinfo", hwnodecpuinfo, rc);
    }
#endif

    if (!configset) {
	AddConfigPreamble(ofile, "env variables", &hwnodeenv);
	rc = BENV_HwdescNodeConfigEnv(context, hwnodeenv);
	if (rc == 0) {
	    hwcconfig[nconfl++]     = hwnodeenv;
	}
	AddConfigPostable(ofile, odfile, "env variables", hwnodeenv, rc);
    }
    else {
	AddConfigPreamble(ofile, "command line config", &hwnodecmdline);
	hwparm.policy  = policy;
	hwparm.rank    = wrank;
	hwparm.nonnode = nonnode;
	hwparm.nobjs[1] = nsocket;
	hwparm.nobjs[2] = nnuma;
	hwparm.nobjs[3] = ncore;
	rc = BENV_HwdescNodeConfigParm(&hwparm, hwnodecmdline);
	if (rc == 0) {
	    hwcconfig[nconfl++]     = hwnodecmdline;
	}
	AddConfigPostable(ofile, odfile, "command line config", hwnodecmdline,
			  rc);

    }

/* ----------------------- Get Assignment -------------------- */

    /* Special case: Need to know the configuration (in part) to map
       rank to indices. */
    hwparm.policy  = policy;
    hwparm.rank    = wrank;
    hwparm.nonnode = nonnode;
#if 0
    /* These must be consistent with the configuration determined */
    // nobjs[0] == nnodes
    /* For each of the configurations, find the number of sockets and
       NUMA regions */
    if (!configset) {
	if (wrank == 0) {
	    fprintf(ofile, "About to find socket and numa values\n");
	    fflush(ofile);
	}
	for (int j=0; j<nconfl; j++) {
	    int nobjs[3], objidx[3];
	    for (int k=0; k<3; k++) {
		nobjs[k]  = -1;
		objidx[k] = -1;
	    }
	    /* FIXME: handle different returns for different
	       configurations */
	    nsocket = -1;
	    nnuma   = -1;
	    ncore   = -1;
	    BENV_HwdescNodeGetSockNumaCore(hwcconfig[j], nobjs, objidx);
	    if (nobjs[0] > 0) nsocket = nobjs[0];
	    if (nobjs[1] > 0) nnuma   = nobjs[1];
	    if (nobjs[2] > 0) ncore   = nobjs[2];
	}
	if (wrank == 0) {
	    fprintf(ofile, "found socket=%d, numa=%d, and core=%d values\n",
		    nsocket, nnuma, ncore);
	    fflush(ofile);
	}
    }
    hwparm.nobjs[1] = nsocket;
    hwparm.nobjs[2] = nnuma;
    hwparm.nobjs[3] = ncore;
#endif

    /* Fillin the assignment information in all available configurations */
    if (wrank == 0) {
	printf("Update %d configurations\n", nconfl); fflush(stdout);
    }
    for (int j=0; j<nconfl; j++) {
	if (wrank == 0) {
	    fprintf(ofile, "Adding info to config from %s\n",
		    hwcconfig[j]->source);
	    fflush(ofile);
	}
	if (!configset) {
	    if (wrank == 0) {
		fprintf(ofile, "About to find socket and numa values\n");
		fflush(ofile);
	    }
	    determinenodeparms(hwcconfig[j], &hwparm);
	    if (wrank == 0) {
		fprintf(ofile, "found socket=%d, numa=%d, and core=%d values\n",
			nsocket, nnuma, ncore);
		fflush(ofile);
	    }
	}
	/* For debugging policy assignment */
	if (!onlypolicy) {
	    addInfoToConfig(ofile, odfile, &hwparm, hwcconfig[j]);
	}
	addinfofrompolicy(ofile, odfile, &hwparm, hwcconfig[j]);
    }

    /* Clean up by freeing the information */
    if (odfilename) fclose(odfile);
    if (wrank == 0) {
	if (ofilename) fclose(ofile);
//	fprintf(stdout, "About to free %d config descriptions...\n", nconfl);
//	fflush(stdout);
    }
    for (int i=0; i<nconfl; i++) {
	//fprintf(stdout, "Freeing hccconfig[%d]:\n", i); fflush(stdout);
	BENV_HwdescFreeCtx(hwcconfig[i]);
	//fprintf(stdout, "Returned from FreeCtx for hwcconfig[%d]\n", i); fflush(stdout);
    }

//    fprintf(stderr, "About to finalize\n"); fflush(stderr);
    MPI_Finalize();
    return 0;
}

/* For each available info method, add it to a COPY of the hwdesc context */
void addInfoToConfig(FILE *ofile, FILE *odfile, hwdescParms *hwparm,
		     hwdescCtx *hwc)
{
#if defined(HAVE_GETCPU) || defined(HAVE_SCHED_GETCPU)
    hwdescCtx *hwccopy;
    int rc;
#endif

#ifdef HAVE_GETCPU
    if (wrank == 0) {
        fprintf(ofile, "Using getcpu for assignment\n");
	fflush(ofile);
    }
    hwccopy = copyhwdesc(hwc);
    BENV_SeqBegin(MPI_COMM_WORLD);
    rc = BENV_HwdescNodeInfoGetcpu(0, hwccopy);
    if (rc == 0) {
	BENV_HwdescNodeNormalize(hwccopy);
	if (wrank == 0)
	    fprintf(odfile, "\nAssignment from getcpu:\n");
	BENV_HwdescPrintLocal(odfile, hwccopy, "getcpu");
	fflush(odfile);
    }
    else {
	fprintf(stderr, "info_getcpu returned rc=%d\n", rc);
    }
    BENV_SeqEnd(MPI_COMM_WORLD);
    if (rc == 0) {
	rc = updateafternodeandprint(ofile, hwccopy);
	if (rc) {
	    if (wrank == 0) {
		fprintf(stderr, "After updateafternode for Getcpu, rc=%d\n",
			rc);
		BENV_HwdescPrintLocal(stderr, hwccopy, "");
	    }
	}
    }
    BENV_HwdescFreeCtx(hwccopy);
#endif
#ifdef HAVE_SCHED_GETCPU
    if (wrank == 0) {
        fprintf(ofile, "Using sched_getcpu for assignment\n");
	fflush(ofile);
    }
    hwccopy = copyhwdesc(hwc);
    BENV_SeqBegin(MPI_COMM_WORLD);
    rc = BENV_HwdescNodeInfoSchedgetcpu(0, hwccopy);
    if (rc == 0) {
	BENV_HwdescNodeNormalize(hwccopy);
	if (wrank == 0)
	    fprintf(odfile, "\nAssignment from schedgetcpu:\n");
	BENV_HwdescPrintLocal(odfile, hwccopy, "schedgetcpu");
	fflush(odfile);
    }
    else {
	fprintf(stderr, "info_schedgetcpu returned rc=%d\n", rc);
    }
    BENV_SeqEnd(MPI_COMM_WORLD);
    if (rc == 0) {
	rc = updateafternodeandprint(ofile, hwccopy);
	if (rc) {
	    if (wrank == 0) {
		fprintf(stderr, "After updateafternode for Sched_getcpu, rc=%d\n",
			rc);
		BENV_HwdescPrintLocal(stderr, hwccopy, "");
	    }
	}
    }
    BENV_HwdescFreeCtx(hwccopy);
#endif
}

void addinfofrompolicy(FILE *ofile, FILE *odfile, hwdescParms *hwparm,
		       hwdescCtx *hwc)
{
    int rc;
    hwdescCtx *hwccopy=0;

    /* Scheduler policy is last because we need to know the node configuration.
       There is a default (see above), but if any other configuration method
       succeeded, use those results (nsocket, nnuma) */
    if (wrank == 0) {
        fprintf(ofile, "Using sched policy for assignment\n");
	fflush(ofile);
    }
    hwccopy = copyhwdesc(hwc);
    BENV_SeqBegin(MPI_COMM_WORLD);

    if (wrank == 0) {
	/* Debugging */
	fprintf(ofile, "\nAssignment from policy (policy=%s, nsocket=%d, nnuma=%d, np=%d):\n",
		hwparm->policy, hwparm->nobjs[1], hwparm->nobjs[2], hwparm->nonnode);
	fflush(ofile);
    }
    rc = BENV_HwdescNodeInfoPolicy(hwparm, hwccopy);
    if (rc == 0) {
	BENV_HwdescNodeNormalize(hwccopy);
	if (wrank == 0) {
	    fprintf(odfile, "\nAssignment from policy (nsocket=%d, nnuma=%d, np=%d):\n",
		    hwparm->nobjs[1], hwparm->nobjs[2], hwparm->nonnode);
	    fflush(odfile);
	}
	BENV_HwdescPrintLocal(odfile, hwccopy, "policy");


	fflush(odfile);
    }
    else {
	fprintf(stderr, "info_policy returned rc=%d\n", rc);
    }
    BENV_SeqEnd(MPI_COMM_WORLD);
    if (rc == 0) {
	if (wrank == 0) {
	    fprintf(odfile, "Output by rank and by object hierarchy\n");
	}
	BENV_HwdescPrintTuple(odfile, MPI_COMM_WORLD, hwccopy, 3);
    }
    if (rc == 0) {
	rc = updateafternodeandprint(ofile, hwccopy);
	if (rc) {
	    if (wrank == 0) {
		fprintf(stderr, "After updateafternode for Policy, rc=%d\n",
			rc);
		BENV_HwdescPrintLocal(stderr, hwccopy, "");
	    }
	}
    }
    //fprintf(stderr, "Freeing copy of hwc at %p\n", hwccopy); fflush(stdout);
    BENV_HwdescFreeCtx(hwccopy);
}

/* Print just the node information*/
void hwdescprintinfo(FILE *fp, hwdescCtx *hwc)
{
    int i, nobjs[3], objidx[3];
    static const char *objname[3] = { "socket", "NUMA", "core" };

    for (i=0; i<3; i++) {
	nobjs[i] = -1;
	objidx[i] = -1;
    }
    BENV_HwdescNodeGetSockNumaCore(hwc, nobjs, objidx);
    fprintf(fp, "ninfo hwc has %d levels\n", hwc->nlevel);
    for (i=0; i<3; i++) {
	if (nobjs[i] > 0) {
	    fprintf(fp, "%s(%d)", objname[i], nobjs[i]);
	    if (objidx[i] >= 0)
		fprintf(fp, "%d,", objidx[i]);
	}
    }
    fputc('\n', fp);
    fprintf(fp, "end of ninfo print\n"); fflush(fp);
}

hwdescCtx *copyhwdesc(hwdescCtx *hwc)
{
    hwdescCtx *newhwc;
    int i;

    newhwc = (hwdescCtx *)malloc(sizeof(hwdescCtx));
    *newhwc = *hwc;
    newhwc->collinfo =
	(hwdescMPIInfo *)malloc(hwc->nAllocated * sizeof(hwdescMPIInfo));
    newhwc->objinfo =
	(hwdescObjInfo *)malloc(hwc->nAllocated * sizeof(hwdescObjInfo));
    if (hwc->source)
	newhwc->source = strdup(newhwc->source);

    for (i=0; i<hwc->nlevel; i++) {
	newhwc->objinfo[i] = hwc->objinfo[i];
	/* Clear out any assignments to the node */
	if (newhwc->objinfo[i].kind == BENV_HWDESC_SOCKET ||
	    newhwc->objinfo[i].kind == BENV_HWDESC_NUMA ||
	    newhwc->objinfo[i].kind == BENV_HWDESC_CORE) {
	    newhwc->objinfo[i].asrc = BENV_HWDESC_ASSIGN_UNKNOWN;
	    newhwc->objinfo[i].objidx     = -1;
	    newhwc->objinfo[i].nodeobjidx = -1;
	    newhwc->objinfo[i].rawobjidx  = -1;
	}
	if (hwc->objinfo[i].othersrc)
	    newhwc->objinfo[i].othersrc = strdup(hwc->objinfo[i].othersrc);
	newhwc->collinfo[i] = hwc->collinfo[i];
	if (hwc->collinfo[i].descstr)
	    newhwc->collinfo[i].descstr = strdup(hwc->collinfo[i].descstr);
	if (hwc->collinfo[i].leadersInParent) {
	    int *lcopy, j;
	    lcopy = (int *)malloc(hwc->collinfo[i].nSiblings*sizeof(int));
	    for (j=0; i<hwc->collinfo[i].nSiblings; j++)
		lcopy[j] = hwc->collinfo[i].leadersInParent[j];
	    newhwc->collinfo[i].leadersInParent = lcopy;
	}
    }

    return newhwc;
}

/* For the configure steps, we provide calls around the config call.
 Note that they start and end a sequential section */

void AddConfigPreamble(FILE *ofile, const char *cname, hwdescCtx **hwnode)
{
    if (wrank == 0) {
        fprintf(ofile, "Using %s for config\n", cname);
	fflush(ofile);
    }
    *hwnode = BENV_HwdescCreateCtx(8);
    /* Add a node to all configurations. This is needed if there are
       multiple sockets per node */
    addnode(*hwnode);
    BENV_SeqBegin(MPI_COMM_WORLD);
}

void AddConfigPostable(FILE *ofile, FILE *odfile, const char *cname,
		       hwdescCtx *hwnode, int rc)
{
    if (rc == 0) {
	// TEMP!!!
	fprintf(odfile,"PrintLocal before NodeNormalizeConfig\n");
	BENV_HwdescPrintLocal(odfile, hwnode, cname);
	fflush(odfile);

	BENV_HwdescNodeNormalizeConfig(hwnode);
	if (wrank == 0) {
	    fprintf(odfile, "\nResults from %s:\n", cname);
	    fprintf(ofile, "\nResults from %s (rank 0):\n", cname);
	    hwdescprintinfo(ofile, hwnode);
	}
	BENV_HwdescPrintLocal(odfile, hwnode, cname);
	fflush(odfile);
    }
    else {
	/* Expect that all processes fail if any fail */
	if (wrank == 0) {
	    fprintf(odfile, "Failed to create hwdesc For %s, rc=%d\n",
		    cname, rc);
	}
    }
    BENV_SeqEnd(MPI_COMM_WORLD);
    if (hwnode) {
	int nvalid;
	rc = BENV_HwdescCheckConsistentSizes(hwnode, MPI_COMM_WORLD,
					     0, -1, 1, &nvalid);
	if (wrank == 0 && rc != 0) {
	    /* A problem */
	    fprintf(ofile, "!Inconsistent configuration for %s (nvalid=%d, rc=%d)\n", cname, nvalid, rc);
	}
    }
}

int updateafternodeandprint(FILE *fp, hwdescCtx *hwc)
{
    int rc;

    rc = BENV_HwdescNodeSetCollinfo(MPI_COMM_WORLD, hwc);
    if (rc != 0) {
	fprintf(stderr, "NodeSetCollinfo returned rc=%d\n", rc);
    }
    else {
	//printf("Validating hwc from config...\n");
	rc = BENV_HwdescValidate(MPI_COMM_WORLD, hwc);
	if (rc == 0)
	    BENV_HwdescPrintTuple(fp, MPI_COMM_WORLD, hwc, 3);
	else {
	    fprintf(stderr, "rc = %d from HwdescValidate\n", rc);
	}
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

/* determine the node socket/numa/core from the configuration. This is
   needed to ensure the parms matches the configuration (needed especially
   by the policy assignment) */
void determinenodeparms(hwdescCtx *hwc, hwdescParms *parms)
{
    int nsocket, nnuma, ncore;
    int nobjs[3], objidx[3];

    for (int k=0; k<3; k++) {
	nobjs[k]  = -1;
	objidx[k] = -1;
    }
    nsocket = -1;
    nnuma   = -1;
    ncore   = -1;
    BENV_HwdescNodeGetSockNumaCore(hwc, nobjs, objidx);
    if (nobjs[0] > 0) nsocket = nobjs[0];
    if (nobjs[1] > 0) nnuma   = nobjs[1];
    if (nobjs[2] > 0) ncore   = nobjs[2];

    parms->nobjs[1] = nsocket;
    parms->nobjs[2] = nnuma;
    parms->nobjs[3] = ncore;
}

void printUsage(void)
{
    fprintf(stderr, "ninfo [ -o ofilenamem ] [ -od debugfilenaem ]\n\
\t[ -nsocket n ] [ -nnuma n ] [ -ncore n ] [ -onlypolicy ] [ -usage ]\n");
// need to add BENV)DebugArgPrintUsage(stderr, prefix);
    // FIXME: I'm not sure this is the right set of flags
    BENV_HwdescArgPrintUsage(stderr, "", BENV_HWDESCARG_CORE);
}

