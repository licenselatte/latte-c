#include "client.h"
#include "../constants.h"
#include "../../vendor/cjson/cJSON.h"
#include <curl/curl.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct ll_http_client {
    char *base_url;
    char *project_key;
    char *machine_name;  /* sent on activation; NULL sends none */
};

ll_http_client *ll_http_client_new(const char *base_url, const char *project_key,
                                   const char *machine_name)
{
    ll_http_client *c = (ll_http_client *)calloc(1, sizeof(ll_http_client));
    if (!c) return NULL;
    c->base_url    = strdup(base_url);
    c->project_key = strdup(project_key);
    if (machine_name && machine_name[0]) c->machine_name = strdup(machine_name);
    return c;
}

void ll_http_client_free(ll_http_client *c)
{
    if (!c) return;
    free(c->base_url);
    free(c->project_key);
    free(c->machine_name);
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

/*
 * POSTs body to path. LL_PORT_ERR_NETWORK on a transport failure; otherwise
 * LL_PORT_OK with the HTTP status in *status and the parsed answer in *json
 * (NULL when it is not JSON; caller cJSON_Deletes it).
 */
static ll_port_error post_json(ll_http_client *c,
                               const char *path,
                               const char *body,
                               long *status,
                               cJSON **json)
{
    *json = NULL;

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
    *status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, status);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        free(resp.data);
        return LL_PORT_ERR_NETWORK;
    }

    *json = resp.data ? cJSON_Parse(resp.data) : NULL;
    free(resp.data);
    return LL_PORT_OK;
}

static ll_port_error status_error(long http_code)
{
    switch (http_code) {
    case 404: return LL_PORT_ERR_LICENSE_NOT_FOUND;
    case 403: return LL_PORT_ERR_LICENSE_INACTIVE_OR_EXPIRED;
    case 409: return LL_PORT_ERR_SEAT_LIMIT_REACHED;
    case 401: return LL_PORT_ERR_INVALID_PROJECT_KEY;
    case 429: return LL_PORT_ERR_SERVER;
    default:  return http_code >= 500 && http_code <= 599 ? LL_PORT_ERR_SERVER
                                                          : LL_PORT_ERR_INVALID_LICENSE;
    }
}

/* post_json for a call whose only success is a 200/201 JSON answer. */
static ll_port_error post_ok(ll_http_client *c, const char *path,
                             const char *body, cJSON **json)
{
    long status = 0;
    ll_port_error e = post_json(c, path, body, &status, json);
    if (e != LL_PORT_OK) return e;
    if (status != 200 && status != 201) {
        cJSON_Delete(*json);
        *json = NULL;
        return status_error(status);
    }
    return *json ? LL_PORT_OK : LL_PORT_ERR_NETWORK;
}

