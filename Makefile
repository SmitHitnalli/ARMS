CC      = gcc
BASE    = -Wall -Wextra -std=c11 -Iinclude
CFLAGS  = $(BASE) $(shell pkg-config --cflags gtk4)
LDLIBS  = $(shell pkg-config --libs gtk4)

BUILD   = build
TARGET  = $(BUILD)/arms.exe
TEST    = $(BUILD)/test_core.exe

# Core modules: no GTK, shared by the app and the tests.
CORE    = src/common.c src/flight.c src/runway.c src/queue.c src/request.c src/cqueue.c src/holding.c src/heap.c src/scheduler.c src/history.c src/operations.c src/reports.c src/graph.c src/system.c src/persist.c src/eventlog.c src/service.c src/demo.c
HEADERS = $(wildcard include/*.h src/ui/*.h)

# GTK frontend: calls the core API, never the other way round. Linked into the app only.
UI      = src/ui/app.c src/ui/page_dashboard.c src/ui/page_flights.c src/ui/page_runways.c src/ui/page_scheduler.c src/ui/page_holding.c src/ui/page_history.c src/ui/page_graph.c src/ui/page_reports.c src/ui/page_eventlog.c

all: $(TARGET)

$(TARGET): src/main.c $(UI) $(CORE) $(HEADERS)
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) src/main.c $(UI) $(CORE) -o $@ $(LDLIBS)

$(TEST): tests/test_core.c $(CORE) $(HEADERS)
	mkdir -p $(BUILD)
	$(CC) $(BASE) -DARMS_TESTING tests/test_core.c $(CORE) -o $@

test: $(TEST)
	./$(TEST)

run: all
	./$(TARGET)

clean:
	rm -rf $(BUILD)

.PHONY: all test run clean
