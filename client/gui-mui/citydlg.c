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

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include <libraries/mui.h>
#include <mui/NListview_MCC.h>

#include <clib/alib_protos.h>
#include <proto/exec.h>
#include <proto/utility.h>
#include <proto/muimaster.h>
#include <exec/memory.h>

#include "city.h"
#include "fcintl.h"
#include "game.h"
#include "genlist.h"
#include "map.h"
#include "mem.h"
#include "packets.h"
#include "player.h"
#include "shared.h"

#include "cityrep.h"
#include "clinet.h"
#include "colors.h"
#include "control.h"
#include "dialogs.h"
#include "graphics.h"
#include "helpdlg.h"
#include "inputdlg.h"
#include "mapctrl.h"
#include "mapview.h"
#include "support.h"
#include "repodlgs.h"
#include "tilespec.h"
#include "wldlg.h"

#include "gui_main.h"
#include "citydlg.h"
#include "autogroupclass.h"
#include "muistuff.h"
#include "mapclass.h"
#include "transparentstringclass.h"
#include "worklistclass.h"

#define NUM_CITYOPT_TOGGLES 5

struct city_prod
{
  Object *wnd;
  struct city *pcity;
  Object *available_listview;
  struct Hook available_disphook;
};

struct city_dialog
{
  struct city *pcity;

  Object *wnd;

  Object *citizen_group;
  Object *citizen_left_space;
  Object *citizen_right_space;
  Object *citizen2_group;

  Object *name_transparentstring;
  Object *title_text;
  Object *food_text;
  Object *shield_text;
  Object *trade_text;
  Object *gold_text;
  Object *luxury_text;
  Object *science_text;
  Object *granary_text;
  Object *pollution_text;

  Object *map_area;

  Object *prod_gauge;
  Object *buy_button;
  Object *change_button;
  struct Hook imprv_disphook;
  Object *imprv_listview;
  Object *sell_button;
  Object *worklist_button;

  Object *present_group; /* auto group */
  Object *supported_group; /* auto group */

  Object *close_button;
  Object *trade_button;
  Object *activateunits_button;
  Object *unitlist_button;
  Object *configure_button;

  struct city_prod prod;

  int sell_id;
  Object *sell_wnd;

  Object *cityopt_wnd;
  Object *cityopt_cycle;
  Object *cityopt_checks[NUM_CITYOPT_TOGGLES];

  Object *worklist_wnd;
};

/****************************************************************
 ...
*****************************************************************/
static void request_city_change_production(struct city *pcity, int id, int is_unit_id)
{
  struct packet_city_request packet;

  packet.city_id = pcity->id;
  packet.name[0] = '\0';
  packet.worklist.name[0] = '\0';
  packet.build_id = id;
  packet.is_build_id_unit_id = is_unit_id;

  send_packet_city_request(&aconnection, &packet, PACKET_CITY_CHANGE);
}
/****************************************************************
 ...
*****************************************************************/
static void request_city_change_specialist(struct city *pcity, int from, int to)
{
  struct packet_city_request packet;

  packet.city_id = pcity->id;
  packet.name[0] = '\0';
  packet.worklist.name[0] = '\0';
  packet.specialist_from = from;
  packet.specialist_to = to;

  send_packet_city_request(&aconnection, &packet, PACKET_CITY_CHANGE_SPECIALIST);
}
/****************************************************************
 ...
*****************************************************************/
static void request_city_toggle_worker(struct city *pcity, int xtile, int ytile)
{
  if(is_valid_city_coords(xtile, ytile))
  {
    struct packet_city_request packet;
    packet.city_id = pcity->id;
    packet.worker_x = xtile;
    packet.worker_y = ytile;
    packet.name[0] = '\0';
    packet.worklist.name[0] = '\0';

    if (pcity->city_map[xtile][ytile] == C_TILE_WORKER)
      send_packet_city_request(&aconnection, &packet, PACKET_CITY_MAKE_SPECIALIST);
    else if (pcity->city_map[xtile][ytile] == C_TILE_EMPTY)
      send_packet_city_request(&aconnection, &packet, PACKET_CITY_MAKE_WORKER);
  }
}
/****************************************************************
 ...
*****************************************************************/
static void request_city_buy(struct city *pcity)
{
  struct packet_city_request packet;
  packet.city_id = pcity->id;
  packet.name[0] = '\0';
  packet.worklist.name[0] = '\0';
  send_packet_city_request(&aconnection, &packet, PACKET_CITY_BUY);
}
/****************************************************************
 ...
*****************************************************************/
static void request_city_sell(struct city *pcity, int sell_id)
{
  struct packet_city_request packet;

  packet.city_id = pcity->id;
  packet.build_id = sell_id;
  packet.name[0] = '\0';
  packet.worklist.name[0] = '\0';
  send_packet_city_request(&aconnection, &packet, PACKET_CITY_SELL);
}

/* End GUI Independed */

static struct genlist dialog_list;
static int dialog_list_has_been_initialised;

static struct city_dialog *get_city_dialog(struct city *pcity);
static struct city_dialog *create_city_dialog(struct city *pcity);
static void close_city_dialog(struct city_dialog *pdialog);

static void city_dialog_update_improvement_list(struct city_dialog *pdialog);
static void city_dialog_update_title(struct city_dialog *pdialog);
static void city_dialog_update_supported_units(struct city_dialog *pdialog, int id);
static void city_dialog_update_present_units(struct city_dialog *pdialog, int id);
static void city_dialog_update_citizens(struct city_dialog *pdialog);
static void city_dialog_update_map(struct city_dialog *pdialog);
static void city_dialog_update_production(struct city_dialog *pdialog);
static void city_dialog_update_output(struct city_dialog *pdialog);
static void city_dialog_update_building(struct city_dialog *pdialog);
static void city_dialog_update_storage(struct city_dialog *pdialog);
static void city_dialog_update_pollution(struct city_dialog *pdialog);

static void open_cityopt_dialog(struct city_dialog *pdialog);

/****************************************************************
...
*****************************************************************/
static struct city_dialog *get_city_dialog(struct city *pcity)
{
  struct genlist_iterator myiter;

  if (!dialog_list_has_been_initialised)
  {
    genlist_init(&dialog_list);
    dialog_list_has_been_initialised = 1;
  }

  genlist_iterator_init(&myiter, &dialog_list, 0);

  for (; ITERATOR_PTR(myiter); ITERATOR_NEXT(myiter))
    if (((struct city_dialog *) ITERATOR_PTR(myiter))->pcity == pcity)
      return ITERATOR_PTR(myiter);

  return 0;
}

/****************************************************************
...
*****************************************************************/
int city_dialog_is_open(struct city *pcity)
{
  if (get_city_dialog(pcity)) {
    return 1;
  } else {
    return 0;
  }
}

/****************************************************************
...
*****************************************************************/
static void refresh_this_city_dialog(struct city_dialog *pdialog)
{
  struct city *pcity = pdialog->pcity;
  int units = (unit_list_size(&map_get_tile(pcity->x, pcity->y)->units) ? TRUE : FALSE);

  city_dialog_update_improvement_list(pdialog);
  city_dialog_update_title(pdialog);
  city_dialog_update_supported_units(pdialog, 0);
  city_dialog_update_present_units(pdialog, 0);
  city_dialog_update_citizens(pdialog);
  city_dialog_update_map(pdialog);
  city_dialog_update_production(pdialog);
  city_dialog_update_output(pdialog);
  city_dialog_update_building(pdialog);
  city_dialog_update_storage(pdialog);
  city_dialog_update_pollution(pdialog);

  set(pdialog->trade_button, MUIA_Disabled, city_num_trade_routes(pcity) ? FALSE : TRUE);
  set(pdialog->activateunits_button, MUIA_Disabled, !units);
  set(pdialog->unitlist_button, MUIA_Disabled, !units);
  set(pdialog->configure_button, MUIA_Disabled, FALSE);
}

