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
#include <stdarg.h>
#include <assert.h>

#include <X11/Intrinsic.h>
#include <X11/StringDefs.h>
#include <X11/Xaw/Form.h>
#include <X11/Xaw/Label.h>
#include <X11/Xaw/Command.h>
#include <X11/Xaw/List.h>
#include <X11/Xaw/MenuButton.h>
#include <X11/Xaw/SimpleMenu.h>
#include <X11/Xaw/SmeBSB.h>
#include <X11/Xaw/Toggle.h>
#include <X11/Xaw/Viewport.h>

#include <shared.h>
#include <gui_stuff.h>
#include <game.h>
#include <unit.h>
#include <city.h>
#include <packets.h>
#include <repodlgs.h>
#include <chatline.h>
#include <mapview.h>
#include <citydlg.h>
#include <optiondlg.h>
#include <log.h>
#include <cityrep.h>
#include <mem.h>
#include <options.h>

extern Widget toplevel, main_form;

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
  static char buf[REPORT_CITYNAME_ABBREV+1];
  if (strlen(pcity->name) <= REPORT_CITYNAME_ABBREV) {
    return pcity->name;
  } else {
    strncpy(buf, pcity->name, REPORT_CITYNAME_ABBREV-1);
    buf[REPORT_CITYNAME_ABBREV-1] = '.';
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
  sprintf(buf,"%d/%d",
	  pcity->food_stock,
	  (pcity->size+1) * game.foodbox);
  return buf;
}

static char *cr_entry_pollution(struct city *pcity)
{
  static char buf[8];
  sprintf(buf,"%3d", pcity->pollution);
  return buf;
}

static char *cr_entry_num_trade(struct city *pcity)
{
  static char buf[8];
  sprintf(buf,"%d", city_num_trade_routes(pcity));
  return buf;
}

static char *cr_entry_building(struct city *pcity)
{
  static char buf[64];
  if(pcity->is_building_unit)  {
    sprintf(buf, "%s(%d/%d/%d)", 
            get_unit_type(pcity->currently_building)->name,
	    pcity->shield_stock,
	    get_unit_type(pcity->currently_building)->build_cost,
	    city_buy_cost(pcity));
  } else {
    if(pcity->currently_building==B_CAPITAL)  {
      sprintf(buf, "%s(%d/X/X)",
              get_imp_name_ex(pcity, pcity->currently_building),
	      pcity->shield_stock);
    } else {
      sprintf(buf, "%s(%d/%d/%d)", 
	      get_imp_name_ex(pcity, pcity->currently_building),
	      pcity->shield_stock,
	      get_improvement_type(pcity->currently_building)->build_cost,
	      city_buy_cost(pcity));
    }
  }
  return buf;
}

static char *cr_entry_corruption(struct city *pcity)
{
  static char buf[8];
  sprintf(buf,"%3d", pcity->corruption);
  return buf;
}

/* City report options (which columns get shown)
 * To add a new entry, you should just have to:
 * - add a function like those above
 * - add an entry in the city_report_specs[] table
 */

