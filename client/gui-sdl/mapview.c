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

/**********************************************************************
                          mapview.c  -  description
                             -------------------
    begin                : Aug 10 2002
    copyright            : (C) 2002 by Rafa³ Bursig
    email                : Rafa³ Bursig <bursig@poczta.fm>
 *********************************************************************/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <assert.h>
/*#include <stdio.h>*/
#include <string.h>

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include <SDL/SDL.h>
#include <SDL/SDL_ttf.h>

#include "gui_mem.h"

#include "fcintl.h"
#include "game.h"
#include "government.h"		/* government_graphic() */
#include "log.h"
#include "map.h"
#include "player.h"
#include "rand.h"
#include "support.h"
#include "timing.h"

#include "civclient.h"
#include "climisc.h"
#include "colors.h"
#include "control.h"
#include "goto.h"

#include "chatline.h"

#include "graphics.h"
#include "gui_string.h"
#include "gui_stuff.h"
#include "gui_id.h"
#include "gui_zoom.h"
#include "gui_main.h"

#include "menu.h"
#include "mapctrl.h"
#include "options.h"
#include "tilespec.h"
#include "citydlg.h"

#include "gui_dither.h"

#include "mapview.h"

extern char *pDataPath;

/* contains the x0, y0 , col0, row0 coordinates of the upper
   center corner view block */
static int map_view_x0, map_view_y0;
static int map_view_row0, map_view_col0;

/* size of view cells block [in cells] - 
   ( map_view_rectsize x map_view_rectsize ) */
static Uint32 map_view_rectsize;

static Uint8 Mini_map_cell_w, Mini_map_cell_h;

static SDL_Surface *pFogSurface;
static SDL_Surface *pTmpSurface;
static SDL_Surface *pTmpSurface33;

static SDL_Surface *pBlinkSurfaceA;
static SDL_Surface *pBlinkSurfaceB;

static SDL_Surface *pDitherMask;
static SDL_Surface *pDitherBuf;

static void draw_map_cell(SDL_Surface * pDest, Sint16 map_x, Sint16 map_y,
			  Uint16 map_col, Uint16 map_wier, int citymode);
static int real_get_mcell_xy(SDL_Surface * pDest, int x0, int y0,
			     Uint16 col, Uint16 row, int *pX, int *pY);

#define Inc_Col( Col )			\
do {					\
  if (++Col >= map.xsize) {		\
    Col = 0;				\
  }					\
} while(0)

#define Dec_Col( Col )		\
do {				\
     if ( --Col < 0 )		\
	Col = map.xsize - 1;	\
} while(0)

#define Inc_Row( Row )		\
do {				\
    if ( ++Row >= map.ysize )	\
		Row = 0;	\
} while(0)

#define Dec_Row( Row )		\
do {				\
    if ( --Row < 0 )		\
	Row = map.ysize - 1;	\
} while(0)

/**************************************************************************
  normalize_map_pos + (y) corrections.  This function must go!
**************************************************************************/
int correction_map_pos(int *pCol, int *pRow)
{
  int iRet = 0;

  /* correction ... pCol and pRow must be in map :) */
  if (*pCol < 0) {
    *pCol += map.xsize;
    iRet = 1;
  } else {
    if (*pCol >= map.xsize) {
      *pCol -= map.xsize;
      iRet = 1;
    }
  }

  if (*pRow < 0) {
    *pRow += map.ysize;
    iRet = 1;
  } else if (*pRow >= map.ysize) {
    *pRow -= map.ysize;
    iRet = 1;
  }

  return iRet;
}

/**************************************************************************
  Finds the pixel coordinates of a tile. Save setting of the results in
  (pX, pY). Function returns TRUE whether the tile is inside the pDest
  surface.
**************************************************************************/
static int real_get_mcell_xy(SDL_Surface * pDest, int x0, int y0,
			     Uint16 col, Uint16 row, int *pX, int *pY)
{
#if 1
  *pX = x0 + (col - row) * (NORMAL_TILE_WIDTH / 2);
  *pY = y0 + (row + col) * (NORMAL_TILE_HEIGHT / 2);
#else
  /* ugly hack for speed.  Assumes:
   *   NORMAL_TILE_WIDTH == 64
   *   NORMAL_TILE_HEIGHT == 32  */
  *pX = x0 + ((col - row) << 5);
  *pY = y0 + ((row + col) << 4);
#endif

  return (*pX > -NORMAL_TILE_WIDTH && *pX < pDest->w + NORMAL_TILE_WIDTH
	  && *pY > -NORMAL_TILE_HEIGHT
	  && *pY < pDest->h + NORMAL_TILE_HEIGHT);
}

/**************************************************************************
  Finds the pixel coordinates of a tile. Save setting of the results in
  (pX, pY).  Function returns TRUE whether the tile is inside the Screen.
**************************************************************************/
static bool get_mcell_xy(Uint16 col, Uint16 row, int *pX, int *pY)
{

  int col_tmp = col - map_view_col0;
  int row_tmp = row - map_view_row0;

  correction_map_pos(&col_tmp, &row_tmp);

  return real_get_mcell_xy(Main.screen, map_view_x0, map_view_y0,
			   col_tmp, row_tmp, pX, pY);
}

/**************************************************************************
  Finds the map coordinates ( pCol, pRow )corresponding to pixel
  coordinates X ,Y.

  Runction return TRUE if is made correction.
**************************************************************************/
int get_mcell_cr(int X, int Y, int *pCol, int *pRow)
{

  float a =
      (float) ((X - NORMAL_TILE_WIDTH / 2) -
	       map_view_x0) / NORMAL_TILE_WIDTH;
  float b = (float) (Y - map_view_y0) / NORMAL_TILE_HEIGHT;

  *pCol = (float) map_view_col0 + (a + b);

  *pRow = (float) map_view_row0 + (b - a);

  return correction_map_pos(pCol, pRow);
}

/**************************************************************************
  Centers the mapview around (col, row).
  col,row are tile cordinate !

  This function callculate:	map_view_x0 , map_view_y0,
				map_view_col0, map_view_row0.

  then redraw all.
**************************************************************************/
void center_tile_mapcanvas(int col, int row)
{
  refresh_rects();

  map_view_x0 = (Main.screen->w - NORMAL_TILE_WIDTH) / 2;

  map_view_y0 = Main.screen->h - (map_view_rectsize * NORMAL_TILE_HEIGHT);

  map_view_y0 /= 2;


  map_view_row0 = row - map_view_rectsize / 2;

  /* corrections */
  if (map_view_row0 < 0) {
    map_view_row0 += map.ysize;
  }

  map_view_col0 = col - map_view_rectsize / 2;

  /* corrections */
  if (map_view_col0 < 0) {
    map_view_col0 += map.xsize;
  }

  /* redraw all */
  redraw_map_visible();

  /*update_map_canvas_scrollbars(); */
  /*refresh_overview_viewrect(); */

  if (hover_state == HOVER_GOTO || hover_state == HOVER_PATROL) {
    create_line_at_mouse_pos();
  }
}

/**************************************************************************
  ...
**************************************************************************/
void get_center_tile_mapcanvas(int *pCol, int *pRow)
{
  get_mcell_cr(Main.screen->w / 2, Main.screen->h / 2, pCol, pRow);
}

/* ===================================================================== */

/**************************************************************************
  Find if (col, row) tile is seen on screen
**************************************************************************/
bool tile_visible_mapcanvas(int col, int row)
{
  int dummy_x, dummy_y;		/* well, it needs two pointers... */
  return get_mcell_xy(col, row, &dummy_x, &dummy_y);
}

/**************************************************************************
  This function has moved.
**************************************************************************/
bool tile_visible_and_not_on_border_mapcanvas(int col, int row)
{

  int cell_x, cell_y;

  get_mcell_xy(col, row, &cell_x, &cell_y);

  return cell_x > NORMAL_TILE_WIDTH / 2
      && cell_x < Main.screen->w - 3 * NORMAL_TILE_WIDTH / 2
      && cell_y >= NORMAL_TILE_HEIGHT
      && cell_y < Main.screen->h - 3 * NORMAL_TILE_HEIGHT / 2;
}

/**************************************************************************
  Typically an info box is provided to tell the player about the state
  of their civilization.  This function is called when the label is
  changed.
**************************************************************************/
void update_info_label(void)
{
  SDL_Surface *pTmp = NULL;
  char buffer[512];
  SDL_String16 *pText = create_string16(NULL, 10);
/* const struct calendar *pCal = game_get_current_calendar(); */

  /* set text settings */
  pText->style |= TTF_STYLE_BOLD;
#if 0
  pText->forecol.r = 255;
  pText->forecol.g = 135;
  pText->forecol.b = 23;
#endif
#if 0
  pText->forecol.r = 0;
  pText->forecol.g = 0;
  pText->forecol.b = 0;
#endif
  pText->forecol.r = 255;
  pText->forecol.g = 255;
  pText->forecol.b = 255;

  my_snprintf(buffer, sizeof(buffer),
/* _("%s Population: %s Year: %s %s (%d yr/turn) "*/
	      _("%s Population: %s  Year: %s  "
		"Gold %d Tax: %d Lux: %d Sci: %d "),
	      get_nation_name(game.player_ptr->nation),
	      population_to_text(civ_population(game.player_ptr)),
/* textyear(game.year), pCal->name, pCal->turn_years, */
	      textyear(game.year),
	      game.player_ptr->economic.gold,
	      game.player_ptr->economic.tax,
	      game.player_ptr->economic.luxury,
	      game.player_ptr->economic.science);

  /* convert to unistr and create text surface */
  pText->text = convert_to_utf16(buffer);
  pTmp = create_text_surf_from_str16(pText);

  /* blit text to screen */
  blit_entire_src(pTmp, Main.screen, (Main.screen->w - pTmp->w) / 2, 5);
  FREESURFACE(pTmp);

  set_indicator_icons(client_research_sprite(),
		      client_warming_sprite(),
		      client_cooling_sprite(),
		      game.player_ptr->government);

  update_timeout_label();

  FREESTRING16(pText);
}

