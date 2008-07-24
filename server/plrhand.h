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

#include <stdarg.h>

#include "shared.h"		/* fc__attribute */
#include "fc_types.h"

#include "events.h"		/* enum event_type */
#include "packets.h"

#include "hand_gen.h"

struct section_file;
struct connection;
struct conn_list;

enum plr_info_level { INFO_MINIMUM, INFO_MEETING, INFO_EMBASSY, INFO_FULL };

void server_player_init(struct player *pplayer,
			bool initmap, bool needs_team);
void server_remove_player(struct player *pplayer);
void kill_player(struct player *pplayer);
void update_revolution(struct player *pplayer);

struct nation_type *pick_a_nation(struct nation_type **choices,
                                  bool ignore_conflicts,
                                  bool only_available,
                                  enum barbarian_type barb_type);

void check_player_max_rates(struct player *pplayer);
void make_contact(struct player *pplayer1, struct player *pplayer2,
		  struct tile *ptile);
void maybe_make_contact(struct tile *ptile, struct player *pplayer);

void send_player_info(struct player *src, struct player *dest);
void send_player_info_c(struct player *src, struct conn_list *dest);

void notify_conn(struct conn_list *dest, struct tile *ptile,
		 enum event_type event, const char *format, ...)
                 fc__attribute((__format__ (__printf__, 4, 5)));
void vnotify_conn(struct conn_list *dest, struct tile *ptile,
		  enum event_type event, const char *format,
		  va_list vargs);
void notify_player(const struct player *pplayer, struct tile *ptile,
		   enum event_type event, const char *format, ...)
                   fc__attribute((__format__ (__printf__, 4, 5)));
void notify_embassies(struct player *pplayer, struct player *exclude,
		      struct tile *ptile, enum event_type event,
		      const char *format, ...)
		      fc__attribute((__format__ (__printf__, 5, 6)));
void notify_team(struct player* pplayer, struct tile *ptile,
                 enum event_type event, const char *format, ...)
                 fc__attribute((__format__ (__printf__, 4, 5)));
void notify_research(struct player *pplayer,
		     enum event_type event, const char *format, ...)
                     fc__attribute((__format__ (__printf__, 3, 4)));

struct conn_list *player_reply_dest(struct player *pplayer);

void send_player_turn_notifications(struct conn_list *dest);

void shuffle_players(void);
void set_shuffled_players(int *shuffled_players);
struct player *shuffled_player(int i);
void reset_all_start_commands(void);

#define shuffled_players_iterate(_p)					\
{									\
  int _p##_index;							\
									\
  for (_p##_index = 0;							\
       _p##_index < player_count();					\
       _p##_index++) {							\
    struct player *_p = shuffled_player(_p##_index);

#define shuffled_players_iterate_end					\
  }									\
}

#define phase_players_iterate(pplayer) \
  shuffled_players_iterate(pplayer) { \
    if (is_player_phase(pplayer, game.info.phase)) {

#define phase_players_iterate_end		\
    }						\
  } shuffled_players_iterate_end

bool civil_war_triggered(struct player *pplayer);
void civil_war(struct player *pplayer);

void update_players_after_alliance_breakup(struct player* pplayer,
                                          struct player* pplayer2);

/* Player counts, total player_count() is in common/player.c */
int barbarian_count(void);
int normal_player_count(void);

#endif  /* FC__PLRHAND_H */
