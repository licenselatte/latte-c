/*
 * validator: port of cmd/validator/main.go.
 * Low-level debug tool: activate, print raw JWT claims, renew.
 *
 * Usage:
 *   ./validator [app_id [license_key [machine_id]]]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "latte/latte.h"

/* Internal headers needed for the raw verification path */
#include "../src/constants.h"
#include "../src/domain.h"
#include "../src/errors.h"
#include "../src/crypto/key.h"
#include "../src/crypto/cert.h"
#include "../src/verify.h"
#include "../src/validate.h"
#include "../src/http/client.h"

#define DEFAULT_APP_ID      "pk_local_AHAK85389VQYXYB6S4BW66SKE53TWVTS"
#define DEFAULT_LICENSE_KEY "AHAK856T8PQS0245KDB1FEC9VXA998"
#define DEFAULT_MACHINE_ID  "test-machine-001"

/* hex decode helper */
static int hex_decode(const char *hex, unsigned char *out, size_t out_len)
{
    for (size_t i = 0; i < out_len; i++) {
        unsigned int byte;
        if (sscanf(hex + 2*i, "%02x", &byte) != 1) return -1;
        out[i] = (unsigned char)byte;
    }
    return 0;
}

int main(int argc, char **argv)
{
    const char *app_id      = argc > 1 ? argv[1] : DEFAULT_APP_ID;
    const char *license_key = argc > 2 ? argv[2] : DEFAULT_LICENSE_KEY;
    const char *machine_id  = argc > 3 ? argv[3] : DEFAULT_MACHINE_ID;

    unsigned char master_pub[32];
    if (hex_decode(LATTE_MASTER_PUBKEY_HEX, master_pub, 32) < 0) {
        fprintf(stderr, "Bad master public key\n");
        return 1;
    }

    /* Use the local API endpoint directly */
    ll_http_client *client = ll_http_client_new(LATTE_URL_LOCAL, app_id);
    if (!client) { fprintf(stderr, "OOM\n"); return 1; }

    char sanitized[64];
    ll_sanitize_key(license_key, sanitized, sizeof(sanitized));
    printf("Sanitized key: %s\n\n", sanitized);

    /* --- Activate --- */
    printf("→ Activating…\n");
    char *token = NULL;
    ll_cert_chain *chain = NULL;
    ll_port_error pe = ll_http_activate(client, sanitized, machine_id, &token, &chain);
    if (pe != LL_PORT_OK) {
        fprintf(stderr, "Activation error: %d\n", pe);
        ll_http_client_free(client);
        return 1;
    }
    printf("Token: %s\n\n", token);

    /* --- Verify --- */
    printf("→ Validating…\n");
    ll_license *lic = NULL;
    ll_port_error ve = LL_PORT_OK;
    if (ll_verify_activation(master_pub, token, chain, &lic, &ve) < 0) {
        fprintf(stderr, "Verification error: %d\n", ve);
        free(token); ll_cert_chain_free(chain);
        ll_http_client_free(client);
        return 1;
    }
    ll_port_error vr = ll_validate(lic, machine_id);
    if (vr != LL_PORT_OK) {
        fprintf(stderr, "Validation error: %d\n", vr);
        ll_license_free(lic);
        free(token); ll_cert_chain_free(chain);
        ll_http_client_free(client);
        return 1;
    }

    char buf[64];
    time_t t = (time_t)lic->expires_at;
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", gmtime(&t));
    printf("Key:           %s\n", lic->key           ? lic->key           : "");
    printf("ActivationID:  %s\n", lic->activation_id ? lic->activation_id : "");
    printf("ProjectID:     %s\n", lic->project_id    ? lic->project_id    : "");
    printf("ExpiresAt:     %s\n", buf);
    printf("GracePeriod:   %lldsec\n", (long long)lic->grace_period);

    char aid[256];
    char key_copy[64];
    strncpy(aid,      lic->activation_id ? lic->activation_id : "", sizeof(aid)-1);
    strncpy(key_copy, lic->key           ? lic->key           : "", sizeof(key_copy)-1);
    ll_license_free(lic);
    free(token); ll_cert_chain_free(chain);

    /* --- Renew --- */
    printf("\n→ Renewing…\n");
    char *renewed_token = NULL;
    ll_cert_chain *renewed_chain = NULL;
    pe = ll_http_renew(client, aid, key_copy, machine_id, &renewed_token, &renewed_chain);
    if (pe != LL_PORT_OK) {
        fprintf(stderr, "Renew error: %d\n", pe);
        ll_http_client_free(client);
        return 1;
    }

    ll_license *lic2 = NULL;
    ve = LL_PORT_OK;
    if (ll_verify_activation(master_pub, renewed_token, renewed_chain, &lic2, &ve) < 0) {
        fprintf(stderr, "Renew verification error: %d\n", ve);
        free(renewed_token); ll_cert_chain_free(renewed_chain);
        ll_http_client_free(client);
        return 1;
    }
    vr = ll_validate(lic2, machine_id);
    if (vr != LL_PORT_OK) {
        fprintf(stderr, "Renew validation error: %d\n", vr);
        ll_license_free(lic2);
        free(renewed_token); ll_cert_chain_free(renewed_chain);
        ll_http_client_free(client);
        return 1;
    }

    t = (time_t)lic2->expires_at;
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", gmtime(&t));
    printf("Renewed token valid — ExpiresAt: %s\n", buf);

    ll_license_free(lic2);
    free(renewed_token); ll_cert_chain_free(renewed_chain);
    ll_http_client_free(client);
    return 0;
}
