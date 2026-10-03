#include "common.h"

int arms_id_valid(const char *id)
{
    if (id == NULL || id[0] == '\0')
        return 0;

    for (int i = 0; id[i] != '\0'; i++) {
        char c = id[i];
        if (i >= ID_MAX_LEN)
            return 0;
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-'))
            return 0;
    }
    return 1;
}

const char *arms_status_str(ArmsStatus s)
{
    switch (s) {
    case ARMS_OK:                    return "OK";
    case ARMS_ERR_INVALID_ARG:       return "Invalid argument";
    case ARMS_ERR_INVALID_ID:        return "Invalid ID (use 1-15 chars of A-Z, 0-9, '-')";
    case ARMS_ERR_DUPLICATE:         return "ID already exists";
    case ARMS_ERR_NOT_FOUND:         return "Not found";
    case ARMS_ERR_FULL:              return "Store is full";
    case ARMS_ERR_INVALID_TRANSITION:return "Invalid state transition";
    case ARMS_ERR_RUNWAY_OCCUPIED:   return "Runway is occupied";
    case ARMS_ERR_RUNWAY_CLOSED:     return "Runway is closed";
    case ARMS_ERR_NOT_OCCUPIED:      return "Runway is not occupied";
    case ARMS_ERR_NOT_ELIGIBLE:      return "Flight is not waiting or holding";
    case ARMS_ERR_INCOMPATIBLE:      return "Runway cannot handle this aircraft category";
    }
    return "Unknown status";
}