/****************************************************************
 Refresh every city dialog displaying this city
*****************************************************************/
void refresh_city_dialog(struct city *pcity)
{
  /* from get_city_dialog() */
  struct genlist_iterator myiter;

  if (!dialog_list_has_been_initialised)
  {
    genlist_init(&dialog_list);
    dialog_list_has_been_initialised = 1;
  }

  genlist_iterator_init(&myiter, &dialog_list, 0);

  for (; ITERATOR_PTR(myiter); ITERATOR_NEXT(myiter))
  {
    if (((struct city_dialog *) ITERATOR_PTR(myiter))->pcity == pcity)
    {
      struct city_dialog *pdialog = (struct city_dialog *) ITERATOR_PTR(myiter);

      refresh_this_city_dialog(pdialog);
      if (pcity->owner != game.player_idx)
      {
	/* Set the buttons we do not want live while a Diplomat investigates */
	set(pdialog->buy_button, MUIA_Disabled, TRUE);
	set(pdialog->sell_button, MUIA_Disabled, TRUE);
	set(pdialog->change_button, MUIA_Disabled, TRUE);
	set(pdialog->worklist_button, MUIA_Disabled, TRUE);
	set(pdialog->activateunits_button, MUIA_Disabled, TRUE);
	set(pdialog->unitlist_button, MUIA_Disabled, TRUE);
	set(pdialog->configure_button, MUIA_Disabled, TRUE);
      }
    }
  }

  if (pcity->owner == game.player_idx)
  {
    city_report_dialog_update_city(pcity);
    economy_report_dialog_update();
  }
}

/****************************************************************
...
*****************************************************************/
void refresh_unit_city_dialogs(struct unit *punit)
{
  struct city *pcity_sup, *pcity_pre;
  struct city_dialog *pdialog;

  pcity_sup = player_find_city_by_id(game.player_ptr, punit->homecity);
  pcity_pre = map_get_city(punit->x, punit->y);

  if (pcity_sup && (pdialog = get_city_dialog(pcity_sup)))
    city_dialog_update_supported_units(pdialog, 0);

  if (pcity_pre && (pdialog = get_city_dialog(pcity_pre)))
    city_dialog_update_present_units(pdialog, 0);
}

/****************************************************************
popup the dialog 10% inside the main-window 
*****************************************************************/
void popup_city_dialog(struct city *pcity, int make_modal)
{
  struct city_dialog *pdialog;

  if (!(pdialog = get_city_dialog(pcity)))
    pdialog = create_city_dialog(pcity);

  if (pdialog)
    set(pdialog->wnd, MUIA_Window_Open, TRUE);
}

/****************************************************************
popdown the dialog 
*****************************************************************/
void popdown_city_dialog(struct city *pcity)
{
  struct city_dialog *pdialog;

  if ((pdialog = get_city_dialog(pcity)))
    close_city_dialog(pdialog);
}

/****************************************************************
popdown all dialogs
*****************************************************************/
void popdown_all_city_dialogs(void)
{
  if (!dialog_list_has_been_initialised)
  {
    return;
  }
  while (genlist_size(&dialog_list))
  {
    close_city_dialog(genlist_get(&dialog_list, 0));
  }
}

/****************************************************************/

struct city_node
{
  struct MinNode node;
  struct city *pcity;
};

struct city_browse_msg
{
  struct city_dialog *pdialog;
  ULONG direction;
};				/* used by city_browse() */

struct city_map_msg
{
  struct city_dialog *pdialog;
  struct Map_Click *click;
};				/* used by city_click() */

struct city_citizen_msg
{
  struct city_dialog *pdialog;
  ULONG type;
};				/* used by city_citizen() */

struct city_unit_msg
{
  struct city_dialog *pdialog;
  struct unit *punit;
};				/* used by city_present() */

/****************************************************************
 Sort the city list alphabetically
*****************************************************************/
static void city_list_sort_amiga(struct MinList *list)
{
  BOOL notfinished = TRUE;

  /* Sort list (quick & dirty bubble sort) */
  while (notfinished)
  {
    struct city_node *first;

    /* Reset not finished flag */
    notfinished = FALSE;

    /* Get first node */
    if ((first = (struct city_node *) List_First(list)))
    {
      struct city_node *second;

      /* One bubble sort round */
      while ((second = (struct city_node *) Node_Next(first)))
      {
	if (Stricmp(first->pcity->name, second->pcity->name) > 0)
	{
	  Remove((struct Node *) first);
	  Insert((struct List *) list, (struct Node *) first, (struct Node *) second);
	  notfinished = TRUE;
	}
	else
	  first = second;
      }
    }
  }
}

/**************************************************************************
 Display function for the listview in the production window
**************************************************************************/
HOOKPROTO(city_prod_display, int, char **array, APTR msg)
{
  static char name[256];
  static char info[32];
  static char cost[32];
  static char rounds[32];
  ULONG which = (ULONG) msg;

  struct city *pcity = (struct city *) hook->h_Data;

  if (which)
  {
    if (which >= 10000)
    {
      if (which == 20000)
      {
	sz_strlcpy(name, _("\33u\338Units\33n"));
	info[0] = cost[0] = rounds[0] = 0;
      }
      else
      {
	if (which == 20001)
	{
	  sz_strlcpy(name, _("\33u\338Improvements\33n"));
	  info[0] = cost[0] = rounds[0] = 0;
	}
	else
	{
	  /* Unit */
	  which -= 10000;
	  sz_strlcpy(name, unit_name(which));

	  {
	    /* from unit.h get_unit_name() */
	    struct unit_type *ptype;
	    ptype = get_unit_type(which);
	    if (ptype->fuel > 0)
	      my_snprintf(info, sizeof(info), "%d/%d/%d(%d)", ptype->attack_strength,
		      ptype->defense_strength,
		ptype->move_rate / 3, (ptype->move_rate / 3) * ptype->fuel);
	    else
	      my_snprintf(info, sizeof(info), "%d/%d/%d", ptype->attack_strength,
		      ptype->defense_strength, ptype->move_rate / 3);

	  }

	  my_snprintf(cost, sizeof(cost), "%d", get_unit_type(which)->build_cost);
	  my_snprintf(rounds, sizeof(rounds), "%d",
		      city_turns_to_build(pcity, which, TRUE, TRUE));
	}
      }
    }
    else
    {
      which--;
      sz_strlcpy(name, get_improvement_type(which)->name);
      info[0] = 0;

      {
	/* from city.c get_impr_name_ex() */
	if (wonder_replacement(pcity, which))
        {
          sz_strlcpy(info, "*");
	}
	else
	{
	  if (is_wonder(which))
	  {
	    sz_strlcpy(info, _("Wonder"));
	    if (game.global_wonders[which])
	      sz_strlcpy(info, _("Built"));
	    if (wonder_obsolete(which))
	      sz_strlcpy(info, _("Obsolete"));
	  }
	}
      }

      if (which != B_CAPITAL)
      {
	my_snprintf(cost, sizeof(cost), "%d", get_improvement_type(which)->build_cost);
	my_snprintf(rounds, sizeof(rounds), "%d",
		    city_turns_to_build(pcity, which, FALSE, TRUE));
      }
      else
      {
	sz_strlcpy(cost, "--");
	sz_strlcpy(rounds, "--");
      }
    }
    *array++ = name;
    *array++ = info;
    *array++ = rounds;
    *array++ = cost;
    *array = NULL;
  }
  else
  {
    *array++ = _("Type");
    *array++ = _("Info");
    *array++ = _("Rounds");
    *array++ = _("Cost");
    *array = NULL;
  }
  return 0;
}

