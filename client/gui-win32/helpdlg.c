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
#include <string.h>
#include <assert.h>
#include <windows.h>
#include <windowsx.h>
 
#include "city.h"
#include "fcintl.h"
#include "game.h"
#include "genlist.h"
#include "government.h"
#include "mem.h"
#include "shared.h"
#include "tech.h"
#include "unit.h"
#include "map.h"
#include "support.h"
#include "version.h"
 
#include "climisc.h"
#include "colors.h"
#include "graphics.h"
#include "gui_stuff.h"
#include "helpdata.h"
#include "tilespec.h"
                                  
#include "helpdlg.h"
#define ID_HELP_LIST 109
#define ID_HELP_IMPR_REQ 110
#define ID_HELP_WOND_REQ 111
extern char long_buffer[64000];
extern HINSTANCE freecivhinst;
extern HWND root_window;
static HWND helpdlg_win;
static HWND helpdlg_list;
static HWND helpdlg_topic;
static HWND helpdlg_text;
static HWND helpdlg_close;
static HWND helpdlg_ilabel[6];
struct fcwin_box *helpdlg_hbox;
struct fcwin_box *helpdlg_vbox;
static void create_help_dialog(void);
static void help_update_dialog(const struct help_item *pitem);
/* static void create_help_page(enum help_page_type type);
 */
static void select_help_item(int item);
static void select_help_item_string(const char *item,
                                    enum help_page_type htype);
char *help_ilabel_name[6] =
{ N_("Cost:"), "", N_("Upkeep:"), "", N_("Requirement:"), "" };
 
char *help_wlabel_name[6] =
{ N_("Cost:"), "", N_("Requirement:"), "", N_("Obsolete by:"), "" };
 
char *help_ulabel_name[5][5] =
{
    { N_("Cost:"),              "", "", N_("Attack:"),  "" },
    { N_("Defense:"),   "", "", N_("Move:")     ,       "" },
    { N_("FirePower:"), "", "", N_("Hitpoints:"),       "" },
    { N_("Basic Upkeep:"),      "", "", N_("Vision:"),  "" },
    { N_("Requirement:"),       "", "", N_("Obsolete by:"),     "" }
};
 
char *help_tlabel_name[4][5] =
{
    { N_("Move/Defense:"),      "", "", N_("Food/Res/Trade:"),  "" },
    { N_("Sp1 F/R/T:"), "", "", N_("Sp2 F/R/T:"),               "" },
    { N_("Road Rslt/Time:"),"", "",     N_("Irrig. Rslt/Time:"),        "" },
    { N_("Mine Rslt/Time:"),"", "",     N_("Trans. Rslt/Time:"),        "" }
};                                 

/**************************************************************************

**************************************************************************/
static LONG APIENTRY HelpdlgWndProc(HWND hWnd,UINT uMsg,
				    UINT wParam,
				    LONG lParam)
{
  switch (uMsg)
    {
    case WM_CREATE:
      helpdlg_win=hWnd;
      return 0;
    case WM_DESTROY:
      helpdlg_win=NULL;
      helpdlg_vbox=NULL;
      break;
    case WM_GETMINMAXINFO:
    
      break;
    case WM_SIZE:
      break;
    case WM_CLOSE:
      DestroyWindow(hWnd);
      break;
    case WM_COMMAND:
      switch (LOWORD(wParam))
	{
	case IDCANCEL:
	  DestroyWindow(hWnd);
	  break;
	case ID_HELP_LIST:
	  {
	    if (HIWORD(wParam)==LBN_SELCHANGE)
	      {
		int row;
		const struct help_item *p = NULL;
		
		row=ListBox_GetCurSel(helpdlg_list);
		help_items_iterate(pitem) {
		  if ((row--)==0)
		    {
		      p=pitem;
		      break;
		    }
		}
		help_items_iterate_end;
		
		if (p)
		  help_update_dialog(p);
	      }
	  }
	  break;
	}
      break;
    default:
      return DefWindowProc(hWnd,uMsg,wParam,lParam);
      
    }
  return 0;
}

/**************************************************************************

**************************************************************************/
static void set_help_text(char *txt)
{
  char *newstr;
  if ((txt)&&(strlen(txt)))
    {
      newstr=convertnl2crnl(txt);
      SetWindowText(helpdlg_text,newstr);
      free(newstr);
    }
}



/**************************************************************************

**************************************************************************/
static void edit_minsize(POINT *pt,void *data)
{
  pt->x=300;
  pt->y=100;
}

/**************************************************************************

**************************************************************************/
static void edit_setsize(LPRECT rc, void *data)
{
  HWND hWnd;
  hWnd=(HWND)data;
  MoveWindow(hWnd,rc->left,rc->top,rc->right-rc->left,rc->bottom-rc->top,TRUE);
}

