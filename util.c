#include "util.h"
#include <ctype.h>
#include <stddef.h>
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
