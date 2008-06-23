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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <assert.h>
#include <stdarg.h>
#include <string.h>

#include "fcintl.h"
#include "log.h"
#include "support.h"

#include "map.h"
#include "movement.h"
#include "packets.h"

#include "civclient.h"
#include "climap.h"
#include "control.h"
#include "editor.h"
#include "mapctrl_common.h"
#include "mapview_common.h"
#include "tilespec.h"

#include "editgui_g.h"
#include "mapview_g.h"

BV_DEFINE(bv_map_selection, MAP_MAX_WIDTH * MAP_MAX_HEIGHT);

enum selection_modes {
  SELECTION_MODE_NEW = 0,
  SELECTION_MODE_ADD,
  SELECTION_MODE_REMOVE
};

enum editor_tool_flags {
  ETF_NO_FLAGS  = 0,
  ETF_HAS_VALUE = 1<<0,
  ETF_HAS_SIZE  = 1<<1,
  ETF_HAS_COUNT = 1<<2,
  ETF_HAS_APPLIED_PLAYER = 1<<3,
  ETF_HAS_VALUE_ERASE = 1<<4
};

struct editor_tool {
  const char *name;
  int flags;
  int value;
  const char *tooltip;
};

struct editor_state {
  bool erase;
  enum editor_tool_type tool;
  int size;
  int count;
  int applied_player_no;
  struct editor_tool tools[NUM_EDITOR_TOOL_TYPES];

  const struct tile *current_tile;
  bool tool_active;

  bool selrect_active;
  int selrect_start_x;
  int selrect_start_y;
  int selrect_x;
  int selrect_y;
  int selrect_width;
  int selrect_height;

  enum selection_modes selection_mode;

  bv_map_selection mapsel;
  int selection_count;
};

static struct editor_state *editor;

/****************************************************************************
  Initialize the client's editor state information to some suitable default
  values. This only needs to be done once at client start.
****************************************************************************/
void editor_init(void)
{
  if (editor != NULL) {
    return;
  }

  editor = fc_calloc(1, sizeof(struct editor_state));

#define SET_TOOL(ARG_ett, ARG_name, ARG_flags, ARG_tooltip) do {\
  editor->tools[(ARG_ett)].name = (ARG_name);\
  editor->tools[(ARG_ett)].flags = (ARG_flags);\
  editor->tools[(ARG_ett)].tooltip = ARG_tooltip;\
} while (0)

  SET_TOOL(ETT_TERRAIN, _("Terrain"),
           ETF_HAS_VALUE | ETF_HAS_SIZE,
           _("Change tile terrain."));
  SET_TOOL(ETT_TERRAIN_RESOURCE, _("Terrain Resource"),
           ETF_HAS_VALUE | ETF_HAS_SIZE,
           _("Change tile terrain resources."));
  SET_TOOL(ETT_TERRAIN_SPECIAL, _("Terrain Special"),
           ETF_HAS_VALUE | ETF_HAS_SIZE | ETF_HAS_VALUE_ERASE,
           _("Modify tile specials."));
  SET_TOOL(ETT_UNIT, _("Unit"),
           ETF_HAS_VALUE | ETF_HAS_COUNT | ETF_HAS_APPLIED_PLAYER,
           _("Create unit."));
  SET_TOOL(ETT_CITY, _("City"), ETF_HAS_SIZE | ETF_HAS_APPLIED_PLAYER,
           _("Create city."));
  SET_TOOL(ETT_VISION, _("Vision"), ETF_HAS_SIZE,
           _("Modify player's tile knowledge."));
  SET_TOOL(ETT_TERRITORY, _("Territory"), ETF_HAS_SIZE | ETF_HAS_APPLIED_PLAYER,
           _("Change tile ownership."));
  SET_TOOL(ETT_STARTPOS, _("Start Position"), ETF_NO_FLAGS | ETF_HAS_APPLIED_PLAYER,
           _("Place a player start position."));
