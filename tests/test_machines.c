/*
 * latte_deactivate, latte_machines, latte_rename_machine and the background
 * renewal against a one-shot HTTP stub on 127.0.0.1.
 *
 * latte.c is included so the test can build an SDK handle pointed at the
 * stub and at a cache file of its own. The cached token is not a valid
 * licence: these calls read its claims without verifying them.
 */
#include "../src/latte.c"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/wait.h>

#define STORE "test_machines.latte"
/* {"aid":"act-1","sub":"KEY1"} */
#define TOKEN "eyJhbGciOiJFZERTQSJ9.eyJhaWQiOiJhY3QtMSIsInN1YiI6IktFWTEifQ.c2ln"

static int pass = 0, fail = 0;

#define CHECK(expr, name) \
    do { \
        if (expr) { printf("  PASS  %s\n", name); pass++; } \
        else      { printf("  FAIL  %s\n", name); fail++; } \
    } while(0)

typedef struct {
    pid_t pid;
    int   request_fd;
    char  url[64];
    char  request[8192];
} stub;

/* Forks a child that answers one request with status and body, and passes
 * the raw request it received back through a pipe for stub_wait. */
static void stub_start(stub *st, int status, const char *body)
{
    int ls = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    socklen_t len = sizeof(addr);
    if (bind(ls, (struct sockaddr *)&addr, sizeof(addr)) < 0 || listen(ls, 1) < 0 ||
        getsockname(ls, (struct sockaddr *)&addr, &len) < 0) {
        perror("stub");
        exit(2);
    }
    snprintf(st->url, sizeof(st->url), "http://127.0.0.1:%d", ntohs(addr.sin_port));

    int fds[2];
    if (pipe(fds) < 0) { perror("pipe"); exit(2); }
    st->pid = fork();
    if (st->pid == 0) {
        close(fds[0]);
        int c = accept(ls, NULL, NULL);
        char req[8192];
        size_t n = 0;
        for (;;) {
            ssize_t r = read(c, req + n, sizeof(req) - 1 - n);
            if (r <= 0) break;
            n += (size_t)r;
            req[n] = '\0';
            char *end = strstr(req, "\r\n\r\n");
            char *cl  = strstr(req, "Content-Length: ");
            if (end && cl && n >= (size_t)(end + 4 - req) + strtoul(cl + 16, NULL, 10)) break;
        }
        if (write(fds[1], req, n) < 0) _exit(1);
        dprintf(c, "HTTP/1.1 %d Stub\r\nContent-Type: application/json\r\n"
                   "Content-Length: %zu\r\nConnection: close\r\n\r\n%s",
                status, strlen(body), body);
        close(c);
        _exit(0);
    }
    close(ls);
    close(fds[1]);
    st->request_fd = fds[0];
}

/* Collects the request the stub received and reaps it. Returns its body. */
static cJSON *stub_wait(stub *st)
{
    size_t n = 0;
    ssize_t r;
    while ((r = read(st->request_fd, st->request + n, sizeof(st->request) - 1 - n)) > 0)
        n += (size_t)r;
    st->request[n] = '\0';
    close(st->request_fd);
    waitpid(st->pid, NULL, 0);
    char *body = strstr(st->request, "\r\n\r\n");
    return body ? cJSON_Parse(body + 4) : NULL;
}

static int str_is(const cJSON *obj, const char *key, const char *want)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    return v && cJSON_IsString(v) && strcmp(v->valuestring, want) == 0;
}

/* An SDK handle pointed at url whose cache holds TOKEN. */
static latte_sdk *stub_sdk(const char *url)
{
    latte_sdk *sdk = (latte_sdk *)calloc(1, sizeof(latte_sdk));
    snprintf(sdk->store_path, sizeof(sdk->store_path), "%s", STORE);
    snprintf(sdk->machine_id, sizeof(sdk->machine_id), "machine-hash");
    sdk->http = ll_http_client_new(url, "pk_test_X", NULL);
    ll_mutex_init(&sdk->renew_mu);
    ll_cert_chain chain = {"s", "p", "d"};
    ll_file_save_token(STORE, TOKEN, &chain);
    return sdk;
}

