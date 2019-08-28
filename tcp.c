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

#define IS_FREE_CB(cb) (!(cb)->used && (cb)->state == TCP_CB_STATE_CLOSED)

#ifndef TCP_DEBUG
#ifdef DEBUG
#define TCP_DEBUG 1
#endif
#endif

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
  uint16_t port;  // network byte order
  struct {
    ip_addr_t addr;
    uint16_t port;  // network byte order
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
  struct queue_head backlog;
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

static void tcp_state_dump(struct tcp_cb *cb) {
  char buf[64];

  fprintf(stderr, "      used: %d\n", cb->used);
  fprintf(stderr, "     state: %s\n", tcp_state_ntop(cb->state));
  fprintf(stderr, " self.port: %u\n", ntoh16(cb->port));
  fprintf(stderr, " peer.addr: %s\n", ip_addr_ntop(&cb->peer.addr, buf, 64));
  fprintf(stderr, " peer.port: %u\n", ntoh16(cb->peer.port));
  fprintf(stderr, "   snd.nxt: %u\n", cb->snd.nxt);
  fprintf(stderr, "   snd.wnd: %u\n", cb->snd.wnd);
  fprintf(stderr, "   rcv.nxt: %u\n", cb->rcv.nxt);
  fprintf(stderr, "   rcv.wnd: %u\n", cb->rcv.wnd);
  fprintf(stderr, " n_backlog: %u\n", cb->backlog.num);
}

static void tcp_dump(struct tcp_cb *cb, struct tcp_hdr *hdr, size_t plen) {
  char buf[64];

  tcp_state_dump(cb);
  fprintf(stderr, " len: %u\n", plen);
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
          tcp_tx(cb, ntoh32(hdr->ack), 0, TCP_FLG_RST, NULL, 0);
        } else {
          tcp_tx(cb, 0, ntoh32(hdr->seq) + plen, TCP_FLG_RST | TCP_FLG_ACK,
                 NULL, 0);
        }
      }
      return;

    case TCP_CB_STATE_LISTEN:
      // first check for an RST
      if (TCP_FLG_ISSET(hdr->flg, TCP_FLG_RST)) {
        // incoming RST is ignored
        goto ERROR_RX_LISTEN;
      }

      // second check for an ACK
      if (TCP_FLG_ISSET(hdr->flg, TCP_FLG_ACK)) {
        tcp_tx(cb, hdr->ack, 0, TCP_FLG_RST, NULL, 0);
        goto ERROR_RX_LISTEN;
      }

      // third check for a SYN
      if (TCP_FLG_ISSET(hdr->flg, TCP_FLG_SYN)) {
        // TODO: check the security

        // TODO: If the SEG.PRC is greater than the TCB.PRC

        // else
        cb->rcv.wnd = sizeof(cb->window);
        cb->rcv.nxt = ntoh32(hdr->seq) + 1;
        cb->irs = ntoh32(hdr->seq);
        cb->iss = (uint32_t)random();
        tcp_tx(cb, cb->iss, cb->rcv.nxt, TCP_FLG_SYN | TCP_FLG_ACK, NULL, 0);
        cb->snd.nxt = cb->iss + 1;
        cb->snd.una = cb->iss;
        cb->state = TCP_CB_STATE_SYN_RCVD;

        // TODO: ?  queue to backlog ?
        break;
      }

      // no packet should come here. drop segment
    ERROR_RX_LISTEN:
      // return state to CLOSED
      cb->state = TCP_CB_STATE_CLOSED;
      cb->parent = NULL;
      return;

    case TCP_CB_STATE_SYN_SENT:
      // first check the ACK bit
      if (TCP_FLG_ISSET(hdr->flg, TCP_FLG_ACK)) {
        if (ntoh32(hdr->ack) <= cb->iss || ntoh32(hdr->ack) > cb->snd.nxt) {
          tcp_tx(cb, ntoh32(hdr->ack), 0, TCP_FLG_RST, NULL, 0);
          return;
        }
        if (cb->snd.una <= ntoh32(hdr->ack) &&
            ntoh32(hdr->ack) <= cb->snd.nxt) {
          acceptable = 1;
        } else {
          // drop invalid ack
          return;
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
        pthread_cond_signal(&cb->cond);
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
          pthread_cond_signal(&cb->cond);
          if (plen > 0 || TCP_FLG_ISSET(hdr->flg, TCP_FLG_URG)) {
            goto CHECK_URG;
          }
          return;
        } else {
          cb->state = TCP_CB_STATE_SYN_RCVD;
          tcp_tx(cb, cb->iss, cb->rcv.nxt, TCP_FLG_SYN | TCP_FLG_ACK, NULL, 0);
          pthread_cond_signal(&cb->cond);
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

    case TCP_CB_STATE_SYN_RCVD:
    case TCP_CB_STATE_ESTABLISHED:
    case TCP_CB_STATE_FIN_WAIT1:
    case TCP_CB_STATE_FIN_WAIT2:
    case TCP_CB_STATE_CLOSING:
    case TCP_CB_STATE_TIME_WAIT:
    case TCP_CB_STATE_CLOSE_WAIT:
    case TCP_CB_STATE_LAST_ACK:
      break;

    default:
      fprintf(stderr, ">>> not implement tcp state (%d) <<<\n", cb->state);
      return;
  }

  // first check sequence number
  if (plen > 0) {
    if (cb->rcv.wnd > 0) {
      acceptable = (cb->rcv.nxt <= ntoh32(hdr->seq) &&
                    ntoh32(hdr->seq) < cb->rcv.nxt + cb->rcv.wnd) ||
                   (cb->rcv.nxt <= ntoh32(hdr->seq) &&
                    ntoh32(hdr->seq) + plen - 1 < cb->rcv.nxt + cb->rcv.wnd);
    } else {
      acceptable = 0;
    }
  } else {
    if (cb->rcv.wnd > 0) {
      acceptable = (cb->rcv.nxt <= ntoh32(hdr->seq) &&
                    ntoh32(hdr->seq) < cb->rcv.nxt + cb->rcv.wnd);
    } else {
      acceptable = ntoh32(hdr->seq) == cb->rcv.nxt;
    }
  }

  if (!acceptable) {
    if (!TCP_FLG_ISSET(hdr->flg, TCP_FLG_RST)) {
      tcp_tx(cb, cb->snd.nxt, cb->rcv.nxt, TCP_FLG_ACK, NULL, 0);
    }
    // drop segment
    return;
  }

  // second check the RST bit
  switch (cb->state) {
    case TCP_CB_STATE_SYN_RCVD:
      if (TCP_FLG_ISSET(hdr->flg, TCP_FLG_RST)) {
        // close connection
        cb->state = TCP_CB_STATE_CLOSED;
        pthread_cond_signal(&cb->cond);
        return;
      }
      break;

    case TCP_CB_STATE_ESTABLISHED:
    case TCP_CB_STATE_FIN_WAIT1:
    case TCP_CB_STATE_FIN_WAIT2:
    case TCP_CB_STATE_CLOSE_WAIT:
      if (TCP_FLG_ISSET(hdr->flg, TCP_FLG_RST)) {
        // TODO: signal to SEND, RECEIVE waiting thread.
        // TODO: ? use reference count for used ?
        cb->state = TCP_CB_STATE_CLOSED;
        return;
      }
      break;
    case TCP_CB_STATE_CLOSING:
    case TCP_CB_STATE_TIME_WAIT:
    case TCP_CB_STATE_LAST_ACK:
      if (TCP_FLG_ISSET(hdr->flg, TCP_FLG_RST)) {
        // close connection
        cb->state = TCP_CB_STATE_CLOSED;
        return;
      }
      break;
  }

  // TODO: third check security and precedence

  // fourth, check the SYN bit
  if (TCP_FLG_ISSET(hdr->flg, TCP_FLG_SYN)) {
    // TODO: signal
    tcp_tx(cb, 0, cb->rcv.nxt, TCP_FLG_RST, NULL, 0);
    cb->state = TCP_CB_STATE_CLOSED;
    return;
  }
  // TODO: ? If the SYN is not in the window this step would not be reached and
  // an ack would have been sent in the first step (sequence number check). ?

  // fifth check the ACK field
  if (TCP_FLG_ISSET(hdr->flg, TCP_FLG_ACK)) {
    switch (cb->state) {
      case TCP_CB_STATE_SYN_RCVD:
        if (cb->snd.una <= ntoh32(hdr->ack) &&
            ntoh32(hdr->ack) <= cb->snd.nxt) {
          cb->state = TCP_CB_STATE_ESTABLISHED;
          if (cb->parent) {
            // add cb to backlog
            queue_push(&cb->parent->backlog, cb, sizeof(*cb));
            pthread_cond_signal(&cb->parent->cond);
          } else {
            // parent == NULL means cb is created by user and first state was
            // SYN_SENT
            pthread_cond_signal(&cb->cond);
          }
        } else {
          tcp_tx(cb, hdr->ack, cb->rcv.nxt, TCP_FLG_RST, NULL, 0);
        }
        break;

      case TCP_CB_STATE_ESTABLISHED:
      case TCP_CB_STATE_FIN_WAIT1:
      case TCP_CB_STATE_FIN_WAIT2:
      case TCP_CB_STATE_CLOSE_WAIT:
        if (cb->snd.una <= ntoh32(hdr->ack) &&
            ntoh32(hdr->ack) <= cb->snd.nxt) {
          cb->snd.una = hdr->ack;
          // TODO: retransmission queue send

          if ((cb->snd.wl1 < ntoh32(hdr->seq)) ||
              (cb->snd.wl1 == ntoh32(hdr->seq) &&
               cb->snd.wl2 <= ntoh32(hdr->ack))) {
            cb->snd.wnd = ntoh16(hdr->win);
            cb->snd.wl1 = ntoh32(hdr->seq);
            cb->snd.wl2 = ntoh32(hdr->ack);
          }
        } else if (ntoh32(hdr->ack) > cb->snd.nxt) {
          tcp_tx(cb, cb->snd.nxt, cb->rcv.nxt, TCP_FLG_ACK, NULL, 0);
          // drop the segment
          return;
        }
        // else if SEG.ACK < SND.UNA then it can be ignored

        if (cb->state == TCP_CB_STATE_FIN_WAIT1) {
          // if this ACK is for sent FIN
          if (ntoh32(hdr->ack) + 1 == cb->snd.nxt) {
            cb->state = TCP_CB_STATE_FIN_WAIT2;
          }
        } else if (cb->state == TCP_CB_STATE_FIN_WAIT2) {
          // TODO: if the retransmission queue is empty, the user's CLOSE can be
          // acknowledged ("ok")
        } else if (cb->state == TCP_CB_STATE_CLOSING) {
          // if this ACK is for sent FIN
          if (ntoh32(hdr->ack) + 1 == cb->snd.nxt) {
            cb->state = TCP_CB_STATE_TIME_WAIT;
          }
        }

        break;

      case TCP_CB_STATE_LAST_ACK:
        // if this ACK is for sent FIN
        if (ntoh32(hdr->ack) == cb->snd.nxt) {
          cb->state = TCP_CB_STATE_CLOSED;
          return;
        }
        break;

      case TCP_CB_STATE_TIME_WAIT:
        // TODO: restart the 2 MSL timeout
        break;
    }
  }

CHECK_URG:
  // sixth, check the URG bit
  if (TCP_FLG_ISSET(hdr->flg, TCP_FLG_URG)) {
    switch (cb->state) {
      case TCP_CB_STATE_ESTABLISHED:
      case TCP_CB_STATE_FIN_WAIT1:
      case TCP_CB_STATE_FIN_WAIT2:
        cb->rcv.up = MAX(cb->rcv.up, hdr->urg);
        // TODO: signal

        break;

      case TCP_CB_STATE_CLOSING:
      case TCP_CB_STATE_TIME_WAIT:
      case TCP_CB_STATE_CLOSE_WAIT:
      case TCP_CB_STATE_LAST_ACK:
        // this should not occur. ignore the urg
        break;

      case TCP_CB_STATE_SYN_RCVD:
        // do nothing
        break;
    }
  }

  // seventh, process the segment text
  switch (cb->state) {
    case TCP_CB_STATE_ESTABLISHED:
    case TCP_CB_STATE_FIN_WAIT1:
    case TCP_CB_STATE_FIN_WAIT2:
      // copy segment to receive buffer
      memcpy(cb->window + (sizeof(cb->window) - cb->rcv.wnd),
             (uint8_t *)hdr + hlen, plen);
      cb->rcv.nxt = ntoh32(hdr->seq) + plen;
      cb->rcv.wnd -= plen;
      tcp_tx(cb, cb->snd.nxt, cb->rcv.nxt, TCP_FLG_ACK, NULL, 0);
      pthread_cond_signal(&cb->cond);
      break;

    case TCP_CB_STATE_CLOSING:
    case TCP_CB_STATE_TIME_WAIT:
    case TCP_CB_STATE_CLOSE_WAIT:
    case TCP_CB_STATE_LAST_ACK:
      // this should not occur. ignore the text
      break;

    case TCP_CB_STATE_SYN_RCVD:
      // do nothing
      break;
  }

  // eighth, check the FIN bit
  if (TCP_FLG_ISSET(hdr->flg, TCP_FLG_FIN)) {
    // TODO: signal "connection closing"
    cb->rcv.nxt = ntoh32(hdr->seq) + 1;
    tcp_tx(cb, cb->snd.nxt, cb->rcv.nxt, TCP_FLG_ACK, NULL, 0);
    switch (cb->state) {
      case TCP_CB_STATE_SYN_RCVD:
      case TCP_CB_STATE_ESTABLISHED:
        cb->state = TCP_CB_STATE_CLOSE_WAIT;
        break;

      case TCP_CB_STATE_FIN_WAIT1:
        cb->state = TCP_CB_STATE_CLOSING;
        break;

      case TCP_CB_STATE_FIN_WAIT2:
        cb->state = TCP_CB_STATE_TIME_WAIT;
        // TODO: start time-wait timer
        // TODO: turn off other timers
        break;

      case TCP_CB_STATE_CLOSING:
      case TCP_CB_STATE_CLOSE_WAIT:
      case TCP_CB_STATE_LAST_ACK:
        // remain state
        break;

      case TCP_CB_STATE_TIME_WAIT:
        // remain state
        // TODO: restart the 2MSL timeout
        break;
    }
  }
  return;
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

#ifdef TCP_DEBUG
  fprintf(stderr, ">>> tcp_tx <<<\n");
  tcp_dump(cb, hdr, len);
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
    if (IS_FREE_CB(cb)) {
      // cache cb for the case SYN message and no cb is connected to this peer
      if (fcb == NULL) {
        fcb = cb;
      }
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
      cb->state = lcb->state;
      cb->port = lcb->port;
      cb->parent = lcb;
    } else {
      // this port is not listened.
      // this packet is invalid. no connection is found.
      cb->state = TCP_CB_STATE_CLOSED;
      cb->port = 0;
    }
    cb->peer.addr = *src;
    cb->peer.port = hdr->src;
  }
  // else cb that matches this tcp packet is found.

