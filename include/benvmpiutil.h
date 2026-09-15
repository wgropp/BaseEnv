#ifndef BENVMPIUTIL_H_INCLUDED
#define BENVMPIUTIL_H_INCLUDED 1

/* Any utility routines the need MPI go here instead of benvutil.h  */
#include "mstring.h"

#ifdef BENVi_Abort
#undef BENVi_Abort
#endif
#define BENVi_Abort() MPI_Abort(MPI_COMM_WORLD,1)

int BENV_CheckSameInts(MPI_Comm comm, const int *arr, int n);
int BENV_CheckNonNull(MPI_Comm comm, const void **arr, int n);
void BENV_MPIFileErr(int rc, const char *msg);
int BENVi_GetCommName(MPI_Comm comm, const char *defname, const char **cname);
char *BENVi_GetInfoString(MPI_Info info, const char *key);
int BENV_DebugArgRank(int argc, char **argv, int *argcnt);
int BENV_DebugPostMPIInit(void);
/* Filename from rank */
const char *BENV_PerRankFilename(const char *pattern, MPI_Comm comm);

int BENV_CollPrintStr(FILE*fp, MPI_Comm comm, int croot, const char *str);
int BENV_CollPrintInt(FILE *fp, MPI_Comm comm, int croot, int v);
int BENV_CollPrintFmt(FILE*fp, MPI_Comm comm, int croot, const char *fmt, ...);

#endif
