/*
 * Trusted master keys: a chain is accepted when its submaster cert is signed
 * by any key in the list, and rejected when signed by none of them.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <sodium.h>

#include "../src/constants.h"
#include "../src/verify.h"

static int pass = 0, fail = 0;

#define CHECK(expr, name) \
    do { \
        if (expr) { printf("  PASS  %s\n", name); pass++; } \
        else      { printf("  FAIL  %s\n", name); fail++; } \
    } while(0)

static char *b64url(const unsigned char *in, size_t len)
{
    size_t cap = sodium_base64_ENCODED_LEN(len, sodium_base64_VARIANT_URLSAFE_NO_PADDING);
    char *out = (char *)malloc(cap);
    if (out) sodium_bin2base64(out, cap, in, len, sodium_base64_VARIANT_URLSAFE_NO_PADDING);
    return out;
}

static char *make_jwt(const unsigned char *sk, const char *payload)
{
    const char *header = "{\"alg\":\"EdDSA\",\"typ\":\"JWT\"}";
    char *h = b64url((const unsigned char *)header, strlen(header));
    char *p = b64url((const unsigned char *)payload, strlen(payload));
    size_t msg_len = strlen(h) + 1 + strlen(p);
    char *msg = (char *)malloc(msg_len + 1);
    snprintf(msg, msg_len + 1, "%s.%s", h, p);

    unsigned char sig[crypto_sign_ed25519_BYTES];
    crypto_sign_ed25519_detached(sig, NULL, (const unsigned char *)msg, msg_len, sk);
    char *s = b64url(sig, sizeof(sig));

    size_t jwt_len = msg_len + 1 + strlen(s) + 1;
    char *jwt = (char *)malloc(jwt_len);
    snprintf(jwt, jwt_len, "%s.%s", msg, s);
    free(h); free(p); free(msg); free(s);
    return jwt;
}

static void hex32(const unsigned char *pk, char out[65])
{
    sodium_bin2hex(out, 65, pk, 32);
}

typedef struct { unsigned char pk[32], sk[64]; } keypair;

static keypair gen(void)
{
    keypair k;
    crypto_sign_ed25519_keypair(k.pk, k.sk);
    return k;
}

/* Builds a full chain whose submaster cert is signed by master. */
static void build_chain(const keypair *master, int64_t now,
                        ll_cert_chain *chain, char **token)
{
    keypair sub = gen(), proj = gen(), daily = gen();
    char hex[65], payload[512];

    hex32(sub.pk, hex);
    snprintf(payload, sizeof(payload),
        "{\"iss\":\"licenselatte\",\"sub\":\"submaster\",\"spk\":\"%s\",\"iat\":%lld,\"exp\":%lld}",
        hex, (long long)(now - 100), (long long)(now + 1000));
    chain->submaster = make_jwt(master->sk, payload);

    hex32(proj.pk, hex);
    snprintf(payload, sizeof(payload),
        "{\"iss\":\"licenselatte\",\"sub\":\"project\",\"pid\":\"proj\",\"ppk\":\"%s\",\"iat\":%lld,\"exp\":%lld}",
        hex, (long long)(now - 100), (long long)(now + 1000));
    chain->project = make_jwt(sub.sk, payload);

    hex32(daily.pk, hex);
    snprintf(payload, sizeof(payload),
        "{\"iss\":\"licenselatte\",\"sub\":\"daily\",\"pid\":\"proj\",\"dpk\":\"%s\",\"iat\":%lld,\"exp\":%lld}",
        hex, (long long)(now - 100), (long long)(now + 1000));
    chain->daily = make_jwt(proj.sk, payload);

    snprintf(payload, sizeof(payload),
        "{\"iss\":\"licenselatte\",\"sub\":\"KEY\",\"aid\":\"act\",\"pid\":\"proj\",\"mid\":\"m\","
        "\"ltype\":\"perpetual\",\"iat\":%lld,\"exp\":%lld,\"grc\":3600}",
        (long long)now, (long long)(now + 86400));
    *token = make_jwt(daily.sk, payload);
}

static int verifies(const unsigned char *trusted, size_t n, const keypair *signer)
{
    int64_t now = (int64_t)time(NULL);
    ll_cert_chain chain = {0};
    char *token = NULL;
    build_chain(signer, now, &chain, &token);

    ll_license *lic = NULL;
    ll_port_error err;
    int ok = ll_verify_activation_any_at(trusted, n, token, &chain, now, &lic, &err) == 0;

    ll_license_free(lic);
    free(chain.submaster); free(chain.project); free(chain.daily); free(token);
    return ok;
}

int main(void)
{
    if (sodium_init() < 0) {
        fprintf(stderr, "sodium_init failed\n");
        return 1;
    }

    printf("=== test_anchors ===\n");

    keypair a = gen(), b = gen(), c = gen(), outsider = gen();
    unsigned char trusted[3 * 32];
    memcpy(trusted,      a.pk, 32);
    memcpy(trusted + 32, b.pk, 32);
    memcpy(trusted + 64, c.pk, 32);

    CHECK(verifies(trusted, 3, &a), "chain signed by first trusted key verifies");
    CHECK(verifies(trusted, 3, &b), "chain signed by middle trusted key verifies");
    CHECK(verifies(trusted, 3, &c), "chain signed by last trusted key verifies");
    CHECK(!verifies(trusted, 3, &outsider), "chain signed by untrusted key is rejected");
    CHECK(!verifies(trusted, 0, &a), "empty trusted list rejects everything");

    /* The single-key entry point still works and still means one key. */
    {
        int64_t now = (int64_t)time(NULL);
        ll_cert_chain chain = {0};
        char *token = NULL;
        build_chain(&b, now, &chain, &token);
        ll_license *lic = NULL;
        ll_port_error err;
        CHECK(ll_verify_activation_at(b.pk, token, &chain, now, &lic, &err) == 0,
              "single-key verify accepts its own key");
        ll_license_free(lic); lic = NULL;
        CHECK(ll_verify_activation_at(a.pk, token, &chain, now, &lic, &err) < 0,
              "single-key verify rejects another key");
        ll_license_free(lic);
        free(chain.submaster); free(chain.project); free(chain.daily); free(token);
    }

    /* Every embedded key is a 32-byte hex Ed25519 public key on the curve. */
    {
        static const char *const hex[] = LATTE_MASTER_PUBKEYS_HEX;
        size_t n = sizeof(hex) / sizeof(hex[0]);
        CHECK(n == LATTE_MASTER_PUBKEY_COUNT, "LATTE_MASTER_PUBKEY_COUNT matches the list");
        for (size_t i = 0; i < n; i++) {
            unsigned char pk[32];
            size_t bin_len = 0;
            int ok = strlen(hex[i]) == 64 &&
                     sodium_hex2bin(pk, sizeof(pk), hex[i], 64, NULL, &bin_len, NULL) == 0 &&
                     bin_len == 32 &&
                     crypto_core_ed25519_is_valid_point(pk) == 1;
            char name[64];
            snprintf(name, sizeof(name), "embedded master key %zu is a valid Ed25519 key", i);
            CHECK(ok, name);
        }
    }

    printf("\n%d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}
