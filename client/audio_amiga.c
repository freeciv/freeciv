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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <clib/alib_protos.h>
#include <datatypes/soundclass.h>
#include <intuition/classusr.h>
#include <proto/datatypes.h>
#include <proto/intuition.h>

#include "log.h"
#include "support.h"

#include "audio.h"
#include "gui_main_g.h"

#include "audio_amiga.h"

#define MAX_SAMPLES 8

struct MySample
{
  Object    *obj;
  const char  *tag;
  ULONG     time;
};

static struct MySample samples[ MAX_SAMPLES ];
static Object *MusicSample  = NULL;

/**************************************************************************
  Clean up
**************************************************************************/
static void my_shutdown(void)
{
  int i;

  for (i = 0; i < MAX_SAMPLES; i++)
  {
    DisposeDTObject( samples[i].obj );
  }

}

/**************************************************************************
  Stop music
**************************************************************************/
static void my_stop(void)
{
  if ( MusicSample != NULL )
    DoMethod(MusicSample, DTM_TRIGGER, 0, STM_STOP, 0);
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
static bool my_play(const char *const tag, const char *const fullpath, bool repeat)
{
  Object  *CurrentSample  = NULL;
  ULONG time, dummy;
  int slot, i;

  /*  Find cached sample and alternatively look for the oldest slot */

  time  = -1;

  for (i = 0; i < MAX_SAMPLES; i++)
  {
    if ( samples[i].tag && (strcmp( samples[i].tag, tag) == 0 ))
    {
      CurrentSample = samples[i].obj;
      slot  = i;
      break;
    }

    if ( samples[ i ].time <= time )
      slot = i;
  }

  if ( CurrentSample == NULL )
  {
    if ( fullpath == NULL )
      return( FALSE );

    DisposeDTObject( samples[ slot ].obj );

    CurrentSample = NewDTObject((APTR)fullpath,
                DTA_GroupID, GID_SOUND,
                SDTA_Cycles, repeat ? -1 : 1,
                DTA_Repeat, repeat,
                TAG_DONE);

    samples[ slot ].obj = CurrentSample;
    samples[ slot ].tag = tag;

    if ( CurrentSample == NULL )
      return( FALSE );
  }

  /* DateStamp the latest sample. This way samples that were not used for
   * a while get expunged from the memory.  */

  CurrentTime( &samples[ slot ].time, &dummy );

  if ( repeat )
    MusicSample = CurrentSample;

  DoMethod(CurrentSample, DTM_TRIGGER, 0, STM_PLAY, 0);
  return( TRUE );
}

/**************************************************************************
  Initialize.
**************************************************************************/
static bool my_init(void)
{
  int i;

  for (i = 0; i < MAX_SAMPLES; i++)
  {
    samples[i].obj    = NULL;
    samples[i].tag    = NULL;
    samples[i].time = 0;
  }

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
