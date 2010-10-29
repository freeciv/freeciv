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
#ifndef FC__GOVERNMENT_H
#define FC__GOVERNMENT_H

/* utility */
#include "hash.h"
#include "shared.h"

/* common */
#include "fc_types.h"
#include "name_translation.h"
#include "requirements.h"


struct strvec;          /* Actually defined in "utility/string_vector.h". */

struct ruler_title;     /* Opaque type. */

#define G_MAGIC (127)		/* magic constant */

/* special values for free_* fields -- SKi */
#define G_CITY_SIZE_FREE          G_MAGIC

/* This is struct government itself.  All information about a form of
 * government is contained inhere. -- SKi */
struct government {
  int item_number;
  struct name_translation name;
  char graphic_str[MAX_LEN_NAME];
  char graphic_alt[MAX_LEN_NAME];
  struct requirement_vector reqs;
  struct hash_table *ruler_titles;
  struct strvec *helptext;

  /* AI cached data for this government. */
  struct {
    struct government *better; /* hint: a better government (or NULL) */
  } ai;
};


/* General government accessor functions. */
int government_count(void);
int government_index(const struct government *pgovern);
int government_number(const struct government *pgovern);

struct government *government_by_number(const int gov);
struct government *government_of_player(const struct player *pplayer);
struct government *government_of_city(const struct city *pcity);

struct government *government_by_rule_name(const char *name);
struct government *government_by_translated_name(const char *name);

const char *government_rule_name(const struct government *pgovern);
const char *government_name_translation(const struct government *pgovern);
const char *government_name_for_player(const struct player *pplayer);

/* Ruler titles. */
struct ruler_title *
government_ruler_title_new(struct government *pgovern,
                           const struct nation_type *pnation,
                           const char *ruler_male_title,
                           const char *ruler_female_title);

const struct nation_type *
ruler_title_nation(const struct ruler_title *pruler_title);
const char *
ruler_title_male_rule_name(const struct ruler_title *pruler_title);
const char *
ruler_title_female_rule_name(const struct ruler_title *pruler_title);

const char *ruler_title_translation(const struct player *pplayer);

#define government_ruler_titles_iterate(NAME_pgov, NAME_rule_title)         \
  generic_iterate(struct hash_iter, const struct ruler_title *,             \
                  NAME_rule_title, hash_iter_sizeof, hash_value_iter_init,  \
                  NAME_pgov->ruler_titles)
#define government_ruler_titles_iterate_end generic_iterate_end

/* Ancillary routines */
bool can_change_to_government(struct player *pplayer,
                              const struct government *pgovern);

/* Initialization and iteration */
void governments_alloc(int num);
void governments_free(void);

struct government_iter;
size_t government_iter_sizeof(void);
struct iterator *government_iter_init(struct government_iter *it);

/* Iterate over government types. */
#define governments_iterate(NAME_pgov)                                      \
  generic_iterate(struct government_iter, struct government *,              \
                  NAME_pgov, government_iter_sizeof, government_iter_init)
#define governments_iterate_end generic_iterate_end

#endif  /* FC__GOVERNMENT_H */
