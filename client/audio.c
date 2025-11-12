/***********************************************************************
 Freeciv - Copyright (C) 2005 - The Freeciv Team
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* utility */
#include "capability.h"
#include "fcintl.h"
#include "log.h"
#include "mem.h"
#include "rand.h"
#include "registry.h"
#include "shared.h"
#include "string_vector.h"
#include "support.h"

/* client */
#include "audio_none.h"
#ifdef AUDIO_SDL
#include "audio_sdl.h"
#endif /* AUDIO_SDL */
#include "client_main.h"
#include "music.h"
#include "options.h"

#include "audio.h"

#define MAX_NUM_PLUGINS		2
#define SNDSPEC_SUFFIX		".soundspec"
#define MUSICSPEC_SUFFIX        ".musicspec"

#define SOUNDSPEC_CAPSTR "+Freeciv-3.2-soundset"
#define MUSICSPEC_CAPSTR "+Freeciv-3.2-musicspec"

/* Keep them open throughout */
static struct section_file *ss_tagfile = NULL;
static struct section_file *ms_tagfile = NULL;

static struct audio_plugin plugins[MAX_NUM_PLUGINS];
static int num_plugins_used = 0;
static int selected_plugin = -1;
static int current_track = -1;
static enum music_usage current_usage;
static bool switching_usage = FALSE;
static bool let_single_track_play = FALSE;

static struct mfcb_data
{
  struct section_file *sfile;
  const char *tag;
} mfcb;

static int audio_play_tag(struct section_file *sfile,
                          const char *tag, bool repeat,
                          int exclude, enum keep_style keep_old_style);

static void audio_shutdown_atexit(void);

/**********************************************************************//**
  Returns a static string vector of all sound plugins
  available on the system.  This function is unfortunately similar to
  audio_get_all_plugin_names().
**************************************************************************/
const struct strvec *get_soundplugin_list(const struct option *poption)
{
  static struct strvec *plugin_list = NULL;

  if (NULL == plugin_list) {
    int i;

    plugin_list = strvec_new();
    strvec_reserve(plugin_list, num_plugins_used);
    for (i = 0; i < num_plugins_used; i++) {
      strvec_set(plugin_list, i, plugins[i].name);
    }
  }

  return plugin_list;
}

/**********************************************************************//**
  Returns a static string vector of audiosets of given type available
  on the system by searching all data directories for files matching
  suffix.
  The list is NULL-terminated.
**************************************************************************/
static const struct strvec *get_audio_speclist(const char *suffix,
                                               struct strvec **audio_list)
{
  if (NULL == *audio_list) {
    *audio_list = fileinfolist(get_data_dirs(), suffix);
  }

  return *audio_list;
}

/**********************************************************************//**
  Returns a static string vector of soundsets available on the system.
**************************************************************************/
const struct strvec *get_soundset_list(const struct option *poption)
{
  static struct strvec *sound_list = NULL;

  return get_audio_speclist(SNDSPEC_SUFFIX, &sound_list);
}

/**********************************************************************//**
  Returns a static string vector of musicsets available on the system.
**************************************************************************/
const struct strvec *get_musicset_list(const struct option *poption)
{
  static struct strvec *music_list = NULL;

  return get_audio_speclist(MUSICSPEC_SUFFIX, &music_list);
}

/**********************************************************************//**
  Add a plugin.
**************************************************************************/
void audio_add_plugin(struct audio_plugin *p)
{
  fc_assert_ret(num_plugins_used < MAX_NUM_PLUGINS);
  memcpy(&plugins[num_plugins_used], p, sizeof(struct audio_plugin));
  num_plugins_used++;
}

