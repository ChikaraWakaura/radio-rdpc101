# make
CC         = gcc
CFLAGS     = -O2 -Wall
#CFLAGS     = -DDEBUG -g -O0 -Wall
LDFLAGS    = 

all : setfreq check-ioctl

.c.o:
	$(CC) $(CFLAGS) -c $<

clean:
	rm -f *.o
