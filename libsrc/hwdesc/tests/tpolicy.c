/* Test routines to unpack a scheduler policy and determine proces/thread
   assignment */

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
    int np, rank, nobj, blocksize, objidx, rankinobj, npinobj;

    /* Set defaults */
    np        = 16;
    nobj      = 2;
    blocksize = 3;

    /* Get args */
    for (int i=1; i<argc; i++) {
	if (strcmp(argv[i], "-np") == 0) {
	    i++;
	    np = atoi(argv[i]);
	}
	else if (strcmp(argv[i], "-nobj") == 0) {
	    i++;
	    nobj = atoi(argv[i]);
	}
	else if (strcmp(argv[i], "-blocksize") == 0) {
	    i++;
	    blocksize = atoi(argv[i]);
	}
	else {
	    fprintf(stderr, "Unrecognized argument %s\n", argv[i]);
	}
    }

    fprintf(stdout, "Test for np=%d, nobj=%d, blocksize=%d\n",
	    np, nobj, blocksize);
    fprintf(stdout, "r\tidx\tr in\tnp in obj\n");
    for (rank=0; rank<np; rank++) {
	BENVi_DistribRankByPolicy(np, rank, nobj, blocksize,
				  &objidx, &rankinobj, &npinobj);
	fprintf(stdout, "%d:\t%d\t%d\t%d\n", rank, objidx, rankinobj, npinobj);
    }
}
