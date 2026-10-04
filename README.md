# ARMS — Airport Runway Management System

## Project Overview

ARMS is a desktop application that manages flights and runways at an airport: it registers flights, queues landing and takeoff requests, ranks them by urgency, assigns them to compatible runways, and keeps a history and audit trail of what happened.

It is a Data Structures and Algorithms (DSA) lab project written in C. Every major data structure is implemented by hand and has a real role in the system; no library containers are used. The frontend is a GTK4 desktop application, state is saved with ordinary C file I/O (no database), and a deterministic seeded demo scenario can be loaded with one click.

## Problem Statement

An airport has a small number of runways and many aircraft that want to use them. ARMS models the decisions this involves:

- **Landing and takeoff requests** arrive over time and must be served fairly.
- **Emergencies and low fuel** must be served before routine traffic.
- **Holding:** a flight can be parked in a holding pattern and rejoin later.
- **Runway compatibility:** a runway only accepts aircraft up to its maximum category, and only when it is available.
- **Movement history:** completed runway movements are recorded.
- **Airport connectivity:** terminals, taxiways and runway entries form a graph that can be traversed and checked for reachability.

## Key Features

- **Flight registration:** ID, type (ARRIVAL/DEPARTURE), category, emergency flag, fuel level.
- **Runway management:** add runways, set maximum category, close and reopen them.
- **Landing/takeoff queues:** separate FIFO request queues.
- **Circular holding queue:** flights can enter and leave holding.
- **Max-heap priority scheduling:** waiting flights are ranked by priority.
- **Emergency and fuel priority:** changing either re-ranks the flight.
- **Runway compatibility:** only compatible, AVAILABLE runways are assigned.
- **Operations:** schedule next, start, complete, and cancel (where allowed).
- **Movement history:** linked list of completed movements.
- **Event/audit log:** linked list of successful operational actions, with filtering.
- **Airport graph:** nodes, undirected edges, BFS, DFS and reachability checks.
- **Reports:** sorted flight and movement reports, and binary search by flight ID.
- **Persistence:** save and load through a single text file.
- **Seeded demo mode:** a fixed, repeatable scenario.
- **GTK4 GUI:** nine pages, with optional developer views of the raw structures.

## DSA Mapping

| Data structure / algorithm | Where it is used |
|---|---|
| Array | `FlightStore`, `RunwayStore` |
| Queue | Landing and takeoff request queues |
| Circular queue | Holding flights |
| Max heap | Priority scheduler |
| Linked list | Movement history, event log |
| Graph (adjacency matrix) | Airport connectivity |
| BFS / DFS | Graph traversal, reachability |
| Linear search | Standard lookups (flights, runways, history) |
| Binary search | Flight ID lookup on sorted report data |
| Insertion sort | Stable report sorting (`qsort` is not used) |
| File handling | Save/load of persistent state |

## Scheduling Logic

Waiting flights are ordered by this priority, highest first:

1. Emergency
2. Fuel urgency (critical, then low, then normal)
3. ARRIVAL before DEPARTURE
4. `request_tick` (earlier first)
5. `request_seq` (earlier first)

Rules that follow from the implementation:

- **Aircraft category is not a priority input.** It only controls runway compatibility.
- The scheduler only assigns a flight to a runway that is **AVAILABLE** and **compatible** with the flight's category.
- If the highest-priority flight has no compatible available runway, it **stays waiting** and a lower-priority flight that does have one can be scheduled. If no waiting flight can use any runway, nothing changes and the UI reports that no runway is available.
- Runways are tried in the order they were added.

## Flight Lifecycle

```
REGISTERED -> WAITING <-> HOLDING
WAITING -> SCHEDULED -> IN_OPERATION -> COMPLETED
```

- `REGISTERED -> WAITING` happens when the request is submitted to the landing or takeoff queue.
- `WAITING <-> HOLDING`: entering holding removes the flight from its queue or the heap; leaving returns it to the rear of the correct normal queue.
- `WAITING -> SCHEDULED` assigns a runway (the runway becomes OCCUPIED).
- `SCHEDULED -> IN_OPERATION -> COMPLETED` releases the runway and records a movement.
- **Cancellation** is allowed from REGISTERED, WAITING, HOLDING and SCHEDULED (a SCHEDULED flight releases its runway). IN_OPERATION flights cannot be cancelled.
- A flight is in only one active queue structure at a time.

