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

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include <X11/Intrinsic.h>
#include <X11/StringDefs.h>
#include <X11/Xaw/Form.h>
#include <X11/Xaw/SimpleMenu.h> 
#include <X11/Xaw/Command.h>
#include <X11/Xaw/List.h>
#include <X11/Xaw/Viewport.h>
#include <X11/Xaw/AsciiText.h>
#include <X11/Xaw/Tree.h>

#include "city.h"
#include "fcintl.h"
#include "game.h"
#include "genlist.h"
#include "government.h"
#include "mem.h"
#include "shared.h"
#include "tech.h"
#include "unit.h"
#include "map.h"
#include "support.h"
#include "version.h"

#include "climisc.h"
#include "dialogs.h"
#include "graphics.h"
#include "gui_main.h"
#include "gui_stuff.h"
#include "helpdata.h"
#include "tilespec.h"

#include "helpdlg.h"

static Widget help_dialog_shell;
static Widget help_form;
static Widget help_viewport;
static Widget help_list;
static Widget help_text;
static Widget help_close_command;

static Widget help_right_form;
static Widget help_title;

static Widget help_improvement_cost, help_improvement_cost_data;
static Widget help_improvement_upkeep, help_improvement_upkeep_data;
static Widget help_improvement_req, help_improvement_req_data;
static Widget help_wonder_obsolete, help_wonder_obsolete_data;

static Widget help_tech_tree;
static Widget help_tree_viewport;

static Widget help_unit_attack, help_unit_attack_data;
static Widget help_unit_def, help_unit_def_data;
static Widget help_unit_move, help_unit_move_data;
static Widget help_unit_hp, help_unit_hp_data;
static Widget help_unit_fp, help_unit_fp_data;
static Widget help_unit_cost, help_unit_cost_data;
static Widget help_unit_visrange, help_unit_visrange_data;
static Widget help_unit_upkeep, help_unit_upkeep_data;
static Widget help_unit_tile;

static Widget help_terrain_movement_defense, help_terrain_movement_defense_data;
static Widget help_terrain_food_shield_trade, help_terrain_food_shield_trade_data;
static Widget help_terrain_special_1, help_terrain_special_1_data;
static Widget help_terrain_special_2, help_terrain_special_2_data;
static Widget help_terrain_road_result_time, help_terrain_road_result_time_data;
static Widget help_terrain_irrigation_result_time, help_terrain_irrigation_result_time_data;
static Widget help_terrain_mining_result_time, help_terrain_mining_result_time_data;
static Widget help_terrain_transform_result_time, help_terrain_transform_result_time_data;

/* HACK: we use a static string for convenience. */
static char long_buffer[64000];

static void create_help_page(enum help_page_type type);

static void help_close_command_callback(Widget w, XtPointer client_data, 
				 XtPointer call_data);
static void help_list_callback(Widget w, XtPointer client_data, 
			         XtPointer call_data);
static void help_tree_node_callback(Widget w, XtPointer client_data, 
			     XtPointer call_data);

static void create_help_dialog(void);
static void help_update_dialog(const struct help_item *pitem);

static void select_help_item(int item);
static void select_help_item_string(const char *item,
				    enum help_page_type htype);

static char *topic_list[1024];

#define TREE_NODE_UNKNOWN_TECH_BG "red"
#define TREE_NODE_KNOWN_TECH_BG "green"
#define TREE_NODE_REACHABLE_TECH_BG "yellow"
#define TREE_NODE_REMOVED_TECH_BG "magenta"


/****************************************************************
...
*****************************************************************/
static void set_title_topic(const struct help_item *pitem)
{
  if (strcmp(pitem->topic, "Freeciv") == 0
      || strcmp(pitem->topic, "About") == 0
      || strcmp(pitem->topic, _("About")) == 0) {
    xaw_set_label(help_title, freeciv_name_version());
  } else {
    xaw_set_label(help_title, pitem->topic);
  }
}


/****************************************************************
...
*****************************************************************/
void popdown_help_dialog(void)
{
  if(help_dialog_shell) {
    XtDestroyWidget(help_dialog_shell);
    help_dialog_shell=0;
  }
}
  
/****************************************************************
...
*****************************************************************/
void popup_help_dialog_typed(const char *item, enum help_page_type htype)
{
  Position x, y;
  Dimension width, height;

  if(!help_dialog_shell) {
    create_help_dialog();
    XtVaGetValues(toplevel, XtNwidth, &width, XtNheight, &height, NULL);
    XtTranslateCoords(toplevel, (Position) width/10, (Position) height/10,
		      &x, &y);
    XtVaSetValues(help_dialog_shell, XtNx, x, XtNy, y, NULL);
  }

  XtPopup(help_dialog_shell, XtGrabNone);

  select_help_item_string(item, htype);
}


/****************************************************************
...
Not sure if this should call _(item) as it does, or whether all
callers of this function should do so themselves... --dwp
*****************************************************************/
void popup_help_dialog_string(const char *item)
{
  popup_help_dialog_typed(_(item), HELP_ANY);
}


