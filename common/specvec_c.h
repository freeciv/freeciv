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

/* See comments in specvec.h about how to use this file.  Notice this
   is kind of a .c file (it provides function bodies), but it should
   only be used included, so its also kind of a .h file.  The name
   specvec_c.h is intended to indicate this, and it also makes
   automake do the right thing. (?)

   Some of the following is duplicated from specvec.h, because specvec.h
   undefs its defines to avoid pollution.  However the file which includes
   this _must_ also include specvec.h appropriately so that the vector type
   is defined.  With the usual arrangement of .c and .h files this normally
   happens anyway, so this restriction may be considered beneficial.
*/

#ifndef SPECVEC_TAG
#error Must define a SPECVEC_TAG to use this header
#endif

#ifndef SPECVEC_TYPE
#define SPECVEC_TYPE struct SPECVEC_TAG
#endif

#define SPECVEC_PASTE_(x,y) x ## y
#define SPECVEC_PASTE(x,y) SPECVEC_PASTE_(x,y)

#define SPECVEC_VECTOR struct SPECVEC_PASTE(SPECVEC_TAG, _vector)

#define SPECVEC_FOO(suffix) SPECVEC_PASTE(SPECVEC_TAG, suffix)

void SPECVEC_FOO(_vector_init) (SPECVEC_VECTOR *tthis)
{
  ath_init(&tthis->vector, sizeof(SPECVEC_TYPE));
}

void SPECVEC_FOO(_vector_reserve) (SPECVEC_VECTOR *tthis, int n)
{
  ath_minnum(&tthis->vector, n);
}

int SPECVEC_FOO(_vector_size) (SPECVEC_VECTOR *tthis)
{
  return tthis->vector.n;
}

SPECVEC_TYPE *SPECVEC_FOO(_vector_get) (SPECVEC_VECTOR *tthis, int index)
{
  assert(index>=0 && index<tthis->vector.n);

  return ((SPECVEC_TYPE *)ath_get(&tthis->vector, index));
}

void SPECVEC_FOO(_vector_free) (SPECVEC_VECTOR *tthis)
{
  ath_free(&tthis->vector);
}

#undef SPECVEC_TAG
#undef SPECVEC_TYPE
#undef SPECVEC_PASTE_
#undef SPECVEC_PASTE
#undef SPECVEC_VECTOR
#undef SPECVEC_FOO
