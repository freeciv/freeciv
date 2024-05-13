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
#ifndef FC__EFFECTS_H
#define FC__EFFECTS_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* utility */
#include "support.h"            /* bool type */

/* common */
#include "fc_types.h"

#include "requirements.h"

struct conn_list;
struct multiplier;

#include "effects_enums_gen.h"

#define EFT_USER_EFFECT_LAST EFT_USER_EFFECT_4

#define USER_EFFECT_NUMBER(eff) (eff - EFT_USER_EFFECT_1)

/* An effect is provided by a source.  If the source is present, and the
 * other conditions (described below) are met, the effect will be active.
 * Note the difference between effect and effect_type. */
struct effect {
  enum effect_type type;

  /* Pointer to multipliers (NULL means that this effect has no multiplier */
  struct multiplier *multiplier;

  /* The "value" of the effect.  The meaning of this varies between
   * effects.  When get_xxx_bonus() is called the value of all applicable
   * effects will be summed up. */
  int value;

  /* An effect can have multiple requirements.  The effect will only be
   * active if all of these requirement are met. */
  struct requirement_vector reqs;

  /* Only relevant for ruledit and other rulesave users. */
  struct {
    /* Indicates that this effect is deleted and shouldn't be saved. */
    bool do_not_save;

    /* Comment field to save. While an entry in the ini-file, not
     * used by freeciv. */
    char *comment;
  } rulesave;
};

/* A callback type that takes an individual effect and a context for it
 * and tells its weighted (by probability or something) value.
 */
typedef double
   (*eft_value_filter_cb)(const struct effect *eft,
                          const struct req_context *context,
                          const struct player *other_player,
                          void *data, int n_data);

/* An effect_list is a list of effects. */
#define SPECLIST_TAG effect
#define SPECLIST_TYPE struct effect
#include "speclist.h"
#define effect_list_iterate(effect_list, peffect) \
  TYPED_LIST_ITERATE(struct effect, effect_list, peffect)
#define effect_list_iterate_end LIST_ITERATE_END

struct effect *effect_new(enum effect_type type, int value,
                          struct multiplier *pmul);
struct effect *effect_copy(struct effect *old,
                           enum effect_type override_type);
void effect_free(struct effect *peffect);
void effect_remove(struct effect *peffect);
void effect_req_append(struct effect *peffect, struct requirement req);

struct astring;
void get_effect_req_text(const struct effect *peffect,
                         char *buf, size_t buf_len);
void get_effect_list_req_text(const struct effect_list *plist,
                              struct astring *astr);

/* ruleset cache creation and communication functions */
struct packet_ruleset_effect;

void ruleset_cache_init(void);
void ruleset_cache_free(void);
void recv_ruleset_effect(const struct packet_ruleset_effect *packet);
void send_ruleset_cache(struct conn_list *dest);

int effect_cumulative_max(enum effect_type type, struct universal *unis,
                          size_t n_unis);
int effect_cumulative_min(enum effect_type type, struct universal *for_uni);

int effect_value_from_universals(enum effect_type type,
                                 struct universal *unis, size_t n_unis);

bool effect_universals_value_never_below(enum effect_type type,
                                         struct universal *unis,
                                         size_t n_unis,
                                         int min_value);

int effect_value_will_make_positive(enum effect_type type);

bool is_building_replaced(const struct city *pcity,
                          const struct impr_type *pimprove,
                          const enum req_problem_type prob_type);

/* Functions to know the bonuses a certain effect is granting */
int get_world_bonus(enum effect_type effect_type);
int get_player_bonus(const struct player *plr, enum effect_type effect_type);
int get_city_bonus(const struct city *pcity, enum effect_type effect_type);
int get_tile_bonus(const struct tile *ptile, enum effect_type effect_type);
int get_city_specialist_output_bonus(const struct city *pcity,
				     const struct specialist *pspecialist,
				     const struct output_type *poutput,
				     enum effect_type effect_type);
int get_city_tile_output_bonus(const struct city *pcity,
			       const struct tile *ptile,
			       const struct output_type *poutput,
			       enum effect_type effect_type);
int get_tile_output_bonus(const struct city *pcity,
                          const struct tile *ptile,
                          const struct output_type *poutput,
                          enum effect_type effect_type);
int get_player_output_bonus(const struct player *pplayer,
                            const struct output_type *poutput,
                            enum effect_type effect_type);
int get_city_output_bonus(const struct city *pcity,
                          const struct output_type *poutput,
                          enum effect_type effect_type);
int get_building_bonus(const struct city *pcity,
		       const struct impr_type *building,
		       enum effect_type effect_type);
int get_unittype_bonus(const struct player *pplayer,
		       const struct tile *ptile, /* pcity is implied */
		       const struct unit_type *punittype,
                       const struct action *paction,
		       enum effect_type effect_type);
int get_unit_bonus(const struct unit *punit, enum effect_type effect_type);
int get_unit_vs_tile_bonus(const struct tile *ptile,
                           const struct unit *punit,
                           enum effect_type etype);

/* Miscellaneous auxiliary effects functions */
struct effect_list *get_req_source_effects(const struct universal *psource);

int get_player_bonus_effects(struct effect_list *plist,
                             const struct player *pplayer,
                             enum effect_type effect_type);
int get_city_bonus_effects(struct effect_list *plist,
			   const struct city *pcity,
			   const struct output_type *poutput,
			   enum effect_type effect_type);

int get_target_bonus_effects(struct effect_list *plist,
                             const struct req_context *context,
                             const struct req_context *other_context,
                             enum effect_type effect_type);
double get_effect_expected_value(const struct req_context *context,
                                 const struct player *other_player,
                                 enum effect_type effect_type,
                                 eft_value_filter_cb weighter,
                                 void *data, int n_data)
fc__attribute((nonnull (4)));

bool building_has_effect(const struct impr_type *pimprove,
			 enum effect_type effect_type);
int get_current_construction_bonus(const struct city *pcity,
                                   enum effect_type effect_type,
                                   const enum req_problem_type prob_type);
int get_potential_improvement_bonus(const struct impr_type *pimprove,
                                    const struct city *pcity,
                                    enum effect_type effect_type,
                                    const enum req_problem_type prob_type,
                                    bool consider_multipliers);

struct effect_list *get_effects(enum effect_type effect_type);

typedef bool (*iec_cb)(struct effect*, void *data);
bool iterate_effect_cache(iec_cb cb, void *data);

bool is_user_effect(enum effect_type eff);
enum effect_type user_effect_ai_valued_as(enum effect_type);
void user_effect_ai_valued_set(enum effect_type tgt, enum effect_type valued_as);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif  /* FC__EFFECTS_H */
