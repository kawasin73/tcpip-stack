#include "util.h"
#include <ctype.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void hexdump(FILE *fp, void *data, size_t size) {
  int offset, index;
  unsigned char *src;

  src = (unsigned char *)data;
  fprintf(fp,
          "+------+-------------------------------------------------+----------"
          "--------+\n");
  for (offset = 0; offset < (int)size; offset += 16) {
    fprintf(fp, "| %04x | ", offset);
    for (index = 0; index < 16; index++) {
      if (offset + index < (int)size) {
        fprintf(fp, "%02x ", 0xff & src[offset + index]);
      } else {
        fprintf(fp, "   ");
      }
    }
    fprintf(fp, "| ");
    for (index = 0; index < 16; index++) {
      if (offset + index < (int)size) {
        if (isascii(src[offset + index]) && isprint(src[offset + index])) {
          fprintf(fp, "%c", src[offset + index]);
        } else {
          fprintf(fp, ".");
        }
      } else {
        fprintf(fp, " ");
      }
    }
    fprintf(fp, " |\n");
  }
  fprintf(fp,
          "+------+-------------------------------------------------+----------"
          "--------+\n");
}

/*
 * QUEUE OPERATIONS
 */

int queue_push(struct queue_head *queue, void *data, size_t size) {
  struct queue_entry *entry;
  if (!queue || !data) {
    return -1;
  }
  entry = malloc(sizeof(struct queue_entry));
  if (!entry) {
    return -1;
  }
  entry->data = data;
  entry->size = size;
  entry->next = NULL;
  if (queue->tail) {
    queue->tail->next = entry;
  }
  queue->tail = entry;
  if (!queue->next) {
    queue->next = entry;
  }
  queue->num++;
  return 0;
}

int queue_pop(struct queue_head *queue, void **data, size_t *size) {
  struct queue_entry *entry;
  if (!queue || !queue->next) {
    return -1;
  }
  entry = queue->next;
  queue->next = entry->next;
  if (!queue->next) {
    queue->tail = NULL;
  }
  queue->num--;
  *data = entry->data;
  *size = entry->size;
  free(entry);
  return 0;
}

uint16_t cksum16(uint16_t *data, uint16_t size, uint32_t init) {
  uint32_t sum;

  sum = init;
  while (size > 1) {
    sum += *(data++);
    size -= 2;
  }
  if (size) {
    sum += *(uint8_t *)data;
  }
  sum = (sum & 0xffff) + (sum >> 16);
  sum = (sum & 0xffff) + (sum >> 16);
  return ~(uint16_t)sum;
}

// <endian.h> is not portable
#ifndef __BIG_ENDIAN
#define __BIG_ENDIAN 4321
#endif
#ifndef __LITTLE_ENDIAN
#define __LITTLE_ENDIAN 1234
#endif

static int endian = 0;

static int byteorder(void) {
  uint32_t x = 0x00000001;

  return *(uint8_t *)&x ? __LITTLE_ENDIAN : __BIG_ENDIAN;
}

static uint16_t byteswap16(uint16_t v) {
  return (v & 0x00ff) << 8 | (v & 0xff00) >> 8;
}

static uint32_t byteswap32(uint32_t v) {
  return (v & 0x000000ff) << 24 | (v & 0x0000ff00) << 8 |
         (v & 0x00ff0000) >> 8 | (v & 0xff000000) >> 24;
}

uint16_t hton16(uint16_t h) {
  if (!endian) {
    endian = byteorder();
  }
  return endian == __LITTLE_ENDIAN ? byteswap16(h) : h;
}

uint16_t ntoh16(uint16_t n) {
  if (!endian) {
    endian = byteorder();
  }
  return endian == __LITTLE_ENDIAN ? byteswap16(n) : n;
}

uint32_t hton32(uint32_t h) {
  if (!endian) {
    endian = byteorder();
  }
  return endian == __LITTLE_ENDIAN ? byteswap32(h) : h;
}

uint32_t ntoh32(uint32_t n) {
  if (!endian) {
    endian = byteorder();
  }
  return endian == __LITTLE_ENDIAN ? byteswap32(n) : n;
}

void maskset(uint32_t *mask, size_t size, size_t offset, size_t len) {
  size_t idx, so, sb, bl;

  so = offset / 32;
  sb = offset % 32;
  bl = (len > 32 - sb) ? 32 - sb : len;
  mask[so] |= (0xffffffff >> (32 - bl)) << sb;
  len -= bl;
  for (idx = so; idx < so + (len / 32); idx++) {
    mask[idx + 1] = 0xffffffff;
  }
  len -= (32 * (idx - so));
  if (len) {
    mask[idx + 1] |= (0xffffffff >> (32 - len));
  }
}

int maskchk(uint32_t *mask, size_t size, size_t offset, size_t len) {
  size_t idx, so, sb, bl;

  so = offset / 32;
  sb = offset % 32;
  bl = (len > 32 - sb) ? 32 - sb : len;
  if ((mask[offset / 32] & ((0xffffffff >> (32 - bl)) << sb)) ^
      ((0xffffffff >> (32 - bl)) << sb)) {
    return 0;
  }
  len -= bl;
  for (idx = so; idx < so + (len / 32); idx++) {
    if (mask[idx + 1] ^ 0xffffffff) {
      return 0;
    }
  }
  len -= (32 * (idx - so));
  if (len) {
    if ((mask[idx + 1] & (0xffffffff >> (32 - len))) ^
        (0xffffffff >> (32 - len))) {
      return 0;
    }
  }
  return 1;
}

void maskclr(uint32_t *mask, size_t size) {
  memset(mask, 0, sizeof(*mask) * size);
}

#define ISBIT(x) ((x) ? 1 : 0)

void maskdbg(uint32_t *mask, size_t size) {
  uint8_t *ptr;

  for (ptr = (uint8_t *)mask; ptr < (uint8_t *)(mask + size); ptr++) {
    fprintf(stderr, "%d%d%d%d %d%d%d%d\n", ISBIT(*ptr & 0x01),
            ISBIT(*ptr & 0x02), ISBIT(*ptr & 0x04), ISBIT(*ptr & 0x08),
            ISBIT(*ptr & 0x10), ISBIT(*ptr & 0x20), ISBIT(*ptr & 0x40),
            ISBIT(*ptr & 0x80));
  }
}
