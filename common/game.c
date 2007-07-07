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

#include "capstr.h"
#include "city.h"
#include "cm.h"
#include "connection.h"
#include "fcintl.h"
#include "government.h"
#include "idex.h"
#include "log.h"
#include "map.h"
#include "mem.h"
#include "nation.h"
#include "packets.h"
#include "player.h"
#include "shared.h"
#include "spaceship.h"
#include "support.h"
#include "tech.h"
#include "unit.h"

#include "game.h"

void dealloc_id(int id);
struct civ_game game;

/*
struct player_score {
  int happy;
  int content;
  int unhappy;
  int angry;
  int taxmen;
  int scientists;
  int elvis;
  int wonders;
  int techs;
  int landarea;
  int settledarea;
  int population;
  int cities;
  int units;
  int pollution;
  int literacy;
  int bnp;
  int mfg;
  int spaceship;
};
*/

/**************************************************************************
Count the # of thousand citizen in a civilisation.
**************************************************************************/
int civ_population(struct player *pplayer)
{
  int ppl=0;
  city_list_iterate(pplayer->cities, pcity)
    ppl+=city_population(pcity);
  city_list_iterate_end;
  return ppl;
}


/**************************************************************************
...
**************************************************************************/
struct city *game_find_city_by_name(const char *name)
{
  players_iterate(pplayer) {
    struct city *pcity = city_list_find_name(&pplayer->cities, name);

    if (pcity) {
      return pcity;
    }
  } players_iterate_end;

  return NULL;
}


/**************************************************************************
  Often used function to get a city pointer from a city ID.
  City may be any city in the game.  This now always uses fast idex
  method, instead of looking through all cities of all players.
**************************************************************************/
struct city *find_city_by_id(int id)
{
  return idex_lookup_city(id);
}


/**************************************************************************
  Find unit out of all units in game: now uses fast idex method,
  instead of looking through all units of all players.
**************************************************************************/
struct unit *find_unit_by_id(int id)
{
  return idex_lookup_unit(id);
}

/**************************************************************************
  In the server call wipe_unit(), and never this function directly.
**************************************************************************/
void game_remove_unit(struct unit *punit)
{
  struct city *pcity;

  freelog(LOG_DEBUG, "game_remove_unit %d", punit->id);
  freelog(LOG_DEBUG, "removing unit %d, %s %s (%d %d) hcity %d",
	  punit->id, get_nation_name(unit_owner(punit)->nation),
	  unit_name(punit->type), punit->tile->x, punit->tile->y,
	  punit->homecity);

  pcity = player_find_city_by_id(unit_owner(punit), punit->homecity);
  if (pcity) {
    unit_list_unlink(&pcity->units_supported, punit);
  }

  if (pcity) {
    freelog(LOG_DEBUG, "home city %s, %s, (%d %d)", pcity->name,
	    get_nation_name(city_owner(pcity)->nation), pcity->tile->x,
	    pcity->tile->y);
  }

  unit_list_unlink(&punit->tile->units, punit);
  unit_list_unlink(&unit_owner(punit)->units, punit);

  idex_unregister_unit(punit);

  if (is_server) {
    dealloc_id(punit->id);
  }
  destroy_unit_virtual(punit);
}

/**************************************************************************
...
**************************************************************************/
void game_remove_city(struct city *pcity)
{
  freelog(LOG_DEBUG, "game_remove_city %d", pcity->id);
  freelog(LOG_DEBUG, "removing city %s, %s, (%d %d)", pcity->name,
	   get_nation_name(city_owner(pcity)->nation), pcity->tile->x,
	  pcity->tile->y);

  city_map_checked_iterate(pcity->tile, x, y, map_tile) {
    set_worker_city(pcity, x, y, C_TILE_EMPTY);
  } city_map_checked_iterate_end;
  city_list_unlink(&city_owner(pcity)->cities, pcity);
  map_set_city(pcity->tile, NULL);
  idex_unregister_city(pcity);
  remove_city_virtual(pcity);
}

