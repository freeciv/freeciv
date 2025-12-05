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

#include <string.h>

#ifdef AUDIO_SDL3
#include <SDL3/SDL.h>
#include <SDL3_mixer/SDL_mixer.h>
#else
#ifdef SDL2_PLAIN_INCLUDE
#include <SDL.h>
#include <SDL_mixer.h>
#else  /* SDL2_PLAIN_INCLUDE */
#include <SDL2/SDL.h>
#include <SDL2/SDL_mixer.h>
#endif /* SDL2_PLAIN_INCLUDE */
#endif /* AUDIO_SDL3 */

/* utility */
#include "log.h"
#include "support.h"

/* client */
#include "audio.h"

#include "audio_sdl.h"

struct sample {
#ifdef AUDIO_SDL3
  MIX_Audio *wave;
#else  /* AUDIO_SDL3 */
  Mix_Chunk *wave;
#endif /* AUDIO_SDL3 */
  const char *tag;
};

/* Sounds don't sound good on Windows unless the buffer size is 4k,
 * but this seems to cause strange behavior on other systems,
 * such as a delay before playing the sound. */
#ifdef FREECIV_MSWINDOWS
const size_t buf_size = 4096;
#else
const size_t buf_size = 1024;
#endif

#ifdef AUDIO_SDL3
#define MIX_CHANNELS 8
static MIX_Audio *mus = NULL;
static MIX_Mixer *mixer = NULL;
static MIX_Track *tracks[MIX_CHANNELS];
static SDL_PropertiesID forever_loop;
#else  /* AUDIO_SDL3 */
static Mix_Music *mus = NULL;
#endif /* AUDIO_SDL3 */

static struct sample samples[MIX_CHANNELS];
static double sdl_audio_volume;

/**********************************************************************//**
  Set the volume.
**************************************************************************/
static void sdl_audio_set_volume(double volume)
{
#ifdef AUDIO_SDL3
  int i;

  for (i = 0; i < MIX_CHANNELS; i++) {
    MIX_SetTrackGain(tracks[i], volume);
  }
#else  /* AUDIO_SDL3 */
  Mix_VolumeMusic(volume * MIX_MAX_VOLUME);
  Mix_Volume(-1, volume * MIX_MAX_VOLUME);
#endif /* AUDIO_SDL3 */

  sdl_audio_volume = volume;
}

/**********************************************************************//**
  Get the volume.
**************************************************************************/
static double sdl_audio_get_volume(void)
{
  return sdl_audio_volume;
}

#ifdef AUDIO_SDL3
/**********************************************************************//**
  Wrap freeciv's callback within SDL3 compatible callback
**************************************************************************/
static void audio_callback_wrapper(void *cb, MIX_Track *track)
{
  ((audio_finished_callback)cb)();
}
#endif /* AUDIO_SDL3 */

