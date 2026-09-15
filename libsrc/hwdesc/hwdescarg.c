/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 * Copyright (C) by University of Illinois 2026
 */
#include "benvconf.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include "mpi.h" // only because hwdescnew.h needs it
#include "benvutil.h"
#include "hwdescnew.h"
#include "benvdbg.h"
#include "hwdescimpl2.h"

CDBGEDECL(GETDESC);
CDBGEDECL(PRINTHW);
CDBGEDECL(ARGV);

static int updatehwdescflagmask(const char *str);
static void printusagermflag(FILE *, const char *);

/* ------------------------------------------------------------------------ */
/* Argument processing for hwdesc */
/* Argument names. May be modified */
static const char *arg_pm         = "-pm";
static const char *arg_no_pm      = "-no-pm";
static const char *arg_pname      = "-pname";
static const char *arg_policy     = "-schedpolicy";
static const char *arg_nobjs[4]   = {"-numnodes", "-numsocks", "-numnumas",
				     "-numcores" };
static const char *arg_forcedebug = "-forcedebug";
static const char *arg_rmflag     = "-hwdesc-rmflag";

static const char *default_policy_env = "BENV_HWDESC_SCHED_POLICY";

/*@ BENV_HwdescArg - Process command line arguments for hwdesc (hardware description) routines

Input Parameters:
+ argc - Argument count
. argv - Argument vector
- prefix - Arguments have this prefix; may be null. See below

Input/output Parameter:
+ argcnt - pointer to the index of the current argument. Will be updated
 if an hwdesc parameter is found by the number of values read, not counting
 the argument itself.
- parms - pointer to an 'hwdescParms' object that is updated based on any
  arguments found.

.N returnvalue

@*/
int BENV_HwdescArg(int argc, char **argv, int *argcnt, const char *prefix,
		   hwdescParms *parms)
{
    int rc;

    CDBGV(ARGV,BASIC,"Starting HwdescArg with prefix %s and next arg %s\n",
	  prefix, argv[*argcnt]);
    rc = BENV_HwdescArgDebug(argc, argv, argcnt, prefix);
    if (rc) return rc;
    rc = BENVi_HwdescNodeArgDebug(argc, argv, argcnt, prefix);
    if (rc) return rc;

    rc = BENV_HwdescArgCore(argc, argv, argcnt, prefix, &parms->printMap,
			    &parms->mapname);
    if (rc) return rc;
    rc = BENV_HwdescArgPolicy(argc, argv, argcnt, prefix,
			      default_policy_env, &parms->policy);
    if (rc) return rc;
    rc = BENV_HwdescArgDebugDecomp(argc, argv, argcnt, prefix,
				   &parms->nnobj, parms->nobjs,
				   &parms->forcedebug);
    if (rc) return rc;
    CDBGV(ARGV,BASIC,"Ending HwdescArg rc=%d\n", rc);

    return rc;
}

/*@ BENV_HwdescArgCore - Process the core command line arguments for Hwdesc

Input Parameters:
+ argc - Argument count
. argv - Argument vector
- prefix - Arguments have this prefix; may be null. See below

Input/output Parameters:
+ argcnt - pointer to the index of the current argument. Will be updated
 if an hwdesc parameter is found by the number of values read, not counting
 the argument itself.
. printMap - Flag indicating if the process map should be output
- mapname - Allocated string containing the name of a file for the process map

Return value:
Returns 1 if an argument was found, 0 if not, and -1 on an error in the
argument.

Notes:
The parameter names may be modified with 'BENV_HwdescArgConfig'. If 'prefix'
is not null, then arguments `must` have the prefix in their name. E.g., if
prefix is '-hw' and the default names are used, then the value argument names
are '-hw-pm', '-hw-no-pm', and '-hw-pname'.

See also:
BENV_HwdescArg
  @*/
