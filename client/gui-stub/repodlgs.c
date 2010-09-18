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

#include <stdlib.h>

#include "repodlgs_common.h"

#include "cityrep.h"

#include "repodlgs.h"

/**************************************************************************
  Update all report dialogs.
**************************************************************************/
void update_report_dialogs(void)
{
  if (!is_report_dialogs_frozen()) {
    economy_report_dialog_update();
    city_report_dialog_update(); 
    science_dialog_update();
  }
}

/**************************************************************************
  Update the science report.
**************************************************************************/
void science_dialog_update(void)
{
  /* PORTME */
}

/**************************************************************************
  Display the science report.  Optionally raise it.
  Typically triggered by F6.
**************************************************************************/
void popup_science_dialog(bool raise)
{
  /* PORTME */
}

/**************************************************************************
  Update the economy report.
**************************************************************************/
void economy_report_dialog_update(void)
{
  /* PORTME */
}

/**************************************************************************
  Display the economy report.  Optionally raise it.
  Typically triggered by F5.
**************************************************************************/
void popup_economy_report_dialog(bool raise)
{
  /* PORTME */
}

/**************************************************************************
  Update the units report.
**************************************************************************/
void real_units_report_dialog_update(void)
{
  /* PORTME */
}

/**************************************************************************
  Display the units report.  Optionally raise it.
  Typically triggered by F2.
**************************************************************************/
void units_report_dialog_popup(bool raise)
{
  /* PORTME */
}

/****************************************************************
  Show a dialog with player statistics at endgame.
*****************************************************************/
void popup_endgame_report_dialog(struct packet_endgame_report *packet)
{
  /* PORTME */
}
