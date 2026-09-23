/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 * Copyright (C) by University of Illinois 2022
 */
#include "benvconf.h"
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>

#include "mpi.h"
#include "benvdbg.h"
#include "benvutil.h"
#include "hwdescnew.h"
#include "cartrepl.h"
#include "cartimplm.h"

//TEMP
#include "seq.h"

#define PRIVATE static
/*
static int nodecartDelFn(MPI_Comm comm, int keyval, void *attr, void *estate);
static int nodeinfoDelFn(MPI_Comm comm, int keyval, void *attr, void *estate);
static int nodetopoCopyFn(MPI_Comm comm, int keyval, void *estate,
			  void *attr_in, void *attr_out, int *flag);
static int nodetopoDelFn(MPI_Comm comm, int keyval, void *attr, void *estate);
static int nodeinfoKeyval = MPI_KEYVAL_INVALID,
    nodecartKeyval = MPI_KEYVAL_INVALID,
    procnodeKeyval = MPI_KEYVAL_INVALID,
    nodetopoKeyval = MPI_KEYVAL_INVALID;
*/

/* This structure holds a condensed and unified set of information on the
   available levels of hierarchy. Typically starts from node and may
   include socket or numa region.
*/
typedef struct {
    int nlevs;      /* Number of available levels */
    int *nobj,      /* Number of "objects" (e.g., nodes) at each level */
	*objidx;    /* Index (starting from 0) of the object at this
		       level of the calling process (e.g., node number) */
} lev_t;

static int checkConsistentLevels(MPI_Comm comm, lev_t *levs);
static lev_t *getLevelSizes(const hwdescCtx *hwc);

static void freeLevelSizes(lev_t *levs);

/* Debugging */
CDBGDECL(NODECART);
CDBGEDECL(ARGV);
CDBGFCALLDECL;

PRIVATE int dimsBalance(int ndims, const int olddims[], const int dims[]);
PRIVATE int pickOrder(int ndims, int startidx, const int olddims[], int dims[]);

/*@
  MPIX_Nodecart_cvar_set - MPI-style cvar support for nodecart routines

Input Parameters:
+ name - Name of parameter
- value - Value for parameter

Notes:
The only name support is 'debug', and the values can be zero or one.
  @*/
void MPIX_Nodecart_cvar_set(const char *name, int value)
{
    if (strcmp(name, "debug") == 0)
	cvar_benv_NODECART_verbose = value;
    else {
	fprintf(stderr, "Unrecognized cvar %s\n", name); fflush(stderr);
    }
}

/* Creation routines for a node-aware Cartesian process topology
 *
 * MPIX_Nodecart_create - A replacement for MPI_Cart_create, with the same
 * arguments. Relies on access to node hierarchy information (where??)
 * MPIX_Nodecart_create_from_hierarchy - Actually does the work, given
 * a hierarchy (in ??)
 */

/*@
 MPIX_Nodecart_create - Create a Cartesian communicator, using information
  about which nodes are on the same process.

 Input Parameters:
+ comm_old - input communicator (handle)
. ndims - number of dimensions of Cartesian grid (integer)
. dims - integer array of size ndims specifying the number of processes in
  each dimension
. periods - logical array of size ndims specifying whether the grid is
  periodic (true) or not (false) in each dimension
- reorder - ranking may be reordered (true) or not (false) (logical)

Output Parameters:
. comm_cart - communicator with new Cartesian topology (handle)

.N mpireturnvalue

 Notes:
 Like 'MPI_Cart_create', the values for dims must be provided on input.
 An option to consider is to allow zero values for elements of dims,
 and then let this routine choose the dimensions to best fit the underlying
 physical hardware.

 The routine 'MPIX_Comm_dims_create' may be used to determine good values
 for 'dims'.

 The control variable 'cvar_nodecart_verbose' may be set to a positive value
 to cause output to be generated about this routine''s operation.
  @*/
