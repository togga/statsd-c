OBJDIR=obj
BINDIR=bin
SRC=src
CC=gcc
OPTFLAGS=-O2
CFLAGS=$(OPTFLAGS) -Wall -Werror

.PHONY: all debug clean

# debug:
# 	OBJDIR=objd
# 	SUFFIX=_d

debug: all


OBJ=$(OBJDIR)/statsd.o $(OBJDIR)/strings.o $(OBJDIR)/udp.o $(OBJDIR)/counters.o $(OBJDIR)/mgmt.o $(OBJDIR)/flush.o $(OBJDIR)/gauge_relay.o


all: $(BINDIR) $(BINDIR)/statsd$(SUFFIX) $(BINDIR)/statsd_client$(SUFFIX)

clean:
	-rm -rf $(BINDIR)
	-rm -rf $(OBJDIR)

$(BINDIR):
	mkdir $(BINDIR)

$(BINDIR)/statsd$(SUFFIX): $(OBJDIR) $(OBJ)
	$(CC) $(LINKFLAGS) -o $(BINDIR)/statsd$(SUFFIX) $(OBJ) -levent

$(BINDIR)/statsd_client$(SUFFIX): $(OBJDIR) $(OBJDIR)/statsd_client.o
	$(CC) $(LINKFLAGS) -o $(BINDIR)/statsd_client$(SUFFIX) $(OBJDIR)/statsd_client.o

$(OBJDIR):
	mkdir $(OBJDIR)

$(OBJDIR)/%.o: $(SRC)/%.c
	$(CC) $(CFLAGS) -c -o $@ $<
