/**********************************************************************
 Freeciv - Copyright (C) 2003 - The Freeciv Project
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
#include <string.h>

#include "improvement.h"
#include "map.h"
#include "mem.h"
#include "player.h"
#include "score.h"
#include "unit.h"


/**************************************************************************
  Allocates, fills and returns a land area claim map.
  Call free_landarea_map(&cmap) to free allocated memory.
**************************************************************************/

#define USER_AREA_MULT 1000

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

static char when_char(int when)
{
  static char list[] = {
    '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
    'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J',
    'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T',
    'U', 'V', 'W', 'X', 'Y', 'Z', 'a', 'b', 'c', 'd',
    'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n',
    'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x',
    'y', 'z'
  };

  return (when >= 0 && when < sizeof(list)) ? list[when] : '?';
}

/* 
 * Writes the map_char_expr expression for each position on the map.
 * map_char_expr is provided with the variables x,y to evaluate the
 * position. The 'type' argument is used for formatting by printf; for
 * instance it should be "%c" for characters.  The data is printed in a
 * native orientation to make it easier to read.  
 */
#define WRITE_MAP_DATA(type, map_char_expr)        \
{                                                  \
  int nat_x, nat_y;                                \
  for (nat_x = 0; nat_x < map.xsize; nat_x++) {    \
    printf("%d", nat_x % 10);                      \
  }                                                \
  putchar('\n');                                   \
  for (nat_y = 0; nat_y < map.ysize; nat_y++) {    \
    printf("%d ", nat_y % 10);                     \
    for (nat_x = 0; nat_x < map.xsize; nat_x++) {  \
      int x, y;                                    \
      native_to_map_pos(&x, &y, nat_x,nat_y);      \
      printf(type, map_char_expr);                 \
    }                                              \
    printf(" %d\n", nat_y % 10);                   \
  }                                                \
}

/**************************************************************************
  Prints the landarea map to stdout (a debugging tool).
**************************************************************************/
static void print_landarea_map(struct claim_map *pcmap, int turn)
{
  int p;

  if (turn == 0) {
    putchar('\n');
  }

  if (turn == 0) {
    printf("Player Info...\n");

    for (p = 0; p < game.nplayers; p++) {
      printf(".know (%d)\n  ", p);
      WRITE_MAP_DATA("%c",
		     TEST_BIT(pcmap->claims[map_pos_to_index(x, y)].know,
			      p) ? 'X' : '-');
      printf(".cities (%d)\n  ", p);
      WRITE_MAP_DATA("%c",
		     TEST_BIT(pcmap->
			      claims[map_pos_to_index(x, y)].cities,
			      p) ? 'O' : '-');
    }
  }

  printf("Turn %d (%c)...\n", turn, when_char (turn));

  printf(".whom\n  ");
  WRITE_MAP_DATA((pcmap->claims[map_pos_to_index(x, y)].whom ==
		  32) ? "%c" : "%X",
		 (pcmap->claims[map_pos_to_index(x, y)].whom ==
		  32) ? '-' : pcmap->claims[map_pos_to_index(x, y)].whom);

  printf(".when\n  ");
  WRITE_MAP_DATA("%c", when_char(pcmap->claims[map_pos_to_index(x, y)].when));
}

#endif

static int no_owner = MAX_NUM_PLAYERS + MAX_NUM_BARBARIANS;

