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
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

#include <libraries/mui.h>
#include <mui/NListview_MCC.h>

#include <clib/alib_protos.h>
#include <proto/exec.h>
#include <proto/utility.h>
#include <proto/muimaster.h>

#include "capability.h"
#include "game.h"
#include "genlist.h"
#include "government.h"
#include "map.h"
#include "mem.h"
#include "player.h"
#include "shared.h"

#include "chatline.h"
#include "clinet.h"
#include "climisc.h"
#include "diptreaty.h"
#include "mapview.h"

#include "diplodlg.h"

/* MUI Imports */

#include "muistuff.h"
#include "mapclass.h"

IMPORT Object *app;

struct Diplomacy_dialog {
  struct Treaty treaty;

  Object *wnd;
  Object *clauses_listview;
  Object *plr0_maps_button;
  Object *plr0_maps_menu;
  Object *plr1_maps_button;
  Object *plr1_maps_menu;
  Object *plr0_adv_button;
  Object *plr0_adv_menu;
  Object *plr1_adv_button;
  Object *plr1_adv_menu;
  Object *plr0_cities_button;
  Object *plr0_cities_menu;
  Object *plr1_cities_button;
  Object *plr1_cities_menu;
  Object *plr0_thumb_sprite;
  Object *plr1_thumb_sprite;
  Object *plr0_gold_integer;
  Object *plr1_gold_integer;
  Object *plr0_pacts_button;
  Object *plr0_pacts_menu;
};

void request_diplomacy_cancel_meeting(struct Treaty *treaty)
{
  struct packet_diplomacy_info pa;

  pa.plrno0=treaty->plr0->player_no;
  pa.plrno1=treaty->plr1->player_no;
  send_packet_diplomacy_info(&aconnection, PACKET_DIPLOMACY_CANCEL_MEETING, 
			     &pa);
}

void request_diplomacy_create_clause(struct Treaty *treaty, int type, int from, int value)
{
  struct packet_diplomacy_info pa;

  pa.plrno0=treaty->plr0->player_no;
  pa.plrno1=treaty->plr1->player_no;
  pa.clause_type=type;
  pa.plrno_from=from;
  pa.value=value;
  send_packet_diplomacy_info(&aconnection, PACKET_DIPLOMACY_CREATE_CLAUSE,
			     &pa);
}

void request_diplomacy_remove_clause_no(struct Treaty *treaty, int clause_no)
{
  int i;
  struct genlist_iterator myiter;

  genlist_iterator_init(&myiter, &treaty->clauses, 0);

  for(i=0; ITERATOR_PTR(myiter); ITERATOR_NEXT(myiter), i++) {
    if(i==clause_no) {
      struct packet_diplomacy_info pa;
      struct Clause *pclause=(struct Clause *)ITERATOR_PTR(myiter);

      pa.plrno0=treaty->plr0->player_no;
      pa.plrno1=treaty->plr1->player_no;
      pa.plrno_from=pclause->from->player_no;
      pa.clause_type=pclause->type;
      pa.value=pclause->value;
      send_packet_diplomacy_info(&aconnection, PACKET_DIPLOMACY_REMOVE_CLAUSE,
				    &pa);
      return;
    }
  }
}

void request_diplomacy_accept_treaty(struct Treaty *treaty, int from)
{
  struct packet_diplomacy_info pa;
  
  pa.plrno0=treaty->plr0->player_no;
  pa.plrno1=treaty->plr1->player_no;
  pa.plrno_from=from;
  send_packet_diplomacy_info(&aconnection, PACKET_DIPLOMACY_ACCEPT_TREATY,
			     &pa);
}

struct Diplomacy_dialog *create_diplomacy_dialog(struct player *plr0, 
						 struct player *plr1);

struct genlist diplomacy_dialogs;
int diplomacy_dialogs_list_has_been_initialised;

struct Diplomacy_dialog *find_diplomacy_dialog(struct player *plr0, 
					       struct player *plr1);
