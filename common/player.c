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
#include <stdlib.h>
#include <string.h>

#include "city.h"
#include "fcintl.h"
#include "game.h"
#include "government.h"
#include "idex.h"
#include "improvement.h"
#include "log.h"
#include "map.h"
#include "mem.h"
#include "rand.h"
#include "shared.h"
#include "support.h"
#include "tech.h"
#include "unit.h"

#include "player.h"


/***************************************************************
...
***************************************************************/
bool player_has_embassy(struct player *pplayer, struct player *pplayer2)
{
  return (pplayer->embassy & (1<<pplayer2->player_no)) ||
         (player_owns_active_wonder(pplayer, B_MARCO) &&
          pplayer != pplayer2 && !is_barbarian(pplayer2));
}

/****************************************************************
...
*****************************************************************/
bool player_owns_city(struct player *pplayer, struct city *pcity)
{
  if (!pcity || !pplayer)
    return FALSE;			/* better safe than sorry */
  return (pcity->owner==pplayer->player_no);
}

/***************************************************************
In the server you must use server_player_init
***************************************************************/
void player_init(struct player *plr)
{
  int i;

  plr->player_no=plr-game.players;

  sz_strlcpy(plr->name, "YourName");
  sz_strlcpy(plr->username, "UserName");
  plr->is_male = TRUE;
  plr->government=game.default_government;
  plr->nation=MAX_NUM_NATIONS;
  plr->capital = FALSE;
  unit_list_init(&plr->units);
  city_list_init(&plr->cities);
  conn_list_init(&plr->connections);
  plr->current_conn = NULL;
  plr->is_connected = FALSE;
  plr->is_alive=TRUE;
  plr->embassy=0;
  plr->reputation=GAME_DEFAULT_REPUTATION;
  for(i = 0; i < MAX_NUM_PLAYERS + MAX_NUM_BARBARIANS; i++) {
    plr->diplstates[i].type = DS_NO_CONTACT;
    plr->diplstates[i].has_reason_to_cancel = 0;
  }
  plr->city_style=0;            /* should be first basic style */
  plr->ai.control=FALSE;
  plr->ai.tech_goal = A_NONE;
  plr->ai.handicap = 0;
  plr->ai.skill_level = 0;
  plr->ai.fuzzy = 0;
  plr->ai.expand = 100;
  plr->ai.is_barbarian = FALSE;
  plr->future_tech=0;
  plr->economic.tax=PLAYER_DEFAULT_TAX_RATE;
  plr->economic.science=PLAYER_DEFAULT_SCIENCE_RATE;
  plr->economic.luxury=PLAYER_DEFAULT_LUXURY_RATE;
  plr->research.changed_from = -1;
  player_limit_to_government_rates(plr);
  spaceship_init(&plr->spaceship);
  for (i = 0; i < MAX_NUM_WORKLISTS; i++) {
    init_worklist(&plr->worklists[i]);
    plr->worklists[i].is_valid = FALSE;
  }
  plr->gives_shared_vision = 0;
  plr->really_gives_vision = 0;

  /* Initialise list of improvements with Player-wide equiv_range */
  improvement_status_init(plr->improvements);
  /* Initialise vector of effects with player range. */
  geff_vector_init(&plr->effects);

  /* Blank lists of Island-range improvements and effects (these are
     initialised by player_init_island_impr) */
  plr->island_improv = NULL;
  plr->island_effects = NULL;

  plr->attribute_block.data = NULL;
  plr->attribute_block.length = 0;
}

/***************************************************************
  Set up the player's lists of Island-range improvements and
  effects. These lists must also be redimensioned (e.g. by
  update_island_impr_effect) if the number of islands later
  changes.
***************************************************************/
void player_init_island_imprs(struct player *plr, int numcont)
{
  int i;

  player_free_island_imprs(plr, numcont);
  if (game.num_impr_types>0) {
    /* Initialise lists of improvements with island-wide equiv_range. */
    if (plr->island_improv)
      free(plr->island_improv);
    plr->island_improv=fc_calloc((numcont+1)*game.num_impr_types,
				 sizeof(Impr_Status));
    for (i=0; i<=numcont; i++) {
      improvement_status_init(&plr->island_improv[i*game.num_impr_types]);
    }

    /* Initialise lists of island-range effects. */
    plr->island_effects=fc_calloc(numcont+1, sizeof(struct geff_vector));
    for (i=0; i<=numcont; i++) {
      geff_vector_init(&plr->island_effects[i]);
    }
  }
}

