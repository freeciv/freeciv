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
#include <assert.h>

#include <X11/Intrinsic.h>
#include <X11/StringDefs.h>
#include <X11/Xaw/Form.h>
#include <X11/Xaw/Label.h>
#include <X11/Xaw/Command.h>
#include <X11/Xaw/List.h>
#include <X11/Xaw/SimpleMenu.h>
#include <X11/Xaw/MenuButton.h>
#include <X11/Xaw/SimpleMenu.h>
#include <X11/Xaw/SmeBSB.h>
#include <X11/Xaw/SmeLine.h>
#include <X11/Xaw/AsciiText.h>  
#include <X11/Xaw/Viewport.h>

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
#include "diptreaty.h"
#include "gui_stuff.h"
#include "mapview.h"

#include "diplodlg.h"

extern Widget toplevel;

#define MAX_NUM_CLAUSES 64

struct Diplomacy_dialog {
  struct Treaty treaty;
  
  Widget dip_dialog_shell;
  Widget dip_form, dip_main_form, dip_form0, dip_formm, dip_form1;
  Widget dip_view;
  
  Widget dip_headline0;
  Widget dip_headlinem;
  Widget dip_headline1;

  Widget dip_map_menubutton0;
  Widget dip_map_menubutton1;
  Widget dip_tech_menubutton0;
  Widget dip_tech_menubutton1;
  Widget dip_city_menubutton0;
  Widget dip_city_menubutton1;
  Widget dip_gold_label0;
  Widget dip_gold_label1;
  Widget dip_gold_input0;
  Widget dip_gold_input1;
  
  Widget dip_label;
  Widget dip_clauselabel;
  Widget dip_clauselist;
  Widget dip_acceptlabel0;
  Widget dip_acceptthumb0;
  Widget dip_acceptlabel1;
  Widget dip_acceptthumb1;
  
  Widget dip_accept_command;
  Widget dip_close_command;

  Widget dip_erase_clause_command;
  
  char clauselist_strings[MAX_NUM_CLAUSES+1][64];
  char *clauselist_strings_ptrs[MAX_NUM_CLAUSES+1];
};

char *dummy_clause_list_strings[]={ "\n", "\n", "\n", "\n", "\n", "\n", 0};

struct Diplomacy_dialog *create_diplomacy_dialog(struct player *plr0, 
						 struct player *plr1);

struct genlist diplomacy_dialogs;
int diplomacy_dialogs_list_has_been_initialised;

struct Diplomacy_dialog *find_diplomacy_dialog(struct player *plr0, 
					       struct player *plr1);
void popup_diplomacy_dialog(struct player *plr0, struct player *plr1);
void diplomacy_dialog_close_callback(Widget w, XtPointer client_data, 
				    XtPointer call_data);
void diplomacy_dialog_map_callback(Widget w, XtPointer client_data, 
				   XtPointer call_data);
void diplomacy_dialog_seamap_callback(Widget w, XtPointer client_data, 
				      XtPointer call_data);
void diplomacy_dialog_erase_clause_callback(Widget w, XtPointer client_data, 
					    XtPointer call_data);
void diplomacy_dialog_accept_callback(Widget w, XtPointer client_data, 
				      XtPointer call_data);
void diplomacy_dialog_tech_callback(Widget w, XtPointer client_data, 
				    XtPointer call_data);
void diplomacy_dialog_city_callback(Widget w, XtPointer client_data, 
				    XtPointer call_data);
void close_diplomacy_dialog(struct Diplomacy_dialog *pdialog);
void update_diplomacy_dialog(struct Diplomacy_dialog *pdialog);


/****************************************************************
...
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




/****************************************************************
popup the dialog 10% inside the main-window 
*****************************************************************/
void popup_diplomacy_dialog(struct player *plr0, struct player *plr1)
{
  Position x, y;
  Dimension width, height;
  struct Diplomacy_dialog *pdialog;
  
  if(!(pdialog=find_diplomacy_dialog(plr0, plr1))) {
    pdialog=create_diplomacy_dialog(plr0, plr1);
    XtVaGetValues(toplevel, XtNwidth, &width, XtNheight, &height, NULL);
    XtTranslateCoords(toplevel, (Position) width/10, (Position) height/10,
		      &x, &y);
    XtVaSetValues(pdialog->dip_dialog_shell, XtNx, x, XtNy, y, NULL);
  }

  XtPopup(pdialog->dip_dialog_shell, XtGrabNone);
}


