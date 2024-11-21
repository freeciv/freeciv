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
#ifndef FC__COMMENTS_H
#define FC__COMMENTS_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define COMMENTS_FILE_NAME "comments-3.3.txt"

struct section_file;

bool comments_load(void);
void comments_free(void);


/* Section comments */
void comment_file_header(struct section_file *sfile);

void comment_buildings(struct section_file *sfile);
void comment_tech_classes(struct section_file *sfile);
void comment_techs(struct section_file *sfile);
void comment_govs(struct section_file *sfile);
void comment_policies(struct section_file *sfile);
void comment_uclasses(struct section_file *sfile);
void comment_utypes(struct section_file *sfile);
void comment_terrains(struct section_file *sfile);
void comment_resources(struct section_file *sfile);
void comment_extras(struct section_file *sfile);
void comment_bases(struct section_file *sfile);
void comment_roads(struct section_file *sfile);
void comment_styles(struct section_file *sfile);
void comment_citystyles(struct section_file *sfile);
void comment_musicstyles(struct section_file *sfile);
void comment_effects(struct section_file *sfile);
void comment_ueffs(struct section_file *sfile);
void comment_disasters(struct section_file *sfile);
void comment_achievements(struct section_file *sfile);
void comment_goods(struct section_file *sfile);
void comment_actions(struct section_file *sfile);
void comment_enablers(struct section_file *sfile);
void comment_specialists(struct section_file *sfile);
void comment_nationsets(struct section_file *sfile);
void comment_nationgroups(struct section_file *sfile);
void comment_nations(struct section_file *sfile);
void comment_clauses(struct section_file *sfile);

/* User (custom) flag types */
void comment_uflags_utype(struct section_file *sfile);
void comment_uflags_uclass(struct section_file *sfile);
void comment_uflags_terrain(struct section_file *sfile);
void comment_uflags_extra(struct section_file *sfile);
void comment_uflags_tech(struct section_file *sfile);
void comment_uflags_building(struct section_file *sfile);

/* Other section comments */
void comment_trade_settings(struct section_file *sfile);
void comment_nations_ruledit(struct section_file *sfile);

/* Entry comments */
void comment_civstyle_granary(struct section_file *sfile);
void comment_civstyle_ransom_gold(struct section_file *sfile);
void comment_civstyle_gameloss_style(struct section_file *sfile);
void comment_civstyle_gold_upkeep_style(struct section_file *sfile);
void comment_civstyle_homeless_gold_upkeep(struct section_file *sfile);
void comment_civstyle_airlift_always(struct section_file *sfile);
void comment_wonder_visibility_small_wonders(struct section_file *sfile);
void comment_incite_cost(struct section_file *sfile);
void comment_combat_rules_tired_attack(struct section_file *sfile);
void comment_combat_rules_only_killing_veteran(struct section_file *sfile);
void comment_combat_rules_only_real_fight_veteran(struct section_file *sfile);
void comment_combat_rules_scaled_veterancy(struct section_file *sfile);
void comment_combat_rules_damage_reduces_bombard_rate(struct section_file *sfile);
void comment_combat_rules_low_fp_badwallattacker(struct section_file *sfile);
void comment_combat_rules_low_fp_pearl_harbor(struct section_file *sfile);
void comment_combat_rules_low_fp_combat_bonus(struct section_file *sfile);
void comment_combat_rules_low_fp_nonnat_bombard(struct section_file *sfile);
void comment_combat_rules_nuke_pop_loss(struct section_file *sfile);
void comment_combat_rules_nuke_defender_survival(struct section_file *sfile);
void comment_auto_attack(struct section_file *sfile);
void comment_actions_dc_initial_odds(struct section_file *sfile);
void comment_actions_quiet_actions(struct section_file *sfile);
void comment_borders_radius_permanent(struct section_file *sfile);
void comment_research_tech_cost_style(struct section_file *sfile);
void comment_research_base_tech_cost(struct section_file *sfile);
void comment_research_min_tech_cost(struct section_file *sfile);
void comment_research_tech_leakage(struct section_file *sfile);
void comment_research_upkeep_style(struct section_file *sfile);
void comment_research_free_tech_method(struct section_file *sfile);
void comment_culture_history_interest(struct section_file *sfile);
void comment_culture_migration_pml(struct section_file *sfile);
void comment_world_peace_turns(struct section_file *sfile);
void comment_calendar_fragments(struct section_file *sfile);
void comment_std_tileset_compat(struct section_file *sfile);
void comment_counters(struct section_file *sfile);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* FC__COMMENTS_H */
