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
#include <unistd.h>		/* close */

#include <esd.h>

#include "fcintl.h"
#include "log.h"
#include "support.h"

#include "audio.h"

#include "audio_esd.h"

#define MAX_SAMPLES 8

struct sample {
  int id;
  const char *tag;
};

static struct sample samples[MAX_SAMPLES];
static int music_id = -1;
static int last_sample = 0;
static int sock = -1;

/* 
 * Note that esd_sample_kill is unimplemented in the current version
 * of esdlib. 
 *
 * This plugin employs an array of samples which are stored in the esd
 * daemon. Already stored samples are cached. A variable points to
 * last used slot in the array, and is incremented after each new
 * added sample. If this slot already has a sample, it is killed and
 * freed to make way for the new one. 
 */

/**************************************************************************
  Clean up
**************************************************************************/
static void my_shutdown(void)
{
  int i;

  for (i = 0; i < MAX_SAMPLES; i++) {
    if (samples[i].id != -1) {
      freelog(LOG_DEBUG, "Freeing sample <%d> in slot %d", samples[i].id,
	      i);
      esd_sample_free(sock, samples[i].id);
    }
  }
  if (music_id != -1) {
    esd_sample_stop(sock, music_id);
    esd_sample_free(sock, music_id);
    music_id = -1;
  }
  esd_audio_close();
  close(sock);
  sock = -1;
}

/**************************************************************************
  Stop music
**************************************************************************/
static void my_stop(void)
{
  if (music_id != -1) {
    esd_sample_stop(sock, music_id);
  }
}

/**************************************************************************
  Wait
**************************************************************************/
static void my_wait(void)
{
  /* not implemented */
}

/**************************************************************************
  Play sound sample
**************************************************************************/
static bool my_play(const char *const tag, const char *const fullpath,
		    bool repeat)
{
  static char program_name[] = "civclient";
  static int i;

  if (!fullpath) {
    return FALSE;
  }

  if (repeat) {
    /* Play music (in separate "channel" so that it is not
     * interrupted by the above channel cycle other sounds pass
     * through)
     */
    if (music_id != -1) {
      esd_sample_stop(sock, music_id);
      esd_sample_free(sock, music_id);
    }

    music_id = esd_file_cache(sock, program_name, fullpath);
    if (music_id < 0) {
      freelog(LOG_ERROR, _("Error while caching sample <%d>: "
			   "confirm value != music_id\n"), music_id);
    }

    esd_sample_loop(sock, music_id);
  } else {
    /* see if we can cache on this one */
    for (i = 0; i < MAX_SAMPLES; i++) {
      if (samples[i].tag && (strcmp(samples[i].tag, tag) == 0)) {
	freelog(LOG_DEBUG, "Playing file %s from cache (slot %d)", fullpath,
		i);
	esd_sample_play(sock, samples[i].id);
	return TRUE;
      }
    }

    /* not in cache, so let's create an open sample slot */
    if (samples[last_sample].id != -1) {
      freelog(LOG_DEBUG, "Opening sample slot %d", last_sample);
      esd_sample_free(sock, samples[last_sample].id);
    }

    freelog(LOG_DEBUG, "Playing file %s in slot %d", fullpath,
	    last_sample);
    samples[last_sample].id = esd_file_cache(sock, program_name, fullpath);
    samples[last_sample].tag = tag;
    if (samples[last_sample].id < 0) {
      freelog(LOG_ERROR, _("Error while caching sample <%d>: "
			   "confirm value != samples[].id\n"),
	      samples[last_sample].id);
    }

    esd_sample_play(sock, samples[last_sample].id);

    last_sample = (last_sample + 1) % MAX_SAMPLES;
  }
  return TRUE;
}

/**************************************************************************
  Initialize.
**************************************************************************/
static bool my_init(void)
{
  sock = esd_open_sound(NULL);
  if (sock >= 0) {
    int i;

    for (i = 0; i < MAX_SAMPLES; i++) {
      samples[i].id = -1;
      samples[i].tag = NULL;
    }
    return TRUE;
  } else {
    /* esd reports all errors as -1, so we can't figure out
       what might have gone wrong */
    return FALSE;
  }
}

/**************************************************************************
  Initialize. Note that this function is called very early at the
  client startup. So for example logging isn't available.
**************************************************************************/
void audio_esd_init(void)
{
  struct audio_plugin self;

  sz_strlcpy(self.name, "esd");
  sz_strlcpy(self.descr, "ESound server plugin");
  self.init = my_init;
  self.shutdown = my_shutdown;
  self.stop = my_stop;
  self.wait = my_wait;
  self.play = my_play;
  audio_add_plugin(&self);
}
