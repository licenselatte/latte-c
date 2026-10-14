#ifndef LATTE_H
#define LATTE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

/* Opaque SDK handle: create with latte_new, destroy with latte_free. */
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
  LATTE_ERR_INTERNAL,

  /* latte_deactivate: the seller's deactivation policy refused the release.
   * The reason, message and available_at are in latte_deactivate_result. */
  LATTE_ERR_DEACTIVATION_BLOCKED
} latte_status;

/* Key-value pair for license metadata (from the pmd JWT claim). */
typedef struct {
  char *key;
  char *value;
} latte_kv;

/*
 * Typed entitlements: the answers a seller signed into a licence about what
 * their customer bought (the `ent` JWT claim).
 *
 * An entitlement answers one of exactly two questions about the software you
 * shipped: *may this customer do X* (a boolean, read with latte_can) and
 * *how many Y do they get* (an integer, read with latte_limit). The values
 * are set on a policy and overridden per licence in the LicenseLatte
 * dashboard, resolved server-side, and signed into the activation token --
 * so latte_can and latte_limit answer fully offline, with no network call.
 *
 * They are deliberately not the same thing as `metadata` (the pmd claim):
 * metadata is arbitrary display data, filtered per field, and stringly
 * typed. Entitlements are booleans and integers, unfiltered, and exist
 * precisely to be read on the customer's machine. The two never merge, and
 * the same key may appear in both meaning different things.
 *
 * ABSENCE DENIES, AND THAT HAS A ROLLOUT CONSEQUENCE. A key that is not in
 * the claim answers 0 / not-present. There is no "unknown means allow": the
 * token is a bearer artefact sitting in a file on the machine of the person
 * it constrains, so if absence granted, stripping the claim would unlock
 * everything. The cost of that default lands on you: shipping
 * `if (!latte_can(lic, "export_pdf")) hide();` before your installed base
 * has renewed disables PDF export for every customer whose cached token
 * predates the claim. Use latte_has_entitlements() to bridge one release,
 * then drop the fallback once the base has renewed.
 *
 * Entitlements are a distribution mechanism for a signed answer, not a
 * tamper-proofing one. If real revenue depends on a feature, re-validate it
 * server-side.
 */
typedef enum {
  LATTE_ENT_BOOL = 0,
  LATTE_ENT_INT = 1
} latte_entitlement_kind;

/*
 * One entitlement. Exactly one of bool_value / int_value is meaningful,
 * selected by kind; the other is zero.
 *
 * A tagged struct rather than a union, because a union buys four bytes and
 * costs every caller a reason to get it wrong.
 */
typedef struct {
  char                  *key;
  latte_entitlement_kind kind;
  int                    bool_value;  /* meaningful when kind == LATTE_ENT_BOOL */
  int64_t                int_value;   /* meaningful when kind == LATTE_ENT_INT  */
} latte_entitlement;

/*
 * The sentinel an integer entitlement carries to mean "no ceiling".
 * latte_limit() returns it as-is; compare against this constant rather than
 * testing for a negative number.
 */
#define LATTE_UNLIMITED ((int64_t)-1)

/*
 * A validated, active license.  Caller owns this struct; free with
 * latte_license_free().
 *
 * issued_at / expires_at are Unix seconds (UTC).
 * grace_period_seconds is the offline tolerance window measured from issued_at.
 * in_grace_period is 1 when the device has been offline > 60 min but the grace
 *   window has not yet elapsed: surface a "please reconnect" warning.
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

  /*
   * Entitlements, appended at the end of the struct so existing compiled
   * callers keep working: consumers never allocate or sizeof a
   * latte_license -- they receive one from latte_activate / latte_check and
   * hand it back to latte_license_free -- so the library owns this layout.
   * Do not insert new fields above this point.
   *
   * has_entitlements is 1 when the token carried an `ent` claim at all,
   * including an empty one; that is not the same as entitlement_count > 0,
   * and it is the distinction an application branches on while its
   * installed base renews. Read individual keys with latte_can and
   * latte_limit rather than scanning this array, unless you want to
   * enumerate what was granted.
   */
  uint32_t has_entitlements;
  uint32_t entitlement_count;
  latte_entitlement *entitlements;
} latte_license;

/*
 * Configuration for latte_new(), built with latte_config_new() and the
 * latte_config_set_* setters below. Opaque so new options can be added
 * without breaking existing callers or the SDK's binary interface.
 */
typedef struct latte_config latte_config;

/*
 * latte_config_new: start building a config.
 *
 * app_id: project key from the LicenseLatte dashboard, format
 *         pk_{env}_{32}. Required; copied internally.
 *
 * Returns NULL on allocation failure or if app_id is NULL. Free with
 * latte_config_free() when done - see latte_new().
 */
latte_config *latte_config_new(const char *app_id);

