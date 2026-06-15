#include "key.h"
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

static const char ALPHABET[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
#define ALPHABET_LEN 32

static int alphabet_index(char c)
{
    const char *p = strchr(ALPHABET, c);
    return p ? (int)(p - ALPHABET) : -1;
}

/* Port of calculateChecksum from crypto/key.go */
static void calculate_checksum(const char *data, int length, char *out)
{
    int sum = 0;
    int n = (int)strlen(data);
    for (int i = 0; i < n; i++) {
        int val = alphabet_index(data[i]);
        if (val < 0) val = 0;
        if (i % 2 == 0) val *= 2;
        sum += val;
    }
    for (int i = 0; i < length; i++) {
        out[i] = ALPHABET[(sum + i * 31) % ALPHABET_LEN];
    }
    out[length] = '\0';
}

int ll_validate_key(const char *key, int luhn_len)
{
    int klen = (int)strlen(key);
    if (klen < luhn_len) return 0;

    /* data part = key without the last luhn_len chars */
    int data_len = klen - luhn_len;
    char *data = (char *)malloc(data_len + 1);
    if (!data) return 0;
    memcpy(data, key, data_len);
    data[data_len] = '\0';

    char expected[8];
    calculate_checksum(data, luhn_len, expected);
    free(data);

    return memcmp(key + data_len, expected, luhn_len) == 0 ? 1 : 0;
}

void ll_sanitize_key(const char *src, char *dst, size_t dst_size)
{
    size_t j = 0;
    for (size_t i = 0; src[i] && j + 1 < dst_size; i++) {
        char c = (char)toupper((unsigned char)src[i]);
        if (c == '-' || c == ' ') continue;
        if (c == 'O') c = '0';
        else if (c == 'I') c = '1';
        else if (c == 'L') c = '1';
        dst[j++] = c;
    }
    dst[j] = '\0';
}