#ifdef TCP_DEBUG
  fprintf(stderr, ">>> tcp_rx <<<\n");
  tcp_dump(cb, hdr, len - sizeof(struct tcp_hdr));
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
    if (IS_FREE_CB(cb)) {
      cb->used = 1;
      pthread_mutex_unlock(&mutex);
      return i;
    }
  }
  pthread_mutex_unlock(&mutex);
  fprintf(stderr, "error:  insufficient resources\n");
  return -1;
}

int tcp_close(struct tcp_cb *cb) {
  struct tcp_cb *backlog = NULL;
  size_t size;
  if (!cb->used) {
    fprintf(stderr, "error:  connection illegal for this process\n");
    return -1;
  }

  cb->used = 0;

  switch (cb->state) {
    case TCP_CB_STATE_CLOSED:
      break;

    case TCP_CB_STATE_LISTEN:
      // close all cb in backlog
      while (queue_pop(&cb->backlog, &backlog, &size) != -1) {
        tcp_close(backlog);
      }
    case TCP_CB_STATE_SYN_SENT:
      // close socket
      cb->state = TCP_CB_STATE_CLOSED;
      pthread_cond_broadcast(&cb->cond);
      break;

    case TCP_CB_STATE_SYN_RCVD:
      // if send buffer is empty
      cb->snd.nxt++;
      tcp_tx(cb, cb->snd.nxt, cb->rcv.nxt, TCP_FLG_FIN, NULL, 0);
      cb->state = TCP_CB_STATE_FIN_WAIT1;
      // TODO: else then wait change to ESTABLISHED state
      break;

    case TCP_CB_STATE_ESTABLISHED:
      // if send buffer is empty
      // TODO: else then wait send all data in send buffer
      cb->snd.nxt++;
      tcp_tx(cb, cb->snd.nxt, cb->rcv.nxt, TCP_FLG_FIN | TCP_FLG_ACK, NULL, 0);
      cb->state = TCP_CB_STATE_FIN_WAIT1;
      break;

    case TCP_CB_STATE_FIN_WAIT1:
    case TCP_CB_STATE_FIN_WAIT2:
    // "ok" response would be acceptable
    case TCP_CB_STATE_CLOSING:
    case TCP_CB_STATE_TIME_WAIT:
    case TCP_CB_STATE_LAST_ACK:
      fprintf(stderr, "error:  connection closing\n");
      return -1;

    case TCP_CB_STATE_CLOSE_WAIT:
      // wait send all data in send buffer
      cb->snd.nxt++;
      tcp_tx(cb, cb->snd.nxt, cb->rcv.nxt, TCP_FLG_FIN, NULL, 0);
      cb->state = TCP_CB_STATE_CLOSING;
      break;
  }

  return 0;
}