#undef SET_TOOL

  editor_set_size(1);
  editor_set_count(1);
}

/****************************************************************************
  Set the current tool to be used by the editor.
****************************************************************************/
void editor_set_tool(enum editor_tool_type emt)
{
  if (editor == NULL) {
    return;
  }

  if (!(0 <= emt && emt < NUM_EDITOR_TOOL_TYPES)) {
    return;
  }

  editor->tool = emt;
}

/****************************************************************************
  Get the current tool used by the editor.
****************************************************************************/
enum editor_tool_type editor_get_tool(void)
{
  if (editor == NULL) {
    return NUM_EDITOR_TOOL_TYPES;
  }

  return editor->tool;
}

/****************************************************************************
  Set erase mode either on or off.
****************************************************************************/
void editor_set_erase_mode(bool mode)
{
  if (editor == NULL) {
    return;
  }
  editor->erase = mode;
}

/****************************************************************************
  Get the current erase mode.
****************************************************************************/
bool editor_get_erase_mode(void)
{
  if (editor == NULL) {
    return FALSE;
  }
  return editor->erase;
}

/****************************************************************************
  Returns TRUE if the *client* is in edit mode.
****************************************************************************/
bool editor_is_active(void)
{
  return can_conn_edit(&client.conn);
}

/****************************************************************************
  Returns TRUE if the given tool type has sub-values (e.g. the terrain
  tool has values corresponding to the terrain types).
****************************************************************************/
bool editor_tool_has_value(enum editor_tool_type ett)
{
  if (!editor || !(0 <= ett && ett < NUM_EDITOR_TOOL_TYPES)) {
    return FALSE;
  }
  return editor->tools[ett].flags & ETF_HAS_VALUE;
}

/****************************************************************************
  Set the value ID for the given tool. How the value is interpreted depends
  on the tool type.
****************************************************************************/
void editor_tool_set_value(enum editor_tool_type ett, int value)
{
  if (!editor || !(0 <= ett && ett < NUM_EDITOR_TOOL_TYPES)
      || !editor_tool_has_value(ett)) {
    return;
  }
  editor->tools[ett].value = value;
}

/****************************************************************************
  Get the current tool sub-value.
****************************************************************************/
int editor_tool_get_value(enum editor_tool_type ett)
{
  if (!editor || !(0 <= ett && ett < NUM_EDITOR_TOOL_TYPES)
      || !editor_tool_has_value(ett)) {
    return 0;
  }
  
  return editor->tools[ett].value;
}

/****************************************************************************
  Record the start of the selection rectangle.
****************************************************************************/
static void editor_start_selection_rectangle(int canvas_x, int canvas_y)
{
  if (!editor) {
    return;
  }

  if (editor->selection_mode == SELECTION_MODE_NEW
      && editor->selection_count > 0) {
    editor_selection_clear();
    update_map_canvas_visible();
  }

  editor->selrect_start_x = canvas_x;
  editor->selrect_start_y = canvas_y;
  editor->selrect_width = 0;
  editor->selrect_height = 0;
  editor->selrect_active = TRUE;
}

/****************************************************************************
  Temporary convenience function to work-around the fact that certain
  special values (S_RESOURCE_VALID and S_PILLAGE_BASE) do not in fact
  correspond to drawable special types, or are to be phased out soon
  (the base "specials").
****************************************************************************/
static inline bool tile_really_has_any_specials(const struct tile *ptile)
{
  int spe;
  bv_special specials;

  if (!ptile) {
    return FALSE;
  }

  specials = tile_specials(ptile);

  BV_CLR(specials, S_RESOURCE_VALID);
  BV_CLR(specials, S_PILLAGE_BASE);

  base_type_iterate(pbase) {
    spe = base_get_tile_special_type(pbase);
    if (!(0 <= spe && spe < S_LAST)) {
      continue;
    }
    BV_CLR(specials, spe);
  } base_type_iterate_end;

  return BV_ISSET_ANY(specials);
}

