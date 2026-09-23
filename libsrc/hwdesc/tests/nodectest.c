#include <stdio.h>
#include "mpi.h"
#include "benvconf.h"
#include "benvutil.h"
#include "benvmpiutil.h"
#include "seq.h"
#include "hwdescnew.h"

/* Test the hwdesc codes. Starting with the convenience routine for
   node information */
int main(int argc, char **argv)
{
    int rc, nodenum, nodeidx, noderank, nodesize, wrank;
    int nodelevel, isexact;
    MPI_Comm nodecomm;
    hwdescParms parms;
    hwdescCtx *hwc;

    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &wrank);

    BENV_DebugPostMPIInit();
    BENV_HwdescCvarInit();

    /* Get default information from environment about hwdesc */
    BENV_HwdescParmInit(&parms);
    BENV_HwdescParmUpdateFromEnv(&parms, 0);

    for (int i=1; i<argc; i++) {
	rc = BENV_HwdescArg(argc, argv, &i, "-hw", &parms);
	BENV_ARGCHECK(rc,"error in hwdesc options",return 1);
	rc = BENV_DebugArgCommon(argc, argv, &i);
	BENV_ARGCHECK(rc,"error in debug options",return 1);
	if (wrank == 0) {
	    fprintf(stderr, "Unrecognized option %s\n", argv[i]);
	    fflush(stderr);
	    MPI_Abort(MPI_COMM_WORLD, 1);
	}
	return 1;  /* should not reach here */
    }

    rc = BENV_HwdescGetDescGeneral(MPI_COMM_WORLD, BENV_HWDESC_USE_ALL,
				   &parms, &hwc);
    rc = BENV_HwdescFindObject(hwc, BENV_HWDESC_NODE, &nodelevel, &isexact);
    if (nodelevel < 0) {
	fprintf(stderr, "Could not find node! in %d levels\n", hwc->nlevel);
	MPI_Abort(MPI_COMM_WORLD, 1);
    }
    else if (wrank == 0) {
	fprintf(stdout, "Nodelevel = %d\n", nodelevel);
	fflush(stdout);
    }
    /* Note: This might give information about the job if there is only
       one node */
    nodecomm = hwc->collinfo[nodelevel].objcomm;
    nodenum  = hwc->collinfo[nodelevel].nSiblings;
    nodeidx  = hwc->collinfo[nodelevel].siblingNum;

    MPI_Comm_rank(nodecomm, &noderank);
    MPI_Comm_size(nodecomm, &nodesize);
#define NEWOUTPUT 1
#ifdef NEWOUTPUT
    BENV_CollPrintFmt(stdout, MPI_COMM_WORLD, 0,
		      "Node %d of %d, process is %d of %d on node\n",
		      nodeidx, nodenum, noderank, nodesize);
#else
    fprintf(stdout, "Node %d of %d, process is %d of %d on node\n",
	    nodeidx, nodenum, noderank, nodesize); fflush(stdout);
#endif

    BENV_SeqBegin(MPI_COMM_WORLD);
    printf("Node information on process %d:\n", wrank);
    BENV_HwdescPrintLocal(stdout, hwc, "");
    fflush(stdout);
    BENV_SeqEnd(MPI_COMM_WORLD);
    if (wrank == 0) {
	printf("hwdesc:\n");
    }
    BENV_HwdescPrintAll(stdout, MPI_COMM_WORLD, hwc, 0);
    fflush(stdout);

    BENV_HwdescFreeCtx(hwc);
    MPI_Finalize();
    return 0;
}
