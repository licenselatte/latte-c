/*
 * Simple example — port of example/simple/main.go.
 * Demonstrates the activate → check pattern.
 */
#include <stdio.h>
#include <string.h>
#include "latte/latte.h"

/* Replace with a real pk_live_... key from the dashboard. */
#define SDK_TEST_APP_KEY "pk_live_G98S8BKDDMZW32RMQM8NZT33E5CTFEDC"

/* The following key is always valid and has unlimited activations:
 * G98S8-BWAR6-Y0Z7F-JT10A-7EWD9-33265 */

int main(void)
{
    latte_sdk *sdk = NULL;
    latte_config cfg = { .app_id = SDK_TEST_APP_KEY };
    latte_status st = latte_new(&cfg, &sdk);
    if (st != LATTE_OK) {
        fprintf(stderr, "Failed to init SDK: %s\n", latte_strerror(st));
        return 1;
    }

    printf("Type your license key (e.g. XXXXX-XXXXX-XXXXX-XXXXX-XXXXX-XXXXX):\n> ");
    char input[128] = {0};
    if (!fgets(input, sizeof(input), stdin)) {
        latte_free(sdk);
        return 1;
    }
    /* Strip newline */
    size_t len = strlen(input);
    if (len > 0 && input[len-1] == '\n') input[len-1] = '\0';

    latte_license *lic = NULL;
    st = latte_activate(sdk, input, &lic);
    if (st != LATTE_OK) {
        fprintf(stderr, "Activation error: %s\n", latte_strerror(st));
    } else {
        printf("License activated!\n");
        printf("  Key:          %s\n", lic->key         ? lic->key         : "");
        printf("  ActivationID: %s\n", lic->activation_id? lic->activation_id: "");
        printf("  LicenseType:  %s\n", lic->license_type ? lic->license_type : "");
        printf("  IssuedAt:     %lld\n", (long long)lic->issued_at);
        printf("  ExpiresAt:    %lld\n", (long long)lic->expires_at);
        printf("  InGracePeriod:%d\n",   lic->in_grace_period);
        latte_license_free(lic);
    }

    latte_free(sdk);
    return st == LATTE_OK ? 0 : 1;
}
