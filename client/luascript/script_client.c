/*****************************************************************************
 Freeciv - Copyright (C) 2005 - The Freeciv Project
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
*****************************************************************************/

#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

#include <stdarg.h>
#include <stdlib.h>
#include <time.h>

/* dependencies/lua */
#include "lua.h"
#include "lualib.h"

/* dependencies/tolua */
#include "tolua.h"

/* utility */
#include "log.h"

/* common */
#include "featured_text.h"

/* common/scriptcore */
#include "api_game_specenum.h"
#include "luascript.h"
#include "tolua_common_a_gen.h"
#include "tolua_common_z_gen.h"
#include "tolua_game_gen.h"
#include "tolua_signal_gen.h"

/* client */
#include "luaconsole_common.h"

/* client/luascript */
#include "tolua_client_gen.h"

#include "script_client.h"

/*****************************************************************************
  Lua virtual machine state.
*****************************************************************************/
static struct fc_lua *fcl = NULL;

/*****************************************************************************
  Optional game script code (useful for scenarios).
*****************************************************************************/
static char *script_client_code = NULL;

static void script_client_vars_init(void);
static void script_client_vars_free(void);
static void script_client_vars_load(struct section_file *file);
static void script_client_vars_save(struct section_file *file);
static void script_client_code_init(void);
static void script_client_code_free(void);
static void script_client_code_load(struct section_file *file);
static void script_client_code_save(struct section_file *file);

static void script_client_output(struct fc_lua *fcl, enum log_level level,
                                 const char *format, ...)
            fc__attribute((__format__ (__printf__, 3, 4)));

static void script_client_signal_create(void);

/*****************************************************************************
  Parse and execute the script in str
*****************************************************************************/
bool script_client_do_string(const char *str)
{
  int status = luascript_do_string(fcl, str, "cmd");
  return (status == 0);
}

/*****************************************************************************
  Parse and execute the script at filename.
*****************************************************************************/
bool script_client_do_file(const char *filename)
{
  int status = luascript_do_file(fcl, filename);
  return (status == 0);
}

/*****************************************************************************
  Invoke the 'callback_name' Lua function.
*****************************************************************************/
bool script_client_callback_invoke(const char *callback_name, int nargs,
                                   enum api_types *parg_types, va_list args)
{
  return luascript_callback_invoke(fcl, callback_name, nargs, parg_types,
                                   args);
}

/*****************************************************************************
  Mark any, if exported, full userdata representing 'object' in
  the current script state as 'Nonexistent'.
  This changes the type of the lua variable.
*****************************************************************************/
void script_client_remove_exported_object(void *object)
{
  luascript_remove_exported_object(fcl, object);
}

/*****************************************************************************
  Initialize the game script variables.
*****************************************************************************/
static void script_client_vars_init(void)
{
  /* nothing */
}

/*****************************************************************************
  Free the game script variables.
*****************************************************************************/
static void script_client_vars_free(void)
{
  /* nothing */
}

/*****************************************************************************
  Load the game script variables in file.
*****************************************************************************/
static void script_client_vars_load(struct section_file *file)
{
  luascript_vars_load(fcl, file, "script.vars");
}

/*****************************************************************************
  Save the game script variables to file.
*****************************************************************************/
static void script_client_vars_save(struct section_file *file)
{
  luascript_vars_save(fcl, file, "script.vars");
}

/*****************************************************************************
  Initialize the optional game script code (useful for scenarios).
*****************************************************************************/
static void script_client_code_init(void)
{
  script_client_code = NULL;
}

/*****************************************************************************
  Free the optional game script code (useful for scenarios).
*****************************************************************************/
static void script_client_code_free(void)
{
  if (script_client_code) {
    free(script_client_code);
    script_client_code = NULL;
  }
}

/*****************************************************************************
  Load the optional game script code from file (useful for scenarios).
*****************************************************************************/
static void script_client_code_load(struct section_file *file)
{
  if (!script_client_code) {
    const char *code;
    const char *section = "script.code";

    code = secfile_lookup_str_default(file, "", "%s", section);
    script_client_code = fc_strdup(code);
    luascript_do_string(fcl, script_client_code, section);
  }
}

/*****************************************************************************
  Save the optional game script code to file (useful for scenarios).
*****************************************************************************/
static void script_client_code_save(struct section_file *file)
{
  if (script_client_code) {
    secfile_insert_str_noescape(file, script_client_code, "script.code");
  }
}

/*****************************************************************************
  Initialize the scripting state.
*****************************************************************************/
bool script_client_init(void)
{
  if (fcl != NULL) {
    fc_assert_ret_val(fcl->state != NULL, FALSE);

    return TRUE;
  }

  fcl = luascript_new(script_client_output);
  if (!fcl) {
    luascript_destroy(fcl);
    fcl = NULL;

    return FALSE;
  }

  tolua_common_a_open(fcl->state);
  api_specenum_open(fcl->state);
  tolua_game_open(fcl->state);
  tolua_signal_open(fcl->state);
  tolua_client_open(fcl->state);
  tolua_common_z_open(fcl->state);

  script_client_code_init();
  script_client_vars_init();

  luascript_signal_init(fcl);
  script_client_signal_create();

  return TRUE;
}

/*****************************************************************************
  Ouput a message on the client lua console.
*****************************************************************************/
static void script_client_output(struct fc_lua *fcl, enum log_level level,
                                 const char *format, ...)
{
  va_list args;
  struct ft_color ftc_luaconsole = ftc_luaconsole_error;

  switch (level) {
  case LOG_FATAL:
    /* Special case - will quit the client. */
    {
      char buf[1024];

      va_start(args, format);
      fc_vsnprintf(buf, sizeof(buf), format, args);
      va_end(args);

      log_fatal("%s", buf);
    }
    break;
  case LOG_ERROR:
    ftc_luaconsole = ftc_luaconsole_error;
    break;
  case LOG_NORMAL:
    ftc_luaconsole = ftc_luaconsole_normal;
    break;
  case LOG_VERBOSE:
    ftc_luaconsole = ftc_luaconsole_verbose;
    break;
  case LOG_DEBUG:
    ftc_luaconsole = ftc_luaconsole_debug;
    break;
  }

  va_start(args, format);
  luaconsole_vprintf(ftc_luaconsole, format, args);
  va_end(args);
}

/*****************************************************************************
  Free the scripting data.
*****************************************************************************/
void script_client_free(void)
{
  if (fcl) {
    script_client_code_free();
    script_client_vars_free();

    luascript_signal_free(fcl);

    luascript_destroy(fcl);
    fcl = NULL;
  }
}

/*****************************************************************************
  Load the scripting state from file.
*****************************************************************************/
void script_client_state_load(struct section_file *file)
{
  script_client_code_load(file);

  /* Variables must be loaded after code is loaded and executed,
   * so we restore their saved state properly */
  script_client_vars_load(file);
}

/*****************************************************************************
  Save the scripting state to file.
*****************************************************************************/
void script_client_state_save(struct section_file *file)
{
  script_client_code_save(file);
  script_client_vars_save(file);
}

/*****************************************************************************
  Invoke all the callback functions attached to a given signal.
*****************************************************************************/
void script_client_signal_emit(const char *signal_name, int nargs, ...)
{
  va_list args;

  va_start(args, nargs);
  luascript_signal_emit_valist(fcl, signal_name, nargs, args);
  va_end(args);
}

/*****************************************************************************
  Declare any new signal types you need here.
*****************************************************************************/
static void script_client_signal_create(void)
{
  luascript_signal_create(fcl, "new_tech", 0);
}
