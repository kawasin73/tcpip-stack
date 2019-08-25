#include "net.h"
#include <stdlib.h>

static struct netdev_driver {
  struct netdev_driver *next;
  uint16_t type;
  uint16_t mtu;
  uint16_t flags;
  uint16_t hlen;
  uint16_t alen;
  struct netdev_ops *ops;
};

static struct netdev_driver *drivers = NULL;
static struct netdev *devices = NULL;

int netdev_driver_register(struct netdev_def *def) {
  struct netdev_driver *entry;

  for (entry = drivers; entry; entry = entry->next) {
    if (entry->type == def->type) {
      return -1;
    }
  }
  entry = malloc(sizeof(struct netdev_driver));
  if (!entry) {
    return -1;
  }
  entry->next = drivers;
  entry->type = def->type;
  entry->mtu = def->mtu;
  entry->flags = def->flags;
  entry->hlen = def->hlen;
  entry->alen = def->alen;
  entry->ops = def->ops;
  drivers = entry;
  return 0;
}

static void netdev_rx_handler(struct netdev* dev, uint16_t type, uint8_t *packet, size_t plen) {
  // TODO: handle protocol
}

struct netdev *netdev_alloc(uint16_t type) {
  struct netdev_driver *driver;
  struct netdev *dev;

  // find driver matches type
  for (driver = drivers; driver; driver = driver->next) {
    if (driver->type == type) {
      break;
    }
  }
  if (!driver) {
    return NULL;
  }

  dev = malloc(sizeof(struct netdev));
  if (!dev) {
    return NULL;
  }
  dev->next = devices;
  dev->ifs = NULL;
  dev->type = driver->type;
  dev->mtu = driver->mtu;
  dev->flags = driver->flags;
  dev->hlen = driver->hlen;
  dev->alen = driver->alen;
  dev->rx_handler = netdev_rx_handler;
  dev->ops = driver->ops;
  devices = dev;
  return dev;
}
