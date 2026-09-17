/* -*- Mode: C; c-basic-offset:4 ; -*- */
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
#include "hip/hip_runtime.h"

static void BENVi_MemHIPFree(void *);
static void BENVi_MemHIPMemToDev(void *dest, const void *src, size_t n);
static void BENVi_MemHIPDevToMem(void *dest, const void *src, size_t n);

static void createBlockDecomp(int ndim, const int ldims[],
			      int bdims[], int tdims[]);

/* Global information to manage streams and a scratchpad area shared between
   the host and the devices */
static void *commonmemInDev=0;
static size_t commonmemlen;
static hipStream_t smem; /* May need one for each active device */

#define MAX_HIP_THREADS 1024
#define MAX_HIP_THREADS_2D 32
/* Other decompositions are possible, and could be created with
   MPI_Dims_create (or similar). This is enough for now, though could
   be inefficient in some cases */
#define MAX_HIP_THREADS_3D_X 16
#define MAX_HIP_THREADS_3D_Y 8
#define MAX_HIP_THREADS_3D_Z 8

#define HIP_CHECKERR_CMD(_rc,_msg,_cmd) do { if (_rc != hipSuccess) {\
    fprintf(stderr,"HIP error: %s\n", hipGetErrorString(_rc));\
    fputs(_msg,stderr);fputc('\n',stderr);_cmd;}} while(0)
#define HIP_CHECKERR_RETURN(_rc,_msg,_err) \
    HIP_CHECKERR_CMD(_rc,_msg,return _err)

extern "C" MemObj_t *BENVi_MemHIPAllocate(size_t n)
{
    MemObj_t *mo = 0;
    hipError_t rc;
    mo = (MemObj_t *)malloc(sizeof(MemObj_t));
    mo->t           = MEMOBJ_HIP;
    mo->nbytes      = n;
    rc = hipMalloc(&mo->memptr, n);
    HIP_CHECKERR_RETURN(rc,"Unable to hipMalloc",0);
    mo->free        = BENVi_MemHIPFree;
    mo->memtodev    = BENVi_MemHIPMemToDev;
    mo->memfromdev  = BENVi_MemHIPDevToMem;
    mo->memdevtodev = 0;  /* Need to figure this out */

    return mo;
}

static void BENVi_MemHIPFree(void *memptr)
{
    hipError_t rc;
    rc = hipFree(memptr);
    HIP_CHECKERR_CMD(rc,"Unable to free hipMalloc memory",abort());
}

static void BENVi_MemHIPMemToDev(void *dest, const void *src, size_t n)
{
    hipError_t rc;
    rc = hipMemcpy(dest, src, n, hipMemcpyHostToDevice);
    HIP_CHECKERR_CMD(rc,"Unable to hipMemcpy host to device ",abort());
}

static void BENVi_MemHIPDevToMem(void *dest, const void *src, size_t n)
{
    hipError_t rc;
    rc = hipMemcpy(dest, src, n, hipMemcpyDeviceToHost);
    HIP_CHECKERR_CMD(rc,"Unable to hipMemcpy device to host",abort());
}

/* Initialize a GPU for access */
extern "C" int BENVi_MemDeviceInitHIP(void)
{
    int dev, devcount;
    hipError_t rc;

    rc = hipGetDeviceCount(&devcount);
    HIP_CHECKERR_RETURN(rc,"Unable to get cuda device count\n",-1);

    if (devcount <= 0) {
	fprintf(stderr, "No HIP devices available!\n");
	return -1;
    }
    dev = 0;  /* could be anything in 0,devcount-1 */
    rc = hipSetDevice(dev);
    HIP_CHECKERR_RETURN(rc,"Unable to set cuda device\n",-1);

    /* Create stream and host/device memory */
    commonmemlen = 8*sizeof(int);  /* Get a modest amount of memory - need
				      to tune later */
    rc = hipMalloc(&commonmemInDev, commonmemlen);
    HIP_CHECKERR_RETURN(rc,"Unable to allocated device memory for return values\n",-1);

    rc = hipStreamCreate(&smem);
    HIP_CHECKERR_RETURN(rc,"Unable to create cuda stream",-1);
    return 0;
}

/* Initialize and check values on the device */

/* FIXME: Decide on implementation - easiest is one thread per entry,
   using dim3 blocking.
   For performance, it might make sense to have each thread perform more
   operations, assuming that operations are pipelined */
