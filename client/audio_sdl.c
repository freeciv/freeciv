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

#include <SDL.h>
#include <SDL_mixer.h>

#include "audio.h"
#include "log.h"
#include "support.h"

#include "audio_sdl.h"

struct sample {
  Mix_Chunk *wave;
  const char *tag;
};

static Mix_Music *mus = NULL;
static struct sample samples[MIX_CHANNELS];

/**************************************************************************
  Play sound
**************************************************************************/
static bool play(const char *const tag, const char *const fullpath,
		bool repeat)
{
  int i, j;
  Mix_Chunk *wave = NULL;

  if (!fullpath) {
    return FALSE;
  }

  if (repeat) {
    /* unload previous */
    Mix_HaltMusic();
    Mix_FreeMusic(mus);

    /* load music file */
    mus = Mix_LoadMUS(fullpath);
    if (mus == NULL) {
      freelog(LOG_FATAL, "Can't open file '%s'", fullpath);
    }

    Mix_PlayMusic(mus, -1);	/* -1 means loop forever */
    freelog(LOG_VERBOSE, "Playing file %s on music channel", fullpath);
    /* in case we did a stop() recently; add volume controls later */
    Mix_VolumeMusic(MIX_MAX_VOLUME);

  } else {

    /* see if we can cache on this one */
    for (j = 0; j < MIX_CHANNELS; j++) {
      if (samples[j].tag && (strcmp(samples[j].tag, tag) == 0)) {
	freelog(LOG_DEBUG, "Playing file %s from cache (slot %d)", fullpath,
		j);
	i = Mix_PlayChannel(-1, samples[j].wave, 0);
	return TRUE;
      }
    }				/* guess not */

    /* load wave */
    wave = Mix_LoadWAV(fullpath);
    if (wave == NULL) {
      freelog(LOG_ERROR, "Can't open file '%s'", fullpath);
    }

    /* play sound sample on first available channel, returns -1 if no
       channel found */
    i = Mix_PlayChannel(-1, wave, 0);
    if (i < 0) {
      freelog(LOG_VERBOSE, "No available sound channel to play %s.", tag);
      Mix_FreeChunk(wave);
      return FALSE;
    }
    freelog(LOG_VERBOSE, "Playing file %s on channel %d", fullpath, i);
    /* free previous sample on this channel. it will by definition no
       longer be playing by the time we get here */
    if (samples[i].wave) {
      Mix_FreeChunk(samples[i].wave);
      samples[i].wave = NULL;
    }
    /* remember for cacheing */
    samples[i].wave = wave;
    samples[i].tag = tag;

  }
  return TRUE;
}

/**************************************************************************
  Stop music
**************************************************************************/
static void stop()
{
  /* fade out over 2 sec */
  Mix_FadeOutMusic(2000);
}

/**************************************************************************
  Wait for audio to die on all channels.
  WARNING: If a channel is looping, it will NEVER exit! Always call
  music_stop() first!
**************************************************************************/
static void wait()
{
  while (Mix_Playing(-1) != 0) {
    SDL_Delay(100);
  }
}

/**************************************************************************
  Clean up.
**************************************************************************/
static void shutdown()
{
  int i;

  stop();
  wait();

  /* remove all buffers */
  for (i = 0; i < MIX_CHANNELS; i++) {
    if (samples[i].wave) {
      Mix_FreeChunk(samples[i].wave);
    }
  }
  Mix_FreeMusic(mus);

  Mix_CloseAudio();
  SDL_Quit();
}

/**************************************************************************
  Initialize.
**************************************************************************/
static bool init(void)
{
  /* Initialize variables */
  const int audio_rate = MIX_DEFAULT_FREQUENCY;
  const int audio_format = MIX_DEFAULT_FORMAT;
  const int audio_channels = 2;
  int i;

  if (SDL_Init(SDL_INIT_AUDIO) < 0) {
    return FALSE;
  }

  if (Mix_OpenAudio(audio_rate, audio_format, audio_channels, 4096) < 0) {
    freelog(LOG_ERROR, "Error calling Mix_OpenAudio");
    /* try something else */
    SDL_Quit();
    return FALSE;
  }

  Mix_AllocateChannels(MIX_CHANNELS);
  for (i = 0; i < MIX_CHANNELS; i++) {
    samples[i].wave = NULL;
  }
  /* sanity check, for now; add volume controls later */
  Mix_Volume(-1, MIX_MAX_VOLUME);
  return TRUE;
}

/**************************************************************************
  Initialize. Note that this function is called very early at the
  client startup. So for example logging isn't available.
**************************************************************************/
void audio_sdl_init(void)
{
  struct audio_plugin self;

  sz_strlcpy(self.name, "sdl");
  sz_strlcpy(self.descr, "Simple DirectMedia Library (SDL) mixer plugin");
  self.init = init;
  self.shutdown = shutdown;
  self.stop = stop;
  self.wait = wait;
  self.play = play;
  audio_add_plugin(&self);
}
