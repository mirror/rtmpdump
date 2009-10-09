CFLAGS=-Wall
CXXFLAGS=-Wall
LDFLAGS=-Wall

all: rtmpdump

clean:
	rm -f rtmpdump *.o

rtmpdump: log.o rtmp.o AMFObject.o rtmppacket.o rtmpdump.o
	g++ $(LDFLAGS) $^ -o $@ -lboost_regex

log.o: log.cpp log.h Makefile
rtmp.o: rtmp.cpp rtmp.h log.h AMFObject.h Makefile
AMFObject.o: AMFObject.cpp AMFObject.h log.h rtmp.h Makefile
rtmppacket.o: rtmppacket.cpp rtmppacket.h log.h Makefile
rtmpdump.o: rtmpdump.cpp rtmp.h log.h AMFObject.h Makefile

