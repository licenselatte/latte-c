#ifndef LATTE_BASE64URL_H
#define LATTE_BASE64URL_H

#include <stddef.h>

/*
 * Decode a base64url (RFC 4648 §5) string with or without padding.
 * Returns 0 on success; out_data and out_len are set (caller must free out_data).
 * Returns -1 on bad input.
 */
int ll_base64url_decode(const char *in, size_t in_len,
                        unsigned char **out_data, size_t *out_len);

#endif /* LATTE_BASE64URL_H */
