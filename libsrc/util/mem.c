/*
 * Copyright (C) by University of Illinois 2025
 */

#include "benvconf.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#define NO_MPI_INCLUDE
#include "benvutil.h"
#include "arrindex.h"
#include "benvmem.h"
#include "memimpl.h"
#include "benvdbg.h"

#if defined(HAVE_CUDA) || defined(HAVE_HIP)
CDBGDECL(DEVMEM);
#endif

/*
 * Memory management
 *
 * These routines provide an abstraction for managing memory that might be
 * resident on another device, e.g., a GPU.
 *
 * The interface allows the same build of a code to use both "host" and
 * "device" memory.
 *
 * To support confirming that data is allocated and properly manipulated,
 * this code also supports the definition of a regular n-d mesh of values,
 * including subarrays, where the values (with and without a halo region)
 * can be initialized with a different value at each mesh point.
 *
 * To aid in code testing, routines to initialize and check values in
 * a 1 to 3-D mesh are included. Tests can be executed on the device
 * or in the host. A test code (see tests/memcudatest.cu) confirms that
 * errors are reported.
 */

/*@ BENV_MemDebug - Set debugging for the memory object routines

Input Parameters:
+ v - Set to 0 to turn off debugging. Values greater than 0 can indicate
 increasing levels of debugging, though at this time anything greater than
 zero is treated the same
- wrank - The rank in 'MPI_COMM_WORLD' of the process to issue output.
  @*/
void BENV_MemDebug(int v, int wrank)
{
#if defined(HAVE_CUDA) || defined(HAVE_HIP)
    CDBGSETVAL(DEVMEM,v);
    cvar_benv_rank = wrank;
#endif
}

static void mallocmemcpy(void *restrict dest, const void *restrict src, size_t n)
{
    /* Discard return value of memcpy */
    memcpy(dest, src, n);
}

/*@ BENV_MemAlloc - Allocate memory for use with different devices

Input Parameters:
+ n - Number of bytes to allocate
- t - Type of memory to allocate; e.g., normal ('MEMOBJ_MALLOC'), CUDA
 ('MEMOBJ_CUDA' for NVIDIA GPUs, on the current device), HIP
 ('MEMOBJ_HIP' for AMD GPUs, on the current device), or SYCL ('MEMOBJ_SYCL'
 for Intel GPUs, on the current device). Only available types are supported,
 selected by '--enable-cuda' etc. when benv is configured.
 @*/
MemObj_t *BENV_MemAlloc(size_t n, MemObj_type t)
{
    MemObj_t *mo = 0;
    /* This could be made more flexible by separately registering
       routines for different devices */
    if (t == MEMOBJ_MALLOC) {
	mo              = (MemObj_t *)malloc(sizeof(MemObj_t));
	if (!mo)
	    BENVi_MallocErr("MemAlloc", 1, "MemObj_t");

	mo->t           = MEMOBJ_MALLOC;
	mo->memptr      = (void *)malloc(n);
	if (!mo->memptr)
	    BENVi_MallocErr("MemAlloc", n, "byte");
	mo->nbytes      = n;
	mo->free        = free;
	mo->memtodev    = mallocmemcpy;
	mo->memfromdev  = mallocmemcpy;
	mo->memdevtodev = mallocmemcpy;
    }
#ifdef HAVE_CUDA
    else if (t == MEMOBJ_CUDA) {
	mo = BENVi_MemCUDAAllocate(n);
    }
#endif
#ifdef HAVE_HIP
    else if (t == MEMOBJ_HIP) {
	mo = BENVi_MemHIPAllocate(n);
    }
#endif
#ifdef HAVE_SYCL
    else if (t == MEMOBJ_SYCL) {
	mo = BENVi_MemSYCLAllocate(n);
    }
#endif
    else {
	fprintf(stderr, "Unknown memory type requested: %d\n", t);
    }
    return mo;
}

/*@ BENV_MemFree - Free a memory object

Input Parameter:
. mo - Memory object to free, previously created with 'BENV_MemAlloc'
@*/
void BENV_MemFree(MemObj_t *mo)
{
    if (mo) {
	if (mo->free) {
	    (mo->free)(mo->memptr);
	}
	free(mo);
    }
}

