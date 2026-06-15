/* Unit tests for validate() grace/expiry rules. */
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "../src/validate.h"
#include "../src/errors.h"
#include "../src/constants.h"

static int pass = 0, fail = 0;

#define CHECK(expr, name) \
    do { \
        if (expr) { printf("  PASS  %s\n", name); pass++; } \
        else      { printf("  FAIL  %s\n", name); fail++; } \
    } while(0)

static ll_license make_lic(const char *ltype, int64_t iat_offset,
                            int64_t exp_offset, int64_t grc_secs)
{
    int64_t now = (int64_t)time(NULL);
    ll_license l = {0};
    l.license_type    = (char *)ltype;
    l.issued_at       = now + iat_offset;
    l.expires_at      = now + exp_offset;
    l.grace_period    = grc_secs;
    /* machine_id_hash matches mid passed to ll_validate() so the check passes */
    l.machine_id_hash = (char *)"test-machine-001";
    return l;
}

int main(void)
{
    printf("=== test_validate ===\n");

    const char *mid = "test-machine-001";

    /* Happy path — perpetual, issued now, expires in 30 days, 7-day grace */
    {
        ll_license l = make_lic(LATTE_TYPE_PERPETUAL, 0,
                                 30*24*3600, 7*24*3600);
        CHECK(ll_validate(&l, mid) == LL_PORT_OK, "happy path: perpetual");
    }

    /* perpetual_fixed — only checks hard expiry, no grc */
    {
        ll_license l = make_lic(LATTE_TYPE_PERPETUAL_FIXED, -100,
                                 365*24*3600, 1 /* irrelevant, but must be > 0 */);
        CHECK(ll_validate(&l, mid) == LL_PORT_OK, "perpetual_fixed: future exp");
    }

    /* perpetual_fixed — past expiry → expired */
    {
        ll_license l = make_lic(LATTE_TYPE_PERPETUAL_FIXED, -200,
                                 -100, 1);
        CHECK(ll_validate(&l, mid) == LL_PORT_ERR_LICENSE_INACTIVE_OR_EXPIRED,
              "perpetual_fixed: past exp → expired");
    }

    /* Expired: now > expires_at */
    {
        ll_license l = make_lic(LATTE_TYPE_EXPIRING, -3600*48,
                                 -3600*24, 7*24*3600);
        CHECK(ll_validate(&l, mid) == LL_PORT_ERR_LICENSE_INACTIVE_OR_EXPIRED,
              "expiring: past expires_at → expired");
    }

    /* Grace period exceeded: now > iat + grc */
    {
        /* Issued 8 days ago, grace 7 days, expires 30 days from now */
        ll_license l = make_lic(LATTE_TYPE_PERPETUAL, -8*24*3600,
                                 30*24*3600, 7*24*3600);
        CHECK(ll_validate(&l, mid) == LL_PORT_ERR_GRACE_PERIOD_EXPIRED,
              "grace period exceeded");
    }

    /* Too old: age > 365 days */
    {
        int64_t old = (int64_t)(LATTE_MAX_AGE_SECS + 3600);
        ll_license l = make_lic(LATTE_TYPE_PERPETUAL, -old,
                                 30*24*3600, (old + 7*24*3600));
        CHECK(ll_validate(&l, mid) == LL_PORT_ERR_LICENSE_TOO_OLD,
              "token too old (> 365 days)");
    }

    /* Machine ID mismatch → invalid */
    {
        ll_license l = make_lic(LATTE_TYPE_PERPETUAL, 0, 30*24*3600, 7*24*3600);
        l.machine_id_hash = (char *)"different-machine-999";
        CHECK(ll_validate(&l, mid) == LL_PORT_ERR_INVALID_LICENSE,
              "machine ID mismatch → invalid");
    }

    /* Zero grace_period → invalid */
    {
        ll_license l = make_lic(LATTE_TYPE_PERPETUAL, 0, 30*24*3600, 0);
        CHECK(ll_validate(&l, mid) == LL_PORT_ERR_INVALID_LICENSE,
              "zero grace period → invalid");
    }

    /* ll_license_is_valid */
    {
        ll_license l = make_lic(LATTE_TYPE_PERPETUAL, -100, 30*24*3600, 7*24*3600);
        CHECK(ll_license_is_valid(&l) == 1, "is_valid: within grace");

        ll_license l2 = make_lic(LATTE_TYPE_PERPETUAL, -8*24*3600, 30*24*3600, 7*24*3600);
        CHECK(ll_license_is_valid(&l2) == 0, "is_valid: outside grace");
    }

    printf("\n%d passed, %d failed\n", pass, fail);
    return fail > 0 ? 1 : 0;
}
