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

/* client */
#include "mapview_common.h"
#include "mapview_g.h"

#include "zoom.h"


float map_zoom = 1.0;
bool zoom_enabled = FALSE;

float mouse_zoom = 1.0;
bool zoom_individual_tiles = TRUE;

static float default_zoom_steps[] = {
  -1.0, 1.0, 2.0, -1.0
};

static float *zoom_steps = default_zoom_steps;

static struct zoom_data
{
  bool active;
  float tgt;
  float factor;
  float interval;
  bool tgt_1_0;
} zdata = { FALSE, 0.0, 0.0 };

/**********************************************************************//**
  Set map zoom level.
**************************************************************************/
void zoom_set(float new_zoom)
{
  zoom_enabled = TRUE;
  mouse_zoom = new_zoom;

  if (zoom_individual_tiles) {
    map_zoom = new_zoom;

    map_canvas_resized(mapview.width, mapview.height);
  } else {
    map_canvas_size_refresh();
  }
}

/**********************************************************************//**
  Set map zoom level to exactly one.
**************************************************************************/
void zoom_1_0(void)
{
  zoom_enabled = FALSE;
  map_zoom = 1.0;
  mouse_zoom = 1.0;

  map_canvas_resized(mapview.width, mapview.height);
}

/**********************************************************************//**
  Set list of zoom steps that the system uses.
  The list is not copied - caller is expected to maintain it.
  First and last value must be -1.0
**************************************************************************/
void zoom_set_steps(float *steps)
{
  if (steps == NULL) {
    zoom_steps = default_zoom_steps;
  } else {
    zoom_steps = steps;
  }
}

/**********************************************************************//**
  Set time zooming takes place. Default is to zoom individual tiles.
  Gui needs to implement alternatives themselves.
**************************************************************************/
void zoom_phase_set(bool individual_tiles)
{
  zoom_individual_tiles = individual_tiles;

  if (!individual_tiles) {
    map_zoom = 1.0;
    zoom_enabled = FALSE;
  }
}

/**********************************************************************//**
  Zoom level one step up
**************************************************************************/
void zoom_step_up(void)
{
  int i;

  /* Even if below previous step, close enough is considered to be in
   * previous step so that change is not minuscule */
  for (i = 1 ;
       zoom_steps[i] < mouse_zoom * 1.05 && zoom_steps[i] > 0.0 ;
       i++ ) {
    /* Empty */
  }

  if (zoom_steps[i] > 0.0) {
    if (zoom_steps[i] > 0.99 && zoom_steps[i] < 1.01) {
      zoom_1_0();
    } else {
      zoom_set(zoom_steps[i]);
    }
  }
}

/**********************************************************************//**
  Zoom level one step down
**************************************************************************/
void zoom_step_down(void)
{
  int i;

  /* Even if above previous step, close enough is considered to be in
   * previous step so that change is not minuscule */
  for (i = 1;
       zoom_steps[i] < mouse_zoom * 1.05 && zoom_steps[i] > 0.0 ;
       i++) {
  }

  /* Get back, and below */
  i = MAX(i - 2, 1);

  if (zoom_steps[i] > 0.0) {
    if (zoom_steps[i] > 0.99 && zoom_steps[i] < 1.01) {
      zoom_1_0();
    } else {
      zoom_set(zoom_steps[i]);
    }
  }
}

/**********************************************************************//**
  Start zoom animation.
**************************************************************************/
void zoom_start(float tgt, bool tgt_1_0, float factor, float interval)
{
  zdata.tgt = tgt;
  if ((tgt < mouse_zoom && factor > 1.0)
      || (tgt > mouse_zoom && factor < 1.0)) {
    factor = 1.0 / factor;
  }
  zdata.factor = factor;
  zdata.interval = interval;
  zdata.tgt_1_0 = tgt_1_0;
  zdata.active = TRUE;
}

/**********************************************************************//**
  Next step from the active zoom.
**************************************************************************/
bool zoom_update(double time_until_next_call)
{
  if (zdata.active) {
    float new_zoom = mouse_zoom * zdata.factor;

    if ((zdata.factor > 1.0 && new_zoom > zdata.tgt)
        || (zdata.factor < 1.0 && new_zoom < zdata.tgt)) {
      new_zoom = zdata.tgt;
      zdata.active = FALSE;
      if (zdata.tgt_1_0) {
        zoom_1_0();
      } else {
        zoom_set(new_zoom);
      }
    } else {
      zoom_set(new_zoom);

      return MIN(time_until_next_call, zdata.interval);
    }
  }

  return time_until_next_call;
}
