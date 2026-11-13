#include "transport.h"
#include "../constants.h"
#include <windows.h>
#include <winhttp.h>
#include <stdlib.h>
#include <string.h>

#ifndef WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY
#define WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY 4
#endif

#define READ_CHUNK 4096
/* How long after the deadline WinHTTP gets to release a cancelled request. */
#define RELEASE_GRACE_MS 5000

/*
 * WinHTTP's own timeouts restart on every byte received, so a peer that
 * drips its handshake or response keeps a synchronous call alive for ever.
 * The request runs asynchronously instead: each step is waited on with what
 * is left of the deadline, and closing the handle cancels it.
 */
typedef struct {
    HANDLE         event;   /* set by each completion below */
    volatile DWORD status;  /* the WINHTTP_CALLBACK_STATUS_* that set it */
    DWORD          bytes;   /* READ_COMPLETE: bytes read, 0 at the end */
    char          *buf;     /* response buffer a pending read writes into */
} completion;

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
}

/* Whether the step just started completed with want before the deadline. */
static int wait_for(completion *c, DWORD want, ULONGLONG deadline)
{
    ULONGLONG now = GetTickCount64();
    return now < deadline &&
           WaitForSingleObject(c->event, (DWORD)(deadline - now)) == WAIT_OBJECT_0 &&
           c->status == want;
}

/* Whether WinHTTP released the closed request (HANDLE_CLOSING) by limit. */
static int released(completion *c, ULONGLONG limit)
{
    while (c->status != WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING) {
        ULONGLONG now = GetTickCount64();
        if (now >= limit || WaitForSingleObject(c->event, (DWORD)(limit - now)) != WAIT_OBJECT_0)
            return 0;
    }
    return 1;
}

int ll_http_post(const char *url, const char *body, long *status, char **response)
{
    int rc = -1, watching = 0;
    wchar_t *wurl = NULL, host[1024];
    size_t len = 0;
    HINTERNET ses = NULL, con = NULL, req = NULL;
    ULONGLONG deadline = GetTickCount64() + (ULONGLONG)LATTE_HTTP_TIMEOUT_SECS * 1000;
    completion *c = (completion *)calloc(1, sizeof(completion));

    *status = 0;
    *response = NULL;

    if (!c || !(c->event = CreateEventW(NULL, FALSE, FALSE, NULL))) {
        free(c);
        return -1;
    }
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
    if (!ses) {
        /* Before Windows 8.1: no automatic proxy, and TLS 1.2 is off by default. */
        DWORD tls12 = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
        ses = WinHttpOpen(NULL, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                          WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, WINHTTP_FLAG_ASYNC);
        if (ses) WinHttpSetOption(ses, WINHTTP_OPTION_SECURE_PROTOCOLS, &tls12, sizeof(tls12));
    }
    if (!ses) goto done;
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

    DWORD size = (DWORD)strlen(body);
    if (!WinHttpSendRequest(req, L"Content-Type: application/json", (DWORD)-1,
                            (LPVOID)body, size, size, context) ||
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
    /* Closing cancels any pending step. Until HANDLE_CLOSING, WinHTTP may
     * still write to c and c->buf: if that has not come by the grace limit,
     * they are left to it rather than block the caller. */
    if (req) WinHttpCloseHandle(req);
    int keep = req && watching && !released(c, deadline + RELEASE_GRACE_MS);
    if (con) WinHttpCloseHandle(con);
    if (ses) WinHttpCloseHandle(ses);
    if (!keep) {
        CloseHandle(c->event);
        free(c->buf);
        free(c);
    }
    return rc;
}
