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
#include <config.h>
#endif

#ifdef AI_MODULES
#include <ltdl.h>
#endif

/* common */
#include "ai.h"
#include "player.h"

/* server/advisors */
#include "autosettlers.h"

/* ai */
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
bool load_ai_module(struct ai_type *ai, const char *filename)
{
#ifdef AI_MODULES
  lt_dlhandle handle;
  void (*setup_func)(struct ai_type *ai);
  char buffer[2048];
#endif /* AI_MODULES */

  init_ai(ai);

#ifdef AI_MODULES
  fc_snprintf(buffer, sizeof(buffer), "%s", filename);
  handle = lt_dlopenext(buffer);
  if (handle == NULL) {
    log_error(_("Cannot open AI module %s (%s)"), filename, fc_module_error());
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
  struct ai_type *ai = ai_type_alloc();
  bool failure = FALSE;

#ifdef AI_MODULES
  if (lt_dlinit()) {
    failure = TRUE;
  }
  if (!failure) {
    /* First search ai modules under directory ai under current directory.
       This allows us to run freeciv without installing it. */
    lt_dladdsearchdir("ai"); 

    /* Then search ai modules from their installation directory. */
    lt_dladdsearchdir(AI_MODULEDIR);
  }
#endif /* AI_MODULES */

  if (!failure && !load_ai_module(ai, "fc_ai_default")) {
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
