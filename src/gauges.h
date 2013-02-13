/*
 * statsd
 *
 * http://github.com/alanpearson/statsd-c
 * (based upon http://github.com/jbuchbinder/statsd-c)
 *
 */

#ifndef GAUGES_H_INCLUDED
#define GAUGES_H_INCLUDED

#include "uthash/uthash.h"

typedef struct
{
    char key[100];
    long double value;
    UT_hash_handle hh; /* makes this structure hashable */
} statsd_gauge_t;

extern statsd_gauge_t *gauges;

#endif
