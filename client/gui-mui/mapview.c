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

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

#include <libraries/mui.h>

#include <clib/alib_protos.h>
#include <proto/exec.h>
#include <proto/muimaster.h>
#include <proto/intuition.h>

#include "game.h"
#include "government.h"		/* government_graphic() */
#include "map.h"
#include "player.h"
#include "rand.h"
#include "support.h"		/* myusleep() */
#include "timing.h"

#include "control.h"
#include "graphics.h"
#include "options.h"
#include "tilespec.h"

#include "mapview.h"

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
#define EXTRA_BOTTOM_ROW 1


/* Amiga Client stuff */

#include "muistuff.h"
#include "mapclass.h"
#include "overviewclass.h"

IMPORT Object *app;
IMPORT Object *main_people_text;
IMPORT Object *main_year_text;
IMPORT Object *main_gold_text;
IMPORT Object *main_tax_text;
IMPORT Object *main_bulb_sprite;
IMPORT Object *main_sun_sprite;
IMPORT Object *main_government_sprite;
IMPORT Object *main_timeout_text;
IMPORT Object *main_econ_sprite[10];
IMPORT Object *main_overview_area;
IMPORT Object *main_overview_group;
IMPORT Object *main_unitname_text;
IMPORT Object *main_moves_text;
IMPORT Object *main_terrain_text;
IMPORT Object *main_hometown_text;
IMPORT Object *main_unit_unit;
IMPORT Object *main_map_area;

extern int goto_state;
extern int paradrop_state;
extern int nuke_state;

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
  int i;

  set_unit_focus_no_center(punit0);
  set_unit_focus_no_center(punit1);
  
  if (!do_combat_animation) {
    punit0->hp = hp0;
    punit1->hp = hp1;
    refresh_tile_mapcanvas(punit0->x, punit0->y, 1);
    refresh_tile_mapcanvas(punit1->x, punit1->y, 1);

    return;
  }

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

  refresh_tile_mapcanvas(punit0->x, punit0->y, 1);
  refresh_tile_mapcanvas(punit1->x, punit1->y, 1);
}

/**************************************************************************
 gui independend
**************************************************************************/
void blink_active_unit(void)
{
  static int is_shown;
  struct unit *punit;

  if ((punit = get_unit_in_focus()))
  {
    struct tile *ptile;
    ptile = map_get_tile(punit->x, punit->y);

    if (is_shown)
    {
      struct unit_list units;
      units = ptile->units;
      unit_list_init(&ptile->units);
      refresh_tile_mapcanvas(punit->x, punit->y, 1);
      ptile->units = units;
    }
    else
    {
      /* make sure that the blinking unit is always on the top */
      unit_list_unlink(&ptile->units, punit);
      unit_list_insert(&ptile->units, punit);
      refresh_tile_mapcanvas(punit->x, punit->y, 1);
    }

    is_shown = !is_shown;
  }
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
/*
   static int flip;
   GdkGC      *fore, *back;

   if(game.player_ptr->ai.control && !ai_manual_turn_done)
   return;
   if((do_restore && flip) || !do_restore)
   { 

   fore = turn_done_button->style->bg_gc[GTK_STATE_NORMAL];
   back = turn_done_button->style->light_gc[GTK_STATE_NORMAL];

   turn_done_button->style->bg_gc[GTK_STATE_NORMAL] = back;
   turn_done_button->style->light_gc[GTK_STATE_NORMAL] = fore;

   gtk_expose_now(turn_done_button);

   flip=!flip;
   }
*/
}

/**************************************************************************
...
**************************************************************************/
void update_timeout_label(void)
{
/*
 char buffer[512];

 sprintf(buffer, "%d", seconds_to_turndone);
 gtk_set_label(timeout_label, buffer);
*/
}

