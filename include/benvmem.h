#ifndef BENVMEM_H_INCLUDED
#define BENVMEM_H_INCLUDED

/* Needed for size_t */
#include <stdlib.h>

/* We may need to define and use some of these in code compiled for C++/CUDA */
#ifdef __cplusplus
extern "C" {
#endif

typedef enum { MEMOBJ_MALLOC, MEMOBJ_CUDA, MEMOBJ_HIP } MemObj_type;
typedef struct memobj {
    void *memptr;
    MemObj_type t;
    size_t nbytes;
    /* private routines for operations with memptr */
    void (*free)(void *);
    void (*memtodev)(void *restrict dest, const void *restrict src, size_t n);
    void (*memfromdev)(void *restrict dest, const void *restrict src, size_t n);
    void (*memdevtodev)(void *restrict dest, const void *restrict src, size_t n);
} MemObj_t;

void BENV_MemDebug(int v, int wrank);
void BENV_MemFree(MemObj_t *mo);
MemObj_t *BENV_MemAlloc(size_t n, MemObj_type t);
int BENV_MemArg(int, char **, int *, MemObj_type *);
void BENV_MemArgPrintUsage(FILE *fp);
int BENV_MemArgConfig(const char *arg, const char *newname);

int BENV_MemDeviceInit(MemObj_type mtype);
int BENV_MemInitValue(MemObj_t *mo, int ndim, const int *gdims,
		      const int *lstarts, const int *loffsets, const int *ldims,
		      const int *ldimsdecl, double startval, double incrval);
int BENV_MemCheckValue(MemObj_t *mo, int ndim, const int *gdims,
		       const int *lstarts, const int *loffsets, const int *ldims,
		       const int *ldimsdecl, double startval, double incrval);

/* Versions that do all work on host */
int BENV_MemInitValueWithHost(MemObj_t *mo, int ndim, const int *gdims,
			      const int *lstarts, const int *loffsets,
			      const int *ldims, const int *ldimsdecl,
			      double startval, double incrval);
int BENV_MemCheckValueWithHost(MemObj_t *mo, int ndim, const int *gdims,
			       const int *lstarts, const int *loffsets,
			       const int *ldims, const int *ldimsdecl,
			       double startval, double incrval);

/* Add 1-d version for simplicity */
int BENV_MemInitValue1D(MemObj_t *mo, int gsize, int lstart, int loffset,
			int lsize, double startval, double incrval);
int BENV_MemCheckValue1D(MemObj_t *mo, int gsize, int lstart, int loffset,
			 int lsize, double startval, double incrval);

/* Versions that do all work on host */
int BENV_MemInitValue1DWithHost(MemObj_t *mo, int gsize, int lstart,
				int loffset, int lsize,
				double startval, double incrval);
int BENV_MemCheckValue1DWithHost(MemObj_t *mo, int gsize, int lstart,
				 int loffset, int lsize,
				 double startval, double incrval);

#ifdef __cplusplus
}
#endif
#endif