/**************************************************************************
  Read Function Name :)
**************************************************************************/
void redraw_unit_info_label(struct unit *pUnit, struct GUI *pInfo_Window)
{
  SDL_Rect area = { pInfo_Window->size.x, pInfo_Window->size.y, 0, 0 };
  SDL_Surface *pBuf_Surf = ResizeSurface(pTheme->Block, 30,
					 pInfo_Window->size.h -
					 DOUBLE_FRAME_WH, 1);

  Uint16 sy, len = pBuf_Surf->w;

  if (SDL_Client_Flags & CF_UNIT_INFO_SHOW) {
    /* Unit Window is Show */

    /* blit theme surface */
    SDL_BlitSurface(pInfo_Window->theme, NULL, Main.screen, &area);

    draw_frame(Main.screen, pInfo_Window->size.x, pInfo_Window->size.y,
	       pInfo_Window->size.w, pInfo_Window->size.h);

    /* blit block surface */
    area.x += FRAME_WH;
    area.y += FRAME_WH;
    SDL_BlitSurface(pBuf_Surf, NULL, Main.screen, &area);

    FREESURFACE(pBuf_Surf);

    if (pUnit) {
      char buffer[512];
      struct city *pCity = player_find_city_by_id(game.player_ptr,
						  pUnit->homecity);
      int infrastructure =
	  get_tile_infrastructure_set(map_get_tile(pUnit->x, pUnit->y));

      change_ptsize16(pInfo_Window->string16, 12);

      /* get and draw unit name (with veteran status) */
      my_snprintf(buffer, sizeof(buffer), "%s %s",
		  unit_type(pUnit)->name,
		  (pUnit->veteran) ? _("(veteran)") : "");

      FREE(pInfo_Window->string16->text);
      pInfo_Window->string16->text = convert_to_utf16(buffer);

      pBuf_Surf = create_text_surf_from_str16(pInfo_Window->string16);

      area.x = pInfo_Window->size.x
	  + (pInfo_Window->size.w - pBuf_Surf->w + len) / 2;
      area.y = pInfo_Window->size.y + DOUBLE_FRAME_WH;

      SDL_BlitSurface(pBuf_Surf, NULL, Main.screen, &area);

      sy = pInfo_Window->size.y + DOUBLE_FRAME_WH + pBuf_Surf->h;
      FREESURFACE(pBuf_Surf);


      /* draw unit sprite */
      pBuf_Surf = (SDL_Surface *) unit_type(pUnit)->sprite;
#if 0
      pBuf_Surf = ZoomSurface(pBuf_Surf2, 1.5, 1.5, 1);
      SDL_SetColorKey(pBuf_Surf, COLORKEYFLAG,
		      pBuf_Surf2->format->colorkey);
#endif

      area.x = pInfo_Window->size.x + len;
      area.y = pInfo_Window->size.y - DOUBLE_FRAME_WH
	  + (pInfo_Window->size.h - pBuf_Surf->h) / 2;

      SDL_BlitSurface(pBuf_Surf, NULL, Main.screen, &area);

      /* get and draw other info (MP, terran, city, etc.) */
      change_ptsize16(pInfo_Window->string16, 10);

      my_snprintf(buffer, sizeof(buffer), "%s\n%s\n%s%s%s",
		  (hover_unit ==
		   pUnit->
		   id) ? _("Select destination") :
		  unit_activity_text(pUnit),
		  map_get_tile_info_text(pUnit->x, pUnit->y),
		  infrastructure ?
		  map_get_infrastructure_text(infrastructure) : "",
		  infrastructure ? "\n" : "", pCity ? pCity->name : "");

      FREE(pInfo_Window->string16->text);
      pInfo_Window->string16->text = convert_to_utf16(buffer);

      pBuf_Surf = create_text_surf_from_str16(pInfo_Window->string16);

      area.x = pInfo_Window->size.x + pInfo_Window->size.w / 2 +
	  (pInfo_Window->size.w / 2 - pBuf_Surf->w - len) / 2 + 10;

      area.y = pInfo_Window->size.y
	  + (pInfo_Window->size.h - pBuf_Surf->h) / 2;

      SDL_BlitSurface(pBuf_Surf, NULL, Main.screen, &area);
    }

  } else {
    /* draw hidden */
    area.x = Main.screen->w - pBuf_Surf->w - FRAME_WH;
    area.y = Main.screen->h - pBuf_Surf->h - FRAME_WH;

    SDL_BlitSurface(pBuf_Surf, NULL, Main.screen, &area);

    draw_frame(Main.screen,
	       Main.screen->w - pBuf_Surf->w - DOUBLE_FRAME_WH,
	       Main.screen->h - pBuf_Surf->h - DOUBLE_FRAME_WH,
	       pBuf_Surf->w + DOUBLE_FRAME_WH,
	       pBuf_Surf->h + DOUBLE_FRAME_WH);


    FREESURFACE(pBuf_Surf);
  }

  /* ID_ECONOMY */
  pInfo_Window = pInfo_Window->prev;

  if (!pInfo_Window->gfx && pInfo_Window->theme) {
    pInfo_Window->gfx = crop_rect_from_screen(pInfo_Window->size);
  }

  redraw_icon(pInfo_Window);

  /* ===== */
  /* ID_RESEARCH */
  pInfo_Window = pInfo_Window->prev;

  if (!pInfo_Window->gfx && pInfo_Window->theme)
    pInfo_Window->gfx = crop_rect_from_screen(pInfo_Window->size);

  redraw_icon(pInfo_Window);
  /* ===== */
  /* ID_REVOLUTION */
  pInfo_Window = pInfo_Window->prev;

  if (!pInfo_Window->gfx && pInfo_Window->theme) {
    pInfo_Window->gfx = crop_rect_from_screen(pInfo_Window->size);
  }

  redraw_icon(pInfo_Window);

  /* ===== */
  /* ID_TOGGLE_UNITS_WINDOW_BUTTON */
  pInfo_Window = pInfo_Window->prev;

  if (!pInfo_Window->gfx && pInfo_Window->theme) {
    pInfo_Window->gfx = crop_rect_from_screen(pInfo_Window->size);
  }

  redraw_icon(pInfo_Window);
}

/**************************************************************************
  Update the information label which gives info on the current unit and
  the square under the current unit, for specified unit.  Note that in
  practice punit is always the focus unit.

  Clears label if punit is NULL.

  Typically also updates the cursor for the map_canvas (this is related
  because the info label may includes  "select destination" prompt etc).
  And it may call update_unit_pix_label() to update the icons for units
  on this square.
**************************************************************************/
void update_unit_info_label(struct unit *pUnit)
{
  struct GUI *pBuf;
  static int mutant = 0;

  /* ugly hack */
  if (SDL_Client_Flags & CF_CITY_DIALOG_IS_OPEN) {
    return;
  }

  pBuf = get_widget_pointer_form_main_list(ID_UNITS_WINDOW);

  /* draw units if window background */
  if (pBuf->gfx) {
    blit_entire_src(pBuf->gfx, Main.screen, pBuf->size.x, pBuf->size.y);
  }

  /* draw unit info window */
  redraw_unit_info_label(pUnit, pBuf);

  add_refresh_rect(pBuf->size);

  if (pUnit) {
    queue_mapview_update();
    if (hover_unit != pUnit->id)
      set_hover_state(NULL, HOVER_NONE);

    switch (hover_state) {
    case HOVER_NONE:
      if (mutant) {
	SDL_SetCursor(pStd_Cursor);
	mutant = 0;
      }
      break;
    case HOVER_PATROL:
      SDL_SetCursor(pPatrol_Cursor);
      mutant = 1;
      break;
    case HOVER_GOTO:
    case HOVER_CONNECT:
      SDL_SetCursor(pGoto_Cursor);
      mutant = 1;
      break;
    case HOVER_NUKE:
      SDL_SetCursor(pNuke_Cursor);
      mutant = 1;
      break;
    case HOVER_PARADROP:
      SDL_SetCursor(pDrop_Cursor);
      mutant = 1;
      break;
    }

  } else {
    refresh_rects();
  }

  update_unit_pix_label(pUnit);
}

void update_timeout_label(void)
{
  freelog(LOG_DEBUG, "MAPVIEW: update_timeout_label : PORT ME");
}

void update_turn_done_button(int do_restore)
{
  freelog(LOG_DEBUG, "MAPVIEW: update_turn_done_button : PORT ME");
}

/**************************************************************************
  Update (refresh) all of the city descriptions on the mapview.
**************************************************************************/
void update_city_descriptions(void)
{
  freelog(LOG_DEBUG, "MAPVIEW: update_city_descriptions : PORT ME");
  /* update_map_canvas_visible(); */
}

