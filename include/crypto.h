#ifndef _CACT_CRYPTO_H
#define _CACT_CRYPTO_H

/* CactLibc crypto — the C face of the in-kernel crypto service (/dev/crypto).
 *
 * The primitives are the same ones the kernel's rustls provider ships:
 * SHA-256/384, HMAC, HKDF, AES-GCM, X25519 and P-256, plus the kernel RNG.
 * Every call is a self-contained one-shot: no contexts to allocate, nothing to
 * free, all pointers are caller-owned buffers.  Data buffers may be up to 1 MiB
 * (the kernel-side per-operation cap).
 *
 * All functions return 0 on success and -1 with errno set on failure.
 */

#include <stddef.h>
#include <stdint.h>

#define CACT_SHA256_LEN     32
#define CACT_SHA384_LEN     48
#define CACT_HMAC_MAX_LEN   64   /* SHA-384 tag */
#define CACT_GCM_NONCE_LEN  12
#define CACT_GCM_TAG_LEN    16
#define CACT_X25519_KEY_LEN 32
#define CACT_P256_PUB_LEN   65   /* uncompressed SEC1 point */

/* ── random ─────────────────────────────────────────────────────────────── */
/* Fill `buf` with random bytes from the kernel RNG. */
int cact_random(void *buf, size_t len);

/* ── hashes ─────────────────────────────────────────────────────────────── */
int cact_sha256(const void *data, size_t len, uint8_t out[CACT_SHA256_LEN]);
int cact_sha384(const void *data, size_t len, uint8_t out[CACT_SHA384_LEN]);

/* ── HMAC ───────────────────────────────────────────────────────────────── */
int cact_hmac_sha256(const void *key, size_t key_len,
                     const void *data, size_t data_len,
                     uint8_t out[CACT_SHA256_LEN]);
int cact_hmac_sha384(const void *key, size_t key_len,
                     const void *data, size_t data_len,
                     uint8_t out[CACT_SHA384_LEN]);

/* Constant-time tag comparison in the kernel; 0 = match, -1 + EIO = mismatch. */
int cact_hmac_sha256_verify(const void *key, size_t key_len,
                            const void *data, size_t data_len,
                            const uint8_t tag[CACT_SHA256_LEN]);
int cact_hmac_sha384_verify(const void *key, size_t key_len,
                            const void *data, size_t data_len,
                            const uint8_t tag[CACT_SHA384_LEN]);

/* ── HKDF (extract + expand) ────────────────────────────────────────────── */
/* `salt`/`info` may be NULL when the matching length is 0.  out_len is capped
 * by the kernel at 255 * hash length (8160 / 12240 bytes). */
int cact_hkdf_sha256(const void *salt, size_t salt_len,
                     const void *ikm, size_t ikm_len,
                     const void *info, size_t info_len,
                     uint8_t *out, size_t out_len);
int cact_hkdf_sha384(const void *salt, size_t salt_len,
                     const void *ikm, size_t ikm_len,
                     const void *info, size_t info_len,
                     uint8_t *out, size_t out_len);

/* ── AES-GCM (AEAD) ─────────────────────────────────────────────────────── */
/* SEAL: `out` receives ciphertext||tag (in_len + 16 bytes). */
int cact_aes128_gcm_seal(const uint8_t key[16], const uint8_t nonce[CACT_GCM_NONCE_LEN],
                         const void *aad, size_t aad_len,
                         const void *in, size_t in_len,
                         void *out, size_t out_cap);
int cact_aes256_gcm_seal(const uint8_t key[32], const uint8_t nonce[CACT_GCM_NONCE_LEN],
                         const void *aad, size_t aad_len,
                         const void *in, size_t in_len,
                         void *out, size_t out_cap);

/* OPEN: `in` is ciphertext||tag (in_len >= 16); `out` receives in_len - 16
 * bytes of plaintext.  Returns -1 + EIO when the tag does not verify. */
int cact_aes128_gcm_open(const uint8_t key[16], const uint8_t nonce[CACT_GCM_NONCE_LEN],
                         const void *aad, size_t aad_len,
                         const void *in, size_t in_len,
                         void *out, size_t out_cap);
