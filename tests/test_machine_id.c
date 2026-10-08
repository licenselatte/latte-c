/*
 * Runs every case in testdata/machine_id.json through ll_machine_id_hash,
 * the function applied to both the OS machine ID and a caller-supplied one.
 */
#include <sodium.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "../src/machineid.h"

static char *read_file(const char *path)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) return NULL;
    fseek(fp, 0, SEEK_END);
    long n = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    char *buf = malloc((size_t)n + 1);
    if (buf && fread(buf, 1, (size_t)n, fp) != (size_t)n) { free(buf); buf = NULL; }
    if (buf) buf[n] = '\0';
    fclose(fp);
    return buf;
}

int main(void)
{
    printf("=== test_machine_id ===\n");
    if (sodium_init() < 0) return 1;

    char *data = read_file("testdata/machine_id.json");
    if (!data) { printf("  FAIL  cannot read testdata/machine_id.json\n"); return 1; }
    cJSON *root = cJSON_Parse(data);
    free(data);
    cJSON *cases = cJSON_GetObjectItem(root, "cases");
    if (!cJSON_IsArray(cases) || cJSON_GetArraySize(cases) == 0) {
        printf("  FAIL  no cases\n");
        return 1;
    }

    int pass = 0, fail = 0;
    cJSON *c;
    cJSON_ArrayForEach(c, cases) {
        const char *raw = cJSON_GetStringValue(cJSON_GetObjectItem(c, "raw_machine_id"));
        const char *app = cJSON_GetStringValue(cJSON_GetObjectItem(c, "app_id"));
        const char *want = cJSON_GetStringValue(cJSON_GetObjectItem(c, "expect_machine_id"));
        char got[65];
        if (raw && app && want && ll_machine_id_hash(raw, app, got) == 0 &&
            strcmp(got, want) == 0) {
            printf("  PASS  %s / %s\n", app, raw);
            pass++;
        } else {
            printf("  FAIL  %s / %s\n", app ? app : "?", raw ? raw : "?");
            fail++;
        }
    }
    cJSON_Delete(root);

    printf("%d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}
