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
#include <string.h>
#include <ctype.h>
#include <stdarg.h>

#include <mui/NListview_MCC.h>
#include <libraries/mui.h>

#include <clib/alib_protos.h>
#include <proto/exec.h>
#include <proto/utility.h>
#include <proto/muimaster.h>

#include "city.h"
#include "game.h"
#include "packets.h"
#include "shared.h"
#include "unit.h"

#include "chatline.h"
#include "citydlg.h"
#include "mapview.h"
#include "optiondlg.h"
#include "options.h"
#include "repodlgs.h"
#include "climisc.h"

#include "cityrep.h"

/* Amiga Client stuff */
#include "muistuff.h"

IMPORT Object *app;
IMPORT Object *main_wnd;

/* TODO: Change button must be implemented and several other things */

extern struct connection aconnection;
extern int delay_report_update;

/* abbreviate long city names to this length in the city report: */
#define REPORT_CITYNAME_ABBREV 15

/************************************************************************
 cr_entry = return an entry (one column for one city) for the city report
 These return ptrs to filled in static strings.
 Note the returned string may not be exactly the right length; that
 is handled later.
*************************************************************************/

static char *cr_entry_cityname(struct city *pcity)
{
  static char buf[REPORT_CITYNAME_ABBREV + 1];
  if (strlen(pcity->name) <= REPORT_CITYNAME_ABBREV)
  {
    return pcity->name;
  }
  else
  {
    strncpy(buf, pcity->name, REPORT_CITYNAME_ABBREV - 1);
    buf[REPORT_CITYNAME_ABBREV - 1] = '.';
    buf[REPORT_CITYNAME_ABBREV] = '\0';
    return buf;
  }
}

static char *cr_entry_size(struct city *pcity)
{
  static char buf[8];
  sprintf(buf, "%2d", pcity->size);
  return buf;
}

static char *cr_entry_hstate_concise(struct city *pcity)
{
  static char buf[4];
  sprintf(buf, "%s", (city_celebrating(pcity) ? "*" :
		      (city_unhappy(pcity) ? "X" : " ")));
  return buf;
}

static char *cr_entry_hstate_verbose(struct city *pcity)
{
  static char buf[16];
  sprintf(buf, "%s", (city_celebrating(pcity) ? "Rapture" :
		      (city_unhappy(pcity) ? "Disorder" : "Peace")));
  return buf;
}

static char *cr_entry_workers(struct city *pcity)
{
  static char buf[32];
  sprintf(buf, "%d/%d/%d",
	  pcity->ppl_happy[4],
	  pcity->ppl_content[4],
	  pcity->ppl_unhappy[4]);
  return buf;
}

static char *cr_entry_specialists(struct city *pcity)
{
  static char buf[32];
  sprintf(buf, "%d/%d/%d",
	  pcity->ppl_elvis,
	  pcity->ppl_scientist,
	  pcity->ppl_taxman);
  return buf;
}

static char *cr_entry_resources(struct city *pcity)
{
  static char buf[32];
  sprintf(buf, "%d/%d/%d",
	  pcity->food_surplus,
	  pcity->shield_surplus,
	  pcity->trade_prod);
  return buf;
}

static char *cr_entry_output(struct city *pcity)
{
  static char buf[32];
  int goldie;

  goldie = city_gold_surplus(pcity);
  sprintf(buf, "%s%d/%d/%d",
	  (goldie < 0) ? "-" : (goldie > 0) ? "+" : "",
	  (goldie < 0) ? (-goldie) : goldie,
	  pcity->luxury_total,
	  pcity->science_total);
  return buf;
}

static char *cr_entry_food(struct city *pcity)
{
  static char buf[32];
  sprintf(buf, "%d/%d",
	  pcity->food_stock,
	  (pcity->size + 1) * game.foodbox);
  return buf;
}

static char *cr_entry_pollution(struct city *pcity)
{
  static char buf[8];
  sprintf(buf, "%3d", pcity->pollution);
  return buf;
}

static char *cr_entry_num_trade(struct city *pcity)
{
  static char buf[8];
  sprintf(buf, "%d", city_num_trade_routes(pcity));
  return buf;
}

