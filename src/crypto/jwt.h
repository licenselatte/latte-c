#ifndef LATTE_JWT_H
#define LATTE_JWT_H

#include <stddef.h>
#include <stdint.h>
#include "../../vendor/cjson/cJSON.h"

/*
 * ll_jwt_verify_ed25519:
 *   Verify an EdDSA JWT (header.payload.signature) against the given
 *   Ed25519 public key (32 bytes raw).
 *
 *   Checks: signature valid, header alg == "EdDSA", payload iss == "licenselatte".
 *   Does NOT enforce exp (matches Go SDK behaviour, grace logic is applied separately).
 *
 *   On success, returns 0 and sets *claims_out to a cJSON object that the
 *   caller must free with cJSON_Delete().
 *   On failure returns -1 and *claims_out is NULL.
 */
int ll_jwt_verify_ed25519(const char *token,
                           const unsigned char *pubkey, size_t pubkey_len,
                           cJSON **claims_out);

/*
 * Convenience: get a string claim from the already-parsed claims object.
 * Returns NULL if the key is absent or not a string.
 */
const char *ll_jwt_string_claim(const cJSON *claims, const char *key);

/*
 * Convenience: get a numeric claim as int64.  Returns 0 and sets *ok=0 if absent.
 */
int64_t ll_jwt_int64_claim(const cJSON *claims, const char *key, int *ok);

#endif /* LATTE_JWT_H */
