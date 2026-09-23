/*
 * Copyright (C) by University of Illinois 2025
 */
#include "benvconf.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "mpi.h"
#include "benvutil.h"
#include "ntest.h"

//#define DO_DEBUG 1
#include "benvdbg.h"

/* Argument names */
static const char *arg_ntestinfo = "-ntest-info";
static const char *arg_ntestmin = "-ntest-min";
static const char *arg_ntestmax = "-ntest-max";
static const char *arg_ntestwt = "-ntest-wt";
static const char *arg_ntests = "-ntest-s";
static const char *arg_ntestr = "-ntest-r";
static const char *arg_ntestwtick = "-ntest-wtick";

/* These routines determine the number of tests to perform. The goal is
   to ensure that the the tests take at least ntestWT times the resolution
   of the timer, which is given (we hope!) by MPI_Wtick. This is estimated
   by assuming the simple performance model, T = s = rn, where s is the latency
   and r is the inverse rate. Provisions are made to extend this to the maxrate
   model, T = s + (1/max(1/rmax,nc*1/r1))n, where nc is the number of
   concurrently communicating processes. Using this formula, ntest can
   be estimate as

   ntest * 2(s+rn) = ntestWT * MPI_Wtick

   (2 since the test is a ping-pong, so there are two communiations in each
   test).

   Thus,

       ntest = ntestWT * MPI_Wtick / (2 (s + rn))

   As a sanity check, ntest is also constrained to be between ntestMIN and
   ntestMAX.
*/

/*@ BENV_NtestInit - Create and initialize an ntext context

Input Parameter:
. comm - Communicator of processes that will share the values in this context

Return Value:
Pointer to an 'ntestctx_t' that can be used with the other Ntest routines

Notes:
The Ntest routines estimate the number of times to repeat a test to ensure
that the measured time is long enough to be accurate. It is based on a simple
time estimate involving two terms - 'T=s + r n', where 'n' is the number
of elements and 's' and 'r' are parameters in the timing model. By default,
this is initialized for internode communication on a high performance
network.

This routine may be called before MPI is initialized. In that case,
pass 'MPI_COMM_NULL' as the communicator and call 'BENV_NtestInitPostMPIInit'
after MPI is initialized.

See also:
BENV_NtestGetVal, BENV_NtestTimeEst, BENV_NtestInitInto, BENV_NtestSaveInfo,
BENV_NtestPrintInfo, BENV_NtestArg, BENV_NtestPrintUsage, BENV_NtestFree
BENV_NtestInitPostMPIInit
  @*/
ntestctx_t *BENV_NtestInit(MPI_Comm comm)
{
    ntestctx_t *new;

    new = (ntestctx_t *)malloc(sizeof(ntestctx_t));
    if (!new) return 0;

    /* Set default values for elements of new */
    new->ntestMIN   = 10;
    new->ntestMAX   = 10000;
    new->ntestWT    = 100;
    new->ntestS     = 2.0e-6;
    new->ntestR     = 1.0e-9;
    new->ntestWtick = 1.0e-9;
    new->ninfo      = 0;

    if (comm != MPI_COMM_NULL) {
	BENV_NtestInitPostMPIInit(new, comm);
    }
    return new;
}

/*@
  BENV_NtestInitPostMPIInit - Complete the initialization of an ntest structure

Input Parameters:
+ ctx - ntest context
- comm - Communicator of processes that will share the values in this context

.N returnvalue

Notes:
This is collective over 'comm' and computes values, such as the clock tick
across the processes in 'comm'.

See also:
BENV_NtestInit
  @*/
int BENV_NtestInitPostMPIInit(ntestctx_t *ctx, MPI_Comm comm)
{
    double wt = MPI_Wtick();
    double t, t1;

   /* Also examine the average cost of a call to
       MPI_Wtime - which is also relevant to the accuracy of the
       times from MPI_Wtime */
    t = MPI_Wtime();
    for (int i=0; i<20; i++) {
	t1 = MPI_Wtime();
    }
    t = (t1 - t) / 20;
    if (wt < t) wt = t;

    MPI_Allreduce(MPI_IN_PLACE, &wt, 1, MPI_DOUBLE, MPI_MAX, comm);
    if  (wt > 1.0e-9)
	ctx->ntestWtick = wt;

    return 0;
}

