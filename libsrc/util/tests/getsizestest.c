#include <stdio.h>
#include <stdlib.h>
#include "getsizes.h"

int main(int argc, char *argv[])
{
    int nsizes, *sptr, i;
    do {
	sptr = BENV_GetSizes(argc, argv, "size", &nsizes);
	printf("%d values:", nsizes); fflush(stdout);
	if (!sptr) {
	    fputc('\n',stdout);
	    break;
	}
	for (i=0; i<nsizes; i++)
	    printf("%d%s", sptr[i], (i!=nsizes-1) ? "," : "\n");
	fflush(stdout);
	free(sptr);
    } while (1);

    return 0;
}

