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
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "fcintl.h"
#include "shared.h"
#include "support.h"

#include "version.h"

/********************************************************************** 
  Return the BETA message.
  If returns NULL, not a beta version.
***********************************************************************/
char *beta_message (void)
{
#if IS_BETA_VERSION
  static char msgbuf[128];
  static const char *month[] =
  {
    NULL,
    N_("January"),
    N_("February"),
    N_("March"),
    N_("April"),
    N_("May"),
    N_("June"),
    N_("July"),
    N_("August"),
    N_("September"),
    N_("October"),
    N_("November"),
    N_("December")
  };
  my_snprintf (msgbuf, sizeof (msgbuf),
	       _("THIS IS A BETA VERSION\n"
		 "Freeciv %s will be released in\n"
		 "%s, at %s"), /* No full stop here since it would be
				  immediately following a URL, which
				  would only cause confusion. */
	       NEXT_STABLE_VERSION,
	       _(NEXT_RELEASE_MONTH),
	       WEBSITE_URL);
  return msgbuf;
#else
  return NULL;
#endif
}