/****************************************************************************
  Set the editor's current applied player number according to what exists
  on the given tile.
****************************************************************************/
static void editor_grab_applied_player(const struct tile *ptile)
{
  int apno = -1;

  if (!editor || !ptile) {
    return;
  }

  if (client_has_player()
      && tile_get_known(ptile, client_player()) == TILE_UNKNOWN) {
    return;
  }

  if (tile_city(ptile) != NULL) {
    apno = player_number(city_owner(tile_city(ptile)));
  } else if (unit_list_size(ptile->units) > 0) {
    struct unit *punit = unit_list_get(ptile->units, 0);
    apno = player_number(unit_owner(punit));
  } else if (tile_owner(ptile) != NULL) {
    apno = player_number(tile_owner(ptile));
  }
  
  if (valid_player_by_number(apno) != NULL) {
    editor_set_applied_player(apno);
    editgui_refresh();
  }
}

/****************************************************************************
  Set the editor's current tool according to what exists at the given tile.
****************************************************************************/
static void editor_grab_tool(const struct tile *ptile)
{
  int ett = -1, value = 0;
  struct base_type *pbase;

  if (!editor) {
    return;
  }

  if (!ptile) {
    return;
  }

  pbase = tile_get_base(ptile);

  if (client_has_player()
      && tile_get_known(ptile, client_player()) == TILE_UNKNOWN) {
    ett = ETT_VISION;

  } else if (tile_city(ptile)) {
    ett = ETT_CITY;

  } else if (unit_list_size(ptile->units) > 0) {
    int max_score = 0, score;
    struct unit *grabbed_punit = NULL;
    unit_list_iterate(ptile->units, punit) {
      score = 0;
      if (is_air_unit(punit)) {
        score = 5;
      } else if (is_ground_unit(punit)) {
        score = 4;
      } else if (is_sailing_unit(punit)) {
        score = 3;
      } else {
        score = 2;
      }
      if (punit->transported_by > 0) {
        score = 1;
      }

      if (score > max_score) {
        max_score = score;
        grabbed_punit = punit;
      }
    } unit_list_iterate_end;

    if (grabbed_punit) {
      ett = ETT_UNIT;
      value = utype_number(unit_type(grabbed_punit));
    }
  } else if (pbase != NULL) {
    ett = ETT_TERRAIN_SPECIAL;
    value = editor_encode_base_number(base_number(pbase));

  } else if (tile_really_has_any_specials(ptile)) {
    int specials_array[S_LAST];
    int count = 0, i, special = -1;

    tile_special_type_iterate(s) {
      specials_array[count++] = s;
    } tile_special_type_iterate_end;

    /* Grab specials in reverse order of enum tile_special_type. */

    for (i = count - 1; i >= 0; i--) {
      if (tile_has_special(ptile, specials_array[i])) {
        special = specials_array[i];
        break;
      }
    }

    if (special >= 0) {
      ett = ETT_TERRAIN_SPECIAL;
      value = special;
    }
  } else if (tile_resource(ptile) != NULL) {
    ett = ETT_TERRAIN_RESOURCE;
    value = resource_number(tile_resource(ptile));

  } else if (tile_terrain(ptile) != NULL) {
    ett = ETT_TERRAIN;
    value = terrain_number(tile_terrain(ptile));
  }

  if (ett < 0) {
    return;
  }

  editor_set_tool(ett);
  if (editor_tool_has_value(ett)) {
    editor_tool_set_value(ett, value);
  }
  editgui_refresh();
}

