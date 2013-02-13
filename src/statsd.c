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
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

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
#include "flush.h"
#include "mgmt.h"

#define STATSD_VERSION "0.1.0"
#define LOCK_FILE "/tmp/statsd.lock"

/*
 * GLOBAL VARIABLES
 */

statsd_stat_t *stats = NULL;
statsd_counter_t *counters = NULL;
statsd_gauge_t *gauges = NULL;
statsd_timer_t *timers = NULL;
UT_icd timers_icd = { sizeof(double), NULL, NULL, NULL };

struct event *ev_sig_hup = NULL, *ev_sig_term = NULL;
struct event *ev_sig_int = NULL, *ev_sig_quit = NULL;

int port = PORT, mgmt_port = MGMT_PORT, flush_interval = FLUSH_INTERVAL;
int debug = 0, friendly = 0, clear_stats = 0, daemonize = 0, graphite_port = 2003;
char *graphite_host = "localhost", *lock_file = NULL;
int percentiles[5], num_percentiles = 0;

void add_timer( char *key, double value );
void update_stat( char *group, char *key, char *value);
void update_counter( char *key, double value, double sample_rate );
void update_gauge( char *key, double value );
void update_timer( char *key, double value );
void process_stats_packet(char buf_in[]);

void init_stats()
{
    char startup_time[12];
    sprintf(startup_time, "%ld", time(NULL));

    update_stat("graphite", "last_flush", startup_time);
    update_stat("messages", "last_msg_seen", startup_time);
    update_stat("messages", "bad_lines_seen", "0");
}

void cleanup()
{
    if (ev_sig_int) event_free(ev_sig_int);
    if (ev_sig_quit) event_free(ev_sig_quit);
    if (ev_sig_term) event_free(ev_sig_term);
    if (ev_sig_hup) event_free(ev_sig_hup);

    syslog(LOG_INFO, "Removing lockfile %s", lock_file != NULL ? lock_file : LOCK_FILE);
    unlink(lock_file != NULL ? lock_file : LOCK_FILE);
}

static void signal_cb(evutil_socket_t fd, short event, void *arg)
{
    struct event *signal = *(struct event **)arg;
    event_base_loopbreak(event_get_base(signal));
}

void sighup_handler (int signum)
{
    syslog(LOG_ERR, "SIGHUP caught");
    cleanup();
    exit(1);
}

void sigterm_handler (int signum)
{
    syslog(LOG_ERR, "SIGTERM caught");
    cleanup();
    exit(1);
}

void daemonize_server(struct event_base* event_base)
{
    int pid;
    int lockfp;
    char str[10];

    if (getppid() == 1)
    {
        return;
    }

    pid = fork();
    if (pid < 0)
    {
        exit(1);
    }
    if (pid > 0)
    {
        exit(0);
    }

    /* Try to become root, but ignore if we can't */
    setuid((uid_t) 0);
    errno = 0;

    setsid();
    for (pid = getdtablesize(); pid>=0; --pid)
    {
        close(pid);
    }
    pid = open("/dev/null", O_RDWR); dup(pid); dup(pid);
    umask((mode_t) 022);
    lockfp = open(lock_file != NULL ? lock_file : LOCK_FILE, O_RDWR | O_CREAT, 0640);
    if (lockfp < 0)
    {
        syslog(LOG_ERR, "Could not serialize PID to lock file");
        exit(1);
    }
    if (lockf(lockfp, F_TLOCK,0)<0)
    {
        syslog(LOG_ERR, "Could not create lock, bailing out");
        exit(0);
    }
    sprintf(str, "%d\n", getpid());
    write(lockfp, str, strlen(str));
    close(lockfp);

    /* Signal handling */
    signal(SIGCHLD, SIG_IGN);
    signal(SIGTSTP, SIG_IGN);
    signal(SIGTTOU, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);

    ev_sig_hup = evsignal_new(event_base, SIGHUP, signal_cb, &ev_sig_hup);
    event_add(ev_sig_int, NULL);

    ev_sig_term = evsignal_new(event_base, SIGTERM, signal_cb, &ev_sig_term);
    event_add(ev_sig_term, NULL);
}