/**************************************************************************

**************************************************************************/
void create_help_page(enum help_page_type type)
{
  struct fcwin_box *vbox;
  printf("helppage1\n");
  fcwin_box_freeitem(helpdlg_hbox,1);
  vbox=fcwin_vbox_new(helpdlg_win,FALSE);
  helpdlg_topic=fcwin_box_add_static(vbox,"",0,SS_LEFT,FALSE,FALSE,5);
  fcwin_box_add_box(helpdlg_hbox,vbox,TRUE,TRUE,5);
  if (type==HELP_IMPROVEMENT || type==HELP_WONDER) {
    struct fcwin_box *hbox;
    int i;
    hbox=fcwin_hbox_new(helpdlg_win,TRUE);
    fcwin_box_add_box(vbox,hbox,FALSE,FALSE,5);
    if (type==HELP_IMPROVEMENT)
      {
	for (i=0;i<5;i++)
	  {
	    helpdlg_ilabel[i]=fcwin_box_add_static(hbox,
						   _(help_ilabel_name[i]),0,
						   SS_LEFT,TRUE,TRUE,5);
	  }
      }
    else
      {
	for(i=0;i<5;i++)
	  {
	    if (i==3)
	      helpdlg_ilabel[3]=fcwin_box_add_button(hbox,"",
						     ID_HELP_WOND_REQ,
						     0,TRUE,TRUE,5);
	    else
	      helpdlg_ilabel[i]=fcwin_box_add_static(hbox,
						     _(help_wlabel_name[i]),0,
						     SS_LEFT,TRUE,TRUE,5);
						     
	  }
      }
    helpdlg_ilabel[5]=fcwin_box_add_button(hbox,"",ID_HELP_IMPR_REQ,0,TRUE,TRUE,5);
  }
  fcwin_box_add_generic(vbox,edit_minsize,edit_setsize,NULL,
			helpdlg_text,TRUE,TRUE,5);
  printf("helppage2\n");
}

/**************************************************************************

**************************************************************************/
void create_help_dialog()
{
  struct fcwin_box *vbox;
  helpdlg_win=fcwin_create_layouted_window(HelpdlgWndProc,
					   _("Freeciv Help Browser"),
					   WS_OVERLAPPEDWINDOW,
					   CW_USEDEFAULT,CW_USEDEFAULT,
					   root_window,
					   NULL,
					   NULL);
  
  helpdlg_hbox=fcwin_hbox_new(helpdlg_win,FALSE);
  vbox=fcwin_vbox_new(helpdlg_win,FALSE);
  helpdlg_topic=fcwin_box_add_static(vbox,"",0,SS_LEFT,
				     FALSE,FALSE,5);
  helpdlg_text=CreateWindow("EDIT","",
			    WS_CHILD | WS_VISIBLE |  ES_READONLY | 
			    ES_WANTRETURN | 
			    ES_AUTOHSCROLL | ES_LEFT | WS_VSCROLL |
			    ES_AUTOVSCROLL | ES_MULTILINE | WS_BORDER,0,0,0,0,
			    helpdlg_win,
			    NULL,
			    freecivhinst,
			    NULL);
  fcwin_box_add_generic(vbox,edit_minsize,edit_setsize,NULL,
			helpdlg_text,TRUE,TRUE,5);
  helpdlg_list=fcwin_box_add_list(helpdlg_hbox,6,ID_HELP_LIST, 
		     LBS_NOTIFY | WS_VSCROLL,
		     FALSE,FALSE,5);
  fcwin_box_add_box(helpdlg_hbox,vbox,TRUE,TRUE,5);
  helpdlg_vbox=fcwin_vbox_new(helpdlg_win,FALSE);
  fcwin_box_add_box(helpdlg_vbox,helpdlg_hbox,TRUE,TRUE,5);
  helpdlg_close=fcwin_box_add_button(helpdlg_vbox,_("Close"),IDCANCEL,0,
		       FALSE,FALSE,5);
  help_items_iterate(pitem)
    ListBox_AddString(helpdlg_list,pitem->topic);
  help_items_iterate_end;
  fcwin_set_box(helpdlg_win,helpdlg_vbox);
  create_help_page(HELP_TEXT);
  printf("create_help1\n");
  fcwin_redo_layout(helpdlg_win);
  printf("create_help2\n");
}