/*@ BENV_NtestGetVal - Get the number of times to repeat a test

Input Parameters:
+ ctx - ntest context
. len - length parameter for ntest time estimate
- nc - ??

Return Value:
Returns the number of times to repeat a test
@*/
int BENV_NtestGetVal(ntestctx_t *ctx, int len, int nc)
{
    int ntest;

    ntest = ctx->ntestWT * ctx->ntestWtick / ( 2.0 *
			  (ctx->ntestS + ctx->ntestR * len));

    if (ntest < ctx->ntestMIN) ntest = ctx->ntestMIN;
    if (ntest > ctx->ntestMAX) ntest = ctx->ntestMAX;

    return ntest;
}

/*@ BENV_NtestTimeEst - Return the time estimate for an ntest run

Input Parameters:
+ ctx - ntest context
- len - length parameter for ntest time estimate

Return Value:
Returns the time that was estimated that each test would take
@*/
double BENV_NtestTimeEst(ntestctx_t *ctx, int len)
{
    return 2.0 * (ctx->ntestS + ctx->ntestR * len);
}

/*@ BENV_NtestInitInfo - Initialize an ntest for collecting information about runs

Input Parameters:
+ ctx - ntest context
- nmsgs - Number of different test lengths (e.g., message sizes) on which to
 store information

.N returnvalue

Notes:
Ntest can save information about how accurate its predictions of the time that
a test will take, for each different size of test. This routine initializes
space to collect that information.
  @*/
int BENV_NtestInitInfo(ntestctx_t *ctx, int nmsgs)
{
    if (ctx->ntestinfo) {
	ctx->ninfo = (ntestinfo_t *)malloc(nmsgs * sizeof(ntestinfo_t));
	ctx->ninfolen = nmsgs;
    }
    else {
	ctx->ninfo    = 0;
	ctx->ninfolen = 0;
    }
    return 0;
}

/*@
  BENV_NtestSaveInfo - Save information relevant to evaluating ntest choices

Input Parameters:
+ ctx - ntest context
. midx - index of information to save
. nval - Number of times a test was repeated
- t - time that the test took

.N returnvalue
  @*/
int BENV_NtestSaveInfo(ntestctx_t *ctx, int midx, int nval, double t)
{
    if (midx >= 0 && midx < ctx->ninfolen) {
	ctx->ninfo[midx].ntestval = nval;
	ctx->ninfo[midx].acttime  = t;
    }
    return 0;
}

/*@ BENV_NtestPrintInfo - Print information about ntest

Input Parameters:
+ fp - file pointer for output
. ctx - ntest context
. nmsgs - Number of distinct test (message) sizes
- msgsizes - Size of each test. There are nmsgs values in this array

.N returnvalue

Notes:
The output provides the following information\:
.n Model computation of number of tests to perform, expressed in terms of
latency, bandwidth, length, goal time and timer tick values.
.n A table that compares estimated and actual time for tests, with columns
for length, number of tests, estimated time, and actual time.

@*/
int BENV_NtestPrintInfo(FILE *fp, ntestctx_t *ctx, int nmsgs,
			const int msgsizes[])
{
    ntestinfo_t *ntestinfo = ctx->ninfo;

    fprintf(fp, "Model ntest = %.2e * %.2e / (2 * (%.2e + %.2e * len))\n",
	    ctx->ntestWT, ctx->ntestWtick, ctx->ntestS, ctx->ntestR);
    if (ntestinfo && ctx->ninfolen >= nmsgs) {
	fprintf(fp, "Ints\tntest\test time\tact time\n");
	for (int i=0; i<nmsgs; i++) {
	    fprintf(fp, "%d\t%d\t%.2e\t%.2e\n", msgsizes[i],
		    ntestinfo[i].ntestval,
		    BENV_NtestTimeEst(ctx,msgsizes[i])*ntestinfo[i].ntestval,
		    ntestinfo[i].acttime);
	}
    }
    fflush(fp);
    return 0;
}

/*@ BENV_NtestArg - Handle any ntest parameters in argv

Input Parameters:
+ argc - Argument count
. argv - Argument vector
- prefix - Arguments have this prefix; may be null. See below

Input/output Parameters:
+ argcnt - pointer to the index of the current argument. Will be updated
 if an ntest parameter is found by the number of values read, not counting
 the argument itself.
- ctx - An ntestctx, created by 'BENV_NtestInit'. Values in this context may
 be updated.

Return value:
Returns 1 if an argument was found, 0 if not, and -1 on an error in the
argument.
  @*/
