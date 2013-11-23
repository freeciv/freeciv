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

#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

/* utility */
#include "registry.h"
#include "string_vector.h"

/* common */
#include "game.h"
#include "government.h"
#include "movement.h"
#include "unittype.h"

/* server */
#include "ruleset.h"

#include "rulesave.h"

/**************************************************************************
  Create new ruleset section file with common header.
**************************************************************************/
static struct section_file *create_ruleset_file(const char *rsname,
                                                const char *rstype)
{
  struct section_file *sfile = secfile_new(TRUE);
  char buf[500];

  if (rsname != NULL) {
    fc_snprintf(buf, sizeof(buf), "Template %s %s data for Freeciv", rsname, rstype);
  } else {
    fc_snprintf(buf, sizeof(buf), "Template %s data for Freeciv", rstype);
  }

  secfile_insert_str(sfile, buf, "datafile.description");
  secfile_insert_str(sfile, RULESET_CAPABILITIES, "datafile.options");

  return sfile;
}

/**************************************************************************
  Save name of the object.
**************************************************************************/
static bool save_name_translation(struct section_file *sfile,
                                  struct name_translation *name,
                                  const char *path)
{
  secfile_insert_str(sfile,
                     untranslated_name(name),
                     "%s.name", path);
  if (strcmp(skip_intl_qualifier_prefix(untranslated_name(name)),
             rule_name(name))) {
    secfile_insert_str(sfile,
                       rule_name(name),
                       "%s.rule_name", path);
  }

  return TRUE;
}

/**************************************************************************
  Save vector of requirements
**************************************************************************/
static bool save_reqs_vector(struct section_file *sfile,
                             struct requirement_vector *reqs,
                             const char *path, const char *entry)
{
  int i;
  bool includes_negated = FALSE;
  bool includes_surviving = FALSE;

  requirement_vector_iterate(reqs, preq) {
    if (!preq->present) {
      includes_negated = TRUE;
    }
    if (preq->survives) {
      includes_surviving = TRUE;
    }
  } requirement_vector_iterate_end;

  i = 0;
  requirement_vector_iterate(reqs, preq) {
    secfile_insert_str(sfile,
                       universals_n_name(preq->source.kind),
                       "%s.%s%d.type", path, entry, i);
    secfile_insert_str(sfile,
                       universal_rule_name(&(preq->source)),
                       "%s.%s%d.name", path, entry, i);
    secfile_insert_str(sfile,
                       req_range_name(preq->range),
                       "%s.%s%d.range", path, entry, i);

    if (includes_surviving) {
      secfile_insert_bool(sfile,
                          preq->survives,
                          "%s.%s%d.survives", path, entry, i);
    }

    if (includes_negated) {
      secfile_insert_bool(sfile,
                          preq->present,
                          "%s.%s%d.present", path, entry, i);
    }

    i++;
  } requirement_vector_iterate_end;

  return TRUE;
}

/**************************************************************************
  Save list of requirements
**************************************************************************/
static bool save_reqs_list(struct section_file *sfile,
                           const struct requirement_list *reqs,
                           const char *path, const char *entry)
{
  int i;
  bool includes_negated = FALSE;
  bool includes_surviving = FALSE;

  requirement_list_iterate(reqs, preq) {
    if (!preq->present) {
      includes_negated = TRUE;
    }
    if (preq->survives) {
      includes_surviving = TRUE;
    }
  } requirement_list_iterate_end;

  i = 0;
  requirement_list_iterate(reqs, preq) {
    secfile_insert_str(sfile,
                       universals_n_name(preq->source.kind),
                       "%s.%s%d.type", path, entry, i);
    secfile_insert_str(sfile,
                       universal_rule_name(&(preq->source)),
                       "%s.%s%d.name", path, entry, i);
    secfile_insert_str(sfile,
                       req_range_name(preq->range),
                       "%s.%s%d.range", path, entry, i);

    if (includes_surviving) {
      secfile_insert_bool(sfile,
                          preq->survives,
                          "%s.%s%d.survives", path, entry, i);
    }

    if (includes_negated) {
      secfile_insert_bool(sfile,
                          preq->present,
                          "%s.%s%d.present", path, entry, i);
    }

    i++;
  } requirement_list_iterate_end;

  return TRUE;
}

