#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include "ip.h"
#include "util.h"

#define TCP_CB_TABLE_SIZE 128
#define TCP_SOURCE_PORT_MIN 49152
#define TCP_SOURCE_PORT_MAX 65535

#define TCP_CB_STATE_CLOSED 0
#define TCP_CB_STATE_LISTEN 1
#define TCP_CB_STATE_SYN_SENT 2
#define TCP_CB_STATE_SYN_RCVD 3
#define TCP_CB_STATE_ESTABLISHED 4
#define TCP_CB_STATE_FIN_WAIT1 5
#define TCP_CB_STATE_FIN_WAIT2 6
#define TCP_CB_STATE_CLOSING 7
#define TCP_CB_STATE_TIME_WAIT 8
#define TCP_CB_STATE_CLOSE_WAIT 9
#define TCP_CB_STATE_LAST_ACK 10

#define TCP_FLG_FIN 0x01
#define TCP_FLG_SYN 0x02
#define TCP_FLG_RST 0x04
#define TCP_FLG_PSH 0x08
#define TCP_FLG_ACK 0x10
#define TCP_FLG_URG 0x20

#define TCP_FLG_IS(x, y) (((x)&0x3f) == (y))
#define TCP_FLG_ISSET(x, y) (((x)&0x3f) & (y))

struct tcp_hdr {
  uint16_t src;
  uint16_t dst;
  uint32_t seq;
  uint32_t ack;
  uint8_t off;
  uint8_t flg;
  uint16_t win;
  uint16_t sum;
  uint16_t urg;
};

struct tcp_cb {
  uint8_t used;
  uint8_t state;
  struct netif *iface;
  uint16_t port; // network byte order
  struct {
    ip_addr_t addr;
    uint16_t port; // network byte order
  } peer;
  struct {
    uint32_t nxt;
    uint32_t una;
    uint16_t up;
    uint32_t wl1;
    uint32_t wl2;
    uint16_t wnd;
  } snd;
  uint32_t iss;
  struct {
    uint32_t nxt;
    uint16_t up;
    uint16_t wnd;
  } rcv;
  uint32_t irs;
  uint8_t window[65535];
  struct tcp_cb *parent;
  pthread_cond_t cond;
};

#define TCP_SOCKET_INVALID(x) ((x) < 0 || (x) >= TCP_CB_TABLE_SIZE)

static struct tcp_cb cb_table[TCP_CB_TABLE_SIZE];
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_t timer_thread;

static ssize_t tcp_tx(struct tcp_cb *cb, uint32_t seq, uint32_t ack,
                      uint8_t flg, uint8_t *buf, size_t len);

static char *tcp_flg_ntop(uint8_t flg, char *buf, int len) {
  int i = 0;
  if (TCP_FLG_ISSET(flg, TCP_FLG_FIN)) {
    buf[i++] = 'F';
  }
  if (TCP_FLG_ISSET(flg, TCP_FLG_SYN)) {
    buf[i++] = 'S';
  }
  if (TCP_FLG_ISSET(flg, TCP_FLG_RST)) {
    buf[i++] = 'R';
  }
  if (TCP_FLG_ISSET(flg, TCP_FLG_PSH)) {
    buf[i++] = 'P';
  }
  if (TCP_FLG_ISSET(flg, TCP_FLG_ACK)) {
    buf[i++] = 'A';
  }
  if (TCP_FLG_ISSET(flg, TCP_FLG_URG)) {
    buf[i++] = 'U';
  }
  buf[i] = 0;
  return buf;
}

static char *tcp_state_ntop(uint8_t state) {
  switch (state) {
    case TCP_CB_STATE_CLOSED:
      return "CLOSED";
    case TCP_CB_STATE_LISTEN:
      return "LISTEN";
    case TCP_CB_STATE_SYN_SENT:
      return "SYN_SENT";
    case TCP_CB_STATE_SYN_RCVD:
      return "SYN_RCVD";
    case TCP_CB_STATE_ESTABLISHED:
      return "ESTABLISHED";
    case TCP_CB_STATE_FIN_WAIT1:
      return "FIN_WAIT1";
    case TCP_CB_STATE_FIN_WAIT2:
      return "FIN_WAIT2";
    case TCP_CB_STATE_CLOSING:
      return "CLOSING";
    case TCP_CB_STATE_TIME_WAIT:
      return "TIME_WAIT";
    case TCP_CB_STATE_CLOSE_WAIT:
      return "CLOSE_WAIT";
    case TCP_CB_STATE_LAST_ACK:
      return "LAST_ACK";

    default:
      return "UNKNOWN";
  }
}

