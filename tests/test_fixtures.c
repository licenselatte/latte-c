/*
 * Runs every shared fixture in testdata/ against this SDK's
 * verify/validate pipeline. See testdata/README.md for the fixture schema,
 * the expect_reason taxonomy, and the entitlement rules asserted here.
 *
 * latte-c was the one SDK outside this suite for a long time, for a
 * concrete reason: nothing here took an explicit "now", so a fixture
 * pinned to a fixed instant could not be replayed. ll_verify_activation_at
 * / ll_validate_at / ll_in_grace_period_at are that seam, and this file is
 * what they exist for.
 */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/domain.h"
#include "../src/entitlements.h"
#include "../src/errors.h"
#include "../src/validate.h"
#include "../src/verify.h"

#define VECTORS_DIR "testdata/vectors"

static int pass = 0, fail = 0;

#define CHECK(expr, fixture, what) \
    do { \
        if (expr) { pass++; } \
        else { printf("  FAIL  %s: %s\n", fixture, what); fail++; } \
    } while (0)

/* ---------------------------------------------------------------------- */

static int hex_decode32(const char *hex, unsigned char *out)
{
    if (!hex || strlen(hex) != 64) return -1;
    for (int i = 0; i < 32; i++) {
        unsigned int byte;
        if (sscanf(hex + 2 * i, "%02x", &byte) != 1) return -1;
        out[i] = (unsigned char)byte;
    }
    return 0;
}

/* Days since the epoch, via the civil_from_days algorithm (Howard Hinnant).
 * Hand-rolled rather than timegm(), which is not portable to every
 * toolchain this SDK builds under. */
static long long days_from_civil(long long y, long long m, long long d)
{
    y -= m <= 2;
    long long era = (y >= 0 ? y : y - 399) / 400;
    long long yoe = y - era * 400;
    long long mp  = (m + 9) % 12;
    long long doy = (153 * mp + 2) / 5 + d - 1;
    long long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

/* Fixtures are always "YYYY-MM-DDTHH:MM:SSZ" -- the generator emits UTC
 * with zero sub-second precision -- so a tiny parser is enough. */
static int parse_rfc3339(const char *s, int64_t *out)
{
    int y, mo, d, h, mi, sec;
    if (!s || strlen(s) != 20 || s[19] != 'Z') return -1;
    if (sscanf(s, "%4d-%2d-%2dT%2d:%2d:%2dZ", &y, &mo, &d, &h, &mi, &sec) != 6)
        return -1;
    *out = days_from_civil(y, mo, d) * 86400 + h * 3600 + mi * 60 + sec;
    return 0;
}

static const char *reason_for(ll_port_error e)
{
    switch (e) {
    case LL_PORT_ERR_LICENSE_INACTIVE_OR_EXPIRED: return "hard_expired";
    case LL_PORT_ERR_GRACE_PERIOD_EXPIRED:        return "grace_expired";
    case LL_PORT_ERR_LICENSE_TOO_OLD:             return "license_too_old";
    case LL_PORT_ERR_MACHINE_ID_MISMATCH:         return "machine_id_mismatch";
    default:                                      return "other";
    }
}

static const char *str_field(const cJSON *o, const char *k)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
    return cJSON_IsString(v) ? v->valuestring : "";
}

static int bool_field(const cJSON *o, const char *k)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
    return cJSON_IsTrue(v) ? 1 : 0;
}

/* ---------------------------------------------------------------------- */

/*
 * Asserts the shared entitlement contract against one fixture.
 *
 * It goes through latte_can / latte_limit rather than reading the array,
 * because the map is the easy half: the rules that actually split
 * implementations are the ones about input an SDK does not like -- a
 * malformed value that must be dropped rather than fatal, and the two
 * coercions (a falsy 0, a boolean read as 1) that must miss.
 */
static void assert_entitlements(const ll_license *lic, const cJSON *f, const char *name)
{
    /* The accessors take the public struct; borrow the decoded array rather
     * than copying it, since latte_can/latte_limit only read. */
    latte_license pub;
    memset(&pub, 0, sizeof(pub));
    pub.has_entitlements  = (uint32_t)lic->has_entitlements;
    pub.entitlement_count = (uint32_t)lic->entitlement_count;
    pub.entitlements      = lic->entitlements;

    CHECK(latte_has_entitlements(&pub) == bool_field(f, "expect_has_entitlements"),
          name, "has_entitlements mismatch");

    const cJSON *want = cJSON_GetObjectItemCaseSensitive(f, "expect_entitlements");
    int want_count = cJSON_IsObject(want) ? cJSON_GetArraySize(want) : 0;
    CHECK((int)pub.entitlement_count == want_count, name, "entitlement count mismatch");

    const cJSON *item;
    cJSON_ArrayForEach(item, want) {
        char what[192];
        if (cJSON_IsBool(item)) {
            int expect = cJSON_IsTrue(item) ? 1 : 0;
            snprintf(what, sizeof what, "can(\"%s\") should be %d", item->string, expect);
            CHECK(latte_can(&pub, item->string) == expect, name, what);
            /* No coercion: a boolean is not 1 or 0. */
            snprintf(what, sizeof what, "limit(\"%s\") on a boolean must miss", item->string);
            CHECK(latte_limit(&pub, item->string, NULL) == 0, name, what);
        } else if (cJSON_IsNumber(item)) {
            int64_t expect = (int64_t)item->valuedouble, got = 0;
            snprintf(what, sizeof what, "limit(\"%s\") should be %lld",
                     item->string, (long long)expect);
            CHECK(latte_limit(&pub, item->string, &got) == 1 && got == expect, name, what);
            if (expect == LATTE_UNLIMITED) {
                snprintf(what, sizeof what,
                         "limit(\"%s\") must return LATTE_UNLIMITED as-is", item->string);
                CHECK(got == LATTE_UNLIMITED, name, what);
            }
            /* No coercion: an integer is not truthy, not even a non-zero one. */
            snprintf(what, sizeof what, "can(\"%s\") on an integer must be 0", item->string);
            CHECK(latte_can(&pub, item->string) == 0, name, what);
        } else {
            CHECK(0, name, "expect_entitlements holds a non-bool, non-number value");
        }
    }

    /* Absence denies, whether or not the claim was there at all. */
    CHECK(latte_can(&pub, "no_such_entitlement_key") == 0, name, "an unset key must deny");
    CHECK(latte_limit(&pub, "no_such_entitlement_key", NULL) == 0, name,
          "an unset key must miss");
}