static int token_kept(void)
{
    char *raw = NULL;
    ll_cert_chain *chain = NULL;
    int kept = ll_file_load_token(STORE, &raw, &chain) == 0 && strcmp(raw, TOKEN) == 0;
    free(raw);
    ll_cert_chain_free(chain);
    return kept;
}

typedef struct { unsigned char pk[32], sk[64]; } keypair;

static char *b64url(const unsigned char *in, size_t len)
{
    size_t cap = sodium_base64_ENCODED_LEN(len, sodium_base64_VARIANT_URLSAFE_NO_PADDING);
    char *out = (char *)malloc(cap);
    sodium_bin2base64(out, cap, in, len, sodium_base64_VARIANT_URLSAFE_NO_PADDING);
    return out;
}

static char *make_jwt(const unsigned char *sk, const char *payload)
{
    const char *header = "{\"alg\":\"EdDSA\",\"typ\":\"JWT\"}";
    char *h = b64url((const unsigned char *)header, strlen(header));
    char *p = b64url((const unsigned char *)payload, strlen(payload));
    char msg[2048];
    snprintf(msg, sizeof(msg), "%s.%s", h, p);
    unsigned char sig[crypto_sign_ed25519_BYTES];
    crypto_sign_ed25519_detached(sig, NULL, (const unsigned char *)msg, strlen(msg), sk);
    char *s = b64url(sig, sizeof(sig));
    char *jwt = (char *)malloc(strlen(msg) + strlen(s) + 2);
    sprintf(jwt, "%s.%s", msg, s);
    free(h); free(p); free(s);
    return jwt;
}

/* Signs a cert JWT for the key pub with parent_sk. */
static char *make_cert(const unsigned char *parent_sk, const char *sub, const char *field,
                       const unsigned char *pub, int64_t iat, int64_t exp)
{
    char hex[65], payload[512];
    sodium_bin2hex(hex, sizeof(hex), pub, 32);
    snprintf(payload, sizeof(payload),
             "{\"iss\":\"licenselatte\",\"sub\":\"%s\",\"pid\":\"proj\",\"%s\":\"%s\","
             "\"iat\":%lld,\"exp\":%lld}",
             sub, field, hex, (long long)iat, (long long)exp);
    return make_jwt(parent_sk, payload);
}

/* A token for act-1/KEY1 issued at iat with the given grace window, carried by
 * a chain rooted at master whose daily cert expires at daily_exp. */
static void signed_token(const keypair *master, int64_t iat, int64_t grace, int64_t daily_exp,
                         const char *machine, char **token, ll_cert_chain *chain)
{
    int64_t now = (int64_t)time(NULL);
    keypair sub, proj, daily;
    crypto_sign_ed25519_keypair(sub.pk, sub.sk);
    crypto_sign_ed25519_keypair(proj.pk, proj.sk);
    crypto_sign_ed25519_keypair(daily.pk, daily.sk);
    chain->submaster = make_cert(master->sk, "submaster", "spk", sub.pk, now - 86400 * 30, now + 86400 * 100);
    chain->project   = make_cert(sub.sk, "project", "ppk", proj.pk, now - 86400 * 30, now + 86400 * 100);
    chain->daily     = make_cert(proj.sk, "daily", "dpk", daily.pk, iat - 60, daily_exp);
    char payload[512];
    snprintf(payload, sizeof(payload),
             "{\"iss\":\"licenselatte\",\"sub\":\"KEY1\",\"aid\":\"act-1\",\"pid\":\"proj\","
             "\"mid\":\"%s\",\"ltype\":\"perpetual\",\"iat\":%lld,\"exp\":%lld,\"grc\":%lld}",
             machine, (long long)iat, (long long)(iat + 365 * 86400), (long long)grace);
    *token = make_jwt(daily.sk, payload);
}

