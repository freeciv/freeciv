/***********************************************************************
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

/***********************************************************************
                          gui_tilespec.c  -  description
                             -------------------
    begin                : Dec. 2 2002
    copyright            : (C) 2002 by Rafał Bursig
    email                : Rafal Bursig <bursig@poczta.fm>
***********************************************************************/

#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

/* SDL2 */
#ifdef SDL2_PLAIN_INCLUDE
#include <SDL.h>
#else  /* SDL2_PLAIN_INCLUDE */
#include <SDL2/SDL.h>
#endif /* SDL2_PLAIN_INCLUDE */

/* utility */
#include "fcintl.h"
#include "log.h"

/* common */
#include "research.h"
#include "specialist.h"

/* client */
#include "client_main.h"

/* gui-sdl2 */
#include "colors.h"
#include "graphics.h"
#include "gui_main.h"
#include "gui_string.h"
#include "sprite.h"
#include "themespec.h"

#include "gui_tilespec.h"

struct Theme *current_theme = NULL;
struct City_Icon *pIcons;

static SDL_Surface *city_surf;

static SDL_Surface *pNeutral_Tech_Icon;
static SDL_Surface *pNone_Tech_Icon;
static SDL_Surface *pFuture_Tech_Icon;

#define load_GUI_surface(pSpr, pStruct, surf, tag)		  \
do {								  \
  pSpr = theme_lookup_sprite_tag_alt(theme, LOG_FATAL, tag, "", "", ""); \
  fc_assert_action(pSpr != NULL, break);                          \
  pStruct->surf = GET_SURF_REAL(pSpr);                           \
} while (FALSE)

#define load_theme_surface(pSpr, surf, tag)		\
	load_GUI_surface(pSpr, current_theme, surf, tag)

#define load_city_icon_surface(pSpr, surf, tag)        \
        load_GUI_surface(pSpr, pIcons, surf, tag)

#define load_order_theme_surface(pSpr, surf, tag)	\
        load_GUI_surface(pSpr, current_theme, surf, tag);

/***************************************************************************//**
  Reload small citizens "style" icons.
*******************************************************************************/
static void reload_small_citizens_icons(int style)
{
  int i;
  int spe_max;

  /* free info icons */
  FREESURFACE(pIcons->pMale_Content);
  FREESURFACE(pIcons->pFemale_Content);
  FREESURFACE(pIcons->pMale_Happy);
  FREESURFACE(pIcons->pFemale_Happy);
  FREESURFACE(pIcons->pMale_Unhappy);
  FREESURFACE(pIcons->pFemale_Unhappy);
  FREESURFACE(pIcons->pMale_Angry);
  FREESURFACE(pIcons->pFemale_Angry);

  spe_max = specialist_count();
  for (i = 0; i < spe_max; i++) {
    FREESURFACE(pIcons->specialists[i]);
  }

  /* allocate icons */
  pIcons->pMale_Happy = adj_surf(get_citizen_surface(CITIZEN_HAPPY, 0));
  pIcons->pFemale_Happy = adj_surf(get_citizen_surface(CITIZEN_HAPPY, 1));
  pIcons->pMale_Content = adj_surf(get_citizen_surface(CITIZEN_CONTENT, 0));
  pIcons->pFemale_Content = adj_surf(get_citizen_surface(CITIZEN_CONTENT, 1));
  pIcons->pMale_Unhappy = adj_surf(get_citizen_surface(CITIZEN_UNHAPPY, 0));
  pIcons->pFemale_Unhappy = adj_surf(get_citizen_surface(CITIZEN_UNHAPPY, 1));
  pIcons->pMale_Angry = adj_surf(get_citizen_surface(CITIZEN_ANGRY, 0));
  pIcons->pFemale_Angry = adj_surf(get_citizen_surface(CITIZEN_ANGRY, 1));

  for (i = 0; i < spe_max; i++) {
    pIcons->specialists[i] = adj_surf(get_citizen_surface(CITIZEN_SPECIALIST + i, 0));
  }
}

/* ================================================================================= */
/* ===================================== Public ==================================== */
/* ================================================================================= */

