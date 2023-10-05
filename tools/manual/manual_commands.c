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
#include "commands.h"

/* tools/manual */
#include "fc_manual.h"

/**********************************************************************//**
  Write commands manual page

  @param  tag_info  Tag set to use
  @return           Success
**************************************************************************/
bool manual_commands(struct tag_types *tag_info)
{
  FILE *doc;
  int i;

  doc = manual_start(tag_info, MANUAL_COMMANDS);

  if (doc == NULL) {
    return FALSE;
  }

  /* TRANS: markup ... Freeciv version ... markup */
  fprintf(doc, _("%sFreeciv %s server commands%s\n\n"), tag_info->title_begin,
          VERSION_STRING, tag_info->title_end);
  for (i = 0; i < CMD_NUM; i++) {
    const struct command *cmd = command_by_number(i);

    fprintf(doc, tag_info->item_begin, "cmd", i);
    fprintf(doc, "%s%s  -  %s%s\n\n", tag_info->sect_title_begin,
            command_name(cmd), command_short_help(cmd),
            tag_info->sect_title_end);
    if (command_synopsis(cmd)) {
      char *cmdstr = fc_strdup(command_synopsis(cmd));
      size_t cmdstr_len = strlen(cmdstr) + 1;

      cmdstr = html_special_chars(cmdstr, &cmdstr_len);
      fprintf(doc, _("<table>\n<tr>\n<td valign=\"top\">"
                     "<pre>Synopsis:</pre></td>\n<td>"));
      fprintf(doc, "<pre>%s</pre></td></tr></table>", cmdstr);
      free(cmdstr);
    }
    fprintf(doc, _("<p class=\"level\">Level: %s</p>\n"),
            cmdlevel_name(command_level(cmd)));
    {
      char *help = command_extra_help(cmd);

      if (help) {
        size_t help_len = strlen(help) + 1;

        fc_break_lines(help, LINE_BREAK);
        help = html_special_chars(help, &help_len);
        fprintf(doc, "\n");
        fprintf(doc, _("<p>Description:</p>\n\n"));
        fprintf(doc, "<pre>%s</pre>\n", help);
        free(help);
      }
    }

    fprintf(doc, "%s", tag_info->item_end);
  }

  manual_finalize(tag_info, doc, MANUAL_COMMANDS);

  return TRUE;
}