/**************************************************************************
...
**************************************************************************/
static void create_help_dialog(void)
{
  int i=0;

  help_items_iterate(pitem) 
    topic_list[i++]=pitem->topic;
  help_items_iterate_end;
  topic_list[i]=0;

  
  help_dialog_shell =
    I_T(XtCreatePopupShell("helpdialog", 
			   topLevelShellWidgetClass,
			   toplevel, NULL, 0));

  help_form = XtVaCreateManagedWidget("helpform", 
				      formWidgetClass, 
				      help_dialog_shell, NULL);
  

  help_viewport = XtVaCreateManagedWidget("helpviewport", 
					  viewportWidgetClass, 
					  help_form, 
					  NULL);
  
  help_list = XtVaCreateManagedWidget("helplist", 
				      listWidgetClass, 
				      help_viewport, 
				      XtNlist, 
				      (XtArgVal)topic_list,
				      NULL);

  
  help_right_form=XtVaCreateManagedWidget("helprightform", 
					  formWidgetClass, 
					  help_form, NULL);
  

  help_title=XtVaCreateManagedWidget("helptitle", 
				     labelWidgetClass, 
				     help_right_form,
				     NULL);
  
    
  help_close_command =
    I_L(XtVaCreateManagedWidget("helpclosecommand", commandWidgetClass,
				help_form, NULL));

  XtAddCallback(help_close_command, XtNcallback, 
		help_close_command_callback, NULL);

  XtAddCallback(help_list, XtNcallback, 
		help_list_callback, NULL);

  XtRealizeWidget(help_dialog_shell);

  XSetWMProtocols(display, XtWindow(help_dialog_shell), 
		  &wm_delete_window, 1);
  XtOverrideTranslations(help_dialog_shell,
	 XtParseTranslationTable("<Message>WM_PROTOCOLS: msg-close-help()"));

  /* create_help_page(HELP_IMPROVEMENT); */
  create_help_page(HELP_TEXT);
}


/**************************************************************************
...
**************************************************************************/
static void create_tech_tree(Widget tree, Widget parent, int tech, int levels)
{
  Widget l;
  int type;
  char *bg="";
  char label[MAX_LEN_NAME+3];
  
  type = (tech==A_LAST) ? TECH_UNKNOWN : get_invention(game.player_ptr, tech);
  switch(type) {
    case TECH_UNKNOWN:
      bg=TREE_NODE_UNKNOWN_TECH_BG;
      break;
    case TECH_KNOWN:
      bg=TREE_NODE_KNOWN_TECH_BG;
      break;
    case TECH_REACHABLE:
      bg=TREE_NODE_REACHABLE_TECH_BG;
      break;
  }
  
  if(tech==A_LAST ||
     (advances[tech].req[0]==A_LAST && advances[tech].req[1]==A_LAST))  {
    sz_strlcpy(label, _("Removed"));
    bg=TREE_NODE_REMOVED_TECH_BG;
    l=XtVaCreateManagedWidget("treenode", commandWidgetClass, 
			      tree,
			      XtNlabel, label,
			      XtNbackground, bg, NULL);
    XtVaSetValues(l, XtVaTypedArg, XtNbackground, 
		  XtRString, bg, strlen(bg)+1, NULL);
     return;
  }
  
  my_snprintf(label, sizeof(label),
	      "%s:%d", advances[tech].name,
	      num_unknown_techs_for_goal(game.player_ptr, tech));

  if(parent) {
    l=XtVaCreateManagedWidget("treenode", 
			      commandWidgetClass, 
			      tree,
			      XtNlabel, label,
			      XtNtreeParent, parent,
			      NULL);
  }
  else {
    l=XtVaCreateManagedWidget("treenode", 
			      commandWidgetClass, 
			      tree,
			      XtNlabel, label,
			      NULL);
  }

  XtAddCallback(l, XtNcallback, help_tree_node_callback,
		INT_TO_XTPOINTER(tech));

  XtVaSetValues(l, XtVaTypedArg, XtNbackground, 
		XtRString, bg, strlen(bg)+1, NULL);

  
  if(--levels>0) {
    if(advances[tech].req[0]!=A_NONE)
      create_tech_tree(tree, l, advances[tech].req[0], levels);
    if(advances[tech].req[1]!=A_NONE)
      create_tech_tree(tree, l, advances[tech].req[1], levels);
  }
  
  
}


