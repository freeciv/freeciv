/***********************************************************************
 Freeciv - Copyright (C) 1996-2005 - Freeciv Development Team
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
#include <fc_config.h>
#endif

#include <stdio.h>
#include <stdlib.h>

#include <X11/Intrinsic.h>
#include <X11/StringDefs.h>
#include <X11/Xaw/Command.h>
#include <X11/Xaw/Form.h>
#include <X11/Xaw/Label.h>
#include <X11/Xaw/List.h>
#include <X11/Xaw/SimpleMenu.h>

/* utility */
#include "astring.h"
#include "fcintl.h"
#include "log.h"

/* common */
#include "actions.h"
#include "game.h"
#include "improvement.h"
#include "movement.h"
#include "research.h"
#include "tech.h"
#include "traderoutes.h"
#include "unitlist.h"

/* client */
#include "client_main.h"
#include "control.h"

/* client/gui-xaw */
#include "citydlg.h"
#include "dialogs.h"
#include "gui_main.h"
#include "gui_stuff.h"

/******************************************************************/
static Widget spy_tech_shell;
static Widget spy_advances_list, spy_advances_list_label;
static Widget spy_steal_command;

static int spy_tech_shell_is_modal;
static int advance_type[A_LAST+1];
static int steal_advance = 0;

/******************************************************************/
static Widget spy_sabotage_shell;
static Widget spy_improvements_list, spy_improvements_list_label;
static Widget spy_sabotage_command;

static int spy_sabotage_shell_is_modal;
static int improvement_type[B_LAST+1];
static int sabotage_improvement = 0;

static Widget diplomat_dialog;
int diplomat_id;
int diplomat_target_id[ATK_COUNT];

/**************************************************************************
  User selected enter market place from caravan dialog
**************************************************************************/
static void caravan_marketplace_callback(Widget w, XtPointer client_data,
                                             XtPointer call_data)
{
  dsend_packet_unit_establish_trade(&client.conn,
                                    diplomat_id,
                                    diplomat_target_id[ATK_CITY],
                                    FALSE);

  destroy_message_dialog(w);
  diplomat_dialog = NULL;

  choose_action_queue_next();
}

/****************************************************************
  User selected traderoute from caravan dialog
*****************************************************************/
static void caravan_establish_trade_callback(Widget w, XtPointer client_data,
                                             XtPointer call_data)
{
  dsend_packet_unit_establish_trade(&client.conn,
                                    diplomat_id,
                                    diplomat_target_id[ATK_CITY],
                                    TRUE);

  destroy_message_dialog(w);
  diplomat_dialog = NULL;

  choose_action_queue_next();
}

/****************************************************************
...
*****************************************************************/
static void caravan_help_build_wonder_callback(Widget w,
					       XtPointer client_data,
					       XtPointer call_data)
{
  dsend_packet_unit_help_build_wonder(&client.conn,
                                      diplomat_id,
                                      diplomat_target_id[ATK_CITY]);

  destroy_message_dialog(w);
  diplomat_dialog = NULL;

  choose_action_queue_next();
}

/**************************************************************************
  Updates caravan dialog
**************************************************************************/
void caravan_dialog_update(void)
{
/* PORT ME */
}

