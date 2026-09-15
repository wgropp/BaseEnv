#include <stdio.h>
#include <stdlib.h>
#include "benvconf.h"
#include "benvutil.h"
#include "benvdbg.h"

CDBGFCALLDECL;
CDBGDECL(ILIST);
CDBGEDECL(ARGV);

/* Compress lists of integers into compact representations, including copies
   and simple arithmetic lists.

   Format of compressed list:
   <term>[,<term]*
   <term> = <int>\*<baseterm>              n copies of a <baseterm>
            <baseterm>
   <baseterm> = <int>
              = <int>:<int>[:<int>]        start:end:optional stride

   For example, this list 0,2,3,4,5,2,3,4,5,2,3,4,5 can be compressed as
   0,3*(2:5)

   There is an important special case. If an int list is just n copies of
   the same value, we may want to print it as if it were a single value
   (rather than as n*val).

*/
/* Individual ints are an array of length 1 */
typedef enum { LIST_ARRAY, LIST_RANGE } listkind;

typedef struct compressedlist {
    listkind kind;
    int v1,v2,v3,ncopy;
    const int *vals;
    struct compressedlist *next;
} compressedlist;


/* Default number of elements that a range must contain */
static int rangeCountThreshold=4;

/*@
  BENV_UtilCompressIntList - Represent an integer list as a combination of ranges and lists

Input Parameters:
+ n - Number of elements in the integer list
. vec - Array of the integer list
- minrange - Minimum number of list elements that a range must represent

Return Value:
An 'intlistPtr', which may be used to create a character string of the list or
to print the list.

See also:
BENV_UtilIntListToStr
  @*/
intlistPtr BENV_UtilCompressIntList(int n, const int vec[], int minrange)
{
    intlistPtr head=0, tail=0, elm;
    int i, j;

    CDBGFCALLENTERV("n=%d, minrange=%d\n", n, minrange);
    /* Use default range threshold unless a valid limit provided */
    if (minrange < 1) minrange = rangeCountThreshold;

    if (n == 1) {
	/* Trivial intlist */
	elm = (compressedlist *)malloc(sizeof(compressedlist));
	if (!elm) { return 0;}
	elm->kind  = LIST_ARRAY;
	elm->v1    = 1;
	elm->v2    = 0;
	elm->v3    = 0;
	elm->vals  = &vec[0];
	elm->ncopy = 1;
	elm->next  = 0;
	CDBGFCALLEXIT;
	return elm;
    }

    /* Look for range (including copies) */
    i = 0;
    while (i < n) {
	int s, stride;
	/* Look for strided entries if there are enough entries left */
	j = i;
	if (n - i >= minrange) {
	    s = vec[i+1];
	    stride = s - vec[i];
	    for (j=i+2; j<n; j++) {
		if (s + stride != vec[j]) break;
		s = vec[j];
	    }
	}
	CDBGV(ILIST,DETAIL,"Possible stride i=%d, j=%d, stride=%d\n", i, j,
	      stride);
	/* Is range list long enough */
	if (j-i >= minrange) {
	    /* Use as a range */
	    /* First check whether this a copy of the most recent node */
	    if (tail && tail->kind == LIST_RANGE && tail->v1 == vec[i] &&
		tail->v2 == vec[j-1] && tail->v3 == stride) {
		/* Found a match */
		CDBGV(ILIST,DETAIL,"Found exact range %d,%d,%d\n", vec[i],
		      vec[j-1], stride);
		tail->ncopy++;
	    }
	    else {
		/* Might want to see if the tail element is a
		   list copy for the previous node first */
		/* Create a new element */
		elm = (compressedlist *)malloc(sizeof(compressedlist));
		if (!elm) {}
		if (tail)
		    tail->next = elm;
		else
		    head = elm;
		tail = elm;
		/* Two options: Range and copy (stride=0) */
		if (stride == 0) {
		    CDBGV(ILIST,DETAIL,"Adding %d copies of %d\n", j-i, vec[i]);
		    elm->kind  = LIST_ARRAY;
		    elm->v1    = 1;
		    elm->v2    = 0;
		    elm->v3    = 0;
		    elm->vals  = &vec[i];
		    elm->ncopy = j-i;
		    elm->next  = 0;
		}
		else {
		    CDBGV(ILIST,DETAIL,"Adding range %d:%d:%d\n",
			  vec[i], vec[j-1], stride);
		    elm->kind  = LIST_RANGE;
		    elm->v1    = vec[i];
		    elm->v2    = vec[j-1];
		    elm->v3    = stride;
		    elm->ncopy = 1;
		    elm->next  = 0;
		}
	    }
	    i = j;
	}
	else {
	    /* Not a stride. Add to the tail */
	    if (tail && tail->kind == LIST_ARRAY && tail->ncopy == 1) {
		/* Just add */
		CDBGV(ILIST,DETAIL,"Add element %d to array\n", vec[i]);
		tail->v1++;
	    }
	    else {
		/* New elm needed */
		elm = (compressedlist *)malloc(sizeof(compressedlist));
		if (!elm) {}
		if (tail)
		    tail->next = elm;
		else
		    head = elm;
		tail = elm;
		CDBGV(ILIST,DETAIL,"Add array starting with %d\n", vec[i]);
		elm->kind  = LIST_ARRAY;
		elm->v1    = 1;
		elm->v2    = 0;
		elm->v3    = 0;
		elm->vals  = &vec[i];
		elm->ncopy = 1;
		elm->next  = 0;
	    }
	    i++;
	}
    }
    CDBGFCALLEXITV("returning %p\n", head);
    return head;
}

