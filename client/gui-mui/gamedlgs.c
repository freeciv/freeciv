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

#include <libraries/mui.h>
#include <clib/alib_protos.h>
#include <proto/exec.h>
#include <proto/muimaster.h>

#include "events.h"
#include "fcintl.h"
#include "game.h"
#include "government.h"
#include "packets.h"
#include "player.h"
#include "shared.h"

#include "chatline.h"
#include "cityrep.h"
#include "clinet.h"
#include "dialogs.h"
#include "mapview.h"
#include "options.h"

#include "ratesdlg.h"
#include "optiondlg.h"
#include "gui_main.h"
#include "muistuff.h"


/**************************************************************************
 Gui independend
**************************************************************************/
void request_player_rates(int rates_tax_value, int rates_sci_value, int rates_lux_value)
{
  struct packet_player_request packet;

  packet.tax = rates_tax_value;
  packet.science = rates_sci_value;
  packet.luxury = rates_lux_value;
  send_packet_player_request(&aconnection, &packet, PACKET_PLAYER_RATES);
}


/******************************************************************/

static Object *rates_wnd;
static Object *rates_title_text;
static Object *rates_tax_slider;
static Object *rates_luxury_slider;
static Object *rates_science_slider;

static int rates_tax_value, rates_lux_value, rates_sci_value;

/**************************************************************************
...
**************************************************************************/
static void rates_set_values(int tax, int no_tax_scroll,
			     int lux, int no_lux_scroll,
			     int sci, int no_sci_scroll)
{
  int tax_lock, lux_lock, sci_lock;
  int maxrate;

  tax_lock = 0;
  lux_lock = 0;
  sci_lock = 0;

  maxrate = get_government_max_rate(game.player_ptr->government);
  /* This's quite a simple-minded "double check".. */
  tax = MIN(tax, maxrate);
  lux = MIN(lux, maxrate);
  sci = MIN(sci, maxrate);

  if (tax + sci + lux != 100)
  {
    if ((tax != rates_tax_value))
    {
      if (!lux_lock)
	lux = MIN(MAX(100 - tax - sci, 0), maxrate);
      if (!sci_lock)
	sci = MIN(MAX(100 - tax - lux, 0), maxrate);
    }
    else if ((lux != rates_lux_value))
    {
      if (!tax_lock)
	tax = MIN(MAX(100 - lux - sci, 0), maxrate);
      if (!sci_lock)
	sci = MIN(MAX(100 - lux - tax, 0), maxrate);
    }
    else if ((sci != rates_sci_value))
    {
      if (!lux_lock)
	lux = MIN(MAX(100 - tax - sci, 0), maxrate);
      if (!tax_lock)
	tax = MIN(MAX(100 - lux - sci, 0), maxrate);
    }

    if (tax + sci + lux != 100)
    {
      tax = rates_tax_value;
      lux = rates_lux_value;
      sci = rates_sci_value;

      rates_tax_value = -1;
      rates_lux_value = -1;
      rates_sci_value = -1;

      no_tax_scroll = 0;
      no_lux_scroll = 0;
      no_sci_scroll = 0;
    }

  }

  if (tax != rates_tax_value)
  {
    if (!no_tax_scroll)
      nnset(rates_tax_slider, MUIA_Numeric_Value, tax / 10);
    rates_tax_value = tax;
  }

  if (lux != rates_lux_value)
  {
    if (!no_lux_scroll)
      nnset(rates_luxury_slider, MUIA_Numeric_Value, lux / 10);
    rates_lux_value = lux;
  }

  if (sci != rates_sci_value)
  {
    if (!no_sci_scroll)
      nnset(rates_science_slider, MUIA_Numeric_Value, sci / 10);
    rates_sci_value = sci;
  }
}

