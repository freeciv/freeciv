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


struct musicset {
  char name[MAX_LEN_NAME];
  char version[MAX_LEN_NAME];
  char *summary;
  char *description;
} current_ms = { .summary = NULL, .description = NULL };

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

    mstr = secfile_lookup_str(tagfile, "musicspec.name");

    if (mstr == NULL) {
      log_error(_("Musicset from %s has no name defined!"), ms_filename);
      secfile_destroy(tagfile);

      return NULL;
    }

    /* Musicset name found */
    sz_strlcpy(current_ms.name, mstr);

    mstr = secfile_lookup_str_default(tagfile, "", "musicspec.version");
    if (mstr[0] != '\0') {
    /* Musicset version found */
      sz_strlcpy(current_ms.version, mstr);
    } else {
      /* No version information */
      current_ms.version[0] = '\0';
    }

    mstr = secfile_lookup_str_default(tagfile, "", "musicspec.summary");
    if (mstr[0] != '\0') {
      size_t len;

      /* Musicset summary found */
      len = strlen(mstr);
      current_ms.summary = fc_malloc(len + 1);
      fc_strlcpy(current_ms.summary, mstr, len + 1);
    } else {
      /* No summary */
      if (current_ms.summary != NULL) {
        free(current_ms.summary);
        current_ms.summary = NULL;
      }
    }

    mstr = secfile_lookup_str_default(tagfile, "", "musicspec.description");
    if (mstr[0] != '\0') {
      size_t len;

      /* Musicset description found */
      len = strlen(mstr);
      current_ms.description = fc_malloc(len + 1);
      fc_strlcpy(current_ms.description, mstr, len + 1);
    } else {
      /* No Description */
      if (current_ms.description != NULL) {
        free(current_ms.description);
        current_ms.description = NULL;
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
    free(current_ms.summary);
    current_ms.summary = NULL;
    free(current_ms.description);
    current_ms.description = NULL;

    secfile_destroy(tagfile);
  }
}

/**********************************************************************//**
  Return name of the current musicset.
**************************************************************************/
const char *current_musicset_name(void)
{
  return current_ms.name;
}

/**********************************************************************//**
  Return version of the current musicset. Can be NULL.
**************************************************************************/
const char *current_musicset_version(void)
{
  if (current_ms.version[0] == '\0') {
    return NULL;
  }

  return current_ms.version;
}

/**********************************************************************//**
  Return summary of the current musicset. Can be NULL.
**************************************************************************/
const char *current_musicset_summary(void)
{
  return current_ms.summary;
}

/**********************************************************************//**
  Return description of the current musicset. Can be NULL.
**************************************************************************/
const char *current_musicset_description(void)
{
  return current_ms.description;
}
