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

#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

/* utility */
#include "support.h"

/* common */
#include "disaster.h"
#include "game.h"
#include "government.h"
#include "improvement.h"
#include "requirements.h"
#include "specialist.h"
#include "style.h"
#include "tech.h"

#include "validity.h"

/**************************************************************************
  Check if universal is mentioned in the requirement vector.
**************************************************************************/
static bool universal_in_req_vec(const struct universal *uni,
                                 const struct requirement_vector *preqs)
{
  requirement_vector_iterate(preqs, preq) {
    if (are_universals_equal(uni, &preq->source)) {
      return TRUE;
    }
  } requirement_vector_iterate_end;

  return FALSE;
}

/**************************************************************************
  Check if anything in ruleset needs universal
**************************************************************************/
static bool is_universal_needed(struct universal *uni, requirers_cb cb)
{
  bool needed = FALSE;
  bool needed_by_music_style = FALSE;
  int i;

  disaster_type_iterate(pdis) {
    if (universal_in_req_vec(uni, &pdis->reqs)) {
      cb(disaster_rule_name(pdis));
      needed = TRUE;
    }
  } disaster_type_iterate_end;

  improvement_iterate(pimprove) {
    if (universal_in_req_vec(uni, &pimprove->reqs)
        || universal_in_req_vec(uni, &pimprove->obsolete_by)) {
      cb(improvement_rule_name(pimprove));
      needed = TRUE;
    }
  } improvement_iterate_end;

  governments_iterate(pgov) {
    if (universal_in_req_vec(uni, &pgov->reqs)) {
      cb(government_rule_name(pgov));
      needed = TRUE;
    }
  } governments_iterate_end;

  specialist_type_iterate(sp) {
    struct specialist *psp = specialist_by_number(sp);

    if (universal_in_req_vec(uni, &psp->reqs)) {
      cb(specialist_rule_name(psp));
      needed = TRUE;
    }
  } specialist_type_iterate_end;

  extra_type_iterate(pextra) {
    if (universal_in_req_vec(uni, &pextra->reqs)
        || universal_in_req_vec(uni, &pextra->rmreqs)) {
      cb(extra_rule_name(pextra));
      needed = TRUE;
    }
  } extra_type_iterate_end;

  for (i = 0; i < game.control.styles_count; i++) {
    if (universal_in_req_vec(uni, &city_styles[i].reqs)) {
      cb(city_style_rule_name(i));
      needed = TRUE;
    }
  }

  music_styles_iterate(pmus) {
    if (universal_in_req_vec(uni, &pmus->reqs)) {
      needed_by_music_style = TRUE;
    }
  } music_styles_iterate_end;

  if (needed_by_music_style) {
    cb(R__("Music Style"));
    needed = TRUE;
  }

  /* TODO: Effect requirement lists */

  return needed;
}

/**************************************************************************
  Check if anything in ruleset needs tech
**************************************************************************/
bool is_tech_needed(struct advance *padv, requirers_cb cb)
{
  struct universal uni = { .value.advance = padv, .kind = VUT_ADVANCE };
  bool needed = FALSE;

  advance_iterate(A_FIRST, pdependant) {
    if (pdependant->require[AR_ONE] == padv
        || pdependant->require[AR_TWO] == padv
        || pdependant->require[AR_ROOT] == padv) {
      cb(advance_rule_name(pdependant));
      needed = TRUE;
    }
  } advance_iterate_end;

  unit_type_iterate(ptype) {
    if (ptype->require_advance == padv) {
      cb(utype_rule_name(ptype));
      needed = TRUE;
    }
  } unit_type_iterate_end;

  needed |= is_universal_needed(&uni, cb);

  return needed;
}
