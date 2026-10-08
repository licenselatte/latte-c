#include "cert.h"
#include "jwt.h"
#include <string.h>
#include <stdlib.h>

/* hex nibble value, returns -1 on bad char */
static int hex_val(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int verify_cert(const unsigned char *parent_pub,
                       const char *cert_jwt,
                       int64_t now,
                       int check_exp,
                       cJSON **claims_out)
{
    cJSON *claims = NULL;
    if (ll_jwt_verify_ed25519(cert_jwt, parent_pub, 32, &claims) < 0)
        return -1;

    /*
     * Zero leeway, and each claim enforced only if present -- the same
     * contract golang-jwt/v5 applies in latte-go's crypto.VerifyCert, which
     * passes WithIssuedAt() and a time func but no WithLeeway.
     *
     *   exp: expired when now > exp   (strictly after, not at); skipped
     *        when check_exp is 0
     *   nbf: not yet valid when now < nbf
     *   iat: not yet valid when now < iat -- a cert cannot have been issued
     *        in the future, which is the check WithIssuedAt() turns on.
     */
    int ok = 0;
    int64_t exp = ll_jwt_int64_claim(claims, "exp", &ok);
    if (check_exp && ok && now > exp) goto reject;

    ok = 0;
    int64_t nbf = ll_jwt_int64_claim(claims, "nbf", &ok);
    if (ok && now < nbf) goto reject;

    ok = 0;
    int64_t iat = ll_jwt_int64_claim(claims, "iat", &ok);
    if (ok && now < iat) goto reject;

    *claims_out = claims;
    return 0;

reject:
    cJSON_Delete(claims);
    *claims_out = NULL;
    return -1;
}

int ll_verify_cert(const unsigned char *parent_pub,
                   const char *cert_jwt,
                   int64_t now,
                   cJSON **claims_out)
{
    return verify_cert(parent_pub, cert_jwt, now, 1, claims_out);
}

int ll_verify_cert_ignoring_expiry(const unsigned char *parent_pub,
                                   const char *cert_jwt,
                                   int64_t now,
                                   cJSON **claims_out)
{
    return verify_cert(parent_pub, cert_jwt, now, 0, claims_out);
}

int ll_pubkey_from_cert(const cJSON *claims, const char *field,
                        unsigned char *pub_out)
{
    const char *hex = ll_jwt_string_claim(claims, field);
    if (!hex) return -1;
    size_t hlen = strlen(hex);
    if (hlen != 64) return -1;   /* Ed25519 = 32 bytes = 64 hex chars */
    for (int i = 0; i < 32; i++) {
        int hi = hex_val(hex[2*i]);
        int lo = hex_val(hex[2*i+1]);
        if (hi < 0 || lo < 0) return -1;
        pub_out[i] = (unsigned char)((hi << 4) | lo);
    }
    return 0;
}
