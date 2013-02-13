/*
 * statsd
 *
 * http://github.com/alanpearson/statsd-c
 * (based upon http://github.com/jbuchbinder/statsd-c)
 *
 */

#ifndef FLUSH_H_INCLUDED
#define FLUSH_H_INCLUDED

struct flush {
    struct event * ev;
    int interval;
};

struct flush * flush_new(int interval);
void flush_start(struct flush * flush, struct event_base *ev_base);
void flush_cleanup(struct flush * flush);

#endif
