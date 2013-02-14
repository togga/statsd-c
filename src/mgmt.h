/*
 * statsd
 *
 * http://github.com/alanpearson/statsd-c
 * (based upon http://github.com/jbuchbinder/statsd-c)
 *
 */

#ifndef MGMT_H_INCLUDED
#define MGMT_H_INCLUDED

struct mgmt {
    struct event * ev;
    struct evconnlistener *listener;
    int sock;
    short port;
};

struct mgmt * mgmt_new(short port);
int mgmt_start(struct mgmt * mgmt, struct event_base * ev_base);
void mgmt_cleanup(struct mgmt * mgmt);

#define STREAM_SEND(b,str) evbuffer_add(b,str,strlen(str))
#define STREAM_SENDLEN(b,str,n) evbuffer_add(b,str,n)

#define STREAM_SEND_LONG(b,y) { \
    char z[32]; int n; \
    n = sprintf(z, "%ld", y); \
    STREAM_SENDLEN(b,z,n); \
    }
#define STREAM_SEND_INT(b,y) { \
    char z[32]; int n; \
    n = sprintf(z, "%d", y); \
    STREAM_SENDLEN(b,z,n); \
    }
#define STREAM_SEND_DOUBLE(b,y) { \
    char z[32]; int n; \
    n = sprintf(z, "%f", y); \
    STREAM_SENDLEN(b,z,n); \
    }
#define STREAM_SEND_LONG_DOUBLE(b,y) { \
    char z[32]; int n; \
    n = sprintf(z, "%Lf", y); \
    STREAM_SENDLEN(b,z,n); \
    }

#define MGMT_END "END\n\n"
#define MGMT_BADCOMMAND "ERROR\n"
#define MGMT_PROMPT "statsd> "
#define MGMT_HELP "Commands: stats, counters, timers, quit\n\n"


#endif
