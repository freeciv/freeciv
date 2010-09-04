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
#include <config.h>
#endif

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* utility */
#include "fciconv.h"
#include "fcintl.h"
#include "mem.h"
#include "rand.h"
#include "string_vector.h"
#include "support.h"

#include "bitvector.h"

/***************************************************************************
  Return whether two vectors: vec1 and vec2 have common
  bits. I.e. (vec1 & vec2) != 0.

  Don't call this function directly, use BV_CHECK_MASK macro
  instead. Don't call this function with two different bitvectors.
***************************************************************************/
bool bv_check_mask(const unsigned char *vec1, const unsigned char *vec2,
                   size_t size1, size_t size2)
{
  size_t i;
  fc_assert_ret_val(size1 == size2, FALSE);

  for (i = 0; i < size1; i++) {
    if ((vec1[0] & vec2[0]) != 0) {
      return TRUE;
    }
    vec1++;
    vec2++;
  }
  return FALSE;
}

/***************************************************************************
  ...
***************************************************************************/
bool bv_are_equal(const unsigned char *vec1, const unsigned char *vec2,
                  size_t size1, size_t size2)
{
  size_t i;
  fc_assert_ret_val(size1 == size2, FALSE);

  for (i = 0; i < size1; i++) {
    if (vec1[0] != vec2[0]) {
      return FALSE;
    }
    vec1++;
    vec2++;
  }
  return TRUE;
}
