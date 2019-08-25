TEST = test/raw_test test/ethernet_test test/ip_test test/mask_test
OBJS = raw.o util.o ethernet.o net.o ip.o
CFLAGS := $(CFLAGS) -g -W -Wall -Wno-unused-parameter -I . -DDEBUG

ifeq ($(shell uname), Linux)
	OBJS := $(OBJS) raw/soc.o
	TEST := $(TEST) test/raw_soc_test
	CFLAGS := $(CFLAGS) -pthread -DHAVE_PF_PACKET
endif

.PHONY: all clean

all: $(TEST)

$(TEST): % : %.o $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

.c.o:
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(TEST) $(TEST:=.o) $(OBJS)