int BENV_HwdescArgCore(int argc, char **argv, int *argcnt, const char *prefix,
		       int *printMap, const char **mapname)
{
    int i = *argcnt, rc=0;
    char *ap = argv[i];

    CDBGV(ARGV,BASIC,"Starting HwdescArgCore with prefix %s and next arg %s\n",
	  prefix, argv[*argcnt]);
    /* If prefix defined, check and skip over it if found */
    if (prefix) {
	const char *p=prefix;
	while (*ap && *p && *ap == *p) {
	    ap++; p++;
	}
	/* if we did not reach the end of prefix, we're done */
	if (*p) return 0;
    }

    if (strcmp(ap, arg_pm) == 0) {
	*printMap = 1;
	CDBGV(ARGV,DETAIL,"Setting printMap to %d\n", *printMap);
	rc = 1;
    }
    else if (strcmp(ap, arg_no_pm) == 0) {
	*printMap = 0;
	CDBGV(ARGV,DETAIL,"Setting printMap to %d\n", *printMap);
	rc = 1;
    }
    else if (strcmp(ap, arg_pname) == 0) {
	i++;
	if (i < argc) {
	    *mapname =  strdup(argv[i]);
	    CDBGV(ARGV,DETAIL,"Setting mapname to %s\n", *mapname);
	    rc = 1;
	}
	else {
	    fprintf(stderr, "%s%s missing value\n", prefix ? prefix : "",
		    arg_pname);
	    fflush(stderr);
	    return -1;
	}
    }
    else if (strcmp(ap, arg_rmflag) == 0) {
	i++;
	if (i < argc) {
	    /* Look for known names */
	    rc = updatehwdescflagmask(argv[i]);
	}
	else {
	    fprintf(stderr, "%s%s missing value\n", prefix ? prefix : "",
		    arg_pname);
	    fflush(stderr);
	    return -1;
	}
    }
    *argcnt = i;
    CDBGV(ARGV,BASIC,"Ending HwdescArgCore rc=%d\n", rc);
    return rc;
}


/*@ BENV_HwdescArgPrintUsage - Print usage information for hwdesc command line
 arguments

Input Parameters:
+ fp - File pointer for output
. prefix - Prefix for arguments
- flags - Used to indicate which hwdesc args to provide usage. -1 for all

Notes:
The values for flags include\:
.n
.n BENV_HWDESCARG_CORE - Prints the basice or core information, including
 process map options, and number of nodes, sockets, NUMA regions, and cores
 that may have been specified.
.n BENV_HWDESCARG_POLICY - Prints the scheduling policy to use
.n BENV_HWDESCARG_DEBUGDECOMP - Prints whether hwdesc information was taken
 from the command line or environment variables rather than the real
 configuration
.n BENV_HWDESCARG_DEBUG - Prints infomration about supporting debug classes
.n
@*/
void BENV_HwdescArgPrintUsage(FILE *fp, const char *prefix, int flags)
{
    const char *p;
    if (prefix) p = prefix;
    else        p = "";

    if (flags & BENV_HWDESCARG_CORE) {
    fprintf(fp, "\
 %s%s - Print the process map\n\
 %s%s - Do not print process map\n\
 %s%s fname - Print the process map to file fname\n", p, arg_pm, p, arg_no_pm,
	    p, arg_pname);
    fprintf(fp, "\
 %s%s n - Number of nodes is n\n\
 %s%s n - Number of sockets on a node is n\n\
 %s%s n - Number of NUMA regions per socket is n\n\
 %s%s n - Number of cores per socket in n\n", p, arg_nobjs[0],
	    p, arg_nobjs[1], p, arg_nobjs[2], p, arg_nobjs[3]);
    printusagermflag(fp, prefix);
    }
    if (flags & BENV_HWDESCARG_POLICY) {
	fprintf(fp, "\
 %s%s string - Use string as the scheduling policy\n", p, arg_policy);
    }
    if (flags & BENV_HWDESCARG_DEBUGDECOMP) {
	fprintf(fp, "\
 %s%s - Use debug information provided rather than determining hwdesc\n\
               from information about the  hardware\n", p, arg_forcedebug);
    }
    if (flags & BENV_HWDESCARG_DEBUG) {
	fprintf(fp, "\
 -debugclass [getdesc printhw getnodeinfo] - set debug info class\n");
    }
    fflush(fp);
}

