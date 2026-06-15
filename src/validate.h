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
 * ll_license_is_valid:
 *   Returns 1 if time.Since(lic->issued_at) <= lic->grace_period (i.e. within window).
 *   Corresponds to domain.License.IsValid() in Go.
 */
int ll_license_is_valid(const ll_license *lic);

#endif /* LATTE_VALIDATE_H */
