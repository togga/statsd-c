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
#include <time.h>

#include <event2/event.h>

#include "uthash/utarray.h"
#include "uthash/utstring.h"
#include "statsd.h"
#include "stats.h"
#include "counters.h"
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
    int fd, result;
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

    pid = open("/dev/null", O_RDWR);
    fd = dup(pid);
    if (fd == -1)
    {
        syslog(LOG_ERR,"Could not duplicated file descriptor.");
        exit(1);
    }
    fd = dup(pid);
    if (fd == -1)
    {
        syslog(LOG_ERR,"Could not duplicated file descriptor.");
        exit(1);
    }

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
    result = write(lockfp, str, strlen(str));
    (void) result;
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
    struct mgmt *mgmt = NULL;

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
    if ((udp = udp_new()) == NULL)
        goto exit;
    udp_start(udp,event_base);

    /* start the flush timer */
    if ((flush = flush_new(flush_interval)) == NULL)
        goto exit;

    flush_start(flush,event_base);

    /* start the management listener */
    if ((mgmt = mgmt_new(mgmt_port)) != NULL) {
        if (mgmt_start(mgmt,event_base) != 0)
        {
            syslog(LOG_WARNING, "Could not start management interface");
        }
    }

    syslog(LOG_DEBUG, "Entering event loop");
    event_base_dispatch(event_base);
    syslog(LOG_DEBUG, "Exiting event loop");

exit:
    cleanup();

    if (flush)
    {
        flush_cleanup(flush);
    }

    if (udp)
    {
        udp_cleanup(udp);
    }

    if (mgmt)
    {
        mgmt_cleanup(mgmt);
    }

    if (event_base)
    {
        event_base_free(event_base);
    }

    return 0;
}
