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

/*
 * Gives every phase of the next WinHTTP call what is left of the deadline.
 * Returns 0 when the deadline has passed or the timeouts cannot be set.
 */
static int arm(HINTERNET req, ULONGLONG deadline)
{
    ULONGLONG now = GetTickCount64();
    if (now >= deadline) return 0;
    int ms = (int)(deadline - now);
    return WinHttpSetTimeouts(req, ms, ms, ms, ms);
}

int ll_http_post(const char *url, const char *body, long *status, char **response)
{
    int rc = -1;
    wchar_t *wurl = NULL, host[1024];
    char *buf = NULL;
    size_t len = 0;
    HINTERNET ses = NULL, con = NULL, req = NULL;
    ULONGLONG deadline = GetTickCount64() + (ULONGLONG)LATTE_HTTP_TIMEOUT_SECS * 1000;

    *status = 0;
    *response = NULL;

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
                      WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!ses)
        ses = WinHttpOpen(NULL, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                          WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!ses) goto done;
    con = WinHttpConnect(ses, host, uc.nPort, 0);
    if (!con) goto done;
    req = WinHttpOpenRequest(con, L"POST", uc.lpszUrlPath, NULL, WINHTTP_NO_REFERER,
                             WINHTTP_DEFAULT_ACCEPT_TYPES,
                             uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);
    if (!req) goto done;

    DWORD size = (DWORD)strlen(body);
    if (!arm(req, deadline) ||
        !WinHttpSendRequest(req, L"Content-Type: application/json", (DWORD)-1,
                            (LPVOID)body, size, size, 0) ||
        !arm(req, deadline) || !WinHttpReceiveResponse(req, NULL))
        goto done;

    DWORD code = 0, code_size = sizeof(code);
    if (!WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &code, &code_size,
                             WINHTTP_NO_HEADER_INDEX))
        goto done;

    for (;;) {
        char *tmp = (char *)realloc(buf, len + READ_CHUNK + 1);
        DWORD got = 0;
        if (!tmp) goto done;
        buf = tmp;
        if (!arm(req, deadline) || !WinHttpReadData(req, buf + len, READ_CHUNK, &got)) goto done;
        if (!got) break;
        len += got;
    }

    *status = (long)code;
    if (len) {
        buf[len] = '\0';
        *response = buf;
        buf = NULL;
    }
    rc = 0;

done:
    free(buf);
    free(wurl);
    if (req) WinHttpCloseHandle(req);
    if (con) WinHttpCloseHandle(con);
    if (ses) WinHttpCloseHandle(ses);
    return rc;
}