/****************************************************************
...
*****************************************************************/
static int fill_diplomacy_tech_menu(Widget popupmenu, 
				    struct player *plr0, struct player *plr1)
{
  int i, flag;
  
  for(i=1, flag=0; i<A_LAST; i++) {
    if(get_invention(plr0, i)==TECH_KNOWN && 
       (get_invention(plr1, i)==TECH_UNKNOWN || 
	get_invention(plr1, i)==TECH_REACHABLE)) {
      Widget entry=
	XtVaCreateManagedWidget(advances[i].name, smeBSBObjectClass, 
				popupmenu, NULL);
      XtAddCallback(entry, XtNcallback, diplomacy_dialog_tech_callback, 
		    (XtPointer)(plr0->player_no*10000+plr1->player_no*100+i)); 
      flag=1;
    }
  }
  return flag;
}

/****************************************************************

Creates a sorted list of plr0's cities, excluding the capital and
any cities not visible to plr1.  This means that you can only trade 
cities visible to requesting player.  

                            - Kris Bubendorfer
*****************************************************************/
static int fill_diplomacy_city_menu(Widget popupmenu, 
				    struct player *plr0, struct player *plr1)
{
  int i = 0, j = 0, n = city_list_size(&plr0->cities);
  struct city **city_list_ptrs = fc_malloc(sizeof(struct city*)*n);

  city_list_iterate(plr0->cities, pcity) {
    if(!city_got_effect(pcity, B_PALACE)){
      city_list_ptrs[i] = pcity;
      i++;
    }
  } city_list_iterate_end;

  qsort(city_list_ptrs, i, sizeof(struct city*), city_name_compare);
  
  for(j=0; j<i; j++) {
    Widget entry=
      XtVaCreateManagedWidget(city_list_ptrs[j]->name, smeBSBObjectClass, 
			      popupmenu, NULL);
    XtAddCallback(entry, XtNcallback, diplomacy_dialog_city_callback, 
		  (XtPointer)(city_list_ptrs[j]->id*1024
			      + plr0->player_no*32
			      + plr1->player_no));
  }
  return i;
}


/****************************************************************
...
*****************************************************************/
struct Diplomacy_dialog *create_diplomacy_dialog(struct player *plr0, 
						 struct player *plr1)
{
  char buf[512], *pheadlinem;
  struct Diplomacy_dialog *pdialog;
  Dimension width, height, maxwidth;
  Widget popupmenu;
  Widget entry;
  XtTranslations textfieldtranslations;
  
  pdialog=fc_malloc(sizeof(struct Diplomacy_dialog));
  genlist_insert(&diplomacy_dialogs, pdialog, 0);
  
  init_treaty(&pdialog->treaty, plr0, plr1);
  
  pdialog->dip_dialog_shell = XtCreatePopupShell("dippopupshell", 
						 topLevelShellWidgetClass,
						 toplevel, NULL, 0);

  pdialog->dip_form = XtVaCreateManagedWidget("dipform", 
					      formWidgetClass, 
					      pdialog->dip_dialog_shell,
					      NULL);

  pdialog->dip_main_form = XtVaCreateManagedWidget("dipmainform", 
						   formWidgetClass, 
						   pdialog->dip_form,
						   NULL);
  
  pdialog->dip_form0 = XtVaCreateManagedWidget("dipform0", 
					       formWidgetClass, 
					       pdialog->dip_main_form, 
					       NULL);
  
  pdialog->dip_formm = XtVaCreateManagedWidget("dipformm", 
					       formWidgetClass, 
					       pdialog->dip_main_form, 
					       NULL);

  pdialog->dip_form1 = XtVaCreateManagedWidget("dipform1", 
					       formWidgetClass, 
					       pdialog->dip_main_form, 
					       NULL);
  
  sprintf(buf, "The %s offerings", get_nation_name(plr0->nation));
  pdialog->dip_headline0=XtVaCreateManagedWidget("dipheadline0", 
						 labelWidgetClass, 
						 pdialog->dip_form0, 
						 XtNlabel, buf,
						 NULL);   

