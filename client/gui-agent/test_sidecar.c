/***********************************************************************
 Freeciv - Copyright (C) 1996 - The Freeciv Project

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2, or (at your option)
 any later version.
***********************************************************************/

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "fd_guard.h"
#include "protocol_v2_codec.h"

#define TEST_CHILD_FD 9
#define TEST_FRAME_MAX 8192
#define TEST_IO_TIMEOUT_SECONDS 10
#define TEST_EXIT_POLLS 500

static int agents_busy_probe_calls;
static bool agents_busy_probe_result;

static bool agents_busy_probe(void)
{
  agents_busy_probe_calls++;
  return agents_busy_probe_result;
}

static bool wait_readable(int fd)
{
  fd_set readfds;
  struct timeval timeout;
  int result;

  do {
    FD_ZERO(&readfds);
    FD_SET(fd, &readfds);
    timeout.tv_sec = TEST_IO_TIMEOUT_SECONDS;
    timeout.tv_usec = 0;
    result = select(fd + 1, &readfds, NULL, NULL, &timeout);
  } while (result < 0 && errno == EINTR);

  return result > 0 && FD_ISSET(fd, &readfds);
}

static bool read_exact(int fd, void *buffer, size_t length)
{
  unsigned char *position = buffer;

  while (length > 0) {
    ssize_t count;

    if (!wait_readable(fd)) {
      return false;
    }
    count = read(fd, position, length);
    if (count < 0 && errno == EINTR) {
      continue;
    }
    if (count <= 0) {
      return false;
    }
    position += count;
    length -= (size_t) count;
  }

  return true;
}

static bool write_exact(int fd, const void *buffer, size_t length)
{
  const unsigned char *position = buffer;

  while (length > 0) {
    ssize_t count = write(fd, position, length);

    if (count < 0 && errno == EINTR) {
      continue;
    }
    if (count <= 0) {
      return false;
    }
    position += count;
    length -= (size_t) count;
  }

  return true;
}

static bool send_frame(int fd, const char *payload)
{
  uint32_t length = (uint32_t) strlen(payload);
  uint32_t network_length = htonl(length);

  return write_exact(fd, &network_length, sizeof(network_length))
         && write_exact(fd, payload, length);
}

static bool read_frame(int fd, char *payload, size_t payload_size)
{
  uint32_t network_length;
  uint32_t length;

  if (!read_exact(fd, &network_length, sizeof(network_length))) {
    fprintf(stderr, "timed out reading frame\n");
    return false;
  }
  length = ntohl(network_length);
  if ((size_t) length + 1 > payload_size
      || !read_exact(fd, payload, length)) {
    fprintf(stderr, "invalid frame of length %u\n", length);
    return false;
  }
  payload[length] = '\0';

  return true;
}

static bool expect_frame(int fd, const char *expected)
{
  char payload[TEST_FRAME_MAX + 1];

  if (!read_frame(fd, payload, sizeof(payload))) {
    fprintf(stderr, "while expecting frame: %s\n", expected);
    return false;
  }
  if (strcmp(payload, expected) != 0) {
    fprintf(stderr, "expected frame '%s', received '%s'\n",
            expected, payload);
    return false;
  }

  return true;
}

