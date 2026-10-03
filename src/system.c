#include "system.h"

void arms_system_init(ArmsSystem *s)
{
    if (s == NULL)
        return;
    flight_store_init(&s->flights);
    runway_store_init(&s->runways);
    request_init(&s->requests);
    holding_init(&s->holding);
    scheduler_init(&s->scheduler);
    history_init(&s->history);
    graph_init(&s->graph);
}

void arms_system_clear(ArmsSystem *s)
{
    if (s == NULL)
        return;
    history_clear(&s->history);
    arms_system_init(s);
}
