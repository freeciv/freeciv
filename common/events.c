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

#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

#include <stdio.h>
#include <stdlib.h>

/* utility */
#include "fcintl.h"
#include "log.h"
#include "mem.h"
#include "shared.h"
#include "support.h"

#include "events.h"

enum event_section_n {
  E_S_ADVANCE,
  E_S_BUILD,
  E_S_CITY,
  E_S_D_ME,
  E_S_D_THEM,
  E_S_GLOBAL,
  E_S_HUT,
  E_S_NATION,
  E_S_TREATY,
  E_S_UNIT,
  E_S_VOTE,
  E_S_WONDER,
  E_S_XYZZY
};

/*
 * Information about all event sections, matching the enum above.
 */
static const char *event_sections[] = {
  /* TRANS: This and following strings are prefixes for event names, which
   * replace %s. For instance, "Technology: Selected New Goal". */
  N_("Technology: %s"),
  N_("Improvement: %s"),
  N_("City: %s"),
  N_("Diplomat Action: %s"),
  N_("Enemy Diplomat: %s"),
  N_("Global: %s"),
  N_("Hut: %s"),
  N_("Nation: %s"),
  N_("Treaty: %s"),
  N_("Unit: %s"),
  /* TRANS: "Vote" as a process */
  N_("Vote: %s"),
  N_("Wonder: %s"),
  NULL
};

#define GEN_EV(event, section, descr) { #event, NULL, section, descr, NULL, event }

/*
 * Holds information about all event types. The entries don't have
 * to be sorted.
 * Every E_* event defined in common/events.h should have an entry here.
 */
