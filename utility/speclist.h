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

/* speclists: "specific genlists", by dwp.
   (A generalisation of previous city_list and unit_list stuff.)
   
   This file is used to implement a "specific" genlist.
   That is, a (sometimes) type-checked genlist.  (Or at least a
   genlist with related functions with distinctly typed parameters.)
   (Or, maybe, what you end up doing when you don't use C++ ?)
   
   Before including this file, you must define the following:
     SPECLIST_TAG - this tag will be used to form names for functions etc.
   You may also define:  
     SPECLIST_TYPE - the typed genlist will contain pointers to this type;
   If SPECLIST_TYPE is not defined, then 'struct SPECLIST_TAG' is used.
   At the end of this file, these (and other defines) are undef-ed.

   Assuming SPECLIST_TAG were 'foo', and SPECLIST_TYPE were 'foo_t',
   including this file would provide a struct definition for:
      struct foo_list;
   and prototypes for the following functions:
      void foo_list_init(struct foo_list *This);
      int  foo_list_size(struct foo_list *This);
      foo_t *foo_list_get(struct foo_list *This, int index);
      void foo_list_insert(struct foo_list *This, foo_t *pfoo);
      void foo_list_insert_back(struct foo_list *This, foo_t *pfoo);
      void foo_list_unlink(struct foo_list *This, foo_t *pfoo);
      void foo_list_unlink_all(struct foo_list *This);
      void foo_list_sort(struct foo_list *This, 
         int (*compar)(const void *, const void *));

   You should also define yourself:  (this file cannot do this for you)
   
   #define foo_list_iterate(foolist, pfoo) \
       TYPED_LIST_ITERATE(foo_t, foolist, pfoo)
   #define foo_list_iterate_end  LIST_ITERATE_END

   Also, in a single .c file, you should include speclist_c.h,
   with the same SPECLIST_TAG and SPECLIST_TYPE defined, to
   provide the function implementations.

   Note this is not protected against multiple inclusions; this is
   so that you can have multiple different speclists.  For each
   speclist, this file should be included _once_, inside a .h file
   which _is_ itself protected against multiple inclusions.
*/

#include "genlist.h"

#ifndef SPECLIST_TAG
#error Must define a SPECLIST_TAG to use this header
#endif

#ifndef SPECLIST_TYPE
#define SPECLIST_TYPE struct SPECLIST_TAG
#endif

#define SPECLIST_PASTE_(x,y) x ## y
#define SPECLIST_PASTE(x,y) SPECLIST_PASTE_(x,y)

#define SPECLIST_LIST struct SPECLIST_PASTE(SPECLIST_TAG, _list)
#define SPECLIST_FOO(suffix) SPECLIST_PASTE(SPECLIST_TAG, suffix)

SPECLIST_LIST {
  struct genlist list;
};

static inline void SPECLIST_FOO(_list_init) (SPECLIST_LIST *tthis)
{
  genlist_init(&tthis->list);
}

static inline void SPECLIST_FOO(_list_insert) (SPECLIST_LIST *tthis, SPECLIST_TYPE *pfoo)
{
  genlist_insert(&tthis->list, pfoo, 0);
}

static inline void SPECLIST_FOO(_list_unlink) (SPECLIST_LIST *tthis, SPECLIST_TYPE *pfoo)
{
  genlist_unlink(&tthis->list, pfoo);
}

static inline int SPECLIST_FOO(_list_size) (const SPECLIST_LIST *tthis)
{
  return genlist_size(&tthis->list);
}

static inline SPECLIST_TYPE *SPECLIST_FOO(_list_get) (const SPECLIST_LIST *tthis, int index)
{
  return genlist_get(&tthis->list, index);
}

static inline void SPECLIST_FOO(_list_insert_back) (SPECLIST_LIST *tthis, SPECLIST_TYPE *pfoo)
{
  genlist_insert(&tthis->list, pfoo, -1);
}

static inline void SPECLIST_FOO(_list_unlink_all) (SPECLIST_LIST *tthis)
{
  genlist_unlink_all(&tthis->list);
}

static inline void SPECLIST_FOO(_list_sort) (SPECLIST_LIST * tthis, int (*compar) (const void *, const void *))
{
  genlist_sort(&tthis->list, compar);
}

#undef SPECLIST_TAG
#undef SPECLIST_TYPE
#undef SPECLIST_PASTE_
#undef SPECLIST_PASTE
#undef SPECLIST_LIST
#undef SPECLIST_FOO
