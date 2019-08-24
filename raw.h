#ifndef RAW_H
#define RAW_H

#include <stddef.h>
#include <stdint.h>
#include <unistd.h>

#define RAWDEV_TYPE_AUTO 0
#define RAWDEV_TYPE_TAP 1
#define RAWDEV_TYPE_SOCKET 2

struct rawdev;

struct rawdev_ops {
  int (*open)(struct rawdev *dev);
  void (*close)(struct rawdev *dev);
  void (*rx)(struct rawdev *dev, void (*callback)(uint8_t *, size_t, void *),
             void *arg, int timeout);
  ssize_t (*tx)(struct rawdev *dev, const uint8_t *buf, size_t len);
  int (*addr)(struct rawdev *dev, uint8_t *dst, size_t size);
};

struct rawdev {
  uint8_t type;
  char *name;
  struct rawdev_ops *ops;
  void *priv;
};

struct rawdev *rawdev_alloc(uint8_t type, char *name);

#endif