/**********************************************************************//**
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
    log_debug("Shutting down %s", plugins[selected_plugin].name);
    plugins[selected_plugin].stop();
    plugins[selected_plugin].wait();
    plugins[selected_plugin].shutdown(&(plugins[selected_plugin]));
  }

  if (!found) {
    log_fatal(_("Plugin '%s' isn't available. Available are %s"),
              name, audio_get_all_plugin_names());
    exit(EXIT_FAILURE);
  }

  if (!plugins[i].init(&(plugins[i]))) {
    log_error("Plugin %s found, but can't be initialized.", name);
    return FALSE;
  }

  selected_plugin = i;
  log_verbose("Plugin '%s' is now selected", plugins[selected_plugin].name);

  plugins[selected_plugin].set_volume(gui_options.sound_effects_volume / 100.0);

  return TRUE;
}

/**********************************************************************//**
  Initialize base audio system. Note that this function is called very
  early at the client startup. So for example logging isn't available.
**************************************************************************/
void audio_init(void)
{
#ifdef AUDIO_SDL
  audio_sdl_init();
#endif

  /* Initialize dummy plugin last, as lowest priority plugin. This
   * affects which plugin gets selected as default in new installations. */
  audio_none_init();
  selected_plugin = 0;
}

/**********************************************************************//**
  Returns the filename for the given audio set. Returns NULL if
  set couldn't be found. Caller has to free the return value.
**************************************************************************/
static const char *audiospec_fullname(const char *audioset_name,
                                      bool music)
{
  const char *suffix = music ? MUSICSPEC_SUFFIX : SNDSPEC_SUFFIX;
  const char *audioset_default = music ? "stdmusic" : "stdsounds";/* Do not i18n! */
  char *fname = fc_malloc(strlen(audioset_name) + strlen(suffix) + 1);
  const char *dname;

  sprintf(fname, "%s%s", audioset_name, suffix);

  dname = fileinfoname(get_data_dirs(), fname);
  free(fname);

  if (dname) {
    return fc_strdup(dname);
  }

  if (strcmp(audioset_name, audioset_default) == 0) {
    /* avoid endless recursion */
    return NULL;
  }

  /* Marked for translation, as user may see this when
   * their client configuration from older version has
   * a musicset that they have not yet installed for the
   * new version. */
  log_error(_("Couldn't find audioset \"%s\", trying \"%s\"."),
            audioset_name, audioset_default);

  return audiospec_fullname(audioset_default, music);
}

/**********************************************************************//**
  Check capabilities of the audio specfile
**************************************************************************/
static bool check_audiofile_capstr(struct section_file *sfile,
                                   const char *filename,
                                   const char *our_cap,
                                   const char *opt_path)
{
  const char *file_capstr;

  file_capstr = secfile_lookup_str(sfile, "%s", opt_path);
  if (NULL == file_capstr) {
    log_fatal("Audio spec-file \"%s\" doesn't have capability string.",
              filename);
    exit(EXIT_FAILURE);
  }
  if (!has_capabilities(our_cap, file_capstr)) {
    log_fatal("Audio spec-file appears incompatible:");
    log_fatal("  file: \"%s\"", filename);
    log_fatal("  file options: %s", file_capstr);
    log_fatal("  supported options: %s", our_cap);
    exit(EXIT_FAILURE);
  }
  if (!has_capabilities(file_capstr, our_cap)) {
    log_fatal("Audio spec-file claims required option(s) "
              "which we don't support:");
    log_fatal("  file: \"%s\"", filename);
    log_fatal("  file options: %s", file_capstr);
    log_fatal("  supported options: %s", our_cap);
    exit(EXIT_FAILURE);
  }

  return TRUE;
}