static const char *arg_memtype = "-memtype";

/*@ BENV_MemArg - Convenience routine to get memory type from command line

Input Parameters:
+ argc - Argument count
- argv - Argument vector. Typically, 'argc' and 'argv' are the values from
 'main'

In/Out Parameters:
. argcnt - Pointer to the index in 'argv' of the current argument. If
 an argument is recognized, this index is incremented.

Output Parameters:
. mtype - The requested memory type if found in the command line.

Notes:
 The available memory types depend on how the benv package was configured.
 'malloc' is always available. Other options include 'cuda', 'HIP', and 'scyl'
 The command line option is '-memtype'
 @*/
int BENV_MemArg(int argc, char **argv, int *argcnt, MemObj_type *mtype)
{
    int rc=0;
    int i=*argcnt;

    if (strcmp(argv[i], arg_memtype) == 0) {
        i++;
	if (strcmp(argv[i], "malloc") == 0) {
            *mtype = MEMOBJ_MALLOC;
            rc = 1;
        }
#ifdef HAVE_CUDA
	else if (strcmp(argv[i], "cuda") == 0) {
            *mtype = MEMOBJ_CUDA;
            rc = 1;
        }
#endif
#ifdef HAVE_HIP
	else if (strcmp(argv[i], "HIP") == 0) {
            *mtype = MEMOBJ_HIP;
            rc = 1;
        }
#endif
#ifdef HAVE_SYCL
	else if (strcmp(argv[i], "sycl") == 0) {
            *mtype = MEMOBJ_SYCL;
            rc = 1;
        }
#endif
    }
    *argcnt = i;
    return rc;
}

/*@
  BENV_MemArgConfig - Configure argument names for Mem

Input Parameters:
+ arg - String matching defined names. See below
- newname - String with replacement argument name

.N returnvalue

Notes:
The known argument names are
.n
.n   memtype - memory type
.n
  @*/
int BENV_MemArgConfig(const char *arg, const char *newname)
{
    int rc = 0;
    if (strcmp(arg, "memtype") == 0) {
	arg_memtype    = strdup(newname);
    }
    else {
	rc = 1;
    }
    return rc;
}

/*@ BENV_MemArgPrintUsage - Print the command line usage information for memory
 objects

Input Parameter:
fp - File pointer for output

Notes:
 Writes out the usage information for the command line arguments understood by
 'BENV_MemArg'
 @*/
void BENV_MemArgPrintUsage(FILE *fp)
{
    fprintf(fp, "\
  -memtype name - Manage data for the named device type. Valid values are:\n\
      malloc - Memory on the CPU (host), allocated with malloc\n");
#ifdef HAVE_CUDA
    fprintf(fp, "\
      cuda   - Memory on the GPU, allocated with cudaMalloc\n");
#endif
#ifdef HAVE_HIP
    fprintf(fp, "\
      HIP    - Memory on the GPU, allocated with hipMalloc\n");
#endif
#ifdef HAVE_SYCL
    fprintf(fp, "\
      sycl    - Memory on the GPU, allocated with OpenAPI malloc_device\n");
#endif
}

/*
 * We also need routines to make the other device available.
 * These will need to evolve as we seek to access more features
 */
