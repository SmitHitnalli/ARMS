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
    ARMS_ERR_NOT_ELIGIBLE,       /* current state does not permit this operation */
    ARMS_ERR_INCOMPATIBLE,
    ARMS_ERR_EMPTY,
    ARMS_ERR_NO_RUNWAY,          /* flights are waiting but no compatible runway is free */
    ARMS_ERR_NO_MEMORY,          /* malloc failed; the call changed nothing */
    ARMS_ERR_IO,                 /* a file could not be opened, written or renamed */
    ARMS_ERR_FORMAT              /* a save file is malformed or internally inconsistent */
} ArmsStatus;

/* Returns 1 if id is 1-15 characters of A-Z, 0-9 or '-', else 0. */
int arms_id_valid(const char *id);

const char *arms_status_str(ArmsStatus s);

#endif