/**************************************************************************
  Draw a description for the given city.  (canvas_x, canvas_y) is the
  canvas position of the city itself.
**************************************************************************/
static void show_desc_at_tile(SDL_Surface * pDest, Sint16 sx, Sint16 sy,
			      Uint16 col, Uint16 row)
{
  static char buffer[512];
  struct city *pCity;
  SDL_Surface *pBuf = NULL;
  int togrow, y_offset = sy;
  SDL_String16 *pText = NULL;

  if ((pCity = map_get_city(col, row))) {
    pText = create_string16(NULL, 12);
    pText->style |= TTF_STYLE_BOLD;
    pText->forecol.r = 255;
    pText->forecol.g = 255;
    pText->forecol.b = 255;

    if (draw_city_names) {
      togrow = city_turns_to_grow(pCity);
      switch (togrow) {
      case 0:
	my_snprintf(buffer, sizeof(buffer), "%s: #", pCity->name);
	break;
      case FC_INFINITY:
	my_snprintf(buffer, sizeof(buffer), "%s: --", pCity->name);
	break;
      default:
	my_snprintf(buffer, sizeof(buffer), "%s: %d", pCity->name, togrow);
	break;
      }

      if (togrow < 0) {
	pText->forecol.g = 0;
	pText->forecol.b = 0;
      }

      pText->text = convert_to_utf16(buffer);
      pBuf = create_text_surf_from_str16(pText);

      if (togrow < 0) {
	pText->forecol.g = 255;
	pText->forecol.b = 255;
      }

      y_offset += NORMAL_TILE_HEIGHT - pBuf->h / 2;
      blit_entire_src(pBuf, pDest,
		      sx + (NORMAL_TILE_WIDTH - pBuf->w) / 2, y_offset);

      y_offset += pBuf->h;
      FREESURFACE(pBuf);

    }

    /* City Production */
    if (draw_city_productions && (pCity->owner == game.player_idx)) {
      /*pText->style &= ~TTF_STYLE_BOLD; */
      change_ptsize16(pText, 10);

      /* set text color */
      if (pCity->is_building_unit) {
	pText->forecol.r = 255;
	pText->forecol.g = 255;
	pText->forecol.b = 0;
      } else {
	if (get_improvement_type(pCity->currently_building)->is_wonder) {
	  pText->forecol.r = 0xe2;
	  pText->forecol.g = 0xc2;
	  pText->forecol.b = 0x1f;
	} else {
	  pText->forecol.r = 255;
	  pText->forecol.g = 255;
	  pText->forecol.b = 255;
	}
      }

      get_city_mapview_production(pCity, buffer, sizeof(buffer));

      FREE(pText->text);
      pText->text = convert_to_utf16(buffer);
      pBuf = create_text_surf_from_str16(pText);

      if (y_offset == sy) {
	y_offset += NORMAL_TILE_HEIGHT - pBuf->h / 2;
      } else {
	y_offset -= 3;
      }

      blit_entire_src(pBuf, pDest,
		      sx + (NORMAL_TILE_WIDTH - pBuf->w) / 2, y_offset);

      FREESURFACE(pBuf);
    }

    FREESTRING16(pText);
  }
}

/**************************************************************************
  This function has moved.
**************************************************************************/
static void show_city_descriptions(void)
{
  int map_row = map_view_row0;
  int map_col = map_view_col0;
  Uint16 col, row;
  int Sx, Sy;

  if (!draw_city_names && !draw_city_productions) {
    return;
  }

  for (col = 0; col < map_view_rectsize; col++) {
    for (row = 0; row < map_view_rectsize; row++) {

      if (real_get_mcell_xy(Main.screen, map_view_x0,
			    map_view_y0, col, row, &Sx, &Sy)) {

	show_desc_at_tile(Main.screen, Sx, Sy, map_col, map_row);

      }

      Inc_Row(map_row);
    }

    map_row = map_view_row0;
    Inc_Col(map_col);
  }
}

/**************************************************************************
  Set information for the indicator icons typically shown in the main
  client window.  The parameters tell which sprite to use for the
  indicator.
**************************************************************************/
void set_indicator_icons(int bulb, int sol, int flake, int gov)
{
  struct GUI *pBuf = NULL;

  bulb = CLIP(0, bulb, NUM_TILES_PROGRESS - 1);
  sol = CLIP(0, sol, NUM_TILES_PROGRESS - 1);
  flake = CLIP(0, flake, NUM_TILES_PROGRESS - 1);

  pBuf = get_widget_pointer_form_main_list(ID_WARMING_ICON);
  pBuf->theme = (SDL_Surface *) sprites.warming[sol];
  redraw_label(pBuf);
  add_refresh_rect(pBuf->size);

  pBuf = get_widget_pointer_form_main_list(ID_COOLING_ICON);
  pBuf->theme = (SDL_Surface *) sprites.cooling[flake];
  redraw_label(pBuf);
  add_refresh_rect(pBuf->size);

  putframe(Main.screen, pBuf->size.x - pBuf->size.w - 1,
	   pBuf->size.y - 1,
	   pBuf->size.x + pBuf->size.w,
	   pBuf->size.y + pBuf->size.h,
	   SDL_MapRGB(Main.screen->format, 255, 255, 255));

  if (SDL_Client_Flags & CF_REVOLUTION) {
    struct Sprite *sprite = NULL;
    if (game.government_count == 0) {
      /* not sure what to do here */
      sprite = get_citizen_sprite(CITIZEN_UNHAPPY, 0, NULL);
    } else {
      sprite = get_government(gov)->sprite;
    }

    pBuf = get_widget_pointer_form_main_list(ID_REVOLUTION);
    set_new_icon_theme(pBuf,
		       create_icon_theme_surf((SDL_Surface *) sprite));
    SDL_Client_Flags &= ~CF_REVOLUTION;
  }

  pBuf = get_widget_pointer_form_main_list(ID_ECONOMY);
  if (!pBuf->theme) {
    set_new_icon_theme(pBuf,
		       create_icon_theme_surf((SDL_Surface *) sprites.
					      citizen[CLIP
						      (0, 2,
						       NUM_TILES_CITIZEN -
						       1)]));
  }

  pBuf = get_widget_pointer_form_main_list(ID_RESEARCH);
  set_new_icon_theme(pBuf,
		     create_icon_theme_surf((SDL_Surface *) sprites.
					    bulb[bulb]));
}

/* ===================================================================== */
/* =============================== Mini Map ============================ */
/* ===================================================================== */

/**************************************************************************
  Set the dimensions for the map overview, in map units (tiles).
  Typically each tile will be a 2x2 rectangle, although this may vary.
**************************************************************************/
void set_overview_dimensions(int w, int h)
{

  struct GUI *pMMap =
      get_widget_pointer_form_main_list(ID_MINI_MAP_WINDOW);

  freelog(LOG_DEBUG,
	  "MAPVIEW: 'set_overview_dimensions' call with x = %d  y = %d", w,
	  h);

  if (map.xsize > 160) {
    Mini_map_cell_w = 1;
  } else {
    Mini_map_cell_w = 160 / w;
  }

  if (map.ysize > 100) {
    Mini_map_cell_h = 1;
  } else {
    Mini_map_cell_h = 100 / h;
  }


  FREESURFACE(pMMap->theme);


  pMMap->theme = create_surf(160, 100, SDL_SWSURFACE);


  enable(ID_TOGGLE_UNITS_WINDOW_BUTTON);

  enable(ID_TOGGLE_MAP_WINDOW_BUTTON);
  enable(ID_CHATLINE_TOGGLE_LOG_WINDOW_BUTTON);
  enable(ID_REVOLUTION);
  enable(ID_ECONOMY);
  enable(ID_RESEARCH);
  enable(ID_FIND_CITY);

  /* del init string */
  pMMap = get_widget_pointer_form_main_list(ID_WAITING_LABEL);

  if (pMMap) {
    del_widget_from_gui_list(pMMap);
  }

  /* load city resource gfx ( should be ported to specfile code ) */
  load_city_icons();
}

/**************************************************************************
  Update the tile for the given map position on the overview.
**************************************************************************/
void overview_update_tile(int col, int row)
{
  struct city *pCity = NULL;
  struct GUI *pMMap =
      get_widget_pointer_form_main_list(ID_MINI_MAP_WINDOW);
  SDL_Rect cell_size = { col * Mini_map_cell_w, row * Mini_map_cell_h,
    Mini_map_cell_w, Mini_map_cell_h
  };
  Uint32 color;

  freelog(LOG_DEBUG, "MAPVIEW: overview_update_tile (x = %d y = %d )", col,
	  row);


  if (tile_get_known(col, row) == TILE_UNKNOWN) {
    SDL_FillRect(pMMap->theme, &cell_size, 0);
    return;
  } else {
    if (map_get_terrain(col, row) == T_OCEAN) {
      color = get_game_color(COLOR_STD_OCEAN);
    } else {
      pCity = map_get_city(col, row);
      if (pCity) {
	color = get_game_color(COLOR_STD_RACE0 + pCity->owner);
      } else {
	color = get_game_color(COLOR_STD_GROUND);
      }

    }
  }

  SDL_FillRect(pMMap->theme, &cell_size, color);
}