/*@ BENV_MemDeviceInit - Initialize the device that uses the requested type
  of memory object

Input Parameter
. mtype - Type of memory (e.g., 'MEMOBJ_MALLOC' or 'MEMOBJ_CUDA').

Notes:
 Initializes a device that supports the selected memory type. This routine
 is not required for using the Memory object routines, but if it is not used,
 the user is responsible for any device initializations. See the source code
 for the CUDA implementation for an example.

Here are the possible types of memory. Whether they are available depends on
how Base Env was configured and what support was found during the configuration
process.
.n
.n MEMOBJ_MALLOC - Memory allocated with malloc. Always available.
.n MEMOBJ_CUDA - Memory managed with CUDA (NVIDIA)
.n MEMOBJ_HIP - Memory managed with HIP (AMD)
.n MEMOBJ_SYCL - Memory managed with SYSL (Intel)

@*/
int BENV_MemDeviceInit(MemObj_type mtype)
{
    int rc = 0;

    if (mtype == MEMOBJ_MALLOC) {
        /* Nothing to do */
        ;
    }
#ifdef HAVE_CUDA
    else if (mtype == MEMOBJ_CUDA) {
        rc = BENVi_MemDeviceInitCUDA();
    }
#endif
#ifdef HAVE_HIP
    else if (mtype == MEMOBJ_HIP) {
        rc = BENVi_MemDeviceInitHIP();
    }
#endif
#ifdef HAVE_SYCL
    else if (mtype == MEMOBJ_SYCL) {
        rc = BENVi_MemDeviceInitSYCL();
    }
#endif
    else {
        fprintf(stderr, "Unknown memory type %d\n", mtype);
        rc = -1;
    }
    return rc;
}

/* Set and check values in a memory block, considered as a 1-3 dimentional
   array of storage. The routines allow picking a subcube within an overall
   block of memory.

   For systems with accellerators, the operations take place on the
   accelerator, through routines included in the relevant system-specific
   file (e.g., memcuda.cu).
 */
static int BENVi_MemInitValueMalloc(double *mem, int ndim, const int *gdims,
	const int *lstarts, const int *loffsets, const int *ldims,
	const int *ldimsdecl, double startval, double incrval);
static int BENVi_MemCheckValueMalloc(double *mem, int ndim, const int *gdims,
	const int *lstarts, const int *loffsets, const int *ldims,
	const int *ldimsdecl, double startval, double incrval);
static int BENVi_MemInitValueMalloc1D(double *mem, int gsize, int lstart,
				      int loffset, int lsize,
				      double startval, double incrval);
static int BENVi_MemCheckValueMalloc1D(double *mem,  int gsize, int lstart,
				      int loffset, int lsize,
				       double startval, double incrval);

/*@ BENV_MemInitValue - Initialize a memory object as a subgrid within a larger
 grid

Input Parameters:
+ mo - The memory object to initialize
. ndim - number of dimensions for grid (between 1 and 3)
. gdims - size of the global grid
. lstarts - index in the global grid of the start of this subgrid
. loffsets - start of subcube to set within the local grid
. ldims - size of the local grid
. ldimsdecl - declared size of the local grid
. startval - initial value for grid
- incrval - increment each value by this ammount

Notes:
   This gives a more detailed description of the meaning of the input
   parameters.

   Initialize values for an n-dimensional mesh, with values
   The global mesh starts at (0,0,0) and has sizes '(gdims[0], gdims[1],
   gdims[2])'
   The local mesh, corresponding to the memory at 'mo->memptr', has
   declared sizes 'ldimsdecl[0..2]' and starts at 'lstarts[0..2]' in the
   global mesh. The subcube to set (itself a subcube of the memory in 'mo')
   starts at 'loffset[0..2]' (relative to the begining of the subcube and
   has size 'ldims[0..2]'.

   'ldimsdecl' and 'loffsets' allow defining the subcube with a halo.
   Typically, if there is no halo, then 'ldims == ldimsdecl' and 'loffsets == 0'

   There are two natural choices for lstarts.
.n   1. The coords in the global mesh of the [0,0,0] element of the subcube
.n   2. The coords in the global mesh of the loffset element of the subcube
   These routines assume the second choice. This is convenient for working
   with overlapping meshes with halo resions.
  @*/
