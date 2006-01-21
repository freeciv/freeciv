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
#include "log.h"
#include "map.h"
#include "mem.h"
#include "player.h"
#include "srv_main.h"
#include "shared.h"
#include "support.h"
#include "terrain.h"
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
  struct tile **edges;
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
      NATIVE_TO_MAP_POS(&x, &y, nat_x,nat_y);      \
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

  nbytes = 2 * map.xsize * map.ysize * sizeof(*pcmap->edges);
  pcmap->edges = fc_malloc(nbytes);

  players_iterate(pplayer) {
    city_list_iterate(pplayer->cities, pcity) {
      map_city_radius_iterate(pcity->tile, tile1) {
	pcmap->claims[tile1->index].cities |= (1u << pcity->owner);
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
  struct tile **nextedge;
  struct claim_cell *pclaim;

  turn = 0;
  nextedge = pcmap->edges;

  whole_map_iterate(ptile) {
    int i = ptile->index;

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
      *nextedge = ptile;
      nextedge++;
      pcmap->player_landarea[owner]++;
      pcmap->player_owndarea[owner]++;
      pclaim->know = ptile->known;
    } else if (ptile->worked) {
      owner = ptile->worked->owner;
      pclaim->when = turn + 1;
      pclaim->whom = owner;
      *nextedge = ptile;
      nextedge++;
      pcmap->player_landarea[owner]++;
      pcmap->player_owndarea[owner]++;
      pclaim->know = ptile->known;
    } else if (unit_list_size(&ptile->units) > 0) {
      owner = (unit_list_get(&ptile->units, 0))->owner;
      pclaim->when = turn + 1;
      pclaim->whom = owner;
      *nextedge = ptile;
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

  *nextedge = NULL;

#if LAND_AREA_DEBUG >= 2
  print_landarea_map(pcmap, turn);
#endif
}

/**************************************************************************
  expand outwards evenly from each starting point, counting tiles
**************************************************************************/
static void build_landarea_map_expand(struct claim_map *pcmap)
{
  struct tile **midedge;
  int turn, accum, other;
  struct tile **thisedge;
  struct tile **nextedge;

  midedge = &pcmap->edges[map.xsize * map.ysize];

  for (accum = 1, turn = 1; accum > 0; turn++) {
    thisedge = ((turn & 0x1) == 1) ? pcmap->edges : midedge;
    nextedge = ((turn & 0x1) == 1) ? midedge : pcmap->edges;

    for (accum = 0; *thisedge; thisedge++) {
      struct tile *ptile = *thisedge;
      int i = ptile->index;
      int owner = pcmap->claims[i].whom;

      if (owner != no_owner) {
	adjc_iterate(ptile, tile1) {
	  int j = tile1->index;
	  struct claim_cell *pclaim = &pcmap->claims[j];

	  if (TEST_BIT(pclaim->know, owner)) {
	    if (pclaim->when == 0) {
	      pclaim->when = turn + 1;
	      pclaim->whom = owner;
	      *nextedge = tile1;
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

    *nextedge = NULL;

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
  Calculates the civilization score for the player.
**************************************************************************/
void calc_civ_score(struct player *pplayer)
{
  struct city *pcity;
  int landarea = 0, settledarea = 0;
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
    return;
  }

  city_list_iterate(pplayer->cities, pcity) {
    int bonus;

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

    bonus = CLIP(0, get_city_bonus(pcity, EFT_SCIENCE_BONUS), 100);
    pplayer->score.literacy += (city_population(pcity) * bonus) / 100;
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
}

/**************************************************************************
  Return the civilization score (a numerical value) for the player.
**************************************************************************/
int get_civ_score(const struct player *pplayer)
{
  /* We used to count pplayer->score.happy here too, but this is too easily
   * manipulated by players at the endyear. */
  return (total_player_citizens(pplayer)
	  + pplayer->score.techs * 2
	  + pplayer->score.wonders * 5
	  + pplayer->score.spaceship);
}

/**************************************************************************
  Return the total number of citizens in the player's nation.
**************************************************************************/
int total_player_citizens(const struct player *pplayer)
{
  return (pplayer->score.happy
	  + pplayer->score.content
	  + pplayer->score.unhappy
	  + pplayer->score.angry
	  + pplayer->score.scientists
	  + pplayer->score.elvis
	  + pplayer->score.taxmen);
}

/**************************************************************************
 save a ppm file which is a representation of the map of the current turn.
 this can later be turned into a animated gif.

 terrain type, units, and cities are saved.
**************************************************************************/
void save_ppm(void)
{
  char filename[600];
  char tmpname[600];
  FILE *fp;
  int i, j;

  /* the colors for each player. these were selected to give
   * the most differentiation between all players. YMMV. */
  int col[MAX_NUM_PLAYERS + MAX_NUM_BARBARIANS][3] = {
    {255,   0,   0}, {  0, 128,   0}, {255, 255, 255}, {255, 255,   0},
    {138,  43, 226}, {255, 140,   0}, {  0, 255, 255}, {139,  69,  19},
    {211, 211, 211}, {255, 215,   0}, {255,  20, 147}, {124, 252,   0},
    {218, 112, 214}, { 30, 144, 255}, {250, 128, 114}, {154, 205,  50},
    { 25,  25, 112}, {  0, 255, 127}, {139,   0,   0}, {100, 149, 237},
    {  0, 128, 128}, {255, 192, 203}, {255, 250, 205}, {119, 136, 153},
    {255, 127,  80}, {255,   0, 255}, {128, 128,   0}, {245, 222, 179},
    {184, 134,  11}, {173, 216, 230}, {102, 205, 170}, {255, 165,   0},
  };
  int watercol[3] = {0,0,255}; /* blue */
  int landcol[3] =  {0,0,0};   /* black */

  if (!srvarg.save_ppm) {
    return;
  }

  /* put this file in the same place we put savegames */
  my_snprintf(filename, sizeof(filename),
              "%s%+05d.int.ppm", game.save_name, game.year);

  /* Ensure the saves directory exists. */
  make_dir(srvarg.saves_pathname);

  sz_strlcpy(tmpname, srvarg.saves_pathname);
  if (tmpname[0] != '\0') {
    sz_strlcat(tmpname, "/");
  }
  sz_strlcat(tmpname, filename);
  sz_strlcpy(filename, tmpname);

  fp = fopen(filename, "w");

  if (!fp) {
    freelog(LOG_ERROR, "couldn't open file ppm save: %s\n", filename);
    return;
  }

  fprintf(fp, "P3\n# version:2\n# gameid: %s\n", game.id);
  fprintf(fp, "# An intermediate map from saved Freeciv game %s%+05d\n",
          game.save_name, game.year);


  for (i = 0; i < game.nplayers; i++) {
    struct player *pplayer = get_player(i);
    fprintf(fp, "# playerno:%d:color:#%02x%02x%02x:name:\"%s\"\n", 
            pplayer->player_no, col[i][0], col[i][1], col[i][2],
            pplayer->name);
  }

  fprintf(fp, "%d %d\n", map.xsize, map.ysize);
  fprintf(fp, "255\n");

  for (j = 0; j < map.ysize; j++) {
    for (i = 0; i < map.xsize; i++) {
       struct tile *ptile = native_pos_to_tile(i, j);
       int *color;

       /* color for cities first, then units, then land */
       if (ptile->city) {
         color = col[city_owner(ptile->city)->player_no];
       } else if (unit_list_size(&ptile->units) > 0) {
         color = col[unit_owner(unit_list_get(&ptile->units, 0))->player_no];
       } else if (is_ocean(ptile->terrain)) {
         color = watercol;
       } else {
         color = landcol;
       }

       fprintf(fp, "%d %d %d\n", color[0], color[1], color[2]);
    }
  }

  fclose(fp);
}
