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
                          gui_tilespec.h  -  description
                             -------------------
    begin                : Dec. 2 2002
    copyright            : (C) 2002 by Rafał Bursig
    email                : Rafal Bursig <bursig@poczta.fm>
***********************************************************************/

#ifndef FC__GUI_TILESPEC_H
#define FC__GUI_TILESPEC_H

/* SDL2 */
#ifdef SDL2_PLAIN_INCLUDE
#include <SDL.h>
#else  /* SDL2_PLAIN_INCLUDE */
#include <SDL2/SDL.h>
#endif /* SDL2_PLAIN_INCLUDE */

/* client */
#include "tilespec.h"

/* gui-sdl2 */
#include "graphics.h"
#include "sprite.h"

struct theme_icons {

  /* Frame */
  SDL_Surface *fr_left;
  SDL_Surface *fr_right;
  SDL_Surface *fr_top;
  SDL_Surface *fr_bottom;

  /* Button */
  SDL_Surface *button;

  /* Edit */
  SDL_Surface *edit;

  /* Checkbox */
  SDL_Surface *cbox_sell_icon;
  SDL_Surface *cbox_unsell_icon;

  /* Scrollbar */
  SDL_Surface *up_icon;
  SDL_Surface *down_icon;
  SDL_Surface *left_icon;
  SDL_Surface *right_icon;
  SDL_Surface *vertic;
  SDL_Surface *horiz;

  /* Game */
  SDL_Surface *ok_icon;
  SDL_Surface *cancel_icon;
  SDL_Surface *small_ok_icon;
  SDL_Surface *small_cancel_icon;
  SDL_Surface *FORWARD_Icon;
  SDL_Surface *back_icon;
  SDL_Surface *info_icon;
  SDL_Surface *r_arrow_icon;
  SDL_Surface *l_arrow_icon;
  SDL_Surface *lock_icon;
  SDL_Surface *unlock_icon;

  SDL_Surface *options_icon;
  SDL_Surface *block;
  SDL_Surface *units_icon;
  SDL_Surface *map_icon;
  SDL_Surface *log_icon;
  SDL_Surface *players_icon;
  SDL_Surface *units2_icon;
  SDL_Surface *find_city_icon;
  SDL_Surface *new_turn_icon;
  SDL_Surface *save_icon;
  SDL_Surface *load_icon;
  SDL_Surface *delete_icon;

  /* Help icons */
  SDL_Surface *tech_tree_icon;

  /* City icons */
  SDL_Surface *army_icon;
  SDL_Surface *support_icon;
  SDL_Surface *happy_icon;
  SDL_Surface *cma_icon;
  SDL_Surface *prod_icon;
  SDL_Surface *qprod_icon;
  SDL_Surface *buy_prod_icon;

  /* Diplomacy */
  SDL_Surface *ok_pact_icon;
  SDL_Surface *cancel_pact_icon;

  /* Orders icons */
  SDL_Surface *order_icon;
  SDL_Surface *o_disband_icon;
  SDL_Surface *o_wait_icon;
  SDL_Surface *o_done_icon;
  SDL_Surface *o_autoexp_icon;
  SDL_Surface *o_autowork_icon;
  SDL_Surface *o_autoconnect_icon;
  SDL_Surface *o_unload_icon;
  SDL_Surface *o_build_city_icon;
  SDL_Surface *o_goto_city_icon;
  SDL_Surface *o_goto_icon;
  SDL_Surface *o_homecity_icon;
  SDL_Surface *o_patrol_icon;
  SDL_Surface *o_mine_icon;
  SDL_Surface *OPlantForest_Icon;
  SDL_Surface *OCutDownForest_Icon;
  SDL_Surface *o_fortify_icon;
  SDL_Surface *o_sentry_icon;
  SDL_Surface *o_irrigation_icon;
  SDL_Surface *o_cultivate_icon;
  SDL_Surface *o_plant_icon;
  SDL_Surface *o_road_icon;
  SDL_Surface *o_railroad_icon;
  SDL_Surface *o_pillage_icon;
  SDL_Surface *o_paradrop_icon;
  SDL_Surface *o_nuke_icon;
  SDL_Surface *o_fortress_icon;
  SDL_Surface *o_clean_icon;
  SDL_Surface *o_airbase_icon;
  SDL_Surface *o_transform_icon;
  SDL_Surface *OAddCity_Icon;
  SDL_Surface *o_wonder_icon;
  SDL_Surface *o_trade_icon;
  SDL_Surface *o_spy_icon;
  SDL_Surface *o_wakeup_icon;
  SDL_Surface *o_return_icon;
  SDL_Surface *OAirLift_Icon;
  SDL_Surface *o_load_icon;
};