/**************************************************************************
  Save tech reference
**************************************************************************/
static bool save_tech_ref(struct section_file *sfile,
                          const struct advance *padv,
                          const char *path, const char *entry)
{
   if (padv == A_NEVER) {
     secfile_insert_str(sfile, "Never", "%s.%s", path, entry);
   } else {
     secfile_insert_str(sfile, advance_rule_name(padv),
                        "%s.%s", path, entry);
   }

   return TRUE;
}

/**************************************************************************
  Save government reference
**************************************************************************/
static bool save_gov_ref(struct section_file *sfile,
                         const struct government *gov,
                         const char *path, const char *entry)
{
  secfile_insert_str(sfile, government_rule_name(gov), "%s.%s", path, entry);

  return TRUE;
}

/**************************************************************************
  Save vector of unit class names based on bitvector bits
**************************************************************************/
static bool save_uclass_vec(struct section_file *sfile,
                            bv_unit_classes *bits,
                            const char *path, const char *entry,
                            bool unreachable_only)
{
  const char *class_names[UCL_LAST];
  int classes = 0;

  unit_class_iterate(pcargo) {
    if (BV_ISSET(*(bits), uclass_index(pcargo))
        && (uclass_has_flag(pcargo, UCF_UNREACHABLE)
            || !unreachable_only)) {
      class_names[classes++] = uclass_rule_name(pcargo);
    }
  } unit_class_iterate_end;

  if (classes > 0) {
    secfile_insert_str_vec(sfile, class_names, classes,
                           "%s.%s", path, entry);
  }

  return TRUE;
}

/**************************************************************************
  Save strvec as ruleset vector of strings
**************************************************************************/
static bool save_strvec(struct section_file *sfile,
                        struct strvec *to_save,
                        const char *path, const char *entry)
{
  if (to_save != NULL) {
    int sect_count = strvec_size(to_save);
    const char *sections[sect_count];
    int i;

    for (i = 0; i < sect_count; i++) {
      sections[i] = strvec_get(to_save, i);
    }

    secfile_insert_str_vec(sfile, sections, sect_count, "%s.%s", path, entry);
  }

  return TRUE;
}

/**************************************************************************
  Save ruleset file.
**************************************************************************/
static bool save_ruleset_file(struct section_file *sfile, const char *filename)
{
  return secfile_save(sfile, filename, 0, FZ_PLAIN);
}

/**************************************************************************
  Save buildings.ruleset
**************************************************************************/
static bool save_buildings_ruleset(const char *filename, const char *name)
{
  struct section_file *sfile = create_ruleset_file(name, "building");

  if (sfile == NULL) {
    return FALSE;
  }

  return save_ruleset_file(sfile, filename);
}

/**************************************************************************
  Save cities.ruleset
**************************************************************************/
static bool save_cities_ruleset(const char *filename, const char *name)
{
  struct section_file *sfile = create_ruleset_file(name, "cities");

  if (sfile == NULL) {
    return FALSE;
  }

  return save_ruleset_file(sfile, filename);
}

/**************************************************************************
  Effect saving callback data structure.
**************************************************************************/
typedef struct {
  int idx;
  struct section_file *sfile;
} effect_cb_data;

/**************************************************************************
  Save one effect. Callback called for each effect in cache.
**************************************************************************/
static bool effect_save(const struct effect *peffect, void *data)
{
  effect_cb_data *cbdata = (effect_cb_data *)data;
  char path[512];

  fc_snprintf(path, sizeof(path), "effect_%d", cbdata->idx++);

  secfile_insert_str(cbdata->sfile,
                     effect_type_name(peffect->type),
                     "%s.type", path);
  secfile_insert_int(cbdata->sfile, peffect->value, "%s.value", path);

  save_reqs_list(cbdata->sfile, peffect->reqs, path, "reqs");
  save_reqs_list(cbdata->sfile, peffect->nreqs, path, "nreqs");

  return TRUE;
}

/**************************************************************************
  Save effects.ruleset
**************************************************************************/
static bool save_effects_ruleset(const char *filename, const char *name)
{
  struct section_file *sfile = create_ruleset_file(name, "effect");
  effect_cb_data data;

  if (sfile == NULL) {
    return FALSE;
  }

  data.idx = 0;
  data.sfile = sfile;

  if (!iterate_effect_cache(effect_save, &data)) {
    return FALSE;
  }

  return save_ruleset_file(sfile, filename);
}

