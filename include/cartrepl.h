#ifndef CARTREPL_H_INCLUDED
#define CARTREPL_H_INCLUDED

/* These routines are replacements for MPI_Cart_xxx routines */
int MPIX_Nodecart_shift(MPI_Comm comm, int direction, int disp,
			int *rank_sources, int *rank_dest);
int MPIX_Nodecart_coords(MPI_Comm comm, int rank, int maxdims, int coords[]);
int MPIX_Nodecart_rank(MPI_Comm comm, const int coords[], int *rank);
int MPIX_Nodecart_sub(MPI_Comm comm, const int remain[], MPI_Comm *subcomm);
int MPIX_Nodecart_get(MPI_Comm comm, int maxdims, int dims[], int periods[],
		      int coords[]);
int MPIX_Nodecart_dim_get(MPI_Comm comm, int *ndims);

int MPIX_Nodecart_create(MPI_Comm comm_old, int ndims, const int dims[],
			 const int periods[], int reorder,
			 MPI_Comm *comm_cart);

/* Not a replacement routine, but available to users that already have
   information on the hw hierarchy for comm */
typedef struct cartHierarchy *cartH_t;
int MPIX_Nodecart_create_from_hierarchy(MPI_Comm comm,
					const hwdescCtx *hwc,
					int ndims, int dims[],
					const int periods[], int *newrank,
					int cartcoords[],
					cartH_t *carth);
/* Hook for MPI-style CVAR support */
void MPIX_Nodecart_cvar_set(const char *name, int value);
int BENV_NodecartArgDebug(int argc, char **argv, int *argcnt,
			  const char *prefix);

/* End of routines that can replace MPI_Cart_xxx routines */

#endif /* CARTREPL_H_INCLUDED */
