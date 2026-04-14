CC = gcc
CFLAGS = -std=c17 -Wall -Wextra -O2 $(shell pkg-config --cflags libadwaita-1 webkitgtk-6.0)
LDFLAGS = $(shell pkg-config --libs libadwaita-1 webkitgtk-6.0) -lpthread -ldl -lm

BUILDDIR = build
SRC = src/main.c src/window.c src/settings.c src/actions.c src/database.c src/highlight.c
OBJ = $(patsubst src/%.c,$(BUILDDIR)/%.o,$(SRC))
SQLITE_OBJ = $(BUILDDIR)/sqlite3.o
BIN = $(BUILDDIR)/notes-desktop

all: $(BIN)

$(BIN): $(OBJ) $(SQLITE_OBJ)
	$(CC) $(OBJ) $(SQLITE_OBJ) -o $@ $(LDFLAGS)

$(BUILDDIR)/%.o: src/%.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(SQLITE_OBJ): src/sqlite3/sqlite3.c | $(BUILDDIR)
	$(CC) -std=c17 -O2 -DSQLITE_ENABLE_FTS5 -w -c $< -o $@

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

clean:
	rm -rf $(BUILDDIR)

.PHONY: all clean
