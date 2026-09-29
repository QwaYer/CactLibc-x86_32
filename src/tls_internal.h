/* Internals shared between the TLS client files (src/tls.c, src/tls_roots.c).
 *
 * Not a public header: it sits next to the sources so `#include "tls_internal.h"`
 * resolves without widening the installed include set. */

#ifndef CACT_TLS_INTERNAL_H
#define CACT_TLS_INTERNAL_H

#include <stddef.h>

/* Load one bundle by exact path.  Returns the number of DER bytes (malloc'd
 * into *out) or -1.  No fallbacks: this is the explicit-path entry point. */
long cact_tls_load_roots(const char *path, unsigned char **out, size_t *out_len);

/* Try the system bundle locations in order: the override from the
 * CACT_CA_BUNDLE environment variable, /etc/ssl/certs/ca-certificates.crt,
 * /etc/ca-certificates.crt, then /usr/share/ca-certificates.crt (the copy the
 * image ships).  Returns DER bytes or -1 when no usable bundle was found. */
long cact_tls_load_default_roots(unsigned char **out, size_t *out_len);

#endif /* CACT_TLS_INTERNAL_H */
