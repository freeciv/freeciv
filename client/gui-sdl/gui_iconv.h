/********************************************************************** 
 Freeciv - Copyright (C) 1996 - A Kjeldberg, L Gregersen, P Unold
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
***********************************************************************/

/***************************************************************************
                          gui_iconv.h  -  description
                             -------------------
    begin                : Thu May 30 2002
    copyright            : (C) 2002 by Rafa³ Bursig
    email                : Rafa³ Bursig <bursig@poczta.fm>
	
    Based on "iconv_string(...)" function Copyright (C) 1999-2001 Bruno Haible.
    This function is put into the public domain.
	
***************************************************************************/

#ifndef FC__GUI_ICONV_H
#define FC__GUI_ICONV_H

#define LOCAL_CODE "ISO-8859-2"

Uint16 *convert_to_utf16(const char *pString);
Uint16 *convertcopy_to_utf16(Uint16 *pToString, const char *pFromString);
char *convert_to_chars(const Uint16 *pUniString);

#endif
