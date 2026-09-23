/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 * Copyright (C) by University of Illinois 2024
 */

#include "benvconf.h"
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>
//#define MPI_Comm int
#define NO_MPI_INCLUDE
#include "benvdbg.h"
#include "benvutil.h"
#include "cartimpl.h"

/* This file implements a version of hw hierarchy to newrank.
   It makes no reference to MPI routines, allowing it to be tested
   for arbitrary hw hierarchies and numbers of processes with a single
   process (see tests/cranktest.c) */

CDBGDECL(CARTRANK);
CDBGFCALLDECL;

/* Routines local to this file */
#define PRIVATE static
PRIVATE int dimsBalance(int ndims, const int olddims[], const int dims[]);
PRIVATE int pickOrder(int ndims, const int fixeddims[], int startidx, const int olddims[], int dims[]);
static int findNextFactorMatch(facinfo_t *lt, int nd, facinfo_t *dimsinfo,
			       int *idx, int *f);
static void freeLevelSizes(hwlevs_t *levs);
static cartHierarchy *carthCreate(int nlevs, int ndims);
//static int BENV_Factor(int n, int *nf_ptr, factor_t **factors);
//void CoordsToRank(int ndims, const int dims[], const int coords[], int *rank);
//void RankToCoords(int ndims, const int dims[], int rank, int coords[]);
static void printFactors(FILE *fp, int n, factor_t *fac);
static hwlevs_t *copyHWdesc(const hwlevs_t *hwlevs);

