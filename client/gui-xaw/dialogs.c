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
#include <stdarg.h>

#include <X11/Intrinsic.h>
#include <X11/StringDefs.h>
#include <X11/Xaw/Form.h>
#include <X11/Xaw/Label.h>
#include <X11/Xaw/List.h>
#include <X11/Xaw/SimpleMenu.h>
#include <X11/Xaw/Toggle.h>     
#include <X11/Xaw/Command.h>
#include <X11/Xaw/AsciiText.h>  
#include <X11/Xaw/SmeBSB.h>

#include "game.h"
#include "government.h"
#include "map.h"
#include "mem.h"
#include "packets.h"
#include "player.h"

#include "chatline.h"
#include "civclient.h"
#include "control.h" /* request_xxx and set_unit_focus */
#include "graphics.h"
#include "gui_stuff.h"
#include "mapctrl.h"
#include "mapview.h"

#include "dialogs.h"

extern Widget toplevel, main_form, map_canvas;

extern struct connection aconnection;
extern Display	*display;
extern int display_depth;
extern int flags_are_transparent;
extern int ai_popup_windows;
extern GC fill_bg_gc;

/******************************************************************/
Widget races_dialog_shell=NULL;
Widget races_form, races_toggles_form, races_label;
Widget races_ok_command;
Widget *races_toggles=NULL, races_name;
Widget races_sex_toggles[2], races_sex_form, races_sex_label;

/******************************************************************/
Widget spy_tech_shell;
Widget spy_advances_list, spy_advances_list_label;
Widget spy_steal_command;

int spy_tech_shell_is_modal;
int advance_type[A_LAST];
int steal_advance = 0;

/******************************************************************/
Widget spy_sabotage_shell;
Widget spy_improvements_list, spy_improvements_list_label;
Widget spy_sabotage_command;

int spy_sabotage_shell_is_modal;
int improvement_type[B_LAST];
int sabotage_improvement = 0;

/******************************************************************/

Widget about_dialog_shell;
Widget about_form, about_label, about_command;

/******************************************************************/
Widget help_copying_dialog_shell, help_keys_dialog_shell;


/******************************************************************/
Widget unit_select_dialog_shell;
Widget unit_select_form;
Widget unit_select_commands[100];
Widget unit_select_labels[100];
Pixmap unit_select_pixmaps[100];
int unit_select_ids[100];
int unit_select_no;

void about_button_callback(Widget w, XtPointer client_data, 
			    XtPointer call_data);

void help_button_callback(Widget w, XtPointer client_data, 
			    XtPointer call_data);

void create_rates_dialog(void);
void create_races_dialog(void);
void create_about_dialog(void);
void create_help_dialog(Widget *shell);


int races_buttons_get_current(void);

void races_buttons_callback(Widget w, XtPointer client_data, 
			    XtPointer call_data);
void races_toggles_callback(Widget w, XtPointer client_data, 
			    XtPointer call_data);

void unit_select_callback(Widget w, XtPointer client_data, 
			    XtPointer call_data);
void unit_select_all_callback(Widget w, XtPointer client_data, 
			    XtPointer call_data);

int is_showing_government_dialog;

int is_showing_pillage_dialog = FALSE;
int unit_to_use_to_pillage;

int caravan_city_id;
int caravan_unit_id;

int diplomat_id;
int diplomat_target_id;

struct city *pcity_caravan_dest;
struct unit *punit_caravan;

static Widget caravan_dialog;


/****************************************************************
...
*****************************************************************/
static void notify_command_callback(Widget w, XtPointer client_data, 
				    XtPointer call_data)
{
  XtDestroyWidget(XtParent(XtParent(w)));
  XtSetSensitive(toplevel, TRUE);
}



/****************************************************************
...
*****************************************************************/
void popup_notify_dialog(char *caption, char *headline, char *lines)
{
  Widget notify_dialog_shell, notify_form, notify_command;
  Widget notify_headline, notify_label;
  Dimension width, width2;
  
  notify_dialog_shell = XtCreatePopupShell(caption,
					   transientShellWidgetClass,
					   toplevel, NULL, 0);

  notify_form = XtVaCreateManagedWidget("notifyform", 
					 formWidgetClass, 
					 notify_dialog_shell, NULL);

  notify_headline=XtVaCreateManagedWidget("notifyheadline", 
			  labelWidgetClass, notify_form, 
			  XtNlabel, headline,
			  NULL);

  
  notify_label=XtVaCreateManagedWidget("notifylabel", 
			  labelWidgetClass, notify_form, 
			  XtNlabel, lines,
			  NULL);   

  notify_command = XtVaCreateManagedWidget("notifycommand", 
					   commandWidgetClass,
					   notify_form,
					   NULL);

  XtVaGetValues(notify_label, XtNwidth, &width, NULL);
  XtVaGetValues(notify_headline, XtNwidth, &width2, NULL);
  if(width>width2)
    XtVaSetValues(notify_headline, XtNwidth, width, NULL); 
  
  XtAddCallback(notify_command, XtNcallback, notify_command_callback, NULL);
  
  xaw_set_relative_position(toplevel, notify_dialog_shell, 25, 25);
  XtPopup(notify_dialog_shell, XtGrabNone);
  XtSetSensitive(toplevel, FALSE);
}

/****************************************************************
...
*****************************************************************/

/* surely this should use genlists??  --dwp */
struct widget_list {
  Widget w;
  int x,y;
  struct widget_list *next;
};
static struct widget_list *notify_goto_widget_list = NULL;

static void notify_goto_widget_remove(Widget w)
{
  struct widget_list *cur, *tmp;
  cur=notify_goto_widget_list;
  if (!cur)
    return;
  if (cur && cur->w == w) {
    cur = cur->next;
    free(notify_goto_widget_list);
    notify_goto_widget_list = cur;
    return;
  }
  for (; cur->next && cur->next->w!= w; cur=cur->next);
  if (cur->next) {
    tmp = cur->next;
    cur->next = cur->next->next;
    free(tmp);
  }
}

static void notify_goto_find_widget(Widget w, int *x, int *y)
{
  struct widget_list *cur;
  *x=0;
  *y=0;
  for (cur = notify_goto_widget_list; cur && cur->w !=w; cur = cur->next);
  if (cur) {
    *x = cur->x;
    *y = cur->y;
  }
}

static void notify_goto_add_widget_coords(Widget w, int  x, int y)
{
  struct widget_list *newwidget;
  newwidget = fc_malloc(sizeof(struct widget_list));
  newwidget->w = w;
  newwidget->x = x;
  newwidget->y = y;
  newwidget->next = notify_goto_widget_list;
  notify_goto_widget_list = newwidget;
}

static void notify_goto_command_callback(Widget w, XtPointer client_data, 
			     XtPointer call_data)
{
  int x,y;
  notify_goto_find_widget(w, &x, &y);
  center_tile_mapcanvas(x, y);
  notify_goto_widget_remove(w);

  XtDestroyWidget(XtParent(XtParent(w)));
  XtSetSensitive(toplevel, TRUE);
}

static void notify_no_goto_command_callback(Widget w, XtPointer client_data, 
			     XtPointer call_data)
{
  notify_goto_widget_remove(w);
  XtDestroyWidget(XtParent(XtParent(w)));
  XtSetSensitive(toplevel, TRUE);
}


