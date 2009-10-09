/*  RTMPDump
 *  Copyright (C) 2008-2009 Andrej Stepanchuk
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
#include <stdarg.h>

#include "log.h"

void Log(int level, const char *format, ...)
{
        char str[256]="";

        va_list args;
        va_start(args, format);
        vsnprintf(str, 255, format, args);
        va_end(args);

	//if(level != LOGDEBUG)
        printf("\r%s: %s\n", level==LOGDEBUG?"DEBUG":(level==LOGERROR?"ERROR":"WARNING"), str);
}

