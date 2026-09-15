/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 * Copyright (C) by University of Illinois 2024
 */
#include "benvconf.h"
#include <stdio.h>
#include <stdlib.h>

#include "mpi.h"
#include "cartimpl.h"
#include "meshsup.h"
#include "subarrays.h"

/* Was DistributeArray */
/*@
  BENV_DarrayDecomposition - Distribute an array across processors

Input Parameters:
+ arraylen - Number of elements in the array
. nproc    - Number of processes to distribute across
. idxproc  - Index of this process, in the range of 0 to nproc-1
- distribkind - How the array is distributed. See below

Output Parameters:
+ arrayidx - Index (from 0) of the first element in the array for this
process
- larraylen - Number of elements in this local array. See below.

Notes:

distribkind values are
+ 0 - Distribute equally; each process receives either arraylen/nproc
or 1 + arraylen/nproc elements
- 1 - HDF distribution; each process receives
(arraylen+nproc-1)/nproc, except the last, which gets whatever is left
over (guaranteed to be no larger than (arraylen+nproc-1)/nproc).

This performs a one-dimensional distribution. For multi-dimensional arrays,
apply this to each dimension.

  @*/
int BENV_DarrayDecomposition(int arraylen, int nproc, int idxproc,
			     int distribkind, int *arrayidx, int *larraylen)
{
    int nbase, addone, idx;

    if (distribkind == 0) {
	/* Evenly distribute accross processes */
	nbase  = arraylen / nproc;
	addone = arraylen - nproc * nbase;

	idx = idxproc * nbase;
	if (idxproc < addone) {
	    idx += idxproc;
	    nbase++;
	}
	else {
	    idx += addone;
	}
    }
    else {
	/* Simplify index calculation, with last process having
	 * potentially much less work. This corresponds to the HDF
	 * Block distribution. For bulk synchronous applications, the
	 * overall computation time is the same, with the idle cycles
	 * concentrated in the last process rather than distributed
	 * across many processes (assuming nproc doesn't exactly
	 * divide arraylen)
	 */
	nbase  = (arraylen + nproc - 1) / nproc;
	addone = arraylen - (nproc-1)*nbase;
	idx    = idxproc * nbase;
	if (idxproc == nproc - 1) {
	    nbase = addone;
	}
    }

    *arrayidx  = idx;
    *larraylen = nbase;

    return 0;
}

/*
 * Input/Output of Cartesian meshes
 *
 * Define the local section as (globaloffsets from 0) (sizes)
 * (localoffsets) (local sizes). This allows halos of different
 * widths, including the case of the physical boundary that may have
 * no halo.
 *
 * global offsets and sizes are for the part "owned" by the calling
 * process.
 * local offsets and local sizes are for the corresponding part of the
 * local array. e.g., if there are no halo cells, local offsets are
 * all zero and local sizes are the same ad the (global) sizes.
 *
 * Also need some information about process decomposition and mesh
 * total size. Knowing the decomposition strategy or other way to
 * determine parameters for the neighboring processes is necessary for
 * using RMA.
 *
 * Need an option to use Darray.
 * Need a test program, with a serial program to write out data in
 * cannonical order, and a test to make sure correct values read.
 * This can be both compare to values from (index tuple -> linear
 * array index) and compare the tuples directly (store tuple in file).
 */

/* Create fileview type for a global mesh */
/*@
  BENV_DarrayFileType - Create a filetype suitable for reading or writing
  a distributed mesh

 Input Parameters:
+ comm - Communicator for processes for the mesh.
. useDarray - If true, use 'MPI_Type_create_darray' to form the filetype
. ndims - Number of dimensions for the mesh
. sizes - Size of the full (global) mesh
. psizes - Number of processes in each dimension
. larraylen - Size of the local mesh (without any halo) on this process in
 each dimension
. gindex - Index (coordinates) of the upper left corner of the local mesh on this process with respect to the global mesh
- basetype - The base MPI datatype of an element of the mesh, e.g., 'MPI_INT' or 'MPI_DOUBLE'

 Output Parameters:
+ filetypeview - MPI datatype for the part of the global mesh belonging to
 this process.
- disp - Displacement from beginning of global array in file to be used with
 filetypeview. May be 0 if the displacement is included within the type (as it
 is when 'useDarray' is true)

  @*/
int BENV_DarrayFileType(MPI_Comm comm, int useDarray,
			int ndims, const int sizes[], const int psizes[],
			const int larraylen[], const int gindex[],
			MPI_Datatype basetype, MPI_Datatype *fileviewtype,
			MPI_Offset *disp)
{
    if (useDarray) {
	int distribs[MAX_DIMS], dargs[MAX_DIMS];
	int csize, crank, i;

	MPI_Comm_size(comm, &csize);
	MPI_Comm_rank(comm, &crank);
	for (i=0; i<ndims; i++) {
	    distribs[i] = MPI_DISTRIBUTE_BLOCK;
	    dargs[i]    = MPI_DISTRIBUTE_DFLT_DARG;
	}
	/* Assumes BENV_DistributeArray used for local mesh sizes */
	MPI_Type_create_darray(csize, crank, ndims, sizes,
			       distribs, dargs, psizes, MPI_ORDER_C,
			       basetype, fileviewtype);
	*disp = 0;
    }
    else {
	int bsize;
	if (ndims > 1) {
	    MPI_Datatype vtype;
	    MPI_Type_vector(larraylen[ndims-2], larraylen[ndims-1],
			    sizes[ndims-1], basetype, &vtype);
	    *disp = gindex[0] * sizes[1] + gindex[1];
	    if (ndims > 2) {
		MPI_Datatype vtype2, vtype3;
		/* Add the 3rd dimension */
		MPI_Type_create_resized(vtype, 0, sizeof(int), &vtype2);
		/* larrylen[0] planes, separated by sizes[2]*sizes[1] ints */
		MPI_Type_vector(larraylen[ndims-3], 1,
				sizes[ndims-2]*sizes[ndims-1], vtype2, &vtype3);
		*fileviewtype = vtype3;
		MPI_Type_free(&vtype);
		MPI_Type_free(&vtype2);
		*disp = (*disp * sizes[2]) + gindex[2];
	    }
	    else {
		*fileviewtype = vtype;
	    }
	}
	else {
	    MPI_Type_contiguous(larraylen[0], basetype, fileviewtype);
	    *disp = gindex[0];
	}
	MPI_Type_size(basetype, &bsize);
	*disp = *disp * bsize;
    }
    MPI_Type_commit(fileviewtype);

    return MPI_SUCCESS;
}

