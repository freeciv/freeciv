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
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

#include <libraries/mui.h>

#include <clib/alib_protos.h>
#include <proto/exec.h>
#include <proto/muimaster.h>
#include <proto/intuition.h>

#include "fcintl.h"
#include "game.h"
#include "government.h"		/* government_graphic() */
#include "map.h"
#include "player.h"
#include "rand.h"
#include "support.h"		/* myusleep() */
#include "timing.h"

#include "civclient.h"
#include "control.h"
#include "goto.h"
#include "graphics.h"
#include "gui_main.h"
#include "options.h"
#include "tilespec.h"
#include "climisc.h"

#include "mapview.h"

/* Amiga Client stuff */

#include "autogroupclass.h"
#include "muistuff.h"
#include "mapclass.h"
#include "overviewclass.h"

/*
The bottom row of the map was sometimes hidden.

As of now the top left corner is always aligned with the tiles. This is what
causes the problem in the first place. The ideal solution would be to align the
window with the bottom left tiles if you tried to center the window on a tile
closer than (screen_tiles_height/2 -1) to the south pole.

But, for now, I just grepped for occurences where the ysize (or the values
derived from it) were used, and those places that had relevance to drawing the
map, and I added 1 (using the EXTRA_BOTTOM_ROW constant).

-Thue
*/

/* If we have isometric view we need to be able to scroll a little extra down.
   The places that needs to be adjusted are the same as above. */
#define EXTRA_BOTTOM_ROW (is_isometric ? 6 : 1)

/**************************************************************************
 Some support functions
**************************************************************************/
int get_map_x_start(void)
{
  return xget(main_map_area, MUIA_Map_HorizFirst);
}

int get_map_y_start(void)
{
  return xget(main_map_area, MUIA_Map_VertFirst);
}

int get_map_x_visible(void)
{
  return xget(main_map_area, MUIA_Map_HorizVisible);
}

int get_map_y_visible(void)
{
  return xget(main_map_area, MUIA_Map_VertVisible);
}

/**************************************************************************
 ...
**************************************************************************/
void decrease_unit_hp_smooth(struct unit *punit0, int hp0,
			     struct unit *punit1, int hp1)
{
  static struct timer *anim_timer = NULL; 
  struct unit *losing_unit = (hp0 == 0 ? punit0 : punit1);

  if (!do_combat_animation) {
    punit0->hp = hp0;
    punit1->hp = hp1;

    refresh_tile_mapcanvas(punit0->x, punit0->y, 1);
    refresh_tile_mapcanvas(punit1->x, punit1->y, 1);

    return;
  }

  set_units_in_combat(punit0, punit1);

  do
  {
    anim_timer = renew_timer_start(anim_timer, TIMER_USER, TIMER_ACTIVE);

    if (punit0->hp > hp0
	&& myrand((punit0->hp - hp0) + (punit1->hp - hp1)) < punit0->hp - hp0)
      punit0->hp--;
    else if (punit1->hp > hp1)
      punit1->hp--;
    else
      punit0->hp--;

    refresh_tile_mapcanvas(punit0->x, punit0->y, 1);
    refresh_tile_mapcanvas(punit1->x, punit1->y, 1);

    usleep_since_timer_start(anim_timer, 10000);

  } while (punit0->hp > hp0 || punit1->hp > hp1);

  DoMethod(main_map_area, MUIM_Map_ExplodeUnit, losing_unit);

  set_units_in_combat(NULL, NULL);

  refresh_tile_mapcanvas(punit0->x, punit0->y, 1);
  refresh_tile_mapcanvas(punit1->x, punit1->y, 1);
}

/**************************************************************************
 Set the dimensions of the overviewmap
**************************************************************************/
void set_overview_dimensions(int x, int y)
{
  if (main_overview_area)
  {
    if ((xget(main_overview_area, MUIA_Overview_Width) == x) &&
	(xget(main_overview_area, MUIA_Overview_Height) == y))
    {
      /* If nothing has changed simply return */
      return;
    }
  }

  DoMethod(main_overview_group, MUIM_Group_InitChange);

  if (main_overview_area)
  {
    DoMethod(main_overview_group, OM_REMMEMBER, main_overview_area);
    MUI_DisposeObject(main_overview_area);
  }

  if ((main_overview_area = MakeOverview(x, y)))
  {
    set(main_map_area, MUIA_Map_Overview, main_overview_area);
    DoMethod(main_overview_group, OM_ADDMEMBER, main_overview_area);
  }

  DoMethod(main_overview_group, MUIM_Group_ExitChange);
}


