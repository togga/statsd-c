#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <pthread.h>
#include <semaphore.h>
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
#include "udp.h"

int stats_udp_socket;
void udp_event_callback(int, short, void *);

extern int port;

void die_with_error(char *s)
{
    perror(s);
    exit(1);
}


struct udp * udp_new()
{
    struct udp * udp;

    udp = (struct udp *) malloc(sizeof(struct udp));
    if (udp)
    {
        memset(udp,0,sizeof(struct udp));
    }

    return udp;
}

void udp_cleanup(struct udp * udp)
{
    if (udp->ev)
    {
        event_del(udp->ev);
        event_free(udp->ev);
    }

    if (stats_udp_socket)
    {
        syslog(LOG_INFO, "Closing UDP stats socket.");
        close(stats_udp_socket);
    }

    free(udp);
}


void udp_start(struct udp * udp, struct event_base * ev_base)
{
    struct sockaddr_in si_me;
    int on, flags;

    /* begin udp listener */
    syslog(LOG_INFO, "Thread[Udp]: Starting UDP listener\n");

    if ((stats_udp_socket = socket(AF_INET, SOCK_DGRAM, 0))==-1)
        die_with_error("UDP: Could not grab socket.");

    /* Reuse socket, please */
    on = 1;
    setsockopt(stats_udp_socket, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

    /* Use non-blocking sockets */
    flags = fcntl(stats_udp_socket, F_GETFL, 0);
    fcntl(stats_udp_socket, F_SETFL, flags | O_NONBLOCK);

    memset((char *) &si_me, 0, sizeof(si_me));
    si_me.sin_family = AF_INET;
    si_me.sin_port = htons(port);
    si_me.sin_addr.s_addr = htonl(INADDR_ANY);
    syslog(LOG_DEBUG, "UDP: Binding to socket.");
    if (bind(stats_udp_socket, (struct sockaddr *)&si_me, sizeof(si_me))==-1)
            die_with_error("UDP: Could not bind");
    syslog(LOG_DEBUG, "UDP: Bound to socket on port %d", port);

    udp->ev = event_new(ev_base, stats_udp_socket, EV_READ | EV_PERSIST, udp_event_callback, NULL);
    event_add(udp->ev,NULL);
}


void udp_event_callback(int fd, short flags, void * param)
{
    char buf_in[BUFLEN];
    struct sockaddr_in si_other;

    syslog(LOG_DEBUG, "UDP: callback event %d", port);

    memset(&buf_in, 0, sizeof(buf_in));
    if (read(stats_udp_socket, buf_in, sizeof(buf_in)) <= 0)
    {
        syslog(LOG_DEBUG, "UDP: read failed");
    }

    /* make sure that the buf_in is NULL terminated */
    buf_in[BUFLEN - 1] = 0;

    syslog(LOG_DEBUG, "UDP: Received packet from %s:%d\nData: %s\n\n",
            inet_ntoa(si_other.sin_addr), ntohs(si_other.sin_port), buf_in);

    syslog(LOG_DEBUG, "UDP: Processing packet");
    process_stats_packet(buf_in);
}
