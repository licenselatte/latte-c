#include "client.h"
#include "../constants.h"
#include "../../vendor/cjson/cJSON.h"
#include <curl/curl.h>
#include <stdlib.h>
#include <string.h>

struct ll_http_client {
    char *base_url;
    char *project_key;
};

ll_http_client *ll_http_client_new(const char *base_url, const char *project_key)
{
    ll_http_client *c = (ll_http_client *)calloc(1, sizeof(ll_http_client));
    if (!c) return NULL;
    c->base_url    = strdup(base_url);
    c->project_key = strdup(project_key);
    return c;
}

void ll_http_client_free(ll_http_client *c)
{
    if (!c) return;
    free(c->base_url);
    free(c->project_key);
    free(c);
}

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

static char *strdup_safe(const char *s)
{
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char *p = (char *)malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

static ll_port_error http_post(ll_http_client *c,
                               const char *path,
                               const char *body,
                               char **token_out,
                               ll_cert_chain **chain_out)
{
    *token_out = NULL;
    *chain_out = NULL;

    char url[1024];
    snprintf(url, sizeof(url), "%s%s", c->base_url, path);

    CURL *curl = curl_easy_init();
    if (!curl) return LL_PORT_ERR_NETWORK;

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

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        free(resp.data);
        return LL_PORT_ERR_NETWORK;
    }

    if (http_code != 200 && http_code != 201) {
        free(resp.data);
        switch (http_code) {
        case 404: return LL_PORT_ERR_LICENSE_NOT_FOUND;
        case 403: return LL_PORT_ERR_LICENSE_INACTIVE_OR_EXPIRED;
        case 409: return LL_PORT_ERR_SEAT_LIMIT_REACHED;
        case 401: return LL_PORT_ERR_INVALID_PROJECT_KEY;
        default:  return LL_PORT_ERR_INVALID_LICENSE;
        }
    }

    cJSON *root = resp.data ? cJSON_Parse(resp.data) : NULL;
    free(resp.data);
    if (!root) return LL_PORT_ERR_NETWORK;

    cJSON *tok   = cJSON_GetObjectItemCaseSensitive(root, "token");
    cJSON *chain = cJSON_GetObjectItemCaseSensitive(root, "chain");

    if (!tok || !cJSON_IsString(tok) || !tok->valuestring || !tok->valuestring[0]) {
        cJSON_Delete(root);
        return LL_PORT_ERR_INVALID_LICENSE;
    }

    ll_cert_chain *cc = (ll_cert_chain *)calloc(1, sizeof(ll_cert_chain));
    if (!cc) { cJSON_Delete(root); return LL_PORT_ERR_NETWORK; }

    if (chain && cJSON_IsObject(chain)) {
        cJSON *subm  = cJSON_GetObjectItemCaseSensitive(chain, "submaster");
        cJSON *proj  = cJSON_GetObjectItemCaseSensitive(chain, "project");
        cJSON *daily = cJSON_GetObjectItemCaseSensitive(chain, "daily");
        cc->submaster = strdup_safe(subm  && cJSON_IsString(subm)  ? subm->valuestring  : NULL);
        cc->project   = strdup_safe(proj  && cJSON_IsString(proj)  ? proj->valuestring  : NULL);
        cc->daily     = strdup_safe(daily && cJSON_IsString(daily) ? daily->valuestring : NULL);
    }

    if (!cc->submaster || !cc->project || !cc->daily ||
        !cc->submaster[0] || !cc->project[0] || !cc->daily[0]) {
        ll_cert_chain_free(cc);
        cJSON_Delete(root);
        return LL_PORT_ERR_INVALID_LICENSE;
    }

    *token_out = strdup_safe(tok->valuestring);
    *chain_out = cc;
    cJSON_Delete(root);
    return *token_out ? LL_PORT_OK : LL_PORT_ERR_NETWORK;
}

ll_port_error ll_http_activate(ll_http_client *c,
                               const char *license_key,
                               const char *machine_id,
                               char **token_out,
                               ll_cert_chain **chain_out)
{
    cJSON *req = cJSON_CreateObject();
    cJSON_AddStringToObject(req, "project_key", c->project_key);
    cJSON_AddStringToObject(req, "license_key", license_key);
    cJSON_AddStringToObject(req, "machine_id",  machine_id);
    char *body = cJSON_PrintUnformatted(req);
    cJSON_Delete(req);
    if (!body) return LL_PORT_ERR_NETWORK;

    ll_port_error e = http_post(c, "/v1/activate", body, token_out, chain_out);
    free(body);
    return e;
}

ll_port_error ll_http_renew(ll_http_client *c,
                            const char *activation_id,
                            const char *license_key,
                            const char *machine_id,
                            char **token_out,
                            ll_cert_chain **chain_out)
{
    cJSON *req = cJSON_CreateObject();
    cJSON_AddStringToObject(req, "activation_id", activation_id);
    cJSON_AddStringToObject(req, "license_key",   license_key);
    cJSON_AddStringToObject(req, "machine_id",    machine_id);
    char *body = cJSON_PrintUnformatted(req);
    cJSON_Delete(req);
    if (!body) return LL_PORT_ERR_NETWORK;

    ll_port_error e = http_post(c, "/v1/renew", body, token_out, chain_out);
    free(body);
    return e;
}
