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

/* utility */
#include "support.h"

/* common */
#include "disaster.h"
#include "game.h"
#include "government.h"
#include "improvement.h"
#include "requirements.h"
#include "specialist.h"
#include "style.h"
#include "tech.h"

#include "validity.h"

struct effect_list_cb_data
{
  bool needed;
  struct universal *uni;
  requirers_cb cb;
  void *requirers_data;
};

/**********************************************************************//**
  Callback to check if effect needs universal.
**************************************************************************/
static bool effect_list_universal_needed_cb(struct effect *peffect,
                                            void *data)
{
  struct effect_list_cb_data *cbdata = (struct effect_list_cb_data *)data;

  if (universal_is_mentioned_by_requirements(&peffect->reqs, cbdata->uni)) {
    cbdata->cb(R__("Effect"), cbdata->requirers_data);
    cbdata->needed = TRUE;
  }

  /* Always continue to next until all effects checked */
  return TRUE;
}

/**********************************************************************//**
  Check if anything in ruleset needs universal
**************************************************************************/
static bool is_universal_needed(struct universal *uni, requirers_cb cb,
                                void *data)
{
  bool needed = FALSE;
  bool needed_by_music_style = FALSE;
  int i;
  struct effect_list_cb_data cb_data;

  disaster_type_iterate(pdis) {
    if (universal_is_mentioned_by_requirements(&pdis->reqs, uni)) {
      cb(disaster_rule_name(pdis), data);
      needed = TRUE;
    }
  } disaster_type_iterate_end;

  improvement_re_active_iterate(pimprove) {
    if (universal_is_mentioned_by_requirements(&pimprove->reqs, uni)
        || universal_is_mentioned_by_requirements(&pimprove->obsolete_by,
                                                  uni)) {
      cb(improvement_rule_name(pimprove), data);
      needed = TRUE;
    }
  } improvement_re_active_iterate_end;

  governments_re_active_iterate(pgov) {
    if (universal_is_mentioned_by_requirements(&pgov->reqs, uni)) {
      cb(government_rule_name(pgov), data);
      needed = TRUE;
    }
  } governments_re_active_iterate_end;

  specialist_type_re_active_iterate(psp) {
    if (universal_is_mentioned_by_requirements(&psp->reqs, uni)) {
      cb(specialist_rule_name(psp), data);
      needed = TRUE;
    }
  } specialist_type_re_active_iterate_end;

  extra_type_re_active_iterate(pextra) {
    if (universal_is_mentioned_by_requirements(&pextra->reqs, uni)
        || universal_is_mentioned_by_requirements(&pextra->rmreqs, uni)
        || universal_is_mentioned_by_requirements(&pextra->appearance_reqs, uni)
        || universal_is_mentioned_by_requirements(&pextra->disappearance_reqs, uni)) {
      cb(extra_rule_name(pextra), data);
      needed = TRUE;
    } else {
      struct road_type *proad = extra_road_get(pextra);

      if (proad != nullptr
          && universal_is_mentioned_by_requirements(&proad->first_reqs, uni)) {
        cb(extra_rule_name(pextra), data);
        needed = TRUE;
      }
    }
  } extra_type_re_active_iterate_end;

  goods_type_re_active_iterate(pgood) {
    if (universal_is_mentioned_by_requirements(&pgood->reqs, uni)) {
      cb(goods_rule_name(pgood), data);
      needed = TRUE;
    }
  } goods_type_re_active_iterate_end;

  action_iterate(act) {
    action_enabler_list_re_iterate(action_enablers_for_action(act), enabler) {
      if (universal_is_mentioned_by_requirements(&(enabler->actor_reqs),
                                                 uni)
          || universal_is_mentioned_by_requirements(&(enabler->target_reqs),
                                                    uni)) {
        char buf[1024];

        fc_snprintf(buf, sizeof(buf), R__("%s action enabler"),
                    action_rule_name(enabler_get_action(enabler)));
        cb(buf, data);
        needed = TRUE;
      }
    } action_enabler_list_re_iterate_end;
  } action_iterate_end;

  for (i = 0; i < game.control.num_city_styles; i++) {
    if (universal_is_mentioned_by_requirements(&city_styles[i].reqs, uni)) {
      cb(city_style_rule_name(i), data);
      needed = TRUE;
    }
  }

  music_styles_iterate(pmus) {
    if (universal_is_mentioned_by_requirements(&pmus->reqs, uni)) {
      needed_by_music_style = TRUE;
    }
  } music_styles_iterate_end;

  if (needed_by_music_style) {
    cb(R__("Music Style"), data);
    needed = TRUE;
  }

  for (i = 0; i < CLAUSE_COUNT; i++) {
    struct clause_info *info = clause_info_get(i);

    if (info->enabled) {
      if (universal_is_mentioned_by_requirements(&info->giver_reqs, uni)
          || universal_is_mentioned_by_requirements(&info->receiver_reqs, uni)
          || universal_is_mentioned_by_requirements(&info->either_reqs, uni)) {
        char buf[1024];

        /* TRANS: e.g. "Advance clause" */
        fc_snprintf(buf, sizeof(buf), R__("%s clause"),
                    clause_type_name(info->type));
        cb(buf, data);
        needed = TRUE;
      }
    }
  }

  cb_data.needed = FALSE;
  cb_data.uni = uni;
  cb_data.cb = cb;
  cb_data.requirers_data = data;

  iterate_effect_cache(effect_list_universal_needed_cb, &cb_data);
  needed |= cb_data.needed;

  return needed;
}