/****************************************************************
...
*****************************************************************/
static void create_help_page(enum help_page_type type)
{
  Dimension w, h, w2, h2, ay, ah;

  XtVaGetValues(help_right_form, XtNwidth, &w, NULL);
  XtVaGetValues(help_viewport, XtNheight, &h, NULL);
  XtVaGetValues(help_title, XtNwidth, &w2, XtNheight, &h2, NULL);

  XtDestroyWidget(help_right_form);

  help_right_form =
    XtVaCreateManagedWidget("helprightform", 
			    formWidgetClass, 
			    help_form, 
			    XtNwidth, w,
			    XtNheight, h,
			    NULL);


  help_title =
    XtVaCreateManagedWidget("helptitle", 
			    labelWidgetClass, 
			    help_right_form,
			    XtNwidth, w2,
			    NULL);

  help_tree_viewport = 0;
  
  if(type==HELP_TEXT || type==HELP_ANY) {
    help_text =
      XtVaCreateManagedWidget("helptext", 
			      asciiTextWidgetClass, 
			      help_right_form,
			      XtNeditType, XawtextRead,
			      XtNscrollVertical, XawtextScrollAlways, 
			      XtNwidth, w2,
			      XtNheight, h-h2-15,
			      XtNbottom, XawChainBottom,
			      NULL);
  }
  else if(type==HELP_IMPROVEMENT || type==HELP_WONDER) {
    help_text =
      XtVaCreateManagedWidget("helptext", 
			      asciiTextWidgetClass, 
			      help_right_form,
			      XtNeditType, XawtextRead,
			      XtNscrollVertical, XawtextScrollAlways, 
			      XtNwidth, w2,
			      XtNheight, 70,
			      NULL);


    help_improvement_cost =
      I_L(XtVaCreateManagedWidget("helpimprcost", 
				  labelWidgetClass, 
				  help_right_form,
				  NULL));
    help_improvement_cost_data =
      XtVaCreateManagedWidget("helpimprcostdata", 
			      labelWidgetClass, 
			      help_right_form,
			      NULL);

    help_improvement_req =
      I_L(XtVaCreateManagedWidget("helpimprreq", 
				  labelWidgetClass, 
				  help_right_form,
				  NULL));
    help_improvement_req_data =
      XtVaCreateManagedWidget("helpimprreqdata", 
			      labelWidgetClass, 
			      help_right_form,
			      NULL);
    if(type==HELP_IMPROVEMENT) {
      help_improvement_upkeep =
	I_L(XtVaCreateManagedWidget("helpimprupkeep", 
				    labelWidgetClass, 
				    help_right_form,
				    NULL));
      help_improvement_upkeep_data =
	XtVaCreateManagedWidget("helpimprupkeepdata", 
				labelWidgetClass, 
				help_right_form,
				NULL);
    } else {
      help_wonder_obsolete =
	I_L(XtVaCreateManagedWidget("helpwonderobsolete", 
				    labelWidgetClass, 
				    help_right_form,
				    NULL));
      help_wonder_obsolete_data =
	XtVaCreateManagedWidget("helpwonderobsoletedata", 
				labelWidgetClass, 
				help_right_form,
				NULL);
    }

    XtVaGetValues(help_improvement_req, XtNy, &ay, XtNheight, &ah, NULL);

    help_tree_viewport =
      XtVaCreateManagedWidget("helptreeviewport", 
			      viewportWidgetClass, 
			      help_right_form,
			      XtNwidth, w2,
			      XtNheight, MAX(1,h-(ay+ah)-10),
			      NULL);
    help_tech_tree =
      XtVaCreateManagedWidget("helptree", 
			      treeWidgetClass, 
			      help_tree_viewport,
			      NULL);

    XawTreeForceLayout(help_tech_tree);  
  }
  else if(type==HELP_UNIT) {
    help_text =
      XtVaCreateManagedWidget("helptext", 
			      asciiTextWidgetClass, 
			      help_right_form,
			      XtNeditType, XawtextRead,
			      XtNscrollVertical, XawtextScrollAlways, 
			      XtNwidth, w2,
			      XtNheight, 70,
			      NULL);

    help_unit_cost =
      I_L(XtVaCreateManagedWidget("helpunitcost", 
				  labelWidgetClass, 
				  help_right_form,
				  NULL));
    help_unit_cost_data =
      XtVaCreateManagedWidget("helpunitcostdata", 
			      labelWidgetClass, 
			      help_right_form,
			      NULL);
    help_unit_attack =
      I_L(XtVaCreateManagedWidget("helpunitattack", 
				  labelWidgetClass, 
				  help_right_form,
				  NULL));
    help_unit_attack_data =
      XtVaCreateManagedWidget("helpunitattackdata", 
			      labelWidgetClass, 
			      help_right_form,
			      NULL);
    help_unit_def =
      I_L(XtVaCreateManagedWidget("helpunitdef", 
				  labelWidgetClass, 
				  help_right_form,
				  NULL));
    help_unit_def_data =
      XtVaCreateManagedWidget("helpunitdefdata", 
			      labelWidgetClass, 
			      help_right_form,
			      NULL);
    help_unit_move =
      I_L(XtVaCreateManagedWidget("helpunitmove", 
				  labelWidgetClass, 
				  help_right_form,
				  NULL));
    help_unit_move_data =
      XtVaCreateManagedWidget("helpunitmovedata", 
			      labelWidgetClass, 
			      help_right_form,
			      NULL);
    help_unit_tile =
      XtVaCreateManagedWidget("helpunittile",
			      labelWidgetClass,
			      help_right_form,
			      XtNwidth, UNIT_TILE_WIDTH,
			      XtNheight, UNIT_TILE_HEIGHT,
			      NULL);  
    XtAddCallback(help_unit_tile,
                  XtNdestroyCallback,free_bitmap_destroy_callback,
		  NULL);
    help_unit_fp =
      I_L(XtVaCreateManagedWidget("helpunitfp", 
				  labelWidgetClass, 
				  help_right_form,
				  NULL));
    help_unit_fp_data =
      XtVaCreateManagedWidget("helpunitfpdata", 
			      labelWidgetClass, 
			      help_right_form,
			      NULL);
    help_unit_hp =
      I_L(XtVaCreateManagedWidget("helpunithp", 
				  labelWidgetClass, 
				  help_right_form,
				  NULL));
    help_unit_hp_data =
      XtVaCreateManagedWidget("helpunithpdata", 
			      labelWidgetClass, 
			      help_right_form,
			      NULL);
    help_unit_visrange =
      I_L(XtVaCreateManagedWidget("helpunitvisrange",
				  labelWidgetClass,
				  help_right_form,
				  NULL));
    help_unit_visrange_data =
      XtVaCreateManagedWidget("helpunitvisrangedata",
			      labelWidgetClass,
			      help_right_form,
			      NULL);
    help_unit_upkeep =
      I_L(XtVaCreateManagedWidget("helpunitupkeep",
				  labelWidgetClass,
				  help_right_form,
				  NULL));
    help_unit_upkeep_data =
      XtVaCreateManagedWidget("helpunitupkeepdata",
			      labelWidgetClass,
			      help_right_form,
			      NULL);
    help_improvement_req =
      I_L(XtVaCreateManagedWidget("helpimprreq", 
				  labelWidgetClass, 
				  help_right_form,
				  XtNfromVert, help_unit_upkeep, 
				  NULL));
     help_improvement_req_data =
       XtVaCreateManagedWidget("helpimprreqdata", 
			       labelWidgetClass, 
			       help_right_form,
			       XtNfromVert, help_unit_upkeep, 
			       NULL);
     help_wonder_obsolete =
       I_L(XtVaCreateManagedWidget("helpwonderobsolete", 
				   labelWidgetClass, 
				   help_right_form,
				   XtNfromVert, help_unit_upkeep, 
				   NULL));
     help_wonder_obsolete_data =
       XtVaCreateManagedWidget("helpwonderobsoletedata", 
			       labelWidgetClass, 
			       help_right_form,
			       XtNfromVert, help_unit_upkeep, 
			       NULL);
     XtVaGetValues(help_improvement_req, XtNy, &ay, XtNheight, &ah, NULL);
     help_tree_viewport =
       XtVaCreateManagedWidget("helptreeviewport", 
			       viewportWidgetClass, 
			       help_right_form,
			       XtNwidth, w2,
			       XtNheight, MAX(1,h-(ay+ah)-10),
			       NULL);
     help_tech_tree =
       XtVaCreateManagedWidget("helptree", 
			       treeWidgetClass, 
			       help_tree_viewport,
			       NULL);
     XawTreeForceLayout(help_tech_tree);
  }
  else if(type==HELP_TECH) {
    help_text =
      XtVaCreateManagedWidget("helptext", 
			      asciiTextWidgetClass, 
			      help_right_form,
			      XtNeditType, XawtextRead,
			      XtNscrollVertical, XawtextScrollAlways, 
			      XtNwidth, w2,
			      XtNheight, 95,
			      NULL);

    XtVaGetValues(help_text, XtNy, &ay, XtNheight, &ah, NULL);
    help_tree_viewport =
      XtVaCreateManagedWidget("helptreeviewport", 
			      viewportWidgetClass, 
			      help_right_form,
			      XtNwidth, w2,
			      XtNheight, MAX(1,h-(ay+ah)-10),
			      XtNfromVert,help_text,
			      NULL);
    help_tech_tree =
      XtVaCreateManagedWidget("helptree", 
			      treeWidgetClass, 
			      help_tree_viewport,
			      NULL);
    XawTreeForceLayout(help_tech_tree);  
  }
  else if(type==HELP_TERRAIN) {
    help_text =
      XtVaCreateManagedWidget
      (
       "helptext",
       asciiTextWidgetClass,
       help_right_form,
       XtNeditType, XawtextRead,
       XtNscrollVertical, XawtextScrollAlways,
       XtNwidth, w2,
       XtNheight, 140,
       NULL);

    help_terrain_movement_defense =
      I_L(XtVaCreateManagedWidget
      (
       "help_terrain_movement_defense",
       labelWidgetClass,
       help_right_form,
       NULL));
    help_terrain_movement_defense_data =
      XtVaCreateManagedWidget
      (
       "help_terrain_movement_defense_data",
       labelWidgetClass,
       help_right_form,
       NULL);

    help_terrain_food_shield_trade =
      I_L(XtVaCreateManagedWidget
      (
       "help_terrain_food_shield_trade",
       labelWidgetClass,
       help_right_form,
       NULL));
    help_terrain_food_shield_trade_data =
      XtVaCreateManagedWidget
      (
       "help_terrain_food_shield_trade_data",
       labelWidgetClass,
       help_right_form,
       NULL);

    help_terrain_special_1 =
      XtVaCreateManagedWidget
      (
       "help_terrain_special_1",
       labelWidgetClass,
       help_right_form,
       NULL);
    help_terrain_special_1_data =
      XtVaCreateManagedWidget
      (
       "help_terrain_special_1_data",
       labelWidgetClass,
       help_right_form,
       NULL);

    help_terrain_special_2 =
      XtVaCreateManagedWidget
      (
       "help_terrain_special_2",
       labelWidgetClass,
       help_right_form,
       NULL);
    help_terrain_special_2_data =
      XtVaCreateManagedWidget
      (
       "help_terrain_special_2_data",
       labelWidgetClass,
       help_right_form,
       NULL);

    help_terrain_road_result_time =
      I_L(XtVaCreateManagedWidget
      (
       "help_terrain_road_result_time",
       labelWidgetClass,
       help_right_form,
       NULL));
    help_terrain_road_result_time_data =
      XtVaCreateManagedWidget
      (
       "help_terrain_road_result_time_data",
       labelWidgetClass,
       help_right_form,
       NULL);

    help_terrain_irrigation_result_time =
      I_L(XtVaCreateManagedWidget
      (
       "help_terrain_irrigation_result_time",
       labelWidgetClass,
       help_right_form,
       NULL));
    help_terrain_irrigation_result_time_data =
      XtVaCreateManagedWidget
      (
       "help_terrain_irrigation_result_time_data",
       labelWidgetClass,
       help_right_form,
       NULL);

    help_terrain_mining_result_time =
      I_L(XtVaCreateManagedWidget
      (
       "help_terrain_mining_result_time",
       labelWidgetClass,
       help_right_form,
       NULL));
    help_terrain_mining_result_time_data =
      XtVaCreateManagedWidget
      (
       "help_terrain_mining_result_time_data",
       labelWidgetClass,
       help_right_form,
       NULL);

    help_terrain_transform_result_time =
      I_L(XtVaCreateManagedWidget
      (
       "help_terrain_transform_result_time",
       labelWidgetClass,
       help_right_form,
       NULL));
    help_terrain_transform_result_time_data =
      XtVaCreateManagedWidget
      (
       "help_terrain_transform_result_time_data",
       labelWidgetClass,
       help_right_form,
       NULL);
  }
}