/****************************************************************
...
*****************************************************************/
void popup_notify_goto_dialog(char *headline, char *lines,int x, int y)
{
  Widget notify_dialog_shell, notify_form, notify_command, notify_goto_command;
  Widget notify_headline, notify_label;
  Dimension width, width2, width_1, width_2;
  
  if (x == 0 && y == 0) {
    popup_notify_dialog("Message:", headline, lines);
    return;
  }
  notify_dialog_shell = XtCreatePopupShell("Message:",
					   transientShellWidgetClass,
					   toplevel, NULL, 0);

  notify_form = XtVaCreateManagedWidget("notifyform", 
					 formWidgetClass, 
					 notify_dialog_shell, NULL);

  notify_headline=XtVaCreateManagedWidget("notifyheadline", 
			  labelWidgetClass, notify_form, 
			  XtNlabel, headline,
			  NULL);

  
  notify_label=XtVaCreateManagedWidget("notifylabel", 
			  labelWidgetClass, notify_form, 
			  XtNlabel, lines,
			  NULL);   

  notify_command = XtVaCreateManagedWidget("notifycommand", 
					   commandWidgetClass,
					   notify_form,
					   NULL);

  notify_goto_command = XtVaCreateManagedWidget("notifygotocommand", 
					   commandWidgetClass,
					   notify_form,
					   NULL);
  
  XtVaGetValues(notify_label, XtNwidth, &width, NULL);
  XtVaGetValues(notify_headline, XtNwidth, &width2, NULL);
  XtVaGetValues(notify_command, XtNwidth, &width_1, NULL);
  XtVaGetValues(notify_goto_command, XtNwidth, &width_2, NULL);
  if (width_1 + width_2 > width) width = width_1 + width_2;
  if(width>width2)
    XtVaSetValues(notify_headline, XtNwidth, width, NULL); 
  
  XtAddCallback(notify_command, XtNcallback, notify_no_goto_command_callback, NULL);
  XtAddCallback(notify_goto_command, XtNcallback, notify_goto_command_callback, NULL);
  notify_goto_add_widget_coords(notify_goto_command, x, y);
  xaw_set_relative_position(toplevel, notify_dialog_shell, 25, 25);
  XtPopup(notify_dialog_shell, XtGrabNone);
  /*  XtSetSensitive(toplevel, FALSE); */
}



/****************************************************************
...
*****************************************************************/
static void diplomat_bribe_yes_callback(Widget w, XtPointer client_data, 
					XtPointer call_data)
{
  struct packet_diplomat_action req;

  req.action_type=DIPLOMAT_BRIBE;
  req.diplomat_id=diplomat_id;
  req.target_id=diplomat_target_id;

  send_packet_diplomat_action(&aconnection, &req);
  
  destroy_message_dialog(w);
}

/****************************************************************
...
*****************************************************************/
static void diplomat_bribe_no_callback(Widget w, XtPointer client_data, 
				       XtPointer call_data)
{
  destroy_message_dialog(w);
}

/****************************************************************
...  Ask the server how much the bribe is
*****************************************************************/
static void diplomat_bribe_callback(Widget w, XtPointer client_data, 
				    XtPointer call_data)
{
  struct packet_generic_integer packet;

  destroy_message_dialog(w);

  if(find_unit_by_id(diplomat_id) && 
     find_unit_by_id(diplomat_target_id)) { 
    packet.value = diplomat_target_id;
    send_packet_generic_integer(&aconnection, PACKET_INCITE_INQ, &packet);
  }
}

/****************************************************************
...
*****************************************************************/
void popup_bribe_dialog(struct unit *punit)
{
  char buf[128];
  
  if(game.player_ptr->economic.gold>=punit->bribe_cost) {
    sprintf(buf, "Bribe unit for %d gold?\nTreasury contains %d gold.", 
	    punit->bribe_cost, game.player_ptr->economic.gold);
    popup_message_dialog(toplevel, "diplomatbribedialog", buf,
			 diplomat_bribe_yes_callback, 0,
			 diplomat_bribe_no_callback, 0, 0);
  } else {
    sprintf(buf, "Bribing the unit costs %d gold.\nTreasury contains %d gold.", 
	    punit->bribe_cost, game.player_ptr->economic.gold);
    popup_message_dialog(toplevel, "diplomatnogolddialog", buf,
			 diplomat_bribe_no_callback, 0, 
			 0);
  }
}

/****************************************************************
...
*****************************************************************/
static void diplomat_sabotage_callback(Widget w, XtPointer client_data, 
				       XtPointer call_data)
{
  destroy_message_dialog(w);
  
  if(find_unit_by_id(diplomat_id) && 
     find_city_by_id(diplomat_target_id)) { 
    struct packet_diplomat_action req;
    
    req.action_type=DIPLOMAT_SABOTAGE;
    req.diplomat_id=diplomat_id;
    req.target_id=diplomat_target_id;
    req.value = -1;

    send_packet_diplomat_action(&aconnection, &req);
  }

}

/****************************************************************
...
*****************************************************************/
static void diplomat_embassy_callback(Widget w, XtPointer client_data, 
				      XtPointer call_data)
{
 
  destroy_message_dialog(w);
  
  if(find_unit_by_id(diplomat_id) && 
     (find_city_by_id(diplomat_target_id))) { 
    struct packet_diplomat_action req;
    
    req.action_type=DIPLOMAT_EMBASSY;
    req.diplomat_id=diplomat_id;
    req.target_id=diplomat_target_id;
    
    send_packet_diplomat_action(&aconnection, &req);
  }

}
/****************************************************************
...
*****************************************************************/
static void diplomat_investigate_callback(Widget w, XtPointer client_data, 
					  XtPointer call_data)
{
 
  destroy_message_dialog(w);
  
  if(find_unit_by_id(diplomat_id) && 
     (find_city_by_id(diplomat_target_id))) { 
    struct packet_diplomat_action req;
    
    req.action_type=DIPLOMAT_INVESTIGATE;
    req.diplomat_id=diplomat_id;
    req.target_id=diplomat_target_id;
    
    send_packet_diplomat_action(&aconnection, &req);
  }

}
/****************************************************************
...
*****************************************************************/
static void spy_sabotage_unit_callback(Widget w, XtPointer client_data, 
				       XtPointer call_data)
{

  struct packet_diplomat_action req;
  
  req.action_type=SPY_SABOTAGE_UNIT;
  req.diplomat_id=diplomat_id;
  req.target_id=diplomat_target_id;
  
  send_packet_diplomat_action(&aconnection, &req);
  
  destroy_message_dialog(w);
}

/****************************************************************
...
*****************************************************************/
static void spy_poison_callback(Widget w, XtPointer client_data, 
				XtPointer call_data)
{
  
  destroy_message_dialog(w);
  
  if(find_unit_by_id(diplomat_id) && 
     (find_city_by_id(diplomat_target_id))) { 
    struct packet_diplomat_action req;
    
    req.action_type=SPY_POISON;
    req.diplomat_id=diplomat_id;
    req.target_id=diplomat_target_id;
    
    send_packet_diplomat_action(&aconnection, &req);
  }

}

/****************************************************************
...
*****************************************************************/
static void diplomat_steal_callback(Widget w, XtPointer client_data, 
				    XtPointer call_data)
{
  destroy_message_dialog(w);
  
  if(find_unit_by_id(diplomat_id) && 
     find_city_by_id(diplomat_target_id)) { 
    struct packet_diplomat_action req;
    
    req.action_type=DIPLOMAT_STEAL;
    req.diplomat_id=diplomat_id;
    req.target_id=diplomat_target_id;
    req.value=0;
    
    send_packet_diplomat_action(&aconnection, &req);
  }

}

/****************************************************************
...
*****************************************************************/
static void spy_close_tech_callback(Widget w, XtPointer client_data, 
				    XtPointer call_data)
{

  if(spy_tech_shell_is_modal)
     XtSetSensitive(main_form, TRUE);
   XtDestroyWidget(spy_tech_shell);
   spy_tech_shell=0;
}

