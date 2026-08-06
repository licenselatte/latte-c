#ifndef LATTE_APPID_H
#define LATTE_APPID_H

#include "../include/latte/latte.h"

typedef enum { LL_ENV_LIVE, LL_ENV_TEST, LL_ENV_LOCAL } ll_env;

/*
 * ll_parse_app_id:
 *   Parse and validate "pk_{env}_{32char}" AppID.
 *   On success: *env_out and *key_out (pointer into app_id, not heap) are set.
 *   Returns LATTE_OK or an error code.
 */
latte_status ll_parse_app_id(const char *app_id,
                              ll_env *env_out,
                              const char **key_out);

/*
 * ll_resolve_storage_path:
 *   Compute the platform-specific .latte file path for a given 32-char app_key.
 *   path_out must be at least 512 bytes.
 *   Returns 0 on success.
 */
int ll_resolve_storage_path(const char *app_key, char *path_out, size_t path_size);

/*
 * ll_resolve_storage_path_local:
 *   Compute the .latte file path for a given app_key under ".licenselatte/"
 *   relative to the CWD (never the OS config dir). Used for multi-instance
 *   mode, where the working directory itself is the instance boundary.
 *   path_out must be at least 512 bytes.
 *   Returns 0 on success.
 */
int ll_resolve_storage_path_local(const char *app_key, char *path_out, size_t path_size);

#endif /* LATTE_APPID_H */