/*@
 MPIX_Nodecart_create_from_hierarchy_local - Generate a Cartesian
 decomposition of processes using a hierarchy description

Input Parameters:
+ hwlevs - Simplified hardware hierarchy - an hwlevs_t with levels
 that are common amoung all processes that will call this routine. This
 must include the individual processors, e.g., not just the down to the
 nodes and sockets.
. ndims   - Number of dimensions
. periods - flag indicating whether mesh is periodic in each dimension
- origrank - Original rank (coorespding to the 'objidx' fields of the
 'hwhierarchy')

Input/Output Parameters:
. dims    - On input, preselected dimensions of the Cartesian
decomposition, if any. Zero means that the dimension is not preselected.
On output, the dimensions of the cartcom.

Output Parameters:
+ cartcoords - Coordinates in of the calling process in the Cartesian process
  topology
. newrank - rank of the process in the new Cartesian process topology,
            assuming C ordering of the coordinates
- carth - hardware-aware Cartesian hierarchy

Notes:
This routine is local, not collective, and can be used outside of an MPI
environment.
The value 'periods' is currently not used to determine the process mapping or
dimensions.
@*/
int BENVi_Nodecart_create_from_hierarchy_local(const hwlevs_t *hwlevs,
					      int ndims, int dims[],
					      const int periods[],
					      int cartcoords[], int *newrank,
					      cartHierarchy **carth_ptr)
{
    cartHierarchy *carth;
    int olddims[MAX_DIMS];
    int lev, i, dimsgiven;
    hwlevs_t *hwlevscp = 0;

    CDBGFCALLENTER;
    /* Handle the case of any dims not zero. Note this was not handled
       in the 2016 versions of the nodecart codes */
    dimsgiven = 0;
    for (i=0; i<ndims; i++) {
	if (dims[i] != 0) dimsgiven++;
    }

    /* Note that if all dims are zero, we skip this step */
    if (dimsgiven) {
	/* The purpose of this step is to distribute the predetermined
	   values of dims across the hw levels. The remaining hw elements
	   will be used in the rest of this code */
	hwlevscp = copyHWdesc(hwlevs);
	BENVi_DistributeDimsOverHW(hwlevscp, ndims, dims, periods, &carth);
	/* Replace hwlevs with this updated copy */
	hwlevs = hwlevscp;
    }
    else {
	/* Create the carth structure to return */
	carth = carthCreate(hwlevs->nlevels, ndims);
	/* FIXME: Initialize */
    }

    /* Keep track of the dims assigned so far. This is needed to help
       balance the assignments at each level to avoid poor aspect ratios
       in the overall decomposition. Note that we need to remember which
       elements of dims were zero on input to identify which can be set
       by this routine. */
    for (i=0; i<ndims; i++) {
	if (dims[i] > 0) olddims[i] = dims[i];
	else             olddims[i] = 1;
    }

    for (lev=0; lev<hwlevs->nlevels; lev++) {
	/* Only initialize the unset dimensions */
	for (i=0; i<ndims; i++) {
	    if (dims[i] == 0)
		carth->cl[lev].dims[i] = 0;
	}
	/* Factor the size, pick a "nice" decomposition */
	/* Must not require MPI_Dims_create, though could still run in a
	   singleton process */
	BENVi_SimpleDimsCreate(hwlevs->hwl[lev].nobj, ndims,
			       carth->cl[lev].dims);
	CDBGV(CARTRANK,BASIC,"\tLevel %d: decomp from (%d) to ",
		 lev, hwlevs->hwl[lev].nobj);
	CDBGCMD(CARTRANK,BASIC,BENV_PrintIntTuple(cvar_vfp,ndims,carth->cl[lev].dims,1));

	/* Reorder dimensions to approximate balance */
	pickOrder(ndims, dims, 0, olddims, carth->cl[lev].dims);
	CDBGV(CARTRANK,BASIC,"After pickOrder: ");
	CDBGCMD(CARTRANK,BASIC,BENV_PrintIntTuple(cvar_vfp,ndims,carth->cl[lev].dims,1));
	/* Update the assigned dimensions */
	for (i=0; i<ndims; i++) olddims[i] *= carth->cl[lev].dims[i];

	/* Determine the coords of this process in the Cartesian grid
	   at this level */
	BENVi_RankToCoords(ndims, carth->cl[lev].dims, hwlevs->hwl[lev].objidx,
			   BENV_ORDER_C, carth->cl[lev].coords);
	CDBGV(CARTRANK,BASIC,"\tLevel %d\tComputed coords ",lev);
	CDBGCMD(CARTRANK,BASIC,BENV_PrintIntTuple(cvar_vfp,ndims,carth->cl[lev].coords,0));
	CDBGV(CARTRANK,BASIC," in dims ");
	CDBGCMD(CARTRANK,BASIC,BENV_PrintIntTuple(cvar_vfp,ndims,carth->cl[lev].dims,1));
    }

    /* Combine dimensions and coordinates across the levels to get the
       overall representation */
    /* FIXME: Only correct for all values <= 0 */
    /* To handle constraints on dims, need to save all sizes and their
       factors, and extract values to match.  See the implementation
       of MPI_Dims_create (at least my implementation for mpich) */
    for (i=0; i<ndims; i++) {
//	olddims[i]    = carth->cl[0].dims[i];
	cartcoords[i] = carth->cl[0].coords[i];
    }
    for (lev=1; lev<hwlevs->nlevels; lev++) {
	for (i=0; i<ndims; i++) {
//	    olddims[i] *= carth->cl[lev].dims[i];
	    cartcoords[i] = carth->cl[lev].coords[i] +
		carth->cl[lev].dims[i] * cartcoords[i];
	}
    }

    /* Setup return values */
    /* Convert coords into an overall rank */
    BENVi_CoordsToRank(ndims, olddims, cartcoords, BENV_ORDER_C, newrank);
    for (i=0; i<ndims; i++) dims[i] = olddims[i];
    if (carth_ptr) {
	*carth_ptr = carth;
    }
    else {
	BENVi_CarthFree(carth);
    }

    if (hwlevscp) {
	/* Free the copy */
	freeLevelSizes(hwlevscp);
    }

    CDBGFCALLEXIT;
    return 0;
}

/*
  Given a description of hardware levels and information about the
  decomposition of dimensions, distribute the dimensions across the
  hardware levels.

  Note that 0 < prod(dims (where dims[1]>0)) < prod(hwlevs) - that is, there
  are dimensions to distribute across the hw levels
 */
