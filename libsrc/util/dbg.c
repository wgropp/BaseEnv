/*
 * Support routines for the debug macros in include/benvdbg.h
 *
 * Unified setting of debug flags from the command line or the
 * environment variables
 *
 * This file has the rouintes that do not require MPI
 *
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#define NO_MPI_INCLUDE
#include "benvdbg.h"
#include "benvutil.h"

/* Global value for output file and process rank */
FILE *cvar_vfp=0;
int cvar_benv_rank=-1, cvar_benv_wrank=-1;
/* Trace function calls */
int cvar_benv_funccall=0, cvar_benv_funccall_indent=0;
/* Global classes */
CDBGGDECL(COMM);
CDBGGDECL(MEM);
CDBGGDECL(DEBUG);
CDBGGDECL(ARGV);

/* Argument names */
static const char *arg_debug = "-debug";
static const char *arg_debugrank = "-debugrank";
static const char *arg_debugclass = "-debugclass";
static const char *arg_debugcalls  = "-debugcalls";

#if 0
int cvar_benv_COMM_verbose=0, cvar_COMM_indent=0;
int cvar_benv_MEM_verbose=0, cvar_MEM_indent=0;
#endif

static void upcase(char *);
int BENVi_DebugMatchClass(const char *name);

int BENVi_DebugStringFromEnv(const char *rootname, const char *subname,
			     char **val);

/* FIXME: option to replace -debug; option to have values like -debug name val
 */

/* FIXME: Add BENV_DebugArgPrintUsage */

/* FIXME: IS THIS OBSOLETE? */
/*@ BENV_DebugArg - Look for debug options

Input Parameters:
+ argc - Argument count
. argv - Argument vector
- name - Arguments have this prefix; may be null. See below

Input/output Parameters:
+ argcnt - pointer to the index of the current argument. Will be updated
 if an hwdesc parameter is found by the number of values read, not counting
 the argument itself.
- val - Incremented by one if '-debug name' seen

Return Value:
Returns 1 if a recognized argument is seen, -1 if there is an error, and
0 otherwise.

Notes:
This provides common processing for arguments of the form '-debug name',
where 'name' is the argument above. Each occurance increments the value
pointed at by 'val', and for '-debugrank n', where 'n' is the rank in
'MPI_COMM_WORLD' that designates which process should generate debug output.

See also:
 BENV_DebugArgClass, BENV_DebugArgRank
  @*/
int BENV_DebugArg(int argc, char **argv, int *argcnt,
		  const char *name, int *val)
{
    int i = *argcnt;
    int rc = 0;

    if (strcmp(argv[i], arg_debug) == 0) {
	i++;
	if (i < argc) {
	    if (strcmp(argv[i], name) == 0) {
		*val = *val + 1;
		rc = 1;
	    }
	}
	else {
	    return -1;
	}
    }
    else if (strcmp(argv[i], arg_debugcalls) == 0) {
	cvar_benv_funccall = 1;
	cvar_benv_funccall_indent = 0;
	if (!cvar_vfp) cvar_vfp = stderr;
    }
#if 0
#ifndef NO_MPI_INCLUDE
    /* FIXME: This needs to be in a separate file */
    else if (strcmp(argv[i], arg_debugrank) == 0) {
	i++;
	if (i < argc) {
	    cvar_benv_rank = atoi(argv[i]);
	    rc = 1;
	}
	else {
	    return -1;
	}
    }
#endif
#endif
    if (rc == 1) *argcnt = i;
    return rc;
}

/* BENV_DebugArgRank - */

