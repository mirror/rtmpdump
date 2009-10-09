/*  RTMPDump
 *  Copyright (C) 2009 Andrej Stepanchuk
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with RTMPDump; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *  http://www.gnu.org/copyleft/gpl.html
 *
 */
#include <string>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <signal.h> // to catch Ctrl-C

#include <getopt.h>

#include "rtmp.h"
#include "log.h"
#include "AMFObject.h"

using namespace RTMP_LIB;

#define RTMPDUMP_VERSION	"v1.3d"

#define RD_SUCCESS		0
#define RD_FAILED		1
#define RD_INCOMPLETE		2

uint32_t nTimeStamp = 0;

#ifdef _DEBUG
uint32_t debugTS = 0;
int pnum=0;
#endif

uint32_t nIgnoredFlvFrameCounter = 0;
uint32_t nIgnoredFrameCounter = 0;
#define MAX_IGNORED_FRAMES	50	

int WriteStream(
		CRTMP* rtmp, 
		char **buf,			// target pointer, maybe preallocated
		unsigned int len, 		// length of buffer if preallocated
		uint32_t *tsm, 			// pointer to timestamp, will contain timestamp of last video packet returned
		bool bNoHeader, 		// resuming mode, will not write FLV header and compare metaHeader and first kexframe
		char *metaHeader, 		// pointer to meta header (if bNoHeader == TRUE)
		uint32_t nMetaHeaderSize,	// length of meta header, if zero meta header check omitted (if bNoHeader == TRUE)
		char *initialFrame,		// pointer to initial keyframe (no FLV header or tagSize, raw data) (if bNoHeader == TRUE)
		uint8_t initialFrameType,	// initial frame type (audio or video)
		uint32_t nInitialFrameSize,	// length of initial frame in bytes, if zero initial frame check omitted (if bNoHeader == TRUE)
		uint8_t *dataType		// whenever we get a video/audio packet we set an appropriate flag here, this will be later written to the FLV header
	)
{
	char flvHeader[] = { 'F', 'L', 'V', 0x01,
                             0x00,//5, // video + audio
                             0x00, 0x00, 0x00, 0x09,
                             0x00, 0x00, 0x00, 0x00 // first prevTagSize=0
	};
	
	static bool bStopIgnoring = false;
	static bool bSentHeader = false;
	static bool bFoundKeyframe = false;
	static bool bFoundFlvKeyframe = false;

	uint32_t prevTagSize = 0;
	RTMPPacket packet;

	if(rtmp->GetNextMediaPacket(packet))
	{
		char *packetBody	= packet.m_body;
		unsigned int nPacketLen	= packet.m_nBodySize;

		// skip video info/command packets
		if(packet.m_packetType == 0x09 && 
		   nPacketLen == 2 &&
		((*packetBody & 0xf0) == 0x50)) {
			return 0;
		}

		if(packet.m_packetType == 0x09 && nPacketLen <= 5) {
			Log(LOGWARNING, "ignoring too small video packet: size: %d", nPacketLen);
			return 0;
		}
		if(packet.m_packetType == 0x08 && nPacketLen <= 1) {
			Log(LOGWARNING, "ignoring too small audio packet: size: %d", nPacketLen);
			return 0;
		}
#ifdef _DEBUG
		debugTS += packet.m_nInfoField1;
		Log(LOGDEBUG, "type: %d, size: %d, TS: %d ms, sent TS: %d ms", packet.m_packetType, nPacketLen, debugTS, packet.m_nInfoField1);
		if(packet.m_packetType == 0x09)
			Log(LOGDEBUG, "frametype: %02X", (*packetBody & 0xf0));
#endif

		// check the header if we get one
		if(bNoHeader && packet.m_nInfoField1 == 0) {
			if(nMetaHeaderSize > 0 && packet.m_packetType == 0x12) {
			
				RTMP_LIB::AMFObject metaObj;
                        	int nRes = metaObj.Decode(packetBody, nPacketLen);
                        	if(nRes >= 0) {
					std::string metastring = metaObj.GetProperty(0).GetString();

                                	if(metastring == "onMetaData") {
						// comapre
						if((nMetaHeaderSize != nPacketLen) || 
						   (memcmp(metaHeader, packetBody, nMetaHeaderSize) != 0)) {
							return -2;
						}
					}                     	
				}
			}

			// check first keyframe to make sure we got the right position in the stream!
			// (the first non ignored frame)
			if(nInitialFrameSize > 0) {

				// video or audio data
				if(packet.m_packetType == initialFrameType && nInitialFrameSize == nPacketLen) {
					// we don't compare the sizes since the packet can contain several FLV packets, just make
					// sure the first frame is our keyframe (which we are going to rewrite)
					if(memcmp(initialFrame, packetBody, nInitialFrameSize) == 0) {
						Log(LOGDEBUG, "Checked keyframe successfully!");
						bFoundKeyframe = true;
						return 0; // ignore it! (what about audio data after it? it is handled by ignoring all 0ms frames, see below)
					}
				}

				// hande FLV streams, even though the server resends the keyframe as an extra video packet
				// it is also included in the first FLV stream chunk and we have to compare it and
				// filter it out !!
				if(packet.m_packetType == 0x16) {
					// basically we have to find the keyframe with the correct TS being nTimeStamp
					unsigned int pos=0;
					uint32_t ts = 0;
					//bool bFound = false;

                        		while(pos+11 < nPacketLen) {
                                		uint32_t dataSize = CRTMP::ReadInt24(packetBody+pos+1); // size without header (11) and prevTagSize (4)
                                		ts = CRTMP::ReadInt24(packetBody+pos+4);
                                		ts |= (packetBody[pos+7]<<24);
						
						#ifdef _DEBUG	
						Log(LOGDEBUG, "keyframe search: FLV Packet: type %02X, dataSize: %d, timeStamp: %d ms",
						                                                packetBody[pos], dataSize, ts);
						#endif
						// ok, is it a keyframe!!!: well doesn't work for audio!
						if(packetBody[0] == initialFrameType /* && (packetBody[11]&0xf0) == 0x10*/) {
							if(ts == nTimeStamp) {
								Log(LOGDEBUG, "Found keyframe with resume-keyframe timestamp!");
								if(nInitialFrameSize != dataSize || memcmp(initialFrame, packetBody+pos+11, nInitialFrameSize) != 0) {
									Log(LOGERROR, "FLV Stream: Keyframe doesn't match!");
									return -2;
								}
								bFoundFlvKeyframe = true;

								// ok, skip this packet
								// check whether skipable:
								if(pos+11+dataSize+4 > nPacketLen) {
									Log(LOGWARNING, "Non skipable packet since it doesn't end with chunk, stream corrupt!");
									return -2;
								}
								packetBody += (pos+11+dataSize+4);
								nPacketLen -= (pos+11+dataSize+4);

								goto stopKeyframeSearch;

							} else if(nTimeStamp < ts)
								goto stopKeyframeSearch; // the timestamp ts will only increase with further packets, wait for seek
						} 
                                		pos += (11+dataSize+4);
                        		}
					if(ts < nTimeStamp) {
						Log(LOGERROR, "First packet does not contain keyframe, all timestamps are smaller than the keyframe timestamp, so probably the resume seek failed?");
					}
stopKeyframeSearch:
					;
					//*
					if(!bFoundFlvKeyframe) {
						Log(LOGERROR, "Couldn't find the seeked keyframe in this chunk!");
						return 0;//-2;
					}//*/
				}
			}
		}

                // skip till we find out keyframe (seeking might put us somewhere before it)
		if(bNoHeader && !bFoundKeyframe && packet.m_packetType != 0x16) {
                        Log(LOGWARNING, "Stream does not start with requested frame, ignoring data... ");
                        nIgnoredFrameCounter++;
                        if(nIgnoredFrameCounter > MAX_IGNORED_FRAMES)
                                return -2;
                        return 0;
                }
		// ok, do the same for FLV streams
		if(bNoHeader && !bFoundFlvKeyframe && packet.m_packetType == 0x16) {
                        Log(LOGWARNING, "Stream does not start with requested FLV frame, ignoring data... ");
                        nIgnoredFlvFrameCounter++;
                        if(nIgnoredFlvFrameCounter > MAX_IGNORED_FRAMES)
                                return -2;
                        return 0;
                }	

		// if bNoHeader, we continue a stream, we have to ignore the 0ms frames since these are the first keyframes, we've got these
		// so don't mess around with multiple copies sent by the server to us! (if the keyframe is found at a later position
		// there is only one copy and it will be ignored by the preceding if clause)
		if(!bStopIgnoring && bNoHeader && packet.m_packetType != 0x16) { // exclude type 0x16 (FLV) since it can conatin several FLV packets
			if(packet.m_nInfoField1 == 0) {
				return 0;
			} else {
				bStopIgnoring = true; // stop ignoring packets
			}
		}

		// calculate packet size and reallocate buffer if necessary
		unsigned int size = nPacketLen 
			+ ((bSentHeader || bNoHeader) ? 0 : sizeof(flvHeader))
			+ ((packet.m_packetType == 0x08 || packet.m_packetType == 0x09 || packet.m_packetType == 0x12) ? 11 : 0)
			+ (packet.m_packetType != 0x16 ? 4 : 0);
		
		if(size+4 > len) { // the extra 4 is for the case of an FLV stream without a last prevTagSize (we need extra 4 bytes to append it)
			*buf = (char *)realloc(*buf, size+4);
			if(*buf == 0) {
				Log(LOGERROR, "Couldn't reallocate memory!");
				return -1; // fatal error
			}
		}
		char *ptr = *buf;

		if(!bSentHeader && !bNoHeader)
		{
			memcpy(ptr, flvHeader, sizeof(flvHeader));
			ptr+=sizeof(flvHeader);

			bSentHeader = true;
		}
		// audio (0x08), video (0x09) or metadata (0x12) packets :
		// construct 11 byte header then add rtmp packet's data
		if(packet.m_packetType == 0x08 || packet.m_packetType == 0x09 || packet.m_packetType == 0x12)
		{
			// set data type
			*dataType |= (((packet.m_packetType == 0x08)<<2)|(packet.m_packetType == 0x09));

			nTimeStamp += packet.m_nInfoField1;
			prevTagSize = 11 + nPacketLen;
			//nTimeStamp += packet.m_nInfoField1;

			//Log(LOGDEBUG, "%02X: Added TS: %d ms, TS: %d", packet.m_packetType, packet.m_nInfoField1, nTimeStamp);
			*ptr = packet.m_packetType;
			ptr++;
			ptr += CRTMP::EncodeInt24(ptr, nPacketLen);

			/*if(packet.m_packetType == 0x09) { // video

				// H264 fix:
				if((packetBody[0] & 0x0f) == 7) { // CodecId = H264
					uint8_t packetType = *(packetBody+1);
					
					uint32_t ts = CRTMP::ReadInt24(packetBody+2); // composition time
					int32_t cts = (ts+0xff800000)^0xff800000;
					Log(LOGDEBUG, "cts  : %d\n", cts);

					nTimeStamp -= cts;
					// get rid of the composition time
					CRTMP::EncodeInt24(packetBody+2, 0);
				}
				Log(LOGDEBUG, "VIDEO: nTimeStamp: 0x%08X (%d)\n", nTimeStamp, nTimeStamp);
			}*/

			ptr += CRTMP::EncodeInt24(ptr, nTimeStamp);
			*ptr = (char)((nTimeStamp & 0xFF000000) >> 24);
			ptr++;

			// stream id
			ptr += CRTMP::EncodeInt24(ptr, 0);
		}

		memcpy(ptr, packetBody, nPacketLen);
		unsigned int len = nPacketLen;
		
		// correct tagSize and obtain timestamp if we have an FLV stream
		if(packet.m_packetType == 0x16) 
		{
			unsigned int pos=0;

                        while(pos+11 < nPacketLen) {
                        	uint32_t dataSize = CRTMP::ReadInt24(packetBody+pos+1); // size without header (11) or prevTagSize (4)
                                nTimeStamp = CRTMP::ReadInt24(packetBody+pos+4);
                                nTimeStamp |= (packetBody[pos+7]<<24);
				
				// set data type
				*dataType |= (((*(packetBody+pos) == 0x08)<<2)|(*(packetBody+pos) == 0x09));

                                if(pos+11+dataSize+4 > nPacketLen) {
                                	Log(LOGWARNING, "No tagSize found, appending!");
                                                
					// we have to append a last tagSize!
                                        prevTagSize = dataSize+11;
                                        CRTMP::EncodeInt32(ptr+pos+11+dataSize, prevTagSize);
                                        size+=4; len+=4;
                                } else {
                                        prevTagSize = CRTMP::ReadInt32(packetBody+pos+11+dataSize);
                                        
					#ifdef _DEBUG
					Log(LOGDEBUG, "FLV Packet: type %02X, dataSize: %d, tagSize: %d, timeStamp: %d ms",
                                                packetBody[pos], dataSize, prevTagSize, nTimeStamp);
					#endif

                                        if(prevTagSize != (dataSize+11)) {
                                                #ifdef _DEBUG
						Log(LOGWARNING, "tag size and data size are not consitent, writing tag size according to data size %d", dataSize+11);
                                                #endif

						prevTagSize = dataSize+11;
                                                CRTMP::EncodeInt32(ptr+pos+11+dataSize, prevTagSize);
                                        }
                                }

                                pos += (11+dataSize+4);
			}
		}
		ptr += len;

		if(packet.m_packetType != 0x16) { // FLV tag packets contain their own prevTagSize
			CRTMP::EncodeInt32(ptr, prevTagSize);
			//ptr += 4;
		}

		if(tsm)
			*tsm = nTimeStamp;

		return size;
	}

	return -1; // no more media packets
}

