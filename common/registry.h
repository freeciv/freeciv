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
#ifndef FC__REGISTRY_H
#define FC__REGISTRY_H

#include "sbuffer.h"
#include "genlist.h"
#include "attribute.h"

struct hash_data;

struct section_file {
  struct genlist section_list;
  int num_entries;
  struct hash_data *hashd;
  struct sbuffer *sb;
};

void section_file_init(struct section_file *file);
int section_file_load(struct section_file *my_section_file, char *filename);
int section_file_save(struct section_file *my_section_file, char *filename);
void section_file_free(struct section_file *file);
void section_file_check_unused(struct section_file *file, char *filename);

void secfile_insert_int(struct section_file *my_section_file, 
			int val, char *path, ...)
                        fc__attribute((format (printf, 3, 4)));
			
void secfile_insert_str(struct section_file *my_section_file, 
			char *sval, char *path, ...)
                        fc__attribute((format (printf, 3, 4)));

int section_file_lookup(struct section_file *my_section_file, 
			char *path, ...)
                        fc__attribute((format (printf, 2, 3)));

int secfile_lookup_int(struct section_file *my_section_file, 
		       char *path, ...)
                       fc__attribute((format (printf, 2, 3)));
		       
char *secfile_lookup_str(struct section_file *my_section_file, 
			 char *path, ...)
                         fc__attribute((format (printf, 2, 3)));

int secfile_lookup_int_default(struct section_file *my_section_file,
                               int def, char *path, ...)
                               fc__attribute((format (printf, 3, 4)));

char *secfile_lookup_str_default(struct section_file *my_section_file, 
                                 char *def, char *path, ...)
                                 fc__attribute((format (printf, 3, 4)));

int secfile_lookup_vec_dimen(struct section_file *my_section_file, 
			     char *path, ...)
                             fc__attribute((format (printf, 2, 3)));
int *secfile_lookup_int_vec(struct section_file *my_section_file,
			    int *dimen, char *path, ...)
                            fc__attribute((format (printf, 3, 4)));
char **secfile_lookup_str_vec(struct section_file *my_section_file,
			      int *dimen, char *path, ...)
                              fc__attribute((format (printf, 3, 4)));

char **secfile_get_secnames_prefix(struct section_file *my_section_file,
				   char *prefix, int *num);

#endif  /* FC__REGISTRY_H */

