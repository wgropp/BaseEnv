/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 * Copyright (C) by University of Illinois 2024
 */

#include <stdlib.h>
#include <stdio.h>
#define MPI_Comm int
#include "benvutil.h"
#include "benvdbg.h"
#include "cartimpl.h"

CDBGDECL(FACTOR);

/* ----------------------------------------------------------------------- */
/* This routine is used to factor the number of processes on a single node,
   and so assumes that the number of processes on a single node is no
   greater than about 1k.  Adding a few more factors would push that up
   to over 16K. */
/* Worst case need at most 6 unique factors to reach over 2310, so the
   factor arrays need not be large */
/* 10 unique factors will reach more than 6 billion */
/* This has been expanded to handle upto 1Mi processes. In this case, there
   can be at most one factor bigger than 1024, and the algorithm used here
   will find all of the other factors < 1031, leaving that one factor (if
   it exists) */

#define MAX_FACTORS 10
static int smallprimes[] = { 2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31,
			     37, 41, 43, 47, 53, 59, 61, 67, 71, 73,
			     79, 83, 89, 97, 101, 103, 107, 109, 113,
			     127, 131, 137, 139, 149, 151, 157, 163,
			     167, 173, 179, 181, 191, 193, 197, 199,
			     211, 223, 227, 229,
			     233, 239, 241, 251, 257, 263, 269, 271, 277, 281,
			     283, 293, 307, 311, 313, 317, 331, 337, 347, 349,
			     353, 359, 367, 373, 379, 383, 389, 397, 401, 409,
			     419, 421, 431, 433, 439, 443, 449, 457, 461, 463,
			     467, 479, 487, 491, 499, 503, 509, 521, 523, 541,
			     547, 557, 563, 569, 571, 577, 587, 593, 599, 601,
			     607, 613, 617, 619, 631, 641, 643, 647, 653, 659,
			     661, 673, 677, 683, 691, 701, 709, 719, 727, 733,
			     739, 743, 751, 757, 761, 769, 773, 787, 797, 809,
			     811, 821, 823, 827, 829, 839, 853, 857, 859, 863,
			     877, 881, 883, 887, 907, 911, 919, 929, 937, 941,
			     947, 953, 967, 971, 977, 983, 991, 997, 1009, 1013,
			     1019, 1021, 1031, -1};

int BENV_DimsCreateOpt(int n, int nf, factor_t *facs, int ndims, int *dims);
void BENVi_DimsCreateFromFactorsRR(int nf, factor_t *facs, int nd, int dims[]);
int BENVi_DimsPickOpt(int n, int ndivs, int dividx, const int *divs,
		      int nd, int dimidx, int *olddims, int *dims,
		      int *mind_ptr, int *maxd_ptr, int *curscore_ptr);
int dimsscore(int n, const int dims[], int *mind, int *maxd);
int dimscompare(int n, const int d1[], const int d2[]);
void sortInts(int n, int *vals);
void BENV_PrintTupleSoFar(FILE *fp, int n, const int vals[], int idx,
			  int withEOL);

/*
 * facs_ptr is allocated with malloc and must be freed by the user
 */
int BENVi_Factor(int n, int *nf_ptr, factor_t **facs_ptr)
{
    int i, nf = 0;
    factor_t *facs;

    facs = (factor_t *)malloc(MAX_FACTORS * sizeof(factor_t));
    if (!facs) BENVi_MallocErr("facs", MAX_FACTORS, "factor_t");

    /* Lots of optimizations possible.  E.g., once smallprimes > sqrt(n),
       you are done, since you've already removed the factors < sqrt(n),
       and if there is a remaining factor, it is unique. Can check if
       n < smallprimes[i]*smallprimes[i].
     */

    /* First, get factors of 2 without division */
    if ((n & 0x1) == 0) {
	int cnt = 1;
	n >>= 1;
	while ((n & 0x1) == 0) {
	    cnt++;
	    n >>= 1;
	}
	facs[0].pwr = cnt;
	facs[0].val = 2;
	nf          = 1;
    }

    for (i=1; n > 1 && smallprimes[i] > 0; i++) {
	int f = smallprimes[i];
	if ((n % f) == 0) {
	    if (nf >= MAX_FACTORS) return -1;
	    facs[nf].val = f;
	    facs[nf].pwr = 1;
	    n = n / f;
	    while ((n % f) == 0) {
		facs[nf].pwr++;
		n = n / f;
	    }
	    nf++;
	}
	else if (n < f*f) break;
    }
    if (n > 1) {
	/* Last value is prime > sqrt(n). Need to confirm, in the event
	   that the largest "smallprime" is < sqrt(n) */
	if (nf >= MAX_FACTORS) return -1;
	facs[nf].val = n;
	facs[nf].pwr = 1;
	nf++;
    }
    *nf_ptr   = nf;
    *facs_ptr = facs;
    return 0;
}


