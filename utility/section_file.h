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
#ifndef FC__SECTION_FILE_H
#define FC__SECTION_FILE_H

/* This header contains internals of section_file that its users should
 * not care about. This header should be included by soruce files implementing
 * registry itself. */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* The section file struct itself. */
struct section_file {
  char *name;                           /* Can be NULL. */
  size_t num_entries;
  struct section_list *sections;
  bool allow_duplicates;
  struct {
    struct section_hash *sections;
    struct entry_hash *entries;
  } hash;
};

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif  /* FC__SECTION_FILE_H */
