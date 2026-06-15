#ifndef LATTE_VERIFY_H
#define LATTE_VERIFY_H

#include "domain.h"

/*
 * ll_verify_activation:
 *   Walk the 4-level cert chain (master→submaster→project→daily→activation JWT)
 *   and return a populated ll_license on success.
 *
 *   master_pub: 32-byte raw Ed25519 public key (hardcoded in constants.h).
 *   token:  activation JWT string.
 *   chain:  the three intermediate cert JWTs.
 *   out:    receives a heap-allocated ll_license on success (caller must
 *           ll_license_free it).
 *
 *   Returns 0 on success, -1 on any verification failure.
 *   On -1, *err_code is set to a ll_port_error value for the caller to convert.
 */
#include "errors.h"

int ll_verify_activation(const unsigned char *master_pub,
                         const char *token,
                         const ll_cert_chain *chain,
                         ll_license **out,
                         ll_port_error *err_code);

#endif /* LATTE_VERIFY_H */