int BENVi_DistributeDimsOverHW(hwlevs_t *hwlevs,
			       int ndims, const int dims[],
			       const int periods[],
			       cartHierarchy **carth_ptr)
{
    facinfo_t *dimsinfo, *levsinfo;
    int i, lastunspec, k, lev, ntot;
    cartHierarchy *carth;

    CDBGFCALLENTER;
    /* Get the total number of processes. Each level specifies the number
       of items in each of the objects at the "higher" (i-1) level. */
    ntot = 1;
    for (i=0; i<hwlevs->nlevels; i++)
	ntot *= hwlevs->hwl[i].nobj;

    /* For each given dimension and each level of the hwhierarchy, factor
       the sizes */
    dimsinfo = (facinfo_t *)malloc(ndims * sizeof(facinfo_t));
    if (!dimsinfo) BENVi_MallocErr("dims info",ndims,"facinto_t");
    levsinfo = (facinfo_t *)malloc(hwlevs->nlevels * sizeof(facinfo_t));
    if (!levsinfo) BENVi_MallocErr("levs info",hwlevs->nlevels,"facinto_t");

    k = 0;
    for (i=0; i<ndims; i++) {
	if (dims[i] > 0) {
	    BENVi_Factor(dims[i], &dimsinfo[k].nfactors, &dimsinfo[k].factors);
	    CDBGV(CARTRANK,BASIC,"Factors for dims[%d]=%d: ",i, dims[i]);
	    CDBGCMD(CARTRANK,BASIC,printFactors(cvar_vfp,dimsinfo[k].nfactors,dimsinfo[k].factors));
	    dimsinfo[k].origidx = i;
	    k++;
	    ntot /= dims[i];
	}
	else lastunspec = i;
    }

    if (k == ndims-1) {
	/* Act as if all dims given, as we know the value for the remaining
	   dimension */
	BENVi_Factor(ntot, &dimsinfo[k].nfactors, &dimsinfo[k].factors);
	CDBGV(CARTRANK,BASIC,"Factors for dims[%d]=%d: ",lastunspec,
		 dims[lastunspec]);
	CDBGCMD(CARTRANK,BASIC,printFactors(cvar_vfp,dimsinfo[k].nfactors,dimsinfo[k].factors));
	/* Note this means origidx not in sorted order */
	dimsinfo[k].origidx = lastunspec;
	k++;  /* k always the number of dimsinfo set */
    }

    /* Factor the hwlevs as well */
    for (i=0; i<hwlevs->nlevels; i++) {
	BENVi_Factor(hwlevs->hwl[i].nobj,
		    &levsinfo[i].nfactors, &levsinfo[i].factors);
	CDBGV(CARTRANK,BASIC,"Factors for nobj[%d]=%d: ",i,hwlevs->hwl[i].nobj);
	CDBGCMD(CARTRANK,BASIC,printFactors(cvar_vfp,levsinfo[i].nfactors,levsinfo[i].factors));
	levsinfo[i].origidx = i;
    }

    /* TODO: How to organize the factors, and how to update the dims/hwdesc */

    /* Starting from the bottom level, distribute the factors of the
       fixed dimensions */
    /* Need to accumulate in a cart hierarchy, so that the appropriate
       mapping to coordinates can be performed. That is, assigning hw to
       the given dimensions */

    /* Create a carth structure */
    carth = carthCreate(hwlevs->nlevels, ndims);

    /* Start at the bottom and move up.
     */
    for (lev=hwlevs->nlevels-1; lev>=0; lev--) {
	int idx, f;
	CDBGV(CARTRANK,BASIC,"hw level %d with %d factors\n", lev,
		 levsinfo[lev].nfactors);
	for (i=0; i<ndims; i++) carth->cl[lev].dims[i] = 1;

	/* Factor the size, pick a "nice" decomposition */
	/* FIXME: Replace with extract from factors in dims that match this
	   level */
	/* n, number of dimensions, set of factors; selected dimension, factor */
	do {
	    findNextFactorMatch(&levsinfo[lev], k, dimsinfo, &idx, &f);
	    if (idx >= 0) {
		carth->cl[lev].dims[idx] *= f;
		CDBGV(CARTRANK,BASIC,"\tLevel %d: added factor %d from dimension %d\n",
			 lev, f, idx);
	    }
	} while (idx >= 0);
    }

    /* Recover allocated space */
    for (i=0; i<k; i++) {
	free(dimsinfo[i].factors);
    }
    free(dimsinfo);
    for (i=0; i<hwlevs->nlevels; i++) {
 	free(levsinfo[i].factors);
    }
    free(levsinfo);

    /* Return the cart hierarchy */
    *carth_ptr = carth;

    CDBGFCALLEXIT;
    return 0;
}

