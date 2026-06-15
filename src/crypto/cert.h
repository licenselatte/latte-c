#ifndef LATTE_CERT_H
#define LATTE_CERT_H

#include <stddef.h>
#include "../../vendor/cjson/cJSON.h"

/*
 * ll_verify_cert:
 *   Parse and verify a cert JWT signed by parent_pub (32-byte Ed25519 key).
 *   On success returns 0 and sets *claims_out (caller must cJSON_Delete).
 */
int ll_verify_cert(const unsigned char *parent_pub,
                   const char *cert_jwt,
                   cJSON **claims_out);

/*
 * ll_pubkey_from_cert:
 *   Extract a 32-byte Ed25519 public key from a hex claim named `field`.
 *   pub_out must be at least 32 bytes.  Returns 0 on success.
 */
int ll_pubkey_from_cert(const cJSON *claims, const char *field,
                        unsigned char *pub_out);

#endif /* LATTE_CERT_H */