/**********************************************************************//**
  Check if anything in ruleset needs counter
**************************************************************************/
bool is_counter_needed(struct counter *pcount, requirers_cb cb, void *data)
{
  struct universal uni = { .value.counter = pcount, .kind = VUT_COUNTER };

  return is_universal_needed(&uni, cb, data);
}

/**********************************************************************//**
  Check if anything in ruleset needs tech
**************************************************************************/
bool is_tech_needed(struct advance *padv, requirers_cb cb, void *data)
{
  struct universal uni = { .value.advance = padv, .kind = VUT_ADVANCE };
  bool needed = FALSE;

  advance_re_active_iterate(pdependant) {
    if (pdependant->require[AR_ONE] == padv
        || pdependant->require[AR_TWO] == padv
        || pdependant->require[AR_ROOT] == padv) {
      cb(advance_rule_name(pdependant), data);
      needed = TRUE;
    }
  } advance_re_active_iterate_end;

  unit_type_re_active_iterate(ptype) {
    if (is_tech_req_for_utype(ptype, padv)) {
      cb(utype_rule_name(ptype), data);
      needed = TRUE;
    }
  } unit_type_re_active_iterate_end;

  extra_type_re_active_iterate(pextra) {
    if (pextra->visibility_req == advance_number(padv)) {
      char buf[512];

      fc_snprintf(buf, sizeof(buf), "%s visibility",
                  extra_rule_name(pextra));
      cb(buf, data);
    }
  } extra_type_re_active_iterate_end;

  needed |= is_universal_needed(&uni, cb, data);

  return needed;
}

/**********************************************************************//**
  Check if anything in ruleset needs building
**************************************************************************/
bool is_building_needed(struct impr_type *pimpr, requirers_cb cb,
                        void *data)
{
  struct universal uni = { .value.building = pimpr, .kind = VUT_IMPROVEMENT };
  bool needed = FALSE;

  needed |= is_universal_needed(&uni, cb, data);

  return needed;
}

/**********************************************************************//**
  Check if anything in ruleset needs unit type
**************************************************************************/
bool is_utype_needed(struct unit_type *ptype, requirers_cb cb,
                     void *data)
{
  struct universal uni = { .value.utype = ptype, .kind = VUT_UTYPE };
  bool needed = FALSE;

  needed |= is_universal_needed(&uni, cb, data);

  terrain_re_active_iterate(pterr) {
    if (pterr->animal == ptype) {
      cb(terrain_rule_name(pterr), data);
      needed = TRUE;
    }
  } terrain_re_active_iterate_end;

  return needed;
}

