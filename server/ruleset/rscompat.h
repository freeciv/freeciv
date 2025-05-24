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

#define RULESET_COMPAT_CAP "+Freeciv-ruleset-3.3-Devel-2023.Feb.24"

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
                                           const char *sec,
                                           const char *sub,
                                           const char *rfor);

/* Functions specific to 3.3 -> 3.4 transition */
const char *rscompat_effect_name_3_4(const char *old_name);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* FC__RSCOMPAT_H */
