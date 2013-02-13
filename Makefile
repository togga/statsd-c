OBJDIR=obj
BINDIR=bin
SRC=src
CC=clang
CFLAGS=-g -Wall -Werror

OBJ=$(OBJDIR)/statsd.o $(OBJDIR)/strings.o $(OBJDIR)/udp.o $(OBJDIR)/counters.o $(OBJDIR)/mgmt.o $(OBJDIR)/flush.o

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

$(OBJDIR)/%.o: $(SRC)/%.c
	$(CC) $(CFLAGS) -c -o $@ $<