/****************************************************************
...
*****************************************************************/
static void spy_close_sabotage_callback(Widget w, XtPointer client_data, 
					XtPointer call_data)
{

  if(spy_sabotage_shell_is_modal)
     XtSetSensitive(main_form, TRUE);
   XtDestroyWidget(spy_sabotage_shell);
   spy_sabotage_shell=0;
}

/****************************************************************
...
*****************************************************************/
static void spy_select_tech_callback(Widget w, XtPointer client_data, 
				     XtPointer call_data)
{
  XawListReturnStruct *ret;
  ret=XawListShowCurrent(spy_advances_list);
  
  if(ret->list_index!=XAW_LIST_NONE && advance_type[ret->list_index] != -1){
    steal_advance = advance_type[ret->list_index];
    XtSetSensitive(spy_steal_command, TRUE);
    return;
  }
  XtSetSensitive(spy_steal_command, FALSE);
}

/****************************************************************
...
*****************************************************************/
static void spy_select_improvement_callback(Widget w, XtPointer client_data, 
					    XtPointer call_data)
{
  XawListReturnStruct *ret;
  ret=XawListShowCurrent(spy_improvements_list);
  
  if(ret->list_index!=XAW_LIST_NONE){
    sabotage_improvement = improvement_type[ret->list_index];
    XtSetSensitive(spy_sabotage_command, TRUE);
    return;
  }
  XtSetSensitive(spy_sabotage_command, FALSE);
}

/****************************************************************
...
*****************************************************************/
static void spy_steal_callback(Widget w, XtPointer client_data, 
			       XtPointer call_data)
{  
  XtDestroyWidget(spy_tech_shell);
  spy_tech_shell = 0l;
  
  if(!steal_advance){
    printf("Bug in spy steal tech code\n");
    return;
  }
  
  if(find_unit_by_id(diplomat_id) && 
     find_city_by_id(diplomat_target_id)) { 
    struct packet_diplomat_action req;
    
    req.action_type=DIPLOMAT_STEAL;
    req.value=steal_advance;
    req.diplomat_id=diplomat_id;
    req.target_id=diplomat_target_id;

    send_packet_diplomat_action(&aconnection, &req);
  }
}

/****************************************************************
...
*****************************************************************/
static void spy_sabotage_callback(Widget w, XtPointer client_data, 
				  XtPointer call_data)
{  
  XtDestroyWidget(spy_sabotage_shell);
  spy_sabotage_shell = 0l;
  
  if(!sabotage_improvement){
    printf("Bug in spy sabotage code\n");
    return;
  }
  
  if(find_unit_by_id(diplomat_id) && 
     find_city_by_id(diplomat_target_id)) { 
    struct packet_diplomat_action req;
    
    req.action_type=DIPLOMAT_SABOTAGE;
    req.value=sabotage_improvement+1;
    req.diplomat_id=diplomat_id;
    req.target_id=diplomat_target_id;

    send_packet_diplomat_action(&aconnection, &req);
  }
}

/****************************************************************
...
*****************************************************************/
static int create_advances_list(struct player *pplayer,
				struct player *pvictim, int make_modal)
{  
  Widget spy_tech_form;
  Widget close_command;
  Dimension width1, width2; 
  int i, j;

  static char *advances_can_steal[A_LAST]; 
  
  spy_tech_shell = XtVaCreatePopupShell("spystealtechpopup", 
					make_modal ? 
					transientShellWidgetClass :
					topLevelShellWidgetClass,
					toplevel, 
					0);  
  
  spy_tech_form = XtVaCreateManagedWidget("spystealtechform", 
					     formWidgetClass,
					     spy_tech_shell,
					     NULL);   

  spy_advances_list_label = XtVaCreateManagedWidget("spystealtechlistlabel", 
						    labelWidgetClass, 
						    spy_tech_form,
						    NULL);

  spy_advances_list = XtVaCreateManagedWidget("spystealtechlist", 
					      listWidgetClass,
					      spy_tech_form,
					      NULL);

  close_command = XtVaCreateManagedWidget("spystealtechclosecommand", 
					  commandWidgetClass,
					  spy_tech_form,
					  NULL);
  
  spy_steal_command = XtVaCreateManagedWidget("spystealtechcommand", 
					    commandWidgetClass,
					    spy_tech_form,
					    XtNsensitive, False,
					    NULL);
  

  XtAddCallback(spy_advances_list, XtNcallback, spy_select_tech_callback, NULL);
  XtAddCallback(close_command, XtNcallback, spy_close_tech_callback, NULL);
  XtAddCallback(spy_steal_command, XtNcallback, spy_steal_callback, NULL);
  XtRealizeWidget(spy_tech_shell);

  /* Now populate the list */
  
  j = 0;
  advances_can_steal[j] = "NONE";
  advance_type[j] = -1;

  if (pvictim) { /* you don't want to know what lag can do -- Syela */
    for(i=1; i<A_LAST; i++) {
      if(get_invention(pvictim, i)==TECH_KNOWN && 
         (get_invention(pplayer, i)==TECH_UNKNOWN || 
          get_invention(pplayer, i)==TECH_REACHABLE)) {
      
        advances_can_steal[j] = advances[i].name;
        advance_type[j++] = i;
      }
    }
  }

  if(j == 0) j++;
  advances_can_steal[j] = NULL; 
  
  XtSetSensitive(spy_steal_command, FALSE);
  
  XawListChange(spy_advances_list, advances_can_steal, 0, 0, 1);
  XtVaGetValues(spy_advances_list, XtNwidth, &width1, NULL);
  XtVaGetValues(spy_advances_list_label, XtNwidth, &width2, NULL);
  XtVaSetValues(spy_advances_list, XtNwidth, MAX(width1,width2), NULL); 
  XtVaSetValues(spy_advances_list_label, XtNwidth, MAX(width1,width2), NULL); 

  return j;
}

/****************************************************************
...
*****************************************************************/
static int create_improvements_list(struct player *pplayer,
				    struct city *pcity, int make_modal)
{  
  Widget spy_sabotage_form;
  Widget close_command;
  Dimension width1, width2; 
  int i, j;

  static char *improvements_can_sabotage[B_LAST]; 
  
  spy_sabotage_shell = XtVaCreatePopupShell("spysabotageimprovementspopup", 
					make_modal ? 
					transientShellWidgetClass :
					topLevelShellWidgetClass,
					toplevel, 
					0);  
  
  spy_sabotage_form = XtVaCreateManagedWidget("spysabotageimprovementsform", 
					     formWidgetClass,
					     spy_sabotage_shell,
					     NULL);   

  spy_improvements_list_label = XtVaCreateManagedWidget("spysabotageimprovementslistlabel", 
						    labelWidgetClass, 
						    spy_sabotage_form,
						    NULL);

  spy_improvements_list = XtVaCreateManagedWidget("spysabotageimprovementslist", 
					      listWidgetClass,
					      spy_sabotage_form,
					      NULL);

  close_command = XtVaCreateManagedWidget("spysabotageimprovementsclosecommand", 
					  commandWidgetClass,
					  spy_sabotage_form,
					  NULL);
  
  spy_sabotage_command = XtVaCreateManagedWidget("spysabotageimprovementscommand", 
						 commandWidgetClass,
						 spy_sabotage_form,
						 XtNsensitive, False,
						 NULL);
  

  XtAddCallback(spy_improvements_list, XtNcallback, spy_select_improvement_callback, NULL);
  XtAddCallback(close_command, XtNcallback, spy_close_sabotage_callback, NULL);
  XtAddCallback(spy_sabotage_command, XtNcallback, spy_sabotage_callback, NULL);
  XtRealizeWidget(spy_sabotage_shell);

