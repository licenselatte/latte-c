#include "hmac_sha256.h"
#include <stdint.h>
#include <string.h>

typedef struct {
    uint32_t h[8];
    unsigned char buf[64];
    uint64_t len;
} sha256_ctx;

static const uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

#define ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

static void compress(uint32_t h[8], const unsigned char b[64])
{
    uint32_t w[64], s[8];
    for (int i = 0; i < 16; i++)
        w[i] = (uint32_t)b[4 * i] << 24 | (uint32_t)b[4 * i + 1] << 16 |
               (uint32_t)b[4 * i + 2] << 8 | (uint32_t)b[4 * i + 3];
    for (int i = 16; i < 64; i++)
        w[i] = w[i - 16] + w[i - 7] +
               (ROR(w[i - 15], 7) ^ ROR(w[i - 15], 18) ^ (w[i - 15] >> 3)) +
               (ROR(w[i - 2], 17) ^ ROR(w[i - 2], 19) ^ (w[i - 2] >> 10));

    memcpy(s, h, sizeof(s));
    for (int i = 0; i < 64; i++) {
        uint32_t t1 = s[7] + (ROR(s[4], 6) ^ ROR(s[4], 11) ^ ROR(s[4], 25)) +
                      ((s[4] & s[5]) ^ (~s[4] & s[6])) + K[i] + w[i];
        uint32_t t2 = (ROR(s[0], 2) ^ ROR(s[0], 13) ^ ROR(s[0], 22)) +
                      ((s[0] & s[1]) ^ (s[0] & s[2]) ^ (s[1] & s[2]));
        memmove(s + 1, s, 7 * sizeof(s[0]));
        s[4] += t1;
        s[0] = t1 + t2;
    }
    for (int i = 0; i < 8; i++) h[i] += s[i];
}

static void sha256_init(sha256_ctx *c)
{
    static const uint32_t iv[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                   0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    memcpy(c->h, iv, sizeof(iv));
    c->len = 0;
}

static void sha256_update(sha256_ctx *c, const unsigned char *p, size_t n)
{
    while (n--) {
        c->buf[c->len++ % 64] = *p++;
        if (c->len % 64 == 0) compress(c->h, c->buf);
    }
}

static void sha256_final(sha256_ctx *c, unsigned char out[32])
{
    unsigned char len_be[8], b = 0x80;
    for (int i = 0; i < 8; i++) len_be[i] = (unsigned char)((c->len * 8) >> (56 - 8 * i));
    sha256_update(c, &b, 1);
    b = 0;
    while (c->len % 64 != 56) sha256_update(c, &b, 1);
    sha256_update(c, len_be, 8);
    for (int i = 0; i < 32; i++) out[i] = (unsigned char)(c->h[i / 4] >> (24 - 8 * (i % 4)));
}

void ll_hmac_sha256(const unsigned char *key, size_t key_len,
                    const unsigned char *msg, size_t msg_len,
                    unsigned char mac[32])
{
    unsigned char k[64] = {0}, pad[64];
    sha256_ctx c;
    if (key_len > sizeof(k)) {
        sha256_init(&c);
        sha256_update(&c, key, key_len);
        sha256_final(&c, k);
    } else if (key_len) {
        memcpy(k, key, key_len);
    }

    for (int i = 0; i < 64; i++) pad[i] = k[i] ^ 0x36;
    sha256_init(&c);
    sha256_update(&c, pad, 64);
    sha256_update(&c, msg, msg_len);
    sha256_final(&c, mac);

    for (int i = 0; i < 64; i++) pad[i] = k[i] ^ 0x5c;
    sha256_init(&c);
    sha256_update(&c, pad, 64);
    sha256_update(&c, mac, 32);
    sha256_final(&c, mac);
}