/**************************************************************************
...
**************************************************************************/
static void help_update_improvement(const struct help_item *pitem,
                                    char *title, int which)
{
  char *buf = &long_buffer[0];
 
  create_help_page(HELP_IMPROVEMENT);
 
  if (which<B_LAST) {
    struct impr_type *imp = &improvement_types[which];
    sprintf(buf, "%d", imp->build_cost);
    SetWindowText(helpdlg_ilabel[1], buf);
    sprintf(buf, "%d", imp->upkeep);
    SetWindowText(helpdlg_ilabel[3], buf);
    if (imp->tech_req == A_LAST) {
      SetWindowText(helpdlg_ilabel[5], _("(Never)"));
    } else {
      SetWindowText(helpdlg_ilabel[5], advances[imp->tech_req].name);
    }
/*    create_tech_tree(help_improvement_tree, 0, imp->tech_req, 3);*/
  }
  else {
    SetWindowText(helpdlg_ilabel[1], "0");
    SetWindowText(helpdlg_ilabel[3], "0");
    SetWindowText(helpdlg_ilabel[5], _("(Never)"));
/*    create_tech_tree(help_improvement_tree, 0, game.num_tech_types, 3);*/
  }
  helptext_improvement(buf, which, pitem->text);
  set_help_text(buf);

}
/**************************************************************************
...
**************************************************************************/
static void help_update_wonder(const struct help_item *pitem,
                               char *title, int which)
{
  char *buf = &long_buffer[0];
 
  create_help_page(HELP_WONDER);
 
  if (which<B_LAST) {
    struct impr_type *imp = &improvement_types[which];
    sprintf(buf, "%d", imp->build_cost);
    SetWindowText(helpdlg_ilabel[1], buf);
    if (imp->tech_req == A_LAST) {
      SetWindowText(helpdlg_ilabel[3], _("(Never)"));
    } else {
      SetWindowText(helpdlg_ilabel[3], advances[imp->tech_req].name);
    }
    SetWindowText(helpdlg_ilabel[5], advances[imp->obsolete_by].name);
    /*    create_tech_tree(help_improvement_tree, 0, imp->tech_req, 3);*/
  }
  else {
    /* can't find wonder */
    SetWindowText(helpdlg_ilabel[1], "0");
    SetWindowText(helpdlg_ilabel[3], _("(Never)"));
    SetWindowText(helpdlg_ilabel[5], _("None"));
/*    create_tech_tree(help_improvement_tree, 0, game.num_tech_types, 3); */
  }
 
  helptext_wonder(buf, which, pitem->text);
  set_help_text(buf);
}                            
/**************************************************************************
  This is currently just a text page, with special text:
**************************************************************************/
static void help_update_government(const struct help_item *pitem,
                                   char *title, struct government *gov)
{
  char *buf = &long_buffer[0];
 
  if (gov==NULL) {
    strcat(buf, pitem->text);
  } else {
    helptext_government(buf, gov-governments, pitem->text);
  }
  create_help_page(HELP_TEXT);
  set_help_text(buf);
}
              
/**************************************************************************
...
**************************************************************************/
static void help_update_dialog(const struct help_item *pitem)
{
  int i;
  char *top;
  /* figure out what kind of item is required for pitem ingo */
  printf("update\n");
  for(top=pitem->topic; *top==' '; ++top);
  SetWindowText(helpdlg_text,"");

  switch(pitem->type) {
  case HELP_IMPROVEMENT:
    i = find_improvement_by_name(top);
    if(i!=B_LAST && is_wonder(i)) i = B_LAST;
    help_update_improvement(pitem, top, i);
    break;
  case HELP_WONDER:
    i = find_improvement_by_name(top);
    if(i!=B_LAST && !is_wonder(i)) i = B_LAST;
    help_update_wonder(pitem, top, i);
    break;
#if 0
  case HELP_UNIT:
    help_update_unit_type(pitem, top, find_unit_type_by_name(top));
    break;
  case HELP_TECH:
    help_update_tech(pitem, top, find_tech_by_name(top));
    break;
  case HELP_TERRAIN:
    help_update_terrain(pitem, top, get_terrain_by_name(top));
    break;
#endif
  case HELP_GOVERNMENT:
    help_update_government(pitem, top, find_government_by_name(top));
    break;
  case HELP_TEXT:
  default:
    create_help_page(HELP_TEXT);
    set_help_text(pitem->text);
    break;
  }
  printf("upd2\n");
  SetWindowText(helpdlg_topic,pitem->topic);
  fcwin_redo_layout(helpdlg_win);
  printf("upd3\n");
  /* set_title_topic(pitem->topic); */
  ShowWindow(helpdlg_win,SW_SHOWNORMAL);
}
/**************************************************************************
...
**************************************************************************/
static void select_help_item(int item)
{
  
  ListBox_SetTopIndex(helpdlg_list,item);
  ListBox_SetCurSel(helpdlg_list,item);
}

/****************************************************************
...
*****************************************************************/
static void select_help_item_string(const char *item,
                                    enum help_page_type htype)
{
  const struct help_item *pitem;
  int idx;

  pitem = get_help_item_spec(item, htype, &idx);
  if(idx==-1) idx = 0;
  select_help_item(idx);
  help_update_dialog(pitem);
}

/**************************************************************************

**************************************************************************/
void
popup_help_dialog_string(char *item)
{
  popup_help_dialog_typed(_(item), HELP_ANY); 
}

/**************************************************************************

**************************************************************************/
void
popup_help_dialog_typed(char *item, enum help_page_type htype)
{
  if (!helpdlg_win)
    create_help_dialog();
  select_help_item_string(item,htype);
}

/**************************************************************************

**************************************************************************/
void
popdown_help_dialog(void)
{
  DestroyWindow(helpdlg_win);
}
