CFLAGS += -O2 -g -Wall #-msse4.2

all: Json.o test.o
	$(CC) $(CFLAGS) Json.o test.o -o Json
clean:
	rm -rf *.o Json
