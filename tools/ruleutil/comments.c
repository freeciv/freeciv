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
#include "fcintl.h"
#include "mem.h"
#include "registry.h"
#include "section_file.h"

#include "comments.h"

static struct {
  /* Comment sections */
  char *file_header;
  char *buildings;
  char *tech_classes;
  char *techs;
  char *govs;
  char *policies;
  char *uclasses;
  char *utypes;
  char *terrains;
  char *resources;
  char *extras;
  char *bases;
  char *roads;
  char *styles;
  char *citystyles;
  char *musicstyles;
  char *effects;
  char *ueffs;
  char *disasters;
  char *achievements;
  char *uflags_utype;
  char *uflags_uclass;
  char *uflags_terrain;
  char *uflags_extra;
  char *uflags_tech;
  char *uflags_building;
  char *trade_settings;
  char *goods;
  char *actions;
  char *enablers;
  char *specialists;
  char *nations;
  char *nationgroups;
  char *nationsets;
  char *clauses;
  char *counters;

  /* Other section entries */
  char *nations_ruledit;

  /* Comment entries */
  char *civstyle_granary;
  char *civstyle_ransom_gold;
  char *civstyle_gameloss_style;
  char *civstyle_gold_upkeep_style;
  char *civstyle_homeless_gold_upkeep;
  char *civstyle_airlift_always;
  char *wonder_visibility_small_wonders;
  char *incite_cost;
  char *combat_rules_tired_attack;
  char *combat_rules_only_killing_veteran;
  char *combat_rules_only_real_fight_veteran;
  char *combat_rules_scaled_veterancy;
  char *combat_rules_damage_reduces_bombard_rate;
  char *combat_rules_low_fp_badwallattacker;
  char *combat_rules_low_fp_pearl_harbour;
  char *combat_rules_low_fp_combat_bonus;
  char *combat_rules_low_fp_nonnat_bombard;
  char *combat_rules_nuke_pop_loss;
  char *combat_rules_nuke_defender_survival;
  char *auto_attack;
  char *actions_dc_initial_odds;
  char *actions_quiet_actions;
  char *borders_radius_permanent;
  char *research_tech_cost_style;
  char *research_base_tech_cost;
  char *research_min_tech_cost;
  char *research_tech_leakage;
  char *research_upkeep_style;
  char *research_free_tech_method;
  char *culture_history_interest;
  char *culture_migration_pml;
  char *world_peace_turns;
  char *calendar_fragments;

  char *std_tileset_compat;
} comments_storage;

