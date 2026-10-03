#ifndef PERSIST_H
#define PERSIST_H

#include "common.h"
#include "system.h"

/* File persistence: one human-readable text file (see persist.c for the format).
 * Only durable facts are written; the queues, holding queue and heap are saved as lists of
 * flight IDs and rebuilt through their normal APIs on load.
 *
 * Complexity (F flights, R runways, M history records, V graph nodes):
 *   save  O(F + R + M + V^2)      load  O(F^2 + M^2 + V^2)  (duplicate checks dominate) */

/* Writes s to path. The data goes to path.tmp first; the previous save is kept as path.bak.
 * Errors: INVALID_ARG (NULL, path too long, or a graph label containing a line break),
 * IO (could not write or rename; the previous save at path is left in place). */
ArmsStatus persist_save(const char *path, const ArmsSystem *s);

/* Reads path into dest. dest must already be initialised. The file is parsed into a
 * temporary system and fully validated; dest is replaced only if everything succeeds, and is
 * left untouched on any error.
 * Errors: NOT_FOUND (no such file), IO, FORMAT (malformed or inconsistent), INVALID_ID,
 * DUPLICATE, FULL (more records than a store can hold), NO_MEMORY.
 * Never falls back to path.bak; that file is for manual recovery. */
ArmsStatus persist_load(const char *path, ArmsSystem *dest);

#endif
