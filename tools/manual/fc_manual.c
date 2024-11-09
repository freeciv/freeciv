/***********************************************************************
 Freeciv - Copyright (C) 2004 - The Freeciv Project
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

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#ifdef HAVE_SIGNAL_H
#include <signal.h>
#endif

#ifdef FREECIV_MSWINDOWS
#include <windows.h>
#endif

/* utility */
#include "capability.h"
#include "fc_cmdline.h"
#include "fciconv.h"
#include "fcintl.h"
#include "log.h"
#include "mem.h"
#include "registry.h"

/* common */
#include "capstr.h"
#include "connection.h"
#include "fc_cmdhelp.h"
#include "fc_interface.h"
#include "version.h"

/* client */
#include "client_main.h"
#include "climisc.h"
#include "helpdata.h"
#include "music.h"
#include "tilespec.h"

/* server */
#include "ruleload.h"
#include "settings.h"
#include "sernet.h"
#include "srv_main.h"
#include "stdinhand.h"

/* tools/shared */
#include "tools_fc_interface.h"

/* tools/manual */
#include "fc_manual.h"


struct tag_types html_tags = {
  /* file extension */
  "html",

  /* header */
  "<html><head><link rel=\"stylesheet\" type=\"text/css\" "
  "href=\"manual.css\"/><meta http-equiv=\"Content-Type\" "
  "content=\"text/html; charset=UTF-8\"/></head><body>\n\n",

  /* title begin */
  "<h1>",

  /* title end */
  "</h1>",

  /* section title begin */
  "<h3 class='section'>",

  /* section title end */
  "</h3>",

  /* image begin */
  "<img src=\"",

  /* image end */
  ".png\">",

  /* item begin */
  "<div class='item' id='%s%d'>\n",

  /* item end */
  "</div>\n",

  /* subitem begin */
  "<pre class='%s'>",

  /* subitem end */
  "</pre>\n",

  /* tail */
  "</body></html>",

  /* horizontal line */
  "<hr/>"
};

struct tag_types wiki_tags = {
  /* file extension */
  "mediawiki",

  /* header */
  " ",

  /* title begin */
  "=",

  /* title end */
  "=",

  /* section title begin */
  "===",

  /* section title end */
  "===",

  /* image begin */
  "[[Image:",

  /* image end */
  ".png]]",

  /* item begin */
  "----\n<!-- %s %d -->\n",

  /* item end */
  "\n",

  /* subitem begin */
  "<!-- %s -->\n",

  /* subitem end */
  "\n",

  /* tail */
  " ",

  /* horizontal line */
  "----"
};


void insert_client_build_info(char *outbuf, size_t outlen);

/* Needed for "About Freeciv" help */
const char *client_string = "freeciv-manual";

static char *ruleset = NULL;

/**********************************************************************//**
  Replace html special characters ('&', '<' and '>').
**************************************************************************/
char *html_special_chars(char *str, size_t *len)
{
  char *buf;

  buf = fc_strrep_resize(str, len, "&", "&amp;");
  buf = fc_strrep_resize(buf, len, "<", "&lt;");
  buf = fc_strrep_resize(buf, len, ">", "&gt;");

  return buf;
}


/*******************************************
  Useless stubs for compiling client code.
*******************************************/

/**********************************************************************//**
  Client stub
**************************************************************************/
void popup_help_dialog_string(const char *item)
{
  /* Empty stub. */
}

/**********************************************************************//**
  Client stub
**************************************************************************/
void popdown_help_dialog(void)
{
  /* Empty stub. */
}

struct tileset *tileset;

/**********************************************************************//**
  Client stub
**************************************************************************/
const char *tileset_name_get(struct tileset *t)
{
  return NULL;
}

/**********************************************************************//**
  Client stub
**************************************************************************/
const char *tileset_version(struct tileset *t)
{
  return NULL;
}

/**********************************************************************//**
  Client stub
**************************************************************************/
const char *tileset_summary(struct tileset *t)
{
  return NULL;
}

/**********************************************************************//**
  Client stub
**************************************************************************/
const char *tileset_description(struct tileset *t)
{
  return NULL;
}

/**********************************************************************//**
  Client stub
**************************************************************************/
const char *current_musicset_name(void)
{
  return NULL;
}

/**********************************************************************//**
  Client stub
**************************************************************************/
const char *current_musicset_version(void)
{
  return NULL;
}

