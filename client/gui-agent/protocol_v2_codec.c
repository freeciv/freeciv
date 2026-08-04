/***********************************************************************
 Freeciv - Copyright (C) 1996 - The Freeciv Project

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2, or (at your option)
 any later version.
***********************************************************************/

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "protocol_v2_codec.h"

static bool v2_unreserved(unsigned char value)
{
  return (value >= 'a' && value <= 'z')
         || (value >= 'A' && value <= 'Z')
         || (value >= '0' && value <= '9')
         || value == '.' || value == '_' || value == '~' || value == '-';
}

static int v2_hex_value(char value)
{
  if (value >= '0' && value <= '9') {
    return value - '0';
  }
  if (value >= 'A' && value <= 'F') {
    return 10 + value - 'A';
  }
  return -1;
}

static bool v2_valid_utf8(const char *text, size_t length)
{
  size_t i = 0;

  while (i < length) {
    const unsigned char first = (unsigned char) text[i];
    uint32_t value;
    size_t count;
    size_t j;

    if (first <= 0x7f) {
      i++;
      continue;
    } else if (first >= 0xc2 && first <= 0xdf) {
      value = first & 0x1f;
      count = 1;
    } else if (first >= 0xe0 && first <= 0xef) {
      value = first & 0x0f;
      count = 2;
    } else if (first >= 0xf0 && first <= 0xf4) {
      value = first & 0x07;
      count = 3;
    } else {
      return false;
    }
    if (length - i - 1 < count) {
      return false;
    }
    for (j = 1; j <= count; j++) {
      const unsigned char next = (unsigned char) text[i + j];

      if ((next & 0xc0) != 0x80) {
        return false;
      }
      value = (value << 6) | (next & 0x3f);
    }
    if ((count == 2 && value < 0x800)
        || (count == 3 && value < 0x10000)
        || value > 0x10ffff
        || (value >= 0xd800 && value <= 0xdfff)) {
      return false;
    }
    i += count + 1;
  }
  return true;
}

bool fc_agent_v2_percent_encode(const char *raw, char *encoded,
                                size_t encoded_size)
{
  static const char hex[] = "0123456789ABCDEF";
  size_t input;
  size_t output = 0;
  size_t raw_length = strlen(raw);

  if (!v2_valid_utf8(raw, raw_length)) {
    return false;
  }
  for (input = 0; input < raw_length; input++) {
    unsigned char value = (unsigned char) raw[input];

    if (v2_unreserved(value)) {
      if (output + 1 >= encoded_size) {
        return false;
      }
      encoded[output++] = (char) value;
    } else {
      if (output + 3 >= encoded_size) {
        return false;
      }
      encoded[output++] = '%';
      encoded[output++] = hex[value >> 4];
      encoded[output++] = hex[value & 0x0f];
    }
  }
  if (encoded_size == 0) {
    return false;
  }
  encoded[output] = '\0';
  return true;
}

bool fc_agent_v2_percent_decode(const char *encoded, char *decoded,
                                size_t decoded_size)
{
  size_t input = 0;
  size_t output = 0;

  if (decoded_size == 0) {
    return false;
  }
  while (encoded[input] != '\0') {
    unsigned char value;

    if (encoded[input] == '%') {
      int high;
      int low;

      if (encoded[input + 1] == '\0' || encoded[input + 2] == '\0'
          || (high = v2_hex_value(encoded[input + 1])) < 0
          || (low = v2_hex_value(encoded[input + 2])) < 0) {
        return false;
      }
      value = (unsigned char) ((high << 4) | low);
      input += 3;
    } else {
      value = (unsigned char) encoded[input++];
      if (!v2_unreserved(value)) {
        return false;
      }
    }
    if (value == '\0' || output + 1 >= decoded_size) {
      return false;
    }
    decoded[output++] = (char) value;
  }
  decoded[output] = '\0';
  return v2_valid_utf8(decoded, output);
}

static bool v2_parse_uint64_canonical(const char **cursor, uint64_t maximum,
                                      uint64_t *result)
{
  const char *start = *cursor;
  uint64_t value = 0;

  if (**cursor < '0' || **cursor > '9'
      || (**cursor == '0' && (*cursor)[1] >= '0'
          && (*cursor)[1] <= '9')) {
    return false;
  }
  while (**cursor >= '0' && **cursor <= '9') {
    unsigned int digit = (unsigned int) (**cursor - '0');

    if (value > (maximum - digit) / 10) {
      return false;
    }
    value = value * 10 + digit;
    (*cursor)++;
  }
  if (*cursor == start) {
    return false;
  }
  *result = value;
  return true;
}

bool fc_agent_v2_parse_entity_ref(const char *text, char *kind, int *id,
                                  uint64_t *incarnation)
{
  const char *cursor = text;
  uint64_t parsed_id;
  uint64_t parsed_incarnation;

  if (text == NULL || kind == NULL || id == NULL || incarnation == NULL
      || (*cursor != 'p' && *cursor != 'c' && *cursor != 'u')
      || cursor[1] != ':') {
    return false;
  }
  *kind = *cursor;
  cursor += 2;
  if (!v2_parse_uint64_canonical(&cursor, INT_MAX, &parsed_id)
      || *cursor++ != ':'
      || !v2_parse_uint64_canonical(&cursor, UINT64_MAX,
                                    &parsed_incarnation)
      || *cursor != '\0' || parsed_incarnation == 0) {
    return false;
  }
  *id = (int) parsed_id;
  *incarnation = parsed_incarnation;
  return true;
}

bool fc_agent_v2_government_change_observable(int revolution_finishes,
                                              int current_turn,
                                              bool has_no_anarchy)
{
  return revolution_finishes > current_turn
         || (revolution_finishes <= 0 && !has_no_anarchy);
}

bool fc_agent_v2_revolution_available(bool untargeted_allowed,
                                      bool can_change_during,
                                      bool current_is_during,
                                      bool target_is_during)
{
  return untargeted_allowed && can_change_during
         && !(current_is_during && target_is_during);
}

enum fc_agent_v2_government_status fc_agent_v2_government_status(
  int current_government, int target_government, int during_government,
  int revolution_finishes, int current_turn)
{
  if (current_government != during_government) {
    return target_government < 0
           ? FC_AGENT_V2_GOV_STABLE
           : FC_AGENT_V2_GOV_ENACTMENT_PENDING;
  }
  if (target_government < 0 || target_government == during_government) {
    return revolution_finishes <= current_turn
           ? FC_AGENT_V2_GOV_CHOICE_REQUIRED
           : FC_AGENT_V2_GOV_ANARCHY;
  }
  return revolution_finishes <= current_turn
         ? FC_AGENT_V2_GOV_ENACTMENT_PENDING
         : FC_AGENT_V2_GOV_ANARCHY_TARGETED;
}

const char *fc_agent_v2_government_status_name(
  enum fc_agent_v2_government_status status)
{
  switch (status) {
  case FC_AGENT_V2_GOV_STABLE:
    return "stable";
  case FC_AGENT_V2_GOV_ANARCHY:
    return "anarchy";
  case FC_AGENT_V2_GOV_ANARCHY_TARGETED:
    return "anarchy_targeted";
  case FC_AGENT_V2_GOV_CHOICE_REQUIRED:
    return "choice_required";
  case FC_AGENT_V2_GOV_ENACTMENT_PENDING:
    return "enactment_pending";
  }
  return NULL;
}

bool fc_agent_v2_government_postcondition(
  enum fc_agent_v2_government_command command,
  int before_current, int before_target, int before_finish,
  int after_current, int after_target, int after_finish,
  int during_government, int desired_government)
{
  if (command == FC_AGENT_V2_GOV_REVOLUTION) {
    return after_current == during_government
           && after_target == during_government
           && (after_current != before_current
               || after_target != before_target);
  }
  if (command == FC_AGENT_V2_GOV_CHANGE) {
    return (after_current == during_government
            && after_target == desired_government)
           || (after_current == desired_government && after_target < 0);
  }
  return false;
}

bool fc_agent_v2_probability_candidate_preferred(
  int candidate_rank, int candidate_min, int candidate_max,
  int candidate_action, int existing_rank, int existing_min,
  int existing_max, int existing_action)
{
  return candidate_rank < existing_rank
         || (candidate_rank == existing_rank
             && (candidate_min > existing_min
                 || (candidate_min == existing_min
                     && (candidate_max < existing_max
                         || (candidate_max == existing_max
                             && candidate_action < existing_action)))));
}

bool fc_agent_v2_unit_lifetime_matches(uint64_t tracked_lifecycle,
                                       uint64_t current_lifecycle)
{
  return tracked_lifecycle != 0
         && current_lifecycle != 0
         && tracked_lifecycle == current_lifecycle;
}

bool fc_agent_v2_city_lifetime_matches(uint64_t tracked_lifecycle,
                                       uint64_t current_lifecycle)
{
  return tracked_lifecycle != 0
         && current_lifecycle != 0
         && tracked_lifecycle == current_lifecycle;
}

bool fc_agent_v2_unit_activity_postcondition(
  uint64_t expected_lifecycle, bool before_present,
  uint64_t before_lifecycle, bool current_present,
  uint64_t current_lifecycle, int current_activity,
  bool activity_target_none, int requested_activity,
  int completed_activity)
{
  return expected_lifecycle != 0
         && before_present
         && before_lifecycle == expected_lifecycle
         && current_present
         && current_lifecycle == expected_lifecycle
         && activity_target_none
         && (current_activity == requested_activity
             || (completed_activity >= 0
                 && current_activity == completed_activity));
}

