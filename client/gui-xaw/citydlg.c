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

#include <X11/Intrinsic.h>
#include <X11/StringDefs.h>
#include <X11/Xaw/Form.h>
#include <X11/Xaw/Label.h>
#include <X11/Xaw/Command.h>
#include <X11/Xaw/SimpleMenu.h>
#include <X11/Xaw/Command.h>
#include <X11/Xaw/List.h>
#include <X11/Xaw/Viewport.h>
#include <X11/Xaw/AsciiText.h>  
#include <X11/Xaw/Toggle.h>     
#include <X11/IntrinsicP.h>

#include "pixcomm.h"
#include "canvas.h"

#include "city.h"
#include "game.h"
#include "genlist.h"
#include "map.h"
#include "mem.h"
#include "packets.h"
#include "player.h"
#include "shared.h"

#include "cityrep.h"
#include "colors.h"
#include "control.h" /* request_xxx and set_unit_focus */
#include "dialogs.h"
#include "graphics.h"
#include "gui_stuff.h"
#include "helpdlg.h"
#include "inputdlg.h"
#include "mapctrl.h"
#include "mapview.h"
#include "optiondlg.h"		/* for toggle_callback */
#include "repodlgs.h"

#include "citydlg.h"

#include "cityicon.ico"

extern Display	*display;
extern Widget toplevel, main_form, map_canvas;
extern int display_depth;
extern struct connection aconnection;
extern int map_view_x0, map_view_y0;
extern int flags_are_transparent;
extern GC fill_bg_gc;

#define NUM_UNITS_SHOWN  12
#define NUM_CITIZENS_SHOWN 25

struct city_dialog {
  struct city *pcity;
  Widget shell;
  Widget main_form;
  Widget cityname_label;
  Widget citizen_labels[NUM_CITIZENS_SHOWN];
  Widget production_label;
  Widget output_label;
  Widget storage_label;
  Widget pollution_label;
  Widget sub_form;
  Widget map_canvas;
  Widget sell_command;
  Widget close_command, rename_command, trade_command, activate_command;
  Widget show_units_command, cityopt_command;
  Widget building_label, progress_label, buy_command, change_command;
  Widget improvement_viewport, improvement_list;
  Widget support_unit_label;
  Widget support_unit_pixcomms[NUM_UNITS_SHOWN];
  Widget present_unit_label;
  Widget present_unit_pixcomms[NUM_UNITS_SHOWN];
  Widget change_list;
  Widget rename_input;
  
  enum improvement_type_id sell_id;
  
  int citizen_type[NUM_CITIZENS_SHOWN];
  int support_unit_ids[NUM_UNITS_SHOWN];
  int present_unit_ids[NUM_UNITS_SHOWN];
  char improvlist_names[B_LAST+1][64];
  char *improvlist_names_ptrs[B_LAST+1];
  
  char *change_list_names_ptrs[B_LAST+1+U_LAST+1+1];
  char change_list_names[B_LAST+1+U_LAST+1][200];
  int change_list_ids[B_LAST+1+U_LAST+1];
  int change_list_num_improvements;

  int is_modal;
};

static struct genlist dialog_list;
static int dialog_list_has_been_initialised;

struct city_dialog *get_city_dialog(struct city *pcity);
struct city_dialog *create_city_dialog(struct city *pcity, int make_modal);
void close_city_dialog(struct city_dialog *pdialog);

void city_dialog_update_improvement_list(struct city_dialog *pdialog);
void city_dialog_update_title(struct city_dialog *pdialog);
void city_dialog_update_supported_units(struct city_dialog *pdialog, int id);
void city_dialog_update_present_units(struct city_dialog *pdialog, int id);
void city_dialog_update_citizens(struct city_dialog *pdialog);
void city_dialog_update_map(struct city_dialog *pdialog);
void city_dialog_update_production(struct city_dialog *pdialog);
void city_dialog_update_output(struct city_dialog *pdialog);
void city_dialog_update_building(struct city_dialog *pdialog);
void city_dialog_update_storage(struct city_dialog *pdialog);
void city_dialog_update_pollution(struct city_dialog *pdialog);

void sell_callback(Widget w, XtPointer client_data, XtPointer call_data);
void buy_callback(Widget w, XtPointer client_data, XtPointer call_data);
void change_callback(Widget w, XtPointer client_data, XtPointer call_data);
void close_callback(Widget w, XtPointer client_data, XtPointer call_data);
void rename_callback(Widget w, XtPointer client_data, XtPointer call_data);
void trade_callback(Widget w, XtPointer client_data, XtPointer call_data);
void activate_callback(Widget w, XtPointer client_data, XtPointer call_data);
void show_units_callback(Widget W, XtPointer client_data, XtPointer call_data);
void unitupgrade_callback_yes(Widget w, XtPointer client_data,
			      XtPointer call_data);
void unitupgrade_callback_no(Widget w, XtPointer client_data,
			     XtPointer call_data);
void upgrade_callback(Widget w, XtPointer client_data, XtPointer call_data);

void elvis_callback(Widget w, XtPointer client_data, XtPointer call_data);
void scientist_callback(Widget w, XtPointer client_data, XtPointer call_data);
void taxman_callback(Widget w, XtPointer client_data, XtPointer call_data);
void rename_ok_return_action(Widget w, XEvent *event, String *params, 
			     Cardinal *nparams);

void present_units_callback(Widget w, XtPointer client_data, 
			    XtPointer call_data);
void cityopt_callback(Widget w, XtPointer client_data, 
			    XtPointer call_data);
void popdown_cityopt_dialog(void);

char *dummy_improvement_list[]={ 
  "Copernicus' Observatory  ",
  "Copernicus' Observatory  ",
  "Copernicus' Observatory  ",
  "Copernicus' Observatory  ",
  "Copernicus' Observatory  ",
  0
};

char *dummy_change_list[]={ 
  "Copernicus' Observatory  125 turns",
  "Copernicus' Observatory  125 turns",
  "Copernicus' Observatory  125 turns",
  "Copernicus' Observatory  125 turns",
  "Copernicus' Observatory  125 turns",
  "Copernicus' Observatory  125 turns",
  "Copernicus' Observatory  125 turns",
  "Copernicus' Observatory  125 turns",
  "Copernicus' Observatory  125 turns",
  "Copernicus' Observatory  125 turns",
  0
};


Pixmap icon_pixmap;
extern Atom wm_delete_window;

/****************************************************************
...
*****************************************************************/
struct city_dialog *get_city_dialog(struct city *pcity)
{
  struct genlist_iterator myiter;

  if(!dialog_list_has_been_initialised) {
    genlist_init(&dialog_list);
    dialog_list_has_been_initialised=1;
  }
  
  genlist_iterator_init(&myiter, &dialog_list, 0);
    
  for(; ITERATOR_PTR(myiter); ITERATOR_NEXT(myiter))
    if(((struct city_dialog *)ITERATOR_PTR(myiter))->pcity==pcity)
      return ITERATOR_PTR(myiter);

  return 0;
}

/****************************************************************
...
*****************************************************************/
void refresh_city_dialog(struct city *pcity)
{
  struct city_dialog *pdialog;
  
  if((pdialog=get_city_dialog(pcity))) {
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

    XtSetSensitive(pdialog->trade_command,
    		   city_num_trade_routes(pcity)?True:False);
    XtSetSensitive(pdialog->activate_command,
		   unit_list_size(&map_get_tile(pcity->x,pcity->y)->units)
		   ?True:False);
    XtSetSensitive(pdialog->show_units_command,
                   unit_list_size(&map_get_tile(pcity->x,pcity->y)->units)
		   ?True:False);
    XtSetSensitive(pdialog->cityopt_command, True);
  }
  if(pcity->owner == game.player_idx)  {
    city_report_dialog_update_city(pcity);
    trade_report_dialog_update();
  } else {
    if(pdialog)  {
      /* Set the buttons we do not want live while a Diplomat investigates */
      XtSetSensitive(pdialog->buy_command, FALSE);
      XtSetSensitive(pdialog->change_command, FALSE);
      XtSetSensitive(pdialog->sell_command, FALSE);
      XtSetSensitive(pdialog->rename_command, FALSE);
      XtSetSensitive(pdialog->activate_command, FALSE);
      XtSetSensitive(pdialog->show_units_command, FALSE);
      XtSetSensitive(pdialog->cityopt_command, FALSE);
    }
  }
}

/****************************************************************
...
*****************************************************************/
void refresh_unit_city_dialogs(struct unit *punit)
{
  struct city *pcity_sup, *pcity_pre;
  struct city_dialog *pdialog;

  pcity_sup=city_list_find_id(&game.player_ptr->cities, punit->homecity);
  pcity_pre=map_get_city(punit->x, punit->y);
  
  if(pcity_sup && (pdialog=get_city_dialog(pcity_sup)))
    city_dialog_update_supported_units(pdialog, 0);
  
  if(pcity_pre && (pdialog=get_city_dialog(pcity_pre)))
    city_dialog_update_present_units(pdialog, 0);
}

