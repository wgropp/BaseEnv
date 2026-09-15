#include <stdio.h>
#include <stdlib.h>
#include "mpi.h"
#include "benvconf.h"
#include "benvutil.h"
#include "benvmpiutil.h"
#ifdef USE_OLD
#include "hwdesc.h"
#else
#include "hwdescnew.h"
#endif
#include "seq.h"
#include "benvdbg.h"

CDBGEDECL(PRINTHW);
/* Test the code to print the hwdesc. This is a parallel code (since the
   print code uses MPI communication) but creates hwdesc objects manually
*/

int main(int argc, char **argv)
{
    int wsize, wrank, nnobj, nobjs[3];
    int rc;
#ifdef USE_OLD
    hwdescCtx_t *hwc;
#else
    hwdescCtx *hwc;
#endif
    const char *str;
    const char *policy;

    MPI_Init(&argc, &argv);
/*
    BENV_HwdescCvarSet("debug", 1);
    BENV_HwdescCvarSet("debugrank", MPI_ANY_SOURCE);
*/
    MPI_Comm_size(MPI_COMM_WORLD, &wsize);
    MPI_Comm_rank(MPI_COMM_WORLD, &wrank);

    BENV_DebugPostMPIInit();

    for (int i=1; i<argc; i++) {
	rc = BENV_HwdescArgDebug(argc, argv, &i, "");
	BENV_ARGCHECK(rc,"error in hwdesc debug options",return 1);
	rc = BENV_DebugArgCommon(argc, argv, &i);
	BENV_ARGCHECK(rc,"error in debug options",return 1);
	if (wrank == 0) {
	    fprintf(stderr, "Unrecognized option %s\n", argv[i]);
	    fflush(stderr);
	    MPI_Abort(MPI_COMM_WORLD, 1);
	}
	return 1;  /* should not reach here */
    }

#ifndef USE_OLD
    hwc = BENV_HwdescCreateCtx(8);
#endif
    /* 4 cores/socket, 2 sockets per node, so nodes = wsize / 8 */
    nobjs[0] = wsize / 8; // nodes
    nobjs[1] = 2;        // sockets
    nnobj    = 2;
    if (nobjs[0] <= 0) {
	nobjs[0] = 1;
    }
    policy   = "B:C";
    rc = BENV_HwdescGetDescFromPolicy(MPI_COMM_WORLD, policy, wsize, wrank,
				      nnobj, nobjs,
#ifdef USE_OLD
				      &hwc
#else
	hwc
#endif
	);

    if (rc != 0) {
	fprintf(stderr, "Failed to get hw from policy!\n");
	MPI_Abort(MPI_COMM_WORLD, 1);
    }
    if (wrank == 0) {
	int nlevel;
#ifdef USE_OLD
	nlevel = hwc->hwlevel;
#else
	nlevel = hwc->nlevel;
#endif
	printf("Created hw from policy %s with %d levels (hw objs[%d,%d])\n",
	       policy, nlevel, nobjs[0], nobjs[1]);
	fflush(stdout);
    }
    /* First, each process prints its hw to check that it was constructed
       properly */
    BENV_SeqBegin(MPI_COMM_WORLD);
#if 0
    for (int i=0; i<hwc->hwlevel; i++) {
	printf("Comm = %ld, desc=%s\n", (long)hw[i].comm, hw[i].descstr);
	printf("nDistinct = %d and idx = %d\n", hw[i].nDistinct, hw[i].idx);
    }
    fflush(stdout);
#endif
    printf("hw for rank %d\n", wrank);
#ifdef USE_OLD
    BENV_HwdescPrintCtxLocal(stdout, hwc, "");
#else
    BENV_HwdescPrintLocal(stdout, hwc, "");
#endif
    fflush(stdout);
    BENV_SeqEnd(MPI_COMM_WORLD);

    MPI_Barrier(MPI_COMM_WORLD);
    if (wrank == 0) {
	printf("Output hw array from each process\n");
	fflush(stdout);
    }

    /* Now, print the combined version */
    str = BENV_HwdescToStr(hwc);

    if (wrank == 0) {
	printf("Hwdesc from string:\n");
	puts(str);
	fflush(stdout);
	printf("Hwdesc from print all:\n");
    }

    BENV_HwdescPrintAll(stdout, MPI_COMM_NULL, hwc, 0);

    MPI_Barrier(MPI_COMM_WORLD);
    BENV_HwdescFreeCtx(hwc);
#ifndef USE_OLD
    hwc = BENV_HwdescCreateCtx(8);
#endif

    nobjs[0] = wsize / 8; // nodes
    nobjs[1] = 2;        // sockets
    policy   = "C(2):C";
    rc = BENV_HwdescGetDescFromPolicy(MPI_COMM_WORLD, policy, wsize, wrank,
				      nnobj, nobjs,
#ifdef USE_OLD
				      &hwc
#else
	hwc
#endif
	);

    if (rc != 0) {
	fprintf(stderr, "Failed to get hw from policy!\n");
	MPI_Abort(MPI_COMM_WORLD, 1);
    }
    if (wrank == 0) {
	int nlevel;
#ifdef USE_OLD
	nlevel = hwc->hwlevel;
#else
	nlevel = hwc->nlevel;
#endif
	printf("Created hw from policy %s with %d levels (hw objs[%d,%d])\n",
	       policy, nlevel, nobjs[0], nobjs[1]);
	fflush(stdout);
    }
    /* Now, print the combined version */
    str = BENV_HwdescToStr(hwc);

    if (wrank == 0) {
	printf("Hwdesc from string:\n");
	puts(str);
	fflush(stdout);
	printf("Hwdesc from print all:\n");
    }

    BENV_HwdescPrintAll(stdout, MPI_COMM_NULL, hwc, 0);
    BENV_HwdescFreeCtx(hwc);

    MPI_Finalize();
    return 0;
}
