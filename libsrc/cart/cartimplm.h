#ifndef CARTIMPLM_H_INCLUDED
#define CARTIMPLM_H_INCLUDED 1

/* Internal routines that require MPI */
#include "cartimpl.h"

/* Support routines */
int MPIXI_NodecartSetTopoInfo(MPI_Comm comm, int ndim,
			      const int dims[], const int coords[],
			      const int periodic[],
			      cartHierarchy *carth);


#endif