/**************************************************************************
  Save game.ruleset
**************************************************************************/
static bool save_game_ruleset(const char *filename, const char *name)
{
  struct section_file *sfile = create_ruleset_file(name, "game");

  if (sfile == NULL) {
    return FALSE;
  }

  return save_ruleset_file(sfile, filename);
}

/**************************************************************************
  Save governments.ruleset
**************************************************************************/
static bool save_governments_ruleset(const char *filename, const char *name)
{
  struct section_file *sfile = create_ruleset_file(name, "government");
  int sect_idx;

  if (sfile == NULL) {
    return FALSE;
  }

  save_gov_ref(sfile, game.government_during_revolution, "governments",
               "during_revolution");

  sect_idx = 0;
  governments_iterate(pg) {
    char path[512];
    struct ruler_title *prtitle;

    fc_snprintf(path, sizeof(path), "government_%d", sect_idx++);

    save_name_translation(sfile, &(pg->name), path);

    secfile_insert_str(sfile, pg->graphic_str, "%s.graphic", path);
    secfile_insert_str(sfile, pg->graphic_alt, "%s.graphic_alt", path);

    save_reqs_vector(sfile, &(pg->reqs), path, "reqs");

    if (pg->ai.better != NULL) {
      save_gov_ref(sfile, pg->ai.better, path,
                   "ai_better");
    }

    ruler_title_hash_lookup(pg->ruler_titles, NULL,
                            &prtitle);
    if (prtitle != NULL) {
      const char *title;

      title = ruler_title_male_untranslated_name(prtitle);
      if (title != NULL) {
        secfile_insert_str(sfile, title,
                           "%s.ruler_male_title", path);
      }

      title = ruler_title_female_untranslated_name(prtitle);
      if (title != NULL) {
        secfile_insert_str(sfile, title,
                           "%s.ruler_female_title", path);
      }
    }

    save_strvec(sfile, pg->helptext, path, "helptext");

  } governments_iterate_end;

  return save_ruleset_file(sfile, filename);
}

/**************************************************************************
  Save nations.ruleset
**************************************************************************/
static bool save_nations_ruleset(const char *filename, const char *name)
{
  struct section_file *sfile = create_ruleset_file(name, "nation");

  if (sfile == NULL) {
    return FALSE;
  }

  return save_ruleset_file(sfile, filename);
}

/**************************************************************************
  Save techs.ruleset
**************************************************************************/
static bool save_techs_ruleset(const char *filename, const char *name)
{
  struct section_file *sfile = create_ruleset_file(name, "tech");
  int i;
  int sect_idx;
  struct advance *a_none = advance_by_number(A_NONE);


  if (sfile == NULL) {
    return FALSE;
  }

  for (i = 0; i < MAX_NUM_USER_TECH_FLAGS; i++) {
    const char *flagname = tech_flag_id_name(i + TECH_USER_1);
    const char *helptxt = tech_flag_helptxt(i + TECH_USER_1);

    if (flagname != NULL && helptxt != NULL) {
      secfile_insert_str(sfile, flagname, "control.flags%d.name", i);
      secfile_insert_str(sfile, helptxt, "control.flags%d.helptxt", i);
    }
  }

  sect_idx = 0;
  advance_iterate(A_FIRST, pa) {
    char path[512];
    const char *flag_names[TF_COUNT];
    int set_count;
    int flagi;

    fc_snprintf(path, sizeof(path), "advance_%d", sect_idx++);

    save_name_translation(sfile, &(pa->name), path);

    save_tech_ref(sfile, pa->require[AR_ONE], path, "req1");
    save_tech_ref(sfile, pa->require[AR_TWO], path, "req2");
    if (pa->require[AR_ROOT] != a_none) {
      save_tech_ref(sfile, pa->require[AR_ROOT], path, "root_req");
    }

    secfile_insert_str(sfile, pa->graphic_str, "%s.graphic", path);
    if (strcmp("-", pa->graphic_alt)) {
      secfile_insert_str(sfile, pa->graphic_alt, "%s.graphic_alt", path);
    }
    if (pa->bonus_message != NULL) {
      secfile_insert_str(sfile, pa->bonus_message, "%s.bonus_message", path);
    }

    set_count = 0;
    for (flagi = 0; flagi < TF_COUNT; flagi++) {
      if (advance_has_flag(advance_index(pa), flagi)) {
        flag_names[set_count++] = tech_flag_id_name(flagi);
      }
    }

    if (set_count > 0) {
      secfile_insert_str_vec(sfile, flag_names, set_count,
                             "%s.flags", path);
    }
    if (pa->preset_cost >= 0) {
      secfile_insert_int(sfile, pa->preset_cost, "%s.cost", path);
    }

    save_strvec(sfile, pa->helptext, path, "helptext");

  } advance_iterate_end;

  return save_ruleset_file(sfile, filename);
}

