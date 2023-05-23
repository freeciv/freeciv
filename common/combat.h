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
#ifndef FC__COMBAT_H
#define FC__COMBAT_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* common */
#include "fc_types.h"
#include "unittype.h"

struct civ_map;

/*
 * attack_strength and defense_strength are multiplied by POWER_FACTOR
 * to yield the base of attack_power and defense_power.
 *
 * The constant may be changed since it isn't externally visibly used.
 */
#define POWER_FACTOR    10

enum unit_attack_result {
  ATT_OK,
  ATT_NON_ATTACK,
  ATT_UNREACHABLE,
  ATT_NONNATIVE_SRC,
  ATT_NONNATIVE_DST,
  ATT_NOT_WIPABLE
};

bool is_unit_reachable_at(const struct unit *defender,
                          const struct unit *attacker,
                          const struct tile *location);
enum unit_attack_result
unit_attack_unit_at_tile_result(const struct unit *punit,
                                const struct action *paction,
                                const struct unit *pdefender,
                                const struct tile *dest_tile);
enum unit_attack_result
unit_attack_units_at_tile_result(const struct unit *punit,
                                 const struct action *paction,
                                 const struct tile *ptile);
enum unit_attack_result
unit_wipe_units_at_tile_result(const struct unit *punit,
                               const struct tile *ptile);
bool can_unit_attack_tile(const struct unit *punit,
                          const struct action *paction,
                          const struct tile *ptile);

double win_chance(int as, int ahp, int afp, int ds, int dhp, int dfp);

void get_modified_firepower(const struct civ_map *nmap,
                            const struct unit *attacker,
                            const struct unit *defender,
                            int *att_fp, int *def_fp);
double unit_win_chance(const struct civ_map *nmap,
                       const struct unit *attacker,
                       const struct unit *defender,
                       const struct action *paction);

bool unit_really_ignores_citywalls(const struct unit *punit);
struct city *sdi_try_defend(const struct civ_map *nmap,
                            const struct player *owner,
                            const struct tile *ptile);
bool is_tired_attack(int moves_left);

int get_attack_power(const struct unit *punit);
int base_get_attack_power(const struct unit_type *punittype,
                          int veteran, int moves_left);
int base_get_defense_power(const struct unit *punit);
int get_total_defense_power(const struct unit *attacker,
                            const struct unit *defender);
int get_fortified_defense_power(const struct unit *attacker,
                                struct unit *defender);
int get_virtual_defense_power(const struct civ_map *nmap,
                              const struct unit_type *attacker,
                              const struct unit_type *defender,
                              struct player *defending_player,
                              struct tile *ptile,
                              bool fortified, int veteran);
int get_total_attack_power(const struct unit *attacker,
                           const struct unit *defender,
                           const struct action *paction);

struct unit *get_defender(const struct civ_map *nmap,
                          const struct unit *attacker,
                          const struct tile *ptile,
                          const struct action *paction);
struct unit *get_attacker(const struct civ_map *nmap,
                          const struct unit *defender,
                          const struct tile *ptile);

struct unit *get_diplomatic_defender(const struct unit *act_unit,
                                     const struct unit *pvictim,
                                     const struct tile *tgt_tile,
                                     const struct action *paction);

bool is_stack_vulnerable(const struct tile *ptile);

int combat_bonus_against(const struct combat_bonus_list *list,
                         const struct unit_type *enemy,
                         enum combat_bonus_type type);

int unit_bombard_rate(struct unit *punit);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* FC__COMBAT_H */