/* findNextFactorMatch - Match factors of a hardware to factors of the
  dimensions
   input:
   lt - factors of n, this is a single level of the hardware
   nd - number of dimensions
   dimsinfo - factors of each dimension

   output:
   idx - selected dimension
   f -   factor
*/
static int findNextFactorMatch(facinfo_t *lt, int nd, facinfo_t *dimsinfo,
			       int *idx, int *f)
{
    int lf,   /* index of next factor in lt (hw level) */
	id,   /* which of the dimsinfo dimentions */
	ifac,   /* which of the factors in the current dimsinfo */
	fval=1;

    CDBGFCALLENTER;
    /* Scan for any match of factors. Handle the case of pwr == 0
       (meaning that all of those factors have already been used) */
    /* Better: Distribute all dimensions across the levels. That
       means to take a single factor from each dimension at a time */
    /* Better still is to distribute all of the dimensions (including the
       unspecified ones) across the levels (otherwise, the coarsest (node)
       level might have some dimensions of size one). */
    /* Better yet is to try a few strategies and take the "best".
       Strategies to consider are:
       i. Distribute one dimension at a time, across the levels (bottom first)
       ii. Same as i., but top first
       iii. Distribute all dimensions, one factor at a time, starting
       from the bottom level
       iv. Same as iii., but top first
       v.  Others (what metric do we need to optimize?)
    */
    /* Best is to consider all distributions, including the unspecified
       dimensions. Not currently planned */

    /* Try each dimension, then take as many factors are are available */
    for (id=0; id<nd; id++) {
	for (ifac=0; ifac<dimsinfo[id].nfactors; ifac++) {
	    /* skip used up factors */
	    if (dimsinfo[id].factors[ifac].pwr == 0) continue;
	    /* Are there any matching factors at this level? */
	    for (lf=0; lf<lt->nfactors; lf++) {
		if (lt->factors[lf].pwr == 0) continue;
		if (lt->factors[lf].val == dimsinfo[id].factors[ifac].val) {
		    int npow;
		    npow = lt->factors[lf].pwr;
		    if (dimsinfo[id].factors[ifac].pwr < npow)
			npow = dimsinfo[id].factors[ifac].pwr;
		    for (int i=0; i<npow; i++)
			fval *= lt->factors[lf].val;
		    /* Remove these factors from the two descriptions */
		    lt->factors[lf].pwr -= npow;
		    dimsinfo[id].factors[ifac].pwr -= npow;
		}
	    }
	}
	if (fval != 1) {
	    /* Found some factors, so done */
	    *idx = dimsinfo[id].origidx;
	    break;
	}
    }
#if 0
    for (lf=0; lf<lt->nfactors; lf++) {
	if (lt->factors[lf].pwr == 0) continue;
	for (id=0; id<nd; id++) {
	    /* Try to take all factors possible at this level */
	    for (ifac=0; ifac<dimsinfo[id].nfactors; ifac++) {
		if (dimsinfo[id].factors[ifac].pwr == 0) continue;
		if (lt->factors[lf].val == dimsinfo[id].factors[ifac].val) {
		    int npow;
		    npow = lt->factors[lf].pwr;
		    if (dimsinfo[id].factors[ifac].pwr < npow)
			npow = dimsinfo[id].factors[ifac].pwr;
		    for (int i=0; i<npow; i++)
			fval *= lt->factors[lf].val;
		    /* Remove these factors from the two descriptions */
		    lt->factors[lf].pwr -= npow;
		    dimsinfo[id].factors[ifac].pwr -= npow;
		    /* We won't match any other factors for this dimension */
		    /* Found something for this dimension. return */
		    *f   = fval;
		    *idx = dimsinfo[id].origidx;
		}
	    }
	    if (fval != 1) break;
	}
    }
#endif
    if (fval == 1) {
	/* This should not happen */
	*idx = -1;
	*f   = 0;
    }
    else {
	*f = fval;
    }
    CDBGFCALLEXIT;
    return 1;
}