/**************************************************************************
  Refresh (update) the entire map overview.
**************************************************************************/
void refresh_overview_canvas(void)
{

  SDL_Rect cell_size = { 0, 0, Mini_map_cell_w, Mini_map_cell_h };

  Uint32 ocean_color;
  Uint32 terran_color;

  Uint16 col = 0, row = 0;

  struct GUI *pMMap =
      get_widget_pointer_form_main_list(ID_MINI_MAP_WINDOW);
  struct city *pCity = NULL;


  /* pMMap->theme == overview_canvas_store */

  if (pMMap->theme) {
    SDL_FillRect(pMMap->theme, NULL, 0);
  } else {
    pMMap->theme = create_surf(160, 100, SDL_SWSURFACE);
  }

  ocean_color = get_game_color(COLOR_STD_OCEAN);

  terran_color = get_game_color(COLOR_STD_GROUND);

  while (TRUE) {		/* mini map draw loop */

    if (tile_get_known(col, row) == TILE_UNKNOWN) {
      SDL_FillRect(pMMap->theme, &cell_size, 0);
    } else {
      if (map_get_terrain(col, row) == T_OCEAN) {
	SDL_FillRect(pMMap->theme, &cell_size, ocean_color);
      } else {
	pCity = map_get_city(col, row);
	if (pCity) {
	  SDL_FillRect(pMMap->theme, &cell_size,
		       get_game_color(COLOR_STD_RACE0 + pCity->owner));
#if 0
	  color = get_game_color(8 + 2);
	  SDL_FillRect(pMMap->theme, &cell_size,
		       SDL_MapRGB(pMMap->theme->format, color.r, color.g,
				  color.b));
#endif
	} else {
	  SDL_FillRect(pMMap->theme, &cell_size, terran_color);
	}
      }
    }

    cell_size.x += Mini_map_cell_w;
    col++;

    if (col == map.xsize) {
      cell_size.y += Mini_map_cell_h;
      cell_size.x = 0;
      row++;
      if (row == map.ysize) {
	break;
      }
      col = 0;
    }

  }				/* end mini map draw loop */
}

/**************************************************************************
  draw line on mini map.
  this algoritm is far from optim.
**************************************************************************/
static void putline_on_mini_map(SDL_Rect * pMapArea, Sint16 x0, Sint16 y0,
				Sint16 x1, Sint16 y1, Uint32 color)
{
  register float m;
  register int x, y;
  int xloop, w = (x1 - x0);


  m = (float) (y1 - y0) / w;

  if (x0 < 0) {
    x0 += pMapArea->w;
  } else if (x0 >= pMapArea->w) {
    x0 -= pMapArea->w;
  }

  if (x1 < 0) {
    x1 += pMapArea->w;
  } else if (x1 >= pMapArea->w) {
    x1 -= pMapArea->w;
  }

  if (y0 < 0) {
    y0 += pMapArea->h;
  } else if (y0 >= pMapArea->h) {
    y0 -= pMapArea->h;
  }

  lock_surf(Main.screen);

  for (xloop = 0; xloop < w; xloop++) {

    x = x0 + xloop;

    y = m * (x - x0) + y0;

    if (x >= pMapArea->w) {
      x -= pMapArea->w;
    }

    if (y < 0) {
      y += pMapArea->h;
    } else if (y >= pMapArea->h) {
      y -= pMapArea->h;
    }

    x += pMapArea->x;
    y += pMapArea->y;

    putpixel(Main.screen, x, y, color);

    x -= pMapArea->x;

  }

  unlock_surf(Main.screen);
}

/**************************************************************************
  Refresh (update) the viewrect on the overview. This is the rectangle
  showing the area covered by the mapview.
**************************************************************************/
void refresh_overview_viewrect(void)
{

  int Nx, Ny, Ex, Ey, Wx, Wy, Sx, Sy, map_w, map_h;

  Uint32 color;

  struct GUI *pMMap =
      get_widget_pointer_form_main_list(ID_MINI_MAP_WINDOW);
  struct GUI *pBuf = pMMap->prev;


  SDL_Rect map_area =
      { FRAME_WH, Main.screen->h - pMMap->theme->h - FRAME_WH,
    pMMap->theme->w, pMMap->theme->h
  };


  SDL_Surface *pBuf_Surf = NULL;

  pBuf_Surf =
      ResizeSurface(pTheme->Block, 30, pMMap->size.h - DOUBLE_FRAME_WH, 2);

  if (SDL_Client_Flags & CF_MINI_MAP_SHOW) {

    SDL_BlitSurface(pMMap->theme, NULL, Main.screen, &map_area);


    /* The x's and y's are in overview coordinates. */

    map_w = (Main.screen->w + NORMAL_TILE_WIDTH - 1) / NORMAL_TILE_WIDTH;
    map_h = (Main.screen->h + NORMAL_TILE_HEIGHT - 1) / NORMAL_TILE_HEIGHT;

    get_mcell_cr(0, 0, &Wx, &Wy);	/* take from Main Map */

    Wx *= Mini_map_cell_w;
    Wy *= Mini_map_cell_h;

    Nx = Wx + Mini_map_cell_w * map_w;
    Ny = Wy - Mini_map_cell_h * map_w;
    Sx = Wx + Mini_map_cell_w * map_h;
    Sy = Wy + Mini_map_cell_h * map_h;
    Ex = Nx + Mini_map_cell_w * map_h;
    Ey = Ny + Mini_map_cell_h * map_h;

    color = SDL_MapRGB(pMMap->theme->format, 255, 255, 255);

    putline_on_mini_map(&map_area, Nx, Ny, Ex, Ey, color);

    putline_on_mini_map(&map_area, Sx, Sy, Ex, Ey, color);

    putline_on_mini_map(&map_area, Wx, Wy, Sx, Sy, color);

    putline_on_mini_map(&map_area, Wx, Wy, Nx, Ny, color);

    freelog(LOG_DEBUG, "wx,wy: %d,%d nx,ny:%d,%x ex,ey:%d,%d, sx,sy:%d,%d",
	    Wx, Wy, Nx, Ny, Ex, Ey, Sx, Sy);

    draw_frame(Main.screen, 0, Main.screen->h - pMMap->size.h,
	       pMMap->size.w, pMMap->size.h);

    /* ===== */


    map_area.x = pMMap->size.w - pBuf_Surf->w - FRAME_WH;
    map_area.y = Main.screen->h - pMMap->size.h + FRAME_WH;

    SDL_BlitSurface(pBuf_Surf, NULL, Main.screen, &map_area);

    FREESURFACE(pBuf_Surf);
    /* ===== */


    /* redraw widgets */

    /* ID_NEW_TURN */
    if (!pBuf->gfx) {
      pBuf->gfx = crop_rect_from_screen(pBuf->size);
    }

    redraw_icon(pBuf);

    /* ===== */
    /* ID_CHATLINE_TOGGLE_LOG_WINDOW_BUTTON */
    pBuf = pBuf->prev;

    if (!pBuf->gfx) {
      pBuf->gfx = crop_rect_from_screen(pBuf->size);
    }

    redraw_icon(pBuf);

    /* ===== */
    /* ID_FIND_CITY */
    pBuf = pBuf->prev;

    if (!pBuf->gfx) {
      pBuf->gfx = crop_rect_from_screen(pBuf->size);
    }

    redraw_icon(pBuf);


    /* ===== */
    /* ID_TOGGLE_MAP_WINDOW_BUTTON */
    pBuf = pBuf->prev;

    if (!pBuf->gfx && pBuf->theme) {
      pBuf->gfx = crop_rect_from_screen(pBuf->size);
    }

    redraw_icon(pBuf);


    add_refresh_rect(pMMap->size);

  } else {			/* map hiden */


    map_area.x = FRAME_WH;
    map_area.y = Main.screen->h - pMMap->size.h + FRAME_WH;

    SDL_BlitSurface(pBuf_Surf, NULL, Main.screen, &map_area);

    draw_frame(Main.screen, 0, map_area.y - FRAME_WH,
	       pBuf_Surf->w + DOUBLE_FRAME_WH,
	       pBuf_Surf->h + DOUBLE_FRAME_WH);

    FREESURFACE(pBuf_Surf);

    /* ID_NEW_TURN */
    redraw_icon(pBuf);

    /* ID_CHATLINE_TOGGLE_LOG_WINDOW_BUTTON */
    pBuf = pBuf->prev;
    redraw_icon(pBuf);

    /* ID_FIND_CITY */
    pBuf = pBuf->prev;
    redraw_icon(pBuf);

    /* ID_TOGGLE_MAP_WINDOW_BUTTON */
    pBuf = pBuf->prev;
    redraw_icon(pBuf);


    add_refresh_rect(pMMap->size);
  }
}

/* ===================================================================== */

/**************************************************************************
  Draw City's surfaces to 'pDest' on position 'map_x' , 'map_y'.
**************************************************************************/
static void put_city_pixmap_draw(struct city *pCity, SDL_Surface * pDest,
				 Sint16 map_x, Sint16 map_y)
{
  SDL_Surface *pSprites[80];
  SDL_Rect src, des = { map_x, map_y, 0, 0 };
  int i;
  int count =
      fill_city_sprite_array_iso((struct Sprite **) pSprites, pCity);

  des.x += NORMAL_TILE_WIDTH / 4 + NORMAL_TILE_WIDTH / 8;
  des.y += NORMAL_TILE_HEIGHT / 4;
  /* blit flag/shield */
  src = get_smaller_surface_rect(pSprites[0]);
  SDL_BlitSurface(pSprites[0], &src, pDest, &des);

  des.x = map_x;
  des.y = map_y;
  for (i = 1; i < count; i++) {
    if (pSprites[i]) {
      SDL_BlitSurface(pSprites[i], NULL, pDest, &des);
    }
  }
}

