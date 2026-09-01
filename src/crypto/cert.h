#ifndef LATTE_CERT_H
#define LATTE_CERT_H

#include <stddef.h>
#include <stdint.h>
#include "../../vendor/cjson/cJSON.h"

/*
 * ll_verify_cert:
 *   Parse and verify a cert JWT signed by parent_pub (32-byte Ed25519 key),
 *   and enforce its validity window against `now` (Unix seconds).
 *
 *   The three chain certs are checked with ZERO leeway, unlike the
 *   activation JWT whose own iat/exp are deliberately not authoritative
 *   (the grace-period math in validate.c is). See SPEC.md section 3.3: a
 *   cert outside its window is a rejection, a token outside its own is not.
 *
 *   iat, nbf and exp are each enforced only when present, matching
 *   golang-jwt/v5's behaviour in latte-go's crypto.VerifyCert.
 *
 *   On success returns 0 and sets *claims_out (caller must cJSON_Delete).
 */
int ll_verify_cert(const unsigned char *parent_pub,
                   const char *cert_jwt,
                   int64_t now,
                   cJSON **claims_out);

/*
 * ll_pubkey_from_cert:
 *   Extract a 32-byte Ed25519 public key from a hex claim named `field`.
 *   pub_out must be at least 32 bytes.  Returns 0 on success.
 */
int ll_pubkey_from_cert(const cJSON *claims, const char *field,
                        unsigned char *pub_out);

#endif /* LATTE_CERT_H */
