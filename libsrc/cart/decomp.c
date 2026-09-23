/* -*- Mode: C; c-basic-offset:4; -*- */
/*
 * Copyright (C) by University of Illinois 2025
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mpi.h"
#include "benvutil.h"
#include "benvdbg.h"
#include "cartimpl.h"
#include "decomp.h"

CDBGFCALLDECL;

/* Forward refs */
static int BENVi_CartDecompFreeComm(cartdecompCtx *ctx);

/* This file contains several routines for defining a regular decomposition
   of processes for a Cartesian grid. The routines here provide a common
   interface, allowing programs to explore different strategies for
   defining the process mapping (virtual topology). These include
   1. MPI Cartesian process topology
   2. Simple "manual" decomp
   3. Node-aware version of Cartesian process topology
*/

/*@ BENV_CartDecompCreate - Create a Cartesian virtual process topology

Input Parameters:
+ ndims - Number of dimensions in the Cartesian process topology
. psizes - Number of processes in each dimension
. periods - Boolean that indicates if the mesh is periodic in each dimension
. comm - Communicator of processes over which to create a Cartesian process
 topology
- flavor - Indicates how the Cartesian virtual process topology is created.
 See notes below.

Output Parameter:
. ctx - A pointer to a structure that holds information about the created
 virtual process topology. This structure is defined in 'include/decomp.h'

Notes:
This is a convenience routine that allows selecting among different
implementations of a Cartesian virtual process topology. The choice is
deterimined first by the value of the environment variable
'BENV_CARTDECOMP_FLAVOR', then by the value of 'flavor'. Valid choices
for 'BENV_CARTDECOMP_FLAVOR' include 'MPI' (use 'MPI_Cart_create') and
'Simple' (use cannonical C-order based on 'psizes'). The corresponding
values of 'flavor' are 1 for 'MPI' and 2 for 'Simple'.

 In the longer term, we expect to add a node-aware implementation of
 creating a Cartesian process topology.

See also:
BENV_CartDecompFree
@*/
int BENV_CartDecompCreate(int ndims, const int *psizes, const int *periods,
			  MPI_Comm comm, int flavor, cartdecompCtx **ctx)
{
    int rc;
    const char *envstr;

    CDBGFCALLENTER;
    /* How to decide???? Options include:
       local or global flag, env variable BENV_CARTDECOMP_FLAVOR, cvar */
    envstr = getenv("BENV_CARTDECOMP_FLAVOR");
    if (envstr) {
	/* FIXME: Provide cvar interface */
	if (strcmp(envstr,"MPI") == 0) flavor = 1;
	else if (strcmp(envstr, "Simple") == 0) flavor = 2;
	else {
	    CDBGFCALLEXIT;
	    return MPI_ERR_OTHER; /* Unknown flavor */ /* FIXME: Add error message */
	}
    }
    if (flavor == 1) {
	rc = BENV_CartDecompCreateMPI(ndims, psizes, periods, comm, ctx);
    }
    else if (flavor == 2) {
	rc = BENV_CartDecompCreateSimple(ndims, psizes, periods, comm, ctx);
    }
    else {
	rc = MPI_ERR_OTHER;
    }

    CDBGFCALLEXIT;
    return rc;
}

/* Convenience routines */

/*@ BENV_CartDecompFree - Free a Cartesian process decomposition structure

Input Parameter:
. ctx - Pointer to 'cartdecompCtx' created with 'BENV_CartDecompCreate'

See also:
BENV_CartDecompCreate
@*/
int BENV_CartDecompFree(cartdecompCtx *ctx)
{
    CDBGFCALLENTER;
    if (ctx->free) (*ctx->free)(ctx);
    CDBGFCALLEXIT;
    return MPI_SUCCESS;
}

/*@ BENV_CartDecompGetShift - Get rank of neighbor in a Cartesian process
  topology

Input Parameters:
+ ctx - A 'cartdecompCtx' context created with 'BENV_CartdecompCreate'
. d - Shift in this dimension
- shift - Amount of shift; may be poistive or negative

Output Parameter:
. nrank - Rank of process shifted by 'shift' in dimension 'd'

See also:
BENV_CartDecompCreate
  @*/