/*@ BENV_UtilIntListToStr - Convert an integer list into a string

Input Parameter:
. lst - An inlistPtr, created by 'BENV_UtilCompressIntList'

Return Value:
A pointer to a string that represents the integer list. It has a
comma-separated list of\:
.n individual integers
.n ranges of the form n1:n2 or n1:n2:n3, which represent n1,n1+1,...,n2 or
 n1,n1+n3,n1+2*n3,...,n2 respectively
.n copies of the form n1*item, as in 2*1:3, which represents the integers
1,2,3,1,2,3
.n
The user is responsible for freeing the string using 'free'.

See also:
BENV_UtilCompressIntList
@*/
char *BENV_UtilIntListToStr(intlistPtr lst)
{
    int slen=0, i, rc, totlen;
    intlistPtr lp=lst;
    char *str=0, *p;

    CDBGFCALLENTER;
    /* Estimate size of string needed */
    while (lp) {
	if (lp->ncopy != 1) slen += 1 + 10;
	if (lp->kind == LIST_RANGE) {
	    if (lp->v3 == 1) slen += 1 + 2*10;
	    else             slen += 2 + 3*10;
	}
	else {
	    slen += lp->v1*(10+1); // Value + comma
	}
	slen++; // Add another comma
	lp = lp->next;
    }
    /* Allocate string */
    str = (char *)malloc((slen+1)*sizeof(char));
    if (!str) { return 0; };

    /* Convert int list into a string */
    p      = str;
    totlen = slen;
    CDBGV(ILIST,DETAIL,"Allocated %d characters for ilist string\n", slen);
    while (lst && totlen > 0) {
	if (lst->ncopy != 1) {
	    rc = snprintf(p, totlen, "%d*", lst->ncopy);
	    if (rc < 0) {
		fprintf(stderr, "sprintf of %%d* with value %d failed, rc=%d!\n",
			lst->ncopy, rc);
		goto fn_fail;
	    }
	    if (rc > 11) {
		fprintf(stderr, "value %d too long (%d)\n", lst->ncopy, rc);
	    }
	    p += rc;
	    totlen -= rc;
	    CDBGV(ILIST,ALL,"Added %d* to string\n", lst->ncopy);
	}
	if (lst->kind == LIST_RANGE) {
	    if (lst->v3 != 1) {
		CDBGV(ILIST,ALL,"Added %d:%d:%d to string\n",
		      lst->v1, lst->v2, lst->v3);
		rc = snprintf(p, totlen, "%d:%d:%d", lst->v1, lst->v2, lst->v3);
		if (rc > 32) {
		    fprintf(stderr, "value too long (%d)\n", rc);
		}
	    }
	    else {
		CDBGV(ILIST,ALL,"Added %d:%d to string\n",
		      lst->v1, lst->v2);
		rc = snprintf(p, totlen, "%d:%d", lst->v1, lst->v2);
		if (rc > 21) {
		    fprintf(stderr, "value too long (%d)\n", rc);
		}
	    }
	    if (rc < 0) {
		fprintf(stderr, "snprint or range failed with rc=%d\n", rc);
		goto fn_fail;
	    }
	    p += rc;
	    totlen -= rc;
	}
	else if (lst->kind == LIST_ARRAY) {
	    CDBGV(ILIST,ALL,"Adding array to string of length %d\n",
		  lst->v1);
	    rc = snprintf(p, totlen, "%d", lst->vals[0]);
	    if (rc > 10) {
		fprintf(stderr, "value %d too long (%d)\n", lst->vals[0], rc);
	    }
	    if (rc < 0) {
		fprintf(stderr, "snprintf of array values failed with rc=%d\n", rc);
		goto fn_fail;
	    }
	    p += rc;
	    totlen -= rc;
	    for (i=1; i<lst->v1; i++) {
		rc = snprintf(p, totlen, ",%d", lst->vals[i]);
		if (rc > 10) {
		    fprintf(stderr, "value %d too long (%d)\n", lst->vals[0], rc);
		}
		if (rc < 0) {
		    fprintf(stderr, "snprintf of array values failed with rc=%d\n", rc);
		    goto fn_fail;
		}
		p += rc;
		totlen -= rc;
	    }
	}
	else {
	    fprintf(stderr, "Panic! unrecognized list type %d!\n", lst->kind);
	    goto fn_fail;
	}
	lst = lst->next;
	if (lst) {*p++ = ','; totlen--; }
    }
    if (totlen < 0) {
	fprintf(stderr, "Panic! string length too long! slen=%d, totlen now=%d\n",
		slen, totlen);
	goto fn_fail;
    }
    CDBGFCALLEXIT;
    return str;
fn_fail:
    CDBGFCALLEXIT;
    return 0;
}