/*@
  BENV_HwdescArgConfig - Configure argument names for Hwdesc

Input Parameters:
+ arg - String matching defined names. See below
- newname - String with replacement argument name

.N returnvalue

Notes:
The known argument names are
.n
.n   pm    - process map; default is "-pm"
.n   no_pm - no process map; default is "-no-pm"
.n   pname - process map file name; define is "-pname"
.n   policy - scheduler policy
.n   numnodes - For debugging, number of nodes
.n   numsocks - For debugging, number of sockets per node
.n   numnumas - For debugging, number of NUMA regions per socket
.n   forcedebug - Force GetDescGeneral to use debug information
.n
  Because scheduler policy and debug options are not always desired,
  these use a separate set of routines to process the arguments.
  @*/
int BENV_HwdescArgConfig(const char *arg, const char *newname)
{
    int rc = 0;
    if (strcmp(arg, "pm") == 0) {
	arg_pm    = strdup(newname);
    }
    else if (strcmp(arg, "no_pm") == 0) {
	arg_no_pm = strdup(newname);
    }
    else if (strcmp(arg, "pname") == 0) {
	arg_pname = strdup(newname);
    }
    else if (strcmp(arg, "schedpolicy") == 0) {
	arg_policy = strdup(newname);
    }
    else if (strcmp(arg, "numnodes") == 0) {
	arg_nobjs[0] = strdup(newname);
    }
    else if (strcmp(arg, "numsocks") == 0) {
	arg_nobjs[1] = strdup(newname);
    }
    else if (strcmp(arg, "numnumas") == 0) {
	arg_nobjs[2] = strdup(newname);
    }
    else if (strcmp(arg, "numcores") == 0) {
	arg_nobjs[3] = strdup(newname);
    }
    else if (strcmp(arg, "forcedebug") == 0) {
	arg_forcedebug = strdup(newname);
    }
    else {
	rc = 1;
    }
    return rc;
}

/* Standard way to access scheduler policy string */

/*@ BENV_HwdescArgPolicy - Get the scheduler policy from argument or environment variable

Input Parameters:
+ argc - Argument count
. argv - Argument vector
- prefix - Arguments have this prefix; may be null. See below

Input/output Parameters:
+ argcnt - pointer to the index of the current argument. Will be updated
 if an hwdesc parameter is found by the number of values read, not counting
 the argument itself.
- policy - Pointer to policy string, if available.  Allocated with 'malloc'.
 Unchanged if no policy available. It shoiuld be initialized as null.

Return Values:
Returns 1 if an argument was found, 0 if not, and -1 on an error in the
argument.

Notes:
The parameter names may be modified with 'BENV_HwdescArgConfig'. If 'prefix'
is not null, then arguments `must` have the prefix in their name. E.g., if
prefix is '-hw' and the default name are used, then the valid argument name
is '-hw-schedpolicy'.
 @*/
int BENV_HwdescArgPolicy(int argc, char **argv, int *argcnt,
			 const char *prefix, const char *envstr,
			 const char **policy)
{
    int i = *argcnt, rc=0;
    char *ap = argv[i];

    CDBGV(ARGV,BASIC,"Starting HwdescArgpolicy with prefix %s and next arg %s\n",
	  prefix, argv[*argcnt]);
     /* If prefix defined, check and skip over it if found */
    if (prefix) {
	const char *p=prefix;
	while (*ap && *p && *ap == *p) {
	    ap++; p++;
	}
	/* if we did not reach the end of prefix, we're done */
	if (*p) return 0;
    }

    if (strcmp(ap, arg_policy) == 0) {
	i++;
	if (i < argc) {
	    *policy =  strdup(argv[i]);
	    CDBGV(GETDESC,ALL,"Found schedpolicy = %s\n", *policy);
	    CDBGV(ARGV,DETAIL,"Setting schedpolicy to %s\n", *policy);
	    rc = 1;
	}
	else {
	    fprintf(stderr, "%s%s missing value\n", prefix ? prefix : "",
		    arg_policy);
	    fflush(stderr);
	    return -1;
	}
    }
    *argcnt = i;

    if (!*policy && envstr) {
	/* See if there is an environment value */
	const char *envval = getenv(envstr);
	if (envval) *policy = strdup(envval);
    }

    CDBGV(ARGV,BASIC,"Ending HwdescArgPolicy rc=%d\n", rc);
    return rc;

}