  sprintf(buf, "The %s offerings", get_nation_name(plr1->nation));
  pdialog->dip_headline1=XtVaCreateManagedWidget("dipheadline1", 
						 labelWidgetClass, 
						 pdialog->dip_form1, 
						 XtNlabel, buf,
						 NULL);   

  
  pdialog->dip_map_menubutton0=XtVaCreateManagedWidget("dipmapmenubutton0", 
						       menuButtonWidgetClass, 
						       pdialog->dip_form0, 
						       NULL);
  popupmenu=XtVaCreatePopupShell("menu", 
				 simpleMenuWidgetClass, 
				 pdialog->dip_map_menubutton0, 
				 NULL);
  
  entry=XtVaCreateManagedWidget("World-map", smeBSBObjectClass, popupmenu, NULL);
  XtAddCallback(entry, XtNcallback, diplomacy_dialog_map_callback, (XtPointer)pdialog);
  entry=XtVaCreateManagedWidget("Sea-map", smeBSBObjectClass, popupmenu, NULL);
  XtAddCallback(entry, XtNcallback, diplomacy_dialog_seamap_callback, (XtPointer)pdialog);
  
  pdialog->dip_map_menubutton1=XtVaCreateManagedWidget("dipmapmenubutton1", 
						       menuButtonWidgetClass, 
						       pdialog->dip_form1, 
						       NULL);
  popupmenu=XtVaCreatePopupShell("menu", 
				 simpleMenuWidgetClass, 
				 pdialog->dip_map_menubutton1, 
				 NULL);
  entry=XtVaCreateManagedWidget("World-map", smeBSBObjectClass, popupmenu, NULL);
  XtAddCallback(entry, XtNcallback, diplomacy_dialog_map_callback, (XtPointer)pdialog);
  entry=XtVaCreateManagedWidget("Sea-map", smeBSBObjectClass, popupmenu, NULL);
  XtAddCallback(entry, XtNcallback, diplomacy_dialog_seamap_callback, (XtPointer)pdialog);
  

  pdialog->dip_tech_menubutton0=XtVaCreateManagedWidget("diptechmenubutton0", 
							menuButtonWidgetClass,
							pdialog->dip_form0,
							NULL);
  popupmenu=XtVaCreatePopupShell("menu", 
				 simpleMenuWidgetClass, 
				 pdialog->dip_tech_menubutton0, 
				 NULL);

  if(!fill_diplomacy_tech_menu(popupmenu, plr0, plr1))
    XtSetSensitive(pdialog->dip_tech_menubutton0, FALSE);
  
  
  pdialog->dip_tech_menubutton1=XtVaCreateManagedWidget("diptechmenubutton1", 
							menuButtonWidgetClass,
							pdialog->dip_form1,
							NULL);
  popupmenu=XtVaCreatePopupShell("menu", 
				 simpleMenuWidgetClass, 
				 pdialog->dip_tech_menubutton1, 
				 NULL);
  if(!fill_diplomacy_tech_menu(popupmenu, plr1, plr0))
    XtSetSensitive(pdialog->dip_tech_menubutton1, FALSE);

  /* Start of trade city code - Kris Bubendorfer */

  pdialog->dip_city_menubutton0=XtVaCreateManagedWidget("dipcitymenubutton0", 
							menuButtonWidgetClass,
							pdialog->dip_form0,
							NULL);
  popupmenu=XtVaCreatePopupShell("menu", 
				 simpleMenuWidgetClass, 
				 pdialog->dip_city_menubutton0, 
				 NULL);
  
  XtSetSensitive(pdialog->dip_city_menubutton0, 
		 fill_diplomacy_city_menu(popupmenu, plr0, plr1));
  
  
  pdialog->dip_city_menubutton1=XtVaCreateManagedWidget("dipcitymenubutton1", 
							menuButtonWidgetClass,
							pdialog->dip_form1,
							NULL);
  popupmenu=XtVaCreatePopupShell("menu", 
				 simpleMenuWidgetClass, 
				 pdialog->dip_city_menubutton1, 
				 NULL);
  
  XtSetSensitive(pdialog->dip_city_menubutton1, 
		 fill_diplomacy_city_menu(popupmenu, plr1, plr0));  
  
  /* End of trade city code */
  
  pdialog->dip_gold_input0=XtVaCreateManagedWidget("dipgoldinput0", 
						   asciiTextWidgetClass,
						   pdialog->dip_form0,
						   NULL);

