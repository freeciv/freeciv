/********************************************************************** 
 Freeciv - Copyright (C) 2004 - J. Pello
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

#include <audiofile.h>
#include <alsa/asoundlib.h>

#include "support.h"
#include "log.h"
#include "audio.h"

#include "audio_alsa.h"

static snd_pcm_t *sound_handle = NULL;
static snd_async_handler_t *ah;
static snd_pcm_sframes_t period_size;

static AFfilehandle file_handle = AF_NULL_FILEHANDLE;
static double file_rate;
static AFframecount file_fcount, left_fcount;
static bool file_repeat;

/**********************************************************************
  Drain if running
***********************************************************************/
static inline void snd_pcm_drain_if (snd_pcm_t *h)
{
  switch (snd_pcm_state(h))  {
    case SND_PCM_STATE_RUNNING:
    case SND_PCM_STATE_XRUN:
    case SND_PCM_STATE_PAUSED:
      snd_pcm_drain (h);
    default: ;
  }
}

/**********************************************************************
  Reset as appropiate
***********************************************************************/
static inline void snd_pcm_drop_free(snd_pcm_t *h)
{
  file_repeat = FALSE;
  switch (snd_pcm_state(h))  {
    case SND_PCM_STATE_RUNNING:
    case SND_PCM_STATE_XRUN:
    case SND_PCM_STATE_DRAINING:
    case SND_PCM_STATE_PAUSED:
      snd_pcm_drop(h);     /* fall through */
    case SND_PCM_STATE_PREPARED:
    case SND_PCM_STATE_SETUP:
      snd_pcm_hw_free(h);
    default: ;
  }
}

/**********************************************************************
  Shut down
***********************************************************************/
static void my_shutdown(void)
{
  snd_pcm_close(sound_handle);
  sound_handle = NULL;
}

/**********************************************************************
  Stop
***********************************************************************/
static void my_stop(void)
{
  file_repeat = FALSE;
  snd_pcm_drop_free(sound_handle);
}

/**********************************************************************
  Wait
***********************************************************************/
static void my_wait(void)
{
  file_repeat = FALSE;
  snd_pcm_drain_if(sound_handle);
  while (snd_pcm_state(sound_handle) == SND_PCM_STATE_DRAINING) {
    myusleep(100000);
  }
}

/**********************************************************************
  Set HW parameters
***********************************************************************/
static int set_hw_params(void)
{
  snd_pcm_hw_params_t *hwparams;
  unsigned rrate;
  unsigned period_time = 100000;

  snd_pcm_hw_params_alloca(&hwparams);

  if (snd_pcm_hw_params_any(sound_handle, hwparams) < 0) {
    return -1;
  }
  if (snd_pcm_hw_params_set_access(sound_handle, hwparams,
      SND_PCM_ACCESS_MMAP_INTERLEAVED) < 0) {
    return -1;
  }
  if (snd_pcm_hw_params_set_format(sound_handle, hwparams,
      SND_PCM_FORMAT_S16) < 0) {
    return -1;
  }
  if (snd_pcm_hw_params_set_channels(sound_handle, hwparams, 2) < 0) {
    return -1;
  }
  rrate = file_rate;
  if (snd_pcm_hw_params_set_rate_near(sound_handle, hwparams, &rrate, 0)
      < 0 ) {
    return -1;
  }
  if (rrate != (unsigned) file_rate) {
    freelog(LOG_VERBOSE, "ALSA: asked for rate %u, got %u",
            (unsigned) file_rate, rrate);
  }
  snd_pcm_hw_params_set_period_time_near(sound_handle, hwparams,
                                         &period_time, &rrate);
  if (snd_pcm_hw_params_get_period_size(hwparams, &period_size, &rrate)
      < 0) {
    return -1;
  }
  return snd_pcm_hw_params(sound_handle, hwparams);
}

/**********************************************************************
  Underrun recovery
***********************************************************************/
static int xrun_recovery(const char *s, int e)
{
  if (e == -EPIPE) {
    e = snd_pcm_prepare(sound_handle);
    if (e < 0) {
      freelog(LOG_ERROR, "ALSA: Cannot recover from underrun: %s",
              snd_strerror(e));
    } else {
      return 0;
    }
  } else if (e < 0) {
    freelog(LOG_ERROR, "ALSA: %s: %s", s, snd_strerror(e));
  }
  return e;
}

