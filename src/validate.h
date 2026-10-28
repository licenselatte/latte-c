#ifndef LATTE_VALIDATE_H
#define LATTE_VALIDATE_H

#include "domain.h"
#include "errors.h"

/*
 * ll_validate:
 *   Apply grace-period and expiry rules to an already-verified license.
 *   Mirrors validate.Validate() in Go.
 *
 *   Returns LL_PORT_OK on success.
 */
ll_port_error ll_validate(const ll_license *lic, const char *machine_id);

/*
 * ll_validate_at:
 *   ll_validate with an injectable clock (`now`, Unix seconds), so the
 *   shared latte-testvectors fixtures -- each pinned to a fixed instant --
 *   can be replayed without depending on the wall clock. Production code
 *   calls ll_validate; this is the test seam every other SDK already has.
 */
ll_port_error ll_validate_at(const ll_license *lic, const char *machine_id,
                             int64_t now);

/*
 * ll_in_grace_period_at:
 *   The public in_grace_period flag: 1 when the licence is still valid on
 *   cached data, the SDK has tried to renew it and could not reach the
 *   server (renewal_failed), and the offline grace window has not run out:
 *   renewal_failed && 60 min < age < grace_period.
 *
 *   Factored out of domain_to_public() in latte.c so the fixture runner can
 *   assert expect_in_grace_period against the same arithmetic the SDK
 *   actually ships, rather than a copy of it.
 */
int ll_in_grace_period_at(const ll_license *lic, int64_t now, int renewal_failed);

/*
 * ll_license_is_valid:
 *   Returns 1 if time.Since(lic->issued_at) <= lic->grace_period (i.e. within window).
 *   Corresponds to domain.License.IsValid() in Go.
 */
int ll_license_is_valid(const ll_license *lic);

#endif /* LATTE_VALIDATE_H */