int BENV_MemInitValue(MemObj_t *mo, int ndim, const int *gdims,
	const int *lstarts, const int *loffsets, const int *ldims,
	const int *ldimsdecl, double startval, double incrval)
{
    int rc;
    if (mo->t == MEMOBJ_MALLOC) {
	rc = BENVi_MemInitValueMalloc(mo->memptr, ndim, gdims, lstarts,
				      loffsets, ldims, ldimsdecl,
				      startval, incrval);
}
#ifdef HAVE_CUDA
    else if (mo->t == MEMOBJ_CUDA) {
	rc = BENVi_MemInitValueCUDA(mo->memptr, ndim, gdims, lstarts,
				    loffsets, ldims, ldimsdecl,
				    startval, incrval);
}
#endif
#ifdef HAVE_HIP
    else if (mo->t == MEMOBJ_HIP) {
	rc = BENVi_MemInitValueHIP(mo->memptr, ndim, gdims, lstarts,
				   loffsets, ldims, ldimsdecl,
				   startval, incrval);
    }
#endif
#ifdef HAVE_SYCL
    else if (mo->t == MEMOBJ_SYCL) {
        rc = BENVi_MemXXSYCL();
    }
#endif

    else {
	fprintf(stderr, "Unknown memory type %d\n", mo->t);
	rc = -1;
    }
	return rc;
}

static int BENVi_MemInitValueMalloc(double *mem, int ndim, const int *gdims,
	const int *lstarts, const int *loffsets, const int *ldims,
	const int *ldimsdecl, double startval, double incrval)
{
    for (int i=0; i<ldims[0]; i++) {
	for (int j=0; j<ldims[1]; j++) {
	    for (int k=0; k<ldims[2]; k++) {
		double gval;
                /* Value based on global mesh and the location of this
		   subcunbe within that global mesh */
		gval = startval + incrval *
		    idx3(lstarts[0]+i,lstarts[1]+j,lstarts[2]+k,
			    gdims[0],gdims[1],gdims[2]);
		mem[idx3(loffsets[0]+i,loffsets[1]+j,loffsets[2]+k,
			 ldimsdecl[0],ldimsdecl[1],ldimsdecl[2])] = gval;
	    }
	}
    }
    return 0;
}

/*@ BENV_MemCheckValue - Check memory object values

Input Parameters:
+ mo - The memory object to initialize
. ndim - number of dimensions for grid (between 1 and 3)
. gdims - size of the global grid
. lstarts - index in the global grid of the start of this subgrid
. loffsets - start of subcube to set within the local grid
. ldims - size of the local grid
. ldimsdecl - declared size of the local grid
. startval - initial value for grid
- incrval - increment each value by this amount

Return value:
Returns 0 on no error. Returns -1 on unknown error. Returns a positive value
on error. The index of an error (it may be the first, but that is not
guaranteed) is the return value -1 (i.e., a return of '1' means that memory
location '0' differs from the expected value).
  @*/
int BENV_MemCheckValue(MemObj_t *mo, int ndim, const int *gdims,
	const int *lstarts, const int *loffsets, const int *ldims,
	const int *ldimsdecl, double startval, double incrval)
{
    int rc;
    if (mo->t == MEMOBJ_MALLOC) {
	rc = BENVi_MemCheckValueMalloc(mo->memptr, ndim, gdims, lstarts,
				       loffsets, ldims, ldimsdecl,
				       startval, incrval);
    }
#ifdef HAVE_CUDA
    else if (mo->t == MEMOBJ_CUDA) {
	rc = BENVi_MemCheckValueCUDA(mo->memptr, ndim, gdims, lstarts,
				     loffsets, ldims, ldimsdecl,
				     startval, incrval);
    }
#endif
#ifdef HAVE_HIP
    else if (mo->t == MEMOBJ_HIP) {
	rc = BENVi_MemCheckValueHIP(mo->memptr, ndim, gdims, lstarts,
				    loffsets, ldims, ldimsdecl,
				    startval, incrval);
    }
#endif
#ifdef HAVE_SYCL
    else if (mo->t == MEMOBJ_SYCL) {
        rc = BENVi_MemXXSYCL();
    }
#endif
    else {
	fprintf(stderr, "Unknown memory type %d\n", mo->t);
	rc = -1;
    }
    return rc;
}