static bool test_protocol_v2_codec(void)
{
  static const size_t action_request_counts[AGENT_V2_ACTION_KIND_COUNT] = {
    [AGENT_V2_ACTION_PREGAME_CONFIGURE] = 1,
    [AGENT_V2_ACTION_PREGAME_SET_TEAM] = 1,
    [AGENT_V2_ACTION_PREGAME_SET_READY] = 1,
    [AGENT_V2_ACTION_PLAYER_CAST_VOTE] = 1,
    [AGENT_V2_ACTION_PHASE_END] = 1,
    [AGENT_V2_ACTION_MOVE] = 2,
    [AGENT_V2_ACTION_ATTACK] = 2,
    [AGENT_V2_ACTION_FOUND_CITY] = 2,
    [AGENT_V2_ACTION_RESEARCH_TARGET] = 1,
    [AGENT_V2_ACTION_RESEARCH_GOAL] = 1,
    [AGENT_V2_ACTION_ECONOMY_RATES] = 1,
    [AGENT_V2_ACTION_PLAYER_SEND_CHAT] = 1,
    [AGENT_V2_ACTION_CITY_PRODUCTION] = 1,
    [AGENT_V2_ACTION_CITY_BUY] = 1,
    [AGENT_V2_ACTION_CITY_WORK_TILE] = 1,
    [AGENT_V2_ACTION_CITY_UNWORK_TILE] = 1,
    [AGENT_V2_ACTION_CITY_SET_SPECIALIST] = 1,
    [AGENT_V2_ACTION_CITY_SET_WORKLIST] = 1,
    [AGENT_V2_ACTION_CITY_SET_OPTIONS] = 1,
    [AGENT_V2_ACTION_CITY_RENAME] = 1,
    [AGENT_V2_ACTION_CITY_SELL_IMPROVEMENT] = 1,
    [AGENT_V2_ACTION_CITY_SET_RALLY] = 1,
    [AGENT_V2_ACTION_CITY_CLEAR_RALLY] = 1,
    [AGENT_V2_ACTION_CITY_SET_GOVERNOR] = 0,
    [AGENT_V2_ACTION_CITY_CLEAR_GOVERNOR] = 0,
    [AGENT_V2_ACTION_CITY_REQUEST_WORKER_TASK] = 1,
    [AGENT_V2_ACTION_CITY_CHANGE_WORKER_TASK] = 1,
    [AGENT_V2_ACTION_CITY_REMOVE_WORKER_TASK] = 1,
    [AGENT_V2_ACTION_WORKER_START] = 2,
    [AGENT_V2_ACTION_CANCEL_ACTIVITY] = 2,
    [AGENT_V2_ACTION_UNIT_SENTRY] = 2,
    [AGENT_V2_ACTION_UNIT_FORTIFY] = 2,
    [AGENT_V2_ACTION_UNIT_CONVERT] = 2,
    [AGENT_V2_ACTION_UNIT_DISBAND] = 2,
    [AGENT_V2_ACTION_UNIT_HOMELESS] = 2,
    [AGENT_V2_ACTION_UNIT_UPGRADE] = 2,
    [AGENT_V2_ACTION_UNIT_REHOME] = 2,
    [AGENT_V2_ACTION_UNIT_JOIN_CITY] = 2,
    [AGENT_V2_ACTION_UNIT_ESTABLISH_TRADE] = 2,
    [AGENT_V2_ACTION_UNIT_MARKETPLACE] = 2,
    [AGENT_V2_ACTION_UNIT_HELP_WONDER] = 2,
    [AGENT_V2_ACTION_UNIT_DISBAND_RECOVER] = 2,
    [AGENT_V2_ACTION_UNIT_AIRLIFT] = 2,
    [AGENT_V2_ACTION_UNIT_PARADROP] = 2,
    [AGENT_V2_ACTION_UNIT_TELEPORT] = 2,
    [AGENT_V2_ACTION_TRANSPORT_BOARD] = 2,
    [AGENT_V2_ACTION_TRANSPORT_DEBOARD] = 2,
    [AGENT_V2_ACTION_TRANSPORT_EMBARK] = 2,
    [AGENT_V2_ACTION_TRANSPORT_DISEMBARK] = 2,
    [AGENT_V2_ACTION_TRANSPORT_LOAD] = 2,
    [AGENT_V2_ACTION_TRANSPORT_UNLOAD] = 2,
    [AGENT_V2_ACTION_UNIT_AUTO_WORK] = 1,
    [AGENT_V2_ACTION_UNIT_AUTO_EXPLORE] = 1,
    [AGENT_V2_ACTION_UNIT_CANCEL_AUTOMATION] = 2,
    [AGENT_V2_ACTION_UNIT_CANCEL_ORDERS] = 2,
    [AGENT_V2_ACTION_UNIT_GOTO] = 2,
    [AGENT_V2_ACTION_UNIT_GOTO_AND_PERFORM] = 2,
    [AGENT_V2_ACTION_UNIT_CONNECT_ROUTE] = 2,
    [AGENT_V2_ACTION_UNIT_SET_ROUTE] = 2,
    [AGENT_V2_ACTION_UNIT_SPECIAL] = 2,
    [AGENT_V2_ACTION_PLAYER_PLACE_INFRA] = 1,
    [AGENT_V2_ACTION_GOVERNMENT_REVOLUTION] = 1,
    [AGENT_V2_ACTION_GOVERNMENT_CHANGE] = 1,
    [AGENT_V2_ACTION_MULTIPLIER_SET] = 1,
    [AGENT_V2_ACTION_SPACESHIP_PLACE] = 1,
    [AGENT_V2_ACTION_SPACESHIP_LAUNCH] = 1,
    [AGENT_V2_ACTION_DIPLOMACY_OPEN_MEETING] = 1,
    [AGENT_V2_ACTION_DIPLOMACY_CLOSE_MEETING] = 1,
    [AGENT_V2_ACTION_DIPLOMACY_PROPOSE_CLAUSE] = 1,
    [AGENT_V2_ACTION_DIPLOMACY_REMOVE_CLAUSE] = 1,
    [AGENT_V2_ACTION_DIPLOMACY_ACCEPT] = 1,
    [AGENT_V2_ACTION_DIPLOMACY_WITHDRAW_ACCEPTANCE] = 1,
    [AGENT_V2_ACTION_DIPLOMACY_BREAK_RELATION] = 1,
    [AGENT_V2_ACTION_DIPLOMACY_WITHDRAW_VISION] = 1,
    [AGENT_V2_ACTION_DIPLOMACY_WITHDRAW_SHARED_TILES] = 1
  };
  const char *raw = "New York%=M\xc3\xbcnchen";
  const char *canonical = "New%20York%25%3DM%C3%BCnchen";
  const char semantic[] = "phase.end|actor=none";
  char encoded[128];
  char decoded[128];
  char slot_a[32];
  char slot_a_again[32];
  char slot_b[32];
  char target_slot[32];
  char target_slot_again[32];
  char target_slot_changed[32];
  char target_slot_selector_changed[32];
  char target_slot_semantic_changed[32];
  uint32_t target_selector;
  char attack[64];
  char suicide[64];
  char phase_frame[256];
  char meta_row[256];
  char specialist_row[256];
  char unknown_tile[256];
  char unknown_local_tile[512];
  int tax;
  int luxury;
  int science;
  int pregame_team;
  char entity_kind;
  int entity_id;
  uint64_t entity_incarnation;
  uint64_t next_incarnation = 1;
  uint64_t previous_incarnation = 0;
  uint64_t research_digest;
  size_t action_kind;
  enum fc_agent_v2_terminal_result terminal = FC_AGENT_V2_TERMINAL_NONE;
  struct fc_agent_v2_epoch_identity seat = {
    .authorized = true, .player = 1, .player_number = 3,
    .map_tiles = 100, .map_xsize = 80, .map_ysize = 50,
    .map_topology = 0, .map_wrap = 1, .game_epoch = 7
  };
  struct fc_agent_v2_epoch_identity observer = seat;
  struct fc_agent_v2_epoch_identity other_seat = seat;
  struct fc_agent_v2_phase_evidence phase;
  struct fc_agent_v2_phase_evidence changed_phase;
  struct fc_agent_v2_phase_notice notice = { 0 };
  size_t i;

  if (!fc_agent_v2_percent_encode(raw, encoded, sizeof(encoded))
      || strcmp(encoded, canonical) != 0
      || !fc_agent_v2_percent_decode(encoded, decoded, sizeof(decoded))
      || strcmp(decoded, raw) != 0
      || fc_agent_v2_percent_decode("raw space", decoded, sizeof(decoded))
      || fc_agent_v2_percent_decode("bad%2fcase", decoded, sizeof(decoded))
      || fc_agent_v2_percent_decode("bad%C0%AFutf8", decoded,
                                    sizeof(decoded))) {
    fprintf(stderr, "protocol 2 canonical percent codec failed\n");
    return false;
  }
  if (snprintf(meta_row, sizeof(meta_row), FC_AGENT_V2_ROW_META,
               "running", 17, 3, "alternating", 2, 1, 1,
               80, 50, "isometric_hex", 1, 0, 23) < 0
      || strcmp(meta_row,
                "meta state=running turn=17 phase=3 cache=human-client "
                "phase_mode=alternating phase_count=2 active_phase=1 "
                "phase_ready=1 map_width=80 map_height=50 "
                "topology=isometric_hex wrap_x=1 wrap_y=0 "
                "known_tile_count=23") != 0) {
    fprintf(stderr, "protocol 2 map metadata row failed\n");
    return false;
  }
  if (snprintf(specialist_row, sizeof(specialist_row),
               FC_AGENT_V2_ROW_CITY_SPECIALIST,
               "c:20:200", 3, "Einstein", 2, 0, 0, 0,
               -3, 2, 0, 0, 1, -1) < 0
      || strcmp(specialist_row,
                "city_specialist city=c:20:200 specialist=3 name=Einstein "
                "count=2 counts_toward_population=0 can_use=0 "
                "is_default=0 food=-3 shields=2 trade=0 gold=0 luxury=1 "
                "science=-1") != 0) {
    fprintf(stderr, "protocol 2 superspecialist row failed\n");
    return false;
  }
  if (!fc_agent_v2_format_unknown_tile(
        unknown_tile, sizeof(unknown_tile), 41, 7, 9)
      || strcmp(unknown_tile,
                "tile index=41 x=7 y=9 known=0 "
                "terrain=unknown owner=none placing_extra=-1 "
                "placing_extra_name=none placing_turns=0 placing_time=-1") != 0
      || fc_agent_v2_format_unknown_tile(
        unknown_tile, 8, 41, 7, 9)
      || fc_agent_v2_format_unknown_tile(
        unknown_tile, sizeof(unknown_tile), -1, 7, 9)
      || !fc_agent_v2_format_unknown_local_tile(
        unknown_local_tile, sizeof(unknown_local_tile), 41, 7, 9)
      || strcmp(unknown_local_tile,
                "tile_local index=41 x=7 y=9 known=0 "
                "terrain=unknown owner=none placing_extra=-1 "
                "placing_extra_name=none placing_turns=0 placing_time=-1 "
                "resource_extra=-1 resource_name=none has_label=0 "
                "label=none food=-1 shields=-1 trade=-1") != 0
      || fc_agent_v2_format_unknown_local_tile(
        unknown_local_tile, 8, 41, 7, 9)
      || fc_agent_v2_format_unknown_local_tile(
        unknown_local_tile, sizeof(unknown_local_tile), -1, 7, 9)
      || fc_agent_v2_target_action_policy(false, false, false)
         != FC_AGENT_V2_TARGET_ACTION_REJECT
      || fc_agent_v2_target_action_policy(false, true, false)
         != FC_AGENT_V2_TARGET_ACTION_REJECT
      || fc_agent_v2_target_action_policy(true, false, false)
         != FC_AGENT_V2_TARGET_ACTION_REJECT
      || fc_agent_v2_target_action_policy(true, true, false)
         != FC_AGENT_V2_TARGET_ACTION_REJECT
      || fc_agent_v2_target_action_policy(false, false, true)
         != FC_AGENT_V2_TARGET_ACTION_PRESERVE_PROBABILITY
      || fc_agent_v2_target_action_policy(false, true, true)
         != FC_AGENT_V2_TARGET_ACTION_PRESERVE_PROBABILITY
      || fc_agent_v2_target_action_policy(true, false, true)
         != FC_AGENT_V2_TARGET_ACTION_REJECT
      || fc_agent_v2_target_action_policy(true, true, true)
         != FC_AGENT_V2_TARGET_ACTION_REDACT_TO_UNKNOWN
      || fc_agent_v2_action_query_policy(false, false)
         != FC_AGENT_V2_ACTION_QUERY_TARGET
      || fc_agent_v2_action_query_policy(false, true)
         != FC_AGENT_V2_ACTION_QUERY_TARGET
      || fc_agent_v2_action_query_policy(true, false)
         != FC_AGENT_V2_ACTION_QUERY_REJECT
      || fc_agent_v2_action_query_policy(true, true)
         != FC_AGENT_V2_ACTION_QUERY_ACTOR_ONLY
      || fc_agent_v2_target_server_query_allowed(false, false)
      || fc_agent_v2_target_server_query_allowed(false, true)
      || fc_agent_v2_target_server_query_allowed(true, false)
      || !fc_agent_v2_target_server_query_allowed(true, true)) {
    fprintf(stderr, "protocol 2 unknown target policy failed\n");
    return false;
  }
  if (!fc_agent_v2_parse_entity_ref(
        "p:0:1", &entity_kind, &entity_id, &entity_incarnation)
      || entity_kind != 'p' || entity_id != 0 || entity_incarnation != 1
      || !fc_agent_v2_parse_entity_ref(
        "c:2147483647:18446744073709551615", &entity_kind, &entity_id,
        &entity_incarnation)
      || entity_kind != 'c' || entity_id != INT_MAX
      || entity_incarnation != UINT64_MAX
      || !fc_agent_v2_parse_entity_ref(
        "u:7:11", &entity_kind, &entity_id, &entity_incarnation)
      || entity_kind != 'u' || entity_id != 7 || entity_incarnation != 11
      || fc_agent_v2_parse_entity_ref(
        "u:01:1", &entity_kind, &entity_id, &entity_incarnation)
      || fc_agent_v2_parse_entity_ref(
        "u:1:0", &entity_kind, &entity_id, &entity_incarnation)
      || fc_agent_v2_parse_entity_ref(
        "x:1:1", &entity_kind, &entity_id, &entity_incarnation)
      || fc_agent_v2_parse_entity_ref(
        "u:2147483648:1", &entity_kind, &entity_id, &entity_incarnation)
      || fc_agent_v2_parse_entity_ref(
        "u:1:18446744073709551616", &entity_kind, &entity_id,
        &entity_incarnation)
      || fc_agent_v2_parse_entity_ref(
        "u:1:1junk", &entity_kind, &entity_id, &entity_incarnation)) {
    fprintf(stderr, "protocol 2 entity ref parser failed\n");
    return false;
  }
  if (!fc_agent_v2_parse_rates(
        "tax=30,luxury=10,science=60", true, 70,
        &tax, &luxury, &science)
      || tax != 30 || luxury != 10 || science != 60
      || !fc_agent_v2_parse_rates(
        "tax=34,luxury=33,science=33", true, 34,
        &tax, &luxury, &science)
      || tax != 34 || luxury != 33 || science != 33
      || fc_agent_v2_parse_rates(
        "tax=30,luxury=10,science=60", false, 70,
        &tax, &luxury, &science)
      || fc_agent_v2_parse_rates(
        "tax=30,luxury=10,science=60", true, 50,
        &tax, &luxury, &science)
      || fc_agent_v2_parse_rates(
        "tax=35,luxury=5,science=59", true, 70,
        &tax, &luxury, &science)
      || fc_agent_v2_parse_rates(
        "tax=35,luxury=5,science=60", true, 34,
        &tax, &luxury, &science)
      || fc_agent_v2_parse_rates(
        "tax=030,luxury=10,science=60", true, 70,
        &tax, &luxury, &science)
      || fc_agent_v2_parse_rates(
        "luxury=10,tax=30,science=60", true, 70,
        &tax, &luxury, &science)
      || fc_agent_v2_parse_rates(
        "tax=30,luxury=10,science=60x", true, 70,
        &tax, &luxury, &science)) {
    fprintf(stderr, "protocol 2 rates validation failed\n");
    return false;
  }
  if (!fc_agent_v2_parse_pregame_team_argument(
        "team=0", 65, &pregame_team)
      || pregame_team != 0
      || !fc_agent_v2_parse_pregame_team_argument(
           "team=64", 65, &pregame_team)
      || pregame_team != 64
      || fc_agent_v2_parse_pregame_team_argument(
           "team=65", 65, &pregame_team)
      || fc_agent_v2_parse_pregame_team_argument(
           "team=01", 65, &pregame_team)
      || fc_agent_v2_parse_pregame_team_argument(
           "team=1,other=2", 65, &pregame_team)
      || fc_agent_v2_parse_pregame_team_argument(
           "team=-1", 65, &pregame_team)
      || !fc_agent_v2_pregame_team_choice_allowed(
           true, true, 2, 1, 4, true, 7)
      || !fc_agent_v2_pregame_team_choice_allowed(
           true, true, 2, 2, 7, false, 7)
      || fc_agent_v2_pregame_team_choice_allowed(
           true, true, 2, 1, 7, false, 7)
      || fc_agent_v2_pregame_team_choice_allowed(
           true, true, 2, 2, 8, false, 7)
      || fc_agent_v2_pregame_team_choice_allowed(
           true, true, 2, 2, 2, true, 7)
      || fc_agent_v2_pregame_team_choice_allowed(
           false, true, 2, 2, 4, true, 7)
      || fc_agent_v2_pregame_team_choice_allowed(
           true, false, 2, 2, 4, true, 7)) {
    fprintf(stderr, "protocol 2 pregame team validation failed\n");
    return false;
  }
  for (i = 0; i < FC_AGENT_V2_MAX_CITY_WORKLIST; i++) {
    if (!fc_agent_v2_worklist_append_allowed(i, true, i, 0)) {
      fprintf(stderr, "protocol 2 repeated worklist boundary failed\n");
      return false;
    }
  }
  if (fc_agent_v2_worklist_append_allowed(
        FC_AGENT_V2_MAX_CITY_WORKLIST, true,
        FC_AGENT_V2_MAX_CITY_WORKLIST, 0)
      || !fc_agent_v2_worklist_append_allowed(0, false, 0, 2)
      || !fc_agent_v2_worklist_append_allowed(1, false, 1, 2)
      || fc_agent_v2_worklist_append_allowed(2, false, 2, 2)
      || fc_agent_v2_worklist_append_allowed(0, false, 0, 0)) {
    fprintf(stderr, "protocol 2 stale worklist multiplicity failed\n");
    return false;
  }

  fc_agent_v2_make_slot(slot_a, sizeof(slot_a), 42, 7,
                        semantic, sizeof(semantic));
  fc_agent_v2_make_slot(slot_a_again, sizeof(slot_a_again), 42, 7,
                        semantic, sizeof(semantic));
  fc_agent_v2_make_slot(slot_b, sizeof(slot_b), 42, 8,
                        semantic, sizeof(semantic));
  if (strcmp(slot_a, slot_a_again) != 0 || strcmp(slot_a, slot_b) == 0) {
    fprintf(stderr, "protocol 2 slot revision binding failed\n");
    return false;
  }
  if (!fc_agent_v2_make_target_slot(
        target_slot, sizeof(target_slot), 42, 7, 0x1234ABCD,
        semantic, sizeof(semantic))
      || !fc_agent_v2_make_target_slot(
           target_slot_again, sizeof(target_slot_again), 42, 7,
           0x1234ABCD, semantic, sizeof(semantic))
      || !fc_agent_v2_make_target_slot(
           target_slot_changed, sizeof(target_slot_changed), 42, 8,
           0x1234ABCD, semantic, sizeof(semantic))
      || !fc_agent_v2_make_target_slot(
           target_slot_selector_changed,
           sizeof(target_slot_selector_changed), 42, 7,
           0x1234ABCE, semantic, sizeof(semantic))
      || !fc_agent_v2_make_target_slot(
           target_slot_semantic_changed,
           sizeof(target_slot_semantic_changed), 42, 7,
           0x1234ABCD, "phase.end|actor=unit", 21)
      || strlen(target_slot) != 25
      || strncmp(target_slot, "t1234ABCD", 9) != 0
      || !fc_agent_v2_parse_target_slot(target_slot, &target_selector)
      || target_selector != UINT32_C(0x1234ABCD)
      || !fc_agent_v2_target_slot_matches(target_slot, target_slot_again)
      || fc_agent_v2_target_slot_matches(target_slot, target_slot_changed)
      || strcmp(target_slot + 9, target_slot_selector_changed + 9) == 0
      || strcmp(target_slot + 9, target_slot_semantic_changed + 9) == 0
      || fc_agent_v2_parse_target_slot(
           "t1234abcd0000000000000000", &target_selector)
      || fc_agent_v2_parse_target_slot(
           "t1234ABCD000000000000000", &target_selector)
      || fc_agent_v2_target_slot_matches(
           target_slot, "t1234ABCD0000000000000000")) {
    fprintf(stderr, "protocol 2 target slot binding failed\n");
    return false;
  }
  if (fc_agent_v2_classify_completion(false, true)
        != FC_AGENT_V2_COMPLETION_WAITING
      || fc_agent_v2_classify_completion(true, true)
         != FC_AGENT_V2_COMPLETION_APPLIED
      || fc_agent_v2_classify_completion(true, false)
         != FC_AGENT_V2_COMPLETION_REJECTED) {
    fprintf(stderr, "protocol 2 action receipt classification failed\n");
    return false;
  }
  observer.authorized = false;
  observer.player = 0;
  other_seat.player = 2;
  other_seat.player_number = 4;
  if (!fc_agent_v2_epoch_changed(false, &observer, &seat)
      || !fc_agent_v2_epoch_changed(true, &seat, &observer)
      || !fc_agent_v2_epoch_changed(true, &seat, &other_seat)
      || fc_agent_v2_epoch_changed(true, &seat, &seat)) {
    fprintf(stderr, "protocol 2 seat epoch transition failed\n");
    return false;
  }
  if (!fc_agent_v2_boundary_ready(true, 0, false)
      || fc_agent_v2_boundary_ready(true, 12, false)
      || fc_agent_v2_boundary_ready(true, 0, true)
      || fc_agent_v2_boundary_ready(false, 0, false)
      || !fc_agent_v2_stream_notification_allowed(false)
      || fc_agent_v2_stream_notification_allowed(true)
      || !fc_agent_v2_action_phase_ready(true, true, true, true, true,
                                         false, false)
      || fc_agent_v2_action_phase_ready(true, true, true, false, true,
                                        false, false)
      || fc_agent_v2_action_phase_ready(true, true, true, true, false,
                                        false, false)
      || fc_agent_v2_action_phase_ready(true, true, true, true, true,
                                        true, false)
      || fc_agent_v2_action_phase_ready(true, true, true, true, true,
                                        false, true)) {
    fprintf(stderr, "protocol 2 coherent phase gating failed\n");
    return false;
  }
  agents_busy_probe_calls = 0;
  agents_busy_probe_result = true;
  if (fc_agent_v2_agents_busy_if_ready(false, agents_busy_probe)
      || agents_busy_probe_calls != 0) {
    fprintf(stderr,
            "protocol 2 pre-cache agent busy probe was not suppressed\n");
    return false;
  }
  if (!fc_agent_v2_agents_busy_if_ready(true, agents_busy_probe)
      || agents_busy_probe_calls != 1) {
    fprintf(stderr, "protocol 2 ready-cache busy result was not preserved\n");
    return false;
  }
  agents_busy_probe_result = false;
  if (fc_agent_v2_agents_busy_if_ready(true, agents_busy_probe)
      || agents_busy_probe_calls != 2) {
    fprintf(stderr, "protocol 2 ready-cache idle result was not preserved\n");
    return false;
  }
  if (!fc_agent_v2_build_phase_evidence(
        FC_AGENT_V2_PHASE_CONCURRENT, 3, 2, 8, 0, 2, -1,
        true, true, true, false, true, &phase)
      || strcmp(fc_agent_v2_phase_mode_name(phase.mode), "concurrent") != 0
      || phase.phase_count != 1 || !phase.active_phase
      || !phase.phase_ready
      || fc_agent_v2_phase_end_action_count(&phase) != 1
      || !fc_agent_v2_format_phase_available(
        phase_frame, sizeof(phase_frame), 17, &phase)
      || strcmp(phase_frame,
                "PHASE_AVAILABLE\t17\t8\t0\tconcurrent\t1\t1\t1\t0\t1")
         != 0) {
    fprintf(stderr, "protocol 2 concurrent phase evidence failed\n");
    return false;
  }
  if (!fc_agent_v2_phase_notice_needed(&notice, 4, 17, &phase)) {
    fprintf(stderr, "protocol 2 initial phase notice was suppressed\n");
    return false;
  }
  fc_agent_v2_phase_notice_record(&notice, 4, 17, &phase);
  if (fc_agent_v2_phase_notice_needed(&notice, 4, 17, &phase)
      || !fc_agent_v2_phase_notice_needed(&notice, 4, 18, &phase)
      || !fc_agent_v2_phase_notice_needed(&notice, 5, 17, &phase)) {
    fprintf(stderr, "protocol 2 phase notice deduplication failed\n");
    return false;
  }
  changed_phase = phase;
  changed_phase.phase_ready = false;
  if (fc_agent_v2_phase_notice_needed(&notice, 4, 17, &changed_phase)) {
    fprintf(stderr, "protocol 2 changed phase tuple reused a revision\n");
    return false;
  }
  if (!fc_agent_v2_phase_revision_changed(
        true, true, &phase, &changed_phase)
      || fc_agent_v2_phase_revision_changed(
           true, true, &phase, &phase)
      || fc_agent_v2_phase_revision_changed(
           false, true, &phase, &changed_phase)) {
    fprintf(stderr, "protocol 2 phase revision identity failed\n");
    return false;
  }
  memset(&notice, 0, sizeof(notice));
  if (!fc_agent_v2_phase_notice_needed(&notice, 4, 17, &phase)) {
    fprintf(stderr, "protocol 2 phase notice epoch reset failed\n");
    return false;
  }
  if (!fc_agent_v2_build_phase_evidence(
        FC_AGENT_V2_PHASE_PLAYERS_ALTERNATE, 3, 2, 9, 1, 2, -1,
        true, true, true, false, false, &phase)
      || phase.phase_count != 3 || phase.active_phase || phase.phase_ready
      || fc_agent_v2_phase_end_action_count(&phase) != 0
      || strcmp(fc_agent_v2_phase_mode_name(phase.mode),
                "players_alternate") != 0
      || !fc_agent_v2_build_phase_evidence(
        FC_AGENT_V2_PHASE_PLAYERS_ALTERNATE, 3, 2, 9, 2, 2, -1,
        true, true, true, false, true, &phase)
      || !phase.active_phase || !phase.phase_ready
      || !fc_agent_v2_build_phase_evidence(
        FC_AGENT_V2_PHASE_TEAMS_ALTERNATE, 4, 2, 10, 1, 3, 1,
        true, true, true, false, true, &phase)
      || phase.phase_count != 2 || !phase.active_phase
      || strcmp(fc_agent_v2_phase_mode_name(phase.mode),
                "teams_alternate") != 0) {
    fprintf(stderr, "protocol 2 alternate phase evidence failed\n");
    return false;
  }
  if (fc_agent_v2_build_phase_evidence(
        99, 2, 1, 1, 0, 0, 0, true, true, true, false, false, &phase)
      || fc_agent_v2_build_phase_evidence(
        FC_AGENT_V2_PHASE_CONCURRENT, 513, 1, 1, 0, 0, 0,
        true, true, true, false, false, &phase)
      || fc_agent_v2_build_phase_evidence(
        FC_AGENT_V2_PHASE_TEAMS_ALTERNATE, 2, 514, 1, 0, 0, 0,
        true, true, true, false, false, &phase)
      || fc_agent_v2_build_phase_evidence(
        FC_AGENT_V2_PHASE_PLAYERS_ALTERNATE, 2, 1, 1, 2, 0, 0,
        true, true, true, false, false, &phase)
      || fc_agent_v2_build_phase_evidence(
        FC_AGENT_V2_PHASE_CONCURRENT, 2, 1, 1, 0, 0, 0,
        false, true, true, false, false, &phase)
      || fc_agent_v2_build_phase_evidence(
        FC_AGENT_V2_PHASE_CONCURRENT, 2, 1, 1, 0, 0, 0,
        true, false, true, false, false, &phase)
      || fc_agent_v2_build_phase_evidence(
        FC_AGENT_V2_PHASE_CONCURRENT, 2, 1, 0, 0, 0, 0,
        true, true, true, false, false, &phase)
      || fc_agent_v2_build_phase_evidence(
        FC_AGENT_V2_PHASE_CONCURRENT, 2, 1, 1, 0, 0, 0,
        true, true, false, false, true, &phase)
      || fc_agent_v2_build_phase_evidence(
        FC_AGENT_V2_PHASE_PLAYERS_ALTERNATE, 2, 1, 1, 1, 0, 0,
        true, true, true, false, true, &phase)
      || fc_agent_v2_build_phase_evidence(
        FC_AGENT_V2_PHASE_CONCURRENT, 2, 1, 1, 0, 0, 0,
        true, true, true, true, true, &phase)
      || fc_agent_v2_format_phase_available(
        phase_frame, sizeof(phase_frame), 0, &changed_phase)) {
    fprintf(stderr, "protocol 2 malformed phase evidence did not fail closed\n");
    return false;
  }
  if (!fc_agent_v2_callback_matches(true, 65535, 9, 65535, 9)
      || fc_agent_v2_callback_matches(true, 2, 10, 65535, 10)
      || fc_agent_v2_callback_matches(true, 2, 10, 2, 9)
      || !fc_agent_v2_callback_matches(true, 2, 10, 2, 10)) {
    fprintf(stderr, "protocol 2 exact wrapped request correlation failed\n");
    return false;
  }
  if (!fc_agent_v2_request_group_exact(40, 41, 41, 1)
      || !fc_agent_v2_request_group_exact(40, 41, 42, 2)
      || !fc_agent_v2_request_group_exact(65534, 65535, 2, 2)
      || fc_agent_v2_request_group_exact(40, 41, 43, 2)
      || fc_agent_v2_request_group_exact(40, 42, 43, 2)
      || fc_agent_v2_request_group_exact(40, 41, 42, 3)) {
    fprintf(stderr, "protocol 2 request group correlation failed\n");
    return false;
  }
  for (action_kind = 0; action_kind < AGENT_V2_ACTION_KIND_COUNT;
       action_kind++) {
    if (fc_agent_v2_expected_request_count(
          (enum agent_v2_action_kind) action_kind)
        != action_request_counts[action_kind]) {
      fprintf(stderr,
              "protocol 2 action kind %zu has wrong request cardinality\n",
              action_kind);
      return false;
    }
  }
  if (fc_agent_v2_expected_request_count(AGENT_V2_ACTION_KIND_COUNT) != 0
      || fc_agent_v2_expected_request_count(
           (enum agent_v2_action_kind) -1) != 0) {
    fprintf(stderr, "protocol 2 unknown action cardinality did not fail closed\n");
    return false;
  }
  if (!fc_agent_v2_relation_baseline_matches(
        7, 7, UINT64_C(0x1234), UINT64_C(0x1234),
        false, false, true, true, 3, 3, true, true, false, false)
      || fc_agent_v2_relation_baseline_matches(
        7, 8, UINT64_C(0x1234), UINT64_C(0x1234),
        false, false, true, true, 3, 3, true, true, false, false)
      || fc_agent_v2_relation_baseline_matches(
        7, 7, UINT64_C(0x1234), UINT64_C(0x5678),
        false, false, true, true, 3, 3, true, true, false, false)
      || fc_agent_v2_relation_baseline_matches(
        7, 7, UINT64_C(0x1234), UINT64_C(0x1234),
        false, true, true, true, 3, 3, true, true, false, false)) {
    /* Generation rotation models close+reopen-before-start.  Digest and
     * acceptance changes model a queued acceptance race: the sent toggle is
     * terminally ambiguous and must never be attributed or replayed. */
    fprintf(stderr, "protocol 2 diplomacy boundary matching failed\n");
    return false;
  }
  if (!fc_agent_v2_unit_automation_latch_matches(
        FC_AGENT_V2_AUTOMATION_EXPLORE, true, true, 42, 42,
        true, 7, 7, 100, 100, 9, 9,
        FC_AGENT_V2_CONTROLLER_AUTO_EXPLORE, true, true)
      || fc_agent_v2_unit_automation_latch_matches(
        FC_AGENT_V2_AUTOMATION_WORK, true, true, 42, 42,
        true, 7, 7, 100, 100, 9, 9,
        FC_AGENT_V2_CONTROLLER_AUTO_WORK, false, true)
      || fc_agent_v2_unit_automation_latch_matches(
        FC_AGENT_V2_AUTOMATION_EXPLORE, true, false, 42, 42,
        true, 7, 7, 100, 100, 9, 9,
        FC_AGENT_V2_CONTROLLER_AUTO_EXPLORE, true, true)
      || fc_agent_v2_unit_automation_latch_matches(
        FC_AGENT_V2_AUTOMATION_EXPLORE, true, true, 41, 42,
        true, 7, 7, 100, 100, 9, 9,
        FC_AGENT_V2_CONTROLLER_AUTO_EXPLORE, true, true)
      || fc_agent_v2_unit_automation_latch_matches(
        FC_AGENT_V2_AUTOMATION_EXPLORE, true, true, 42, 42,
        true, 7, 7, 101, 100, 9, 9,
        FC_AGENT_V2_CONTROLLER_AUTO_EXPLORE, true, true)) {
    fprintf(stderr, "protocol 2 automation latch matching failed\n");
    return false;
  }
  if (!fc_agent_v2_unit_automation_postcondition(
        FC_AGENT_V2_AUTOMATION_WORK, 100, true, 100,
        FC_AGENT_V2_CONTROLLER_NONE, true, true, false, true,
        true, 100, FC_AGENT_V2_CONTROLLER_AUTO_WORK,
        false, false, true, false)
      || !fc_agent_v2_unit_automation_postcondition(
        FC_AGENT_V2_AUTOMATION_EXPLORE, 100, true, 100,
        FC_AGENT_V2_CONTROLLER_NONE, true, true, false, true,
        false, 0, FC_AGENT_V2_CONTROLLER_NONE,
        false, false, false, true)
      || !fc_agent_v2_unit_automation_postcondition(
        FC_AGENT_V2_AUTOMATION_CANCEL, 100, true, 100,
        FC_AGENT_V2_CONTROLLER_AUTO_EXPLORE, false, true, true, false,
        true, 100, FC_AGENT_V2_CONTROLLER_NONE,
        true, false, true, false)
      || fc_agent_v2_unit_automation_postcondition(
        FC_AGENT_V2_AUTOMATION_WORK, 100, true, 99,
        FC_AGENT_V2_CONTROLLER_NONE, true, true, false, true,
        true, 100, FC_AGENT_V2_CONTROLLER_AUTO_WORK,
        false, false, true, false)
      || fc_agent_v2_unit_automation_postcondition(
        FC_AGENT_V2_AUTOMATION_EXPLORE, 100, true, 100,
        FC_AGENT_V2_CONTROLLER_NONE, true, true, true, true,
        true, 100, FC_AGENT_V2_CONTROLLER_AUTO_EXPLORE,
        false, true, true, false)
      || fc_agent_v2_unit_automation_postcondition(
        FC_AGENT_V2_AUTOMATION_CANCEL, 100, true, 100,
        FC_AGENT_V2_CONTROLLER_AUTO_EXPLORE, false, true, false, true,
        false, 0, FC_AGENT_V2_CONTROLLER_NONE,
        true, false, true, false)) {
    fprintf(stderr, "protocol 2 automation postcondition failed\n");
    return false;
  }
  if (!fc_agent_v2_unit_cancel_orders_postcondition(
        100, true, 100, 42, 42, FC_AGENT_V2_CONTROLLER_NONE,
        true, true, true, false, true, 100, false, true)
      || !fc_agent_v2_unit_cancel_orders_postcondition(
        100, true, 100, 42, 42, FC_AGENT_V2_CONTROLLER_NONE,
        true, true, true, true, true, 100, false, true)
      || fc_agent_v2_unit_cancel_orders_postcondition(
        100, true, 100, 42, 42, FC_AGENT_V2_CONTROLLER_AUTO_WORK,
        true, true, true, false, true, 100, false, true)
      || fc_agent_v2_unit_cancel_orders_postcondition(
        100, true, 100, 42, 42, FC_AGENT_V2_CONTROLLER_NONE,
        true, true, false, false, true, 100, false, true)
      || fc_agent_v2_unit_cancel_orders_postcondition(
        100, true, 100, 42, -1, FC_AGENT_V2_CONTROLLER_NONE,
        true, true, true, false, true, 100, false, true)
      || fc_agent_v2_unit_cancel_orders_postcondition(
        100, true, 100, 42, 42, FC_AGENT_V2_CONTROLLER_NONE,
        true, true, true, false, true, 101, false, true)
      || fc_agent_v2_unit_cancel_orders_postcondition(
        100, true, 100, 42, 42, FC_AGENT_V2_CONTROLLER_NONE,
        true, true, true, false, true, 100, true, true)
      || fc_agent_v2_unit_cancel_orders_postcondition(
        100, true, 100, 42, 42, FC_AGENT_V2_CONTROLLER_NONE,
        true, true, true, false, true, 100, false, false)
      || fc_agent_v2_unit_cancel_orders_postcondition(
        100, false, 100, 42, 42, FC_AGENT_V2_CONTROLLER_NONE,
        true, true, true, false, true, 100, false, true)
      || fc_agent_v2_unit_cancel_orders_postcondition(
        100, true, 99, 42, 42, FC_AGENT_V2_CONTROLLER_NONE,
        true, true, true, false, true, 100, false, true)
      || fc_agent_v2_unit_cancel_orders_postcondition(
        100, true, 100, 41, 42, FC_AGENT_V2_CONTROLLER_NONE,
        true, true, true, false, true, 100, false, true)
      || fc_agent_v2_unit_cancel_orders_postcondition(
        100, true, 100, 42, 42, FC_AGENT_V2_CONTROLLER_NONE,
        false, true, true, false, true, 100, false, true)
      || fc_agent_v2_unit_cancel_orders_postcondition(
        100, true, 100, 42, 42, FC_AGENT_V2_CONTROLLER_NONE,
        true, false, true, false, true, 100, false, true)
      || fc_agent_v2_unit_cancel_orders_postcondition(
        100, true, 100, 42, 42, FC_AGENT_V2_CONTROLLER_NONE,
        true, true, true, false, false, 0, false, true)) {
    fprintf(stderr, "protocol 2 cancel-orders postcondition failed\n");
    return false;
  }
  if (!fc_agent_v2_worker_task_echo_matches(
        true, true, true, 42, 42, 7, 7, 99, 99,
        13, 13, 4, 4, 100, 100, true)
      || fc_agent_v2_worker_task_echo_matches(
        true, true, true, 41, 42, 7, 7, 99, 99,
        13, 13, 4, 4, 100, 100, true)
      || fc_agent_v2_worker_task_echo_matches(
        true, true, true, 42, 42, 7, 7, 99, 99,
        13, 13, 4, 5, 100, 100, true)
      || fc_agent_v2_worker_task_echo_matches(
        true, true, true, 42, 42, 7, 7, 99, 99,
        13, 13, 4, 4, 100, 100, false)) {
    fprintf(stderr, "protocol 2 worker-task echo correlation failed\n");
    return false;
  }
  if (!fc_agent_v2_rally_state_canonical(false, false, false, 0, 0)
      || !fc_agent_v2_rally_state_canonical(true, true, false, 3, 0x1234)
      || !fc_agent_v2_rally_state_canonical(
           true, false, false, 1999, 0x1234)
      || fc_agent_v2_rally_state_canonical(
           true, false, false, 2000, 0x1234)
      || fc_agent_v2_rally_state_canonical(false, true, false, 0, 0)
      || fc_agent_v2_rally_state_canonical(false, false, false, 0, 1)
      || fc_agent_v2_rally_state_canonical(true, false, false, 0, 1)
      || !fc_agent_v2_rally_postcondition(
        100, true, 100, 42, 42, true, 100, 42,
        true, true, 3, 0x1234,
        true, true, false, 3, 0x1234)
      || !fc_agent_v2_rally_postcondition(
        100, true, 100, 42, 42, true, 100, 42,
        true, false, 1999, 0x1234,
        true, false, false, 1999, 0x1234)
      || fc_agent_v2_rally_postcondition(
        100, true, 100, 42, 42, true, 100, 42,
        true, false, 2000, 0x1234,
        true, false, false, 2000, 0x1234)
      || !fc_agent_v2_rally_postcondition(
        100, true, 100, 42, 42, true, 100, 42,
        false, false, 0, 0,
        false, false, false, 0, 0)
      || fc_agent_v2_rally_postcondition(
        100, true, 100, 42, 42, true, 100, 42,
        true, true, 3, 0x1234,
        true, true, false, 3, 0x1235)
      || fc_agent_v2_rally_postcondition(
        100, true, 100, 42, 42, true, 101, 42,
        true, true, 3, 0x1234,
        true, true, false, 3, 0x1234)
      || fc_agent_v2_rally_terminal(
           true, true, true, true, true)
         != FC_AGENT_V2_TERMINAL_APPLIED
      || fc_agent_v2_rally_terminal(
           true, true, true, true, false)
         != FC_AGENT_V2_TERMINAL_PROCESSING_BOUNDARY_MISMATCH
      || fc_agent_v2_rally_terminal(
           false, true, true, true, true)
         != FC_AGENT_V2_TERMINAL_SEAT_EPOCH_CHANGED) {
    fprintf(stderr, "protocol 2 rally state/terminal contract failed\n");
    return false;
  }
  if (!fc_agent_v2_unit_goto_postcondition(
        100, true, 100, 42, 42, FC_AGENT_V2_CONTROLLER_NONE,
        true, true, false, true, true, true, 77,
        true, 100, 42, true, 77)
      || !fc_agent_v2_unit_goto_postcondition(
        100, true, 100, 42, 42, FC_AGENT_V2_CONTROLLER_NONE,
        true, true, false, true, true, true, 77,
        true, 100, 77, false, -1)
      || fc_agent_v2_unit_goto_postcondition(
        100, true, 100, 42, 41, FC_AGENT_V2_CONTROLLER_NONE,
        true, true, false, true, true, true, 77,
        true, 100, 42, true, 77)
      || fc_agent_v2_unit_goto_postcondition(
        100, true, 100, 42, 42, FC_AGENT_V2_CONTROLLER_AUTO_WORK,
        true, true, false, true, true, true, 77,
        true, 100, 42, true, 77)
      || fc_agent_v2_unit_goto_postcondition(
        100, true, 100, 42, 42, FC_AGENT_V2_CONTROLLER_NONE,
        false, true, false, true, true, true, 77,
        true, 100, 42, true, 77)
      || fc_agent_v2_unit_goto_postcondition(
        100, true, 100, 42, 42, FC_AGENT_V2_CONTROLLER_NONE,
        true, false, false, true, true, true, 77,
        true, 100, 42, true, 77)
      || fc_agent_v2_unit_goto_postcondition(
        100, true, 100, 42, 42, FC_AGENT_V2_CONTROLLER_NONE,
        true, true, true, true, true, true, 77,
        true, 100, 42, true, 77)
      || fc_agent_v2_unit_goto_postcondition(
        100, true, 100, 42, 42, FC_AGENT_V2_CONTROLLER_NONE,
        true, true, false, false, true, true, 77,
        true, 100, 42, true, 77)
      || fc_agent_v2_unit_goto_postcondition(
        100, true, 100, 42, 42, FC_AGENT_V2_CONTROLLER_NONE,
        true, true, false, true, false, true, 77,
        true, 100, 42, true, 77)
      || fc_agent_v2_unit_goto_postcondition(
        100, true, 100, 42, 42, FC_AGENT_V2_CONTROLLER_NONE,
        true, true, false, true, true, false, 77,
        true, 100, 42, true, 77)
      || fc_agent_v2_unit_goto_postcondition(
        100, true, 100, 42, 42, FC_AGENT_V2_CONTROLLER_NONE,
        true, true, false, true, true, true, 42,
        true, 100, 42, true, 42)
      || fc_agent_v2_unit_goto_postcondition(
        100, true, 100, 42, 42, FC_AGENT_V2_CONTROLLER_NONE,
        true, true, false, true, true, true, 77,
        false, 0, -1, false, -1)
      || fc_agent_v2_unit_goto_postcondition(
        100, true, 100, 42, 42, FC_AGENT_V2_CONTROLLER_NONE,
        true, true, false, true, true, true, 77,
        true, 101, 42, true, 77)
      || fc_agent_v2_unit_goto_postcondition(
        100, true, 100, 42, 42, FC_AGENT_V2_CONTROLLER_NONE,
        true, true, false, true, true, true, 77,
        true, 100, 42, true, 76)
      || fc_agent_v2_unit_goto_postcondition(
        100, true, 100, 42, 42, FC_AGENT_V2_CONTROLLER_NONE,
        true, true, false, true, true, true, 77,
        true, 100, 76, false, -1)) {
    fprintf(stderr, "protocol 2 goto postcondition failed\n");
    return false;
  }
  if (!fc_agent_v2_goto_candidate_precedes(1, 99, 2, 1)
      || !fc_agent_v2_goto_candidate_precedes(2, 5, 2, 6)
      || fc_agent_v2_goto_candidate_precedes(2, 6, 2, 5)
      || fc_agent_v2_goto_candidate_precedes(2, 5, 2, 5)) {
    fprintf(stderr, "protocol 2 goto candidate ordering failed\n");
    return false;
  }
  if (!fc_agent_v2_unit_route_postcondition(
        100, true, 100, 42, 42, FC_AGENT_V2_CONTROLLER_NONE,
        true, true, false, true, true, true,
        77, 3, UINT64_C(0x1234), false, true,
        true, 100, 42, true, 77, 3, UINT64_C(0x1234), false, true)
      || !fc_agent_v2_unit_route_postcondition(
        100, true, 100, 42, 42, FC_AGENT_V2_CONTROLLER_NONE,
        true, true, false, true, true, true,
        77, 3, UINT64_C(0x1234), false, true,
        true, 100, 77, false, -1, 0, 0, false, false)
      || !fc_agent_v2_unit_route_postcondition(
        100, true, 100, 42, 42, FC_AGENT_V2_CONTROLLER_NONE,
        true, true, false, true, true, true,
        42, 5, UINT64_C(0x5678), true, true,
        true, 100, 42, true, -1, 5, UINT64_C(0x5678), true, true)
      || fc_agent_v2_unit_route_postcondition(
        100, true, 100, 42, 42, FC_AGENT_V2_CONTROLLER_NONE,
        true, true, false, true, true, true,
        77, 3, UINT64_C(0x1234), false, true,
        true, 100, 42, true, 77, 3, UINT64_C(0x1235), false, true)
      || fc_agent_v2_unit_route_postcondition(
        100, true, 100, 42, 42, FC_AGENT_V2_CONTROLLER_NONE,
        true, true, false, true, true, true,
        42, 5, UINT64_C(0x5678), true, true,
        true, 100, 42, false, -1, 0, 0, false, false)) {
    fprintf(stderr, "protocol 2 route postcondition failed\n");
    return false;
  }
  if (fc_agent_v2_unit_route_postcondition(
        100, true, 100, 42, 42, FC_AGENT_V2_CONTROLLER_NONE,
        true, true, false, true, true, true,
        42, 5, UINT64_C(0x5678), false, false,
        true, 100, 42, false, -1, 0, 0, false, false)) {
    fprintf(stderr, "protocol 2 route false-applied guard failed\n");
    return false;
  }
  if (!fc_agent_v2_unit_route_install_postcondition(
        100, true, 100, 42, 42, FC_AGENT_V2_CONTROLLER_NONE,
        true, true, false, true, true, true,
        77, 3, UINT64_C(0x1234), false, false, false, true)
      || fc_agent_v2_unit_route_install_postcondition(
        100, true, 100, 42, 42, FC_AGENT_V2_CONTROLLER_NONE,
        true, true, false, true, true, true,
        77, 3, UINT64_C(0x1234), false, false, false, false)
      || fc_agent_v2_unit_route_install_postcondition(
        100, true, 100, 42, 42, FC_AGENT_V2_CONTROLLER_NONE,
        true, true, false, true, true, true,
        77, 3, 0, false, false, false, true)
      || !fc_agent_v2_unit_route_install_postcondition(
        100, true, 100, 42, 42, FC_AGENT_V2_CONTROLLER_NONE,
        true, true, false, true, true, true,
        42, 1, UINT64_C(0x5678), false, false, true, true)
      || fc_agent_v2_unit_route_install_postcondition(
        100, true, 100, 42, 42, FC_AGENT_V2_CONTROLLER_NONE,
        true, true, false, true, true, true,
        42, 1, UINT64_C(0x5678), false, false, false, true)) {
    fprintf(stderr, "protocol 2 exact route installation proof failed\n");
    return false;
  }
  if (!fc_agent_v2_unit_route_shape_matches(
        AGENT_V2_ACTION_UNIT_GOTO, true,
        -1, -1, -1, -1, -1, -1)
      || fc_agent_v2_unit_route_shape_matches(
        AGENT_V2_ACTION_UNIT_GOTO, true,
        77, -1, -1, -1, -1, -1)
      || !fc_agent_v2_unit_route_shape_matches(
        AGENT_V2_ACTION_UNIT_GOTO_AND_PERFORM, false,
        77, -1, -1, -1, 77, -1)
      || fc_agent_v2_unit_route_shape_matches(
        AGENT_V2_ACTION_UNIT_GOTO_AND_PERFORM, true,
        77, -1, -1, -1, 77, -1)
      || !fc_agent_v2_unit_route_shape_matches(
        AGENT_V2_ACTION_UNIT_CONNECT_ROUTE, false,
        88, 4, -1, -1, 88, 4)
      || fc_agent_v2_unit_route_shape_matches(
        AGENT_V2_ACTION_UNIT_CONNECT_ROUTE, false,
        88, 4, -1, -1, 88, 5)) {
    fprintf(stderr, "protocol 2 route family shape proof failed\n");
    return false;
  }
  if (!fc_agent_v2_infrastructure_postcondition(100, 25, 75, true, 4, 4)
      || fc_agent_v2_infrastructure_postcondition(
           100, 25, 76, true, 4, 4)
      || fc_agent_v2_infrastructure_postcondition(
           100, 25, 75, false, 4, 4)
      || fc_agent_v2_infrastructure_postcondition(
           100, 25, 75, true, 4, 5)
      || fc_agent_v2_infrastructure_postcondition(
           20, 25, -5, true, 4, 4)) {
    fprintf(stderr, "protocol 2 infrastructure postcondition failed\n");
    return false;
  }
  if (!fc_agent_v2_hut_transition_postcondition(
        true, true, true, 100, true, 100, 77, 77)
      || !fc_agent_v2_hut_transition_postcondition(
           true, true, true, 100, false, 0, -1, 77)
      || fc_agent_v2_hut_transition_postcondition(
           true, true, false, 100, true, 100, 77, 77)
      || fc_agent_v2_hut_transition_postcondition(
           true, true, true, 100, true, 100, 42, 77)
      || !fc_agent_v2_conquer_extras_postcondition(
           true, -1, 3, 3, 100, true, 100, 77, 77)
      || !fc_agent_v2_conquer_extras_postcondition(
           true, 4, 3, 3, 100, false, 0, -1, 77)
      || fc_agent_v2_conquer_extras_postcondition(
           true, 4, 3, 4, 100, true, 100, 77, 77)
      || fc_agent_v2_conquer_extras_postcondition(
           true, 3, 3, 3, 100, true, 100, 77, 77)) {
    fprintf(stderr, "protocol 2 hut/extras postcondition failed\n");
    return false;
  }
  if (!fc_agent_v2_espionage_effect_postcondition(
        true, true, 200, true, 200, true, 200, true)
      || fc_agent_v2_espionage_effect_postcondition(
           true, true, 200, true, 200, true, 200, false)
      || fc_agent_v2_espionage_effect_postcondition(
           true, false, 200, true, 200, true, 200, true)
      || fc_agent_v2_espionage_effect_postcondition(
           true, true, 200, true, 200, true, 201, true)) {
    fprintf(stderr, "protocol 2 espionage postcondition failed\n");
    return false;
  }
  if (!fc_agent_v2_targeted_sabotage_postcondition(
        true, true, false, 200, true, 200, true, 200, false, true)
      || !fc_agent_v2_targeted_sabotage_postcondition(
           true, true, true, 200, true, 200, true, 200, true, false)
      || fc_agent_v2_targeted_sabotage_postcondition(
           true, false, false, 200, true, 200, true, 200, true, true)
      || fc_agent_v2_targeted_sabotage_postcondition(
           true, true, true, 200, true, 200, true, 200, true, true)
      || fc_agent_v2_targeted_sabotage_postcondition(
           true, true, false, 200, true, 200, true, 201, false, false)) {
    fprintf(stderr, "protocol 2 targeted sabotage postcondition failed\n");
    return false;
  }
  if (!fc_agent_v2_city_espionage_event_matches(
        true, true, true, true, 9, true, true, true,
        42, 42, 77, 77, 35, 35)
      || fc_agent_v2_city_espionage_event_matches(
           true, true, true, true, 0, true, true, true,
           42, 42, 77, 77, 35, 35)
      || fc_agent_v2_city_espionage_event_matches(
           true, true, true, true, 9, false, true, true,
           42, 42, 77, 77, 35, 35)
      || fc_agent_v2_city_espionage_event_matches(
           true, true, true, true, 9, true, false, true,
           42, 42, 77, 77, 35, 35)
      || fc_agent_v2_city_espionage_event_matches(
           true, true, true, true, 9, true, true, false,
           42, 42, 77, 77, 35, 35)
      || fc_agent_v2_city_espionage_event_matches(
           true, true, true, true, 9, true, true, true,
           41, 42, 77, 77, 35, 35)
      || fc_agent_v2_city_espionage_event_matches(
           true, true, true, true, 9, true, true, true,
           42, 42, 76, 77, 35, 35)
      || fc_agent_v2_city_espionage_event_matches(
           true, true, true, true, 9, true, true, true,
           42, 42, 77, 77, 34, 35)) {
    fprintf(stderr, "protocol 2 city espionage event binding failed\n");
    return false;
  }
  if (!fc_agent_v2_action_receipt_matches(
        true, true, true, true, true, true, true,
        42, 42, 252, 252, 100, 100, 77, 77, 35, 35, 1)
      || fc_agent_v2_action_receipt_matches(
           false, true, true, true, true, true, true,
           42, 42, 252, 252, 100, 100, 77, 77, 35, 35, 1)
      || fc_agent_v2_action_receipt_matches(
           true, false, true, true, true, true, true,
           42, 42, 252, 252, 100, 100, 77, 77, 35, 35, 1)
      || fc_agent_v2_action_receipt_matches(
           true, true, false, true, true, true, true,
           42, 42, 252, 252, 100, 100, 77, 77, 35, 35, 1)
      || fc_agent_v2_action_receipt_matches(
           true, true, true, false, true, true, true,
           42, 42, 252, 252, 100, 100, 77, 77, 35, 35, 1)
      || fc_agent_v2_action_receipt_matches(
           true, true, true, true, false, true, true,
           42, 42, 252, 252, 100, 100, 77, 77, 35, 35, 1)
      || fc_agent_v2_action_receipt_matches(
           true, true, true, true, true, false, true,
           42, 42, 252, 252, 100, 100, 77, 77, 35, 35, 1)
      || fc_agent_v2_action_receipt_matches(
           true, true, true, true, true, true, false,
           42, 42, 252, 252, 100, 100, 77, 77, 35, 35, 1)
      || fc_agent_v2_action_receipt_matches(
           true, true, true, true, true, true, true,
           0, 42, 252, 252, 100, 100, 77, 77, 35, 35, 1)
      || fc_agent_v2_action_receipt_matches(
           true, true, true, true, true, true, true,
           41, 42, 252, 252, 100, 100, 77, 77, 35, 35, 1)
      || fc_agent_v2_action_receipt_matches(
           true, true, true, true, true, true, true,
           42, 42, 253, 252, 100, 100, 77, 77, 35, 35, 1)
      || fc_agent_v2_action_receipt_matches(
           true, true, true, true, true, true, true,
           42, 42, 252, 252, 101, 100, 77, 77, 35, 35, 1)
      || fc_agent_v2_action_receipt_matches(
           true, true, true, true, true, true, true,
           42, 42, 252, 252, 100, 100, 78, 77, 35, 35, 1)
      || fc_agent_v2_action_receipt_matches(
           true, true, true, true, true, true, true,
           42, 42, 252, 252, 100, 100, 77, 77, 34, 35, 1)
      || fc_agent_v2_action_receipt_matches(
           true, true, true, true, true, true, true,
           42, 42, 252, 252, 100, 100, 77, 77, 35, 35, 0)) {
    fprintf(stderr, "protocol 2 structured action receipt binding failed\n");
    return false;
  }
  if (!fc_agent_v2_poison_city_postcondition(
        true, true, 200, true, 200, 5, true, true, 200, 4)
      || !fc_agent_v2_poison_city_postcondition(
           true, true, 200, true, 200, 1, false, false, 0, -1)
      || fc_agent_v2_poison_city_postcondition(
           true, false, 200, true, 200, 5, true, true, 200, 4)
      || fc_agent_v2_poison_city_postcondition(
           true, true, 200, true, 201, 5, true, true, 200, 4)
      || fc_agent_v2_poison_city_postcondition(
           true, true, 200, true, 200, 5, true, true, 200, 5)
      || fc_agent_v2_poison_city_postcondition(
           true, true, 200, true, 200, 2, false, false, 0, -1)
      || fc_agent_v2_poison_city_postcondition(
           true, true, 200, true, 200, 5, true, false, 201, 4)) {
    fprintf(stderr, "protocol 2 poison-city receipt failed\n");
    return false;
  }
  if (!fc_agent_v2_sabotage_city_postcondition(
        true, true, 200, true, 200, true, true, 200, false)
      || !fc_agent_v2_sabotage_city_postcondition(
           true, true, 200, true, 200, true, true, 200, true)
      || fc_agent_v2_sabotage_city_postcondition(
           true, false, 200, true, 200, true, true, 200, true)
      || fc_agent_v2_sabotage_city_postcondition(
           true, true, 200, true, 201, true, true, 200, false)
      || fc_agent_v2_sabotage_city_postcondition(
           true, true, 200, true, 200, false, false, 0, false)
      || fc_agent_v2_sabotage_city_postcondition(
           true, true, 200, true, 200, true, false, 201, true)) {
    fprintf(stderr, "protocol 2 sabotage-city receipt failed\n");
    return false;
  }
  if (!fc_agent_v2_combat_observer_matches(
        true, true, true, true, true, 42, 42, 100, 100, 200, true)
      || fc_agent_v2_combat_observer_matches(
           false, true, true, true, true, 42, 42, 100, 100, 200, true)
      || fc_agent_v2_combat_observer_matches(
           true, false, true, true, true, 42, 42, 100, 100, 200, true)
      || fc_agent_v2_combat_observer_matches(
           true, true, false, true, true, 42, 42, 100, 100, 200, true)
      || fc_agent_v2_combat_observer_matches(
           true, true, true, false, true, 42, 42, 100, 100, 200, true)
      || fc_agent_v2_combat_observer_matches(
           true, true, true, true, true, 0, 0, 100, 100, 200, true)
      || fc_agent_v2_combat_observer_matches(
           true, true, true, true, true, 41, 42, 100, 100, 200, true)
      || fc_agent_v2_combat_observer_matches(
           true, true, true, true, true, 42, 42, 101, 100, 200, true)
      || fc_agent_v2_combat_observer_matches(
           true, true, true, true, true, 42, 42, 100, 100, 100, true)
      || fc_agent_v2_combat_observer_matches(
           true, true, true, true, false, 42, 42, 100, 100, 200, true)
      || fc_agent_v2_combat_observer_matches(
           true, true, true, true, true, 42, 42, 100, 100, 200, false)) {
    fprintf(stderr, "protocol 2 combat observer binding failed\n");
    return false;
  }
  if (!fc_agent_v2_spy_attack_postcondition(
        true, 100, true, 100, true, false,
        false, false, UINT64_C(0x1234), UINT64_C(0x1234))
      || !fc_agent_v2_spy_attack_postcondition(
           true, 100, true, 100, false, true,
           true, true, UINT64_C(0x1234), UINT64_C(0x5678))
      || fc_agent_v2_spy_attack_postcondition(
           true, 100, true, 100, true, true,
           false, false, UINT64_C(0x1234), UINT64_C(0x5678))
      || fc_agent_v2_spy_attack_postcondition(
           true, 100, true, 100, true, false,
           true, true, UINT64_C(0x1234), UINT64_C(0x1234))
      || fc_agent_v2_spy_attack_postcondition(
           true, 100, true, 100, false, true,
           true, true, UINT64_C(0x1234), UINT64_C(0x1234))
      || fc_agent_v2_spy_attack_postcondition(
           true, 100, true, 100, false, true,
           true, false, UINT64_C(0x1234), UINT64_C(0x5678))) {
    fprintf(stderr, "protocol 2 spy-attack receipt failed\n");
    return false;
  }
  if (!fc_agent_v2_sabotage_unit_postcondition(
        true, true, 200, true, 200, 10, false, false, -1)
      || !fc_agent_v2_sabotage_unit_postcondition(
           true, true, 200, true, 200, 10, true, true, 5)
      || fc_agent_v2_sabotage_unit_postcondition(
           true, false, 200, true, 200, 10, false, false, -1)
      || fc_agent_v2_sabotage_unit_postcondition(
           true, true, 200, true, 201, 10, false, false, -1)
      || fc_agent_v2_sabotage_unit_postcondition(
           true, true, 200, true, 200, 10, true, false, 5)
      || fc_agent_v2_sabotage_unit_postcondition(
           true, true, 200, true, 200, 10, true, true, 10)) {
    fprintf(stderr, "protocol 2 sabotage-unit receipt failed\n");
    return false;
  }
  if (!fc_agent_v2_nuke_observer_matches(
        true, true, true, true, 42, 42, 77, 77)
      || fc_agent_v2_nuke_observer_matches(
           true, true, true, true, 41, 42, 77, 77)
      || fc_agent_v2_nuke_observer_matches(
           true, true, true, true, 42, 42, 76, 77)
      || fc_agent_v2_nuke_observer_matches(
           true, true, false, true, 42, 42, 77, 77)
      || fc_agent_v2_nuke_observer_matches(
           true, true, true, false, 42, 42, 77, 77)
      || !fc_agent_v2_nuke_stack_binding_matches(
           true, UINT64_C(0x1234), UINT64_C(0x1234))
      || fc_agent_v2_nuke_stack_binding_matches(
           true, UINT64_C(0x1234), UINT64_C(0x5678))
      || !fc_agent_v2_nuke_stack_binding_matches(
           false, 0, UINT64_C(0x5678))
      || fc_agent_v2_nuke_stack_binding_matches(
           false, UINT64_C(0x1234), UINT64_C(0x1234))
      || !fc_agent_v2_nuke_postcondition(
           true, true, 100, true, 100, false)
      || fc_agent_v2_nuke_postcondition(
           true, false, 100, true, 100, false)
      || fc_agent_v2_nuke_postcondition(
           true, true, 100, true, 100, true)
      || fc_agent_v2_nuke_postcondition(
           true, true, 100, true, 99, false)
      || fc_agent_v2_nuke_postcondition(
           false, true, 100, true, 100, false)) {
    fprintf(stderr, "protocol 2 nuke receipt binding failed\n");
    return false;
  }
  if (!fc_agent_v2_city_target_distance_candidate(3, false, 3)
      || fc_agent_v2_city_target_distance_candidate(4, false, 3)
      || !fc_agent_v2_city_target_distance_candidate(4000, true, -1)
      || fc_agent_v2_city_target_distance_candidate(-1, true, -1)
      || fc_agent_v2_city_target_distance_candidate(0, false, -1)) {
    fprintf(stderr, "protocol 2 city-target scan bound failed\n");
    return false;
  }
  if (fc_agent_v2_capture_group_terminal(
        true, true, true, true, true, true)
        != FC_AGENT_V2_TERMINAL_APPLIED
      || fc_agent_v2_capture_group_terminal(
           true, true, true, true, true, false)
         != FC_AGENT_V2_TERMINAL_PROCESSING_BOUNDARY_MISMATCH
      || fc_agent_v2_capture_group_terminal(
           true, true, true, false, true, true)
         != FC_AGENT_V2_TERMINAL_PROCESSING_BOUNDARY_MISMATCH
      || fc_agent_v2_capture_group_terminal(
           false, true, true, true, true, true)
         != FC_AGENT_V2_TERMINAL_SEAT_EPOCH_CHANGED) {
    fprintf(stderr, "protocol 2 automation group terminal failed\n");
    return false;
  }
  if (fc_agent_v2_automation_terminal(
        FC_AGENT_V2_AUTOMATION_WORK,
        true, true, true, false, true, true, true, true)
        != FC_AGENT_V2_TERMINAL_APPLIED
      || fc_agent_v2_automation_terminal(
           FC_AGENT_V2_AUTOMATION_EXPLORE,
           true, true, true, false, true, true, true, false)
         != FC_AGENT_V2_TERMINAL_PROCESSING_BOUNDARY_MISMATCH
      || fc_agent_v2_automation_terminal(
           FC_AGENT_V2_AUTOMATION_CANCEL,
           true, true, true, true, true, false, true, false)
         != FC_AGENT_V2_TERMINAL_PROCESSING_BOUNDARY_MISMATCH
      || fc_agent_v2_automation_terminal(
           FC_AGENT_V2_AUTOMATION_CANCEL,
           true, true, true, false, true, false, true, true)
         != FC_AGENT_V2_TERMINAL_PROCESSING_BOUNDARY_MISMATCH
      || fc_agent_v2_automation_terminal(
           FC_AGENT_V2_AUTOMATION_WORK,
           false, true, true, false, true, true, true, true)
         != FC_AGENT_V2_TERMINAL_SEAT_EPOCH_CHANGED) {
    fprintf(stderr, "protocol 2 automation ambiguity failed closed\n");
    return false;
  }
  if (fc_agent_v2_automation_terminal(
        FC_AGENT_V2_AUTOMATION_WORK,
        true, true, true, false, true, true, true, false)
        != FC_AGENT_V2_TERMINAL_POSTCONDITION_NOT_MET
      || fc_agent_v2_automation_terminal(
           FC_AGENT_V2_AUTOMATION_WORK,
           true, true, true, false, true, true, false, false)
         != FC_AGENT_V2_TERMINAL_PROCESSING_BOUNDARY_MISMATCH
      || fc_agent_v2_automation_terminal(
           FC_AGENT_V2_AUTOMATION_WORK,
           true, true, true, false, true, false, true, false)
         != FC_AGENT_V2_TERMINAL_PROCESSING_BOUNDARY_MISMATCH) {
    fprintf(stderr, "protocol 2 auto-work exact rejection failed\n");
    return false;
  }
  if (fc_agent_v2_consuming_city_terminal(
        true, true, true, true, true, false, true, true)
        != FC_AGENT_V2_TERMINAL_APPLIED
      || fc_agent_v2_consuming_city_terminal(
           true, true, true, true, true, true, false, false)
         != FC_AGENT_V2_TERMINAL_POSTCONDITION_NOT_MET
      || fc_agent_v2_consuming_city_terminal(
           true, true, true, true, true, false, true, false)
         != FC_AGENT_V2_TERMINAL_PROCESSING_BOUNDARY_MISMATCH
      || fc_agent_v2_consuming_city_terminal(
           true, true, true, true, true, false, false, false)
         != FC_AGENT_V2_TERMINAL_PROCESSING_BOUNDARY_MISMATCH
      || fc_agent_v2_consuming_city_terminal(
           true, true, true, true, true, true, false, true)
         != FC_AGENT_V2_TERMINAL_PROCESSING_BOUNDARY_MISMATCH
      || fc_agent_v2_consuming_city_terminal(
           true, true, true, false, true, false, true, true)
         != FC_AGENT_V2_TERMINAL_PROCESSING_BOUNDARY_MISMATCH
      || fc_agent_v2_consuming_city_terminal(
           false, true, true, true, true, false, true, true)
         != FC_AGENT_V2_TERMINAL_SEAT_EPOCH_CHANGED) {
    fprintf(stderr, "protocol 2 consuming-city terminal proof failed\n");
    return false;
  }
  fc_agent_v2_capture_terminal(&terminal, true, true, true, true);
  terminal = fc_agent_v2_terminal_after_epoch_change(terminal);
  if (terminal != FC_AGENT_V2_TERMINAL_APPLIED) {
    fprintf(stderr,
            "protocol 2 applied result did not survive epoch transition\n");
    return false;
  }
  terminal = FC_AGENT_V2_TERMINAL_NONE;
  fc_agent_v2_capture_terminal(&terminal, true, true, true, false);
  terminal = fc_agent_v2_terminal_after_epoch_change(terminal);
  if (terminal != FC_AGENT_V2_TERMINAL_POSTCONDITION_NOT_MET) {
    fprintf(stderr,
            "protocol 2 rejected result did not survive epoch transition\n");
    return false;
  }
  terminal = fc_agent_v2_terminal_after_epoch_change(
    FC_AGENT_V2_TERMINAL_NONE);
  if (terminal != FC_AGENT_V2_TERMINAL_SEAT_EPOCH_CHANGED) {
    fprintf(stderr, "protocol 2 unresolved epoch transition was not fatal\n");
    return false;
  }
  terminal = FC_AGENT_V2_TERMINAL_NONE;
  fc_agent_v2_capture_terminal(&terminal, true, false, false, false);
  if (terminal != FC_AGENT_V2_TERMINAL_PROCESSING_BOUNDARY_MISMATCH) {
    fprintf(stderr, "protocol 2 processing-boundary mismatch failed\n");
    return false;
  }
  terminal = FC_AGENT_V2_TERMINAL_NONE;
  fc_agent_v2_capture_terminal(&terminal, false, true, true, true);
  if (terminal != FC_AGENT_V2_TERMINAL_SEAT_EPOCH_CHANGED) {
    fprintf(stderr, "protocol 2 seat-epoch boundary failed closed\n");
    return false;
  }
  for (i = 0; i < 5000; i++) {
    uint64_t incarnation = fc_agent_v2_take_incarnation(&next_incarnation);

    if (incarnation == 0 || incarnation <= previous_incarnation) {
      fprintf(stderr, "protocol 2 incarnation nonreuse failed\n");
      return false;
    }
    previous_incarnation = incarnation;
  }
  if (!fc_agent_v2_percent_encode("Attack", attack, sizeof(attack))
      || !fc_agent_v2_percent_encode("Suicide Attack", suicide,
                                     sizeof(suicide))
      || strcmp(attack, suicide) == 0) {
    fprintf(stderr, "protocol 2 action variant descriptors collapsed\n");
    return false;
  }
  research_digest = fc_agent_v2_research_choices_digest_init();
  if (!fc_agent_v2_research_choices_digest_add(
        &research_digest, 3, "Alphabet", "known", false, false)
      || !fc_agent_v2_research_choices_digest_add(
        &research_digest, 4, "Writing", "available", true, true)
      || !fc_agent_v2_research_choices_digest_add(
        &research_digest, 5, "Pottery", "reachable", false, true)
      || !fc_agent_v2_research_choices_digest_add(
        &research_digest, 6, "Bronze Working", "available", true, true)
      || !fc_agent_v2_research_choices_digest_add(
        &research_digest, 1000, "Unset", "unset", false, true)
      || research_digest != UINT64_C(0xda5a057e14a5995d)
      || fc_agent_v2_research_choices_digest_add(
        &research_digest, -1, "invalid", "known", false, false)) {
    fprintf(stderr, "protocol 2 research catalog digest failed\n");
    return false;
  }
  if (!fc_agent_v2_government_change_observable(12, 11, true)
      || !fc_agent_v2_government_change_observable(-1, 11, false)
      || fc_agent_v2_government_change_observable(-1, 11, true)
      || fc_agent_v2_government_change_observable(11, 11, false)
      || !fc_agent_v2_revolution_available(true, true, false, false)
      || fc_agent_v2_revolution_available(false, true, false, false)
      || fc_agent_v2_revolution_available(true, false, false, false)
      || fc_agent_v2_revolution_available(true, true, true, true)) {
    fprintf(stderr, "protocol 2 government availability failed\n");
    return false;
  }
  if (fc_agent_v2_government_status(2, -1, 0, -1, 7)
        != FC_AGENT_V2_GOV_STABLE
      || fc_agent_v2_government_status(0, 0, 0, 9, 7)
         != FC_AGENT_V2_GOV_ANARCHY
      || fc_agent_v2_government_status(0, 3, 0, 9, 7)
         != FC_AGENT_V2_GOV_ANARCHY_TARGETED
      || fc_agent_v2_government_status(0, 0, 0, 7, 7)
         != FC_AGENT_V2_GOV_CHOICE_REQUIRED
      || fc_agent_v2_government_status(0, 3, 0, 7, 7)
         != FC_AGENT_V2_GOV_ENACTMENT_PENDING
      || strcmp(fc_agent_v2_government_status_name(
                  FC_AGENT_V2_GOV_ENACTMENT_PENDING),
                "enactment_pending") != 0) {
    fprintf(stderr, "protocol 2 government status classification failed\n");
    return false;
  }
  if (!fc_agent_v2_government_postcondition(
        FC_AGENT_V2_GOV_REVOLUTION, 2, -1, -1, 0, 0, 10, 0, 0)
      || fc_agent_v2_government_postcondition(
        FC_AGENT_V2_GOV_REVOLUTION, 0, 0, 9, 0, 0, 10, 0, 0)
      || !fc_agent_v2_government_postcondition(
        FC_AGENT_V2_GOV_CHANGE, 2, -1, -1, 0, 3, 10, 0, 3)
      || !fc_agent_v2_government_postcondition(
        FC_AGENT_V2_GOV_CHANGE, 2, -1, -1, 3, -1, -1, 0, 3)
      || fc_agent_v2_government_postcondition(
        FC_AGENT_V2_GOV_CHANGE, 2, -1, -1, 0, 4, 10, 0, 3)) {
    fprintf(stderr, "protocol 2 government postcondition failed\n");
    return false;
  }
  if (!fc_agent_v2_probability_candidate_preferred(
        0, 100, 100, 9, 1, 180, 200, 1)
      || !fc_agent_v2_probability_candidate_preferred(
        1, 190, 200, 9, 1, 180, 200, 1)
      || !fc_agent_v2_probability_candidate_preferred(
        1, 180, 190, 9, 1, 180, 200, 1)
      || !fc_agent_v2_probability_candidate_preferred(
        1, 180, 200, 1, 1, 180, 200, 9)
      || fc_agent_v2_probability_candidate_preferred(
        2, 200, 200, 1, 1, 0, 200, 9)
      || fc_agent_v2_probability_candidate_preferred(
        1, 180, 200, 9, 1, 180, 200, 1)) {
    fprintf(stderr, "protocol 2 probability preference failed\n");
    return false;
  }
  if (!fc_agent_v2_unit_lifetime_matches(7, 7)
      || fc_agent_v2_unit_lifetime_matches(0, 7)
      || fc_agent_v2_unit_lifetime_matches(7, 0)
      || fc_agent_v2_unit_lifetime_matches(7, 8)) {
    fprintf(stderr, "protocol 2 unit lifetime match failed\n");
    return false;
  }
  if (!fc_agent_v2_city_lifetime_matches(11, 11)
      || fc_agent_v2_city_lifetime_matches(0, 11)
      || fc_agent_v2_city_lifetime_matches(11, 0)
      || fc_agent_v2_city_lifetime_matches(11, 12)) {
    fprintf(stderr, "protocol 2 city lifetime match failed\n");
    return false;
  }
  if (!fc_agent_v2_unit_activity_postcondition(
        7, true, 7, true, 7, 4, true, 4, -1)
      || !fc_agent_v2_unit_activity_postcondition(
        7, true, 7, true, 7, 5, true, 4, 5)
      || fc_agent_v2_unit_activity_postcondition(
        7, false, 7, true, 7, 4, true, 4, -1)
      || fc_agent_v2_unit_activity_postcondition(
        7, true, 8, true, 7, 4, true, 4, -1)
      || fc_agent_v2_unit_activity_postcondition(
        7, true, 7, true, 8, 4, true, 4, -1)
      || fc_agent_v2_unit_activity_postcondition(
        7, true, 7, true, 7, 4, false, 4, -1)
      || fc_agent_v2_unit_activity_postcondition(
        7, true, 7, false, 7, 4, true, 4, -1)) {
    fprintf(stderr, "protocol 2 unit activity postcondition failed\n");
    return false;
  }
  if (!fc_agent_v2_unit_conversion_postcondition(
        7, true, 7, true, 7, 6, true, 6, 12, 13, 12)
      || !fc_agent_v2_unit_conversion_postcondition(
        7, true, 7, true, 7, 0, false, 6, 12, 13, 13)
      || fc_agent_v2_unit_conversion_postcondition(
        7, false, 7, true, 7, 6, true, 6, 12, 13, 12)
      || fc_agent_v2_unit_conversion_postcondition(
        7, true, 8, true, 7, 6, true, 6, 12, 13, 12)
      || fc_agent_v2_unit_conversion_postcondition(
        7, true, 7, true, 8, 6, true, 6, 12, 13, 12)
      || fc_agent_v2_unit_conversion_postcondition(
        7, true, 7, true, 7, 6, false, 6, 12, 13, 12)
      || fc_agent_v2_unit_conversion_postcondition(
        7, true, 7, true, 7, 0, true, 6, 13, 13, 13)) {
    fprintf(stderr, "protocol 2 unit conversion postcondition failed\n");
    return false;
  }
  if (!fc_agent_v2_unit_consumed_postcondition(7, true, 7, false)
      || fc_agent_v2_unit_consumed_postcondition(0, true, 0, false)
      || fc_agent_v2_unit_consumed_postcondition(7, false, 7, false)
      || fc_agent_v2_unit_consumed_postcondition(7, true, 8, false)
      || fc_agent_v2_unit_consumed_postcondition(7, true, 7, true)
      || !fc_agent_v2_unit_home_cleared_postcondition(
        7, true, 7, true, 7, 20, 0)
      || fc_agent_v2_unit_home_cleared_postcondition(
        7, false, 7, true, 7, 20, 0)
      || fc_agent_v2_unit_home_cleared_postcondition(
        7, true, 8, true, 7, 20, 0)
      || fc_agent_v2_unit_home_cleared_postcondition(
        7, true, 7, true, 8, 20, 0)
      || fc_agent_v2_unit_home_cleared_postcondition(
        7, true, 7, true, 7, 0, 0)
      || fc_agent_v2_unit_home_cleared_postcondition(
        7, true, 7, true, 7, 20, 21)) {
    fprintf(stderr, "protocol 2 unit removal/home postcondition failed\n");
    return false;
  }
  if (!fc_agent_v2_unit_upgrade_postcondition(
        7, true, 7, 12, true, 7, 13, 13)
      || fc_agent_v2_unit_upgrade_postcondition(
        7, true, 8, 12, true, 7, 13, 13)
      || fc_agent_v2_unit_upgrade_postcondition(
        7, true, 7, 12, true, 8, 13, 13)
      || fc_agent_v2_unit_upgrade_postcondition(
        7, true, 7, 13, true, 7, 13, 13)
      || fc_agent_v2_unit_upgrade_postcondition(
        7, true, 7, 12, false, 0, 13, 13)
      || fc_agent_v2_unit_upgrade_postcondition(
        7, true, 7, 12, true, 7, 13, 12)) {
    fprintf(stderr, "protocol 2 unit upgrade postcondition failed\n");
    return false;
  }
  if (!fc_agent_v2_unit_rehome_postcondition(
        7, true, 7, 20, true, 7, 21, 21)
      || fc_agent_v2_unit_rehome_postcondition(
        7, true, 8, 20, true, 7, 21, 21)
      || fc_agent_v2_unit_rehome_postcondition(
        7, true, 7, 20, true, 8, 21, 21)
      || fc_agent_v2_unit_rehome_postcondition(
        7, true, 7, 21, true, 7, 21, 21)
      || fc_agent_v2_unit_rehome_postcondition(
        7, true, 7, 20, false, 0, 21, 21)
      || fc_agent_v2_unit_rehome_postcondition(
        7, true, 7, 20, true, 7, 21, 22)) {
    fprintf(stderr, "protocol 2 unit rehome postcondition failed\n");
    return false;
  }
  if (!fc_agent_v2_consumed_city_postcondition(
        7, true, 7, false, 11, true, 11, true, 11)
      || fc_agent_v2_consumed_city_postcondition(
        7, true, 7, true, 11, true, 11, true, 11)
      || fc_agent_v2_consumed_city_postcondition(
        7, true, 7, false, 11, true, 11, true, 12)
      || !fc_agent_v2_join_city_postcondition(
        7, true, 7, false, 11, true, 11, 2, 1, true, 11, 3)
      || fc_agent_v2_join_city_postcondition(
        7, true, 7, false, 11, true, 11, 2, 0, true, 11, 2)
      || fc_agent_v2_join_city_postcondition(
        7, true, 7, false, 11, true, 11, 2, 1, true, 11, 4)) {
    fprintf(stderr, "protocol 2 consumed-city/join postcondition failed\n");
    return false;
  }
  if (!fc_agent_v2_help_wonder_postcondition(
        7, true, 7, false, 11, true, 11, true, 10, 40,
        true, 11, 50)
      || !fc_agent_v2_help_wonder_postcondition(
        7, true, 7, false, 11, true, 11, false, -1, 40,
        true, 11, -1)
      || fc_agent_v2_help_wonder_postcondition(
        7, true, 7, false, 11, true, 11, true, 10, 40,
        true, 11, 49)
      || fc_agent_v2_help_wonder_postcondition(
        7, true, 7, true, 11, true, 11, false, -1, 40,
        true, 11, -1)
      || fc_agent_v2_help_wonder_postcondition(
        7, true, 7, false, 11, true, 11, false, -1, 40,
        true, 12, -1)) {
    fprintf(stderr, "protocol 2 help-wonder postcondition failed\n");
    return false;
  }
  if (!fc_agent_v2_disband_recover_postcondition(
        7, true, 7, false, 11, true, 11, true,
        true, 10, 40, true, 11, true, 50)
      || !fc_agent_v2_disband_recover_postcondition(
           7, true, 7, false, 11, true, 11, true,
           false, -1, 40, true, 11, false, -1)
      || fc_agent_v2_disband_recover_postcondition(
           7, true, 7, false, 11, true, 11, false,
           true, 10, 40, true, 11, true, 50)
      || fc_agent_v2_disband_recover_postcondition(
           7, true, 7, true, 11, true, 11, true,
           true, 10, 40, true, 11, true, 50)
      || fc_agent_v2_disband_recover_postcondition(
           7, true, 7, false, 11, true, 11, true,
           true, 10, 40, true, 12, true, 50)
      || fc_agent_v2_disband_recover_postcondition(
           7, true, 7, false, 11, true, 11, true,
           true, 10, 40, true, 11, true, 49)
      || fc_agent_v2_disband_recover_postcondition(
           7, true, 7, false, 11, true, 11, true,
           false, -1, 0, true, 11, false, -1)
      || fc_agent_v2_disband_recover_postcondition(
           7, true, 7, false, 11, true, 11, true,
           true, 10, 40, true, 11, false, 50)) {
    fprintf(stderr, "protocol 2 recover-disband postcondition failed\n");
    return false;
  }
  if (!fc_agent_v2_marketplace_postcondition(
        7, true, 7, false,
        11, true, 11, true, 11,
        12, true, 12, true, 12)
      || fc_agent_v2_marketplace_postcondition(
        7, true, 7, true,
        11, true, 11, true, 11,
        12, true, 12, true, 12)
      || fc_agent_v2_marketplace_postcondition(
        7, true, 7, false,
        11, true, 11, true, 13,
        12, true, 12, true, 12)
      || fc_agent_v2_marketplace_postcondition(
        7, true, 7, false,
        11, true, 11, true, 11,
        12, true, 12, true, 13)) {
    fprintf(stderr, "protocol 2 marketplace postcondition failed\n");
    return false;
  }
  if (!fc_agent_v2_trade_route_postcondition(
        7, true, 7, false,
        11, true, 11, true, 11,
        12, true, 12, true, 12, false, true)
      || fc_agent_v2_trade_route_postcondition(
        7, true, 7, true,
        11, true, 11, true, 11,
        12, true, 12, true, 12, false, true)
      || fc_agent_v2_trade_route_postcondition(
        7, true, 7, false,
        11, true, 11, true, 13,
        12, true, 12, true, 12, false, true)
      || fc_agent_v2_trade_route_postcondition(
        7, true, 7, false,
        11, true, 11, true, 11,
        12, true, 12, true, 12, true, true)
      || fc_agent_v2_trade_route_postcondition(
        7, true, 7, false,
        11, true, 11, true, 11,
        12, true, 12, true, 12, false, false)) {
    fprintf(stderr, "protocol 2 trade-route postcondition failed\n");
    return false;
  }
  if (!fc_agent_v2_unit_relocation_postcondition(
        7, true, 7, 5, true, 7, 19, 19, false, false, false)
      || !fc_agent_v2_unit_relocation_postcondition(
        7, true, 7, 5, true, 7, 19, 19, true, false, true)
      || fc_agent_v2_unit_relocation_postcondition(
        7, true, 7, 5, false, 0, -1, 19, false, false, false)
      || fc_agent_v2_unit_relocation_postcondition(
        7, true, 8, 5, true, 7, 19, 19, false, false, false)
      || fc_agent_v2_unit_relocation_postcondition(
        7, true, 7, 5, true, 8, 19, 19, false, false, false)
      || fc_agent_v2_unit_relocation_postcondition(
        7, true, 7, 5, true, 7, 18, 19, false, false, false)
      || fc_agent_v2_unit_relocation_postcondition(
        7, true, 7, 19, true, 7, 19, 19, false, false, false)
      || fc_agent_v2_unit_relocation_postcondition(
        7, true, 7, 5, true, 7, 19, 19, true, true, true)
      || fc_agent_v2_unit_relocation_postcondition(
        7, true, 7, 5, true, 7, 19, 19, true, false, false)) {
    fprintf(stderr, "protocol 2 unit relocation postcondition failed\n");
    return false;
  }
  if (!fc_agent_v2_paradrop_enter_conquer_postcondition(
        true, 7, true, 7, 5, false, true, 7, 19, true, 19, 3,
        -1, -1, -1, -1, -1, -1, false, false)
      || !fc_agent_v2_paradrop_enter_conquer_postcondition(
           true, 7, true, 7, 5, false, true, 7, 19, true, 19, 3,
           22, 4, 22, 3, 4, 3, true, true)
      || fc_agent_v2_paradrop_enter_conquer_postcondition(
           true, 7, true, 7, 5, false, false, 0, -1, false, 19, 3,
           22, 4, 22, 3, 4, 3, true, true)
      || fc_agent_v2_paradrop_enter_conquer_postcondition(
           true, 7, true, 7, 5, false, true, 7, 19, true, 19, 3,
           22, 4, 22, 4, 4, 3, true, true)
      || fc_agent_v2_paradrop_enter_conquer_postcondition(
           true, 7, true, 7, 5, false, true, 7, 19, true, 19, 3,
           22, 4, 22, 3, 4, 4, true, true)
      || fc_agent_v2_paradrop_enter_conquer_postcondition(
           true, 7, true, 7, 5, false, true, 7, 19, true, 19, 3,
           22, 4, 22, 3, 4, 3, true, false)) {
    fprintf(stderr,
            "protocol 2 paradrop-enter-conquer postcondition failed\n");
    return false;
  }
  if (!fc_agent_v2_transport_occupancy_exact(false, 0, 2, false)
      || fc_agent_v2_transport_occupancy_exact(true, 1, 2, false)
      || !fc_agent_v2_transport_occupancy_exact(true, 2, 2, false)
      || !fc_agent_v2_transport_occupancy_exact(true, 1, 2, true)
      || fc_agent_v2_transport_occupancy_exact(true, 0, 2, false)
      || fc_agent_v2_transport_occupancy_exact(false, 1, 2, false)
      || fc_agent_v2_transport_occupancy_exact(true, 3, 2, true)
      || fc_agent_v2_transport_occupancy_exact(false, -1, 2, true)
      || !fc_agent_v2_transport_postcondition(
        FC_AGENT_V2_TRANSPORT_BOARD,
        7, true, 7, 5, 8, true, 8, 5, 0, false, 0, -1, true,
        true, 7, 5, true, 8, 5, false, 0, -1, true, false, -1)
      || !fc_agent_v2_transport_postcondition(
        FC_AGENT_V2_TRANSPORT_EMBARK,
        7, true, 7, 4, 8, true, 8, 5, 0, false, 0, -1, true,
        true, 7, 5, true, 8, 5, false, 0, -1, true, false, -1)
      || !fc_agent_v2_transport_postcondition(
        FC_AGENT_V2_TRANSPORT_LOAD,
        7, true, 7, 5, 8, true, 8, 5, 0, false, 0, -1, true,
        true, 7, 5, true, 8, 5, false, 0, -1, true, false, -1)
      || !fc_agent_v2_transport_postcondition(
        FC_AGENT_V2_TRANSPORT_DEBOARD,
        7, true, 7, 5, 8, true, 8, 5, 8, true, 8, 5, true,
        true, 7, 5, true, 8, 5, true, 8, 5, false, true, -1)
      || !fc_agent_v2_transport_postcondition(
        FC_AGENT_V2_TRANSPORT_UNLOAD,
        7, true, 7, 5, 8, true, 8, 5, 7, true, 7, 5, true,
        true, 7, 5, true, 8, 5, true, 7, 5, false, true, -1)
      || !fc_agent_v2_transport_postcondition(
        FC_AGENT_V2_TRANSPORT_DISEMBARK,
        7, true, 7, 5, 0, false, 0, -1, 8, true, 8, 5, true,
        true, 7, 6, false, 0, -1, true, 8, 5, false, true, 6)) {
    fprintf(stderr, "protocol 2 transport positive postconditions failed\n");
    return false;
  }
  if (fc_agent_v2_transport_postcondition(
        FC_AGENT_V2_TRANSPORT_BOARD,
        7, true, 7, 5, 8, true, 8, 5, 0, false, 0, -1, false,
        true, 7, 5, true, 8, 5, false, 0, -1, true, false, -1)
      || fc_agent_v2_transport_postcondition(
        FC_AGENT_V2_TRANSPORT_EMBARK,
        7, true, 7, 4, 8, true, 8, 5, 0, false, 0, -1, true,
        true, 7, 5, true, 9, 5, false, 0, -1, true, false, -1)
      || fc_agent_v2_transport_postcondition(
        FC_AGENT_V2_TRANSPORT_LOAD,
        7, true, 7, 5, 8, true, 8, 5, 0, false, 0, -1, true,
        true, 9, 5, true, 8, 5, false, 0, -1, true, false, -1)
      || fc_agent_v2_transport_postcondition(
        FC_AGENT_V2_TRANSPORT_DEBOARD,
        7, true, 7, 5, 8, true, 8, 5, 8, true, 8, 5, true,
        true, 7, 5, true, 8, 5, true, 9, 5, false, true, -1)
      || fc_agent_v2_transport_postcondition(
        FC_AGENT_V2_TRANSPORT_UNLOAD,
        7, true, 7, 5, 8, true, 8, 5, 7, true, 7, 5, true,
        true, 7, 5, true, 8, 5, true, 7, 5, true, true, -1)
      || fc_agent_v2_transport_postcondition(
        FC_AGENT_V2_TRANSPORT_DISEMBARK,
        7, true, 7, 5, 0, false, 0, -1, 8, true, 8, 5, true,
        true, 7, 6, false, 0, -1, true, 8, 5, false, true, 7)) {
    fprintf(stderr, "protocol 2 transport negative postconditions failed\n");
    return false;
  }
  return true;
}