/***************************************************************************//**
  Set city citizens icons sprite value; should only happen after
  start of game (city style struct was filled ).
*******************************************************************************/
void reload_citizens_icons(int style)
{
  reload_small_citizens_icons(style);
  pIcons->style = style;
}

/***************************************************************************//**
  Load theme city screen graphics.
*******************************************************************************/
void tilespec_setup_city_gfx(void) {
  struct sprite *pSpr =
    theme_lookup_sprite_tag_alt(theme, LOG_FATAL, "theme.city", "", "", "");

  city_surf = (pSpr ? adj_surf(GET_SURF_REAL(pSpr)) : NULL);

  fc_assert(city_surf != NULL);
}

/***************************************************************************//**
  Free theme city screen graphics.
*******************************************************************************/
void tilespec_free_city_gfx(void)
{
  /* There was two copies of this. One is freed from the sprite hash, but
   * one created by adj_surf() in tilespec_setup_city_gfx() is freed here. */
  FREESURFACE(city_surf);
}

/***************************************************************************//**
  Set city icons sprite value; should only happen after
  tileset_load_tiles(tileset).
*******************************************************************************/
void tilespec_setup_city_icons(void)
{
  struct sprite *pSpr = NULL;

  pIcons = ( struct City_Icon *)fc_calloc(1,  sizeof( struct City_Icon ));

  load_city_icon_surface(pSpr, pBIG_Food_Corr, "city.food_waste");
  load_city_icon_surface(pSpr, pBIG_Shield_Corr, "city.shield_waste");
  load_city_icon_surface(pSpr, pBIG_Trade_Corr, "city.trade_waste");
  load_city_icon_surface(pSpr, pBIG_Food, "city.food");

  pIcons->pBIG_Food_Surplus = crop_rect_from_surface(pIcons->pBIG_Food, NULL);
  SDL_SetSurfaceAlphaMod(pIcons->pBIG_Food_Surplus, 128);

  load_city_icon_surface(pSpr, pBIG_Shield, "city.shield");

  pIcons->pBIG_Shield_Surplus = crop_rect_from_surface(pIcons->pBIG_Shield, NULL);
  SDL_SetSurfaceAlphaMod(pIcons->pBIG_Shield_Surplus, 128);

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

  create_frame(pIcons->pWorklist,
               0, 0, pIcons->pWorklist->w - 1, pIcons->pWorklist->h - 1,
               get_theme_color(COLOR_THEME_CITYREP_FRAME));
  create_line(pIcons->pWorklist,
              3, 2, 5, 2,
              get_theme_color(COLOR_THEME_CITYREP_FRAME));
  create_line(pIcons->pWorklist,
              3, 4, 7, 4,
              get_theme_color(COLOR_THEME_CITYREP_FRAME));
  create_line(pIcons->pWorklist,
              3, 6, 6, 6,
              get_theme_color(COLOR_THEME_CITYREP_FRAME));

  /* ================================================================= */

  /* force reload citizens icons */
  pIcons->style = 999;
}

/***************************************************************************//**
  Free resources associated with city screen icons.
*******************************************************************************/
void tilespec_free_city_icons(void)
{
  int i;
  int spe_max;

  if (!pIcons) {
    return;
  }

  /* small citizens */
  FREESURFACE(pIcons->pMale_Content);
  FREESURFACE(pIcons->pFemale_Content);
  FREESURFACE(pIcons->pMale_Happy);
  FREESURFACE(pIcons->pFemale_Happy);
  FREESURFACE(pIcons->pMale_Unhappy);
  FREESURFACE(pIcons->pFemale_Unhappy);
  FREESURFACE(pIcons->pMale_Angry);
  FREESURFACE(pIcons->pFemale_Angry);

  spe_max = specialist_count();
  for (i = 0; i < spe_max; i++) {
    FREESURFACE(pIcons->specialists[i]);
  }

  FC_FREE(pIcons);
}

/* =================================================== */
/* ===================== THEME ======================= */
/* =================================================== */

