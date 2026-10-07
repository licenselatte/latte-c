#ifndef LATTE_HTTP_CLIENT_H
#define LATTE_HTTP_CLIENT_H

#include "../domain.h"
#include "../errors.h"

typedef struct ll_http_client ll_http_client;

ll_http_client *ll_http_client_new(const char *base_url, const char *project_key);
void            ll_http_client_free(ll_http_client *c);

/*
 * ll_http_activate:
 *   POST /v1/activate.  On success token_out and chain_out are heap-allocated
 *   (caller must free token_out and ll_cert_chain_free chain_out).
 *   Returns LL_PORT_OK or an error code.
 */
ll_port_error ll_http_activate(ll_http_client *c,
                               const char *license_key,
                               const char *machine_id,
                               char **token_out,
                               ll_cert_chain **chain_out);

/*
 * ll_http_renew:
 *   POST /v1/renew.
 */
ll_port_error ll_http_renew(ll_http_client *c,
                            const char *activation_id,
                            const char *license_key,
                            const char *machine_id,
                            char **token_out,
                            ll_cert_chain **chain_out);

/*
 * ll_http_activate_body / ll_http_renew_body:
 *   The JSON request bodies for POST /v1/activate and POST /v1/renew. Both
 *   carry "sdk": {"language": "c", "version": LATTE_VERSION} so the API can
 *   tell which SDK release sent them. Heap-allocated (caller frees), NULL on
 *   allocation failure.
 */
char *ll_http_activate_body(const char *project_key,
                            const char *license_key,
                            const char *machine_id);
char *ll_http_renew_body(const char *activation_id,
                         const char *license_key,
                         const char *machine_id);

#endif /* LATTE_HTTP_CLIENT_H */
