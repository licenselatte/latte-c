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

int ll_in_grace_period_at(const ll_license *lic, int64_t now)
{
    int64_t age = now - lic->issued_at;
    return (age > LATTE_MAX_RENEWAL_SECS && age < lic->grace_period) ? 1 : 0;
}

ll_port_error ll_validate(const ll_license *lic, const char *machine_id)
{
    return ll_validate_at(lic, machine_id, (int64_t)time(NULL));
}

ll_port_error ll_validate_at(const ll_license *lic, const char *machine_id,
                             int64_t now)
{
    if (!lic->issued_at || !lic->expires_at)
        return LL_PORT_ERR_INVALID_LICENSE;

    if (lic->grace_period <= 0)
        return LL_PORT_ERR_INVALID_LICENSE;

    /* Reject if the token was issued for a different machine. Its own
     * reason code, not the generic one: latte-testvectors treats
     * machine_id_mismatch as one of the four validate-stage reasons a port
     * may not collapse into another. */
    if (!lic->machine_id_hash || !machine_id ||
        strcmp(lic->machine_id_hash, machine_id) != 0)
        return LL_PORT_ERR_MACHINE_ID_MISMATCH;


    if (lic->expires_at < lic->issued_at)
        return LL_PORT_ERR_INVALID_LICENSE;

    int64_t offline_deadline = lic->issued_at + lic->grace_period;

    if (now > lic->expires_at)
        return LL_PORT_ERR_LICENSE_INACTIVE_OR_EXPIRED;

    if (now > offline_deadline)
        return LL_PORT_ERR_GRACE_PERIOD_EXPIRED;

    if ((now - lic->issued_at) > LATTE_MAX_AGE_SECS)
        return LL_PORT_ERR_LICENSE_TOO_OLD;

    return LL_PORT_OK;
}