/****************************************************************
...
*****************************************************************/
static void diplomat_bribe_yes_callback(Widget w, XtPointer client_data, 
					XtPointer call_data)
{
  request_do_action(ACTION_SPY_BRIBE_UNIT, diplomat_id,
                    diplomat_target_id[ATK_UNIT], 0);

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

/**************************************************************************
  Asks the server how much the bribe is
**************************************************************************/
static void diplomat_bribe_callback(Widget w, XtPointer client_data, 
				    XtPointer call_data)
{
  destroy_message_dialog(w);
  diplomat_dialog = NULL;

  if (NULL != game_unit_by_number(diplomat_id)
      && NULL != game_unit_by_number(diplomat_target_id[ATK_UNIT])) {
    request_action_details(ACTION_SPY_BRIBE_UNIT, diplomat_id,
                           diplomat_target_id[ATK_UNIT]);
  }
}

/**************************************************************************
  Creates and popups the bribe dialog
**************************************************************************/
void popup_bribe_dialog(struct unit *actor, struct unit *punit, int cost)
{
  char tbuf[128], buf[128];

  fc_snprintf(tbuf, ARRAY_SIZE(tbuf), PL_("Treasury contains %d gold.",
                                          "Treasury contains %d gold.",
                                          client_player()->economic.gold),
              client_player()->economic.gold);

  if (cost <= client_player()->economic.gold) {
    fc_snprintf(buf, sizeof(buf),
                /* TRANS: %s is pre-pluralised "Treasury contains %d gold." */
                PL_("Bribe unit for %d gold?\n%s",
                    "Bribe unit for %d gold?\n%s", cost), cost, tbuf);
    popup_message_dialog(toplevel, "diplomatbribedialog", buf,
			 diplomat_bribe_yes_callback, 0, 0,
			 diplomat_bribe_no_callback, 0, 0,
			 NULL);
  } else {
    fc_snprintf(buf, sizeof(buf),
                /* TRANS: %s is pre-pluralised "Treasury contains %d gold." */
                PL_("Bribing the unit costs %d gold.\n%s",
                    "Bribing the unit costs %d gold.\n%s", cost), cost, tbuf);
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
  diplomat_dialog = NULL;

  if (NULL != game_unit_by_number(diplomat_id)
      && NULL != game_city_by_number(diplomat_target_id[ATK_CITY])) {
    request_do_action(ACTION_SPY_SABOTAGE_CITY, diplomat_id,
                      diplomat_target_id[ATK_CITY], B_LAST + 1);
  }

  choose_action_queue_next();
}

/****************************************************************
...
*****************************************************************/
static void diplomat_embassy_callback(Widget w, XtPointer client_data, 
				      XtPointer call_data)
{
  destroy_message_dialog(w);
  diplomat_dialog = NULL;

  if (NULL != game_unit_by_number(diplomat_id)
      && NULL != game_city_by_number(diplomat_target_id[ATK_CITY])) {
    request_do_action(ACTION_ESTABLISH_EMBASSY, diplomat_id,
                      diplomat_target_id[ATK_CITY], 0);
  }

  choose_action_queue_next();
}

/****************************************************************
...
*****************************************************************/
static void diplomat_investigate_callback(Widget w, XtPointer client_data,
                                          XtPointer call_data)
{
  destroy_message_dialog(w);
  diplomat_dialog = NULL;

  if (NULL != game_city_by_number(diplomat_target_id[ATK_CITY])
      && NULL != game_unit_by_number(diplomat_id)) {
    request_do_action(ACTION_SPY_INVESTIGATE_CITY, diplomat_id,
                      diplomat_target_id[ATK_CITY], 0);
  }

  choose_action_queue_next();
}

/****************************************************************
...
*****************************************************************/
static void spy_sabotage_unit_callback(Widget w, XtPointer client_data, 
				       XtPointer call_data)
{
  request_do_action(ACTION_SPY_SABOTAGE_UNIT, diplomat_id,
                    diplomat_target_id[ATK_UNIT], 0);

  destroy_message_dialog(w);
  diplomat_dialog = NULL;
}

/****************************************************************
...
*****************************************************************/
static void spy_poison_callback(Widget w, XtPointer client_data, 
				XtPointer call_data)
{
  destroy_message_dialog(w);
  diplomat_dialog = NULL;

  if (NULL != game_unit_by_number(diplomat_id)
      && NULL != game_city_by_number(diplomat_target_id[ATK_CITY])) {
    request_do_action(ACTION_SPY_POISON, diplomat_id,
                      diplomat_target_id[ATK_CITY], 0);
  }

  choose_action_queue_next();
}

/********************************************************************
  The player selected "Steal Gold"
********************************************************************/
static void spy_steal_gold_callback(Widget w, XtPointer client_data,
                                    XtPointer call_data)
{
  destroy_message_dialog(w);
  diplomat_dialog = NULL;

  if (NULL != game_unit_by_number(diplomat_id)
      && NULL != game_city_by_number(diplomat_target_id[ATK_CITY])) {
    request_do_action(ACTION_SPY_STEAL_GOLD, diplomat_id,
                      diplomat_target_id[ATK_CITY], 0);
  }

  choose_action_queue_next();
}

/****************************************************************
...
*****************************************************************/
static void diplomat_steal_callback(Widget w, XtPointer client_data, 
				    XtPointer call_data)
{
  destroy_message_dialog(w);
  diplomat_dialog = NULL;

  if (NULL != game_unit_by_number(diplomat_id)
      && NULL != game_city_by_number(diplomat_target_id[ATK_CITY])) {
    request_do_action(ACTION_SPY_STEAL_TECH, diplomat_id,
                      diplomat_target_id[ATK_CITY], A_UNSET);
  }

  choose_action_queue_next();
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

  choose_action_queue_next();
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

  choose_action_queue_next();
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
    log_error("Bug in spy steal tech code");
    choose_action_queue_next();
    return;
  }

  if (NULL != game_unit_by_number(diplomat_id)
      && NULL != game_city_by_number(diplomat_target_id[ATK_CITY])) {
    request_do_action(ACTION_SPY_TARGETED_STEAL_TECH, diplomat_id,
                      diplomat_target_id[ATK_CITY], steal_advance);
  }

  choose_action_queue_next();
}

/****************************************************************
...
*****************************************************************/
static void spy_sabotage_callback(Widget w, XtPointer client_data, 
				  XtPointer call_data)
{  
  XtDestroyWidget(spy_sabotage_shell);
  spy_sabotage_shell = 0l;
  
  if (sabotage_improvement < -1) {
    log_error("Bug in spy sabotage code");
    choose_action_queue_next();
    return;
  }

  if (NULL != game_unit_by_number(diplomat_id)
      && NULL != game_city_by_number(diplomat_target_id[ATK_CITY])) {
    request_do_action(ACTION_SPY_TARGETED_SABOTAGE_CITY, diplomat_id,
                      diplomat_target_id[ATK_CITY],
                      sabotage_improvement + 1);
  }

  choose_action_queue_next();
}

/****************************************************************
...
*****************************************************************/
static int create_advances_list(struct player *pplayer,
				struct player *pvictim, bool make_modal)
{  
  Widget spy_tech_form;
  Widget close_command;
  Dimension width1, width2; 
  int j;

  static const char *advances_can_steal[A_LAST+1]; 

  spy_tech_shell =
    I_T(XtVaCreatePopupShell("spystealtechpopup", 
			     (make_modal ? transientShellWidgetClass :
			      topLevelShellWidgetClass),
			     toplevel, NULL));  
  
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
    const struct research *presearch = research_get(pplayer);
    const struct research *vresearch = research_get(pvictim);

    advance_index_iterate(A_FIRST, i) {
      if (research_invention_state(vresearch, i) == TECH_KNOWN
          && (research_invention_state(presearch, i) == TECH_UNKNOWN
              || research_invention_state(presearch, i)
                 == TECH_PREREQS_KNOWN)) {
        advances_can_steal[j] = advance_name_translation(advance_by_number(i));
        advance_type[j++] = i;
      }
    }
    {
      static struct astring str = ASTRING_INIT;
      /* TRANS: %s is a unit name, e.g., Spy */
      astr_set(&str, _("At %s's Discretion"),
               unit_name_translation(game_unit_by_number(diplomat_id)));
      advances_can_steal[j] = astr_str(&str);
      advance_type[j++] = A_UNSET;
    }
  } advance_index_iterate_end;

  if(j == 0) j++;
  advances_can_steal[j] = NULL; 
  
  XtSetSensitive(spy_steal_command, FALSE);
  
  XawListChange(spy_advances_list, (char **)advances_can_steal, 0, 0, 1);
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
				    struct city *pcity, bool make_modal)
{  
  Widget spy_sabotage_form;
  Widget close_command;
  Dimension width1, width2; 
  int j;