/**************************************************************************
 Display function for the listview in the city window
**************************************************************************/
HOOKPROTO(city_imprv_display, int, char **array, APTR msg)
{
  static char name[256];
  static char cost[32];
  ULONG which = (ULONG) msg;

  if (which)
  {
    struct city_dialog *pdialog = (struct city_dialog *) hook->h_Data;
    which--;
    my_snprintf(name, sizeof(name), "%s", get_impr_name_ex(pdialog->pcity, which));
    my_snprintf(cost, sizeof(cost), "%d", improvement_upkeep(pdialog->pcity, which));
    *array++ = name;
    *array = cost;
  }
  else
  {
    *array++ = _("Type");
    *array = _("Upkeep");
  }
  return 0;
}

/**************************************************************************
 Must be called from the Application object so it is safe to
 dispose the window
**************************************************************************/
static int city_close_real(struct city_dialog **ppdialog)
{
  close_city_dialog(*ppdialog);
  return 0;
}

/**************************************************************************
 Callback for the Close Button (or CloseGadget)
**************************************************************************/
static void city_close(struct city_dialog **ppdialog)
{
  set((*ppdialog)->wnd, MUIA_Window_Open, FALSE);
  DoMethod(app, MUIM_Application_PushMethod, app, 4, MUIM_CallHook, &civstandard_hook, city_close_real, *ppdialog);
}

/**************************************************************************
 Callback for the Yes in the Buy confirmation window
**************************************************************************/
static void city_buy_yes(struct popup_message_data *data)
{
  struct city *pcity = (struct city *) data->data;
  request_city_buy(pcity);
  destroy_message_dialog(data->wnd);
}


/**************************************************************************
 Callback for the No in the Sell confirmation window
**************************************************************************/
static void city_sell_no(struct popup_message_data *data)
{
  struct city_dialog *pdialog = (struct city_dialog *)data->data;
  destroy_message_dialog(data->wnd);
  set(pdialog->sell_button, MUIA_Disabled, FALSE);
  pdialog->sell_wnd = NULL;
  pdialog->sell_id = -1;
}

/**************************************************************************
 Callback for the Yes in the Sell confirmation window
**************************************************************************/
static void city_sell_yes(struct popup_message_data *data)
{
  struct city_dialog *pdialog = (struct city_dialog *) data->data;

  if (pdialog->sell_id >= 0)
    request_city_sell(pdialog->pcity, pdialog->sell_id);

  destroy_message_dialog(data->wnd);
  pdialog->sell_wnd = NULL;
  pdialog->sell_id = -1;
}

/**************************************************************************
 Callback to accept the options in the configure window
**************************************************************************/
static void city_opt_ok(struct city_dialog **ppdialog)
{
  struct city_dialog *pdialog = *ppdialog;
  struct city *pcity = pdialog->pcity;

  if (pcity)
  {
    struct packet_generic_values packet;
    int i, new_options, newcitizen_index = xget(pdialog->cityopt_cycle, MUIA_Cycle_Active);

    new_options = 0;
    for (i = 0; i < NUM_CITYOPT_TOGGLES; i++)
    {
      if (xget(pdialog->cityopt_checks[i], MUIA_Selected))
	new_options |= (1 << i);
    }
    if (newcitizen_index == 1)
    {
      new_options |= (1 << CITYO_NEW_EINSTEIN);
    }
    else if (newcitizen_index == 2)
    {
      new_options |= (1 << CITYO_NEW_TAXMAN);
    }

    packet.value1 = pcity->id;
    packet.value2 = new_options;
    send_packet_generic_values(&aconnection, PACKET_CITY_OPTIONS,
			       &packet);
  }
  set(pdialog->cityopt_wnd, MUIA_Window_Open, FALSE);
}

/**************************************************************************
 Callback for the Trade button
**************************************************************************/
static void city_trade(struct city_dialog **ppdialog)
{
  int i;
  int x = 0, total = 0;
  char buf[512], *bptr=buf;
  struct city_dialog *pdialog = *ppdialog;
  int nleft = sizeof(buf);

  my_snprintf(buf, sizeof(buf),
	     _("These trade routes have been established with %s:\n"),
	     pdialog->pcity->name);
  bptr = end_of_strn(bptr, &nleft);

  for (i = 0; i < 4; i++)
  {
    if (pdialog->pcity->trade[i])
    {
      struct city *pcity;
      x = 1;
      total += pdialog->pcity->trade_value[i];
      if ((pcity = find_city_by_id(pdialog->pcity->trade[i])))
      {
	my_snprintf(bptr, nleft, _("%s: %2d Trade/Turn\n"), pcity->name, pdialog->pcity->trade_value[i]);
	bptr = end_of_strn(bptr, &nleft);
      }
      else
      {
	my_snprintf(bptr, nleft, _("%s: %2d Trade/Turn\n"), _("Unknown"), pdialog->pcity->trade_value[i]);
	bptr = end_of_strn(bptr, &nleft);
      }
    }
  }

  if (!x)
    mystrlcpy(bptr, _("No trade routes exist.\n"), nleft);
  else
    my_snprintf(bptr, nleft, _("\nTotal trade %d Trade/Turn\n"), total);

  popup_message_dialog(pdialog->wnd, _("Trade Routes"), buf,
		       _("_Done"), message_close, 0,
		       0);
}

/****************************************************************
 Callback for the City Name String
*****************************************************************/
static void city_rename(struct city_dialog **ppdialog)
{
  struct city_dialog *pdialog = *ppdialog;
  struct packet_city_request packet;

  packet.city_id=pdialog->pcity->id;
  packet.worklist.name[0] = '\0';
  sz_strlcpy(packet.name, (char*)xget(pdialog->name_transparentstring, MUIA_TransparentString_Contents));
  send_packet_city_request(&aconnection, &packet, PACKET_CITY_RENAME);
}

/**************************************************************************
 Callback for the List button
**************************************************************************/
static void city_unitlist(struct city_dialog **ppdialog)
{
  struct city_dialog *pdialog = *ppdialog;
  struct tile *ptile = map_get_tile(pdialog->pcity->x, pdialog->pcity->y);

  if (unit_list_size(&ptile->units))
    popup_unit_select_dialog(ptile);
}

/**************************************************************************
 Callback for the Activate All button
**************************************************************************/
static void city_activate_units(struct city_dialog **ppdialog)
{
  struct city_dialog *pdialog = *ppdialog;
  int x=pdialog->pcity->x,y=pdialog->pcity->y;
  struct unit_list *punit_list = &map_get_tile(x,y)->units;
  struct unit *pmyunit = NULL;

  if(unit_list_size(punit_list))  {
    unit_list_iterate((*punit_list), punit) {
      if(game.player_idx==punit->owner) {
	pmyunit = punit;
	request_new_unit_activity(punit, ACTIVITY_IDLE);
      }
    } unit_list_iterate_end;
    if (pmyunit)
      set_unit_focus(pmyunit);
  }
}

/**************************************************************************
 Callback for the Configure button
**************************************************************************/
static void city_configure(struct city_dialog **ppdialog)
{
  open_cityopt_dialog(*ppdialog);
}

/**************************************************************************
 Callback for the Change button
**************************************************************************/
static void city_change(struct city_dialog **ppdialog)
{
  popup_city_production_dialog((*ppdialog)->pcity);
}

/****************************************************************
  Commit the changes to the worklist for the city.
*****************************************************************/
static void commit_city_worklist(struct worklist *pwl, void *data)
{
  struct packet_city_request packet;
  struct city_dialog *pdialog = (struct city_dialog *)data;
  int i, id, is_unit;

  /* Update the worklist.  But, remember -- the first element of the 
     worklist is actually just the current build target; don't send it
     to the server as part of the worklist. */
  packet.city_id=pdialog->pcity->id;
  packet.name[0] = '\0';
  packet.worklist.is_valid = 1;
  packet.worklist.name[0] = '\0';
  for (i = 0; i < MAX_LEN_WORKLIST-1; i++) {
    packet.worklist.wlefs[i] = pwl->wlefs[i+1];
    packet.worklist.wlids[i] = pwl->wlids[i+1];
  }
    
  send_packet_city_request(&aconnection, &packet, PACKET_CITY_WORKLIST);

  /* Additionally, if the first element in the worklist changed, then
     send a change build target packet. */
  worklist_peek(pwl, &id, &is_unit);

  if (id != pdialog->pcity->currently_building || 
      is_unit != pdialog->pcity->is_building_unit) {
    /* Change the current target */
    packet.build_id = id;
    packet.is_build_id_unit_id = is_unit;
    send_packet_city_request(&aconnection, &packet, PACKET_CITY_CHANGE);
  }
  pdialog->worklist_wnd = NULL;
}

