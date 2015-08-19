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
#ifndef FC__HANDICAPS_H
#define FC__HANDICAPS_H

enum handicap_type {
  H_DIPLOMAT = 0,     /* Can't build offensive diplomats */
  H_AWAY,             /* Away mode */
  H_LIMITEDHUTS,      /* Can get only 25 gold and barbs from huts */
  H_DEFENSIVE,        /* Build defensive buildings without calculating need */
  H_EXPERIMENTAL,     /* Enable experimental AI features (for testing) */
  H_RATES,            /* Can't set its rates beyond government limits */
  H_TARGETS,          /* Can't target anything it doesn't know exists */
  H_HUTS,             /* Doesn't know which unseen tiles have huts on them */
  H_FOG,              /* Can't see through fog of war */
  H_NOPLANES,         /* Doesn't build air units */
  H_MAP,              /* Only knows map_is_known tiles */
  H_DIPLOMACY,        /* Not very good at diplomacy */
  H_REVOLUTION,       /* Cannot skip anarchy */
  H_EXPANSION,        /* Don't like being much larger than human */
  H_DANGER,           /* Always thinks its city is in danger */
  H_CEASEFIRE,        /* Has to offer cease-fire on first contact */
  H_NOBRIBE_WF,       /* Can't bribe worker and city founder units. */
  H_LAST
};

BV_DEFINE(bv_handicap, H_LAST);

void handicaps_init(struct player *pplayer);
void handicaps_close(struct player *pplayer);

void handicaps_set(struct player *pplayer, bv_handicap handicaps);
bool has_handicap(const struct player *pplayer, enum handicap_type htype);

#endif /* FC__HANDICAPS_H */