/**************************************************************************
...
**************************************************************************/
void update_turn_done_button(int do_restore)
{
  static int flip;

  if (game.player_ptr->ai.control && !ai_manual_turn_done)
    return;

  if ((do_restore && flip) || !do_restore)
  {
    if (!flip) set(main_turndone_button,MUIA_Background, MUII_FILL);
    else set(main_turndone_button,MUIA_Background, MUII_ButtonBack);
    flip = !flip;
  }
}

/**************************************************************************
 Update the timeout label
**************************************************************************/
void update_timeout_label(void)
{
  char buffer[64];
  if (game.timeout <= 0)
    sz_strlcpy(buffer, _("off"));
  else
    format_duration(buffer, sizeof(buffer), seconds_to_turndone);

  settext(main_timeout_text, buffer);
}

/**************************************************************************
...
**************************************************************************/
void update_info_label(void)
{
  int d;

  settextf(main_people_text, _("Population: %s"),
	   population_to_text(civ_population(game.player_ptr)));
  settextf(main_year_text, _("Year: %s"), textyear(game.year));
  settextf(main_gold_text, _("Gold: %d"), game.player_ptr->economic.gold);
  settextf(main_tax_text, _("Tax:%d Lux:%d Sci:%d"),
	   game.player_ptr->economic.tax,
	   game.player_ptr->economic.luxury,
	   game.player_ptr->economic.science);

  set_indicator_icons(client_research_sprite(),
		      client_warming_sprite(),
		      client_cooling_sprite(),
		      game.player_ptr->government);

  d = 0;
  for (; d < (game.player_ptr->economic.luxury) / 10; d++)
    set(main_econ_sprite[d], MUIA_Sprite_Sprite, get_citizen_sprite(0));

  for (; d < (game.player_ptr->economic.science + game.player_ptr->economic.luxury) / 10; d++)
    set(main_econ_sprite[d], MUIA_Sprite_Sprite, get_citizen_sprite(1));

  for (; d < 10; d++)
    set(main_econ_sprite[d], MUIA_Sprite_Sprite, get_citizen_sprite(2));

  update_timeout_label();
}

/**************************************************************************
 Callback if a below unit was selected
**************************************************************************/
void activate_below_unit (int *id)
{
  struct unit *punit = find_unit_by_id(*id);
  if (punit) request_unit_selected(punit);
}

