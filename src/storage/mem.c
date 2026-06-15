#include "mem.h"
#include <stdlib.h>
#include <string.h>

static char *strdup_safe(const char *s)
{
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char *p = (char *)malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

struct ll_mem_storage {
    char *token;
    ll_cert_chain *chain;
};

ll_mem_storage *ll_mem_storage_new(void)
{
    return (ll_mem_storage *)calloc(1, sizeof(ll_mem_storage));
}

void ll_mem_storage_free(ll_mem_storage *s)
{
    if (!s) return;
    free(s->token);
    ll_cert_chain_free(s->chain);
    free(s);
}

int ll_mem_save_token(ll_mem_storage *s, const char *token,
                      const ll_cert_chain *chain)
{
    free(s->token);
    ll_cert_chain_free(s->chain);
    s->token = strdup_safe(token);
    if (chain) {
        ll_cert_chain *c = (ll_cert_chain *)calloc(1, sizeof(ll_cert_chain));
        if (c) {
            c->submaster = strdup_safe(chain->submaster);
            c->project   = strdup_safe(chain->project);
            c->daily     = strdup_safe(chain->daily);
        }
        s->chain = c;
    } else {
        s->chain = NULL;
    }
    return 0;
}

int ll_mem_load_token(ll_mem_storage *s, char **token_out,
                      ll_cert_chain **chain_out)
{
    if (!s->token || !s->token[0]) return -1;
    *token_out = strdup_safe(s->token);
    if (s->chain) {
        ll_cert_chain *c = (ll_cert_chain *)calloc(1, sizeof(ll_cert_chain));
        if (c) {
            c->submaster = strdup_safe(s->chain->submaster);
            c->project   = strdup_safe(s->chain->project);
            c->daily     = strdup_safe(s->chain->daily);
        }
        *chain_out = c;
    } else {
        *chain_out = (ll_cert_chain *)calloc(1, sizeof(ll_cert_chain));
    }
    return *token_out ? 0 : -1;
}