/**********************************************************************//**
  Load comments to add to the saved rulesets.
**************************************************************************/
bool comments_load(void)
{
  struct section_file *comment_file;
  const char *fullpath;

  fullpath = fileinfoname(get_data_dirs(), "ruledit/" COMMENTS_FILE_NAME);

  if (fullpath == NULL) {
    log_error(_("Can't find the comments file"));
    return FALSE;
  }

  comment_file = secfile_load(fullpath, FALSE);
  if (comment_file == NULL) {
    log_error(_("Can't parse the comments file"));
    return FALSE;
  }

#define comment_load(target, comment_file, comment_path)                  \
{                                                                         \
  const char *comment;                                                    \
                                                                          \
  if ((comment = secfile_lookup_str(comment_file, comment_path))) {       \
    target = fc_strdup(comment);                                          \
  } else {                                                                \
    log_error(_("Can't read %s from comments file"), comment_path);     \
    return FALSE;                                                         \
  }                                                                       \
}

  comment_load(comments_storage.file_header, comment_file, "common.header");
  comment_load(comments_storage.buildings,
               comment_file, "typedoc.buildings");
  comment_load(comments_storage.tech_classes,
               comment_file, "typedoc.tech_classes");
  comment_load(comments_storage.techs, comment_file, "typedoc.techs");
  comment_load(comments_storage.govs, comment_file, "typedoc.governments");
  comment_load(comments_storage.policies, comment_file, "typedoc.policies");
  comment_load(comments_storage.uclasses, comment_file, "typedoc.uclasses");
  comment_load(comments_storage.utypes, comment_file, "typedoc.utypes");
  comment_load(comments_storage.terrains, comment_file, "typedoc.terrains");
  comment_load(comments_storage.resources,
               comment_file, "typedoc.resources");
  comment_load(comments_storage.extras, comment_file, "typedoc.extras");
  comment_load(comments_storage.bases, comment_file, "typedoc.bases");
  comment_load(comments_storage.roads, comment_file, "typedoc.roads");
  comment_load(comments_storage.styles, comment_file, "typedoc.styles");
  comment_load(comments_storage.citystyles,
               comment_file, "typedoc.citystyles");
  comment_load(comments_storage.musicstyles,
               comment_file, "typedoc.musicstyles");
  comment_load(comments_storage.effects, comment_file, "typedoc.effects");
  comment_load(comments_storage.ueffs, comment_file, "typedoc.ueffs");
  comment_load(comments_storage.disasters,
               comment_file, "typedoc.disasters");
  comment_load(comments_storage.achievements,
               comment_file, "typedoc.achievements");
  comment_load(comments_storage.uflags_utype,
               comment_file, "uflag_types.utype");
  comment_load(comments_storage.uflags_uclass,
               comment_file, "uflag_types.uclass");
  comment_load(comments_storage.uflags_terrain,
               comment_file, "uflag_types.terrain");
  comment_load(comments_storage.uflags_extra,
               comment_file, "uflag_types.extra");
  comment_load(comments_storage.uflags_tech,
               comment_file, "uflag_types.tech");
  comment_load(comments_storage.uflags_building,
               comment_file, "uflag_types.building");
  comment_load(comments_storage.trade_settings,
               comment_file, "typedoc.trade_settings");
  comment_load(comments_storage.goods, comment_file, "typedoc.goods");
  comment_load(comments_storage.actions, comment_file, "typedoc.actions");
  comment_load(comments_storage.enablers, comment_file, "typedoc.enablers");
  comment_load(comments_storage.specialists,
               comment_file, "typedoc.specialists");
  comment_load(comments_storage.nations, comment_file, "typedoc.nations");
  comment_load(comments_storage.nationgroups,
               comment_file, "typedoc.nationgroups");
  comment_load(comments_storage.nationsets,
               comment_file, "typedoc.nationsets");
  comment_load(comments_storage.clauses, comment_file, "typedoc.clauses");
  comment_load(comments_storage.counters, comment_file,
               "typedoc.counters");

  comment_load(comments_storage.nations_ruledit, comment_file,
               "sectiondoc.nations_ruledit");

  comment_load(comments_storage.civstyle_granary, comment_file,
               "entrydoc.granary");
  comment_load(comments_storage.civstyle_ransom_gold, comment_file,
               "entrydoc.ransom_gold");
  comment_load(comments_storage.civstyle_gameloss_style, comment_file,
               "entrydoc.gameloss_style");
  comment_load(comments_storage.civstyle_gold_upkeep_style, comment_file,
               "entrydoc.gold_upkeep_style");
  comment_load(comments_storage.civstyle_homeless_gold_upkeep, comment_file,
               "entrydoc.homeless_gold_upkeep");
  comment_load(comments_storage.civstyle_airlift_always, comment_file,
               "entrydoc.airlift_always_enabled");
  comment_load(comments_storage.wonder_visibility_small_wonders, comment_file,
               "entrydoc.wv_small_wonders");
  comment_load(comments_storage.incite_cost, comment_file,
               "entrydoc.incite_cost");
  comment_load(comments_storage.combat_rules_tired_attack, comment_file,
               "entrydoc.tired_attack");
  comment_load(comments_storage.combat_rules_only_killing_veteran, comment_file,
               "entrydoc.only_killing_makes_veteran");
  comment_load(comments_storage.combat_rules_only_real_fight_veteran, comment_file,
               "entrydoc.only_real_fight_makes_veteran");
  comment_load(comments_storage.combat_rules_scaled_veterancy, comment_file,
               "entrydoc.combat_odds_scaled_veterancy");
  comment_load(comments_storage.combat_rules_damage_reduces_bombard_rate, comment_file,
               "entrydoc.damage_reduces_bombard_rate");
  comment_load(comments_storage.combat_rules_low_fp_badwallattacker, comment_file,
               "entrydoc.low_firepower_badwallattacker");
  comment_load(comments_storage.combat_rules_low_fp_pearl_harbour, comment_file,
               "entrydoc.low_firepower_pearl_harbour");
  comment_load(comments_storage.combat_rules_low_fp_combat_bonus, comment_file,
               "entrydoc.low_firepower_combat_bonus");
  comment_load(comments_storage.combat_rules_low_fp_nonnat_bombard, comment_file,
               "entrydoc.low_firepower_nonnat_bombard");
  comment_load(comments_storage.combat_rules_nuke_pop_loss, comment_file,
               "entrydoc.nuke_pop_loss_pct");
  comment_load(comments_storage.combat_rules_nuke_defender_survival,
               comment_file,
               "entrydoc.nuke_defender_survival_chance_pct");
  comment_load(comments_storage.auto_attack, comment_file,
               "entrydoc.auto_attack");
  comment_load(comments_storage.actions_dc_initial_odds, comment_file,
               "entrydoc.dc_initial_odds");
  comment_load(comments_storage.actions_quiet_actions, comment_file,
               "entrydoc.quiet_actions");
  comment_load(comments_storage.borders_radius_permanent, comment_file,
               "entrydoc.radius_permanent");
  comment_load(comments_storage.research_tech_cost_style, comment_file,
               "entrydoc.tech_cost_style");
  comment_load(comments_storage.research_base_tech_cost, comment_file,
               "entrydoc.base_tech_cost");
  comment_load(comments_storage.research_min_tech_cost, comment_file,
               "entrydoc.min_tech_cost");
  comment_load(comments_storage.research_tech_leakage, comment_file,
               "entrydoc.tech_leakage");
  comment_load(comments_storage.research_upkeep_style, comment_file,
               "entrydoc.tech_upkeep_style");
  comment_load(comments_storage.research_free_tech_method, comment_file,
               "entrydoc.free_tech_method");
  comment_load(comments_storage.culture_history_interest, comment_file,
               "entrydoc.history_interest_pml");
  comment_load(comments_storage.culture_migration_pml, comment_file,
               "entrydoc.migration_pml");
  comment_load(comments_storage.world_peace_turns, comment_file,
               "entrydoc.world_peace_turns");
  comment_load(comments_storage.calendar_fragments, comment_file,
               "entrydoc.calendar_fragments");
  comment_load(comments_storage.std_tileset_compat, comment_file,
               "entrydoc.std_tileset_compat");

  secfile_check_unused(comment_file);
  secfile_destroy(comment_file);

  return TRUE;
}