static void chain_free(ll_cert_chain *chain)
{
    free((char *)chain->submaster); free((char *)chain->project); free((char *)chain->daily);
}

/* A stub_sdk whose cache holds a token signed under master. */
static latte_sdk *signed_sdk(const char *url, const keypair *master, int64_t age, int64_t grace,
                             int64_t daily_exp_after_iat, const char *machine)
{
    latte_sdk *sdk = stub_sdk(url);
    memcpy(sdk->master_pubs, master->pk, 32);
    int64_t iat = (int64_t)time(NULL) - age;
    char *token;
    ll_cert_chain chain;
    signed_token(master, iat, grace, iat + daily_exp_after_iat, machine, &token, &chain);
    ll_file_save_token(STORE, token, &chain);
    free(token); chain_free(&chain);
    return sdk;
}

/* The /v1/renew response carrying a fresh token issued now. */
static char *renew_response(const keypair *master)
{
    char *token;
    ll_cert_chain chain;
    int64_t now = (int64_t)time(NULL);
    signed_token(master, now, 7 * 86400, now + 86400, "machine-hash", &token, &chain);
    cJSON *root = cJSON_CreateObject(), *c = cJSON_AddObjectToObject(root, "chain");
    cJSON_AddStringToObject(root, "token", token);
    cJSON_AddStringToObject(c, "submaster", chain.submaster);
    cJSON_AddStringToObject(c, "project", chain.project);
    cJSON_AddStringToObject(c, "daily", chain.daily);
    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root); free(token); chain_free(&chain);
    return out;
}

static int cached(void)
{
    char *raw = NULL;
    ll_cert_chain *chain = NULL;
    int has = ll_file_load_token(STORE, &raw, &chain) == 0 && raw[0];
    free(raw);
    ll_cert_chain_free(chain);
    return has;
}

/* Whether the cached token still verifies and was issued less than a minute ago. */
static int token_fresh(const keypair *master)
{
    char *raw = NULL;
    ll_cert_chain *chain = NULL;
    ll_license *lic = NULL;
    ll_port_error ve;
    int fresh = ll_file_load_token(STORE, &raw, &chain) == 0 &&
                ll_verify_activation_any(master->pk, 1, raw, chain, &lic, &ve) == 0 &&
                (int64_t)time(NULL) - lic->issued_at < 60;
    ll_license_free(lic); free(raw); ll_cert_chain_free(chain);
    return fresh;
}

#define DAY 86400LL

/* Checks twice (the second inside the 5-minute floor), waits for any renewal,
 * and reports the first verdict in *first. Returns whether the stub received
 * a renewal request. */
static int check_and_renew(stub *st, latte_sdk *sdk, latte_status *first)
{
    latte_license *lic = NULL;
    *first = latte_check(sdk, &lic);
    latte_license_free(lic);
    /* A second check inside the 5-minute floor must not renew again. */
    lic = NULL;
    latte_check(sdk, &lic);
    latte_license_free(lic);
    ll_thread_join(&sdk->renew_thread);
    kill(st->pid, SIGKILL);
    cJSON_Delete(stub_wait(st));
    return strncmp(st->request, "POST /v1/renew ", 15) == 0;
}