  /* Now populate the list */
  
  j = 0;
  improvements_can_sabotage[j] = "City Production";
  improvement_type[j++] = -1;

  for(i=0; i<B_LAST; i++) 
    if(i != B_PALACE && pcity->improvements[i] && !is_wonder(i)) {
      improvements_can_sabotage[j] = get_imp_name_ex(pcity, i);
      improvement_type[j++] = i;
    }  
  
  improvements_can_sabotage[j] = NULL;
  
  XtSetSensitive(spy_sabotage_command, FALSE);
  
  XawListChange(spy_improvements_list, improvements_can_sabotage, 0, 0, 1);
  XtVaGetValues(spy_improvements_list, XtNwidth, &width1, NULL);
  XtVaGetValues(spy_improvements_list_label, XtNwidth, &width2, NULL);
  XtVaSetValues(spy_improvements_list, XtNwidth, MAX(width1,width2), NULL); 
  XtVaSetValues(spy_improvements_list_label, XtNwidth, MAX(width1,width2), NULL); 

  return j;
}

/****************************************************************
...
*****************************************************************/
static void spy_steal_popup(Widget w, XtPointer client_data, 
			    XtPointer call_data)
{
  struct city *pvcity = find_city_by_id(diplomat_target_id);
  struct player *pvictim = NULL;

  if(pvcity)
    pvictim = city_owner(pvcity);

/* it is concievable that pvcity will not be found, because something
has happened to the city during latency.  Therefore we must initialize
pvictim to NULL and account for !pvictim in create_advances_list. -- Syela */
  
  destroy_message_dialog(w);

  if(!spy_tech_shell){
    Position x, y;
    Dimension width, height;
    spy_tech_shell_is_modal=1;

    create_advances_list(game.player_ptr, pvictim, spy_tech_shell_is_modal);
    
    XtVaGetValues(toplevel, XtNwidth, &width, XtNheight, &height, NULL);
    
    XtTranslateCoords(toplevel, (Position) width/10, (Position) height/10,
		      &x, &y);
    XtVaSetValues(spy_tech_shell, XtNx, x, XtNy, y, NULL);
    
    XtPopup(spy_tech_shell, XtGrabNone);
  }
}

/****************************************************************
...
*****************************************************************/
static void spy_sabotage_popup(Widget w, XtPointer client_data, 
			       XtPointer call_data)
{
  struct city *pvcity = find_city_by_id(diplomat_target_id);
  
  destroy_message_dialog(w);

  if(!spy_sabotage_shell){
    Position x, y;
    Dimension width, height;
    spy_sabotage_shell_is_modal=1;

    create_improvements_list(game.player_ptr, pvcity, spy_sabotage_shell_is_modal);
    
    XtVaGetValues(toplevel, XtNwidth, &width, XtNheight, &height, NULL);
    
    XtTranslateCoords(toplevel, (Position) width/10, (Position) height/10,
		      &x, &y);
    XtVaSetValues(spy_sabotage_shell, XtNx, x, XtNy, y, NULL);
    
    XtPopup(spy_sabotage_shell, XtGrabNone);
  }
}

/****************************************************************
...
*****************************************************************/
static void diplomat_incite_yes_callback(Widget w, XtPointer client_data, 
					 XtPointer call_data)
{
  struct packet_diplomat_action req;

  req.action_type=DIPLOMAT_INCITE;
  req.diplomat_id=diplomat_id;
  req.target_id=diplomat_target_id;

  send_packet_diplomat_action(&aconnection, &req);
  
  destroy_message_dialog(w);
}

/****************************************************************
...
*****************************************************************/
static void diplomat_incite_no_callback(Widget w, XtPointer client_data, 
					XtPointer call_data)
{
  destroy_message_dialog(w);
}


/****************************************************************
...  Ask the server how much the revolt is going to cost us
*****************************************************************/
static void diplomat_incite_callback(Widget w, XtPointer client_data, 
				     XtPointer call_data)
{
  struct city *pcity;
  struct packet_generic_integer packet;

  destroy_message_dialog(w);

  if(find_unit_by_id(diplomat_id) && 
     (pcity=find_city_by_id(diplomat_target_id))) { 
    packet.value = diplomat_target_id;
    send_packet_generic_integer(&aconnection, PACKET_INCITE_INQ, &packet);
  }
}

/****************************************************************
...  Popup the yes/no dialog for inciting, since we know the cost now
*****************************************************************/
void popup_incite_dialog(struct city *pcity)
{
  char buf[128];

  if(game.player_ptr->economic.gold>=pcity->incite_revolt_cost) {
   sprintf(buf, "Incite a revolt for %d gold?\nTreasury contains %d gold.", 
	   pcity->incite_revolt_cost, game.player_ptr->economic.gold);
   diplomat_target_id = pcity->id;
   popup_message_dialog(toplevel, "diplomatrevoltdialog", buf,
			diplomat_incite_yes_callback, 0,
			diplomat_incite_no_callback, 0, 0);
  } else {
   sprintf(buf, "Inciting a revolt costs %d gold.\nTreasury contains %d gold.", 
	      pcity->incite_revolt_cost, game.player_ptr->economic.gold);
   popup_message_dialog(toplevel, "diplomatnogolddialog", buf,
			diplomat_incite_no_callback, 0, 
			0);
  }
}


/****************************************************************
...
*****************************************************************/
static void diplomat_cancel_callback(Widget w, XtPointer a, XtPointer b)
{
  destroy_message_dialog(w);
}