int BENV_NtestArg(int argc, char **argv, int *argcnt, const char *prefix,
		  ntestctx_t *ctx)
{
    int i = *argcnt;
    int rc = 0;
    char *ap = argv[i];

    /* If prefix defined, check and skip over it if found */
    if (prefix) {
	const char *p=prefix;
	while (*ap && *p && *ap == *p) {
	    ap++; p++;
	}
	/* if we did not reach the end of prefix, we're done */
	if (*p) return 0;
    }

    if (strcmp(ap, arg_ntestinfo) == 0) {
	ctx->ntestinfo = 1;
	rc = 1;
    }
    else if (strcmp(ap, arg_ntestmin) == 0) {
	i++;
	if (BENV_ArgGetint(i, argc, argv, &ctx->ntestMIN))
	    rc = -1;
	else rc = 1;
    }
    else if (strcmp(ap, arg_ntestmax) == 0) {
	i++;
	if (BENV_ArgGetint(i, argc, argv, &ctx->ntestMAX))
	    rc = -1;
	else rc = 1;
    }
    else if (strcmp(ap, arg_ntestwt) == 0) {
	i++;
	if (BENV_ArgGetdouble(i, argc, argv, &ctx->ntestWT))
	    rc = -1;
	else rc = 1;
    }
    else if (strcmp(ap, arg_ntests) == 0) {
	i++;
	if (BENV_ArgGetdouble(i, argc, argv, &ctx->ntestS))
	    rc = -1;
	else rc = 1;
    }
    else if (strcmp(ap, arg_ntestr) == 0) {
	i++;
	if (BENV_ArgGetdouble(i, argc, argv, &ctx->ntestR))
	    rc = -1;
	else rc = 1;
    }
    else if (strcmp(ap, arg_ntestwtick) == 0) {
	i++;
	if (BENV_ArgGetdouble(i, argc, argv, &ctx->ntestWtick))
	    rc = -1;
	else rc = 1;
    }

    *argcnt = i;
    return rc;
}

/*@
  BENV_NtestArgConfig - Configure argument names for ntest

Input Parameters:
+ arg - String matching defined names. See below
- newname - String with replacement argument name

.N returnvalue

Notes:
The known argument names are
.n
.n ntestinfo - default is "-ntest-info"
.n ntestmin - default is "-ntest-min"
.n ntestmax - default is "-ntest-max"
.n ntestwt - default is "-ntest-wt"
.n ntests - default is "-ntest-s"
.n ntestr - default is "-ntest-r"
.n ntestwtick - default is "-ntest-wtick"
.n
  @*/
int BENV_NtestArgConfig(const char *arg, const char *newname)
{
    int rc = 0;
    if (strcmp(arg, "ntestinfo") == 0) {
	arg_ntestinfo   = strdup(newname);
    }
    else if (strcmp(arg, "ntestmin") == 0) {
	arg_ntestmin    = strdup(newname);
    }
    else if (strcmp(arg, "ntestmax") == 0) {
	arg_ntestmax = strdup(newname);
    }
    else if (strcmp(arg, "ntestwt") == 0) {
	arg_ntestwt = strdup(newname);
    }
    else if (strcmp(arg, "ntests") == 0) {
	arg_ntests = strdup(newname);
    }
    else if (strcmp(arg, "ntestr") == 0) {
	arg_ntestr = strdup(newname);
    }
    else if (strcmp(arg, "ntestwtick") == 0) {
	arg_ntestwtick = strdup(newname);
    }
    else {
	rc = 1;
    }
    return rc;
}

/*@ BENV_NtestArgPrintUsage - Print usage information for ntest command line
 arguments

Input Parameter:
+ fp - File pointer for output
- prefix - Prefix for arguments

@*/
void BENV_NtestArgPrintUsage(FILE *fp, const char *prefix)
{
    const char *p;
    if (prefix) p = prefix;
    else        p = "";
    fprintf(fp, "\
 %s-ntest-info - Write information on the 'ntest' value used and the time that\n\
 the tests took. This allows evaluation of how well the 'ntest' estimation is\n\
 working.\n\
 %s-ntest-min n - Minumum number for ntest\n\
 %s-ntest-max n - Maximum number for ntest\n\
 %s-ntest-wt f - Multiple of 'wtick' to use in estimating 'ntest'\n\
 %s-ntest-s f - Latency (in seconds) to use in estimating 'ntest'\n\
 %s-ntest-r f - Inverse bandwidth (in seconds/byte) to use in estimating 'ntest'\n\
 %s-ntest-wtick f - Value of clock tick to use instead of value from 'MPI_Wtick'\n\
\n", p, p, p, p, p, p, p);
}

/*@ BENV_NtestFree - Free an Ntest context

Input Parameter:
. ctx - Ntest context created with BENV_NtestInit

See also:
BENV_NtestInit
  @*/
void BENV_NtestFree(ntestctx_t *ctx)
{
    if (ctx->ninfo) {
	free(ctx->ninfo);
    }
    free(ctx);
}
