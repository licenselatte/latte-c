#include "jwt.h"
#include "base64url.h"
#include <sodium.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

static int split_jwt(const char *token,
                     const char **h_start, size_t *h_len,
                     const char **p_start, size_t *p_len,
                     const char **s_start, size_t *s_len)
{
    const char *dot1 = strchr(token, '.');
    if (!dot1) return -1;
    const char *dot2 = strchr(dot1 + 1, '.');
    if (!dot2) return -1;

    *h_start = token;
    *h_len   = (size_t)(dot1 - token);
    *p_start = dot1 + 1;
    *p_len   = (size_t)(dot2 - (dot1 + 1));
    *s_start = dot2 + 1;
    *s_len   = strlen(dot2 + 1);
    return 0;
}

int ll_jwt_verify_ed25519(const char *token,
                           const unsigned char *pubkey, size_t pubkey_len,
                           cJSON **claims_out)
{
    *claims_out = NULL;
    if (pubkey_len != crypto_sign_ed25519_PUBLICKEYBYTES) return -1;

    const char *h_start, *p_start, *s_start;
    size_t h_len, p_len, s_len;
    if (split_jwt(token, &h_start, &h_len, &p_start, &p_len, &s_start, &s_len) < 0)
        return -1;

    /* Decode and verify the signature over "header.payload" (the signed message) */
    unsigned char *sig_bytes = NULL;
    size_t sig_len = 0;
    if (ll_base64url_decode(s_start, s_len, &sig_bytes, &sig_len) < 0)
        return -1;
    if (sig_len != crypto_sign_ed25519_BYTES) {
        free(sig_bytes);
        return -1;
    }

    /* signed_msg = "header_b64.payload_b64" (the raw ASCII portions of the JWT) */
    size_t signed_len = h_len + 1 + p_len; /* header + '.' + payload */
    unsigned char *signed_msg = (unsigned char *)malloc(signed_len);
    if (!signed_msg) { free(sig_bytes); return -1; }
    memcpy(signed_msg, h_start, h_len);
    signed_msg[h_len] = '.';
    memcpy(signed_msg + h_len + 1, p_start, p_len);

    int sig_ok = crypto_sign_ed25519_verify_detached(sig_bytes, signed_msg, signed_len, pubkey);
    free(sig_bytes);
    free(signed_msg);
    if (sig_ok != 0) return -1;

    /* Decode header and check alg = EdDSA */
    unsigned char *hdr_bytes = NULL;
    size_t hdr_len = 0;
    if (ll_base64url_decode(h_start, h_len, &hdr_bytes, &hdr_len) < 0) return -1;
    cJSON *hdr = cJSON_ParseWithLength((const char *)hdr_bytes, hdr_len);
    free(hdr_bytes);
    if (!hdr) return -1;
    cJSON *alg = cJSON_GetObjectItemCaseSensitive(hdr, "alg");
    int alg_ok = alg && cJSON_IsString(alg) && strcmp(alg->valuestring, "EdDSA") == 0;
    cJSON_Delete(hdr);
    if (!alg_ok) return -1;

    /* Decode payload */
    unsigned char *pay_bytes = NULL;
    size_t pay_len = 0;
    if (ll_base64url_decode(p_start, p_len, &pay_bytes, &pay_len) < 0) return -1;
    cJSON *claims = cJSON_ParseWithLength((const char *)pay_bytes, pay_len);
    free(pay_bytes);
    if (!claims) return -1;

    /* Check iss = "licenselatte" */
    cJSON *iss = cJSON_GetObjectItemCaseSensitive(claims, "iss");
    if (!iss || !cJSON_IsString(iss) || strcmp(iss->valuestring, "licenselatte") != 0) {
        cJSON_Delete(claims);
        return -1;
    }

    *claims_out = claims;
    return 0;
}

const char *ll_jwt_string_claim(const cJSON *claims, const char *key)
{
    if (!claims) return NULL;
    cJSON *item = cJSON_GetObjectItemCaseSensitive(claims, key);
    if (!item || !cJSON_IsString(item)) return NULL;
    return item->valuestring;
}

int64_t ll_jwt_int64_claim(const cJSON *claims, const char *key, int *ok)
{
    if (!claims) { if (ok) *ok = 0; return 0; }
    cJSON *item = cJSON_GetObjectItemCaseSensitive(claims, key);
    if (!item || !cJSON_IsNumber(item)) { if (ok) *ok = 0; return 0; }
    if (ok) *ok = 1;
    return (int64_t)item->valuedouble;
}