  pdialog->dip_gold_input1=XtVaCreateManagedWidget("dipgoldinput1", 
						   asciiTextWidgetClass,
						   pdialog->dip_form1,
						   NULL);
  
  sprintf(buf, "Gold(max %d)", plr0->economic.gold);
  pdialog->dip_gold_label0=XtVaCreateManagedWidget("dipgoldlabel0", 
						   labelWidgetClass,
						   pdialog->dip_form0,
						   XtNlabel, buf,
						   NULL);

  sprintf(buf, "Gold(max %d)", plr1->economic.gold);
  pdialog->dip_gold_label1=XtVaCreateManagedWidget("dipgoldlabel1", 
						   labelWidgetClass,
						   pdialog->dip_form1,
						   XtNlabel, buf,
						   NULL);

  
  sprintf(buf, "This Eternal Treaty\nmarks the results of the diplomatic work between\nThe %s %s %s\nand\nThe %s %s %s",
	  get_nation_name(plr0->nation),
	  get_ruler_title(plr0->government, plr0->is_male, plr0->nation),
	  plr0->name,
	  get_nation_name(plr1->nation),
	  get_ruler_title(plr1->government, plr0->is_male, plr0->nation),
	  plr1->name);
  
  pheadlinem=create_centered_string(buf);
  pdialog->dip_headline1=XtVaCreateManagedWidget("dipheadlinem", 
						 labelWidgetClass, 
						 pdialog->dip_formm,
						 XtNlabel, pheadlinem,
						 NULL);
  
  pdialog->dip_clauselabel=XtVaCreateManagedWidget("dipclauselabel",
						   labelWidgetClass, 
						   pdialog->dip_formm, 
						   NULL);   
  
  pdialog->dip_view =  XtVaCreateManagedWidget("dipview",
					       viewportWidgetClass, 
					       pdialog->dip_formm, 
					       NULL);
  
  
  pdialog->dip_clauselist = XtVaCreateManagedWidget("dipclauselist",
						    listWidgetClass, 
						    pdialog->dip_view, 
						    XtNlist, 
						    (XtArgVal)dummy_clause_list_strings,
						    NULL);

  XtVaGetValues(pdialog->dip_headline1, XtNwidth, &width, NULL);
  XtVaSetValues(pdialog->dip_view, XtNwidth, width, NULL); 
  XtVaSetValues(pdialog->dip_clauselist, XtNwidth, width, NULL); 

  sprintf(buf, "%s view:", get_nation_name(plr0->nation));
  pdialog->dip_acceptlabel0=XtVaCreateManagedWidget("dipacceptlabel0",
						    labelWidgetClass, 
						    pdialog->dip_formm, 
						    XtNlabel, buf,
						    NULL);
  pdialog->dip_acceptthumb0=XtVaCreateManagedWidget("dipacceptthumb0",
						    labelWidgetClass, 
						    pdialog->dip_formm, 
						    XtNbitmap, get_thumb_pixmap(0),
						    NULL);
  sprintf(buf, "%s view:", get_nation_name(plr1->nation));
  pdialog->dip_acceptlabel1=XtVaCreateManagedWidget("dipacceptlabel1",
						    labelWidgetClass, 
						    pdialog->dip_formm, 
						    XtNlabel, buf,
						    NULL);
  pdialog->dip_acceptthumb1=XtVaCreateManagedWidget("dipacceptthumb1",
						    labelWidgetClass, 
						    pdialog->dip_formm, 
						    NULL);

  
  pdialog->dip_erase_clause_command=XtVaCreateManagedWidget("diperaseclausecommand", 
							     commandWidgetClass, 
							     pdialog->dip_main_form, 
							     NULL);
  
  pdialog->dip_accept_command = XtVaCreateManagedWidget("dipacceptcommand", 
							commandWidgetClass, 
							pdialog->dip_form,
							NULL);


  pdialog->dip_close_command = XtVaCreateManagedWidget("dipclosecommand", 
						       commandWidgetClass,
						       pdialog->dip_form,
						       NULL);