bool fc_agent_v2_unit_conversion_postcondition(
  uint64_t expected_lifecycle, bool before_present,
  uint64_t before_lifecycle, bool current_present,
  uint64_t current_lifecycle, int current_activity,
  bool activity_target_none, int convert_activity,
  int before_type, int desired_type, int current_type)
{
  return expected_lifecycle != 0
         && before_present
         && before_lifecycle == expected_lifecycle
         && current_present
         && current_lifecycle == expected_lifecycle
         && ((current_activity == convert_activity && activity_target_none)
             || (before_type != desired_type
                 && current_type == desired_type));
}

bool fc_agent_v2_unit_consumed_postcondition(
  uint64_t expected_lifecycle, bool before_present,
  uint64_t before_lifecycle, bool current_present)
{
  return expected_lifecycle != 0
         && before_present
         && before_lifecycle == expected_lifecycle
         && !current_present;
}

bool fc_agent_v2_unit_home_cleared_postcondition(
  uint64_t expected_lifecycle, bool before_present,
  uint64_t before_lifecycle, bool current_present,
  uint64_t current_lifecycle, int before_home, int current_home)
{
  return expected_lifecycle != 0
         && before_present
         && before_lifecycle == expected_lifecycle
         && current_present
         && current_lifecycle == expected_lifecycle
         && before_home != 0
         && current_home == 0;
}

bool fc_agent_v2_unit_upgrade_postcondition(
  uint64_t expected_lifecycle, bool before_present,
  uint64_t before_lifecycle, int before_type,
  bool current_present, uint64_t current_lifecycle,
  int desired_type, int current_type)
{
  return expected_lifecycle != 0
         && before_present && before_lifecycle == expected_lifecycle
         && current_present && current_lifecycle == expected_lifecycle
         && before_type >= 0 && desired_type >= 0
         && before_type != desired_type && current_type == desired_type;
}

bool fc_agent_v2_unit_rehome_postcondition(
  uint64_t expected_lifecycle, bool before_present,
  uint64_t before_lifecycle, int before_home,
  bool current_present, uint64_t current_lifecycle,
  int desired_home, int current_home)
{
  return expected_lifecycle != 0
         && before_present && before_lifecycle == expected_lifecycle
         && current_present && current_lifecycle == expected_lifecycle
         && desired_home > 0 && before_home != desired_home
         && current_home == desired_home;
}

bool fc_agent_v2_consumed_city_postcondition(
  uint64_t expected_unit_lifecycle, bool before_unit_present,
  uint64_t before_unit_lifecycle, bool current_unit_present,
  uint64_t expected_city_lifecycle, bool before_city_present,
  uint64_t before_city_lifecycle, bool current_city_present,
  uint64_t current_city_lifecycle)
{
  return fc_agent_v2_unit_consumed_postcondition(
           expected_unit_lifecycle, before_unit_present,
           before_unit_lifecycle, current_unit_present)
         && expected_city_lifecycle != 0
         && before_city_present
         && before_city_lifecycle == expected_city_lifecycle
         && current_city_present
         && current_city_lifecycle == expected_city_lifecycle;
}

bool fc_agent_v2_marketplace_postcondition(
  uint64_t expected_unit_lifecycle, bool before_unit_present,
  uint64_t before_unit_lifecycle, bool current_unit_present,
  uint64_t expected_source_lifecycle, bool before_source_present,
  uint64_t before_source_lifecycle, bool current_source_present,
  uint64_t current_source_lifecycle,
  uint64_t expected_destination_lifecycle,
  bool before_destination_present,
  uint64_t before_destination_lifecycle,
  bool current_destination_present,
  uint64_t current_destination_lifecycle)
{
  return fc_agent_v2_unit_consumed_postcondition(
           expected_unit_lifecycle, before_unit_present,
           before_unit_lifecycle, current_unit_present)
         && expected_source_lifecycle != 0 && before_source_present
         && before_source_lifecycle == expected_source_lifecycle
         && current_source_present
         && current_source_lifecycle == expected_source_lifecycle
         && expected_destination_lifecycle != 0
         && before_destination_present
         && before_destination_lifecycle == expected_destination_lifecycle
         && current_destination_present
         && current_destination_lifecycle == expected_destination_lifecycle;
}

bool fc_agent_v2_join_city_postcondition(
  uint64_t expected_unit_lifecycle, bool before_unit_present,
  uint64_t before_unit_lifecycle, bool current_unit_present,
  uint64_t expected_city_lifecycle, bool before_city_present,
  uint64_t before_city_lifecycle, int before_size, int population_added,
  bool current_city_present, uint64_t current_city_lifecycle,
  int current_size)
{
  return fc_agent_v2_consumed_city_postcondition(
           expected_unit_lifecycle, before_unit_present,
           before_unit_lifecycle, current_unit_present,
           expected_city_lifecycle, before_city_present,
           before_city_lifecycle, current_city_present,
           current_city_lifecycle)
         && before_size > 0 && population_added > 0
         && current_size == before_size + population_added;
}

bool fc_agent_v2_help_wonder_postcondition(
  uint64_t expected_unit_lifecycle, bool before_unit_present,
  uint64_t before_unit_lifecycle, bool current_unit_present,
  uint64_t expected_city_lifecycle, bool before_city_present,
  uint64_t before_city_lifecycle, bool exact_city_internals,
  int before_shields, int shields_added,
  bool current_city_present, uint64_t current_city_lifecycle,
  int current_shields)
{
  return fc_agent_v2_consumed_city_postcondition(
           expected_unit_lifecycle, before_unit_present,
           before_unit_lifecycle, current_unit_present,
           expected_city_lifecycle, before_city_present,
           before_city_lifecycle, current_city_present,
           current_city_lifecycle)
         && (!exact_city_internals
             || (before_shields >= 0 && shields_added > 0
                 && current_shields == before_shields + shields_added));
}

bool fc_agent_v2_disband_recover_postcondition(
  uint64_t expected_unit_lifecycle, bool before_unit_present,
  uint64_t before_unit_lifecycle, bool current_unit_present,
  uint64_t expected_city_lifecycle, bool before_city_present,
  uint64_t before_city_lifecycle, bool caravan_action_event,
  bool before_city_owned, int before_shields, int shields_added,
  bool current_city_present, uint64_t current_city_lifecycle,
  bool current_city_owned, int current_shields)
{
  return caravan_action_event && shields_added > 0
         && fc_agent_v2_consumed_city_postcondition(
              expected_unit_lifecycle, before_unit_present,
              before_unit_lifecycle, current_unit_present,
              expected_city_lifecycle, before_city_present,
              before_city_lifecycle, current_city_present,
              current_city_lifecycle)
         && (!before_city_owned
             || (current_city_owned && before_shields >= 0
                 && current_shields == before_shields + shields_added));
}

bool fc_agent_v2_trade_route_postcondition(
  uint64_t expected_unit_lifecycle, bool before_unit_present,
  uint64_t before_unit_lifecycle, bool current_unit_present,
  uint64_t expected_source_lifecycle, bool before_source_present,
  uint64_t before_source_lifecycle, bool current_source_present,
  uint64_t current_source_lifecycle,
  uint64_t expected_destination_lifecycle,
  bool before_destination_present,
  uint64_t before_destination_lifecycle,
  bool current_destination_present,
  uint64_t current_destination_lifecycle,
  bool before_route, bool current_route)
{
  return fc_agent_v2_marketplace_postcondition(
           expected_unit_lifecycle, before_unit_present,
           before_unit_lifecycle, current_unit_present,
           expected_source_lifecycle, before_source_present,
           before_source_lifecycle, current_source_present,
           current_source_lifecycle,
           expected_destination_lifecycle, before_destination_present,
           before_destination_lifecycle, current_destination_present,
           current_destination_lifecycle)
         && !before_route && current_route;
}

bool fc_agent_v2_unit_relocation_postcondition(
  uint64_t expected_lifecycle, bool before_present,
  uint64_t before_lifecycle, int before_tile,
  bool current_present, uint64_t current_lifecycle, int current_tile,
  int target_tile, bool require_paradropped, bool before_paradropped,
  bool current_paradropped)
{
  return expected_lifecycle != 0
         && before_present
         && before_lifecycle == expected_lifecycle
         && before_tile >= 0
         && target_tile >= 0
         && before_tile != target_tile
         && current_present
         && current_lifecycle == expected_lifecycle
         && current_tile == target_tile
         && (!require_paradropped
             || (!before_paradropped && current_paradropped));
}

bool fc_agent_v2_paradrop_enter_conquer_postcondition(
  bool baseline_exact,
  uint64_t expected_lifecycle, bool before_present,
  uint64_t before_lifecycle, int before_tile, bool before_paradropped,
  bool current_present, uint64_t current_lifecycle, int current_tile,
  bool current_paradropped, int target_tile, int self_player,
  int before_city_id, int before_city_owner,
  int current_city_id, int current_city_owner,
  int before_extra_owner, int current_extra_owner,
  bool before_hut_present, bool hut_removed)
{
  return baseline_exact && self_player >= 0
         && fc_agent_v2_unit_relocation_postcondition(
              expected_lifecycle, before_present, before_lifecycle,
              before_tile, current_present, current_lifecycle, current_tile,
              target_tile, true, before_paradropped, current_paradropped)
         && (before_city_id < 0
             ? before_city_owner < 0
             : (before_city_owner >= 0
                && before_city_owner != self_player
                && current_city_id == before_city_id
                && current_city_owner == self_player))
         && (before_extra_owner < 0
             || (before_extra_owner != self_player
                 && current_extra_owner == self_player))
         && (!before_hut_present || hut_removed);
}

bool fc_agent_v2_transport_occupancy_exact(bool advertised_occupied,
                                           int known_occupied,
                                           int capacity,
                                           bool all_cargo_visible)
{
  /* The wire only advertises empty/nonempty.  Without authority proving that
   * every cargo packet is visible, only empty and locally full counts are
   * exact; a positive partial list may omit third-party cargo. */
  return known_occupied >= 0
         && capacity >= 0
         && known_occupied <= capacity
         && advertised_occupied == (known_occupied > 0)
         && (all_cargo_visible
             || known_occupied == 0
             || known_occupied == capacity);
}

