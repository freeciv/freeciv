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

#include <mui/NListview_MCC.h>
#include <libraries/mui.h>

#include <clib/alib_protos.h>
#include <proto/exec.h>
#include <proto/utility.h>
#include <proto/muimaster.h>

#include "city.h"
#include "fcintl.h"
#include "game.h"
#include "packets.h"
#include "shared.h"
#include "unit.h"

#include "chatline.h"
#include "climisc.h"
#include "clinet.h"
#include "citydlg.h"
#include "cityrepdata.h"
#include "gui_main.h"
#include "mapview.h"
#include "optiondlg.h"
#include "options.h"
#include "repodlgs.h"
#include "support.h"

#include "cityrep.h"
#include "muistuff.h"

static Object *cityrep_wnd;
static Object *cityrep_listview;
static struct Hook cityreq_disphook;
static struct Hook cityreq_comparehook;
static Object *cityrep_title_text;
static Object *cityrep_center_button;
static Object *cityrep_popup_button;
static Object *cityrep_buy_button;
static Object *cityrep_change_button;
static Object *cityrep_configure_objects[NUM_CREPORT_COLS];

/**************************************************************************
 Display function for the listview in the city report window
**************************************************************************/
HOOKPROTONH(cityrep_display, int, char **array, struct city *pcity)
{
  static char buf[NUM_CREPORT_COLS][128];
  int i, j = 0;
  char *text;

  for(i = 0; i < NUM_CREPORT_COLS; i++)
  {
    if(city_report_specs[i].show)
    {
      buf[j][0] = '\0';
      if(pcity)
      {
        if((text = _(city_report_specs[i].func(pcity))))
          sz_strlcpy(buf[j], text);
      }
      else /* the header */
      {
        if(city_report_specs[i].title1)
        {
          sz_strlcpy(buf[j], city_report_specs[i].title1);
          sz_strlcat(buf[j], " ");
        }
        if(city_report_specs[i].title2)
          sz_strlcat(buf[j], city_report_specs[i].title2);
      }
      array[j] = buf[j];
      j++;
    }
  }
  return 0;
}

/**************************************************************************
 Compare function for the listview in the city report window
**************************************************************************/
HOOKPROTONH(cityrep_compare, int, struct city *pcity2, struct city *pcity1)
{
  return stricmp(pcity1->name, pcity2->name);
}

/**************************************************************************
 Callback if a new entry inside the listview is selected
**************************************************************************/
static void cityrep_active(void)
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

    impr_type_iterate(i) {
      if (can_build_improvement(pcity, i))
      {
	flag = 1;
      }
    } impr_type_iterate_end;

    unit_type_iterate(i) {
      if (can_build_unit(pcity, i))
      {
	flag = 1;
      }
    } unit_type_iterate_end;


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
}

/**************************************************************************
 Callback for the Buy Button
**************************************************************************/
static void cityrep_buy(void)
{
  struct city *pcity;

  DoMethod(cityrep_listview, MUIM_NList_GetEntry, MUIV_NList_GetEntry_Active,
	   &pcity);

  if (pcity) {
    cityrep_buy(pcity);
  }
}

/**************************************************************************
 Callback for the Center Button
**************************************************************************/
static void cityrep_center(void)
{
  struct city *pcity;

  DoMethod(cityrep_listview, MUIM_NList_GetEntry, MUIV_NList_GetEntry_Active, &pcity);

  if (pcity)
  {
    center_tile_mapcanvas(pcity->tile);
  }
}

/**************************************************************************
 Callback for the Popup Button
**************************************************************************/
static void cityrep_popup(void)
{
  struct city *pcity;

  DoMethod(cityrep_listview, MUIM_NList_GetEntry, MUIV_NList_GetEntry_Active, &pcity);

  if (pcity)
  {
    if (center_when_popup_city) {
      center_tile_mapcanvas(pcity->tile);
    }
    popup_city_dialog(pcity, 0);
  }
}

/**************************************************************************
 Callback for the Change Button
**************************************************************************/
static void cityrep_change(void)
{
  struct city *pcity;

  DoMethod(cityrep_listview, MUIM_NList_GetEntry, MUIV_NList_GetEntry_Active, &pcity);

  if(pcity)
    popup_city_production_dialog(pcity);
}

/****************************************************************
 Callback for the Refresh Button
*****************************************************************/
static void cityrep_refresh(void)
{
  struct city *pcity;
  struct packet_generic_integer packet;

  DoMethod(cityrep_listview, MUIM_NList_GetEntry, MUIV_NList_GetEntry_Active, &pcity);

  if(pcity) {
    packet.value = pcity->id;
    send_packet_generic_integer(&aconnection, PACKET_CITY_REFRESH, 
				&packet);
  }
}

