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
#include <ctype.h>

#include <windows.h>
#include <windowsx.h>

#include "fcintl.h"
#include "game.h"
#include "genlist.h"
#include "government.h"
#include "map.h"
#include "mem.h"
#include "packets.h"
#include "player.h"
#include "shared.h"
#include "support.h"

#include "chatline.h"
#include "climisc.h"
#include "clinet.h"
#include "diptreaty.h"
#include "gui_main.h"
#include "gui_stuff.h"
#include "graphics.h"
#include "mapview.h"
#include "tilespec.h"

#include "diplodlg.h"

enum Diplomacy_ids {
  ID_MAP0=100,
  ID_MAP1,
  ID_MAP_SEA,
  ID_MAP_WORLD,
  ID_TECH0,
  ID_TECH1,
  ID_CITY0,
  ID_CITY1,
  ID_GOLD0,
  ID_GOLD1,
  ID_PACT,
  ID_CEASEFIRE,
  ID_PEACE,
  ID_ALLIANCE,
  ID_VISION0,
  ID_VISION1,
  ID_ERASE,
  ID_LIST,
  ID_ADVANCES_BASE=1000,
  ID_CITIES_BASE=2000
};

#define MAX_NUM_CLAUSES 64

struct Diplomacy_dialog {
  struct Treaty treaty;
  
  HWND mainwin;
  HWND list;
  HWND gold0_label;
  HWND gold1_label;
  POINT thumb0_pos;
  POINT thumb1_pos;
  struct Sprite *thumb0;
  struct Sprite *thumb1;
  HMENU menu_shown;
};

static struct genlist diplomacy_dialogs;
static int diplomacy_dialogs_list_has_been_initialised;

static struct Diplomacy_dialog *create_diplomacy_dialog(struct player *plr0, 
                                                 struct player *plr1);

static struct Diplomacy_dialog *find_diplomacy_dialog(struct player *plr0, 
                                               struct player *plr1);
static void popup_diplomacy_dialog(struct player *plr0, struct player *plr1);


/****************************************************************

*****************************************************************/
static void update_diplomacy_dialog(struct Diplomacy_dialog *pdialog)
{
  HDC hdc;
  char buf[64];
  ListBox_ResetContent(pdialog->list);
  
  clause_list_iterate(pdialog->treaty.clauses, pclause) {
    client_diplomacy_clause_string(buf, sizeof(buf), pclause);
    ListBox_AddString(pdialog->list,buf);
  } clause_list_iterate_end;
  
  pdialog->thumb0=sprites.treaty_thumb[BOOL_VAL(pdialog->treaty.accept0)];
  pdialog->thumb1=sprites.treaty_thumb[BOOL_VAL(pdialog->treaty.accept1)];
  hdc=GetDC(pdialog->mainwin);
  draw_sprite(pdialog->thumb0,hdc,pdialog->thumb0_pos.x,pdialog->thumb0_pos.y);
  draw_sprite(pdialog->thumb1,hdc,pdialog->thumb1_pos.x,pdialog->thumb1_pos.y);
  ReleaseDC(pdialog->mainwin,hdc);
}

/****************************************************************

*****************************************************************/
static void diplomacy_dialog_close(struct Diplomacy_dialog *pdialog)
{
  struct packet_diplomacy_info pa;

  pa.plrno0=game.player_idx;
  pa.plrno1=pdialog->treaty.plr1->player_no;
  send_packet_diplomacy_info(&aconnection, PACKET_DIPLOMACY_CANCEL_MEETING, 
                             &pa);
  DestroyWindow(pdialog->mainwin);
  
}

