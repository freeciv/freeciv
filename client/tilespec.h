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

/********************************************************************** 
  Reading and using the tilespec files, which describe
  the files and contents of tilesets.
***********************************************************************/

#ifndef FC__TILESPEC_H
#define FC__TILESPEC_H

#include "map.h"		/* NUM_DIRECTION_NSEW */

struct Sprite;			/* opaque; gui-dep */

void tilespec_read_toplevel(const char *tileset_name);
void tilespec_load_tiles(void);

void tilespec_setup_unit_type(int id);
void tilespec_setup_tile_type(int id);
void tilespec_setup_government(int id);

/* This the way directional indices are now encoded: */

#define BIT_NORTH (0x01)
#define BIT_SOUTH (0x02)
#define BIT_EAST  (0x04)
#define BIT_WEST  (0x08)

#define INDEX_NSEW(n,s,e,w) ((!!(n))*BIT_NORTH | (!!(s))*BIT_SOUTH | \
                             (!!(e))*BIT_EAST  | (!!(w))*BIT_WEST)

#define NUM_TILES_PROGRESS 8
#define NUM_TILES_CITIZEN 9
#define NUM_TILES_HP_BAR 11
#define NUM_TILES_DIGITS 10

enum Directions { DIR_NORTH=0, DIR_SOUTH, DIR_EAST, DIR_WEST };


struct named_sprites {
  struct Sprite
    *bulb[NUM_TILES_PROGRESS],
    *warming[NUM_TILES_PROGRESS],
    *citizen[NUM_TILES_CITIZEN],   /* internal code... */
    *treaty_thumb[2],     /* 0=disagree, 1=agree */
    *right_arrow;
  struct {
    struct Sprite
      *solar_panels,
      *life_support,
      *habitation,
      *structural,
      *fuel,
      *propulsion;
  } spaceship;
  struct {
    struct Sprite
      *isolated,
      *cardinal[NUM_DIRECTION_NSEW],     /* first unused */
      *diagonal[NUM_DIRECTION_NSEW];     /* first unused */
  } road, rail;
  struct {
    struct Sprite *nuke[3][3];	         /* row, column, from top-left */
  } explode;
  struct {
    struct Sprite
      *hp_bar[NUM_TILES_HP_BAR],
      *auto_attack,
      *auto_settler,
      *auto_explore,
      *fortify,
      *fortress,
      *go_to,			/* goto is a C keyword :-) */
      *irrigate,
      *mine,
      *pillage,
      *pollution,
      *road,
      *sentry,
      *stack,
      *transform;
  } unit;
  struct {
    struct Sprite
      *food[2],
      *unhappy[2],
      *shield;
  } upkeep;
  struct {
    struct Sprite
      *occupied,
      *disorder,
      *size[NUM_TILES_DIGITS],
      *size_tens[NUM_TILES_DIGITS],		/* first unused */
      *tile_foodnum[NUM_TILES_DIGITS],
      *tile_shieldnum[NUM_TILES_DIGITS],
      *tile_tradenum[NUM_TILES_DIGITS];
  } city;
  struct {
    struct Sprite *attention;
  } user;
  struct {
    struct Sprite
      *farmland,
      *irrigation,
      *mine,
      *oil_mine,
      *pollution,
      *city,
      *city_walls,
      *village,
      *fortress,
      *spec_river[NUM_DIRECTION_NSEW],
      *coast_cape[NUM_DIRECTION_NSEW],	      /* first unused */
      *darkness[NUM_DIRECTION_NSEW],         /* first unused */
      *river_outlet[4],		/* indexed by enum Directions */
      *denmark[2][3];		/* row, column */
  } tx;				/* terrain extra */
};

extern struct named_sprites sprites;

/* full pathnames: */
extern char *main_intro_filename;
extern char *minimap_intro_filename;

#endif  /* FC__TILESPEC_H */
