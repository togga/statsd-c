/*
 * statsd
 *
 * http://github.com/alanpearson/statsd-c
 * (based upon http://github.com/jbuchbinder/statsd-c)
 *
 */

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
#include "flush.h"

static void dump_stats();
static void flush_callback(int fd, short flags, void * param);
static void do_flush(struct flush * flush);

extern int percentiles[5];
extern int num_percentiles;

extern int graphite_port;
extern char *graphite_host;

static int double_sort(const void *a, const void *b)
{
    double _a = *(double *)a;
    double _b = *(double *)b;
    if (_a == _b) return 0;
    return (_a < _b) ? -1 : 1;
}

struct flush * flush_new(int interval)
{
    struct flush * flush;

    flush = (struct flush *) malloc(sizeof(struct flush));
    if (flush)
    {
        memset(flush,0,sizeof(struct flush));
        flush->interval = interval;
    }

    return flush;
}

void flush_start(struct flush * flush, struct event_base * ev_base)
{
    struct timeval tv;

    syslog(LOG_DEBUG, "Starting Flush timer");

    flush->ev = event_new(ev_base,-1,EV_PERSIST,flush_callback,flush);

    tv.tv_sec = flush->interval;
    tv.tv_usec = 0;

    evtimer_add(flush->ev,&tv);
}

void flush_cleanup(struct flush * flush)
{
    if (flush->ev)
    {
        event_del(flush->ev);
        event_free(flush->ev);
    }

    free(flush);
}

static void flush_callback(int fd, short flags, void * param)
{
    struct flush * flush = (struct flush *) param;

    do_flush(flush);
}

static void dump_stats()
{
    if (debug)
    {
        {
            syslog(LOG_DEBUG, "Stats dump:");
            statsd_stat_t *s, *tmp;
            HASH_ITER(hh, stats, s, tmp)
            {
                syslog(LOG_DEBUG, "%s.%s: %ld", s->name.group_name, s->name.key_name, s->value);
            }
            if (s)
                free(s);
            if (tmp)
                free(tmp);
        }

        {
            syslog(LOG_DEBUG, "Counters dump:");
            statsd_counter_t *c, *tmp;
            HASH_ITER(hh, counters, c, tmp)
            {
                syslog(LOG_DEBUG, "%s: %Lf", c->key, c->value);
            }
            if (c)
                free(c);
            if (tmp)
                free(tmp);
        }

        {
            syslog(LOG_DEBUG, "Gauges dump:");
            statsd_gauge_t *g, *tmp;
            HASH_ITER(hh, gauges, g, tmp)
            {
                syslog(LOG_DEBUG, "%s: %Lf", g->key, g->value);
            }
            if (g)
                free(g);
            if (tmp)
                free(tmp);
        }
    }
}


