#ifndef GAUGE_RELAY_H_INCLUDED
#define GAUGE_RELAY_H_INCLUDED

int init_gauge_relay();
void gauge_relay_cleanup();
void relay_gauge(const char* key, double value);

#endif
