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

/**************************************************************************
   idex = ident index: a lookup table for quick mapping of unit and city
   id values to unit and city pointers.

   Method: use separate hash tables for each type.
   Means code duplication for city/unit cases, but simplicity advantages.
   Don't have to manage memory at all: store pointers to unit and city
   structs allocated elsewhere, and keys are pointers to id values inside
   the structs.

   Note id values should probably be unsigned int: here leave as plain int
   so can use pointers to pcity->id etc.
***************************************************************************/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

/* utility */
#include "log.h"

/* common */
#include "city.h"
#include "unit.h"

#include "idex.h"


/* struct city_hash. */
#define SPECHASH_TAG city
#define SPECHASH_KEY_TYPE int
#define SPECHASH_DATA_TYPE struct city *
#define SPECHASH_KEY_TO_PTR FC_INT_TO_PTR
#define SPECHASH_PTR_TO_KEY FC_PTR_TO_INT
#include "spechash.h"

/* struct unit_hash. */
#define SPECHASH_TAG unit
#define SPECHASH_KEY_TYPE int
#define SPECHASH_DATA_TYPE struct unit *
#define SPECHASH_KEY_TO_PTR FC_INT_TO_PTR
#define SPECHASH_PTR_TO_KEY FC_PTR_TO_INT
#include "spechash.h"


/* "Global" data: */
static struct city_hash *idex_city_hash = NULL;
static struct unit_hash *idex_unit_hash = NULL;

/**************************************************************************
   Initialize.  Should call this at the start before use.
***************************************************************************/
void idex_init(void)
{
  fc_assert_ret(idex_city_hash == NULL);
  fc_assert_ret(idex_unit_hash == NULL);

  idex_city_hash = city_hash_new();
  idex_unit_hash = unit_hash_new();
}

/**************************************************************************
   Free the hashs.
***************************************************************************/
void idex_free(void)
{
  city_hash_destroy(idex_city_hash);
  idex_city_hash = NULL;

  unit_hash_destroy(idex_unit_hash);
  idex_unit_hash = NULL;
}

/**************************************************************************
   Register a city into idex, with current pcity->id.
   Call this when pcity created.
***************************************************************************/
void idex_register_city(struct city *pcity)
{
  struct city *old;

  city_hash_replace_full(idex_city_hash, pcity->id, pcity, NULL, &old);
  fc_assert_ret_msg(NULL == old,
                    "IDEX: city collision: new %d %p %s, old %d %p %s",
                    pcity->id, (void *) pcity, city_name(pcity),
                    old->id, (void *) old, city_name(old));
}

/**************************************************************************
   Register a unit into idex, with current punit->id.
   Call this when punit created.
***************************************************************************/
void idex_register_unit(struct unit *punit)
{
  struct unit *old;

  unit_hash_replace_full(idex_unit_hash, punit->id, punit, NULL, &old);
  fc_assert_ret_msg(NULL == old,
                    "IDEX: unit collision: new %d %p %s, old %d %p %s",
                    punit->id, (void *) punit, unit_rule_name(punit),
                    old->id, (void *) old, unit_rule_name(old));
}

/**************************************************************************
   Remove a city from idex, with current pcity->id.
   Call this when pcity deleted.
***************************************************************************/
void idex_unregister_city(struct city *pcity)
{
  struct city *old;

  city_hash_remove_full(idex_city_hash, pcity->id, NULL, &old);
  fc_assert_ret_msg(NULL != old,
                    "IDEX: city unreg missing: %d %p %s",
                    pcity->id, (void *) pcity, city_name(pcity));
  fc_assert_ret_msg(old == pcity, "IDEX: city unreg mismatch: "
                    "unreg %d %p %s, old %d %p %s",
                    pcity->id, (void *) pcity, city_name(pcity),
                    old->id, (void *) old, city_name(old));
}

/**************************************************************************
   Remove a unit from idex, with current punit->id.
   Call this when punit deleted.
***************************************************************************/
void idex_unregister_unit(struct unit *punit)
{
  struct unit *old;

  unit_hash_remove_full(idex_unit_hash, punit->id, NULL, &old);
  fc_assert_ret_msg(NULL != old,
                    "IDEX: unit unreg missing: %d %p %s",
                    punit->id, (void *) punit, unit_rule_name(punit));
  fc_assert_ret_msg(old == punit, "IDEX: unit unreg mismatch: "
                    "unreg %d %p %s, old %d %p %s",
                    punit->id, (void *) punit, unit_rule_name(punit),
                    old->id, (void*) old, unit_rule_name(old));
}

/**************************************************************************
   Lookup city with given id.
   Returns NULL if the city is not registered (which is not an error).
***************************************************************************/
struct city *idex_lookup_city(int id)
{
  struct city *pcity;

  city_hash_lookup(idex_city_hash, id, &pcity);
  return pcity;
}

/**************************************************************************
   Lookup unit with given id.
   Returns NULL if the unit is not registered (which is not an error).
***************************************************************************/
struct unit *idex_lookup_unit(int id)
{
  struct unit *punit;

  unit_hash_lookup(idex_unit_hash, id, &punit);
  return punit;
}
