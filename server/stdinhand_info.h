/**********************************************************************
 Freeciv - Copyright (C) 2004 - The Freeciv Project
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.
                                                                                
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
***********************************************************************/
#ifndef FC__STDINHAND_C_H
#define FC__STDINHAND_C_H

bool valid_notradesize(int value, const char **reject_message);
bool valid_fulltradesize(int value, const char **reject_message);
bool autotoggle(bool value, const char **reject_message);
bool is_valid_allowtake(const char *allow_take,
                          const char **error_string);
bool is_valid_startunits(const char *start_units,
                         const char **error_string);
bool valid_max_players(int v, const char **r_m);

bool sset_is_changeable(int idx);

#define SSET_MAX_LEN  16             /* max setting name length (plus nul) */
#define TOKEN_DELIMITERS " \t\n,"

#endif
