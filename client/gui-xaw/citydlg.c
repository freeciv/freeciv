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

#include <X11/Intrinsic.h>
#include <X11/StringDefs.h>
#include <X11/Xaw/Form.h>
#include <X11/Xaw/Label.h>
#include <X11/Xaw/Command.h>
#include <X11/Xaw/MenuButton.h>
#include <X11/Xaw/SimpleMenu.h>
#include <X11/Xaw/SmeBSB.h>
#include <X11/Xaw/Command.h>
#include <X11/Xaw/List.h>
#include <X11/Xaw/Viewport.h>
#include <X11/Xaw/AsciiText.h>  
#include <X11/Xaw/Toggle.h>     
#include <X11/IntrinsicP.h>

#include "pixcomm.h"
#include "canvas.h"

#include "city.h"
#include "fcintl.h"
#include "game.h"
#include "genlist.h"
#include "map.h"
#include "mem.h"
#include "packets.h"
#include "player.h"
#include "shared.h"
#include "support.h"

#include "cityrep.h"
#include "clinet.h"
#include "colors.h"
#include "control.h" /* request_xxx and set_unit_focus */
#include "dialogs.h"
#include "graphics.h"
#include "gui_main.h"
#include "gui_stuff.h"
#include "helpdlg.h"
#include "inputdlg.h"
#include "mapctrl.h"
#include "mapview.h"
#include "optiondlg.h"		/* for toggle_callback */
#include "options.h"
#include "repodlgs.h"
#include "tilespec.h"
#include "wldlg.h"
#include "climisc.h"

#include "citydlg.h"

#include "cityicon.ico"

#define MIN_NUM_CITIZENS	22
#define MAX_NUM_CITIZENS	50
#define DEFAULT_NUM_CITIZENS	38
#define MIN_NUM_UNITS		8
#define MAX_NUM_UNITS		20
#define DEFAULT_NUM_UNITS	11

struct city_dialog {
  struct city *pcity;

  int num_citizens_shown;
  int num_units_shown;

  Widget shell;
  Widget main_form;
  Widget left_form;
  Widget cityname_label;
  Widget *citizen_labels;
  Widget production_label;
  Widget output_label;
  Widget storage_label;
  Widget pollution_label;
  Widget sub_form;
  Widget map_canvas;
  Widget sell_command;
  Widget close_command, rename_command, trade_command, activate_command;
  Widget show_units_command, cityopt_command;
  Widget building_label, progress_label, buy_command, change_command,
    worklist_command, worklist_label;
  Widget improvement_viewport, improvement_list;
  Widget support_unit_label;
  Widget *support_unit_pixcomms;
  Widget support_unit_next_command;
  Widget support_unit_prev_command;
  Widget present_unit_label;
  Widget *present_unit_pixcomms;
  Widget present_unit_next_command;
  Widget present_unit_prev_command;
  Widget change_list;
  Widget rename_input;
  Widget worklist_shell;
  
  Impr_Type_id sell_id;
  
