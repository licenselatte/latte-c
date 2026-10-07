#ifndef LATTE_VERIFY_H
#define LATTE_VERIFY_H

#include "domain.h"

/*
 * ll_verify_activation:
 *   Walk the 4-level cert chain (master→submaster→project→daily→activation JWT)
 *   and return a populated ll_license on success.
 *
 *   master_pub: 32-byte raw Ed25519 public key. The SDK itself verifies
 *   against every trusted key in constants.h via ll_verify_activation_any.
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

/*
 * ll_verify_activation_at:
 *   ll_verify_activation with an injectable clock (`now`, Unix seconds), so
 *   the shared latte-testvectors fixtures -- each pinned to a fixed instant
 *   -- can be replayed without depending on the wall clock. Production code
 *   calls ll_verify_activation; this is the test seam every other SDK
 *   already has.
 *
 *   `now` is used for the three chain certs' validity windows only. The
 *   activation JWT's own iat/exp are deliberately not authoritative here
 *   (SPEC.md section 3.3) -- the grace-period math in validate.c is.
 */
int ll_verify_activation_at(const unsigned char *master_pub,
                            const char *token,
                            const ll_cert_chain *chain,
                            int64_t now,
                            ll_license **out,
                            ll_port_error *err_code);

/*
 * ll_verify_activation_any_at:
 *   ll_verify_activation_at against a list of trusted master keys. Step 1
 *   uses the first key under which the submaster cert verifies; the rest of
 *   the chain is checked exactly as for a single key. Fails if none does.
 *
 *   master_pubs: n_master raw 32-byte Ed25519 public keys, back to back.
 */
int ll_verify_activation_any_at(const unsigned char *master_pubs,
                                size_t n_master,
                                const char *token,
                                const ll_cert_chain *chain,
                                int64_t now,
                                ll_license **out,
                                ll_port_error *err_code);

/* ll_verify_activation_any_at at the current wall-clock time. */
int ll_verify_activation_any(const unsigned char *master_pubs,
                             size_t n_master,
                             const char *token,
                             const ll_cert_chain *chain,
                             ll_license **out,
                             ll_port_error *err_code);

#endif /* LATTE_VERIFY_H */
