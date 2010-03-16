/**********************************************************************
 Freeciv - Copyright (C) 2004 - The Freeciv Project
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
***********************************************************************/

#ifndef FC__NAME_TRANSLATION_H
#define FC__NAME_TRANSLATION_H

/* utility */
#include "fcintl.h"
#include "support.h"

/* Don't allow other modules to access directly to the fields. */
#define vernacular _private_vernacular_
#define translated _private_translated_

/* Ruleset strings (such as names) are kept in their original vernacular,
 * translated upon first use. The translation is cached for future use.
 */
struct name_translation {
#ifdef ENABLE_NLS
  const char *translated;               /* String doesn't need freeing,
                                         * Used as mutable. */
#endif
  char vernacular[MAX_LEN_NAME];        /* Original string,
                                         * used for comparisons. */
};

/* Inititalization macro. */
#ifdef ENABLE_NLS
#define NAME_INIT { NULL, "\0" }
#else
#define NAME_INIT { "\0" }
#endif /* ENABLE_NLS */

/****************************************************************************
  Initializes a name translation structure.
****************************************************************************/
static inline void name_init(struct name_translation *ptrans)
{
  ptrans->vernacular[0] = '\0';
#ifdef ENABLE_NLS
  ptrans->translated = NULL;
#endif
}

/****************************************************************************
  Set the vernacular name of the name translation structure.
****************************************************************************/
static inline void name_set(struct name_translation *ptrans,
                            const char *vernacular_name)
{
  static const char name_too_long[] = "Name \"%s\" too long; truncating.";

  (void) sz_loud_strlcpy(ptrans->vernacular, vernacular_name, name_too_long);
#ifdef ENABLE_NLS
  /* Not translated yet. */
  ptrans->translated = NULL;
#endif
}

/****************************************************************************
  Return the vernacular name of the name translation structure.
****************************************************************************/
static inline const char *rule_name(const struct name_translation *ptrans)
{
  return Qn_(ptrans->vernacular);
}

/****************************************************************************
  Return the translated name of the name translation structure.
****************************************************************************/
static inline const char *
name_translation(const struct name_translation *ptrans)
{
#ifdef ENABLE_NLS
  if (NULL == ptrans->translated) {
    /* ptrans->translated used as mutable. */
    ((struct name_translation *) ptrans)->translated =
        ('\0' == ptrans->vernacular[0]
         ? ptrans->vernacular : Q_(ptrans->vernacular));
  }
  return ptrans->translated;
#else
  return Qn_(ptrans->vernacular);
#endif /* ENABLE_NLS */
}

/* Don't allow other modules to access directly to the fields. */
#undef vernacular
#undef translated

#endif /* FC__NAME_TRANSLATION_H */
