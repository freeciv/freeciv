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

/**********************************************************************
                          unistring.h  -  description
                             -------------------
    begin                : Mon Jul 08 2002
    copyright            : (C) 2002 by Rafa³ Bursig
    email                : Rafa³ Bursig <bursig@poczta.fm>
 **********************************************************************/

#ifndef __UNISTRING_H
#define __UNISTRING_H

size_t unistrlen(const Uint16 *pUniString);
Uint16 *unistrcpy(Uint16 *pToUniString, const Uint16 *pFromUniString);
Uint16 *unistrcat(Uint16 *pToUniString,
		  const Uint16 *pFromUniString);
Uint16 *unistrclon(const Uint16 *pFromUniString);
Uint16 *unistrdup(const Uint16 *pUniString);
Uint16 **create_new_line_unistrings(const Uint16 *pUnistring);

/*
 *	Unistring version of strdupa macro
 *	MUST NOT BE CALL IN FUNCTION PARAMETRS
 */
#define unistrdupa( pUniString )					\
(__extension__								\
	({								\
		__const Uint16 *__uniold = (pUniString);		\
		size_t __unilen = (unistrlen( __uniold ) + 1) << 1;	\
		Uint16 *__uninew = ALLOCA( __unilen );			\
		( Uint16 *) memcpy( __uninew , __uniold, __unilen);	\
	}))

#endif
