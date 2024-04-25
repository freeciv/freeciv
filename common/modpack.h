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
#ifndef FC__MODPACK_H
#define FC__MODPACK_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define MODPACK_CAPSTR "+Freeciv-modpack-3.3-Devel-2024.Apr.14"

#define MODPACK_SUFFIX ".modpack"

void modpacks_init(void);
void modpacks_free(void);

const char *modpack_cache_ruleset(struct section_file *sf);
const char *modpack_file_from_ruleset_cache(const char *name);

const char *modpack_cache_tileset(struct section_file *sf);
const char *modpack_file_from_tileset_cache(const char *name);
const char *modpack_tileset_target(const char *name);

struct fileinfo_list *get_modpacks_list(void);
const char *modpack_has_ruleset(struct section_file *sf);
const char *modpack_has_tileset(struct section_file *sf);

bool modpack_check_capabilities(struct section_file *file, const char *us_capstr,
                                const char *filename, bool verbose);

const char *modpack_serv_file(struct section_file *sf);
const char *modpack_rulesetdir(struct section_file *sf);
const char *modpack_tilespec(struct section_file *sf);

typedef void (*mrc_cb)(const char *, const char *, void *data);
void modpack_ruleset_cache_iterate(mrc_cb cb, void *data);
void modpack_tileset_cache_iterate(mrc_cb cb, void *data);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* FC__MODPACK_H */