static int BENVi_MemCheckValueMalloc(double *mem, int ndim, const int *gdims,
	const int *lstarts, const int *loffsets, const int *ldims,
	const int *ldimsdecl, double startval, double incrval)
{
    int rc = 0;
    for (int i=0; i<ldims[0]; i++) {
	for (int j=0; j<ldims[1]; j++) {
	    for (int k=0; k<ldims[2]; k++) {
		double gval, bval;
                /* Value based on global mesh and the location of this
		   subcunbe within that global mesh */

		/* Comparison value */
		gval = startval + incrval *
		    idx3(lstarts[0]+i,lstarts[1]+j,lstarts[2]+k,
			 gdims[0],gdims[1],gdims[2]);
		bval = mem[idx3(loffsets[0]+i,loffsets[1]+j,loffsets[2]+k,
				ldimsdecl[0],ldimsdecl[1],ldimsdecl[2])];
		if (gval != bval) {
		    /* what? */
		    rc = 1+idx3(i,j,k,ldims[0],ldims[1],ldims[2]);
		    return rc;
		}
	    }
	}
    }
    /* 0 if all the same, > 0 for a difference */
    return rc;

}

/*@ BENV_MemInitValue1D - Simplified initialization for Memory Objects

Input Parameters:
+ mo - Memory object to initialize
. gsize - size of the global 1D grid
. lstart - index in the global grid of the start of this 1D subgrid
. loffset - start of subcube to set within the local grid
. lsize - size of the local grid
. startval - initial value for grid
- incrval - increment each value by this ammount

  @*/
int BENV_MemInitValue1D(MemObj_t *mo, int gsize, int lstart, int loffset,
			int lsize, double startval, double incrval)
{
    int rc;
    if (mo->t == MEMOBJ_MALLOC) {
	rc = BENVi_MemInitValueMalloc1D(mo->memptr, gsize, lstart, loffset,
					lsize, startval, incrval);
    }
#ifdef HAVE_CUDA
    else if (mo->t == MEMOBJ_CUDA) {
	rc = BENVi_MemInitValueCUDA1D(mo->memptr, gsize, lstart, loffset,
				      lsize, startval, incrval);
}
#endif
#ifdef HAVE_HIP
    else if (mo->t == MEMOBJ_HIP) {
	rc = BENVi_MemInitValueHIP1D(mo->memptr, gsize, lstart, loffset,
				      lsize, startval, incrval);
    }
#endif
#ifdef HAVE_SYCL
    else if (mo->t == MEMOBJ_SYCL) {
        rc = BENVi_MemXXSYCL();
    }
#endif
    else {
	fprintf(stderr, "Unknown memory type %d\n", mo->t);
	rc = -1;
    }
    return rc;
}

static int BENVi_MemInitValueMalloc1D(double *mem, int gsize, int lstart,
				      int loffset, int lsize,
				      double startval, double incrval)
{
    /* Special case for constant value in all entries */
    if (incrval == 0) {
	for (int i=0; i<lsize; i++) mem[loffset+i] = startval;
    }
    else {
	/* Could use induction for gval, but this makes this a foreach loop */
	for (int i=0; i<lsize; i++) {
	    double gval;
	    /* Value based on global mesh and the location of this
	       subcunbe within that global mesh */
	    gval = startval + incrval * (i + lstart);
	    mem[loffset+i] = gval;
	}
    }
    return 0;
}

/*@ BENV_MemCheckValue1D - Simplified value check for Memory Objects

Input Parameters:
+ mo - Memory object to initialize
. gsize - size of the global 1D grid
. lstart - index in the global grid of the start of this 1D subgrid
. loffset - start of subcube to set within the local grid
. lsize - size of the local grid
. startval - initial value for grid
- incrval - increment each value by this ammount

Return value:
Returns 0 on no error. Returns -1 on unknown error. Returns a positive value
on error. The index of an error (it may be the first, but that is not
guaranteed) is the return value -1 (i.e., a return of '1' means that memory
location '0' differs from the expected value).
  @*/