/***************************************************************
  Frees the player's list of island-range improvements and
  effects.
***************************************************************/
void player_free_island_imprs(struct player *plr, int numcont)
{
  int i;

  if (plr->island_improv)
    free(plr->island_improv);
  if (plr->island_effects) {
    for (i=0; i<=numcont; i++) {
      geff_vector_free(&plr->island_effects[i]);
    }
    free(plr->island_effects);
  }
  plr->island_improv=NULL;
  plr->island_effects=NULL;
}

/***************************************************************
...
***************************************************************/
void player_set_unit_focus_status(struct player *pplayer)
{
  unit_list_iterate(pplayer->units, punit) 
    punit->focus_status=FOCUS_AVAIL;
  unit_list_iterate_end;
}

/***************************************************************
...
***************************************************************/
struct player *find_player_by_name(char *name)
{
  int i;

  for(i=0; i<game.nplayers; i++)
    if (mystrcasecmp(name, game.players[i].name) == 0)
	return &game.players[i];

  return NULL;
}

/***************************************************************
  Find player by name, allowing unambigous prefix (ie abbreviation).
  Returns NULL if could not match, or if ambiguous or other
  problem, and fills *result with characterisation of match/non-match
  (see shared.[ch])
***************************************************************/
static const char *pname_accessor(int i) {
  return game.players[i].name;
}
struct player *find_player_by_name_prefix(const char *name,
					  enum m_pre_result *result)
{
  int ind;

  *result = match_prefix(pname_accessor, game.nplayers, MAX_LEN_NAME-1,
			 mystrncasecmp, name, &ind);

  if (*result < M_PRE_AMBIGUOUS) {
    return get_player(ind);
  } else {
    return NULL;
  }
}

/***************************************************************
Find player by its user name (not player/leader name)
***************************************************************/
struct player *find_player_by_user(char *name)
{
  int i;

  for(i=0; i<game.nplayers; i++)
    if (mystrcasecmp(name, game.players[i].username) == 0)
	return &game.players[i];

  return NULL;
}

/***************************************************************
no map visibility check here!
***************************************************************/
int player_can_see_unit(struct player *pplayer, struct unit *punit)
{
  if (punit->owner==pplayer->player_no)
    return 1;
  if (is_hiding_unit(punit)) {
    /* Search for units/cities that might be able to see the sub/missile */
    struct city *pcity;
    square_iterate(punit->x, punit->y, 1, x, y) {
      unit_list_iterate(map_get_tile(x, y)->units, punit2) {
	if (punit2->owner == pplayer->player_no)
	  return 1;
      } unit_list_iterate_end;

      pcity = map_get_city(x, y);
      if (pcity && pcity->owner == pplayer->player_no)
	return 1;
    } square_iterate_end;
    return 0;
  } else {
    return 1;
  }
}

/***************************************************************
 If the specified player owns the city with the specified id,
 return pointer to the city struct.  Else return NULL.
 Now always uses fast idex_lookup_city.
***************************************************************/
struct city *player_find_city_by_id(struct player *pplayer, int city_id)
{
  struct city *pcity = idex_lookup_city(city_id);
  
  if(pcity && (pcity->owner==pplayer->player_no)) {
    return pcity;
  } else {
    return NULL;
  }
}

/***************************************************************
 If the specified player owns the unit with the specified id,
 return pointer to the unit struct.  Else return NULL.
 Uses fast idex_lookup_city.
***************************************************************/
struct unit *player_find_unit_by_id(struct player *pplayer, int unit_id)
{
  struct unit *punit = idex_lookup_unit(unit_id);
  
  if(punit && (punit->owner==pplayer->player_no)) {
    return punit;
  } else {
    return NULL;
  }
}

/*************************************************************************
Return 1 if x,y is inside any of the player's city radii.
**************************************************************************/
int player_in_city_radius(struct player *pplayer, int x, int y)
{
  struct city *pcity;
  map_city_radius_iterate(x, y, x1, y1) {
    pcity = map_get_city(x1, y1);
    if (pcity && (pcity->owner == pplayer->player_no))
      return TRUE;
  } map_city_radius_iterate_end;
  return FALSE;
}

/**************************************************************************
 Return 1 if one of the player's cities has the specified wonder,
 and it is not obsolete.
**************************************************************************/
bool player_owns_active_wonder(struct player *pplayer,
			      Impr_Type_id id)
{
  return (improvement_exists(id)
	  && is_wonder(id)
	  && (!wonder_obsolete(id))
	  && player_find_city_by_id(pplayer, game.global_wonders[id]));
}

