/*
 *      Copyright (C) 2005-2008 Team XBMC
 *      http://www.xbmc.org
 *      Copyright (C) 2008-2009 Andrej Stepanchuk
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/times.h>
#include <time.h>
#include <errno.h>
#include <boost/regex.hpp>

#include "rtmp.h"
#include "AMFObject.h"
#include "log.h"

#define RTMP_SIG_SIZE 1536
#define RTMP_LARGE_HEADER_SIZE 12

#define RTMP_BUFFER_CACHE_SIZE (16*1024)

using namespace RTMP_LIB;
using namespace std;

static const int packetSize[] = { 12, 8, 4, 1 };
#define RTMP_PACKET_SIZE_LARGE    0
#define RTMP_PACKET_SIZE_MEDIUM   1
#define RTMP_PACKET_SIZE_SMALL    2
#define RTMP_PACKET_SIZE_MINIMUM  3

clock_t GetTime()
{
	struct tms t;
	return times(&t);
}

CRTMP::CRTMP() : m_socket(0)
{
  Close();
  m_pBuffer = new char[RTMP_BUFFER_CACHE_SIZE];
  m_nBufferMS = 300;
  m_fDuration = 0;
}

CRTMP::~CRTMP()
{
  Close();
  delete [] m_pBuffer;
}

double CRTMP::GetDuration() { return m_fDuration; }
bool CRTMP::IsConnected() { return m_socket != 0; }

void CRTMP::SetBufferMS(int size)
{
  m_nBufferMS = size;
}
//*
void CRTMP::UpdateBufferMS()
{
  SendPing(3, 1, m_nBufferMS);
}
//*/
bool CRTMP::Connect(char *url, char *tcUrl, char *player, char *pageUrl, char *app, char *auth, char *flashVer, double dTime)
{
  // check url formatting
  boost::regex re("^rtmp:\\/\\/[a-zA-Z0-9_\\.\\-]+((:(\\d)+\\/)|\\/)(([0-9a-zA-Z_:;\\+\\-\\.\\!\\\"\\$\\%\\&\\/\\(\\)\\=\\?\\<\\>\\s]*)$|$)");

  if (!boost::regex_match(url, re))
  {
  	Log(LOGERROR, "RTMP Connect: invalid url!");
	return false;
  }

  Link.url = url;
  Link.tcUrl = tcUrl;
  Link.player = player;
  Link.pageUrl = pageUrl;
  Link.app = app;
  Link.auth = auth;
  Link.flashVer = flashVer;
  Link.seekTime = dTime;

  boost::cmatch matches;

  boost::regex re1("^rtmp:\\/\\/([a-zA-Z0-9_\\.\\-]+)((:([0-9]+)\\/)|\\/)[0-9a-zA-Z_:;\\+\\-\\.\\!\\\"\\$\\%\\&\\/\\(\\)\\=\\?\\<\\>\\s]+");
  if(!boost::regex_match(url, matches, re1))
  {
  	Log(LOGERROR, "RTMP Connect: Regex for url doesn't match (error in programme)!");
	return false;
  }
  /*for(int i=0; i<matches.size(); i++) {
  	Log(LOGDEBUG, "matches[%d]: %s, %s", i, matches[i].first, matches[i].second);
  }*/

  if(matches[1].second-matches[1].first > 255) {
  	Log(LOGERROR, "Hostname must not be longer than 255 characters!");
	return false;
  }
  strncpy(Link.hostname, matches[1].first, matches[1].second-matches[1].first);
  Link.hostname[matches[1].second-matches[1].first]=0x00;

  Log(LOGDEBUG, "Hostname: %s", Link.hostname);

  char portstr[6];
  if(matches[4].second-matches[4].first > 5) {
          Log(LOGERROR, "Port must not be longer than 5 digits!");
	  return false;
  }
  strncpy(portstr, matches[4].first, matches[4].second-matches[4].first);
  portstr[matches[4].second-matches[4].first]=0x00;

  Link.port = atoi(portstr);

  if (Link.port == 0)
    Link.port = 1935;
  
  Log(LOGDEBUG, "Port: %d", Link.port);

  // obtain auth string if available
  /*
  boost::regex re2("^.*auth\\=([0-9a-zA-Z_:;\\-\\.\\!\\\"\\$\\%\\/\\(\\)\\=\\s]+)((&.+$)|$)");
  boost::cmatch matches2;
  
  if(boost::regex_match(url, matches2, re2)) {
    int len = matches2[1].second-matches2[1].first;
    if(len > 255) {
    	Log(LOGERROR, "Auth string must not be longer than 255 characters!");
    }
    Link.auth = (char *)malloc((len+1)*sizeof(char));
    strncpy(Link.auth, matches2[1].first, len);
    Link.auth[len]=0;

    Log(LOGDEBUG, "Auth: %s", Link.auth);
  } else { Link.auth = 0; }
  //*/

  Close();

  sockaddr_in service;
  memset(&service, 0, sizeof(sockaddr_in));
  service.sin_family = AF_INET;
  service.sin_addr.s_addr = inet_addr(Link.hostname);
  if (service.sin_addr.s_addr == INADDR_NONE)
  {
    struct hostent *host = gethostbyname(Link.hostname);
    if (host == NULL || host->h_addr == NULL)
    {
      Log(LOGERROR, "Problem accessing the DNS. (addr: %s)", Link.hostname);
      return false;
    }
    service.sin_addr = *(struct in_addr*)host->h_addr;
  }

  service.sin_port = htons(Link.port);
  m_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (m_socket != 0 )
  {
    if (connect(m_socket, (sockaddr*) &service, sizeof(struct sockaddr)) < 0)
    {
      Log(LOGERROR, "%s, failed to connect.", __FUNCTION__);
      Close();
      return false;
    }

    Log(LOGDEBUG, "connected, hand shake:");
    if (!HandShake())
    {
      Log(LOGERROR, "%s, handshake failed.", __FUNCTION__);
      Close();
      return false;
    }

    Log(LOGDEBUG, "handshaked");
    if (!Connect())
    {
      Log(LOGERROR, "%s, connect failed.", __FUNCTION__);
      Close();
      return false;
    }
    // set timeout
      struct timeval tv;
      memset(&tv, 0, sizeof(tv));
      tv.tv_sec = 300;
      if (setsockopt(m_socket, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv,  sizeof(tv)))
      {
      	Log(LOGERROR,"Setting timeout failed!");
      }
  }
  else
  {
    Log(LOGERROR, "%s, failed to create socket.", __FUNCTION__);
    return false;
  }

  return true;
}

