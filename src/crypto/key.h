#ifndef LATTE_CRYPTO_KEY_H
#define LATTE_CRYPTO_KEY_H

#include <stddef.h>

/*
 * Crockford-style Luhn checksum over the LicenseLatte key alphabet
 * "0123456789ABCDEFGHJKMNPQRSTVWXYZ".
 *
 * validate_key:  returns 1 if the last luhn_len chars are the correct checksum.
 * sanitize_key:  normalises user input (uppercase, strip - / space, O→0, I→1,
 *                L→1); dst must be at least strlen(src)+1 bytes. This fold is
 *                specific to the native key alphabet (which deliberately
 *                excludes O/I/L) -- use it only where the value is expected
 *                to be a native-format key. Use normalize_key for anything
 *                else.
 * normalize_key: uppercases and strips - / space, with no other
 *                transformation; dst must be at least strlen(src)+1 bytes.
 *                Unlike sanitize_key, this never assumes the input is in the
 *                native key alphabet, so it's safe to use on any license key
 *                string regardless of which system minted it.
 */
int  ll_validate_key(const char *key, int luhn_len);
void ll_sanitize_key(const char *src, char *dst, size_t dst_size);
void ll_normalize_key(const char *src, char *dst, size_t dst_size);

#endif /* LATTE_CRYPTO_KEY_H */
