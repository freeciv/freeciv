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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "city.h"
#include "fcintl.h"
#include "government.h"
#include "log.h"
#include "map.h"
#include "mem.h"
#include "nation.h"
#include "player.h"
#include "shared.h"
#include "spaceship.h"
#include "support.h"
#include "tech.h"
#include "unit.h"

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

#define USER_AREA_MULT (1000)

struct claim_cell {
  int when;
  int whom;
  int know;
  int cities;
};

struct claim_map {
  struct claim_cell *claims;
  int *player_landarea;
  int *player_owndarea;
  struct map_position *edges;
};

/**************************************************************************
Land Area Debug...
**************************************************************************/

#define LAND_AREA_DEBUG 0

#if LAND_AREA_DEBUG >= 2

static char when_char (int when)
{
  static char list[] =
  {
    '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
    'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J',
    'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T',
    'U', 'V', 'W', 'X', 'Y', 'Z', 'a', 'b', 'c', 'd',
    'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n',
    'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x',
    'y', 'z'
  };

  return (((when >= 0) && (when < sizeof (list))) ? list[when] : '?');
}

static void print_landarea_map (struct claim_map *pcmap, int turn)
{
  int x, y, p;

  if (turn == 0)
    {
      putchar ('\n');
    }

  if (turn == 0)
    {
      printf ("Player Info...\n");

      for (p = 0; p < game.nplayers; p++)
	{
	  printf (".know (%d)\n  ", p);
	  for (x = 0; x < map.xsize; x++)
	    {
	      printf ("%d", x % 10);
	    }
	  putchar ('\n');
	  for (y = 0; y < map.ysize; y++)
	    {
	      printf ("%d ", y % 10);
	      for (x = 0; x < map.xsize; x++)
		{
		  printf ("%c",
			  (pcmap->claims[map_inx(x,y)].know & (1u << p)) ?
			  'X' :
			  '-');
		}
	      printf (" %d\n", y % 10);
	    }

	  printf (".cities (%d)\n  ", p);
	  for (x = 0; x < map.xsize; x++)
	    {
	      printf ("%d", x % 10);
	    }
	  putchar ('\n');
	  for (y = 0; y < map.ysize; y++)
	    {
	      printf ("%d ", y % 10);
	      for (x = 0; x < map.xsize; x++)
		{
		  printf ("%c",
			  (pcmap->claims[map_inx(x,y)].cities & (1u << p)) ?
			  'O' :
			  '-');
		}
	      printf (" %d\n", y % 10);
	    }
	}
    }

  printf ("Turn %d (%c)...\n", turn, when_char (turn));

  printf (".whom\n  ");
  for (x = 0; x < map.xsize; x++)
    {
      printf ("%d", x % 10);
    }
  putchar ('\n');
  for (y = 0; y < map.ysize; y++)
    {
      printf ("%d ", y % 10);
      for (x = 0; x < map.xsize; x++)
	{
	  printf ("%X", pcmap->claims[map_inx(x,y)].whom);
	}
      printf (" %d\n", y % 10);
    }

  printf (".when\n  ");
  for (x = 0; x < map.xsize; x++)
    {
      printf ("%d", x % 10);
    }
  putchar ('\n');
  for (y = 0; y < map.ysize; y++)
    {
      printf ("%d ", y % 10);
      for (x = 0; x < map.xsize; x++)
	{
	  printf ("%c", when_char (pcmap->claims[map_inx(x,y)].when));
	}
      printf (" %d\n", y % 10);
    }
}

#endif

/**************************************************************************
Allocates, fills and returns a land area claim map.
Call free_landarea_map(&cmap) to free allocated memory.
**************************************************************************/

static int no_owner = MAX_NUM_PLAYERS + MAX_NUM_BARBARIANS;

/* allocate and clear claim map; determine city radii */

