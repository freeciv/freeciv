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
                          unistring.c  -  description
                             -------------------
    begin                : Mon Jul 08 2002
    copyright            : (C) 2002 by Rafa³ Bursig
    email                : Rafa³ Bursig <bursig@poczta.fm>
 ***************************************************************************/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdlib.h>
#include <string.h>

#include <SDL/SDL_types.h>

#include "gui_mem.h"
#include "unistring.h"

/**************************************************************************
  ...
**************************************************************************/
size_t unistrlen(const Uint16 *pUniString)
{
  size_t ret = 0;
  if (pUniString) {
    while (*pUniString) {
      pUniString++;
      ret++;
    }
  }
  return ret;
}

/**************************************************************************
  ...
**************************************************************************/
Uint16 *unistrcpy(Uint16 *pToUniString, const Uint16 *pFromUniString)
{
  size_t size = (unistrlen(pFromUniString) + 1) << 1;
  if (!pToUniString) {
    pToUniString = MALLOC(size);
  }
  return memcpy(pToUniString, pFromUniString, size);
}

/**************************************************************************
  ...
**************************************************************************/
Uint16 *unistrcat(Uint16 *pToUniString,
		  const Uint16 *pFromUniString)
{
  size_t src_size = (unistrlen(pFromUniString) + 1) << 1;
  size_t dst_size = unistrlen(pToUniString) << 1;
  pToUniString = REALLOC(pToUniString, src_size + dst_size);
  return memcpy((Uint8 *) pToUniString + dst_size,
		pFromUniString, src_size);
}

/**************************************************************************
  ...
**************************************************************************/
Uint16 *unistrclon(const Uint16 *pFromUniString)
{
  /* size_t size = (unistrlen(pFromUniString) + 1) * sizeof(Uint16); */
  size_t size = (unistrlen(pFromUniString) + 1) << 1;
  Uint16 *pToUniString = MALLOC(size);
  *pToUniString = *pFromUniString;
  /* return memcpy(pToUniString, pFromUniString, size); */
  return pToUniString;
}

/**************************************************************************
  ...
**************************************************************************/
Uint16 *unistrdup(const Uint16 *pUniString)
{
  size_t size = (unistrlen(pUniString) + 1) << 1;
  Uint16 *pNewUniString = MALLOC(size);
  return memcpy(pNewUniString, pUniString, size);
}

/**************************************************************************
  ...
**************************************************************************/
Uint16 **create_new_line_unistrings(const Uint16 *pUnistring)
{
  Uint16 *pTmp = NULL;
  Uint16 **pBuf = NULL;
  Uint32 len = 0, count = 0;
  int i;

  while (*pUnistring != 0) {
    if (*pUnistring == 10) {	/* find new line char */
      pBuf = REALLOC(pBuf, ++count * sizeof(Uint16 *));
      if (len) {
	i = 0;
	pBuf[count - 1] = MALLOC(len * sizeof(Uint16) + 2);
	for (pTmp = (Uint16 *) (pUnistring - len); pTmp < pUnistring;
	     pTmp++)
	  pBuf[count - 1][i++] = *pTmp;
      } else {
	pBuf[count - 1] = MALLOC(sizeof(Uint16) + 2);
	pBuf[count - 1][0] = 32;
      }
      len = 0;
    } else {
      len++;
    }

    pUnistring++;

    if ((*pUnistring == 0) && len) {
      pBuf = REALLOC(pBuf, ++count * sizeof(Uint16 *));
      i = 0;
      pBuf[count - 1] = MALLOC(len * sizeof(Uint16) + 2);
      for (pTmp = (Uint16 *) (pUnistring - len); pTmp < pUnistring; pTmp++)
	pBuf[count - 1][i++] = *pTmp;
    }

  }

  pBuf = REALLOC(pBuf, ++count * sizeof(Uint16 *));
  pBuf[count - 1] = NULL;

  return pBuf;
}
