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
#include <assert.h>

#include <libraries/mui.h>
#include <mui/NListview_MCC.h>

#include <clib/alib_protos.h>
#include <proto/exec.h>
#include <proto/muimaster.h>
#include <proto/graphics.h>

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
#include "version.h"

#include "climisc.h"
#include "colors.h"
#include "graphics.h"
#include "helpdata.h"

#include "helpdlg.h"

/* Amiga Client Stuff */
#include "mapclass.h"
#include "objecttreeclass.h"
#include "muistuff.h"
IMPORT Object *app;

Object *help_wnd;
Object *help_topic_listview;
Object *help_topic_text;
Object *help_text_listview;
Object *help_right_group;

Object *help_page_group;
Object *help_imprv_cost_text;
Object *help_imprv_upkeep_text;
Object *help_imprv_needs_button;	/* tech */

Object *help_wonder_cost_text;
Object *help_wonder_needs_button;	/* tech */

Object *help_wonder_obsolete_button;	/* tech */

Object *help_tech_group;	/* Allows tech with tech */
Object *help_tech_tree;	/* The tree of techs */

Object *help_unit_sprite;
Object *help_unit_cost_text;
Object *help_unit_attack_text;
Object *help_unit_defense_text;
Object *help_unit_move_text;
Object *help_unit_vision_text;
Object *help_unit_firepower_text;
Object *help_unit_hitpoints_text;
Object *help_unit_basic_upkeep_text;
Object *help_unit_needs_button;
Object *help_unit_obsolete_button;
Object *help_terrain_sprite;
Object *help_terrain_food_text;
Object *help_terrain_prod_text;
Object *help_terrain_trade_text;
Object *help_terrain_move_text;
Object *help_terrain_defense_text;

static enum help_page_type help_last_page;
extern char long_buffer[64000];	/* helpdata.c */

static void clear_page_objects(void);
static void free_help_page(void);
/*static void create_help_dialog(void);*/
static void help_update_dialog(const struct help_item *pitem);
static void create_help_page(enum help_page_type type);

static void select_help_item(int item);
static void select_help_item_string(const char *item,
				    enum help_page_type htype);

static void help_tree_leaf( Object **pobj); /* Callback Function */

/****************************************************************
...
*****************************************************************/
static void set_title_topic(char *topic)
{
  char *text;

  if (!strcmp(topic, "Freeciv")
      || !strcmp(topic, "About")
      || !strcmp(topic, _("About")))
  {
    text = FREECIV_NAME_VERSION;
  }
  else
  {
    text = topic;
  }

  set(help_topic_text, MUIA_Text_Contents, topic);
}

/****************************************************************
...
*****************************************************************/
void popdown_help_dialog(void)
{
  if (help_wnd)
  {
    set(help_wnd, MUIA_Window_Open, FALSE);
  }
  free_help_page();
}


/****************************************************************
 Callback for activation of a new entry in the topic listview
*****************************************************************/
static void help_topic_active(ULONG * newact)
{
  const struct help_item *pitem = get_help_item(*newact);
  if (pitem)
  {
    help_update_dialog(pitem);
  }
}

/****************************************************************
 Callback for a Hyperlink button
*****************************************************************/
static void help_hyperlink(Object ** text_obj)
{
  char *s = (char *) xget(*text_obj, MUIA_Text_Contents);
  enum help_page_type type = (enum help_page_type) xget(*text_obj, MUIA_UserData);

  if (strcmp(s, _("(Never)")) && strcmp(s, _("None"))
      && strcmp(s, advances[A_NONE].name))
  {
    const struct help_item *pitem;
    int idx;

    pitem = get_help_item_spec(s, type, &idx);
    if (idx == -1)
      idx = 0;

    /* Changing a page will dispose the button */
    DoMethod(app, MUIM_Application_PushMethod, help_topic_listview, 3, MUIM_Set, MUIA_NList_Active, idx);
  }
}

