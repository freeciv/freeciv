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
                          gui_tilespec.c  -  description
                             -------------------
    begin                : Dec. 2 2002
    copyright            : (C) 2002 by Rafał Bursig
    email                : Rafal Bursig <bursig@poczta.fm>
 **********************************************************************/
 
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include <SDL/SDL.h>

#include "hash.h"
#include "support.h"
#include "log.h"

#include "tilespec.h"

#include "gui_mem.h"
#include "gui_main.h"
#include "graphics.h"
#include "gui_zoom.h"

#include "gui_tilespec.h"

extern SDL_Surface *pDitherMask;

static struct {
    /* Each citizen type has up to MAX_NUM_CITIZEN_SPRITES different
     * sprites, as defined by the tileset. */
    int count;
    SDL_Surface *surface[MAX_NUM_CITIZEN_SPRITES];
  } citizen[NUM_TILES_CITIZEN];


#define load_GUI_surface(pSpr, pStruct, pSurf, tag)		\
do {								\
  pSpr = load_sprite(tag);					\
  pStruct->pSurf = (pSpr ? GET_SURF(pSpr) : NULL);		\
  assert(pStruct->pSurf != NULL);				\
  pSpr->psurface = NULL;					\
  unload_sprite(tag);						\
} while(0)


#define load_theme_surface(pSpr, pSurf, tag)		\
	load_GUI_surface(pSpr, pTheme, pSurf, tag)

#define load_city_icon_surface(pSpr, pSurf, tag)	\
	load_GUI_surface(pSpr, pIcons, pSurf, tag)


#define load_order_theme_surface(pSpr, pSurf, tag)	\
do {							\
  load_GUI_surface(pSpr, pTheme, pSurf, tag);		\
  if(pTheme->pSurf->format->Amask) {			\
    SDL_SetAlpha(pTheme->pSurf, 0x0 , 0x0);		\
  }							\
} while(0)

/**********************************************************************
  Returns a text name for the citizen, as used in the tileset.
***********************************************************************/
static const char *get_citizen_name(struct citizen_type citizen)
{
  /* These strings are used in reading the tileset.  Do not
   * translate. */
  switch (citizen) {
  case CITIZEN_ELVIS:
    return "entertainer";
  case CITIZEN_SCIENTIST:
    return "scientist";
  case CITIZEN_TAXMAN:
    return "tax_collector";
  case CITIZEN_HAPPY:
    return "happy";
  case CITIZEN_CONTENT:
    return "content";
  case CITIZEN_UNHAPPY:
    return "unhappy";
  case CITIZEN_ANGRY:
    return "angry";
  default:
    die("unknown citizen type %d", (int) citizen);
  }
  return NULL;
}

/*******************************************************************************
 * reload citizens "style" icons.
 *******************************************************************************/
static void real_reload_citizens_icons(int style)
{
  char tag[64];
  char alt_buf[32] = ".";
  int i , j;
  struct Sprite *pSpr = NULL;
    
  if (strcmp("generic" , city_styles[style].citizens_graphic_alt))
  {
    my_snprintf(alt_buf , sizeof(alt_buf) , ".%s_",
                       city_styles[style].citizens_graphic_alt ); 
  }
	
  /* Load the citizen sprite graphics. */
  for (i = 0; i < NUM_TILES_CITIZEN; i++) {
	  
    my_snprintf(tag, sizeof(tag), "citizen.%s_%s",
	  city_styles[style].citizens_graphic , get_citizen_name(i));  
    
    
    pSpr = load_sprite(tag);
    if(!pSpr) {
      freelog(LOG_DEBUG,"Can't find %s", tag);
      my_snprintf(tag, sizeof(tag), "citizen%s%s", alt_buf ,get_citizen_name(i));  
      freelog(LOG_DEBUG,"Trying load alternative %s", tag);
      pSpr = load_sprite(tag);
    }
    
    FREESURFACE(citizen[i].surface[0]);
    citizen[i].surface[0] = (pSpr ? GET_SURF(pSpr) : NULL);
    
    if (citizen[i].surface[0]) {
      /* If this form exists, use it as the only sprite.  This allows
       * backwards compatability with tilesets that use e.g.,
       * citizen.entertainer. */
      citizen[i].count = 1;
      pSpr->psurface = NULL;
      unload_sprite(tag);
      continue;
    }
    for (j = 0; j < MAX_NUM_CITIZEN_SPRITES; j++) {
      my_snprintf(tag, sizeof(tag), "citizen.%s_%s_%d",
	  city_styles[style].citizens_graphic ,get_citizen_name(i) , j );
      
      pSpr = load_sprite(tag);
      if(!pSpr) {
        freelog(LOG_DEBUG,"Can't find %s", tag);
        my_snprintf(tag, sizeof(tag), "citizen%s%s_%d", alt_buf,
	    				get_citizen_name(i), j);
        freelog(LOG_DEBUG,"Trying load alternative %s", tag);
        pSpr = load_sprite(tag);
      }
      
      FREESURFACE(citizen[i].surface[j]);
      citizen[i].surface[j] = (pSpr ? GET_SURF(pSpr) : NULL);
      if (!citizen[i].surface[j]) {
	break;
      }
      pSpr->psurface = NULL;
      unload_sprite(tag);
    }
    citizen[i].count = j;
    assert(j > 0);
  }	

}


