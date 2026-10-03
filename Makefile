CC      = gcc
CFLAGS  = -Wall -Wextra -std=c11 $(shell pkg-config --cflags gtk4)
LDLIBS  = $(shell pkg-config --libs gtk4)

BUILD   = build
TARGET  = $(BUILD)/arms.exe

all: $(TARGET)

$(TARGET): src/main.c
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $< -o $@ $(LDLIBS)

run: all
	./$(TARGET)

clean:
	rm -rf $(BUILD)

.PHONY: all run clean
