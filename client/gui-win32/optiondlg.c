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
#include <windows.h>
#include <windowsx.h>

/* utility */
#include "fcintl.h"
#include "string_vector.h"
#include "support.h"

/* common */
#include "game.h"

/* client */
#include "options.h"

/* gui-win32 */
#include "gui_stuff.h"
#include "gui_main.h"

#include "optiondlg.h"

static HWND option_dialog;

/****************************************************************

*****************************************************************/
static LONG CALLBACK option_proc(HWND dlg,UINT message,
				 WPARAM wParam,LPARAM lParam)
{
  switch(message) {
  case WM_CREATE:
  case WM_SIZE:
  case WM_GETMINMAXINFO:
    break;
  case WM_DESTROY:
    option_dialog=NULL;
    break;
  case WM_COMMAND:
    if (LOWORD(wParam)==IDOK) {
      void *gui_data;
      char dp[512];
      int val;
      
      client_options_iterate(poption) {
        gui_data = option_get_gui_data(poption);

	switch (option_type(poption)) {
	case COT_BOOLEAN:
          (void) option_bool_set(poption,
                                 Button_GetCheck((HWND)(gui_data))
                                 == BST_CHECKED);
	  break;
	case COT_INTEGER:
	  GetWindowText((HWND)(gui_data),dp,sizeof(dp));
          if (str_to_int(dp, &val)) {
            (void) option_int_set(poption, val);
          }
	  break;
	case COT_STRING:
	  if (!gui_data) {
	    break;
	  }
	  GetWindowText((HWND) (gui_data), dp, sizeof(dp));
          (void) option_str_set(poption, dp);
	  break;
        case COT_FONT:
          /* FIXME: */
          break;
	}
      } client_options_iterate_end;
      DestroyWindow(dlg);
    }
    break;
  case WM_CLOSE:
    DestroyWindow(dlg);
    break;
  default:
    return DefWindowProc(dlg,message,wParam,lParam);
  }
  return 0;
}

/****************************************************************
... 
*****************************************************************/
static void create_option_dialog(void)
{
  struct fcwin_box *hbox;
  struct fcwin_box *vbox_labels;
  struct fcwin_box *vbox;
  option_dialog=fcwin_create_layouted_window(option_proc,
					     _("Set local options"),
					     WS_OVERLAPPEDWINDOW,
					     CW_USEDEFAULT,CW_USEDEFAULT,
					     root_window,NULL,
					     JUST_CLEANUP,
					     NULL);

  hbox=fcwin_hbox_new(option_dialog,FALSE);
  vbox=fcwin_vbox_new(option_dialog,TRUE);
  vbox_labels=fcwin_vbox_new(option_dialog,TRUE);
  client_options_iterate(poption) {
    switch (option_type(poption)) {
    case COT_BOOLEAN:
      fcwin_box_add_static(vbox_labels, option_description(poption),
			   0,SS_LEFT,TRUE,TRUE,0);
      option_set_gui_data(poption, (void *)
                          fcwin_box_add_checkbox(vbox," ",0,0,TRUE,TRUE,0));
      break;
    case COT_INTEGER:
      fcwin_box_add_static(vbox_labels,option_description(poption),
			   0,SS_LEFT,TRUE,TRUE,0);
      option_set_gui_data(poption, (void *)
                          fcwin_box_add_edit(vbox,"",6,0,0,TRUE,TRUE,0));
      break;
    case COT_STRING:
      fcwin_box_add_static(vbox_labels,option_description(poption),
			   0,SS_LEFT,TRUE,TRUE,0);
      if (option_str_values(poption)) {
        const struct strvec *vals = option_str_values(poption);

	if (0 == strvec_size(vals)) {
	  fcwin_box_add_static(vbox, option_str_get(poption), 0, SS_LEFT,
			       TRUE, TRUE, 0);
          option_set_gui_data(poption, NULL);
	} else {
          option_set_gui_data(poption, (void *)
                              fcwin_box_add_combo(vbox, 5, 0,
                              WS_VSCROLL | CBS_DROPDOWNLIST | CBS_SORT,
                              TRUE, TRUE, 0));

          strvec_iterate(vals, val) {
            ComboBox_AddString(option_get_gui_data(poption), val);
          } strvec_iterate_end;
	}
      } else {
        option_set_gui_data(poption, (void *)
                            fcwin_box_add_edit(vbox, "", 40, 0, 0,
                                               TRUE, TRUE, 0));
	break;
      }
     case COT_FONT:
       /* FIXME: */
       break;
    }
  } client_options_iterate_end;
  fcwin_box_add_box(hbox,vbox_labels,TRUE,TRUE,0);
  fcwin_box_add_box(hbox,vbox,TRUE,TRUE,0);
  vbox=fcwin_vbox_new(option_dialog,FALSE);
  fcwin_box_add_box(vbox,hbox,TRUE,FALSE,10);
  fcwin_box_add_button(vbox,_("Close"),IDOK,0,FALSE,FALSE,5);
  fcwin_set_box(option_dialog,vbox);
}

/****************************************************************
... 
*****************************************************************/
void popup_option_dialog(void)
{
  char valstr[64];
  void *gui_data;

  if (!option_dialog)
    create_option_dialog();

  client_options_iterate(poption) {
    gui_data = option_get_gui_data(poption);

    switch (option_type(poption)) {
    case COT_BOOLEAN:
      Button_SetCheck((HWND)(gui_data),
		      option_bool_get(poption)?BST_CHECKED:BST_UNCHECKED);
      break;
    case COT_INTEGER:
      fc_snprintf(valstr, sizeof(valstr), "%d", option_int_get(poption));
      SetWindowText((HWND)(gui_data), valstr);
      break;
    case COT_STRING:
      if (!gui_data) {
	break;
      }

      if (option_str_values(poption) && option_str_get(poption)[0] != 0) {
	int i =
	    ComboBox_FindStringExact(gui_data, 0, option_str_get(poption));

	if (i == CB_ERR) {
	  i = ComboBox_AddString(gui_data, option_str_get(poption));
	}
	ComboBox_SetCurSel(gui_data, i);
      } 
      SetWindowText((HWND)(gui_data), option_str_get(poption));
      break;
    case COT_FONT:
      /* FIXME: */
      break;
    }
  } client_options_iterate_end;
  fcwin_redo_layout(option_dialog);
  ShowWindow(option_dialog,SW_SHOWNORMAL);
}