/* Given nf factors facs, create all of the divisors */
int BENVi_FactorsToDivisors(int nf, factor_t *facs,
			   int *ndivs_ptr, int **divs_ptr)
{
    int ndivs, i, j, k, *divs, val, v1, cnd, ii, np;

    /* The number of divisors is the product of (facs[i].pwr + 1) */
    ndivs = 1;
    for (i=0; i<nf; i++) {
	ndivs *= (facs[i].pwr + 1);
    }
    /* Allocate the storage */
    divs = (int *)malloc(ndivs*sizeof(int));
    if (!divs) BENVi_MallocErr("divs", ndivs, "int");

    /* We create the divisors one factor at a time, starting my making
       facs[i].pwr+1 copies of the current set of divisors, and apply
       powers of facs[i].val (starting with a power of 0) to the copies */
    /* Do the first set (no initial copy */
    k   = 0;
    val = facs[0].val;
    np  = facs[0].pwr;
    v1  = 1;
    for (j=0; j<np+1; j++) {
	divs[k++] = v1;
	v1       *= val;
    }

    for (i=1; i<nf; i++) {
	np  = facs[i].pwr;
	val = facs[i].val;
	v1  = val;
	/* make np copies of the k values. Multiply the values by v1 */
	cnd = k;
	for (j=0; j<np; j++) {
	    for (ii=0; ii<cnd; ii++) {
		divs[k+ii] = divs[k-cnd+ii] * v1;
	    }
	    k += cnd;
	}
    }
    /* Sanity check */
    if (k != ndivs) {
	fprintf(stdout, "k = %d, sbould be %d\n", k, ndivs);
    }

    /* Sort the divisors in decreasing order (? remove 1 and n?) */
    sortInts(ndivs, divs);

    *ndivs_ptr = ndivs;
    *divs_ptr  = divs;
    return 0;
}

/* */
int BENV_DimsCreateOpt(int n, int nf, factor_t *facs, int ndims, int *dims)
{
    int ndivs, *divs, olddims[10], curdif;
    int maxd, mind, rc;

    /* Get an initial guess for dims */
    BENVi_DimsCreateFromFactorsRR(nf, facs, ndims, olddims);
    CDBG(FACTOR,BASIC,"Initial dims:");
    CDBGCMD(FACTOR,BASIC,BENV_PrintIntTuple(cvar_vfp,ndims,olddims,1));

    BENVi_FactorsToDivisors(nf, facs, &ndivs, &divs);

    /* compute min/max values for dims */
    curdif = dimsscore(ndims, olddims, &mind, &maxd);

    CDBGV(FACTOR,BASIC,"Calling PickOpt for %d\n",n);
//    for (int i=0; i<ndims; i++) dims[i] = olddims[i];
    rc = BENVi_DimsPickOpt(n, ndivs, 0, divs,
			   ndims, 0, olddims, dims,
			   &mind, &maxd, &curdif);
    if (rc) {
	int i;
	for (i=0; i<ndims; i++) dims[i] = olddims[i];
	CDBG(FACTOR,BASIC,"Resulting dims:");
	CDBGCMD(FACTOR,BASIC,BENV_PrintIntTuple(cvar_vfp,ndims,dims,1));
    }
    else {
	int i;
	CDBG(FACTOR,BASIC,"Dims already optimal");
	for (i=0; i<ndims; i++) dims[i] = olddims[i];
    }
    /* FIXME: With sorted divisors, this should not be necessary */
    sortInts(ndims, dims);

    free(divs);
    return 0;
}