void syntax(char *argv[])
{
    fprintf(stderr, "statsd version %s\n\n", STATSD_VERSION);
    fprintf(stderr, "Usage: %s [-hDdfFc] [-p port] [-m port] [-s file] [-G host] [-g port] [-S spoofhost] [-P prefix] [-l lockfile] [-T percentiles]\n", argv[0]);
    fprintf(stderr, "\t-p port         set statsd udp listener port (default 8125)\n");
    fprintf(stderr, "\t-m port         set statsd management port (default 8126)\n");
    fprintf(stderr, "\t-R host         graphite host (default disabled)\n");
    fprintf(stderr, "\t-l lockfile     lock file (only used when daemonizing)\n");
    fprintf(stderr, "\t-h              this help display\n");
    fprintf(stderr, "\t-d              enable debug\n");
    fprintf(stderr, "\t-D              daemonize\n");
    fprintf(stderr, "\t-f              enable friendly mode (breaks wire compatibility)\n");
    fprintf(stderr, "\t-F seconds      set flush interval in seconds (default 10)\n");
    fprintf(stderr, "\t-c              clear stats on startup\n");
    fprintf(stderr, "\t-T              percentile thresholds, csv (defaults to 90)\n");
    exit(1);
}

int main(int argc, char *argv[])
{
    int opt;
    char *p_raw, *pch;
    struct event_base *event_base;
    struct udp *udp = NULL;
    struct flush *flush = NULL;

    while ((opt = getopt(argc, argv, "dDfhp:m:s:cg:G:F:S:P:l:T:R:")) != -1)
    {
        switch (opt)
        {
        case 'd':
            printf("Debug enabled.\n");
            debug = 1;
            break;
        case 'D':
            printf("Daemonize enabled.\n");
            daemonize = 1;
            break;
        case 'f':
            printf("Friendly mode enabled (breaks wire compatibility).\n");
            friendly = 1;
            break;
        case 'F':
            flush_interval = atoi(optarg);
            printf("Flush interval set to %d seconds.\n", flush_interval);
            break;
        case 'p':
            port = atoi(optarg);
            printf("Statsd port set to %d\n", port);
            break;
        case 'm':
            mgmt_port = atoi(optarg);
            printf("Management port set to %d\n", mgmt_port);
            break;
        case 'c':
            clear_stats = 1;
            printf("Clearing stats on start.\n");
            break;
        case 'R':
            graphite_host = strdup(optarg);
            printf("Graphite host %s\n", graphite_host);
            break;
        case 'l':
            lock_file = strdup(optarg);
            printf("Lock file %s\n", lock_file);
            break;
        case 'T':
            p_raw = strdup(optarg);
            pch = strtok (p_raw, ",");
            while (pch != NULL)
            {
                percentiles[num_percentiles] = atoi(pch);
                pch = strtok (p_raw, ",");
                num_percentiles++;
            }
            printf("Percentiles %s (%d values)\n", p_raw, num_percentiles);
            break;
        case 'h':
        default:
            syntax(argv);
            break;
        }
    }

    if (num_percentiles == 0)
    {
        percentiles[0] = 90;
        num_percentiles = 1;
    }

    if (debug)
    {
        setlogmask(LOG_UPTO(LOG_DEBUG));
        openlog("statsd-c",  LOG_CONS | LOG_NDELAY | LOG_PERROR | LOG_PID, LOG_USER);
    }
    else
    {
        setlogmask(LOG_UPTO(LOG_INFO));
        openlog("statsd-c", LOG_CONS, LOG_USER);
    }

    /* Initialization of certain stats, here. */
    init_stats();

    /* allocate the libevent base object */
    event_base = event_base_new();

    /* Initalize signal handling events */
    ev_sig_int = evsignal_new(event_base, SIGINT, signal_cb, &ev_sig_int);
    event_add(ev_sig_int, NULL);

    ev_sig_quit = evsignal_new(event_base, SIGQUIT, signal_cb, &ev_sig_quit);
    event_add(ev_sig_quit, NULL);

    if (daemonize)
    {
        syslog(LOG_DEBUG, "Daemonizing statsd-c");
        daemonize_server(event_base);
    }

    /* start the UDP listener */
    udp = udp_new();
    udp_start(udp,event_base);

    /* start the flush timer */
    flush = flush_new(flush_interval);
    flush_start(flush,event_base);

    syslog(LOG_DEBUG, "Entering event loop");
    event_base_dispatch(event_base);
    syslog(LOG_DEBUG, "Exiting event loop");

    cleanup();

    if (flush)
    {
        flush_cleanup(flush);
    }

    if (udp)
    {
        udp_cleanup(udp);
    }

    event_base_free(event_base);

    return 0;
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

/**
 * Record or update stat value.
 */
void update_stat( char *group, char *key, char *value )
{
    syslog(LOG_DEBUG, "update_stat ( %s, %s, %s )\n", group, key, value);
    statsd_stat_t *s;
    statsd_stat_name_t l;

    memset(&l, 0, sizeof(statsd_stat_name_t));
    strcpy(l.group_name, group);
    strcpy(l.key_name, key);
    syslog(LOG_DEBUG, "HASH_FIND '%s' '%s'\n", l.group_name, l.key_name);
    HASH_FIND( hh, stats, &l, sizeof(statsd_stat_name_t), s );

    if (s)
    {
        syslog(LOG_DEBUG, "Updating old stat entry");

        s->value = atol( value );
    }
    else
    {
        syslog(LOG_DEBUG, "Adding new stat entry");
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
    syslog(LOG_DEBUG, "update_counter ( %s, %f, %f )\n", key, value, sample_rate);
    statsd_counter_t *c;
    HASH_FIND_STR( counters, key, c );
    if (c)
    {
        syslog(LOG_DEBUG, "Updating old counter entry");
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
        syslog(LOG_DEBUG, "Adding new counter entry");
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
    syslog(LOG_DEBUG, "update_gauge ( %s, %f )\n", key, value);
    statsd_gauge_t *g;
    syslog(LOG_DEBUG, "HASH_FIND_STR '%s'\n", key);
    HASH_FIND_STR( gauges, key, g );
    syslog(LOG_DEBUG, "after HASH_FIND_STR '%s'\n", key);
    if (g)
    {
        syslog(LOG_DEBUG, "Updating old timer entry");
        g->value = value;
    }
    else
    {
        syslog(LOG_DEBUG, "Adding new timer entry");
        g = malloc(sizeof(statsd_gauge_t));

        strcpy(g->key, key);
        g->value = value;

        HASH_ADD_STR( gauges, key, g );
    }
}

void update_timer(char *key, double value)
{
    syslog(LOG_DEBUG, "update_timer ( %s, %f )\n", key, value);
    statsd_timer_t *t;
    syslog(LOG_DEBUG, "HASH_FIND_STR '%s'\n", key);
    HASH_FIND_STR( timers, key, t );
    syslog(LOG_DEBUG, "after HASH_FIND_STR '%s'\n", key);
    if (t)
    {
        syslog(LOG_DEBUG, "Updating old timer entry");
        utarray_push_back(t->values, &value);
        t->count++;
    }
    else
    {
        syslog(LOG_DEBUG, "Adding new timer entry");
        t = malloc(sizeof(statsd_timer_t));

        strcpy(t->key, key);
        t->count = 0;
        utarray_new(t->values, &timers_icd);
        utarray_push_back(t->values, &value);
        t->count++;

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
    char *s_sample_rate = NULL, *s_number = NULL;
    double sample_rate;
    bool is_timer = 0, is_gauge = 0;

    if (strlen(buf_in) < 2)
    {
        UPDATE_LAST_MSG_SEEN()
        return;
    }

    for (i = 1, bits=buf_in; ; i++, bits=NULL)
    {
        syslog(LOG_DEBUG, "i = %d\n", i);
        token = strtok_r(bits, ":", &save);
        if (token == NULL)
        {
            break;
        }

        if (i == 1)
        {
            syslog(LOG_DEBUG, "Found token '%s', key name\n", token);
            key_name = strdup(token);
            sanitize_key(key_name);
            /* break; */
        }
        else
        {
            syslog(LOG_DEBUG, "\ttoken [#%d] = %s\n", i, token);
            s_sample_rate = NULL;
            s_number = NULL;
            is_timer = 0;
            is_gauge = 0;

            if (strstr(token, "|") == NULL)
            {
                syslog(LOG_DEBUG, "No pipes found, basic logic");
                sanitize_value(token);
                syslog(LOG_DEBUG, "\t\tvalue = %s\n", token);
                value = strtod(token, (char **) NULL);
                syslog(LOG_DEBUG, "\t\tvalue = %s => %f\n", token, value);
            }
            else
            {
                for (j = 1, fields = token; ; j++, fields = NULL)
                {
                    subtoken = strtok_r(fields, "|", &subsave);
                    if (subtoken == NULL)
                        break;
                    syslog(LOG_DEBUG, "\t\tsubtoken = %s\n", subtoken);

                    switch (j)
                    {
                    case 1:
                        syslog(LOG_DEBUG, "case 1");
                        sanitize_value(subtoken);
                        value = strtod(subtoken, (char **) NULL);
                        break;
                    case 2:
                        syslog(LOG_DEBUG, "case 2");
                        if (subtoken == NULL)
                            { break ; }
                        if (strlen(subtoken) < 2)
                        {
                            syslog(LOG_DEBUG, "subtoken length < 2");
                            is_timer = 0;
                            if (*subtoken == 'g')
                            {
                                is_gauge = 1;
                            }
                        }
                        else
                        {
                            syslog(LOG_DEBUG, "subtoken length >= 2");
                            if (*subtoken == 'm' && *(subtoken + 1) == 's')
                            {
                                is_timer = 1;
                                is_gauge = 0;
                            }
                        }
                        break;
                    case 3:
                        syslog(LOG_DEBUG, "case 3");
                        if (subtoken == NULL)
                            break ;
                        s_sample_rate = strdup(subtoken);
                        break;
                    }
                }
            }

            syslog(LOG_DEBUG, "Post token processing");

            if (is_timer == 1)
            {
                /* ms passed, handle timer */
                update_timer( key_name, value );
            }
            else if (is_gauge == 1)
            {
                /* Handle non-timer, as gauge */
                update_gauge(key_name, value);
                syslog(LOG_DEBUG, "Found gauge key name '%s'\n", key_name);
                syslog(LOG_DEBUG, "Found gauge value '%f'\n", value);
            }
            else
            {
                if (s_sample_rate && *s_sample_rate == 'g')
                {
                    /* Handle non-timer, as counter */
                    sample_rate = strtod( (s_sample_rate + 1), (char **) NULL );
                }
                else
                {
                    /* sample_rate is assumed to be 1.0 if not specified */
                    sample_rate = 1.0;
                }
                update_counter(key_name, value, sample_rate);
                syslog(LOG_DEBUG, "Found key name '%s'\n", key_name);
                syslog(LOG_DEBUG, "Found value '%f'\n", value);
            }
            if (s_sample_rate)
                free(s_sample_rate);
            if (s_number)
                free(s_number);
        }
    }
    i--; /* For ease */

    syslog(LOG_DEBUG, "After loop, i = %d, value = %f", i, value);

    if (i <= 1)
    {
        /* No value, assign "1" and process */
        update_counter(key_name, value, 1);
    }

    syslog(LOG_DEBUG, "freeing key and value");
    if (key_name)
        free(key_name);

    UPDATE_LAST_MSG_SEEN()
}