  XtAddCallback(pdialog->dip_close_command, XtNcallback, 
		diplomacy_dialog_close_callback, (XtPointer)pdialog);
  XtAddCallback(pdialog->dip_erase_clause_command, XtNcallback, 
		diplomacy_dialog_erase_clause_callback, (XtPointer)pdialog);
  XtAddCallback(pdialog->dip_accept_command, XtNcallback, 
		diplomacy_dialog_accept_callback, (XtPointer)pdialog);
  

  textfieldtranslations = 
  XtParseTranslationTable("<Key>Return: diplo-dialog-returnkey()");
  XtOverrideTranslations(pdialog->dip_gold_input0, textfieldtranslations);
  XtOverrideTranslations(pdialog->dip_gold_input1, textfieldtranslations);
  
  XtRealizeWidget(pdialog->dip_dialog_shell);
  

  XtVaGetValues(pdialog->dip_map_menubutton0, XtNwidth, &maxwidth, NULL);
  XtVaGetValues(pdialog->dip_tech_menubutton0, XtNwidth, &width, NULL);
  XtVaGetValues(pdialog->dip_city_menubutton0, XtNwidth, &width, NULL);
  maxwidth=MAX(width, maxwidth);
  XtVaGetValues(pdialog->dip_gold_input0, XtNwidth, &width, NULL);
  maxwidth=MAX(width, maxwidth);
  XtVaSetValues(pdialog->dip_map_menubutton0, XtNwidth, maxwidth, NULL);
  XtVaSetValues(pdialog->dip_tech_menubutton0, XtNwidth, maxwidth, NULL);
  XtVaSetValues(pdialog->dip_city_menubutton0, XtNwidth, maxwidth, NULL);
  XtVaSetValues(pdialog->dip_gold_input0,  XtNwidth, maxwidth, NULL);
  
  XtVaGetValues(pdialog->dip_formm, XtNheight, &height, NULL);
  XtVaSetValues(pdialog->dip_form0, XtNheight, height, NULL); 
  XtVaSetValues(pdialog->dip_form1, XtNheight, height, NULL); 
  
  
  
  free(pheadlinem);

  update_diplomacy_dialog(pdialog);
  
  return pdialog;
}


/**************************************************************************
...
**************************************************************************/
void update_diplomacy_dialog(struct Diplomacy_dialog *pdialog)
{
  int i;
  struct genlist_iterator myiter;
  
  genlist_iterator_init(&myiter, &pdialog->treaty.clauses, 0);
  
  for(i=0; i<MAX_NUM_CLAUSES && ITERATOR_PTR(myiter); ITERATOR_NEXT(myiter)) {
    struct Clause *pclause=(struct Clause *)ITERATOR_PTR(myiter);
    
    switch(pclause->type) {
     case CLAUSE_ADVANCE:
      sprintf(pdialog->clauselist_strings[i], "The %s give %s",
	      get_nation_name_plural(pclause->from->nation),
	      advances[pclause->value].name);
      break;
    case CLAUSE_CITY:
      sprintf(pdialog->clauselist_strings[i], "The %s give %s",
	      get_nation_name_plural(pclause->from->nation),
	      find_city_by_id(pclause->value)->name);
      break;
     case CLAUSE_GOLD:
      sprintf(pdialog->clauselist_strings[i], "The %s give %d gold",
	      get_nation_name_plural(pclause->from->nation),
	      pclause->value);
      break;
     case CLAUSE_MAP: 
      sprintf(pdialog->clauselist_strings[i], "The %s give their worldmap",
	      get_nation_name_plural(pclause->from->nation));
      break;
     case CLAUSE_SEAMAP: 
      sprintf(pdialog->clauselist_strings[i], "The %s give their seamap",
	      get_nation_name_plural(pclause->from->nation));
      break;
    }
    pdialog->clauselist_strings_ptrs[i]=pdialog->clauselist_strings[i];
    i++;
  }

  pdialog->clauselist_strings_ptrs[i]=0;
  XawListChange(pdialog->dip_clauselist, pdialog->clauselist_strings_ptrs, 
		0, 0, False);

/* force refresh of viewport so that scrollbar is added
   sun seems to need this */ 
  XtVaSetValues(pdialog->dip_view, XtNforceBars, False, NULL);
  XtVaSetValues(pdialog->dip_view, XtNforceBars, True, NULL);

  xaw_set_bitmap(pdialog->dip_acceptthumb0,
		 get_thumb_pixmap(pdialog->treaty.accept0));
  xaw_set_bitmap(pdialog->dip_acceptthumb1, 
		 get_thumb_pixmap(pdialog->treaty.accept1));
}

