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
  Write file header.
**************************************************************************/
void comment_file_header(struct section_file *sfile)
{
  secfile_insert_long_comment(sfile, comments_storage.file_header);
}
