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
#include "connection.h"
#include "fcintl.h"
#include "government.h"
#include "idex.h"
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

/**************************************************************************
...
**************************************************************************/
static void print_landarea_map(struct claim_map *pcmap, int turn)
{
  int x, y, p;

  if (turn == 0) {
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

/**************************************************************************
allocate and clear claim map; determine city radii
**************************************************************************/
static void build_landarea_map_new(struct claim_map *pcmap)
{
  int nbytes;

  nbytes = map.xsize * map.ysize * sizeof(struct claim_cell);
  pcmap->claims = fc_malloc(nbytes);
  memset (pcmap->claims, 0, nbytes);

  nbytes  = game.nplayers * sizeof(int);
  pcmap->player_landarea = fc_malloc(nbytes);
  memset (pcmap->player_landarea, 0, nbytes);

  nbytes  = game.nplayers * sizeof(int);
  pcmap->player_owndarea = fc_malloc(nbytes);
  memset (pcmap->player_owndarea, 0, nbytes);

  nbytes = 2 * map.xsize * map.ysize * sizeof(struct map_position);
  pcmap->edges = fc_malloc(nbytes);

  players_iterate(pplayer) {
    city_list_iterate(pplayer->cities, pcity) {
      map_city_radius_iterate(pcity->x, pcity->y, x, y) {
	int i = map_inx(x, y);
	pcmap->claims[i].cities |= (1u << pcity->owner);
      } map_city_radius_iterate_end;
    } city_list_iterate_end;
  } players_iterate_end;
}

/**************************************************************************
0th turn: install starting points, counting their tiles
**************************************************************************/
static void build_landarea_map_turn_0(struct claim_map *pcmap)
{
  int turn;
  struct map_position *nextedge;
  struct claim_cell *pclaim;
  struct tile *ptile;
  int owner;

  turn = 0;
  nextedge = pcmap->edges;

  whole_map_iterate(x, y) {
    int i = map_inx(x, y);
    pclaim = &(pcmap->claims[i]);
    ptile = &(map.tiles[i]);

    if (ptile->terrain == T_OCEAN) {
      /* pclaim->when = 0; */
      pclaim->whom = no_owner;
      /* pclaim->know = 0; */
    } else if (ptile->city) {
      owner = ptile->city->owner;
      pclaim->when = turn + 1;
      pclaim->whom = owner;
      nextedge->x = x;
      nextedge->y = y;
      nextedge++;
      (pcmap->player_landarea[owner])++;
      (pcmap->player_owndarea[owner])++;
      pclaim->know = ptile->known;
    } else if (ptile->worked) {
      owner = ptile->worked->owner;
      pclaim->when = turn + 1;
      pclaim->whom = owner;
      nextedge->x = x;
      nextedge->y = y;
      nextedge++;
      (pcmap->player_landarea[owner])++;
      (pcmap->player_owndarea[owner])++;
      pclaim->know = ptile->known;
    } else if (unit_list_size (&(ptile->units)) > 0) {
      owner = (unit_list_get (&(ptile->units), 0))->owner;
      pclaim->when = turn + 1;
      pclaim->whom = owner;
      nextedge->x = x;
      nextedge->y = y;
      nextedge++;
      (pcmap->player_landarea[owner])++;
      if (pclaim->cities & (1u << owner)) {
	(pcmap->player_owndarea[owner])++;
      }
      pclaim->know = ptile->known;
    } else {
      /* pclaim->when = 0; */
      pclaim->whom = no_owner;
      pclaim->know = ptile->known;
    }
  } whole_map_iterate_end;

  nextedge->x = -1;

#if LAND_AREA_DEBUG >= 2
  print_landarea_map (pcmap, turn);
#endif
}


/**************************************************************************
expand outwards evenly from each starting point, counting tiles
**************************************************************************/
static void build_landarea_map_expand(struct claim_map *pcmap)
{
  int x, y, i, j;
  struct map_position *midedge;
  int turn;
  struct map_position *thisedge;
  struct map_position *nextedge;
  int accum;
  struct claim_cell *pclaim;
  int owner, other;

  midedge = &(pcmap->edges[map.xsize * map.ysize]);

  for (accum = 1, turn = 1; accum > 0; turn++) {
    thisedge = (turn & 0x1) ? pcmap->edges : midedge;
    nextedge = (turn & 0x1) ? midedge : pcmap->edges;

    for (accum = 0; thisedge->x >= 0; thisedge++) {
      x = thisedge->x;
      y = thisedge->y;
      i = map_inx (x, y);
      owner = pcmap->claims[i].whom;

      if (owner != no_owner) {
	adjc_iterate(x, y, mx, my) {
	  j = map_inx(mx, my);
	  pclaim = &(pcmap->claims[j]);

	  if (pclaim->know & (1u << owner)) {
	    if (pclaim->when == 0) {
	      pclaim->when = turn + 1;
	      pclaim->whom = owner;
	      nextedge->x = mx;
	      nextedge->y = my;
	      nextedge++;
	      (pcmap->player_landarea[owner])++;
	      if (pclaim->cities & (1u << owner)) {
		(pcmap->player_owndarea[owner])++;
	      }
	      accum++;
	    } else if ((pclaim->when == (turn + 1)) &&
		       (pclaim->whom != no_owner) &&
		       (pclaim->whom != owner)) {
	      other = pclaim->whom;
	      if (pclaim->cities & (1u << other)) {
		(pcmap->player_owndarea[other])--;
	      }
	      (pcmap->player_landarea[other])--;
	      pclaim->whom = no_owner;
	      accum--;
	    }
	  }
	} adjc_iterate_end;
      }
    }

    nextedge->x = -1;

#if LAND_AREA_DEBUG >= 2
    print_landarea_map (pcmap, turn);
#endif
  }
}

/**************************************************************************
this just calls the three worker routines
**************************************************************************/
static void build_landarea_map(struct claim_map *pcmap)
{
  build_landarea_map_new (pcmap);
  build_landarea_map_turn_0 (pcmap);
  build_landarea_map_expand (pcmap);
}

/**************************************************************************
Frees and NULLs an allocated claim map.
**************************************************************************/
static void free_landarea_map(struct claim_map *pcmap)
{
  if (pcmap) {
    if (pcmap->claims) {
      free (pcmap->claims);
      pcmap->claims = NULL;
    }
    if (pcmap->player_landarea) {
      free (pcmap->player_landarea);
      pcmap->player_landarea = NULL;
    }
    if (pcmap->player_owndarea) {
      free (pcmap->player_owndarea);
      pcmap->player_owndarea = NULL;
    }
    if (pcmap->edges) {
      free (pcmap->edges);
      pcmap->edges = NULL;
    }
  }
}

/**************************************************************************
Returns the given player's land and settled areas from a claim map.
**************************************************************************/
static void get_player_landarea(struct claim_map *pcmap, struct player *pplayer,
				 int *return_landarea, int *return_settledarea)
{
  if (pcmap && pplayer) {
#if LAND_AREA_DEBUG >= 1
    printf ("%-14s", pplayer->name);
#endif
    if (return_landarea && pcmap->player_landarea) {
      *return_landarea =
	USER_AREA_MULT * pcmap->player_landarea[pplayer->player_no];
#if LAND_AREA_DEBUG >= 1
      printf (" l=%d", *return_landarea / USER_AREA_MULT);
#endif
    }
    if (return_settledarea && pcmap->player_owndarea) {
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
  return timemod*pplayer->research.researchpoints*game.researchcost;
}

/**************************************************************************
...
**************************************************************************/
int total_player_citizens(struct player *pplayer)
{
  return (pplayer->score.happy
	  +pplayer->score.content
	  +pplayer->score.unhappy
	  +pplayer->score.angry
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
  pplayer->score.angry=0;                       /* done */
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
    pplayer->score.angry+=pcity->ppl_angry[4];
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
  
  for (i=0;i<game.num_impr_types;i++) {
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
If in the server use wipe_unit().
**************************************************************************/
void game_remove_unit(struct unit *punit)
{
  struct city *pcity;

  freelog(LOG_DEBUG, "game_remove_unit %d", punit->id);
  freelog(LOG_DEBUG, "removing unit %d, %s %s (%d %d) hcity %d",
	  punit->id, get_nation_name(unit_owner(punit)->nation),
	  unit_name(punit->type), punit->x, punit->y, punit->homecity);

  pcity = player_find_city_by_id(unit_owner(punit), punit->homecity);
  if (pcity)
    unit_list_unlink(&pcity->units_supported, punit);

  if (pcity) {
    freelog(LOG_DEBUG, "home city %s, %s, (%d %d)", pcity->name,
	    get_nation_name(city_owner(pcity)->nation), pcity->x,
	    pcity->y);
  }

  unit_list_unlink(&map_get_tile(punit->x, punit->y)->units, punit);
  unit_list_unlink(&unit_owner(punit)->units, punit);

  idex_unregister_unit(punit);

  if (is_server)
    dealloc_id(punit->id);
  free(punit);
}

/**************************************************************************
...
**************************************************************************/
void game_remove_city(struct city *pcity)
{
  freelog(LOG_DEBUG, "game_remove_city %d", pcity->id);
  freelog(LOG_DEBUG, "removing city %s, %s, (%d %d)", pcity->name,
	   get_nation_name(city_owner(pcity)->nation), pcity->x, pcity->y);

  city_map_checked_iterate(pcity->x, pcity->y, x, y, mx, my) {
    set_worker_city(pcity, x, y, C_TILE_EMPTY);
  } city_map_checked_iterate_end;
  city_list_unlink(&city_owner(pcity)->cities, pcity);
  map_set_city(pcity->x, pcity->y, NULL);
  idex_unregister_city(pcity);
  free(pcity->worklist);
  free(pcity);
}

/***************************************************************
...
***************************************************************/
void game_init(void)
{
  int i;
  game.is_new_game   = 1;
  game.globalwarming = 0;
  game.warminglevel  = 8;
  game.nuclearwinter = 0;
  game.coolinglevel  = 8;
  game.gold          = GAME_DEFAULT_GOLD;
  game.tech          = GAME_DEFAULT_TECHLEVEL;
  game.skill_level   = GAME_DEFAULT_SKILL_LEVEL;
  game.timeout       = GAME_DEFAULT_TIMEOUT;
  game.tcptimeout    = GAME_DEFAULT_TCPTIMEOUT;
  game.netwait       = GAME_DEFAULT_NETWAIT;
  game.last_ping     = 0;
  game.pingtimeout   = GAME_DEFAULT_PINGTIMEOUT;
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
  game.settlers    = GAME_DEFAULT_SETTLERS;
  game.explorer    = GAME_DEFAULT_EXPLORER;
  game.dispersion  = GAME_DEFAULT_DISPERSION;
  game.cityfactor  = GAME_DEFAULT_CITYFACTOR;
  game.civilwarsize= GAME_DEFAULT_CIVILWARSIZE;
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
  game.fogofwar    = GAME_DEFAULT_FOGOFWAR;
  game.fogofwar_old= game.fogofwar;
  game.auto_ai_toggle = GAME_DEFAULT_AUTO_AI_TOGGLE;
  game.barbarianrate  = GAME_DEFAULT_BARBARIANRATE;
  game.onsetbarbarian = GAME_DEFAULT_ONSETBARBARIAN;
  game.nbarbarians = 0;
  game.occupychance= GAME_DEFAULT_OCCUPYCHANCE;
  game.heating     = 0;
  game.cooling     = 0;
  sz_strlcpy(game.save_name, GAME_DEFAULT_SAVE_NAME);
  game.save_nturns=10;
#ifdef HAVE_LIBZ
  game.save_compress_level = GAME_DEFAULT_COMPRESS_LEVEL;
#else
  game.save_compress_level = GAME_NO_COMPRESS_LEVEL;
#endif
  game.randseed=GAME_DEFAULT_RANDSEED;
  game.watchtower_vision=GAME_DEFAULT_WATCHTOWER_VISION;
  game.watchtower_extra_vision=GAME_DEFAULT_WATCHTOWER_EXTRA_VISION,

  sz_strlcpy(game.ruleset.techs,       GAME_DEFAULT_RULESET);
  sz_strlcpy(game.ruleset.units,       GAME_DEFAULT_RULESET);
  sz_strlcpy(game.ruleset.buildings,   GAME_DEFAULT_RULESET);
  sz_strlcpy(game.ruleset.terrain,     GAME_DEFAULT_RULESET);
  sz_strlcpy(game.ruleset.governments, GAME_DEFAULT_RULESET);
  sz_strlcpy(game.ruleset.nations,     GAME_DEFAULT_RULESET);
  sz_strlcpy(game.ruleset.cities,      GAME_DEFAULT_RULESET);
  sz_strlcpy(game.ruleset.game,        GAME_DEFAULT_RULESET);
  game.firepower_factor = 1;
  game.num_unit_types = 0;
  game.num_impr_types = 0;
  game.num_tech_types = 0;

  game.government_count = 0;
  game.default_government = G_MAGIC;        /* flag */
  game.government_when_anarchy = G_MAGIC;   /* flag */
  game.ai_goal_government = G_MAGIC;        /* flag */

  sz_strlcpy(game.demography, GAME_DEFAULT_DEMOGRAPHY);
  sz_strlcpy(game.allow_connect, GAME_DEFAULT_ALLOW_CONNECT);

  game.save_options.save_random = 1;
  game.save_options.save_players = 1;
  game.save_options.save_known = 1;
  game.save_options.save_starts = 1;
  game.save_options.save_private_map = 1;

  game.load_options.load_random = 1;
  game.load_options.load_players = 1;
  game.load_options.load_known = 1;
  game.load_options.load_starts = 1;
  game.load_options.load_private_map = 1;
  game.load_options.load_settings = 1;
  
  map_init();
  idex_init();
  
  conn_list_init(&game.all_connections);
  conn_list_init(&game.est_connections);
  conn_list_init(&game.game_connections);
  
  for(i=0; i<MAX_NUM_PLAYERS+MAX_NUM_BARBARIANS; i++)
    player_init(&game.players[i]);
  for (i=0; i<A_LAST; i++)      /* game.num_tech_types = 0 here */
    game.global_advances[i]=0;
  for (i=0; i<B_LAST; i++)      /* game.num_impr_types = 0 here */
    game.global_wonders[i]=0;
  game.conn_id = 0;
  game.player_idx=0;
  game.player_ptr=&game.players[0];
  terrain_control.river_help_text = NULL;
}

/***************************************************************
...
***************************************************************/
void initialize_globals(void)
{
  int i;

  players_iterate(plr) {
    city_list_iterate(plr->cities, pcity) {
      for (i=0;i<game.num_impr_types;i++) {
	if (city_got_building(pcity, i) && is_wonder(i))
	  game.global_wonders[i] = pcity->id;
      }
    } city_list_iterate_end;
  } players_iterate_end;
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
      int t = improvement_types[parts[i]].tech_req;
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
  game.turn++;
}


/***************************************************************
...
***************************************************************/
void game_remove_all_players(void)
{
  int i;
  for(i=0; i<game.nplayers; ++i)
    game_remove_player(get_player(i));
  game.nplayers=0;
}


/***************************************************************
...
***************************************************************/
void game_remove_player(struct player *pplayer)
{
  if (pplayer->attribute_block.data != NULL) {
    free(pplayer->attribute_block.data);
    pplayer->attribute_block.data = NULL;
  }

  unit_list_iterate(pplayer->units, punit) 
    game_remove_unit(punit);
  unit_list_iterate_end;

  city_list_iterate(pplayer->cities, pcity) 
    game_remove_city(pcity);
  city_list_iterate_end;

  if (is_barbarian(pplayer)) game.nbarbarians--;
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
    conn_list_iterate(game.players[i].connections, pconn)
      pconn->player = &game.players[i];
    conn_list_iterate_end;
  }

  if(game.player_idx>plrno) {
    game.player_idx--;
    game.player_ptr=&game.players[game.player_idx];
  }

  game.nplayers--;
}

/**************************************************************************
get_player() - Return player struct pointer corresponding to player_id.
               Eg: player_id = punit->owner, or pcity->owner
**************************************************************************/
struct player *get_player(int player_id)
{
    return &game.players[player_id];
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
  static const char too_long_msg[]
    = "Translated name is too long, truncating: %s";

#define name_strlcpy(dst, src) sz_loud_strlcpy(dst, src, too_long_msg)
  
  for (i=0; i<game.num_tech_types; i++) {
    struct advance *tthis = &advances[i];
    sz_strlcpy(tthis->name_orig, tthis->name);
    name_strlcpy(tthis->name, Q_(tthis->name_orig));
  }
  for (i=0; i<game.num_unit_types; i++) {
    struct unit_type *tthis = &unit_types[i];
    sz_strlcpy(tthis->name_orig, tthis->name);
    name_strlcpy(tthis->name, Q_(tthis->name_orig));
  }
  for (i=0; i<game.num_impr_types; i++) {
    struct impr_type *tthis = &improvement_types[i];
    sz_strlcpy(tthis->name_orig, tthis->name);
    name_strlcpy(tthis->name, Q_(tthis->name_orig));
  }
  for (i=T_FIRST; i<T_COUNT; i++) {
    struct tile_type *tthis = &tile_types[i];
    sz_strlcpy(tthis->terrain_name_orig, tthis->terrain_name);
    name_strlcpy(tthis->terrain_name,
		 strcmp(tthis->terrain_name_orig, "") ?
			Q_(tthis->terrain_name_orig) : "");
    sz_strlcpy(tthis->special_1_name_orig, tthis->special_1_name);
    name_strlcpy(tthis->special_1_name,
		 strcmp(tthis->special_1_name_orig, "") ?
			Q_(tthis->special_1_name_orig) : "");
    sz_strlcpy(tthis->special_2_name_orig, tthis->special_2_name);
    name_strlcpy(tthis->special_2_name,
		 strcmp(tthis->special_2_name_orig, "") ?
			Q_(tthis->special_2_name_orig) : "");
  }
  for (i=0; i<game.government_count; i++) {
    int j;
    struct government *tthis = &governments[i];
    sz_strlcpy(tthis->name_orig, tthis->name);
    name_strlcpy(tthis->name, Q_(tthis->name_orig));
    for(j=0; j<tthis->num_ruler_titles; j++) {
      struct ruler_title *that = &tthis->ruler_titles[j];
      sz_strlcpy(that->male_title_orig, that->male_title);
      name_strlcpy(that->male_title, Q_(that->male_title_orig));
      sz_strlcpy(that->female_title_orig, that->female_title);
      name_strlcpy(that->female_title, Q_(that->female_title_orig));
    }
  }
  for (i=0; i<game.nation_count; i++) {
    struct nation_type *tthis = get_nation_by_idx(i);
    sz_strlcpy(tthis->name_orig, tthis->name);
    name_strlcpy(tthis->name, Q_(tthis->name_orig));
    sz_strlcpy(tthis->name_plural_orig, tthis->name_plural);
    name_strlcpy(tthis->name_plural, Q_(tthis->name_plural_orig));
  }
  for (i=0; i<game.styles_count; i++) {
    struct citystyle *tthis = &city_styles[i];
    sz_strlcpy(tthis->name_orig, tthis->name);
    name_strlcpy(tthis->name, Q_(tthis->name_orig));
  }

#undef name_strlcpy

}

/***************************************************************
 Update the improvments effects
***************************************************************/
void update_all_effects(void)
{
  int i;

  freelog(LOG_DEBUG,"update_all_effects");

  players_iterate(pplayer) {
    city_list_iterate(pplayer->cities,pcity) {
      for (i=0;i<game.num_impr_types;i++) {
        if (pcity->improvements[i]==I_NONE) continue;
        if (improvement_obsolete(pplayer,i)) {
          freelog(LOG_DEBUG,"%s in %s is obsolete",
                  improvement_types[i].name,pcity->name);
          mark_improvement(pcity,i,I_OBSOLETE);
        }
      }
    } city_list_iterate_end;
  } players_iterate_end;

  players_iterate(pplayer) {
    city_list_iterate(pplayer->cities,pcity) {
      for (i=0;i<game.num_impr_types;i++) {
        if (pcity->improvements[i]==I_NONE ||
            pcity->improvements[i]==I_OBSOLETE) continue;
        if (improvement_redundant(pplayer,pcity,i,0)) {
          freelog(LOG_DEBUG,"%s in %s is redundant",
                  improvement_types[i].name,pcity->name);
          mark_improvement(pcity,i,I_REDUNDANT);
        } else {
          mark_improvement(pcity,i,I_ACTIVE);
          freelog(LOG_DEBUG,"%s in %s is active!",
                  improvement_types[i].name,pcity->name);
        }
      }
    } city_list_iterate_end;
  } players_iterate_end;
}
