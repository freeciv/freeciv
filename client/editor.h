/**********************************************************************
 Freeciv - Copyright (C) 2005 - The Freeciv Poject
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
***********************************************************************/

#ifndef FC__TOOLS_H
#define FC__TOOLS_H

#include "fc_types.h"

/* See client/gui-gtk-2.0/editprop.c for instructions
 * on how to add more object types. */
enum editor_object_type {
  OBJTYPE_TILE,
  OBJTYPE_UNIT,
  OBJTYPE_CITY,
  OBJTYPE_PLAYER,
  OBJTYPE_GAME,
  
  NUM_OBJTYPES
};

enum editor_tool_type {
  ETT_TERRAIN = 0,
  ETT_TERRAIN_RESOURCE,
  ETT_TERRAIN_SPECIAL,
  ETT_MILITARY_BASE,
  ETT_UNIT,
  ETT_CITY,
  ETT_VISION,
  ETT_TERRITORY,
  ETT_STARTPOS,

  NUM_EDITOR_TOOL_TYPES
};

void editor_init(void);
bool editor_is_active(void);
enum editor_tool_type editor_get_tool(void);
void editor_set_tool(enum editor_tool_type emt);
bool editor_get_erase_mode(void);
void editor_set_erase_mode(bool mode);
const struct tile *editor_get_current_tile(void);
void editor_set_current_tile(const struct tile *ptile);
int editor_get_size(void);
void editor_set_size(int size);
int editor_get_count(void);
void editor_set_count(int count);
int editor_get_applied_player(void);
void editor_set_applied_player(int player_no);

bool editor_tool_has_value(enum editor_tool_type ett);
bool editor_tool_has_size(enum editor_tool_type ett);
bool editor_tool_has_count(enum editor_tool_type ett);
bool editor_tool_has_applied_player(enum editor_tool_type ett);
bool editor_tool_has_value_erase(enum editor_tool_type ett);

int editor_tool_get_value(enum editor_tool_type ett);
void editor_tool_set_value(enum editor_tool_type ett, int value);
const char *editor_tool_get_value_name(enum editor_tool_type ett, int value);

const char *editor_tool_get_name(enum editor_tool_type ett);
struct sprite *editor_tool_get_sprite(enum editor_tool_type ett);
const char *editor_tool_get_tooltip(enum editor_tool_type ett);

enum editor_keyboard_modifiers {
  EKM_NONE  = 0,
  EKM_SHIFT = 1<<0,
  EKM_ALT   = 1<<1,
  EKM_CTRL  = 1<<2
};

enum mouse_button_values {
  MOUSE_BUTTON_OTHER = 0,

  MOUSE_BUTTON_LEFT = 1,
  MOUSE_BUTTON_MIDDLE = 2,
  MOUSE_BUTTON_RIGHT = 3
};

void editor_mouse_button_press(int canvas_x, int canvas_y,
                               int button, int modifiers);
void editor_mouse_button_release(int canvas_x, int canvas_y,
                                 int button, int modifiers);
void editor_mouse_move(int canvas_x, int canvas_y, int modifiers);
void editor_redraw(void);

void editor_apply_tool(const struct tile *ptile,
                       bool part_of_selection);
void editor_notify_edit_finished(void);
void editor_toggle_erase_mode(void);

void editor_selection_clear(void);
void editor_selection_add(const struct tile *ptile);
void editor_selection_remove(const struct tile *ptile);
bool editor_tile_is_selected(const struct tile *ptile);
void editor_apply_tool_to_selection(void);
int editor_selection_count(void);

#endif /* FC__TOOLS_H */
