/**********************************************************************
 Freeciv - Copyright (C) 2005 - The Freeciv Project
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
#include <config.h>
#endif

#include "fcintl.h"

#include "api_intl.h"

const char *api_intl__(const char *string) {
  return _(string);
}

const char *api_intl_N_(const char *string) {
  return N_(string);
}

const char *api_intl_Q_(const char *string) {
  return Q_(string);
}

const char *api_intl_PL_(const char *string1, const char *string2, int n) {
  return PL_(string1, string2, n);
}

