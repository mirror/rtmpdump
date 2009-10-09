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

#include <stdlib.h>
#include <string.h>

#include <assert.h>

#include "log.h"
#include "parseurl.h"

#include "rtmp.h"
/*
#define RTMP_PROTOCOL_RTMP      0
#define RTMP_PROTOCOL_RTMPT     1 // not yet supported
#define RTMP_PROTOCOL_RTMPS     2 // not yet supported
#define RTMP_PROTOCOL_RTMPE     3 // not yet supported
#define RTMP_PROTOCOL_RTMPTE    4 // not yet supported
#define RTMP_PROTOCOL_RTMFP     5 // not yet supported
*/
char *str2lower(char *str, int len)
{
	char *res = (char *)malloc(len+1);
	char *p;

	for(p=res; p<res+len; p++, str++) {
		*p = tolower(*str);
	}

	*p = 0;

	return res;
}

bool IsUrlValid(char *url)
{
	return true;
	/*boost::regex re("^rtmp:\\/\\/[a-zA-Z0-9_\\.\\-]+((:(\\d)+\\/)|\\/)(([0-9a-zA-Z_:;\\+\\-\\.\\!\\\"\\$\\%\\&\\/\\(\\)\\=\\?\\<\\>\\s]*)$|$)");

  	if (!boost::regex_match(url, re))
        	return false;
	
	return true;*/
}

