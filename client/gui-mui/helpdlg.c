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
#include <string.h>

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
#include "support.h"
#include "tech.h"
#include "unit.h"
#include "map.h"
#include "version.h"

#include "climisc.h"
#include "colors.h"
#include "graphics.h"
#include "gui_main.h"
#include "helpdata.h"
#include "helpdlg.h"
#include "tilespec.h"

#include "colortextclass.h"
#include "mapclass.h"
#include "objecttreeclass.h"
#include "muistuff.h"

static Object *help_wnd;
static Object *help_topic_listview;
static Object *help_topic_text;
static Object *help_text_listview;
static Object *help_right_group;

static Object *help_page_group;
static Object *help_page_balance;

static Object *help_imprv_cost_text;
static Object *help_imprv_upkeep_text;
static Object *help_imprv_needs_button;	/* tech */

static Object *help_wonder_cost_text;
static Object *help_wonder_needs_button;	/* tech */
static Object *help_wonder_obsolete_button;	/* tech */

static Object *help_tech_group;	/* Allows tech with tech */
static Object *help_tech_scrollgroup;   /* the Scrollgroup Object */
static Object *help_tech_tree;	/* the tree of techs (inside the SgO) */

static Object *help_unit_sprite;
static Object *help_unit_cost_text;
static Object *help_unit_attack_text;
static Object *help_unit_defense_text;
static Object *help_unit_move_text;
static Object *help_unit_vision_text;
static Object *help_unit_firepower_text;
static Object *help_unit_hitpoints_text;
static Object *help_unit_basic_upkeep_text;
static Object *help_unit_needs_button;
static Object *help_unit_obsolete_button;

static Object *help_terrain_move_text;
static Object *help_terrain_defense_text;
static Object *help_terrain_dynamic_group; /* the tile types */
static Object *help_terrain_static_group;

static enum help_page_type help_last_page;

static void clear_page_objects(void);
static void free_help_page(void);
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

  if (strcmp(topic, "Freeciv") == 0 || strcmp(topic, "About") == 0
      || strcmp(topic, _("About")) == 0) {
    text = freeciv_name_version();
  } else {
    text = topic;
  }

  set(help_topic_text, MUIA_Text_Contents, text);
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
  char *s = (char *) xget(*text_obj, MUIA_Text_Contents); /* works also for ColorButtons */
  enum help_page_type type = (enum help_page_type) xget(*text_obj, MUIA_UserData);

  if (strcmp(s, _("(Never)")) != 0 && strcmp(s, _("None")) != 0
      && strcmp(s, advances[A_NONE].name) != 0)
  {
    int idx;

    get_help_item_spec(s, type, &idx);
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
    DoMethod(button, MUIM_Notify, MUIA_Pressed, FALSE, app, 4, MUIM_CallHook, &civstandard_hook, help_hyperlink, button);
  }
  return button;
}

/****************************************************************
 Returns a tech buttons background color
*****************************************************************/
ULONG GetTechBG(int tech)
{
  ULONG bg;

  switch(get_invention(game.player_ptr, tech))
  {
  case TECH_KNOWN:
    bg = GetColorRGB(COLOR_MUI_TECHKNOWN); /* green */
    break;
  case TECH_REACHABLE:
    bg = GetColorRGB(COLOR_MUI_TECHREACHABLE); /* yellow */
    break;
  case TECH_UNKNOWN:
    bg = GetColorRGB(COLOR_MUI_TECHUNKNOWN); /* red */
    break;
  default:
    bg = GetColorRGB(COLOR_STD_WHITE); /* white */
  }
  return bg;
}

#define TECHTYPE_NOTSET	A_LAST+2
#define TECHTYPE_NONE		A_LAST+1

/****************************************************************
 Returns a tech buttons text
*****************************************************************/
static char *GetTechText(int tech)
{
  char *text;

  if(tech == A_LAST)
    text = _("(Never)");
  else if(tech == TECHTYPE_NONE)
    text = _("None");
  else
    text = advances[tech].name;

  return text;
}