static struct {
  const char *enum_name;
  char *tag_name;
  enum event_section_n esn;
  char *descr_orig;
  char *full_descr;
  enum event_type event;
} events[] = {
  /* TRANS: This and following strings are names for events which cause the
   * server to generate messages. They are used in configuring how the client
   * handles the different types of messages. Some of them will be displayed
   * with prefixes, such as "Technology: Learned From Great Library". */
  GEN_EV(E_TECH_GAIN,		E_S_ADVANCE,	N_("Acquired New Tech")),
  GEN_EV(E_TECH_LEARNED,	E_S_ADVANCE,	N_("Learned New Tech")),
  GEN_EV(E_TECH_GOAL,		E_S_ADVANCE,	N_("Selected New Goal")),
  GEN_EV(E_TECH_LOST,		E_S_ADVANCE,	N_("Lost a Tech")),
  GEN_EV(E_TECH_EMBASSY,	E_S_ADVANCE,	N_("Other Player Gained/Lost a Tech")),
  GEN_EV(E_IMP_BUY,		E_S_BUILD,	N_("Bought")),
  GEN_EV(E_IMP_BUILD,		E_S_BUILD,	N_("Built")),
  GEN_EV(E_IMP_AUCTIONED,	E_S_BUILD,	N_("Forced to Sell")),
  GEN_EV(E_IMP_AUTO,		E_S_BUILD,	N_("New Improvement Selected")),
  GEN_EV(E_IMP_SOLD,		E_S_BUILD,	N_("Sold")),
  GEN_EV(E_CITY_CANTBUILD,	E_S_CITY,	N_("Building Unavailable Item")),
  GEN_EV(E_CITY_LOST,		E_S_CITY,	N_("Captured/Destroyed")),
  GEN_EV(E_CITY_LOVE,		E_S_CITY,	N_("Celebrating")),
  GEN_EV(E_CITY_DISORDER,	E_S_CITY,	N_("Civil Disorder")),
  GEN_EV(E_CITY_FAMINE,		E_S_CITY,	N_("Famine")),
  GEN_EV(E_CITY_FAMINE_FEARED,	E_S_CITY,	N_("Famine Feared")),
  GEN_EV(E_CITY_GROWTH,		E_S_CITY,	N_("Growth")),
  GEN_EV(E_CITY_MAY_SOON_GROW,	E_S_CITY,	N_("May Soon Grow")),
  GEN_EV(E_CITY_AQUEDUCT,	E_S_CITY,	N_("Needs Aqueduct")),
  GEN_EV(E_CITY_AQ_BUILDING,	E_S_CITY,	N_("Needs Aqueduct Being Built")),
  GEN_EV(E_CITY_NORMAL,		E_S_CITY,	N_("Normal")),
  GEN_EV(E_CITY_NUKED,		E_S_CITY,	N_("Nuked")),
  GEN_EV(E_CITY_CMA_RELEASE,	E_S_CITY,	N_("Released from citizen governor")),
  GEN_EV(E_CITY_GRAN_THROTTLE,	E_S_CITY,	N_("Suggest Growth Throttling")),
  GEN_EV(E_CITY_TRANSFER,	E_S_CITY,	N_("Transfer")),
  GEN_EV(E_CITY_BUILD,		E_S_CITY,	N_("Was Built")),
  GEN_EV(E_CITY_PLAGUE,		E_S_CITY,	N_("Has Plague")),
  GEN_EV(E_CITY_RADIUS_SQ,	E_S_CITY,	N_("City Map changed")),
  GEN_EV(E_WORKLIST,		E_S_CITY,	N_("Worklist Events")),
  GEN_EV(E_CITY_PRODUCTION_CHANGED, E_S_CITY,	N_("Production changed")),
  GEN_EV(E_DISASTER,            E_S_CITY,       N_("Disaster")),
  GEN_EV(E_MY_DIPLOMAT_BRIBE,		E_S_D_ME,	N_("Bribe")),
  GEN_EV(E_DIPLOMATIC_INCIDENT,		E_S_D_ME,	N_("Caused Incident")),
  GEN_EV(E_MY_DIPLOMAT_ESCAPE,		E_S_D_ME,	N_("Escape")),
  GEN_EV(E_MY_DIPLOMAT_EMBASSY,		E_S_D_ME,	N_("Embassy")),
  GEN_EV(E_MY_DIPLOMAT_FAILED,		E_S_D_ME,	N_("Failed")),
  GEN_EV(E_MY_DIPLOMAT_INCITE,		E_S_D_ME,	N_("Incite")),
  GEN_EV(E_MY_DIPLOMAT_POISON,		E_S_D_ME,	N_("Poison")),
  GEN_EV(E_MY_DIPLOMAT_SABOTAGE,	E_S_D_ME,	N_("Sabotage")),
  GEN_EV(E_MY_DIPLOMAT_THEFT,		E_S_D_ME,	N_("Theft")),
  GEN_EV(E_MY_SPY_STEAL_GOLD,		E_S_D_ME,	N_("Gold Theft")),
  GEN_EV(E_MY_SPY_STEAL_MAP,		E_S_D_ME,	N_("Map Theft")),
  GEN_EV(E_MY_SPY_NUKE,			E_S_D_ME,	N_("Suitcase Nuke")),
  GEN_EV(E_ENEMY_DIPLOMAT_BRIBE,	E_S_D_THEM,	N_("Bribe")),
  GEN_EV(E_ENEMY_DIPLOMAT_EMBASSY,	E_S_D_THEM,	N_("Embassy")),
  GEN_EV(E_ENEMY_DIPLOMAT_FAILED,	E_S_D_THEM,	N_("Failed")),
  GEN_EV(E_ENEMY_DIPLOMAT_INCITE,	E_S_D_THEM,	N_("Incite")),
  GEN_EV(E_ENEMY_DIPLOMAT_POISON,	E_S_D_THEM,	N_("Poison")),
  GEN_EV(E_ENEMY_DIPLOMAT_SABOTAGE,	E_S_D_THEM,	N_("Sabotage")),
  GEN_EV(E_ENEMY_DIPLOMAT_THEFT,	E_S_D_THEM,	N_("Theft")),
  GEN_EV(E_ENEMY_SPY_STEAL_GOLD,	E_S_D_THEM,	N_("Gold Theft")),
  GEN_EV(E_ENEMY_SPY_STEAL_MAP,		E_S_D_THEM,	N_("Map Theft")),
  GEN_EV(E_ENEMY_SPY_NUKE,		E_S_D_THEM,	N_("Suitcase Nuke")),
  GEN_EV(E_GLOBAL_ECO,		E_S_GLOBAL,	N_("Eco-Disaster")),
  GEN_EV(E_NUKE,		E_S_GLOBAL,	N_("Nuke Detonated")),
  GEN_EV(E_HUT_BARB,		E_S_HUT,	N_("Barbarians in a Hut Roused")),
  GEN_EV(E_HUT_CITY,		E_S_HUT,	N_("City Founded from Hut")),
  GEN_EV(E_HUT_GOLD,		E_S_HUT,	N_("Gold Found in Hut")),
  GEN_EV(E_HUT_BARB_KILLED,	E_S_HUT,	N_("Killed by Barbarians in a Hut")),
  GEN_EV(E_HUT_MERC,		E_S_HUT,	N_("Mercenaries Found in Hut")),
  GEN_EV(E_HUT_SETTLER,		E_S_HUT,	N_("Settler Found in Hut")),
  GEN_EV(E_HUT_TECH,		E_S_HUT,	N_("Tech Found in Hut")),
  GEN_EV(E_HUT_BARB_CITY_NEAR,	E_S_HUT,	N_("Unit Spared by Barbarians")),
  GEN_EV(E_ACHIEVEMENT,         E_S_NATION,     N_("Achievements")),
  GEN_EV(E_UPRISING,		E_S_NATION,	N_("Barbarian Uprising")),
  GEN_EV(E_CIVIL_WAR,		E_S_NATION,	N_("Civil War")),
  GEN_EV(E_ANARCHY,		E_S_NATION,	N_("Collapse to Anarchy")),
  GEN_EV(E_FIRST_CONTACT,	E_S_NATION,	N_("First Contact")),
  GEN_EV(E_NEW_GOVERNMENT,	E_S_NATION,	N_("Learned New Government")),
  GEN_EV(E_LOW_ON_FUNDS,	E_S_NATION,	N_("Low Funds")),
  GEN_EV(E_POLLUTION,           E_S_NATION,     N_("?event:Pollution")),
  GEN_EV(E_REVOLT_DONE,         E_S_NATION,     N_("Revolution Ended")),
  GEN_EV(E_REVOLT_START,	E_S_NATION,	N_("Revolution Started")),
  GEN_EV(E_SPACESHIP,		E_S_NATION,	N_("Spaceship Events")),
  GEN_EV(E_TREATY_ALLIANCE,	E_S_TREATY,	N_("Alliance")),
  GEN_EV(E_TREATY_BROKEN,	E_S_TREATY,	N_("Broken")),
  GEN_EV(E_TREATY_CEASEFIRE,	E_S_TREATY,	N_("Cease-fire")),
  GEN_EV(E_TREATY_EMBASSY,	E_S_TREATY,	N_("Embassy")),
  GEN_EV(E_TREATY_PEACE,	E_S_TREATY,	N_("Peace")),
  GEN_EV(E_TREATY_SHARED_VISION,E_S_TREATY,	N_("Shared Vision")),
  GEN_EV(E_UNIT_LOST_ATT,	E_S_UNIT,	N_("Attack Failed")),
  GEN_EV(E_UNIT_WIN_ATT,	E_S_UNIT,	N_("Attack Succeeded")),
  GEN_EV(E_UNIT_BUY,		E_S_UNIT,	N_("Bought")),
  GEN_EV(E_UNIT_BUILT,		E_S_UNIT,	N_("Built")),
  GEN_EV(E_UNIT_LOST_DEF,	E_S_UNIT,	N_("Defender Destroyed")),
  GEN_EV(E_UNIT_WIN_DEF,	E_S_UNIT,	N_("Defender Survived")),
  GEN_EV(E_UNIT_BECAME_VET,	E_S_UNIT,	N_("Promoted to Veteran")),
  GEN_EV(E_UNIT_LOST_MISC,      E_S_UNIT,       N_("Lost outside battle")),
  GEN_EV(E_UNIT_UPGRADED,	E_S_UNIT,	N_("Production Upgraded")),
  GEN_EV(E_UNIT_RELOCATED,	E_S_UNIT,	N_("Relocated")),
  GEN_EV(E_UNIT_ORDERS,         E_S_UNIT,       N_("Orders / goto events")),
  GEN_EV(E_UNIT_BUILT_POP_COST, E_S_UNIT,       N_("Built unit with population cost")),
  GEN_EV(E_UNIT_WAS_EXPELLED,	E_S_UNIT,	N_("Was Expelled")),
  GEN_EV(E_UNIT_DID_EXPEL,	E_S_UNIT,	N_("Did Expel")),
  GEN_EV(E_UNIT_ACTION_FAILED,	E_S_UNIT,	N_("Action failed")),
  /* TRANS: "vote" as a process */
  GEN_EV(E_VOTE_NEW,		E_S_VOTE,	N_("New vote")),
  /* TRANS: "Vote" as a process */
  GEN_EV(E_VOTE_RESOLVED,	E_S_VOTE,	N_("Vote resolved")),
  /* TRANS: "Vote" as a process */
  GEN_EV(E_VOTE_ABORTED,	E_S_VOTE,	N_("Vote canceled")),
  GEN_EV(E_WONDER_BUILD,	E_S_WONDER,	N_("Finished")),
  GEN_EV(E_WONDER_OBSOLETE,	E_S_WONDER,	N_("Made Obsolete")),
  GEN_EV(E_WONDER_STARTED,	E_S_WONDER,	N_("Started")),
  GEN_EV(E_WONDER_STOPPED,	E_S_WONDER,	N_("Stopped")),
  GEN_EV(E_WONDER_WILL_BE_BUILT,E_S_WONDER,	N_("Will Finish Next Turn")),
  GEN_EV(E_AI_DEBUG,		E_S_XYZZY,	N_("AI Debug messages")),
  GEN_EV(E_BROADCAST_REPORT,	E_S_XYZZY,	N_("Broadcast Report")),
  GEN_EV(E_CARAVAN_ACTION,	E_S_XYZZY,	N_("Caravan actions")),
  GEN_EV(E_CHAT_ERROR,		E_S_XYZZY,	N_("Chat error messages")),
  GEN_EV(E_CHAT_MSG,		E_S_XYZZY,	N_("Chat messages")),
  GEN_EV(E_CONNECTION,		E_S_XYZZY,	N_("Connect/disconnect messages")),
  GEN_EV(E_DIPLOMACY,		E_S_XYZZY,	N_("Diplomatic Message")),
  GEN_EV(E_BAD_COMMAND,		E_S_XYZZY,	N_("Error message from bad command")),
  GEN_EV(E_GAME_END,		E_S_XYZZY,	N_("Game Ended")),
  GEN_EV(E_GAME_START,		E_S_XYZZY,	N_("Game Started")),
  GEN_EV(E_NATION_SELECTED,	E_S_XYZZY,	N_("Nation Selected")),
  GEN_EV(E_DESTROYED,		E_S_XYZZY,	N_("Player Destroyed")),
  GEN_EV(E_REPORT,		E_S_XYZZY,	N_("Report")),
  GEN_EV(E_LOG_FATAL,		E_S_XYZZY,	N_("Server Aborting")),
  GEN_EV(E_LOG_ERROR,		E_S_XYZZY,	N_("Server Problems")),
  GEN_EV(E_MESSAGE_WALL,	E_S_XYZZY,	N_("Message from server operator")),
  GEN_EV(E_SETTING,		E_S_XYZZY,	N_("Server settings changed")),
  GEN_EV(E_TURN_BELL,		E_S_XYZZY,	N_("Turn Bell")),
  GEN_EV(E_SCRIPT,		E_S_XYZZY,	N_("Scenario/ruleset script message")),
  /* TRANS: Event name for when the game year changes. */
  GEN_EV(E_NEXT_YEAR,		E_S_XYZZY,	N_("Year Advance")),
  GEN_EV(E_DEPRECATION_WARNING, E_S_XYZZY,	N_("Deprecated Modpack syntax warnings")),
  GEN_EV(E_SPONTANEOUS_EXTRA,   E_S_XYZZY,      N_("Extra Appears or Disappears")),
  GEN_EV(E_UNIT_ILLEGAL_ACTION, E_S_UNIT,       N_("Unit Illegal Action")),
  GEN_EV(E_UNIT_ESCAPED,        E_S_UNIT,       N_("Unit escaped")),
  GEN_EV(E_BEGINNER_HELP,       E_S_XYZZY,      N_("Help for beginners")),
  GEN_EV(E_MY_UNIT_DID_HEAL,    E_S_UNIT,       N_("Unit did heal")),
  GEN_EV(E_MY_UNIT_WAS_HEALED,  E_S_UNIT,       N_("Unit was healed")),
  GEN_EV(E_MULTIPLIER,          E_S_NATION,     N_("Multiplier changed")),
  GEN_EV(E_UNIT_ACTION_ACTOR_SUCCESS,  E_S_UNIT, N_("Your unit did")),
  GEN_EV(E_UNIT_ACTION_ACTOR_FAILURE,  E_S_UNIT, N_("Your unit failed")),
  GEN_EV(E_UNIT_ACTION_TARGET_HOSTILE, E_S_UNIT, N_("Unit did to you")),
  GEN_EV(E_UNIT_ACTION_TARGET_OTHER,   E_S_UNIT, N_("Unit did")),
  GEN_EV(E_INFRAPOINTS,         E_S_NATION,      N_("Infrapoints")),
  GEN_EV(E_HUT_MAP,             E_S_HUT,         N_("Map found from a hut")),
  GEN_EV(E_TREATY_SHARED_TILES, E_S_TREATY,      N_("Tiles shared")),
  GEN_EV(E_CITY_CONQUERED,      E_S_CITY,        N_("Conquered")),
  /* The sound system also generates "e_client_quit", although there's no
   * corresponding identifier E_CLIENT_QUIT. */
};


