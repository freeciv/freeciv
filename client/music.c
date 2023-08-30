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
#include "connection.h"
#include "log.h"

/* client */
#include "audio.h"
#include "client_main.h"
#include "options.h"

#include "music.h"


static char *ms_summary = NULL;
static char *ms_description = NULL;

/**********************************************************************//**
  Start music suitable for current game situation
**************************************************************************/
void start_style_music(void)
{
  if (client_state() != C_S_RUNNING) {
    /* Style music plays in running game only */
    return;
  }

  if (client.conn.playing == NULL) {
    /* Detached connections and global observers currently not
     * supported */
    return;
  }

  if (gui_options.sound_enable_game_music) {
    struct music_style *pms;

    stop_style_music();

    pms = music_style_by_number(client.conn.playing->music_style);

    if (pms != NULL) {
      const char *tag = NULL;

      switch (client.conn.playing->client.mood) {
      case MOOD_COUNT:
        fc_assert(client.conn.playing->client.mood != MOOD_COUNT);
        fc__fallthrough; /* No break but use default tag */
      case MOOD_PEACEFUL:
        tag = pms->music_peaceful;
        break;
      case MOOD_COMBAT:
        tag = pms->music_combat;
        break;
      }

      if (tag != NULL && tag[0] != '\0') {
        log_debug("Play %s", tag);
        audio_play_music(tag, NULL, MU_INGAME);
      }
    }
  }
}

/**********************************************************************//**
  Stop style music completely.
**************************************************************************/
void stop_style_music(void)
{
  audio_stop_usage();
}

/**********************************************************************//**
  Start menu music.
**************************************************************************/
void start_menu_music(const char *const tag, char *const alt_tag)
{
  if (gui_options.sound_enable_menu_music) {
    audio_play_music(tag, alt_tag, MU_MENU);
  }
}

/**********************************************************************//**
  Stop menu music completely.
**************************************************************************/
void stop_menu_music(void)
{
  audio_stop_usage();
}

/**********************************************************************//**
  Play single track before continuing normal style music
**************************************************************************/
void play_single_track(const char *const tag)
{
  if (client_state() != C_S_RUNNING) {
    /* Only in running game */
    return;
  }

  audio_play_track(tag, NULL);
}

/**********************************************************************//**
  Musicset changed in options.
**************************************************************************/
void musicspec_reread_callback(struct option *poption)
{
  const char *musicset_name = option_str_get(poption);

  audio_restart(sound_set_name, musicset_name);

  /* Start suitable music from the new set */
  if (client_state() != C_S_RUNNING) {
    start_menu_music("music_menu", NULL);
  } else {
    start_style_music();
  }
}

/**********************************************************************//**
  Load specified musicspec.

  This is called from the audio code, and not the vice versa.
**************************************************************************/
struct section_file *musicspec_load(const char *ms_filename)
{
  struct section_file *tagfile = secfile_load(ms_filename, TRUE);

  if (tagfile != NULL) {
    const char *mstr;

    mstr = secfile_lookup_str_default(tagfile, "", "musicspec.summary");
    if (mstr[0] != '\0') {
      size_t len;

      /* Musicset summary found */
      len = strlen(mstr);
      ms_summary = fc_malloc(len + 1);
      fc_strlcpy(ms_summary, mstr, len + 1);
    } else {
      /* No summary */
      if (ms_summary != NULL) {
        free(ms_summary);
        ms_summary = NULL;
      }
    }

    mstr = secfile_lookup_str_default(tagfile, "", "musicspec.description");
    if (mstr[0] != '\0') {
      size_t len;

      /* Musicset description found */
      len = strlen(mstr);
      ms_description = fc_malloc(len + 1);
      fc_strlcpy(ms_description, mstr, len + 1);
    } else {
      /* No summary */
      if (ms_description != NULL) {
        free(ms_description);
        ms_description = NULL;
      }
    }
  }

  return tagfile;
}

/**********************************************************************//**
  Close the musicspec.
  Tagfile should refer to currently active musicspec as some data
  is not retrievable from tagfile alone, but currently active music only.

  This is called from the audio code, and not the vice versa.
**************************************************************************/
void musicspec_close(struct section_file *tagfile)
{
  if (tagfile != NULL) {
    free(ms_summary);
    ms_summary = NULL;
    free(ms_description);
    ms_description = NULL;

    secfile_destroy(tagfile);
  }
}