/*******************************************************************************
 * reload small citizens "style" icons.
 *******************************************************************************/
static void reload_small_citizens_icons(int style)
{

  /* free info icons */
  FREESURFACE(pIcons->pMale_Content);
  FREESURFACE(pIcons->pFemale_Content);
  FREESURFACE(pIcons->pMale_Happy);
  FREESURFACE(pIcons->pFemale_Happy);
  FREESURFACE(pIcons->pMale_Unhappy);
  FREESURFACE(pIcons->pFemale_Unhappy);
  FREESURFACE(pIcons->pMale_Angry);
  FREESURFACE(pIcons->pFemale_Angry);
	
  FREESURFACE(pIcons->pSpec_Lux); /* Elvis */
  FREESURFACE(pIcons->pSpec_Tax); /* TaxMan */
  FREESURFACE(pIcons->pSpec_Sci); /* Scientist */

  /* allocate icons */
  pIcons->pMale_Happy =
    ResizeSurface(get_citizen_surface(CITIZEN_HAPPY, 0), 15, 26, 1);
  SDL_SetColorKey(pIcons->pMale_Happy , SDL_SRCCOLORKEY|SDL_RLEACCEL, 
			    get_first_pixel(pIcons->pMale_Happy));
    
  pIcons->pFemale_Happy =
    ResizeSurface(get_citizen_surface(CITIZEN_HAPPY, 1), 15, 26, 1);
  SDL_SetColorKey(pIcons->pFemale_Happy , SDL_SRCCOLORKEY|SDL_RLEACCEL, 
			    get_first_pixel(pIcons->pFemale_Happy));
    
  pIcons->pMale_Content =
    ResizeSurface(get_citizen_surface(CITIZEN_CONTENT, 0), 15, 26, 1);
  SDL_SetColorKey( pIcons->pMale_Content , SDL_SRCCOLORKEY|SDL_RLEACCEL, 
			    get_first_pixel(pIcons->pMale_Content ));
    
  pIcons->pFemale_Content =
    ResizeSurface(get_citizen_surface(CITIZEN_CONTENT, 1), 15, 26, 1);
  SDL_SetColorKey(pIcons->pFemale_Content, SDL_SRCCOLORKEY|SDL_RLEACCEL, 
			    get_first_pixel(pIcons->pFemale_Content));
    
  pIcons->pMale_Unhappy =
    ResizeSurface(get_citizen_surface(CITIZEN_UNHAPPY, 0), 15, 26, 1);
  SDL_SetColorKey(pIcons->pMale_Unhappy , SDL_SRCCOLORKEY|SDL_RLEACCEL , 
			    get_first_pixel(pIcons->pMale_Unhappy));
    
  pIcons->pFemale_Unhappy =
    ResizeSurface(get_citizen_surface(CITIZEN_UNHAPPY, 1), 15, 26, 1);
  SDL_SetColorKey(pIcons->pFemale_Unhappy , SDL_SRCCOLORKEY|SDL_RLEACCEL, 
			    get_first_pixel(pIcons->pFemale_Unhappy));
    
  pIcons->pMale_Angry =
    ResizeSurface(get_citizen_surface(CITIZEN_ANGRY, 0), 15, 26, 1);
  SDL_SetColorKey(pIcons->pMale_Angry , SDL_SRCCOLORKEY|SDL_RLEACCEL , 
			    get_first_pixel(pIcons->pMale_Angry));
    
  pIcons->pFemale_Angry =
    ResizeSurface(get_citizen_surface(CITIZEN_ANGRY, 1), 15, 26, 1);
  SDL_SetColorKey(pIcons->pFemale_Angry , SDL_SRCCOLORKEY|SDL_RLEACCEL, 
			    get_first_pixel(pIcons->pFemale_Angry));
    
  pIcons->pSpec_Lux =
    ResizeSurface(get_citizen_surface(CITIZEN_ELVIS, 0), 15, 26, 1);
  SDL_SetColorKey(pIcons->pSpec_Lux , SDL_SRCCOLORKEY|SDL_RLEACCEL, 
			    get_first_pixel(pIcons->pSpec_Lux));
    
  pIcons->pSpec_Tax =
    ResizeSurface(get_citizen_surface(CITIZEN_TAXMAN, 0), 15, 26, 1);
  SDL_SetColorKey(pIcons->pSpec_Tax, SDL_SRCCOLORKEY|SDL_RLEACCEL, 
			    get_first_pixel(pIcons->pSpec_Tax));
    
  pIcons->pSpec_Sci =
    ResizeSurface(get_citizen_surface(CITIZEN_SCIENTIST, 0), 15, 26, 1);
  SDL_SetColorKey(pIcons->pSpec_Sci, SDL_SRCCOLORKEY|SDL_RLEACCEL, 
			    get_first_pixel(pIcons->pSpec_Sci));
    
}

