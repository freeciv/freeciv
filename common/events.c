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

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "fcintl.h"
#include "log.h"
#include "shared.h"
#include "support.h"

#include "events.h"

#define GEN_EV(section, descr, event) { #event, NULL, section, descr, NULL, event }
#define GEN_EV_TERMINATOR { NULL, NULL, NULL, NULL, NULL, 0 }

/*
 * Holds information about all event types. The entries don't have
 * to be sorted.
 */
static struct {
  const char *enum_name;
  char *tag_name;
  const char *section_orig;
  char *descr_orig;
  char *full_descr;
  enum event_type event;
} events[] = {
  GEN_EV(N_("City"), N_("Building Unavailable Item"),       E_CITY_CANTBUILD),
  GEN_EV(N_("City"), N_("Captured/Destroyed"),              E_CITY_LOST),
  GEN_EV(N_("City"), N_("Celebrating"),                     E_CITY_LOVE),
  GEN_EV(N_("City"), N_("Civil Disorder"),                  E_CITY_DISORDER),
  GEN_EV(N_("City"), N_("Famine"),                          E_CITY_FAMINE),
  GEN_EV(N_("City"), N_("Famine Feared"),                   E_CITY_FAMINE_FEARED),
  GEN_EV(N_("City"), N_("Growth"),                          E_CITY_GROWTH),
  GEN_EV(N_("City"), N_("May Soon Grow"),                   E_CITY_MAY_SOON_GROW),
  GEN_EV(N_("City"), N_("Needs Aqueduct"),                  E_CITY_AQUEDUCT),
  GEN_EV(N_("City"), N_("Needs Aqueduct Being Built"),      E_CITY_AQ_BUILDING),
  GEN_EV(N_("City"), N_("Normal"),                          E_CITY_NORMAL),
  GEN_EV(N_("City"), N_("Nuked"),                           E_CITY_NUKED),
  GEN_EV(N_("City"), N_("Released from citizen governor"),  E_CITY_CMA_RELEASE),
  GEN_EV(N_("City"), N_("Suggest Growth Throttling"),       E_CITY_GRAN_THROTTLE),
  GEN_EV(N_("City"), N_("Transfer"),                        E_CITY_TRANSFER),
  GEN_EV(N_("City"), N_("Was Built"),                       E_CITY_BUILD),
  GEN_EV(N_("City"), N_("Worklist Events"),                 E_WORKLIST),
  GEN_EV(N_("City"), N_("Production changed"),              E_CITY_PRODUCTION_CHANGED),
  GEN_EV(N_("Civ"), N_("Barbarian Uprising"),               E_UPRISING ),
  GEN_EV(N_("Civ"), N_("Civil War"),                        E_CIVIL_WAR),
  GEN_EV(N_("Civ"), N_("Collapse to Anarchy"),              E_ANARCHY),
  GEN_EV(N_("Civ"), N_("First Contact"),                    E_FIRST_CONTACT),
  GEN_EV(N_("Civ"), N_("Learned New Government"),           E_NEW_GOVERNMENT),
  GEN_EV(N_("Civ"), N_("Low Funds"),                        E_LOW_ON_FUNDS),
  GEN_EV(N_("Civ"), N_("Pollution"),                        E_POLLUTION),
  GEN_EV(N_("Civ"), N_("Revolt Ended"),                     E_REVOLT_DONE),
  GEN_EV(N_("Civ"), N_("Revolt Started"),                   E_REVOLT_START),
  GEN_EV(N_("Civ"), N_("Spaceship Events"),                 E_SPACESHIP),
  GEN_EV(N_("Diplomat Action"), N_("Bribe"),                E_MY_DIPLOMAT_BRIBE),
  GEN_EV(N_("Diplomat Action"), N_("Caused Incident"),      E_DIPLOMATIC_INCIDENT),
  GEN_EV(N_("Diplomat Action"), N_("Escape"),               E_MY_DIPLOMAT_ESCAPE),
  GEN_EV(N_("Diplomat Action"), N_("Embassy"),              E_MY_DIPLOMAT_EMBASSY),
  GEN_EV(N_("Diplomat Action"), N_("Failed"),               E_MY_DIPLOMAT_FAILED),
  GEN_EV(N_("Diplomat Action"), N_("Incite"),               E_MY_DIPLOMAT_INCITE),
  GEN_EV(N_("Diplomat Action"), N_("Poison"),               E_MY_DIPLOMAT_POISON),
  GEN_EV(N_("Diplomat Action"), N_("Sabotage"),             E_MY_DIPLOMAT_SABOTAGE),
  GEN_EV(N_("Diplomat Action"), N_("Theft"),                E_MY_DIPLOMAT_THEFT),
  GEN_EV(N_("Enemy Diplomat"), N_("Bribe"),                 E_ENEMY_DIPLOMAT_BRIBE),
  GEN_EV(N_("Enemy Diplomat"), N_("Embassy"),               E_ENEMY_DIPLOMAT_EMBASSY),
  GEN_EV(N_("Enemy Diplomat"), N_("Failed"),                E_ENEMY_DIPLOMAT_FAILED),
  GEN_EV(N_("Enemy Diplomat"), N_("Incite"),                E_ENEMY_DIPLOMAT_INCITE),
  GEN_EV(N_("Enemy Diplomat"), N_("Poison"),                E_ENEMY_DIPLOMAT_POISON),
  GEN_EV(N_("Enemy Diplomat"), N_("Sabotage"),              E_ENEMY_DIPLOMAT_SABOTAGE),
  GEN_EV(N_("Enemy Diplomat"), N_("Theft"),                 E_ENEMY_DIPLOMAT_THEFT),
  GEN_EV(NULL, N_("Caravan actions"),                       E_CARAVAN_ACTION),
  GEN_EV(NULL, N_("Tutorial message"),                      E_TUTORIAL),
  GEN_EV(NULL, N_("Broadcast Report"),                      E_BROADCAST_REPORT),
  GEN_EV(NULL, N_("Game Ended"),                            E_GAME_END),
  GEN_EV(NULL, N_("Game Started"),                          E_GAME_START),
  GEN_EV(NULL, N_("Message from Server Operator"),          E_MESSAGE_WALL),
  GEN_EV(NULL, N_("Nation Selected"),                       E_NATION_SELECTED),
  GEN_EV(NULL, N_("Player Destroyed"),                      E_DESTROYED),
  GEN_EV(NULL, N_("Report"),                                E_REPORT),
  GEN_EV(NULL, N_("Turn Bell"),                             E_TURN_BELL),
  GEN_EV(NULL, N_("Year Advance"),                          E_NEXT_YEAR),
  GEN_EV(N_("Global"), N_("Eco-Disaster"),                  E_GLOBAL_ECO),
  GEN_EV(N_("Global"), N_("Nuke Detonated"),                E_NUKE),
  GEN_EV(N_("Hut"), N_("Barbarians in a Hut Roused"),       E_HUT_BARB),
  GEN_EV(N_("Hut"), N_("City Founded from Hut"),            E_HUT_CITY),
  GEN_EV(N_("Hut"), N_("Gold Found in Hut"),                E_HUT_GOLD),
  GEN_EV(N_("Hut"), N_("Killed by Barbarians in a Hut"),    E_HUT_BARB_KILLED),
  GEN_EV(N_("Hut"), N_("Mercenaries Found in Hut"),         E_HUT_MERC),
  GEN_EV(N_("Hut"), N_("Settler Found in Hut"),             E_HUT_SETTLER),
  GEN_EV(N_("Hut"), N_("Tech Found in Hut"),                E_HUT_TECH),
  GEN_EV(N_("Hut"), N_("Unit Spared by Barbarians"),        E_HUT_BARB_CITY_NEAR),
  GEN_EV(N_("Improvement"), N_("Bought"),                   E_IMP_BUY),
  GEN_EV(N_("Improvement"), N_("Built"),                    E_IMP_BUILD),
  GEN_EV(N_("Improvement"), N_("Forced to Sell"),           E_IMP_AUCTIONED),
  GEN_EV(N_("Improvement"), N_("New Improvement Selected"), E_IMP_AUTO),
  GEN_EV(N_("Improvement"), N_("Sold"),                     E_IMP_SOLD),
  GEN_EV(N_("Tech"), N_("Learned From Great Library"),      E_TECH_GAIN),
  GEN_EV(N_("Tech"), N_("Learned New Tech"),                E_TECH_LEARNED),
  GEN_EV(N_("Tech"), N_("Selected New Goal"),               E_TECH_GOAL),
  GEN_EV(N_("Treaty"), N_("Alliance"),                      E_TREATY_ALLIANCE),
  GEN_EV(N_("Treaty"), N_("Broken"),                        E_TREATY_BROKEN),
  GEN_EV(N_("Treaty"), N_("Cease-fire"),                    E_TREATY_CEASEFIRE),
  GEN_EV(N_("Treaty"), N_("Peace"),                         E_TREATY_PEACE),
  GEN_EV(N_("Treaty"), N_("Shared Vision"),                 E_TREATY_SHARED_VISION),
  GEN_EV(N_("Unit"), N_("Attack Failed"),                   E_UNIT_LOST_ATT),
  GEN_EV(N_("Unit"), N_("Attack Succeeded"),                E_UNIT_WIN_ATT),
  GEN_EV(N_("Unit"), N_("Bought"),                          E_UNIT_BUY),
  GEN_EV(N_("Unit"), N_("Built"),                           E_UNIT_BUILT),
  GEN_EV(N_("Unit"), N_("Defender Destroyed"),              E_UNIT_LOST),
  GEN_EV(N_("Unit"), N_("Defender Survived"),               E_UNIT_WIN),
  GEN_EV(N_("Unit"), N_("Became More Veteran"),             E_UNIT_BECAME_VET),
  GEN_EV(N_("Unit"), N_("Production Upgraded"),             E_UNIT_UPGRADED),
  GEN_EV(N_("Unit"), N_("Relocated"),                       E_UNIT_RELOCATED),
  GEN_EV(N_("Unit"), N_("Orders / goto events"),            E_UNIT_ORDERS),
  GEN_EV(N_("Wonder"), N_("Finished"),                      E_WONDER_BUILD),
  GEN_EV(N_("Wonder"), N_("Made Obsolete"),                 E_WONDER_OBSOLETE),
  GEN_EV(N_("Wonder"), N_("Started"),                       E_WONDER_STARTED),
  GEN_EV(N_("Wonder"), N_("Stopped"),                       E_WONDER_STOPPED),
  GEN_EV(N_("Wonder"), N_("Will Finish Next Turn"),         E_WONDER_WILL_BE_BUILT),
  GEN_EV(NULL, N_("Diplomatic Message"),                    E_DIPLOMACY),
  GEN_EV(N_("Treaty"), N_("Embassy"),                       E_TREATY_EMBASSY),
  GEN_EV(NULL, N_("Error message from bad command"),        E_BAD_COMMAND),
  GEN_EV(NULL, N_("Server settings changed"),               E_SETTING),
  GEN_EV(NULL, N_("Chat messages"),                         E_CHAT_MSG),
  GEN_EV(NULL, N_("Chat error messages"),                   E_CHAT_ERROR),
  GEN_EV(NULL, N_("Connect/disconnect messages"),           E_CONNECTION),
  GEN_EV(NULL, N_("AI Debug messages"),                     E_AI_DEBUG),
  GEN_EV(NULL, N_("Player settings"),                       E_PLAYER_SETTINGS),
  GEN_EV_TERMINATOR
};


