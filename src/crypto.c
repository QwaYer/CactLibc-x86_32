#include "crypto.h"
#include "nodeio.h"
#include "ioctl_abi.h"
#include "errno.h"
#include "string.h"
#include <stdint.h>

/* CactLibc crypto — thin relay onto the in-kernel crypto service (/dev/crypto).
 * Every helper builds the matching cact_crypt_*_arg_t from ioctl_abi.h, issues
 * one ioctl and maps the kernel's -errno onto the POSIX convention.
 *
 * The kernel treats the *arg structure as the whole request/response: it
 * copies it in, runs the primitive and copies it back out.  Fields the ABI
 * embeds in the struct (hash digest, HMAC tag, KX pub/priv/shared) therefore
 * have to be copied out of our local copy into the caller's buffers; the ones
 * that are plain pointers (HKDF out, AEAD out) are written by the kernel
 * directly into caller memory. */

/* One-shot ioctl on /dev/crypto.  Returns 0 or -1 with errno set. */
static int crypt_cmd(unsigned long cmd, void *arg) {
    int r = nio_dev_cmd("crypto", cmd, arg);
    if (r < 0) return nio_map(r);
    return 0;
}

int cact_random(void *buf, size_t len) {
    if (!buf && len) { errno = EINVAL; return -1; }
    cact_crypt_random_arg_t a;
    a.buf = (uint8_t *)buf;
    a.len = (uint32_t)len;
    return crypt_cmd(CACT_CRYPTCTL_RANDOM, &a);
}

/* ── hashes ─────────────────────────────────────────────────────────────── */

static int crypt_hash(uint32_t alg, size_t digest_len,
                      const void *data, size_t len, uint8_t *out) {
    if ((!data && len) || !out) { errno = EINVAL; return -1; }
    cact_crypt_hash_arg_t a;
    a.alg = alg;
    a.data = (const uint8_t *)data;
    a.data_len = (uint32_t)len;
    int r = crypt_cmd(CACT_CRYPTCTL_HASH, &a);
    if (r == 0) memcpy(out, a.digest, digest_len);
    return r;
}

int cact_sha256(const void *data, size_t len, uint8_t out[CACT_SHA256_LEN]) {
    return crypt_hash(CACT_CRYPT_SHA256, CACT_SHA256_LEN, data, len, out);
}

int cact_sha384(const void *data, size_t len, uint8_t out[CACT_SHA384_LEN]) {
    return crypt_hash(CACT_CRYPT_SHA384, CACT_SHA384_LEN, data, len, out);
}

/* ── HMAC ───────────────────────────────────────────────────────────────── */

static void hmac_fill(cact_crypt_hmac_arg_t *a, uint32_t alg,
                      const void *key, size_t key_len,
                      const void *data, size_t data_len) {
    a->alg = alg;
    a->key = (const uint8_t *)key;
    a->key_len = (uint32_t)key_len;
    a->data = (const uint8_t *)data;
    a->data_len = (uint32_t)data_len;
    memset(a->tag, 0, sizeof(a->tag));
}

static int crypt_hmac_sign(uint32_t alg, size_t tag_len,
                           const void *key, size_t key_len,
                           const void *data, size_t data_len,
                           uint8_t *out) {
    if ((!key && key_len) || (!data && data_len) || !out) {
        errno = EINVAL;
        return -1;
    }
    cact_crypt_hmac_arg_t a;
    hmac_fill(&a, alg, key, key_len, data, data_len);
    int r = crypt_cmd(CACT_CRYPTCTL_HMAC, &a);
    if (r == 0) memcpy(out, a.tag, tag_len);
    return r;
}

static int crypt_hmac_verify(uint32_t alg, size_t tag_len,
                             const void *key, size_t key_len,
                             const void *data, size_t data_len,
                             const uint8_t *tag) {
    if ((!key && key_len) || (!data && data_len) || !tag) {
        errno = EINVAL;
        return -1;
    }
    cact_crypt_hmac_arg_t a;
    hmac_fill(&a, alg, key, key_len, data, data_len);
    memcpy(a.tag, tag, tag_len);
    return crypt_cmd(CACT_CRYPTCTL_HMAC_VERIFY, &a);
}