/****************************************************************
 Creates a tech button
*****************************************************************/
static Object *MakeTechButton(int tech)
{
  return ColorTextObject,
    ButtonFrame,
    MUIA_UserData, HELP_TECH,
    tech == TECHTYPE_NOTSET ? TAG_IGNORE : MUIA_ColorText_Background, GetTechBG(tech),
    tech == TECHTYPE_NOTSET ? TAG_IGNORE : MUIA_ColorText_Contents, GetTechText(tech),
    MUIA_ColorText_Align, MUIV_ColorText_Align_Center,
    MUIA_InputMode, MUIV_InputMode_RelVerify,
    End;
}

/****************************************************************
 Creates a tech help button
*****************************************************************/
static Object *MakeHelpButtonTech(int tech)
{
  Object *button;

  if((button = MakeTechButton(tech)))
  {
    set(button,MUIA_Weight,0);
    DoMethod(button, MUIM_Notify, MUIA_Pressed, FALSE, app, 4, MUIM_CallHook, &civstandard_hook, help_hyperlink, button);
  }

  return button;
}

/****************************************************************
 Update a tech button
*****************************************************************/
static void UpdateTechButton(Object *o, int tech)
{
  SetAttrs(o,
    MUIA_ColorText_Background, GetTechBG(tech),
    MUIA_ColorText_Contents, GetTechText(tech),
    TAG_DONE);
}