static void tcp_dump(struct tcp_cb *cb, struct tcp_hdr *hdr) {
  char buf[64];

  fprintf(stderr, " state: %s\n", tcp_state_ntop(cb->state));
  fprintf(stderr, " src: %u\n", ntoh16(hdr->src));
  fprintf(stderr, " dst: %u\n", ntoh16(hdr->dst));
  fprintf(stderr, " seq: %u\n", ntoh32(hdr->seq));
  fprintf(stderr, " ack: %u\n", ntoh32(hdr->ack));
  fprintf(stderr, " off: %u\n", hdr->off);
  fprintf(stderr, " flg: [%s]\n", tcp_flg_ntop(hdr->flg, &buf, 64));
  fprintf(stderr, " win: %u\n", ntoh16(hdr->win));
  fprintf(stderr, " sum: %u\n", ntoh16(hdr->sum));
  fprintf(stderr, " urg: %u\n", ntoh16(hdr->urg));
}

/*
 * EVENT PROCESSING
 * https://tools.ietf.org/html/rfc793#section-3.9
 */

// SEGMENT ARRIVES
// https://tools.ietf.org/html/rfc793#page-65
static void tcp_event_segment_arrives(struct tcp_cb *cb, struct tcp_hdr *hdr,
                                      size_t len) {
  uint32_t seq, ack;
  size_t hlen, plen;
  int acceptable = 0;

  hlen = (hdr->off >> 4) << 2;
  plen = len - hlen;
  switch (cb->state) {
    case TCP_CB_STATE_CLOSED:
      if (!TCP_FLG_ISSET(hdr->flg, TCP_FLG_RST)) {
        if (TCP_FLG_ISSET(hdr->flg, TCP_FLG_ACK)) {
          tcp_tx(cb,ntoh32( hdr->ack), 0, TCP_FLG_RST, NULL, 0);
        } else {
          tcp_tx(cb, 0, ntoh32(hdr->seq) + plen, TCP_FLG_RST | TCP_FLG_ACK, NULL, 0);
        }
      }
      break;

    case TCP_CB_STATE_SYN_SENT:
      // first check the ACK bit
      if (TCP_FLG_ISSET(hdr->flg, TCP_FLG_ACK)) {
        if (ntoh32(hdr->ack) <= cb->iss || ntoh32(hdr->ack) > cb->snd.nxt) {
          tcp_tx(cb, ntoh32(hdr->ack), 0, TCP_FLG_RST, NULL, 0);
          return;
        } else if (cb->snd.una <= ntoh32(hdr->ack) && ntoh32(hdr->ack) <= cb->snd.nxt) {
          acceptable = 1;
          // TODO: ? if not acceptable ?
        }
      }

      // second check the RST bit
      if (TCP_FLG_ISSET(hdr->flg, TCP_FLG_RST)) {
        if (!acceptable) {
          // drop segment
          return;
        }
        sprintf(stderr, "error: connection reset\n");
        cb->state = TCP_CB_STATE_CLOSED;
        // TODO: delete cb
        cb->used = 0;
        return;
      }

      // TODO: third check the security and precedence

      // fourth check the SYN bit
      if (TCP_FLG_ISSET(hdr->flg, TCP_FLG_SYN)) {
        cb->rcv.nxt = ntoh32(hdr->seq) + 1;
        cb->irs = ntoh32(hdr->seq);
        // TODO: ? if there is an ACK ?
        cb->snd.una = ntoh32(hdr->ack);
        // TODO: clear all retransmission queue

        if (cb->snd.una > cb->iss) {
          // our SYN has been ACKed
          cb->state = TCP_CB_STATE_ESTABLISHED;
          tcp_tx(cb, cb->snd.nxt, cb->rcv.nxt, TCP_FLG_ACK, NULL, 0);
          if (!TCP_FLG_ISSET(hdr->flg, TCP_FLG_URG)) {
            return;
          }
          goto CHECK_URG;
        } else {
          cb->state = TCP_CB_STATE_SYN_RCVD;
          tcp_tx(cb, cb->iss, cb->rcv.nxt, TCP_FLG_SYN | TCP_FLG_ACK, NULL, 0);
          // TODO: If there are other controls or text in the segment, queue
          // them for processing after the ESTABLISHED state has been reached,
          return;
        }
      }

      // fifth, if neither of the SYN or RST bits is set
      if (!TCP_FLG_ISSET(hdr->flg, TCP_FLG_ACK | TCP_FLG_RST)) {
        // drop segment
        return;
      }

      return;

    default:
      fprintf(stderr, ">>> not implement tcp state (%d) <<<\n", cb->state);

    CHECK_URG:
      // TODO: check the URG bit
      return;
  }
}

