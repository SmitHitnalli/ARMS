/* Save file format (text, one record per line, fields separated by single spaces):
 *
 *   ARMS-SAVE 1
 *   FLIGHTS <count> <now> <next_seq>
 *   F <id> <type> <category> <state> <emergency> <fuel> <request_tick> <request_seq> <runway|~>
 *   RUNWAYS <count>
 *   R <id> <status> <max_category> <current_flight|~>
 *   LANDING <count>      then   L <id>   in FIFO order (front first)
 *   TAKEOFF <count>      then   T <id>   in FIFO order
 *   HOLDING <count>      then   H <id>   in FIFO order
 *   HEAP <count>         then   P <id>   membership only (heap is rebuilt on load)
 *   HISTORY <count>      then   M <flight> <type> <category> <runway> <req_tick> <req_seq> <done_tick>
 *   NODES <count>        then   N <id> [label to end of line]
 *   EDGES <count>        then   E <id> <id>        (each undirected edge once)
 *   END
 *
 * "~" means "none" (it can never be a valid ID). Enums are written by name. */

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "operations.h"
#include "persist.h"

#define SAVE_HEADER  "ARMS-SAVE 1"
#define NONE_TOKEN   "~"
#define LINE_LEN     160
#define PATH_LEN     512

enum { LIST_LANDING, LIST_TAKEOFF, LIST_HOLDING, LIST_HEAP, LIST_COUNT };

typedef struct {
    char ids[MAX_FLIGHTS][ID_MAX_LEN + 1];
    int  n;
} IdList;

/* ---------- enum names (the flight/runway ones already exist in their modules) ---------- */

static const char *emergency_name(EmergencyLevel e)
{
    return e == EMERGENCY_ACTIVE ? "ACTIVE" : "NONE";
}

static const char *fuel_name(FuelLevel f)
{
    switch (f) {
    case FUEL_LOW:      return "LOW";
    case FUEL_CRITICAL: return "CRITICAL";
    default:            return "NORMAL";
    }
}

/* Name -> enum: try every value and compare with its printed name. */
#define DEFINE_ENUM_PARSER(fn, type, count, namefn)                       \
    static int fn(const char *s, type *out)                               \
    {                                                                     \
        for (int v = 0; v < (count); v++) {                               \
            if (strcmp(namefn((type)v), s) == 0) {                        \
                *out = (type)v;                                           \
                return 1;                                                 \
            }                                                             \
        }                                                                 \
        return 0;                                                         \
    }

DEFINE_ENUM_PARSER(parse_type,      FlightType,       2,           flight_type_name)
DEFINE_ENUM_PARSER(parse_category,  AircraftCategory, 3,           aircraft_category_name)
DEFINE_ENUM_PARSER(parse_state,     FlightState,      STATE_COUNT, flight_state_name)
DEFINE_ENUM_PARSER(parse_status,    RunwayStatus,     3,           runway_status_name)
DEFINE_ENUM_PARSER(parse_emergency, EmergencyLevel,   2,           emergency_name)
DEFINE_ENUM_PARSER(parse_fuel,      FuelLevel,        3,           fuel_name)

/* ======================================================================= */
/* Save                                                                    */
/* ======================================================================= */

static const char *id_or_none(const char *id)
{
    return id[0] != '\0' ? id : NONE_TOKEN;
}

static void write_queue_section(FILE *f, const char *name, const char *tag,
                                const char *(*at)(const void *, int),
                                const void *q, int count)
{
    fprintf(f, "%s %d\n", name, count);
    for (int i = 0; i < count; i++)
        fprintf(f, "%s %s\n", tag, at(q, i));
}

static const char *queue_at_v(const void *q, int i)  { return queue_at((const Queue *)q, i); }
static const char *cqueue_at_v(const void *q, int i) { return cqueue_at((const CircularQueue *)q, i); }

