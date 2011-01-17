/**********************************************************************
 Freeciv - Copyright (C) 2005 - M.C. Kaufman
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
***********************************************************************/
#ifndef FC__FCDB_H
#define FC__FCDB_H

#include "shared.h"

#define SPECENUM_NAME fcdb_option_type
#define SPECENUM_VALUE0     FCDB_OPTION_TYPE_HOST
#define SPECENUM_VALUE0NAME "host"
#define SPECENUM_VALUE1     FCDB_OPTION_TYPE_PORT
#define SPECENUM_VALUE1NAME "port"
#define SPECENUM_VALUE2     FCDB_OPTION_TYPE_USER
#define SPECENUM_VALUE2NAME "user"
#define SPECENUM_VALUE3     FCDB_OPTION_TYPE_PASSWORD
#define SPECENUM_VALUE3NAME "password"
#define SPECENUM_VALUE4     FCDB_OPTION_TYPE_DATABASE
#define SPECENUM_VALUE4NAME "database"
#define SPECENUM_VALUE5     FCDB_OPTION_TYPE_TABLE_USER
#define SPECENUM_VALUE5NAME "table_user"
#define SPECENUM_VALUE6     FCDB_OPTION_TYPE_TABLE_LOG
#define SPECENUM_VALUE6NAME "table_log"
#define SPECENUM_COUNT      FCDB_OPTION_TYPE_COUNT
#include "specenum_gen.h"

bool fcdb_init(const char *conf_file);
const char *fcdb_option_get(enum fcdb_option_type type);
void fcdb_free(void);

#endif /* FC__FCDB_H */
