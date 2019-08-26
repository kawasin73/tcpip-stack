#include "ip.h"
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "arp.h"
#include "net.h"
#include "util.h"

#define IP_FRAGMENT_TIMEOUT_SEC 30
#define IP_FRAGMENT_NUM_MAX 8

struct ip_route {
  uint8_t used;
  ip_addr_t network;
  ip_addr_t netmask;
  ip_addr_t nexthop;
  struct netif *netif;
};

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
static struct ip_fragment *fragments = NULL;
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
 * IP FRAGMENT
 */

static struct ip_fragment *ip_fragment_alloc(struct ip_hdr *hdr) {
  struct ip_fragment *new_fragment;

  new_fragment = malloc(sizeof(struct ip_fragment));
  if (!new_fragment) {
    return NULL;
  }
  new_fragment->next = fragments;
  new_fragment->src = hdr->src;
  new_fragment->dst = hdr->dst;
  new_fragment->id = hdr->id;
  new_fragment->protocol = hdr->protocol;
  new_fragment->len = 0;
  memset(new_fragment->data, 0, sizeof(new_fragment->data));
  maskclr(new_fragment->mask, sizeof(new_fragment->mask));
  fragments = new_fragment;
  return new_fragment;
}

static void ip_fragment_free(struct ip_fragment *fragment) { free(fragment); }

static struct ip_fragment *ip_fragment_detach(struct ip_fragment *fragment) {
  struct ip_fragment *entry, *prev = NULL;

  for (entry = fragments; entry; entry = entry->next) {
    if (entry == fragment) {
      if (prev) {
        prev->next = fragment->next;
      } else {
        fragments = fragment->next;
      }
      fragment->next = NULL;
      return fragment;
    }
    prev = entry;
  }
  return NULL;
}

static struct ip_fragment *ip_fragment_search(struct ip_hdr *hdr) {
  struct ip_fragment *entry;

  for (entry = fragments; entry; entry = entry->next) {
    if (entry->src == hdr->src && entry->dst == hdr->dst &&
        entry->id == hdr->id && entry->protocol == hdr->protocol) {
      return entry;
    }
  }
  return NULL;
}

static int ip_fragment_patrol(void) {
  time_t now;
  struct ip_fragment *entry, *prev = NULL;
  int count = 0;

  now = time(NULL);
  entry = fragments;
  while (entry) {
    if (now - entry->timestamp > IP_FRAGMENT_TIMEOUT_SEC) {
      if (prev) {
        entry = prev->next = entry->next;
      } else {
        entry = fragments = entry->next;
      }
      free(entry);
      count++;
      continue;
    }
    prev = entry;
    entry = entry->next;
  }
  return count;
}

static struct ip_fragment *ip_fragment_process(struct ip_hdr *hdr,
                                               uint8_t *payload, size_t plen) {
  struct ip_fragment *fragment;
  uint16_t off;
  static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
  static time_t timestamp = 0;
  static size_t count = 0;

  pthread_mutex_lock(&mutex);

  // check fragment timeout
  if (time(NULL) - timestamp > 10) {
    ip_fragment_patrol();
  }

  // find or create fragment object
  fragment = ip_fragment_search(hdr);
  if (!fragment) {
    if (count >= IP_FRAGMENT_NUM_MAX) {
      // too many fragments
      pthread_mutex_unlock(&mutex);
      return NULL;
    }
    // create new fragment object
    fragment = ip_fragment_alloc(hdr);
    if (!fragment) {
      // failed to allocate fragment object
      pthread_mutex_unlock(&mutex);
      return NULL;
    }
    count++;
  }

  // copy data to fragment object
  off = (ntoh16(hdr->offset) & 0x1fff) << 3;
  memcpy(fragment->data + off, payload, plen);
  maskset(fragment->mask, sizeof(fragment->mask), off, plen);
  if ((ntoh16(hdr->offset) & 0x2000) == 0) {
    fragment->len = off + plen;
  }
  fragment->timestamp = time(NULL);

  // check fragment is completed
  if (!fragment->len) {
    // don't know total fragment length yet
    pthread_mutex_unlock(&mutex);
    return NULL;
  }
  if (!maskchk(fragment->mask, sizeof(fragment->mask), 0, fragment->len)) {
    // imcomplete fragments
    pthread_mutex_unlock(&mutex);
    return NULL;
  }

  // detach fragment object
  ip_fragment_detach(fragment);
  count--;
  pthread_mutex_unlock(&mutex);
  return fragment;
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
    fragment = ip_fragment_process(hdr, payload, plen);
    if (!fragment) {
      return;
    }
    // completed fragment
    payload = fragment->data;
    plen = fragment->len;
  }
  for (protocol = protocols; protocol; protocol = protocol->next) {
    if (protocol->type == hdr->protocol) {
      protocol->handler(payload, plen, &hdr->src, &hdr->dst,
                        (struct netif *)iface);
      break;
    }
  }
  if (fragment) {
    ip_fragment_free(fragment);
  }
}

