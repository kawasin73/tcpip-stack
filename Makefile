TEST = test/raw_test test/ethernet_test test/ip_test test/mask_test \
	test/tcp_test test/tcp_listen_test test/queue_test
OBJS = raw.o util.o ethernet.o net.o ip.o arp.o tcp.o
CFLAGS := $(CFLAGS) -g -W -Wall -Wno-unused-parameter -I . -DTCP_DEBUG -g

ifeq ($(shell uname), Linux)
	OBJS := $(OBJS) raw/soc.o raw/tap_linux.o
	TEST := $(TEST) test/raw_soc_test test/raw_tap_test
	CFLAGS := $(CFLAGS) -pthread -DHAVE_PF_PACKET -DHAVE_TAP
endif

.PHONY: all clean

all: $(TEST)

$(TEST): % : %.o $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

.c.o:
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(TEST) $(TEST:=.o) $(OBJS)
