#ifndef DEMO_H
#define DEMO_H

#include "system.h"

/* Builds the fixed demo scenario: 3 runways, 11 flights in assorted states, one completed
 * movement, an airport graph and the audit events those actions produce. Everything goes
 * through the public service/scheduler APIs, so it is validated and logged like real use.
 * Deterministic: same IDs, order, priorities and events every time. No GTK here.
 *
 * `out` is initialised by this function (pass an uninitialised or already-cleared system,
 * never a live one: it would leak the history and event lists). On any error `out` is
 * cleared back to empty and the status is returned. */
ArmsStatus demo_build(ArmsSystem *out);

#endif
