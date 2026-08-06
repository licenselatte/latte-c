# latte-c

C library SDK for [LicenseLatte](https://licenselatte.com) — a port of the official [`latte-go`](https://github.com/licenselatte/latte-go) SDK with exact behavioral parity.

Embed license enforcement directly into your C application in a few lines of code. The library handles activation, offline grace periods, background token renewal, and local token caching transparently.

> [!NOTE]
> The C library has its own version line independent of the Go SDK. Both SDKs are 1.x with a stable API.

---

## Table of contents

- [Requirements](#requirements)
- [Building](#building)
- [Running the tests](#running-the-tests)
- [Running the example](#running-the-example)
- [Running the tools](#running-the-tools)
    - [sdktest](#sdktest)
    - [validator](#validator)
- [API reference](#api-reference)
    - [latte_new](#latte_new)
    - [latte_activate](#latte_activate)
    - [latte_check](#latte_check)
    - [latte_license_free / latte_free](#latte_license_free--latte_free)
    - [latte_strerror](#latte_strerror)
- [The latte_license struct](#the-latte_license-struct)
- [C++ wrapper](#c-wrapper)
- [Error codes](#error-codes)
- [License types](#license-types)
- [Offline grace period](#offline-grace-period)
- [Token storage](#token-storage)
- [Background renewal](#background-renewal)
- [Custom metadata](#custom-metadata)
- [Environments](#environments)
- [Cross-SDK compatibility](#cross-sdk-compatibility)
- [Architecture](#architecture)

---

## Requirements

| Dependency                          | Version  | Purpose                                     |
| ----------------------------------- | -------- | ------------------------------------------- |
| [libsodium](https://libsodium.org)  | ≥ 1.0.18 | Ed25519 signature verification, HMAC-SHA256 |
| [libcurl](https://curl.se/libcurl/) | ≥ 7.68   | HTTPS activate / renew requests             |
| CMake                               | ≥ 3.15   | Build system                                |
| C compiler                          | C11      | clang or gcc                                |
| pkg-config                          | any      | Dependency discovery                        |

cJSON is vendored under `vendor/cjson/` — no separate installation needed.

### macOS (Homebrew)

```sh
brew install libsodium pkg-config cmake
# libcurl ships with macOS; Homebrew's version also works
```

### Ubuntu / Debian

```sh
apt install libsodium-dev libcurl4-openssl-dev cmake pkg-config build-essential
```

### Fedora / RHEL

```sh
dnf install libsodium-devel libcurl-devel cmake pkgconfig gcc
```

### Windows (MSYS2 / MinGW-w64)

The build relies on `pkg-config` to locate libsodium and libcurl, which native MSVC does not provide. Install [MSYS2](https://www.msys2.org), then from the **MSYS2 MinGW64** shell:

```sh
pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake mingw-w64-x86_64-pkgconf \
          mingw-w64-x86_64-libsodium mingw-w64-x86_64-curl
```

Run the [Building](#building) and [Running the tests](#running-the-tests) steps below from the same MinGW64 shell — `cmake`, `gcc`, and `ctest` all behave the same as on Linux/macOS. Static and shared libraries are named `liblatte.a` / `liblatte.dll`, and binaries get a `.exe` suffix.

---

## Building

```sh
git clone https://github.com/licenselatte/latte-c
cd latte-c
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

This produces:

| Artifact       | Path                                                                                |
| -------------- | ------------------------------------------------------------------------------------ |
| Static library | `build/liblatte.a`                                                                    |
| Shared library | `build/liblatte.dylib` (macOS) / `build/liblatte.so` (Linux) / `build/liblatte.dll` (Windows) |
| Example binary | `build/example_simple` (`.exe` on Windows)                                            |
| Example binary (C++) | `build/example_simple_cpp` (`.exe` on Windows)                                  |
| CLI tool       | `build/sdktest` (`.exe` on Windows)                                                   |
| CLI tool       | `build/validator` (`.exe` on Windows)                                                 |

### Linking your application

```sh
# Static
cc myapp.c -Ipath/to/latte-c/include -Lbuild -llatte -lsodium -lcurl -o myapp

# Windows (MSYS2 MinGW64 shell)
gcc myapp.c -Ipath/to/latte-c/include -Lbuild -llatte -lsodium -lcurl -o myapp.exe

# Or let CMake handle it via FetchContent / add_subdirectory
```

CMake consumers can use the alias target `latte::latte`:

```cmake
find_package(latte REQUIRED)
target_link_libraries(myapp PRIVATE latte::latte)
```

---

## Running the tests

```sh
ctest --test-dir build --output-on-failure
```

Expected output:

```
100% tests passed, 0 tests failed out of 4

Total Test time (real) =   0.76 sec
```

Individual test binaries can be run directly for verbose output:

```sh
./build/test_key       # key checksum and sanitise logic
./build/test_storage   # JSON record round-trip and legacy format
./build/test_validate  # grace-period / expiry rules
./build/test_jwt       # base64url decode and EdDSA JWT verify
```

---

## Running the example

The example reads a license key from stdin, calls `latte_activate`, and prints the returned `latte_license`.

```sh
./build/example_simple
```

```
Type your license key (e.g. XXXXX-XXXXX-XXXXX-XXXXX-XXXXXX):
> G98S8-BWAR6-Y0Z7F-JT10A-7EWD9-33265
License activated!
  Key:          G98S8BWAR6Y0Z7FJT10A7EWD933265
  ActivationID: xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx
  LicenseType:  perpetual_fixed
  IssuedAt:     1700000000
  ExpiresAt:    4102444800
  InGracePeriod:0
```

The hardcoded `SDK_TEST_APP_KEY` in `examples/simple.c` targets the live API.  
Edit it to point at `pk_test_…` for development.

---

## Running the tools

Both tools connect to a running LicenseLatte server.

### sdktest

Full integration test: activate → offline check → polling loop.

```sh
# Defaults (local server)
./build/sdktest

# Custom app ID and license key
./build/sdktest pk_test_XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX XXXXX-XXXXX-XXXXX-XXXXX-XXXXX-XXXXX
```

```
→ Activating…
  [Activate]
  Key:           AHAK85T628WZ639CMVHF8TNDDX260Z
  ActivationID:  xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx
  ProjectID:     yyyyyyyy-yyyy-yyyy-yyyy-yyyyyyyyyyyy
  IssuedAt:      2024-01-01T00:00:00Z
  ExpiresAt:     2025-01-01T00:00:00Z
  GracePeriod:   604800s
  InGracePeriod: false
  LicenseType:   perpetual

→ Checking stored token (offline)…
  [Check]
  ...

→ Polling every 15 s (Ctrl-C to stop)…
  OK — expires 2025-01-01T00:00:00Z
```

### validator

Low-level debug tool: activate → verify JWT claims → renew. Useful for inspecting raw server token contents and confirming the full cert-chain walk.

```sh
# Defaults (local server)
./build/validator

# Custom arguments: app_id  license_key  machine_id
./build/validator pk_test_AHAK85389VQYXYB6S4BW66SKE53TWVTS \
                  AHAK856T8PQS0245KDB1FEC9VXA998 \
                  test-machine-001
```

```
Sanitized key: AHAK856T8PQS0245KDB1FEC9VXA998

→ Activating…
Token: eyJhbGciOiJFZERTQSIsInR5cCI6IkpXVCJ9...

→ Validating…
Key:           AHAK856T8PQS0245KDB1FEC9VXA998
ActivationID:  xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx
ProjectID:     yyyyyyyy-yyyy-yyyy-yyyy-yyyyyyyyyyyy
ExpiresAt:     2025-01-01T00:00:00Z
GracePeriod:   604800sec

→ Renewing…
Renewed token valid — ExpiresAt: 2025-01-01T00:00:00Z
```

---

## API reference

Include the single public header:

```c
#include <latte/latte.h>
```

### latte_new

```c
typedef struct latte_config latte_config; /* opaque, built with a builder */

latte_config *latte_config_new(const char *app_id);
latte_config *latte_config_set_multi_instance(latte_config *cfg, int multi_instance);
void          latte_config_free(latte_config *cfg);

latte_status latte_new(const latte_config *config, latte_sdk **out);
```

Creates and returns a new SDK handle. Call once at application startup and reuse the returned pointer for the lifetime of the process.

`latte_config` is built with `latte_config_new()` and configured with `latte_config_set_*` setters, which return the config pointer so calls can be chained. It's opaque so new options can be added later without breaking existing callers or the SDK's binary interface. `latte_new()` does not retain the config — free it with `latte_config_free()` any time after the call, even immediately.

`app_id` is the project key shown in the LicenseLatte dashboard, format `pk_{env}_{32-char}`.

```c
latte_config *cfg = latte_config_new("pk_live_XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX");
latte_sdk *sdk = NULL;
latte_status st = latte_new(cfg, &sdk);
latte_config_free(cfg);
```

**Errors:**

| Code                                  | Meaning                                             |
| -------------------------------------- | ----------------------------------------------------- |
| `LATTE_ERR_INVALID_CONFIG`            | `config` or its `app_id` is `NULL`                  |
| `LATTE_ERR_INVALID_APPID`             | Malformed app ID (wrong prefix or structure)        |
| `LATTE_ERR_UNKNOWN_ENVIRONMENT`       | Environment field is not `live`, `test`, or `local` |
| `LATTE_ERR_INVALID_APPID_KEY_SEGMENT` | 32-char segment has the wrong length                |
| `LATTE_ERR_INVALID_APPID_CHECKSUM`    | Built-in checksum mismatch — likely a typo          |
| `LATTE_ERR_STORAGE_INIT_FAILED`       | Token directory could not be created                |
| `LATTE_ERR_MACHINE_ID_FAILED`         | OS machine UUID is unavailable                      |

#### Multiple licensed instances of the same app

By default, one machine has a single cached token per `app_id`: only one license can be active for that app on the machine at a time, stored in the shared OS config directory. Call `latte_config_set_multi_instance(cfg, 1)` to let several independently-licensed copies of the *same* `app_id` run side by side — e.g. several portable installs of the same app, each in its own directory, each activated with a different license.

```c
latte_config *cfg = latte_config_set_multi_instance(latte_config_new("pk_live_..."), 1);
```

The working directory itself is the instance boundary: the token is stored at `.licenselatte/{app_key}.latte` relative to the CWD instead of the shared OS config directory, which is never read or written in this mode. Give each instance its own directory (e.g. one per portable install) and they stay independent automatically — no id to generate or manage.


### latte_activate

```c
latte_status latte_activate(latte_sdk *sdk, const char *key, latte_license **out);
```

Validates the license key and activates this machine.

**Fast path (cache hit):** if a valid token is already stored locally, returns in microseconds without a network call. A background thread silently renews the token so the cache stays fresh.

**Slow path (cache miss or expired token):** calls `POST /v1/activate` and caches the result.

`key` is normalised automatically — hyphens, spaces, lowercase, and the ambiguous characters `O/0`, `I/L/1` are all handled.

On `LATTE_OK`, `*out` is heap-allocated. The caller must free it with `latte_license_free()`.

**Errors:**

| Code                             | Meaning                                        |
| -------------------------------- | ---------------------------------------------- |
| `LATTE_ERR_INVALID_KEY`          | Key fails format or checksum validation        |
| `LATTE_ERR_LICENSE_EXPIRED`      | License is past its expiry or grace period     |
| `LATTE_ERR_SEAT_LIMIT`           | All activation slots are occupied              |
| `LATTE_ERR_LICENSE_NOT_FOUND`    | Key is valid but not registered on the server  |
| `LATTE_ERR_INVALID_PROJECT_KEY`  | The app ID is not recognised by the server     |
| `LATTE_ERR_NETWORK`              | Could not reach the API                        |
| `LATTE_ERR_SERVER_INVALID_TOKEN` | Server response failed cert-chain verification |

### latte_check

```c
latte_status latte_check(latte_sdk *sdk, latte_license **out);
```

Validates the locally-stored token **without a network call**. Use this on every startup and for periodic in-process gates (e.g. before allowing a premium feature).

Returns `LATTE_ERR_NOT_ACTIVATED` if `latte_activate` has never been called on this machine.  
Returns `LATTE_ERR_LICENSE_EXPIRED` if the token's grace period has elapsed.

A background renewal thread is triggered automatically when the token is old enough (≥ 60 min since issuance).

### latte_license_free / latte_free

```c
void latte_license_free(latte_license *lic);
void latte_free(latte_sdk *sdk);
```

`latte_license_free` releases a `latte_license` returned by `latte_activate` or `latte_check`.

`latte_free` releases the SDK handle. It **waits** for any in-flight renewal thread to finish before returning — it is safe to call immediately after your application is done.

### latte_strerror

```c
const char *latte_strerror(latte_status s);
```

Returns a human-readable, statically-allocated string for a status code.

---

## The latte_license struct

```c
typedef struct {
    char    *key;                  /* raw license key, no hyphens */
    char    *activation_id;        /* server UUID for this machine's slot */
    char    *project_id;           /* UUID of the owning project */
    char    *license_type;         /* "perpetual_fixed" | "perpetual" | "expiring" */
    int64_t  issued_at;            /* Unix timestamp — when the token was issued */
    int64_t  expires_at;           /* Unix timestamp — hard expiry (year 2099 for perpetual_fixed) */
    int64_t  grace_period_seconds; /* offline tolerance window from issued_at */
    int      in_grace_period;      /* 1 → offline > 60 min; show "please reconnect" */
    size_t   metadata_count;       /* number of custom metadata key-value pairs */
    latte_kv *metadata;            /* array of {char *key, char *value} */
} latte_license;
```

`in_grace_period` is `1` once the device has been offline longer than 60 minutes but the full grace window has not elapsed yet. Surface this to the user as a "please reconnect soon" warning.

---

## C++ wrapper

A header-only RAII wrapper is available at `include/latte/latte.hpp` for C++ consumers. It wraps `latte_config` / `latte_sdk` / `latte_license` in move-only classes that free their underlying C resource automatically (destructors call `latte_config_free` / `latte_free` / `latte_license_free`), and it turns `latte_status` failures into a `latte::Error` exception instead of a return code.

```cpp
#include <iostream>
#include <latte/latte.hpp>

int main()
{
    try {
        latte::Config cfg("pk_live_XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX");
        latte::Sdk sdk(cfg);

        latte::License lic = sdk.activate("XXXXX-XXXXX-XXXXX-XXXXX-XXXXX-XXXXX");
        std::cout << "activated, license type: " << lic.license_type() << "\n";

        for (const auto &[key, value] : lic.metadata())
            std::cout << key << " = " << value << "\n";

        lic = sdk.check();
        if (lic.in_grace_period())
            std::cerr << "warning: offline too long, please reconnect soon\n";
    } catch (const latte::Error &e) {
        std::cerr << "error: " << e.what() << " (status " << e.status() << ")\n";
        return 1;
    }
    return 0;
}
```

| Class            | Wraps          | Notes                                                                 |
| ----------------- | -------------- | ---------------------------------------------------------------------- |
| `latte::Config`    | `latte_config` | Constructor takes `app_id`; `set_multi_instance(bool)` chains          |
| `latte::Sdk`       | `latte_sdk`    | Constructor throws `latte::Error`; `activate()` / `check()` return `License` |
| `latte::License`   | `latte_license`| Accessors (`key()`, `license_type()`, `metadata()`, …) instead of struct fields |
| `latte::Error`     | `latte_status` | `std::runtime_error`; `.status()` returns the original `latte_status`  |

All three RAII classes are move-only (no copy constructor/assignment), matching the single-owner semantics of the underlying C handles. `include/latte/latte.h` remains usable directly from C++ (it's wrapped in `extern "C"`) if you don't want the wrapper.

Build the C++ example with the `example_simple_cpp` CMake target (mirrors `example_simple`, ported to use the wrapper).

---

## Error codes

```c
typedef enum {
    LATTE_OK = 0,

    /* Activation / Check */
    LATTE_ERR_INVALID_KEY,
    LATTE_ERR_LICENSE_EXPIRED,
    LATTE_ERR_NOT_ACTIVATED,
    LATTE_ERR_SEAT_LIMIT,
    LATTE_ERR_LICENSE_NOT_FOUND,
    LATTE_ERR_INVALID_PROJECT_KEY,

    /* SDK initialisation (latte_new) */
    LATTE_ERR_INVALID_CONFIG,
    LATTE_ERR_INVALID_APPID,
    LATTE_ERR_UNKNOWN_ENVIRONMENT,
    LATTE_ERR_INVALID_APPID_KEY_SEGMENT,
    LATTE_ERR_INVALID_APPID_CHECKSUM,
    LATTE_ERR_STORAGE_INIT_FAILED,
    LATTE_ERR_MACHINE_ID_FAILED,

    /* Runtime */
    LATTE_ERR_NETWORK,
    LATTE_ERR_SERVER_INVALID_TOKEN,
    LATTE_ERR_INTERNAL
} latte_status;
```

---

## Quick start

```c
#include <stdio.h>
#include <latte/latte.h>

int main(void)
{
    /* 1. Create one SDK instance per process. */
    latte_sdk *sdk = NULL;
    latte_config *cfg = latte_config_new("pk_live_XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX");
    latte_status st = latte_new(cfg, &sdk);
    latte_config_free(cfg);
    if (st != LATTE_OK) {
        fprintf(stderr, "SDK init failed: %s\n", latte_strerror(st));
        return 1;
    }

    /* 2. Activate the key entered by the user.
     *    Returns from cache immediately if already activated on this machine. */
    latte_license *lic = NULL;
    st = latte_activate(sdk, "XXXXX-XXXXX-XXXXX-XXXXX-XXXXX-XXXXX", &lic);
    if (st != LATTE_OK) {
        switch (st) {
        case LATTE_ERR_INVALID_KEY:    fprintf(stderr, "invalid key\n");    break;
        case LATTE_ERR_LICENSE_EXPIRED:fprintf(stderr, "license expired\n");break;
        case LATTE_ERR_SEAT_LIMIT:     fprintf(stderr, "seat limit reached\n"); break;
        default: fprintf(stderr, "error: %s\n", latte_strerror(st)); break;
        }
        latte_free(sdk);
        return 1;
    }
    printf("activated, license type: %s\n", lic->license_type);
    latte_license_free(lic);

    /* 3. On subsequent startups, check the cached token — no network call. */
    st = latte_check(sdk, &lic);
    if (st != LATTE_OK) {
        fprintf(stderr, "check failed: %s\n", latte_strerror(st));
        latte_free(sdk);
        return 1;
    }
    if (lic->in_grace_period)
        fprintf(stderr, "warning: offline too long, please reconnect soon\n");

    /* ... run your application ... */

    latte_license_free(lic);
    latte_free(sdk);
    return 0;
}
```

---

## License types

| Type              | Expiry            | Renewal               | Revocable |
| ----------------- | ----------------- | --------------------- | --------- |
| `perpetual_fixed` | Never (year 2099) | Never                 | No        |
| `perpetual`       | Never             | Periodic (background) | Yes       |
| `expiring`        | Set by policy     | Periodic (background) | Yes       |

**`perpetual_fixed`** tokens are irrevocable one-time activations. The server signs a token that expires in 2099; the SDK never contacts the API again after the first activation. There is no grace period — the token is simply valid until 2099.

**`perpetual`** and **`expiring`** licenses use a rolling-renewal model. The SDK renews the cached token in the background every 5–60 minutes. The grace period gives the device a buffer to operate offline if renewal fails.

---

## Offline grace period

The grace period (`grace_period_seconds`) is an offline tolerance window measured **from the last token issuance** (`issued_at`), not from the expiry date. While `now ≤ issued_at + grace_period_seconds`, the device may operate without a network connection.

```
issued_at ──────────────────────────────────> expires_at
              |                   |
              └── grace_period ───┘
                  ^ offline window
```

Two cases result in expiry:

1. **Collision** — `issued_at + grace_period > expires_at`: the grace window extends past the license hard expiry. The SDK treats this as expired immediately.
2. **Offline too long** — `now > issued_at + grace_period`: the device has been offline longer than the window. The user must reconnect to receive a fresh token.

`lic->in_grace_period` is `1` once the device has been offline longer than 60 minutes but the deadline has not passed. Use it to surface a "please reconnect" banner.

---

## Token storage

Tokens are stored as a JSON record in a per-OS directory:

| OS      | Path                                                        |
| ------- | ----------------------------------------------------------- |
| macOS   | `~/Library/Application Support/LicenseLatte/{appkey}.latte` |
| Linux   | `~/.config/LicenseLatte/{appkey}.latte`                     |
| Windows | `%AppData%\LicenseLatte\{appkey}.latte`                     |

If the OS config directory is unavailable the SDK falls back to `.licenselatte/{appkey}.latte` relative to the working directory.

Each project has its own file (keyed by the 32-char project key segment), so multiple products on the same machine do not interfere with each other.

When `multi_instance` is set via `latte_config_set_multi_instance()`, the OS-wide directory above is bypassed entirely: the token always lives at `.licenselatte/{appkey}.latte` relative to the working directory, so several independently-licensed instances of the same `app_id` can coexist on one machine, one per directory. See [Multiple licensed instances of the same app](#multiple-licensed-instances-of-the-same-app).

The storage format is compatible with the `latte-go` SDK — tokens activated by one SDK can be read by the other.

---

## Background renewal

When `latte_activate` or `latte_check` returns a valid cached token, the SDK may fire a background `pthread` that calls `POST /v1/renew` and overwrites the stored token with the fresh one.

Renewal happens after the token is older than 60 minutes (randomised jitter between 5 and 60 minutes at the SDK level). Renewal errors are silently ignored — the existing token remains valid until its grace period elapses.

If the server returns an invalid-license response during renewal (e.g. the license was revoked), the stored token is wiped so the next `latte_check` returns `LATTE_ERR_NOT_ACTIVATED`.

`perpetual_fixed` licenses skip renewal entirely: the token is permanent.

`latte_free` waits for any in-flight renewal thread before returning, so there is no risk of use-after-free.

---

## Custom metadata

When issuing a license from the dashboard you can attach arbitrary metadata (e.g. customer name, plan tier, feature flags). These values are embedded in the JWT under the `pmd` claim and exposed via `latte_license.metadata`:

```c
latte_license *lic = NULL;
latte_activate(sdk, key, &lic);

for (size_t i = 0; i < lic->metadata_count; i++) {
    printf("%s = %s\n", lic->metadata[i].key, lic->metadata[i].value);
}
```

Metadata values are always strings. They are signed by the server and cannot be tampered with by the client.

---

## Environments

The `app_id` prefix determines which API endpoint the SDK uses:

| Prefix     | Endpoint                            | Purpose      |
| ---------- | ------------------------------------ | ------------ |
| `pk_live_` | `https://api.licenselatte.com`      | Production   |
| `pk_test_` | `https://test.api.licenselatte.com` | Sandbox / CI |

---

## Cross-SDK compatibility

`latte-c` and `latte-go` are wire-compatible:

- **Same on-disk format** — the `.latte` JSON record is identical between SDKs. A token cached by one can be read by the other.
- **Same machine fingerprint** — the HMAC-SHA256 machine ID (`denisbrodbeck/machineid` algorithm) is replicated exactly, so server-issued tokens bind to the same machine identity regardless of which SDK activated them.
- **Same cert chain** — both SDKs walk the same master → submaster → project → daily → JWT hierarchy.

To verify cross-SDK parity, activate with `latte-go`'s `cmd/sdktest` then run `latte-c`'s `build/sdktest` (or vice versa) using the same `AppID` — both should read and accept the cached `.latte` file.

---

## Architecture

```
include/latte/latte.h          Public C API
include/latte/latte.hpp        Header-only C++ RAII wrapper (Config / Sdk / License / Error)
src/
  latte.c                      SDK: New / Activate / Check + renewal orchestration
  appid.c                      AppID parsing, storage path resolution
  domain.{c,h}                 Internal license + cert_chain structs
  constants.h                  URLs, keys, timing constants
  errors.{c,h}                 Status codes and port-level error mapping
  machineid.c                  Platform machine UUID + HMAC-SHA256 protect()
  thread.{c,h}                 pthreads / Win32 wrapper for renewal thread
  validate.{c,h}               Grace-period and expiry rules
  verify.{c,h}                 4-step Ed25519 cert-chain verification
  crypto/
    key.{c,h}                  Crockford-style key checksum and sanitise
    base64url.{c,h}            RFC 4648 §5 decoder
    jwt.{c,h}                  EdDSA JWT parse + verify
    cert.{c,h}                 verify_cert / pubkey_from_cert helpers
  http/client.{c,h}            libcurl POST activate / renew
  storage/
    file.{c,h}                 JSON token file (+ legacy timestamp:token)
    mem.{c,h}                  In-memory storage for unit tests
vendor/cjson/                  Vendored cJSON (MIT)
examples/simple.c              Minimal activate → check example (C API)
examples/simple.cpp            Same example, ported to the C++ wrapper
tools/sdktest.c                Integration test: activate → check → poll
tools/validator.c              Debug: activate → print JWT claims → renew
tests/
  test_key.c                   Key checksum + sanitise
  test_storage.c               Storage round-trip + legacy format
  test_validate.c              Grace/expiry rule table tests
  test_jwt.c                   base64url + EdDSA JWT verify
```

---

## License

MIT, see [LICENSE](LICENSE).