/****************************************************************

*****************************************************************/
static void popup_map_menu(struct Diplomacy_dialog *pdialog,int plr)
{
  RECT rc;
  MENUITEMINFO iteminfo;
  HMENU menu;
  menu=CreatePopupMenu();
  AppendMenu(menu,MF_STRING,ID_MAP_SEA,_("Sea-map"));
  iteminfo.dwItemData = plr;
  iteminfo.fMask = MIIM_DATA;
  iteminfo.cbSize = sizeof(MENUITEMINFO);
  SetMenuItemInfo(menu, ID_MAP_SEA, FALSE, &iteminfo);
  AppendMenu(menu,MF_STRING,ID_MAP_WORLD,_("Wold-map"));
  iteminfo.dwItemData = plr;
  iteminfo.fMask = MIIM_DATA;
  iteminfo.cbSize = sizeof(MENUITEMINFO);
  SetMenuItemInfo(menu, ID_MAP_WORLD, FALSE, &iteminfo);
  GetWindowRect(GetDlgItem(pdialog->mainwin,plr?ID_MAP1:ID_MAP0),&rc);
  TrackPopupMenu(menu,0,rc.left,rc.top,0,pdialog->mainwin,NULL);  
  pdialog->menu_shown=menu;
}

/****************************************************************

*****************************************************************/
static void popup_tech_menu(struct Diplomacy_dialog *pdialog,int plr)
{
  RECT rc;
  HMENU menu;
  MENUITEMINFO iteminfo;
  int i, flag;
  struct player *plr0;
  struct player *plr1;
  plr0=plr?pdialog->treaty.plr1:pdialog->treaty.plr0;
  plr1=plr?pdialog->treaty.plr0:pdialog->treaty.plr1;
  menu=CreatePopupMenu();
  for(i=1, flag=0; i<game.num_tech_types; i++) {
    if(get_invention(plr0, i)==TECH_KNOWN && 
       (get_invention(plr1, i)==TECH_UNKNOWN || 
        get_invention(plr1, i)==TECH_REACHABLE)) {
      AppendMenu(menu,MF_STRING,ID_ADVANCES_BASE+i,advances[i].name);
      iteminfo.dwItemData=plr0->player_no*10000+plr1->player_no*100+i;
      iteminfo.fMask = MIIM_DATA;
      iteminfo.cbSize = sizeof(MENUITEMINFO);
      SetMenuItemInfo(menu, ID_ADVANCES_BASE+i, FALSE, &iteminfo);
    }
  }
  GetWindowRect(GetDlgItem(pdialog->mainwin,plr?ID_TECH1:ID_TECH0),
		&rc);
  TrackPopupMenu(menu,0,rc.left,rc.top,0,pdialog->mainwin,NULL);  
  pdialog->menu_shown=menu;
}

/****************************************************************

*****************************************************************/
static void popup_cities_menu(struct Diplomacy_dialog *pdialog,int plr)
{
  RECT rc;
  HMENU menu;
  MENUITEMINFO iteminfo;
  int i = 0, j = 0;
  int n;
  struct city **city_list_ptrs;
  struct player *plr0;
  struct player *plr1;
  menu=CreatePopupMenu();
  plr0=plr?pdialog->treaty.plr1:pdialog->treaty.plr0;
  plr1=plr?pdialog->treaty.plr0:pdialog->treaty.plr1;
  n=city_list_size(&plr0->cities);
  if (n>0) {
    city_list_ptrs = fc_malloc(sizeof(struct city*)*n);
  } else {
    city_list_ptrs = NULL;
  }
  
  city_list_iterate(plr0->cities, pcity) {
    if(!city_got_effect(pcity, B_PALACE)){
      city_list_ptrs[i] = pcity;
      i++;
    }
  } city_list_iterate_end;

  qsort(city_list_ptrs, i, sizeof(struct city*), city_name_compare);
  
  for(j=0; j<i; j++) {
    AppendMenu(menu,MF_STRING,ID_CITIES_BASE+j,city_list_ptrs[j]->name);
    iteminfo.dwItemData=city_list_ptrs[j]->id*1024 + 
      plr0->player_no*32 + plr1->player_no;
    iteminfo.fMask = MIIM_DATA;
    iteminfo.cbSize = sizeof(MENUITEMINFO);
    SetMenuItemInfo(menu, ID_CITIES_BASE+j, FALSE, &iteminfo);
  }
  free(city_list_ptrs);
  GetWindowRect(GetDlgItem(pdialog->mainwin,plr?ID_CITY1:ID_CITY0),
		&rc);
  TrackPopupMenu(menu,0,rc.left,rc.top,0,pdialog->mainwin,NULL); 
  pdialog->menu_shown=menu;
}

