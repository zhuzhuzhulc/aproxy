#
# Makefile for Proxy Lab 
#
# You may modify is file any way you like (except for the handin
# rule). Autolab will execute the command "make" on your specific 
# Makefile to build your proxy from sources.
#
CC = gcc
CFLAGS = -g -Wall -DNCACHING
#CFLAGS = -g -DNCACHING
LDFLAGS = -lpthread

OBJS = proxy.o csapp.o cache.o

all: proxy

cache.o: cache.c cache.h
	$(CC) $(CFLAGS) $(LDFLAGS) -c cache.c

csapp.o: csapp.c csapp.h
	$(CC) $(CFLAGS) $(LDFLAGS) -c csapp.c

proxy.o: proxy.c csapp.h cache.h
	$(CC) $(CFLAGS) $(LDFLAGS) -c proxy.c

proxy: $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o proxy $(OBJS)

# Creates a tarball in ../proxylab-handin.tar that you should then
# hand in to Autolab. DO NOT MODIFY THIS!
handin:
	(make clean; cd ..; tar cvf proxylab-handin.tar aproxy --exclude tiny --exclude nop-server.py --exclude proxy --exclude driver.sh --exclude port-for-user.pl --exclude free-port.sh --exclude ".*")

clean:
	rm -f *~ *.o proxy core *.tar *.zip *.gzip *.bzip *.gz

