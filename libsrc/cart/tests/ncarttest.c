/* -*- Mode: C; c-basic-offset:4 ; -*- */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "mpi.h"
#include "benvutil.h"
#include "hwdescnew.h"
//#include "nodeinfo.h"
#include "cartrepl.h"
#include "topometrics.h"
#include "seq.h"

typedef struct {
    int (*Cart_shift)(MPI_Comm comm, int direction, int disp,
		      int *rank_sources, int *rank_dest);
    int (*Cart_coords)(MPI_Comm comm, int rank, int maxdims, int coords[]);
    int (*Cart_rank)(MPI_Comm comm, const int coords[], int *rank);
    int (*Cart_sub)(MPI_Comm comm, const int remain[], MPI_Comm *subcomm);
    int (*Cart_get)(MPI_Comm comm, int maxdims, int dims[], int periods[],
		    int coords[]);
    int (*Cart_dim_get)(MPI_Comm comm, int *ndims);
} cartmethods_t;

void printNeighborInfo(FILE *fp, MPI_Comm comm, int coords[2],
		       int w, int e, int n, int s);
int isCommReordered(MPI_Comm c1, MPI_Comm c2);
int checkCommN(cartmethods_t *cm, MPI_Comm comm, int ndimsAct,
	       MPI_Comm *localcomm);
int checkComm(MPI_Comm comm, int ndimsAct, MPI_Comm *localcomm);
int checkMPIXComm(MPI_Comm comm, int ndimsAct, MPI_Comm *localcomm);
void errmsgPrefix(const char *prefix);
void errmsg(const char *fmat, ...);
void rankInRange(int r, int sz, const char *dirname);
void checkCart(MPI_Comm comm, int sz, int ndimexp);

#define MAX_HW_DEPTH 16