/****************************************************************
popup the dialog 10% inside the main-window 
*****************************************************************/
void popup_city_dialog(struct city *pcity, int make_modal)
{
  struct city_dialog *pdialog;
  
  if(!(pdialog=get_city_dialog(pcity)))
    pdialog=create_city_dialog(pcity, make_modal);

  xaw_set_relative_position(toplevel, pdialog->shell, 10, 10);
  XtPopup(pdialog->shell, XtGrabNone);
}

/****************************************************************
popdown the dialog 
*****************************************************************/
void popdown_city_dialog(struct city *pcity)
{
  struct city_dialog *pdialog;
  
  if((pdialog=get_city_dialog(pcity)))
    close_city_dialog(pdialog);
}

/****************************************************************
popdown all dialogs
*****************************************************************/
void popdown_all_city_dialogs(void)
{
  if(!dialog_list_has_been_initialised) {
    return;
  }
  while(genlist_size(&dialog_list)) {
    close_city_dialog(genlist_get(&dialog_list,0));
  }
  popdown_cityopt_dialog();
}


/****************************************************************
...
*****************************************************************/
static void city_map_canvas_expose(Widget w, XEvent *event, Region exposed, 
				   void *client_data)
{
  struct city_dialog *pdialog;
  
  pdialog=(struct city_dialog *)client_data;
  city_dialog_update_map(pdialog);
}


/****************************************************************
...
*****************************************************************/
struct city_dialog *create_city_dialog(struct city *pcity, int make_modal)
{
  int i;
  struct city_dialog *pdialog;
  XtTranslations textfieldtranslations;
  
  pdialog=fc_malloc(sizeof(struct city_dialog));
  pdialog->pcity=pcity;

  if(!icon_pixmap)
    icon_pixmap=XCreateBitmapFromData(display,
				      RootWindowOfScreen(XtScreen(toplevel)),
				      cityicon_bits,
				      cityicon_width, cityicon_height);

  
  pdialog->shell=XtVaCreatePopupShell(pcity->name,
				      make_modal ? transientShellWidgetClass :
				      topLevelShellWidgetClass,
				      toplevel, 
				      XtNallowShellResize, True, 
				      NULL);
  
  pdialog->main_form=
    XtVaCreateManagedWidget("citymainform", 
			    formWidgetClass, 
			    pdialog->shell, 
			    NULL);
  pdialog->cityname_label=
    XtVaCreateManagedWidget("citynamelabel", 
			    labelWidgetClass,
			    pdialog->main_form,
			    NULL);

  pdialog->citizen_labels[0]=
    XtVaCreateManagedWidget("citizenlabels",
			    commandWidgetClass,
			    pdialog->main_form,
			    XtNfromVert, 
			    pdialog->cityname_label,
			    XtNbitmap, get_citizen_pixmap(2),
			    NULL);


  for(i=1; i<NUM_CITIZENS_SHOWN; i++)
    pdialog->citizen_labels[i]=
    XtVaCreateManagedWidget("citizenlabels",
			    commandWidgetClass,
			    pdialog->main_form,
			    XtNfromVert, 
			    pdialog->cityname_label,
			    XtNfromHoriz, 
			    (XtArgVal)pdialog->citizen_labels[i-1],
			    XtNbitmap, get_citizen_pixmap(2),
			    NULL);
    
  pdialog->sub_form=
    XtVaCreateManagedWidget("citysubform", 
			    formWidgetClass, 
			    pdialog->main_form, 
			    XtNfromVert, 
			    (XtArgVal)pdialog->citizen_labels[0],
			    NULL);


  pdialog->production_label=
    XtVaCreateManagedWidget("cityprodlabel", 
			    labelWidgetClass,
			    pdialog->sub_form,
			    NULL);

  pdialog->output_label=
    XtVaCreateManagedWidget("cityoutputlabel", 
			    labelWidgetClass,
			    pdialog->sub_form,
			    XtNfromVert, 
			    (XtArgVal)pdialog->production_label,
			    NULL);

  pdialog->storage_label=
    XtVaCreateManagedWidget("citystoragelabel", 
			    labelWidgetClass,
			    pdialog->sub_form,
			    XtNfromVert, 
			    (XtArgVal)pdialog->output_label,
			    NULL);

  pdialog->pollution_label=
    XtVaCreateManagedWidget("citypollutionlabel", 
			    labelWidgetClass,
			    pdialog->sub_form,
			    XtNfromVert, 
			    (XtArgVal)pdialog->storage_label,
			    NULL);
  
  
  pdialog->map_canvas=
    XtVaCreateManagedWidget("citymapcanvas", 
			    xfwfcanvasWidgetClass,
			    pdialog->sub_form,
			    "exposeProc", (XtArgVal)city_map_canvas_expose,
			    "exposeProcData", (XtArgVal)pdialog,
			    XtNfromHoriz, (XtArgVal)pdialog->production_label,
			    XtNwidth, NORMAL_TILE_WIDTH*5,
			    XtNheight, NORMAL_TILE_HEIGHT*5,
			    NULL);

  
  pdialog->building_label=
    XtVaCreateManagedWidget("citybuildinglabel",
			    labelWidgetClass,
			    pdialog->sub_form,
			    XtNfromHoriz, 
			    (XtArgVal)pdialog->map_canvas,
			    NULL);
  
  pdialog->progress_label=
    XtVaCreateManagedWidget("cityprogresslabel",
			    labelWidgetClass,
			    pdialog->sub_form,
			    XtNfromHoriz, 
			    (XtArgVal)pdialog->map_canvas,
			    XtNfromVert, 
			    pdialog->building_label,
			    NULL);

  pdialog->buy_command=
    XtVaCreateManagedWidget("citybuycommand", 
			    commandWidgetClass,
			    pdialog->sub_form,
			    XtNfromVert, 
			    pdialog->building_label,
			    XtNfromHoriz, 
			    (XtArgVal)pdialog->progress_label,
			    NULL);

  pdialog->change_command=
    XtVaCreateManagedWidget("citychangecommand", 
			    commandWidgetClass,
			    pdialog->sub_form,
			    XtNfromVert, 
			    pdialog->building_label,
			    XtNfromHoriz, 
			    (XtArgVal)pdialog->buy_command,
			    NULL);
 
  pdialog->improvement_viewport=
    XtVaCreateManagedWidget("cityimprovview", 
			    viewportWidgetClass,
			    pdialog->sub_form,
			    XtNfromHoriz, 
			    (XtArgVal)pdialog->map_canvas,
			    XtNfromVert, 
			    pdialog->progress_label,
			    NULL);


  pdialog->improvement_list=
    XtVaCreateManagedWidget("cityimprovlist", 
			    listWidgetClass,
			    pdialog->improvement_viewport,
			    XtNforceColumns, 1,
			    XtNdefaultColumns,1, 
			    XtNlist, (XtArgVal)dummy_improvement_list,
			    XtNverticalList, False,
			    NULL);

  pdialog->sell_command=
    XtVaCreateManagedWidget("citysellcommand", 
			    commandWidgetClass,
			    pdialog->sub_form,
			    XtNfromVert, 
			    pdialog->improvement_viewport,
			    XtNfromHoriz, 
			    (XtArgVal)pdialog->map_canvas,
			    NULL);
  
  pdialog->support_unit_label=
    XtVaCreateManagedWidget("supportunitlabel",
			    labelWidgetClass,
			    pdialog->main_form,
			    XtNfromVert, 
			    pdialog->sub_form,
			    NULL);

  pdialog->support_unit_pixcomms[0]=
    XtVaCreateManagedWidget("supportunitcanvas",
			    pixcommWidgetClass,
			    pdialog->main_form,
			    XtNfromVert, pdialog->support_unit_label,
			    XtNwidth, NORMAL_TILE_WIDTH,
			    XtNheight, NORMAL_TILE_HEIGHT+NORMAL_TILE_HEIGHT/2,
			    NULL);
  pdialog->support_unit_ids[0]=-1;


  for(i=1; i<NUM_UNITS_SHOWN; i++) {
    pdialog->support_unit_pixcomms[i]=
      XtVaCreateManagedWidget("supportunitcanvas",
			      pixcommWidgetClass,
			      pdialog->main_form,
			      XtNfromVert, pdialog->support_unit_label,
			      XtNfromHoriz, (XtArgVal)pdialog->support_unit_pixcomms[i-1],
			      XtNwidth, NORMAL_TILE_WIDTH,
			      XtNheight, NORMAL_TILE_HEIGHT+NORMAL_TILE_HEIGHT/2,
			      NULL);
    pdialog->support_unit_ids[i]=-1;
    
    if(pcity->owner != game.player_idx)
      XtSetSensitive(pdialog->support_unit_pixcomms[i], FALSE);    
  }

