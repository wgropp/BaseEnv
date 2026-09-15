/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 * Copyright (C) by University of Illinois 2022
 */

#include "benvconf.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "mpi.h"
#include "benvutil.h"
#ifdef USE_OLD
#include "hwdesc.h"
#else
#include "hwdescnew.h"
#endif
#include "cartrepl.h"
#include "cartimplm.h"

/* This file contains routines to provide replacements for the MPI cartesian
   process topology routines, except for Cart_create.
   These routines simply provide access to internal data; thus, they
   work with different implementations of cart_create.

   For this to work, there needs to be a common set of fields that are
   stored in the associated attribute on the communicator
*/

/*
 * MPIX_Nodecart_shift   - Like MPI_Cart_shift
 * MPIX_Nodecart_coords  - Like MPI_Cart_coords
 * MPIX_Nodecart_get     - Like MPI_Cart_get
 * MPIX_Nodecart_rank    - Like MPI_Cart_rank
 * MPIX_Nodecart_dim_get - Like MPI_Cartdim_get
 * MPIX_Nodecart_sub     - Like MPI_Cart_sub
 */

/* This typdedef has only the information needed for the general cart
   topology routines */
typedef struct {
    int ndim; /* Number of dimensions */
    int dims[MAX_DIMS], coords[MAX_DIMS], periodic[MAX_DIMS];
    /* this saves the node (etc.) aware information about the decomposition
       that may be used in, for example, cart_sub */
    int           nlevels;  /* Number of levels in cart Hierarchy */
    cartHierarchy *carth;   /* carth[i] is the Hierarchy for level i */
} cart_t;

static int cartKeyval = MPI_KEYVAL_INVALID;
static int cvar_cart_verbose = 0;
static FILE *vfp=0;

static void rankShift(int ndims, const int dims[], const int coords[],
		      const int periodic[], int order, int rank,
		      int direction, int disp, int *rsource, int *rdest);

/*@
 MPIX_Nodecart_shift - Returns the shifted source and destination
                 ranks, given a shift direction and amount

Input Parameters:
+ comm - communicator with Cartesian structure (handle)
. direction - coordinate dimension of shift (integer)
- disp - displacement (> 0: upwards shift, < 0: downwards shift) (integer)

Output Parameters:
+ rank_source - rank of source process (integer)
- rank_dest - rank of destination process (integer)

Notes:
The 'direction' argument is in the range '[0,n-1]' for an n-dimensional
Cartesian mesh.
@*/
int MPIX_Nodecart_shift(MPI_Comm comm, int direction, int disp,
			int *rank_sources, int *rank_dest)
{
    int    rank, flag;
    cart_t *cinfo;

    MPI_Comm_rank(comm, &rank);

    MPI_Comm_get_attr(comm, cartKeyval, &cinfo, &flag);
    if (!cinfo || !flag) {
	BENVi_ErrAttr("cartKeyval");
    }

    rankShift(cinfo->ndim, cinfo->dims, cinfo->coords, cinfo->periodic,
	      MPI_ORDER_C, rank, direction, disp, rank_sources, rank_dest);

    if (cvar_cart_verbose > 1) {
	if (!vfp) vfp = stdout;
	fprintf(vfp, "coords[%d] = %d, dims = %d, low = %d high = %d\n",
	       direction, cinfo->coords[direction], cinfo->dims[direction],
		*rank_sources, *rank_dest);
	fflush(vfp);
    }

    return MPI_SUCCESS;
}

/*@
MPIX_Nodecart_coords - Determines process coords in Cartesian topology given
                  rank in group

Input Parameters:
+ comm - communicator with Cartesian structure (handle)
. rank - rank of a process within group of 'comm' (integer)
- maxdims - length of vector 'coords' in the calling program (integer)

Output Parameters:
. coords - integer array (of size 'ndims') containing the Cartesian
  coordinates of specified process (integer)
  @*/
int MPIX_Nodecart_coords(MPI_Comm comm, int rank, int maxdims, int coords[])
{
    int        flag;
    cart_t *cinfo;

    MPI_Comm_get_attr(comm, cartKeyval, &cinfo, &flag);
    if (!cinfo || !flag) {
	BENVi_ErrAttr("cartKeyval");
    }

    BENVi_RankToCoords(cinfo->ndim, cinfo->dims, rank, BENV_ORDER_C, coords);

    return MPI_SUCCESS;
}

/*@
MPIX_Nodecart_rank - Determines process rank in communicator given Cartesian
                location

Input Parameters:
+ comm - communicator with Cartesian structure (handle)
- coords - integer array (of size 'ndims', the number of dimensions of
    the Cartesian topology associated with 'comm') specifying the Cartesian
  coordinates of a process

Output Parameters:
. rank - rank of specified process (integer)

Notes:
 Out-of-range coordinates are erroneous for non-periodic dimensions.
 @*/