/**************************************************************************
  Draw Unit's surfaces to 'pDest' on position 'map_x' , 'map_y'.
**************************************************************************/
void put_unit_pixmap_draw(struct unit *pUnit, SDL_Surface * pDest,
			  Sint16 map_x, Sint16 map_y)
{
  SDL_Surface *pSprites[10];
  SDL_Rect src, des = { map_x, map_y, 0, 0 };
  int dummy;

  int count =
      fill_unit_sprite_array((struct Sprite **) pSprites, pUnit, &dummy);

  des.x += NORMAL_TILE_WIDTH / 4;

  /* blit hp bar */
  src = get_smaller_surface_rect(pSprites[count - 1]);
  SDL_BlitSurface(pSprites[count - 1], &src, pDest, &des);

  des.y += src.h;
  des.x++;
  /* blit flag/shield */
  src = get_smaller_surface_rect(pSprites[0]);
  SDL_BlitSurface(pSprites[0], &src, pDest, &des);

  des.x = map_x;
  des.y = map_y;
  for (dummy = 1; dummy < count - 1; dummy++) {
    if (pSprites[dummy]) {
      SDL_BlitSurface(pSprites[dummy], NULL, pDest, &des);
    }
  }
}

/**************************************************************************
  This function draw 1x1 [map_col]x[map_row] map cell to 'pDest' surface
  on map_x , map_y position.
**************************************************************************/
static void draw_map_cell(SDL_Surface * pDest, Sint16 map_x, Sint16 map_y,
			  Uint16 map_col, Uint16 map_row, int citymode)
{
  SDL_Surface *pTile_sprs[80];
  SDL_Surface *pCoasts[4] = { NULL, NULL, NULL, NULL };
  SDL_Surface *pDither[4] = { NULL, NULL, NULL, NULL };

  SDL_Surface *pBufSurface = NULL;
  /* SDL_Surface *pTemp = NULL; */

  SDL_Rect des = { map_x, map_y, 0, 0 };

  struct city *pCity = NULL;
  struct unit *pUnit = NULL, *pFocus = NULL;
  enum tile_special_type special;
  int count, i = 0;
  int fog, outside = 0;
  int solid_bg;

  count = fill_tile_sprite_array_iso((struct Sprite **) pTile_sprs,
				     (struct Sprite **) pCoasts,
				     (struct Sprite **) pDither,
				     map_col, map_row, citymode,
				     &solid_bg);

  if (count == -1) {		/* tile is unknown */
    SDL_BlitSurface((SDL_Surface *) sprites.black_tile,
		    NULL, Main.screen, &des);
    return;
  }

  /* Replace with check for is_normal_tile later */
  assert(is_real_tile(map_col, map_row));

  /* normalize_map_pos(&map_col, &map_wier); */

  fog = tile_get_known(map_col, map_row) == TILE_KNOWN_FOGGED
      && draw_fog_of_war;
  pCity = map_get_city(map_col, map_row);
  pUnit = get_drawable_unit(map_col, map_row, citymode);
  pFocus = get_unit_in_focus();
  special = map_get_special(map_col, map_row);

  if (map_x < 0 || map_y < 0) {
    outside = 1;
  }

  if (fog || outside) {
    des.x = 0;
    des.y = NORMAL_TILE_HEIGHT / 2;
    pBufSurface = pTmpSurface;
  } else {
    pBufSurface = pDest;
  }

  if (draw_terrain) {
    if (map_get_terrain(map_col, map_row) == T_OCEAN) {	/* coasts */
      /* top */
      des.x += NORMAL_TILE_WIDTH / 4;
      SDL_BlitSurface(pCoasts[0], NULL, pBufSurface, &des);

      /* bottom */
      des.y += NORMAL_TILE_HEIGHT / 2;
      SDL_BlitSurface(pCoasts[1], NULL, pBufSurface, &des);

      /* left */
      des.x -= NORMAL_TILE_WIDTH / 4;
      des.y -= NORMAL_TILE_HEIGHT / 4;
      SDL_BlitSurface(pCoasts[2], NULL, pBufSurface, &des);

      /* right */
      des.x += NORMAL_TILE_WIDTH / 2;
      SDL_BlitSurface(pCoasts[3], NULL, pBufSurface, &des);

      if (fog || outside) {
	des.x = 0;
	des.y = NORMAL_TILE_HEIGHT / 2;
      } else {
	des.x = map_x;
	des.y = map_y;
      }
    } else {
      SDL_BlitSurface(pTile_sprs[0], NULL, pBufSurface, &des);
      i++;
    }

    /*** Dither base terrain ***/


    /* north */
    if (pDither[0]) {
      dither_north(pDither[0], pDitherMask, pDitherBuf);
    }

    /* south */
    if (pDither[1]) {
      dither_south(pDither[1], pDitherMask, pDitherBuf);
    }

    /* east */
    if (pDither[2]) {
      dither_east(pDither[2], pDitherMask, pDitherBuf);
    }

    /* west */
    if (pDither[3]) {
      dither_west(pDither[3], pDitherMask, pDitherBuf);
    }

    if (pDither[0] || pDither[1] || pDither[2] || pDither[3]) {
      SDL_BlitSurface(pDitherBuf, NULL, pBufSurface, &des);

      SDL_FillRect(pDitherBuf, NULL, 0);
    }
  }

    /*** Rest of terrain and specials ***/
  for (; i < count; i++) {
    if (pTile_sprs[i]) {
      if (pTile_sprs[i]->w - NORMAL_TILE_WIDTH > 0
	  || pTile_sprs[i]->h - NORMAL_TILE_HEIGHT > 0) {
	des.x -= (pTile_sprs[i]->w - NORMAL_TILE_WIDTH);
	des.y -= (pTile_sprs[i]->h - NORMAL_TILE_HEIGHT);
	SDL_BlitSurface(pTile_sprs[i], NULL, pBufSurface, &des);
	if ((fog) || (outside)) {
	  des.x = 0;
	  des.y = NORMAL_TILE_HEIGHT / 2;
	} else {
	  des.x = map_x;
	  des.y = map_y;
	}
      } else {
	SDL_BlitSurface(pTile_sprs[i], NULL, pBufSurface, &des);
      }
    } else {
      freelog(LOG_ERROR, _("sprite is NULL"));
    }
  }

    /*** Map grid ***/
  if (draw_map_grid) {
    /* we draw the 2 lines on top of the tile; the buttom lines will be
       drawn by the tiles underneath. */
    Uint32 grid_color = 64;
#if 0
    Uint32 grid_color = SDL_MapRGB(pBufSurface->format, 168, 168, 168);
#endif
    putline(pBufSurface, des.x, des.y + NORMAL_TILE_HEIGHT / 2,
	    des.x + NORMAL_TILE_WIDTH / 2, des.y, grid_color);

    putline(pBufSurface, des.x + NORMAL_TILE_WIDTH / 2, des.y,
	    des.x + NORMAL_TILE_WIDTH,
	    des.y + NORMAL_TILE_HEIGHT / 2, grid_color);
  }

  if (draw_coastline && !draw_terrain) {
#if 0
    enum tile_terrain_type t1 = map_get_terrain(x, y), t2;
    int x1, y1;
    gdk_gc_set_foreground(thin_line_gc, colors_standard[COLOR_STD_OCEAN]);
    x1 = x;
    y1 = y - 1;
    if (normalize_map_pos(&x1, &y1)) {
      t2 = map_get_terrain(x1, y1);
      if (draw & D_M_R && ((t1 == T_OCEAN) ^ (t2 == T_OCEAN)))
	gdk_draw_line(pm, thin_line_gc,
		      canvas_x + NORMAL_TILE_WIDTH / 2, canvas_y,
		      canvas_x + NORMAL_TILE_WIDTH,
		      canvas_y + NORMAL_TILE_HEIGHT / 2);
    }
    x1 = x - 1;
    y1 = y;
    if (normalize_map_pos(&x1, &y1)) {
      t2 = map_get_terrain(x1, y1);
      if (draw & D_M_L && ((t1 == T_OCEAN) ^ (t2 == T_OCEAN)))
	gdk_draw_line(pm, thin_line_gc,
		      canvas_x, canvas_y + NORMAL_TILE_HEIGHT / 2,
		      canvas_x + NORMAL_TILE_WIDTH / 2, canvas_y);
    }
#endif
  }

  /*** City and various terrain improvements ***/
  if (contains_special(special, S_FORTRESS) && draw_fortress_airbase) {
    SDL_BlitSurface((SDL_Surface *) sprites.tx.fortress_back,
		    NULL, pBufSurface, &des);
  }

  if (pCity && draw_cities) {
    put_city_pixmap_draw(pCity, pBufSurface,
			 des.x, des.y - NORMAL_TILE_HEIGHT / 2);
  }

  if (contains_special(special, S_AIRBASE) && draw_fortress_airbase) {
    SDL_BlitSurface((SDL_Surface *) sprites.tx.airbase,
		    NULL, pBufSurface, &des);
  }

  if (contains_special(special, S_FALLOUT) && draw_pollution) {
    SDL_BlitSurface((SDL_Surface *) sprites.tx.fallout,
		    NULL, pBufSurface, &des);
  }

  if (contains_special(special, S_POLLUTION) && draw_pollution) {
    SDL_BlitSurface((SDL_Surface *) sprites.tx.pollution,
		    NULL, pBufSurface, &des);
  }

    /*** city size ***/
  /* Not fogged as it would be unreadable */
  if (pCity && draw_cities) {
    if (pCity->size >= 10) {
      SDL_BlitSurface((SDL_Surface *) sprites.city.
		      size_tens[pCity->size / 10], NULL, pBufSurface,
		      &des);
    }

    SDL_BlitSurface((SDL_Surface *) sprites.city.size[pCity->size % 10],
		    NULL, pBufSurface, &des);
  }

    /*** Unit ***/
  if (pUnit && (draw_units || (pUnit == pFocus && draw_focus_unit))) {
    put_unit_pixmap_draw(pUnit, pBufSurface, des.x,
			 des.y - NORMAL_TILE_HEIGHT / 2);

    if (!pCity
	&& unit_list_size(&map_get_tile(map_col, map_row)->units) > 1) {
      SDL_BlitSurface((SDL_Surface *) sprites.unit.stack, NULL,
		      pBufSurface, &des);
    }
  }

  if (contains_special(special, S_FORTRESS) && draw_fortress_airbase) {
    assert((SDL_Surface *) sprites.tx.fortress);
    SDL_BlitSurface((SDL_Surface *) sprites.tx.fortress,
		    NULL, pBufSurface, &des);
  }


  if (fog || outside) {
    if (fog) {
      SDL_BlitSurface(pFogSurface, NULL, pBufSurface, NULL);
    }

    des.x = map_x;
    des.y = map_y - NORMAL_TILE_HEIGHT / 2;
    SDL_BlitSurface(pBufSurface, NULL, pDest, &des);

    /* clear pBufSurface */
    /* if BytesPerPixel == 4 and Amask == 0 ( 24 bit coding ) alpha blit 
     * functions Set A = 255 in all pixels and then pixel val 
     * ( r=0, g=0, b=0) != 0.  Real val of this pixel is 0xff000000. */
    if (pBufSurface->format->BytesPerPixel == 4
	&& !pBufSurface->format->Amask) {
      SDL_FillRect(pBufSurface, NULL, 0xff000000);
    } else {
      SDL_FillRect(pBufSurface, NULL, 0);
    }
  }
}

