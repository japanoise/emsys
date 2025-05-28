PROGNAME=emsys
PREFIX=/usr/local
VERSION?=git-$(shell git rev-parse --short HEAD)
BINDIR=$(PREFIX)/bin
MANDIR=$(PREFIX)/man/man1
OBJECTS=main.o wcwidth.o unicode.o row.o region.o undo.o transform.o bound.o command.o find.o pipe.o tab.o register.o keybindings.o compat.o terminal.o display.o
CFLAGS+=-std=c99 -D_POSIX_C_SOURCE=200112L -Wall -Wno-pointer-sign -DEMSYS_BUILD_DATE=\"$(shell date '+%Y-%m-%dT%H:%M:%S%z')\" -DEMSYS_VERSION=\"$(VERSION)\"

# Platform detection
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),FreeBSD)
    CFLAGS += -D__BSD_VISIBLE
endif
ifeq ($(UNAME_S),OpenBSD)
    CFLAGS += -D_BSD_SOURCE
endif
ifeq ($(UNAME_S),NetBSD)
    CFLAGS += -D_NETBSD_SOURCE
endif
ifeq ($(UNAME_S),DragonFly)
    CFLAGS += -D_BSD_SOURCE
endif
ifeq ($(UNAME_S),Darwin)
    CFLAGS += -D_DARWIN_C_SOURCE
endif
# Windows/MSYS2/MinGW detection
ifneq (,$(findstring MSYS_NT,$(UNAME_S)))
    CFLAGS += -D_GNU_SOURCE
endif
ifneq (,$(findstring MINGW,$(UNAME_S)))
    CFLAGS += -D_GNU_SOURCE
endif
ifneq (,$(findstring CYGWIN,$(UNAME_S)))
    CFLAGS += -D_GNU_SOURCE
endif

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

clean:
	rm -rf *.o
	rm -rf *.exe
	rm -rf $(PROGNAME)
	rm -rf unicodetest