/****************************************************************

*****************************************************************/
static void handle_gold_entry(struct Diplomacy_dialog *pdialog,int plr)
{
  char buf[32];
  HWND edit;
  edit=GetDlgItem(pdialog->mainwin,plr?ID_GOLD1:ID_GOLD0);
  GetWindowText(edit,buf,sizeof(buf));
  if (strchr(buf,'\n')) {
    int amount;
    struct player *pgiver;
    SetWindowText(edit,"");
    pgiver=plr?pdialog->treaty.plr1:pdialog->treaty.plr0;
    amount=atoi(buf);
     if(amount>=0 && amount<=pgiver->economic.gold) {
       struct packet_diplomacy_info pa;
       pa.plrno0=pdialog->treaty.plr0->player_no;
       pa.plrno1=pdialog->treaty.plr1->player_no;
       pa.clause_type=CLAUSE_GOLD;
       pa.plrno_from=pgiver->player_no;
       pa.value=amount;
       send_packet_diplomacy_info(&aconnection, PACKET_DIPLOMACY_CREATE_CLAUSE,
				  &pa);
     } else {
       append_output_window(_("Game: Invalid amount of gold specified."));
     }
  }
}

/****************************************************************

*****************************************************************/
static void handle_vision_button(struct Diplomacy_dialog *pdialog,int plr)
{
  struct packet_diplomacy_info pa;
  struct player *pgiver;
  pgiver=plr?pdialog->treaty.plr1:pdialog->treaty.plr0;
  pa.plrno0 = pdialog->treaty.plr0->player_no;
  pa.plrno1 = pdialog->treaty.plr1->player_no;
  pa.clause_type = CLAUSE_VISION;
  pa.plrno_from = pgiver->player_no;
  pa.value = 0;
  send_packet_diplomacy_info(&aconnection, PACKET_DIPLOMACY_CREATE_CLAUSE,
                             &pa);
}

/****************************************************************

*****************************************************************/
static void handle_pact_button(struct Diplomacy_dialog *pdialog)
{
  RECT rc;
  MENUITEMINFO iteminfo;
  HMENU menu;
  iteminfo.fMask=MIIM_DATA;
  iteminfo.cbSize=sizeof(MENUITEMINFO);
  menu=CreatePopupMenu();
  AppendMenu(menu,MF_STRING,ID_CEASEFIRE,Q_("?diplomatic_state:Cease-fire"));
  AppendMenu(menu,MF_STRING,ID_PEACE,Q_("?diplomatic_state:Peace"));
  AppendMenu(menu,MF_STRING,ID_ALLIANCE,Q_("?diplomatic_state:Alliance"));
  iteminfo.dwItemData=CLAUSE_CEASEFIRE;
  SetMenuItemInfo(menu,ID_CEASEFIRE,FALSE,&iteminfo);
  iteminfo.dwItemData=CLAUSE_PEACE;
  SetMenuItemInfo(menu,ID_PEACE,FALSE,&iteminfo);  
  iteminfo.dwItemData=CLAUSE_ALLIANCE;
  SetMenuItemInfo(menu,ID_ALLIANCE,FALSE,&iteminfo);
  GetWindowRect(GetDlgItem(pdialog->mainwin,ID_PACT),
		&rc);
  TrackPopupMenu(menu,0,rc.left,rc.top,0,pdialog->mainwin,NULL); 
  pdialog->menu_shown=menu;
}