/* ================================================================================= */
/* ===================================== Public ==================================== */
/* ================================================================================= */

/**********************************************************************
  Set city citizens icons sprite value; should only happen after
  start of game (city style struct was filled ).
***********************************************************************/
void reload_citizens_icons(int style)
{
  real_reload_citizens_icons(style);
  reload_small_citizens_icons(style);
  pIcons->style = style;
}

/**********************************************************************
  Set city icons sprite value; should only happen after
  tilespec_load_tiles().
***********************************************************************/
void tilespec_setup_city_icons(void)
{
  int i, j;
  struct Sprite *pSpr = NULL;
  
  pIcons = ( struct City_Icon *)MALLOC( sizeof( struct City_Icon ));
  
  load_city_icon_surface(pSpr, pBIG_Food_Corr, "city.food_waste");
  load_city_icon_surface(pSpr, pBIG_Shield_Corr, "city.shield_waste");
  load_city_icon_surface(pSpr, pBIG_Trade_Corr, "city.trade_waste");
  load_city_icon_surface(pSpr, pBIG_Food, "city.food");
      
  pIcons->pBIG_Food_Surplus = crop_rect_from_surface(pIcons->pBIG_Food, NULL);
  SDL_SetAlpha(pIcons->pBIG_Food_Surplus, SDL_SRCALPHA, 128);
    
  load_city_icon_surface(pSpr, pBIG_Shield, "city.shield");
  
  pIcons->pBIG_Shield_Surplus = crop_rect_from_surface(pIcons->pBIG_Shield, NULL);
  SDL_SetAlpha(pIcons->pBIG_Shield_Surplus, SDL_SRCALPHA, 128);
  
  load_city_icon_surface(pSpr, pBIG_Trade, "city.trade");
  load_city_icon_surface(pSpr, pBIG_Luxury, "city.lux");
  load_city_icon_surface(pSpr, pBIG_Coin, "city.coin");
  load_city_icon_surface(pSpr, pBIG_Colb, "city.colb");
  load_city_icon_surface(pSpr, pBIG_Face, "city.red_face");
  load_city_icon_surface(pSpr, pBIG_Coin_Corr, "city.dark_coin");
  load_city_icon_surface(pSpr, pBIG_Coin_UpKeep, "city.unkeep_coin");
  		  
  /* small icon */
  load_city_icon_surface(pSpr, pFood, "city.small_food");
  load_city_icon_surface(pSpr, pShield, "city.small_shield");
  load_city_icon_surface(pSpr, pTrade, "city.small_trade");
  load_city_icon_surface(pSpr, pFace, "city.small_red_face");
  load_city_icon_surface(pSpr, pLuxury, "city.small_lux");
  load_city_icon_surface(pSpr, pCoin, "city.small_coin");		  
  load_city_icon_surface(pSpr, pColb, "city.small_colb");		  
    
  load_city_icon_surface(pSpr, pPollution, "city.pollution");
  /* ================================================================= */
  
  load_city_icon_surface(pSpr, pPolice, "city.police");
  /* ================================================================= */
  pIcons->pWorklist = create_surf(9,9, SDL_SWSURFACE);
  SDL_FillRect(pIcons->pWorklist, NULL,
		  SDL_MapRGB(pIcons->pWorklist->format, 255, 255,255));
  putframe(pIcons->pWorklist, 0,0,
	pIcons->pWorklist->w - 1, pIcons->pWorklist->h - 1, 0xFF000000);
  putline(pIcons->pWorklist, 3, 2, 5, 2, 0xFF000000);
  putline(pIcons->pWorklist, 3, 4, 7, 4, 0xFF000000);
  putline(pIcons->pWorklist, 3, 6, 6, 6, 0xFF000000);
  /* ================================================================= */
  /* clear all citizens icons pointer */
  for (i = 0; i < NUM_TILES_CITIZEN; i++) {
    for (j = 0; j < MAX_NUM_CITIZEN_SPRITES; j++) {
      citizen[i].surface[j] = NULL;
    }
  }
  
  /* force reload citizens icons */
  pIcons->style = 999;
}

