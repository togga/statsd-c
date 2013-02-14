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

#define UPDATE_LAST_MSG_SEEN() { \
    char time_sec[32]; \
    sprintf(time_sec, "%ld", time(NULL)); \
    update_stat( "messages", "last_msg_seen", time_sec); \
    }

#endif
