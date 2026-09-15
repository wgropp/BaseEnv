/*
 * Copyright (C) by University of Illinois 2025
 */
#include "benvconf.h"
#include <stdlib.h>
#include <stdio.h>
#include "mpi.h" // benvutil.h needs mpi.h or NO_MPI_INCLUDE
#include "benvutil.h"
#include "tarray.h"
#include "arrindex.h"

static int dcmp(const void *a, const void *b);

/*@ BENV_TAInit - Create a timing array object

Input Parameters:
. n1, n2, n3 - Size of array in each dimension. These are ordered as
 (test-type, test-parameter, trial). I.e., if there are two test types,
 such as a blocking and nonblocking ping-pong, 'n1' is 2. If there are 10
 different message lengths, 'n2' is 10. If each test is run 5 times, 'n3'
 is 5.

Return Value:
A time array context. Null if an error occurs.
 @*/
TActx *BENV_TAInit(int n1, int n2, int n3)
{
    TActx *ta;

    ta = (TActx *)malloc(sizeof(TActx));
    if (!ta) BENVi_MallocErr("TActx", 1, "TAInit");
    ta->ndims = 3;
    ta->dims[0] = n1;
    ta->dims[1] = n2;
    ta->dims[2] = n3;
    ta->data = (double *)malloc(n1*n2*n3*sizeof(double));
    if (!ta->data) BENVi_MallocErr("TActx.data", n1*n2*n3, "TAInit");

    return ta;
}

/*@ BENV_TAFree - Free a timing array context

Input Parameter:
. ta - Timing array context created with 'BENV_TAInit'
@*/
void BENV_TAFree(TActx *ta)
{
    if (!ta) return;
    if (ta->data) free(ta->data);
    free(ta);
}

/*@ BENV_TASetVal3 - Store a value in a timing array

Input Parameters:
+ ta - Timing array context
. n1,n2,n3 - indices of the element to store
- val - value to store

.N returnvalue
  @*/
int BENV_TASetVal3(TActx *ta, int n1, int n2, int n3, double val)
{
    /* Check in range */
    if (n1 < 0 || n1 >= ta->dims[0] ||
	n2 < 0 || n2 >= ta->dims[1] ||
	n3 < 0 || n3 >= ta->dims[2]) {
	fprintf(stderr, "TASetVal at (%d,%d,%d) out of range (%d,%d,%d)\n",
		n1, n2, n3, ta->dims[0], ta->dims[1], ta->dims[2]);
	return 1;
    }
    ta->data[idx3(n1,n2,n3,ta->dims[0],ta->dims[1],ta->dims[2])] = val;
    return 0;
}

/*@ BENV_TAFindQuartiles - Given an array of data, find the quartiles

Input Parameters:
+ n - Number of elements
. stride - stride between elements (1 for contiguous elements)
- vals - array of values

Output Parameter:
. quartiles - contains the min, first, second, third quartile, and max, in that
 order.

.N returnvalue

Notes:
Quartiles are defined as the values such that 1/4 of the elements of 'vals'
are in each quartile. If there are an odd numebr of elements, the quartile
value is the element of 'vals' that divides two uartiles. If there are an
even number of elements, the quartile value is the arithmetic average of the
values that are just below and above the quartile break. For example, if 'vals'
contained '1,2,3,4,5,6', the quartiles are '1,2,3.5,5,6' (including the min and
max values as the first and last values in the 'quartiles' array).

The 'stride' value allows this routine to work on a multidimensional array
of value, accessing a single, non-contiguous dimension.
  @*/
int BENV_TAFindQuartiles(int n, int stride, const double *vals,
			 double quartiles[5])
{
    int    i, m;
    double *svals;

    /* First, copy the values into a array for sorting */
    svals = (double *)malloc(n*sizeof(double));
    if (!svals) { return -1;}
    for (i=0; i<n; i++) {
	svals[i] = *vals;
	vals += stride;
    }

    qsort(svals, n, sizeof(double), dcmp);

    quartiles[0] = svals[0];     /* min value */
    quartiles[4] = svals[n-1];   /* max value */
    /* Median */
    if (n & 0x1) { /* n is Odd */
	quartiles[2] = svals[n/2];
	m = n/2 +1;
    }
    else {
	quartiles[2] = (svals[n/2-1] + svals[n/2])/2.0;
	m = n/2;
    }
    /* Q1 and Q3 */
    if (m & 0x1) {
	quartiles[1] = svals[m/2];
	quartiles[3] = svals[n/2 + m/2];
    }
    else {
	quartiles[1] = (svals[m/2-1] + svals[m/2])/2.0;
	quartiles[3] = (svals[n/2+m/2-1] + svals[n/2+m/2])/2.0;
    }

    free(svals);
    return 0;
}

int BENV_TAPrintRaw(FILE *fp, TActx *ta)
{
    /* Organize as 2-d tables, 1 for each type */
    for (int i0=0; i0<ta->dims[0]; i0++) {
	for (int i1=0; i1<ta->dims[1]; i1++) {
	    for (int i2=0; i2<ta->dims[2]; i2++) {
		/* Note that this should be consecutive elements of ta->data */
		fprintf(fp, "%.3e\t",
			ta->data[idx3(i0,i1,i2,
				      ta->dims[0],ta->dims[1],ta->dims[2])]);
	    }
	    fputc('\n', fp);
	}
	fputc('\n', fp);
    }
    return 0;
}

/* Internal routines */

static int dcmp(const void *a, const void *b)
{
    double av = *(double*)a, bv = *(double *)b;
    if (av == bv) return 0;
    if (av < bv) return -1;
    return 1;
}

