/*
 * STATSD-C
 * C port of Etsy's node.js-based statsd server
 *
 * originally based on http://github.com/jbuchbinder/statsd-c
 *
 */

#ifndef TIMERS_H_INCLUDED
#define TIMERS_H_INCLUDED

#include <semaphore.h>

#include "uthash/uthash.h"
#include "uthash/utarray.h"

typedef struct
{
    UT_hash_handle hh; /* makes this structure hashable */
    char key[100];
    int count;
    UT_array *values;
} statsd_timer_t;

extern statsd_timer_t *timers;
extern sem_t timers_lock;
extern UT_icd timers_icd;

#define wait_for_timers_lock() sem_wait(&timers_lock)
#define remove_timers_lock() sem_post(&timers_lock)

#endif