  static const char *improvements_can_sabotage[B_LAST+1]; 
  
  spy_sabotage_shell =
    I_T(XtVaCreatePopupShell("spysabotageimprovementspopup", 
			     (make_modal ? transientShellWidgetClass :
			      topLevelShellWidgetClass),
			     toplevel, NULL));  
  
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

  city_built_iterate(pcity, pimprove) {
    if (pimprove->sabotage > 0) {
      improvements_can_sabotage[j] = city_improvement_name_translation(pcity, pimprove);
      improvement_type[j++] = improvement_number(pimprove);
    }  
  } city_built_iterate_end;

  if(j > 1) {
    static struct astring str = ASTRING_INIT;
    /* TRANS: %s is a unit name, e.g., Spy */
    astr_set(&str, _("At %s's Discretion"),
             unit_name_translation(game_unit_by_number(diplomat_id)));
    improvements_can_sabotage[j] = astr_str(&str);
    improvement_type[j++] = B_LAST;
  } else {
    improvement_type[0] = B_LAST; /* fake "discretion", since must be production */
  }

  improvements_can_sabotage[j] = NULL;
  
  XtSetSensitive(spy_sabotage_command, FALSE);
  
  XawListChange(spy_improvements_list, (String *) improvements_can_sabotage,
		0, 0, 1);
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
  struct city *pvcity = game_city_by_number(diplomat_target_id[ATK_CITY]);
  struct player *pvictim = NULL;

