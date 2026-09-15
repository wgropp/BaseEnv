#ifndef DECOMP_H_INCLUDED
#define DECOMP_H_INCLUDED

#ifndef MAX_DIMS
#define MAX_DIMS 5
#endif

typedef struct cartdecompCtx {
    MPI_Comm cartcomm;
    int      ndims, pcoords[MAX_DIMS], psizes[MAX_DIMS], periods[MAX_DIMS];
    int      (*coordsToRank)(struct cartdecompCtx *, const int *coords,
			     int *rank);
    int      (*free)(struct cartdecompCtx *);
    const char *desc;     /* Short description of the decomp method, e.g.,
			     "MPI_Cart_create" or "C-order mesh" */
    void     *extraData;
} cartdecompCtx;

int BENV_CartDecompCreate(int ndims, const int *psizes, const int *periods,
			  MPI_Comm comm, int flavor, cartdecompCtx **ctx);
int BENV_CartDecompFree(cartdecompCtx *ctx);
int BENV_CartDecompGetShift(cartdecompCtx *ctx, int d, int shift, int *nrank);
int BENV_CartDecompCreateMPI(int ndims, const int *psizes, const int *periods,
			     MPI_Comm comm, cartdecompCtx **ctx);
int BENV_CartDecompCreateSimple(int ndims, const int *psizes,
				const int *periods, MPI_Comm comm,
				cartdecompCtx **ctx);

#endif