static pid_t spawn_child(char *const child_argv[], int child_fd, int peer_fd)
{
  pid_t child = fork();

  if (child != 0) {
    return child;
  }

  if (peer_fd >= 0) {
    close(peer_fd);
  }
  if (child_fd >= 0 && child_fd != TEST_CHILD_FD) {
    if (dup2(child_fd, TEST_CHILD_FD) < 0) {
      _exit(126);
    }
    close(child_fd);
  }
  if (child_fd >= 0) {
    int descriptor_flags = fcntl(TEST_CHILD_FD, F_GETFD, 0);

    if (descriptor_flags < 0
        || fcntl(TEST_CHILD_FD, F_SETFD,
                 descriptor_flags & ~FD_CLOEXEC) < 0) {
      _exit(126);
    }
  }

  {
    int null_fd = open("/dev/null", O_RDWR);

    if (null_fd >= 0) {
      (void) dup2(null_fd, STDOUT_FILENO);
      if (null_fd != STDOUT_FILENO) {
        close(null_fd);
      }
    }
  }

  execv(child_argv[0], child_argv);
  _exit(127);
}

static bool wait_child(pid_t child, bool expect_success)
{
  struct timespec pause = { 0, 10000000 };
  int status = 0;
  int poll;

  for (poll = 0; poll < TEST_EXIT_POLLS; poll++) {
    pid_t result = waitpid(child, &status, WNOHANG);

    if (result == child) {
      bool success = WIFEXITED(status) && WEXITSTATUS(status) == 0;

      if (success != expect_success) {
        if (WIFSIGNALED(status)) {
          fprintf(stderr,
                  "child received signal %d; expected success=%d\n",
                  WTERMSIG(status), expect_success ? 1 : 0);
        } else {
          fprintf(stderr,
                  "child exited %d; expected success=%d\n",
                  WIFEXITED(status) ? WEXITSTATUS(status) : -1,
                  expect_success ? 1 : 0);
        }
        return false;
      }
      return true;
    }
    if (result < 0 && errno != EINTR) {
      return false;
    }
    (void) nanosleep(&pause, NULL);
  }

  (void) kill(child, SIGKILL);
  (void) waitpid(child, &status, 0);
  fprintf(stderr, "child did not exit within five seconds\n");
  return false;
}

