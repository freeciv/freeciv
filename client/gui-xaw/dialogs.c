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
#include <X11/Xaw/AsciiText.h>  
#include <X11/Xaw/Command.h>
#include <X11/Xaw/Form.h>
#include <X11/Xaw/Label.h>
#include <X11/Xaw/List.h>
#include <X11/Xaw/MenuButton.h>
#include <X11/Xaw/SimpleMenu.h>
#include <X11/Xaw/SmeBSB.h>
#include <X11/Xaw/Toggle.h>     

#include "capability.h"
#include "fcintl.h"
#include "game.h"
#include "government.h"
#include "log.h"
#include "map.h"
#include "mem.h"
#include "packets.h"
#include "player.h"
#include "rand.h"
#include "support.h"

#include "chatline.h"
#include "civclient.h"
#include "clinet.h"
#include "control.h" /* request_xxx and set_unit_focus */
#include "graphics.h"
#include "gui_stuff.h"
#include "mapctrl.h"
#include "mapview.h"
#include "tilespec.h"

#include "dialogs.h"

extern Widget toplevel, main_form, map_canvas;

extern Display	*display;
extern int display_depth;
extern int ai_popup_windows;
extern GC fill_bg_gc;

/******************************************************************/
static Widget races_dialog_shell=NULL;
static Widget races_form, races_toggles_form, races_label;
static Widget *races_toggles=NULL;
static int *races_toggles_to_nations=NULL;
static Widget races_leader_form, races_leader;
static Widget races_leader_pick_popupmenu, races_leader_pick_menubutton;
static Widget races_sex_toggles[2], races_sex_form, races_sex_label;
static Widget races_style_form, races_style_label;
static Widget *races_style_toggles=NULL;
static Widget races_action_form;
static Widget races_ok_command, races_disconnect_command, races_quit_command;

/******************************************************************/
Widget spy_tech_shell;
Widget spy_advances_list, spy_advances_list_label;
Widget spy_steal_command;

int spy_tech_shell_is_modal;
int advance_type[A_LAST+1];
int steal_advance = 0;

/******************************************************************/
Widget spy_sabotage_shell;
Widget spy_improvements_list, spy_improvements_list_label;
Widget spy_sabotage_command;

int spy_sabotage_shell_is_modal;
int improvement_type[B_LAST+1];
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
void create_about_dialog(void);
void create_help_dialog(Widget *shell);


/******************************************************************/
static void create_races_dialog(void);
static void races_leader_set_values(int race, int lead);
static int races_buttons_get_current(void);
static int races_sex_buttons_get_current(void);
static int races_style_buttons_get_current(void);
static void races_sex_buttons_set_current(int i);

static int races_indirect_compare(const void *first, const void *second);

static void races_toggles_callback(Widget w, XtPointer client_data, 
				   XtPointer call_data);
static void races_leader_pick_callback(Widget w, XtPointer client_data,
				       XtPointer call_data);
static void races_ok_command_callback(Widget w, XtPointer client_data, 
				      XtPointer call_data);
static void races_disconnect_command_callback(Widget w, XtPointer client_data, 
					      XtPointer call_data);
static void races_quit_command_callback(Widget w, XtPointer client_data, 
					XtPointer call_data);

/******************************************************************/
void unit_select_callback(Widget w, XtPointer client_data, 
			    XtPointer call_data);
void unit_select_all_callback(Widget w, XtPointer client_data, 
			    XtPointer call_data);


/******************************************************************/
static int city_style_idx[64];     /* translation table basic style->city_style  */
static int city_style_ridx[64];    /* translation table the other way            */
                                   /* they in fact limit the num of styles to 64 */
static int b_s_num; /* num basic city styles, i.e. those that you can start with */


/******************************************************************/

int is_showing_government_dialog;

int is_showing_pillage_dialog = FALSE;
int unit_to_use_to_pillage;

int caravan_city_id;
int caravan_unit_id;

int diplomat_dialog_open = 0;
int diplomat_id;
int diplomat_target_id;

struct city *pcity_caravan_dest;
struct unit *punit_caravan;

static Widget caravan_dialog;

static int is_showing_unit_connect_dialog = FALSE;
static int unit_to_use_to_connect;
static int connect_unit_x;
static int connect_unit_y;

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

  notify_command =
    I_L(XtVaCreateManagedWidget("notifycommand", 
				commandWidgetClass,
				notify_form,
				NULL));

  XtVaGetValues(notify_label, XtNwidth, &width, NULL);
  XtVaGetValues(notify_headline, XtNwidth, &width2, NULL);
  if(width>width2)
    XtVaSetValues(notify_headline, XtNwidth, width, NULL); 
  
  XtAddCallback(notify_command, XtNcallback, notify_command_callback, NULL);
  
  xaw_set_relative_position(toplevel, notify_dialog_shell, 25, 5);
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
  
  if (y < 0 || y >= map.ysize) {
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

  notify_command =
    I_L(XtVaCreateManagedWidget("notifycommand", 
				commandWidgetClass,
				notify_form,
				NULL));

  notify_goto_command =
    I_L(XtVaCreateManagedWidget("notifygotocommand", 
				commandWidgetClass,
				notify_form,
				NULL));
  
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
  xaw_set_relative_position(toplevel, notify_dialog_shell, 25, 5);
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
    my_snprintf(buf, sizeof(buf),
		_("Bribe unit for %d gold?\n"
		  "Treasury contains %d gold."), 
		punit->bribe_cost, game.player_ptr->economic.gold);
    popup_message_dialog(toplevel, "diplomatbribedialog", buf,
			 diplomat_bribe_yes_callback, 0, 0,
			 diplomat_bribe_no_callback, 0, 0,
			 NULL);
  } else {
    my_snprintf(buf, sizeof(buf),
		_("Bribing the unit costs %d gold.\n"
		  "Treasury contains %d gold."), 
		punit->bribe_cost, game.player_ptr->economic.gold);
    popup_message_dialog(toplevel, "diplomatnogolddialog", buf,
			 diplomat_bribe_no_callback, 0, 0,
			 NULL);
  }
}

