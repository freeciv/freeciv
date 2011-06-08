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
#ifndef FC__AI_H
#define FC__AI_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* common */
#include "fc_types.h" /* MAX_LEN_NAME */

/* Update this capability string when ever there is changes to ai_type
   structure below */
#define FC_AI_MOD_CAPSTR "+Freeciv-ai-module-2011.Jun.08"

#define FC_AI_LAST 3

struct Treaty;
struct player;
struct ai_choice;
struct city;
struct unit;
struct tile;
struct settlermap;
struct pf_path;
struct section_file;
struct tech_vector;

enum incident_type {
  INCIDENT_DIPLOMAT = 0, INCIDENT_WAR, INCIDENT_PILLAGE,
  INCIDENT_NUCLEAR, INCIDENT_NUCLEAR_NOT_TARGET,
  INCIDENT_NUCLEAR_SELF, INCIDENT_LAST
};

enum danger_consideration { DANG_UNDECIDED, DANG_NOT, DANG_YES };

struct ai_type
{
  char name[MAX_LEN_NAME];

  struct {
    void (*game_free)(void);

    void (*player_alloc)(struct player *pplayer);
    void (*player_free)(struct player *pplayer);
    void (*player_save)(struct player *pplayer, struct section_file *file,
                        int plrno);
    void (*player_load)(struct player *pplayer, struct section_file *file,
                        int plrno);
    void (*gained_control)(struct player *pplayer);
    void (*lost_control)(struct player *pplayer);
    void (*split_by_civil_war)(struct player *pplayer);

    void (*phase_begin)(struct player *pplayer, bool new_phase);
    void (*phase_finished)(struct player *pplayer);

    void (*city_alloc)(struct city *pcity);
    void (*city_free)(struct city *pcity);
    void (*city_got)(struct player *pplayer, struct city *pcity);
    void (*city_lost)(struct player *pplayer, struct city *pcity);
    void (*city_save)(struct section_file *file, const struct city *pcity,
                      const char *citystr);
    void (*city_load)(const struct section_file *file, struct city *pcity,
                      const char *citystr);
    void (*choose_building)(struct city *pcity, struct ai_choice *choice);
    void (*build_adv_adjust_want)(struct player *pplayer, struct city *wonder_city);

    void (*units_ruleset_init)(void);

    void (*unit_alloc)(struct unit *punit);
    void (*unit_free)(struct unit *punit);
    void (*unit_got)(struct unit *punit);
    void (*unit_lost)(struct unit *punit);
    void (*unit_created)(struct unit *punit);
    void (*unit_turn_end)(struct unit *punit);
    void (*unit_move)(struct unit *punit, struct tile *ptile,
                      struct pf_path *path, int step);
    void (*unit_task)(struct unit *punit, enum adv_unit_task task,
                      struct tile *ptile);
    void (*unit_save)(struct section_file *file, const struct unit *punit,
                      const char *unitstr);
    void (*unit_load)(const struct section_file *file, struct unit *punit,
                      const char *unitstr);

    void (*settler_reset)(struct player *pplayer);
    void (*settler_run)(struct player *pplayer, struct unit *punit,
                        struct settlermap *state);

    void (*first_activities)(struct player *pplayer);
    void (*diplomacy_actions)(struct player *pplayer);
    void (*last_activities)(struct player *pplayer);
    void (*treaty_evaluate)(struct player *pplayer, struct player *aplayer,
                            struct Treaty *ptreaty);
    void (*treaty_accepted)(struct player *pplayer, struct player *aplayer,
                            struct Treaty *ptreaty);
    void (*first_contact)(struct player *pplayer, struct player *aplayer);
    void (*incident)(enum incident_type type, struct player *violator,
                     struct player *victim);

    void (*log_fragment_city)(char *buffer, int buflength, const struct city *pcity);
    void (*log_fragment_unit)(char *buffer, int buflength, const struct unit *punit);

    void (*consider_plr_dangerous)(struct player *plr1, struct player *plr2,
                                   enum danger_consideration *result);
    void (*consider_tile_dangerous)(struct tile *ptile, struct unit *punit,
                                    enum danger_consideration *result);
  } funcs;
};

struct ai_type *ai_type_alloc(void);
struct ai_type *get_ai_type(int id);
int ai_type_number(const struct ai_type *ai);
void init_ai(struct ai_type *ai);

struct ai_type *ai_type_by_name(const char *search);


#define ai_type_iterate(NAME_ai)                        \
  do {                                                  \
    int _aii_;                                          \
    for (_aii_ = 0; _aii_ < FC_AI_LAST; _aii_++) {      \
      struct ai_type *NAME_ai = get_ai_type(_aii_);

#define ai_type_iterate_end \
    }                       \
  } while (FALSE);

/* FIXME: This should also check if player is ai controlled */
#define CALL_PLR_AI_FUNC(_func, _player, ...)                           \
  do {                                                                  \
    struct player *_plr_ = _player; /* _player expanded just once */    \
    if (_plr_ && _plr_->ai && _plr_->ai->funcs._func) {                 \
      _plr_->ai->funcs._func( __VA_ARGS__ );                            \
    }                                                                   \
  } while (FALSE)

#define CALL_FUNC_EACH_AI(_func, ...)           \
  do {                                          \
    ai_type_iterate(_ait_) {                    \
      if (_ait_->funcs._func) {                 \
        _ait_->funcs._func( __VA_ARGS__ );      \
      }                                         \
    } ai_type_iterate_end;                      \
  } while (FALSE)

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif  /* FC__AI_H */