/****************************************************************
 Callback for the Numeric buttons
*****************************************************************/
static void Rates_Change(Object ** pobj)
{
  Object *o = *pobj;
  int percent = xget(o, MUIA_Numeric_Value);

  if (o == rates_tax_slider)
  {
    int tax_value;

    tax_value = 10 * percent;
    tax_value = MIN(tax_value, 100);
    rates_set_values(tax_value, 1, rates_lux_value, 0, rates_sci_value, 0);
  }
  else if (o == rates_luxury_slider)
  {
    int lux_value;

    lux_value = 10 * percent;
    lux_value = MIN(lux_value, 100);
    rates_set_values(rates_tax_value, 0, lux_value, 1, rates_sci_value, 0);
  }
  else if (o == rates_science_slider)
  {
    int sci_value;

    sci_value = 10 * percent;
    sci_value = MIN(sci_value, 100);
    rates_set_values(rates_tax_value, 0, rates_lux_value, 0, sci_value, 1);
  }
}

/****************************************************************
 Callback for the Ok button
*****************************************************************/
static void rates_ok(void)
{
  request_player_rates(rates_tax_value, rates_sci_value, rates_lux_value);
  set(rates_wnd, MUIA_Window_Open, FALSE);
}

/****************************************************************
... 
*****************************************************************/
static void create_rates_dialog(void)
{
  Object *ok_button, *cancel_button;

  if (rates_wnd)
    return;

  rates_wnd = WindowObject,
      MUIA_Window_Title, _("Set tax, luxury and science rates"),
      WindowContents, VGroup,
          Child, rates_title_text = TextObject, End,

          Child, ColGroup(2),
              Child, MakeLabel(_("Tax:")),
              Child, rates_tax_slider = SliderObject,
                  MUIA_Numeric_Min, 0,
                  MUIA_Numeric_Max, 10,
                  MUIA_Numeric_Format, "%ld0%%",
                  End,

              Child, MakeLabel(_("Luxury:")),
              Child, rates_luxury_slider = SliderObject,
                  MUIA_Numeric_Min, 0,
                  MUIA_Numeric_Max, 10,
                  MUIA_Numeric_Format, "%ld0%%",
                  End,

              Child, MakeLabel(_("Science:")),
              Child, rates_science_slider = SliderObject,
                  MUIA_Numeric_Min, 0,
                  MUIA_Numeric_Max, 10,
                  MUIA_Numeric_Format, "%ld0%%",
                  End,
              End,

          Child, HGroup,
              Child, ok_button = MakeButton(_("_Ok")),
              Child, cancel_button = MakeButton(_("_Cancel")),
              End,
          End,
      End;

  if (rates_wnd)
  {
    DoMethod(rates_wnd, MUIM_Notify, MUIA_Window_CloseRequest, TRUE, rates_wnd, 3, MUIM_Set, MUIA_Window_Open, FALSE);
    DoMethod(cancel_button, MUIM_Notify, MUIA_Pressed, FALSE, rates_wnd, 3, MUIM_Set, MUIA_Window_Open, FALSE);
    DoMethod(ok_button, MUIM_Notify, MUIA_Pressed, FALSE, app, 3, MUIM_CallHook, &civstandard_hook, rates_ok);
    DoMethod(rates_tax_slider, MUIM_Notify, MUIA_Numeric_Value, MUIV_EveryTime, app, 4, MUIM_CallHook, &civstandard_hook, Rates_Change, rates_tax_slider);
    DoMethod(rates_luxury_slider, MUIM_Notify, MUIA_Numeric_Value, MUIV_EveryTime, app, 4, MUIM_CallHook, &civstandard_hook, Rates_Change, rates_luxury_slider);
    DoMethod(rates_science_slider, MUIM_Notify, MUIA_Numeric_Value, MUIV_EveryTime, app, 4, MUIM_CallHook, &civstandard_hook, Rates_Change, rates_science_slider);
    DoMethod(app, OM_ADDMEMBER, rates_wnd);
  }
}