/***************************************************************
...
***************************************************************/
void game_init(void)
{
  int i;
  game.is_new_game   = TRUE;
  game.globalwarming = 0;
  game.warminglevel  = 8;
  game.nuclearwinter = 0;
  game.coolinglevel  = 8;
  game.gold          = GAME_DEFAULT_GOLD;
  game.tech          = GAME_DEFAULT_TECHLEVEL;
  game.skill_level   = GAME_DEFAULT_SKILL_LEVEL;
  game.timeout       = GAME_DEFAULT_TIMEOUT;
  game.timeoutint    = GAME_DEFAULT_TIMEOUTINT;
  game.timeoutintinc = GAME_DEFAULT_TIMEOUTINTINC;
  game.timeoutinc    = GAME_DEFAULT_TIMEOUTINC;
  game.timeoutincmult= GAME_DEFAULT_TIMEOUTINCMULT;
  game.timeoutcounter= 1;
  game.timeoutaddenemymove = GAME_DEFAULT_TIMEOUTADDEMOVE; 
  game.tcptimeout    = GAME_DEFAULT_TCPTIMEOUT;
  game.netwait       = GAME_DEFAULT_NETWAIT;
  game.last_ping     = 0;
  game.pingtimeout   = GAME_DEFAULT_PINGTIMEOUT;
  game.pingtime      = GAME_DEFAULT_PINGTIME;
  game.end_year      = GAME_DEFAULT_END_YEAR;
  game.year          = GAME_START_YEAR;
  game.turn          = 0;
  game.min_players   = GAME_DEFAULT_MIN_PLAYERS;
  game.max_players  = GAME_DEFAULT_MAX_PLAYERS;
  game.aifill      = GAME_DEFAULT_AIFILL;
  game.nplayers=0;
  game.researchcost = GAME_DEFAULT_RESEARCHCOST;
  game.diplcost    = GAME_DEFAULT_DIPLCOST;
  game.diplchance  = GAME_DEFAULT_DIPLCHANCE;
  game.freecost    = GAME_DEFAULT_FREECOST;
  game.conquercost = GAME_DEFAULT_CONQUERCOST;
  sz_strlcpy(game.start_units, GAME_DEFAULT_START_UNITS);
  game.dispersion  = GAME_DEFAULT_DISPERSION;
  game.cityfactor  = GAME_DEFAULT_CITYFACTOR;
  game.citymindist = GAME_DEFAULT_CITYMINDIST;
  game.civilwarsize= GAME_DEFAULT_CIVILWARSIZE;
  game.contactturns= GAME_DEFAULT_CONTACTTURNS;
  game.rapturedelay= GAME_DEFAULT_RAPTUREDELAY;
  game.savepalace  = GAME_DEFAULT_SAVEPALACE;
  game.natural_city_names = GAME_DEFAULT_NATURALCITYNAMES;
  game.unhappysize = GAME_DEFAULT_UNHAPPYSIZE;
  game.angrycitizen= GAME_DEFAULT_ANGRYCITIZEN;
  game.foodbox     = GAME_DEFAULT_FOODBOX;
  game.aqueductloss= GAME_DEFAULT_AQUEDUCTLOSS;
  game.killcitizen = GAME_DEFAULT_KILLCITIZEN;
  game.scorelog    = GAME_DEFAULT_SCORELOG;
  game.techpenalty = GAME_DEFAULT_TECHPENALTY;
  game.civstyle    = GAME_DEFAULT_CIVSTYLE;
  game.razechance  = GAME_DEFAULT_RAZECHANCE;
  game.spacerace   = GAME_DEFAULT_SPACERACE;
  game.turnblock   = GAME_DEFAULT_TURNBLOCK;
  game.fogofwar    = GAME_DEFAULT_FOGOFWAR;
  game.fogofwar_old= game.fogofwar;
  game.borders     = GAME_DEFAULT_BORDERS;
  game.happyborders = GAME_DEFAULT_HAPPYBORDERS;
  game.slow_invasions = GAME_DEFAULT_SLOW_INVASIONS;
  game.auto_ai_toggle = GAME_DEFAULT_AUTO_AI_TOGGLE;
  game.notradesize    = GAME_DEFAULT_NOTRADESIZE;
  game.fulltradesize  = GAME_DEFAULT_FULLTRADESIZE;
  game.barbarianrate  = GAME_DEFAULT_BARBARIANRATE;
  game.onsetbarbarian = GAME_DEFAULT_ONSETBARBARIAN;
  game.nbarbarians = 0;
  game.occupychance= GAME_DEFAULT_OCCUPYCHANCE;
  game.revolution_length = GAME_DEFAULT_REVOLUTION_LENGTH;

  game.heating     = 0;
  game.cooling     = 0;
  sz_strlcpy(game.save_name, GAME_DEFAULT_SAVE_NAME);
  game.save_nturns=10;
#ifdef HAVE_LIBZ
  game.save_compress_level = GAME_DEFAULT_COMPRESS_LEVEL;
#else
  game.save_compress_level = GAME_NO_COMPRESS_LEVEL;
#endif
  game.seed = GAME_DEFAULT_SEED;
  game.watchtower_vision=GAME_DEFAULT_WATCHTOWER_VISION;
  game.watchtower_extra_vision=GAME_DEFAULT_WATCHTOWER_EXTRA_VISION,
  game.allowed_city_names = GAME_DEFAULT_ALLOWED_CITY_NAMES;

  sz_strlcpy(game.rulesetdir, GAME_DEFAULT_RULESETDIR);

  game.num_unit_types = 0;
  game.num_impr_types = 0;
  game.num_tech_types = 0;
 
  game.nation_count = 0;
  game.government_count = 0;
  game.default_government = G_MAGIC;        /* flag */
  game.government_when_anarchy = G_MAGIC;   /* flag */
  game.ai_goal_government = G_MAGIC;        /* flag */

  game.default_building = B_LAST;
  game.palace_building = B_LAST;
  game.land_defend_building = B_LAST;

  sz_strlcpy(game.demography, GAME_DEFAULT_DEMOGRAPHY);
  sz_strlcpy(game.allow_take, GAME_DEFAULT_ALLOW_TAKE);

  game.save_options.save_random = TRUE;
  game.save_options.save_players = TRUE;
  game.save_options.save_known = TRUE;
  game.save_options.save_starts = TRUE;
  game.save_options.save_private_map = TRUE;

  init_our_capability();    
  map_init();
  idex_init();
  cm_init();
  
  for(i=0; i<MAX_NUM_PLAYERS+MAX_NUM_BARBARIANS; i++)
    player_init(&game.players[i]);
  for (i=0; i<A_LAST; i++)      /* game.num_tech_types = 0 here */
    game.global_advances[i]=0;
  for (i=0; i<B_LAST; i++)      /* game.num_impr_types = 0 here */
    game.global_wonders[i]=0;
  game.player_idx=0;
  game.player_ptr=&game.players[0];
  terrain_control.river_help_text[0] = '\0';
}

