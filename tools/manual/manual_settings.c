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

/* server */
#include "settings.h"

/* tools/manual */
#include "fc_manual.h"

/**********************************************************************//**
  Write settings manual page

  @param  tag_info  Tag set to use
  @return           Success
**************************************************************************/
bool manual_settings(struct tag_types *tag_info)
{
  FILE *doc;
  struct connection dummy_conn;
  int i;

  doc = manual_start(tag_info, MANUAL_SETTINGS);

  if (doc == NULL) {
    return FALSE;
  }

  /* Default client access. */
  connection_common_init(&dummy_conn);
  dummy_conn.access_level = ALLOW_CTRL;

  /* TRANS: markup ... Freeciv version ... markup */
  fprintf(doc, _("%sFreeciv %s server settings%s\n\n"), tag_info->title_begin,
          VERSION_STRING, tag_info->title_end);
  settings_iterate(SSET_ALL, pset) {
    char buf[256];
    const char *sethelp;

    fprintf(doc, tag_info->item_begin, "setting", setting_number(pset));
    fprintf(doc, "%s%s - %s%s\n\n", tag_info->sect_title_begin,
            setting_name(pset), _(setting_short_help(pset)),
            tag_info->sect_title_end);
    sethelp = _(setting_extra_help(pset, TRUE));
    if (strlen(sethelp) > 0) {
      char *help = fc_strdup(sethelp);
      size_t help_len = strlen(help) + 1;

      fc_break_lines(help, LINE_BREAK);
      help = html_special_chars(help, &help_len);
      fprintf(doc, "<pre>%s</pre>\n\n", help);
      FC_FREE(help);
    }
    fprintf(doc, "<p class=\"misc\">");
    fprintf(doc, _("Level: %s.<br>"),
            _(sset_level_name(setting_level(pset))));
    fprintf(doc, _("Category: %s.<br>"),
            _(sset_category_name(setting_category(pset))));

    /* First check if the setting is locked because this is included in
     * the function setting_is_changeable() */
    if (setting_ruleset_locked(pset)) {
      fprintf(doc, _("Is locked by the ruleset."));
    } else if (!setting_is_changeable(pset, &dummy_conn, NULL, 0)) {
      fprintf(doc, _("Can only be used in server console."));
    }

    fprintf(doc, "</p>\n");
    setting_default_name(pset, TRUE, buf, sizeof(buf));
    switch (setting_type(pset)) {
    case SST_INT:
      fprintf(doc, "\n<p class=\"bounds\">%s %d, %s %s, %s %d</p>\n",
              _("Minimum:"), setting_int_min(pset),
              _("Default:"), buf,
              _("Maximum:"), setting_int_max(pset));
      break;
    case SST_ENUM:
      {
        const char *value;

        fprintf(doc, "\n<p class=\"bounds\">%s</p><ul>",
                _("Possible values:"));
        for (i = 0; (value = setting_enum_val(pset, i, FALSE)); i++) {
          fprintf(doc, "\n<li><p class=\"bounds\"> %s: \"%s\"</p>",
                  value, setting_enum_val(pset, i, TRUE));
        }
        fprintf(doc, "</ul>\n");
      }
      break;
    case SST_BITWISE:
      {
        const char *value;

        fprintf(doc, "\n<p class=\"bounds\">%s</p><ul>",
                _("Possible values (option can take any number of these):"));
        for (i = 0; (value = setting_bitwise_bit(pset, i, FALSE)); i++) {
          fprintf(doc, "\n<li><p class=\"bounds\"> %s: \"%s\"</p>",
                  value, setting_bitwise_bit(pset, i, TRUE));
        }
        fprintf(doc, "</ul>\n");
      }
      break;
    case SST_BOOL:
    case SST_STRING:
      break;
    case SST_COUNT:
      fc_assert(setting_type(pset) != SST_COUNT);
      break;
    }
    if (SST_INT != setting_type(pset)) {
      fprintf(doc, "\n<p class=\"bounds\">%s %s</p>\n",
              _("Default:"), buf);
    }
    if (setting_non_default(pset)) {
      fprintf(doc, _("\n<p class=\"changed\">Value set to %s</p>\n"),
              setting_value_name(pset, TRUE, buf, sizeof(buf)));
    }

    fprintf(doc, "%s", tag_info->item_end);
  } settings_iterate_end;

  manual_finalize(tag_info, doc, MANUAL_SETTINGS);

  return TRUE;
}
