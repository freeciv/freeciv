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
#ifndef FC__EVENTS_H
#define FC__EVENTS_H

enum {
  E_NOEVENT = -1,
  E_LOW_ON_FUNDS,
  E_POLLUTION,
  E_WARMING,
  E_CITY_DISORDER,
  E_CITY_LOVE,
  E_CITY_NORMAL,
  E_CITY_GROWTH,
  E_CITY_AQUEDUCT,
  E_CITY_FAMINE,
  E_CITY_LOST,
  E_CITY_CANTBUILD,
  E_WONDER_STARTED,
  E_WONDER_BUILD,
  E_IMP_BUILD,
  E_IMP_AUTO,
  E_IMP_AUCTIONED,
  E_UNIT_UPGRADED,
  E_UNIT_BUILD,
  E_UNIT_LOST,
  E_UNIT_WIN,
  E_ANARCHY,
  E_DIPLOMATED,
  E_TECH_GAIN,
  E_DESTROYED,
  E_IMP_BUY,
  E_IMP_SOLD,
  E_UNIT_BUY,
  E_WONDER_STOPPED,
  E_CITY_AQ_BUILDING,
  E_MY_DIPLOMAT,
  E_UNIT_LOST_ATT,
  E_UNIT_WIN_ATT,
  E_CITY_GRAN_THROTTLE,
  E_SPACESHIP,
  E_UPRISING,
  E_WORKLIST,
  E_CANCEL_PACT,
  E_DIPL_INCIDENT,
  /* Note:  If you add a new event, make sure you make a similar change to
     message_text in client/options.c
     */
  E_LAST
};

#endif /* FC__EVENTS_H */