/* Pick the remaining dimensions from the available divisors
   Total size
   n - the number of elements being distributed across the remaining dims

   Divisors:
   ndivs - total number (size of divs)
   dividx - current divisor is divs[dividx]
   divs  - array of divisors

   Dimensions:
   nd - total number (size of dims)
   dimidx = current dimension being filled (dims[dimidx]). Dimensions lower
    than dimidx have already been filled.
   dims - array of dimensions

   Score:
   mind_ptr,maxd_ptr - pointers to values of min and max dimension, either
     in dims so far (dims[0]..dims[dimidx-1]) or in a candidate dims (not
     passed to this routine as not needed).
   curscore_ptr - pointer to the current score (?? is this needed)

   On output:
   if a better dims has been found, dims, mind, maxd, and curscore are updated,
   otherwise, the values are left unchanged.

   olddims always has the best value found
*/
int BENVi_DimsPickOpt(int n, int ndivs, int dividx, const int *divs,
		      int nd, int dimidx, int *olddims, int *dims,
		      int *mind_ptr, int *maxd_ptr, int *curscore_ptr)
{
    int i, mind=*mind_ptr, maxd=*maxd_ptr, curdif=maxd-mind;
    int foundcandidate=0;

    /* Consider all possible divisors, skipping ones that are < mind or
       > maxd, or that are > sqrt(n). Start at the current divisor as
       a divisor may be used multiple times */
    CDBGV(FACTOR,DETAIL,"PickOpt n=%d, dimidx=%d, curdif=%d, in dims=", n,dimidx,curdif);
    CDBGCMD(FACTOR,DETAIL,BENV_PrintTupleSoFar(cvar_vfp,nd,dims,dimidx,1));
    /* Fixme: Don't need dividx? Or keep so ndivs always the same */
    for (i=dividx; i<ndivs; i++) {
	int d = divs[i];
	if (d > maxd) continue;
	/* a value of d < mind may lead to a better overall score */
	/* d must be a divisor of the remaining size */
	if (d > n) continue;
	if ((n % d) != 0) continue;

	if (nd-dimidx == 2) {
	    int d2 = n / d, c;
	    if (d2 < d) continue; /* d > sqrt(nleft) */
	    /* Do we want to compare to current dims[dimidx,dimidx+1] ? */
	    /* Down to the end. See if dims with these is better than
	       olddims */
	    dims[dimidx]   = d2; /* Larger first */
	    dims[dimidx+1] = d;
	    c = dimscompare(nd, olddims, dims);
	    if (c == -1) {
		CDBGV(FACTOR,DETAIL,"Found better 2-d dims (%dx%d)\n",d2,d);
		/* dims is better */
		for (i=0; i<nd; i++) olddims[i] = dims[i];
		*curscore_ptr = dimsscore(nd, olddims, mind_ptr, maxd_ptr);
		if (*curscore_ptr == 1) break; /* can't do better (though might
						  get slightly better subarrays,
						  it probably isn't worth the effort */
	    }
	}
	else {
	    /* Pick a divisor, and optimize the remaining dimensions */
	    /* opt(ndivs, divs, dividx, nd, olddims, ndleft dims); */
	    /* Use the min/max for all of the dimensions */
	    /* Make a temp copy of dims to see what is the best we
	       can do with this approximation */
	    int newscore, j, rc; /*FIXME on newdims*/
	    int nmind = *mind_ptr, nmaxd = *maxd_ptr;
	    dims[dimidx] = d;
	    CDBGV(FACTOR,DETAIL,"PickOpt(nest) n=%d, dimidx=%d, curdif=%d, divisor=%d,curscore=%d\n", n/d,dimidx+1,curdif,d,*curscore_ptr);
	    rc = BENVi_DimsPickOpt(n/d, ndivs, dividx, divs,
				   nd, dimidx+1, olddims, dims,
				   &nmind, &nmaxd, &newscore);
	    /* Do we accept the divisor d? */
	    /* On return, dims is a valid decomposition.
	       Compare to the olddims (*curscore_ptr) for the full size
	    */
	    CDBGV(FACTOR,ALL,"PickOpt(nest) returned rc=%d\n", rc);
	    if (rc) {
		CDBGV(FACTOR,DETAIL,"PickOpt(nest) found newscore %d (old %d)\n",newscore, *curscore_ptr);
		if (newscore < *curscore_ptr) {
		    CDBG(FACTOR,DETAIL,"New dims: ");
		    CDBGCMD(FACTOR,DETAIL,BENV_PrintIntTuple(cvar_vfp,nd,dims,1));
		    /* Update olddims (current best) */
		    for (j=0; j<nd; j++) olddims[j] = dims[j];
		    curdif        = dimsscore(nd, olddims, &mind, &maxd);
		    *mind_ptr     = mind;
		    *maxd_ptr     = maxd;
		    *curscore_ptr = curdif;
		    foundcandidate = 1;
		    /* if curdif is 1, we can't do better */
		    if (curdif == 1) break;
		}
		else if (newscore == *curscore_ptr) {
		    int c;
		    /* Current newdims is at least as good */
		    fprintf(stdout, "Tie (step 2) with diff == %d\n", newscore);
		    fprintf(stdout, "\tnd=%d\n", nd);
		    c = dimscompare(nd, olddims, dims);
		    if (c == -1) {
			fprintf(stdout, "dims should be used: dims = ");
			BENV_PrintIntTuple(stdout, nd, olddims, 0);
			fprintf(stdout, ", dims = ");
			BENV_PrintIntTuple(stdout, nd, dims, 1);
			for (j=0; j<nd; j++) olddims[j] = dims[j];
			curdif        = dimsscore(nd, olddims, &mind, &maxd);
			*mind_ptr     = mind;
			*maxd_ptr     = maxd;
			*curscore_ptr = curdif;
			foundcandidate = 1;
			if (curdif == 1) break;
		    }
		    else {
			fprintf(stdout, "\tcompare gave %d\n", c);
		    }
		    fflush(stdout);
		}
	    }
	}
    }

    /* QUERY: Really do this, even if no update? */
    //*curscore_ptr = dimsscore(nd, dims, mind_ptr, maxd_ptr);

    return foundcandidate;
}