bool CRTMP::GetNextMediaPacket(RTMPPacket &packet)
{
  bool bHasMediaPacket = false;
  while (!bHasMediaPacket && IsConnected() && ReadPacket(packet))
  {
    if (!packet.IsReady())
    {
      packet.FreePacket();
      usleep(5000); // 5ms
      //sleep(0.1);//30);
      continue;
    }

    switch (packet.m_packetType)
    {
      case 0x01:
        // chunk size
        HandleChangeChunkSize(packet);
        break;

      case 0x03:
        // bytes read report
        Log(LOGDEBUG, "%s, received: bytes read report", __FUNCTION__);
        break;

      case 0x04:
        // ping
        HandlePing(packet);
        break;

      case 0x05:
        // server bw
        Log(LOGDEBUG, "%s, received: server BW", __FUNCTION__);
        break;

      case 0x06:
        // client bw
        Log(LOGDEBUG, "%s, received: client BW", __FUNCTION__);
        break;

      case 0x08:
        // audio data
        //Log(LOGDEBUG, "%s, received: audio %lu bytes", __FUNCTION__, packet.m_nBodySize);
        HandleAudio(packet);
        bHasMediaPacket = true;
        break;

      case 0x09:
        // video data
        //Log(LOGDEBUG, "%s, received: video %lu bytes", __FUNCTION__, packet.m_nBodySize);
        HandleVideo(packet);
        bHasMediaPacket = true;
        break;

      case 0x12:
        // metadata (notify)
        Log(LOGDEBUG, "%s, received: notify", __FUNCTION__);
        HandleMetadata(packet.m_body, packet.m_nBodySize);
        bHasMediaPacket = true;
        break;

      case 0x14:
        // invoke
	Log(LOGDEBUG, "%s, received: invoke", __FUNCTION__);
        HandleInvoke(packet);
        break;

      case 0x16:
      	// ok, this might be a meta data packet as well, so check!
	if(packet.m_body[0] == 0x12) {
		HandleMetadata(packet.m_body+11, packet.m_nBodySize-11);
	}
        // FLV tag(s)
        //Log(LOGDEBUG, "%s, received: FLV tag(s) %lu bytes", __FUNCTION__, packet.m_nBodySize);
        bHasMediaPacket = true;
        break;

      default:
        Log(LOGDEBUG, "unknown packet type received: 0x%02x", packet.m_packetType);
    }

    if (!bHasMediaPacket) { 
      packet.FreePacket();
    }
  }
        
  if (bHasMediaPacket)
    m_bPlaying = true;

  return bHasMediaPacket;
}

