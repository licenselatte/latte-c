#ifndef LATTE_ENTITLEMENTS_H
#define LATTE_ENTITLEMENTS_H

#include "latte/latte.h"

#include <cJSON.h>
#include <stddef.h>

/*
 * ll_decode_entitlements: narrow a raw `ent` claim object to the two types
 * the format admits, writing the result into *out_list / *out_count.
 *
 * Returns 1 when the claim was present (so the caller sets
 * has_entitlements), 0 when ent is NULL or not an object. A present but
 * empty claim returns 1 with a count of 0 -- an empty claim and an absent
 * one deny identically and differ only in that flag, which is the whole
 * reason the flag exists.
 *
 * Only booleans and whole finite numbers survive; a string, a fractional
 * number, a nested object, an array or a null is dropped and the licence
 * stays valid. Rejecting a token because a seller managed to get a string
 * into one value would take a working product offline for a data-entry
 * mistake, on a machine that cannot be reached to fix it. Refusing bad
 * values is the server's job at write time, where there is a human and an
 * error message.
 *
 * The caller owns *out_list: free each .key, then the array.
 */
int ll_decode_entitlements(const cJSON *ent, latte_entitlement **out_list, size_t *out_count);

#endif /* LATTE_ENTITLEMENTS_H */