/**********************************************************************//**
  Client stub
**************************************************************************/
const char *current_musicset_summary(void)
{
  return NULL;
}

/**********************************************************************//**
  Client stub
**************************************************************************/
const char *current_musicset_description(void)
{
  return NULL;
}

/**********************************************************************//**
  Mostly a client stub.
**************************************************************************/
enum client_states client_state(void)
{
  return C_S_INITIAL;
}

/**********************************************************************//**
  Mostly a client stub.
**************************************************************************/
bool client_nation_is_in_current_set(const struct nation_type *pnation)
{
  /* Currently, there is no way to select a nation set for freeciv-manual.
   * Then, let's assume we want to print help for all nations. */
  return TRUE;
}

/**********************************************************************//**
  Create manual file, and do the generic header for it.

  @param tag_info       Tag set to use
  @param manual_number  Number of the manual page
  @return               Handle of the created file
**************************************************************************/
FILE *manual_start(struct tag_types *tag_info, int manual_number)
{
  char filename[40];
  FILE *doc;

  fc_snprintf(filename, sizeof(filename), "%s%d.%s",
              game.server.rulesetdir, manual_number + 1, tag_info->file_ext);

  if (!is_reg_file_for_access(filename, TRUE)
      || !(doc = fc_fopen(filename, "w"))) {
    log_error(_("Could not write manual file %s."), filename);

    return NULL;
  }

  fprintf(doc, "%s", tag_info->header);
  fprintf(doc, "<!-- Generated by freeciv-manual version %s -->\n\n",
          freeciv_datafile_version());

  return doc;
}

/**********************************************************************//**
  Generic finalizing step for a manual page. Closes the file.

  @param tag_info     Tag set to use
  @param doc          Manual handle
  @param manual       Type of the manual
**************************************************************************/
void manual_finalize(struct tag_types *tag_info, FILE *doc,
                     enum manuals manual)
{
  fprintf(doc, "%s", tag_info->tail);
  fclose(doc);
  log_normal(_("%s (%d) manual successfully written."),
             _(manuals_name(manual)), manual + 1);
}

/**********************************************************************//**
  Write a server manual, then quit.
**************************************************************************/
static bool manual_command(struct tag_types *tag_info)
{
  /* Reset aifill to zero */
  game.info.aifill = 0;

  if (!load_rulesets(NULL, NULL, FALSE, NULL, FALSE, FALSE, FALSE)) {
    /* Failed to load correct ruleset */
    return FALSE;
  }

  if (!manual_settings(tag_info)
      || !manual_commands(tag_info)
      || !manual_terrain(tag_info)
      || !manual_extras(tag_info)
      || !manual_buildings(tag_info)
      || !manual_governments(tag_info)
      || !manual_units(tag_info)
      || !manual_uclasses(tag_info)
      || !manual_techs(tag_info)) {
    return FALSE;
  }

  return TRUE;
}