/**********************************************************************//**
  Play sound
**************************************************************************/
static bool sdl_audio_play(const char *const tag, const char *const fullpath,
                           bool repeat, bool music, audio_finished_callback cb)
{
  int j;
#ifdef AUDIO_SDL3
  MIX_Audio *wave = NULL;
  int channel;
  static int channel_turn = 1; /* Channel 0 is music */
#else  /* AUDIO_SDL3 */
  int i;
  Mix_Chunk *wave = NULL;
#endif /* AUDIO_SDL3 */

  if (!fullpath) {
    return FALSE;
  }

#ifdef AUDIO_SDL3
  if (music) {
    channel = 0;
  } else {
    channel = channel_turn++;
    if (channel_turn >= MIX_CHANNELS) {
      channel_turn = 1;
    }
  }
#endif /* AUDIO_SDL3 */

  if (repeat) {
    /* Unload previous */
#ifdef AUDIO_SDL3
    MIX_StopTrack(tracks[channel], 0);
    MIX_DestroyAudio(mus);
#else  /* AUDIO_SDL3 */
    Mix_HaltMusic();
    Mix_FreeMusic(mus);
#endif /* AUDIO_SDL3 */

    /* Load music file */
#ifdef AUDIO_SDL3
    mus = MIX_LoadAudio(mixer, fullpath, FALSE);
#else  /* AUDIO_SDL3 */
    mus = Mix_LoadMUS(fullpath);
#endif /* AUDIO_SDL3 */
    if (mus == NULL) {
      log_error("Can't open file \"%s\": %s",
                fullpath, SDL_GetError());
    }

    if (cb == NULL) {
#ifdef AUDIO_SDL3
      MIX_SetTrackAudio(tracks[channel], mus);
      MIX_PlayTrack(tracks[channel], forever_loop);
#else  /* AUDIO_SDL3 */
      Mix_PlayMusic(mus, -1);   /* -1 means loop forever */
#endif /* AUDIO_SDL3 */
    } else {
#ifdef AUDIO_SDL3
      MIX_SetTrackAudio(tracks[channel], mus);
      MIX_PlayTrack(tracks[channel], 0);
      MIX_SetTrackStoppedCallback(tracks[channel],
                                  audio_callback_wrapper, cb);
#else  /* AUDIO_SDL3 */
      Mix_PlayMusic(mus, 0);
      Mix_HookMusicFinished(cb);
#endif /* AUDIO_SDL3 */
    }
    log_verbose("Playing file \"%s\" on music channel", fullpath);
    /* In case we did a sdl_audio_stop() recently; add volume controls later */
#ifdef AUDIO_SDL3
    MIX_SetTrackGain(tracks[0], sdl_audio_volume);
#else  /* AUDIO_SDL3 */
    Mix_VolumeMusic(sdl_audio_volume * MIX_MAX_VOLUME);
#endif /* AUDIO_SDL3 */

  } else {

    /* See if we can cache on this one */
    for (j = 0; j < MIX_CHANNELS; j++) {
      if (samples[j].tag && (strcmp(samples[j].tag, tag) == 0)) {
        log_debug("Playing file \"%s\" from cache (slot %d)", fullpath, j);

#ifdef AUDIO_SDL3
        MIX_SetTrackAudio(tracks[channel], samples[j].wave);
        MIX_PlayTrack(tracks[channel], 0);
#else  /* AUDIO_SDL3 */
        Mix_PlayChannel(-1, samples[j].wave, 0);
#endif /* AUDIO_SDL3 */

        return TRUE;
      }
    }                           /* Guess not */

    /* Load wave */
#ifdef AUDIO_SDL3
    wave = MIX_LoadAudio(mixer, fullpath, FALSE);
#else  /* AUDIO_SDL3 */
    wave = Mix_LoadWAV(fullpath);
#endif /* AUDIO_SDL3 */
    if (wave == NULL) {
      log_error("Can't open file \"%s\"", fullpath);
    }

    /* Play sound sample on first available channel, returns -1 if no
       channel found */
#ifdef AUDIO_SDL3
    MIX_SetTrackAudio(tracks[channel], wave);
    MIX_PlayTrack(tracks[channel], 0);

    log_verbose("Playing file \"%s\" on channel %d", fullpath, channel);
    /* Free previous sample on this channel. it will by definition no
       longer be playing by the time we get here */
    if (samples[channel].wave) {
      MIX_DestroyAudio(samples[channel].wave);
      samples[channel].wave = NULL;
    }

    /* Remember for caching */
    samples[channel].wave = wave;
    samples[channel].tag = tag;
#else  /* AUDIO_SDL3 */
    i = Mix_PlayChannel(-1, wave, 0);

    if (i < 0) {
      log_verbose("No available sound channel to play %s.", tag);
      Mix_FreeChunk(wave);
      return FALSE;
    }
    log_verbose("Playing file \"%s\" on channel %d", fullpath, i);
    /* Free previous sample on this channel. it will by definition no
       longer be playing by the time we get here */
    if (samples[i].wave) {
      Mix_FreeChunk(samples[i].wave);
      samples[i].wave = NULL;
    }

    /* Remember for caching */
    samples[i].wave = wave;
    samples[i].tag = tag;
#endif /* AUDIO_SDL3 */

  }
  return TRUE;
}

/**********************************************************************//**
  Pause music
**************************************************************************/
static void sdl_audio_pause(void)
{
#ifdef AUDIO_SDL3
  int i;

  for (i = 0; i < MIX_CHANNELS; i++) {
    MIX_PauseTrack(tracks[i]);
  }
#else  /* AUDIO_SDL3 */
  Mix_PauseMusic();
#endif /* AUDIO_SDL3 */
}

/**********************************************************************//**
  Resume music
**************************************************************************/
static void sdl_audio_resume(void)
{
#ifdef AUDIO_SDL3
  int i;

  for (i = 0; i < MIX_CHANNELS; i++) {
    MIX_ResumeTrack(tracks[i]);
  }
#else  /* AUDIO_SDL3 */
  Mix_ResumeMusic();
#endif /* AUDIO_SDL3 */
}

/**********************************************************************//**
  Stop music
**************************************************************************/
static void sdl_audio_stop(void)
{
  /* Fade out over 2 sec */
#ifdef AUDIO_SDL3
  int i;

  for (i = 0; i < MIX_CHANNELS; i++) {
    MIX_StopTrack(tracks[i],
                  MIX_TrackMSToFrames(tracks[i], 2000));
  }
#else  /* AUDIO_SDL3 */
  Mix_FadeOutMusic(2000);
#endif /* AUDIO_SDL3 */
}

/**********************************************************************//**
  Wait for audio to die on all channels.
  WARNING: If a channel is looping, it will NEVER exit! Always call
  stop_style_music() first!
**************************************************************************/
static void sdl_audio_wait(void)
{
#ifdef AUDIO_SDL3
  bool wait = TRUE;
  while (wait) {
    int i;

    wait = FALSE;
    for (i = 0; i < MIX_CHANNELS && !wait; i++) {
      if (MIX_TrackPlaying(tracks[i])) {
        wait = TRUE;
      }
    }

    if (wait) {
      SDL_Delay(100);
    }
  }
#else  /* AUDIO_SDL3 */
  while (Mix_Playing(-1) != 0) {
    SDL_Delay(100);
  }
#endif /* AUDIO_SDL3 */
}