/*
 * TCP APPLICATION CONTROLLER
 */

static ssize_t tcp_tx(struct tcp_cb *cb, uint32_t seq, uint32_t ack,
                      uint8_t flg, uint8_t *buf, size_t len) {
  uint8_t segment[1500];
  struct tcp_hdr *hdr;
  ip_addr_t self, peer;
  uint32_t pseudo = 0;

  memset(&segment, 0, sizeof(segment));
  hdr = (struct tcp_hdr *)segment;
  hdr->src = cb->port;
  hdr->dst = cb->peer.port;
  hdr->seq = hton32(seq);
  hdr->ack = hton32(ack);
  hdr->off = (sizeof(struct tcp_hdr) >> 2) << 4;
  hdr->flg = flg;
  hdr->win = hton16(cb->rcv.wnd);
  hdr->sum = 0;
  hdr->urg = 0;
  // TODO: check buffer len is smaller than segment size
  memcpy(hdr + 1, buf, len);
  self = ((struct netif_ip *)cb->iface)->unicast;
  peer = cb->peer.addr;
  pseudo += (self >> 16) & 0xffff;
  pseudo += self & 0xffff;
  pseudo += (peer >> 16) & 0xffff;
  pseudo += self & 0xffff;
  pseudo += hton16((uint16_t)IP_PROTOCOL_TCP);
  pseudo += hton16(sizeof(struct tcp_hdr) + len);
  hdr->sum = cksum16((uint16_t *)hdr, sizeof(struct tcp_hdr) + len, pseudo);

#ifdef DEBUG
  fprintf(stderr, ">>> tcp_tx <<<\n");
  tcp_dump(cb, hdr);
#endif

  if (ip_tx(cb->iface, IP_PROTOCOL_TCP, (uint8_t *)hdr,
            sizeof(struct tcp_hdr) + len, &peer) == -1) {
    // failed to send ip packet
    return -1;
  }

  // TODO: add txq
  return len;
}

static void tcp_rx(uint8_t *segment, size_t len, ip_addr_t *src, ip_addr_t *dst,
                   struct netif *iface) {
  struct tcp_hdr *hdr;
  uint32_t pseudo = 0;
  int i;
  struct tcp_cb *cb, *fcb = NULL, *lcb = NULL;

  // validate tcp packet
  if (*dst != ((struct netif_ip *)iface)->unicast) {
    return;
  }
  if (len < sizeof(struct tcp_hdr)) {
    return;
  }

  // validate checksum
  hdr = (struct tcp_hdr *)segment;
  pseudo += *src >> 16;
  pseudo += *src & 0xffff;
  pseudo += *dst >> 16;
  pseudo += *dst & 0xffff;
  pseudo += hton16((uint16_t)IP_PROTOCOL_TCP);
  pseudo += hton16(len);
  if (cksum16((uint16_t *)hdr, len, pseudo) != 0) {
    fprintf(stderr, "tcp checksum error\n");
    return;
  }

  pthread_mutex_lock(&mutex);

  // find connection cb or listener cb
  for (i = 0; i < TCP_CB_TABLE_SIZE; i++) {
    cb = &cb_table[i];
    if (!cb->used && cb->state == TCP_CB_STATE_CLOSED) {
      // cache cb for the case SYN message and no cb is connected to this peer
      fcb = cb;
    } else if ((!cb->iface || cb->iface == iface) && cb->port == hdr->dst) {
      // if ip address and dst port number matches
      if (cb->peer.addr == *src && cb->peer.port == hdr->src) {
        // this cb is connection for this tcp packet
        break;
      } else if (cb->state == TCP_CB_STATE_LISTEN && !lcb) {
        // listener socket is found
        lcb = cb;
      }
    }
  }

  // cb that matches this tcp packet is not found.
  // create socket if listener socket exists and packet is SYN packet.
  if (i == TCP_CB_TABLE_SIZE) {
    if (!fcb) {
      // cb resource is run out
      // TODO: send RST
      pthread_mutex_unlock(&mutex);
      return;
    }
    // create accept socket
    cb = fcb;
    cb->iface = iface;
    if (lcb) {
      // TODO: ? if SYN is not set ?
      cb->used = 1;
      cb->state = lcb->state;
      cb->port = lcb->port;
      cb->rcv.wnd = sizeof(cb->window);
      cb->parent = lcb;
    } else {
      // this port is not listened.
      // this packet is invalid. no connection is found.
      cb->used = 0;
      cb->port = 0;
      cb->rcv.wnd = 0;
    }
    cb->peer.addr = *src;
    cb->peer.port = hdr->src;
  }
  // else cb that matches this tcp packet is found.

#ifdef DEBUG
  fprintf(stderr, ">>> tcp_rx <<<\n");
  tcp_dump(cb, hdr);
#endif

  // handle message
  tcp_event_segment_arrives(cb, hdr, len);
  pthread_mutex_unlock(&mutex);
  return;
}