/**************************************************************************
  allocate and clear claim map; determine city radii
**************************************************************************/
static void build_landarea_map_new(struct claim_map *pcmap)
{
  int nbytes;

  nbytes = map.xsize * map.ysize * sizeof(struct claim_cell);
  pcmap->claims = fc_malloc(nbytes);
  memset(pcmap->claims, 0, nbytes);

  nbytes = game.nplayers * sizeof(int);
  pcmap->player_landarea = fc_malloc(nbytes);
  memset(pcmap->player_landarea, 0, nbytes);

  nbytes = game.nplayers * sizeof(int);
  pcmap->player_owndarea = fc_malloc(nbytes);
  memset(pcmap->player_owndarea, 0, nbytes);

  nbytes = 2 * map.xsize * map.ysize * sizeof(struct map_position);
  pcmap->edges = fc_malloc(nbytes);

  players_iterate(pplayer) {
    city_list_iterate(pplayer->cities, pcity) {
      map_city_radius_iterate(pcity->x, pcity->y, x, y) {
	int i = map_pos_to_index(x, y);

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
  int turn, owner;
  struct map_position *nextedge;
  struct claim_cell *pclaim;
  struct tile *ptile;

  turn = 0;
  nextedge = pcmap->edges;

  whole_map_iterate(x, y) {
    int i = map_pos_to_index(x, y);

    pclaim = &pcmap->claims[i];
    ptile = &map.tiles[i];

    if (is_ocean(ptile->terrain)) {
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
      pcmap->player_landarea[owner]++;
      pcmap->player_owndarea[owner]++;
      pclaim->know = ptile->known;
    } else if (ptile->worked) {
      owner = ptile->worked->owner;
      pclaim->when = turn + 1;
      pclaim->whom = owner;
      nextedge->x = x;
      nextedge->y = y;
      nextedge++;
      pcmap->player_landarea[owner]++;
      pcmap->player_owndarea[owner]++;
      pclaim->know = ptile->known;
    } else if (unit_list_size(&ptile->units) > 0) {
      owner = (unit_list_get(&ptile->units, 0))->owner;
      pclaim->when = turn + 1;
      pclaim->whom = owner;
      nextedge->x = x;
      nextedge->y = y;
      nextedge++;
      pcmap->player_landarea[owner]++;
      if (TEST_BIT(pclaim->cities, owner)) {
	pcmap->player_owndarea[owner]++;
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
  print_landarea_map(pcmap, turn);
#endif
}

/**************************************************************************
  expand outwards evenly from each starting point, counting tiles
**************************************************************************/
static void build_landarea_map_expand(struct claim_map *pcmap)
{
  struct map_position *midedge;
  int turn, accum, other;
  struct map_position *thisedge;
  struct map_position *nextedge;

  midedge = &pcmap->edges[map.xsize * map.ysize];

  for (accum = 1, turn = 1; accum > 0; turn++) {
    thisedge = ((turn & 0x1) == 1) ? pcmap->edges : midedge;
    nextedge = ((turn & 0x1) == 1) ? midedge : pcmap->edges;

    for (accum = 0; thisedge->x >= 0; thisedge++) {
      int x = thisedge->x;
      int y = thisedge->y;
      int i = map_pos_to_index (x, y);
      int owner = pcmap->claims[i].whom;

      if (owner != no_owner) {
	adjc_iterate(x, y, mx, my) {
	  int j = map_pos_to_index(mx, my);
	  struct claim_cell *pclaim = &pcmap->claims[j];

	  if (TEST_BIT(pclaim->know, owner)) {
	    if (pclaim->when == 0) {
	      pclaim->when = turn + 1;
	      pclaim->whom = owner;
	      nextedge->x = mx;
	      nextedge->y = my;
	      nextedge++;
	      pcmap->player_landarea[owner]++;
	      if (TEST_BIT(pclaim->cities, owner)) {
		pcmap->player_owndarea[owner]++;
	      }
	      accum++;
	    } else if (pclaim->when == turn + 1
		       && pclaim->whom != no_owner
		       && pclaim->whom != owner) {
	      other = pclaim->whom;
	      if (TEST_BIT(pclaim->cities, other)) {
		pcmap->player_owndarea[other]--;
	      }
	      pcmap->player_landarea[other]--;
	      pclaim->whom = no_owner;
	      accum--;
	    }
	  }
	} adjc_iterate_end;
      }
    }

    nextedge->x = -1;

#if LAND_AREA_DEBUG >= 2
    print_landarea_map(pcmap, turn);
#endif
  }
}

/**************************************************************************
  this just calls the three worker routines
**************************************************************************/
static void build_landarea_map(struct claim_map *pcmap)
{
  build_landarea_map_new(pcmap);
  build_landarea_map_turn_0(pcmap);
  build_landarea_map_expand(pcmap);
}

/**************************************************************************
  Frees and NULLs an allocated claim map.
**************************************************************************/
static void free_landarea_map(struct claim_map *pcmap)
{
  if (pcmap) {
    if (pcmap->claims) {
      free(pcmap->claims);
      pcmap->claims = NULL;
    }
    if (pcmap->player_landarea) {
      free(pcmap->player_landarea);
      pcmap->player_landarea = NULL;
    }
    if (pcmap->player_owndarea) {
      free(pcmap->player_owndarea);
      pcmap->player_owndarea = NULL;
    }
    if (pcmap->edges) {
      free(pcmap->edges);
      pcmap->edges = NULL;
    }
  }
}

/**************************************************************************
  Returns the given player's land and settled areas from a claim map.
**************************************************************************/
static void get_player_landarea(struct claim_map *pcmap,
				struct player *pplayer,
				int *return_landarea,
				int *return_settledarea)
{
  if (pcmap && pplayer) {
#if LAND_AREA_DEBUG >= 1
    printf("%-14s", pplayer->name);
#endif
    if (return_landarea && pcmap->player_landarea) {
      *return_landarea =
	USER_AREA_MULT * pcmap->player_landarea[pplayer->player_no];
#if LAND_AREA_DEBUG >= 1
      printf(" l=%d", *return_landarea / USER_AREA_MULT);
#endif
    }
    if (return_settledarea && pcmap->player_owndarea) {
      *return_settledarea =
	USER_AREA_MULT * pcmap->player_owndarea[pplayer->player_no];
#if LAND_AREA_DEBUG >= 1
      printf(" s=%d", *return_settledarea / USER_AREA_MULT);
#endif
    }
#if LAND_AREA_DEBUG >= 1
    printf("\n");
#endif
  }
}

/**************************************************************************
  Return the civilization score (a numerical value) for the player.
**************************************************************************/
int civ_score(struct player *pplayer)
{
  struct city *pcity;
  int landarea, settledarea;
  static struct claim_map cmap = { NULL, NULL, NULL,NULL };

  pplayer->score.happy = 0;
  pplayer->score.content = 0;
  pplayer->score.unhappy = 0;
  pplayer->score.angry = 0;
  pplayer->score.taxmen = 0;
  pplayer->score.scientists = 0;
  pplayer->score.elvis = 0;
  pplayer->score.wonders = 0;
  pplayer->score.techs = 0;
  pplayer->score.techout = 0;
  pplayer->score.landarea = 0;
  pplayer->score.settledarea = 0;
  pplayer->score.population = 0;
  pplayer->score.cities = 0;
  pplayer->score.units = 0;
  pplayer->score.pollution = 0;
  pplayer->score.bnp = 0;
  pplayer->score.mfg = 0;
  pplayer->score.literacy = 0;
  pplayer->score.spaceship = 0;

  if (is_barbarian(pplayer)) {
    if (pplayer->player_no == game.nplayers - 1) {
      free_landarea_map(&cmap);
    }
    return 0;
  }

  city_list_iterate(pplayer->cities, pcity) {
    pplayer->score.happy += pcity->ppl_happy[4];
    pplayer->score.content += pcity->ppl_content[4];
    pplayer->score.unhappy += pcity->ppl_unhappy[4];
    pplayer->score.angry += pcity->ppl_angry[4];
    pplayer->score.taxmen += pcity->specialists[SP_TAXMAN];
    pplayer->score.scientists += pcity->specialists[SP_SCIENTIST];
    pplayer->score.elvis += pcity->specialists[SP_ELVIS];
    pplayer->score.population += city_population(pcity);
    pplayer->score.cities++;
    pplayer->score.pollution += pcity->pollution;
    pplayer->score.techout += pcity->science_total;
    pplayer->score.bnp += pcity->trade_prod;
    pplayer->score.mfg += pcity->shield_surplus;
    if (city_got_building(pcity, B_UNIVERSITY)) {
      pplayer->score.literacy += city_population(pcity);
    } else if (city_got_building(pcity,B_LIBRARY)) {
      pplayer->score.literacy += city_population(pcity) / 2;
    }
  } city_list_iterate_end;

  if (pplayer->player_no == 0) {
    free_landarea_map(&cmap);
    build_landarea_map(&cmap);
  }
  get_player_landarea(&cmap, pplayer, &landarea, &settledarea);
  pplayer->score.landarea = landarea;
  pplayer->score.settledarea = settledarea;
  if (pplayer->player_no == game.nplayers - 1) {
    free_landarea_map(&cmap);
  }

  tech_type_iterate(i) {
    if (get_invention(pplayer, i)==TECH_KNOWN) {
      pplayer->score.techs++;
    }
  } tech_type_iterate_end;
  pplayer->score.techs += pplayer->future_tech * 5 / 2;
  
  unit_list_iterate(pplayer->units, punit) {
    if (is_military_unit(punit)) {
      pplayer->score.units++;
    }
  } unit_list_iterate_end

  impr_type_iterate(i) {
    if (is_wonder(i)
	&& (pcity = find_city_by_id(game.global_wonders[i]))
	&& player_owns_city(pplayer, pcity)) {
      pplayer->score.wonders++;
    }
  } impr_type_iterate_end;

  /* How much should a spaceship be worth?
   * This gives 100 points per 10,000 citizens. */
  if (pplayer->spaceship.state == SSHIP_ARRIVED) {
    pplayer->score.spaceship += (int)(100 * pplayer->spaceship.habitation
				      * pplayer->spaceship.success_rate);
  }

  /* We used to count pplayer->score.happy here too, but this is too easily
   * manipulated by players at the endyear. */
  return (total_player_citizens(pplayer)
	  + pplayer->score.techs * 2
	  + pplayer->score.wonders * 5
	  + pplayer->score.spaceship);
}
