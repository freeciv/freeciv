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

#include <stdio.h>
#include <stdlib.h>

#include <X11/Intrinsic.h>
#include <X11/StringDefs.h>
#include <X11/Xaw/Form.h>
#include <X11/Xaw/Label.h>
#include <X11/Xaw/Command.h>
#include <X11/Xaw/SimpleMenu.h>
#include <X11/Xaw/List.h>

#include "fcintl.h"
#include "game.h"
#include "government.h"
#include "packets.h"
#include "player.h"
#include "shared.h"
#include "support.h"

#include "clinet.h"
#include "gui_main.h"
#include "gui_stuff.h"
#include "mapview.h"
#include "optiondlg.h"

#include "inteldlg.h"

/******************************************************************/
static Widget intel_dialog_shell;
static Widget intel_form;
static Widget intel_label;
static Widget intel_close_command;
static Widget intel_diplo_command;
static bool intel_dialog_shell_is_raised;

static Widget intel_diplo_dialog_shell;
static Widget intel_diplo_form;
static Widget intel_diplo_label;
static Widget intel_diplo_list;
static Widget intel_diplo_close_command;
static bool intel_diplo_dialog_shell_is_raised;
/******************************************************************/

void popdown_intel_dialog(void);
void create_intel_dialog(struct player *pplayer);

void intel_close_callback(Widget w, XtPointer client_data,
			  XtPointer call_data);
void intel_diplo_callback(Widget w, XtPointer client_data,
			  XtPointer call_data);

void popup_intel_diplo_dialog(struct player *pplayer, bool raise);
void popdown_intel_diplo_dialog(void);
void create_intel_diplo_dialog(struct player *pplayer, bool raise);
void update_intel_diplo_dialog(struct player *pplayer);
void intel_diplo_close_callback(Widget w, XtPointer client_data,
				XtPointer call_data);

/****************************************************************
  Popup an intelligence dialog for the given player.
*****************************************************************/
void popup_intel_dialog(struct player *p)
{
  intel_dialog_shell_is_raised = TRUE;
  if (!intel_dialog_shell) {
    create_intel_dialog(p);
  }
  xaw_set_relative_position(toplevel, intel_dialog_shell, 25, 25);
  XtPopup(intel_dialog_shell, XtGrabNone);
  if (intel_dialog_shell_is_raised) {
    XtSetSensitive(main_form, FALSE);
  }
}

/****************************************************************
  Close an intelligence dialog.
*****************************************************************/
void popdown_intel_dialog(void)
{
  if (intel_dialog_shell) {
    if (intel_dialog_shell_is_raised) {
      XtSetSensitive(main_form, TRUE);
    }
    XtDestroyWidget(intel_dialog_shell);
    intel_dialog_shell = 0;
  }
}