/* 
 * Maps from enum event_type to indexes of events[]. Set by
 * events_init. 
 */
static int event_to_index[E_LAST];

enum event_type sorted_events[E_LAST];


/**************************************************************************
  Returns the translated description of the given event.
**************************************************************************/
const char *get_event_message_text(enum event_type event)
{
  assert(event >= 0 && event < E_LAST);

  if (events[event_to_index[event]].event == event) {
    return events[event_to_index[event]].full_descr;
  }

  freelog(LOG_ERROR, "unknown event %d", event);
  return "UNKNOWN EVENT"; /* FIXME: Should be marked for translation?
                           * we get non-translated in log message. */
}

/**************************************************************************
  Comparison function for qsort; i1 and i2 are pointers to an event
  (enum event_type).
**************************************************************************/
static int compar_event_message_texts(const void *i1, const void *i2)
{
  const enum event_type *j1 = i1;
  const enum event_type *j2 = i2;
  
  return mystrcasecmp(get_event_message_text(*j1),
		      get_event_message_text(*j2));
}

/****************************************************************************
  Returns a string for the sound to be used for this message type.
****************************************************************************/
const char *get_event_sound_tag(enum event_type event)
{
  if (event < 0 || event >= E_LAST) {
    return NULL;
  }

  assert(event >= 0 && event < E_LAST);

  if (events[event_to_index[event]].event == event) {
    return events[event_to_index[event]].tag_name;
  }
  freelog(LOG_ERROR, "unknown event %d", event);
  return NULL;
}