/****************************************************************
...
*****************************************************************/
static void cancel_city_worklist(void *data)
{
  struct city_dialog *pdialog = (struct city_dialog *)data;
  pdialog->worklist_wnd = NULL;
}


/****************************************************************
  Display the city's worklist.
*****************************************************************/
static void city_worklist(struct city_dialog **ppdialog)
{
  struct city_dialog *pdialog = *ppdialog;

  if (!pdialog->worklist_wnd)
  {
    pdialog->worklist_wnd = popup_worklist(pdialog->pcity->worklist,
    		pdialog->pcity, (void *)pdialog,
    		commit_city_worklist, cancel_city_worklist);
  }
}

/**************************************************************************
 Callback for the browse buttons (switch the city which is display to
 the next or previous. Alphabetically)
**************************************************************************/
static void city_browse(struct city_browse_msg *msg)
{
  struct player *pplayer = get_player(msg->pdialog->pcity->owner);
  struct city *pcity_new;
  struct MinList list;
  struct city_node *node;
  NewList((struct List *) &list);

  city_list_iterate(pplayer->cities, pcity)
  {
    struct city_node *node = malloc_struct(struct city_node);
    if (node)
    {
      node->pcity = pcity;
      AddTail((struct List *) &list, (struct Node *) node);
    }
  }
  city_list_iterate_end

    city_list_sort_amiga(&list);

  node = (struct city_node *) List_First(&list);
  while (node)
  {
    if (node->pcity == msg->pdialog->pcity)
      break;
    node = (struct city_node *) Node_Next(node);
  }

  if (node)
  {
    if (msg->direction)
    {
      node = (struct city_node *) Node_Next(node);
      if (!node)
	node = (struct city_node *) List_First(&list);
    }
    else
    {
      node = (struct city_node *) Node_Prev(node);
      if (!node)
	node = (struct city_node *) List_Last(&list);
    }
  }

  if (node)
  {
    if ((pcity_new = node->pcity))
    {
      msg->pdialog->pcity = pcity_new;
      msg->pdialog->sell_id = -1;
      if (msg->pdialog->sell_wnd)
      {
        destroy_message_dialog(msg->pdialog->sell_wnd);
        msg->pdialog->sell_wnd = NULL;
      }

      set(msg->pdialog->map_area, MUIA_CityMap_City, pcity_new);
      refresh_this_city_dialog(msg->pdialog);
    }
  }
}

/**************************************************************************
 Callback for the Buy button
**************************************************************************/
static void city_buy(struct city_dialog **ppdialog)
{
  struct city_dialog *pdialog = *ppdialog;
  int value;
  char *name;
  char buf[512];

  if (pdialog->pcity->is_building_unit)
  {
    name = get_unit_type(pdialog->pcity->currently_building)->name;
  }
  else
  {
    name = get_impr_name_ex(pdialog->pcity, pdialog->pcity->currently_building);
  }

  value = city_buy_cost(pdialog->pcity);

  if (game.player_ptr->economic.gold >= value)
  {
    my_snprintf(buf, sizeof(buf), _("Buy %s for %d gold?\nTreasury contains %d gold."),
	    name, value, game.player_ptr->economic.gold);

    popup_message_dialog(pdialog->wnd, _("Buy It!"), buf,
			 _("_Yes"), city_buy_yes, pdialog->pcity,
			 _("_No"), message_close, 0,
			 NULL);
  }
  else
  {
    my_snprintf(buf, sizeof(buf), _("%s costs %d gold.\nTreasury contains %d gold."),
	    name, value, game.player_ptr->economic.gold);

    popup_message_dialog(pdialog->wnd, _("Buy It!"), buf,
			 _("_Darn"), message_close, 0,
			 NULL);
  }
}

/**************************************************************************
 Callback for the Sell button
**************************************************************************/
static void city_sell(struct city_dialog **ppdialog)
{
  struct city_dialog *pdialog = *ppdialog;
  LONG sel = xget(pdialog->imprv_listview, MUIA_NList_Active);
  if (sel >= 0 && !pdialog->sell_wnd)
  {
    LONG i = 0;
    char buf[512];
    DoMethod(pdialog->imprv_listview, MUIM_NList_GetEntry, sel, &i);

    if (!i--)
      return;

    if (is_wonder(i))
      return;

    my_snprintf(buf, sizeof(buf), _("Sell %s for %d gold?"), get_impr_name_ex(pdialog->pcity, i),
	    improvement_value(i));

    pdialog->sell_id = i;
    pdialog->sell_wnd = popup_message_dialog(pdialog->wnd,
			_("Sell It!"), buf,
			_("_Yes"), city_sell_yes, pdialog,
			_("_No"), city_sell_no, pdialog,
			NULL);
    set(pdialog->sell_button, MUIA_Disabled, TRUE);
  }
}

/**************************************************************************
 Callback if the user clicked on a map
**************************************************************************/
static void city_click(struct city_map_msg *msg)
{
  struct city *pcity = msg->pdialog->pcity;
  int xtile = msg->click->x;
  int ytile = msg->click->y;

  request_city_toggle_worker(pcity, xtile, ytile);
}

/**************************************************************************
 Callback if the user clicked on a citizen
**************************************************************************/
static void city_citizen(struct city_citizen_msg *msg)
{
  struct city_dialog *pdialog = msg->pdialog;
  struct city *pcity = pdialog->pcity;

  switch (msg->type)
  {
  case 0:
    request_city_change_specialist(pcity, SP_ELVIS, SP_SCIENTIST);
    break;

  case 1:
    request_city_change_specialist(pcity, SP_SCIENTIST, SP_TAXMAN);
    break;

  default:
    request_city_change_specialist(pcity, SP_TAXMAN, SP_ELVIS);
    break;
  }
}

/**************************************************************************
 Callback if the user clicked on a present unit
**************************************************************************/
static void city_present(struct city_unit_msg *data)
{
  request_unit_selected(data->punit);
}

/****************************************************************
 Must be called from the Application object so it is safe to
 dispose the window
*****************************************************************/
static void city_prod_close_real(struct city_prod **ppcprod)
{
  set((*ppcprod)->wnd,MUIA_Window_Open,FALSE);
  DoMethod(app, OM_REMMEMBER, (*ppcprod)->wnd);
  MUI_DisposeObject((*ppcprod)->wnd);
  FreeVec(*ppcprod);
}

/****************************************************************
 city_prod_destroy destroy the object after use
*****************************************************************/
static void city_prod_destroy(struct city_prod **ppcprod)
{
  set((*ppcprod)->wnd, MUIA_Window_Open, FALSE);
  DoMethod(app, MUIM_Application_PushMethod, app, 4, MUIM_CallHook, &civstandard_hook, city_prod_close_real, *ppcprod);
}

/**************************************************************************
 Callback for the Change button in the production window
**************************************************************************/
static void city_prod_change(struct city_prod **ppcprod)
{
  struct city_prod *pcprod = *ppcprod;
  LONG sel = xget(pcprod->available_listview, MUIA_NList_Active);
  if (sel >= 0)
  {
    LONG which = 0;
    LONG is_unit = 0;

    DoMethod(pcprod->available_listview, MUIM_NList_GetEntry, sel, &which);
    if (which >= 10000)
    {
      if (which == 20000 || which == 20001)
	return;
      which -= 10000;
      is_unit = 1;
    }
    else
      which--;

    request_city_change_production(pcprod->pcity, which, is_unit);
    city_prod_destroy(ppcprod);
  }
}