/**********************************************************************//**
  Free comments.
**************************************************************************/
void comments_free(void)
{
  free(comments_storage.file_header);
}

/**********************************************************************//**
  Generic comment section writing function with some error checking.
**************************************************************************/
static void comment_write(struct section_file *sfile, const char *comment,
                          const char *name)
{
  if (comment == NULL) {
    log_error(_("Comment for %s missing."), name);
    return;
  }

  secfile_insert_long_comment(sfile, comment);
}

/**********************************************************************//**
  Generic comment entry writing function with some error checking.
**************************************************************************/
static void comment_entry_write(struct section_file *sfile,
                                const char *comment, const char *section)
{
  if (comment == NULL) {
    log_error(_("Comment to section %s missing."), section);
    return;
  }

  secfile_insert_comment(sfile, comment, "%s", section);
}

/**********************************************************************//**
  Write file header.
**************************************************************************/
void comment_file_header(struct section_file *sfile)
{
  comment_write(sfile, comments_storage.file_header, "File header");
}

/**********************************************************************//**
  Write buildings header.
**************************************************************************/
void comment_buildings(struct section_file *sfile)
{
  comment_write(sfile, comments_storage.buildings, "Buildings");
}

/**********************************************************************//**
  Write tech classes' header.
**************************************************************************/
void comment_tech_classes(struct section_file *sfile)
{
  comment_write(sfile, comments_storage.tech_classes, "Tech Classes");
}