int cact_hmac_sha256(const void *key, size_t key_len,
                     const void *data, size_t data_len,
                     uint8_t out[CACT_SHA256_LEN]) {
    return crypt_hmac_sign(CACT_CRYPT_SHA256, CACT_SHA256_LEN,
                           key, key_len, data, data_len, out);
}

int cact_hmac_sha384(const void *key, size_t key_len,
                     const void *data, size_t data_len,
                     uint8_t out[CACT_SHA384_LEN]) {
    return crypt_hmac_sign(CACT_CRYPT_SHA384, CACT_SHA384_LEN,
                           key, key_len, data, data_len, out);
}

int cact_hmac_sha256_verify(const void *key, size_t key_len,
                            const void *data, size_t data_len,
                            const uint8_t tag[CACT_SHA256_LEN]) {
    return crypt_hmac_verify(CACT_CRYPT_SHA256, CACT_SHA256_LEN,
                             key, key_len, data, data_len, tag);
}

int cact_hmac_sha384_verify(const void *key, size_t key_len,
                            const void *data, size_t data_len,
                            const uint8_t tag[CACT_SHA384_LEN]) {
    return crypt_hmac_verify(CACT_CRYPT_SHA384, CACT_SHA384_LEN,
                             key, key_len, data, data_len, tag);
}

/* ── HKDF ───────────────────────────────────────────────────────────────── */

static int crypt_hkdf(uint32_t alg, const void *salt, size_t salt_len,
                      const void *ikm, size_t ikm_len,
                      const void *info, size_t info_len,
                      uint8_t *out, size_t out_len) {
    if ((!salt && salt_len) || (!ikm && ikm_len) || (!info && info_len) ||
        (!out && out_len)) {
        errno = EINVAL;
        return -1;
    }
    cact_crypt_hkdf_arg_t a;
    a.alg = alg;
    a.salt = (const uint8_t *)salt;
    a.salt_len = (uint32_t)salt_len;
    a.ikm = (const uint8_t *)ikm;
    a.ikm_len = (uint32_t)ikm_len;
    a.info = (const uint8_t *)info;
    a.info_len = (uint32_t)info_len;
    a.out = out;
    a.out_len = (uint32_t)out_len;
    return crypt_cmd(CACT_CRYPTCTL_HKDF, &a);
}

int cact_hkdf_sha256(const void *salt, size_t salt_len,
                     const void *ikm, size_t ikm_len,
                     const void *info, size_t info_len,
                     uint8_t *out, size_t out_len) {
    return crypt_hkdf(CACT_CRYPT_SHA256, salt, salt_len, ikm, ikm_len,
                      info, info_len, out, out_len);
}

int cact_hkdf_sha384(const void *salt, size_t salt_len,
                     const void *ikm, size_t ikm_len,
                     const void *info, size_t info_len,
                     uint8_t *out, size_t out_len) {
    return crypt_hkdf(CACT_CRYPT_SHA384, salt, salt_len, ikm, ikm_len,
                      info, info_len, out, out_len);
}

/* ── AES-GCM ────────────────────────────────────────────────────────────── */

static int crypt_gcm(uint32_t alg, uint32_t op, size_t key_len,
                     const uint8_t *key, const uint8_t *nonce,
                     const void *aad, size_t aad_len,
                     const void *in, size_t in_len,
                     void *out, size_t out_cap) {
    if (!key || !nonce || (!aad && aad_len) || (!in && in_len) || !out) {
        errno = EINVAL;
        return -1;
    }
    if (key_len != 16 && key_len != 32) { errno = EINVAL; return -1; }
    cact_crypt_aead_arg_t a;
    a.alg = alg;
    a.op = op;
    a.key = key;
    a.key_len = (uint32_t)key_len;
    memcpy(a.nonce, nonce, CACT_GCM_NONCE_LEN);
    a.aad = (const uint8_t *)aad;
    a.aad_len = (uint32_t)aad_len;
    a.in = (const uint8_t *)in;
    a.in_len = (uint32_t)in_len;
    a.out = (uint8_t *)out;
    a.out_cap = (uint32_t)out_cap;
    a.out_len = 0;
    return crypt_cmd(CACT_CRYPTCTL_AEAD, &a);
}

