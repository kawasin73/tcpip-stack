TEST = test/raw_soc_test
OBJS = raw/soc.o

test/raw_soc_test: test/raw_soc_test.c $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) test/raw_soc_test.c

.c.o:
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(TEST) $(TEST:=.o) $(OBJS)
