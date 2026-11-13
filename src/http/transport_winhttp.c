#include "transport.h"
#include "../constants.h"
#include <windows.h>
#include <winhttp.h>
#include <stdlib.h>
#include <string.h>

#ifndef WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY
#define WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY 4
#endif
#ifndef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
#define WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3 0x00002000
#endif

#define READ_CHUNK 4096
/* How long after the deadline WinHTTP gets to release a cancelled request. */
#define RELEASE_GRACE_MS 5000

/*
 * WinHTTP's own timeouts restart on every byte received, so a peer that
 * drips its handshake or response keeps a synchronous call alive for ever.
 * The request runs asynchronously instead: each step is waited on with what
 * is left of the deadline, and closing the handle cancels it.
 *
 * WinHTTP uses the completion, the body it sends and the buffer it reads into
 * until it reports HANDLE_CLOSING, which may come after ll_http_post gives up
 * on it. So ll_http_post and the request each hold a reference, and whichever
 * lets go last frees them.
 */
typedef struct {
    volatile LONG  refs;
    HANDLE         event;   /* set by each completion below */
    volatile DWORD status;  /* the WINHTTP_CALLBACK_STATUS_* that set it */
    DWORD          bytes;   /* READ_COMPLETE: bytes read, 0 at the end */
    char          *body;    /* copy of the request body */
    char          *buf;     /* response buffer a pending read writes into */
} completion;

static void release(completion *c)
{
    if (InterlockedDecrement(&c->refs)) return;
    CloseHandle(c->event);
    free(c->body);
    free(c->buf);
    free(c);
}

static void CALLBACK on_status(HINTERNET h, DWORD_PTR context, DWORD status,
                               LPVOID info, DWORD info_len)
{
    completion *c = (completion *)context;
    (void)h;
    (void)info;
    switch (status) {
    case WINHTTP_CALLBACK_STATUS_READ_COMPLETE:
        c->bytes = info_len;
        break;
    case WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE:
    case WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE:
    case WINHTTP_CALLBACK_STATUS_REQUEST_ERROR:
    case WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING:
        break;
    default:
        return;
    }
    c->status = status;
    SetEvent(c->event);
    /* The last notification for the request. */
    if (status == WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING) release(c);
}

/* Whether the step just started completed with want before the deadline. */
static int wait_for(completion *c, DWORD want, ULONGLONG deadline)
{
    ULONGLONG now = GetTickCount64();
    return now < deadline &&
           WaitForSingleObject(c->event, (DWORD)(deadline - now)) == WAIT_OBJECT_0 &&
           c->status == want;
}

/* Waits, until limit at most, for WinHTTP to report the closed request gone. */
static void wait_closed(completion *c, ULONGLONG limit)
{
    while (c->status != WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING) {
        ULONGLONG now = GetTickCount64();
        if (now >= limit || WaitForSingleObject(c->event, (DWORD)(limit - now)) != WAIT_OBJECT_0)
            return;
    }
}

