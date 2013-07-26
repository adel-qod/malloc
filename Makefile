CC = clang
CFLAGS = -g -O3 -pedantic -W -Wall

OBJS = mm.o test_main.o

memory_allocator: $(OBJS)
	$(CC) $(CFLAGS) -o mm_test_suit $(OBJS) 

test_main.o: test_main.c
	$(CC) $(CFLAGS) -c test_main.c

mm.o: mm.c
	$(CC) $(CFLAGS) -c mm.c

clean:
	-rm mm_test_suit *.o
