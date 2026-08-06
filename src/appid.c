#include "appid.h"
#include "crypto/key.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/stat.h>
#include <errno.h>

#if defined(_WIN32)
#  include <direct.h>
#  include <windows.h>
#  define mkdir_compat(p) _mkdir(p)
#else
#  include <unistd.h>
#  define mkdir_compat(p) mkdir((p), 0700)
#endif

/* Split "pk_env_key32" into its three parts. */
latte_status ll_parse_app_id(const char *app_id,
                              ll_env *env_out,
                              const char **key_out)
{
    if (!app_id) return LATTE_ERR_INVALID_APPID;

    /* Must start with "pk_" */
    if (strncmp(app_id, "pk_", 3) != 0) return LATTE_ERR_INVALID_APPID;
    const char *rest = app_id + 3;

    /* Find second underscore */
    const char *under = strchr(rest, '_');
    if (!under) return LATTE_ERR_INVALID_APPID;

    size_t env_len = (size_t)(under - rest);
    if (env_len == 0) return LATTE_ERR_INVALID_APPID;

    /* Determine environment */
    if (env_len == 4 && strncmp(rest, "live", 4) == 0)
        *env_out = LL_ENV_LIVE;
    else if (env_len == 4 && strncmp(rest, "test", 4) == 0)
        *env_out = LL_ENV_TEST;
    else if (env_len == 5 && strncmp(rest, "local", 5) == 0)
        *env_out = LL_ENV_LOCAL;
    else
        return LATTE_ERR_UNKNOWN_ENVIRONMENT;

    const char *key = under + 1;
    if (strlen(key) != 32) return LATTE_ERR_INVALID_APPID_KEY_SEGMENT;

    /* 4-char checksum over the 32-char key segment */
    if (!ll_validate_key(key, 4)) return LATTE_ERR_INVALID_APPID_CHECKSUM;

    *key_out = key;
    return LATTE_OK;
}

static int mkdir_p(const char *path)
{
    /* Try to create; if it exists, that's fine. */
    if (mkdir_compat(path) == 0) return 0;
    if (errno == EEXIST) return 0;
    return -1;
}

int ll_resolve_storage_path(const char *app_key, char *path_out, size_t path_size)
{
    char base[512] = {0};
    int ok = 0;

#if defined(_WIN32)
    const char *appdata = getenv("APPDATA");
    if (appdata) {
        snprintf(base, sizeof(base), "%s\\LicenseLatte", appdata);
        if (mkdir_p(base) == 0) ok = 1;
    }
#elif defined(__APPLE__)
    const char *home = getenv("HOME");
    if (home) {
        snprintf(base, sizeof(base), "%s/Library/Application Support/LicenseLatte", home);
        if (mkdir_p(base) == 0) ok = 1;
    }
#else
    /* XDG or HOME/.config */
    const char *xdg = getenv("XDG_CONFIG_HOME");
    if (xdg) {
        snprintf(base, sizeof(base), "%s/LicenseLatte", xdg);
        if (mkdir_p(base) == 0) ok = 1;
    }
    if (!ok) {
        const char *home = getenv("HOME");
        if (home) {
            snprintf(base, sizeof(base), "%s/.config/LicenseLatte", home);
            if (mkdir_p(base) == 0) ok = 1;
        }
    }
#endif

    if (!ok) {
        /* Fallback: .licenselatte in CWD */
        snprintf(base, sizeof(base), ".licenselatte");
        if (mkdir_p(base) < 0) return -1;
    }

#if defined(_WIN32)
    snprintf(path_out, path_size, "%s\\%s.latte", base, app_key);
#else
    snprintf(path_out, path_size, "%s/%s.latte", base, app_key);
#endif
    return 0;
}

int ll_resolve_storage_path_local(const char *app_key, char *path_out, size_t path_size)
{
    if (mkdir_p(".licenselatte") < 0) return -1;

#if defined(_WIN32)
    snprintf(path_out, path_size, ".licenselatte\\%s.latte", app_key);
#else
    snprintf(path_out, path_size, ".licenselatte/%s.latte", app_key);
#endif
    return 0;
}