int BENV_CartDecompGetShift(cartdecompCtx *ctx, int d, int shift, int *nrank)
{
    int ncoords[MAX_DIMS];
    int i;

    CDBGFCALLENTER;
    for (i=0; i<ctx->ndims; i++) {
	ncoords[i] = ctx->pcoords[i];
    }
    ncoords[d] += shift;
    /* If non-periodic, out-of-range should be PROC_NULL */
    if (ctx->periods[d] == 0) {
	if (ncoords[d] < 0 || ncoords[d] >= ctx->psizes[d]) {
	    *nrank = MPI_PROC_NULL;
	    CDBGFCALLEXIT;
	    return MPI_SUCCESS;
	}
    }
    (ctx->coordsToRank)(ctx, ncoords, nrank);
    CDBGFCALLEXIT;
    return MPI_SUCCESS;
}

/* Individual methods */

int BENV_CartDecompCreateMPI(int ndims, const int *psizes, const int *periods,
			     MPI_Comm comm, cartdecompCtx **ctx)
{
    int wsize, crank, i;
    cartdecompCtx *nctx;

    CDBGFCALLENTER;
    nctx = (cartdecompCtx *)malloc(sizeof(cartdecompCtx));
    if (!nctx) BENVi_MallocErr("cartdecompCtx", 1, "CartDecompCreateMPI");

    nctx->ndims = ndims;

    /* At this point, an option is to use alternate implementations of
       the process topology functions, since many MPI implementations
       do not provide good implementations of these */
    MPI_Comm_size(comm, &wsize);
    for (i=0; i<ndims; i++) {
	nctx->psizes[i] = psizes[i];
	nctx->periods[i] = periods[i];
    }
    MPI_Dims_create(wsize, ndims, nctx->psizes);
    MPI_Cart_create(MPI_COMM_WORLD, ndims, nctx->psizes, nctx->periods, 1,
		    &nctx->cartcomm);
    MPI_Comm_rank(nctx->cartcomm, &crank);
    MPI_Cart_coords(nctx->cartcomm, crank, ndims, nctx->pcoords);
    nctx->free = BENVi_CartDecompFreeComm;
    nctx->desc = (const char *)strdup("MPI_Cart_create");
    *ctx = nctx;

    CDBGFCALLEXIT;
    return MPI_SUCCESS;
}

int BENV_CartDecompCreateSimple(int ndims, const int *psizes,
				const int *periods, MPI_Comm comm,
				cartdecompCtx **ctx)
{
    int wsize, r, i;
    cartdecompCtx *nctx;

    CDBGFCALLENTER;
    nctx = (cartdecompCtx *)malloc(sizeof(cartdecompCtx));
    if (!nctx) BENVi_MallocErr("cartdecompCtx", 1, "CartDecompCreateSimple");

    nctx->ndims = ndims;

    /* At this point, an option is to use alternate implementations of
       the process topology functions, since many MPI implementations
       do not provide good implementations of these */
    MPI_Comm_size(comm, &wsize);
    for (i=0; i<ndims; i++) {
	nctx->psizes[i] = psizes[i];
	nctx->periods[i] = periods[i];
    }
    MPI_Dims_create(wsize, ndims, nctx->psizes);
    nctx->cartcomm = comm; /* Could be dup of comm, but sometimes COMM_WORLD is
			      handled specially, so may want to stick with the
			      same comm */
    MPI_Comm_rank(comm, &r);
    BENVi_RankToCoords(ndims, nctx->psizes, r, BENV_ORDER_C, nctx->pcoords);
    nctx->free = 0;
    nctx->desc = (const char *)strdup("C order mesh");

    *ctx = nctx;

    CDBGFCALLEXIT;
    return MPI_SUCCESS;
}

/* Internal routines */
static int BENVi_CartDecompFreeComm(cartdecompCtx *ctx)
{
    MPI_Comm_free(&ctx->cartcomm);
    return MPI_SUCCESS;
}

