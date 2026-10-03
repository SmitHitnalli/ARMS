#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>

#define ID_MAX_LEN   15
#define MAX_FLIGHTS  100
#define MAX_RUNWAYS  10

/* Result code returned by every core function that can fail. */
typedef enum {
    ARMS_OK = 0,
    ARMS_ERR_INVALID_ARG,        /* NULL pointer or out-of-range enum */
    ARMS_ERR_INVALID_ID,
    ARMS_ERR_DUPLICATE,
    ARMS_ERR_NOT_FOUND,
    ARMS_ERR_FULL,
    ARMS_ERR_INVALID_TRANSITION,
    ARMS_ERR_RUNWAY_OCCUPIED,
    ARMS_ERR_RUNWAY_CLOSED,
    ARMS_ERR_NOT_OCCUPIED,
    ARMS_ERR_NOT_ELIGIBLE,       /* flight is not WAITING or HOLDING */
    ARMS_ERR_INCOMPATIBLE
} ArmsStatus;

/* Returns 1 if id is 1-15 characters of A-Z, 0-9 or '-', else 0. */
int arms_id_valid(const char *id);

const char *arms_status_str(ArmsStatus s);

#endif
