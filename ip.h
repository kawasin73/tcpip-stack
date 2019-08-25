#ifndef IP_H
#define IP_H

#include <stddef.h>
#include <stdint.h>
#include "net.h"

#define IP_VERSION_IPV4 4

#define IP_ADDR_LEN 4
#define IP_ADDR_STR_LEN 16 /* "ddd.ddd.ddd.ddd\0" */

typedef uint32_t ip_addr_t;

struct ip_hdr {
  uint8_t vhl;
  uint8_t tos;
  uint16_t len;
  uint16_t id;
  uint16_t offset;
  uint8_t ttl;
  uint8_t protocol;
  uint16_t sum;
  ip_addr_t src;
  ip_addr_t dst;
  uint8_t options[0];
};

struct netif_ip {
  struct netif netif;
  ip_addr_t unicast;
  ip_addr_t netmask;
  ip_addr_t network;
  ip_addr_t broadcast;
  ip_addr_t gateway;
};

int ip_addr_pton(const char *p, ip_addr_t *n);
char *ip_addr_ntop(const ip_addr_t *n, char *p, size_t size);

int ip_init(void);

#endif
