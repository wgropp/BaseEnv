#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mpi.h"
#include "benvconf.h"
#ifdef USE_OLD
#include "hwdesc.h"
#include "hwdescimpl.h"
#else
#include "hwdescnew.h"
#include "hwdescimpl2.h"
#endif

int main(int argc, char **argv)
{
    int r, np, ppn, objs[3], bsize[3], nobjs, idxs[4], rc=0;

    /* Defaults */
    np = 48;
    ppn = 16;
    objs[1] = 2;   /* spn */
    objs[2] = 4;   /* numa per sock */ /* and 2 ranks per numa */
    nobjs   = 3;

    /* TODO: Add arg processing for these values */
    for (int i=1; i<argc; i++) {
	int spn, numas;
	if (strcmp(argv[i], "-np") == 0) {
	    i++;
	    np = atoi(argv[i]);
	}
	else if (strcmp(argv[i], "-ppn") == 0) {
	    i++;
	    ppn = atoi(argv[i]);
	}
	else if (strcmp(argv[i], "-spn") == 0) {
	    i++;
	    spn = atoi(argv[i]);
	    objs[1] = spn;
	}
	else if (strcmp(argv[i], "-numas") == 0) {
	    i++;
	    numas = atoi(argv[i]);
	    objs[2] = numas;
	}
    }

    objs[0] = np/ppn;

    /* Run an internal routine to determine the decomposition of processes
       across elemnts for debugging */
    /* Block ordering */
    bsize[0] = ppn;
    bsize[1] = ppn / objs[1];
    bsize[2] = bsize[1] / objs[2];

    printf("Block ordering for all levels\n");
    /* Note: oor is "Out-of-range" and indicates invalid values. Correct
       output should be o for the oor column */
    printf("rank\tnode\tsocket\tnuma\tr-in-numa\toor\n");
    for (r=0; r<np; r++) {
	int oor=0; /* Out of range? */
#if 0
	BENVi_HwdescGetDebugDecomp(r, np, objs, bsize, nobjs, idxs);
#else
	int rr=r, nnp=np;
	for (int ii=0; ii<nobjs; ii++) {
	    BENVi_DistribRankByPolicy(nnp, rr, objs[ii], bsize[ii], &idxs[ii],
				      &rr, &nnp);
	}
	idxs[3] = rr;
#endif
	if (idxs[0] < 0 || idxs[0] >= np/objs[0]) {
	    oor += 1;
	}
	if (idxs[1] < 0 || idxs[1] >= objs[1]) {
	    oor += 2;
	}
	if (idxs[2] < 0 || idxs[2] >= objs[2]) {
	    oor += 4;
	}
	if (oor > 0) rc++;
	printf("%d\t%d\t%d\t%d\t%d\t(%x)\n", r, idxs[0], idxs[1], idxs[2], idxs[3],
	       oor);
    }

    /* RR ordering */
    bsize[0] = 1;
    bsize[1] = 1;
    bsize[2] = 1;
    printf("Round robin ordering for all levels\n");
    printf("rank\tnode\tsocket\tnuma\tr-in-numa\toor\n");
    for (r=0; r<np; r++) {
	int oor=0; /* Out of range? */
#if 0
	BENVi_HwdescGetDebugDecomp(r, np, objs, bsize, nobjs, idxs);
#else
	int rr=r, nnp=np;
	for (int ii=0; ii<nobjs; ii++) {
	    BENVi_DistribRankByPolicy(nnp, rr, objs[ii], bsize[ii], &idxs[ii],
				      &rr, &nnp);
	}
	idxs[3] = rr;
#endif
	if (idxs[0] < 0 || idxs[0] >= np/objs[0]) {
	    oor += 1;
	}
	if (idxs[1] < 0 || idxs[1] >= objs[1]) {
	    oor += 2;
	}
	if (idxs[2] < 0 || idxs[2] >= objs[2]) {
	    oor += 4;
	}
	if (oor > 0) rc++;
	printf("%d\t%d\t%d\t%d\t%d\t(%x)\n", r, idxs[0], idxs[1], idxs[2], idxs[3],
	       oor);
    }

    if (rc > 0) printf("Found %d errors\n", rc);
    return 0;
}
