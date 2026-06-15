#include "file.h"
#include "../../vendor/cjson/cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>

static char *read_file(const char *path)
{
    FILE *fp = fopen(path, "r");
    if (!fp) return NULL;
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (sz <= 0 || sz > 1024 * 1024) { fclose(fp); return NULL; }
    char *buf = (char *)malloc(sz + 1);
    if (!buf) { fclose(fp); return NULL; }
    size_t n = fread(buf, 1, sz, fp);
    fclose(fp);
    buf[n] = '\0';
    /* trim trailing whitespace */
    while (n > 0 && (buf[n-1] == '\n' || buf[n-1] == '\r' || buf[n-1] == ' '))
        buf[--n] = '\0';
    return buf;
}

static char *strdup_safe(const char *s)
{
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char *p = (char *)malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

int ll_file_save_token(const char *path,
                       const char *token,
                       const ll_cert_chain *chain)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) return -1;

    cJSON_AddNumberToObject(root, "Timestamp", (double)time(NULL));
    cJSON_AddStringToObject(root, "Token",     token ? token : "");
    cJSON_AddStringToObject(root, "Submaster", chain && chain->submaster ? chain->submaster : "");
    cJSON_AddStringToObject(root, "Project",   chain && chain->project   ? chain->project   : "");
    cJSON_AddStringToObject(root, "Daily",     chain && chain->daily     ? chain->daily     : "");

    char *data = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!data) return -1;

#if defined(_WIN32)
    FILE *fp = fopen(path, "w");
#else
    FILE *fp = fopen(path, "w");
    if (fp) chmod(path, 0600);
#endif
    if (!fp) { free(data); return -1; }
    fputs(data, fp);
    free(data);
    fclose(fp);
    return 0;
}

int ll_file_load_token(const char *path,
                       char **token_out,
                       ll_cert_chain **chain_out)
{
    *token_out = NULL;
    *chain_out = NULL;

    char *raw = read_file(path);
    if (!raw) return -1;

    /* Try JSON format first (only if root is an object, not a bare number/value) */
    cJSON *root = cJSON_Parse(raw);
    if (root && cJSON_IsObject(root)) {
        cJSON *tok      = cJSON_GetObjectItemCaseSensitive(root, "Token");
        cJSON *subm     = cJSON_GetObjectItemCaseSensitive(root, "Submaster");
        cJSON *proj     = cJSON_GetObjectItemCaseSensitive(root, "Project");
        cJSON *daily    = cJSON_GetObjectItemCaseSensitive(root, "Daily");

        if (!tok || !cJSON_IsString(tok) || !tok->valuestring || !tok->valuestring[0]) {
            cJSON_Delete(root);
            free(raw);
            return -1;
        }

        *token_out = strdup_safe(tok->valuestring);

        ll_cert_chain *c = (ll_cert_chain *)calloc(1, sizeof(ll_cert_chain));
        if (c) {
            c->submaster = subm  && cJSON_IsString(subm)  ? strdup_safe(subm->valuestring)  : NULL;
            c->project   = proj  && cJSON_IsString(proj)  ? strdup_safe(proj->valuestring)  : NULL;
            c->daily     = daily && cJSON_IsString(daily) ? strdup_safe(daily->valuestring) : NULL;
        }
        *chain_out = c;
        cJSON_Delete(root);
        free(raw);
        return (*token_out) ? 0 : -1;
    }

    /* If cJSON parsed something but it wasn't an object (e.g. bare number), free it */
    if (root) { cJSON_Delete(root); root = NULL; }

    /* Legacy "timestamp:token" fallback */
    char *colon = strchr(raw, ':');
    if (!colon || colon == raw) {
        free(raw);
        return -1;
    }

    *token_out = strdup_safe(colon + 1);
    *chain_out = (ll_cert_chain *)calloc(1, sizeof(ll_cert_chain));
    free(raw);
    return (*token_out && *token_out[0]) ? 0 : -1;
}