static char *cr_entry_building(struct city *pcity)
{
  static char buf[64];
  if (pcity->is_building_unit)
    sprintf(buf, "%s(%d/%d/%d)",
	    get_unit_type(pcity->currently_building)->name,
	    pcity->shield_stock,
	    get_unit_type(pcity->currently_building)->build_cost,
	    city_buy_cost(pcity));
  else
    sprintf(buf, "%s(%d/%d/%d)",
	    get_imp_name_ex(pcity, pcity->currently_building),
	    pcity->shield_stock,
	    get_improvement_type(pcity->currently_building)->build_cost,
	    city_buy_cost(pcity));
  return buf;
}

static char *cr_entry_corruption(struct city *pcity)
{
  static char buf[8];
  sprintf(buf, "%3d", pcity->corruption);
  return buf;
}

/* City report options (which columns get shown)
 * To add a new entry, you should just have to:
 * - add a function like those above
 * - add an entry in the city_report_specs[] table
 */

struct city_report_spec
{
  int show;			/* modify this to customize */
  int width;			/* 0 means variable; rightmost only */
  int space;			/* number of leading spaces (see below) */
  char *title1;
  char *title2;
  char *explanation;
  char *(*func) (struct city *);
  char *tagname;		/* for save_options */
};

/* This generates the function name and the tagname: */
#define FUNC_TAG(var)  cr_entry_##var, #var

/* Use tagname rather than index for load/save, because later
   additions won't necessarily be at the end.
 */

/* Note on space: you can do spacing and alignment in various ways;
   you can avoid explicit space between columns if they are bracketted,
   but the problem is that with a configurable report you don't know
   what's going to be next to what.
 */

static struct city_report_spec city_report_specs[] =
{
  {1, -15, 0, "", "Name", "City Name",
   FUNC_TAG(cityname)},
  {1 /*0 */ , -2, 1, "", "Sz", "Size",
   FUNC_TAG(size)},
  {1, -8, 1, "", "State", "Rapture/Peace/Disorder",
   FUNC_TAG(hstate_verbose)},
  {1 /*0 */ , -1, 1, "", "", "Concise *=Rapture, X=Disorder",
   FUNC_TAG(hstate_concise)},
  {1, -8, 1, "Workers", "H/C/U", "Workers: Happy, Content, Unhappy",
   FUNC_TAG(workers)},
  {1 /*0 */ , -7, 1, "Special", "E/S/T", "Entertainers, Scientists, Taxmen",
   FUNC_TAG(specialists)},
  {1, -10, 1, "Surplus", "F/P/T", "Surplus: Food, Production, Trade",
   FUNC_TAG(resources)},
  {1, -10, 1, "Economy", "G/L/S", "Economy: Gold, Luxuries, Science",
   FUNC_TAG(output)},
  {1 /*0 */ , -1, 1, "n", "T", "Number of Trade Routes",
   FUNC_TAG(num_trade)},
  {1, -7, 1, "Food", "Stock", "Food Stock",
   FUNC_TAG(food)},
  {1 /*0 */ , -3, 1, "", "Pol", "Pollution",
   FUNC_TAG(pollution)},
  {1 /*0 */ , -3, 1, "", "Cor", "Corruption",
   FUNC_TAG(corruption)},
  {1, 0, 1, "Currently Building", "(Stock,Target,Buy Cost)",
   "Currently Building",
   FUNC_TAG(building)}
};

#define NUM_CREPORT_COLS \
	 sizeof(city_report_specs)/sizeof(city_report_specs[0])

/******************************************************************
Some simple wrappers:
******************************************************************/
int num_city_report_spec(void)
{
  return NUM_CREPORT_COLS;
}
int *city_report_spec_show_ptr(int i)
{
  return &(city_report_specs[i].show);
}
char *city_report_spec_tagname(int i)
{
  return city_report_specs[i].tagname;
}

/******************************************************************/
void create_city_report_dialog(int make_modal);

/****************************************************************
 Create the text for a line in the city report
*****************************************************************/
static void get_city_text(struct city *pcity, char *buf[])
{
  struct city_report_spec *spec;
  int i;

  for (i = 0, spec = city_report_specs; i < NUM_CREPORT_COLS; i++, spec++)
  {
    char *text;

    buf[i][0] = '\0';
    if (!spec->show)
      continue;

    text = (spec->func) (pcity);
    if (text)
    {
      strncpy(buf[i], text, 63);
      buf[i][63] = 0;
    }
  }
}

