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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "city.h"
#include "log.h"
#include "map.h"
#include "player.h"
#include "shared.h"
#include "spaceship.h"

#include "game.h"

void dealloc_id(int id);
extern int is_server;
struct civ_game game;

/*
struct player_score {
  int happy;
  int content;
  int unhappy;
  int taxmen;
  int scientists;
  int elvis;
  int wonders;
  int techs;
  int landmass;
  int cities;
  int units;
  int pollution;
  int literacy;
  int bnp;
  int mfg;
  int spaceship;
};


*/


int research_time(struct player *pplayer)
{
  int timemod=(game.year>0) ? 2:1;
  return timemod*pplayer->research.researchpoints*game.techlevel;
}

int total_player_citizens(struct player *pplayer)
{
  return (pplayer->score.happy
	  +pplayer->score.unhappy
	  +pplayer->score.content
	  +pplayer->score.scientists
	  +pplayer->score.elvis
	  +pplayer->score.taxmen);
}

/**************************************************************************
...
**************************************************************************/
int civ_score(struct player *pplayer)
{
  int i;
  struct city *pcity;
  pplayer->score.happy=0;                       /* done */
  pplayer->score.content=0;                     /* done */   
  pplayer->score.unhappy=0;                     /* done */
  pplayer->score.taxmen=0;                      /* done */
  pplayer->score.scientists=0;                  /* done */
  pplayer->score.elvis=0;                       /* done */ 
  pplayer->score.wonders=0;                     /* done */
  pplayer->score.techs=0;                       /* done */
  pplayer->score.techout=0;                     /* done */
  pplayer->score.landmass=0;
  pplayer->score.cities=0;                      /* done */
  pplayer->score.units=0;                       /* done */
  pplayer->score.pollution=0;                   /* done */
  pplayer->score.bnp=0;                         /* done */
  pplayer->score.mfg=0;                         /* done */
  pplayer->score.literacy=0;
  pplayer->score.spaceship=0;
  city_list_iterate(pplayer->cities, pcity) {
    pplayer->score.happy+=pcity->ppl_happy[4];
    pplayer->score.content+=pcity->ppl_content[4];
    pplayer->score.unhappy+=pcity->ppl_unhappy[4];

    pplayer->score.taxmen+=pcity->ppl_taxman;
    pplayer->score.scientists+=pcity->ppl_scientist;
    pplayer->score.elvis+=pcity->ppl_elvis;
    pplayer->score.cities++;
    pplayer->score.pollution+=pcity->pollution;
    pplayer->score.techout+=(1+pcity->science_total);
    pplayer->score.bnp+=pcity->trade_prod;
    pplayer->score.mfg+=pcity->shield_surplus;
    if (city_got_building(pcity, B_UNIVERSITY)) 
      pplayer->score.literacy+=city_population(pcity);
    else if (city_got_building(pcity,B_LIBRARY))
      pplayer->score.literacy+=(city_population(pcity)/2);
  }
  city_list_iterate_end;
  for (i=0;i<A_LAST;i++) 
    if (get_invention(pplayer, i)==TECH_KNOWN) 
      pplayer->score.techs++;
  pplayer->score.techs+=(((pplayer->future_tech)*5)/2);
  
  unit_list_iterate(pplayer->units, punit) 
    if (is_military_unit(punit)) pplayer->score.units++;
  unit_list_iterate_end;
  
  for (i=0;i<B_LAST;i++) {
    if (is_wonder(i) && (pcity=find_city_by_id(game.global_wonders[i])) && 
	player_owns_city(pplayer, pcity))
      pplayer->score.wonders++;
  }

  /* How much should a spaceship be worth??
     This gives 100 points per 10,000 citizens.  --dwp
  */
  if (pplayer->spaceship.state == SSHIP_ARRIVED) {
    pplayer->score.spaceship += (int)(100 * pplayer->spaceship.habitation
				      * pplayer->spaceship.success_rate);
  }
  return (total_player_citizens(pplayer)
	  +pplayer->score.happy
	  +pplayer->score.techs*2
	  +pplayer->score.wonders*5
	  +pplayer->score.spaceship);
}

/**************************************************************************
Count the # of citizen in a civilisation.
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
struct city *game_find_city_by_name(char *name)
{
  int i;
  struct city *pcity;

  for(i=0; i<game.nplayers; i++)
    if((pcity=city_list_find_name(&game.players[i].cities, name)))
      return pcity;

  return 0;
}


/**************************************************************************
...
**************************************************************************/
struct unit *game_find_unit_by_id(int unit_id)
{
  int i;
  struct unit *punit;