/* ---------------------------------------------------------------------- */

static void run_fixture(const cJSON *f)
{
    const char *name = str_field(f, "name");
    const char *expect = str_field(f, "expect");
    const char *stage = str_field(f, "expect_stage");

    int64_t now;
    if (parse_rfc3339(str_field(f, "now"), &now) < 0) {
        CHECK(0, name, "unparseable 'now'");
        return;
    }

    unsigned char master_pub[32];
    if (hex_decode32(str_field(f, "master_public_key_hex"), master_pub) < 0) {
        CHECK(0, name, "bad master_public_key_hex");
        return;
    }

    const cJSON *chain_json = cJSON_GetObjectItemCaseSensitive(f, "chain");
    ll_cert_chain chain;
    chain.submaster = (char *)str_field(chain_json, "submaster");
    chain.project   = (char *)str_field(chain_json, "project");
    chain.daily     = (char *)str_field(chain_json, "daily");

    ll_license *lic = NULL;
    ll_port_error verr = LL_PORT_OK;
    if (ll_verify_activation_at(master_pub, str_field(f, "token"), &chain,
                                now, &lic, &verr) < 0) {
        CHECK(strcmp(expect, "reject") == 0 && strcmp(stage, "verify") == 0,
              name, "unexpected verify-stage rejection");
        return;
    }
    if (strcmp(expect, "reject") == 0 && strcmp(stage, "verify") == 0) {
        CHECK(0, name, "expected verify-stage rejection but the chain verified");
        ll_license_free(lic);
        return;
    }

    ll_port_error vaerr = ll_validate_at(lic, str_field(f, "machine_id"), now);
    if (vaerr != LL_PORT_OK) {
        CHECK(strcmp(expect, "reject") == 0 && strcmp(stage, "validate") == 0,
              name, "unexpected validate-stage rejection");
        CHECK(strcmp(reason_for(vaerr), str_field(f, "expect_reason")) == 0,
              name, "validate-stage reason mismatch");
        ll_license_free(lic);
        return;
    }

    if (strcmp(expect, "accept") != 0) {
        CHECK(0, name, "expected rejection but verify+validate both succeeded");
        ll_license_free(lic);
        return;
    }

    CHECK(ll_in_grace_period_at(lic, now) == bool_field(f, "expect_in_grace_period"),
          name, "in_grace_period mismatch");

    assert_entitlements(lic, f, name);
    ll_license_free(lic);
}

/* ---------------------------------------------------------------------- */

static char *read_file(const char *path)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) return NULL;
    fseek(fp, 0, SEEK_END);
    long n = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    char *buf = (char *)malloc((size_t)n + 1);
    if (buf && fread(buf, 1, (size_t)n, fp) == (size_t)n) buf[n] = '\0';
    else { free(buf); buf = NULL; }
    fclose(fp);
    return buf;
}

int main(void)
{
    printf("=== test_fixtures ===\n");

    DIR *dir = opendir(VECTORS_DIR);
    if (!dir) {
        printf("  cannot open %s -- did you forget to populate the submodule?\n"
               "  (git submodule update --init)\n", VECTORS_DIR);
        return 1;
    }

    int ran = 0;
    struct dirent *de;
    while ((de = readdir(dir)) != NULL) {
        size_t len = strlen(de->d_name);
        if (len < 6 || strcmp(de->d_name + len - 5, ".json") != 0) continue;
        if (strcmp(de->d_name, "manifest.json") == 0) continue;

        char path[1024];
        snprintf(path, sizeof path, "%s/%s", VECTORS_DIR, de->d_name);
        char *data = read_file(path);
        if (!data) { printf("  FAIL  cannot read %s\n", path); fail++; continue; }

        cJSON *f = cJSON_Parse(data);
        if (!f) { printf("  FAIL  cannot parse %s\n", path); fail++; free(data); continue; }

        run_fixture(f);
        ran++;

        cJSON_Delete(f);
        free(data);
    }
    closedir(dir);

    if (ran <= 15) {
        printf("  FAIL  expected the full shared fixture set, only found %d\n", ran);
        fail++;
    }

    printf("\n%d fixtures, %d assertions passed, %d failed\n", ran, pass, fail);
    return fail == 0 ? 0 : 1;
}
