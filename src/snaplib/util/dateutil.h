#ifndef _DATEUTIL_H
#define _DATEUTIL_H

/*
   $Log: dateutil.h,v $
   Revision 1.2  2004/04/22 02:35:26  ccrook
   Setting up to support linux compilation (x86 architecture)

   Revision 1.1  1995/12/22 19:52:02  CHRIS
   Initial revision

*/

#ifndef DATEUTIL_H_RCSID
#define DATEUTIL_H_RCSID "$Id: dateutil.h,v 1.2 2004/04/22 02:35:26 ccrook Exp $"
#endif
/* Header file for dateutil.c - SNAP date functions */

#define DAYS_PER_YEAR 365.25

/* Unspecified date */

#define UNDEFINED_DATE     0.0

/* Snap uses dates as double day number */

double snap_date( int year, int month, int day );
double snap_datetime( int year, int month, int day, int hour, int min, int sec );
double snap_datetime_now();
double snap_datetime_parse( const char *definition, const char *format );

/* Conversion to other date formats */
double date_as_year( double snapdate );
double year_as_snapdate( double years );
void date_as_ymd( double snapdate, int *year, int *month, int *day );
void date_as_ymdhms( double snapdate, int *year, int *month, int *day, int *hour, int *min, int *sec );

#endif
