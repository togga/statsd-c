/*
 * STATSD-C
 * C port of Etsy's node.js-based statsd server
 *
 * originally based on http://github.com/jbuchbinder/statsd-c
 *
 */

#ifndef SERIALIZE_H_INCLUDED
#define SERIALIZE_H_INCLUDED

#include "counters.h"
#include "stats.h"
#include "timers.h"

int statsd_serialize( char *filename );
int statsd_deserialize( char *filename );

#endif
