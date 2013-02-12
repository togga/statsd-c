#ifndef MGMT_H_INCLUDED
#define MGMT_H_INCLUDED

struct mgmt {
    struct event * ev;
    int sock;
    short port;
};

struct mgmt * mgmt_new(short port);
void mgmt_start(struct mgmt * mgmt, struct event_base *ev_base);
void mgmt_cleanup(struct mgmt * mgmt);

#endif
