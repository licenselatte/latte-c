/*
 * Unit tests for the typed-entitlements decoder and accessors.
 *
 * latte-c is the one SDK outside the shared latte-testvectors fixture
 * suite (it carries no testdata submodule), so this file stands in for the
 * six `entitlements_*` fixtures the other four run, asserting the same six
 * rules from latte-testvectors/README.md against hand-built claims:
 *
 *   1. A malformed value is dropped, never fatal.
 *   2. Absence denies.
 *   3. No coercion across kinds.
 *   4. Byte-exact key comparison.
 *   5. LATTE_UNLIMITED is -1, returned as-is.
 *   6. Only whole numbers are integers.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/entitlements.h"

static int pass = 0, fail = 0;

#define CHECK(expr, name) \
    do { \
        if (expr) { printf("  PASS  %s\n", name); pass++; } \
        else      { printf("  FAIL  %s\n", name); fail++; } \
    } while(0)

/* Builds a license whose entitlements are decoded from a JSON literal. */
static latte_license *lic_from_ent_json(const char *json)
{
    latte_license *lic = (latte_license *)calloc(1, sizeof(latte_license));
    if (!json) return lic; /* no ent claim at all */

    cJSON *ent = cJSON_Parse(json);
    size_t count = 0;
    lic->has_entitlements =
        (uint32_t)ll_decode_entitlements(ent, &lic->entitlements, &count);
    lic->entitlement_count = (uint32_t)count;
    cJSON_Delete(ent);
    return lic;
}

static void lic_free(latte_license *lic)
{
    for (uint32_t i = 0; i < lic->entitlement_count; i++) free(lic->entitlements[i].key);
    free(lic->entitlements);
    free(lic);
}

