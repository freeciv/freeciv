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

#include "game.h"
#include "improvement.h"
#include "log.h"
#include "map.h"
#include "mem.h"
#include "player.h"
#include "srv_main.h"
#include "shared.h"
#include "score.h"
#include "unit.h"

static int get_civ_score(const struct player *pplayer);

/**************************************************************************
  Allocates, fills and returns a land area claim map.
  Call free_landarea_map(&cmap) to free allocated memory.
**************************************************************************/

#define USER_AREA_MULT 1000

struct claim_map {
  struct {
    int landarea, settledarea;
  } player[MAX_NUM_PLAYERS + MAX_NUM_BARBARIANS];
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
*******************************************o*******************************/
static void print_landarea_map(struct claim_map *pcmap, int turn)
{
  int p;

  if (turn == 0) {
    putchar('\n');
  }

  if (turn == 0) {
    printf("Player Info...\n");

    for (p = 0; p < game.info.nplayers; p++) {
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

/****************************************************************************
  Count landarea, settled area, and claims map for all players.
****************************************************************************/
static void build_landarea_map(struct claim_map *pcmap)
{
  bv_player claims[MAP_INDEX_SIZE];

  memset(claims, 0, sizeof(claims));
  memset(pcmap, 0, sizeof(*pcmap));

  /* First calculate claims: which tiles are owned by each player. */
  players_iterate(pplayer) {
    city_list_iterate(pplayer->cities, pcity) {
      map_city_radius_iterate(pcity->tile, tile1) {
	BV_SET(claims[tile1->index], pcity->owner->player_no);
      } map_city_radius_iterate_end;
    } city_list_iterate_end;
  } players_iterate_end;

  whole_map_iterate(ptile) {
    struct player *owner = NULL;
    bv_player *pclaim = &claims[ptile->index];

    if (is_ocean(ptile->terrain)) {
      /* Nothing. */
    } else if (ptile->city) {
      owner = ptile->city->owner;
      pcmap->player[owner->player_no].settledarea++;
    } else if (ptile->worked) {
      owner = ptile->worked->owner;
      pcmap->player[owner->player_no].settledarea++;
    } else if (unit_list_size(ptile->units) > 0) {
      /* Because of allied stacking these calculations are a bit off. */
      owner = (unit_list_get(ptile->units, 0))->owner;
      if (BV_ISSET(*pclaim, owner->player_no)) {
	pcmap->player[owner->player_no].settledarea++;
      }
    }

    if (game.info.borders > 0) {
      /* If borders are enabled, use owner information directly from the
       * map.  Otherwise use the calculations above. */
      owner = ptile->owner;
    }
    if (owner) {
      pcmap->player[owner->player_no].landarea++;
    }
  } whole_map_iterate_end;

#if LAND_AREA_DEBUG >= 2
  print_landarea_map(pcmap, turn);
#endif
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
    if (return_landarea) {
      *return_landarea
	= USER_AREA_MULT * pcmap->player[pplayer->player_no].landarea;
#if LAND_AREA_DEBUG >= 1
      printf(" l=%d", *return_landarea / USER_AREA_MULT);
#endif
    }
    if (return_settledarea) {
      *return_settledarea
	= USER_AREA_MULT * pcmap->player[pplayer->player_no].settledarea;
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
  static struct claim_map cmap;

  pplayer->score.happy = 0;
  pplayer->score.content = 0;
  pplayer->score.unhappy = 0;
  pplayer->score.angry = 0;
  specialist_type_iterate(sp) {
    pplayer->score.specialists[sp] = 0;
  } specialist_type_iterate_end;
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
    return;
  }

  city_list_iterate(pplayer->cities, pcity) {
    int bonus;

    pplayer->score.happy += pcity->ppl_happy[4];
    pplayer->score.content += pcity->ppl_content[4];
    pplayer->score.unhappy += pcity->ppl_unhappy[4];
    pplayer->score.angry += pcity->ppl_angry[4];
    specialist_type_iterate(sp) {
      pplayer->score.specialists[sp] += pcity->specialists[sp];
    } specialist_type_iterate_end;
    pplayer->score.population += city_population(pcity);
    pplayer->score.cities++;
    pplayer->score.pollution += pcity->pollution;
    pplayer->score.techout += pcity->prod[O_SCIENCE];
    pplayer->score.bnp += pcity->surplus[O_TRADE];
    pplayer->score.mfg += pcity->surplus[O_SHIELD];

    bonus = get_final_city_output_bonus(pcity, O_SCIENCE) - 100;
    bonus = CLIP(0, bonus, 100);
    pplayer->score.literacy += (city_population(pcity) * bonus) / 100;
  } city_list_iterate_end;

  build_landarea_map(&cmap);

  get_player_landarea(&cmap, pplayer, &landarea, &settledarea);
  pplayer->score.landarea = landarea;
  pplayer->score.settledarea = settledarea;

  tech_type_iterate(i) {
    if (i > A_NONE && get_invention(pplayer, i) == TECH_KNOWN) {
      pplayer->score.techs++;
    }
  } tech_type_iterate_end;
  pplayer->score.techs += get_player_research(pplayer)->future_tech * 5 / 2;
  
  unit_list_iterate(pplayer->units, punit) {
    if (is_military_unit(punit)) {
      pplayer->score.units++;
    }
  } unit_list_iterate_end

  impr_type_iterate(i) {
    if (is_great_wonder(i)
	&& (pcity = find_city_from_great_wonder(i))
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

  pplayer->score.game = get_civ_score(pplayer);
}

/**************************************************************************
  Return the civilization score (a numerical value) for the player.
**************************************************************************/
static int get_civ_score(const struct player *pplayer)
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
  int count = (pplayer->score.happy
	       + pplayer->score.content
	       + pplayer->score.unhappy
	       + pplayer->score.angry);

  specialist_type_iterate(sp) {
    count += pplayer->score.specialists[sp];
  } specialist_type_iterate_end;

  return count;
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

  /* put this file in the same place we put savegames */
  my_snprintf(filename, sizeof(filename),
              "%s%+05d.int.ppm", game.save_name, game.info.year);

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

  fprintf(fp, "P3\n# An intermediate map from saved Freeciv game %s%+05d\n",
          game.save_name, game.info.year);

  for (i = 0; i < game.info.nplayers; i++) {
    struct player *pplayer = get_player(i);
    fprintf(fp, "# playerno:%d:race:%d:name:\"%s\"\n", pplayer->player_no,
            pplayer->nation->index, pplayer->name);
  }
  terrain_type_iterate(pterrain) {
    fprintf(fp, "# terrain:%d:name:\"%s\"\n", pterrain->index, pterrain->name);
  } terrain_type_iterate_end;

  fprintf(fp, "%d %d\n", map.xsize, map.ysize);
  fprintf(fp, "255\n");

  for (j = 0; j < map.ysize; j++) {
    for (i = 0; i < map.xsize; i++) {
       struct tile *ptile = native_pos_to_tile(i, j);
       int R, G, B;

       R = (ptile->city) ? city_owner(ptile->city)->nation->index + 1 : 0;
       G = (unit_list_size(ptile->units) > 0) ?
            unit_owner(unit_list_get(ptile->units, 0))->nation->index + 1 : 0;
       B = tile_get_terrain(ptile)->index;

       fprintf(fp, "%d %d %d\n", R, G, B);
    }
  }

  fclose(fp);
}
