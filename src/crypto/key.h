#ifndef LATTE_CRYPTO_KEY_H
#define LATTE_CRYPTO_KEY_H

#include <stddef.h>

/*
 * Crockford-style Luhn checksum over the LicenseLatte key alphabet
 * "0123456789ABCDEFGHJKMNPQRSTVWXYZ".
 *
 * validate_key: returns 1 if the last luhn_len chars are the correct checksum.
 * sanitize_key: normalises user input in-place (uppercase, strip - / space,
 *               O→0, I→1, L→1); dst must be at least strlen(src)+1 bytes.
 */
int  ll_validate_key(const char *key, int luhn_len);
void ll_sanitize_key(const char *src, char *dst, size_t dst_size);

#endif /* LATTE_CRYPTO_KEY_H */
