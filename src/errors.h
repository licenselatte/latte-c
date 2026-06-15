#ifndef LATTE_ERRORS_H
#define LATTE_ERRORS_H

#include "../include/latte/latte.h"

/*
 * Internal port-level error codes (mirrors Go's ports package sentinels).
 * These are mapped to public latte_status values by map_network_error().
 */
typedef enum {
    LL_PORT_OK = 0,
    LL_PORT_ERR_NETWORK,
    LL_PORT_ERR_LICENSE_NOT_FOUND,
    LL_PORT_ERR_LICENSE_INACTIVE_OR_EXPIRED,
    LL_PORT_ERR_SEAT_LIMIT_REACHED,
    LL_PORT_ERR_INVALID_PROJECT_KEY,
    LL_PORT_ERR_GRACE_PERIOD_EXPIRED,
    LL_PORT_ERR_LICENSE_TOO_OLD,
    LL_PORT_ERR_INVALID_LICENSE,  /* generic invalid — wipe stored token */
} ll_port_error;

latte_status map_network_error(ll_port_error e);

#endif /* LATTE_ERRORS_H */