int BENV_MemCheckValue1D(MemObj_t *mo, int gsize, int lstart,
			 int loffset, int lsize,
			 double startval, double incrval)
{
    int rc;
    if (mo->t == MEMOBJ_MALLOC) {
	rc = BENVi_MemCheckValueMalloc1D(mo->memptr, gsize, lstart, loffset,
					 lsize, startval, incrval);
}
#ifdef HAVE_CUDA
    else if (mo->t == MEMOBJ_CUDA) {
	rc = BENVi_MemCheckValueCUDA1D(mo->memptr, gsize, lstart, loffset,
				       lsize, startval, incrval);
}
#endif
#ifdef HAVE_HIP
    else if (mo->t == MEMOBJ_HIP) {
	rc = BENVi_MemCheckValueHIP1D(mo->memptr, gsize, lstart, loffset,
				       lsize, startval, incrval);
    }
#endif
#ifdef HAVE_SYCL
    else if (mo->t == MEMOBJ_SYCL) {
        rc = BENVi_MemXXSYCL();
    }
#endif
    else {
	fprintf(stderr, "Unknown memory type %d\n", mo->t);
	rc = -1;
    }
    return rc;
}

static int BENVi_MemCheckValueMalloc1D(double *mem,  int gsize, int lstart,
				      int loffset, int lsize,
				      double startval, double incrval)
{
    int rc = 0;
    for (int i=0; i<lsize; i++) {
	double gval, bval;
	gval = startval + incrval * (i + lstart);
	bval = mem[loffset+i];
	if (gval != bval) {
	    rc = 1+i;
	    return rc;
	}
    }
    /* 0 if all the same, > 0 for a difference */
    return rc;
}

/*@ BENV_MemInitValueWithHost - Initialize a memory object using the host

Input Parameters:
+ mo - The memory object to initialize
. ndim - number of dimensions for grid (between 1 and 3)
. gdims - size of the global grid
. lstarts - index in the global grid of the start of this subgrid
. loffsets - start of subcube to set within the local grid
. ldims - size of the local grid
. ldimsdecl - declared size of the local grid
. startval - initial value for grid
- incrval - increment each value by this ammount

  Notes:
 Implements an alternative to init/check on the device by performing
 the operations on the host and using the move to/from device features of
 a memory object.
  @*/
int BENV_MemInitValueWithHost(MemObj_t *mo, int ndim, const int *gdims,
	const int *lstarts, const int *loffsets, const int *ldims,
	const int *ldimsdecl, double startval, double incrval)
{
    int rc;
    if (mo->t == MEMOBJ_MALLOC) {
	rc = BENVi_MemInitValueMalloc(mo->memptr, ndim, gdims, lstarts,
				      loffsets, ldims, ldimsdecl,
				      startval, incrval);
    }
#ifdef HAVE_CUDA
    else if (mo->t == MEMOBJ_CUDA) {
	/* Create memory on the host, init there, then copy to device */
	MemObj_t *mohost;
	mohost = BENV_MemAlloc(mo->nbytes, MEMOBJ_MALLOC);
	rc = BENVi_MemInitValueMalloc(mohost->memptr,ndim, gdims, lstarts,
				      loffsets, ldims, ldimsdecl,
				      startval, incrval);
	mo->memtodev(mo->memptr, mohost->memptr, mo->nbytes);
	mohost->free(mohost);
    }
#endif
#ifdef HAVE_HIP
    else if (mo->t == MEMOBJ_HIP) {
	/* Create memory on the host, init there, then copy to device */
	MemObj_t *mohost;
	mohost = BENV_MemAlloc(mo->nbytes, MEMOBJ_MALLOC);
	rc = BENVi_MemInitValueMalloc(mohost->memptr,ndim, gdims, lstarts,
				      loffsets, ldims, ldimsdecl,
				      startval, incrval);
	mo->memtodev(mo->memptr, mohost->memptr, mo->nbytes);
	mohost->free(mohost);
    }
#endif
#ifdef HAVE_SYCL
    else if (mo->t == MEMOBJ_SYCL) {
        rc = BENVi_MemXXSYCL();
    }
#endif
    else {
	fprintf(stderr, "Unknown memory type %d\n", mo->t);
	rc = -1;
    }
    return rc;
}