/**********************************************************************//**
  Write techs header.
**************************************************************************/
void comment_techs(struct section_file *sfile)
{
  comment_write(sfile, comments_storage.techs, "Techs");
}

/**********************************************************************//**
  Write governments header.
**************************************************************************/
void comment_govs(struct section_file *sfile)
{
  comment_write(sfile, comments_storage.govs, "Governments");
}

/**********************************************************************//**
  Write policies header.
**************************************************************************/
void comment_policies(struct section_file *sfile)
{
  comment_write(sfile, comments_storage.policies, "Policies");
}

/**********************************************************************//**
  Write unit classes header.
**************************************************************************/
void comment_uclasses(struct section_file *sfile)
{
  comment_write(sfile, comments_storage.uclasses, "Unit classes");
}

/**********************************************************************//**
  Write unit types header.
**************************************************************************/
void comment_utypes(struct section_file *sfile)
{
  comment_write(sfile, comments_storage.utypes, "Unit types");
}

/**********************************************************************//**
  Write terrains header.
**************************************************************************/
void comment_terrains(struct section_file *sfile)
{
  comment_write(sfile, comments_storage.terrains, "Terrains");
}

/**********************************************************************//**
  Write resources header.
**************************************************************************/
void comment_resources(struct section_file *sfile)
{
  comment_write(sfile, comments_storage.resources, "Resources");
}

/**********************************************************************//**
  Write extras header.
**************************************************************************/
void comment_extras(struct section_file *sfile)
{
  comment_write(sfile, comments_storage.extras, "Extras");
}

/**********************************************************************//**
  Write bases header.
**************************************************************************/
void comment_bases(struct section_file *sfile)
{
  comment_write(sfile, comments_storage.bases, "Bases");
}

/**********************************************************************//**
  Write roads header.
**************************************************************************/
void comment_roads(struct section_file *sfile)
{
  comment_write(sfile, comments_storage.roads, "Roads");
}

/**********************************************************************//**
  Write styles header.
**************************************************************************/
void comment_styles(struct section_file *sfile)
{
  comment_write(sfile, comments_storage.styles, "Styles");
}

/**********************************************************************//**
  Write city styles header.
**************************************************************************/
void comment_citystyles(struct section_file *sfile)
{
  comment_write(sfile, comments_storage.citystyles, "City Styles");
}

/**********************************************************************//**
  Write music styles header.
**************************************************************************/
void comment_musicstyles(struct section_file *sfile)
{
  comment_write(sfile, comments_storage.musicstyles, "Music Styles");
}

/**********************************************************************//**
  Write effects header.
**************************************************************************/
void comment_effects(struct section_file *sfile)
{
  comment_write(sfile, comments_storage.effects, "Effects");
}

/**********************************************************************//**
  Write User effects header.
**************************************************************************/
void comment_ueffs(struct section_file *sfile)
{
  comment_write(sfile, comments_storage.ueffs, "User Effects");
}

/**********************************************************************//**
  Write disasters header.
**************************************************************************/
void comment_disasters(struct section_file *sfile)
{
  comment_write(sfile, comments_storage.disasters, "Disasters");
}

/**********************************************************************//**
  Write achievements header.
**************************************************************************/
void comment_achievements(struct section_file *sfile)
{
  comment_write(sfile, comments_storage.achievements, "Achievements");
}

/**********************************************************************//**
  Write header for unit type user flags.
**************************************************************************/
void comment_uflags_utype(struct section_file *sfile)
{
  comment_entry_write(sfile, comments_storage.uflags_utype, "control");
}

/**********************************************************************//**
  Write header for unit class user flags.
**************************************************************************/
void comment_uflags_uclass(struct section_file *sfile)
{
  comment_entry_write(sfile, comments_storage.uflags_uclass, "control");
}

/**********************************************************************//**
  Write header for terrain user flags.
**************************************************************************/
void comment_uflags_terrain(struct section_file *sfile)
{
  comment_entry_write(sfile, comments_storage.uflags_terrain, "control");
}

/**********************************************************************//**
  Write header for extra user flags.
**************************************************************************/
void comment_uflags_extra(struct section_file *sfile)
{
  comment_entry_write(sfile, comments_storage.uflags_extra, "control");
}