/**********************************************************************
  Send a burst of samples
***********************************************************************/
static int snd_mmap_run(void)
{
  snd_pcm_uframes_t left, frames, offset;
  snd_pcm_sframes_t commit;
  const snd_pcm_channel_area_t *area;

  left = period_size;
  if (left > left_fcount) {
    left = left_fcount;
  }

  while (left > 0) {
    frames = left;
    if (xrun_recovery("MMAP begin",
        snd_pcm_mmap_begin(sound_handle, &area, &offset, &frames)) < 0) {
      return -1;
    }

    /* Confirm that our parameters match what we expect for
     * SND_PCM_ACCESS_MMAP_INTERLEAVED and SND_PCM_FORMAT_S16.
     * Of course, we could adapt the code below, but it is easier to leave it
     * this way as long as ALSA's input format matches audiofile's output.
     */
    if (area[0].step != 32 
        || area[1].step != 32
        || area[0].addr != area[1].addr) {
      snd_pcm_mmap_commit (sound_handle, offset, 0);
      freelog(LOG_ERROR, "ALSA: unexpected area parameters");
      return -1;
    }

    if (afReadFrames(file_handle, AF_DEFAULT_TRACK,
                     ADD_TO_POINTER(area->addr, + 4 * offset), frames) < 0 )  {
      snd_pcm_mmap_commit(sound_handle, offset, 0);
      freelog(LOG_ERROR, "ALSA: cannot read frames");
      break;
    }

    commit = snd_pcm_mmap_commit(sound_handle, offset, frames);
    if (commit < 0 || commit != frames) {
      if (xrun_recovery("MMAP commit", commit<0 ? commit : -EPIPE) < 0) {
        return -1;
      }
    }
    left_fcount -= frames;
    left -= frames;
  }

  return 0;
}

/**********************************************************************
  Play callback
***********************************************************************/
static void play_callback(snd_async_handler_t *ah)
{
  snd_pcm_state_t state;
  snd_pcm_sframes_t avail;
  int restart = 0;

  while (TRUE)  {
    if (!left_fcount) {
      if (file_repeat == FALSE) {
        return;
      }
      /* rewind */
      if (afSeekFrame (file_handle, AF_DEFAULT_TRACK, 0) < 0) {
        break;
      }
      left_fcount = file_fcount;
    }

    state = snd_pcm_state(sound_handle);
    if (state == SND_PCM_STATE_XRUN) {
      if (xrun_recovery(NULL,-EPIPE) < 0) {
        break;
      }
    }

    avail =
      xrun_recovery ("avail update", snd_pcm_avail_update(sound_handle));
    if (avail < 0) {
      restart = 1;
      continue;
    }
    if (avail < period_size) {
      if (!restart) {
        return;
      }
      restart = 0;
      if (xrun_recovery("start", snd_pcm_start(sound_handle)) < 0) {
        break;
      }
    }

    if (snd_mmap_run() < 0) {
      break;
    }
  }

  /* error path */
  snd_pcm_drop_free(sound_handle);
}

/**********************************************************************
  Play
***********************************************************************/
static bool my_play(const char *tag, const char *fullpath, bool repeat)
{
  AFfilehandle new_handle;
  double new_rate;
  AFframecount new_fcount;

  if (fullpath == NULL) {
    return FALSE;
  }

  new_handle = afOpenFile(fullpath, "r", 0);
  if (new_handle == AF_NULL_FILEHANDLE)  {
    freelog(LOG_ERROR, "ALSA: cannot open %s", fullpath);
    return FALSE;
  }

  afSetVirtualSampleFormat(new_handle, AF_DEFAULT_TRACK,
                           AF_SAMPFMT_TWOSCOMP, 16);
  afSetVirtualChannels(new_handle, AF_DEFAULT_TRACK, 2);
  new_rate = afGetRate(new_handle, AF_DEFAULT_TRACK);
  new_fcount = afGetFrameCount(new_handle, AF_DEFAULT_TRACK);
  freelog(LOG_VERBOSE, "ALSA: %s: rate %f, %d frames", fullpath,
          new_rate, (int) new_fcount);

  snd_pcm_drop_free(sound_handle);
  if (file_handle != AF_NULL_FILEHANDLE) {
    afCloseFile(file_handle);
  }

  file_handle = new_handle;
  file_rate = new_rate;
  left_fcount = file_fcount = new_fcount;
  file_repeat = repeat;

  if (set_hw_params() < 0) {
    freelog (LOG_ERROR, "ALSA: bad format in %s", fullpath);
    return FALSE;
  }

  if (snd_mmap_run() < 0 || snd_mmap_run() < 0) {
    return FALSE;
  }

  if (xrun_recovery("ALSA: start error", snd_pcm_start(sound_handle)) < 0 )  {
    return FALSE;
  }

  return TRUE;
}

/**********************************************************************
  Initialize
***********************************************************************/
static bool my_init(void)
{
  if (snd_pcm_open(&sound_handle, "default", SND_PCM_STREAM_PLAYBACK, 0) 
      < 0)  {
    return FALSE;
  }

  if (snd_async_add_pcm_handler(&ah, sound_handle, play_callback, &ah)
      < 0 )  {
    freelog(LOG_ERROR, "ALSA: cannot set callback handler");
    return FALSE;
  }

  return TRUE;
}

/**********************************************************************
  Initialize. Called early during startup -- no logging available.
***********************************************************************/
void audio_alsa_init(void)
{
  struct audio_plugin self;

  sz_strlcpy(self.name, "alsa");
  sz_strlcpy(self.descr, "ALSA plugin");
  self.init = my_init;
  self.shutdown = my_shutdown;
  self.stop = my_stop;
  self.wait = my_wait;
  self.play = my_play;
  audio_add_plugin (&self);
}