/**************************************************************************
 ...
**************************************************************************/
bool player_owns_active_govchange_wonder(struct player *pplayer)
{
  return ( player_owns_active_wonder(pplayer, B_LIBERTY) ||
	   ( (improvement_variant(B_PYRAMIDS)==1) &&
	     player_owns_active_wonder(pplayer, B_PYRAMIDS) ) ||
	   ( (improvement_variant(B_UNITED)==1) &&
	     player_owns_active_wonder(pplayer, B_UNITED) ) );
}

/**************************************************************************
 Returns the number of techs the player has researched which has this
 flag. Needs to be optimized later (e.g. int tech_flags[TF_LAST] in
 struct player)
**************************************************************************/
int num_known_tech_with_flag(struct player *pplayer, enum tech_flag_id flag)
{
  int i, result = 0;
  for (i = A_FIRST; i < game.num_tech_types; i++) {
    if (get_invention(pplayer, i) == TECH_KNOWN && tech_flag(i, flag)) {
      result++;
    }
  }
  return result;
}

/**************************************************************************
 Returns TRUE iff the player knows at least one tech which has the
 given flag.
**************************************************************************/
int player_knows_techs_with_flag(struct player *pplayer,
				 enum tech_flag_id flag)
{
  return num_known_tech_with_flag(pplayer, flag) > 0;
}

/**************************************************************************
The following limits a player's rates to those that are acceptable for the
present form of government.  If a rate exceeds maxrate for this government,
it adjusts rates automatically adding the extra to the 2nd highest rate,
preferring science to taxes and taxes to luxuries.
(It assumes that for any government maxrate>=50)
**************************************************************************/
void player_limit_to_government_rates(struct player *pplayer)
{
  int maxrate, surplus;

  /* ai players allowed to cheat */
  if (pplayer->ai.control) {
    return;
  }

  maxrate = get_government_max_rate(pplayer->government);
  surplus = 0;
  if (pplayer->economic.luxury > maxrate) {
    surplus += pplayer->economic.luxury - maxrate;
    pplayer->economic.luxury = maxrate;
  }
  if (pplayer->economic.tax > maxrate) {
    surplus += pplayer->economic.tax - maxrate;
    pplayer->economic.tax = maxrate;
  }
  if (pplayer->economic.science > maxrate) {
    surplus += pplayer->economic.science - maxrate;
    pplayer->economic.science = maxrate;
  }

  assert(surplus % 10 == 0);
  while (surplus > 0) {
    if (pplayer->economic.science < maxrate) {
      pplayer->economic.science += 10;
    } else if (pplayer->economic.tax < maxrate) {
      pplayer->economic.tax += 10;
    } else if (pplayer->economic.luxury < maxrate) {
      pplayer->economic.luxury += 10;
    } else {
      abort();
    }
    surplus -= 10;
  }

  return;
}

/**************************************************************************
  Hack to support old code which expects player to have single address
  string (possibly blank_addr_str) and not yet updated for multi-connects.
  When all such code is converted, this function should be removed.
  Returns "blank" address for no connections, single address for one,
  or string "<multiple>" if there are multiple connections.
  
  Currently used by:
      civserver.c: send_server_info_to_metaserver()
      plrhand.c: send_player_info_c()
      gui-{xaw,gtk,mui}/plrdlg.c: update_players_dialog()
**************************************************************************/
const char *player_addr_hack(struct player *pplayer)
{
  int n = conn_list_size(&pplayer->connections);
  return ((n==0) ? blank_addr_str :
	  (n==1) ? conn_list_get(&pplayer->connections, 0)->addr :
	  "<multiple>");
}

/**************************************************************************
Locate the city where the players palace is located, (NULL Otherwise) 
**************************************************************************/
struct city *find_palace(struct player *pplayer)
{
  city_list_iterate(pplayer->cities, pcity) 
    if (city_got_building(pcity, B_PALACE)) 
      return pcity;
  city_list_iterate_end;
  return NULL;
}

/**************************************************************************
...
**************************************************************************/
bool player_knows_improvement_tech(struct player *pplayer,
				   Impr_Type_id id)
{
  int t;
  if (!improvement_exists(id)) return FALSE;
  t = get_improvement_type(id)->tech_req;
  return (get_invention(pplayer, t) == TECH_KNOWN);
}

/**************************************************************************
...
**************************************************************************/
bool ai_handicap(struct player *pplayer, enum handicap_type htype)
{
  if (!pplayer->ai.control) {
    return TRUE;
  }
  return BOOL_VAL(pplayer->ai.handicap & htype);
}