bool fc_agent_v2_transport_postcondition(
  enum fc_agent_v2_transport_command command,
  uint64_t expected_actor_lifecycle,
  bool before_actor_present, uint64_t before_actor_lifecycle,
  int before_actor_tile,
  uint64_t expected_target_lifecycle,
  bool before_target_present, uint64_t before_target_lifecycle,
  int before_target_tile,
  uint64_t expected_context_lifecycle,
  bool before_context_present, uint64_t before_context_lifecycle,
  int before_context_tile, bool before_baseline_exact,
  bool current_actor_present, uint64_t current_actor_lifecycle,
  int current_actor_tile,
  bool current_target_present, uint64_t current_target_lifecycle,
  int current_target_tile,
  bool current_context_present, uint64_t current_context_lifecycle,
  int current_context_tile, bool current_relationship_exact,
  bool current_detached_exact, int requested_tile)
{
  bool actor_exact = expected_actor_lifecycle != 0
                     && before_actor_present
                     && before_actor_lifecycle == expected_actor_lifecycle
                     && current_actor_present
                     && current_actor_lifecycle == expected_actor_lifecycle
                     && before_actor_tile >= 0 && current_actor_tile >= 0;
  bool target_exact = expected_target_lifecycle != 0
                      && before_target_present
                      && before_target_lifecycle == expected_target_lifecycle
                      && current_target_present
                      && current_target_lifecycle
                         == expected_target_lifecycle
                      && before_target_tile >= 0 && current_target_tile >= 0;
  bool context_exact = expected_context_lifecycle != 0
                       && before_context_present
                       && before_context_lifecycle
                          == expected_context_lifecycle
                       && current_context_present
                       && current_context_lifecycle
                          == expected_context_lifecycle
                       && before_context_tile >= 0
                       && current_context_tile >= 0;
  bool optional_context_exact =
    (expected_context_lifecycle == 0
     && !before_context_present && !current_context_present)
    || (context_exact && current_context_tile == before_context_tile);

  if (!before_baseline_exact || !actor_exact) {
    return false;
  }
  switch (command) {
  case FC_AGENT_V2_TRANSPORT_BOARD:
    return target_exact && optional_context_exact
           && before_actor_tile == before_target_tile
           && current_target_tile == before_target_tile
           && current_actor_tile == current_target_tile
           && current_relationship_exact && !current_detached_exact;
  case FC_AGENT_V2_TRANSPORT_EMBARK:
    return target_exact && optional_context_exact
           && before_actor_tile != before_target_tile
           && current_target_tile == before_target_tile
           && current_actor_tile == current_target_tile
           && current_relationship_exact && !current_detached_exact;
  case FC_AGENT_V2_TRANSPORT_LOAD:
    return target_exact && optional_context_exact
           && before_actor_tile == before_target_tile
           && current_actor_tile == before_actor_tile
           && current_target_tile == current_actor_tile
           && current_relationship_exact && !current_detached_exact;
  case FC_AGENT_V2_TRANSPORT_DEBOARD:
    return target_exact && context_exact
           && expected_context_lifecycle == expected_target_lifecycle
           && before_actor_tile == before_target_tile
           && before_context_tile == before_target_tile
           && current_target_tile == before_target_tile
           && current_context_tile == current_target_tile
           && current_actor_tile == current_target_tile
           && !current_relationship_exact && current_detached_exact;
  case FC_AGENT_V2_TRANSPORT_UNLOAD:
    return target_exact && context_exact
           && expected_context_lifecycle == expected_actor_lifecycle
           && before_actor_tile == before_target_tile
           && before_context_tile == before_actor_tile
           && current_actor_tile == before_actor_tile
           && current_context_tile == current_actor_tile
           && current_target_tile == current_actor_tile
           && !current_relationship_exact && current_detached_exact;
  case FC_AGENT_V2_TRANSPORT_DISEMBARK:
    return expected_target_lifecycle == 0 && !before_target_present
           && !current_target_present && context_exact
           && requested_tile >= 0 && requested_tile != before_context_tile
           && current_context_tile == before_context_tile
           && current_actor_tile == requested_tile
           && !current_relationship_exact && current_detached_exact;
  }
  return false;
}

static uint64_t v2_fnv1a64(uint64_t digest, const void *data, size_t length)
{
  const unsigned char *bytes = data;
  size_t i;

  for (i = 0; i < length; i++) {
    digest ^= bytes[i];
    digest *= UINT64_C(1099511628211);
  }
  return digest;
}

uint64_t fc_agent_v2_research_choices_digest_init(void)
{
  return UINT64_C(14695981039346656037);
}

bool fc_agent_v2_research_choices_digest_add(
  uint64_t *digest, int native_id, const char *canonical_name,
  const char *state, bool can_target, bool can_goal)
{
  unsigned char id_bytes[4];
  unsigned char name_length_bytes[4];
  unsigned char state_length_byte;
  unsigned char flags[2];
  size_t name_length;
  size_t state_length;
  uint32_t id;
  uint32_t encoded_name_length;

  if (digest == NULL || native_id < 0 || canonical_name == NULL
      || state == NULL) {
    return false;
  }
  name_length = strlen(canonical_name);
  state_length = strlen(state);
  if (name_length > UINT32_MAX || state_length > UINT8_MAX
      || !v2_valid_utf8(canonical_name, name_length)
      || !v2_valid_utf8(state, state_length)) {
    return false;
  }
  {
    size_t i;

    for (i = 0; i < state_length; i++) {
      if ((unsigned char) state[i] > 0x7f) {
        return false;
      }
    }
  }

  id = (uint32_t) native_id;
  encoded_name_length = (uint32_t) name_length;
  id_bytes[0] = (unsigned char) (id >> 24);
  id_bytes[1] = (unsigned char) (id >> 16);
  id_bytes[2] = (unsigned char) (id >> 8);
  id_bytes[3] = (unsigned char) id;
  name_length_bytes[0] = (unsigned char) (encoded_name_length >> 24);
  name_length_bytes[1] = (unsigned char) (encoded_name_length >> 16);
  name_length_bytes[2] = (unsigned char) (encoded_name_length >> 8);
  name_length_bytes[3] = (unsigned char) encoded_name_length;
  state_length_byte = (unsigned char) state_length;
  flags[0] = can_target ? 1 : 0;
  flags[1] = can_goal ? 1 : 0;

  *digest = v2_fnv1a64(*digest, id_bytes, sizeof(id_bytes));
  *digest = v2_fnv1a64(*digest, name_length_bytes,
                       sizeof(name_length_bytes));
  *digest = v2_fnv1a64(*digest, canonical_name, name_length);
  *digest = v2_fnv1a64(*digest, &state_length_byte,
                       sizeof(state_length_byte));
  *digest = v2_fnv1a64(*digest, state, state_length);
  *digest = v2_fnv1a64(*digest, flags, sizeof(flags));
  return true;
}

bool fc_agent_v2_format_unknown_tile(char *buffer, size_t buffer_size,
                                     int tile_index, int x, int y)
{
  int length;

  if (buffer == NULL || buffer_size == 0
      || tile_index < 0 || x < 0 || y < 0) {
    return false;
  }
  length = snprintf(buffer, buffer_size, FC_AGENT_V2_ROW_TILE,
                    tile_index, x, y, 0, "unknown", "none",
                    -1, "none", 0, -1);
  return length >= 0 && (size_t) length < buffer_size;
}

/************************************************************************//**
  Format the privacy-preserving local-observation form of an unknown tile.

  Every local-only field uses its closed sentinel.  The Python boundary strips
  these sentinels and exposes only the opaque identity, coordinates, and
  unknown visibility.
****************************************************************************/
bool fc_agent_v2_format_unknown_local_tile(char *buffer, size_t buffer_size,
                                           int tile_index, int x, int y)
{
  int length;

  if (buffer == NULL || buffer_size == 0
      || tile_index < 0 || x < 0 || y < 0) {
    return false;
  }
  length = snprintf(buffer, buffer_size, FC_AGENT_V2_ROW_TILE_LOCAL,
                    tile_index, x, y, 0, "unknown", "none",
                    -1, "none", 0, -1, -1, "none", 0, "none",
                    -1, -1, -1);
  return length >= 0 && (size_t) length < buffer_size;
}

static bool v2_parse_rate_value(const char **cursor, const char *prefix,
                                int *value)
{
  const size_t prefix_length = strlen(prefix);
  const char *start;
  char *end = NULL;
  long parsed;

  if (strncmp(*cursor, prefix, prefix_length) != 0) {
    return false;
  }
  *cursor += prefix_length;
  start = *cursor;
  if (**cursor < '0' || **cursor > '9') {
    return false;
  }
  errno = 0;
  parsed = strtol(*cursor, &end, 10);
  if (errno != 0 || end == *cursor || parsed < 0 || parsed > INT_MAX
      || (start[0] == '0' && end != start + 1)) {
    return false;
  }
  *cursor = end;
  *value = (int) parsed;
  return true;
}

bool fc_agent_v2_parse_rates(const char *text, bool changeable,
                             int max_rate, int *tax, int *luxury,
                             int *science)
{
  const char *cursor = text;
  int parsed_tax;
  int parsed_luxury;
  int parsed_science;

  if (text == NULL || tax == NULL || luxury == NULL || science == NULL
      || !changeable || max_rate < 0 || max_rate > 100
      || !v2_parse_rate_value(&cursor, "tax=", &parsed_tax)
      || *cursor++ != ','
      || !v2_parse_rate_value(&cursor, "luxury=", &parsed_luxury)
      || *cursor++ != ','
      || !v2_parse_rate_value(&cursor, "science=", &parsed_science)
      || *cursor != '\0'
      || parsed_tax > max_rate || parsed_luxury > max_rate
      || parsed_science > max_rate
      || parsed_tax + parsed_luxury + parsed_science != 100) {
    return false;
  }
  *tax = parsed_tax;
  *luxury = parsed_luxury;
  *science = parsed_science;
  return true;
}