static void write_system(FILE *f, const ArmsSystem *s)
{
    fprintf(f, "%s\n", SAVE_HEADER);

    fprintf(f, "FLIGHTS %d %d %u\n", s->flights.count, s->flights.now, s->flights.next_seq);
    for (int i = 0; i < s->flights.count; i++) {
        const Flight *fl = &s->flights.items[i];
        fprintf(f, "F %s %s %s %s %s %s %d %u %s\n", fl->id, flight_type_name(fl->type),
                aircraft_category_name(fl->category), flight_state_name(fl->state),
                emergency_name(fl->emergency), fuel_name(fl->fuel), fl->request_tick,
                fl->request_seq, id_or_none(fl->runway_id));
    }

    fprintf(f, "RUNWAYS %d\n", s->runways.count);
    for (int i = 0; i < s->runways.count; i++) {
        const Runway *r = &s->runways.items[i];
        fprintf(f, "R %s %s %s %s\n", r->id, runway_status_name(r->status),
                aircraft_category_name(r->max_category), id_or_none(r->current_flight_id));
    }

    write_queue_section(f, "LANDING", "L", queue_at_v, &s->requests.landing,
                        queue_count(&s->requests.landing));
    write_queue_section(f, "TAKEOFF", "T", queue_at_v, &s->requests.takeoff,
                        queue_count(&s->requests.takeoff));
    write_queue_section(f, "HOLDING", "H", cqueue_at_v, &s->holding.queue,
                        cqueue_count(&s->holding.queue));

    /* Heap: membership only, written in flight-store order so the text is deterministic. */
    fprintf(f, "HEAP %d\n", heap_count(&s->scheduler.heap));
    for (int i = 0; i < s->flights.count; i++) {
        if (heap_contains(&s->scheduler.heap, s->flights.items[i].id))
            fprintf(f, "P %s\n", s->flights.items[i].id);
    }

    fprintf(f, "HISTORY %d\n", s->history.count);
    for (const MovementNode *n = s->history.head; n != NULL; n = n->next) {
        const Movement *m = &n->data;
        fprintf(f, "M %s %s %s %s %d %u %d\n", m->flight_id, flight_type_name(m->type),
                aircraft_category_name(m->category), m->runway_id, m->request_tick,
                m->request_seq, m->completed_tick);
    }

    fprintf(f, "NODES %d\n", s->graph.count);
    for (int i = 0; i < s->graph.count; i++) {
        const GraphNode *n = &s->graph.nodes[i];
        if (n->label[0] != '\0')
            fprintf(f, "N %s %s\n", n->id, n->label);
        else
            fprintf(f, "N %s\n", n->id);
    }

    int edges = 0;
    for (int i = 0; i < s->graph.count; i++)
        for (int j = i + 1; j < s->graph.count; j++)
            edges += s->graph.adj[i][j] ? 1 : 0;
    fprintf(f, "EDGES %d\n", edges);
    for (int i = 0; i < s->graph.count; i++)
        for (int j = i + 1; j < s->graph.count; j++)
            if (s->graph.adj[i][j])
                fprintf(f, "E %s %s\n", s->graph.nodes[i].id, s->graph.nodes[j].id);

    fprintf(f, "END\n");
}

ArmsStatus persist_save(const char *path, const ArmsSystem *s)
{
    if (path == NULL || s == NULL || strlen(path) + 5 > PATH_LEN)
        return ARMS_ERR_INVALID_ARG;

    /* A line break in a label would corrupt the line-based file. */
    for (int i = 0; i < s->graph.count; i++)
        if (strpbrk(s->graph.nodes[i].label, "\r\n") != NULL)
            return ARMS_ERR_INVALID_ARG;

    char tmp[PATH_LEN], bak[PATH_LEN];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    snprintf(bak, sizeof bak, "%s.bak", path);

    FILE *f = fopen(tmp, "w");
    if (f == NULL)
        return ARMS_ERR_IO;
    write_system(f, s);
    int bad = ferror(f);            /* sticky: set if any fprintf above failed */
    if (fflush(f) != 0)
        bad = 1;
    if (fclose(f) != 0)
        bad = 1;
    if (bad) {
        remove(tmp);
        return ARMS_ERR_IO;
    }

    /* Windows rename() refuses to overwrite, so move the old save aside first. */
    int had_old = 0;
    FILE *old = fopen(path, "r");
    if (old != NULL) {
        fclose(old);
        had_old = 1;
        remove(bak);
        if (rename(path, bak) != 0) {
            remove(tmp);
            return ARMS_ERR_IO;
        }
    }
    if (rename(tmp, path) != 0) {
        if (had_old)
            rename(bak, path);      /* put the previous save back */
        remove(tmp);
        return ARMS_ERR_IO;
    }
    return ARMS_OK;
}

