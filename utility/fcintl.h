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
#ifndef FC__FCINTL_H
#define FC__FCINTL_H

#ifdef HAVE_CONFIG_H
#ifndef FC_CONFIG_H		/* this should be defined in config.h */
#error Files including fcintl.h should also include config.h directly
#endif
#endif

#ifdef ENABLE_NLS
#include <libintl.h>
#ifdef HAVE_LOCALE_H
#include <locale.h>
#endif

#define _(String) gettext(String)
#define N_(String) String
#define Q_(String) skip_intl_qualifier_prefix(gettext(String))
#define PL_(String1, String2, n) ngettext((String1), (String2), (n))

#else

#define _(String) (String)
#define N_(String) String
#define Q_(String) skip_intl_qualifier_prefix(String)
#define PL_(String1, String2, n) ((n) == 1 ? (String1) : (String2))

#define textdomain(Domain)
#define bindtextdomain(Package, Directory)

#endif

const char *skip_intl_qualifier_prefix(const char *str);

#endif  /* FC__FCINTL_H */
