/*
 * statsd
 *
 * http://github.com/alanpearson/statsd-c
 * (based upon http://github.com/jbuchbinder/statsd-c)
 *
 */

#ifndef COUNTERS_H_INCLUDED
#define COUNTERS_H_INCLUDED

#include "uthash/uthash.h"
#include "uthash/utarray.h"


/********************************
 * Counters
 ********************************/

typedef struct
{
    UT_hash_handle hh; /* makes this structure hashable */
    char key[100];
    long double value;
} statsd_counter_t;

extern statsd_counter_t *counters;



/********************************
 * Timers
 ********************************/

typedef struct
{
    UT_hash_handle hh; /* makes this structure hashable */
    char key[100];
    int count;
    UT_array *values;
} statsd_timer_t;

extern statsd_timer_t *timers;
extern UT_icd timers_icd;



/********************************
 * Gauges
 ********************************/

typedef struct
{
    UT_hash_handle hh; /* makes this structure hashable */
    char key[100];
    long double value;
} statsd_gauge_t;

extern statsd_gauge_t *gauges;


#endif