/**************************************************************************
  Refresh and draw to 'pDest' surface all the tiles in a rectangde (start
  from col0,row0) width,height (as seen in overhead view) with the top
  left map corrner pixel pos. at x0 , y0.

  'real' parametr determinate loop algoritm:
    real = 0:  loop start from 0 ( col = 0/row = 0 ) and end on
               width/height.
    real = 1:  loop start from col0/row0 ( col = col0/row = row0 ) and
               end on (col0 + width)/(row0 + height).

  This trik is important to start pixel position calculation.
  Example:

    1)  If you want use this funtion with general start map left top
        corner position parametrs: 'map_x0' , 'map_y0' , you must set
        'real' = 1.  ( see 'update_map_canvas(void)' funtion)

    2)  If you want use this funtion with local start map position
        parametrs,  you must set 'real' = 0. ( see
        'real_update_single_map_canvas(...)' funtion)
**************************************************************************/
static void real_update_map_canvas(SDL_Surface * pDest, int x0, int y0,
				   int col0, int row0, int width,
				   int height, int real)
{

  register Uint16 col = 0, row;

  Uint16 StRow = 0, EndCol = width, EndRow = height;
  Uint16 real_col = col0, real_row = row0;
  int sx, sy;

  freelog(LOG_DEBUG,
	  "real_update_map_canvas( pos=(%d,%d), size=(%d,%d) )",
	  col0, row0, width, height);

  /* set start loop param. */
  if (real) {
    col = col0;
    StRow = row0;
    EndCol = col0 + width;
    EndRow = row0 + height;
  }

  /* draw loop */
  for (; col < EndCol; col++) {
    for (row = StRow; row < EndRow; row++) {
      /* calculate start pixel position and check if it belong to 'pDest' */
      if (real_get_mcell_xy(pDest, x0, y0, col, row, &sx, &sy)) {

	/* draw map cell */
	draw_map_cell(pDest, sx, sy, real_col, real_row, 0);

      }

      Inc_Row(real_row);
    }
    real_row = row0;
    Inc_Col(real_col);
  }
}

/*
 *
 */
/**************************************************************************
  This function draw 3x3 map cells rect to 'pTmpSurface33' surface.
  To Main.screen is only blit (col0,row0) center tile from 'pTmpSurface33'
  surface. if 'pTmpSurface33' will be draw on : Log, MiniMap , UnitWindow
  area then:
    - clean under this widegets
    - blit 'pTmpSurface33'.
    - refresh widget background.
    - redraw widgets
**************************************************************************/
static void real_update_single_map_canvas(int col0, int row0,
					  bool write_to_screen)
{

  int sx, sy;
  SDL_Rect buf_rect, dest;
  struct GUI *pLog;
  struct GUI *pUnitsW;
  struct GUI *pMinimap;
  int log_colision = 0, unit_colision = 0, mmap_colision = 0;

  /* ugly hack */
  if (SDL_Client_Flags & CF_CITY_DIALOG_IS_OPEN) {
    return;
  }

  pLog = get_widget_pointer_form_main_list(ID_CHATLINE_WINDOW);
  pUnitsW = get_widget_pointer_form_main_list(ID_UNITS_WINDOW);
  pMinimap = get_widget_pointer_form_main_list(ID_MINI_MAP_WINDOW);

  freelog(LOG_DEBUG,
	  "MAPVIEW: update_singel_map_canvas : col = %d row = %d", col0,
	  row0);

  get_mcell_xy(col0, row0, &sx, &sy);

  Dec_Col(col0);
  Dec_Row(row0);

  /* redraw 3x3 map tile to 'pTmpSurface33' surface */
  real_update_map_canvas(pTmpSurface33,
			 (pTmpSurface33->w - NORMAL_TILE_WIDTH) / 2, 0,
			 col0, row0, 3, 3, 0);

  /* redraw city names and productions */
  if (draw_city_names || draw_city_productions) {
    int real_col = col0;
    int real_row = row0;
    int col, row, Sx, Sy;

    for (col = 0; col < 3; col++) {
      for (row = 0; row < 3; row++) {

	if (real_get_mcell_xy(pTmpSurface33,
			      (pTmpSurface33->w - NORMAL_TILE_WIDTH) / 2,
			      0, col, row, &Sx, &Sy)) {

	  show_desc_at_tile(pTmpSurface33, Sx, Sy, real_col, real_row);

	}

	Inc_Row(real_row);
      }
      real_row = row0;
      Inc_Col(real_col);
    }
  }

  /* set blit area in Main.screen */
  dest.x = sx;
  dest.y = sy - NORMAL_TILE_HEIGHT / 2;

  dest.w = NORMAL_TILE_WIDTH;
  dest.h = NORMAL_TILE_HEIGHT + NORMAL_TILE_HEIGHT / 2;

  correct_rect_region(&dest);

  /* detect colisions with windows */
  if (!(get_wflags(pLog) & WF_HIDDEN)) {
    log_colision = detect_rect_colisions(&pLog->size, &dest);
  }

  unit_colision = detect_rect_colisions(&pUnitsW->size, &dest);
  mmap_colision = detect_rect_colisions(&pMinimap->size, &dest);

  /* undraw log window */
  if (log_colision) {
    buf_rect.x = pLog->size.x;
    buf_rect.y = pLog->size.y;
    SDL_BlitSurface(pLog->gfx, NULL, Main.screen, &buf_rect);
  }

  /* undraw unit window */
  if (unit_colision && (SDL_Client_Flags & CF_UNIT_INFO_SHOW)) {
    buf_rect.x = Main.screen->w - pUnitsW->size.w;
    buf_rect.y = pUnitsW->size.y;
    SDL_BlitSurface(pUnitsW->gfx, NULL, Main.screen, &buf_rect);
  }

  /* undraw mimi map window */
  if (mmap_colision) {
    buf_rect.x = pMinimap->size.x;
    buf_rect.y = pMinimap->size.y;
    SDL_BlitSurface(pMinimap->gfx, NULL, Main.screen, &buf_rect);
  }

  SDL_SetClipRect(Main.screen, &dest);

  /* blit 3x3 map to Main.screen */
  buf_rect.x = sx - NORMAL_TILE_WIDTH;
  buf_rect.y = sy - NORMAL_TILE_HEIGHT;

  SDL_BlitSurface(pTmpSurface33, NULL, Main.screen, &buf_rect);

  /* clear ClipRect */
  SDL_SetClipRect(Main.screen, NULL);

  /* redraw units info window */
  if (unit_colision) {

    if (SDL_Client_Flags & CF_UNIT_INFO_SHOW) {
      pUnitsW->gfx = crop_rect_from_screen(pUnitsW->size);
    }

    redraw_unit_info_label(get_unit_in_focus(), pUnitsW);
  }

  /* redraw mini map window */
  if (mmap_colision) {
    refresh_widget_background(pMinimap);
    refresh_overview_viewrect();
  }

  /* redraw log window */
  if (log_colision) {
    Redraw_Log_Window(2);
  }

  /* clear pBufSurface33 */
  SDL_FillRect(pTmpSurface33, NULL, 0);

  if (write_to_screen) {
    add_refresh_rect(dest);
  }
}