int cact_aes128_gcm_seal(const uint8_t key[16], const uint8_t nonce[CACT_GCM_NONCE_LEN],
                         const void *aad, size_t aad_len,
                         const void *in, size_t in_len,
                         void *out, size_t out_cap) {
    return crypt_gcm(CACT_CRYPT_AES128_GCM, CACT_CRYPT_OP_SEAL, 16, key, nonce,
                     aad, aad_len, in, in_len, out, out_cap);
}

int cact_aes256_gcm_seal(const uint8_t key[32], const uint8_t nonce[CACT_GCM_NONCE_LEN],
                         const void *aad, size_t aad_len,
                         const void *in, size_t in_len,
                         void *out, size_t out_cap) {
    return crypt_gcm(CACT_CRYPT_AES256_GCM, CACT_CRYPT_OP_SEAL, 32, key, nonce,
                     aad, aad_len, in, in_len, out, out_cap);
}

int cact_aes128_gcm_open(const uint8_t key[16], const uint8_t nonce[CACT_GCM_NONCE_LEN],
                         const void *aad, size_t aad_len,
                         const void *in, size_t in_len,
                         void *out, size_t out_cap) {
    return crypt_gcm(CACT_CRYPT_AES128_GCM, CACT_CRYPT_OP_OPEN, 16, key, nonce,
                     aad, aad_len, in, in_len, out, out_cap);
}

int cact_aes256_gcm_open(const uint8_t key[32], const uint8_t nonce[CACT_GCM_NONCE_LEN],
                         const void *aad, size_t aad_len,
                         const void *in, size_t in_len,
                         void *out, size_t out_cap) {
    return crypt_gcm(CACT_CRYPT_AES256_GCM, CACT_CRYPT_OP_OPEN, 32, key, nonce,
                     aad, aad_len, in, in_len, out, out_cap);
}

/* ── key exchange ───────────────────────────────────────────────────────── */

int cact_x25519_keypair(uint8_t pub[CACT_X25519_KEY_LEN],
                        uint8_t priv[CACT_X25519_KEY_LEN]) {
    if (!pub || !priv) { errno = EINVAL; return -1; }
    cact_crypt_kx_keygen_arg_t a;
    a.alg = CACT_CRYPT_KX_X25519;
    int r = crypt_cmd(CACT_CRYPTCTL_KX_KEYGEN, &a);
    if (r == 0) {
        memcpy(pub, a.pub, CACT_X25519_KEY_LEN);
        memcpy(priv, a.priv, CACT_X25519_KEY_LEN);
    }
    return r;
}

static int crypt_kx_derive(uint32_t alg, size_t peer_len,
                           const uint8_t *priv, const uint8_t *peer_pub,
                           uint8_t *shared) {
    if (!priv || !peer_pub || !shared) { errno = EINVAL; return -1; }
    cact_crypt_kx_derive_arg_t a;
    a.alg = alg;
    memcpy(a.priv, priv, sizeof(a.priv));
    memset(a.peer_pub, 0, sizeof(a.peer_pub));
    memcpy(a.peer_pub, peer_pub, peer_len);
    int r = crypt_cmd(CACT_CRYPTCTL_KX_DERIVE, &a);
    if (r == 0) memcpy(shared, a.shared, CACT_X25519_KEY_LEN);
    return r;
}

int cact_x25519_derive(const uint8_t priv[CACT_X25519_KEY_LEN],
                       const uint8_t peer_pub[CACT_X25519_KEY_LEN],
                       uint8_t shared[CACT_X25519_KEY_LEN]) {
    return crypt_kx_derive(CACT_CRYPT_KX_X25519, CACT_X25519_KEY_LEN,
                           priv, peer_pub, shared);
}