bool ParseUrl(char *url, int *protocol, char **host, unsigned int *port, char **playpath, char **app)
{
	assert(url != 0 && protocol != 0 && host != 0 && port != 0 && playpath != 0 && app != 0);

	Log(LOGDEBUG, "parsing...");

	*protocol = 0; // default: RTMP

	// Old School Parsing
	char *lw = str2lower(url, 6);

	char *p = strstr(url, "://");
	int len = (int)(p-url);
	if(p == 0) {
		Log(LOGWARNING, "RTMP URL: No :// in url!");
		free(lw);
		return false;
	}

	if(len == 4 && strncmp(lw, "rtmp", 4)==0)
		*protocol = RTMP_PROTOCOL_RTMP;
	else if(len == 5 && strncmp(lw, "rtmpt", 5)==0)
		*protocol = RTMP_PROTOCOL_RTMPT;
	else if(len == 5 && strncmp(lw, "rtmps", 5)==0)
	        *protocol = RTMP_PROTOCOL_RTMPS;
	else if(len == 5 && strncmp(lw, "rtmpe", 5)==0)
	        *protocol = RTMP_PROTOCOL_RTMPE;
	else if(len == 5 && strncmp(lw, "rtmfp", 5)==0)
	        *protocol = RTMP_PROTOCOL_RTMFP;
	else if(len == 6 && strncmp(lw, "rtmpte", 6)==0)
	        *protocol = RTMP_PROTOCOL_RTMPTE;
	else {
		Log(LOGWARNING, "Unknown protocol!\n");
		goto parsehost;
	}

	Log(LOGDEBUG, "Parsed protocol: %d", *protocol);

parsehost:
	free(lw);

	// lets get the hostname
	p+=3;

	char *temp;

	int iEnd   = strlen(p)-1;
	int iCol   = iEnd+1; 
	int iQues  = iEnd+1;
	int iSlash = iEnd+1;

	if((temp=strstr(p, ":"))!=0)
		iCol = temp-p;
	if((temp=strstr(p, "?"))!=0)
	        iQues = temp-p;
	if((temp=strstr(p, "/"))!=0)
	        iSlash = temp-p;

	int min = iSlash < iEnd ? iSlash : iEnd;
	min = iQues   < min ? iQues   : min;

	int hostlen = iCol < min ? iCol : min;

	if(min < 256) {
		*host = (char *)malloc((hostlen+1)*sizeof(char));
		strncpy(*host, p, hostlen);
		(*host)[hostlen]=0;

		Log(LOGDEBUG, "Parsed host    : %s", *host);
	} else {
		Log(LOGWARNING, "Hostname exceeds 255 characters!");
	}

	p+=hostlen; iEnd-=hostlen;

	// get the port number if available
	if(*p == ':') {
		p++; iEnd--;

		int portlen = min-hostlen-1;
		if(portlen < 6) {
			char portstr[6];
			strncpy(portstr,p,portlen);
			portstr[portlen]=0;

			*port = atoi(portstr);
			if(*port == 0)
				*port = 1935;

			Log(LOGDEBUG, "Parsed port    : %d", *port);
		} else {
			Log(LOGWARNING, "Port number is longer than 5 characters!");
		}

		p+=portlen; iEnd-=portlen;
	}

	if(*p != '/') {
		Log(LOGWARNING, "No application or playpath in URL!");
		return true;
	}
	p++; iEnd--;

	// parse application
	//
	// rtmp://host[:port]/app[/appinstance][/...]
	// application = app[/appinstance]
	int iSlash2 = iEnd+1; // 2nd slash
        int iSlash3 = iEnd+1; // 3rd slash

        if((temp=strstr(p, "/"))!=0)
        	iSlash2 = temp-p;
	
	if((temp=strstr(p, "?"))!=0)
	        iQues = temp-p;

	if(iSlash2 < iEnd)
		if((temp=strstr(p+iSlash2+1, "/"))!=0)
			iSlash3 = temp-p;

	//Log(LOGDEBUG, "p:%s, iEnd: %d\niSlash : %d\niSlash2: %d\niSlash3: %d", p, iEnd, iSlash, iSlash2, iSlash3);
	
	int applen = iEnd+1; // ondemand, pass all parameters as app
	int appnamelen = 8; // ondemand length

	if(iQues < iEnd) { // whatever it is, the '?' means we need to use everything as app
		appnamelen = iQues;
		applen = iEnd+1; // pass the parameters as well
	} 
	else if(strncmp(p, "ondemand/", 9)==0) {
                // app = ondemand/foobar, only pass app=ondemand
                applen = 8;
        }
	else { // app!=ondemand, so app is app[/appinstance]
		appnamelen = iSlash2 < iEnd ? iSlash2 : iEnd;
        	if(iSlash3 < iEnd)
                	appnamelen = iSlash3;
	
		applen = appnamelen;
	}

	*app = (char *)malloc((applen+1)*sizeof(char));
	strncpy(*app, p, applen);
	(*app)[applen]=0;
	Log(LOGDEBUG, "Parsed app     : %s", *app);

	p += appnamelen; 
	iEnd -= appnamelen;

	// parse playpath
	int iPlaypathPos = -1;
	int iPlaypathLen = -1;

	if(*p=='?' && (temp=strstr(p, "slist="))!=0) {
		iPlaypathPos = temp-p+6;

		int iAnd = iEnd+1;
		if((temp=strstr(p+iPlaypathPos, "&"))!=0)
			iAnd = temp-p;
		if(iAnd < iEnd)
			iPlaypathLen = iAnd-iPlaypathPos;
		else
			iPlaypathLen = iEnd-iPlaypathPos+1;
	} else { // no slist parameter, so take string after applen 
		if(iEnd > 0) {
			iPlaypathPos = 1;
			iPlaypathLen = iEnd-iPlaypathPos+1;
			
			// filter .flv from playpath specified with slashes: rtmp://host/app/path.flv
			if(iPlaypathLen >=4 && strncmp(&p[iPlaypathPos+iPlaypathLen-4], ".flv", 4)==0) {
				iPlaypathLen-=4;
			}
		} else {
			Log(LOGERROR, "No playpath found!");
		}
	}

	if(iPlaypathLen > -1) {
		*playpath = (char *)malloc((iPlaypathLen+1)*sizeof(char));
		strncpy(*playpath, &p[iPlaypathPos], iPlaypathLen);
		(*playpath)[iPlaypathLen]=0;

		Log(LOGDEBUG, "Parsed playpath: %s", *playpath);
	} else {
		Log(LOGWARNING, "No playpath in URL!");
	}

        return true;
	

	//boost::cmatch matches;
	/*boost::regex re1("^rtmp:\\/\\/([a-zA-Z0-9_\\.\\-]+)((:([0-9]+)\\/)|\\/)[0-9a-zA-Z_:;\\+\\-\\.\\!\\\"\\$\\%\\&\\/\\(\\)\\=\\?\\<\\>\\s]+");
	if(boost::regex_match(url, matches, re1))
	{
		if(matches[1].second-matches[1].first < 512) {
			*host = (char *)malloc(512*sizeof(char));
			memcpy(*host, matches[1].first, matches[1].second-matches[1].first);
			(*host)[matches[1].second-matches[1].first]=0x00;
			Log(LOGDEBUG, "Hostname: %s", *host);
		} else {
			Log(LOGWARNING, "Hostname too long: must not be longer than 255 characters!");
		}

		char portstr[6];
		if(matches[4].second-matches[4].first < 6) {
			strncpy(portstr, matches[4].first, matches[4].second-matches[4].first);
			portstr[matches[4].second-matches[4].first]=0x00;
			*port = atoi(portstr);
		} else {
			Log(LOGWARNING, "Port too long: must not be longer than 5 digits!");
		}
		
		std::string strPlay;
		// use slist parameter, if there is one
		std::string surl = std::string(url);
		int nPos = surl.find("slist=");
		if (nPos > 0)
			strPlay = surl.substr(nPos+6, surl.size()-(nPos+6));

        	if (strPlay.empty()) {
			// or use last piece of URL, if there's more than one level
			std::string::size_type pos_slash = surl.find_last_of("/");
			if ( pos_slash != std::string::npos )
				strPlay = surl.substr(pos_slash+1, surl.size()-(pos_slash+1));
		}

		if(!strPlay.empty()){
			*playpath = (char *)malloc(1024);
			if(strlen(strPlay.c_str()) < 1024)
				strcpy(*playpath, strPlay.c_str());
			else
				Log(LOGWARNING, "Playpath too long: must not be longer than 1023 characters!");
		}
		return true;
	}*/
	return false;
}