/****************************************************************
...
*****************************************************************/
void popup_diplomat_dialog(struct unit *punit, int dest_x, int dest_y)
{
  struct city *pcity;
  struct unit *ptunit;
  Widget shl;

  diplomat_id=punit->id;
  
  if((pcity=map_get_city(dest_x, dest_y))){
    
    /* Spy/Diplomat acting against a city */ 
    
    diplomat_target_id=pcity->id;
    if(!unit_flag(punit->type, F_SPY)){
      shl=popup_message_dialog(toplevel, "diplomatdialog", 
			       "Sir, the diplomat is waiting for your command",
			       diplomat_embassy_callback, 0,
			       diplomat_investigate_callback, 0,
			       diplomat_sabotage_callback, 0,
			       diplomat_steal_callback, 0,
			       diplomat_incite_callback, 0,
			       diplomat_cancel_callback, 0,
			       0);
      
      if(!diplomat_can_do_action(punit, DIPLOMAT_EMBASSY, dest_x, dest_y))
	XtSetSensitive(XtNameToWidget(shl, "*button0"), FALSE);
      if(!diplomat_can_do_action(punit, DIPLOMAT_INVESTIGATE, dest_x, dest_y))
	XtSetSensitive(XtNameToWidget(shl, "*button1"), FALSE);
      if(!diplomat_can_do_action(punit, DIPLOMAT_SABOTAGE, dest_x, dest_y))
	XtSetSensitive(XtNameToWidget(shl, "*button2"), FALSE);
      if(!diplomat_can_do_action(punit, DIPLOMAT_STEAL, dest_x, dest_y))
	XtSetSensitive(XtNameToWidget(shl, "*button3"), FALSE);
      if(!diplomat_can_do_action(punit, DIPLOMAT_INCITE, dest_x, dest_y))
	XtSetSensitive(XtNameToWidget(shl, "*button4"), FALSE);
    }else{
      shl=popup_message_dialog(toplevel, "spydialog", 
			       "Sir, the spy is waiting for your command",
			       diplomat_embassy_callback, 0,
			       diplomat_investigate_callback, 0,
			       spy_poison_callback,0,
			       spy_sabotage_popup, 0,
			       spy_steal_popup, 0,
			       diplomat_incite_callback, 0,
			       diplomat_cancel_callback, 0,
			       0);
      
      if(!diplomat_can_do_action(punit, DIPLOMAT_EMBASSY, dest_x, dest_y))
	XtSetSensitive(XtNameToWidget(shl, "*button0"), FALSE);
      if(!diplomat_can_do_action(punit, DIPLOMAT_INVESTIGATE, dest_x, dest_y))
	XtSetSensitive(XtNameToWidget(shl, "*button1"), FALSE);
      if(!diplomat_can_do_action(punit, SPY_POISON, dest_x, dest_y))
	XtSetSensitive(XtNameToWidget(shl, "*button2"), FALSE);
      if(!diplomat_can_do_action(punit, DIPLOMAT_SABOTAGE, dest_x, dest_y))
	XtSetSensitive(XtNameToWidget(shl, "*button3"), FALSE);
      if(!diplomat_can_do_action(punit, DIPLOMAT_STEAL, dest_x, dest_y))
	XtSetSensitive(XtNameToWidget(shl, "*button4"), FALSE);
      if(!diplomat_can_do_action(punit, DIPLOMAT_INCITE, dest_x, dest_y))
	XtSetSensitive(XtNameToWidget(shl, "*button5"), FALSE);
    }
  }else{ 
    if((ptunit=unit_list_get(&map_get_tile(dest_x, dest_y)->units, 0))){
      /* Spy/Diplomat acting against a unit */ 
      
      diplomat_target_id=ptunit->id;

      shl=popup_message_dialog(toplevel, "spybribedialog", 
      			       (!unit_flag(punit->type, F_SPY))?
			       "Sir, the diplomat is waiting for your command":
			       "Sir, the spy is waiting for your command",
			       diplomat_bribe_callback, 0,
			       spy_sabotage_unit_callback, 0,
			       diplomat_cancel_callback, 0,
			       0);
	
      if(!diplomat_can_do_action(punit, DIPLOMAT_BRIBE, dest_x, dest_y))
	XtSetSensitive(XtNameToWidget(shl, "*button0"), FALSE);
      if(!diplomat_can_do_action(punit, SPY_SABOTAGE_UNIT, dest_x, dest_y))
	XtSetSensitive(XtNameToWidget(shl, "*button1"), FALSE);
    }
  }
}


/****************************************************************
...
*****************************************************************/
static void caravan_establish_trade_callback(Widget w, XtPointer client_data, 
					     XtPointer call_data)
{
  struct packet_unit_request req;
  req.unit_id=caravan_unit_id;
  req.city_id=caravan_city_id;
  req.name[0]='\0';
  send_packet_unit_request(&aconnection, &req, PACKET_UNIT_ESTABLISH_TRADE);
    
  destroy_message_dialog(w);
  caravan_dialog = 0;
  process_caravan_arrival(NULL);
}


/****************************************************************
...
*****************************************************************/
static void caravan_help_build_wonder_callback(Widget w, XtPointer client_data, 
					       XtPointer call_data)
{
  struct packet_unit_request req;
  req.unit_id=caravan_unit_id;
  req.city_id=caravan_city_id;
  req.name[0]='\0';
  send_packet_unit_request(&aconnection, &req, PACKET_UNIT_HELP_BUILD_WONDER);

  destroy_message_dialog(w);
  caravan_dialog = 0;
  process_caravan_arrival(NULL);
}


/****************************************************************
...
*****************************************************************/
static void caravan_keep_moving_callback(Widget w, XtPointer client_data, 
					 XtPointer call_data)
{
#if 0   /* Now don't want to move at all in this case --dwp */
  struct unit *punit;
  struct city *pcity;

  if((punit=find_unit_by_id(caravan_unit_id)) && 
     (pcity=find_city_by_id(caravan_city_id))) {
    struct unit req_unit;

    req_unit=*punit;
    req_unit.x=pcity->x;
    req_unit.y=pcity->y;
    send_unit_info(&req_unit);
  }
#endif
  
  destroy_message_dialog(w);
  caravan_dialog = 0;
  process_caravan_arrival(NULL);
}


/****************************************************************
...
*****************************************************************/
void popup_caravan_dialog(struct unit *punit,
			  struct city *phomecity, struct city *pdestcity)
{
  char buf[128];
  
  sprintf(buf, "Your caravan from %s reaches the city of %s.\nWhat now?",
	  phomecity->name, pdestcity->name);
  
  caravan_city_id=pdestcity->id; /* callbacks need these */
  caravan_unit_id=punit->id;
  
  caravan_dialog=popup_message_dialog(toplevel, "caravandialog", 
			   buf,
			   caravan_establish_trade_callback, 0,
			   caravan_help_build_wonder_callback, 0,
			   caravan_keep_moving_callback, 0,
			   0);
  
  if(!can_establish_trade_route(phomecity, pdestcity))
    XtSetSensitive(XtNameToWidget(caravan_dialog, "*button0"), FALSE);
  
  if(!unit_can_help_build_wonder(punit, pdestcity))
    XtSetSensitive(XtNameToWidget(caravan_dialog, "*button1"), FALSE);
}


/****************************************************************
...
*****************************************************************/
static void government_callback(Widget w, XtPointer client_data, 
				XtPointer call_data)
{
  struct packet_player_request packet;

  packet.government=(size_t)client_data;
  send_packet_player_request(&aconnection, &packet, PACKET_PLAYER_GOVERNMENT);

  destroy_message_dialog(w);
  is_showing_government_dialog=0;
}


/****************************************************************
...
*****************************************************************/
void popup_government_dialog(void)
{
  Widget shell, form, dlabel, button, prev;
  int i, can_change;
  
  if(is_showing_government_dialog) {
    return;
  }
  is_showing_government_dialog=1;

  XtSetSensitive(toplevel, FALSE);

  shell = XtCreatePopupShell("governmentdialog", transientShellWidgetClass,
			     toplevel, NULL, 0);
  form = XtVaCreateManagedWidget("form", formWidgetClass, shell, NULL);
  dlabel = XtVaCreateManagedWidget("dlabel", labelWidgetClass, form, NULL);

  prev = dlabel;
  for (i=0; i < game.government_count; ++i) {
    if (i == game.government_when_anarchy) continue;
    can_change = can_change_to_government(game.player_ptr, i);
    button = XtVaCreateManagedWidget("button", commandWidgetClass, form,
				     XtNfromVert, prev,
				     XtNlabel, (XtArgVal)governments[i].name,
				     NULL);
    XtAddCallback(button, XtNcallback, government_callback, (XtPointer)i);
    XtSetSensitive(button, can_change ? TRUE : FALSE);
    prev = button;
  }
  
  xaw_set_relative_position(toplevel, shell, 10, 0);
  XtPopup(shell, XtGrabNone);
}


/****************************************************************
...
*****************************************************************/
static void revolution_callback_yes(Widget w, XtPointer client_data, 
				    XtPointer call_data)
{
  struct packet_player_request packet;

  send_packet_player_request(&aconnection, &packet, PACKET_PLAYER_REVOLUTION);
  
  destroy_message_dialog(w);
}

/****************************************************************
...
*****************************************************************/
static void revolution_callback_no(Widget w, XtPointer client_data, 
				   XtPointer call_data)
{
  destroy_message_dialog(w);
}



/****************************************************************
...
*****************************************************************/
void popup_revolution_dialog(void)
{
  popup_message_dialog(toplevel, "revolutiondialog", 
		       "You say you wanna revolution?",
		       revolution_callback_yes, 0,
		       revolution_callback_no, 0, 
		       0);
}