/**************************************************************************
 Update the information label which gives info on the current unit and the
 square under the current unit, for specified unit.  Note that in practice
 punit is almost always (or maybe strictly always?) the focus unit.
 Clears label if punit is NULL.
 Also updates the cursor for the map_canvas (this is related because the
 info label includes a "select destination" prompt etc).
 Also calls update_unit_pix_label() to update the icons for units on this
 square.
 (Note, that in the Mui client the last part is handled different)
**************************************************************************/
void update_unit_info_label(struct unit *punit)
{
  if (punit)
  {
    struct city *pcity;
    int units = 0;
    pcity = player_find_city_by_id(game.player_ptr, punit->homecity);

    settextf(main_unitname_text, "%s%s", unit_type(punit)->name,
	     (punit->veteran) ? _(" (veteran)") : "");
    settext(main_moves_text, (hover_unit == punit->id) ? _("Select destination") : unit_activity_text(punit));
    settext(main_terrain_text, map_get_tile_info_text(punit->x, punit->y));
    settext(main_hometown_text, pcity ? pcity->name : "");


    /* Count the number of units */
    unit_list_iterate(map_get_tile(punit->x, punit->y)->units, aunit) {
      if (aunit != punit) {
	units++;
      }
    }
    unit_list_iterate_end;

    if (xget(main_below_group, MUIA_AutoGroup_NumObjects) == units + 1)
    {
      struct List *child_list = (struct List*)xget(main_below_group,MUIA_Group_ChildList);
      Object *cstate = (Object *)child_list->lh_Head;
      Object *child;

      unit_list_iterate(map_get_tile(punit->x, punit->y)->units, aunit) {
        if (aunit != punit) {
          if ((child = (Object*)NextObject(&cstate))) {
            set(child, MUIA_Unit_Unit, aunit);
            DoMethod(child, MUIM_KillNotify, MUIA_Pressed);
	    /* Activate the unit if pressed, note that the object may get's disposed */
	    DoMethod(child, MUIM_Notify, MUIA_Pressed, FALSE, app, 7, MUIM_Application_PushMethod, app, 4, MUIM_CallHook, &civstandard_hook, activate_below_unit, aunit->id);
          }
        }
      }
      unit_list_iterate_end;
    } else
    {
      Object *obj;
      DoMethod(main_below_group, MUIM_Group_InitChange);
      DoMethod(main_below_group, MUIM_AutoGroup_DisposeChilds);

      unit_list_iterate(map_get_tile(punit->x, punit->y)->units, aunit) {
        if (aunit != punit) {
	  if ((obj = UnitObject, MUIA_InputMode, MUIV_InputMode_RelVerify, MUIA_Unit_Unit, aunit, End)) {
	    DoMethod(main_below_group, OM_ADDMEMBER, obj);
	    /* Activate the unit if pressed, note that the object may get's disposed */
	    DoMethod(obj, MUIM_Notify, MUIA_Pressed, FALSE, app, 7, MUIM_Application_PushMethod, app, 4, MUIM_CallHook, &civstandard_hook, activate_below_unit, aunit->id);
	  }
        }
      }
      unit_list_iterate_end;

      DoMethod(main_below_group, OM_ADDMEMBER, RectangleObject, MUIA_Weight,0,End);
      DoMethod(main_below_group, MUIM_Group_ExitChange);
    }
  }
  else
  {
    settext(main_unitname_text, "");
    settext(main_moves_text, "");
    settext(main_terrain_text, "");
    settext(main_hometown_text, "");

    if (xget(main_below_group, MUIA_AutoGroup_NumObjects) > 1)
    {
      DoMethod(main_below_group, MUIM_Group_InitChange);
      DoMethod(main_below_group, MUIM_AutoGroup_DisposeChilds);
      DoMethod(main_below_group, OM_ADDMEMBER, RectangleObject, MUIA_Weight,0,End);
      DoMethod(main_below_group, MUIM_Group_ExitChange);
    }

  }

  set(main_unit_unit, MUIA_Unit_Unit, punit);
}

/**************************************************************************
...
**************************************************************************/
void set_indicator_icons(int bulb, int sol, int flake, int gov)
{
  struct Sprite *gov_sprite;

  bulb = CLIP(0, bulb, NUM_TILES_PROGRESS - 1);
  sol = CLIP(0, sol, NUM_TILES_PROGRESS - 1);
  flake = CLIP(0, flake, NUM_TILES_PROGRESS-1);

  set(main_bulb_sprite, MUIA_Sprite_Sprite, sprites.bulb[bulb]);
  set(main_sun_sprite, MUIA_Sprite_Sprite, sprites.warming[sol]);
  set(main_flake_sprite, MUIA_Sprite_Sprite, sprites.cooling[flake]);

  if (game.government_count == 0)
  {
    /* not sure what to do here */
    gov_sprite = sprites.citizen[7];
  }
  else
  {
    gov_sprite = get_government(gov)->sprite;
  }
  set(main_government_sprite, MUIA_Sprite_Sprite, gov_sprite);
}