## System Architecture

| Layer | Role |
|---|---|
| Core data structures | Flight/runway stores, queues, circular queue, heap, history, event log, graph. No GTK dependency. |
| Scheduler / operations | Priority comparison, heap scheduling, runway assignment, start, complete, cancel, invariant checks. |
| Service layer (`svc_*`) | GTK-free wrappers around operations that log an audit event for each successful action. |
| Persistence | Reads and writes the whole `ArmsSystem` aggregate. |
| UI | GTK4 pages that call the core and service APIs and contain no scheduling logic. |
| Demo builder | Builds the fixed demo scenario through the service layer. |

## Project Structure

```
include/        Public headers for the core modules
src/            Core modules and main.c
  flight, runway, queue, request, cqueue, holding, heap, scheduler,
  operations, history, reports, graph, eventlog, service,
  system, persist, demo, common
src/ui/         GTK4 frontend
  app, ui_common, page_dashboard, page_flights, page_runways,
  page_scheduler, page_holding, page_history, page_graph,
  page_reports, page_eventlog
tests/          test_core.c (GTK-free core test suite)
assets/css/     arms.css (GTK stylesheet)
data/           Save file location (arms.dat is not tracked by git)
```

## Tech Stack

- C11
- GTK4
- GCC (MinGW UCRT64)
- Makefile
- Standard C file I/O

## Build Requirements

Tested environment:

- Windows
- MSYS2 UCRT64 shell
- GCC and `mingw32-make` from the UCRT64 toolchain
- GTK4 and `pkg-config` (UCRT64 packages)

The project has been built and run only in this environment. Other platforms have not been tested. The core modules and tests need no GTK; only the application does.

## Build Instructions

Run these from the project root in an MSYS2 UCRT64 shell:

```
mingw32-make clean    # remove build/
mingw32-make test     # build and run the core tests (no GTK needed)
mingw32-make          # build the application: build/arms.exe
mingw32-make run      # build if needed, then launch build/arms.exe
```

Compilation uses `-Wall -Wextra -std=c11`.

## Demo Mode

Click **Load Demo** in the header bar to fill ARMS with a fixed, repeatable scenario.

- The data is the same every time (deterministic).
- The demo is loaded **in memory only**. It is written to `data/arms.dat` when you click **Save** or close the app cleanly.
- If the system already has data, you are asked to confirm replacing it. Cancel changes nothing.
- If startup loading of the save file failed, saving is disabled to protect the existing file, and Load Demo is disabled too.

Representative seeded data:

- 3 runways (RWY-01 LARGE, RWY-02 MEDIUM, RWY-03 SMALL and closed)
- 11 flights, including one emergency, critical and low fuel cases, one flight in holding, two registered flights and several waiting
- 1 completed movement (AI101)
- An 8-node airport graph that includes an isolated `MAINT` node
- 31 audit events

## Recommended Demo Walkthrough

1. Click **Load Demo** and open the **Dashboard** (11 flights, 7 waiting, 1 emergency).
2. **Scheduler:** look at the landing and takeoff queues.
3. Press **Pull** to move waiting flights into the heap (DL410 and TK150 join).
4. Check the priority preview: AF150 (emergency), BA202 (critical fuel), KL330 (low fuel), then DL410, SQ610, EK505, TK150.
5. **Holding:** LH720 is already parked; move SQ610 in and out.
6. **Schedule Next:** AF150 goes to RWY-01. Start it.
7. Schedule Next again: BA202 needs a LARGE runway, which is occupied, so it is blocked and KL330 takes RWY-02.
8. Complete AF150.
9. **Movement History** now has 2 records.
10. **Event Log:** filter by event type or flight ID.
11. **Airport Graph:** run BFS and DFS from TERMINAL. Reachability from TERMINAL to RWY-02-ENT is yes; to MAINT is no.
12. **Reports:** sort by priority and binary-search AF150.
13. **Save**, relaunch, and check that everything is restored.

