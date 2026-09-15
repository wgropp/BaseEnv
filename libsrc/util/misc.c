/* Miscellaneous routines to provide uniformity in error messages, memory
   allocation, output of vectors, and argument processing */
#include "benvconf.h"
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
//#include "mpi.h"
#include "benvutil.h"

static void upcase(char *);

/* Standard error report for a memory allocation failure (from malloc,
   strdup, etc.
   Does NOT abort
 */
void BENVi_MallocErr(const char *item, size_t len, const char *basetype)
{
    fprintf(stderr, "Unable to allocate %ld of %s for %s\n",
	    (long)len, basetype, item);
    fflush(stderr);
//    MPI_Abort(MPI_COMM_WORLD, 1);
}

/*@
  BENV_PrintIntTuple - Print an array of values as a tuple

Input Parameters:
+ fp - FILE for output
. n  - Number of elements
. vals - Values
- withEOL - If true, print a newline after the values

Notes:
 Prints a tuple as
.vb
   (vals[0], vals[1], ... , vals[n-1])
.ve
  @*/
void BENV_PrintIntTuple(FILE *fp, int n, const int vals[], int withEOL)
{
    fputs("(", fp);
    BENV_PrintIntList(fp, n, vals, 0);
    fputs(")", fp);
    if (withEOL) fputs("\n", fp);
}

/*@
  BENV_PrintIntList - Print an array of values as a list

Input Parameters:
+ fp - FILE for output
. n  - Number of elements
. vals - Values
- withEOL - If true, print a newline after the values

Notes:
 Prints a list as
.vb
   vals[0], vals[1], ... , vals[n-1]
.ve
  @*/
void BENV_PrintIntList(FILE *fp, int n, const int vals[], int withEOL)
{
    int i;

    for (i=0; i<n-1; i++) {
	fprintf(fp,"%d,", vals[i]);
    }
    if (n > 0)
	fprintf(fp,"%d", vals[n-1]);
    if (withEOL) fputs("\n", fp);
}

/*@
  BENV_PrintRunInfo - Print information about an execution - date, time, args

Input Parameters:
+ fp - FILE pointer
. argc - Commandline argc. May be 0
- argv - Commandline argv. May be null

Notes:
Prints basic information about the run, including the time when the run took
place and the command line arguments. The definition of this routine can be
changed to add additional information, such as system infomration.
@*/
void BENV_PrintRunInfo(FILE *fp, int argc, char **argv)
{
    time_t clock;
    int i;

    clock = time( (time_t *)0 );
    fprintf(fp, "%s\n", ctime(&clock));
    if (argc > 0 && argv) {
	fputs(argv[0], fp);
	for (i=1; i<argc; i++) {
	    fprintf(fp, " \"%s\"", argv[i]);
	}
	fputc('\n', fp);
    }
    /* Consider adding MPI version string as an option */
}

/*@ BENV_ArgGetint - Get an integer value from an argument list

Input Parameters:
+ i - Index of argument to scan
. argc - Number of values in argument list
- argv - Vector of argument values

Output Parameter:
. val - Value of 'argv[i]'

Return value:
Returns 0 on success and non-zero on failure

Notes:
Provided to combine checking for a value as well as that the
argument value is a valid integer.
  @*/
int BENV_ArgGetint(int i, int argc, char **argv, int *val)
{
    if (i < argc) {
	/* FIXME: This should check for valid input */
	*val = atoi(argv[i]);
    }
    else {
	fprintf(stderr, "%s missing value\n", argv[i-1]);
	fflush(stderr);
	return 1;
    }
    return 0;
}

/*@ BENV_ArgGetdouble - Get an double value from an argument list

Input Parameters:
+ i - Index of argument to scan
. argc - Number of values in argument list
- argv - Vector of argument values

Output Parameter:
. val - Value of 'argv[i]' as a double

Return value:
Returns 0 on success and non-zero on failure

Notes:
Provided to combine checking for a value as well as correctness of the
argument value.
  @*/
int BENV_ArgGetdouble(int i, int argc, char **argv, double *val)
{
    if (i < argc) {
	/* FIXME: This should check for valid input */
	*val = strtod(argv[i],NULL);
    }
    else {
	fprintf(stderr, "%s missing value\n", argv[i-1]);
	fflush(stderr);
	return 1;
    }
    return 0;
}

/*@
  BENV_ArgCheckEnoughArgs - Common check for enough commandline arguments

Input Parameters:
+ curarg - Index of current argument
. argc - Number of arguments
. argname - Value of 'argv[curarg]'
. needed - Number of `additional` arguments needed
- printUsage - Pointer to routine to print usage information. May be null.

Return value:
Returns 0 if there are 'needed' more arguments available, 1 otherwise.

Notes:
This routine is provided primarily to provide consistent error messages when
an argument does `not` have enough values. The 'printUsage' argument is used
to provide application-specific information about the arguments.

This routine will write an error message to 'stderr', and will call the routine
passed as 'printUsage' if not null, if there are insufficient arguments
available.
  @*/
int BENV_ArgCheckEnoughArgs(int curarg, int argc, const char *argname,
			    int needed, void (*printUsage)(FILE*))
{
    int rc=0;
    if (curarg+needed >= argc) {
	fprintf(stderr, "Not enough values for %s\n", argname);
	if (printUsage) printUsage(stderr);
	rc = 1;
    }
    return rc;
}


/*@ BENV_GetIntFromEnv - Get an integer value from an environment variable

Input Parameters:
. envname - Name of environment variable

Output Parameter:
. val - Value of the environment variable, interpreted as an integer

Return value:
Returns 0 on success and non-zero on failure

Notes:
Provided to combine checking for a value as well as correctness of the
argument value.
  @*/
