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
#include "log.h"
#include "mem.h"
#include "shared.h"
#include "string_vector.h"
#include "support.h"

/* common */
#include "events.h"

/* client */
#include "options.h"

/* client/gui-gtk-2.0 */
#include "gui_main.h"
#include "gui_stuff.h"

#include "optiondlg.h"


/* The option dialog data. */
struct option_dialog {
  const struct option_set *poptset;     /* The option set. */
  Widget shell;                         /* The main widget. */
};

#define SPECLIST_TAG option_dialog
#define SPECLIST_TYPE struct option_dialog
#include "speclist.h"
#define option_dialogs_iterate(pdialog) \
  TYPED_LIST_ITERATE(struct option_dialog, option_dialogs, pdialog)
#define option_dialogs_iterate_end  LIST_ITERATE_END

/* All option dialog are set on this list. */
static struct option_dialog_list *option_dialogs = NULL;

/* Option dialog main functions. */
static struct option_dialog *
option_dialog_get(const struct option_set *poptset);
static struct option_dialog *
option_dialog_new(const char *name, const struct option_set *poptset);
static void option_dialog_destroy(struct option_dialog *pdialog);

static inline void option_dialog_foreach(struct option_dialog *pdialog,
                                         void (*option_action)
                                         (struct option *));

static void option_dialog_option_refresh(struct option *poption);
static void option_dialog_option_apply(struct option *poption);


/****************************************************************************
 Changes the label of the toggle widget to Yes/No depending on the state of
 the toggle.
****************************************************************************/
void toggle_callback(Widget w, XtPointer client_data, XtPointer call_data)
{
  Boolean b;

  XtVaGetValues(w, XtNstate, &b, NULL);
  XtVaSetValues(w, XtNlabel, b ? _("Yes") : _("No"), NULL);
}

/****************************************************************************
  Callback to change the entry for a string option that has a fixed list
  of possible entries.
****************************************************************************/
static void stropt_change_callback(Widget w, XtPointer client_data,
                                   XtPointer call_data)
{
  char *val = (char *) client_data;

  XtVaSetValues(XtParent(XtParent(w)), "label", val, NULL);
}

/****************************************************************************
  OK button callback.
****************************************************************************/
static void option_dialog_ok_callback(Widget w, XtPointer client_data,
                                      XtPointer call_data)
{
  struct option_dialog *pdialog = (struct option_dialog *) client_data;

  option_dialog_foreach(pdialog, option_dialog_option_apply);
  option_dialog_destroy(pdialog);
}

/****************************************************************************
  Cancel button callback.
****************************************************************************/
static void option_dialog_cancel_callback(Widget w, XtPointer client_data,
                                          XtPointer call_data)
{
  struct option_dialog *pdialog = (struct option_dialog *) client_data;

  option_dialog_destroy(pdialog);
}

/****************************************************************************
  Returns the option dialog which fit the option set.
****************************************************************************/
static struct option_dialog *
option_dialog_get(const struct option_set *poptset)
{
  if (NULL != option_dialogs) {
    option_dialogs_iterate(pdialog) {
      if (pdialog->poptset == poptset) {
        return pdialog;
      }
    } option_dialogs_iterate_end;
  }
  return NULL;
}

