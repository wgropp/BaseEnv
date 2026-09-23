/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 * Copyright (C) by University of Illinois 2026
 */
#include "benvconf.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include "mpi.h"
#include "benvutil.h"
#include "benvmpiutil.h"
#include "benvdbg.h"
#include "hwdescnew.h"
#include "hwdescimpl2.h"

CDBGEDECL(GETDESC);
CDBGEDECL(COMM);

/*
  This file provides a version of MPI_Comm_split_nguided that provides a partial
  emulation of MPI_Comm_split_unguided for use with MPI implementations that
  either do not provide that routine or whose implementation is too limited.
 */

/* We use an attribute to attach a string description of the method used
   to create a communicator with MPIX_Comm_split_unguided.
   It would be nicer to use the info on a communicator, but the MPI
   standard only defines that info for hints used with the communicator */
/* This is the method used. It is set on first call to
   MPIX_Comm_split_unguided */
static int mpixsplitKeyval = MPI_KEYVAL_INVALID;

static int mpixsplitDelFn(MPI_Comm comm, int keyval, void *attr, void *estate)
{
    const char *splitname = (const char *)attr;

    /* To output something about the communicator, we need a common
       representation for a communicator. */
    CDBGV(COMM,DETAIL,"About to free split name %s", splitname);
    free((void *)splitname);
    return 0;
}

/* Call this routine to attach "name" to a communicator as an attribute */
static void setsplitmethodname(MPI_Comm comm, const char *name)
{
    if (mpixsplitKeyval == MPI_KEYVAL_INVALID) {
	MPI_Comm_create_keyval(MPI_COMM_NULL_COPY_FN, mpixsplitDelFn,
			       &mpixsplitKeyval, NULL);
    }
    //printf("Setting split source to %s\n", name);
    MPI_Comm_set_attr(comm, mpixsplitKeyval, strdup(name));
}

/*@
  MPIX_Comm_split_unguided - Emulate MPI_Comm_split_unguided for MPI implementations that do not include it

Notes:
This routine was developed to provide portable access to Open MPI''s split types
for versions of Open MPI that do not implement the MPI 4
'MPI_COMM_TYPE_HW_UNGUIDED'.
  @*/
