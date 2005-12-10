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

/********************************************************************** 
  Reading and using the themespec files, which describe
  the files and contents of themesets.
***********************************************************************/
#ifndef FC__THEMESPEC_H
#define FC__THEMESPEC_H

#include "fc_types.h"

#include "options.h"

struct sprite;			/* opaque; gui-dep */

struct themeset;

extern struct themeset *themeset;

const char **get_themeset_list(void);

struct themeset *themeset_read_toplevel(const char *themeset_name);
void themeset_free(struct themeset *themeset);
void themeset_load_tiles(struct themeset *t);
void themeset_free_tiles(struct themeset *t);

void themespec_try_read(const char *themeset_name);
void themespec_reread(const char *themeset_name);
void themespec_reread_callback(struct client_option *option);

struct sprite* themeset_lookup_sprite_tag_alt(struct themeset *t,
					    const char *tag, const char *alt,
					    bool required, const char *what,
					    const char *name);

/* Themeset accessor functions. */
const char *themeset_get_name(const struct themeset *t);
const char *themeset_main_intro_filename(const struct themeset *t);
const char *themeset_mini_intro_filename(const struct themeset *t);

#endif  /* FC__THEMESPEC_H */