int MPIX_Nodecart_rank(MPI_Comm comm, const int coords[], int *rank)
{
    int        flag;
    cart_t *cinfo;

    MPI_Comm_get_attr(comm, cartKeyval, &cinfo, &flag);
    if (!cinfo || !flag) {
	BENVi_ErrAttr("cartKeyval");
    }

    BENVi_CoordsToRank(cinfo->ndim, cinfo->dims, coords, BENV_ORDER_C, rank);

    return MPI_SUCCESS;
}

/* Given a mapping of processes to ranks, create a Cartesian topology.
   This is an internal implementation routine for everything that is common
   to that communicator creation
*/

/*@
MPIX_Nodecart_sub - Partitions a communicator into subgroups which
               form lower-dimensional Cartesian subgrids

Input Parameters:
+ comm - communicator with Cartesian structure (handle)
- remain_dims - the  'i'th entry of remain_dims specifies whether the 'i'th
dimension is kept in the subgrid (true) or is dropped (false) (logical
vector)

Output Parameters:
. newcomm - communicator containing the subgrid that includes the calling
process (handle)

Note:
 Needed for the snap CORAL benchmark - they use this instead of
   'MPI_Cart_shift' . Note that processes are not reordered, and thus
   retain the order from the parent communicator 'comm'.
@*/
int MPIX_Nodecart_sub(MPI_Comm comm, const int remain[], MPI_Comm *subcomm)
{
    int        i, flag;
    int        newdims, color, k;
    int        inrank;
    cart_t     *cinfo;
    cartHierarchy *carth;
    int        *ndims, *ncoords, *nperiodic;

    /* Algorithm: Using the coordinates where remain == FALSE, compute a
       rank using the coords to rank.  Perform comm_split using that
       rank as the color, and the original rank as rank.
       Create the nodeinfo data using the remaining dimensions from the
       input

       Must update nodecomm, since it is likely to
       be changed by the removal of dimensions.

       Note that we can't simply create a communicator with the remaining
       dimensions and then call Nodecart_create on that because we must
       retain the original mapping of all processes
    */

    MPI_Comm_get_attr(comm, cartKeyval, &cinfo, &flag);
    if (!cinfo || !flag) {
	BENVi_ErrAttr("cartKeyval");
    }

    MPI_Comm_rank(comm, &inrank);
    /* How many remaining dimensions are there? Also compute the color on
       which to split */
    newdims  = 0;
    color    = 0;
    for (i=0; i<cinfo->ndim; i++) {
	if (remain[i]) {
	    newdims++;
	}
	else {
	    /* Row major for simplicity */
	    color = color * cinfo->dims[i] + cinfo->coords[i];
	}
    }

    /* Create the subcommunicator */
    MPI_Comm_rank(comm, &inrank);
    MPI_Comm_split(comm, color, inrank, subcomm);

    /* Create the process topology attribute, including the node-related
       information */
    /* Create the information for the topology and node information */
    carth     = (cartHierarchy *)malloc(sizeof(cartHierarchy));
    carth->cl = (cartLevel *)malloc(cinfo->carth->nlevels * sizeof(cartLevel));
    ndims     = (int *)malloc(newdims * 3 * sizeof(int));
    ncoords   = ndims + newdims;
    nperiodic = ncoords + newdims;
    carth->nlevels = cinfo->carth->nlevels;
    carth->ndims = newdims;

    //MPI_Comm_rank(cinfo->nodecomm, &noderank);
    //MPI_Comm_split(cinfo->nodecomm, color, noderank, &cinfonew->nodecomm);

    /* Create the new cart information */
    /* This the part that is different from the older code in nc2 */
    k = 0;
    for (i=0; i<cinfo->ndim; i++) {
	if (remain[i]) {
	    int nl;
	    ndims[k]        = cinfo->dims[i];
	    ncoords[k]      = cinfo->coords[i];
	    nperiodic[k]    = cinfo->periodic[i];
	    for (nl=0; nl<cinfo->carth->nlevels; nl++) {
		carth->cl[nl].dims[k] = cinfo->carth->cl[nl].dims[i];
		carth->cl[nl].coords[k] = cinfo->carth->cl[nl].coords[i];
	    }
	    k++;
	}
    }
//    MPI_Comm_rank(cinfonew->nodecomm, &nrank);
    MPIXI_NodecartSetTopoInfo(*subcomm, newdims, ndims, ncoords, nperiodic,
			      carth);
    free(ndims);

    return MPI_SUCCESS;
}

/*@
MPIX_Nodecartdim_get - Retrieves Cartesian topology information
                  associated with a communicator

Input Parameters:
. comm - communicator with Cartesian structure (handle)

Output Parameters:
. ndims - number of dimensions of the Cartesian structure (integer)
@*/
int MPIX_Nodecart_dim_get(MPI_Comm comm, int *ndims)
{
    int        flag;
    cart_t *cinfo;

    MPI_Comm_get_attr(comm, cartKeyval, &cinfo, &flag);
    if (!cinfo || !flag) {
	BENVi_ErrAttr("cartKeyval");
    }
    *ndims = cinfo->ndim;
    return MPI_SUCCESS;
}

