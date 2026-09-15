#include "benvconf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mpi.h"
#include "hwdescnew.h"
#include "seq.h"
#include "benvutil.h"
#include "benvmpiutil.h"

/* Tests for the node information by policy routine.
 */

void printUsage(void);
void addnode(hwdescCtx *hwc);
hwdescCtx *copyhwdesc(hwdescCtx *hwc);
void hwdescprintinfo(FILE *fp, hwdescCtx *hwc);
void addinfofrompolicy(FILE *ofile, FILE *odfile, hwdescParms *hwparm,
		       hwdescCtx *hwc);
void AddConfigPreamble(FILE *ofile, const char *cname, hwdescCtx **hwnode);
void AddConfigPostable(FILE *ofile, FILE *odfile, const char *cname,
		       hwdescCtx *hwnode, int rc);
int updateafternodeandprint(FILE *fp, hwdescCtx *hwc);

/* Global for rank in COMM_WORLD */
int wrank;

int main(int argc, char **argv)
{
    int rc, wsize;
    /* hwdesc contexts with node configurations */
    hwdescParms hwparm;
    hwdescCtx *hwnodecmdline=0;
    /* An array of the configured hwdesc */
    static const char *policy = "B:B";
    int        nonnode, nsocket, nnuma, ncore;
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

    AddConfigPreamble(ofile, "command line config", &hwnodecmdline);
    hwparm.policy  = policy;
    hwparm.rank    = 127 /*wrank*/;
    hwparm.nonnode = 128; /*nonnode;*/
    hwparm.nobjs[1] = nsocket;
    hwparm.nobjs[2] = nnuma;
    hwparm.nobjs[3] = ncore;
    rc = BENV_HwdescNodeConfigParm(&hwparm, hwnodecmdline);
    AddConfigPostable(ofile, odfile, "command line config", hwnodecmdline,
		      rc);


/* ----------------------- Get Assignment -------------------- */

    addinfofrompolicy(ofile, odfile, &hwparm, hwnodecmdline);

    /* Clean up by freeing the information */
    if (odfilename) fclose(odfile);
    if (wrank == 0) {
	if (ofilename) fclose(ofile);
    }
    BENV_HwdescFreeCtx(hwnodecmdline);

//    fprintf(stderr, "About to finalize\n"); fflush(stderr);
    MPI_Finalize();
    return 0;
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
	fprintf(odfile, "Failed to create hwdesc For %s, rc=%d\n", cname, rc);
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

void printUsage(void)
{
    fprintf(stderr, "ninfo [ -o ofilenamem ] [ -od debugfilenaem ]\n\
\t[ -nsocket n ] [ -nnuma n ] [ -ncore n ] [ -usage ]\n");
// need to add BENV)DebugArgPrintUsage(stderr, prefix);
    // FIXME: I'm not sure this is the right set of flags
    BENV_HwdescArgPrintUsage(stderr, "", BENV_HWDESCARG_CORE);
}

