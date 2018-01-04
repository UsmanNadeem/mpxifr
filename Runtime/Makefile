#CC=gcc
CC=g++
GLIB_CFLAGS=-pthread `pkg-config --cflags glib-2.0`
GLIB_LIBS=-lpthread  -lgthread-2.0 -lglib-2.0


OPT=-O3
DBG=-g
CFLAGS=$(OPT) $(DBG) -fPIC $(GLIB_CFLAGS) -w -mrtm  -std=c++11
LDFLAGS=$(OPT) $(DBG) -shared -fPIC $(GLIB_LIBS)


all: Runtime

Runtime:
	g++ -Ofast -fPIC $(DBG) -w -c mash.c -o mash.o
	ar cr libmash.a mash.o
	$(CC) $(CFLAGS) -c $(CFLAGS) -o IFR.o IFR.c
	$(CC) $(CFLAGS) -c $(CFLAGS) -o IFR_Runtime.o IFR_Runtime.cpp
	$(CC) $(CFLAGS) -c $(CFLAGS) -o IFR_Runtime_orig.o IFR_Runtime_orig.cpp
	$(CC) $(CFLAGS) -c $(CFLAGS) -o IFR_Runtime_allraces.o IFR_Runtime_allraces.cpp
	$(CC) $(LDFLAGS) IFR.o IFR_Runtime.o libmash.a  -o libIFR_Runtime.so
	$(CC) $(LDFLAGS) IFR.o IFR_Runtime_allraces.o libmash.a  -o libIFR_Runtime_allraces.so
	$(CC) $(LDFLAGS) IFR.o IFR_Runtime_orig.o -o libIFR_Runtime_orig.so

clean:
	-rm -f *.so
	-rm -f *.o
	-rm -f *.a

