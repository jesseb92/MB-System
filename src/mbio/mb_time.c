/*--------------------------------------------------------------------
 *    The MB-system:	mb_time.c	3.00	1/21/93
 *    $Id: mb_time.c,v 3.2 1993-05-15 14:40:30 caress Exp $
 *
 *    Copyright (c) 1993 by 
 *    D. W. Caress (caress@lamont.ldgo.columbia.edu)
 *    and D. N. Chayes (dale@lamont.ldgo.columbia.edu)
 *    Lamont-Doherty Earth Observatory
 *    Palisades, NY  10964
 *
 *    See README file for copying and redistribution conditions.
 *--------------------------------------------------------------------*/
/*
 * mb_time.c includes the "mb_" functions used to translate between
 * various time formats.
 *
 * Author:	D. W. Caress
 * Date:	January 21, 1993
 *
 * $Log: not supported by cvs2svn $
 * Revision 3.1  1993/05/14  22:43:31  sohara
 * fixed rcs_id message
 *
 * Revision 3.0  1993/04/23  19:00:00  dale
 * Initial version
 *
 *
 */

/* standard include files */
#include <stdio.h>
#include <math.h>
#include <strings.h>

/* mbio include files */
#include "../../include/mb_status.h"

/* time conversion constants and variables */
#define MININYEAR 525600.0
#define MININDAY 1440.0
#define MININHOUR 60.0
#define IMININHOUR 60
#define MININSECOND 0.0166667
#define SECONDINMIN 60.0
int	jday[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
static char rcs_id[]="$Id: mb_time.c,v 3.2 1993-05-15 14:40:30 caress Exp $";

/*--------------------------------------------------------------------*/
/* 	function mb_get_time returns the number of minutes from
 * 	1/1/81 00:00:00 calculated from (yy/mm/dd/hr/mi/sc). */
int mb_get_time(verbose,time_i,time_d)
int verbose;
int time_i[6];
double *time_d;
{
  char	*function_name = "mb_get_time";
	int	status;
	int	julday;
	int	leapday;

	/* print input debug statements */
	if (verbose >= 2)
		{
		fprintf(stderr,"\ndbg2  MBIO function <%s> called\n",
			function_name);
		fprintf(stderr,"dbg2  Input arguments:\n");
		fprintf(stderr,"dbg2       verbose: %d\n",verbose);
		fprintf(stderr,"dbg2       year:    %d\n",time_i[0]);
		fprintf(stderr,"dbg2       month:   %d\n",time_i[1]);
		fprintf(stderr,"dbg2       day:     %d\n",time_i[2]);
		fprintf(stderr,"dbg2       hour:    %d\n",time_i[3]);
		fprintf(stderr,"dbg2       minute:  %d\n",time_i[4]);
		fprintf(stderr,"dbg2       second:  %d\n",time_i[5]);
		}

	/* get time */
	julday = jday[time_i[1]-1];
	if (((time_i[0]%4) == 0) && (time_i[1] > 2)) julday++;
	leapday = (time_i[0] - 1981)/4;
	*time_d = (time_i[0] - 1981)*MININYEAR + (julday + leapday + time_i[2])*MININDAY 
		+ time_i[3]*MININHOUR + time_i[4] + time_i[5]/SECONDINMIN;

	/* assume success */
	status = MB_SUCCESS;

	/* print output debug statements */
	if (verbose >= 2)
		{
		fprintf(stderr,"\ndbg2  MBIO function <%s> completed\n",
			function_name);
		fprintf(stderr,"dbg2  Return value:\n");
		fprintf(stderr,"dbg2       time_d:  %f\n",*time_d);
		fprintf(stderr,"dbg2  Return status:\n");
		fprintf(stderr,"dbg2       status:  %d\n",status);
		}

	/* return success */
	return(status);
}
/*--------------------------------------------------------------------*/
/* 	function mb_get_date returns yy/mm/dd/hr/mi/sc calculated
 * 	from the number of minutes after 1/1/81 00:00:0 */
int mb_get_date(verbose,time_d,time_i)
double time_d;
int time_i[6];
{

	char	*function_name = "mb_get_date";
	int	status;
	int	i;
	int	daytotal;
	int	julday;
	int	leapday;

	/* print input debug statements */
	if (verbose >= 2)
		{
		fprintf(stderr,"\ndbg2  MBIO function <%s> called\n",
			function_name);
		fprintf(stderr,"dbg2  Input arguments:\n");
		fprintf(stderr,"dbg2       verbose: %d\n",verbose);
		fprintf(stderr,"dbg2       time_d:  %f\n",time_d);
		}

	/* get the date */
	daytotal = (int) (time_d/MININDAY);
	time_i[3] = (int) ((time_d - daytotal*MININDAY)/MININHOUR);
	time_i[4] = (int) (time_d - daytotal*MININDAY - time_i[3]*MININHOUR);
	time_i[5] = (time_d - ((int) time_d))*SECONDINMIN;
	time_i[0] = (int) (time_d/MININYEAR) + 1981;
	leapday = (time_i[0] - 1981)/4;
	julday = daytotal - 365*(time_i[0] - 1981) - leapday;
	leapday = 0;
	if ((time_i[0]%4) == 0 && julday > jday[2]) leapday = 1;
	for (i=0;i<12;i++)
		if (julday > (jday[i] + leapday)) time_i[1] = i + 1;
	time_i[2] = julday - jday[time_i[1]-1] - leapday;

	/* assume success */
	status = MB_SUCCESS;

	/* print output debug statements */
	if (verbose >= 2)
		{
		fprintf(stderr,"\nMBIO function <%s> completed\n",
			function_name);
		fprintf(stderr,"dbg2  Return values:");
		fprintf(stderr,"dbg2       year:    %d\n",time_i[0]);
		fprintf(stderr,"dbg2       month:   %d\n",time_i[1]);
		fprintf(stderr,"dbg2       day:     %d\n",time_i[2]);
		fprintf(stderr,"dbg2       hour:    %d\n",time_i[3]);
		fprintf(stderr,"dbg2       minute:  %d\n",time_i[4]);
		fprintf(stderr,"dbg2       second:  %d\n",time_i[5]);
		fprintf(stderr,"dbg2  Return status:\n");
		fprintf(stderr,"dbg2       status:  %d\n",status);
		}

	/* return success */
	return(status);
}
/*--------------------------------------------------------------------*/
/* 	function mb_get_jtime returns the julian days calculated 
 *	from (yy/mm/dd/hr/mi/sc). */
int mb_get_jtime(verbose,time_i,time_j)
int verbose;
int time_i[6];
int time_j[4];
{
	char	*function_name = "mb_get_jtime";
	int	status;
	int	leapday;

	/* print input debug statements */
	if (verbose >= 2)
		{
		fprintf(stderr,"\ndbg2  MBIO function <%s> called\n",
			function_name);
		fprintf(stderr,"dbg2  Input arguments:\n");
		fprintf(stderr,"dbg2       verbose:    %d\n",verbose);
		fprintf(stderr,"dbg2       year:       %d\n",time_i[0]);
		fprintf(stderr,"dbg2       month:      %d\n",time_i[1]);
		fprintf(stderr,"dbg2       day:        %d\n",time_i[2]);
		fprintf(stderr,"dbg2       hour:       %d\n",time_i[3]);
		fprintf(stderr,"dbg2       minute:     %d\n",time_i[4]);
		fprintf(stderr,"dbg2       second:     %d\n",time_i[5]);
		}

	/* get time with julian day */
	time_j[0] = time_i[0];
	time_j[1] = jday[time_i[1]-1] + time_i[2];
	if (((time_i[0]%4) == 0) && (time_i[1] > 2)) time_j[1]++;
	time_j[2] = time_i[3]*IMININHOUR + time_i[4];
	time_j[3] = time_i[5];

	/* assume success */
	status = MB_SUCCESS;

	/* print output debug statements */
	if (verbose >= 2)
		{
		fprintf(stderr,"\ndbg2  MBIO function <%s> completed\n",
			function_name);
		fprintf(stderr,"dbg2  Return value:\n");
		fprintf(stderr,"dbg2       year:       %d\n",time_j[0]);
		fprintf(stderr,"dbg2       julian day: %d\n",time_j[1]);
		fprintf(stderr,"dbg2       minute:     %d\n",time_j[2]);
		fprintf(stderr,"dbg2       second:     %d\n",time_j[3]);
		fprintf(stderr,"dbg2  Return status:\n");
		fprintf(stderr,"dbg2       status:     %d\n",status);
		}

	/* return success */
	return(status);
}
/*--------------------------------------------------------------------*/
/* 	function mb_get_itime returns the time in (yy/mm/dd/hr/mi/sc)
 *	calculated from the time in (yy/jd/hr/mi/sc) where jd is the
 *	julian day of the year.
 */
int mb_get_itime(verbose,time_j,time_i)
int verbose;
int time_j[4];
int time_i[6];
{
	char	*function_name = "mb_get_itime";
	int	status;
	int	leapday;
	int	i;

	/* print input debug statements */
	if (verbose >= 2)
		{
		fprintf(stderr,"\ndbg2  MBIO function <%s> called\n",
			function_name);
		fprintf(stderr,"dbg2  Input arguments:\n");
		fprintf(stderr,"dbg2       verbose:    %d\n",verbose);
		fprintf(stderr,"dbg2       year:       %d\n",time_j[0]);
		fprintf(stderr,"dbg2       julian day: %d\n",time_j[1]);
		fprintf(stderr,"dbg2       minute:     %d\n",time_j[2]);
		fprintf(stderr,"dbg2       second:     %d\n",time_j[3]);
		}

	/* get the date */
	time_i[0] = time_j[0];
	time_i[3] = time_j[2]/IMININHOUR;
	time_i[4] = time_j[2] - time_i[3]*IMININHOUR;
	time_i[5] = time_j[3];
	if ((time_j[0]%4) == 0 && time_j[1] > jday[2]) 
		leapday = 1;
	else
		leapday = 0;
	for (i=0;i<12;i++)
		if (time_j[1] > (jday[i] + leapday)) time_i[1] = i + 1;
	time_i[2] = time_j[1] - jday[time_i[1]-1] - leapday;

	/* assume success */
	status = MB_SUCCESS;

	/* print output debug statements */
	if (verbose >= 2)
		{
		fprintf(stderr,"\ndbg2  MBIO function <%s> completed\n",
			function_name);
		fprintf(stderr,"dbg2  Return value:\n");
		fprintf(stderr,"dbg2       year:       %d\n",time_i[0]);
		fprintf(stderr,"dbg2       month:      %d\n",time_i[1]);
		fprintf(stderr,"dbg2       day:        %d\n",time_i[2]);
		fprintf(stderr,"dbg2       hour:       %d\n",time_i[3]);
		fprintf(stderr,"dbg2       minute:     %d\n",time_i[4]);
		fprintf(stderr,"dbg2       second:     %d\n",time_i[5]);
		fprintf(stderr,"dbg2  Return status:\n");
		fprintf(stderr,"dbg2       status:     %d\n",status);
		}

	/* return success */
	return(status);
}
/*--------------------------------------------------------------------*/