/**************************************************************************
Return the value normal_decision (a boolean), except if the AI is fuzzy,
then sometimes flip the value.  The intention of this is that instead of
    if (condition) { action }
you can use
    if (ai_fuzzy(pplayer, condition)) { action }
to sometimes flip a decision, to simulate an AI with some confusion,
indecisiveness, forgetfulness etc. In practice its often safer to use
    if (condition && ai_fuzzy(pplayer,1)) { action }
for an action which only makes sense if condition holds, but which a
fuzzy AI can safely "forget".  Note that for a non-fuzzy AI, or for a
human player being helped by the AI (eg, autosettlers), you can ignore
the "ai_fuzzy(pplayer," part, and read the previous example as:
    if (condition && 1) { action }
--dwp
**************************************************************************/
bool ai_fuzzy(struct player *pplayer, bool normal_decision)
{
  if (!pplayer->ai.control || pplayer->ai.fuzzy == 0) return normal_decision;
  if (myrand(1000) >= pplayer->ai.fuzzy) return normal_decision;
  return !normal_decision;
}

/**************************************************************************
Return a reputation level as a human-readable string
**************************************************************************/
const char *reputation_text(const int rep)
{
  if (rep == -1)
    return "-";
  else if (rep > GAME_MAX_REPUTATION * 0.95)
    return _("Spotless");
  else if (rep > GAME_MAX_REPUTATION * 0.85)
    return _("Excellent");
  else if (rep > GAME_MAX_REPUTATION * 0.75)
    return _("Honorable");
  else if (rep > GAME_MAX_REPUTATION * 0.55)
    return _("Questionable");
  else if (rep > GAME_MAX_REPUTATION * 0.30)
    return _("Dishonorable");
  else if (rep > GAME_MAX_REPUTATION * 0.15)
    return _("Poor");
  else if (rep > GAME_MAX_REPUTATION * 0.07)
    return _("Despicable");
  else
    return _("Atrocious");
}

/**************************************************************************
Return a diplomatic state as a human-readable string
**************************************************************************/
const char *diplstate_text(const enum diplstate_type type)
{
  static const char *ds_names[DS_LAST] = 
  {
    N_("?diplomatic_state:Neutral"),
    N_("?diplomatic_state:War"), 
    N_("?diplomatic_state:Cease-fire"),
    N_("?diplomatic_state:Peace"),
    N_("?diplomatic_state:Alliance"),
    N_("?diplomatic_state:No Contact")
  };

  if (type < DS_LAST)
    return Q_(ds_names[type]);
  freelog(LOG_FATAL, "Bad diplstate_type in diplstate_text: %d", type);
  abort();
}

/***************************************************************
returns diplomatic state type between two players
***************************************************************/
const struct player_diplstate *pplayer_get_diplstate(const struct player *pplayer,
						     const struct player *pplayer2)
{
  return &(pplayer->diplstates[pplayer2->player_no]);
}

/***************************************************************
returns true iff players can attack each other.
***************************************************************/
bool pplayers_at_war(const struct player *pplayer,
		    const struct player *pplayer2)
{
  enum diplstate_type ds = pplayer_get_diplstate(pplayer, pplayer2)->type;
  if (pplayer == pplayer2) return FALSE;
  if (is_barbarian(pplayer) || is_barbarian(pplayer2))
    return TRUE;
  return ds == DS_WAR || ds == DS_NO_CONTACT;
}

/***************************************************************
returns true iff players are allied
***************************************************************/
bool pplayers_allied(const struct player *pplayer,
		    const struct player *pplayer2)
{
  enum diplstate_type ds = pplayer_get_diplstate(pplayer, pplayer2)->type;
  if (pplayer == pplayer2)
    return TRUE;
  if (is_barbarian(pplayer) || is_barbarian(pplayer2))
    return FALSE;
  return (ds == DS_ALLIANCE);
}

/***************************************************************
returns true iff players have peace or cease-fire
***************************************************************/
bool pplayers_non_attack(const struct player *pplayer,
			const struct player *pplayer2)
{
  enum diplstate_type ds = pplayer_get_diplstate(pplayer, pplayer2)->type;
  if (pplayer == pplayer2)
    return FALSE;
  if (is_barbarian(pplayer) || is_barbarian(pplayer2))
    return FALSE;
  return (ds == DS_PEACE || ds == DS_CEASEFIRE || ds == DS_NEUTRAL);
}

/**************************************************************************
...
**************************************************************************/
bool is_barbarian(const struct player *pplayer)
{
  return (pplayer->ai.is_barbarian > 0);
}