bool fc_agent_v2_parse_pregame_team_argument(
  const char *text, int team_slot_count, int *team_slot)
{
  const char *cursor;
  unsigned int value = 0;

  if (text == NULL || team_slot == NULL || team_slot_count <= 0
      || strncmp(text, "team=", 5) != 0) {
    return false;
  }
  cursor = text + 5;
  if (*cursor == '\0' || (*cursor == '0' && cursor[1] != '\0')) {
    return false;
  }
  for (; *cursor != '\0'; cursor++) {
    unsigned int digit;

    if (*cursor < '0' || *cursor > '9') {
      return false;
    }
    digit = (unsigned int) (*cursor - '0');
    if (value > ((unsigned int) INT_MAX - digit) / 10U) {
      return false;
    }
    value = value * 10U + digit;
  }
  if (value >= (unsigned int) team_slot_count) {
    return false;
  }
  *team_slot = (int) value;
  return true;
}

bool fc_agent_v2_pregame_team_choice_allowed(
  bool initial_pregame, bool unready, int current_team,
  int current_team_members, int desired_team, bool desired_team_used,
  int first_unused_team)
{
  return initial_pregame && unready
         && current_team >= 0 && current_team_members >= 1
         && desired_team >= 0 && desired_team != current_team
         && (desired_team_used
             || (current_team_members > 1
                 && desired_team == first_unused_team));
}

bool fc_agent_v2_worklist_append_allowed(
  size_t desired_length, bool can_queue,
  size_t desired_occurrences, size_t current_occurrences)
{
  return desired_length < FC_AGENT_V2_MAX_CITY_WORKLIST
         && (can_queue || desired_occurrences < current_occurrences);
}

enum fc_agent_v2_target_action_policy fc_agent_v2_target_action_policy(
  bool target_unknown, bool movement_action, bool probability_possible)
{
  if (!probability_possible || (target_unknown && !movement_action)) {
    return FC_AGENT_V2_TARGET_ACTION_REJECT;
  }
  return target_unknown
         ? FC_AGENT_V2_TARGET_ACTION_REDACT_TO_UNKNOWN
         : FC_AGENT_V2_TARGET_ACTION_PRESERVE_PROBABILITY;
}

enum fc_agent_v2_action_query fc_agent_v2_action_query_policy(
  bool target_unknown, bool movement_action)
{
  if (!target_unknown) {
    return FC_AGENT_V2_ACTION_QUERY_TARGET;
  }
  return movement_action
         ? FC_AGENT_V2_ACTION_QUERY_ACTOR_ONLY
         : FC_AGENT_V2_ACTION_QUERY_REJECT;
}

bool fc_agent_v2_target_server_query_allowed(
  bool target_known, bool target_visible)
{
  return target_known && target_visible;
}

static uint64_t v2_hash_bytes(uint64_t hash, const void *data, size_t length)
{
  const unsigned char *bytes = data;
  size_t i;

  for (i = 0; i < length; i++) {
    hash ^= bytes[i];
    hash *= UINT64_C(1099511628211);
  }
  return hash;
}

void fc_agent_v2_make_slot(char *slot, size_t slot_size,
                           uint64_t secret, uint64_t revision,
                           const void *semantic, size_t semantic_size)
{
  uint64_t hash = UINT64_C(1469598103934665603);

  hash = v2_hash_bytes(hash, &secret, sizeof(secret));
  hash = v2_hash_bytes(hash, &revision, sizeof(revision));
  hash = v2_hash_bytes(hash, semantic, semantic_size);
  (void) snprintf(slot, slot_size, "a%016llX",
                  (unsigned long long) hash);
}

bool fc_agent_v2_make_target_slot(char *slot, size_t slot_size,
                                  uint64_t secret, uint64_t revision,
                                  uint32_t native_tile,
                                  const void *semantic,
                                  size_t semantic_size)
{
  static const char domain[] = "target-slot-v1";
  uint64_t hash = UINT64_C(1469598103934665603);
  int length;

  if (slot == NULL || slot_size < 26
      || (semantic == NULL && semantic_size != 0)) {
    return false;
  }
  /* sizeof(domain) intentionally binds the fixed trailing NUL as part of
   * the versioned domain contract.  Keep the selector bound independently
   * from its duplicate inside the complete action semantic. */
  hash = v2_hash_bytes(hash, domain, sizeof(domain));
  hash = v2_hash_bytes(hash, &secret, sizeof(secret));
  hash = v2_hash_bytes(hash, &revision, sizeof(revision));
  hash = v2_hash_bytes(hash, &native_tile, sizeof(native_tile));
  hash = v2_hash_bytes(hash, semantic, semantic_size);
  length = snprintf(slot, slot_size, "t%08X%016llX", native_tile,
                    (unsigned long long) hash);
  return length == 25;
}

bool fc_agent_v2_parse_target_slot(const char *slot,
                                   uint32_t *native_tile)
{
  uint32_t value = 0;
  size_t i;

  if (slot == NULL || native_tile == NULL || strlen(slot) != 25
      || slot[0] != 't') {
    return false;
  }
  for (i = 1; i < 25; i++) {
    int digit = v2_hex_value(slot[i]);

    if (digit < 0) {
      return false;
    }
    if (i <= 8) {
      value = (value << 4) | (uint32_t) digit;
    }
  }
  *native_tile = value;
  return true;
}

bool fc_agent_v2_target_slot_matches(const char *left, const char *right)
{
  uint32_t ignored;
  unsigned int difference = 0;
  size_t i;

  if (!fc_agent_v2_parse_target_slot(left, &ignored)
      || !fc_agent_v2_parse_target_slot(right, &ignored)) {
    return false;
  }
  for (i = 0; i < 25; i++) {
    difference |= (unsigned char) left[i] ^ (unsigned char) right[i];
  }
  return difference == 0;
}

enum fc_agent_v2_completion
fc_agent_v2_classify_completion(bool request_processed,
                                bool postcondition_met)
{
  if (!request_processed) {
    return FC_AGENT_V2_COMPLETION_WAITING;
  }
  return postcondition_met ? FC_AGENT_V2_COMPLETION_APPLIED
                           : FC_AGENT_V2_COMPLETION_REJECTED;
}

bool fc_agent_v2_epoch_changed(
  bool previous_known,
  const struct fc_agent_v2_epoch_identity *previous,
  const struct fc_agent_v2_epoch_identity *current)
{
  if (!previous_known) {
    return current->authorized;
  }
  if (previous->authorized != current->authorized) {
    return true;
  }
  return current->authorized
         && (previous->player != current->player
             || previous->player_number != current->player_number
             || previous->map_tiles != current->map_tiles
             || previous->map_xsize != current->map_xsize
             || previous->map_ysize != current->map_ysize
             || previous->map_topology != current->map_topology
             || previous->map_wrap != current->map_wrap
             || previous->game_epoch != current->game_epoch);
}

bool fc_agent_v2_boundary_ready(bool authorized, int processing_request_id,
                                bool agents_are_busy)
{
  return authorized && processing_request_id == 0 && !agents_are_busy;
}

bool fc_agent_v2_stream_notification_allowed(bool pending_active)
{
  /* ACT_ACCEPTED and its correlated ACT_RESULT form one stream boundary. */
  return !pending_active;
}

bool fc_agent_v2_agents_busy_if_ready(bool client_cache_initialized,
                                      bool (*agents_are_busy)(void))
{
  return client_cache_initialized
         && (agents_are_busy == NULL || agents_are_busy());
}

bool fc_agent_v2_action_phase_ready(bool authorized, bool coherent,
                                    bool can_issue_orders, bool is_alive,
                                    bool is_active_phase, bool phase_done,
                                    bool server_is_busy)
{
  return authorized && coherent && can_issue_orders && is_alive
         && is_active_phase && !phase_done && !server_is_busy;
}

const char *fc_agent_v2_phase_mode_name(enum fc_agent_v2_phase_mode mode)
{
  switch (mode) {
  case FC_AGENT_V2_PHASE_CONCURRENT:
    return "concurrent";
  case FC_AGENT_V2_PHASE_PLAYERS_ALTERNATE:
    return "players_alternate";
  case FC_AGENT_V2_PHASE_TEAMS_ALTERNATE:
    return "teams_alternate";
  }
  return NULL;
}

bool fc_agent_v2_build_phase_evidence(
  int mode, int player_count, int team_count, int turn, int phase,
  int player_number, int team_number, bool running, bool authorized,
  bool alive, bool phase_done, bool can_end_turn,
  struct fc_agent_v2_phase_evidence *evidence)
{
  enum fc_agent_v2_phase_mode normalized;
  int phase_count;
  bool active;

  /* These are protocol bounds in Freeciv's common types.  Keeping them here
   * lets this strict codec remain independently testable. */
  if (evidence == NULL || !running || !authorized || turn <= 0
      || player_count <= 0 || player_count > 512
      || team_count < 0 || team_count > 513
      || player_number < 0 || player_number >= 512) {
    return false;
  }

