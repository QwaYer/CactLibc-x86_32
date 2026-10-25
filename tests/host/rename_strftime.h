/* Host-test renames for src/strftime.c (see rename_tz.h). */
#ifndef CACT_HOST_RENAME_STRFTIME_H
#define CACT_HOST_RENAME_STRFTIME_H

#include "rename_time.h"

#define strftime cact_strftime
#define strptime cact_strptime

#endif
