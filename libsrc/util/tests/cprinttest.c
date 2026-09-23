#include <stdlib.h>
#include <stdio.h>
#include "mpi.h"
#include "benvconf.h"
#include "benvmpiutil.h"

int main(int argc, char **argv)
{
    int wrank;
    MPI_Init(0,0);
    MPI_Comm_rank(MPI_COMM_WORLD, &wrank);
    if (wrank == 0) printf("Testing individual CollPrint calls\n");
    BENV_CollPrintStr(stdout, MPI_COMM_WORLD, 0, "starting test with val ");
    BENV_CollPrintInt(stdout, MPI_COMM_WORLD, 0, wrank);
    BENV_CollPrintStr(stdout, MPI_COMM_WORLD, 0, "\n");
    if (wrank == 0) {
	fflush(stdout);
    }
    if (wrank == 0) printf("Testing formatted CollPrint calls\n");
    BENV_CollPrintFmt(stdout, MPI_COMM_WORLD, 0, "Same using fmt with %d\n", wrank);
    if (wrank == 0) {
	fflush(stdout);
    }
    if (wrank == 0) printf("More complex example:\n");
    BENV_CollPrintFmt(stdout, MPI_COMM_WORLD, 0, "Complex format: str %s, range %d, and (expected) constant %d\n", "my str", wrank, 3);
    if (wrank == 0) {
	fflush(stdout);
    }
    MPI_Finalize();
    return 0;
}