/****************************************************************************
  Handle a user's mouse button press at the given point on the map canvas.
****************************************************************************/
void editor_mouse_button_press(int canvas_x, int canvas_y,
                               int button, int modifiers)
{
  struct tile *ptile;
  struct tile_list *tiles;

  if (editor == NULL) {
    return;
  }

  ptile = canvas_pos_to_tile(canvas_x, canvas_y);
  if (ptile == NULL) {
    return;
  }

  tiles = tile_list_new();

  if ((client_is_global_observer()
       || (client_has_player()
           && tile_get_known(ptile, client_player()) == TILE_KNOWN_SEEN))
      && ptile != NULL) {
    tile_list_append(tiles, ptile);
  }

  switch (button) {

  case MOUSE_BUTTON_LEFT:
    if (modifiers == EKM_SHIFT) {
      editor_grab_tool(ptile);
    } else if (modifiers == EKM_CTRL) {
      editor_grab_applied_player(ptile);
    } else if (modifiers == EKM_NONE) {
      editor->tool_active = TRUE;
      editor_apply_tool_single(ptile);
      editor_set_current_tile(ptile);
    }
    break;

  case MOUSE_BUTTON_RIGHT:
    if (modifiers == (EKM_ALT | EKM_CTRL)) {
      editgui_popup_properties(tiles);
      break;
    }
    
    if (modifiers == EKM_SHIFT) {
      editor->selection_mode = SELECTION_MODE_ADD;
    } else if (modifiers == EKM_ALT) {
      editor->selection_mode = SELECTION_MODE_REMOVE;
    } else if (modifiers == EKM_NONE) {
      editor->selection_mode = SELECTION_MODE_NEW;
    } else {
      break;
    }
    editor_start_selection_rectangle(canvas_x, canvas_y);
    break;

  case MOUSE_BUTTON_MIDDLE:
    if (modifiers == EKM_NONE) {
      editgui_popup_properties(tiles);
    }
    break;

  default:
    break;
  }

  tile_list_free(tiles);
}

/****************************************************************************
  Record and handle the end of the selection rectangle.
****************************************************************************/
static void editor_end_selection_rectangle(int canvas_x, int canvas_y)
{
  int w, h;

  if (!editor) {
    return;
  }

  editor->selrect_active = FALSE;

  if (editor->selrect_width <= 0 || editor->selrect_height <= 0) {
    struct tile *ptile;
    
    ptile = canvas_pos_to_tile(canvas_x, canvas_y);
    if (ptile && editor->selection_mode == SELECTION_MODE_ADD) {
      editor_selection_add(ptile);
    } else if (ptile && editor->selection_mode == SELECTION_MODE_REMOVE) {
      editor_selection_remove(ptile);
    } else {
      recenter_button_pressed(canvas_x, canvas_y);
      return;
    }

    if (ptile) {
      refresh_tile_mapcanvas(ptile, TRUE, TRUE);
    }

    return;
  }

  gui_rect_iterate(mapview.gui_x0 + editor->selrect_x,
                   mapview.gui_y0 + editor->selrect_y,
                   editor->selrect_width, editor->selrect_height,
                   ptile, pedge, pcorner, gui_x, gui_y) {
    if (ptile == NULL) {
      continue;
    }
    if (editor->selection_mode == SELECTION_MODE_NEW
        || editor->selection_mode == SELECTION_MODE_ADD) {
      editor_selection_add(ptile);
    } else if (editor->selection_mode == SELECTION_MODE_REMOVE) {
      editor_selection_remove(ptile);
    }
  } gui_rect_iterate_end;

  w = tileset_tile_width(tileset);
  h = tileset_tile_height(tileset);

  update_map_canvas(editor->selrect_x - w,
                    editor->selrect_y - h,
                    editor->selrect_width + 2 * w,
                    editor->selrect_height + 2 * h);
  flush_dirty();
}

