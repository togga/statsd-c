/*
 * statsd
 *
 * http://github.com/alanpearson/statsd-c
 * (based upon http://github.com/jbuchbinder/statsd-c)
 *
 */

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <time.h>

#include <event2/event.h>

#include "uthash/utarray.h"
#include "uthash/utstring.h"
#include "statsd.h"
#include "stats.h"
#include "counters.h"
#include "strings.h"
#include "gauge_relay.h"


/**
 * Record or update stat value.
 */
void update_stat( char *group, char *key, char *value )
{
    DPRINTF("update_stat ( %s, %s, %s )\n", group, key, value);
    statsd_stat_t *s;
    statsd_stat_name_t l;

    memset(&l, 0, sizeof(statsd_stat_name_t));
    strcpy(l.group_name, group);
    strcpy(l.key_name, key);
    DPRINTF("HASH_FIND '%s' '%s'\n", l.group_name, l.key_name);
    HASH_FIND( hh, stats, &l, sizeof(statsd_stat_name_t), s );

    if (s)
    {
        DPRINTF("Updating old stat entry\n");

        s->value = atol( value );
    }
    else
    {
        DPRINTF("Adding new stat entry\n");
        s = malloc(sizeof(statsd_stat_t));
        memset(s, 0, sizeof(statsd_stat_t));

        strcpy(s->name.group_name, group);
        strcpy(s->name.key_name, key);
        s->value = atol(value);
        s->locked = 0;

        HASH_ADD( hh, stats, name, sizeof(statsd_stat_name_t), s );
    }
}

void update_counter( char *key, double value, double sample_rate)
{
    DPRINTF("update_counter ( %s, %f, %f )\n", key, value, sample_rate);
    statsd_counter_t *c;
    HASH_FIND_STR( counters, key, c );
    if (c)
    {
        DPRINTF("Updating old counter entry\n");
        if (sample_rate == 0)
        {
            c->value = c->value + value;
        }
        else
        {
            c->value = c->value + ( value * ( 1 / sample_rate ) );
        }
    }
    else
    {
        DPRINTF("Adding new counter entry\n");
        c = malloc(sizeof(statsd_counter_t));

        strcpy(c->key, key);
        c->value = 0;
        if (sample_rate == 0)
        {
            c->value = value;
        }
        else
        {
            c->value = value * ( 1 / sample_rate );
        }

        HASH_ADD_STR( counters, key, c );
    }
}

void update_gauge( char *key, double value )
{
    DPRINTF("update_gauge ( %s, %f )\n", key, value);
    statsd_gauge_t *g;
    DPRINTF("HASH_FIND_STR '%s'\n", key);
    HASH_FIND_STR( gauges, key, g );
    DPRINTF("after HASH_FIND_STR '%s'\n", key);
    if (g)
    {
        DPRINTF("Updating old timer entry\n");
        g->value = value;
    }
    else
    {
        DPRINTF("Adding new timer entry\n");
        g = malloc(sizeof(statsd_gauge_t));

        strcpy(g->key, key);
        g->value = value;

        HASH_ADD_STR( gauges, key, g );
    }
}

void update_timer(char *key, double value)
{
    DPRINTF("update_timer ( %s, %f )\n", key, value);
    statsd_timer_t *t;
    DPRINTF("HASH_FIND_STR '%s'\n", key);
    HASH_FIND_STR( timers, key, t );
    DPRINTF("after HASH_FIND_STR '%s'\n", key);
    if (t)
    {
        DPRINTF("Updating old timer entry\n");
        utarray_push_back(t->values, &value);
        t->count++;
    }
    else
    {
        DPRINTF("Adding new timer entry\n");
        t = malloc(sizeof(statsd_timer_t));

        strcpy(t->key, key);
        t->count = 0;
        utarray_new(t->values, &timers_icd);
        utarray_push_back(t->values, &value);
        t->count++;

        HASH_ADD_STR( timers, key, t );
    }
}

