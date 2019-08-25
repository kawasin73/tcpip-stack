#ifndef _UTIL_H_
#define _UTIL_H_

#include <stdio.h>
#include <stdint.h>

void hexdump(FILE *fp, void *data, size_t size);

uint16_t cksum16(uint16_t *data, uint16_t size, uint32_t init);
uint16_t hton16(uint16_t);
uint16_t ntoh16(uint16_t);

#endif
