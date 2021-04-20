PROGNAME=emsys
OBJECTS=main.o

all: $(PROGNAME)

debug: CFLAGS+=-g -O0 -v -Q
debug: $(PROGNAME)

$(PROGNAME): $(OBJECTS)
	$(CC) -o $@ $^ $(LDFLAGS)

clean:
	rm -rf *.o
	rm -rf $(PROGNAME)
	rm -rf $(PROGNAME).exe
