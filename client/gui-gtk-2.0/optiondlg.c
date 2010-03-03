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

/* utility */
#include "log.h"

/* client */
#include "options.h"

/* client/gui-gtk-2.0 */
#include "repodlgs.h"

#include "optiondlg.h"


/****************************************************************************
  Popup the option dialog for the option set.
  FIXME/PORTME
****************************************************************************/
void option_dialog_popup(const char *name, const struct option_set *poptset)
{
  if (poptset == client_optset) {
    /* FIXME: this is a big hack! */
    popup_option_dialog(name);
  } else if (poptset == server_optset) {
    popup_settable_options_dialog(name);
  } else {
    log_error("%s(): PORTME!", __FUNCTION__);
  }
}

/****************************************************************************
  Popdown the option dialog for the option set.
  FIXME/PORTME
****************************************************************************/
void option_dialog_popdown(const struct option_set *poptset)
{
  log_error("%s(): PORTME!", __FUNCTION__);
}

/****************************************************************************
  Update the GUI for the option.
  FIXME/PORTME
****************************************************************************/
void option_gui_update(const struct option *poption)
{
  log_error("%s(): PORTME!", __FUNCTION__);
}

/****************************************************************************
  Add the GUI for the option.
  FIXME/PORTME
****************************************************************************/
void option_gui_add(const struct option *poption)
{
  log_error("%s(): PORTME!", __FUNCTION__);
}

/****************************************************************************
  Remove the GUI for the option.
  FIXME/PORTME
****************************************************************************/
void option_gui_remove(const struct option *poption)
{
  log_error("%s(): PORTME!", __FUNCTION__);
}