int BENV_GetIntFromEnv(const char *envname, int *value)
{
    char       *s;

    s = getenv(envname);
    if (s && *s) {
	char *endptr;
	/* Use strtol since atoi has no error indicator. strtol is awkward,
	   but it has an error indicator */
	long val;
	errno = 0;
	/* A base of zero permits 0nn for octal, 0x for hex, and other for 10 */
	val = strtol(s,&endptr,0);
	if (errno != 0 || endptr == 0 || *endptr != '\0') {
//	    if (verbose) {
//		fprintf(stderr, "Invalid value for %s: %s\n", envname, s );
//	    }
	    return -1;
	}
	else if (val > 0) {
	    *value = (int)val;
	    return 0;
	}
    }
    return 1;
}

/*@ BENV_GetBooleanFromEnv - Get a boolean value from an environment variable

Input Parameters:
. envname - Name of environment variable

Output Parameter:
. val - Value of the environment variable, interpreted as an boolean.

Return value:
Returns 0 on success and non-zero on failure. A value of '1' means the
environment variable is undefined. Any other value indicates that the
environment variable is defined but has an unrecognized value.

Notes:
Provided to combine checking for a value as well as correctness of the
argument value. True values may be 'true' or 'yes' in any case; false
values may be 'false' or 'no' in any case. E.g., "TrUe", "truE", "true", and
"YeS" all are true values.
  @*/
int BENV_GetBooleanFromEnv(const char *envname, int *value)
{
    char       *s;
    int        rc=1;

    s = getenv(envname);
    if (s && *s) {
	char *envval = strdup(s);
	upcase(envval);
	if (strcmp(envval, "TRUE") == 0 ||
	    strcmp(envval, "YES") == 0) {
	    *value = 1;
	    rc = 0;
	}
	else if (strcmp(envval, "FALSE") == 0 ||
		 strcmp(envval, "NO") == 0) {
	    *value = 0;
	    rc = 0;
	}
	else
	    rc = -1;
	free(envval);
    }

    return rc;
}

static void upcase(char *val)
{
    char *p = val;
    if (!p) return;
    while (*p) {
	*p = toupper(*p);
	p++;
    }
}


/*@ BENV_StringCat2 - Concatenate two strings

Input Parameters:
+ s1 - First string (null terminated)
- s2 - Second string (null terminated)

Return Value:
Concatenation of 's1' and 's2', with storage allocated with 'malloc'. Returns
'NULL' if allocation fails.
  @*/
char *BENV_StringCat2(const char *s1, const char *s2)
{
    size_t s1len=strlen(s1), s2len=strlen(s2), s3len;
    char *s3, *s3out;
    const char *p;

    s3len = s1len + s2len + 1;
    s3out = (char *)malloc(s3len);
    if (!s3out) {
	return 0;
    }
    s3 = s3out;
    p  = s1;
    while (*p) *s3++ = *p++;
    p = s2;
    while (*p) *s3++ = *p++;
    *s3 = 0;

    return s3out;
}

#include <stdarg.h>
/*@ BENV_StringApp - Append a number of strings to a string, freeing the original

Input Parameters:
+ s1 - First string (null terminated). Will be freed on a successful exit
. nstr - Number of strings to append
- ... - Pointers to strings to append to s1

Return Value:
Concatenation of 's1' with all of the strings following 'nstr', with storage
allocated with 'malloc'. Returns 'NULL' if allocation fails.

Notes:
This is like 'BENV_StringCat', except it frees the first argument and accepts
more than one string to append.

This routine is designed to be used to append strings, as in\:
.vb
    rmsg = BENV_StringApp(rmsg, 2, desc, "\n");
.ve
  @*/
char *BENV_StringApp(const char *s1, int nstr, ...)
{
    va_list ap;
    size_t slen;
    int    i;
    const char *p1;
    char   *sout, *p, *pout;

    /* Find length of final string */
    slen = 0;
    va_start(ap, nstr);
    for (i=0; i<nstr; i++) {
	p1 = va_arg(ap,char *);
	slen += strlen(p1);
    }
    slen += strlen(s1) + 1;
    va_end(ap);

    /* Create the new string */
    sout = (char *)malloc(slen*sizeof(char));
    if (!sout) {}

    /* Add the initial string */
    p1 = s1;
    pout = sout;
    while (*p1) *pout++ = *p1++;

    va_start(ap, nstr);
    for (i=0; i<nstr; i++) {
	p = va_arg(ap,char *);
	while (*p) *pout++ = *p++;
    }
    *pout = 0;
    va_end(ap);

    free((char *)s1);
    return sout;
}

/* Compute cannonical offset for a multidimensional mesh */
/*@ BENV_ArrayComputeOffset - Compute the offset to an array element

Input Parameters:
+ nl - Number of dimensions
. coords - Coordinates of element
. sizes - Values in 'coords'[i] are between 0 and 'sizes[i]-1'
- order - Order of coordinates. Currently ignored, but could change the order.
 C order for now.

Return value:
The offset of the designated coordinates in the array

Notes:
Computes this linearized offest into a multidimensional array, given the
array dimensions in 'sizes' and the 0-origin indices in 'coords'. For example,
if 'sizes = [3,4,5]' and 'coords = [1,2,3]', then the computed offset is
'3+4*(2+3*(1))' or '23'.
  @*/
int BENV_ArrayComputeOffset(int nl, const int coords[], const int sizes[],
			    int order)
{
    int i, r = 0;
    for (i=0; i<nl; i++) {
	r = r*sizes[i] + coords[i];
    }
    return r;
}
