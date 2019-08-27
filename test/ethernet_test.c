#include "ethernet.h"
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include "net.h"
#include "raw.h"

static int setup(void) {
  if (ethernet_init() == -1) {
    fprintf(stderr, "ethernet_init(): failure\n");
    return -1;
  }
  return 0;
}

int main(int argc, char const *argv[]) {
  sigset_t sigset;
  int signo;
  struct netdev *dev;
  char *name = "tap1";

  sigemptyset(&sigset);
  sigaddset(&sigset, SIGINT);
  sigprocmask(SIG_BLOCK, &sigset, NULL);
  if (setup() == -1) {
    return -1;
  }

  // setup
  dev = netdev_alloc(NETDEV_TYPE_ETHERNET);
  if (!dev) {
    fprintf(stderr, "netdev_alloc() : failed\n");
    return -1;
  }
  strncpy(dev->name, name, sizeof(dev->name) - 1);
  if (dev->ops->open(dev, RAWDEV_TYPE_AUTO) == -1) {
    fprintf(stderr, "failed to open raw device\n");
    return -1;
  }
  dev->ops->run(dev);

  while (1) {
    sigwait(&sigset, &signo);
    if (signo == SIGINT) {
      break;
    }
  }
  if (dev->ops->close) {
    dev->ops->close(dev);
  }
  fprintf(stderr, "closed\n");
  return 0;
}