int cact_aes256_gcm_open(const uint8_t key[32], const uint8_t nonce[CACT_GCM_NONCE_LEN],
                         const void *aad, size_t aad_len,
                         const void *in, size_t in_len,
                         void *out, size_t out_cap);

/* ── key exchange ───────────────────────────────────────────────────────── */
int cact_x25519_keypair(uint8_t pub[CACT_X25519_KEY_LEN],
                        uint8_t priv[CACT_X25519_KEY_LEN]);
int cact_x25519_derive(const uint8_t priv[CACT_X25519_KEY_LEN],
                       const uint8_t peer_pub[CACT_X25519_KEY_LEN],
                       uint8_t shared[CACT_X25519_KEY_LEN]);

int cact_p256_keypair(uint8_t pub[CACT_P256_PUB_LEN],
                      uint8_t priv[CACT_X25519_KEY_LEN]);
int cact_p256_derive(const uint8_t priv[CACT_X25519_KEY_LEN],
                     const uint8_t peer_pub[CACT_P256_PUB_LEN],
                     uint8_t shared[CACT_X25519_KEY_LEN]);

/* Remote TLS is not a kernel service: a TLS 1.3 client is being built on top of
 * exactly these primitives, with the session keys living in the calling
 * process.  The kernel keeps its own TLS for its own use only. */

/* ── signature verification ─────────────────────────────────────────────── */
/* Schemes — the same constants as CACT_SIG_* in ioctl_abi.h. */
#define CACT_SIG_ECDSA_P256_SHA256 0
#define CACT_SIG_ECDSA_P384_SHA384 1
#define CACT_SIG_RSA_PKCS1_SHA256  2
#define CACT_SIG_RSA_PKCS1_SHA384  3
#define CACT_SIG_RSA_PKCS1_SHA512  4
#define CACT_SIG_RSA_PSS_SHA256    5
#define CACT_SIG_RSA_PSS_SHA384    6
#define CACT_SIG_RSA_PSS_SHA512    7

/* Verify `sig` over `msg` under `pubkey`.  The key is passed exactly as a
 * certificate carries it — the SubjectPublicKeyInfo subjectPublicKey contents:
 * a SEC1 point for ECDSA (65/97 bytes uncompressed), a DER RSAPublicKey for
 * RSA.  `msg` is hashed inside with the scheme's digest.
 *
 * Returns 0 when the signature is valid, -1 with errno = EBADMSG when it is
 * not, and -1 with errno = EINVAL for malformed arguments or an unknown
 * scheme. */
int cact_sig_verify(uint32_t scheme,
                    const void *pubkey, size_t pubkey_len,
                    const void *msg, size_t msg_len,
                    const void *sig, size_t sig_len);

/* ── certificate chains ─────────────────────────────────────────────────── */
/* Verify a certificate chain against caller-supplied trust anchors.
 *
 * `chain` and `roots` are buffers of concatenated DER certificates (the leaf
 * first in `chain`); `roots` are the anchors the caller is willing to trust —
 * the kernel holds no trust policy, so the roots come from
 * /etc/ca-certificates.crt or wherever the caller's policy says.
 *
 * The chain must reach a root, be inside its validity window at `unix_time`,
 * and `hostname` has to match the leaf.  When `tls_scheme` is non-zero,
 * `hs_sig` must additionally be a valid signature over `hs_msg` by the leaf's
 * key — that is the TLS 1.3 CertificateVerify check.
 *
 * Returns 0 when everything checks out, -1 with errno = EBADMSG when it does
 * not, and -1 with errno = EINVAL on malformed input. */
int cact_x509_verify(const void *chain, size_t chain_len,
                     const void *roots, size_t roots_len,
                     const char *hostname, uint64_t unix_time,
                     uint32_t tls_scheme,
                     const void *hs_msg, size_t hs_msg_len,
                     const void *hs_sig, size_t hs_sig_len);

#endif /* _CACT_CRYPTO_H */