  pdialog->present_unit_label=
    XtVaCreateManagedWidget("presentunitlabel",
			    labelWidgetClass,
			    pdialog->main_form,
			    XtNfromVert, 
			    pdialog->support_unit_pixcomms[0],
			    NULL);

  pdialog->present_unit_pixcomms[0]=
    XtVaCreateManagedWidget("presentunitcanvas",
    			    pixcommWidgetClass,
			    pdialog->main_form,
			    XtNfromVert, pdialog->present_unit_label,
			    XtNwidth, NORMAL_TILE_WIDTH,
			    XtNheight, NORMAL_TILE_HEIGHT,
			    NULL);
  pdialog->present_unit_ids[0]=-1;

  for(i=1; i<NUM_UNITS_SHOWN; i++) {
    pdialog->present_unit_pixcomms[i]=
      XtVaCreateManagedWidget("presentunitcanvas",
			      pixcommWidgetClass,
			      pdialog->main_form,
			      XtNfromVert, pdialog->present_unit_label,
			      XtNfromHoriz, 
			      (XtArgVal)pdialog->support_unit_pixcomms[i-1],
			      XtNwidth, NORMAL_TILE_WIDTH,
			      XtNheight, NORMAL_TILE_HEIGHT,
			      NULL);
    pdialog->present_unit_ids[i]=-1;

    if(pcity->owner != game.player_idx)
      XtSetSensitive(pdialog->present_unit_pixcomms[i], FALSE);
  }

  
  XtVaSetValues(pdialog->shell, XtNiconPixmap, icon_pixmap, NULL);

  pdialog->close_command=
    XtVaCreateManagedWidget("cityclosecommand", 
			    commandWidgetClass,
			    pdialog->main_form,
			    XtNfromVert, 
			    pdialog->present_unit_pixcomms[0],
			    NULL);

  pdialog->rename_command=
    XtVaCreateManagedWidget("cityrenamecommand", 
			    commandWidgetClass,
			    pdialog->main_form,
			    XtNfromVert, 
			    pdialog->present_unit_pixcomms[0],
			    XtNfromHoriz,
			    pdialog->close_command,
			    NULL);

  pdialog->trade_command=
    XtVaCreateManagedWidget("citytradecommand", 
			    commandWidgetClass,
			    pdialog->main_form,
			    XtNfromVert, 
			    pdialog->present_unit_pixcomms[0],
			    XtNfromHoriz,
			    pdialog->rename_command,
			    NULL);

  pdialog->activate_command=
    XtVaCreateManagedWidget("cityactivatecommand",
    			    commandWidgetClass,
			    pdialog->main_form,
			    XtNfromVert, pdialog->present_unit_pixcomms[0],
			    XtNfromHoriz, pdialog->trade_command,
			    NULL);

  pdialog->show_units_command=
    XtVaCreateManagedWidget("cityshowunitscommand",
			    commandWidgetClass,
			    pdialog->main_form,
			    XtNfromVert, pdialog->present_unit_pixcomms[0],
			    XtNfromHoriz, pdialog->activate_command,
			    NULL);

  pdialog->cityopt_command=
    XtVaCreateManagedWidget("cityoptionscommand",
			    commandWidgetClass,
			    pdialog->main_form,
			    XtNfromVert, pdialog->present_unit_pixcomms[0],
			    XtNfromHoriz, pdialog->show_units_command,
			    NULL);

  XtAddCallback(pdialog->sell_command, XtNcallback, sell_callback,
		(XtPointer)pdialog);

  XtAddCallback(pdialog->buy_command, XtNcallback, buy_callback,
		(XtPointer)pdialog);

  XtAddCallback(pdialog->change_command, XtNcallback, change_callback,
		(XtPointer)pdialog);

  XtAddCallback(pdialog->close_command, XtNcallback, close_callback,
		(XtPointer)pdialog);

  XtAddCallback(pdialog->rename_command, XtNcallback, rename_callback,
		(XtPointer)pdialog);

  XtAddCallback(pdialog->trade_command, XtNcallback, trade_callback,
		(XtPointer)pdialog);

  XtAddCallback(pdialog->activate_command, XtNcallback, activate_callback,
		(XtPointer)pdialog);

  XtAddCallback(pdialog->show_units_command, XtNcallback, show_units_callback,
		(XtPointer)pdialog);

  XtAddCallback(pdialog->cityopt_command, XtNcallback, cityopt_callback,
		(XtPointer)pdialog);

  genlist_insert(&dialog_list, pdialog, 0);

  for(i=0; i<B_LAST+1; i++)
    pdialog->improvlist_names_ptrs[i]=0;

  for(i=0; i<NUM_CITIZENS_SHOWN; i++)
    pdialog->citizen_type[i]=-1;

  
  XtRealizeWidget(pdialog->shell);

  refresh_city_dialog(pdialog->pcity);

  if(make_modal)
    XtSetSensitive(toplevel, FALSE);
  
  pdialog->is_modal=make_modal;

  XSetWMProtocols(display, XtWindow(pdialog->shell), &wm_delete_window, 1);
  XtOverrideTranslations(pdialog->shell, 
    XtParseTranslationTable ("<Message>WM_PROTOCOLS: close-citydialog()"));

  textfieldtranslations = 
    XtParseTranslationTable("<Key>Return: city-dialog-returnkey()");
  XtOverrideTranslations(pdialog->close_command, textfieldtranslations);
  XtSetKeyboardFocus(pdialog->shell, pdialog->close_command);


  return pdialog;
}

/****************************************************************
...
*****************************************************************/
void activate_callback(Widget w, XtPointer client_data,
		       XtPointer call_data)
{
  struct city_dialog *pdialog = (struct city_dialog *)client_data;
  int x=pdialog->pcity->x,y=pdialog->pcity->y;
  struct unit_list *punit_list = &map_get_tile(x,y)->units;

  if( unit_list_size(punit_list) )  {
    unit_list_iterate((*punit_list), punit) {
      if(game.player_idx==punit->owner) {
	request_new_unit_activity(punit, ACTIVITY_IDLE);
      }
    } unit_list_iterate_end;
    set_unit_focus(unit_list_get(punit_list, 0));
  }
}


/****************************************************************
...
*****************************************************************/
void show_units_callback(Widget w, XtPointer client_data,
                        XtPointer call_data)
{
  struct city_dialog *pdialog = (struct city_dialog *)client_data;
  struct tile *ptile = map_get_tile(pdialog->pcity->x, pdialog->pcity->y);

  if( unit_list_size(&ptile->units) )
    popup_unit_select_dialog(ptile);
}


#ifdef UNUSED
/****************************************************************
...
*****************************************************************/
static void present_units_ok_callback(Widget w, XtPointer client_data, 
				      XtPointer call_data)
{
  destroy_message_dialog(w);
}
#endif


/****************************************************************
...
*****************************************************************/
void activate_unit(struct unit *punit)
{
  if((punit->activity!=ACTIVITY_IDLE || punit->ai.control)
     && can_unit_do_activity(punit, ACTIVITY_IDLE))
    request_new_unit_activity(punit, ACTIVITY_IDLE);
  set_unit_focus(punit);
}


/****************************************************************
...
*****************************************************************/
static void present_units_activate_callback(Widget w, XtPointer client_data, 
					    XtPointer call_data)
{
  struct unit *punit;

  if((punit=unit_list_find(&game.player_ptr->units, (size_t)client_data)))
    activate_unit(punit);
  destroy_message_dialog(w);
}


/****************************************************************
...
*****************************************************************/
static void present_units_activate_close_callback(Widget w,
						  XtPointer client_data, 
						  XtPointer call_data)
{
  struct unit *punit;
  struct city *pcity;
  struct city_dialog *pdialog;

  destroy_message_dialog(w);

  if((punit=unit_list_find(&game.player_ptr->units, (size_t)client_data)))  {
    activate_unit(punit);
    if((pcity=map_get_city(punit->x, punit->y)))
      if((pdialog=get_city_dialog(pcity)))
	close_city_dialog(pdialog);
  }
}

/****************************************************************
...
*****************************************************************/
static void supported_units_activate_close_callback(Widget w,
						    XtPointer client_data, 
						    XtPointer call_data)
{
  struct unit *punit;
  struct city *pcity;
  struct city_dialog *pdialog;

  destroy_message_dialog(w);

  if((punit=unit_list_find(&game.player_ptr->units, (size_t)client_data)))  {
    activate_unit(punit);
    if((pcity=city_list_find_id(&game.player_ptr->cities, punit->homecity)))
      if((pdialog=get_city_dialog(pcity)))
	close_city_dialog(pdialog);
  }
}



/****************************************************************
...
*****************************************************************/
static void present_units_disband_callback(Widget w, XtPointer client_data, 
					   XtPointer call_data)
{
  struct unit *punit;

  if((punit=unit_list_find(&game.player_ptr->units, (size_t)client_data)))
    request_unit_disband(punit);

  destroy_message_dialog(w);
}