/* ------------------------------------------------------------------------ */
/* Compute the max-min of the product of the old and new dimensions. This
   product is used because dimsBalance is used to consider which choices of
   dims, when composed with olddims, leads to the most balanced new dims */
PRIVATE int dimsBalance(int ndims, const int olddims[], const int dims[])
{
    int maxdim, mindim, s, i;

    maxdim = -1;
    mindim = INT_MAX;
    for (i=0; i<ndims; i++) {
	s = olddims[i] * dims[i];
	if (s > maxdim) maxdim = s;
	if (s < mindim) mindim = s;
    }
    return maxdim - mindim;
}

PRIVATE int pickOrder(int ndims, const int fixeddims[],
		      int startidx, const int olddims[], int dims[])
{
    int tmpdims[MAX_DIMS];
    int s, i, curscore;

    CDBGFCALLENTER;
    /* for all orderings of combining dims with olddims, compute the
       balance, and take the ordering with the best balance */
    /* balance is defined as maxdim-mindim. Other definitions could be
       used */
    curscore = dimsBalance(ndims, olddims, dims);
    if (startidx == ndims-1) {
	CDBGFCALLEXIT;
	return curscore;
    }
    /* Else, recurse by considering all permutations starting at
       startidx */

    if (fixeddims[startidx] != 0) {
	int k;
	/* This dimension is fixed, but try swapping dimensions > startidx */
	for (k=0; k<ndims; k++) tmpdims[k] = dims[k];
	s = pickOrder(ndims, fixeddims, startidx+1, olddims, tmpdims);
	if (s < curscore) {
	    curscore = s;
	    for (k=startidx; k<ndims; k++)
		dims[k] = tmpdims[k];
	}
    }
    else {
	for (i=startidx; i<ndims; i++) {
	    int k, kk;
	    /* skip the fixed dimensions */
	    if (fixeddims[i] != 0) continue;
	    /* Pick dimension i as the leading dimension. Compute the
	       score for all permutations on the remaining dimensions */
	    for (k=0; k<ndims; k++) tmpdims[k] = dims[k];
	    /* swap with location i if i != startidx */
	    if (i != startidx) {
		kk = tmpdims[startidx];
		tmpdims[startidx] = tmpdims[i];
		tmpdims[i] = kk;
	    }

	    s = pickOrder(ndims, fixeddims, startidx+1, olddims, tmpdims);
	    if (s < curscore) {
		curscore = s;
		for (k=startidx; k<ndims; k++)
		    dims[k] = tmpdims[k];
	    }
	}
    }
    CDBGFCALLEXIT;
    return curscore;
}


static void printFactors(FILE *fp, int n, factor_t *fac)
{
    fprintf(fp, "Factors (%d): ", n);
    for (int i=0; i<n; i++) {
	fprintf(fp, "%d^%d%s", fac[i].val, fac[i].pwr, (i==n-1)?"\n":",");
    }
    fflush(fp);
}

/* FIXME: Make these available to all of the cart routines, and save but
   do not compile ORDER_FORTRAN version */
/* Simplified from the MPIX version */
#if 0
void RankToCoords(int ndims, const int dims[], int rank, int coords[])
{
    int i, s;
    s = dims[0];
    for (i=1; i<ndims; i++)
	s *= dims[i];

    for (i=0; i<ndims; i++) {
	s = s / dims[i];
	coords[i] = rank / s;
	rank = rank - coords[i] * s;
    }
}

void CoordsToRank(int ndims, const int dims[], const int coords[], int *rank)
{
    int i, r;

    r = coords[0];
    for (i=1; i<ndims; i++) {
	r = r * dims[i] + coords[i];
    }
    *rank = r;
}
#endif
static hwlevs_t *copyHWdesc(const hwlevs_t *hwlevs)
{
    hwlevs_t *hwlevscp;

    hwlevscp          = (hwlevs_t*)malloc(sizeof(hwlevs_t));
    if (!hwlevscp) BENVi_MallocErr("hwlevs",1,"hwlevs_t");
    hwlevscp->nlevels = hwlevs->nlevels;
    hwlevscp->hwl     = (hwlevinfo_t*)malloc(hwlevs->nlevels*sizeof(hwlevinfo_t));
    if (!hwlevscp->hwl) BENVi_MallocErr("hwlevs->hwl",hwlevs->nlevels,"hwlevinfo_t");
    for (int i=0; i<hwlevs->nlevels; i++) {
	hwlevscp->hwl[i].nobj   = hwlevs->hwl[i].nobj;
	hwlevscp->hwl[i].objidx = hwlevs->hwl[i].objidx;
    }

    return hwlevscp;
}