/*@ BENV_DebugArgCommon - Initialize common debug options

Input Parameters:
+ argc - Argument count
. argv - Argument vector
- name - Arguments have this prefix; may be null. See below

Input/output Parameters:
+ argcnt - pointer to the index of the current argument. Will be updated
 if an hwdesc parameter is found by the number of values read, not counting
 the argument itself.
- val - Incremented by one if '-debug name' seen

Return Value:
Returns 1 if a recognized argument is seen, -1 if there is an error, and
0 otherwise.

Notes:
This provides common processing for debug arguments. This supports
.vb
    -debugcalls
    -debugclass args
    -debugclass mem
.ve

See also:
 BENV_DebugArgClass, BENV_DebugArgRank
@*/
int BENV_DebugArgCommon(int argc, char **argv, int *argcnt)
{
    int i = *argcnt;
    int rc = 0;
    static const char *classes[] = { "args", "mem" };
    static int *classval[] = { &cvar_benv_ARGV_verbose,
			       &cvar_benv_MEM_verbose, };
    static int numclasses = 2; /* Must be the number of strings in classes */

    if (strcmp(argv[*argcnt], arg_debugcalls) == 0) {
	cvar_benv_funccall = 1;
	cvar_benv_funccall_indent = 0;
	if (!cvar_vfp) cvar_vfp = stderr;
	rc = 1;
    }
    else {
	rc = BENV_DebugArgClass(argc, argv, &i, numclasses, classes, classval);
	if (rc == -1) {
	    /* Ignore unrecognized class names */
	    rc = 0;
	}
    }
    if (rc == 1) *argcnt = i;
    return rc;
}

/*@ BENV_DebugArgClass - Look for debugclass options

Input Parameters:
+ argc - Argument count
. argv - Argument vector
. nclass - Number of class names in classes
- classes - Names of debug classes

Input/output Parameters:
. argcnt - pointer to the index of the current argument. Will be updated
 if an hwdesc parameter is found by the number of values read, not counting
 the argument itself.

Output Parameter:
. classvals - Pointer to int variables for each class. See notes below

Notes:
This provides common processing for arguments of the form '-debugclass
name', where 'name' is one of the strings in 'classes'. Each occurance
increments the value pointed at by the corresponding 'classvals' array
of pointers.

The name of the debug class may have an optional level qualifier by
adding ':b', ':d', or ':a', for basic, detail, and all respectively.
For example, '-debugclass name:a' for all debug messages for class name.

Return Value:
Returns 1 if a class value is seen, 0 if the argument is not
'-debugclass', and -1 if the argument is '-debugclass' but the class
name is not one of the values in 'classes'. This allows multiple
packages within BENV to use this routine to look for debug class
names.

See also:
 BENV_DebugArgClass, BENV_DebugArgRank
 @*/
int BENV_DebugArgClass(int argc, char **argv, int *argcnt, int nclass,
		       const char *(classes[]), int *(classvals[]))
{
    int i = *argcnt;
    int rc = 0;

    CDBGV(ARGV,BASIC,"Starting DebugArgClass with %d classes; first is %s\n",
	  nclass, classes[0]);
    if (strcmp(argv[i], arg_debugclass) == 0) {
	i++;
	if (i < argc) {
	    char *p, *basearg = strdup(argv[i]);
	    int incrval = 1;

	    /* Look for a :c at the end of the class name */
	    p = strchr(basearg, ':');
	    if (p) {
		char cl = *(p+1);
		if (cl == 'b') incrval = 1;
		else if (cl == 'd') incrval = 2;
		else if (cl == 'a') incrval = 3;
		else {
		    /* Invalid level */
		    return -1;
		}
		*p = 0;
	    }

	    for (int j=0; j<nclass; j++) {
		CDBGV(ARGV,DETAIL,"Checking for class %s\n", classes[j]);
		if (strcmp(basearg, classes[j]) == 0) {
		    *classvals[j] = *classvals[j] + incrval;
		    CDBGV(ARGV,DETAIL,"Incrementing classval for %s by %d to %d\n",
			  classes[j], incrval, *classvals[j]);
		    rc = 1;
		    break;
		}
	    }
	    free(basearg);
	    if (rc == 0) {
		/* Unknown class */
		/* FIXME: Allow unknown classes so different systems can
		   accept class names */
		CDBGV(ARGV,BASIC,"Ending DebugArgClass rc=-1 (unrecognized class name %s)\n",
		      argv[i]);
		return -1;
	    }
	}
	else {
	    /* No value for class */
	    return -1;
	}
    }
    if (rc == 1) *argcnt = i;
    CDBGV(ARGV,BASIC,"Ending DebugArgClass rc=%d\n", rc);
    return rc;
}

/* TODO: add a version that takes a single class name and pointer */

/* FIXME: should this handle different forms of value */

