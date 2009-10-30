CC=gcc
CXX=g++
LD=ld

OPT=-O2
CFLAGS=-Wall $(OPT)
CXXFLAGS=-Wall $(OPT)
LDFLAGS=-Wall

all: rtmpdump

clean:
	rm -f *.o

streams: bytes.o log.o rtmp.o AMFObject.o rtmppacket.o streams.o parseurl.o dh.o handshake.o
	$(CXX) $(LDFLAGS) $^ -o $@_x86 -lpthread -lssl -lcrypto

rtmpdump: bytes.o log.o rtmp.o AMFObject.o rtmppacket.o rtmpdump.o parseurl.o dh.o handshake.o
	$(CXX) $(LDFLAGS) $^ -o $@_x86 -lssl -lcrypto

bytes.o: bytes.c bytes.h Makefile
log.o: log.c log.h Makefile
rtmp.o: rtmp.cpp rtmp.h log.h AMFObject.h Makefile
AMFObject.o: AMFObject.cpp AMFObject.h log.h rtmp.h Makefile
rtmppacket.o: rtmppacket.cpp rtmppacket.h log.h Makefile
rtmpdump.o: rtmpdump.cpp rtmp.h log.h AMFObject.h Makefile
parseurl.o: parseurl.c parseurl.h log.h Makefile
streams.o: streams.cpp log.h Makefile
dh.o: dh.c dh.h log.h Makefile
handshake.o: handshake.cpp log.h Makefile