/****************************************************************************
  Creates a new option dialog.
****************************************************************************/
static struct option_dialog *
option_dialog_new(const char *name, const struct option_set *poptset)
{
  struct option_dialog *pdialog;
  Widget form, viewport, command, scrollform, label, widget;
  Dimension width, longest_width = 0;

  /* Create the dialog structure. */
  pdialog = fc_malloc(sizeof(*pdialog));
  pdialog->poptset = poptset;
  pdialog->shell = I_T(XtCreatePopupShell("settableoptionspopup",
                                          transientShellWidgetClass,
                                          toplevel, NULL, 0));
  /* Append to the option dialog list. */
  if (NULL == option_dialogs) {
    option_dialogs = option_dialog_list_new();
  }
  option_dialog_list_append(option_dialogs, pdialog);

  form = XtVaCreateManagedWidget("settableoptionsform", formWidgetClass,
                                 pdialog->shell, NULL);
  label = I_L(XtVaCreateManagedWidget("settableoptionslabel",
                                      labelWidgetClass, form, NULL));
  viewport = XtVaCreateManagedWidget("settableoptionsviewport",
                                     viewportWidgetClass, form, NULL);
  scrollform = XtVaCreateManagedWidget("settableoptionsscrollform",
                                       formWidgetClass, viewport, NULL);

  /* Add option labels. */
  widget = NULL;
  options_iterate(poptset, poption) {
    if (NULL != widget) {
      option_set_gui_data(poption, widget);     /* Set previous label. */

      widget = XtVaCreateManagedWidget("label", labelWidgetClass,
                                       scrollform, XtNlabel,
                                       option_description(poption),
                                       XtNfromVert, widget, NULL);
    } else {
      widget = XtVaCreateManagedWidget("label", labelWidgetClass,
                                       scrollform, XtNlabel,
                                       option_description(poption), NULL);
    }

    /* The addition of a scrollbar screws things up. There must be a
     * better way to do this. */
    XtVaGetValues(widget, XtNwidth, &width, NULL);
    width += 15;
    XtVaSetValues(widget, XtNwidth, width, NULL);

    if (width > longest_width) {
      longest_width = width;
    }
  } options_iterate_end;

  /* Resize label. */
  XtVaSetValues(label, XtNwidth, longest_width + 15, NULL);

  /* Add option widgets. */
  options_iterate(poptset, poption) {
    widget = option_get_gui_data(poption);      /* Get previous label. */
    option_set_gui_data(poption, NULL);

    switch (option_type(poption)) {
    case OT_BOOLEAN:
      if (NULL != widget) {
        widget = XtVaCreateManagedWidget("toggle", toggleWidgetClass,
                                         scrollform, XtNfromHoriz,
                                         label, XtNfromVert, widget, NULL);
      } else {
        widget = XtVaCreateManagedWidget("toggle", toggleWidgetClass,
                                         scrollform, XtNfromHoriz, label,
                                         NULL);
      }
      XtAddCallback(widget, XtNcallback, toggle_callback, NULL);
      option_set_gui_data(poption, widget);
      break;
    case OT_STRING:
      if (option_str_values(poption)) {
        const struct strvec *vals = option_str_values(poption);
        Widget popupmenu;

        if (NULL != widget) {
          widget = XtVaCreateManagedWidget(option_name(poption),
                                           menuButtonWidgetClass, scrollform,
                                           XtNfromHoriz, label, XtNfromVert,
                                           widget, NULL);
        } else {
          widget = XtVaCreateManagedWidget(option_name(poption),
                                           menuButtonWidgetClass, scrollform,
                                           XtNfromHoriz, label, NULL);
        }

        popupmenu = XtVaCreatePopupShell("menu", simpleMenuWidgetClass,
                                         widget, NULL);

        strvec_iterate(vals, val) {
          Widget entry = XtVaCreateManagedWidget(val, smeBSBObjectClass,
                                                 popupmenu, NULL);
          XtAddCallback(entry, XtNcallback, stropt_change_callback,
                        (XtPointer) val);
        } strvec_iterate_end;

        if (0 == strvec_size(vals)) {
          /* We could disable this if there was just one possible choice,
           * too, but for values that are uninitialized (empty) this
           * would be confusing. */
          XtSetSensitive(widget, FALSE);
        }

        /* There should be another way to set width of menu button */
        XtVaSetValues(widget, XtNwidth, 120 ,NULL);
        option_set_gui_data(poption, widget);
        break;
      }
      /* Else fallthrough. */
    case OT_INTEGER:
      if (NULL != widget) {
        widget = XtVaCreateManagedWidget("input", asciiTextWidgetClass,
                                         scrollform, XtNfromHoriz, label,
                                         XtNfromVert, widget, NULL);
      } else {
        widget = XtVaCreateManagedWidget("input", asciiTextWidgetClass,
                                         scrollform, XtNfromHoriz, label,
                                         NULL);
      }
      option_set_gui_data(poption, widget);
      break;
    case OT_ENUM:
    case OT_BITWISE:
    case OT_FONT:
    case OT_COLOR:
    case OT_VIDEO_MODE:
      log_error("Option type %s (%d) not supported yet.",
                option_type_name(option_type(poption)),
                option_type(poption));
      break;
    }
  } options_iterate_end;

  /* Add buttons. */
  command = I_L(XtVaCreateManagedWidget("settableoptionsokcommand",
                                        commandWidgetClass, form, NULL));
  XtAddCallback(command, XtNcallback, option_dialog_ok_callback, pdialog);

  command = I_L(XtVaCreateManagedWidget("settableoptionscancelcommand",
                                        commandWidgetClass, form, NULL));
  XtAddCallback(command, XtNcallback,
                option_dialog_cancel_callback, pdialog);

  XtRealizeWidget(pdialog->shell);
  xaw_horiz_center(label);
  XtSetSensitive(main_form, FALSE);
  xaw_set_relative_position(toplevel, pdialog->shell, 25, 25);
  XtPopup(pdialog->shell, XtGrabNone);

  return pdialog;
}

