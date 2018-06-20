#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "statsd.h"

extern int graphite_port;
extern char* graphite_host;

static int sockfd = 0;

int init_gauge_relay()
{
  struct addrinfo hints;
  int s;
  struct addrinfo *result, *rp;

  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_DGRAM;

  char portstr[64];
  sprintf(portstr, "%d", graphite_port);

  s = getaddrinfo(graphite_host, portstr, &hints, &result);
  if (s != 0) {
    DPRINTF("gauge_relay: getaddrinfo failed %d\n", s);
    return 2;
  }

  for (rp = result; rp != NULL; rp = rp->ai_next) {
    sockfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
    if (sockfd < 0) {
      continue;
    }
    if (connect(sockfd, rp->ai_addr, rp->ai_addrlen) != -1) {
      break;
    }
    close(sockfd);
  }

  freeaddrinfo(result);

  if (rp == NULL) {
    DPRINTF("gauge_relay: failed to connect socket\n");
    sockfd = 0;
    return 3;
  }

  DPRINTF("gauge_relay: initiated\n");
  return 0;
}

void gauge_relay_cleanup()
{
  if (sockfd) {
    close(sockfd);
  }
}

void relay_gauge(const char* key, double value)
{
  if (!sockfd) {
    DPRINTF("gauge_relay: warning - no open socket\n");
    return;
  }

  char* buffer;
  long ts = time(NULL);
  buffer = malloc(strlen(key) + 256);
  sprintf(buffer, "stats.%s %f %ld\n", key, value, ts);
  DPRINTF("gauge_relay: sending %s\n", buffer);
  int r = write(sockfd, buffer, strlen(buffer));
  if (r) {}
  free(buffer);
}