/**********************************************************************//**
  Write header for tech user flags.
**************************************************************************/
void comment_uflags_tech(struct section_file *sfile)
{
  comment_entry_write(sfile, comments_storage.uflags_tech, "control");
}

/**********************************************************************//**
  Write header for building user flags.
**************************************************************************/
void comment_uflags_building(struct section_file *sfile)
{
  comment_entry_write(sfile, comments_storage.uflags_building, "control");
}

/**********************************************************************//**
  Write trade settings header.
**************************************************************************/
void comment_trade_settings(struct section_file *sfile)
{
  comment_write(sfile, comments_storage.trade_settings, "Trade settings");
}

/**********************************************************************//**
  Write goods header.
**************************************************************************/
void comment_goods(struct section_file *sfile)
{
  comment_write(sfile, comments_storage.goods, "Goods");
}

/**********************************************************************//**
  Write actions header.
**************************************************************************/
void comment_actions(struct section_file *sfile)
{
  comment_write(sfile, comments_storage.actions, "Actions");
}

/**********************************************************************//**
  Write action enablers header.
**************************************************************************/
void comment_enablers(struct section_file *sfile)
{
  comment_write(sfile, comments_storage.enablers, "Action Enablers");
}

/**********************************************************************//**
  Write specialists header.
**************************************************************************/
void comment_specialists(struct section_file *sfile)
{
  comment_write(sfile, comments_storage.specialists, "Specialists");
}

/**********************************************************************//**
  Write nations header.
**************************************************************************/
void comment_nations(struct section_file *sfile)
{
  comment_write(sfile, comments_storage.nations, "Nations");
}

/**********************************************************************//**
  Write nationgroups header.
**************************************************************************/
void comment_nationgroups(struct section_file *sfile)
{
  comment_write(sfile, comments_storage.nationgroups, "Nationgroups");
}

/**********************************************************************//**
  Write nationsets header.
**************************************************************************/
void comment_nationsets(struct section_file *sfile)
{
  comment_write(sfile, comments_storage.nationsets, "Nationsets");
}

/**********************************************************************//**
  Write clauses header.
**************************************************************************/
void comment_clauses(struct section_file *sfile)
{
  comment_write(sfile, comments_storage.clauses, "Clauses");
}

/**********************************************************************//**
  Write counters comment header.
**************************************************************************/
void comment_counters(struct section_file *sfile)
{
  comment_write(sfile, comments_storage.counters,
                      "counters");
}

/**********************************************************************//**
  Write nations.ruleset [ruledit] section header.
**************************************************************************/
void comment_nations_ruledit(struct section_file *sfile)
{
  comment_write(sfile, comments_storage.nations_ruledit, "Ruledit");
}

/**********************************************************************//**
  Write civstyle granary settings header.
**************************************************************************/
void comment_civstyle_granary(struct section_file *sfile)
{
  comment_entry_write(sfile, comments_storage.civstyle_granary,
                      "civstyle");
}

/**********************************************************************//**
  Write civstyle ransom_gold settings header.
**************************************************************************/
void comment_civstyle_ransom_gold(struct section_file *sfile)
{
  comment_entry_write(sfile, comments_storage.civstyle_ransom_gold,
                      "civstyle");
}

/**********************************************************************//**
  Write civstyle gameloss_style settings header.
**************************************************************************/
void comment_civstyle_gameloss_style(struct section_file *sfile)
{
  comment_entry_write(sfile, comments_storage.civstyle_gameloss_style,
                      "civstyle");
}

/**********************************************************************//**
  Write civstyle gold_upkeep_style settings header.
**************************************************************************/
void comment_civstyle_gold_upkeep_style(struct section_file *sfile)
{
  comment_entry_write(sfile, comments_storage.civstyle_gold_upkeep_style,
                      "civstyle");
}

/**********************************************************************//**
  Write civstyle homeless_gold_upkeep settings header.
**************************************************************************/
void comment_civstyle_homeless_gold_upkeep(struct section_file *sfile)
{
  comment_entry_write(sfile, comments_storage.civstyle_homeless_gold_upkeep,
                      "civstyle");
}

