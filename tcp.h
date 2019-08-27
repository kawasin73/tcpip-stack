#ifndef _TCP_H_
#define _TCP_H_

#include <stdint.h>
#include <unistd.h>
#include "ip.h"

int tcp_init(void);
int tcp_api_open(void);
int tcp_api_close(int soc);
int tcp_api_connect(int soc, ip_addr_t *addr, uint16_t port);
int tcp_api_bind(int soc, uint16_t port);
int tcp_api_listen(int soc);
int tcp_api_accept(int soc);
ssize_t tcp_api_recv(int soc, uint8_t *buf, size_t size);
ssize_t tcp_api_send(int soc, uint8_t *buf, size_t len);

#endif
