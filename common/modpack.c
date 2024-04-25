/****************************************************************************
 Freeciv - Copyright (C) 2004 - The Freeciv Team
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
****************************************************************************/

#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

/* utility */
#include "capability.h"
#include "registry.h"
#include "section_file.h"
#include "shared.h"

/* common */
#include "game.h"

#include "modpack.h"

struct modpack_cache_item {
  char *modpack_name;
  char *filename;
  char *target;
};

/* Get 'struct modpack_cache_list' and related functions: */
#define SPECLIST_TAG modpack_cache
#define SPECLIST_TYPE struct modpack_cache_item
#include "speclist.h"

#define modpack_cache_iterate(mplist, item) \
    TYPED_LIST_ITERATE(struct modpack_cache_item, mplist, item)
#define modpack_cache_iterate_end LIST_ITERATE_END

static struct modpack_cache_list *modpack_rulesets;
static struct modpack_cache_list *modpack_tilesets;

/************************************************************************//**
  Initialize modpacks system
****************************************************************************/
void modpacks_init(void)
{
  if (is_server()) {
    modpack_rulesets = modpack_cache_list_new();
  } else {
    modpack_tilesets = modpack_cache_list_new();
  }
}

/************************************************************************//**
  Free the memory associated with modpacks system
****************************************************************************/
void modpacks_free(void)
{
  if (is_server()) {
    modpack_cache_iterate(modpack_rulesets, item) {
      free(item->modpack_name);
      free(item->filename);
      free(item);
    } modpack_cache_iterate_end;

    modpack_cache_list_destroy(modpack_rulesets);
  } else {
    modpack_cache_iterate(modpack_tilesets, item) {
      free(item->modpack_name);
      free(item->filename);
      free(item->target);
      free(item);
    } modpack_cache_iterate_end;

    modpack_cache_list_destroy(modpack_tilesets);
  }
}

/************************************************************************//**
  Check modpack file capabilities.

  @param file      Modpack section file
  @param us_capstr Engine capabilities
  @param filename  Modpack filename
  @param verbose   Whether logging should be verbose
****************************************************************************/
bool modpack_check_capabilities(struct section_file *file, const char *us_capstr,
                                const char *filename, bool verbose)
{
  enum log_level level = verbose ? LOG_WARN : LOG_DEBUG;

  const char *file_capstr = secfile_lookup_str(file, "datafile.options");

  if (file_capstr == nullptr) {
    log_base(level, "\"%s\": file doesn't have a capability string",
             filename);
    return FALSE;
  }
  if (!has_capabilities(us_capstr, file_capstr)) {
    log_base(level, "\"%s\": file appears incompatible:",
             filename);
    log_base(level, "  datafile options: %s", file_capstr);
    log_base(level, "  supported options: %s", us_capstr);
    return FALSE;
  }
  if (!has_capabilities(file_capstr, us_capstr)) {
    log_base(level, "\"%s\": file requires option(s) "
             "that freeciv doesn't support:", filename);
    log_base(level, "  datafile options: %s", file_capstr);
    log_base(level, "  supported options: %s", us_capstr);
    return FALSE;
  }

  return TRUE;
}

/************************************************************************//**
  Get list of modpack meta info files.

  @return List of available .modpack files
****************************************************************************/
struct fileinfo_list *get_modpacks_list(void)
{
  struct fileinfo_list *files;

  /* Search for modpack files. */
  files = fileinfolist_infix(get_data_dirs(), MODPACK_SUFFIX, TRUE);

  return files;
}

/************************************************************************//**
  Return name of the modpack if it contains a ruleset. If it does not contain
  ruleset, return nullptr.

  @param sf Modpack section file
  @return   Name of the modpack, or nullptr
****************************************************************************/
const char *modpack_has_ruleset(struct section_file *sf)
{
  if (sf != nullptr) {
    if (!modpack_check_capabilities(sf, MODPACK_CAPSTR, sf->name,
                                    FALSE)) {
      return nullptr;
    }

    if (secfile_lookup_bool_default(sf, FALSE, "components.ruleset")) {
      return secfile_lookup_str_default(sf, nullptr, "modpack.name");
    }
  }

  return nullptr;
}

/************************************************************************//**
  Return name of the modpack if it contains a tileset. If it does not contain
  ruleset, return nullptr.

  @param sf Modpack section file
  @return   Name of the modpack, or nullptr
****************************************************************************/
const char *modpack_has_tileset(struct section_file *sf)
{
  if (sf != nullptr) {
    if (!modpack_check_capabilities(sf, MODPACK_CAPSTR, sf->name,
                                    FALSE)) {
      return nullptr;
    }

    if (secfile_lookup_bool_default(sf, FALSE, "components.tileset")) {
      return secfile_lookup_str_default(sf, nullptr, "modpack.name");
    }
  }

  return nullptr;
}

