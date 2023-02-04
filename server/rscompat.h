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
#include "settings.h"

#define RULESET_COMPAT_CAP "+Freeciv-3.1-ruleset"

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

/* Functions specific to 3.1 -> 3.2 transition */
enum impr_genus_id rscompat_genus_3_2(struct rscompat_info *compat,
                                      const bv_impr_flags flags,
                                      enum impr_genus_id old_genus);
const char *rscompat_req_range_3_2(struct rscompat_info *compat,
                                   const char *type,
                                   const char *old_range);
void rscompat_req_adjust_3_2(const struct rscompat_info *compat,
                             const char **ptype, const char **pname,
                             bool *ppresent, const char *sec_name);
int add_user_extra_flags_3_2(int start);
void rscompat_extra_adjust_3_2(struct extra_type *pextra);
bool rscompat_setting_needs_special_handling(const char *name);
void rscompat_settings_do_special_handling(struct section_file *file,
                const char *section, void (*setdef)(struct setting *pset));
bool rscompat_terrain_extra_rmtime_3_2(struct section_file *file,
                                       const char *tsection,
                                       struct terrain *pterrain);
const char *rscompat_action_ui_name_S3_2(struct rscompat_info *compat,
                                         int act_id);

/* In ruleset.c, but should not be in public interface - make static again once
 * rscompat.c no longer needs. */
bool lookup_time(const struct section_file *secfile, int *turns,
                 const char *sec_name, const char *property_name,
                 const char *filename, const char *item_name,
                 bool *ok);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* FC__RSCOMPAT_H */
