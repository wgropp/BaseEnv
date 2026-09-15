#include <stdio.h>
#include "mpi.h"
#include "decomp.h"
#include "stencil.h"
#include "arrindex.h"

/* This is a simple Jacobi relaxation sweep using the 7 point 3-d stencil. */
void sweep(double *aold, double *anew, int n0, int n1, int n2)
{
    double scale = 1./6.;
    int i, j, k;
    for (i=1; i<n0-1; i++) {
	for (j=1; j<n1-1; j++) {
	    for (k=1;k<n2-1; k++) {
		anew[idx3(i,j,k,n0,n1,n2)] =
		    aold[idx3(i,j,k,n0,n1,n2)] +
		    scale* (aold[idx3(i+1,j,k,n0,n1,n2)] +
			    aold[idx3(i-1,j,k,n0,n1,n2)] +
			    aold[idx3(i,j+1,k,n0,n1,n2)] +
			    aold[idx3(i,j-1,k,n0,n1,n2)] +
			    aold[idx3(i,j,k+1,n0,n1,n2)] +
			    aold[idx3(i,j,k-1,n0,n1,n2)]);
	    }
	}
    }
}