/****************************************************************
...
*****************************************************************/
void create_intel_dialog(struct player *pplayer)
{
  char buf[64];
  struct city *pcity;

  static char *tech_list_names_ptrs[A_LAST+1];
  static char tech_list_names[A_LAST+1][200];
  int i, j;


  I_T(intel_dialog_shell = XtCreatePopupShell("intelpopup", 
					      transientShellWidgetClass,
					      toplevel, NULL, 0));

  intel_form = XtVaCreateManagedWidget("intelform", 
				       formWidgetClass, 
				       intel_dialog_shell, NULL);

  my_snprintf(buf, sizeof(buf),
	      _("Intelligence Information for the %s Empire"), 
	      get_nation_name(pplayer->nation));

  intel_label = I_L(XtVaCreateManagedWidget("inteltitlelabel", 
					    labelWidgetClass, 
					    intel_form, 
					    XtNlabel, buf,
					    NULL));

  my_snprintf(buf, sizeof(buf), _("Ruler: %s %s"), 
	      get_ruler_title(pplayer->government, pplayer->is_male,
			      pplayer->nation),
	      pplayer->name);
  XtVaCreateManagedWidget("intelnamelabel", 
			  labelWidgetClass, 
			  intel_form, 
			  XtNlabel, buf,
			  NULL);   

  my_snprintf(buf, sizeof(buf),
	      _("Government: %s"), get_government_name(pplayer->government));
  XtVaCreateManagedWidget("intelgovlabel", 
			  labelWidgetClass, 
			  intel_form, 
			  XtNlabel, buf,
			  NULL);   

  my_snprintf(buf, sizeof(buf), _("Gold: %d"), pplayer->economic.gold);
  XtVaCreateManagedWidget("intelgoldlabel", 
			  labelWidgetClass, 
			  intel_form, 
			  XtNlabel, buf,
			  NULL);   

  my_snprintf(buf, sizeof(buf), _("Tax: %d%%"), pplayer->economic.tax);
  XtVaCreateManagedWidget("inteltaxlabel", 
			  labelWidgetClass, 
			  intel_form, 
			  XtNlabel, buf,
			  NULL);   

  my_snprintf(buf, sizeof(buf), _("Science: %d%%"),
	      pplayer->economic.science);
  XtVaCreateManagedWidget("intelscilabel", 
			  labelWidgetClass, 
			  intel_form, 
			  XtNlabel, buf,
			  NULL);   

  my_snprintf(buf, sizeof(buf), _("Luxury: %d%%"), pplayer->economic.luxury);
  XtVaCreateManagedWidget("intelluxlabel", 
			  labelWidgetClass, 
			  intel_form, 
			  XtNlabel, buf,
			  NULL);   

  if (pplayer->research.researching == A_UNSET) {
    my_snprintf(buf, sizeof(buf), _("Researching: %s(%d/%d)"),
		advances[A_NONE].name,
		pplayer->research.bulbs_researched,
		total_bulbs_required(pplayer));
  } else {
    my_snprintf(buf, sizeof(buf), _("Researching: %s(%d/%d)"),
		get_tech_name(pplayer, pplayer->research.researching),
		pplayer->research.bulbs_researched,
		total_bulbs_required(pplayer));
  }

  XtVaCreateManagedWidget("intelreslabel", 
			  labelWidgetClass, 
			  intel_form, 
			  XtNlabel, buf,
			  NULL);   

  pcity = find_palace(pplayer);
  my_snprintf(buf, sizeof(buf), _("Capital: %s"),
	      (!pcity)?_("(Unknown)"):pcity->name);
  XtVaCreateManagedWidget("intelcapitallabel", 
			  labelWidgetClass, 
			  intel_form, 
			  XtNlabel, buf,
			  NULL);   

  for(i=A_FIRST, j=0; i<game.num_tech_types; i++)
    if (get_invention(pplayer, i) == TECH_KNOWN) {
      if(get_invention(game.player_ptr, i)==TECH_KNOWN) {
	sz_strlcpy(tech_list_names[j], advances[i].name);
      } else {
	my_snprintf(tech_list_names[j], sizeof(tech_list_names[j]),
		    "%s*", advances[i].name);
      }
      tech_list_names_ptrs[j]=tech_list_names[j];
      j++;
    }
  tech_list_names_ptrs[j]=0;

  XtVaCreateManagedWidget("inteltechlist", 
			  listWidgetClass,
			  intel_form,
			  XtNlist, tech_list_names_ptrs,
			  NULL);

  intel_close_command =
    I_L(XtVaCreateManagedWidget("intelclosecommand", 
				commandWidgetClass,
				intel_form, NULL));

  intel_diplo_command =
    I_L(XtVaCreateManagedWidget("inteldiplocommand",
				commandWidgetClass,
				intel_form, NULL));

  XtAddCallback(intel_close_command, XtNcallback,
		intel_close_callback, NULL);
  XtAddCallback(intel_diplo_command, XtNcallback,
		intel_diplo_callback, INT_TO_XTPOINTER(pplayer->player_no));
  XtRealizeWidget(intel_dialog_shell);

  xaw_horiz_center(intel_label);
}


/**************************************************************************
...
**************************************************************************/
void intel_close_callback(Widget w, XtPointer client_data,
			  XtPointer call_data)
{
/*  XtSetSensitive(main_form, TRUE);
  XtDestroyWidget(intel_dialog_shell);
  intel_dialog_shell=0;
*/
  popdown_intel_dialog();
}

/**************************************************************************
...
**************************************************************************/
void intel_diplo_callback(Widget w, XtPointer client_data,
			  XtPointer call_data)
{
  popup_intel_diplo_dialog(&game.players[XTPOINTER_TO_INT(client_data)],
			   FALSE);
}

/****************************************************************************
  Update the intelligence dialog for the given player.  This is called by
  the core client code when that player's information changes.
****************************************************************************/
void update_intel_dialog(struct player *p)
{
  /* PORTME */
}

