/* Unit tests for key checksum and sanitise logic. */
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "../src/crypto/key.h"

static int pass = 0, fail = 0;

#define CHECK(expr, name) \
    do { \
        if (expr) { printf("  PASS  %s\n", name); pass++; } \
        else      { printf("  FAIL  %s\n", name); fail++; } \
    } while(0)

int main(void)
{
    printf("=== test_key ===\n");

    /* --- sanitize_key --- */
    char out[64];

    ll_sanitize_key("ABCD-EF-GH", out, sizeof(out));
    CHECK(strcmp(out, "ABCDEFGH") == 0, "strip hyphens");

    ll_sanitize_key("ab cd", out, sizeof(out));
    CHECK(strcmp(out, "ABCD") == 0, "uppercase + strip spaces");

    ll_sanitize_key("OIL0", out, sizeof(out));
    CHECK(strcmp(out, "0110") == 0, "O→0 I→1 L→1");

    ll_sanitize_key("G98S8-BWAR6-Y0Z7F-JT10A-7EWD9-33265", out, sizeof(out));
    CHECK(strcmp(out, "G98S8BWAR6Y0Z7FJT10A7EWD933265") == 0, "real key normalise");

    /* --- normalize_key: no O/I/L fold, unlike sanitize_key --- */
    ll_normalize_key("ABCD-EF-GH", out, sizeof(out));
    CHECK(strcmp(out, "ABCDEFGH") == 0, "normalize: strip hyphens");

    ll_normalize_key("ab cd", out, sizeof(out));
    CHECK(strcmp(out, "ABCD") == 0, "normalize: uppercase + strip spaces");

    ll_normalize_key("OIL0", out, sizeof(out));
    CHECK(strcmp(out, "OIL0") == 0, "normalize: does not fold O/I/L");

    /* --- validate_key: AppID segment (4-char checksum) ---
     * AppID: pk_live_G98S8BKDDMZW32RMQM8NZT33E5CTFEDC
     * The 32-char segment is: G98S8BKDDMZW32RMQM8NZT33E5CTFEDC
     * Per the Go SDK this is a known-valid key; last 4 chars are the checksum.
     */
    const char *appkey = "G98S8BKDDMZW32RMQM8NZT33E5CTFEDC";
    CHECK(strlen(appkey) == 32, "appkey length");
    CHECK(ll_validate_key(appkey, 4) == 1, "appkey checksum valid");

    /* Corrupt the last byte → should fail */
    char bad_appkey[33];
    memcpy(bad_appkey, appkey, 33);
    bad_appkey[31] ^= 1;
    CHECK(ll_validate_key(bad_appkey, 4) == 0, "corrupted appkey checksum");

    /* --- validate_key: license key (2-char checksum) ---
     * Sanitised: G98S8BWAR6Y0Z7FJT10A7EWD933265 (30 chars, prefix matches)
     * Suffix to check: chars [6..29] with 2-char checksum at end.
     */
    const char *sanitised_lic = "G98S8BWAR6Y0Z7FJT10A7EWD933265";
    CHECK(strlen(sanitised_lic) == 30, "license key length");
    /* validate_key is called on key[6:] with luhn_len=2 */
    CHECK(ll_validate_key(sanitised_lic + 6, 2) == 1, "license key checksum valid");

    char bad_lic[31];
    memcpy(bad_lic, sanitised_lic, 31);
    bad_lic[29] ^= 1;
    CHECK(ll_validate_key(bad_lic + 6, 2) == 0, "corrupted license key checksum");

    printf("\n%d passed, %d failed\n", pass, fail);
    return fail > 0 ? 1 : 0;
}