/**************************************************************************
...
**************************************************************************/
static void help_update_improvement(const struct help_item *pitem,
				    char *title, int which)
{
  char buf[64000];
  
  create_help_page(HELP_IMPROVEMENT);
  
  if (which<game.num_impr_types) {
    struct impr_type *imp = &improvement_types[which];
    sprintf(buf, "%d ", impr_build_shield_cost(which));
    xaw_set_label(help_improvement_cost_data, buf);
    sprintf(buf, "%d ", imp->upkeep);
    xaw_set_label(help_improvement_upkeep_data, buf);
    if (imp->tech_req == A_LAST) {
      xaw_set_label(help_improvement_req_data, _("(Never)"));
    } else {
      xaw_set_label(help_improvement_req_data,
		    advances[imp->tech_req].name);
    }
    create_tech_tree(help_tech_tree, 0, imp->tech_req, 3);
  }
  else {
    xaw_set_label(help_improvement_cost_data, "0 ");
    xaw_set_label(help_improvement_upkeep_data, "0 ");
    xaw_set_label(help_improvement_req_data, _("(Never)"));
    create_tech_tree(help_tech_tree, 0, A_LAST, 3);
  }
  set_title_topic(pitem);
  helptext_building(buf, sizeof(buf), which, pitem->text);
  XtVaSetValues(help_text, XtNstring, buf, NULL);
}
  