/*
 * TCP APPLICATION INTERFACE
 */

int tcp_api_open(void) {
  struct tcp_cb *cb;
  int i;

  pthread_mutex_lock(&mutex);
  for (i = 0; i < TCP_CB_TABLE_SIZE; i++) {
    cb = &cb_table[i];
    if (!cb->used) {
      cb->used = 1;
      cb->state = TCP_CB_STATE_CLOSED;
      pthread_mutex_unlock(&mutex);
      return i;
    }
  }
  pthread_mutex_unlock(&mutex);
  fprintf(stderr, "error:  insufficient resources\n");
  return -1;
}

int tcp_api_close(int soc) {
  // TODO: close
}

int tcp_api_connect(int soc, ip_addr_t *addr, uint16_t port) {
  struct tcp_cb *cb, *tmp;
  int i, j;
  int offset;

  // validate soc id
  if (TCP_SOCKET_INVALID(soc)) {
    return -1;
  }

  pthread_mutex_lock(&mutex);
  cb = &cb_table[soc];

  // check cb state
  if (!cb->used || cb->state != TCP_CB_STATE_CLOSED) {
    pthread_mutex_unlock(&mutex);
    return -1;
  }

  // if port number is not specified then generate nice port
  if (!cb->port) {
    offset = time(NULL) % 1024;
    // find port number which is not used between TCP_SOURCE_PORT_MIN and
    // TCP_SOURCE_PORT_MAX
    for (i = TCP_SOURCE_PORT_MIN + offset; i <= TCP_SOURCE_PORT_MAX; i++) {
      for (j = 0; j < TCP_CB_TABLE_SIZE; j++) {
        tmp = &cb_table[j];
        if (tmp->used && tmp->port == hton16((uint16_t)i)) {
          break;
        }
      }
      if (j == TCP_CB_TABLE_SIZE) {
        // port number (i) is not used
        cb->port = hton16((uint16_t)i);
        break;
      }
    }
    if (!cb->port) {
      // could not find unused port number
      pthread_mutex_unlock(&mutex);
      return -1;
    }
  }

  // initalize cb
  cb->peer.addr = *addr;
  cb->peer.port = hton16(port);
  cb->iface = ip_netif_by_peer(&cb->peer.addr);
  if (!cb->iface) {
    pthread_mutex_unlock(&mutex);
    return -1;
  }
  cb->rcv.wnd = sizeof(cb->window);
  cb->iss = (uint32_t)random();
  if (tcp_tx(cb, cb->iss, 0, TCP_FLG_SYN, NULL, 0) == -1) {
    pthread_mutex_unlock(&mutex);
    return -1;
  }
  cb->snd.una = cb->iss;
  cb->snd.nxt = cb->iss + 1;
  cb->state = TCP_CB_STATE_SYN_SENT;

  // wait until state change
  while (cb->state == TCP_CB_STATE_SYN_SENT) {
    pthread_cond_wait(&cb_table[soc].cond, &mutex);
  }

  pthread_mutex_unlock(&mutex);
  return 0;
}

int tcp_api_bind(int soc, uint16_t port);
int tcp_api_listen(int soc);
int tcp_api_accept(int soc);
ssize_t tcp_api_recv(int soc, uint8_t *buf, size_t size);
ssize_t tcp_api_send(int soc, uint8_t *buf, size_t len);

int tcp_init(void) {
  int i;

  // initialize mutex and condition variables
  for (i = 0; i < TCP_CB_TABLE_SIZE; i++) {
    pthread_cond_init(&cb_table[i].cond, NULL);
  }
  pthread_mutex_init(&mutex, NULL);

  if (ip_add_protocol(IP_PROTOCOL_TCP, tcp_rx) == -1) {
    return -1;
  }
  // if (pthread_create(&timer_thread, NULL, tcp_timer_thread, NULL) == -1) {
  //   return -1;
  // }
  return 0;
}