int main(int argc, char *argv[])
{
    int wrank, wsize, north, south, east, west, rc;
    MPI_Comm cartcomm, ncartcomm, ncartcomm3;
    int dims[3], coords[3], periods[3], i;
    int crank, nsize, ncrank, ranks[5];
    int nnodes, nodelevel, isexact;
    hwdescCtx *hwc=0;
    hwdescParms parms;
    MPI_Comm    nodecomm;
    //hwdesc_t hwdesc[MAX_HW_DEPTH];
    cartmethods_t cmorig, cmncart;

    MPI_Init(&argc, &argv);
    MPI_Comm_size(MPI_COMM_WORLD, &wsize);
    MPI_Comm_rank(MPI_COMM_WORLD, &wrank);

    /* Look for debugging and verbose arguments */
    /* Process command line and environment options */
    BENV_HwdescCvarInit();
    /* Get default information from environment about hwdesc */
    BENV_HwdescParmInit(&parms);
    BENV_HwdescParmUpdateFromEnv(&parms, 0);
    for (i=1; i<argc; i++) {
	rc = BENV_HwdescArg(argc, argv, &i, "-hw", &parms);
	BENV_ARGCHECK(rc,"error in hwdesc options",return 1);
	rc = BENV_DebugArgCommon(argc, argv, &i);
	BENV_ARGCHECK(rc,"error in debug options\n",return 1);
	rc = BENV_NodecartArgDebug(argc, argv, &i, "");
	BENV_ARGCHECK(rc,"error in nodecart debug options\n",return 1);

	if (wrank == 0) {
	    fprintf(stderr, "Unrecognized argument %s\n", argv[i]);
	    fflush(stderr);
	    BENV_HwdescArgPrintUsage(stderr, "-hw", -1);
	    MPI_Abort(MPI_COMM_WORLD,1);
	}
    }

    /* If any cvars are set, update the parms */
    BENV_HwdescParmSetFromCvar(&parms);

    /* Get the hw description */
    BENV_HwdescGetDescGeneral(MPI_COMM_WORLD, BENV_HWDESC_USE_ALL,
			      &parms, &hwc);

    /* Save the description on COMM_WORLD (Nodecart_create will need it) */
    BENV_HwdescSaveDescToComm(hwc, MPI_COMM_WORLD);

    BENV_HwdescFindObject(hwc, BENV_HWDESC_NODE, &nodelevel, &isexact);
    if (!isexact || nodelevel == -1) {
	/* No hw information. Exit. */
	fprintf(stderr, "Panic: Did not get back node information!\n");
	MPI_Abort(MPI_COMM_WORLD, 1);
    }

    nodecomm = hwc->collinfo[nodelevel].objcomm;
    MPI_Comm_size(nodecomm, &nsize);
    nnodes = hwc->collinfo[nodelevel].nSiblings;
    if (wrank == 0) {
	printf("SMP: nodes = %d, nodesize = %d\n", nnodes, nsize);
	fflush(stdout);
    }

    /* Print the hw hierarchy */
    BENV_HwdescPrintAll(stdout, MPI_COMM_WORLD, hwc, 0);

    /* Test the nodecart routines and compare them with the default MPI
       implementation behavior */
    /* Initialize the sets of methods */
    cmorig.Cart_shift    = MPI_Cart_shift;
    cmorig.Cart_coords   = MPI_Cart_coords;
    cmorig.Cart_rank     = MPI_Cart_rank;
    cmorig.Cart_sub      = MPI_Cart_sub;
    cmorig.Cart_get      = MPI_Cart_get;
    cmorig.Cart_dim_get  = MPI_Cartdim_get;

    cmncart.Cart_shift   = MPIX_Nodecart_shift;
    cmncart.Cart_coords  = MPIX_Nodecart_coords;
    cmncart.Cart_rank    = MPIX_Nodecart_rank;
    cmncart.Cart_sub     = MPIX_Nodecart_sub;
    cmncart.Cart_get     = MPIX_Nodecart_get;
    cmncart.Cart_dim_get = MPIX_Nodecart_dim_get;

    /* MPI Cartesian routine */
    for (i=0; i<2; i++) {
	dims[i] = 0;
	periods[i] = 0;
    }
    MPI_Dims_create(wsize, 2, dims);
    MPI_Cart_create(MPI_COMM_WORLD, 2, dims, periods, 1, &cartcomm);
    MPI_Comm_set_name(cartcomm, "cartcomm");
    MPI_Cart_shift(cartcomm, 0, 1, &west, &east);
    MPI_Cart_shift(cartcomm, 1, 1, &north, &south);
    MPI_Comm_rank(cartcomm, &crank);
    MPI_Cart_coords(cartcomm, crank, 2, coords);
    /* Check for valid ranks */
    rankInRange(west, wsize, "West");
    rankInRange(east, wsize, "East");
    rankInRange(north, wsize, "North");
    rankInRange(south, wsize, "South");
    { int remain[2], sz, rk;
	MPI_Comm cart1;

	remain[0] = 0; remain[1] = 1;
        MPI_Cart_sub(cartcomm, remain, &cart1);
	MPI_Comm_size(cart1, &sz);
	MPI_Comm_rank(cart1, &rk);
	if (rk == 0) {
	    printf("Cart: Sizeof sub (remain[1]=1) is %d\n", sz);fflush(stdout);
	}
        MPI_Comm_free(&cart1);
	remain[1] = 0; remain[0] = 1;
        MPI_Cart_sub(cartcomm, remain, &cart1);
	MPI_Comm_size(cart1, &sz);
	MPI_Comm_rank(cart1, &rk);
	if (rk == 0) {
	    printf("Cart: Sizeof sub (remain[0]=1) is %d\n", sz);fflush(stdout);
	}
        MPI_Comm_free(&cart1);
	//printf("Done freeing cart from MPI_Cart_sub\n"); fflush(stdout);
	MPI_Barrier(MPI_COMM_WORLD);
    }

    if (wrank == 0) {
        printf("cartcomm dims = (%d,%d)\n", dims[0], dims[1]);
	printf("wrank(cartrank): (coords in mesh):w,e,n,s nbr ranks\n");
	printf("same, but ranks in comm world\n");
	fflush(stdout);
    }
    BENV_SeqBegin(MPI_COMM_WORLD);
    printNeighborInfo(stdout, cartcomm, coords, west, east, north, south);
    BENV_SeqEnd(MPI_COMM_WORLD);
    ranks[0] = west;
    ranks[1] = east;
    ranks[2] = north;
    ranks[3] = south;
    BENV_PrintNonLocalCounts(stdout, cartcomm, 4, ranks, 1, &nodecomm);

    errmsgPrefix("cartcomm:");
    rc = checkCommN(&cmorig, cartcomm, 2, &nodecomm);
    MPI_Allreduce(MPI_IN_PLACE, &rc, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);
    if (wrank == 0) {
	if (rc > 0)
	    errmsg("Errors in cartcomm on %d processes\n", rc);
	else {
	    printf("No errors found in cartcomm\n");
	}
    }

    MPI_Barrier(MPI_COMM_WORLD);
    /* MPIX Nodecart routine */
    for (i=0; i<2; i++) {
	dims[i] = 0;
	periods[i] = 0;
    }
    /* Nodecart is best used without dims_create, so that it can determine
       the best decomposition */
    MPIX_Nodecart_create(MPI_COMM_WORLD, 2, dims, periods, 1, &ncartcomm);
    checkCart(ncartcomm, wsize, 2);
    MPI_Comm_set_name(ncartcomm, "ncartcomm");
    MPIX_Nodecart_shift(ncartcomm, 0, 1, &west, &east);
    MPIX_Nodecart_shift(ncartcomm, 1, 1, &north, &south);
    rankInRange(west, wsize, "West");
    rankInRange(east, wsize, "East");
    rankInRange(north, wsize, "North");
    rankInRange(south, wsize, "South");
    { int remain[2], sz, rk;
	MPI_Comm ncart1;

	remain[0] = 0; remain[1] = 1;
	//printf("TMP: About to do first nodecart_sub\n"); fflush(stdout);
        MPIX_Nodecart_sub(ncartcomm, remain, &ncart1);
	MPI_Comm_size(ncart1, &sz);
	MPI_Comm_rank(ncart1, &rk);
	if (rk == 0) {
	    printf("Ncart: Sizeof sub (remain[1]=1) is %d\n", sz);fflush(stdout);
	}
        MPI_Comm_free(&ncart1);
	remain[1] = 0; remain[0] = 1;
	//printf("TMP: About to do second nodecart_sub\n"); fflush(stdout);
        MPIX_Nodecart_sub(ncartcomm, remain, &ncart1);
	MPI_Comm_size(ncart1, &sz);
	MPI_Comm_rank(ncart1, &rk);
	if (rk == 0) {
	    printf("Ncart: Sizeof sub (remain[0]=1) is %d\n", sz);fflush(stdout);
	}
        MPI_Comm_free(&ncart1);
	MPI_Barrier(MPI_COMM_WORLD);
    }
    MPI_Comm_rank(ncartcomm, &ncrank);
    //printf("TMP: About to do nodecart_coords\n"); fflush(stdout);
    MPIX_Nodecart_coords(ncartcomm, ncrank, 2, coords);

    if (wrank == 0) {
	int ndims[2], nperiods[2], ncoords[2];
	printf("For ncartcomm\n");
	// Note that we let ncartcomm pick the dimensions, so we need
	// to extract them from the cartcomm
	MPIX_Nodecart_get(ncartcomm, 2, ndims, nperiods, ncoords);
        printf("ncartcomm dims = (%d,%d)\n", ndims[0], ndims[1]);
	printf("wrank(cartrank): (coords in mesh):w,e,n,s nbr ranks\n");
	printf("same, but ranks in comm world\n");
	fflush(stdout);
    }

    if (isCommReordered(MPI_COMM_WORLD, ncartcomm)) {
	BENV_SeqBegin(MPI_COMM_WORLD);
	printNeighborInfo(stdout, ncartcomm, coords, west, east, north, south);
	BENV_SeqEnd(MPI_COMM_WORLD);
	ranks[0] = west;
	ranks[1] = east;
	ranks[2] = north;
	ranks[3] = south;
	BENV_PrintNonLocalCounts(stdout, ncartcomm, 4, ranks, 1, &nodecomm);
    }
    else {
	if (wrank == 0) {
	    printf("ncartcomm has same order as COMM_WORLD\n");
	}
    }

    errmsgPrefix("ncartcomm:");
    rc = checkCommN(&cmncart, ncartcomm, 2, &nodecomm);
    MPI_Allreduce(MPI_IN_PLACE, &rc, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);
    if (wrank == 0) {
	if (rc > 0)
 	    errmsg("Errors in ncartcomm on %d processes\n", rc);
	else {
	    printf("No errors found in ncartcomm\n");
	}
    }

    /* MPIX Nodecart routine */
    for (i=0; i<3; i++) {
	dims[i] = 0;
	periods[i] = 0;
    }
    /* Nodecart is best used without dims_create, so that it can determine
       the best decomposition */
    //printf("TMP: About to create 3-d comm with unspecified dims\n"); fflush(stdout);
    MPIX_Nodecart_create(MPI_COMM_WORLD, 3, dims, periods, 1, &ncartcomm3);
    MPI_Comm_set_name(ncartcomm3, "ncartcomm3");
    checkCart(ncartcomm3, wsize, 3);
    errmsgPrefix("ncartcomm3:");
    rc = checkCommN(&cmncart, ncartcomm3, 3, &nodecomm);
    MPI_Allreduce(MPI_IN_PLACE, &rc, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);
    if (wrank == 0) {
	if (rc > 0)
 	    errmsg("Errors in ncartcomm3 on %d processes\n", rc);
	else {
	    printf("No errors found in ncartcomm3\n");
	}
    }

    /* Get node info and check on quality of mappings */

    /* Free communicators */
    MPI_Comm_free(&cartcomm);
    MPI_Comm_free(&ncartcomm);
    MPI_Comm_free(&ncartcomm3);

    MPI_Finalize();
    return 0;
}