/**************************************************************************
  Save terrain.ruleset
**************************************************************************/
static bool save_terrain_ruleset(const char *filename, const char *name)
{
  struct section_file *sfile = create_ruleset_file(name, "terrain");

  if (sfile == NULL) {
    return FALSE;
  }

  return save_ruleset_file(sfile, filename);
}

/**************************************************************************
  Save one veteran system.
**************************************************************************/
static bool save_veteran_system(struct section_file *sfile, const char *path,
                                struct veteran_system *vsystem)
{
  const char *vlist_name[vsystem->levels];
  int vlist_power[vsystem->levels];
  int vlist_raise[vsystem->levels];
  int vlist_wraise[vsystem->levels];
  int vlist_move[vsystem->levels];
  int i;

  for (i = 0; i < vsystem->levels; i++) {
    vlist_name[i] = rule_name(&(vsystem->definitions[i].name));
    vlist_power[i] = vsystem->definitions[i].power_fact;
    vlist_raise[i] = vsystem->definitions[i].raise_chance;
    vlist_wraise[i] = vsystem->definitions[i].work_raise_chance;
    vlist_move[i] = vsystem->definitions[i].move_bonus;
  }

  secfile_insert_str_vec(sfile, vlist_name, vsystem->levels,
                         "%s.veteran_names", path);
  secfile_insert_int_vec(sfile, vlist_power, vsystem->levels,
                         "%s.veteran_power_fact", path);
  secfile_insert_int_vec(sfile, vlist_raise, vsystem->levels,
                         "%s.veteran_raise_chance", path);
  secfile_insert_int_vec(sfile, vlist_wraise, vsystem->levels,
                         "%s.veteran_work_raise_chance", path);
  secfile_insert_int_vec(sfile, vlist_move, vsystem->levels,
                         "%s.veteran_move_bonus", path);

  return TRUE;
}

/**************************************************************************
  Save unit combat bonuses list.
**************************************************************************/
static bool save_combat_bonuses(struct section_file *sfile,
                                struct unit_type *put,
                                char *path)
{
  int i = 0;

  combat_bonus_list_iterate(put->bonuses, pbonus) {
    secfile_insert_str(sfile, unit_type_flag_id_name(pbonus->flag),
                       "%s.bonuses%d.flag", path, i);
    secfile_insert_str(sfile, combat_bonus_type_name(pbonus->type),
                       "%s.bonuses%d.type", path, i);
    secfile_insert_int(sfile, pbonus->value,
                       "%s.bonuses%d.value", path, i);
    i++;
  } combat_bonus_list_iterate_end;

  return TRUE;
}

