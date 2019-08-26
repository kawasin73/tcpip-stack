#ifndef _ARP_H_
#define _ARP_H_

#include <stdint.h>
#include "ip.h"
#include "net.h"

#define ARP_RESOLVE_ERROR -1
#define ARP_RESOLVE_QUERY 0
#define ARP_RESOLVE_FOUND 1

int arp_init(void);
int arp_resolve(struct netif *netif, const ip_addr_t *pa, uint8_t *ha,
                const void *data, size_t len);

#endif
