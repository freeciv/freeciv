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
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <assert.h>

#include <libraries/mui.h>
#include <mui/NListview_MCC.h>
#include <clib/alib_protos.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>

#include "mem.h"
#include "fcintl.h"
#include "game.h"
#include "government.h"
#include "packets.h"
#include "shared.h"

#include "cityrep.h"
#include "clinet.h"
#include "dialogs.h"
#include "helpdlg.h"

#include "repodlgs.h"

/* Amiga Client stuff */

#include "muistuff.h"
#include "autogroupclass.h"

IMPORT Object *app;

STATIC Object *science_wnd;
STATIC Object *science_title_text;
STATIC Object *science_cycle_group;
STATIC Object *science_research_popup;
STATIC LONG science_research_active;
STATIC Object *science_goal_popup;
STATIC LONG science_goal_active;
STATIC Object *science_steps_text;
STATIC Object *science_help_button;
STATIC Object *science_researched_group;

STATIC STRPTR *help_goal_entries;
STATIC STRPTR *help_research_entries;

int delay_report_update = 0;

static void create_trade_report_dialog(void/*int make_modal*/);
void create_activeunits_report_dialog(int make_modal);

/******************************************************************
 GUI independend
*******************************************************************/
void request_player_tech_goal(int to)
{
  struct packet_player_request packet;
  packet.tech = to;
  send_packet_player_request(&aconnection, &packet, PACKET_PLAYER_TECH_GOAL);
}

/******************************************************************
 GUI independend
*******************************************************************/
void request_player_research(int to)
{
  struct packet_player_request packet;
  packet.tech = to;
  send_packet_player_request(&aconnection, &packet, PACKET_PLAYER_RESEARCH);
}


/******************************************************************
 Turn off updating of reports
*******************************************************************/
void report_update_delay_on(void)
{
  delay_report_update = 1;
}

/******************************************************************
 Turn on updating of reports
*******************************************************************/

void report_update_delay_off(void)
{
  delay_report_update = 0;
}

/******************************************************************
...
*******************************************************************/
void update_report_dialogs(void)
{
  if (delay_report_update)
    return;
  activeunits_report_dialog_update();
  trade_report_dialog_update();
  city_report_dialog_update();
  science_dialog_update();
}

/****************************************************************
...
****************************************************************/
char *get_report_title(char *report_name)
{
  char buf[512];

  sprintf(buf, _("%s\n%s of the %s\n%s %s: %s"),
	  report_name,
	  get_government_name(game.player_ptr->government),
	  get_nation_name_plural(game.player_ptr->nation),
	  get_ruler_title(game.player_ptr->government, game.player_ptr->is_male, game.player_ptr->nation),
	  game.player_ptr->name,
	  textyear(game.year));

  return mystrdup(buf);
}

/****************************************************************
 Callback for the Goal popup (cycle)
****************************************************************/
static void science_goal(ULONG * newgoal)
{
  int i;
  int to = -1;

  if (game.player_ptr->ai.tech_goal == A_NONE)
    if (help_goal_entries[*newgoal] == (STRPTR) advances[A_NONE].name)
      to = 0;
  for (i = A_FIRST; i < game.num_tech_types; i++)
  {
    if (help_goal_entries[*newgoal] == (STRPTR) advances[i].name)
      to = i;
  }

  if (to != -1)
  {
    if (xget(science_help_button, MUIA_Selected))
    {
      nnset(science_goal_popup, MUIA_Cycle_Active, science_goal_active);
      popup_help_dialog_typed(advances[to].name, HELP_TECH);
    }
    else
    {
      request_player_tech_goal(to);
      DoMethod(science_steps_text, MUIM_SetAsString, MUIA_Text_Contents, _("(%ld steps)"), tech_goal_turns(game.player_ptr, to));
    }
  }
}

/****************************************************************
 Callback for the Research popup (cycle)
****************************************************************/
static void science_research(ULONG * newresearch)
{
  int i;
  int to = -1;

  for (i = A_FIRST; i < game.num_tech_types; i++)
  {
    if (help_research_entries[*newresearch] == (STRPTR) advances[i].name)
      to = i;
  }

  if (to != -1)
  {
    if (xget(science_help_button, MUIA_Selected))
    {
      nnset(science_goal_popup, MUIA_Cycle_Active, science_goal_active);
      popup_help_dialog_typed(advances[to].name, HELP_TECH);
    }
    else
    {
      request_player_research(to);
/*      DoMethod(science_steps_text, MUIM_SetAsString, MUIA_Text_Contents, _("(%ld steps)"), tech_goal_turns(game.player_ptr, to)); */
    }
  }
}

