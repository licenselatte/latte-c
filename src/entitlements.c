#include "entitlements.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static char *ent_strdup(const char *s)
{
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char *p = (char *)malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

int ll_decode_entitlements(const cJSON *ent, latte_entitlement **out_list, size_t *out_count)
{
    if (out_list) *out_list = NULL;
    if (out_count) *out_count = 0;
    if (!ent || !cJSON_IsObject(ent)) return 0;
    if (!out_list || !out_count) return 1;

    /* Upper bound first, so the array is sized once. */
    int capacity = 0;
    const cJSON *item;
    cJSON_ArrayForEach(item, ent) {
        if (cJSON_IsBool(item) || cJSON_IsNumber(item)) capacity++;
    }
    if (capacity == 0) return 1;

    latte_entitlement *list =
        (latte_entitlement *)calloc((size_t)capacity, sizeof(latte_entitlement));
    if (!list) return 1; /* Out of memory degrades to "no entitlements", never to a failed licence. */

    size_t idx = 0;
    cJSON_ArrayForEach(item, ent) {
        if (idx >= (size_t)capacity || !item->string) continue;

        if (cJSON_IsBool(item)) {
            list[idx].key        = ent_strdup(item->string);
            list[idx].kind       = LATTE_ENT_BOOL;
            list[idx].bool_value = cJSON_IsTrue(item) ? 1 : 0;
            idx++;
        } else if (cJSON_IsNumber(item)) {
            /*
             * cJSON reports every number as a double, so an integer is
             * recognised by being whole rather than by its type: 25 and 25.0
             * are both 25, and 1.5 is dropped. Go's and JavaScript's JSON
             * parsers cannot tell those two apart either, so a rule that
             * distinguished them is one three of the five SDKs could not
             * implement.
             */
            double d = item->valuedouble;
            if (!isfinite(d) || d != trunc(d) ||
                d < -9223372036854775808.0 || d >= 9223372036854775808.0)
                continue;
            list[idx].key       = ent_strdup(item->string);
            list[idx].kind      = LATTE_ENT_INT;
            list[idx].int_value = (int64_t)d;
            idx++;
        }
    }

    *out_list  = list;
    *out_count = idx;
    return 1;
}

/*
 * find_entitlement: byte-exact key lookup, no case folding and no trimming.
 * The server enforces the ^[a-z][a-z0-9_]{0,63}$ charset at write time so
 * clients never have to.
 */
static const latte_entitlement *find_entitlement(const latte_license *lic, const char *key)
{
    if (!lic || !key || !lic->entitlements) return NULL;
    for (uint32_t i = 0; i < lic->entitlement_count; i++) {
        if (lic->entitlements[i].key && strcmp(lic->entitlements[i].key, key) == 0)
            return &lic->entitlements[i];
    }
    return NULL;
}

int latte_can(const latte_license *lic, const char *key)
{
    const latte_entitlement *e = find_entitlement(lic, key);
    /* No coercion: an integer entitlement is not "true when non-zero". */
    if (!e || e->kind != LATTE_ENT_BOOL) return 0;
    return e->bool_value ? 1 : 0;
}

int latte_limit(const latte_license *lic, const char *key, int64_t *out)
{
    const latte_entitlement *e = find_entitlement(lic, key);
    /* No coercion: a boolean entitlement is not a 1 or a 0. */
    if (!e || e->kind != LATTE_ENT_INT) return 0;
    if (out) *out = e->int_value;
    return 1;
}

int latte_has_entitlements(const latte_license *lic)
{
    return (lic && lic->has_entitlements) ? 1 : 0;
}