/****************************************************************
...
*****************************************************************/
void diplomacy_dialog_tech_callback(Widget w, XtPointer client_data, 
				    XtPointer call_data)
{
  size_t choice;
  struct packet_diplomacy_info pa;
  
  choice=(size_t)client_data;

  pa.plrno0=choice/10000;
  pa.plrno1=(choice/100)%100;
  pa.clause_type=CLAUSE_ADVANCE;
  pa.plrno_from=pa.plrno0;
  pa.value=choice%100;
    
  send_packet_diplomacy_info(&aconnection, PACKET_DIPLOMACY_CREATE_CLAUSE,
			     &pa);
}

/****************************************************************
Callback for trading cities
                              - Kris Bubendorfer
*****************************************************************/
void diplomacy_dialog_city_callback(Widget w, XtPointer client_data, 
				    XtPointer call_data)
{
  size_t choice;
  struct packet_diplomacy_info pa;
  
  choice=(size_t)client_data;

  pa.value = choice/1024;
  choice -= pa.value * 1024;
  pa.plrno0 = choice/32;
  choice -= pa.plrno0 * 32;
  pa.plrno1 = choice;
 
  pa.clause_type=CLAUSE_CITY;
  pa.plrno_from=pa.plrno0;
    
  send_packet_diplomacy_info(&aconnection, PACKET_DIPLOMACY_CREATE_CLAUSE,
			     &pa);
}


/****************************************************************
...
*****************************************************************/
void diplomacy_dialog_erase_clause_callback(Widget w, XtPointer client_data, 
					    XtPointer call_data)
{
  struct Diplomacy_dialog *pdialog=(struct Diplomacy_dialog *)client_data;
  XawListReturnStruct *ret;

  ret=XawListShowCurrent(pdialog->dip_clauselist);

  if(ret->list_index!=XAW_LIST_NONE) {
    int i;
    struct genlist_iterator myiter;
  
    genlist_iterator_init(&myiter, &pdialog->treaty.clauses, 0);

    for(i=0; ITERATOR_PTR(myiter); ITERATOR_NEXT(myiter), i++) {
      if(i==ret->list_index) {
	struct packet_diplomacy_info pa;
	struct Clause *pclause=(struct Clause *)ITERATOR_PTR(myiter);

	pa.plrno0=pdialog->treaty.plr0->player_no;
	pa.plrno1=pdialog->treaty.plr1->player_no;
	pa.plrno_from=pclause->from->player_no;
	pa.clause_type=pclause->type;
	pa.value=pclause->value;
	send_packet_diplomacy_info(&aconnection, PACKET_DIPLOMACY_REMOVE_CLAUSE,
				   &pa);
	return;
      }
    }
  }
}




/****************************************************************
...
*****************************************************************/
void diplomacy_dialog_map_callback(Widget w, XtPointer client_data, 
				   XtPointer call_data)
{
  struct Diplomacy_dialog *pdialog=(struct Diplomacy_dialog *)client_data;
  struct packet_diplomacy_info pa;
  struct player *pgiver;
  
  pgiver=(XtParent(XtParent(w))==pdialog->dip_map_menubutton0) ? 
    pdialog->treaty.plr0 : pdialog->treaty.plr1;
  
  pa.plrno0=pdialog->treaty.plr0->player_no;
  pa.plrno1=pdialog->treaty.plr1->player_no;
  pa.clause_type=CLAUSE_MAP;
  pa.plrno_from=pgiver->player_no;
  pa.value=0;
  send_packet_diplomacy_info(&aconnection, PACKET_DIPLOMACY_CREATE_CLAUSE,
			     &pa);
}

/****************************************************************
...
*****************************************************************/
void diplomacy_dialog_seamap_callback(Widget w, XtPointer client_data, 
				   XtPointer call_data)
{
  struct Diplomacy_dialog *pdialog=(struct Diplomacy_dialog *)client_data;
  struct packet_diplomacy_info pa;
  struct player *pgiver;
  
  pgiver=(XtParent(XtParent(w))==pdialog->dip_map_menubutton0) ? 
    pdialog->treaty.plr0 : pdialog->treaty.plr1;
  
  pa.plrno0=pdialog->treaty.plr0->player_no;
  pa.plrno1=pdialog->treaty.plr1->player_no;
  pa.clause_type=CLAUSE_SEAMAP;
  pa.plrno_from=pgiver->player_no;
  pa.value=0;
  send_packet_diplomacy_info(&aconnection, PACKET_DIPLOMACY_CREATE_CLAUSE,
			     &pa);
}