/****************************************************************
 Callback for a researched technology
****************************************************************/
static void science_researched(ULONG * tech)
{
  popup_help_dialog_typed(advances[*tech].name, HELP_TECH);
}

/****************************************************************
...
****************************************************************/
void popup_science_dialog(int make_modal)
{
  STATIC STRPTR def_entries[2];
  STRPTR *goal_entries = def_entries;
  STRPTR *research_entries = def_entries;
  int i, j;

  def_entries[0] = _("None");
  def_entries[1] = 0;

  if (help_research_entries)
  {
    free(help_research_entries);
    help_research_entries = NULL;
  }
  if (help_goal_entries)
  {
    free(help_goal_entries);
    help_goal_entries = NULL;
  }

  if (game.player_ptr->research.researching != A_NONE)
  {
    for (i = A_FIRST, j = 0; i < game.num_tech_types; i++)
    {
      if (get_invention(game.player_ptr, i) != TECH_REACHABLE)
	continue;
      j++;
    }

    if (j)
    {
      if ((help_research_entries = (STRPTR *) malloc((j + 1) * sizeof(STRPTR))))
      {
	for (i = A_FIRST, j = 0; i < game.num_tech_types; i++)
	{
	  if (get_invention(game.player_ptr, i) != TECH_REACHABLE)
	    continue;
	  if (i == game.player_ptr->research.researching)
	    science_research_active = j;

	  help_research_entries[j++] = advances[i].name;
	}
	help_research_entries[j] = NULL;
	research_entries = help_research_entries;
      }
    }
  }


  for (i = A_FIRST, j = 0; i < game.num_tech_types; i++)
  {
    if (get_invention(game.player_ptr, i) != TECH_KNOWN &&
	advances[i].req[0] != A_LAST && advances[i].req[1] != A_LAST &&
	tech_goal_turns(game.player_ptr, i) < 11)
      j++;
  }
  if (game.player_ptr->ai.tech_goal == A_NONE)
    j++;

  if (j)
  {
    if ((help_goal_entries = (STRPTR *) malloc((j + 2) * sizeof(STRPTR))))
    {
      j = 0;
      if (game.player_ptr->ai.tech_goal == A_NONE)
	help_goal_entries[j++] = advances[A_NONE].name;


      for (i = A_FIRST; i < game.num_tech_types; i++)
      {
	if (get_invention(game.player_ptr, i) != TECH_KNOWN &&
	    advances[i].req[0] != A_LAST && advances[i].req[1] != A_LAST &&
	    tech_goal_turns(game.player_ptr, i) < 11)
	{
	  if (i == game.player_ptr->ai.tech_goal)
	    science_goal_active = j;
	  help_goal_entries[j++] = advances[i].name;
	}
      }

      help_goal_entries[j] = NULL;
      goal_entries = help_goal_entries;
    }
  }

/*  else
   {
   sprintf(text, _("Researching Future Tech. %d"),
   ((game.player_ptr->future_tech)+1));

   item = gtk_menu_item_new_with_label(text);
   gtk_menu_append(GTK_MENU(popupmenu), item);
   } */


  if (!science_wnd)
  {
    science_wnd = WindowObject,
      MUIA_Window_Title, _("Science"),
      MUIA_Window_ID, MAKE_ID('S','C','N','C'),

      WindowContents, VGroup,
	  Child, science_title_text = TextObject,
	      MUIA_Text_PreParse, "\33c",
	      End,
          Child, HGroup,
	      Child, science_cycle_group = VGroup,
	          End,
              Child, science_help_button = TextObject,
                  ButtonFrame,
                  MUIA_Text_PreParse, "\33c",
                  MUIA_Text_Contents, _("Help"),
                  MUIA_Font, MUIV_Font_Button,
                  MUIA_InputMode, MUIV_InputMode_Toggle,
                  MUIA_Background, MUII_ButtonBack,
                  End,
              End,
          Child, ScrollgroupObject,
              MUIA_Scrollgroup_FreeVert, FALSE,
              MUIA_Scrollgroup_Contents, science_researched_group = AutoGroup,
                  VirtualFrame,
		  MUIA_AutoGroup_DefVertObjects, 8,
                  End,
              End,              
          End,
      End;

    if (science_wnd)
    {
      DoMethod(science_wnd, MUIM_Notify, MUIA_Window_CloseRequest, TRUE, science_wnd, 3, MUIM_Set, MUIA_Window_Open, FALSE);
      DoMethod(app, OM_ADDMEMBER, science_wnd);
    }
  }

  if (science_wnd)
  {
    char *report_title = get_report_title(_("Science Advisor"));
    settextf(science_title_text, _("%s\n(%d turns/advance)"), report_title,tech_turns_to_advance(game.player_ptr));
    free(report_title);

    DoMethod(science_cycle_group, MUIM_Group_InitChange);
    {
      STATIC Object *o;
      Object *status_text;

      if (o)
      {
	DoMethod(science_cycle_group, OM_REMMEMBER, o);
	MUI_DisposeObject(o);
      }

      o = VGroup,
	Child, HGroup,
	    Child, MakeLabel(_("_Goal")),
	    Child, science_goal_popup = MakeCycle(_("_Goal"), goal_entries),
	    Child, science_steps_text = TextObject, End,
	    End,
	Child, HGroup,
	    Child, MakeLabel(_("_Research")),
	    Child, science_research_popup = MakeCycle(_("_Research"), research_entries),
	    Child, status_text = TextObject, End,
	    End,
	End;

      if (o)
      {
	DoMethod(science_cycle_group, OM_ADDMEMBER, o);

	set(science_research_popup, MUIA_Cycle_Active, science_research_active);
	set(science_goal_popup, MUIA_Cycle_Active, science_goal_active);

	DoMethod(science_research_popup, MUIM_Notify, MUIA_Cycle_Active, MUIV_EveryTime, app, 4, MUIM_CallHook, &civstandard_hook, science_research, MUIV_TriggerValue);
	DoMethod(science_goal_popup, MUIM_Notify, MUIA_Cycle_Active, MUIV_EveryTime, app, 4, MUIM_CallHook, &civstandard_hook, science_goal, MUIV_TriggerValue);

	DoMethod(status_text, MUIM_SetAsString, MUIA_Text_Contents, "%ld/%ld", game.player_ptr->research.researched, research_time(game.player_ptr));
      }
    }
    DoMethod(science_cycle_group, MUIM_Group_ExitChange);

    DoMethod(science_steps_text, MUIM_SetAsString, MUIA_Text_Contents, _("(%ld steps)"), tech_goal_turns(game.player_ptr, game.player_ptr->ai.tech_goal));

    DoMethod(science_researched_group, MUIM_Group_InitChange);
    DoMethod(science_researched_group, MUIM_AutoGroup_DisposeChilds);

    for (i = A_FIRST; i < game.num_tech_types; i++)
    {
      if ((get_invention(game.player_ptr, i) == TECH_KNOWN))
      {
      	Object *tech = TextObject,
      	    MUIA_Text_Contents, advances[i].name,
     	    MUIA_InputMode, MUIV_InputMode_RelVerify,
     	    MUIA_CycleChain, 1,
      	    End;

      	if (tech)
      	{
      	  DoMethod(tech, MUIM_Notify, MUIA_Pressed, FALSE, app, 4, MUIM_CallHook, &civstandard_hook, science_researched, i);
      	  DoMethod(science_researched_group, OM_ADDMEMBER, tech);
      	}
      }
    }

    DoMethod(science_researched_group, MUIM_Group_ExitChange);

    set(science_wnd, MUIA_Window_Open, TRUE);
  }
}