/*
 * Maps from enum event_type to indexes of events[]. Set by
 * events_init().
 */
static int event_to_index[E_COUNT];

enum event_type sorted_events[E_COUNT];


/**********************************************************************//**
  Returns the translated description of the given event.
**************************************************************************/
const char *get_event_message_text(enum event_type event)
{
  fc_assert_ret_val(event_type_is_valid(event), NULL);

  if (events[event_to_index[event]].event == event) {
    return events[event_to_index[event]].full_descr;
  }

  log_error("unknown event %d", event);
  return "UNKNOWN EVENT"; /* FIXME: Should be marked for translation?
                           * we get non-translated in log message. */
}

/**********************************************************************//**
  Comparison function for qsort; i1 and i2 are pointers to an event
  (enum event_type).
**************************************************************************/
static int compar_event_message_texts(const void *i1, const void *i2)
{
  const enum event_type *j1 = i1;
  const enum event_type *j2 = i2;

  return fc_strcasecmp(get_event_message_text(*j1),
                       get_event_message_text(*j2));
}

/**********************************************************************//**
  Returns a string for the sound to be used for this message type.
**************************************************************************/
const char *get_event_tag(enum event_type event)
{
  fc_assert_ret_val(event_type_is_valid(event), NULL);

  if (events[event_to_index[event]].event == event) {
    return events[event_to_index[event]].tag_name;
  }
  log_error("unknown event %d", event);

  return NULL;
}