int MPIX_Nodecart_create(MPI_Comm comm_old, int ndims, const int dims[],
			 const int periods[], int reorder,
			 MPI_Comm *comm_cart)
{
    hwdescCtx *hwdesc=0;
    int      rc, newrank;
    int      *cartcoords=0;
    cartHierarchy *carth;
    int      newdims[MAX_DIMS];

    CDBGFCALLENTER;
    if (!reorder) {
	/* Simply add the topology information, using the existing
	   ranks of the processes */
	/* FIXME: Given rank of process, set the attributes */
	CDBGFCALLEXIT;
	return MPI_SUCCESS;
    }

    /* Get the node hierarchy from the communicator, or if not available,
       create it and attach it as an attribute */
    rc = BENV_HwdescGetDescFromComm(comm_old, &hwdesc);
    if (!hwdesc) {
	// This won't work: Requires a non-null parms
	BENV_HwdescGetDescGeneral(comm_old, BENV_HWDESC_USE_ALL, 0, &hwdesc);
//	nlevels         = hwc->nlevel;
	CDBG(NODECART,ALL,"About to save hwinfo on old comm");
	BENV_HwdescSaveDescToComm(hwdesc, comm_old);
	CDBG(NODECART,ALL,"...Done saving hwinfo on old comm");
    }

    /* TODO: If reorder is false, just use existing order */

    /* create_from_hierarchy is more general and doesn't require a
       set of dims values. But if the values are all set, then
       we can just use the array dims */
    /* Create the node cart from the hierarchy */
    cartcoords = (int *)malloc(ndims*sizeof(int));
    *comm_cart = 0;  /* DEBUGGING */
    for (int i=0; i<ndims; i++) newdims[i] = dims[i];
    rc = MPIX_Nodecart_create_from_hierarchy(comm_old, hwdesc,
					     ndims, newdims, periods,
					     cartcoords, &newrank, &carth);
    CDBGV(NODECART,DETAIL,"Created nodecart info, rc=%d\n", rc);

    /* Call the appropriate error handler if rc != MPI_SUCCESS */
    if (rc != MPI_SUCCESS) {
	CDBGFCALLEXIT;
	return rc;
    }
    CDBGV(NODECART,ALL,"topo info %p\n", carth);

    MPI_Comm_split(comm_old, 0, newrank, comm_cart);

    /* Add topology information to comm_cart */
    CDBG(NODECART,ALL,"About to save topo info on new comm");
    MPIXI_NodecartSetTopoInfo(*comm_cart, ndims, newdims, cartcoords,
			      periods, carth);
    CDBG(NODECART,ALL,"Done saving topo info on new comm");

    CDBGFCALLEXIT;
    return MPI_SUCCESS;
}
/* THIS ONE USES THE NEWER HWDESC */

/* FIXME: SHOULD THIS USE THE CARTRANK IMPLEMENTATION (WHICH IS INDEPENDENT
   OF MPI AND DOES NOT REQUIRE PARALLEL PROCESSES */

/*@
 MPIX_Nodecart_create_from_hierarchy - Generate a Cartesian
 decomposition of processes using a hierarchy description

Input Parameters:
+ comm - Input communicator
. hwdesc - Node hierarchy array of type hwdesc_t; see 'BENV_HwdescGetLocal'
. nlevels - Depth of hierarchy array
. ndims   - Number of dimensions
- periods - flag indicating whether mesh is periodic in each dimension

Input/Output Parameters:
. dims    - On input, preselected dimensions of the Cartesian
decomposition, if any. On output, the dimensions of the cartcom

Output Parameters:
+ cartcoords - Coordinates in of the calling process in the Cartesian process
  topology
. newrank - rank of the process in the new Cartesian process topology,
            assuming C ordering of the coordinates
- carth - hardware-aware Cartesian hierarchy

.N mpireturnvalue

Notes:
This routine also adds an attribute to the 'cartcomm' that allows access to
the values needed to implement the MPI Cartesian process topology information
routines, e.g., 'MPI_Cart_coords'.  The value 'periods' is used for this
purpose and (currently) not used to determine the process mapping or
dimensions.

The only collective part of this routine is a check on consistency
across all processes in 'comm'.
@*/
int MPIX_Nodecart_create_from_hierarchy(MPI_Comm comm, const hwdescCtx *hwc,
					int ndims, int dims[],
					const int periods[],
					int cartcoords[], int *newrank,
					struct cartHierarchy **carth_ptr)
{
    cartHierarchy *carth;
    int olddims[MAX_DIMS];     /* Used to save the dimensions determined so
				  far. At the end, the final dimensions of
			          the Cartesian mesh */
    int lev, i;
    lev_t *levs;

