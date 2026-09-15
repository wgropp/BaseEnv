/*
 * Copyright (C) by University of Illinois 2022
 */
#include "benvconf.h"
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include "getsizes.h"
//#define DO_DEBUG 1
#include "benvdbg.h"

typedef enum { UNKNOWN, LIST, RANGE, RANGE_ADDITIVE, RANGE_MULT,
	       RANGE_MULT_DELTA } sizeval_t;

#define MAXLISTVALS 256
static void errSizeArg(const char *, const char *, const char *);
/*
 * Look through the arg list for -argname value, where value can be:
 *   a,b,...,c (comma separated list)
 *   a:b or a:b:c (range with additive stride)
 *   a:b*c (range with multiplicative stride)
 *   a:b*c+d:n (range with multiplicative stride, with arithmetic
 *              distribution around points: that is, take the values from
 *              a:b*c and add (-nd, -(n-1)d,... -d, d, ..., nd) points,
 *              staying within the interval [a,b]
 *
 * a and b make have postfix k or K for X 1024, m or M for 1024*1024.
 * On error, nsizes == -1 and return is NULL.
 * User should free(ptr) the value returned by this routine.
 *
 * Enhancement: make it possible to include either range type within
 * a list, e.g., 2,4,7:23,32. To implement this, a list element can
 * be either a number or a range (e.g., 4:xx[optional stride])
 *
 */

#define KIBI 1024
#define MEBI (KIBI*KIBI)

/* QUERY: extract just the parse arg, return the values of a,b,c,d,n
   and the type; if list, return the list val */

/*@ BENV_RstringToArrayOLD - Return an array of integers from a string describing
  a range of integers

 Input Parameters:
. rstring - String containing a list of ranges (see below)

 Output Parameter:
. nsizes - number of values returned

 Return value:
 Pointer to an array, allocated with 'malloc', containing the integers
 On error, 'nsizes == -1' and the return value is 'NULL'.
 The user should 'free(ptr)' where 'ptr' is the value returned by this routine.

 Notes:
This routine looks a string representing a list, where
where 'list' may be either a comma-separated list of values or a range with
an optional stride.  Specifically,
.vb
   a,b,...,c (comma separated list)
   a:b or a:b:c (range with additive stride)
   a:b*c (range with multiplicative stride)
   a:b*c+d:n (range with multiplicative stride, with arithmetic
              distribution around points: that is, take the values from
              a:b*c and add (-nd, -(n-1)d,... -d, d, ..., nd) points,
              staying within the interval [a,b]
.ve
Here, 'a' and 'b' may have postfix 'k' or 'K' for times 1024 and 'm' or 'M'
for times 1024*1024.
  @*/