/**************************************************************************
  Save units.ruleset
**************************************************************************/
static bool save_units_ruleset(const char *filename, const char *name)
{
  struct section_file *sfile = create_ruleset_file(name, "unit");
  int i;
  int sect_idx;

  if (sfile == NULL) {
    return FALSE;
  }

  for (i = 0; i < MAX_NUM_USER_UNIT_FLAGS; i++) {
    const char *flagname = unit_type_flag_id_name(i + UTYF_USER_FLAG_1);
    const char *helptxt = unit_type_flag_helptxt(i + UTYF_USER_FLAG_1);

    if (flagname != NULL && helptxt != NULL) {
      secfile_insert_str(sfile, flagname, "control.flags%d.name", i);
      secfile_insert_str(sfile, helptxt, "control.flags%d.helptxt", i);
    }
  }

  save_veteran_system(sfile, "veteran_system", game.veteran);

  sect_idx = 0;
  unit_class_iterate(puc) {
    char path[512];
    char *hut_str = NULL;
    const char *flag_names[UCF_COUNT];
    int flagi;
    int set_count;

    fc_snprintf(path, sizeof(path), "unitclass_%d", sect_idx++);

    save_name_translation(sfile, &(puc->name), path);

    secfile_insert_int(sfile, puc->min_speed / SINGLE_MOVE,
                       "%s.min_speed", path);
    secfile_insert_int(sfile, puc->hp_loss_pct, "%s.hp_loss_pct", path);
    if (puc->non_native_def_pct != 100) {
      secfile_insert_int(sfile, puc->non_native_def_pct,
                         "%s.non_native_def_pct", path);
    }
    if (puc->hut_behavior != HUT_NORMAL) {
      switch (puc->hut_behavior) {
      case HUT_NORMAL:
        hut_str = "Normal";
        break;
      case HUT_NOTHING:
        hut_str = "Nothing";
        break;
      case HUT_FRIGHTEN:
        hut_str = "Frighten";
        break;
      }
      fc_assert(hut_str != NULL);
      secfile_insert_str(sfile, hut_str, "%s.hut_behavior", path);
    }

    set_count = 0;
    for (flagi = 0; flagi < UCF_COUNT; flagi++) {
      if (uclass_has_flag(puc, flagi)) {
        flag_names[set_count++] = unit_class_flag_id_name(flagi);
      }
    }

    if (set_count > 0) {
      secfile_insert_str_vec(sfile, flag_names, set_count,
                             "%s.flags", path);
    }

  } unit_class_iterate_end;

  sect_idx = 0;
  unit_type_iterate(put) {
    char path[512];
    const char *flag_names[UTYF_LAST_USER_FLAG + 1];
    int flagi;
    int set_count;

    fc_snprintf(path, sizeof(path), "unit_%d", sect_idx++);

    save_name_translation(sfile, &(put->name), path);

    secfile_insert_str(sfile, uclass_rule_name(put->uclass),
                       "%s.class", path);

    save_tech_ref(sfile, put->require_advance, path, "tech_req");

    if (put->need_government != NULL) {
      secfile_insert_str(sfile, government_rule_name(put->need_government),
                         "%s.gov_req", path);
    }

    if (put->need_improvement != NULL) {
      secfile_insert_str(sfile, improvement_rule_name(put->need_improvement),
                         "%s.impr_req", path);
    }

    if (put->obsoleted_by != NULL) {
      secfile_insert_str(sfile, utype_rule_name(put->obsoleted_by),
                         "%s.obsolete_by", path);
    }

    secfile_insert_str(sfile, put->graphic_str, "%s.graphic", path);
    if (strcmp("-", put->graphic_alt)) {
      secfile_insert_str(sfile, put->graphic_alt, "%s.graphic_alt", path);
    }
    if (strcmp("-", put->sound_move)) {
      secfile_insert_str(sfile, put->sound_move, "%s.sound_move", path);
    }
    if (strcmp("-", put->sound_move_alt)) {
      secfile_insert_str(sfile, put->sound_move_alt, "%s.sound_move_alt", path);
    }
    if (strcmp("-", put->sound_fight)) {
      secfile_insert_str(sfile, put->sound_fight, "%s.sound_fight", path);
    }
    if (strcmp("-", put->sound_fight_alt)) {
      secfile_insert_str(sfile, put->sound_fight_alt, "%s.sound_fight_alt", path);
    }

    secfile_insert_int(sfile, put->build_cost, "%s.build_cost", path);
    secfile_insert_int(sfile, put->pop_cost, "%s.pop_cost", path);
    secfile_insert_int(sfile, put->attack_strength, "%s.attack", path);
    secfile_insert_int(sfile, put->defense_strength, "%s.defense", path);
    secfile_insert_int(sfile, put->move_rate / SINGLE_MOVE, "%s.move_rate", path);
    secfile_insert_int(sfile, put->vision_radius_sq, "%s.vision_radius_sq", path);
    secfile_insert_int(sfile, put->transport_capacity, "%s.transport_cap", path);

    save_uclass_vec(sfile, &(put->cargo), path, "cargo", FALSE);
    save_uclass_vec(sfile, &(put->embarks), path, "embarks", TRUE);
    save_uclass_vec(sfile, &(put->disembarks), path, "disembarks", TRUE);

    secfile_insert_int(sfile, put->hp, "%s.hitpoints", path);
    secfile_insert_int(sfile, put->firepower, "%s.firepower", path);
    secfile_insert_int(sfile, put->fuel, "%s.fuel", path);
    secfile_insert_int(sfile, put->happy_cost, "%s.uk_happy", path);

    output_type_iterate(o) {
      if (put->upkeep[o] != 0) {
        secfile_insert_int(sfile, put->upkeep[o], "%s.uk_%s",
                           path, get_output_identifier(o));
      }
    } output_type_iterate_end;

    if (put->converted_to != NULL) {
      secfile_insert_str(sfile, utype_rule_name(put->converted_to),
                         "%s.convert_to", path);
    }
    if (put->convert_time != 1) {
      secfile_insert_int(sfile, put->convert_time, "%s.convert_time", path);
    }

    save_combat_bonuses(sfile, put, path);
    save_uclass_vec(sfile, &(put->targets), path, "targets", TRUE);

    if (put->veteran != NULL) {
      save_veteran_system(sfile, path, put->veteran);
    }

    if (put->paratroopers_range != 0) {
      secfile_insert_int(sfile, put->paratroopers_range,
                         "%s.paratroopers_range", path);
     secfile_insert_int(sfile, put->paratroopers_mr_req / SINGLE_MOVE,
                         "%s.paratroopers_mr_req", path);
     secfile_insert_int(sfile, put->paratroopers_mr_sub / SINGLE_MOVE,
                         "%s.paratroopers_mr_sub", path);
    }
    if (put->bombard_rate != 0) {
      secfile_insert_int(sfile, put->bombard_rate,
                         "%s.bombard_rate", path);
    }
    if (put->city_size != 1) {
      secfile_insert_int(sfile, put->city_size,
                         "%s.city_size", path);
    }

    set_count = 0;
    for (flagi = 0; flagi <= UTYF_LAST_USER_FLAG; flagi++) {
      if (utype_has_flag(put, flagi)) {
        flag_names[set_count++] = unit_type_flag_id_name(flagi);
      }
    }

    if (set_count > 0) {
      secfile_insert_str_vec(sfile, flag_names, set_count,
                             "%s.flags", path);
    }

    set_count = 0;
    for (flagi = L_FIRST; flagi < L_LAST; flagi++) {
      if (utype_has_role(put, flagi)) {
        flag_names[set_count++] = unit_role_id_name(flagi);
      }
    }

    if (set_count > 0) {
      secfile_insert_str_vec(sfile, flag_names, set_count,
                             "%s.roles", path);
    }

    save_strvec(sfile, put->helptext, path, "helptext");

  } unit_type_iterate_end;

  return save_ruleset_file(sfile, filename);
}

