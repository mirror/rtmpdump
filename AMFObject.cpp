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

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <arpa/inet.h>

#include "AMFObject.h"
#include "log.h"
#include "rtmp.h"

RTMP_LIB::AMFObjectProperty RTMP_LIB::AMFObject::m_invalidProp;

RTMP_LIB::AMFObjectProperty::AMFObjectProperty()
{
  Reset();
}

RTMP_LIB::AMFObjectProperty::AMFObjectProperty(const std::string & strName, double dValue)
{
  Reset();
}

RTMP_LIB::AMFObjectProperty::AMFObjectProperty(const std::string & strName, bool bValue)
{
  Reset();
}

RTMP_LIB::AMFObjectProperty::AMFObjectProperty(const std::string & strName, const std::string & strValue)
{
  Reset();
}

RTMP_LIB::AMFObjectProperty::AMFObjectProperty(const std::string & strName, const AMFObject & objValue)
{
  Reset();
}

RTMP_LIB::AMFObjectProperty::~ AMFObjectProperty()
{
}

const std::string &RTMP_LIB::AMFObjectProperty::GetPropName() const
{
  return m_strName;
}

RTMP_LIB::AMFDataType RTMP_LIB::AMFObjectProperty::GetType() const
{
  return m_type;
}

double RTMP_LIB::AMFObjectProperty::GetNumber() const
{
  return m_dNumVal;
}

bool RTMP_LIB::AMFObjectProperty::GetBoolean() const
{
  return m_dNumVal != 0;
}

const std::string &RTMP_LIB::AMFObjectProperty::GetString() const
{
  return m_strVal;
}

const RTMP_LIB::AMFObject &RTMP_LIB::AMFObjectProperty::GetObject() const
{
  return m_objVal;
}

bool RTMP_LIB::AMFObjectProperty::IsValid() const
{
  return (m_type != AMF_INVALID);
}

int RTMP_LIB::AMFObjectProperty::Encode(char * pBuffer, int nSize) const
{
  int nBytes = 0;
  
  if (m_type == AMF_INVALID)
    return -1;

  if (m_type != AMF_NULL && nSize < (int)m_strName.size() + (int)sizeof(short) + 1)
    return -1;

  if (m_type != AMF_NULL && !m_strName.empty())
  {
    nBytes += EncodeName(pBuffer);
    pBuffer += nBytes;
    nSize -= nBytes;
  }

  switch (m_type)
  {
    case AMF_NUMBER:
      if (nSize < 9)
        return -1;
      nBytes += RTMP_LIB::CRTMP::EncodeNumber(pBuffer, GetNumber());
      break;

    case AMF_BOOLEAN:
      if (nSize < 2)
        return -1;
      nBytes += RTMP_LIB::CRTMP::EncodeBoolean(pBuffer, GetBoolean());
      break;

    case AMF_STRING:
      if (nSize < (int)m_strVal.size() + (int)sizeof(short))
        return -1;
      nBytes += RTMP_LIB::CRTMP::EncodeString(pBuffer, GetString());
      break;

    case AMF_NULL:
      if (nSize < 1)
        return -1;
      *pBuffer = 0x05;
      nBytes += 1;
      break;

    case AMF_OBJECT:
    {
      int nRes = m_objVal.Encode(pBuffer, nSize);
      if (nRes == -1)
        return -1;

      nBytes += nRes;
      break;
    }
    default:
      Log(LOGERROR,"%s, invalid type. %d", __FUNCTION__, m_type);
      return -1;
  };  

  return nBytes;
}

