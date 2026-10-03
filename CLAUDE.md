# ARMS — Airport Runway Management System

University DSA project in C with a GTK desktop frontend. Product/design source of truth is the Notion page "ARMS - Airport Runway Management System"; do not duplicate its specs here.

## Architecture
- Core DSA logic is fully independent of GTK/UI code. Core modules must never include GTK headers.
- The GTK frontend only calls the core API.
- Do not silently change architecture or major project decisions; ask first.

## DSA (implement manually, no library containers)
Arrays, linked lists, queues, circular queues, max heap / priority queue, searching, sorting, graphs, BFS, DFS.

## Persistence and logging
- Persistence uses ordinary C file handling (`fopen`/`fread`/etc.). No SQLite or any database.
- Event/audit logging is part of the system.

## Visualization
DSA visualizations must be optional/togglable or live in a separate developer view.

## Code style
- Simple, modular, student-readable, viva-friendly C.
- Compile with warnings enabled (e.g. `-Wall -Wextra`).
- Test core modules independently from GTK.

## Workflow
- Work phase by phase.
- Before a substantial implementation phase, explain the plan and affected files first.
- Use the brain (SmitKaDimaag) selectively for prior decisions; don't load unnecessary context.
- Update brain state/tasks only at meaningful milestones.