struct city_icon {
  int style;

  SDL_Surface *big_food_corr;
  SDL_Surface *big_shield_corr;
  SDL_Surface *big_trade_corr;
  SDL_Surface *big_food;
  SDL_Surface *big_shield;
  SDL_Surface *big_trade;
  SDL_Surface *big_luxury;
  SDL_Surface *big_coin;
  SDL_Surface *big_colb;
  SDL_Surface *pBIG_Face;
  SDL_Surface *big_coin_corr;
  SDL_Surface *big_coin_upkeep;
  SDL_Surface *big_shield_surplus;
  SDL_Surface *big_food_surplus;

  SDL_Surface *food;
  SDL_Surface *shield;
  SDL_Surface *trade;
  SDL_Surface *pLuxury;
  SDL_Surface *coint;
  SDL_Surface *pColb;
  SDL_Surface *face;
  /* SDL_Surface *pDark_Face; */

  SDL_Surface *pollution;
  SDL_Surface *police;
  SDL_Surface *worklist;

  /* Small Citizens */
  SDL_Surface *male_happy;
  SDL_Surface *female_happy;
  SDL_Surface *male_content;
  SDL_Surface *female_content;
  SDL_Surface *male_unhappy;
  SDL_Surface *female_unhappy;
  SDL_Surface *male_angry;
  SDL_Surface *female_angry;

  SDL_Surface *specialists[SP_MAX];
};

extern struct theme_icons *current_theme;
extern struct city_icon *icons;

void tilespec_setup_theme(void);
void tilespec_free_theme(void);

void tilespec_setup_city_gfx(void);
void tilespec_free_city_gfx(void);

void tilespec_setup_city_icons(void);
void tilespec_free_city_icons(void);

void reload_citizens_icons(int style);
SDL_Surface *get_city_gfx(void);

void draw_intro_gfx(void);

void setup_auxiliary_tech_icons(void);
void free_auxiliary_tech_icons(void);
SDL_Surface *get_tech_icon(Tech_type_id tech);
SDL_Color *get_tech_color(Tech_type_id tech_id);

/**********************************************************************//**
  Return a surface for the given citizen. The citizen's type is given,
  as well as their index (in the range [0..city_size_get(pcity))).
**************************************************************************/
static inline SDL_Surface *get_citizen_surface(enum citizen_category type,
                                               int citizen_index)
{
  return GET_SURF(get_citizen_sprite(tileset, type, 0, NULL));
}

/**********************************************************************//**
  Return a surface for the flag of the given nation.
**************************************************************************/
static inline SDL_Surface *get_nation_flag_surface(const struct nation_type *pnation)
{
  return GET_SURF(get_nation_flag_sprite(tileset, pnation));
}

/**********************************************************************//**
  Return a surface for the given government icon.
**************************************************************************/
static inline SDL_Surface *get_government_surface(const struct government *gov)
{
  return GET_SURF(get_government_sprite(tileset, gov));
}

/**********************************************************************//**
  Return a sample surface of a city with the given city style
**************************************************************************/
static inline SDL_Surface *get_sample_city_surface(int city_style)
{
  return GET_SURF(get_sample_city_sprite(tileset, city_style));
}

/**********************************************************************//**
  Return a surface for the given building icon.
**************************************************************************/
static inline SDL_Surface *get_building_surface(const struct impr_type *pimprove)
{
  return GET_SURF(get_building_sprite(tileset, pimprove));
}

/**********************************************************************//**
  Return a surface for the given unit type.
**************************************************************************/
static inline SDL_Surface *get_unittype_surface(const struct unit_type *punittype,
                                                enum direction8 facing)
{
  return GET_SURF(get_unittype_sprite(tileset, punittype, ACTIVITY_LAST,
                                      facing));
}

/**********************************************************************//**
  Return a surface for the tax icon of the given output type.
**************************************************************************/
static inline SDL_Surface *get_tax_surface(Output_type_id otype)
{
  return adj_surf(GET_SURF(get_tax_sprite(tileset, otype)));
}

#endif /* FC__GUI_TILESPEC_H */
