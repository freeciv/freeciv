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
#include <fc_config.h>
#endif

/* utility */
#include "fcintl.h"
#include "ioz.h"
#include "log.h"
#include "mem.h"
#include "shared.h"
#include "support.h"

/* aicore */
#include "cm.h"

/* common */
#include "base.h"
#include "city.h"
#include "connection.h"
#include "disaster.h"
#include "government.h"
#include "idex.h"
#include "map.h"
#include "nation.h"
#include "packets.h"
#include "player.h"
#include "research.h"
#include "spaceship.h"
#include "specialist.h"
#include "tech.h"
#include "terrain.h"
#include "traderoutes.h"
#include "unit.h"
#include "unitlist.h"

#include "game.h"

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

bool am_i_server = FALSE;

static void game_defaults(void);

/**************************************************************************
  Is program type server?
**************************************************************************/
bool is_server(void)
{
  return am_i_server;
}

/**************************************************************************
  Set program type to server.
**************************************************************************/
void i_am_server(void)
{
  am_i_server = TRUE;
}

/**************************************************************************
  Set program type to client.
**************************************************************************/
void i_am_client(void)
{
  am_i_server = FALSE;
}

/**************************************************************************
Count the # of thousand citizen in a civilisation.
**************************************************************************/
int civ_population(const struct player *pplayer)
{
  int ppl=0;
  city_list_iterate(pplayer->cities, pcity)
    ppl+=city_population(pcity);
  city_list_iterate_end;
  return ppl;
}


