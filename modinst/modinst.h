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
#ifndef FC__MODINST_H
#define FC__MODINST_H

struct fcmp_params
{
  const char *list_url;
  const char *inst_prefix;
};

#if IS_DEVEL_VERSION
#define MODPACK_LIST_URL  "http://www.cazfi.net/freeciv/modinst/" DATASUBDIR "/modpack.list"
#define DEFAULT_URL_START "http://www.cazfi.net/freeciv/modinst/" DATASUBDIR "/"
#else  /* IS_DEVEL_VERSION */
#define MODPACK_LIST_URL  "http://download.gna.org/freeciv/modinst/" DATASUBDIR "/modpack.list"
#define DEFAULT_URL_START "http://download.gna.org/freeciv/modinst/" DATASUBDIR "/"
#endif /* IS_DEVEL_VERSION */

#define EXAMPLE_URL DEFAULT_URL_START "ancients.modpack"

#endif /* FC__MODINST_H */