/**************************************************************************
...
**************************************************************************/
static void help_update_wonder(const struct help_item *pitem,
			       char *title, int which)
{
  char buf[64000];
  
  create_help_page(HELP_WONDER);

  if (which<game.num_impr_types) {
    struct impr_type *imp = &improvement_types[which];
    sprintf(buf, "%d ", impr_build_shield_cost(which));
    xaw_set_label(help_improvement_cost_data, buf);
    if (imp->tech_req == A_LAST) {
      xaw_set_label(help_improvement_req_data, _("(Never)"));
    } else {
      xaw_set_label(help_improvement_req_data,
		    advances[imp->tech_req].name);
    }
    if (tech_exists(imp->obsolete_by)) {
      xaw_set_label(help_wonder_obsolete_data,
		    advances[imp->obsolete_by].name);
    } else {
      xaw_set_label(help_wonder_obsolete_data, _("(Never)"));
    }
    create_tech_tree(help_tech_tree, 0, imp->tech_req, 3);
  }
  else {
    /* can't find wonder */
    xaw_set_label(help_improvement_cost_data, "0 ");
    xaw_set_label(help_improvement_req_data, _("(Never)"));
    xaw_set_label(help_wonder_obsolete_data, _("None"));
    create_tech_tree(help_tech_tree, 0, game.num_tech_types, 3); 
  }
  set_title_topic(pitem);
  helptext_building(buf, sizeof(buf), which, pitem->text);
  XtVaSetValues(help_text, XtNstring, buf, NULL);
}