static bool test_protocol(const char *binary)
{
  int sockets[2];
  pid_t child;
  char *const child_argv[] = {
    (char *) binary,
    "--name", "AgentSidecar",
    "--server", "127.0.0.1",
    "--port", "5555",
    "--",
    "--ipc-fd", "9",
    "--player", "AgentPlace1",
    NULL
  };
  bool passed;

  if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) < 0) {
    return false;
  }
  child = spawn_child(child_argv, sockets[1], sockets[0]);
  close(sockets[1]);
  if (child < 0) {
    close(sockets[0]);
    return false;
  }

  passed = expect_frame(sockets[0], "HELLO\t1\tfreeciv-agent")
           && send_frame(sockets[0], "HELLO\t1")
           && expect_frame(sockets[0], "HELLO\tOK\t1")
           && expect_frame(sockets[0], FC_AGENT_V2_CAPS_FRAME)
           && send_frame(sockets[0], "OBS_OPEN\treq-open\tstate")
           && expect_frame(sockets[0],
                           "ERR\treq-open\tNOT_READY\texact%20target%20human%20seat%20is%20not%20authorized")
           && send_frame(sockets[0], "OBS_OPEN\treq-bad")
           && expect_frame(sockets[0],
                           "ERR\treq-bad\tBAD_REQUEST\tOBS_OPEN%20requires%20request%20and%20state")
           && send_frame(sockets[0],
                         "SCOPE_OPEN\treq-scope\t11\tp%3A1%3A10")
           && expect_frame(sockets[0],
                           "ERR\treq-scope\tNOT_READY\texact%20target%20human%20seat%20is%20not%20authorized")
           && send_frame(sockets[0], "SCOPE_OPEN\treq-scope-bad\t11")
           && expect_frame(sockets[0],
                           "ERR\treq-scope-bad\tBAD_REQUEST\tSCOPE_OPEN%20requires%20request%20revision%20and%20actor")
           && send_frame(sockets[0],
                         "SCOPE_PAGE\treq-page\tv11-1\t0\t16")
           && expect_frame(sockets[0],
                           "ERR\treq-page\tNOT_READY\texact%20target%20human%20seat%20is%20not%20authorized")
           && send_frame(sockets[0],
                         "TARGET_ACTION\treq-target\t11\tu%3A7%3A9\t42")
           && expect_frame(sockets[0],
                           "ERR\treq-target\tNOT_READY\texact%20target%20human%20seat%20is%20not%20authorized")
           && send_frame(sockets[0],
                         "TARGET_ACTION\treq-target-bad\t11\tu%3A7%3A9\t-1")
           && expect_frame(sockets[0],
                           "ERR\treq-target-bad\tBAD_REQUEST\tTARGET_ACTION%20requires%20request%20revision%20actor%20and%20tile")
           && send_frame(sockets[0],
                         "ACT_CAP\treq-cap\t11\tu%3A7%3A9\ta0000000000000000\t-")
           && expect_frame(sockets[0],
                           "ERR\treq-cap\tNOT_READY\texact%20target%20human%20seat%20is%20not%20authorized")
           && send_frame(sockets[0],
                         "ACT\tact-stale\ta0000000000000000\t-")
           && expect_frame(sockets[0],
                           "ERR\tact-stale\tNOT_READY\texact%20target%20human%20seat%20is%20not%20authorized")
           && send_frame(sockets[0], "PING\tprocess-test")
           && expect_frame(sockets[0], "PONG\tprocess-test")
           && send_frame(sockets[0], "STATUS")
           && expect_frame(sockets[0],
                           "STATUS\tstate=disconnected\tserver=0\tseat=idle"
                           "\tplayer=-1\tlifecycle=0")
           && send_frame(sockets[0], "TAKE")
           && expect_frame(sockets[0], "TAKE\tQUEUED")
           && send_frame(sockets[0], "SHUTDOWN")
           && expect_frame(sockets[0], "BYE\tSHUTDOWN");
  close(sockets[0]);

  return wait_child(child, true) && passed;
}

