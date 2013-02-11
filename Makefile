OBJDIR=obj
BINDIR=bin
SRC=src
CC=clang
CFLAGS=-g -Wall -Werror

OBJ=$(OBJDIR)/statsd.o $(OBJDIR)/strings.o $(OBJDIR)/queue.o

.PHONY: all clean

all: $(BINDIR) $(BINDIR)/statsd $(BINDIR)/statsd_client

clean:
	-rm -rf $(BINDIR)
	-rm -rf $(OBJDIR)

$(BINDIR):
	mkdir $(BINDIR)

$(BINDIR)/statsd: $(OBJDIR) $(OBJ)
	$(CC) $(LINKFLAGS) -o $(BINDIR)/statsd $(OBJ) -levent

$(BINDIR)/statsd_client: $(OBJDIR) $(OBJDIR)/statsd_client.o
	$(CC) $(LINKFLAGS) -o $(BINDIR)/statsd_client $(OBJDIR)/statsd_client.o

$(OBJDIR):
	mkdir $(OBJDIR)

$(OBJDIR)/statsd.o: $(SRC)/statsd.c
	$(CC) $(CFLAGS) -c -o $(OBJDIR)/statsd.o $(SRC)/statsd.c

$(OBJDIR)/strings.o: $(SRC)/strings.c
	$(CC) $(CFLAGS) -c -o $(OBJDIR)/strings.o $(SRC)/strings.c

$(OBJDIR)/queue.o: $(SRC)/queue.c
	$(CC) $(CFLAGS) -c -o $(OBJDIR)/queue.o $(SRC)/queue.c

$(OBJDIR)/statsd_client.o: $(SRC)/statsd_client.c
	$(CC) $(CFLAGS) -c -o $(OBJDIR)/statsd_client.o $(SRC)/statsd_client.c