/**************************************************************************
...
**************************************************************************/
static void help_update_unit_type(const struct help_item *pitem,
				  char *title, int i)
{
  char *buf = &long_buffer[0];
  
  create_help_page(HELP_UNIT);
  if (i<game.num_unit_types) {
    struct unit_type *utype = get_unit_type(i);
    sprintf(buf, "%d ", unit_build_shield_cost(i));
    xaw_set_label(help_unit_cost_data, buf);
    sprintf(buf, "%d ", utype->attack_strength);
    xaw_set_label(help_unit_attack_data, buf);
    sprintf(buf, "%d ", utype->defense_strength);
    xaw_set_label(help_unit_def_data, buf);
    sprintf(buf, "%d ", utype->move_rate/3);
    xaw_set_label(help_unit_move_data, buf);
    sprintf(buf, "%d ", utype->firepower);
    xaw_set_label(help_unit_fp_data, buf);
    sprintf(buf, "%d ", utype->hp);
    xaw_set_label(help_unit_hp_data, buf);
    sprintf(buf, "%d ", utype->vision_range);
    xaw_set_label(help_unit_visrange_data, buf);
    xaw_set_label(help_unit_upkeep_data, helptext_unit_upkeep_str(i));
    if(utype->tech_requirement==A_LAST) {
      xaw_set_label(help_improvement_req_data, _("(Never)"));
    } else {
      xaw_set_label(help_improvement_req_data,
		    advances[utype->tech_requirement].name);
    }
    create_tech_tree(help_tech_tree, 0, utype->tech_requirement, 3);
    if(utype->obsoleted_by==-1) {
      xaw_set_label(help_wonder_obsolete_data, _("None"));
    } else {
      xaw_set_label(help_wonder_obsolete_data,
		    get_unit_type(utype->obsoleted_by)->name);
    }
    /* add text for transport_capacity, fuel, and flags: */
    helptext_unit(buf, i, pitem->text);
    XtVaSetValues(help_text, XtNstring, buf, NULL);
  }
  else {
    xaw_set_label(help_unit_cost_data, "0 ");
    xaw_set_label(help_unit_attack_data, "0 ");
    xaw_set_label(help_unit_def_data, "0 ");
    xaw_set_label(help_unit_move_data, "0 ");
    xaw_set_label(help_unit_fp_data, "0 ");
    xaw_set_label(help_unit_hp_data, "0 ");
    xaw_set_label(help_unit_visrange_data, "0 ");
    xaw_set_label(help_improvement_req_data, _("(Never)"));
    create_tech_tree(help_tech_tree, 0, game.num_tech_types, 3);
    xaw_set_label(help_wonder_obsolete_data, _("None"));
    XtVaSetValues(help_text, XtNstring, pitem->text, NULL);
  }
  xaw_set_bitmap(help_unit_tile, create_overlay_unit(i));
  set_title_topic(pitem);
}

/**************************************************************************
...
**************************************************************************/
static void help_update_tech(const struct help_item *pitem, char *title, int i)
{
  char *buf = &long_buffer[0];
  int j;

  create_help_page(HELP_TECH);
  set_title_topic(pitem);

  if (!is_future_tech(i)) {
    create_tech_tree(help_tech_tree, 0, i, 3);
    helptext_tech(buf, i, pitem->text);

    impr_type_iterate(j) {
      if(i==improvement_types[j].tech_req) 
	sprintf(buf+strlen(buf), _("Allows %s.\n"),
		improvement_types[j].name);
      if(i==improvement_types[j].obsolete_by)
	sprintf(buf+strlen(buf), _("Obsoletes %s.\n"),
		improvement_types[j].name);
    } impr_type_iterate_end;

    unit_type_iterate(j) {
      if(i==get_unit_type(j)->tech_requirement) 
	sprintf(buf+strlen(buf), _("Allows %s.\n"), 
		get_unit_type(j)->name);
    } unit_type_iterate_end;

    for (j = 0; j < game.num_tech_types; j++) {
      if(i==advances[j].req[0]) {
	if(advances[j].req[1]==A_NONE)
	  sprintf(buf+strlen(buf), _("Allows %s.\n"), 
		  advances[j].name);
	else
	  sprintf(buf+strlen(buf), _("Allows %s (with %s).\n"), 
		  advances[j].name, advances[advances[j].req[1]].name);
      }
      if(i==advances[j].req[1]) {
	sprintf(buf+strlen(buf), _("Allows %s (with %s).\n"), 
		advances[j].name, advances[advances[j].req[0]].name);
      }
    }
    if (strlen(buf)) strcat(buf, "\n");
    if (advances[i].helptext) {
      sprintf(buf+strlen(buf), "%s\n", _(advances[i].helptext));
    }
  }
  else {
    create_help_page(HELP_TECH);
    create_tech_tree(help_tech_tree, 0, game.num_tech_types, 3);
    strcpy(buf, pitem->text);
  }
  wordwrap_string(buf, 68);
  XtVaSetValues(help_text, XtNstring, buf, NULL);
}

