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
#include "fcintl.h"

/* common */
#include "game.h"
#include "government.h"

/* client */
#include "helpdata.h"

/* tools/manual */
#include "fc_manual.h"

/**********************************************************************//**
  Write governments manual page

  @param  tag_info  Tag set to use
  @return           Success
**************************************************************************/
bool manual_governments(struct tag_types *tag_info)
{
  FILE *doc;

  doc = manual_start(tag_info, MANUAL_GOVS);

  if (doc == NULL) {
    return FALSE;
  }

  /* Freeciv-web uses (parts of) the government HTML output in its own
   * manual pages. */
  /* FIXME: this doesn't resemble the wiki manual at all. */
  /* TRANS: markup ... Freeciv version ... ruleset name ... markup */
  fprintf(doc, _("%sFreeciv %s governments help (%s)%s\n\n"), tag_info->title_begin,
          VERSION_STRING, game.control.name, tag_info->title_end);
  governments_iterate(pgov) {
    char buf[64000];

    fprintf(doc, tag_info->item_begin, "gov", pgov->item_number);
    fprintf(doc, "%s%s%s\n\n", tag_info->sect_title_begin,
            government_name_translation(pgov), tag_info->sect_title_end);
    fprintf(doc, tag_info->subitem_begin, "helptext");
    helptext_government(buf, sizeof(buf), NULL, NULL, pgov);
    fprintf(doc, "%s\n\n", buf);
    fprintf(doc, "%s", tag_info->subitem_end);
    fprintf(doc, "%s", tag_info->item_end);
  } governments_iterate_end;

  manual_finalize(tag_info, doc, MANUAL_GOVS);

  return TRUE;
}