/****************************************************************

*****************************************************************/
static void give_sea_map(struct Diplomacy_dialog *pdialog, int plr)
{

  struct packet_diplomacy_info pa;
  struct player *pgiver;
  
  pgiver=plr?pdialog->treaty.plr1:pdialog->treaty.plr0;
  pa.plrno0=pdialog->treaty.plr0->player_no;
  pa.plrno1=pdialog->treaty.plr1->player_no;
  pa.clause_type=CLAUSE_SEAMAP;
  pa.plrno_from=pgiver->player_no;
  pa.value=0;
  send_packet_diplomacy_info(&aconnection, PACKET_DIPLOMACY_CREATE_CLAUSE,
                             &pa);
}

/****************************************************************

*****************************************************************/
static void give_map(struct Diplomacy_dialog *pdialog, int plr)
{
  struct packet_diplomacy_info pa;
  struct player *pgiver;
  pgiver=plr?pdialog->treaty.plr1:pdialog->treaty.plr0;
  pa.plrno0=pdialog->treaty.plr0->player_no;
  pa.plrno1=pdialog->treaty.plr1->player_no;
  pa.clause_type=CLAUSE_MAP;
  pa.plrno_from=pgiver->player_no;
  pa.value=0;
  send_packet_diplomacy_info(&aconnection, PACKET_DIPLOMACY_CREATE_CLAUSE,
                             &pa);
}

/****************************************************************

*****************************************************************/
static void handle_cities_menu(struct Diplomacy_dialog *pdialog,int choice)
{
  struct packet_diplomacy_info pa;
  
  pa.value = choice/1024;
  choice -= pa.value * 1024;
  pa.plrno0 = choice/32;
  choice -= pa.plrno0 * 32;
  pa.plrno1 = choice;
 
  pa.clause_type=CLAUSE_CITY;
  pa.plrno_from=pa.plrno0;

  send_packet_diplomacy_info(&aconnection, PACKET_DIPLOMACY_CREATE_CLAUSE,
                             &pa);
}

/****************************************************************

*****************************************************************/
static void handle_advances_menu(struct Diplomacy_dialog *pdialog,int choice)
{
  struct packet_diplomacy_info pa;
  pa.plrno0=choice/10000;
  pa.plrno1=(choice/100)%100;
  pa.clause_type=CLAUSE_ADVANCE;
  pa.plrno_from=pa.plrno0;
  pa.value=choice%100;
 
  send_packet_diplomacy_info(&aconnection, PACKET_DIPLOMACY_CREATE_CLAUSE,
                            &pa);
}

/****************************************************************

*****************************************************************/
static void diplomacy_dialog_add_pact_clause(struct Diplomacy_dialog *pdialog,
					     int type)
{
  struct packet_diplomacy_info pa;
  
  pa.plrno0 = pdialog->treaty.plr0->player_no;
  pa.plrno1 = pdialog->treaty.plr1->player_no;
  pa.clause_type = type;
  pa.plrno_from = pdialog->treaty.plr0->player_no;
  pa.value = 0;
  send_packet_diplomacy_info(&aconnection, PACKET_DIPLOMACY_CREATE_CLAUSE,
                             &pa);
}

/****************************************************************

*****************************************************************/
static void handle_erase_button(struct Diplomacy_dialog *pdialog)
{
  int i=0;
  int row;
  row=ListBox_GetCurSel(pdialog->list);
  if (row==LB_ERR)
    return;
  clause_list_iterate(pdialog->treaty.clauses, pclause) {
    if(i == row) {
      struct packet_diplomacy_info pa;
      
      pa.plrno0=pdialog->treaty.plr0->player_no;
      pa.plrno1=pdialog->treaty.plr1->player_no;
      pa.plrno_from=pclause->from->player_no;
      pa.clause_type=pclause->type;
      pa.value=pclause->value;
      send_packet_diplomacy_info(&aconnection, PACKET_DIPLOMACY_REMOVE_CLAUSE,
				 &pa);
      return;
    }
    i++;
  } clause_list_iterate_end;
}

