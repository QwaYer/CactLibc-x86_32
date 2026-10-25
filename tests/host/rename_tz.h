/* Host-test renames for the timezone engine (src/tz.c).
 *
 * This is the base header for the time/strftime units too: tzset/tzname/
 * timezone/daylight are glibc's own globals, and defining them in an object
 * linked into a host test would *interpose* glibc's — so the reference would
 * stop being a reference.  Everything that pulls in tz.c includes this. */
#ifndef CACT_HOST_RENAME_TZ_H
#define CACT_HOST_RENAME_TZ_H

#undef errno
#define __errno_location cact_host_errno_location

#define tzset     cact_tzset
#define tzname    cact_tzname
#define timezone  cact_timezone
#define daylight  cact_daylight

#endif
