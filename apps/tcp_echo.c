#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include "arp.h"
#include "ethernet.h"
#include "ip.h"
#include "net.h"
#include "raw.h"
#include "tcp.h"
#include "util.h"

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

static void *handler(void *arg) {
  int soc = arg, n;
  uint8_t buf[1024];

  while (1) {
    n = tcp_api_recv(soc, buf, 1024);
    if (n == -1) {
      fprintf(stderr, "tcp_api_recv: failed\n");
      break;
    } else if (n == 0) {
      fprintf(stderr, "tcp_api_recv: size is 0\n");
    } else {
      fprintf(stderr, ">>> recv data <<<\n");
      hexdump(stderr, buf, n);
    }

    n = tcp_api_send(soc, buf, n);
    if (n == -1) {
      fprintf(stderr, "tcp_api_send: failed\n");
      break;
    } else {
      fprintf(stderr, ">>> send data success <<<\n");
    }
    if (n >= 4 && strncmp("quit", buf, 4) == 0) {
      break;
    }
  }

  if (tcp_api_close(soc) == -1) {
    fprintf(stderr, "tcp_api_close: failed\n");
  } else {
    fprintf(stderr, "tcp_api_close: success\n");
  }

  return NULL;
}

static void *accept_handler(void *arg) {
  int listener = arg, soc;
  pthread_t th;

  while (1) {
    soc = tcp_api_accept(listener);
    if (soc == -1) {
      fprintf(stderr, "tcp_api_accept: failed\n");
      return NULL;
    }
    fprintf(stderr, "tcp_api_accept success: soc : %d\n", soc);

    pthread_create(&th, NULL, handler, soc);
  }

  return NULL;
}

int main(int argc, char const *argv[]) {
  sigset_t sigset;
  int signo;
  struct netdev *dev;
  struct netif *netif;
  char *name = "tap2", *ipaddr = "192.168.33.13", *netmask = "255.255.0.0";
  int listener;
  uint8_t buf[1024];
  size_t n;
  pthread_t th;

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
  netif = ip_netif_register(dev, ipaddr, netmask, NULL);

  dev->ops->run(dev);

  listener = tcp_api_open();
  if (listener == -1) {
    fprintf(stderr, "tcp_api_open: failed\n");
    return -1;
  }

  if (tcp_api_bind(listener, 20000) == -1) {
    fprintf(stderr, "tcp_api_bind: failed\n");
    return -1;
  }
  if (tcp_api_listen(listener) == -1) {
    fprintf(stderr, "tcp_api_listen: failed\n");
    return -1;
  }

  fprintf(stderr, "tcp_api_listen success\n");

  pthread_create(&th, NULL, accept_handler, listener);

  while (1) {
    sigwait(&sigset, &signo);
    if (signo == SIGINT) {
      break;
    }
  }

  if (tcp_api_close(listener) == -1) {
    fprintf(stderr, "tcp_api_close: failed\n");
    return -1;
  }

  if (dev->ops->close) {
    dev->ops->close(dev);
  }
  fprintf(stderr, "closed\n");
  return 0;
}
