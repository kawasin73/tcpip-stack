#include "raw.h"
#include <signal.h>
#include <stdio.h>

volatile sig_atomic_t terminate;

static void on_signal(int s) { terminate = 1; }

static void dump(uint8_t *frame, size_t len, void *arg) {
  fprintf(stderr, "%s: receive %zu octets\n", (char *)arg, len);
}

int main(int argc, char const *argv[]) {
  char *name = "eth1";
  struct rawdev *dev;
  uint8_t addr[6];

  signal(SIGINT, on_signal);

  dev = rawdev_alloc(RAWDEV_TYPE_AUTO ,name);
  if (dev == NULL) {
    fprintf(stderr, "rawdev_alloc(): error\n");
    return -1;
  }
  if (dev->ops->open(dev) == -1) {
    fprintf(stderr, "dev->ops->open(): failure - (%s)\n", dev->name);
    return -1;
  }
  dev->ops->addr(dev, addr, sizeof(addr));
  fprintf(stderr, "[%s] %02x:%02x:%02x:%02x:%02x:%02x\n", name, addr[0],
          addr[1], addr[2], addr[3], addr[4], addr[5]);
  while (!terminate) {
    dev->ops->rx(dev, dump, dev, 1000);
  }

  dev->ops->close(dev);

  printf("closed");

  return 0;
}