int RTMP_LIB::AMFObjectProperty::Decode(const char * pBuffer, int nSize, bool bDecodeName) 
{
  int nOriginalSize = nSize;

  if (nSize == 0 || !pBuffer) {
    Log(LOGDEBUG,"empty buffer/no buffer pointer!");
    return -1;
  }
  
  //if (*pBuffer == 0x05 /* AMF_NULL */ || *pBuffer == 0x06 /* AMF_UNDEFINED */ || *pBuffer == 0x0D /* AMF_UNSUPPORTED */)
  //{
  //  m_type = AMF_NULL;
  //  return 1;
  //}

  if (bDecodeName && nSize < 4) { // at least name (length + at least 1 byte) and 1 byte of data
    Log(LOGDEBUG,"Not enough data for decoding with name, less then 4 bytes!");
    return -1;
  }

  if (bDecodeName)
  {
    short nNameSize = RTMP_LIB::CRTMP::ReadInt16(pBuffer);
    if (nNameSize > nSize - (short)sizeof(short)) {
      Log(LOGDEBUG,"Name size out of range: namesize (%d) > len (%d) - 2", nNameSize, nSize);
      return -1;
    }

    m_strName = RTMP_LIB::CRTMP::ReadString(pBuffer);
    nSize -= sizeof(short) + m_strName.size();
    pBuffer += sizeof(short) + m_strName.size();
  }

  if (nSize == 0) {
    return -1;
  }

  nSize--;

  switch (*pBuffer)
  {
    case 0x00: //AMF_NUMBER:
      if (nSize < (int)sizeof(double))
        return -1;
      m_dNumVal = RTMP_LIB::CRTMP::ReadNumber(pBuffer+1);
      nSize -= sizeof(double);
      m_type = AMF_NUMBER;
      break;
    case 0x01: //AMF_BOOLEAN:
      if (nSize < 1)
        return -1;
      m_dNumVal = (double)RTMP_LIB::CRTMP::ReadBool(pBuffer+1);
      nSize--;
      m_type = AMF_BOOLEAN;
      break;
    case 0x02: //AMF_STRING:
    {
      short nStringSize = RTMP_LIB::CRTMP::ReadInt16(pBuffer+1);
      if (nSize < nStringSize + (int)sizeof(short))
        return -1;
      m_strVal = RTMP_LIB::CRTMP::ReadString(pBuffer+1);
      nSize -= (sizeof(short) + nStringSize);
      m_type = AMF_STRING;
      break;
    }
    case 0x03: //AMF_OBJECT:
    {
      int nRes = m_objVal.Decode(pBuffer+1, nSize, true);
      if (nRes == -1)
        return -1;
      nSize -= nRes;
      m_type = AMF_OBJECT;
      break;
    }
    case 0x0A: //AMF_ARRAY
    {
      int nArrayLen = RTMP_LIB::CRTMP::ReadInt32(pBuffer+1);
      nSize -= 4;
      
      int nRes = m_objVal.DecodeArray(pBuffer+5, nSize, nArrayLen, false);
      if (nRes == -1)
        return -1;
      nSize -= nRes;
      m_type = AMF_OBJECT; 
      break;
    }
    case 0x08: //AMF_MIXEDARRAY
    {
      //int nMaxIndex = RTMP_LIB::CRTMP::ReadInt32(pBuffer+1); // can be zero for unlimited
      nSize -= 4;

      // next comes the rest, mixed array has a final 0x000009 mark and names, so its an object
      int nRes = m_objVal.Decode(pBuffer+5, nSize, true);
      if (nRes == -1)
      	return -1;
      nSize -= nRes;
      m_type = AMF_OBJECT; 
      break;
    }
    case 0x05: /* AMF_NULL */
    case 0x06: /* AMF_UNDEFINED */
    case 0x0D: /* AMF_UNSUPPORTED */
        m_type = AMF_NULL;
    	break;
    case 0x0B: // AMF_DATE
    {
      if (nSize < 10)
              return -1;

      m_dNumVal = RTMP_LIB::CRTMP::ReadNumber(pBuffer+1);
      m_nUTCOffset = RTMP_LIB::CRTMP::ReadInt16(pBuffer+9);

      m_type = AMF_DATE;
      nSize -= 10;
      break;
    }
    default:
      Log(LOGDEBUG,"%s - unknown datatype 0x%02x, @0x%08X", __FUNCTION__, (unsigned char)(*pBuffer), pBuffer);
      return -1;
  }

  return nOriginalSize - nSize;
}

void RTMP_LIB::AMFObjectProperty::Dump() const
{
  if (m_type == AMF_INVALID)
  {
    Log(LOGDEBUG,"Property: INVALID");
    return;
  }

  if (m_type == AMF_NULL)
  {
    Log(LOGDEBUG,"Property: NULL");
    return;
  }

  if (m_type == AMF_OBJECT)
  {
    Log(LOGDEBUG,"Property: <Name: %25s, OBJECT>", m_strName.empty() ? "no-name." : m_strName.c_str());
    m_objVal.Dump();
    return;
  }

  char strRes[256]="";
  snprintf(strRes, 255, "Name: %25s, ", m_strName.empty()? "no-name.":m_strName.c_str());

  char str[256]="";
  switch(m_type)
  {
    case AMF_NUMBER:
      snprintf(str, 255, "NUMBER:\t%.2f", m_dNumVal);
      break;
    case AMF_BOOLEAN:
      snprintf(str, 255, "BOOLEAN:\t%s", m_dNumVal == 1.?"TRUE":"FALSE");
      break;
    case AMF_STRING:
      snprintf(str, 255, "STRING:\t%s", m_strVal.c_str());
      break;
    case AMF_DATE:
      snprintf(str, 255, "DATE:\ttimestamp: %.2f, UTC offset: %d", m_dNumVal, m_nUTCOffset);
      break;
    default:
      snprintf(str, 255, "INVALID TYPE 0x%02x", (unsigned char)m_type);
  }

  Log(LOGDEBUG,"Property: <%s%s>", strRes, str);
}