/*@
  BENV_MeshHaloDatatypes - Create MPI datatypes for halo regions in a Cartesian mesh

Input Parameters:
+ ndims - Number of dimensions of mesh
. whichdim - which dimension of the mesh (see below)
. shift - Partner process is at 'pcoords[whichdim]+shift'. If the mesh is
 nonperiodic, this value must be in the range [0,psizes[whichdim]). If not,
 MPI_DATATYPE_NULL is returned in 'halotype'.
. useSubarray - if true, use 'MPI_Type_create_subarray'; otherwise, use
 'BENV_Create_Subarray'
. basetype - Base datatype (e.g., 'MPI_DOUBLE')
. pcoords - ?? remove??
. psizes - ?? also remove
. halowidth - width of the halo
. larraylen - size of local mesh in each dimension
- larraylenhalo - larraylenhalo[i] = larraylen[i] + 2*halowidth ?? why have both this and halowidth???

Output Parameters:
+ halotyperecv - 
. recvoffset - 
. halotypesend -
- sendoffset -

Notes:

  @*/
int BENV_MeshHaloDatatypes(int ndims, int whichdim, int shift, int useSubarray,
			   MPI_Datatype basetype,
			   const int pcoords[], const int psizes[],
			   int halowidth, const int larraylen[],
			   const int larraylenhalo[],
			   MPI_Datatype *halotyperecv, MPI_Aint *recvoffset,
			   MPI_Datatype *halotypesend, MPI_Aint *sendoffset)
{
    int boxsize[MAX_DIMS], boxstart[MAX_DIMS];
    int i;

    /* Set defaults */
    *halotyperecv = MPI_DATATYPE_NULL;
    *halotypesend = MPI_DATATYPE_NULL;
    *recvoffset   = 0;
    *sendoffset   = 0;

    /* distribution and ordering follows process coords.
       In the descriptions below, halowidth is h. The number of mesh
       elements is n0,n1,n2 (larraylen[0] etc.), and the declared array
       size is 2h+ni (e.g., 2h+n0, 2h+n1, and 2h+n2).
    */

    if (pcoords[whichdim] + shift >= 0 &&
	pcoords[whichdim] + shift < psizes[whichdim]) {
	/* whichdim | shift | Halo box
                  0      -1   (0,h,h) to (h,h+n1-1,h+n2-1),
		              e.g., a box with sizes (h,n1,n2) at origin
			      (0,h,h)
		  0       1   (h+n0,h,h) to (h+n0+h,h+n1-1,h+n2-1),
		              e.g., a box with sizes (h,n1,n2) at origin
			      (h+n0,h,h)
           and similarly for whichdim of 1, 2, ..., ndims-1
	*/
	for (i=0; i<ndims; i++) {
	    boxsize[i] = larraylen[i];
	    boxstart[i] = halowidth;
	}
	boxsize[whichdim]  = halowidth;
	if (shift == -1)
	    boxstart[whichdim] = 0;
	else
	    boxstart[whichdim] = halowidth + larraylen[whichdim];

	if (useSubarray) {
	    MPI_Type_create_subarray(ndims, larraylenhalo,
				     boxsize, boxstart,
				     MPI_ORDER_C, basetype, halotyperecv);
	    *recvoffset = 0;
	}
	else {
	    BENV_CreateSubarray(ndims, larraylenhalo, boxsize, boxstart,
				MPI_ORDER_C, basetype, halotyperecv,
				recvoffset);
	}

	MPI_Type_commit(halotyperecv);
//	halosize[0]   = prod(ndims, boxsize);

	/* The data to send to the same partner is the same size, just
	   starting at (h,h,h) (shift==-1) or (h+n0-h,h,h) (shift==1,
	   whichdim==0) */
	if (shift == -1)
	    boxstart[whichdim] = halowidth;
	else
	    boxstart[whichdim] = halowidth + larraylen[whichdim] - halowidth;
	if (useSubarray) {
	    MPI_Type_create_subarray(ndims, larraylenhalo,
				     boxsize, boxstart,
				     MPI_ORDER_C, basetype, halotypesend);
	    *sendoffset = 0;
	}
	else {
	    BENV_CreateSubarray(ndims, larraylenhalo, boxsize, boxstart,
				MPI_ORDER_C, basetype, halotypesend,
				sendoffset);
	}
	MPI_Type_commit(halotypesend);
    }
    return 0;
}
#if 0
/* Partner ranks */
    {
	for (i=0; i<ndims; i++) ncoords[i] = pcoords[i];
	ncoords[whichdim] = ncoords[whichdim]+shift;
	MPI_Cart_rank(cartcomm, ncoords, &partnerrank[0]);
    }
    else {
	partnerrank[0] = MPI_PROC_NULL;
    }
#endif