/**************************************************************************
...
**************************************************************************/
void update_info_label(void)
{
  int d;

  settextf(main_people_text, "Population: %s", int_to_text(civ_population(game.player_ptr)));
  settextf(main_year_text, "Year: %s", textyear(game.year));
  settextf(main_gold_text, "Gold: %d", game.player_ptr->economic.gold);
  settextf(main_tax_text, "Tax:%d Lux:%d Sci:%d",
	   game.player_ptr->economic.tax,
	   game.player_ptr->economic.luxury,
	   game.player_ptr->economic.science);

  if (game.heating > 7)
    game.heating = 7;
  set_bulb_sol_government(8 * game.player_ptr->research.researched /
			  (research_time(game.player_ptr) + 1),
			  game.heating,
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
...
**************************************************************************/
void update_unit_info_label(struct unit *punit)
{
  if (punit)
  {
    struct city *pcity;
    pcity = city_list_find_id(&game.player_ptr->cities, punit->homecity);

    settextf(main_unitname_text, "%s%s", get_unit_type(punit->type)->name, (punit->veteran) ? " (veteran)" : "");
    settext(main_moves_text, (goto_state == punit->id) ? "Select destination" : unit_activity_text(punit));
    settext(main_terrain_text, map_get_tile_info_text(punit->x, punit->y));
    settext(main_hometown_text, pcity ? pcity->name : "");
  }
  else
  {
    settext(main_unitname_text, "");
    settext(main_moves_text, "");
    settext(main_terrain_text, "");
    settext(main_hometown_text, "");
  }

  set(main_unit_unit, MUIA_Unit_Unit, punit);
}

/**************************************************************************
...
**************************************************************************/
void set_bulb_sol_government(int bulb, int sol, int government)
{
  struct Sprite *gov_sprite;

  bulb = CLIP(0, bulb, NUM_TILES_PROGRESS - 1);
  sol = CLIP(0, sol, NUM_TILES_PROGRESS - 1);

  set(main_bulb_sprite, MUIA_Sprite_Sprite, sprites.bulb[bulb]);
  set(main_sun_sprite, MUIA_Sprite_Sprite, sprites.warming[sol]);

  if (game.government_count == 0)
  {
    /* not sure what to do here */
    gov_sprite = sprites.citizen[7];
  }
  else
  {
    gov_sprite = get_government(government)->sprite;
  }
  set(main_government_sprite, MUIA_Sprite_Sprite, gov_sprite);
}


/**************************************************************************
 GUI Independ (with new access functions)
**************************************************************************/
int map_canvas_adjust_x(int x)
{
  LONG map_view_x0 = get_map_x_start();
  LONG map_canvas_store_twidth = get_map_x_visible();

  if (map_view_x0 + map_canvas_store_twidth <= map.xsize)
    return x - map_view_x0;
  else if (x >= map_view_x0)
    return x - map_view_x0;
  else if (x < map_adjust_x(map_view_x0 + map_canvas_store_twidth))
    return x + map.xsize - map_view_x0;

  return -1;
}

/**************************************************************************
 GUI Independ (with new access functions)
**************************************************************************/
int map_canvas_adjust_y(int y)
{
  return y - get_map_y_start();	//map_view_y0;

}

/**************************************************************************
 GUI Independ (with new access functions)
**************************************************************************/
void refresh_tile_mapcanvas(int x, int y, int write_to_screen)
{
  x = map_adjust_x(x);
  y = map_adjust_y(y);

  if (tile_visible_mapcanvas(x, y))
  {
    update_map_canvas(map_canvas_adjust_x(x),
		      map_canvas_adjust_y(y), 1, 1, write_to_screen);
  }
  overview_update_tile(x, y);
}

/**************************************************************************
 GUI Independ (with new access functions)
**************************************************************************/
int tile_visible_mapcanvas(int x, int y)
{
  LONG map_view_x0 = get_map_x_start();
  LONG map_view_y0 = get_map_y_start();
  LONG map_canvas_store_twidth = get_map_x_visible();
  LONG map_canvas_store_theight = get_map_y_visible();

  return (y >= map_view_y0 && y < map_view_y0 + map_canvas_store_theight &&
	  ((x >= map_view_x0 && x < map_view_x0 + map_canvas_store_twidth) ||
	   (x + map.xsize >= map_view_x0 &&
	    x + map.xsize < map_view_x0 + map_canvas_store_twidth)));
}

/**************************************************************************
 GUI Independ (with new access functions)
**************************************************************************/
int tile_visible_and_not_on_border_mapcanvas(int x, int y)
{
  LONG map_view_x0 = get_map_x_start();
  LONG map_view_y0 = get_map_y_start();
  LONG map_canvas_store_twidth = get_map_x_visible();
  LONG map_canvas_store_theight = get_map_y_visible();

  return ((y>=map_view_y0+2 || (y >= map_view_y0 && map_view_y0 == 0))
	  && (y<map_view_y0+map_canvas_store_theight-2 ||
	      (y<map_view_y0+map_canvas_store_theight &&
	       map_view_y0 + map_canvas_store_theight-EXTRA_BOTTOM_ROW == map.ysize))
	  && ((x>=map_view_x0+2 && x<map_view_x0+map_canvas_store_twidth-2) ||
	      (x+map.xsize>=map_view_x0+2
	       && x+map.xsize<map_view_x0+map_canvas_store_twidth-2)));
}

/**************************************************************************
...
**************************************************************************/
void move_unit_map_canvas(struct unit *punit, int x0, int y0, int dx, int dy)
{
  int dest_x, dest_y;
  int i;

  dest_x = map_adjust_x(x0 + dx);
  dest_y = map_adjust_y(y0 + dy);

  if (player_can_see_unit(game.player_ptr, punit) && (
					   tile_visible_mapcanvas(x0, y0) ||
				    tile_visible_mapcanvas(dest_x, dest_y)))
  {
    DoMethod(main_map_area, MUIM_Map_MoveUnit, punit, x0, y0, dx, dy, dest_x, dest_y);
  }
}

/**************************************************************************
...
**************************************************************************/
void get_center_tile_mapcanvas(int *x, int *y)
{
  LONG map_view_x0 = get_map_x_start();
  LONG map_view_y0 = get_map_y_start();
  LONG map_canvas_store_twidth = get_map_x_visible();
  LONG map_canvas_store_theight = get_map_y_visible();

  *x = map_adjust_x(map_view_x0 + map_canvas_store_twidth / 2);
  *y = map_adjust_y(map_view_y0 + map_canvas_store_theight / 2);
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
  int new_map_view_x0, new_map_view_y0;
  LONG map_view_x0 = get_map_x_start();
  LONG map_view_y0 = get_map_y_start();
  LONG map_canvas_store_twidth = get_map_x_visible();
  LONG map_canvas_store_theight = get_map_y_visible();

  new_map_view_x0 = map_adjust_x(x - map_canvas_store_twidth / 2);
  new_map_view_y0 = map_adjust_y(y - map_canvas_store_theight / 2);
  if (new_map_view_y0 > map.ysize - map_canvas_store_theight)
    new_map_view_y0 = map_adjust_y(map.ysize - map_canvas_store_theight);

  set_map_xy_start(new_map_view_x0, new_map_view_y0);
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
void update_map_canvas(int tile_x, int tile_y, int width, int height,
		       int write_to_screen)
{
  DoMethod(main_map_area, MUIM_Map_Refresh, tile_x, tile_y, width, height, write_to_screen);
}

/**************************************************************************
 Update the visible part of the map
**************************************************************************/
void update_map_canvas_visible(void)
{
  LONG map_canvas_store_twidth = get_map_x_visible();
  LONG map_canvas_store_theight = get_map_y_visible();

  update_map_canvas(0, 0, map_canvas_store_twidth, map_canvas_store_theight, 1);
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