/*
 * latte_config_set_multi_instance - opt into per-directory token storage.
 *
 * Normally one machine has a single cached token per app_id (stored in the
 * OS config directory), so only one license can be active for that app at
 * a time on the machine. Set multi_instance to 1 to let several
 * independently-licensed instances of the *same* app_id run side by side
 * (e.g. several portable installs of the same app, each in its own
 * directory, each activated with a different license). The working
 * directory itself is the instance boundary: the token is stored at
 * `.licenselatte/{app_id}.latte` relative to the CWD instead of the
 * shared OS config directory, which is never read or written in this mode.
 *
 * Returns cfg, for chaining. No-op if cfg is NULL.
 */
latte_config *latte_config_set_multi_instance(latte_config *cfg, int multi_instance);

/*
 * latte_config_set_machine_id - supply the raw machine ID yourself.
 *
 * By default the SDK reads the OS machine ID (IOPlatformUUID on macOS,
 * /var/lib/dbus/machine-id or /etc/machine-id on Linux, MachineGuid on
 * Windows). Either way it sends only
 *   HMAC-SHA256(key = raw ID, msg = "licenselatte_" + app_id)
 * as lowercase hex; the raw ID never leaves the machine. raw_machine_id
 * is used byte for byte, with no trimming, and copied internally.
 *
 * Set it when the OS ID does not identify the seat you want to license:
 * containers that share an image's /etc/machine-id or have none, cloned
 * VMs that share one, a seat per user rather than per machine (pass a
 * stable user ID), or tests. Keep it stable: changing it on an activated
 * install makes that install a new machine, which takes another seat.
 *
 * NULL or "" clears it, so the OS ID is used. Returns cfg, for chaining.
 * No-op if cfg is NULL. If the copy cannot be allocated, latte_new()
 * returns LATTE_ERR_INTERNAL rather than silently using the OS ID.
 */
latte_config *latte_config_set_machine_id(latte_config *cfg, const char *raw_machine_id);

/*
 * latte_config_set_machine_name - the name this machine is given when it
 * activates, shown to the customer by latte_machines() and the customer
 * portal, and to you in the dashboard.
 *
 * By default (or after passing NULL) the SDK sends the OS hostname, or no
 * name when the hostname cannot be read. "" sends no name at all. The
 * server stores the name only when the activation is created and drops an
 * invalid one; a name set later with latte_rename_machine() is kept on
 * re-activation. Copied internally.
 *
 * Returns cfg, for chaining. No-op if cfg is NULL. If the copy cannot be
 * allocated, latte_new() returns LATTE_ERR_INTERNAL.
 */
latte_config *latte_config_set_machine_name(latte_config *cfg, const char *name);

/* Release a config built with latte_config_new(). */
void latte_config_free(latte_config *cfg);

/*
 * latte_new - create one SDK instance per application process.
 *
 * config: built with latte_config_new(). latte_new() reads cfg but does
 *         not take ownership of it. Call latte_config_free() when you're
 *         done with cfg, regardless of whether latte_new() succeeded or
 *         failed.
 * out:    receives the new SDK handle on LATTE_OK.
 *
 * Returns LATTE_ERR_INVALID_CONFIG if config or its app_id is NULL,
 *         LATTE_ERR_INVALID_APPID / _CHECKSUM if app_id is malformed,
 *         LATTE_ERR_STORAGE_INIT_FAILED if the token directory cannot be
 * created, LATTE_ERR_MACHINE_ID_FAILED if the machine fingerprint cannot be
 * read (only when no machine ID was set with latte_config_set_machine_id()).
 */
latte_status latte_new(const latte_config *config, latte_sdk **out);

/*
 * latte_activate: validate a license key and activate this machine.
 *
 * Fast path: if a valid cached token exists, returns immediately (no network).
 *   A background thread silently renews the token to keep it fresh.
 * Slow path: calls POST /v1/activate and stores the token.
 *
 * The key is normalised automatically (uppercase, strip hyphens/spaces) and
 * given a minimal sanity check (non-empty, not implausibly long) before
 * ever reaching the network -- a license key may be native or a
 * legacy-system alias (see the legacy-key-migration feature in
 * license-latte-api, internal/usecase/api/activate_license.go), and only
 * the server knows which, so anything beyond that minimal check is
 * deferred to it.
 *
 * out receives a heap-allocated latte_license on LATTE_OK; free with
 * latte_license_free().
 */
latte_status latte_activate(latte_sdk *sdk, const char *key,
                            latte_license **out);

/*
 * latte_check: validate the locally-stored token without a network call.
 *
 * Returns LATTE_ERR_NOT_ACTIVATED if Activate has never been called.
 * Returns LATTE_ERR_LICENSE_EXPIRED if the grace period has elapsed.
 */
latte_status latte_check(latte_sdk *sdk, latte_license **out);