void popup_diplomacy_dialog(struct player *plr0, struct player *plr1);
static void close_diplomacy_dialog(struct Diplomacy_dialog *pdialog);
static void update_diplomacy_dialog(struct Diplomacy_dialog *pdialog);


/****************************************************************
 GUI Independ
*****************************************************************/
void handle_diplomacy_accept_treaty(struct packet_diplomacy_info *pa)
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

/****************************************************************
...
*****************************************************************/
void handle_diplomacy_init_meeting(struct packet_diplomacy_info *pa)
{
  popup_diplomacy_dialog(&game.players[pa->plrno0], 
			 &game.players[pa->plrno1]);
}

/****************************************************************
...
*****************************************************************/
void handle_diplomacy_cancel_meeting(struct packet_diplomacy_info *pa)
{
  struct Diplomacy_dialog *pdialog;
  
  if((pdialog=find_diplomacy_dialog(&game.players[pa->plrno0],
				    &game.players[pa->plrno1])))
    close_diplomacy_dialog(pdialog);
}

/****************************************************************
...
*****************************************************************/
void handle_diplomacy_create_clause(struct packet_diplomacy_info *pa)
{
  struct Diplomacy_dialog *pdialog;
  
  if((pdialog=find_diplomacy_dialog(&game.players[pa->plrno0],
				&game.players[pa->plrno1]))) {
    add_clause(&pdialog->treaty, &game.players[pa->plrno_from],
	       pa->clause_type, pa->value);
    update_diplomacy_dialog(pdialog);
  }
}

/****************************************************************
...
*****************************************************************/
void handle_diplomacy_remove_clause(struct packet_diplomacy_info *pa)
{
  struct Diplomacy_dialog *pdialog;

  if((pdialog=find_diplomacy_dialog(&game.players[pa->plrno0],
				&game.players[pa->plrno1]))) {
    remove_clause(&pdialog->treaty, &game.players[pa->plrno_from],
		  pa->clause_type, pa->value);
    
    update_diplomacy_dialog(pdialog);
  }
}

struct Diplomacy_data
{
  struct Diplomacy_dialog *pdialog;
  int playerno;
  Object *obj;
};

static void diplomacy_tech( struct Diplomacy_data *data);
static void diplomacy_city( struct Diplomacy_data *data);

/****************************************************************
popup the diplomacy window
*****************************************************************/
void popup_diplomacy_dialog(struct player *plr0, struct player *plr1)
{
  struct Diplomacy_dialog *pdialog;
  
  if(!(pdialog=find_diplomacy_dialog(plr0, plr1))) {
    pdialog=create_diplomacy_dialog(plr0, plr1);
  }

  if(pdialog)
  {
    update_diplomacy_dialog(pdialog);
    set(pdialog->wnd,MUIA_Window_Open,TRUE);
   }
}

/****************************************************************
...
*****************************************************************/
static int fill_diplomacy_tech_menu(Object *menu_title, struct Diplomacy_dialog *pdialog,
				    struct player *plr0, struct player *plr1)
{
  int i, flag;

  for(i=1, flag=0; i<game.num_tech_types; i++)
  {
    if(get_invention(plr0, i)==TECH_KNOWN && (get_invention(plr1, i)==TECH_UNKNOWN || get_invention(plr1, i)==TECH_REACHABLE))
    {
      Object *entry;
      entry = MUI_MakeObject(MUIO_Menuitem,advances[i].name,NULL,0,0);
      set(entry,MUIA_UserData,i);
      DoMethod(entry,MUIM_Notify,MUIA_Menuitem_Trigger, MUIV_EveryTime, entry,6, MUIM_CallHook, &standart_hook, diplomacy_tech, pdialog, plr0->player_no,entry);
      DoMethod(menu_title,MUIM_Family_AddTail, entry);
    }
  }

  return flag;
}

