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

enum editor_tool_type {
  ETOOL_PAINT,
  ETOOL_UNIT,
  ETOOL_CITY,
  ETOOL_PLAYER,
  ETOOL_VISION,
  ETOOL_DELETE,
  ETOOL_LAST
};

enum editor_paint_type {
  EPAINT_TERRAIN = 0,
  EPAINT_SPECIAL,
  EPAINT_RESOURCE,
  EPAINT_LAST
};

typedef void (*ToolFunction)(struct tile *ptile);

void editor_init_tools(void);
void editor_show_tools(void);

void editor_set_selected_tool_type(enum editor_tool_type type);
void editor_set_selected_paint_type(enum editor_paint_type type);
void editor_set_selected_terrain(struct terrain *pterrain);
void editor_set_selected_special(enum tile_special_type special);
void editor_set_selected_resource(struct resource *presource);
void editor_set_selected_player(struct player *pplayer);
void editor_set_vision_mode(enum editor_vision_mode mode);

struct unit *editor_get_selected_unit(void);
struct city *editor_get_selected_city(void);

bool can_do_editor_click(struct tile *ptile);
void editor_do_click(struct tile *ptile);
enum cursor_type editor_test_click(struct tile *ptile);

#endif /* FC__TOOLS_H */
