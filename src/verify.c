#include "verify.h"
#include "constants.h"
#include "crypto/cert.h"
#include "crypto/jwt.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

static char *strdup_safe(const char *s)
{
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char *p = (char *)malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

int ll_verify_activation(const unsigned char *master_pub,
                         const char *token,
                         const ll_cert_chain *chain,
                         ll_license **out,
                         ll_port_error *err_code)
{
    *out = NULL;
    *err_code = LL_PORT_ERR_INVALID_LICENSE;

    unsigned char submaster_pub[32], project_pub[32], daily_pub[32];
    cJSON *sub_claims = NULL, *proj_claims = NULL,
          *daily_claims = NULL, *act_claims = NULL;

    /* Step 1: verify submaster cert (signed by master) → extract spk */
    if (ll_verify_cert(master_pub, chain->submaster, &sub_claims) < 0)
        goto fail;
    if (ll_pubkey_from_cert(sub_claims, "spk", submaster_pub) < 0)
        goto fail;

    /* Step 2: verify project cert (signed by submaster) → extract ppk */
    if (ll_verify_cert(submaster_pub, chain->project, &proj_claims) < 0)
        goto fail;
    if (ll_pubkey_from_cert(proj_claims, "ppk", project_pub) < 0)
        goto fail;

    /* Step 3: verify daily cert (signed by project key) → extract dpk */
    if (ll_verify_cert(project_pub, chain->daily, &daily_claims) < 0)
        goto fail;
    if (ll_pubkey_from_cert(daily_claims, "dpk", daily_pub) < 0)
        goto fail;

    /* Step 4: verify activation JWT (signed by daily key) */
    if (ll_jwt_verify_ed25519(token, daily_pub, 32, &act_claims) < 0)
        goto fail;

    /* Extract claims */
    int iat_ok = 0, exp_ok = 0;
    int64_t iat = ll_jwt_int64_claim(act_claims, "iat", &iat_ok);
    int64_t exp = ll_jwt_int64_claim(act_claims, "exp", &exp_ok);
    if (!iat_ok || !exp_ok) goto fail;

    int grc_ok = 0;
    int64_t grc = ll_jwt_int64_claim(act_claims, "grc", &grc_ok);

    /* Cross-check: pid in JWT must match pid in project cert */
    const char *pid_jwt  = ll_jwt_string_claim(act_claims, "pid");
    const char *pid_cert = ll_jwt_string_claim(proj_claims, "pid");
    if (pid_cert && *pid_cert && pid_jwt && strcmp(pid_jwt, pid_cert) != 0)
        goto fail;

    /* Cross-check: activation iat >= daily cert iat */
    int daily_iat_ok = 0;
    int64_t daily_iat = ll_jwt_int64_claim(daily_claims, "iat", &daily_iat_ok);
    if (!daily_iat_ok) goto fail;
    if (iat < daily_iat) goto fail;

    /* Cross-check: activation iat <= daily cert exp */
    int daily_exp_ok = 0;
    int64_t daily_exp = ll_jwt_int64_claim(daily_claims, "exp", &daily_exp_ok);
    if (!daily_exp_ok) goto fail;
    if (iat > daily_exp) goto fail;

    /* Cross-check: grace period not too long */
    if (grc_ok && grc > LATTE_MAX_GRACE_SECS) goto fail;

    /* Build output */
    ll_license *lic = (ll_license *)calloc(1, sizeof(ll_license));
    if (!lic) goto fail;

    lic->key            = strdup_safe(ll_jwt_string_claim(act_claims, "sub"));
    lic->activation_id  = strdup_safe(ll_jwt_string_claim(act_claims, "aid"));
    lic->project_id     = strdup_safe(ll_jwt_string_claim(act_claims, "pid"));
    lic->machine_id_hash= strdup_safe(ll_jwt_string_claim(act_claims, "mid"));
    lic->license_type   = strdup_safe(ll_jwt_string_claim(act_claims, "ltype"));
    lic->issued_at      = iat;
    lic->expires_at     = exp;
    lic->grace_period   = grc_ok ? grc : 0;

    /* Extract pmd metadata (string→string map) */
    cJSON *pmd = cJSON_GetObjectItemCaseSensitive(act_claims, "pmd");
    if (pmd && cJSON_IsObject(pmd)) {
        int count = 0;
        cJSON *item;
        cJSON_ArrayForEach(item, pmd) { if (cJSON_IsString(item)) count++; }
        if (count > 0) {
            lic->metadata_keys   = (char **)calloc(count, sizeof(char *));
            lic->metadata_values = (char **)calloc(count, sizeof(char *));
            if (lic->metadata_keys && lic->metadata_values) {
                int idx = 0;
                cJSON_ArrayForEach(item, pmd) {
                    if (cJSON_IsString(item) && idx < count) {
                        lic->metadata_keys[idx]   = strdup_safe(item->string);
                        lic->metadata_values[idx] = strdup_safe(item->valuestring);
                        idx++;
                    }
                }
                lic->metadata_count = idx;
            }
        }
    }

    cJSON_Delete(sub_claims);
    cJSON_Delete(proj_claims);
    cJSON_Delete(daily_claims);
    cJSON_Delete(act_claims);

    *out = lic;
    *err_code = LL_PORT_OK;
    return 0;

fail:
    cJSON_Delete(sub_claims);
    cJSON_Delete(proj_claims);
    cJSON_Delete(daily_claims);
    cJSON_Delete(act_claims);
    return -1;
}