/*@ BENV_HwdescArgDebugDecomp - Get arguments for debugging hwdesc usage

Input Parameters:
+ argc - Argument count
. argv - Argument vector
- prefix - Arguments have this prefix; may be null. See below

Input/output Parameters:
+ argcnt - pointer to the index of the current argument. Will be updated
 if an hwdesc parameter is found by the number of values read, not counting
 the argument itself.
. nnobj - Number of valid entries in nobjs
- nobjs - Array of number of objects, for nodes, sockets, and numa regions.
 Values not specified by an argument are unchaged. Should be initialized to
 all ones before the first call.

Output Parameters:
- forcedebug - True if 'BENV_HwdescGetDescGeneral' should use the 'nobjs'
 values rather than trying to determine the hardward configuration.

Return Values:
Returns 1 if an argument was found, 0 if not, and -1 on an error in the
argument.

Notes:
The parameter names may be modified with 'BENV_HwdescArgConfig'. If 'prefix'
is not null, then arguments `must` have the prefix in their name. E.g., if
prefix is '-hw' and the default name are used, then the valid argument name
is '-hw-numnodes'.
 @*/
int BENV_HwdescArgDebugDecomp(int argc, char **argv, int *argcnt,
			      const char *prefix,
			      int *nnobj, int nobjs[], int *forcedebug)
{
    int i = *argcnt, rc=0;
    char *ap = argv[i];

    CDBGV(ARGV,BASIC,"Starting HwdescArgDebugDecomp with prefix %s and next arg %s\n",
	  prefix, argv[*argcnt]);
     /* If prefix defined, check and skip over it if found */
    if (prefix) {
	const char *p=prefix;
	while (*ap && *p && *ap == *p) {
	    ap++; p++;
	}
	/* if we did not reach the end of prefix, we're done */
	if (*p) return 0;
    }

    for (int j=0; j<4; j++) {
	if (strcmp(ap, arg_nobjs[j]) == 0) {
	    i++;
	    if (i < argc) {
		nobjs[j] =  atoi(argv[i]);
		/* Update only if this is greater than the current value */
		if (j+1 > *nnobj)
		    *nnobj = j+1;
		CDBGV(ARGV,DETAIL,"Setting nobjs[%d] for %s to %d\n",
		      j, arg_nobjs[j], nobjs[j]);
		rc = 1;
		break;
	    }
	    else {
		fprintf(stderr, "%s%s missing value\n", prefix ? prefix : "",
			arg_nobjs[j]);
		fflush(stderr);
		rc = -1;
	    }
	}
    }
    if (rc == 0 && strcmp(ap, arg_forcedebug) == 0) {
	    *forcedebug = 1;
	    CDBGV(ARGV,DETAIL,"Setting forcedebug to %d\n", *forcedebug);
	    rc = 1;
    }

    *argcnt = i;

    CDBGV(ARGV,BASIC,"Ending HwdescArgDebugDecomp rc=%d\n", rc);
    return rc;
}