int CRTMP::ReadN(char *buffer, int n)
{
  int nOriginalSize = n;
  memset(buffer, 0, n);
  char *ptr = buffer;
  while (n > 0)
  {
    int nBytes = 0;
    // for dumping we don't need buffering, so we won't get stuck in the end!
    /*if (m_bPlaying)
    {
      if (m_nBufferSize < n)
        FillBuffer();

      int nRead = ((n<m_nBufferSize)?n:m_nBufferSize);
      if (nRead > 0)
      {
        memcpy(buffer, m_pBuffer, nRead);
        memmove(m_pBuffer, m_pBuffer + nRead, m_nBufferSize - nRead); // crunch buffer
        m_nBufferSize -= nRead;
        nBytes = nRead;
        m_nBytesIn += nRead;
        if (m_nBytesIn > m_nBytesInSent + (600*1024) ) // report every 600K
          SendBytesReceived();
      }
    }
    else*/
      nBytes = recv(m_socket, ptr, n, 0);

    if(m_bPlaying) {
        m_nBytesIn += nBytes;
	if (m_nBytesIn > m_nBytesInSent + (600*1024) ) // report every 600K
                  SendBytesReceived();
    }
 
    if (nBytes == -1)
    {
      Log(LOGERROR, "%s, RTMP recv error %d", __FUNCTION__, errno);
      Close();
      return false;
    }
    
    if (nBytes == 0)
    {
      Log(LOGDEBUG, "%s, RTMP socket closed by server", __FUNCTION__);
      Close();
      break;
    }
    
    n -= nBytes;
    ptr += nBytes;
  }

  return nOriginalSize - n;
}

bool CRTMP::WriteN(const char *buffer, int n)
{
  const char *ptr = buffer;
  while (n > 0)
  {
    int nBytes = send(m_socket, ptr, n, 0);
    if (nBytes < 0)
    {
      Log(LOGERROR, "%s, RTMP send error %d (%d bytes)", __FUNCTION__, errno, n);
      Close();
      return false;
    }
    
    if (nBytes == 0)
      break;
    
    n -= nBytes;
    ptr += nBytes;
  }

  return n == 0;
}

bool CRTMP::Connect()
{
  if (!SendConnectPacket())
  {
    Log(LOGERROR, "%s, failed to send connect RTMP packet", __FUNCTION__);
    return false;
  }

  return true;
}

bool CRTMP::SendConnectPacket()
{
  RTMPPacket packet;
  packet.m_nChannel = 0x03;   // control channel (invoke)
  packet.m_headerType = RTMP_PACKET_SIZE_LARGE;
  packet.m_packetType = 0x14; // INVOKE
  packet.AllocPacket(4096);

  char *enc = packet.m_body;
  enc += EncodeString(enc, "connect");
  enc += EncodeNumber(enc, 1.0);
  *enc = 0x03; //Object Datatype
  enc++;
 
  if(Link.app)
  	enc += EncodeString(enc, "app", Link.app);
  if(Link.flashVer)
  	enc += EncodeString(enc, "flashVer", Link.flashVer);
  if(Link.player)
 	enc += EncodeString(enc, "swfUrl", Link.player);
  if(Link.tcUrl)
  	enc += EncodeString(enc, "tcUrl", Link.tcUrl);
  
  enc += EncodeBoolean(enc, "fpad", false);
  enc += EncodeNumber(enc, "capabilities", 15.0);
  enc += EncodeNumber(enc, "audioCodecs", 1639.0);
  enc += EncodeNumber(enc, "videoCodecs", 252.0);
  enc += EncodeNumber(enc, "videoFunction", 1.0);
  if(Link.pageUrl)
  	enc += EncodeString(enc, "pageUrl", Link.pageUrl);  
  enc += 2; // end of object - 0x00 0x00 0x09
  *enc = 0x09;
  enc++;
  
  // add auth string
  if(Link.auth)
  {
  	*enc = 0x01; enc++;
  	*enc = 0x01; enc++;

  	enc += EncodeString(enc, Link.auth);
  }
  packet.m_nBodySize = enc-packet.m_body;

  return SendRTMP(packet);
}

bool CRTMP::SendCreateStream(double dStreamId)
{
  RTMPPacket packet;
  packet.m_nChannel = 0x03;   // control channel (invoke)
  packet.m_headerType = RTMP_PACKET_SIZE_MEDIUM;
  packet.m_packetType = 0x14; // INVOKE

  packet.AllocPacket(256); // should be enough
  char *enc = packet.m_body;
  enc += EncodeString(enc, "createStream");
  enc += EncodeNumber(enc, dStreamId);
  *enc = 0x05; // NULL
  enc++;

  packet.m_nBodySize = enc - packet.m_body;

  return SendRTMP(packet);
}

bool CRTMP::SendPause()
{
  RTMPPacket packet;
  packet.m_nChannel = 0x08;   // video channel 
  packet.m_headerType = RTMP_PACKET_SIZE_MEDIUM;
  packet.m_packetType = 0x14; // invoke

  packet.AllocPacket(256); // should be enough
  char *enc = packet.m_body;
  enc += EncodeString(enc, "pause");
  enc += EncodeNumber(enc, 0);
  *enc = 0x05; // NULL
  enc++;
  enc += EncodeBoolean(enc, true);
  enc += EncodeNumber(enc, 0);

  packet.m_nBodySize = enc - packet.m_body;

  return SendRTMP(packet);
}

