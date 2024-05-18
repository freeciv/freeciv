/***********************************************************************
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
#ifndef FC__MAP_TYPES_H
#define FC__MAP_TYPES_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* utility */
#include "randseed.h"

/* common */
#include "fc_types.h"

/*****************************************************************
  Miscellaneous terrain information
*****************************************************************/
#define terrain_misc packet_ruleset_terrain_control

/* Some types used below. */
struct nation_hash;
struct nation_type;
struct packet_edit_startpos_full;
struct startpos;
struct startpos_hash;

enum mapsize_type {
  MAPSIZE_FULLSIZE = 0, /* Using the number of tiles / 1000. */
  MAPSIZE_PLAYER,       /* Define the number of (land) tiles per player;
                         * the setting 'landmass' and the number of players
                         * are used to calculate the map size. */
  MAPSIZE_XYSIZE        /* 'xsize' and 'ysize' are defined. */
};

enum map_generator {
  MAPGEN_SCENARIO = 0,
  MAPGEN_RANDOM,
  MAPGEN_FRACTAL,
  MAPGEN_ISLAND,
  MAPGEN_FAIR,
  MAPGEN_FRACTURE
};

enum map_startpos {
  MAPSTARTPOS_DEFAULT = 0,      /* Generator's choice. */
  MAPSTARTPOS_SINGLE,           /* One player per continent. */
  MAPSTARTPOS_2or3,             /* Two on three players per continent. */
  MAPSTARTPOS_ALL,              /* All players on a single continent. */
  MAPSTARTPOS_VARIABLE,         /* Depending on size of continents. */
};

#define SPECENUM_NAME team_placement
#define SPECENUM_VALUE0 TEAM_PLACEMENT_DISABLED
#define SPECENUM_VALUE1 TEAM_PLACEMENT_CLOSEST
#define SPECENUM_VALUE2 TEAM_PLACEMENT_CONTINENT
#define SPECENUM_VALUE3 TEAM_PLACEMENT_HORIZONTAL
#define SPECENUM_VALUE4 TEAM_PLACEMENT_VERTICAL
#include "specenum_gen.h"

struct civ_map {
  int topology_id;
  int wrap_id;
  bool altitude_info;
  enum direction8 valid_dirs[8], cardinal_dirs[8];
  int num_valid_dirs, num_cardinal_dirs;
  struct iter_index *iterate_outwards_indices;
  int num_iterate_outwards_indices;
  int xsize, ysize;   /* Native dimensions */
  int north_latitude;
  int south_latitude;

  int num_continents;
  int num_oceans;
  /* These arrays are indexed by continent number (or negative of the
   * ocean number) so the 0th element is unused and the array is 1 element
   * larger than you'd expect.
   *
   * The _sizes arrays give the sizes (in tiles) of each continent and
   * ocean.
   *
   * The lake_surrounders array tells which single continent surrounds each
   * ocean; or -1 if there's more than one adjacent continent.
   */
  int *continent_sizes;
  int *ocean_sizes;
  Continent_id *lake_surrounders;   /* Not updated at the client */

  struct tile *tiles;
  struct startpos_hash *startpos_table;

  union {
    struct {
      enum mapsize_type mapsize; /* How the map size is defined */
      int size;                  /* Used to calculate [xy]size */
      int tilesperplayer;        /* Tiles per player; used to calculate size */
      randseed seed_setting;
      randseed seed;
      int riches;
      int huts;
      int huts_absolute; /* For compatibility conversion from pre-2.6 savegames */
      int animals;
      int landpercent;
      enum map_generator generator;
      enum map_startpos startpos;
      bool tinyisles;
      bool separatepoles;
      int flatpoles;
      int temperature;
      int wetness;
      int steepness;
      bool ocean_resources;         /* Resources in the middle of the ocean */
      bool have_huts;
      bool have_resources;
      enum team_placement team_placement;
    } server;

    struct {
      /* These arrays count how many adjacencies there are between known
       * tiles of a given continent or ocean and unknown tiles, i.e. if a
       * single known tile is adjacent to multiple unknowns (or vice versa)
       * it gets counted multiple times.
       *
       * If this is 0 for a continent or ocean, we know for sure that its
       * size in continent/ocean_sizes is accurate. */
      int *continent_unknown_adj_counts;
      int *ocean_unknown_adj_counts;
    } client;
  };
};

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* FC__MAP_H */