/****************************************************************
 Return text line for the column headers for the city report
*****************************************************************/
static void get_city_table_header(char *text[])
{
  struct city_report_spec *spec;
  int i;

  for (i = 0, spec = city_report_specs; i < NUM_CREPORT_COLS; i++, spec++)
  {
    if (spec->title1)
    {
      strcpy(text[i], spec->title1);
      strcat(text[i], " ");
    }
    else
      text[i][0] = 0;

    if (spec->title2)
    {
      strcat(text[i], spec->title2);
    }
  }
}


STATIC Object *cityrep_wnd;
STATIC Object *cityrep_title_text;
STATIC Object *cityrep_listview;
STATIC struct Hook cityreq_disphook;
STATIC Object *cityrep_close_button;
STATIC Object *cityrep_center_button;
STATIC Object *cityrep_popup_button;
STATIC Object *cityrep_buy_button;
STATIC Object *cityrep_change_button;
STATIC Object *cityrep_refresh_button;
STATIC Object *cityrep_configure_button;

/****************************************************************
...
****************************************************************/
void popup_city_report_dialog(int make_modal)
{
  if (!cityrep_wnd)
    create_city_report_dialog(make_modal);
  if (cityrep_wnd)
  {
    city_report_dialog_update();
    set(cityrep_wnd, MUIA_Window_Open, TRUE);
  }
}

/**************************************************************************
 Display function for the listview in the city report window
**************************************************************************/
__asm __saveds static int cityrep_display(register __a0 struct Hook *h, register __a2 char **array, register __a1 struct city *pcity)
{
  static char buf[NUM_CREPORT_COLS][64];
  int i;

  if (pcity)
  {
    for (i = 0; i < NUM_CREPORT_COLS; i++)
      array[i] = buf[i];

    get_city_text(pcity, array);
  }
  else
  {
    for (i = 0; i < NUM_CREPORT_COLS; i++)
      array[i] = buf[i];

    get_city_table_header(array);
  }
  return 0;
}

/**************************************************************************
 Callback if a new entry inside the listview is selected
**************************************************************************/
static int cityrep_active(void)
{
  struct city *pcity;

  DoMethod(cityrep_listview, MUIM_NList_GetEntry, MUIV_NList_GetEntry_Active, &pcity);

  if (pcity)
  {
    int flag;
    size_t i;

    set(cityrep_center_button, MUIA_Disabled, FALSE);
    set(cityrep_popup_button, MUIA_Disabled, FALSE);
    set(cityrep_buy_button, MUIA_Disabled, FALSE);

    flag = 0;

    for (i = 0; i < B_LAST; i++)
    {
      if (can_build_improvement(pcity, i))
      {
	flag = 1;
      }
    }

    for (i = 0; i < game.num_unit_types; i++)
    {
      if (can_build_unit(pcity, i))
      {
	flag = 1;
      }
    }


    if (!flag)
      set(cityrep_change_button, MUIA_Disabled, TRUE);
    else
      set(cityrep_change_button, MUIA_Disabled, FALSE);
  }
  else
  {
    set(cityrep_change_button, MUIA_Disabled, TRUE);
    set(cityrep_center_button, MUIA_Disabled, TRUE);
    set(cityrep_popup_button, MUIA_Disabled, TRUE);
    set(cityrep_buy_button, MUIA_Disabled, TRUE);
  }
  return NULL;
}

/**************************************************************************
 Callback for the Center Button
**************************************************************************/
static int cityrep_center(void)
{
  struct city *pcity;

  DoMethod(cityrep_listview, MUIM_NList_GetEntry, MUIV_NList_GetEntry_Active, &pcity);

  if (pcity)
  {
    center_tile_mapcanvas(pcity->x, pcity->y);
  }
  return NULL;
}

/**************************************************************************
 Callback for the Popup Button
**************************************************************************/
static int cityrep_popup(void)
{
  struct city *pcity;

  DoMethod(cityrep_listview, MUIM_NList_GetEntry, MUIV_NList_GetEntry_Active, &pcity);

  if (pcity)
  {
    if (center_when_popup_city) {
      center_tile_mapcanvas(pcity->x, pcity->y);
    }
    popup_city_dialog(pcity, 0);
  }
  return NULL;
}


