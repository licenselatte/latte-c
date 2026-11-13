/*
 * https_check: the HTTP transport against real TLS endpoints, for CI.
 *
 *   https_check --accept URL...   a POST to each URL must get an HTTP answer
 *                                 (any status): handshake and certificate OK.
 *   https_check --reject URL...   a POST to each URL must fail before any
 *                                 answer: bad certificate, TLS < 1.2, ...
 *   https_check --live APP_ID LICENSE_KEY MACHINE_ID
 *                                 activates, renews and deactivates against
 *                                 the API APP_ID points at. Every token must
 *                                 verify against the embedded roots.
 *                                 MACHINE_ID is the raw ID to use, so
 *                                 parallel jobs do not share an activation.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "latte/latte.h"
#include "../src/appid.h"
#include "../src/constants.h"
#include "../src/machineid.h"
#include "../src/verify.h"
#include "../src/http/client.h"
#include "../src/http/transport.h"

static int post_each(char **urls, int n, int want_answer)
{
    int failed = 0;
    for (int i = 0; i < n; i++) {
        long status = 0;
        char *resp = NULL;
        int answered = ll_http_post(urls[i], "{}", &status, &resp) == 0;
        free(resp);
        int ok = answered == want_answer;
        printf("%s  %s: %s\n", ok ? "PASS" : "FAIL", urls[i],
               answered ? "HTTP answer" : "transport failure");
        if (answered) printf("        status %ld\n", status);
        failed += !ok;
    }
    return failed;
}

static int live(const char *app_id, const char *license_key, const char *raw_machine_id)
{
    latte_config *cfg = latte_config_new(app_id);
    latte_config_set_multi_instance(cfg, 1);
    latte_config_set_machine_id(cfg, raw_machine_id);
    latte_sdk *sdk = NULL;
    latte_status st = latte_new(cfg, &sdk);
    latte_config_free(cfg);
    if (st != LATTE_OK) {
        printf("FAIL  latte_new: %s\n", latte_strerror(st));
        return 1;
    }

    int failed = 0;
    latte_license *lic = NULL;
    st = latte_activate(sdk, license_key, &lic);
    printf("%s  activate: %s\n", st == LATTE_OK ? "PASS" : "FAIL", latte_strerror(st));
    failed += st != LATTE_OK;

    if (lic) {
        /* Renewal normally starts an hour after issuance; call it directly. */
        ll_env env;
        const char *app_key = NULL;
        char mid[65];
        ll_parse_app_id(app_id, &env, &app_key);
        ll_machine_id_hash(raw_machine_id, app_id, mid);
        ll_http_client *http = ll_http_client_new(
            env == LL_ENV_LOCAL ? LATTE_URL_LOCAL : env == LL_ENV_TEST ? LATTE_URL_TEST : LATTE_URL_LIVE,
            app_id, NULL);

        static const char *const roots_hex[] = LATTE_MASTER_PUBKEYS_HEX;
        unsigned char roots[LATTE_MASTER_PUBKEY_COUNT * 32];
        for (size_t i = 0; i < sizeof(roots); i++) {
            unsigned v = 0;
            sscanf(roots_hex[i / 32] + 2 * (i % 32), "%2x", &v);
            roots[i] = (unsigned char)v;
        }

        char *token = NULL;
        ll_cert_chain *chain = NULL;
        ll_license *renewed = NULL;
        ll_port_error verr;
        ll_port_error pe = ll_http_renew(http, lic->activation_id, lic->key, mid, &token, &chain);
        int ok = pe == LL_PORT_OK &&
                 ll_verify_activation_any(roots, LATTE_MASTER_PUBKEY_COUNT, token, chain,
                                          &renewed, &verr) == 0 &&
                 strcmp(renewed->activation_id, lic->activation_id) == 0;
        printf("%s  renew: port error %d, token %s\n", ok ? "PASS" : "FAIL", (int)pe,
               renewed ? "verified" : "not verified");
        failed += !ok;
        ll_license_free(renewed);
        ll_cert_chain_free(chain);
        free(token);
        ll_http_client_free(http);
        latte_license_free(lic);

        latte_deactivate_result res;
        st = latte_deactivate(sdk, &res);
        printf("%s  deactivate: %s\n", st == LATTE_OK ? "PASS" : "FAIL", latte_strerror(st));
        if (st == LATTE_ERR_DEACTIVATION_BLOCKED) printf("        %s (%s)\n", res.message, res.reason);
        failed += st != LATTE_OK;
    }

    latte_free(sdk);
    return failed;
}

int main(int argc, char **argv)
{
    if (argc >= 3 && strcmp(argv[1], "--accept") == 0) return post_each(argv + 2, argc - 2, 1) != 0;
    if (argc >= 3 && strcmp(argv[1], "--reject") == 0) return post_each(argv + 2, argc - 2, 0) != 0;
    if (argc == 5 && strcmp(argv[1], "--live") == 0) return live(argv[2], argv[3], argv[4]) != 0;
    fprintf(stderr, "usage: https_check --accept URL... | --reject URL... | "
                    "--live APP_ID LICENSE_KEY MACHINE_ID\n");
    return 2;
}
