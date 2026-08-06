/*
 * sdktest — port of cmd/sdktest/main.go.
 * Manual integration test: activate → check → poll.
 *
 * Usage:
 *   ./sdktest [app_id [license_key [--multi-instance]]]
 *
 * With --multi-instance, the SDK stores its token under ./.licenselatte/
 * instead of the shared OS-wide store, so each working directory can hold
 * its own license.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#  include <windows.h>
#  define sleep_seconds(n) Sleep((n)*1000)
#else
#  include <unistd.h>
#  define sleep_seconds(n) sleep(n)
#endif

#include "latte/latte.h"

#define DEFAULT_APP_ID      "pk_local_AHAK85389VQYXYB6S4BW66SKE53TWVTS"
#define DEFAULT_LICENSE_KEY "AHAK85T628WZ639CMVHF8TNDDX260Z"

static void print_license(const char *label, const latte_license *lic)
{
    char buf_iat[64], buf_exp[64];
    time_t t_iat = (time_t)lic->issued_at;
    time_t t_exp = (time_t)lic->expires_at;
    strftime(buf_iat, sizeof(buf_iat), "%Y-%m-%dT%H:%M:%SZ", gmtime(&t_iat));
    strftime(buf_exp, sizeof(buf_exp), "%Y-%m-%dT%H:%M:%SZ", gmtime(&t_exp));

    printf("  [%s]\n",             label);
    printf("  Key:           %s\n", lic->key           ? lic->key           : "");
    printf("  ActivationID:  %s\n", lic->activation_id ? lic->activation_id : "");
    printf("  ProjectID:     %s\n", lic->project_id    ? lic->project_id    : "");
    printf("  IssuedAt:      %s\n", buf_iat);
    printf("  ExpiresAt:     %s\n", buf_exp);
    printf("  GracePeriod:   %llds\n", (long long)lic->grace_period_seconds);
    printf("  InGracePeriod: %s\n", lic->in_grace_period ? "true" : "false");
    printf("  LicenseType:   %s\n", lic->license_type  ? lic->license_type  : "");
    for (size_t i = 0; i < lic->metadata_count; i++)
        printf("  Metadata[%s]: %s\n", lic->metadata[i].key, lic->metadata[i].value);
}

int main(int argc, char **argv)
{
    const char *app_id      = argc > 1 ? argv[1] : DEFAULT_APP_ID;
    const char *license_key = argc > 2 ? argv[2] : DEFAULT_LICENSE_KEY;
    int multi_instance = argc > 3 && strcmp(argv[3], "--multi-instance") == 0;

    latte_sdk *sdk = NULL;
    latte_config cfg = { .app_id = app_id, .multi_instance = multi_instance };
    latte_status st = latte_new(&cfg, &sdk);
    if (st != LATTE_OK) {
        fprintf(stderr, "Failed to init SDK: %s\n", latte_strerror(st));
        return 1;
    }

    printf("→ Activating…\n");
    latte_license *lic = NULL;
    st = latte_activate(sdk, license_key, &lic);
    if (st != LATTE_OK) {
        fprintf(stderr, "Activation failed: %s\n", latte_strerror(st));
        latte_free(sdk);
        return 1;
    }
    print_license("Activate", lic);
    latte_license_free(lic);

    printf("\n→ Checking stored token (offline)…\n");
    lic = NULL;
    st = latte_check(sdk, &lic);
    if (st != LATTE_OK) {
        fprintf(stderr, "  Check error: %s\n", latte_strerror(st));
    } else {
        print_license("Check", lic);
        latte_license_free(lic);
    }

    printf("\n→ Polling every 15 s (Ctrl-C to stop)…\n");
    for (;;) {
        sleep_seconds(15);
        lic = NULL;
        st = latte_check(sdk, &lic);
        if (st != LATTE_OK) {
            printf("  license invalid: %s\n", latte_strerror(st));
            continue;
        }
        char buf[64];
        time_t t = (time_t)lic->expires_at;
        strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", gmtime(&t));
        printf("  OK — expires %s%s\n", buf, lic->in_grace_period ? " [GRACE PERIOD]" : "");
        latte_license_free(lic);
    }

    latte_free(sdk);
    return 0;
}