/**************************************************************************
...
**************************************************************************/
static void help_update_terrain(const struct help_item *pitem,
				char *title, int i)
{
  char *buf = &long_buffer[0];
  struct tile_type *ptype = get_tile_type(i);

  create_help_page(HELP_TERRAIN);
  set_title_topic(pitem);

  helptext_terrain(buf, i, pitem->text);
  XtVaSetValues(help_text, XtNstring, buf, NULL);

  if (i < T_COUNT)
    {
      sprintf (buf, "%d/%d.%d",
	       ptype->movement_cost,
	       (int)(ptype->defense_bonus/10), ptype->defense_bonus%10);
      xaw_set_label (help_terrain_movement_defense_data, buf);

      sprintf (buf, "%d/%d/%d",
	       ptype->food,
	       ptype->shield,
	       ptype->trade);
      xaw_set_label (help_terrain_food_shield_trade_data, buf);

      if (*(ptype->special_1_name))
	{
	  sprintf (buf, _("%s F/R/T:"),
		   ptype->special_1_name);
	  xaw_set_label (help_terrain_special_1, buf);
	  sprintf (buf, "%d/%d/%d",
		   ptype->food_special_1,
		   ptype->shield_special_1,
		   ptype->trade_special_1);
	  xaw_set_label (help_terrain_special_1_data, buf);
	} else {
	  xaw_set_label (help_terrain_special_1, "");
	  xaw_set_label (help_terrain_special_1_data, "");
	}

      if (*(ptype->special_2_name))
	{
	  sprintf (buf, _("%s F/R/T:"),
		   ptype->special_2_name);
	  xaw_set_label (help_terrain_special_2, buf);
	  sprintf (buf, "%d/%d/%d",
		   ptype->food_special_2,
		   ptype->shield_special_2,
		   ptype->trade_special_2);
	  xaw_set_label (help_terrain_special_2_data, buf);
	} else {
	  xaw_set_label (help_terrain_special_2, "");
	  xaw_set_label (help_terrain_special_2_data, "");
	}

      if (ptype->road_trade_incr > 0)
	{
	  sprintf (buf, _("+%d Trade / %d"),
		   ptype->road_trade_incr,
		   ptype->road_time);
	}
      else if (ptype->road_time > 0)
	{
	  sprintf (buf, _("no extra / %d"),
		   ptype->road_time);
	}
      else
	{
	  strcpy (buf, _("n/a"));
	}
      xaw_set_label (help_terrain_road_result_time_data, buf);

      strcpy (buf, _("n/a"));
      if (ptype->irrigation_result == i)
	{
	  if (ptype->irrigation_food_incr > 0)
	    {
	      sprintf (buf, _("+%d Food / %d"),
		       ptype->irrigation_food_incr,
		       ptype->irrigation_time);
	    }
	}
      else if (ptype->irrigation_result != T_NONE)
	{
	  sprintf (buf, "%s / %d",
		   get_tile_type(ptype->irrigation_result)->terrain_name,
		   ptype->irrigation_time);
	}
      xaw_set_label (help_terrain_irrigation_result_time_data, buf);

      strcpy (buf, _("n/a"));
      if (ptype->mining_result == i)
	{
	  if (ptype->mining_shield_incr > 0)
	    {
	      sprintf (buf, _("+%d Res. / %d"),
		       ptype->mining_shield_incr,
		       ptype->mining_time);
	    }
	}
      else if (ptype->mining_result != T_NONE)
	{
	  sprintf (buf, "%s / %d",
		   get_tile_type(ptype->mining_result)->terrain_name,
		   ptype->mining_time);
	}
      xaw_set_label (help_terrain_mining_result_time_data, buf);

      if (ptype->transform_result != T_NONE)
	{
	  sprintf (buf, "%s / %d",
		   get_tile_type(ptype->transform_result)->terrain_name,
		   ptype->transform_time);
	} else {
	  strcpy (buf, _("n/a"));
	}
      xaw_set_label (help_terrain_transform_result_time_data, buf);
    }
}

