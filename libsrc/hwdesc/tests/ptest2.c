#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mpi.h"
#include "benvconf.h"
#include "benvutil.h"
#include "hwdescnew.h"

/* Test the routines to create configuration information about nodes,
   and to combine those with assignment information from scheduler
   policy */

hwdescCtx *createHwnode(int nsocket, int nnuma, int nonnode);
void clearHwnodeIdx(hwdescCtx *hwnode);
void printtuple(FILE *, int rank, hwdescCtx *hwnode);

int main(int argc, char **argv)
{
    hwdescParms parms;
    hwdescCtx   *hwnode;
    int np = 128;

    /* Initialize parms */
    parms.policy   = "B:B";
    parms.nnobj    = 3;
    parms.nobjs[0] = 1; // number of nodes
    parms.nobjs[1] = 2; // number of sockets
    parms.nobjs[2] = 4; // number of NUMA regions on a socket
    parms.nonnode  = np;

    /* Create a hwnode configuration that matches the parms */
    hwnode = createHwnode(parms.nobjs[1], parms.nobjs[2], parms.nonnode);

    printf("Node config is:\n");
    BENV_HwdescPrintLocal(stdout, hwnode, "");
    fputc('\n', stdout);

    printf("For policy %s\n", parms.policy);
    for (int rank=0; rank<np; rank++) {
	parms.rank = rank;
	BENV_HwdescNodeInfoPolicy(&parms, hwnode);
	printtuple(stdout, rank, hwnode);
    }
    // Keep this only for comparison with older ptest.c program. Same
    // output as above
    printf("\nFor each valid rank:\n");
    for (int rank=0; rank<np; rank++) {
	parms.rank = rank;
	clearHwnodeIdx(hwnode);
	BENV_HwdescNodeInfoPolicy(&parms, hwnode);
	printtuple(stdout, rank, hwnode);
    }

    parms.policy = "B:C:C:C";
    printf("\nFor policy %s\n", parms.policy);
    for (int rank=0; rank<np; rank++) {
	parms.rank = rank;
	clearHwnodeIdx(hwnode);
	BENV_HwdescNodeInfoPolicy(&parms, hwnode);
	printtuple(stdout, rank, hwnode);
    }

    parms.policy = "C:C:C:C";
    printf("\nFor policy %s\n", parms.policy);
    for (int rank=0; rank<np; rank++) {
	parms.rank = rank;
	clearHwnodeIdx(hwnode);
	BENV_HwdescNodeInfoPolicy(&parms, hwnode);
	printtuple(stdout, rank, hwnode);
    }

    return 0;
}

hwdescCtx *createHwnode(int nsocket, int nnuma, int nonnode)
{
    hwdescCtx *hwnode;
    hwdescObjInfo *objinfo;

    hwnode = BENV_HwdescCreateCtx(8);

    hwnode->nlevel = 3;
    hwnode->source = strdup("createHwnode");

    objinfo = hwnode->objinfo;

    objinfo[0].nobj    = nsocket;
    objinfo[0].objidx  = -1;
    objinfo[0].kind    = BENV_HWDESC_SOCKET;
    objinfo[0].csrc    = BENV_HWDESC_CONFIG_UNKNOWN;

    objinfo[1].nobj    = nnuma;                      /* Number per socket */
    objinfo[1].objidx  = -1;
    objinfo[1].kind    = BENV_HWDESC_NUMA;
    objinfo[1].csrc    = BENV_HWDESC_CONFIG_UNKNOWN;

    objinfo[2].nobj    = nonnode / nnuma / nsocket;   /* Number per NUMA */;
    objinfo[2].objidx  = -1;
    objinfo[2].kind    = BENV_HWDESC_CORE;
    objinfo[2].csrc    = BENV_HWDESC_CONFIG_UNKNOWN;
    return hwnode;
}

void clearHwnodeIdx(hwdescCtx *hwnode)
{
    hwdescObjInfo *objinfo;

    objinfo = hwnode->objinfo;
    for (int i=0; i<hwnode->nlevel; i++) {
	objinfo[i].objidx = -1;
    }
}

void printtuple(FILE *fp, int rank, hwdescCtx *hwc)
{
    int idxs[16], i;

    if (hwc->nlevel > 16) {
	fprintf(stderr, "Too many levels (%d) in hw description!\n",
		hwc->nlevel);
	abort();
    }
    for (i=0; i<hwc->nlevel; i++)
	idxs[i] = hwc->objinfo[i].objidx;

    fprintf(fp, "%d:", rank);
    BENV_PrintIntList(fp, hwc->nlevel, idxs, 1);
}
