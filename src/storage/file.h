#ifndef LATTE_STORAGE_FILE_H
#define LATTE_STORAGE_FILE_H

#include "../domain.h"

/*
 * ll_file_save_token: write token+chain to a JSON record at path.
 *   If token is NULL / empty, writes an empty JSON (effectively clears the cache).
 *   Returns 0 on success.
 */
int ll_file_save_token(const char *path,
                       const char *token,
                       const ll_cert_chain *chain);

/*
 * ll_file_load_token: read token+chain from path.
 *   Supports both JSON format and legacy "timestamp:token" format.
 *   Sets *token_out (heap-allocated, caller frees) and *chain_out.
 *   Returns 0 on success.
 */
int ll_file_load_token(const char *path,
                       char **token_out,
                       ll_cert_chain **chain_out);

#endif /* LATTE_STORAGE_FILE_H */
