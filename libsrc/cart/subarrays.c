/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 * Copyright (C) by University of Illinois 2024
 */

#include <stdio.h>
#include <stdlib.h>
#include "mpi.h"
#include "benvdbg.h"
#include "subarrays.h"

CDBGFCALLDECL;
static int debugSubarray = 0;

/*@ BENV_CreateSubarray - Generalized MPI_Type_create_subarray

 Input Parameters:
+ ndims - Number of dimensions of array
. gsize - Dimensions of the full array across all processes (i.e., the
 GlobalSIZE)
. lsize - Dimension of the array on this process (i.e., the LocalSIZE)
. lstart - Coordinates of the corner of the part of the array on this
 process. This is the corner with the lowest coordinate values.
. order - Either 'MPI_ORDER_C' or 'MPI_ORDER_FORTRAN'
- oldtype - MPI datatype for an element of the array. Typically a basic
 type such as 'MPI_DOUBLE'

 Output Parameters:
+ newtype - MPI datatype representing the local part of the global array on
 the calling process.
- offset - Offset of the start of the global array to the first element
 of the local array, relative to 'newtype'. Note that both the 'newtype' and
 'offset' must be used to specify the memory locations in the global array
 that correspond to the local array on the process.

Notes:

This is effectively a re-implementation of 'MPI_Type_create_subarray',
but with a few differences\:
.n 1. The initial offset is separate, not baked into the datatype. As such,
   it has the same parameters as 'MPI_Type_create_subarray' but with one
   additional parameter, 'offset'.
.n 2. It attempts to optimize for important special cases (which subarray
   should do - but may not)
.n 3. Only 'MPI_ORDER_C' is implemented at this time
.n 4. Recall that 'MPI_Type_create_subarray' was defined to support the creation
   of filetypes for distributed arrays. The description above mentions
   processes and talks about the mesh as distributed across processes, but
   this is not required and there is no explicit dependency on the process
   decomposition (other than how the subarray, given by 'lsize' and 'lstart',
   is defined).
@*/
int BENV_CreateSubarray(int ndims, const int gsize[], const int lsize[],
			const int lstart[], int order, MPI_Datatype oldtype,
			MPI_Datatype *newtype, MPI_Aint *offset)
{
    MPI_Aint off;
    int i, sizeoldtype;
    MPI_Datatype vtype2, vtype3;

    CDBGFCALLENTER;
    if (ndims <= 0) {
	*newtype = MPI_DATATYPE_NULL;
	*offset  = 0;
	CDBGFCALLEXIT;
	return MPI_ERR_OTHER;
    }
    if (ndims >= 4) {
	/* Error - unimplemented */
	fprintf(stderr, "Only ndims <= 3 implemented!\n");
	*newtype = MPI_DATATYPE_NULL;
	*offset  = 0;
	CDBGFCALLEXIT;
	return MPI_ERR_OTHER;
    }

    if (order == MPI_ORDER_C) {
	off = lstart[0];
	for (i=1; i<ndims; i++) {
	    off = off * gsize[i] + lstart[i];
	}
	if (ndims < 2) {
	    if (debugSubarray) {
		fprintf(stdout, "Creating contiguous (count=%d)for subarray\n",
		    lsize[0]);
	    }
	    MPI_Type_contiguous(lsize[0], oldtype, newtype);
	}
	else {
	    /* build type starting with contiguous pieces */
	    if (debugSubarray) {
		fprintf(stdout, "Created type for 2 or 3d subarray\n");
	    }
	    if (lsize[ndims-2] == 1) {
		if (debugSubarray) {
		    fprintf(stdout, "\tcontig (%d)\n", lsize[ndims-1]);
		}
		MPI_Type_contiguous(lsize[ndims-1], oldtype, &vtype3);
	    }
	    else {
		/* if lsize[n-1] == gsize[n-1], then also contig */
		if (debugSubarray) {
		    fprintf(stdout, "\tvector(%d,%d,%d)\n", lsize[ndims-2],
			    lsize[ndims-1], gsize[ndims-1]);
		}
		MPI_Type_vector(lsize[ndims-2], lsize[ndims-1], gsize[ndims-1],
				oldtype, &vtype3);
	    }
	    if (ndims > 2) {
		if (lsize[ndims-3] > 1) {
		    if (debugSubarray) {
			fprintf(stdout, "\tresize;vector(%d,%d,%d)\n",
				lsize[ndims-3], 1,
				gsize[ndims-2]*gsize[ndims-1]);
		    }
		    MPI_Type_size(oldtype, &sizeoldtype);
		    MPI_Type_create_resized(vtype3, 0, sizeoldtype, &vtype2);
		    MPI_Type_free(&vtype3);
		    MPI_Type_vector(lsize[ndims-3], 1,
				    gsize[ndims-2]*gsize[ndims-1], vtype2,
				    &vtype3);
		    MPI_Type_free(&vtype2);
		}
		else {
		    /* Nothing to do, so just keep */
		    ;
		}
	    }
	    *newtype = vtype3;
	}
	*offset = off;
    }
    else {
	/* Not yet implemented */
	*newtype = MPI_DATATYPE_NULL;
	*offset  = 0;
    }
    CDBGFCALLEXIT;
    return MPI_SUCCESS;
}
