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

#include <game.h>
#include <player.h>
#include <map.h>
#include <city.h>

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
};


*/


int research_time(struct player *pplayer)
{
  int timemod=(game.year>0) ? 2:1;
  return timemod*pplayer->research.researchpoints*game.techlevel;
}

void set_tech_req(int tech, int req1, int req2)
{
  advances[tech].req[0]=req1;
  advances[tech].req[1]=req2;
}

void set_unit_req(int unit, int req, int obs_by)
{
  unit_types[unit].tech_requirement = req;
  unit_types[unit].obsoleted_by = obs_by;
}

void set_building_req(int building, int req, int obs_by)
{
  improvement_types[building].tech_requirement = req;
  improvement_types[building].obsolete_by = obs_by;
}

void remove_tech(int tech)
{
  set_tech_req(tech, A_LAST, A_LAST);
}

void remove_unit(int unit)
{
  set_unit_req(unit, A_LAST, -1);
}

void remove_building(int building)
{
  set_building_req(building, A_LAST, A_NONE);
}

void set_civ2_style(void)
{
  remove_tech(A_FUNDAMENTALISM);

  remove_unit(U_FANATICS); 

  remove_tech(A_ENVIRONMENTALISM); /* add it and solar plant will be in */

  remove_building(B_SCOMP);
  remove_building(B_SMODULE);
  remove_building(B_SSTRUCTURAL);

  remove_unit(U_CRUSADERS);       /* i don't see the point in combat units */
  remove_unit(U_ELEPHANT);        /* in the peaceful part of the tech tree */
  remove_unit(U_PARATROOPERS);
}

void set_civ1_style(void)
{
  set_civ2_style();
  return; /* this return should be removed when the stuff below is correct */
  remove_tech(A_SEAFARING);
  remove_tech(A_GUERILLA);
  remove_tech(A_ENVIRONMENTALISM);
  remove_tech(A_AMPHIBIOUS);
  remove_tech(A_TACTICS);
  /* etc */
  set_tech_req(A_NAVIGATION, A_MAPMAKING, A_ASTRONOMY);
  /* etc */
  remove_unit(U_ARCHERS);
  /* etc */
  set_unit_req(U_KNIGHTS, A_CHIVALRY, A_AUTOMOBILE);
  /* etc */
  remove_building(B_OFFSHORE);
  /* etc */
  set_building_req(B_MICHELANGELO, A_THEOLOGY, A_COMMUNISM);
  /* etc */
}

/**************************************************************************
...
**************************************************************************/
void set_civ_style(int style)
{
  if (style == 1) 
    set_civ1_style();
  else if (style == 2)
    set_civ2_style();
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
  city_list_iterate(pplayer->cities, pcity) {
    pplayer->score.happy+=pcity->ppl_happy[4];
    pplayer->score.content+=pcity->ppl_content[4];
    pplayer->score.unhappy+=pcity->ppl_unhappy[4];

    pplayer->score.taxmen+=pcity->ppl_taxman;
    pplayer->score.scientists+=pcity->ppl_scientist;
    pplayer->score.elvis=pcity->ppl_elvis;
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
  return (total_player_citizens(pplayer)+pplayer->score.happy+pplayer->score.techs*2+pplayer->score.wonders*5);
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
struct city *game_find_city_by_coor(int x, int y)
{
  int i;
  struct city *pcity;

  for(i=0; i<game.nplayers; i++)
    if((pcity=city_list_find_coor(&game.players[i].cities, x, y)))
      return pcity;

  return 0;
}

/**************************************************************************
...
**************************************************************************/
struct city *game_find_city_by_id(int city_id)
{
  int i;
  struct city *pcity;

  for(i=0; i<game.nplayers; i++)
    if((pcity=city_list_find_id(&game.players[i].cities, city_id)))
      return pcity;

  return 0;
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
  if((punit=game_find_unit_by_id(unit_id))) {
    struct city *pcity;

    pcity=city_list_find_id(&game.players[punit->owner].cities, 
			    punit->homecity);
    if(pcity)
      unit_list_unlink(&pcity->units_supported, punit);

    unit_list_unlink(&map_get_tile(punit->x, punit->y)->units, punit);
    unit_list_unlink(&game.players[punit->owner].units, punit);
    
    free(punit);
  }
}

/**************************************************************************
...
**************************************************************************/
void game_remove_city(int city_id)
{
  struct city *pcity;
  
  if((pcity=game_find_city_by_id(city_id))) {
    city_list_unlink(&game.players[pcity->owner].cities, pcity);
    map_set_city(pcity->x, pcity->y, 0);
    free(pcity);
  }

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
  game.skill_level = 0;
  game.timeout     = GAME_DEFAULT_TIMEOUT;
  game.end_year    = GAME_DEFAULT_END_YEAR;
  game.year        = -4000;
  game.min_players = GAME_DEFAULT_MIN_PLAYERS;
  game.max_players = GAME_DEFAULT_MAX_PLAYERS;
  game.nplayers=0;
  game.techlevel   = GAME_DEFAULT_RESEARCHLEVEL;
  game.diplcost    = GAME_DEFAULT_DIPLCOST;
  game.freecost    = GAME_DEFAULT_FREECOST;
  game.conquercost = GAME_DEFAULT_CONQUERCOST;
  game.settlers    = GAME_DEFAULT_SETTLERS;
  game.cityfactor  = GAME_DEFAULT_CITYFACTOR;
  game.explorer    = GAME_DEFAULT_EXPLORER;
  game.unhappysize = GAME_DEFAULT_UNHAPPYSIZE;
  game.rail_trade  = GAME_DEFAULT_RAILTRADE;
  game.rail_food   = GAME_DEFAULT_RAILFOOD;
  game.rail_prod   = GAME_DEFAULT_RAILPROD;
  game.foodbox     = GAME_DEFAULT_FOODBOX;
  game.techpenalty = GAME_DEFAULT_TECHPENALTY;
  game.civstyle    = GAME_DEFAULT_CIVSTYLE;
  game.razechance  = GAME_DEFAULT_RAZECHANCE;
  game.heating     = 0;
  game.scenario    = 0;
  strcpy(game.save_name, "civgame");
  game.save_nturns=10;
  
  map_init();
  
  for(i=0; i<MAX_PLAYERS; i++)
    player_init(&game.players[i]);
  for (i=0; i<A_LAST; i++) 
    game.global_advances[i]=0;
  for (i=0; i<B_LAST; i++)
    game.global_wonders[i]=0;
  game.player_idx=0;
  game.player_ptr=&game.players[0];
}

void initialize_globals()
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
  if (game.year == 1) /* hacked it to get rid of year 0 */
    game.year = 0;

  if (game.year < 1000)
    game.year += 20;
  else if (game.year < 1500)
    game.year += 10;
  else if (game.year < 1700)
    game.year += 5;
  else if (game.year < 1800)
    game.year += 2;
  else
    game.year++;

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
    game_remove_city(pcity->id);
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
