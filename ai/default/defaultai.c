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

#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

/* common */
#include "ai.h"
#include "player.h"

/* server */
#include "aiiface.h"

/* server/advisors */
#include "advdata.h"
#include "autosettlers.h"

/* ai */
#include "advdiplomacy.h"
#include "advdomestic.h"
#include "advmilitary.h"
#include "aicity.h"
#include "aidata.h"
#include "aiferry.h"
#include "aihand.h"
#include "ailog.h"
#include "aiplayer.h"
#include "aisettler.h"
#include "aitools.h"
#include "aiunit.h"

#include "defaultai.h"

/* This function is only needed as symbol in module. Nevertheless
   gcc requires that there is prior prototype. */
const char *fc_ai_classic_capstr(void);

/**************************************************************************
  Return module capability string
**************************************************************************/
const char *fc_ai_classic_capstr(void)
{
  return FC_AI_MOD_CAPSTR;
}

/**************************************************************************
  Call default ai with classic ai type as parameter.
**************************************************************************/
static void cai_player_alloc(struct player *pplayer)
{
  struct ai_type *deftype = default_ai_get_self();

  dai_player_alloc(deftype, pplayer);
}

/**************************************************************************
  Call default ai with classic ai type as parameter.
**************************************************************************/
static void cai_player_free(struct player *pplayer)
{
  struct ai_type *deftype = default_ai_get_self();

  dai_player_free(deftype, pplayer);
}

/**************************************************************************
  Call default ai with classic ai type as parameter.
**************************************************************************/
static void cai_player_save(struct player *pplayer, struct section_file *file,
                     int plrno)
{
  struct ai_type *deftype = default_ai_get_self();

  dai_player_save(deftype, pplayer, file, plrno);
}

/**************************************************************************
  Call default ai with classic ai type as parameter.
**************************************************************************/
static void cai_player_load(struct player *pplayer, struct section_file *file,
                            int plrno)
{
  struct ai_type *deftype = default_ai_get_self();

  dai_player_load(deftype, pplayer, file, plrno);
}

/**************************************************************************
  Call default ai with classic ai type as parameter.
**************************************************************************/
static void cai_assess_danger_player(struct player *pplayer)
{
  struct ai_type *deftype = default_ai_get_self();

  dai_assess_danger_player(deftype, pplayer);
}

/**************************************************************************
  Call default ai with classic ai type as parameter.
**************************************************************************/
static void cai_data_phase_begin(struct player *pplayer, bool is_new_phase)
{
  struct ai_type *deftype = default_ai_get_self();

  dai_data_phase_begin(deftype, pplayer, is_new_phase);
}

/**************************************************************************
  Call default ai with classic ai type as parameter.
**************************************************************************/
static void cai_data_phase_finished(struct player *pplayer)
{
  struct ai_type *deftype = default_ai_get_self();

  dai_data_phase_finished(deftype, pplayer);
}

/**************************************************************************
  Call default ai with classic ai type as parameter.
**************************************************************************/
static void cai_city_alloc(struct city *pcity)
{
  struct ai_type *deftype = default_ai_get_self();

  dai_city_alloc(deftype, pcity);
}

/**************************************************************************
  Call default ai with classic ai type as parameter.
**************************************************************************/
static void cai_city_free(struct city *pcity)
{
  struct ai_type *deftype = default_ai_get_self();

  dai_city_free(deftype, pcity);
}

/**************************************************************************
  Call default ai with classic ai type as parameter.
**************************************************************************/
static void cai_city_save(struct section_file *file, const struct city *pcity,
                          const char *citystr)
{
  struct ai_type *deftype = default_ai_get_self();

  dai_city_save(deftype, file, pcity, citystr);
}

/**************************************************************************
  Call default ai with classic ai type as parameter.
**************************************************************************/
static void cai_city_load(const struct section_file *file, struct city *pcity,
                          const char *citystr)
{
  struct ai_type *deftype = default_ai_get_self();

  dai_city_load(deftype, file, pcity, citystr);
}

/**************************************************************************
  Call default ai with classic ai type as parameter.
**************************************************************************/
static void cai_build_adv_override(struct city *pcity, struct adv_choice *choice)
{
  struct ai_type *deftype = default_ai_get_self();

  dai_build_adv_override(deftype, pcity, choice);
}

/**************************************************************************
  Call default ai with classic ai type as parameter.
**************************************************************************/
static void cai_wonder_city_distance(struct player *pplayer, struct adv_data *adv)
{
  struct ai_type *deftype = default_ai_get_self();

  dai_wonder_city_distance(deftype, pplayer, adv);
}

/**************************************************************************
  Call default ai with classic ai type as parameter.
**************************************************************************/
static void cai_build_adv_adjust(struct player *pplayer, struct city *wonder_city)
{
  struct ai_type *deftype = default_ai_get_self();

  dai_build_adv_adjust(deftype, pplayer, wonder_city);
}

/**************************************************************************
  Call default ai with classic ai type as parameter.
**************************************************************************/
static void cai_units_ruleset_init(void)
{
  struct ai_type *deftype = default_ai_get_self();

  dai_units_ruleset_init(deftype);
}