int MPIX_Comm_split_unguided(MPI_Comm comm, int nrank, const char **desc,
			     MPI_Comm *newcomm)
{
#if MPI_VERSION > 3 || defined(HAS_MPI_COMM_TYPE_HW_UNGUIDED)
    MPI_Info hwinfo;
    MPI_Info_create(&hwinfo);

    CDBG(GETDESC,DETAIL,"Running split_type with HW_UNGUIDED");
    MPI_Comm_split_type(comm, MPI_COMM_TYPE_HW_UNGUIDED,
			nrank,/* Keep same process ordering as parent */
			hwinfo, newcomm);
    if (*newcomm != MPI_COMM_NULL) {
	*desc = BENVi_GetInfoString(hwinfo, "mpi_hw_resource_type");
	CDBGV(GETDESC,DETAIL,"Returned communicator for %s\n", *desc);
	setsplitmethodname(*newcomm, "Comm split with HW_UNGUIDED");
    }
    else {
	CDBG(GETDESC,DETAIL,"Returned null communicator for hw_unguided split");
	*desc = 0;
    }
    MPI_Info_free(&hwinfo);
#elif defined(HAS_OMPI_HWSPLITTYPES)
    /* Open MPI at this writing hasn't implemented
       MPI_COMM_TYPE_HW_UNGUIDED, but does have predefined splittypes
       that make it possible to emulate UNGUIDED.  This also allows a
       fallback to MPI_COMM_TYPE_SHARED.
    */
    int nsplittypes = 4, curtype=0;
    static int splittypes[4] = { OMPI_COMM_TYPE_NODE, OMPI_COMM_TYPE_SOCKET,
				 OMPI_COMM_TYPE_NUMA, OMPI_COMM_TYPE_CORE };
    static const char *splitname[4] = { "Node", "Socket", "NUMA", "Core" };
    MPI_Info hwinfo;

    CDBG(GETDESC,DETAIL,"Starting MPIX_Comm_split_unguided with OMPI_HWSPLITTYPES code");
    *newcomm = MPI_COMM_NULL; /* Set default (no split found) */
    MPI_Info_create(&hwinfo);
    while (curtype < nsplittypes) {
	MPI_Comm ncomm;
	CDBGV(GETDESC,DETAIL,"Trying to split with type %s (%d)\n", splitname[curtype],
	     curtype);
	MPI_Comm_split_type(comm,
			    splittypes[curtype],
			    nrank,/* Keep same process ordering as parent */
			    hwinfo, &ncomm);
	/* For next round, move on to the next split type */
	curtype++;
	/* Ensure that any output communicator is part of a strict
	   hierarchy */
	if (ncomm != MPI_COMM_NULL) {
	    int nsize, osize;
	    CDBG(GETDESC,DETAIL,"Split produced new communicator");
	    MPI_Comm_size(ncomm, &nsize);
	    MPI_Comm_size(comm, &osize);
	    if (nsize < osize) {
		/* Found a strict subset communicator */
		CDBGV(GETDESC,DETAIL,"Found split comm of size %d\n", nsize);
		*newcomm = ncomm;
		break;
	    }
	    /* Ignore this split comm */
	    CDBGV(GETDESC,DETAIL,"New comm is not subset (%d in %d)\n", nsize, osize);
	    MPI_Comm_free(&ncomm);
	}
    }
    if (*newcomm != MPI_COMM_NULL) {
	char *value;
	CDBG(GETDESC,DETAIL,"Get resource string");
	value =	BENVi_GetInfoString(hwinfo, "mpi_hw_resource_type");
	if (value) {
	    *desc = value;
	    CDBGV(GETDESC,BASIC,"Resource string is %s\n", value);
	}
	else
	    /* Use the default */
	    *desc = strdup(splitname[curtype-1]);
    }
    MPI_Info_free(&hwinfo);
    setsplitmethodname(*newcomm, strdup("Comm split with OpenMPI split types");
#elif defined(HAS_MPI_COMM_TYPE_SHARED)
    /* We can do type shared for node, and that's it */
    MPI_Comm ncomm;

    CDBG(GETDESC,DETAIL,"Running split_type with SHARED\n");
    MPI_Comm_split_type(comm, MPI_COMM_TYPE_SHARED, nrank,
			MPI_INFO_NULL, &ncomm);
    /* Check that ncomm is smaller than comm. If not, return MPI_COMM_NULL. */
    if (ncomm != MPI_COMM_NULL) {
	int nsize, osize;
	MPI_Comm_size(ncomm, &nsize);
	MPI_Comm_size(comm, &osize);
	if (nsize == osize) {
	    MPI_Comm_free(&ncomm);
	    *newcomm = MPI_COMM_NULL;
	    return MPI_SUCCESS;
	}
    }
    else {
	*newcomm = MPI_COMM_NULL;
	return MPI_SUCCESS;
    }
    /* Found a strict subset communicator */
    /* We use node here because this is a common result and this matches
       expectations for other codes */
    *desc    = strdup("Node");
    *newcomm = ncomm;
    setsplitmethodname(*newcomm, "Comm split with SHARED");
#else
    /* Failure */
    *newcomm = MPI_COMM_NULL;

#endif /* MPI_VERSION and HW split types */
    return MPI_SUCCESS;
}

/*@ MPIX_Comm_split_method - Return a string describing the method used for
 MPIX_Comm_split_unguided

Input Parameter:
. comm - A communicator that has been used with 'MPIX_Comm_split_unguided'

Return Value:
A pointer to a string describing the method used to implement
'MPIX_Comm_split_unguided' on this communicator. Do not free this string.
@*/
const char *MPIX_Comm_split_method(MPI_Comm comm)
{
    const char *name=0;
    int flag;

    if (comm == MPI_COMM_NULL) return 0;
    MPI_Comm_get_attr(comm, mpixsplitKeyval, &name, &flag);

    if (!flag)
	name = 0;
    return name;
}