int ll_http_post(const char *url, const char *body, long *status, char **response)
{
    int rc = -1, watching = 0;
    wchar_t *wurl = NULL, host[1024];
    size_t len = 0;
    HINTERNET ses = NULL, con = NULL, req = NULL;
    ULONGLONG deadline = GetTickCount64() + (ULONGLONG)LATTE_HTTP_TIMEOUT_MS;
    completion *c = (completion *)calloc(1, sizeof(completion));

    *status = 0;
    *response = NULL;

    DWORD size = (DWORD)strlen(body);
    if (!c) return -1;
    c->refs = 1;
    c->event = CreateEventW(NULL, FALSE, FALSE, NULL);
    c->body = (char *)malloc(size ? size : 1);
    if (!c->event || !c->body) goto done;
    memcpy(c->body, body, size);
    int n = MultiByteToWideChar(CP_UTF8, 0, url, -1, NULL, 0);
    wurl = n ? (wchar_t *)malloc((size_t)n * sizeof(wchar_t)) : NULL;
    if (!wurl || !MultiByteToWideChar(CP_UTF8, 0, url, -1, wurl, n)) goto done;

    /* The path points into wurl and runs on through any query to its end. */
    URL_COMPONENTS uc = {0};
    uc.dwStructSize     = sizeof(uc);
    uc.lpszHostName     = host;
    uc.dwHostNameLength = sizeof(host) / sizeof(host[0]);
    uc.dwUrlPathLength  = (DWORD)-1;
    if (!WinHttpCrackUrl(wurl, 0, 0, &uc)) goto done;

    ses = WinHttpOpen(NULL, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                      WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, WINHTTP_FLAG_ASYNC);
    if (!ses) /* Before Windows 8.1 there is no automatic proxy. */
        ses = WinHttpOpen(NULL, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                          WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, WINHTTP_FLAG_ASYNC);
    if (!ses) goto done;
    /* TLS 1.2 at least. A WinHTTP without TLS 1.3 refuses that flag; Windows
     * 7 and 8.0 leave TLS 1.2 off until it is set here (7 needs KB3140245).
     * Where even TLS 1.2 cannot be set, the request fails rather than fall
     * back to an older protocol. */
    DWORD tls = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
    if (!WinHttpSetOption(ses, WINHTTP_OPTION_SECURE_PROTOCOLS, &tls, sizeof(tls))) {
        tls = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
        if (!WinHttpSetOption(ses, WINHTTP_OPTION_SECURE_PROTOCOLS, &tls, sizeof(tls))) goto done;
    }
    con = WinHttpConnect(ses, host, uc.nPort, 0);
    if (!con) goto done;
    req = WinHttpOpenRequest(con, L"POST", uc.lpszUrlPath, NULL, WINHTTP_NO_REFERER,
                             WINHTTP_DEFAULT_ACCEPT_TYPES,
                             uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);
    if (!req) goto done;

    /* Only the request reports back, so no callback can run once it is closed. */
    DWORD_PTR context = (DWORD_PTR)c;
    if (!WinHttpSetOption(req, WINHTTP_OPTION_CONTEXT_VALUE, &context, sizeof(context)) ||
        WinHttpSetStatusCallback(req, on_status,
                                 WINHTTP_CALLBACK_FLAG_ALL_COMPLETIONS | WINHTTP_CALLBACK_FLAG_HANDLES,
                                 0) == WINHTTP_INVALID_STATUS_CALLBACK)
        goto done;
    watching = 1;
    InterlockedIncrement(&c->refs);

    if (!WinHttpSendRequest(req, L"Content-Type: application/json", (DWORD)-1,
                            c->body, size, size, context) ||
        !wait_for(c, WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE, deadline) ||
        !WinHttpReceiveResponse(req, NULL) ||
        !wait_for(c, WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE, deadline))
        goto done;

    DWORD code = 0, code_size = sizeof(code);
    if (!WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &code, &code_size,
                             WINHTTP_NO_HEADER_INDEX))
        goto done;
    /* A proxy wanting credentials never reached the API: a transport failure, as with libcurl. */
    if (code == 407) goto done;

    for (;;) {
        char *tmp = (char *)realloc(c->buf, len + READ_CHUNK + 1);
        if (!tmp) goto done;
        c->buf = tmp;
        if (!WinHttpReadData(req, c->buf + len, READ_CHUNK, NULL) ||
            !wait_for(c, WINHTTP_CALLBACK_STATUS_READ_COMPLETE, deadline))
            goto done;
        if (!c->bytes) break;
        len += c->bytes;
    }

    *status = (long)code;
    if (len) {
        c->buf[len] = '\0';
        *response = c->buf;
        c->buf = NULL;
    }
    rc = 0;

done:
    free(wurl);
    /* Closing cancels any pending step. If WinHTTP has not let go of the
     * request by the grace limit, it keeps c and frees it when it does. */
    if (req) WinHttpCloseHandle(req);
    if (watching) wait_closed(c, deadline + RELEASE_GRACE_MS);
    if (con) WinHttpCloseHandle(con);
    if (ses) WinHttpCloseHandle(ses);
    release(c);
    return rc;
}
