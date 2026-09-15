#ifndef SEQ_H_INCLUDED
#define SEQ_H_INCLUDED 1
void BENV_SeqBegin(MPI_Comm comm);
void BENV_SeqEnd(MPI_Comm comm);
void BENV_SeqChangeOrder(MPI_Comm, int, int);
#endif