/***************************************************************************//**
  Alloc and fill Theme struct
*******************************************************************************/
void tilespec_setup_theme(void)
{
  struct sprite *buf = NULL;

  current_theme = fc_calloc(1, sizeof(struct Theme));

  load_theme_surface(buf, FR_Left, "theme.left_frame");
  load_theme_surface(buf, FR_Right, "theme.right_frame");
  load_theme_surface(buf, FR_Top, "theme.top_frame");
  load_theme_surface(buf, FR_Bottom, "theme.bottom_frame");
  load_theme_surface(buf, button, "theme.button");
  load_theme_surface(buf, Edit, "theme.edit");
  load_theme_surface(buf, CBOX_Sell_Icon, "theme.sbox");
  load_theme_surface(buf, CBOX_Unsell_Icon, "theme.ubox");
  load_theme_surface(buf, UP_Icon, "theme.UP_scroll");
  load_theme_surface(buf, DOWN_Icon, "theme.DOWN_scroll");
#if 0
  load_theme_surface(buf, LEFT_Icon, "theme.LEFT_scroll");
  load_theme_surface(buf, RIGHT_Icon, "theme.RIGHT_scroll");
#endif
  load_theme_surface(buf, Vertic, "theme.vertic_scrollbar");
  load_theme_surface(buf, Horiz, "theme.horiz_scrollbar");

  /* ------------------- */
  load_theme_surface(buf, OK_PACT_Icon, "theme.pact_ok");
  load_theme_surface(buf, CANCEL_PACT_Icon, "theme.pact_cancel");
  /* ------------------- */
  load_theme_surface(buf, Small_OK_Icon, "theme.SMALL_OK_button");
  load_theme_surface(buf, Small_CANCEL_Icon, "theme.SMALL_FAIL_button");
  /* ------------------- */
  load_theme_surface(buf, OK_Icon, "theme.OK_button");
  load_theme_surface(buf, CANCEL_Icon, "theme.FAIL_button");
  load_theme_surface(buf, FORWARD_Icon, "theme.NEXT_button");
  load_theme_surface(buf, BACK_Icon, "theme.BACK_button");
  load_theme_surface(buf, L_ARROW_Icon, "theme.LEFT_ARROW_button");
  load_theme_surface(buf, R_ARROW_Icon, "theme.RIGHT_ARROW_button");
  load_theme_surface(buf, MAP_Icon, "theme.MAP_button");
  load_theme_surface(buf, FindCity_Icon, "theme.FIND_CITY_button");
  load_theme_surface(buf, NEW_TURN_Icon, "theme.NEW_TURN_button");
  load_theme_surface(buf, LOG_Icon, "theme.LOG_button");
  load_theme_surface(buf, UNITS_Icon, "theme.UNITS_INFO_button");
  load_theme_surface(buf, Options_Icon, "theme.OPTIONS_button");
  load_theme_surface(buf, Block, "theme.block");
  load_theme_surface(buf, INFO_Icon, "theme.INFO_button");
  load_theme_surface(buf, Army_Icon, "theme.ARMY_button");
  load_theme_surface(buf, Happy_Icon, "theme.HAPPY_button");
  load_theme_surface(buf, Support_Icon, "theme.HOME_button");
  load_theme_surface(buf, Buy_PROD_Icon, "theme.BUY_button");
  load_theme_surface(buf, PROD_Icon, "theme.PROD_button");
  load_theme_surface(buf, QPROD_Icon, "theme.WORK_LIST_button");
  load_theme_surface(buf, CMA_Icon, "theme.CMA_button");
  load_theme_surface(buf, LOCK_Icon, "theme.LOCK_button");
  load_theme_surface(buf, UNLOCK_Icon, "theme.UNLOCK_button");
  load_theme_surface(buf, PLAYERS_Icon, "theme.PLAYERS_button");
  load_theme_surface(buf, UNITS2_Icon, "theme.UNITS_button");
  load_theme_surface(buf, SAVE_Icon, "theme.SAVE_button");
  load_theme_surface(buf, LOAD_Icon, "theme.LOAD_button");
  load_theme_surface(buf, DELETE_Icon, "theme.DELETE_button");
  load_theme_surface(buf, BORDERS_Icon, "theme.BORDERS_button");
  /* ------------------------------ */
  load_theme_surface(buf, Tech_Tree_Icon, "theme.tech_tree");
  /* ------------------------------ */

  load_order_theme_surface(buf, Order_Icon, "theme.order_empty");
  load_order_theme_surface(buf, OAutoAtt_Icon, "theme.order_auto_attack");
  load_order_theme_surface(buf, OAutoConnect_Icon, "theme.order_auto_connect");
  load_order_theme_surface(buf, OAutoExp_Icon, "theme.order_auto_explorer");
  load_order_theme_surface(buf, OAutoSett_Icon, "theme.order_auto_settler");
  load_order_theme_surface(buf, OBuildCity_Icon, "theme.order_build_city");
  load_order_theme_surface(buf, OCutDownForest_Icon, "theme.order_cutdown_forest");
  load_order_theme_surface(buf, OPlantForest_Icon, "theme.order_plant_forest");
  load_order_theme_surface(buf, OMine_Icon, "theme.order_build_mining");
  load_order_theme_surface(buf, OIrrigation_Icon, "theme.order_irrigation");
  load_order_theme_surface(buf, OCultivate_Icon, "theme.order_cutdown_forest");
  load_order_theme_surface(buf, OPlant_Icon, "theme.order_plant_forest");
  load_order_theme_surface(buf, ODone_Icon, "theme.order_done");
  load_order_theme_surface(buf, ODisband_Icon, "theme.order_disband");
  load_order_theme_surface(buf, OFortify_Icon, "theme.order_fortify");
  load_order_theme_surface(buf, OGoto_Icon, "theme.order_goto");
  load_order_theme_surface(buf, OGotoCity_Icon, "theme.order_goto_city");
  load_order_theme_surface(buf, OHomeCity_Icon, "theme.order_home");
  load_order_theme_surface(buf, ONuke_Icon, "theme.order_nuke");
  load_order_theme_surface(buf, OParaDrop_Icon, "theme.order_paradrop");
  load_order_theme_surface(buf, OPatrol_Icon, "theme.order_patrol");
  load_order_theme_surface(buf, OPillage_Icon, "theme.order_pillage");
  load_order_theme_surface(buf, ORailRoad_Icon, "theme.order_build_railroad");
  load_order_theme_surface(buf, ORoad_Icon, "theme.order_build_road");
  load_order_theme_surface(buf, OSentry_Icon, "theme.order_sentry");
  load_order_theme_surface(buf, OUnload_Icon, "theme.order_unload");
  load_order_theme_surface(buf, OWait_Icon, "theme.order_wait");
  load_order_theme_surface(buf, OFortress_Icon, "theme.order_build_fortress");
  load_order_theme_surface(buf, OFallout_Icon, "theme.order_clean_fallout");
  load_order_theme_surface(buf, OPollution_Icon, "theme.order_clean_pollution");
  load_order_theme_surface(buf, OAirBase_Icon, "theme.order_build_airbase");
  load_order_theme_surface(buf, OTransform_Icon, "theme.order_transform");
  load_order_theme_surface(buf, OAddCity_Icon, "theme.order_add_to_city");
  load_order_theme_surface(buf, OWonder_Icon, "theme.order_carravan_wonder");
  load_order_theme_surface(buf, OTrade_Icon, "theme.order_carravan_trade_route");
  load_order_theme_surface(buf, OSpy_Icon, "theme.order_spying");
  load_order_theme_surface(buf, OWakeUp_Icon, "theme.order_wakeup");
  load_order_theme_surface(buf, OReturn_Icon, "theme.order_return");
  load_order_theme_surface(buf, OAirLift_Icon, "theme.order_airlift");
  load_order_theme_surface(buf, OLoad_Icon, "theme.order_load");
}