/****************************************************************
...
*****************************************************************/
static void diplomat_sabotage_callback(Widget w, XtPointer client_data, 
				       XtPointer call_data)
{
  destroy_message_dialog(w);
  diplomat_dialog_open=0;

  if(find_unit_by_id(diplomat_id) && 
     find_city_by_id(diplomat_target_id)) { 
    struct packet_diplomat_action req;
    
    req.action_type=DIPLOMAT_SABOTAGE;
    req.diplomat_id=diplomat_id;
    req.target_id=diplomat_target_id;
    req.value = -1;

    send_packet_diplomat_action(&aconnection, &req);
  }

  process_diplomat_arrival(NULL, 0);
}

/****************************************************************
...
*****************************************************************/
static void diplomat_embassy_callback(Widget w, XtPointer client_data, 
				      XtPointer call_data)
{
  destroy_message_dialog(w);
  diplomat_dialog_open=0;

  if(find_unit_by_id(diplomat_id) && 
     (find_city_by_id(diplomat_target_id))) { 
    struct packet_diplomat_action req;
    
    req.action_type=DIPLOMAT_EMBASSY;
    req.diplomat_id=diplomat_id;
    req.target_id=diplomat_target_id;
    
    send_packet_diplomat_action(&aconnection, &req);
  }

  process_diplomat_arrival(NULL, 0);
}

/****************************************************************
...
*****************************************************************/
static void diplomat_investigate_callback(Widget w, XtPointer client_data, 
					  XtPointer call_data)
{
  destroy_message_dialog(w);
  diplomat_dialog_open=0;

  if(find_unit_by_id(diplomat_id) && 
     (find_city_by_id(diplomat_target_id))) { 
    struct packet_diplomat_action req;
    
    req.action_type=DIPLOMAT_INVESTIGATE;
    req.diplomat_id=diplomat_id;
    req.target_id=diplomat_target_id;
    
    send_packet_diplomat_action(&aconnection, &req);
  }

  process_diplomat_arrival(NULL, 0);
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
  diplomat_dialog_open=0;

  if(find_unit_by_id(diplomat_id) && 
     (find_city_by_id(diplomat_target_id))) { 
    struct packet_diplomat_action req;

    req.action_type=SPY_POISON;
    req.diplomat_id=diplomat_id;
    req.target_id=diplomat_target_id;

    send_packet_diplomat_action(&aconnection, &req);
  }

  process_diplomat_arrival(NULL, 0);
}

