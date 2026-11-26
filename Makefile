# Build: make            (Windows MinGW: mingw32-make)
# Real SQLite: put sqlite3.c and sqlite3.h from the SQLite "amalgamation" into backend/vendor/
CC      ?= gcc
CFLAGS  ?= -std=c11 -O2 -Wall -Wextra -Iinclude
LIBS    ?=
EXE     :=
ifeq ($(OS),Windows_NT)
  EXE   := .exe
  LIBS  += -lbcrypt -lws2_32
else
  LIBS  += -lm
endif

ifeq ($(COMPAT),1)            # test-only: system libsqlite3 + minimal header (no sqlite3.h installed)
  CFLAGS += -Icompat
  SQLITE_OBJ :=
  LIBS += $(SQLITE_LIB)
else ifneq ($(wildcard vendor/sqlite3.c),)
  CFLAGS += -Ivendor
  SQLITE_OBJ := build/sqlite3.o
  ifneq ($(OS),Windows_NT)
    LIBS += -ldl -lpthread
  endif
else
  SQLITE_OBJ :=
  LIBS += -lsqlite3
endif

COMMON := build/sha256.o build/password.o

all: build/init_admin$(EXE) build/unisphere_server$(EXE)

build/init_admin$(EXE): build/init_admin.o $(COMMON) $(SQLITE_OBJ)
	$(CC) -o $@ $^ $(LIBS)

SERVER := build/main.o build/http.o build/json.o build/api.o build/auth.o build/validate.o build/util.o build/fields.o build/clubs.o build/events.o build/registrations.o build/announcements.o build/router.o
build/unisphere_server$(EXE): $(SERVER) $(COMMON) $(SQLITE_OBJ)
	$(CC) -o $@ $^ $(LIBS)

build/test_password$(EXE): ../tests/test_password.c $(COMMON)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

build/%.o: src/%.c | build
	$(CC) $(CFLAGS) -c -o $@ $<

build/sqlite3.o: vendor/sqlite3.c | build
	$(CC) -O2 -DSQLITE_THREADSAFE=1 -Ivendor -c -o $@ $<

build:
	mkdir build

test: build/test_password$(EXE)
	./build/test_password$(EXE)

clean:
	rm -rf build
