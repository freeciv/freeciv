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
#ifndef FC__RSCOMPAT_H
#define FC__RSCOMPAT_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* utility */
#include "support.h"

/* server */
#include "ruleset.h"

#define RULESET_COMPAT_CAP "+Freeciv-3.0-ruleset"

struct rscompat_info
{
  bool compat_mode;
  rs_conversion_logger log_cb;
  int version;
};

void rscompat_init_info(struct rscompat_info *info);

int rscompat_check_capabilities(struct section_file *file, const char *filename,
                                const struct rscompat_info *info);
bool rscompat_check_cap_and_version(struct section_file *file,
                                    const char *filename,
                                    const struct rscompat_info *info);

bool rscompat_names(struct rscompat_info *info);

void rscompat_adjust_pre_sanity(struct rscompat_info *info);
void rscompat_postprocess(struct rscompat_info *info);

/* General upgrade functions that should be kept to avoid regressions in
 * corner case handling. */
void rscompat_enablers_add_obligatory_hard_reqs(void);

/* Functions from ruleset.c made visible to rscompat.c */
struct requirement_vector *lookup_req_list(struct section_file *file,
                                           struct rscompat_info *compat,
                                           const char *sec,
                                           const char *sub,
                                           const char *rfor);

/* Functions specific to 3.0 -> 3.1 transition */
bool rscompat_auto_attack_3_1(struct rscompat_info *compat,
                              struct action_auto_perf *auto_perf,
                              size_t psize,
                              enum unit_type_flag_id *protecor_flag);
const char *rscompat_req_type_name_3_1(const char *old_type);
const char *rscompat_req_name_3_1(const char *type,
                                  const char *old_name);
void rscompat_req_vec_adjust_3_1(struct rscompat_info *compat,
                                 struct requirement_vector *preqs,
                                 int *reqs_count,
                                 const char *filename, const char *sec,
                                 const char *sub, const char *rfor);
const char *rscompat_utype_flag_name_3_1(struct rscompat_info *info,
                                         const char *old_type);
const char *rscompat_combat_bonus_name_3_1(struct rscompat_info *compat,
                                           const char *old_type);
bool rscompat_old_effect_3_1(const char *type, struct section_file *file,
                             const char *sec_name, struct rscompat_info *compat);
void rscompat_uclass_flags_3_1(struct rscompat_info *compat,
                               struct unit_class *pclass);
void rscompat_extra_adjust_3_1(struct rscompat_info *compat,
                               struct extra_type *pextra);
enum road_gui_type rscompat_road_gui_type_3_1(struct road_type *proad);
bool rscompat_old_slow_invasions_3_1(struct rscompat_info *compat,
                                     bool slow_invasions);

const char *rscompat_action_ui_name_S3_1(struct rscompat_info *compat,
                                         int act_id);
const char *rscompat_action_max_range_name_S3_1(struct rscompat_info *compat,
                                                int act_id);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif  /* FC__RSCOMPAT_H */
