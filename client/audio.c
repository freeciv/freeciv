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
#include <config.h>
#endif

#include <string.h>
#include <assert.h>

#include "support.h"
#include "fcintl.h"
#include "log.h"
#include "capability.h"
#include "shared.h"
#include "registry.h"
#include "audio_none.h"

#ifdef ESD
#include "audio_esd.h"
#endif

#ifdef SDL
#include "audio_sdl.h"
#endif

#ifdef AUDIO_WINMM
#include "audio_winmm.h"
#endif

#include "audio.h"

#define MAX_NUM_PLUGINS        3

/* keep it open throughout */
static struct section_file tagstruct, *tagfile = &tagstruct;

static struct audio_plugin plugins[MAX_NUM_PLUGINS];
static int num_plugins_used = 0;
static int selected_plugin = -1;

/**************************************************************************
  Add a plugin.
**************************************************************************/
void audio_add_plugin(struct audio_plugin *p)
{
  assert(num_plugins_used < MAX_NUM_PLUGINS);
  memcpy(&plugins[num_plugins_used], p, sizeof(struct audio_plugin));
  num_plugins_used++;
}

/**************************************************************************
  Choose plugin. Returns TRUE on success, FALSE if not
**************************************************************************/
bool audio_select_plugin(const char *const name)
{
  int i;
  bool found = FALSE;

  for (i = 0; i < num_plugins_used; i++) {
    if (strcmp(plugins[i].name, name) == 0) {
      found = TRUE;
      break;
    }
  }

  if (found && i != selected_plugin) {
    freelog(LOG_DEBUG, "Shutting down %s", plugins[selected_plugin].name);
    plugins[selected_plugin].shutdown();
  }

  if (!found) {
    freelog(LOG_ERROR,
	    _("Plugin '%s' isn't available. Available are %s"), name,
	    audio_get_all_plugin_names());
    return FALSE;
  }

  if (!plugins[i].init()) {
    freelog(LOG_ERROR, _("Plugin found but can't be initialized."));
    return FALSE;
  }

  selected_plugin = i;
  freelog(LOG_NORMAL, _("Plugin '%s' is now selected"),
	  plugins[selected_plugin].name);
  return TRUE;
}

/**************************************************************************
  Initialize base audio system. Note that this function is called very
  early at the client startup. So for example logging isn't available.
**************************************************************************/
void audio_init()
{
  audio_none_init();
  assert(num_plugins_used == 1);
  selected_plugin = 0;

#ifdef ESD
  audio_esd_init();
#endif
#ifdef SDL
  audio_sdl_init();
#endif
#ifdef WINMM
  audio_winmm_init();
#endif
}

/**************************************************************************
  Initialize audio system and autoselect a plugin
**************************************************************************/
void audio_real_init(const char *const spec_name,
		     const char *const prefered_plugin_name)
{
  char *filename, *file_capstr;
  char us_capstr[] = "+soundspec";

  if (!spec_name) {
    freelog(LOG_FATAL, _("No audio ruleset given!"));
    exit(EXIT_FAILURE);
  }
  filename = datafilename(spec_name);
  if (!filename) {
    freelog(LOG_ERROR, _("Cannot find audio spec-file \"%s\"."), spec_name);
    freelog(LOG_ERROR, _("To get sound you need to download a sound set!"));
    freelog(LOG_ERROR, _("Get sound sets from <%s>."),
	    "ftp://ftp.freeciv.org/freeciv/contrib/sounds/sets");
    freelog(LOG_ERROR, _("Will continue with disabled sounds."));
    tagfile = NULL;
    return;
  }
  if (!section_file_load(tagfile, filename)) {
    freelog(LOG_FATAL, _("Could not load audio spec-file: %s"), filename);
    exit(EXIT_FAILURE);
  }

  file_capstr = secfile_lookup_str(tagfile, "soundspec.options");
  if (!has_capabilities(us_capstr, file_capstr)) {
    freelog(LOG_FATAL, _("soundspec file appears incompatible:"));
    freelog(LOG_FATAL, _("file: \"%s\""), filename);
    freelog(LOG_FATAL, _("file options: %s"), file_capstr);
    freelog(LOG_FATAL, _("supported options: %s"), us_capstr);
    exit(EXIT_FAILURE);
  }
  if (!has_capabilities(file_capstr, us_capstr)) {
    freelog(LOG_FATAL, _("soundspec file claims required option(s)"
			 " which we don't support:"));
    freelog(LOG_FATAL, _("file: \"%s\""), filename);
    freelog(LOG_FATAL, _("file options: %s"), file_capstr);
    freelog(LOG_FATAL, _("supported options: %s"), us_capstr);
    exit(EXIT_FAILURE);
  }

  if (prefered_plugin_name && audio_select_plugin(prefered_plugin_name)) {
    return;
  }

  if (!audio_select_plugin("esd") && !audio_select_plugin("sdl")
      && !audio_select_plugin("winmm")) {
    freelog(LOG_NORMAL,
	    _("No real audio subsystem managed to initialize!"));
    freelog(LOG_NORMAL,
	    _("For sound support, install either esound or SDL_mixer"));
    freelog(LOG_NORMAL, "Esound: http://www.tux.org/~ricdude/EsounD.html");
    freelog(LOG_NORMAL, "SDL_mixer: http://www.libsdl.org/"
	    "projects/SDL_mixer/index.html");
  }
}

