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

#define DRAW_TIMING

#include "gui_mem.h"

#include "fcintl.h"
#include "game.h"
#include "government.h"		/* government_graphic() */
#include "log.h"
#include "map.h"
#include "player.h"
#include "rand.h"
#include "support.h"

#ifdef DRAW_TIMING
#include "timing.h"
#endif

#include "civclient.h"
#include "climap.h"
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
#include "gui_tilespec.h"

#include "menu.h"
#include "mapctrl.h"
#include "options.h"
#include "tilespec.h"
#include "citydlg.h"

#include "gui_dither.h"
#include "timing.h"
#include "mapview.h"

extern char *pDataPath;

/* contains the x0, y0 , coordinates of the upper
   left corner view block */
static int map_view_x0, map_view_y0;

/* size of view cells block [in cells] - 
   ( map_view_rectsize x map_view_rectsize ) */
static Uint32 map_view_rectsize;

static Uint8 Mini_map_cell_w, Mini_map_cell_h;

static SDL_Surface *pFogSurface;
static SDL_Surface *pTmpSurface;


#if 0
static SDL_Surface *pBlinkSurfaceA;
static SDL_Surface *pBlinkSurfaceB;
#endif

#if 0
static SDL_Surface *pDitherMask;
static SDL_Surface *pDitherBuf;
#endif

static void draw_map_cell(SDL_Surface * pDest, Sint16 map_x, Sint16 map_y,
			  Uint16 map_col, Uint16 map_wier, int citymode);
			  
static int real_get_mcell_xy(SDL_Surface * pDest, int x0, int y0,
			     Uint16 col, Uint16 row, int *pX, int *pY);


static void redraw_map_widgets(void);


static struct CELL_MAP {
	SDL_Surface *sprite;
	Uint16 x,y;
} **map_cell;

static int cell_w_count = 0, cell_h_count = 0;

/*static SDL_Surface *cell[T_LAST][T_LAST][5][2];*/
#define MAX_CORNER  4
#define MAX_BLEND   4
static SDL_Surface *cells[T_LAST][T_LAST][MAX_BLEND][MAX_CORNER];

SDL_Surface *pDitherMasks[4];/* MAX_BLEND */

static void fill_map_cells( int x0 , int y0 );
static void redraw_fullscreen( int x0 , int y0 );
static void redraw_single_cell( int x , int y );
static void reset_map_cells(void);

static const int ISO_DIR_DX[8] = { -1, -1,  0, -1,  1, 0, 1, 1 };
static const int ISO_DIR_DY[8] = {  0, -1, -1,  1, -1, 1, 1, 0 };

static const int ISO_CAR_DIR_DX[4] = { 1, 1, -1, -1};
static const int ISO_CAR_DIR_DY[4] = {-1, 1,  1, -1};


/* ================================================================ */

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
  This function can be used by mapview_common code to determine the
  location and dimensions of the mapview canvas.
**************************************************************************/
void get_mapview_dimensions(int *map_view_topleft_map_x,
			    int *map_view_topleft_map_y,
			    int *map_view_pixel_width,
			    int *map_view_pixel_height)
{
  *map_view_topleft_map_x = map_view_x0;
  *map_view_topleft_map_y = map_view_y0;
  *map_view_pixel_width = Main.screen->w;
  *map_view_pixel_height = Main.screen->h;
}

#if 0
/**************************************************************************
  Draw the given map tile at the given canvas position in no-isometric
  view use with unification of update_mapcanvas(...).
**************************************************************************/
void put_one_tile( int map_x , int map_y , int canvas_x , int canvas_y )
{
}

/**************************************************************************
  Draw the given map tile at the given canvas position in isometric
  view use with unification of update_mapcanvas(...).
**************************************************************************/
void put_one_tile_iso(int x, int y, int canvas_x, int canvas_y,
		      enum draw_type draw)
{
  SDL_Rect dest;	
  int height, width;
  int offset_x, offset_y;

/*
  if (!tile_visible_mapcanvas(x, y)) {
    freelog(LOG_DEBUG, "dropping %d,%d", x, y);
    return;
  }
*/
	
  if (!normalize_map_pos(&x, &y)) {
    blit_entire_src( (SDL_Surface *)sprites.black_tile ,
                       Main.screen , canvas_x , canvas_y );
    return;  
  }
  
  freelog(LOG_DEBUG, "putting %d,%d draw %x", x, y, draw);

  if ( draw == D_MB_LR || draw == D_FULL )
  {
    draw_map_cell(Main.screen, canvas_x, canvas_y,
			  (Uint16)x, (Uint16)y, 1);
    return;
  }
  
  width = (draw & D_TMB_L) && (draw & D_TMB_R)
        ? NORMAL_TILE_WIDTH : NORMAL_TILE_WIDTH>>1;
  
  if (!(draw & D_TMB_L))
    offset_x = NORMAL_TILE_WIDTH>>1;
  else
    offset_x = 0;

  height = 0;
  if (draw & D_M_LR) height += (NORMAL_TILE_HEIGHT>>1);
  if (draw & D_B_LR) height += (NORMAL_TILE_HEIGHT>>1);
  if (draw & D_T_LR) height += (NORMAL_TILE_HEIGHT>>1);
	  
  if ((draw & D_T_LR))
    offset_y = - (NORMAL_TILE_HEIGHT>>1);
  else
    offset_y = (draw & D_M_LR) ? 0 : NORMAL_TILE_HEIGHT>>1;
  
  dest.x = canvas_x + offset_x;
  dest.y = canvas_y + offset_y;
  dest.w = width;
  dest.h = height;
  SDL_SetClipRect(Main.screen, &dest);
  
  draw_map_cell(Main.screen, canvas_x, canvas_y,
			  (Uint16)x, (Uint16)y, 1 );
  
  /* clear ClipRect */
  SDL_SetClipRect(Main.screen, NULL);
 
  return;
}

void flush_mapcanvas( int canvas_x , int canvas_y ,
		     int pixel_width , int pixel_height )
{
   refresh_screen( canvas_x, canvas_y,
      (Uint16)pixel_width, (Uint16)pixel_height );	
   /* refresh_rects();
      refresh_fullscreen(); */
}
#endif

/* ======================================================== */

/**************************************************************************
  normalize_map_pos + (y) corrections.  This function must go to drink!
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
  Centers the mapview around (col, row).
  col,row are tile cordinate !

  This function callculate:	map_view_x0 , map_view_y0,
				map_view_col0, map_view_row0.

  then redraw all.
**************************************************************************/
void center_tile_mapcanvas(int col, int row)
{
  
    int ww = ( Main.screen->w - 1 ) / NORMAL_TILE_WIDTH + 1;
    int hh = ( Main.screen->h - 1 ) / NORMAL_TILE_HEIGHT + 1;
	
    refresh_rects();	
	
    base_center_tile_mapcanvas( col , row, &map_view_x0 , &map_view_y0 ,
					ww , hh );

    reset_map_cells();  
    fill_map_cells( map_view_x0 , map_view_y0 );
  
    update_map_canvas_visible();
  
    refresh_fullscreen();
  


  /*update_map_canvas_scrollbars(); */
  /*refresh_overview_viewrect(); */

  if (hover_state == HOVER_GOTO || hover_state == HOVER_PATROL) {
    create_line_at_mouse_pos();
  }
}

/* ===================================================================== */

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
      pBuf_Surf = (SDL_Surface *)unit_type(pUnit)->sprite;
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

  redraw_icon2(pInfo_Window);

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

void update_turn_done_button(bool do_restore)
{
  freelog(LOG_DEBUG, "MAPVIEW: update_turn_done_button : PORT ME");
}

