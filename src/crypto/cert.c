#include "cert.h"
#include "jwt.h"
#include <string.h>
#include <stdlib.h>

/* hex nibble value, returns -1 on bad char */
static int hex_val(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

int ll_verify_cert(const unsigned char *parent_pub,
                   const char *cert_jwt,
                   cJSON **claims_out)
{
    return ll_jwt_verify_ed25519(cert_jwt, parent_pub, 32, claims_out);
}

int ll_pubkey_from_cert(const cJSON *claims, const char *field,
                        unsigned char *pub_out)
{
    const char *hex = ll_jwt_string_claim(claims, field);
    if (!hex) return -1;
    size_t hlen = strlen(hex);
    if (hlen != 64) return -1;   /* Ed25519 = 32 bytes = 64 hex chars */
    for (int i = 0; i < 32; i++) {
        int hi = hex_val(hex[2*i]);
        int lo = hex_val(hex[2*i+1]);
        if (hi < 0 || lo < 0) return -1;
        pub_out[i] = (unsigned char)((hi << 4) | lo);
    }
    return 0;
}
