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
#ifndef FC__PACKHAND_H
#define FC__PACKHAND_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* common */
#include "events.h"             /* enum event_type */
#include "fc_types.h"           /* struct connection, struct government */
#include "map.h"

struct unit;
struct city;
struct packet_city_info;
struct packet_chat_msg;
struct packet_city_sabotage_list;
struct packet_nuke_tile_info;
struct packet_unit_action_answer;
struct packet_unit_actions;
struct packet_unit_combat_info;
struct packet_worker_task;
struct voteinfo;

/* Read-only notification after a full PACKET_UNIT_INFO has been merged into
 * the client cache. Short unit packets deliberately do not reach this hook. */
typedef void (*packhand_full_unit_info_observer_fn)(
  const struct unit *punit, int request_id, void *data);

/* A narrowly scoped packet observer used by non-GUI control surfaces that
 * issue their own action-discovery queries. Returning TRUE consumes the
 * packet and prevents the normal action-selection UI from seeing it. */
typedef bool (*packhand_unit_actions_observer_fn)(
  const struct packet_unit_actions *packet, void *data);

/* Passive packet mirrors for request-correlated control surfaces.  These
 * observers never consume a packet or replace the ordinary GUI handler. */
typedef void (*packhand_unit_action_answer_observer_fn)(
  const struct packet_unit_action_answer *packet, int request_id, void *data);
/* A reserved background sabotage-list query may disclose the same hidden
 * improvements that the normal modal dialog is authorized to display.  A
 * non-GUI control surface can consume only its exact correlated reply so the
 * list never hydrates unrelated persistent city-cache state. */
typedef bool (*packhand_city_sabotage_list_observer_fn)(
  const struct packet_city_sabotage_list *packet, int request_id, void *data);
typedef void (*packhand_chat_msg_observer_fn)(
  const struct packet_chat_msg *packet, int request_id, void *data);
typedef void (*packhand_nuke_tile_info_observer_fn)(
  const struct packet_nuke_tile_info *packet, int request_id, void *data);
typedef void (*packhand_unit_combat_info_observer_fn)(
  const struct packet_unit_combat_info *packet, int request_id, void *data);
enum packhand_investigation_stage {
  PACKHAND_INVESTIGATION_STARTED,
  PACKHAND_INVESTIGATION_CITY_INFO,
  PACKHAND_INVESTIGATION_FINISHED
};

/* Passive mirror of the normal human-client investigation boundary. CITY_INFO
 * is non-NULL only after that exact packet has been merged into the cache. */
typedef void (*packhand_investigation_observer_fn)(
  enum packhand_investigation_stage stage, int city_id,
  const struct packet_city_info *city_info, const struct city *city,
  int request_id, void *data);
/* Passive notification after PACKET_WORKER_TASK has been validated and its
 * add/change/remove result has been merged into the normal city cache. */
typedef void (*packhand_worker_task_observer_fn)(
  const struct packet_worker_task *packet, const struct city *pcity,
  int request_id, void *data);
enum packhand_vote_stage {
  PACKHAND_VOTE_NEW,
  PACKHAND_VOTE_UPDATE,
  PACKHAND_VOTE_RESOLVE,
  PACKHAND_VOTE_REMOVE
};

/* Passive notification after a structured vote packet has been merged into
 * the normal client's vote cache.  The pointed-to record is owned by the
 * vote cache and is valid only for the duration of the callback. */
typedef void (*packhand_vote_observer_fn)(
  enum packhand_vote_stage stage, const struct voteinfo *vote,
  int request_id, void *data);

/* client */
#include <packhand_gen.h>       /* <> so looked from the build directory first. */

void packhand_free(void);

void notify_about_incoming_packet(struct connection *pc,
                                  int packet_type, int size);
void notify_about_outgoing_packet(struct connection *pc,
                                  int packet_type, int size,
                                  int request_id);
void set_reports_thaw_request(int request_id);
void packhand_set_full_unit_info_observer(
  packhand_full_unit_info_observer_fn observer, void *data);
void packhand_set_unit_actions_observer(
  packhand_unit_actions_observer_fn actions_observer, void *data);
void packhand_set_unit_action_answer_observer(
  packhand_unit_action_answer_observer_fn observer, void *data);
void packhand_set_city_sabotage_list_observer(
  packhand_city_sabotage_list_observer_fn observer, void *data);
void packhand_set_chat_msg_observer(
  packhand_chat_msg_observer_fn observer, void *data);
void packhand_set_nuke_tile_info_observer(
  packhand_nuke_tile_info_observer_fn observer, void *data);
void packhand_set_unit_combat_info_observer(
  packhand_unit_combat_info_observer_fn observer, void *data);
void packhand_set_investigation_observer(
  packhand_investigation_observer_fn observer, void *data);
void packhand_set_worker_task_observer(
  packhand_worker_task_observer_fn observer, void *data);
void packhand_set_vote_observer(
  packhand_vote_observer_fn observer, void *data);

void play_sound_for_event(enum event_type type);
void target_government_init(void);
void set_government_choice(struct government *government);
void start_revolution(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* FC__PACKHAND_H */