/**************************************************************************
  This is currently just a text page, with special text:
**************************************************************************/
static void help_update_government(const struct help_item *pitem,
				   char *title, struct government *gov)
{
  char *buf = &long_buffer[0];

  if (gov==NULL) {
    strcat(buf, pitem->text);
  } else {
    helptext_government(buf, gov-governments, pitem->text);
  }
  create_help_page(HELP_TEXT);
  set_title_topic(pitem);
  XtVaSetValues(help_text, XtNstring, buf, NULL);
}

/**************************************************************************
...
**************************************************************************/
static void help_update_dialog(const struct help_item *pitem)
{
  int i;
  char *top;

  /* figure out what kind of item is required for pitem ingo */

  for (top = pitem->topic; *top == ' '; top++) {
    /* nothing */
  }

  switch(pitem->type) {
  case HELP_IMPROVEMENT:
    i = find_improvement_by_name(top);
    if(i!=B_LAST && is_wonder(i)) i = B_LAST;
    help_update_improvement(pitem, top, i);
    break;
  case HELP_WONDER:
    i = find_improvement_by_name(top);
    if(i!=B_LAST && !is_wonder(i)) i = B_LAST;
    help_update_wonder(pitem, top, i);
    break;
  case HELP_UNIT:
    help_update_unit_type(pitem, top, find_unit_type_by_name(top));
    break;
  case HELP_TECH:
    help_update_tech(pitem, top, find_tech_by_name(top));
    break;
  case HELP_TERRAIN:
    help_update_terrain(pitem, top, get_terrain_by_name(top));
    break;
  case HELP_GOVERNMENT:
    help_update_government(pitem, top, find_government_by_name(top));
    break;
  case HELP_TEXT:
  default:
    /* it was a pure text item */ 
    create_help_page(HELP_TEXT);
    set_title_topic(pitem);
    XtVaSetValues(help_text, XtNstring, pitem->text, NULL);
  }

  if (help_tree_viewport) {
    /* Buggy sun athena may require this? --dwp */
    /* And it possibly looks better anyway... */
    XtVaSetValues(help_tree_viewport, XtNforceBars, True, NULL);
  }
}

/**************************************************************************
...
**************************************************************************/
static int help_tree_destroy_children(Widget w)
{
  Widget *children=0;
  Cardinal cnt;
  int did_destroy=0;
  
  XtVaGetValues(help_tech_tree, 
		XtNchildren, &children, 
		XtNnumChildren, &cnt,
		NULL);

  for (; cnt > 0; cnt--, children++) {
    if(XtIsSubclass(*children, commandWidgetClass)) {
      Widget par;
      XtVaGetValues(*children, XtNtreeParent, &par, NULL);
      if(par==w) {
	help_tree_destroy_children(*children);
	XtDestroyWidget(*children);
	did_destroy=1;
      }
    }
  }
  
  return did_destroy;
}


/**************************************************************************
...
**************************************************************************/
static void help_tree_node_callback(Widget w, XtPointer client_data, 
			     XtPointer call_data)
{
  size_t tech=(size_t)client_data;
  
  if(!help_tree_destroy_children(w)) {
    if(advances[tech].req[0]!=A_NONE)
      create_tech_tree(help_tech_tree, w, advances[tech].req[0], 1);
    if(advances[tech].req[1]!=A_NONE)
      create_tech_tree(help_tech_tree, w, advances[tech].req[1], 1);
  }
  
}


/**************************************************************************
...
**************************************************************************/
static void help_list_callback(Widget w, XtPointer client_data, 
			XtPointer call_data)
{
  XawListReturnStruct *ret;
  
  ret=XawListShowCurrent(help_list);

  if(ret->list_index!=XAW_LIST_NONE) {
    const struct help_item *pitem=get_help_item(ret->list_index);
    if(pitem)  {
      help_update_dialog(pitem);
      set_title_topic(pitem);
    }
    XawListHighlight(help_list, ret->list_index); 
  }
}

/**************************************************************************
...
**************************************************************************/
static void help_close_command_callback(Widget w, XtPointer client_data, 
				 XtPointer call_data)
{
  XtDestroyWidget(help_dialog_shell);
  help_dialog_shell=0;
}

/****************************************************************
...
*****************************************************************/
void helpdlg_msg_close(Widget w)
{
  help_close_command_callback(w, NULL, NULL);
}

/**************************************************************************
...
**************************************************************************/
static void select_help_item(int item)
{
  int nitems, pos;
  Dimension height;
  
  XtVaGetValues(help_list, XtNnumberStrings, &nitems, NULL);
  XtVaGetValues(help_list, XtNheight, &height, NULL);
  pos= (((double)item)/nitems)*height;
  XawViewportSetCoordinates(help_viewport, 0, pos);
  XawListHighlight(help_list, item); 
}

/****************************************************************
...
*****************************************************************/
static void select_help_item_string(const char *item,
				    enum help_page_type htype)
{
  const struct help_item *pitem;
  int idx;

  pitem = get_help_item_spec(item, htype, &idx);
  if(idx==-1) idx = 0;
  select_help_item(idx);
  help_update_dialog(pitem);
}
