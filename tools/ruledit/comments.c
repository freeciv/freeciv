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
#include "registry_ini.h"
#include "section_file.h"

#include "comments.h"

/**************************************************************************
  Write file header.
**************************************************************************/
void comment_file_header(struct section_file *sfile)
{

  secfile_insert_long_comment(sfile, "\
; Modifying this file:\n\
; You should not modify this file except to make bugfixes or\n\
; for other \"maintenance\".  If you want to make custom changes,\n\
; you should create a new datadir subdirectory and copy this file\n\
; into that directory, and then modify that copy.  Then use the\n\
; command \"rulesetdir <mysubdir>\" in the server to have freeciv\n\
; use your new customized file.\n\
\n\
; Note that the freeciv AI may not cope well with anything more\n\
; than minor changes.\n\
");

}
