#ifndef UDP_H_INCLUDED
#define UDP_H_INCLUDED

struct udp {
    struct event * ev;
};

struct udp * udp_new();
void udp_start(struct udp * udp, struct event_base *ev_base);
void udp_cleanup(struct udp * udp);

#endif