  for(i=0; i<game.nplayers; i++)
    if((punit=unit_list_find(&game.players[i].units, unit_id)))  
      return punit;

  return 0;
}



/**************************************************************************
...
**************************************************************************/
void game_remove_unit(int unit_id)
{
  struct unit *punit;

  freelog(LOG_DEBUG, "game_remove_unit %d", unit_id);
  
  if((punit=game_find_unit_by_id(unit_id))) {
    struct city *pcity;

    freelog(LOG_DEBUG, "removing unit %d, %s %s (%d %d) hcity %d",
	   unit_id, get_race_name(get_player(punit->owner)->race),
	   unit_name(punit->type), punit->x, punit->y, punit->homecity);
    
    pcity=player_find_city_by_id(get_player(punit->owner), punit->homecity);
    if(pcity)
      unit_list_unlink(&pcity->units_supported, punit);
    
    if (pcity) {
      freelog(LOG_DEBUG, "home city %s, %s, (%d %d)", pcity->name,
	   get_race_name(city_owner(pcity)->race), pcity->x, pcity->y);
    }

    unit_list_unlink(&map_get_tile(punit->x, punit->y)->units, punit);
    unit_list_unlink(&game.players[punit->owner].units, punit);
    
    free(punit);
    if(is_server)
      dealloc_id(unit_id);
  }
}

/**************************************************************************
...
**************************************************************************/
void game_remove_city(struct city *pcity)
{
  int x,y;
  
  freelog(LOG_DEBUG, "game_remove_city %d", pcity->id);
  freelog(LOG_DEBUG, "removing city %s, %s, (%d %d)", pcity->name,
	   get_race_name(city_owner(pcity)->race), pcity->x, pcity->y);
  
  city_map_iterate(x,y) {
    set_worker_city(pcity, x, y, C_TILE_EMPTY);
  }
  city_list_unlink(&game.players[pcity->owner].cities, pcity);
  map_set_city(pcity->x, pcity->y, NULL);
  free(pcity);
}

/***************************************************************
...
***************************************************************/
void game_init(void)
{
  int i;
  game.globalwarming=0;
  game.warminglevel=8;
  game.gold        = GAME_DEFAULT_GOLD;
  game.tech        = GAME_DEFAULT_TECHLEVEL;
  game.skill_level = GAME_DEFAULT_SKILL_LEVEL;
  game.timeout     = GAME_DEFAULT_TIMEOUT;
  game.end_year    = GAME_DEFAULT_END_YEAR;
  game.year        = GAME_START_YEAR;
  game.min_players = GAME_DEFAULT_MIN_PLAYERS;
  game.max_players = GAME_DEFAULT_MAX_PLAYERS;
  game.aifill      = GAME_DEFAULT_AIFILL;
  game.nplayers=0;
  game.techlevel   = GAME_DEFAULT_RESEARCHLEVEL;
  game.diplcost    = GAME_DEFAULT_DIPLCOST;
  game.diplchance  = GAME_DEFAULT_DIPLCHANCE;
  game.freecost    = GAME_DEFAULT_FREECOST;
  game.conquercost = GAME_DEFAULT_CONQUERCOST;
  game.settlers    = GAME_DEFAULT_SETTLERS;
  game.cityfactor  = GAME_DEFAULT_CITYFACTOR;
  game.civilwarsize= GAME_DEFAULT_CIVILWARSIZE;
  game.explorer    = GAME_DEFAULT_EXPLORER;
  game.unhappysize = GAME_DEFAULT_UNHAPPYSIZE;
  game.rail_trade  = GAME_DEFAULT_RAILTRADE;
  game.rail_food   = GAME_DEFAULT_RAILFOOD;
  game.rail_prod   = GAME_DEFAULT_RAILPROD;
  game.foodbox     = GAME_DEFAULT_FOODBOX;
  game.aqueductloss= GAME_DEFAULT_AQUEDUCTLOSS;
  game.scorelog    = GAME_DEFAULT_SCORELOG;
  game.techpenalty = GAME_DEFAULT_TECHPENALTY;
  game.civstyle    = GAME_DEFAULT_CIVSTYLE;
  game.razechance  = GAME_DEFAULT_RAZECHANCE;
  game.spacerace   = GAME_DEFAULT_SPACERACE;
  game.heating     = 0;
  game.scenario    = 0;
  strcpy(game.save_name, "civgame");
  game.save_nturns=10;
  game.randseed=GAME_DEFAULT_RANDSEED;

  strcpy(game.ruleset.techs, GAME_DEFAULT_RULESET);
  strcpy(game.ruleset.units, GAME_DEFAULT_RULESET);
  strcpy(game.ruleset.buildings, GAME_DEFAULT_RULESET);
  game.firepower_factor = 1;
  
  map_init();
  
  for(i=0; i<MAX_NUM_PLAYERS; i++)
    player_init(&game.players[i]);
  for (i=0; i<A_LAST; i++) 
    game.global_advances[i]=0;
  for (i=0; i<B_LAST; i++)
    game.global_wonders[i]=0;
  game.player_idx=0;
  game.player_ptr=&game.players[0];
}

