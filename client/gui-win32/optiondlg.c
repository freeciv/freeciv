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
#include <windows.h>
#include <windowsx.h>

#include "fcintl.h"
#include "game.h"
#include "support.h"
#include "gui_stuff.h"
#include "gui_main.h"
#include "options.h"

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
      client_option *o;
      char dp[20];
      
      for (o=options; o->name; ++o) {
	switch (o->type) {
	case COT_BOOL:
	  *(o->p_bool_value)=(Button_GetCheck((HWND)(o->p_gui_data))==BST_CHECKED);
	  break;
	case COT_INT:
	  GetWindowText((HWND)(o->p_gui_data),dp,sizeof(dp));
	  sscanf(dp, "%d", o->p_int_value);
	  break;
	}
      }
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
  client_option *o; 
  struct fcwin_box *hbox;
  struct fcwin_box *vbox_labels;
  struct fcwin_box *vbox;
  option_dialog=fcwin_create_layouted_window(option_proc,
					     _("Set local options"),
					     WS_OVERLAPPEDWINDOW,
					     CW_USEDEFAULT,CW_USEDEFAULT,
					     root_window,NULL,NULL);

  hbox=fcwin_hbox_new(option_dialog,FALSE);
  vbox=fcwin_vbox_new(option_dialog,TRUE);
  vbox_labels=fcwin_vbox_new(option_dialog,TRUE);
  for (o=options; o->name; ++o) {
    switch (o->type) {
    case COT_BOOL:
      fcwin_box_add_static(vbox_labels,_(o->description),
			   0,SS_LEFT,TRUE,TRUE,0);
      o->p_gui_data=(void *)
	fcwin_box_add_checkbox(vbox," ",0,0,TRUE,TRUE,0);
      break;
    case COT_INT:
      fcwin_box_add_static(vbox_labels,_(o->description),
			   0,SS_LEFT,TRUE,TRUE,0);
      o->p_gui_data=(void *)
	fcwin_box_add_edit(vbox,"",6,0,0,TRUE,TRUE,0);
      break;
    } 
  }
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
  client_option *o;
  char valstr[64];

  if (!option_dialog)
    create_option_dialog();

  for (o=options; o->name; ++o) {
    switch (o->type) {
    case COT_BOOL:
      Button_SetCheck((HWND)(o->p_gui_data),
		      (*(o->p_bool_value))?BST_CHECKED:BST_UNCHECKED);
      break;
    case COT_INT:
      my_snprintf(valstr, sizeof(valstr), "%d", *(o->p_int_value));
      SetWindowText((HWND)(o->p_gui_data), valstr);
      break;
    }
  }
  fcwin_redo_layout(option_dialog);
  ShowWindow(option_dialog,SW_SHOWNORMAL);
}
