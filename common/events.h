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

enum event_type {
  E_NOEVENT = -1,
  E_CITY_CANTBUILD,
  E_CITY_LOST,
  E_CITY_LOVE,
  E_CITY_DISORDER,
  E_CITY_FAMINE,
  E_CITY_FAMINE_FEARED,
  E_CITY_GROWTH,
  E_CITY_MAY_SOON_GROW,
  E_CITY_AQUEDUCT,
  E_CITY_AQ_BUILDING,
  E_CITY_NORMAL,
  E_CITY_NUKED,
  E_CITY_CMA_RELEASE,
  E_CITY_GRAN_THROTTLE,
  E_CITY_TRANSFER,
  E_CITY_BUILD,
  E_WORKLIST,
  E_UPRISING,
  E_CIVIL_WAR,
  E_ANARCHY,
  E_FIRST_CONTACT,
  E_NEW_GOVERNMENT,
  E_LOW_ON_FUNDS,
  E_POLLUTION,
  E_REVOLT_DONE,
  E_REVOLT_START,
  E_SPACESHIP,
  E_MY_DIPLOMAT_BRIBE,
  E_DIPLOMATIC_INCIDENT,
  E_MY_DIPLOMAT_ESCAPE,
  E_MY_DIPLOMAT_EMBASSY,
  E_MY_DIPLOMAT_FAILED,
  E_MY_DIPLOMAT_INCITE,
  E_MY_DIPLOMAT_POISON,
  E_MY_DIPLOMAT_SABOTAGE,
  E_MY_DIPLOMAT_THEFT,
  E_ENEMY_DIPLOMAT_BRIBE,
  E_ENEMY_DIPLOMAT_EMBASSY,
  E_ENEMY_DIPLOMAT_FAILED,
  E_ENEMY_DIPLOMAT_INCITE,
  E_ENEMY_DIPLOMAT_POISON,
  E_ENEMY_DIPLOMAT_SABOTAGE,
  E_ENEMY_DIPLOMAT_THEFT,
  E_BROADCAST_REPORT,
  E_GAME_END,
  E_GAME_START,
  E_MESSAGE_WALL,
  E_NATION_SELECTED,
  E_DESTROYED,
  E_REPORT,
  E_TURN_BELL,
  E_NEXT_YEAR,
  E_GLOBAL_ECO,
  E_NUKE,
  E_HUT_BARB,
  E_HUT_CITY,
  E_HUT_GOLD,
  E_HUT_BARB_KILLED,
  E_HUT_MERC,
  E_HUT_SETTLER,
  E_HUT_TECH,
  E_HUT_BARB_CITY_NEAR,
  E_IMP_BUY,
  E_IMP_BUILD,
  E_IMP_AUCTIONED,
  E_IMP_AUTO,
  E_IMP_SOLD,
  E_TECH_GAIN,
  E_TECH_LEARNED,
  E_TREATY_ALLIANCE,
  E_TREATY_BROKEN,
  E_TREATY_CEASEFIRE,
  E_TREATY_PEACE,
  E_TREATY_SHARED_VISION,
  E_UNIT_LOST_ATT,
  E_UNIT_WIN_ATT,
  E_UNIT_BUY,
  E_UNIT_BUILT,
  E_UNIT_LOST,
  E_UNIT_WIN,
  E_UNIT_BECAME_VET,
  E_UNIT_UPGRADED,
  E_UNIT_RELOCATED,
  E_UNIT_ORDERS,
  E_WONDER_BUILD,
  E_WONDER_OBSOLETE,
  E_WONDER_STARTED,
  E_WONDER_STOPPED,
  E_WONDER_WILL_BE_BUILT,
  E_DIPLOMACY,
  E_CITY_PRODUCTION_CHANGED,
  E_TREATY_EMBASSY,
  /* 
   * Note: If you add a new event, make sure you make a similar change
   * to the events array in client/options.c using GEN_EV and to
   * data/stdsounds.spec.
   */
  E_LAST
};

#endif /* FC__EVENTS_H */