/**********************************************************************//**
  Write civstyle airlift always enabled settings header.
**************************************************************************/
void comment_civstyle_airlift_always(struct section_file *sfile)
{
  comment_entry_write(sfile, comments_storage.civstyle_airlift_always,
                      "civstyle");
}

/**********************************************************************//**
  Write wonder_visibility small_wonders settings header.
**************************************************************************/
void comment_wonder_visibility_small_wonders(struct section_file *sfile)
{
  comment_entry_write(sfile, comments_storage.wonder_visibility_small_wonders,
                      "wonder_visibility");
}

/**********************************************************************//**
  Write incite_cost settings header.
**************************************************************************/
void comment_incite_cost(struct section_file *sfile)
{
  comment_entry_write(sfile, comments_storage.incite_cost,
                      "incite_cost");
}

/**********************************************************************//**
  Write combat_rules tired_attack settings header.
**************************************************************************/
void comment_combat_rules_tired_attack(struct section_file *sfile)
{
  comment_entry_write(sfile, comments_storage.combat_rules_tired_attack,
                      "combat_rules");
}

/**********************************************************************//**
  Write combat_rules only_killing_makes_veteran settings header.
**************************************************************************/
void comment_combat_rules_only_killing_veteran(struct section_file *sfile)
{
  comment_entry_write(sfile,
                      comments_storage.combat_rules_only_killing_veteran,
                      "combat_rules");
}

/**********************************************************************//**
  Write combat_rules only_real_fight_makes_veteran settings header.
**************************************************************************/
void comment_combat_rules_only_real_fight_veteran(struct section_file *sfile)
{
  comment_entry_write(sfile,
                      comments_storage.combat_rules_only_real_fight_veteran,
                      "combat_rules");
}

/**********************************************************************//**
  Write combat_rules combat_odds_scaled_veterancy settings header.
**************************************************************************/
void comment_combat_rules_scaled_veterancy(struct section_file *sfile)
{
  comment_entry_write(sfile,
                      comments_storage.combat_rules_scaled_veterancy,
                      "combat_rules");
}

/**********************************************************************//**
  Write combat_rules damage_reduces_bombard_rate settings header.
**************************************************************************/
void comment_combat_rules_damage_reduces_bombard_rate(struct section_file *sfile)
{
  comment_entry_write(sfile,
                      comments_storage.combat_rules_damage_reduces_bombard_rate,
                      "combat_rules");
}

/**********************************************************************//**
  Write combat_rules low_firepower_badwallattacker settings header.
**************************************************************************/
void comment_combat_rules_low_fp_badwallattacker(struct section_file *sfile)
{
  comment_entry_write(sfile,
                      comments_storage.combat_rules_low_fp_badwallattacker,
                      "combat_rules");
}

/**********************************************************************//**
  Write combat_rules low_firepower_pearl_harbour settings header.
**************************************************************************/
void comment_combat_rules_low_fp_pearl_harbour(struct section_file *sfile)
{
  comment_entry_write(sfile,
                      comments_storage.combat_rules_low_fp_pearl_harbour,
                      "combat_rules");
}

/**********************************************************************//**
  Write combat_rules low_firepower_combat_bonus settings header.
**************************************************************************/
void comment_combat_rules_low_fp_combat_bonus(struct section_file *sfile)
{
  comment_entry_write(sfile,
                      comments_storage.combat_rules_low_fp_combat_bonus,
                      "combat_rules");
}

/**********************************************************************//**
  Write combat_rules low_firepower_nonnat_bombard settings header.
**************************************************************************/
void comment_combat_rules_low_fp_nonnat_bombard(struct section_file *sfile)
{
  comment_entry_write(sfile,
                      comments_storage.combat_rules_low_fp_nonnat_bombard,
                      "combat_rules");
}

/**********************************************************************//**
  Write combat_rules nuke_pop_loss_pct settings header.
**************************************************************************/
void comment_combat_rules_nuke_pop_loss(struct section_file *sfile)
{
  comment_entry_write(sfile, comments_storage.combat_rules_nuke_pop_loss,
                      "combat_rules");
}

