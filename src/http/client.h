#ifndef LATTE_HTTP_CLIENT_H
#define LATTE_HTTP_CLIENT_H

#include "../domain.h"
#include "../errors.h"

typedef struct ll_http_client ll_http_client;

/* machine_name is sent on activation; NULL or "" sends none. */
ll_http_client *ll_http_client_new(const char *base_url, const char *project_key,
                                   const char *machine_name);
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
 * ll_http_deactivate:
 *   POST /v1/deactivate. Fills out->released / seat_free_at on LL_PORT_OK,
 *   and out->reason / message / available_at on
 *   LL_PORT_ERR_DEACTIVATION_BLOCKED (the 409).
 */
ll_port_error ll_http_deactivate(ll_http_client *c,
                                 const char *activation_id,
                                 const char *license_key,
                                 const char *machine_id,
                                 latte_deactivate_result *out);

/*
 * ll_http_machines:
 *   POST /v1/machines. On LL_PORT_OK *out is a heap array of *count
 *   machines (NULL when empty), freed with latte_machines_free.
 */
ll_port_error ll_http_machines(ll_http_client *c,
                               const char *activation_id,
                               const char *license_key,
                               const char *machine_id,
                               latte_machine **out,
                               size_t *count);

/*
 * ll_http_rename_machine:
 *   POST /v1/machines/rename. On LL_PORT_OK the stored name is copied into
 *   stored[stored_size] when stored is non-NULL.
 */
ll_port_error ll_http_rename_machine(ll_http_client *c,
                                     const char *activation_id,
                                     const char *license_key,
                                     const char *machine_id,
                                     const char *name,
                                     char *stored,
                                     size_t stored_size);

/*
 * ll_http_activate_body / ll_http_renew_body:
 *   The JSON request bodies for POST /v1/activate and POST /v1/renew. Both
 *   carry "sdk": {"language": "c", "version": LATTE_VERSION} so the API can
 *   tell which SDK release sent them. The activate body carries
 *   "machine_name" unless machine_name is NULL or "". Heap-allocated (caller
 *   frees), NULL on allocation failure.
 */
char *ll_http_activate_body(const char *project_key,
                            const char *license_key,
                            const char *machine_id,
                            const char *machine_name);
char *ll_http_renew_body(const char *activation_id,
                         const char *license_key,
                         const char *machine_id);

#endif /* LATTE_HTTP_CLIENT_H */
