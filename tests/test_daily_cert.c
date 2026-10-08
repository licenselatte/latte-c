/*
 * The daily cert expires at 00:05 UTC the day after it is issued, while the
 * token it signed must verify offline for its whole grace period. Its exp is
 * therefore not held against now; its window bounds the token's iat instead.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sodium.h>

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

typedef struct { unsigned char pk[32], sk[64]; } keypair;

static keypair gen(void)
{
    keypair k;
    crypto_sign_ed25519_keypair(k.pk, k.sk);
    return k;
}

#define DAY_START 1791331200LL           /* 2026-10-07T00:00:00Z */
#define DAILY_EXP (DAY_START + 86400 + 300)
#define GRACE     (30LL * 86400)

/*
 * Builds the chain the server issues: 180-day submaster and project certs,
 * a daily cert for DAY_START with the given issuer and iat, and a token
 * issued at token_iat. Verifies it as of now.
 */
static int verify_with(const char *daily_iss, int64_t daily_iat,
                       int64_t token_iat, int64_t now)
{
    keypair master = gen(), sub = gen(), proj = gen(), daily = gen();
    char hex[65], payload[512];
    int64_t cert_start = DAY_START - 30 * 86400;
    int64_t cert_end = cert_start + 180 * 86400;

    sodium_bin2hex(hex, sizeof(hex), sub.pk, 32);
    snprintf(payload, sizeof(payload),
        "{\"iss\":\"licenselatte\",\"sub\":\"submaster\",\"spk\":\"%s\",\"iat\":%lld,\"exp\":%lld}",
        hex, (long long)cert_start, (long long)cert_end);
    ll_cert_chain chain = {0};
    chain.submaster = make_jwt(master.sk, payload);

    sodium_bin2hex(hex, sizeof(hex), proj.pk, 32);
    snprintf(payload, sizeof(payload),
        "{\"iss\":\"licenselatte\",\"sub\":\"project\",\"pid\":\"proj\",\"ppk\":\"%s\",\"iat\":%lld,\"exp\":%lld}",
        hex, (long long)cert_start, (long long)cert_end);
    chain.project = make_jwt(sub.sk, payload);

    sodium_bin2hex(hex, sizeof(hex), daily.pk, 32);
    snprintf(payload, sizeof(payload),
        "{\"iss\":\"%s\",\"sub\":\"daily\",\"pid\":\"proj\",\"dpk\":\"%s\",\"day\":\"2026-10-07\",\"iat\":%lld,\"exp\":%lld}",
        daily_iss, hex, (long long)daily_iat, (long long)DAILY_EXP);
    chain.daily = make_jwt(proj.sk, payload);

    snprintf(payload, sizeof(payload),
        "{\"iss\":\"licenselatte\",\"sub\":\"KEY\",\"aid\":\"act\",\"pid\":\"proj\",\"mid\":\"m\","
        "\"ltype\":\"perpetual\",\"iat\":%lld,\"exp\":%lld,\"grc\":%lld}",
        (long long)token_iat, (long long)(token_iat + 365 * 86400), (long long)GRACE);
    char *token = make_jwt(daily.sk, payload);

    ll_license *lic = NULL;
    ll_port_error err;
    int ok = ll_verify_activation_any_at(master.pk, 1, token, &chain, now, &lic, &err) == 0;

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

    printf("=== test_daily_cert ===\n");

    int64_t late = DAY_START + 23 * 3600 + 59 * 60;   /* activated 23:59 UTC */

    CHECK(verify_with("licenselatte", DAY_START, late, late + 60),
          "offline 1 minute after a 23:59 activation verifies");
    CHECK(verify_with("licenselatte", DAY_START, late, late + 600),
          "offline 10 minutes, past the daily cert's exp, verifies");
    CHECK(verify_with("licenselatte", DAY_START, late, late + 86400),
          "offline 1 day verifies");
    CHECK(verify_with("licenselatte", DAY_START, late, late + 29 * 86400),
          "offline 29 days, inside the grace period, verifies");

    CHECK(!verify_with("licenselatte", DAY_START, DAY_START - 60, DAY_START),
          "token issued before the daily cert's iat is rejected");
    CHECK(!verify_with("licenselatte", DAY_START, DAILY_EXP + 60, DAILY_EXP + 120),
          "token issued after the daily cert's exp is rejected");
    CHECK(!verify_with("someone-else", DAY_START, late, late + 60),
          "daily cert with the wrong issuer is rejected");
    CHECK(!verify_with("licenselatte", late + 3600, late + 3600, late),
          "daily cert issued in the future is rejected");

    printf("\n%d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}