/**************************************************************************
  Save ruleset to directory given.
**************************************************************************/
bool save_ruleset(const char *path, const char *name)
{
  if (make_dir(path)) {
    bool success = TRUE;
    char filename[500];

    if (success) {
      fc_snprintf(filename, sizeof(filename), "%s/buildings.ruleset", path);
      success = save_buildings_ruleset(filename, name);
    }

    if (success) {
      fc_snprintf(filename, sizeof(filename), "%s/cities.ruleset", path);
      success = save_cities_ruleset(filename, name);
    }

    if (success) {
      fc_snprintf(filename, sizeof(filename), "%s/effects.ruleset", path);
      success = save_effects_ruleset(filename, name);
    }

    if (success) {
      fc_snprintf(filename, sizeof(filename), "%s/game.ruleset", path);
      success = save_game_ruleset(filename, name);
    }

    if (success) {
      fc_snprintf(filename, sizeof(filename), "%s/governments.ruleset", path);
      success = save_governments_ruleset(filename, name);
    }

    if (success) {
      fc_snprintf(filename, sizeof(filename), "%s/nations.ruleset", path);
      success = save_nations_ruleset(filename, name);
    }

    if (success) {
      fc_snprintf(filename, sizeof(filename), "%s/techs.ruleset", path);
      success = save_techs_ruleset(filename, name);
    }

    if (success) {
      fc_snprintf(filename, sizeof(filename), "%s/terrain.ruleset", path);
      success = save_terrain_ruleset(filename, name);
    }

    if (success) {
      fc_snprintf(filename, sizeof(filename), "%s/units.ruleset", path);
      success = save_units_ruleset(filename, name);
    }

    return success;
  } else {
    log_error("Failed to create directory %s", path);
    return FALSE;
  }

  return TRUE;
}
