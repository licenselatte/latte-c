#ifndef LATTE_DOMAIN_H
#define LATTE_DOMAIN_H

#include "latte/latte.h"

#include <stdint.h>
#include <time.h>

/* Internal cert chain (corresponds to domain.CertChain in Go). */
typedef struct {
    char *submaster;
    char *project;
    char *daily;
} ll_cert_chain;

/*
 * Internal validated license (corresponds to domain.License in Go).
 * All string fields heap-allocated; free with ll_license_free().
 */
typedef struct {
    char    *key;            /* sub claim: license key, no hyphens */
    /*
     * alias claim: the legacy-system key string this license was resolved
     * from, when it was minted via a legacy-key migration alias rather than
     * activated by its own native key. NULL (or "") for a natively-keyed
     * license. Internal only -- used to recognize a cached token on a later
     * latte_activate() call passing the same legacy key, since `key` above
     * will be the newly minted native key instead.
     */
    char    *alias;
    char    *activation_id;  /* aid */
    char    *project_id;     /* pid */
    char    *machine_id_hash;/* mid */
    char    *license_type;   /* ltype */
    int64_t  issued_at;      /* iat (Unix seconds) */
    int64_t  expires_at;     /* licence end, Unix seconds: exp with grc, else lex or LATTE_NO_EXPIRY */
    int64_t  grace_period;   /* offline window from iat, seconds: grc, else exp - iat */

    /* pmd sub-object: flat string→string map */
    size_t   metadata_count;
    char   **metadata_keys;
    char   **metadata_values;

    /*
     * ent sub-object: the typed entitlements map. has_entitlements records
     * whether the claim was present at all -- an empty claim is not the
     * same as an absent one, and only that flag can tell them apart.
     */
    int                has_entitlements;
    size_t             entitlement_count;
    latte_entitlement *entitlements;
} ll_license;

void ll_license_free(ll_license *l);
void ll_cert_chain_free(ll_cert_chain *c);

#endif /* LATTE_DOMAIN_H */
