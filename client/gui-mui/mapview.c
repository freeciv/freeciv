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

#include "control.h"
#include "goto.h"
#include "graphics.h"
#include "options.h"
#include "tilespec.h"
#include "climisc.h"

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
IMPORT Object *main_flake_sprite;
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
IMPORT Object *main_turndone_button;

extern int seconds_to_turndone;

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

  settextf(main_people_text, _("Population: %s"), int_to_text(civ_population(game.player_ptr)));
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
    pcity = player_find_city_by_id(game.player_ptr, punit->homecity);

    settextf(main_unitname_text, "%s%s", get_unit_type(punit->type)->name, (punit->veteran) ? _(" (veteran)") : "");
    settext(main_moves_text, (hover_unit == punit->id) ? _("Select destination") : unit_activity_text(punit));
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
  return y - get_map_y_start();	/*map_view_y0;*/

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
Put a line on a pixmap. This line goes from the center of a tile
(canvas_src_x, canvas_src_y) to the border of the tile.
Note that this means that if you want to draw a line from x1,y1->x2,y2 you
must call this function twice; once to draw from the center of x1,y1 to the
border of the tile, with a dir argument. And once from x2,y2 with a dir
argument for the opposite direction. This is necessary to enable the
refreshing of a single tile

Since the line_gc that is used to draw the line has function GDK_INVERT,
drawing a line once and then again will leave the map unchanged. Therefore
this function is used both to draw and undraw lines.

There are two issues that make this function somewhat complex:
1) The center pixel should only be drawn once (or it could be inverted
   twice). This is done by adjusting the line starting point depending on
   if the line we are drawing is the first line to be drawn. (not strictly
   correct; read on to 2))
(in the following I am assuming a tile width of 30 (as trident); replace as
neccesary This point is not a problem with tilesets with odd height/width,
fx engels with height 45)
2) Since the tile has width 30 we cannot put the center excactly "at the
   center" of the tile (that would require the width to be an odd number).
   So we put it at 15,15, with the effect that there is 15 pixels above and
   14 underneath (and likewise horizontally). This has an unfortunent
   consequence for the drawing in dirs 2 and 5 (as used in the DIR_DX and
   DIR_DY arrays); since we want to draw the line to the very corner of the
   tile the line vector will be (14,-15) and (-15,14) respectively, which
   would look wrong when drawn. To make the lines look nice the starting
   point of dir 2 is moved one pixel up, and the starting point of dir 5 is
   moved one pixel left, so the vectors will be (14,-14) and (-14,14).
   Also, because they are off-center the starting pixel is not drawn when
   drawing one of these directions.
