/********************************************************************** 
 Freeciv - Copyright (C) 1996-2004 - The Freeciv Team
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

/* utility */
#include "log.h"

/* gui-sdl */
#include "connectdlg.h"

#include "pages.h"

/**************************************************************************
  Sets the "page" that the client should show.  See documentation in
  pages_g.h.
**************************************************************************/
void set_client_page(enum client_pages page)
{
  /* PORTME */
  freelog(LOG_DEBUG, "PORT ME: set_client_page()");
}

/****************************************************************************
  Set the list of available rulesets.  The default ruleset should be
  "default", and if the user changes this then set_ruleset() should be
  called.
****************************************************************************/
void gui_set_rulesets(int num_rulesets, char **rulesets)
{
  /* PORTME */
  freelog(LOG_DEBUG, "PORT ME: gui_set_rulesets()");    
}

/**************************************************************************
  Returns current client page
**************************************************************************/
enum client_pages get_client_page(void)
{
  /* PORTME*/
  freelog(LOG_DEBUG, "PORT ME: get_client_page()");    
  return PAGE_MAIN;
}

/**************************************************************************
  update the start page.
**************************************************************************/
void update_start_page(void)
{
  /* PORTME*/    
  freelog(LOG_DEBUG, "PORT ME: update_start_page()");    
}