void tilespec_free_city_icons(void)
{
  FREE(pIcons->pBIG_Food_Corr);
  FREE(pIcons->pBIG_Shield_Corr);
  FREE(pIcons->pBIG_Trade_Corr);
  FREE(pIcons->pBIG_Food);
  FREE(pIcons->pBIG_Food_Surplus);
  FREE(pIcons->pBIG_Shield);
  FREE(pIcons->pBIG_Shield_Surplus);
  FREE(pIcons->pBIG_Trade);
  FREE(pIcons->pBIG_Luxury);
  FREE(pIcons->pBIG_Coin);
  FREE(pIcons->pBIG_Colb);
  FREE(pIcons->pBIG_Face);
  FREE(pIcons->pBIG_Coin_Corr);
  FREE(pIcons->pBIG_Coin_UpKeep);
  
  FREE(pIcons->pFood);
  FREE(pIcons->pShield);
  FREE(pIcons->pTrade);
  FREE(pIcons->pFace);
  FREE(pIcons->pLuxury);
  FREE(pIcons->pCoin);		  
  FREE(pIcons->pColb);		  
    
  FREE(pIcons->pPollution);
  FREE(pIcons->pPolice);
  FREE(pIcons->pWorklist);
  
  /* small citizens */
  FREESURFACE(pIcons->pMale_Content);
  FREESURFACE(pIcons->pFemale_Content);
  FREESURFACE(pIcons->pMale_Happy);
  FREESURFACE(pIcons->pFemale_Happy);
  FREESURFACE(pIcons->pMale_Unhappy);
  FREESURFACE(pIcons->pFemale_Unhappy);
  FREESURFACE(pIcons->pMale_Angry);
  FREESURFACE(pIcons->pFemale_Angry);
	
  FREESURFACE(pIcons->pSpec_Lux); /* Elvis */
  FREESURFACE(pIcons->pSpec_Tax); /* TaxMan */
  FREESURFACE(pIcons->pSpec_Sci); /* Scientist */
  
  FREE(pIcons);
  
}


/* =================================================== */
/* ===================== THEME ======================= */
/* =================================================== */

/*
 *	Alloc and fill Theme struct
 */
