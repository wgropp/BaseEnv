#ifndef MEMIMPL_H_INCLUDED
#define MEMIMPL_H_INCLUDED

#ifdef HAVE_CUDA
#ifdef USING_NVCC
extern "C" {
#endif
    MemObj_t *BENVi_MemCUDAAllocate(size_t n);
    int BENVi_MemDeviceInitCUDA(void);
    int BENVi_MemInitValueCUDA(double *mem, int ndim, const int *gdims,
			       const int *lstarts, const int *loffsets,
			       const int *ldims, const int *ldimsdecl,
			       double startval, double incrval);
    int BENVi_MemCheckValueCUDA(double *mem, int ndim, const int *gdims,
			       const int *lstarts, const int *loffsets,
			       const int *ldims, const int *ldimsdecl,
			       double startval, double incrval);
    int BENVi_MemInitValueCUDA1D(double *mem, int gsize, int lstart,
				   int loffset, int lsize,
				   double startval, double incrval);
    int BENVi_MemCheckValueCUDA1D(double *mem,  int gsize, int lstart,
				    int loffset, int lsize,
				    double startval, double incrval);
#ifdef USING_NVCC
}
#endif
#endif /* HAVE_CUDA */

#ifdef HAVE_HIP
#ifdef USING_HIPCC
extern "C" {
#endif
    MemObj_t *BENVi_MemHIPAllocate(size_t n);
    int BENVi_MemDeviceInitHIP(void);
    int BENVi_MemInitValueHIP(double *mem, int ndim, const int *gdims,
			      const int *lstarts, const int *loffsets,
			      const int *ldims, const int *ldimsdecl,
			      double startval, double incrval);
    int BENVi_MemCheckValueHIP(double *mem, int ndim, const int *gdims,
			       const int *lstarts, const int *loffsets,
			       const int *ldims, const int *ldimsdecl,
			       double startval, double incrval);
    int BENVi_MemInitValueHIP1D(double *mem, int gsize, int lstart,
				   int loffset, int lsize,
				   double startval, double incrval);
    int BENVi_MemCheckValueHIP1D(double *mem,  int gsize, int lstart,
				    int loffset, int lsize,
				    double startval, double incrval);
#ifdef USING_HIPCC
}
#endif
#endif /* HAVE HIP */

#ifdef HAVE_SYCL
#endif /* HAVE_SYCL */

#endif
