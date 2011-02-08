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

#ifdef AI_MODULES
#include <ltdl.h>
#endif

/* utility */
#include "support.h"

/* common */
#include "ai.h"
#include "player.h"

/* server/advisors */
#include "autosettlers.h"

/* ai/default */
#include "defaultai.h"

#include "aiiface.h"

/**************************************************************************
  Return string describing module loading error. Never returns NULL.
**************************************************************************/
#ifdef AI_MODULES
static const char *fc_module_error(void)
{
  static char def_err[] = "Unknown error";
  const char *errtxt = lt_dlerror();

  if (errtxt == NULL) {
    return def_err;
  }

  return errtxt;
}
#endif /* AI_MODULES */

/**************************************************************************
  Load ai module from file.
**************************************************************************/
bool load_ai_module(const char *filename)
{
  struct ai_type *ai = ai_type_alloc();

#ifdef AI_MODULES
  lt_dlhandle handle;
  void (*setup_func)(struct ai_type *ai);
  const char *(*capstr_func)(void);
  const char *capstr;
  char buffer[2048];
#endif /* AI_MODULES */

  if (ai == NULL) {
    return FALSE;
  }

  init_ai(ai);

#ifdef AI_MODULES
  fc_snprintf(buffer, sizeof(buffer), "%s", filename);
  handle = lt_dlopenext(buffer);
  if (handle == NULL) {
    log_error(_("Cannot open AI module %s (%s)"), filename, fc_module_error());
    return FALSE;
  }

  fc_snprintf(buffer, sizeof(buffer), "%s_capstr", filename);
  capstr_func = lt_dlsym(handle, buffer);
  if (capstr_func == NULL) {
    log_error(_("Cannot find capstr function from ai module %s (%s)"),
              filename, fc_module_error());
    return FALSE;
  }

  capstr = capstr_func();
  if (strcmp(FC_AI_MOD_CAPSTR, capstr)) {
    log_error(_("Incompatible ai module %s:"), filename);
    log_error(_("  Module options:    %s"), capstr);
    log_error(_("  Supported options: %s"), FC_AI_MOD_CAPSTR);

    return FALSE;
  }

  fc_snprintf(buffer, sizeof(buffer), "%s_setup", filename);
  setup_func = lt_dlsym(handle, buffer);
  if (setup_func == NULL) {
    log_error(_("Cannot find setup function from ai module %s (%s)"),
              filename, fc_module_error());
    return FALSE;
  }
  setup_func(ai);
#else  /* AI_MODULES */

  fc_ai_default_setup(ai);

#endif /* AI_MODULES */

  return TRUE;
}

/**************************************************************************
  Initialize ai stuff
**************************************************************************/
void ai_init(void)
{
  bool failure = FALSE;

#ifdef AI_MODULES
  if (lt_dlinit()) {
    failure = TRUE;
  }
  if (!failure) {

#ifdef DEBUG
    /* First search ai modules under directory ai/<module> under
       current directory. This allows us to run freeciv without
       installing it. */
    const char *moduledirs[] = { "default", "stub", NULL };
    int i;

    for (i = 0; moduledirs[i] != NULL ; i++) {
      char buf[2048];

      fc_snprintf(buf, sizeof(buf), "ai/%s", moduledirs[i]);
      lt_dladdsearchdir(buf);
    }
#endif /* DEBUG */

    /* Then search ai modules from their installation directory. */
    lt_dladdsearchdir(AI_MODULEDIR);
  }
#endif /* AI_MODULES */

  if (!failure && !load_ai_module("fc_ai_default")) {
    failure = TRUE;
  }

  if (failure) {
    log_fatal(_("Failed to load default ai module, cannot continue."));
    exit(EXIT_FAILURE);
  }
}

/**************************************************************************
  Call incident function of victim.
**************************************************************************/
void call_incident(enum incident_type type, struct player *violator,
                   struct player *victim)
{
  CALL_PLR_AI_FUNC(incident, victim, type, violator, victim);
}