int *BENV_RstringToArrayOLD(char *rstring, int *nsizes)
{
    sizeval_t sizetype;
    int       j;
    char      *p;
    int       start, end, stride=1;
    int       ival, err;
    int       delta=0, ndelta=0; /* for the arithmetic around multiplicative
				    strides */
    double    fval, fstride;
    int       nvals, listvals[MAXLISTVALS], *sptr;

    *nsizes = -1;

    p = rstring;
    sizetype = UNKNOWN;
    nvals    = 0;
    while (*p) {
	ival = BENV_ScanScaledInt((const char **)&p, 1, &err);
	if (err) {
	    errSizeArg("scaled int", NULL, rstring);
	    return 0;
	}
	fval = ival;
	if (sizetype == RANGE_MULT && *p == '.') {
	    /* Handle the case of a decimal stride for the multiplicative
	       fraction. For best precision, use strtod, but this
	       is good enough for the need here. */
	    double frac = 1e-1;
	    p++;
	    while (*p && isdigit(*p)) {
		fval += (*p - '0') * frac;
		frac *= 1e-1;
		p++;
	    }
	    /* No postfix (kKmM) for stride */
	}

	switch (sizetype) {
	case UNKNOWN:
	    if (!*p || *p == ',') {
		if (nvals >= MAXLISTVALS) {
		    errSizeArg("list", NULL, rstring);
		    return 0;
		}
		sizetype = LIST; listvals[nvals++] = ival;
		}
	    else if (*p == ':') {
		sizetype = RANGE;
		start    = ival;
	    }
	    else {
		errSizeArg("size", NULL, rstring);
		return 0;
	    }
	    if (*p) p++;
	    break;
	case LIST:
	    if (!*p || *p == ',') {
		if (nvals >= MAXLISTVALS) {
		    errSizeArg("list", NULL, rstring);
		    return 0;
		}
		listvals[nvals++] = ival;
	    }
	    else {
		errSizeArg("list", NULL, rstring);
		return 0;
	    }
	    if (*p) p++;
	    break;
	case RANGE:
	    if (!*p || *p == ':') {
		sizetype = RANGE_ADDITIVE;
		end = ival;
	    }
	    else if (*p == '*') {
		sizetype = RANGE_MULT;
		end = ival;
	    }
	    else {
		errSizeArg("range", NULL, rstring);
		return 0;
	    }
	    if (*p) p++;
	    break;
	case RANGE_ADDITIVE:
	    if (!*p) {
		stride = ival;
	    }
	    else {
		errSizeArg("range", NULL, rstring);
		return 0;
	    }
	    break;
	case RANGE_MULT:
	    if (*p == '+') {
		fstride  = fval;
		p++;
		sizetype = RANGE_MULT_DELTA;
	    }
	    else if (!*p) {
		fstride = fval;
	    }
	    else {
		errSizeArg("range", NULL, rstring);
		return 0;
	    }
	    break;
	case RANGE_MULT_DELTA:
	    delta   = ival;
	    if (*p != ':') {
		errSizeArg("range", NULL, rstring);
		return 0;
	    }
	    else {
		p++;
		ndelta = BENV_ScanScaledInt((const char **)&p, 1, &err);
	    }
	} /* switch */
    } /* while *p */

    switch (sizetype) {
    case LIST:
	*nsizes = nvals;
	sptr = (int *)malloc(nvals * sizeof(int));
	if (!sptr) {
	    fprintf(stderr, "Could not allocate %d words for size %s\n",
		    nvals, rstring);
	    return 0;
	}
	for (j=0; j<nvals; j++) sptr[j] = listvals[j];
	break;
    case RANGE:
    case RANGE_ADDITIVE:
	sptr = BENV_GetSizesArith(start, end, stride, nsizes);
	if (!sptr) {
	    fprintf(stderr, "Could not allocate %d words for size %s\n",
		    nvals, rstring);
	    return 0;
	}
	break;
    case RANGE_MULT:
	sptr = BENV_GetSizesMult(start, end, fstride, nsizes);
	if (!sptr) {
	    fprintf(stderr, "Could not allocate %d words for size %s\n",
		    nvals, rstring);
	    return 0;
	}
	break;
    case RANGE_MULT_DELTA:
	sptr = BENV_GetSizesMultDelta(start, end, fstride, delta, ndelta,
				      nsizes);
	if (!sptr) {
	    fprintf(stderr, "Could not allocate %d words for size %s\n",
		    nvals, rstring);
	    return 0;
	}
	break;
    default:
	fprintf(stderr, "Malformed size argument %s\n", rstring);
	return 0;
    }
    return sptr;
}

