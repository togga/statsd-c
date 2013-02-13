/*
 * statsd
 *
 * http://github.com/alanpearson/statsd-c
 * (based upon http://github.com/jbuchbinder/statsd-c)
 *
 */

#ifndef TIMERS_H_INCLUDED
#define TIMERS_H_INCLUDED

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
extern UT_icd timers_icd;

#endif

