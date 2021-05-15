PROGNAME=emsys
PREFIX=/usr/local
BINDIR=$(PREFIX)/bin
OBJECTS=main.o wcwidth.o unicode.o row.o region.o undo.o transform.o bound.o
CFLAGS+=-Wall -Wextra -pedantic -Wno-pointer-sign -Werror=incompatible-pointer-types

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
