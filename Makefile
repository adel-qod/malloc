CC = gcc
CFLAGS = -std=gnu99 -g -O3 -pedantic -W -Wall -Wextra

OBJS = mm.o test_functions.o test_main.o

memory_allocator: $(OBJS)
	$(CC) $(CFLAGS) -o mm_test_suit $(OBJS) 

test_main.o: test_main.c
	$(CC) $(CFLAGS) -c test_main.c

test_functions.o: test_functions.c
	$(CC) $(CFLAGS) -c test_functions.c

mm.o: mm.c
	$(CC) $(CFLAGS) -c mm.c

clean:
	-rm mm_test_suit *.o