int main(void)
{
    printf("=== test_entitlements ===\n");

    /* entitlements_absent: no claim. Every key denies, and the probe says so. */
    {
        latte_license *lic = lic_from_ent_json(NULL);
        int64_t out = 999;
        CHECK(latte_has_entitlements(lic) == 0, "absent: has_entitlements is 0");
        CHECK(lic->entitlement_count == 0, "absent: no entitlements");
        CHECK(latte_can(lic, "export_pdf") == 0, "absent: can() denies");
        CHECK(latte_limit(lic, "max_projects", &out) == 0, "absent: limit() misses");
        CHECK(out == 999, "absent: limit() leaves out untouched on a miss");
        lic_free(lic);
    }

    /* entitlements_empty_object: identical to absent for every accessor,
     * and distinguishable only by the presence probe. */
    {
        latte_license *lic = lic_from_ent_json("{}");
        CHECK(latte_has_entitlements(lic) == 1, "empty: has_entitlements is 1");
        CHECK(lic->entitlement_count == 0, "empty: no entitlements");
        CHECK(latte_can(lic, "export_pdf") == 0, "empty: can() denies");
        CHECK(latte_limit(lic, "max_projects", NULL) == 0, "empty: limit() misses");
        lic_free(lic);
    }

    /* entitlements_bool_and_int: the happy path, including a present false. */
    {
        latte_license *lic = lic_from_ent_json(
            "{\"export_pdf\":true,\"beta_ui\":false,\"max_projects\":25}");
        int64_t out = 0;
        CHECK(latte_has_entitlements(lic) == 1, "happy: has_entitlements is 1");
        CHECK(lic->entitlement_count == 3, "happy: three entitlements decoded");
        CHECK(latte_can(lic, "export_pdf") == 1, "happy: can() on a true boolean");
        CHECK(latte_can(lic, "beta_ui") == 0, "happy: can() on a false boolean");
        CHECK(latte_limit(lic, "max_projects", &out) == 1 && out == 25,
              "happy: limit() on an integer");
        lic_free(lic);
    }

    /* entitlements_unlimited: -1 round-trips as -1. */
    {
        latte_license *lic = lic_from_ent_json("{\"max_seats\":-1}");
        int64_t out = 0;
        CHECK(latte_limit(lic, "max_seats", &out) == 1 && out == LATTE_UNLIMITED,
              "unlimited: limit() returns the sentinel as-is");
        lic_free(lic);
    }

    /* entitlements_malformed_value_dropped: each unrepresentable entry
     * vanishes, the good ones survive, and nothing about this is fatal. */
    {
        latte_license *lic = lic_from_ent_json(
            "{\"export_pdf\":true,\"max_projects\":25,\"tier\":\"pro\","
            "\"ratio\":1.5,\"nested\":{\"a\":true},\"listy\":[1,2],\"nulled\":null}");
        int64_t out = 0;
        CHECK(latte_has_entitlements(lic) == 1, "malformed: claim still counts as present");
        CHECK(lic->entitlement_count == 2, "malformed: only the two good values survive");
        CHECK(latte_can(lic, "export_pdf") == 1, "malformed: the good boolean survives");
        CHECK(latte_limit(lic, "max_projects", &out) == 1 && out == 25,
              "malformed: the good integer survives");
        CHECK(latte_can(lic, "tier") == 0, "malformed: a string value is dropped");
        CHECK(latte_limit(lic, "ratio", NULL) == 0, "malformed: a fractional number is dropped");
        CHECK(latte_limit(lic, "nested", NULL) == 0, "malformed: a nested object is dropped");
        CHECK(latte_limit(lic, "listy", NULL) == 0, "malformed: an array is dropped");
        CHECK(latte_can(lic, "nulled") == 0, "malformed: a null is dropped");
        lic_free(lic);
    }

    /* entitlements_type_mismatch: no coercion in either direction. This is
     * the pair that would split five implementations if any of them
     * converted: 0 is falsy in most languages, and a boolean is 1-or-0 in
     * several. */
    {
        latte_license *lic = lic_from_ent_json("{\"is_pro\":true,\"seat_count\":0}");
        int64_t out = 777;
        CHECK(latte_can(lic, "seat_count") == 0, "mismatch: can() on an integer is 0");
        CHECK(latte_limit(lic, "is_pro", &out) == 0, "mismatch: limit() on a boolean misses");
        CHECK(out == 777, "mismatch: a missed limit() leaves out untouched");
        /* A non-zero integer is still not "true". */
        latte_license *lic2 = lic_from_ent_json("{\"seat_count\":42}");
        CHECK(latte_can(lic2, "seat_count") == 0, "mismatch: a non-zero integer is not true");
        lic_free(lic2);
        lic_free(lic);
    }

    /* A whole-valued float is an integer; Go's and JavaScript's JSON parsers
     * cannot tell 25 from 25.0, so neither may this one. */
    {
        latte_license *lic = lic_from_ent_json("{\"max_projects\":25.0}");
        int64_t out = 0;
        CHECK(latte_limit(lic, "max_projects", &out) == 1 && out == 25,
              "whole float is an integer");
        lic_free(lic);
    }

    /* Byte-exact key comparison: no case folding, no trimming. */
    {
        latte_license *lic = lic_from_ent_json("{\"export_pdf\":true}");
        CHECK(latte_can(lic, "Export_PDF") == 0, "keys are compared case-sensitively");
        CHECK(latte_can(lic, " export_pdf") == 0, "keys are not trimmed");
        CHECK(latte_can(lic, "export_pd") == 0, "a prefix is not a match");
        lic_free(lic);
    }

    /* NULL tolerance: an accessor is never the thing that crashes an app. */
    {
        CHECK(latte_can(NULL, "x") == 0, "can(NULL) is 0");
        CHECK(latte_limit(NULL, "x", NULL) == 0, "limit(NULL) misses");
        CHECK(latte_has_entitlements(NULL) == 0, "has_entitlements(NULL) is 0");
        latte_license *lic = lic_from_ent_json("{\"a\":true}");
        CHECK(latte_can(lic, NULL) == 0, "can(lic, NULL) is 0");
        lic_free(lic);
    }

    printf("\n%d passed, %d failed\n", pass, fail);
    return fail == 0 ? 0 : 1;
}
