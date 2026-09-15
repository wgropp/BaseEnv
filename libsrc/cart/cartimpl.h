#ifndef CARTIMPL_H_INCLUDED
#define CARTIMPL_H_INCLUDED 1

/* Internal data structures and routines. None of these require MPI to
 make it easier to test without MPI */

#define MAX_DIMS 10

typedef struct {
    int nobj,      /* Number of objects (e.g., nodes or cores) on this level,
		      for each object at the next higher level. E.g.,
		      if there are 2 nodes with 64 cores on each node, this
		      is 64 for the level of cores, not 128. */
	objidx;    /* Index of this process at this level, in [0,nobj).
		      E.g., node number or core number. */
} hwlevinfo_t;

typedef struct {
    int nlevels;           /* Number of available levels */
    hwlevinfo_t *hwl;      /* Information on each level (number of objects
			      and index of this process) */
} hwlevs_t;


/* Information on Cartesian process topology corresponding to node hardware
   levels */
typedef struct {
    int dims[MAX_DIMS];    /* Dimensions at this level */
    int coords[MAX_DIMS];  /* Coords of process at this level */
} cartLevel;

typedef struct cartHierarchy {
    int nlevels;           /* Number of levels in the hierarchy */
    int ndims;             /* Number of dimensions */
    cartLevel *cl;         /* Information on cart topology at each level */
    hwlevinfo_t *hwl;      /* Information on the hw at each level */
} cartHierarchy;

typedef struct {
    int val,    /* factor */
	pwr;    /* power of the factor (e.g., factor occurs pwr times) */
} factor_t;

/* A structure used to work with factors */
typedef struct {
    int nfactors,   /* Number of distinct factors */
	curfactor,  /* Index of current factor. Starts at 0 */
	curpower,   /* value of current power = number used (starts at 0) */
	origidx;    /* Identifies which dimension or level this is from */
    factor_t *factors; /* Array of factors (both value and power) */
} facinfo_t;

typedef enum { BENV_ORDER_C, BENV_ORDER_FORTRAN } arrayorder_t;
void BENVi_RankToCoords(int ndims, const int dims[], int rank,
			arrayorder_t order, int coords[]);
void BENVi_CoordsToRank(int ndims, const int dims[], const int coords[],
			arrayorder_t order, int *rank);

int BENVi_SimpleDimsCreate(int n, int nd, int dims[]);
int BENVi_DistributeDimsOverHW(hwlevs_t *hwlevs,
			       int ndims, const int dims[],
			       const int periods[],
			       cartHierarchy **carth_ptr);

/* Internal but shared routines */
int BENVi_Factor(int n, int *nf_ptr, factor_t **facs_ptr);
void BENVi_PrintCarth(FILE *fp, int newrank, cartHierarchy *carth);
void BENVi_CarthFree(cartHierarchy *carth);

hwlevs_t *BENVi_CreateHwdescFromLevs(int nlevs, int *levsize, int rank);
void BENVi_PrintHWDesc(FILE *fp, hwlevs_t *hwlevs);

int BENVi_Nodecart_create_from_hierarchy_local(const hwlevs_t *hwlevs,
					      int ndims, int dims[],
					      const int periods[],
					      int cartcoords[], int *newrank,
					      cartHierarchy **carth_ptr);

int BENVi_FactorsToDivisors(int nf, factor_t *facs,
			    int *ndivs_ptr, int **divs_ptr);

#endif