  if(pvcity)
    pvictim = city_owner(pvcity);

/* it is concievable that pvcity will not be found, because something
has happened to the city during latency.  Therefore we must initialize
pvictim to NULL and account for !pvictim in create_advances_list. -- Syela */
  
  destroy_message_dialog(w);
  diplomat_dialog = NULL;

  if(!spy_tech_shell){
    Position x, y;
    Dimension width, height;
    spy_tech_shell_is_modal=1;

    create_advances_list(client.conn.playing, pvictim, spy_tech_shell_is_modal);
    
    XtVaGetValues(toplevel, XtNwidth, &width, XtNheight, &height, NULL);
    
    XtTranslateCoords(toplevel, (Position) width/10, (Position) height/10,
		      &x, &y);
    XtVaSetValues(spy_tech_shell, XtNx, x, XtNy, y, NULL);
    
    XtPopup(spy_tech_shell, XtGrabNone);
  }
}

/**************************************************************************
  Requests up-to-date list of improvements, the return of
  which will trigger the popup_sabotage_dialog() function.
**************************************************************************/
static void spy_request_sabotage_list(Widget w, XtPointer client_data,
				      XtPointer call_data)
{
  destroy_message_dialog(w);
  diplomat_dialog = NULL;

  if (NULL != game_unit_by_number(diplomat_id)
      && NULL != game_city_by_number(diplomat_target_id[ATK_CITY])) {
    request_action_details(ACTION_SPY_TARGETED_SABOTAGE_CITY, diplomat_id,
                           diplomat_target_id[ATK_CITY]);
  }
}