/*@ BENV_MemCheckValueWithHost - Check the values of memory object on the host

Input Parameters:

Notes:
Moves data to the host and uses the 'MEMOBJ_MALLOC' version to check the data.
This allows the user to rely only on code that runs on the host to perform
checks (and with the corresponing init code, the initialization).
  @*/
int BENV_MemCheckValueWithHost(MemObj_t *mo, int ndim, const int *gdims,
	const int *lstarts, const int *loffsets, const int *ldims,
	const int *ldimsdecl, double startval, double incrval)
{
    int rc;
    if (mo->t == MEMOBJ_MALLOC) {
	rc = BENVi_MemCheckValueMalloc(mo->memptr, ndim, gdims, lstarts,
				       loffsets, ldims, ldimsdecl,
				       startval, incrval);
    }
#ifdef HAVE_CUDA
    else if (mo->t == MEMOBJ_CUDA) {
	/* Create memory on the host, copy data there, then check */
	MemObj_t *mohost;
	mohost = BENV_MemAlloc(mo->nbytes, MEMOBJ_MALLOC);
	mo->memtodev(mohost->memptr, mo->memptr, mo->nbytes);
	rc = BENVi_MemCheckValueMalloc(mohost->memptr, ndim, gdims, lstarts,
				       loffsets, ldims, ldimsdecl,
				       startval, incrval);
	mohost->free(mohost);
    }
#endif
#ifdef HAVE_HIP
    else if (mo->t == MEMOBJ_HIP) {
	/* Create memory on the host, copy data there, then check */
	MemObj_t *mohost;
	mohost = BENV_MemAlloc(mo->nbytes, MEMOBJ_MALLOC);
	mo->memtodev(mohost->memptr, mo->memptr, mo->nbytes);
	rc = BENVi_MemCheckValueMalloc(mohost->memptr, ndim, gdims, lstarts,
				       loffsets, ldims, ldimsdecl,
				       startval, incrval);
	mohost->free(mohost);
    }
#endif
#ifdef HAVE_SYCL
    else if (mo->t == MEMOBJ_SYCL) {
        rc = BENVi_MemXXSYCL();
    }
#endif
    else {
	fprintf(stderr, "Unknown memory type %d\n", mo->t);
	rc = -1;
    }
    return rc;
}

/*@ BENV_MemInitValue1DWithHost - Simplified initialization for Memory Objects, from the host

Input Parameters:
+ mo - Memory object to initialize
. gsize - size of the global 1D grid
. lstart - index in the global grid of the start of this 1D subgrid
. loffset - start of subcube to set within the local grid
. lsize - size of the local grid
. startval - initial value for grid
- incrval - increment each value by this ammount

Notes:
This routine is similar to 'BENV_MemInitValue1D' except the initialization
is performed on the host rather than in the device. This typcially is
implemented by allocating memory on the host, initializing it, and then
copying the data to the device.

See also:
BENV_MemInitValue1D
  @*/
int BENV_MemInitValue1DWithHost(MemObj_t *mo, int gsize, int lstart,
				int loffset, int lsize,
				double startval, double incrval)
{
    int rc;
    if (mo->t == MEMOBJ_MALLOC) {
	rc = BENVi_MemInitValueMalloc1D(mo->memptr, gsize, lstart, loffset,
					lsize, startval, incrval);
    }
#ifdef HAVE_CUDA
    else if (mo->t == MEMOBJ_CUDA) {
	/* Create memory on the host, init there, then copy to device */
	MemObj_t *mohost;
	mohost = BENV_MemAlloc(mo->nbytes, MEMOBJ_MALLOC);
	rc = BENVi_MemInitValueMalloc1D(mo->memptr, gsize, lstart, loffset,
					lsize, startval, incrval);
	mo->memtodev(mo->memptr, mohost->memptr, mo->nbytes);
	mohost->free(mohost);
    }
#endif
#ifdef HAVE_HIP
    else if (mo->t == MEMOBJ_HIP) {
	/* Create memory on the host, init there, then copy to device */
	MemObj_t *mohost;
	mohost = BENV_MemAlloc(mo->nbytes, MEMOBJ_MALLOC);
	rc = BENVi_MemInitValueMalloc1D(mo->memptr, gsize, lstart, loffset,
					lsize, startval, incrval);
	mo->memtodev(mo->memptr, mohost->memptr, mo->nbytes);
	mohost->free(mohost);
    }
#endif
#ifdef HAVE_SYCL
    else if (mo->t == MEMOBJ_SYCL) {
        rc = BENVi_MemXXSYCL();
    }
#endif
    else {
	fprintf(stderr, "Unknown memory type %d\n", mo->t);
	rc = -1;
    }
    return rc;
}

