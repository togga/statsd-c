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
#include <ctype.h>

#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/listener.h>
#include <event2/util.h>
#include <event2/event.h>

#include "uthash/utarray.h"
#include "uthash/utstring.h"
#include "statsd.h"
#include "stats.h"
#include "counters.h"
#include "strings.h"
#include "mgmt.h"

void mgmt_callback(int fd, short flags, void * param);

static void mgmt_listener(struct evconnlistener *listener, evutil_socket_t fd, struct sockaddr *sa, int socklen, void *ptr);
static void mgmt_readcb(struct bufferevent *bev, void *user_data);
static void mgmt_writecb(struct bufferevent *bev, void *user_data);
static void mgmt_eventcb(struct bufferevent *bev, short events, void *user_data);
static void mgmt_parse_input(struct mgmt * mgmt, struct bufferevent *bev, char * bufptr, int nbytes);
static void mgmt_send_stats(struct evbuffer* output);
static void mgmt_send_timers(struct evbuffer* output);
static void mgmt_send_counters(struct evbuffer* output);

struct mgmt * mgmt_new(short port)
{
    struct mgmt * mgmt;

    mgmt = (struct mgmt *) malloc(sizeof(struct mgmt));
    if (mgmt)
    {
        memset(mgmt,0,sizeof(struct mgmt));
        mgmt->port = port;
    }

    return mgmt;
}


int mgmt_start(struct mgmt * mgmt, struct event_base *ev_base)
{
    struct sockaddr_in sin;

    syslog(LOG_DEBUG,"starting mgmt listener\n");
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port = htons(mgmt->port);

    mgmt->listener = evconnlistener_new_bind(ev_base, mgmt_listener, (void *)mgmt,
        LEV_OPT_REUSEABLE|LEV_OPT_CLOSE_ON_FREE, -1,
        (struct sockaddr*)&sin,
        sizeof(sin));

    if (!mgmt->listener) {
        syslog(LOG_ERR, "Could not create a listener!\n");
        return 1;
    }

    return 0;
}


static void mgmt_listener(struct evconnlistener *listener, evutil_socket_t fd,
                          struct sockaddr *sa, int socklen, void *ptr)
{
    struct mgmt * mgmt = (struct mgmt *)ptr;
    struct event_base *base = NULL;
    struct bufferevent *bev;

    base = evconnlistener_get_base(listener);

    bev = bufferevent_socket_new(base, fd, BEV_OPT_CLOSE_ON_FREE);
    if (!bev)
    {
        syslog(LOG_ERR, "Error constructing bufferevent!");
        event_base_loopbreak(base);
        return;
    }

    bufferevent_setcb(bev, mgmt_readcb, mgmt_writecb, mgmt_eventcb, mgmt);
    bufferevent_disable(bev, EV_WRITE);
    bufferevent_enable(bev, EV_READ);
}

static void mgmt_readcb(struct bufferevent *bev, void *user_data)
{
    struct mgmt * mgmt = (struct mgmt *)user_data;
    struct evbuffer * input;
    int nbytes, n, result;
    char buffer[80];

    input = bufferevent_get_input(bev);
    nbytes = evbuffer_get_length(input);
    DPRINTF("mgmt_readcb: %d bytes available\n", nbytes);

    n = nbytes;
    if (n > 79) n = 79;

    DPRINTF("mgmt_readcb: attempting to read %d bytes\n", n);
    result = evbuffer_remove(input, buffer, n);
    if (result == -1)
    {
        DPRINTF("mgmt_readcb: could not read available bytes\n");
    }
    else
    {
        buffer[result] = 0;
        DPRINTF("mgmt_readcb: read %d bytes: %s\n", result, buffer);

        mgmt_parse_input(mgmt, bev, buffer, result);
        bufferevent_enable(bev, EV_WRITE);
    }
}

static void mgmt_writecb(struct bufferevent *bev, void *user_data)
{
    struct evbuffer *output = bufferevent_get_output(bev);
    if (evbuffer_get_length(output) == 0)
    {
        bufferevent_disable(bev, EV_WRITE);
    }
}

