/********************************************************************** 
 Freeciv - Copyright (C) 2002 - The Freeciv Project
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
***********************************************************************/
#ifndef FC__AIDATA_H
#define FC__AIDATA_H

/* 
 * This file and aidata.c contains global data structures for the AI
 * and some of the functions that fill them with useful values at the 
 * start of every turn. 
 */

struct ai_data {
  /* Long-term threats, not to be confused with short-term danger */
  struct {
    bool invasions;   /* check if we need to consider invasions */
    bool *continent;  /* non-allied cities on continent? */
    bool sea;         /* check if exists non-allied offensive ships */
    bool air;         /* check for non-allied offensive aircraft */
    bool missile;     /* check for non-allied missiles */
    int nuclear;      /* nuke check: 0=no, 1=capability, 2=built */
  } threats;

  /* Keeps track of which continents are fully explored already */
  struct {
    bool *continent;  /* are we done exploring this continent? */
    bool land_done;   /* nothing more on land to explore anywhere */
    bool sea_done;    /* nothing more to explore at sea */
  } explore;

  /* this struct is used for statistical unit building, to ensure
     that we don't build too few or too many units of a given type */
  struct {
    int *workers; /* cities to workers on continent*/
    int *cities;  /* number of cities on continent */
  } stats;

  int num_continents; /* last time we updated our continent data */
};

void ai_data_turn_init(struct player *pplayer);
void ai_data_turn_done(struct player *pplayer);

struct ai_data *ai_data_get(struct player *pplayer);

#endif