/*@ BENV_MemCheckValue1DWithHost - Check the values of memory object for a 1D
 set on the host

Input Parameters:
+ mo - Memory object to initialize
. gsize - size of the global 1D grid
. lstart - index in the global grid of the start of this 1D subgrid
. loffset - start of subcube to set within the local grid
. lsize - size of the local grid
. startval - initial value for grid
- incrval - increment each value by this ammount

Return value:
Returns 0 on no error. Returns -1 on unknown error. Returns a positive value
on error. The index of an error (it may be the first, but that is not
guaranteed) is the return value -1 (i.e., a return of '1' means that memory
location '0' differs from the expected value).

Notes:
This routine is similar to 'BENV_MemCheckValue1D' except the check
is performed on the host rather than in the device. This typcially is
implemented by allocating memory on the host, copying the data from the
device, and then running the check on the host.

See also:
BENV_MemCheckValue1D
  @*/
int BENV_MemCheckValue1DWithHost(MemObj_t *mo, int gsize, int lstart,
				 int loffset, int lsize,
				 double startval, double incrval)
{
    int rc;
    if (mo->t == MEMOBJ_MALLOC) {
	rc = BENVi_MemCheckValueMalloc1D(mo->memptr, gsize, lstart, loffset,
					 lsize, startval, incrval);
}
#ifdef HAVE_CUDA
    else if (mo->t == MEMOBJ_CUDA) {
	/* Create memory on the host, copy there, then check */
	MemObj_t *mohost;
	CDBGV(DEVMEM,BASIC,"Create malloc memobj of size %ld\n",(long)(mo->nbytes));
	mohost = BENV_MemAlloc(mo->nbytes, MEMOBJ_MALLOC);
	CDBG(DEVMEM,BASIC,"About to move data from device into host");
	mo->memfromdev(mohost->memptr, mo->memptr, mo->nbytes);
	CDBG(DEVMEM,BASIC,"Use malloc version to check value");
	rc = BENVi_MemCheckValueMalloc1D(mohost->memptr, gsize, lstart, loffset,
					 lsize, startval, incrval);
	CDBG(DEVMEM,BASIC,"About to free the host memory");
	BENV_MemFree(mohost);
    }
#endif
#ifdef HAVE_HIP
    else if (mo->t == MEMOBJ_HIP) {
	/* Create memory on the host, copy there, then check */
	MemObj_t *mohost;
	CDBGV(DEVMEM,BASIC,"Create malloc memobj of size %ld\n",(long)(mo->nbytes));
	mohost = BENV_MemAlloc(mo->nbytes, MEMOBJ_MALLOC);
	CDBG(DEVMEM,BASIC,"About to move data from device into host");
	mo->memfromdev(mohost->memptr, mo->memptr, mo->nbytes);
	CDBG(DEVMEM,BASIC,"Use malloc version to check value");
	rc = BENVi_MemCheckValueMalloc1D(mohost->memptr, gsize, lstart, loffset,
					 lsize, startval, incrval);
	CDBG(DEVMEM,BASIC,"About to free the host memory");
	BENV_MemFree(mohost);
    }
#endif
#ifdef HAVE_SYCL
    else if (mo->t == MEMOBJ_SYCL) {
        rc = BENVi_MemXXSYCL();
    }
#endif
    else {
	fprintf(stderr, "Unknown memory type %d\n", mo->t);
	rc = -1;
    }
    return rc;
}