struct city_report_spec {
  int show;			/* modify this to customize */
  int width;			/* 0 means variable; rightmost only */
  int space;			/* number of leading spaces (see below) */
  char *title1;
  char *title2;
  char *explanation;
  char *(*func)(struct city*);
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

static struct city_report_spec city_report_specs[] = {
  { 1,-15, 0, "",  "Name",            "City Name",
                                      FUNC_TAG(cityname) },
  { 0,  2, 1, "",  "Sz",              "Size",
                                      FUNC_TAG(size) },
  { 1, -8, 1, "",  "State",           "Rapture/Peace/Disorder",
                                      FUNC_TAG(hstate_verbose) },
  { 0,  1, 1, "",  "",                "Concise *=Rapture, X=Disorder",
                                      FUNC_TAG(hstate_concise) },
  { 1,  8, 1, "Workers", "H/C/U",     "Workers: Happy, Content, Unhappy",
                                      FUNC_TAG(workers) },
  { 0,  7, 1, "Special", "E/S/T",     "Entertainers, Scientists, Taxmen",
                                      FUNC_TAG(specialists) },
  { 1, 10, 1, "Surplus", "F/P/T",     "Surplus: Food, Production, Trade",
                                      FUNC_TAG(resources) },
  { 1, 10, 1, "Economy", "G/L/S",     "Economy: Gold, Luxuries, Science",
                                      FUNC_TAG(output) },
  { 0,  1, 1, "n", "T",               "Number of Trade Routes",
                                      FUNC_TAG(num_trade) },
  { 1,  7, 1, "Food", "Stock",        "Food Stock",
                                      FUNC_TAG(food) },
  { 0,  3, 1, "", "Pol",              "Pollution",
                                      FUNC_TAG(pollution) },
  { 0,  3, 1, "", "Cor",              "Corruption",
                                      FUNC_TAG(corruption) },
  { 1,  0, 1, "Currently Building",   "(Stock,Target,Buy Cost)",
                                      "Currently Building",
                                      FUNC_TAG(building) }
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
Widget config_shell;
Widget config_toggle[NUM_CREPORT_COLS];

void create_city_report_config_dialog(void);
void popup_city_report_config_dialog(void);
void config_ok_command_callback(Widget w, XtPointer client_data, 
				XtPointer call_data);


/******************************************************************/
void create_city_report_dialog(int make_modal);
void city_close_callback(Widget w, XtPointer client_data, 
			 XtPointer call_data);
void city_center_callback(Widget w, XtPointer client_data, 
			  XtPointer call_data);
void city_popup_callback(Widget w, XtPointer client_data, 
			 XtPointer call_data);
void city_buy_callback(Widget w, XtPointer client_data, 
		       XtPointer call_data);
void city_refresh_callback(Widget w, XtPointer client_data, 
		       XtPointer call_data);
void city_change_callback(Widget w, XtPointer client_data, 
			  XtPointer call_data);
void city_list_callback(Widget w, XtPointer client_data, 
			XtPointer call_data);
void city_config_callback(Widget w, XtPointer client_data, 
			  XtPointer call_data);

Widget city_form;
Widget city_dialog_shell;
Widget city_label;
Widget city_viewport;
Widget city_list, city_list_label;
Widget city_center_command, city_popup_command, city_buy_command,
       city_refresh_command, city_config_command;
Widget city_change_command, city_popupmenu;

int city_dialog_shell_is_modal;
struct city **cities_in_list = NULL;

static char *dummy_city_list[]={ 
  "    "
  " ",  " ",  " ",  " ",  " ",  " ",  " ",  " ",  " ",  " ",  " ",  " ",  " ",
  " ",  " ",  " ",  0
};


/****************************************************************
 Create the text for a line in the city report
*****************************************************************/
static void get_city_text(struct city *pcity, char *text)
{
  struct city_report_spec *spec;
  int i;

  text[0] = '\0';		/* init for strlen */
  for(i=0, spec=city_report_specs; i<NUM_CREPORT_COLS; i++, spec++) {
    if(!spec->show) continue;

    if(spec->space>0)
      sprintf(text+strlen(text), "%*s", spec->space, " ");

    sprintf(text+strlen(text), "%*s",
	    spec->width, (spec->func)(pcity));
  }
}

/****************************************************************
 Return text line for the column headers for the city report
*****************************************************************/
static char *get_city_table_header(void)
{
  static char text[400];
  struct city_report_spec *spec;
  int i, j;

  text[0] = '\0';		/* init for strlen */
  for(j=0; j<=1; j++) {
    for(i=0, spec=city_report_specs; i<NUM_CREPORT_COLS; i++, spec++) {
      if(!spec->show) continue;

      if(spec->space>0)
	sprintf(text+strlen(text), "%*s", spec->space, " ");

      sprintf(text+strlen(text), "%*s", spec->width,
	      (j?spec->title2:spec->title1));
    }
    if (j==0) strcat(text, "\n");
  }
  return text;
}

/****************************************************************

                      CITY REPORT DIALOG
 
****************************************************************/

/****************************************************************
...
****************************************************************/
void popup_city_report_dialog(int make_modal)
{
  if(!city_dialog_shell) {
      Position x, y;
      Dimension width, height;
      
      city_dialog_shell_is_modal=make_modal;
    
      if(make_modal)
	XtSetSensitive(main_form, FALSE);
      
      create_city_report_dialog(make_modal);
      
      XtVaGetValues(toplevel, XtNwidth, &width, XtNheight, &height, NULL);
      
      XtTranslateCoords(toplevel, (Position) width/10, (Position) height/10,
			&x, &y);
      XtVaSetValues(city_dialog_shell, XtNx, x, XtNy, y, NULL);
      
      XtPopup(city_dialog_shell, XtGrabNone);

      /* force refresh of viewport so the scrollbar is added.
       * Buggy sun athena requires this */
      XtVaSetValues(city_viewport, XtNforceBars, True, NULL);
   }
}


/****************************************************************
...
*****************************************************************/
void create_city_report_dialog(int make_modal)
{
  Widget close_command;
  char *report_title;
  
  city_dialog_shell = XtVaCreatePopupShell("reportcitypopup", 
					   make_modal ? 
					   transientShellWidgetClass :
					   topLevelShellWidgetClass,
					   toplevel, 
					   0);

  city_form = XtVaCreateManagedWidget("reportcityform", 
				      formWidgetClass,
				      city_dialog_shell,
				      NULL);   

  report_title=get_report_title("City Advisor");
  city_label = XtVaCreateManagedWidget("reportcitylabel", 
				       labelWidgetClass, 
				       city_form,
				       XtNlabel, 
				       report_title,
				       NULL);
  free(report_title);
  city_list_label = XtVaCreateManagedWidget("reportcitylistlabel", 
				            labelWidgetClass, 
				            city_form,
					    XtNlabel,
					    get_city_table_header(),
				            NULL);
  city_viewport = XtVaCreateManagedWidget("reportcityviewport", 
				          viewportWidgetClass, 
				          city_form, 
				          NULL);
  
  city_list = XtVaCreateManagedWidget("reportcitylist", 
				      listWidgetClass,
				      city_viewport,
                                      XtNlist,
				      (XtArgVal)dummy_city_list,
				      NULL);

  close_command = XtVaCreateManagedWidget("reportcityclosecommand", 
					  commandWidgetClass,
					  city_form,
					  NULL);
  
  city_center_command = XtVaCreateManagedWidget("reportcitycentercommand", 
						commandWidgetClass,
						city_form,
						NULL);

  city_popup_command = XtVaCreateManagedWidget("reportcitypopupcommand", 
					       commandWidgetClass,
					       city_form,
					       NULL);

  city_popupmenu = 0;

  city_buy_command = XtVaCreateManagedWidget("reportcitybuycommand", 
					     commandWidgetClass,
					     city_form,
					     NULL);

  city_change_command = XtVaCreateManagedWidget("reportcitychangemenubutton", 
						menuButtonWidgetClass,
						city_form,
						NULL);

  city_refresh_command = XtVaCreateManagedWidget("reportcityrefreshcommand",
					     commandWidgetClass,
					     city_form,
					     NULL);

  city_config_command = XtVaCreateManagedWidget("reportcityconfigcommand",
						commandWidgetClass,
						city_form,
						NULL);

  XtAddCallback(close_command, XtNcallback, city_close_callback, NULL);
  XtAddCallback(city_center_command, XtNcallback, city_center_callback, NULL);
  XtAddCallback(city_popup_command, XtNcallback, city_popup_callback, NULL);
  XtAddCallback(city_buy_command, XtNcallback, city_buy_callback, NULL);
  XtAddCallback(city_refresh_command, XtNcallback, city_refresh_callback, NULL);
  XtAddCallback(city_config_command, XtNcallback, city_config_callback, NULL);
  XtAddCallback(city_list, XtNcallback, city_list_callback, NULL);
  
  XtRealizeWidget(city_dialog_shell);
  city_report_dialog_update();
}

/****************************************************************
...
*****************************************************************/
void city_list_callback(Widget w, XtPointer client_data, 
			 XtPointer call_data)
{
  XawListReturnStruct *ret=XawListShowCurrent(city_list);
  struct city *pcity;

  if(ret->list_index!=XAW_LIST_NONE && 
     (pcity=cities_in_list[ret->list_index])) {
    int flag;
    size_t i;
    char buf[512];

    XtSetSensitive(city_change_command, TRUE);
    XtSetSensitive(city_center_command, TRUE);
    XtSetSensitive(city_popup_command, TRUE);
    XtSetSensitive(city_buy_command, !pcity->did_buy && 
         !(!pcity->is_building_unit && pcity->currently_building==B_CAPITAL));
    if (city_popupmenu)
      XtDestroyWidget(city_popupmenu);

    city_popupmenu=XtVaCreatePopupShell("menu", 
				        simpleMenuWidgetClass, 
				        city_change_command,
				        NULL);
    flag = 0;
    for(i=0; i<B_LAST; i++)
      if(can_build_improvement(pcity, i)) {
	Widget entry;
	sprintf(buf,"%s (%d)", get_imp_name_ex(pcity, i),get_improvement_type(i)->build_cost);
	entry = XtVaCreateManagedWidget(buf, smeBSBObjectClass, city_popupmenu, NULL);
	XtAddCallback(entry, XtNcallback, city_change_callback, (XtPointer) i);
	flag=1;
      }

    for(i=0; i<U_LAST; i++)
      if(can_build_unit(pcity, i)) {
	Widget entry;
	sprintf(buf,"%s (%d)", 
		get_unit_name(i),get_unit_type(i)->build_cost);
	entry = XtVaCreateManagedWidget(buf, smeBSBObjectClass, 
					city_popupmenu, NULL);
	XtAddCallback(entry, XtNcallback, city_change_callback, 
		      (XtPointer) (i+B_LAST));
	flag = 1;
      }

    if(!flag)
      XtSetSensitive(city_change_command, FALSE);
  } else {
    XtSetSensitive(city_change_command, FALSE);
    XtSetSensitive(city_center_command, FALSE);
    XtSetSensitive(city_popup_command, FALSE);
    XtSetSensitive(city_buy_command, FALSE);
  }
}

/****************************************************************
...
*****************************************************************/
void city_change_callback(Widget w, XtPointer client_data, 
			 XtPointer call_data)
{
  XawListReturnStruct *ret=XawListShowCurrent(city_list);
  struct city *pcity;



  if(ret->list_index!=XAW_LIST_NONE && 
     (pcity=cities_in_list[ret->list_index])) {
    struct packet_city_request packet;
    int build_nr;
    Boolean unit;
      
    build_nr = (size_t) client_data;

    if (build_nr >= B_LAST) {
      build_nr -= B_LAST;
      unit = TRUE;
    } else {
      unit = FALSE;
    }

    packet.city_id=pcity->id;
    packet.name[0]='\0';
    packet.build_id=build_nr;
    packet.is_build_id_unit_id=unit;
    send_packet_city_request(&aconnection, &packet, PACKET_CITY_CHANGE);
  }
}

/****************************************************************
...
*****************************************************************/
void city_buy_callback(Widget w, XtPointer client_data, 
			 XtPointer call_data)
{
  XawListReturnStruct *ret=XawListShowCurrent(city_list);

  if(ret->list_index!=XAW_LIST_NONE) {
    struct city *pcity;
    if((pcity=cities_in_list[ret->list_index])) {
      int value;
      char *name;
      char buf[512];

      value=city_buy_cost(pcity);    
      if(pcity->is_building_unit)
	name=get_unit_type(pcity->currently_building)->name;
      else
	name=get_imp_name_ex(pcity, pcity->currently_building);

      if (game.player_ptr->economic.gold >= value)
	{
	  struct packet_city_request packet;
	  packet.city_id=pcity->id;
	  packet.name[0]='\0';
	  send_packet_city_request(&aconnection, &packet, PACKET_CITY_BUY);
	}
      else
	{
	  sprintf(buf, "Game: %s costs %d gold and you only have %d gold.",
		  name,value,game.player_ptr->economic.gold);
	  append_output_window(buf);
	}
    }
  }
}

/****************************************************************
...
*****************************************************************/
void city_refresh_callback(Widget w, XtPointer client_data, XtPointer call_data)
{ /* added by Syela - I find this very useful */
  XawListReturnStruct *ret=XawListShowCurrent(city_list);
  struct city *pcity;
  struct packet_generic_integer packet;

  if (ret->list_index!=XAW_LIST_NONE) {
    if ((pcity=cities_in_list[ret->list_index])) {
      packet.value = pcity->id;
      send_packet_generic_integer(&aconnection, PACKET_CITY_REFRESH, &packet);
    }
  } else {
    packet.value = 0;
    send_packet_generic_integer(&aconnection, PACKET_CITY_REFRESH, &packet);
  }
}

/****************************************************************
...
*****************************************************************/
void city_close_callback(Widget w, XtPointer client_data, 
			 XtPointer call_data)
{

  if(city_dialog_shell_is_modal)
     XtSetSensitive(main_form, TRUE);
   XtDestroyWidget(city_dialog_shell);
   city_dialog_shell=0;
}

/****************************************************************
...
*****************************************************************/
void city_center_callback(Widget w, XtPointer client_data, 
			  XtPointer call_data)
{
  XawListReturnStruct *ret=XawListShowCurrent(city_list);

  if(ret->list_index!=XAW_LIST_NONE) {
    struct city *pcity;
    if((pcity=cities_in_list[ret->list_index]))
      center_tile_mapcanvas(pcity->x, pcity->y);
  }
}

/****************************************************************
...
*****************************************************************/
void city_popup_callback(Widget w, XtPointer client_data, 
			 XtPointer call_data)
{
  XawListReturnStruct *ret=XawListShowCurrent(city_list);

  if(ret->list_index!=XAW_LIST_NONE) {
    struct city *pcity;
    if((pcity=cities_in_list[ret->list_index])) {
      if (center_when_popup_city) {
	center_tile_mapcanvas(pcity->x, pcity->y);
      }
      popup_city_dialog(pcity, 0);
    }
  }
}

/****************************************************************
...
*****************************************************************/
void city_config_callback(Widget w, XtPointer client_data,
			  XtPointer call_data)
{
  popup_city_report_config_dialog();
}

/****************************************************************
...
*****************************************************************/
void city_report_dialog_update(void)
{
  if(delay_report_update) return;
  if(city_dialog_shell) {
    int i=0, n;
    Dimension width;
    static int n_alloc = 0;
    static char **city_list_text = NULL;
    char *report_title;

    n = city_list_size(&game.player_ptr->cities);
    freelog(LOG_DEBUG, "%d cities in report", n);
    if(n_alloc == 0 || n > n_alloc) {
      int j, n_prev = n_alloc;
      
      n_alloc *= 2;
      if (!n_alloc || n_alloc < n) n_alloc = n + 1;
      freelog(LOG_DEBUG, "city report n_alloc increased to %d", n_alloc);
      cities_in_list = fc_realloc(cities_in_list,
				  n_alloc*sizeof(*cities_in_list));
      city_list_text = fc_realloc(city_list_text, n_alloc*sizeof(char*));
      for(j=n_prev; j<n_alloc; j++)  city_list_text[j] = malloc(128);
    }
       
    report_title=get_report_title("City Advisor");
    xaw_set_label(city_label, report_title);
    free(report_title);

    xaw_set_label(city_list_label, get_city_table_header());
    
    if (city_popupmenu) {
      XtDestroyWidget(city_popupmenu);
      city_popupmenu = 0;
    }    

    /* Only sort once, in case any cities have duplicate/truncated names.
     * Plus this should be much faster than sorting on ids which means
     * having to find city corresponding to id for each comparison.
     */
    i=0;
    city_list_iterate(game.player_ptr->cities, pcity) {
      cities_in_list[i++] = pcity;
    } city_list_iterate_end;
    assert(i==n);
    qsort(cities_in_list, n, sizeof(struct city*), city_name_compare);
    for(i=0; i<n; i++) {
      get_city_text(cities_in_list[i], city_list_text[i]);
    }
    i = n;
    if(!n) {
      strcpy(city_list_text[0], 
	     "                                                             ");
      i=1;
      cities_in_list[0]=NULL;
    }

    XawFormDoLayout(city_form, False);
    XawListChange(city_list, city_list_text, i, 0, True);

    XtVaGetValues(city_list, XtNlongest, &i, NULL);
    width=i+10;
    /* I don't know the proper way to set the width of this viewport widget.
       Someone who knows is more than welcome to fix this */
    XtVaSetValues(city_viewport, XtNwidth, width+15, NULL); 
    XtVaSetValues(city_list_label, XtNwidth, width, NULL);
    XtVaSetValues(city_label, XtNwidth, width+15, NULL);
    XawFormDoLayout(city_form, True);

    XtSetSensitive(city_change_command, FALSE);
    XtSetSensitive(city_center_command, FALSE);
    XtSetSensitive(city_popup_command, FALSE);
    XtSetSensitive(city_buy_command, FALSE);
  }
}

/****************************************************************
  Update the text for a single city in the city report
*****************************************************************/
void city_report_dialog_update_city(struct city *pcity)
{
  int i;

  if(delay_report_update) return;
  if(!city_dialog_shell) return;

  for(i=0; cities_in_list[i]; i++)  {
    if(cities_in_list[i]==pcity)  {
      int n;
      String *list;
      Dimension w;
      char new_city_line[200];

      XtVaGetValues(city_list, XtNnumberStrings, &n, XtNlist, &list, NULL);
      if(strncmp(pcity->name,list[i],
		 MIN(strlen(pcity->name),REPORT_CITYNAME_ABBREV-1))) {
	 break;
      }
      get_city_text(pcity,new_city_line);
      if(strcmp(new_city_line, list[i])==0) return; /* no change */
      strcpy(list[i], new_city_line);

      /* It seems really inefficient to regenerate the whole list just to
         change one line.  It's also annoying to have to set the size
	 of each widget explicitly, since Xt is supposed to handle that. */
      XawFormDoLayout(city_form, False);
      XawListChange(city_list, list, n, 0, False);
      XtVaGetValues(city_list, XtNlongest, &n, NULL);
      w=n+10;
      XtVaSetValues(city_viewport, XtNwidth, w+15, NULL);
      XtVaSetValues(city_list_label, XtNwidth, w, NULL);
      XtVaSetValues(city_label, XtNwidth, w+15, NULL);
      XawFormDoLayout(city_form, True);
      return;
    };
  }
  city_report_dialog_update();
}

/****************************************************************

                      CITY REPORT CONFIGURE DIALOG
 
****************************************************************/

/****************************************************************
... 
*****************************************************************/
void popup_city_report_config_dialog(void)
{
  int i;

  if(config_shell)
    return;
  
  create_city_report_config_dialog();

  for(i=1; i<NUM_CREPORT_COLS; i++) {
    XtVaSetValues(config_toggle[i],
		  XtNstate, city_report_specs[i].show,
		  XtNlabel, city_report_specs[i].show?"Yes":"No", NULL);
  }

  xaw_set_relative_position(toplevel, config_shell, 25, 25);
  XtPopup(config_shell, XtGrabNone);
  /* XtSetSensitive(main_form, FALSE); */
}

/****************************************************************
...
*****************************************************************/
void create_city_report_config_dialog(void)
{
  Widget config_form, config_label, config_ok_command;
  Widget config_optlabel=0, above;
  struct city_report_spec *spec;
  char buf[64];
  int i;
  
  config_shell = XtCreatePopupShell("cityconfig", 
				    transientShellWidgetClass,
				    toplevel, NULL, 0);

  config_form = XtVaCreateManagedWidget("cityconfigform", 
				        formWidgetClass, 
				        config_shell, NULL);   

  config_label = XtVaCreateManagedWidget("cityconfiglabel", 
					 labelWidgetClass, 
					 config_form, NULL);   

  for(i=1, spec=city_report_specs+i; i<NUM_CREPORT_COLS; i++, spec++) {
    sprintf(buf, "%-32s", spec->explanation);
    above = (i==1)?config_label:config_optlabel;

    config_optlabel = XtVaCreateManagedWidget("cityconfiglabel", 
					      labelWidgetClass,
					      config_form,
					      XtNlabel, buf,
					      XtNfromVert, above,
					      NULL);
    
    config_toggle[i] = XtVaCreateManagedWidget("cityconfigtoggle", 
					       toggleWidgetClass, 
					       config_form,
					       XtNfromVert, above,
					       XtNfromHoriz, config_optlabel,
					       NULL);
  }

  config_ok_command = XtVaCreateManagedWidget("cityconfigokcommand", 
					      commandWidgetClass,
					      config_form,
					      XtNfromVert, config_optlabel,
					      NULL);
  
  XtAddCallback(config_ok_command, XtNcallback, 
		config_ok_command_callback, NULL);

  for(i=1; i<NUM_CREPORT_COLS; i++) 
    XtAddCallback(config_toggle[i], XtNcallback, toggle_callback, NULL);
  
  XtRealizeWidget(config_shell);

  xaw_horiz_center(config_label);
}

/**************************************************************************
...
**************************************************************************/
void config_ok_command_callback(Widget w, XtPointer client_data, 
				XtPointer call_data)
{
  struct city_report_spec *spec;
  Boolean b;
  int i;
  
  XtDestroyWidget(config_shell);

  for(i=1, spec=city_report_specs+i; i<NUM_CREPORT_COLS; i++, spec++) {
    XtVaGetValues(config_toggle[i], XtNstate, &b, NULL);
    spec->show = b;
  }
  config_shell=0;
  city_report_dialog_update();
}