/****************************************************************
...
*****************************************************************/
static void present_units_homecity_callback(Widget w, XtPointer client_data, 
					    XtPointer call_data)
{
  struct unit *punit;
  
  if((punit=unit_list_find(&game.player_ptr->units, (size_t)client_data)))
    request_unit_change_homecity(punit);

  destroy_message_dialog(w);
}


/****************************************************************
...
*****************************************************************/
static void present_units_cancel_callback(Widget w, XtPointer client_data, 
					  XtPointer call_data)
{
  destroy_message_dialog(w);
}


/****************************************************************
...
*****************************************************************/
void present_units_callback(Widget w, XtPointer client_data, 
			    XtPointer call_data)
{
  struct unit *punit;
  struct city *pcity;
  struct city_dialog *pdialog;
  Widget wd;
  XEvent *e = (XEvent*)call_data;
  
  if((punit=unit_list_find(&game.player_ptr->units, (size_t)client_data)) &&
     (pcity=map_get_city(punit->x, punit->y)) &&
     (pdialog=get_city_dialog(pcity))) {
    
    if(e->type==ButtonRelease && e->xbutton.button==2)  {
      activate_unit(punit);
      close_city_dialog(pdialog);
      return;
    }

    wd=popup_message_dialog(pdialog->shell, 
			    "presentunitsdialog", 
			    unit_description(punit),
			    present_units_activate_callback, punit->id,
			    present_units_activate_close_callback, punit->id,
			    present_units_disband_callback, punit->id,
			    present_units_homecity_callback, punit->id,
			    upgrade_callback, punit->id,
			    present_units_cancel_callback, 0, 
			    NULL);
    if (can_upgrade_unittype(game.player_ptr,punit->type) == -1) {
      XtSetSensitive(XtNameToWidget(wd, "*button4"), FALSE);
    }
  }
}

/****************************************************************
...
*****************************************************************/
static void rename_city_callback(Widget w, XtPointer client_data, 
				 XtPointer call_data)
{
  struct city_dialog *pdialog;
  struct packet_city_request packet;

  if((pdialog=(struct city_dialog *)client_data)) {
    packet.city_id=pdialog->pcity->id;
    strncpy(packet.name, input_dialog_get_input(w), MAX_LEN_NAME);
    packet.name[MAX_LEN_NAME-1]='\0';
    send_packet_city_request(&aconnection, &packet, PACKET_CITY_RENAME);
  }
  input_dialog_destroy(w);
}





/****************************************************************
...
*****************************************************************/
void rename_callback(Widget w, XtPointer client_data, XtPointer call_data)
{
  struct city_dialog *pdialog;

  pdialog=(struct city_dialog *)client_data;
  
  input_dialog_create(pdialog->shell, 
		      "shellrenamecity", 
		      "What should we rename the city to?",
		      pdialog->pcity->name,
		      (void*)rename_city_callback, (XtPointer)pdialog,
		      (void*)rename_city_callback, (XtPointer)0);
}

/****************************************************************
...
*****************************************************************/
static void trade_message_dialog_callback(Widget w, XtPointer client_data, 
					  XtPointer call_data)
{
  destroy_message_dialog(w);
}

/****************************************************************
...
*****************************************************************/
void trade_callback(Widget w, XtPointer client_data, XtPointer call_data)
{
  int i;
  int x=0,total=0;
  char buf[512],*bptr=buf;
  struct city_dialog *pdialog;

  pdialog=(struct city_dialog *)client_data;

  sprintf(buf, "These trade routes have been established with %s:\n",
	  pdialog->pcity->name);
  bptr += strlen(bptr);
  
  for(i=0; i<4; i++)
    if(pdialog->pcity->trade[i]) {
      struct city *pcity;
      x=1;
      total+=pdialog->pcity->trade_value[i];
      if((pcity=find_city_by_id(pdialog->pcity->trade[i]))) {
       sprintf(bptr, "%32s: %2d Gold/Year\n",
               pcity->name, pdialog->pcity->trade_value[i]);
       bptr += strlen(bptr);
      } else {	
       sprintf(bptr, "%32s: %2d Gold/Year\n","Unknown",
               pdialog->pcity->trade_value[i]);
       bptr += strlen(bptr);
      }
    }
  if (!x)
    sprintf(bptr, "No trade routes exist.\n");
  else
    sprintf(bptr, "\nTotal trade %d Gold/Year\n",total);
  
  popup_message_dialog(pdialog->shell, 
		       "citytradedialog", 
		       buf, 
		       trade_message_dialog_callback, 0,
		       0);
}


/****************************************************************
...
*****************************************************************/
void city_dialog_update_pollution(struct city_dialog *pdialog)
{
  char buf[512];
  struct city *pcity=pdialog->pcity;

  sprintf(buf, "Pollution:   %3d", pcity->pollution);

  xaw_set_label(pdialog->pollution_label, buf);
}



/****************************************************************
...
*****************************************************************/
void city_dialog_update_storage(struct city_dialog *pdialog)
{
  char buf[512];
  struct city *pcity=pdialog->pcity;
  
  sprintf(buf, "Granary: %3d/%-3d", pcity->food_stock,
	  game.foodbox*pcity->size);

  xaw_set_label(pdialog->storage_label, buf);
}

/****************************************************************
...
*****************************************************************/
void city_dialog_update_building(struct city_dialog *pdialog)
{
  char buf[32], buf2[64];
  struct city *pcity=pdialog->pcity;
  
  XtSetSensitive(pdialog->buy_command, !pcity->did_buy);
  XtSetSensitive(pdialog->sell_command, !pcity->did_sell);

  if(pcity->is_building_unit) {
    sprintf(buf, "%3d/%3d", pcity->shield_stock, 
	    get_unit_type(pcity->currently_building)->build_cost);
    sprintf(buf2, "%s", get_unit_type(pcity->currently_building)->name);
  }
  else {
    if(pcity->currently_building==B_CAPITAL)  {
      /* Capitalization is special, you can't buy it or finish making it */
      sprintf(buf,"%3d/XXX", pcity->shield_stock);
      XtSetSensitive(pdialog->buy_command, False);
    } else {
      sprintf(buf, "%3d/%3d", pcity->shield_stock, 
	      get_improvement_type(pcity->currently_building)->build_cost);
    }
    sprintf(buf2, "%s", 
	    get_imp_name_ex(pcity, pcity->currently_building));
  }
    
  xaw_set_label(pdialog->building_label, buf2);
  xaw_set_label(pdialog->progress_label, buf);
}



/****************************************************************
...
*****************************************************************/
void city_dialog_update_production(struct city_dialog *pdialog)
{
  char buf[512];
  struct city *pcity=pdialog->pcity;
  
  sprintf(buf, "Food:    %2d (%+2d)\nProd:    %2d (%+2d)\nTrade:   %2d (%+2d)",
	  pcity->food_prod, pcity->food_surplus,
	  pcity->shield_prod, pcity->shield_surplus,
	  pcity->trade_prod+pcity->corruption, pcity->trade_prod);

  xaw_set_label(pdialog->production_label, buf);
}
/****************************************************************
...
*****************************************************************/
void city_dialog_update_output(struct city_dialog *pdialog)
{
  char buf[512];
  struct city *pcity=pdialog->pcity;
  
  sprintf(buf, "Gold:    %2d (%+2d)\nLuxury:  %2d\nScience: %2d",
	  pcity->tax_total, city_gold_surplus(pcity),
	  pcity->luxury_total,
	  pcity->science_total);

  xaw_set_label(pdialog->output_label, buf);
}


/****************************************************************
...
*****************************************************************/
void city_dialog_update_map(struct city_dialog *pdialog)
{
  int x, y;
  struct city *pcity=pdialog->pcity;
  
  for(y=0; y<CITY_MAP_SIZE; y++)
    for(x=0; x<CITY_MAP_SIZE; x++) {
      if(!(x==0 && y==0) && !(x==0 && y==CITY_MAP_SIZE-1) &&
	 !(x==CITY_MAP_SIZE-1 && y==0) && 
	 !(x==CITY_MAP_SIZE-1 && y==CITY_MAP_SIZE-1) &&
	 tile_is_known(pcity->x+x-CITY_MAP_SIZE/2, 
		       pcity->y+y-CITY_MAP_SIZE/2)) {
	pixmap_put_tile(XtWindow(pdialog->map_canvas), x, y, 
	                pcity->x+x-CITY_MAP_SIZE/2,
			pcity->y+y-CITY_MAP_SIZE/2, 1);
	if(pcity->city_map[x][y]==C_TILE_WORKER)
	  put_city_tile_output(XtWindow(pdialog->map_canvas), x, y, 
			       get_food_tile(x, y, pcity),
			       get_shields_tile(x, y, pcity), 
			       get_trade_tile(x, y, pcity) );
	else if(pcity->city_map[x][y]==C_TILE_UNAVAILABLE)
	  pixmap_frame_tile_red(XtWindow(pdialog->map_canvas), x, y);
      }
      else {
	pixmap_put_black_tile(XtWindow(pdialog->map_canvas), x, y);
      }
    }
}

