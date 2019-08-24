#include "raw.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "raw.h"

#ifdef HAVE_PF_PACKET
#include "raw/soc.h"
#define RAWDEV_TYPE_DEFAULT RAWDEV_TYPE_SOCKET
extern struct rawdev_ops soc_dev_ops;
#endif

static uint8_t rawdev_detect_type(char *name) {
  if (strncmp(name, "tap", 3) == 0) {
    return RAWDEV_TYPE_TAP;
  }
  return RAWDEV_TYPE_DEFAULT;
}

struct rawdev *rawdev_alloc(uint8_t type, char *name) {
  struct rawdev *dev;
  struct rawdev_ops *ops;

  if (type == RAWDEV_TYPE_AUTO) {
    type = rawdev_detect_type(name);
  }

  // set ops
  switch (type) {
#ifdef HAVE_PF_PACKET
    case RAWDEV_TYPE_SOCKET:
      ops = &soc_dev_ops;
      break;
#endif
    default:
      fprintf(stderr, "unsupported raw device type (%u)\n", type);
      return NULL;
  }

  dev = malloc(sizeof(struct rawdev));
  if (!dev) {
    fprintf(stderr, "malloc rawdev: failure\n");
    return NULL;
  }
  dev->type = type;
  dev->name = name;
  dev->ops = ops;
  dev->priv = NULL;
  return dev;
}