/* ======================================================================= */
/* Load: parse into a temporary system, validate, then swap in             */
/* ======================================================================= */

typedef struct {
    FILE *f;
    char  line[LINE_LEN];
} Reader;

static ArmsStatus next_line(Reader *r)
{
    if (fgets(r->line, sizeof r->line, r->f) == NULL)
        return ferror(r->f) ? ARMS_ERR_IO : ARMS_ERR_FORMAT;   /* EOF = truncated file */
    size_t n = strlen(r->line);
    if (n > 0 && r->line[n - 1] == '\n')
        r->line[--n] = '\0';
    else if (n == sizeof r->line - 1)
        return ARMS_ERR_FORMAT;                                 /* line too long */
    if (n > 0 && r->line[n - 1] == '\r')
        r->line[n - 1] = '\0';
    return ARMS_OK;
}

/* Splits line in place on single spaces into at most max fields; the last field keeps any
 * remaining spaces. Returns the number of fields. */
static int split(char *line, char *fields[], int max)
{
    int n = 0;
    char *p = line;
    while (n < max) {
        fields[n++] = p;
        if (n == max)
            break;
        char *sp = strchr(p, ' ');
        if (sp == NULL)
            break;
        *sp = '\0';
        p = sp + 1;
    }
    return n;
}

static int parse_ll(const char *s, long long lo, long long hi, long long *out)
{
    if (*s == '\0')
        return 0;
    char *end;
    errno = 0;
    long long v = strtoll(s, &end, 10);
    if (errno != 0 || *end != '\0' || v < lo || v > hi)
        return 0;
    *out = v;
    return 1;
}

/* An ID field; allow_none accepts "~" and stores "". */
static ArmsStatus parse_id(const char *s, char *dest, int allow_none)
{
    if (allow_none && strcmp(s, NONE_TOKEN) == 0) {
        dest[0] = '\0';
        return ARMS_OK;
    }
    if (!arms_id_valid(s))
        return ARMS_ERR_INVALID_ID;
    strcpy(dest, s);
    return ARMS_OK;
}

/* Reads one line that must be exactly `fields` fields long and start with `tag`. */
static ArmsStatus read_record(Reader *r, const char *tag, int fields, char *out[])
{
    ArmsStatus st = next_line(r);
    if (st != ARMS_OK)
        return st;
    if (split(r->line, out, fields) != fields || strcmp(out[0], tag) != 0)
        return ARMS_ERR_FORMAT;
    return ARMS_OK;
}

/* "<NAME> <count>"; a count above max means the file cannot fit in the store. */
static ArmsStatus read_count(Reader *r, const char *name, long long max, int *count)
{
    char *f[2];
    long long n;
    ArmsStatus st = read_record(r, name, 2, f);
    if (st != ARMS_OK)
        return st;
    if (!parse_ll(f[1], 0, INT_MAX, &n))
        return ARMS_ERR_FORMAT;
    if (n > max)
        return ARMS_ERR_FULL;
    *count = (int)n;
    return ARMS_OK;
}