/****************************************************************
...
*****************************************************************/
void popup_help_dialog_typed(const char *item, enum help_page_type htype)
{
  if (!help_wnd)
  {
    help_wnd = WindowObject,
      MUIA_Window_Title, _("Help"),
      MUIA_Window_ID, MAKE_ID('H','E','L','P'),

      WindowContents, HGroup,
          Child, help_topic_listview = NListviewObject,
              MUIA_Weight, 50,
              MUIA_NListview_NList, NListObject,
                  MUIA_NList_ConstructHook, MUIV_NList_ConstructHook_String,
                  MUIA_NList_DestructHook, MUIV_NList_DestructHook_String,
                  End,
              End,
          Child, BalanceObject, End,
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
      DoMethod(help_topic_listview, MUIM_Notify, MUIA_NList_Active, MUIV_EveryTime, help_topic_listview, 4, MUIM_CallHook, &civstandard_hook, help_topic_active, MUIV_TriggerValue);

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
void popup_help_dialog_string(const char *item)
{
  popup_help_dialog_typed(_(item), HELP_ANY);
}


/****************************************************************
 Clear all pointers of objects which are displayed in
 a page
*****************************************************************/
static void clear_page_objects(void)
{
  help_page_group =
    help_page_balance =
    help_imprv_cost_text =
    help_imprv_upkeep_text =
    help_imprv_needs_button =
    help_wonder_cost_text =
    help_wonder_needs_button =
    help_wonder_obsolete_button =
    help_tech_group =
    help_tech_scrollgroup =
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
    help_terrain_dynamic_group =
    help_terrain_static_group = NULL;
}

/****************************************************************
 Create the tech tree
*****************************************************************/
static void create_tech_tree(Object *tree, APTR parent, int tech, int levels)
{
  Object *o;
  APTR leaf = 0;

  char label[MAX_LEN_NAME+3];
  if(!tech_exists(tech))
  {
    Object *o = MakeButton(_("Removed"));
    DoMethod(tree, MUIM_ObjectTree_AddNode,NULL,o);
    return;
  }
  
  my_snprintf(label, sizeof(label), "%s:%d", advances[tech].name,
	      num_unknown_techs_for_goal(game.player_ptr, tech));

  o = ColorTextObject,
      ButtonFrame,
      MUIA_UserData, tech,
      MUIA_Weight, 0,
      MUIA_ColorText_Background, GetTechBG(tech),
      MUIA_ColorText_Contents, label,
      MUIA_InputMode, MUIV_InputMode_RelVerify,
      MUIA_CycleChain, 1,
      End;

  if(o)
  {
    DoMethod(o, MUIM_Notify, MUIA_Pressed, FALSE, app, 7, MUIM_Application_PushMethod, app, 4, MUIM_CallHook, &civstandard_hook, help_tree_leaf, o);
    leaf = (APTR)DoMethod(tree, MUIM_ObjectTree_AddNode,parent,o);
  }
  
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
  if (help_page_balance)
  {
    DoMethod(help_right_group, OM_REMMEMBER, help_page_balance);
    MUI_DisposeObject(help_page_balance);
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
	Child, MakeLabel(_("Cost:")),
	Child, help_imprv_cost_text = TextObject, End,
	Child, MakeLabel(_("Upkeep:")),
	Child, help_imprv_upkeep_text = TextObject, End,
	Child, MakeLabel(_("Requirement:")),
	Child, help_imprv_needs_button = MakeTechButton(TECHTYPE_NOTSET),
	End;

      if (help_page_group)
      {
	DoMethod(help_imprv_needs_button, MUIM_Notify, MUIA_Pressed, FALSE, app, 4, MUIM_CallHook, &civstandard_hook, help_hyperlink, help_imprv_needs_button);
      }
      break;

    case HELP_WONDER:
      help_page_group = HGroup,
	Child, MakeLabel(_("Cost:")),
	Child, help_wonder_cost_text = TextObject, End,
	Child, MakeLabel(_("Requirement:")),
	Child, help_wonder_needs_button = MakeTechButton(TECHTYPE_NOTSET),
	Child, MakeLabel(_("Obsolete by:")),
	Child, help_wonder_obsolete_button = MakeTechButton(TECHTYPE_NOTSET),
	End;

      if (help_page_group)
      {
	DoMethod(help_wonder_needs_button, MUIM_Notify, MUIA_Pressed, FALSE, app, 4, MUIM_CallHook, &civstandard_hook, help_hyperlink, help_wonder_needs_button);
	DoMethod(help_wonder_obsolete_button, MUIM_Notify, MUIA_Pressed, FALSE, app, 4, MUIM_CallHook, &civstandard_hook, help_hyperlink, help_wonder_obsolete_button);
      }
      break;

    case HELP_TERRAIN:
      help_page_group = VGroup,
	Child, help_terrain_static_group = HGroup,
	    Child, help_terrain_move_text = TextObject, MUIA_Text_PreParse, "\33c", End,
	    Child, help_terrain_defense_text = TextObject, MUIA_Text_PreParse, "\33c", End,
	    End,
	End;
      break;

    case HELP_TECH:
      help_page_group = VGroup,
	Child, help_tech_group = VGroup,
	    Child, HSpace(0),
	    End,
	Child, help_tech_scrollgroup = ScrollgroupObject,
	    MUIA_Scrollgroup_Contents, help_tech_tree = ObjectTreeObject, VirtualFrame, End,
	    End,
	End;
      help_page_balance = BalanceObject,End;
      break;

    case HELP_UNIT:
      help_page_group = VGroup,
	Child, HGroup,
	    Child, HSpace(0),
	    Child, help_unit_sprite = SpriteObject,
		MUIA_FixWidth, UNIT_TILE_WIDTH,
		MUIA_FixHeight, UNIT_TILE_HEIGHT,
		MUIA_Sprite_Transparent, TRUE,
		End,
	    Child, HSpace(0),
	    End,
	Child, HGroup,
	    Child, help_unit_cost_text = TextObject, MUIA_Text_PreParse, "\33c", End,
	    End,
	Child, ColGroup(4),
	    Child, MakeLabel(_("Attack:")),
	    Child, help_unit_attack_text = TextObject, End,
	    Child, MakeLabel(_("Defense:")),
	    Child, help_unit_defense_text = TextObject, End,
	    Child, MakeLabel(_("Move:")),
	    Child, help_unit_move_text = TextObject, End,
	    Child, MakeLabel(_("Vision:")),
	    Child, help_unit_vision_text = TextObject, End,
	    Child, MakeLabel(_("Firepower:")),
	    Child, help_unit_firepower_text = TextObject, End,
	    Child, MakeLabel(_("Hitpoints:")),
	    Child, help_unit_hitpoints_text = TextObject, End,
	    Child, MakeLabel(_("Basic Upkeep:")),
	    Child, help_unit_basic_upkeep_text = TextObject, End,
	    Child, HSpace(0),
	    Child, HSpace(0),
	    Child, MakeLabel(_("Requirement:")),
	    Child, help_unit_needs_button = MakeTechButton(TECHTYPE_NOTSET),
	    Child, MakeLabel(_("Obsolete by:")),
	    Child, help_unit_obsolete_button = MakeButton(""),
	    End,
	End;
      if (help_page_group)
      {
	DoMethod(help_unit_needs_button, MUIM_Notify, MUIA_Pressed, FALSE, app, 4, MUIM_CallHook, &civstandard_hook, help_hyperlink, help_unit_needs_button);
	DoMethod(help_unit_obsolete_button, MUIM_Notify, MUIA_Pressed, FALSE, app, 4, MUIM_CallHook, &civstandard_hook, help_hyperlink, help_unit_obsolete_button);
      }
      break;
    }

    if (help_page_group)
    {
      DoMethod(help_right_group, OM_ADDMEMBER, help_page_group);

      if (help_page_balance)
      {
      	DoMethod(help_right_group, OM_ADDMEMBER, help_page_balance);
      	DoMethod(help_right_group, MUIM_Group_Sort, help_page_group, help_page_balance, help_text_listview, NULL);
      } else
      {
      	DoMethod(help_right_group, MUIM_Group_Sort, help_page_group, help_text_listview, NULL);
      }
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
  char buf[64000];

  create_help_page(HELP_IMPROVEMENT);

  if (which < game.num_impr_types)
  {
    struct impr_type *imp = &improvement_types[which];

    DoMethod(help_imprv_cost_text, MUIM_SetAsString,
	     MUIA_Text_Contents, "%ld", impr_build_shield_cost(which));
    DoMethod(help_imprv_upkeep_text, MUIM_SetAsString,
	     MUIA_Text_Contents, "%ld", imp->upkeep);
    UpdateTechButton(help_imprv_needs_button, imp->tech_req);
  }

  helptext_building(buf, sizeof(buf), which, pitem->text);
  DoMethod(help_text_listview, MUIM_NList_Insert, buf, -2, MUIV_List_Insert_Bottom);
}

/**************************************************************************
...
**************************************************************************/
static void help_update_wonder(const struct help_item *pitem,
			       char *title, int which)
{
  char buf[64000];

  create_help_page(HELP_WONDER);

  if (which < game.num_impr_types)
  {
    struct impr_type *imp = &improvement_types[which];

    DoMethod(help_wonder_cost_text, MUIM_SetAsString,
	     MUIA_Text_Contents, "%ld", impr_build_shield_cost(which));

    UpdateTechButton(help_wonder_needs_button, imp->tech_req);
    UpdateTechButton(help_wonder_obsolete_button, imp->obsolete_by);
  }
  else
  {
    set(help_wonder_cost_text, MUIA_Text_Contents, "0");
    set(help_wonder_needs_button, MUIA_Text_Contents, A_LAST);
    set(help_wonder_obsolete_button, MUIA_Text_Contents, TECHTYPE_NONE);
  }

  helptext_building(buf, sizeof(buf), which, pitem->text);
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
    ULONG bg_color;
    struct unit_type *utype = get_unit_type(i);
    char *text;

    settextf(help_unit_cost_text,
	     _("Cost: %ld"), unit_build_shield_cost(which));
    DoMethod(help_unit_attack_text, MUIM_SetAsString,
	     MUIA_Text_Contents, "%ld", utype->attack_strength);
    DoMethod(help_unit_defense_text, MUIM_SetAsString,
	     MUIA_Text_Contents, "%ld", utype->defense_strength);
    DoMethod(help_unit_move_text, MUIM_SetAsString,
	     MUIA_Text_Contents, "%ld", utype->move_rate / 3);
    DoMethod(help_unit_firepower_text, MUIM_SetAsString,
	     MUIA_Text_Contents, "%ld", utype->firepower);
    settextf(help_unit_hitpoints_text, "%ld", utype->hp);
    settextf(help_unit_vision_text, "%ld", utype->vision_range);
    set(help_unit_basic_upkeep_text, MUIA_Text_Contents,
	helptext_unit_upkeep_str(i));

    UpdateTechButton(help_unit_needs_button, utype->tech_requirement);

    if (utype->obsoleted_by == -1)
      text = _("None");
    else
      text = get_unit_type(utype->obsoleted_by)->name;
    SetAttrs(help_unit_obsolete_button,
	     MUIA_Text_Contents, text,
	     MUIA_UserData, HELP_UNIT,
	      TAG_DONE);

    switch (get_unit_type(i)->move_type)
    {
      case LAND_MOVING: bg_color = 0x0000c800; /* green */ break;
      case SEA_MOVING:  bg_color = 0x000000c8; /* blue */ break;
      case HELI_MOVING: bg_color = 0x00ffff00; /* yellow */ break;
      case AIR_MOVING:  bg_color = 0x0000ffc8; /* cyan */ break;
      default:          bg_color = 0x00000000; /* black */ break;
    }

    SetAttrs(help_unit_sprite,
             MUIA_Sprite_Sprite, get_unit_type(i)->sprite,
             MUIA_Sprite_Background, bg_color,
             TAG_DONE);

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

  if (!is_future_tech(i))
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
	impr_type_iterate(j) {
	  Object *o, *button;
	  if (i != improvement_types[j].tech_req)
	    continue;

	  o = HGroup,
	    GroupSpacing(0),
	    Child, MakeLabel(_("Allows ")),
	    Child, button = MakeHelpButton(improvement_types[j].name, is_wonder(j) ? HELP_WONDER : HELP_IMPROVEMENT),
	    Child, is_wonder(j) ? MakeLabel(_(" wonder")) : MakeLabel(_(" improvement")),
	    Child, HSpace(0),
	    End;

	  if (o)
	    DoMethod(help_tech_group, OM_ADDMEMBER, o);
	} impr_type_iterate_end;

	unit_type_iterate(j) {
	  Object *o, *button;
	  if (i != get_unit_type(j)->tech_requirement)
	    continue;

	  o = HGroup,
	    GroupSpacing(0),
	    Child, MakeLabel(_("Allows ")),
	    Child, button = MakeHelpButton(get_unit_type(j)->name, HELP_UNIT),
	    Child, MakeLabel(_(" unit")),
	    Child, HSpace(0),
	    End;

	  if (o)
	    DoMethod(help_tech_group, OM_ADDMEMBER, o);
	} unit_type_iterate_end;


	for (j = 0; j < game.num_tech_types; j++) {
	  Object *o, *button;
	  if (i == advances[j].req[0])
	  {
	    Object *group;
	    group = o = HGroup,
	      GroupSpacing(0),
	      Child, MakeLabel(_("Allows ")),
	      Child, button = MakeHelpButtonTech(j),
	      End;

	    if (o)
	      DoMethod(help_tech_group, OM_ADDMEMBER, o);

	    if (advances[j].req[1] != A_NONE)
	    {
	      o = HGroup,
		GroupSpacing(0),
		Child, MakeLabel(_(" (with ")),
		Child, button = MakeHelpButtonTech(advances[j].req[1]),
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
	      Child, MakeLabel(_("Allows ")),
	      Child, button = MakeHelpButtonTech(j),
	      Child, MakeLabel(_(" (with ")),
	      Child, button2 = MakeHelpButtonTech(advances[j].req[0]),
	      Child, MakeLabel(")"),
	      Child, HSpace(0),
	      End;

	    if (o)
	      DoMethod(help_tech_group, OM_ADDMEMBER, o);
	  }
	}
	DoMethod(help_page_group, OM_ADDMEMBER, help_tech_group);
	DoMethod(help_page_group, MUIM_Group_Sort, help_tech_group, help_tech_scrollgroup, NULL);
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
    char buf[256];
    Object *o,*g;

    settextf(help_terrain_move_text, _("Movecost: %ld"), tile_types[i].movement_cost);
    settextf(help_terrain_defense_text, _("Defense: %ld.%ld"), (tile_types[i].defense_bonus / 10), tile_types[i].defense_bonus % 10);

    DoMethod(help_right_group, MUIM_Group_InitChange);
    DoMethod(help_page_group, MUIM_Group_InitChange);
    if(help_terrain_dynamic_group)
    {
      DoMethod(help_page_group, OM_REMMEMBER, help_terrain_dynamic_group);
      MUI_DisposeObject(help_terrain_dynamic_group);
    }

    help_terrain_dynamic_group = VGroup,End;

    my_snprintf(buf,sizeof(buf),_("Food:   %d\nShield: %d\nTrade:  %d"),tile->food, tile->shield, tile->trade);
    if((o = HGroup,
              Child, HSpace(0),
	      Child, TextObject, MUIA_Text_Contents, "", End,
	      Child, SpriteObject, MUIA_Sprite_Sprite, tile->sprite[NUM_DIRECTION_NSEW - 1],
	        MUIA_FixWidth, NORMAL_TILE_WIDTH,
	        MUIA_FixHeight, NORMAL_TILE_HEIGHT,
	      	End,
	      Child, TextObject, MUIA_Text_Contents, buf, End,
              Child, HSpace(0),
	      End))
    {
      DoMethod(help_terrain_dynamic_group, OM_ADDMEMBER, o);
    }

    g = HGroup, Child, HSpace(0), End;

    my_snprintf(buf,sizeof(buf),_("Food:   %d\nShield: %d\nTrade:  %d"),tile->food_special_1, tile->shield_special_1,
    tile->trade_special_1);
    if((o = HGroup,
              Child, HSpace(0),
	      Child, TextObject, MUIA_Text_Contents, tile->special_1_name, End,
	      Child, SpriteObject,
		  MUIA_Sprite_Sprite, tile->sprite[NUM_DIRECTION_NSEW - 1],
		  MUIA_Sprite_OverlaySprite, tile->special[0].sprite,
		  MUIA_FixWidth, NORMAL_TILE_WIDTH,
		  MUIA_FixHeight, NORMAL_TILE_HEIGHT,
		  End,
	      Child, TextObject, MUIA_Text_Contents, buf, End,
              Child, HSpace(0),
	      End))
    {
      DoMethod(g, OM_ADDMEMBER, o);
    }

    my_snprintf(buf,sizeof(buf),_("Food:   %d\nShield: %d\nTrade:  %d"),tile->food_special_2, tile->shield_special_2,
    tile->trade_special_2);
    if((o = HGroup,
              Child, HSpace(0),
	      Child, TextObject, MUIA_Text_Contents, tile->special_2_name, End,
	      Child, SpriteObject,
		  MUIA_Sprite_Sprite, tile->sprite[NUM_DIRECTION_NSEW - 1],
		  MUIA_Sprite_OverlaySprite, tile->special[1].sprite,
		  MUIA_FixWidth, NORMAL_TILE_WIDTH,
		  MUIA_FixHeight, NORMAL_TILE_HEIGHT,
		  End,
	      Child, TextObject, MUIA_Text_Contents, buf, End,
              Child, HSpace(0),
	      End))
    {
      DoMethod(g, OM_ADDMEMBER, o);
    }

    DoMethod(g, OM_ADDMEMBER, HSpace(0));
    DoMethod(help_terrain_dynamic_group, OM_ADDMEMBER, g);

    DoMethod(help_page_group, OM_ADDMEMBER, help_terrain_dynamic_group);
    DoMethod(help_page_group, MUIM_Group_Sort, help_terrain_dynamic_group, help_terrain_static_group, NULL);
    DoMethod(help_page_group, MUIM_Group_ExitChange);
    DoMethod(help_right_group, MUIM_Group_ExitChange);
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
    sz_strlcat(buf, pitem->text);
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

  for (top = pitem->topic; *top == ' '; top++) {
    /* nothing */
  }

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
  int idx;

  get_help_item_spec(item, htype, &idx);
  if (idx == -1)
    idx = 0;
  select_help_item(idx);
}
