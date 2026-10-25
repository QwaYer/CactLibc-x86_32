#ifndef CACT_TZ_H
#define CACT_TZ_H

#include "time.h"

/* Internal timezone engine behind tzset()/localtime()/mktime()/strftime().
 *
 * TZ is read as a POSIX TZ string: `std offset [dst [offset][,start[/time],end
 * [/time]]]`, with `Jn` / `n` / `Mm.w.d` transitions.  CactOS ships no timezone
 * database, so a name that points at one (`TZ=Europe/Berlin`, which contains a
 * '/') has nothing to read and falls back to UTC — which is what glibc does when
 * the zone file is missing. */

/* Re-read TZ if it changed since the last call (POSIX tzset()). */
void tzset(void);

/* Seconds east of UTC in effect at the UTC instant `t`; *isdst reports DST. */
long cact_tz_offset_utc(long t, int *isdst);

/* The same, for a struct tm that describes a *local* time (what strftime's
 * %z/%Z have to work from). */
long cact_tz_offset_for_tm(const struct tm *tm, int *isdst);

/* UTC instant for the local time described by *tm (mktime's core). */
long cact_tz_utc_from_tm(const struct tm *tm);

#endif
