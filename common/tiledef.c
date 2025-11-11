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

/* common */
#include "game.h"
#include "map.h"

#include "tiledef.h"

static struct tiledef tiledefs[MAX_TILEDEFS];

/************************************************************************//**
  Initialize tiledef structures.
****************************************************************************/
void tiledefs_init(void)
{
  int i;

  for (i = 0; i < MAX_TILEDEFS; i++) {
    tiledefs[i].id = i;
    tiledefs[i].extras = extra_type_list_new();
  }
}

/************************************************************************//**
  Free the memory associated with tiledef
****************************************************************************/
void tiledefs_free(void)
{
  int i;

  for (i = 0; i < MAX_TILEDEFS; i++) {
    extra_type_list_destroy(tiledefs[i].extras);
  }
}

/************************************************************************//**
  Return the number of tiledef_types
****************************************************************************/
int tiledef_count(void)
{
  return game.control.num_tiledef_types;
}

/************************************************************************//**
  Return the tiledef id.
****************************************************************************/
int tiledef_number(const struct tiledef *td)
{
  fc_assert_ret_val(td != nullptr, -1);

  return td->id;
}

#ifndef tiledef_index
/************************************************************************//**
  Return the tiledef index.
****************************************************************************/
int tiledef_index(const struct tiledef *td)
{
  fc_assert_ret_val(td != nullptr, -1);

  return td - tiledefs;
}
#endif /* tiledef_index */

/************************************************************************//**
  Return tiledef type of given id.
****************************************************************************/
struct tiledef *tiledef_by_number(int id)
{
  fc_assert_ret_val(id >= 0 && id < MAX_TILEDEFS, nullptr);

  return &tiledefs[id];
}

/************************************************************************//**
  Return the (translated) name of the tiledef.
  You don't have to free the return pointer.
****************************************************************************/
const char *tiledef_name_translation(const struct tiledef *td)
{
  return name_translation_get(&td->name);
}

/************************************************************************//**
  Return the (untranslated) rule name of the tiledef.
  You don't have to free the return pointer.
****************************************************************************/
const char *tiledef_rule_name(const struct tiledef *td)
{
  return rule_name_get(&td->name);
}

/************************************************************************//**
  Returns tiledef matching rule name or nullptr if there is no tiledef
  with such name.
****************************************************************************/
struct tiledef *tiledef_by_rule_name(const char *name)
{
  const char *qs;

  if (name == nullptr) {
    return nullptr;
  }

  qs = Qn_(name);

  tiledef_iterate(td) {
    if (!fc_strcasecmp(tiledef_rule_name(td), qs)) {
      return td;
    }
  } tiledef_iterate_end;

  return nullptr;
}

/************************************************************************//**
  Returns tiledef matching the translated name, or nullptr if there is no
  tiledef with that name.
****************************************************************************/
struct tiledef *tiledef_by_translated_name(const char *name)
{
  tiledef_iterate(td) {
    if (0 == strcmp(tiledef_name_translation(td), name)) {
      return td;
    }
  } tiledef_iterate_end;

  return nullptr;
}

/************************************************************************//**
  Check if tile matches tiledef
****************************************************************************/
bool tile_matches_tiledef(const struct tiledef *td, const struct tile *ptile)
{
  extra_type_list_iterate(td->extras, pextra) {
    if (!tile_has_extra(ptile, pextra)) {
      return FALSE;
    }
  } extra_type_list_iterate_end;

  return TRUE;
}

/************************************************************************//**
  Is there tiledef of the given type cardinally near tile?
  (Does not check ptile itself.)
****************************************************************************/
bool is_tiledef_card_near(const struct civ_map *nmap, const struct tile *ptile,
                          const struct tiledef *ptd)
{
  cardinal_adjc_iterate(nmap, ptile, adjc_tile) {
    if (tile_matches_tiledef(ptd, adjc_tile)) {
      return TRUE;
    }
  } cardinal_adjc_iterate_end;

  return FALSE;
}

/************************************************************************//**
  Is there tiledef of the given type near tile?
  (Does not check ptile itself.)
****************************************************************************/
bool is_tiledef_near_tile(const struct civ_map *nmap, const struct tile *ptile,
                          const struct tiledef *ptd)
{
  adjc_iterate(nmap, ptile, adjc_tile) {
    if (tile_matches_tiledef(ptd, adjc_tile)) {
      return TRUE;
    }
  } adjc_iterate_end;

  return FALSE;
}
