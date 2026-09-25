/* Trust anchor loading for the TLS client.
 *
 * Reads a CA bundle in PEM (the format every OS ships) and turns it into the
 * concatenated DER form the kernel chain verifier wants.  Portable C on
 * purpose: the host test harness compiles this file unchanged. */

#include "tls_internal.h"

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define ROOTS_MAX_BYTES (4u * 1024u * 1024u)

static const char *image_paths[] = {
    "/etc/ca-certificates.crt",
    "/lib/ca-certificates.crt",
};

/* Read a whole file into a malloc'd buffer. */
static unsigned char *read_file(const char *path, size_t *out_len) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return NULL;

    size_t cap = 64 * 1024, len = 0;
    unsigned char *buf = (unsigned char *)malloc(cap);
    if (!buf) { close(fd); return NULL; }

    for (;;) {
        if (len == cap) {
            if (cap >= ROOTS_MAX_BYTES) { free(buf); close(fd); return NULL; }
            unsigned char *nb = (unsigned char *)realloc(buf, cap * 2);
            if (!nb) { free(buf); close(fd); return NULL; }
            buf = nb;
            cap *= 2;
        }
        ssize_t n = read(fd, buf + len, cap - len);
        if (n < 0) { free(buf); close(fd); return NULL; }
        if (n == 0) break;
        len += (size_t)n;
    }
    close(fd);
    *out_len = len;
    return buf;
}

static int b64_val(int c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

/* Decode a base64 run; returns bytes written or -1. */
static long b64_decode(const char *s, size_t n, unsigned char *out, size_t cap) {
    size_t o = 0;
    unsigned acc = 0;
    int bits = 0;
    for (size_t i = 0; i < n; i++) {
        int c = (unsigned char)s[i];
        if (c == '=') break;
        int v = b64_val(c);
        if (v < 0) {
            /* Newlines inside the body are normal; anything else is junk. */
            if (c == '\n' || c == '\r' || c == ' ' || c == '\t') continue;
            return -1;
        }
        acc = (acc << 6) | (unsigned)v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (o >= cap) return -1;
            out[o++] = (unsigned char)((acc >> bits) & 0xff);
        }
    }
    return (long)o;
}

/* Does this line start with "-----<tag>"? */
static int pem_line_is(const char *line, size_t len, const char *tag) {
    size_t tag_len = strlen(tag);
    if (len < tag_len + 5) return 0;
    if (memcmp(line, "-----", 5) != 0) return 0;
    return memcmp(line + 5, tag, tag_len) == 0;
}

/* Append every CERTIFICATE block of a PEM bundle to the DER buffer. */
static long pem_to_der(const char *pem, size_t pem_len,
                       unsigned char **out, size_t *out_len) {
    size_t cap = 256 * 1024, len = 0;
    unsigned char *der = (unsigned char *)malloc(cap);
    if (!der) return -1;

    const char *p = pem, *end = pem + pem_len;
    while (p < end) {
        const char *nl = memchr(p, '\n', (size_t)(end - p));
        size_t line_len = nl ? (size_t)(nl - p) : (size_t)(end - p);

        if (pem_line_is(p, line_len, "BEGIN CERTIFICATE")) {
            const char *body = nl ? nl + 1 : end;
            const char *body_end = body;
            while (body_end < end) {
                const char *nl2 = memchr(body_end, '\n', (size_t)(end - body_end));
                size_t l2 = nl2 ? (size_t)(nl2 - body_end) : (size_t)(end - body_end);
                if (pem_line_is(body_end, l2, "END CERTIFICATE")) break;
                body_end += l2 + (nl2 ? 1 : 0);
                if (!nl2) break;
            }
            size_t body_len = (size_t)(body_end - body);
            size_t need = body_len / 4 * 3 + 4;
            if (len + need > cap) { free(der); return -1; }
            long n = b64_decode(body, body_len, der + len, cap - len);
            if (n <= 0) { free(der); return -1; }
            len += (size_t)n;
            p = body_end;
        }
        if (!nl) break;
        p = nl + 1;
    }

    if (len == 0) { free(der); return -1; }
    *out = der;
    *out_len = len;
    return (long)len;
}

long cact_tls_load_roots(const char *path, unsigned char **out, size_t *out_len) {
    size_t pem_len = 0;
    unsigned char *pem = read_file(path, &pem_len);
    if (!pem) return -1;
    long n = pem_to_der((const char *)pem, pem_len, out, out_len);
    free(pem);
    return n;
}

long cact_tls_load_default_roots(unsigned char **out, size_t *out_len) {
    const char *env = getenv("CACT_CA_BUNDLE");
    if (env && env[0]) return cact_tls_load_roots(env, out, out_len);

    for (size_t i = 0; i < sizeof(image_paths) / sizeof(image_paths[0]); i++) {
        if (cact_tls_load_roots(image_paths[i], out, out_len) > 0) return 1;
    }
    return -1;
}