/**************************************************************************
  Update (refresh) all of the city descriptions on the mapview.
**************************************************************************/
void update_city_descriptions(void)
{
  freelog(LOG_DEBUG, "MAPVIEW: update_city_descriptions : PORT ME");
  
}

/**************************************************************************
  Draw a description for the given city onto the surface.
**************************************************************************/
static void put_city_desc_on_surface(SDL_Surface *pDest,
				     struct city *pcity,
				     int canvas_x, int canvas_y)
{
  static char buffer[512];
  SDL_Surface *pBuf = NULL , *pCity_Name = NULL, *pCity_Prod = NULL;
  int togrow;
  SDL_String16 *pText = NULL;
  SDL_Rect dst, clear_area = { 0 , 0 , 0 , 0 };	
  SDL_Color color = *( get_game_colorRGB( player_color( get_player(pcity->owner) ) ));	
  Uint32 frame_color = SDL_MapRGB( pDest->format, color.r ,color.g, color.b );
  
  color.unused = 128;
  
  pText = create_string16(NULL, 10);
  pText->style |= TTF_STYLE_BOLD;
  pText->forecol.r = 255;
  pText->forecol.g = 255;
  pText->forecol.b = 255;

  canvas_y += NORMAL_TILE_HEIGHT;

  if (draw_city_names) {
    if (draw_city_growth && pcity->owner == game.player_idx) {
      togrow = city_turns_to_grow(pcity);
      switch (togrow) {
      case 0:
	my_snprintf(buffer, sizeof(buffer), "%s: #", pcity->name);
	break;
      case FC_INFINITY:
	my_snprintf(buffer, sizeof(buffer), "%s: --", pcity->name);
	break;
      default:
	my_snprintf(buffer, sizeof(buffer), "%s: %d", pcity->name, togrow);
	break;
      }
    } else {
      /* Force certain behavior below. */
      togrow = 0;
      my_snprintf(buffer, sizeof(buffer), "%s\n%s", pcity->name , 
	    get_nation_name(get_player(pcity->owner)->nation) );
    }

    if (togrow < 0) {
      pText->forecol.g = 0;
      pText->forecol.b = 0;
    }

    pText->text = convert_to_utf16(buffer);
    pCity_Name = create_text_surf_from_str16(pText);

    if (togrow < 0) {
      pText->forecol.g = 255;
      pText->forecol.b = 255;
    }
  }

  /* City Production */
  if (draw_city_productions && pcity->owner == game.player_idx) {
    /*pText->style &= ~TTF_STYLE_BOLD; */
    change_ptsize16(pText, 9);

    /* set text color */
    if (pcity->is_building_unit) {
      pText->forecol.r = 255;
      pText->forecol.g = 255;
      pText->forecol.b = 0;
    } else {
      if (get_improvement_type(pcity->currently_building)->is_wonder) {
	pText->forecol.r = 0xe2;
	pText->forecol.g = 0xc2;
	pText->forecol.b = 0x1f;
      } else {
	pText->forecol.r = 255;
	pText->forecol.g = 255;
	pText->forecol.b = 255;
      }
    }

    get_city_mapview_production(pcity, buffer, sizeof(buffer));

    FREE(pText->text);
    pText->text = convert_to_utf16(buffer);
    pCity_Prod = create_text_surf_from_str16(pText);
    
  }
  else
  {
    if ( !pCity_Name && ( SDL_Client_Flags & CF_CIV3_CITY_TEXT_STYLE ) )
    {
      my_snprintf(buffer, sizeof(buffer), "%s", 
	    get_nation_name(get_player(pcity->owner)->nation) );
	    
      FREE(pText->text);
      pText->text = convert_to_utf16(buffer);
      pCity_Prod = create_text_surf_from_str16(pText);    
    }
  }
  
  
  if ( SDL_Client_Flags & CF_CIV3_CITY_TEXT_STYLE )
  {
  
    /* city size */
    if ( pCity_Name || pCity_Prod ) {
      my_snprintf( buffer , sizeof( buffer ) , "%d" , pcity->size );
      pText->text = convert_to_utf16( buffer );
      pText->style |= TTF_STYLE_BOLD;
      change_ptsize16(pText, 10);
      pBuf = create_text_surf_from_str16( pText );
	    
      clear_area.y = canvas_y;    
    }
                
    if ( pCity_Name )
    {
      clear_area.w = pCity_Name->w + 4;
      clear_area.h = pCity_Name->h;
    }
  
    if ( pCity_Prod )
    {
      clear_area.h += pCity_Prod->h;
      if (clear_area.w < pCity_Prod->w)
      {
        clear_area.w = pCity_Prod->w + 4;
      }
    
    }
  
    clear_area.h += 2;
    
    if ( pBuf )
    {
      /* this isn't a error */	    
      clear_area.w += pBuf->h + 1;
      if ( clear_area.h < pBuf->h )
      {
        clear_area.h = pBuf->h + 2;
      }    
    }
    
    if ( clear_area.w )
    {
      clear_area.x = canvas_x + (NORMAL_TILE_WIDTH - clear_area.w) / 2;
    }
  
    if ( pBuf )
    {
  
#if 0
      /* solid */	    
      dst = clear_area;	    
      SDL_FillRect( pDest, &dst , 0x0 );
      dst.w = pBuf->h + 1;	    
      SDL_FillRect( pDest, &dst , frame_color );
#else
     
      dst = clear_area;
      dst.w = pBuf->h + 1;
      /* citi size background */
      SDL_FillRectAlpha( pDest , &dst , &color );
    
    
      dst.x = clear_area.x + pBuf->h + 2; 
      dst.w = clear_area.w - pBuf->h - 2;
	    
      /* text background */
      SDL_FillRectAlpha( pDest, &dst , &((SDL_Color){0,0,0,80}) );
#endif

      blit_entire_src(pBuf, pDest,
		    clear_area.x + ( pBuf->h - pBuf->w) / 2,
		    canvas_y + ( clear_area.h - pBuf->h ) / 2);

      /* Horizontal lines */
      putline( pDest , clear_area.x , clear_area.y - 1 ,
			  clear_area.x + clear_area.w ,
                          clear_area.y - 1 , frame_color );						 
      putline( pDest , clear_area.x  , clear_area.y + clear_area.h,
			  clear_area.x + clear_area.w ,
                          clear_area.y + clear_area.h , frame_color );

      /* vertical lines */
      putline( pDest , clear_area.x + clear_area.w, clear_area.y ,
			  clear_area.x + clear_area.w ,
                          clear_area.y + clear_area.h  , frame_color );
      putline( pDest , clear_area.x - 1, clear_area.y  ,
			  clear_area.x - 1,
                          clear_area.y + clear_area.h  , frame_color );
			  
      putline( pDest , clear_area.x + 1 + pBuf->h, clear_area.y  ,
			  clear_area.x + 1 + pBuf->h,
                          clear_area.y + clear_area.h  , frame_color );
    }
    
    if ( pCity_Name )
    {
      blit_entire_src(pCity_Name, pDest,
		    clear_area.x + pBuf->h + 2 +
			  (clear_area.w - (pBuf->h + 2) - pCity_Name->w) / 2,
		    canvas_y);

      canvas_y += pCity_Name->h;
    }
  
    if ( pCity_Prod )
    {
      blit_entire_src(pCity_Prod, pDest,
		    clear_area.x + pBuf->h + 2 +
			  (clear_area.w - (pBuf->h + 2) - pCity_Prod->w) / 2,
		    canvas_y);
    } 
  }
  else
  {
    if ( pCity_Name )
    {
      blit_entire_src(pCity_Name, pDest,
		    canvas_x + ( NORMAL_TILE_WIDTH - pCity_Name->w) / 2,
		    canvas_y);

      canvas_y += pCity_Name->h;
    }
  
    if ( pCity_Prod )
    {
      blit_entire_src(pCity_Prod, pDest,
		    canvas_x +
			  (NORMAL_TILE_WIDTH - pCity_Prod->w) / 2,
		    canvas_y);
    }
    
  }
  
  FREESURFACE(pCity_Prod);
  FREESURFACE(pCity_Name);
  FREESURFACE(pBuf);
  FREESTRING16(pText);
}

