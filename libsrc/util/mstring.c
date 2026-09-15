/*
 * Copyright (C) by University of Illinois 2023
 */
#include "benvconf.h"
#include <stdlib.h>
#include <string.h>
#include "mpi.h"
#include "mstring.h"

/*@ BENV_StringConcatFree - Concatenate two strings and return the result

Input Parameters:
+ s1 - First string. May be the null string. If not null, must be null
  terminated.
- s2 - Second string. May be the null string. If not null, must be null
  terminated.

Return Value:
 A null terminated string containting the concatenation of 's1' and 's2'.
 If 's1' and 's2' are both null, an empty string (a single null terminator)
 is returned. On error, a null is returned.

Notes:
's1' and 's2' are freed with 'free' if non-null.
@*/
const char *BENV_StringConcatFree(const char *s1, const char *s2)
{
    int l1=0, l2=0;
    char *str, *p;
    const char *pin;

    if (s1) l1 = strlen(s1);
    if (s2) l2 = strlen(s2);
    str = (char *)malloc(l1 + l2 + 1);
    if (!str) return 0;

    p   = str;
    if (s1) {
	pin = s1;
	while (*pin) *p++ = *pin++;
	free((void *)s1);
    }
    if (s2) {
	pin = s2;
	while (*pin) *p++ = *pin++;
	free((void *)s2);
    }
    *p = 0;
    return (const char *)str;
}

/*@ BENV_StringSend - Send a string to another process using MPI Send

Input parmeters:
+ s - String to send. Must be null terminated
. dest - Rank of destination process
. tag  - Message tag
- comm - Communicator for message

Notes:
This routine sends a null-terminated string of arbitrary length to another
process. Blocking 'MPI_Send' is used.  If message isolation is needed,
the user should create a separate communicator.
@*/
int BENV_StringSend(const char *s, int dest, int tag, MPI_Comm comm)
{
    int rc;
    rc = MPI_Send(s, strlen(s)+1, MPI_CHAR, tag, dest, comm);
    return rc;
}

/*@ BENV_StringRecv - Receive a string from another process using MPI Recv

Input parmeters:
+ source - Rank of sending process
. tag  - Message tag
- comm - Communicator for message

Return Value:
A pointer to the string that was received.  The memory for this string is
allocated with 'malloc' and it is the responsibility of the user to free
the memory when it is no longer needed.  On error, a null string is returned.

Notes:
This routine receives a null-terminated string of arbitrary length
from another process. Blocking 'MPI_Recv' is used.  If message isolation
is needed, the user should create a separate communicator.
 @*/
const char *BENV_StringRecv(int source, int tag, MPI_Comm comm)
{
    MPI_Status st;
    int        ln;
    char       *str;

    MPI_Probe(source, tag, comm, &st);
    MPI_Get_count(&st, MPI_CHAR, &ln);
    str = (char *)malloc(ln);
    if (!str) return 0;
    MPI_Recv(str, ln, MPI_CHAR, source, tag, comm, MPI_STATUS_IGNORE);
    return (const char *)str;
}

/*@ BENV_StringBcast - Broadcast a string to all processes in a communicator

Input parmeters:
+ s - String to send. Must be null terminated. Significant only at root.
. root - rank of sending process. All others receive the string
- comm - Communicator for message

Return Value:
A pointer to the string that was received.  The memory for this string is
allocated with 'malloc' and it is the responsibility of the user to free
the memory when it is no longer needed.  On error, a null string is returned.
The process with 'rank = root' returns the value of 's', unless an error is
encountered.

Notes:
This routine sends a null-terminated string of arbitrary length to all
other processes in the commuicator. Blocking 'MPI_Bcast' is used. If message
isolation is needed, the user should create a separate communicator.
  @*/
const char *BENV_StringBcast(const char *s, int root, MPI_Comm comm)
{
    int slen = 0, rank;
    char *ss;

    MPI_Comm_rank(comm, &rank);

    /* Share length */
    if (rank == root)
	slen = (int)strlen(s);
    MPI_Bcast(&slen, 1, MPI_INT, root, comm);
    /* Allocate spae if needed */
    if (rank != root) {
	ss = (char *)malloc(slen+1);
	if (!ss) {
	    return 0;
	}
    }
    else {
	ss = (char *)s;
    }
    MPI_Bcast(ss, slen+1, MPI_CHAR, root, comm);
    /* Return string */
    return (const char *)ss;
}

/*@ BENV_StringCheckSame - Check that a string is the same on all
  processes in comm

Input Parameters:
+ comm - Communicator of processes
- str  - String to compare

Output Parameter:
. flag - true (1) if all are the same, 0 otherwise

 @*/
int BENV_StringCheckSame(MPI_Comm comm, const char *str, int *flag)
{
    int i, lsame[2], len;
    short *csame;

    /* First check length */
    len = strlen(str);
    lsame[0] = len;
    lsame[1] = -len;

    MPI_Allreduce(MPI_IN_PLACE, lsame, 2, MPI_INT, MPI_MAX, comm);
    if (lsame[0] != -lsame[1]) {
	*flag = 0;
	return 0;
    }

    /* Check individual characters as short values */
    csame = (short *)malloc(len*2*sizeof(short));
    for (i=0; i<len; i++) {
	csame[i]     = str[i];
	csame[len+i] = -csame[i];
    }
    MPI_Allreduce(MPI_IN_PLACE, csame, 2*len, MPI_SHORT, MPI_MAX, comm);
    for (i=0; i<len; i++) {
	if (csame[i] != -csame[i+len]) {
	    *flag = 0;
	    free(csame);
	    return 0;
	}
    }
    free(csame);
    *flag = 1;
    return 0;
}
