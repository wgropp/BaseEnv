/*
  ctestinput - Create an input dataset for a Cartesian mesh.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "benvconf.h"
#define NO_MPI_INCLUDE
#include "benvutil.h"

#define MAX_DIMS 3
typedef enum { C_ORDER, F_ORDER } ordertype;
typedef enum { OUT_INDEX, OUT_TUPLE } outputtype;

void printUsage(FILE *);
void writeIdxValue(FILE *fp, int ndims, int sizes[]);
void writeTupleValue(FILE *fp, int ndims, int sizes[], ordertype order);

/*D ctestinput - Create an input dataset of a Cartesian mesh

Command Line Arguments:
+ --dims - Set the number of dimensions. 2 by default
. --sizes n1 ... - Set the size of the mesh. Must come after '--dims n'
. --fortran-order - Use Fortran order (leftmost index varies fastest) for mesh
. --coord-tuple - The output in in tuples. Default is by index (see notes)
- --fname filename - File name for output. mesh.bin by default

Notes:
This writes a file containing integers, representing the coordinates of
each element of a Cartesian mesh. This is intended to be used as the input
file to 'meshio'. The integer values make it easy to confirm that 'meshio' is
correctly using parallel I/O to read the file. By default, it writes just
the index value of each entry, considered as a one-dimentional array.
If '--coord-tuple' is given, each entry is written with a tuple of
values for the index in each dimension.
  D*/
int main(int argc, char **argv)
{
    int i, ndims=2, sizes[MAX_DIMS];
    ordertype order=C_ORDER;
    outputtype outtype=OUT_INDEX;
    const char *fname=0;
    FILE *fp;

    /* Set defaults */
    for (i=0; i<ndims; i++) sizes[i]=32;

    /* Get the command line arguments */
    for (i=1; i<argc; i++) {
	if (strcmp("--dims", argv[i]) == 0) {
	    if (BENV_ArgCheckEnoughArgs(i, argc, argv[i], 1, printUsage))
		abort();
	    ndims = atoi(argv[++i]);
	    if (ndims <= 0 || ndims > MAX_DIMS) {
		fprintf(stderr, "--dims value out of range\n");
		abort();
	    }
	}
	else if (strcmp("--sizes", argv[i]) == 0) {
	    int k;
	    if (BENV_ArgCheckEnoughArgs(i, argc, argv[i], ndims, printUsage))
		abort();
	    for (k=0; k<ndims; k++) sizes[k] = atoi(argv[++i]);
	}
	else if (strcmp("--fortran-order", argv[i]) == 0) {
	    order = F_ORDER;
	}
	else if (strcmp("--coord-tuple", argv[i]) == 0) {
	    outtype = OUT_TUPLE;
	}
	else if (strcmp("--fname", argv[i]) == 0) {
	    if (BENV_ArgCheckEnoughArgs(i, argc, argv[i], 1, printUsage))
		abort();
	    fname = argv[++i];
	}
	else {
	    fprintf(stderr, "Unrecognized argument %s\n", argv[i]);
	    printUsage(stderr);
	    abort();
	}
    }

    /* Sanity check */
    if (!fname) {
	fname = "mesh.bin";
    }
    for (i=0; i<ndims; i++) {
	if (sizes[i] <= 0) {
	    fprintf(stderr, "sizes[%d] = %d <= 0!\n", i, sizes[i]);
	    abort();
	}
    }

    /* open file and output */
    fp = fopen(fname, "w");
    if (!fp) {
	fprintf(stderr, "Unable to open %s for output\n", fname);
	abort();
    }

    /* write to the file */
    if (outtype == OUT_INDEX || (ndims == 1 && outtype == OUT_TUPLE)) {
	writeIdxValue(fp, ndims, sizes);
    }
    else if (outtype == OUT_TUPLE) {
	writeTupleValue(fp, ndims, sizes, order);
    }

    /* close file and return */
    fclose(fp);
    return 0;
}

void printUsage(FILE *fp)
{
    fprintf(fp, "\
ctestinput [--dims n] [--sizes n1 ...] [--fortran-order] [--coord-tuple]\n\
           [--fname filename]\n");
}

void writeIdxValue(FILE *fp, int ndims, int sizes[])
{
    int idx = 0, i;
    for (i=ndims; i<MAX_DIMS; i++) sizes[i] = 1;

    /* This works for all ndims with sizes[i] = 1 for any dim >= ndims */
    /* Note that this is independent of ordering - even though the iteration
       order is different for C and Fortran order, the output values are the
       same - consequtive values */
#if 0
    for (i=0; i<sizes[0]; i++) {
	for (int j=0; j<sizes[1]; j++) {
	    for (int k=0; k<sizes[2]; k++) {
		fwrite(&idx, sizeof(int), 1, fp);
		idx++;
	    }
	}
    }
#else
    int len=sizes[0]*sizes[1]*sizes[2];
    for (idx=0; idx<len; idx++)
	fwrite(&idx, sizeof(int), 1, fp);
#endif
}

void writeTupleValue(FILE *fp, int ndims, int sizes[], ordertype order)
{
    int i, j, k;
    int c[MAX_DIMS];
    for (i=ndims; i<MAX_DIMS; i++) sizes[i] = 1;

    if (order == C_ORDER) {
	c[0] = 0;
	for (i=0; i<sizes[0]; i++) {
	    c[1] = 0;
	    for (j=0; j<sizes[1]; j++) {
		c[2] = 0;
		for (k=0; k<sizes[2]; k++) {
		    fwrite(c, sizeof(int), ndims, fp);
		    c[2]++;
		}
		c[1]++;
	    }
	    c[0]++;
	}
    }
    else if (order == F_ORDER) {
	c[2] = 0;
	for (k=0; i<sizes[2]; k++) {
	    c[1] = 0;
	    for (j=0; j<sizes[1]; j++) {
		c[0] = 0;
		for (i=0; i<sizes[0]; i++) {
		    fwrite(c, sizeof(int), ndims, fp);
		    c[0]++;
		}
		c[1]++;
	    }
	    c[2]++;
	}
    }
}

