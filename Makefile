PROGNAME=emsys
PREFIX=/usr/local
VERSION?=git-$(shell git rev-parse --short HEAD)
BINDIR=$(PREFIX)/bin
MANDIR=$(PREFIX)/man/man1
OBJECTS=main.o wcwidth.o unicode.o row.o region.o undo.o transform.o bound.o command.o find.o pipe.o tab.o register.o re.o
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

install: $(PROGNAME) $(PROGNAME).1
	mkdir -pv $(BINDIR)
	mkdir -pv $(MANDIR)
	mkdir -pv $(PREFIX)/share/doc/
	install -m 0755 $(PROGNAME) $(BINDIR)
	install -m 0644 README.md $(PREFIX)/share/doc/$(PROGNAME).md
	install -m 0644 $(PROGNAME).1 $(MANDIR)/$(PROGNAME).1

clean:
	rm -rf *.o
	rm -rf *.exe
	rm -rf $(PROGNAME)
	rm -rf unicodetest