static cartHierarchy *carthCreate(int nlevs, int ndims)
{
    cartHierarchy *carth;
    carth     = (cartHierarchy *)malloc(sizeof(cartHierarchy));
    if (!carth) BENVi_MallocErr("carth structure",1,"cartHierarchy");
    carth->cl = (cartLevel *)malloc(nlevs * sizeof(cartLevel));
    if (!carth->cl) BENVi_MallocErr("carth structure",1,"cartlevel");
    carth->nlevels = nlevs;
    carth->ndims   = ndims;

    return carth;
}

void BENVi_CarthFree(cartHierarchy *carth)
{
    if (!carth) return;
    if (carth->cl) free(carth->cl);
    free(carth);
}

/* Internal routines for conversion to/from rank and coordinates in a
   Cartesian process topology */
/* Given ndims/dims, rank, and whether C or Fortran order, return coords
   in the process topology */
/* FIXME: No use of MPI_ORDER_FORTRAN; should use a conversion routine
   for ORDER_C. Can extract, maintain the ORDER_FORTRAN version in
   case it is ever needed */
void BENVi_RankToCoords(int ndims, const int dims[], int rank,
			arrayorder_t order, int coords[])
{
    int i, s;

    CDBGFCALLENTERV("ndims=%d,dims[0]=%d,coords[0]=%d\n",
		    ndims,dims[0],coords[0]);
    s = dims[0];
    for (i=1; i<ndims; i++)
	s *= dims[i];
    /* Sanity check: 0 <= rank < dims  */
    if (rank < 0 || rank >= s) {
	fprintf(stderr, "ERROR: Rank = %d not in [0,%d) in RankToCoords\n",
		rank, s);
    }

    if (order == BENV_ORDER_FORTRAN) {
	/* "column major - first index varies fastest */
	for (i=ndims-1; i>=0; i--) {
	    s = s / dims[i];
	    coords[i] = rank / s;
	    rank = rank - coords[i] * s;
	}
    }
    else {
	/* BENV_ORDER_C - "row major" - last index varies fastest*/
	for (i=0; i<ndims; i++) {
	    s = s / dims[i];
	    coords[i] = rank / s;
	    rank = rank - coords[i] * s;
	}
    }
    CDBGFCALLEXIT;
}

/* Given ndims/dims, coords in the process topology, and whether C or Fortran
   order, return rank of the process  */
void BENVi_CoordsToRank(int ndims, const int dims[], const int coords[],
			arrayorder_t order, int *rank)
{
    int i, r;

    if (order == BENV_ORDER_FORTRAN) {
	r = coords[ndims-1];
	for (i=ndims-2; i>=0; i--) {
	    r = r * dims[i] + coords[i];
	}
    }
    else {
	r = coords[0];
	for (i=1; i<ndims; i++) {
	    r = r * dims[i] + coords[i];
	}
    }
    *rank = r;
}

