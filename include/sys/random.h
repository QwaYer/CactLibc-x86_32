#ifndef _SYS_RANDOM_H
#define _SYS_RANDOM_H

#include <stddef.h>

#define GRND_NONBLOCK 0x0001
#define GRND_RANDOM   0x0002

int getrandom(void *buf, size_t buflen, unsigned int flags);

#endif