/**********************************************************************//**
  Check if anything in ruleset needs achievement type
**************************************************************************/
bool is_achievement_needed(struct achievement *pach, requirers_cb cb,
                           void *data)
{
  struct universal uni = { .value.achievement = pach, .kind = VUT_ACHIEVEMENT };
  bool needed = FALSE;

  needed |= is_universal_needed(&uni, cb, data);

  return needed;
}

/**********************************************************************//**
  Check if anything in ruleset needs goods type
**************************************************************************/
bool is_good_needed(struct goods_type *pgood, requirers_cb cb,
                    void *data)
{
  struct universal uni = { .value.good = pgood, .kind = VUT_GOOD };
  bool needed = FALSE;

  needed |= is_universal_needed(&uni, cb, data);

  return needed;
}

/**********************************************************************//**
  Check if anything in ruleset needs extra type
**************************************************************************/
bool is_extra_needed(struct extra_type *pextra, requirers_cb cb,
                     void *data)
{
  struct universal uni = { .value.extra = pextra, .kind = VUT_EXTRA };
  bool needed = FALSE;
  bool conflicts = FALSE;
  bool hides = FALSE;
  int id = extra_index(pextra);

  extra_type_re_active_iterate(requirer) {
    conflicts |= BV_ISSET(requirer->conflicts, id);
    hides     |= BV_ISSET(requirer->hidden_by, id);
  } extra_type_re_active_iterate_end;

  if (conflicts) {
    cb(R__("Conflicting extra"), data);
  }
  if (hides) {
    cb(R__("Hidden extra"), data);
  }
  needed |= conflicts | hides;

  needed |= is_universal_needed(&uni, cb, data);

  return needed;
}

/**********************************************************************//**
  Check if anything in ruleset needs terrain type
**************************************************************************/
bool is_terrain_needed(struct terrain *pterr, requirers_cb cb, void *data)
{
  struct universal uni = { .value.terrain = pterr, .kind = VUT_TERRAIN };
  bool needed = FALSE;

  needed |= is_universal_needed(&uni, cb, data);

  terrain_re_active_iterate(pother) {
    if (pother != pterr
        && (pother->cultivate_result == pterr
            || pother->plant_result == pterr
            || pother->transform_result == pterr
            || pother->warmer_wetter_result == pterr
            || pother->warmer_drier_result == pterr
            || pother->cooler_wetter_result == pterr
            || pother->cooler_drier_result == pterr)) {
      cb(terrain_rule_name(pother), data);
      needed = TRUE;
    }
  } terrain_re_active_iterate_end;

  return needed;
}

/**********************************************************************//**
  Check if anything in ruleset needs government
**************************************************************************/
bool is_government_needed(struct government *pgov, requirers_cb cb, void *data)
{
  struct universal uni = { .value.govern = pgov, .kind = VUT_GOVERNMENT };
  bool needed = FALSE;

  needed |= is_universal_needed(&uni, cb, data);

  return needed;
}

struct effect_list_multiplier_data
{
  bool needed;
  struct multiplier *pmul;
  requirers_cb cb;
  void *requirers_data;
};

/**********************************************************************//**
  Callback to check if effect needs universal.
**************************************************************************/
static bool effect_list_multiplier_cb(struct effect *peffect,
                                      void *data)
{
  struct effect_list_multiplier_data *cbdata = (struct effect_list_multiplier_data *)data;

  if (peffect->multiplier == cbdata->pmul) {
    cbdata->cb(R__("Effect"), cbdata->requirers_data);
    cbdata->needed = TRUE;
  }

  /* Always continue to next until all effects checked */
  return TRUE;
}

/**********************************************************************//**
  Check if anything in ruleset needs multiplier
**************************************************************************/
bool is_multiplier_needed(struct multiplier *pmul, requirers_cb cb, void *data)
{
  struct effect_list_multiplier_data cb_data;
  bool needed = FALSE;

  cb_data.needed = FALSE;
  cb_data.pmul = pmul;
  cb_data.cb = cb;
  cb_data.requirers_data = data;

  iterate_effect_cache(effect_list_multiplier_cb, &cb_data);
  needed |= cb_data.needed;

  return needed;
}