**************************************************************************/
#ifdef NOTYET
static void put_line(GdkDrawable *pm, int canvas_src_x, int canvas_src_y,
		     int map_src_x, int map_src_y, int dir, int first_draw)
{
  int dir2;
  int canvas_dest_x = canvas_src_x + DIR_DX[dir] * NORMAL_TILE_WIDTH/2;
  int canvas_dest_y = canvas_src_y + DIR_DY[dir] * NORMAL_TILE_WIDTH/2;
  int start_pixel = 1;
  int has_only25 = 1;
  int num_other = 0;
  /* if they are not equal we cannot draw nicely */
  int equal = NORMAL_TILE_WIDTH == NORMAL_TILE_HEIGHT;

  if (map_src_y == map.ysize)
    abort();

  if (!first_draw) {
    /* only draw the pixel at the src one time. */
    for (dir2 = 0; dir2 < 8; dir2++) {
      if (goto_map.drawn[map_src_x][map_src_y] & (1<<dir2) && dir != dir2) {
	start_pixel = 0;
	break;
      }
    }
  }

  if (equal) { /* bit of a mess but neccesary */
    for (dir2 = 0; dir2 < 8; dir2++) {
      if (goto_map.drawn[map_src_x][map_src_y] & (1<<dir2) && dir != dir2) {
	if (dir2 != 2 && dir2 != 5) {
	  has_only25 = 0;
	  num_other++;
	}
      }
    }
    if (has_only25) {
      if (dir == 2 || dir == 5)
	start_pixel = 0;
      else
	start_pixel = 1;
    } else if (dir == 2 || dir == 5)
      start_pixel = first_draw && !(num_other == 1);

    switch (dir) {
    case 0:
    case 1:
    case 3:
      break;
    case 2:
    case 4:
      canvas_dest_x -= DIR_DX[dir];
      break;
    case 5:
    case 6:
      canvas_dest_y -= DIR_DY[dir];
      break;
    case 7:
      canvas_dest_x -= DIR_DX[dir];
      canvas_dest_y -= DIR_DY[dir];
      break;
    default:
      abort();
    }

    if (dir == 2) {
      if (start_pixel)
	gdk_draw_point(pm, line_gc, canvas_src_x, canvas_src_y);
      if (goto_map.drawn[map_src_x][map_src_y] & (1<<1) && !first_draw)
	gdk_draw_point(pm, line_gc, canvas_src_x+1, canvas_src_y);
      canvas_src_y -= 1;
    } else if (dir == 5) {
      if (start_pixel)
	gdk_draw_point(pm, line_gc, canvas_src_x, canvas_src_y);
      if (goto_map.drawn[map_src_x][map_src_y] & (1<<3) && !first_draw)
	gdk_draw_point(pm, line_gc, canvas_src_x+1, canvas_src_y);
      canvas_src_x -= 1;
    } else {
      if (!start_pixel) {
	canvas_src_x += DIR_DX[dir];
	canvas_src_y += DIR_DY[dir];
      }
      if (dir == 1 && goto_map.drawn[map_src_x][map_src_y] & (1<<2) && !first_draw)
	gdk_draw_point(pm, line_gc, canvas_src_x, canvas_src_y-1);
      if (dir == 3 && goto_map.drawn[map_src_x][map_src_y] & (1<<5) && !first_draw)
	gdk_draw_point(pm, line_gc, canvas_src_x-1, canvas_src_y);
    }
  } else { /* !equal */
    if (!start_pixel) {
      canvas_src_x += DIR_DX[dir];
      canvas_src_y += DIR_DY[dir];
    }
  }

  freelog(LOG_DEBUG, "%i->%i; x0: %i; twidth %i\n",
	  map_src_x, map_canvas_adjust_x(map_src_x),
	  map_view_x0, map_canvas_store_twidth);
  freelog(LOG_DEBUG, "%i,%i-> %i,%i : %i,%i -> %i,%i\n",
	  map_src_x, map_src_y, map_src_x + DIR_DX[dir], map_src_y + DIR_DY[dir],
	  canvas_src_x, canvas_src_y, canvas_dest_x, canvas_dest_y);

  /* draw it (at last!!) */
  gdk_draw_line(pm, line_gc, canvas_src_x, canvas_src_y, canvas_dest_x, canvas_dest_y);
}
#endif
/**************************************************************************
draw a line from src_x,src_y -> dest_x,dest_y on both map_canvas and
map_canvas_store
**************************************************************************/
void draw_segment(int src_x, int src_y, int dest_x, int dest_y)
{
#ifdef NOTYET
  int map_start_x, map_start_y;
  int dir, x, y;

  for (dir = 0; dir < 8; dir++) {
    x = map_adjust_x(src_x + DIR_DX[dir]);
    y = src_y + DIR_DY[dir];
    if (x == dest_x && y == dest_y) {
      if (!(goto_map.drawn[src_x][src_y] & (1<<dir))) {
	map_start_x = map_canvas_adjust_x(src_x) * NORMAL_TILE_WIDTH + NORMAL_TILE_WIDTH/2;
	map_start_y = map_canvas_adjust_y(src_y) * NORMAL_TILE_HEIGHT + NORMAL_TILE_HEIGHT/2;
	if (tile_visible_mapcanvas(src_x, src_y))
	  put_line(map_canvas->window, map_start_x, map_start_y, src_x, src_y, dir, 0);
	put_line(map_canvas_store, map_start_x, map_start_y, src_x, src_y, dir, 0);
	goto_map.drawn[src_x][src_y] |= (1<<dir);

	map_start_x += DIR_DX[dir] * NORMAL_TILE_WIDTH;
	map_start_y += DIR_DY[dir] * NORMAL_TILE_HEIGHT;
	if (tile_visible_mapcanvas(dest_x, dest_y))
	  put_line(map_canvas->window, map_start_x, map_start_y, dest_x, dest_y, 7-dir, 0);
	put_line(map_canvas_store, map_start_x, map_start_y, dest_x, dest_y, 7-dir, 0);
	goto_map.drawn[dest_x][dest_y] |= (1<<(7-dir));
      }
      return;
    }
  }
#endif
}