/**********************************************************************//**
  Quit SDL. If the video is still in use (by gui-sdl2), just quit the
  subsystem.

  This will need to be changed if SDL is used elsewhere.
**************************************************************************/
static void quit_sdl_audio(void)
{
#ifdef AUDIO_SDL3
  MIX_Quit();
#else  /* AUDIO_SDL3 */
  if (SDL_WasInit(SDL_INIT_VIDEO)) {
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
  } else {
    SDL_Quit();
  }
#endif /* AUDIO_SDL3 */
}

/**********************************************************************//**
  Init SDL. If the video is already in use (by gui-sdl2), just init the
  subsystem.

  This will need to be changed if SDL is used elsewhere.
**************************************************************************/
static bool init_sdl_audio(void)
{
#ifdef AUDIO_SDL3
  return MIX_Init();
#else  /* AUDIO_SDL3 */
  SDL_SetHint(SDL_HINT_AUDIO_RESAMPLING_MODE, "medium");

  if (SDL_WasInit(SDL_INIT_VIDEO)) {
    return SDL_InitSubSystem(SDL_INIT_AUDIO | SDL_INIT_NOPARACHUTE) >= 0;
  } else {
    return SDL_Init(SDL_INIT_AUDIO | SDL_INIT_NOPARACHUTE) >= 0;
  }
#endif /* AUDIO_SDL3 */
}

/**********************************************************************//**
  Clean up.
**************************************************************************/
static void sdl_audio_shutdown(struct audio_plugin *self)
{
  int i;

  self->initialized = FALSE;

  sdl_audio_stop();
  sdl_audio_wait();

  /* Remove all buffers */
  for (i = 0; i < MIX_CHANNELS; i++) {
    if (samples[i].wave) {
#ifdef AUDIO_SDL3
      MIX_StopTrack(tracks[i], 0);
      MIX_DestroyAudio(samples[i].wave);
#else  /* AUDIO_SDL3 */
      Mix_FreeChunk(samples[i].wave);
#endif /* AUDIO_SDL3 */
    }
  }

#ifdef AUDIO_SDL3
  MIX_DestroyAudio(mus);
  MIX_DestroyMixer(mixer);
#else  /* AUDIO_SDL3 */
  Mix_HaltMusic();
  Mix_FreeMusic(mus);

  Mix_CloseAudio();
#endif /* AUDIO_SDL3 */

  quit_sdl_audio();

#ifdef AUDIO_SDL3
  SDL_DestroyProperties(forever_loop);
#endif /* AUDIO_SDL3 */
}

/**********************************************************************//**
  Initialize.
**************************************************************************/
static bool sdl_audio_init(struct audio_plugin *self)
{
  int i;

  if (!init_sdl_audio()) {
    return FALSE;
  }

#ifdef AUDIO_SDL3
  mixer = MIX_CreateMixerDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, NULL);
  if (mixer == NULL) {
    log_error("Error calling MIX_CreateMixerDevice()");
#else  /* AUDIO_SDL3 */
  /* Initialize variables */
  const int audio_rate = MIX_DEFAULT_FREQUENCY;
  const int audio_format = MIX_DEFAULT_FORMAT;
  const int audio_channels = 2;

  if (Mix_OpenAudio(audio_rate, audio_format, audio_channels, buf_size) < 0) {
    log_error("Error calling Mix_OpenAudio");
#endif /* AUDIO_SDL3 */

    /* Try something else */
    quit_sdl_audio();
    return FALSE;
  }

#ifdef AUDIO_SDL3
  forever_loop = SDL_CreateProperties();
  SDL_SetNumberProperty(forever_loop,
                        "MIX_PROP_PLAY_LOOPS_NUMBER", -1);
#else  /* AUDIO_SDL3 */
  Mix_AllocateChannels(MIX_CHANNELS);
#endif /* AUDIO_SDL3 */
  for (i = 0; i < MIX_CHANNELS; i++) {
#ifdef AUDIO_SDL3
    tracks[i] = MIX_CreateTrack(mixer);
#endif /* AUDIO_SDL3 */
    samples[i].wave = NULL;
  }

  /* Sanity check, for now; add volume controls later */
  sdl_audio_set_volume(sdl_audio_volume);

  self->initialized = TRUE;

  return TRUE;
}

/**********************************************************************//**
  Initialize. Note that this function is called very early at the
  client startup. So for example logging isn't available.
**************************************************************************/
void audio_sdl_init(void)
{
  struct audio_plugin self;

  sz_strlcpy(self.name, "sdl");
  sz_strlcpy(self.descr, "Simple DirectMedia Library (SDL) mixer plugin");
  self.initialized = FALSE;
  self.init = sdl_audio_init;
  self.shutdown = sdl_audio_shutdown;
  self.stop = sdl_audio_stop;
  self.wait = sdl_audio_wait;
  self.play = sdl_audio_play;
  self.pause = sdl_audio_pause;
  self.resume = sdl_audio_resume;
  self.set_volume = sdl_audio_set_volume;
  self.get_volume = sdl_audio_get_volume;
  audio_add_plugin(&self);
  sdl_audio_volume = 1.0;
}
