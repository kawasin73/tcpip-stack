# TCP/IP stack

Create TCP/IP stack from scratch.

This is based on [pandax381/microps](https://github.com/pandax381/microps).

`tcp.c` is based on [RFC 793 - Transmission Control Protocol](https://tools.ietf.org/html/rfc793#page-52).

## How to use

Use tap device on Linux on VirtualBox

```bash
# use brctl to create bridge
$ sudo apt install -y bridge-utils

# create br0 and connect it to eth1
$ sudo brctl addbr br0
$ sudo brctl addif br0 eth1
$ sudo ip link set br0 up
$ sudo ip addr add dev br0 192.168.33.12/24

# create apps/tcp_echo and test binary at /vagrant
$ cd /vagrant
$ make clean && make

# start tcp echo server
$ sudo apps/tcp_echo

# connect created tap1 device to br0
$ sudo brctl addif br0 tap2 && sudo ip link set tap2 up && sudo arp -d 192.168.33.13

# connect using telnet
$ telnet 192.168.33.13 20000
```

## Features

- [x] Raw Device
  - [x] tap device on Linux
  - [x] PF_PACKET socket on Linux
  - [ ] tap device on BSD
  - [ ] BFP on BSD
- [x] Ethernet
- [x] ARP
- [x] IP
  - [x] ip_tx
  - [x] ip_rx
  - [x] Fragmentation
  - [x] Checksum
  - [ ] Routing
  - [ ] Packet Forwarding
  - [ ] Dynamic network device selection by IP Address
- [ ] ICMP
- [ ] DHCP
- [x] TCP
  - [x] Socket API (open, connect, bind, listen, accept, send, recv)
    - [ ] Blocking I/O API
    - [ ] Non-blocing I/O API
    - [ ] Event Driven Architecture API (like select, poll, epoll, kqueue...)
  - [x] Timeout
    - [x] User Timeout
    - [x] Retransmission Timeout
    - [x] TIME WAIT Timeout
  - [x] Connection Control
    - [x] Passive Open
      - [x] Backlog
      - [ ] SYN-Backlog
    - [x] Active Open
    - [x] Close Connection
  - [x] Send Data
    - [x] Data Segmentation by MTU
    - [x] Retransmission
    - [x] Flow Control
    - [ ] Congestion Control
  - [x] Receive Data
    - [x] Reply ACK
    - [ ] Partial ACK
  - [ ] Sequential ID Rotation
  - [ ] URG Pointer
  - [ ] Precedence and Security
- [ ] UDP
- [ ] Payload Passing over layers with Zero Copy

## Development on VSCode

I use macOS and create Ubuntu 18.04 VM using vagrant.
I use VSCode as editor and Remote SSH plugin.

```bash
$ vagrant up
$ vagrant ssh-config > .ssh-config
```

Setup VSCode Remote SSH with `.ssh-config` and login.
Then re-install VSCode plugins in remote VSCode.

## LICENSE

MIT