/*@ BENV_HwdescArgDebug - Look for debug options for hwdesc routines

Input Parameters:
+ argc - Argument count
. argv - Argument vector
- prefix - Arguments have this prefix; may be null. See below

Input/Output Parameter:
. argcnt - pointer to the index of the current argument. Will be updated
 if an hwdesc parameter is found by the number of values read, not counting
 the argument itself.

Return Value:
Returns 1 if a known argument value is found, zero otherwise.

Notes:
Recognizes two debug classes - 'getdesc' and 'printhw'. Recognizes
'-debugclass' as the argument name. Currently, the prefix is ignored.
The class may be followed with ':b', ':d', or ':a' for basic, detail, or all
debug information respectively.

See also:
BENV_DebugArgClass, BENV_DebugArgRank
  @*/
int BENV_HwdescArgDebug(int argc, char **argv, int *argcnt,
			const char *prefix)
{
    int rc=0;
    static const char *classes[] = { "getdesc", "printhw" };
    static int *classval[] = { &cvar_benv_GETDESC_verbose,
			       &cvar_benv_PRINTHW_verbose, };

    CDBGV(ARGV,BASIC,"Starting HwdescArgDebug with prefix %s and next arg %s\n",
	  prefix, argv[*argcnt]);
     /* FIXME: Ignore prefix or allow but not require? */
    /* Debug arg rank is not included so these routines can work without MPI */
    rc = BENV_DebugArgClass(argc, argv, argcnt, 2, classes, classval);
    if (rc == -1) {
	/* DebugArgClass returns -1 if class not recognized. Ignore */
	rc = 0;
    }
    if (rc == 0)
	rc = BENV_UtilIntListArgDebug(argc, argv, argcnt, prefix);
#if 0
    /* FIXME: Do we need this? */
    if (rc == 0)
	rc = BENVi_HwdescNodeArgDebug(argc, argv, argcnt, prefix);
#endif

    CDBGV(ARGV,BASIC,"Ending HwdescArgDebug rc=%d\n", rc);
    return rc;
}

typedef struct {
    const char *name;
    int        flag;
} hwflags;
static hwflags knownflags[] = { { "hwloc", BENV_HWDESC_CONFIG_HWLOC },
				{ "sysctl", BENV_HWDESC_CONFIG_SYSCTL },
				{ "cpuinfo", BENV_HWDESC_CONFIG_CPUINFO },
				{ "numanode", BENV_HWDESC_CONFIG_NUMANUMNODE },
				{ "MPI4", BENV_HWDESC_CONFIG_MPI4 |
				  BENV_HWDESC_ASSIGN_MPI4 },
				{ "MPI-Shared", BENV_HWDESC_CONFIG_MPI_SHARED |
				  BENV_HWDESC_ASSIGN_MPI_SHARED },
				{ "EnvVars", BENV_HWDESC_CONFIG_ENV },
				{ "getcpu", BENV_HWDESC_ASSIGN_GETCPU },
				{ "schedgetcpu", BENV_HWDESC_ASSIGN_SCHEDGETCPU },
				{ "numagetnode", BENV_HWDESC_ASSIGN_NUMAGETNODE },
				{ "policy", BENV_HWDESC_ASSIGN_POLICY },
				{ 0, 0 },
};

/* Return 1 on success and -1 on failure */
static int updatehwdescflagmask(const char *str)
{
    int i, rc=-1;

    for (i=0; knownflags[i].name; i++) {
	if (strcmp(str, knownflags[i].name) == 0) {
	    BENVi_HwdescFlagMask &= ~ knownflags[i].flag;
	    return 1;
	}
    }
    return rc;
}

static void printusagermflag(FILE *fp, const char *prefix)
{
    const char *p;
    int i;

    if (prefix) p = prefix;
    else        p = "";

    fprintf(fp, "\
%s%s name - Remove name from methods available in BENV_HwdescGetDescGeneral\n\
            name may be one of ", p, arg_rmflag);
    fprintf(fp, ", %s", knownflags[0].name);
    for (i=1; knownflags[i].name; i++) {
	fprintf(fp, ", %s", knownflags[i].name);
    }
    fputc('\n', fp);
}
