#ifndef LATTE_ED25519_H
#define LATTE_ED25519_H

#include <stddef.h>

/*
 * Whether p is a canonical encoding (y < 2^255 - 19) of a point that is not
 * of small order. Both R and the public key must pass.
 */
int ll_ed25519_point_ok(const unsigned char p[32]);

/*
 * Ed25519 (RFC 8032, pure) verification of sig over msg under pk.
 * Returns 0 when valid, -1 otherwise. Rejects S >= L, points off the curve,
 * and any R or pk that fails ll_ed25519_point_ok.
 */
int ll_ed25519_verify(const unsigned char sig[64],
                      const unsigned char *msg, size_t msg_len,
                      const unsigned char pk[32]);

#endif /* LATTE_ED25519_H */