#if 0
void printcompressedlist(FILE *fp, intlistPtr lst)
{
#if 1
    char *str = BENV_UtilIntListToStr(lst);
    if (str) {
	fputs(str,fp);
	free(str);
    }
#else
    int i;
    while (lst) {
	if (lst->ncopy != 1) {
	    fprintf(fp, "%d*", lst->ncopy);
	}
	if (lst->kind == LIST_RANGE) {
	    if (lst->v3 != 1) {
		fprintf(fp, "%d:%d:%d", lst->v1, lst->v2, lst->v3);
	    }
	    else {
		fprintf(fp, "%d:%d", lst->v1, lst->v2);
	    }
	}
	else if (lst->kind == LIST_ARRAY) {
	    fprintf(fp, "%d", lst->vals[0]);
	    for (i=1; i<lst->v1; i++) {
		fprintf(fp, ",%d", lst->vals[i]);
	    }
	}
	else {
	    fprintf(stderr, "Panic! unrecognized list type!\n");
	}
	lst = lst->next;
	if (lst) fputc(',',fp);
    }
#endif
}
#endif

/*@
  BENV_UtilFreeIntList - Free an intlist object

Input Parameter:
. lst - An integer list created by 'BENV_UtilCompressIntList'

.N returnvalue
  @*/
int BENV_UtilFreeIntList(intlistPtr lst)
{
    intlistPtr nxt;

    while (lst) {
	nxt = lst->next;
	free(lst);
	lst = nxt;
    }
    return 0;
}

#if 0
/* TEMP for debugging */
void ilistprint(FILE *fp, intlistPtr lst)
{
    if (!lst) {
	fprintf(fp, "lst is NULL!\n");
    }
    else {
	fprintf(fp, "kind = %d, v1,v2,v3,ncopy = %d,%d,%d,%d\n",
		lst->kind, lst->v1, lst->v2, lst->v3, lst->ncopy);
    }
}
#endif

/*@ BENV_UtilIntListIsSimple - Determine if an intlist has only a single value

Input Parameter:
. lst - An intlist

Output Parameters:
+ val - If the lst is simple, the integer value
- n - If the lst is simple, the number of copies of the value in the intlist

Return value:
Returns 0 if the intlist is simple (n copies of a single value) and 1 otherwise
@*/
int BENV_UtilIntListIsSimple(intlistPtr lst, int *val, int *n)
{
    if (lst && lst->next == 0 && lst->kind == LIST_ARRAY && lst->v1 == 1) {
	*val = lst->vals[0];
	*n   = lst->ncopy;
	return 0;
    }
    return 1;
}