__global__ void BENVi_HIP_MemInit(double *mem, int ndim, const int *gdims,
	const int *lstarts, const int *loffsets, const int *ldims,
	const int *ldimsdecl, double startval, double incrval)
{
    int i, j, k;
    double gval;

    i = threadIdx.x; /* In [0,ldims[0]) */
    j = threadIdx.y;
    k = threadIdx.z;

    gval = startval + incrval *
	idx3(lstarts[0]+i,lstarts[1]+j,lstarts[2]+k,
	     gdims[0],gdims[1],gdims[2]);
    mem[idx3(loffsets[0]+i,loffsets[1]+j,loffsets[2]+k,
	     ldimsdecl[0],ldimsdecl[1],ldimsdecl[2])] = gval;
}

__global__ void BENVi_HIP_MemCheck(double *mem, int ndim, const int *gdims,
	      const int *lstarts, const int *loffsets, const int *ldims,
	      const int *ldimsdecl, double startval, double incrval, int *rc)
{
    int i, j, k, ival;
    double gval;

    i = threadIdx.x; /* In [0,ldims[0]) */
    j = threadIdx.y;
    k = threadIdx.z;

    gval = startval + incrval *
	idx3(lstarts[0]+i,lstarts[1]+j,lstarts[2]+k,
	     gdims[0],gdims[1],gdims[2]);
    ival = idx3(loffsets[0]+i,loffsets[1]+j,loffsets[2]+k,
		ldimsdecl[0],ldimsdecl[1],ldimsdecl[2]);
    if (mem[ival] != gval) *rc = ival+1;
    else *rc = 0;
}

extern "C" int BENVi_MemInitValueHIP(double *mem, int ndim, const int *gdims,
	const int *lstarts, const int *loffsets, const int *ldims,
	const int *ldimsdecl, double startval, double incrval)
{
    /* Divy up the ldims across the same number of dimentions of
       threads and blocks */
    int tdims[3], bdims[3];
    createBlockDecomp(ndim, ldims, bdims, tdims);
    dim3 bnums(bdims[0], bdims[1], bdims[2]),
	threadnums(tdims[0], tdims[1], tdims[2]);
    BENVi_HIP_MemInit<<<bnums,threadnums>>>(mem, ndim, gdims,
					     lstarts, loffsets, ldims,
					     ldimsdecl, startval, incrval);
    return 0;
}

extern "C" int BENVi_MemCheckValueHIP(double *mem, int ndim, const int *gdims,
	const int *lstarts, const int *loffsets, const int *ldims,
	const int *ldimsdecl, double startval, double incrval)
{
    int rc;
    hipError_t crc;
    int *rcptr = (int *)commonmemInDev; /* Use preallocated host/device memory
					   for return value */
    /* Divy up the ldims across the same number of dimentions of
       threads and blocks */
    int tdims[3], bdims[3];
    createBlockDecomp(ndim, ldims, bdims, tdims);
    dim3 bnums(bdims[0], bdims[1], bdims[2]),
	threadnums(tdims[0], tdims[1], tdims[2]);
    BENVi_HIP_MemCheck<<<bnums,threadnums,0,smem>>>(mem, ndim, gdims,
					     lstarts, loffsets, ldims,
					     ldimsdecl, startval, incrval,
					     rcptr);

    crc = hipStreamSynchronize(smem);
    HIP_CHECKERR_RETURN(crc,"cuda stream sync failed",-1);
    return *rcptr;
}

__global__ void BENVi_HIP_MemInit1D(double *mem, int lstart, int loffset,
				     int lsize,
				     double startval, double incrval)
{
    int i=(blockDim.x * blockIdx.x) + threadIdx.x;
    if (loffset+i >= lsize) return;
    mem[loffset+i] = startval + (i + lstart) * incrval;
}

/* Only return a result if an inconsistent value is found. Otherwise,
   there is a race in the return value. */
__global__ void BENVi_HIP_MemCheck1D(double *mem, int lstart, int loffset,
				      int lsize,
				      double startval, double incrval,
				      int *rc)
{
    int i=(blockDim.x * blockIdx.x) + threadIdx.x;
    if (loffset+i >= lsize) return;
    double gval = startval + (i + lstart) * incrval;
    if (mem[loffset+i] != gval) *rc = i+1;
}

extern "C" int BENVi_MemInitValueHIP1D(double *mem, int gsize, int lstart,
					int loffset, int lsize,
					double startval, double incrval)
{
    int nblocks=1, nthreadsPerBlock=lsize;
    if (lsize > MAX_HIP_THREADS) {
	nthreadsPerBlock = MAX_HIP_THREADS;
	nblocks = (lsize + MAX_HIP_THREADS - 1) / MAX_HIP_THREADS;
    }
    BENVi_HIP_MemInit1D<<<nblocks,nthreadsPerBlock>>>(mem, lstart, loffset, lsize, startval, incrval);
    return 0;
}

