#ifndef COUNTERS_H_INCLUDED
#define COUNTERS_H_INCLUDED

#include "uthash/uthash.h"

typedef struct
{
    char key[100];
    long double value;
    UT_hash_handle hh; /* makes this structure hashable */
} statsd_counter_t;

extern statsd_counter_t *counters;

#endif