/***************************************************************
  Remove all initialized players. This is all player slots, 
  since we initialize them all on game initialization.
***************************************************************/
static void game_remove_all_players(void)
{
  int i;

  for (i = 0; i < MAX_NUM_PLAYERS + MAX_NUM_BARBARIANS; i++) {
    game_remove_player(&game.players[i]);
  }

  game.nplayers=0;
  game.nbarbarians=0;
}

/***************************************************************
  Frees all memory of the game.
***************************************************************/
void game_free(void)
{
  game_remove_all_players();
  map_free();
  idex_free();
  ruleset_data_free();
  cm_free();
}

/***************************************************************
 Frees all memory which in objects which are read from a ruleset.
***************************************************************/
void ruleset_data_free()
{
  techs_free();
  governments_free();
  nations_free();
  unit_types_free();
  improvements_free();
  city_styles_free();
  tile_types_free();
}

/***************************************************************
...
***************************************************************/
void initialize_globals(void)
{
  players_iterate(plr) {
    city_list_iterate(plr->cities, pcity) {
      built_impr_iterate(pcity, i) {
	if (is_wonder(i))
	  game.global_wonders[i] = pcity->id;
      } built_impr_iterate_end;
    } city_list_iterate_end;
  } players_iterate_end;
}

/***************************************************************
  Returns the next year in the game.
***************************************************************/
int game_next_year(int year)
{
  int spaceshipparts, space_parts[3] = {0, 0, 0};

  if (year == 1) /* hacked it to get rid of year 0 */
    year = 0;

    /* !McFred: 
       - want year += 1 for spaceship.
    */

  /* test game with 7 normal AI's, gen 4 map, foodbox 10, foodbase 0: 
   * Gunpowder about 0 AD
   * Railroad  about 500 AD
   * Electricity about 1000 AD
   * Refining about 1500 AD (212 active units)
   * about 1750 AD
   * about 1900 AD
   */

  /* Count how many of the different spaceship parts we can build.  Note this
   * operates even if Enable_Space is not active. */
  if (game.spacerace) {
    impr_type_iterate(impr) {
      Tech_Type_id t = improvement_types[impr].tech_req;

      if (!improvement_exists(impr)) {
	continue;
      }
      if (building_has_effect(impr, EFT_SS_STRUCTURAL)
	  && tech_exists(t) && game.global_advances[t] != 0) {
	space_parts[0] = 1;
      }
      if (building_has_effect(impr, EFT_SS_COMPONENT)
	  && tech_exists(t) && game.global_advances[t] != 0) {
	space_parts[1] = 1;
      }
      if (building_has_effect(impr, EFT_SS_MODULE)
	  && tech_exists(t) && game.global_advances[t] != 0) {
	space_parts[2] = 1;
      }
    } impr_type_iterate_end;
  }
  spaceshipparts = space_parts[0] + space_parts[1] + space_parts[2];

  if( year >= 1900 || ( spaceshipparts>=3 && year>0 ) )
    year += 1;
  else if( year >= 1750 || spaceshipparts>=2 )
    year += 2;
  else if( year >= 1500 || spaceshipparts>=1 )
    year += 5;
  else if( year >= 1000 )
    year += 10;
  else if( year >= 0 )
    year += 20;
  else if( year >= -1000 ) /* used this line for tuning (was -1250) */
    year += 25;
  else
    year += 50; 

  if (year == 0) 
    year = 1;

  return year;
}