/**************************************************************************
remove the line from src_x,src_y -> dest_x,dest_y on both map_canvas and
map_canvas_store
**************************************************************************/
void undraw_segment(int src_x, int src_y, int dest_x, int dest_y)
{
#ifdef NOTYET
  int map_start_x, map_start_y;
  int dir, x, y;

  for (dir = 0; dir < 8; dir++) {
    x = map_adjust_x(src_x + DIR_DX[dir]);
    y = src_y + DIR_DY[dir];
    if (x == dest_x && y == dest_y) {
      if (goto_map.drawn[src_x][src_y] & (1<<dir)) {
	map_start_x = map_canvas_adjust_x(src_x) * NORMAL_TILE_WIDTH + NORMAL_TILE_WIDTH/2;
	map_start_y = map_canvas_adjust_y(src_y) * NORMAL_TILE_HEIGHT + NORMAL_TILE_HEIGHT/2;
	if (tile_visible_mapcanvas(src_x, src_y))
	  put_line(map_canvas->window, map_start_x, map_start_y, src_x, src_y, dir, 0);
	put_line(map_canvas_store, map_start_x, map_start_y, src_x, src_y, dir, 0);
	goto_map.drawn[src_x][src_y] &= ~(1<<dir);

	map_start_x += DIR_DX[dir] * NORMAL_TILE_WIDTH;
	map_start_y += DIR_DY[dir] * NORMAL_TILE_HEIGHT;
	if (tile_visible_mapcanvas(dest_x, dest_y))
	  put_line(map_canvas->window, map_start_x, map_start_y, dest_x, dest_y, 7-dir, 0);
	put_line(map_canvas_store, map_start_x, map_start_y, dest_x, dest_y, 7-dir, 0);
	goto_map.drawn[dest_x][dest_y] &= ~(1<<(7-dir));
      }
      return;
    }
  }
#endif
}

/**************************************************************************
...
**************************************************************************/
void create_line_at_mouse_pos(void)
{
#ifdef NOTYET
  int x, y;

  gdk_window_get_pointer(map_canvas->window, &x, &y, 0);

  update_line(x, y);
#endif
}

extern int line_dest_x; /* from goto.c */
extern int line_dest_y;
/**************************************************************************
...
**************************************************************************/
void update_line(int window_x, int window_y)
{
  LONG map_view_x0 = get_map_x_start();
  LONG map_view_y0 = get_map_y_start();
  int x, y;

  if ((hover_state == HOVER_GOTO || hover_state == HOVER_PATROL)
      && draw_goto_line) {
    x = map_adjust_x(map_view_x0 + window_x/NORMAL_TILE_WIDTH);
    y = map_adjust_y(map_view_y0 + window_y/NORMAL_TILE_HEIGHT);

    if (line_dest_x != x || line_dest_y != y) {
      undraw_line();
      draw_line(x, y);
    }
  }
}