/****************************************************************
...
*****************************************************************/
void diplomacy_dialog_close_callback(Widget w, XtPointer client_data, 
				     XtPointer call_data)
{
  struct Diplomacy_dialog *pdialog=(struct Diplomacy_dialog *)client_data;
  struct packet_diplomacy_info pa;

  pa.plrno0=game.player_idx;
  pa.plrno1=pdialog->treaty.plr1->player_no;
  send_packet_diplomacy_info(&aconnection, PACKET_DIPLOMACY_CANCEL_MEETING, 
			     &pa);
  
  close_diplomacy_dialog(pdialog);
}


/****************************************************************
...
*****************************************************************/
void diplomacy_dialog_accept_callback(Widget w, XtPointer client_data, 
				      XtPointer call_data)
{
  struct Diplomacy_dialog *pdialog=(struct Diplomacy_dialog *)client_data;
  struct packet_diplomacy_info pa;
  
  pa.plrno0=pdialog->treaty.plr0->player_no;
  pa.plrno1=pdialog->treaty.plr1->player_no;
  pa.plrno_from=game.player_idx;
  send_packet_diplomacy_info(&aconnection, PACKET_DIPLOMACY_ACCEPT_TREATY,
			     &pa);
}


/*****************************************************************
...
*****************************************************************/
void close_diplomacy_dialog(struct Diplomacy_dialog *pdialog)
{
  XtDestroyWidget(pdialog->dip_dialog_shell);
  
  genlist_unlink(&diplomacy_dialogs, pdialog);
  free(pdialog);
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
    struct Diplomacy_dialog *pdialog=
      (struct Diplomacy_dialog *)ITERATOR_PTR(myiter);
    if((pdialog->treaty.plr0==plr0 && pdialog->treaty.plr1==plr1) ||
       (pdialog->treaty.plr0==plr1 && pdialog->treaty.plr1==plr0))
      return pdialog;
  }
  return 0;
}

/*****************************************************************
...
*****************************************************************/
static struct Diplomacy_dialog *find_diplomacy_by_input(Widget w)
{
  struct genlist_iterator myiter;
  
  genlist_iterator_init(&myiter, &diplomacy_dialogs, 0);
    
  for(; ITERATOR_PTR(myiter); ITERATOR_NEXT(myiter)) {
    struct Diplomacy_dialog *pdialog=
      (struct Diplomacy_dialog *)ITERATOR_PTR(myiter);
    if((pdialog->dip_gold_input0==w) || (pdialog->dip_gold_input1==w)) {
      return pdialog;
    }
  }
  return 0;
}

/*****************************************************************
...
*****************************************************************/
void diplo_dialog_returnkey(Widget w, XEvent *event, String *params,
			    Cardinal *num_params)
{
  struct Diplomacy_dialog *pdialog;
  
  if((pdialog=find_diplomacy_by_input(w))) {
    struct player *pgiver;
    XtPointer dp;
    int amount;
    
    pgiver=(w==pdialog->dip_gold_input0) ? 
      pdialog->treaty.plr0 : pdialog->treaty.plr1;
    
    XtVaGetValues(w, XtNstring, &dp, NULL);
    amount=atoi(dp);
    
    if(amount>=0 && amount<=pgiver->economic.gold) {
      struct packet_diplomacy_info pa;
      pa.plrno0=pdialog->treaty.plr0->player_no;
      pa.plrno1=pdialog->treaty.plr1->player_no;
      pa.clause_type=CLAUSE_GOLD;
      pa.plrno_from=pgiver->player_no;
      pa.value=amount;
      send_packet_diplomacy_info(&aconnection, PACKET_DIPLOMACY_CREATE_CLAUSE,
				 &pa);
      XtVaSetValues(w, XtNstring, "", NULL);
    }
    else
      append_output_window("Game: Invalid amount of gold specified");
  }
}