static void test_renew_after_grace(void)
{
    keypair master, other;
    crypto_sign_ed25519_keypair(master.pk, master.sk);
    crypto_sign_ed25519_keypair(other.pk, other.sk);
    latte_license *lic = NULL;
    latte_status first;
    stub st;

    /* Past its 7-day grace window: check reports not activated, renews once,
     * and the next check returns the licence. */
    char *resp = renew_response(&master);
    stub_start(&st, 200, resp);
    latte_sdk *sdk = signed_sdk(st.url, &master, 10 * DAY, 7 * DAY, DAY, "machine-hash");
    CHECK(check_and_renew(&st, sdk, &first) == 1 && first == LATTE_ERR_NOT_ACTIVATED,
          "a token past its grace window renews in the background; check is still not activated");
    CHECK(token_fresh(&master) && sdk->renew_failed == 0, "the renewal saved the new token");
    CHECK(latte_check(sdk, &lic) == LATTE_OK && lic && strcmp(lic->activation_id, "act-1") == 0 &&
          lic->in_grace_period == 0,
          "a later check returns the licence");
    latte_license_free(lic);
    latte_free(sdk);

    /* A daily cert that lapsed long before the token's age matters. */
    stub_start(&st, 200, resp);
    sdk = signed_sdk(st.url, &master, 2 * 3600, 7 * DAY, 300, "machine-hash");
    CHECK(check_and_renew(&st, sdk, &first) == 1 && token_fresh(&master),
          "a token whose daily cert has expired renews");
    latte_free(sdk);
    free(resp);

    /* Tampered: signed under a master the SDK does not trust. */
    stub_start(&st, 200, "{}");
    sdk = signed_sdk(st.url, &other, 10 * DAY, 7 * DAY, DAY, "machine-hash");
    memcpy(sdk->master_pubs, master.pk, 32);
    CHECK(check_and_renew(&st, sdk, &first) == 0 && first == LATTE_ERR_NOT_ACTIVATED,
          "a token that does not verify starts no renewal");
    latte_free(sdk);

    /* Signed, but for another machine. */
    stub_start(&st, 200, "{}");
    sdk = signed_sdk(st.url, &master, 10 * DAY, 7 * DAY, DAY, "other-machine");
    CHECK(check_and_renew(&st, sdk, &first) == 0 && first == LATTE_ERR_NOT_ACTIVATED,
          "a token for another machine starts no renewal");
    latte_free(sdk);

    /* The server rejects the renewal: the cache is wiped. */
    stub_start(&st, 403, "{\"error\":\"license is inactive or expired\"}");
    sdk = signed_sdk(st.url, &master, 10 * DAY, 7 * DAY, DAY, "machine-hash");
    CHECK(check_and_renew(&st, sdk, &first) == 1 && !cached() &&
          latte_check(sdk, &lic) == LATTE_ERR_NOT_ACTIVATED,
          "a rejected renewal wipes the cache");
    latte_free(sdk);

    /* The server cannot be reached: the cache stays and the flag is set. */
    sdk = signed_sdk("http://127.0.0.1:1", &master, 10 * DAY, 7 * DAY, DAY, "machine-hash");
    latte_check(sdk, &lic);
    ll_thread_join(&sdk->renew_thread);
    CHECK(sdk->renew_failed == 1 && cached() && !token_fresh(&master) && latte_check(sdk, &lic) == LATTE_ERR_NOT_ACTIVATED,
          "a network failure keeps the cache and sets renew_failed");
    latte_free(sdk);
}

