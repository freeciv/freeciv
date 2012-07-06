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
#ifndef FC__MPDB_H
#define FC__MPDB_H

/* common */
#include "fc_types.h"

/* modinst */
#include "download.h"

struct install_info
{
  char name[MAX_LEN_NAME];
  enum modpack_type type;
  char version[MAX_LEN_NAME];
};

#define SPECLIST_TAG install_info
#define SPECLIST_TYPE struct install_info
#include "speclist.h"

#define install_info_list_iterate(ii_list, item) \
  TYPED_LIST_ITERATE(struct install_info, ii_list, item)
#define install_info_list_iterate_end  LIST_ITERATE_END

void load_install_info_list(const char *filename,
                            struct install_info_list *list);
void save_install_info_list(const char *filename,
                            struct install_info_list *list);

#endif /* FC__MPDB_H */