/*@ BENV_GetSizes - Return an array of integers from the argument list

 Input Parameters:
+ argc - argument count
- argname - argument name

Input/Output Parameter:
. argv - argument vector on input.  If 'argname' is found, the values
 of argv for that argument and the following argument are set to null.

 Output Parameter:
. nsizes - number of values returned

Return Value:
 Pointer to an array, allocated with 'malloc', containing the integers
 On error, 'nsizes == -1' and the return value is 'NULL'.
 The user should 'free(ptr)' where 'ptr' is the value returned by this routine.

 Notes:
This routine looks for a pair of arguments of the form
.vb
      ... --argname list
.ve
where 'list' may be either a comma-separated list of values or a range with
an optional stride.  Specifically,
.vb
   a,b,...,c (comma separated list)
   a:b or a:b:c (range with additive stride)
   a:b*c (range with multiplicative stride)
   a:b*c+d:n (range with multiplicative stride, with arithmetic
              distribution around points: that is, take the values from
              a:b*c and add (-nd, -(n-1)d,... -d, d, ..., nd) points,
              staying within the interval [a,b]
.ve
Here, 'a' and 'b' may have postfix 'k' or 'K' for times 1024 and 'm' or 'M'
for time 1024*1024.

This routine sets found arguments to null to permit other routines to process
other arguments; this requires all routines to permit a 'NULL' argument for
a value in 'argv'.
  @*/
int *BENV_GetSizes(int argc, char *argv[], const char *argname, int *nsizes)
{
    int       i;
    char      *p;
    int       *sptr;

    *nsizes = -1;

    for (i=1; i<argc; i++) {
	p = argv[i];
	if (!p || !*p) continue;
	if (*p++ != '-') continue;
	if (*p == '-') p++;
	if (strcmp(p, argname) != 0) continue;
	/* Found the argument.  Process it and exit */
	sptr      = BENV_RstringToArray(argv[i+1], nsizes);
	argv[i]   = 0;
	argv[i+1] = 0;
	return sptr;
    }

    return 0;
}

/*
 * Routines to generate size arrays, given start, end, and increment
 * information.  Useful for providing a default is no size option provided.
 */

/*@ BENV_GetSizesArith - Return an array with an arithmetric progression of values

Input Parameters:
+ start - First element in the progression
. end - Last element. Actual last value may be less than 'end' depending on the value of 'stride'
- stride - Values are 'start', 'start'+'stride', 'start'+2*'stride', ...

Output Parameter:
. nsizes - Number of values in the returned array

Return Value:
 Pointer to an array, allocated with 'malloc', containing the integers
 On error, 'nsizes == -1' and the return value is 'NULL'.
 The user should 'free(ptr)' where 'ptr' is the value returned by this routine.
  @*/
int *BENV_GetSizesArith(int start, int end, int stride, int *nsizes)
{
    int nvals, *sptr, j, ival;
    nvals = 1 + (end - start) / stride;
    sptr = (int *)malloc(nvals * sizeof(int));
    if (!sptr) return 0;

    ival = start;
    for (j=0; j<nvals; j++) {
	sptr[j] = ival;
	ival += stride;
    }
    *nsizes = nvals;
    return sptr;
}

/*@ BENV_GetSizesMult - Return an array with an geometric progression of values

Input Parameters:
+ start - First element in the progression
. end - Last element. Actual last value may be less than 'end' depending on the value of 'stride'
- factor - Values are 'start', 'start'*'factor', 'start'*'factor'*'factor', ...

Output Parameter:
. nsizes - Number of values in the returned array

Return Value:
 Pointer to an array, allocated with 'malloc', containing the integers
 On error, 'nsizes == -1' and the return value is 'NULL'.
 The user should 'free(ptr)' where 'ptr' is the value returned by this routine.
  @*/
int *BENV_GetSizesMult(int start, int end, double factor, int *nsizes)
{
    int nvals, *sptr, ival;

    nvals = 1;
    ival = start;
    if (factor <= 1) {
	fprintf(stderr, "Multiplicative range requires factor > 1\n");
	return 0;
    }
    if ((int)(ival * factor) == ival) {
	fprintf(stderr,
		"Multiplicative factor must satisfy factor*start>=start+1\n");
	return 0;
    }

    /* Determine number of elements */
    while (ival <= end) { ival *= factor; nvals++; }

    sptr = (int *)malloc(nvals * sizeof(int));
    if (!sptr) return 0;

    ival  = start;
    nvals = 0;
    while (ival <= end) {
	sptr[nvals++] = ival;
	ival *= factor;
    }
    *nsizes = nvals;
    return sptr;
}