/****************************************************************
...
*****************************************************************/
void city_dialog_update_citizens(struct city_dialog *pdialog)
{
  int i, n;
  struct city *pcity=pdialog->pcity;
    
  for(i=0, n=0; n<pcity->ppl_happy[4] && i<NUM_CITIZENS_SHOWN; n++, i++)
    if(pdialog->citizen_type[i]!=5 &&  pdialog->citizen_type[i]!=6) {
      pdialog->citizen_type[i]=5+i%2;
      xaw_set_bitmap(pdialog->citizen_labels[i], 
		     get_citizen_pixmap(pdialog->citizen_type[i]));
      XtSetSensitive(pdialog->citizen_labels[i], FALSE);
      XtRemoveAllCallbacks(pdialog->citizen_labels[i], XtNcallback);
    }

  for(n=0; n<pcity->ppl_content[4] && i<NUM_CITIZENS_SHOWN; n++, i++)
    if(pdialog->citizen_type[i]!=3 && pdialog->citizen_type[i]!=4) {
      pdialog->citizen_type[i]=3+i%2;
      xaw_set_bitmap(pdialog->citizen_labels[i], 
		     get_citizen_pixmap(pdialog->citizen_type[i]));
      XtSetSensitive(pdialog->citizen_labels[i], FALSE);
      XtRemoveAllCallbacks(pdialog->citizen_labels[i], XtNcallback);
    }
      
  for(n=0; n<pcity->ppl_unhappy[4] && i<NUM_CITIZENS_SHOWN; n++, i++)
    if(pdialog->citizen_type[i]!=7) {
      xaw_set_bitmap(pdialog->citizen_labels[i], get_citizen_pixmap(7));
      pdialog->citizen_type[i]=7;
      XtRemoveAllCallbacks(pdialog->citizen_labels[i], XtNcallback);
      XtSetSensitive(pdialog->citizen_labels[i], FALSE);
    }
      
  for(n=0; n<pcity->ppl_elvis && i<NUM_CITIZENS_SHOWN; n++, i++)
    if(pdialog->citizen_type[i]!=0) {
      xaw_set_bitmap(pdialog->citizen_labels[i], get_citizen_pixmap(0));
      pdialog->citizen_type[i]=0;
      XtRemoveAllCallbacks(pdialog->citizen_labels[i], XtNcallback);
      XtAddCallback(pdialog->citizen_labels[i], XtNcallback, elvis_callback,
		    (XtPointer)pdialog);
      XtSetSensitive(pdialog->citizen_labels[i], TRUE);
    }

  
  for(n=0; n<pcity->ppl_scientist && i<NUM_CITIZENS_SHOWN; n++, i++)
    if(pdialog->citizen_type[i]!=1) {
      xaw_set_bitmap(pdialog->citizen_labels[i], get_citizen_pixmap(1));
      pdialog->citizen_type[i]=1;
      XtRemoveAllCallbacks(pdialog->citizen_labels[i], XtNcallback);
      XtAddCallback(pdialog->citizen_labels[i], XtNcallback, scientist_callback,
		    (XtPointer)pdialog);
      XtSetSensitive(pdialog->citizen_labels[i], TRUE);
    }
  
  for(n=0; n<pcity->ppl_taxman && i<NUM_CITIZENS_SHOWN; n++, i++)
    if(pdialog->citizen_type[i]!=2) {
      xaw_set_bitmap(pdialog->citizen_labels[i], get_citizen_pixmap(2));
      pdialog->citizen_type[i]=2;
      XtRemoveAllCallbacks(pdialog->citizen_labels[i], XtNcallback);
      XtAddCallback(pdialog->citizen_labels[i], XtNcallback, taxman_callback,
		    (XtPointer)pdialog);
      XtSetSensitive(pdialog->citizen_labels[i], TRUE);
    }
  
  for(; i<NUM_CITIZENS_SHOWN; i++) {
    xaw_set_bitmap(pdialog->citizen_labels[i], None);
    XtSetSensitive(pdialog->citizen_labels[i], FALSE);
    XtRemoveAllCallbacks(pdialog->citizen_labels[i], XtNcallback);
  }
}

/****************************************************************
...
*****************************************************************/
static void support_units_callback(Widget w, XtPointer client_data, 
				   XtPointer call_data)
{
  struct unit *punit;
  struct city *pcity;
  struct city_dialog *pdialog;
  XEvent *e = (XEvent*)call_data;
  
  if((punit=unit_list_find(&game.player_ptr->units, (size_t)client_data)))
    if((pcity=find_city_by_id(punit->homecity)))
      if((pdialog=get_city_dialog(pcity)))  {
	if(e->type==ButtonRelease && e->xbutton.button==2)  {
	  activate_unit(punit);
	  close_city_dialog(pdialog);
	  return;
	}
	popup_message_dialog(pdialog->shell,
			     "supportunitsdialog", 
			     unit_description(punit),
			     present_units_activate_callback, punit->id,
			     supported_units_activate_close_callback, punit->id, /* act+c */
			     present_units_disband_callback, punit->id,
			     present_units_cancel_callback, 0, 0);
      }
}

/****************************************************************
...
*****************************************************************/
void city_dialog_update_supported_units(struct city_dialog *pdialog, 
					int unitid)
{
  int i;
  struct genlist_iterator myiter;
  struct unit *punit;

  if(unitid) {
    for(i=0; i<NUM_UNITS_SHOWN; i++)
      if(pdialog->support_unit_ids[i]==unitid)
	break;
    if(i==NUM_UNITS_SHOWN)
      unitid=0;
  }
  
  genlist_iterator_init(&myiter, &pdialog->pcity->units_supported.list, 0);

  for(i=0; i<NUM_UNITS_SHOWN && ITERATOR_PTR(myiter); ITERATOR_NEXT(myiter), i++) {
    punit=(struct unit*)ITERATOR_PTR(myiter);
        
    if(unitid && punit->id!=unitid)
      continue;
    if (flags_are_transparent)
      XawPixcommClear(pdialog->support_unit_pixcomms[i]); /* STG */
    put_unit_pixmap(punit, XawPixcommPixmap(pdialog->support_unit_pixcomms[i]), 
		    0, 0);

    
    put_unit_pixmap_city_overlays(punit, 
				  XawPixcommPixmap(pdialog->support_unit_pixcomms[i]),
				  punit->unhappiness, punit->upkeep);   
    xaw_expose_now(pdialog->support_unit_pixcomms[i]);
    pdialog->support_unit_ids[i]=punit->id;
    
    XtRemoveAllCallbacks(pdialog->support_unit_pixcomms[i], XtNcallback);
    XtAddCallback(pdialog->support_unit_pixcomms[i], XtNcallback, 
		  support_units_callback, (XtPointer)punit->id);
    XtSetSensitive(pdialog->support_unit_pixcomms[i], TRUE);
  }
    
  for(; i<NUM_UNITS_SHOWN; i++) {
    XawPixcommClear(pdialog->support_unit_pixcomms[i]);
    pdialog->support_unit_ids[i]=0;
    XtSetSensitive(pdialog->support_unit_pixcomms[i], FALSE);
  }
}

/****************************************************************
...
*****************************************************************/
void city_dialog_update_present_units(struct city_dialog *pdialog, int unitid)
{
  int i;
  struct genlist_iterator myiter;
  struct unit *punit;
  
  if(unitid) {
    for(i=0; i<NUM_UNITS_SHOWN; i++)
      if(pdialog->present_unit_ids[i]==unitid)
	break;
    if(i==NUM_UNITS_SHOWN)
      unitid=0;
  }

  genlist_iterator_init(&myiter, 
	&map_get_tile(pdialog->pcity->x, pdialog->pcity->y)->units.list, 0);
  
  for(i=0; i<NUM_UNITS_SHOWN && ITERATOR_PTR(myiter); ITERATOR_NEXT(myiter), i++) {
    punit=(struct unit*)ITERATOR_PTR(myiter);
    
    if(unitid && punit->id!=unitid)
      continue;

    if (flags_are_transparent)
      XawPixcommClear(pdialog->present_unit_pixcomms[i]); /* STG */

    put_unit_pixmap(punit, XawPixcommPixmap(pdialog->present_unit_pixcomms[i]),
		    0, 0);
    put_unit_pixmap_city_overlays(punit, 
			  XawPixcommPixmap(pdialog->present_unit_pixcomms[i]),
				  0,0);
 
    xaw_expose_now(pdialog->present_unit_pixcomms[i]);
    pdialog->present_unit_ids[i]=punit->id;

    XtRemoveAllCallbacks(pdialog->present_unit_pixcomms[i], XtNcallback);
    XtAddCallback(pdialog->present_unit_pixcomms[i], XtNcallback, 
		  present_units_callback, (XtPointer)punit->id);
    XtSetSensitive(pdialog->present_unit_pixcomms[i], TRUE);
  }

  for(; i<NUM_UNITS_SHOWN; i++) {
    XawPixcommClear(pdialog->present_unit_pixcomms[i]);
    pdialog->present_unit_ids[i]=0;
    XtSetSensitive(pdialog->present_unit_pixcomms[i], FALSE);
  }
}