void tilespec_setup_theme(void)
{
  struct Sprite *pBuf = NULL;
  
  pTheme = MALLOC(sizeof(struct Theme));
  
  if(!sprite_exists("theme.tech_tree")) {
    freelog(LOG_FATAL, "Your current tileset don't contains ""all"" GUI theme graphic\n"
    "Please use other tileset with ""full"" GUI graphic pack (use -t tileset options)\n"
    "If you don't have any tileset with SDLClient GUI theme then go to freeciv\n"
    "(ftp.freeciv.org/pub/freeciv/incoming) ftp site and download current DELUXE"
    "tileset theme");
  }
  
  load_theme_surface(pBuf, Button, "theme.button");
  load_theme_surface(pBuf, Edit, "theme.edit");
  load_theme_surface(pBuf, Vertic, "theme.vertic_scrollbar");
  load_theme_surface(pBuf, Horiz, "theme.horiz_scrollbar");
  load_theme_surface(pBuf, CBOX_Sell_Icon, "theme.sbox");
  load_theme_surface(pBuf, CBOX_Unsell_Icon, "theme.ubox");
  load_theme_surface(pBuf, Block, "theme.block");
  load_theme_surface(pBuf, FR_Vert, "theme.vertic_frame");
  load_theme_surface(pBuf, FR_Hor, "theme.horiz_frame");
  /* ------------------- */
  load_theme_surface(pBuf, OK_PACT_Icon, "theme.pact_ok");
  load_theme_surface(pBuf, CANCEL_PACT_Icon, "theme.pact_cancel");
  /* ------------------- */
  load_theme_surface(pBuf, Small_OK_Icon, "theme.SMALL_OK_button");
  load_theme_surface(pBuf, Small_CANCEL_Icon, "theme.SMALL_FAIL_button");
  /* ------------------- */
  load_theme_surface(pBuf, OK_Icon, "theme.OK_button");
  load_theme_surface(pBuf, CANCEL_Icon, "theme.FAIL_button");
  load_theme_surface(pBuf, FORWARD_Icon, "theme.NEXT_button");
  load_theme_surface(pBuf, BACK_Icon, "theme.BACK_button");
  load_theme_surface(pBuf, L_ARROW_Icon, "theme.LEFT_ARROW_button");
  load_theme_surface(pBuf, R_ARROW_Icon, "theme.RIGHT_ARROW_button");
  load_theme_surface(pBuf, META_Icon, "theme.META_button");
  load_theme_surface(pBuf, MAP_Icon, "theme.MAP_button");
  load_theme_surface(pBuf, FindCity_Icon, "theme.FIND_CITY_button");
  load_theme_surface(pBuf, NEW_TURN_Icon, "theme.NEW_TURN_button");
  load_theme_surface(pBuf, LOG_Icon, "theme.LOG_button");
  load_theme_surface(pBuf, UNITS_Icon, "theme.UNITS_INFO_button");
  load_theme_surface(pBuf, Options_Icon, "theme.OPTIONS_button");
  load_theme_surface(pBuf, INFO_Icon, "theme.INFO_button");
  load_theme_surface(pBuf, Army_Icon, "theme.ARMY_button");
  load_theme_surface(pBuf, Happy_Icon, "theme.HAPPY_button");
  load_theme_surface(pBuf, Support_Icon, "theme.HOME_button");
  load_theme_surface(pBuf, Buy_PROD_Icon, "theme.BUY_button");
  load_theme_surface(pBuf, PROD_Icon, "theme.PROD_button");
  load_theme_surface(pBuf, QPROD_Icon, "theme.WORK_LIST_button");
  load_theme_surface(pBuf, CMA_Icon, "theme.CMA_button");
  load_theme_surface(pBuf, LOCK_Icon, "theme.LOCK_button");
  load_theme_surface(pBuf, UNLOCK_Icon, "theme.UNLOCK_button");
  load_theme_surface(pBuf, PLAYERS_Icon, "theme.PLAYERS_button");
  load_theme_surface(pBuf, UNITS2_Icon, "theme.UNITS_button");
  load_theme_surface(pBuf, SAVE_Icon, "theme.SAVE_button");
  load_theme_surface(pBuf, LOAD_Icon, "theme.LOAD_button");
  load_theme_surface(pBuf, DELETE_Icon, "theme.DELETE_button");
  load_theme_surface(pBuf, BORDERS_Icon, "theme.BORDERS_button");
  /* ------------------------------ */
  load_theme_surface(pBuf, Tech_Tree_Icon, "theme.tech_tree");
  /* ------------------------------ */
  load_theme_surface(pBuf, UP_Icon, "theme.UP_scroll");
  load_theme_surface(pBuf, DOWN_Icon, "theme.DOWN_scroll");
#if 0
  load_theme_surface(pBuf, LEFT_Icon, "theme.LEFT_scroll");
  load_theme_surface(pBuf, RIGHT_Icon, "theme.RIGHT_button");
#endif
  /* ------------------------------ */

  load_order_theme_surface(pBuf, Order_Icon, "theme.order_empty");  
  load_order_theme_surface(pBuf, OAutoAtt_Icon, "theme.order_auto_attack");
  load_order_theme_surface(pBuf, OAutoConnect_Icon, "theme.order_auto_connect");  
  load_order_theme_surface(pBuf, OAutoExp_Icon, "theme.order_auto_explorer");
  load_order_theme_surface(pBuf, OAutoSett_Icon, "theme.order_auto_settler");
  load_order_theme_surface(pBuf, OBuildCity_Icon, "theme.order_build_city");
  load_order_theme_surface(pBuf, OCutDownForest_Icon, "theme.order_cutdown_forest");
  load_order_theme_surface(pBuf, OPlantForest_Icon, "theme.order_plant_forest");
  load_order_theme_surface(pBuf, OMine_Icon, "theme.order_build_mining");
  load_order_theme_surface(pBuf, OIrrigation_Icon, "theme.order_irrigation");
  load_order_theme_surface(pBuf, ODone_Icon, "theme.order_done");
  load_order_theme_surface(pBuf, ODisband_Icon, "theme.order_disband");
  load_order_theme_surface(pBuf, OFortify_Icon, "theme.order_fortify");
  load_order_theme_surface(pBuf, OGoto_Icon, "theme.order_goto");
  load_order_theme_surface(pBuf, OGotoCity_Icon, "theme.order_goto_city");
  load_order_theme_surface(pBuf, OHomeCity_Icon, "theme.order_home");  
  load_order_theme_surface(pBuf, ONuke_Icon, "theme.order_nuke");
  load_order_theme_surface(pBuf, OParaDrop_Icon, "theme.order_paradrop");
  load_order_theme_surface(pBuf, OPatrol_Icon, "theme.order_patrol");
  load_order_theme_surface(pBuf, OPillage_Icon, "theme.order_pillage");
  load_order_theme_surface(pBuf, ORailRoad_Icon, "theme.order_build_railroad");
  load_order_theme_surface(pBuf, ORoad_Icon, "theme.order_build_road");
  load_order_theme_surface(pBuf, OSentry_Icon, "theme.order_sentry");
  load_order_theme_surface(pBuf, OUnload_Icon, "theme.order_unload");
  load_order_theme_surface(pBuf, OWait_Icon, "theme.order_wait");
  load_order_theme_surface(pBuf, OFortress_Icon, "theme.order_build_fortress");
  load_order_theme_surface(pBuf, OFallout_Icon, "theme.order_clean_fallout");
  load_order_theme_surface(pBuf, OPollution_Icon, "theme.order_clean_pollution");
  load_order_theme_surface(pBuf, OAirBase_Icon, "theme.order_build_airbase");
  load_order_theme_surface(pBuf, OTransform_Icon, "theme.order_transform");
  load_order_theme_surface(pBuf, OAddCity_Icon, "theme.order_add_to_city");
  load_order_theme_surface(pBuf, OWonder_Icon, "theme.order_carravan_wonder");
  load_order_theme_surface(pBuf, OTrade_Icon, "theme.order_carravan_traderoute");
  load_order_theme_surface(pBuf, OSpy_Icon, "theme.order_spying");
  load_order_theme_surface(pBuf, OWakeUp_Icon, "theme.order_wakeup");
  load_order_theme_surface(pBuf, OReturn_Icon, "theme.order_return");
  load_order_theme_surface(pBuf, OAirLift_Icon, "theme.order_airlift");
  
  /* ------------------------------ */
    
  /* Map Dithering */
  
    if (is_isometric)
    {
      pBuf = sprites.dither_tile;
      pDitherMask = GET_SURF(pBuf);
      pBuf->psurface = NULL;
      unload_sprite("t.dither_tile");
      assert(pDitherMask != NULL);	  
  /* ------------------------------ */
  /* Map Borders */
      load_theme_surface(pBuf, NWEST_BORDER_Icon, "theme.normal_border_iso_west");
      load_theme_surface(pBuf, NNORTH_BORDER_Icon, "theme.normal_border_iso_north");
      load_theme_surface(pBuf, NSOUTH_BORDER_Icon, "theme.normal_border_iso_south");
      load_theme_surface(pBuf, NEAST_BORDER_Icon, "theme.normal_border_iso_east");
    }
  return;
}

