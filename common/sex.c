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
#include "fcintl.h"
#include "support.h"

#include "sex.h"

/************************************************************************//**
  Return sex by the name provided
****************************************************************************/
sex_t sex_by_name(const char *name)
{
  if (!fc_strcasecmp("Male", name)) {
    return SEX_MALE;
  }

  if (!fc_strcasecmp("Female", name)) {
    return SEX_FEMALE;
  }

  return SEX_UNKNOWN;
}

/************************************************************************//**
  Return name of the sex.
****************************************************************************/
const char *sex_rule_name(sex_t kind)
{
  switch (kind) {
  case SEX_MALE:
    return N_("Male");
  case SEX_FEMALE:
    return N_("Female");
  case SEX_UNKNOWN:
    return NULL;
  }

  return NULL;
}

/************************************************************************//**
  Return translated name of the sex.
****************************************************************************/
const char *sex_name_translation(sex_t kind)
{
  const char *rule_name = sex_rule_name(kind);

  if (rule_name == NULL) {
    return NULL;
  }

  return _(rule_name);
}

/************************************************************************//**
  Return translated name of the sex with a mnemonic placed on it
****************************************************************************/
const char *sex_name_mnemonic(sex_t kind, const char *mnemonic)
{
  static char buf[128];

  switch (kind) {
  case SEX_MALE:
    fc_snprintf(buf, sizeof(buf), _("%sMale"), mnemonic);
    return buf;
  case SEX_FEMALE:
    fc_snprintf(buf, sizeof(buf), _("%sFemale"), mnemonic);
    return buf;
  case SEX_UNKNOWN:
    break;
  }

  return NULL;
}
