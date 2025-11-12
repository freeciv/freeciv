/***********************************************************************
 Freeciv - Copyright (C) 2002 - R. Falke
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

/* utility */
#include "support.h"

/* client */
#include "audio.h"
#include "gui_main_g.h"

#include "audio_none.h"

/**********************************************************************//**
  Clean up
**************************************************************************/
static void none_audio_shutdown(struct audio_plugin *self)
{
  self->initialized = FALSE;
}

/**********************************************************************//**
  Stop music
**************************************************************************/
static void none_audio_stop(void)
{
}

/**********************************************************************//**
  Wait
**************************************************************************/
static void none_audio_wait(void)
{
}

/**********************************************************************//**
  Play sound sample
**************************************************************************/
static bool none_audio_play(const char *const tag, const char *const fullpath,
                            bool repeat, bool music,
                            audio_finished_callback cb)
{
  if (strcmp(tag, "e_turn_bell") == 0) {
    sound_bell();
    return TRUE;
  }

  return FALSE;
}

/**********************************************************************//**
  Pause audio
**************************************************************************/
static void none_audio_pause(void)
{
}

/**********************************************************************//**
  Resume audio
**************************************************************************/
static void none_audio_resume(void)
{
}

/**********************************************************************//**
  Initialize.
**************************************************************************/
static bool none_audio_init(struct audio_plugin *self)
{
  self->initialized = TRUE;

  return TRUE;
}

/**********************************************************************//**
  Initialize.
**************************************************************************/
void audio_none_init(void)
{
  struct audio_plugin self;

  sz_strlcpy(self.name, "none");
  sz_strlcpy(self.descr, "/dev/null plugin");
  self.initialized = FALSE;
  self.init = none_audio_init;
  self.shutdown = none_audio_shutdown;
  self.stop = none_audio_stop;
  self.wait = none_audio_wait;
  self.play = none_audio_play;
  self.pause = none_audio_pause;
  self.resume = none_audio_resume;
  audio_add_plugin(&self);
}