/* Code come from SDL-dev list */
static SDL_Cursor *SurfaceToCursor(SDL_Surface *image, int hx, int hy) {
        int             w, x, y;
        Uint8           *data, *mask, *d, *m, r, g, b;
        Uint32          color;
        SDL_Cursor      *cursor;

        w = (image->w + 7) / 8;
        data = (Uint8 *)MALLOC(w * image->h * 2);
        if (data == NULL)
                return NULL;
        /*memset(data, 0, w * image->h * 2);*/
        mask = data + w * image->h;
	lock_surf(image);
        for (y = 0; y < image->h; y++) {
                d = data + y * w;
                m = mask + y * w;
                for (x = 0; x < image->w; x++) {
                        color = getpixel(image, x, y);
                        if ((image->flags & SDL_SRCCOLORKEY) == 0
			  || color != image->format->colorkey) {
                                SDL_GetRGB(color, image->format, &r, &g, &b);
                                color = (r + g + b) / 3;
                                m[x / 8] |= 128 >> (x & 7);
                                if (color < 128)
                                        d[x / 8] |= 128 >> (x & 7);
                        }
                }
        }
	unlock_surf(image);
        
        cursor = SDL_CreateCursor(data, mask, w * 8, image->h, hx, hy);
	
	FREE(data);
        return cursor;
}

#define load_cursor(iter, num, pSpr, image, cBuf, Type, Tag, x, y, center) \
do { \
  iter = 0;	\
  my_snprintf(cBuf , sizeof(cBuf), "%s_%d", Tag, iter);	\
  while(sprite_exists(cBuf)) {	\
    iter++;	\
    my_snprintf(cBuf , sizeof(cBuf), "%s_%d", Tag, iter);	\
  }	\
  num = iter;	\
  if (num) {	\
    pAnim->Cursors.Type = CALLOC(num + 1, sizeof(SDL_Cursor *));	\
    for( iter=0; iter<num; iter++) {	\
      my_snprintf(cBuf,sizeof(cBuf), "%s_%d", Tag, iter);	\
      pSpr = load_sprite(cBuf);	\
      image = (pSpr ? GET_SURF(pSpr) : NULL);	\
      assert(image != NULL);	\
      if (center) {	\
        pAnim->Cursors.Type[iter] = SurfaceToCursor(image, image->w/2, image->h/2);	\
      } else {	\
	pAnim->Cursors.Type[iter] = SurfaceToCursor(image, x, y);	\
      }	\
      unload_sprite(cBuf);	\
    }	\
  }	\
} while(0)

/*
 *	Alloc and fill Animation struct
 */
void tilespec_setup_anim(void)
{
  char buf[32];	/* I hope this is enought :) */
  struct Sprite *pSpr = NULL;
  SDL_Surface *image = NULL;
  int i, num;
  pAnim = MALLOC(sizeof(struct Animation));
    
  i = 0;
  my_snprintf(buf , sizeof(buf), "explode.iso_nuke_%d", i);
  while(sprite_exists(buf)) {
    i++;
    my_snprintf(buf , sizeof(buf), "explode.iso_nuke_%d", i);
  }
  pAnim->num_tiles_explode_nuke = i;
  
  /* focus unit animation */
  i = 0;
  my_snprintf(buf , sizeof(buf), "anim.focus_%d", i);
  while(sprite_exists(buf)) {
    i++;
    my_snprintf(buf , sizeof(buf), "anim.focus_%d", i);
  }
  pAnim->num_tiles_focused_unit = i;
  
  pAnim->Focus = CALLOC(pAnim->num_tiles_focused_unit, sizeof(SDL_Surface *));
  for( i=0; i<pAnim->num_tiles_focused_unit; i++) {
    my_snprintf(buf,sizeof(buf), "anim.focus_%d", i);
    load_GUI_surface(pSpr, pAnim, Focus[i], buf);
  }
  
  /* load cursors */
  load_cursor(i, num, pSpr, image, buf, Patrol, "anim.patrol_cursor", 0, 0, TRUE);
  load_cursor(i, num, pSpr, image, buf, Goto, "anim.goto_cursor", 0, 0, TRUE);
  load_cursor(i, num, pSpr, image, buf, Connect, "anim.connect_cursor", 0, 0, TRUE);
  load_cursor(i, num, pSpr, image, buf, Nuke, "anim.nuke_cursor", 0, 0, TRUE);
  load_cursor(i, num, pSpr, image, buf, Paradrop, "anim.paradrop_cursor", 0, 0, TRUE);
  
  load_cursor(i, num, pSpr, image, buf, MapScroll[SCROLL_NORTH], "anim.scroll_north_cursor", 20, 3, FALSE);
  load_cursor(i, num, pSpr, image, buf, MapScroll[SCROLL_SOUTH], "anim.scroll_south_cursor", 20, 37, FALSE);
  load_cursor(i, num, pSpr, image, buf, MapScroll[SCROLL_EAST], "anim.scroll_east_cursor", 37, 20, FALSE);
  load_cursor(i, num, pSpr, image, buf, MapScroll[SCROLL_WEST], "anim.scroll_west_cursor", 3, 20, FALSE);
  
}