/**************************************************************************
Finds the pixel coordinates of a tile.
Beside setting the results in canvas_x,canvas_y it returns whether the tile
is inside the visible map.
**************************************************************************/
int get_canvas_xy(int map_x, int map_y, int *canvas_x, int *canvas_y)
{
  int map_canvas_store_twidth = xget(main_map_area, MUIA_Map_HorizVisible);
  int map_canvas_store_theight = xget(main_map_area, MUIA_Map_VertVisible);
  int map_view_x0 = xget(main_map_area, MUIA_Map_HorizFirst);
  int map_view_y0 = xget(main_map_area, MUIA_Map_VertFirst);

  if (is_isometric) {
    /* canvas_x,canvas_y is the top corner of the actual tile, not the pixmap.
       This function also handels non-adjusted tile coords (like -1, -2) as if
       they were adjusted.
       You might want to first take a look at the simpler city_get_xy() for basic
       understanding. */
    int diff_xy;
    int diff_x, diff_y;
    int width, height;

    width = _mwidth(main_map_area); /* !! */
    height = _mheight(main_map_area); /* !! */

    map_x %= map.xsize;
    if (map_x < map_view_x0) map_x += map.xsize;
    diff_xy = (map_x + map_y) - (map_view_x0 + map_view_y0);
    /* one diff_xy value defines a line parallel with the top of the isometric
       view. */
    *canvas_y = diff_xy/2 * NORMAL_TILE_HEIGHT + (diff_xy%2) * (NORMAL_TILE_HEIGHT/2);

    /* changing both x and y with the same amount doesn't change the isometric
       x value. (draw a grid to see it!) */
    map_x -= diff_xy/2;
    map_y -= diff_xy/2;
    diff_x = map_x - map_view_x0;
    diff_y = map_view_y0 - map_y;

    *canvas_x = (diff_x - 1) * NORMAL_TILE_WIDTH
      + (diff_x == diff_y ? NORMAL_TILE_WIDTH : NORMAL_TILE_WIDTH/2)
      /* tiles starting above the visible area */
      + (diff_y > diff_x ? NORMAL_TILE_WIDTH : 0);

    /* We now have the corner of the sprite. For drawing we move it. */
    *canvas_x -= NORMAL_TILE_WIDTH/2;

    return (*canvas_x > -NORMAL_TILE_WIDTH)
      && *canvas_x < (width + NORMAL_TILE_WIDTH/2)
      && (*canvas_y > -NORMAL_TILE_HEIGHT)
      && (*canvas_y < height);
  } else { /* is_isometric */
    if (map_view_x0+map_canvas_store_twidth <= map.xsize)
      *canvas_x = map_x-map_view_x0;
    else if(map_x >= map_view_x0)
      *canvas_x = map_x-map_view_x0;
    else if(map_x < map_adjust_x(map_view_x0+map_canvas_store_twidth))
      *canvas_x = map_x+map.xsize-map_view_x0;
    else *canvas_x = -1;

    *canvas_y = map_y - map_view_y0;

    *canvas_x *= NORMAL_TILE_WIDTH;
    *canvas_y *= NORMAL_TILE_HEIGHT;

    return *canvas_x >= 0
	&& *canvas_x < map_canvas_store_twidth * NORMAL_TILE_WIDTH
	&& *canvas_y >= 0
	&& *canvas_y < map_canvas_store_theight * NORMAL_TILE_HEIGHT;
  }
}

/**************************************************************************
Finds the map coordinates corresponding to pixel coordinates.
**************************************************************************/
void get_map_xy(int canvas_x, int canvas_y, int *map_x, int *map_y)
{
  int map_view_x0 = xget(main_map_area, MUIA_Map_HorizFirst);
  int map_view_y0 = xget(main_map_area, MUIA_Map_VertFirst);

  if (is_isometric) {
    *map_x = map_view_x0;
    *map_y = map_view_y0;

    /* first find an equivalent position on the left side of the screen. */
    *map_x += canvas_x/NORMAL_TILE_WIDTH;
    *map_y -= canvas_x/NORMAL_TILE_WIDTH;
    canvas_x %= NORMAL_TILE_WIDTH;

    /* Then move op to the top corner. */
    *map_x += canvas_y/NORMAL_TILE_HEIGHT;
    *map_y += canvas_y/NORMAL_TILE_HEIGHT;
    canvas_y %= NORMAL_TILE_HEIGHT;

    /* We are inside a rectangle, with 2 half tiles starting in the corner,
       and two tiles further out. Draw a grid to see how this works :). */
    assert(NORMAL_TILE_WIDTH == 2*NORMAL_TILE_HEIGHT);
    canvas_y *= 2; /* now we have a square. */
    if (canvas_x > canvas_y) (*map_y) -= 1;
    if (canvas_x + canvas_y > NORMAL_TILE_WIDTH) (*map_x) += 1;

    /* If we are outside the map find the nearest tile, with distance as
       seen on the map. */
    nearest_real_pos(map_x, map_y);
  } else { /* is_isometric */
    *map_x = map_adjust_x(map_view_x0 + canvas_x/NORMAL_TILE_WIDTH);
    *map_y = map_adjust_y(map_view_y0 + canvas_y/NORMAL_TILE_HEIGHT);
  }
}

/**************************************************************************
 GUI Independ (with new access functions)
**************************************************************************/
int tile_visible_mapcanvas(int x, int y)
{
  if (is_isometric) {
    int dummy_x, dummy_y; /* well, it needs two pointers... */
    return get_canvas_xy(x, y, &dummy_x, &dummy_y);
  } else {
    int map_view_x0 = get_map_x_start();
    int map_view_y0 = get_map_y_start();
    int map_canvas_store_twidth = get_map_x_visible();
    int map_canvas_store_theight = get_map_y_visible();

    return (y>=map_view_y0 && y<map_view_y0+map_canvas_store_theight &&
	    ((x>=map_view_x0 && x<map_view_x0+map_canvas_store_twidth) ||
	     (x+map.xsize>=map_view_x0 && 
	      x+map.xsize<map_view_x0+map_canvas_store_twidth)));
  }
}