int tcp_api_close(int soc) {
  int ret;

  // validate soc id
  if (TCP_SOCKET_INVALID(soc)) {
    return -1;
  }

  pthread_mutex_lock(&mutex);
  ret = tcp_close(&cb_table[soc]);
  pthread_mutex_unlock(&mutex);

  return ret;
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
        if (!IS_FREE_CB(tmp) && tmp->port == hton16((uint16_t)i)) {
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

int tcp_api_bind(int soc, uint16_t port) {
  struct tcp_cb *cb;
  int i;

  // validate soc id
  if (TCP_SOCKET_INVALID(soc)) {
    return -1;
  }

  pthread_mutex_lock(&mutex);

  // check port is already used
  for (i = 0; i < TCP_CB_TABLE_SIZE; i++) {
    if (cb_table[i].port == hton16(port)) {
      pthread_mutex_unlock(&mutex);
      fprintf(stderr, "error:  port is already used\n");
      return -1;
    }
  }

  // check cb is closed
  cb = &cb_table[soc];
  if (!cb->used || cb->state != TCP_CB_STATE_CLOSED) {
    pthread_mutex_unlock(&mutex);
    return -1;
  }

  // TODO: bind ip address

  // set port number
  cb->port = hton16(port);
  pthread_mutex_unlock(&mutex);
  return 0;
}

int tcp_api_listen(int soc) {
  struct tcp_cb *cb;
  int i;

  // validate soc id
  if (TCP_SOCKET_INVALID(soc)) {
    return -1;
  }

  pthread_mutex_lock(&mutex);
  cb = &cb_table[soc];
  if (!cb->used || cb->state != TCP_CB_STATE_CLOSED || !cb->port) {
    pthread_mutex_unlock(&mutex);
    return -1;
  }
  cb->state = TCP_CB_STATE_LISTEN;
  pthread_mutex_unlock(&mutex);
  return 0;
}

int tcp_api_accept(int soc) {
  struct tcp_cb *cb, *backlog = NULL;
  size_t size;

  // validate soc id
  if (TCP_SOCKET_INVALID(soc)) {
    return -1;
  }

  pthread_mutex_lock(&mutex);
  cb = &cb_table[soc];
  if (!cb->used || cb->state != TCP_CB_STATE_LISTEN) {
    pthread_mutex_unlock(&mutex);
    return -1;
  }

  while (cb->state == TCP_CB_STATE_LISTEN &&
         queue_pop(&cb->backlog, &backlog, &size) == -1) {
    pthread_cond_wait(&cb->cond, &mutex);
  }

  if (!backlog) {
    pthread_mutex_unlock(&mutex);
    return -1;
  }

  backlog->used = 1;
  pthread_mutex_unlock(&mutex);

  return array_offset(cb_table, backlog);
}
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