## Persistence

- Save file: `data/arms.dat`, a text file written with `fopen`/`fprintf`/`fgets`.
- The current format header is `ARMS-SAVE 2`. Files with the `ARMS-SAVE 1` header still load; they have no event section, so the event log starts empty.
- **Safe save:** data is written to `arms.dat.tmp` first and the previous good save is kept as `arms.dat.bak`. The `.bak` file is for manual recovery and is never loaded automatically.
- **All-or-nothing load:** the file is loaded into a temporary system and replaces the live one only if the whole load succeeds. A failed load leaves the live system unchanged.
- The heap's internal array layout is not stored; only membership is saved, and heap order is rebuilt on load.
- A missing save file is treated as a normal first run.

## Event Log

- A manually implemented singly linked list with head, tail and sequence numbers.
- A successful operational action (register, add runway, submit request, hold, emergency/fuel change, schedule, start, complete, cancel, runway close/reopen) appends one event through the service layer. Failed actions log nothing.
- Logging is best-effort: if an event cannot be allocated, the operation still succeeds and a `dropped` counter is incremented.
- Events are stored chronologically; the UI displays them newest first.
- The page can filter by event type and by flight ID.
- The log is saved and restored with the rest of the state.

## Complexity Summary

n is the number of items in the structure; V is the number of graph nodes (at most 16).

| Operation | Time |
|---|---|
| Array lookup by ID (linear search) | O(n) |
| Normal queue enqueue | O(1) usually; O(n) when compaction reclaims freed slots |
| Normal queue dequeue | O(1) |
| Normal queue remove by ID | O(n) |
| Holding (circular) enqueue / dequeue | O(1) |
| Holding remove by ID | O(n) |
| Heap insert / pop | O(log n) (insert also does an O(n) duplicate check) |
| Heap remove by ID | O(n) find + O(log n) repair |
| Stable insertion sort (reports) | O(n^2) worst/average; O(n) if already ordered |
| Binary search (sorted flight IDs) | O(log n) |
| BFS / DFS (adjacency matrix) | O(V^2) |
| Linked-list append (history, event log) | O(1) |

## Testing

`mingw32-make test` builds and runs `tests/test_core.c`, a GTK-free assert-based suite. It covers:

- Core structures, state transitions and scheduling logic
- Persistence round-trip tests, including ARMS-SAVE 1 compatibility
- Malformed and missing file load tests
- Allocation-failure tests (history, event log, operations)
- Service and event-log tests
- A demo end-to-end integration test (counts, priority order, blocked flight, save/load, determinism)

The build is recorded as passing with zero warnings. There is no GUI automation; GUI behaviour was verified manually.

## Design Decisions

- The core is independent of GTK and never includes GTK headers, so it is tested on its own.
- The service layer is responsible for operational logging; core APIs are unchanged by it.
- The UI does not implement scheduling logic; it only calls the core and service APIs.
- The heap and queues store flight IDs, not duplicate `Flight` structs.
- Reports sort temporary pointer arrays, so the canonical stores are never reordered.
- DSA visualizations are optional: developer views are collapsed by default.

## Limitations

- No real-time simulation clock. `request_tick` and the logical time stay at 0, so waiting time does not yet affect priority and `request_seq` provides the effective FIFO order.
- No database; persistence is a single text file.
- No real airport integration or live data.
- GUI testing is manual.
- Capacities are fixed: 100 flights, 10 runways, 16 graph nodes (queues, holding and heap are sized to the flight limit).

## Future Scope

- A simulation clock so waiting time affects priority
- Dynamic capacities
- Live airport data or API integration
- Richer visualization
- Automated GUI tests

## Academic Relevance

ARMS uses different data structures for different operational problems rather than for demonstration alone: a queue for fair request order, a circular queue for holding, a heap for priority, linked lists for append-only records, and a graph for connectivity. The algorithms (heap sift, insertion sort, binary search, BFS, DFS) are written by hand, and their complexity can be read directly from the code and observed in the developer views.
