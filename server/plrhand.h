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
#ifndef FC__PLRHAND_H
#define FC__PLRHAND_H

struct section_file;
struct connection;
struct conn_list;

enum plr_info_level { INFO_MINIMUM, INFO_MEETING, INFO_EMBASSY, INFO_FULL };

void server_player_init(struct player *pplayer,
			bool initmap, bool needs_team);
struct player *server_create_player(int player_id);
void server_remove_player(struct player *pplayer);
void kill_player(struct player *pplayer);
void update_revolution(struct player *pplayer);

struct player_economic player_limit_to_max_rates(struct player *pplayer);

void server_player_set_name(struct player *pplayer, const char *name);
bool server_player_set_name_full(const struct connection *caller,
                                 struct player *pplayer,
                                 const struct nation_type *pnation,
                                 const char *name,
                                 char *error_buf, size_t error_buf_len);

struct nation_type *pick_a_nation(const struct nation_list *choices,
                                  bool ignore_conflicts,
                                  bool only_available,
                                  enum barbarian_type barb_type);

void check_player_max_rates(struct player *pplayer);
void make_contact(struct player *pplayer1, struct player *pplayer2,
		  struct tile *ptile);
void maybe_make_contact(struct tile *ptile, struct player *pplayer);
void enter_war(struct player *pplayer, struct player *pplayer2);

void send_player_all_c(struct player *src, struct conn_list *dest);
void send_player_info_c(struct player *src, struct conn_list *dest);
void send_player_diplstate_c(struct player *src, struct conn_list *dest);

struct conn_list *player_reply_dest(struct player *pplayer);

void shuffle_players(void);
void set_shuffled_players(int *shuffled_players);
struct player *shuffled_player(int i);
void reset_all_start_commands(void);

#define shuffled_players_iterate(NAME_pplayer)\
do {\
  int MY_i;\
  struct player *NAME_pplayer;\
  log_debug("shuffled_players_iterate @ %s line %d",\
            __FILE__, __LINE__);\
  for (MY_i = 0; MY_i < player_slot_count(); MY_i++) {\
    NAME_pplayer = shuffled_player(MY_i);\
    if (NAME_pplayer != NULL) {\

#define shuffled_players_iterate_end\
    }\
  }\
} while (0)

#define phase_players_iterate(pplayer)\
do {\
  shuffled_players_iterate(pplayer) {\
    if (is_player_phase(pplayer, game.info.phase)) {

#define phase_players_iterate_end\
    }\
  } shuffled_players_iterate_end;\
} while (0)

bool civil_war_triggered(struct player *pplayer);
void civil_war(struct player *pplayer);

void update_players_after_alliance_breakup(struct player* pplayer,
                                          struct player* pplayer2);

/* Player counts, total player_count() is in common/player.c */
int barbarian_count(void);
int normal_player_count(void);

void player_status_add(struct player *plr, enum player_status status);
bool player_status_check(struct player *plr, enum player_status status);
void player_status_reset(struct player *plr);

#endif  /* FC__PLRHAND_H */