/**************************************************************************
  Call default ai with classic ai type as parameter.
**************************************************************************/
static void cai_unit_init(struct unit *punit)
{
  struct ai_type *deftype = default_ai_get_self();

  dai_unit_init(deftype, punit);
}

/**************************************************************************
  Call default ai with classic ai type as parameter.
**************************************************************************/
static void cai_unit_close(struct unit *punit)
{
  struct ai_type *deftype = default_ai_get_self();

  dai_unit_close(deftype, punit);
}

/**************************************************************************
  Call default ai with classic ai type as parameter.
**************************************************************************/
static void cai_ferry_init_ferry(struct unit *ferry)
{
  struct ai_type *deftype = default_ai_get_self();

  dai_ferry_init_ferry(deftype, ferry);
}

/**************************************************************************
  Call default ai with classic ai type as parameter.
**************************************************************************/
static void cai_unit_turn_end(struct unit *punit)
{
  struct ai_type *deftype = default_ai_get_self();

  dai_unit_turn_end(deftype, punit);
}

/**************************************************************************
  Call default ai with classic ai type as parameter.
**************************************************************************/
static void cai_unit_move_or_attack(struct unit *punit, struct tile *ptile,
                                    struct pf_path *path, int step)
{
  struct ai_type *deftype = default_ai_get_self();

  dai_unit_move_or_attack(deftype, punit, ptile, path, step);
}

/**************************************************************************
  Call default ai with classic ai type as parameter.
**************************************************************************/
static void cai_unit_new_adv_task(struct unit *punit, enum adv_unit_task task,
                                  struct tile *ptile)
{
  struct ai_type *deftype = default_ai_get_self();

  dai_unit_new_adv_task(deftype, punit, task, ptile);
}

/**************************************************************************
  Call default ai with classic ai type as parameter.
**************************************************************************/
static void cai_unit_save(struct section_file *file, const struct unit *punit,
                          const char *unitstr)
{
  struct ai_type *deftype = default_ai_get_self();

  dai_unit_save(deftype, file, punit, unitstr);
}

/**************************************************************************
  Call default ai with classic ai type as parameter.
**************************************************************************/
static void cai_unit_load(const struct section_file *file, struct unit *punit,
                          const char *unitstr)
{
  struct ai_type *deftype = default_ai_get_self();

  dai_unit_load(deftype, file, punit, unitstr);
}

/**************************************************************************
  Call default ai with classic ai type as parameter.
**************************************************************************/
static void cai_auto_settler_reset(struct player *pplayer)
{
  struct ai_type *deftype = default_ai_get_self();

  dai_auto_settler_reset(deftype, pplayer);
}

/**************************************************************************
  Call default ai with classic ai type as parameter.
**************************************************************************/
static void cai_auto_settler_run(struct player *pplayer, struct unit *punit,
                                 struct settlermap *state)
{
  struct ai_type *deftype = default_ai_get_self();

  dai_auto_settler_run(deftype, pplayer, punit, state);
}

/**************************************************************************
  Call default ai with classic ai type as parameter.
**************************************************************************/
static void cai_do_first_activities(struct player *pplayer)
{
  struct ai_type *deftype = default_ai_get_self();

  dai_do_first_activities(deftype, pplayer);
}

/**************************************************************************
  Call default ai with classic ai type as parameter.
**************************************************************************/
static void cai_diplomacy_actions(struct player *pplayer)
{
  struct ai_type *deftype = default_ai_get_self();

  dai_diplomacy_actions(deftype, pplayer);
}

/**************************************************************************
  Call default ai with classic ai type as parameter.
**************************************************************************/
static void cai_do_last_activities(struct player *pplayer)
{
  struct ai_type *deftype = default_ai_get_self();

  dai_do_last_activities(deftype, pplayer);
}

/**************************************************************************
  Call default ai with classic ai type as parameter.
**************************************************************************/
static void cai_treaty_evaluate(struct player *pplayer, struct player *aplayer,
                                struct Treaty *ptreaty)
{
  struct ai_type *deftype = default_ai_get_self();

  dai_treaty_evaluate(deftype, pplayer, aplayer, ptreaty);
}

/**************************************************************************
  Call default ai with classic ai type as parameter.
**************************************************************************/
static void cai_treaty_accepted(struct player *pplayer, struct player *aplayer, 
                                struct Treaty *ptreaty)
{
  struct ai_type *deftype = default_ai_get_self();

  dai_treaty_accepted(deftype, pplayer, aplayer, ptreaty);
}

/**************************************************************************
  Call default ai with classic ai type as parameter.
**************************************************************************/
static void cai_diplomacy_first_contact(struct player *pplayer,
                                        struct player *aplayer)
{
  struct ai_type *deftype = default_ai_get_self();

  dai_diplomacy_first_contact(deftype, pplayer, aplayer);
}

/**************************************************************************
  Call default ai with classic ai type as parameter.
**************************************************************************/
static void cai_incident(enum incident_type type, struct player *violator,
                         struct player *victim)
{
  struct ai_type *deftype = default_ai_get_self();