/****************************************************************************
  Handle the release of a mouse button click.
****************************************************************************/
void editor_mouse_button_release(int canvas_x, int canvas_y,
                                 int button, int modifiers)
{
  switch (button) {

  case MOUSE_BUTTON_LEFT:
    editor_set_current_tile(NULL);
    editor->tool_active = FALSE;
    break;

  case MOUSE_BUTTON_RIGHT:
    if (editor->selrect_active) {
      editor_end_selection_rectangle(canvas_x, canvas_y);
    }
    break;

  case MOUSE_BUTTON_MIDDLE:
    break;

  default:
    break;
  }
}

/****************************************************************************
  Handle a change in the size of the selection rectangle. The given point
  is the new extremity of the rectangle.
****************************************************************************/
static void editor_resize_selection_rectangle(int canvas_x, int canvas_y)
{
  int x1, y1, x2, y2;

  if (editor->selrect_start_x <= canvas_x) {
    x1 = editor->selrect_start_x;
    x2 = canvas_x;
  } else {
    x1 = canvas_x;
    x2 = editor->selrect_start_x;
  }

  if (editor->selrect_start_y <= canvas_y) {
    y1 = editor->selrect_start_y;
    y2 = canvas_y;
  } else {
    y1 = canvas_y;
    y2 = editor->selrect_start_y;
  }

  dirty_all();
  flush_dirty();

  if (x1 == x2 || y1 == y2) {
    editor->selrect_width = 0;
    editor->selrect_height = 0;
    return;
  }

  editor->selrect_x = x1;
  editor->selrect_y = y1;
  editor->selrect_width = x2 - x1;
  editor->selrect_height = y2 - y1;

  editor_redraw();
}

/****************************************************************************
  Handle the mouse moving over the map canvas.
****************************************************************************/
void editor_mouse_move(int canvas_x, int canvas_y, int modifiers)
{
  const struct tile *ptile, *old;

  if (!editor) {
    return;
  }

  old = editor_get_current_tile();
  ptile = canvas_pos_to_tile(canvas_x, canvas_y);

  if (!ptile) {
    return;
  }

  if (editor->tool_active && old != NULL && old != ptile) {
    editor_apply_tool_single(ptile);
    editor_set_current_tile(ptile);
  }

  if (editor->selrect_active) {
    editor_resize_selection_rectangle(canvas_x, canvas_y);
  }
}

/****************************************************************************
  Notify the server that a batch of edits has completed. This is used as
  a hint for the server to now do any checks it has saved while the batch
  was being processed.
****************************************************************************/
void editor_apply_tool_batch_finished(void)
{
  send_packet_edit_check_tiles(&client.conn);
}

/****************************************************************************
  Apply the current tool at the given tile as a single operation rather
  than in a batch.
****************************************************************************/
void editor_apply_tool_single(const struct tile *ptile)
{
  editor_apply_tool_batch(ptile);
  editor_apply_tool_batch_finished();
}