/****************************************************************
 Creates a help button
*****************************************************************/
static Object *MakeHelpButton(STRPTR label, enum help_page_type type)
{
  Object *button = MakeButton(label);

  if(button)
  {
    SetAttrs(button,
	     MUIA_UserData, type,
	     MUIA_Weight, 0,
	     TAG_DONE);
    DoMethod(button, MUIM_Notify, MUIA_Pressed, FALSE, app, 4, MUIM_CallHook, &standart_hook, help_hyperlink, button);
  }
  return button;
}

/****************************************************************
...
*****************************************************************/
void popup_help_dialog_typed(char *item, enum help_page_type htype)
{
  if (!help_wnd)
  {
    help_wnd = WindowObject,
      MUIA_Window_Title, "Help",
      MUIA_Window_ID, 'HELP',

      WindowContents, HGroup,
          Child, help_topic_listview = NListviewObject,
              MUIA_Weight, 50,
              MUIA_NListview_NList, NListObject,
                  MUIA_NList_ConstructHook, MUIV_NList_ConstructHook_String,
                  MUIA_NList_DestructHook, MUIV_NList_DestructHook_String,
                  End,
              End,
          Child, VGroup,
              Child, help_topic_text = TextObject,
                  MUIA_Text_PreParse, "\33c",
                  End,
             Child, help_right_group = VGroup,
                 Child, help_text_listview = NListviewObject,
                     MUIA_Weight, 100,
                     MUIA_NListview_NList, NListObject,
                         MUIA_NList_ConstructHook, MUIV_NList_ConstructHook_String,
                         MUIA_NList_DestructHook, MUIV_NList_DestructHook_String,
                         MUIA_NList_Input, FALSE,
                         MUIA_NList_TypeSelect, MUIV_NList_TypeSelect_Char,
                         MUIA_NList_AutoCopyToClip, TRUE,
                         End,
                     End,
                 End,
             End,
         End,
      End;

    if (help_wnd)
    {
      DoMethod(help_wnd, MUIM_Notify, MUIA_Window_CloseRequest, TRUE, help_wnd, 3, MUIM_Set, MUIA_Window_Open, FALSE);
      DoMethod(help_topic_listview, MUIM_Notify, MUIA_NList_Active, MUIV_EveryTime, help_topic_listview, 4, MUIM_CallHook, &standart_hook, help_topic_active, MUIV_TriggerValue);

      DoMethod(app, OM_ADDMEMBER, help_wnd);
    }
  }

  if (help_wnd)
  {
    set(help_topic_listview, MUIA_NList_Quiet, TRUE);
    DoMethod(help_topic_listview, MUIM_NList_Clear);

    help_items_iterate(pitem)
      DoMethod(help_topic_listview, MUIM_NList_InsertSingle, pitem->topic, MUIV_NList_Insert_Bottom);
    help_items_iterate_end;

    set(help_topic_listview, MUIA_NList_Quiet, FALSE);
    set(help_wnd, MUIA_Window_Open, TRUE);
  }
  select_help_item_string(item, htype);
}


/****************************************************************
...
*****************************************************************/
void popup_help_dialog_string(char *item)
{
  popup_help_dialog_typed(item, HELP_ANY);
}


/****************************************************************
 Clear all pointers of objects which are displayed in
 a page
*****************************************************************/
static void clear_page_objects(void)
{
  help_page_group =
    help_imprv_cost_text =
    help_imprv_upkeep_text =
    help_imprv_needs_button =
    help_wonder_cost_text =
    help_wonder_needs_button =
    help_wonder_obsolete_button =
    help_tech_group =
    help_tech_tree =
    help_unit_sprite =
    help_unit_cost_text =
    help_unit_attack_text =
    help_unit_defense_text =
    help_unit_move_text =
    help_unit_vision_text =
    help_unit_firepower_text =
    help_unit_hitpoints_text =
    help_unit_basic_upkeep_text =
    help_unit_needs_button =
    help_unit_obsolete_button =
    help_terrain_sprite =
    help_terrain_food_text =
    help_terrain_prod_text =
    help_terrain_trade_text = NULL;
}