void printNeighborInfo(FILE *fp, MPI_Comm comm, int coords[2],
		       int w, int e, int n, int s)
{
    int ranks[5], wrank, wranks[5], ncrank;
    MPI_Group gcomm, gworld;

    ranks[0] = w;
    ranks[1] = e;
    ranks[2] = n;
    ranks[3] = s;
    MPI_Comm_rank(comm, &ranks[4]);
    MPI_Comm_rank(MPI_COMM_WORLD, &wrank);
    ncrank = ranks[4];
    MPI_Comm_group(MPI_COMM_WORLD, &gworld);
    MPI_Comm_group(comm, &gcomm);
    //printf("About to call group_translate with ranks=%d,%d,%d,%d,%d\n",
    //   ranks[0], ranks[1], ranks[2], ranks[3] , ranks[4]);
    MPI_Group_translate_ranks(gcomm, 5, ranks, gworld, wranks);
    fprintf(fp, "%d(%d): (ranks in cartcomm)\t(%d,%d):%d:%d:%d:%d\n",
	    wrank, ncrank, coords[0], coords[1], w, e, n, s);
    fprintf(fp, "%d(%d):%d: (ranks in commworld)\t(%d,%d):%d:%d:%d:%d\n",
	    wrank, ncrank, wranks[4],
	    coords[0], coords[1], wranks[0], wranks[1], wranks[2], wranks[3]);
    fflush(fp);
    MPI_Group_free(&gworld);
    MPI_Group_free(&gcomm);
}