static void build_landarea_map_new (struct claim_map *pcmap)
{
  int nbytes;
  int n, i;
  int cx, cy, mx, my;
  int halfcity = (CITY_MAP_SIZE / 2);

  nbytes = map.xsize * map.ysize * sizeof (struct claim_cell);
  pcmap->claims = fc_malloc (nbytes);
  memset (pcmap->claims, 0, nbytes);

  nbytes  = game.nplayers * sizeof (int);
  pcmap->player_landarea = fc_malloc (nbytes);
  memset (pcmap->player_landarea, 0, nbytes);

  nbytes  = game.nplayers * sizeof (int);
  pcmap->player_owndarea = fc_malloc (nbytes);
  memset (pcmap->player_owndarea, 0, nbytes);

  nbytes = 2 * map.xsize * map.ysize * sizeof (struct map_position);
  pcmap->edges = fc_malloc (nbytes);

  for (n = 0; n < game.nplayers; n++)
    {
      city_list_iterate (game.players[n].cities, pcity) {
	city_map_iterate (cx, cy) {
	  my = pcity->y + cy - halfcity;
	  if ((my >= 0) && (my < map.ysize))
	    {
	      mx = map_adjust_x (pcity->x + cx - halfcity);
	      i = map_inx (mx, my);
	      pcmap->claims[i].cities |= (1u << pcity->owner);
	    }
	}
      }
      city_list_iterate_end;
    }
}

/* 0th turn: install starting points, counting their tiles */

static void build_landarea_map_turn_0 (struct claim_map *pcmap)
{
  int x, y, i;
  int turn;
  struct map_position *nextedge;
  struct claim_cell *pclaim;
  struct tile *ptile;
  int owner;

  turn = 0;
  nextedge = pcmap->edges;

  for (y = 0; y < map.ysize; y++)
    {
      for (x = 0, i = y * map.xsize; x < map.xsize; x++, i++)
	{
	  pclaim = &(pcmap->claims[i]);
	  ptile = &(map.tiles[i]);

	  if (ptile->terrain == T_OCEAN)
	    {
	      /* pclaim->when = 0; */
	      pclaim->whom = no_owner;
	      /* pclaim->know = 0; */
	    }
	  else if (ptile->city)
	    {
	      owner = ptile->city->owner;
	      pclaim->when = turn + 1;
	      pclaim->whom = owner;
	      nextedge->x = x;
	      nextedge->y = y;
	      nextedge++;
	      (pcmap->player_landarea[owner])++;
	      (pcmap->player_owndarea[owner])++;
	      pclaim->know = ptile->known;
	    }
	  else if (ptile->worked)
	    {
	      owner = ptile->worked->owner;
	      pclaim->when = turn + 1;
	      pclaim->whom = owner;
	      nextedge->x = x;
	      nextedge->y = y;
	      nextedge++;
	      (pcmap->player_landarea[owner])++;
	      (pcmap->player_owndarea[owner])++;
	      pclaim->know = ptile->known;
	    }
	  else if (unit_list_size (&(ptile->units)) > 0)
	    {
	      owner = (unit_list_get (&(ptile->units), 0))->owner;
	      pclaim->when = turn + 1;
	      pclaim->whom = owner;
	      nextedge->x = x;
	      nextedge->y = y;
	      nextedge++;
	      (pcmap->player_landarea[owner])++;
	      if (pclaim->cities & (1u << owner))
		{
		  (pcmap->player_owndarea[owner])++;
		}
	      pclaim->know = ptile->known;
	    }
	  else
	    {
	      /* pclaim->when = 0; */
	      pclaim->whom = no_owner;
	      pclaim->know = ptile->known;
	    }
	}
    }

  nextedge->x = -1;

#if LAND_AREA_DEBUG >= 2
  print_landarea_map (pcmap, turn);
#endif
}

/* expand outwards evenly from each starting point, counting tiles */

static void build_landarea_map_expand (struct claim_map *pcmap)
{
  int x, y, n, i, j, mx, my;
  struct map_position *midedge;
  int turn;
  struct map_position *thisedge;
  struct map_position *nextedge;
  int accum;
  struct claim_cell *pclaim;
  int owner, other;
  static int nx[8] = { -1,  0,  1, -1,  1, -1,  0,  1 };
  static int ny[8] = { -1, -1, -1,  0,  0,  1,  1,  1 };

  midedge = &(pcmap->edges[map.xsize * map.ysize]);

  for (accum = 1, turn = 1; accum > 0; turn++)
    {
      thisedge = (turn & 0x1) ? pcmap->edges : midedge;
      nextedge = (turn & 0x1) ? midedge : pcmap->edges;

      for (accum = 0; thisedge->x >= 0; thisedge++)
	{
	  x = thisedge->x;
	  y = thisedge->y;
	  i = map_inx (x, y);
	  owner = pcmap->claims[i].whom;

	  if (owner != no_owner)
	    {
	      for (n = 0; n < 8; n++)
		{
		  my = y + ny[n];
		  if ((my < 0) || (my >= map.ysize))
		    {
		      continue;
		    }
		  mx = map_adjust_x (x + nx[n]);
		  j = map_inx (mx, my);
		  pclaim = &(pcmap->claims[j]);

		  if (pclaim->know & (1u << owner))
		    {
		      if (pclaim->when == 0)
			{
			  pclaim->when = turn + 1;
			  pclaim->whom = owner;
			  nextedge->x = mx;
			  nextedge->y = my;
			  nextedge++;
			  (pcmap->player_landarea[owner])++;
			  if (pclaim->cities & (1u << owner))
			    {
			      (pcmap->player_owndarea[owner])++;
			    }
			  accum++;
			}
		      else if ((pclaim->when == (turn + 1)) &&
			       (pclaim->whom != no_owner) &&
			       (pclaim->whom != owner))
			{
			  other = pclaim->whom;
			  if (pclaim->cities & (1u << other))
			    {
			      (pcmap->player_owndarea[other])--;
			    }
			  (pcmap->player_landarea[other])--;
			  pclaim->whom = no_owner;
			  accum--;
			}
		    }
		}
	    }
	}

      nextedge->x = -1;

#if LAND_AREA_DEBUG >= 2
      print_landarea_map (pcmap, turn);
#endif
    }
}