/*@ BENV_DebugFromEnv - Get true/false value from the environment

Input Parameters:
+ rootname - Used to form the environment variable name
- subname - If non-null, used to form the environment variable name

Output Parameter:
. debugval - 1 if environment variable has value true or yes (any case), 0
 if environment variable has value false or no (any case). Unchanged otherwise.

Return Value:
Returns 1 if a value (either true or false) is found, 0 if not found, and -1
on an error, such as the environment variable is defined, but the value is not
recognized.

Notes:
The environment variable name is constructed from the 'rootname' and 'subname'
if present. If only the 'rootname' is present, the varaiable name is
'BENV_rootname_DEBUG'. If the subname is also present, the environment
variable name is 'BENV_rootname_DEBUG_subname'.
  @*/
int BENV_DebugFromEnv(const char *rootname, const char *subname,
		      int *debugval)
{
    char *envval;
    int  rc;

    /* Get value */
    rc = BENVi_DebugStringFromEnv(rootname, subname, &envval);
    if (!rc) return 0;

    /* Check for valid values */
    /* Uppercase string first */
    upcase(envval);
    if (strcmp(envval, "TRUE") == 0 ||
	strcmp(envval, "YES") == 0) {
	*debugval = 1;
	rc = 1;
    }
    else if (strcmp(envval, "FALSE") == 0 ||
	     strcmp(envval, "NO") == 0) {
	*debugval = 0;
	rc = 1;
    }
    else
	rc = -1;

    free(envval);
    return rc;
}

/* FIXME: see notes above about value - break this into just get value,
   then process value. Perhaps an internal routine for the value */

int BENV_DebugIntFromEnv(const char *rootname, const char *subname,
			 int *debugval)
{
    char *envval;
    int        rc;

    /* Get value */
    rc = BENVi_DebugStringFromEnv(rootname, subname, &envval);
    if (!rc) return 0;

    /* FIXME: Check for valid values */
    *debugval = atoi(envval);
    free(envval);

    return 1;
}

static void upcase(char *val)
{
    char *p = val;
    if (!p) return;
    while (*p) {
	*p = toupper(*p);
	p++;
    }
}

/* Internal routine to get the string value from an environment variable
   Returns a copy of the environment value

   return 1 if value found, 0 if not.
 */
int BENVi_DebugStringFromEnv(const char *rootname, const char *subname,
			     char **val)
{
    char *envstr, *envval;
    size_t rootnamelen = strlen(rootname);
    size_t subnamelen = 0, envstrlen;
    int    rc;

    /* Form string */
    envstrlen = rootnamelen + 12 + 1;
    if (subname) {
	subnamelen = strlen(subname);
	envstrlen += subnamelen + 1;
    }
    envstr = (char *)malloc(envstrlen);
    if (subname)
	snprintf(envstr, envstrlen, "BENV_%s_DEBUG_%s", rootname, subname);
    else
	snprintf(envstr, envstrlen, "BENV_%s_DEBUG", rootname);

    /* Is it defined? */
    envval = getenv(envstr);
    /* Free the environment variable name */
    free(envstr);
    if (envval) {
	*val = strdup(envval);
	rc = 1;
    }
    else {
	rc = 0;
    }

    return rc;
}


/* Match name to a known debug class. If found, set that bit in the
   debug class vector */
int BENVi_DebugMatchClass(const char *name)
{
    if (strcmp(name, "") == 0) {
	return 1;
    }

    return 0;
}

/*@
  BENV_DebugArgConfig - Configure argument names for debug

Input Parameters:
+ arg - String matching defined names. See below
- newname - String with replacement argument name

.N returnvalue

Notes:
The known argument names are
.n
.n   debug - default is "-debug"
.n   debugrank - default is "-debugrank"
.n   debugclass - default is "-debugclass"
.n   debugcalls - default is "-debugcalls"
.n
  @*/
int BENV_DebugArgConfig(const char *arg, const char *newname)
{
    int rc = 0;
    if (strcmp(arg, "debug") == 0) {
	arg_debug    = strdup(newname);
    }
    else if (strcmp(arg, "debugclass") == 0) {
	arg_debugclass = strdup(newname);
    }
    else if (strcmp(arg, "debugrank") == 0) {
	arg_debugrank = strdup(newname);
    }
    else if (strcmp(arg, "debugcalls") == 0) {
	arg_debugcalls = strdup(newname);
    }
    else {
	rc = 1;
    }
    return rc;
}
