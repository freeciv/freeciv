/****************************************************************************
 Freeciv - Copyright (C) 2005  - M.C. Kaufman
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
****************************************************************************/

#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* utility */
#include "fcintl.h"
#include "log.h"
#include "md5.h"
#include "shared.h"
#include "support.h"

/* common */
#include "connection.h"
#include "packets.h"

/* server */
#include "connecthand.h"
#include "notify.h"
#include "sernet.h"
#include "srv_main.h"

/* server/scripting */
#ifdef HAVE_FCDB
#include "script_fcdb.h"
#endif /* HAVE_FCDB */

#include "fcdb.h"

/* If HAVE_FCDB is set, the freeciv database module is compiled. Else only
 * some dummy functions are defiend. */
#ifdef HAVE_FCDB

/* Default database backend */
#define DEFAULT_FCDB_BACKEND  "mysql"

/* where our mysql database is located and how to get to it */
#define DEFAULT_FCDB_HOST     "localhost"
#define DEFAULT_FCDB_PORT     "3306"
#define DEFAULT_FCDB_USER     "anonymous"
#define DEFAULT_FCDB_PASSWORD ""

/* the database where our table is located */
#define DEFAULT_FCDB_DATABASE "test"

/* The definition of the tables can be found in the database lua script.
 * See ./data/database.lua. */
#define DEFAULT_FCDB_TABLE_USER      "fcdb_auth"
#define DEFAULT_FCDB_TABLE_LOG       "fcdb_log"

/* How much information dump functions show */
enum show_source_type {
  SST_NONE,
  SST_DEFAULT,
  SST_ALL
};

enum fcdb_option_source {
  AOS_DEFAULT,  /* Internal default         */
  AOS_FILE,     /* Read from config file    */
  AOS_SET       /* Set, currently not used  */
};

struct fcdb_option {
  enum fcdb_option_type type;
  enum fcdb_option_source source;
  char *value;
};

struct fcdb_option fcdb_config[FCDB_OPTION_TYPE_COUNT];

static void fcdb_print_option(enum log_level level,
                              enum show_source_type show_source,
                              bool show_value,
                              const struct fcdb_option *target);
static void fcdb_print_config(enum log_level level,
                              enum show_source_type show_source,
                              bool show_value);
static bool fcdb_set_option(struct fcdb_option *target, const char *value,
                            enum fcdb_option_source source);
static void fcdb_load_option(struct section_file *secfile,
                             struct fcdb_option *target);
static bool fcdb_load_config(const char *filename);

/****************************************************************************
  Output information about one fcdb option.
****************************************************************************/
static void fcdb_print_option(enum log_level level,
                              enum show_source_type show_source,
                              bool show_value,
                              const struct fcdb_option *target)
{
  char buffer[512];
  bool real_show_source;

  buffer[0] = '\0';

  real_show_source = (show_source == SST_ALL
                      || (show_source == SST_DEFAULT
                          && target->source == AOS_DEFAULT));

  if (show_value || real_show_source) {
    /* We will print this line. Begin it. */
    /* TRANS: Further information about option will follow. */
    fc_snprintf(buffer, sizeof(buffer), _("fcdb option \"%s\":"),
                fcdb_option_type_name(target->type));
  }

  if (show_value) {
    cat_snprintf(buffer, sizeof(buffer), " \"%s\"", target->value);
  }
  if (real_show_source) {
    switch(target->source) {
     case AOS_DEFAULT:
       if (show_source == SST_DEFAULT) {
         cat_snprintf(buffer, sizeof(buffer),
                      /* TRANS: After 'fcdb option "user":'. Option value
                         may have been inserted between these. */
                      _(" missing from config file (using default)"));
       } else {
         /* TRANS: fcdb option originates from internal default */
         cat_snprintf(buffer, sizeof(buffer), _(" (default)"));
       }
       break;
     case AOS_FILE:
       /* TRANS: fcdb option originates from config file */
       cat_snprintf(buffer, sizeof(buffer), _(" (config)"));
       break;
     case AOS_SET:
       /* TRANS: fcdb option has been set from prompt */
       cat_snprintf(buffer, sizeof(buffer), _(" (set)"));
       break;
    }
  }

  if (buffer[0] != '\0') {
    /* There is line to print */
    log_base(level, "%s", buffer);
  }
}

/****************************************************************************
  Output fcdb config information.
****************************************************************************/
static void fcdb_print_config(enum log_level level,
                              enum show_source_type show_source,
                              bool show_value)
{
  enum fcdb_option_type type;

  for (type = fcdb_option_type_begin(); type != fcdb_option_type_end();
       type = fcdb_option_type_next(type)) {
    if (type == FCDB_OPTION_TYPE_PASSWORD) {
      /* Do not show the password. */
      show_value = FALSE;
    }
    fcdb_print_option(level, show_source, show_value, &fcdb_config[type]);
  }
}

/****************************************************************************
  Set one fcdb option.
****************************************************************************/
static bool fcdb_set_option(struct fcdb_option *target, const char *value,
                            enum fcdb_option_source source)
{
  if (value != NULL && target->type == FCDB_OPTION_TYPE_PORT) {
    /* Port value must be all numeric. */
    int i;

    for (i = 0; value[i] != '\0'; i++) {
      if (value[i] < '0' || value[i] > '9') {
        log_error(_("Illegal value for fcdb port: \"%s\""), value);
        return FALSE;
      }
    }
  }