int cact_p256_keypair(uint8_t pub[CACT_P256_PUB_LEN],
                      uint8_t priv[CACT_X25519_KEY_LEN]) {
    if (!pub || !priv) { errno = EINVAL; return -1; }
    cact_crypt_kx_keygen_arg_t a;
    a.alg = CACT_CRYPT_KX_P256;
    int r = crypt_cmd(CACT_CRYPTCTL_KX_KEYGEN, &a);
    if (r == 0) {
        memcpy(pub, a.pub, CACT_P256_PUB_LEN);
        memcpy(priv, a.priv, CACT_X25519_KEY_LEN);
    }
    return r;
}

int cact_p256_derive(const uint8_t priv[CACT_X25519_KEY_LEN],
                     const uint8_t peer_pub[CACT_P256_PUB_LEN],
                     uint8_t shared[CACT_X25519_KEY_LEN]) {
    return crypt_kx_derive(CACT_CRYPT_KX_P256, CACT_P256_PUB_LEN,
                           priv, peer_pub, shared);
}

/* ── signature verification ─────────────────────────────────────────────── */

int cact_sig_verify(uint32_t scheme,
                    const void *pubkey, size_t pubkey_len,
                    const void *msg, size_t msg_len,
                    const void *sig, size_t sig_len) {
    if (!pubkey || !sig || pubkey_len == 0 || sig_len == 0) {
        errno = EINVAL;
        return -1;
    }
    if (!msg && msg_len) { errno = EINVAL; return -1; }
    if (scheme > CACT_SIG_RSA_PSS_SHA512) { errno = EINVAL; return -1; }

    cact_crypt_sig_verify_arg_t a;
    a.scheme = scheme;
    a.pubkey = (const uint8_t *)pubkey;
    a.pubkey_len = (uint32_t)pubkey_len;
    a.msg = (const uint8_t *)msg;
    a.msg_len = (uint32_t)msg_len;
    a.sig = (const uint8_t *)sig;
    a.sig_len = (uint32_t)sig_len;

    int r = nio_dev_cmd("crypto", CACT_CRYPTCTL_SIG_VERIFY, &a);
    if (r == 0) return 0;
    /* The kernel distinguishes "not a valid signature" (-1) from "malformed
     * request" (-EINVAL); keep that distinction visible in errno. */
    if (r == -EINVAL) { errno = EINVAL; return -1; }
    if (r == -1) { errno = EBADMSG; return -1; }
    return nio_map(r);
}

/* ── certificate chains ─────────────────────────────────────────────────── */

int cact_x509_verify(const void *chain, size_t chain_len,
                     const void *roots, size_t roots_len,
                     const char *hostname, uint64_t unix_time,
                     uint32_t tls_scheme,
                     const void *hs_msg, size_t hs_msg_len,
                     const void *hs_sig, size_t hs_sig_len) {
    if (!chain || !chain_len || !roots || !roots_len || !hostname || !hostname[0]) {
        errno = EINVAL;
        return -1;
    }
    if ((!hs_msg && hs_msg_len) || (!hs_sig && hs_sig_len)) {
        errno = EINVAL;
        return -1;
    }

    cact_crypt_x509_verify_arg_t a;
    a.chain       = (const uint8_t *)chain;
    a.chain_len   = (uint32_t)chain_len;
    a.roots       = (const uint8_t *)roots;
    a.roots_len   = (uint32_t)roots_len;
    a.hostname    = hostname;
    a.unix_time   = unix_time;
    a.tls_scheme  = tls_scheme;
    a.hs_msg      = (const uint8_t *)hs_msg;
    a.hs_msg_len  = (uint32_t)hs_msg_len;
    a.hs_sig      = (const uint8_t *)hs_sig;
    a.hs_sig_len  = (uint32_t)hs_sig_len;

    int r = nio_dev_cmd("crypto", CACT_CRYPTCTL_X509_VERIFY, &a);
    if (r == 0) return 0;
    if (r == -EINVAL) { errno = EINVAL; return -1; }
    if (r == -1) { errno = EBADMSG; return -1; }
    return nio_map(r);
}
