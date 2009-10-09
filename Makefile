CROSS=
CC=$(CROSS)gcc
CXX=$(CROSS)g++
LD=$(CROSS)ld

#CC=arm-linux-gcc
#CXX=arm-linux-g++
#LD=arm-linux-ld
CFLAGS=-Wall
CXXFLAGS=-Wall
LDFLAGS=-Wall

all: rtmpdump

clean:
	rm -f rtmpdump rtmpdump.exe *.o

rtmpdump: bytes.o log.o rtmp.o AMFObject.o rtmppacket.o rtmpdump.o parseurl.o
	$(CXX) $(LDFLAGS) $^ -o $@

win32: bytes.o log.o rtmp.o AMFObject.o rtmppacket.o rtmpdump.o parseurl.o
	$(CXX) $(LDFLAGS) $^ -o rtmpdump.exe -lws2_32 -lwinmm

bytes.o: bytes.cpp bytes.h Makefile
log.o: log.cpp log.h Makefile
rtmp.o: rtmp.cpp rtmp.h log.h AMFObject.h Makefile
AMFObject.o: AMFObject.cpp AMFObject.h log.h rtmp.h Makefile
rtmppacket.o: rtmppacket.cpp rtmppacket.h log.h Makefile
rtmpdump.o: rtmpdump.cpp rtmp.h log.h AMFObject.h Makefile
parseurl.o: parseurl.cpp parseurl.h log.h Makefile

