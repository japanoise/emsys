PROGNAME=emsys
OBJECTS=main.o wcwidth.o unicode.o

all: $(PROGNAME)

debug: CFLAGS+=-g -O0 -v -Q
debug: $(PROGNAME)

$(PROGNAME): $(OBJECTS)
	$(CC) -o $@ $^ $(LDFLAGS)

unicodetest: unicode.o unicodetest.o wcwidth.o
	$(CC) -o $@ $^ $(LDFLAGS)

clean:
	rm -rf *.o
	rm -rf *.exe
	rm -rf $(PROGNAME)
	rm -rf unicodetest