/**************************************************************************
  Pops-up the Spy sabotage dialog, upon return of list of
  available improvements requested by the above function.
**************************************************************************/
void popup_sabotage_dialog(struct unit *actor, struct city *pcity)
{  
  if(!spy_sabotage_shell){
    Position x, y;
    Dimension width, height;
    spy_sabotage_shell_is_modal=1;

    create_improvements_list(client.conn.playing, pcity, spy_sabotage_shell_is_modal);
    
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
  request_do_action(ACTION_SPY_INCITE_CITY, diplomat_id,
                    diplomat_target_id[ATK_CITY], 0);

  destroy_message_dialog(w);

  choose_action_queue_next();
}

/****************************************************************
...
*****************************************************************/
static void diplomat_incite_no_callback(Widget w, XtPointer client_data, 
					XtPointer call_data)
{
  destroy_message_dialog(w);

  choose_action_queue_next();
}


/**************************************************************************
  Asks the server how much the revolt is going to cost us
**************************************************************************/
static void diplomat_incite_callback(Widget w, XtPointer client_data, 
				     XtPointer call_data)
{
  destroy_message_dialog(w);
  diplomat_dialog = NULL;

  if (NULL != game_unit_by_number(diplomat_id)
      && NULL != game_city_by_number(diplomat_target_id[ATK_CITY])) {
    request_action_details(ACTION_SPY_INCITE_CITY, diplomat_id,
                           diplomat_target_id[ATK_CITY]);
  }
}

/**************************************************************************
  Popup the yes/no dialog for inciting, since we know the cost now
**************************************************************************/
void popup_incite_dialog(struct unit *actor, struct city *pcity, int cost)
{
  char tbuf[128], buf[128];

  fc_snprintf(tbuf, ARRAY_SIZE(tbuf), PL_("Treasury contains %d gold.",
                                          "Treasury contains %d gold.",
                                          client_player()->economic.gold),
              client_player()->economic.gold);

  if (INCITE_IMPOSSIBLE_COST == cost) {
    fc_snprintf(buf, sizeof(buf), _("You can't incite a revolt in %s."),
		city_name(pcity));
    popup_message_dialog(toplevel, "diplomatnogolddialog", buf,
			 diplomat_incite_no_callback, 0, 0, NULL);
  } else if (cost <= client_player()->economic.gold) {
    fc_snprintf(buf, sizeof(buf),
                /* TRANS: %s is pre-pluralised "Treasury contains %d gold." */
                PL_("Incite a revolt for %d gold?\n%s",
                    "Incite a revolt for %d gold?\n%s", cost), cost, tbuf);
    diplomat_target_id[ATK_CITY] = pcity->id;
    popup_message_dialog(toplevel, "diplomatrevoltdialog", buf,
                         diplomat_incite_yes_callback, 0, 0,
                         diplomat_incite_no_callback, 0, 0,
                         NULL);
  } else {
    fc_snprintf(buf, sizeof(buf),
                /* TRANS: %s is pre-pluralised "Treasury contains %d gold." */
                PL_("Inciting a revolt costs %d gold.\n%s",
                    "Inciting a revolt costs %d gold.\n%s", cost), cost, tbuf);
    popup_message_dialog(toplevel, "diplomatnogolddialog", buf,
                         diplomat_incite_no_callback, 0, 0,
                         NULL);
  }
}


/**************************************************************************
  Callback from diplomat/spy dialog for "keep moving".
  (This should only occur when entering a tile that has an allied city or
  an allied unit.)
**************************************************************************/
static void diplomat_keep_moving_callback(Widget w,
                                          XtPointer client_data,
                                          XtPointer call_data)
{
  struct unit *punit;
  struct tile *ptile;
  
  destroy_message_dialog(w);
  diplomat_dialog = NULL;

  if ((punit = game_unit_by_number(diplomat_id))
      && (ptile = (struct tile *)client_data)
      && !same_pos(unit_tile(punit), ptile)) {
    request_do_action(ACTION_MOVE, diplomat_id,
                      ptile->index, 0);
  }
  choose_action_queue_next();
}

/****************************************************************
...
*****************************************************************/
static void diplomat_cancel_callback(Widget w, XtPointer a, XtPointer b)
{
  destroy_message_dialog(w);
  diplomat_dialog = NULL;

  choose_action_queue_next();
}

/*************************************************************************
  Control the display of an action
**************************************************************************/
static void action_entry(Widget w, int action_id,
                         const action_probability *action_probabilities)
{
  Arg arglist[1];

  if (!action_prob_possible(action_probabilities[action_id])) {
    XtSetSensitive(w, FALSE);
  }

  XtSetArg(arglist[0], "label",
           action_prepare_ui_name(action_id, "",
                                  action_probabilities[action_id]));
  XtSetValues(w, arglist, XtNumber(arglist));
}

/**************************************************************************
  Popup a dialog that allows the player to select what action a unit
  should take.
**************************************************************************/
void popup_action_selection(struct unit *actor_unit,
                            struct city *target_city,
                            struct unit *target_unit,
                            struct tile *target_tile,
                            const action_probability *act_probs)
{
  struct astring text = ASTRING_INIT;

  struct city *actor_homecity = game_city_by_number(actor_unit->homecity);

  bool can_marketplace = target_city
      && unit_has_type_flag(actor_unit, UTYF_TRADE_ROUTE)
      && can_cities_trade(actor_homecity, target_city);
  bool can_traderoute = can_marketplace
      && can_establish_trade_route(actor_homecity, target_city);
  bool can_wonder = target_city
      && unit_can_help_build_wonder(actor_unit, target_city);

  diplomat_id = actor_unit->id;

  if (target_unit) {
    diplomat_target_id[ATK_UNIT] = target_unit->id;
  } else {
    diplomat_target_id[ATK_UNIT] = IDENTITY_NUMBER_ZERO;
  }

  if (target_city) {
    diplomat_target_id[ATK_CITY] = target_city->id;
  } else {
    diplomat_target_id[ATK_CITY] = IDENTITY_NUMBER_ZERO;
  }

  if (target_city && actor_homecity) {
    astr_set(&text,
             _("Your %s from %s reaches the city of %s.\nWhat now?"),
             unit_name_translation(actor_unit),
             city_name(actor_homecity),
             city_name(target_city));
  } else if (target_city) {
    astr_set(&text,
             _("Your %s has arrived at %s.\nWhat is your command?"),
             unit_name_translation(actor_unit),
             city_name(target_city));
  } else {
    astr_set(&text,
             /* TRANS: %s is a unit name, e.g., Diplomat, Spy */
             _("Your %s is waiting for your command."),
             unit_name_translation(actor_unit));
  }

  diplomat_dialog =
      popup_message_dialog(toplevel, "diplomatdialog", astr_str(&text),
                           diplomat_embassy_callback, 0, 1,
                           diplomat_investigate_callback, 0, 1,
                           spy_poison_callback, 0, 1,
                           diplomat_sabotage_callback, 0, 1,
                           spy_request_sabotage_list, 0, 1,
                           diplomat_steal_callback, 0, 1,
                           spy_steal_popup, 0, 1,
                           diplomat_incite_callback, 0, 1,
                           caravan_marketplace_callback, 0, 0,
                           caravan_establish_trade_callback, 0, 0,
                           caravan_help_build_wonder_callback, 0, 0,
                           diplomat_bribe_callback, 0, 0,
                           spy_sabotage_unit_callback, 0, 0,
                           spy_steal_gold_callback, 0, 0,
                           diplomat_keep_moving_callback, target_tile, 1,
                           diplomat_cancel_callback, 0, 0,
                           NULL);

  action_entry(XtNameToWidget(diplomat_dialog, "*button0"),
               ACTION_ESTABLISH_EMBASSY,
               act_probs);

  action_entry(XtNameToWidget(diplomat_dialog, "*button1"),
               ACTION_SPY_INVESTIGATE_CITY,
               act_probs);

  action_entry(XtNameToWidget(diplomat_dialog, "*button2"),
               ACTION_SPY_POISON,
               act_probs);

  action_entry(XtNameToWidget(diplomat_dialog, "*button3"),
               ACTION_SPY_SABOTAGE_CITY,
               act_probs);

  action_entry(XtNameToWidget(diplomat_dialog, "*button4"),
               ACTION_SPY_TARGETED_SABOTAGE_CITY,
               act_probs);

  action_entry(XtNameToWidget(diplomat_dialog, "*button5"),
               ACTION_SPY_STEAL_TECH,
               act_probs);

  action_entry(XtNameToWidget(diplomat_dialog, "*button6"),
               ACTION_SPY_TARGETED_STEAL_TECH,
               act_probs);

  action_entry(XtNameToWidget(diplomat_dialog, "*button7"),
               ACTION_SPY_INCITE_CITY,
               act_probs);

  if (!can_marketplace) {
    XtSetSensitive(XtNameToWidget(diplomat_dialog, "*button8"), FALSE);
  }

  if (!can_traderoute) {
    XtSetSensitive(XtNameToWidget(diplomat_dialog, "*button9"), FALSE);
  }

  if (!can_wonder) {
    XtSetSensitive(XtNameToWidget(diplomat_dialog, "*button10"), FALSE);
  }

  action_entry(XtNameToWidget(diplomat_dialog, "*button11"),
               ACTION_SPY_BRIBE_UNIT,
               act_probs);

  action_entry(XtNameToWidget(diplomat_dialog, "*button12"),
               ACTION_SPY_SABOTAGE_UNIT,
               act_probs);

  action_entry(XtNameToWidget(diplomat_dialog, "*button13"),
               ACTION_SPY_STEAL_GOLD,
               act_probs);

  if (!unit_can_move_to_tile(actor_unit, target_tile, FALSE)) {
    XtSetSensitive(XtNameToWidget(diplomat_dialog, "*button14"), FALSE);
  }

  astr_free(&text);
}

/**************************************************************************
  Returns the id of the actor unit currently handled in action selection
  dialog when the action selection dialog is open.
  Returns IDENTITY_NUMBER_ZERO if no action selection dialog is open.
**************************************************************************/
int action_selection_actor_unit(void)
{
  if (diplomat_dialog == NULL) {
    return IDENTITY_NUMBER_ZERO;
  }

  return diplomat_id;
}

/**************************************************************************
  Returns id of the target city of the actions currently handled in action
  selection dialog when the action selection dialog is open and it has a
  city target. Returns IDENTITY_NUMBER_ZERO if no action selection dialog
  is open or no city target is present in the action selection dialog.
**************************************************************************/
int action_selection_target_city(void)
{
  if (diplomat_dialog == NULL) {
    return IDENTITY_NUMBER_ZERO;
  }

  return diplomat_target_id[ATK_CITY];
}

/**************************************************************************
  Returns id of the target unit of the actions currently handled in action
  selection dialog when the action selection dialog is open and it has a
  unit target. Returns IDENTITY_NUMBER_ZERO if no action selection dialog
  is open or no unit target is present in the action selection dialog.
**************************************************************************/
int action_selection_target_unit(void)
{
  if (diplomat_dialog == NULL) {
    return IDENTITY_NUMBER_ZERO;
  }

  return diplomat_target_id[ATK_UNIT];
}

/**************************************************************************
  Updates the action selection dialog with new information.
**************************************************************************/
void action_selection_refresh(struct unit *actor_unit,
                              struct city *target_city,
                              struct unit *target_unit,
                              struct tile *target_tile,
                              const action_probability *act_prob)
{
  /* TODO: port me. */
}

/**************************************************************************
  Closes the diplomat dialog
**************************************************************************/
void close_diplomat_dialog(void)
{
  if (diplomat_dialog != NULL) {
    XtDestroyWidget(diplomat_dialog);
  }
}
