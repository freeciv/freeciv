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
#ifndef FC__MODPACK_DOWNLOAD_H
#define FC__MODPACK_DOWNLOAD_H

#define MODPACK_SUFFIX ".modpack"

#define MODPACK_CAPSTR "+modpack-090810-2"
#define MODLIST_CAPSTR "+Freeciv-2.3-modlist"

typedef void (*dl_msg_callback)(const char *msg);
typedef void (*dl_pb_callback)(const double fraction);

const char *download_modpack(const char *URL,
                             dl_msg_callback mcb,
                             dl_pb_callback pbcb);

typedef void (*modpack_list_setup_cb)(const char *name, const char *URL,
                                      const char *version,
                                      const char *type);

const char *download_modpack_list(const char *URL, modpack_list_setup_cb cb,
                                  dl_msg_callback mcb);

#endif /* FC__MODPACK_DOWNLOAD_H */