  dai_incident(deftype, type, violator, victim);
}

/**************************************************************************
  Call default ai with classic ai type as parameter.
**************************************************************************/
static void cai_city_log(char *buffer, int buflength, const struct city *pcity)
{
  struct ai_type *deftype = default_ai_get_self();

  dai_city_log(deftype, buffer, buflength, pcity);
}

/**************************************************************************
  Call default ai with classic ai type as parameter.
**************************************************************************/
static void cai_unit_log(char *buffer, int buflength, const struct unit *punit)
{
  struct ai_type *deftype = default_ai_get_self();

  dai_unit_log(deftype, buffer, buflength, punit);
}

/**************************************************************************
  Call default ai with classic ai type as parameter.
**************************************************************************/
static void cai_consider_plr_dangerous(struct player *plr1, struct player *plr2,
                                       enum danger_consideration *result)
{
  struct ai_type *deftype = default_ai_get_self();

  dai_consider_plr_dangerous(deftype, plr1, plr2, result);
}

/**************************************************************************
  Call default ai with classic ai type as parameter.
**************************************************************************/
static void cai_consider_tile_dangerous(struct tile *ptile, struct unit *punit,
                                        enum danger_consideration *result)
{
  struct ai_type *deftype = default_ai_get_self();

  dai_consider_tile_dangerous(deftype, ptile, punit, result);
}

/**************************************************************************
  Call default ai with classic ai type as parameter.
**************************************************************************/
static void cai_consider_wonder_city(struct city *pcity, bool *result)
{
  struct ai_type *deftype = default_ai_get_self();

  dai_consider_wonder_city(deftype, pcity, result);
}

/**************************************************************************
  Setup player ai_funcs function pointers.
**************************************************************************/
bool fc_ai_classic_setup(struct ai_type *ai)
{
  default_ai_set_self(ai);

  strncpy(ai->name, "classic", sizeof(ai->name));

  /* ai->funcs.game_free = NULL; */

  ai->funcs.player_alloc = cai_player_alloc;
  ai->funcs.player_free = cai_player_free;
  ai->funcs.player_save = cai_player_save;
  ai->funcs.player_load = cai_player_load;
  ai->funcs.gained_control = cai_assess_danger_player;
  /* ai->funcs.lost_control = NULL; */
  ai->funcs.split_by_civil_war = cai_assess_danger_player;

  ai->funcs.phase_begin = cai_data_phase_begin;
  ai->funcs.phase_finished = cai_data_phase_finished;

  ai->funcs.city_alloc = cai_city_alloc;
  ai->funcs.city_free = cai_city_free;
  /*
    ai->funcs.city_got = NULL;
    ai->funcs.city_lost = NULL;
  */
  ai->funcs.city_save = cai_city_save;
  ai->funcs.city_load = cai_city_load;
  ai->funcs.choose_building = cai_build_adv_override;
  ai->funcs.build_adv_prepare = cai_wonder_city_distance;
  ai->funcs.build_adv_adjust_want = cai_build_adv_adjust;

  ai->funcs.units_ruleset_init = cai_units_ruleset_init;

  /* FIXME: We should allocate memory only for units owned by
     default ai in unit_got. We track no data
     about enemy units.
     But advisors code still depends on some default ai data (role) to be
     always allocated. */
  /*
    ai->funcs.unit_alloc = NULL;
    ai->funcs.unit_free = NULL;
    ai->funcs.unit_got = dai_unit_init;
    ai->funcs.unit_lost = dai_unit_close;
  */
  ai->funcs.unit_alloc = cai_unit_init;
  ai->funcs.unit_free = cai_unit_close;
  ai->funcs.unit_got = NULL;
  ai->funcs.unit_lost = NULL;
  ai->funcs.unit_created = cai_ferry_init_ferry;

  ai->funcs.unit_turn_end = cai_unit_turn_end;
  ai->funcs.unit_move = cai_unit_move_or_attack;
  ai->funcs.unit_task = cai_unit_new_adv_task;

  ai->funcs.unit_save = cai_unit_save;
  ai->funcs.unit_load = cai_unit_load;

  ai->funcs.settler_reset = cai_auto_settler_reset;
  ai->funcs.settler_run = cai_auto_settler_run;

  ai->funcs.first_activities = cai_do_first_activities;
  ai->funcs.diplomacy_actions = cai_diplomacy_actions;
  ai->funcs.last_activities = cai_do_last_activities;
  ai->funcs.treaty_evaluate = cai_treaty_evaluate;
  ai->funcs.treaty_accepted = cai_treaty_accepted;
  ai->funcs.first_contact = cai_diplomacy_first_contact;
  ai->funcs.incident = cai_incident;

  ai->funcs.log_fragment_city = cai_city_log;
  ai->funcs.log_fragment_unit = cai_unit_log;

  ai->funcs.consider_plr_dangerous = cai_consider_plr_dangerous;
  ai->funcs.consider_tile_dangerous = cai_consider_tile_dangerous;
  ai->funcs.consider_wonder_city = cai_consider_wonder_city;

  ai->funcs.refresh = NULL;

  return TRUE;
}
