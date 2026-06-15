#ifndef LATTE_CONSTANTS_H
#define LATTE_CONSTANTS_H

/* Master Ed25519 public key (hex) — hardcoded, never changes. */
#define LATTE_MASTER_PUBKEY_HEX \
    "6773cdfdfb7fc44f13f097449b715e7147a2d73f525d9f09a8d25229e458a2fb"

/* API base URLs per environment */
#define LATTE_URL_LIVE  "https://api.licenselatte.com"
#define LATTE_URL_TEST  "https://test.api.licenselatte.com"
#define LATTE_URL_LOCAL "http://localhost:8080"

/* JWT issuer claim value */
#define LATTE_ISSUER "licenselatte"

/* Token renewal timing (seconds) */
#define LATTE_MIN_RENEWAL_SECS   (5  * 60)   /*  5 minutes */
#define LATTE_MAX_RENEWAL_SECS   (60 * 60)   /* 60 minutes */

/* Grace-period collision guard (seconds) */
#define LATTE_MAX_GRACE_SECS     (90 * 24 * 3600)  /* 90 days */
#define LATTE_MAX_AGE_SECS       (365 * 24 * 3600) /* 1 year  */

/* License type strings */
#define LATTE_TYPE_PERPETUAL_FIXED "perpetual_fixed"
#define LATTE_TYPE_PERPETUAL       "perpetual"
#define LATTE_TYPE_EXPIRING        "expiring"

/* HTTP timeout for activate/renew calls (seconds) */
#define LATTE_HTTP_TIMEOUT_SECS  10L

/* Ed25519 public key size in bytes */
#define LATTE_ED25519_PUBKEY_SIZE 32

#endif /* LATTE_CONSTANTS_H */
