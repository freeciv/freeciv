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

/********************************************************************** 
  Allocated/allocatable strings (and things?)
  See comments in astring.c
***********************************************************************/

#ifndef FC__ASTRING_H
#define FC__ASTRING_H

struct astring {
  char *str;			/* the string */
  int n;			/* size most recently requested */
  int n_alloc;			/* total allocated */
};

struct athing {
  void *ptr;			/* the data */
  int size;			/* size of one object */
  int n;			/* number most recently requested */
  int n_alloc;			/* total number allocated */
};

/* Can assign this in variable declaration to initialize:
 * Notice a static astring var is exactly this already.
 * For athing need to call ath_init() due to size.
 */
#define ASTRING_INIT  { NULL, 0, 0 }

void astr_init(struct astring *astr);
void astr_minsize(struct astring *astr, int n);
void astr_free(struct astring *astr);

void ath_init(struct athing *ath, int size);
void ath_minnum(struct athing *ath, int n);
void ath_free(struct athing *ath);

/* Returns a pointer to the nth (0-based) element in the given athing. Does
   no boundary or pointer checking */
#define ath_get(ath,n) ((char *)((ath)->ptr)+(ath)->size*(n))

#endif  /* FC__ASTRING_H */