/**************************************************************************
 Callback for the Help button in the production window
**************************************************************************/
static void city_prod_help(struct city_prod **ppcprod)
{
  struct city_prod *pcprod = *ppcprod;
  LONG sel = xget(pcprod->available_listview, MUIA_NList_Active);
  if (sel >= 0)
  {
    LONG which = 0;

    DoMethod(pcprod->available_listview, MUIM_NList_GetEntry, sel, &which);
    if (which >= 10000)
    {
      which -= 10000;
      popup_help_dialog_typed(get_unit_type(which)->name, HELP_UNIT);
    }
    else
    {
      which--;
      if (is_wonder(which))
      {
	popup_help_dialog_typed(get_improvement_name(which), HELP_WONDER);
      }
      else
      {
	popup_help_dialog_typed(get_improvement_name(which), HELP_IMPROVEMENT);
      }
    }
  }
}

/**************************************************************************
 Allocate and initialize a new city production dialog
**************************************************************************/
void popup_city_production_dialog(struct city *pcity)
{
  struct city_prod *pcprod;
  Object *change_button;
  Object *help_button;
  Object *cancel_button;

  if(!(pcprod = (struct city_prod *) AllocVec(sizeof(struct city_prod), MEMF_CLEAR)))
    return;

  pcprod->pcity = pcity;
  pcprod->available_disphook.h_Entry = (HOOKFUNC) city_prod_display;
  pcprod->available_disphook.h_Data = pcity;

  pcprod->wnd = WindowObject,
    MUIA_Window_Title, _("Freeciv - Cityproduction"),
    MUIA_Window_ID, MAKE_ID('P','R','O','D'),
    WindowContents, VGroup,
	Child, pcprod->available_listview = NListviewObject,
	    MUIA_CycleChain, 1,
	    MUIA_NListview_NList, NListObject,
		MUIA_NList_DisplayHook, &pcprod->available_disphook,
		MUIA_NList_Format, "BAR,P=\33c BAR,P=\33c BAR,P=\33r NOBAR,",
		MUIA_NList_Title, TRUE,
		MUIA_NList_AutoVisible, TRUE,
		End,
	    End,
	Child, HGroup,
	    Child, change_button = MakeButton(_("Chan_ge")),
	    Child, help_button = MakeButton(_("_Help")),
	    Child, cancel_button = MakeButton(_("_Cancel")),
	    End,
	End,
    End;

  if(pcprod->wnd)
  {
    int i, pos = 0, current = -1, improv = 0;

    set(pcprod->available_listview, MUIA_NList_Quiet, TRUE);

    DoMethod(pcprod->available_listview, MUIM_NList_Clear);

    for (i = 0; i < game.num_impr_types; i++)
    {
      if (can_build_improvement(pcity, i))
      {
        improv = TRUE;

        DoMethod(pcprod->available_listview, MUIM_NList_InsertSingle, i + 1, MUIV_NList_Insert_Bottom);

        if (i == pcity->currently_building && !pcity->is_building_unit)
         current = ++pos;

        pos++;
      }
    }

    if (improv)
    {
      DoMethod(pcprod->available_listview, MUIM_NList_InsertSingle, 20001, MUIV_NList_Insert_Top);
      if (current == -1)
        pos++;
    }
    DoMethod(pcprod->available_listview, MUIM_NList_InsertSingle, 20000, MUIV_NList_Insert_Bottom);

    for (i = 0; i < game.num_unit_types; i++)
    {
      if (can_build_unit(pcity, i))
      {
        DoMethod(pcprod->available_listview, MUIM_NList_InsertSingle, i + 10000, MUIV_NList_Insert_Bottom);

        if(i == pcity->currently_building && pcity->is_building_unit)
         current = ++pos;

        pos++;
      }
    }

    set(pcprod->available_listview, MUIA_NList_Quiet, FALSE);

    if(current != -1)
    {
      set(pcprod->available_listview, MUIA_NList_Active, current);
    }

    DoMethod(pcprod->wnd, MUIM_Notify, MUIA_Window_CloseRequest, TRUE, app, 4, MUIM_CallHook, &civstandard_hook, city_prod_destroy, pcprod);
    DoMethod(cancel_button, MUIM_Notify, MUIA_Pressed, FALSE, MUIV_Notify_Self, 4, MUIM_CallHook, &civstandard_hook, city_prod_destroy, pcprod);
    DoMethod(pcprod->available_listview, MUIM_Notify, MUIA_NList_DoubleClick, TRUE, MUIV_Notify_Self, 4, MUIM_CallHook, &civstandard_hook, city_prod_change, pcprod);
    DoMethod(change_button, MUIM_Notify, MUIA_Pressed, FALSE, MUIV_Notify_Self, 4, MUIM_CallHook, &civstandard_hook, city_prod_change, pcprod);
    DoMethod(help_button, MUIM_Notify, MUIA_Pressed, FALSE, MUIV_Notify_Self, 4, MUIM_CallHook, &civstandard_hook, city_prod_help, pcprod);

    DoMethod(app,OM_ADDMEMBER,pcprod->wnd);
    SetAttrs(pcprod->wnd, MUIA_Window_Open, TRUE, TAG_DONE);
  }
  else
    FreeVec(pcprod);
}

/**************************************************************************
 Allocate and initialize a new city dialog
**************************************************************************/
static struct city_dialog *create_city_dialog(struct city *pcity)
{
  struct city_dialog *pdialog;
  static char *newcitizen_labels[4];
  Object *cityopt_ok_button;
  Object *cityopt_cancel_button, *next_button, *prev_button;

  newcitizen_labels[0] = _("Workers");
  newcitizen_labels[1] = _("Scientists");
  newcitizen_labels[2] = _("Taxmen");
  newcitizen_labels[3] = NULL;

  pdialog = AllocVec(sizeof(struct city_dialog), 0x10000);
  if (!pdialog)
    return NULL;

  pdialog->pcity = pcity;

  pdialog->imprv_disphook.h_Entry = (HOOKFUNC) city_imprv_display;
  pdialog->imprv_disphook.h_Data = pdialog;

  if (pcity->owner == game.player_idx)
  {
    prev_button = MakeButton("_<");
    next_button = MakeButton("_>");
    pdialog->name_transparentstring = TransparentStringObject,
	MUIA_CycleChain,1,
	End;
  } else prev_button = next_button = pdialog->name_transparentstring = NULL;