/****************************************************************
 Create the tech tree
*****************************************************************/
static void create_tech_tree(Object *tree, APTR parent, int tech, int levels)
{
  APTR leaf;
/*
  int type;
  char *bg="";*/
  char label[MAX_LEN_NAME+3];
  /*
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
 */ 
  if(!tech_exists(tech))
  {
    Object *o = MakeButton("Removed");
    leaf = (APTR)DoMethod(tree, MUIM_ObjectTree_AddNode,NULL,o);
/*    bg=TREE_NODE_REMOVED_TECH_BG;
    l=XtVaCreateManagedWidget("treenode", commandWidgetClass, 
			      tree,
			      XtNlabel, label,
			      XtNbackground, bg, NULL);
    XtVaSetValues(l, XtVaTypedArg, XtNbackground, 
		  XtRString, bg, strlen(bg)+1, NULL);*/
     return;
  }
  
  sprintf(label,"%s:%d",advances[tech].name,
                        tech_goal_turns(game.player_ptr,tech));
  

  if(parent)
  {
    Object *o = MakeButton(label);
    if (o)
    {
      DoMethod(o, MUIM_Notify, MUIA_Pressed, FALSE, app, 7, MUIM_Application_PushMethod, app, 4, MUIM_CallHook, &standart_hook, help_tree_leaf, o);
      set(o,MUIA_UserData, tech);
      leaf = (APTR)DoMethod(tree, MUIM_ObjectTree_AddNode,parent,o);
    }
  } else
  {
    Object *o = MakeButton(label);
    if (o)
    {
      DoMethod(o, MUIM_Notify, MUIA_Pressed, FALSE, app, 7, MUIM_Application_PushMethod, app, 4, MUIM_CallHook, &standart_hook, help_tree_leaf, o);
//      DoMethod(o, MUIM_Notify, MUIA_Pressed, FALSE, app, 4, MUIM_CallHook, &standart_hook, help_tree_leaf, o);
      set(o,MUIA_UserData, tech);
      leaf = (APTR)DoMethod(tree, MUIM_ObjectTree_AddNode,NULL,o);
    }
  }

/*
  XtAddCallback(l, XtNcallback, help_tree_node_callback, (XtPointer)tech);

  XtVaSetValues(l, XtVaTypedArg, XtNbackground, 
		XtRString, bg, strlen(bg)+1, NULL);

*/

  
  if(--levels>0) {
    if(advances[tech].req[0]!=A_NONE)
      create_tech_tree(tree, leaf, advances[tech].req[0], levels);
    if(advances[tech].req[1]!=A_NONE)
      create_tech_tree(tree, leaf, advances[tech].req[1], levels);
  }
  
  
}

/****************************************************************
 Callback for a button in the Techtree
*****************************************************************/
static void help_tree_leaf( Object **pobj )
{
  Object *obj = *pobj;
  APTR leaf = (APTR)DoMethod(help_tech_tree, MUIM_ObjectTree_FindObject, obj);
  if (leaf)
  {
    int tech = xget(obj, MUIA_UserData);
    DoMethod(help_tech_tree, MUIM_Group_InitChange);

    if(!DoMethod(help_tech_tree, MUIM_ObjectTree_HasSubNodes, leaf))
    {
      if(advances[tech].req[0]!=A_NONE)
	create_tech_tree(help_tech_tree, leaf, advances[tech].req[0], 1);
      if(advances[tech].req[1]!=A_NONE)
	create_tech_tree(help_tech_tree, leaf, advances[tech].req[1], 1);
    } else
    {
      DoMethod(help_tech_tree,MUIM_ObjectTree_ClearSubNodes,leaf);
    }

    DoMethod(help_tech_tree, MUIM_Group_ExitChange);
  }
}

/**************************************************************************
...
**************************************************************************/
static void free_help_page(void)
{
  if (help_page_group)
  {
    DoMethod(help_right_group, OM_REMMEMBER, help_page_group);
    MUI_DisposeObject(help_page_group);
  }
  clear_page_objects();

  help_last_page = HELP_ANY;
}

