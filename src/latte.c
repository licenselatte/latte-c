#include "../include/latte/latte.h"
#include "domain.h"
#include "constants.h"
#include "errors.h"
#include "appid.h"
#include "verify.h"
#include "validate.h"
#include "machineid.h"
#include "crypto/key.h"
#include "crypto/jwt.h"
#include "storage/file.h"
#include "http/client.h"
#include "thread.h"
#include <sodium.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#if !defined(_WIN32)
#  include <unistd.h>
#endif

/* -------------------------------------------------------------------------
 * SDK handle
 * ------------------------------------------------------------------------- */

struct latte_sdk {
    char               app_id[128];
    char               app_key[33];      /* 32-char segment + NUL */
    char               store_path[512];
    char               machine_id[65];   /* 64-char hex + NUL */
    unsigned char      master_pubs[LATTE_MASTER_PUBKEY_COUNT * 32];
    ll_http_client    *http;

    /* Background renewal state */
    ll_mutex           renew_mu;
    ll_thread          renew_thread;
    int                renew_in_flight;
    time_t             last_renew_attempt;
    /* The last renewal attempt could not reach the server and nothing has
     * succeeded since. In memory only; guarded by renew_mu. */
    int                renew_failed;
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
static latte_license *domain_to_public(latte_sdk *sdk, const ll_license *d)
{
    latte_license *pub = (latte_license *)calloc(1, sizeof(latte_license));
    if (!pub) return NULL;

    /* InGracePeriod = renewal_failed && 60 min < age < grace_period.
     * Shared with the fixture runner via ll_in_grace_period_at so the two
     * cannot drift. */
    ll_mutex_lock(&sdk->renew_mu);
    int renew_failed = sdk->renew_failed;
    ll_mutex_unlock(&sdk->renew_mu);
    pub->in_grace_period = (uint32_t)ll_in_grace_period_at(d, (int64_t)time(NULL), renew_failed);

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

    /* has_entitlements is copied unconditionally: an empty ent claim still
     * sets it, and that is the whole distinction it carries. */
    pub->has_entitlements = d->has_entitlements ? 1u : 0u;
    if (d->entitlement_count > 0) {
        pub->entitlements =
            (latte_entitlement *)calloc(d->entitlement_count, sizeof(latte_entitlement));
        if (pub->entitlements) {
            for (size_t i = 0; i < d->entitlement_count; i++) {
                pub->entitlements[i]      = d->entitlements[i];
                pub->entitlements[i].key  = strdup_safe(d->entitlements[i].key);
            }
            pub->entitlement_count = (uint32_t)d->entitlement_count;
        }
    }
    return pub;
}


/*
 * Reads the activation id (aid) and licence key (sub) out of the cached
 * token into aid[256] and key[64]. The token is not verified: the server
 * authenticates the calls these credentials go to, and a machine whose token
 * has outlived its grace window or its certificate chain still holds a seat
 * it must be able to release. Returns 0, or -1 when there is no usable
 * cached token.
 */
static int cached_credentials(const latte_sdk *sdk, char aid[256], char key[64])
{
    char *raw_token = NULL;
    ll_cert_chain *chain = NULL;
    if (ll_file_load_token(sdk->store_path, &raw_token, &chain) < 0) return -1;
    ll_cert_chain_free(chain);

    cJSON *claims = NULL;
    int rc = ll_jwt_decode_unverified(raw_token, &claims);
    free(raw_token);
    if (rc < 0) return -1;

    const char *a = ll_jwt_string_claim(claims, "aid");
    const char *k = ll_jwt_string_claim(claims, "sub");
    rc = a && a[0] && k && k[0] &&
         (size_t)snprintf(aid, 256, "%s", a) < 256 &&
         (size_t)snprintf(key, 64, "%s", k) < 64 ? 0 : -1;
    cJSON_Delete(claims);
    return rc;
}

/* Whether the cached token belongs to activation_id. */
static int holds_activation(const latte_sdk *sdk, const char *activation_id)
{
    char aid[256], key[64];
    return cached_credentials(sdk, aid, key) == 0 && strcmp(aid, activation_id) == 0;
}

static int should_try_renew(latte_sdk *sdk, const ll_license *lic)
{
    ll_mutex_lock(&sdk->renew_mu);

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

/* Clears renew_failed after a renewal that reached the server but produced no
 * usable token, while the cache still holds the activation it renewed. */
static void clear_renew_failed(latte_sdk *sdk, const char *activation_id)
{
    ll_mutex_lock(&sdk->renew_mu);
    if (holds_activation(sdk, activation_id)) sdk->renew_failed = 0;
    ll_mutex_unlock(&sdk->renew_mu);
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

    if (pe != LL_PORT_OK) {
        /* renew_failed is set only when the server could not be reached, and
         * only while the cache still holds the activation this renewed: after
         * a latte_deactivate and a fresh activation it holds a new one. If
         * the server says the license is invalid, wipe the cache under the
         * same condition. */
        int wipe = pe == LL_PORT_ERR_LICENSE_INACTIVE_OR_EXPIRED ||
                   pe == LL_PORT_ERR_LICENSE_NOT_FOUND           ||
                   pe == LL_PORT_ERR_INVALID_LICENSE;
        ll_mutex_lock(&sdk->renew_mu);
        if (holds_activation(sdk, ra->activation_id)) {
            sdk->renew_failed = pe == LL_PORT_ERR_NETWORK;
            if (wipe) ll_file_save_token(sdk->store_path, "", NULL);
        }
        ll_mutex_unlock(&sdk->renew_mu);
        goto done;
    }

    {
        ll_license *lic2 = NULL;
        ll_port_error ve = LL_PORT_OK;
        if (ll_verify_activation_any(sdk->master_pubs, LATTE_MASTER_PUBKEY_COUNT, token, chain, &lic2, &ve) < 0) {
            free(token); ll_cert_chain_free(chain);
            clear_renew_failed(sdk, ra->activation_id);
            goto done;
        }
        if (ll_validate(lic2, sdk->machine_id) != LL_PORT_OK) {
            ll_license_free(lic2);
            free(token); ll_cert_chain_free(chain);
            clear_renew_failed(sdk, ra->activation_id);
            goto done;
        }
        ll_license_free(lic2);

        /* latte_deactivate clears the cache under renew_mu, so a renewal
         * that finishes afterwards finds a different activation, or none,
         * and leaves the cache alone. */
        ll_mutex_lock(&sdk->renew_mu);
        if (holds_activation(sdk, ra->activation_id)) {
            ll_file_save_token(sdk->store_path, token, chain);
            sdk->renew_failed = 0;
        }
        ll_mutex_unlock(&sdk->renew_mu);
    }

    free(token);
    ll_cert_chain_free(chain);

done:
    free(ra);
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
 * Config builder
 * ------------------------------------------------------------------------- */

struct latte_config {
    char *app_id;
    int   multi_instance;
    char *machine_id;        /* raw override; NULL means read the OS ID */
    int   machine_id_failed; /* the override could not be copied */
    char *machine_name;      /* NULL means the hostname; "" sends none */
    int   machine_name_failed;
};

latte_config *latte_config_new(const char *app_id)
{
    if (!app_id) return NULL;
    latte_config *cfg = (latte_config *)calloc(1, sizeof(latte_config));
    if (!cfg) return NULL;
    cfg->app_id = strdup_safe(app_id);
    if (!cfg->app_id) { free(cfg); return NULL; }
    return cfg;
}

latte_config *latte_config_set_multi_instance(latte_config *cfg, int multi_instance)
{
    if (cfg) cfg->multi_instance = multi_instance;
    return cfg;
}

latte_config *latte_config_set_machine_id(latte_config *cfg, const char *raw_machine_id)
{
    if (!cfg) return cfg;
    free(cfg->machine_id);
    cfg->machine_id = NULL;
    cfg->machine_id_failed = 0;
    if (raw_machine_id && raw_machine_id[0]) {
        cfg->machine_id = strdup_safe(raw_machine_id);
        if (!cfg->machine_id) cfg->machine_id_failed = 1;
    }
    return cfg;
}

latte_config *latte_config_set_machine_name(latte_config *cfg, const char *name)
{
    if (!cfg) return cfg;
    free(cfg->machine_name);
    cfg->machine_name = NULL;
    cfg->machine_name_failed = 0;
    if (name) {
        cfg->machine_name = strdup_safe(name);
        if (!cfg->machine_name) cfg->machine_name_failed = 1;
    }
    return cfg;
}

void latte_config_free(latte_config *cfg)
{
    if (!cfg) return;
    free(cfg->app_id);
    free(cfg->machine_id);
    free(cfg->machine_name);
    free(cfg);
}

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

latte_status latte_new(const latte_config *config, latte_sdk **out)
{
    *out = NULL;
    if (!config || !config->app_id) return LATTE_ERR_INVALID_CONFIG;
    if (config->machine_id_failed || config->machine_name_failed) return LATTE_ERR_INTERNAL;
    const char *app_id = config->app_id;

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

    static const char *const master_hex[] = LATTE_MASTER_PUBKEYS_HEX;
    _Static_assert(sizeof(master_hex) / sizeof(master_hex[0]) == LATTE_MASTER_PUBKEY_COUNT,
                   "LATTE_MASTER_PUBKEY_COUNT must match LATTE_MASTER_PUBKEYS_HEX");
    for (size_t i = 0; i < LATTE_MASTER_PUBKEY_COUNT; i++) {
        if (hex_decode(master_hex[i], sdk->master_pubs + 32 * i, 32) < 0) {
            free(sdk); return LATTE_ERR_INTERNAL;
        }
    }

    char store_path[512];
    int path_ok = config->multi_instance
        ? ll_resolve_storage_path_local(sdk->app_key, store_path, sizeof(store_path))
        : ll_resolve_storage_path(sdk->app_key, store_path, sizeof(store_path));
    if (path_ok < 0) {
        free(sdk); return LATTE_ERR_STORAGE_INIT_FAILED;
    }
    strncpy(sdk->store_path, store_path, sizeof(sdk->store_path) - 1);

    /* Machine fingerprint: HMAC-SHA256(key=raw machine ID, msg="licenselatte_"+AppID) */
    int mid_rc = config->machine_id
        ? ll_machine_id_hash(config->machine_id, app_id, sdk->machine_id)
        : ll_machine_id_protected(app_id, sdk->machine_id);
    if (mid_rc < 0) {
        free(sdk); return LATTE_ERR_MACHINE_ID_FAILED;
    }

    /* The name is a courtesy, so a hostname the OS will not give is no name. */
    char hostname[256] = "";
    const char *machine_name = config->machine_name;
    if (!machine_name) {
#if defined(_WIN32)
        DWORD n = sizeof(hostname);
        if (!GetComputerNameA(hostname, &n)) hostname[0] = '\0';
#else
        if (gethostname(hostname, sizeof(hostname) - 1) != 0) hostname[0] = '\0';
#endif
        machine_name = hostname;
    }

    sdk->http = ll_http_client_new(base_url, app_id, machine_name);
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

    /* Sanitised (native-alphabet fold) is only used to match a cached
     * license's `key` (sub claim), which is always the canonical native
     * form. Normalized (uppercase + strip separators, no fold) is what's
     * actually sent over the wire and matched against a cached license's
     * `alias` claim -- a license key may be native or a legacy-system
     * alias, and only the server knows which, so anything beyond a
     * minimal sanity check below is deferred to it. */
    char sanitized[64];
    char normalized[64];
    ll_sanitize_key(key, sanitized, sizeof(sanitized));
    ll_normalize_key(key, normalized, sizeof(normalized));

    /* Minimal local sanity check: reject obviously-not-a-key input
     * (empty, or implausibly long) without a network round trip. */
    size_t normalized_len = strlen(normalized);
    if (normalized_len == 0 || normalized_len > 256) return LATTE_ERR_INVALID_KEY;

    /* Fast path: try the cached token */
    char *raw_token = NULL;
    ll_cert_chain *chain = NULL;
    if (ll_file_load_token(sdk->store_path, &raw_token, &chain) == 0) {
        ll_license *lic = NULL;
        ll_port_error ve = LL_PORT_OK;
        if (ll_verify_activation_any(sdk->master_pubs, LATTE_MASTER_PUBKEY_COUNT, raw_token, chain, &lic, &ve) == 0) {
            if (ll_validate(lic, sdk->machine_id) == LL_PORT_OK) {
                maybe_start_renew(sdk, lic);
                int key_matches = lic->key && strcmp(lic->key, sanitized) == 0;
                int alias_matches = lic->alias && lic->alias[0] != '\0' &&
                                     strcmp(lic->alias, normalized) == 0;
                if ((key_matches || alias_matches) && ll_license_is_valid(lic)) {
                    latte_license *pub = domain_to_public(sdk, lic);
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
    ll_port_error pe = ll_http_activate(sdk->http, normalized, sdk->machine_id,
                                        &raw_token, &chain);
    if (pe != LL_PORT_OK) return map_network_error(pe);

    ll_license *lic = NULL;
    ll_port_error ve = LL_PORT_OK;
    if (ll_verify_activation_any(sdk->master_pubs, LATTE_MASTER_PUBKEY_COUNT, raw_token, chain, &lic, &ve) < 0) {
        free(raw_token); ll_cert_chain_free(chain);
        return LATTE_ERR_SERVER_INVALID_TOKEN;
    }
    if (ll_validate(lic, sdk->machine_id) != LL_PORT_OK) {
        ll_license_free(lic);
        free(raw_token); ll_cert_chain_free(chain);
        return LATTE_ERR_SERVER_INVALID_TOKEN;
    }

    ll_mutex_lock(&sdk->renew_mu);
    ll_file_save_token(sdk->store_path, raw_token, chain);
    sdk->renew_failed = 0;
    ll_mutex_unlock(&sdk->renew_mu);
    latte_license *pub = domain_to_public(sdk, lic);
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
    if (ll_verify_activation_any(sdk->master_pubs, LATTE_MASTER_PUBKEY_COUNT, raw_token, chain, &lic, &ve) < 0) {
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

    latte_license *pub = domain_to_public(sdk, lic);
    ll_license_free(lic);
    if (!pub) return LATTE_ERR_INTERNAL;
    *out = pub;
    return LATTE_OK;
}

latte_status latte_deactivate(latte_sdk *sdk, latte_deactivate_result *out)
{
    latte_deactivate_result discard;
    if (!out) out = &discard;
    memset(out, 0, sizeof(*out));
    if (!sdk) return LATTE_ERR_INTERNAL;

    char aid[256], key[64];
    if (cached_credentials(sdk, aid, key) < 0) return LATTE_ERR_NOT_ACTIVATED;

    /* The server first: the cache is cleared only once the seat is released,
     * so any failure leaves this machine as it was and the call retryable. */
    ll_port_error pe = ll_http_deactivate(sdk->http, aid, key, sdk->machine_id, out);
    if (pe != LL_PORT_OK) return map_network_error(pe);

    /* Under renew_mu: an in-flight renewal saves only while the cache still
     * holds its activation, so it cannot write a token back after this. */
    ll_mutex_lock(&sdk->renew_mu);
    ll_file_save_token(sdk->store_path, "", NULL);
    sdk->renew_failed = 0;
    ll_mutex_unlock(&sdk->renew_mu);
    return LATTE_OK;
}

latte_status latte_machines(latte_sdk *sdk, latte_machine **out, size_t *count)
{
    if (!sdk || !out || !count) return LATTE_ERR_INTERNAL;
    *out = NULL;
    *count = 0;

    char aid[256], key[64];
    if (cached_credentials(sdk, aid, key) < 0) return LATTE_ERR_NOT_ACTIVATED;

    ll_port_error pe = ll_http_machines(sdk->http, aid, key, sdk->machine_id, out, count);
    return pe == LL_PORT_OK ? LATTE_OK : map_network_error(pe);
}

void latte_machines_free(latte_machine *machines, size_t count)
{
    if (!machines) return;
    for (size_t i = 0; i < count; i++) {
        free(machines[i].name);
        free(machines[i].fingerprint);
    }
    free(machines);
}

latte_status latte_rename_machine(latte_sdk *sdk, const char *name,
                                  char *stored, size_t stored_size)
{
    if (stored && stored_size) stored[0] = '\0';
    if (!sdk || !name) return LATTE_ERR_INTERNAL;

    char aid[256], key[64];
    if (cached_credentials(sdk, aid, key) < 0) return LATTE_ERR_NOT_ACTIVATED;

    ll_port_error pe = ll_http_rename_machine(sdk->http, aid, key, sdk->machine_id,
                                              name, stored, stored_size);
    return pe == LL_PORT_OK ? LATTE_OK : map_network_error(pe);
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
    for (uint32_t i = 0; i < lic->entitlement_count; i++) {
        free(lic->entitlements[i].key);
    }
    free(lic->entitlements);
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