  pdialog->wnd = WindowObject,
    MUIA_Window_Title, _("Freeciv - Cityview"),
    MUIA_Window_ID, MAKE_ID('C','I','T','Y'),
    WindowContents, VGroup,
      Child, HGroup,
	Child, HVSpace,
	prev_button?Child:TAG_IGNORE, prev_button,
	Child, VGroup,
	  MUIA_Weight, 200,
	  pdialog->name_transparentstring ? Child:TAG_IGNORE, pdialog->name_transparentstring,
	  Child, pdialog->title_text = TextObject,
	    MUIA_Text_PreParse, "\033c",
            End,
	  End,
	next_button?Child:TAG_IGNORE, next_button,
	Child, HVSpace,
	End,
      Child, pdialog->citizen_group = HGroup,
	Child, pdialog->citizen_left_space = HSpace(0),
	Child, pdialog->citizen_right_space = HSpace(0),
	End,
      Child, VGroup,
	Child, HGroup,
	  Child, VGroup,
	    MUIA_HorizWeight, 0,
	    Child, HorizLineTextObject(_("City Output")),
	    Child, ColGroup(2),
	      Child, MakeLabel(_("Food:")),
	      Child, pdialog->food_text = TextObject, End,
	      Child, MakeLabel(_("Shields:")),
	      Child, pdialog->shield_text = TextObject, End,
	      Child, MakeLabel(_("Trade:")),
	      Child, pdialog->trade_text = TextObject, End,
	      Child, MakeLabel(_("Gold:")),
	      Child, pdialog->gold_text = TextObject, End,
	      Child, MakeLabel(_("Luxury:")),
	      Child, pdialog->luxury_text = TextObject, End,
	      Child, MakeLabel(_("Science:")),
	      Child, pdialog->science_text = TextObject, End,
	      Child, MakeLabel(_("Granary:")),
	      Child, pdialog->granary_text = TextObject, End,
	      Child, MakeLabel(_("Pollution:")),
	      Child, pdialog->pollution_text = TextObject, End,
	      End,
	    Child, VSpace(0),
	    End,
	  Child, VGroup,
	    Child, HorizLineTextObject(_("Citymap")),
	    Child, pdialog->map_area = MakeCityMap(pcity),
	    Child, VSpace(0),
	    End,
	  Child, VGroup,
	    Child, HorizLineTextObject(_("City Improvements")),
	    Child, pdialog->imprv_listview = NListviewObject,
	      MUIA_CycleChain, 1,
	      MUIA_NListview_NList, NListObject,
	        MUIA_NList_DisplayHook, &pdialog->imprv_disphook,
	        MUIA_NList_Format, ",P=\033c",
	        MUIA_NList_Title, TRUE,
	        End,
	      End,
	    Child, pdialog->sell_button = MakeButton(_("_Sell")),
            Child, HorizLineTextObject(_("Production")),
            Child, pdialog->prod_gauge = MyGaugeObject,
	      GaugeFrame,
	      MUIA_Gauge_Horiz, TRUE,
	      End,
	    Child, HGroup,
	      Child, pdialog->buy_button = MakeButton(_("_Buy")),
	      Child, pdialog->worklist_button = MakeButton(_("_Worklist")),
	      Child, pdialog->change_button = MakeButton(_("Chan_ge")),
	      End,
	    End,
	  End,
        Child, BalanceObject, End,
	Child, HGroup,
          Child, VGroup,
	    Child, HorizLineTextObject(_("Units present")),
	    Child, HGroup,
	      Child, pdialog->present_group = AutoGroup,
		MUIA_AutoGroup_DefVertObjects, 3,
		End,
              End,
	    Child, HGroup,
	      MUIA_HorizWeight, 0,
	      Child, HVSpace,
	      Child, pdialog->activateunits_button = MakeButton(_("_Activate Units")),
	      Child, HVSpace,
	      Child, pdialog->unitlist_button = MakeButton(_("_Unit List")),
	      Child, HVSpace,
	      End,
            End,
	  Child, BalanceObject, End,
	  Child, VGroup,
	    Child, HorizLineTextObject(_("Supported Units")),
	    Child, pdialog->supported_group = AutoGroup,
	      MUIA_AutoGroup_DefVertObjects, 3,
	      End,
            End,
	  End,
	End,
      Child, VGroup,
	Child, HorizLineObject,
	Child, HGroup,
	  Child, pdialog->close_button = MakeButton(_("_Close")),
	  Child, HSpace(0),
	  Child, pdialog->trade_button = MakeButton(_("_Trade")),
	  Child, HSpace(0),
	  Child, pdialog->configure_button = MakeButton(_("Con_figure")),
	  End,
        End,
      End,
    End;

  pdialog->cityopt_wnd = WindowObject,
    MUIA_Window_Title, _("Freeciv - Cityoptions"),
    MUIA_Window_ID, MAKE_ID('C','I','T','O'),
    WindowContents, VGroup,
	Child, HGroup,
	    Child, HSpace(0),
	    Child, ColGroup(2),
		Child, MakeLabelLeft(_("_New specialists are")),
		Child, pdialog->cityopt_cycle = MakeCycle(_("_New specialists are"), newcitizen_labels),

		Child, MakeLabelLeft(_("_Disband if build settler at size 1")),
		Child, pdialog->cityopt_checks[4] = MakeCheck(_("_Disband if build settler at size 1"), FALSE),

		Child, MakeLabelLeft(_("Auto-attack vs _land units")),
		Child, pdialog->cityopt_checks[0] = MakeCheck(_("Auto-attack vs _land units"), FALSE),

		Child, MakeLabelLeft(_("Auto-attack vs _sea units")),
		Child, pdialog->cityopt_checks[1] = MakeCheck(_("Auto-attack vs _sea units"), FALSE),

		Child, MakeLabelLeft(_("Auto-attack vs _air units")),
		Child, pdialog->cityopt_checks[3] = MakeCheck(_("Auto-attack vs _air units"), FALSE),

		Child, MakeLabelLeft(_("Auto-attack vs _helicopters")),
		Child, pdialog->cityopt_checks[2] = MakeCheck(_("Auto-attack vs _helicopters"), FALSE),
		End,
	    Child, HSpace(0),
	    End,
	Child, HGroup,
	    Child, cityopt_ok_button = MakeButton(_("_Ok")),
	    Child, cityopt_cancel_button = MakeButton(_("_Cancel")),
	    End,
	End,
    End;

  if(pdialog->wnd && pdialog->cityopt_wnd)
  {
    DoMethod(pdialog->wnd, MUIM_Notify, MUIA_Window_CloseRequest, TRUE, app, 4, MUIM_CallHook, &civstandard_hook, city_close, pdialog);
    DoMethod(pdialog->close_button, MUIM_Notify, MUIA_Pressed, FALSE, app, 4, MUIM_CallHook, &civstandard_hook, city_close, pdialog);
    DoMethod(pdialog->trade_button, MUIM_Notify, MUIA_Pressed, FALSE, app, 4, MUIM_CallHook, &civstandard_hook, city_trade, pdialog);
    DoMethod(pdialog->configure_button, MUIM_Notify, MUIA_Pressed, FALSE, app, 4, MUIM_CallHook, &civstandard_hook, city_configure, pdialog);
    DoMethod(pdialog->unitlist_button, MUIM_Notify, MUIA_Pressed, FALSE, app, 4, MUIM_CallHook, &civstandard_hook, city_unitlist, pdialog);
    DoMethod(pdialog->activateunits_button, MUIM_Notify, MUIA_Pressed, FALSE, app, 4, MUIM_CallHook, &civstandard_hook, city_activate_units, pdialog);

    if (prev_button && next_button)
    {
      set(next_button, MUIA_Weight, 0);
      set(prev_button, MUIA_Weight, 0);
      DoMethod(prev_button, MUIM_Notify, MUIA_Pressed, FALSE, app, 5, MUIM_CallHook, &civstandard_hook, city_browse, pdialog, 0);
      DoMethod(next_button, MUIM_Notify, MUIA_Pressed, FALSE, app, 5, MUIM_CallHook, &civstandard_hook, city_browse, pdialog, 1);
    }

    if (pdialog->name_transparentstring)
    {
      DoMethod(pdialog->name_transparentstring, MUIM_Notify, MUIA_TransparentString_Acknowledge, MUIV_EveryTime, app, 4, MUIM_CallHook, &civstandard_hook, city_rename, pdialog);
    }

    DoMethod(pdialog->map_area, MUIM_Notify, MUIA_CityMap_Click, MUIV_EveryTime, app, 5, MUIM_CallHook, &civstandard_hook, city_click, pdialog, MUIV_TriggerValue);
    DoMethod(pdialog->change_button, MUIM_Notify, MUIA_Pressed, FALSE, app, 4, MUIM_CallHook, &civstandard_hook, city_change, pdialog);
    DoMethod(pdialog->worklist_button, MUIM_Notify, MUIA_Pressed, FALSE, app, 4, MUIM_CallHook, &civstandard_hook, city_worklist, pdialog);
    DoMethod(pdialog->buy_button, MUIM_Notify, MUIA_Pressed, FALSE, app, 4, MUIM_CallHook, &civstandard_hook, city_buy, pdialog);
    DoMethod(pdialog->sell_button, MUIM_Notify, MUIA_Pressed, FALSE, app, 4, MUIM_CallHook, &civstandard_hook, city_sell, pdialog);

    DoMethod(pdialog->cityopt_wnd, MUIM_Notify, MUIA_Window_CloseRequest, TRUE, MUIV_Notify_Self, 3, MUIM_Set, MUIA_Window_Open, FALSE);
    DoMethod(cityopt_cancel_button, MUIM_Notify, MUIA_Pressed, FALSE, MUIV_Notify_Window, 3, MUIM_Set, MUIA_Window_Open, FALSE);
    DoMethod(cityopt_ok_button, MUIM_Notify, MUIA_Pressed, FALSE, MUIV_Notify_Self, 4, MUIM_CallHook, &civstandard_hook, city_opt_ok, pdialog);

    DoMethod(app, OM_ADDMEMBER, pdialog->wnd);
    DoMethod(app, OM_ADDMEMBER, pdialog->cityopt_wnd);

    genlist_insert(&dialog_list, pdialog, 0);
    refresh_city_dialog(pdialog->pcity);
    return pdialog;
  }

