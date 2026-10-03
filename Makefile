CC      = gcc
BASE    = -Wall -Wextra -std=c11 -Iinclude
CFLAGS  = $(BASE) $(shell pkg-config --cflags gtk4)
LDLIBS  = $(shell pkg-config --libs gtk4)

BUILD   = build
TARGET  = $(BUILD)/arms.exe
TEST    = $(BUILD)/test_core.exe

# Core modules: no GTK, shared by the app and the tests.
CORE    = src/common.c src/flight.c src/runway.c src/queue.c src/request.c
HEADERS = $(wildcard include/*.h)

all: $(TARGET)

$(TARGET): src/main.c $(CORE) $(HEADERS)
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) src/main.c $(CORE) -o $@ $(LDLIBS)

$(TEST): tests/test_core.c $(CORE) $(HEADERS)
	mkdir -p $(BUILD)
	$(CC) $(BASE) tests/test_core.c $(CORE) -o $@

test: $(TEST)
	./$(TEST)

run: all
	./$(TARGET)

clean:
	rm -rf $(BUILD)

.PHONY: all test run clean
