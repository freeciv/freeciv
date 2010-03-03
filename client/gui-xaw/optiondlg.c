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
#include <X11/Xaw/List.h>
#include <X11/Xaw/Command.h>
#include <X11/Xaw/MenuButton.h>
#include <X11/Xaw/SimpleMenu.h>
#include <X11/Xaw/Scrollbar.h>
#include <X11/Xaw/SmeBSB.h>
#include <X11/Xaw/AsciiText.h>
#include <X11/Xaw/Toggle.h>
#include <X11/Xaw/Viewport.h>

/* utility */
#include "fcintl.h"
#include "shared.h"
#include "string_vector.h"
#include "support.h"

/* common */
#include "events.h"
#include "game.h"
#include "packets.h"
#include "player.h"

/* client */
#include "options.h"

/* client/gui-xaw */
#include "chatline.h"
#include "cityrep.h"
#include "gui_main.h"
#include "gui_stuff.h"
#include "mapview.h"
#include "repodlgs.h"

#include "optiondlg.h"

static Widget option_dialog_shell;

/******************************************************************/
void create_option_dialog(const char *name);

void option_ok_command_callback(Widget w, XtPointer client_data, 
			        XtPointer call_data);
void option_cancel_command_callback(Widget w, XtPointer client_data, 
				    XtPointer call_data);

/****************************************************************
... 
*****************************************************************/
void popup_option_dialog(const char *name)
{
  char valstr[64];
  void *gui_data;

  create_option_dialog(name);

  options_iterate(client_optset, poption) {
    gui_data = option_get_gui_data(poption);

    switch (option_type(poption)) {
    case OT_BOOLEAN:
      XtVaSetValues((Widget) gui_data, XtNstate, option_bool_get(poption),
                    XtNlabel, option_bool_get(poption) ? _("Yes") : _("No"),
                    NULL);
      break;
    case OT_INTEGER:
      my_snprintf(valstr, sizeof(valstr), "%d", option_int_get(poption));
      XtVaSetValues((Widget) gui_data, XtNstring, valstr, NULL);
      break;
    case OT_STRING:
      XtVaSetValues((Widget) gui_data,
                    option_str_values(poption) ? "label" : XtNstring,
                    option_str_get(poption), NULL);
      break;
    case OT_FONT:
      /* FIXME */
      break;
    }
  } options_iterate_end;

  xaw_set_relative_position(toplevel, option_dialog_shell, 25, 25);
  XtPopup(option_dialog_shell, XtGrabNone);
  XtSetSensitive(main_form, FALSE);
}



/****************************************************************
  Callback to change the entry for a string option that has
  a fixed list of possible entries.
*****************************************************************/
static void stropt_change_callback(Widget w,
				   XtPointer client_data,
				   XtPointer call_data)
{
  char* val = (char*)client_data;

  XtVaSetValues(XtParent(XtParent(w)), "label", val, NULL);
}

