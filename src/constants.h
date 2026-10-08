#ifndef LATTE_CONSTANTS_H
#define LATTE_CONSTANTS_H

/*
 * Trusted master Ed25519 public keys (hex). A submaster cert signed by any
 * one of them is accepted. Each root lives on its own YubiKey; the hex is the
 * card's authentication key.
 */
#define LATTE_MASTER_PUBKEYS_HEX { \
    /* Original root, YubiKey 32493801, fingerprint                       \
     * 49C1CA77D17984E0D25C0994D626409AD567D479. Kept until the submaster  \
     * cert it signed expires on 2026-12-11. */                           \
    "6773cdfdfb7fc44f13f097449b715e7147a2d73f525d9f09a8d25229e458a2fb",   \
    /* root-a, YubiKey 40127477, primary                                  \
     * B89594D5DD7B9213E5FC7DC27FC9430452C9F38D, auth subkey              \
     * A53549AF5B5570F304D4342D702AEFFF07B512EF */                        \
    "73358e45a5c77b7f7236d26f5a1756011b522d277bb19ac4869d2e88952705cd",   \
    /* root-b, YubiKey 40127498, primary                                  \
     * A980BA5DD0B3831A4038D22C744D413D801A1AFA, auth subkey              \
     * B217F9743D6FD3EB21FF87D83752947C4AD9CC5E */                        \
    "4f9369b8a4a0fd9be67cd403e4ed92e0d45609772659be4a066c1a9c4eff43fe",   \
    /* root-c, YubiKey 40127484, primary                                  \
     * 7F3CC680FF472FCF9A351CAF8204C38EAF10DA14, auth subkey              \
     * 59282CA469B7E8168952E07476352E58361B3A92 */                        \
    "46d45bbc3280df763fcaf7a37055888eeefbb5781784c1f471f4f413d72b123f",   \
}
#define LATTE_MASTER_PUBKEY_COUNT 4

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

/* expires_at of a licence with no end date: 2099-01-01T00:00:00Z, the exp a
 * grc-format token carries for a perpetual licence. A lex-format token
 * without `lex` is given the same value. */
#define LATTE_NO_EXPIRY          INT64_C(4070908800)

/* License type strings */
#define LATTE_TYPE_PERPETUAL       "perpetual"
#define LATTE_TYPE_EXPIRING        "expiring"

/* HTTP timeout for activate/renew calls (seconds) */
#define LATTE_HTTP_TIMEOUT_SECS  10L

/* Ed25519 public key size in bytes */
#define LATTE_ED25519_PUBKEY_SIZE 32

#endif /* LATTE_CONSTANTS_H */
