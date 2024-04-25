/***********************************************************************
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

#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

#include <string.h>

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

/* utility */
#include "mem.h"

#include "fcintl.h"

static bool autocap = FALSE;

/*******************************************************************//**
  Some strings are ambiguous for translation. For example, "Game" is
  something you play (like Freeciv!) or animals that can be hunted.
  To distinguish strings for translation, we qualify them with a prefix
  string of the form "?qualifier:". So, the above two cases might be:
    "Game"           -- when used as meaning something you play
    "?animals:Game"  -- when used as animals to be hunted
  Notice that only the second is qualified; the first is processed in
  the normal gettext() manner (as at most one ambiguous string can be).

  This function tests for, and removes if found, the qualifier prefix part
  of a string.

  This function is called by the Q_() macro and specenum. If used in the
  Q_() macro it should, if NLS is enabled, have called gettext() to get the
  argument to pass to this function. Specenum use it untranslated.
***********************************************************************/
const char *skip_intl_qualifier_prefix(const char *str)
{
  const char *ptr;

  if (*str != '?') {
    return str;
  } else if ((ptr = strchr(str, ':'))) {
    return (ptr + 1);
  } else {
    return str;                 /* May be something wrong */
  }
}

/*******************************************************************//**
  This function tries to capitalize first letter of the string.
  Currently this handles just single byte UTF-8 characters, since
  those are same as ASCII.
***********************************************************************/
char *capitalized_string(const char *str)
{
  int len = strlen(str);
  char *result = fc_malloc(len + 1);

  fc_strlcpy(result, str, len + 1);

  if (autocap) {
    if ((unsigned char) result[0] < 128) {
      result[0] = fc_toupper(result[0]);
    }
  }

  return result;
}

/*******************************************************************//**
  Free capitalized string.
***********************************************************************/
void free_capitalized(char *str)
{
  FC_FREE(str);
}

/*******************************************************************//**
  Translation opts in to automatic capitalization features.
***********************************************************************/
void capitalization_opt_in(bool opt_in)
{
  autocap = opt_in;
}

/*******************************************************************//**
  Automatic capitalization features requested.
***********************************************************************/
bool is_capitalization_enabled(void)
{
  return autocap;
}

/*******************************************************************//**
  Return directory containing locales.
  In a rare cases this can be a relative path, but it tries
  to return absolute path when ever possible.
***********************************************************************/
const char *get_locale_dir(void)
{
  static bool ldbuf_init = FALSE;
  static char buf[4096];

  if (!ldbuf_init) {
    bool absolute = FALSE;

    /* FIXME: On Windows, also something starting with the drive,
     *        e.g., "C:\", can be absolute path. Paths like that
     *        are never used with our currently supported setups.
     *
     * Can't check just against DIR_SEPARATOR_CHAR as the mingw
     * layer may have converted path to use '/' even on Windows.
     *
     * Have to have these as two separate ifs instead of just one
     * with || or &&, to avoid compiler warning about constants
     * being used in such a condition.
     */
    if (LOCALEDIR[0] == '/') {
      absolute = TRUE;
    } else if (LOCALEDIR[0] == '\\') {
      absolute = TRUE;
    }

    if (!absolute) {
      char *cwdbuf;

#ifdef HAVE_GETCWD
      cwdbuf = getcwd(nullptr, 0);
#else
      /* Can't help it. Must construct relative path. */
      cwdbuf = fc_strdup(".");
#endif

      fc_snprintf(buf, sizeof(buf), "%s" DIR_SEPARATOR LOCALEDIR, cwdbuf);

      free(cwdbuf);
    } else {
      strncpy(buf, LOCALEDIR, sizeof(buf));
    }

    ldbuf_init = TRUE;
  }

  return buf;
}