void add_timer( char *key, double value )
{
    statsd_timer_t *t;
    HASH_FIND_STR( timers, key, t );
    if (t)
    {
        /* Add to old entry */
        t->count++;
        utarray_push_back(t->values, &value);
    }
    else
    {
        /* Create new entry */
        t = malloc(sizeof(statsd_timer_t));

        strcpy(t->key, key);
        t->count = 1;
        utarray_new(t->values, &timers_icd);
        utarray_push_back(t->values, &value);

        HASH_ADD_STR( timers, key, t );
    }
}

void process_stats_packet(char buf_in[])
{
    char *key_name = NULL;
    char *save, *subsave, *token, *subtoken, *bits, *fields;
    double value = 1.0;
    int i;
    int j;
    double sample_rate;
    bool is_timer = 0, is_gauge = 0;

    if (strlen(buf_in) < 2)
    {
        UPDATE_LAST_MSG_SEEN()
        return;
    }

    for (i = 1, bits=buf_in; ; i++, bits=NULL)
    {
        DPRINTF("i = %d\n", i);
        token = strtok_r(bits, ":\n", &save);
        if (token == NULL)
        {
            break;
        }

        if (i == 1)
        {
            DPRINTF("Found token '%s', key name\n", token);
            key_name = strdup(token);
            sanitize_key(key_name);
            /* break; */
        }
        else
        {
            DPRINTF("\ttoken [#%d] = %s\n", i, token);
            sample_rate = 1.0;
            is_timer = 0;
            is_gauge = 0;

            if (strstr(token, "|") == NULL)
            {
                DPRINTF("No pipes found, basic logic\n");
                sanitize_value(token);
                DPRINTF("\t\tvalue = %s\n", token);
                value = strtod(token, (char **) NULL);
                DPRINTF("\t\tvalue = %s => %f\n", token, value);
            }
            else
            {
                for (j = 1, fields = token; ; j++, fields = NULL)
                {
                    subtoken = strtok_r(fields, "|", &subsave);
                    if (subtoken == NULL)
                        break;
                    DPRINTF("\t\tsubtoken = %s\n", subtoken);

                    switch (j)
                    {
                    case 1:
                        DPRINTF("case 1\n");
                        sanitize_value(subtoken);
                        value = strtod(subtoken, (char **) NULL);
                        break;
                    case 2:
                        DPRINTF("case 2\n");
                        if (subtoken == NULL)
                            { break ; }
                        if (strlen(subtoken) < 2)
                        {
                            DPRINTF("subtoken length < 2\n");
                            is_timer = 0;
                            if (*subtoken == 'g')
                            {
                                is_gauge = 1;
                            }
                        }
                        else
                        {
                            DPRINTF("subtoken length >= 2\n");
                            if (*subtoken == 'm' && *(subtoken + 1) == 's')
                            {
                                is_timer = 1;
                                is_gauge = 0;
                            }
                        }
                        break;
                    case 3:
                        DPRINTF("case 3\n");
                        if (subtoken == NULL)
                            break ;
                        if (*subtoken == '@')
                        {
                          sample_rate = strtod(subtoken + 1, (char **) NULL);
                          if (sample_rate == 0) {
                            sample_rate = 1.0;
                          }
                        }
                        break;
                    }
                }
            }

            DPRINTF("Post token processing\n");

            if (is_timer == 1)
            {
                /* ms passed, handle timer */
                update_timer( key_name, value );
            }
            else if (is_gauge == 1)
            {
              relay_gauge(key_name, value);
            }
            else
            {
                update_counter(key_name, value, sample_rate);
                DPRINTF("Found counter '%s' value '%f' sample rate '%f'\n",
                    key_name, value, sample_rate);
            }

            i = 0;
            if (key_name) {
                DPRINTF("freeing key\n");
                free(key_name);
                key_name = NULL;
            }

        }
    }

    if (key_name) {
        DPRINTF("freeing key\n");
        free(key_name);
        key_name = NULL;
    }

    UPDATE_LAST_MSG_SEEN()
}
