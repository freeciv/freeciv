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

struct theme_icons *current_theme = NULL;
struct city_icon *icons;

static SDL_Surface *city_surf;

static SDL_Surface *pNeutral_Tech_Icon;
static SDL_Surface *pNone_Tech_Icon;
static SDL_Surface *pFuture_Tech_Icon;

#define load_GUI_surface(_spr_, _struct_, _surf_, _tag_)	  \
do {								  \
  _spr_ = theme_lookup_sprite_tag_alt(theme, LOG_FATAL, _tag_, "", "", ""); \
  fc_assert_action(_spr_ != NULL, break);                          \
  _struct_->_surf_ = GET_SURF_REAL(_spr_);                           \
} while (FALSE)

#define load_theme_surface(spr, surf, tag)		\
	load_GUI_surface(spr, current_theme, surf, tag)

#define load_city_icon_surface(spr, surf, tag)        \
        load_GUI_surface(spr, icons, surf, tag)

#define load_order_theme_surface(spr, surf, tag)	\
        load_GUI_surface(spr, current_theme, surf, tag);

/***************************************************************************//**
  Reload small citizens "style" icons.
*******************************************************************************/
static void reload_small_citizens_icons(int style)
{
  int i;
  int spe_max;

  /* free info icons */
  FREESURFACE(icons->pMale_Content);
  FREESURFACE(icons->pFemale_Content);
  FREESURFACE(icons->pMale_Happy);
  FREESURFACE(icons->pFemale_Happy);
  FREESURFACE(icons->pMale_Unhappy);
  FREESURFACE(icons->pFemale_Unhappy);
  FREESURFACE(icons->pMale_Angry);
  FREESURFACE(icons->pFemale_Angry);

  spe_max = specialist_count();
  for (i = 0; i < spe_max; i++) {
    FREESURFACE(icons->specialists[i]);
  }

  /* allocate icons */
  icons->pMale_Happy = adj_surf(get_citizen_surface(CITIZEN_HAPPY, 0));
  icons->pFemale_Happy = adj_surf(get_citizen_surface(CITIZEN_HAPPY, 1));
  icons->pMale_Content = adj_surf(get_citizen_surface(CITIZEN_CONTENT, 0));
  icons->pFemale_Content = adj_surf(get_citizen_surface(CITIZEN_CONTENT, 1));
  icons->pMale_Unhappy = adj_surf(get_citizen_surface(CITIZEN_UNHAPPY, 0));
  icons->pFemale_Unhappy = adj_surf(get_citizen_surface(CITIZEN_UNHAPPY, 1));
  icons->pMale_Angry = adj_surf(get_citizen_surface(CITIZEN_ANGRY, 0));
  icons->pFemale_Angry = adj_surf(get_citizen_surface(CITIZEN_ANGRY, 1));

  for (i = 0; i < spe_max; i++) {
    icons->specialists[i] = adj_surf(get_citizen_surface(CITIZEN_SPECIALIST + i, 0));
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
  icons->style = style;
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

  icons = (struct city_icon *)fc_calloc(1, sizeof(struct city_icon));

  load_city_icon_surface(pSpr, pBIG_Food_Corr, "city.food_waste");
  load_city_icon_surface(pSpr, pBIG_Shield_Corr, "city.shield_waste");
  load_city_icon_surface(pSpr, pBIG_Trade_Corr, "city.trade_waste");
  load_city_icon_surface(pSpr, pBIG_Food, "city.food");

  icons->pBIG_Food_Surplus = crop_rect_from_surface(icons->pBIG_Food, NULL);
  SDL_SetSurfaceAlphaMod(icons->pBIG_Food_Surplus, 128);

  load_city_icon_surface(pSpr, pBIG_Shield, "city.shield");

  icons->pBIG_Shield_Surplus = crop_rect_from_surface(icons->pBIG_Shield, NULL);
  SDL_SetSurfaceAlphaMod(icons->pBIG_Shield_Surplus, 128);

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
  icons->pWorklist = create_surf(9,9, SDL_SWSURFACE);
  SDL_FillRect(icons->pWorklist, NULL,
               SDL_MapRGB(icons->pWorklist->format, 255, 255,255));

  create_frame(icons->pWorklist,
               0, 0, icons->pWorklist->w - 1, icons->pWorklist->h - 1,
               get_theme_color(COLOR_THEME_CITYREP_FRAME));
  create_line(icons->pWorklist,
              3, 2, 5, 2,
              get_theme_color(COLOR_THEME_CITYREP_FRAME));
  create_line(icons->pWorklist,
              3, 4, 7, 4,
              get_theme_color(COLOR_THEME_CITYREP_FRAME));
  create_line(icons->pWorklist,
              3, 6, 6, 6,
              get_theme_color(COLOR_THEME_CITYREP_FRAME));

  /* ================================================================= */

  /* force reload citizens icons */
  icons->style = 999;
}

/***************************************************************************//**
  Free resources associated with city screen icons.
*******************************************************************************/
void tilespec_free_city_icons(void)
{
  int i;
  int spe_max;

  if (!icons) {
    return;
  }

  /* small citizens */
  FREESURFACE(icons->pMale_Content);
  FREESURFACE(icons->pFemale_Content);
  FREESURFACE(icons->pMale_Happy);
  FREESURFACE(icons->pFemale_Happy);
  FREESURFACE(icons->pMale_Unhappy);
  FREESURFACE(icons->pFemale_Unhappy);
  FREESURFACE(icons->pMale_Angry);
  FREESURFACE(icons->pFemale_Angry);

  spe_max = specialist_count();
  for (i = 0; i < spe_max; i++) {
    FREESURFACE(icons->specialists[i]);
  }

  FC_FREE(icons);
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

  current_theme = fc_calloc(1, sizeof(struct theme_icons));

  load_theme_surface(buf, fr_left, "theme.left_frame");
  load_theme_surface(buf, fr_right, "theme.right_frame");
  load_theme_surface(buf, fr_top, "theme.top_frame");
  load_theme_surface(buf, fr_bottom, "theme.bottom_frame");
  load_theme_surface(buf, button, "theme.button");
  load_theme_surface(buf, edit, "theme.edit");
  load_theme_surface(buf, cbox_sell_icon, "theme.sbox");
  load_theme_surface(buf, cbox_unsell_icon, "theme.ubox");
  load_theme_surface(buf, up_icon, "theme.UP_scroll");
  load_theme_surface(buf, down_icon, "theme.DOWN_scroll");
#if 0
  load_theme_surface(buf, left_icon, "theme.LEFT_scroll");
  load_theme_surface(buf, right_icon, "theme.RIGHT_scroll");
#endif
  load_theme_surface(buf, vertic, "theme.vertic_scrollbar");
  load_theme_surface(buf, horiz, "theme.horiz_scrollbar");

  /* ------------------- */
  load_theme_surface(buf, ok_pact_icon, "theme.pact_ok");
  load_theme_surface(buf, cancel_pact_icon, "theme.pact_cancel");
  /* ------------------- */
  load_theme_surface(buf, small_ok_icon, "theme.SMALL_OK_button");
  load_theme_surface(buf, small_cancel_icon, "theme.SMALL_FAIL_button");
  /* ------------------- */
  load_theme_surface(buf, ok_icon, "theme.OK_button");
  load_theme_surface(buf, cancel_icon, "theme.FAIL_button");
  load_theme_surface(buf, FORWARD_Icon, "theme.NEXT_button");
  load_theme_surface(buf, back_icon, "theme.BACK_button");
  load_theme_surface(buf, l_arrow_icon, "theme.LEFT_ARROW_button");
  load_theme_surface(buf, r_arrow_icon, "theme.RIGHT_ARROW_button");
  load_theme_surface(buf, map_icon, "theme.MAP_button");
  load_theme_surface(buf, find_city_icon, "theme.FIND_CITY_button");
  load_theme_surface(buf, new_turn_icon, "theme.NEW_TURN_button");
  load_theme_surface(buf, log_icon, "theme.LOG_button");
  load_theme_surface(buf, units_icon, "theme.UNITS_INFO_button");
  load_theme_surface(buf, options_icon, "theme.OPTIONS_button");
  load_theme_surface(buf, block, "theme.block");
  load_theme_surface(buf, info_icon, "theme.INFO_button");
  load_theme_surface(buf, army_icon, "theme.ARMY_button");
  load_theme_surface(buf, happy_icon, "theme.HAPPY_button");
  load_theme_surface(buf, support_icon, "theme.HOME_button");
  load_theme_surface(buf, buy_prod_icon, "theme.BUY_button");
  load_theme_surface(buf, prod_icon, "theme.PROD_button");
  load_theme_surface(buf, qprod_icon, "theme.WORK_LIST_button");
  load_theme_surface(buf, cma_icon, "theme.CMA_button");
  load_theme_surface(buf, lock_icon, "theme.LOCK_button");
  load_theme_surface(buf, unlock_icon, "theme.UNLOCK_button");
  load_theme_surface(buf, players_icon, "theme.PLAYERS_button");
  load_theme_surface(buf, units2_icon, "theme.UNITS_button");
  load_theme_surface(buf, save_icon, "theme.SAVE_button");
  load_theme_surface(buf, load_icon, "theme.LOAD_button");
  load_theme_surface(buf, delete_icon, "theme.DELETE_button");
  load_theme_surface(buf, borders_icon, "theme.BORDERS_button");
  /* ------------------------------ */
  load_theme_surface(buf, tech_tree_icon, "theme.tech_tree");
  /* ------------------------------ */

  load_order_theme_surface(buf, order_icon, "theme.order_empty");
  load_order_theme_surface(buf, OAutoAtt_Icon, "theme.order_auto_attack");
  load_order_theme_surface(buf, o_autoconnect_icon, "theme.order_auto_connect");
  load_order_theme_surface(buf, o_autoexp_icon, "theme.order_auto_explorer");
  load_order_theme_surface(buf, o_autosett_icon, "theme.order_auto_settler");
  load_order_theme_surface(buf, o_build_city_icon, "theme.order_build_city");
  load_order_theme_surface(buf, OCutDownForest_Icon, "theme.order_cutdown_forest");
  load_order_theme_surface(buf, OPlantForest_Icon, "theme.order_plant_forest");
  load_order_theme_surface(buf, o_mine_icon, "theme.order_build_mining");
  load_order_theme_surface(buf, o_irrigation_icon, "theme.order_irrigation");
  load_order_theme_surface(buf, o_cultivate_icon, "theme.order_cutdown_forest");
  load_order_theme_surface(buf, o_plant_icon, "theme.order_plant_forest");
  load_order_theme_surface(buf, o_done_icon, "theme.order_done");
  load_order_theme_surface(buf, o_disband_icon, "theme.order_disband");
  load_order_theme_surface(buf, o_fortify_icon, "theme.order_fortify");
  load_order_theme_surface(buf, o_goto_icon, "theme.order_goto");
  load_order_theme_surface(buf, o_goto_city_icon, "theme.order_goto_city");
  load_order_theme_surface(buf, o_homecity_icon, "theme.order_home");
  load_order_theme_surface(buf, o_nuke_icon, "theme.order_nuke");
  load_order_theme_surface(buf, o_paradrop_icon, "theme.order_paradrop");
  load_order_theme_surface(buf, o_patrol_icon, "theme.order_patrol");
  load_order_theme_surface(buf, o_pillage_icon, "theme.order_pillage");
  load_order_theme_surface(buf, o_railroad_icon, "theme.order_build_railroad");
  load_order_theme_surface(buf, o_road_icon, "theme.order_build_road");
  load_order_theme_surface(buf, o_sentry_icon, "theme.order_sentry");
  load_order_theme_surface(buf, o_unload_icon, "theme.order_unload");
  load_order_theme_surface(buf, o_wait_icon, "theme.order_wait");
  load_order_theme_surface(buf, o_fortress_icon, "theme.order_build_fortress");
  load_order_theme_surface(buf, o_fallout_icon, "theme.order_clean_fallout");
  load_order_theme_surface(buf, o_pollution_icon, "theme.order_clean_pollution");
  load_order_theme_surface(buf, o_airbase_icon, "theme.order_build_airbase");
  load_order_theme_surface(buf, o_transform_icon, "theme.order_transform");
  load_order_theme_surface(buf, OAddCity_Icon, "theme.order_add_to_city");
  load_order_theme_surface(buf, o_wonder_icon, "theme.order_carravan_wonder");
  load_order_theme_surface(buf, o_trade_icon, "theme.order_carravan_trade_route");
  load_order_theme_surface(buf, o_spy_icon, "theme.order_spying");
  load_order_theme_surface(buf, o_wakeup_icon, "theme.order_wakeup");
  load_order_theme_surface(buf, o_return_icon, "theme.order_return");
  load_order_theme_surface(buf, OAirLift_Icon, "theme.order_airlift");
  load_order_theme_surface(buf, o_load_icon, "theme.order_load");
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
  SDL_Surface *intro = theme_get_background(theme, BACKGROUND_MAINPAGE);

  if (intro->w != main_window_width()) {
    SDL_Surface *tmp = ResizeSurface(intro, main_window_width(), main_window_height(), 1);

    FREESURFACE(intro);
    intro = tmp;
  }

  /* draw intro gfx center in screen */
  alphablit(intro, NULL, main_data.map, NULL, 255);

  FREESURFACE(intro);
}
