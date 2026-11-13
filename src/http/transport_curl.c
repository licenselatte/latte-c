#include "transport.h"
#include "../constants.h"
#include <curl/curl.h>
#include <stdlib.h>
#include <string.h>

/* Dynamic buffer for curl write callback */
typedef struct {
    char  *data;
    size_t len;
} response_buf;

static size_t write_cb(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    size_t bytes = size * nmemb;
    response_buf *buf = (response_buf *)userdata;
    char *tmp = (char *)realloc(buf->data, buf->len + bytes + 1);
    if (!tmp) return 0;
    buf->data = tmp;
    memcpy(buf->data + buf->len, ptr, bytes);
    buf->len += bytes;
    buf->data[buf->len] = '\0';
    return bytes;
}

int ll_http_post(const char *url, const char *body, long *status, char **response)
{
    *status = 0;
    *response = NULL;

    CURL *curl = curl_easy_init();
    if (!curl) return -1;

    response_buf resp = {NULL, 0};

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL,            url);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS,     body);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER,     headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,  write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA,      &resp);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT,        LATTE_HTTP_TIMEOUT_SECS);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    /* A redirect may only lead to https, so it can never downgrade the API's TLS. */
#if LIBCURL_VERSION_NUM >= 0x075500
    curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "https");
#else
    curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS, (long)CURLPROTO_HTTPS);
#endif

    CURLcode res = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, status);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        free(resp.data);
        return -1;
    }
    *response = resp.data;
    return 0;
}