  switch (mode) {
  case FC_AGENT_V2_PHASE_CONCURRENT:
    normalized = FC_AGENT_V2_PHASE_CONCURRENT;
    phase_count = 1;
    active = true;
    break;
  case FC_AGENT_V2_PHASE_PLAYERS_ALTERNATE:
    normalized = FC_AGENT_V2_PHASE_PLAYERS_ALTERNATE;
    phase_count = player_count;
    active = player_number == phase;
    break;
  case FC_AGENT_V2_PHASE_TEAMS_ALTERNATE:
    if (team_count <= 0 || team_number < 0 || team_number >= team_count) {
      return false;
    }
    normalized = FC_AGENT_V2_PHASE_TEAMS_ALTERNATE;
    phase_count = team_count;
    active = team_number == phase;
    break;
  default:
    return false;
  }
  if (phase_count <= 0 || phase < 0 || phase >= phase_count
      || (can_end_turn && (!alive || !active || phase_done))) {
    return false;
  }

  evidence->mode = normalized;
  evidence->turn = turn;
  evidence->phase = phase;
  evidence->phase_count = phase_count;
  evidence->active_phase = active;
  evidence->alive = alive;
  evidence->phase_done = phase_done;
  evidence->phase_ready = can_end_turn;
  return true;
}

size_t fc_agent_v2_phase_end_action_count(
  const struct fc_agent_v2_phase_evidence *evidence)
{
  return evidence != NULL && evidence->phase_ready ? 1 : 0;
}

bool fc_agent_v2_format_phase_available(
  char *buffer, size_t buffer_size, uint64_t revision,
  const struct fc_agent_v2_phase_evidence *evidence)
{
  const char *mode;
  int length;

  if (buffer == NULL || buffer_size == 0 || revision == 0
      || evidence == NULL
      || (mode = fc_agent_v2_phase_mode_name(evidence->mode)) == NULL
      || evidence->turn <= 0 || evidence->phase_count <= 0
      || evidence->phase < 0 || evidence->phase >= evidence->phase_count
      || (evidence->phase_ready
          && (!evidence->alive || !evidence->active_phase
              || evidence->phase_done))) {
    return false;
  }
  length = snprintf(
    buffer, buffer_size,
    "PHASE_AVAILABLE\t%llu\t%d\t%d\t%s\t%d\t%d\t%d\t%d\t%d",
    (unsigned long long) revision, evidence->turn, evidence->phase, mode,
    evidence->phase_count, evidence->active_phase ? 1 : 0,
    evidence->alive ? 1 : 0, evidence->phase_done ? 1 : 0,
    evidence->phase_ready ? 1 : 0);
  return length >= 0 && (size_t) length < buffer_size;
}

static bool v2_phase_evidence_equal(
  const struct fc_agent_v2_phase_evidence *left,
  const struct fc_agent_v2_phase_evidence *right)
{
  return left->mode == right->mode
         && left->turn == right->turn
         && left->phase == right->phase
         && left->phase_count == right->phase_count
         && left->active_phase == right->active_phase
         && left->alive == right->alive
         && left->phase_done == right->phase_done
         && left->phase_ready == right->phase_ready;
}

bool fc_agent_v2_phase_notice_needed(
  const struct fc_agent_v2_phase_notice *notice, uint64_t seat_epoch,
  uint64_t revision, const struct fc_agent_v2_phase_evidence *evidence)
{
  if (notice == NULL || evidence == NULL || revision == 0
      || fc_agent_v2_phase_mode_name(evidence->mode) == NULL
      || evidence->turn <= 0 || evidence->phase < 0
      || evidence->phase_count <= 0
      || evidence->phase >= evidence->phase_count) {
    return false;
  }
  /* Phase evidence is part of native revision identity. A changed tuple at
   * the same revision is a contradiction and must never be emitted as a
   * second notice; the Python side intentionally keeps the same guard. */
  return !notice->valid || notice->seat_epoch != seat_epoch
         || notice->revision != revision;
}

bool fc_agent_v2_phase_revision_changed(
  bool running, bool have_current,
  const struct fc_agent_v2_phase_evidence *current,
  const struct fc_agent_v2_phase_evidence *next)
{
  return running && next != NULL
         && (!have_current || current == NULL
             || !v2_phase_evidence_equal(current, next));
}

void fc_agent_v2_phase_notice_record(
  struct fc_agent_v2_phase_notice *notice, uint64_t seat_epoch,
  uint64_t revision, const struct fc_agent_v2_phase_evidence *evidence)
{
  if (notice == NULL || evidence == NULL) {
    return;
  }
  notice->valid = true;
  notice->seat_epoch = seat_epoch;
  notice->revision = revision;
  notice->evidence = *evidence;
}

bool fc_agent_v2_callback_matches(bool pending_active,
                                  int pending_request_id,
                                  uint64_t pending_nonce,
                                  int callback_request_id,
                                  uint64_t callback_nonce)
{
  return pending_active && pending_request_id == callback_request_id
         && pending_nonce == callback_nonce;
}

static int v2_next_request_id(int request_id)
{
  int next = request_id + 1;

  return (next & 0xffff) == 0 ? 2 : next;
}

bool fc_agent_v2_request_group_exact(int before_request_id,
                                     int first_request_id,
                                     int last_request_id,
                                     size_t request_count)
{
  if (request_count != 1 && request_count != 2) {
    return false;
  }
  if (first_request_id != v2_next_request_id(before_request_id)) {
    return false;
  }
  return last_request_id == (request_count == 1
                             ? first_request_id
                             : v2_next_request_id(first_request_id));
}

size_t fc_agent_v2_expected_request_count(enum agent_v2_action_kind kind)
{
  switch (kind) {
  case AGENT_V2_ACTION_MOVE:
  case AGENT_V2_ACTION_ATTACK:
  case AGENT_V2_ACTION_FOUND_CITY:
  case AGENT_V2_ACTION_WORKER_START:
  case AGENT_V2_ACTION_CANCEL_ACTIVITY:
  case AGENT_V2_ACTION_UNIT_SENTRY:
  case AGENT_V2_ACTION_UNIT_FORTIFY:
  case AGENT_V2_ACTION_UNIT_CONVERT:
  case AGENT_V2_ACTION_UNIT_DISBAND:
  case AGENT_V2_ACTION_UNIT_HOMELESS:
  case AGENT_V2_ACTION_UNIT_UPGRADE:
  case AGENT_V2_ACTION_UNIT_REHOME:
  case AGENT_V2_ACTION_UNIT_JOIN_CITY:
  case AGENT_V2_ACTION_UNIT_ESTABLISH_TRADE:
  case AGENT_V2_ACTION_UNIT_MARKETPLACE:
  case AGENT_V2_ACTION_UNIT_HELP_WONDER:
  case AGENT_V2_ACTION_UNIT_DISBAND_RECOVER:
  case AGENT_V2_ACTION_UNIT_AIRLIFT:
  case AGENT_V2_ACTION_UNIT_PARADROP:
  case AGENT_V2_ACTION_UNIT_TELEPORT:
  case AGENT_V2_ACTION_TRANSPORT_BOARD:
  case AGENT_V2_ACTION_TRANSPORT_DEBOARD:
  case AGENT_V2_ACTION_TRANSPORT_EMBARK:
  case AGENT_V2_ACTION_TRANSPORT_DISEMBARK:
  case AGENT_V2_ACTION_TRANSPORT_LOAD:
  case AGENT_V2_ACTION_TRANSPORT_UNLOAD:
  case AGENT_V2_ACTION_UNIT_CANCEL_AUTOMATION:
  case AGENT_V2_ACTION_UNIT_CANCEL_ORDERS:
  case AGENT_V2_ACTION_UNIT_GOTO:
  case AGENT_V2_ACTION_UNIT_GOTO_AND_PERFORM:
  case AGENT_V2_ACTION_UNIT_CONNECT_ROUTE:
  case AGENT_V2_ACTION_UNIT_SET_ROUTE:
  case AGENT_V2_ACTION_UNIT_SPECIAL:
    return 2;
  case AGENT_V2_ACTION_PREGAME_CONFIGURE:
  case AGENT_V2_ACTION_PREGAME_SET_TEAM:
  case AGENT_V2_ACTION_PREGAME_SET_READY:
  case AGENT_V2_ACTION_PLAYER_CAST_VOTE:
  case AGENT_V2_ACTION_PHASE_END:
  case AGENT_V2_ACTION_RESEARCH_TARGET:
  case AGENT_V2_ACTION_RESEARCH_GOAL:
  case AGENT_V2_ACTION_ECONOMY_RATES:
  case AGENT_V2_ACTION_PLAYER_SEND_CHAT:
  case AGENT_V2_ACTION_CITY_PRODUCTION:
  case AGENT_V2_ACTION_CITY_BUY:
  case AGENT_V2_ACTION_CITY_WORK_TILE:
  case AGENT_V2_ACTION_CITY_UNWORK_TILE:
  case AGENT_V2_ACTION_CITY_SET_SPECIALIST:
  case AGENT_V2_ACTION_PLAYER_PLACE_INFRA:
  case AGENT_V2_ACTION_CITY_SET_WORKLIST:
  case AGENT_V2_ACTION_CITY_SET_OPTIONS:
  case AGENT_V2_ACTION_CITY_RENAME:
  case AGENT_V2_ACTION_CITY_SELL_IMPROVEMENT:
  case AGENT_V2_ACTION_CITY_SET_RALLY:
  case AGENT_V2_ACTION_CITY_CLEAR_RALLY:
  case AGENT_V2_ACTION_CITY_REQUEST_WORKER_TASK:
  case AGENT_V2_ACTION_CITY_CHANGE_WORKER_TASK:
  case AGENT_V2_ACTION_CITY_REMOVE_WORKER_TASK:
  case AGENT_V2_ACTION_UNIT_AUTO_WORK:
  case AGENT_V2_ACTION_UNIT_AUTO_EXPLORE:
  case AGENT_V2_ACTION_GOVERNMENT_REVOLUTION:
  case AGENT_V2_ACTION_GOVERNMENT_CHANGE:
  case AGENT_V2_ACTION_MULTIPLIER_SET:
  case AGENT_V2_ACTION_SPACESHIP_PLACE:
  case AGENT_V2_ACTION_SPACESHIP_LAUNCH:
  case AGENT_V2_ACTION_DIPLOMACY_OPEN_MEETING:
  case AGENT_V2_ACTION_DIPLOMACY_CLOSE_MEETING:
  case AGENT_V2_ACTION_DIPLOMACY_PROPOSE_CLAUSE:
  case AGENT_V2_ACTION_DIPLOMACY_REMOVE_CLAUSE:
  case AGENT_V2_ACTION_DIPLOMACY_ACCEPT:
  case AGENT_V2_ACTION_DIPLOMACY_WITHDRAW_ACCEPTANCE:
  case AGENT_V2_ACTION_DIPLOMACY_BREAK_RELATION:
  case AGENT_V2_ACTION_DIPLOMACY_WITHDRAW_VISION:
  case AGENT_V2_ACTION_DIPLOMACY_WITHDRAW_SHARED_TILES:
    return 1;
  case AGENT_V2_ACTION_CITY_SET_GOVERNOR:
  case AGENT_V2_ACTION_CITY_CLEAR_GOVERNOR:
    /* Native CMA is a synchronous local client agent. It may emit zero or
     * an arbitrary contiguous group of city requests and waits internally. */
    return 0;
  case AGENT_V2_ACTION_KIND_COUNT:
    break;
  }
  return 0;
}

