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
#ifndef FC__CLIENT_ATTRIBUTE_H
#define FC__CLIENT_ATTRIBUTE_H

/*
 * If 4 byte wide signed int is used this gives 20 object types with
 * 100 million keys each.
 */
enum attr_object_type_start_keys {
  ATTR_UNIT_START = 0 * 100 * 1000 * 1000,
  ATTR_CITY_START = 1 * 100 * 1000 * 1000,
  ATTR_PLAYER_START = 2 * 100 * 1000 * 1000,
  ATTR_TILE_START = 3 * 100 * 1000 * 1000
};

enum attr_unit {
  ATTR_UNIT_DUMMY = ATTR_UNIT_START,
};

enum attr_city {
  ATTR_CITY_DUMMY = ATTR_CITY_START,
};

enum attr_player {
    ATTR_PLAYER_DUMMY = ATTR_PLAYER_START
};

enum attr_tile {
    ATTR_TILE_DUMMY = ATTR_TILE_START,
};

/*
 * Generic methods.
 */
void attribute_init(void);

/*
 * Send current state to server.
 */
void attribute_flush(void);

/*
 * Recreate the attribute set from the player's attribute_block.
 */
void attribute_restore(void);

/* 
 * If data_length is zero the attribute is removed.
 */
void attribute_set(int key, int id, int x, int y, int data_length,
		   const void *const data);
/* 
 * If data hasn't enough space to hold the attribute attribute_get
 * aborts. Returns the actual size of the attribute. Can be zero if
 * the attribute is unset. 
 */
int attribute_get(int key, int id, int x, int y, int max_data_length,
		  void *data);

/*
 * Special methods for units.
 */
void attr_unit_set(enum attr_unit what, int unit_id, int data_length,
		   const void *const data);
int attr_unit_get(enum attr_unit what, int unit_id, int max_data_length,
		   void *data);
void attr_unit_set_int(enum attr_unit what, int unit_id, int data);
int attr_unit_get_int(enum attr_unit what, int unit_id, int *data);


/*
 * Special methods for cities.
 */
void attr_city_set(enum attr_city what, int city_id, int data_length,
		   const void *const data);
int attr_city_get(enum attr_city what, int city_id, int max_data_length,
		   void *data);
void attr_city_set_int(enum attr_city what, int city_id, int data);
int attr_city_get_int(enum attr_city what, int city_id, int *data);

/*
 * Special methods for players.
 */
void attr_player_set(enum attr_player what, int player_id, int data_length,
		     const void *const data);
int attr_player_get(enum attr_player what, int player_id,
		    int max_data_length, void *data);

/*
 * Special methods for tiles.
 */
void attr_tile_set(enum attr_tile what, int x, int y, int data_length,
		   const void *const data);
int attr_tile_get(enum attr_tile what, int x, int y, int max_data_length,
		  void *data);
#endif
