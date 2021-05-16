PROGNAME=emsys
PREFIX=/usr/local
VERSION?=git-$(shell git rev-parse --short HEAD)
BINDIR=$(PREFIX)/bin
OBJECTS=main.o wcwidth.o unicode.o row.o region.o undo.o transform.o bound.o command.o
CFLAGS+=-Wall -Wextra -pedantic -Wno-pointer-sign -Werror=incompatible-pointer-types -DEMSYS_BUILD_DATE=\"$(shell date '+%Y-%m-%dT%H:%M:%S%z')\" -DEMSYS_VERSION=\"$(VERSION)\"

all: $(PROGNAME)

debug: CFLAGS+=-g -O0
debug: $(PROGNAME)

$(PROGNAME): $(OBJECTS)
	$(CC) -o $@ $^ $(LDFLAGS)

debug-unicodetest: CFLAGS+=-g -O0
debug-unicodetest: unicodetest

unicodetest: unicode.o unicodetest.o wcwidth.o
	$(CC) -o $@ $^ $(LDFLAGS)

install: $(PROGNAME)
	install -m 0755 $(PROGNAME) $(BINDIR)

clean:
	rm -rf *.o
	rm -rf *.exe
	rm -rf $(PROGNAME)
	rm -rf unicodetest
