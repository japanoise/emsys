PROGNAME=emsys
PREFIX=/usr/local
VERSION?=git-$(shell git rev-parse --short HEAD)
BINDIR=$(PREFIX)/bin
MANDIR=$(PREFIX)/man/man1
OBJECTS=main.o wcwidth.o unicode.o buffer.o region.o undo.o transform.o find.o pipe.o tab.o register.o re.o fileio.o terminal.o display.o keymap.o edit.o prompt.o
CFLAGS+=-std=c99 -D_POSIX_C_SOURCE=200112L -Wall -Wextra -pedantic -Wno-pointer-sign -Werror=incompatible-pointer-types -DEMSYS_BUILD_DATE=\"$(shell date '+%Y-%m-%dT%H:%M:%S%z')\" -DEMSYS_VERSION=\"$(VERSION)\"

all: $(PROGNAME)

debug: CFLAGS+=-g -O0
debug: $(PROGNAME)

$(PROGNAME): config.h $(OBJECTS)
	$(CC) -o $@ $^ $(LDFLAGS)

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

test: tests/test_example tests/test_interactive
	./tests/test_example
	./tests/test_interactive

tests/test_example: tests/test_example.o tests/test_stubs.o
	$(CC) -o $@ $^ $(LDFLAGS)

tests/test_example.o: tests/test_example.c tests/test.h tests/test_stubs.h
	$(CC) $(CFLAGS) -c -o $@ $<

tests/test_stubs.o: tests/test_stubs.c tests/test_stubs.h
	$(CC) $(CFLAGS) -c -o $@ $<

# Interactive editor test - exclude main.o since it has main()
TEST_OBJS = buffer.o edit.o keymap.o fileio.o display.o terminal.o \
            undo.o region.o transform.o unicode.o wcwidth.o prompt.o \
            tab.o pipe.o register.o re.o find.o

tests/test_interactive: tests/test_interactive.o tests/test_stubs.o $(TEST_OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

tests/test_interactive.o: tests/test_interactive.c tests/test.h tests/test_stubs.h
	$(CC) $(CFLAGS) -c -o $@ $<

# Unit tests
tests/test_units: tests/test_units.o buffer.o unicode.o wcwidth.o undo.o
	$(CC) -o $@ $^ $(LDFLAGS)

tests/test_units.o: tests/test_units.c tests/test.h
	$(CC) $(CFLAGS) -c -o $@ $<

# Headless integration test - exclude conflicting modules
HEADLESS_OBJS = buffer.o edit.o keymap.o fileio.o \
                undo.o region.o transform.o unicode.o wcwidth.o \
                tab.o pipe.o register.o re.o find.o

tests/test_headless: tests/test_headless.o $(HEADLESS_OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

tests/test_headless.o: tests/test_headless.c
	$(CC) $(CFLAGS) -c -o $@ $<

# Test targets
test: tests/test_units tests/test_headless
	@echo "=== Running unit tests ==="
	./tests/test_units
	@echo ""
	@echo "=== Running integration tests ==="
	./tests/test_headless

test-unit: tests/test_units
	./tests/test_units

test-integration: tests/test_headless
	./tests/test_headless

test-help:
	./tests/test_runner.sh --help

clean:
	rm -rf *.o
	rm -rf *.exe
	rm -rf $(PROGNAME)
	rm -rf unicodetest
	rm -rf tests/*.o tests/test_example tests/test_interactive tests/test_units tests/test_headless
