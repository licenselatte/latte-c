/*
 * Simple example: port of example/simple/main.go.
 * Demonstrates activate, then listing this licence's machines and releasing
 * this one so the licence can move to another machine.
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
    latte_config *cfg = latte_config_new(SDK_TEST_APP_KEY);
    latte_status st = latte_new(cfg, &sdk);
    latte_config_free(cfg);
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
        latte_free(sdk);
        return 1;
    }
    printf("License activated!\n");
    printf("  Key:          %s\n", lic->key         ? lic->key         : "");
    printf("  ActivationID: %s\n", lic->activation_id? lic->activation_id: "");
    printf("  LicenseType:  %s\n", lic->license_type ? lic->license_type : "");
    printf("  IssuedAt:     %lld\n", (long long)lic->issued_at);
    printf("  ExpiresAt:    %lld\n", (long long)lic->expires_at);
    printf("  InGracePeriod:%d\n",   lic->in_grace_period);
    if (lic->in_grace_period)
        printf("  Couldn't reach the licence server. Connect to the internet to keep using the app.\n");
    latte_license_free(lic);

    latte_machine *machines = NULL;
    size_t count = 0;
    st = latte_machines(sdk, &machines, &count);
    if (st != LATTE_OK) {
        fprintf(stderr, "Machine list error: %s\n", latte_strerror(st));
        latte_free(sdk);
        return 1;
    }
    for (size_t i = 0; i < count; i++)
        printf("  %s %s (last seen %lld)%s\n", machines[i].fingerprint,
               machines[i].name ? machines[i].name : "",
               (long long)machines[i].last_seen_at,
               machines[i].current ? " <- this machine" : "");
    latte_machines_free(machines, count);

    printf("Release this machine so the licence can move to another one? [y/N]\n> ");
    if (!fgets(input, sizeof(input), stdin) || input[0] != 'y') {
        latte_free(sdk);
        return 0;
    }
    latte_deactivate_result res;
    st = latte_deactivate(sdk, &res);
    if (st == LATTE_ERR_DEACTIVATION_BLOCKED)
        printf("%s\n", res.message);
    else if (st != LATTE_OK)
        fprintf(stderr, "Deactivation failed, nothing was changed; try again when online: %s\n",
                latte_strerror(st));
    else if (res.seat_free_at)
        printf("Released. The seat can be used on another machine from %lld.\n",
               (long long)res.seat_free_at);
    else
        printf("Released. Activate the licence on your other machine.\n");

    latte_free(sdk);
    return st == LATTE_OK ? 0 : 1;
}
