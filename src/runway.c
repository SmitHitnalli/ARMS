#include "runway.h"

#include <string.h>

void runway_store_init(RunwayStore *rs)
{
    if (rs != NULL)
        rs->count = 0;
}

ArmsStatus runway_add(RunwayStore *rs, const char *id, AircraftCategory max_category)
{
    if (rs == NULL)
        return ARMS_ERR_INVALID_ARG;
    if ((int)max_category < AIRCRAFT_SMALL || (int)max_category > AIRCRAFT_LARGE)
        return ARMS_ERR_INVALID_ARG;
    if (!arms_id_valid(id))
        return ARMS_ERR_INVALID_ID;
    if (runway_find(rs, id) != NULL)
        return ARMS_ERR_DUPLICATE;
    if (rs->count >= MAX_RUNWAYS)
        return ARMS_ERR_FULL;

    Runway *r = &rs->items[rs->count];
    snprintf(r->id, sizeof r->id, "%s", id);
    r->status = RUNWAY_AVAILABLE;
    r->max_category = max_category;
    r->current_flight_id[0] = '\0';
    rs->count++;
    return ARMS_OK;
}

Runway *runway_find(RunwayStore *rs, const char *id)
{
    if (rs == NULL || id == NULL)
        return NULL;

    for (int i = 0; i < rs->count; i++) {
        if (strcmp(rs->items[i].id, id) == 0)
            return &rs->items[i];
    }
    return NULL;
}

ArmsStatus runway_set_status(RunwayStore *rs, const char *id, RunwayStatus to)
{
    if (rs == NULL || id == NULL)
        return ARMS_ERR_INVALID_ARG;
    if (to != RUNWAY_AVAILABLE && to != RUNWAY_CLOSED)
        return ARMS_ERR_INVALID_ARG;

    Runway *r = runway_find(rs, id);
    if (r == NULL)
        return ARMS_ERR_NOT_FOUND;
    if (r->status == RUNWAY_OCCUPIED)
        return ARMS_ERR_RUNWAY_OCCUPIED;
    if (r->status == to)
        return ARMS_ERR_INVALID_ARG;

    r->status = to;
    return ARMS_OK;
}

int runway_is_compatible(const Runway *r, const Flight *f)
{
    if (r == NULL || f == NULL)
        return 0;
    return f->category <= r->max_category;
}

ArmsStatus runway_assign(RunwayStore *rs, const char *runway_id, const Flight *flight)
{
    if (rs == NULL || runway_id == NULL || flight == NULL)
        return ARMS_ERR_INVALID_ARG;

    Runway *r = runway_find(rs, runway_id);
    if (r == NULL)
        return ARMS_ERR_NOT_FOUND;
    if (r->status == RUNWAY_CLOSED)
        return ARMS_ERR_RUNWAY_CLOSED;
    if (r->status == RUNWAY_OCCUPIED)
        return ARMS_ERR_RUNWAY_OCCUPIED;
    if (flight->state != STATE_WAITING && flight->state != STATE_HOLDING)
        return ARMS_ERR_NOT_ELIGIBLE;
    if (!runway_is_compatible(r, flight))
        return ARMS_ERR_INCOMPATIBLE;

    r->status = RUNWAY_OCCUPIED;
    snprintf(r->current_flight_id, sizeof r->current_flight_id, "%s", flight->id);
    return ARMS_OK;
}

ArmsStatus runway_release(RunwayStore *rs, const char *runway_id)
{
    if (rs == NULL || runway_id == NULL)
        return ARMS_ERR_INVALID_ARG;

    Runway *r = runway_find(rs, runway_id);
    if (r == NULL)
        return ARMS_ERR_NOT_FOUND;
    if (r->status != RUNWAY_OCCUPIED)
        return ARMS_ERR_NOT_OCCUPIED;

    r->status = RUNWAY_AVAILABLE;
    r->current_flight_id[0] = '\0';
    return ARMS_OK;
}

int runway_count(const RunwayStore *rs)
{
    return rs == NULL ? 0 : rs->count;
}

const Runway *runway_at(const RunwayStore *rs, int index)
{
    if (rs == NULL || index < 0 || index >= rs->count)
        return NULL;
    return &rs->items[index];
}

void runway_print(FILE *out, const Runway *r)
{
    if (out == NULL || r == NULL)
        return;
    fprintf(out, "%-15s %-9s %-7s %s\n", r->id, runway_status_name(r->status),
            aircraft_category_name(r->max_category),
            r->current_flight_id[0] != '\0' ? r->current_flight_id : "-");
}

void runway_list(FILE *out, const RunwayStore *rs)
{
    if (out == NULL || rs == NULL)
        return;
    if (rs->count == 0) {
        fprintf(out, "(no runways)\n");
        return;
    }
    fprintf(out, "%-15s %-9s %-7s %s\n", "ID", "STATUS", "MAX", "FLIGHT");
    for (int i = 0; i < rs->count; i++)
        runway_print(out, &rs->items[i]);
}

const char *runway_status_name(RunwayStatus s)
{
    switch (s) {
    case RUNWAY_AVAILABLE: return "AVAILABLE";
    case RUNWAY_OCCUPIED:  return "OCCUPIED";
    case RUNWAY_CLOSED:    return "CLOSED";
    default:               return "UNKNOWN";
    }
}