/**************************************************************************
 Creates the Help page
**************************************************************************/
static void create_help_page(enum help_page_type type)
{
  if (type != help_last_page)
  {
    DoMethod(help_right_group, MUIM_Group_InitChange);
    free_help_page();

    switch (type)
    {
    case HELP_IMPROVEMENT:
      help_page_group = HGroup,
	Child, MakeLabel("Cost:"),
	Child, help_imprv_cost_text = TextObject, End,
	Child, MakeLabel("Upkeep:"),
	Child, help_imprv_upkeep_text = TextObject, End,
	Child, MakeLabel("Needs:"),
	Child, help_imprv_needs_button = MakeButton(""),
	End;

      if (help_page_group)
      {
	DoMethod(help_imprv_needs_button, MUIM_Notify, MUIA_Pressed, FALSE, app, 4, MUIM_CallHook, &standart_hook, help_hyperlink, help_imprv_needs_button);
      }
      break;

    case HELP_WONDER:
      help_page_group = HGroup,
	Child, MakeLabel("Cost:"),
	Child, help_wonder_cost_text = TextObject, End,
	Child, MakeLabel("Needs:"),
	Child, help_wonder_needs_button = MakeButton(""),
	Child, MakeLabel("Obsolete by:"),
	Child, help_wonder_obsolete_button = MakeButton(""),
	End;

      if (help_page_group)
      {
	DoMethod(help_wonder_needs_button, MUIM_Notify, MUIA_Pressed, FALSE, app, 4, MUIM_CallHook, &standart_hook, help_hyperlink, help_wonder_needs_button);
	DoMethod(help_wonder_obsolete_button, MUIM_Notify, MUIA_Pressed, FALSE, app, 4, MUIM_CallHook, &standart_hook, help_hyperlink, help_wonder_obsolete_button);
      }
      break;

    case HELP_TERRAIN:
      help_page_group = VGroup,
	Child, HGroup,
	    Child, HSpace(0),
	    Child, help_terrain_sprite = SpriteObject,
		MUIA_FixWidth, get_normal_tile_width(),
		MUIA_FixHeight, get_normal_tile_height(),
		End,
	    Child, HSpace(0),
	    End,
	Child, HGroup,
	    Child, help_terrain_move_text = TextObject, MUIA_Text_PreParse, "\33c", End,
	    Child, help_terrain_defense_text = TextObject, MUIA_Text_PreParse, "\33c", End,
	    End,
	Child, HGroup,
	    Child, help_terrain_food_text = TextObject, MUIA_Text_PreParse, "\33c", End,
	    Child, help_terrain_prod_text = TextObject, MUIA_Text_PreParse, "\33c", End,
	    Child, help_terrain_trade_text = TextObject, MUIA_Text_PreParse, "\33c", End,
	    End,
	End;
      break;

    case HELP_TECH:
      help_page_group = VGroup,
	Child, help_tech_group = VGroup,
	    Child, HSpace(0),
	    End,
	Child, help_tech_tree = ObjectTreeObject,
	    VirtualFrame,
	    End,
	End;
      break;

    case HELP_UNIT:
      help_page_group = VGroup,
	Child, HGroup,
	    Child, HSpace(0),
	    Child, help_unit_sprite = SpriteObject,
		MUIA_FixWidth, get_normal_tile_width(),
		MUIA_FixHeight, get_normal_tile_height(),
		End,
	    Child, HSpace(0),
	    End,
	Child, HGroup,
	    Child, help_unit_cost_text = TextObject, MUIA_Text_PreParse, "\33c", End,
	    End,
	Child, ColGroup(4),
	    Child, MakeLabel("Attack:"),
	    Child, help_unit_attack_text = TextObject, End,
	    Child, MakeLabel("Defense:"),
	    Child, help_unit_defense_text = TextObject, End,
	    Child, MakeLabel("Move:"),
	    Child, help_unit_move_text = TextObject, End,
	    Child, MakeLabel("Vision:"),
	    Child, help_unit_vision_text = TextObject, End,
	    Child, MakeLabel("Firepower:"),
	    Child, help_unit_firepower_text = TextObject, End,
	    Child, MakeLabel("Hitpoints:"),
	    Child, help_unit_hitpoints_text = TextObject, End,
	    Child, MakeLabel("Basic Upkeep: "),
	    Child, help_unit_basic_upkeep_text = TextObject, End,
	    Child, HSpace(0),
	    Child, HSpace(0),
	    Child, MakeLabel("Needs:"),
	    Child, help_unit_needs_button = MakeButton(""),
	    Child, MakeLabel("Obsolete by: "),
	    Child, help_unit_obsolete_button = MakeButton(""),
	    End,
	End;
      if (help_page_group)
      {
	DoMethod(help_unit_needs_button, MUIM_Notify, MUIA_Pressed, FALSE, app, 4, MUIM_CallHook, &standart_hook, help_hyperlink, help_unit_needs_button);
	DoMethod(help_unit_obsolete_button, MUIM_Notify, MUIA_Pressed, FALSE, app, 4, MUIM_CallHook, &standart_hook, help_hyperlink, help_unit_obsolete_button);
      }
      break;
    }

    if (help_page_group)
    {
      DoMethod(help_right_group, OM_ADDMEMBER, help_page_group);
      DoMethod(help_right_group, MUIM_Group_Sort, help_page_group, help_text_listview, NULL);
    }
    DoMethod(help_right_group, MUIM_Group_ExitChange);

    help_last_page = type;
  }
}

