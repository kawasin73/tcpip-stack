#include "tcp.h"
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include "arp.h"
#include "ethernet.h"
#include "ip.h"
#include "net.h"
#include "raw.h"

static int setup(void) {
  if (ethernet_init() == -1) {
    fprintf(stderr, "ethernet_init(): failure\n");
    return -1;
  } else if (ip_init() == -1) {
    fprintf(stderr, "ip_init(): failure\n");
    return -1;
  } else if (arp_init() == -1) {
    fprintf(stderr, "arp_init(): failure\n");
    return -1;
  } else if (tcp_init() == -1) {
    fprintf(stderr, "tcp_init(): failure\n");
    return -1;
  }
  return 0;
}

int main(int argc, char const *argv[]) {
  sigset_t sigset;
  int signo;
  struct netdev *dev;
  struct netif *netif;
  char *name = "tap1", *ipaddr = "192.168.33.11", *netmask = "255.255.0.0";
  int soc;

  // sigemptyset(&sigset);
  // sigaddset(&sigset, SIGINT);
  // sigprocmask(SIG_BLOCK, &sigset, NULL);
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

  // set netif
  netif = ip_netif_register(dev, ipaddr, netmask, NULL);

  dev->ops->run(dev);

  ip_addr_t dst;
  if (ip_addr_pton("192.168.33.10", &dst) != 0) {
    fprintf(stderr, "ip_addr_pton: failed\n");
    return -1;
  }

  soc = tcp_api_open();
  if (soc == -1) {
    fprintf(stderr, "tcp_api_open: failed\n");
    return -1;
  }

  fprintf(stderr, "sleep\n");
  sleep(10);
  fprintf(stderr, "start\n");

  if (tcp_api_connect(soc, &dst, 19999) == -1) {
    fprintf(stderr, "tcp_api_connect: failed\n");
    return -1;
  }

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