/****************************************************************
Creates a sorted list of plr0's cities, excluding the capital and
any cities not visible to plr1.  This means that you can only trade 
cities visible to requesting player.  - Kris Bubendorfer
*****************************************************************/
static int fill_diplomacy_city_menu(Object *menu_title, struct Diplomacy_dialog *pdialog,
				    struct player *plr0, struct player *plr1)
{
  int flag=0;

  city_list_iterate(plr0->cities, pcity) {
    if(!city_got_effect(pcity, B_PALACE)){
      Object *entry;
      entry = MUI_MakeObject(MUIO_Menuitem,pcity->name,NULL,0,0);
      set(entry,MUIA_UserData,pcity->id);
      DoMethod(entry,MUIM_Notify,MUIA_Menuitem_Trigger, MUIV_EveryTime, entry,6, MUIM_CallHook, &standart_hook, diplomacy_city, pdialog, plr0->player_no,entry);
      DoMethod(menu_title,MUIM_Family_AddTail, entry);
      flag=1;
    }
  } city_list_iterate_end;

  return flag;
}

/****************************************************************
 Must be called from the Application object so it is safe to
 dispose the window
*****************************************************************/
static void diplomacy_close_real( struct Diplomacy_dialog **ppdialog)
{
  close_diplomacy_dialog(*ppdialog);
}

/****************************************************************
 Callback for the Cancel Button or CloseButton
*****************************************************************/
static void diplomacy_close( struct Diplomacy_dialog **ppdialog)
{
  struct Diplomacy_dialog *pdialog = *ppdialog;
  set((*ppdialog)->wnd,MUIA_Window_Open,FALSE);
  request_diplomacy_cancel_meeting(&pdialog->treaty);
  DoMethod(app, MUIM_Application_PushMethod, app, 4, MUIM_CallHook, &standart_hook, diplomacy_close_real, *ppdialog);
}

/****************************************************************
 Callback for the erase clause button
*****************************************************************/
static void diplomacy_erase_clause(struct Diplomacy_data *data)
{
  struct Diplomacy_dialog *pdialog=data->pdialog;
  int i = xget(pdialog->clauses_listview,MUIA_NList_Active);
  if(i>=0)
  {
    request_diplomacy_remove_clause_no(&pdialog->treaty, i);
  }
}

/****************************************************************
 Callback for the accept treaty button
*****************************************************************/
static void diplomacy_accept_treaty(struct Diplomacy_data *data)
{
  struct Diplomacy_dialog *pdialog=data->pdialog;
  request_diplomacy_accept_treaty(&pdialog->treaty, game.player_idx);
}

/****************************************************************
 Callback for the world map
*****************************************************************/
static void diplomacy_world_map( struct Diplomacy_data *data)
{
  struct Diplomacy_dialog *pdialog = data->pdialog;
  request_diplomacy_create_clause(&pdialog->treaty, CLAUSE_MAP, data->playerno,0);
}

/****************************************************************
 Callback for the sea map
*****************************************************************/
static void diplomacy_sea_map( struct Diplomacy_data *data)
{
  struct Diplomacy_dialog *pdialog = data->pdialog;
  request_diplomacy_create_clause(&pdialog->treaty, CLAUSE_SEAMAP, data->playerno,0);
}

/****************************************************************
 Callback for the Gold integer
*****************************************************************/
static void diplomacy_gold( struct Diplomacy_data *data)
{
  struct Diplomacy_dialog *pdialog = data->pdialog;
  int amount = xget(data->obj, MUIA_String_Integer);

  if(amount>=0 && amount<=game.players[data->playerno].economic.gold)
  {
    request_diplomacy_create_clause(&pdialog->treaty, CLAUSE_GOLD, data->playerno,amount);
  } else append_output_window("Game: Invalid amount of gold specified.");
}