/****************************************************************

*****************************************************************/
static void handle_accept_button(struct Diplomacy_dialog *pdialog)
{
  struct packet_diplomacy_info pa;
  
  pa.plrno0=pdialog->treaty.plr0->player_no;
  pa.plrno1=pdialog->treaty.plr1->player_no;
  pa.plrno_from=game.player_idx;
  send_packet_diplomacy_info(&aconnection, PACKET_DIPLOMACY_ACCEPT_TREATY,
                             &pa);

}

/****************************************************************

*****************************************************************/
static LONG CALLBACK diplomacy_proc(HWND dlg,UINT message,WPARAM wParam,LPARAM lParam)
{
  int is_menu;
  int menu_data;

  struct Diplomacy_dialog *pdialog;
  pdialog=(struct Diplomacy_dialog *)fcwin_get_user_data(dlg);
  switch(message) {
  case WM_CREATE:
  case WM_SIZE:
  case WM_GETMINMAXINFO:
    break;
  case WM_CLOSE:
    diplomacy_dialog_close(pdialog);
    break;
  case WM_DESTROY:
    if (pdialog->menu_shown)
      DestroyMenu(pdialog->menu_shown);
    genlist_unlink(&diplomacy_dialogs, pdialog);
    free(pdialog);
    break;
  case WM_COMMAND:
    is_menu=0;
    menu_data=0;
    if (pdialog->menu_shown) {
      MENUITEMINFO iteminfo;
      iteminfo.cbSize=sizeof(MENUITEMINFO);
      iteminfo.fMask=MIIM_DATA;
      is_menu=GetMenuItemInfo(pdialog->menu_shown,LOWORD(wParam),FALSE,&iteminfo);
      DestroyMenu(pdialog->menu_shown);
      pdialog->menu_shown=NULL;
      menu_data=iteminfo.dwItemData;
    }
    switch((enum Diplomacy_ids) LOWORD(wParam)) {
    case ID_MAP0:
      popup_map_menu(pdialog,0);
      break;
    case ID_MAP1:
      popup_map_menu(pdialog,1);
      break;
    case ID_MAP_SEA:
      give_sea_map(pdialog,menu_data);
      break;
    case ID_MAP_WORLD:
      give_map(pdialog,menu_data);
      break;
    case ID_TECH0:
      popup_tech_menu(pdialog,0);
      break;
    case ID_TECH1:
      popup_tech_menu(pdialog,1);
      break;
    case ID_CITY0:
      popup_cities_menu(pdialog,0);
      break;
    case ID_CITY1:
      popup_cities_menu(pdialog,1);
      break;
    case ID_GOLD0:
      handle_gold_entry(pdialog,0);
      break;
    case ID_GOLD1:
      handle_gold_entry(pdialog,1);
      break;
    case ID_PEACE:
    case ID_CEASEFIRE:
    case ID_ALLIANCE:
      diplomacy_dialog_add_pact_clause(pdialog,menu_data);
      break;
    case ID_VISION0:
      handle_vision_button(pdialog,0);
      break;
    case ID_VISION1:
      handle_vision_button(pdialog,1);
      break;
    case ID_PACT:
      handle_pact_button(pdialog);
      break;
    case ID_ERASE:
      handle_erase_button(pdialog);
      break;
    case IDCANCEL:
      diplomacy_dialog_close(pdialog);
      break;
    case IDOK:
      handle_accept_button(pdialog);
      break;
    default:
      if (LOWORD(wParam)>=ID_CITIES_BASE) {
	handle_cities_menu(pdialog,menu_data);
      } else if (LOWORD(wParam)>=ID_ADVANCES_BASE) {
	handle_advances_menu(pdialog,menu_data);
      }
      
    }
    break;
  case WM_PAINT:
    {
      PAINTSTRUCT ps;
      HDC hdc;
      hdc=BeginPaint(dlg,(LPPAINTSTRUCT)&ps);
      draw_sprite(pdialog->thumb0,hdc,
		  pdialog->thumb0_pos.x,pdialog->thumb0_pos.y);
      draw_sprite(pdialog->thumb1,hdc,
		  pdialog->thumb1_pos.x,pdialog->thumb1_pos.y);
      EndPaint(dlg,(LPPAINTSTRUCT)&ps);
    }
    break;
  default:
    return DefWindowProc(dlg,message,wParam,lParam);
  }
  return 0;
}