static ll_port_error http_post(ll_http_client *c,
                               const char *path,
                               const char *body,
                               char **token_out,
                               ll_cert_chain **chain_out)
{
    *token_out = NULL;
    *chain_out = NULL;

    cJSON *root = NULL;
    ll_port_error e = post_ok(c, path, body, &root);
    if (e != LL_PORT_OK) return e;

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

#ifndef LATTE_VERSION
#error "LATTE_VERSION must be defined by the build (CMakeLists.txt sets it from the project version)"
#endif

/* Serialises req after adding the "sdk" object, then frees req. */
static char *finish_body(cJSON *req)
{
    if (!req) return NULL;
    cJSON *sdk = cJSON_AddObjectToObject(req, "sdk");
    if (sdk) {
        cJSON_AddStringToObject(sdk, "language", "c");
        cJSON_AddStringToObject(sdk, "version",  LATTE_VERSION);
    }
    char *body = cJSON_PrintUnformatted(req);
    cJSON_Delete(req);
    return body;
}

char *ll_http_activate_body(const char *project_key,
                            const char *license_key,
                            const char *machine_id,
                            const char *machine_name)
{
    cJSON *req = cJSON_CreateObject();
    if (!req) return NULL;
    cJSON_AddStringToObject(req, "project_key", project_key);
    cJSON_AddStringToObject(req, "license_key", license_key);
    cJSON_AddStringToObject(req, "machine_id",  machine_id);
    if (machine_name && machine_name[0])
        cJSON_AddStringToObject(req, "machine_name", machine_name);
    return finish_body(req);
}

/* Renew's credentials, which the machine calls send too. */
static cJSON *credentials(const char *activation_id,
                          const char *license_key,
                          const char *machine_id)
{
    cJSON *req = cJSON_CreateObject();
    if (!req) return NULL;
    cJSON_AddStringToObject(req, "activation_id", activation_id);
    cJSON_AddStringToObject(req, "license_key",   license_key);
    cJSON_AddStringToObject(req, "machine_id",    machine_id);
    return req;
}

char *ll_http_renew_body(const char *activation_id,
                         const char *license_key,
                         const char *machine_id)
{
    return finish_body(credentials(activation_id, license_key, machine_id));
}

ll_port_error ll_http_activate(ll_http_client *c,
                               const char *license_key,
                               const char *machine_id,
                               char **token_out,
                               ll_cert_chain **chain_out)
{
    char *body = ll_http_activate_body(c->project_key, license_key, machine_id,
                                       c->machine_name);
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
    char *body = ll_http_renew_body(activation_id, license_key, machine_id);
    if (!body) return LL_PORT_ERR_NETWORK;

    ll_port_error e = http_post(c, "/v1/renew", body, token_out, chain_out);
    free(body);
    return e;
}

/* Days since the epoch, via the days_from_civil algorithm (Howard Hinnant). */
static int64_t days_from_civil(int64_t y, int64_t m, int64_t d)
{
    y -= m <= 2;
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    int64_t yoe = y - era * 400;
    int64_t mp  = (m + 9) % 12;
    int64_t doy = (153 * mp + 2) / 5 + d - 1;
    int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

/* Unix seconds of an RFC 3339 timestamp, 0 when s is NULL or malformed. */
static int64_t parse_time(const char *s)
{
    int y, mo, d, h, mi, sec, n = 0;
    if (!s || sscanf(s, "%4d-%2d-%2dT%2d:%2d:%2d%n", &y, &mo, &d, &h, &mi, &sec, &n) != 6)
        return 0;
    s += n;
    if (*s == '.')
        while (isdigit((unsigned char)*++s)) {}
    int64_t offset = 0;
    if (*s == '+' || *s == '-') {
        int oh, om;
        if (sscanf(s + 1, "%2d:%2d", &oh, &om) != 2) return 0;
        offset = (int64_t)(oh * 60 + om) * 60 * (*s == '-' ? -1 : 1);
    } else if (*s != 'Z' && *s != 'z') {
        return 0;
    }
    return days_from_civil(y, mo, d) * 86400 + h * 3600 + mi * 60 + sec - offset;
}

static const char *string_item(const cJSON *obj, const char *key)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsString(v) ? v->valuestring : NULL;
}

/* Copies src into dst[size], truncating; a no-op without a buffer. */
static void copy_string(char *dst, size_t size, const char *src)
{
    if (dst && size) snprintf(dst, size, "%s", src ? src : "");
}

ll_port_error ll_http_deactivate(ll_http_client *c,
                                 const char *activation_id,
                                 const char *license_key,
                                 const char *machine_id,
                                 latte_deactivate_result *out)
{
    char *body = ll_http_renew_body(activation_id, license_key, machine_id);
    if (!body) return LL_PORT_ERR_NETWORK;

    long status = 0;
    cJSON *root = NULL;
    ll_port_error e = post_json(c, "/v1/deactivate", body, &status, &root);
    free(body);
    if (e != LL_PORT_OK) return e;

    /* A 409 here is the seller's deactivation policy, never a seat limit. */
    if (status == 409) {
        copy_string(out->reason,  sizeof(out->reason),  string_item(root, "reason"));
        copy_string(out->message, sizeof(out->message), string_item(root, "error"));
        out->available_at = parse_time(string_item(root, "available_at"));
        cJSON_Delete(root);
        return LL_PORT_ERR_DEACTIVATION_BLOCKED;
    }
    if (status != 200 && status != 201) {
        cJSON_Delete(root);
        return status_error(status);
    }
    if (!root) return LL_PORT_ERR_NETWORK;

    out->released     = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "released"));
    out->seat_free_at = parse_time(string_item(root, "seat_free_at"));
    cJSON_Delete(root);
    return LL_PORT_OK;
}

ll_port_error ll_http_machines(ll_http_client *c,
                               const char *activation_id,
                               const char *license_key,
                               const char *machine_id,
                               latte_machine **out,
                               size_t *count)
{
    *out = NULL;
    *count = 0;

    char *body = ll_http_renew_body(activation_id, license_key, machine_id);
    if (!body) return LL_PORT_ERR_NETWORK;

    cJSON *root = NULL;
    ll_port_error e = post_ok(c, "/v1/machines", body, &root);
    free(body);
    if (e != LL_PORT_OK) return e;

    const cJSON *list = cJSON_GetObjectItemCaseSensitive(root, "machines");
    if (!cJSON_IsArray(list)) { cJSON_Delete(root); return LL_PORT_ERR_INVALID_LICENSE; }

    size_t n = (size_t)cJSON_GetArraySize(list);
    latte_machine *machines = n ? (latte_machine *)calloc(n, sizeof(latte_machine)) : NULL;
    if (n && !machines) { cJSON_Delete(root); return LL_PORT_ERR_NETWORK; }

    size_t i = 0;
    const cJSON *m;
    cJSON_ArrayForEach(m, list) {
        const char *name = string_item(m, "machine_name");
        machines[i].name         = name && name[0] ? strdup_safe(name) : NULL;
        machines[i].fingerprint  = strdup_safe(string_item(m, "machine_fingerprint"));
        machines[i].activated_at = parse_time(string_item(m, "activated_at"));
        machines[i].last_seen_at = parse_time(string_item(m, "last_seen_at"));
        machines[i].seat_free_at = parse_time(string_item(m, "seat_free_at"));
        machines[i].current      = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(m, "current"));
        i++;
    }
    cJSON_Delete(root);

    *out = machines;
    *count = n;
    return LL_PORT_OK;
}

ll_port_error ll_http_rename_machine(ll_http_client *c,
                                     const char *activation_id,
                                     const char *license_key,
                                     const char *machine_id,
                                     const char *name,
                                     char *stored,
                                     size_t stored_size)
{
    cJSON *req = credentials(activation_id, license_key, machine_id);
    if (req) cJSON_AddStringToObject(req, "machine_name", name ? name : "");
    char *body = finish_body(req);
    if (!body) return LL_PORT_ERR_NETWORK;

    cJSON *root = NULL;
    ll_port_error e = post_ok(c, "/v1/machines/rename", body, &root);
    free(body);
    if (e != LL_PORT_OK) return e;

    copy_string(stored, stored_size, string_item(root, "machine_name"));
    cJSON_Delete(root);
    return LL_PORT_OK;
}