/**************************************************************************
 GUI Independ (with new access functions)
**************************************************************************/
int tile_visible_and_not_on_border_mapcanvas(int x, int y)
{
  if (is_isometric) {
    int canvas_x, canvas_y;
    int width, height;
    width = _mwidth(main_map_area);
    height = _mheight(main_map_area);

    get_canvas_xy(x, y, &canvas_x, &canvas_y);

    return canvas_x > NORMAL_TILE_WIDTH/2
      && canvas_x < (width - 3*NORMAL_TILE_WIDTH/2)
      && canvas_y >= NORMAL_TILE_HEIGHT
      && canvas_y < height - 3 * NORMAL_TILE_HEIGHT/2;
  } else {
    int map_view_x0 = get_map_x_start();
    int map_view_y0 = get_map_y_start();
    int map_canvas_store_twidth = get_map_x_visible();
    int map_canvas_store_theight = get_map_y_visible();

    return ((y>=map_view_y0+2 || (y >= map_view_y0 && map_view_y0 == 0))
	    && (y<map_view_y0+map_canvas_store_theight-2 ||
		(y<map_view_y0+map_canvas_store_theight &&
		 map_view_y0 + map_canvas_store_theight-EXTRA_BOTTOM_ROW == map.ysize))
	    && ((x>=map_view_x0+2 && x<map_view_x0+map_canvas_store_twidth-2) ||
		(x+map.xsize>=map_view_x0+2
		 && x+map.xsize<map_view_x0+map_canvas_store_twidth-2)));
  }
}

/**************************************************************************
...
**************************************************************************/
void move_unit_map_canvas(struct unit *punit, int x0, int y0, int dx, int dy)
{
  int dest_x, dest_y;
  /* only works for adjacent-square moves */
  if ((dx < -1) || (dx > 1) || (dy < -1) || (dy > 1) ||
      ((dx == 0) && (dy == 0))) {
    return;
  }

  if (punit == get_unit_in_focus() && hover_state != HOVER_NONE) {
    set_hover_state(NULL, HOVER_NONE);
    update_unit_info_label(punit);
  }

  dest_x = map_adjust_x(x0+dx);
  dest_y = map_adjust_y(y0+dy);

  if (player_can_see_unit(game.player_ptr, punit) &&
      (tile_visible_mapcanvas(x0, y0) ||
       tile_visible_mapcanvas(dest_x, dest_y))) {
    DoMethod(main_map_area, MUIM_Map_MoveUnit, punit, x0, y0, dx, dy, dest_x, dest_y);
  }
}

/**************************************************************************
...
**************************************************************************/
void get_center_tile_mapcanvas(int *x, int *y)
{
  int width, height;
  width = _mwidth(main_map_area);
  height = _mheight(main_map_area);

  /* This sets the pointers x and y */
  get_map_xy(width/2, height/2, x, y);
}

/**************************************************************************
...
**************************************************************************/
void set_map_xy_start(int new_map_view_x0, int new_map_view_y0)
{
  SetAttrs(main_map_area,
	   MUIA_Map_HorizFirst, new_map_view_x0,
	   MUIA_Map_VertFirst, new_map_view_y0,
	   TAG_DONE);
}

/**************************************************************************
...
**************************************************************************/
void center_tile_mapcanvas(int x, int y)
{
  int map_view_x0;
  int map_view_y0;
  int map_canvas_store_twidth = get_map_x_visible();
  int map_canvas_store_theight = get_map_y_visible();

  if (is_isometric) {
    x -= map_canvas_store_twidth/2;
    y += map_canvas_store_twidth/2;
    x -= map_canvas_store_theight/2;
    y -= map_canvas_store_theight/2;

    map_view_x0 = map_adjust_x(x);
    map_view_y0 = map_adjust_y(y);

    map_view_y0 =
      (map_view_y0 > map.ysize + EXTRA_BOTTOM_ROW - map_canvas_store_theight) ? 
      map.ysize + EXTRA_BOTTOM_ROW - map_canvas_store_theight :
      map_view_y0;
  } else {
    map_view_x0=map_adjust_x(x-map_canvas_store_twidth/2);
    map_view_y0=map_adjust_y(y-map_canvas_store_theight/2);
    if (map_view_y0>map.ysize+EXTRA_BOTTOM_ROW-map_canvas_store_theight)
      map_view_y0=map_adjust_y(map.ysize+EXTRA_BOTTOM_ROW-map_canvas_store_theight);
  }

  set_map_xy_start(map_view_x0, map_view_y0);
// remove me
#ifdef DISABLED
  update_map_canvas_visible();
  update_map_canvas_scrollbars();
  refresh_overview_viewrect();
#endif
}

