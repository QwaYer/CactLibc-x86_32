/* CactLibc TLS 1.3 client — handshake, record layer and key schedule.
 *
 * The session keys live in this process; the kernel is only consulted for
 * primitives (SHA-256, HMAC, AES-GCM, X25519, randomness) and for certificate
 * chain verification, which needs the vendored webpki the kernel already has.
 * The ephemeral key is generated here from `cact_random` and only handed to the
 * kernel service as an operand of the scalar multiplication, so no private key
 * is ever produced by the kernel on this path.
 *
 * Deliberate limits: TLS 1.3 only, X25519 only, TLS_AES_128_GCM_SHA256 only, no
 * PSK/resumption/0-RTT, no client certificates, and no HelloRetryRequest — with
 * X25519 offered, a server insisting on another group is rejected rather than
 * silently downgraded.
 *
 * Portable C: apart from <crypto.h> this file only uses POSIX read/write and
 * the C library, so the host test harness compiles it unchanged. */

#include "tls.h"
#include "tls_internal.h"

#include <crypto.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* ── limits and constants ───────────────────────────────────────────────── */

#define MAX_PLAIN      16384            /* TLSPlaintext limit (RFC 8446) */
#define MAX_CIPHER     (MAX_PLAIN + 256)
#define MAX_HANDSHAKE  65536
#define HASH_LEN       32               /* this client is SHA-256 only */
#define KEY_LEN        16               /* AES-128 */
#define IV_LEN         12
#define TAG_LEN        16

#define CT_CHANGE_CIPHER 20
#define CT_ALERT       21
#define CT_HANDSHAKE   22
#define CT_APP         23

#define HS_CLIENT_HELLO        1
#define HS_SERVER_HELLO        2
#define HS_NEW_SESSION_TICKET  4
#define HS_ENCRYPTED_EXTENSIONS 8
#define HS_CERTIFICATE         11
#define HS_CERTIFICATE_VERIFY  15
#define HS_FINISHED            20
#define HS_KEY_UPDATE          24

#define ALERT_CLOSE_NOTIFY     0

#define TLS_SUITE_AES128_GCM_SHA256 0x1301
#define TLS_GROUP_X25519            0x001d

/* Signature schemes we advertise; every one is verifiable by the kernel. */
static const uint8_t sig_schemes[] = {
    0x04, 0x03,   /* ecdsa_secp256r1_sha256 */
    0x05, 0x03,   /* ecdsa_secp384r1_sha384 */
    0x08, 0x04,   /* rsa_pss_rsae_sha256   */
    0x08, 0x05,   /* rsa_pss_rsae_sha384   */
    0x08, 0x06,   /* rsa_pss_rsae_sha512   */
    0x04, 0x01,   /* rsa_pkcs1_sha256      */
    0x05, 0x01,   /* rsa_pkcs1_sha384      */
    0x06, 0x01,   /* rsa_pkcs1_sha512      */
};

/* HelloRetryRequest sentinel random (RFC 8446 §4.1.3). */
static const uint8_t hrr_random[32] = {
    0xcf, 0x21, 0xad, 0x74, 0xe5, 0x9a, 0x61, 0x11, 0xbe, 0x1d, 0x8c, 0x02,
    0x1e, 0x65, 0xb8, 0x91, 0xc2, 0xa2, 0x11, 0x16, 0x7a, 0xbb, 0x8c, 0x5e,
    0x07, 0x9e, 0x09, 0xe2, 0xc8, 0xa8, 0x33, 0x9c,
};

/* X25519 base point (RFC 7748 §6.1), used to turn a private scalar into a
 * public key with the service's derive operation. */
static const uint8_t x25519_basepoint[32] = {9};

enum { ST_HANDSHAKE = 0, ST_APP, ST_CLOSED };

struct cact_tls {
    int fd;
    int state;
    int rd_enc;                 /* records from the peer are encrypted */
    int wr_enc;                 /* records we send are encrypted */

    uint8_t c_hs_key[KEY_LEN], c_hs_iv[IV_LEN];
    uint8_t s_hs_key[KEY_LEN], s_hs_iv[IV_LEN];
    uint8_t c_hs_secret[HASH_LEN], s_hs_secret[HASH_LEN];
    uint8_t c_ap_key[KEY_LEN], c_ap_iv[IV_LEN];
    uint8_t s_ap_key[KEY_LEN], s_ap_iv[IV_LEN];
    uint8_t c_ap_secret[HASH_LEN], s_ap_secret[HASH_LEN];
    uint8_t hs_secret[HASH_LEN];        /* kept for the application keys */
    /* keys currently in use, switched as the handshake progresses */
    uint8_t rd_key[KEY_LEN], rd_iv[IV_LEN];
    uint8_t wr_key[KEY_LEN], wr_iv[IV_LEN];
    uint64_t rd_seq, wr_seq;

    uint8_t *rec;               /* record scratch, 5 + MAX_CIPHER */
    uint8_t *plain;             /* decrypted payload, MAX_CIPHER */
    uint8_t *app;               /* leftover application bytes */
    size_t app_len, app_off;

    uint8_t *hs;                /* handshake reassembly */
    size_t hs_len, hs_off, hs_cap;

    uint8_t *tr;                /* transcript of handshake messages */
    size_t tr_len, tr_cap;

    uint8_t *chain;             /* leaf || intermediates, concatenated DER */
    size_t chain_len, chain_cap;

    char hostname[256];
};

/* Trust anchors: set explicitly or loaded from the system bundle on first use. */
static uint8_t *roots_der;
static size_t roots_len;

static char tls_err[160] = "no error";

const char *cact_tls_error(void) { return tls_err; }

