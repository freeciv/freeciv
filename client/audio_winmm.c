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

/************************************************************************* 
 This two includes are misordered, but it does not compile otherwise
*************************************************************************/
#include <windows.h>
#include <mmsystem.h>

#include "fcintl.h"
#include "support.h"

#include "audio.h"

#include "audio_winmm.h"

/**************************************************************************
  Stop music
**************************************************************************/
static void my_stop()
{
  sndPlaySound(NULL, 0);
}

/**************************************************************************
  Wait
**************************************************************************/
static void my_wait()
{
  /* not implemented */
}

/**************************************************************************
  Play sound sample
**************************************************************************/
static bool my_play(const char *const tag, const char *const fullpath,
		    bool repeat)
{
  if (!fullpath) {
    return FALSE;
  }
  sndPlaySound(fullpath, SND_ASYNC | (repeat ? SND_LOOP : 0));
  return TRUE;
}

/**************************************************************************
 
**************************************************************************/
static bool my_init(void)
{
  return TRUE;
}


/**************************************************************************
  Initialize. Note that this function is called very early at the
  client startup. So for example logging isn't available.
**************************************************************************/
void audio_winmm_init(void)
{
  struct audio_plugin self;

  sz_strlcpy(self.name, "winmm");
  sz_strlcpy(self.descr, "WinMM plugin");
  self.shutdown = my_stop;
  self.init = my_init;
  self.stop = my_stop;
  self.wait = my_wait;
  self.play = my_play;
  audio_add_plugin(&self);
}