static void do_flush(struct flush * flush)
{
    dump_stats();

    long ts = time(NULL);
    char *ts_string = ltoa(ts);
    int numStats = 0;
    UT_string *statString;

    utstring_new(statString);

    /* ---------------------------------------------------------------------
        Process counter metrics
        -------------------------------------------------------------------- */
    {
        statsd_counter_t *s_counter, *tmp;
        HASH_ITER(hh, counters, s_counter, tmp)
        {
            long double value = s_counter->value / flush->interval;
            utstring_printf(statString, "stats.%s %Lf %ld\nstats.counts.%s %Lf %ld\n", s_counter->key, value, ts, s_counter->key, s_counter->value, ts);

            /* Clear counter after we're done with it */
            s_counter->value = 0;

            numStats++;
        }
        if (s_counter)
            free(s_counter);
        if (tmp)
            free(tmp);
    }

    /* ---------------------------------------------------------------------
        Process timer metrics
        -------------------------------------------------------------------- */

    {
        statsd_timer_t *s_timer, *tmp;
        HASH_ITER(hh, timers, s_timer, tmp)
        {
            if (s_timer->count > 0)
            {
                int pctThreshold = percentiles[0]; /* TODO FIXME: support multiple percentiles */

                /* Sort all values in this timer list */
                utarray_sort(s_timer->values, double_sort);

                double min = 0;
                double max = 0;
                {
                    double *i = NULL; int count = 0;
                    while( (i=(double *) utarray_next( s_timer->values, i)) )
                    {
                        if (count == 0)
                        {
                            min = *i;
                            max = *i;
                        }
                        else
                        {
                            if (*i < min) min = *i;
                            if (*i > max) max = *i;
                        }
                        count++;
                    }
                }

                double mean = min;
                double maxAtThreshold = max;

                if (s_timer->count > 1)
                {
                    // Find the index of the 90th percentile threshold
                    int thresholdIndex = ( pctThreshold / 100.0 ) * s_timer->count;
                    maxAtThreshold = * ( utarray_eltptr( s_timer->values, thresholdIndex - 1 ) );

                    double sum = 0;
                    double *i = NULL; int count = 0;
                    while( (i=(double *) utarray_next( s_timer->values, i)) && count < s_timer->count - 1 )
                    {
                        sum += *i;
                        count++;
                    }
                    mean = sum / s_timer->count;
                }

                /* Clear all values for this timer */
                utarray_clear(s_timer->values);
                s_timer->count = 0;

                utstring_printf(statString, "stats.timers.%s.mean %f %ld\n"
                    "stats.timers.%s.upper %f %ld\n"
                    "stats.timers.%s.upper_%d %f %ld\n"
                    "stats.timers.%s.lower %f %ld\n"
                    "stats.timers.%s.count %d %ld\n",
                    s_timer->key, mean, ts,
                    s_timer->key, max, ts,
                    s_timer->key, pctThreshold, maxAtThreshold, ts,
                    s_timer->key, min, ts,
                    s_timer->key, s_timer->count, ts
                );

            }
            numStats++;
        }
        if (s_timer) free(s_timer);
        if (tmp) free(tmp);
    }

    /* ---------------------------------------------------------------------
        Process gauge metrics
        -------------------------------------------------------------------- */

    {
        statsd_gauge_t *s_gauge, *tmp;
        HASH_ITER(hh, gauges, s_gauge, tmp)
        {
            long double value = s_gauge->value;
            utstring_printf(statString, "stats.%s %Lf %ld\nstats.gauges.%s %Lf %ld\n", s_gauge->key, value, ts, s_gauge->key, s_gauge->value, ts);
            numStats++;
        }
        if (s_gauge) free(s_gauge);
        if (tmp) free(tmp);
    }

    /* ---------------------------------------------------------------------
        Process totals
        -------------------------------------------------------------------- */

    {
        utstring_printf(statString, "statsd.numStats %d %ld\n", numStats, ts);
    }

    DPRINTF("Messages:\n%s", utstring_body(statString));

    int failure = 0, sock = -1;
    struct hostent* result = NULL;
    struct sockaddr_in sa;
#ifdef __linux__
    struct hostent he;
    char tmpbuf[1024];
    int local_errno = 0;
    if (gethostbyname_r(graphite_host, &he, tmpbuf, sizeof(tmpbuf),
                                            &result, &local_errno))
    {
        failure = 1;
    }
#else
    result = gethostbyname(graphite_host);
#endif

    if (result == NULL || result->h_addr_list[0] == NULL || result->h_length != 4)
    {
        DPRINTF("Cannot get hostname\n");
        failure = 1;
    }

    uint32_t* ip = (uint32_t*) result->h_addr_list[0];

    if (!failure)
    {
        sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sock == -1)
        {
            failure = 1;
        }
    }

    if (!failure)
    {
        memset(&sa, 0, sizeof(struct sockaddr_in));
#ifdef DARWIN
        sa.sin_len = sizeof(struct sockaddr_in);
#endif
        sa.sin_family = AF_INET;
        sa.sin_port = htons(graphite_port);
        memcpy(&(sa.sin_addr), ip, sizeof(*ip));
    }

    if (!failure)
    {
        DPRINTF("Sending data to %s:%d. addr = %08x\n", graphite_host, graphite_port, *ip);
        int r = connect(sock, (struct sockaddr *)&sa, sizeof(sa));
        DPRINTF("Connect result = %d\n", r);
        int n = send(sock, utstring_body(statString), utstring_len(statString), 0);
        DPRINTF("Send result = %d\n", n);
        close(sock);
    }

    if (ts_string)
    {
        free(ts_string);
    }

    utstring_free(statString);
}
