/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 * Copyright (C) by University of Illinois 2026
 */
#include "benvconf.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "mpi.h"
#include "benvutil.h"
#include "benvmpiutil.h"
#include "hwdescnew.h"
#include "benvdbg.h"
#include "hwdescimpl2.h"

/* Used to debug hwdesc routines by overriding the hw topology
   inquiry routines to specify a decomposition */
static int cvar_hwdesc_ppn = -1;
static int cvar_hwdesc_spn = -1;
static int cvar_hwdesc_numaps = -1;
static int cvar_hwdesc_corepnuma = -1;
static int cvar_hwdesc_debug = 0;
static int cvar_hwdesc_from_cvar = 0;   /* replaces split_debug */
int cvar_hwdesc_addNodeName = 1;

/* Routines to initialize an hwdescParms, including from the environment */
/*@ BENV_HwdescParmInit - Initialize an 'hwdescParms' structure

Output Parameter:
. hwparm - Initialized 'hwdescParms' structure

.N returnvalue

Notes:
Does `not` use values from the environment to initialize the structure.

See also:
BENV_HwdescParmInitFromEnv
@*/
int BENV_HwdescParmInit(hwdescParms *hwparm)
{
    hwparm->policy     = 0;
    hwparm->printMap   = 0;
    hwparm->mapname    = 0;
    hwparm->forcedebug = 0;
    hwparm->nnobj      = 0;
    hwparm->nonnode    = 0;
    for (int i=0; i<4; i++) hwparm->nobjs[i] = 1;
    return 0;
}

/*@ BENV_HwdescParmUpdateFromEnv - Update an hwdescParms from the environment

Input Parameter:
. envbase - Environment base name. Currently ignored

Output Parameter:
. hwparm - Initialized 'hwdescParms'

Return Value:
1 if any value updates, 0 otherwise.

Notes:
This routine currectly uses and cvar values for the number of objects; see
'BENV_HwdescCvarInit'. That routine is called to ensure that the cvars used
have been initialized.  It also looks at these environment variables\:

. BENV_HWDESC_SCHEDULER_POLICY - Scheduler policy string, e.g., "B:C"

Values not set by an environment variable or CVAR variable are left unchanged.

See also:
BENV_HwdescCvarInit
 @*/
int BENV_HwdescParmUpdateFromEnv(hwdescParms *hwparm, const char *envbase)
{
    int valsset = 0;
    const char *envstr;

    /* Fixme: if envbase is not null, use that instead of the cvars */
    BENV_HwdescCvarInit();
    /* We use the cvars as the way to use information in the environment
     variables */
    BENV_HwdescParmSetFromCvar(hwparm);
    valsset = (cvar_hwdesc_ppn > 0) || (cvar_hwdesc_spn > 0) ||
	(cvar_hwdesc_numaps > 0) || (cvar_hwdesc_corepnuma > 0);

    /* Trim nnobj to exclude any levels with 0 objects */
    for (int j=hwparm->nnobj; j>0; j--) {
	if (hwparm->nobjs[j-1] == 0) hwparm->nnobj = j-1;
    }

    envstr = getenv("BENV_HWDESC_SCHEDULER_POLICY");
    if (envstr) {
	hwparm->policy = strdup(envstr);
	valsset = 1;
    }

    return valsset;
}

/*@ BENV_HwdescParmSetFromCvar - Set relevant fields in a parms structure from cvars

Output Parameter:
. hwparms - Hwdesc Parms structure to set

.N returnvalue

Notes:
If any of the cvars 'cvar_hwdesc_ppn', 'cvar_hwdesc_spn', 'cvar_hwdesc_numaps',
or 'cvar_hwdesc_corepnuma' are greater than zero, use these to set the 'nobjs'
array in 'hwparms'.
@*/
int BENV_HwdescParmSetFromCvar(hwdescParms *hwparms)
{
    /* FIXME: Should this only set the levels that have values? */
    if (cvar_hwdesc_ppn > 0) {
	int np;
	MPI_Comm_size(MPI_COMM_WORLD, &np);
	hwparms->nobjs[0] = (np + cvar_hwdesc_ppn-1) / cvar_hwdesc_ppn; // ceiling
	if (hwparms->nnobj < 1) hwparms->nnobj = 1;
    }
    if (cvar_hwdesc_spn > 0) {
	hwparms->nobjs[1] = cvar_hwdesc_spn;
	if (hwparms->nnobj < 2) hwparms->nnobj = 2;
    }
    if (cvar_hwdesc_numaps > 0) {
	hwparms->nobjs[2] = cvar_hwdesc_numaps;
	if (hwparms->nnobj < 3) hwparms->nnobj = 3;
    }
    if (cvar_hwdesc_corepnuma > 0) {
	hwparms->nobjs[3] = cvar_hwdesc_corepnuma;
	if (hwparms->nnobj < 4) hwparms->nnobj = 4;
    }
    if (cvar_hwdesc_debug)
	hwparms->forcedebug = 1;

    return 0;
}

/*@ BENV_HwdescParmPrint - Print the contents of an hwdesc parms structure

Input Parameters:
+ fp - File pointer for output
- parms - Hwdesc parms structure

.N returnvalue

Notes:
This is a local (non-collective) routine.
@*/
int BENV_HwdescParmPrint(FILE *fp, hwdescParms *hwparm)
{
    fprintf(fp, "\tPolicy: %s\n\
\tnnobj: %d\n\tnobjs: ", hwparm->policy ? hwparm->policy : "<NONE>",
	    hwparm->nnobj);
    for (int i=0; i<hwparm->nnobj; i++) {
	fprintf(fp, "%d, ", hwparm->nobjs[i]);
    }
    fputs("\n", fp);
    fprintf(fp, "\tforcedebug: %s\n\
\tprintmap: %s\n\
\tmapname: %s\n",
	    hwparm->forcedebug ? "true" : "false",
	    hwparm->printMap ? "true" : "false",
	    hwparm->mapname ? hwparm->mapname : "<NONE>");
    fflush(fp);
    return 0;
}

