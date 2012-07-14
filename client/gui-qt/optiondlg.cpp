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
#include <fc_config.h>
#endif

// utility
#include "log.h"

// gui-qt
#include "qtg_cxxside.h"

#include "optiondlg.h"

// client
#include "options.h"


#define SPECLIST_TAG option_dialog
#define SPECLIST_TYPE struct option_dialog
#include "speclist.h"
#define option_dialogs_iterate(pdialog) \
  TYPED_LIST_ITERATE(struct option_dialog, option_dialogs, pdialog)
#define option_dialogs_iterate_end  LIST_ITERATE_END

enum {
  RESPONSE_CANCEL,
  RESPONSE_OK,
  RESPONSE_APPLY,
  RESPONSE_RESET,
  RESPONSE_REFRESH,
  RESPONSE_SAVE
};

/****************************************************************************
  Popup the option dialog for the option set.
  FIXME/PORTME
****************************************************************************/
void option_dialog_popup(const char *name, const struct option_set *poptset)
{
  log_error("%s(): PORTME!", __FUNCTION__);
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
void option_gui_update(struct option *poption)
{
  log_error("%s(): PORTME!", __FUNCTION__);
}

/****************************************************************************
  Add the GUI for the option.
  FIXME/PORTME
****************************************************************************/
void option_gui_add(struct option *poption)
{
  log_error("%s(): PORTME!", __FUNCTION__);
}

/****************************************************************************
  Remove the GUI for the option.
  FIXME/PORTME
****************************************************************************/
void option_gui_remove(struct option *poption)
{
  log_error("%s(): PORTME!", __FUNCTION__);
}