/*
 
boost::cmatch matches;

  boost::regex re1("^rtmp:\\/\\/([a-zA-Z0-9_\\.\\-]+)((:([0-9]+)\\/)|\\/)[0-9a-zA-Z_:;\\+\\-\\.\\!\\\"\\$\\%\\&\\/\\(\\)\\=\\?\\<\\>\\s]+");
  if(!boost::regex_match(url, matches, re1))
  {
        Log(LOGERROR, "RTMP Connect: Regex for url doesn't match (error in programme)!");
        return false;
  }
  //for(int i=0; i<matches.size(); i++) {
  //      Log(LOGDEBUG, "matches[%d]: %s, %s", i, matches[i].first, matches[i].second);
  //}

  if(matches[1].second-matches[1].first > 255) {
        Log(LOGERROR, "Hostname must not be longer than 255 characters!");
        return false;
  }
  strncpy(Link.hostname, matches[1].first, matches[1].second-matches[1].first);

  Log(LOGDEBUG, "Hostname: %s", Link.hostname);

  char portstr[256];
  if(matches[4].second-matches[4].first > 5) {
          Log(LOGERROR, "Port must not be longer than 5 digits!");
          return false;
  }
  strncpy(portstr, matches[4].first, matches[4].second-matches[4].first);
  Link.port = atoi(portstr);

*/
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
/*
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
*/


//CURL url(m_strLink);
  /*std::string app = std::string(Link.url);

  std::string::size_type slistPos = std::string(Link.url).find("slist=");
  if ( slistPos == std::string::npos ){
    // no slist parameter. send the path as the app
    // if URL path contains a slash, use the part up to that as the app
    // as we'll send the part after the slash as the thing to play
    std::string::size_type pos_slash = app.find_last_of("/");
    if( pos_slash != std::string::npos ){
      app = app.substr(0,pos_slash);
    }
  }*/