static ArmsStatus load_flights(Reader *r, ArmsSystem *t)
{
    char *f[4];
    long long count, now, next_seq;
    ArmsStatus st = read_record(r, "FLIGHTS", 4, f);
    if (st != ARMS_OK)
        return st;
    if (!parse_ll(f[1], 0, INT_MAX, &count) || !parse_ll(f[2], 0, INT_MAX, &now) ||
        !parse_ll(f[3], 0, UINT_MAX, &next_seq))
        return ARMS_ERR_FORMAT;
    if (count > MAX_FLIGHTS)
        return ARMS_ERR_FULL;
    t->flights.now = (int)now;
    t->flights.next_seq = (unsigned)next_seq;

    for (int i = 0; i < count; i++) {
        char *g[10];
        Flight fl;
        long long tick, seq;
        memset(&fl, 0, sizeof fl);

        st = read_record(r, "F", 10, g);
        if (st != ARMS_OK)
            return st;
        st = parse_id(g[1], fl.id, 0);
        if (st != ARMS_OK)
            return st;
        if (flight_find(&t->flights, fl.id) != NULL)
            return ARMS_ERR_DUPLICATE;
        if (!parse_type(g[2], &fl.type) || !parse_category(g[3], &fl.category) ||
            !parse_state(g[4], &fl.state) || !parse_emergency(g[5], &fl.emergency) ||
            !parse_fuel(g[6], &fl.fuel) || !parse_ll(g[7], 0, INT_MAX, &tick) ||
            !parse_ll(g[8], 0, UINT_MAX, &seq))
            return ARMS_ERR_FORMAT;
        st = parse_id(g[9], fl.runway_id, 1);
        if (st != ARMS_OK)
            return st;
        fl.request_tick = (int)tick;
        fl.request_seq = (unsigned)seq;

        /* Flights never submitted keep request_seq 0, so only non-zero numbers are checked:
         * they must be below next_seq and unique. */
        if (seq != 0) {
            if (seq >= next_seq)
                return ARMS_ERR_FORMAT;
            for (int j = 0; j < t->flights.count; j++)
                if (t->flights.items[j].request_seq == fl.request_seq)
                    return ARMS_ERR_FORMAT;
        }
        t->flights.items[t->flights.count++] = fl;
    }
    return ARMS_OK;
}

static ArmsStatus load_runways(Reader *r, ArmsSystem *t)
{
    int count;
    ArmsStatus st = read_count(r, "RUNWAYS", MAX_RUNWAYS, &count);
    if (st != ARMS_OK)
        return st;

    for (int i = 0; i < count; i++) {
        char *g[5];
        Runway rw;
        memset(&rw, 0, sizeof rw);

        st = read_record(r, "R", 5, g);
        if (st != ARMS_OK)
            return st;
        st = parse_id(g[1], rw.id, 0);
        if (st != ARMS_OK)
            return st;
        if (runway_find(&t->runways, rw.id) != NULL)
            return ARMS_ERR_DUPLICATE;
        if (!parse_status(g[2], &rw.status) || !parse_category(g[3], &rw.max_category))
            return ARMS_ERR_FORMAT;
        st = parse_id(g[4], rw.current_flight_id, 1);
        if (st != ARMS_OK)
            return st;
        t->runways.items[t->runways.count++] = rw;
    }
    return ARMS_OK;
}

static ArmsStatus load_id_list(Reader *r, const char *name, const char *tag, IdList *list)
{
    int count;
    ArmsStatus st = read_count(r, name, MAX_FLIGHTS, &count);
    if (st != ARMS_OK)
        return st;
    for (int i = 0; i < count; i++) {
        char *g[2];
        st = read_record(r, tag, 2, g);
        if (st != ARMS_OK)
            return st;
        st = parse_id(g[1], list->ids[i], 0);
        if (st != ARMS_OK)
            return st;
    }
    list->n = count;
    return ARMS_OK;
}

/* Checks every WAITING/HOLDING flight is in exactly one structure of the right kind and
 * nothing else is, then fills the real queues and heap through their normal APIs. */