/****************************************************************
 Callback for the Configure Ok button
*****************************************************************/
static void cityrep_configure_ok(void)
{
  UBYTE buffer[NUM_CREPORT_COLS+1];
  LONG i, j = 0;

  buffer[j++] = ','; /* the name! */
  for(i = 1; i < NUM_CREPORT_COLS; i++)
  {
    if((city_report_specs[i].show = xget(cityrep_configure_objects[i], MUIA_Selected)))
      buffer[j++] = ',';
  }
  buffer[j] = 0;

  set(cityrep_listview, MUIA_NList_Format, buffer);

  city_report_dialog_update();
}

/****************************************************************
 Callback for the Configure Button
*****************************************************************/
static void cityrep_configure(void)
{
  static Object *config_wnd = 0;
  LONG i, err = 0;

  if(!config_wnd)
  {
    Object *group, *o;
    Object *ok_button, *cancel_button;

    config_wnd = WindowObject,
      MUIA_Window_ID, MAKE_ID('O','P','T','C'),
      MUIA_Window_Title, _("Configure City Report"),
      WindowContents, VGroup,
        Child, TextObject,
          MUIA_Text_Contents,_("Set columns shown"),
          MUIA_Text_PreParse, "\33c",
          End,
        Child, HGroup,
          Child, HSpace(0),
          Child, group = ColGroup(2), End,
          Child, HSpace(0),
          End,
        Child, HGroup,
          Child, ok_button = MakeButton(_("_Ok")),
          Child, cancel_button = MakeButton(_("_Cancel")),
          End,
        End,
      End;

    if(config_wnd)
    {
      for(i = 1; i < NUM_CREPORT_COLS && !err; i++)
      {
        if((o = MakeLabel(city_report_specs[i].explanation)))
          DoMethod(group, OM_ADDMEMBER, o);
        else
          err++;

        if((cityrep_configure_objects[i] = MakeCheck(city_report_specs[i].explanation, FALSE)))
          DoMethod(group, OM_ADDMEMBER, cityrep_configure_objects[i]);
        else
          err++;
      }
      if(!err)
      {
        DoMethod(config_wnd, MUIM_Notify, MUIA_Window_CloseRequest, TRUE, config_wnd, 3, MUIM_Set, MUIA_Window_Open, FALSE);
        DoMethod(ok_button, MUIM_Notify, MUIA_Pressed, FALSE, config_wnd, 3, MUIM_CallHook, &civstandard_hook, cityrep_configure_ok);
        DoMethod(ok_button, MUIM_Notify, MUIA_Pressed, FALSE, config_wnd, 3, MUIM_Set, MUIA_Window_Open, FALSE);
        DoMethod(cancel_button, MUIM_Notify, MUIA_Pressed, FALSE, config_wnd, 3, MUIM_Set, MUIA_Window_Open, FALSE);
        DoMethod(app, OM_ADDMEMBER, config_wnd);
      }
      else
      {
        MUI_DisposeObject(config_wnd); config_wnd = 0; /* something went wrong */
      }
    }
  }

  if(config_wnd)
  {
    for (i = 0; i < NUM_CREPORT_COLS; i++)
      setcheckmark(cityrep_configure_objects[i], city_report_specs[i].show);
    set(config_wnd, MUIA_Window_Open, TRUE);
  }
}

