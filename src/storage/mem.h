#ifndef LATTE_STORAGE_MEM_H
#define LATTE_STORAGE_MEM_H

#include "../domain.h"

/* Simple in-memory storage for unit tests. */
typedef struct ll_mem_storage ll_mem_storage;

ll_mem_storage *ll_mem_storage_new(void);
void            ll_mem_storage_free(ll_mem_storage *s);
int             ll_mem_save_token(ll_mem_storage *s, const char *token,
                                  const ll_cert_chain *chain);
int             ll_mem_load_token(ll_mem_storage *s, char **token_out,
                                  ll_cert_chain **chain_out);

#endif /* LATTE_STORAGE_MEM_H */
