#include "domain.h"
#include <stdlib.h>

void ll_license_free(ll_license *l)
{
    if (!l) return;
    free(l->key);
    free(l->activation_id);
    free(l->project_id);
    free(l->machine_id_hash);
    free(l->license_type);
    for (size_t i = 0; i < l->metadata_count; i++) {
        free(l->metadata_keys[i]);
        free(l->metadata_values[i]);
    }
    free(l->metadata_keys);
    free(l->metadata_values);
    free(l);
}

void ll_cert_chain_free(ll_cert_chain *c)
{
    if (!c) return;
    free(c->submaster);
    free(c->project);
    free(c->daily);
    free(c);
}