bool fc_agent_v2_unit_automation_latch_matches(
  enum fc_agent_v2_automation_command command,
  bool pending_active, bool seat_epoch_current,
  int packet_request_id, int expected_request_id,
  bool owned, int packet_unit_id, int expected_unit_id,
  uint64_t packet_lifecycle, uint64_t expected_lifecycle,
  uint64_t packet_incarnation, uint64_t expected_incarnation,
  enum fc_agent_v2_automation_controller controller,
  bool activity_explore, bool activity_target_none)
{
  return command == FC_AGENT_V2_AUTOMATION_EXPLORE
         && pending_active && seat_epoch_current
         && packet_request_id == expected_request_id && owned
         && packet_unit_id == expected_unit_id
         && packet_lifecycle != 0
         && packet_lifecycle == expected_lifecycle
         && packet_incarnation != 0
         && packet_incarnation == expected_incarnation
         && controller == FC_AGENT_V2_CONTROLLER_AUTO_EXPLORE
         && activity_explore && activity_target_none;
}

bool fc_agent_v2_unit_automation_postcondition(
  enum fc_agent_v2_automation_command command,
  uint64_t expected_lifecycle,
  bool before_present, uint64_t before_lifecycle,
  enum fc_agent_v2_automation_controller before_controller,
  bool before_idle, bool before_target_none,
  bool before_has_orders, bool before_goto_none,
  bool current_present, uint64_t current_lifecycle,
  enum fc_agent_v2_automation_controller current_controller,
  bool current_idle, bool current_explore, bool current_target_none,
  bool exact_explore_latch)
{
  if (expected_lifecycle == 0 || !before_present
      || before_lifecycle != expected_lifecycle) {
    return false;
  }
  if (command == FC_AGENT_V2_AUTOMATION_CANCEL) {
    return (before_controller == FC_AGENT_V2_CONTROLLER_AUTO_WORK
            || before_controller == FC_AGENT_V2_CONTROLLER_AUTO_EXPLORE)
           && current_present && current_lifecycle == expected_lifecycle
           && current_controller == FC_AGENT_V2_CONTROLLER_NONE
           && current_idle && current_target_none;
  }
  if (before_controller != FC_AGENT_V2_CONTROLLER_NONE
      || !before_idle || !before_target_none
      || before_has_orders || !before_goto_none) {
    return false;
  }
  if (command == FC_AGENT_V2_AUTOMATION_EXPLORE
      && exact_explore_latch) {
    return true;
  }
  if (!current_present || current_lifecycle != expected_lifecycle) {
    return false;
  }
  if (command == FC_AGENT_V2_AUTOMATION_WORK) {
    return current_controller == FC_AGENT_V2_CONTROLLER_AUTO_WORK;
  }
  return command == FC_AGENT_V2_AUTOMATION_EXPLORE
         && current_controller == FC_AGENT_V2_CONTROLLER_AUTO_EXPLORE
         && current_explore && current_target_none;
}

bool fc_agent_v2_unit_cancel_orders_postcondition(
  uint64_t expected_lifecycle,
  bool before_present, uint64_t before_lifecycle,
  int expected_source_tile, int before_tile,
  enum fc_agent_v2_automation_controller before_controller,
  bool before_idle, bool before_target_none,
  bool before_has_orders, bool before_goto_none,
  bool current_present, uint64_t current_lifecycle,
  bool current_has_orders, bool current_goto_none)
{
  (void) before_goto_none;
  return expected_lifecycle != 0
         && before_present && before_lifecycle == expected_lifecycle
         && expected_source_tile >= 0 && before_tile == expected_source_tile
         && before_controller == FC_AGENT_V2_CONTROLLER_NONE
         && before_idle && before_target_none
         && before_has_orders
         && current_present && current_lifecycle == expected_lifecycle
         && !current_has_orders && current_goto_none;
}

bool fc_agent_v2_worker_task_echo_matches(
  bool pending_active, bool baseline_captured, bool seat_epoch_current,
  int packet_request_id, int expected_request_id,
  int packet_city, int expected_city,
  int packet_tile, int expected_tile,
  int packet_activity, int expected_activity,
  int packet_extra, int expected_extra,
  int packet_want, int expected_want,
  bool exact_cache_state)
{
  return pending_active && baseline_captured && seat_epoch_current
         && packet_request_id == expected_request_id
         && packet_city == expected_city && packet_tile == expected_tile
         && packet_activity == expected_activity
         && packet_extra == expected_extra && packet_want == expected_want
         && exact_cache_state;
}

bool fc_agent_v2_rally_state_canonical(
  bool active, bool persistent, bool vigilant,
  int order_count, uint64_t orders_digest)
{
  if (!active) {
    return !persistent && !vigilant && order_count == 0
           && orders_digest == 0;
  }
  return order_count > 0 && order_count < FC_AGENT_V2_MAX_RALLY_ORDERS;
}

bool fc_agent_v2_rally_postcondition(
  uint64_t expected_lifecycle,
  bool before_present, uint64_t before_lifecycle,
  int expected_source_tile, int before_source_tile,
  bool current_present, uint64_t current_lifecycle,
  int current_source_tile, bool desired_active,
  bool desired_persistent, int desired_order_count,
  uint64_t desired_orders_digest,
  bool current_active, bool current_persistent,
  bool current_vigilant, int current_order_count,
  uint64_t current_orders_digest)
{
  if (expected_lifecycle == 0 || !before_present
      || before_lifecycle != expected_lifecycle
      || expected_source_tile < 0 || before_source_tile != expected_source_tile
      || !current_present || current_lifecycle != expected_lifecycle
      || current_source_tile != expected_source_tile
      || !fc_agent_v2_rally_state_canonical(
           current_active, current_persistent, current_vigilant,
           current_order_count, current_orders_digest)) {
    return false;
  }
  if (!desired_active) {
    return !current_active && !desired_persistent && !current_vigilant
           && current_order_count == 0 && current_orders_digest == 0;
  }
  return desired_order_count > 0
         && desired_order_count < FC_AGENT_V2_MAX_RALLY_ORDERS
         && current_active && current_persistent == desired_persistent
         && !current_vigilant && current_order_count == desired_order_count
         && current_orders_digest == desired_orders_digest;
}

enum fc_agent_v2_terminal_result fc_agent_v2_rally_terminal(
  bool seat_epoch_current, bool processing_started,
  bool baseline_captured, bool last_started,
  bool postcondition_met)
{
  if (!seat_epoch_current) {
    return FC_AGENT_V2_TERMINAL_SEAT_EPOCH_CHANGED;
  }
  if (!processing_started || !baseline_captured || !last_started
      || !postcondition_met) {
    /* Once the forced rally packet escaped, absence of an exact echo can no
     * longer distinguish loss, capture, malformed replacement, or a delayed
     * cache update.  It is terminally ambiguous, never a clean rejection. */
    return FC_AGENT_V2_TERMINAL_PROCESSING_BOUNDARY_MISMATCH;
  }
  return FC_AGENT_V2_TERMINAL_APPLIED;
}

bool fc_agent_v2_unit_goto_postcondition(
  uint64_t expected_lifecycle,
  bool before_present, uint64_t before_lifecycle,
  int expected_source_tile, int before_tile,
  enum fc_agent_v2_automation_controller before_controller,
  bool before_idle, bool before_target_none,
  bool before_has_orders, bool before_goto_none,
  bool before_untransported, bool before_cargo_empty,
  int expected_target_tile,
  bool current_present, uint64_t current_lifecycle,
  int current_tile, bool current_has_orders, int current_goto_tile)
{
  bool queued;
  bool completed;

  if (expected_lifecycle == 0
      || !before_present || before_lifecycle != expected_lifecycle
      || expected_source_tile < 0 || before_tile != expected_source_tile
      || before_controller != FC_AGENT_V2_CONTROLLER_NONE
      || !before_idle || !before_target_none
      || before_has_orders || !before_goto_none
      || !before_untransported || !before_cargo_empty
      || expected_target_tile < 0
      || expected_target_tile == expected_source_tile
      || !current_present || current_lifecycle != expected_lifecycle) {
    return false;
  }
  queued = current_has_orders && current_goto_tile == expected_target_tile;
  completed = current_tile == expected_target_tile
              && !current_has_orders && current_goto_tile < 0;
  return queued || completed;
}