/****************************************************************************
  Apply the current editor tool to the given tile. This function is
  suitable to called over multiple tiles at once. One the batch of
  operations is finished you should call editor_apply_tool_batch_finished.
****************************************************************************/
void editor_apply_tool_batch(const struct tile *ptile)
{
  enum editor_tool_type ett;
  int value, size, count, apno;
  bool erase;

  if (editor == NULL) {
    return;
  }

  if (ptile == NULL) {
    return;
  }

  ett = editor_get_tool();
  size = editor_get_size();
  count = editor_get_count();
  erase = editor_get_erase_mode();
  value = editor_tool_get_value(ett);
  apno = editor_get_applied_player();

  if (ett != ETT_VISION && !client_is_global_observer()
      && client_has_player()
      && tile_get_known(ptile, client_player()) == TILE_UNKNOWN) {
    return;
  }

  if (editor_tool_has_applied_player(ett)
      && !valid_player_by_number(apno)) {
    return;
  }

  switch (ett) {

  case ETT_TERRAIN:
    dsend_packet_edit_tile_terrain(&client.conn, ptile->x, ptile->y,
                                   erase ? 0 : value, size);
    break;

  case ETT_TERRAIN_RESOURCE:
    dsend_packet_edit_tile_resource(&client.conn, ptile->x, ptile->y,
                                    erase ? -1 : value, size);
    break;

  case ETT_TERRAIN_SPECIAL:
    if (editor_value_is_encoded_base_number(value)) {
      value = editor_decode_base_value(value);
      dsend_packet_edit_tile_base(&client.conn, ptile->x, ptile->y,
                                  value, erase, size);
    } else {
      dsend_packet_edit_tile_special(&client.conn, ptile->x, ptile->y,
                                     value, erase, size);
    }
    break;

  case ETT_UNIT:
    if (erase) {
      unit_list_iterate(ptile->units, punit) {
        if (apno != player_number(punit->owner)) {
          continue;
        }
        dsend_packet_edit_unit_remove(&client.conn, punit->id);
        break;
      } unit_list_iterate_end;
    } else {
      dsend_packet_edit_unit_create(&client.conn, apno,
                                    ptile->x, ptile->y, value, count);
    }
    break;

  case ETT_CITY:
    if (erase) {
      dsend_packet_edit_city_remove(&client.conn,
                                    ptile->x, ptile->y);
    } else {
      dsend_packet_edit_city_create(&client.conn, apno,
                                    ptile->x, ptile->y, size);
    }
    break;

  case ETT_VISION:
    if (client_has_player()) {
      dsend_packet_edit_player_vision(&client.conn,
                                      client_player_number(),
                                      ptile->x, ptile->y,
                                      !erase, size);
    }
    break;

  case ETT_TERRITORY:
    dsend_packet_edit_territory(&client.conn,
                                ptile->x, ptile->y,
                                erase ? -1 : apno, size);
    break;

  case ETT_STARTPOS:
    dsend_packet_edit_startpos(&client.conn,
                               ptile->x, ptile->y,
                               erase ? -1 : apno);
    break;

  default:
    break;
  }
}

/****************************************************************************
  Sets the tile currently assumed to be under the user's mouse pointer.
****************************************************************************/
void editor_set_current_tile(const struct tile *ptile)
{
  if (editor == NULL) {
    return;
  }
  
  editor->current_tile = ptile;
}

/****************************************************************************
  Get the tile that the user's mouse pointer is currently over.
****************************************************************************/
const struct tile *editor_get_current_tile(void)
{
  if (editor == NULL) {
    return NULL;
  }
  
  return editor->current_tile;
}

/****************************************************************************
  Redraw any editor-specific decorations. This should usually be called
  whenever the map canvas is redrawn.
****************************************************************************/
void editor_redraw(void)
{
  if (!editor) {
    return;
  }

  if (editor->selrect_active && editor->selrect_width > 0
      && editor->selrect_height > 0) {
    draw_selection_rectangle(editor->selrect_x,
                             editor->selrect_y,
                             editor->selrect_width,
                             editor->selrect_height);
  }
}

/****************************************************************************
  Set the editor's erase mode to the opposite of its current value.
****************************************************************************/
void editor_toggle_erase_mode(void)
{
  bool erase;
  
  erase = editor_get_erase_mode();
  editor_set_erase_mode(!erase);
}

/****************************************************************************
  Unselect all selected tiles.
****************************************************************************/
void editor_selection_clear(void)
{
  if (!editor) {
    return;
  }
  memset(editor->mapsel.vec, 0, _BV_BYTES(MAP_INDEX_SIZE));
  editor->selection_count = 0;
}

/****************************************************************************
  Add the given tile to the current selection.
****************************************************************************/
void editor_selection_add(const struct tile *ptile)
{
  if (!editor || !ptile) {
    return;
  }
  BV_SET(editor->mapsel, tile_index(ptile));
  editor->selection_count++;
}

/****************************************************************************
  Remove the given tile from the current selection.
****************************************************************************/
void editor_selection_remove(const struct tile *ptile)
{
  if (!editor || !ptile) {
    return;
  }
  BV_CLR(editor->mapsel, tile_index(ptile));
  editor->selection_count--;
}

