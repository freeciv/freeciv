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

/* client */
#include "helpdata.h"

/* tools/manual */
#include "fc_manual.h"

/**********************************************************************//**
  Write improvements manual page

  @param  tag_info  Tag set to use
  @param  manual    Exact manual page
  @return           Success
**************************************************************************/
static bool manual_improvements(struct tag_types *tag_info,
                                enum manuals manual)
{
  FILE *doc;

  doc = manual_start(tag_info, manual);

  if (doc == NULL) {
    return FALSE;
  }

  if (manual == MANUAL_BUILDINGS) {
    /* TRANS: markup ... Freeciv version ... ruleset name ... markup */
    fprintf(doc, _("%sFreeciv %s buildings help (%s)%s\n\n"), tag_info->title_begin,
            VERSION_STRING, game.control.name, tag_info->title_end);
  } else {
    /* TRANS: markup ... Freeciv version ... ruleset name ... markup */
    fprintf(doc, _("%sFreeciv %s wonders help (%s)%s\n\n"), tag_info->title_begin,
            VERSION_STRING, game.control.name, tag_info->title_end);
  }

  fprintf(doc, "<table>\n<tr bgcolor=#9bc3d1><th colspan=2>%s</th>"
          "<th>%s<br/>%s</th><th>%s<br/>%s</th><th>%s</th></tr>\n\n",
          _("Name"), _("Cost"), _("Upkeep"),
          _("Requirement"), _("Obsolete by"), _("More info"));

  improvement_iterate(pimprove) {
    char buf[64000];

    if (!valid_improvement(pimprove)
        || is_great_wonder(pimprove) == (manual == MANUAL_BUILDINGS)) {
      continue;
    }

    helptext_building(buf, sizeof(buf), NULL, NULL, pimprove);

    fprintf(doc, "<tr><td>%s%s%s</td><td>%s</td>\n"
            "<td align=\"center\"><b>%d</b><br/>%d</td>\n<td>",
            tag_info->image_begin, pimprove->graphic_str, tag_info->image_end,
            improvement_name_translation(pimprove),
            pimprove->build_cost,
            pimprove->upkeep);

    if (requirement_vector_size(&pimprove->reqs) == 0) {
      char text[512];

      strncpy(text, Q_("?req:None"), sizeof(text) - 1);
      fprintf(doc, "%s<br/>", text);
    } else {
      requirement_vector_iterate(&pimprove->reqs, req) {
        char text[512], text2[512];

        fc_snprintf(text2, sizeof(text2),
                    /* TRANS: Feature required to be absent. */
                    req->present ? "%s" : _("no %s"),
                    universal_name_translation(&req->source,
                                               text, sizeof(text)));
        fprintf(doc, "%s<br/>", text2);
      } requirement_vector_iterate_end;
    }

    fprintf(doc, "\n%s\n", tag_info->hline);

    if (requirement_vector_size(&pimprove->obsolete_by) == 0) {
      char text[512];

      strncpy(text, Q_("?req:None"), sizeof(text) - 1);
      fprintf(doc, "<em>%s</em><br/>", text);
    } else {
      requirement_vector_iterate(&pimprove->obsolete_by, pobs) {
        char text[512], text2[512];

        fc_snprintf(text2, sizeof(text2),
                    /* TRANS: Feature required to be absent. */
                    pobs->present ? "%s" : _("no %s"),
                    universal_name_translation(&pobs->source,
                                               text, sizeof(text)));
        fprintf(doc, "<em>%s</em><br/>", text2);
      } requirement_vector_iterate_end;
    }

    fprintf(doc,
            "</td>\n<td>%s</td>\n</tr><tr><td colspan=\"5\">\n%s\n</td></tr>\n\n",
            buf, tag_info->hline);
  } improvement_iterate_end;

  fprintf(doc, "</table>");

  manual_finalize(tag_info, doc, manual);

  return TRUE;
}

/**********************************************************************//**
  Write improvements manual page

  @param  tag_info  Tag set to use
  @return           Success
**************************************************************************/
bool manual_buildings(struct tag_types *tag_info)
{
  return manual_improvements(tag_info, MANUAL_BUILDINGS)
    && manual_improvements(tag_info, MANUAL_WONDERS);
}