bool fc_agent_v2_unit_route_postcondition(
  uint64_t expected_lifecycle,
  bool before_present, uint64_t before_lifecycle,
  int expected_source_tile, int before_tile,
  enum fc_agent_v2_automation_controller before_controller,
  bool before_idle, bool before_target_none,
  bool before_has_orders, bool before_goto_none,
  bool before_untransported, bool before_cargo_empty,
  int expected_destination_tile, int expected_order_count,
  uint64_t expected_orders_digest, bool expected_repeat,
  bool expected_vigilant,
  bool current_present, uint64_t current_lifecycle,
  int current_tile, bool current_has_orders, int current_goto_tile,
  int current_order_count, uint64_t current_orders_digest,
  bool current_repeat, bool current_vigilant)
{
  bool queued;
  bool completed;

  if (expected_lifecycle == 0
      || !before_present || before_lifecycle != expected_lifecycle
      || expected_source_tile < 0 || before_tile != expected_source_tile
      || before_controller != FC_AGENT_V2_CONTROLLER_NONE
      || !before_idle || !before_target_none
      || before_has_orders || !before_goto_none
      || !before_untransported || !before_cargo_empty
      || expected_destination_tile < 0
      || (!expected_repeat
          && expected_destination_tile == expected_source_tile)
      || expected_order_count < 1
      || expected_orders_digest == 0
      || !current_present || current_lifecycle != expected_lifecycle) {
    return false;
  }
  queued = current_has_orders
           && current_order_count == expected_order_count
           && current_orders_digest == expected_orders_digest
           && current_repeat == expected_repeat
           && current_vigilant == expected_vigilant
           && (expected_repeat
               ? current_goto_tile < 0
               : current_goto_tile == expected_destination_tile);
  completed = !expected_repeat
              && current_tile == expected_destination_tile
              && !current_has_orders && current_goto_tile < 0;
  return queued || completed;
}

bool fc_agent_v2_unit_route_install_postcondition(
  uint64_t expected_lifecycle,
  bool before_present, uint64_t before_lifecycle,
  int expected_source_tile, int before_tile,
  enum fc_agent_v2_automation_controller before_controller,
  bool before_idle, bool before_target_none,
  bool before_has_orders, bool before_goto_none,
  bool before_untransported, bool before_cargo_empty,
  int expected_destination_tile, int expected_order_count,
  uint64_t expected_orders_digest, bool expected_repeat,
  bool expected_vigilant, bool allow_same_destination,
  bool exact_install_latched)
{
  if (!exact_install_latched || expected_lifecycle == 0
      || !before_present || before_lifecycle != expected_lifecycle
      || expected_source_tile < 0 || before_tile != expected_source_tile
      || before_controller != FC_AGENT_V2_CONTROLLER_NONE
      || !before_idle || !before_target_none
      || before_has_orders || !before_goto_none
      || !before_untransported || !before_cargo_empty
      || expected_destination_tile < 0
      || (!expected_repeat && !allow_same_destination
          && expected_destination_tile == expected_source_tile)
      || expected_order_count < 1 || expected_orders_digest == 0) {
    return false;
  }
  return true;
}

bool fc_agent_v2_unit_route_shape_matches(
  enum agent_v2_action_kind kind, bool action_move,
  int final_action, int final_subtarget,
  int action_none, int no_target,
  int expected_action, int expected_subtarget)
{
  switch (kind) {
  case AGENT_V2_ACTION_UNIT_GOTO:
    /* A normal goto may end in ACTION_MOVE, but never appends a separate
     * perform-action descriptor. */
    return final_action == action_none && final_subtarget == no_target;
  case AGENT_V2_ACTION_UNIT_GOTO_AND_PERFORM:
    return !action_move && expected_action != action_none
           && expected_subtarget == no_target
           && final_action == expected_action
           && final_subtarget == expected_subtarget;
  case AGENT_V2_ACTION_UNIT_CONNECT_ROUTE:
    return !action_move && expected_action != action_none
           && expected_subtarget >= 0
           && final_action == expected_action
           && final_subtarget == expected_subtarget;
  default:
    return false;
  }
}

bool fc_agent_v2_infrastructure_postcondition(
  int before_points, int cost, int current_points,
  bool before_unplaced, int expected_extra, int current_placing_extra)
{
  return before_points >= 0 && cost > 0 && before_points >= cost
         && before_unplaced && expected_extra >= 0
         && current_points == before_points - cost
         && current_placing_extra == expected_extra;
}

bool fc_agent_v2_hut_transition_postcondition(
  bool baseline_exact, bool baseline_hut_present,
  bool baseline_hut_removed, uint64_t expected_unit_lifecycle,
  bool current_unit_present, uint64_t current_unit_lifecycle,
  int current_unit_tile, int target_tile)
{
  return baseline_exact && baseline_hut_present && baseline_hut_removed
         && (!current_unit_present
             || (expected_unit_lifecycle != 0
                 && current_unit_lifecycle == expected_unit_lifecycle
                 && current_unit_tile == target_tile));
}

bool fc_agent_v2_conquer_extras_postcondition(
  bool baseline_exact, int before_owner, int self_player,
  int current_owner, uint64_t expected_unit_lifecycle,
  bool current_unit_present, uint64_t current_unit_lifecycle,
  int current_unit_tile, int target_tile)
{
  return baseline_exact && self_player >= 0 && before_owner != self_player
         && current_owner == self_player
         && (!current_unit_present
             || (expected_unit_lifecycle != 0
                 && current_unit_lifecycle == expected_unit_lifecycle
                 && current_unit_tile == target_tile));
}

bool fc_agent_v2_espionage_effect_postcondition(
  bool target_binding_exact, bool effect_baseline_exact,
  uint64_t expected_city_lifecycle,
  bool before_city_present, uint64_t before_city_lifecycle,
  bool current_city_present, uint64_t current_city_lifecycle,
  bool effect_transition_proven)
{
  return target_binding_exact && effect_baseline_exact
         && expected_city_lifecycle != 0
         && before_city_present
         && before_city_lifecycle == expected_city_lifecycle
         && current_city_present
         && current_city_lifecycle == expected_city_lifecycle
         && effect_transition_proven;
}

bool fc_agent_v2_targeted_sabotage_postcondition(
  bool target_binding_exact, bool success_receipt_latched,
  bool building_externally_visible,
  uint64_t expected_city_lifecycle,
  bool before_city_present, uint64_t before_city_lifecycle,
  bool current_city_present, uint64_t current_city_lifecycle,
  bool before_building_present, bool current_building_present)
{
  return target_binding_exact && success_receipt_latched
         && expected_city_lifecycle != 0
         && before_city_present
         && before_city_lifecycle == expected_city_lifecycle
         && current_city_present
         && current_city_lifecycle == expected_city_lifecycle
         && (!building_externally_visible
             || (before_building_present && !current_building_present));
}

bool fc_agent_v2_city_espionage_event_matches(
  bool active, bool processing_started, bool baseline_captured,
  bool seat_epoch_current, uint64_t frozen_revision,
  bool exact_action_family, bool actor_binding_exact,
  bool city_binding_exact, int observed_request, int expected_request,
  int observed_tile, int expected_tile,
  int observed_event, int expected_event)
{
  return active && processing_started && baseline_captured
         && seat_epoch_current && frozen_revision != 0
         && exact_action_family && actor_binding_exact
         && city_binding_exact && observed_request > 0
         && observed_request == expected_request
         && observed_tile >= 0 && observed_tile == expected_tile
         && observed_event == expected_event;
}

bool fc_agent_v2_action_receipt_matches(
  bool active, bool processing_started, bool baseline_captured,
  bool seat_epoch_current, bool terminal_clear,
  bool actor_binding_exact, bool target_binding_exact,
  int observed_request, int expected_request,
  int observed_request_kind, int expected_request_kind,
  int observed_actor, int expected_actor,
  int observed_target, int expected_target,
  int observed_action, int expected_action, int observed_status)
{
  return active && processing_started && baseline_captured
         && seat_epoch_current && terminal_clear
         && actor_binding_exact && target_binding_exact
         && observed_request > 0 && observed_request == expected_request
         && expected_request_kind >= 0
         && observed_request_kind == expected_request_kind
         && observed_actor >= 0 && observed_actor == expected_actor
         && observed_target >= 0 && observed_target == expected_target
         && observed_action >= 0 && observed_action == expected_action
         && observed_status == 1;
}

bool fc_agent_v2_poison_city_postcondition(
  bool target_binding_exact, bool success_receipt_latched,
  uint64_t expected_city_lifecycle,
  bool before_city_present, uint64_t before_city_lifecycle,
  int before_city_size, bool current_city_present,
  bool current_city_binding_exact, uint64_t current_city_lifecycle,
  int current_city_size)
{
  if (!target_binding_exact || !success_receipt_latched
      || expected_city_lifecycle == 0 || !before_city_present
      || before_city_lifecycle != expected_city_lifecycle
      || before_city_size <= 0) {
    return false;
  }
  if (!current_city_present) {
    /* Poison can destroy only a size-one city.  The request-bound success
     * receipt plus its disappearance is the normal client's exact observable
     * transition for that server-confirmed outcome. */
    return before_city_size == 1;
  }
  return current_city_binding_exact
         && current_city_lifecycle == expected_city_lifecycle
         && current_city_size == before_city_size - 1;
}

bool fc_agent_v2_sabotage_city_postcondition(
  bool target_binding_exact, bool success_receipt_latched,
  uint64_t expected_city_lifecycle,
  bool before_city_present, uint64_t before_city_lifecycle,
  bool current_city_present, bool current_city_binding_exact,
  uint64_t current_city_lifecycle, bool visible_effect_corroborated)
{
  /* Foreign improvements and shield stock are normally hidden.  A visible
   * removal/decrease is useful corroboration, but the exact request-bound
   * structured receipt is the authoritative positive evidence. */
  (void) visible_effect_corroborated;
  return target_binding_exact && success_receipt_latched
         && expected_city_lifecycle != 0 && before_city_present
         && before_city_lifecycle == expected_city_lifecycle
         && current_city_present && current_city_binding_exact
         && current_city_lifecycle == expected_city_lifecycle;
}