/****************************************************************
...
*****************************************************************/
void city_dialog_update_title(struct city_dialog *pdialog)
{
  char buf[512];
  String now;
  
  sprintf(buf, "%s - %s citizens",
	  pdialog->pcity->name, int_to_text(city_population(pdialog->pcity)));

  XtVaGetValues(pdialog->cityname_label, XtNlabel, &now, NULL);
  if(strcmp(now, buf)) {
    XtVaSetValues(pdialog->cityname_label, XtNlabel, (XtArgVal)buf, NULL);
    xaw_horiz_center(pdialog->cityname_label);
  }
}

/****************************************************************
...
*****************************************************************/
void city_dialog_update_improvement_list(struct city_dialog *pdialog)
{
  int i, n, flag;

  for(i=0, n=0, flag=0; i<B_LAST; ++i)
    if(pdialog->pcity->improvements[i]) {
      if(!pdialog->improvlist_names_ptrs[n] ||
	 strcmp(pdialog->improvlist_names_ptrs[n], get_imp_name_ex(pdialog->pcity, i)))
	flag=1;
      strcpy(pdialog->improvlist_names[n], get_imp_name_ex(pdialog->pcity, i));
      pdialog->improvlist_names_ptrs[n]=pdialog->improvlist_names[n];
      n++;
    }
  
  if(pdialog->improvlist_names_ptrs[n]!=0) {
    pdialog->improvlist_names_ptrs[n]=0;
    flag=1;
  }
  
  if(flag || n==0) {
    XawListChange(pdialog->improvement_list, pdialog->improvlist_names_ptrs, 
		  n, 0, False);  
    /* force refresh of viewport so the scrollbar is added.
     * Buggy sun athena requires this */
    XtVaSetValues(pdialog->improvement_viewport, XtNforceBars, False, NULL);
    XtVaSetValues(pdialog->improvement_viewport, XtNforceBars, True, NULL);
  }
}


/**************************************************************************
...
**************************************************************************/
void button_down_citymap(Widget w, XEvent *event, String *argv, Cardinal *argc)
{
  XButtonEvent *ev=&event->xbutton;
  struct genlist_iterator myiter;
  struct city *pcity;

  genlist_iterator_init(&myiter, &dialog_list, 0);
    
  for(; ITERATOR_PTR(myiter); ITERATOR_NEXT(myiter))
    if(((struct city_dialog *)ITERATOR_PTR(myiter))->map_canvas==w)
      break;

  if((pcity=((struct city_dialog *)ITERATOR_PTR(myiter))->pcity)) {
    int xtile, ytile;
    struct packet_city_request packet;

    xtile=ev->x/NORMAL_TILE_WIDTH;
    ytile=ev->y/NORMAL_TILE_HEIGHT;
    packet.city_id=pcity->id;
    packet.worker_x=xtile;
    packet.worker_y=ytile;
    packet.name[0]='\0';
    
    if(pcity->city_map[xtile][ytile]==C_TILE_WORKER)
      send_packet_city_request(&aconnection, &packet, 
			       PACKET_CITY_MAKE_SPECIALIST);
    else if(pcity->city_map[xtile][ytile]==C_TILE_EMPTY)
      send_packet_city_request(&aconnection, &packet, PACKET_CITY_MAKE_WORKER);
  }
}

/****************************************************************
...
*****************************************************************/
void elvis_callback(Widget w, XtPointer client_data, XtPointer call_data)
{
  struct city_dialog *pdialog;
  struct packet_city_request packet;
  
  pdialog=(struct city_dialog *)client_data;

  packet.city_id=pdialog->pcity->id;
  packet.name[0]='\0';
  packet.specialist_from=SP_ELVIS;
  packet.specialist_to=SP_SCIENTIST;
  
  send_packet_city_request(&aconnection, &packet, 
			   PACKET_CITY_CHANGE_SPECIALIST);
}

/****************************************************************
...
*****************************************************************/
void scientist_callback(Widget w, XtPointer client_data, XtPointer call_data)
{
  struct city_dialog *pdialog;
  struct packet_city_request packet;
  
  pdialog=(struct city_dialog *)client_data;

  packet.city_id=pdialog->pcity->id;
  packet.name[0]='\0';
  packet.specialist_from=SP_SCIENTIST;
  packet.specialist_to=SP_TAXMAN;
  
  send_packet_city_request(&aconnection, &packet, 
			   PACKET_CITY_CHANGE_SPECIALIST);
}

/****************************************************************
...
*****************************************************************/
void taxman_callback(Widget w, XtPointer client_data, XtPointer call_data)
{
  struct city_dialog *pdialog;
  struct packet_city_request packet;
  
  pdialog=(struct city_dialog *)client_data;

  packet.city_id=pdialog->pcity->id;
  packet.name[0]='\0';
  packet.specialist_from=SP_TAXMAN;
  packet.specialist_to=SP_ELVIS;
  
  send_packet_city_request(&aconnection, &packet, 
			   PACKET_CITY_CHANGE_SPECIALIST);
}

/****************************************************************
...
*****************************************************************/
static void buy_callback_yes(Widget w, XtPointer client_data,
			     XtPointer call_data)
{
  struct city_dialog *pdialog;
  struct packet_city_request packet;

  pdialog=(struct city_dialog *)client_data;

  packet.city_id=pdialog->pcity->id;
  packet.name[0]='\0';
  send_packet_city_request(&aconnection, &packet, PACKET_CITY_BUY);

  destroy_message_dialog(w);
}


/****************************************************************
...
*****************************************************************/
static void buy_callback_no(Widget w, XtPointer client_data,
			    XtPointer call_data)
{
  destroy_message_dialog(w);
}


/****************************************************************
...
*****************************************************************/
void buy_callback(Widget w, XtPointer client_data, XtPointer call_data)
{
  struct city_dialog *pdialog;
  int value;
  char *name;
  char buf[512];
  
  pdialog=(struct city_dialog *)client_data;

  if(pdialog->pcity->is_building_unit) {
    name=get_unit_type(pdialog->pcity->currently_building)->name;
  }
  else {
    name=get_imp_name_ex(pdialog->pcity, pdialog->pcity->currently_building);
  }
  value=city_buy_cost(pdialog->pcity);
 
  if(game.player_ptr->economic.gold>=value) {
    sprintf(buf, "Buy %s for %d gold?\nTreasury contains %d gold.", 
	    name, value, game.player_ptr->economic.gold);
    popup_message_dialog(pdialog->shell, "buydialog", buf,
			 buy_callback_yes, pdialog,
			 buy_callback_no, 0, 0);
  }
  else {
    sprintf(buf, "%s costs %d gold.\nTreasury contains %d gold.", 
	    name, value, game.player_ptr->economic.gold);
    popup_message_dialog(pdialog->shell, "buynodialog", buf,
			 buy_callback_no, 0, 0);
  }
  
}


/****************************************************************
...
*****************************************************************/
void unitupgrade_callback_yes(Widget w, XtPointer client_data, XtPointer call_data)
{
  struct unit *punit;

  if((punit=unit_list_find(&game.player_ptr->units, (size_t)client_data))) {
    request_unit_upgrade(punit);
  }
  destroy_message_dialog(w);
}


/****************************************************************
...
*****************************************************************/
void unitupgrade_callback_no(Widget w, XtPointer client_data, XtPointer call_data)
{
  destroy_message_dialog(w);
}