/***************************************************************
  Advance the game year.
***************************************************************/
void game_advance_year(void)
{
  game.year = game_next_year(game.year);
  game.turn++;
}

/***************************************************************
...
***************************************************************/
void game_remove_player(struct player *pplayer)
{
  if (pplayer->attribute_block.data) {
    free(pplayer->attribute_block.data);
    pplayer->attribute_block.data = NULL;
  }
  pplayer->attribute_block.length = 0;

  if (pplayer->attribute_block_buffer.data) {
    free(pplayer->attribute_block_buffer.data);
    pplayer->attribute_block_buffer.data = NULL;
  }
  pplayer->attribute_block_buffer.length = 0;

  if (pplayer->island_improv) {
    free(pplayer->island_improv);
    pplayer->island_improv = NULL;
  }

  conn_list_unlink_all(&pplayer->connections);

  unit_list_iterate(pplayer->units, punit) 
    game_remove_unit(punit);
  unit_list_iterate_end;
  assert(unit_list_size(&pplayer->units) == 0);
  unit_list_unlink_all(&pplayer->units);

  city_list_iterate(pplayer->cities, pcity) 
    game_remove_city(pcity);
  city_list_iterate_end;
  assert(city_list_size(&pplayer->cities) == 0);
  city_list_unlink_all(&pplayer->cities);

  if (is_barbarian(pplayer)) game.nbarbarians--;
}

