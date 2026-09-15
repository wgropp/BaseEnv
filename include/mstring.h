#ifndef MSTRING_H_INCLUDED
#define MSTRING_H_INCLUDED 1

/* Communication strings in MPI (mstring.c) */
const char *BENV_StringConcatFree(const char *s1, const char *s2);
int BENV_StringSend(const char *s, int dest, int tag, MPI_Comm comm);
const char *BENV_StringRecv(int source, int tag, MPI_Comm comm);
const char *BENV_StringBcast(const char *s, int root, MPI_Comm comm);
int BENV_StringCheckSame(MPI_Comm comm, const char *str, int *flag);

#endif
