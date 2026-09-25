#ifndef _CACT_TLS_H
#define _CACT_TLS_H

/* CactLibc TLS 1.3 client.
 *
 * The session keys live here, in the calling process: the kernel only supplies
 * primitives (/dev/crypto) and certificate-chain verification, and never sees
 * plaintext or the traffic secrets.  Supported: TLS 1.3, X25519 key exchange,
 * TLS_AES_128_GCM_SHA256, and RSA/ECDSA server certificates; no client
 * certificates, no PSK, no session resumption, no TLS 1.2.
 *
 * Usage:
 *     int fd = <connected TCP socket>;
 *     cact_tls_t *t = cact_tls_connect(fd, "example.com");
 *     if (!t) { ... cact_tls_error() ... }
 *     cact_tls_write(t, req, req_len);
 *     cact_tls_read(t, buf, sizeof buf);
 *     cact_tls_close(t);
 */

#include <stddef.h>

typedef struct cact_tls cact_tls_t;

/* Handshake over the connected socket `fd`.  `hostname` is sent as SNI and is
 * the name the certificate must match.  Returns NULL on failure, with the
 * reason in cact_tls_error(); the socket is left open either way. */
cact_tls_t *cact_tls_connect(int fd, const char *hostname);

/* Application data.  read returns the byte count, 0 on a clean close_notify,
 * or -1 with errno set.  write returns bytes accepted or -1. */
long cact_tls_read(cact_tls_t *t, void *buf, size_t len);
long cact_tls_write(cact_tls_t *t, const void *buf, size_t len);

/* Send close_notify (best effort) and release the connection.  Returns 0, or
 * -1 when the alert could not be sent. */
int cact_tls_close(cact_tls_t *t);

/* Trust anchors as a concatenated DER bundle; NULL/0 restores the default,
 * which is the system bundle (see tls_roots.c).  Returns 0 or -1. */
int cact_tls_set_roots(const void *der, size_t len);

/* Last error message, for logging.  Never NULL. */
const char *cact_tls_error(void);

#endif /* _CACT_TLS_H */
