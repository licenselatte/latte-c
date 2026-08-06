#ifndef LATTE_H
#define LATTE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

/* Opaque SDK handle — create with latte_new, destroy with latte_free. */
typedef struct latte_sdk latte_sdk;

typedef enum {
  LATTE_OK = 0,

  /* Activation / Check errors */
  LATTE_ERR_INVALID_KEY,
  LATTE_ERR_LICENSE_EXPIRED,
  LATTE_ERR_NOT_ACTIVATED,
  LATTE_ERR_SEAT_LIMIT,
  LATTE_ERR_LICENSE_NOT_FOUND,
  LATTE_ERR_INVALID_PROJECT_KEY,

  /* SDK initialisation errors (latte_new) */
  LATTE_ERR_INVALID_CONFIG,
  LATTE_ERR_INVALID_APPID,
  LATTE_ERR_UNKNOWN_ENVIRONMENT,
  LATTE_ERR_INVALID_APPID_KEY_SEGMENT,
  LATTE_ERR_INVALID_APPID_CHECKSUM,
  LATTE_ERR_STORAGE_INIT_FAILED,
  LATTE_ERR_MACHINE_ID_FAILED,

  /* Runtime errors */
  LATTE_ERR_NETWORK,
  LATTE_ERR_SERVER_INVALID_TOKEN,
  LATTE_ERR_INTERNAL
} latte_status;

/* Key-value pair for license metadata (from the pmd JWT claim). */
typedef struct {
  char *key;
  char *value;
} latte_kv;

/*
 * A validated, active license.  Caller owns this struct; free with
 * latte_license_free().
 *
 * issued_at / expires_at are Unix seconds (UTC).
 * grace_period_seconds is the offline tolerance window measured from issued_at.
 * in_grace_period is 1 when the device has been offline > 60 min but the grace
 *   window has not yet elapsed — surface a "please reconnect" warning.
 */
typedef struct {
  char *key;
  char *activation_id;
  char *project_id;
  char *license_type;
  int64_t issued_at;
  int64_t expires_at;
  int64_t grace_period_seconds;
  uint32_t in_grace_period;
  uint32_t metadata_count;
  latte_kv *metadata;
} latte_license;

/*
 * Configuration for latte_new().
 *
 * app_id:         project key from the LicenseLatte dashboard, format
 *                 pk_{env}_{32}. Required.
 *
 * multi_instance: normally one machine has a single cached token per app_id
 *                 (stored in the OS config directory), so only one license
 *                 can be active for that app at a time on the machine. Set
 *                 this to 1 to let several independently-licensed instances
 *                 of the *same* app_id run side by side (e.g. several
 *                 portable installs of the same app, each in its own
 *                 directory, each activated with a different license). The
 *                 working directory itself is the instance boundary: the
 *                 token is stored at `.licenselatte/{app_key}.latte`
 *                 relative to the CWD instead of the shared OS config
 *                 directory, which is never read or written in this mode.
 */
typedef struct {
  const char *app_id;
  int         multi_instance;
} latte_config;

/*
 * latte_new — create one SDK instance per application process.
 *
 * config: see latte_config. Not retained; may be stack-allocated.
 * out:    receives the new SDK handle on LATTE_OK.
 *
 * Returns LATTE_ERR_INVALID_CONFIG if config or config->app_id is NULL,
 *         LATTE_ERR_INVALID_APPID / _CHECKSUM if app_id is malformed,
 *         LATTE_ERR_STORAGE_INIT_FAILED if the token directory cannot be
 * created, LATTE_ERR_MACHINE_ID_FAILED if the machine fingerprint cannot be
 * read.
 */
latte_status latte_new(const latte_config *config, latte_sdk **out);

/*
 * latte_activate — validate a license key and activate this machine.
 *
 * Fast path: if a valid cached token exists, returns immediately (no network).
 *   A background thread silently renews the token to keep it fresh.
 * Slow path: calls POST /v1/activate and stores the token.
 *
 * The key is normalised automatically (uppercase, strip hyphens/spaces,
 * O→0, I→1, L→1).
 *
 * out receives a heap-allocated latte_license on LATTE_OK; free with
 * latte_license_free().
 */
latte_status latte_activate(latte_sdk *sdk, const char *key,
                            latte_license **out);

/*
 * latte_check — validate the locally-stored token without a network call.
 *
 * Returns LATTE_ERR_NOT_ACTIVATED if Activate has never been called.
 * Returns LATTE_ERR_LICENSE_EXPIRED if the grace period has elapsed.
 */
latte_status latte_check(latte_sdk *sdk, latte_license **out);

/* Release a latte_license returned by latte_activate or latte_check. */
void latte_license_free(latte_license *lic);

/* Release the SDK handle.  Waits for any in-flight renewal thread to finish. */
void latte_free(latte_sdk *sdk);

/* Human-readable description of a status code. */
const char *latte_strerror(latte_status s);

#ifdef __cplusplus
}
#endif

#endif /* LATTE_H */