static void test_deactivate(void)
{
    stub st;
    latte_deactivate_result res;
    latte_license *lic = NULL;

    stub_start(&st, 200, "{\"released\":true,\"seat_free_at\":\"2026-10-21T09:00:00Z\"}");
    latte_sdk *sdk = stub_sdk(st.url);
    latte_status s = latte_deactivate(sdk, &res);
    cJSON *body = stub_wait(&st);
    CHECK(strncmp(st.request, "POST /v1/deactivate ", 20) == 0, "deactivate posts /v1/deactivate");
    CHECK(body && str_is(body, "activation_id", "act-1") && str_is(body, "license_key", "KEY1") &&
          str_is(body, "machine_id", "machine-hash") &&
          cJSON_IsObject(cJSON_GetObjectItemCaseSensitive(body, "sdk")),
          "deactivate sends renew's credentials and the sdk object");
    cJSON_Delete(body);
    CHECK(s == LATTE_OK && res.released == 1 && res.seat_free_at == 1792573200,
          "deactivate reports released and seat_free_at");
    CHECK(!token_kept(), "deactivate clears the cache");
    CHECK(latte_check(sdk, &lic) == LATTE_ERR_NOT_ACTIVATED, "check after deactivate is not activated");
    CHECK(latte_deactivate(sdk, NULL) == LATTE_ERR_NOT_ACTIVATED,
          "deactivate with nothing cached is not activated");
    latte_free(sdk);

    stub_start(&st, 200, "{\"released\":false,\"seat_free_at\":null}");
    sdk = stub_sdk(st.url);
    s = latte_deactivate(sdk, &res);
    cJSON_Delete(stub_wait(&st));
    CHECK(s == LATTE_OK && res.released == 0 && res.seat_free_at == 0,
          "an earlier release is success too");
    CHECK(!token_kept(), "released:false clears the cache");
    latte_free(sdk);

    stub_start(&st, 409, "{\"error\":\"Wait a week.\",\"reason\":\"deactivation_cooldown\","
                         "\"available_at\":\"2026-10-21T09:00:00Z\"}");
    sdk = stub_sdk(st.url);
    s = latte_deactivate(sdk, &res);
    cJSON_Delete(stub_wait(&st));
    CHECK(s == LATTE_ERR_DEACTIVATION_BLOCKED, "409 is LATTE_ERR_DEACTIVATION_BLOCKED");
    CHECK(strcmp(res.reason, "deactivation_cooldown") == 0 && strcmp(res.message, "Wait a week.") == 0 &&
          res.available_at == 1792573200,
          "409 carries reason, message and available_at");
    CHECK(token_kept(), "a policy refusal keeps the cache");
    latte_free(sdk);

    stub_start(&st, 403, "{\"error\":\"license is inactive or expired\",\"reason\":\"license_suspended\"}");
    sdk = stub_sdk(st.url);
    s = latte_deactivate(sdk, &res);
    cJSON_Delete(stub_wait(&st));
    CHECK(s == LATTE_ERR_LICENSE_EXPIRED && token_kept(), "403 maps as before and keeps the cache");
    latte_free(sdk);

    /* A port nothing listens on: bound, then closed. */
    stub_start(&st, 200, "{}");
    kill(st.pid, SIGKILL);
    stub_wait(&st);
    sdk = stub_sdk(st.url);
    s = latte_deactivate(sdk, &res);
    CHECK(s == LATTE_ERR_NETWORK && token_kept(), "a network failure keeps the cache");
    latte_free(sdk);
}

static void test_machines_and_rename(void)
{
    stub st;
    stub_start(&st, 200, "{\"machines\":["
        "{\"machine_name\":\"Studio\",\"machine_fingerprint\":\"9f1c4a0b2d3e\","
        "\"activated_at\":\"2026-10-01T00:00:00Z\","
        "\"last_seen_at\":\"2026-10-14T02:00:00.123456+02:00\",\"current\":true},"
        "{\"machine_fingerprint\":\"0a1b2c3d4e5f\",\"activated_at\":\"2026-09-01T00:00:00Z\","
        "\"last_seen_at\":\"2026-09-02T00:00:00Z\",\"seat_free_at\":\"2026-10-20T00:00:00Z\","
        "\"current\":false}]}");
    latte_sdk *sdk = stub_sdk(st.url);
    latte_machine *ms = NULL;
    size_t n = 0;
    latte_status s = latte_machines(sdk, &ms, &n);
    cJSON *body = stub_wait(&st);
    CHECK(strncmp(st.request, "POST /v1/machines ", 18) == 0 &&
          body && str_is(body, "activation_id", "act-1"),
          "machines posts /v1/machines with the credentials");
    cJSON_Delete(body);
    CHECK(s == LATTE_OK && n == 2, "machines lists both machines");
    if (n == 2) {
        CHECK(ms[0].name && strcmp(ms[0].name, "Studio") == 0 &&
              strcmp(ms[0].fingerprint, "9f1c4a0b2d3e") == 0 &&
              ms[0].activated_at == 1790812800 && ms[0].last_seen_at == 1791936000 &&
              ms[0].seat_free_at == 0 && ms[0].current == 1,
              "first machine: name, fingerprint, times, current");
        CHECK(ms[1].name == NULL && ms[1].seat_free_at == 1792454400 && ms[1].current == 0,
              "second machine: no name, seat held");
    }
    latte_machines_free(ms, n);
    latte_free(sdk);

    stub_start(&st, 200, "{\"machine_name\":\"Studio Mac\"}");
    sdk = stub_sdk(st.url);
    char stored[LATTE_MACHINE_NAME_SIZE];
    s = latte_rename_machine(sdk, " Studio Mac ", stored, sizeof(stored));
    body = stub_wait(&st);
    CHECK(strncmp(st.request, "POST /v1/machines/rename ", 25) == 0 &&
          body && str_is(body, "machine_name", " Studio Mac ") && str_is(body, "activation_id", "act-1"),
          "rename posts machine_name with the credentials");
    cJSON_Delete(body);
    CHECK(s == LATTE_OK && strcmp(stored, "Studio Mac") == 0, "rename returns the stored name");
    latte_free(sdk);

    stub_start(&st, 400, "{\"error\":\"invalid machine name\"}");
    sdk = stub_sdk(st.url);
    s = latte_rename_machine(sdk, "", stored, sizeof(stored));
    cJSON_Delete(stub_wait(&st));
    CHECK(s != LATTE_OK && stored[0] == '\0', "a refused rename fails and stores nothing");
    latte_free(sdk);
}