static ArmsStatus rebuild_active(ArmsSystem *t, const IdList lists[LIST_COUNT])
{
    int seen[MAX_FLIGHTS] = { 0 };

    for (int k = 0; k < LIST_COUNT; k++) {
        for (int i = 0; i < lists[k].n; i++) {
            Flight *fl = flight_find(&t->flights, lists[k].ids[i]);
            if (fl == NULL)
                return ARMS_ERR_FORMAT;
            if (fl->state != (k == LIST_HOLDING ? STATE_HOLDING : STATE_WAITING))
                return ARMS_ERR_FORMAT;
            if (k == LIST_LANDING && fl->type != FLIGHT_ARRIVAL)
                return ARMS_ERR_FORMAT;
            if (k == LIST_TAKEOFF && fl->type != FLIGHT_DEPARTURE)
                return ARMS_ERR_FORMAT;
            if (seen[fl - t->flights.items]++ > 0)
                return ARMS_ERR_DUPLICATE;
        }
    }
    for (int i = 0; i < t->flights.count; i++) {
        FlightState st = t->flights.items[i].state;
        if ((st == STATE_WAITING || st == STATE_HOLDING) && !seen[i])
            return ARMS_ERR_FORMAT;
    }

    for (int i = 0; i < lists[LIST_LANDING].n; i++) {
        ArmsStatus st = queue_enqueue(&t->requests.landing, lists[LIST_LANDING].ids[i]);
        if (st != ARMS_OK)
            return st;
    }
    for (int i = 0; i < lists[LIST_TAKEOFF].n; i++) {
        ArmsStatus st = queue_enqueue(&t->requests.takeoff, lists[LIST_TAKEOFF].ids[i]);
        if (st != ARMS_OK)
            return st;
    }
    for (int i = 0; i < lists[LIST_HOLDING].n; i++) {
        ArmsStatus st = cqueue_enqueue(&t->holding.queue, lists[LIST_HOLDING].ids[i]);
        if (st != ARMS_OK)
            return st;
    }
    for (int i = 0; i < lists[LIST_HEAP].n; i++) {
        ArmsStatus st = heap_insert(&t->scheduler.heap, &t->flights, lists[LIST_HEAP].ids[i]);
        if (st != ARMS_OK)
            return st;
    }
    return ARMS_OK;
}

/* Runway <-> flight links: the ops invariants plus runway compatibility. */
static ArmsStatus check_runway_links(ArmsSystem *t)
{
    if (!ops_check_invariants(&t->flights, &t->runways, &t->scheduler))
        return ARMS_ERR_FORMAT;
    for (int i = 0; i < t->flights.count; i++) {
        const Flight *fl = &t->flights.items[i];
        if (fl->state == STATE_SCHEDULED || fl->state == STATE_IN_OPERATION) {
            const Runway *rw = runway_find(&t->runways, fl->runway_id);
            if (rw == NULL || !runway_is_compatible(rw, fl))
                return ARMS_ERR_FORMAT;
        }
    }
    return ARMS_OK;
}

static ArmsStatus load_history(Reader *r, ArmsSystem *t)
{
    int count;
    ArmsStatus st = read_count(r, "HISTORY", INT_MAX, &count);
    if (st != ARMS_OK)
        return st;

    for (int i = 0; i < count; i++) {
        char *g[8];
        Movement m;
        long long tick, seq, done;
        memset(&m, 0, sizeof m);

        st = read_record(r, "M", 8, g);
        if (st != ARMS_OK)
            return st;
        st = parse_id(g[1], m.flight_id, 0);
        if (st != ARMS_OK)
            return st;
        st = parse_id(g[4], m.runway_id, 0);
        if (st != ARMS_OK)
            return st;
        if (!parse_type(g[2], &m.type) || !parse_category(g[3], &m.category) ||
            !parse_ll(g[5], 0, INT_MAX, &tick) || !parse_ll(g[6], 0, UINT_MAX, &seq) ||
            !parse_ll(g[7], 0, INT_MAX, &done))
            return ARMS_ERR_FORMAT;
        m.request_tick = (int)tick;
        m.request_seq = (unsigned)seq;
        m.completed_tick = (int)done;

        /* A movement belongs to a COMPLETED flight and a known runway, and is recorded once. */
        const Flight *fl = flight_find(&t->flights, m.flight_id);
        if (fl == NULL || fl->state != STATE_COMPLETED ||
            runway_find(&t->runways, m.runway_id) == NULL)
            return ARMS_ERR_FORMAT;
        if (history_find(&t->history, m.flight_id) != NULL)
            return ARMS_ERR_DUPLICATE;
        if (fl->type != m.type || fl->category != m.category ||
            fl->request_tick != m.request_tick || fl->request_seq != m.request_seq)
            return ARMS_ERR_FORMAT;

        st = history_append(&t->history, &m);
        if (st != ARMS_OK)
            return st;                                   /* NO_MEMORY */
    }
    return ARMS_OK;
}

