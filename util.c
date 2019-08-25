#include "util.h"
#include <ctype.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

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
