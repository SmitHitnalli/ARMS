# ARMS — Airport Runway Management System

A university DSA project written in C with a GTK desktop frontend. ARMS simulates airport runway scheduling, with a focus on implementing core data structures by hand.

**Status:** In Development

## Planned Features
- Runway scheduling using a priority queue (max heap)
- Flight queues using linked lists, queues, and circular queues
- Searching and sorting of flight records
- Airport/taxiway graph with BFS and DFS
- File-based persistence and event/audit logging
- Optional DSA visualization (developer view)

## Demo

Click **Load Demo** in the header bar to fill ARMS with a fixed, repeatable scenario. If data already exists you are asked to confirm replacing it; Cancel changes nothing. The demo is loaded in memory only: it reaches `data/arms.dat` when you click Save or close the app.

It seeds 3 runways (RWY-01 LARGE, RWY-02 MEDIUM, RWY-03 SMALL closed), 11 flights, one completed movement (AI101), an airport graph (with an isolated `MAINT` node) and the matching audit events.

Suggested walkthrough:
1. Dashboard, Flights, Runways: 11 flights, 7 waiting, 1 holding, 2 registered, 1 completed, 1 emergency.
2. Scheduler: press Pull so DL410 and TK150 join the heap. Priority: AF150 (emergency), BA202 (critical fuel), KL330 (low fuel), then DL410, SQ610, EK505, TK150 (arrivals before departures).
3. Holding: LH720 is already parked; move SQ610 in and out.
4. Schedule Next puts AF150 on RWY-01; Start it. Schedule Next again: BA202 (LARGE) is blocked, so KL330 takes RWY-02. Complete AF150.
5. Movement History (2 records) and Event Log (filter by type or flight).
6. Airport Graph: BFS/DFS from TERMINAL; reachability TERMINAL to RWY-02-ENT is yes, to MAINT is no.
7. Reports: sort by priority, binary-search AF150. Save, restart, and check everything is restored.
