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
  ...
**************************************************************************/
Uint16 *convert_to_utf16(const char *pString)
{
  /* Start Parametrs */
  const char *pTocode = get_display_encoding();
  const char *pFromcode = get_local_encoding();
  const char *pStart = pString;
  const char *pEnd = pString + strlen(pString);
  /* ===== */

  char *pResult = NULL;

  /* From 8 bit code to UTF-16 (16 bit code)
     size_t length = 2*(strlen(pString)+1); */
  size_t length = (strlen(pString) + 1) << 1;
  /* ===== */

  iconv_t cd = iconv_open(pTocode, pFromcode);
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
    size_t Insize = pEnd - pStart + 1;

    char *pOutptr = pResult;
    size_t Outsize = length;

    while (Insize > 0) {
      size_t Res =
	  iconv(cd, (char **) &pInptr, &Insize, &pOutptr, &Outsize);
      if (Res == (size_t) (-1)) {
	if (errno == EINVAL) {
	  break;
	} else {
	  int saved_errno = errno;
	  iconv_close(cd);
	  errno = saved_errno;
	  FREE( pResult );
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
	FREE( pResult );
	return NULL;
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
Uint16 *convertcopy_to_utf16(Uint16 * pToString, const char *pFromString)
{
  /* Start Parametrs */
  const char *pTocode = get_display_encoding();
  const char *pFromcode = get_local_encoding();
  const char *pStart = pFromString;
  const char *pEnd = pFromString + strlen(pFromString);
  /* ===== */

  char *pResult = (char *) pToString;

  /* From 8 bit code to UTF-16 (16 bit code)
     size_t length = 2*(strlen(pString)+1); */
  size_t length = (strlen(pFromString) + 1) << 1;
  /* ===== */

  iconv_t cd = iconv_open(pTocode, pFromcode);
  if (cd == (iconv_t) (-1)) {
    if (errno != EINVAL) {
      return NULL;
    }
  }

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
	  iconv(cd, (char **) &pInptr, &Insize, &pOutptr, &Outsize);
      if (Res == (size_t) (-1)) {
	if (errno == EINVAL) {
	  break;
	} else {
	  int saved_errno = errno;
	  iconv_close(cd);
	  errno = saved_errno;
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
	return NULL;
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
  const char *pEnd =
      (char *) pUniString + (unistrlen(pUniString) << 1) + 2;
  /* ===== */

  char *pResult = NULL;
  iconv_t cd;
  size_t length;

  /* ===== */

  if (!pStart) {
    return NULL;
  }

  /* From 16 bit code to 8 bit code */
  length = unistrlen(pUniString) + 1;

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
	  iconv(cd, (char **) &pInptr, &Insize, &pOutptr, &Outsize);
      if (Res == (size_t) (-1)) {
	if (errno == EINVAL) {
	  break;
	} else {
	  int saved_errno = errno;
	  iconv_close(cd);
	  errno = saved_errno;
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