/* See projects/software/dimscreate/dimsnew.c .
   Run with OpenMPI and record failures (manyt are pretty simple) */
/* Simplified dims_create (with the expectation of using elements
   to distribute values at each level of decomposition):

   Note can't simply distribute primes. E.g., if factors are 2^3 * 3^2,
   in 2 dimensions, the optimal decomp is (2^3,3^2) or (8,9), whereas
   distributing the primes gives (2*3,2*2*3) or (6,12). This is why
   posible divisors, formed from combinations of the primes, must be
   considered.

   1. Remove any primes > sqrt(n). There will be at most one of these,
      but removing it reduces the number of possible divisors. In most
      cases, there will be no such prime.
   2. If the number of remaining primes <= remaining dimensions, just
      distribute them, one to each dimenision. Note some values might be
      1 (e.g., n==17 and 3 dimensions)
   3. If the count (pwr) of each prime is a multiple of the number of
      dimensions, then we just assign each dimension the same value
   4. At this point, there are no easy solutions. Create the possible
      divisors and create a starting position for the dims by doing a
      round-robin distribution of factors (just what leads to the (6,12)
      problem above).
      a. To create the possible divisors, create all powers of the
         first factor and then replicate that (cnt_2+1) times (Number of
	 factors in the next block).
	 The apply the powers of the second factor to each block and
	 replicate (cnt_3+1) times. Repeat until all factors used.
	 Note that the first and last divisor (1 and n) can be ignored.
   5. (See optbalance) Recursively try possibilities:
      a. for each divisor
         i. Use that in the jth position in newdims
	 ii. Recursive try all remaining divisors (position doesn't matter)
	     Note that many divisors are ineligible, as their "factors" have
	     already been consumed. This is easy to check with an integer
	     divide, but could be pruned knowing which factors have been used.
	 iii. Note that position *can* matter in reality, and we might want
	      to consider either generalizations or at least orderings of
	      the position-independent choice that takes position into
	      account (e.g., because of different performance between different
	      levels).
*/

/* */

/*
 * Distribute all factors across all dimensions of dims in a round-robin
 * fashion.
 *
 * Note: If an input dims has some values set, call this routine with
 * a temp (nd,tmpdims) where nd is the number of unset dimensions.
 *
 * Algorithm:
 * First, distribute factors to dims[0..nd-1].  The purpose is to get the
 * initial factors set and to ensure that the smallest dimension is > 1.
 * Second, distibute the remaining factors, starting with the largest, to
 * the elements of dims with the smallest index i such that
 *   dims[i-1] > dims[i]*val
 * or to dims[0] if no i satisfies.
 * For example, this will successfully distribute the factors of 100 in 3-d
 * as 5,5,4.  A pure round robin would give 10,5,2.
 *
 * Note, however, that this does not give the best decompositon for 2-d and
 * 72. This gives (3*2*2,3*2) or (12,6) instead of (3*3,2*2*2) == (9,8).
 */