/****************************************************************************
  Destroys an option dialog.
****************************************************************************/
static void option_dialog_destroy(struct option_dialog *pdialog)
{
  if (NULL != option_dialogs) {
    option_dialog_list_remove(option_dialogs, pdialog);
  }

  options_iterate(pdialog->poptset, poption) {
    option_set_gui_data(poption, NULL);
  } options_iterate_end;

  XtDestroyWidget(pdialog->shell);
  free(pdialog);
  XtSetSensitive(main_form, TRUE);
}

/****************************************************************************
  Do an action for all options of the option dialog.
****************************************************************************/
static inline void option_dialog_foreach(struct option_dialog *pdialog,
                                         void (*option_action)
                                         (struct option *))
{
  fc_assert_ret(NULL != pdialog);

  options_iterate(pdialog->poptset, poption) {
    option_action(poption);
  } options_iterate_end;
}

/****************************************************************************
  Update an option in the option dialog.
****************************************************************************/
static void option_dialog_option_refresh(struct option *poption)
{
  Widget widget = (Widget) option_get_gui_data(poption);

  switch (option_type(poption)) {
  case OT_BOOLEAN:
    XtVaSetValues(widget, XtNstate, option_bool_get(poption),
                  XtNlabel, option_bool_get(poption) ? _("Yes") : _("No"),
                  NULL);
  break;
  case OT_INTEGER:
    {
      char valstr[64];

      fc_snprintf(valstr, sizeof(valstr), "%d", option_int_get(poption));
      XtVaSetValues(widget, XtNstring, valstr, NULL);
    }
    break;
  case OT_STRING:
    XtVaSetValues(widget, option_str_values(poption) ? "label"
                  : XtNstring, option_str_get(poption), NULL);
    break;
  case OT_ENUM:
  case OT_BITWISE:
  case OT_FONT:
  case OT_COLOR:
  case OT_VIDEO_MODE:
    log_error("Option type %s (%d) not supported yet.",
              option_type_name(option_type(poption)),
              option_type(poption));
    break;
  }

  XtSetSensitive(widget, option_is_changeable(poption));
}

/****************************************************************************
  Apply the option change.
****************************************************************************/
static void option_dialog_option_apply(struct option *poption)
{
  if (option_is_changeable(poption)) {
    Widget widget = (Widget) option_get_gui_data(poption);

    switch (option_type(poption)) {
    case OT_BOOLEAN:
      {
        Boolean b;

        XtVaGetValues(widget, XtNstate, &b, NULL);
        option_bool_set(poption, b);
      }
      break;
    case OT_INTEGER:
      {
        XtPointer dp;
        int val;

        XtVaGetValues(widget, XtNstring, &dp, NULL);
        if (str_to_int(dp, &val)) {
          option_int_set(poption, val);
        }
      }
      break;
    case OT_STRING:
      {
        XtPointer dp;

        XtVaGetValues(widget, option_str_values(poption) ? "label"
                      : XtNstring, &dp, NULL);
        option_str_set(poption, dp);
      }
      break;
    case OT_ENUM:
    case OT_BITWISE:
    case OT_FONT:
    case OT_COLOR:
    case OT_VIDEO_MODE:
      log_error("Option type %s (%d) not supported yet.",
                option_type_name(option_type(poption)),
                option_type(poption));
      break;
    }
  }
}

/****************************************************************************
  Popup the option dialog for the option set.
****************************************************************************/
void option_dialog_popup(const char *name, const struct option_set *poptset)
{
  struct option_dialog *pdialog = option_dialog_get(poptset);

  if (NULL != pdialog) {
    option_dialog_foreach(pdialog, option_dialog_option_refresh);
  } else {
    (void) option_dialog_new(name, poptset);
  }
}

/****************************************************************************
  Popdown the option dialog for the option set.
****************************************************************************/
void option_dialog_popdown(const struct option_set *poptset)
{
  struct option_dialog *pdialog = option_dialog_get(poptset);

  if (NULL != pdialog) {
    option_dialog_destroy(pdialog);
  }
}

/****************************************************************************
  Update the GUI for the option.
****************************************************************************/
void option_gui_update(struct option *poption)
{
  struct option_dialog *pdialog = option_dialog_get(option_optset(poption));

  if (NULL != pdialog) {
    option_dialog_option_refresh(poption);
  }
}

/****************************************************************************
  Add the GUI for the option.
****************************************************************************/
void option_gui_add(struct option *poption)
{
  /* We cannot currently insert new option widgets. */
  option_dialog_popdown(option_optset(poption));
}

/****************************************************************************
  Remove the GUI for the option.
****************************************************************************/
void option_gui_remove(struct option *poption)
{
  /* We cannot currently remove new option widgets. */
  option_dialog_popdown(option_optset(poption));
}
