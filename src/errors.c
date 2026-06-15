#include "errors.h"

latte_status map_network_error(ll_port_error e)
{
    switch (e) {
    case LL_PORT_ERR_LICENSE_INACTIVE_OR_EXPIRED: return LATTE_ERR_LICENSE_EXPIRED;
    case LL_PORT_ERR_SEAT_LIMIT_REACHED:          return LATTE_ERR_SEAT_LIMIT;
    case LL_PORT_ERR_INVALID_PROJECT_KEY:         return LATTE_ERR_INVALID_PROJECT_KEY;
    case LL_PORT_ERR_LICENSE_NOT_FOUND:           return LATTE_ERR_LICENSE_NOT_FOUND;
    case LL_PORT_ERR_NETWORK:                     return LATTE_ERR_NETWORK;
    default:                                       return LATTE_ERR_INTERNAL;
    }
}

const char *latte_strerror(latte_status s)
{
    switch (s) {
    case LATTE_OK:                          return "success";
    case LATTE_ERR_INVALID_KEY:             return "licenselatte: invalid license key";
    case LATTE_ERR_LICENSE_EXPIRED:         return "licenselatte: license expired";
    case LATTE_ERR_NOT_ACTIVATED:           return "licenselatte: not activated on this machine";
    case LATTE_ERR_SEAT_LIMIT:              return "licenselatte: activation seat limit reached";
    case LATTE_ERR_LICENSE_NOT_FOUND:       return "licenselatte: license not found";
    case LATTE_ERR_INVALID_PROJECT_KEY:     return "licenselatte: invalid project key";
    case LATTE_ERR_INVALID_APPID:           return "licenselatte: invalid AppID";
    case LATTE_ERR_UNKNOWN_ENVIRONMENT:     return "licenselatte: unknown environment";
    case LATTE_ERR_INVALID_APPID_KEY_SEGMENT: return "licenselatte: invalid app id key segment";
    case LATTE_ERR_INVALID_APPID_CHECKSUM:  return "licenselatte: invalid app id checksum";
    case LATTE_ERR_STORAGE_INIT_FAILED:     return "licenselatte: cannot initialize storage";
    case LATTE_ERR_MACHINE_ID_FAILED:       return "licenselatte: cannot determine machine ID";
    case LATTE_ERR_NETWORK:                 return "licenselatte: network error";
    case LATTE_ERR_SERVER_INVALID_TOKEN:    return "licenselatte: server returned invalid token";
    default:                                return "licenselatte: internal error";
    }
}
