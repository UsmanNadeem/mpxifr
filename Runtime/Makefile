#CC=gcc
#CC=/sampa/home/effinger/gcc-4.6.3/install/bin/g++
CC=g++
GLIB_CFLAGS=-pthread `pkg-config --cflags glib-2.0`
GLIB_LIBS=-lpthread  -lgthread-2.0 -lglib-2.0


OPT=-O3
DBG=-g
CFLAGS=$(OPT) $(DBG) -fPIC $(GLIB_CFLAGS) -w
LDFLAGS=$(OPT) $(DBG) -shared -fPIC $(GLIB_LIBS)


all: libmash.a Runtime

libmash.a: mash.o
	@ar cr libmash.a mash.o

mash.o: mash.c
	@$(CC) -Ofast -fPIC -w -c mash.c -o mash.o

Runtime: libmash.a
	@$(CC) $(CFLAGS) -c $(CFLAGS) -o IFR.o IFR.c
	@$(CC) $(CFLAGS) -c $(CFLAGS) -o IFR_Runtime.o IFR_Runtime.cpp
	@$(CC) $(LDFLAGS) libmash.a IFR.o IFR_Runtime.o -o libIFR_Runtime.so

clean:
	-rm -f *.so
	-rm -f *.o
	-rm -f *.a