void BENVi_DimsCreateFromFactorsRR(int nf, factor_t *facs, int nd, int dims[])
{
    int i, j, cnt, val;

    /* Initialize dims */
    for (i=0; i<nd; i++) dims[i] = 1;

    /* Start with the largest factors, move back to the smallest factor */
    for (i=nf-1; i>=0; i--) {
	val = facs[i].val;
	cnt = facs[i].pwr;
	for (j=0; j<cnt; j++) {
	    /* Find dimension where running total is smallest */
	    int bestval, idx, kk;
	    bestval = dims[0]*val;
	    idx     = 0;
	    for (kk=1; kk<nd; kk++) {
		if (dims[kk]*val < bestval) {
		    bestval = dims[kk]*val;
		    idx     = kk;
		}
	    }
	    dims[idx] *= val;
	}
    }
    sortInts(nd, dims);
}

/* A very basic dimscreate */
int BENVi_SimpleDimsCreate(int n, int nd, int dims[])
{
    int tmpdims[MAX_DIMS], ntd;
    int i, k, nn;
    int nf;
    factor_t *facs;

    /* determine the number of preset values */
    ntd = 0;
    for (i=0; i<nd; i++) {
	if (dims[i] == 0) ntd++;
	else {
	    nn = n/dims[i];
	    /* Test for valid dims[i] (must divide n) */
	    if (nn * dims[i] != n) {
		return -1;
	    }
	    n = nn;
	}
    }
    if (ntd == 0) {
	/* Nothing to do */
	return 0;
    }

    /* Factor the remaining values */
    BENVi_Factor(n, &nf, &facs);
    BENVi_DimsCreateFromFactorsRR(nf, facs, ntd, tmpdims);

    /* FIXME: Could consider other choices of distribution of the
     factors to provide a better balance, such as minimizing the
     max-min value of tmpdims[i] */

    free(facs);

    /* Copy tmpdims back into dims */
    k = 0;
    for (i=0; i<nd; i++) {
	if (dims[i] == 0) {
	    dims[i] = tmpdims[k++];
	}
    }
    sortInts(nd, dims);

    return 0;
}
/* Determine the score (balance) in the values of dims */
int dimsscore(int n, const int dims[], int *mind_ptr, int *maxd_ptr)
{
    int i, mind, maxd;
    mind = maxd = dims[0];
    for (i=1; i<n; i++) {
	if (dims[i] < mind) mind = dims[i];
	else if (dims[i] > maxd) maxd = dims[i];
    }
    *mind_ptr = mind;
    *maxd_ptr = maxd;
    return (maxd-mind);
}

/* Compare d1 and d2 and return 1 for d1 "better" than d2, -1 for d2
   "better" than d1 and 0 for a tie.
   better is based on the differenece between the largest and smallest
   dimension. When there is a tie, subarrays are compared.
*/
int dimscompare(int n, const int d1[], const int d2[])
{
    int rc, i, mind, maxd, d1s, d2s;
    int d1sort[11], d2sort[11];
    for (i=0; i<n; i++) {d1sort[i] = d1[i]; d2sort[i] = d2[i];}
    sortInts(n, d1sort);
    sortInts(n, d2sort);
    d1s = dimsscore(n, d1sort, &mind, &maxd);
    d2s = dimsscore(n, d2sort, &mind, &maxd);
    i = 1;
    while (d1s == d2s && i < n) {
	d1s = dimsscore(n-i, d1sort+i, &mind, &maxd);
	d2s = dimsscore(n-i, d2sort+i, &mind, &maxd);
	if (d1s != d2s) break;
	i++;
    }
    if (d1s < d2s) rc = 1;
    else if (d1s > d2s) rc = -1;
    else rc = 0;
    return rc;
}

