#include "validate.h"
#include "constants.h"
#include <string.h>
#include <time.h>
#include <stdio.h>

int ll_license_is_valid(const ll_license *lic)
{
    int64_t now = (int64_t)time(NULL);
    int64_t age = now - lic->issued_at;
    return age <= lic->grace_period;
}

ll_port_error ll_validate(const ll_license *lic, const char *machine_id)
{
    if (!lic->issued_at || !lic->expires_at)
        return LL_PORT_ERR_INVALID_LICENSE;

    if (lic->grace_period <= 0)
        return LL_PORT_ERR_INVALID_LICENSE;

    /* Reject if the token was issued for a different machine. */
    if (!lic->machine_id_hash || !machine_id ||
        strcmp(lic->machine_id_hash, machine_id) != 0)
        return LL_PORT_ERR_INVALID_LICENSE;


    if (lic->expires_at < lic->issued_at)
        return LL_PORT_ERR_INVALID_LICENSE;

    int64_t now = (int64_t)time(NULL);

    /* perpetual_fixed: only check hard expiry */
    if (lic->license_type && strcmp(lic->license_type, LATTE_TYPE_PERPETUAL_FIXED) == 0) {
        if (now > lic->expires_at)
            return LL_PORT_ERR_LICENSE_INACTIVE_OR_EXPIRED;
        return LL_PORT_OK;
    }

    int64_t offline_deadline = lic->issued_at + lic->grace_period;

    if (now > lic->expires_at)
        return LL_PORT_ERR_LICENSE_INACTIVE_OR_EXPIRED;

    if (now > offline_deadline)
        return LL_PORT_ERR_GRACE_PERIOD_EXPIRED;

    if ((now - lic->issued_at) > LATTE_MAX_AGE_SECS)
        return LL_PORT_ERR_LICENSE_TOO_OLD;

    return LL_PORT_OK;
}
