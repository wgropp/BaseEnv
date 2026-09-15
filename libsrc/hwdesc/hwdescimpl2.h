#ifndef HWDESCIMPL_H_INCLUDED
#define HWDESCIMPL_H_INCLUDED 1

extern int BENVi_HwdescFlagMask;

/* Shared cvars */
extern int cvar_hwdesc_addNodeName;

/* Internal routines */
int BENVi_FindLeadersInSplit(MPI_Comm pcomm, MPI_Comm comm,
			     hwdescObjInfo *objinfo, hwdescMPIInfo *mpiinfo);
int BENVi_GetBlocksizeFromString(const char *policy, int *loc, int defblock);
int BENVi_DistribRankByPolicy(int np, int rank, int nobj,
			      int blocksize, int *objidx, int *rankinobj,
			      int *npinobj);
#ifdef HAVE_HWLOC
void BENVi_HwdescNodeHwlocPrintInfo(FILE *fp, int cpu);
int BENVi_HwdescNodeHwlocCPUidToLogical(int cpu);
#endif

int BENVi_HwdescNodeArgDebug(int argc, char **argv, int *argcnt,
			     const char *prefix);
#endif