bool fc_agent_v2_combat_observer_matches(
  bool active, bool baseline_captured, bool seat_epoch_current,
  bool classic_combat_action, bool actor_binding_exact,
  int observed_request, int expected_request,
  int observed_attacker, int expected_actor,
  int observed_defender, bool defender_on_expected_target)
{
  return active && baseline_captured && seat_epoch_current
         && classic_combat_action && actor_binding_exact
         && observed_request > 0
         && observed_request == expected_request
         && observed_attacker >= 0
         && observed_attacker == expected_actor
         && observed_defender >= 0
         && observed_defender != expected_actor
         && defender_on_expected_target;
}

bool fc_agent_v2_spy_attack_postcondition(
  bool target_binding_exact, uint64_t expected_actor_lifecycle,
  bool before_actor_present, uint64_t before_actor_lifecycle,
  bool actor_loss_event, bool target_loss_event,
  bool current_actor_present, bool current_actor_binding_exact,
  uint64_t before_stack_signature, uint64_t current_stack_signature)
{
  bool baseline_exact = target_binding_exact
                        && expected_actor_lifecycle != 0
                        && before_actor_present
                        && before_actor_lifecycle
                           == expected_actor_lifecycle;

  if (!baseline_exact || actor_loss_event == target_loss_event) {
    return false;
  }
  if (actor_loss_event) {
    return !current_actor_present;
  }
  return current_actor_present && current_actor_binding_exact
         && before_stack_signature != current_stack_signature;
}

bool fc_agent_v2_sabotage_unit_postcondition(
  bool target_binding_exact, bool success_event_latched,
  uint64_t expected_target_lifecycle,
  bool before_target_present, uint64_t before_target_lifecycle,
  int before_target_hp, bool current_target_present,
  bool current_target_binding_exact, int current_target_hp)
{
  if (!target_binding_exact || !success_event_latched
      || expected_target_lifecycle == 0 || !before_target_present
      || before_target_lifecycle != expected_target_lifecycle
      || before_target_hp <= 0) {
    return false;
  }
  if (!current_target_present) {
    return true;
  }
  return current_target_binding_exact
         && current_target_hp >= 0
         && current_target_hp < before_target_hp;
}

bool fc_agent_v2_nuke_observer_matches(
  bool active, bool baseline_captured, bool seat_epoch_current,
  bool classic_nuke_action, int observed_request, int expected_request,
  int observed_tile, int expected_tile)
{
  return active && baseline_captured && seat_epoch_current
         && classic_nuke_action
         && observed_request > 0
         && observed_request == expected_request
         && observed_tile >= 0
         && observed_tile == expected_tile;
}

bool fc_agent_v2_nuke_stack_binding_matches(
  bool stack_target_action, uint64_t frozen_signature,
  uint64_t current_signature)
{
  return stack_target_action
         ? frozen_signature == current_signature
         : frozen_signature == 0;
}

bool fc_agent_v2_nuke_postcondition(
  bool target_binding_exact, bool nuke_tile_info_latched,
  uint64_t expected_actor_lifecycle,
  bool before_actor_present, uint64_t before_actor_lifecycle,
  bool current_actor_present)
{
  return target_binding_exact && nuke_tile_info_latched
         && fc_agent_v2_unit_consumed_postcondition(
              expected_actor_lifecycle,
              before_actor_present, before_actor_lifecycle,
              current_actor_present);
}

bool fc_agent_v2_goto_candidate_precedes(
  int left_distance, int left_tile,
  int right_distance, int right_tile)
{
  return left_distance < right_distance
         || (left_distance == right_distance && left_tile < right_tile);
}

bool fc_agent_v2_city_target_distance_candidate(
  int distance, bool unlimited, int maximum_distance)
{
  return distance >= 0
         && (unlimited
             || (maximum_distance >= 0 && distance <= maximum_distance));
}

enum fc_agent_v2_terminal_result fc_agent_v2_capture_group_terminal(
  bool seat_epoch_current, bool first_started, bool baseline_captured,
  bool first_finished, bool last_started, bool postcondition_met)
{
  if (!seat_epoch_current) {
    return FC_AGENT_V2_TERMINAL_SEAT_EPOCH_CHANGED;
  }
  if (!first_started || !baseline_captured
      || !first_finished || !last_started || !postcondition_met) {
    return FC_AGENT_V2_TERMINAL_PROCESSING_BOUNDARY_MISMATCH;
  }
  return FC_AGENT_V2_TERMINAL_APPLIED;
}

enum fc_agent_v2_terminal_result fc_agent_v2_automation_terminal(
  enum fc_agent_v2_automation_command command,
  bool seat_epoch_current, bool first_started, bool baseline_captured,
  bool first_finished, bool last_started,
  bool exact_start_baseline, bool exact_unit_lifetime_current,
  bool postcondition_met)
{
  bool boundaries = first_started && baseline_captured && last_started
                    && (command != FC_AGENT_V2_AUTOMATION_CANCEL
                        || first_finished);

  if (!seat_epoch_current) {
    return FC_AGENT_V2_TERMINAL_SEAT_EPOCH_CHANGED;
  }
  if (!boundaries) {
    return FC_AGENT_V2_TERMINAL_PROCESSING_BOUNDARY_MISMATCH;
  }
  if (postcondition_met) {
    return FC_AGENT_V2_TERMINAL_APPLIED;
  }
  if (command == FC_AGENT_V2_AUTOMATION_WORK
      && exact_start_baseline && exact_unit_lifetime_current) {
    /* Auto-work does not run inline. With an exact clean baseline and the
     * same unit lifetime at finish, absence of AUTOWORKER proves refusal. */
    return FC_AGENT_V2_TERMINAL_POSTCONDITION_NOT_MET;
  }
  /* A sent SSA request can apply before the actor moves, completes, dies,
   * or a later request in the cancel group fails. Never claim rejection
   * from an inconclusive final cache. */
  return FC_AGENT_V2_TERMINAL_PROCESSING_BOUNDARY_MISMATCH;
}

enum fc_agent_v2_terminal_result fc_agent_v2_consuming_city_terminal(
  bool seat_epoch_current, bool first_started, bool baseline_captured,
  bool first_finished, bool last_started,
  bool exact_actor_present, bool actor_absent,
  bool postcondition_met)
{
  if (!seat_epoch_current) {
    return FC_AGENT_V2_TERMINAL_SEAT_EPOCH_CHANGED;
  }
  if (!first_started || !baseline_captured
      || !first_finished || !last_started) {
    return FC_AGENT_V2_TERMINAL_PROCESSING_BOUNDARY_MISMATCH;
  }
  if (postcondition_met && actor_absent && !exact_actor_present) {
    return FC_AGENT_V2_TERMINAL_APPLIED;
  }
  if (!postcondition_met && exact_actor_present && !actor_absent) {
    /* A consuming city action cannot have applied while the exact tracked
     * actor remains.  Exact request boundaries therefore prove refusal. */
    return FC_AGENT_V2_TERMINAL_POSTCONDITION_NOT_MET;
  }
  /* If the actor vanished but the destination/source semantic proof is
   * incomplete, disappearance alone cannot distinguish application from
   * death, transfer, replacement, or a stale auxiliary cache. */
  return FC_AGENT_V2_TERMINAL_PROCESSING_BOUNDARY_MISMATCH;
}

void fc_agent_v2_capture_terminal(
  enum fc_agent_v2_terminal_result *terminal,
  bool seat_epoch_current, bool processing_started,
  bool baseline_captured, bool postcondition_met)
{
  if (*terminal != FC_AGENT_V2_TERMINAL_NONE) {
    return;
  }
  if (!seat_epoch_current) {
    *terminal = FC_AGENT_V2_TERMINAL_SEAT_EPOCH_CHANGED;
  } else if (!processing_started || !baseline_captured) {
    *terminal = FC_AGENT_V2_TERMINAL_PROCESSING_BOUNDARY_MISMATCH;
  } else if (postcondition_met) {
    *terminal = FC_AGENT_V2_TERMINAL_APPLIED;
  } else {
    *terminal = FC_AGENT_V2_TERMINAL_POSTCONDITION_NOT_MET;
  }
}

enum fc_agent_v2_terminal_result fc_agent_v2_terminal_after_epoch_change(
  enum fc_agent_v2_terminal_result terminal)
{
  return terminal == FC_AGENT_V2_TERMINAL_NONE
         ? FC_AGENT_V2_TERMINAL_SEAT_EPOCH_CHANGED : terminal;
}

bool fc_agent_v2_relation_baseline_matches(
  uint64_t expected_generation, uint64_t current_generation,
  uint64_t expected_clauses_digest, uint64_t current_clauses_digest,
  bool expected_self_accepted, bool current_self_accepted,
  bool expected_other_accepted, bool current_other_accepted,
  int expected_relation_state, int current_relation_state,
  bool expected_outgoing_vision, bool current_outgoing_vision,
  bool expected_outgoing_shared_tiles,
  bool current_outgoing_shared_tiles)
{
  return expected_generation == current_generation
         && expected_clauses_digest == current_clauses_digest
         && expected_self_accepted == current_self_accepted
         && expected_other_accepted == current_other_accepted
         && expected_relation_state == current_relation_state
         && expected_outgoing_vision == current_outgoing_vision
         && expected_outgoing_shared_tiles
            == current_outgoing_shared_tiles;
}

uint64_t fc_agent_v2_take_incarnation(uint64_t *next_incarnation)
{
  uint64_t result = *next_incarnation;

  if (result == 0 || result == UINT64_MAX) {
    return 0;
  }
  *next_incarnation = result + 1;
  return result;
}