/***************************************************************************//**
  Free theme memory
*******************************************************************************/
void tilespec_free_theme(void)
{
  if (!current_theme) {
    return;
  }

  FC_FREE(current_theme);
  current_theme = NULL;
}

/***************************************************************************//**
  Setup icons for special (non-real) technologies.
*******************************************************************************/
void setup_auxiliary_tech_icons(void)
{
  SDL_Color bg_color = {255, 255, 255, 136};
  SDL_Surface *surf;
  utf8_str *pstr = create_utf8_from_char(Q_("?tech:None"), adj_font(10));

  pstr->style |= (TTF_STYLE_BOLD | SF_CENTER);

  /* create icons */
  surf = create_surf(adj_size(50), adj_size(50), SDL_SWSURFACE);
  SDL_FillRect(surf, NULL, map_rgba(surf->format, bg_color));
  create_frame(surf,
               0 , 0, surf->w - 1, surf->h - 1,
               get_theme_color(COLOR_THEME_SCIENCEDLG_FRAME));

  pNeutral_Tech_Icon = copy_surface(surf);
  pNone_Tech_Icon = copy_surface(surf);
  pFuture_Tech_Icon = surf;

  /* None */
  surf = create_text_surf_from_utf8(pstr);
  blit_entire_src(surf, pNone_Tech_Icon ,
                  (adj_size(50) - surf->w) / 2 , (adj_size(50) - surf->h) / 2);

  FREESURFACE(surf);

  /* TRANS: Future Technology */ 
  copy_chars_to_utf8_str(pstr, _("FT"));
  surf = create_text_surf_from_utf8(pstr);
  blit_entire_src(surf, pFuture_Tech_Icon,
                  (adj_size(50) - surf->w) / 2 , (adj_size(50) - surf->h) / 2);

  FREESURFACE(surf);

  FREEUTF8STR(pstr);
}

