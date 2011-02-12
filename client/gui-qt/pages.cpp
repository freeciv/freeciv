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
#include <fc_config.h>
#endif

// client
#include "chatline_common.h"

// gui-qt
#include "fc_client.h"
#include "qtg_cxxside.h"

#include "pages.h"

/**************************************************************************
  Sets the "page" that the client should show.  See also pages_g.h.
**************************************************************************/
void qtg_real_set_client_page(enum client_pages page)
{
  static bool obs_cmd_given = false;

  gui()->switch_page(page);

  switch (page) {
   case PAGE_START:
     if (!obs_cmd_given) {
       send_chat("/observe");
       obs_cmd_given = true;
     }
     break;
   default:
     break;
  }
}

/****************************************************************************
  Set the list of available rulesets.  The default ruleset should be
  "default", and if the user changes this then set_ruleset() should be
  called.
****************************************************************************/
void qtg_gui_set_rulesets(int num_rulesets, char **rulesets)
{
  /* PORTME */
}

/**************************************************************************
  Returns current client page
**************************************************************************/
enum client_pages qtg_get_current_client_page()
{
  return gui()->current_page();
}

/**************************************************************************
  update the start page.
**************************************************************************/
void update_start_page(void)
{
  /* PORTME */    
}