/**************************************************************************
...
**************************************************************************/
static void help_update_improvement(const struct help_item *pitem,
				    char *title, int which)
{
  char *buf = &long_buffer[0];

  create_help_page(HELP_IMPROVEMENT);

  if (which < B_LAST)
  {
    char *text;
    struct improvement_type *imp = &improvement_types[which];

    DoMethod(help_imprv_cost_text, MUIM_SetAsString, MUIA_Text_Contents, "%ld", imp->build_cost);
    DoMethod(help_imprv_upkeep_text, MUIM_SetAsString, MUIA_Text_Contents, "%ld", imp->shield_upkeep);
    if (imp->tech_requirement == A_LAST)
      text = "(Never)";
    else
      text = advances[imp->tech_requirement].name;
    set(help_imprv_needs_button, MUIA_Text_Contents, text);
  }

  helptext_improvement(buf, which, pitem->text);
  DoMethod(help_text_listview, MUIM_NList_Insert, buf, -2, MUIV_List_Insert_Bottom);
}

/**************************************************************************
...
**************************************************************************/
static void help_update_wonder(const struct help_item *pitem,
			       char *title, int which)
{
  char *buf = &long_buffer[0];

  create_help_page(HELP_WONDER);

  if (which < B_LAST)
  {
    char *text;
    struct improvement_type *imp = &improvement_types[which];

    DoMethod(help_wonder_cost_text, MUIM_SetAsString, MUIA_Text_Contents, "%ld", imp->build_cost);

    if (imp->tech_requirement == A_LAST)
      text = "(Never)";
    else
      text = advances[imp->tech_requirement].name;
    set(help_wonder_needs_button, MUIA_Text_Contents, text);
    set(help_wonder_obsolete_button, MUIA_Text_Contents, advances[imp->obsolete_by].name);
  }
  else
  {
    set(help_wonder_cost_text, MUIA_Text_Contents, "0");
    set(help_wonder_needs_button, MUIA_Text_Contents, "(Never)");
    set(help_wonder_obsolete_button, MUIA_Text_Contents, "None");
  }

  helptext_wonder(buf, which, pitem->text);
  DoMethod(help_text_listview, MUIM_NList_Insert, buf, -2, MUIV_List_Insert_Bottom);
}