/****************************************************************
...
*****************************************************************/
void upgrade_callback(Widget w, XtPointer client_data, XtPointer call_data)
{
  struct unit *punit;
  char buf[512];
  int ut1,ut2;
  int value;

  if((punit=unit_list_find(&game.player_ptr->units, (size_t)client_data))) {
    ut1 = punit->type;
    /* printf("upgrade_callback for %s\n", unit_types[ut1].name); */

    ut2 = can_upgrade_unittype(game.player_ptr,ut1);

    if ( ut2 == -1 ) {
      /* this shouldn't generally happen, but it is conceivable */
      sprintf(buf, "Sorry: cannot upgrade %s.", unit_types[ut1].name);
      popup_message_dialog(toplevel, "upgradenodialog", buf,
                           unitupgrade_callback_no, 0,
                           NULL);
    } else {
      value=unit_upgrade_price(game.player_ptr, ut1, ut2);

      if(game.player_ptr->economic.gold>=value) {
        sprintf(buf, "Upgrade %s to %s for %d gold?\n"
		"Treasury contains %d gold.",
		unit_types[ut1].name, unit_types[ut2].name,
		value, game.player_ptr->economic.gold);
        popup_message_dialog(toplevel, "upgradedialog", buf,
                             unitupgrade_callback_yes, (XtPointer)(punit->id),
                             unitupgrade_callback_no, 0,
                             NULL);
      } else {
        sprintf(buf, "Upgrading %s to %s costs %d gold.\n"
		"Treasury contains %d gold.",
		unit_types[ut1].name, unit_types[ut2].name,
		value, game.player_ptr->economic.gold);
        popup_message_dialog(toplevel, "upgradenodialog", buf,
                             unitupgrade_callback_no, 0,
                             NULL);
      }
    }
    destroy_message_dialog(w);
  }
}


/****************************************************************
...
*****************************************************************/
static void change_to_callback(Widget w, XtPointer client_data,
			       XtPointer call_data)
{
  struct city_dialog *pdialog;
  XawListReturnStruct *ret;

  pdialog=(struct city_dialog *)client_data;

  ret=XawListShowCurrent(pdialog->change_list);

  if(ret->list_index!=XAW_LIST_NONE) {
    struct packet_city_request packet;
  
    packet.city_id=pdialog->pcity->id;
    packet.name[0]='\0';
    packet.build_id=pdialog->change_list_ids[ret->list_index];
    packet.is_build_id_unit_id=
      (ret->list_index >= pdialog->change_list_num_improvements);
    
    send_packet_city_request(&aconnection, &packet, PACKET_CITY_CHANGE);
  }
  
  XtDestroyWidget(XtParent(XtParent(w)));
  XtSetSensitive(pdialog->shell, TRUE);
}

/****************************************************************
...
*****************************************************************/
static void change_no_callback(Widget w, XtPointer client_data,
			       XtPointer call_data)
{
  struct city_dialog *pdialog;
  
  pdialog=(struct city_dialog *)client_data;
  
  XtDestroyWidget(XtParent(XtParent(w)));
  XtSetSensitive(pdialog->shell, TRUE);
}

/****************************************************************
...
*****************************************************************/
static void change_help_callback(Widget w, XtPointer client_data,
				 XtPointer call_data)
{
  struct city_dialog *pdialog;
  XawListReturnStruct *ret;

  pdialog=(struct city_dialog *)client_data;

  ret=XawListShowCurrent(pdialog->change_list);
  if(ret->list_index!=XAW_LIST_NONE) {
    int idx = pdialog->change_list_ids[ret->list_index];
    int is_unit = (ret->list_index >= pdialog->change_list_num_improvements);

    if (is_unit) {
      popup_help_dialog_typed(get_unit_type(idx)->name, HELP_UNIT);
    } else if(is_wonder(idx)) {
      popup_help_dialog_typed(get_improvement_name(idx), HELP_WONDER);
    } else {
      popup_help_dialog_typed(get_improvement_name(idx), HELP_IMPROVEMENT);
    }
  }
  else
    popup_help_dialog_string(HELP_IMPROVEMENTS_ITEM);
}


/****************************************************************
...
*****************************************************************/
void change_callback(Widget w, XtPointer client_data, XtPointer call_data)
{
  Widget cshell, cform, clabel, cview, button_change, button_cancel, button_help;
  Position x, y;
  Dimension width, height;
  struct city_dialog *pdialog;
  int i, n;
  
  pdialog=(struct city_dialog *)client_data;
  
  cshell=XtCreatePopupShell("changedialog", transientShellWidgetClass,
			    pdialog->shell, NULL, 0);
  
  cform=XtVaCreateManagedWidget("dform", formWidgetClass, cshell, NULL);
  
  clabel=XtVaCreateManagedWidget("dlabel", labelWidgetClass, cform, 
				 NULL);   

  cview=XtVaCreateManagedWidget("dview", viewportWidgetClass,
				cform,
				XtNfromVert, 
				clabel,
				NULL);

  pdialog->change_list=XtVaCreateManagedWidget("dlist", listWidgetClass, 
					       cview, 
					       XtNforceColumns, 1,
					       XtNdefaultColumns,1, 
					       XtNlist, 
					       (XtArgVal)dummy_change_list,
					       XtNverticalList, False,
					       NULL);

  
  button_change=XtVaCreateManagedWidget("buttonchange",
					commandWidgetClass,
					cform,
					XtNfromVert, 
					cview,
					NULL);

  button_cancel=XtVaCreateManagedWidget("buttoncancel",
				    commandWidgetClass,
				    cform,
				    XtNfromVert, 
				    cview,
				    XtNfromHoriz,
				    button_change,
				    NULL);

  button_help = XtVaCreateManagedWidget("buttonhelp",
				    commandWidgetClass,
				    cform,
				    XtNfromVert, 
				    cview,
				    XtNfromHoriz,
				    button_cancel,
				    NULL);

  XtAddCallback(button_change, XtNcallback, 
		change_to_callback, (XtPointer)pdialog);
  XtAddCallback(button_cancel, XtNcallback, 
		change_no_callback, (XtPointer)pdialog);
  XtAddCallback(button_help, XtNcallback, 
		change_help_callback, (XtPointer)pdialog);

  

  XtVaGetValues(pdialog->shell, XtNwidth, &width, XtNheight, &height, NULL);
  XtTranslateCoords(pdialog->shell, (Position) width/3, (Position) height/3,
		    &x, &y);
  XtVaSetValues(cshell, XtNx, x, XtNy, y, NULL);
  
  XtPopup(cshell, XtGrabNone);
  
  XtSetSensitive(pdialog->shell, FALSE);

  for(i=0, n=0; i<B_LAST; i++)
    if(can_build_improvement(pdialog->pcity, i)) {
      sprintf(pdialog->change_list_names[n], "%s (%d)", get_imp_name_ex(pdialog->pcity, i),get_improvement_type(i)->build_cost);
      
      pdialog->change_list_names_ptrs[n]=pdialog->change_list_names[n];
      pdialog->change_list_ids[n++]=i;
    }
  
  pdialog->change_list_num_improvements=n;


  for(i=0; i<U_LAST; i++)
    if(can_build_unit(pdialog->pcity, i)) {
      sprintf(pdialog->change_list_names[n],"%s (%d)", get_unit_name(i), get_unit_type(i)->build_cost);
      pdialog->change_list_names_ptrs[n]=pdialog->change_list_names[n];
      pdialog->change_list_ids[n++]=i;
    }
  
  pdialog->change_list_names_ptrs[n]=0;

  XawListChange(pdialog->change_list, pdialog->change_list_names_ptrs, 
		0, 0, False);
  /* force refresh of viewport so the scrollbar is added.
   * Buggy sun athena requires this */
  XtVaSetValues(cview, XtNforceBars, True, NULL);
}


/****************************************************************
...
*****************************************************************/
static void sell_callback_yes(Widget w, XtPointer client_data,
			      XtPointer call_data)
{
  struct city_dialog *pdialog;
  struct packet_city_request packet;

  pdialog=(struct city_dialog *)client_data;

  packet.city_id=pdialog->pcity->id;
  packet.build_id=pdialog->sell_id;
  packet.name[0]='\0';
  send_packet_city_request(&aconnection, &packet, PACKET_CITY_SELL);

  destroy_message_dialog(w);
}


/****************************************************************
...
*****************************************************************/
static void sell_callback_no(Widget w, XtPointer client_data,
			     XtPointer call_data)
{
  destroy_message_dialog(w);
}


/****************************************************************
...
*****************************************************************/
void sell_callback(Widget w, XtPointer client_data, XtPointer call_data)
{
  struct city_dialog *pdialog;
  XawListReturnStruct *ret;
  
  pdialog=(struct city_dialog *)client_data;

  ret=XawListShowCurrent(pdialog->improvement_list);

  if(ret->list_index!=XAW_LIST_NONE) {
    int i, n;
    for(i=0, n=0; i<B_LAST; i++)
      if(pdialog->pcity->improvements[i]) {
	if(n==ret->list_index) {
	  char buf[512];
	  
	  if(is_wonder(i))
	    return;
	  
	  pdialog->sell_id=i;
	  sprintf(buf, "Sell %s for %d gold?", 
		  get_imp_name_ex(pdialog->pcity, i),
		  improvement_value(i));

	  popup_message_dialog(pdialog->shell, "selldialog", buf,
			       sell_callback_yes, pdialog,
			       sell_callback_no, pdialog, 0);
	  
	  return;
	}
	n++;
      }
  }
}

/****************************************************************
...
*****************************************************************/
void close_city_dialog(struct city_dialog *pdialog)
{
  XtDestroyWidget(pdialog->shell);
  genlist_unlink(&dialog_list, pdialog);

  if(pdialog->is_modal)
    XtSetSensitive(toplevel, TRUE);
  free(pdialog);
}

