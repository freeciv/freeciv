/********************************************************************** 
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
#include <config.h>
#endif

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "log.h"
#include "support.h"
#include "audio.h"
#include "gui_main_g.h"

#include "audio_none.h"

#include <proto/dos.h>

/**************************************************************************
  Clean up
**************************************************************************/
static void my_shutdown(void)
{
}

/**************************************************************************
  Stop music
**************************************************************************/
static void my_stop(void)
{
}

/**************************************************************************
  Wait
**************************************************************************/
static void my_wait(void)
{
}

/**************************************************************************
  Play sound sample
**************************************************************************/
static bool my_play(const char *const tag, const char *const fullpath,
		    bool repeat)
{
	static char cmdbuf[1024];
	if (!fullpath) return FALSE;

  sprintf(cmdbuf,"Run >NIL: <NIL: C:Play16 %s %s >NIL:",fullpath,repeat?"loops=1":"");

  system(cmdbuf);

#if 0
  if (strcmp(tag, "e_turn_bell") == 0) {
    sound_bell();
    return TRUE;
  }
#endif
  return TRUE;
}

/**************************************************************************
  Initialize.
**************************************************************************/
static bool my_init(void)
{
  return TRUE;
}

/**************************************************************************
  Initialize.
**************************************************************************/
void audio_amiga_init(void)
{
  struct audio_plugin self;

  sz_strlcpy(self.name, "amiga");
  sz_strlcpy(self.descr, "Amiga audio plugin");
  self.init = my_init;
  self.shutdown = my_shutdown;
  self.stop = my_stop;
  self.wait = my_wait;
  self.play = my_play;
  audio_add_plugin(&self);
}