FILE *file = 0;
bool bCtrlC = false;

void sigIntHandler(int sig) {
	printf("Catched signal: %d, cleaning up, just a second...\n", sig);
	bCtrlC = true;
	signal(SIGINT, SIG_DFL);
}

//#define _DEBUG_TEST_PLAYSTOP

int main(int argc, char **argv)
{
//#ifdef _DEBUG_TEST_PLAYSTOP
//	RTMPPacket packet;
//#endif
	int nStatus = RD_SUCCESS;
	double percent = 0;
	double duration = 0.0;

	uint8_t dataType = 0;    // will be written into the FLV header (position 4)

	bool bResume = false;    // true in resume mode
	bool bNoHeader = false;  // in resume mode this will tell not to write an FLV header again
	bool bAudioOnly = false; // when resuming this will tell whether its an audio only stream
	uint32_t dSeek = 0;	 // seek position in resume mode, 0 otherwise
	uint32_t bufferTime = 10*60*60*1000; // 10 hours as default

	// meta header and initial frame for the resume mode (they are read from the file and compared with
	// the stream we are trying to continue
	char *metaHeader = 0;
	uint32_t nMetaHeaderSize = 0;
	
	// video keyframe for matching
	char *initialFrame = 0;
	uint32_t nInitialFrameSize = 0;
	int initialFrameType = 0; // tye: audio or video

	char *url = 0;
	char *swfUrl = 0;
	char *tcUrl = 0;
	char *pageUrl = 0;
	char *app = 0;
	char *auth = 0;
	char *flashVer = 0;

	char *flvFile = 0;

	char DEFAULT_FLASH_VER[]  = "LNX 9,0,124,0";

 	printf("RTMPDump %s\n", RTMPDUMP_VERSION);
	printf("(c) 2009 Andrej Stepanchuk, license: GPL\n\n");

	int opt;
	struct option longopts[] = {
		{"help",    0, NULL, 'h'},
		{"rtmp",    1, NULL, 'r'},
		{"swfUrl",  1, NULL, 's'},
		{"tcUrl",   1, NULL, 't'},
		{"pageUrl", 1, NULL, 'p'},
		{"app",     1, NULL, 'a'},
		{"auth",    1, NULL, 'u'},
		{"flashVer",1, NULL, 'f'},
		{"flv",     1, NULL, 'o'},
		{"resume",  0, NULL, 'e'},
		{0,0,0,0}
	};

	signal(SIGINT, sigIntHandler);

	while((opt = getopt_long(argc, argv, "hr:s:t:p:a:f:o:u:", longopts, NULL)) != -1) {
		switch(opt) {
			case 'h':
				printf("\nThis program dumps the media contnt streamed over rtmp.\n\n");
				printf("--help|-h\t\tPrints this help screen.\n");
				printf("--rtmp|-r url\t\tURL (e.g. rtmp//hotname[:port]/path)\n");
				printf("--swfUrl|-s url\t\tURL to player swf file\n");
				printf("--tcUrl|-t url\t\tURL to played stream\n");
				printf("--pageUrl|-p url\tWeb URL of played programme\n");
				printf("--app|-a app\t\tName of player used\n");
				printf("--auth|-u string\tAuthentication string to be appended to the connect string\n");
				printf("--flashVer|-f string\tflash version string (default: \"LNX 9,0,124,0\")\n");
				printf("--flv|-o string\t\tflv output file name\n\n");
				printf("--resume|-e\n\n");
				printf("If you don't pass parameters for swfUrl, tcUrl, pageUrl, app or auth these propertiews will not be included in the connect ");
				printf("packet.\n\n");
				return RD_SUCCESS;
			case 'r':
				url = optarg;
				break;
			case 's':
				swfUrl = optarg;
				break;
			case 't':
				tcUrl = optarg;
				break;
			case 'p':
				pageUrl = optarg;
				break;
			case 'a':
				app = optarg;
				break;
			case 'f':
				flashVer = optarg;
				break;
			case 'o':
				flvFile = optarg;
				break;
			case 'e':
				bResume = true;
				break;
			case 'u':
				auth = optarg;		
				break;
			default:
				printf("unknown option: %c\n", opt);
				break;
		}
	}

	if(url == 0) {
		printf("ERROR: You must specify a url (-r \"rtmp://host[:port]/playpath\" )\n");
		return RD_FAILED;
	}
	if(flvFile == 0) {
		printf("ERROR: You must specify an output flv file (-o filename)\n");
		return RD_FAILED;
	}

	if(flashVer == 0)
		flashVer = DEFAULT_FLASH_VER;

	int bufferSize = 1024*1024;
	char *buffer = (char *)malloc(bufferSize);
        int nRead = 0;

	memset(buffer, 0, bufferSize);

	CRTMP  *rtmp = new CRTMP();

	Log(LOGDEBUG, "Setting buffer time to: %dms", bufferTime);
	rtmp->SetBufferMS(bufferTime);

	unsigned long size = 0;
        uint32_t timestamp = 0;

	// ok, we have to get the timestamp of the last keyframe (only keyframes are seekable) / last audio frame (audio only streams) 
	if(bResume) {
		file = fopen(flvFile, "r+b");
		if(file == 0) {
			bResume = false; // we are back in fresh file mode (otherwise finalizing file won't be done)
			goto start; // file does not exist, so go back into normal mode
		}

		fseek(file, 0, SEEK_END);
		size = ftell(file);
		fseek(file, 0, SEEK_SET);

		if(size > 0) {
			// verify FLV format and read header 
			uint32_t prevTagSize = 0;

			// check we've got a valid FLV file to continue!
			if(fread(buffer, 1, 13, file) != 13) {
				Log(LOGERROR, "Couldn't read FLV file header!");
				nStatus = RD_FAILED;
				goto clean;
			}
			if(buffer[0] != 'F' || buffer[1] != 'L' || buffer[2] != 'V' || buffer[3] != 0x01) {
				Log(LOGERROR, "Inavlid FLV file!");
				nStatus = RD_FAILED;
                                goto clean;
			}

			if((buffer[4]&0x05) == 0) {
                                Log(LOGERROR, "FLV file contains neither video nor audio, aborting!");
				nStatus = RD_FAILED;
				goto clean;
			}
                        bAudioOnly = (buffer[4] & 0x4) && !(buffer[4] & 0x1);
                        if(bAudioOnly)
				Log(LOGDEBUG, "Resuming audio only stream!");
	
			uint32_t dataOffset = RTMP_LIB::CRTMP::ReadInt32(buffer+5);
			fseek(file, dataOffset, SEEK_SET);

			if(fread(buffer, 1, 4, file) != 4) {
				Log(LOGERROR, "Invalid FLV file: missing first prevTagSize!");
				nStatus = RD_FAILED;
                                goto clean;
			}
			prevTagSize = RTMP_LIB::CRTMP::ReadInt32(buffer);
			if(prevTagSize != 0) {
				Log(LOGWARNING, "First prevTagSize is not zero: prevTagSize = 0x%08X", prevTagSize);
			}

			// go through the file to find the mata data!
			uint32_t pos = dataOffset+4;
			bool bFoundMetaHeader = false;

			while(pos < size-4 && !bFoundMetaHeader) {
				fseek(file, pos, SEEK_SET);
				if(fread(buffer, 1, 4, file)!=4)
					break;

				uint32_t dataSize = RTMP_LIB::CRTMP::ReadInt24(buffer+1);
				
				if(buffer[0] == 0x12) {
					fseek(file, pos+11, SEEK_SET);
					if(fread(buffer, 1, dataSize, file) != dataSize)
						break;
					
					RTMP_LIB::AMFObject metaObj;
					int nRes = metaObj.Decode(buffer, dataSize);
					if(nRes < 0) {
						Log(LOGERROR, "%s, error decoding meta data packet", __FUNCTION__);
						break;
					}
					
					std::string metastring = metaObj.GetProperty(0).GetString();

					if(metastring == "onMetaData") {
						metaObj.Dump();
						
						nMetaHeaderSize = dataSize;
						metaHeader = (char *)malloc(nMetaHeaderSize);
						memcpy(metaHeader, buffer, nMetaHeaderSize);

						// get duration
						AMFObjectProperty prop;
                				if(RTMP_LIB::CRTMP::FindFirstMatchingProperty(metaObj, "duration", prop)) {
							duration = prop.GetNumber();
							Log(LOGDEBUG, "File has duration: %f", duration);
						}		

						bFoundMetaHeader = true;
						break;
					}
					//metaObj.Reset();
					//delete obj;
				}
				pos += (dataSize+11+4);
			}

			if(!bFoundMetaHeader)
				Log(LOGWARNING, "Couldn't locate meta data!");

			//if(!bAudioOnly) // we have to handle video/video+audio different since we have non-seekable frames
			//{
				// find the last seekable frame
				uint32_t tsize = 0;

				// go through the file and find the last video keyframe
				do {
					if(size-tsize < 13) {
						Log(LOGERROR, "Unexpected start of file, error in tag sizes, couldn't arrive at prevTagSize=0");
						nStatus = RD_FAILED; goto clean;
					}

					fseek(file, size-tsize-4, SEEK_SET);
					if(fread(buffer, 1, 4, file) != 4) {
						Log(LOGERROR, "Couldn't read prevTagSize from file!");
						nStatus = RD_FAILED; goto clean;
					}

					prevTagSize = RTMP_LIB::CRTMP::ReadInt32(buffer);
					//Log(LOGDEBUG, "Last packet: prevTagSize: %d", prevTagSize);
				
					if(prevTagSize == 0) {
						Log(LOGERROR, "Couldn't find keyframe to resume from!");
						nStatus = RD_FAILED; goto clean;
					}

					if(prevTagSize < 0 || prevTagSize > size-4-13) {
						Log(LOGERROR, "Last tag size must be greater/equal zero (prevTagSize=%d) and smaller then filesize, corrupt file!", prevTagSize);
						nStatus = RD_FAILED; goto clean;
					}
					tsize += prevTagSize+4;

					// read header
					fseek(file, size-tsize, SEEK_SET);
					if(fread(buffer, 1, 12, file) != 12) {
						Log(LOGERROR, "Couldn't read header!");
						nStatus=RD_FAILED; goto clean;
					}
					//*
					#ifdef _DEBUG
					uint32_t ts = RTMP_LIB::CRTMP::ReadInt24(buffer+4);
					ts |= (buffer[7]<<24);
					Log(LOGDEBUG, "%02X: TS: %d ms", buffer[0], ts);
					#endif	//*/
				} while(
						(bAudioOnly && buffer[0] != 0x08) ||
						(!bAudioOnly && (buffer[0] != 0x09 || (buffer[11]&0xf0) != 0x10))
					); // as long as we don't have a keyframe / last audio frame
		
				// save keyframe to compare/find position in stream
				initialFrameType = buffer[0];
				nInitialFrameSize = prevTagSize-11;
				initialFrame = (char *)malloc(nInitialFrameSize);
				
				fseek(file, size-tsize+11, SEEK_SET);
				if(fread(initialFrame, 1, nInitialFrameSize, file) != nInitialFrameSize) {
					Log(LOGERROR, "Couldn't read last keyframe, aborting!");
					nStatus=RD_FAILED;
					goto clean;
				}

				dSeek = RTMP_LIB::CRTMP::ReadInt24(buffer+4); // set seek position to keyframe tmestamp
				dSeek |= (buffer[7]<<24);
			//} 
			//else // handle audio only, we can seek anywhere we'd like
			//{
			//}

			if(dSeek < 0) {
				Log(LOGERROR, "Last keyframe timestamp is negative, aborting, your file is corrupt!");
				nStatus=RD_FAILED;
				goto clean;
			}
			Log(LOGDEBUG,"Last keyframe found at: %d ms, size: %d, type: %02X", dSeek, nInitialFrameSize, initialFrameType);

			/*
			// now read the timestamp of the frame before the seekable keyframe:
			fseek(file, size-tsize-4, SEEK_SET);
			if(fread(buffer, 1, 4, file) != 4) {
				Log(LOGERROR, "Couldn't read prevTagSize from file!");
				goto start;
			}
			uint32_t prevTagSize = RTMP_LIB::CRTMP::ReadInt32(buffer);
			fseek(file, size-tsize-4-prevTagSize+4, SEEK_SET);
			if(fread(buffer, 1, 4, file) != 4) {
                                Log(LOGERROR, "Couldn't read previous timestamp!");
                                goto start;
                        }
			uint32_t timestamp = RTMP_LIB::CRTMP::ReadInt24(buffer);
			timestamp |= (buffer[3]<<24);

			Log(LOGDEBUG, "Previuos timestamp: %d ms", timestamp);
			*/

			// seek to position after keyframe in our file (we will ignore the keyframes resent by the server
			// since they are sent a couple of times and handling this would be a mess)
			fseek(file, size-tsize+prevTagSize+4, SEEK_SET);
			
			// make sure the WriteStream doesn't write headers and ignores all the 0ms TS packets
			// (including several meta data headers and the keyframe we seeked to)
			bNoHeader = true;
		}
	} else {
start:
		if(file != 0)
			fclose(file);

		file = fopen(flvFile, "wb");
		if(file == 0) {
                        printf("Failed to open file!\n");
                        return RD_FAILED;
                }
	}
        
	printf("Connecting to %s ...\n", url);
/*
#ifdef _DEBUG_TEST_PLAYSTOP
	// DEBUG!!!! seek to end if duration known!
	printf("duration: %f", duration);
	//return 1;
	if(duration > 0)
		dSeek = (duration-5.0)*1000.0;
#endif*/
	if (!rtmp->Connect(url, tcUrl, swfUrl, pageUrl, app, auth, flashVer, dSeek)) {
		printf("Failed to connect!\n");
		return RD_FAILED;
	}
	printf("Connected...\n\n");

/*
	// DEBUG read out packets
#ifdef _DEBUG_TEST_PLAYSTOP
	printf("duration: %f", duration);
	while(rtmp->IsConnected() && rtmp->GetNextMediaPacket(packet)) {
		char str[256]={0};
        sprintf(str, "packet%d", pnum);
        pnum++;
        FILE *f = fopen(str, "wb");
        fwrite(packet.m_body, 1, packet.m_nBodySize, f);
        fclose(f);
		
		printf(".");
	}
	return 1;
#endif
*/
	#ifdef _DEBUG
	debugTS = dSeek;
	#endif

	timestamp  = dSeek;
	nTimeStamp = dSeek; // set offset if we continue	
	if(dSeek != 0) {
		printf("Continuing at TS: %d ms\n", nTimeStamp);
	}

	// print initial status
	printf("Starting download at ");
	if(duration > 0) {
		percent = ((double)timestamp) / (duration*1000.0)*100.0;
                percent = round(percent*10.0)/10.0;
                printf("%.3f KB (%.1f%%)\n", (double)size/1024.0, percent);
        } else {
                printf("%.3f KB\n", (double)size/1024.0);
        }

	do
	{
		nRead = WriteStream(rtmp, &buffer, bufferSize, &timestamp, bNoHeader, metaHeader, nMetaHeaderSize, initialFrame, initialFrameType, nInitialFrameSize, &dataType);

		//printf("nRead: %d\n", nRead);
		if(nRead > 0) {
			fwrite(buffer, sizeof(unsigned char), nRead, file);
			size += nRead;
	
			//printf("write %dbytes (%.1f KB)\n", nRead, nRead/1024.0);
			if(duration <= 0) // if duration unknown try to get it from the stream (onMetaData)
				duration = rtmp->GetDuration();

			if(duration > 0) {
				// make sure we claim to have enough buffer time!
				if(bufferTime < (duration*1000.0)) {
					bufferTime = (uint32_t)(duration*1000.0)+5000; // extra 5sec to make sure we've got enough
					
					Log(LOGDEBUG, "Detected that buffer time is less than duration, resetting to: %dms", bufferTime);
					rtmp->SetBufferMS(bufferTime);
					rtmp->UpdateBufferMS();
				}
				percent = ((double)timestamp) / (duration*1000.0)*100.0;
				percent = round(percent*10.0)/10.0;
				printf("\r%.3f KB (%.1f%%)", (double)size/1024.0, percent);
			} else {
				printf("\r%.3f KB", (double)size/1024.0);
			}
		}
		#ifdef _DEBUG
		else { Log(LOGDEBUG, "zero read!"); }
		#endif

	} while(!bCtrlC && nRead > -1 && rtmp->IsConnected());

	if(nRead == -2) {
		printf("Couldn't continue FLV file!\n\n");
		nStatus = RD_FAILED;
		goto clean;
	}

	// finalize header by writing the correct dataType
	if(!bResume) {
		//Log(LOGDEBUG, "Writing data type: %02X", dataType);
		fseek(file, 4, SEEK_SET);
		fwrite(&dataType, sizeof(unsigned char), 1, file);
	}
	if((duration > 0 && percent < 100.0) || bCtrlC || nRead != (-1)) {
		Log(LOGWARNING, "Download may be incomplete, try --resume!");
		nStatus = RD_INCOMPLETE;
	}

	fclose(file);

clean:
	printf("Closing connection... ");
	rtmp->Close();
	printf("done!\n\n");

	return nStatus;
}

