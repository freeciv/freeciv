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
#ifndef FC__GUI_MAIN_H
#define FC__GUI_MAIN_H

#include <X11/Intrinsic.h>

#include "gui_main_g.h"

void setup_widgets(void);

void quit_civ(Widget w, XtPointer client_data, XtPointer call_data);
void end_turn_callback(Widget w, XtPointer client_data, XtPointer call_data);
void remove_net_callback(void);

/**************************************************************************/
/* FIXME */

/* The following function is in helpdlg.c, but currently there is no
 * good place to put this prototype --dwp  */
void close_help_dialog_action(Widget w, XEvent *event, 
			      String *argv, Cardinal *argc);

/* The following function is in plrdlg.c, but currently there is no
 * good place to put this prototype --dwp  */
void close_players_dialog_action(Widget w, XEvent *event, 
				 String *argv, Cardinal *argc);

/* The following function is in messagewin.c, but currently there is no
 * good place to put this prototype --dwp  */
void close_meswin_dialog_action(Widget w, XEvent *event, 
				String *argv, Cardinal *argc);

/* The following function is in cityrep.c, but currently there is no
 * good place to put this prototype --dwp  */
void close_city_report_action(Widget w, XEvent *event, 
			      String *argv, Cardinal *argc);

#endif  /* FC__GUI_MAIN_H */