/*@ BENV_GetSizesMultDelta - Return an array with an geometric progression of values

Input Parameters:
+ start - First element in the progression
. end - Last element. Actual last value may be less than 'end' depending on the value of 'stride'
. factor - Values are 'start', 'start'*'factor', 'start'*'factor'*'factor', ... , with additional values given by delta (see below)
. delta - Add values that are separated by 'delta' (see below)
- ndelta - Number of values to add around each value (see below)

Output Parameter:
. nsizes - Number of values in the returned array

Return Value:
 Pointer to an array, allocated with 'malloc', containing the integers
 On error, 'nsizes == -1' and the return value is 'NULL'.
 The user should 'free(ptr)' where 'ptr' is the value returned by this routine.

 The values returned are 'start', 'start' + 'delta', 'start' + 2*'delta', ...,
 'start' + 'ndelta'*'delta', 'start'*'factor' - 'ndelta'*'delta', 'start'*'factor' - ('ndelta'-1)*'delta', ..., 'start'*'factor', 'start'*'factor' + 'delta', ..., 'start'*'factor' + 'ndelta'*'delta', 'start'*'factor'*'factor' - 'ndelta'*'delta', ... . That is create the geometric series with 'start' and 'factor' up to 'end', and then add 2*'ndelta' elements, separated arithmetically arouncd each of those points.
  @*/
int *BENV_GetSizesMultDelta(int start, int end, double factor, int delta,
			    int ndelta, int *nsizes)
{
    int nvals, *sptr, ival;

    nvals = 1;
    ival = start;
    if (factor <= 1) {
	fprintf(stderr, "Multiplicative range requires factor > 1\n");
	return 0;
    }
    if ((int)(ival * factor) == ival) {
	fprintf(stderr,
		"Multiplicative factor must satisfy factor*start>=start+1\n");
	return 0;
    }
    if (ndelta < 0 && delta < 0) {
	fprintf(stderr, "Arithmetic delta and count must be positive\n");
	return 0;
    }

    /* Determine number of elements.  This is a slight overestimate,
       because at a and b, there are only ndelta instead of 2*ndelta;
       values that are less than the current top value are also ignored. */
    while (ival <= end) { ival *= factor; nvals += 2*ndelta+1; }

    sptr = (int *)malloc(nvals * sizeof(int));
    if (!sptr) return 0;

    /* Add the initial value to simplify the tests */
    nvals         = 0;
    sptr[nvals++] = start;
    ival          = start;
    while (ival <= end) {
	int nd, iival;
	iival = ival-ndelta*delta;
	for (nd=-ndelta; nd<=ndelta; nd++) {
	    if (iival >= start && iival <= end) {
		/* some values might for an insert not at the end */
		if (iival > sptr[nvals-1])
		    sptr[nvals++] = iival;
		else {
		    int ll = nvals-1;
		    while (ll > 0 && sptr[ll] > iival) ll--;
		    /* Check for a duplicate */
		    if (sptr[ll] != iival) {
			/* Make room in the list */
			for (int idx=nvals; idx>ll; idx--)
			    sptr[idx+1] = sptr[idx];
			nvals++;
			sptr[ll+1] = iival;
		    }
		}
	    }
	    iival += delta;
	}
	ival *= factor;
    }
    *nsizes = nvals;
    return sptr;
}

#if 0
/*
 * Scan an int, which may be followed by k,K,m, or M, which are 1024 or 1024^2
 * Update the passed argument to point to the next character.
 */
static int scanInt(const char **str)
{
    int ival = 0;
    const char *p = *str;

    while (*p && isdigit(*p)) {
	ival = (*p - '0') + 10*ival;   /* Assumes ASCII */
	p++;
    }
    if (*p) {
	if (*p == 'k' || *p == 'K') {
	    ival *= KIBI;
	    p++;
	}
	if (*p == 'm' || *p == 'M') {
	    ival *= MEBI;
	    p++;
	}
	/* Allow "i" or "I" to follow (but ignore) */
	if (*p == 'i' || *p == 'I') p++;
    }
    *str = p;
    return ival;
}
#endif