void tilespec_free_anim(void)
{
  int i,j;
  for(i=0; i<pAnim->num_tiles_focused_unit; i++) {
    FREESURFACE(pAnim->Focus[i]);
  }
  FREE(pAnim->Focus);
  
  for(i=0; pAnim->Cursors.Patrol[i]; i++) {
    SDL_FreeCursor(pAnim->Cursors.Patrol[i]);
  }
  for(i=0; pAnim->Cursors.Goto[i]; i++) {
    SDL_FreeCursor(pAnim->Cursors.Goto[i]);
  }
  for(i=0; pAnim->Cursors.Connect[i]; i++) {
    SDL_FreeCursor(pAnim->Cursors.Connect[i]);
  }
  for(i=0; pAnim->Cursors.Nuke[i]; i++) {
    SDL_FreeCursor(pAnim->Cursors.Nuke[i]);
  }
  for(i=0; pAnim->Cursors.Paradrop[i]; i++) {
    SDL_FreeCursor(pAnim->Cursors.Paradrop[i]);
  }
  for (i = 0; i < SCROLL_LAST; i++) {
    for (j = 0; pAnim->Cursors.MapScroll[i][j]; j++) {
      SDL_FreeCursor(pAnim->Cursors.MapScroll[i][j]);
    }
  }
  
  FREE(pAnim);
}

/*
 *	Free memmory
 */