bool CRTMP::SendSeek(double dTime)
{
  RTMPPacket packet;
  packet.m_nChannel = 0x08;   // video channel 
  packet.m_headerType = RTMP_PACKET_SIZE_MEDIUM;
  packet.m_packetType = 0x14; // invoke

  packet.AllocPacket(256); // should be enough
  char *enc = packet.m_body;
  enc += EncodeString(enc, "seek");
  enc += EncodeNumber(enc, 0);
  *enc = 0x05; // NULL
  enc++;
  enc += EncodeNumber(enc, dTime);

  packet.m_nBodySize = enc - packet.m_body;

  return SendRTMP(packet);
}

bool CRTMP::SendServerBW()
{
  RTMPPacket packet;
  packet.m_nChannel = 0x02;   // control channel (invoke)
  packet.m_headerType = RTMP_PACKET_SIZE_LARGE;
  packet.m_packetType = 0x05; // Server BW

  packet.AllocPacket(4);
  packet.m_nBodySize = 4;

  EncodeInt32(packet.m_body, 0x001312d0); // hard coded for now
  return SendRTMP(packet);
}

bool CRTMP::SendBytesReceived()
{
  RTMPPacket packet;
  packet.m_nChannel = 0x02;   // control channel (invoke)
  packet.m_headerType = RTMP_PACKET_SIZE_MEDIUM;
  packet.m_packetType = 0x03; // bytes in

  packet.AllocPacket(4);
  packet.m_nBodySize = 4;

  EncodeInt32(packet.m_body, m_nBytesIn); // hard coded for now
  m_nBytesInSent = m_nBytesIn;

  //Log(LOGDEBUG, "Send bytes report. 0x%x (%d bytes)", (unsigned int)m_nBytesIn, m_nBytesIn);
  return SendRTMP(packet);
}

bool CRTMP::SendCheckBW()
{
  RTMPPacket packet;
  packet.m_nChannel = 0x03;   // control channel (invoke)
  packet.m_headerType = RTMP_PACKET_SIZE_LARGE;
  packet.m_packetType = 0x14; // INVOKE
  packet.m_nInfoField1 = GetTime();

  packet.AllocPacket(256); // should be enough
  char *enc = packet.m_body;
  enc += EncodeString(enc, "_checkbw");
  enc += EncodeNumber(enc, 0x00);
  *enc = 0x05; // NULL
  enc++;

  packet.m_nBodySize = enc - packet.m_body;

  return SendRTMP(packet);
}

bool CRTMP::SendCheckBWResult()
{
  RTMPPacket packet;
  packet.m_nChannel = 0x03;   // control channel (invoke)
  packet.m_headerType = RTMP_PACKET_SIZE_MEDIUM;
  packet.m_packetType = 0x14; // INVOKE
  packet.m_nInfoField1 = 0x16 * m_nBWCheckCounter; // temp inc value. till we figure it out.

  packet.AllocPacket(256); // should be enough
  char *enc = packet.m_body;
  enc += EncodeString(enc, "_result");
  enc += EncodeNumber(enc, (double)time(NULL)); // temp
  *enc = 0x05; // NULL
  enc++;
  enc += EncodeNumber(enc, (double)m_nBWCheckCounter++); 

  packet.m_nBodySize = enc - packet.m_body;

  return SendRTMP(packet);
}

bool CRTMP::SendPlay()
{
  RTMPPacket packet;
  packet.m_nChannel = 0x08;   // we make 8 our stream channel
  packet.m_headerType = RTMP_PACKET_SIZE_LARGE;
  packet.m_packetType = 0x14; // INVOKE
  packet.m_nInfoField2 = 0x01000000;

  packet.AllocPacket(256); // should be enough
  char *enc = packet.m_body;
  enc += EncodeString(enc, "play");
  enc += EncodeNumber(enc, 0.0);
  *enc = 0x05; // NULL
  enc++;
  // use m_strPlayPath
  std::string strPlay;// = m_strPlayPath;
  //if (strPlay.empty())
  //{
    // or use slist parameter, if there is one
    std::string url = std::string(Link.url);
    int nPos = url.find("slist=");
    if (nPos > 0)
      strPlay = url.substr(nPos+6, url.size()-(nPos+6)); //Mid(nPos + 6);
		
		if (strPlay.empty())
		{
			// or use last piece of URL, if there's more than one level
			std::string::size_type pos_slash = url.find_last_of("/");
			if ( pos_slash != std::string::npos )
				strPlay = url.substr(pos_slash+1, url.size()-(pos_slash+1)); //Mid(pos_slash+1);
		}

		if (strPlay.empty()){
			Log(LOGERROR, "%s, no name to play!", __FUNCTION__);
			return false;
		}
  //}

  Log(LOGDEBUG, "Sending play: %s", strPlay.c_str());
  enc += EncodeString(enc, strPlay.c_str());
  enc += EncodeNumber(enc, 0.0);

  packet.m_nBodySize = enc - packet.m_body;

  return SendRTMP(packet);
}