static bool test_ipc_eof(const char *binary)
{
  int sockets[2];
  pid_t child;
  char *const child_argv[] = {
    (char *) binary,
    "--",
    "--ipc-fd", "9",
    "--player", "AgentPlace1",
    NULL
  };

  if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) < 0) {
    return false;
  }
  child = spawn_child(child_argv, sockets[1], sockets[0]);
  close(sockets[1]);
  if (child < 0) {
    close(sockets[0]);
    return false;
  }
  if (!expect_frame(sockets[0], "HELLO\t1\tfreeciv-agent")) {
    close(sockets[0]);
    (void) wait_child(child, false);
    return false;
  }
  close(sockets[0]);
  return wait_child(child, true);
}

static bool test_rejected_launch(const char *binary, char *const argv[],
                                 bool use_socket, bool use_pipe)
{
  int descriptors[2] = { -1, -1 };
  pid_t child;
  bool passed;

  if ((use_socket && socketpair(AF_UNIX, SOCK_STREAM, 0, descriptors) < 0)
      || (use_pipe && pipe(descriptors) < 0)) {
    return false;
  }
  child = spawn_child(argv,
                      (use_socket || use_pipe) ? descriptors[1] : -1,
                      (use_socket || use_pipe) ? descriptors[0] : -1);
  if (use_socket || use_pipe) {
    close(descriptors[1]);
  }
  passed = child >= 0 && wait_child(child, false);
  if (use_socket || use_pipe) {
    close(descriptors[0]);
  }
  return passed;
}