/**************************************************************************
...
**************************************************************************/
static void help_update_unit_type(const struct help_item *pitem,
				  char *title, int i)
{
  char *buf = &long_buffer[0];

  create_help_page(HELP_UNIT);

  if (i < game.num_unit_types)
  {
    struct unit_type *utype = get_unit_type(i);
    char *text;

    settextf(help_unit_cost_text, "Cost: %ld", utype->build_cost);
    DoMethod(help_unit_attack_text, MUIM_SetAsString, MUIA_Text_Contents, "%ld", utype->attack_strength);
    DoMethod(help_unit_defense_text, MUIM_SetAsString, MUIA_Text_Contents, "%ld", utype->defense_strength);
    DoMethod(help_unit_move_text, MUIM_SetAsString, MUIA_Text_Contents, "%ld", utype->move_rate / 3);
    DoMethod(help_unit_firepower_text, MUIM_SetAsString, MUIA_Text_Contents, "%ld", utype->firepower);
    settextf(help_unit_hitpoints_text, "%ld", utype->hp);
    settextf(help_unit_vision_text, "%ld", utype->vision_range);
    set(help_unit_basic_upkeep_text, MUIA_Text_Contents, helptext_unit_upkeep_str(i));

    if (utype->tech_requirement == A_LAST)
      text = "(Never)";
    else
      text = advances[utype->tech_requirement].name;
    SetAttrs(help_unit_needs_button,
	     MUIA_Text_Contents, text,
	     MUIA_UserData, HELP_TECH,
	     TAG_DONE);

    if (utype->obsoleted_by == -1)
      text = "None";
    else
      text = get_unit_type(utype->obsoleted_by)->name;
    SetAttrs(help_unit_obsolete_button,
	     MUIA_Text_Contents, text,
	     MUIA_UserData, HELP_UNIT,	//utype->obsoleted_by,
	      TAG_DONE);

    set(help_unit_sprite, MUIA_Sprite_Sprite, get_unit_type(i)->sprite);

    helptext_unit(buf, i, pitem->text);
    DoMethod(help_text_listview, MUIM_NList_Insert, buf, -2, MUIV_List_Insert_Bottom);
  }
}

/**************************************************************************
...
**************************************************************************/
static void help_update_tech(const struct help_item *pitem, char *title, int i)
{
  char *buf = &long_buffer[0];

  create_help_page(HELP_TECH);

  if (i < game.num_tech_types && i != A_NONE)
  {
    if (help_tech_group)
    {
      int j;
      DoMethod(help_right_group, MUIM_Group_InitChange);
      DoMethod(help_page_group, OM_REMMEMBER, help_tech_group);
      MUI_DisposeObject(help_tech_group);

      /* Update the tech tree */
      DoMethod(help_tech_tree, MUIM_ObjectTree_Clear);
      create_tech_tree(help_tech_tree,NULL,i, 3);

      help_tech_group = VGroup,
	Child, HSpace(0),
	End;

      if (help_tech_group)
      {
	for (j = 0; j < B_LAST; ++j)
	{
	  Object *o, *button;
	  if (i != improvement_types[j].tech_requirement)
	    continue;

	  o = HGroup,
	    GroupSpacing(0),
	    Child, MakeLabel("Allows "),
	    Child, button = MakeHelpButton(improvement_types[j].name, is_wonder(j) ? HELP_WONDER : HELP_IMPROVEMENT),
	    Child, is_wonder(j) ? MakeLabel(" wonder") : MakeLabel(" improvement"),
	    Child, HSpace(0),
	    End;

	  if (o)
	    DoMethod(help_tech_group, OM_ADDMEMBER, o);
	}

	for (j = 0; j < game.num_unit_types; ++j)
	{
	  Object *o, *button;
	  if (i != get_unit_type(j)->tech_requirement)
	    continue;

	  o = HGroup,
	    GroupSpacing(0),
	    Child, MakeLabel("Allows "),
	    Child, button = MakeHelpButton(get_unit_type(j)->name, HELP_UNIT),
	    Child, MakeLabel(" unit"),
	    Child, HSpace(0),
	    End;

	  if (o)
	    DoMethod(help_tech_group, OM_ADDMEMBER, o);
	}


	for (j = 0; j < game.num_tech_types; ++j)
	{
	  Object *o, *button;
	  if (i == advances[j].req[0])
	  {
	    Object *group;
	    group = o = HGroup,
	      GroupSpacing(0),
	      Child, MakeLabel("Allows "),
	      Child, button = MakeHelpButton(advances[j].name, HELP_TECH),
	      End;

	    if (o)
	      DoMethod(help_tech_group, OM_ADDMEMBER, o);

	    if (advances[j].req[1] != A_NONE)
	    {
	      o = HGroup,
		GroupSpacing(0),
		Child, MakeLabel(" (with "),
		Child, button = MakeHelpButton(advances[advances[j].req[1]].name, HELP_TECH),
		Child, MakeLabel(")"),
		End;
	      if (o)
		DoMethod(group, OM_ADDMEMBER, o);
	    }

	    o = HSpace(0);
	    if (o)
	      DoMethod(group, OM_ADDMEMBER, o);
	  }

	  if (i == advances[j].req[1])
	  {
	    Object *button2;
	    o = HGroup,
	      GroupSpacing(0),
	      Child, MakeLabel("Allows "),
	      Child, button = MakeHelpButton(advances[j].name, HELP_TECH),
	      Child, MakeLabel(" (with "),
	      Child, button2 = MakeHelpButton(advances[advances[j].req[0]].name, HELP_TECH),
	      Child, MakeLabel(")"),
	      Child, HSpace(0),
	      End;

	    if (o)
	      DoMethod(help_tech_group, OM_ADDMEMBER, o);
	  }
	}
	DoMethod(help_page_group, OM_ADDMEMBER, help_tech_group);
	DoMethod(help_page_group, MUIM_Group_Sort, help_tech_group, help_tech_tree, NULL);
      }

      DoMethod(help_right_group, MUIM_Group_ExitChange);
    }

    helptext_tech(buf, i, pitem->text);
    DoMethod(help_text_listview, MUIM_NList_Insert, buf, -2, MUIV_List_Insert_Bottom);
  }
}