/**************************************************************************
  INTERNAL. Returns TRUE for success.
**************************************************************************/
static bool audio_play_tag(const char *tag, bool repeat)
{
  char *soundfile, *fullpath = NULL;

  if (!tag || strcmp(tag, "-") == 0) {
    return FALSE;
  }

  if (tagfile) {
    soundfile = secfile_lookup_str_default(tagfile, "-", "files.%s", tag);
    if (strcmp(soundfile, "-") == 0) {
      freelog(LOG_VERBOSE, _("No sound file for tag %s (file %s)"), tag,
	      soundfile);
    } else {
      fullpath = datafilename(soundfile);
      if (!fullpath) {
	freelog(LOG_ERROR, _("Cannot find audio file %s"), soundfile);
      }
    }
  }

  return plugins[selected_plugin].play(tag, fullpath, repeat);
}

/**************************************************************************
  Play an audio sample as suggested by sound tags
**************************************************************************/
void audio_play_sound(const char *const tag, char *const alt_tag)
{
  char *pretty_alt_tag = alt_tag ? alt_tag : "(null)";

  assert(tag);

  freelog(LOG_DEBUG, "audio_play_sound('%s', '%s')", tag, pretty_alt_tag);

  /* try playing primary tag first, if not go to alternative tag */
  if (!audio_play_tag(tag, FALSE) && !audio_play_tag(alt_tag, FALSE)) {
    freelog(LOG_VERBOSE, "Neither of tags %s and %s found", tag,
	    pretty_alt_tag);
  }
}

/**************************************************************************
  Loop sound sample as suggested by sound tags
**************************************************************************/
void audio_play_music(const char *const tag, char *const alt_tag)
{
  char *pretty_alt_tag = alt_tag ? alt_tag : "(null)";
  assert(tag);

  freelog(LOG_DEBUG, "audio_play_music('%s', '%s')", tag, pretty_alt_tag);

  /* try playing primary tag first, if not go to alternative tag */
  if (!audio_play_tag(tag, TRUE) && !audio_play_tag(alt_tag, TRUE)) {
    freelog(LOG_VERBOSE, "Neither of tags %s and %s found", tag,
	    pretty_alt_tag);
  }
}

/**************************************************************************
  Stop looping sound. Music should die down in a few seconds.
**************************************************************************/
void audio_stop()
{
  plugins[selected_plugin].stop();
}

/**************************************************************************
  Call this at end of program only.
**************************************************************************/
void audio_shutdown()
{
  /* avoid infinite loop at end of game */
  audio_stop();

  audio_play_sound("e_game_quit", NULL);
  plugins[selected_plugin].wait();
  plugins[selected_plugin].shutdown();
  if (tagfile) {
    section_file_free(tagfile);
  }
}

/**************************************************************************
  Returns a string which list all available plugins. You don't have to
  free the string.
**************************************************************************/
const char *const audio_get_all_plugin_names()
{
  static char buffer[100];
  int i;

  sz_strlcpy(buffer, "[");

  for (i = 0; i < num_plugins_used; i++) {
    sz_strlcat(buffer, plugins[i].name);
    if (i != num_plugins_used - 1) {
      sz_strlcat(buffer, ", ");
    }
  }
  sz_strlcat(buffer, "]");
  return buffer;
}