void initialize_globals(void)
{
  int i,j;
  struct player *plr;
  for (j=0;j<game.nplayers;j++) {
    plr=&game.players[j];
    city_list_iterate(plr->cities, pcity) {
      for (i=0;i<B_LAST;i++) {
	if (city_got_building(pcity, i) && is_wonder(i))
	  game.global_wonders[i]=pcity->id;
      }
    }
    city_list_iterate_end;
  }
}

/***************************************************************
...
***************************************************************/
void game_next_year(void)
{
  int spaceshipparts, i;
  int parts[] = { B_SCOMP, B_SMODULE, B_SSTRUCTURAL, B_LAST };

  if (game.year == 1) /* hacked it to get rid of year 0 */
    game.year = 0;

    /* !McFred: 
       - want game.year += 1 for spaceship.
    */

  /* test game with 7 normal AI's, gen 4 map, foodbox 10, foodbase 0: 
   * Gunpowder about 0 AD
   * Railroad  about 500 AD
   * Electricity about 1000 AD
   * Refining about 1500 AD (212 active units)
   * about 1750 AD
   * about 1900 AD
   */

  spaceshipparts= 0;
  if (game.spacerace) {
    for(i=0; parts[i] < B_LAST; i++) {
      int t = improvement_types[parts[i]].tech_requirement;
      if(tech_exists(t) && game.global_advances[t]) 
	spaceshipparts++;
    }
  }

  if( game.year >= 1900 || ( spaceshipparts>=3 && game.year>0 ) )
    game.year += 1;
  else if( game.year >= 1750 || spaceshipparts>=2 )
    game.year += 2;
  else if( game.year >= 1500 || spaceshipparts>=1 )
    game.year += 5;
  else if( game.year >= 1000 )
    game.year += 10;
  else if( game.year >= 0 )
    game.year += 20;
  else if( game.year >= -1000 ) /* used this line for tuning (was -1250) */
    game.year += 25;
  else
    game.year += 50; 

  if (game.year == 0) 
    game.year = 1;
}

/***************************************************************
...
***************************************************************/
void game_remove_all_players(void)
{
  int i;
  for(i=0; i<game.nplayers; ++i)
    game_remove_player(i);
  game.nplayers=0;
}


/***************************************************************
...
***************************************************************/
void game_remove_player(int plrno)
{
  struct player *pplayer=&game.players[plrno];
  
  unit_list_iterate(pplayer->units, punit) 
    game_remove_unit(punit->id);
  unit_list_iterate_end;

  city_list_iterate(pplayer->cities, pcity) 
    game_remove_city(pcity);
  city_list_iterate_end;
}

/***************************************************************
...
***************************************************************/
void game_renumber_players(int plrno)
{
  int i;
  
  for(i=plrno; i<game.nplayers-1; ++i) {
    game.players[i]=game.players[i+1];
    game.players[i].player_no=i;
    unit_list_iterate(game.players[i].units, punit)
      punit->owner=i;
    unit_list_iterate_end;

    city_list_iterate(game.players[i].cities, pcity) {
      pcity->owner=i;
      pcity->original= (pcity->original<plrno) ? pcity->original : pcity->original-1;
    }
    city_list_iterate_end;
  }
  
  if(game.player_idx>plrno) {
    game.player_idx--;
    game.player_ptr=&game.players[game.player_idx];
  }
  
  game.nplayers--;
  
  for(i=0; i<game.nplayers-1; ++i) {
    game.players[i].embassy=WIPEBIT(game.players[i].embassy, plrno);
  }
  
}

/**************************************************************************
get_player() - Return player struct pointer corresponding to player_id.
               Eg: player_id = punit->owner, or pcity->owner
**************************************************************************/
struct player *get_player(int player_id)
{
    return &game.players[player_id];
}