/****************************************************************
...
*****************************************************************/
void science_dialog_update(void)
{
  if (science_wnd)
  {
    if (xget(science_wnd, MUIA_Window_Open))
      popup_science_dialog(0);
  }
}


/****************************************************************

                      TRADE REPORT DIALOG
 
****************************************************************/

struct trade_imprv_entry
{
  int type;
  int count;
  struct city *pcity;
};

STATIC Object *trade_wnd;
STATIC Object *trade_title_text;
STATIC Object *trade_imprv_listview;
STATIC struct Hook trade_imprv_consthook;
STATIC struct Hook trade_imprv_desthook;
STATIC struct Hook trade_imprv_disphook;
STATIC Object *trade_total_text;
STATIC Object *trade_close_button;
STATIC Object *trade_sellobsolete_button;
STATIC Object *trade_sellall_button;

/****************************************************************
 Constructor of a new entry in the trade listview
*****************************************************************/
HOOKPROTONHNO(trade_imprv_construct, struct trade_imprv_entry *, struct trade_imprv_entry *entry)
{
  struct trade_imprv_entry *newentry = (struct trade_imprv_entry *) AllocVec(sizeof(*newentry), 0);
  if (newentry)
  {
    *newentry = *entry;
    return newentry;
  }
}

/****************************************************************
 Destructor of a entry in the trades listview
*****************************************************************/
HOOKPROTONHNO(trade_imprv_destruct, void, struct trade_imprv_entry *entry)
{
  FreeVec(entry);
}