static void update_rates_dialog(void)
{
  int max_rate = get_government_max_rate(game.player_ptr->government);
  settextf(rates_title_text, _("%s max rate: %d%%"),
	   get_government_name(game.player_ptr->government),
	   max_rate);

  set(rates_tax_slider, MUIA_Numeric_Max, max_rate / 10);
  set(rates_luxury_slider, MUIA_Numeric_Max, max_rate / 10);
  set(rates_science_slider, MUIA_Numeric_Max, max_rate / 10);

  rates_set_values(game.player_ptr->economic.tax, 0,
		   game.player_ptr->economic.luxury, 0,
		   game.player_ptr->economic.science, 0);
}

/****************************************************************
... 
*****************************************************************/
void popup_rates_dialog(void)
{
  if (!rates_wnd)
    create_rates_dialog();

  if (rates_wnd)
  {
    update_rates_dialog();
    set(rates_wnd, MUIA_Window_Open, TRUE);
  }
}

/**************************************************************************
 Callback for the Ok button
************************************************************************* */
static void option_ok(void)
{
  client_options_iterate(o) {
    Object *obj = (Object *) o->p_gui_data;
    if (obj)
    {
      if (o->type == COT_BOOL) *(o->p_bool_value) = xget(obj, MUIA_Selected);
      else if (o->type == COT_INT) *(o->p_int_value) = xget(obj, MUIA_String_Integer);
    }
  } client_options_iterate_end;

  update_map_canvas_visible();
}

/****************************************************************
... 
*****************************************************************/
static void create_option_dialog(void)
{
  static Object * option_wnd;

  if (!option_wnd)
  {
    Object *group;
    Object *ok_button, *cancel_button;

    option_wnd = WindowObject,
        MUIA_Window_ID, MAKE_ID('O','P','T','I'),
        MUIA_Window_Title, _("Set local options"),
        WindowContents, VGroup,
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

    if (option_wnd)
    {
      client_options_iterate(o) {
      	Object *obj, *label;

	if (o->type == COT_BOOL) obj = MakeCheck(_(o->description), FALSE);
	else if (o->type == COT_INT) obj = MakeInteger(_(o->description));
	else obj = NULL;

	
	if (obj)
	{
	  if ((label = MakeLabelLeft(_(o->description))))
	  {
	    o->p_gui_data = obj;
	    DoMethod(group, OM_ADDMEMBER, label);
	    DoMethod(group, OM_ADDMEMBER, obj);
	  } else o->p_gui_data = NULL;
	} else o->p_gui_data = NULL;
      } client_options_iterate_end;

      DoMethod(option_wnd, MUIM_Notify, MUIA_Window_CloseRequest, TRUE, option_wnd, 3, MUIM_Set, MUIA_Window_Open, FALSE);
      DoMethod(ok_button, MUIM_Notify, MUIA_Pressed, FALSE, option_wnd, 3, MUIM_CallHook, &civstandard_hook, option_ok);
      DoMethod(ok_button, MUIM_Notify, MUIA_Pressed, FALSE, option_wnd, 3, MUIM_Set, MUIA_Window_Open, FALSE);
      DoMethod(cancel_button, MUIM_Notify, MUIA_Pressed, FALSE, option_wnd, 3, MUIM_Set, MUIA_Window_Open, FALSE);
      DoMethod(app, OM_ADDMEMBER, option_wnd);
    }
  }

  if (option_wnd)
  {
    client_options_iterate(o) {
      Object *obj = (Object *) o->p_gui_data;
      if (obj)
      {
      	if (o->type == COT_BOOL) setcheckmark(obj, *(o->p_bool_value));
	else if (o->type == COT_INT) set(obj,MUIA_String_Integer,*(o->p_int_value));
      }
    } client_options_iterate_end;

    set(option_wnd, MUIA_Window_Open, TRUE);
  }
}

/****************************************************************
... 
*****************************************************************/
void popup_option_dialog(void)
{
  create_option_dialog();
}