/**************************************************************************
  Draw a description for the given city.  (canvas_x, canvas_y) is the
  canvas position of the city itself.
**************************************************************************/
void show_city_desc(struct city *pcity, int canvas_x, int canvas_y)
{
  put_city_desc_on_surface(Main.screen, pcity,
                     canvas_x, canvas_y - NORMAL_TILE_HEIGHT / 6 );
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
#if 0
  pBuf = get_widget_pointer_form_main_list(ID_ECONOMY);
  
  
#endif
  
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
    "MAPVIEW: 'set_overview_dimensions' call with x = %d  y = %d", w, h);

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
  
  init_cells_sprites();
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

  while (TRUE) { /* mini map draw loop */

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

  } /* end mini map draw loop */
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

    get_map_xy(0, 0, &Wx, &Wy);	/* take from Main Map */
	  
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

  } else {/* map hiden */


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
  SDL_Rect src , des = { map_x, map_y, 0, 0 };
  int i;
  int count =
      fill_city_sprite_array_iso((struct Sprite **) pSprites, pCity);
  
  if ( !(SDL_Client_Flags & CF_CIV3_CITY_TEXT_STYLE) )
  {
    src = get_smaller_surface_rect(pSprites[0]);	  
    des.x += NORMAL_TILE_WIDTH / 4 + NORMAL_TILE_WIDTH / 8;
    des.y += NORMAL_TILE_HEIGHT / 4;
      
    /* blit flag/shield */
    SDL_BlitSurface(pSprites[0], &src, pDest, &des);
  }
      
  des.x = map_x;
  des.y = map_y;
  src = des;
  for (i = 1; i < count; i++) {
    if (pSprites[i]) {
      SDL_BlitSurface(pSprites[i], NULL, pDest, &des);
      des = src;	    
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
  SDL_Rect copy, des = { map_x, map_y, 0, 0 };
  static SDL_Rect src_hp = {0,0,0,0};
  static SDL_Rect src_flag = {0,0,0,0};
  int dummy;

  int count =
      fill_unit_sprite_array((struct Sprite **) pSprites, pUnit, &dummy);

  des.x += NORMAL_TILE_WIDTH / 4;

  /* blit hp bar */
  if ( !src_hp.x )
  {
    src_hp = get_smaller_surface_rect(pSprites[count - 1]);
  }
  copy = des;
  SDL_BlitSurface(pSprites[count - 1], &src_hp, pDest, &des);

  des = copy;
  des.y += src_hp.h;
  des.x++;
  
  /* blit flag/shield */
  if ( !src_flag.x )
  {
    src_flag = get_smaller_surface_rect(pSprites[0]);
  }
  
  SDL_BlitSurface(pSprites[0], &src_flag, pDest, &des);

  des.x = map_x;
  des.y = map_y;
  copy = des;    
  for (dummy = 1; dummy < count - 1; dummy++) {
    if (pSprites[dummy]) {
      SDL_BlitSurface(pSprites[dummy], NULL, pDest, &des);
      des = copy;	    
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
  
  static SDL_Surface *pTemp = NULL;

  SDL_Rect dst , des = { map_x, map_y, 0, 0 };

  struct city *pCity = NULL;
  struct unit *pUnit = NULL, *pFocus = NULL;
  enum tile_special_type special;
  int count, i = 0;
  int fog;
  int solid_bg;
  
  count = fill_tile_sprite_array_iso((struct Sprite **) pTile_sprs,
				     (struct Sprite **) pCoasts,
				     (struct Sprite **) pDither,
				     map_col, map_row, citymode,
				     &solid_bg);
    				     

  if ((count == -1)&&citymode) { /* tile is unknown */
    SDL_BlitSurface((SDL_Surface *) sprites.black_tile,
		    NULL, Main.screen, &des);
  
    return;
  }

#if 0
  /* Replace with check for is_normal_tile later */
  assert(is_real_tile(map_col, map_row));
#endif
  
  /* normalize_map_pos(&map_col, &map_wier); */

  fog = tile_get_known(map_col, map_row) == TILE_KNOWN_FOGGED
      && draw_fog_of_war;
  pCity = map_get_city(map_col, map_row);
  pUnit = get_drawable_unit(map_col, map_row, citymode);
  pFocus = get_unit_in_focus();
  special = map_get_special(map_col, map_row);
  
  /*if ( fog ) {
    des.x = 0;
    des.y = NORMAL_TILE_HEIGHT / 2;
    pBufSurface = pTmpSurface;
  } else {*/
    pBufSurface = pDest;
  /*}*/

  dst = des;
  
  if ( !pTemp )
  {
    pTemp = create_surf(NORMAL_TILE_WIDTH,
			    NORMAL_TILE_HEIGHT ,
			    SDL_SWSURFACE);
	
    SDL_FillRect(pTemp, NULL,
	       SDL_MapRGB(pTemp->format, 255, 255, 255));

    SDL_BlitSurface( (SDL_Surface *) sprites.black_tile , NULL , pTemp , NULL );

    SDL_SetColorKey(pTemp, SDL_SRCCOLORKEY | SDL_RLEACCEL, 
	       SDL_MapRGB(pTemp->format, 255, 255, 255));
	  
    SDL_SetAlpha(pTemp, SDL_SRCALPHA, 128);
	  
  }
  
  if (draw_terrain) {
   if (citymode) {	  
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
      
      des = dst;
    } else {
      SDL_BlitSurface(pTile_sprs[0], NULL, pBufSurface, &des);
      i++;
    }
   }
   else
    i++;

    /*** Dither base terrain ***/

#if 0
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
#endif    
  }

    /*** Rest of terrain and specials ***/
  for (; i < count; i++) {
    if (pTile_sprs[i]) {
      if (pTile_sprs[i]->w - NORMAL_TILE_WIDTH > 0
	  || pTile_sprs[i]->h - NORMAL_TILE_HEIGHT > 0) {
	des.x -= (pTile_sprs[i]->w - NORMAL_TILE_WIDTH);
	des.y -= (pTile_sprs[i]->h - NORMAL_TILE_HEIGHT);
	SDL_BlitSurface(pTile_sprs[i], NULL, pBufSurface, &des);
      } else {
	SDL_BlitSurface(pTile_sprs[i], NULL, pBufSurface, &des);
      }
      des = dst;
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
  /*if (contains_special(special, S_FORTRESS) && draw_fortress_airbase) {*/
  if ((special & S_FORTRESS) && draw_fortress_airbase) {
    SDL_BlitSurface((SDL_Surface *) sprites.tx.fortress_back,
		    NULL, pBufSurface, &des);
    des = dst;	  
  }

  if (pCity && draw_cities) {
    put_city_pixmap_draw(pCity, pBufSurface,
			 des.x, des.y - NORMAL_TILE_HEIGHT / 2);
  }

  /*if (contains_special(special, S_AIRBASE) && draw_fortress_airbase) {*/
  if ((special & S_AIRBASE) && draw_fortress_airbase) {
    SDL_BlitSurface((SDL_Surface *) sprites.tx.airbase,
		    NULL, pBufSurface, &des);
    des = dst;
  }

  /*if (contains_special(special, S_FALLOUT) && draw_pollution) {*/
  if ((special & S_FALLOUT) && draw_pollution) {
    SDL_BlitSurface((SDL_Surface *) sprites.tx.fallout,
		    NULL, pBufSurface, &des);
    des = dst;
  }

  /*if (contains_special(special, S_POLLUTION) && draw_pollution) {*/
  if ((special & S_POLLUTION) && draw_pollution) {
    SDL_BlitSurface((SDL_Surface *) sprites.tx.pollution,
		    NULL, pBufSurface, &des);
    des = dst;	  
  }

    /*** city size ***/
  /* Not fogged as it would be unreadable */
  if ( !( SDL_Client_Flags & CF_CIV3_CITY_TEXT_STYLE ) && pCity && draw_cities) {
    if (pCity->size >= 10) {
      SDL_BlitSurface((SDL_Surface *) sprites.city.
		      size_tens[pCity->size / 10], NULL, pBufSurface,
		      &des);
      des = dst;	    
    }

    SDL_BlitSurface((SDL_Surface *) sprites.city.size[pCity->size % 10],
		    NULL, pBufSurface, &des);
    des = dst;    
  }
  

    /*** Unit ***/
  if (pUnit && (draw_units || (pUnit == pFocus && draw_focus_unit))) {
    put_unit_pixmap_draw(pUnit, pBufSurface, des.x,
			 des.y - NORMAL_TILE_HEIGHT / 2);

    if (!pCity
	&& unit_list_size(&map_get_tile(map_col, map_row)->units) > 1) {
      SDL_BlitSurface((SDL_Surface *) sprites.unit.stack, NULL,
		      pBufSurface, &des);
      des = dst;		
    }
  }

  /*if (contains_special(special, S_FORTRESS) && draw_fortress_airbase) {*/
  if ((special & S_FORTRESS) && draw_fortress_airbase) {
    assert((SDL_Surface *) sprites.tx.fortress);
    SDL_BlitSurface((SDL_Surface *) sprites.tx.fortress,
		    NULL, pBufSurface, &des);
    des = dst;	  
  }


  if (fog)  {
	  
    /*SDL_BlitSurface(pFogSurface, NULL, pBufSurface, NULL);*/
    des.x = map_x;
    des.y = map_y;	  
    SDL_BlitSurface( pTemp , NULL , pDest , &des );
/*	  
    des.x = map_x;
    des.y = map_y - NORMAL_TILE_HEIGHT / 2;
    SDL_BlitSurface(pBufSurface, NULL, pDest, &des);
*/      
    /* clear pBufSurface */
    /* if BytesPerPixel == 4 and Amask == 0 ( 24 bit coding ) alpha blit 
     * functions Set A = 255 in all pixels and then pixel val 
     * ( r=0, g=0, b=0) != 0.  Real val of this pixel is 0xff000000. *
    if (pBufSurface->format->BytesPerPixel == 4
	&& !pBufSurface->format->Amask) {
      SDL_FillRect(pBufSurface, NULL, 0xff000000);
    } else {
      SDL_FillRect(pBufSurface, NULL, 0);
    }
 */
  }
  
  
}

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
  {
	/* draw single tille */
    redraw_single_cell( col , row );
  } else {
    if ((width > 10) && (height > 10))
    {
      redraw_fullscreen( map_view_x0 , map_view_y0 );
      redraw_map_widgets();
    }
    else
    {
	/* draw  width x height tile rect */
	/* PORTME */    
	    ;
    }
  }
}


/**************************************************************************
...
**************************************************************************/
/* not used jet */
#if 0
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
#endif

/**************************************************************************
  Redraw ALL.
  All seen widgets are draw upon map then must be redraw too.
**************************************************************************/
static void redraw_map_widgets(void)
{
  struct GUI *pBuf = NULL;
#ifdef DRAW_TIMING
  struct timer *tttt=new_timer_start(TIMER_USER,TIMER_ACTIVE);
#endif	  
  /* just find a big rectangle that includes the whole visible area. The
     invisible tiles will not be drawn. */
  
#if 0  
  /* redraw city descriptions
     should be draw here - under map witgets */
  show_city_descriptions();
#endif

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

#ifdef DRAW_TIMING
  freelog(LOG_NORMAL, "redraw_map_widgets = %fms\n=============================",
	  1000.0 * read_timer_seconds_free(tttt));
#endif
  
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
void draw_unit_animation_frame(struct unit *punit,
			       bool first_frame, bool last_frame,	
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
    put_unit_pixmap_draw(punit, pUnit_Surf, 0, 0);
  }
  else
  {
    /* Clear old sprite. */
    SDL_BlitSurface(pMap_Copy, NULL, Main.screen, &src);
    add_refresh_rect(src);
  }
  
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
  int i, canvas_x, canvas_y;
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
      get_canvas_xy(losing_unit->x, losing_unit->y, &canvas_x, &canvas_y)) {
    /* copy screen area */
    src.x = canvas_x;
    src.y = canvas_y;
    SDL_BlitSurface(Main.screen, &src, pTmpSurface, NULL);

    dest.y = canvas_y;
    for (i = 0; i < num_tiles_explode_unit; i++) {

      anim_timer = renew_timer_start(anim_timer, TIMER_USER, TIMER_ACTIVE);

      /* blit explosion */
      dest.x = canvas_x + NORMAL_TILE_WIDTH / 4;

      SDL_BlitSurface((SDL_Surface *) sprites.explode.unit[i],
		      NULL, Main.screen, &dest);
      refresh_rect(dest);

      usleep_since_timer_start(anim_timer, 20000);

      /* clear explosion */
      dest.x = canvas_x;
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
  This function is called when the tileset is changed.
**************************************************************************/
void tileset_changed(void)
{
  /* PORTME */
  /* Here you should do any necessary redraws (for instance, the city
   * dialogs usually need to be resized). */
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
	  draw_map_cell(pDest, dest.x, dest.y, real_col, real_row, 1);
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
  
  if (cell_w_count && cell_h_count)
  {
     free_map_cells();
  }
  init_map_cells();
  
}

/* =====================================================================
				New Draw Algorithm
   ===================================================================== */

/*
   This is development code and may be unstable

   Coast tiles still are not properly implemented :(
   FOG OF WAR is disabled becouse is buggy.

   Code is based on Daniel L Speyer idea.

   Basic idea is separate terrain draw
   from forest/mountain/hill,special,units draws.
   
   Terrain is draw as (NORMAL_TILE_WIDTH/2)x(NORMAL_TILE_HEIGHT/2) rect.
   When we draw 16xrect (4 tiles) we have 4 rect ( 1 center tile) for free 
   and make automatical blending.
   
    ____ ____ ____ ____     __ __
   |____|____|____|____|   |/\|/\|
   |____|____|____|____| = |\/|\/|
   |____|____|____|____|   |/\|/\|
   |____|____|____|____|   |\/|\/|
                            -- --

   Each tile is div on 4 cells.
			    
   Screen is div on such cells and when we center screen I fill 
   screen cell array.
   
   All screen redraw will simply read cell array to draw correct cell.
*/


/*
 *  init screen cells array
*/
void init_map_cells(void)
{
  
  int i , count_w , count_h ;
  
  count_w = Main.screen->w / ( NORMAL_TILE_WIDTH / 2 );
  if ( Main.screen->w % ( NORMAL_TILE_WIDTH / 2 ) )
  {
    count_w++;
  }

  count_h = Main.screen->h / ( NORMAL_TILE_HEIGHT / 2 );
  if ( Main.screen->h % ( NORMAL_TILE_HEIGHT / 2 ) )
  {
    count_h++;
  }
  
  count_w++;
  count_h++;
  
  map_cell = CALLOC( count_w , sizeof( struct CELL_MAP *));
  for ( i = 0; i < count_w; i++)  
  {
    map_cell[i] = CALLOC( count_h , sizeof( struct CELL_MAP ));  
  }

  cell_w_count = count_w;
  cell_h_count = count_h;
  
}

void free_map_cells(void)
{
  int i ;
      
  for ( i = 0; i < cell_w_count; i++)  
  {
    FREE( map_cell[i] );
  }
  FREE( map_cell );
  
  cell_w_count = 0;
  cell_h_count = 0;
}

static void reset_map_cells(void)
{
  int i , j ;
      
  for ( i = 0; i < cell_w_count; i++)  
  {
    for ( j = 0; j < cell_h_count; j++)  
    {
      map_cell[i][j].sprite = NULL;
      map_cell[i][j].x = map_cell[i][j].y = 0xFFFF;
    }
  }
  
}
/* ========================================================== */

/*
 * Some code duplication becouse DIR_DX is buggy for iso map.
 * DIR_DX are TRUE only for no-iso maps or I again don't read something :).
*/

/*
 * Returns true if the step yields a new valid map position. If yes
 * (dest_x, dest_y) is set to the new map position.
 *
 * Direct calls to DIR_DXY should be avoided and DIRSTEP should be
 * used. But to allow dest and src to be the same, as in
 *    MAPSTEP(x, y, x, y, dir)
 * we bend this rule here.
 */
#define ISO_MAPSTEP(dest_x, dest_y, src_x, src_y, dir)	\
(    (dest_x) = (src_x) + ISO_DIR_DX[(dir)],   		\
     (dest_y) = (src_y) + ISO_DIR_DY[(dir)],		\
     normalize_map_pos(&(dest_x), &(dest_y)))


/**************************************************************************
  Assemble some data that is used in building the tile sprite arrays.
    (map_x, map_y) : the (normalized) map position
  The values we fill in:
    ttype          : the terrain type of the tile
    tspecial       : all specials the tile has
    ttype_near     : terrain types of all adjacent terrain
    tspecial_near  : specials of all adjacent terrain
**************************************************************************/
static void build_tile_terrain_data(int map_x, int map_y,
			    enum tile_terrain_type *ttype,
/*			    enum tile_special_type *tspecial,*/
			    enum tile_terrain_type *ttype_near
/*			    ,enum tile_special_type *tspecial_near*/
                            )
{
  enum direction8 dir;

  /**tspecial = map_get_special(map_x, map_y);*/
  *ttype = map_get_terrain(map_x, map_y);

  /* In iso view a river is drawn as an overlay on top of an underlying
   * grassland terrain. */
  if (is_isometric && *ttype == T_RIVER) {
    *ttype = T_GRASSLAND;
    /*tspecial |= S_RIVER;*/
  }

  /* Loop over all adjacent tiles.  We should have an iterator for this. */
  for (dir = 0; dir < 8; dir++) {
    int x1, y1;

    if (ISO_MAPSTEP(x1, y1, map_x, map_y, dir)) {
      /*tspecial_near[dir] = map_get_special(x1, y1);*/
      ttype_near[dir] = map_get_terrain(x1, y1);

      /* hacking away the river here... */
      if (is_isometric && ttype_near[dir] == T_RIVER) {
	/*tspecial_near[dir] |= S_RIVER;*/
	ttype_near[dir] = T_GRASSLAND;
      }
    } else {
      /* We draw the edges of the map as if the same terrain just
       * continued off the edge of the map. */
      /*tspecial_near[dir] = S_NO_SPECIAL;*/
      ttype_near[dir] = *ttype;
    }
  }
}

static void fill_blend_indexs( int *blend , const int *dirs , 
           const int *dirs_NESW , enum tile_terrain_type ttype , 
           enum tile_terrain_type *ttype_near )
{
  int index;	
  int slave1[MAX_BLEND] = { 1 , 0 , 3 , 2 };
  int slave2[MAX_BLEND] = { 2 , 3 , 0 , 1 };
  int slave1_NESW[MAX_BLEND] = { 3 , 1 , 3 , 1 };
  int slave2_NESW[MAX_BLEND] = { 0 , 0 , 2 , 2 };
  
  
  for( index = 0; index < MAX_BLEND; index++)
  {
    blend[index] = 0;
    if ( ttype <= ttype_near[dirs[index]] )
    {
/* slave 1 must be on thesame level that index 
 ___________ ___________      ________ _________      
| (0)index  | (1)slave1 |    | slave2 |         |
|___________|___________| or |________|_________|
| (2)slave2 | (3)       |    | index  | slave1  |
|___________|___________|    |________|_________|
*/
      if (( ttype_near[dirs[index]] == ttype_near[dirs[slave1[index]]] ) &&
        ( ttype_near[dirs[index]] == ttype_near[dirs[slave2[index]]] ) )
      {
        blend[index] = 3;
      }
      else
      {
        if (( ttype_near[dirs[index]] == ttype_near[dirs[slave1[index]]] ) &&
           ( ttype_near[dirs[index]] != ttype_near[dirs[slave2[index]]] ) )
        {
          blend[index] = 1;
	  continue;      
        }
    
        if (( ttype_near[dirs[index]] != ttype_near[dirs[slave1[index]]] ) &&
           ( ttype_near[dirs[index]] == ttype_near[dirs[slave2[index]]] ) )
        {
          blend[index] = 2;
        }
      }
    }/* ttype <= ttype_near[dirs[index]] */
    else
    {
      if (( ttype == ttype_near[dirs_NESW[slave1_NESW[index]]] ) &&
         ( ttype == ttype_near[dirs_NESW[slave2_NESW[index]]] ) )
      {
        blend[index] = 3;
      }
      else
      {
        if (( ttype == ttype_near[dirs_NESW[slave1_NESW[index]]] ) &&
           ( ttype != ttype_near[dirs_NESW[slave2_NESW[index]]] ) )
        {
          blend[index] = 1;
	  continue;      
        }
    
        if (( ttype != ttype_near[dirs_NESW[slave1_NESW[index]]] ) &&
           ( ttype == ttype_near[dirs_NESW[slave2_NESW[index]]] ) )
        {
          blend[index] = 2;
        }
      }
	    
    }
  }	
    return;
}


static void fill_cells_corners( SDL_Surface **cell , const int *dirs_X ,
                          const int *dirs_NESW ,
                          int map_x, int map_y,
                          enum tile_terrain_type *ttype_near )
{
  int blend[MAX_BLEND] ;
  enum tile_terrain_type ttype;
	
  build_tile_terrain_data( map_x, map_y, &ttype, ttype_near );
	
  fill_blend_indexs( blend , dirs_X , dirs_NESW , ttype ,ttype_near );

  
  cell[0] = cells[ttype][ttype_near[dirs_X[0]]][blend[0]][0];
  cell[1] = cells[ttype][ttype_near[dirs_X[1]]][blend[1]][1];
  cell[2] = cells[ttype][ttype_near[dirs_X[2]]][blend[2]][2];
  cell[3] = cells[ttype][ttype_near[dirs_X[3]]][blend[3]][3];

}

static void fill_map_cells( int x0 , int y0 )
{
  int col, row , x , y, real_x , real_y;
  //enum tile_terrain_type ttype;
  enum tile_terrain_type ttype_near[8];
  SDL_Surface *cell_rect[MAX_CORNER];
	
  const int dirs_X[4] = {
      /* 0 */
      DIR8_NORTHWEST,
      /* 1 */
      DIR8_NORTHEAST,
      /* 2 */
      DIR8_SOUTHWEST,
      /* 3 */
      DIR8_SOUTHEAST
    };

   const int dirs_NESW[4] = {
      /* 0 */
      DIR8_NORTH,
      /* 1 */
      DIR8_EAST,
      /* 2 */
      DIR8_SOUTH,
      /* 3 */
      DIR8_WEST
    };    
    
  x = x0;
  y = y0;
    
  for ( row = 0; row < cell_h_count; row+=2 )
  {
    for ( col = 0; col < cell_w_count; col+=2, x+=ISO_DIR_DX[DIR8_EAST], y+=ISO_DIR_DY[DIR8_EAST] )	  
    {
      /*real_x = x;
      real_y = y;
      if ( normalize_map_pos(&real_x, &real_y) )*/
      if ( normalize_map_pos( &x, &y) )    
      {	    
        fill_cells_corners( cell_rect , dirs_X , dirs_NESW ,
                          x, y, ttype_near );
      
	map_cell[col][row].sprite = cell_rect[0];
	/*map_cell[col][row].x = real_x;
	map_cell[col][row].y = real_y;*/
	map_cell[col][row].x = x;
	map_cell[col][row].y = y; 
	if ( col + 1 < cell_w_count )
	{
          map_cell[col + 1][row].sprite = cell_rect[1];
	  map_cell[col + 1][row].x = map_cell[col + 1][row].y = 0xFFFF;
	}
        if ( row + 1 < cell_h_count )
        {
          map_cell[col][row + 1].sprite = cell_rect[2];
	  map_cell[col][row + 1].x = map_cell[col][row + 1].y = 0xFFFF;
	  if ( col + 1 < cell_w_count )
	  {
            map_cell[col + 1][row + 1].sprite = cell_rect[3];
	    if (  ISO_MAPSTEP(real_x, real_y, x, y, dirs_X[3] ) )  
	    {
	      map_cell[col + 1][row + 1].x = real_x;
	      map_cell[col + 1][row + 1].y = real_y;
	    }
	    else
	    {
	      map_cell[col + 1][row + 1].x = map_cell[col + 1][row + 1].y = 0xFFFF;
	    }
	  }
        }
      }
      else
      {
	map_cell[col][row].sprite = NULL;
        map_cell[col][row].x = map_cell[col][row].y = 0xFFFF;
        if ( col + 1 < cell_w_count )
	{
          map_cell[col + 1][row].sprite = NULL;
	  map_cell[col + 1][row].x = map_cell[col + 1][row].y = 0xFFFF;
	}
        if ( row + 1 < cell_h_count )
        {
          map_cell[col][row + 1].sprite = NULL;
	  map_cell[col][row + 1].x = map_cell[col][row + 1].y = 0xFFFF;
	  if ( col + 1 < cell_w_count )
	  {
            map_cell[col + 1][row + 1].sprite = NULL;
            map_cell[col + 1][row + 1].x = map_cell[col + 1][row + 1].y = 0xFFFF;
	  }
        }      
      }
    }
        
    x0 += ISO_DIR_DX[DIR8_SOUTH];
    y0 += ISO_DIR_DY[DIR8_SOUTH];
    x = x0;
    y = y0;
  }
	
}

/* ============================================================== */

static void redraw_single_cell( int x , int y )
{
  int i , col , row , start_x , start_y;
  SDL_Rect dest;
  int w = NORMAL_TILE_WIDTH / 2, h = NORMAL_TILE_HEIGHT / 2;
  int col_mod[6] = {  0,  1 , 0 , 1 , 0 , 1 };
  int row_mod[6] = { -1, -1 , 0 , 0 , 1 , 1 };
	  
#ifdef DRAW_TIMING
  struct timer *ttt = NULL;
#endif
  
  get_canvas_xy( x , y , &start_x , &start_y );	

  for ( row = 0; row < cell_h_count; row++ )
  {
    for ( col = 0; col < cell_w_count; col++ )
    {
	if ( map_cell[col][row].x == x && map_cell[col][row].y == y)
	{
	  goto PUT;
	}
    }
  }
  
  return;
  
PUT:
#ifdef DRAW_TIMING
  ttt=new_timer_start(TIMER_USER,TIMER_ACTIVE);
#endif  
  for ( i = 0; i < 6 ; i++ )
  {
    if ( col + col_mod[i] < cell_w_count && row + row_mod[i] < cell_h_count &&
	col + col_mod[i] >= 0 && row + row_mod[i] >= 0 )
    {
      dest.x = start_x + w * col_mod[i];
      dest.y = start_y + h * row_mod[i];
      if ( map_cell[ col + col_mod[i] ][ row + row_mod[i] ].sprite )
      {
        SDL_BlitSurface( map_cell[col + col_mod[i]][row + row_mod[i]].sprite,
						    NULL , Main.screen, &dest );
      }
      else
      { /* fill Black */
        dest.w = w;
        dest.h = h;
        SDL_FillRect( Main.screen , &dest , 0x0 );
      }
    }
  }
  

  dest.w = UNIT_TILE_WIDTH;
  dest.h = UNIT_TILE_HEIGHT;
  dest.x = start_x;
  dest.y = start_y - h;
  SDL_SetClipRect( Main.screen , &dest );

  if ( row - 2 >= 0 )
  draw_map_cell( Main.screen , start_x , start_y - 2 * h,
	  map_cell[col][row - 2].x , map_cell[col][row - 2].y , 0);
  
  if ( col - 1 >= 0 && row - 1 >= 0 )
    draw_map_cell( Main.screen , start_x - w, start_y - h,
	  map_cell[col - 1][row - 1].x , map_cell[col-1][row-1].y , 0);
  
  if ( col + 1 < cell_w_count && row - 1 >= 0 )  
    draw_map_cell( Main.screen , start_x + w, start_y - h,
	  map_cell[col + 1][row - 1].x , map_cell[col + 1][row - 1].y , 0);
  
 
  draw_map_cell( Main.screen , start_x , start_y, x , y , 0);

  if ( col - 1 >= 0 && row + 1 < cell_h_count )
    draw_map_cell( Main.screen , start_x - w, start_y + h,
	  map_cell[col - 1][row + 1].x , map_cell[col - 1][row + 1].y , 0);
  
    
  if ( col + 1 < cell_w_count && row + 1 < cell_h_count ) 
    draw_map_cell( Main.screen , start_x + w, start_y + h,
	  map_cell[col + 1][row + 1].x , map_cell[col + 1][row + 1].y , 0);
	
#ifdef DRAW_TIMING	
  freelog(LOG_NORMAL, "redraw_singe_cell = %fms",
	  	  1000.0 * read_timer_seconds_free(ttt));
#endif
  
  SDL_SetClipRect( Main.screen , NULL );
  add_refresh_region( start_x , start_y - h ,
                  UNIT_TILE_WIDTH , UNIT_TILE_HEIGHT );
}

static void redraw_fullscreen( int x0 , int y0 )
{
  int col , row , sx , sy , start_x , start_y;
  int w = NORMAL_TILE_WIDTH / 2, h = NORMAL_TILE_HEIGHT / 2;
  SDL_Rect dest;
	
#ifdef DRAW_TIMING	
  struct timer *tt = NULL;
#endif
  
  get_canvas_xy( x0 , y0 , &start_x , &start_y );

  sx = start_x;
  sy = start_y;
  
#ifdef DRAW_TIMING  
  tt=new_timer_start(TIMER_USER,TIMER_ACTIVE);
#endif
  for ( col = 0; col < cell_w_count; col++ )
  {
    for ( row = 0; row < cell_h_count; row++ )
    {
	dest.x = sx;
	dest.y = sy;
	if ( map_cell[col][row ].sprite )
	{
	  SDL_BlitSurface( map_cell[col][row ].sprite,
                           NULL , Main.screen, &dest );
	}
	else
	{ /* fill Black */
	  dest.w = w;
          dest.h = h;
	  SDL_FillRect( Main.screen , &dest , 0x0 );	
	}
	sy += h;
	
    }
    sx += w;
    sy = start_y;
  }

  sx = start_x;	
  sy = start_y;
	
  for ( row = 0; row < cell_h_count; row++ )
  {
    for ( col = 0; col < cell_w_count; col++ )
    {
	if ( map_cell[col][row].x != 0xFFFF )
	{
	  draw_map_cell( Main.screen , sx , sy,
			  map_cell[col][row].x , map_cell[col][row].y , 0);
	}
	sx += w;    
    }
    sy += h;
    sx = start_x;
  }  
  
#ifdef DRAW_TIMING  
  freelog(LOG_NORMAL, "redraw_fullscreen of %dx%d (rect cells) = %fms",
	  cell_w_count, cell_h_count,
	  1000.0 * read_timer_seconds_free(tt));
#endif
  
}

/* ===================================================== */

static void rebuild_blending_tile( SDL_Surface **pDitherBuffers ,
                                   SDL_Surface *pTerrain2 )
{
  int mask = 0;	
  while( *pDitherBuffers )
  {
    SDL_FillRect( *pDitherBuffers , NULL, 0x0 );
	  
    /* north */
    dither_north(pTerrain2, pDitherMasks[mask] , *pDitherBuffers);
	  
    /* south */
    dither_south(pTerrain2, pDitherMasks[mask] , *pDitherBuffers);
    
    /* east */
    dither_east(pTerrain2, pDitherMasks[mask] , *pDitherBuffers);
    
    /* west */
    dither_west(pTerrain2, pDitherMasks[mask] , *pDitherBuffers);
    
    mask++;
    pDitherBuffers++;
  }
}

static void rebuild_coast_tile( SDL_Surface **pCoastBuffers , int blend )
{
  const int dirs[4] = {
      /* up */
      DIR8_NORTHWEST,
      /* down */
      DIR8_SOUTHEAST,
      /* left */
      DIR8_SOUTHWEST,
      /* right */
      DIR8_NORTHEAST
    };
    
  int array_index , i , corner;
  SDL_Rect des;
  SDL_Surface *coasts[4];
  enum tile_terrain_type ttype_near[8];
    
  for (i = 0; i < 4; i++) {
    ttype_near[i] = T_OCEAN;
  }
  
  switch( blend )
  {
    case 0:
	ttype_near[DIR8_NORTH] = T_ARCTIC;
        ttype_near[DIR8_WEST] = T_ARCTIC;
        ttype_near[DIR8_EAST] = T_ARCTIC;
        ttype_near[DIR8_SOUTH] = T_ARCTIC;
    break;
    case 1:
	ttype_near[DIR8_NORTH] = T_ARCTIC;
        ttype_near[DIR8_WEST] = T_ARCTIC;
        ttype_near[DIR8_EAST] = T_ARCTIC;
        ttype_near[DIR8_SOUTH] = T_ARCTIC;
        ttype_near[DIR8_NORTHWEST] = T_ARCTIC;
        ttype_near[DIR8_SOUTHEAST] = T_ARCTIC;
    break;
    case 2:
	ttype_near[DIR8_NORTH] = T_ARCTIC;
        ttype_near[DIR8_WEST] = T_ARCTIC;
        ttype_near[DIR8_EAST] = T_ARCTIC;
        ttype_near[DIR8_SOUTH] = T_ARCTIC;
        ttype_near[DIR8_SOUTHWEST] = T_ARCTIC;
        ttype_near[DIR8_NORTHEAST] = T_ARCTIC;
    break;
    case 3:
      for (i = 0; i < 4; i++) {
        ttype_near[i] = T_ARCTIC;
      }    
    break;
    default:
      abort();
  }
   
  for( corner = 0; corner < 4; corner++ )  
  {
	  
    /* put coasts */
    for (i = 0; i < 4; i++) {
      array_index = ((ttype_near[dir_ccw(dirs[i])] != T_OCEAN ? 1 : 0)
			 + (ttype_near[dirs[i]] != T_OCEAN ? 2 : 0)
			 + (ttype_near[dir_cw(dirs[i])] != T_OCEAN ? 4 : 0));

	    
	    
      coasts[i] = (SDL_Surface *)sprites.tx.coast_cape_iso[array_index][i];
    }
    
    SDL_FillRect( pCoastBuffers[corner] , NULL, 0x0 );
    
    /* top */
    des.x = NORMAL_TILE_WIDTH / 4;
    SDL_BlitSurface(coasts[0], NULL, pCoastBuffers[corner], &des);

    /* bottom */
    des.y = NORMAL_TILE_HEIGHT / 2;
    SDL_BlitSurface(coasts[1], NULL, pCoastBuffers[corner], &des);

      /* left */
    des.x -= NORMAL_TILE_WIDTH / 4;
    des.y -= NORMAL_TILE_HEIGHT / 4;
    SDL_BlitSurface(coasts[2], NULL, pCoastBuffers[corner], &des);

      /* right */
    des.x += NORMAL_TILE_WIDTH / 2;
    SDL_BlitSurface(coasts[3], NULL, pCoastBuffers[corner], &des);
  
  }
  
}

/* I should do it in this way or I should load ready graphic ? */
void init_cells_sprites(void)
{
  enum tile_terrain_type t1, t2;
  int blend , corner , w = NORMAL_TILE_WIDTH / 2, h = NORMAL_TILE_HEIGHT / 2;
  SDL_Surface *pBuf , *pT1_TERRAIN , *pT2_TERRAIN;
  SDL_Surface *pDither[MAX_BLEND + 1 ];
  SDL_Rect t1_src , t2_src ;

  for ( blend = 0; blend < MAX_BLEND; blend++ )
  {
    pDither[blend] = create_surf( NORMAL_TILE_WIDTH ,
                                  NORMAL_TILE_HEIGHT ,
                                  SDL_SWSURFACE );
    SDL_SetColorKey(pDither[blend], SDL_SRCCOLORKEY, 0x0);
  }
  pDither[blend] = NULL;
  
  t1_src.w = t2_src.w = w;
  t1_src.h = t2_src.h = h;
  for ( t1 = T_ARCTIC ; t1 < T_LAST ; t1++ )
  {
    if ( t1 == T_RIVER ) continue;	  
    pT1_TERRAIN = (SDL_Surface *)get_tile_type(t1)->sprite[0];  
    for ( t2 = T_ARCTIC ; t2 < T_LAST ; t2++ )
    {
      if ( t2 == T_RIVER ) continue;
      pT2_TERRAIN = (SDL_Surface *)get_tile_type(t2)->sprite[0];
      if ( !pT2_TERRAIN )/*&& t2 == T_UNKNOWN )*/
	pT2_TERRAIN = (SDL_Surface *)sprites.black_tile;
      if ( t1 <= t2 )
      {
	if ( t1 != T_OCEAN && t2 != T_OCEAN )
          rebuild_blending_tile( pDither , pT2_TERRAIN );
	for ( blend = 0; blend < MAX_BLEND; blend++ )
        {
	  if ( t1 == T_OCEAN || t2 == T_OCEAN )	
	    rebuild_coast_tile( pDither , blend );
	  for ( corner = 0 ; corner < MAX_CORNER ; corner++ )
	  {
	    pBuf = create_surf( w , h , SDL_SWSURFACE );
	    if ( t1 != T_OCEAN && t2 != T_OCEAN )
	    {
	      t1_src.x = (!corner || corner == 2) ?  0 : w;
	      t1_src.y = ( corner > 1 ) ?  h : 0;
	
	      t2_src.x = (!corner || corner == 2) ?  w : 0;
	      t2_src.y = ( corner > 1 ) ?  0 : h;
	      
	      SDL_BlitSurface( pT1_TERRAIN , &t1_src , pBuf , NULL );
	      SDL_BlitSurface( pT2_TERRAIN , &t2_src , pBuf , NULL );	    
	      SDL_BlitSurface( pDither[blend] , &t1_src , pBuf , NULL );
	    }
	  else
	  { /* t1 == T_OCEAN || t2 == T_OCEAN */
	    if ( t1 == T_OCEAN && t2 == T_UNKNOWN )
	    {
              t1_src.x = ((corner % 2) == 0) * w - w / 2 ;
	      t1_src.y = 0;
	      SDL_BlitSurface( 
		  (SDL_Surface *)sprites.tx.coast_cape_iso[0][0],
		                             NULL , pBuf , &t1_src );
		  
	      t1_src.x = 0 ;
	      t1_src.y = ( (corner < 2) ? h : 0 ) - h / 2 ;
	      SDL_BlitSurface( 
		  (SDL_Surface *)sprites.tx.coast_cape_iso[0][0],
		                             NULL , pBuf , &t1_src );	  
		
              t1_src.w = w;
              t1_src.h = h;		    
	    }/* t1 == T_OCEAN && t2 == T_UNKNOWN */
	    
            if ( t1 == T_OCEAN && t2 != T_OCEAN && t2 != T_UNKNOWN )
	    {	  
	      t2_src.x = w - ((corner % 2) > 0) * w;
	      t2_src.y = h - ( corner > 1 ) * h;
	      SDL_BlitSurface( pT2_TERRAIN , &t2_src , pBuf , NULL );
/*		
	      t1_src.x = ((corner % 2) == 0) * w - w / 2 ;
	      t1_src.y = 0;
	      SDL_BlitSurface( 
		  (SDL_Surface *)sprites.tx.coast_cape_iso[1][( corner > 1 )],
		                                    NULL , pBuf , &t1_src );
		  
	      t1_src.x = 0 ;
	      t1_src.y = ( (corner < 2) ? h : 0 ) - h / 2 ;
	      SDL_BlitSurface( 
		  (SDL_Surface *)sprites.tx.coast_cape_iso[1][( corner % 2 ) ? 3 : 2 ],
		                                     NULL , pBuf , &t1_src );	  

		    
		    
              t1_src.w = w;
              t1_src.h = h;
	      */
	      t1_src.x = (!corner || corner == 2) ?  0 : w;
	      t1_src.y = ( corner > 1 ) ?  h : 0;
	      SDL_BlitSurface( pDither[corner] , &t1_src , pBuf , NULL );
	      
	    } /* t1 == T_OCEAN && t2 != T_OCEAN */
	    
	    if ( t1 != T_OCEAN && t2 == T_OCEAN )
	    {	  
	      t1_src.x = ((corner % 2) > 0) * w;
	      t1_src.y = ( corner > 1 ) * h;
	      SDL_BlitSurface( pT1_TERRAIN , &t1_src , pBuf , NULL );
/*		
	      t2_src.x = ((corner % 2) > 0) * w - w / 2 ;
	      t2_src.y = 0;
	      SDL_BlitSurface( 
		  (SDL_Surface *)sprites.tx.coast_cape_iso[4][( corner < 2 )],
		                                     NULL , pBuf , &t2_src );
		  
	      t2_src.x = 0 ;
	      t2_src.y = ( (corner > 1) ? h : 0 ) - h / 2 ;
	      SDL_BlitSurface( 
		  (SDL_Surface *)sprites.tx.coast_cape_iso[3][( corner % 2 ) ? 2 : 3 ],
		                                NULL , pBuf , &t2_src );	  
		
              t2_src.w = w;
              t2_src.h = h;
*/
	      t2_src.x = (!corner || corner == 2) ?  w : 0;
	      t2_src.y = ( corner > 1 ) ?  0 : h;    
	      SDL_BlitSurface( pDither[corner] , &t2_src , pBuf , NULL );
	      
          }/* t1 != T_OCEAN && t2 == T_OCEAN */
	  
	    if ( t1 == T_OCEAN && t2 == T_OCEAN )
	    {	  
	      if ( !corner )
	      {
	        t2_src.x = w / 2 ;
	        t2_src.y = 0;
	        SDL_BlitSurface( 
		  (SDL_Surface *)sprites.tx.coast_cape_iso[0][ 0 ],
		                             NULL , pBuf , &t2_src );
		
		t2_src.x *= -1; 
	        SDL_BlitSurface( 
		  (SDL_Surface *)sprites.tx.coast_cape_iso[0][ 1 ],
		                             NULL , pBuf , &t2_src );      
		
		t2_src.x = 0 ;
	        t2_src.y = h / 2;
	        SDL_BlitSurface( 
		  (SDL_Surface *)sprites.tx.coast_cape_iso[0][ 2 ],
		                             NULL , pBuf , &t2_src );
		
		t2_src.y *= -1;
	        SDL_BlitSurface( 
		  (SDL_Surface *)sprites.tx.coast_cape_iso[0][ 3 ],
		                             NULL , pBuf , &t2_src );
						       
                t2_src.w = w;
                t2_src.h = h;
	      }
	      else
	      {
		FREESURFACE( pBuf );
		pBuf = cells[t1][t2][blend][0];
	      }
	      
            }/* t1 == T_OCEAN && t2 == T_OCEAN */
	    
          } /* t1 == T_OCEAN || t2 == T_OCEAN */
	  
	  cells[t1][t2][blend][corner] = pBuf;
	} /* for */
        }/* blend */
      } /* t1 <= t2 */
      else
      { /* t1 > t2 */
	for ( blend = 0; blend < MAX_BLEND; blend++ )
        {      
	  cells[t1][t2][blend][0] = cells[t2][t1][blend][3];
	  cells[t1][t2][blend][1] = cells[t2][t1][blend][2];
	  cells[t1][t2][blend][2] = cells[t2][t1][blend][1];
	  cells[t1][t2][blend][3] = cells[t2][t1][blend][0];
	}
      }
    } /* t2 */
  }/* t2 */
  
  for ( blend = 0; blend < MAX_BLEND; blend++ )
  {
    FREESURFACE ( pDither[blend] );
  }
  
}

void free_cells_sprites(void)
{
  enum tile_terrain_type t1, t2;
  int blend , corner;
	
  for ( t1 = T_ARCTIC ; t1 < T_LAST ; t1++ )
  {
    for ( t2 = T_ARCTIC ; t2 < T_LAST ; t2++ )
    {
      for ( blend = 0; blend < MAX_BLEND; blend++ )
      {
        for ( corner = 0 ; corner < MAX_CORNER ; corner++ )
        {
	  FREESURFACE( cells[t1][t2][blend][corner] );
	  cells[t2][t1][blend][corner] = NULL;
        }
      }
    }
  }
  
}

/**************************************************************************
  ...
**************************************************************************/
void tmp_map_surfaces_init(void)
{
	
  pFogSurface = create_surf(NORMAL_TILE_WIDTH,
			    UNIT_TILE_HEIGHT ,
			    SDL_SWSURFACE);
	
  SDL_FillRect(pFogSurface, NULL,
	       SDL_MapRGBA(pFogSurface->format, 0, 0, 0, 128));
	
  SDL_SetAlpha(pFogSurface, SDL_SRCALPHA, 128);

  pTmpSurface = create_surf(NORMAL_TILE_WIDTH,
			    UNIT_TILE_HEIGHT,
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
  
  
#if 0
  pBlinkSurfaceA =
      create_surf(NORMAL_TILE_WIDTH, 1.5 * NORMAL_TILE_HEIGHT,
		  SDL_SWSURFACE);
  pBlinkSurfaceB =
      create_surf(NORMAL_TILE_WIDTH, 1.5 * NORMAL_TILE_HEIGHT,
		  SDL_SWSURFACE);
#endif

  get_new_view_rectsize();



}
