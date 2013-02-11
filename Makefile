OBJDIR=obj
BINDIR=bin
SRC=src
CC=clang
CFLAGS=-g

OBJ=$(OBJDIR)/statsd.o $(OBJDIR)/strings.o $(OBJDIR)/queue.o

.PHONY: all clean

all: $(BINDIR) $(BINDIR)/statsd

clean:
	-rm -rf $(BINDIR)
	-rm -rf $(OBJDIR)

$(BINDIR):
	mkdir $(BINDIR)

$(BINDIR)/statsd: $(OBJDIR) $(OBJ)
	$(CC) $(LINKFLAGS) -o $(BINDIR)/statsd $(OBJ)

$(OBJDIR):
	mkdir $(OBJDIR)

$(OBJDIR)/statsd.o: $(SRC)/statsd.c
	$(CC) $(CFLAGS) -c -o $(OBJDIR)/statsd.o $(SRC)/statsd.c

$(OBJDIR)/strings.o: $(SRC)/strings.c
	$(CC) $(CFLAGS) -c -o $(OBJDIR)/strings.o $(SRC)/strings.c

$(OBJDIR)/queue.o: $(SRC)/queue.c
	$(CC) $(CFLAGS) -c -o $(OBJDIR)/queue.o $(SRC)/queue.c