/* Runs one renewal against a stub that answers status, and reports whether
 * the cached token survived. */
static int renew_keeps_token(int status)
{
    stub st;
    stub_start(&st, status, "{\"error\":\"stub\"}");
    latte_sdk *sdk = stub_sdk(st.url);
    renew_args *ra = (renew_args *)calloc(1, sizeof(renew_args));
    ra->sdk = sdk;
    snprintf(ra->activation_id, sizeof(ra->activation_id), "act-1");
    snprintf(ra->key, sizeof(ra->key), "KEY1");
    renew_thread_fn(ra);
    cJSON_Delete(stub_wait(&st));
    int kept = token_kept();
    latte_free(sdk);
    return kept;
}

static ll_port_error renew_error(int status)
{
    stub st;
    stub_start(&st, status, "{\"error\":\"stub\"}");
    latte_sdk *sdk = stub_sdk(st.url);
    char *token = NULL;
    ll_cert_chain *chain = NULL;
    ll_port_error pe = ll_http_renew(sdk->http, "act-1", "KEY1", "machine-hash", &token, &chain);
    cJSON_Delete(stub_wait(&st));
    latte_free(sdk);
    return pe;
}

static void test_renew_errors(void)
{
    CHECK(renew_keeps_token(500), "a 500 on renew keeps the cached token");
    CHECK(renew_keeps_token(503), "a 503 on renew keeps the cached token");
    CHECK(renew_keeps_token(429), "a 429 on renew keeps the cached token");
    CHECK(!renew_keeps_token(403), "a 403 on renew wipes the cached token");
    CHECK(!renew_keeps_token(404), "a 404 on renew wipes the cached token");
    CHECK(renew_error(500) == LL_PORT_ERR_SERVER, "a 500 on renew is LL_PORT_ERR_SERVER");
    CHECK(renew_error(429) == LL_PORT_ERR_SERVER, "a 429 on renew is LL_PORT_ERR_SERVER");
    CHECK(map_network_error(LL_PORT_ERR_SERVER) == LATTE_ERR_INTERNAL,
          "LL_PORT_ERR_SERVER maps to the public LATTE_ERR_INTERNAL");

    /* Nothing listens on port 1. */
    latte_sdk *sdk = stub_sdk("http://127.0.0.1:1");
    char *token = NULL;
    ll_cert_chain *chain = NULL;
    CHECK(ll_http_renew(sdk->http, "act-1", "KEY1", "machine-hash", &token, &chain) ==
          LL_PORT_ERR_NETWORK, "a transport failure on renew is LL_PORT_ERR_NETWORK");
    CHECK(token_kept(), "a transport failure keeps the cached token");
    latte_free(sdk);
}

