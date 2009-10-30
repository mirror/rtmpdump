#CROSS_COMPILE=arm-angstrom-linux-gnueabi-
#CROSS_COMPILE=mingw32-
CC=$(CROSS_COMPILE)gcc
CXX=$(CROSS_COMPILE)g++
LD=$(CROSS_COMPILE)ld

#STAGING=/OE/tmp/staging/armv7a-angstrom-linux-gnueabi
#INC=-I$(STAGING)/usr/include
OPT=-O2
CFLAGS=-Wall $(INC) $(OPT)
CXXFLAGS=-Wall $(INC) $(OPT)
LDFLAGS=-Wall
#LIBS=-lws2_32 -lwinmm -lcrypto -lgdi32
LIBS=-lcrypto
THREADLIB=-lpthread
SLIBS=$(THREADLIB) $(LIBS)

#EXT=.exe
EXT=

all: rtmpdump

clean:
	rm -f *.o

streams: bytes.o log.o rtmp.o AMFObject.o rtmppacket.o streams.o parseurl.o dh.o handshake.o
	$(CXX) $(LDFLAGS) $^ -o $@$(EXT) $(SLIBS)

rtmpdump: bytes.o log.o rtmp.o AMFObject.o rtmppacket.o rtmpdump.o parseurl.o dh.o handshake.o
	$(CXX) $(LDFLAGS) $^ -o $@$(EXT) $(LIBS)

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