/************************************************************************//**
  Return .serv file name for the modpack, if any.

  @param sf Modpack section file
  @return   Server script name, or nullptr
****************************************************************************/
const char *modpack_serv_file(struct section_file *sf)
{
  if (sf != nullptr) {
    return secfile_lookup_str_default(sf, nullptr, "ruleset.serv");
  }

  return nullptr;
}

/************************************************************************//**
  Return rulesetdir for the modpack, if any.

  @param sf Modpack section file
  @return   Rulesetdir, or nullptr
****************************************************************************/
const char *modpack_rulesetdir(struct section_file *sf)
{
  if (sf != nullptr) {
    return secfile_lookup_str_default(sf, nullptr, "ruleset.rulesetdir");
  }

  return nullptr;
}

/************************************************************************//**
  Return tilespec for the modpack, if any.

  @param sf Modpack section file
  @return   Tilespec file name, or nullptr
****************************************************************************/
const char *modpack_tilespec(struct section_file *sf)
{
  if (sf != nullptr) {
    return secfile_lookup_str_default(sf, nullptr, "tileset.tilespec");
  }

  return nullptr;
}

/************************************************************************//**
  Add modpack/ruleset mapping to cache, if modpack has ruleset.
  Return name of the modpack, if the mapping exist.

  @param sf Modpack section file to cache
  @return   Name of the modpack, or nullptr
****************************************************************************/
const char *modpack_cache_ruleset(struct section_file *sf)
{
  const char *mp_name = modpack_has_ruleset(sf);
  struct modpack_cache_item *item;

  if (mp_name == nullptr) {
    return nullptr;
  }

  fc_assert(sf->name != nullptr);

  item = fc_malloc(sizeof(struct modpack_cache_item));
  item->modpack_name = fc_strdup(mp_name);
  item->filename = fc_strdup(sf->name);

  modpack_cache_list_append(modpack_rulesets, item);

  return mp_name;
}

/************************************************************************//**
  Add modpack/ruleset mapping to cache, if modpack has tileset.
  Return name of the modpack, if the mapping exist.

  @param sf Modpack section file to cache
  @return   Name of the modpack, or nullptr
****************************************************************************/
const char *modpack_cache_tileset(struct section_file *sf)
{
  const char *mp_name = modpack_has_tileset(sf);
  struct modpack_cache_item *item;
  const char *target;

  if (mp_name == nullptr) {
    return nullptr;
  }

  fc_assert(sf->name != nullptr);

  item = fc_malloc(sizeof(struct modpack_cache_item));
  item->modpack_name = fc_strdup(mp_name);
  item->filename = fc_strdup(sf->name);
  target = secfile_lookup_str_default(sf, nullptr, "tileset.tilespec");
  if (target != nullptr) {
    item->target = fc_strdup(target);
  } else {
    item->target = nullptr;
  }

  modpack_cache_list_append(modpack_tilesets, item);

  return mp_name;
}

/************************************************************************//**
  Find filename by name of the modpack, from the ruleset cache

  @param name Modpack name
  @return     Filename of the modpack
****************************************************************************/
const char *modpack_file_from_ruleset_cache(const char *name)
{
  modpack_cache_iterate(modpack_rulesets, item) {
    if (!fc_strcasecmp(name, item->modpack_name)) {
      return item->filename;
    }
  } modpack_cache_iterate_end;

  return nullptr;
}

/************************************************************************//**
  Find filename by name of the modpack, from the tileset cache

  @param name Modpack name
  @return     Filename of the modpack
****************************************************************************/
const char *modpack_file_from_tileset_cache(const char *name)
{
  modpack_cache_iterate(modpack_tilesets, item) {
    if (!fc_strcasecmp(name, item->modpack_name)) {
      return item->filename;
    }
  } modpack_cache_iterate_end;

  return nullptr;
}

/************************************************************************//**
  Find target by name of the modpack, from the tileset cache

  @param name Modpack name
  @return     Target of the modpack
****************************************************************************/
const char *modpack_tileset_target(const char *name)
{
  modpack_cache_iterate(modpack_tilesets, item) {
    if (!fc_strcasecmp(name, item->modpack_name)) {
      return item->target;
    }
  } modpack_cache_iterate_end;

  return nullptr;
}

/************************************************************************//**
  Call callback for each item in the ruleset cache.

  @param cb   Callback to call
  @param data Data to pass to the callback
****************************************************************************/
void modpack_ruleset_cache_iterate(mrc_cb cb, void *data)
{
  modpack_cache_iterate(modpack_rulesets, item) {
    cb(item->modpack_name, item->filename, data);
  } modpack_cache_iterate_end;
}


/************************************************************************//**
  Call callback for each item in the tileset cache.

  @param cb   Callback to call
  @param data Data to pass to the callback
****************************************************************************/
void modpack_tileset_cache_iterate(mrc_cb cb, void *data)
{
  modpack_cache_iterate(modpack_tilesets, item) {
    cb(item->modpack_name, item->filename, data);
  } modpack_cache_iterate_end;
}