  if (pdialog->wnd)
    MUI_DisposeObject(pdialog->wnd);
  if (pdialog->cityopt_wnd)
    MUI_DisposeObject(pdialog->cityopt_wnd);

  FreeVec(pdialog);
  return NULL;
}

/****************************************************************
...
*****************************************************************/
static void city_dialog_update_pollution(struct city_dialog *pdialog)
{
  /* TODO: different colors? */
  char *fmt;
  if (pdialog->pcity->pollution >= 10) fmt = MUIX_B"%3ld";
  else fmt = "%3ld";
  settextf(pdialog->pollution_text, fmt, pdialog->pcity->pollution);
}

/****************************************************************
...
*****************************************************************/
static void city_dialog_update_storage(struct city_dialog *pdialog)
{
  struct city *pcity = pdialog->pcity;
  settextf(pdialog->granary_text, "%3ld/%3ld", pcity->food_stock, 
	   city_granary_size(pcity->size));
}

/****************************************************************
...
*****************************************************************/
static void city_dialog_update_building(struct city_dialog *pdialog)
{
  char buf[32], buf2[64], buf3[128];
  struct city *pcity = pdialog->pcity;
  int turns;

  int max_shield;
  int shield;

  set(pdialog->buy_button, MUIA_Disabled, pcity->did_buy);
  set(pdialog->sell_button, MUIA_Disabled, pcity->did_sell || pdialog->sell_wnd);

  if (pcity->is_building_unit)
  {
    turns = city_turns_to_build(pcity, pcity->currently_building,
				TRUE, TRUE);
    shield = pcity->shield_stock;
    max_shield = get_unit_type(pcity->currently_building)->build_cost;

    my_snprintf(buf, sizeof(buf),
		PL_("%3d/%3d %3d turn", "%3d/%3d %3d turns", turns),
		shield, max_shield, turns);
    sz_strlcpy(buf2, get_unit_type(pcity->currently_building)->name);
  }
  else
  {
    if (pcity->currently_building == B_CAPITAL)
    {
      /* Capitalization is special, you can't buy it or finish making it */
      my_snprintf(buf, sizeof(buf),"%d/XXX", pcity->shield_stock);
      set(pdialog->buy_button, MUIA_Disabled, TRUE);

      shield = 0;
      max_shield = 1;
    }
    else
    {
      turns = city_turns_to_build(pcity, pcity->currently_building,
				  FALSE, TRUE);
      shield = pcity->shield_stock;
      max_shield = get_improvement_type(pcity->currently_building)->build_cost;

      my_snprintf(buf, sizeof(buf),
		  PL_("%3d/%3d %3d turn", "%3d/%3d %3d turns", turns),
		  shield, max_shield, turns);

    }

    sz_strlcpy(buf2, get_impr_name_ex(pcity, pcity->currently_building));
  }

  if (!worklist_is_empty(pcity->worklist))
  {
    my_snprintf(buf3, sizeof(buf3), _("%s (%s) (worklist)"), buf, buf2);
  } else
  {
    my_snprintf(buf3, sizeof(buf3), "%s (%s)", buf, buf2);
  }

  DoMethod(pdialog->prod_gauge, MUIM_MyGauge_SetGauge, shield, max_shield, buf3);
}


/****************************************************************
...
*****************************************************************/
static void city_dialog_update_production(struct city_dialog *pdialog)
{
  struct city *pcity = pdialog->pcity;
  settextf(pdialog->food_text, "%2d (%+2d)", pcity->food_prod, pcity->food_surplus);
  settextf(pdialog->shield_text, "%2d (%+2d)", pcity->shield_prod, pcity->shield_surplus);
  settextf(pdialog->trade_text, "%2d (%+2d)", pcity->trade_prod + pcity->corruption, pcity->trade_prod);
}
/****************************************************************
...
*****************************************************************/
static void city_dialog_update_output(struct city_dialog *pdialog)
{
  struct city *pcity = pdialog->pcity;
  settextf(pdialog->gold_text, "%2d (%+2d)", pcity->tax_total, city_gold_surplus(pcity));
  settextf(pdialog->luxury_text, "%2d", pcity->luxury_total);
  settextf(pdialog->science_text, "%2d", pcity->science_total);
}


/****************************************************************
...
*****************************************************************/
static void city_dialog_update_map(struct city_dialog *pdialog)
{
  DoMethod(pdialog->map_area, MUIM_CityMap_Refresh);
}

/****************************************************************
 Updates the displayed citizens. TODO: Optimize it (only one
 group)
*****************************************************************/
static void city_dialog_update_citizens(struct city_dialog *pdialog)
{
  int n;
  struct city *pcity = pdialog->pcity;

  DoMethod(pdialog->citizen_group, MUIM_Group_InitChange);
  if (pdialog->citizen2_group)
  {
    DoMethod(pdialog->citizen_group, OM_REMMEMBER, pdialog->citizen2_group);
    MUI_DisposeObject(pdialog->citizen2_group);
  }

  pdialog->citizen2_group = HGroup, GroupSpacing(0), End;

  /* maybe add an i < NUM_CITIZENS_SHOWN check into every loop with own counter i */

  for (n = 0; n < pcity->ppl_happy[4]; n++)
  {
    Object *o = MakeSprite(get_citizen_sprite(5 + n % 2));
    if (o)
      DoMethod(pdialog->citizen2_group, OM_ADDMEMBER, o);
  }

  for (n = 0; n < pcity->ppl_content[4]; n++)
  {
    Object *o = MakeSprite(get_citizen_sprite(3 + n % 2));
    if (o)
      DoMethod(pdialog->citizen2_group, OM_ADDMEMBER, o);
  }


  for (n = 0; n < pcity->ppl_unhappy[4]; n++)
  {
    Object *o = MakeSprite(get_citizen_sprite(7 + n % 2));
    if (o)
      DoMethod(pdialog->citizen2_group, OM_ADDMEMBER, o);
  }

  for (n = 0; n < pcity->ppl_angry[4]; n++) {
    Object *o = MakeSprite(get_citizen_sprite(9 + n % 2));
    if (o)
      DoMethod(pdialog->citizen2_group, OM_ADDMEMBER, o);
  }

  for (n = 0; n < pcity->ppl_elvis; n++)
  {
    Object *o = MakeSprite(get_citizen_sprite(0));
    if (o)
    {
      DoMethod(pdialog->citizen2_group, OM_ADDMEMBER, o);
      DoMethod(o, MUIM_Notify, MUIA_Pressed, FALSE, o, 5, MUIM_CallHook, &civstandard_hook, city_citizen, pdialog, 0);
    }
  }

  for (n = 0; n < pcity->ppl_scientist; n++)
  {
    Object *o = MakeSprite(get_citizen_sprite(1));
    if (o)
    {
      DoMethod(pdialog->citizen2_group, OM_ADDMEMBER, o);
      DoMethod(o, MUIM_Notify, MUIA_Pressed, FALSE, o, 5, MUIM_CallHook, &civstandard_hook, city_citizen, pdialog, 1);
    }
  }

  for (n = 0; n < pcity->ppl_taxman; n++)
  {
    Object *o = MakeSprite(get_citizen_sprite(2));

    if (o)
    {
      DoMethod(pdialog->citizen2_group, OM_ADDMEMBER, o);
      DoMethod(o, MUIM_Notify, MUIA_Pressed, FALSE, o, 5, MUIM_CallHook, &civstandard_hook, city_citizen, pdialog, 2);
    }
  }

  DoMethod(pdialog->citizen_group, OM_ADDMEMBER, pdialog->citizen2_group);

  DoMethod(pdialog->citizen_group, MUIM_Group_Sort,
	   pdialog->citizen_left_space, pdialog->citizen2_group, pdialog->citizen_right_space, NULL);

  DoMethod(pdialog->citizen_group, MUIM_Group_ExitChange);
}

