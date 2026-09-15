/*
 * Copyright (C) by University of Illinois 2025
 */
#include "benvconf.h"
#include <stdio.h>
#include <stdlib.h>
#include "tarray.h"

/* TODO: Either an option to convert to rate OR a separate routine.
   Note that for rate, need scaling of xval (e.g., typically need
   to multiply by sizeof(double)
   Scaling should be a double (e.g., to convert to GB/sec)
   Possible approach: Two more args: scaling and rate-or-time */

/*@ BENV_TAPrintCandlestickData - Output data table for a candlestick plot

Input Parameters:
+ fp - FILE pointer for output
. n - Number of data items
. xval - x axis values. Integers, as this routine is typically used
  where the x value is a message or problem size
- qvals - the values for the candlestick plot, as an array of values. This
 is an array with 5 entries for each data item ('xval'), giving box_min,
 whisker_min, whisker_high, box_high, and median, in that order

.N returnvalue

Notes:
This simply outputs the data in the natural order for plotting. The values
output are x,box_min, whisker_min. whisker_high, box_high, median. These
values correspond to 'xval', 'qvals[1]', 'qvals[0]', 'qvals[4]', 'qvals[3]',
and 'qvals[2]' respectively. This is a convenient ordering for gnuplot.

@*/
int BENV_TAPrintCandlestickData(FILE *fp, int n, const int *xval,
				const double *qvals)
{
    int i;
    for (i=0; i<n; i++) {
	/* x box_min, whisker_min  whisker_high  box_high median */
	fprintf(fp, "%d\t%e\t%e\t%e\t%e\t%e\n", *xval,
		qvals[1], qvals[0], qvals[4], qvals[3], qvals[2]);
	xval++;
	qvals += 5;
    }
    return 0;
}

/*@ BENV_TAPlotCandlestickData - Create gnuplot input files to create a candlestick plot from data expressed a quartiles

Input Parameters:
+ datafp - File pointer for data file
. cmdfp - File ponter for command file
. dfname - Name of the data file. More precisely, the name of the
 data file to which the gnuplot command will refer
. n - Number of data items
. xval - x axis values. Integers, as this routine is typically used
  where the x value is a message or problem size
- qvals - the values for the candlestick plot, as an array of values.

.N returnvalue

Notes:
  @*/

int BENV_TAPlotCandlestickData(FILE *datafp, FILE *cmdfp, const char *dfname,
			       int n, const int *xval, const double *qvals)
{
    /* Consider adding:
       set terminal pdf (or one of the others)
       set output 'filename.pdf'
       set logscale (or set logscale x (or y or xy))
       --plot command
       set output # flushes to current output and closes that file
    */
    fprintf(cmdfp, "plot '%s' using 1:2:3:4:5 with candlesticks,\
            '' using 1:6:6:6:6 with candlesticks lt -1 lw 2 notitle\n", dfname);
    BENV_TAPrintCandlestickData(datafp, n, xval, qvals);
    return 0;
}

