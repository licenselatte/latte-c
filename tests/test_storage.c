/* Unit tests for file storage: JSON round-trip and legacy format. */
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include "../src/storage/file.h"
#include "../src/domain.h"

static int pass = 0, fail = 0;

#define CHECK(expr, name) \
    do { \
        if (expr) { printf("  PASS  %s\n", name); pass++; } \
        else      { printf("  FAIL  %s\n", name); fail++; } \
    } while(0)

int main(void)
{
    printf("=== test_storage ===\n");

    const char *path = "/tmp/latte_test_storage.latte";

    /* JSON round-trip */
    ll_cert_chain chain_in = {
        .submaster = "submaster-jwt-string",
        .project   = "project-jwt-string",
        .daily     = "daily-jwt-string",
    };
    const char *token_in = "test.token.value";

    int r = ll_file_save_token(path, token_in, &chain_in);
    CHECK(r == 0, "save_token returns 0");

    char *token_out = NULL;
    ll_cert_chain *chain_out = NULL;
    r = ll_file_load_token(path, &token_out, &chain_out);
    CHECK(r == 0, "load_token returns 0");
    CHECK(token_out && strcmp(token_out, token_in) == 0, "token round-trips correctly");
    CHECK(chain_out != NULL, "chain_out not null");
    CHECK(chain_out->submaster && strcmp(chain_out->submaster, chain_in.submaster) == 0,
          "submaster round-trips");
    CHECK(chain_out->project   && strcmp(chain_out->project, chain_in.project) == 0,
          "project round-trips");
    CHECK(chain_out->daily     && strcmp(chain_out->daily, chain_in.daily) == 0,
          "daily round-trips");

    free(token_out);
    ll_cert_chain_free(chain_out);

    /* Legacy "timestamp:token" format */
    const char *legacy_path = "/tmp/latte_test_legacy.latte";
    FILE *fp = fopen(legacy_path, "w");
    if (fp) {
        fprintf(fp, "1700000000:legacy.token.value");
        fclose(fp);
    }
    char *leg_tok = NULL;
    ll_cert_chain *leg_chain = NULL;
    r = ll_file_load_token(legacy_path, &leg_tok, &leg_chain);
    CHECK(r == 0, "legacy load returns 0");
    CHECK(leg_tok && strcmp(leg_tok, "legacy.token.value") == 0, "legacy token parsed");
    free(leg_tok);
    ll_cert_chain_free(leg_chain);

    /* Empty token clears cache */
    r = ll_file_save_token(path, "", NULL);
    CHECK(r == 0, "save empty token returns 0");
    token_out = NULL; chain_out = NULL;
    r = ll_file_load_token(path, &token_out, &chain_out);
    CHECK(r != 0, "load after empty save fails (cache cleared)");
    free(token_out);
    ll_cert_chain_free(chain_out);

    printf("\n%d passed, %d failed\n", pass, fail);
    return fail > 0 ? 1 : 0;
}
