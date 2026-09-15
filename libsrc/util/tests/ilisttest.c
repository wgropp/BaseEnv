#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "benvconf.h"
#include "benvutil.h"

int *readlistfromfile(FILE *fp, int *n);

int main(int argc, char **argv)
{
    const char *iname=0;
    FILE *fp;
    int *vec, n, rc;
    intlistPtr lst;

    for (int i=1; i<argc; i++) {
	rc = BENV_UtilIntListArgDebug(argc, argv, &i, "");
	BENV_ARGCHECK(rc,"error in hwdesc options\n",return 1;);

	if (strcmp(argv[i], "-fname") == 0) {
	    iname = argv[++i];
	}
	else {
	    fprintf(stderr, "Unrecognized argument %s\n", argv[i]);
	    return 1;
	}
    }
    if (!iname) {
	fprintf(stderr, "No file specified!\n");
	return 1;
    }
    fp = fopen(iname, "r");
    while ((vec = readlistfromfile(fp, &n)) != NULL) {
	char *lstr;
	lst = BENV_UtilCompressIntList(n, vec, 0);
	lstr = BENV_UtilIntListToStr(lst);
	fputs(lstr, stdout);
	free(lstr);
	fputc('\n',stdout);
	free(vec);
	BENV_UtilFreeIntList(lst);
    }
    fclose(fp);
    return 0;
}

#include <ctype.h>
int *readlistfromfile(FILE *fp, int *n)
{
    int *veclist, vlen=16, clen=0, v, rc;
    char *p, *p1, *strbuf, *str;

    strbuf = (char *)malloc(1024);
    str = fgets(strbuf, 1024, fp);
    if (!str) {*n = 0; free(strbuf); return 0;}
    p = str;
    /* FIXME: realloc veclist to lengthen */
    veclist = (int *)malloc(vlen*sizeof(int));
    while (clen < vlen && *p) {
	/* Extract an int (before comma or newline, then use sscanf to
	   convert */
	p1 = p;
	while (*p1 && (isdigit(*p1) || *p1 == '-' || isspace(*p1))) p1++;
	if (*p1 == ',') { *p1++ = 0; }
	rc = sscanf(p, "%d", &v);
	//fprintf(stdout, "read with rc=%d, value = %d\n", rc, v );
	//fflush(stdout);
	if (rc == 1)
	    veclist[clen++] = v;
	else
	    break;
	p = p1;
    }
    free(strbuf);
    if (clen == 0) {
	free(veclist);
	veclist = 0;
    }
    *n = clen;
    return veclist;
}