/****************************************************************
 Create and initialize the city report window
*****************************************************************/
static void create_city_report_dialog(void)
{
  Object *cityrep_close_button;
  Object *cityrep_refresh_button;
  Object *cityrep_configure_button;
  UBYTE format[NUM_CREPORT_COLS+1];
  LONG i, j = 0;

  format[j++] = ','; /* the name! */
  for (i = 1; i < NUM_CREPORT_COLS; i++)
  {
    if(city_report_specs[i].show)
      format[j++] = ',';
  }
  format[j] = 0;

  if (cityrep_wnd)
    return;

  cityreq_disphook.h_Entry = (HOOKFUNC) cityrep_display;
  cityreq_comparehook.h_Entry = (HOOKFUNC) cityrep_compare;

  cityrep_wnd = WindowObject,
    MUIA_Window_Title, _("City Report"),
    MUIA_Window_ID, MAKE_ID('C','T','Y','R'),
    WindowContents, VGroup,
	Child, cityrep_title_text = TextObject,
	    MUIA_Text_PreParse, "\33c",
	    End,
	Child, cityrep_listview = NListviewObject,
	    MUIA_NListview_NList, NListObject,
		MUIA_NList_Format, format,
		MUIA_NList_DisplayHook, &cityreq_disphook,
		MUIA_NList_CompareHook, &cityreq_comparehook,
		MUIA_NList_Title, TRUE,
		End,
	    End,
	Child, HGroup,
	    Child, cityrep_close_button = MakeButton(_("_Close")),
	    Child, cityrep_center_button = MakeButton(_("Cen_ter")),
	    Child, cityrep_popup_button = MakeButton(_("_Popup")),
	    Child, cityrep_buy_button = MakeButton(_("_Buy")),
	    Child, cityrep_change_button = MakeButton(_("Chan_ge")),
	    Child, cityrep_refresh_button = MakeButton(_("_Refresh")),
	    Child, cityrep_configure_button = MakeButton(_("Con_figure")),
	    End,
	End,
    End;

  if (cityrep_wnd) {
    set(cityrep_title_text, MUIA_Text_Contents,
	get_report_title(_("City Report")));

    set(cityrep_change_button, MUIA_Disabled, TRUE);
    set(cityrep_center_button, MUIA_Disabled, TRUE);
    set(cityrep_popup_button, MUIA_Disabled, TRUE);
    set(cityrep_buy_button, MUIA_Disabled, TRUE);

    DoMethod(cityrep_wnd, MUIM_Notify, MUIA_Window_CloseRequest, TRUE, cityrep_wnd, 3, MUIM_Set, MUIA_Window_Open, FALSE);
    DoMethod(cityrep_close_button, MUIM_Notify, MUIA_Pressed, FALSE, cityrep_wnd, 3, MUIM_Set, MUIA_Window_Open, FALSE);
    DoMethod(cityrep_center_button, MUIM_Notify, MUIA_Pressed, FALSE, app, 3, MUIM_CallHook, &civstandard_hook, cityrep_center);
    DoMethod(cityrep_popup_button, MUIM_Notify, MUIA_Pressed, FALSE, app, 3, MUIM_CallHook, &civstandard_hook, cityrep_popup);
    DoMethod(cityrep_buy_button, MUIM_Notify, MUIA_Pressed, FALSE, app, 3, MUIM_CallHook, &civstandard_hook, cityrep_buy);
    DoMethod(cityrep_change_button, MUIM_Notify, MUIA_Pressed, FALSE, app, 3, MUIM_CallHook, &civstandard_hook, cityrep_change);
    DoMethod(cityrep_refresh_button, MUIM_Notify, MUIA_Pressed, FALSE, app, 3, MUIM_CallHook, &civstandard_hook, cityrep_refresh);
    DoMethod(cityrep_configure_button, MUIM_Notify, MUIA_Pressed, FALSE, app, 3, MUIM_CallHook, &civstandard_hook, cityrep_configure);
    DoMethod(cityrep_listview, MUIM_Notify, MUIA_NList_Active, MUIV_EveryTime, app, 3, MUIM_CallHook, &civstandard_hook, cityrep_active);
    DoMethod(cityrep_listview, MUIM_Notify, MUIA_NList_DoubleClick, TRUE, app, 3, MUIM_CallHook, &civstandard_hook, cityrep_center);

    DoMethod(app, OM_ADDMEMBER, cityrep_wnd);
  }
}

/****************************************************************
...
*****************************************************************/
void city_report_dialog_update(void)
{
  if (!cityrep_wnd)
    return;
  if (is_report_dialogs_frozen())
    return;

  set(cityrep_title_text, MUIA_Text_Contents,
      get_report_title(_("City Advisor")));

  set(cityrep_listview, MUIA_NList_Quiet, TRUE);
  DoMethod(cityrep_listview, MUIM_NList_Clear);

  city_list_iterate(game.player_ptr->cities, pcity)
  {
    DoMethod(cityrep_listview, MUIM_NList_InsertSingle, pcity, MUIV_NList_Insert_Sorted);
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
  struct city *pfindcity = 0;

  if (!cityrep_wnd)
    return;
  if (is_report_dialogs_frozen())
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

/****************************************************************
...
****************************************************************/
void popup_city_report_dialog(bool make_modal)
{
  if (!cityrep_wnd)
    create_city_report_dialog();
  if (cityrep_wnd)
  {
    city_report_dialog_update();
    set(cityrep_wnd, MUIA_Window_Open, TRUE);
  }
}

/****************************************************************
 After a selection rectangle is defined, make the cities that
 are hilited on the canvas exclusively hilited in the
 City List window.
*****************************************************************/
void hilite_cities_from_canvas(void)
{
  /* PORTME */
}

/****************************************************************
 Toggle a city's hilited status.
*****************************************************************/
void toggle_city_hilite(struct city *pcity, bool on_off)
{
  /* PORTME */
}
