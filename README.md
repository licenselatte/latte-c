# latte-c

C library SDK for [LicenseLatte](https://licenselatte.com) — a port of the official [`latte-go`](https://github.com/licenselatte/latte-go) SDK with exact behavioral parity.

Full docs: [licenselatte.com/docs/sdks/c](https://licenselatte.com/docs/sdks/c) (C) · [licenselatte.com/docs/sdks/cpp](https://licenselatte.com/docs/sdks/cpp) (C++) · [licenselatte.com/docs/sdks/d](https://licenselatte.com/docs/sdks/d) (D)

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
    - [latte_deactivate](#latte_deactivate)
    - [latte_machines / latte_rename_machine](#latte_machines--latte_rename_machine)
    - [latte_can / latte_limit / latte_has_entitlements](#latte_can--latte_limit--latte_has_entitlements)
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
- [Entitlements](#entitlements)
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

The shared cross-language fixtures live in `testdata/`, a submodule of
[latte-testvectors](https://github.com/licenselatte/latte-testvectors). Fetch
it before the first build, or `test_fixtures` will report an empty vector
directory:

```sh
git submodule update --init
```

```sh
ctest --test-dir build --output-on-failure
```

Expected output:

```
100% tests passed, 0 tests failed out of 6

Total Test time (real) =   0.43 sec
```

Individual test binaries can be run directly for verbose output:

```sh
./build/test_key           # key checksum and sanitise logic
./build/test_storage       # JSON record round-trip and legacy format
./build/test_validate      # grace-period / expiry rules
./build/test_jwt           # base64url decode and EdDSA JWT verify
./build/test_entitlements  # entitlement decoding and the can/limit accessors
./build/test_fixtures      # the 29 shared latte-testvectors fixtures
./build/test_machine_id    # testdata/machine_id.json through the shared hash (run from the source dir)
./build/test_machines      # deactivate / machines / rename against a local HTTP stub (POSIX only)
```

`test_fixtures` is the cross-language parity check: it runs the identical
JSON vectors that latte-go, latte-rs, latte-py and latte-js run, and must
agree with them on accept/reject, failure stage, failure reason, grace-period
flag and entitlements for every one. It runs from the source directory (not
`build/`) so the fixture paths resolve; `ctest` handles that for you.

---

## Running the example

The example reads a license key from stdin, calls `latte_activate`, prints the returned `latte_license` and this licence's machines, then offers to release this machine with `latte_deactivate`.

```sh
./build/example_simple
```

```
Type your license key (e.g. XXXXX-XXXXX-XXXXX-XXXXX-XXXXXX):
> G98S8-BWAR6-Y0Z7F-JT10A-7EWD9-33265
License activated!
  Key:          G98S8BWAR6Y0Z7FJT10A7EWD933265
  ActivationID: xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx
  LicenseType:  perpetual
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
latte_config *latte_config_set_machine_id(latte_config *cfg, const char *raw_machine_id);
latte_config *latte_config_set_machine_name(latte_config *cfg, const char *name);
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
| `LATTE_ERR_MACHINE_ID_FAILED`         | OS machine UUID is unavailable (and none was set)   |

#### Multiple licensed instances of the same app

By default, one machine has a single cached token per `app_id`: only one license can be active for that app on the machine at a time, stored in the shared OS config directory. Call `latte_config_set_multi_instance(cfg, 1)` to let several independently-licensed copies of the *same* `app_id` run side by side — e.g. several portable installs of the same app, each in its own directory, each activated with a different license.

```c
latte_config *cfg = latte_config_set_multi_instance(latte_config_new("pk_live_..."), 1);
```

The working directory itself is the instance boundary: the token is stored at `.licenselatte/{app_key}.latte` relative to the CWD instead of the shared OS config directory, which is never read or written in this mode. Give each instance its own directory (e.g. one per portable install) and they stay independent automatically — no id to generate or manage.

#### Machine ID

A licence seat is bound to a `machine_id`. The SDK never sends the raw machine ID: it sends

```
machine_id = lowercase hex HMAC-SHA256(key = raw machine ID, message = "licenselatte_" + app_id)
```

which is `denisbrodbeck/machineid`'s `ProtectedID`, so every LicenseLatte SDK derives the same value for the same raw ID and app. By default the raw ID is read from the OS: `IOPlatformUUID` on macOS, `/var/lib/dbus/machine-id` or `/etc/machine-id` on Linux, `MachineGuid` on Windows.

Call `latte_config_set_machine_id(cfg, raw)` to supply the raw ID yourself. It goes through the same hash, byte for byte with no trimming, and still never leaves the machine. Set it when the OS ID does not identify the seat you mean to license:

- containers that share an image's `/etc/machine-id`, or have none at all;
- cloned VMs that all carry the same ID;
- a seat per user rather than per machine (pass a stable user ID);
- tests that need a deterministic machine.

```c
latte_config *cfg = latte_config_set_machine_id(latte_config_new("pk_live_..."), "user-8412");
```

`NULL` or `""` means unset, so the OS ID is used and behaviour is the default. Keep the value stable: changing it on an activated install makes that install a new machine, which takes another seat. If the copy cannot be allocated, `latte_new()` returns `LATTE_ERR_INTERNAL` instead of falling back to the OS ID. The shared vectors in `testdata/machine_id.json` pin the hash.

#### Machine name

A machine is given a name when it activates, shown to the customer by [`latte_machines`](#latte_machines--latte_rename_machine) and in the seller's customer portal, and to you in the dashboard. By default the SDK sends the OS hostname (`gethostname`, or `GetComputerNameA` on Windows), and no name when the hostname cannot be read.

```c
latte_config *cfg = latte_config_set_machine_name(latte_config_new("pk_live_..."), "Studio Mac");
```

`""` sends no name at all; `NULL` restores the hostname default. The server stores the name only when the activation is created and silently drops an invalid one, and a name the customer sets later with `latte_rename_machine()` is kept on re-activation. If the copy cannot be allocated, `latte_new()` returns `LATTE_ERR_INTERNAL`.


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

A background renewal thread is triggered automatically when the token is old enough (≥ 60 min since issuance). That includes a token that is past its grace window.

### latte_deactivate

```c
typedef struct {
    int     released;      /* 0 when it was already released; still success */
    int64_t seat_free_at;  /* set when the seller's policy holds the seat until then, else 0 */

    /* Filled on LATTE_ERR_DEACTIVATION_BLOCKED only */
    char    reason[64];    /* "deactivation_cooldown" | "deactivation_limit_reached" */
    char    message[512];  /* written to be shown to the user */
    int64_t available_at;  /* when a release becomes possible, 0 when not known */
} latte_deactivate_result;

latte_status latte_deactivate(latte_sdk *sdk, latte_deactivate_result *out);
```

Releases this machine's seat so the licence can be activated on another machine: the "Deactivate" button in your app. It **needs the network**. The SDK asks the server to release the seat first and deletes the cached token only once the server has answered, so `latte_check` returns `LATTE_ERR_NOT_ACTIVATED` afterwards until the next `latte_activate`. If the call fails, nothing changes locally and the user can try again; the seat is never lost to a dropped connection. Repeating a release that already went through is success (`released` is 0). Times are Unix seconds; `out` may be `NULL`.

The activation id and licence key come from the cached token, read without verifying it, so a machine whose token has expired can still release its seat; with no cached token the call returns `LATTE_ERR_NOT_ACTIVATED`. A machine that is lost, dead or wiped cannot call this: its seat is released from the seller's customer portal instead.

Releases are limited on every plan: a fixed number per licence per year, or on plans with the deactivation policy engine the seller's cooldown and yearly cap. A refusal returns `LATTE_ERR_DEACTIVATION_BLOCKED` with `reason`, `message` and `available_at` filled in. The seller may also hold the freed seat until this machine's last token has expired, which `seat_free_at` reports.

```c
latte_deactivate_result res;
latte_status st = latte_deactivate(sdk, &res);
if (st == LATTE_ERR_DEACTIVATION_BLOCKED)
    show_message(res.message);
else if (st != LATTE_OK)
    show_message("Could not reach the licence server. Nothing was changed; try again when online.");
else
    show_message("This machine is released.");
```

### latte_machines / latte_rename_machine

```c
typedef struct {
    char   *name;          /* NULL when the machine has none */
    char   *fingerprint;   /* first 12 characters of the machine id hash */
    int64_t activated_at;
    int64_t last_seen_at;  /* last activation or renewal */
    int64_t seat_free_at;  /* set on a released machine whose seat is still held, else 0 */
    int     current;       /* 1 for this machine */
} latte_machine;

latte_status latte_machines(latte_sdk *sdk, latte_machine **out, size_t *count);
void         latte_machines_free(latte_machine *machines, size_t count);

#define LATTE_MACHINE_NAME_SIZE 257
latte_status latte_rename_machine(latte_sdk *sdk, const char *name,
                                  char *stored, size_t stored_size);
```

`latte_machines` lists the machines holding a seat on this machine's licence, for showing the customer where their licence is in use. It cannot release another machine; that is the customer portal's job. Free the array with `latte_machines_free()`.

`latte_rename_machine` names this machine and writes the name as stored (trimmed) to `stored`, which may be `NULL`; a `LATTE_MACHINE_NAME_SIZE` buffer holds any name. The server refuses a name that is empty after trimming, longer than 64 characters or holds control characters. Both calls need the network and an active activation on this machine (`LATTE_ERR_LICENSE_EXPIRED` otherwise, from the server's `403`).

### latte_can / latte_limit / latte_has_entitlements

```c
int latte_can(const latte_license *lic, const char *key);
int latte_limit(const latte_license *lic, const char *key, int64_t *out);
int latte_has_entitlements(const latte_license *lic);

#define LATTE_UNLIMITED ((int64_t)-1)
```

Read the typed entitlements signed into the licence. `latte_can` returns 1 or
0; `latte_limit` returns 1 and writes through `out` when the key is present
and integer-kinded, 0 otherwise (`out` may be `NULL` to test presence alone).
All three tolerate a `NULL` licence or key and answer "no". See the
[Entitlements](#entitlements) section for the semantics.

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
    char    *license_type;         /* "perpetual" | "expiring" */
    int64_t  issued_at;            /* Unix timestamp — when the token was issued */
    int64_t  expires_at;           /* Unix timestamp of the hard expiry */
    int64_t  grace_period_seconds; /* offline tolerance window from issued_at */
    int      in_grace_period;      /* 1 → a renewal could not reach the server */
    size_t   metadata_count;       /* number of custom metadata key-value pairs */
    latte_kv *metadata;            /* array of {char *key, char *value} */

    /* Entitlements — appended at the end; see the Entitlements section. */
    uint32_t has_entitlements;     /* 1 → the token carried an `ent` claim at all */
    uint32_t entitlement_count;    /* number of decoded entitlements */
    latte_entitlement *entitlements;
} latte_license;
```

New fields are **appended** to this struct, never inserted, so an already
compiled caller keeps working across a minor release: you never allocate or
`sizeof` a `latte_license` — you receive one from `latte_activate` /
`latte_check` and hand it back to `latte_license_free`, so the library owns
the layout.

`in_grace_period` is true when the licence is still valid on cached data, the SDK has tried to renew it and could not reach the server, and the offline grace window has not run out. It is advisory: gate features on the `latte_check` verdict, never on `in_grace_period`. See [Offline grace period](#offline-grace-period) for the states it reports.

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
            std::cerr << "Couldn't reach the licence server. Connect to the internet to keep using the app.\n";
    } catch (const latte::Error &e) {
        std::cerr << "error: " << e.what() << " (status " << e.status() << ")\n";
        return 1;
    }
    return 0;
}
```

| Class            | Wraps          | Notes                                                                 |
| ----------------- | -------------- | ---------------------------------------------------------------------- |
| `latte::Config`    | `latte_config` | Constructor takes `app_id`; `set_multi_instance(bool)`, `set_machine_id(std::string)` and `set_machine_name(std::string)` chain |
| `latte::Sdk`       | `latte_sdk`    | Constructor throws `latte::Error`; `activate()` / `check()` return `License`; `deactivate()` returns `DeactivateResult`, `machines()` a `std::vector<latte::Machine>`, `rename_machine()` the stored name |
| `latte::License`   | `latte_license`| Accessors (`key()`, `license_type()`, `metadata()`, …) instead of struct fields |
| `latte::Error`     | `latte_status` | `std::runtime_error`; `.status()` returns the original `latte_status`  |
| `latte::DeactivationBlocked` | `LATTE_ERR_DEACTIVATION_BLOCKED` | `latte::Error` thrown by `deactivate()`; `.reason()`, `.message()`, `.available_at()` |

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
    LATTE_ERR_INTERNAL,

    /* latte_deactivate */
    LATTE_ERR_DEACTIVATION_BLOCKED
} latte_status;
```

`LATTE_ERR_DEACTIVATION_BLOCKED` is a release limit refusing a release (the licence's releases for the year used up, or the seller's cooldown); the details are in the `latte_deactivate_result` passed to `latte_deactivate`. It is appended after `LATTE_ERR_INTERNAL`, so no existing value changes.

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
        fprintf(stderr, "Couldn't reach the licence server. Connect to the internet to keep using the app.\n");

    /* ... run your application ... */

    latte_license_free(lic);
    latte_free(sdk);
    return 0;
}
```

---

## License types

| Type          | Expiry        | Renewal               | Revocable |
| ------------- | ------------- | --------------------- | --------- |
| `perpetual`   | Never         | Periodic (background) | Yes       |
| `expiring`    | Set by policy | Periodic (background) | Yes       |

**`perpetual`** and **`expiring`** licenses use a rolling-renewal model. The SDK renews the cached token in the background once it is at least 60 minutes old. The grace period gives the device a buffer to operate offline if renewal fails.

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

1. **Licence ended**: `now > expires_at`. `latte_check` returns `LATTE_ERR_LICENSE_EXPIRED`, whatever the offline window says.
2. **Offline too long**: `now > issued_at + grace_period`. The device has been offline longer than the window. The device must reconnect to receive a fresh token; see [Recovering after a long offline spell](#recovering-after-a-long-offline-spell).

### Recovering after a long offline spell

A computer that was off longer than the grace window recovers by itself. The first `latte_check` after launch returns `LATTE_ERR_NOT_ACTIVATED` and starts a background renewal, as long as the cached token's signature chain verifies and it was issued for this machine. When the machine is online the renewal saves a fresh token, and a `latte_check` a few seconds later returns the licence. Hosts should re-check rather than send the user to activate again. If the server rejects the renewal as invalid or inactive the cache is cleared, and if it cannot be reached the cached token is kept and the next attempt comes after the 5-minute window. `in_grace_period` stays `0` for such a token, because `latte_check` returns an error for it.

### Token formats

The SDK reads two activation token formats, told apart by whether the token carries a `grc` claim:

| Claims            | `expires_at`                                   | `grace_period_seconds` |
| ----------------- | ---------------------------------------------- | ---------------------- |
| with `grc`        | `exp`                                          | `grc`                  |
| without `grc`     | `lex`, or 2099-01-01T00:00:00Z when absent     | `exp - iat`            |

In the second format `exp` is the offline deadline itself, already capped at the licence's end, and `lex` is the licence's end date (absent for a perpetual licence). Both formats fill the same two fields, so the rules above apply unchanged, and a perpetual licence reports `expires_at` = `4070908800` in either. A cached token is stored as the signed token and re-read the same way on every load.

### `in_grace_period`

`in_grace_period` is true when the licence is still valid on cached data, the SDK has tried to renew it and could not reach the server, and the offline grace window has not run out.

It is `renewal_failed && 60 min < age < grace_period`, where `age` is the time since `issued_at` and `renewal_failed` is a flag the SDK keeps in memory for the life of the `latte_sdk` handle. The flag is never stored and `latte_check` never makes a network call to compute it, so a check at startup reports `0` until a background renewal has failed. It changes only on a `latte_check` made after the renewal attempt, so re-check after a renewal rather than rely on the startup value.

| State                                                                  | `in_grace_period` | What to tell the user                                                  |
| ---------------------------------------------------------------------- | ----------------- | ---------------------------------------------------------------------- |
| Token under 60 minutes old                                             | `0`               | Nothing.                                                               |
| Token older than 60 minutes, no renewal has failed                     | `0`               | Nothing. The SDK is renewing, or the app is online and has not renewed yet. |
| Renewal could not reach the server, token inside the grace window      | `1`               | "Couldn't reach the licence server. Connect to the internet to keep using the app." |
| Renewal could not reach the server, grace window over                  | n/a               | `latte_check` returns an error: "Reconnect to continue." |
| Server rejected the renewal as invalid or inactive                     | `0`               | The cache is cleared and the next `latte_check` returns `LATTE_ERR_NOT_ACTIVATED`: "Licence deactivated." Never "connect to the internet". |

A renewal answered by the server with a 5xx or 429, or whose token does not verify, is not a connectivity problem: the flag is cleared, the cached token is kept and the next attempt comes after the 5-minute window.

The flag is advisory. Gate features on the `latte_check` verdict, never on `in_grace_period`.

Deactivating a licence in the dashboard takes effect at the next renewal attempt. A rejected renewal clears the local cache, so the following `latte_check` returns `LATTE_ERR_NOT_ACTIVATED`.

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

`latte_deactivate` empties this file once the server has released the seat, and leaves it untouched when the release fails.

---

## Background renewal

When `latte_activate` or `latte_check` returns a valid cached token, the SDK may fire a background `pthread` that calls `POST /v1/renew` and overwrites the stored token with the fresh one.

Renewal is attempted once the token is at least 60 minutes old, and retried no more than every 5 minutes. A token past its grace window is renewed too; see [Recovering after a long offline spell](#recovering-after-a-long-offline-spell). Renewal errors are silently ignored — the existing token remains valid until its grace period elapses. A renewal that could not reach the server sets `in_grace_period`; see [`in_grace_period`](#in_grace_period).

If the server returns an invalid-license response during renewal (e.g. the license was revoked), the stored token is wiped so the next `latte_check` returns `LATTE_ERR_NOT_ACTIVATED`. Deactivating a licence in the dashboard therefore takes effect at the next renewal attempt.

A renewal saves its token only while the cache still holds the activation it renewed, checked under the same lock `latte_deactivate` clears the cache with, so a renewal in flight during a release never writes a token back.

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

## Entitlements

Entitlements are the typed answers a seller signed into a licence about what
their customer bought. Two questions, and only two: *may this customer do X*
(a boolean) and *how many Y do they get* (an integer).

```c
latte_license *lic = NULL;
latte_activate(sdk, key, &lic);

if (latte_can(lic, "export_pdf"))
    enable_pdf_export();

int64_t max_projects;
if (latte_limit(lic, "max_projects", &max_projects) &&
    max_projects != LATTE_UNLIMITED && used >= max_projects) {
    return E_PROJECT_LIMIT;
}
```

You set the values on a policy and override them per licence in the
dashboard; the server resolves the two and signs the result into the
activation token, so `latte_can` and `latte_limit` answer offline with no
network call.

`LATTE_UNLIMITED` is `-1`. `latte_limit` writes it out as-is — compare
against the macro rather than testing for a negative number.

### The rules

| | |
|---|---|
| **Absence denies.** | An unset key is `latte_can() == 0`, `latte_limit() == 0`. |
| **No coercion.** | `latte_can` on an integer is 0 even when it is non-zero. `latte_limit` on a boolean returns 0 rather than writing a 1 or a 0. |
| **Keys are byte-exact.** | `strcmp`, no case folding and no trimming. |
| **A bad value is dropped, never fatal.** | If a value reaches the token that is neither a boolean nor a whole number, that one entry vanishes and the licence stays valid. |

### Rolling this out without switching your own features off

Absence denies, and that has a consequence worth reading twice: **a token
issued before you set any entitlements answers 0 to everything.** Ship
`if (!latte_can(lic, "export_pdf")) hide();` and every customer still holding
a cached token from before the change loses PDF export until they renew.

`latte_has_entitlements` exists for exactly this, and it is not a convenience
accessor:

```c
int enabled = latte_has_entitlements(lic)
    ? latte_can(lic, "export_pdf")
    : legacy_behaviour();   /* this token predates entitlements */
```

The published order is: set the values in the dashboard first, wait one grace
window for the installed base to renew, then ship the release that reads them
behind `latte_has_entitlements`, and drop the fallback once the base has
turned over.

`latte_has_entitlements` reports whether the claim was **present**, including
when it is empty — which is why it is not an `entitlement_count > 0` check.

### Entitlements are not metadata

Entitlements and `metadata` are separate namespaces and never merge. Metadata
is arbitrary display data, filtered per field in the dashboard, and stringly
typed; entitlements are booleans and integers, unfiltered, and exist
precisely to be read on the customer's machine. The same key may appear in
both meaning different things.

Entitlements are a distribution mechanism for a signed answer, not a
tamper-proofing one. If real revenue depends on a feature, re-validate it
server-side.

### From C++

```cpp
latte::License lic = sdk.activate(key);

if (lic.can("export_pdf")) enable_pdf_export();

if (auto max = lic.limit("max_projects"); max && *max != LATTE_UNLIMITED && used >= *max)
    throw ProjectLimitReached{};

if (!lic.has_entitlements()) { /* token predates entitlements */ }
```

---

## Environments

The `app_id` prefix determines which API endpoint the SDK uses:

| Prefix     | Endpoint                            | Purpose      |
| ---------- | ------------------------------------ | ------------ |
| `pk_live_` | `https://api.licenselatte.com`      | Production   |
| `pk_test_` | `https://api.licenselatte.com`      | Sandbox / CI |

---

## Cross-SDK compatibility

`latte-c` and `latte-go` are wire-compatible:

- **Same on-disk format** — the `.latte` JSON record is identical between SDKs. A token cached by one can be read by the other.
- **Same machine fingerprint**: the HMAC-SHA256 machine ID (`denisbrodbeck/machineid` algorithm, see [Machine ID](#machine-id)) is replicated exactly, whether the raw ID comes from the OS or from `latte_config_set_machine_id()`, so server-issued tokens bind to the same machine identity regardless of which SDK activated them.
- **Same cert chain** — both SDKs walk the same master → submaster → project → daily → JWT hierarchy.

To verify cross-SDK parity, activate with `latte-go`'s `cmd/sdktest` then run `latte-c`'s `build/sdktest` (or vice versa) using the same `AppID` — both should read and accept the cached `.latte` file.

---

## Architecture

```
include/latte/latte.h          Public C API
include/latte/latte.hpp        Header-only C++ RAII wrapper (Config / Sdk / License / Error)
src/
  latte.c                      SDK: New / Activate / Check / Deactivate / Machines + renewal orchestration
  appid.c                      AppID parsing, storage path resolution
  domain.{c,h}                 Internal license + cert_chain structs
  constants.h                  URLs, keys, timing constants
  errors.{c,h}                 Status codes and port-level error mapping
  machineid.c                  Platform machine UUID + the shared HMAC-SHA256 hash
  thread.{c,h}                 pthreads / Win32 wrapper for renewal thread
  validate.{c,h}               Grace-period and expiry rules
  verify.{c,h}                 4-step Ed25519 cert-chain verification
  crypto/
    key.{c,h}                  Crockford-style key checksum and sanitise
    base64url.{c,h}            RFC 4648 §5 decoder
    jwt.{c,h}                  EdDSA JWT parse + verify
    cert.{c,h}                 verify_cert / pubkey_from_cert helpers
  http/client.{c,h}            libcurl POST activate / renew / deactivate / machines / rename
  storage/
    file.{c,h}                 JSON token file (+ legacy timestamp:token)
    mem.{c,h}                  In-memory storage for unit tests
vendor/cjson/                  Vendored cJSON (MIT)
examples/simple.c              Minimal activate → machines → deactivate example (C API)
examples/simple.cpp            Same example, ported to the C++ wrapper
tools/sdktest.c                Integration test: activate → check → poll
tools/validator.c              Debug: activate → print JWT claims → renew
tests/
  test_key.c                   Key checksum + sanitise
  test_storage.c               Storage round-trip + legacy format
  test_validate.c              Grace/expiry rule table tests
  test_jwt.c                   base64url + EdDSA JWT verify
  test_machines.c              Deactivate / machines / rename against a local HTTP stub
```

---

## License

MIT, see [LICENSE](LICENSE).