/* Sort vals in decreasing order */
void sortInts(int n, int *vals)
{
//    printf("Enter sort of %d:", n);BENV_PrintIntTuple(stdout, n, vals, 1);
    if (n < 2) return;
    if (n == 2) {
	if (vals[1] > vals[0]) {
//	    printf("\tswapping %d and %d\n", vals[0], vals[1]);
	    int t = vals[1];
	    vals[1] = vals[0];
	    vals[0] = t;
	}
    }
    else {
	int m, i1, i2, *v2, k;
	sortInts(n/2, vals);
	m = n - n/2;
	sortInts(m, vals+n/2);
	/* Merge the two lists */
//	printf("\tMerging sorted lists:");BENV_PrintIntTuple(stdout,n/2,vals,0);
//	BENV_PrintIntTuple(stdout,m,vals+n/2,1);
	i1 = 0;
	i2 = n/2;
	k = 0;
	v2 = (int *)malloc(n * sizeof(int));
	while (i1 < n/2 && i2 < n) {
	    if (vals[i1] > vals[i2]) {
		v2[k++] = vals[i1++];
	    }
	    else {
		v2[k++] = vals[i2++];
	    }
	}
	/* Handle remainders ?? */
	while (i1 < n/2) {
	    v2[k++] = vals[i1++];
	}
	while (i2 < n) {
	    v2[k++] = vals[i2++];
	}
	for (i1=0; i1<n; i1++) vals[i1] = v2[i1];
	free(v2);
    }
//    printf("Exit sort of %d:", n);BENV_PrintIntTuple(stdout, n, vals, 1);
}

/* Like print tuple, but tuple is only defined through (including) index idx.
   n is probably unnecessary, but would allow printing the values after idx */
void BENV_PrintTupleSoFar(FILE *fp, int n, const int vals[], int idx,
			  int withEOL)
{
    int i;
    fputs("(", fp);
    for (i=0; i<idx; i++) {
	fprintf(fp,"%d%s", vals[i], (i!=idx-1) ? "," : "...");
    }
    fputs(")", fp);
    if (withEOL) fputs("\n", fp);
}

#ifdef BUILD_FACTOR_TEST
#include <string.h>
#include "benvutil.h"
void printFactors(FILE *fp, int n, factor_t *fac);
int readlistofints(const char *s, int **vals);
void printFactors(FILE *fp, int n, factor_t *fac)
{
    fprintf(fp, "Factors (%d): ", n);
    for (int i=0; i<n; i++) {
	fprintf(fp, "%d^%d%s", fac[i].val, fac[i].pwr, (i==n-1)?"\n":",");
    }
    fflush(fp);
}
int readlistofints(const char *s, int **vals)
{
    const char *p;
    int nvals = 0, *v, k;

    if (!s) return 0;

    /* Determine number of elements (1 + number of commas) */
    p = s;
    if (*p) nvals = 1;
    while (*p) {
	if (*p++ == ',') nvals++;
    }

    /* Allocate storage */
    v = (int *)malloc(nvals * sizeof(int));
    for (k=0; k<nvals; k++) v[k] = 0;
    /* scan and save values */
    p = s;
    k = 0;
    while (*p) {
	if (*p == ',') { k++; }
	else v[k] = v[k]*10 + (*p - '0');
	p++;
    }
    *vals = v;
    return nvals;
}

void checkValidDims(int val, int ndims, const int dims[])
{
    int v, i;
    v = dims[0];
    for (i=1; i<ndims; i++) v *= dims[i];
    if (val != v) {
	fprintf(stderr, "****Expected %d got %d\n", val, v);
    }
}
int *dimsListToArray(const char *str, int *nv);
    int arrayCompare(int n, const int *v1, const int *v2);
#include <errno.h>
#include <ctype.h>
/* Other values to try:
   3168 (18*16*11)
   5460 (21*20*13)
   6552 (24*21*13)
   225000 (9*8*5*5*5*5*5), not (10*6*6*5*5*5*5)
   See also the file tests/f.vals (all of the values that Open MPI gets wrong)
 */