/**********************************************************************//**
  Initialize audio system and autoselect a plugin
**************************************************************************/
void audio_real_init(const char *const soundset_name,
                     const char *const musicset_name,
                     const char *const preferred_plugin_name)
{
  const char *ss_filename;
  const char *ms_filename;
  char us_ss_capstr[] = SOUNDSPEC_CAPSTR;
  char us_ms_capstr[] = MUSICSPEC_CAPSTR;

  if (strcmp(preferred_plugin_name, "none") == 0) {
    /* We explicitly choose none plugin, silently skip the code below */
    log_verbose("Proceeding with sound support disabled.");
    ss_tagfile = NULL;
    musicspec_close(ms_tagfile);
    ms_tagfile = NULL;

    return;
  }
  if (num_plugins_used == 1) {
    /* We only have the dummy plugin, skip the code but issue an advertise */
    log_normal(_("No real audio plugin present."));
    log_normal(_("Proceeding with sound support disabled."));
    log_normal(_("For sound support, install SDL2_mixer"));
    log_normal("https://github.com/libsdl-org/SDL_mixer");
    ss_tagfile = NULL;
    musicspec_close(ms_tagfile);
    ms_tagfile = NULL;

    return;
  }
  if (!soundset_name) {
    log_fatal("No sound spec-file given!");
    exit(EXIT_FAILURE);
  }
  if (!musicset_name) {
    log_fatal("No music spec-file given!");
    exit(EXIT_FAILURE);
  }
  log_verbose("Initializing sound using %s and %s...",
              soundset_name, musicset_name);
  ss_filename = audiospec_fullname(soundset_name, FALSE);
  ms_filename = audiospec_fullname(musicset_name, TRUE);
  if (!ss_filename || !ms_filename) {
    log_error("Cannot find audio spec-file \"%s\" or \"%s\"",
              soundset_name, musicset_name);
    log_normal(_("To get sound you need to download a sound set!"));
    log_normal(_("Get sound sets from <%s>."),
               "https://www.freeciv.org/wiki/Sounds");
    log_normal(_("Proceeding with sound support disabled."));
    ss_tagfile = NULL;
    musicspec_close(ms_tagfile);
    ms_tagfile = NULL;

    return;
  }
  if (!(ss_tagfile = secfile_load(ss_filename, TRUE))) {
    log_fatal(_("Could not load sound spec-file '%s':\n%s"), ss_filename,
              secfile_error());
    exit(EXIT_FAILURE);
  }
  if (!(ms_tagfile = musicspec_load(ms_filename))) {
    log_fatal(_("Could not load music spec-file '%s':\n%s"), ms_filename,
              secfile_error());
    exit(EXIT_FAILURE);
  }

  check_audiofile_capstr(ss_tagfile, ss_filename, us_ss_capstr,
                         "soundspec.options");

  free((void *) ss_filename);

  check_audiofile_capstr(ms_tagfile, ms_filename, us_ms_capstr,
                         "musicspec.options");

  free((void *) ms_filename);

  {
    static bool atexit_set = FALSE;

    if (!atexit_set) {
      atexit(audio_shutdown_atexit);
      atexit_set = TRUE;
    }
  }

  if (preferred_plugin_name[0] != '\0') {
    if (!audio_select_plugin(preferred_plugin_name))
      log_normal(_("Proceeding with sound support disabled."));
    return;
  }

#ifdef AUDIO_SDL
  if (audio_select_plugin("sdl")) {
    return;
  }
#endif /* AUDIO_SDL */
  log_normal(_("No real audio subsystem managed to initialize!"));
  log_normal(_("Perhaps there is some misconfiguration or bad permissions."));
  log_normal(_("Proceeding with sound support disabled."));
}

/**********************************************************************//**
  Switch soundset
**************************************************************************/
void audio_restart(const char *soundset_name, const char *musicset_name)
{
  audio_stop(); /* Fade down old one */

  sz_strlcpy(sound_set_name, soundset_name);
  sz_strlcpy(music_set_name, musicset_name);
  audio_real_init(sound_set_name, music_set_name, sound_plugin_name);
}

/**********************************************************************//**
  Callback to start new track
**************************************************************************/
static void music_finished_callback(void)
{
  bool usage_enabled = TRUE;

  if (switching_usage) {
    switching_usage = FALSE;

    return;
  }

  if (let_single_track_play) {
    /* This call is style music ending before single track plays.
     * Do not restart style music now.
     * Make sure style music restarts when single track itself finishes. */
    let_single_track_play = FALSE;

    return;
  }

  switch (current_usage) {
  case MU_MENU:
    usage_enabled = gui_options.sound_enable_menu_music;
    break;
  case MU_INGAME:
    usage_enabled = gui_options.sound_enable_game_music;
    break;
  }

  if (usage_enabled) {
    current_track = audio_play_tag(mfcb.sfile, mfcb.tag, TRUE, current_track,
                                   KS_CHANGE);
  }
}