/**********************************************************************//**
  Does the event affect a city if there's one at the event location.
**************************************************************************/
bool is_city_event(enum event_type event)
{
  switch (event) {
  case E_GLOBAL_ECO:
  case E_CITY_LOST:
  case E_UNIT_LOST_DEF:  /* FIXME: Is this correct.
                          * I'd like to find now defendeseless city quickly! */
  case E_UNIT_LOST_MISC:
  case E_UNIT_WIN_DEF:
  case E_ENEMY_DIPLOMAT_FAILED:
  case E_ENEMY_DIPLOMAT_EMBASSY:
  case E_ENEMY_DIPLOMAT_POISON:
  case E_ENEMY_DIPLOMAT_BRIBE:
  case E_ENEMY_DIPLOMAT_INCITE:
  case E_ENEMY_DIPLOMAT_SABOTAGE:
  case E_ENEMY_DIPLOMAT_THEFT:
  case E_MY_DIPLOMAT_FAILED:
  case E_MY_DIPLOMAT_EMBASSY:
  case E_MY_DIPLOMAT_POISON:
  case E_MY_DIPLOMAT_BRIBE:
  case E_MY_DIPLOMAT_INCITE:
  case E_MY_DIPLOMAT_SABOTAGE:
  case E_MY_DIPLOMAT_THEFT:
  case E_MY_DIPLOMAT_ESCAPE:
  case E_UNIT_LOST_ATT:
  case E_UNIT_WIN_ATT:
  case E_UPRISING:
  case E_UNIT_RELOCATED:
  case E_UNIT_ILLEGAL_ACTION:
    return FALSE;

  default:
    return TRUE;
  }
}