bool bSeekedSuccessfully = false;

bool CRTMP::SendPing(short nType, unsigned int nObject, unsigned int nTime)
{
  Log(LOGDEBUG, "sending ping. type: 0x%04x", (unsigned short)nType);

  RTMPPacket packet; 
  packet.m_nChannel = 0x02;   // control channel (ping)
  packet.m_headerType = RTMP_PACKET_SIZE_MEDIUM;
  packet.m_packetType = 0x04; // ping
  packet.m_nInfoField1 = GetTime();

  int nSize = (nType==0x03?10:6); // type 3 is the buffer time and requires all 3 parameters. all in all 10 bytes.
  packet.AllocPacket(nSize);
  packet.m_nBodySize = nSize;

  char *buf = packet.m_body;
  buf += EncodeInt16(buf, nType);

  if (nSize > 2)
    buf += EncodeInt32(buf, nObject);

  if (nSize > 6)
    buf += EncodeInt32(buf, nTime);

  return SendRTMP(packet);
}

void CRTMP::HandleInvoke(const RTMPPacket &packet)
{
  if (packet.m_body[0] != 0x02) // make sure it is a string method name we start with
  {
    Log(LOGWARNING, "%s, Sanity failed. no string method in invoke packet", __FUNCTION__);
    return;
  }

  RTMP_LIB::AMFObject obj;
  int nRes = obj.Decode(packet.m_body, packet.m_nBodySize);
  if (nRes < 0)
  { 
    Log(LOGERROR, "%s, error decoding invoke packet", __FUNCTION__);
    return;
  }

  obj.Dump();
  std::string method = obj.GetProperty(0).GetString();
  Log(LOGDEBUG, "%s, server invoking <%s>", __FUNCTION__, method.c_str());

  if (method == "_result")
  {
    std::string methodInvoked = m_methodCalls[0];
    m_methodCalls.erase(m_methodCalls.begin());

    Log(LOGDEBUG, "%s, received result for method call <%s>", __FUNCTION__, methodInvoked.c_str());
  
    if (methodInvoked == "connect")
    {
      SendServerBW();
      SendPing(3, 0, 300);
      SendCreateStream(2.0);
    }
    else if (methodInvoked == "createStream")
    {
      SendPlay();
      if(Link.seekTime > 0) {
      	Log(LOGDEBUG, "%s, sending seek: %f ms", __FUNCTION__, Link.seekTime);
	SendSeek(Link.seekTime);
      }
	
      SendPing(3, 1, m_nBufferMS);
    }
    else if (methodInvoked == "play")
    {
    }
  }
  else if (method == "onBWDone")
  {
    //SendCheckBW();
  }
  else if (method == "_onbwcheck")
  {
    SendCheckBWResult();
  }
  else if (method == "_error")
  {
    Log(LOGERROR, "rtmp server sent error");
  }
  else if (method == "close")
  {
    Log(LOGERROR, "rtmp server requested close");
    Close();
  }
  else if (method == "onStatus")
  {
    std::string code  = obj.GetProperty(3).GetObject().GetProperty("code").GetString();
    std::string level = obj.GetProperty(3).GetObject().GetProperty("level").GetString();

    Log(LOGDEBUG, "%s, onStatus: %s", __FUNCTION__, code.c_str() );
    if (code == "NetStream.Failed"
    ||  code == "NetStream.Play.Failed"
    ||  code == "NetStream.Play.Stop")
      Close();

    /*if(Link.seekTime > 0) {
    	if(code == "NetStream.Seek.Notify") { // seeked successfully, can play now!
    		bSeekedSuccessfully = true;
    	} else if(code == "NetStream.Play.Start" && !bSeekedSuccessfully) { // well, try to seek again
		Log(LOGWARNING, "%s, server ignored seek!", __FUNCTION__);
    	}
    }*/
  }
  else
  {

  }
}

//int pnum=0;

bool CRTMP::FindFirstMatchingProperty(AMFObject &obj, std::string name, AMFObjectProperty &p)
{
	// this is a small object search to locate the "duration" property
	for (int n=0; n<obj.GetPropertyCount(); n++) {
		AMFObjectProperty prop = obj.GetProperty(n);

		if(prop.GetPropName() == name) {
			
			p = obj.GetProperty(n);
			return true;
		}

		if(prop.GetType() == AMF_OBJECT) {
			AMFObject next = prop.GetObject();
			return FindFirstMatchingProperty(next, name, p);
		}
	}
	return false;
}