hwlevs_t *BENVi_CreateHwdescFromLevs(int nlevs, int *levsize, int rank)
{
    hwlevs_t *hwlevs;
    int i, *levcoords;

    hwlevs          = (hwlevs_t*)malloc(sizeof(hwlevs_t));
    if (!hwlevs) BENVi_MallocErr("hwlevs",1,"hwlevs_t");
    hwlevs->nlevels = nlevs;
    hwlevs->hwl    = (hwlevinfo_t *)malloc(nlevs*sizeof(hwlevinfo_t));
    if (!hwlevs->hwl) BENVi_MallocErr("hwlevs->hwl",nlevs,"hwlevinfo_t");
    for (i=0; i<nlevs; i++) {
	hwlevs->hwl[i].nobj = levsize[i];
    }
    /* This is the one place where it would be more convenient for objidx
       to be a contiguous array */
    levcoords = (int *)malloc(nlevs*sizeof(int));
    BENVi_RankToCoords(nlevs, levsize, rank, BENV_ORDER_C, levcoords);
    for (i=0; i<nlevs; i++)
	hwlevs->hwl[i].objidx = levcoords[i];
    free(levcoords);

    return hwlevs;
}
void BENVi_PrintHWDesc(FILE *fp, hwlevs_t *hwlevs)
{
    fprintf(fp,"hw:\n\t%d levels\n", hwlevs->nlevels);
    /* Sanity check */
    if (hwlevs->nlevels < 0 || hwlevs->nlevels > 10) {
	fprintf(fp, "PANIC: nlevs = %d, hwlevs must be damaged\n",
		hwlevs->nlevels);
	fflush(fp);
	return;
    }
    for (int i=0; i<hwlevs->nlevels; i++) {
	fprintf(fp, "\t%d: nobj=%d, objidx=%d\n", i, hwlevs->hwl[i].nobj,
		hwlevs->hwl[i].objidx);
    }
    fflush(fp);
}
static void freeLevelSizes(hwlevs_t *levs)
{
    if (!levs) return;
    if (levs->hwl) free(levs->hwl);
    free(levs);
}
void BENVi_PrintCarth(FILE *fp, int newrank, cartHierarchy *carth)
{
    fprintf(fp, "Cart Hierarchy with %d levels and rank %d\n",
	    carth->nlevels, newrank);
    for (int i=0; i<carth->nlevels; i++) {
	fprintf(fp, "\t%d: dims =", i);
	BENV_PrintIntTuple(fp, carth->ndims, carth->cl[i].dims, 0);
	fprintf(fp, ", coords = ");
	BENV_PrintIntTuple(fp, carth->ndims, carth->cl[i].coords, 1);
    }
}