static ArmsStatus load_graph(Reader *r, ArmsSystem *t)
{
    int count;
    ArmsStatus st = read_count(r, "NODES", MAX_NODES, &count);
    if (st != ARMS_OK)
        return st;

    for (int i = 0; i < count; i++) {
        char *g[3];
        st = next_line(r);
        if (st != ARMS_OK)
            return st;
        int n = split(r->line, g, 3);
        if (n < 2 || strcmp(g[0], "N") != 0)
            return ARMS_ERR_FORMAT;
        const char *label = n == 3 ? g[2] : "";
        if (strlen(label) >= NODE_LABEL_LEN)
            return ARMS_ERR_FORMAT;                      /* would be silently truncated */
        st = graph_add_node(&t->graph, g[1], label);
        if (st != ARMS_OK)
            return st;                                   /* INVALID_ID, DUPLICATE, FULL */
    }

    st = read_count(r, "EDGES", INT_MAX, &count);
    if (st != ARMS_OK)
        return st;
    for (int i = 0; i < count; i++) {
        char *g[3];
        st = read_record(r, "E", 3, g);
        if (st != ARMS_OK)
            return st;
        st = graph_add_edge(&t->graph, g[1], g[2]);
        if (st == ARMS_ERR_DUPLICATE)
            return st;
        if (st != ARMS_OK)
            return ARMS_ERR_FORMAT;                      /* unknown node or self-edge */
    }
    return ARMS_OK;
}

static ArmsStatus parse_system(FILE *file, ArmsSystem *t)
{
    Reader r;
    IdList lists[LIST_COUNT];
    ArmsStatus st;

    r.f = file;
    memset(lists, 0, sizeof lists);

    st = next_line(&r);
    if (st != ARMS_OK)
        return st;
    if (strcmp(r.line, SAVE_HEADER) != 0)
        return ARMS_ERR_FORMAT;                          /* wrong file or unknown version */

    if ((st = load_flights(&r, t)) != ARMS_OK)
        return st;
    if ((st = load_runways(&r, t)) != ARMS_OK)
        return st;
    if ((st = load_id_list(&r, "LANDING", "L", &lists[LIST_LANDING])) != ARMS_OK)
        return st;
    if ((st = load_id_list(&r, "TAKEOFF", "T", &lists[LIST_TAKEOFF])) != ARMS_OK)
        return st;
    if ((st = load_id_list(&r, "HOLDING", "H", &lists[LIST_HOLDING])) != ARMS_OK)
        return st;
    if ((st = load_id_list(&r, "HEAP", "P", &lists[LIST_HEAP])) != ARMS_OK)
        return st;
    if ((st = rebuild_active(t, lists)) != ARMS_OK)
        return st;
    if ((st = check_runway_links(t)) != ARMS_OK)
        return st;
    if ((st = load_history(&r, t)) != ARMS_OK)
        return st;
    if ((st = load_graph(&r, t)) != ARMS_OK)
        return st;

    st = next_line(&r);
    if (st != ARMS_OK)
        return st;
    if (strcmp(r.line, "END") != 0 || fgetc(file) != EOF)
        return ARMS_ERR_FORMAT;                          /* missing END or trailing junk */
    return ARMS_OK;
}

ArmsStatus persist_load(const char *path, ArmsSystem *dest)
{
    if (path == NULL || dest == NULL)
        return ARMS_ERR_INVALID_ARG;

    FILE *f = fopen(path, "r");
    if (f == NULL)
        return errno == ENOENT ? ARMS_ERR_NOT_FOUND : ARMS_ERR_IO;

    ArmsSystem temp;
    arms_system_init(&temp);
    ArmsStatus st = parse_system(f, &temp);
    fclose(f);

    if (st != ARMS_OK) {
        arms_system_clear(&temp);        /* frees any history nodes already built */
        return st;
    }
    arms_system_clear(dest);             /* free the old history, then take over temp's */
    *dest = temp;
    return ARMS_OK;
}