void CRTMP::HandleMetadata(char *body, unsigned int len)
{
	/*Log(LOGDEBUG,"Parsing meta data: %d @0x%08X", packet.m_nBodySize, packet.m_body);
	for(int i=0; i<packet.m_nBodySize; i++) {
		printf("%02X ", packet.m_body[i]);
	}
	printf("\n");

	char str[256]={0};
	sprintf(str, "packet%d", pnum);
	pnum++;
	FILE *f = fopen(str, "wb");
	fwrite(packet.m_body, 1, packet.m_nBodySize, f);
	fclose(f);//*/

	// allright we get some info here, so parse it and print it
	// also keep duration or filesize to make a nice progress bar

	//int len = packet.m_nBodySize;
	//char *p = packet.m_body;

	RTMP_LIB::AMFObject obj;
	int nRes = obj.Decode(body, len);
	if(nRes < 0) {
		Log(LOGERROR, "%s, error decoding meta data packet", __FUNCTION__);
		return;
	}

	obj.Dump();
  	std::string metastring = obj.GetProperty(0).GetString();

	if(metastring == "onMetaData") {
		AMFObjectProperty prop;
		if(FindFirstMatchingProperty(obj, "duration", prop)) {
			m_fDuration = prop.GetNumber();
			Log(LOGDEBUG, "Set duration: %f", m_fDuration);
		}
	}
}

void CRTMP::HandleChangeChunkSize(const RTMPPacket &packet)
{
  if (packet.m_nBodySize >= 4)
  {
    m_chunkSize = ReadInt32(packet.m_body);
    Log(LOGDEBUG, "%s, received: chunk size change to %d", __FUNCTION__, m_chunkSize);
  }
}

void CRTMP::HandleAudio(const RTMPPacket &packet)
{
}

void CRTMP::HandleVideo(const RTMPPacket &packet)
{
}

void CRTMP::HandlePing(const RTMPPacket &packet)
{
  short nType = -1;
  if (packet.m_body && packet.m_nBodySize >= sizeof(short))
    nType = ReadInt16(packet.m_body);
  Log(LOGDEBUG, "server sent ping. type: %d", nType);

  if (nType == 0x06 && packet.m_nBodySize >= 6) // server ping. reply with pong.
  {
    unsigned int nTime = ReadInt32(packet.m_body + sizeof(short));
    SendPing(0x07, nTime);
  }
}

bool CRTMP::ReadPacket(RTMPPacket &packet)
{
  char type;
  if (ReadN(&type,1) != 1)
  {
    Log(LOGERROR, "%s, failed to read RTMP packet header", __FUNCTION__);
    return false;
  } 

  packet.m_headerType = (type & 0xc0) >> 6;
  packet.m_nChannel = (type & 0x3f);

  int nSize = packetSize[packet.m_headerType];
  
//  Log(LOGDEBUG, "%s, reading RTMP packet chunk on channel %x, headersz %i", __FUNCTION__, packet.m_nChannel, nSize);

  if (nSize < RTMP_LARGE_HEADER_SIZE) // using values from the last message of this channel
    packet = m_vecChannelsIn[packet.m_nChannel];
  
  nSize--;

  char header[RTMP_LARGE_HEADER_SIZE] = {0};
  if (nSize > 0 && ReadN(header,nSize) != nSize)
  {
    Log(LOGERROR, "%s, failed to read RTMP packet header. type: %x", __FUNCTION__, (unsigned int)type);
    return false;
  }

  if (nSize >= 3)
    packet.m_nInfoField1 = ReadInt24(header);

  if (nSize >= 6)
  {
    packet.m_nBodySize = ReadInt24(header + 3);
    packet.m_nBytesRead = 0;
    packet.FreePacketHeader(); // new packet body
  }
  
  if (nSize > 6)
    packet.m_packetType = header[6];

  if (nSize == 11)
    packet.m_nInfoField2 = ReadInt32(header+7);

  if (packet.m_nBodySize > 0 && packet.m_body == NULL && !packet.AllocPacket(packet.m_nBodySize))
  {
    Log(LOGDEBUG, "%s, failed to allocate packet", __FUNCTION__);
    return false;
  }

  int nToRead = packet.m_nBodySize - packet.m_nBytesRead;
  int nChunk = m_chunkSize;
  if (nToRead < nChunk)
     nChunk = nToRead;

  if (ReadN(packet.m_body + packet.m_nBytesRead, nChunk) != nChunk)
  {
    Log(LOGERROR, "%s, failed to read RTMP packet body. len: %lu", __FUNCTION__, packet.m_nBodySize);
    packet.m_body = NULL; // we dont want it deleted since its pointed to from the stored packets (m_vecChannelsIn)
    return false;  
  }

  packet.m_nBytesRead += nChunk;

  // keep the packet as ref for other packets on this channel
  m_vecChannelsIn[packet.m_nChannel] = packet;

  if (packet.IsReady())
  {
    // reset the data from the stored packet. we keep the header since we may use it later if a new packet for this channel
    // arrives and requests to re-use some info (small packet header)
    m_vecChannelsIn[packet.m_nChannel].m_body = NULL;
    m_vecChannelsIn[packet.m_nChannel].m_nBytesRead = 0;
  }
  else
    packet.m_body = NULL; // so it wont be erased on "free"

  return true;
}

