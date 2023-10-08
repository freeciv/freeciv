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

static SDL_Surface *neutral_tech_icon;
static SDL_Surface *none_tech_icon;
static SDL_Surface *future_tech_icon;

#define load_gui_surface(_spr_, _struct_, _surf_, _tag_)	  \
do {								  \
  _spr_ = theme_lookup_sprite_tag_alt(active_theme, LOG_FATAL,     \
                                      _tag_, "", "", "");          \
  fc_assert_action(_spr_ != NULL, break);                          \
  _struct_->_surf_ = GET_SURF_REAL(_spr_);                           \
} while (FALSE)

#define load_theme_surface(spr, surf, tag)		\
	load_gui_surface(spr, current_theme, surf, tag)

#define load_city_icon_surface(spr, surf, tag)        \
        load_gui_surface(spr, icons, surf, tag)

#define load_order_theme_surface(spr, surf, tag)	\
        load_gui_surface(spr, current_theme, surf, tag);

/***************************************************************************//**
  Reload small citizens "style" icons.
*******************************************************************************/
static void reload_small_citizens_icons(int style)
{
  int i;
  int spe_max;

  /* free info icons */
  FREESURFACE(icons->male_content);
  FREESURFACE(icons->female_content);
  FREESURFACE(icons->male_happy);
  FREESURFACE(icons->female_happy);
  FREESURFACE(icons->male_unhappy);
  FREESURFACE(icons->female_unhappy);
  FREESURFACE(icons->male_angry);
  FREESURFACE(icons->female_angry);

  spe_max = specialist_count();
  for (i = 0; i < spe_max; i++) {
    FREESURFACE(icons->specialists[i]);
  }

  /* allocate icons */
  icons->male_happy = adj_surf(get_citizen_surface(CITIZEN_HAPPY, 0));
  icons->female_happy = adj_surf(get_citizen_surface(CITIZEN_HAPPY, 1));
  icons->male_content = adj_surf(get_citizen_surface(CITIZEN_CONTENT, 0));
  icons->female_content = adj_surf(get_citizen_surface(CITIZEN_CONTENT, 1));
  icons->male_unhappy = adj_surf(get_citizen_surface(CITIZEN_UNHAPPY, 0));
  icons->female_unhappy = adj_surf(get_citizen_surface(CITIZEN_UNHAPPY, 1));
  icons->male_angry = adj_surf(get_citizen_surface(CITIZEN_ANGRY, 0));
  icons->female_angry = adj_surf(get_citizen_surface(CITIZEN_ANGRY, 1));

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
void tilespec_setup_city_gfx(void)
{
  struct sprite *spr =
    theme_lookup_sprite_tag_alt(active_theme, LOG_FATAL,
                                "theme.city", "", "", "");

  city_surf = (spr ? adj_surf(GET_SURF_REAL(spr)) : NULL);

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
  struct sprite *spr = NULL;

  icons = (struct city_icon *)fc_calloc(1, sizeof(struct city_icon));

  load_city_icon_surface(spr, big_food_corr, "city.food_waste");
  load_city_icon_surface(spr, big_shield_corr, "city.shield_waste");
  load_city_icon_surface(spr, big_trade_corr, "city.trade_waste");
  load_city_icon_surface(spr, big_food, "city.food");

  icons->big_food_surplus = crop_rect_from_surface(icons->big_food, NULL);
  SDL_SetSurfaceAlphaMod(icons->big_food_surplus, 128);

  load_city_icon_surface(spr, big_shield, "city.shield");

  icons->big_shield_surplus = crop_rect_from_surface(icons->big_shield, NULL);
  SDL_SetSurfaceAlphaMod(icons->big_shield_surplus, 128);

  load_city_icon_surface(spr, big_trade, "city.trade");
  load_city_icon_surface(spr, big_luxury, "city.lux");
  load_city_icon_surface(spr, big_coin, "city.coin");
  load_city_icon_surface(spr, big_colb, "city.colb");
  load_city_icon_surface(spr, pBIG_Face, "city.red_face");
  load_city_icon_surface(spr, big_coin_corr, "city.dark_coin");
  load_city_icon_surface(spr, big_coin_upkeep, "city.upkeep_coin");

  /* Small icon */
  load_city_icon_surface(spr, food, "city.small_food");
  load_city_icon_surface(spr, shield, "city.small_shield");
  load_city_icon_surface(spr, trade, "city.small_trade");
  load_city_icon_surface(spr, face, "city.small_red_face");
  load_city_icon_surface(spr, pLuxury, "city.small_lux");
  load_city_icon_surface(spr, coint, "city.small_coin");
  load_city_icon_surface(spr, pColb, "city.small_colb");

  load_city_icon_surface(spr, pollution, "city.pollution");
  /* ================================================================= */

  load_city_icon_surface(spr, police, "city.police");
  /* ================================================================= */
  icons->worklist = create_surf(9,9, SDL_SWSURFACE);
  SDL_FillRect(icons->worklist, NULL,
               SDL_MapRGB(icons->worklist->format, 255, 255,255));

  create_frame(icons->worklist,
               0, 0, icons->worklist->w - 1, icons->worklist->h - 1,
               get_theme_color(COLOR_THEME_CITYREP_FRAME));
  create_line(icons->worklist,
              3, 2, 5, 2,
              get_theme_color(COLOR_THEME_CITYREP_FRAME));
  create_line(icons->worklist,
              3, 4, 7, 4,
              get_theme_color(COLOR_THEME_CITYREP_FRAME));
  create_line(icons->worklist,
              3, 6, 6, 6,
              get_theme_color(COLOR_THEME_CITYREP_FRAME));

  /* ================================================================= */

  /* Force reload citizens icons */
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
  FREESURFACE(icons->male_content);
  FREESURFACE(icons->female_content);
  FREESURFACE(icons->male_happy);
  FREESURFACE(icons->female_happy);
  FREESURFACE(icons->male_unhappy);
  FREESURFACE(icons->female_unhappy);
  FREESURFACE(icons->male_angry);
  FREESURFACE(icons->female_angry);

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
  /* ------------------------------ */
  load_theme_surface(buf, tech_tree_icon, "theme.tech_tree");
  /* ------------------------------ */

  load_order_theme_surface(buf, order_icon, "theme.order_empty");
  load_order_theme_surface(buf, o_autoconnect_icon, "theme.order_auto_connect");
  load_order_theme_surface(buf, o_autoexp_icon, "theme.order_auto_explorer");
  load_order_theme_surface(buf, o_autowork_icon, "theme.order_auto_worker");
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
  load_order_theme_surface(buf, o_clean_icon, "theme.order_clean");
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
  utf8_str *pstr = create_utf8_from_char_fonto(Q_("?tech:None"), FONTO_DEFAULT);

  pstr->style |= (TTF_STYLE_BOLD | SF_CENTER);

  /* Create icons */
  surf = create_surf(adj_size(50), adj_size(50), SDL_SWSURFACE);
  SDL_FillRect(surf, NULL, map_rgba(surf->format, bg_color));
  create_frame(surf,
               0 , 0, surf->w - 1, surf->h - 1,
               get_theme_color(COLOR_THEME_SCIENCEDLG_FRAME));

  neutral_tech_icon = copy_surface(surf);
  none_tech_icon = copy_surface(surf);
  future_tech_icon = surf;

  /* None */
  surf = create_text_surf_from_utf8(pstr);
  blit_entire_src(surf, none_tech_icon ,
                  (adj_size(50) - surf->w) / 2 , (adj_size(50) - surf->h) / 2);

  FREESURFACE(surf);

  /* TRANS: Future Technology */
  copy_chars_to_utf8_str(pstr, _("FT"));
  surf = create_text_surf_from_utf8(pstr);
  blit_entire_src(surf, future_tech_icon,
                  (adj_size(50) - surf->w) / 2 , (adj_size(50) - surf->h) / 2);

  FREESURFACE(surf);

  FREEUTF8STR(pstr);
}

/***************************************************************************//**
  Free resources associated with aux tech icons.
*******************************************************************************/
void free_auxiliary_tech_icons(void)
{
  FREESURFACE(neutral_tech_icon);
  FREESURFACE(none_tech_icon);
  FREESURFACE(future_tech_icon);
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
    return adj_surf(none_tech_icon);
  case A_FUTURE:
    return adj_surf(future_tech_icon);
  default:
    if (get_tech_sprite(tileset, tech)) {
      return adj_surf(GET_SURF(get_tech_sprite(tileset, tech)));
    } else {
      return adj_surf(neutral_tech_icon);
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
  SDL_Surface *intro = theme_get_background(active_theme, BACKGROUND_MAINPAGE);

  if (intro->w != main_window_width()) {
    SDL_Surface *tmp = resize_surface(intro, main_window_width(),
                                      main_window_height(), 1);

    FREESURFACE(intro);
    intro = tmp;
  }

  /* draw intro gfx center in screen */
  alphablit(intro, NULL, main_data.map, NULL, 255);

  FREESURFACE(intro);
}