    CDBGFCALLENTER;
    /* FIXME: It might be better to have a separate, fully local routine
       for determining the decompostion, given an array of levels */
    /* Extract from hwdesc an array of the number of "objects" (e.g.,
       nodes, sockets, numa regions) at each of nsizes levels */
    /* FIXME: Is there a new routine for this??? */
    levs = getLevelSizes(hwc);
    /* Check for uniformity across processes and for nsizes > 0 */
    if (checkConsistentLevels(comm, levs) != MPI_SUCCESS) {
	CDBGFCALLEXIT;
	return MPI_ERR_OTHER;
    }
#if 0
    /* TMP: print out the level sizes */
    printf("Number of levels = %d\n", levs->nlevs);
    BENV_SeqBegin(comm);
    for (int j=0; j<levs->nlevs; j++) {
	printf("\tLevel %d: nobjs=%d, my idx=%d\n", j, levs->nobj[j],
	       levs->objidx[j]);
    }
    fflush(stdout);
    BENV_SeqEnd(comm);
    /* TMP ends here */
#endif
    carth     = (cartHierarchy *)malloc(sizeof(cartHierarchy));
    carth->cl = (cartLevel *)malloc(levs->nlevs * sizeof(cartLevel));
    carth->nlevels = levs->nlevs;
    carth->ndims   = ndims;
    for (i=0; i<ndims; i++) olddims[i] = 1;
    for (lev=0; lev<levs->nlevs; lev++) {
	for (i=0; i<ndims; i++) carth->cl[lev].dims[i] = 0;
	/* Factor the size, pick a "nice" decomposition */
	/* FIXME: Do not rely on Dims_create (but also allow it) */
	MPI_Dims_create(levs->nobj[lev], ndims, carth->cl[lev].dims);
	CDBGV(NODECART,BASIC,"\tLevel %d: (2donly)decomp from nobjs=%d to (%d,%d)\n",
		    lev, levs->nobj[lev], carth->cl[lev].dims[0],
		    carth->cl[lev].dims[1]);

	/* Reorder dimensions to approximate balance */
	pickOrder(ndims, 0, olddims, carth->cl[lev].dims);
	for (i=0; i<ndims; i++) olddims[i] *= carth->cl[lev].dims[i];

	/* Determine the coords of this process in the Cartesian grid
	   at this level */
	BENVi_RankToCoords(ndims, carth->cl[lev].dims, levs->objidx[lev],
			   BENV_ORDER_C, carth->cl[lev].coords);
	CDBGV(NODECART,BASIC,"\tLevel %d: (2donly)Computed coords (%d,%d) in (%d,%d) from objidx=%d\n",
		 lev, carth->cl[lev].coords[0], carth->cl[lev].coords[1],
	      carth->cl[lev].dims[0], carth->cl[lev].dims[1],
	    levs->objidx[lev]);
    }
    /* Combine dimensions and coordinates across the levels to get the
       overall representation */
    /* FIXME: Only correct for all values <= 0 */
    /* To handle constraints on dims, need to save all sizes and their
       factors, and extract values to match.  See the implementation
       of MPI_Dims_create (at least my implementation for mpich) */
    for (i=0; i<ndims; i++) {
	olddims[i]    = carth->cl[0].dims[i];
	cartcoords[i] = carth->cl[0].coords[i];
    }
    for (lev=1; lev<levs->nlevs; lev++) {
	for (i=0; i<ndims; i++) {
	    olddims[i] *= carth->cl[lev].dims[i];
	    cartcoords[i] = carth->cl[lev].coords[i] +
		carth->cl[lev].dims[i] * cartcoords[i];
	}
    }
    /* Convert coords into an overall rank */
    /* DIFFERENCE: Forces order == MPI_ORDER_C. But only use picks this
       order */
    BENVi_CoordsToRank(ndims, olddims, cartcoords, BENV_ORDER_C, newrank);
    if (carth_ptr) {
	*carth_ptr = carth;
    }
    else {
	free(carth);
    }
    /* Save the overall dimensions */
    for (i=0; i<ndims; i++) dims[i] = olddims[i];