/**************************************************************************
...
**************************************************************************/
static void help_update_terrain(const struct help_item *pitem,
				char *title, int i)
{
  char *buf = &long_buffer[0];

  create_help_page(HELP_TERRAIN);

  if (i < T_COUNT)
  {
    struct tile_type *tile = get_tile_type(i);

    settextf(help_terrain_move_text, "Move: %ld", tile_types[i].movement_cost);
    settextf(help_terrain_defense_text, "Defense: %ld.%ld", (tile_types[i].defense_bonus / 10), tile_types[i].defense_bonus % 10);

    settextf(help_terrain_food_text, "Food: %ld", tile->food);
    settextf(help_terrain_prod_text, "Prod: %ld", tile->shield);
    settextf(help_terrain_trade_text, "Trade: %ld", tile->trade);

    set(help_terrain_sprite, MUIA_Sprite_Sprite, tile->sprite[NUM_DIRECTION_NSEW - 1]);
  }

  helptext_terrain(buf, i, pitem->text);

  DoMethod(help_text_listview, MUIM_NList_Insert, buf, -2, MUIV_List_Insert_Bottom);
}

/**************************************************************************
  This is currently just a text page, with special text:
**************************************************************************/
static void help_update_government(const struct help_item *pitem,
				   char *title, struct government *gov)
{
  char *buf = &long_buffer[0];

  create_help_page(HELP_TEXT);

  if (gov == NULL)
  {
    strcat(buf, pitem->text);
  }
  else
  {
    helptext_government(buf, gov - governments, pitem->text);
  }

  DoMethod(help_text_listview, MUIM_NList_Insert, buf, -2, MUIV_List_Insert_Bottom);
}

/**************************************************************************
...
**************************************************************************/
static void help_update_dialog(const struct help_item *pitem)
{
  int i;
  char *top;

  set(help_text_listview, MUIA_NList_Quiet, TRUE);
  DoMethod(help_text_listview, MUIM_NList_Clear);

  /* figure out what kind of item is required for pitem ingo */

  for (top = pitem->topic; *top == ' '; ++top);

  switch (pitem->type)
  {
  case HELP_IMPROVEMENT:
    i = find_improvement_by_name(top);
    if (i != B_LAST && is_wonder(i))
      i = B_LAST;
    help_update_improvement(pitem, top, i);
    break;

  case HELP_WONDER:
    i = find_improvement_by_name(top);
    if (i != B_LAST && !is_wonder(i))
      i = B_LAST;
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
    create_help_page(HELP_TEXT);
    DoMethod(help_text_listview, MUIM_NList_Insert, pitem->text, -2, MUIV_List_Insert_Bottom);
    break;
  }

  set(help_text_listview, MUIA_NList_Quiet, FALSE);
  set_title_topic(pitem->topic);
}

/**************************************************************************
...
**************************************************************************/
static void select_help_item(int item)
{
  set(help_topic_listview, MUIA_NList_Active, item);
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
  if (idx == -1)
    idx = 0;
  select_help_item(idx);
}