/****************************************************************
  Popup a player's relations dialog for the given player.
*****************************************************************/
void popup_intel_diplo_dialog(struct player *pplayer, bool raise)
{
  if (!intel_diplo_dialog_shell) {
    intel_diplo_dialog_shell_is_raised = raise;
    create_intel_diplo_dialog(pplayer, intel_diplo_dialog_shell_is_raised);
  }
  xaw_set_relative_position(toplevel, intel_diplo_dialog_shell, 25, 25);
  XtPopup(intel_diplo_dialog_shell, XtGrabNone);
  if (intel_diplo_dialog_shell_is_raised) {
    XtSetSensitive(main_form, FALSE);
  }
}

/****************************************************************
  Close a player's relations dialog.
*****************************************************************/
void popdown_intel_diplo_dialog(void)
{
  if (intel_diplo_dialog_shell) {
    if (intel_diplo_dialog_shell_is_raised) {
      XtSetSensitive(main_form, TRUE);
    }
    XtDestroyWidget(intel_diplo_dialog_shell);
    intel_diplo_dialog_shell = 0;
  }
}

/****************************************************************
  Create a player's relations dialog for the given player.
*****************************************************************/
void create_intel_diplo_dialog(struct player *pplayer, bool raise)
{
  char buf[64];

  intel_diplo_dialog_shell =
    I_IN(I_T(XtCreatePopupShell("inteldiplopopup",
				raise ? transientShellWidgetClass
				: topLevelShellWidgetClass,
				toplevel, NULL, 0)));

  intel_diplo_form = XtVaCreateManagedWidget("inteldiploform",
					     formWidgetClass,
					     intel_diplo_dialog_shell,
					     NULL);

  my_snprintf(buf, sizeof(buf),
	      _("Intelligence Diplomacy Information for the %s Empire"),
	      get_nation_name(pplayer->nation));

  intel_diplo_label = I_L(XtVaCreateManagedWidget("inteldiplolabel",
						  labelWidgetClass,
						  intel_diplo_form,
						  XtNlabel, buf,
						  NULL));

   
  my_snprintf(buf, sizeof(buf), _("Ruler: %s %s"), 
	      get_ruler_title(pplayer->government, pplayer->is_male,
			      pplayer->nation),
	      pplayer->name);
  XtVaCreateManagedWidget("inteldiplonamelabel", 
			  labelWidgetClass, 
			  intel_diplo_form, 
			  XtNlabel, buf,
			  NULL);   

  intel_diplo_list = XtVaCreateManagedWidget("inteldiplolist",
					     listWidgetClass,
					     intel_diplo_form,
					     NULL);

  intel_diplo_close_command =
    I_L(XtVaCreateManagedWidget("inteldiploclosecommand",
				commandWidgetClass,
				intel_diplo_form,
				NULL));

  XtAddCallback(intel_diplo_close_command, XtNcallback,
		intel_diplo_close_callback, NULL);

  update_intel_diplo_dialog(pplayer);

  XtRealizeWidget(intel_diplo_dialog_shell);
  
  XSetWMProtocols(display, XtWindow(intel_diplo_dialog_shell), 
		  &wm_delete_window, 1);
  XtOverrideTranslations(intel_diplo_dialog_shell,
    XtParseTranslationTable("<Message>WM_PROTOCOLS: msg-close-intel-diplo()"));
}

/****************************************************************
  Update a player's relations dialog for the given player.
*****************************************************************/
void update_intel_diplo_dialog(struct player *pplayer)
{
  int i;
  Dimension width;
  static char *namelist_ptrs[MAX_NUM_PLAYERS];
  static char namelist_text[MAX_NUM_PLAYERS][72];
  const struct player_diplstate *state;

  if (intel_diplo_dialog_shell) {
    i = 0;
    players_iterate(other) {
      if (other == pplayer) {
	continue;
      }
      state = pplayer_get_diplstate(pplayer, other);
      my_snprintf(namelist_text[i], sizeof(namelist_text[i]),
		  "%-32s %-16s %-16s",
		  other->name,
		  get_nation_name(other->nation),
		  diplstate_text(state->type));
      namelist_ptrs[i] = namelist_text[i];
      i++;
    } players_iterate_end;

    XawListChange(intel_diplo_list, namelist_ptrs, i, 0, True);

    XtVaGetValues(intel_diplo_list, XtNwidth, &width, NULL);
    XtVaSetValues(intel_diplo_label, XtNwidth, width, NULL); 
  }
}

/**************************************************************************
...
**************************************************************************/
void intel_diplo_close_callback(Widget w, XtPointer client_data,
			  XtPointer call_data)
{
  popdown_intel_diplo_dialog();
}

/**************************************************************************
...
**************************************************************************/
void intel_diplo_dialog_msg_close(Widget w)
{
  intel_diplo_close_callback(w, NULL, NULL);
}