/****************************************************************
 Create and initialize the city report window
*****************************************************************/
void create_city_report_dialog(int make_modal)
{
  if (cityrep_wnd)
    return;

  cityreq_disphook.h_Entry = (HOOKFUNC) cityrep_display;

  cityrep_wnd = WindowObject,
    MUIA_Window_Title, "City Report",
    MUIA_Window_ID, 'CTYR',
    WindowContents, VGroup,
	Child, cityrep_title_text = TextObject,
	    MUIA_Text_PreParse, "\33c",
	    End,
	Child, cityrep_listview = NListviewObject,
	    MUIA_NListview_NList, NListObject,
		MUIA_NList_Format, ",,,,,,,,,,,,",
		MUIA_NList_DisplayHook, &cityreq_disphook,
		MUIA_NList_Title, TRUE,
		End,
	    End,
	Child, HGroup,
	    Child, cityrep_close_button = MakeButton("_Close"),
	    Child, cityrep_center_button = MakeButton("Cen_ter"),
	    Child, cityrep_popup_button = MakeButton("_Popup"),
	    Child, cityrep_buy_button = MakeButton("_Buy"),
	    Child, cityrep_change_button = MakeButton("C_hange"),
	    Child, cityrep_refresh_button = MakeButton("_Refresh"),
	    Child, cityrep_configure_button = MakeButton("Con_figure"),
	    End,
	End,
    End;

  if (cityrep_wnd)
  {
    char *report_title = get_report_title("City Report");
    if (report_title)
    {
      set(cityrep_title_text, MUIA_Text_Contents, report_title);
      free(report_title);
    }

    set(cityrep_change_button, MUIA_Disabled, TRUE);
    set(cityrep_center_button, MUIA_Disabled, TRUE);
    set(cityrep_popup_button, MUIA_Disabled, TRUE);
    set(cityrep_buy_button, MUIA_Disabled, TRUE);

    DoMethod(cityrep_wnd, MUIM_Notify, MUIA_Window_CloseRequest, TRUE, cityrep_wnd, 3, MUIM_Set, MUIA_Window_Open, FALSE);
    DoMethod(cityrep_close_button, MUIM_Notify, MUIA_Pressed, FALSE, cityrep_wnd, 3, MUIM_Set, MUIA_Window_Open, FALSE);
    DoMethod(cityrep_center_button, MUIM_Notify, MUIA_Pressed, FALSE, app, 3, MUIM_CallHook, &standart_hook, cityrep_center);
    DoMethod(cityrep_popup_button, MUIM_Notify, MUIA_Pressed, FALSE, app, 3, MUIM_CallHook, &standart_hook, cityrep_popup);
    DoMethod(cityrep_listview, MUIM_Notify, MUIA_NList_Active, MUIV_EveryTime, app, 3, MUIM_CallHook, &standart_hook, cityrep_active);
    DoMethod(cityrep_listview, MUIM_Notify, MUIA_NList_DoubleClick, TRUE, app, 3, MUIM_CallHook, &standart_hook, cityrep_center);

    DoMethod(app, OM_ADDMEMBER, cityrep_wnd);
  }
}

/****************************************************************
...
*****************************************************************/
void city_report_dialog_update(void)
{
  char *report_title;

  if (!cityrep_wnd)
    return;
  if (delay_report_update)
    return;

  report_title = get_report_title("City Advisor");
  set(cityrep_title_text, MUIA_Text_Contents, report_title);
  free(report_title);

  set(cityrep_listview, MUIA_NList_Quiet, TRUE);
  DoMethod(cityrep_listview, MUIM_NList_Clear);

  city_list_iterate(game.player_ptr->cities, pcity)
  {
    DoMethod(cityrep_listview, MUIM_NList_InsertSingle, pcity, MUIV_NList_Insert_Bottom);
  }
  city_list_iterate_end

    set(cityrep_listview, MUIA_NList_Quiet, FALSE);

  /* Update the buttons */
  cityrep_active();
}

/****************************************************************
  Update the text for a single city in the city report
*****************************************************************/
void city_report_dialog_update_city(struct city *pcity)
{
  int i, entries;
  struct city *pfindcity;

  if (!cityrep_wnd)
    return;
  if (delay_report_update)
    return;

  entries = xget(cityrep_listview, MUIA_NList_Entries);

  for (i = 0; i < entries; i++)
  {
    DoMethod(cityrep_listview, MUIM_NList_GetEntry, i, &pfindcity);
    if (pfindcity == pcity)
      break;
  }

  if (pfindcity)
  {
    DoMethod(cityrep_listview, MUIM_NList_ReplaceSingle, pcity, i, NOWRAP, 0);
  }
  else
    city_report_dialog_update();
}
