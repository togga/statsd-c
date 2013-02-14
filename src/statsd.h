/*
 * statsd
 *
 * http://github.com/alanpearson/statsd-c
 * (based upon http://github.com/jbuchbinder/statsd-c)
 *
 */

#ifndef STATSD_H_INCLUDED
#define STATSD_H_INCLUDED

extern int debug, friendly;

void cleanup();
void process_stats_packet(char buf_in[]);

#define DPRINTF if (debug) printf

#define BUFLEN 1024

/* Default statsd ports */
#define PORT 8125
#define MGMT_PORT 8126

/* Define stat flush interval in sec */
#define FLUSH_INTERVAL 10

#define STREAM_SEND(x,y) if (send(x, y, strlen(y), 0) == -1) { perror("send error"); }
#define STREAM_SEND_LONG(x,y) { \
    char z[32]; \
    sprintf(z, "%ld", y); \
    if (send(x, z, strlen(z), 0) == -1) { perror("send error"); } \
    }
#define STREAM_SEND_INT(x,y) { \
    char z[32]; \
    sprintf(z, "%d", y); \
    if (send(x, z, strlen(z), 0) == -1) { perror("send error"); } \
    }
#define STREAM_SEND_DOUBLE(x,y) { \
    char z[32]; \
    sprintf(z, "%f", y); \
    if (send(x, z, strlen(z), 0) == -1) { perror("send error"); } \
    }
#define STREAM_SEND_LONG_DOUBLE(x,y) { \
    char z[32]; \
    sprintf(z, "%Lf", y); \
    if (send(x, z, strlen(z), 0) == -1) { perror("send error"); } \
    }
#define UPDATE_LAST_MSG_SEEN() { \
    char time_sec[32]; \
    sprintf(time_sec, "%ld", time(NULL)); \
    update_stat( "messages", "last_msg_seen", time_sec); \
    }

#define MGMT_END "END\n\n"
#define MGMT_BADCOMMAND "ERROR\n"
#define MGMT_PROMPT "statsd> "
#define MGMT_HELP "Commands: stats, counters, timers, quit\n\n"

#endif