/****************************************************************
 Display function for the trade listview
*****************************************************************/
HOOKPROTONH(trade_imprv_render, void, char **array, struct trade_imprv_entry *entry)
{
  if (entry)
  {
    static char count[16];
    static char coststr[16];
    static char utotal[16];

    struct city *pcity = entry->pcity;
    int j = entry->type;
    int cost = entry->count * improvement_upkeep(pcity, j);

    sprintf(count, "%5d", entry->count);
    sprintf(coststr, "%5d", improvement_upkeep(pcity, j));
    sprintf(utotal, "%6d", cost);

    *array++ = get_improvement_name(j);
    *array++ = count;
    *array++ = coststr;
    *array = utotal;
  }
  else
  {
    *array++ = _("Building Name");
    *array++ = _("Count");
    *array++ = _("Cost");
    *array = _("U Total");
  }
}

/****************************************************************
...
****************************************************************/
void popup_trade_report_dialog(int make_modal)
{
  if (!trade_wnd)
    create_trade_report_dialog(/*make_modal*/);
  if (trade_wnd)
  {
    trade_report_dialog_update();
    set(trade_wnd, MUIA_Window_Open, TRUE);
  }
}

/****************************************************************
...
*****************************************************************/
static void trade_sell(int *data)
{
  struct trade_imprv_entry *entry;
  DoMethod(trade_imprv_listview, MUIM_NList_GetEntry, MUIV_NList_GetEntry_Active, &entry);
  if (entry)
  {
    int i = entry->type;
    int count = 0, gold = 0;
    char str[128];

    city_list_iterate(game.player_ptr->cities, pcity)
      if (!pcity->did_sell && city_got_building(pcity, i) &&
	  (*data || improvement_obsolete(game.player_ptr, i) || wonder_replacement(pcity, i)))
    {
      struct packet_city_request packet;

      count++;
      gold += improvement_value(i);

      packet.city_id = pcity->id;
      packet.build_id = i;
      packet.name[0] = '\0';
      packet.worklist.name[0] = '\0';
      send_packet_city_request(&aconnection, &packet, PACKET_CITY_SELL);
    }
    city_list_iterate_end

    if (count)
    {
      sprintf(str, _("Sold %d %s for %d gold"), count, get_improvement_name(i), gold);
    }
    else
    {
      sprintf(str, _("No %s could be sold"), get_improvement_name(i));
    }
    popup_notify_dialog(_("Sell-Off:"), _("Results"), str);
  }
}

