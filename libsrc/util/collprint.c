/* Collective Print routines.
   These check for common strings and for patterns in integers, combining
   output into simpler, more consise output (with an option to not combine
   an not operate collectively).
*/
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "mpi.h"
#include "benvconf.h"
#include "benvutil.h"
#include "benvdbg.h"
#include "benvmpiutil.h"

CDBGFCALLDECL;

/* TEMP for debugging */
//void ilistprint(FILE *fp, intlistPtr lst);

/* All routines are collective. */

int BENV_CollPrintStr(FILE*fp, MPI_Comm comm, int croot, const char *str)
{
    int crank, rc;

    CDBGFCALLENTER;
    MPI_Comm_rank(comm, &crank);
    /* Compute a hash of the string to check */
    /* Source: https://github.com/aappleby/smhasher/blob/master/src/Hashes.cpp
       see also
       https://stackoverflow.com/questions/7666509/hash-function-for-string
     */
    unsigned int h=0;  /* Prefer uint32_t */
    const char *p=str;
    for (; *p; ++p) {
        h ^= *p;
        h *= 0x5bd1e995;
        h ^= h >> 15;
    }
    rc = BENV_CheckSameInts(comm, &h, 1);
    if (rc != 0) {
	// strings are not the same on all processes.
	/* FIXME: Split comm on hash, store as attribute. Newline will
	   free that new communicator */
	fprintf(stderr, "%s: different strings!\n", __func__);
    }

    /* If the hash values are not identical, split the communicator */

    if (crank == croot) {
	fputs(str, fp);
    }

    CDBGFCALLEXIT;
    return 0;
}

int BENV_CollPrintInt(FILE *fp, MPI_Comm comm, int croot, int v)
{
    int *varr, crank, csize, rc, val, nc;

    CDBGFCALLENTER;
    MPI_Comm_rank(comm, &crank);
    MPI_Comm_size(comm, &csize);
    /* Gather the int to the root of comm, compress, and print */
    if (crank == croot) {
	varr = (int *)malloc(csize * sizeof(int));
	if (!varr) {return 0;}
    }
    else varr = 0;
//    printf("Gathering data...\n"); fflush(stdout);
    MPI_Gather(&v, 1, MPI_INT, varr, 1, MPI_INT, croot, comm);
//    printf("Gathered data\n"); fflush(stdout);
    if (crank == croot) {
	intlistPtr ilist;
	char *lstr;
//	printf("About to create intlist with %d ints starting with %d\n",
//	       csize, varr[0]); fflush(stdout);
	ilist = BENV_UtilCompressIntList(csize, varr, 0);
//	printf("ilist is: ");
//	ilistprint(stdout, ilist);
//	fflush(stdout);
	/* Check for simple list */
	rc = BENV_UtilIntListIsSimple(ilist, &val, &nc);
	if (rc == 0 && nc == csize) {
	    fprintf(fp, "%d", val);
	}
	else {
	    lstr = BENV_UtilIntListToStr(ilist);
	    if (lstr) {
		fputs(lstr, fp);
		free(lstr);
	    }
	    else {
		fprintf(stderr, "IntList returned null string for %d values:",
			csize);
		for (int j=0; j<csize; j++) {
		    fprintf(stderr, "%d,", varr[j]);
		}
		fputc('\n', stderr);
	    }
	}
//	printf("About to free intlist %p=%s\n", lstr, lstr); fflush(stdout);
	BENV_UtilFreeIntList(ilist);
//	printf("Freed intlist\n"); fflush(stdout);
	free(varr);
//	printf("Freed varr and lstr\n"); fflush(stdout);
    }
    CDBGFCALLEXIT;
    return 0;
}

#include <stdarg.h>

/* TODO: A routine that takes a string with %s and %d formats, and
   processes it with the above routines. */
/* BENV_CollPrintFmt(FILE*fp, MPI_Comm comm, int croot, const char *fmt, ...)
   implementation:
   while string left:
       Extract string until % (look for %% or is it \%)
           if %s, add to string, continue
       coll print string
       if at %d
           coll print int
       advance through string
 */
int BENV_CollPrintFmt(FILE*fp, MPI_Comm comm, int croot, const char *fmt, ...)
{
    va_list ap;
    int rc=0;
    const char *p=fmt, *cp;
    char *curstr=0;

    CDBGFCALLENTER;
    if (!p) {
	/* Format is null. Nothing to do but exit */
	return 0;
    }

//    printf("About to run collprintfmt\n");
    va_start(ap,fmt);
    while (*p) {
	char fchar;
	int  ival;
	size_t slen;
	char *sval, *p1;
	/* Skip string until we find a format specifier */
//	printf("Looking for next arg starting at %s\n", p);
	cp = p;
	while (*cp && *cp != '%') cp++;
	/* Process the string so far */
	slen = cp - p;
//	printf("Slen = %d\n", (int)slen); fflush(stdout);
	if (slen > 0) {
	    curstr = (char *)malloc((slen + 1)*sizeof(char));
	    if (!curstr) {}
	    p1 = curstr;
	    /* Copy to curstr and advance p */
	    while (p < cp) *p1++ = *p++;
	    *p1 = 0;
//	printf("Calling collprintstr with :%s:\n", curstr); fflush(stdout);
	    rc = BENV_CollPrintStr(fp, comm, croot, curstr);
	    if (rc) goto fn_end;
	    if (curstr) {
		free(curstr);
		curstr = 0;
	    }
	}
//	printf("Remaining string is %s\n", p); fflush(stdout);

	/* Process the format character (if any) */
	fchar = 0;
	if (*p == '%') {
	    fchar = *++p;
	    if (*p) p++;
//	    printf("Found format specifier %c\n", fchar);
	}
//	printf("Jumping on fchar = %d(%c)\n", fchar, fchar ? fchar : 'N');
	switch (fchar) {
	case 0: /*printf("fchar null\n");*/ break;
	case 's':
	    sval = va_arg(ap, char *);
	    rc = BENV_CollPrintStr(fp, comm, croot, sval);
	    if (rc) goto fn_end;
#if 0
	    ncurstr = (char *)malloc(strlen(curstr) + cp-p+1 + strlen(sval)+1);
	    p1 = ncurstr;
	    p2 = curstr;
	    while (p2 && *p2) *p1++ = *p2++;
	    while (p < cp) *p1++ = *p++;
	    while (sval && *sval) *p1++ = *sval++;
	    *p1 = 0;
	    if (curstr) free(curstr);
	    curstr = ncurstr;
#endif
	    break;
	case 'd':
	    /* FIXME: doesn't handle the string before the %d */
	    ival = va_arg(ap,int);
//	    printf("Handling %%d with value %d\n", ival);
#if 0
	    if (curstr) {
		rc = BENV_CollPrintStr(fp, comm, croot, curstr);
		free(curstr);
		curstr = 0;
		if (rc) goto fn_end;
	    }
#endif
	    rc = BENV_CollPrintInt(fp, comm, croot, ival);
	    if (rc) goto fn_end;
	    break;
	default: break;
	}
//	printf("p = %p, value is %d(%c)\n", p, *p, *p); fflush(stdout);
//	p = cp+1;
    }
#if 0
    if (curstr) {
	rc = BENV_CollPrintStr(fp, comm, croot, curstr);
	free(curstr);
	curstr = 0;
	if (rc) goto fn_end;
    }
#endif
fn_end:
    va_end(ap);
    CDBGFCALLEXIT;
    return rc;
}
