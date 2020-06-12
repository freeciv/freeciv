/**********************************************************************
 Freeciv - Copyright (C) 2020 - The Freeciv Project contributors.
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

/* common */
#include "actions.h"
#include "unittype.h"


#include "aiactions.h"


/**********************************************************************//**
  Returns TRUE if the specified unit type is able to perform diplomatic
  actions against cities.
**************************************************************************/
bool aia_utype_is_considered_spy_vs_city(const struct unit_type *putype)
{
  return (utype_can_do_action(putype, ACTION_SPY_POISON)
          || utype_can_do_action(putype, ACTION_SPY_POISON_ESC)
          || utype_can_do_action(putype, ACTION_SPY_SABOTAGE_CITY)
          || utype_can_do_action(putype, ACTION_SPY_SABOTAGE_CITY_ESC)
          || utype_can_do_action(putype, ACTION_SPY_TARGETED_SABOTAGE_CITY)
          || utype_can_do_action(putype,
                                 ACTION_SPY_TARGETED_SABOTAGE_CITY_ESC)
          || utype_can_do_action(putype, ACTION_SPY_INCITE_CITY)
          || utype_can_do_action(putype, ACTION_SPY_INCITE_CITY_ESC)
          || utype_can_do_action(putype, ACTION_ESTABLISH_EMBASSY)
          || utype_can_do_action(putype, ACTION_ESTABLISH_EMBASSY_STAY)
          || utype_can_do_action(putype, ACTION_SPY_STEAL_TECH)
          || utype_can_do_action(putype, ACTION_SPY_STEAL_TECH_ESC)
          || utype_can_do_action(putype, ACTION_SPY_TARGETED_STEAL_TECH)
          || utype_can_do_action(putype, ACTION_SPY_TARGETED_STEAL_TECH_ESC)
          || utype_can_do_action(putype, ACTION_SPY_INVESTIGATE_CITY)
          || utype_can_do_action(putype, ACTION_INV_CITY_SPEND)
          || utype_can_do_action(putype, ACTION_SPY_STEAL_GOLD)
          || utype_can_do_action(putype, ACTION_SPY_STEAL_GOLD_ESC)
          || utype_can_do_action(putype, ACTION_STEAL_MAPS)
          || utype_can_do_action(putype, ACTION_STEAL_MAPS_ESC)
          || utype_can_do_action(putype, ACTION_SPY_NUKE_ESC)
          || utype_can_do_action(putype, ACTION_SPY_NUKE));
}

/**********************************************************************//**
  Returns TRUE if the specified unit type is able to perform diplomatic
  actions.
**************************************************************************/
bool aia_utype_is_considered_spy(const struct unit_type *putype)
{
  return (aia_utype_is_considered_spy_vs_city(putype)
          || utype_can_do_action(putype,
                                 ACTION_SPY_BRIBE_UNIT)
          || utype_can_do_action(putype,
                                 ACTION_SPY_SABOTAGE_UNIT_ESC)
          || utype_can_do_action(putype,
                                 ACTION_SPY_SABOTAGE_UNIT));
}

/**********************************************************************//**
  Returns TRUE if the specified unit type is able to perform trade
  caravan actions.
**************************************************************************/
bool aia_utype_is_considered_caravan_trade(const struct unit_type *putype)
{
  return (utype_can_do_action(putype,
                              ACTION_TRADE_ROUTE)
          || utype_can_do_action(putype,
                                 ACTION_MARKETPLACE));
}

/**********************************************************************//**
  Returns TRUE if the specified unit type is able to perform caravan
  actions.
**************************************************************************/
bool aia_utype_is_considered_caravan(const struct unit_type *putype)
{
  return (aia_utype_is_considered_caravan_trade(putype)
          || utype_can_do_action(putype,
                                 ACTION_HELP_WONDER));
}
