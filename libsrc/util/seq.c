/*
 * Copyright (C) by University of Illinois 2022
 */
/* Sequential execution code */
#include "benvconf.h"
#include "mpi.h"
#include <stdio.h>
#include <stdlib.h>
#include "seq.h"
#include "benvdbg.h"
CDBGEDECL(COMM);

static int verbose = 0;

typedef struct {
    MPI_Comm lcomm;
    int      prevRank, nextRank;
} seqInfo;

static int seqKeyval = MPI_KEYVAL_INVALID;

static int seqDelFn(MPI_Comm comm, int keyval, void *attr, void *estate)
{
    seqInfo *myinfo = (seqInfo *)attr;

  /* To output something about the communicator, we need a common
     representation for a communicator. */
    CDBGV(COMM,DETAIL,"About to free communicator %ld",(long)(myinfo->lcomm));

    MPI_Comm_free( &myinfo->lcomm );
    free( myinfo );
    return 0;
}

/*@
  BENV_SeqBegin - Begin a sequential section

  Input Parameter:
. comm - Communicator of all processes involved in sequential section

  Notes:
  This routine is collective over the 'comm'; only one process at a time
  executes the code between the call to this routine and to 'BENV_SeqEnd';
  the others wait.  The processes execute in rank order (in 'comm')

  See also:
  BENV_SeqEnd, BENV_SeqChangeOrder
  @*/
void BENV_SeqBegin(MPI_Comm comm)
{
    int      flag, mysize, myrank;
    seqInfo  *info;

    if (seqKeyval == MPI_KEYVAL_INVALID) {
	MPI_Comm_create_keyval(MPI_COMM_NULL_COPY_FN, seqDelFn,
			       &seqKeyval, NULL);
    }

    MPI_Comm_get_attr( comm, seqKeyval, &info, &flag );
    if (!flag) {
	info = (seqInfo *)malloc( sizeof(seqInfo) );
	MPI_Comm_dup( comm, &info->lcomm );
	MPI_Comm_rank( info->lcomm, &myrank );
	MPI_Comm_size( info->lcomm, &mysize );
	info->prevRank = myrank - 1;
	if (info->prevRank < 0)   info->prevRank = MPI_PROC_NULL;
	info->nextRank = myrank + 1;
	if (info->nextRank >= mysize) info->nextRank = MPI_PROC_NULL;
	if (verbose) {
	    printf( "seqbegin: prev = %d, next = %d\n",
		    info->prevRank, info->nextRank );
	}
	MPI_Comm_set_attr( comm, seqKeyval, info );
    }
    MPI_Recv(NULL, 0, MPI_INT, info->prevRank, 0, info->lcomm,
	     MPI_STATUS_IGNORE);
}

/*@
  BENV_SeqEnd - End a sequential section

  Input Parameter:
. comm - Communicator of all processes involved in sequential section

  Notes:
  This is a collective routine over all processes in 'comm'

  See also:
  BENV_SeqBegin, BENV_SeqChangeOrder
  @*/
void BENV_SeqEnd(MPI_Comm comm)
{
  seqInfo *info;
  int     flag;

  /* Sanity check */
  if (seqKeyval == MPI_KEYVAL_INVALID)
    MPI_Abort( MPI_COMM_WORLD, 1 );
  MPI_Comm_get_attr( comm, seqKeyval, &info, &flag );
  if (!info || !flag)
    MPI_Abort( MPI_COMM_WORLD, 1 );
  if (verbose) {
    printf( "seqend: prev = %d, next = %d\n",
	    info->prevRank, info->nextRank );
  }
  MPI_Send( NULL, 0, MPI_INT, info->nextRank, 0, info->lcomm );

  /* Make everyone wait until all have completed their send */
  MPI_Barrier( info->lcomm );
}

/*@
  BENV_SeqChangeOrder - Change the order in which processes proceed through a
  sequential section

  Input Parameters:
+ comm - The communicator containing all processes for the sequential section
. prev - The rank of the previous process
- next - The rank of the next process

  Notes:
  This routine changes the order in which the processes in 'comm' proceed in
  a sequential section begun with 'seqBegin'.  While not a collective routine,
  it is the users'' responsibility to ensure that the specification of 'prev'
  and 'next' contains no cycles and all processes are reachable.

  The process for which 'prev' is 'MPI_PROC_NULL' goes first; the last process
  must have 'MPI_PROC_NULL' for the 'next' value.
  @*/
void BENV_SeqChangeOrder( MPI_Comm comm, int prev, int next )
{
  seqInfo *info;
  int flag;
  /* Sanity check */
  if (seqKeyval == MPI_KEYVAL_INVALID)
      MPI_Abort( MPI_COMM_WORLD, 1 );
  MPI_Comm_get_attr( comm, seqKeyval, &info, &flag );
  if (!info || !flag)
      MPI_Abort( MPI_COMM_WORLD, 1 );
  /* Update the order */
  info->prevRank = prev;
  info->nextRank = next;
}
