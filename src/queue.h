#ifndef QUEUE_H_INCLUDED
#define QUEUE_H_INCLUDED

#include "counters.h"
#include "stats.h"
#include "timers.h"

#define MAX_QUEUE_SIZE ( 1024 * 1024 )

void queue_init( );
int queue_store( char *ptr );
char *queue_pop_first( );

#endif
