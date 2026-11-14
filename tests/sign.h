/*
 * Test-only Ed25519 keys, signatures and encodings, on Monocypher.
 */
#ifndef LATTE_TEST_SIGN_H
#define LATTE_TEST_SIGN_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "monocypher-ed25519.h"

typedef struct { unsigned char pk[32], sk[64]; } keypair;

/* A fresh key pair. Seeds come from a counter: distinct, and the same run to run. */
static inline keypair gen(void)
{
    static unsigned counter;
    unsigned char seed[32] = {0};
    keypair k;
    counter++;
    memcpy(seed, &counter, sizeof(counter));
    crypto_ed25519_key_pair(k.sk, k.pk, seed);
    return k;
}

/* Unpadded base64url of in, heap-allocated. */
static inline char *b64url(const unsigned char *in, size_t len)
{
    static const char abc[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    char *out = (char *)malloc(len / 3 * 4 + 4);
    size_t j = 0;
    if (!out) return NULL;
    for (size_t i = 0; i < len; i += 3) {
        unsigned v = (unsigned)in[i] << 16 | (i + 1 < len ? (unsigned)in[i + 1] << 8 : 0) |
                     (i + 2 < len ? in[i + 2] : 0);
        out[j++] = abc[v >> 18];
        out[j++] = abc[(v >> 12) & 63];
        if (i + 1 < len) out[j++] = abc[(v >> 6) & 63];
        if (i + 2 < len) out[j++] = abc[v & 63];
    }
    out[j] = '\0';
    return out;
}

static inline void hex32(const unsigned char in[32], char out[65])
{
    for (int i = 0; i < 32; i++) snprintf(out + 2 * i, 3, "%02x", in[i]);
}

#endif /* LATTE_TEST_SIGN_H */