/****************************************************************

*****************************************************************/
static void thumb_minsize(POINT *minsize, void *data)
{
  minsize->x=sprites.treaty_thumb[0]->width;
  minsize->y=sprites.treaty_thumb[0]->height;
}

/****************************************************************

*****************************************************************/
static void thumb_setsize(RECT *rc, void *data)
{
  POINT *thumb_pos;
  thumb_pos=(POINT *)data;
  thumb_pos->x=rc->left;
  thumb_pos->y=rc->top;
}

/****************************************************************
...
*****************************************************************/
static struct Diplomacy_dialog *create_diplomacy_dialog(struct player *plr0, 
							struct player *plr1)
{
  char buf[512];
  struct fcwin_box *vbox;
  struct fcwin_box *hbox;
  struct fcwin_box *hbox2;
  struct Diplomacy_dialog *pdialog;

  pdialog=fc_malloc(sizeof(struct Diplomacy_dialog));  
  genlist_insert(&diplomacy_dialogs, pdialog, 0);
  pdialog->menu_shown=NULL;
  init_treaty(&pdialog->treaty, plr0, plr1);

  pdialog->mainwin=fcwin_create_layouted_window(diplomacy_proc,
						_("Diplomacy meeting"),
						WS_OVERLAPPEDWINDOW,
						CW_USEDEFAULT,CW_USEDEFAULT,
						root_window,NULL,pdialog);
  vbox=fcwin_vbox_new(pdialog->mainwin,FALSE);
  my_snprintf(buf, sizeof(buf),
              _("The %s offerings"), get_nation_name(plr0->nation));
  fcwin_box_add_static(vbox,buf,0,SS_LEFT,FALSE,FALSE,5);
  fcwin_box_add_button(vbox,_("Maps"),ID_MAP0,0,FALSE,FALSE,5);
  fcwin_box_add_button(vbox,_("Advances"),ID_TECH0,0,FALSE,FALSE,5);
  fcwin_box_add_button(vbox,_("Cities"),ID_CITY0,0,FALSE,FALSE,5);
  
  my_snprintf(buf, sizeof(buf), _("Gold(max %d)"), plr0->economic.gold); 
  pdialog->gold0_label=fcwin_box_add_static(vbox,buf,0,SS_LEFT,FALSE,FALSE,5);
  fcwin_box_add_edit(vbox,"",6,ID_GOLD0,ES_WANTRETURN | ES_MULTILINE,
		     FALSE,FALSE,5);
  fcwin_box_add_button(vbox,_("Give shared vision"),ID_VISION0,0,
		       FALSE,FALSE,5);
  fcwin_box_add_button(vbox,_("Pacts"),ID_PACT,0,FALSE,FALSE,5);
  hbox=fcwin_hbox_new(pdialog->mainwin,FALSE);
  fcwin_box_add_box(hbox,vbox,FALSE,FALSE,5);
  vbox=fcwin_vbox_new(pdialog->mainwin,FALSE);
    