    freeLevelSizes(levs);

    CDBGFCALLEXIT;
    return MPI_SUCCESS;
}

/* ------------------------------------------------------------------------ */
/* Compute the balance in an array of dimensions, defined as the difference
   between the maximum and minimum value in the product of
   olddims[i]*dims[i]. The product is used because we are looking at the
   impact of using the values in dims, given olddims.
*/
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

/* Pick the best order of dims, given "best" defined by dimsBalance. While
 olddims is ordered, the values in dims are not ordered, and this routine
 selects the best ordering. */
PRIVATE int pickOrder(int ndims, int startidx, const int olddims[], int dims[])
{
    int tmpdims[MAX_DIMS];
    int s, i, curscore;
    /* for all orderings of combining dims with olddims, compute the
       balance, and take the ordering with the best balance */
    /* balance is defined as maxdim-mindim. Other definitions could be
       used */
    curscore = dimsBalance(ndims, olddims, dims);
    if (startidx == ndims-1) {
	return curscore;
    }
    /* Else, recurse by considering all permutations starting at
       startidx */

    for (i=startidx; i<ndims; i++) {
	int k, kk;
	/* Pick dimension i as the leading dimension. Compute the
	   score for all permutations on the remaining dimensions */
	for (k=0; k<ndims; k++) tmpdims[k] = dims[k];
	/* swap with location i */
	kk = tmpdims[startidx];
	tmpdims[startidx] = tmpdims[i];
	tmpdims[i] = kk;

	s = pickOrder(ndims, startidx+1, olddims, tmpdims);
	if (s < curscore) {
	    curscore = s;
	    for (k=startidx; k<ndims; k++)
		dims[k] = tmpdims[k];
	}
    }
    return curscore;
}

/* Next on the todo list
   1. the node information (local)
   2. the node information (collective and consistant)
   3. Using this info for smptest and for halo exchange codes,
   starting with the examples in the MPI tutorial.
   4. Output in cannonical order for mesh, including an integer mesh
      of ranks (from MPI_COMM_WORLD and from new comm). Use MPI IO and
      datatypes.  Look at what is used in current tutorial.
*/

/*
  Working from a hw description, determine the number of relevant objects
  at each level, skipping any levels with a single object (typically the
  parallel machine (level 0) but could also be a single socket on a node.

  This is a local routine.
  The array returned is allocated with malloc and must be freed
  when no longer needed.
 */
/*
  Fixme:
  This should return information from the collinfo: That is different
  from the objinfo, which is more absolute about hardware and may not
  have unique hardware for each process (e.g., if there are multiple processes
  per core, as there are in testing).

 */
static lev_t *getLevelSizes(const hwdescCtx *hwc)
{
    int lev, k, nsizes, np;
    lev_t *levptr;
    int nlevels = hwc->nlevel;

    /* First, find the number of levels with more than one member. */
    nsizes = 0;
    for (lev=0; lev<hwc->nlevel; lev++) {
	if (hwc->objinfo[lev].nobj == 1) continue;
        nsizes++;
    }
    /* Second, if the bottom level has communicators with more than one
       process, add one to the number of levels */
    MPI_Comm_size(hwc->collinfo[nlevels-1].objcomm, &np);
    if (np > 1) nsizes++;

    /* Allocate an array and add values */
    levptr = (lev_t *)malloc(sizeof(lev_t));
    levptr->nobj = (int *)malloc(2*nsizes * sizeof(int));
    levptr->objidx = levptr->nobj + nsizes;

    k = 0;
    for (lev=0; lev<nlevels; lev++) {
	if (hwc->objinfo[lev].nobj == 1) continue;
//        levptr->nobj[k]   = hwc->objinfo[lev].nobj;
//        levptr->objidx[k] = hwc->objinfo[lev].objidx;
// Not right FIXME!!!
//	MPI_Comm_size(hwc->collinfo[lev].objcomm, &levptr->nobj[k]);
//	MPI_Comm_rank(hwc->collinfo[lev].objcomm, &levptr->objidx[k]);
	levptr->nobj[k]   = hwc->collinfo[lev].nSiblings;
	levptr->objidx[k] = hwc->collinfo[lev].siblingNum;
	k++;
    }
    if (np > 1) {
        levptr->nobj[k] = np;
        MPI_Comm_rank(hwc->collinfo[nlevels-1].objcomm, &levptr->objidx[k]);
        k++;
    }
    levptr->nlevs = k;
    return levptr;
}

