TEST = test/raw_test
OBJS = raw.o
CFLAGS := $(CFLAGS) -g -W -Wall -Wno-unused-parameter -I .

ifeq ($(shell uname), Linux)
	OBJS := $(OBJS) raw/soc.o
	TEST := $(TEST) test/raw_soc_test
	CFLAGS := $(CFLAGS) -DHAVE_PF_PACKET
endif

.PHONY: all clean

all: $(TEST)

$(TEST): % : %.o $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

.c.o:
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(TEST) $(TEST:=.o) $(OBJS)
