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

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
#include "support.h"

#include "cityrep.h"
#include "clinet.h"
#include "dialogs.h"
#include "helpdlg.h"
#include "gui_main.h"
#include "repodlgs.h"
#include "repodlgs_common.h"

/* Amiga Client stuff */

#include "muistuff.h"
#include "autogroupclass.h"

static Object *science_wnd;
static Object *science_title_text;
static Object *science_cycle_group;
static Object *science_research_popup;
static LONG science_research_active;
static Object *science_goal_popup;
static LONG science_goal_active;
static Object *science_steps_text;
static Object *science_help_button;
static Object *science_researched_group;

static STRPTR *help_goal_entries;
static STRPTR *help_research_entries;

static void create_trade_report_dialog(void);
void create_activeunits_report_dialog(bool make_modal);

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
...
*******************************************************************/
void update_report_dialogs(void)
{
  if (is_report_dialogs_frozen())
    return;
  activeunits_report_dialog_update();
  economy_report_dialog_update();
  city_report_dialog_update();
  science_dialog_update();
}

/****************************************************************
 Callback for the Goal popup (cycle)
****************************************************************/
static void science_goal(ULONG * newgoal)
{
  int i;
  int to = -1;

  if (game.player_ptr->ai.tech_goal == A_UNSET)
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
      DoMethod(science_steps_text, MUIM_SetAsString, MUIA_Text_Contents,
	       _("(%ld steps)"),
	       num_unknown_techs_for_goal(game.player_ptr, to));
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
void popup_science_dialog(bool make_modal)
{
  static STRPTR def_entries[2];
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

  if (!is_future_tech(game.player_ptr->research.researching)) {
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
    if (tech_is_available(game.player_ptr, i)
	&& get_invention(game.player_ptr, i) != TECH_KNOWN &&
	&& advances[i].req[0] != A_LAST && advances[i].req[1] != A_LAST
	&& num_unknown_techs_for_goal(game.player_ptr, i) < 11)
      j++;
  }
  if (game.player_ptr->ai.tech_goal == A_UNSET) {
    j++;
  }

  if (j)
  {
    if ((help_goal_entries = (STRPTR *) malloc((j + 2) * sizeof(STRPTR))))
    {
      j = 0;
      if (game.player_ptr->ai.tech_goal == A_UNSET) {
	help_goal_entries[j++] = advances[A_NONE].name;
      }

      for (i = A_FIRST; i < game.num_tech_types; i++)
      {
	if (get_invention(game.player_ptr, i) != TECH_KNOWN &&
	    advances[i].req[0] != A_LAST && advances[i].req[1] != A_LAST &&
	    num_unknown_techs_for_goal(game.player_ptr, i) < 11)
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
    const char *report_title = get_report_title(_("Science Advisor"));
    int turns_to_advance = tech_turns_to_advance(game.player_ptr);

    if (turns_to_advance == FC_INFINITY) {
      settextf(science_title_text, _("%s\n(no research)"), report_title);
    } else {
      settextf(science_title_text,
	       PL_("%s\n(%d turn/advance)", "%s\n(%d turns/advance)",
		   turns_to_advance), report_title, turns_to_advance);
    }

    DoMethod(science_cycle_group, MUIM_Group_InitChange);
    {
      static Object *o;
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

	DoMethod(status_text, MUIM_SetAsString, MUIA_Text_Contents,
		 "%ld/%ld", game.player_ptr->research.bulbs_researched,
		 total_bulbs_required(game.player_ptr));
      }
    }
    DoMethod(science_cycle_group, MUIM_Group_ExitChange);

    DoMethod(science_steps_text, MUIM_SetAsString, MUIA_Text_Contents,
	     _("(%ld steps)"), num_unknown_techs_for_goal(game.player_ptr,
							  game.player_ptr->
							  ai.tech_goal));

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

static Object *trade_wnd;
static Object *trade_title_text;
static Object *trade_imprv_listview;
static struct Hook trade_imprv_consthook;
static struct Hook trade_imprv_desthook;
static struct Hook trade_imprv_disphook;
static Object *trade_total_text;
static Object *trade_close_button;
static Object *trade_sellobsolete_button;
static Object *trade_sellall_button;

/****************************************************************
 Constructor of a new entry in the trade listview
*****************************************************************/
HOOKPROTONHNO(trade_imprv_construct, struct improvement_entry *, struct improvement_entry *entry)
{
  struct improvement_entry *newentry = (struct improvement_entry *) AllocVec(sizeof(*newentry), 0);
  if (newentry)
  {
    *newentry = *entry;
    return newentry;
  }
}

/****************************************************************
 Destructor of a entry in the trades listview
*****************************************************************/
HOOKPROTONHNO(trade_imprv_destruct, void, struct improvement_entry *entry)
{
  FreeVec(entry);
}

/****************************************************************
 Display function for the trade listview
*****************************************************************/
HOOKPROTONH(trade_imprv_render, void, char **array, struct improvement_entry *entry)
{
  if (entry)
  {
    static char count[16];
    static char coststr[16];
    static char utotal[16];

    my_snprintf(count, sizeof(count), "%5d", entry->count);
    my_snprintf(coststr, sizeof(coststr), "%5d", entry->cost);
    my_snprintf(utotal, sizeof(utotal), "%6d", entry->total_cost);

    *array++ = get_improvement_name(entry->type);
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
void popup_economy_report_dialog(bool make_modal)
{
  if (!trade_wnd)
    create_trade_report_dialog();
  if (trade_wnd)
  {
    economy_report_dialog_update();
    set(trade_wnd, MUIA_Window_Open, TRUE);
  }
}

/****************************************************************
...
*****************************************************************/
static void trade_sell(int *data)
{
  struct improvement_entry *entry;
  DoMethod(trade_imprv_listview, MUIM_NList_GetEntry, MUIV_NList_GetEntry_Active, &entry);
  if (entry) {
    char str[1024];

    sell_all_improvements(entry->type, data == NULL, str, sizeof(str));
    popup_notify_dialog(_("Sell-Off:"), _("Results"), str);
  }
}

static void create_trade_report_dialog(void)
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

  if (trade_wnd) {
    set(trade_title_text, MUIA_Text_Contents,
	get_report_title(_("Trade Advisor")));

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
void economy_report_dialog_update(void)
{
  int tax, total;
  static struct improvement_entry entries[B_LAST];
  int i, entries_used = 0;

  if (is_report_dialogs_frozen())
    return;

  if (!trade_wnd)
    return;

  set(trade_title_text, MUIA_Text_Contents,
      get_report_title(_("Trade Advisor")));

  set(trade_imprv_listview, MUIA_NList_Quiet, TRUE);
  DoMethod(trade_imprv_listview, MUIM_NList_Clear);

  get_economy_report_data(entries, &entries_used, &total, &tax);

  for (i = 0; i < entries_used; i++) {
    DoMethod(trade_imprv_listview, MUIM_NList_InsertSingle, &entries[i],
	     MUIV_NList_Insert_Bottom);
  }

  set(trade_imprv_listview, MUIA_NList_Quiet, FALSE);

  settextf(trade_total_text, _("Income:%6d    Total Costs: %6d"), tax, total);
}

/****************************************************************

                      ACTIVE UNITS REPORT DIALOG
 
****************************************************************/

static Object *actunit_wnd;
static Object *actunit_title_text;
static Object *actunit_units_listview;
static struct Hook actunit_units_consthook;
static struct Hook actunit_units_desthook;
static struct Hook actunit_units_disphook;
static Object *actunit_total_text;
static Object *actunit_close_button;
static Object *actunit_upgrade_button;

/****************************************************************
...
****************************************************************/
void popup_activeunits_report_dialog(bool make_modal)
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

    my_snprintf(active_count, sizeof(active_count), "%5d", entry->active_count);
    my_snprintf(upkeep_shield, sizeof(upkeep_shield), "%5d", entry->upkeep_shield);
    my_snprintf(upkeep_food, sizeof(upkeep_food), "%5d", entry->upkeep_food);
    my_snprintf(upkeep_gold, sizeof(upkeep_gold), "%5d", entry->upkeep_gold);
    my_snprintf(building_count, sizeof(building_count), "%5d", entry->building_count);

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

      my_snprintf(buf, sizeof(buf),
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
void create_activeunits_report_dialog(bool make_modal)
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
    set(actunit_title_text, MUIA_Text_Contents,
	get_report_title(_("Military Report")))
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

  int i;
  struct actunit_units_entry entry;
  struct repoinfo unitarray[U_LAST];

  if (!actunit_wnd)
    return;
  if (is_report_dialogs_frozen())
    return;

  set(actunit_title_text, MUIA_Text_Contents,
      get_report_title(_("Military Report")));

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

  unit_type_iterate(i) {
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
  } unit_type_iterate_end;

  set(actunit_units_listview, MUIA_NList_Quiet, FALSE);
}

/****************************************************************
  Show a dialog with player statistics at endgame.
  TODO: Display all statistics in packet_endgame_report.
*****************************************************************/
void popup_endgame_report_dialog(struct packet_endgame_report *packet)
{
  char buffer[150 * MAX_NUM_PLAYERS];
  int i;
 
  buffer[0] = '\0';
  for (i = 0; i < packet->nscores; i++) {
    cat_snprintf(buffer, sizeof(buffer),
                 PL_("%2d: The %s ruler %s scored %d point\n",
                     "%2d: The %s ruler %s scored %d points\n",
                     packet->score[i]),
                 i + 1,
                 get_nation_name(get_player(packet->id[i])->nation),
                 get_player(packet->id[i])->name,
                 packet->score[i]);
  }
  popup_notify_dialog(_("Final Report:"),
                      _("The Greatest Civilizations in the world."),
                      buffer);
}

/*************************************************************************
  Server options dialog
*************************************************************************/
void popup_settable_options_dialog(void)
{
  /* PORT ME */
}
