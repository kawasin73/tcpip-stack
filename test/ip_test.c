#include "ip.h"
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include "ethernet.h"
#include "net.h"
#include "raw.h"

#define IP_ORIGINAL_PROTOCOL 100

static int setup(void) {
  if (ethernet_init() == -1) {
    fprintf(stderr, "ethernet_init(): failure\n");
    return -1;
  } else if (ip_init() == -1) {
    fprintf(stderr, "ip_init(): failure\n");
    return -1;
  }
  return 0;
}

int main(int argc, char const *argv[]) {
  sigset_t sigset;
  int signo;
  struct netdev *dev;
  char *name = "eth1", *ipaddr = "192.168.33.10";
  struct netif_ip iface = {};

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

  // set netif
  iface.netif.family = NETIF_FAMILY_IPV4;
  ip_addr_pton(ipaddr, &iface.unicast);
  netdev_add_netif(dev, (struct netif *)&iface);

  dev->ops->run(dev);

  char *data = "hello world";
  ip_addr_t dst;
  if (ip_addr_pton("192.168.33.1", &dst) != 0) {
    fprintf(stderr, "ip_addr_pton: failed\n");
    return -1;
  }
  if (ip_tx((struct netif *)&iface, IP_ORIGINAL_PROTOCOL, (uint8_t *)data, 12,
            &dst) == -1) {
    fprintf(stderr, "ip_tx: failed\n");
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