void RTMP_LIB::AMFObjectProperty::Reset()
{
  m_dNumVal = 0.;
  m_strVal.clear();
  m_objVal.Reset();
  m_type = AMF_INVALID;
}

int RTMP_LIB::AMFObjectProperty::EncodeName(char *pBuffer) const
{
  short length = htons(m_strName.size());
  memcpy(pBuffer, &length, sizeof(short));
  pBuffer += sizeof(short);

  memcpy(pBuffer, m_strName.c_str(), m_strName.size());
  return m_strName.size() + sizeof(short);
}


// AMFObject

RTMP_LIB::AMFObject::AMFObject()
{
  Reset();
}

RTMP_LIB::AMFObject::~ AMFObject()
{
  Reset();
}

int RTMP_LIB::AMFObject::Encode(char * pBuffer, int nSize) const
{
  if (nSize < 4)
    return -1;

  *pBuffer = 0x03; // object

  int nOriginalSize = nSize;
  for (size_t i=0; i<m_properties.size(); i++)
  {
    int nRes = m_properties[i].Encode(pBuffer, nSize);
    if (nRes == -1)
    {
      Log(LOGERROR,"AMFObject::Encode - failed to encode property in index %d", i);
    }
    else
    {
      nSize -= nRes;
      pBuffer += nRes;
    }
  }

  if (nSize < 3)
    return -1; // no room for the end marker

  RTMP_LIB::CRTMP::EncodeInt24(pBuffer, 0x000009);
  nSize -= 3;

  return nOriginalSize - nSize;
}

int RTMP_LIB::AMFObject::DecodeArray(const char * pBuffer, int nSize, int nArrayLen, bool bDecodeName)
{
  int nOriginalSize = nSize;
  bool bError = false;

  while(nArrayLen > 0)
  {
    nArrayLen--;

    RTMP_LIB::AMFObjectProperty prop;
    int nRes = prop.Decode(pBuffer, nSize, bDecodeName);
    if (nRes == -1)
      bError = true;
    else
    {
      nSize -= nRes;
      pBuffer += nRes;
      m_properties.push_back(prop);
    }
  }
  if (bError)
    return -1;

  return nOriginalSize - nSize;
}

int RTMP_LIB::AMFObject::Decode(const char * pBuffer, int nSize, bool bDecodeName)
{
  int nOriginalSize = nSize;
  bool bError = false; // if there is an error while decoding - try to at least find the end mark 0x000009

  while (nSize >= 3)
  {
    if (RTMP_LIB::CRTMP::ReadInt24(pBuffer) == 0x00000009)
    {
      nSize -= 3;
      bError = false;
      break;
    }

    if (bError)
    {
      Log(LOGDEBUG,"DECODING ERROR, IGNORING BYTES UNTIL NEXT KNOWN PATTERN!");
      nSize--;
      pBuffer++;
      continue;
    }

    RTMP_LIB::AMFObjectProperty prop;
    int nRes = prop.Decode(pBuffer, nSize, bDecodeName);
    if (nRes == -1)
      bError = true;
    else
    {
      nSize -= nRes;
      pBuffer += nRes;
      m_properties.push_back(prop);
    }
  }

  if (bError)
    return -1;

  return nOriginalSize - nSize;
}

void RTMP_LIB::AMFObject::AddProperty(const AMFObjectProperty & prop)
{
  m_properties.push_back(prop);
}

int RTMP_LIB::AMFObject::GetPropertyCount() const
{
  return m_properties.size();
}

const RTMP_LIB::AMFObjectProperty & RTMP_LIB::AMFObject::GetProperty(const std::string & strName) const
{
  for (size_t n=0; n<m_properties.size(); n++)
  {
    if (m_properties[n].GetPropName() == strName)
      return m_properties[n];
  }

  return m_invalidProp;
}

const RTMP_LIB::AMFObjectProperty & RTMP_LIB::AMFObject::GetProperty(size_t nIndex) const
{
  if (nIndex >= m_properties.size())
    return m_invalidProp;

  return m_properties[nIndex];
}

void RTMP_LIB::AMFObject::Dump() const
{
  //Log(LOGDEBUG,"START AMF Object Dump:");
  
  for (size_t n=0; n<m_properties.size(); n++) {
    m_properties[n].Dump();
  }

  //Log(LOGDEBUG,"END AMF Object Dump");
}

void RTMP_LIB::AMFObject::Reset()
{
  m_properties.clear();
}

