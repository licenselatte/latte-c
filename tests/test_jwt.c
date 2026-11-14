/*
 * Unit tests for base64url decode and Ed25519 JWT verify.
 * Uses a self-signed test keypair.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "sign.h"

#include "../src/crypto/base64url.h"
#include "../src/crypto/jwt.h"

static int pass = 0, fail = 0;

#define CHECK(expr, name) \
    do { \
        if (expr) { printf("  PASS  %s\n", name); pass++; } \
        else      { printf("  FAIL  %s\n", name); fail++; } \
    } while(0)

/* ---- build a minimal EdDSA JWT ----------------------------------------- */
static char *make_jwt(const unsigned char *sk, const char *payload_json,
                      int bad_sig)
{
    const char *header_json = "{\"alg\":\"EdDSA\",\"typ\":\"JWT\"}";
    char *h = b64url((const unsigned char *)header_json, strlen(header_json));
    char *p = b64url((const unsigned char *)payload_json, strlen(payload_json));
    if (!h || !p) { free(h); free(p); return NULL; }

    size_t msg_len = strlen(h) + 1 + strlen(p);
    char *msg = (char *)malloc(msg_len + 1);
    snprintf(msg, msg_len + 1, "%s.%s", h, p);

    unsigned char sig[64];
    crypto_ed25519_sign(sig, sk, (const unsigned char *)msg, msg_len);
    if (bad_sig) sig[0] ^= 0xFF;  /* corrupt */

    char *s = b64url(sig, sizeof(sig));
    size_t jwt_len = msg_len + 1 + strlen(s) + 1;
    char *jwt = (char *)malloc(jwt_len);
    snprintf(jwt, jwt_len, "%s.%s", msg, s);

    free(h); free(p); free(msg); free(s);
    return jwt;
}

int main(void)
{
    printf("=== test_jwt ===\n");

    /* --- base64url round-trip --- */
    {
        const unsigned char data[] = {0x00, 0xFF, 0xAB, 0xCD, 0xEF, 0x01};
        char *enc = b64url(data, sizeof(data));
        unsigned char *dec = NULL; size_t dec_len = 0;
        int r = ll_base64url_decode(enc, strlen(enc), &dec, &dec_len);
        CHECK(r == 0,                                     "b64url decode returns 0");
        CHECK(dec_len == sizeof(data),                    "b64url decoded length");
        CHECK(memcmp(dec, data, sizeof(data)) == 0,       "b64url round-trip bytes");
        free(enc); free(dec);
    }

    /* empty string */
    {
        unsigned char *dec = NULL; size_t dec_len = 0;
        int r = ll_base64url_decode("", 0, &dec, &dec_len);
        CHECK(r == 0 && dec_len == 0, "b64url empty string");
        free(dec);
    }

    /* --- JWT verify --- */
    keypair k1 = gen();
    unsigned char pk[32], sk[64];
    memcpy(pk, k1.pk, 32);
    memcpy(sk, k1.sk, 64);

    const char *payload =
        "{\"iss\":\"licenselatte\",\"sub\":\"TESTKEY123\","
        "\"iat\":1700000000,\"exp\":9999999999}";

    char *jwt = make_jwt(sk, payload, 0);
    CHECK(jwt != NULL, "JWT built");

    cJSON *claims = NULL;
    int r = ll_jwt_verify_ed25519(jwt, pk, sizeof(pk), &claims);
    CHECK(r == 0,    "valid JWT verifies");
    CHECK(claims != NULL, "claims not null on success");
    if (claims) {
        CHECK(strcmp(ll_jwt_string_claim(claims, "sub"), "TESTKEY123") == 0,
              "sub claim correct");
        int ok = 0;
        int64_t iat = ll_jwt_int64_claim(claims, "iat", &ok);
        CHECK(ok == 1 && iat == 1700000000LL, "iat claim correct");
        cJSON_Delete(claims);
    }
    free(jwt);

    /* Bad signature */
    char *bad_jwt = make_jwt(sk, payload, 1);
    cJSON *bad_claims = NULL;
    r = ll_jwt_verify_ed25519(bad_jwt, pk, sizeof(pk), &bad_claims);
    CHECK(r != 0,          "bad sig rejected");
    CHECK(bad_claims == NULL, "no claims on failure");
    free(bad_jwt);

    /* Wrong public key */
    keypair k2 = gen();
    unsigned char pk2[32];
    memcpy(pk2, k2.pk, 32);
    char *jwt2 = make_jwt(sk, payload, 0);
    cJSON *c2 = NULL;
    r = ll_jwt_verify_ed25519(jwt2, pk2, sizeof(pk2), &c2);
    CHECK(r != 0, "wrong pubkey rejected");
    free(jwt2);

    /* Wrong alg: replace "EdDSA" with "HS256" */
    {
        const char *hdr_hs = "{\"alg\":\"HS256\",\"typ\":\"JWT\"}";
        char *h_enc = b64url((const unsigned char *)hdr_hs, strlen(hdr_hs));
        char *p_enc = b64url((const unsigned char *)payload, strlen(payload));
        size_t mlen = strlen(h_enc) + 1 + strlen(p_enc);
        char *msg = (char *)malloc(mlen + 1);
        snprintf(msg, mlen+1, "%s.%s", h_enc, p_enc);
        unsigned char sig[64];
        crypto_ed25519_sign(sig, sk, (const unsigned char *)msg, mlen);
        char *s_enc = b64url(sig, sizeof(sig));
        size_t jlen = mlen + 1 + strlen(s_enc) + 1;
        char *bad_alg_jwt = (char *)malloc(jlen);
        snprintf(bad_alg_jwt, jlen, "%s.%s", msg, s_enc);
        cJSON *bc = NULL;
        r = ll_jwt_verify_ed25519(bad_alg_jwt, pk, sizeof(pk), &bc);
        CHECK(r != 0, "wrong alg rejected");
        free(h_enc); free(p_enc); free(msg); free(s_enc); free(bad_alg_jwt);
    }

    /* Wrong issuer */
    {
        const char *bad_iss_payload =
            "{\"iss\":\"evil\",\"sub\":\"X\",\"iat\":1700000000,\"exp\":9999999999}";
        char *bj = make_jwt(sk, bad_iss_payload, 0);
        cJSON *bc = NULL;
        r = ll_jwt_verify_ed25519(bj, pk, sizeof(pk), &bc);
        CHECK(r != 0, "wrong issuer rejected");
        free(bj);
    }

    /* Unverified decode reads the payload of a token that fails verification */
    {
        char *bj = make_jwt(sk, payload, 1);
        cJSON *uc = NULL;
        r = ll_jwt_decode_unverified(bj, &uc);
        CHECK(r == 0 && strcmp(ll_jwt_string_claim(uc, "sub"), "TESTKEY123") == 0,
              "unverified decode reads a badly signed token's claims");
        cJSON_Delete(uc);
        free(bj);
        CHECK(ll_jwt_decode_unverified("garbage", &uc) != 0 && uc == NULL,
              "unverified decode rejects a non-JWT");
        CHECK(ll_jwt_verify_ed25519(NULL, pk, sizeof(pk), &uc) != 0 && uc == NULL,
              "verify rejects a missing token");
        CHECK(ll_jwt_decode_unverified(NULL, &uc) != 0 && uc == NULL,
              "unverified decode rejects a missing token");
    }

    printf("\n%d passed, %d failed\n", pass, fail);
    return fail > 0 ? 1 : 0;
}
