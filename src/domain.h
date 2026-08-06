#ifndef LATTE_DOMAIN_H
#define LATTE_DOMAIN_H

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
    char    *activation_id;  /* aid */
    char    *project_id;     /* pid */
    char    *machine_id_hash;/* mid */
    char    *license_type;   /* ltype */
    int64_t  issued_at;      /* iat (Unix seconds) */
    int64_t  expires_at;     /* exp (Unix seconds) */
    int64_t  grace_period;   /* grc (seconds) */

    /* pmd sub-object: flat string→string map */
    size_t   metadata_count;
    char   **metadata_keys;
    char   **metadata_values;
} ll_license;

void ll_license_free(ll_license *l);
void ll_cert_chain_free(ll_cert_chain *c);

#endif /* LATTE_DOMAIN_H */