/*@ BENV_UtilIntListArgDebug - Look for debug options for Int List routines

Input Parameters:
+ argc - Argument count
. argv - Argument vector
- prefix - Arguments have this prefix; may be null. See below

Input/Output Parameter:
. argcnt - pointer to the index of the current argument. Will be updated
 if an hwdesc parameter is found by the number of values read, not counting
 the argument itself.

Return Value:
Returns 1 if a known argument value is found, zero otherwise.

Notes:
Recognizes the debug class 'ilist'. Recognizes
'-debugclass' as the argument name. Currently, the prefix is ignored.
The class may be followed with ':b', ':d', or ':a' for basic, detail, or all
debug information respectively.

Other packages that use this package, such as hwdesc, may call this routine
as part of their argument processing.

See also:
BENV_DebugArgClass, BENV_DebugArgRank
  @*/
int BENV_UtilIntListArgDebug(int argc, char **argv, int *argcnt,
			     const char *prefix)
{
    int rc=0;
    static const char *classes[] = { "ilist", };
    static int *classval[] = { &cvar_benv_ILIST_verbose, };

    CDBGV(ARGV,BASIC,"Starting UtilIntListArgDebug with prefix %s and next arg %s\n",
	  prefix ? prefix : "<NULL>", argv[*argcnt]);
     /* FIXME: Ignore prefix or allow but not require? */
    /* Debug arg rank is not included so these routines can work without MPI */
    rc = BENV_DebugArgClass(argc, argv, argcnt, 1, classes, classval);
    if (rc == -1) {
	/* DebugArgClass returns -1 if class not recognized. Ignore */
	rc = 0;
    }

    CDBGV(ARGV,BASIC,"Ending UtilIntListArgDebug rc=%d\n", rc);
    return rc;
}

/* TODO: Make it easy to append to an integer array and then to convert to an int list.
   This is useful in debugging the hwdesc code

   Add manpage comments
 */

typedef struct intarray {
    int nalloc, nlen;
    int *vals; } intarray;

/*@ BENV_UtilCreateIntArray - Create a variable length array of integers

Input Parameter:
. nalloc - Number of entries to preallocate

Return value:
A pointer to an empty intarray. Null if there is an error, such as no memory.
  @*/
intarrayPtr BENV_UtilCreateIntArray(int nalloc)
{
    intarray *narr;

    narr = (intarray *)malloc(sizeof(intarray));
    if (!narr) return 0;
    if (nalloc > 0) {
	narr->vals = (int *)malloc(nalloc*sizeof(int));
    }
    narr->nalloc = nalloc;
    narr->nlen   = 0;

    return narr;
}

/*@ BENV_UtilAppendIntArray - Append a value to an integer array

Input Parameters:
+ iarr - intarray
- val - Value to append

.N returnvalue
@*/
int BENV_UtilAppendIntArray(intarrayPtr iarr, int val)
{
    if (iarr->nlen == iarr->nalloc) {
	iarr->nalloc += 16;
	iarr->vals = (int *)realloc(iarr->vals, iarr->nalloc * sizeof(int));
	if (!iarr->vals) return -1;
    }
    iarr->vals[iarr->nlen++] = val;
    return 0;
}

/*@ BENV_UtilIntArrayToIntList - Return an intlist from and intarray

Input Parameter:
. iarr - intarray

Return Value:
Returns a compressed intlist created from the integer array 'iarr'
@*/
intlistPtr BENV_UtilIntArrayToIntList(intarrayPtr iarr)
{
    return BENV_UtilCompressIntList(iarr->nlen, iarr->vals, 0);
}

/*@ BENV_UtilFreeIntArray - Free an int array

Input Parameter:
. iarr - int array created with 'BENV_UtilCreateIntArray'
@*/
void BENV_UtilFreeIntArray(intarrayPtr iarr)
{
    if (iarr->vals) free(iarr->vals);
    free(iarr);
}
