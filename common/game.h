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
#ifndef __GAME_H
#define __GAME_H

#include "player.h"

#define GAME_DEFAULT_TIMEOUT 0

enum server_states { 
  PRE_GAME_STATE, 
  SELECT_RACES_STATE, 
  RUN_GAME_STATE,
  GAME_OVER_STATE
};

enum client_states { 
  CLIENT_BOOT_STATE,
  CLIENT_PRE_GAME_STATE,
  CLIENT_SELECT_RACE_STATE,
  CLIENT_WAITING_FOR_GAME_START_STATE,
  CLIENT_GAME_RUNNING_STATE
};

struct unit;
struct city;

struct civ_game {
  int version;
  int civstyle;
  int gold;
  int settlers, explorer;
  int tech;
  int skill_level;
  int timeout;
  int end_year;
  int year;
  int techlevel;
  int diplcost, freecost, conquercost;
  int diplchance;
  int cityfactor;
  int min_players, max_players, nplayers;
  int aifill;
  int unhappysize;
  char *startmessage;
  int player_idx;
  struct player *player_ptr;
  struct player players[MAX_PLAYERS];   
  int global_advances[A_LAST];             /* a counter */
  int global_wonders[B_LAST];              /* contains city id's */
  int globalwarming;                       /* counter of how disturbed 
					      mother nature is */
  int rail_food, rail_trade, rail_prod;
  int heating;
  int warminglevel;
  char save_name[MAX_LENGTH_NAME];
  int save_nturns;
  int scenario;
  int foodbox;
  int techpenalty;
  int razechance;
};

struct lvldat {
  int advspeed;
};

void game_init(void);
void game_next_year(void);


int civ_population(struct player *pplayer);
struct unit *game_find_unit_by_id(int unit_id);
struct city *game_find_city_by_coor(int x, int y);
struct city *game_find_city_by_id(int city_id);
struct city *game_find_city_by_name(char *name);

void game_remove_player(int plrno);
void game_remove_all_players(void);
void game_renumber_players(int plrno);

void game_remove_unit(int unit_id);
void game_remove_city(int city_id);
int research_time(struct player *pplayer);
int total_player_citizens(struct player *pplayer);
int civ_score(struct player *pplayer);
void initialize_globals();
void set_civ_style(int style);

int city_name_compare(const void *first, const void *second);

struct player *get_player(int player_id);

extern struct civ_game game;

#endif