  my_snprintf(buf, sizeof(buf),
	      _("This Eternal Treaty\n"
		"marks the results of the diplomatic work between\n"
		"The %s %s %s\nand\nThe %s %s %s"),
	      get_nation_name(plr0->nation),
	      get_ruler_title(plr0->government, plr0->is_male, plr0->nation),
	      plr0->name,
	      get_nation_name(plr1->nation),
	      get_ruler_title(plr1->government, plr1->is_male, plr1->nation),
	      plr1->name);
  fcwin_box_add_static(vbox,buf,0,SS_CENTER,FALSE,FALSE,5);
  pdialog->list=fcwin_box_add_list(vbox,6,ID_LIST,WS_VSCROLL,TRUE,TRUE,5);
  hbox2=fcwin_hbox_new(pdialog->mainwin,FALSE);
  my_snprintf(buf, sizeof(buf), _("%s view:"), get_nation_name(plr0->nation));
  fcwin_box_add_static(hbox2,buf,0,SS_LEFT,FALSE,FALSE,5);
  fcwin_box_add_generic(hbox2,thumb_minsize,thumb_setsize,NULL,
			&pdialog->thumb0_pos,FALSE,FALSE,5);
  
  my_snprintf(buf, sizeof(buf), _("%s view:"), get_nation_name(plr1->nation));
  fcwin_box_add_static(hbox2,buf,0,SS_LEFT,FALSE,FALSE,5);
  fcwin_box_add_generic(hbox2,thumb_minsize,thumb_setsize,NULL,
			&pdialog->thumb1_pos,FALSE,FALSE,5);
  pdialog->thumb0=sprites.treaty_thumb[0];
  pdialog->thumb1=sprites.treaty_thumb[0];
  fcwin_box_add_box(vbox,hbox2,FALSE,FALSE,5);

  fcwin_box_add_box(hbox,vbox,TRUE,TRUE,5);
  
  vbox=fcwin_vbox_new(pdialog->mainwin,FALSE);
  my_snprintf(buf, sizeof(buf),
              _("The %s offerings"), get_nation_name(plr1->nation));
  fcwin_box_add_static(vbox,buf,0,SS_LEFT,FALSE,FALSE,5);
  fcwin_box_add_button(vbox,_("Maps"),ID_MAP1,0,FALSE,FALSE,5);
  fcwin_box_add_button(vbox,_("Advances"),ID_TECH1,0,FALSE,FALSE,5);
  fcwin_box_add_button(vbox,_("Cities"),ID_CITY1,0,FALSE,FALSE,5);
  
  my_snprintf(buf, sizeof(buf), _("Gold(max %d)"), plr1->economic.gold); 
  pdialog->gold1_label=fcwin_box_add_static(vbox,buf,0,SS_LEFT,FALSE,FALSE,5);
  fcwin_box_add_edit(vbox,"",6,ID_GOLD1,ES_WANTRETURN | ES_MULTILINE,
		     FALSE,FALSE,5);
  fcwin_box_add_button(vbox,_("Give shared vision"),ID_VISION1,0,
		       FALSE,FALSE,5);

  fcwin_box_add_box(hbox,vbox,FALSE,FALSE,5);
  vbox=fcwin_vbox_new(pdialog->mainwin,FALSE);
  fcwin_box_add_box(vbox,hbox,TRUE,TRUE,5);
  hbox=fcwin_hbox_new(pdialog->mainwin,TRUE);
  
  fcwin_box_add_button(hbox,_("Accept treaty"),IDOK,0,TRUE,TRUE,5);
  fcwin_box_add_button(hbox,_("Cancel meeting"),IDCANCEL,0,TRUE,TRUE,5);

  fcwin_box_add_box(vbox,hbox,FALSE,FALSE,5);
  fcwin_set_box(pdialog->mainwin,vbox);

  update_diplomacy_dialog(pdialog);

  return pdialog;
}

/****************************************************************
...
*****************************************************************/
static struct Diplomacy_dialog *find_diplomacy_dialog(struct player *plr0, 
                                                      struct player *plr1)
{
  struct genlist_iterator myiter;