/***************************************************************
...
***************************************************************/
void game_renumber_players(int plrno)
{
  int i;

  for (i = plrno; i < game.nplayers - 1; i++) {
    game.players[i]=game.players[i+1];
    game.players[i].player_no=i;
    conn_list_iterate(game.players[i].connections, pconn)
      pconn->player = &game.players[i];
    conn_list_iterate_end;
  }

  if(game.player_idx>plrno) {
    game.player_idx--;
    game.player_ptr=&game.players[game.player_idx];
  }

  game.nplayers--;

  /* a bit of cleanup to keep connections sane */
  conn_list_init(&game.players[game.nplayers].connections);
  game.players[game.nplayers].is_connected = FALSE;
  game.players[game.nplayers].was_created = FALSE;
  game.players[game.nplayers].ai.control = FALSE;
  sz_strlcpy(game.players[game.nplayers].name, ANON_PLAYER_NAME);
  sz_strlcpy(game.players[game.nplayers].username, ANON_USER_NAME);
}

/**************************************************************************
get_player() - Return player struct pointer corresponding to player_id.
               Eg: player_id = punit->owner, or pcity->owner
**************************************************************************/
struct player *get_player(int player_id)
{
    return &game.players[player_id];
}

bool is_valid_player_id(int player_id)
{
  return player_id >= 0 && player_id < game.nplayers;
}

/**************************************************************************
This function is used by is_wonder_useful to estimate if it is worthwhile
to build the great library.
**************************************************************************/
int get_num_human_and_ai_players(void)
{
  return game.nplayers-game.nbarbarians;
}

/***************************************************************
  For various data, copy eg .name to .name_orig and put
  translated version in .name
  (These could be in the separate modules, but since they are
  all almost the same, and all needed together, it seems a bit
  easier to just do them all here.)
***************************************************************/
void translate_data_names(void)
{
  int i;

  tech_type_iterate(tech_id) {
    struct advance *tthis = &advances[tech_id];

    tthis->name = Q_(tthis->name_orig);
  } tech_type_iterate_end;

  unit_type_iterate(i) {
    struct unit_type *tthis = &unit_types[i];

    tthis->name = Q_(tthis->name_orig);
  } unit_type_iterate_end;

  impr_type_iterate(i) {
    struct impr_type *tthis = &improvement_types[i];

    tthis->name = Q_(tthis->name_orig);
  } impr_type_iterate_end;

  terrain_type_iterate(i) {
    struct tile_type *tthis = get_tile_type(i);

    tthis->terrain_name = ((strcmp(tthis->terrain_name_orig, "") != 0)
			   ? Q_(tthis->terrain_name_orig) : "");

    tthis->special_1_name = ((strcmp(tthis->special_1_name_orig, "") != 0)
			     ? Q_(tthis->special_1_name_orig) : "");
    tthis->special_2_name = ((strcmp(tthis->special_2_name_orig, "") != 0)
			     ? Q_(tthis->special_2_name_orig) : "");
  } terrain_type_iterate_end;

  government_iterate(tthis) {
    int j;

    tthis->name = Q_(tthis->name_orig);
    for(j=0; j<tthis->num_ruler_titles; j++) {
      struct ruler_title *that = &tthis->ruler_titles[j];

      that->male_title = Q_(that->male_title_orig);
      that->female_title = Q_(that->female_title_orig);
    }
  } government_iterate_end;
  for (i=0; i<game.nation_count; i++) {
    struct nation_type *tthis = get_nation_by_idx(i);

    tthis->name = Q_(tthis->name_orig);
    tthis->name_plural = Q_(tthis->name_plural_orig);
  }
  for (i=0; i<game.styles_count; i++) {
    struct citystyle *tthis = &city_styles[i];

    tthis->name = Q_(tthis->name_orig);
  }

}

/****************************************************************************
  Return a prettily formatted string containing the population text.  The
  population is passed in as the number of citizens, in thousands.
****************************************************************************/
const char *population_to_text(int thousand_citizen)
{
  /* big_int_to_text can't handle negative values, and in any case we'd
   * better not have a negative population. */
  assert(thousand_citizen >= 0);
  return big_int_to_text(thousand_citizen, 3);
}