static void create_trade_report_dialog(void/*int make_modal*/)
{
  if (trade_wnd)
    return;

  trade_imprv_consthook.h_Entry = (HOOKFUNC) trade_imprv_construct;
  trade_imprv_desthook.h_Entry = (HOOKFUNC) trade_imprv_destruct;
  trade_imprv_disphook.h_Entry = (HOOKFUNC) trade_imprv_render;

  trade_wnd = WindowObject,
    MUIA_Window_Title, _("Trade Report"),
    MUIA_Window_ID, MAKE_ID('T','R','A','D'),
    WindowContents, VGroup,
	Child, trade_title_text = TextObject, MUIA_Text_PreParse, "\33c", End,
	Child, trade_imprv_listview = NListviewObject,
	    MUIA_NListview_NList, NListObject,
		MUIA_NList_ConstructHook, &trade_imprv_consthook,
		MUIA_NList_DestructHook, &trade_imprv_desthook,
		MUIA_NList_DisplayHook, &trade_imprv_disphook,
		MUIA_NList_Title, TRUE,
		MUIA_NList_Format, ",,,",
		End,
	    End,
	Child, trade_total_text = TextObject, MUIA_Text_PreParse, "\33c", End,
	Child, HGroup,
	    Child, trade_close_button = MakeButton(_("_Close")),
	    Child, trade_sellobsolete_button = MakeButton(_("Sell _Obsolete")),
	    Child, trade_sellall_button = MakeButton(_("Sell _All")),
	    End,
	End,
    End;

  if (trade_wnd)
  {
    char *report_title = report_title = get_report_title(_("Trade Advisor"));
    set(trade_title_text, MUIA_Text_Contents, report_title);
    free(report_title);

    DoMethod(trade_wnd, MUIM_Notify, MUIA_Window_CloseRequest, TRUE, trade_wnd, 3, MUIM_Set, MUIA_Window_Open, FALSE);
    DoMethod(trade_close_button, MUIM_Notify, MUIA_Pressed, FALSE, trade_wnd, 3, MUIM_Set, MUIA_Window_Open, FALSE);
    DoMethod(trade_sellobsolete_button, MUIM_Notify, MUIA_Pressed, FALSE, app, 4, MUIM_CallHook, &civstandard_hook, trade_sell, 0);
    DoMethod(trade_sellall_button, MUIM_Notify, MUIA_Pressed, FALSE, app, 4, MUIM_CallHook, &civstandard_hook, trade_sell, 1);
    DoMethod(app, OM_ADDMEMBER, trade_wnd);
  }
}

/****************************************************************
...
*****************************************************************/
void trade_report_dialog_update(void)
{
  int tax, total;
  char *report_title;
  struct city *pcity;
  struct trade_imprv_entry entry;

  if (delay_report_update)
    return;

  if (!trade_wnd)
    return;

  if ((report_title = get_report_title(_("Trade Advisor"))))
  {
    set(trade_title_text, MUIA_Text_Contents, report_title);
    free(report_title);
  }

  set(trade_imprv_listview, MUIA_NList_Quiet, TRUE);
  DoMethod(trade_imprv_listview, MUIM_NList_Clear);
  total = tax = 0;

  if ((pcity = city_list_get(&game.player_ptr->cities, 0)))
  {
    int j;

    for (j = 0; j < B_LAST; j++)
    {
      if (!is_wonder(j))
      {
	int cost, count = 0;
	city_list_iterate(game.player_ptr->cities, pcity)
	  if (city_got_building(pcity, j))
	  count++;
	city_list_iterate_end;

	if (!count)
	  continue;
	cost = count * improvement_upkeep(pcity, j);

	entry.type = j;
	entry.count = count;
	entry.pcity = pcity;

	DoMethod(trade_imprv_listview, MUIM_NList_InsertSingle, &entry, MUIV_NList_Insert_Bottom);

	total += cost;
      }
    }


    city_list_iterate(game.player_ptr->cities, pcity)
      tax += pcity->tax_total;
    if (!pcity->is_building_unit && pcity->currently_building == B_CAPITAL)
      tax += pcity->shield_surplus;
    city_list_iterate_end;
  }

  set(trade_imprv_listview, MUIA_NList_Quiet, FALSE);

  settextf(trade_total_text, _("Income:%6d    Total Costs: %6d"), tax, total);
}

/****************************************************************

                      ACTIVE UNITS REPORT DIALOG
 
****************************************************************/

STATIC Object *actunit_wnd;
STATIC Object *actunit_title_text;
STATIC Object *actunit_units_listview;
STATIC struct Hook actunit_units_consthook;
STATIC struct Hook actunit_units_desthook;
STATIC struct Hook actunit_units_disphook;
STATIC Object *actunit_total_text;
STATIC Object *actunit_close_button;
STATIC Object *actunit_upgrade_button;

/****************************************************************
...
****************************************************************/
void popup_activeunits_report_dialog(int make_modal)
{
  if (!actunit_wnd)
    create_activeunits_report_dialog(make_modal);
  if (actunit_wnd)
  {
    activeunits_report_dialog_update();
    set(actunit_wnd, MUIA_Window_Open, TRUE);
  }
}