static int ip_tx_netdev(struct netif *netif, uint8_t *packet, size_t plen,
                        const ip_addr_t *dst) {
  ssize_t ret;
  uint8_t ha[128] = {};

  if (!(netif->dev->flags & NETDEV_FLAG_NOARP)) {
    if (dst) {
      ret = arp_resolve(netif, dst, (void *)ha, packet, plen);
      if (ret != ARP_RESOLVE_FOUND) {
        // ARP_RESOLVE_ERROR then error
        // ARP_RESOLVE_QUERY then wait and send packet in arp layer after arp
        // reply come
        return ret;
      }
    } else {
      memcpy(ha, netif->dev->broadcast, netif->dev->alen);
    }
  }
  if (netif->dev->ops->tx(netif->dev, ETHERNET_TYPE_IP, packet, plen,
                          (void *)ha) != (ssize_t)plen) {
    return -1;
  }
  return 1;
}

static int ip_tx_core(struct netif *netif, uint8_t protocol, const uint8_t *buf,
                      size_t len, const ip_addr_t *src, const ip_addr_t *dst,
                      const ip_addr_t *nexthop, uint16_t id, uint16_t offset) {
  uint8_t packet[4096];
  struct ip_hdr *hdr;
  uint16_t hlen;

  // set header
  hdr = (struct ip_hdr *)packet;
  hlen = sizeof(struct ip_hdr);
  hdr->vhl = (IP_VERSION_IPV4 << 4) | (hlen >> 2);
  hdr->tos = 0;
  hdr->len = hton16(hlen + len);
  hdr->id = hton16(id);
  hdr->offset = hton16(offset);
  hdr->ttl = 0xff;
  hdr->protocol = protocol;
  hdr->sum = 0;
  hdr->src = src ? *src : ((struct netif_ip *)netif)->unicast;
  hdr->dst = *dst;
  hdr->sum = cksum16((uint16_t *)hdr, hlen, 0);

  // copy payload
  memcpy(hdr + 1, buf, len);

#ifdef DEBUG
  fprintf(stderr, ">>> ip_tx_core <<<\n");
  ip_dump(netif, hdr, (uint8_t *)packet, hlen + len);
#endif

  return ip_tx_netdev(netif, (uint8_t *)packet, hlen + len, nexthop);
}

static uint16_t ip_generate_id(void) {
  static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
  static uint16_t id = 128;
  uint16_t ret;

  // TODO: multi netdev
  // TODO: rotate id
  pthread_mutex_lock(&mutex);
  ret = id++;
  pthread_mutex_unlock(&mutex);
  return ret;
}

ssize_t ip_tx(struct netif *netif, uint8_t protocol, const uint8_t *buf,
              size_t len, const ip_addr_t *dst) {
  struct ip_route *route;
  ip_addr_t *nexthop = NULL, *src = NULL;
  uint16_t id, flag, offset;
  size_t done, slen;

  // determine nexthop
  if (netif && *dst == IPADDR_BROADCAST) {
    nexthop = NULL;
  } else {
    // TODO: find route
    if (netif) {
      src = &((struct netif_ip *)netif)->unicast;
    }
    // TODO: use route to determin nexthop
    nexthop = dst;
  }
  id = ip_generate_id();

  // send ip packet (if fragmented then sometimes)
  for (done = 0; done < len; done += slen) {
    slen = MIN((len - done), (size_t)(netif->dev->mtu - IP_HDR_SIZE_MIN));
    flag = ((done + slen) < len) ? 0x2000 : 0x0000;
    offset = flag | ((done >> 3) & 0x1fff);
    if (ip_tx_core(netif, protocol, buf + done, slen, src, dst, nexthop, id,
                   offset) == -1) {
      return -1;
    }
  }
  return len;
}

int ip_add_protocol(uint8_t protocol,
                    void (*handler)(uint8_t *, size_t, ip_addr_t *, ip_addr_t *,
                                    struct netif *)) {
  struct ip_protocol *p;

  // check protocol is already registered
  for (p = protocols; p; p = p->next) {
    if (p->type == protocol) {
      return -1;
    }
  }

  // register new protocol
  p = malloc(sizeof(struct ip_protocol));
  p->next = protocols;
  p->type = protocol;
  p->handler = handler;
  protocols = p;
  return 0;
}

int ip_init(void) { return netdev_proto_register(NETDEV_PROTO_IP, ip_rx); }