/* check that all processes have the same values for nlev and sizes[*] */
int checkConsistentLevels(MPI_Comm comm, lev_t *levptr)
{
    int *check, myrank, i;

    MPI_Comm_rank(comm, &myrank);
    check = (int *)malloc(levptr->nlevs*2*sizeof(int));
    check[0] = levptr->nlevs;
    check[1] = -check[0];
    MPI_Allreduce(MPI_IN_PLACE, check, 2, MPI_INT, MPI_MAX, comm);
    if (check[0] != -check[1]) {
	if (myrank == 0)
	    fprintf(stderr, "Number of levels not consistent\n");
	free(check);
	return MPI_ERR_OTHER;
    }
    for (i=0; i<levptr->nlevs; i++) {
	check[i]      = levptr->nobj[i];
	check[i+levptr->nlevs] = -check[i];
    }
    MPI_Allreduce(MPI_IN_PLACE, check, 2*levptr->nlevs, MPI_INT, MPI_MAX, comm);
    for (i=0; i<levptr->nlevs; i++) {
	if (check[i] != -check[i+levptr->nlevs]) {
	    if (myrank == 0)
		fprintf(stderr, "Level %d has inconsistent values\n", i);
	    free(check);
	    return MPI_ERR_OTHER;
	}
    }
    free(check);
    return MPI_SUCCESS;
}

static void freeLevelSizes(lev_t *levs)
{
    if (!levs) return;
    if (levs->nobj)
	free(levs->nobj);
    free(levs);
}

/*@ BENV_NodecartArgDebug - Look for debug options for nodecart routines

Input Parameters:
+ argc - Argument count
. argv - Argument vector
- prefix - Arguments have this prefix; may be null. See below

Input/Output Parameter:
. argcnt - pointer to the index of the current argument. Will be updated
 if a nodecart parameter is found by the number of values read, not counting
 the argument itself.

Return Value:
Returns 1 if a known argument value is found, zero otherwise.

Notes:
Recognizes the debug class - 'nodecart'. Recognizes
'-debugclass' as the argument name. Currently, the prefix is ignored.
The class may be followed with ':b', ':d', or ':a' for basic, detail, or all
debug information respectively.

See also:
BENV_DebugArgClass, BENV_DebugArgRank
  @*/
int BENV_NodecartArgDebug(int argc, char **argv, int *argcnt,
			  const char *prefix)
{
    int rc=0;
    static const char *classes[] = { "nodecart" };
    static int *classval[] = { &cvar_benv_NODECART_verbose, };

    CDBGV(ARGV,BASIC,"Starting NodecartArgDebug with prefix %s and next arg %s\n",
	  prefix, argv[*argcnt]);
    /* FIXME: Ignore prefix or allow but not require? */
    /* Debug arg rank is not included so these routines can work without MPI */
    rc = BENV_DebugArgClass(argc, argv, argcnt, 1, classes, classval);
    if (rc == -1) {
	/* DebugArgClass returns -1 if class not recognized. Ignore */
	rc = 0;
    }

    CDBGV(ARGV,BASIC,"Ending NodecartArgDebug rc=%d\n", rc);
    return rc;
}