/* this just calls the three worker routines */

static void build_landarea_map (struct claim_map *pcmap)
{
  build_landarea_map_new (pcmap);
  build_landarea_map_turn_0 (pcmap);
  build_landarea_map_expand (pcmap);
}

/**************************************************************************
Frees and NULLs an allocated claim map.
**************************************************************************/

static void free_landarea_map (struct claim_map *pcmap)
{
  if (pcmap)
    {
      if (pcmap->claims)
	{
	  free (pcmap->claims);
	  pcmap->claims = NULL;
	}
      if (pcmap->player_landarea)
	{
	  free (pcmap->player_landarea);
	  pcmap->player_landarea = NULL;
	}
      if (pcmap->player_owndarea)
	{
	  free (pcmap->player_owndarea);
	  pcmap->player_owndarea = NULL;
	}
      if (pcmap->edges)
	{
	  free (pcmap->edges);
	  pcmap->edges = NULL;
	}
    }
}

/**************************************************************************
Returns the given player's land and settled areas from a claim map.
**************************************************************************/

static void get_player_landarea (struct claim_map *pcmap, struct player *pplayer,
				 int *return_landarea, int *return_settledarea)
{
  if (pcmap && pplayer)
    {
#if LAND_AREA_DEBUG >= 1
      printf ("%-14s", pplayer->name);
#endif
      if (return_landarea && pcmap->player_landarea)
	{
	  *return_landarea =
	    USER_AREA_MULT * pcmap->player_landarea[pplayer->player_no];
#if LAND_AREA_DEBUG >= 1
	  printf (" l=%d", *return_landarea / USER_AREA_MULT);
#endif
	}
      if (return_settledarea && pcmap->player_owndarea)
	{
	  *return_settledarea =
	    USER_AREA_MULT * pcmap->player_owndarea[pplayer->player_no];
#if LAND_AREA_DEBUG >= 1
	  printf (" s=%d", *return_settledarea / USER_AREA_MULT);
#endif
	}
#if LAND_AREA_DEBUG >= 1
      printf ("\n");
#endif
    }
}

/**************************************************************************
...
**************************************************************************/

int research_time(struct player *pplayer)
{
  int timemod=(game.year>0) ? 2:1;
  return timemod*pplayer->research.researchpoints*game.techlevel;
}