#ifdef BUILD_TEST
#include "benvutil.h"
void printUsage(FILE *fp);
/* Temporary for simple testing */
int main(int argc, char **argv)
{
    int ndims=0, *dims, nlevs=0, *levsize, np, npdim, i, k, docoords=0;
    int nperiods=0, *periods=0, *cartcoords, newrank;
    int nranks=0, *wranks;
    cartHierarchy *carth_ptr;
    hwlevs_t *hwlevs;
    static int defhw[] = { 4, 2, 6 }, defdims[] = { 8, 6 };

    /* Read input for dims and for hw hierarchy */
    for (i=1; i<argc; i++) {
	if (strcmp(argv[i], "--dims") == 0) {
	    i++;
	    dims = BENV_RstringToArray(argv[i], &ndims);
	}
	else if (strcmp(argv[i], "--ranks") == 0) {
	    i++;
	    wranks = BENV_RstringToArray(argv[i], &nranks);
	}
	else if (strcmp(argv[i], "--ndims") == 0) {
	    i++;
	    ndims = atoi(argv[i]);
	    dims  = (int *)calloc(ndims,sizeof(int));
	}
	else if (strcmp(argv[i], "--hw") == 0) {
	    i++;

	    levsize = BENV_RstringToArray(argv[i], &nlevs);
	}
	else if (strcmp(argv[i], "--periods") == 0) {
	    i++;
	    periods = BENV_RstringToArray(argv[i], &nperiods);
	}
	else if (strcmp(argv[i], "--coords") == 0)
	    docoords = 1;
	else if (strcmp(argv[i], "--default") == 0) {
	    nlevs   = 3;
	    levsize = defhw;
	    ndims   = 2;
	    dims    = defdims;
	}
	else if (strcmp(argv[i], "-v") == 0) {
	    CDBGINCRVAL(CARTRANK);
	}
	else {
	    fprintf(stderr, "Unrecognized argument %s\n", argv[i]);
	    printUsage(stderr);
	    return 1;
	}
    }

    if (!nlevs || !dims) {
	fprintf(stderr, "Missing hw and/or dims!\n");
	printUsage(stderr);
	return 1;
    }

    /* What is the test */
    /* Convert (nlevs,levsize) into the hwlevs structure, which includes
       information on "this process" */
    /* Total number of processes is produce of the levsizes */
    np = 1;
    for (k=0; k<nlevs; k++)
	np *= levsize[k];

    /* periods is all zero if not specified on input */
    if (nperiods == 0) {
	nperiods = ndims;
	periods = (int *)malloc(nperiods*sizeof(int));
	for (k=0; k<ndims; k++) periods[k] = 0;
    }

    /* Allocate storage for the cart coords */
    cartcoords = (int *)malloc(ndims*sizeof(int));

    /* Determine the number of processes from the dimensions */
    npdim = 1;
    for (k=0; k<ndims; k++) npdim *= dims[k];

    if (npdim > 0 && np != npdim) {
	fprintf(stderr, "hw levels describes %d and dims %d processes; they must be equal\n", np, npdim);
	return 1;
    }

    /* Set wranks from default if not set */
    if (nranks == 0) {
	nranks = 1;
	wranks = (int *)calloc(1,sizeof(int));
    }

    /* Form hwlevs from levels */
    for (int r=0; r<nranks; r++) {
	int rank = wranks[r];

	fprintf(stdout, "Rank %d:\n", rank);

	hwlevs = BENVi_CreateHwdescFromLevs(nlevs, levsize, rank);
	CDBGV(CARTRANK,BASIC,"hwlevs from %d levels\n",nlevs);
	CDBGCMD(CARTRANK,BASIC,BENVi_PrintHWDesc(cvar_vfp,hwlevs));

	/* Test one: distribute processes over the dims (used in the create
	   routine) */
//         BENVi_DistributeDimsOverHW(hwlevs, ndims, dims, periods);


	/* First test: determine decomposition (same for all ranks) */
	CDBG(CARTRANK,BASIC,"Create carth from hw hierarchy");
	BENVi_Nodecart_create_from_hierarchy_local(hwlevs, ndims, dims, periods,
						  cartcoords, &newrank,
						  &carth_ptr);
	//CDBG(CARTRANK,BASIC,"Cart hierarchy");
	//CDBGCMD(CARTRANK,BASIC,printCarth(cvar_vfp, newrank, carth_ptr));

	/* Print the decomposition (both complete and by level) */
	fprintf(stdout, "Process coords ");
	BENV_PrintIntTuple(stdout, ndims, cartcoords, 0);
	fprintf(stdout, " in ");
	BENV_PrintIntTuple(stdout, ndims, dims, 1);
	fprintf(stdout,"Print the decomposition by cart hierarchy levels\n");
	BENVi_PrintCarth(stdout, newrank, carth_ptr);
	fflush(stdout);

	freeLevelSizes(hwlevs);
	BENVi_CarthFree(carth_ptr);

	/* Optional: for all processes, what is the decomposition and
	   coordinates (both the mesh and the HW coordinates) */
	/* Fixme: This could be --ranks all */
	if (docoords) {
	    CDBG(CARTRANK,BASIC,"Decomposition and coordinates for each rank");
	    for (i=0; i<np; i++) {
		hwlevs = BENVi_CreateHwdescFromLevs(nlevs, levsize, i);
		CDBGV(CARTRANK,BASIC,"Rank %d:\n",i);
		CDBGCMD(CARTRANK,BASIC,BENVi_PrintHWDesc(cvar_vfp,hwlevs));
		BENVi_Nodecart_create_from_hierarchy_local(hwlevs,
							  ndims, dims, periods,
							  cartcoords,
							  &newrank, &carth_ptr);
		/* FIXME: Print results */
		freeLevelSizes(hwlevs);
	        BENVi_CarthFree(carth_ptr);
	    }
	}
    }

    return 0;
}
void printUsage(FILE *fp)
{
    fprintf(fp, "cartranktst --dims n1,n2,... --hw m1,m2,... --ranks r1,r2,... --periods 1,0,... --coords --default --ndims nd -v\n");
}


/* Allow the test to run without any MPI routines. MPI_Abort is used in
   BENVi_MallocErr */
#if 0
int MPI_Abort(int c, int rc)
{
    abort();
}
#endif
#endif