/**********************************************************************//**
  Write combat_rules nuke_defender_survival_chance_pct settings header.
**************************************************************************/
void comment_combat_rules_nuke_defender_survival(struct section_file *sfile)
{
  comment_entry_write(sfile,
                      comments_storage.combat_rules_nuke_defender_survival,
                      "combat_rules");
}

/**********************************************************************//**
  Write auto_attack settings header.
**************************************************************************/
void comment_auto_attack(struct section_file *sfile)
{
  comment_entry_write(sfile, comments_storage.auto_attack, "auto_attack");
}

/**********************************************************************//**
  Write actions diplchance_initial_odds settings header.
**************************************************************************/
void comment_actions_dc_initial_odds(struct section_file *sfile)
{
  comment_entry_write(sfile, comments_storage.actions_dc_initial_odds,
                      "actions");
}

/**********************************************************************//**
  Write actions quiet_actions settings header.
**************************************************************************/
void comment_actions_quiet_actions(struct section_file *sfile)
{
  comment_entry_write(sfile, comments_storage.actions_quiet_actions,
                      "actions");
}

/**********************************************************************//**
  Write borders radius_sq_city_permanent settings header.
**************************************************************************/
void comment_borders_radius_permanent(struct section_file *sfile)
{
  comment_entry_write(sfile, comments_storage.borders_radius_permanent,
                      "borders");
}

/**********************************************************************//**
  Write research tech_cost_style settings header.
**************************************************************************/
void comment_research_tech_cost_style(struct section_file *sfile)
{
  comment_entry_write(sfile, comments_storage.research_tech_cost_style,
                      "research");
}

/**********************************************************************//**
  Write research base_tech_cost settings header.
**************************************************************************/
void comment_research_base_tech_cost(struct section_file *sfile)
{
  comment_entry_write(sfile, comments_storage.research_base_tech_cost,
                      "research");
}

/**********************************************************************//**
  Write research min_tech_cost settings header.
**************************************************************************/
void comment_research_min_tech_cost(struct section_file *sfile)
{
  comment_entry_write(sfile, comments_storage.research_min_tech_cost,
                      "research");
}

/**********************************************************************//**
  Write research tech_leakage settings header.
**************************************************************************/
void comment_research_tech_leakage(struct section_file *sfile)
{
  comment_entry_write(sfile, comments_storage.research_tech_leakage,
                      "research");
}

/**********************************************************************//**
  Write research tech_upkeep_style settings header.
**************************************************************************/
void comment_research_upkeep_style(struct section_file *sfile)
{
  comment_entry_write(sfile, comments_storage.research_upkeep_style,
                      "research");
}

/**********************************************************************//**
  Write research free_tech_method settings header.
**************************************************************************/
void comment_research_free_tech_method(struct section_file *sfile)
{
  comment_entry_write(sfile, comments_storage.research_free_tech_method,
                      "research");
}

/**********************************************************************//**
  Write culture history_interest_pml settings header.
**************************************************************************/
void comment_culture_history_interest(struct section_file *sfile)
{
  comment_entry_write(sfile, comments_storage.culture_history_interest,
                      "culture");
}

/**********************************************************************//**
  Write culture migration_pml settings header.
**************************************************************************/
void comment_culture_migration_pml(struct section_file *sfile)
{
  comment_entry_write(sfile, comments_storage.culture_migration_pml,
                      "culture");
}

/**********************************************************************//**
  Write world peace turns settings header.
**************************************************************************/
void comment_world_peace_turns(struct section_file *sfile)
{
  comment_entry_write(sfile, comments_storage.world_peace_turns,
                      "world_peace");
}

/**********************************************************************//**
  Write calendar fragments settings header.
**************************************************************************/
void comment_calendar_fragments(struct section_file *sfile)
{
  comment_entry_write(sfile, comments_storage.calendar_fragments,
                      "calendar");
}


/**********************************************************************//**
  Write std_tileset_compat settings header.
**************************************************************************/
void comment_std_tileset_compat(struct section_file *sfile)
{
  comment_entry_write(sfile, comments_storage.std_tileset_compat,
                      "std_tileset_compat");
}