/**********************************************************************//**
  INTERNAL. Returns id (>= 0) of the tag selected for playing, 0 when
  there's no alternative tags, or negative value in case of error.
**************************************************************************/
static int audio_play_tag(struct section_file *sfile,
                          const char *tag, bool repeat, int exclude,
                          enum keep_style keep_old_style)
{
  const char *soundfile;
  const char *fullpath = NULL;
  audio_finished_callback cb = NULL;
  int ret = 0;

  if (!tag || strcmp(tag, "-") == 0) {
    return -1;
  }

  if (sfile) {
    soundfile = secfile_lookup_str(sfile, "files.%s", tag);
    if (soundfile == NULL) {
      const char *files[MAX_ALT_AUDIO_FILES];
      int excluded = -1;
      int i;
      int j;

      j = 0;
      for (i = 0; i < MAX_ALT_AUDIO_FILES; i++) {
        const char *ftmp = secfile_lookup_str(sfile, "files.%s_%d", tag, i);

        if (ftmp == NULL) {
          if (excluded != -1 && j == 0) {
            /* Cannot exclude the only track */
            excluded = -1;
            j++;
          }
          files[j] = NULL;
          break;
        }
        files[j] = ftmp;
        if (i != exclude) {
          j++;
        } else {
          excluded = j;
        }
      }

      if (j > 0) {
        ret = fc_rand(j);

        soundfile = files[ret];
        if (excluded != -1 && excluded < ret) {
          /* Exclude track was skipped earlier, include it to track number to return */
          ret++;
        }
      }
    }

    if (repeat) {
      if (keep_old_style != KS_KEEP) {
        mfcb.sfile = sfile;
        mfcb.tag = tag;
      }

      /* Callback is needed even when there's no alternative tracks -
       * we may be running single track now, and want to switch
       * (by the callback) back to style music when it ends. */
      cb = music_finished_callback;
    }

    if (NULL == soundfile) {
      log_verbose("No sound file for tag %s", tag);
    } else {
      fullpath = fileinfoname(get_data_dirs(), soundfile);
      if (!fullpath) {
        log_error("Cannot find audio file %s for tag %s", soundfile, tag);
      }
    }
  }

  if (!plugins[selected_plugin].play(tag, fullpath, repeat,
                                     keep_old_style != KS_SND_EFFECT, cb)) {
    return -1;
  }

  return ret;
}

/**********************************************************************//**
  Play tag from sound set
**************************************************************************/
static bool audio_play_sound_tag(const char *tag, bool repeat)
{
  return (audio_play_tag(ss_tagfile, tag, repeat, -1, KS_SND_EFFECT) >= 0);
}

/**********************************************************************//**
  Play tag from music set
**************************************************************************/
static int audio_play_music_tag(const char *tag, bool repeat,
                                enum keep_style keep_old_style)
{
  return audio_play_tag(ms_tagfile, tag, repeat, -1, keep_old_style);
}

/**********************************************************************//**
  Play an audio sample as suggested by sound tags
**************************************************************************/
void audio_play_sound(const char *const tag, const char *const alt_tag,
                      const char *const alt_tag2)
{
  const char *pretty_alt_tag = alt_tag ? alt_tag : "(null)";
  const char *pretty_alt2_tag = alt_tag2 ? alt_tag2 : "(null)";

  if (gui_options.sound_enable_effects) {
    fc_assert_ret(tag != NULL);

    log_debug("audio_play_sound('%s', '%s', '%s')",
              tag, pretty_alt_tag, pretty_alt2_tag);

    /* Try playing primary tag first, if not go to alternative tags */
    if (!audio_play_sound_tag(tag, FALSE)
        && !audio_play_sound_tag(alt_tag, FALSE)
        && !audio_play_sound_tag(alt_tag2, FALSE)) {
      log_verbose( "None of tags %s, %s, or %s found",
                   tag, pretty_alt_tag, pretty_alt2_tag);
    }
  }
}

