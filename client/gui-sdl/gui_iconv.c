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
                          gui_iconv.c  -  description
                             -------------------
    begin                : Thu May 30 2002
    copyright            : (C) 2002 by Rafa³ Bursig
    email                : Rafa³ Bursig <bursig@poczta.fm>
	
    Based on "iconv_string(...)" function Copyright (C) 1999-2001 Bruno Haible.
    This function is put into the public domain.
	
***********************************************************************/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <errno.h>
#include <iconv.h>
#include <stdlib.h>
#include <string.h>

#include <SDL/SDL_byteorder.h>
#include <SDL/SDL_types.h>

#ifdef HAVE_LIBCHARSET
#include <libcharset.h>
#else
#ifdef HAVE_LANGINFO_CODESET
#include <langinfo.h>
#endif
#endif

#ifndef ICONV_CONST
#define ICONV_CONST	const
#endif /* ICONV_CONST */

#include "gui_mem.h"
#include "unistring.h"
#include "gui_iconv.h"

/**************************************************************************
  Return the display charset encoding (which is always a variant of
  UTF-16, but must be adjusted for byteorder since SDL_ttf is not
  byteorder-clean).
**************************************************************************/
static const char *get_display_encoding(void)
{
#if SDL_BYTEORDER == SDL_LIL_ENDIAN
  return "UTF-16LE";
#else
  return "UTF-16BE";
#endif
}

/**************************************************************************
  Return the local charset encoding (which will be passed to iconv).
**************************************************************************/
static const char *get_local_encoding(void)
{
#ifdef HAVE_LIBCHARSET
  return locale_charset();
#else
#  ifdef HAVE_LANGINFO_CODESET
  return nl_langinfo(CODESET);
#  else
  return "";
#  endif
#endif
}

/**************************************************************************
  Convert string from local encoding (8 bit char) to
  display encoding (16 bit unicode) and resut put in pToString.
  if pToString == NULL then resulting string will be allocate automaticaly.
  DANGER !: pToString size MUST BE >= strlen(pFromString)

  Function return (Uint16 *) pointer to (new) pToString.
**************************************************************************/
Uint16 *convertcopy_to_utf16(Uint16 * pToString, const char *pFromString)
{
  /* Start Parametrs */
  const char *pTocode = get_display_encoding();
  const char *pFromcode = get_local_encoding();
  const char *pStart = pFromString;
  size_t length = strlen(pFromString);
  const char *pEnd = pFromString + length;
  char *pResult = (char *) pToString;
  /* ===== */

  iconv_t cd = iconv_open(pTocode, pFromcode);
  if (cd == (iconv_t) (-1)) {
    if (errno != EINVAL) {
      return pToString;
    }
  }

  /* From 8 bit code to UTF-16 (16 bit code) */
  length = (length + 1) * 2;
  
  if (!pResult) {
    pResult = MALLOC(length);
  }

  iconv(cd, NULL, NULL, NULL, NULL);	/* return to the initial state */

  /* Do the conversion for real. */
  {
    const char *pInptr = pStart;
    size_t Insize = pEnd - pStart + 1;

    char *pOutptr = pResult;
    size_t Outsize = length;

    while (Insize > 0) {
      size_t Res =
	  iconv(cd, (ICONV_CONST char **) &pInptr, &Insize, &pOutptr, &Outsize);
      if (Res == (size_t) (-1)) {
	if (errno == EINVAL) {
	  break;
	} else {
	  int saved_errno = errno;
	  iconv_close(cd);
	  errno = saved_errno;
	  if(!pToString) {
	    FREE(pResult);
	  }
	  return pToString;
	}
      }
    }

    {
      size_t Res = iconv(cd, NULL, NULL, &pOutptr, &Outsize);
      if (Res == (size_t) (-1)) {
	int saved_errno = errno;
	iconv_close(cd);
	errno = saved_errno;
	if(!pToString) {
	  FREE(pResult);
	}
	return pToString;
      }
    }

    if (Outsize != 0) {
      abort();
    }
  }

  iconv_close(cd);

  return (Uint16 *) pResult;
}

/**************************************************************************
  ...
**************************************************************************/
char *convert_to_chars(const Uint16 * pUniString)
{
  /* Start Parametrs */
  const char *pFromcode = get_display_encoding();
  const char *pTocode = get_local_encoding();
  const char *pStart = (char *) pUniString;
  size_t length = unistrlen(pUniString);
  const char *pEnd = (char *) pUniString + (length * 2) + 2;
  /* ===== */

  char *pResult = NULL;
  iconv_t cd;
  
  /* ===== */

  if (!pStart) {
    return NULL;
  }

  /* From 16 bit code to 8 bit code */
  length++;

  cd = iconv_open(pTocode, pFromcode);
  if (cd == (iconv_t) (-1)) {
    if (errno != EINVAL) {
      return NULL;
    }
  }

  pResult = MALLOC(length);

  iconv(cd, NULL, NULL, NULL, NULL);	/* return to the initial state */

  /* Do the conversion for real. */
  {
    const char *pInptr = pStart;
    size_t Insize = pEnd - pStart;

    char *pOutptr = pResult;
    size_t Outsize = length;

    while (Insize > 0) {
      size_t Res =
	  iconv(cd, (ICONV_CONST char **) &pInptr, &Insize, &pOutptr, &Outsize);
      if (Res == (size_t) (-1)) {
	if (errno == EINVAL) {
	  break;
	} else {
	  int saved_errno = errno;
	  iconv_close(cd);
	  errno = saved_errno;
	  FREE(pResult);
	  return NULL;
	}
      }
    }

    {
      size_t Res = iconv(cd, NULL, NULL, &pOutptr, &Outsize);
      if (Res == (size_t) (-1)) {
	int saved_errno = errno;
	iconv_close(cd);
	errno = saved_errno;
	FREE(pResult);
	return NULL;
      }
    }

    if (Outsize != 0) {
      abort();
    }
  }

  iconv_close(cd);

  return pResult;
}