/**********************************************************************//**
  Initialize events.
  Now also initialise sorted_events[].
**************************************************************************/
void events_init(void)
{
  int i;

  for (i = 0; i < ARRAY_SIZE(event_to_index); i++) {
    event_to_index[i] = 0;
  }

  for (i = 0; i < E_COUNT; i++) {
    int j;

    if (E_S_XYZZY > events[i].esn) {
      const char *event_format = Q_(event_sections[events[i].esn]);
      int l = 1 + strlen(event_format) + strlen(Q_(events[i].descr_orig));

      events[i].full_descr = fc_malloc(l);
      fc_snprintf(events[i].full_descr, l, event_format,
                  Q_(events[i].descr_orig));
    } else {
      /* No section part */
      events[i].full_descr = fc_strdup(Q_(events[i].descr_orig));
    }

    event_to_index[events[i].event] = i;
    events[i].tag_name = fc_strdup(events[i].enum_name);
    for (j = 0; j < strlen(events[i].tag_name); j++) {
      events[i].tag_name[j] = fc_tolower(events[i].tag_name[j]);
    }
    log_debug("event[%d]=%d: name='%s' / '%s'\n"
              "\tdescr_orig='%s'\n"
              "\tdescr='%s'",
              i, events[i].event, events[i].enum_name, events[i].tag_name,
              Qn_(events[i].descr_orig), events[i].full_descr);
  }

  for (i = 0; i <= event_type_max(); i++) {
    /* Initialise sorted list of all (even possble missing) events. */
    sorted_events[i] = i;
  }
  qsort(sorted_events, event_type_max() + 1, sizeof(*sorted_events),
        compar_event_message_texts);
}

/**********************************************************************//**
  Free events.
**************************************************************************/
void events_free(void)
{
  int i;

  for (i = 0; i <= event_type_max(); i++) {
    free(events[i].full_descr);
    events[i].full_descr = NULL;
  }
}