/***************************************************************************//**
  Free resources associated with aux tech icons.
*******************************************************************************/
void free_auxiliary_tech_icons(void)
{
  FREESURFACE(pNeutral_Tech_Icon);
  FREESURFACE(pNone_Tech_Icon);
  FREESURFACE(pFuture_Tech_Icon);
}

/***************************************************************************//**
  Return tech icon surface.
*******************************************************************************/
SDL_Surface *get_tech_icon(Tech_type_id tech)
{
  switch (tech) {
  case A_NONE:
  case A_UNSET:
  case A_UNKNOWN:
  case A_LAST:
    return adj_surf(pNone_Tech_Icon);
  case A_FUTURE:
    return adj_surf(pFuture_Tech_Icon);
  default:
    if (get_tech_sprite(tileset, tech)) {
      return adj_surf(GET_SURF(get_tech_sprite(tileset, tech)));
    } else {
      return adj_surf(pNeutral_Tech_Icon);
    }
  }

  return NULL;
}

/***************************************************************************//**
  Return color associated with current tech knowledge state.
*******************************************************************************/
SDL_Color *get_tech_color(Tech_type_id tech_id)
{
  if (research_invention_gettable(research_get(client_player()),
                                  tech_id, TRUE)) {
    switch (research_invention_state(research_get(client_player()),
                                     tech_id)) {
    case TECH_UNKNOWN:
      return get_game_color(COLOR_REQTREE_UNKNOWN);
    case TECH_KNOWN:
      return get_game_color(COLOR_REQTREE_KNOWN);
    case TECH_PREREQS_KNOWN:
      return get_game_color(COLOR_REQTREE_PREREQS_KNOWN);
    default:
      return get_game_color(COLOR_REQTREE_BACKGROUND);
    }
  }
  return get_game_color(COLOR_REQTREE_UNREACHABLE);
}

/***************************************************************************//**
  Return current city screen graphics
*******************************************************************************/
SDL_Surface *get_city_gfx(void)
{
  return city_surf;
}

/***************************************************************************//**
  Draw theme intro gfx.
*******************************************************************************/
void draw_intro_gfx(void)
{
  SDL_Surface *pIntro = theme_get_background(theme, BACKGROUND_MAINPAGE);

  if (pIntro->w != main_window_width()) {
    SDL_Surface *pTmp = ResizeSurface(pIntro, main_window_width(), main_window_height(), 1);

    FREESURFACE(pIntro);
    pIntro = pTmp;
  }

  /* draw intro gfx center in screen */
  alphablit(pIntro, NULL, main_data.map, NULL, 255);

  FREESURFACE(pIntro);
}