/****************************************************************
...
*****************************************************************/
static void city_dialog_update_supported_units(struct city_dialog *pdialog,
					int unitid)
{
  struct unit_list *plist;
  struct genlist_iterator myiter;
  struct unit *punit;

  /* TODO: use unit id */

  DoMethod(pdialog->supported_group, MUIM_Group_InitChange);
  DoMethod(pdialog->supported_group, MUIM_AutoGroup_DisposeChilds);


  if(pdialog->pcity->owner != game.player_idx) {
    plist = &(pdialog->pcity->info_units_supported);
  } else {
    plist = &(pdialog->pcity->units_supported);
  }

  genlist_iterator_init(&myiter, &(plist->list), 0);

  for(;ITERATOR_PTR(myiter); ITERATOR_NEXT(myiter))
  {
    Object *o;

    punit = (struct unit *) ITERATOR_PTR(myiter);
    o = MakeSupportedUnit(punit);
    if (o)
      DoMethod(pdialog->supported_group, OM_ADDMEMBER, o);
  }

  DoMethod(pdialog->supported_group, MUIM_Group_ExitChange);
}

/****************************************************************
...
*****************************************************************/
static void city_dialog_update_present_units(struct city_dialog *pdialog, int unitid)
{
  struct unit_list *plist;
  struct genlist_iterator myiter;
  struct unit *punit;

  /* TODO: use unit id */

  DoMethod(pdialog->present_group, MUIM_Group_InitChange);
  DoMethod(pdialog->present_group, MUIM_AutoGroup_DisposeChilds); 

  if(pdialog->pcity->owner != game.player_idx) {
    plist = &(pdialog->pcity->info_units_present);
  } else {
    plist = &(map_get_tile(pdialog->pcity->x, pdialog->pcity->y)->units);
  }

  genlist_iterator_init(&myiter, &(plist->list), 0);

  for (; ITERATOR_PTR(myiter); ITERATOR_NEXT(myiter))
  {
    Object *o;

    punit = (struct unit *) ITERATOR_PTR(myiter);
    if ((o = MakePresentUnit(punit)))
    {
      DoMethod(o, MUIM_Notify, MUIA_Pressed, FALSE, app, 5, MUIM_CallHook, &civstandard_hook, city_present, pdialog, punit);
      DoMethod(pdialog->present_group, OM_ADDMEMBER, o);
    }
  }

  DoMethod(pdialog->present_group, MUIM_Group_ExitChange);
}

/****************************************************************
...
*****************************************************************/
static void city_dialog_update_title(struct city_dialog *pdialog)
{
  if (pdialog->name_transparentstring)
  {
    set(pdialog->name_transparentstring,MUIA_TransparentString_Contents, pdialog->pcity->name);
    settextf(pdialog->title_text, _("%s citizens"),
	     population_to_text(city_population(pdialog->pcity)));
  } else
  {
    settextf(pdialog->title_text, _("%s - %s citizens"),
	     pdialog->pcity->name,
	     population_to_text(city_population(pdialog->pcity)));
  }
}

/****************************************************************
...
*****************************************************************/
static void city_dialog_update_improvement_list(struct city_dialog *pdialog)
{
  LONG i, j = 0, refresh = FALSE, imprv;

  for (i = 0; i < game.num_impr_types && !refresh; ++i)
  {
    if(pdialog->pcity->improvements[i])
    {
      DoMethod(pdialog->imprv_listview, MUIM_NList_GetEntry, j++, &imprv);
      if(!imprv || imprv - 1 != i)
	refresh = TRUE;
    }
  }
  /* check the case for to much improvements in list */
  DoMethod(pdialog->imprv_listview, MUIM_NList_GetEntry, j, &imprv);

  if(refresh || imprv)
  {
    set(pdialog->imprv_listview, MUIA_NList_Quiet, TRUE);
    DoMethod(pdialog->imprv_listview, MUIM_NList_Clear);

    for (i = 0; i < game.num_impr_types; ++i)
    {
      if (pdialog->pcity->improvements[i])
      {
	DoMethod(pdialog->imprv_listview, MUIM_NList_InsertSingle, i + 1, MUIV_NList_Insert_Bottom);
      }
    }

    set(pdialog->imprv_listview, MUIA_NList_Quiet, FALSE);
  }
}

/****************************************************************
...
*****************************************************************/
static void close_city_dialog(struct city_dialog *pdialog)
{
  if (pdialog)
  {
    genlist_unlink(&dialog_list, pdialog);
    unit_list_iterate(pdialog->pcity->info_units_supported, psunit) {
      free(psunit);
    } unit_list_iterate_end;
    unit_list_unlink_all(&(pdialog->pcity->info_units_supported));
    unit_list_iterate(pdialog->pcity->info_units_present, psunit) {
      free(psunit);
    } unit_list_iterate_end;
    unit_list_unlink_all(&(pdialog->pcity->info_units_present));
    if (pdialog->worklist_wnd)
    {
      set(pdialog->worklist_wnd, MUIA_Window_Open, FALSE);
      DoMethod(app, OM_REMMEMBER, pdialog->worklist_wnd);
      MUI_DisposeObject(pdialog->worklist_wnd);
    }
    set(pdialog->wnd, MUIA_Window_Open, FALSE);
    set(pdialog->cityopt_wnd, MUIA_Window_Open, FALSE);
    if (pdialog->sell_wnd)
      destroy_message_dialog(pdialog->sell_wnd);

    DoMethod(app, OM_REMMEMBER, pdialog->wnd);
    DoMethod(app, OM_REMMEMBER, pdialog->cityopt_wnd);
    MUI_DisposeObject(pdialog->wnd);
    MUI_DisposeObject(pdialog->cityopt_wnd);
    FreeVec(pdialog);
  }
}

/**************************************************************************
 Open the City Options dialog for this city
**************************************************************************/
static void open_cityopt_dialog(struct city_dialog *pdialog)
{
  struct city *pcity = pdialog->pcity;
  int i, state, newcitizen_index;

  for (i = 0; i < NUM_CITYOPT_TOGGLES; i++)
  {
    state = (pcity->city_options & (1 << i));
    set(pdialog->cityopt_checks[i], MUIA_Selected, state);
  }
  if (pcity->city_options & (1 << CITYO_NEW_EINSTEIN))
  {
    newcitizen_index = 1;
  }
  else if (pcity->city_options & (1 << CITYO_NEW_TAXMAN))
  {
    newcitizen_index = 2;
  }
  else
  {
    newcitizen_index = 0;
  }

  set(pdialog->cityopt_cycle, MUIA_Cycle_Active, newcitizen_index);
  set(pdialog->cityopt_wnd, MUIA_Window_Open, TRUE);
}