int main(int argc, char **argv)
{
    int i, j, *vals=0, nvals, *ndims=0;
    int v = 0; /*Verbosity level */
    char **best=0;
    /* defvals are values that cause problems with Open MPI */
    static int defvals[] = { 72, 180, 240, 288, 336, 420, 450, 504, 540, 600,
			   648, 675, 720, 756, 792, 800, 864, 924, 945, 960,
			   1050, 1056, 1092, 1120, 1152, 1188, 1248, 1260,
			   1320, 1323, 1344, 1400, 1404, 1560, 1568, 1575,
			   1620, 1680, 1716, 1800, 1836, 1872, 1960, 1980,
			   2016, 2040,
			   432, 576, 936, 1224, 1368, 1620, 1656, 2088,
			   2592, 3024, 3456, 3528, 3888, 4032, -1 };
    /* Another good set, used in the dimstest program, was
       72 (9,8); 72 (4,3,3,2), 320 (20,16), 361 (19,19) */
    /* Get a list of integers to factor */
    for (i=1; i<argc; i++) {
	if (strcmp(argv[i], "--n") == 0) {
	    i++;
	    nvals = readlistofints(argv[i], &vals);
	}
	else if (strcmp(argv[i], "-v") == 0) {
	    v++;
	    CDBGSET(FACTOR, v);
	}
	else if (strcmp(argv[i], "--file") == 0) {
	    FILE *fp;
	    int vsize = 4000;
	    i++;
	    fp = fopen(argv[i], "r");
	    if (!fp) {
		fprintf(stderr, "Unable to open file %s\n", argv[i]);
		return 1;
	    }
	    nvals = 0;
	    vals = (int *)malloc(vsize*sizeof(int));
	    ndims= (int *)malloc(vsize*sizeof(int));
	    best= (char **)malloc(vsize*sizeof(char *));
	    errno = 0;
	    while (!feof(fp)) {
		int rc;
		if (nvals >= vsize-1) {
		    vsize += 4000;
		    vals  = (int *)realloc(vals, vsize*sizeof(int));
		    ndims = (int *)realloc(ndims, vsize*sizeof(int));
		    best  = (char **)realloc(best, vsize*sizeof(char *));
		    if (!vals || !ndims) {
			fprintf(stderr, "Realloc failed!\n");
			return 1;
		    }
		    fprintf(stderr, "Enlarging vals to %d\n", vsize);
		    fflush(stderr);
		}
		int v1, v2;
		char buf[81], *bp, vbuf[81];
		bp = fgets(buf, 81, fp);
		if (bp) {
		    /* Use sscanf on the string */
		    rc =sscanf(buf, "%d\t%d\t%s", &v1, &v2, vbuf);
		    if (rc != 3) {
			fprintf(stderr, "sscanf returned %d\n", rc);
		    }
		}
		ndims[nvals] = v1;
		vals[nvals]  = v2;
		best[nvals]  = strdup(vbuf);
//		fprintf(stderr, "Read %d %d\n", ndims[nvals], vals[nvals]);
		if (ndims[nvals] > 10) {
		    fprintf(stderr, "Invalid value for ndims[%d] = %d\n",
			    nvals, ndims[nvals]);
		    fprintf(stderr, "Last value read is %d %d\n",
			    vals[nvals-1], ndims[nvals-1]);
		    fflush(stderr);
		}
		else
		    nvals++;
	    }
	    fprintf(stderr, "Read %d values\n", nvals); fflush(stderr);
	}
	else {
	    fprintf(stderr, "Unrecognized argument %s\n", argv[i]);
	    return 1;
	}
    }

    if (!vals) {
	fprintf(stderr, "Using default vals...\n");
	nvals = 0;
	while (defvals[nvals] > 0) nvals++;
	vals = defvals;
    }

    /* for each value, factor it and print the vactors */
    for (i=0; i<nvals; i++) {
	factor_t *facptr;
	int      nfac, dims[10], ndivs, *divs;
	BENVi_Factor(vals[i], &nfac, &facptr);
	fprintf(stdout, "\n%d: ", vals[i]);
	printFactors(stdout, nfac, facptr);

	if (ndims) {
	    /* Do dims create for ndims[i] */
//	    fprintf(stderr, "create for %d in %d dimensions\n", vals[i], ndims[i]); fflush(stderr);
	    for (j=0; j<ndims[i]; j++) dims[j] = 0;
	    BENVi_DimsCreateFromFactorsRR(nfac, facptr, ndims[i], dims);
	    fprintf(stdout, "Decomp in %dd: ", ndims[i]);
	    BENV_PrintIntTuple(stdout, ndims[i], dims, 1);
	    checkValidDims(vals[i], ndims[i], dims);

	    /* Create divisors */
	    BENVi_FactorsToDivisors(nfac, facptr, &ndivs, &divs);
	    /* print the list of divisors */
	    fprintf(stdout, "Divisors:");
	    for (j=0; j<ndivs; j++) {
		fprintf(stdout, "%d,", divs[j]);
	    }
	    fputs("\n", stdout);

	    BENV_DimsCreateOpt(vals[i], nfac, facptr, ndims[i], dims);
	    fprintf(stdout, "Decomp in %dd (opt): ", ndims[i]);
	    BENV_PrintIntTuple(stdout, ndims[i], dims, 0);
	    fprintf(stdout, ", best is %s\n", best[i]);
	    checkValidDims(vals[i], ndims[i], dims);
	    int nb, *bestvals=dimsListToArray(best[i],&nb);
	    if (nb != ndims[i] || !arrayCompare(nb, dims, bestvals)) {
		/* See if the diff of the extremes is the same */
		int d1 = dims[0] - dims[nb-1];
		int d2 = bestvals[0] - bestvals[nb-1];
		if (d1 != d2) {
		    fprintf(stderr, "Expected (n=%d) ", vals[i]);
		}
		else {
		    fprintf(stderr, "Expected (but same diff=%d) (n=%d) ",
			    d1, vals[i]);
		}
		    BENV_PrintIntTuple(stderr, nb, bestvals, 0);
		    fprintf(stderr, " got ");
		    BENV_PrintIntTuple(stderr, ndims[i], dims, 1);
	    }
	    free(bestvals);
	}
	else {
	    /* Do dims create for 2 and 3d */
	    for (j=0; j<2; j++) dims[j] = 0;
	    BENVi_DimsCreateFromFactorsRR(nfac, facptr, 2, dims);
	    fprintf(stdout, "Decomp in 2d: ");
	    BENV_PrintIntTuple(stdout, 2, dims, 1);
	    checkValidDims(vals[i], 2, dims);

	    for (j=0; j<3; j++) dims[j] = 0;
	    BENVi_DimsCreateFromFactorsRR(nfac, facptr, 3, dims);
	    fprintf(stdout, "Decomp in 3d: ");
	    BENV_PrintIntTuple(stdout, 3, dims, 1);
	    checkValidDims(vals[i], 3, dims);

	    /* Create divisors */
	    BENVi_FactorsToDivisors(nfac, facptr, &ndivs, &divs);
	    /* print the list of divisors */
	    fprintf(stdout, "Divsors:");
	    for (j=0; j<ndivs; j++) {
		fprintf(stdout, "%d,", divs[j]);
	    }
	    fputs("\n", stdout);

	    BENV_DimsCreateOpt(vals[i], nfac, facptr, 2, dims);
	    fprintf(stdout, "Decomp in 2d (opt): ");
	    BENV_PrintIntTuple(stdout, 2, dims, 1);
	    checkValidDims(vals[i], 2, dims);

	    BENV_DimsCreateOpt(vals[i], nfac, facptr, 3, dims);
	    fprintf(stdout, "Decomp in 3d (opt): ");
	    BENV_PrintIntTuple(stdout, 3, dims, 1);
	    checkValidDims(vals[i], 3, dims);
	}
	/* Need to free factors */
	free(facptr);

	/* Free divs */
	free(divs);
    }
    return 0;
}

/* Convert a string of the form 1x2x3.. to an integer array of values */
int *dimsListToArray(const char *str, int *nv)
{
    const char *p = str;
    int *vals=0, v=0, vi=0;

    vals = (int *)malloc(10*sizeof(int));
    while (*p) {
	if (*p == 'x' || *p == 'X') {
	    vals[vi++] = v;
	    v = 0;
	}
	else if (isdigit(*p)) {
	    v = 10*v + (*p-'0');
	}
	p++;
    }
    /* Last value */
    if (v > 0) {
	vals[vi++] = v;
    }
    *nv = vi;
    return vals;
}
int arrayCompare(int n, const int *v1, const int *v2)
{
    int i;
    for (i=0; i<n; i++) {
	if (v1[i] != v2[i]) return 0;
    }
    return 1;
}
#endif
