/*
 * STATSD-C
 * C port of Etsy's node.js-based statsd server
 *
 * originally based on http://github.com/jbuchbinder/statsd-c
 *
 */

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>

#include "queue.h"

int queue_store_pos = 0;
int queue_retrieve_pos = 0;
char *queue[MAX_QUEUE_SIZE];

void queue_init()
{
    int p;

    syslog(LOG_DEBUG, "queue_init");
    queue_store_pos = 0;
    queue_retrieve_pos = 0;
    for (p = 0; p < MAX_QUEUE_SIZE; p++ ) 
    {
        queue[ p ] = NULL;
    }
}

int queue_store(char *ptr)
{
    syslog(LOG_DEBUG, "queue_store ('%s')", ptr);
    if (queue_store_pos == MAX_QUEUE_SIZE)
    {
        syslog(LOG_INFO, "Queue has reached maximum size of %d, wrapping", MAX_QUEUE_SIZE);
        queue_store_pos = 0;
    }
    queue[ queue_store_pos ] = ptr;
    queue_store_pos ++;
    return 1;
}

char *queue_pop_first()
{
    if (queue[ queue_retrieve_pos ] == NULL) 
        return NULL;
    char *tmpptr = queue[ queue_retrieve_pos ];
    queue[ queue_retrieve_pos ] = NULL;
    queue_retrieve_pos ++;
    if (queue_retrieve_pos == MAX_QUEUE_SIZE) 
    {
        queue_retrieve_pos = 0;
    }
    return tmpptr;
}