/****************************************************************************
  Returns TRUE if the given tile is selected.
****************************************************************************/
bool editor_tile_is_selected(const struct tile *ptile)
{
  if (!editor || !ptile) {
    return FALSE;
  }
  return BV_ISSET(editor->mapsel, tile_index(ptile));
}

/****************************************************************************
  Apply the current editor tool to all tiles in the current selection.
****************************************************************************/
void editor_apply_tool_to_selection(void)
{
  if (!editor) {
    return;
  }
  if (editor->selection_count <= 0) {
    return;
  }

  connection_do_buffer(&client.conn);
  whole_map_iterate(ptile) {
    if (editor_tile_is_selected(ptile)) {
      editor_apply_tool_batch(ptile);
    }
  } whole_map_iterate_end;
  editor_apply_tool_batch_finished();
  connection_do_unbuffer(&client.conn);
}

/****************************************************************************
  Get the translated name of the given tool type.
****************************************************************************/
const char *editor_tool_get_name(enum editor_tool_type ett)
{
  if (!editor || !(0 <= ett && ett < NUM_EDITOR_TOOL_TYPES)) {
    return "";
  }

  return editor->tools[ett].name;
}

/****************************************************************************
  Get the translated name of the given tool value. If no such name exists,
  returns an empty string.
****************************************************************************/
const char *editor_tool_get_value_name(enum editor_tool_type emt, int value)
{
  struct terrain *pterrain;
  struct resource *presource;
  struct unit_type *putype;

  if (!editor) {
    return "";
  }

  switch (emt) {
  case ETT_TERRAIN:
    pterrain = terrain_by_number(value);
    return pterrain ? terrain_name_translation(pterrain) : "";
    break;
  case ETT_TERRAIN_RESOURCE:
    presource = resource_by_number(value);
    return presource ? resource_name_translation(presource) : "";
    break;
  case ETT_TERRAIN_SPECIAL:
    if (editor_value_is_encoded_base_number(value)) {
      struct base_type *pbase;
      
      pbase = base_by_number(editor_decode_base_value(value));
      return pbase != NULL ? base_name_translation(pbase) : "";
    }
    return 0 <= value && value < S_LAST
      ? special_name_translation(value) : "";
    break;
  case ETT_UNIT:
    putype = utype_by_number(value);
    return putype ? utype_name_translation(putype) : "";
    break;
  default:
    break;
  }
  return "";
}

/****************************************************************************
  Return TRUE if the given editor tool uses the 'size' parameter.
****************************************************************************/
bool editor_tool_has_size(enum editor_tool_type ett)
{
  if (!editor || !(0 <= ett && ett < NUM_EDITOR_TOOL_TYPES)) {
    return FALSE;
  }
  return editor->tools[ett].flags & ETF_HAS_SIZE;
}

/****************************************************************************
  Returns the current size parameter for the editor's tools.
****************************************************************************/
int editor_get_size(void)
{
  if (!editor) {
    return 1;
  }
  return editor->size;
}

/****************************************************************************
  Sets the size parameter for all editor tools.
****************************************************************************/
void editor_set_size(int size)
{
  if (!editor) {
    return;
  }
  editor->size = MAX(1, size);
}

/****************************************************************************
  Return TRUE if it is meaningful for the given tool to use the 'count'
  parameter.
****************************************************************************/
bool editor_tool_has_count(enum editor_tool_type ett)
{
  if (!editor || !(0 <= ett && ett < NUM_EDITOR_TOOL_TYPES)) {
    return FALSE;
  }
  return editor->tools[ett].flags & ETF_HAS_COUNT;
}

/****************************************************************************
  Returns the 'count' parameter for the editor tools.
****************************************************************************/
int editor_get_count(void)
{
  if (!editor) {
    return 1;
  }
  return editor->count;
}