short  CRTMP::ReadInt16(const char *data)
{
  short val;
  memcpy(&val,data,sizeof(short));
  return ntohs(val);
}

int  CRTMP::ReadInt24(const char *data)
{
  char tmp[4] = {0};
  memcpy(tmp+1, data, 3);
  int val;
  memcpy(&val, tmp, sizeof(int));
  return ntohl(val);
}

int  CRTMP::ReadInt32(const char *data)
{
  int val;
  memcpy(&val, data, sizeof(int));
  return ntohl(val);
}

std::string CRTMP::ReadString(const char *data)
{
  std::string strRes;
  short len = ReadInt16(data);
  if (len > 0)
  {
    char *pStr = new char[len+1]; 
    memset(pStr, 0, len+1);
    memcpy(pStr, data + sizeof(short), len);
    strRes = pStr;
    delete [] pStr;
  }
  return strRes;
}

bool CRTMP::ReadBool(const char *data)
{
  return *data == 0x01;
}

double CRTMP::ReadNumber(const char *data)
{
  double val;
  char *dPtr = (char *)&val;
  for (int i=7;i>=0;i--)
  {
    *dPtr = data[i];
    dPtr++;
  }

  return val;
}

int CRTMP::EncodeString(char *output, const std::string &strName, const std::string &strValue)
{
  char *buf = output;
  short length = htons(strName.size());
  memcpy(buf, &length, 2);
  buf += 2;

  memcpy(buf, strName.c_str(), strName.size());
  buf += strName.size();
  
  buf += EncodeString(buf, strValue);
  return buf - output;
}

int CRTMP::EncodeInt16(char *output, short nVal)
{
  nVal = htons(nVal);
  memcpy(output, &nVal, sizeof(short));
  return sizeof(short);
}

int CRTMP::EncodeInt24(char *output, int nVal)
{
  nVal = htonl(nVal);
  char *ptr = (char *)&nVal;
  ptr++;
  memcpy(output, ptr, 3);
  return 3;
}

int CRTMP::EncodeInt32(char *output, int nVal)
{
  nVal = htonl(nVal);
  memcpy(output, &nVal, sizeof(int));
  return sizeof(int);
}

int CRTMP::EncodeNumber(char *output, const std::string &strName, double dVal)
{
  char *buf = output;

  unsigned short length = htons(strName.size());
  memcpy(buf, &length, 2);
  buf += 2;

  memcpy(buf, strName.c_str(), strName.size());
  buf += strName.size();

  buf += EncodeNumber(buf, dVal);
  return buf - output;
}

int CRTMP::EncodeBoolean(char *output, const std::string &strName, bool bVal)
{
  char *buf = output;
  unsigned short length = htons(strName.size());
  memcpy(buf, &length, 2);
  buf += 2;

  memcpy(buf, strName.c_str(), strName.size());
  buf += strName.size();

  buf += EncodeBoolean(buf, bVal);

  return buf - output;
}

int CRTMP::EncodeString(char *output, const std::string &strValue)
{
  char *buf = output;
  *buf = 0x02; // Datatype: String
  buf++;

  short length = htons(strValue.size());
  memcpy(buf, &length, 2);
  buf += 2;

  memcpy(buf, strValue.c_str(), strValue.size());
  buf += strValue.size();

  return buf - output;
}

int CRTMP::EncodeNumber(char *output, double dVal)
{
  char *buf = output;  
  *buf = 0x00; // type: Number
  buf++;

  char *dPtr = (char *)&dVal;
  for (int i=7;i>=0;i--)
  {
    buf[i] = *dPtr;
    dPtr++;
  }

  buf += 8;

  return buf - output;
}

int CRTMP::EncodeBoolean(char *output, bool bVal)
{
  char *buf = output;  

  *buf = 0x01; // type: Boolean
  buf++;

  *buf = bVal?0x01:0x00; 
  buf++;

  return buf - output;
}