struct actunit_units_entry
{
  int type;
  int active_count;
  int upkeep_shield;
  int upkeep_food;
  int upkeep_gold;
  int building_count;
};

/****************************************************************
 Constructor of a new entry in the units listview
*****************************************************************/
HOOKPROTONHNO(actunit_units_construct, struct actunit_units_entry *, struct actunit_units_entry *entry)
{
  struct actunit_units_entry *newentry = (struct actunit_units_entry *) AllocVec(sizeof(*newentry), 0);
  if (newentry)
  {
    *newentry = *entry;
    return newentry;
  }
}

/****************************************************************
 Destructor of a entry in the units listview
*****************************************************************/
HOOKPROTONHNO(actunit_units_destruct, void, struct actunit_units_entry *entry)
{
  FreeVec(entry);
}

/**************************************************************************
 Display function for the message listview
**************************************************************************/
HOOKPROTONH(actunit_units_display, void, char **array, struct actunit_units_entry *entry)
{
  if (entry)
  {
    static char active_count[16];
    static char upkeep_shield[16];
    static char upkeep_food[16];
    static char upkeep_gold[16];
    static char building_count[16];
    int i = entry->type;

    sprintf(active_count, "%5d", entry->active_count);
    sprintf(upkeep_shield, "%5d", entry->upkeep_shield);
    sprintf(upkeep_food, "%5d", entry->upkeep_food);
    sprintf(upkeep_gold, "%5d", entry->upkeep_gold);
    sprintf(building_count, "%5d", entry->building_count);

    *array++ = unit_name(i);
    *array++ = can_upgrade_unittype(game.player_ptr, i) != -1 ? "*" : "-";
    *array++ = building_count;
    *array++ = active_count;
    *array++ = upkeep_shield;
    *array = upkeep_food;
  }
  else
  {
    *array++ = _("Unit Type");
    *array++ = _("U");
    *array++ = _("In-Prog");
    *array++ = _("Active");
    *array++ = _("Shield");
    *array = _("Food");
  }
}

/****************************************************************
 Callback if a new inside the listview is selected
*****************************************************************/
static void actunit_units(LONG * newval)
{
  struct actunit_units_entry *entry;

  DoMethod(actunit_units_listview, MUIM_NList_GetEntry, *newval, &entry);
  if (entry)
  {
    if (can_upgrade_unittype(game.player_ptr, entry->type) != -1)
      set(actunit_upgrade_button, MUIA_Disabled, FALSE);
    else
      set(actunit_upgrade_button, MUIA_Disabled, TRUE);
  }
}

/****************************************************************
 Callback for the Yes button in the Upgrade confirmation
*****************************************************************/
static void actunit_upgrade_yes(struct popup_message_data * data)
{
  send_packet_unittype_info(&aconnection, (size_t) data->data, PACKET_UNITTYPE_UPGRADE);
  message_close(data);
}

/****************************************************************
 Callback for the Upgrade button
*****************************************************************/
static void actunit_upgrade(void)
{
  struct actunit_units_entry *entry;

  DoMethod(actunit_units_listview, MUIM_NList_GetEntry, MUIV_NList_GetEntry_Active, &entry);
  if (entry)
  {
    int ut1, ut2;

    ut1 = entry->type;
    ut2 = can_upgrade_unittype(game.player_ptr, entry->type);

    if (ut2 != -1)
    {
      char buf[512];

      sprintf(buf,
	      _("Upgrade as many %s to %s as possible for %d gold each?\n"
	      "Treasury contains %d gold."),
	      unit_types[ut1].name, unit_types[ut2].name,
	      unit_upgrade_price(game.player_ptr, ut1, ut2),
	      game.player_ptr->economic.gold);

      popup_message_dialog(actunit_wnd, _("Upgrade Obsolete Units"), buf,
			   _("_Yes"), actunit_upgrade_yes, entry->type,
			   _("_No"), message_close, 0,
			   0);

    }
  }
}