/* Issue an error message for a malformed range string. a1 may be null,
   if non-null, a typical value is the command-line argument name */
static void errSizeArg(const char *nm, const char *a1, const char *a2)
{
    if (a1)
	fprintf(stderr, "Malformed %s argument %s %s\n", nm, a1, a2);
    else
	fprintf(stderr, "Malformed %s argument %s\n", nm, a2);
}


/* Temp - new get sizes */
typedef enum { STATE_READINT, STATE_CHECKRANGE, STATE_CHECKSTRIDE,
	       STATE_READFLOAT, STATE_CHECKSEP, STATE_CHECKSPLIT,
	       STATE_READSPLIT } parsestate_t;
typedef enum { LIST_UNKNOWN, LIST_ELEMENT, LIST_ARITH, LIST_MULT,
	       LIST_MULT_DELTA } numlist_t;

/* If meganotmebi, then 1k = 1000, not 1024 (1ki). Because many often
   assume that k = 1024 and m = 1024*1024, this is selectable. The default
   is to keep these distinct */
#if 0
static int meganotmebi=1;
#endif

/*@ BENV_ScanScaledInt - Read an int from a string, which may be follwed a modifier for k or m

Input parameter:
. usepwr2 - If true, modifiers always imply power of 2 (e.g., 1024 instead of
 1000 for k)

Input/output parameter:
. str - pointer the the string on imput. On output, pointer to the next
GA character after those processed

Output parameter:
. err - 0 on success, non-zero on failure

Return value:
Returns the integer value of the scanned string

Notes:
Scan an int, which may be followed by a scaling. Valid scalings are
.vb
   k or K             1000
   ki, Ki, kI, or KI  1024
   m or M             1000000
   mi, Mi, mI, or MI  1048576   (=1024*1024)
.ve
If the argument 'usepwr2' is true, then the scaling is 1024 for k or K and
1048576 for m or M. Update the passed argument to point to the next character.
 @*/
int BENV_ScanScaledInt(const char **str, int usepwr2, int *err)
{
    int ival = 0;
    const char *p = *str;

    while (*p && isdigit(*p)) {
	ival = (*p - '0') + 10*ival;   /* Assumes ASCII */
	p++;
    }
    if (*p) {
	if (*p == 'k' || *p == 'K') {
	    p++;
	    /* Allow "i" or "I" to follow */
	    if (*p == 'i' || *p == 'I' ||  usepwr2) {
		ival *= KIBI;
		p++;
	    }
	    else {
		ival *= 1000;
	    }
	}
	if (*p == 'm' || *p == 'M') {
	    p++;
	    /* Allow "i" or "I" to follow */
	    if (*p == 'i' || *p == 'I' || usepwr2) {
		ival *= MEBI;
		p++;
	    }
	    else {
		ival *= 1000000;
	    }
	}
    }
    /* Return error if no digits seen */
    *err = (p == *str);
    *str = p;
    return ival;
}

static double scanFloat(const char **str)
{
    double fval, frac;
    const char *p = *str;
    int   ival, err;

    ival = BENV_ScanScaledInt(&p, 0, &err);
    fval = (double)ival;   /* gcc will warn if fval assigned with cast
			      from function return, even though no
			      information is lost */
    if (err) return 0.0;   /* Could pass err back to caller */
    if (*p == '.') {
	p++;
	frac = 1e-1;
	while (*p && isdigit(*p)) {
	    fval += (*p - '0') * frac;  /* Assumes ASCII */
	    frac *= 1e-1;
	    p++;
	}
    }
    *str = p;
    return fval;
}

/* return the concatenation of two lists of integers
 */
static int *concatLists(const int *l1, int n1, const int *l2, int n2, int *n3)
{
    int nn = n1 + n2;
    int *nnew;

    nnew = (int *)realloc((int *)l1, nn*sizeof(int));
    for (int i=0; i<n2; i++) {
	nnew[n1+i] = l2[i];
    }
    free((int*)l2);
    *n3 = nn;
    return nnew;
}