static void mgmt_eventcb(struct bufferevent *bev, short events, void *user_data)
{
    if (events & BEV_EVENT_EOF)
    {
        DPRINTF("Connection closed.\n");
    }
    else if (events & BEV_EVENT_ERROR)
    {
        syslog(LOG_ERR,"Got an error on the mgmt connection: %s\n", strerror(errno));
    }
    /* None of the other events can happen here, since we haven't enabled
     * timeouts */
    bufferevent_free(bev);
}


void mgmt_cleanup(struct mgmt * mgmt)
{
    if (mgmt->ev)
    {
        event_del(mgmt->ev);
        event_free(mgmt->ev);
    }

    free(mgmt);
}


static void mgmt_parse_input(struct mgmt * mgmt, struct bufferevent *bev, char * bufptr, int nbytes)
{
    struct evbuffer * output = bufferevent_get_output(bev);

    while (nbytes > 0 && isspace(bufptr[nbytes-1]))
        nbytes--;

    if (nbytes == 4 && strncmp(bufptr, (char *)"help", 4) == 0)
    {
        STREAM_SEND(output, MGMT_HELP);
    }
    else if (nbytes == 8 && strncmp(bufptr, (char *)"counters", 8) == 0)
    {
        mgmt_send_counters(output);
    }
    else if (nbytes == 6 && strncmp(bufptr, (char *)"timers", 6) == 0)
    {
        mgmt_send_timers(output);
    }
    else if (nbytes == 5 && strncmp(bufptr, (char *)"stats", 5) == 0)
    {
        mgmt_send_stats(output);
    }
    else if (nbytes == 4 && strncmp(bufptr, (char *)"quit", 4) == 0)
    {
        /* disconnect */
        bufferevent_free(bev);
    }
    else
    {
        STREAM_SEND(output, MGMT_BADCOMMAND);
    }
}

static void mgmt_send_counters(struct evbuffer* output)
{
    /* send counters */
    statsd_counter_t *s_counter, *tmp;
    HASH_ITER(hh, counters, s_counter, tmp)
    {
        STREAM_SEND(output, s_counter->key);
        STREAM_SEND(output, ": ");
        STREAM_SEND_LONG_DOUBLE(output, s_counter->value);
        STREAM_SEND(output, "\n");
    }
    if (s_counter)
        free(s_counter);
    if (tmp)
        free(tmp);

    STREAM_SEND(output, MGMT_END);
}

static void mgmt_send_timers(struct evbuffer* output)
{
    /* send timers */

    statsd_timer_t *s_timer, *tmp;
    HASH_ITER(hh, timers, s_timer, tmp)
    {
        STREAM_SEND(output, s_timer->key);
        STREAM_SEND(output, ": ");
        STREAM_SEND_INT(output, s_timer->count);

        if (s_timer->count > 0)
        {
            double *j = NULL; bool first = 1;
            STREAM_SEND(output, " [");
            while ( (j=(double *)utarray_next(s_timer->values, j)) )
            {
                if (first == 1)
                {
                    first = 0;
                    STREAM_SEND(output, ",");
                }
                STREAM_SEND_DOUBLE(output, *j);
            }
            STREAM_SEND(output, "]");
        }
        STREAM_SEND(output, "\n");
    }
    if (s_timer)
        free(s_timer);
    if (tmp)
        free(tmp);

    STREAM_SEND(output, MGMT_END);
}

static void mgmt_send_stats(struct evbuffer* output)
{
    /* send stats */

    statsd_stat_t *s_stat, *tmp;
    HASH_ITER(hh, stats, s_stat, tmp)
    {
        if (strlen(s_stat->name.group_name) > 1)
        {
            STREAM_SEND(output, s_stat->name.group_name);
            STREAM_SEND(output, ".");
        }
        STREAM_SEND(output, s_stat->name.key_name);
        STREAM_SEND(output, ": ");
        STREAM_SEND_LONG(output, s_stat->value);
        STREAM_SEND(output, "\n");
    }
    if (s_stat)
        free(s_stat);
    if (tmp)
        free(tmp);

    STREAM_SEND(output, MGMT_END);
}