/****************************************************************
 Callback for a tech entry
*****************************************************************/
static void diplomacy_tech( struct Diplomacy_data *data)
{
  struct Diplomacy_dialog *pdialog = data->pdialog;
  int tech = xget(data->obj, MUIA_UserData);
  request_diplomacy_create_clause(&pdialog->treaty, CLAUSE_ADVANCE, data->playerno,tech);
}

/****************************************************************
 Callback for a city entry
*****************************************************************/
static void diplomacy_city( struct Diplomacy_data *data)
{
  struct Diplomacy_dialog *pdialog = data->pdialog;
  int cityid = xget(data->obj, MUIA_UserData);
  request_diplomacy_create_clause(&pdialog->treaty, CLAUSE_CITY, data->playerno,cityid);
}

/****************************************************************
 Add a diplomacy clause
*****************************************************************/
static void diplomacy_dialog_add_pact_clause(struct Diplomacy_data *data, int type)
{
  struct Diplomacy_dialog *pdialog = data->pdialog;
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
 Callback for a ceasefire entry
*****************************************************************/
static void diplomacy_ceasefire(struct Diplomacy_data *data)
{
  diplomacy_dialog_add_pact_clause(data, CLAUSE_CEASEFIRE);
}

/****************************************************************
 Callback for a peace entry
*****************************************************************/
static void diplomacy_peace(struct Diplomacy_data *data)
{
  diplomacy_dialog_add_pact_clause(data, CLAUSE_PEACE);
}

/****************************************************************
 Callback for a alliance entry
*****************************************************************/
static void diplomacy_alliance(struct Diplomacy_data *data)
{
  diplomacy_dialog_add_pact_clause(data, CLAUSE_ALLIANCE);
}

/****************************************************************
 Creates the diplomacy dialog between two players
*****************************************************************/
struct Diplomacy_dialog *create_diplomacy_dialog(struct player *plr0, 
						 struct player *plr1)
{
  struct Diplomacy_dialog *pdialog;
  Object *plr0_text, *plr0_gold_text, *plr1_text, *plr1_gold_text, *diplo_text;
  Object *plr0_view_text, *plr1_view_text;
	Object *accept_treaty;
	Object *cancel_meeting;
	Object *erase_clause;

  pdialog = (struct Diplomacy_dialog*)AllocVec(sizeof(struct Diplomacy_dialog),0x10000);
  if(!pdialog) return NULL;

  pdialog->wnd = WindowObject,
      MUIA_Window_Title, "Diplomacy meeting",
      WindowContents, VGroup,
          Child, HGroup,
              Child, VGroup,  /* Plr 0 */
                  MUIA_HorizWeight,50,
		  Child, HVSpace,
                  Child, plr0_text = TextObject, End,
                  Child, pdialog->plr0_maps_button = MakeButton("Maps"),
                  Child, pdialog->plr0_adv_button = MakeButton("Advances"),
                  Child, pdialog->plr0_cities_button = MakeButton("Cities"),
                  Child, HGroup,
                      Child, plr0_gold_text = TextObject, End,
                      Child, pdialog->plr0_gold_integer = MakeInteger(NULL),
                      End,
                  Child, pdialog->plr0_pacts_button = MakeButton("Pacts"),
                  Child, HVSpace,
                  End,
              Child, VGroup, /* Middle */
                  Child, diplo_text = TextObject, MUIA_Weight,0,MUIA_Text_PreParse,"\33c",End,
                  Child, pdialog->clauses_listview = NListviewObject,
                      MUIA_NListview_NList, NListObject,
                          MUIA_NList_ConstructHook, MUIV_NList_ConstructHook_String,
                          MUIA_NList_DestructHook , MUIV_NList_DestructHook_String,
                          End,
                      End,
                  Child, HGroup,
                     Child, HGroup,
                         Child, plr0_view_text = TextObject,End,
                         Child, pdialog->plr0_thumb_sprite = SpriteObject, MUIA_Sprite_Sprite, get_thumb_sprite(0), End,
                         End,
                     Child, HGroup,
                         Child, plr1_view_text = TextObject,End,
                         Child, pdialog->plr1_thumb_sprite = SpriteObject, MUIA_Sprite_Sprite, get_thumb_sprite(0), End,
                         End,
                     End,
                  Child, erase_clause = MakeButton("Erase clause"),
                  End,
              Child, VGroup,  /* Plr 1 */
                  MUIA_HorizWeight,50,
                  Child, HVSpace,
                  Child, plr1_text = TextObject, End,
                  Child, pdialog->plr1_maps_button = MakeButton("Maps"),
                  Child, pdialog->plr1_adv_button = MakeButton("Advances"),
                  Child, pdialog->plr1_cities_button = MakeButton("Cities"),
                  Child, HGroup,
                      Child, plr1_gold_text = TextObject, End,
                      Child, pdialog->plr1_gold_integer = MakeInteger(NULL),
                      End,
                  Child, HVSpace,
                  End,
              End,
          Child, HGroup,
              Child, accept_treaty = MakeButton("_Accept treaty"),
              Child, cancel_meeting = MakeButton("_Cancel meeting"),
              End,
          End,
      End;

  if(pdialog->wnd)
  {
    Object *menu_strip;
    Object *menu_title;

    genlist_insert(&diplomacy_dialogs, pdialog, 0);
    init_treaty(&pdialog->treaty, plr0, plr1);

    pdialog->plr0_maps_menu = menu_strip = MenustripObject,
        Child, menu_title = MenuObjectT("Maps"),
            End,
	End;

    if(menu_strip)
    {
       Object *entry;
       entry = MUI_MakeObject(MUIO_Menuitem,"World-Map",NULL,0,0);
       DoMethod(entry,MUIM_Notify,MUIA_Menuitem_Trigger, MUIV_EveryTime, entry,5, MUIM_CallHook, &standart_hook, diplomacy_world_map, pdialog, plr0->player_no);
       DoMethod(menu_title,MUIM_Family_AddTail, entry);

       entry = MUI_MakeObject(MUIO_Menuitem,"Sea-Map",NULL,0,0);
       DoMethod(entry,MUIM_Notify,MUIA_Menuitem_Trigger, MUIV_EveryTime, entry,5, MUIM_CallHook, &standart_hook, diplomacy_sea_map, pdialog, plr0->player_no);
       DoMethod(menu_title,MUIM_Family_AddTail, entry);
       set(pdialog->plr0_maps_button,MUIA_ContextMenu,menu_strip);
    }

    pdialog->plr1_maps_menu = menu_strip = MenustripObject,
        Child, menu_title = MenuObjectT("Maps"),
            End,
        End;

    if(menu_strip)
    {
      Object *entry;
      entry = MUI_MakeObject(MUIO_Menuitem,"World-Map",NULL,0,0);
      DoMethod(entry,MUIM_Notify,MUIA_Menuitem_Trigger, MUIV_EveryTime, entry,5, MUIM_CallHook, &standart_hook, diplomacy_world_map, pdialog, plr1->player_no);
      DoMethod(menu_title,MUIM_Family_AddTail, entry);

      entry = MUI_MakeObject(MUIO_Menuitem,"Sea-Map",NULL,0,0);
      DoMethod(entry,MUIM_Notify,MUIA_Menuitem_Trigger, MUIV_EveryTime, entry,5, MUIM_CallHook, &standart_hook, diplomacy_sea_map, pdialog, plr1->player_no);
      DoMethod(menu_title,MUIM_Family_AddTail, entry);
      set(pdialog->plr1_maps_button,MUIA_ContextMenu,menu_strip);
    }

    pdialog->plr0_adv_menu = menu_strip = MenustripObject,
        Child, menu_title = MenuObjectT("Advances"), End,
        End;

    if(menu_strip)
    {
      fill_diplomacy_tech_menu(menu_title, pdialog, plr0, plr1);
      set(pdialog->plr0_adv_button,MUIA_ContextMenu,menu_strip);
    }

    pdialog->plr1_adv_menu = menu_strip = MenustripObject,
        Child, menu_title = MenuObjectT("Advances"), End,
        End;

    if(menu_strip)
    {
      fill_diplomacy_tech_menu(menu_title, pdialog, plr1, plr0);
      set(pdialog->plr1_adv_button,MUIA_ContextMenu,menu_strip);
    }

    pdialog->plr0_cities_menu = menu_strip = MenustripObject,
        Child, menu_title = MenuObjectT("Cities"), End,
        End;

    if(menu_strip)
    {
      fill_diplomacy_city_menu(menu_title, pdialog, plr0, plr1);
      set(pdialog->plr0_cities_button,MUIA_ContextMenu,menu_strip);
    }

    pdialog->plr1_cities_menu = menu_strip = MenustripObject,
        Child, menu_title = MenuObjectT("Cities"), End,
        End;

    if(menu_strip)
    {
      fill_diplomacy_city_menu(menu_title, pdialog, plr1, plr0);
      set(pdialog->plr1_cities_button,MUIA_ContextMenu,menu_strip);
    }


    pdialog->plr0_pacts_menu = menu_strip = MenustripObject,
        Child, menu_title = MenuObjectT("Pacts"),
            End,
        End;

    if(menu_strip)
    {
      Object *entry;
      entry = MUI_MakeObject(MUIO_Menuitem,"Cease-fire",NULL,0,0);
      DoMethod(entry,MUIM_Notify,MUIA_Menuitem_Trigger, MUIV_EveryTime, entry,4, MUIM_CallHook, &standart_hook, diplomacy_ceasefire, pdialog);
      DoMethod(menu_title,MUIM_Family_AddTail, entry);

      entry = MUI_MakeObject(MUIO_Menuitem,"Peace",NULL,0,0);
      DoMethod(entry,MUIM_Notify,MUIA_Menuitem_Trigger, MUIV_EveryTime, entry,4, MUIM_CallHook, &standart_hook, diplomacy_peace, pdialog);
      DoMethod(menu_title,MUIM_Family_AddTail, entry);

      entry = MUI_MakeObject(MUIO_Menuitem,"Alliance",NULL,0,0);
      DoMethod(entry,MUIM_Notify,MUIA_Menuitem_Trigger, MUIV_EveryTime, entry,4, MUIM_CallHook, &standart_hook, diplomacy_alliance, pdialog);
      DoMethod(menu_title,MUIM_Family_AddTail, entry);
      set(pdialog->plr0_pacts_button,MUIA_ContextMenu, menu_strip);
    }

    settextf(plr0_text,"The %s offerings", get_nation_name(plr0->nation));
    settextf(plr1_text,"The %s offerings", get_nation_name(plr1->nation));
    settextf(plr0_gold_text,"Gold(max %d)", plr0->economic.gold);
    settextf(plr1_gold_text,"Gold(max %d)", plr1->economic.gold);
    settextf(plr0_view_text, "%s view:", get_nation_name(plr0->nation));
    settextf(plr1_view_text, "%s view:", get_nation_name(plr1->nation));

    settextf(diplo_text, "This Eternal Treaty\nmarks the results\nof the diplomatic work between\nThe %s %s %s\nand\nThe %s %s %s",
             get_nation_name(plr0->nation),
             get_ruler_title(plr0->government,plr0->is_male,plr0->nation),
             plr0->name,
             get_nation_name(plr1->nation),
             get_ruler_title(plr1->government,plr1->is_male,plr1->nation),
             plr1->name);

    DoMethod(pdialog->wnd, MUIM_Notify, MUIA_Window_CloseRequest,TRUE,app,4,MUIM_CallHook, &standart_hook, diplomacy_close,pdialog);
    DoMethod(accept_treaty, MUIM_Notify, MUIA_Pressed,FALSE, app,4,MUIM_CallHook, &standart_hook, diplomacy_accept_treaty,pdialog);
    DoMethod(cancel_meeting, MUIM_Notify, MUIA_Pressed,FALSE, app,4,MUIM_CallHook, &standart_hook, diplomacy_close,pdialog);
    DoMethod(erase_clause, MUIM_Notify, MUIA_Pressed,FALSE, app,4,MUIM_CallHook, &standart_hook, diplomacy_erase_clause,pdialog);
    DoMethod(pdialog->plr0_gold_integer, MUIM_Notify, MUIA_String_Acknowledge, MUIV_EveryTime, app, 6, MUIM_CallHook, &standart_hook, diplomacy_gold, pdialog, plr0->player_no,pdialog->plr0_gold_integer);
    DoMethod(pdialog->plr1_gold_integer, MUIM_Notify, MUIA_String_Acknowledge, MUIV_EveryTime, app, 6, MUIM_CallHook, &standart_hook, diplomacy_gold, pdialog, plr1->player_no,pdialog->plr1_gold_integer);

    DoMethod(app,OM_ADDMEMBER,pdialog->wnd);
    return pdialog;
  }

  FreeVec(pdialog);
  return NULL;
}

/**************************************************************************
...
**************************************************************************/
static void update_diplomacy_dialog(struct Diplomacy_dialog *pdialog)
{
  struct genlist_iterator  myiter;
  char buf[128];

  set(pdialog->clauses_listview,MUIA_NList_Quiet,TRUE);
  DoMethod(pdialog->clauses_listview, MUIM_NList_Clear);

  genlist_iterator_init(&myiter, &pdialog->treaty.clauses, 0);
  
  for(;ITERATOR_PTR(myiter); ITERATOR_NEXT(myiter)) {
    struct Clause *pclause=(struct Clause *)ITERATOR_PTR(myiter);
    client_diplomacy_clause_string(buf, sizeof(buf), pclause);
    DoMethod(pdialog->clauses_listview,MUIM_NList_InsertSingle, buf, MUIV_NList_Insert_Bottom);
  }

  set(pdialog->clauses_listview,MUIA_NList_Quiet,FALSE);
  set(pdialog->plr0_thumb_sprite, MUIA_Sprite_Sprite,get_thumb_sprite(pdialog->treaty.accept0));
  set(pdialog->plr1_thumb_sprite, MUIA_Sprite_Sprite,get_thumb_sprite(pdialog->treaty.accept1));
}

/*****************************************************************
...
*****************************************************************/
static void close_diplomacy_dialog(struct Diplomacy_dialog *pdialog)
{
  if(pdialog)
  {
    set(pdialog->wnd,MUIA_Window_Open,FALSE);
    DoMethod(app,OM_REMMEMBER,pdialog->wnd);
    MUI_DisposeObject(pdialog->wnd);
    genlist_unlink(&diplomacy_dialogs, pdialog);
    FreeVec(pdialog);
  }
}

/*****************************************************************
...
*****************************************************************/
struct Diplomacy_dialog *find_diplomacy_dialog(struct player *plr0, 
					       struct player *plr1)
{
  struct genlist_iterator myiter;

  if(!diplomacy_dialogs_list_has_been_initialised) {
    genlist_init(&diplomacy_dialogs);
    diplomacy_dialogs_list_has_been_initialised=1;
  }
  
  genlist_iterator_init(&myiter, &diplomacy_dialogs, 0);
    
  for(; ITERATOR_PTR(myiter); ITERATOR_NEXT(myiter)) {
    struct Diplomacy_dialog *pdialog= (struct Diplomacy_dialog *)ITERATOR_PTR(myiter);
    if((pdialog->treaty.plr0==plr0 && pdialog->treaty.plr1==plr1) ||
       (pdialog->treaty.plr0==plr1 && pdialog->treaty.plr1==plr0))
      return pdialog;
  }
  return 0;
}
