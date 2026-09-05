#include "nodeio.h"
#include "errno.h"
#include <stdint.h>

int dns_resolve(const char *name, uint32_t *out_ip_host) {
    cact_dns_arg_t a;
    a.name   = (char *)name;
    a.out_ip = out_ip_host;
    int r = nio_dev_cmd("net", CACT_NETCTL_DNS_RESOLVE, &a);
    if (r < 0) { errno = -r; return -1; }
    return 0;
}