/**************************************************************************
...
**************************************************************************/
void refresh_overview_canvas(void)
{
  DoMethod(main_overview_area, MUIM_Overview_Refresh);
}


/**************************************************************************
...
**************************************************************************/
void overview_update_tile(int x, int y)
{
  DoMethod(main_overview_area, MUIM_Overview_RefreshSingle, x, y);
}

/**************************************************************************
...
**************************************************************************/
void refresh_overview_viewrect(void)
{
  DoMethod(main_overview_area, MUIM_Overview_Refresh);
}

/**************************************************************************
...
**************************************************************************/
static void show_city_descriptions(void)
{
  if (!draw_city_names && !draw_city_productions)
    return;

  DoMethod(main_map_area, MUIM_Map_ShowCityDescriptions);
}

/**************************************************************************
Refresh and draw to sceen all the tiles in a rectangde width,height (as
seen in overhead ciew) with the top corner at x,y.
All references to "left","right", "top" and "bottom" refer to the sides of
the rectangle width, height as it would be seen in top-down view, unless
said otherwise.
The trick is to draw tiles furthest up on the map first, since we will be
drawing on top of them when we draw tiles further down.

Works by first refreshing map_canvas_store and then drawing the result to
the screen.
**************************************************************************/
void update_map_canvas(int x, int y, int width, int height, 
		       int write_to_screen)
{
  DoMethod(main_map_area, MUIM_Map_Refresh, x, y, width, height, write_to_screen);
}

/**************************************************************************
 Update (only) the visible part of the map
**************************************************************************/
void update_map_canvas_visible(void)
{
  int map_view_x0 = xget(main_map_area, MUIA_Map_HorizFirst);
  int map_view_y0 = xget(main_map_area, MUIA_Map_VertFirst);
  int map_canvas_store_twidth = get_map_x_visible();
  int map_canvas_store_theight = get_map_y_visible();

  if (is_isometric) {
    /* just find a big rectangle that includes the whole visible area. The
       invisible tiles will not be drawn. */
    int width, height;

    width = height = map_canvas_store_twidth + map_canvas_store_theight;
    update_map_canvas(map_view_x0,
		      map_view_y0 - map_canvas_store_twidth,
		      width, height, 1);
  } else {
    update_map_canvas(map_view_x0, map_view_y0,
		      map_canvas_store_twidth,map_canvas_store_theight, 1);
  }

  show_city_descriptions();
}

/**************************************************************************
 Update display of descriptions associated with cities on the main map.
**************************************************************************/
void update_city_descriptions(void)
{
  update_map_canvas_visible();
}

/**************************************************************************
...
**************************************************************************/
void put_nuke_mushroom_pixmaps(int abs_x0, int abs_y0)
{
  DoMethod(main_map_area, MUIM_Map_DrawMushroom, abs_x0, abs_y0);
}

/**************************************************************************
 Draws a cross-hair overlay on a tile
**************************************************************************/
void put_cross_overlay_tile(int x, int y)
{
  DoMethod(main_map_area, MUIM_Map_PutCrossTile, x, y);
}

/**************************************************************************
 Shade the tiles around a city to indicate the location of workers
**************************************************************************/
void put_city_workers(struct city *pcity, int color)
{
  DoMethod(main_map_area, MUIM_Map_PutCityWorkers, pcity, color);
}

/**************************************************************************
draw a line from src_x,src_y -> dest_x,dest_y on both map_canvas and
map_canvas_store
**************************************************************************/
void draw_segment(int src_x, int src_y, int dir)
{
  DoMethod(main_map_area, MUIM_Map_DrawSegment, src_x, src_y, dir);
}

/**************************************************************************
remove the line from src_x,src_y -> dest_x,dest_y on both map_canvas and
map_canvas_store.
**************************************************************************/
void undraw_segment(int src_x, int src_y, int dir)
{
  DoMethod(main_map_area, MUIM_Map_UndrawSegment, src_x, src_y, dir);
}

