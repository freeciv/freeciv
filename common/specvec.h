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

/* specvectors: "specific vectors".
   
   This file is used to implement type-checked vectors (variable size arrays).
   (Or, maybe, what you end up doing when you don't use C++ ?)
   
   Before including this file, you must define the following:
     SPECVEC_TAG - this tag will be used to form names for functions etc.
   You may also define:  
     SPECVEC_TYPE - the typed vector will contain pointers to this type;
   If SPECVEC_TYPE is not defined, then 'struct SPECVEC_TAG' is used.
   At the end of this file, these (and other defines) are undef-ed.

   Assuming SPECVEC_TAG were 'foo', and SPECVEC_TYPE were 'foo_t',
   including this file would provide a struct definition for:
      struct foo_vector;
   and prototypes for the following functions:
      void foo_vector_init(struct foo_vector *tthis);
      void foo_vector_reserve(struct foo_vector *tthis, int n);
      int  foo_vector_size(struct foo_vector *tthis);
      foo_t *foo_vector_get(struct foo_vector *tthis, int index);
      void foo_vector_free(struct foo_vector *tthis);

   Also, in a single .c file, you should include specvec_c.h,
   with the same SPECVEC_TAG and SPECVEC_TYPE defined, to
   provide the function implementations.

   Note this is not protected against multiple inclusions; this is
   so that you can have multiple different specvectors.  For each
   specvector, this file should be included _once_, inside a .h file
   which _is_ itself protected against multiple inclusions.
*/

#include "astring.h"

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

SPECVEC_VECTOR {
  struct athing vector;
};

void SPECVEC_FOO(_vector_init) (SPECVEC_VECTOR *tthis);
void SPECVEC_FOO(_vector_reserve) (SPECVEC_VECTOR *tthis, int n);
int  SPECVEC_FOO(_vector_size) (SPECVEC_VECTOR *tthis);
SPECVEC_TYPE *SPECVEC_FOO(_vector_get) (SPECVEC_VECTOR *tthis, int index);
void SPECVEC_FOO(_vector_free) (SPECVEC_VECTOR *tthis);

#undef SPECVEC_TAG
#undef SPECVEC_TYPE
#undef SPECVEC_PASTE_
#undef SPECVEC_PASTE
#undef SPECVEC_VECTOR
#undef SPECVEC_FOO