/****************************************************************
...
*****************************************************************/
static void pillage_callback(Widget w, XtPointer client_data, 
			     XtPointer call_data)
{
  if (!is_showing_pillage_dialog) {
    destroy_message_dialog (w);
    return;
  }

  if (client_data) {
    struct unit *punit = find_unit_by_id (unit_to_use_to_pillage);
    if (punit) {
      request_new_unit_activity_targeted (punit,
					  ACTIVITY_PILLAGE,
					  (int)client_data);
    }
  }

  destroy_message_dialog (w);
  is_showing_pillage_dialog = FALSE;
}

/****************************************************************
...
*****************************************************************/
void popup_pillage_dialog(struct unit *punit, int may_pillage)
{
  Widget shell, form, dlabel, button, prev;
  int what;

  if (is_showing_pillage_dialog) {
    return;
  }
  is_showing_pillage_dialog = TRUE;
  unit_to_use_to_pillage = punit->id;

  XtSetSensitive (toplevel, FALSE);

  shell = XtCreatePopupShell ("pillagedialog", transientShellWidgetClass,
			      toplevel, NULL, 0);
  form = XtVaCreateManagedWidget ("form", formWidgetClass, shell, NULL);
  dlabel = XtVaCreateManagedWidget ("dlabel", labelWidgetClass, form, NULL);

  prev = dlabel;
  while (may_pillage) {
    what = get_preferred_pillage (may_pillage);
    button =
      XtVaCreateManagedWidget ("button", commandWidgetClass, form,
			       XtNfromVert, prev,
			       XtNlabel,
			         (XtArgVal)(map_get_infrastructure_text (what)),
			       NULL);
    XtAddCallback (button, XtNcallback, pillage_callback, (XtPointer)what);
    prev = button;
    may_pillage &= (~(what | map_get_infrastructure_prerequisite (what)));
  }
  button =
    XtVaCreateManagedWidget ("closebutton", commandWidgetClass, form,
			     XtNfromVert, prev,
			     NULL);
  XtAddCallback (button, XtNcallback, pillage_callback, NULL);

  xaw_set_relative_position (toplevel, shell, 10, 0);
  XtPopup (shell, XtGrabNone);
}


/****************************************************************
...
*****************************************************************/
Widget popup_message_dialog(Widget parent, char *dialogname, char *text, ...)
{
  va_list args;
  Widget dshell, dform, button;
  Position x, y;
  Dimension width, height;
  void (*fcb)(Widget, XtPointer, XtPointer);
  XtPointer client_data;
  char button_name[512];
  int i;

  XtSetSensitive(parent, FALSE);
  
  dshell=XtCreatePopupShell(dialogname, transientShellWidgetClass,
			    parent, NULL, 0);
  
  dform=XtVaCreateManagedWidget("dform", formWidgetClass, dshell, NULL);
  
  XtVaCreateManagedWidget("dlabel", labelWidgetClass, dform, 
			  XtNlabel, (XtArgVal)text,
			  NULL);   

  i=0;
  va_start(args, text);
  
  while((fcb=va_arg(args, void *))) {
    client_data=va_arg(args, XtPointer);
    sprintf(button_name, "button%d", i++);
    
    button=XtVaCreateManagedWidget(button_name, commandWidgetClass, 
				   dform, NULL);
    XtAddCallback(button, XtNcallback, fcb, client_data);
  }
  
  va_end(args);

  XtVaGetValues(parent, XtNwidth, &width, XtNheight, &height, NULL);
  XtTranslateCoords(parent, (Position) width/10, (Position) height/10,
		    &x, &y);
  XtVaSetValues(dshell, XtNx, x, XtNy, y, NULL);
  
  XtPopup(dshell, XtGrabNone);

  return dshell;
}

/****************************************************************
...
*****************************************************************/
void destroy_message_dialog(Widget button)
{
  XtSetSensitive(XtParent(XtParent(XtParent(button))), TRUE);

  XtDestroyWidget(XtParent(XtParent(button)));
}

static int number_of_columns(int n)
{
#if 0
  /* This would require libm, which isn't worth it for this one little
   * function.  Since the number of units is limited to 100 already, the ifs
   * work fine.  */
  double sqrt(); double ceil();
  return ceil(sqrt((double)n/5.0));
#else
  if(n<=5) return 1;
  else if(n<=20) return 2;
  else if(n<=45) return 3;
  else if(n<=80) return 4;
  else return 5;
#endif
}
static int number_of_rows(int n)
{
  int c=number_of_columns(n);
  return (n+c-1)/c;
}

/****************************************************************
popup the dialog 10% inside the main-window 
*****************************************************************/
void popup_unit_select_dialog(struct tile *ptile)
{
  int i,n,r;
  char buffer[512];
  Arg args[4];
  int nargs;
  Widget unit_select_all_command, unit_select_close_command;
  Widget firstcolumn=0,column=0;

  XtSetSensitive(main_form, FALSE);

  unit_select_dialog_shell = XtCreatePopupShell("unitselectdialogshell", 
						transientShellWidgetClass,
						toplevel, NULL, 0);

  unit_select_form = XtVaCreateManagedWidget("unitselectform", 
					     formWidgetClass, 
					     unit_select_dialog_shell, NULL);

  if(flags_are_transparent)  {
    Pixel bg;
    XtVaGetValues(unit_select_form, XtNbackground, &bg, NULL);
    XSetForeground(display, fill_bg_gc, bg);
  }

  n=unit_list_size(&ptile->units);
  r=number_of_rows(n);

  for(i=0; i<n; i++) {
    struct unit *punit=unit_list_get(&ptile->units, i);
    struct unit_type *punittemp=get_unit_type(punit->type);
    struct city *pcity;
    
    if(!(i%r))  {
      nargs=0;
      if(i)  { XtSetArg(args[nargs], XtNfromHoriz, column); nargs++;}
      column = XtCreateManagedWidget("column", formWidgetClass,
				     unit_select_form,
				     args, nargs);
      if(!i) firstcolumn=column;
    }

    unit_select_ids[i]=punit->id;

    pcity=city_list_find_id(&game.player_ptr->cities, punit->homecity);
    
    sprintf(buffer, "%s(%s)\n%s", 
	    punittemp->name, 
	    pcity ? pcity->name : "",
	    unit_activity_text(punit));

    unit_select_pixmaps[i]=XCreatePixmap(display, XtWindow(map_canvas), 
					 NORMAL_TILE_WIDTH, NORMAL_TILE_HEIGHT,
					 display_depth);

    if (flags_are_transparent) {
      XFillRectangle(display, unit_select_pixmaps[i], fill_bg_gc,
		     0, 0, NORMAL_TILE_WIDTH, NORMAL_TILE_HEIGHT);
    }

    put_unit_pixmap(punit, unit_select_pixmaps[i], 0, 0);

    nargs=0;
    XtSetArg(args[nargs], XtNbitmap, (XtArgVal)unit_select_pixmaps[i]);nargs++;
    XtSetArg(args[nargs], XtNsensitive, 
             can_unit_do_activity(punit, ACTIVITY_IDLE));nargs++;
    if(i%r)  {
      XtSetArg(args[nargs], XtNfromVert, unit_select_commands[i-1]); nargs++;
    }
    unit_select_commands[i]=XtCreateManagedWidget("unitselectcommands", 
						  commandWidgetClass,
						  column, args, nargs);

    nargs=0;
    XtSetArg(args[nargs], XtNlabel, (XtArgVal)buffer); nargs++;
    XtSetArg(args[nargs], XtNfromHoriz, unit_select_commands[i]); nargs++;
    if(i%r) {
      XtSetArg(args[nargs], XtNfromVert, unit_select_commands[i-1]); nargs++;
    }
    unit_select_labels[i]=XtCreateManagedWidget("unitselectlabels", 
						labelWidgetClass, 
						column, args, nargs);

    XtAddCallback(unit_select_commands[i],
		  XtNdestroyCallback,free_bitmap_destroy_callback, NULL);
    XtAddCallback(unit_select_commands[i],
                  XtNcallback, unit_select_callback, NULL);
  }

  unit_select_no=i;


  unit_select_close_command=XtVaCreateManagedWidget("unitselectclosecommand", 
						    commandWidgetClass,
						    unit_select_form,
						    XtNfromVert, firstcolumn,
						    NULL);

  unit_select_all_command=XtVaCreateManagedWidget("unitselectallcommand", 
						  commandWidgetClass,
						  unit_select_form,
						  XtNfromVert, firstcolumn,
						  NULL);

  XtAddCallback(unit_select_close_command, XtNcallback, unit_select_callback, NULL);
  XtAddCallback(unit_select_all_command, XtNcallback, unit_select_all_callback, NULL);

  xaw_set_relative_position(toplevel, unit_select_dialog_shell, 15, 10);
  XtPopup(unit_select_dialog_shell, XtGrabNone);
}

