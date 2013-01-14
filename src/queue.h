/*
 * STATSD-C
 * C port of Etsy's node.js-based statsd server
 *
 * originally based on http://github.com/jbuchbinder/statsd-c
 *
 */

#ifndef QUEUE_H_INCLUDED
#define QUEUE_H_INCLUDED 1

#include "counters.h"
#include "stats.h"
#include "timers.h"

#define MAX_QUEUE_SIZE ( 1024 * 1024 )

void queue_init( );
int queue_store( char *ptr );
char *queue_pop_first( );

#endif
