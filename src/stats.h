#ifndef STATS_H_INCLUDED
#define STATS_H_INCLUDED

#include <stdbool.h>
#include <semaphore.h>

#include "uthash/uthash.h"

typedef struct
{
    char group_name[100];
    char key_name[100];
} statsd_stat_name_t;

typedef struct
{
    statsd_stat_name_t name;
    long value;
    bool locked;
    UT_hash_handle hh; /* makes this structure hashable */
} statsd_stat_t;

extern statsd_stat_t *stats;

#endif