/**************************************************************************
...
**************************************************************************/
void unit_select_all_callback(Widget w, XtPointer client_data, 
			      XtPointer call_data)
{
  int i;

  XtSetSensitive(main_form, TRUE);
  XtDestroyWidget(unit_select_dialog_shell);
  
  for(i=0; i<unit_select_no; i++) {
    struct unit *punit=unit_list_find(&game.player_ptr->units, 
				      unit_select_ids[i]);
    if(punit) {
      request_new_unit_activity(punit, ACTIVITY_IDLE);
      set_unit_focus(punit);
    }
  }
}

/**************************************************************************
...
**************************************************************************/
void unit_select_callback(Widget w, XtPointer client_data, 
			    XtPointer call_data)
{
  int i;

  XtSetSensitive(main_form, TRUE);
  XtDestroyWidget(unit_select_dialog_shell);

  for(i=0; i<unit_select_no; i++) {

    if(unit_select_commands[i]==w) {
      struct unit *punit=unit_list_find(&game.player_ptr->units, 
					unit_select_ids[i]);
      if(punit) {
	request_new_unit_activity(punit, ACTIVITY_IDLE);
	set_unit_focus(punit);
      }
      return;
    }
  }
}





/****************************************************************
popup the dialog 10% inside the main-window 
*****************************************************************/
void popup_races_dialog(void)
{
  Position x, y;
  Dimension width, height;

  XtSetSensitive(main_form, FALSE);

  create_races_dialog();

  XtVaGetValues(toplevel, XtNwidth, &width, XtNheight, &height, NULL);

  XtTranslateCoords(toplevel, (Position) width/10, (Position) height/10,
		    &x, &y);
  XtVaSetValues(races_dialog_shell, XtNx, x, XtNy, y, NULL);

  XtPopup(races_dialog_shell, XtGrabNone);
}

/****************************************************************
...
*****************************************************************/
void popdown_races_dialog(void)
{
  if (races_dialog_shell) {
  XtSetSensitive(main_form, TRUE);
  XtDestroyWidget(races_dialog_shell);
  races_dialog_shell = NULL;
  } /* else there is no dialog shell to destroy */
}


/****************************************************************
...
*****************************************************************/
void create_races_dialog(void)
 
{
  int i, j, maxlen;
  char maxname[MAX_LEN_NAME];
  XtTranslations textfieldtranslations;

  maxlen = 0;
  for(i=0; i<game.nation_count; i++) {
    int len = strlen(get_nation_name(i));
    maxlen = MAX(maxlen, len);
  }
  maxlen = MIN(maxlen, MAX_LEN_NAME-1);
  sprintf(maxname, "%*s", maxlen+2, "W");

  races_dialog_shell = XtCreatePopupShell("racespopup", 
					  transientShellWidgetClass,
					  toplevel, NULL, 0);

  races_form = XtVaCreateManagedWidget("racesform", 
				       formWidgetClass, 
				       races_dialog_shell, NULL);   

  races_label = XtVaCreateManagedWidget("raceslabel", 
				       labelWidgetClass, 
				       races_form, NULL);  

  races_toggles_form = XtVaCreateManagedWidget("racestogglesform", 
					       formWidgetClass, 
					       races_form, 
					       XtNfromVert, races_label, 
					       NULL);   

  free(races_toggles);
  races_toggles = fc_calloc(game.nation_count,sizeof(Widget));

  races_toggles[0]=XtVaCreateManagedWidget("racestoggle0", 
					   toggleWidgetClass, 
					   races_toggles_form,
					   XtNlabel, maxname,
					   NULL);
  if( game.nation_count > 1 )
    races_toggles[1]=XtVaCreateManagedWidget("racestoggle1", 
					     toggleWidgetClass, 
					     races_toggles_form,
					     XtNradioGroup, 
					     races_toggles[0],
					     XtNfromHoriz,
					     races_toggles[0],
					     XtNlabel, maxname,
					     NULL);
  if( game.nation_count > 2 )
    races_toggles[2]=XtVaCreateManagedWidget("racestoggle2", 
					     toggleWidgetClass, 
					     races_toggles_form,
					     XtNradioGroup, 
					     races_toggles[1],
					     XtNfromHoriz,
					     races_toggles[1],
					     XtNlabel, maxname,
					     NULL);

  for( i = 1; i < (game.nation_count+2)/3; i++) {
    int idx = i*3;
    char buf[64];
    sprintf(buf, "racestoggle%d", idx);
    races_toggles[idx]=XtVaCreateManagedWidget(buf, 
					       toggleWidgetClass, 
					       races_toggles_form,
					       XtNradioGroup, 
					       races_toggles[idx-1],
					       XtNfromVert,
					       races_toggles[idx-3],
					       XtNlabel, maxname,
					       NULL);
    for( j=0,idx=i*3+1; (j<2) && (idx<game.nation_count); idx++,j++) {
      sprintf(buf, "racestoggle%d", idx);
      races_toggles[idx]=XtVaCreateManagedWidget(buf,
						 toggleWidgetClass, 
						 races_toggles_form,
						 XtNradioGroup, 
						 races_toggles[idx-1],
						 XtNfromVert,
						 races_toggles[idx-3],
						 XtNfromHoriz,
						 races_toggles[idx-1],
						 XtNlabel, maxname,
					         NULL);
    }
  }

  races_name = XtVaCreateManagedWidget("racesname", 
				       asciiTextWidgetClass, 
				       races_form,
				       XtNfromVert, 
				       (XtArgVal)races_toggles_form,
				       XtNeditType, XawtextEdit,
				       XtNstring, "",
				       NULL);

  races_sex_label = XtVaCreateManagedWidget("racessexlabel", 
				            labelWidgetClass, 
				            races_form, 
					    XtNfromVert, races_name, 
					    NULL);  

  races_sex_form = XtVaCreateManagedWidget("racessexform", 
					   formWidgetClass, 
					   races_form, 
					   XtNfromVert, races_sex_label, 
					   NULL);   

  races_sex_toggles[0]=XtVaCreateManagedWidget("racessextoggle0", 
					       toggleWidgetClass, 
					       races_sex_form,
					       NULL);

  races_sex_toggles[1]=XtVaCreateManagedWidget("racessextoggle1",
					       toggleWidgetClass, 
					       races_sex_form,
					       XtNfromHoriz, 
					       (XtArgVal)races_sex_toggles[0],
					       XtNradioGroup, 
					       races_sex_toggles[0], 
					       NULL);

  races_ok_command = XtVaCreateManagedWidget("racesokcommand", 
					     commandWidgetClass,
					     races_form,
					     NULL);

  for(i=0; i<game.nation_count; i++) {
    XtAddCallback(races_toggles[i], XtNcallback, 
		  races_toggles_callback, (XtPointer) 0);
  }


  XtAddCallback(races_ok_command, XtNcallback, races_buttons_callback, NULL);

  textfieldtranslations = 
    XtParseTranslationTable("<Key>Return: races-dialog-returnkey()");
  XtOverrideTranslations(races_name, textfieldtranslations);


  XtSetKeyboardFocus(races_form, races_name);
  
  XtRealizeWidget(races_dialog_shell);
  
  for(i=0; i<game.nation_count; i++) {
    XtVaSetValues(races_toggles[i], XtNlabel, (XtArgVal)get_nation_name(i), NULL);
  }

}