/****************************************************************************
  Sets the 'count' parameter to the given value.
****************************************************************************/
void editor_set_count(int count)
{
  if (!editor) {
    return;
  }
  editor->count = MAX(1, count);
}

/****************************************************************************
  Returns a sprite containing an icon for the given tool type. Returns
  NULL if no such sprite exists.
****************************************************************************/
struct sprite *editor_tool_get_sprite(enum editor_tool_type ett)
{
  const struct editor_sprites *sprites;

  if (!tileset || !(0 <= ett && ett < NUM_EDITOR_TOOL_TYPES)) {
    return NULL;
  }

  sprites = get_editor_sprites(tileset);
  if (!sprites) {
    return NULL;
  }

  switch (ett) {
  case ETT_TERRAIN:
    return sprites->terrain;
    break;
  case ETT_TERRAIN_RESOURCE:
    return sprites->terrain_resource;
    break;
  case ETT_TERRAIN_SPECIAL:
    return sprites->terrain_special;
    break;
  case ETT_UNIT:
    return sprites->unit;
    break;
  case ETT_CITY:
    return sprites->city;
    break;
  case ETT_VISION:
    return sprites->vision;
    break;
  case ETT_TERRITORY:
    return sprites->territory;
    break;
  case ETT_STARTPOS:
    return sprites->startpos;
    break;
  default:
    break;
  }

  return NULL;
}

/****************************************************************************
  Returns a translated "tooltip" description for the given tool type.
****************************************************************************/
const char *editor_tool_get_tooltip(enum editor_tool_type ett)
{
  if (!editor || !(0 <= ett && ett < NUM_EDITOR_TOOL_TYPES)
      || !editor->tools[ett].tooltip) {
    return "";
  }
  return editor->tools[ett].tooltip;
}

/****************************************************************************
  Returns the current applied player number for the editor.

  May return a player number for which valid_player_by_number returns NULL.
****************************************************************************/
int editor_get_applied_player(void)
{
  if (!editor) {
    return -1;
  }
  return editor->applied_player_no;
}

/****************************************************************************
  Sets the editor's applied player number to the given value.
****************************************************************************/
void editor_set_applied_player(int player_no)
{
  if (!editor) {
    return;
  }
  editor->applied_player_no = player_no;
}

/****************************************************************************
  Returns TRUE if the given tool makes use of the editor's applied player
  number.
****************************************************************************/
bool editor_tool_has_applied_player(enum editor_tool_type ett)
{
  if (!editor || !(0 <= ett && ett < NUM_EDITOR_TOOL_TYPES)) {
    return FALSE;
  }
  return editor->tools[ett].flags & ETF_HAS_APPLIED_PLAYER;
}

/****************************************************************************
  Returns TRUE if erase mode for the given tool erases by sub-value instead
  of any object corresponding to the tool type.
****************************************************************************/
bool editor_tool_has_value_erase(enum editor_tool_type ett)
{
  if (!editor || !(0 <= ett && ett < NUM_EDITOR_TOOL_TYPES)) {
    return FALSE;
  }
  return editor->tools[ett].flags & ETF_HAS_VALUE_ERASE;
}

/****************************************************************************
  Encode the given base type id so that it can be stored as a value for
  the editor tool ETT_TERRAIN_SPECIAL without conflicting with enum
  tile_special_type.
****************************************************************************/
int editor_encode_base_number(int base_type_id)
{
  return S_LAST + base_type_id;
}

/****************************************************************************
  Decode the given ETT_TERRAIN_SPECIAL tool value to a base type id.
****************************************************************************/
int editor_decode_base_value(int value)
{
  return value - S_LAST;
}

/****************************************************************************
  Return TRUE if the give tool value for ETT_TERRAIN_SPECIAL is an
  encoded base type id.
****************************************************************************/
bool editor_value_is_encoded_base_number(int value)
{
  return value >= S_LAST;
}
