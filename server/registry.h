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
#ifndef __REGISTRY_H
#define __REGISTRY_H

#include "genlist.h"

struct hash_data;

struct section_file {
  struct genlist section_list;
  int num_entries;
  struct hash_data *hashd;
};


struct section {
  char *name;
  struct genlist entry_list;
};


struct section_entry {
  char *name;
  int  ivalue;
  char *svalue;
};


void secfile_insert_int(struct section_file *my_section_file, 
			int val, char *path, ...);

void secfile_insert_str(struct section_file *my_section_file, 
			char *sval, char *path, ...);



int section_file_load(struct section_file *my_section_file, char *filename);
int section_file_save(struct section_file *my_section_file, char *filename);
void section_file_free(struct section_file *file);

void section_file_init(struct section_file *file);


int secfile_lookup_int(struct section_file *my_section_file, 
		       char *path, ...);

char *secfile_lookup_str(struct section_file *my_section_file, 
			 char *path, ...);

int secfile_lookup_int_default(struct section_file *my_section_file,
                               int def, char *path, ...);

char *secfile_lookup_str_default(struct section_file *my_section_file, 
                                 char *def, char *path, ...);

int section_file_lookup(struct section_file *my_section_file, 
			char *path, ...);

struct section_entry *section_file_lookup_internal(struct section_file 
						   *my_section_file,  
						   char *fullpath);
 

struct section_entry *section_file_insert_internal(struct section_file 
						   *my_section_file, 
						   char *fullpath);

#endif