/****************************************************************
...
*****************************************************************/
void races_dialog_returnkey(Widget w, XEvent *event, String *params,
			    Cardinal *num_params)
{
  x_simulate_button_click(XtNameToWidget(XtParent(w), "racesokcommand"));
}


/**************************************************************************
...
**************************************************************************/
void races_toggles_set_sensitive(int bits1, int bits2)
{
  int i, selected, mybits;

  mybits=bits1;

  for(i=0; i<game.nation_count && i<32; i++) {
    if(mybits&1)
      XtSetSensitive(races_toggles[i], FALSE);
    else
      XtSetSensitive(races_toggles[i], TRUE);
    mybits>>=1;
  }

  mybits=bits2;

  for(i=32; i<game.nation_count; i++) {
    if(mybits&1)
      XtSetSensitive(races_toggles[i], FALSE);
    else
      XtSetSensitive(races_toggles[i], TRUE);
    mybits>>=1;
  }

  if((selected=races_buttons_get_current())==-1)
     return;

  if( (bits1 & (1<<selected)) || 
      ( selected>32 && (bits2 & (1<<(selected-32))) ) )
     XawToggleUnsetCurrent(races_toggles[0]);
}


/**************************************************************************
...
**************************************************************************/
void races_toggles_callback(Widget w, XtPointer client_data, 
			    XtPointer call_data)
{
  int i, dim;
  char **leaders;

  for(i=0; i<game.nation_count; i++)
    if(w==races_toggles[i]) {
      leaders = get_nation_leader_names(i, &dim);
      XtVaSetValues(races_name, XtNstring, leaders[0], NULL);
      return;
    }
}

/**************************************************************************
...
**************************************************************************/
int races_buttons_get_current(void)
{
  int i;
  XtPointer dp, yadp;

  if(!(dp=XawToggleGetCurrent(races_toggles[0])))
    return -1;

  for(i=0; i<game.nation_count; i++) {
    XtVaGetValues(races_toggles[i], XtNradioData, &yadp, NULL);
    if(dp==yadp)
      return i;
  }

  return -1;
}

/**************************************************************************
...
**************************************************************************/
int races_sex_buttons_get_current(void)
{
  int i;
  XtPointer dp, yadp;

  if(!(dp=XawToggleGetCurrent(races_sex_toggles[0])))
    return -1;

  for(i=0; i<2; i++) {
    XtVaGetValues(races_sex_toggles[i], XtNradioData, &yadp, NULL);
    if(dp==yadp)
      return i;
  }

  return -1;
}

/**************************************************************************
...
**************************************************************************/
void races_buttons_callback(Widget w, XtPointer client_data, 
			    XtPointer call_data)
{
  int selected, selected_sex;
  XtPointer dp;

  struct packet_alloc_nation packet;

  if((selected=races_buttons_get_current())==-1) {
    append_output_window("You must select a nation.");
    return;
  }

  if((selected_sex=races_sex_buttons_get_current())==-1) {
    append_output_window("You must select your sex.");
    return;
  }

  XtVaGetValues(races_name, XtNstring, &dp, NULL);

  /* perform a minimum of sanity test on the name */
  packet.nation_no=selected;
  packet.is_male = selected_sex? 0: 1;     /* first button is male */
  strncpy(packet.name, (char*)dp, MAX_LEN_NAME);
  packet.name[MAX_LEN_NAME-1]='\0';
  
  if(!get_sane_name(packet.name)) {
    append_output_window("You must type a legal name.");
    return;
  }

  packet.name[0]=toupper(packet.name[0]);

  send_packet_alloc_nation(&aconnection, &packet);
}

/**************************************************************************
  Frees a bitmap associated with a Widget when it is destroyed
**************************************************************************/
void free_bitmap_destroy_callback(Widget w, XtPointer client_data, 
				  XtPointer call_data)
{
  Pixmap pm;

  XtVaGetValues(w,XtNbitmap,&pm,NULL);
  if(pm) XFreePixmap(XtDisplay(w),pm);
}

/**************************************************************************
  Destroys its widget.  Usefull for a popdown callback on pop-ups that
  won't get resused.
**************************************************************************/
void destroy_me_callback(Widget w, XtPointer client_data, 
			 XtPointer call_data)
{
  XtDestroyWidget(w);
}

/**************************************************************************
  Adjust tax rates from main window
**************************************************************************/
void taxrates_callback(Widget w, XtPointer client_data, XtPointer call_data)
{
  int tax_end,lux_end,sci_end;
  size_t i;
  int delta=10;

  struct packet_player_request packet;
  i= (size_t)client_data;
  
  lux_end= game.player_ptr->economic.luxury;
  sci_end= lux_end + game.player_ptr->economic.science;
  tax_end= 100;

  packet.luxury= game.player_ptr->economic.luxury;
  packet.science= game.player_ptr->economic.science;
  packet.tax= game.player_ptr->economic.tax;

  i*= 10;
  if(i<lux_end){
    packet.luxury-= delta; packet.science+= delta;
  }else if(i<sci_end){
    packet.science-= delta; packet.tax+= delta;
   }else{
    packet.tax-= delta; packet.luxury+= delta;
  }
  send_packet_player_request(&aconnection, &packet, PACKET_PLAYER_RATES);

}

/**************************************************************************
  Add punit to queue of caravan arrivals, and popup a window for the
  next arrival in the queue, if there is not already a popup, and
  re-checking that a popup is appropriate.
  If punit is NULL, just do for the next arrival in the queue.
**************************************************************************/
void process_caravan_arrival(struct unit *punit)
{
  static struct genlist arrival_queue;
  static int is_init_arrival_queue = 0;
  int *p_id;

  /* arrival_queue is a list of individually malloc-ed ints with
     punit.id values, for units which have arrived. */

  if (!is_init_arrival_queue) {
    genlist_init(&arrival_queue);
    is_init_arrival_queue = 1;
  }

  if (punit) {
    p_id = fc_malloc(sizeof(int));
    *p_id = punit->id;
    genlist_insert(&arrival_queue, p_id, -1);
  }

  /* There can only be one dialog at a time: */
  if (caravan_dialog) {
    return;
  }
  
  while (genlist_size(&arrival_queue)) {
    int id;
    
    p_id = genlist_get(&arrival_queue, 0);
    genlist_unlink(&arrival_queue, p_id);
    id = *p_id;
    free(p_id);
    punit = unit_list_find(&game.player_ptr->units, id);

    if (punit && (unit_can_help_build_wonder_here(punit)
		  || unit_can_est_traderoute_here(punit))
	&& (!game.player_ptr->ai.control || ai_popup_windows)) {
      struct city *pcity_dest = map_get_city(punit->x, punit->y);
      struct city *pcity_homecity = find_city_by_id(punit->homecity);
      if (pcity_dest && pcity_homecity) {
	popup_caravan_dialog(punit, pcity_homecity, pcity_dest);
	return;
      }
    }
  }
}
