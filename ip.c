#include "ip.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "net.h"
#include "util.h"

struct ip_fragment {
  struct ip_fragment *next;
  ip_addr_t src;
  ip_addr_t dst;
  uint16_t id;
  uint16_t protocol;
  uint16_t len;
  uint8_t data[65535];
  uint32_t mask[2048];
  time_t timestamp;
};

struct ip_protocol {
  struct ip_protocol *next;
  uint8_t type;
  void (*handler)(uint8_t *payload, size_t len, ip_addr_t *src, ip_addr_t *dst,
                  struct netif *netif);
};

static struct ip_protocol *protocols = NULL;
static int ip_forwarding = 0;

const ip_addr_t IP_ADDR_ANY = 0x00000000;
const ip_addr_t IPADDR_BROADCAST = 0xffffffff;

// convert string ip address (p) to ip_addr_t (n)
int ip_addr_pton(const char *p, ip_addr_t *n) {
  char *sp, *ep;
  int idx;
  long ret;

  sp = (char *)p;
  for (idx = 0; idx < 4; idx++) {
    ret = strtol(sp, &ep, 10);
    if (ret < 0 || ret > 255) {
      return -1;
    } else if (ep == sp) {
      // not change
      return -1;
    } else if ((idx == 3 && *ep != '\0') || (idx != 3 && *ep != '.')) {
      // separater or end of string
      return -1;
    }
    ((uint8_t *)n)[idx] = ret;
    sp = ep + 1;
  }
  return 0;
}

char *ip_addr_ntop(const ip_addr_t *n, char *p, size_t size) {
  uint8_t *ptr;

  ptr = (uint8_t *)n;
  snprintf(p, size, "%d.%d.%d.%d", ptr[0], ptr[1], ptr[2], ptr[3]);
  return p;
}

void ip_dump(struct netif *netif, struct ip_hdr *hdr, uint8_t *packet,
             size_t plen) {
  struct netif_ip *iface;
  char addr[IP_ADDR_STR_LEN];

  iface = (struct netif_ip *)netif;
  fprintf(stderr, "      dev: %s (%s)\n", netif->dev->name,
          ip_addr_ntop(&iface->unicast, addr, sizeof(addr)));
  fprintf(stderr, "      src: %s\n",
          ip_addr_ntop(&hdr->src, addr, sizeof(addr)));
  fprintf(stderr, "      dst: %s\n",
          ip_addr_ntop(&hdr->dst, addr, sizeof(addr)));
  fprintf(stderr, " protocol: 0x%02x\n", hdr->protocol);
  fprintf(stderr, "      len: %zu octets\n", plen);
  hexdump(stderr, packet, plen);
}

/*
 * IP CORE
 */

static void ip_rx(uint8_t *dgram, size_t dlen, struct netdev *dev) {
  struct ip_hdr *hdr;
  uint16_t hlen, offset;
  struct netif_ip *iface;
  uint8_t *payload;
  size_t plen;
  struct ip_fragment *fragment = NULL;
  struct ip_protocol *protocol;

  // get ip header
  if (dlen < sizeof(struct ip_hdr)) {
    fprintf(stderr, "too short dgram for ip header\n");
    return;
  }
  hdr = (struct ip_hdr *)dgram;
  if ((hdr->vhl) >> 4 != IP_VERSION_IPV4) {
    fprintf(stderr, "not ipv4 packet.\n");
    return;
  }

  // validate ip header
  hlen = (hdr->vhl & 0x0f) << 2;
  if (dlen < hlen || dlen < ntoh16(hdr->len)) {
    fprintf(stderr, "ip packet length error.\n");
    return;
  }
  if (cksum16((uint16_t *)hdr, hlen, 0) != 0) {
    fprintf(stderr, "ip packet checksum error.\n");
    return;
  }
  if (!hdr->ttl) {
    fprintf(stderr, "ip packet was dead (TTL=0).\n");
    return;
  }

  iface = (struct netif_ip *)netdev_get_netif(dev, NETIF_FAMILY_IPV4);
  if (!iface) {
    fprintf(stderr, "ip unknown interface.\n");
    return;
  }
  if (hdr->dst != iface->unicast) {
    if (hdr->dst != iface->broadcast && hdr->dst != IPADDR_BROADCAST) {
      // for other host
      if (ip_forwarding) {
        // TOOD: ip_forward_process
      }
      return;
    }
  }

#ifdef DEBUG
  fprintf(stderr, ">>> ip_rx <<<\n");
  ip_dump((struct netif *)iface, hdr, dgram, dlen);
#endif

  payload = (uint8_t *)hdr + hlen;
  plen = ntoh16(hdr->len) - hlen;
  offset = ntoh16(hdr->offset);
  if (offset & 0x2000 || offset & 0x1fff) {
    // TODO: fragment
    fprintf(stderr, "TODO: fragmented.\n");
    return;
  }
  for (protocol = protocols; protocol; protocol = protocol->next) {
    if (protocol->type == hdr->protocol) {
      protocol->handler(payload, plen, &hdr->src, &hdr->dst,
                        (struct netif *)iface);
      break;
    }
  }
  if (fragment) {
    // TODO: free fragment
  }
}

int ip_init(void) { return netdev_proto_register(NETDEV_PROTO_IP, ip_rx); }