/****************************************************************
...
*****************************************************************/
void create_option_dialog(const char *name)
{
  Widget option_form, option_label;
  Widget option_viewport, option_scrollform;
  Widget option_ok_command, option_cancel_command;
  Widget prev_widget, longest_label = 0;
  size_t longest_len = 0;
  Dimension width;
  
  option_dialog_shell =
    I_T(XtCreatePopupShell("optionpopup", transientShellWidgetClass,
			   toplevel, NULL, 0));

  option_form = XtVaCreateManagedWidget("optionform", 
				        formWidgetClass, 
				        option_dialog_shell, NULL);   

  option_label =
    I_L(XtVaCreateManagedWidget("optionlabel", labelWidgetClass, 
				option_form, NULL));

  option_viewport =
    XtVaCreateManagedWidget("optionviewport", viewportWidgetClass,
			    option_form, NULL);

  option_scrollform =
    XtVaCreateManagedWidget("optionscrollform", formWidgetClass,
			    option_viewport, NULL);   

  prev_widget = NULL; /* init the prev-Widget */
  options_iterate(client_optset, poption) {
    const char *descr = option_description(poption);
    size_t len = strlen(descr);

    /* 
     * Remember widget so we can reset the vertical position; need to
     * do this because labels and toggles etc have different heights.
     */
    option_set_gui_data(poption, prev_widget);

    if (prev_widget) {
      prev_widget = 
	XtVaCreateManagedWidget("label", labelWidgetClass, option_scrollform,
				XtNlabel, descr,
				XtNfromVert, prev_widget,
				NULL);
    } else {
      prev_widget = 
	XtVaCreateManagedWidget("label", labelWidgetClass, option_scrollform,
				XtNlabel, descr,
				NULL);
    }

    /* 
     * The addition of a scrollbar screws things up. There must be a
     * better way to do this.
     */
    XtVaGetValues(prev_widget, XtNwidth, &width, NULL);
    XtVaSetValues(prev_widget, XtNwidth, width + 15, NULL);

    if (len > longest_len) {
      longest_len = len;
      longest_label = prev_widget;
    }
  } options_iterate_end;

  XtVaGetValues(longest_label, XtNwidth, &width, NULL);
  XtVaSetValues(option_label, XtNwidth, width + 15, NULL);

  options_iterate(client_optset, poption) {
    void *gui_data = option_get_gui_data(poption);

    /* 
     * At the start of the loop gui_data will contain the widget
     * which is above the label widget which is associated with this
     * option.
     */
    switch (option_type(poption)) {
    case OT_BOOLEAN:
      if (gui_data) {
	prev_widget =
	  XtVaCreateManagedWidget("toggle", toggleWidgetClass,
				  option_scrollform,
				  XtNfromHoriz, option_label,
				  XtNfromVert, gui_data,
				  NULL);
      } else {
	prev_widget =
	  XtVaCreateManagedWidget("toggle", toggleWidgetClass,
				  option_scrollform,
				  XtNfromHoriz, option_label,
				  NULL);
      }
      XtAddCallback(prev_widget, XtNcallback, toggle_callback, NULL);
      break;
    case OT_STRING:
      if (option_str_values(poption)) {
        const struct strvec *vals = option_str_values(poption);
	Widget popupmenu;

        if (gui_data) {
	  prev_widget =
            XtVaCreateManagedWidget(option_name(poption),
                                    menuButtonWidgetClass,
				    option_scrollform,
				    XtNfromHoriz, option_label,
				    XtNfromVert, gui_data,
				    NULL);
	} else {
	  prev_widget =
            XtVaCreateManagedWidget(option_name(poption),
                                    menuButtonWidgetClass,
				    option_scrollform,
				    XtNfromHoriz, option_label,
				    NULL);
	}

	popupmenu = XtVaCreatePopupShell("menu",
					 simpleMenuWidgetClass,
					 prev_widget,
					 NULL);

        strvec_iterate(vals, val) {
	  Widget entry = XtVaCreateManagedWidget(val, smeBSBObjectClass,
                                                 popupmenu, NULL);
          XtAddCallback(entry, XtNcallback, stropt_change_callback,
                        (XtPointer) val);
        } strvec_iterate_end;

        if (0 == strvec_size(vals)) {
          /* We could disable this if there was just one possible choice,
             too, but for values that are uninitialized (empty) this
             would be confusing. */
	  XtSetSensitive(prev_widget, FALSE);
	}

	/* There should be another way to set width of menu button */
	XtVaSetValues(prev_widget, XtNwidth, 120 ,NULL);

	break;
      }
      /* else fall through */
    case OT_INTEGER:
      if (gui_data) {
	prev_widget =
	  XtVaCreateManagedWidget("input", asciiTextWidgetClass,
				  option_scrollform,
				  XtNfromHoriz, option_label,
				  XtNfromVert, gui_data,
				  NULL);
      } else {
	prev_widget =
	  XtVaCreateManagedWidget("input", asciiTextWidgetClass,
				  option_scrollform,
				  XtNfromHoriz, option_label,
				  NULL);
      }
      break;
    case OT_FONT:
      /* FIXME */
      break;
    }

    /* store the final widget */
    option_set_gui_data(poption, (void *) prev_widget);
  } options_iterate_end;

  option_ok_command =
    I_L(XtVaCreateManagedWidget("optionokcommand", commandWidgetClass,
				option_form, XtNfromVert, option_viewport,
				NULL));
  
  option_cancel_command =
    I_L(XtVaCreateManagedWidget("optioncancelcommand", commandWidgetClass,
				option_form, XtNfromVert, option_viewport,
				NULL));
	
  XtAddCallback(option_ok_command, XtNcallback, 
		option_ok_command_callback, NULL);
  XtAddCallback(option_cancel_command, XtNcallback, 
		option_cancel_command_callback, NULL);

  XtRealizeWidget(option_dialog_shell);

  xaw_horiz_center(option_label);
}


/**************************************************************************
 Changes the label of the toggle widget to Yes/No depending on the state of
 the toggle.
**************************************************************************/
void toggle_callback(Widget w, XtPointer client_data, XtPointer call_data)
{
  Boolean b;

  XtVaGetValues(w, XtNstate, &b, NULL);
  XtVaSetValues(w, XtNlabel, b ? _("Yes") : _("No"), NULL);
}

/**************************************************************************
...
**************************************************************************/
void option_ok_command_callback(Widget w, XtPointer client_data, 
			       XtPointer call_data)
{
  Boolean b;
  int val;
  XtPointer dp;
  Widget gui_data;

  options_iterate(client_optset, poption) {
    gui_data = (Widget) option_get_gui_data(poption);

    switch (option_type(poption)) {
    case OT_BOOLEAN:
      XtVaGetValues(gui_data, XtNstate, &b, NULL);
      (void) option_bool_set(poption, b);
      break;
    case OT_INTEGER:
      XtVaGetValues(gui_data, XtNstring, &dp, NULL);
      sscanf(dp, "%d", &val);
      (void) option_int_set(poption, val);
      break;
    case OT_STRING:
      XtVaGetValues(gui_data,
                    option_str_values(poption) ? "label" : XtNstring,
                    &dp, NULL);
      (void) option_str_set(poption, dp);
      break;
    case OT_FONT:
      /* FIXME */
      break;
    }
  } options_iterate_end;

  XtSetSensitive(main_form, TRUE);
  XtDestroyWidget(option_dialog_shell);
}

/**************************************************************************
...
**************************************************************************/
void option_cancel_command_callback(Widget w, XtPointer client_data, 
				    XtPointer call_data)
{
  XtSetSensitive(main_form, TRUE);
  XtDestroyWidget(option_dialog_shell);
}

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
  if (poptset == server_optset) {
    /* FIXME: this is a big hack! */
    popdown_settable_options_dialog();
  } else {
    log_error("%s(): PORTME!", __FUNCTION__);
  }
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
