/* <sys/types.h> forwarder — CactLibc keeps its headers flat in include/, but
 * many ports include <sys/...> directly.  Without this, the compiler falls back
 * to the host's glibc header and its typedefs clash with CactLibc's. */
#ifndef _CACT_SYS_TYPES_H
#define _CACT_SYS_TYPES_H
#include "../types.h"
#endif
