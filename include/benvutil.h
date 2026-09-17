#ifndef BENVUTIL_H_INCLUDED
#define BENVUTIL_H_INCLUDED 1

#include "getsizes.h"

/* Opaque integer list */
typedef struct compressedlist *intlistPtr;
typedef struct intarray *intarrayPtr;

/* Error messages (print or save) */
/* TODO: Provide way to suppress printing and save message */
#define BENV_ERRMSGV(...) do {\
    fprintf(stderr, "%s: ", __func__);\
    fprintf(stderr, __VA_ARGS__); fputc('\n', stderr);fflush(stderr);	\
    } while(0)

void BENVi_MallocErr(const char *item, size_t len, const char *basetype);

void BENV_PrintRunInfo(FILE *fp, int argc, char **argv);

/* Provide a common error message for missing attributes. _attrname is
   const char * */
#define BENVi_Abort() abort()

#define BENVi_ErrAttr(_attrname) \
    do {\
    fprintf(stderr, "Unable to access attribute %s on comm\n",_attrname); \
    fflush(stderr); BENVi_Abort();					\
    } while(0)

/* Print a vector as a list of values (a tuple) */
void BENV_PrintIntTuple(FILE *fp, int n, const int vals[], int withEOL);
void BENV_PrintIntList(FILE *fp, int n, const int vals[], int withEOL);

/* Convert command line argument into a value */
int BENV_ArgGetint(int i, int argc, char **argv, int *val);
int BENV_ArgGetdouble(int i, int argc, char **argv, double *val);

/* Convenience routine for standard checking of args with values */
int BENV_ArgCheckEnoughArgs(int curarg, int argc, const char *argname,
			    int needed, void (*printUsage)(FILE*));

/* Standard arg/env variable handling for debug options */
int BENV_DebugArg(int argc, char **argv, int *argcnt,
		  const char *name, int *val);
int BENV_DebugArgCommon(int argc, char **argv, int *argcnt);
int BENV_DebugArgClass(int argc, char **argv, int *argcnt, int nclass,
		       const char *(classes[]), int *(classvals[]));
int BENV_DebugArgConfig(const char *arg, const char *newname);
int BENV_DebugFromEnv(const char *rootname, const char *subname,
		      int *debugval);
int BENV_DebugIntFromEnv(const char *rootname, const char *subname,
			 int *debugval);

/*M
   BENV_ARGCHECK - Convenience macro for checking whether a commandline argument was handled

Input Parmaeters:
+ rc - A return code from routines such as BENV_DebugArg
. msg - A message string to print if rc is -1
. erraction - A command to execute if rc is -1

Synopsis:
void BENV_ARGCHECK(int rc, const char *msg, statement)

Notes:
If 'rc' is 1, this macro executes a 'continue' statement. If 'rc' is 0, no
action is taken. This is intended to be used within a 'for' loop over the
arguments.
  M*/
#define BENV_ARGCHECK(_rc,_msg,_erraction) \
    if ((_rc) == -1) {\
	fprintf(stderr, "%s\n", _msg); fflush(stderr);_erraction;}\
    else if ((_rc) == 1) { continue; }

/* Carefully get an integer value from the environment */
int BENV_GetIntFromEnv(const char *envname, int *value);
int BENV_GetBooleanFromEnv(const char *envname, int *value);

char *BENV_StringCat2(const char *s1, const char *x2);
char *BENV_StringApp(const char *s1, int nstr, ...);

/* Array computations */
int BENV_ArrayComputeOffset(int nl, const int coords[], const int sizes[],
			    int order);

/* Integer lists */
intlistPtr BENV_UtilCompressIntList(int n, const int vec[], int minrange);
int BENV_UtilFreeIntList(intlistPtr lst);
char *BENV_UtilIntListToStr(intlistPtr lst);
int BENV_UtilIntListIsSimple(intlistPtr lst, int *val, int *n);
int BENV_UtilIntListArgDebug(int argc, char **argv, int *argcnt,
			     const char *prefix);
intarrayPtr BENV_UtilCreateIntArray(int nalloc);
int BENV_UtilAppendIntArray(intarrayPtr iarr, int val);
intlistPtr BENV_UtilIntArrayToIntList(intarrayPtr iarr);
void BENV_UtilFreeIntArray(intarrayPtr iarr);
int BENV_UtilIntArrayLen(intarrayPtr iarr);


#endif