/****************************************************************
...
*****************************************************************/
void create_activeunits_report_dialog(int make_modal)
{
  if (actunit_wnd)
    return;

  actunit_units_consthook.h_Entry = (HOOKFUNC) actunit_units_construct;
  actunit_units_desthook.h_Entry = (HOOKFUNC) actunit_units_destruct;
  actunit_units_disphook.h_Entry = (HOOKFUNC) actunit_units_display;

  actunit_wnd = WindowObject,
    MUIA_Window_Title, _("Military Report"),
    MUIA_Window_ID, MAKE_ID('M','I','L','I'),
	WindowContents, VGroup,
	Child, actunit_title_text = TextObject,
	    MUIA_Text_PreParse, "\33c",
	    End,
	Child, actunit_units_listview = NListviewObject,
	    MUIA_NListview_NList, NListObject,
		MUIA_NList_ConstructHook, &actunit_units_consthook,
		MUIA_NList_DestructHook, &actunit_units_desthook,
		MUIA_NList_DisplayHook, &actunit_units_disphook,
		MUIA_NList_Title, TRUE,
		MUIA_NList_Format, ",,,,,",
		End,
	    End,
	Child, actunit_total_text = TextObject, End,
	Child, HGroup,
	    Child, actunit_close_button = MakeButton(_("_Close")),
	    Child, actunit_upgrade_button = MakeButton(_("_Upgrade")),
	    End,
	End,
    End;

  if (actunit_wnd)
  {
    char *report_title;
    report_title = get_report_title(_("Military Report"));
    set(actunit_title_text, MUIA_Text_Contents, report_title);
    free(report_title);
    set(actunit_upgrade_button, MUIA_Disabled, TRUE);

    DoMethod(actunit_wnd, MUIM_Notify, MUIA_Window_CloseRequest, TRUE, actunit_wnd, 3, MUIM_Set, MUIA_Window_Open, FALSE);
    DoMethod(actunit_close_button, MUIM_Notify, MUIA_Pressed, FALSE, actunit_wnd, 3, MUIM_Set, MUIA_Window_Open, FALSE);
    DoMethod(actunit_upgrade_button, MUIM_Notify, MUIA_Pressed, FALSE, app, 3, MUIM_CallHook, &civstandard_hook, actunit_upgrade);
    DoMethod(actunit_units_listview, MUIM_Notify, MUIA_NList_Active, MUIV_EveryTime, app, 4, MUIM_CallHook, &civstandard_hook, actunit_units, MUIV_TriggerValue);
    DoMethod(app, OM_ADDMEMBER, actunit_wnd);
  }
}

/****************************************************************
...
*****************************************************************/
void activeunits_report_dialog_update(void)
{
  struct repoinfo
  {
    int active_count;
    int upkeep_shield;
    int upkeep_food;
    /* int upkeep_gold;   FIXME: add gold when gold is implemented --jjm */
    int building_count;
  };

  char *report_title;
  int i;
  struct actunit_units_entry entry;
  struct repoinfo unitarray[U_LAST];

  if (!actunit_wnd)
    return;
  if (delay_report_update)
    return;

  report_title = get_report_title(_("Military Report"));
  set(actunit_title_text, MUIA_Text_Contents, report_title);
  free(report_title);

  memset(unitarray, '\0', sizeof(unitarray));
  unit_list_iterate(game.player_ptr->units, punit)
  {
    (unitarray[punit->type].active_count)++;
    if (punit->homecity)
    {
      unitarray[punit->type].upkeep_shield += punit->upkeep;
      unitarray[punit->type].upkeep_food += punit->upkeep_food;
    }
  }
  unit_list_iterate_end;

  city_list_iterate(game.player_ptr->cities, pcity)
  {
    if (pcity->is_building_unit &&
	(unit_type_exists(pcity->currently_building)))
      (unitarray[pcity->currently_building].building_count)++;
  }
  city_list_iterate_end;


  set(actunit_units_listview, MUIA_NList_Quiet, TRUE);
  DoMethod(actunit_units_listview, MUIM_NList_Clear);

  for (i = 0; i < game.num_unit_types; i++)
  {
    if (unitarray[i].active_count)
    {
      entry.type = i;
      entry.active_count = unitarray[i].active_count;
      entry.upkeep_shield = unitarray[i].upkeep_shield;
      entry.upkeep_food = unitarray[i].upkeep_food;
      entry.upkeep_gold = 0;
      entry.building_count = unitarray[i].building_count;
      DoMethod(actunit_units_listview, MUIM_NList_InsertSingle, &entry, MUIV_NList_Insert_Bottom);
    }
  }

  set(actunit_units_listview, MUIA_NList_Quiet, FALSE);
}
