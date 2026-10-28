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
    remove(STORE);
    printf("\n%d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}
