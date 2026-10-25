/* Host-test renames for src/time.c (see rename_tz.h for the rationale). */
#ifndef CACT_HOST_RENAME_TIME_H
#define CACT_HOST_RENAME_TIME_H

#include "rename_tz.h"

#define gettimeofday  cact_gettimeofday
#define clock_gettime cact_clock_gettime
#define clock_getres  cact_clock_getres
#define clock_settime cact_clock_settime
#define nanosleep     cact_nanosleep
#define clock_nanosleep cact_clock_nanosleep
#define time          cact_time
#define difftime      cact_difftime
#define gmtime        cact_gmtime
#define gmtime_r      cact_gmtime_r
#define localtime     cact_localtime
#define localtime_r   cact_localtime_r
#define mktime        cact_mktime
#define asctime       cact_asctime
#define asctime_r     cact_asctime_r
#define ctime         cact_ctime
#define ctime_r       cact_ctime_r

#endif
