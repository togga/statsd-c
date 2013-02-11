#ifndef COUNTERS_H_INCLUDED
#define COUNTERS_H_INCLUDED

#include <semaphore.h>

#include "uthash/uthash.h"

typedef struct
{
    char key[100];
    long double value;
    UT_hash_handle hh; /* makes this structure hashable */
} statsd_counter_t;

extern statsd_counter_t *counters;
extern sem_t counters_lock;

#define wait_for_counters_lock() sem_wait(&counters_lock)
#define remove_counters_lock() sem_post(&counters_lock)

#endif
