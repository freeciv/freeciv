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

/* tools/manual */
#include "fc_manual.h"

/**********************************************************************//**
  Write terrain manual page

  @param  tag_info  Tag set to use
  @return           Success
**************************************************************************/
bool manual_terrain(struct tag_types *tag_info)
{
  FILE *doc;
  int ri;

  doc = manual_start(tag_info, MANUAL_TERRAIN);

  if (doc == NULL) {
    return FALSE;
  }

  /* TRANS: Markup ... Freeciv version ... ruleset name ... markup */
  fprintf(doc, _("%sFreeciv %s terrain help (%s)%s\n\n"), tag_info->title_begin,
          VERSION_STRING, game.control.name, tag_info->title_end);
  fprintf(doc, "<table><tr bgcolor=#9bc3d1><th>%s</th>", _("Terrain"));
  fprintf(doc, "<th>F/P/T</th><th>%s</th>", _("Resources"));
  fprintf(doc, "<th>%s<br/>%s</th>", _("Move cost"), _("Defense bonus"));
  fprintf(doc, "<th>%s<br/>%s<br/>%s<br/>%s<br/>%s<br/>%s<br/>(%s)</th>",
          _("Irrigation"), _("Cultivate"), _("Mining"), _("Plant"), _("Transform"),
          /* xgettext:no-c-format */
          _("% of Road bonus"), _("turns"));
  ri = 0;
  if (game.control.num_road_types > 0) {
    fprintf(doc, "<th>");
  }
  extra_type_by_cause_iterate(EC_ROAD, pextra) {
    if (++ri < game.control.num_road_types) {
      fprintf(doc, "%s<br/>", extra_name_translation(pextra));
    } else {
      /* Last one */
      fprintf(doc, "%s</th>", extra_name_translation(pextra));
    }
  } extra_type_by_cause_iterate_end;
  fprintf(doc, "</tr>\n\n");
  terrain_type_iterate(pterrain) {
    struct extra_type **r;
    struct universal for_terr
      = { .kind = VUT_TERRAIN, .value = { .terrain = pterrain }};

    if (0 == strlen(terrain_rule_name(pterrain))) {
      /* Must be a disabled piece of terrain */
      continue;
    }

    fprintf(doc, "<tr><td>%s%s%s<br/>%s</td>",
            tag_info->image_begin, pterrain->graphic_str,
            tag_info->image_end,
            terrain_name_translation(pterrain));
    fprintf(doc, "<td>%d/%d/%d</td>\n",
            pterrain->output[O_FOOD], pterrain->output[O_SHIELD],
            pterrain->output[O_TRADE]);

    /* TODO: Include resource frequency information */
    fprintf(doc, "<td><table width=\"100%%\">\n");
    for (r = pterrain->resources; *r; r++) {
      fprintf(doc, "<tr><td>%s%s%s</td><td>%s</td>"
              "<td align=\"right\">%d/%d/%d</td></tr>\n",
              tag_info->image_begin, (*r)->graphic_str, tag_info->image_end,
              extra_name_translation(*r),
              (*r)->data.resource->output[O_FOOD],
              (*r)->data.resource->output[O_SHIELD],
              (*r)->data.resource->output[O_TRADE]);
    }
    fprintf(doc, "</table></td>\n");

    fprintf(doc, "<td align=\"center\">%d<br/>+%d%%</td>\n",
            pterrain->movement_cost, pterrain->defense_bonus);

    fprintf(doc, "<td><table width=\"100%%\">\n");
    if (action_id_univs_not_blocking(ACTION_IRRIGATE,
                                     NULL, &for_terr)) {
      fprintf(doc, "<tr><td>+%d F</td>", pterrain->irrigation_food_incr);
      if (pterrain->irrigation_time == 0) {
        fprintf(doc, "<td align=\"right\">(-)</td></tr>\n");
      } else {
        fprintf(doc, "<td align=\"right\">(%d)</td></tr>\n",
                pterrain->irrigation_time);
      }
    } else {
      fprintf(doc, "<tr><td>%s</td></tr>\n", _("impossible"));
    }
    if (pterrain->cultivate_result != NULL
        && action_id_univs_not_blocking(ACTION_CULTIVATE,
                                        NULL, &for_terr)) {
      fprintf(doc, "<tr><td>%s</td>",
              terrain_name_translation(pterrain->cultivate_result));
      if (pterrain->cultivate_time == 0) {
        fprintf(doc, "<td align=\"right\">(-)</td></tr>\n");
      } else {
        fprintf(doc, "<td align=\"right\">(%d)</td></tr>\n",
                pterrain->cultivate_time);
      }
    } else {
      fprintf(doc, "<tr><td>%s</td></tr>\n", _("impossible"));
    }
    if (action_id_univs_not_blocking(ACTION_MINE, NULL, &for_terr)) {
      fprintf(doc, "<tr><td>+%d P</td>", pterrain->mining_shield_incr);
      if (pterrain->mining_time == 0) {
        fprintf(doc, "<td align=\"right\">(-)</td></tr>\n");
      } else {
        fprintf(doc, "<td align=\"right\">(%d)</td></tr>\n",
                pterrain->mining_time);
      }
    } else {
      fprintf(doc, "<tr><td>%s</td></tr>\n", _("impossible"));
    }
    if (pterrain->plant_result != NULL
        && action_id_univs_not_blocking(ACTION_PLANT,
                                        NULL, &for_terr)) {
      fprintf(doc, "<tr><td>%s</td>",
              terrain_name_translation(pterrain->plant_result));
      if (pterrain->plant_time == 0) {
        fprintf(doc, "<td align=\"right\">(-)</td></tr>\n");
      } else {
        fprintf(doc, "<td align=\"right\">(%d)</td></tr>\n",
                pterrain->plant_time);
      }
    } else {
      fprintf(doc, "<tr><td>%s</td></tr>\n", _("impossible"));
    }

    if (pterrain->transform_result
        && action_id_univs_not_blocking(ACTION_TRANSFORM_TERRAIN,
                                        NULL, &for_terr)) {
      fprintf(doc, "<tr><td>%s</td>",
              terrain_name_translation(pterrain->transform_result));
      if (pterrain->transform_time == 0) {
        fprintf(doc, "<td align=\"right\">(-)</td></tr>\n");
      } else {
        fprintf(doc, "<td align=\"right\">(%d)</td></tr>\n",
                pterrain->transform_time);
      }
    } else {
      fprintf(doc, "<tr><td>-</td><td align=\"right\">(-)</td></tr>\n");
    }
    fprintf(doc, "<tr><td>%d / %d / %d</td></tr>\n</table></td>\n",
            pterrain->road_output_incr_pct[O_FOOD],
            pterrain->road_output_incr_pct[O_SHIELD],
            pterrain->road_output_incr_pct[O_TRADE]);

    ri = 0;
    if (game.control.num_road_types > 0) {
      fprintf(doc, "<td>");
    }
    extra_type_by_cause_iterate(EC_ROAD, pextra) {
      int btime = terrain_extra_build_time(pterrain, ACTIVITY_GEN_ROAD, pextra);

      if (++ri < game.control.num_road_types) {
        if (btime == 0) {
          fprintf(doc, "- / ");
        } else {
          fprintf(doc, "%d / ", btime);
        }
      } else {
        if (btime == 0) {
          fprintf(doc, "-</td>");
        } else {
          fprintf(doc, "%d</td>", btime);
        }
      }
    } extra_type_by_cause_iterate_end;
    fprintf(doc, "</tr><tr><td colspan=\"7\">\n%s\n</td></tr>\n\n", tag_info->hline);
  } terrain_type_iterate_end;

  fprintf(doc, "</table>\n");

  manual_finalize(tag_info, doc, MANUAL_TERRAIN);

  return TRUE;
}
