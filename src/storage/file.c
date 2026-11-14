#include "file.h"
#include "../../vendor/cjson/cJSON.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#if defined(_WIN32)
#  include <windows.h>
#  include <fcntl.h>
#  include <io.h>
#else
#  include <unistd.h>
#endif

static char *read_file(const char *path)
{
#if defined(_WIN32)
    /* FILE_SHARE_DELETE, which fopen leaves out, lets a save replace the
     * cache while this reads it. A rename in progress briefly refuses the
     * open, so sharing failures are retried as replace_file does. */
    HANDLE h;
    for (DWORD delay = 1; ; delay *= 2) {
        h = CreateFileA(path, GENERIC_READ,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h != INVALID_HANDLE_VALUE) break;
        DWORD err = GetLastError();
        if ((err != ERROR_ACCESS_DENIED && err != ERROR_SHARING_VIOLATION) || delay > 128)
            return NULL;
        Sleep(delay);
    }
    int fd = _open_osfhandle((intptr_t)h, _O_RDONLY);
    if (fd < 0) { CloseHandle(h); return NULL; }
    FILE *fp = _fdopen(fd, "r");
    if (!fp) { _close(fd); return NULL; }
#else
    FILE *fp = fopen(path, "r");
    if (!fp) return NULL;
#endif
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

#if defined(_WIN32)
/* FileRenameInfoEx and its flags, spelled out for headers that predate them;
 * the struct is FILE_RENAME_INFO with Flags in place of ReplaceIfExists. */
#define LL_FILE_RENAME_INFO_EX ((FILE_INFO_BY_HANDLE_CLASS)22)
#define LL_RENAME_REPLACE_IF_EXISTS 0x1
#define LL_RENAME_POSIX_SEMANTICS   0x2
typedef struct { DWORD flags; HANDLE root; DWORD len; WCHAR name[1024]; } ll_rename_info;

/*
 * Moves tmp over path. A POSIX-semantics rename (Windows 10 1607 and later,
 * NTFS) replaces the cache even while readers hold it open with
 * FILE_SHARE_DELETE, as read_file does; MoveFileEx refuses while any handle
 * is open, so it is only the fallback where POSIX semantics are not
 * supported. A program that opened the cache without FILE_SHARE_DELETE, such
 * as a virus scanner, blocks both, so those failures are retried for about
 * a quarter of a second. Returns 1 on success.
 */
static int replace_file(const char *tmp, const char *path)
{
    char full[1024];
    ll_rename_info ri = { LL_RENAME_REPLACE_IF_EXISTS | LL_RENAME_POSIX_SEMANTICS, NULL, 0, {0} };
    DWORD n = GetFullPathNameA(path, sizeof(full), full, NULL);
    int wlen = n && n < sizeof(full)
             ? MultiByteToWideChar(CP_ACP, 0, full, -1, ri.name, 1024) : 0;
    if (wlen <= 0) return 0;
    ri.len = (DWORD)(wlen - 1) * sizeof(WCHAR);
    DWORD ri_size = (DWORD)(offsetof(ll_rename_info, name) + wlen * sizeof(WCHAR));

    for (DWORD delay = 1; ; delay *= 2) {
        HANDLE h = CreateFileA(tmp, DELETE,
                               FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        int ok = h != INVALID_HANDLE_VALUE &&
                 SetFileInformationByHandle(h, LL_FILE_RENAME_INFO_EX, &ri, ri_size);
        DWORD err = ok ? 0 : GetLastError();
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
        if (ok) return 1;
        if (err == ERROR_INVALID_PARAMETER || err == ERROR_NOT_SUPPORTED ||
            err == ERROR_INVALID_FUNCTION) {
            if (MoveFileExA(tmp, path, MOVEFILE_REPLACE_EXISTING)) return 1;
            err = GetLastError();
        }
        if ((err != ERROR_ACCESS_DENIED && err != ERROR_SHARING_VIOLATION) || delay > 128)
            return 0;
        Sleep(delay);
    }
}
#endif

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

    /* Write a temp file beside the cache and rename it over, so a reader or a
     * second writer never sees a partial file. */
    size_t plen = strlen(path);
    char *tmp = (char *)malloc(plen + 40);
    if (!tmp) { free(data); return -1; }
    FILE *fp;
#if defined(_WIN32)
    static volatile LONG seq;
    snprintf(tmp, plen + 40, "%s.%lu.%ld.tmp", path,
             (unsigned long)GetCurrentProcessId(), (long)InterlockedIncrement(&seq));
    fp = fopen(tmp, "wx");
#else
    snprintf(tmp, plen + 40, "%s.tmpXXXXXX", path);
    int fd = mkstemp(tmp);
    fp = fd >= 0 ? fdopen(fd, "w") : NULL;
    if (fd >= 0 && !fp) { close(fd); remove(tmp); }
#endif
    if (!fp) { free(tmp); free(data); return -1; }
    int ok = fputs(data, fp) >= 0;
    free(data);
    ok = fflush(fp) == 0 && ok;
    ok = fclose(fp) == 0 && ok;
#if defined(_WIN32)
    ok = ok && replace_file(tmp, path);
#else
    ok = ok && rename(tmp, path) == 0;
#endif
    if (!ok) remove(tmp);
    free(tmp);
    return ok ? 0 : -1;
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
