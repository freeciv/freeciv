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
#ifndef __PLAYER_H
#define __PLAYER_H

#include "tech.h"
#include "genlist.h"
#include "unit.h"
#include "city.h"
#include "aitools.h"
#define PLAYER_DEFAULT_TAX_RATE 50
#define PLAYER_DEFAULT_SCIENCE_RATE 50
#define PLAYER_DEFAULT_LUXURY_RATE 0
#define TECH_GOALS 10
enum government_type { 
  G_ANARCHY, G_DESPOTISM, G_MONARCHY, G_COMMUNISM, G_REPUBLIC, G_DEMOCRACY,
  G_LAST
};

enum race_type {
  R_ROMAN, R_BABYLONIAN, R_GERMAN, R_EGYPTIAN, R_AMERICAN, R_GREEK, R_INDIAN, 
  R_RUSSIAN, R_ZULU, R_FRENCH, R_AZTEC, R_CHINESE, R_ENGLISH, R_MONGOL, R_LAST
};

enum advisor_type {ADV_ISLAND, ADV_MILITARY, ADV_TRADE, ADV_SCIENCE, ADV_FOREIGN, ADV_ATTITUDE, ADV_DOMESTIC, ADV_LAST};

struct player_race {
  char name[MAX_LENGTH_NAME];
  char name_plural[MAX_LENGTH_NAME];
  int attack;     /* c 0 = optimize for food, 2 =  optimize for prod  */
                  /* c0 = large amount of buildings, 2 = units */
  int expand;    /* c0 = transform first ,  2 = build cities first */
  int civilized; /* c 0 = don't use nukes,  2 = use nukes, lots of pollution */
  int advisors[ADV_LAST];
  struct {
    int tech[TECH_GOALS]; /* Tech goals */
    int wonder;   /* primary Wonder (maybe primary opponent, if other builds it) */
    int government; /* wanted government form */
  } goals;
};

struct player_economic {
  int gold;
  int tax;
  int science;
  int luxury;
};

struct player_research {
  int researched;     /* # bulbs reseached */
  int researchpoints; /* # bulbs to complete */
  int researching;    /* invention being researched in */
  unsigned char inventions[A_LAST];
};

struct player_score {
  int happy;
  int content;
  int unhappy;
  int taxmen;
  int scientists;
  int elvis;
  int wonders;
  int techs;
  int techout;
  int landmass;
  int cities;
  int units;
  int pollution;
  int literacy;
  int bnp;
  int mfg;
};
struct ai_player_island {
  int cities;
  int workremain;
  int settlers;
  struct point harbour;
  struct point wonder;
  struct point cityspot;
};

struct player_ai {
  int control;
  struct ai_player_island island_data[100];
  int tech_goal;
  int prev_gold;
  int maxbuycost;
  int tech_want[A_LAST];
  int tech_turns[A_LAST]; /* saves zillions of calculations! */
};


struct player {
  int player_no;
  char name[MAX_LENGTH_NAME];
  enum government_type government;
  enum race_type race;
  int turn_done;
  int nturns_idle;
  int is_alive;
  int got_tech;
  int revolution;
  int capital; /* bool used to give player capital in first city. */
  int embassy;
  struct unit_list units;
  struct city_list cities;
  struct player_score score;
  struct player_economic economic;
  struct player_research research;
  int future_tech;
  struct player_ai ai;
  int is_connected;
  struct connection *conn;
  char addr[MAX_LENGTH_ADDRESS];
};

void player_init(struct player *plr);
struct player *find_player_by_name(char *name);
void player_set_unit_focus_status(struct player *pplayer);
int player_has_embassy(struct player *pplayer, struct player *pplayer2);

int can_change_to_government(struct player *pplayer, 
			    enum government_type);
char *get_government_name(enum government_type type);
char *get_ruler_title(enum government_type type);
char *get_race_name(enum race_type race);
char *get_race_name_plural(enum race_type race);
struct player_race *get_race(struct player *plr);
int player_can_see_unit(struct player *pplayer, struct unit *punit);
int player_owns_city(struct player *pplayer, struct city *pcity);

extern struct player_race races[];
extern char *default_race_leader_names[];
#endif














