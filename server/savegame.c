/********************************************************************** 
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
#include "log.h"
#include "registry.h"

/* common */
#include "capability.h"

/* server */
#include "legacysave.h"
#include "savegame2.h"
#include "savegame3.h"

#include "savegame.h"

/****************************************************************************
  Main entry point for loading a game.
****************************************************************************/
void savegame_load(struct section_file *sfile)
{
  const char *savefile_options;

  fc_assert_ret(sfile != NULL);

#ifdef DEBUG_TIMERS
  struct timer *loadtimer = timer_new(TIMER_CPU, TIMER_DEBUG);
  timer_start(loadtimer);
#endif

  savefile_options = secfile_lookup_str(sfile, "savefile.options");

  if (!savefile_options) {
    log_error("Missing savefile options. Can not load the savegame.");
    return;
  }

  if (has_capabilities("+version3", savefile_options)) {
    /* load new format (freeciv 3.0.x and newer) */
    log_verbose("loading savefile in 3.0+ format ...");
    savegame3_load(sfile);
  } else if (has_capabilities("+version2", savefile_options)) {
    /* load old format (freeciv 2.3 - 2.6) */
    log_verbose("loading savefile in 2.3 - 2.6 format ...");
    savegame2_load(sfile);
  } else {
    log_verbose("loading savefile in legacy format ...");
    secfile_allow_digital_boolean(sfile, TRUE);
    legacy_game_load(sfile);
  }

#ifdef DEBUG_TIMERS
  timer_stop(loadtimer);
  log_debug("Loading secfile in %.3f seconds.", timer_read_seconds(loadtimer));
  timer_destroy(loadtimer);
#endif /* DEBUG_TIMERS */
}

/****************************************************************************
  Main entry point for saving a game.
****************************************************************************/
void savegame_save(struct section_file *sfile, const char *save_reason,
                   bool scenario)
{
  savegame3_save(sfile, save_reason, scenario);
}