/****************************************************************
...
*****************************************************************/
static void diplomat_steal_callback(Widget w, XtPointer client_data, 
				    XtPointer call_data)
{
  destroy_message_dialog(w);
  diplomat_dialog_open=0;

  if(find_unit_by_id(diplomat_id) && 
     find_city_by_id(diplomat_target_id)) { 
    struct packet_diplomat_action req;

    req.action_type=DIPLOMAT_STEAL;
    req.diplomat_id=diplomat_id;
    req.target_id=diplomat_target_id;
    req.value=0;

    send_packet_diplomat_action(&aconnection, &req);
  }

  process_diplomat_arrival(NULL, 0);
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

  process_diplomat_arrival(NULL, 0);
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

  process_diplomat_arrival(NULL, 0);
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
    freelog(LOG_ERROR, "Bug in spy steal tech code");
    process_diplomat_arrival(NULL, 0);
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

  process_diplomat_arrival(NULL, 0);
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
    freelog(LOG_ERROR, "Bug in spy sabotage code");
    process_diplomat_arrival(NULL, 0);
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

  process_diplomat_arrival(NULL, 0);
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

  static char *advances_can_steal[A_LAST+1]; 

  spy_tech_shell =
    I_T(XtVaCreatePopupShell("spystealtechpopup", 
			     (make_modal ? transientShellWidgetClass :
			      topLevelShellWidgetClass),
			     toplevel, 0));  
  
  spy_tech_form = XtVaCreateManagedWidget("spystealtechform", 
					     formWidgetClass,
					     spy_tech_shell,
					     NULL);   

  spy_advances_list_label =
    I_L(XtVaCreateManagedWidget("spystealtechlistlabel", labelWidgetClass, 
				spy_tech_form, NULL));

  spy_advances_list = XtVaCreateManagedWidget("spystealtechlist", 
					      listWidgetClass,
					      spy_tech_form,
					      NULL);

  close_command =
    I_L(XtVaCreateManagedWidget("spystealtechclosecommand", commandWidgetClass,
				spy_tech_form, NULL));
  
  spy_steal_command =
    I_L(XtVaCreateManagedWidget("spystealtechcommand", commandWidgetClass,
				spy_tech_form,
				XtNsensitive, False,
				NULL));
  

  XtAddCallback(spy_advances_list, XtNcallback, spy_select_tech_callback, NULL);
  XtAddCallback(close_command, XtNcallback, spy_close_tech_callback, NULL);
  XtAddCallback(spy_steal_command, XtNcallback, spy_steal_callback, NULL);
  XtRealizeWidget(spy_tech_shell);

  /* Now populate the list */
  
  j = 0;
  advances_can_steal[j] = _("NONE");
  advance_type[j] = -1;

  if (pvictim) { /* you don't want to know what lag can do -- Syela */
    for(i=A_FIRST; i<game.num_tech_types; i++) {
      if(get_invention(pvictim, i)==TECH_KNOWN && 
         (get_invention(pplayer, i)==TECH_UNKNOWN || 
          get_invention(pplayer, i)==TECH_REACHABLE)) {
      
        advances_can_steal[j] = advances[i].name;
        advance_type[j++] = i;
      }
    }
    if(j > 0) {
      advances_can_steal[j] = _("At Spy's Discretion");
      advance_type[j++] = game.num_tech_types;
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

  static char *improvements_can_sabotage[B_LAST+1]; 
  
  spy_sabotage_shell =
    I_T(XtVaCreatePopupShell("spysabotageimprovementspopup", 
			     (make_modal ? transientShellWidgetClass :
			      topLevelShellWidgetClass),
			     toplevel, 0));  
  
  spy_sabotage_form = XtVaCreateManagedWidget("spysabotageimprovementsform", 
					     formWidgetClass,
					     spy_sabotage_shell,
					     NULL);   

  spy_improvements_list_label =
    I_L(XtVaCreateManagedWidget("spysabotageimprovementslistlabel", 
				labelWidgetClass, 
				spy_sabotage_form,
				NULL));

  spy_improvements_list = XtVaCreateManagedWidget("spysabotageimprovementslist", 
					      listWidgetClass,
					      spy_sabotage_form,
					      NULL);

  close_command =
    I_L(XtVaCreateManagedWidget("spysabotageimprovementsclosecommand", 
				commandWidgetClass,
				spy_sabotage_form,
				NULL));
  
  spy_sabotage_command =
    I_L(XtVaCreateManagedWidget("spysabotageimprovementscommand", 
				commandWidgetClass,
				spy_sabotage_form,
				XtNsensitive, False,
				NULL));
  

  XtAddCallback(spy_improvements_list, XtNcallback, spy_select_improvement_callback, NULL);
  XtAddCallback(close_command, XtNcallback, spy_close_sabotage_callback, NULL);
  XtAddCallback(spy_sabotage_command, XtNcallback, spy_sabotage_callback, NULL);
  XtRealizeWidget(spy_sabotage_shell);

  /* Now populate the list */
  
  j = 0;
  improvements_can_sabotage[j] = _("City Production");
  improvement_type[j++] = -1;

  for(i=0; i<B_LAST; i++) 
    if(i != B_PALACE && pcity->improvements[i] && !is_wonder(i)) {
      improvements_can_sabotage[j] = get_impr_name_ex(pcity, i);
      improvement_type[j++] = i;
    }  

  if(j > 1) {
    improvements_can_sabotage[j] = _("At Spy's Discretion");
    improvement_type[j++] = B_LAST;
  } else {
    improvement_type[0] = B_LAST; /* fake "discretion", since must be production */
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
  diplomat_dialog_open=0;

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
 Requests up-to-date list of improvements, the return of
 which will trigger the popup_sabotage_dialog() function.
*****************************************************************/
static void spy_request_sabotage_list(Widget w, XtPointer client_data,
				      XtPointer call_data)
{
  destroy_message_dialog(w);
  diplomat_dialog_open=0;

  if(find_unit_by_id(diplomat_id) &&
     (find_city_by_id(diplomat_target_id))) {
    struct packet_diplomat_action req;

    req.action_type = SPY_GET_SABOTAGE_LIST;
    req.diplomat_id = diplomat_id;
    req.target_id = diplomat_target_id;

    send_packet_diplomat_action(&aconnection, &req);
  }
}

/****************************************************************
 Pops-up the Spy sabotage dialog, upon return of list of
 available improvements requested by the above function.
*****************************************************************/
void popup_sabotage_dialog(struct city *pcity)
{  
  if(!spy_sabotage_shell){
    Position x, y;
    Dimension width, height;
    spy_sabotage_shell_is_modal=1;

    create_improvements_list(game.player_ptr, pcity, spy_sabotage_shell_is_modal);
    
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

  process_diplomat_arrival(NULL, 0);
}

/****************************************************************
...
*****************************************************************/
static void diplomat_incite_no_callback(Widget w, XtPointer client_data, 
					XtPointer call_data)
{
  destroy_message_dialog(w);

  process_diplomat_arrival(NULL, 0);
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
  diplomat_dialog_open=0;

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
    my_snprintf(buf, sizeof(buf),
		_("Incite a revolt for %d gold?\n"
		  "Treasury contains %d gold."), 
		pcity->incite_revolt_cost, game.player_ptr->economic.gold);
   diplomat_target_id = pcity->id;
   popup_message_dialog(toplevel, "diplomatrevoltdialog", buf,
			diplomat_incite_yes_callback, 0, 0,
			diplomat_incite_no_callback, 0, 0,
			NULL);
  } else {
   my_snprintf(buf, sizeof(buf),
	       _("Inciting a revolt costs %d gold.\n"
		 "Treasury contains %d gold."), 
	       pcity->incite_revolt_cost, game.player_ptr->economic.gold);
   popup_message_dialog(toplevel, "diplomatnogolddialog", buf,
			diplomat_incite_no_callback, 0, 0,
			NULL);
  }
}


/****************************************************************
...
*****************************************************************/
static void diplomat_cancel_callback(Widget w, XtPointer a, XtPointer b)
{
  destroy_message_dialog(w);
  diplomat_dialog_open=0;

  process_diplomat_arrival(NULL, 0);
}


/****************************************************************
...
*****************************************************************/
void popup_diplomat_dialog(struct unit *punit, int dest_x, int dest_y)
{
  struct city *pcity;
  struct unit *ptunit;
  Widget shl;
  char buf[128];

  diplomat_id=punit->id;

  if((pcity=map_get_city(dest_x, dest_y))){
    /* Spy/Diplomat acting against a city */

    diplomat_target_id=pcity->id;
    my_snprintf(buf, sizeof(buf),
		_("Your %s has arrived at %s.\nWhat is your command?"),
		unit_name(punit->type), pcity->name);

    if(!unit_flag(punit->type, F_SPY)){
      shl=popup_message_dialog(toplevel, "diplomatdialog", buf,
			       diplomat_embassy_callback, 0, 1,
			       diplomat_investigate_callback, 0, 1,
			       diplomat_sabotage_callback, 0, 1,
			       diplomat_steal_callback, 0, 1,
			       diplomat_incite_callback, 0, 1,
			       diplomat_cancel_callback, 0, 0,
			       NULL);
      
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
      shl=popup_message_dialog(toplevel, "spydialog", buf,
			       diplomat_embassy_callback, 0,  1,
			       diplomat_investigate_callback, 0, 1,
			       spy_poison_callback,0, 1,
			       spy_request_sabotage_list, 0, 1,
			       spy_steal_popup, 0, 1,
			       diplomat_incite_callback, 0, 1,
			       diplomat_cancel_callback, 0, 0,
			       NULL);
      
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

    diplomat_dialog_open=1;
  }else{ 
    if((ptunit=unit_list_get(&map_get_tile(dest_x, dest_y)->units, 0))){
      /* Spy/Diplomat acting against a unit */
      
      char *message = (!unit_flag(punit->type, F_SPY))?
	_("Sir, the diplomat is waiting for your command"):
	_("Sir, the spy is waiting for your command");
      
      diplomat_target_id=ptunit->id;

      shl=popup_message_dialog(toplevel, "spybribedialog",
			       message,
			       diplomat_bribe_callback, 0, 0,
			       spy_sabotage_unit_callback, 0, 0,
			       diplomat_cancel_callback, 0, 0,
			       NULL);
	
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
int diplomat_dialog_is_open(void)
{
  return diplomat_dialog_open;
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
  
  my_snprintf(buf, sizeof(buf),
	      _("Your caravan from %s reaches the city of %s.\nWhat now?"),
	      phomecity->name, pdestcity->name);
  
  caravan_city_id=pdestcity->id; /* callbacks need these */
  caravan_unit_id=punit->id;
  
  caravan_dialog=popup_message_dialog(toplevel, "caravandialog", 
			   buf,
			   caravan_establish_trade_callback, 0, 0,
			   caravan_help_build_wonder_callback, 0, 0,
			   caravan_keep_moving_callback, 0, 0,
			   NULL);
  
  if(!can_establish_trade_route(phomecity, pdestcity))
    XtSetSensitive(XtNameToWidget(caravan_dialog, "*button0"), FALSE);
  
  if(!unit_can_help_build_wonder(punit, pdestcity))
    XtSetSensitive(XtNameToWidget(caravan_dialog, "*button1"), FALSE);
}

/****************************************************************
...
*****************************************************************/
int caravan_dialog_is_open(void)
{
  return BOOL_VAL(caravan_dialog);
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

  shell = I_T(XtCreatePopupShell("governmentdialog", transientShellWidgetClass,
				 toplevel, NULL, 0));
  form = XtVaCreateManagedWidget("form", formWidgetClass, shell, NULL);
  dlabel = I_L(XtVaCreateManagedWidget("dlabel", labelWidgetClass, form, NULL));

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
		       _("You say you wanna revolution?"),
		       revolution_callback_yes, 0, 0,
		       revolution_callback_no, 0, 0,
		       NULL);
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

  shell = I_T(XtCreatePopupShell("pillagedialog", transientShellWidgetClass,
				 toplevel, NULL, 0));
  form = XtVaCreateManagedWidget ("form", formWidgetClass, shell, NULL);
  dlabel = I_L(XtVaCreateManagedWidget("dlabel", labelWidgetClass, form, NULL));

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
    I_L(XtVaCreateManagedWidget("closebutton", commandWidgetClass, form,
				XtNfromVert, prev,
				NULL));
  XtAddCallback (button, XtNcallback, pillage_callback, NULL);

  xaw_set_relative_position (toplevel, shell, 10, 0);
  XtPopup (shell, XtGrabNone);
}

/****************************************************************
common code for all buttons in unit connect dialog
*****************************************************************/
static void unit_connect_callback (Widget w, XtPointer client_data, 
				   XtPointer call_data)
{
  struct unit *punit;
  int activity = (int)client_data;

  if (!is_showing_unit_connect_dialog) {
    destroy_message_dialog(w);
    return;
  }

  punit = find_unit_by_id(unit_to_use_to_connect);

  if (punit) {
    if (activity != ACTIVITY_IDLE) {
      struct packet_unit_connect req;
      req.activity_type = activity;
      req.unit_id = punit->id;
      req.dest_x = connect_unit_x;
      req.dest_y = connect_unit_y;
      send_packet_unit_connect(&aconnection, &req);
    }
    else {
      update_unit_info_label(punit);
    }
  }

  destroy_message_dialog(w);
  is_showing_unit_connect_dialog = FALSE;
}

/****************************************************************
popup dialog which prompts for activity type (unit connect)
*****************************************************************/
void popup_unit_connect_dialog(struct unit *punit, int dest_x, int dest_y)
{
  Widget shell, form, dlabel, button, prev;
  int activity;

  if (is_showing_unit_connect_dialog)
    return;

  is_showing_unit_connect_dialog = TRUE;
  unit_to_use_to_connect = punit->id;
  connect_unit_x = dest_x;
  connect_unit_y = dest_y;

  XtSetSensitive (toplevel, FALSE);

  shell = I_T(XtCreatePopupShell("unitconnectdialog", transientShellWidgetClass,
				 toplevel, NULL, 0));
  form = XtVaCreateManagedWidget ("form", formWidgetClass, shell, NULL);
  dlabel = I_L(XtVaCreateManagedWidget("dlabel", labelWidgetClass, form, NULL));

  prev = dlabel;
  for (activity = ACTIVITY_IDLE + 1; activity < ACTIVITY_LAST; activity++) {
    if (! can_unit_do_connect (punit, activity)) continue;

    button =
      XtVaCreateManagedWidget ("button", commandWidgetClass, form,
			       XtNfromVert, prev,
			       XtNlabel,
			         (XtArgVal)(get_activity_text (activity)),
			       NULL);
    XtAddCallback (button, XtNcallback, unit_connect_callback, 
		   (XtPointer)activity);
    prev = button;
  }
  button =
    I_L(XtVaCreateManagedWidget("closebutton", commandWidgetClass, form,
				XtNfromVert, prev,
				NULL));
  XtAddCallback (button, XtNcallback, unit_connect_callback, ACTIVITY_IDLE);

  xaw_set_relative_position (toplevel, shell, 10, 0);
  XtPopup (shell, XtGrabNone);
}

/****************************************************************
  Parameters after named parameters should be in triplets:
  - callback, callback_data, fixed_width 
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
  int i, fixed_width;

  XtSetSensitive(parent, FALSE);
  
  I_T(dshell=XtCreatePopupShell(dialogname, transientShellWidgetClass,
				parent, NULL, 0));
  
  dform=XtVaCreateManagedWidget("dform", formWidgetClass, dshell, NULL);

  /* caller should i18n text as desired */
  XtVaCreateManagedWidget("dlabel", labelWidgetClass, dform, 
			  XtNlabel, (XtArgVal)text,
			  NULL);   

  i=0;
  va_start(args, text);

  while((fcb=((void(*)(Widget, XtPointer, XtPointer))(va_arg(args, void *))))) {
    client_data=va_arg(args, XtPointer);
    fixed_width=va_arg(args, int);
    my_snprintf(button_name, sizeof(button_name), "button%d", i++);
    
    button=XtVaCreateManagedWidget(button_name, commandWidgetClass, 
				   dform, NULL);
    if (fixed_width) {
      I_LW(button);
    } else {
      I_L(button);
    }
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

  unit_select_dialog_shell =
    I_T(XtCreatePopupShell("unitselectdialogshell", 
			   transientShellWidgetClass,
			   toplevel, NULL, 0));

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

    pcity=player_find_city_by_id(game.player_ptr, punit->homecity);
    
    my_snprintf(buffer, sizeof(buffer), "%s(%s)\n%s", 
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

  unit_select_close_command =
    I_L(XtVaCreateManagedWidget("unitselectclosecommand", 
				commandWidgetClass,
				unit_select_form,
				XtNfromVert, firstcolumn,
				NULL));

  unit_select_all_command =
    I_L(XtVaCreateManagedWidget("unitselectallcommand", 
				commandWidgetClass,
				unit_select_form,
				XtNfromVert, firstcolumn,
				NULL));

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
    struct unit *punit = player_find_unit_by_id(game.player_ptr,
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
      struct unit *punit = player_find_unit_by_id(game.player_ptr,
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
popup the dialog 5% inside the main-window 
*****************************************************************/
void popup_races_dialog(void)
{
  Position x, y;
  Dimension width, height;

  XtSetSensitive(main_form, FALSE);

  create_races_dialog();

  XtVaGetValues(toplevel, XtNwidth, &width, XtNheight, &height, NULL);

  XtTranslateCoords(toplevel, (Position) width/20, (Position) height/20,
		    &x, &y);
  XtVaSetValues(races_dialog_shell, XtNx, x, XtNy, y, NULL);

  XtPopup(races_dialog_shell, XtGrabNone);
  XtSetKeyboardFocus(toplevel, races_dialog_shell);
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
  int per_row = 5;
  int i, j, len, maxracelen, index;
  char maxracename[MAX_LEN_NAME];
  char namebuf[64];
  int space;
  XtWidgetGeometry geom;

  maxracelen = 0;
  for(i=0; i<game.playable_nation_count; i++) {
    len = strlen(get_nation_name(i));
    maxracelen = MAX(maxracelen, len);
  }
  maxracelen = MIN(maxracelen, MAX_LEN_NAME-1);
  my_snprintf(maxracename, sizeof(maxracename), "%*s", maxracelen+2, "W");

  races_dialog_shell = I_T(XtCreatePopupShell("racespopup", 
					  transientShellWidgetClass,
					  toplevel, NULL, 0));

  races_form = XtVaCreateManagedWidget("racesform", 
				       formWidgetClass, 
				       races_dialog_shell, NULL);   

  races_label = I_L(XtVaCreateManagedWidget("raceslabel", 
				       labelWidgetClass, 
				       races_form, NULL));  

  races_toggles_form = XtVaCreateManagedWidget("racestogglesform", 
					       formWidgetClass, 
					       races_form, 
					       XtNfromVert, races_label, 
					       NULL);   

  free(races_toggles);
  races_toggles = fc_calloc(game.playable_nation_count,sizeof(Widget));
  free(races_toggles_to_nations);
  races_toggles_to_nations = fc_calloc(game.playable_nation_count,sizeof(int));

  for( i = 0; i < ((game.playable_nation_count-1)/per_row)+1; i++) {
    index = i * per_row;
    my_snprintf(namebuf, sizeof(namebuf), "racestoggle%d", index);
    if( i == 0 ) {
      races_toggles[index] =
	XtVaCreateManagedWidget(namebuf,
				toggleWidgetClass,
				races_toggles_form,
				XtNlabel, maxracename,
				NULL);
    } else {
      races_toggles[index] =
	XtVaCreateManagedWidget(namebuf,
				toggleWidgetClass,
				races_toggles_form,
				XtNradioGroup,
				races_toggles[index-1],
				XtNfromVert,
				races_toggles[index-per_row],
				XtNlabel, maxracename,
				NULL);
    }

    for( j = 1; j < per_row; j++) {
      index = i * per_row + j;
      if( index >= game.playable_nation_count ) break;
      my_snprintf(namebuf, sizeof(namebuf), "racestoggle%d", index);
      if( i == 0 ) {
	races_toggles[index] =
	  XtVaCreateManagedWidget(namebuf,
				  toggleWidgetClass,
				  races_toggles_form,
				  XtNradioGroup,
				  races_toggles[index-1],
				  XtNfromHoriz,
				  races_toggles[index-1],
				  XtNlabel, maxracename,
				  NULL);
      } else {
	races_toggles[index] =
	  XtVaCreateManagedWidget(namebuf,
				  toggleWidgetClass,
				  races_toggles_form,
				  XtNradioGroup,
				  races_toggles[index-1],
				  XtNfromVert,
				  races_toggles[index-per_row],
				  XtNfromHoriz,
				  races_toggles[index-1],
				  XtNlabel, maxracename,
				  NULL);
      }
    }
  }

  races_leader_form = XtVaCreateManagedWidget("racesleaderform",
					      formWidgetClass,
					      races_form,
					      XtNfromVert, races_toggles_form,
					      NULL);

  XtVaGetValues(races_leader_form, XtNdefaultDistance, &space, NULL);
  XtQueryGeometry(races_toggles[0], NULL, &geom);
  races_leader = XtVaCreateManagedWidget("racesleader",
					 asciiTextWidgetClass,
					 races_leader_form,
					 XtNeditType, XawtextEdit,
					 XtNwidth,
					   space + 2*(geom.width + geom.border_width),
					 XtNstring, "",
					 NULL);

  races_leader_pick_popupmenu = 0;

  races_leader_pick_menubutton =
    I_L(XtVaCreateManagedWidget("racesleaderpickmenubutton",
			    menuButtonWidgetClass,
			    races_leader_form,
			    XtNfromHoriz, races_leader,
			    NULL));

  races_sex_label = I_L(XtVaCreateManagedWidget("racessexlabel", 
				            labelWidgetClass, 
				            races_form, 
					    XtNfromVert, races_leader_form, 
					    NULL));  

  races_sex_form = XtVaCreateManagedWidget("racessexform", 
					   formWidgetClass, 
					   races_form, 
					   XtNfromVert, races_sex_label, 
					   NULL);   

  races_sex_toggles[0] =
    I_L(XtVaCreateManagedWidget("racessextoggle0", 
				toggleWidgetClass, 
				races_sex_form,
				NULL));

  races_sex_toggles[1] =
    I_L(XtVaCreateManagedWidget("racessextoggle1",
				toggleWidgetClass, 
				races_sex_form,
				XtNfromHoriz, 
				(XtArgVal)races_sex_toggles[0],
				XtNradioGroup, 
				races_sex_toggles[0], 
				NULL));

  /* find out styles that can be used at the game beginning */
  for(i=0,b_s_num=0; i<game.styles_count && i<64; i++) {
    if(city_styles[i].techreq == A_NONE) {
      city_style_idx[b_s_num] = i;
      city_style_ridx[i] = b_s_num;
      b_s_num++;
    }
  }

  races_style_label = I_L(XtVaCreateManagedWidget("racesstylelabel", 
					      labelWidgetClass, 
					      races_form,
					      XtNfromVert, races_sex_form, 
					      NULL));  

  races_style_form = XtVaCreateManagedWidget("racesstyleform", 
					       formWidgetClass, 
					       races_form, 
					       XtNfromVert, races_style_label, 
					       NULL);   

  free(races_style_toggles);
  races_style_toggles = fc_calloc(b_s_num,sizeof(Widget));

  for( i = 0; i < ((b_s_num-1)/per_row)+1; i++) {
    index = i * per_row;
    my_snprintf(namebuf, sizeof(namebuf), "racesstyle%d", index);
    if( i == 0 ) {
      races_style_toggles[index] =
	XtVaCreateManagedWidget(namebuf,
				toggleWidgetClass,
				races_style_form,
				XtNlabel, maxracename,
				NULL);
    } else {
      races_style_toggles[index] =
	XtVaCreateManagedWidget(namebuf,
				toggleWidgetClass,
				races_style_form,
				XtNradioGroup,
				races_style_toggles[index-1],
				XtNfromVert,
				races_style_toggles[index-per_row],
				XtNlabel, maxracename,
				NULL);
    }

    for( j = 1; j < per_row; j++) {
      index = i * per_row + j;
      if( index >= b_s_num ) break;
      my_snprintf(namebuf, sizeof(namebuf), "racesstyle%d", index);
      if( i == 0 ) {
	races_style_toggles[index] =
	  XtVaCreateManagedWidget(namebuf,
				  toggleWidgetClass,
				  races_style_form,
				  XtNradioGroup,
				  races_style_toggles[index-1],
				  XtNfromHoriz,
				  races_style_toggles[index-1],
				  XtNlabel, maxracename,
				  NULL);
      } else {
	races_style_toggles[index] =
	  XtVaCreateManagedWidget(namebuf,
				  toggleWidgetClass,
				  races_style_form,
				  XtNradioGroup,
				  races_style_toggles[index-1],
				  XtNfromVert,
				  races_style_toggles[index-per_row],
				  XtNfromHoriz,
				  races_style_toggles[index-1],
				  XtNlabel, maxracename,
				  NULL);
      }
    }
  }

  races_action_form = XtVaCreateManagedWidget("racesactionform",
					      formWidgetClass,
					      races_form,
					      XtNfromVert, races_style_form,
					      NULL);

  races_ok_command =
    I_L(XtVaCreateManagedWidget("racesokcommand",
				commandWidgetClass,
				races_action_form,
				NULL));

  races_disconnect_command =
    I_L(XtVaCreateManagedWidget("racesdisconnectcommand",
				commandWidgetClass,
				races_action_form,
				XtNfromHoriz, races_ok_command,
				NULL));

  races_quit_command =
    I_L(XtVaCreateManagedWidget("racesquitcommand",
				commandWidgetClass,
				races_action_form,
				XtNfromHoriz, races_disconnect_command,
				NULL));

  XtAddCallback(races_disconnect_command, XtNcallback,
		races_disconnect_command_callback, NULL);
  XtAddCallback(races_quit_command, XtNcallback,
		races_quit_command_callback, NULL);


  for(i=0; i<game.playable_nation_count; i++) {
    XtAddCallback(races_toggles[i], XtNcallback, 
		  races_toggles_callback, (XtPointer)i);
  }


  XtAddCallback(races_ok_command, XtNcallback,
		races_ok_command_callback, NULL);


  XtSetKeyboardFocus(races_form, races_leader);

  XtRealizeWidget(races_dialog_shell);

  for(i=0; i<game.playable_nation_count; i++) {
    races_toggles_to_nations[i] = i;
  }
  qsort(races_toggles_to_nations, i, sizeof(int), races_indirect_compare);
  for(i=0; i<game.playable_nation_count; i++) {
    XtVaSetValues (races_toggles[i],
		   XtNlabel, (XtArgVal)get_nation_name(races_toggles_to_nations[i]),
		   NULL);
  }

  for(i=0; i<b_s_num; i++) {
    XtVaSetValues(races_style_toggles[i], XtNlabel,
		  (XtArgVal)city_styles[city_style_idx[i]].name, NULL);
  }

  x_simulate_button_click(races_toggles[myrand(game.playable_nation_count)]);
}

/****************************************************************
...
*****************************************************************/
void racesdlg_key_ok(Widget w)
{
  Widget ok = XtNameToWidget(XtParent(XtParent(w)), "*racesokcommand");
  if (ok)
    x_simulate_button_click(ok);
}

/**************************************************************************
...
**************************************************************************/
void races_toggles_set_sensitive(int bits1, int bits2)
{
  int i, race, selected;
  Boolean sens;

  for(i=0; i<game.playable_nation_count; i++) {
    race = races_toggles_to_nations[i];
    sens = !( ( ((race<32)?bits1:bits2) >> ((race<32)?race:race-32) )&1 );
    XtSetSensitive(races_toggles[i], sens);
  }

  if((selected=races_buttons_get_current())==-1)
    return;
  race = races_toggles_to_nations[selected];

  if( (bits1 & (1<<race)) || 
      (race>32 && (bits2 & (1<<(race-32))) ) )
    XawToggleUnsetCurrent(races_toggles[0]);
}

/**************************************************************************
...
**************************************************************************/
void races_toggles_callback(Widget w, XtPointer client_data, 
			    XtPointer call_data)
{
  int index, race;
  int j;
  int leader_count;
  char **leaders;
  Widget entry;

  index = (int)client_data;
  race = races_toggles_to_nations[index];

  leaders = get_nation_leader_names(race, &leader_count);

  if(races_leader_pick_popupmenu)
    XtDestroyWidget(races_leader_pick_popupmenu);

  races_leader_pick_popupmenu =
    XtVaCreatePopupShell("menu",
			 simpleMenuWidgetClass,
			 races_leader_pick_menubutton,
			 NULL);

  for(j=0; j<leader_count; j++) {
    entry =
      XtVaCreateManagedWidget(leaders[j],
			      smeBSBObjectClass,
			      races_leader_pick_popupmenu,
			      NULL);
    XtAddCallback(entry,
		  XtNcallback,
		  races_leader_pick_callback,
		  (XtPointer)((MAX_NUM_NATIONS * race) + j));
  }

  races_leader_set_values(race, myrand(leader_count));

  x_simulate_button_click
  (
   races_style_toggles[city_style_ridx[get_nation_city_style(race)]]
  );
}

/**************************************************************************
...
**************************************************************************/
void races_leader_pick_callback(Widget w, XtPointer client_data,
				XtPointer call_data)
{
  int race, lead;

  race = ((int)client_data) / MAX_NUM_NATIONS;
  lead = ((int)client_data) - (MAX_NUM_NATIONS * race);

  races_leader_set_values(race, lead);
}

/**************************************************************************
...
**************************************************************************/
void races_leader_set_values(int race, int lead)
{
  int leader_count;
  char **leaders;
  int sex;

  leaders = get_nation_leader_names(race, &leader_count);
  XtVaSetValues(races_leader, XtNstring, leaders[lead], NULL);
  XawTextSetInsertionPoint(races_leader, strlen(leaders[lead]));

  sex = !(get_nation_leader_sex(race, leaders[lead]));
  races_sex_buttons_set_current(sex);
}

/**************************************************************************
...
**************************************************************************/
int races_buttons_get_current(void)
{
  int i;
  XtPointer dp, yadp;

  if((game.playable_nation_count)==1)
    return 0;

  if(!(dp=XawToggleGetCurrent(races_toggles[0])))
    return -1;

  for(i=0; i<game.playable_nation_count; i++) {
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
int races_style_buttons_get_current(void)
{
  int i;
  XtPointer dp, yadp;

  if(b_s_num==1)
    return 0;

  if(!(dp=XawToggleGetCurrent(races_style_toggles[0])))
    return -1;

  for(i=0; i<b_s_num; i++) {
    XtVaGetValues(races_style_toggles[i], XtNradioData, &yadp, NULL);
    if(dp==yadp)
      return i;
  }

  return -1;
}

/**************************************************************************
...
**************************************************************************/
void races_sex_buttons_set_current(int i)
{
  XtPointer dp;

  XtVaGetValues(races_sex_toggles[i], XtNradioData, &dp, NULL);

  XawToggleSetCurrent(races_sex_toggles[0], dp);
}

/**************************************************************************
...
**************************************************************************/
int races_indirect_compare(const void *first, const void *second)
{
  int first_index;
  int second_index;
  char *first_string;
  char *second_string;

  first_index = *((const int *)first);
  second_index = *((const int *)second);

  first_string = get_nation_name(first_index);
  second_string = get_nation_name(second_index);

  return mystrcasecmp(first_string, second_string);
}

/**************************************************************************
...
**************************************************************************/
void races_ok_command_callback(Widget w, XtPointer client_data, 
			       XtPointer call_data)
{
  int selected_index, selected_sex, selected_style;
  XtPointer dp;

  struct packet_alloc_nation packet;

  if((selected_index=races_buttons_get_current())==-1) {
    append_output_window(_("You must select a nation."));
    return;
  }

  if((selected_sex=races_sex_buttons_get_current())==-1) {
    append_output_window(_("You must select your sex."));
    return;
  }

  if((selected_style=races_style_buttons_get_current())==-1) {
    append_output_window(_("You must select your city style."));
    return;
  }

  XtVaGetValues(races_leader, XtNstring, &dp, NULL);

  /* perform a minimum of sanity test on the name */
  packet.nation_no=races_toggles_to_nations[selected_index];
  packet.city_style = get_nation_city_style(packet.nation_no);
  packet.is_male = selected_sex? 0: 1;     /* first button is male */
  packet.city_style = city_style_idx[selected_style];
  sz_strlcpy(packet.name, (char*)dp);
  
  if(!get_sane_name(packet.name)) {
    append_output_window(_("You must type a legal name."));
    return;
  }

  packet.name[0]=toupper(packet.name[0]);

  send_packet_alloc_nation(&aconnection, &packet);
}

/**************************************************************************
...
**************************************************************************/
void races_disconnect_command_callback(Widget w, XtPointer client_data, 
				       XtPointer call_data)
{
  popdown_races_dialog();
  disconnect_from_server();
}

/**************************************************************************
...
**************************************************************************/
void races_quit_command_callback(Widget w, XtPointer client_data, 
				 XtPointer call_data)
{
  exit(0);
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

  if (get_client_state()!=CLIENT_GAME_RUNNING_STATE)
    return;
  
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
