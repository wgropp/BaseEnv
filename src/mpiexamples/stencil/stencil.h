#ifndef STENCIL_H_INCLUDED
#define STENCIL_H_INCLUDED

#ifndef MAX_DIMS
#define MAX_DIMS 3
#endif

typedef struct { double tcommPost, tcommWait, tsweep;
    long commlen;} timingData;

void haloexchangeInit(cartdecompCtx *ctx,
		      int halowidth, int useSubarray, const int larraylen[],
		      const int larraylenhalo[]);
void haloexchange(MPI_Comm comm, double *lmeshbuf, timingData *td);

void sweep(double *, double *, int, int, int);

#endif
