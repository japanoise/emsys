PROGNAME = emsys
PREFIX = /usr/local

# Standard C99 compiler settings
CC = cc
# Enable BSD and POSIX features portably
CFLAGS = -std=c99 -Wall -Wextra -Wpedantic -Wno-pointer-sign \
         -D_DEFAULT_SOURCE \
         -D_BSD_SOURCE -O2 \
         -D_POSIX_C_SOURCE=200112L \
         -DEMSYS_VERSION=\"git\" \
         -DEMSYS_BUILD_DATE=\"$(shell date '+%Y-%m-%d')\"

# Installation directories
BINDIR = $(PREFIX)/bin
MANDIR = $(PREFIX)/man/man1
DOCDIR = $(PREFIX)/share/doc/emsys

# Source files
OBJECTS = main.o wcwidth.o unicode.o buffer.o region.o undo.o transform.o \
          find.o pipe.o tab.o register.o fileio.o terminal.o display.o \
          keymap.o edit.o prompt.o util.o

# Default target
all: $(PROGNAME)

# Link the executable
$(PROGNAME): $(OBJECTS)
	$(CC) -o $(PROGNAME) $(OBJECTS) $(LDFLAGS)

# POSIX suffix rule for .c to .o
.SUFFIXES: .c .o
.c.o:
	$(CC) $(CFLAGS) -c $<

# Simple header dependency
$(OBJECTS): config.h

# Copy default config if it doesn't exist
config.h:
	cp config.def.h config.h


# Installation
install: $(PROGNAME)
	mkdir -p $(BINDIR) $(MANDIR) $(DOCDIR)
	cp $(PROGNAME) $(BINDIR)/
	-cp $(PROGNAME).1 $(MANDIR)/ 2>/dev/null
	-cp README.md $(DOCDIR)/ 2>/dev/null
	chmod 755 $(BINDIR)/$(PROGNAME)

# Removal
uninstall:
	rm -f $(BINDIR)/$(PROGNAME)
	rm -f $(MANDIR)/$(PROGNAME).1
	rm -f $(DOCDIR)/README.md
	-rmdir $(DOCDIR) 2>/dev/null

# Cleanup
clean:
	rm -f $(OBJECTS) $(PROGNAME)

distclean: clean
	rm -f config.h

# Testing
test: $(PROGNAME)
	./tests/run_tests.sh

# Development targets
debug: CFLAGS += -g -O0
debug: clean all

format:
	clang-format -i *.c *.h

# Platform-specific variants
android:
	CC=clang CFLAGS="$(CFLAGS) -fPIC -fPIE -DEMSYS_DISABLE_PIPE" LDFLAGS="-pie" $(MAKE) all

msys2:
	CFLAGS="$(CFLAGS) -D_GNU_SOURCE" $(MAKE) all

minimal:
	CFLAGS="$(CFLAGS) -DEMSYS_DISABLE_PIPE -Os" $(MAKE) all

darwin:
	CFLAGS="-std=c99 -Wall -Wextra -Wpedantic -Wno-pointer-sign -D_DARWIN_C_SOURCE -O2 -DEMSYS_VERSION=\"git\" -DEMSYS_BUILD_DATE=\"$(shell date '+%Y-%m-%d')\"" $(MAKE) all

# Standard POSIX targets
.PHONY: all install uninstall clean distclean test debug android msys2 minimal darwin

# Help
help:
	@echo "emsys build targets:"
	@echo "  all       Build emsys (default)"
	@echo "  install   Install to PREFIX ($(PREFIX))"
	@echo "  uninstall Remove installed files"
	@echo "  clean     Remove object files"
	@echo "  test      Run basic test"
	@echo "  debug     Build with debug symbols"
	@echo "  android   Build for Android/Termux"
	@echo "  darwin    Build for macOS/Darwin"
	@echo "  msys2     Build for MSYS2"
	@echo "  minimal   Build minimal version"
