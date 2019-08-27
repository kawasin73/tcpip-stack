#ifndef TAP_DEV_H
#define TAP_DEV_H

#include <stdint.h>
#include <stddef.h>
#include <unistd.h>

struct tap_dev;

struct tap_dev *tap_dev_open(char *name);
void tap_dev_close(struct tap_dev *dev);
void tap_dev_rx(struct tap_dev *dev,
                void (*callback)(uint8_t *, size_t, void *), void *arg,
                int timeout);
ssize_t tap_dev_tx(struct tap_dev *dev, const uint8_t *buf, size_t len);
int tap_dev_addr(char *name, uint8_t *dst, size_t size);

#endif