/*
 * latte_can: whether the boolean entitlement named by key is present and
 * true. Returns 1 or 0.
 *
 * A key that is absent, or that holds an integer rather than a boolean,
 * answers 0. There is no coercion across kinds: latte_can on an integer
 * entitlement is 0 even when that integer is non-zero, because a rule that
 * read "nonzero is true" is one five SDKs would eventually disagree about.
 *
 * Returns 0 for a NULL lic or a NULL key.
 */
int latte_can(const latte_license *lic, const char *key);

/*
 * latte_limit: the integer entitlement named by key.
 *
 * Returns 1 and writes the value through out when the key is present and
 * holds an integer; returns 0 and leaves out untouched otherwise. out may be
 * NULL to test presence alone.
 *
 * The unlimited sentinel is written out as-is: compare *out against
 * LATTE_UNLIMITED rather than testing for a negative number. A key that
 * holds a boolean rather than an integer misses -- latte_limit on a boolean
 * returns 0, not 1-with-a-1-or-0.
 */
int latte_limit(const latte_license *lic, const char *key, int64_t *out);

/*
 * latte_has_entitlements: whether the activation token carried an `ent`
 * claim at all -- including an empty one, which is why this is not an
 * entitlement_count check. Returns 1 or 0 (0 for a NULL lic).
 *
 * It exists for one job: letting an application fall back to its
 * pre-entitlements behaviour for the one release it takes an installed base
 * to renew. See the latte_entitlement comment above.
 */
int latte_has_entitlements(const latte_license *lic);

/*
 * What latte_deactivate reports. Times are Unix seconds (UTC), 0 when absent.
 */
typedef struct {
  /* 0 when this machine was already released, on an earlier call or by the
   * seller. Both outcomes are success. */
  int     released;
  /* When the freed seat can be used by another machine: set when the
   * seller's policy holds it until this machine's last token has expired,
   * 0 when it is free now. */
  int64_t seat_free_at;

  /* Filled on LATTE_ERR_DEACTIVATION_BLOCKED only. reason is
   * "deactivation_cooldown" or "deactivation_limit_reached"; message is
   * written to be shown to the user; available_at is when a release becomes
   * possible, 0 when not known. */
  char    reason[64];
  char    message[512];
  int64_t available_at;
} latte_deactivate_result;

/*
 * latte_deactivate: release this machine's seat so the licence can be
 * activated on another machine.
 *
 * Needs the network. The server releases the seat first, and only once it
 * has answered does the SDK delete the cached token, after which
 * latte_check returns LATTE_ERR_NOT_ACTIVATED until the next latte_activate.
 * On any error the cached token is kept and the call can be retried. A
 * machine that is lost or dead cannot call this; its seat is released from
 * the seller's customer portal.
 *
 * The activation id and licence key come from the cached token, read
 * without verifying it, so an expired token can still release its seat.
 * Returns LATTE_ERR_NOT_ACTIVATED when there is no cached token, and
 * LATTE_ERR_DEACTIVATION_BLOCKED when the seller's policy refuses the
 * release. out may be NULL.
 */
latte_status latte_deactivate(latte_sdk *sdk, latte_deactivate_result *out);

/*
 * One machine holding a seat on the licence, as latte_machines lists it.
 * It identifies a machine for a person to recognise and is never a handle
 * for acting on one. Times are Unix seconds (UTC).
 */
typedef struct {
  char   *name;          /* NULL when the machine has none */
  char   *fingerprint;   /* first 12 characters of the machine id hash */
  int64_t activated_at;
  int64_t last_seen_at;  /* the machine's last activation or renewal */
  int64_t seat_free_at;  /* set on a released machine whose seat is still held, else 0 */
  int     current;       /* 1 for the machine the SDK is running on */
} latte_machine;

/*
 * latte_machines: list the machines holding a seat on this machine's
 * licence. Needs the network and an active activation on this machine
 * (LATTE_ERR_LICENSE_EXPIRED otherwise, from the server's 403).
 *
 * On LATTE_OK, out receives an array of count machines, NULL when count is
 * 0; free it with latte_machines_free().
 */
latte_status latte_machines(latte_sdk *sdk, latte_machine **out, size_t *count);

/* Release an array returned by latte_machines. */
void latte_machines_free(latte_machine *machines, size_t count);

/* Buffer size that holds any stored machine name: 64 characters of up to
 * four UTF-8 bytes each, plus the terminator. */
#define LATTE_MACHINE_NAME_SIZE 257

/*
 * latte_rename_machine: name this machine. Needs the network and an active
 * activation on this machine.
 *
 * The server trims the name and refuses one that is then empty, longer than
 * 64 characters or holds control characters. On LATTE_OK the name as
 * stored is written to stored (stored_size bytes, LATTE_MACHINE_NAME_SIZE
 * holds any name); stored may be NULL.
 */
latte_status latte_rename_machine(latte_sdk *sdk, const char *name,
                                  char *stored, size_t stored_size);

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
