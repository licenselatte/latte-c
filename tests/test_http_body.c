/*
 * Request bodies for /v1/activate and /v1/renew carry the SDK's language and
 * version alongside the fields the API acts on.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "../src/http/client.h"
#include "cJSON.h"

static int pass = 0, fail = 0;

#define CHECK(expr, name) \
    do { \
        if (expr) { printf("  PASS  %s\n", name); pass++; } \
        else      { printf("  FAIL  %s\n", name); fail++; } \
    } while(0)

static int str_is(const cJSON *obj, const char *key, const char *want)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    return v && cJSON_IsString(v) && strcmp(v->valuestring, want) == 0;
}

static void check_sdk(const char *body, const char *what)
{
    char name[96];
    cJSON *root = body ? cJSON_Parse(body) : NULL;
    const cJSON *sdk = root ? cJSON_GetObjectItemCaseSensitive(root, "sdk") : NULL;

    snprintf(name, sizeof(name), "%s body has an sdk object", what);
    CHECK(sdk && cJSON_IsObject(sdk), name);
    snprintf(name, sizeof(name), "%s sdk.language is \"c\"", what);
    CHECK(sdk && str_is(sdk, "language", "c"), name);
    snprintf(name, sizeof(name), "%s sdk.version is the CMake project version", what);
    CHECK(sdk && str_is(sdk, "version", LATTE_VERSION), name);

    cJSON_Delete(root);
}

int main(void)
{
    printf("=== test_http_body (version %s) ===\n", LATTE_VERSION);

    char *activate = ll_http_activate_body("pk_test_x", "KEY-1", "machine");
    check_sdk(activate, "activate");
    {
        cJSON *root = cJSON_Parse(activate);
        CHECK(root && str_is(root, "project_key", "pk_test_x") &&
              str_is(root, "license_key", "KEY-1") &&
              str_is(root, "machine_id", "machine"),
              "activate body keeps project_key, license_key, machine_id");
        cJSON_Delete(root);
    }
    free(activate);

    char *renew = ll_http_renew_body("act-1", "KEY-1", "machine");
    check_sdk(renew, "renew");
    {
        cJSON *root = cJSON_Parse(renew);
        CHECK(root && str_is(root, "activation_id", "act-1") &&
              str_is(root, "license_key", "KEY-1") &&
              str_is(root, "machine_id", "machine"),
              "renew body keeps activation_id, license_key, machine_id");
        cJSON_Delete(root);
    }
    free(renew);

    printf("\n%d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}
