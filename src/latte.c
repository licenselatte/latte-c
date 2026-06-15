#include "../include/latte/latte.h"
#include "domain.h"
#include "constants.h"
#include "errors.h"
#include "appid.h"
#include "verify.h"
#include "validate.h"
#include "machineid.h"
#include "crypto/key.h"
#include "storage/file.h"
#include "http/client.h"
#include "thread.h"
#include <sodium.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

/* -------------------------------------------------------------------------
 * SDK handle
 * ------------------------------------------------------------------------- */

struct latte_sdk {
    char               app_id[128];
    char               app_key[33];      /* 32-char segment + NUL */
    char               store_path[512];
    char               machine_id[65];   /* 64-char hex + NUL */
    unsigned char      master_pub[32];
    ll_http_client    *http;

    /* Background renewal state */
    ll_mutex           renew_mu;
    ll_thread          renew_thread;
    int                renew_in_flight;
    time_t             last_renew_attempt;
};

/* -------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------- */

static char *strdup_safe(const char *s)
{
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char *p = (char *)malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

static int hex_decode(const char *hex, unsigned char *out, size_t out_len)
{
    for (size_t i = 0; i < out_len; i++) {
        unsigned int byte;
        if (sscanf(hex + 2*i, "%02x", &byte) != 1) return -1;
        out[i] = (unsigned char)byte;
    }
    return 0;
}

/*
 * Build a public latte_license from the internal ll_license.
 * Mirrors domainToPublic() in helpers.go.
 */
static latte_license *domain_to_public(const ll_license *d)
{
    latte_license *pub = (latte_license *)calloc(1, sizeof(latte_license));
    if (!pub) return NULL;

    time_t now = time(NULL);
    int64_t age = (int64_t)now - d->issued_at;
    /* InGracePeriod = age > maxRenewalTime (60 min) && age < grace_period */
    pub->in_grace_period = (age > LATTE_MAX_RENEWAL_SECS && age < d->grace_period) ? 1 : 0;

    pub->key             = strdup_safe(d->key);
    pub->activation_id   = strdup_safe(d->activation_id);
    pub->project_id      = strdup_safe(d->project_id);
    pub->license_type    = strdup_safe(d->license_type);
    pub->issued_at       = d->issued_at;
    pub->expires_at      = d->expires_at;
    pub->grace_period_seconds = d->grace_period;

    if (d->metadata_count > 0) {
        pub->metadata = (latte_kv *)calloc(d->metadata_count, sizeof(latte_kv));
        if (pub->metadata) {
            for (size_t i = 0; i < d->metadata_count; i++) {
                pub->metadata[i].key   = strdup_safe(d->metadata_keys[i]);
                pub->metadata[i].value = strdup_safe(d->metadata_values[i]);
            }
            pub->metadata_count = d->metadata_count;
        }
    }
    return pub;
}

static int should_try_renew(latte_sdk *sdk, const ll_license *lic)
{
    ll_mutex_lock(&sdk->renew_mu);

    if (lic->license_type && strcmp(lic->license_type, LATTE_TYPE_PERPETUAL_FIXED) == 0) {
        ll_mutex_unlock(&sdk->renew_mu);
        return 0;
    }
    if (sdk->renew_in_flight) {
        ll_mutex_unlock(&sdk->renew_mu);
        return 0;
    }

    time_t now = time(NULL);
    if ((now - sdk->last_renew_attempt) < LATTE_MIN_RENEWAL_SECS) {
        ll_mutex_unlock(&sdk->renew_mu);
        return 0;
    }
    if ((now - lic->issued_at) < LATTE_MAX_RENEWAL_SECS) {
        ll_mutex_unlock(&sdk->renew_mu);
        return 0;
    }

    sdk->renew_in_flight    = 1;
    sdk->last_renew_attempt = now;
    ll_mutex_unlock(&sdk->renew_mu);
    return 1;
}

/* Argument bundle passed to the renewal thread. */
typedef struct {
    latte_sdk *sdk;
    char       activation_id[256];
    char       key[64];
} renew_args;

static void *renew_thread_fn(void *arg)
{
    renew_args *ra = (renew_args *)arg;
    latte_sdk  *sdk = ra->sdk;

    char *token = NULL;
    ll_cert_chain *chain = NULL;

    ll_port_error pe = ll_http_renew(sdk->http,
                                     ra->activation_id,
                                     ra->key,
                                     sdk->machine_id,
                                     &token,
                                     &chain);
    free(ra);

    if (pe != LL_PORT_OK) {
        /* If the server says the license is invalid, wipe the cache. */
        if (pe == LL_PORT_ERR_LICENSE_INACTIVE_OR_EXPIRED ||
            pe == LL_PORT_ERR_LICENSE_NOT_FOUND           ||
            pe == LL_PORT_ERR_INVALID_LICENSE) {
            ll_file_save_token(sdk->store_path, "", NULL);
        }
        goto done;
    }

    {
        ll_license *lic2 = NULL;
        ll_port_error ve = LL_PORT_OK;
        if (ll_verify_activation(sdk->master_pub, token, chain, &lic2, &ve) < 0) {
            free(token); ll_cert_chain_free(chain);
            goto done;
        }
        if (ll_validate(lic2, sdk->machine_id) != LL_PORT_OK) {
            ll_license_free(lic2);
            free(token); ll_cert_chain_free(chain);
            goto done;
        }
        ll_license_free(lic2);
        ll_file_save_token(sdk->store_path, token, chain);
    }

    free(token);
    ll_cert_chain_free(chain);

done:
    ll_mutex_lock(&sdk->renew_mu);
    sdk->renew_in_flight = 0;
    ll_mutex_unlock(&sdk->renew_mu);
    return NULL;
}

static void maybe_start_renew(latte_sdk *sdk, const ll_license *lic)
{
    if (!should_try_renew(sdk, lic)) return;
    if (!lic->activation_id || !lic->key) {
        ll_mutex_lock(&sdk->renew_mu);
        sdk->renew_in_flight = 0;
        ll_mutex_unlock(&sdk->renew_mu);
        return;
    }

    /* Wait for any previous thread before spawning a new one. */
    ll_thread_join(&sdk->renew_thread);

    renew_args *ra = (renew_args *)calloc(1, sizeof(renew_args));
    if (!ra) {
        ll_mutex_lock(&sdk->renew_mu);
        sdk->renew_in_flight = 0;
        ll_mutex_unlock(&sdk->renew_mu);
        return;
    }
    ra->sdk = sdk;
    strncpy(ra->activation_id, lic->activation_id, sizeof(ra->activation_id) - 1);
    strncpy(ra->key, lic->key, sizeof(ra->key) - 1);

    if (ll_thread_spawn(&sdk->renew_thread, renew_thread_fn, ra) < 0) {
        free(ra);
        ll_mutex_lock(&sdk->renew_mu);
        sdk->renew_in_flight = 0;
        ll_mutex_unlock(&sdk->renew_mu);
    }
}

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

latte_status latte_new(const char *app_id, latte_sdk **out)
{
    *out = NULL;
    if (sodium_init() < 0) return LATTE_ERR_INTERNAL;

    ll_env env;
    const char *app_key = NULL;
    latte_status st = ll_parse_app_id(app_id, &env, &app_key);
    if (st != LATTE_OK) return st;

    const char *base_url;
    switch (env) {
    case LL_ENV_LIVE:  base_url = LATTE_URL_LIVE;  break;
    case LL_ENV_TEST:  base_url = LATTE_URL_TEST;  break;
    default:           base_url = LATTE_URL_LOCAL; break;
    }

    latte_sdk *sdk = (latte_sdk *)calloc(1, sizeof(latte_sdk));
    if (!sdk) return LATTE_ERR_INTERNAL;

    strncpy(sdk->app_id, app_id, sizeof(sdk->app_id) - 1);
    strncpy(sdk->app_key, app_key, 32);

    if (hex_decode(LATTE_MASTER_PUBKEY_HEX, sdk->master_pub, 32) < 0) {
        free(sdk); return LATTE_ERR_INTERNAL;
    }

    char store_path[512];
    if (ll_resolve_storage_path(sdk->app_key, store_path, sizeof(store_path)) < 0) {
        free(sdk); return LATTE_ERR_STORAGE_INIT_FAILED;
    }
    strncpy(sdk->store_path, store_path, sizeof(sdk->store_path) - 1);

    /* Compute machine fingerprint: HMAC-SHA256(key=machine_id, msg="licenselatte_"+AppID) */
    char protect_key[256];
    snprintf(protect_key, sizeof(protect_key), "licenselatte_%s", app_id);
    if (ll_machine_id_protected(protect_key, sdk->machine_id) < 0) {
        free(sdk); return LATTE_ERR_MACHINE_ID_FAILED;
    }

    sdk->http = ll_http_client_new(base_url, app_id);
    if (!sdk->http) { free(sdk); return LATTE_ERR_INTERNAL; }

    ll_mutex_init(&sdk->renew_mu);
    memset(&sdk->renew_thread, 0, sizeof(sdk->renew_thread));

    *out = sdk;
    return LATTE_OK;
}

latte_status latte_activate(latte_sdk *sdk, const char *key, latte_license **out)
{
    if (!sdk || !key || !out) return LATTE_ERR_INTERNAL;
    *out = NULL;

    /* Sanitise the key */
    char sanitized[64];
    ll_sanitize_key(key, sanitized, sizeof(sanitized));

    /* Validate format: length 30, first 6 chars match app_key[:6], 2-char checksum */
    if (strlen(sanitized) != 30) return LATTE_ERR_INVALID_KEY;
    if (strncmp(sanitized, sdk->app_key, 6) != 0) return LATTE_ERR_INVALID_KEY;
    if (!ll_validate_key(sanitized + 6, 2)) return LATTE_ERR_INVALID_KEY;

    /* Fast path: try the cached token */
    char *raw_token = NULL;
    ll_cert_chain *chain = NULL;
    if (ll_file_load_token(sdk->store_path, &raw_token, &chain) == 0) {
        ll_license *lic = NULL;
        ll_port_error ve = LL_PORT_OK;
        if (ll_verify_activation(sdk->master_pub, raw_token, chain, &lic, &ve) == 0) {
            if (ll_validate(lic, sdk->machine_id) == LL_PORT_OK) {
                maybe_start_renew(sdk, lic);
                if (lic->key && strcmp(lic->key, sanitized) == 0 && ll_license_is_valid(lic)) {
                    latte_license *pub = domain_to_public(lic);
                    ll_license_free(lic);
                    free(raw_token); ll_cert_chain_free(chain);
                    if (!pub) return LATTE_ERR_INTERNAL;
                    *out = pub;
                    return LATTE_OK;
                }
            }
            ll_license_free(lic);
        }
        free(raw_token); ll_cert_chain_free(chain);
        raw_token = NULL; chain = NULL;
    }

    /* Slow path: activate via API */
    ll_port_error pe = ll_http_activate(sdk->http, sanitized, sdk->machine_id,
                                        &raw_token, &chain);
    if (pe != LL_PORT_OK) return map_network_error(pe);

    ll_license *lic = NULL;
    ll_port_error ve = LL_PORT_OK;
    if (ll_verify_activation(sdk->master_pub, raw_token, chain, &lic, &ve) < 0) {
        free(raw_token); ll_cert_chain_free(chain);
        return LATTE_ERR_SERVER_INVALID_TOKEN;
    }
    if (ll_validate(lic, sdk->machine_id) != LL_PORT_OK) {
        ll_license_free(lic);
        free(raw_token); ll_cert_chain_free(chain);
        return LATTE_ERR_SERVER_INVALID_TOKEN;
    }

    ll_file_save_token(sdk->store_path, raw_token, chain);
    latte_license *pub = domain_to_public(lic);
    ll_license_free(lic);
    free(raw_token); ll_cert_chain_free(chain);

    if (!pub) return LATTE_ERR_INTERNAL;
    *out = pub;
    return LATTE_OK;
}

latte_status latte_check(latte_sdk *sdk, latte_license **out)
{
    if (!sdk || !out) return LATTE_ERR_INTERNAL;
    *out = NULL;

    char *raw_token = NULL;
    ll_cert_chain *chain = NULL;
    if (ll_file_load_token(sdk->store_path, &raw_token, &chain) < 0)
        return LATTE_ERR_NOT_ACTIVATED;

    ll_license *lic = NULL;
    ll_port_error ve = LL_PORT_OK;
    if (ll_verify_activation(sdk->master_pub, raw_token, chain, &lic, &ve) < 0) {
        free(raw_token); ll_cert_chain_free(chain);
        if (ve == LL_PORT_ERR_LICENSE_INACTIVE_OR_EXPIRED) return LATTE_ERR_LICENSE_EXPIRED;
        return LATTE_ERR_NOT_ACTIVATED;
    }
    free(raw_token); ll_cert_chain_free(chain);

    ll_port_error vr = ll_validate(lic, sdk->machine_id);
    if (vr != LL_PORT_OK) {
        ll_license_free(lic);
        return LATTE_ERR_NOT_ACTIVATED;
    }

    maybe_start_renew(sdk, lic);

    if (!ll_license_is_valid(lic)) {
        ll_license_free(lic);
        return LATTE_ERR_NOT_ACTIVATED;
    }

    latte_license *pub = domain_to_public(lic);
    ll_license_free(lic);
    if (!pub) return LATTE_ERR_INTERNAL;
    *out = pub;
    return LATTE_OK;
}

void latte_license_free(latte_license *lic)
{
    if (!lic) return;
    free(lic->key);
    free(lic->activation_id);
    free(lic->project_id);
    free(lic->license_type);
    for (size_t i = 0; i < lic->metadata_count; i++) {
        free(lic->metadata[i].key);
        free(lic->metadata[i].value);
    }
    free(lic->metadata);
    free(lic);
}

void latte_free(latte_sdk *sdk)
{
    if (!sdk) return;
    /* Wait for any in-flight renewal so we don't free under it. */
    ll_thread_join(&sdk->renew_thread);
    ll_mutex_destroy(&sdk->renew_mu);
    ll_http_client_free(sdk->http);
    free(sdk);
}
