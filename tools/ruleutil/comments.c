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

#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

/* utility */
#include "mem.h"
#include "registry.h"
#include "section_file.h"

#include "comments.h"

static struct {
  char *file_header;
  char *buildings;
  char *techs;
  char *govs;
  char *policies;
  char *uclasses;
  char *utypes;
  char *terrains;
  char *extras;
  char *styles;
  char *citystyles;
  char *musicstyles;
} comments_storage;

/**************************************************************************
  Load comments to add to the saved rulesets.
**************************************************************************/
bool comments_load(void)
{
  struct section_file *comment_file;
  const char *fullpath;

  fullpath = fileinfoname(get_data_dirs(), "ruledit/comments.txt");

  if (fullpath == NULL) {
    return FALSE;
  }

  comment_file = secfile_load(fullpath, FALSE);
  if (comment_file == NULL) {
    return FALSE;
  }

  comments_storage.file_header = fc_strdup(secfile_lookup_str(comment_file, "common.header"));
  comments_storage.buildings = fc_strdup(secfile_lookup_str(comment_file, "typedoc.buildings"));
  comments_storage.techs = fc_strdup(secfile_lookup_str(comment_file, "typedoc.techs"));
  comments_storage.govs = fc_strdup(secfile_lookup_str(comment_file, "typedoc.governments"));
  comments_storage.policies = fc_strdup(secfile_lookup_str(comment_file, "typedoc.policies"));
  comments_storage.uclasses = fc_strdup(secfile_lookup_str(comment_file, "typedoc.uclasses"));
  comments_storage.utypes = fc_strdup(secfile_lookup_str(comment_file, "typedoc.utypes"));
  comments_storage.terrains = fc_strdup(secfile_lookup_str(comment_file, "typedoc.terrains"));
  comments_storage.extras = fc_strdup(secfile_lookup_str(comment_file, "typedoc.extras"));
  comments_storage.styles = fc_strdup(secfile_lookup_str(comment_file, "typedoc.styles"));
  comments_storage.citystyles = fc_strdup(secfile_lookup_str(comment_file, "typedoc.citystyles"));
  comments_storage.musicstyles = fc_strdup(secfile_lookup_str(comment_file, "typedoc.musicstyles"));

  secfile_check_unused(comment_file);
  secfile_destroy(comment_file);

  return TRUE;
}

/**************************************************************************
  Free comments.
**************************************************************************/
void comments_free(void)
{
  free(comments_storage.file_header);
}

/**************************************************************************
  Generic comment writing function with some error checking.
**************************************************************************/
static void comment_write(struct section_file *sfile, const char *comment,
                          const char *name)
{
  if (comment == NULL) {
    log_error("Comment for %s missing.", name);
    return;
  }

  secfile_insert_long_comment(sfile, comment);
}

/**************************************************************************
  Write file header.
**************************************************************************/
void comment_file_header(struct section_file *sfile)
{
  comment_write(sfile, comments_storage.file_header, "File header");
}

/**************************************************************************
  Write buildings header.
**************************************************************************/
void comment_buildings(struct section_file *sfile)
{
  comment_write(sfile, comments_storage.buildings, "Buildings");
}

/**************************************************************************
  Write techs header.
**************************************************************************/
void comment_techs(struct section_file *sfile)
{
  comment_write(sfile, comments_storage.techs, "Techs");
}

/**************************************************************************
  Write governments header.
**************************************************************************/
void comment_govs(struct section_file *sfile)
{
  comment_write(sfile, comments_storage.govs, "Governments");
}

/**************************************************************************
  Write policies header.
**************************************************************************/
void comment_policies(struct section_file *sfile)
{
  comment_write(sfile, comments_storage.policies, "Policies");
}

/**************************************************************************
  Write unit classes header.
**************************************************************************/
void comment_uclasses(struct section_file *sfile)
{
  comment_write(sfile, comments_storage.uclasses, "Unit classes");
}

/**************************************************************************
  Write unit types header.
**************************************************************************/
void comment_utypes(struct section_file *sfile)
{
  comment_write(sfile, comments_storage.utypes, "Unit types");
}

/**************************************************************************
  Write terrains header.
**************************************************************************/
void comment_terrains(struct section_file *sfile)
{
  comment_write(sfile, comments_storage.terrains, "Terrains");
}

/**************************************************************************
  Write extras header.
**************************************************************************/
void comment_extras(struct section_file *sfile)
{
  comment_write(sfile, comments_storage.extras, "Extras");
}

/**************************************************************************
  Write styles header.
**************************************************************************/
void comment_styles(struct section_file *sfile)
{
  comment_write(sfile, comments_storage.styles, "Styles");
}

/**************************************************************************
  Write city styles header.
**************************************************************************/
void comment_citystyles(struct section_file *sfile)
{
  comment_write(sfile, comments_storage.citystyles, "City Styles");
}

/**************************************************************************
  Write music styles header.
**************************************************************************/
void comment_musicstyles(struct section_file *sfile)
{
  comment_write(sfile, comments_storage.musicstyles, "Music Styles");
}