/**************************************************************************
  Refresh (update) the entire map overview.
**************************************************************************/
/**************************************************************************
  Update (refresh) the map canvas starting at the given tile (in map
  coordinates) and with the given dimensions (also in map coordinates).

  This function contains a lot of the drawing logic.  It is very similar
  in each GUI - except this one.

  I leave this function to be compatible with high client API,
  in fact use this function only to width = 1 and height = 1.
  In all other sytuations use 'real_update_map_canvas(...)'.
**************************************************************************/
void update_map_canvas(int col, int row, int width, int height,
		       bool write_to_screen)
{
  if ((width == 1) && (height == 1))
    real_update_single_map_canvas(col, row, write_to_screen);
  else {
#if 0
    SDL_Rect Dest;
    get_mcell_xy(col, row, (int *) &Dest.x, (int *) &Dest.y);
    real_update_map_canvas(Main.screen, map_x0, map_y0,
			   col, row, width, height, 1);
    Dest.w = width * NORMAL_TILE_WIDTH;
    Dest.h = height * NORMAL_TILE_HEIGHT;
    Dest.x -= (Dest.w - NORMAL_TILE_WIDTH) / 2;
    if (correct_rect_region(&Dest)) {
      refresh_rect(Dest);
    }
#endif
  }
}

/**************************************************************************
...
**************************************************************************/
/* not used jet */
void real_blink_active_unit(void)
{
  static bool is_shown, oldCol, oldRow;
  static struct unit *pPrevUnit = NULL;
  struct unit *pUnit;
  SDL_Rect area = { 0, 0, NORMAL_TILE_WIDTH, 1.5 * NORMAL_TILE_HEIGHT };

  if ((pUnit = get_unit_in_focus())) {
    get_mcell_xy(pUnit->x, pUnit->y, (int *) &area.x, (int *) &area.y);
    area.y -= NORMAL_TILE_HEIGHT / 2;
    if (pUnit == pPrevUnit && pUnit->x == oldCol && pUnit->y == oldRow) {
      if (is_shown) {
	SDL_BlitSurface(pBlinkSurfaceA, NULL, Main.screen, &area);
	refresh_rect(area);
      } else {
	SDL_BlitSurface(pBlinkSurfaceB, NULL, Main.screen, &area);
	refresh_rect(area);
      }
      is_shown = !is_shown;
    } else {
      set_focus_unit_hidden_state(TRUE);
      refresh_tile_mapcanvas(pUnit->x, pUnit->y, FALSE);
      SDL_BlitSurface(Main.screen, &area, pBlinkSurfaceA, NULL);
      /*     
         refresh_rects();
         is_shown = 0;
       */
      set_focus_unit_hidden_state(FALSE);
      refresh_tile_mapcanvas(pUnit->x, pUnit->y, FALSE);
      SDL_BlitSurface(Main.screen, &area, pBlinkSurfaceB, NULL);

      pPrevUnit = pUnit;
      oldCol = pUnit->x;
      oldRow = pUnit->y;
    }

  }
}

/**************************************************************************
  Redraw ALL.
  All seen widgets are draw upon map then must be redraw too.
**************************************************************************/
void redraw_map_visible(void)
{
  struct GUI *pBuf = NULL;
#ifdef LOG_DEBUG
  Uint32 t1, t2;
#endif
  /* just find a big rectangle that includes the whole visible area. The
     invisible tiles will not be drawn. */

  /* clear screen */
  SDL_FillRect(Main.screen, NULL, 0);

#ifdef LOG_DEBUG
  t1 = SDL_GetTicks();
#endif

  /* redraw map */
  real_update_map_canvas(Main.screen, map_view_x0, map_view_y0,
			 map_view_col0, map_view_row0, map_view_rectsize,
			 map_view_rectsize, 0);
#ifdef LOG_DEBUG
  t2 = SDL_GetTicks();

  freelog(LOG_DEBUG, _("Redraw TIME = %d"), t2 - t1);
#endif

  /* redraw city descriptions */
  show_city_descriptions();

  /* redraw minimap */
  refresh_ID_background(ID_MINI_MAP_WINDOW);
  refresh_overview_viewrect();

  /* redraw units window */
  pBuf = get_widget_pointer_form_main_list(ID_UNITS_WINDOW);

  refresh_widget_background(pBuf);

  redraw_unit_info_label(get_unit_in_focus(), pBuf);

  /* redraw info label */
  update_info_label();


  if (get_unit_in_focus()) {
    update_order_widget();
  }


  /* redraw Options Icon */
  pBuf = get_widget_pointer_form_main_list(ID_CLIENT_OPTIONS);
  refresh_widget_background(pBuf);
  redraw_icon(pBuf);

  /* redraw Log window */
  Redraw_Log_Window(2);

  if (!(SDL_Client_Flags & CF_CITY_DIALOG_IS_OPEN)) {
    /*refresh_city_dlg_background();
       }
       else
       { */
    refresh_fullscreen();
  }
}

/*
 */
/**************************************************************************
  Rerfesh ALL.

  The hack is after all packets have been read call
  'update_map_canvas_visible'.  The functions track which areas of the
  screen need updating and refresh them all at one.

  Problem is "the redraw of the city descriptions bug" is back :(
**************************************************************************/
void update_map_canvas_visible(void)
{
  freelog(LOG_DEBUG, "MAPVIEW: update_map_canvas_visible");
  refresh_rects();
}

/**************************************************************************
  Update (refresh) the locations of the mapview scrollbars (if it uses
  them).
**************************************************************************/
void update_map_canvas_scrollbars(void)
{
  /* No scrollbars. */
}

/**************************************************************************
  Draw a single frame of animation.  This function needs to clear the old
  image and draw the new one.  It must flush output to the display.
**************************************************************************/
void draw_unit_animation_frame(struct unit *punit, int frame_number,
			       int old_canvas_x, int old_canvas_y,
			       int new_canvas_x, int new_canvas_y)
{
  SDL_Rect src =
      { old_canvas_x, old_canvas_y, UNIT_TILE_WIDTH, UNIT_TILE_HEIGHT };
  SDL_Rect dest =
      { new_canvas_x, new_canvas_y, UNIT_TILE_WIDTH, UNIT_TILE_HEIGHT };
  static SDL_Surface *pMap_Copy, *pUnit_Surf;

  if (first_frame) {
    /* Create extra backing stores. */
    pMap_Copy =
	create_surf(UNIT_TILE_WIDTH, UNIT_TILE_HEIGHT, SDL_SWSURFACE);
    pUnit_Surf =
	create_surf(UNIT_TILE_WIDTH, UNIT_TILE_HEIGHT, SDL_SWSURFACE);
    SDL_SetColorKey(pUnit_Surf, SDL_SRCCOLORKEY, 0x0);
    put_unit_pixmap_draw(pUnit, pUnit_Surf, 0, 0);
  }

  /* Clear old sprite. */
  SDL_BlitSurface(pMap_Copy, NULL, Main.screen, &src);
  add_refresh_rect(src);

  /* Draw the new sprite. */
  SDL_BlitSurface(Main.screen, &dest, pMap_Copy, NULL);
  SDL_BlitSurface(pUnit_Surf, NULL, Main.screen, &dest);
  add_refresh_rect(dest);

  /* Write to screen. */
  refresh_rects();

  if (last_frame) {
    FREESURFACE(pMap_Copy);
    FREESURFACE(pUnit_Surf);
  }
}

/**************************************************************************
 This function is called to decrease a unit's HP smoothly in battle when
 combat_animation is turned on.
**************************************************************************/
void decrease_unit_hp_smooth(struct unit *punit0, int hp0,
			     struct unit *punit1, int hp1)
{

  static struct timer *anim_timer = NULL;
  struct unit *losing_unit = (hp0 == 0 ? punit0 : punit1);
  int i, map_x, map_y;
  SDL_Rect src =
      { 0, 0, NORMAL_TILE_WIDTH, 1.5 * NORMAL_TILE_HEIGHT }, dest;

  set_units_in_combat(punit0, punit1);

  do {
    anim_timer = renew_timer_start(anim_timer, TIMER_USER, TIMER_ACTIVE);

    if (punit0->hp > hp0
	&& myrand((punit0->hp - hp0) + (punit1->hp - hp1)) <
	punit0->hp - hp0) {
      punit0->hp--;
    } else {
      if (punit1->hp > hp1) {
	punit1->hp--;
      } else {
	punit0->hp--;
      }
    }

    refresh_tile_mapcanvas(punit0->x, punit0->y, TRUE);
    refresh_tile_mapcanvas(punit1->x, punit1->y, TRUE);
    refresh_rects();

    usleep_since_timer_start(anim_timer, 10000);

  } while (punit0->hp > hp0 || punit1->hp > hp1);


  if (num_tiles_explode_unit &&
      get_mcell_xy(losing_unit->x, losing_unit->y, &map_x, &map_y)) {
    /* copy screen area */
    src.x = map_x;
    src.y = map_y;
    SDL_BlitSurface(Main.screen, &src, pTmpSurface, NULL);

    dest.y = map_y;
    for (i = 0; i < num_tiles_explode_unit; i++) {

      anim_timer = renew_timer_start(anim_timer, TIMER_USER, TIMER_ACTIVE);

      /* blit explosion */
      dest.x = map_x + NORMAL_TILE_WIDTH / 4;

      SDL_BlitSurface((SDL_Surface *) sprites.explode.unit[i],
		      NULL, Main.screen, &dest);
      refresh_rect(dest);

      usleep_since_timer_start(anim_timer, 20000);

      /* clear explosion */
      dest.x = map_x;
      SDL_BlitSurface(pTmpSurface, NULL, Main.screen, &dest);
    }
  }