/*@
MPIX_Nodecart_get - Retrieves Cartesian topology information associated with a
               communicator

Input Parameters:
+ comm - communicator with Cartesian structure (handle)
- maxdims - length of vectors  'dims', 'periods', and 'coords'
in the calling program (integer)

Output Parameters:
+ dims - number of processes for each Cartesian dimension (array of integer)
. periods - periodicity (true/false) for each Cartesian dimension
(array of logical)
- coords - coordinates of calling process in Cartesian structure
(array of integer)
@*/
int MPIX_Nodecart_get(MPI_Comm comm, int maxdims, int dims[], int periods[],
		      int coords[])
{
    int        flag, i;
    cart_t *cinfo;

    MPI_Comm_get_attr(comm, cartKeyval, &cinfo, &flag);
    if (!cinfo || !flag) {
	BENVi_ErrAttr("cartKeyval");
    }
    /* The standard isn't clear what happens if maxdims < cinfo->ndim.
       We abort with an error message */
    if (maxdims < cinfo->ndim) {
	fprintf(stderr, "Nodecart_get: maxdims < ndims for comm\n");
	fflush(stderr);
	MPI_Abort(MPI_COMM_WORLD, 1);
    }
    for (i=0; i<cinfo->ndim; i++) {
	dims[i]    = cinfo->dims[i];
	periods[i] = cinfo->periodic[i];
	coords[i]  = cinfo->coords[i];
    }
    return MPI_SUCCESS;
}


/* Interface routines */
static int cartDelFn(MPI_Comm comm, int keyval, void *attr, void *estate);

/* Set the topology information on a cartesian communicator.
   carth (the pointer) will be copied to the topo info. The memory to which it
   points must not be freed except by the routine that frees this info (i.e.,
   the attribute free routine).
 */
int MPIXI_NodecartSetTopoInfo(MPI_Comm comm, int ndim,
			      const int dims[], const int coords[],
			      const int periodic[],
			      cartHierarchy *carth)
{
    cart_t *cinfo;
    int i;

    cinfo = (cart_t *)malloc(sizeof(cart_t));
    if (!cinfo) {
	fprintf(stderr, "Unable to allocate memory for cart_t\n");
	fflush(stderr);
	MPI_Abort(MPI_COMM_WORLD, 1);
    }

    cinfo->ndim  = ndim;
    cinfo->carth = carth;
    for (i=0; i<cinfo->ndim; i++) {
        cinfo->dims[i]     = dims[i];
	cinfo->coords[i]   = coords[i];
	cinfo->periodic[i] = periodic[i];
    }
    if (cartKeyval == MPI_KEYVAL_INVALID) {
	MPI_Comm_create_keyval(MPI_COMM_NULL_COPY_FN, cartDelFn,
			       &cartKeyval, NULL);
    }
    MPI_Comm_set_attr(comm, cartKeyval, cinfo);

    return MPI_SUCCESS;
}

/* Attribute functions */
static int cartDelFn(MPI_Comm comm, int keyval, void *attr, void *estate)
{
    cart_t *cinfo = (cart_t *)attr;

    if (!cinfo) return MPI_ERR_OTHER;
    free(cinfo);

    return 0;
}

/* Utility routines */

/* Given a value val and an offset and high, return val such that
   offset+val >= 0 and offset+val<high, with the returned value "val"
   computed by adding or subtracting integral numbers of "high" */
static int findInInterval(int val, int offset, int high)
{
    while (offset + val < 0)
	val += high;
    while (offset + val >= high)
	val -= high;
    return val;
}

/* Given the description of process topology (ndims, dims, periodic), the
   location of the process in that topology (coords), the rank of the calling
   process (rank), which dimension is being shifted (direction) and the
   amount of the shift (disp), return the rank of the processes in the
   direction with shift disp (rdest) and shift -disp (rsource) */
static void rankShift(int ndims, const int dims[], const int coords[],
		      const int periodic[], int order, int rank,
		      int direction, int disp, int *rsource, int *rdest)
{
    int rfrom, rto;
    int offset, i;

    offset = 1;
    if (order == MPI_ORDER_C) {
	for (i=direction+1; i<ndims; i++) offset *= dims[i];
    }
    else {
	/* MPI_ORDER_FORTRAN */
	for (i=0; i<direction; i--) offset *= dims[i];
    }


    rfrom = -disp;
    rto   = disp;
    if (periodic[direction]) {
	/* Allow disp to be negative, so must make both in the range
	   [0,dims[direction]-1] */
	rfrom = findInInterval(rfrom, coords[direction], dims[direction]);
	rto   = findInInterval(rto,   coords[direction], dims[direction]);
	rfrom = rank + rfrom * offset;
	rto   = rank + rto * offset;
    }
    else {
	if (rfrom + coords[direction] < 0 ||
	    rfrom + coords[direction] >= dims[direction])
	    rfrom =  MPI_PROC_NULL;
	else
	    rfrom = rank + rfrom * offset;
	if (rto + coords[direction] < 0 ||
	    rto + coords[direction] >= dims[direction])
	    rto = MPI_PROC_NULL;
	else
	    rto = rank + rto * offset;
    }
    *rsource = rfrom;
    *rdest   = rto;
}