/*
 * Here's the grammar:
 *    LIST -> LISTITEM [, LISTITEM]
 *
 * General form of an entry is (with state)
 *   read             nextstate
 *   ----             ---------
 *                    readint     (must be int, unless null)
 *   int              checkrange  (options: sep, :)
 *   int:int          checkstride (three options: sep, :, *)(set arith/mult)
 *     int:int:       readint     (must be int)
 *     int:int*       readfloat   (must be float or int(
 *   int:int:int      checksep    (must be sep)
 *   int:int*float    checksplit  (options: sep, +)
 *   int:int*float+int:int readsplit (must be int:int)
 *
 *   float is just int[.int]
 *
 *   at checksep, if sep found, then process list (using saved values)
 *   and reset state to readint.
 */

/*@ BENV_RstringToArray - Return an array of integers from a string describing
  a range of integers

 Input Parameters:
. rstring - String containing a list of ranges (see below)

 Output Parameter:
. nsizes - number of values returned

 Return value:
 Pointer to an array, allocated with 'malloc', containing the integers
 On error, 'nsizes == -1' and the return value is 'NULL'.
 The user should 'free(ptr)' where 'ptr' is the value returned by this routine.

 Notes:
This routine looks a string representing a list, where
where 'list' may be either a comma-separated list of values or a range with
an optional stride.  Specifically,
.vb
   a,b,...,c (comma separated list)
   a:b or a:b:c (range with additive stride)
   a:b*c (range with multiplicative stride)
   a:b*c+d:n (range with multiplicative stride, with arithmetic
              distribution around points: that is, take the values from
              a:b*c and add (-nd, -(n-1)d,... -d, d, ..., nd) points,
              staying within the interval [a,b]
.ve
Here, 'a' and 'b' may have postfix 'k' or 'K' for times 1024 and 'm' or 'M'
for times 1024*1024. The string may contain a comma-separated list of 'list's,
as in 'a:b,c,d,e:f*g' .
  @*/
