OBJDIR=obj
SRC=src
CC=clang
CFLAGS=-g

OBJ=$(OBJDIR)/statsd.o $(OBJDIR)/strings.o $(OBJDIR)/queue.o

all: statsd

statsd: $(OBJDIR) $(OBJ)
	$(CC) $(LINKFLAGS) -o statsd $(OBJ)

$(OBJDIR):
	mkdir $(OBJDIR)

$(OBJDIR)/statsd.o: $(SRC)/statsd.c
	$(CC) $(CFLAGS) -c -o $(OBJDIR)/statsd.o $(SRC)/statsd.c

$(OBJDIR)/strings.o: $(SRC)/strings.c
	$(CC) $(CFLAGS) -c -o $(OBJDIR)/strings.o $(SRC)/strings.c

$(OBJDIR)/queue.o: $(SRC)/queue.c
	$(CC) $(CFLAGS) -c -o $(OBJDIR)/queue.o $(SRC)/queue.c
