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
#include "movement.h"

/* client */
#include "helpdata.h"

/* tools/manual */
#include "fc_manual.h"

/**********************************************************************//**
  Write units manual page

  @param  tag_info  Tag set to use
  @return           Success
**************************************************************************/
bool manual_units(struct tag_types *tag_info)
{
  FILE *doc;

  doc = manual_start(tag_info, MANUAL_UNITS);

  if (doc == NULL) {
    return FALSE;
  }

  /* Freeciv-web uses (parts of) the unit type HTML output in its own
   * manual pages. */
  /* FIXME: this doesn't resemble the wiki manual at all. */
  /* TRANS: markup ... Freeciv version ... ruleset name ... markup */
  fprintf(doc, _("%sFreeciv %s unit types help (%s)%s\n\n"),
          tag_info->title_begin, VERSION_STRING, game.control.name,
          tag_info->title_end);
  unit_type_iterate(putype) {
    char buf[64000];

    fprintf(doc, tag_info->item_begin, "utype", putype->item_number);
    fprintf(doc, "%s%s%s\n\n", tag_info->sect_title_begin,
            utype_name_translation(putype), tag_info->sect_title_end);
    fprintf(doc, tag_info->subitem_begin, "cost");
    fprintf(doc,
            PL_("Cost: %d shield",
                "Cost: %d shields",
                utype_build_shield_cost_base(putype)),
            utype_build_shield_cost_base(putype));
    fprintf(doc, "%s", tag_info->subitem_end);
    fprintf(doc, tag_info->subitem_begin, "upkeep");
    fprintf(doc, _("Upkeep: %s"),
            helptext_unit_upkeep_str(putype));
    fprintf(doc, "%s", tag_info->subitem_end);
    fprintf(doc, tag_info->subitem_begin, "moves");
    fprintf(doc, _("Moves: %s"),
            move_points_text(putype->move_rate, TRUE));
    fprintf(doc, "%s", tag_info->subitem_end);
    fprintf(doc, tag_info->subitem_begin, "vision");
    fprintf(doc, _("Vision: %d"),
            (int)sqrt((double)putype->vision_radius_sq));
    fprintf(doc, "%s", tag_info->subitem_end);
    fprintf(doc, tag_info->subitem_begin, "attack");
    fprintf(doc, _("Attack: %d"),
            putype->attack_strength);
    fprintf(doc, "%s", tag_info->subitem_end);
    fprintf(doc, tag_info->subitem_begin, "defense");
    fprintf(doc, _("Defense: %d"),
            putype->defense_strength);
    fprintf(doc, "%s", tag_info->subitem_end);
    fprintf(doc, tag_info->subitem_begin, "firepower");
    fprintf(doc, _("Firepower: %d"),
            putype->firepower);
    fprintf(doc, "%s", tag_info->subitem_end);
    fprintf(doc, tag_info->subitem_begin, "hitpoints");
    fprintf(doc, _("Hitpoints: %d"),
            putype->hp);
    fprintf(doc, "%s", tag_info->subitem_end);
    fprintf(doc, tag_info->subitem_begin, "obsolete");
    fprintf(doc, _("Obsolete by: %s"),
            U_NOT_OBSOLETED == putype->obsoleted_by ?
            Q_("?utype:None") :
            utype_name_translation(putype->obsoleted_by));
    fprintf(doc, "%s", tag_info->subitem_end);
    fprintf(doc, tag_info->subitem_begin, "helptext");
    helptext_unit(buf, sizeof(buf), NULL, "", putype, FALSE);
    fprintf(doc, "%s", buf);
    fprintf(doc, "%s", tag_info->subitem_end);
    fprintf(doc, "%s", tag_info->item_end);
  } unit_type_iterate_end;

  manual_finalize(tag_info, doc, MANUAL_UNITS);

  return TRUE;
}
