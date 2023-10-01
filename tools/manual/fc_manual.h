/***********************************************************************
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
#ifndef FC__FC_MANUAL_H
#define FC__FC_MANUAL_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include <stdio.h> /* FILE */

/* utility */
#include "support.h"

#define SPECENUM_NAME manuals
#define SPECENUM_VALUE0 MANUAL_SETTINGS
#define SPECENUM_VALUE0NAME "Settings"
#define SPECENUM_VALUE1 MANUAL_COMMANDS
#define SPECENUM_VALUE1NAME "Commands"
#define SPECENUM_VALUE2 MANUAL_TERRAIN
#define SPECENUM_VALUE2NAME "Terrain"
#define SPECENUM_VALUE3 MANUAL_BUILDINGS
#define SPECENUM_VALUE3NAME "Buildings"
#define SPECENUM_VALUE4 MANUAL_WONDERS
#define SPECENUM_VALUE4NAME "Wonders"
#define SPECENUM_VALUE5 MANUAL_GOVS
#define SPECENUM_VALUE5NAME "Governments"
#define SPECENUM_VALUE6 MANUAL_UNITS
#define SPECENUM_VALUE6NAME "Units"
#define SPECENUM_VALUE7 MANUAL_TECHS
#define SPECENUM_VALUE7NAME "Techs"
#define SPECENUM_COUNT MANUAL_COUNT
#include "specenum_gen.h"

struct tag_types {
  const char *file_ext;
  const char *header;
  const char *title_begin;
  const char *title_end;
  const char *sect_title_begin;
  const char *sect_title_end;
  const char *image_begin;
  const char *image_end;
  const char *item_begin;
  const char *item_end;
  const char *subitem_begin;
  const char *subitem_end;
  const char *tail;
  const char *hline;
};

/* Utility functions */
FILE *manual_start(struct tag_types *tag_info, int manual_number);
void manual_finalize(struct tag_types *tag_info, FILE *doc,
                     const char *manual_name);
char *html_special_chars(char *str, size_t *len);

/* Individual manual pages */
bool manual_settings(struct tag_types *tag_info);
bool manual_commands(struct tag_types *tag_info);
bool manual_terrain(struct tag_types *tag_info);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* FC__FC_MANUAL_H */