  if (value == NULL) {
    if (target->value != NULL) {
      free(target->value);
    }
    target->value = NULL;
  } else {
    target->value = fc_realloc(target->value, strlen(value) + 1);
    memcpy(target->value, value, strlen(value) + 1);
  }
  target->source = source;

  return TRUE;
}

/****************************************************************************
  Load value for one fcdb option from section_file.
****************************************************************************/
static void fcdb_load_option(struct section_file *secfile,
                             struct fcdb_option *target)
{
  const char *value
    = secfile_lookup_str(secfile, "fcdb.%s",
                         fcdb_option_type_name(target->type));

  if (NULL != value) {
    /* We really loaded something from file */
    fcdb_set_option(target, value, AOS_FILE);
  }
}

/****************************************************************************
  Load fcdb configuration from file.
  We use filename just like user gave it to us.
  No searching from datadirs, if file with same name exist there!
****************************************************************************/
static bool fcdb_load_config(const char *filename)
{
  struct section_file *secfile;
  enum fcdb_option_type type;

  fc_assert_ret_val(NULL != filename, FALSE);

  if (!(secfile = secfile_load(filename, FALSE))) {
    log_error(_("Cannot load fcdb config file '%s':\n%s"), filename,
              secfile_error());
    return FALSE;
  }

  for (type = fcdb_option_type_begin(); type != fcdb_option_type_end();
       type = fcdb_option_type_next(type)) {
    fcdb_load_option(secfile, &fcdb_config[type]);
  }

  secfile_check_unused(secfile);
  secfile_destroy(secfile);

  return TRUE;
}

/****************************************************************************
  Initialize freeciv database system
****************************************************************************/
bool fcdb_init(const char *conf_file)
{
  static bool first_init = TRUE;

  if (first_init) {
    enum fcdb_option_type type;

    /* Run just once when program starts */
    for (type = fcdb_option_type_begin(); type != fcdb_option_type_end();
         type = fcdb_option_type_next(type)) {
      fcdb_config[type].value = NULL;
      fcdb_config[type].type = type;
    }

    first_init = FALSE;
  }

#define fcdb_set_option_default(_type, _default)                            \
  fcdb_set_option(&fcdb_config[_type], _default, AOS_DEFAULT)

  fcdb_set_option_default(FCDB_OPTION_TYPE_BACKEND, DEFAULT_FCDB_BACKEND);
  fcdb_set_option_default(FCDB_OPTION_TYPE_HOST, DEFAULT_FCDB_HOST);
  fcdb_set_option_default(FCDB_OPTION_TYPE_PORT, DEFAULT_FCDB_PORT);
  fcdb_set_option_default(FCDB_OPTION_TYPE_USER, DEFAULT_FCDB_USER);
  fcdb_set_option_default(FCDB_OPTION_TYPE_PASSWORD, DEFAULT_FCDB_PASSWORD);
  fcdb_set_option_default(FCDB_OPTION_TYPE_DATABASE, DEFAULT_FCDB_DATABASE);
  fcdb_set_option_default(FCDB_OPTION_TYPE_TABLE_USER,
                          DEFAULT_FCDB_TABLE_USER);
  fcdb_set_option_default(FCDB_OPTION_TYPE_TABLE_LOG,
                          DEFAULT_FCDB_TABLE_LOG);
#undef fcdb_set_option_default

  if (conf_file && strcmp(conf_file, "-")) {
    if (!fcdb_load_config(conf_file)) {
      return FALSE;
    }

    /* Print all options in LOG_DEBUG level... */
    fcdb_print_config(LOG_DEBUG, SST_ALL, TRUE);

    /* ...and those missing from config file with LOG_NORMAL too */
    fcdb_print_config(LOG_NORMAL, SST_DEFAULT, FALSE);

  } else {
    log_debug("No fcdb config file. Using defaults");
  }

  return script_fcdb_init(NULL);
}

/****************************************************************************
  Return the selected fcdb config value.
****************************************************************************/
const char *fcdb_option_get(enum fcdb_option_type type)
{
  fc_assert_ret_val(fcdb_option_type_is_valid(type), NULL);

  return fcdb_config[type].value;
}

/****************************************************************************
  Free resources allocated by fcdb system.
****************************************************************************/
void fcdb_free(void)
{
  enum fcdb_option_type type;

  script_fcdb_free();

  for (type = fcdb_option_type_begin(); type != fcdb_option_type_end();
       type = fcdb_option_type_next(type)) {
    fcdb_set_option(&fcdb_config[type], NULL, AOS_DEFAULT);
  }
}

#else  /* HAVE_FCDB */

/****************************************************************************
  Dummy function - Initialize freeciv database system
****************************************************************************/
bool fcdb_init(const char *conf_file)
{
  return TRUE;
}

/****************************************************************************
  Dummy function - Return the selected fcdb config value.
****************************************************************************/
const char *fcdb_option_get(enum fcdb_option_type type)
{
  return NULL;
}

/****************************************************************************
  Dummy function - Free resources allocated by fcdb system.
****************************************************************************/
void fcdb_free(void)
{
  return;
}
#endif /* HAVE_FCDB */