/****************************************************************************
 If is_city_event is FALSE this event doesn't effect a city even if
 there is a city at the event location.
****************************************************************************/
bool is_city_event(enum event_type event)
{
  switch (event) {
  case E_GLOBAL_ECO:
  case E_CITY_LOST:
  case E_UNIT_LOST:
  case E_UNIT_WIN:
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
    return FALSE;

  default:
    return TRUE;
  }
}

/****************************************************************************
  Initialize events. 
  Now also initialise sorted_events[].
****************************************************************************/
void events_init(void)
{
  int i;
  
  for (i = 0; i < ARRAY_SIZE(event_to_index); i++) {
    event_to_index[i] = 0;
  }

  for (i = 0; events[i].enum_name; i++) {
    int j;

    if (events[i].section_orig) {
      /* TRANS: Most event descriptions come in two parts "Civ: Transfer"
       *        This format is their glue. */
      const char *event_format = Q_("?eventdescr:%s: %s");

      events[i].full_descr = fc_malloc(strlen(_(events[i].descr_orig))
                                       + strlen(_(events[i].section_orig))
                                       + strlen(event_format) + 1);
      sprintf(events[i].full_descr, event_format,
              _(events[i].section_orig),
              _(events[i].descr_orig));
    } else {
      /* No section part */
      events[i].full_descr = _(events[i].descr_orig);
    }

    event_to_index[events[i].event] = i;
    events[i].tag_name = mystrdup(events[i].enum_name);
    for (j = 0; j < strlen(events[i].tag_name); j++) {
      events[i].tag_name[j] = my_tolower(events[i].tag_name[j]);
    }
    freelog(LOG_DEBUG,
	    "event[%d]=%d: name='%s' / '%s'\n\tdescr_orig='%s'\n\tdescr='%s'",
	    i, events[i].event, events[i].enum_name, events[i].tag_name,
	    events[i].descr_orig, events[i].full_descr);
  }

  for (i = 0; i < E_LAST; i++)  {
    sorted_events[i] = i;
  }
  qsort(sorted_events, E_LAST, sizeof(*sorted_events),
	compar_event_message_texts);
}

/****************************************************************************
  Free events. 
****************************************************************************/
void events_free(void)
{
  int i;

  for (i = 0; events[i].enum_name; i++) {
    if (events[i].section_orig) {
      /* We have allocated memory for this event */
      free(events[i].full_descr);
      events[i].full_descr = NULL;
    }
  }
}