int main(int argc, char **argv)
{
  const char *binary;
  char *missing_delimiter[2];
  char *unsafe_player[7];
  char *unknown_common[9];
  char *duplicate_common[11];
  char *pipe_ipc[7];
  bool passed = true;

  if (argc == 2 && strcmp(argv[1], "--codec-only") == 0) {
    return test_protocol_v2_codec() ? EXIT_SUCCESS : EXIT_FAILURE;
  }
  if (argc != 2) {
    fprintf(stderr, "usage: %s FREECIV_AGENT\n", argv[0]);
    return EXIT_FAILURE;
  }
  if (fc_agent_fd_selectable(-1)
      || !fc_agent_fd_selectable(0)
      || !fc_agent_fd_selectable(FD_SETSIZE - 1)
      || fc_agent_fd_selectable(FD_SETSIZE)) {
    fprintf(stderr, "select descriptor boundary guard failed\n");
    return EXIT_FAILURE;
  }
  binary = argv[1];
  (void) signal(SIGPIPE, SIG_IGN);

  passed = test_protocol_v2_codec() && passed;

  missing_delimiter[0] = (char *) binary;
  missing_delimiter[1] = NULL;

  unsafe_player[0] = (char *) binary;
  unsafe_player[1] = "--";
  unsafe_player[2] = "--ipc-fd";
  unsafe_player[3] = "9";
  unsafe_player[4] = "--player";
  unsafe_player[5] = "unsafe name";
  unsafe_player[6] = NULL;

  unknown_common[0] = (char *) binary;
  unknown_common[1] = "--tiles";
  unknown_common[2] = "amplio2";
  unknown_common[3] = "--";
  unknown_common[4] = "--ipc-fd";
  unknown_common[5] = "9";
  unknown_common[6] = "--player";
  unknown_common[7] = "AgentPlace1";
  unknown_common[8] = NULL;

  duplicate_common[0] = (char *) binary;
  duplicate_common[1] = "--port";
  duplicate_common[2] = "5555";
  duplicate_common[3] = "--port";
  duplicate_common[4] = "5556";
  duplicate_common[5] = "--";
  duplicate_common[6] = "--ipc-fd";
  duplicate_common[7] = "9";
  duplicate_common[8] = "--player";
  duplicate_common[9] = "AgentPlace1";
  duplicate_common[10] = NULL;

  pipe_ipc[0] = (char *) binary;
  pipe_ipc[1] = "--";
  pipe_ipc[2] = "--ipc-fd";
  pipe_ipc[3] = "9";
  pipe_ipc[4] = "--player";
  pipe_ipc[5] = "AgentPlace1";
  pipe_ipc[6] = NULL;

  passed = test_protocol(binary) && passed;
  passed = test_ipc_eof(binary) && passed;
  passed = test_rejected_launch(binary, missing_delimiter, false, false)
           && passed;
  passed = test_rejected_launch(binary, unsafe_player, true, false)
           && passed;
  passed = test_rejected_launch(binary, unknown_common, true, false)
           && passed;
  passed = test_rejected_launch(binary, duplicate_common, true, false)
           && passed;
  passed = test_rejected_launch(binary, pipe_ipc, false, true) && passed;

  return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