/**************************************************************************
  Find city with given name from any player.
**************************************************************************/
struct city *game_city_by_name(const char *name)
{
  players_iterate(pplayer) {
    struct city *pcity = city_list_find_name(pplayer->cities, name);

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
struct city *game_city_by_number(int id)
{
  return idex_lookup_city(id);
}


/**************************************************************************
  Find unit out of all units in game: now uses fast idex method,
  instead of looking through all units of all players.
**************************************************************************/
struct unit *game_unit_by_number(int id)
{
  return idex_lookup_unit(id);
}

/**************************************************************************
  In the server call wipe_unit(), and never this function directly.
**************************************************************************/
void game_remove_unit(struct unit *punit)
{
  struct city *pcity;

  /* It's possible that during city transfer homecity/unit owner
   * information is inconsistent, and client then tries to remove
   * now unseen unit so that homecity is not in the list of cities
   * of the player (seemingly) owning the unit.
   * Thus cannot use player_city_by_number() here, but have to
   * consider cities of all players. */
  pcity = game_city_by_number(punit->homecity);
  if (pcity) {
    unit_list_remove(pcity->units_supported, punit);

    log_debug("game_remove_unit()"
              " at (%d,%d) unit %d, %s %s home (%d,%d) city %d, %s %s",
              TILE_XY(unit_tile(punit)),
              punit->id, 
              nation_rule_name(nation_of_unit(punit)),
              unit_rule_name(punit),
              TILE_XY(pcity->tile),
              punit->homecity,
              nation_rule_name(nation_of_city(pcity)),
              city_name(pcity));
  } else if (IDENTITY_NUMBER_ZERO == punit->homecity) {
    log_debug("game_remove_unit() at (%d,%d) unit %d, %s %s home %d",
              TILE_XY(unit_tile(punit)),
              punit->id, 
              nation_rule_name(nation_of_unit(punit)),
              unit_rule_name(punit),
              punit->homecity);
  } else {
    log_error("game_remove_unit() at (%d,%d) unit %d, %s %s home %d invalid",
              TILE_XY(unit_tile(punit)),
              punit->id, 
              nation_rule_name(nation_of_unit(punit)),
              unit_rule_name(punit),
              punit->homecity);
  }

  unit_list_remove(unit_tile(punit)->units, punit);
  unit_list_remove(unit_owner(punit)->units, punit);

  idex_unregister_unit(punit);

  if (game.callbacks.unit_deallocate) {
    (game.callbacks.unit_deallocate)(punit->id);
  }
  unit_virtual_destroy(punit);
}

/**************************************************************************
  Remove city from game.
**************************************************************************/
void game_remove_city(struct city *pcity)
{
  struct tile *pcenter = city_tile(pcity);
  struct player *powner = city_owner(pcity);

  if (NULL != powner) {
    /* always unlink before clearing data */
    city_list_remove(powner->cities, pcity);
  }

  if (NULL == pcenter) {
    log_debug("game_remove_city() virtual city %d, %s",
              pcity->id,
              city_name(pcity));
  } else {
    log_debug("game_remove_city() at (%d,%d) city %d, %s %s",
              TILE_XY(pcenter),
              pcity->id,
              nation_rule_name(nation_of_player(powner)),
              city_name(pcity));

    city_tile_iterate(city_map_radius_sq_get(pcity), pcenter, ptile) {
      if (tile_worked(ptile) == pcity) {
        tile_set_worked(ptile, NULL);
      }
    } city_tile_iterate_end;
  }

  idex_unregister_city(pcity);
  destroy_city_virtual(pcity);
}

/****************************************************************************
  Set default game values.
****************************************************************************/
static void game_defaults(void)
{
  int i;

  /* The control packet. */
  game.control.government_count        = 0;
  game.control.nation_count            = 0;
  game.control.num_base_types          = 0;
  game.control.num_road_types          = 0;
  game.control.num_impr_types          = 0;
  game.control.num_specialist_types    = 0;
  game.control.num_tech_types          = 0;
  game.control.num_unit_classes        = 0;
  game.control.num_unit_types          = 0;
  game.control.num_disaster_types      = 0;
  game.control.prefered_tileset[0]     = '\0';
  game.control.resource_count          = 0;
  game.control.styles_count            = 0;
  game.control.terrain_count           = 0;

  /* The info packet. */
  game.info.aifill           = GAME_DEFAULT_AIFILL;
  game.info.airlifting_style = GAME_DEFAULT_AIRLIFTINGSTYLE;
  game.info.angrycitizen     = GAME_DEFAULT_ANGRYCITIZEN;
  game.info.borders          = GAME_DEFAULT_BORDERS;
  game.info.calendar_skip_0  = FALSE;
  game.info.celebratesize    = GAME_DEFAULT_CELEBRATESIZE;
  game.info.citymindist      = GAME_DEFAULT_CITYMINDIST;
  game.info.cooling          = 0;
  game.info.coolinglevel     = 0; /* set later */
  game.info.diplomacy        = GAME_DEFAULT_DIPLOMACY;
  game.info.fogofwar         = GAME_DEFAULT_FOGOFWAR;
  game.info.foodbox          = GAME_DEFAULT_FOODBOX;
  game.info.fulltradesize    = GAME_DEFAULT_FULLTRADESIZE;
  for (i = 0; i < A_LAST; i++) {
    /* game.num_tech_types = 0 here */
    game.info.global_advances[i] = FALSE;
  }
  for (i = 0; i < B_LAST; i++) {
    /* game.num_impr_types = 0 here */
    game.info.great_wonder_owners[i] = WONDER_NOT_OWNED;
  }
  game.info.globalwarming    = 0;
  game.info.global_warming   = GAME_DEFAULT_GLOBAL_WARMING;
  game.info.gold             = GAME_DEFAULT_GOLD;
  game.info.government_during_revolution_id = G_MAGIC; /* flag */
  game.info.happyborders     = GAME_DEFAULT_HAPPYBORDERS;
  game.info.heating          = 0;
  game.info.is_edit_mode     = FALSE;
  game.info.is_new_game      = TRUE;
  game.info.killstack        = GAME_DEFAULT_KILLSTACK;
  game.info.killcitizen      = GAME_DEFAULT_KILLCITIZEN;
  game.info.negative_year_label[0] = '\0';
  game.info.notradesize      = GAME_DEFAULT_NOTRADESIZE;
  game.info.nuclearwinter    = 0;
  game.info.nuclear_winter   = GAME_DEFAULT_NUCLEAR_WINTER;
  game.info.positive_year_label[0] = '\0';
  game.info.rapturedelay     = GAME_DEFAULT_RAPTUREDELAY;
  game.info.restrictinfra    = GAME_DEFAULT_RESTRICTINFRA;
  game.info.sciencebox       = GAME_DEFAULT_SCIENCEBOX;
  game.info.shieldbox        = GAME_DEFAULT_SHIELDBOX;
  game.info.skill_level      = GAME_DEFAULT_SKILL_LEVEL;
  game.info.slow_invasions   = RS_DEFAULT_SLOW_INVASIONS;
  game.info.spacerace        = GAME_DEFAULT_SPACERACE;
  game.info.team_pooled_research = GAME_DEFAULT_TEAM_POOLED_RESEARCH;
  game.info.tech             = GAME_DEFAULT_TECHLEVEL;
  game.info.timeout          = GAME_DEFAULT_TIMEOUT;
  game.info.trademindist     = GAME_DEFAULT_TRADEMINDIST;
  game.info.trading_city     = GAME_DEFAULT_TRADING_CITY;
  game.info.trading_gold     = GAME_DEFAULT_TRADING_GOLD;
  game.info.trading_tech     = GAME_DEFAULT_TRADING_TECH;
  game.info.turn             = 0;
  game.info.warminglevel     = 0; /* set later */
  game.info.year_0_hack      = FALSE;
  game.info.year             = GAME_START_YEAR;

  /* The scenario packet. */
  game.scenario.description[0] = '\0';
  game.scenario.is_scenario = FALSE;
  game.scenario.name[0] = '\0';
  game.scenario.players = TRUE;

  /* Veteran system. */
  game.veteran = NULL;

  /* player colors */
  game.plr_bg_color = NULL;

  if (is_server()) {
    /* All settings only used by the server (./server/ and ./ai/ */
    sz_strlcpy(game.server.allow_take, GAME_DEFAULT_ALLOW_TAKE);
    game.server.allied_victory    = GAME_DEFAULT_ALLIED_VICTORY;
    game.server.allowed_city_names = GAME_DEFAULT_ALLOWED_CITY_NAMES;
    game.server.aqueductloss      = GAME_DEFAULT_AQUEDUCTLOSS;
    game.server.auto_ai_toggle    = GAME_DEFAULT_AUTO_AI_TOGGLE;
    game.server.autoattack        = GAME_DEFAULT_AUTOATTACK;
    game.server.barbarianrate     = GAME_DEFAULT_BARBARIANRATE;
    game.server.civilwarsize      = GAME_DEFAULT_CIVILWARSIZE;
    game.server.connectmsg[0]     = '\0';
    game.server.conquercost       = GAME_DEFAULT_CONQUERCOST;
    game.server.contactturns      = GAME_DEFAULT_CONTACTTURNS;
    for (i = 0; i < DEBUG_LAST; i++) {
      game.server.debug[i] = FALSE;
    }
    sz_strlcpy(game.server.demography, GAME_DEFAULT_DEMOGRAPHY);
    game.server.diplchance        = GAME_DEFAULT_DIPLCHANCE;
    game.server.diplcost          = GAME_DEFAULT_DIPLCOST;
    game.server.dispersion        = GAME_DEFAULT_DISPERSION;
    game.server.endspaceship      = GAME_DEFAULT_END_SPACESHIP;
    game.server.end_turn          = GAME_DEFAULT_END_TURN;
    game.server.event_cache.chat  = GAME_DEFAULT_EVENT_CACHE_CHAT;
    game.server.event_cache.info  = GAME_DEFAULT_EVENT_CACHE_INFO;
    game.server.event_cache.max_size = GAME_DEFAULT_EVENT_CACHE_MAX_SIZE;
    game.server.event_cache.turns = GAME_DEFAULT_EVENT_CACHE_TURNS;
    game.server.foggedborders     = GAME_DEFAULT_FOGGEDBORDERS;
    game.server.fogofwar_old      = game.info.fogofwar;
    game.server.freecost          = GAME_DEFAULT_FREECOST;
    game.server.homecaughtunits   = GAME_DEFAULT_HOMECAUGHTUNITS;
    game.server.kick_time         = GAME_DEFAULT_KICK_TIME;
    game.server.killunhomed       = GAME_DEFAULT_KILLUNHOMED;
    game.server.maxconnectionsperhost = GAME_DEFAULT_MAXCONNECTIONSPERHOST;
    game.server.last_ping         = 0;
    game.server.max_players       = GAME_DEFAULT_MAX_PLAYERS;
    game.server.meta_info.user_message[0] = '\0';
    game.server.meta_info.user_message_set = FALSE;
    game.server.mgr_distance      = GAME_DEFAULT_MGR_DISTANCE;
    game.server.mgr_foodneeded    = GAME_DEFAULT_MGR_FOODNEEDED;
    game.server.mgr_nationchance  = GAME_DEFAULT_MGR_NATIONCHANCE;
    game.server.mgr_turninterval  = GAME_DEFAULT_MGR_TURNINTERVAL;
    game.server.mgr_worldchance   = GAME_DEFAULT_MGR_WORLDCHANCE;
    game.server.migration         = GAME_DEFAULT_MIGRATION;
    game.server.min_players       = GAME_DEFAULT_MIN_PLAYERS;
    game.server.natural_city_names = GAME_DEFAULT_NATURALCITYNAMES;
    game.server.plrcolormode      = GAME_DEFAULT_PLRCOLORMODE;
    game.server.netwait           = GAME_DEFAULT_NETWAIT;
    game.server.occupychance      = GAME_DEFAULT_OCCUPYCHANCE;
    game.server.onsetbarbarian    = GAME_DEFAULT_ONSETBARBARIAN;
    game.server.phase_mode_stored = GAME_DEFAULT_PHASE_MODE;
    game.server.pingtime          = GAME_DEFAULT_PINGTIME;
    game.server.pingtimeout       = GAME_DEFAULT_PINGTIMEOUT;
    game.server.razechance        = GAME_DEFAULT_RAZECHANCE;
    game.server.revealmap         = GAME_DEFAULT_REVEALMAP;
    game.server.revolution_length = GAME_DEFAULT_REVOLUTION_LENGTH;
    sz_strlcpy(game.server.rulesetdir, GAME_DEFAULT_RULESETDIR);
    game.server.save_compress_level = GAME_DEFAULT_COMPRESS_LEVEL;
    game.server.save_compress_type = GAME_DEFAULT_COMPRESS_TYPE;
    sz_strlcpy(game.server.save_name, GAME_DEFAULT_SAVE_NAME);
    game.server.save_nturns       = GAME_DEFAULT_SAVETURNS;
    game.server.save_options.save_known = TRUE;
    game.server.save_options.save_private_map = TRUE;
    game.server.save_options.save_random = TRUE;
    game.server.save_options.save_starts = TRUE;
    game.server.savepalace        = GAME_DEFAULT_SAVEPALACE;
    game.server.scorelog          = GAME_DEFAULT_SCORELOG;
    game.server.scoreturn         = GAME_DEFAULT_SCORETURN;
    game.server.seed              = GAME_DEFAULT_SEED;
    sz_strlcpy(game.server.start_units, GAME_DEFAULT_START_UNITS);
    game.server.start_year        = GAME_START_YEAR;
    game.server.tcptimeout        = GAME_DEFAULT_TCPTIMEOUT;
    game.server.techlost_donor    = GAME_DEFAULT_TECHLOST_DONOR;
    game.server.techlost_recv     = GAME_DEFAULT_TECHLOST_RECV;
    game.server.techpenalty       = GAME_DEFAULT_TECHPENALTY;
    game.server.timeoutaddenemymove = GAME_DEFAULT_TIMEOUTADDEMOVE;
    game.server.timeoutcounter    = GAME_DEFAULT_TIMEOUTCOUNTER;
    game.server.timeoutinc        = GAME_DEFAULT_TIMEOUTINC;
    game.server.timeoutincmult    = GAME_DEFAULT_TIMEOUTINCMULT;
    game.server.timeoutint        = GAME_DEFAULT_TIMEOUTINT;
    game.server.timeoutintinc     = GAME_DEFAULT_TIMEOUTINTINC;
    game.server.turnblock         = GAME_DEFAULT_TURNBLOCK;
    game.server.unitwaittime      = GAME_DEFAULT_UNITWAITTIME;
    game.server.plr_colors        = NULL;
  }

  terrain_control.river_help_text[0] = '\0';
}

/****************************************************************************
  Initialise all game settings.

  The variables are listed in alphabetical order.
****************************************************************************/
void game_init(void)
{
  game_defaults();
  player_slots_init();
  map_init();
  team_slots_init();
  game_ruleset_init();
  idex_init();
  cm_init();
  player_researches_init();
}

/****************************************************************************
  Initialize map-specific parts of the game structure.  Maybe these should
  be moved into the map structure?
****************************************************************************/
void game_map_init(void)
{
  /* FIXME: it's not clear where these values should be initialized.  It
   * can't be done in game_init because the map isn't created yet.  Maybe it
   * should be done in the mapgen code or in the maphand code.  It should
   * surely be called when the map is generated. */
  game.info.warminglevel = (map_num_tiles() + 499) / 500;
  game.info.coolinglevel = (map_num_tiles() + 499) / 500;
}

/***************************************************************
  Frees all memory of the game.
***************************************************************/
void game_free(void)
{
  player_slots_free();
  map_free();
  idex_free();
  team_slots_free();
  set_allowed_nation_groups(NULL);
  game_ruleset_free();
  cm_free();
}

/***************************************************************
  Do all changes to change view, and not full
  game_free()/game_init().
***************************************************************/
void game_reset(void)
{
  if (is_server()) {
    game_free();
    game_init();
  } else {
    /* Reset the players infos. */
    players_iterate(pplayer) {
      player_clear(pplayer, FALSE);
    } players_iterate_end;

    map_free();
    idex_free();

    map_init();
    idex_init();
  }
}

/***************************************************************
  Initialize the objects which will read from a ruleset.
***************************************************************/
void game_ruleset_init(void)
{
  nation_groups_init();
  ruleset_cache_init();
  disaster_types_init();
  trade_route_types_init();
  terrains_init();
  base_types_init();
  road_types_init();
  improvements_init();
  techs_init();
  unit_classes_init();
  unit_types_init();
  specialists_init();
  user_unit_type_flags_init();
  user_terrain_flags_init();
}

/***************************************************************
  Frees all memory which in objects which are read from a ruleset.
***************************************************************/
void game_ruleset_free(void)
{
  CALL_FUNC_EACH_AI(units_ruleset_close);

  specialists_free();
  techs_free();
  governments_free();
  nations_free();
  unit_types_free();
  unit_type_flags_free();
  role_unit_precalcs_free();
  improvements_free();
  base_types_free();
  road_types_free();
  city_styles_free();
  disaster_types_free();
  terrains_free();
  user_terrain_flags_free();
  ruleset_cache_free();
  nation_groups_free();

  /* Destroy the default veteran system. */
  veteran_system_destroy(game.veteran);
  game.veteran = NULL;
}

/***************************************************************
  Initialize wonder information.
***************************************************************/
void initialize_globals(void)
{
  players_iterate(pplayer) {
    city_list_iterate(pplayer->cities, pcity) {
      city_built_iterate(pcity, pimprove) {
        if (is_wonder(pimprove)) {
          if (is_great_wonder(pimprove)) {
            game.info.great_wonder_owners[improvement_index(pimprove)] =
                player_number(pplayer);
          }
          pplayer->wonders[improvement_index(pimprove)] = pcity->id;
        }
      } city_built_iterate_end;
    } city_list_iterate_end;
  } players_iterate_end;
}

/***************************************************************
  Returns the next year in the game.
***************************************************************/
int game_next_year(int year)
{
  int increase = get_world_bonus(EFT_TURN_YEARS);
  const int slowdown = (game.info.spacerace
			? get_world_bonus(EFT_SLOW_DOWN_TIMELINE) : 0);

  if (game.info.year_0_hack) {
    /* hacked it to get rid of year 0 */
    year = 0;
    game.info.year_0_hack = FALSE;
  }

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

  /* Note the slowdown operates even if Enable_Space is not active.  See
   * README.effects for specifics. */
  if (slowdown >= 3) {
    if (increase > 1) {
      increase = 1;
    }
  } else if (slowdown >= 2) {
    if (increase > 2) {
      increase = 2;
    }
  } else if (slowdown >= 1) {
    if (increase > 5) {
      increase = 5;
    }
  }

  year += increase;

  if (year == 0 && game.info.calendar_skip_0) {
    year = 1;
    game.info.year_0_hack = TRUE;
  }

  return year;
}

/***************************************************************
  Advance the game year.
***************************************************************/
void game_advance_year(void)
{
  game.info.year = game_next_year(game.info.year);
  game.info.turn++;
}

/**************************************************************************
  Return TRUE if it is this player's phase.
  NB: The meaning of the 'phase' argument must match its use in the
  function begin_turn() in server/srv_main.c.
  NB: The phase mode PMT_TEAMS_ALTERNATE assumes that every player is
  on a team, i.e. that pplayer->team is never NULL.
**************************************************************************/
bool is_player_phase(const struct player *pplayer, int phase)
{
  switch (game.info.phase_mode) {
  case PMT_CONCURRENT:
    return TRUE;
    break;
  case PMT_PLAYERS_ALTERNATE:
    return player_number(pplayer) == phase;
    break;
  case PMT_TEAMS_ALTERNATE:
    fc_assert_ret_val(NULL != pplayer->team, FALSE);
    return team_number(pplayer->team) == phase;
    break;
  default:
    break;
  }

  fc_assert_msg(FALSE, "Unrecognized phase mode %d in is_player_phase().",
                phase);
  return TRUE;
}

/****************************************************************************
  Return a prettily formatted string containing the population text.  The
  population is passed in as the number of citizens, in unit
  (tens/hundreds/thousands...) defined in cities.ruleset.
****************************************************************************/
const char *population_to_text(int thousand_citizen)
{
  /* big_int_to_text can't handle negative values, and in any case we'd
   * better not have a negative population. */
  fc_assert_ret_val(thousand_citizen >= 0, NULL);
  return big_int_to_text(thousand_citizen, game.info.pop_report_zeroes - 1);
}

/****************************************************************************
  Produce a statically allocated textual representation of the given
  year.
****************************************************************************/
const char *textyear(int year)
{
  static char y[32];
  if (year < 0) {
    /* TRANS: <year> <label> -> "1000 BC" */
    fc_snprintf(y, sizeof(y), _("%d %s"), -year,
                game.info.negative_year_label);
  } else {
    /* TRANS: <year> <label> -> "1000 AD" */
    fc_snprintf(y, sizeof(y), _("%d %s"), year,
                game.info.positive_year_label);
  }
  return y;
}

/**************************************************************************
  Return a string conaining the save year.
**************************************************************************/
static char *year_suffix(void)
{
  static char buf[MAX_LEN_NAME];
  const char *year_suffix;
  char safe_year_suffix[MAX_LEN_NAME];
  const char *max = safe_year_suffix + MAX_LEN_NAME - 1;
  char *c = safe_year_suffix;

  if (game.info.year < 0) {
    year_suffix = game.info.negative_year_label;
  } else {
    year_suffix = game.info.positive_year_label;
  }

  /* Remove all non alphanumeric characters from the year suffix. */
  for (; '\0' != *year_suffix && c < max; year_suffix++) {
    if (fc_isalnum(*year_suffix)) {
      *c++ = *year_suffix;
    }
  }
  *c = '\0';

  fc_snprintf(buf, sizeof(buf), "%s", safe_year_suffix);

  return buf;
}

/**************************************************************************
  Generate a default save file name and place it in the provided buffer.
  Within the name the following custom formats are allowed:

    %R = <reason>
    %S = <suffix>
    %T = <game.info.turn>
    %Y = <game.info.year>

  Examples:
    'freeciv-T%04T-Y%+04Y-%R' => 'freeciv-T0099-Y-0050-manual'
                              => 'freeciv-T0100-Y00001-auto'

  Returns the number of characters written, or the number of characters
  that would have been written if truncation occurs.

  NB: If you change the format definition, be sure to update the above
      function comment and the help text for the 'savename' setting.
**************************************************************************/
int generate_save_name(const char *format, char *buf, int buflen,
                       const char *reason)
{
  struct cf_sequence sequences[] = {
    cf_str_seq('R', (reason == NULL) ? "auto" : reason),
    cf_str_seq('S', year_suffix()),
    cf_int_seq('T', game.info.turn),
    cf_int_seq('Y', game.info.year),
    cf_end()
  };

  fc_vsnprintcf(buf, buflen, format, sequences, -1);

  if (0 == strcmp(format, buf)) {
    /* Use the default savename if 'format' does not contain
     * printf information. */
    char savename[512];

    fc_snprintf(savename, sizeof(savename), "%s-T%%04T-Y%%05Y-%%R",
                format);
    fc_vsnprintcf(buf, buflen, savename, sequences, -1);
  }

  log_debug("save name generated from '%s': %s", format, buf);

  return strlen(buf);
}

/**************************************************************************
  Initialize user flag.
**************************************************************************/
void user_flag_init(struct user_flag *flag)
{
  flag->name = NULL;
  flag->helptxt = NULL;
}

/**************************************************************************
  Free user flag.
**************************************************************************/
void user_flag_free(struct user_flag *flag)
{
  if (flag->name != NULL) {
    FC_FREE(flag->name);
    flag->name = NULL;
  }
  if (flag->helptxt != NULL) {
    FC_FREE(flag->helptxt);
    flag->helptxt = NULL;
  }
}