/* This routine checks to see if two communicators have the same process
   ordering */
int isCommReordered(MPI_Comm c1, MPI_Comm c2)
{
    int result;
    MPI_Comm_compare(c1, c2, &result);

    return result == MPI_SIMILAR;
}

/* Perform some basic checks */
#define MAX_DIMS 6
int checkCommN(cartmethods_t *cm, MPI_Comm comm, int ndimsAct,
	       MPI_Comm *localcomm)
{
    int ndims, rank, crank, csize, i;
    int dims[MAX_DIMS], periods[MAX_DIMS], coords[MAX_DIMS], tcoords[MAX_DIMS];
    int haloranks[2*MAX_DIMS], k;

    //printf("Entering checkCommN\n");
    (cm->Cart_dim_get)(comm, &ndims);
    if (ndims != ndimsAct) {
	errmsg("cart ndims = %d not expected value of %d\n", ndims, ndimsAct);
	return 1;
    }
    (cm->Cart_get)(comm, ndims, dims, periods, coords);

    /* Cart_rank and Cart_coords are roughly duals of each other */
    (cm->Cart_rank)(comm, coords, &rank);
    /* Basic test: compare to comm_rank value */
    MPI_Comm_rank(comm, &crank);
    if (rank != crank) {
	errmsg("rank from coords %d does not match rank in comm %d\n",
	       rank, crank);
	return 1;
    }
    /* Look at all ranks and check that the corresponding coords match */
    MPI_Comm_size(comm, &csize);
    for (i=0; i<csize; i++) {
	(cm->Cart_coords)(comm, i, ndims, tcoords);
	(cm->Cart_rank)(comm, tcoords, &rank);
	if (i != rank) {
	    errmsg("converting rank %d to coords and back to rank gave %d\n",
		   i, rank);
	    return 1;
	}
    }

    /* Create sub comms */
    if (ndims > 1) {
	for (i=0; i<ndims; i++) {
	    MPI_Comm newsub;
	    int remain[MAX_DIMS], j, ssize;
	    for (j=0; j<ndims; j++) remain[j] = 0;
	    remain[i] = 1;
	    (cm->Cart_sub)(comm, remain, &newsub);
	    /* Confirm sizes match dims. Also check communicator */
	    MPI_Comm_size(newsub, &ssize);
	    if (ssize != dims[i]) {
		errmsg("size %d of sub cart in dimension %d does not match expected size of %d\n", ssize, i, dims[i]);
		return 1;
	    }
	    checkCommN(cm, newsub, 1, 0);
	    MPI_Comm_free(&newsub);
	}
    }

    /* Shift in all coordinate directions */
    k = 0;
    for (i=0; i<ndims; i++) {
	int rsource, rdest, rtest, j;
	(cm->Cart_shift)(comm, i, 1, &rsource, &rdest);
	rankInRange(rsource, csize, "rsource");
	rankInRange(rdest, csize, "rdest");
	/* Compare with MPI_Cart_rank values for computed coords */
	for (j=0; j<ndims; j++) tcoords[j] = coords[j];
	/* Note that in the non-periodic case, out of range coordinates
	   are erroneous */
	tcoords[i] += 1;
	if (tcoords[i] < dims[i] || periods[i]) {
	    (cm->Cart_rank)(comm, tcoords, &rtest);
	    if (rtest != rdest) {
		errmsg("rank from explict shift of coords in direction %d is %d but art shift gave %d\n", i, rtest, rdest);
		return 1;
	    }
	}
	for (j=0; j<ndims; j++) tcoords[j] = coords[j];
	tcoords[i] -= 1;
	if (tcoords[i] >= 0 || periods[i]) {
	    (cm->Cart_rank)(comm, tcoords, &rtest);
	    if (rtest != rsource) {
		errmsg("rank from explict shift of coords in direction %d is %d but art shift gave %d\n", i, rtest, rsource);
		return 1;
	    }
	}
	/* Save ranks for halo exchange locality test */
	haloranks[k++] = rsource;
	haloranks[k++] = rdest;
    }

    /* Look at quality of process mapping */
    if (localcomm) {
	BENV_PrintNonLocalCounts(stdout, comm, k, haloranks, 1, localcomm);
    }

    //printf("Exiting CommCheckN\n");
    return 0;
}

