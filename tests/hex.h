/*
 * Hex decoding for test vectors.
 */
#ifndef LATTE_TEST_HEX_H
#define LATTE_TEST_HEX_H

#include <stdlib.h>
#include <string.h>

static inline int hex_nibble(char c)
{
    return c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10;
}

/* Decodes a well-formed hex string into a new buffer the caller frees; *len is its size. */
static inline unsigned char *hex_dup(const char *hex, size_t *len)
{
    size_t n = strlen(hex) / 2;
    unsigned char *out = (unsigned char *)malloc(n + 1);
    if (!out) abort();
    for (size_t i = 0; i < n; i++)
        out[i] = (unsigned char)(hex_nibble(hex[2 * i]) << 4 | hex_nibble(hex[2 * i + 1]));
    *len = n;
    return out;
}

#endif /* LATTE_TEST_HEX_H */