static void err_set(const char *msg) {
    size_t i = 0;
    if (!msg) msg = "tls error";
    while (msg[i] && i < sizeof(tls_err) - 1) { tls_err[i] = msg[i]; i++; }
    tls_err[i] = '\0';
}

/* ── growable buffers ───────────────────────────────────────────────────── */

static int grow(uint8_t **buf, size_t *cap, size_t need) {
    if (need <= *cap) return 0;
    size_t ncap = *cap ? *cap : 256;
    while (ncap < need) ncap *= 2;
    uint8_t *nb = (uint8_t *)realloc(*buf, ncap);
    if (!nb) return -1;
    *buf = nb;
    *cap = ncap;
    return 0;
}

static int append(uint8_t **buf, size_t *len, size_t *cap,
                  const void *data, size_t n) {
    if (grow(buf, cap, *len + n) != 0) return -1;
    if (n) memcpy(*buf + *len, data, n);
    *len += n;
    return 0;
}

/* ── socket I/O ─────────────────────────────────────────────────────────── */

/* 1 = filled, 0 = clean EOF before any byte, -1 = error or short EOF. */
static int read_full(int fd, uint8_t *p, size_t n) {
    size_t got = 0;
    while (got < n) {
        ssize_t r = read(fd, p + got, n - got);
        if (r < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (r == 0) return got == 0 ? 0 : -1;
        got += (size_t)r;
    }
    return 1;
}

static int write_full(int fd, const uint8_t *p, size_t n) {
    size_t sent = 0;
    while (sent < n) {
        ssize_t r = write(fd, p + sent, n - sent);
        if (r < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (r == 0) return -1;
        sent += (size_t)r;
    }
    return 0;
}

/* ── key schedule (RFC 8446 §7.1) ───────────────────────────────────────── */

static int hkdf_extract(const uint8_t *salt, size_t salt_len,
                        const uint8_t *ikm, size_t ikm_len, uint8_t out[HASH_LEN]) {
    return cact_hmac_sha256(salt, salt_len, ikm, ikm_len, out);
}

static int hkdf_expand(const uint8_t prk[HASH_LEN], const uint8_t *info,
                       size_t info_len, uint8_t *out, size_t out_len) {
    uint8_t t[HASH_LEN];
    uint8_t msg[HASH_LEN + 300];
    size_t t_len = 0, done = 0;
    uint8_t counter = 1;

    if (info_len > 300) return -1;
    while (done < out_len) {
        if (t_len) memcpy(msg, t, t_len);
        memcpy(msg + t_len, info, info_len);
        msg[t_len + info_len] = counter;
        if (cact_hmac_sha256(prk, HASH_LEN, msg, t_len + info_len + 1, t) != 0)
            return -1;
        t_len = HASH_LEN;
        size_t take = out_len - done < HASH_LEN ? out_len - done : HASH_LEN;
        memcpy(out + done, t, take);
        done += take;
        counter++;
    }
    return 0;
}

/* HKDF-Expand-Label (RFC 8446 §7.1):
 *   info = u16(out_len) || u8(label_len) || "tls13 " || label
 *          || u8(ctx_len) || ctx
 * The label length byte is easy to forget and corrupts every derived key. */
static int expand_label(const uint8_t *secret, const char *label,
                        const uint8_t *ctx, size_t ctx_len,
                        uint8_t *out, size_t out_len) {
    uint8_t info[2 + 1 + 6 + 64 + 1 + HASH_LEN];
    size_t label_len = strlen(label);
    size_t full_len = 6 + label_len;            /* "tls13 " || label */
    if (full_len > 255 || ctx_len > 255) return -1;

    info[0] = (uint8_t)(out_len >> 8);
    info[1] = (uint8_t)out_len;
    info[2] = (uint8_t)full_len;
    memcpy(info + 3, "tls13 ", 6);
    memcpy(info + 9, label, label_len);
    info[9 + label_len] = (uint8_t)ctx_len;
    if (ctx_len) memcpy(info + 10 + label_len, ctx, ctx_len);

    return hkdf_expand(secret, info, 10 + label_len + ctx_len, out, out_len);
}

static int transcript_hash(cact_tls_t *t, uint8_t out[HASH_LEN]) {
    if (t->tr_len == 0) return cact_sha256("", 0, out);
    return cact_sha256(t->tr, t->tr_len, out);
}

/* Derive-Secret over the transcript so far */
static int derive_secret(cact_tls_t *t, const uint8_t *secret, const char *label,
                         uint8_t out[HASH_LEN]) {
    uint8_t th[HASH_LEN];
    if (transcript_hash(t, th) != 0) return -1;
    return expand_label(secret, label, th, HASH_LEN, out, HASH_LEN);
}

/* Derive-Secret over the *empty* transcript, i.e. with Hash("") as context.
 * The "derived" steps and "traffic upd" are defined that way (RFC 8446 §7.1),
 * and using the live transcript there silently produces the wrong keys. */
static int derive_secret_empty(const uint8_t *secret, const char *label,
                               uint8_t out[HASH_LEN]) {
    uint8_t empty_hash[HASH_LEN];
    if (cact_sha256("", 0, empty_hash) != 0) return -1;
    return expand_label(secret, label, empty_hash, HASH_LEN, out, HASH_LEN);
}

static int traffic_keys(const uint8_t *secret, uint8_t key[KEY_LEN],
                        uint8_t iv[IV_LEN]) {
    if (expand_label(secret, "key", NULL, 0, key, KEY_LEN) != 0) return -1;
    return expand_label(secret, "iv", NULL, 0, iv, IV_LEN);
}

/* ── transcript ─────────────────────────────────────────────────────────── */

static int tr_add(cact_tls_t *t, const uint8_t *msg, size_t len) {
    return append(&t->tr, &t->tr_len, &t->tr_cap, msg, len);
}

/* ── record layer ───────────────────────────────────────────────────────── */

static void nonce_from(const uint8_t iv[IV_LEN], uint64_t seq, uint8_t nonce[IV_LEN]) {
    memcpy(nonce, iv, IV_LEN);
    for (int i = 0; i < 8; i++)
        nonce[IV_LEN - 1 - i] ^= (uint8_t)(seq >> (8 * i));
}

static int send_record(cact_tls_t *t, uint8_t type, const uint8_t *data, size_t len) {
    if (len > MAX_PLAIN) { err_set("record too large to send"); return -1; }

    if (!t->wr_enc) {
        t->rec[0] = type;
        t->rec[1] = 0x03;
        t->rec[2] = 0x03;
        t->rec[3] = (uint8_t)(len >> 8);
        t->rec[4] = (uint8_t)len;
        if (len) memcpy(t->rec + 5, data, len);
        if (write_full(t->fd, t->rec, 5 + len) != 0) {
            err_set("write failed");
            return -1;
        }
        return 0;
    }

    /* inner plaintext = content || real content type */
    memcpy(t->plain, data, len);
    t->plain[len] = type;
    size_t inner = len + 1;
    size_t ct_len = inner + TAG_LEN;

    t->rec[0] = CT_APP;
    t->rec[1] = 0x03;
    t->rec[2] = 0x03;
    t->rec[3] = (uint8_t)(ct_len >> 8);
    t->rec[4] = (uint8_t)ct_len;

    uint8_t nonce[IV_LEN];
    nonce_from(t->wr_iv, t->wr_seq, nonce);
    if (cact_aes128_gcm_seal(t->wr_key, nonce, t->rec, 5,
                             t->plain, inner, t->rec + 5, MAX_CIPHER) != 0) {
        err_set("record encryption failed");
        return -1;
    }
    t->wr_seq++;
    if (write_full(t->fd, t->rec, 5 + ct_len) != 0) {
        err_set("write failed");
        return -1;
    }
    return 0;
}

/* Read one record.  1 = ok, 0 = clean EOF, -1 = error. */
static int recv_record(cact_tls_t *t, uint8_t *type, uint8_t *out, size_t *out_len) {
    for (;;) {
        uint8_t hdr[5];
        int r = read_full(t->fd, hdr, 5);
        if (r <= 0) {
            err_set(r == 0 ? "peer closed the connection" : "short read on the socket");
            return r;
        }
        size_t len = ((size_t)hdr[3] << 8) | hdr[4];
        if (len > MAX_CIPHER) { err_set("record too large"); return -1; }
        if (len && read_full(t->fd, t->rec, len) != 1) {
            err_set("short record body");
            return -1;
        }

        /* TLS 1.3 keeps a dummy ChangeCipherSpec on the wire for middlebox
         * compatibility (RFC 8446 §5).  It carries no meaning and must be
         * ignored, in either direction of the handshake. */
        if (hdr[0] == CT_CHANGE_CIPHER) {
            if (len != 1 || t->rec[0] != 0x01) {
                err_set("malformed ChangeCipherSpec");
                return -1;
            }
            continue;
        }

        if (!t->rd_enc) {
            if (hdr[0] != CT_HANDSHAKE && hdr[0] != CT_ALERT) {
                err_set("unexpected plaintext record type");
                return -1;
            }
            memcpy(out, t->rec, len);
            *type = hdr[0];
            *out_len = len;
            return 1;
        }

        if (hdr[0] != CT_APP || len < TAG_LEN) {
            err_set("unexpected encrypted record");
            return -1;
        }

        uint8_t nonce[IV_LEN];
        nonce_from(t->rd_iv, t->rd_seq, nonce);
        memcpy(out, t->rec, len);
        if (cact_aes128_gcm_open(t->rd_key, nonce, hdr, 5, out, len, out, len - TAG_LEN) != 0) {
            err_set("record failed authentication");
            return -1;
        }
        t->rd_seq++;

        size_t pt_len = len - TAG_LEN;
        while (pt_len > 0 && out[pt_len - 1] == 0) pt_len--;   /* strip padding */
        if (pt_len == 0) { err_set("empty inner plaintext"); return -1; }
        *type = out[pt_len - 1];
        *out_len = pt_len - 1;
        return 1;
    }
}

/* ── alerts ─────────────────────────────────────────────────────────────── */

/* 0 = close_notify (clean end), -1 = anything else. */
static int handle_alert(cact_tls_t *t, const uint8_t *body, size_t len) {
    if (len < 2) { err_set("malformed alert"); return -1; }
    if (body[1] == ALERT_CLOSE_NOTIFY) {
        t->state = ST_CLOSED;
        return 0;
    }
    switch (body[1]) {
    case 10: err_set("peer rejected the parameters (unexpected_message)"); break;
    case 20: err_set("peer could not decrypt a record (bad_record_mac)"); break;
    case 47: err_set("peer sent illegal_parameter"); break;
    case 50: err_set("peer could not decode a message (decode_error)"); break;
    case 70: err_set("peer supports no TLS 1.3 (protocol_version)"); break;
    case 80: err_set("peer hit an internal error"); break;
    case 109: err_set("peer missed a required extension"); break;
    case 112: err_set("peer did not recognise the server name"); break;
    default: {
        /* Include the numeric description: it is the fastest way to tell what
         * the peer disliked without a packet capture. */
        char msg[48];
        size_t i = 0;
        const char *prefix = "peer sent a fatal alert ";
        while (*prefix && i < sizeof msg - 4) msg[i++] = *prefix++;
        uint8_t code = body[1];
        if (code >= 100) msg[i++] = (char)('0' + code / 100);
        if (code >= 10) msg[i++] = (char)('0' + (code / 10) % 10);
        msg[i++] = (char)('0' + code % 10);
        msg[i] = '\0';
        err_set(msg);
        break;
    }
    }
    return -1;
}

/* ── handshake framing ──────────────────────────────────────────────────── */

static int hs_append(cact_tls_t *t, const uint8_t *data, size_t len) {
    return append(&t->hs, &t->hs_len, &t->hs_cap, data, len);
}

/* Next handshake message.  1 = ok, 0 = clean EOF, -1 = error.
 * `body` points into the reassembly buffer with the 4-byte handshake header
 * immediately before it, so a caller can add (body - 4, len + 4) to the
 * transcript.  The pointer is valid until the next call. */
static int hs_next(cact_tls_t *t, uint8_t *msg_type, const uint8_t **body,
                   size_t *body_len) {
    for (;;) {
        if (t->hs_off > 0) {
            memmove(t->hs, t->hs + t->hs_off, t->hs_len - t->hs_off);
            t->hs_len -= t->hs_off;
            t->hs_off = 0;
        }
        if (t->hs_len >= 4) {
            size_t mlen = ((size_t)t->hs[1] << 16) | ((size_t)t->hs[2] << 8) | t->hs[3];
            if (mlen > MAX_HANDSHAKE) { err_set("handshake message too large"); return -1; }
            if (t->hs_len >= 4 + mlen) {
                *msg_type = t->hs[0];
                *body = t->hs + 4;
                *body_len = mlen;
                t->hs_off = 4 + mlen;
                return 1;
            }
        }

        uint8_t type;
        size_t len;
        int r = recv_record(t, &type, t->plain, &len);
        if (r <= 0) return r;
        if (type == CT_HANDSHAKE) {
            if (hs_append(t, t->plain, len) != 0) { err_set("out of memory"); return -1; }
        } else if (type == CT_ALERT) {
            return handle_alert(t, t->plain, len);
        } else {
            err_set("unexpected record during handshake");
            return -1;
        }
    }
}

/* ── ClientHello ────────────────────────────────────────────────────────── */

static int build_client_hello(cact_tls_t *t, const uint8_t pub[32],
                              uint8_t **out, size_t *out_len) {
    uint8_t *body = NULL, *ext = NULL;
    size_t blen = 0, bcap = 0, elen = 0, ecap = 0;
    uint8_t random[32], sid[32];

    if (cact_random(random, sizeof random) != 0 ||
        cact_random(sid, sizeof sid) != 0) {
        err_set("no randomness available");
        goto fail;
    }

    uint8_t ver[2] = {0x03, 0x03};              /* legacy_version */
    uint8_t sid_hdr = sizeof sid;
    uint8_t comp[2] = {0x01, 0x00};             /* null compression only */
    uint8_t suite[4] = {0x00, 0x02,
                        (uint8_t)(TLS_SUITE_AES128_GCM_SHA256 >> 8),
                        (uint8_t)TLS_SUITE_AES128_GCM_SHA256};

    if (append(&body, &blen, &bcap, ver, 2) != 0 ||
        append(&body, &blen, &bcap, random, 32) != 0 ||
        append(&body, &blen, &bcap, &sid_hdr, 1) != 0 ||
        append(&body, &blen, &bcap, sid, sizeof sid) != 0 ||
        append(&body, &blen, &bcap, suite, 4) != 0 ||
        append(&body, &blen, &bcap, comp, 2) != 0) goto oom;

    /* server_name */
    {
        size_t hl = strlen(t->hostname);
        uint8_t *e = NULL;
        size_t n = 0, c = 0, list_len = hl + 3;
        uint8_t list_hdr[2] = {(uint8_t)(list_len >> 8), (uint8_t)list_len};
        uint8_t ntype = 0;
        uint8_t nlen[2] = {(uint8_t)(hl >> 8), (uint8_t)hl};
        uint8_t eh[4] = {0x00, 0x00, 0, 0};

        if (append(&e, &n, &c, list_hdr, 2) != 0 ||
            append(&e, &n, &c, &ntype, 1) != 0 ||
            append(&e, &n, &c, nlen, 2) != 0 ||
            append(&e, &n, &c, t->hostname, hl) != 0) { free(e); goto oom; }
        if (n > 0xffff) { free(e); goto oom; }
        eh[2] = (uint8_t)(n >> 8);
        eh[3] = (uint8_t)n;
        int rc = append(&ext, &elen, &ecap, eh, 4);
        if (rc == 0) rc = append(&ext, &elen, &ecap, e, n);
        free(e);
        if (rc != 0) goto oom;
    }
    /* supported_groups: x25519 — body is u16(list_len) || list */
    {
        uint8_t e[8] = {0x00, 0x0a, 0x00, 0x04, 0x00, 0x02, 0x00, 0x1d};
        if (append(&ext, &elen, &ecap, e, sizeof e) != 0) goto oom;
    }
    /* signature_algorithms — body is u16(list_len) || list */
    {
        uint8_t e[6] = {0x00, 0x0d,
                        0x00, (uint8_t)(sizeof sig_schemes + 2),
                        0x00, (uint8_t)sizeof sig_schemes};
        if (append(&ext, &elen, &ecap, e, sizeof e) != 0 ||
            append(&ext, &elen, &ecap, sig_schemes, sizeof sig_schemes) != 0) goto oom;
    }
    /* supported_versions: TLS 1.3 — body is u8(list_len) || versions */
    {
        uint8_t e[7] = {0x00, 0x2b, 0x00, 0x03, 0x02, 0x03, 0x04};
        if (append(&ext, &elen, &ecap, e, sizeof e) != 0) goto oom;
    }
    /* key_share: x25519 — body is u16(shares_len) || entry(group, klen, key) */
    {
        uint8_t e[10] = {0x00, 0x33,
                         0x00, 0x26,             /* extension length: 2 + 36 */
                         0x00, 0x24,             /* shares length: 2 + 2 + 32 */
                         0x00, 0x1d,             /* group: x25519 */
                         0x00, 0x20};            /* key length: 32 */
        if (append(&ext, &elen, &ecap, e, sizeof e) != 0 ||
            append(&ext, &elen, &ecap, pub, 32) != 0) goto oom;
    }

    {
        uint8_t ext_len[2] = {(uint8_t)(elen >> 8), (uint8_t)elen};
        if (append(&body, &blen, &bcap, ext_len, 2) != 0 ||
            append(&body, &blen, &bcap, ext, elen) != 0) goto oom;
    }
    free(ext);
    ext = NULL;

    {
        uint8_t *msg = NULL;
        size_t mlen = 0, mcap = 0;
        uint8_t mh[4] = {HS_CLIENT_HELLO, (uint8_t)(blen >> 16),
                         (uint8_t)(blen >> 8), (uint8_t)blen};
        if (append(&msg, &mlen, &mcap, mh, 4) != 0 ||
            append(&msg, &mlen, &mcap, body, blen) != 0) {
            free(msg);
            goto oom;
        }
        free(body);
        *out = msg;
        *out_len = mlen;
        return 0;
    }

oom:
    err_set("out of memory");
fail:
    free(body);
    free(ext);
    return -1;
}

/* ── ServerHello ────────────────────────────────────────────────────────── */

static int parse_server_hello(const uint8_t *b, size_t len, uint8_t peer[32]) {
    size_t o;
    int have_version = 0, have_share = 0;

    if (len < 2 + 32 + 1) { err_set("truncated ServerHello"); return -1; }
    if (memcmp(b + 2, hrr_random, 32) == 0) {
        err_set("server wants another key exchange group (HelloRetryRequest)");
        return -1;
    }
    o = 2 + 32;

    uint8_t sid_len = b[o++];
    if (o + sid_len + 3 > len) { err_set("truncated ServerHello"); return -1; }
    o += sid_len;

    uint16_t suite = (uint16_t)((b[o] << 8) | b[o + 1]);
    o += 2;
    if (suite != TLS_SUITE_AES128_GCM_SHA256) {
        err_set("server picked an unsupported cipher suite");
        return -1;
    }
    o += 1;                                     /* compression */

    if (o + 2 > len) { err_set("truncated ServerHello"); return -1; }
    size_t ext_len = ((size_t)b[o] << 8) | b[o + 1];
    o += 2;
    if (o + ext_len > len) { err_set("truncated ServerHello"); return -1; }

    size_t end = o + ext_len;
    while (o + 4 <= end) {
        uint16_t type = (uint16_t)((b[o] << 8) | b[o + 1]);
        size_t elen = ((size_t)b[o + 2] << 8) | b[o + 3];
        o += 4;
        if (o + elen > end) { err_set("truncated extension"); return -1; }

        if (type == 43) {                       /* supported_versions */
            if (elen == 2 && b[o] == 0x03 && b[o + 1] == 0x04) have_version = 1;
        } else if (type == 51) {                /* key_share */
            if (elen >= 4 + 32) {
                uint16_t group = (uint16_t)((b[o] << 8) | b[o + 1]);
                size_t klen = ((size_t)b[o + 2] << 8) | b[o + 3];
                if (group == TLS_GROUP_X25519 && klen == 32) {
                    memcpy(peer, b + o + 4, 32);
                    have_share = 1;
                }
            }
        }
        o += elen;
    }

    if (!have_version) { err_set("server did not negotiate TLS 1.3"); return -1; }
    if (!have_share) { err_set("server sent no x25519 key share"); return -1; }
    return 0;
}

/* ── key derivation steps ───────────────────────────────────────────────── */

static int setup_handshake_keys(cact_tls_t *t, const uint8_t shared[32]) {
    uint8_t zeros[HASH_LEN], early[HASH_LEN], derived[HASH_LEN];
    memset(zeros, 0, sizeof zeros);

    if (hkdf_extract(zeros, sizeof zeros, zeros, sizeof zeros, early) != 0) return -1;
    if (derive_secret_empty(early, "derived", derived) != 0) return -1;
    if (hkdf_extract(derived, sizeof derived, shared, 32, t->hs_secret) != 0) return -1;

    if (derive_secret(t, t->hs_secret, "c hs traffic", t->c_hs_secret) != 0) return -1;
    if (derive_secret(t, t->hs_secret, "s hs traffic", t->s_hs_secret) != 0) return -1;
    if (traffic_keys(t->c_hs_secret, t->c_hs_key, t->c_hs_iv) != 0) return -1;
    if (traffic_keys(t->s_hs_secret, t->s_hs_key, t->s_hs_iv) != 0) return -1;
    return 0;
}

/* Called with the transcript up to and including the server's Finished. */
static int setup_application_keys(cact_tls_t *t) {
    uint8_t zeros[HASH_LEN], derived[HASH_LEN], master[HASH_LEN];
    memset(zeros, 0, sizeof zeros);

    if (derive_secret_empty(t->hs_secret, "derived", derived) != 0) return -1;
    if (hkdf_extract(derived, sizeof derived, zeros, sizeof zeros, master) != 0) return -1;

    if (derive_secret(t, master, "c ap traffic", t->c_ap_secret) != 0) return -1;
    if (derive_secret(t, master, "s ap traffic", t->s_ap_secret) != 0) return -1;
    if (traffic_keys(t->c_ap_secret, t->c_ap_key, t->c_ap_iv) != 0) return -1;
    if (traffic_keys(t->s_ap_secret, t->s_ap_key, t->s_ap_iv) != 0) return -1;
    return 0;
}

/* Rekey one direction after a KeyUpdate (RFC 8446 §7.2). */
static int key_update(cact_tls_t *t, int from_peer) {
    uint8_t *secret = from_peer ? t->s_ap_secret : t->c_ap_secret;
    uint8_t next[HASH_LEN];

    if (derive_secret_empty(secret, "traffic upd", next) != 0) return -1;
    memcpy(secret, next, HASH_LEN);
    if (from_peer) {
        if (traffic_keys(secret, t->s_ap_key, t->s_ap_iv) != 0) return -1;
        t->rd_seq = 0;
    } else {
        if (traffic_keys(secret, t->c_ap_key, t->c_ap_iv) != 0) return -1;
        t->wr_seq = 0;
    }
    return 0;
}

/* ── Finished ───────────────────────────────────────────────────────────── */

static int finished_verify_data(cact_tls_t *t, const uint8_t secret[HASH_LEN],
                                uint8_t out[HASH_LEN]) {
    uint8_t key[HASH_LEN], th[HASH_LEN];
    if (expand_label(secret, "finished", NULL, 0, key, HASH_LEN) != 0) return -1;
    if (transcript_hash(t, th) != 0) return -1;
    return cact_hmac_sha256(key, HASH_LEN, th, HASH_LEN, out);
}

static int bytes_equal(const uint8_t *a, const uint8_t *b, size_t n) {
    uint8_t diff = 0;
    for (size_t i = 0; i < n; i++) diff |= (uint8_t)(a[i] ^ b[i]);
    return diff == 0;
}

/* ── Certificate / CertificateVerify ────────────────────────────────────── */

static int handle_certificate(cact_tls_t *t, const uint8_t *b, size_t len) {
    if (len < 4) { err_set("truncated Certificate"); return -1; }
    size_t o = 0;
    size_t ctx_len = b[o++];
    if (o + ctx_len + 3 > len) { err_set("truncated Certificate"); return -1; }
    o += ctx_len;
    size_t list_len = ((size_t)b[o] << 16) | ((size_t)b[o + 1] << 8) | b[o + 2];
    o += 3;
    if (o + list_len > len) { err_set("truncated Certificate"); return -1; }

    size_t end = o + list_len;
    while (o < end) {
        if (o + 3 > end) { err_set("truncated certificate entry"); return -1; }
        size_t clen = ((size_t)b[o] << 16) | ((size_t)b[o + 1] << 8) | b[o + 2];
        o += 3;
        if (o + clen > end || clen == 0) { err_set("truncated certificate"); return -1; }
        if (append(&t->chain, &t->chain_len, &t->chain_cap, b + o, clen) != 0) {
            err_set("out of memory");
            return -1;
        }
        o += clen;
        if (o + 2 > end) { err_set("truncated certificate entry"); return -1; }
        size_t ext_len = ((size_t)b[o] << 8) | b[o + 1];
        o += 2;
        if (o + ext_len > end) { err_set("truncated certificate extension"); return -1; }
        o += ext_len;
    }
    if (t->chain_len == 0) { err_set("server sent an empty certificate chain"); return -1; }
    return 0;
}

/* The signed message: 64 spaces || context || 0x00 || transcript hash. */
#define CV_CONTEXT "TLS 1.3, server CertificateVerify"

static int handle_certificate_verify(cact_tls_t *t, const uint8_t *b, size_t len) {
    if (len < 4) { err_set("truncated CertificateVerify"); return -1; }
    uint16_t scheme = (uint16_t)((b[0] << 8) | b[1]);
    size_t sig_len = ((size_t)b[2] << 8) | b[3];
    if (4 + sig_len > len) { err_set("truncated CertificateVerify"); return -1; }
    if (t->chain_len == 0) { err_set("CertificateVerify without a certificate"); return -1; }
    if (!roots_der || roots_len == 0) {
        err_set("no trust anchors: cannot verify the certificate");
        return -1;
    }

    /* The transcript for this signature excludes the CertificateVerify itself. */
    uint8_t th[HASH_LEN];
    if (transcript_hash(t, th) != 0) return -1;

    /* 64 spaces || context string || 0x00 || transcript hash */
    static const char ctx[] = CV_CONTEXT;
    const size_t ctx_len = sizeof ctx - 1;
    uint8_t msg[64 + sizeof ctx - 1 + 1 + HASH_LEN];
    const size_t msg_len = 64 + ctx_len + 1 + HASH_LEN;
    memset(msg, 0x20, 64);
    memcpy(msg + 64, ctx, ctx_len);
    msg[64 + ctx_len] = 0x00;
    memcpy(msg + 64 + ctx_len + 1, th, HASH_LEN);

    if (cact_x509_verify(t->chain, t->chain_len, roots_der, roots_len,
                         t->hostname, (uint64_t)time(NULL), scheme,
                         msg, msg_len, b + 4, sig_len) != 0) {
        err_set("certificate rejected (chain, hostname, validity or handshake signature)");
        return -1;
    }
    return 0;
}

/* ── handshake driver ───────────────────────────────────────────────────── */

static int do_handshake(cact_tls_t *t) {
    uint8_t priv[32], pub[32], peer[32], shared[32];
    uint8_t *ch = NULL;
    size_t ch_len = 0;
    uint8_t msg_type;
    const uint8_t *body;
    size_t body_len;

    if (cact_random(priv, sizeof priv) != 0) {
        err_set("no randomness available");
        return -1;
    }
    /* Public key from the private scalar via the service's scalar
     * multiplication against the base point: the kernel never generates or
     * keeps this key. */
    if (cact_x25519_derive(priv, x25519_basepoint, pub) != 0) {
        err_set("could not derive the ephemeral public key");
        return -1;
    }

    if (build_client_hello(t, pub, &ch, &ch_len) != 0) return -1;
    if (tr_add(t, ch, ch_len) != 0) { free(ch); err_set("out of memory"); return -1; }
    if (send_record(t, CT_HANDSHAKE, ch, ch_len) != 0) { free(ch); return -1; }
    free(ch);

    /* ServerHello */
    int r = hs_next(t, &msg_type, &body, &body_len);
    if (r <= 0) return r;
    if (msg_type != HS_SERVER_HELLO) { err_set("expected ServerHello"); return -1; }
    if (parse_server_hello(body, body_len, peer) != 0) return -1;
    if (tr_add(t, body - 4, body_len + 4) != 0) { err_set("out of memory"); return -1; }

    if (cact_x25519_derive(priv, peer, shared) != 0) {
        err_set("key exchange failed");
        return -1;
    }
    if (setup_handshake_keys(t, shared) != 0) {
        err_set("key schedule failed");
        return -1;
    }
    memset(priv, 0, sizeof priv);
    memset(shared, 0, sizeof shared);

    /* Everything from here on is encrypted. */
    memcpy(t->rd_key, t->s_hs_key, KEY_LEN);
    memcpy(t->rd_iv, t->s_hs_iv, IV_LEN);
    memcpy(t->wr_key, t->c_hs_key, KEY_LEN);
    memcpy(t->wr_iv, t->c_hs_iv, IV_LEN);
    t->rd_seq = 0;
    t->wr_seq = 0;
    t->rd_enc = 1;
    t->wr_enc = 1;      /* our Finished is already encrypted */

    int saw_ee = 0, saw_cert = 0, saw_cv = 0;

    /* EncryptedExtensions, [CertificateRequest], Certificate,
     * CertificateVerify, Finished. */
    for (;;) {
        r = hs_next(t, &msg_type, &body, &body_len);
        if (r <= 0) return r;

        if (msg_type == HS_ENCRYPTED_EXTENSIONS) {
            if (tr_add(t, body - 4, body_len + 4) != 0) goto oom;
            saw_ee = 1;
        } else if (msg_type == HS_CERTIFICATE) {
            if (handle_certificate(t, body, body_len) != 0) return -1;
            if (tr_add(t, body - 4, body_len + 4) != 0) goto oom;
            saw_cert = 1;
        } else if (msg_type == HS_CERTIFICATE_VERIFY) {
            if (handle_certificate_verify(t, body, body_len) != 0) return -1;
            if (tr_add(t, body - 4, body_len + 4) != 0) goto oom;
            saw_cv = 1;
        } else if (msg_type == HS_FINISHED) {
            uint8_t expect[HASH_LEN];
            if (body_len != HASH_LEN) { err_set("malformed Finished"); return -1; }
            if (finished_verify_data(t, t->s_hs_secret, expect) != 0) return -1;
            if (!bytes_equal(expect, body, HASH_LEN)) {
                err_set("server Finished does not match the transcript");
                return -1;
            }
            if (tr_add(t, body - 4, body_len + 4) != 0) goto oom;
            break;
        } else {
            err_set("unexpected handshake message");
            return -1;
        }
    }

    if (!saw_ee || !saw_cert || !saw_cv) {
        err_set("server omitted part of the authenticated handshake");
        return -1;
    }

    /* Application keys are derived from the transcript through the server's
     * Finished, before our own Finished is added. */
    if (setup_application_keys(t) != 0) { err_set("key schedule failed"); return -1; }

    /* Our Finished goes out under the handshake write keys. */
    {
        uint8_t verify_data[HASH_LEN];
        uint8_t msg[4 + HASH_LEN];
        if (finished_verify_data(t, t->c_hs_secret, verify_data) != 0) return -1;
        msg[0] = HS_FINISHED;
        msg[1] = 0;
        msg[2] = 0;
        msg[3] = HASH_LEN;
        memcpy(msg + 4, verify_data, HASH_LEN);
        if (send_record(t, CT_HANDSHAKE, msg, sizeof msg) != 0) return -1;
    }

    /* Switch to application traffic keys. */
    memcpy(t->rd_key, t->s_ap_key, KEY_LEN);
    memcpy(t->rd_iv, t->s_ap_iv, IV_LEN);
    memcpy(t->wr_key, t->c_ap_key, KEY_LEN);
    memcpy(t->wr_iv, t->c_ap_iv, IV_LEN);
    t->rd_seq = 0;
    t->wr_seq = 0;
    t->wr_enc = 1;
    t->state = ST_APP;

    /* The handshake is over: the transcript and the certificate chain are no
     * longer needed and should not linger in memory. */
    free(t->tr);
    t->tr = NULL;
    t->tr_len = t->tr_cap = 0;
    free(t->chain);
    t->chain = NULL;
    t->chain_len = t->chain_cap = 0;
    return 0;

oom:
    err_set("out of memory");
    return -1;
}

/* ── post-handshake messages ────────────────────────────────────────────── */

static int handle_post_handshake(cact_tls_t *t, const uint8_t *data, size_t len) {
    size_t o = 0;
    while (o + 4 <= len) {
        uint8_t mt = data[o];
        size_t mlen = ((size_t)data[o + 1] << 16) | ((size_t)data[o + 2] << 8) | data[o + 3];
        o += 4;
        if (o + mlen > len) { err_set("truncated post-handshake message"); return -1; }

        if (mt == HS_NEW_SESSION_TICKET) {
            /* No resumption support; tickets are simply dropped. */
        } else if (mt == HS_KEY_UPDATE) {
            if (mlen != 1) { err_set("malformed KeyUpdate"); return -1; }
            uint8_t requested = data[o];
            if (key_update(t, 1) != 0) { err_set("KeyUpdate failed"); return -1; }
            if (requested == 1) {
                uint8_t msg[5] = {HS_KEY_UPDATE, 0x00, 0x00, 0x01, 0x00};
                if (send_record(t, CT_HANDSHAKE, msg, sizeof msg) != 0) return -1;
                if (key_update(t, 0) != 0) { err_set("KeyUpdate failed"); return -1; }
            }
        } else {
            err_set("unexpected post-handshake message");
            return -1;
        }
        o += mlen;
    }
    return 0;
}

/* ── public API ─────────────────────────────────────────────────────────── */

int cact_tls_set_roots(const void *der, size_t len) {
    free(roots_der);
    roots_der = NULL;
    roots_len = 0;
    if (!der || len == 0) return 0;
    roots_der = (uint8_t *)malloc(len);
    if (!roots_der) { err_set("out of memory"); return -1; }
    memcpy(roots_der, der, len);
    roots_len = len;
    return 0;
}

static int ensure_roots(void) {
    if (roots_der && roots_len) return 0;
    if (cact_tls_load_default_roots(&roots_der, &roots_len) <= 0) {
        roots_der = NULL;
        roots_len = 0;
        err_set("no CA bundle: looked for /etc/ssl/certs/ca-certificates.crt, "
                "/etc/ca-certificates.crt and /usr/share/ca-certificates.crt");
        return -1;
    }
    return 0;
}

cact_tls_t *cact_tls_connect(int fd, const char *hostname) {
    if (fd < 0 || !hostname || !hostname[0]) { err_set("invalid argument"); return NULL; }
    if ((size_t)strlen(hostname) + 1 > sizeof(((cact_tls_t *)0)->hostname)) {
        err_set("hostname too long");
        return NULL;
    }
    if (ensure_roots() != 0) return NULL;

    cact_tls_t *t = (cact_tls_t *)calloc(1, sizeof *t);
    if (!t) { err_set("out of memory"); return NULL; }

    t->fd = fd;
    t->state = ST_HANDSHAKE;
    t->rec = (uint8_t *)malloc(5 + MAX_CIPHER);
    t->plain = (uint8_t *)malloc(MAX_CIPHER);
    t->app = (uint8_t *)malloc(MAX_CIPHER);
    if (!t->rec || !t->plain || !t->app) {
        err_set("out of memory");
        free(t->rec); free(t->plain); free(t->app); free(t);
        return NULL;
    }
    strcpy(t->hostname, hostname);

    if (do_handshake(t) != 0) {
        int saved_errno = errno;
        free(t->rec); free(t->plain); free(t->app);
        free(t->hs); free(t->tr); free(t->chain);
        free(t);
        errno = saved_errno;
        return NULL;
    }
    return t;
}

long cact_tls_read(cact_tls_t *t, void *buf, size_t len) {
    if (!t || !buf) { errno = EINVAL; return -1; }
    if (t->state == ST_CLOSED) return 0;
    if (len == 0) return 0;

    for (;;) {
        if (t->app_off < t->app_len) {
            size_t avail = t->app_len - t->app_off;
            size_t take = avail < len ? avail : len;
            memcpy(buf, t->app + t->app_off, take);
            t->app_off += take;
            if (t->app_off == t->app_len) t->app_off = t->app_len = 0;
            return (long)take;
        }

        uint8_t type;
        size_t got;
        int r = recv_record(t, &type, t->plain, &got);
        if (r <= 0) {
            if (r < 0) errno = EIO;
            t->state = (r == 0) ? ST_CLOSED : t->state;
            return r;
        }

        if (type == CT_APP) {
            size_t take = got < len ? got : len;
            memcpy(buf, t->plain, take);
            if (take < got) {
                memcpy(t->app, t->plain + take, got - take);
                t->app_len = got - take;
                t->app_off = 0;
            }
            return (long)take;
        }
        if (type == CT_HANDSHAKE) {
            if (handle_post_handshake(t, t->plain, got) != 0) { errno = EIO; return -1; }
            continue;
        }
        if (type == CT_ALERT) {
            if (handle_alert(t, t->plain, got) == 0) return 0;
            errno = EIO;
            return -1;
        }
        err_set("unexpected record type");
        errno = EIO;
        return -1;
    }
}

long cact_tls_write(cact_tls_t *t, const void *buf, size_t len) {
    if (!t || (!buf && len)) { errno = EINVAL; return -1; }
    if (t->state == ST_CLOSED) { errno = EPIPE; return -1; }

    /* One record per call keeps the code simple; callers send small requests. */
    size_t take = len < MAX_PLAIN ? len : MAX_PLAIN;
    if (send_record(t, CT_APP, (const uint8_t *)buf, take) != 0) {
        errno = EIO;
        return -1;
    }
    return (long)take;
}

int cact_tls_close(cact_tls_t *t) {
    if (!t) { errno = EINVAL; return -1; }

    int rc = 0;
    if (t->state != ST_CLOSED) {
        uint8_t alert[2] = {1 /* warning */, ALERT_CLOSE_NOTIFY};
        rc = send_record(t, CT_ALERT, alert, sizeof alert);
        t->state = ST_CLOSED;
    }
    free(t->rec);
    free(t->plain);
    free(t->app);
    free(t->hs);
    free(t->tr);
    free(t->chain);
    free(t);
    return rc;
}
