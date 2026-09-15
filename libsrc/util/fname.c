/*
 * Copyright (C) by University of Illinois 2022
 */
#include "benvconf.h"
#include "mpi.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "benvmpiutil.h"


/*@
  BENV_PerRankFilename - Create a file name that contains the process rank

Input Parameters:
+ pattern - Format string with a single '%d' field
- comm    - Communicator from which process rank is taken

Return Value:
String created from the pattern, with the rank in 'comm' replacing the '%d' in
the pattern. It is an error for the pattern to not contain exactly one '%d'
field.
  @*/
const char *BENV_PerRankFilename(const char *pattern, MPI_Comm comm)
{
    int ln = strlen(pattern), lrank, rc;
    char *outstr;

    ln += 20;
    outstr = (char *)malloc(ln * sizeof(char));
    if (!outstr) {
	return 0;
    }
    MPI_Comm_rank(comm, &lrank);
    rc = snprintf(outstr, ln, pattern, lrank);
    if (rc < 0) {
	return 0;
    }
    return (const char *)outstr;
}

