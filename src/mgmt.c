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
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <limits.h>

#include <event2/event.h>

#include "uthash/utarray.h"
#include "uthash/utstring.h"
#include "statsd.h"
#include "stats.h"
#include "timers.h"
#include "counters.h"
#include "gauges.h"
#include "strings.h"
#include "mgmt.h"

void mgmt_callback(int fd, short flags, void * param);

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


void mgmt_start(struct mgmt * mgmt, struct event_base *ev_base)
{

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

void mgmt_stuff(struct mgmt * mgmt)
{
    fd_set master;
    fd_set read_fds;
    struct sockaddr_in serveraddr;
    struct sockaddr_in clientaddr;
    int fdmax;
    int newfd;
    char buf[1024];
    int nbytes;
    int yes = 1;
    int addrlen;
    int i;

    /* begin mgmt listener */
    syslog(LOG_INFO, "Thread[Mgmt]: Starting\n");

    FD_ZERO(&master);
    FD_ZERO(&read_fds);

    if ((mgmt->sock = socket(AF_INET, SOCK_STREAM, 0)) == -1)
    {
        perror("socket error");
        exit(1);
    }

    if (setsockopt(mgmt->sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1)
    {
        perror("setsockopt error");
        exit(1);
    }

    /* bind */
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_addr.s_addr = INADDR_ANY;
    serveraddr.sin_port = htons(mgmt->port);
    memset(&(serveraddr.sin_zero), '\0', 8);

    if (bind(mgmt->sock, (struct sockaddr *)&serveraddr, sizeof(serveraddr)) == -1)
    {
        exit(1);
    }

    if (listen(mgmt->sock, 10) == -1)
    {
        exit(1);
    }

    FD_SET(mgmt->sock, &master);
    fdmax = mgmt->sock;

    for (;;)
    {
        read_fds = master;

        if (select(fdmax+1, &read_fds, NULL, NULL, NULL) == -1)
        {
            perror("select error");
            exit(1);
        }

        for (i = 0; i <= fdmax; i++)
        {
            if (FD_ISSET(i, &read_fds))
            {
                if (i == mgmt->sock)
                {
                    addrlen = sizeof(clientaddr);
                    if ((newfd = accept(mgmt->sock, (struct sockaddr *)&clientaddr, (socklen_t *) &addrlen)) == -1)
                    {
                        perror("accept error");
                    }
                    else
                    {
                        FD_SET(newfd, &master);
                        if(newfd > fdmax)
                        {
                            fdmax = newfd;
                        }
                        syslog(LOG_INFO, "New connection from %s on socket %d\n", inet_ntoa(clientaddr.sin_addr), newfd);

                        /* Send prompt on connection */
                        if (friendly)
                        {
                            STREAM_SEND(newfd, MGMT_PROMPT)
                        }
                    }
                }
                else
                {
                    /* handle data from a client */
                    if ((nbytes = recv(i, buf, sizeof(buf), 0)) <= 0)
                    {
                        if (nbytes == 0)
                        {
                            syslog(LOG_INFO, "Socket %d hung up\n", i);
                        }
                        else
                        {
                            perror("recv() error");
                        }

                        close(i);
                        FD_CLR(i, &master);
                    }
                    else
                    {
                        syslog(LOG_DEBUG, "Found data: '%s'\n", buf);
                        char *bufptr = &buf[0];
                        if (strncasecmp(bufptr, (char *)"help", 4) == 0)
                        {
                            STREAM_SEND(i, MGMT_HELP);
                            if (friendly)
                            {
                                STREAM_SEND(i, MGMT_PROMPT);
                            }
                        }
                        else if (strncasecmp(bufptr, (char *)"counters", 8) == 0)
                        {
                            /* send counters */

                            statsd_counter_t *s_counter, *tmp;
                            HASH_ITER(hh, counters, s_counter, tmp)
                            {
                                STREAM_SEND(i, s_counter->key);
                                STREAM_SEND(i, ": ");
                                STREAM_SEND_LONG_DOUBLE(i, s_counter->value);
                                STREAM_SEND(i, "\n");
                            }
                            if (s_counter)
                                free(s_counter);
                            if (tmp)
                                free(tmp);

                            STREAM_SEND(i, MGMT_END);
                            if (friendly)
                            {
                                STREAM_SEND(i, MGMT_PROMPT);
                            }
                        }
                        else if (strncasecmp(bufptr, (char *)"timers", 6) == 0)
                        {
                            /* send timers */

                            statsd_timer_t *s_timer, *tmp;
                            HASH_ITER(hh, timers, s_timer, tmp)
                            {
                                STREAM_SEND(i, s_timer->key);
                                STREAM_SEND(i, ": ");
                                STREAM_SEND_INT(i, s_timer->count);

                                if (s_timer->count > 0)
                                {
                                    double *j = NULL; bool first = 1;
                                    STREAM_SEND(i, " [");
                                    while ( (j=(double *)utarray_next(s_timer->values, j)) )
                                    {
                                        if (first == 1)
                                        {
                                            first = 0;
                                            STREAM_SEND(i, ",");
                                        }
                                        STREAM_SEND_DOUBLE(i, *j);
                                    }
                                    STREAM_SEND(i, "]");
                                }
                                STREAM_SEND(i, "\n");
                            }
                            if (s_timer)
                                free(s_timer);
                            if (tmp)
                                free(tmp);

                            STREAM_SEND(i, MGMT_END);
                            if (friendly)
                            {
                                STREAM_SEND(i, MGMT_PROMPT);
                            }
                        }
                        else if (strncasecmp(bufptr, (char *)"stats", 5) == 0)
                        {
                            /* send stats */

                            statsd_stat_t *s_stat, *tmp;
                            HASH_ITER(hh, stats, s_stat, tmp)
                            {
                                if (strlen(s_stat->name.group_name) > 1)
                                {
                                    STREAM_SEND(i, s_stat->name.group_name);
                                    STREAM_SEND(i, ".");
                                }
                                STREAM_SEND(i, s_stat->name.key_name);
                                STREAM_SEND(i, ": ");
                                STREAM_SEND_LONG(i, s_stat->value);
                                STREAM_SEND(i, "\n");
                            }
                            if (s_stat)
                                free(s_stat);
                            if (tmp)
                                free(tmp);

                            STREAM_SEND(i, MGMT_END);
                            if (friendly)
                            {
                                STREAM_SEND(i, MGMT_PROMPT);
                            }
                        }
                        else if (strncasecmp(bufptr, (char *)"quit", 4) == 0)
                        {
                            /* disconnect */
                            close(i);
                            FD_CLR(i, &master);
                        }
                        else
                        {
                            STREAM_SEND(i, MGMT_BADCOMMAND);
                            if (friendly)
                            {
                                STREAM_SEND(i, MGMT_PROMPT);
                            }
                        }
                    }
                }
            }
        }
    }

    /* end mgmt listener */

    syslog(LOG_INFO, "Thread[Mgmt]: Ending thread");
}