/****************************************************************
...
*****************************************************************/
void city_dialog_returnkey(Widget w, XEvent *event, String *params, Cardinal *num_params)
{
  close_city_dialog_action(XtParent(XtParent(w)), 0, 0, 0);
}

/****************************************************************
...
*****************************************************************/
void close_city_dialog_action(Widget w, XEvent *event, String *argv, Cardinal *argc)
{
  struct genlist_iterator myiter;

  genlist_iterator_init(&myiter, &dialog_list, 0);
  for(; ITERATOR_PTR(myiter); ITERATOR_NEXT(myiter))
    if(((struct city_dialog *)ITERATOR_PTR(myiter))->shell==w) {
      close_city_dialog((struct city_dialog *)ITERATOR_PTR(myiter));
      return;
    }
}


/****************************************************************
...
*****************************************************************/
void close_callback(Widget w, XtPointer client_data, XtPointer call_data)
{
  close_city_dialog((struct city_dialog *)client_data);
}


/****************************************************************
								 
 City Options dialog:  (current only auto-attack options)
 
Note, there can only be one such dialog at a time, because
I'm lazy.  That could be fixed, similar to way you can have
multiple city dialogs.

triggle = tri_toggle (three way toggle button)

*****************************************************************/
								  
#define NUM_CITYOPT_TOGGLES 5

Widget create_cityopt_dialog(char *city_name);
void cityopt_ok_command_callback(Widget w, XtPointer client_data, 
				XtPointer call_data);
void cityopt_cancel_command_callback(Widget w, XtPointer client_data, 
				    XtPointer call_data);
void cityopt_newcit_triggle_callback(Widget w, XtPointer client_data,
					XtPointer call_data);

char *newcitizen_labels[] = { "Workers", "Scientists", "Taxmen" };

static Widget cityopt_shell = 0;
static Widget cityopt_triggle;
static Widget cityopt_toggles[NUM_CITYOPT_TOGGLES];
static int cityopt_city_id = 0;
static int newcitizen_index;

/****************************************************************
...
*****************************************************************/
void cityopt_callback(Widget w, XtPointer client_data,
                        XtPointer call_data)
{
  struct city_dialog *pdialog = (struct city_dialog *)client_data;
  struct city *pcity = pdialog->pcity;
  int i, state;

  if(cityopt_shell) {
    XtDestroyWidget(cityopt_shell);
  }
  cityopt_shell=create_cityopt_dialog(pcity->name);
  /* Doing this here makes the "No"'s centered consistently */
  for(i=0; i<NUM_CITYOPT_TOGGLES; i++) {
    state = (pcity->city_options & (1<<i));
    XtVaSetValues(cityopt_toggles[i], XtNstate, state,
		  XtNlabel, state?"Yes":"No", NULL);
  }
  if (pcity->city_options & (1<<CITYO_NEW_EINSTEIN)) {
    newcitizen_index = 1;
  } else if (pcity->city_options & (1<<CITYO_NEW_TAXMAN)) {
    newcitizen_index = 2;
  } else {
    newcitizen_index = 0;
  }
  XtVaSetValues(cityopt_triggle, XtNstate, 1,
		XtNlabel, newcitizen_labels[newcitizen_index],
		NULL);
  
  cityopt_city_id = pcity->id;

  xaw_set_relative_position(toplevel, cityopt_shell, 15, 15);
  XtPopup(cityopt_shell, XtGrabNone);
}


/**************************************************************************
...
**************************************************************************/
Widget create_cityopt_dialog(char *city_name)
{
  Widget shell, form, label, ok, cancel;
  int i;

  shell = XtCreatePopupShell("cityoptpopup",
			     transientShellWidgetClass,
			     toplevel, NULL, 0);
  form = XtVaCreateManagedWidget("cityoptform", 
				 formWidgetClass, 
				 shell, NULL);
  label = XtVaCreateManagedWidget("cityoptlabel", labelWidgetClass,
				  form, XtNlabel, city_name, NULL);


  XtVaCreateManagedWidget("cityoptnewcitlabel",
			  labelWidgetClass, 
			  form, NULL);
  cityopt_triggle = XtVaCreateManagedWidget("cityoptnewcittriggle", 
					    toggleWidgetClass, 
					    form, NULL);

  /* NOTE: the ordering here is deliberately out of order;
     want toggles[] to be in enum city_options order, but
     want display in different order. --dwp
     - disband and workers options at top
     - helicopters (special case air) at bottom
  */

  XtVaCreateManagedWidget("cityoptdisbandlabel",
			  labelWidgetClass, 
			  form, NULL);
  cityopt_toggles[4] = XtVaCreateManagedWidget("cityoptdisbandtoggle", 
					      toggleWidgetClass, 
					      form, NULL);

  XtVaCreateManagedWidget("cityoptvlandlabel", 
			  labelWidgetClass, 
			  form, NULL);
  cityopt_toggles[0] = XtVaCreateManagedWidget("cityoptvlandtoggle", 
					      toggleWidgetClass, 
					      form, NULL);
  XtVaCreateManagedWidget("cityoptvsealabel", 
			  labelWidgetClass, 
			  form, NULL);
  cityopt_toggles[1] = XtVaCreateManagedWidget("cityoptvseatoggle", 
					      toggleWidgetClass, 
					      form, NULL);
  XtVaCreateManagedWidget("cityoptvairlabel", 
			  labelWidgetClass, 
			  form, NULL);
  cityopt_toggles[3] = XtVaCreateManagedWidget("cityoptvairtoggle", 
					      toggleWidgetClass, 
					      form, NULL);
  XtVaCreateManagedWidget("cityoptvhelilabel", 
			  labelWidgetClass, 
			  form, NULL);
  cityopt_toggles[2] = XtVaCreateManagedWidget("cityoptvhelitoggle", 
					      toggleWidgetClass, 
					      form, NULL);

  ok = XtVaCreateManagedWidget("cityoptokcommand", 
			       commandWidgetClass,
			       form, NULL);
  cancel = XtVaCreateManagedWidget("cityoptcancelcommand", 
				   commandWidgetClass,
				   form, NULL);

  XtAddCallback(ok, XtNcallback, cityopt_ok_command_callback, 
                (XtPointer)shell);
  XtAddCallback(cancel, XtNcallback, cityopt_cancel_command_callback, 
                (XtPointer)shell);
  for(i=0; i<NUM_CITYOPT_TOGGLES; i++) {
    XtAddCallback(cityopt_toggles[i], XtNcallback, toggle_callback, NULL);
  }
  XtAddCallback(cityopt_triggle, XtNcallback,
		cityopt_newcit_triggle_callback, NULL);

  XtRealizeWidget(shell);

  xaw_horiz_center(label);
  return shell;
}
  
/**************************************************************************
...
**************************************************************************/
void cityopt_cancel_command_callback(Widget w, XtPointer client_data, 
				    XtPointer call_data)
{
  XtDestroyWidget(cityopt_shell);
  cityopt_shell = 0;
}

/**************************************************************************
...
**************************************************************************/
void cityopt_ok_command_callback(Widget w, XtPointer client_data, 
				XtPointer call_data)
{
  struct city *pcity = find_city_by_id(cityopt_city_id);

  if (pcity) {
    struct packet_generic_values packet;
    int i, new;
    Boolean b;
    
    new = 0;
    for(i=0; i<NUM_CITYOPT_TOGGLES; i++)  {
      XtVaGetValues(cityopt_toggles[i], XtNstate, &b, NULL);
      if (b) new |= (1<<i);
    }
    if (newcitizen_index == 1) {
      new |= (1<<CITYO_NEW_EINSTEIN);
    } else if (newcitizen_index == 2) {
      new |= (1<<CITYO_NEW_TAXMAN);
    }
    packet.value1 = cityopt_city_id;
    packet.value2 = new;
    send_packet_generic_values(&aconnection, PACKET_CITY_OPTIONS,
			       &packet);
  }
  XtDestroyWidget(cityopt_shell);
  cityopt_shell = 0;
}

/**************************************************************************
 Changes the label of the toggle widget to between newcitizen_labels
 and increments (mod 3) newcitizen_index.
**************************************************************************/
void cityopt_newcit_triggle_callback(Widget w, XtPointer client_data,
					XtPointer call_data)
{
  newcitizen_index++;
  if (newcitizen_index>=3) {
    newcitizen_index = 0;
  }
  XtVaSetValues(cityopt_triggle, XtNstate, 1,
		XtNlabel, newcitizen_labels[newcitizen_index],
		NULL);
}

/**************************************************************************
...
**************************************************************************/
void popdown_cityopt_dialog(void)
{
  if(cityopt_shell) {
    XtDestroyWidget(cityopt_shell);
    cityopt_shell = 0;
  }
}

