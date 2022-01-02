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
#ifndef FC__SEX_H
#define FC__SEX_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

typedef enum {
  SEX_UNKNOWN = -1,
  SEX_FEMALE  = 0,
  SEX_MALE    = 1
} sex_t;

sex_t sex_by_name(const char *name);
const char *sex_rule_name(sex_t kind);
const char *sex_name_translation(sex_t kind);
const char *sex_name_mnemonic(sex_t kind, const char *mnemonic);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif  /* FC__SEX_H */