/* Runs one renewal for activation act against url with renew_failed preset
 * to before, and returns renew_failed afterwards. */
static int renew_failed_after(const char *url, const char *act, int before, int *kept)
{
    latte_sdk *sdk = stub_sdk(url);
    sdk->renew_failed = before;
    renew_args *ra = (renew_args *)calloc(1, sizeof(renew_args));
    ra->sdk = sdk;
    snprintf(ra->activation_id, sizeof(ra->activation_id), "%s", act);
    snprintf(ra->key, sizeof(ra->key), "KEY1");
    renew_thread_fn(ra);
    int after = sdk->renew_failed;
    if (kept) *kept = token_kept();
    latte_free(sdk);
    return after;
}

static int renew_failed_after_status(int status, int before, int *kept)
{
    stub st;
    stub_start(&st, status, "{\"error\":\"stub\"}");
    int after = renew_failed_after(st.url, "act-1", before, kept);
    cJSON_Delete(stub_wait(&st));
    return after;
}

static void test_renew_failed(void)
{
    int kept;

    /* Nothing listens on port 1. */
    CHECK(renew_failed_after("http://127.0.0.1:1", "act-1", 0, &kept) == 1 && kept,
          "a transport failure on renew sets renew_failed and keeps the token");
    CHECK(renew_failed_after("http://127.0.0.1:1", "other-act", 0, NULL) == 0,
          "a transport failure for an activation the cache no longer holds does not set it");
    CHECK(renew_failed_after_status(500, 1, &kept) == 0 && kept,
          "a 500 on renew clears renew_failed");
    CHECK(renew_failed_after_status(429, 1, &kept) == 0 && kept,
          "a 429 on renew clears renew_failed");
    CHECK(renew_failed_after_status(403, 1, &kept) == 0 && !kept,
          "a 403 on renew clears renew_failed and wipes the token");
    stub bad;
    stub_start(&bad, 200, "{\"token\":\"a.b.c\",\"chain\":{\"submaster\":\"s\",\"project\":\"p\",\"daily\":\"d\"}}");
    CHECK(renew_failed_after(bad.url, "act-1", 1, &kept) == 0 && kept,
          "a renewed token that does not verify clears renew_failed and keeps the old token");
    cJSON_Delete(stub_wait(&bad));

    stub st;
    stub_start(&st, 200, "{\"released\":true,\"seat_free_at\":\"2026-10-21T09:00:00Z\"}");
    latte_sdk *sdk = stub_sdk(st.url);
    sdk->renew_failed = 1;
    CHECK(latte_deactivate(sdk, NULL) == LATTE_OK && sdk->renew_failed == 0,
          "deactivate resets renew_failed");
    cJSON_Delete(stub_wait(&st));
    latte_free(sdk);

    /* A token 2h old with a 7-day grace window. */
    ll_license lic = {0};
    lic.issued_at    = (int64_t)time(NULL) - 2 * 3600;
    lic.grace_period = 7 * 24 * 3600;
    sdk = stub_sdk("http://127.0.0.1:1");
    latte_license *pub = domain_to_public(sdk, &lic);
    CHECK(pub && pub->in_grace_period == 0, "a stale token reads in_grace_period false by default");
    latte_license_free(pub);
    sdk->renew_failed = 1;
    pub = domain_to_public(sdk, &lic);
    CHECK(pub && pub->in_grace_period == 1, "domain_to_public reads renew_failed");
    latte_license_free(pub);
    latte_free(sdk);
}

int main(void)
{
    printf("=== test_machines ===\n");
    signal(SIGPIPE, SIG_IGN);
    test_deactivate();
    test_machines_and_rename();
    test_renew_errors();
    test_renew_failed();
    test_renew_after_grace();
    remove(STORE);
    printf("\n%d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}