extern "C" int BENVi_MemCheckValueHIP1D(double *mem,  int gsize, int lstart,
					 int loffset, int lsize,
					 double startval, double incrval)
{
    int rc;
    hipError_t crc;
    int *rcptr = (int *)commonmemInDev; /* Use preallocated device memory
					   for return value */
    int nblocks=1, nthreadsPerBlock=lsize;
    if (lsize > MAX_HIP_THREADS) {
	nthreadsPerBlock = MAX_HIP_THREADS;
	nblocks = (lsize + MAX_HIP_THREADS - 1) / MAX_HIP_THREADS;
    }

    /* Set the default (success) return value */
    rc = 0;
    BENVi_MemHIPMemToDev(rcptr, &rc, sizeof(int));

    /* Run check. rcptr is set only if an error is found */
    BENVi_HIP_MemCheck1D<<<nblocks,nthreadsPerBlock,0,smem>>>(mem,
				       lstart, loffset, lsize,
				       startval, incrval, rcptr);
    crc = hipStreamSynchronize(smem);
    HIP_CHECKERR_RETURN(crc,"cuda stream sync failed",-1);
    /* Does cudamemcpy, with error check */
    rc = -1;
    BENVi_MemHIPDevToMem(&rc, rcptr, sizeof(int));
    return rc;
}

static void createBlockDecomp(int ndim, const int ldims[],
			      int bdims[], int tdims[])
{
    for (int i=0; i<3; i++) { tdims[i] = 1; bdims[i] = 1; }
    for (int i=0; i<ndim; i++) tdims[i] = ldims[i];
    if (ndim == 1) {
	if (tdims[0] > MAX_HIP_THREADS) tdims[0] = MAX_HIP_THREADS;
    }
    else if (ndim == 2) {
	if (tdims[0] > MAX_HIP_THREADS_2D) tdims[0] = MAX_HIP_THREADS_2D;
	if (tdims[1] > MAX_HIP_THREADS_2D) tdims[1] = MAX_HIP_THREADS_2D;
    }
    else {
	if (tdims[0] > MAX_HIP_THREADS_3D_X) tdims[0] = MAX_HIP_THREADS_3D_X;
	if (tdims[1] > MAX_HIP_THREADS_3D_Y) tdims[1] = MAX_HIP_THREADS_3D_Y;
	if (tdims[2] > MAX_HIP_THREADS_3D_Z) tdims[2] = MAX_HIP_THREADS_3D_Y;
    }
    /* Compute the block sizes */
    for (int i=0; i<ndim; i++) bdims[i] = (ldims[i] + (tdims[i]-1))/tdims[i];
}

/* Routine copied from COMMBENCH to print information about the current
   device */
extern "C" void BENVi_MemHIPPrintDevProp(FILE *fp)
{
    hipDeviceProp_t deviceProp;
    hipError_t rc;

    rc = hipGetDeviceProperties(&deviceProp,0);
    HIP_CHECKERR_CMD(rc,"In PrintDevProp",abort());
    fprintf(fp,"Device %d name: %s\n",0,deviceProp.name);
    fprintf(fp,"Clock Frequency: %f GHz\n",deviceProp.clockRate/1.e9);
    fprintf(fp,"Computational Capabilities: %d, %d\n",deviceProp.major,deviceProp.minor);
    fprintf(fp,"Maximum global memory size: %lu\n",deviceProp.totalGlobalMem);
    fprintf(fp,"Maximum constant memory size: %lu\n",deviceProp.totalConstMem);
    fprintf(fp,"Maximum shared memory size per block: %lu\n",deviceProp.sharedMemPerBlock);
    fprintf(fp,"Maximum block dimensions: %dx%dx%d\n",deviceProp.maxThreadsDim[0],deviceProp.maxThreadsDim[1],deviceProp.maxThreadsDim[2]);
    fprintf(fp,"Maximum grid dimensions: %dx%dx%d\n",deviceProp.maxGridSize[0],deviceProp.maxGridSize[1],deviceProp.maxGridSize[2]);
    fprintf(fp,"Maximum threads per block: %d\n",deviceProp.maxThreadsPerBlock);
    fprintf(fp,"Warp size: %d\n",deviceProp.warpSize);
    fprintf(fp,"32-bit Reg. per block: %d\n",deviceProp.regsPerBlock);
    fprintf(fp,"\n");
}

