/* Miscellaneous routines that support use of MPI routines */

#include "benvconf.h"
//#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
//#include <ctype.h>
//#include <time.h>
#include "mpi.h"
#include "benvutil.h"
#include "benvmpiutil.h"

/* FIXME: This is a place holder */
void BENV_MPIFileErr(int rc, const char *msg)
{
    char mpimsg[MPI_MAX_ERROR_STRING];
    int resultlen;
    FILE *fp=stderr;
    MPI_Error_string(rc, mpimsg, &resultlen);
    fprintf(fp, "%s: %s\n", msg, mpimsg);
    fflush(fp);
}

/* Get the name from a communicator; if none, use defname */
int BENVi_GetCommName(MPI_Comm comm, const char *defname, const char **cname)
{
    char *ccname;
    int  ccnameLen;

    ccname = (char *)malloc(MPI_MAX_OBJECT_NAME+1);
    MPI_Comm_get_name(comm, ccname, &ccnameLen);
    if (ccnameLen > 0) {
	/* Create a shortened copy */
	char *scname;
	scname = (char *)malloc(ccnameLen+1);
	strncpy(scname, ccname, ccnameLen+1);
	*cname = (const char *)scname;
    }
    else {
	*cname = strdup(defname);
    }
    free(ccname);
    return 0;
}

/* Return a copy, allocated with malloc, of the value
   associated with the key in the info object */
char *BENVi_GetInfoString(MPI_Info info, const char *key)
{
#ifdef HAVE_MPI_INFO_GET_STRING
    char tmpstr[2];
#endif
    char *value=0;
    int flag, buflen;

    /* MPICH requires a non-null location for the output string,
       even when buflen is 0. So we provide tmpstr and ignore it */
#ifdef HAVE_MPI_INFO_GET_STRING
    buflen = 0;
    MPI_Info_get_string(info, key, &buflen, tmpstr, &flag);
#else
    MPI_Info_get_valuelen(info, key, &buflen, &flag);
#endif

    if (flag && buflen > 0) {
	/* Allocate buffer for value */
	value = (char *)malloc(buflen);
	if (!value) {
	    BENVi_MallocErr("get info string", buflen, "char");
	    MPI_Abort(MPI_COMM_WORLD, 1);
	}
#ifdef HAVE_MPI_INFO_GET_STRING
	MPI_Info_get_string(info, key, &buflen, value, &flag);
#else
	MPI_Info_get(info, key, buflen, value, &flag);
#endif
    }

    return value;
}
