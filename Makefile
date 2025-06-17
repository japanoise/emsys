PROGNAME=emsys
PREFIX=/usr/local
VERSION?=git-$(shell git rev-parse --short HEAD 2>/dev/null | sed 's/^/git-/' || echo "")
BINDIR=$(PREFIX)/bin
MANDIR=$(PREFIX)/man/man1
OBJECTS=main.o wcwidth.o unicode.o buffer.o region.o undo.o transform.o find.o pipe.o tab.o register.o re.o fileio.o terminal.o display.o keymap.o edit.o prompt.o compat.o

# Platform Detection: 3-Platform Strategy
# 
# emsys supports exactly 3 platforms:
# 1. POSIX   - Default for all Unix systems (Linux, *BSD, macOS, Solaris, AIX, HP-UX, etc.)
# 2. Android - Android/Termux with optimizations  
# 3. MSYS2   - Windows MSYS2 environment
#
# If no platform argument specified or platform is not android/msys2, assume POSIX.

UNAME_S = $(shell uname -s)

# Default: assume POSIX-compliant system
DETECTED_PLATFORM = posix

# Exception 1: Android/Termux detection (multiple methods for reliability)
ifdef ANDROID_ROOT
    DETECTED_PLATFORM = android
endif
ifneq (,$(TERMUX))
    DETECTED_PLATFORM = android
endif
ifeq ($(shell test -d /data/data/com.termux && echo termux),termux)
    DETECTED_PLATFORM = android
endif

# Exception 2: MSYS2 detection  
ifneq (,$(findstring MSYS_NT,$(UNAME_S)))
    DETECTED_PLATFORM = msys2
endif
ifneq (,$(findstring MINGW,$(UNAME_S)))
    DETECTED_PLATFORM = msys2
endif
ifneq (,$(findstring CYGWIN,$(UNAME_S)))
    DETECTED_PLATFORM = msys2
endif

# Allow override via command line: make PLATFORM=android
PLATFORM ?= $(DETECTED_PLATFORM)

CFLAGS+=-std=c99 -Wall -Wextra -pedantic -Wno-pointer-sign -Werror=incompatible-pointer-types -DEMSYS_BUILD_DATE=\"$(shell date '+%Y-%m-%dT%H:%M:%S%z')\" -DEMSYS_VERSION=\"$(VERSION)\"

# Optional feature disabling
DISABLE_PIPE ?= 0

# Apply platform-specific settings
ifeq ($(PLATFORM),android)
    CC = clang
    CFLAGS += -O2 -fPIC -fPIE -DNDEBUG
    LDFLAGS += -pie
    # Android disables pipe by default
    DISABLE_PIPE = 1
endif
ifeq ($(PLATFORM),msys2)
    CFLAGS += -D_GNU_SOURCE
endif
# All other platforms (posix) use standard POSIX C99 flags

# Apply optional feature flags
ifeq ($(DISABLE_PIPE),1)
    CFLAGS += -DEMSYS_DISABLE_PIPE
endif

all: $(PROGNAME)

debug: CFLAGS+=-g -O0
debug: $(PROGNAME)

$(PROGNAME): config.h $(OBJECTS)
	$(CC) -o $@ $(OBJECTS) $(LDFLAGS)

debug-unicodetest: CFLAGS+=-g -O0
debug-unicodetest: unicodetest

unicodetest: unicode.o unicodetest.o wcwidth.o
	$(CC) -o $@ $^ $(LDFLAGS)

install: $(PROGNAME) $(PROGNAME).1
	mkdir -pv $(BINDIR)
	mkdir -pv $(MANDIR)
	mkdir -pv $(PREFIX)/share/doc/
	install -m 0755 $(PROGNAME) $(BINDIR)
	install -m 0644 README.md $(PREFIX)/share/doc/$(PROGNAME).md
	install -m 0644 $(PROGNAME).1 $(MANDIR)/$(PROGNAME).1

config.h:
	cp config.def.h $@

format:
	clang-format -i *.c *.h

# Test stub infrastructure
tests/test_stubs.o: tests/test_stubs.c tests/test_stubs.h
	$(CC) $(CFLAGS) -c -o $@ $<

# Headless integration test - exclude conflicting modules
HEADLESS_OBJS = buffer.o edit.o keymap.o fileio.o \
                undo.o region.o transform.o unicode.o wcwidth.o \
                tab.o pipe.o register.o re.o find.o compat.o

tests/test_headless: tests/test_headless.o $(HEADLESS_OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

tests/test_headless.o: tests/test_headless.c
	$(CC) $(CFLAGS) -c -o $@ $<

# Test targets
test: tests/test_headless
	@echo "=== Running integration tests ==="
	./tests/test_headless

test-integration: tests/test_headless
	./tests/test_headless

test-help:
	./tests/test_runner.sh --help

clean:
	rm -rf *.o
	rm -rf *.exe
	rm -rf $(PROGNAME)
	rm -rf unicodetest
	rm -rf tests/*.o tests/test_headless