  refresh_rect(dest);

  /* clear pTmpSurface */
  if ((pTmpSurface->format->BytesPerPixel == 4) &&
      (!pTmpSurface->format->Amask)) {
    SDL_FillRect(pTmpSurface, NULL, 0xff000000);
  } else {
    SDL_FillRect(pTmpSurface, NULL, 0x0);
  }

  set_units_in_combat(NULL, NULL);
  /*
     refresh_tile_mapcanvas(punit0->x, punit0->y, TRUE);
     refresh_tile_mapcanvas(punit1->x, punit1->y, TRUE);
   */
}

/**************************************************************************
  Draw a cross-hair overlay on a tile.
**************************************************************************/
void put_cross_overlay_tile(int x, int y)
{
  freelog(LOG_DEBUG, "MAPVIEW: put_cross_overlay_tile : PORT ME");
}

/**************************************************************************
  Draw in information about city workers on the mapview in the given
  color.
**************************************************************************/
void put_city_workers(struct city *pcity, int color)
{
  freelog(LOG_DEBUG, "MAPVIEW: put_city_workers : PORT ME");
}

/**************************************************************************
  Draw a nuke mushroom cloud at the given tile.
**************************************************************************/
void put_nuke_mushroom_pixmaps(int x, int y)
{
  freelog(LOG_DEBUG, "MAPVIEW: put_nuke_mushroom_pixmaps : PORT ME");
}

/**************************************************************************
  Draw a segment (e.g., a goto line) from the given tile in the given
  direction.
**************************************************************************/
void draw_segment(int src_x, int src_y, int dir)
{
  freelog(LOG_DEBUG, "MAPVIEW: draw_segment : PORT ME");
}

/**************************************************************************
  ...
**************************************************************************/
void undraw_segment(int src_x, int src_y, int dir)
{
  freelog(LOG_DEBUG, "MAPVIEW: undraw_segment : PORT ME");
}

/* =====================================================================
				City MAP
   ===================================================================== */

/**************************************************************************
  ...
**************************************************************************/
SDL_Surface *create_city_map(struct city *pCity)
{

  register Uint16 col = 0, row;
  SDL_Rect dest;
  int draw_units_backup = draw_units;
  int draw_map_grid_backup = draw_map_grid;
  int sx, sy, row0, real_col = pCity->x, real_row = pCity->y;

  Sint16 x0 = 3 * NORMAL_TILE_WIDTH / 2;
  Sint16 y0 = -NORMAL_TILE_HEIGHT / 2;
  SDL_Surface *pTile = NULL;
  SDL_Surface *pDest = create_surf(4 * NORMAL_TILE_WIDTH,
				   4 * NORMAL_TILE_HEIGHT,
				   SDL_SWSURFACE);
  real_col -= 2;
  real_row -= 2;
  correction_map_pos((int *) &real_col, (int *) &real_row);
  row0 = real_row;

  /* turn off drawing units and map_grid */
  draw_units = 0;
  draw_map_grid = 0;

  /* draw loop */
  for (; col < CITY_MAP_SIZE; col++) {
    for (row = 0; row < CITY_MAP_SIZE; row++) {
      /* calculate start pixel position and check if it belong to 'pDest' */
      if (real_get_mcell_xy(pDest, x0, y0, col, row, &sx, &sy)) {
	dest.x = sx;
	dest.y = sy;
	if ((!col && !row) ||
	    (!col && row == CITY_MAP_SIZE - 1) ||
	    (!row && col == CITY_MAP_SIZE - 1) ||
	    (col == CITY_MAP_SIZE - 1 && row == CITY_MAP_SIZE - 1)) {
	  /* draw black corners */

	  SDL_BlitSurface((SDL_Surface *) sprites.black_tile,
			  NULL, pDest, &dest);
	} else {
	  /* draw map cell */
	  draw_map_cell(pDest, dest.x, dest.y, real_col, real_row, 0);
	  if (get_worker_city(pCity, col, row) == C_TILE_UNAVAILABLE &&
	      map_get_terrain(real_col, real_row) != T_UNKNOWN) {
	    if (!pTile) {
	      /* make mask */
	      SDL_BlitSurface((SDL_Surface *) sprites.black_tile,
			      NULL, pTmpSurface, NULL);

	      SDL_SetColorKey(pTmpSurface, SDL_SRCCOLORKEY,
			      getpixel(pTmpSurface, NORMAL_TILE_WIDTH / 2,
				       NORMAL_TILE_HEIGHT / 2));
	      /* create pTile */
	      pTile = create_surf(NORMAL_TILE_WIDTH,
				  NORMAL_TILE_HEIGHT, SDL_SWSURFACE);
	      /* fill pTile with RED */
	      SDL_FillRect(pTile, NULL,
			   SDL_MapRGBA(pTile->format, 255, 0, 0, 96));

	      /* blit mask */
	      SDL_BlitSurface(pTmpSurface, NULL, pTile, NULL);

	      SDL_SetColorKey(pTile, SDL_SRCCOLORKEY | SDL_RLEACCEL,
			      getpixel(pTile, 0, 0));

	      SDL_SetAlpha(pTile, SDL_SRCALPHA, 96);

	      /* clear pTmpSurface */
	      if (pTmpSurface->format->BytesPerPixel == 4 &&
		  !pTmpSurface->format->Amask) {
		SDL_SetColorKey(pTmpSurface, SDL_SRCCOLORKEY, 0xff000000);
		SDL_FillRect(pTmpSurface, NULL, 0xff000000);
	      } else {
		SDL_SetColorKey(pTmpSurface, SDL_SRCCOLORKEY, 0x0);
		SDL_FillRect(pTmpSurface, NULL, 0);
	      }
	    }

	    SDL_BlitSurface(pTile, NULL, pDest, &dest);

	  }
	}

      }

      Inc_Row(real_row);
    }
    real_row = row0;
    Inc_Col(real_col);
  }

  draw_units = draw_units_backup;
  draw_map_grid = draw_map_grid_backup;

  FREESURFACE(pTile);

  return pDest;
}

/* =====================================================================
				Init Functions
   ===================================================================== */

/**************************************************************************
  This Function is used when resize Main.screen.
  We must set new 'map_view_rectsize' value.
**************************************************************************/
void get_new_view_rectsize(void)
{
  float w = (float) Main.screen->w / NORMAL_TILE_WIDTH;
  float h = (float) Main.screen->h / NORMAL_TILE_HEIGHT - w;
  map_view_rectsize = 2 * w + h;

  if (!div(map_view_rectsize, 2).rem) {
    map_view_rectsize++;
  }
}

/**************************************************************************
  ...
**************************************************************************/
void tmp_map_surfaces_init(void)
{
  char cBuf[80];

  pFogSurface = create_surf(NORMAL_TILE_WIDTH,
			    3 * NORMAL_TILE_HEIGHT / 2,
			    SDL_SWSURFACE);
  SDL_FillRect(pFogSurface, NULL,
	       SDL_MapRGBA(pFogSurface->format, 0, 0, 0, 96));
  SDL_SetAlpha(pFogSurface, SDL_SRCALPHA, 96);

  pTmpSurface = create_surf(NORMAL_TILE_WIDTH,
			    3 * NORMAL_TILE_HEIGHT / 2,
			    SDL_SWSURFACE);
  SDL_SetAlpha(pTmpSurface, 0x0, 0x0);

  /* if BytesPerPixel == 4 and Amask == 0 ( 24 bit coding ) alpha blit
   * functions; Set A = 255 in all pixels and then pixel val
   *  ( r=0, g=0, b=0) != 0.  Real val this pixel is 0xff000000. */
  if (pTmpSurface->format->BytesPerPixel == 4
      && !pTmpSurface->format->Amask) {
    SDL_SetColorKey(pTmpSurface, SDL_SRCCOLORKEY, 0xff000000);
  } else {
    SDL_SetColorKey(pTmpSurface, SDL_SRCCOLORKEY, 0x0);
  }

  pTmpSurface33 =
      create_surf(3 * NORMAL_TILE_WIDTH, 3 * NORMAL_TILE_HEIGHT,
		  SDL_SWSURFACE);
  SDL_SetAlpha(pTmpSurface33, 0x0, 0x0);
  SDL_SetColorKey(pTmpSurface33, SDL_SRCCOLORKEY, 0x0);

#if 0
  pBlinkSurfaceA =
      create_surf(NORMAL_TILE_WIDTH, 1.5 * NORMAL_TILE_HEIGHT,
		  SDL_SWSURFACE);
  pBlinkSurfaceB =
      create_surf(NORMAL_TILE_WIDTH, 1.5 * NORMAL_TILE_HEIGHT,
		  SDL_SWSURFACE);
#endif

  get_new_view_rectsize();


  pDitherBuf =
      create_surf(NORMAL_TILE_WIDTH, NORMAL_TILE_HEIGHT, SDL_SWSURFACE);
  SDL_SetColorKey(pDitherBuf, SDL_SRCCOLORKEY, 0x0);

  sprintf(cBuf, "%s%s", pDataPath, "/theme/default/dither_mask.png");

  pDitherMask = load_surf(cBuf);
#if 0
  SDL_SetColorKey(pDitherMask, SDL_SRCCOLORKEY,
		  getpixel(pDitherMask, pDitherMask->w / 2,
			   pDitherMask->h / 2));
#endif
}