static const char *errprefix=0;
void errmsgPrefix(const char *prefix)
{
    errprefix = prefix;
}

void errmsg(const char *fmat, ...)
{
    va_list argp;
    va_start(argp, fmat);
    if (errprefix) {
	fputs(errprefix, stderr);
    }
    vfprintf(stderr, fmat, argp);
    va_end(argp);
    fflush(stderr);
}

void rankInRange(int r, int sz, const char *dirname)
{
    if (r == MPI_PROC_NULL) return;
    if (r < 0 || r >= sz) {
	fprintf(stderr, "ERROR: Rank for %s = %d, not in [0,%d)\n", dirname, r, sz);
    }
}

/* For a NODECART comm only */
void checkCart(MPI_Comm comm, int sz, int ndimexp)
{
    int dims[MAX_DIMS], periods[MAX_DIMS], coords[MAX_DIMS], ndims;
    int i, totaldim, errcnt=0;

    /* Extract the basic information about the Cartesian communicator */
    MPIX_Nodecart_dim_get(comm, &ndims);
    if (ndims != ndimexp) {
	fprintf(stderr, "ERROR: comm ndims=%d expected %d\n", ndims, ndimexp);
	return;
    }
    MPIX_Nodecart_get(comm, MAX_DIMS, dims, periods, coords);
    /* Check values are consistent */
    totaldim = 1;
    for (i=0; i<ndims; i++) {
	if (dims[i] <= 0 || dims[i] > sz) {
	    fprintf(stderr, "ERROR: dims[%d]=%d is out of range\n", i,dims[i]);
	    errcnt++;
	}
	else
	    totaldim *= dims[i];
	if (coords[i] < 0 || coords[i] >= dims[i]) {
	    fprintf(stderr, "ERROR: coords[%d]=%d not in [0,%d)\n", i, coords[i],
		    dims[i]);
	    errcnt++;
	}
	if (periods[i] != 0) {
	    fprintf(stderr, "ERROR: periods[%d] = %d, expected 0\n",
		    i, periods[i]);
	    errcnt++;
	}
    }
    if (totaldim != sz) {
	fprintf(stderr, "ERROR: totaldim = %d, should = %d\n", totaldim, sz);
	fputs("dims = ", stderr);
	for (i=0; i<ndims; i++) {
	    fprintf(stderr, "%d,", dims[i]);
	}
	fputc('\n', stderr);
	errcnt++;
    }
    if (errcnt) {
	fprintf(stderr, "ERROR: Found %d errors in nodecart comm!\n", errcnt);
    }
}