/**********************************************************************//**
  Entry point of whole freeciv-manual program
**************************************************************************/
int main(int argc, char **argv)
{
  int inx;
  bool showhelp = FALSE;
  bool showvers = FALSE;
  char *option = NULL;
  int retval = EXIT_SUCCESS;
  struct tag_types *tag_info = &html_tags;

  /* Load Windows post-crash debugger */
#ifdef FREECIV_MSWINDOWS
# ifndef FREECIV_NDEBUG
  if (LoadLibrary("exchndl.dll") == NULL) {
#  ifdef FREECIV_DEBUG
    fprintf(stderr, "exchndl.dll could not be loaded, no crash debugger\n");
#  endif /* FREECIV_DEBUG */
  }
# endif /* FREECIV_NDEBUG */
#endif /* FREECIV_MSWINDOWS */

  /* Initialize the fc_interface functions needed to generate the help
   * text.
   * fc_interface_init_tool() includes low level support like
   * guaranteeing that fc_vsnprintf() will work after it,
   * so this need to be early. */
  fc_interface_init_tool();

  registry_module_init();
  init_character_encodings(FC_DEFAULT_DATA_ENCODING, FALSE);

  /* Set the default log level. */
  srvarg.loglevel = LOG_NORMAL;

  /* Parse command-line arguments... */
  inx = 1;
  while (inx < argc) {
    if ((option = get_option_malloc("--ruleset", argv, &inx, argc, TRUE))) {
      if (ruleset != NULL) {
        fc_fprintf(stderr, _("Multiple rulesets requested. Only one "
                             "ruleset at a time is supported.\n"));
      } else {
        ruleset = option;
      }
    } else if (is_option("--help", argv[inx])) {
      showhelp = TRUE;
      break;
    } else if (is_option("--version", argv[inx])) {
      showvers = TRUE;
    } else if ((option = get_option_malloc("--log", argv, &inx, argc, TRUE))) {
      srvarg.log_filename = option;
    } else if (is_option("--wiki", argv[inx])) {
      tag_info = &wiki_tags;
#ifndef FREECIV_NDEBUG
    } else if (is_option("--Fatal", argv[inx])) {
      if (inx + 1 >= argc || '-' == argv[inx + 1][0]) {
        srvarg.fatal_assertions = SIGABRT;
      } else if (str_to_int(argv[inx + 1], &srvarg.fatal_assertions)) {
        inx++;
      } else {
        fc_fprintf(stderr, _("Invalid signal number \"%s\".\n"),
                   argv[inx + 1]);
        inx++;
        showhelp = TRUE;
      }
#endif /* FREECIV_NDEBUG */
    } else if ((option = get_option_malloc("--debug", argv, &inx, argc, FALSE))) {
      if (!log_parse_level_str(option, &srvarg.loglevel)) {
        showhelp = TRUE;
        break;
      }
      free(option);
    } else {
      fc_fprintf(stderr, _("Unrecognized option: \"%s\"\n"), argv[inx]);
      exit(EXIT_FAILURE);
    }
    inx++;
  }

  init_our_capability();

  /* Must be before con_log_init() */
  init_connections();
  con_log_init(srvarg.log_filename, srvarg.loglevel,
               srvarg.fatal_assertions);
  /* Logging available after this point */

  /* Get common code to treat us as a tool. */
  i_am_tool();

  /* Initialize game with default values */
  game_init(FALSE);

  /* Set ruleset user requested in to use */
  if (ruleset != NULL) {
    sz_strlcpy(game.server.rulesetdir, ruleset);
  }

  settings_init(FALSE);

  if (showvers && !showhelp) {
    fc_fprintf(stderr, "%s \n", freeciv_name_version());
    exit(EXIT_SUCCESS);
  } else if (showhelp) {
    struct cmdhelp *help = cmdhelp_new(argv[0]);

#ifdef FREECIV_DEBUG
    cmdhelp_add(help, "d",
                /* TRANS: "debug" is exactly what user must type, do not translate. */
                _("debug LEVEL"),
                _("Set debug log level (one of f,e,w,n,v,d, or "
                  "d:file1,min,max:...)"));
#else  /* FREECIV_DEBUG */
    cmdhelp_add(help, "d",
                /* TRANS: "debug" is exactly what user must type, do not translate. */
                _("debug LEVEL"),
                _("Set debug log level (one of f,e,w,n,v)"));
#endif /* FREECIV_DEBUG */
#ifndef FREECIV_NDEBUG
    cmdhelp_add(help, "F",
                /* TRANS: "Fatal" is exactly what user must type, do not translate. */
                _("Fatal [SIGNAL]"),
                _("Raise a signal on failed assertion"));
#endif /* FREECIV_NDEBUG */
    cmdhelp_add(help, "h", "help",
                _("Print a summary of the options"));
    cmdhelp_add(help, "l",
                  /* TRANS: "log" is exactly what user must type, do not translate. */
                _("log FILE"),
                _("Use FILE as logfile"));
    cmdhelp_add(help, "r",
                  /* TRANS: "ruleset" is exactly what user must type, do not translate. */
                _("ruleset RULESET"),
                _("Make manual for RULESET"));
    cmdhelp_add(help, "v", "version",
                _("Print the version number"));
    cmdhelp_add(help, "w", "wiki",
                _("Write manual in wiki format"));

    /* The function below prints a header and footer for the options.
     * Furthermore, the options are sorted. */
    cmdhelp_display(help, TRUE, FALSE, TRUE);
    cmdhelp_destroy(help);

    exit(EXIT_SUCCESS);
  }

  if (!manual_command(tag_info)) {
    retval = EXIT_FAILURE;
  }

  con_log_close();
  registry_module_close();
  libfreeciv_free();
  cmdline_option_values_free();

  return retval;
}

/**********************************************************************//**
  Empty function required by helpdata
**************************************************************************/
void insert_client_build_info(char *outbuf, size_t outlen)
{
  /* Nothing here */
}
