#ifndef LATTE_HTTP_TRANSPORT_H
#define LATTE_HTTP_TRANSPORT_H

/*
 * ll_http_post:
 *   POSTs body to url as application/json, with a LATTE_HTTP_TIMEOUT_SECS
 *   budget for the whole exchange. Returns 0 with the HTTP status in *status
 *   and the NUL-terminated answer in *response (heap-allocated, caller frees;
 *   NULL when the answer is empty), or -1 on a transport failure, with
 *   *response NULL.
 */
int ll_http_post(const char *url, const char *body, long *status, char **response);

#endif /* LATTE_HTTP_TRANSPORT_H */