  int *citizen_type;
  int support_unit_base;
  int present_unit_base;
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


static struct city_dialog *get_city_dialog(struct city *pcity);
static struct city_dialog *create_city_dialog(struct city *pcity, int make_modal);
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

static void sell_callback(Widget w, XtPointer client_data, XtPointer call_data);
static void buy_callback(Widget w, XtPointer client_data, XtPointer call_data);
static void change_callback(Widget w, XtPointer client_data, XtPointer call_data);
static void worklist_callback(Widget w, XtPointer client_data, XtPointer call_data);
void commit_city_worklist(struct worklist *pwl, void *data);
void cancel_city_worklist(void *data);
static void close_callback(Widget w, XtPointer client_data, XtPointer call_data);
static void rename_callback(Widget w, XtPointer client_data, XtPointer call_data);
static void trade_callback(Widget w, XtPointer client_data, XtPointer call_data);
static void activate_callback(Widget w, XtPointer client_data, XtPointer call_data);
static void show_units_callback(Widget W, XtPointer client_data, XtPointer call_data);
static void units_next_prev_callback(Widget W, XtPointer client_data,
				     XtPointer call_data);
static void unitupgrade_callback_yes(Widget w, XtPointer client_data,
				     XtPointer call_data);
static void unitupgrade_callback_no(Widget w, XtPointer client_data,
				    XtPointer call_data);
static void upgrade_callback(Widget w, XtPointer client_data, XtPointer call_data);

static void elvis_callback(Widget w, XtPointer client_data, XtPointer call_data);
static void scientist_callback(Widget w, XtPointer client_data, XtPointer call_data);
static void taxman_callback(Widget w, XtPointer client_data, XtPointer call_data);

static void present_units_callback(Widget w, XtPointer client_data, 
				   XtPointer call_data);
static void cityopt_callback(Widget w, XtPointer client_data, 
			     XtPointer call_data);
static void popdown_cityopt_dialog(void);

/****************************************************************
...
*****************************************************************/
static void get_contents_of_pollution(struct city_dialog *pdialog,
				      char *retbuf, int n)
{
  struct city *pcity;
  int pollution=0;

  if (pdialog) {
    pcity=pdialog->pcity;
    pollution=pcity->pollution;
  }

  my_snprintf(retbuf, n, _("Pollution:    %3d"), pollution);
}

/****************************************************************
...
*****************************************************************/
static void get_contents_of_storage(struct city_dialog *pdialog,
				    char *retbuf, int n)
{
  struct city *pcity;
  char granary='?';
  int foodstock=0;
  int foodbox=0;

  if (pdialog) {
    pcity=pdialog->pcity;
    granary=(city_got_effect(pcity, B_GRANARY) ? '*' : ' ');
    foodstock=pcity->food_stock;
    foodbox=city_granary_size(pcity->size);
  }

  my_snprintf(retbuf, n, _("Granary: %c%3d/%-3d"),
	      granary, foodstock, foodbox);
}

/****************************************************************
...
*****************************************************************/
static void get_contents_of_production(struct city_dialog *pdialog,
				       char *retbuf, int n)
{
  struct city *pcity;
  int foodprod=0;
  int foodsurplus=0;
  int shieldprod=0;
  int shieldsurplus=0;
  int tradeprod=0;
  int tradesurplus=0;

  if (pdialog) {
    pcity=pdialog->pcity;
    foodprod=pcity->food_prod;
    foodsurplus=pcity->food_surplus;
    shieldprod=pcity->shield_prod;
    shieldsurplus=pcity->shield_surplus;
    tradeprod=pcity->trade_prod+pcity->corruption;
    tradesurplus=pcity->trade_prod;
  }

  my_snprintf(retbuf, n,
	  _("Food:  %3d (%+-4d)\n"
	    "Prod:  %3d (%+-4d)\n"
	    "Trade: %3d (%+-4d)"),
	  foodprod, foodsurplus,
	  shieldprod, shieldsurplus,
	  tradeprod, tradesurplus);
}

/****************************************************************
...
*****************************************************************/
static void get_contents_of_output(struct city_dialog *pdialog,
				   char *retbuf, int n)
{
  struct city *pcity;
  int goldtotal=0;
  int goldsurplus=0;
  int luxtotal=0;
  int scitotal=0;

  if (pdialog) {
    pcity=pdialog->pcity;
    goldtotal=pcity->tax_total;
    goldsurplus=city_gold_surplus(pcity);
    luxtotal=pcity->luxury_total;
    scitotal=pcity->science_total;
  }

  my_snprintf(retbuf, n, 
	  _("Gold:  %3d (%+-4d)\n"
	    "Lux:   %3d\n"
	    "Sci:   %3d"),
	  goldtotal, goldsurplus,
	  luxtotal,
	  scitotal);
}

/****************************************************************
...
*****************************************************************/
static void get_contents_of_progress(struct city_dialog *pdialog,
				     char *retbuf, int n)
{
  get_city_dialog_production(pdialog ? pdialog->pcity : NULL, retbuf, n);
}

/****************************************************************
...
*****************************************************************/
static void get_contents_of_worklist(struct city_dialog *pdialog,
				     char *retbuf, int n)
{
  struct city *pcity = pdialog ? pdialog->pcity : NULL;

  if (pcity && worklist_is_empty(&pcity->worklist)) {
    mystrlcpy(retbuf, _("(is empty)"), n);
  } else {
    mystrlcpy(retbuf, _("(in prog.)"), n);
  }
}

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
    economy_report_dialog_update();
  } else {
    if(pdialog)  {
      /* Set the buttons we do not want live while a Diplomat investigates */
      XtSetSensitive(pdialog->buy_command, FALSE);
      XtSetSensitive(pdialog->change_command, FALSE);
      XtSetSensitive(pdialog->worklist_command, FALSE);
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

  pcity_sup=player_find_city_by_id(game.player_ptr, punit->homecity);
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

#define LAYOUT_DEBUG 0

struct city_dialog *create_city_dialog(struct city *pcity, int make_modal)
{
  char *dummy_improvement_list[]={ 
    "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX",
    "2",
    "3",
    "4",
    "5",
    "6",
    "7",
    "8",
    "9",
    0
  };
  static Pixmap icon_pixmap = 0;

  int i, itemWidth;
  struct city_dialog *pdialog;
  char lblbuf[512];
  Widget first_citizen, first_support, first_present;
  XtWidgetGeometry geom;
  Dimension widthTotal;
  Dimension widthCitizen, borderCitizen, internalCitizen, spaceCitizen;
  Dimension widthUnit, borderUnit, internalUnit, spaceUnit;
  Dimension widthNext, borderNext, internalNext, spaceNext;
  Dimension widthPrev, borderPrev, internalPrev, spacePrev;
  Widget relative;


  if (NORMAL_TILE_HEIGHT<45) dummy_improvement_list[5]=0;

  if (concise_city_production) {
    dummy_improvement_list[0] = "XXXXXXXXXXXXXXXXXXXXXXXXXXXX";
  }

  pdialog=fc_malloc(sizeof(struct city_dialog));
  pdialog->pcity=pcity;
  pdialog->support_unit_base=0;
  pdialog->present_unit_base=0;
  pdialog->worklist_shell = NULL;

  if(!icon_pixmap)
    icon_pixmap=
      XCreateBitmapFromData(display,
			    RootWindowOfScreen(XtScreen(toplevel)),
			    cityicon_bits,
			    cityicon_width, cityicon_height);


  pdialog->shell=
    XtVaCreatePopupShell(pcity->name,
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


  first_citizen=
    XtVaCreateManagedWidget("citizenlabels",
			    commandWidgetClass,
			    pdialog->main_form,
			    XtNfromVert, 
			    pdialog->cityname_label,
			    XtNbitmap, get_citizen_pixmap(2),
			    NULL);


  pdialog->sub_form=
    XtVaCreateManagedWidget("citysubform", 
			    formWidgetClass, 
			    pdialog->main_form, 
			    XtNfromVert, 
			    (XtArgVal)first_citizen,
			    NULL);


  pdialog->left_form=
    XtVaCreateManagedWidget("cityleftform", 
			    formWidgetClass, 
			    pdialog->sub_form, 
			    NULL);

  get_contents_of_production(NULL, lblbuf, sizeof(lblbuf));
  pdialog->production_label=
    XtVaCreateManagedWidget("cityprodlabel", 
			    labelWidgetClass,
			    pdialog->left_form,
			    XtNlabel, lblbuf,
			    NULL);

  get_contents_of_output(NULL, lblbuf, sizeof(lblbuf));
  pdialog->output_label=
    XtVaCreateManagedWidget("cityoutputlabel", 
			    labelWidgetClass,
			    pdialog->left_form,
			    XtNlabel, lblbuf,
			    XtNfromVert, 
			    (XtArgVal)pdialog->production_label,
			    NULL);

  get_contents_of_storage(NULL, lblbuf, sizeof(lblbuf));
  pdialog->storage_label=
    XtVaCreateManagedWidget("citystoragelabel", 
			    labelWidgetClass,
			    pdialog->left_form,
			    XtNlabel, lblbuf,
			    XtNfromVert, 
			    (XtArgVal)pdialog->output_label,
			    NULL);

  get_contents_of_pollution(NULL, lblbuf, sizeof(lblbuf));
  pdialog->pollution_label=
    XtVaCreateManagedWidget("citypollutionlabel", 
			    labelWidgetClass,
			    pdialog->left_form,
			    XtNlabel, lblbuf,
			    XtNfromVert, 
			    (XtArgVal)pdialog->storage_label,
			    NULL);


  pdialog->map_canvas=
    XtVaCreateManagedWidget("citymapcanvas", 
			    xfwfcanvasWidgetClass,
			    pdialog->sub_form,
			    "exposeProc", (XtArgVal)city_map_canvas_expose,
			    "exposeProcData", (XtArgVal)pdialog,
			    XtNfromHoriz, (XtArgVal)pdialog->left_form,
			    XtNwidth, NORMAL_TILE_WIDTH*5,
			    XtNheight, NORMAL_TILE_HEIGHT*5,
			    NULL);


  pdialog->building_label=
    XtVaCreateManagedWidget("citybuildinglabel",
			    labelWidgetClass,
			    pdialog->sub_form,
			    XtNfromHoriz, 
			    (XtArgVal)pdialog->map_canvas,
			    XtNlabel,
			    concise_city_production
				? "XXXXXXXXXXXXXXXXXXXXXXXXXXXX"
				: "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX",
			    NULL);

  get_contents_of_progress(NULL, lblbuf, sizeof(lblbuf));
  pdialog->progress_label=
    XtVaCreateManagedWidget("cityprogresslabel",
			    labelWidgetClass,
			    pdialog->sub_form,
			    XtNfromHoriz, 
			    (XtArgVal)pdialog->map_canvas,
			    XtNfromVert, 
			    pdialog->building_label,
			    XtNlabel, lblbuf,
			    NULL);

  pdialog->buy_command=
    I_L(XtVaCreateManagedWidget("citybuycommand", 
				commandWidgetClass,
				pdialog->sub_form,
				XtNfromVert, 
				pdialog->building_label,
				XtNfromHoriz, 
				pdialog->progress_label,
				NULL));

  pdialog->change_command=
    I_L(XtVaCreateManagedWidget("citychangecommand", 
			    commandWidgetClass,
			    pdialog->sub_form,
			    XtNfromVert, 
			    pdialog->building_label,
			    XtNfromHoriz, 
			    pdialog->buy_command,
			    NULL));
 
  pdialog->improvement_viewport=
    XtVaCreateManagedWidget("cityimprovview", 
			    viewportWidgetClass,
			    pdialog->sub_form,
			    XtNfromHoriz, 
			    (XtArgVal)pdialog->map_canvas,
			    XtNfromVert, 
			    pdialog->change_command,
			    NULL);

  pdialog->improvement_list=
    XtVaCreateManagedWidget("cityimprovlist", 
			    listWidgetClass,
			    pdialog->improvement_viewport,
			    XtNforceColumns, 1,
			    XtNdefaultColumns,1, 
			    XtNlist,
			      (XtArgVal)dummy_improvement_list,
			    XtNverticalList, False,
			    NULL);

  pdialog->sell_command=
    I_L(XtVaCreateManagedWidget("citysellcommand", 
			    commandWidgetClass,
			    pdialog->sub_form,
			    XtNfromVert, 
			    pdialog->improvement_viewport,
			    XtNfromHoriz, 
			    (XtArgVal)pdialog->map_canvas,
			    NULL));

  pdialog->worklist_command=
    I_L(XtVaCreateManagedWidget("cityworklistcommand", 
			    commandWidgetClass,
			    pdialog->sub_form,
			    XtNfromVert, 
			    pdialog->improvement_viewport,
			    XtNfromHoriz, 
			    pdialog->sell_command,
			    NULL));

  get_contents_of_worklist(NULL, lblbuf, sizeof(lblbuf));
  pdialog->worklist_label=
    XtVaCreateManagedWidget("cityworklistlabel",
			    labelWidgetClass,
			    pdialog->sub_form,
			    XtNfromVert,
			    pdialog->improvement_viewport,
			    XtNfromHoriz,
			    pdialog->worklist_command,
			    XtNlabel, lblbuf,
			    NULL);


  pdialog->support_unit_label=
    I_L(XtVaCreateManagedWidget("supportunitlabel",
			    labelWidgetClass,
			    pdialog->main_form,
			    XtNfromVert, 
			    pdialog->sub_form,
			    NULL));

  first_support=
    XtVaCreateManagedWidget("supportunitcanvas",
			    pixcommWidgetClass,
			    pdialog->main_form,
			    XtNfromVert, pdialog->support_unit_label,
			    XtNwidth, NORMAL_TILE_WIDTH,
			    XtNheight, NORMAL_TILE_HEIGHT+NORMAL_TILE_HEIGHT/2,
			    NULL);

  pdialog->present_unit_label=
    I_L(XtVaCreateManagedWidget("presentunitlabel",
			    labelWidgetClass,
			    pdialog->main_form,
			    XtNfromVert, 
			    first_support,
			    NULL));

  first_present=
    XtVaCreateManagedWidget("presentunitcanvas",
    			    pixcommWidgetClass,
			    pdialog->main_form,
			    XtNfromVert, pdialog->present_unit_label,
			    XtNwidth, NORMAL_TILE_WIDTH,
			    XtNheight, NORMAL_TILE_HEIGHT,
			    NULL);


  pdialog->support_unit_next_command=
    XtVaCreateManagedWidget("supportunitnextcommand",
			    commandWidgetClass,
			    pdialog->main_form,
			    NULL);
  pdialog->support_unit_prev_command=
    XtVaCreateManagedWidget("supportunitprevcommand",
			    commandWidgetClass,
			    pdialog->main_form,
			    NULL);

  pdialog->present_unit_next_command=
    XtVaCreateManagedWidget("presentunitnextcommand",
			    commandWidgetClass,
			    pdialog->main_form,
			    NULL);
  pdialog->present_unit_prev_command=
    XtVaCreateManagedWidget("presentunitprevcommand",
			    commandWidgetClass,
			    pdialog->main_form,
			    NULL);


  pdialog->close_command=
    I_L(XtVaCreateManagedWidget("cityclosecommand", 
			    commandWidgetClass,
			    pdialog->main_form,
			    XtNfromVert, first_present,
			    NULL));

  pdialog->rename_command=
    I_L(XtVaCreateManagedWidget("cityrenamecommand", 
			    commandWidgetClass,
			    pdialog->main_form,
			    XtNfromVert, first_present,
			    XtNfromHoriz, pdialog->close_command,
			    NULL));

  pdialog->trade_command=
    I_L(XtVaCreateManagedWidget("citytradecommand", 
			    commandWidgetClass,
			    pdialog->main_form,
			    XtNfromVert, first_present,
			    XtNfromHoriz, pdialog->rename_command,
			    NULL));

  pdialog->activate_command=
    I_L(XtVaCreateManagedWidget("cityactivatecommand",
    			    commandWidgetClass,
			    pdialog->main_form,
			    XtNfromVert, first_present,
			    XtNfromHoriz, pdialog->trade_command,
			    NULL));

  pdialog->show_units_command=
    I_L(XtVaCreateManagedWidget("cityshowunitscommand",
			    commandWidgetClass,
			    pdialog->main_form,
			    XtNfromVert, first_present,
			    XtNfromHoriz, pdialog->activate_command,
			    NULL));

  pdialog->cityopt_command=
    I_L(XtVaCreateManagedWidget("cityoptionscommand",
			    commandWidgetClass,
			    pdialog->main_form,
			    XtNfromVert, first_present,
			    XtNfromHoriz, pdialog->show_units_command,
			    NULL));


  XtRealizeWidget(pdialog->shell);
  XtQueryGeometry (pdialog->sub_form, NULL, &geom);
  widthTotal=geom.width;
  if (widthTotal>0) {
    XtQueryGeometry (first_citizen, NULL, &geom);
    widthCitizen=geom.width;
    borderCitizen=geom.border_width;
    XtVaGetValues(first_citizen,
		  XtNinternalWidth, &internalCitizen,
		  XtNhorizDistance, &spaceCitizen,
		  NULL);
    XtQueryGeometry (first_support, NULL, &geom);
    widthUnit=geom.width;
    borderUnit=geom.border_width;
    XtVaGetValues(first_support,
		  XtNinternalWidth, &internalUnit,
		  XtNhorizDistance, &spaceUnit,
		  NULL);
    XtQueryGeometry (pdialog->support_unit_next_command, NULL, &geom);
    widthNext=geom.width;
    borderNext=geom.border_width;
    XtVaGetValues(pdialog->support_unit_next_command,
		  XtNinternalWidth, &internalNext,
		  XtNhorizDistance, &spaceNext,
		  NULL);
    XtQueryGeometry (pdialog->support_unit_prev_command, NULL, &geom);
    widthPrev=geom.width;
    borderPrev=geom.border_width;
    XtVaGetValues(pdialog->support_unit_prev_command,
		  XtNinternalWidth, &internalPrev,
		  XtNhorizDistance, &spacePrev,
		  NULL);
#if LAYOUT_DEBUG >= 3
    printf
    (
     "T: w: %d\n"
     "C: wbis: %d %d %d %d\n"
     "U: wbis: %d %d %d %d\n"
     "N: wbis: %d %d %d %d\n"
     "P: wbis: %d %d %d %d\n"
     ,
     widthTotal,
     widthCitizen, borderCitizen, internalCitizen, spaceCitizen,
     widthUnit, borderUnit, internalUnit, spaceUnit,
     widthNext, borderNext, internalNext, spaceNext,
     widthPrev, borderPrev, internalPrev, spacePrev
    );
#endif
    itemWidth=widthCitizen+2*borderCitizen+2*internalCitizen+spaceCitizen;
    if (itemWidth>0) {
      pdialog->num_citizens_shown=widthTotal/itemWidth;
      if (pdialog->num_citizens_shown<MIN_NUM_CITIZENS)
	pdialog->num_citizens_shown=MIN_NUM_CITIZENS;
      else if (pdialog->num_citizens_shown>MAX_NUM_CITIZENS)
	pdialog->num_citizens_shown=MAX_NUM_CITIZENS;
    } else {
      pdialog->num_citizens_shown=MIN_NUM_CITIZENS;
    }
#if LAYOUT_DEBUG >= 2
    printf
    (
     "C: wT iW nC: %d %d %d\n"
     ,
     widthTotal,
     itemWidth,
     pdialog->num_citizens_shown
    );
#endif
    if (widthNext<widthPrev) widthNext=widthPrev;
    if (borderNext<borderPrev) borderNext=borderPrev;
    if (internalNext<internalPrev) internalNext=internalPrev;
    if (spaceNext<spacePrev) spaceNext=spacePrev;
    widthTotal-=(widthNext+2*borderNext+2*internalNext+spaceNext);
    itemWidth=widthUnit+2*borderUnit+2*internalUnit+spaceUnit;
    if (itemWidth>0) {
      pdialog->num_units_shown=widthTotal/itemWidth;
      if (pdialog->num_units_shown<MIN_NUM_UNITS)
	pdialog->num_units_shown=MIN_NUM_UNITS;
      else if (pdialog->num_units_shown>MAX_NUM_UNITS)
	pdialog->num_units_shown=MAX_NUM_UNITS;
    } else {
      pdialog->num_units_shown=MIN_NUM_UNITS;
    }
#if LAYOUT_DEBUG >= 2
    printf
    (
     "U: wT iW nU: %d %d %d\n"
     ,
     widthTotal,
     itemWidth,
     pdialog->num_units_shown
    );
#endif
  } else {
    pdialog->num_citizens_shown=DEFAULT_NUM_CITIZENS;
    pdialog->num_units_shown=DEFAULT_NUM_UNITS;
    if (NORMAL_TILE_HEIGHT<45) {
      pdialog->num_citizens_shown-=5;
      pdialog->num_units_shown+=3;
    }
  }
#if LAYOUT_DEBUG >= 1
    printf
    (
     "nC nU: %d %d\n"
     ,
     pdialog->num_citizens_shown, pdialog->num_units_shown
    );
#endif

  pdialog->citizen_labels=
    fc_malloc(pdialog->num_citizens_shown * sizeof(Widget));
  pdialog->citizen_type=
    fc_malloc(pdialog->num_citizens_shown * sizeof(int));

  pdialog->support_unit_pixcomms=
    fc_malloc(pdialog->num_units_shown * sizeof(Widget));
  pdialog->present_unit_pixcomms=
    fc_malloc(pdialog->num_units_shown * sizeof(Widget));


  pdialog->citizen_labels[0]=first_citizen;
  for(i=1; i<pdialog->num_citizens_shown; i++)
    pdialog->citizen_labels[i]=
    XtVaCreateManagedWidget("citizenlabels",
			    commandWidgetClass,
			    pdialog->main_form,
			    XtNfromVert, pdialog->cityname_label,
			    XtNfromHoriz, 
			      (XtArgVal)pdialog->citizen_labels[i-1],
			    XtNbitmap, get_citizen_pixmap(2),
			    NULL);


  pdialog->support_unit_pixcomms[0]=first_support;
  for(i=1; i<pdialog->num_units_shown; i++) {
    pdialog->support_unit_pixcomms[i]=
      XtVaCreateManagedWidget("supportunitcanvas",
			      pixcommWidgetClass,
			      pdialog->main_form,
			      XtNfromVert, pdialog->support_unit_label,
			      XtNfromHoriz,
			        (XtArgVal)pdialog->support_unit_pixcomms[i-1],
			      XtNwidth, NORMAL_TILE_WIDTH,
			      XtNheight, NORMAL_TILE_HEIGHT+NORMAL_TILE_HEIGHT/2,
			      NULL);
  }

  relative=pdialog->support_unit_pixcomms[pdialog->num_units_shown-1];
  XtVaSetValues(pdialog->support_unit_next_command,
		XtNfromVert, pdialog->support_unit_label,
		XtNfromHoriz, (XtArgVal)relative,
		NULL);
  XtVaSetValues(pdialog->support_unit_prev_command,
		XtNfromVert, pdialog->support_unit_next_command,
		XtNfromHoriz, (XtArgVal)relative,
		NULL);

  pdialog->present_unit_pixcomms[0]=first_present;
  for(i=1; i<pdialog->num_units_shown; i++) {
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
  }

  relative=pdialog->present_unit_pixcomms[pdialog->num_units_shown-1];
  XtVaSetValues(pdialog->present_unit_next_command,
		XtNfromVert, pdialog->present_unit_label,
		XtNfromHoriz, (XtArgVal)relative,
		NULL);
  XtVaSetValues(pdialog->present_unit_prev_command,
		XtNfromVert, pdialog->present_unit_next_command,
		XtNfromHoriz, (XtArgVal)relative,
		NULL);

  
  XtVaSetValues(pdialog->shell, XtNiconPixmap, icon_pixmap, NULL);


  XtAddCallback(pdialog->sell_command, XtNcallback, sell_callback,
		(XtPointer)pdialog);

  XtAddCallback(pdialog->buy_command, XtNcallback, buy_callback,
		(XtPointer)pdialog);

  XtAddCallback(pdialog->change_command, XtNcallback, change_callback,
		(XtPointer)pdialog);

  XtAddCallback(pdialog->worklist_command, XtNcallback, worklist_callback,
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

  XtAddCallback(pdialog->support_unit_next_command, XtNcallback,
		units_next_prev_callback, (XtPointer)pdialog);
  XtAddCallback(pdialog->support_unit_prev_command, XtNcallback,
		units_next_prev_callback, (XtPointer)pdialog);
  XtAddCallback(pdialog->present_unit_next_command, XtNcallback,
		units_next_prev_callback, (XtPointer)pdialog);
  XtAddCallback(pdialog->present_unit_prev_command, XtNcallback,
		units_next_prev_callback, (XtPointer)pdialog);

  XtAddCallback(pdialog->cityopt_command, XtNcallback, cityopt_callback,
		(XtPointer)pdialog);

  genlist_insert(&dialog_list, pdialog, 0);

  for(i=0; i<B_LAST+1; i++)
    pdialog->improvlist_names_ptrs[i]=0;

  for(i=0; i<pdialog->num_citizens_shown; i++)
    pdialog->citizen_type[i]=-1;

  
  XtRealizeWidget(pdialog->shell);

  refresh_city_dialog(pdialog->pcity);

  if(make_modal)
    XtSetSensitive(toplevel, FALSE);
  
  pdialog->is_modal=make_modal;

  XSetWMProtocols(display, XtWindow(pdialog->shell), &wm_delete_window, 1);
  XtOverrideTranslations(pdialog->shell, 
    XtParseTranslationTable ("<Message>WM_PROTOCOLS: msg-close-city()"));

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
  struct unit *pmyunit = NULL;

  if( unit_list_size(punit_list) )  {
    unit_list_iterate((*punit_list), punit) {
      if(game.player_idx==punit->owner) {
	request_new_unit_activity(punit, ACTIVITY_IDLE);
	pmyunit = punit;
      }
    } unit_list_iterate_end;
    if (pmyunit)
      set_unit_focus(pmyunit);
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


/****************************************************************
...
*****************************************************************/
void units_next_prev_callback(Widget w, XtPointer client_data,
			      XtPointer call_data)
{
  struct city_dialog *pdialog = (struct city_dialog *)client_data;

  if (w==pdialog->support_unit_next_command) {
    (pdialog->support_unit_base)++;
    city_dialog_update_supported_units(pdialog, 0);
  } else if (w==pdialog->support_unit_prev_command) {
    (pdialog->support_unit_base)--;
    city_dialog_update_supported_units(pdialog, 0);
  } else if (w==pdialog->present_unit_next_command) {
    (pdialog->present_unit_base)++;
    city_dialog_update_present_units(pdialog, 0);
  } else if (w==pdialog->present_unit_prev_command) {
    (pdialog->present_unit_base)--;
    city_dialog_update_present_units(pdialog, 0);
  }
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
static void activate_unit(struct unit *punit)
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
  struct city *pcity;
  struct city_dialog *pdialog;

  if((punit=player_find_unit_by_id(game.player_ptr, (size_t)client_data)))  {
    activate_unit(punit);
    if((pcity=map_get_city(punit->x, punit->y)))
      if((pdialog=get_city_dialog(pcity)))
	city_dialog_update_present_units(pdialog, 0);
  }

  destroy_message_dialog(w);
}


/****************************************************************
...
*****************************************************************/
static void supported_units_activate_callback(Widget w, XtPointer client_data, 
					      XtPointer call_data)
{
  struct unit *punit;
  struct city *pcity;
  struct city_dialog *pdialog;

  if((punit=player_find_unit_by_id(game.player_ptr, (size_t)client_data)))  {
    activate_unit(punit);
    if((pcity=map_get_city(punit->x, punit->y)))
      if((pdialog=get_city_dialog(pcity)))
	city_dialog_update_supported_units(pdialog, 0);
  }

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

  if((punit=player_find_unit_by_id(game.player_ptr, (size_t)client_data)))  {
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

  if((punit=player_find_unit_by_id(game.player_ptr, (size_t)client_data)))  {
    activate_unit(punit);
    if((pcity=player_find_city_by_id(game.player_ptr, punit->homecity)))
      if((pdialog=get_city_dialog(pcity)))
	close_city_dialog(pdialog);
  }
}


/****************************************************************
...
*****************************************************************/
static void present_units_sentry_callback(Widget w, XtPointer client_data, 
					   XtPointer call_data)
{
  struct unit *punit;

  if((punit=player_find_unit_by_id(game.player_ptr, (size_t)client_data)))
    request_unit_sentry(punit);

  destroy_message_dialog(w);
}


/****************************************************************
...
*****************************************************************/
static void present_units_fortify_callback(Widget w, XtPointer client_data, 
					   XtPointer call_data)
{
  struct unit *punit;

  if((punit=player_find_unit_by_id(game.player_ptr, (size_t)client_data)))
    request_unit_fortify(punit);

  destroy_message_dialog(w);
}


/****************************************************************
...
*****************************************************************/
static void present_units_disband_callback(Widget w, XtPointer client_data, 
					   XtPointer call_data)
{
  struct unit *punit;

  if((punit=player_find_unit_by_id(game.player_ptr, (size_t)client_data)))
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
  
  if((punit=player_find_unit_by_id(game.player_ptr, (size_t)client_data)))
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
  
  if((punit=player_find_unit_by_id(game.player_ptr, (size_t)client_data)) &&
     (pcity=map_get_city(punit->x, punit->y)) &&
     (pdialog=get_city_dialog(pcity))) {
    
    if(e->type==ButtonRelease && e->xbutton.button==Button2)  {
      activate_unit(punit);
      close_city_dialog(pdialog);
      return;
    }
    if(e->type==ButtonRelease && e->xbutton.button==Button3)  {
      activate_unit(punit);
      return;
    }

    wd=popup_message_dialog(pdialog->shell, 
			    "presentunitsdialog", 
			    unit_description(punit),
			    present_units_activate_callback, punit->id, 1,
			    present_units_activate_close_callback, punit->id, 1,
			    present_units_sentry_callback, punit->id, 1,
			    present_units_fortify_callback, punit->id, 1,
			    present_units_disband_callback, punit->id, 1,
			    present_units_homecity_callback, punit->id, 1,
			    upgrade_callback, punit->id, 1,
			    present_units_cancel_callback, 0, 0, 
			    NULL);

    if (punit->activity == ACTIVITY_SENTRY
	|| !can_unit_do_activity(punit, ACTIVITY_SENTRY)) {
      XtSetSensitive(XtNameToWidget(wd, "*button2"), FALSE);
    }
    if (punit->activity == ACTIVITY_FORTIFYING
	|| !can_unit_do_activity(punit, ACTIVITY_FORTIFYING)) {
      XtSetSensitive(XtNameToWidget(wd, "*button3"), FALSE);
    }
    if (punit->homecity == pcity->id) {
      XtSetSensitive(XtNameToWidget(wd, "*button5"), FALSE);
    }
    if (can_upgrade_unittype(game.player_ptr,punit->type) == -1) {
      XtSetSensitive(XtNameToWidget(wd, "*button6"), FALSE);
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
     sz_strlcpy(packet.name, input_dialog_get_input(w));
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
		      _("What should we rename the city to?"),
		      pdialog->pcity->name,
		      rename_city_callback, (XtPointer)pdialog,
		      rename_city_callback, (XtPointer)0);
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
  char buf[512], *bptr=buf;
  int nleft = sizeof(buf);
  struct city_dialog *pdialog;

  pdialog=(struct city_dialog *)client_data;

  my_snprintf(buf, sizeof(buf),
	      _("These trade routes have been established with %s:\n"),
	      pdialog->pcity->name);
  bptr = end_of_strn(bptr, &nleft);
  
  for (i = 0; i < NUM_TRADEROUTES; i++)
    if(pdialog->pcity->trade[i]) {
      struct city *pcity;
      x=1;
      total+=pdialog->pcity->trade_value[i];
      if((pcity=find_city_by_id(pdialog->pcity->trade[i]))) {
	my_snprintf(bptr, nleft, _("%32s: %2d Trade/Year\n"),
		    pcity->name, pdialog->pcity->trade_value[i]);
	bptr = end_of_strn(bptr, &nleft);
      } else {	
	my_snprintf(bptr, nleft, _("%32s: %2d Trade/Year\n"), _("Unknown"),
		    pdialog->pcity->trade_value[i]);
	bptr = end_of_strn(bptr, &nleft);
      }
    }
  if (!x) {
    mystrlcpy(bptr, _("No trade routes exist.\n"), nleft);
  } else {
    my_snprintf(bptr, nleft, _("\nTotal trade %d Trade/Year\n"), total);
  }
  
  popup_message_dialog(pdialog->shell, 
		       "citytradedialog", 
		       buf, 
		       trade_message_dialog_callback, 0, 0,
		       NULL);
}


/****************************************************************
...
*****************************************************************/
void city_dialog_update_pollution(struct city_dialog *pdialog)
{
  char buf[512];

  get_contents_of_pollution(pdialog, buf, sizeof(buf));
  xaw_set_label(pdialog->pollution_label, buf);
}

/****************************************************************
...
*****************************************************************/
void city_dialog_update_storage(struct city_dialog *pdialog)
{
  char buf[512];

  get_contents_of_storage(pdialog, buf, sizeof(buf));
  xaw_set_label(pdialog->storage_label, buf);
}

/****************************************************************
...
*****************************************************************/
void city_dialog_update_building(struct city_dialog *pdialog)
{
  char buf[32];
  struct city *pcity=pdialog->pcity;

  if (pcity->currently_building==B_CAPITAL)
    XtSetSensitive(pdialog->buy_command, False);
  else
    XtSetSensitive(pdialog->buy_command, !pcity->did_buy);
  XtSetSensitive(pdialog->sell_command, !pcity->did_sell);

  xaw_set_label(pdialog->building_label,
		pcity->is_building_unit ?
		  get_unit_type(pcity->currently_building)->name :
		  get_impr_name_ex(pcity, pcity->currently_building));

  get_contents_of_progress(pdialog, buf, sizeof(buf));
  xaw_set_label(pdialog->progress_label, buf);

  get_contents_of_worklist(pdialog, buf, sizeof(buf));
  xaw_set_label(pdialog->worklist_label, buf);
}

/****************************************************************
...
*****************************************************************/
void city_dialog_update_production(struct city_dialog *pdialog)
{
  char buf[512];

  get_contents_of_production(pdialog, buf, sizeof(buf));
  xaw_set_label(pdialog->production_label, buf);
}

/****************************************************************
...
*****************************************************************/
void city_dialog_update_output(struct city_dialog *pdialog)
{
  char buf[512];

  get_contents_of_output(pdialog, buf, sizeof(buf));
  xaw_set_label(pdialog->output_label, buf);
}


/****************************************************************
...
*****************************************************************/
void city_dialog_update_map(struct city_dialog *pdialog)
{
  int x, y;
  struct city *pcity=pdialog->pcity;
  
  for(y=0; y<CITY_MAP_SIZE; y++) {
    for(x=0; x<CITY_MAP_SIZE; x++) {
      int map_x, map_y;

      if (is_valid_city_coords(x, y)
	  && city_map_to_map(&map_x, &map_y, pcity, x, y)
	  && tile_get_known(map_x, map_y)) {
	pixmap_put_tile(XtWindow(pdialog->map_canvas), map_x, map_y,
			x * NORMAL_TILE_WIDTH, y * NORMAL_TILE_HEIGHT, 1);
	if (pcity->city_map[x][y] == C_TILE_WORKER)
	  put_city_tile_output(XtWindow(pdialog->map_canvas),
			       x * NORMAL_TILE_WIDTH,
			       y * NORMAL_TILE_HEIGHT,
			       city_get_food_tile(x, y, pcity),
			       city_get_shields_tile(x, y, pcity),
			       city_get_trade_tile(x, y, pcity));
	else if (pcity->city_map[x][y] == C_TILE_UNAVAILABLE)
	  pixmap_frame_tile_red(XtWindow(pdialog->map_canvas),
				x * NORMAL_TILE_WIDTH,
				y * NORMAL_TILE_HEIGHT);
      } else {
	pixmap_put_black_tile(XtWindow(pdialog->map_canvas),
			      x * NORMAL_TILE_WIDTH,
			      y * NORMAL_TILE_HEIGHT);
      }
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

  i=0;

  for(n=0; n<pcity->ppl_happy[4] && i<pdialog->num_citizens_shown; n++, i++)
    if(pdialog->citizen_type[i]!=5 &&  pdialog->citizen_type[i]!=6) {
      pdialog->citizen_type[i]=5+i%2;
      xaw_set_bitmap(pdialog->citizen_labels[i], 
		     get_citizen_pixmap(pdialog->citizen_type[i]));
      XtSetSensitive(pdialog->citizen_labels[i], FALSE);
      XtRemoveAllCallbacks(pdialog->citizen_labels[i], XtNcallback);
    }
  if(n<pcity->ppl_happy[4]) goto city_dialog_update_citizens_overflow;

  for(n=0; n<pcity->ppl_content[4] && i<pdialog->num_citizens_shown; n++, i++)
    if(pdialog->citizen_type[i]!=3 && pdialog->citizen_type[i]!=4) {
      pdialog->citizen_type[i]=3+i%2;
      xaw_set_bitmap(pdialog->citizen_labels[i], 
		     get_citizen_pixmap(pdialog->citizen_type[i]));
      XtSetSensitive(pdialog->citizen_labels[i], FALSE);
      XtRemoveAllCallbacks(pdialog->citizen_labels[i], XtNcallback);
    }
  if(n<pcity->ppl_content[4]) goto city_dialog_update_citizens_overflow;

  for(n=0; n<pcity->ppl_unhappy[4] && i<pdialog->num_citizens_shown; n++, i++)
    if(pdialog->citizen_type[i]!=7 && pdialog->citizen_type[i]!=8) {
      pdialog->citizen_type[i]=7+i%2;
      xaw_set_bitmap(pdialog->citizen_labels[i],
		     get_citizen_pixmap(pdialog->citizen_type[i]));
      XtRemoveAllCallbacks(pdialog->citizen_labels[i], XtNcallback);
      XtSetSensitive(pdialog->citizen_labels[i], FALSE);
    }
  if(n<pcity->ppl_unhappy[4]) goto city_dialog_update_citizens_overflow;

  for (n = 0; n < pcity->ppl_angry[4] && i < pdialog->num_citizens_shown;
       n++, i++)
    if (pdialog->citizen_type[i] != 9 && pdialog->citizen_type[i] != 10) {
      pdialog->citizen_type[i] = 9 + i % 2;
      xaw_set_bitmap(pdialog->citizen_labels[i],
		     get_citizen_pixmap(pdialog->citizen_type[i]));
      XtRemoveAllCallbacks(pdialog->citizen_labels[i], XtNcallback);
      XtSetSensitive(pdialog->citizen_labels[i], FALSE);
    }
  if (n < pcity->ppl_angry[4])
    goto city_dialog_update_citizens_overflow;

  for(n=0; n<pcity->ppl_elvis && i<pdialog->num_citizens_shown; n++, i++)
    if(pdialog->citizen_type[i]!=0) {
      xaw_set_bitmap(pdialog->citizen_labels[i], get_citizen_pixmap(0));
      pdialog->citizen_type[i]=0;
      XtRemoveAllCallbacks(pdialog->citizen_labels[i], XtNcallback);
      XtAddCallback(pdialog->citizen_labels[i], XtNcallback, elvis_callback,
		    (XtPointer)pdialog);
      XtSetSensitive(pdialog->citizen_labels[i], TRUE);
    }
  if(n<pcity->ppl_elvis) goto city_dialog_update_citizens_overflow;

  for(n=0; n<pcity->ppl_scientist && i<pdialog->num_citizens_shown; n++, i++)
    if(pdialog->citizen_type[i]!=1) {
      xaw_set_bitmap(pdialog->citizen_labels[i], get_citizen_pixmap(1));
      pdialog->citizen_type[i]=1;
      XtRemoveAllCallbacks(pdialog->citizen_labels[i], XtNcallback);
      XtAddCallback(pdialog->citizen_labels[i], XtNcallback, scientist_callback,
		    (XtPointer)pdialog);
      XtSetSensitive(pdialog->citizen_labels[i], TRUE);
    }
  if(n<pcity->ppl_scientist) goto city_dialog_update_citizens_overflow;

  for(n=0; n<pcity->ppl_taxman && i<pdialog->num_citizens_shown; n++, i++)
    if(pdialog->citizen_type[i]!=2) {
      xaw_set_bitmap(pdialog->citizen_labels[i], get_citizen_pixmap(2));
      pdialog->citizen_type[i]=2;
      XtRemoveAllCallbacks(pdialog->citizen_labels[i], XtNcallback);
      XtAddCallback(pdialog->citizen_labels[i], XtNcallback, taxman_callback,
		    (XtPointer)pdialog);
      XtSetSensitive(pdialog->citizen_labels[i], TRUE);
    }
  if(n<pcity->ppl_taxman) goto city_dialog_update_citizens_overflow;

  for(; i<pdialog->num_citizens_shown; i++) {
    xaw_set_bitmap(pdialog->citizen_labels[i], None);
    XtSetSensitive(pdialog->citizen_labels[i], FALSE);
    XtRemoveAllCallbacks(pdialog->citizen_labels[i], XtNcallback);
  }

  return;

city_dialog_update_citizens_overflow:

  i=pdialog->num_citizens_shown-1;
  xaw_set_bitmap(pdialog->citizen_labels[i], sprites.right_arrow->pixmap);
  XtSetSensitive(pdialog->citizen_labels[i], FALSE);
  XtRemoveAllCallbacks(pdialog->citizen_labels[i], XtNcallback);

  return;
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
  
  if((punit=player_find_unit_by_id(game.player_ptr, (size_t)client_data)))
    if((pcity=find_city_by_id(punit->homecity)))
      if((pdialog=get_city_dialog(pcity)))  {
	if(e->type==ButtonRelease && e->xbutton.button==Button2)  {
	  activate_unit(punit);
	  close_city_dialog(pdialog);
	  return;
	}
	if(e->type==ButtonRelease && e->xbutton.button==Button3)  {
	  activate_unit(punit);
	  return;
	}
	popup_message_dialog(pdialog->shell,
			     "supportunitsdialog", 
			     unit_description(punit),
			     supported_units_activate_callback, punit->id, 1,
			     supported_units_activate_close_callback,
			                                     punit->id, 1,
			     present_units_disband_callback, punit->id, 1,
			     present_units_cancel_callback, 0, 0,
			     NULL);
      }
}


/****************************************************************
...
*****************************************************************/
static int units_scroll_maintenance(int nunits, int nshow, int *base,
				    Widget next, Widget prev)
{
  int adj_base=FALSE;
  int nextra;

  nextra=nunits-nshow;
  if (nextra<0) nextra=0;

  if (*base>nextra) {
    *base=nextra;
    adj_base=TRUE;
  }
  if (*base<0) {
    *base=0;
    adj_base=TRUE;
  }

  if (nextra<=0) {
    XtUnmapWidget(next);
    XtUnmapWidget(prev);
  } else {
    XtMapWidget(next);
    XtMapWidget(prev);
    XtSetSensitive(next, *base<nextra);
    XtSetSensitive(prev, *base>0);
  }

  return (adj_base);
}

/****************************************************************
...
*****************************************************************/
void city_dialog_update_supported_units(struct city_dialog *pdialog, 
					int unitid)
{
  struct unit_list *plist;
  int i, adj_base;
  struct genlist_iterator myiter;
  struct unit *punit;
  Widget pixcomm;

  if(pdialog->pcity->owner != game.player_idx) {
    plist = &(pdialog->pcity->info_units_supported);
  } else {
    plist = &(pdialog->pcity->units_supported);
  }

  adj_base=
    units_scroll_maintenance
    (
     plist->list.nelements,
     pdialog->num_units_shown,
     &(pdialog->support_unit_base),
     pdialog->support_unit_next_command,
     pdialog->support_unit_prev_command
    );

  genlist_iterator_init(&myiter,
			&(plist->list),
			pdialog->support_unit_base);

  for(i=0;
      i<pdialog->num_units_shown && ITERATOR_PTR(myiter);
      ITERATOR_NEXT(myiter), i++) {
    punit=(struct unit*)ITERATOR_PTR(myiter);
    pixcomm=pdialog->support_unit_pixcomms[i];

    if(!adj_base && unitid && punit->id!=unitid)
      continue;

    if (flags_are_transparent)
      XawPixcommClear(pixcomm); /* STG */
    put_unit_pixmap(punit,
		    XawPixcommPixmap(pixcomm), 
		    0, 0);
    put_unit_pixmap_city_overlays(punit,
				  XawPixcommPixmap(pixcomm));

    xaw_expose_now(pixcomm);

    XtRemoveAllCallbacks(pixcomm, XtNcallback);
    XtAddCallback(pixcomm, XtNcallback, 
		  support_units_callback, (XtPointer)punit->id);
    XtSetSensitive(pixcomm, TRUE);
  }

  for(; i<pdialog->num_units_shown; i++) {
    XawPixcommClear(pdialog->support_unit_pixcomms[i]);
    XtSetSensitive(pdialog->support_unit_pixcomms[i], FALSE);
  }
}

/****************************************************************
...
*****************************************************************/
void city_dialog_update_present_units(struct city_dialog *pdialog, int unitid)
{
  struct unit_list *plist;
  int i, adj_base;
  struct genlist_iterator myiter;
  struct unit *punit;
  Widget pixcomm;

  if(pdialog->pcity->owner != game.player_idx) {
    plist = &(pdialog->pcity->info_units_present);
  } else {
    plist = &(map_get_tile(pdialog->pcity->x, pdialog->pcity->y)->units);
  }

  adj_base=
    units_scroll_maintenance
    (
     plist->list.nelements,
     pdialog->num_units_shown,
     &(pdialog->present_unit_base),
     pdialog->present_unit_next_command,
     pdialog->present_unit_prev_command
    );

  genlist_iterator_init(&myiter, 
			&(plist->list),
			pdialog->present_unit_base);

  for(i=0;
      i<pdialog->num_units_shown && ITERATOR_PTR(myiter);
      ITERATOR_NEXT(myiter), i++) {
    punit=(struct unit*)ITERATOR_PTR(myiter);
    pixcomm=pdialog->present_unit_pixcomms[i];

    if(!adj_base && unitid && punit->id!=unitid)
      continue;

    if (flags_are_transparent)
      XawPixcommClear(pixcomm); /* STG */
    put_unit_pixmap(punit,
		    XawPixcommPixmap(pixcomm),
		    0, 0);

    xaw_expose_now(pixcomm);

    XtRemoveAllCallbacks(pixcomm, XtNcallback);
    XtAddCallback(pixcomm, XtNcallback, 
		  present_units_callback, (XtPointer)punit->id);
    XtSetSensitive(pixcomm, TRUE);
  }

  for(; i<pdialog->num_units_shown; i++) {
    XawPixcommClear(pdialog->present_unit_pixcomms[i]);
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
  
  my_snprintf(buf, sizeof(buf), _("%s - %s citizens"),
	      pdialog->pcity->name,
	      population_to_text(city_population(pdialog->pcity)));

  XtVaGetValues(pdialog->cityname_label, XtNlabel, &now, NULL);
  if(strcmp(now, buf) != 0) {
    XtVaSetValues(pdialog->cityname_label, XtNlabel, (XtArgVal)buf, NULL);
    xaw_horiz_center(pdialog->cityname_label);
    XtVaSetValues(pdialog->shell, XtNtitle, (XtArgVal)pdialog->pcity->name, NULL);
  }
}

/****************************************************************
...
*****************************************************************/
void city_dialog_update_improvement_list(struct city_dialog *pdialog)
{
  int n = 0, flag = 0;

  built_impr_iterate(pdialog->pcity, i) {
    if (!pdialog->improvlist_names_ptrs[n] ||
	strcmp(pdialog->improvlist_names_ptrs[n],
	       get_impr_name_ex(pdialog->pcity, i)) != 0)
      flag = 1;
    sz_strlcpy(pdialog->improvlist_names[n],
	       get_impr_name_ex(pdialog->pcity, i));
    pdialog->improvlist_names_ptrs[n] = pdialog->improvlist_names[n];
    n++;
  } built_impr_iterate_end;
  
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
void citydlg_btn_select_citymap(Widget w, XEvent *event)
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
    name=get_impr_name_ex(pdialog->pcity, pdialog->pcity->currently_building);
  }
  value=city_buy_cost(pdialog->pcity);
 
  if(game.player_ptr->economic.gold>=value) {
    my_snprintf(buf, sizeof(buf),
		_("Buy %s for %d gold?\nTreasury contains %d gold."), 
		name, value, game.player_ptr->economic.gold);
    popup_message_dialog(pdialog->shell, "buydialog", buf,
			 buy_callback_yes, pdialog, 0,
			 buy_callback_no, 0, 0,
			 NULL);
  }
  else {
    my_snprintf(buf, sizeof(buf),
		_("%s costs %d gold.\nTreasury contains %d gold."), 
		name, value, game.player_ptr->economic.gold);
    popup_message_dialog(pdialog->shell, "buynodialog", buf,
			 buy_callback_no, 0, 0,
			 NULL);
  }
  
}


/****************************************************************
...
*****************************************************************/
void unitupgrade_callback_yes(Widget w, XtPointer client_data, XtPointer call_data)
{
  struct unit *punit;

  if((punit=player_find_unit_by_id(game.player_ptr, (size_t)client_data))) {
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

  if((punit=player_find_unit_by_id(game.player_ptr, (size_t)client_data))) {
    ut1 = punit->type;
    /* printf("upgrade_callback for %s\n", unit_types[ut1].name); */

    ut2 = can_upgrade_unittype(game.player_ptr,ut1);

    if ( ut2 == -1 ) {
      /* this shouldn't generally happen, but it is conceivable */
      my_snprintf(buf, sizeof(buf),
		  _("Sorry: cannot upgrade %s."), unit_types[ut1].name);
      popup_message_dialog(toplevel, "upgradenodialog", buf,
                           unitupgrade_callback_no, 0, 0,
                           NULL);
    } else {
      value=unit_upgrade_price(game.player_ptr, ut1, ut2);

      if(game.player_ptr->economic.gold>=value) {
        my_snprintf(buf, sizeof(buf),
		    _("Upgrade %s to %s for %d gold?\n"
		      "Treasury contains %d gold."),
		    unit_types[ut1].name, unit_types[ut2].name,
		    value, game.player_ptr->economic.gold);
        popup_message_dialog(toplevel, "upgradedialog", buf,
                             unitupgrade_callback_yes, (XtPointer)(punit->id), 0,
                             unitupgrade_callback_no, 0, 0,
                             NULL);
      } else {
        my_snprintf(buf, sizeof(buf),
		    _("Upgrading %s to %s costs %d gold.\n"
		      "Treasury contains %d gold."),
		    unit_types[ut1].name, unit_types[ut2].name,
		    value, game.player_ptr->economic.gold);
        popup_message_dialog(toplevel, "upgradenodialog", buf,
                             unitupgrade_callback_no, 0, 0,
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
  static char *dummy_change_list[]={ 
    "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX  888 turns",
    "2",
    "3",
    "4",
    "5",
    "6",
    "7",
    "8",
    "9",
    "0",
    "1",
    "2",
    "3",
    "4",
    "5",
    0
  };

  Widget cshell, cform, clabel, cview, button_change, button_cancel, button_help;
  Position x, y;
  Dimension width, height;
  struct city_dialog *pdialog;
  int n, turns;
  
  pdialog=(struct city_dialog *)client_data;
  
  I_T(cshell=XtCreatePopupShell("changedialog", transientShellWidgetClass,
				pdialog->shell, NULL, 0));
  
  cform=XtVaCreateManagedWidget("dform", formWidgetClass, cshell, NULL);
  
  I_L(clabel=XtVaCreateManagedWidget("dlabel", labelWidgetClass, cform,
				     NULL));

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

  
  button_change = I_L(XtVaCreateManagedWidget("buttonchange",
					commandWidgetClass,
					cform,
					XtNfromVert, 
					cview,
					NULL));

  button_cancel = I_L(XtVaCreateManagedWidget("buttoncancel",
				    commandWidgetClass,
				    cform,
				    XtNfromVert, 
				    cview,
				    XtNfromHoriz,
				    button_change,
				    NULL));

  button_help = I_L(XtVaCreateManagedWidget("buttonhelp",
				    commandWidgetClass,
				    cform,
				    XtNfromVert, 
				    cview,
				    XtNfromHoriz,
				    button_cancel,
				    NULL));

  XtAddCallback(button_change, XtNcallback, 
		change_to_callback, (XtPointer)pdialog);
  XtAddCallback(button_cancel, XtNcallback, 
		change_no_callback, (XtPointer)pdialog);
  XtAddCallback(button_help, XtNcallback, 
		change_help_callback, (XtPointer)pdialog);

  

  XtVaGetValues(pdialog->shell, XtNwidth, &width, XtNheight, &height, NULL);
  XtTranslateCoords(pdialog->shell, (Position) width/6, (Position) height/10,
		    &x, &y);
  XtVaSetValues(cshell, XtNx, x, XtNy, y, NULL);
  
  XtPopup(cshell, XtGrabNone);
  
  XtSetSensitive(pdialog->shell, FALSE);

  n = 0;
  impr_type_iterate(i) {
    if(can_build_improvement(pdialog->pcity, i)) {
      if (i==B_CAPITAL) {
	my_snprintf(pdialog->change_list_names[n],
		    sizeof(pdialog->change_list_names[n]),
		    "%s (XX)",
		    get_impr_name_ex(pdialog->pcity, i));
      } else {
	turns = city_turns_to_build(pdialog->pcity, i, FALSE, TRUE);
	my_snprintf(pdialog->change_list_names[n],
		    sizeof(pdialog->change_list_names[n]),
		    PL_("%s (%d) %d turn", "%s (%d) %d turns", turns),
		    get_impr_name_ex(pdialog->pcity, i),
		    get_improvement_type(i)->build_cost, turns);
      }
      pdialog->change_list_names_ptrs[n]=pdialog->change_list_names[n];
      pdialog->change_list_ids[n++]=i;
    }
  } impr_type_iterate_end;
  
  pdialog->change_list_num_improvements=n;


  unit_type_iterate(i) {
    if(can_build_unit(pdialog->pcity, i)) {
      turns = city_turns_to_build(pdialog->pcity, i, TRUE, TRUE);
      my_snprintf(pdialog->change_list_names[n],
		  sizeof(pdialog->change_list_names[n]),
		  PL_("%s (%d) %d turn", "%s (%d) %d turns", turns),
		  get_unit_name(i), get_unit_type(i)->build_cost, turns);
      pdialog->change_list_names_ptrs[n]=pdialog->change_list_names[n];
      pdialog->change_list_ids[n++]=i;
    }
  } unit_type_iterate_end;
  
  pdialog->change_list_names_ptrs[n]=0;

  XawListChange(pdialog->change_list, pdialog->change_list_names_ptrs, 
		0, 0, False);
  /* force refresh of viewport so the scrollbar is added.
   * Buggy sun athena requires this */
  XtVaSetValues(cview, XtNforceBars, True, NULL);
}


/****************************************************************
  Display the city's worklist.
*****************************************************************/
void worklist_callback(Widget w, XtPointer client_data, XtPointer call_data)
{
  struct city_dialog *pdialog;
  
  pdialog = (struct city_dialog *)client_data;

  if (pdialog->worklist_shell) {
    XtPopup(pdialog->worklist_shell, XtGrabNone);
  } else {
    pdialog->worklist_shell = 
      popup_worklist(&pdialog->pcity->worklist, pdialog->pcity,
		     pdialog->shell, (void *) pdialog, commit_city_worklist,
		     cancel_city_worklist);
  }
}

/****************************************************************
  Commit the changes to the worklist for the city.
*****************************************************************/
void commit_city_worklist(struct worklist *pwl, void *data)
{
  struct packet_city_request packet;
  struct city_dialog *pdialog = (struct city_dialog *)data;
  int k, id, is_unit;

  /* Update the worklist.  Remember, though -- the current build 
     target really isn't in the worklist; don't send it to the 
     server as part of the worklist.  Of course, we have to
     search through the current worklist to find the first
     _now_available_ build target (to cope with players who try
     mean things like adding a Battleship to a city worklist when
     the player doesn't even yet have the Map Making tech).  */

  for (k = 0; k < MAX_LEN_WORKLIST; k++) {
    int same_as_current_build;
    if (! worklist_peek_ith(pwl, &id, &is_unit, k))
      break;
    same_as_current_build = id == pdialog->pcity->currently_building
      && is_unit == pdialog->pcity->is_building_unit;

    /* Very special case: If we are currently building a wonder we
       allow the construction to continue, even if we the wonder is
       finished elsewhere, ie unbuildable. */
    if (k == 0 && !is_unit && is_wonder(id) && same_as_current_build) {
      worklist_remove(pwl, k);
      break;
    }

    /* If it can be built... */
    if (( is_unit && can_build_unit(pdialog->pcity, id)) ||
	(!is_unit && can_build_improvement(pdialog->pcity, id))) {
      /* ...but we're not yet building it, then switch. */
      if (!same_as_current_build) {

	/* Change the current target */
	packet.city_id=pdialog->pcity->id;
	packet.build_id = id;
	packet.is_build_id_unit_id = is_unit;
	send_packet_city_request(&aconnection, &packet, PACKET_CITY_CHANGE);
      }

      /* This item is now (and may have always been) the current
	 build target.  Drop it out of the worklist. */
      worklist_remove(pwl, k);
      break;
    }
  }

  /* Send the rest of the worklist on its way. */
  packet.city_id=pdialog->pcity->id;
  copy_worklist(&packet.worklist, pwl);
  packet.worklist.name[0] = '\0';
  send_packet_city_request(&aconnection, &packet, PACKET_CITY_WORKLIST);

  pdialog->worklist_shell = NULL;
}

void cancel_city_worklist(void *data) {
  struct city_dialog *pdialog = (struct city_dialog *)data;
  pdialog->worklist_shell = NULL;
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
    int n = 0;
    built_impr_iterate(pdialog->pcity, i) {
      if (n == ret->list_index) {
	char buf[512];

	if (is_wonder(i)) {
	  return;
	}

	pdialog->sell_id = i;
	my_snprintf(buf, sizeof(buf), _("Sell %s for %d gold?"),
		    get_impr_name_ex(pdialog->pcity, i),
		    improvement_value(i));

	popup_message_dialog(pdialog->shell, "selldialog", buf,
			     sell_callback_yes, pdialog, 0,
			     sell_callback_no, pdialog, 0, NULL);

	return;
      }
      n++;
    } built_impr_iterate_end;
  }
}

/****************************************************************
...
*****************************************************************/
void close_city_dialog(struct city_dialog *pdialog)
{
  if (pdialog->worklist_shell)
    XtDestroyWidget(pdialog->worklist_shell);

  XtDestroyWidget(pdialog->shell);
  genlist_unlink(&dialog_list, pdialog);

  free(pdialog->citizen_labels);
  free(pdialog->citizen_type);

  free(pdialog->support_unit_pixcomms);
  free(pdialog->present_unit_pixcomms);

  unit_list_iterate(pdialog->pcity->info_units_supported, psunit) {
    free(psunit);
  } unit_list_iterate_end;
  unit_list_unlink_all(&(pdialog->pcity->info_units_supported));
  unit_list_iterate(pdialog->pcity->info_units_present, psunit) {
    free(psunit);
  } unit_list_iterate_end;
  unit_list_unlink_all(&(pdialog->pcity->info_units_present));

  if(pdialog->is_modal)
    XtSetSensitive(toplevel, TRUE);
  free(pdialog);
}

/****************************************************************
...
*****************************************************************/
void citydlg_key_close(Widget w)
{
  citydlg_msg_close(XtParent(XtParent(w)));
}

/****************************************************************
...
*****************************************************************/
void citydlg_msg_close(Widget w)
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

char *newcitizen_labels[] = { N_("Workers"), N_("Scientists"), N_("Taxmen") };

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
  int i;

  if(cityopt_shell) {
    XtDestroyWidget(cityopt_shell);
  }
  cityopt_shell=create_cityopt_dialog(pcity->name);
  /* Doing this here makes the "No"'s centered consistently */
  for(i=0; i<NUM_CITYOPT_TOGGLES; i++) {
    bool state = is_city_option_set(pcity, i);
    XtVaSetValues(cityopt_toggles[i], XtNstate, state,
		  XtNlabel, state?_("Yes"):_("No"), NULL);
  }
  if (is_city_option_set(pcity, CITYO_NEW_EINSTEIN)) {
    newcitizen_index = 1;
  } else if (is_city_option_set(pcity, CITYO_NEW_TAXMAN)) {
    newcitizen_index = 2;
  } else {
    newcitizen_index = 0;
  }
  XtVaSetValues(cityopt_triggle, XtNstate, 1,
		XtNlabel, _(newcitizen_labels[newcitizen_index]),
		NULL);
  
  cityopt_city_id = pcity->id;

  xaw_set_relative_position(pdialog->shell, cityopt_shell, 15, 15);
  XtPopup(cityopt_shell, XtGrabNone);
}


/**************************************************************************
...
**************************************************************************/
Widget create_cityopt_dialog(char *city_name)
{
  Widget shell, form, label, ok, cancel;
  int i;

  shell = I_T(XtCreatePopupShell("cityoptpopup",
				 transientShellWidgetClass,
				 toplevel, NULL, 0));
  form = XtVaCreateManagedWidget("cityoptform", 
				 formWidgetClass, 
				 shell, NULL);
  label = XtVaCreateManagedWidget("cityoptlabel", labelWidgetClass,
				  form, XtNlabel, city_name, NULL);


  I_L(XtVaCreateManagedWidget("cityoptnewcitlabel", labelWidgetClass, 
			      form, NULL));
  
  cityopt_triggle = XtVaCreateManagedWidget("cityoptnewcittriggle", 
					    toggleWidgetClass, 
					    form, NULL);

  /* NOTE: the ordering here is deliberately out of order;
     want toggles[] to be in enum city_options order, but
     want display in different order. --dwp
     - disband and workers options at top
     - helicopters (special case air) at bottom
  */

  I_L(XtVaCreateManagedWidget("cityoptdisbandlabel", labelWidgetClass, 
			      form, NULL));
  
  cityopt_toggles[4] = XtVaCreateManagedWidget("cityoptdisbandtoggle", 
					      toggleWidgetClass, 
					      form, NULL);

  I_L(XtVaCreateManagedWidget("cityoptvlandlabel", labelWidgetClass, 
			      form, NULL));
  
  cityopt_toggles[0] = XtVaCreateManagedWidget("cityoptvlandtoggle", 
					      toggleWidgetClass, 
					      form, NULL);
  
  I_L(XtVaCreateManagedWidget("cityoptvsealabel", labelWidgetClass, 
			      form, NULL));
  
  cityopt_toggles[1] = XtVaCreateManagedWidget("cityoptvseatoggle", 
					      toggleWidgetClass, 
					      form, NULL);
  
  I_L(XtVaCreateManagedWidget("cityoptvairlabel", labelWidgetClass, 
			      form, NULL));
  
  cityopt_toggles[3] = XtVaCreateManagedWidget("cityoptvairtoggle", 
					      toggleWidgetClass, 
					      form, NULL);
  
  I_L(XtVaCreateManagedWidget("cityoptvhelilabel", labelWidgetClass, 
			      form, NULL));
  
  cityopt_toggles[2] = XtVaCreateManagedWidget("cityoptvhelitoggle", 
					      toggleWidgetClass, 
					      form, NULL);

  ok = I_L(XtVaCreateManagedWidget("cityoptokcommand",
				   commandWidgetClass,
				   form, NULL));
  
  cancel = I_L(XtVaCreateManagedWidget("cityoptcancelcommand",
				       commandWidgetClass,
				       form, NULL));

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
    int i, new_options;
    Boolean b;
    
    new_options = 0;
    for(i=0; i<NUM_CITYOPT_TOGGLES; i++)  {
      XtVaGetValues(cityopt_toggles[i], XtNstate, &b, NULL);
      if (b) new_options |= (1<<i);
    }
    if (newcitizen_index == 1) {
      new_options |= (1<<CITYO_NEW_EINSTEIN);
    } else if (newcitizen_index == 2) {
      new_options |= (1<<CITYO_NEW_TAXMAN);
    }
    packet.value1 = cityopt_city_id;
    packet.value2 = new_options;
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
		XtNlabel, _(newcitizen_labels[newcitizen_index]),
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
