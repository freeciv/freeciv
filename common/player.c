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
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "city.h"
#include "game.h"
#include "government.h"
#include "map.h"
#include "rand.h"
#include "shared.h"
#include "support.h"
#include "tech.h"
#include "unit.h"

#include "player.h"

extern int is_server;

/***************************************************************
...
***************************************************************/
int player_has_embassy(struct player *pplayer, struct player *pplayer2)
{
  return (pplayer->embassy & (1<<pplayer2->player_no)) ||
         (player_owns_active_wonder(pplayer, B_MARCO) &&
          pplayer != pplayer2 && !is_barbarian(pplayer2));
}

/****************************************************************
...
*****************************************************************/

int player_owns_city(struct player *pplayer, struct city *pcity)
{
  if (!pcity || !pplayer) return 0; /* better safe than sorry */
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
  plr->is_male = 1;
  plr->government=game.default_government;
  plr->nation=MAX_NUM_NATIONS;
  plr->capital=0;
  unit_list_init(&plr->units);
  city_list_init(&plr->cities);
  sz_strlcpy(plr->addr, "---.---.---.---");
  plr->is_alive=1;
  plr->embassy=0;
  plr->city_style=0;            /* should be first basic style */
  plr->ai.control=0;
  plr->ai.tech_goal = A_NONE;
  plr->ai.handicap = 0;
  plr->ai.skill_level = 0;
  plr->ai.fuzzy = 0;
  plr->ai.expand = 100;
  plr->ai.is_barbarian = 0;
  plr->future_tech=0;
  plr->economic.tax=PLAYER_DEFAULT_TAX_RATE;
  plr->economic.science=PLAYER_DEFAULT_SCIENCE_RATE;
  plr->economic.luxury=PLAYER_DEFAULT_LUXURY_RATE;
  player_limit_to_government_rates(plr);
  spaceship_init(&plr->spaceship);
  for (i = 0; i < MAX_NUM_WORKLISTS; i++) {
    init_worklist(&plr->worklists[i]);
    plr->worklists[i].is_valid = 0;
  }
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
     if(!mystrcasecmp(name, game.players[i].name))
	return &game.players[i];

  return 0;
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
     if(!mystrcasecmp(name, game.players[i].username))
	return &game.players[i];

  return 0;
}

/***************************************************************
no map visibility check here!
***************************************************************/
int player_can_see_unit(struct player *pplayer, struct unit *punit)
{
  if(punit->owner==pplayer->player_no)
    return 1;
  if(is_hiding_unit(punit)) {
    /* Search for units/cities that might be able to see the sub/missile */
    int x, y;
    struct city *pcity;
    struct unit *punit2;
    for(y=punit->y-1; y<=punit->y+1; ++y) { 
      if(y<0 || y>=map.ysize)
	continue;
      for(x=punit->x-1; x<=punit->x+1; ++x) { 
	if((punit2=unit_list_get(&map_get_tile(x, y)->units, 0)) &&
	   punit2->owner == pplayer->player_no ) return 1;

	if((pcity=map_get_city(x, y)) && pcity->owner==pplayer->player_no)
	  return 1;
      }
    }
    return 0;
  }
  
  return 1;
}

/***************************************************************
 If the specified player owns the city with the specified id,
 return pointer to the city struct.  Else return 0.
 In the server we want to use find_city_by_id, which is fast due
 to the citycache, while in the client we just search the
 player's cities, rather than find_city_by_id which goes through
 all players.
***************************************************************/
struct city *player_find_city_by_id(struct player *pplayer, int city_id)
{
  struct city *pcity;
  
  if(is_server) {
    pcity = find_city_by_id(city_id);
    if(pcity && (pcity->owner==pplayer->player_no)) {
      return pcity;
    } else {
      return 0;
    }
  } else {
    return city_list_find_id(&pplayer->cities, city_id);
  }
}

/*************************************************************************
Return 1 if x,y is inside any of the player's city radii.
**************************************************************************/
int player_in_city_radius(struct player *pplayer, int x, int y)
{
  int i, j;
  struct city *pcity;
  city_map_iterate(i, j) {
    pcity = map_get_city(x+i-2, y+j-2);
    if (pcity && (pcity->owner == pplayer->player_no)) return 1;
  }
  return 0;
}

/**************************************************************************
 Return 1 if one of the player's cities has the specified wonder,
 and it is not obsolete.
**************************************************************************/
int player_owns_active_wonder(struct player *pplayer,
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
int player_owns_active_govchange_wonder(struct player *pplayer)
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
int player_knows_techs_with_flag(struct player *pplayer, int flag)
{
  int i;
  int count=0;
  for( i=A_FIRST; i<game.num_tech_types; i++ ) {
    if((get_invention(pplayer, i) == TECH_KNOWN) && tech_flag(i,flag))
      count++;
  }
  return count;
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

  if (pplayer->economic.luxury > maxrate) {
    surplus = pplayer->economic.luxury - maxrate;
    pplayer->economic.luxury = maxrate;
    if (pplayer->economic.science >= pplayer->economic.tax)
      pplayer->economic.science += surplus;
    else
      pplayer->economic.tax += surplus;
  }
  if (pplayer->economic.tax > maxrate) {
    surplus = pplayer->economic.tax - maxrate;
    pplayer->economic.tax = maxrate;
    if (pplayer->economic.science >= pplayer->economic.luxury)
      pplayer->economic.science += surplus;
    else
      pplayer->economic.luxury += surplus;
  }
  if (pplayer->economic.science > maxrate) {
    surplus = pplayer->economic.science - maxrate;
    pplayer->economic.science = maxrate;
    if (pplayer->economic.tax >= pplayer->economic.luxury)
      pplayer->economic.tax += surplus;
    else
      pplayer->economic.luxury += surplus;
  }

  return;
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
int player_knows_improvement_tech(struct player *pplayer,
				   Impr_Type_id id)
{
  int t;
  if (!improvement_exists(id)) return 0;
  t = get_improvement_type(id)->tech_requirement;
  return (get_invention(pplayer, t) == TECH_KNOWN);
}

/**************************************************************************
...
**************************************************************************/
int ai_handicap(struct player *pplayer, enum handicap_type htype)
{
  if (!pplayer->ai.control) return -1;
  return(pplayer->ai.handicap&htype);
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
int ai_fuzzy(struct player *pplayer, int normal_decision)
{
  if (!pplayer->ai.control || !pplayer->ai.fuzzy) return normal_decision;
  if (myrand(1000) >= pplayer->ai.fuzzy) return normal_decision;
  return !normal_decision;
}

/**************************************************************************
Command access levels for client-side use; at present, they are 
only used to control access to server commands typed at the client chatline.
**************************************************************************/
static const char *levelnames[] = {
  "none",
  "info",
  "ctrl",
  "hack"
};

const char *cmdlevel_name(enum cmdlevel_id lvl)
{
  assert (lvl >= 0 && lvl < ALLOW_NUM);
  return levelnames[lvl];
}

enum cmdlevel_id cmdlevel_named(const char *token)
{
  enum cmdlevel_id i;
  int len = strlen(token);

  for (i = 0; i < ALLOW_NUM; ++i) {
    if (strncmp(levelnames[i], token, len) == 0) {
      return i;
    }
  }

  return ALLOW_UNRECOGNIZED;
}

/**************************************************************************
...
**************************************************************************/
int is_barbarian(struct player *pplayer)
{
  return (pplayer->ai.is_barbarian > 0);
}