/**********************************************************************//**
  Play music, either in loop or just one track in the middle of the style
  music.
**************************************************************************/
static void real_audio_play_music(const char *const tag, char *const alt_tag,
                                  enum keep_style keep_old_style)
{
  char *pretty_alt_tag = alt_tag ? alt_tag : "(null)";

  fc_assert_ret(tag != NULL);

  log_debug("audio_play_music('%s', '%s')", tag, pretty_alt_tag);

  /* try playing primary tag first, if not go to alternative tag */
  current_track = audio_play_music_tag(tag, TRUE, keep_old_style);

  if (current_track < 0) {
    current_track = audio_play_music_tag(alt_tag, TRUE, keep_old_style);

    if (current_track < 0) {
      log_verbose("Neither of tags %s or %s found", tag, pretty_alt_tag);
    }
  }
}

/**********************************************************************//**
  Loop music as suggested by sound tags
**************************************************************************/
void audio_play_music(const char *const tag, char *const alt_tag,
                      enum music_usage usage)
{
  current_usage = usage;

  real_audio_play_music(tag, alt_tag, KS_CHANGE);
}

/**********************************************************************//**
  Play single track as suggested by sound tags
**************************************************************************/
void audio_play_track(const char *const tag, char *const alt_tag)
{
  if (current_track >= 0) {
    /* Only set let_single_track_play when there's music playing that will
     * result in calling the music_finished_callback */
    let_single_track_play = TRUE;

    /* Stop old music. */
    audio_stop();
  }

  real_audio_play_music(tag, alt_tag, KS_KEEP);
}

/**********************************************************************//**
  Pause sound.
**************************************************************************/
void audio_pause(void)
{
  plugins[selected_plugin].pause();
}

/**********************************************************************//**
  Resume sound.
**************************************************************************/
void audio_resume(void)
{
  plugins[selected_plugin].resume();
}

/**********************************************************************//**
  Stop sound. Music should die down in a few seconds.
**************************************************************************/
void audio_stop(void)
{
  plugins[selected_plugin].stop();
}

/**********************************************************************//**
  Stop looping sound. Music should die down in a few seconds.
**************************************************************************/
void audio_stop_usage(void)
{
  switching_usage = TRUE;
  plugins[selected_plugin].stop();
}

/**********************************************************************//**
  Get sound volume currently in use.
**************************************************************************/
double audio_get_volume(void)
{
  return plugins[selected_plugin].get_volume();
}

/**********************************************************************//**
  Set sound volume to use.
**************************************************************************/
void audio_set_volume(double volume)
{
  plugins[selected_plugin].set_volume(volume);
}

/**********************************************************************//**
  Call this at end of program only.

  @param play_quit_tag Play exit sound
**************************************************************************/
void audio_shutdown(bool play_quit_tag)
{
  /* Avoid infinite loop at end of game */
  audio_stop();

  if (play_quit_tag) {
    audio_play_sound("e_client_quit", NULL, NULL);
  }

  if (plugins[selected_plugin].initialized) {
    plugins[selected_plugin].wait();
    plugins[selected_plugin].shutdown(&(plugins[selected_plugin]));
  }

  if (NULL != ss_tagfile) {
    secfile_destroy(ss_tagfile);
    ss_tagfile = NULL;
  }
  if (NULL != ms_tagfile) {
    musicspec_close(ms_tagfile);
    ms_tagfile = NULL;
  }
}

/**********************************************************************//**
  Called as atexit handler.
**************************************************************************/
static void audio_shutdown_atexit(void)
{
  /* If support has already been shut down, can't handle audio tags. */
  audio_shutdown(are_support_services_available());
}

/**********************************************************************//**
  Returns a string which list all available plugins. You don't have to
  free the string.
**************************************************************************/
const char *audio_get_all_plugin_names(void)
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
