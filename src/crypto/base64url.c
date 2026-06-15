#include "base64url.h"
#include <stdlib.h>
#include <string.h>

static const signed char DEC[256] = {
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1, /* - at 45 */
    52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,
    -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
    15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,63, /* _ at 95 */
    -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
    41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
};

int ll_base64url_decode(const char *in, size_t in_len,
                        unsigned char **out_data, size_t *out_len)
{
    /* Strip trailing '=' padding */
    while (in_len > 0 && in[in_len - 1] == '=')
        in_len--;

    size_t full_groups = in_len / 4;
    size_t remainder   = in_len % 4;

    /* remainder 1 is invalid */
    if (remainder == 1) return -1;

    size_t decoded_len = full_groups * 3;
    if (remainder == 2) decoded_len += 1;
    else if (remainder == 3) decoded_len += 2;

    unsigned char *buf = (unsigned char *)malloc(decoded_len + 1);
    if (!buf) return -1;

    size_t out_i = 0;
    size_t i = 0;
    for (; i + 4 <= in_len; i += 4) {
        int b0 = DEC[(unsigned char)in[i]];
        int b1 = DEC[(unsigned char)in[i+1]];
        int b2 = DEC[(unsigned char)in[i+2]];
        int b3 = DEC[(unsigned char)in[i+3]];
        if (b0 < 0 || b1 < 0 || b2 < 0 || b3 < 0) { free(buf); return -1; }
        buf[out_i++] = (unsigned char)((b0 << 2) | (b1 >> 4));
        buf[out_i++] = (unsigned char)((b1 << 4) | (b2 >> 2));
        buf[out_i++] = (unsigned char)((b2 << 6) | b3);
    }

    if (remainder == 2) {
        int b0 = DEC[(unsigned char)in[i]];
        int b1 = DEC[(unsigned char)in[i+1]];
        if (b0 < 0 || b1 < 0) { free(buf); return -1; }
        buf[out_i++] = (unsigned char)((b0 << 2) | (b1 >> 4));
    } else if (remainder == 3) {
        int b0 = DEC[(unsigned char)in[i]];
        int b1 = DEC[(unsigned char)in[i+1]];
        int b2 = DEC[(unsigned char)in[i+2]];
        if (b0 < 0 || b1 < 0 || b2 < 0) { free(buf); return -1; }
        buf[out_i++] = (unsigned char)((b0 << 2) | (b1 >> 4));
        buf[out_i++] = (unsigned char)((b1 << 4) | (b2 >> 2));
    }

    buf[out_i] = '\0';
    *out_data = buf;
    *out_len  = out_i;
    return 0;
}