  if(!diplomacy_dialogs_list_has_been_initialised) {
    genlist_init(&diplomacy_dialogs);
    diplomacy_dialogs_list_has_been_initialised=1;
  }
  
  genlist_iterator_init(&myiter, &diplomacy_dialogs, 0);
    
  for(; ITERATOR_PTR(myiter); ITERATOR_NEXT(myiter)) {
    struct Diplomacy_dialog *pdialog=
      (struct Diplomacy_dialog *)ITERATOR_PTR(myiter);
    if((pdialog->treaty.plr0==plr0 && pdialog->treaty.plr1==plr1) ||
       (pdialog->treaty.plr0==plr1 && pdialog->treaty.plr1==plr0))
      return pdialog;
  }
  return 0;
}


/****************************************************************
...
*****************************************************************/
static void popup_diplomacy_dialog(struct player *plr0, struct player *plr1)
{
  struct Diplomacy_dialog *pdialog;
  
  if(!(pdialog=find_diplomacy_dialog(plr0, plr1))) {
    pdialog=create_diplomacy_dialog(plr0, plr1);
  }
  
  ShowWindow(pdialog->mainwin,SW_SHOWNORMAL);
}

/**************************************************************************
...
**************************************************************************/
void
handle_diplomacy_accept_treaty(struct packet_diplomacy_info *pa)
{
  struct Diplomacy_dialog *pdialog;
  
  if((pdialog=find_diplomacy_dialog(&game.players[pa->plrno0],
				    &game.players[pa->plrno1]))) {
    if(pa->plrno_from==game.player_idx)
      pdialog->treaty.accept0=!pdialog->treaty.accept0;
    else
      pdialog->treaty.accept1=!pdialog->treaty.accept1;
    update_diplomacy_dialog(pdialog);
  }
  
}

/**************************************************************************
...
**************************************************************************/
void
handle_diplomacy_init_meeting(struct packet_diplomacy_info *pa)
{
  popup_diplomacy_dialog(&game.players[pa->plrno0], 
                         &game.players[pa->plrno1]);
}

/**************************************************************************
...
**************************************************************************/
void
handle_diplomacy_create_clause(struct packet_diplomacy_info *pa)
{
  struct Diplomacy_dialog *pdialog;
  if((pdialog=find_diplomacy_dialog(&game.players[pa->plrno0],
				    &game.players[pa->plrno1]))) {
    add_clause(&pdialog->treaty, &game.players[pa->plrno_from],
               pa->clause_type, pa->value);
    update_diplomacy_dialog(pdialog);
  }
  
}

/**************************************************************************
...
**************************************************************************/
void
handle_diplomacy_cancel_meeting(struct packet_diplomacy_info *pa)
{
  struct Diplomacy_dialog *pdialog;
  if((pdialog=find_diplomacy_dialog(&game.players[pa->plrno0],
                                    &game.players[pa->plrno1])))
    DestroyWindow(pdialog->mainwin);
}

/**************************************************************************
...
**************************************************************************/
void
handle_diplomacy_remove_clause(struct packet_diplomacy_info *pa)
{
  struct Diplomacy_dialog *pdialog;
  
  if((pdialog=find_diplomacy_dialog(&game.players[pa->plrno0],
				    &game.players[pa->plrno1]))) {
    remove_clause(&pdialog->treaty, &game.players[pa->plrno_from],
                  pa->clause_type, pa->value);
    
    update_diplomacy_dialog(pdialog);
  }
  
}

/**************************************************************************
...
**************************************************************************/
void
close_all_diplomacy_dialogs(void)
{
  struct Diplomacy_dialog *pdialog;
  
  if (!diplomacy_dialogs_list_has_been_initialised) {
    return;
  }
  while (genlist_size(&diplomacy_dialogs)) {
    pdialog = genlist_get(&diplomacy_dialogs, 0);
    DestroyWindow(pdialog->mainwin);
  }

}