bool CRTMP::HandShake()
{
  char clientsig[RTMP_SIG_SIZE+1];
  char serversig[RTMP_SIG_SIZE];

  //Log(LOGDEBUG, "HandShake: ");
  clientsig[0] = 0x3;
  uint32_t uptime = htonl(GetTime());
  memcpy(clientsig + 1, &uptime, 4);
  memset(clientsig + 5, 0, 4);

  for (int i=9; i<=RTMP_SIG_SIZE; i++)
    clientsig[i] = (char)(rand() % 256);

  if (!WriteN(clientsig, RTMP_SIG_SIZE + 1))
    return false;

  char dummy;
  if (ReadN(&dummy, 1) != 1) // 0x03
    return false;

  
  if (ReadN(serversig, RTMP_SIG_SIZE) != RTMP_SIG_SIZE)
    return false;

  char resp[RTMP_SIG_SIZE];
  if (ReadN(resp, RTMP_SIG_SIZE) != RTMP_SIG_SIZE)
    return false;

  bool bMatch = (memcmp(resp, clientsig + 1, RTMP_SIG_SIZE) == 0);
  if (!bMatch)
  {
    Log(LOGWARNING, "%s, client signiture does not match!",__FUNCTION__);
  }

  if (!WriteN(serversig, RTMP_SIG_SIZE))
    return false;

  return true;
}

bool CRTMP::SendRTMP(RTMPPacket &packet)
{
  const RTMPPacket &prevPacket = m_vecChannelsOut[packet.m_nChannel];
  if (packet.m_headerType != RTMP_PACKET_SIZE_LARGE)
  {
    // compress a bit by using the prev packet's attributes
    if (prevPacket.m_nBodySize == packet.m_nBodySize && packet.m_headerType == RTMP_PACKET_SIZE_MEDIUM) 
      packet.m_headerType = RTMP_PACKET_SIZE_SMALL;

    if (prevPacket.m_nInfoField2 == packet.m_nInfoField2 && packet.m_headerType == RTMP_PACKET_SIZE_SMALL)
      packet.m_headerType = RTMP_PACKET_SIZE_MINIMUM;
      
  }

  if (packet.m_headerType > 3) // sanity
  { 
    Log(LOGERROR, "sanity failed!! tring to send header of type: 0x%02x.", (unsigned char)packet.m_headerType);
    return false;
  }

  int nSize = packetSize[packet.m_headerType];
  char header[RTMP_LARGE_HEADER_SIZE] = { 0 };
  header[0] = (char)((packet.m_headerType << 6) | packet.m_nChannel);
  if (nSize > 1)
    EncodeInt24(header+1, packet.m_nInfoField1);
  
  if (nSize > 4)
  {
    EncodeInt24(header+4, packet.m_nBodySize);
    header[7] = packet.m_packetType;
  }

  if (nSize > 8)
    EncodeInt32(header+8, packet.m_nInfoField2);

  if (!WriteN(header, nSize))
  {
    Log(LOGWARNING, "couldnt send rtmp header");
    return false;
  }

  nSize = packet.m_nBodySize;
  char *buffer = packet.m_body;

  while (nSize)
  {
    int nChunkSize = packet.m_packetType == 0x14?m_chunkSize:packet.m_nBodySize;
    if (nSize < m_chunkSize)
      nChunkSize = nSize;

    if (!WriteN(buffer, nChunkSize))
      return false;

    nSize -= nChunkSize;
    buffer += nChunkSize;

    if (nSize > 0)
    {
      char sep = (0xc0 | packet.m_nChannel);
      if (!WriteN(&sep, 1))
        return false;  
    }
  }

  if (packet.m_packetType == 0x14) // we invoked a remote method, keep it in call queue till result arrives
    m_methodCalls.push_back(ReadString(packet.m_body + 1));

  m_vecChannelsOut[packet.m_nChannel] = packet;
  m_vecChannelsOut[packet.m_nChannel].m_body = NULL;
  return true;
}

void CRTMP::Close()
{
  if (IsConnected())
    close(m_socket);

  m_socket = 0;
  m_chunkSize = 128;
  m_nBWCheckCounter = 0;
  m_nBytesIn = 0;
  m_nBytesInSent = 0;

  for (int i=0; i<64; i++)
  {
    m_vecChannelsIn[i].Reset();
    m_vecChannelsIn[i].m_nChannel = i;
    m_vecChannelsOut[i].Reset();
    m_vecChannelsOut[i].m_nChannel = i;
  }

  m_bPlaying = false;
  m_nBufferSize = 0;
}

bool CRTMP::FillBuffer()
{
  time_t now = time(NULL);
  while (m_nBufferSize < RTMP_BUFFER_CACHE_SIZE && time(NULL) - now < 4)
  {
    int nBytes = recv(m_socket, m_pBuffer + m_nBufferSize, RTMP_BUFFER_CACHE_SIZE - m_nBufferSize, 0);
    if (nBytes != -1)
      m_nBufferSize += nBytes;
    else
    {
      Log(LOGDEBUG, "%s, read buffer returned %d. errno: %d", __FUNCTION__, nBytes, errno);
      break;
    }
  }

  return true;
}
