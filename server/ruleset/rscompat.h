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
#include "ruleload.h"
#include "settings.h"

#define RULESET_COMPAT_CAP "+Freeciv-3.2-ruleset"

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

/* Functions specific to 3.2 -> 3.3 transition */
const char *rscompat_utype_flag_name_3_3(const char *old_name);
const char *rscompat_universal_name_3_3(const char *old_name);
const char *rscompat_effect_name_3_3(const char *old_name);

const char *blocked_by_old_name_3_3(const char *new_name);
const char *ui_name_old_name_3_3(const char *new_name);

void rscompat_civil_war_effects_3_3(struct section_file *game_rs);

bool load_action_ui_name_3_3(struct section_file *file, int act,
                             const char *entry_name,
                             struct rscompat_info *compat);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* FC__RSCOMPAT_H */