/**************************************************************************
...
**************************************************************************/

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
  int landarea, settledarea;
  static struct claim_map cmap = { NULL, NULL, NULL };

  pplayer->score.happy=0;                       /* done */
  pplayer->score.content=0;                     /* done */   
  pplayer->score.unhappy=0;                     /* done */
  pplayer->score.taxmen=0;                      /* done */
  pplayer->score.scientists=0;                  /* done */
  pplayer->score.elvis=0;                       /* done */ 
  pplayer->score.wonders=0;                     /* done */
  pplayer->score.techs=0;                       /* done */
  pplayer->score.techout=0;                     /* done */
  pplayer->score.landarea=0;
  pplayer->score.settledarea=0;
  pplayer->score.population=0;
  pplayer->score.cities=0;                      /* done */
  pplayer->score.units=0;                       /* done */
  pplayer->score.pollution=0;                   /* done */
  pplayer->score.bnp=0;                         /* done */
  pplayer->score.mfg=0;                         /* done */
  pplayer->score.literacy=0;
  pplayer->score.spaceship=0;

  if (is_barbarian(pplayer)) {
    if (pplayer->player_no == (game.nplayers - 1)) {
      free_landarea_map(&cmap);
    }
    return 0;
  }

  city_list_iterate(pplayer->cities, pcity) {
    pplayer->score.happy+=pcity->ppl_happy[4];
    pplayer->score.content+=pcity->ppl_content[4];
    pplayer->score.unhappy+=pcity->ppl_unhappy[4];
    pplayer->score.taxmen+=pcity->ppl_taxman;
    pplayer->score.scientists+=pcity->ppl_scientist;
    pplayer->score.elvis+=pcity->ppl_elvis;
    pplayer->score.population+=city_population(pcity);
    pplayer->score.cities++;
    pplayer->score.pollution+=pcity->pollution;
    pplayer->score.techout+=pcity->science_total;
#ifdef CITIES_PROVIDE_RESEARCH
    (pplayer->score.techout)++;
#endif
    pplayer->score.bnp+=pcity->trade_prod;
    pplayer->score.mfg+=pcity->shield_surplus;
    if (city_got_building(pcity, B_UNIVERSITY)) 
      pplayer->score.literacy+=city_population(pcity);
    else if (city_got_building(pcity,B_LIBRARY))
      pplayer->score.literacy+=(city_population(pcity)/2);
  }
  city_list_iterate_end;

  if (pplayer->player_no == 0) {
    free_landarea_map(&cmap);
    build_landarea_map(&cmap);
  }
  get_player_landarea(&cmap, pplayer, &landarea, &settledarea);
  pplayer->score.landarea=landarea;
  pplayer->score.settledarea=settledarea;
  if (pplayer->player_no == (game.nplayers - 1)) {
    free_landarea_map(&cmap);
  }

  for (i=0;i<game.num_tech_types;i++) 
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
If called from server use the wrapper void server_remove_unit(punit);
**************************************************************************/
void game_remove_unit(int unit_id)
{
  struct unit *punit;

  freelog(LOG_DEBUG, "game_remove_unit %d", unit_id);
  
  if((punit=game_find_unit_by_id(unit_id))) {
    struct city *pcity;

    freelog(LOG_DEBUG, "removing unit %d, %s %s (%d %d) hcity %d",
	   unit_id, get_nation_name(get_player(punit->owner)->nation),
	   unit_name(punit->type), punit->x, punit->y, punit->homecity);

    pcity=player_find_city_by_id(get_player(punit->owner), punit->homecity);
    if(pcity)
      unit_list_unlink(&pcity->units_supported, punit);
    
    if (pcity) {
      freelog(LOG_DEBUG, "home city %s, %s, (%d %d)", pcity->name,
	   get_nation_name(city_owner(pcity)->nation), pcity->x, pcity->y);
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
	   get_nation_name(city_owner(pcity)->nation), pcity->x, pcity->y);
  
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
  game.is_new_game = 1;
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
  game.foodbox     = GAME_DEFAULT_FOODBOX;
  game.aqueductloss= GAME_DEFAULT_AQUEDUCTLOSS;
  game.killcitizen = GAME_DEFAULT_KILLCITIZEN;
  game.scorelog    = GAME_DEFAULT_SCORELOG;
  game.techpenalty = GAME_DEFAULT_TECHPENALTY;
  game.civstyle    = GAME_DEFAULT_CIVSTYLE;
  game.razechance  = GAME_DEFAULT_RAZECHANCE;
  game.spacerace   = GAME_DEFAULT_SPACERACE;
  game.fogofwar    = GAME_DEFAULT_FOGOFWAR;
  game.fogofwar_old= game.fogofwar;
  game.barbarianrate  = GAME_DEFAULT_BARBARIANRATE;
  game.onsetbarbarian = GAME_DEFAULT_ONSETBARBARIAN;
  game.occupychance= GAME_DEFAULT_OCCUPYCHANCE;
  game.heating     = 0;
  sz_strlcpy(game.save_name, "civgame");
  game.save_nturns=10;
  game.randseed=GAME_DEFAULT_RANDSEED;

  sz_strlcpy(game.ruleset.techs,       GAME_DEFAULT_RULESET);
  sz_strlcpy(game.ruleset.units,       GAME_DEFAULT_RULESET);
  sz_strlcpy(game.ruleset.buildings,   GAME_DEFAULT_RULESET);
  sz_strlcpy(game.ruleset.terrain,     GAME_DEFAULT_RULESET);
  sz_strlcpy(game.ruleset.governments, GAME_DEFAULT_RULESET);
  sz_strlcpy(game.ruleset.nations,     GAME_DEFAULT_RULESET);
  sz_strlcpy(game.ruleset.cities,      GAME_DEFAULT_RULESET);
  game.firepower_factor = 1;
  game.num_unit_types = 0;
  game.num_tech_types = 0;

  game.government_count = 0;
  game.default_government = G_MAGIC;        /* flag */
  game.government_when_anarchy = G_MAGIC;   /* flag */
  game.ai_goal_government = G_MAGIC;        /* flag */

  sz_strlcpy(game.demography, GAME_DEFAULT_DEMOGRAPHY);

  map_init();
  
  for(i=0; i<MAX_NUM_PLAYERS+MAX_NUM_BARBARIANS; i++)
    player_init(&game.players[i]);
  for (i=0; i<A_LAST; i++)      /* game.num_tech_types = 0 here */
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
  Returns the next year in the game.
***************************************************************/
int game_next_year(int year)
{
  int spaceshipparts, i;
  int parts[] = { B_SCOMP, B_SMODULE, B_SSTRUCTURAL, B_LAST };

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

  spaceshipparts= 0;
  if (game.spacerace) {
    for(i=0; parts[i] < B_LAST; i++) {
      int t = improvement_types[parts[i]].tech_requirement;
      if(tech_exists(t) && game.global_advances[t]) 
	spaceshipparts++;
    }
  }

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


/***************************************************************
  For various data, copy eg .name to .name_orig and put
  translated version in .name
  (These could be in the separate modules, but since they are
  all almost the same, and all needed together, it seems a bit
  easier to just do them all here.)
***************************************************************/
void translate_data_names(void)
{
  int i, j;
  
  for (i=0; i<game.num_tech_types; i++) {
    struct advance *this = &advances[i];
    sz_strlcpy(this->name_orig, this->name);
    sz_strlcpy(this->name, Q_(this->name_orig));
  }
  for (i=0; i<game.num_unit_types; i++) {
    struct unit_type *this = &unit_types[i];
    sz_strlcpy(this->name_orig, this->name);
    sz_strlcpy(this->name, Q_(this->name_orig));
  }
  for (i=0; i<B_LAST; i++) {
    struct improvement_type *this = &improvement_types[i];
    sz_strlcpy(this->name_orig, this->name);
    sz_strlcpy(this->name, Q_(this->name_orig));
  }
  for (i=T_FIRST; i<T_COUNT; i++) {
    struct tile_type *this = &tile_types[i];
    sz_strlcpy(this->terrain_name_orig, this->terrain_name);
    sz_strlcpy(this->terrain_name, Q_(this->terrain_name_orig));
    sz_strlcpy(this->special_1_name_orig, this->special_1_name);
    sz_strlcpy(this->special_1_name, Q_(this->special_1_name_orig));
    sz_strlcpy(this->special_2_name_orig, this->special_2_name);
    sz_strlcpy(this->special_2_name, Q_(this->special_2_name_orig));
  }
  for (i=0; i<game.government_count; i++) {
    struct government *this = &governments[i];
    sz_strlcpy(this->name_orig, this->name);
    sz_strlcpy(this->name, Q_(this->name_orig));
    for(j=0; j<this->num_ruler_titles; j++) {
      struct ruler_title *that = &this->ruler_titles[j];
      sz_strlcpy(that->male_title_orig, that->male_title);
      sz_strlcpy(that->male_title, Q_(that->male_title_orig));
      sz_strlcpy(that->female_title_orig, that->female_title);
      sz_strlcpy(that->female_title, Q_(that->female_title_orig));
    }
  }
  for (i=0; i<game.nation_count; i++) {
    struct nation_type *this = get_nation_by_idx(i);
    sz_strlcpy(this->name_orig, this->name);
    sz_strlcpy(this->name, Q_(this->name_orig));
    sz_strlcpy(this->name_plural_orig, this->name_plural);
    sz_strlcpy(this->name_plural, Q_(this->name_plural_orig));
  }
  for (i=0; i<game.styles_count; i++) {
    struct citystyle *this = &city_styles[i];
    sz_strlcpy(this->name_orig, this->name);
    sz_strlcpy(this->name, Q_(this->name_orig));
  }
}
