#ifndef LATTE_HMAC_SHA256_H
#define LATTE_HMAC_SHA256_H

#include <stddef.h>

/* HMAC-SHA256 (RFC 2104, FIPS 180-4) of msg under key. */
void ll_hmac_sha256(const unsigned char *key, size_t key_len,
                    const unsigned char *msg, size_t msg_len,
                    unsigned char mac[32]);

#endif /* LATTE_HMAC_SHA256_H */
