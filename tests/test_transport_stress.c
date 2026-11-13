/*
 * ll_http_post under load, built with a 200 ms deadline: THREADS threads
 * each send PER_THREAD requests, cancelled by the deadline while connecting,
 * while waiting for the headers and while reading the body, mixed with
 * requests that complete. Each one must end in time with the right result.
 * On Windows the process must not hold more handles afterwards: every
 * request's event, sockets and WinHTTP handles are released (MSVC's
 * AddressSanitizer reports invalid accesses but not leaks; the Linux ASan
 * job checks leaks of the libcurl side).
 */
#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  include <windows.h>
typedef SOCKET sock_t;
#  define sock_close closesocket
#else
#  include <arpa/inet.h>
#  include <netinet/in.h>
#  include <signal.h>
#  include <sys/socket.h>
#  include <time.h>
#  include <unistd.h>
typedef int sock_t;
#  define sock_close close
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/constants.h"
#include "../src/thread.h"
#include "../src/http/transport.h"

#define THREADS    4
#define PER_THREAD 60
#define SERVERS    (THREADS + 2)
/* Deadline, plus the most WinHTTP gets to release a cancelled request, plus slack. */
#define MAX_MS     (LATTE_HTTP_TIMEOUT_MS + 5000 + 1000)

enum { CONNECT, HEADERS, BODY, COMPLETE, PHASES };
static const char *const phase_name[PHASES] = {"connect", "headers", "body", "complete"};

static sock_t listener;
static struct sockaddr_in addr;
static ll_mutex mu;
static int runs[PHASES], failures[PHASES];
static long long slowest;

static long long now_ms(void)
{
#ifdef _WIN32
    return (long long)GetTickCount64();
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
#endif
}

/* Serves connections until one arrives with no request. The path picks the
 * answer: /complete gets one, /body gets the headers and part of the body,
 * /headers gets nothing. Each connection is held until the client drops it. */
static void *serve(void *arg)
{
    (void)arg;
    for (;;) {
        sock_t c = accept(listener, NULL, NULL);
        char req[2048];
        int n = (int)recv(c, req, (int)sizeof(req) - 1, 0);
        if (n <= 0) {
            sock_close(c);
            return NULL;
        }
        req[n] = '\0';
        const char *answer = NULL;
        if (strstr(req, "POST /complete "))
            answer = "HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\n{}";
        else if (strstr(req, "POST /body "))
            answer = "HTTP/1.1 200 OK\r\nContent-Length: 100000\r\nConnection: close\r\n\r\n{\"partial\":";
        if (answer) send(c, answer, (int)strlen(answer), 0);
        while (recv(c, req, (int)sizeof(req), 0) > 0) {}
        sock_close(c);
    }
}

static void *client(void *arg)
{
    int id = (int)(size_t)arg;
    for (int i = 0; i < PER_THREAD; i++) {
        int phase = (i + id) % PHASES;
        char url[64];
        if (phase == CONNECT) /* TEST-NET-1 (RFC 5737): nothing answers there. */
            snprintf(url, sizeof(url), "http://192.0.2.1:9/");
        else
            snprintf(url, sizeof(url), "http://127.0.0.1:%d/%s", ntohs(addr.sin_port), phase_name[phase]);
        long status = 0;
        char *resp = NULL;
        long long start = now_ms();
        int r = ll_http_post(url, "{\"stress\":true}", &status, &resp);
        long long took = now_ms() - start;
        int ok = phase == COMPLETE ? r == 0 && status == 200 && resp && strcmp(resp, "{}") == 0
                                   : r == -1 && resp == NULL;
        free(resp);
        ll_mutex_lock(&mu);
        runs[phase]++;
        failures[phase] += !ok || took > MAX_MS;
        if (took > slowest) slowest = took;
        ll_mutex_unlock(&mu);
    }
    return NULL;
}

static void run_clients(void)
{
    ll_thread t[THREADS];
    for (int i = 0; i < THREADS; i++) ll_thread_spawn(&t[i], client, (void *)(size_t)i);
    for (int i = 0; i < THREADS; i++) ll_thread_join(&t[i]);
}

#ifdef _WIN32
static long handles(void)
{
    DWORD n = 0;
    GetProcessHandleCount(GetCurrentProcess(), &n);
    return (long)n;
}
#endif

int main(void)
{
    printf("=== test_transport_stress (deadline %ld ms) ===\n", (long)LATTE_HTTP_TIMEOUT_MS);
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#else
    signal(SIGPIPE, SIG_IGN);
#endif
    ll_mutex_init(&mu);

    listener = socket(AF_INET, SOCK_STREAM, 0);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    socklen_t len = (socklen_t)sizeof(addr);
    if (bind(listener, (struct sockaddr *)&addr, (int)sizeof(addr)) < 0 || listen(listener, 64) < 0 ||
        getsockname(listener, (struct sockaddr *)&addr, &len) < 0) {
        perror("listener");
        return 2;
    }
    ll_thread servers[SERVERS];
    for (int i = 0; i < SERVERS; i++) ll_thread_spawn(&servers[i], serve, NULL);

    /* A first round brings WinHTTP's and the system's pools up to size. */
    run_clients();
    memset(runs, 0, sizeof(runs));
    memset(failures, 0, sizeof(failures));
    slowest = 0;
#ifdef _WIN32
    long before = handles();
#endif

    long long start = now_ms();
    run_clients();
    long long took = now_ms() - start;

    int fail = 0, total = 0;
    for (int p = 0; p < PHASES; p++) {
        printf("  %s  %-8s %d requests, %d wrong or late\n",
               failures[p] ? "FAIL" : "PASS", phase_name[p], runs[p], failures[p]);
        fail += failures[p] != 0;
        total += runs[p];
    }
    printf("  %d requests in %lld ms, slowest %lld ms (limit %d ms)\n",
           total, took, slowest, (int)MAX_MS);

#ifdef _WIN32
    Sleep(500); /* the last HANDLE_CLOSING callbacks may still be returning */
    long after = handles();
    /* A leaked request keeps its event: 60 per phase would show. */
    int leak = after - before > 40;
    printf("  %s  handles before %ld, after %ld\n", leak ? "FAIL" : "PASS", before, after);
    fail += leak;
#endif

    for (int i = 0; i < SERVERS; i++) {
        sock_t c = socket(AF_INET, SOCK_STREAM, 0);
        connect(c, (struct sockaddr *)&addr, (int)sizeof(addr));
        sock_close(c);
    }
    for (int i = 0; i < SERVERS; i++) ll_thread_join(&servers[i]);
    sock_close(listener);
    ll_mutex_destroy(&mu);

    printf("\n%s\n", fail ? "FAILED" : "all passed");
    return fail ? 1 : 0;
}