/*@ BENV_HwdescCvarInit - Initialize the control variables for the hwdesc
    routines from environment variables

Environment Variables:
The following environment variables can be used to provide values for the
hwdesc routines.
+ BENV_HWDESC_DEBUG - Enable debugging of node info
. BENV_HWDESC_DEBUG_PPN - Processes per node
. BENV_HWDESC_DEBUG_SPN - Sockets (packages) per node
. BENV_HWDESC_DEBUG_NUMA - NUMA domains per socket
. BENV_HWDESC_DEBUG_COREPNUMA - Cores per NUMA domain
. BENV_HWDESC_DEBUG_FROM_CVAR - Debug creation of hwdescCtx using cvar values
- BENV_HWDESC_ADDNODENAME - If true or yes (in any combinatino of letter case),
  add the node name in printing an hwdesc structure

.N returnvalue

Notes:
This routine should only be called once. If called again, it returns without
performing any action.
  @*/
int BENV_HwdescCvarInit(void)
{
    static int wascalled=0;
    int        rc, val;

    /* Ignore this routine if it has already been called */
    if (wascalled) return 0;
    wascalled = 1;

    BENV_DebugFromEnv("HWDESC", (char *)0, &cvar_hwdesc_debug);

    /* Processes per node */
    /* We define the processes per node, because that is more commonly
       a property of a system, while the number of nodes is something
       that the user implicitly selects by selecting the total number
       of processes. */
    /* FIXME: This should be BENV_DebugIntFromEnv("HWDESC", "PPN", &value) */
    rc = BENV_GetIntFromEnv("BENV_HWDESC_DEBUG_PPN", &cvar_hwdesc_ppn);
    if (!rc) {
	cvar_hwdesc_debug = 1;
    }

    /* Sockets per node */
    rc = BENV_GetIntFromEnv("BENV_HWDESC_DEBUG_SPN", &val);
    if (!rc) {
	if (cvar_hwdesc_ppn > 0)
	    cvar_hwdesc_spn = val;
	else {
	    fprintf(stderr, "spn requires ppn be set\n"); fflush(stderr);
	}
    }

    /* Numa regions per socket */
    rc = BENV_GetIntFromEnv("BENV_HWDESC_DEBUG_NUMA", &val);
    if (!rc) {
	if (cvar_hwdesc_spn > 0)
	    cvar_hwdesc_numaps = val;
	else {
	    fprintf(stderr, "numa requires spn be set\n"); fflush(stderr);
	}
    }

    /* Cores per Numa regions */
    rc = BENV_GetIntFromEnv("BENV_HWDESC_DEBUG_COREPNUMA", &val);
    if (!rc) {
	if (cvar_hwdesc_numaps > 0)
	    cvar_hwdesc_corepnuma = val;
	else {
	    fprintf(stderr, "cores requires numaps be set\n"); fflush(stderr);
	}
    }

    /* Use debug information to create hwdescCtx_t */
    BENV_DebugFromEnv("HWDESC", "FROM_CVAR", &cvar_hwdesc_from_cvar);

    BENV_GetBooleanFromEnv("BENV_HWDESC_ADDNODENAME", &cvar_hwdesc_addNodeName);

    return 0;
}

/*@ BENV_HwdescCvarSet - Set control variables for hwdesc info routines

Input Parameters:
+ name - Name of cvar. See below for valid names
- value - Value for cvar

Valid Cvar Names:
+ ppn - Processes per node
. sockets - Sockets (packages) per node
. numa - NUMA regions per socket
. corepnuma - Cores per NUMA region
. from_cvar - Create 'hwdescCtx_t' from other cvar information
- addnodename - Add the node name when generating output
  @*/
void BENV_HwdescCvarSet(const char *name, int value)
{
    if (strcmp(name, "ppn") == 0)
	cvar_hwdesc_ppn         = value;
    else if (strcmp(name, "debug") == 0)
	cvar_hwdesc_debug       = value;
    else if (strcmp(name, "sockets") == 0)
	cvar_hwdesc_spn         = value;
    else if (strcmp(name, "numa") == 0)
	cvar_hwdesc_numaps      = value;
    else if (strcmp(name, "corepnuma") == 0)
	cvar_hwdesc_corepnuma   = value;
    else if (strcmp(name, "from_cvar") == 0)
	cvar_hwdesc_from_cvar   = value;
    else if (strcmp(name, "addnodename") == 0)
	cvar_hwdesc_addNodeName = value;
    else {
	fprintf(stderr, "Unrecognized cvar %s\n", name); fflush(stderr);
    }
}


/*@ BENV_HwdescCvarPrint - Print the control variables for hwdesc

Input Parameter:
. fp - FILE pointer to print to
@*/
void BENV_HwdescCvarPrint(FILE *fp)
{
    fprintf(fp, "cvar_hwdesc_\n\
\tppn: %d\n\
\tspn: %d\n\
\tnumaps: %d\n\
\tcorespnuma: %d\n\
\tdebug: %s\n\
\tfrom_cvar: %s\n\
\taddNodeName: %s\n",
	    cvar_hwdesc_ppn, cvar_hwdesc_spn, cvar_hwdesc_numaps,
	    cvar_hwdesc_corepnuma,
	    cvar_hwdesc_debug ? "true" : "false",
	    cvar_hwdesc_from_cvar ? "true" : "false",
	    cvar_hwdesc_addNodeName ? "true" : "false" );
    fflush(fp);
}

