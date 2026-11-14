/*
 * ll_hmac_sha256 against the HMAC-SHA-256 test cases of RFC 4231 section 4.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hex.h"
#include "../src/crypto/hmac_sha256.h"

static int pass = 0, fail = 0;

#define CHECK(expr, name) \
    do { \
        if (expr) { printf("  PASS  %s\n", name); pass++; } \
        else      { printf("  FAIL  %s\n", name); fail++; } \
    } while(0)

/* Cases 1 to 7 in order; case 5 (index 4) carries only the first 128 bits of the MAC. */
static const struct { const char *key, *data, *mac; } cases[] = {
    {
        "0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b",
        "4869205468657265",
        "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7",
    },
    {
        "4a656665",
        "7768617420646f2079612077616e7420666f72206e6f7468696e673f",
        "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843",
    },
    {
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
        "dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd"
        "dddddddddddddddddddddddddddddddddddd",
        "773ea91e36800e46854db8ebd09181a72959098b3ef8c122d9635514ced565fe",
    },
    {
        "0102030405060708090a0b0c0d0e0f10111213141516171819",
        "cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"
        "cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd",
        "82558a389a443c0ea4cc819899f2083a85f0faa3e578f8077a2e3ff46729665b",
    },
    {
        "0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c",
        "546573742057697468205472756e636174696f6e",
        "a3b6167473100ee06e0c796c2955552b",
    },
    {
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "aaaaaa",
        "54657374205573696e67204c6172676572205468616e20426c6f636b2d53697a"
        "65204b6579202d2048617368204b6579204669727374",
        "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54",
    },
    {
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "aaaaaa",
        "5468697320697320612074657374207573696e672061206c6172676572207468"
        "616e20626c6f636b2d73697a65206b657920616e642061206c61726765722074"
        "68616e20626c6f636b2d73697a6520646174612e20546865206b6579206e6565"
        "647320746f20626520686173686564206265666f7265206265696e6720757365"
        "642062792074686520484d414320616c676f726974686d2e",
        "9b09ffa71b942fcb27635fbcd5b0e944bfdc63644f0713938a7f51535c3a35e2",
    },
};

int main(void)
{
    printf("=== test_hmac_sha256 ===\n");

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        size_t key_len, data_len, want_len;
        unsigned char *key = hex_dup(cases[i].key, &key_len);
        unsigned char *data = hex_dup(cases[i].data, &data_len);
        unsigned char *want = hex_dup(cases[i].mac, &want_len);
        unsigned char got[32];
        char name[32];

        ll_hmac_sha256(key, key_len, data, data_len, got);
        snprintf(name, sizeof(name), "RFC 4231 case %u", (unsigned)i + 1);
        CHECK(want_len <= sizeof(got) && memcmp(got, want, want_len) == 0, name);

        free(key);
        free(data);
        free(want);
    }

    /* Block boundaries RFC 4231 misses: keys of 63, 64, 65 and 200 bytes,
     * messages ending at the padding edges. Key byte i is 7i+1, message byte
     * i is 13i+5 (mod 256); expected MACs from Python's hmac module. */
    static const struct { size_t key_len, msg_len; const char *mac; } edges[] = {
        {64, 55, "34b579f0c1dac0756e7e574901720c25f867f600143de92a8d762a4bb5981ed3"},
        {64, 56, "3da1c57165d1cb95221c14d2381f86de1aa213809fde9446e2b9d40e39df70f5"},
        {65, 63, "38bc50c7bdde6e8d4b7c6f42e6e14526f7c76f6eeb6ae435e4626fe3ae2e1d1a"},
        {65, 64, "686c47845e64df78d4c10afb95c324b78a3424f696eb90a7d9a3700c7275c4ef"},
        {63, 119, "194c743584379a5e372827b2e5ecaf29d4509646ee08fbb9c3791de6c4eaaccf"},
        {0, 120, "429fda592edbf5806f1bebf98b14face8ec2da525f0eb1a7ce3bda7f7c5357c7"},
        {200, 0, "1f26b41a7acaba4ad7b69d99c769fb1165df09acd3931132548bd97aeef712d6"},
    };
    for (size_t i = 0; i < sizeof(edges) / sizeof(edges[0]); i++) {
        unsigned char key[200], msg[120], got[32];
        for (size_t j = 0; j < edges[i].key_len; j++) key[j] = (unsigned char)(j * 7 + 1);
        for (size_t j = 0; j < edges[i].msg_len; j++) msg[j] = (unsigned char)(j * 13 + 5);
        ll_hmac_sha256(key, edges[i].key_len, msg, edges[i].msg_len, got);
        size_t want_len = 0;
        unsigned char *want = hex_dup(edges[i].mac, &want_len);
        char name[64];
        snprintf(name, sizeof(name), "key %zu bytes, message %zu bytes",
                 edges[i].key_len, edges[i].msg_len);
        CHECK(want && want_len == 32 && memcmp(got, want, 32) == 0, name);
        free(want);
    }

    printf("%d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}
