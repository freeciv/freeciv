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
#include "shared.h"
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
         (player_owns_active_wonder(pplayer, B_MARCO) && pplayer != pplayer2);
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
...
***************************************************************/
void player_init(struct player *plr)
{
  plr->player_no=plr-game.players;

  strcpy(plr->name, "YourName");
  strcpy(plr->username, "UserName");
  plr->is_male = 1;
  plr->government=game.default_government;
  plr->nation=MAX_NUM_NATIONS;
  plr->capital=0;
  unit_list_init(&plr->units);
  city_list_init(&plr->cities);
  strcpy(plr->addr, "---.---.---.---");
  plr->is_alive=1;
  plr->embassy=0;
  plr->ai.control=0;
  plr->ai.tech_goal = A_NONE;
  plr->ai.handicap = 0;
  plr->ai.skill_level = 0;
  plr->ai.fuzzy = 0;
  plr->ai.expand = 100;
  plr->future_tech=0;
  plr->economic.tax=PLAYER_DEFAULT_TAX_RATE;
  plr->economic.science=PLAYER_DEFAULT_SCIENCE_RATE;
  plr->economic.luxury=PLAYER_DEFAULT_LUXURY_RATE;
  spaceship_init(&plr->spaceship);
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
  Return a pointer to a visible unit, if there is one.
***************************************************************/
struct unit *player_find_visible_unit(struct player *pplayer, struct tile *ptile)
{
  if(unit_list_size(&ptile->units)==0) return NULL;

  unit_list_iterate(ptile->units, punit)
    if(player_can_see_unit(pplayer, punit)) return punit;
  unit_list_iterate_end;

  return NULL;
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

/**************************************************************************
 Return 1 if one of the player's cities has the specified wonder,
 and it is not obsolete.
**************************************************************************/
int player_owns_active_wonder(struct player *pplayer,
			      enum improvement_type_id id)
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
  for( i=0; i<A_LAST; i++ ) {
    if((get_invention(pplayer, i) == TECH_KNOWN) && tech_flag(i,flag))
      count++;
  }
  return count;
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
				   enum improvement_type_id id)
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
static char *levelnames[] = {
  "none",
  "info",
  "ctrl",
  "hack"
};

char *cmdlevel_name(enum cmdlevel_id lvl)
{
  assert (lvl >= 0 && lvl < ALLOW_NUM);
  return levelnames[lvl];
}

enum cmdlevel_id cmdlevel_named (char *token)
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