int *BENV_RstringToArray(const char *rstring, int *nsizes)
{
    parsestate_t pstate;
    numlist_t    ltype;
    int          listvals[4], listvalidx;
    double       fstride;
    int          createList;
    int          *list, listlen;
    const char   *p;

    *nsizes = -1;

    p          = rstring;
    listvalidx = 0;
    createList = 0;
    pstate     = STATE_READINT;
    ltype      = LIST_UNKNOWN;
    list       = 0;
    listlen    = 0;

    while (p && *p) {
	int err;
	DBGCALL(printf("pstate = %d, ltype = %d, remaining string %s\n",
		       pstate, ltype, p));
	switch (pstate) {
	case STATE_READINT: /* Read int, add to next listval */
	    listvals[listvalidx++] = BENV_ScanScaledInt(&p, 1, &err);
	    if (err) {
		errSizeArg("scaled int", NULL, rstring);
		goto errexit;
	    }
	    switch (listvalidx) {
	    case 1: pstate = STATE_CHECKRANGE;  ltype = LIST_ELEMENT; break;
	    case 2: pstate = STATE_CHECKSTRIDE; ltype = LIST_ARITH; break;
	    case 3: pstate = STATE_CHECKSEP;    break;
		/* case 3: Don't create the list until the sep is seen */
	    default: errSizeArg("parse error", NULL, rstring); break;
	    }
	    if (!*p) createList = 1;
	    break;
	case STATE_CHECKRANGE: /* look for sep, : */
	    if (*p == ':') {
		p++;
		pstate = STATE_READINT;
	    }
	    else if (*p == ',' || !*p) {
		if (*p) p++;
		pstate = STATE_READINT;
		createList = 1;
	    }
	    else {
		errSizeArg("list (checkrange)", NULL, rstring);
		goto errexit;
	    }
	    break;
	case STATE_CHECKSTRIDE: /* look for sep, :, * */
	    DBGCALL(printf("Checkstride: %s\n", p));
	    if (*p == ':') {
		p++;
		pstate = STATE_READINT;
	    }
	    else if (*p == '*') {
		p++;
		ltype  = LIST_MULT;
		pstate = STATE_READFLOAT;
	    }
	    else if (*p == ',' || !*p) {
		if (*p) p++;
		createList = 1;
		pstate = STATE_READINT;
	    }
	    else {
		DBGCALL(printf("p = %s\n", p));
		errSizeArg("range (stride)", NULL, rstring);
		goto errexit;
	    }
	    break;
	case STATE_READFLOAT: /* float (or int) for mult stride */
	    fstride  = scanFloat(&p);
	    DBGCALL(printf("read float: %.2e\n", fstride));
	    if (!*p) createList = 1;
	    pstate   = STATE_CHECKSPLIT;
	    break;
	case STATE_CHECKSEP:  /* must be sep at end of a list item */
	    if (*p != ',' && !*p) {
		errSizeArg("list", NULL, rstring);
		goto errexit;
	    }
	    if (*p) p++;
	    pstate = STATE_READINT;
	    createList = 1;
	    break;
	case STATE_CHECKSPLIT: /* look for sep (end of mult stride) or + */
	    if (*p == '+') {
		p++;
		pstate = STATE_READSPLIT;
		ltype  = LIST_MULT_DELTA;
	    }
	    else if (*p == ',' || !*p) {
		if (*p) p++;
		createList = 1;
		pstate = STATE_READINT;
	    }
	    else {
		errSizeArg("mult range", NULL, rstring);
		goto errexit;
	    }
	    break;
	case STATE_READSPLIT:  /* must be int:int */
	    listvals[listvalidx++] = BENV_ScanScaledInt(&p, 1, &err);
	    if (*p == ':') {
		p++;
		listvals[listvalidx++] = BENV_ScanScaledInt(&p, 1, &err);
	    }
	    else {
		errSizeArg("mult range", NULL, rstring);
		goto errexit;
	    }
	    if (!*p) createList = 1;
	    pstate = STATE_CHECKSEP;
	    break;
	}
	if (createList) {
	    int start = listvals[0];
	    int end   = listvals[1];
	    int stride = 1;
	    int *sptr;

	    DBGCALL(printf("Create list type %d (%d:%d)\n", ltype, start, end));
	    switch (ltype) {
	    case LIST_ELEMENT:  /* start == end in this case */
		end = start;
		/* Could fall through to LIST_ARITH but gcc complains */
		sptr = BENV_GetSizesArith(start, end, 1, nsizes);
		break;
	    case LIST_ARITH:
		if (listvalidx > 2) stride = listvals[2];
		/* how many values do we have? */
		sptr = BENV_GetSizesArith(start, end, stride, nsizes);
		break;
	    case LIST_MULT:
		sptr = BENV_GetSizesMult(start, end, fstride, nsizes);
		break;
	    case LIST_MULT_DELTA:
		if (listvalidx != 4) {
		    errSizeArg("mult", NULL, rstring);
		    goto errexit;
		}
		else {
		    sptr = BENV_GetSizesMultDelta(start, end, fstride,
						  listvals[2], listvals[3],
						  nsizes);
		}
		break;
	    case LIST_UNKNOWN:
		errSizeArg("list", NULL, rstring);
		goto errexit;
	    }
	    if (!sptr) {
		fprintf(stderr, "Could not allocate %d words for size %s\n",
			*nsizes, rstring);
		goto errexit;
	    }
	    else {
		DBGCALL(printf("ConcatList with len1=%d, len2=%d\n",
			       listlen, *nsizes));
		list = concatLists(list, listlen, sptr, *nsizes, &listlen);
		DBGCALL(printf("Concatlist, new size %d\n", listlen));
	    }

	    listvalidx = 0;
	    createList = 0;
	    ltype      = LIST_UNKNOWN;
	}
    }
    /* printf("End of reading arg %s\n", rstring); */
    *nsizes = listlen;
    return list;

errexit:
    if (list) free(list);
    return 0;
}