void tilespec_unload_theme(void)
{
  FREESURFACE(pTheme->Button);
  FREESURFACE(pTheme->Edit);
  FREESURFACE(pTheme->Vertic);
  FREESURFACE(pTheme->Horiz);
  FREESURFACE(pTheme->CBOX_Sell_Icon);
  FREESURFACE(pTheme->CBOX_Unsell_Icon);
  FREESURFACE(pTheme->Block);
  FREESURFACE(pTheme->FR_Vert);
  FREESURFACE(pTheme->FR_Hor);
  /* ------------------- */
  
  FREESURFACE(pTheme->OK_Icon);
  FREESURFACE(pTheme->CANCEL_Icon);
  FREESURFACE(pTheme->Small_OK_Icon);
  FREESURFACE(pTheme->Small_CANCEL_Icon);
  FREESURFACE(pTheme->FORWARD_Icon);
  FREESURFACE(pTheme->BACK_Icon);
  FREESURFACE(pTheme->L_ARROW_Icon);
  FREESURFACE(pTheme->R_ARROW_Icon);
  FREESURFACE(pTheme->META_Icon);
  FREESURFACE(pTheme->MAP_Icon);
  FREESURFACE(pTheme->FindCity_Icon);
  FREESURFACE(pTheme->NEW_TURN_Icon);
  FREESURFACE(pTheme->LOG_Icon);
  FREESURFACE(pTheme->UNITS_Icon);
  FREESURFACE(pTheme->UNITS2_Icon);
  FREESURFACE(pTheme->PLAYERS_Icon);
  FREESURFACE(pTheme->Options_Icon);
  FREESURFACE(pTheme->INFO_Icon);
  FREESURFACE(pTheme->Army_Icon);
  FREESURFACE(pTheme->Happy_Icon);
  FREESURFACE(pTheme->Support_Icon);
  FREESURFACE(pTheme->Buy_PROD_Icon);
  FREESURFACE(pTheme->PROD_Icon);
  FREESURFACE(pTheme->QPROD_Icon);
  FREESURFACE(pTheme->CMA_Icon);
  FREESURFACE(pTheme->LOCK_Icon);
  FREESURFACE(pTheme->UNLOCK_Icon);
  FREESURFACE(pTheme->OK_PACT_Icon);
  FREESURFACE(pTheme->CANCEL_PACT_Icon);
  FREESURFACE(pTheme->SAVE_Icon);
  FREESURFACE(pTheme->LOAD_Icon);
  FREESURFACE(pTheme->DELETE_Icon);
  FREESURFACE(pTheme->BORDERS_Icon);
  /* ------------------------------ */
  FREESURFACE(pTheme->Tech_Tree_Icon);
  /* ------------------------------ */
  FREESURFACE(pTheme->UP_Icon);
  FREESURFACE(pTheme->DOWN_Icon);
#if 0
  FREESURFACE(pTheme->LEFT_Icon);
  FREESURFACE(pTheme->RIGHT_Icon);
#endif
  /* ------------------------------ */

  FREESURFACE(pTheme->Order_Icon);
  FREESURFACE(pTheme->OAutoAtt_Icon);
  FREESURFACE(pTheme->OAutoConnect_Icon);
  FREESURFACE(pTheme->OAutoExp_Icon);
  FREESURFACE(pTheme->OAutoSett_Icon);
  FREESURFACE(pTheme->OBuildCity_Icon);
  FREESURFACE(pTheme->OCutDownForest_Icon);
  FREESURFACE(pTheme->OPlantForest_Icon);
  FREESURFACE(pTheme->OMine_Icon);
  FREESURFACE(pTheme->OIrrigation_Icon);
  FREESURFACE(pTheme->ODone_Icon);
  FREESURFACE(pTheme->ODisband_Icon);
  FREESURFACE(pTheme->OFortify_Icon);
  FREESURFACE(pTheme->OGoto_Icon);
  FREESURFACE(pTheme->OGotoCity_Icon);
  FREESURFACE(pTheme->OHomeCity_Icon);
  FREESURFACE(pTheme->ONuke_Icon);
  FREESURFACE(pTheme->OParaDrop_Icon);
  FREESURFACE(pTheme->OPatrol_Icon);
  FREESURFACE(pTheme->OPillage_Icon);
  FREESURFACE(pTheme->ORailRoad_Icon);
  FREESURFACE(pTheme->ORoad_Icon);
  FREESURFACE(pTheme->OSentry_Icon);
  FREESURFACE(pTheme->OUnload_Icon);
  FREESURFACE(pTheme->OWait_Icon);
  FREESURFACE(pTheme->OFortress_Icon);
  FREESURFACE(pTheme->OFallout_Icon);
  FREESURFACE(pTheme->OPollution_Icon);
  FREESURFACE(pTheme->OAirBase_Icon);
  FREESURFACE(pTheme->OTransform_Icon);
  FREESURFACE(pTheme->OAddCity_Icon);
  FREESURFACE(pTheme->OWonder_Icon);
  FREESURFACE(pTheme->OTrade_Icon);
  FREESURFACE(pTheme->OSpy_Icon);
  FREESURFACE(pTheme->OWakeUp_Icon);
  FREESURFACE(pTheme->OReturn_Icon);
  FREESURFACE(pTheme->OAirLift_Icon);

  /* Map Dithering */
   
  FREESURFACE(pDitherMask);
  	
  /* Map Borders */
  FREESURFACE(pTheme->NWEST_BORDER_Icon);
  FREESURFACE(pTheme->NNORTH_BORDER_Icon);
  FREESURFACE(pTheme->NSOUTH_BORDER_Icon);
  FREESURFACE(pTheme->NEAST_BORDER_Icon);
	
  FREE(pTheme);
  return;
}

/**************************************************************************
  Return a surface for the given citizen.  The citizen's type is given,
  as well as their index (in the range [0..pcity->size)).
**************************************************************************/
SDL_Surface * get_citizen_surface(struct citizen_type type,
				  int citizen_index)
{
  assert(type >= 0 && type < NUM_TILES_CITIZEN);
  citizen_index %= sprites.citizen[type].count;
  return citizen[type].surface[citizen_index];
}

void unload_unused_graphics(void)
{
  unload_sprite("treaty.disagree_thumb_down");
  unload_sprite("treaty.agree_thumb_up");
  unload_sprite("spaceship.solar_panels");
  unload_sprite("spaceship.life_support");
  unload_sprite("spaceship.habitation");
  unload_sprite("spaceship.structural");
  unload_sprite("spaceship.fuel");
  unload_sprite("spaceship.propulsion");
  unload_sprite("citizen.entertainer");
  unload_sprite("citizen.scientist");
  unload_sprite("citizen.tax_collector");
  unload_sprite("citizen.content_0");
  unload_sprite("citizen.content_1");
  unload_sprite("citizen.happy_0");
  unload_sprite("citizen.happy_1");
  unload_sprite("citizen.unhappy_0");
  unload_sprite("citizen.unhappy_1");
  unload_sprite("citizen.angry_0");
  unload_sprite("citizen.angry_1");
  unload_sprite("s.right_arrow");
  if (sprite_exists("t.coast_color"))
  {
    unload_sprite("t.coast_color");
  }
  unload_sprite("upkeep.gold");
  unload_sprite("upkeep.gold2");
  unload_sprite("upkeep.food");
  unload_sprite("upkeep.food2");
  unload_sprite("upkeep.unhappy");
  unload_sprite("upkeep.unhappy2");
  unload_sprite("upkeep.shield");
  if (is_isometric && sprite_exists("explode.iso_nuke"))
  {
    unload_sprite("explode.iso_nuke");
  }
}
