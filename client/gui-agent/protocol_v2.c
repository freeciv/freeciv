/***********************************************************************
 Freeciv - Copyright (C) 1996 - The Freeciv Project

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2, or (at your option)
 any later version.
***********************************************************************/

#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* utility */
#include "log.h"
#include "rand.h"
#include "support.h"
#include "timing.h"
#include "fc_utf8.h"

/* common */
#include "actions.h"
#include "chat.h"
#include "city.h"
#include "diptreaty.h"
#include "effects.h"
#include "extras.h"
#include "featured_text.h"
#include "game.h"
#include "government.h"
#include "improvement.h"
#include "map.h"
#include "metaknowledge.h"
#include "movement.h"
#include "multipliers.h"
#include "connection.h"
#include "nation.h"
#include "player.h"
#include "packets.h"
#include "research.h"
#include "requirements.h"
#include "server_settings.h"
#include "spaceship.h"
#include "specialist.h"
#include "style.h"
#include "tech.h"
#include "team.h"
#include "terrain.h"
#include "tile.h"
#include "unit.h"
#include "unittype.h"

/* client */
#include "agents/agents.h"
#include "agents/cma_core.h"
#include "chatline_common.h"
#include "client_main.h"
#include "climap.h"
#include "clinet.h"
#include "control.h"
#include "citydlg_common.h"
#include "goto.h"
#include "mapctrl_common.h"
#include "packhand.h"
#include "update_queue.h"
#include "voteinfo.h"

#include "ipc_codec.h"
#include "protocol_v2.h"
#include "protocol_v2_codec.h"

#define AGENT_V2_MAX_ROWS 8192
#define AGENT_V2_ROW_MAX 2048
#define AGENT_V2_MAX_ACTIONS FC_AGENT_V2_MAX_ACTIONS
#define AGENT_V2_SCOPE_PINNED FC_AGENT_V2_MAX_PINNED_SCOPES
#define AGENT_V2_STATE_SCOPE_PINNED FC_AGENT_V2_MAX_PINNED_STATE_SCOPES
#define AGENT_V2_RELATION_SCOPE_PINNED \
  FC_AGENT_V2_MAX_PINNED_RELATION_SCOPES
#define AGENT_V2_PINNED 2
#define AGENT_V2_PAGE_MAX FC_AGENT_V2_PAGE_MAX
#define AGENT_V2_MAX_FIELDS 8
#define AGENT_V2_TOKEN_MAX 64
#define AGENT_V2_ACTION_TIMEOUT 15.0
#define AGENT_V2_TARGET_QUERY_TIMEOUT 4.0
#define AGENT_V2_GOTO_MAX_DESTINATIONS 64
#define AGENT_V2_GOTO_MAX_DISTANCE 8
#define AGENT_V2_GOTO_MAX_NEIGHBORHOOD 1024
#define AGENT_V2_MAX_VISIBLE_BRIBE_STACK 256
#define AGENT_V2_CITY_WORKER_TASK_WANT 100
#define AGENT_V2_VOTE_TEAM_ONLY (1U << 2)
#define AGENT_V2_INVESTIGATION_TOKEN_MAX 32
FC_STATIC_ASSERT(CLIENT_UNIT_ROUTE_MAX_WAYPOINTS
                 == FC_AGENT_V2_MAX_UNIT_ROUTE_WAYPOINTS,
                 agent_v2_unit_route_waypoint_limit_mismatch);

enum {
  AGENT_V2_MANIFEST_COUNT = 0
#define AGENT_V2_STATE_CLASS(_row, _authority, _visibility) + 1
#include "state_manifest.def"
#undef AGENT_V2_STATE_CLASS
};

enum agent_v2_entity_kind {
  AGENT_V2_ENTITY_PLAYER,
  AGENT_V2_ENTITY_CITY,
  AGENT_V2_ENTITY_UNIT
};

enum agent_v2_probability_kind {
  AGENT_V2_PROBABILITY_EXACT,
  AGENT_V2_PROBABILITY_RANGE,
  AGENT_V2_PROBABILITY_UNKNOWN,
  AGENT_V2_PROBABILITY_NOT_IMPLEMENTED
};

struct agent_v2_row {
  char text[AGENT_V2_ROW_MAX];
};

struct agent_v2_action {
  enum agent_v2_action_kind kind;
  int player_id;
  uint64_t player_incarnation;
  int unit_id;
  uint64_t unit_incarnation;
  uint64_t unit_lifecycle_id;
  int city_id;
  uint64_t city_incarnation;
  uint64_t city_lifecycle_id;
  int target_tile;
  int source_unit_tile;
  int goto_destination_tile;
  int source_unit_moves;
  bool source_unit_paradropped;
  bool special_target_known_seen;
  int special_target_city_id;
  uint64_t special_target_city_incarnation;
  uint64_t special_target_city_lifecycle_id;
  int special_target_city_owner;
  int special_target_extra_owner;
  bv_extras special_target_extras;
  bv_extras special_target_hut_extras;
  int goto_order_count;
  uint64_t goto_orders_digest;
  uint64_t goto_route_signature;
  bool goto_action_move;
  int route_waypoint_limit;
  int rally_source_tile;
  int rally_production_unit_type;
  int rally_veteran_level;
  int rally_order_count;
  uint64_t rally_orders_digest;
  bool rally_action_move;
  int source_city_id;
  uint64_t source_city_incarnation;
  uint64_t source_city_lifecycle_id;
  int source_city_tile;
  int destination_city_id;
  uint64_t destination_city_incarnation;
  uint64_t destination_city_lifecycle_id;
  int destination_city_tile;
  int target_unit_id;
  uint64_t target_unit_incarnation;
  uint64_t target_unit_lifecycle_id;
  int transport_context_id;
  uint64_t transport_context_incarnation;
  uint64_t transport_context_lifecycle_id;
  uint64_t transport_before_signature;
  uint64_t transport_after_signature;
  int target_tech;
  uint64_t target_research_digest;
  uint64_t target_stack_signature;
  int vote_no;
  uint64_t vote_signature;
  int target_government;
  int target_build_kind;
  int target_build_id;
  int target_building_catalog_request_id;
  uint64_t target_building_catalog_revision;
  uint64_t target_building_catalog_digest;
  int spaceship_part;
  int spaceship_value;
  int target_multiplier;
  int multiplier_value;
  int source_specialist;
  int target_specialist;
  bool worker_task_baseline_present;
  enum unit_activity worker_task_baseline_activity;
  int worker_task_baseline_extra;
  int worker_task_baseline_want;
  int target_extra;
  int infrastructure_cost;
  int infrastructure_turns;
  int infrastructure_choice_count;
  char infrastructure_choices[FC_AGENT_V2_INFRA_CHOICES_TEXT];
  enum unit_activity target_activity;
  int max_rate;
  int counterpart_id;
  uint64_t counterpart_incarnation;
  uint64_t meeting_generation;
  uint64_t clauses_digest;
  bool self_accepted;
  bool other_accepted;
  enum diplstate_type relation_state;
  bool outgoing_vision;
  bool outgoing_shared_tiles;
  int clause_giver_id;
  enum clause_type clause_type;
  int clause_value;
  int desired_acceptance;
  int gold_cost;
  action_id action;
  enum agent_v2_probability_kind probability_kind;
  int probability_min;
  int probability_max;
  char slot[32];
};

struct agent_v2_snapshot {
  bool valid;
  char id[48];
  uint64_t revision;
  size_t row_count;
  struct agent_v2_row rows[AGENT_V2_MAX_ROWS];
};

struct agent_v2_action_buffer {
  struct agent_v2_action *actions;
  size_t count;
  size_t capacity;
  bool overflow;
  bool export_unknown_rows;
  bool *unknown_exported;
};

struct agent_v2_scope {
  bool valid;
  char id[48];
  char actor_ref[48];
  uint64_t revision;
  size_t action_count;
  struct agent_v2_action actions[AGENT_V2_MAX_ACTIONS];
};

struct agent_v2_target_scope {
  bool valid;
  char actor_ref[48];
  uint64_t revision;
  int target_tile;
  size_t action_count;
  struct agent_v2_action actions[FC_AGENT_V2_MAX_TARGET_ACTIONS];
};

struct agent_v2_target_query {
  bool active;
  bool emitting;
  bool stream_started;
  char request[AGENT_V2_TOKEN_MAX + 1];
  char actor_ref[48];
  char encoded_actor[160];
  uint64_t revision;
  int actor_id;
  int target_tile;
  size_t action_count;
  size_t emit_index;
  size_t cost_action_index;
  int cost_request_id;
  bool cost_query_pending;
  size_t detail_action_index;
  int detail_request_id;
  bool detail_query_pending;
  int scope_index;
  struct timer *timer;
  struct agent_v2_action actions[FC_AGENT_V2_MAX_TARGET_ACTIONS];
};

struct agent_v2_special_revalidation {
  bool active;
  bool ready;
  bool cost_query_pending;
  int cost_request_id;
  bool detail_query_pending;
  int detail_request_id;
  char request[AGENT_V2_TOKEN_MAX + 1];
  char slot[32];
  uint64_t revision;
  struct timer *timer;
  struct agent_v2_action action;
};

struct agent_v2_relation_scope {
  bool valid;
  char id[48];
  char actor_ref[48];
  char counterpart_ref[48];
  uint64_t revision;
  size_t action_count;
  struct agent_v2_action actions[FC_AGENT_V2_MAX_RELATION_ACTIONS];
};

struct agent_v2_state_scope {
  bool valid;
  char id[48];
  char section[32];
  char selector[64];
  uint64_t revision;
  size_t total;
  size_t bytes;
  uint64_t digest;
  struct agent_v2_row *rows;
};

/* Exact subset of PACKET_CITY_INFO deliberately admitted across the v2 trust
 * boundary after an investigation.  No cache-only, unit, nationality, route,
 * worklist, rally, or web-client fields are retained here. */
struct agent_v2_investigation_payload {
  int city_id;
  uint64_t city_incarnation;
  uint64_t city_lifecycle_id;
  int tile;
  int size;
  int production_kind;
  int production_value;
  int shield_stock;
  int shield_surplus;
  int feelings[CITIZEN_LAST][FEELING_LAST];
  int specialists_size;
  int specialists[SP_MAX];
  bv_imprs improvements;
  char name[MAX_LEN_CITYNAME];
  uint64_t digest;
};

struct agent_v2_investigation_observation {
  bool valid;
  bool consumed;
  uint64_t seat_epoch;
  uint64_t revision;
  uint64_t serial;
  char token[AGENT_V2_INVESTIGATION_TOKEN_MAX];
  struct agent_v2_investigation_payload payload;
};

struct agent_v2_relation_state {
  int counterpart_id;
  uint64_t counterpart_incarnation;
  uint64_t meeting_generation;
  bool meeting_open;
};

struct agent_v2_clause_key {
  const struct Clause *clause;
  int giver;
  int type;
  int value;
};

struct agent_v2_visible_bribe_member {
  int old_id;
  uint64_t old_incarnation;
  uint64_t old_lifecycle_id;
  int unit_type;
  int nationality;
  int veteran;
  int hp;
  int moves_left;
  int fuel;
  int birth_turn;
  int current_form_turn;
  bool paradropped;
  bool replacement_latched;
  int replacement_id;
  uint64_t replacement_incarnation;
  uint64_t replacement_lifecycle_id;
};

struct agent_v2_pending {
  bool active;
  bool processing_started;
  bool baseline_captured;
  enum fc_agent_v2_terminal_result terminal;
  uint64_t nonce;
  uint64_t seat_epoch;
  uint64_t revision;
  char request[AGENT_V2_TOKEN_MAX + 1];
  char slot[32];
  struct agent_v2_action action;
  int request_id;
  int first_request_id;
  int request_count;
  bool first_processing_finished;
  bool last_processing_started;
  bool exact_unit_state_latched;
  bool exact_route_state_latched;
  bool paid_success_event_latched;
  bool paid_failure_event_latched;
  bool action_success_receipt_latched;
  bool combat_info_latched;
  bool spy_attack_actor_loss_event_latched;
  bool spy_attack_target_loss_event_latched;
  bool sabotage_unit_success_event_latched;
  bool poison_city_success_event_latched;
  bool sabotage_city_success_event_latched;
  bool paid_replacement_latched;
  bool paid_replacement_conflict;
  bool nuke_tile_info_latched;
  bool investigation_started_latched;
  bool investigation_city_info_latched;
  bool investigation_finished_latched;
  struct agent_v2_investigation_payload investigation;
  bool caravan_action_event_latched;
  bool chat_echo_latched;
  bool chat_error_latched;
  bool desired_chat_allied;
  char desired_chat_message[FC_AGENT_V2_MAX_CHAT_MESSAGE_BYTES + 1];
  bool bribe_visible_baseline_exact;
  bool bribe_visible_mapping_conflict;
  bool bribe_visible_mapping_corroborated;
  size_t bribe_visible_count;
  struct agent_v2_visible_bribe_member
    bribe_visible[AGENT_V2_MAX_VISIBLE_BRIBE_STACK];
  int paid_replacement_unit_id;
  uint64_t paid_replacement_unit_incarnation;
  uint64_t paid_replacement_unit_lifecycle_id;
  int requested_unit_source_tile;
  int requested_unit_destination_tile;
  int before_turn;
  bool before_phase_done;
  int before_unit_tile;
  int before_unit_hp;
  int before_unit_type;
  int before_unit_homecity;
  bool before_unit_present;
  uint64_t before_unit_lifecycle_id;
  int before_target_unit_tile;
  int before_target_unit_type;
  int before_target_unit_hp;
  bool before_target_unit_present;
  uint64_t before_target_unit_lifecycle_id;
  int before_transport_context_tile;
  bool before_transport_context_present;
  uint64_t before_transport_context_lifecycle_id;
  bool before_transport_baseline_exact;
  bool before_source_city_present;
  uint64_t before_source_city_lifecycle_id;
  bool before_destination_city_present;
  uint64_t before_destination_city_lifecycle_id;
  int before_destination_city_size;
  int before_destination_city_shield_stock;
  bool before_destination_city_owned;
  bool before_destination_city_internals_exact;
  bool before_trade_route_exists;
  int expected_unit_population;
  int expected_help_shields;
  bool before_unit_paradropped;
  enum server_side_agent before_unit_ssa;
  enum unit_activity before_unit_activity;
  bool before_unit_activity_target_none;
  bool before_unit_has_orders;
  bool before_unit_goto_none;
  bool before_unit_untransported;
  bool before_unit_cargo_empty;
  int before_infrastructure_points;
  bool before_infrastructure_unplaced;
  bool before_special_target_exact;
  bool before_target_building_present;
  int before_extra_owner;
  bv_extras before_hut_extras;
  bv_imprs before_destination_city_improvements;
  bool before_research_exact;
  bv_techs before_known_techs;
  int before_future_tech;
  uint64_t before_target_signature;
  int before_research_target;
  bool before_city_did_buy;
  bool before_city_present;
  uint64_t before_city_lifecycle_id;
  int before_city_shield_stock;
  int before_city_specialists;
  int before_source_specialists;
  int before_target_specialists;
  bool before_city_tile_worked;
  bool before_worker_task_present;
  enum unit_activity before_worker_task_activity;
  int before_worker_task_extra;
  int before_worker_task_want;
  bool worker_task_echo_latched;
  int before_player_gold;
  int before_buy_cost;
  bool before_city_did_sell;
  bool before_city_had_improvement;
  int before_city_source_tile;
  bool before_city_rally_active;
  bool before_city_rally_persistent;
  bool before_city_rally_vigilant;
  int before_city_rally_order_count;
  uint64_t before_city_rally_orders_digest;
  struct worklist before_city_worklist;
  bv_city_options before_city_options;
  enum city_wl_cancel_behavior before_city_wlcb;
  char before_city_name[MAX_LEN_CITYNAME];
  struct worklist desired_worklist;
  bv_city_options desired_city_options;
  enum city_wl_cancel_behavior desired_wlcb;
  const struct impr_type *desired_improvement;
  int desired_tax;
  int desired_luxury;
  int desired_science;
  enum client_vote_type desired_client_vote;
  struct universal desired_production;
  enum unit_activity desired_activity;
  enum server_side_agent desired_ssa;
  int desired_extra;
  int desired_route_destination_tile;
  int desired_route_order_count;
  uint64_t desired_route_orders_digest;
  bool desired_route_repeat;
  bool desired_route_vigilant;
  int desired_government;
  int desired_pregame_nation;
  int desired_pregame_style;
  int desired_pregame_team;
  bool desired_pregame_male;
  bool desired_pregame_ready;
  char desired_pregame_leader[MAX_LEN_NAME];
  int desired_unit_type;
  struct player_spaceship before_spaceship;
  int desired_spaceship_part;
  int desired_spaceship_value;
  int desired_multiplier;
  int desired_multiplier_value;
  int before_multiplier_count;
  int before_multiplier_values[MAX_NUM_MULTIPLIERS];
  int before_multiplier_targets[MAX_NUM_MULTIPLIERS];
  int before_spaceship_year;
  bool desired_rally_active;
  bool desired_rally_persistent;
  int desired_rally_order_count;
  uint64_t desired_rally_orders_digest;
  bool desired_worker_task_present;
  int desired_worker_task_want;
  int before_government;
  int before_target_government;
  int before_revolution_finishes;
  enum diplstate_type before_diplstate;
  bool before_gives_vision;
  bool before_gives_shared_tiles;
  bool diplomacy_echo_latched;
  char city_name[MAX_LEN_CITYNAME];
  struct client_rally_plan *rally_plan;
  struct client_unit_route_plan *unit_route_plan;
  struct timer *timer;
  struct agent_v2_callback_token *started_token;
  struct agent_v2_callback_token *finished_token;
  struct agent_v2_callback_token *first_finished_token;
  struct agent_v2_callback_token *last_started_token;
};

struct agent_v2_chat_entry {
  uint64_t sequence;
  int turn;
  int phase;
  bool self;
  bool truncated;
  char sender_kind[16];
  char sender_name[MAX_LEN_NAME];
  char channel[16];
  char event[64];
  char message[FC_AGENT_V2_MAX_CHAT_MESSAGE_BYTES + 1];
};

struct agent_v2_callback_token {
  uint64_t nonce;
  int request_id;
};

static fc_agent_v2_emit_fn v2_emit;
static fc_agent_v2_authorized_fn v2_authorized;
static uint64_t v2_secret;
static uint64_t v2_revision;
static uint64_t v2_hash;
static uint64_t v2_notified_revision;
static struct fc_agent_v2_phase_notice v2_phase_notice;
static struct fc_agent_v2_phase_evidence v2_current_phase;
static bool v2_have_current_phase;
static bool v2_have_current;
static bool v2_overflow;
static bool v2_seat_known;
static bool v2_seat_authorized;
static const struct player *v2_seat_player;
static int v2_seat_player_number;
static const struct tile *v2_seat_map_tiles;
static int v2_seat_map_xsize;
static int v2_seat_map_ysize;
static int v2_seat_map_topology;
static int v2_seat_map_wrap;
static uint64_t v2_seat_game_epoch;
static enum client_states v2_seat_client_state = C_S_INITIAL;
static uint64_t v2_seat_epoch;
static uint64_t v2_next_action_nonce;
static int v2_next_local_operation_id;
static unsigned int v2_snapshot_serial;
static struct agent_v2_row v2_work_rows[AGENT_V2_MAX_ROWS];
static size_t v2_work_row_count;
static struct agent_v2_row v2_current_rows[AGENT_V2_MAX_ROWS];
static size_t v2_current_row_count;
static struct agent_v2_action v2_work_actions[AGENT_V2_MAX_ACTIONS];
static size_t v2_work_action_count;
static struct agent_v2_action v2_current_actions[AGENT_V2_MAX_ACTIONS];
static size_t v2_current_action_count;
static struct agent_v2_snapshot v2_snapshots[AGENT_V2_PINNED];
static struct agent_v2_scope v2_scopes[AGENT_V2_SCOPE_PINNED];
static struct agent_v2_target_scope
  v2_target_scopes[AGENT_V2_SCOPE_PINNED];
static struct agent_v2_state_scope
  v2_state_scopes[AGENT_V2_STATE_SCOPE_PINNED];
static struct agent_v2_relation_scope
  v2_relation_scopes[AGENT_V2_RELATION_SCOPE_PINNED];
static struct agent_v2_relation_state v2_relations[MAX_NUM_PLAYER_SLOTS];
static size_t v2_relation_count;
static unsigned int v2_scope_serial;
static unsigned int v2_state_scope_serial;
static struct agent_v2_action v2_scope_actions[AGENT_V2_MAX_ACTIONS];
static struct agent_v2_row *v2_state_scope_rows;
static size_t v2_state_scope_row_capacity;
static size_t v2_state_scope_total;
static size_t v2_state_scope_bytes;
static bool v2_state_scope_capture;
static uint64_t v2_state_scope_digest;
static struct agent_v2_action
  v2_relation_scope_actions[FC_AGENT_V2_MAX_RELATION_ACTIONS];
static struct agent_v2_pending v2_pending;
static struct agent_v2_chat_entry
  v2_chat_history[FC_AGENT_V2_MAX_CHAT_HISTORY];
static size_t v2_chat_history_start;
static size_t v2_chat_history_count;
static uint64_t v2_chat_sequence;
static struct agent_v2_target_query v2_target_query;
static struct agent_v2_special_revalidation v2_special_revalidation;
static struct agent_v2_investigation_observation v2_investigation;
static uint64_t v2_investigation_serial;
/* A timed-out native query may still have an uncorrelated reply in flight.
 * Refuse all later target queries in this process rather than letting that
 * reply populate a different opaque scope. */
static bool v2_target_query_desynchronized;
/* A timed-out server-authoritative preflight may also reply late. Never bind
 * such a reply to a later action in the same process. */
static bool v2_special_revalidation_desynchronized;

static bool v2_cache_coherent(void);
static bool v2_action_postcondition(void);
static bool v2_action_probability_matches(
  struct act_prob probability, const struct agent_v2_action *action);
static void v2_action_processing_started(void *data);
static void v2_action_processing_finished(void *data);
static void v2_action_first_processing_finished(void *data);
static void v2_action_last_processing_started(void *data);
static void v2_worker_task_observer(
  const struct packet_worker_task *packet, const struct city *pcity,
  int request_id, void *data);
static void v2_full_unit_info_observer(const struct unit *punit,
                                       int request_id, void *data);
static void v2_unit_action_answer_observer(
  const struct packet_unit_action_answer *packet, int request_id, void *data);
static void v2_chat_msg_observer(
  const struct packet_chat_msg *packet, int request_id, void *data);
static bool v2_city_sabotage_list_observer(
  const struct packet_city_sabotage_list *packet, int request_id, void *data);
static void v2_unit_combat_info_observer(
  const struct packet_unit_combat_info *packet, int request_id, void *data);
static uint64_t v2_hash_bytes(uint64_t hash, const void *data, size_t length);
static bool v2_encode_row_value(const char *raw, char *encoded,
                                size_t encoded_size);
static uint64_t v2_existing_incarnation(enum agent_v2_entity_kind kind,
                                        int id);
static bool v2_production_supported(const struct universal *target);
static const char *v2_build_kind_name(int kind);
static bool v2_exact_seat_epoch_current(void);
static void v2_nuke_tile_info_observer(
  const struct packet_nuke_tile_info *packet, int request_id, void *data);
static void v2_investigation_observer(
  enum packhand_investigation_stage stage, int city_id,
  const struct packet_city_info *city_info, const struct city *city,
  int request_id, void *data);
static bool v2_investigation_payload_exportable(
  const struct agent_v2_investigation_payload *payload);
static bool v2_unit_actions_observer(
  const struct packet_unit_actions *packet, void *data);
static bool v2_special_action_still_bound(
  const struct player *self, const struct unit *actor,
  const struct agent_v2_action *action, int *target_id, int *subtarget_id);
static bv_extras v2_hut_extras_on_tile(const struct tile *ptile);
static void v2_target_query_request_next_cost(void);
static void v2_target_query_request_next_detail(void);
static bool v2_treaty_has_clause(
  const struct treaty *treaty, int giver_id,
  enum clause_type type, int value);
static bool v2_relation_action_still_legal(
  const struct agent_v2_action *action, int proposed_gold);
static bool v2_resolve_owned_actor(
  const char *actor_ref, enum agent_v2_entity_kind *kind,
  int *id, uint64_t *incarnation);
static bool v2_build_actor_scope(
  const char *actor_ref, struct agent_v2_action *actions,
  size_t *action_count, bool *overflow);
static uint64_t v2_visible_stack_signature(const struct player *self,
                                           int tile_id);
static uint64_t v2_visible_bribe_stack_signature(
  const struct player *self, int tile_id);
static bool v2_visible_bribe_stack_bounded(const struct player *self,
                                           const struct tile *tile);

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

static bool v2_investigation_payload_exportable(
  const struct agent_v2_investigation_payload *payload)
{
  struct universal production;
  char encoded[AGENT_V2_ROW_MAX];

  if (payload == NULL || payload->city_id < 0
      || payload->city_incarnation == 0 || payload->city_lifecycle_id == 0
      || payload->tile < 0 || payload->size < 0
      || payload->specialists_size < 0
      || payload->specialists_size > SP_MAX
      || !universals_n_is_valid(payload->production_kind)) {
    return FALSE;
  }
  production = universal_by_number(
    payload->production_kind, payload->production_value);
  if (!universals_n_is_valid(production.kind)
      || !v2_production_supported(&production)
      || v2_build_kind_name(production.kind) == NULL
      || !v2_encode_row_value(
           payload->name, encoded, sizeof(encoded))
      || !v2_encode_row_value(
           universal_rule_name(&production), encoded, sizeof(encoded))) {
    return FALSE;
  }
  improvement_iterate(pimprove) {
    if (BV_ISSET(payload->improvements, improvement_index(pimprove))
        && !v2_encode_row_value(
             improvement_rule_name(pimprove), encoded, sizeof(encoded))) {
      return FALSE;
    }
  } improvement_iterate_end;
  for (int specialist = 0;
       specialist < payload->specialists_size; specialist++) {
    const struct specialist *pspecialist =
      specialist_by_number(specialist);

    if (pspecialist == NULL
        || !v2_encode_row_value(
             specialist_rule_name(pspecialist), encoded, sizeof(encoded))) {
      return FALSE;
    }
  }
  return TRUE;
}

static uint64_t v2_investigation_payload_digest(
  const struct agent_v2_investigation_payload *payload)
{
  uint64_t digest = UINT64_C(1469598103934665603);

#define V2_INVESTIGATION_HASH(_field)                                      \
  digest = v2_hash_bytes(digest, &payload->_field, sizeof(payload->_field))
  V2_INVESTIGATION_HASH(city_id);
  V2_INVESTIGATION_HASH(city_incarnation);
  V2_INVESTIGATION_HASH(city_lifecycle_id);
  V2_INVESTIGATION_HASH(tile);
  V2_INVESTIGATION_HASH(size);
  V2_INVESTIGATION_HASH(production_kind);
  V2_INVESTIGATION_HASH(production_value);
  V2_INVESTIGATION_HASH(shield_stock);
  V2_INVESTIGATION_HASH(shield_surplus);
  V2_INVESTIGATION_HASH(feelings);
  V2_INVESTIGATION_HASH(specialists_size);
  digest = v2_hash_bytes(
    digest, payload->specialists,
    payload->specialists_size * sizeof(payload->specialists[0]));
  V2_INVESTIGATION_HASH(improvements);
  digest = v2_hash_bytes(digest, payload->name, strlen(payload->name) + 1);
#undef V2_INVESTIGATION_HASH
  return digest;
}

static void v2_investigation_observer(
  enum packhand_investigation_stage stage, int city_id,
  const struct packet_city_info *city_info, const struct city *city,
  int request_id, void *data)
{
  const struct action *native;
  struct agent_v2_investigation_payload payload;

  (void) data;
  native = action_by_number(v2_pending.action.action);
  if (!v2_pending.active || !v2_pending.baseline_captured
      || v2_pending.seat_epoch != v2_seat_epoch
      || !v2_exact_seat_epoch_current()
      || request_id != v2_pending.request_id
      || native == NULL
      || native->result != ACTRES_SPY_INVESTIGATE_CITY
      || city_id != v2_pending.action.destination_city_id
      || city == NULL || city->id != city_id
      || city->client.lifecycle_id == 0
      || city->client.lifecycle_id
         != v2_pending.action.destination_city_lifecycle_id
      || v2_existing_incarnation(AGENT_V2_ENTITY_CITY, city_id)
         != v2_pending.action.destination_city_incarnation) {
    return;
  }
  if (stage == PACKHAND_INVESTIGATION_STARTED) {
    v2_pending.investigation_started_latched = TRUE;
    return;
  }
  if (stage == PACKHAND_INVESTIGATION_CITY_INFO) {
    if (!v2_pending.investigation_started_latched || city_info == NULL
        || !city_info->diplomat_investigate
        || city_info->id != city_id
        || city_info->tile != tile_index(city_tile(city))
        || city_info->specialists_size > SP_MAX) {
      return;
    }
    memset(&payload, 0, sizeof(payload));
    payload.city_id = city_id;
    payload.city_incarnation = v2_pending.action.destination_city_incarnation;
    payload.city_lifecycle_id = city->client.lifecycle_id;
    payload.tile = city_info->tile;
    payload.size = city_info->size;
    payload.production_kind = city_info->production_kind;
    payload.production_value = city_info->production_value;
    payload.shield_stock = city_info->shield_stock;
    payload.shield_surplus = city_info->surplus[O_SHIELD];
    for (int feeling = 0; feeling < FEELING_LAST; feeling++) {
      payload.feelings[CITIZEN_HAPPY][feeling] =
        city_info->ppl_happy[feeling];
      payload.feelings[CITIZEN_CONTENT][feeling] =
        city_info->ppl_content[feeling];
      payload.feelings[CITIZEN_UNHAPPY][feeling] =
        city_info->ppl_unhappy[feeling];
      payload.feelings[CITIZEN_ANGRY][feeling] =
        city_info->ppl_angry[feeling];
    }
    payload.specialists_size = city_info->specialists_size;
    for (int specialist = 0;
         specialist < payload.specialists_size; specialist++) {
      payload.specialists[specialist] = city_info->specialists[specialist];
    }
    payload.improvements = city_info->improvements;
    fc_strlcpy(payload.name, city_info->name, sizeof(payload.name));
    payload.digest = v2_investigation_payload_digest(&payload);
    if (!v2_investigation_payload_exportable(&payload)) {
      return;
    }
    v2_pending.investigation = payload;
    v2_pending.investigation_city_info_latched = TRUE;
    return;
  }
  if (stage == PACKHAND_INVESTIGATION_FINISHED
      && v2_pending.investigation_started_latched
      && v2_pending.investigation_city_info_latched) {
    v2_pending.investigation_finished_latched = TRUE;
  }
}

static bool v2_unreserved(unsigned char value)
{
  return (value >= 'a' && value <= 'z')
         || (value >= 'A' && value <= 'Z')
         || (value >= '0' && value <= '9')
         || value == '.' || value == '_' || value == '~' || value == '-';
}

static bool v2_token_valid(const char *token)
{
  size_t length = strlen(token);
  size_t i;

  if (length == 0 || length > AGENT_V2_TOKEN_MAX) {
    return FALSE;
  }
  for (i = 0; i < length; i++) {
    if (!v2_unreserved((unsigned char) token[i])) {
      return FALSE;
    }
  }
  return TRUE;
}

static bool v2_sendf(const char *format, ...)
{
  char message[FC_AGENT_IPC_MAX_PAYLOAD + 1];
  va_list args;
  int length;

  if (v2_emit == NULL) {
    return FALSE;
  }
  va_start(args, format);
  length = fc_vsnprintf(message, sizeof(message), format, args);
  va_end(args);
  if (length < 0 || (size_t) length >= sizeof(message)) {
    return FALSE;
  }
  return v2_emit(message);
}

static void v2_error(const char *request, const char *code,
                     const char *detail)
{
  char encoded[384];

  if (!fc_agent_v2_percent_encode(detail, encoded, sizeof(encoded))) {
    fc_strlcpy(encoded, "error-detail-too-long", sizeof(encoded));
  }
  v2_sendf("ERR\t%s\t%s\t%s",
           request != NULL && v2_token_valid(request) ? request : "-",
           code, encoded);
}

static int v2_row_compare(const void *left, const void *right)
{
  const struct agent_v2_row *a = left;
  const struct agent_v2_row *b = right;

  return strcmp(a->text, b->text);
}

static int v2_action_compare(const void *left, const void *right)
{
  const struct agent_v2_action *a = left;
  const struct agent_v2_action *b = right;

  if (a->kind != b->kind) {
    return (int) a->kind - (int) b->kind;
  }
  if (a->player_id != b->player_id) {
    return a->player_id < b->player_id ? -1 : 1;
  }
  if (a->counterpart_id != b->counterpart_id) {
    return a->counterpart_id < b->counterpart_id ? -1 : 1;
  }
  if (a->relation_state != b->relation_state) {
    return (int) a->relation_state - (int) b->relation_state;
  }
  if (a->clause_giver_id != b->clause_giver_id) {
    return a->clause_giver_id < b->clause_giver_id ? -1 : 1;
  }
  if (a->clause_type != b->clause_type) {
    return (int) a->clause_type - (int) b->clause_type;
  }
  if (a->clause_value != b->clause_value) {
    return a->clause_value < b->clause_value ? -1 : 1;
  }
  if (a->unit_id != b->unit_id) {
    return a->unit_id < b->unit_id ? -1 : 1;
  }
  if (a->city_id != b->city_id) {
    return a->city_id < b->city_id ? -1 : 1;
  }
  if (a->source_unit_tile != b->source_unit_tile) {
    return a->source_unit_tile < b->source_unit_tile ? -1 : 1;
  }
  if (a->source_unit_moves != b->source_unit_moves) {
    return a->source_unit_moves < b->source_unit_moves ? -1 : 1;
  }
  if (a->source_unit_paradropped != b->source_unit_paradropped) {
    return a->source_unit_paradropped ? 1 : -1;
  }
  if (a->target_tile != b->target_tile) {
    return a->target_tile < b->target_tile ? -1 : 1;
  }
  if (a->special_target_city_id != b->special_target_city_id) {
    return a->special_target_city_id < b->special_target_city_id ? -1 : 1;
  }
  if (a->special_target_city_owner != b->special_target_city_owner) {
    return a->special_target_city_owner < b->special_target_city_owner ? -1 : 1;
  }
  if (a->special_target_extra_owner != b->special_target_extra_owner) {
    return a->special_target_extra_owner < b->special_target_extra_owner
           ? -1 : 1;
  }
  if (a->source_city_id != b->source_city_id) {
    return a->source_city_id < b->source_city_id ? -1 : 1;
  }
  if (a->destination_city_id != b->destination_city_id) {
    return a->destination_city_id < b->destination_city_id ? -1 : 1;
  }
  if (a->target_unit_id != b->target_unit_id) {
    return a->target_unit_id < b->target_unit_id ? -1 : 1;
  }
  if (a->transport_context_id != b->transport_context_id) {
    return a->transport_context_id < b->transport_context_id ? -1 : 1;
  }
  if (a->target_tech != b->target_tech) {
    return a->target_tech < b->target_tech ? -1 : 1;
  }
  if (a->target_research_digest != b->target_research_digest) {
    return a->target_research_digest < b->target_research_digest ? -1 : 1;
  }
  if (a->target_stack_signature != b->target_stack_signature) {
    return a->target_stack_signature < b->target_stack_signature ? -1 : 1;
  }
  if (a->vote_no != b->vote_no) {
    return a->vote_no < b->vote_no ? -1 : 1;
  }
  if (a->vote_signature != b->vote_signature) {
    return a->vote_signature < b->vote_signature ? -1 : 1;
  }
  if (a->target_government != b->target_government) {
    return a->target_government < b->target_government ? -1 : 1;
  }
  if (a->target_build_kind != b->target_build_kind) {
    return a->target_build_kind < b->target_build_kind ? -1 : 1;
  }
  if (a->target_build_id != b->target_build_id) {
    return a->target_build_id < b->target_build_id ? -1 : 1;
  }
  if (a->target_building_catalog_request_id
      != b->target_building_catalog_request_id) {
    return a->target_building_catalog_request_id
             < b->target_building_catalog_request_id ? -1 : 1;
  }
  if (a->target_building_catalog_revision
      != b->target_building_catalog_revision) {
    return a->target_building_catalog_revision
             < b->target_building_catalog_revision ? -1 : 1;
  }
  if (a->target_building_catalog_digest
      != b->target_building_catalog_digest) {
    return a->target_building_catalog_digest
             < b->target_building_catalog_digest ? -1 : 1;
  }
  if (a->spaceship_part != b->spaceship_part) {
    return a->spaceship_part < b->spaceship_part ? -1 : 1;
  }
  if (a->spaceship_value != b->spaceship_value) {
    return a->spaceship_value < b->spaceship_value ? -1 : 1;
  }
  if (a->target_multiplier != b->target_multiplier) {
    return a->target_multiplier < b->target_multiplier ? -1 : 1;
  }
  if (a->multiplier_value != b->multiplier_value) {
    return a->multiplier_value < b->multiplier_value ? -1 : 1;
  }
  if (a->source_specialist != b->source_specialist) {
    return a->source_specialist < b->source_specialist ? -1 : 1;
  }
  if (a->target_specialist != b->target_specialist) {
    return a->target_specialist < b->target_specialist ? -1 : 1;
  }
  if (a->worker_task_baseline_present != b->worker_task_baseline_present) {
    return a->worker_task_baseline_present ? 1 : -1;
  }
  if (a->worker_task_baseline_activity
      != b->worker_task_baseline_activity) {
    return (int) a->worker_task_baseline_activity
           - (int) b->worker_task_baseline_activity;
  }
  if (a->worker_task_baseline_extra != b->worker_task_baseline_extra) {
    return a->worker_task_baseline_extra
           < b->worker_task_baseline_extra ? -1 : 1;
  }
  if (a->worker_task_baseline_want != b->worker_task_baseline_want) {
    return a->worker_task_baseline_want
           < b->worker_task_baseline_want ? -1 : 1;
  }
  if (a->target_activity != b->target_activity) {
    return (int) a->target_activity - (int) b->target_activity;
  }
  if (a->target_extra != b->target_extra) {
    return a->target_extra < b->target_extra ? -1 : 1;
  }
  if (a->max_rate != b->max_rate) {
    return a->max_rate < b->max_rate ? -1 : 1;
  }
  return (int) a->action - (int) b->action;
}

static void v2_add_row(const char *format, ...)
{
  va_list args;
  int length;

  if (v2_overflow) {
    return;
  }
  if (v2_work_row_count >= AGENT_V2_MAX_ROWS) {
    v2_overflow = TRUE;
    return;
  }
  va_start(args, format);
  length = fc_vsnprintf(v2_work_rows[v2_work_row_count].text,
                        sizeof(v2_work_rows[v2_work_row_count].text),
                        format, args);
  va_end(args);
  if (length < 0
      || (size_t) length >= sizeof(v2_work_rows[v2_work_row_count].text)) {
    v2_overflow = TRUE;
    return;
  }
  v2_work_row_count++;
}

static void v2_state_add_row(const char *format, ...)
{
  struct agent_v2_row row;
  va_list args;
  int length;
  size_t row_bytes;

  if (v2_overflow) {
    return;
  }
  if (v2_state_scope_total >= FC_AGENT_V2_MAX_STATE_SCOPE_ROWS) {
    v2_overflow = TRUE;
    return;
  }
  va_start(args, format);
  length = fc_vsnprintf(row.text, sizeof(row.text), format, args);
  va_end(args);
  if (length < 0 || (size_t) length >= sizeof(row.text)) {
    v2_overflow = TRUE;
    return;
  }
  row_bytes = (size_t) length + 1;
  if (row_bytes > FC_AGENT_V2_MAX_STATE_SCOPE_BYTES
      || v2_state_scope_bytes
         > FC_AGENT_V2_MAX_STATE_SCOPE_BYTES - row_bytes) {
    v2_overflow = TRUE;
    return;
  }
  if (v2_state_scope_capture) {
    if (v2_state_scope_total == v2_state_scope_row_capacity) {
      size_t capacity = v2_state_scope_row_capacity == 0
                        ? 64 : v2_state_scope_row_capacity * 2;

      if (capacity > FC_AGENT_V2_MAX_STATE_SCOPE_ROWS) {
        capacity = FC_AGENT_V2_MAX_STATE_SCOPE_ROWS;
      }
      v2_state_scope_rows = fc_realloc(
        v2_state_scope_rows, capacity * sizeof(*v2_state_scope_rows));
      v2_state_scope_row_capacity = capacity;
    }
    v2_state_scope_rows[v2_state_scope_total] = row;
  }
  v2_state_scope_digest = v2_hash_bytes(
    v2_state_scope_digest, row.text, strlen(row.text) + 1);
  v2_state_scope_total++;
  v2_state_scope_bytes += row_bytes;
}

/* Row bodies are space-delimited key=value records. Encode every textual
 * value again inside that grammar, independently of the outer frame field,
 * so spaces, percent signs, equals signs, and UTF-8 remain unambiguous. */
static bool v2_encode_row_value(const char *raw, char *encoded,
                                size_t encoded_size)
{
  if (raw == NULL
      || !fc_agent_v2_percent_encode(raw, encoded, encoded_size)) {
    v2_overflow = TRUE;
    return FALSE;
  }
  return TRUE;
}

static char v2_entity_prefix(enum agent_v2_entity_kind kind)
{
  switch (kind) {
  case AGENT_V2_ENTITY_PLAYER:
    return 'p';
  case AGENT_V2_ENTITY_CITY:
    return 'c';
  case AGENT_V2_ENTITY_UNIT:
    return 'u';
  }
  return '?';
}

static uint64_t v2_existing_incarnation(enum agent_v2_entity_kind kind,
                                        int id);

static void v2_entity_ref(enum agent_v2_entity_kind kind, int id,
                          char *buffer, size_t buffer_size)
{
  uint64_t incarnation = v2_existing_incarnation(kind, id);

  if (incarnation == 0) {
    v2_overflow = TRUE;
    fc_strlcpy(buffer, "overflow", buffer_size);
    return;
  }
  fc_snprintf(buffer, buffer_size, "%c:%d:%llu",
              v2_entity_prefix(kind),
              id, (unsigned long long) incarnation);
}

static uint64_t v2_existing_incarnation(enum agent_v2_entity_kind kind,
                                        int id)
{
  if (kind == AGENT_V2_ENTITY_PLAYER) {
    const struct player *pplayer = player_by_number(id);

    return pplayer != NULL ? pplayer->client.lifecycle_id : 0;
  } else if (kind == AGENT_V2_ENTITY_CITY) {
    const struct city *pcity = game_city_by_number(id);

    return pcity != NULL ? pcity->client.lifecycle_id : 0;
  } else {
    const struct unit *punit = game_unit_by_number(id);

    return punit != NULL ? punit->client.lifecycle_id : 0;
  }
}

static bool v2_normalize_probability(
  struct act_prob probability,
  enum agent_v2_probability_kind *kind,
  int *minimum, int *maximum)
{
  struct act_prob not_implemented = action_prob_new_not_impl();
  struct act_prob unknown = action_prob_new_unknown();
  struct act_prob certain = action_prob_new_certain();

  if (are_action_probabilitys_equal(&probability, &not_implemented)) {
    *kind = AGENT_V2_PROBABILITY_NOT_IMPLEMENTED;
    *minimum = -1;
    *maximum = -1;
    return TRUE;
  }
  if (are_action_probabilitys_equal(&probability, &unknown)) {
    *kind = AGENT_V2_PROBABILITY_UNKNOWN;
    *minimum = probability.min;
    *maximum = probability.max;
    return TRUE;
  }
  if (probability.min < 0 || probability.max > certain.max
      || probability.min > probability.max) {
    return FALSE;
  }
  *kind = probability.min == probability.max
          ? AGENT_V2_PROBABILITY_EXACT : AGENT_V2_PROBABILITY_RANGE;
  *minimum = probability.min;
  *maximum = probability.max;
  return TRUE;
}

static int v2_probability_rank(enum agent_v2_probability_kind kind)
{
  switch (kind) {
  case AGENT_V2_PROBABILITY_EXACT:
    return 0;
  case AGENT_V2_PROBABILITY_RANGE:
    return 1;
  case AGENT_V2_PROBABILITY_UNKNOWN:
    return 2;
  case AGENT_V2_PROBABILITY_NOT_IMPLEMENTED:
    return 3;
  }
  return 4;
}

static bool v2_probability_preferred(
  enum agent_v2_probability_kind candidate_kind,
  int candidate_min, int candidate_max, action_id candidate_action,
  const struct agent_v2_action *existing)
{
  int candidate_rank = v2_probability_rank(candidate_kind);
  int existing_rank = v2_probability_rank(existing->probability_kind);

  return fc_agent_v2_probability_candidate_preferred(
    candidate_rank, candidate_min, candidate_max, candidate_action,
    existing_rank, existing->probability_min, existing->probability_max,
    existing->action);
}

static void v2_action_init(struct agent_v2_action *entry)
{
  memset(entry, 0, sizeof(*entry));
  entry->player_id = -1;
  entry->unit_id = -1;
  entry->city_id = -1;
  entry->target_tile = -1;
  entry->source_unit_tile = -1;
  entry->goto_destination_tile = -1;
  entry->source_unit_moves = -1;
  entry->special_target_city_id = -1;
  entry->special_target_city_owner = -1;
  entry->special_target_extra_owner = -1;
  entry->goto_order_count = 0;
  entry->rally_source_tile = -1;
  entry->rally_production_unit_type = -1;
  entry->rally_veteran_level = -1;
  entry->rally_order_count = 0;
  entry->source_city_id = -1;
  entry->source_city_tile = -1;
  entry->destination_city_id = -1;
  entry->destination_city_tile = -1;
  entry->target_unit_id = -1;
  entry->transport_context_id = -1;
  entry->target_tech = -1;
  entry->vote_no = -1;
  entry->target_government = -1;
  entry->target_build_kind = VUT_NONE;
  entry->target_build_id = -1;
  entry->spaceship_part = -1;
  entry->spaceship_value = -1;
  entry->target_multiplier = -1;
  entry->multiplier_value = -1;
  entry->source_specialist = -1;
  entry->target_specialist = -1;
  entry->worker_task_baseline_activity = ACTIVITY_LAST;
  entry->worker_task_baseline_extra = EXTRA_NONE;
  entry->target_extra = EXTRA_NONE;
  fc_strlcpy(entry->infrastructure_choices, "-",
             sizeof(entry->infrastructure_choices));
  entry->target_activity = ACTIVITY_LAST;
  entry->counterpart_id = -1;
  entry->relation_state = DS_LAST;
  entry->clause_giver_id = -1;
  entry->clause_type = CLAUSE_COUNT;
  entry->clause_value = -1;
  entry->desired_acceptance = -1;
  entry->gold_cost = -1;
  entry->action = ACTION_NONE;
}

static void v2_buffer_add_action(
  struct agent_v2_action_buffer *buffer,
  enum agent_v2_action_kind kind,
  const struct unit *punit, const struct tile *target,
  action_id action, struct act_prob probability)
{
  struct agent_v2_action *entry;

  if (buffer->count >= buffer->capacity) {
    buffer->overflow = TRUE;
    return;
  }
  entry = &buffer->actions[buffer->count++];
  v2_action_init(entry);
  entry->kind = kind;
  entry->unit_id = punit != NULL ? punit->id : -1;
  entry->unit_incarnation = punit != NULL
                            ? v2_existing_incarnation(AGENT_V2_ENTITY_UNIT,
                                                      punit->id)
                            : 0;
  entry->unit_lifecycle_id = punit != NULL
                             ? punit->client.lifecycle_id : 0;
  entry->target_tile = target != NULL ? tile_index(target) : -1;
  entry->action = action;
  if (!v2_normalize_probability(probability, &entry->probability_kind,
                                &entry->probability_min,
                                &entry->probability_max)) {
    buffer->overflow = TRUE;
    buffer->count--;
  }
}

static void v2_buffer_add_relocation_action(
  struct agent_v2_action_buffer *buffer,
  enum agent_v2_action_kind kind, const struct unit *punit,
  const struct tile *target, const struct city *source_city,
  const struct city *destination_city, action_id action,
  struct act_prob probability)
{
  struct agent_v2_action *entry;

  if (punit == NULL || punit->client.lifecycle_id == 0
      || (source_city != NULL && source_city->client.lifecycle_id == 0)
      || (destination_city != NULL
          && destination_city->client.lifecycle_id == 0)
      || buffer->count >= buffer->capacity) {
    buffer->overflow = TRUE;
    return;
  }
  entry = &buffer->actions[buffer->count++];
  v2_action_init(entry);
  entry->kind = kind;
  entry->unit_id = punit->id;
  entry->unit_incarnation = v2_existing_incarnation(
    AGENT_V2_ENTITY_UNIT, punit->id);
  entry->unit_lifecycle_id = punit->client.lifecycle_id;
  entry->target_tile = target != NULL ? tile_index(target) : -1;
  if (source_city != NULL) {
    entry->source_city_id = source_city->id;
    entry->source_city_incarnation = v2_existing_incarnation(
      AGENT_V2_ENTITY_CITY, source_city->id);
    entry->source_city_lifecycle_id = source_city->client.lifecycle_id;
    entry->source_city_tile = tile_index(city_tile(source_city));
  }
  if (destination_city != NULL) {
    entry->destination_city_id = destination_city->id;
    entry->destination_city_incarnation = v2_existing_incarnation(
      AGENT_V2_ENTITY_CITY, destination_city->id);
    entry->destination_city_lifecycle_id =
      destination_city->client.lifecycle_id;
    entry->destination_city_tile = tile_index(city_tile(destination_city));
  }
  entry->action = action;
  if (!v2_normalize_probability(probability, &entry->probability_kind,
                                &entry->probability_min,
                                &entry->probability_max)) {
    buffer->overflow = TRUE;
    buffer->count--;
  }
}

static void v2_buffer_add_player_action(
  struct agent_v2_action_buffer *buffer,
  enum agent_v2_action_kind kind, int target_tech, int max_rate)
{
  struct agent_v2_action *entry;

  if (buffer->count >= buffer->capacity) {
    buffer->overflow = TRUE;
    return;
  }
  entry = &buffer->actions[buffer->count++];
  v2_action_init(entry);
  entry->kind = kind;
  entry->target_tech = target_tech;
  entry->max_rate = max_rate;
  entry->probability_kind = AGENT_V2_PROBABILITY_EXACT;
  entry->probability_min = action_prob_new_certain().min;
  entry->probability_max = action_prob_new_certain().max;
}

static void v2_buffer_add_government_action(
  struct agent_v2_action_buffer *buffer,
  enum agent_v2_action_kind kind, const struct player *self,
  const struct government *target)
{
  struct agent_v2_action *entry;

  if (self == NULL || target == NULL || buffer->count >= buffer->capacity) {
    buffer->overflow = TRUE;
    return;
  }
  entry = &buffer->actions[buffer->count++];
  v2_action_init(entry);
  entry->kind = kind;
  entry->player_id = player_number(self);
  entry->player_incarnation = v2_existing_incarnation(
    AGENT_V2_ENTITY_PLAYER, entry->player_id);
  entry->target_government = government_number(target);
  entry->probability_kind = AGENT_V2_PROBABILITY_EXACT;
  entry->probability_min = action_prob_new_certain().min;
  entry->probability_max = action_prob_new_certain().max;
}

static void v2_buffer_add_city_action(
  struct agent_v2_action_buffer *buffer,
  enum agent_v2_action_kind kind, const struct city *pcity,
  const struct universal *target)
{
  struct agent_v2_action *entry;

  if (pcity == NULL || pcity->client.lifecycle_id == 0
      || buffer->count >= buffer->capacity) {
    buffer->overflow = TRUE;
    return;
  }
  entry = &buffer->actions[buffer->count++];
  v2_action_init(entry);
  entry->kind = kind;
  entry->city_id = pcity->id;
  entry->city_incarnation = v2_existing_incarnation(
    AGENT_V2_ENTITY_CITY, pcity->id);
  entry->city_lifecycle_id = pcity->client.lifecycle_id;
  entry->target_build_kind = target->kind;
  entry->target_build_id = universal_number(target);
  entry->probability_kind = AGENT_V2_PROBABILITY_EXACT;
  entry->probability_min = action_prob_new_certain().min;
  entry->probability_max = action_prob_new_certain().max;
}

static void v2_buffer_add_city_citizen_action(
  struct agent_v2_action_buffer *buffer,
  enum agent_v2_action_kind kind, const struct city *pcity,
  const struct tile *target, Specialist_type_id from,
  Specialist_type_id to)
{
  struct agent_v2_action *entry;

  if (pcity == NULL || pcity->client.lifecycle_id == 0
      || buffer->count >= buffer->capacity) {
    buffer->overflow = TRUE;
    return;
  }
  entry = &buffer->actions[buffer->count++];
  v2_action_init(entry);
  entry->kind = kind;
  entry->city_id = pcity->id;
  entry->city_incarnation = v2_existing_incarnation(
    AGENT_V2_ENTITY_CITY, pcity->id);
  entry->city_lifecycle_id = pcity->client.lifecycle_id;
  entry->target_tile = target != NULL ? tile_index(target) : -1;
  entry->source_specialist = from;
  entry->target_specialist = to;
  entry->probability_kind = AGENT_V2_PROBABILITY_EXACT;
  entry->probability_min = action_prob_new_certain().min;
  entry->probability_max = action_prob_new_certain().max;
}

static void v2_buffer_add_city_management_action(
  struct agent_v2_action_buffer *buffer,
  enum agent_v2_action_kind kind, const struct city *pcity,
  const struct impr_type *improvement)
{
  struct agent_v2_action *entry;

  if (pcity == NULL || pcity->client.lifecycle_id == 0
      || buffer->count >= buffer->capacity) {
    buffer->overflow = TRUE;
    return;
  }
  entry = &buffer->actions[buffer->count++];
  v2_action_init(entry);
  entry->kind = kind;
  entry->city_id = pcity->id;
  entry->city_incarnation = v2_existing_incarnation(
    AGENT_V2_ENTITY_CITY, pcity->id);
  entry->city_lifecycle_id = pcity->client.lifecycle_id;
  if (improvement != NULL) {
    entry->target_build_kind = VUT_IMPROVEMENT;
    entry->target_build_id = improvement_number(improvement);
  }
  entry->probability_kind = AGENT_V2_PROBABILITY_EXACT;
  entry->probability_min = action_prob_new_certain().min;
  entry->probability_max = action_prob_new_certain().max;
}

static bool v2_city_rally_active(const struct city *pcity)
{
  return pcity != NULL && pcity->rally_point.length > 0
         && pcity->rally_point.orders != NULL;
}

static uint64_t v2_city_rally_orders_digest(const struct city *pcity)
{
  return v2_city_rally_active(pcity)
         ? unit_orders_digest((int) pcity->rally_point.length,
                              pcity->rally_point.orders)
         : 0;
}

static bool v2_cma_parameter_valid(const struct cm_parameter *parameter)
{
  if (parameter == NULL || parameter->allow_disorder
      || !parameter->allow_specialists
      || parameter->happy_factor < 0 || parameter->happy_factor > 50) {
    return FALSE;
  }
  output_type_iterate(output) {
    if (parameter->minimal_surplus[output] < -100
        || parameter->minimal_surplus[output] > 100
        || parameter->factor[output] < 0
        || parameter->factor[output] > 25) {
      return FALSE;
    }
  } output_type_iterate_end;
  return TRUE;
}

static bool v2_build_city_rally_target_action(
  const struct city *pcity, const struct tile *target,
  struct client_rally_plan *plan, struct agent_v2_action *action)
{
  const struct client_rally_plan_info *info =
    client_rally_plan_get_info(plan);
  struct player *self = client_player();

  if (pcity == NULL || self == NULL || city_owner(pcity) != self
      || pcity->client.lifecycle_id == 0 || city_tile(pcity) == NULL
      || target == NULL || target == city_tile(pcity)
      || client_tile_get_known(target) == TILE_UNKNOWN
      || info == NULL || info->source_city_tile != tile_index(city_tile(pcity))
      || info->target_tile != tile_index(target)
      || info->production_unit_type < 0 || info->veteran_level < 0
      || info->order_count < 1 || info->order_count >= MAX_LEN_ROUTE
      || info->action_move) {
    return FALSE;
  }
  v2_action_init(action);
  action->kind = AGENT_V2_ACTION_CITY_SET_RALLY;
  action->city_id = pcity->id;
  action->city_incarnation = v2_existing_incarnation(
    AGENT_V2_ENTITY_CITY, pcity->id);
  action->city_lifecycle_id = pcity->client.lifecycle_id;
  action->target_tile = info->target_tile;
  action->rally_source_tile = info->source_city_tile;
  action->rally_production_unit_type = info->production_unit_type;
  action->rally_veteran_level = info->veteran_level;
  action->rally_order_count = info->order_count;
  action->rally_orders_digest = info->orders_digest;
  action->rally_action_move = info->action_move;
  action->probability_kind = AGENT_V2_PROBABILITY_EXACT;
  action->probability_min = action_prob_new_certain().min;
  action->probability_max = action_prob_new_certain().max;
  return TRUE;
}

static bool v2_rally_plan_matches_action(
  const struct client_rally_plan *plan,
  const struct agent_v2_action *action)
{
  const struct client_rally_plan_info *info =
    client_rally_plan_get_info(plan);

  return info != NULL && action != NULL
         && action->kind == AGENT_V2_ACTION_CITY_SET_RALLY
         && info->source_city_tile == action->rally_source_tile
         && info->production_unit_type
            == action->rally_production_unit_type
         && info->veteran_level == action->rally_veteran_level
         && info->target_tile == action->target_tile
         && info->order_count == action->rally_order_count
         && info->orders_digest == action->rally_orders_digest
         && info->action_move == action->rally_action_move
         && !info->action_move;
}

static bool v2_city_worklist_action_available(const struct city *pcity)
{
  if (pcity == NULL) {
    return FALSE;
  }
  if (worklist_length(&pcity->worklist) > 0) {
    return TRUE;
  }
  improvement_iterate(pimprove) {
    struct universal target = {
      .kind = VUT_IMPROVEMENT,
      .value = { .building = pimprove }
    };

    if (can_city_build_later(&wld.map, pcity, &target)) {
      return TRUE;
    }
  } improvement_iterate_end;
  unit_type_iterate(putype) {
    struct universal target = {
      .kind = VUT_UTYPE,
      .value = { .utype = putype }
    };

    if (can_city_build_later(&wld.map, pcity, &target)) {
      return TRUE;
    }
  } unit_type_iterate_end;
  return FALSE;
}

static void v2_build_city_management_actions(
  const struct city *pcity, struct agent_v2_action_buffer *buffer)
{
  struct player *self = client_player();

  if (pcity == NULL || self == NULL || city_owner(pcity) != self
      || pcity->client.lifecycle_id == 0) {
    return;
  }
  if (v2_city_worklist_action_available(pcity)) {
    v2_buffer_add_city_management_action(
      buffer, AGENT_V2_ACTION_CITY_SET_WORKLIST, pcity, NULL);
  }
  v2_buffer_add_city_management_action(
    buffer, AGENT_V2_ACTION_CITY_SET_OPTIONS, pcity, NULL);
  v2_buffer_add_city_management_action(
    buffer, AGENT_V2_ACTION_CITY_RENAME, pcity, NULL);
  v2_buffer_add_city_management_action(
    buffer, AGENT_V2_ACTION_CITY_SET_GOVERNOR, pcity, NULL);
  if (cma_is_city_under_agent(pcity, NULL)) {
    v2_buffer_add_city_management_action(
      buffer, AGENT_V2_ACTION_CITY_CLEAR_GOVERNOR, pcity, NULL);
  }
  if (v2_city_rally_active(pcity)) {
    size_t index = buffer->count;

    v2_buffer_add_city_management_action(
      buffer, AGENT_V2_ACTION_CITY_CLEAR_RALLY, pcity, NULL);
    if (buffer->count == index + 1 && city_tile(pcity) != NULL) {
      buffer->actions[index].rally_source_tile =
        tile_index(city_tile(pcity));
    } else if (buffer->count == index + 1) {
      buffer->overflow = TRUE;
    }
  }
  if (!pcity->did_sell) {
    improvement_iterate(pimprove) {
      if (can_city_sell_building(pcity, pimprove)) {
        v2_buffer_add_city_management_action(
          buffer, AGENT_V2_ACTION_CITY_SELL_IMPROVEMENT, pcity, pimprove);
      }
    } improvement_iterate_end;
  }
}

static Specialist_type_id v2_first_city_specialist(
  const struct city *pcity)
{
  normal_specialist_type_iterate(specialist) {
    if (pcity->specialists[specialist] > 0) {
      return specialist;
    }
  } normal_specialist_type_iterate_end;
  return -1;
}

static void v2_build_city_citizen_actions(
  const struct city *pcity, struct agent_v2_action_buffer *buffer)
{
  Specialist_type_id first;

  if (pcity == NULL || city_owner(pcity) != client_player()
      || city_tile(pcity) == NULL || pcity->client.lifecycle_id == 0
      || cma_is_city_under_agent(pcity, NULL)) {
    return;
  }
  first = v2_first_city_specialist(pcity);
  city_tile_iterate(&wld.map, city_map_radius_sq_get(pcity),
                    city_tile(pcity), ptile) {
    if (is_free_worked(pcity, ptile)) {
      continue;
    }
    if (tile_worked(ptile) == pcity) {
      v2_buffer_add_city_citizen_action(
        buffer, AGENT_V2_ACTION_CITY_UNWORK_TILE, pcity, ptile,
        -1, DEFAULT_SPECIALIST);
    } else if (first >= 0 && city_can_work_tile(pcity, ptile)) {
      v2_buffer_add_city_citizen_action(
        buffer, AGENT_V2_ACTION_CITY_WORK_TILE, pcity, ptile,
        first, -1);
    }
  } city_tile_iterate_end;

  normal_specialist_type_iterate(from) {
    if (pcity->specialists[from] == 0) {
      continue;
    }
    normal_specialist_type_iterate(to) {
      if (from != to && city_can_use_specialist(pcity, to)) {
        v2_buffer_add_city_citizen_action(
          buffer, AGENT_V2_ACTION_CITY_SET_SPECIALIST, pcity, NULL,
          from, to);
      }
    } normal_specialist_type_iterate_end;
  } normal_specialist_type_iterate_end;
}

static const struct worker_task *v2_city_worker_task_at(
  const struct city *pcity, const struct tile *ptile)
{
  if (pcity == NULL || ptile == NULL) {
    return NULL;
  }
  worker_task_list_iterate(pcity->task_reqs, ptask) {
    if (ptask->ptile == ptile) {
      return ptask;
    }
  } worker_task_list_iterate_end;
  return NULL;
}

static bool v2_city_worker_task_tile_allowed(
  const struct player *self, const struct city *pcity,
  const struct tile *ptile)
{
  const struct player *owner;

  if (self == NULL || pcity == NULL || city_owner(pcity) != self
      || ptile == NULL || city_tile(pcity) == NULL
      || !city_map_includes_tile(pcity, ptile)
      || client_tile_get_known(ptile) != TILE_KNOWN_SEEN) {
    return FALSE;
  }
  owner = tile_owner(ptile);
  return owner == NULL || owner == self;
}

static bool v2_city_worker_task_choice(
  const struct player *self, struct tile *ptile,
  enum unit_activity activity, const struct extra_type **target)
{
  struct terrain *terrain;
  struct universal terrain_universal;
  enum extra_cause cause;
  enum extra_rmcause rmcause;

  if (self == NULL || ptile == NULL || target == NULL
      || client_tile_get_known(ptile) != TILE_KNOWN_SEEN) {
    return FALSE;
  }
  terrain = tile_terrain(ptile);
  if (terrain == NULL) {
    return FALSE;
  }
  *target = NULL;
  terrain_universal.kind = VUT_TERRAIN;
  terrain_universal.value.terrain = terrain;
  switch (activity) {
  case ACTIVITY_PLANT:
    return terrain->plant_result != NULL
           && action_id_univs_not_blocking(
                ACTION_PLANT, NULL, &terrain_universal);
  case ACTIVITY_CULTIVATE:
    return terrain->cultivate_result != NULL
           && action_id_univs_not_blocking(
                ACTION_CULTIVATE, NULL, &terrain_universal);
  case ACTIVITY_TRANSFORM:
    return terrain->transform_result != NULL
           && terrain->transform_result != terrain
           && action_id_univs_not_blocking(
                ACTION_TRANSFORM_TERRAIN, NULL, &terrain_universal);
  case ACTIVITY_MINE:
    if (!action_id_univs_not_blocking(
          ACTION_MINE, NULL, &terrain_universal)) {
      return FALSE;
    }
    break;
  case ACTIVITY_IRRIGATE:
    if (!action_id_univs_not_blocking(
          ACTION_IRRIGATE, NULL, &terrain_universal)) {
      return FALSE;
    }
    break;
  case ACTIVITY_GEN_ROAD:
  case ACTIVITY_CLEAN:
    break;
  default:
    return FALSE;
  }

  cause = activity_to_extra_cause(activity);
  rmcause = activity_to_extra_rmcause(activity);
  if (cause != EC_NONE) {
    *target = next_extra_for_tile(ptile, cause, self, NULL);
  } else if (rmcause != ERM_NONE) {
    *target = prev_extra_in_tile(ptile, rmcause, self, NULL);
  }
  return *target != NULL;
}

static void v2_buffer_add_city_worker_task_action(
  struct agent_v2_action_buffer *buffer,
  enum agent_v2_action_kind kind, const struct city *pcity,
  const struct tile *ptile, const struct worker_task *baseline,
  enum unit_activity activity, const struct extra_type *target)
{
  struct agent_v2_action *entry;

  if (buffer->count >= buffer->capacity || pcity == NULL || ptile == NULL
      || pcity->client.lifecycle_id == 0) {
    buffer->overflow = TRUE;
    return;
  }
  entry = &buffer->actions[buffer->count++];
  v2_action_init(entry);
  entry->kind = kind;
  entry->city_id = pcity->id;
  entry->city_incarnation = v2_existing_incarnation(
    AGENT_V2_ENTITY_CITY, pcity->id);
  entry->city_lifecycle_id = pcity->client.lifecycle_id;
  entry->target_tile = tile_index(ptile);
  entry->worker_task_baseline_present = baseline != NULL;
  entry->worker_task_baseline_activity = baseline != NULL
                                         ? baseline->act : ACTIVITY_LAST;
  entry->worker_task_baseline_extra = baseline != NULL
                                      && baseline->tgt != NULL
                                      ? extra_number(baseline->tgt) : EXTRA_NONE;
  entry->worker_task_baseline_want = baseline != NULL ? baseline->want : 0;
  entry->target_activity = activity;
  entry->target_extra = target != NULL ? extra_number(target) : EXTRA_NONE;
  entry->probability_kind = AGENT_V2_PROBABILITY_EXACT;
  entry->probability_min = action_prob_new_certain().min;
  entry->probability_max = action_prob_new_certain().max;
}

static void v2_build_city_worker_task_actions(
  const struct city *pcity, struct agent_v2_action_buffer *buffer)
{
  static const enum unit_activity activities[] = {
    ACTIVITY_CULTIVATE, ACTIVITY_MINE, ACTIVITY_IRRIGATE,
    ACTIVITY_TRANSFORM, ACTIVITY_CLEAN, ACTIVITY_GEN_ROAD, ACTIVITY_PLANT
  };
  const struct player *self = client_player();

  if (pcity == NULL || self == NULL || city_owner(pcity) != self
      || city_tile(pcity) == NULL || pcity->client.lifecycle_id == 0) {
    return;
  }
  city_tile_iterate(&wld.map, city_map_radius_sq_get(pcity),
                    city_tile(pcity), ptile) {
    const struct worker_task *baseline;
    size_t activity_index;

    if (!v2_city_worker_task_tile_allowed(self, pcity, ptile)) {
      continue;
    }
    baseline = v2_city_worker_task_at(pcity, ptile);
    if (baseline != NULL) {
      v2_buffer_add_city_worker_task_action(
        buffer, AGENT_V2_ACTION_CITY_REMOVE_WORKER_TASK,
        pcity, ptile, baseline, ACTIVITY_LAST, NULL);
    }
    for (activity_index = 0; activity_index < ARRAY_SIZE(activities);
         activity_index++) {
      const struct extra_type *target;
      enum unit_activity activity = activities[activity_index];
      int target_extra;

      if (!v2_city_worker_task_choice(self, ptile, activity, &target)) {
        continue;
      }
      target_extra = target != NULL ? extra_number(target) : EXTRA_NONE;
      if (baseline != NULL && baseline->act == activity
          && (baseline->tgt != NULL
              ? extra_number(baseline->tgt) : EXTRA_NONE) == target_extra
          && baseline->want == AGENT_V2_CITY_WORKER_TASK_WANT) {
        continue;
      }
      v2_buffer_add_city_worker_task_action(
        buffer,
        baseline == NULL ? AGENT_V2_ACTION_CITY_REQUEST_WORKER_TASK
                         : AGENT_V2_ACTION_CITY_CHANGE_WORKER_TASK,
        pcity, ptile, baseline, activity, target);
    }
  } city_tile_iterate_end;
}

static void v2_buffer_add_worker_action(
  struct agent_v2_action_buffer *buffer,
  enum agent_v2_action_kind kind, const struct unit *punit,
  enum unit_activity activity, const struct extra_type *target,
  action_id action, struct act_prob probability)
{
  struct agent_v2_action *entry;
  enum agent_v2_probability_kind candidate_kind;
  int target_extra = target != NULL ? extra_index(target) : EXTRA_NONE;
  int candidate_min;
  int candidate_max;
  size_t i;

  if (!v2_normalize_probability(probability, &candidate_kind,
                                &candidate_min, &candidate_max)) {
    buffer->overflow = TRUE;
    return;
  }
  if (kind == AGENT_V2_ACTION_WORKER_START
      && candidate_kind == AGENT_V2_PROBABILITY_NOT_IMPLEMENTED) {
    return;
  }

  /* The public capability is deliberately canonicalized to activity+extra,
   * not the private ruleset action variant.  Keep one deterministic native
   * action for that semantic outcome so two variants cannot be presented as
   * distinguishable and then acknowledged by the same cache transition. */
  if (kind == AGENT_V2_ACTION_WORKER_START) {
    for (i = 0; i < buffer->count; i++) {
      struct agent_v2_action *existing = &buffer->actions[i];

      if (existing->kind == kind
          && existing->unit_id == punit->id
          && existing->target_activity == activity
          && existing->target_extra == target_extra) {
        if (v2_probability_preferred(
              candidate_kind, candidate_min, candidate_max, action,
              existing)) {
          existing->action = action;
          existing->probability_kind = candidate_kind;
          existing->probability_min = candidate_min;
          existing->probability_max = candidate_max;
        }
        return;
      }
    }
  }

  if (buffer->count >= buffer->capacity) {
    buffer->overflow = TRUE;
    return;
  }
  entry = &buffer->actions[buffer->count++];
  v2_action_init(entry);
  entry->kind = kind;
  entry->unit_id = punit->id;
  entry->unit_incarnation = v2_existing_incarnation(
    AGENT_V2_ENTITY_UNIT, punit->id);
  entry->unit_lifecycle_id = punit->client.lifecycle_id;
  entry->target_extra = target_extra;
  entry->target_activity = activity;
  entry->action = action;
  entry->probability_kind = candidate_kind;
  entry->probability_min = candidate_min;
  entry->probability_max = candidate_max;
}

static const char *v2_build_kind_name(int kind)
{
  switch (kind) {
  case VUT_UTYPE:
    return "unit";
  case VUT_IMPROVEMENT:
    return "improvement";
  case VUT_NONE:
    return "none";
  default:
    return NULL;
  }
}

static const char *v2_new_citizens_name(const struct city *pcity)
{
  bool science;
  bool gold;

  if (pcity == NULL) {
    return NULL;
  }
  science = BV_ISSET(pcity->city_options, CITYO_SCIENCE_SPECIALISTS);
  gold = BV_ISSET(pcity->city_options, CITYO_GOLD_SPECIALISTS);
  /* Match the native growth and GUI precedence while separately exposing the
   * legacy conflicting-bit condition so city.set_options can repair it. */
  return science ? "science" : (gold ? "gold" : "default");
}

static const char *v2_worker_activity_name(enum unit_activity activity)
{
  switch (activity) {
  case ACTIVITY_IDLE:
    return "idle";
  case ACTIVITY_CULTIVATE:
    return "cultivate";
  case ACTIVITY_MINE:
    return "mine";
  case ACTIVITY_IRRIGATE:
    return "irrigate";
  case ACTIVITY_PILLAGE:
    return "pillage";
  case ACTIVITY_TRANSFORM:
    return "transform";
  case ACTIVITY_CLEAN:
    return "clean";
  case ACTIVITY_BASE:
    return "base";
  case ACTIVITY_GEN_ROAD:
    return "road";
  case ACTIVITY_PLANT:
    return "plant";
  case ACTIVITY_FORTIFIED:
    return "fortified";
  case ACTIVITY_SENTRY:
    return "sentry";
  case ACTIVITY_GOTO:
    return "goto";
  case ACTIVITY_EXPLORE:
    return "explore";
  case ACTIVITY_FORTIFYING:
    return "fortifying";
  case ACTIVITY_CONVERT:
    return "convert";
  case ACTIVITY_LAST:
    return "none";
  }
  return NULL;
}

static const char *v2_unit_controller_name(enum server_side_agent controller)
{
  switch (controller) {
  case SSA_NONE:
    return "none";
  case SSA_AUTOWORKER:
    return "auto_work";
  case SSA_AUTOEXPLORE:
    return "auto_explore";
  case SSA_COUNT:
    break;
  }
  return NULL;
}

static enum fc_agent_v2_automation_controller v2_automation_controller(
  enum server_side_agent controller)
{
  switch (controller) {
  case SSA_NONE:
    return FC_AGENT_V2_CONTROLLER_NONE;
  case SSA_AUTOWORKER:
    return FC_AGENT_V2_CONTROLLER_AUTO_WORK;
  case SSA_AUTOEXPLORE:
    return FC_AGENT_V2_CONTROLLER_AUTO_EXPLORE;
  case SSA_COUNT:
    return FC_AGENT_V2_CONTROLLER_NONE;
  }
  return FC_AGENT_V2_CONTROLLER_NONE;
}

static enum fc_agent_v2_automation_command v2_automation_command(
  enum agent_v2_action_kind kind)
{
  switch (kind) {
  case AGENT_V2_ACTION_UNIT_AUTO_WORK:
    return FC_AGENT_V2_AUTOMATION_WORK;
  case AGENT_V2_ACTION_UNIT_AUTO_EXPLORE:
    return FC_AGENT_V2_AUTOMATION_EXPLORE;
  case AGENT_V2_ACTION_UNIT_CANCEL_AUTOMATION:
    return FC_AGENT_V2_AUTOMATION_CANCEL;
  default:
    return FC_AGENT_V2_AUTOMATION_CANCEL;
  }
}

static int v2_player_max_rate(const struct player *self)
{
  int max_rate = get_player_bonus(self, EFT_MAX_RATES);

  if (max_rate == 0) {
    return 100;
  }
  return CLIP(34, max_rate, 100);
}

static const char *v2_revolution_method_name(enum revolen_type method)
{
  switch (method) {
  case REVOLEN_FIXED:
    return "fixed";
  case REVOLEN_RANDOM:
    return "random";
  case REVOLEN_QUICKENING:
    return "quickening";
  case REVOLEN_RANDQUICK:
    return "random_quickening";
  }
  return NULL;
}

static const char *v2_diplomacy_cancel_reason_name(enum dipl_reason reason)
{
  switch (reason) {
  case DIPL_OK:
    return "allowed";
  case DIPL_SENATE_BLOCKING:
    return "senate_blocking";
  case DIPL_ERROR:
    return "not_allowed";
  case DIPL_ALLIANCE_PROBLEM_US:
    return "alliance_problem_us";
  case DIPL_ALLIANCE_PROBLEM_THEM:
    return "alliance_problem_them";
  }
  return NULL;
}

static int v2_revolution_max_turns(void)
{
  server_setting_id setting = server_setting_by_name("revolen");

  if (setting == SERVER_SETTING_NONE || !server_setting_exists(setting)
      || server_setting_type_get(setting) != SST_INT) {
    return -1;
  }
  return CLIP(GAME_MIN_REVOLUTION_LENGTH,
              server_setting_value_int_get(setting),
              GAME_MAX_REVOLUTION_LENGTH);
}

static int v2_government_id(const struct government *government)
{
  return government != NULL ? government_number(government) : -1;
}

static bool v2_government_change_available(
  const struct player *self, const struct government *target)
{
  const struct government *current;
  const struct government *selected;
  const struct government *during = game.government_during_revolution;
  bool has_no_anarchy;

  if (self == NULL || target == NULL || during == NULL
      || target == during) {
    return FALSE;
  }
  current = government_of_player(self);
  selected = self->target_government;
  has_no_anarchy = get_player_bonus(self, EFT_NO_ANARCHY) > 0;
  return target != current && target != selected
         && can_change_to_government((struct player *) self, target)
         && fc_agent_v2_government_change_observable(
              self->revolution_finishes, game.info.turn, has_no_anarchy);
}

static bool v2_government_revolution_available(const struct player *self)
{
  const struct government *during = game.government_during_revolution;

  return self != NULL && during != NULL
         && fc_agent_v2_revolution_available(
              untargeted_revolution_allowed(),
              can_change_to_government((struct player *) self, during),
              government_of_player(self) == during,
              self->target_government == during);
}

static void v2_build_government_actions(
  struct player *self, struct agent_v2_action_buffer *buffer)
{
  if (government_count() < 1
      || government_count() > FC_AGENT_V2_MAX_GOVERNMENTS) {
    buffer->overflow = TRUE;
    return;
  }
  if (v2_government_revolution_available(self)) {
    v2_buffer_add_government_action(
      buffer, AGENT_V2_ACTION_GOVERNMENT_REVOLUTION, self,
      game.government_during_revolution);
  }
  governments_iterate(government) {
    if (v2_government_change_available(self, government)) {
      v2_buffer_add_government_action(
        buffer, AGENT_V2_ACTION_GOVERNMENT_CHANGE, self, government);
    }
  } governments_iterate_end;
}

static const char *v2_spaceship_state_name(enum spaceship_state state)
{
  switch (state) {
  case SSHIP_NONE:
    return "none";
  case SSHIP_STARTED:
    return "started";
  case SSHIP_LAUNCHED:
    return "launched";
  case SSHIP_ARRIVED:
    return "arrived";
  }
  return NULL;
}

static const char *v2_spaceship_part_name(int part)
{
  switch (part) {
  case SSHIP_PLACE_STRUCTURAL:
    return "structural";
  case SSHIP_PLACE_FUEL:
    return "fuel";
  case SSHIP_PLACE_PROPULSION:
    return "propulsion";
  case SSHIP_PLACE_HABITATION:
    return "habitation";
  case SSHIP_PLACE_LIFE_SUPPORT:
    return "life_support";
  case SSHIP_PLACE_SOLAR_PANELS:
    return "solar_panels";
  }
  return NULL;
}

static int v2_scaled_nonnegative(double value, double factor)
{
  if (!(value >= 0.0)) {
    return 0;
  }
  if (value >= INT_MAX / factor) {
    return INT_MAX;
  }
  return (int) (value * factor + 0.5);
}

static bool v2_spaceship_place_available(
  const struct player_spaceship *ship, int part, int value)
{
  if (ship == NULL || ship->state != SSHIP_STARTED) {
    return FALSE;
  }
  switch (part) {
  case SSHIP_PLACE_STRUCTURAL:
    return value >= 0 && value < NUM_SS_STRUCTURALS
           && !BV_ISSET(ship->structure, value)
           && num_spaceship_structurals_placed(ship) < ship->structurals
           && (value == 0
               || BV_ISSET(ship->structure,
                           structurals_info[value].required));
  case SSHIP_PLACE_FUEL:
    return value == ship->fuel + 1
           && ship->fuel + ship->propulsion < ship->components
           && value <= NUM_SS_COMPONENTS / 2;
  case SSHIP_PLACE_PROPULSION:
    return value == ship->propulsion + 1
           && ship->fuel + ship->propulsion < ship->components
           && value <= NUM_SS_COMPONENTS / 2;
  case SSHIP_PLACE_HABITATION:
    return value == ship->habitation + 1
           && ship->habitation + ship->life_support + ship->solar_panels
              < ship->modules
           && value <= NUM_SS_MODULES / 3;
  case SSHIP_PLACE_LIFE_SUPPORT:
    return value == ship->life_support + 1
           && ship->habitation + ship->life_support + ship->solar_panels
              < ship->modules
           && value <= NUM_SS_MODULES / 3;
  case SSHIP_PLACE_SOLAR_PANELS:
    return value == ship->solar_panels + 1
           && ship->habitation + ship->life_support + ship->solar_panels
              < ship->modules
           && value <= NUM_SS_MODULES / 3;
  }
  return FALSE;
}

static bool v2_spaceship_launch_available(const struct player *self)
{
  return self != NULL && self->spaceship.state == SSHIP_STARTED
         && self->spaceship.success_rate > 0.0
         && player_primary_capital(self) != NULL;
}

static bool v2_spaceship_action_still_legal(
  const struct player *self, const struct agent_v2_action *action)
{
  if (self == NULL || action == NULL
      || action->player_id != player_number(self)
      || action->player_incarnation == 0
      || action->player_incarnation
         != v2_existing_incarnation(AGENT_V2_ENTITY_PLAYER,
                                    player_number(self))) {
    return FALSE;
  }
  switch (action->kind) {
  case AGENT_V2_ACTION_SPACESHIP_PLACE:
    return v2_spaceship_place_available(
      &self->spaceship, action->spaceship_part, action->spaceship_value);
  case AGENT_V2_ACTION_SPACESHIP_LAUNCH:
    return action->spaceship_part == -1 && action->spaceship_value == -1
           && v2_spaceship_launch_available(self);
  default:
    return FALSE;
  }
}

static bool v2_spaceship_unchanged_except_structural(
  const struct player_spaceship *before,
  const struct player_spaceship *after, int changed_slot)
{
  int slot;

  if (before->structurals != after->structurals
      || before->components != after->components
      || before->modules != after->modules
      || before->fuel != after->fuel
      || before->propulsion != after->propulsion
      || before->habitation != after->habitation
      || before->life_support != after->life_support
      || before->solar_panels != after->solar_panels
      || before->launch_year != after->launch_year) {
    return FALSE;
  }
  for (slot = 0; slot < NUM_SS_STRUCTURALS; slot++) {
    bool old_placed = BV_ISSET(before->structure, slot);
    bool new_placed = BV_ISSET(after->structure, slot);

    if (slot == changed_slot) {
      if (old_placed || !new_placed) {
        return FALSE;
      }
    } else if (old_placed != new_placed) {
      return FALSE;
    }
  }
  return TRUE;
}

static bool v2_spaceship_unchanged_except_counter(
  const struct player_spaceship *before,
  const struct player_spaceship *after, int changed_part, int desired)
{
  return before->structurals == after->structurals
         && before->components == after->components
         && before->modules == after->modules
         && BV_ARE_EQUAL(before->structure, after->structure)
         && before->launch_year == after->launch_year
         && after->fuel
            == (changed_part == SSHIP_PLACE_FUEL ? desired : before->fuel)
         && after->propulsion
            == (changed_part == SSHIP_PLACE_PROPULSION
                ? desired : before->propulsion)
         && after->habitation
            == (changed_part == SSHIP_PLACE_HABITATION
                ? desired : before->habitation)
         && after->life_support
            == (changed_part == SSHIP_PLACE_LIFE_SUPPORT
                ? desired : before->life_support)
         && after->solar_panels
            == (changed_part == SSHIP_PLACE_SOLAR_PANELS
                ? desired : before->solar_panels);
}

static void v2_buffer_add_spaceship_action(
  struct agent_v2_action_buffer *buffer,
  enum agent_v2_action_kind kind, const struct player *self,
  int part, int value)
{
  struct agent_v2_action *entry;

  if (buffer->count >= buffer->capacity) {
    buffer->overflow = TRUE;
    return;
  }
  entry = &buffer->actions[buffer->count++];
  v2_action_init(entry);
  entry->kind = kind;
  entry->player_id = player_number(self);
  entry->player_incarnation = v2_existing_incarnation(
    AGENT_V2_ENTITY_PLAYER, entry->player_id);
  entry->spaceship_part = part;
  entry->spaceship_value = value;
  entry->probability_kind = AGENT_V2_PROBABILITY_EXACT;
  entry->probability_min = action_prob_new_certain().min;
  entry->probability_max = action_prob_new_certain().max;
}

static void v2_build_spaceship_actions(
  struct player *self, struct agent_v2_action_buffer *buffer)
{
  int slot;
  int part;

  if (self == NULL || self->spaceship.state != SSHIP_STARTED) {
    return;
  }
  for (slot = 0; slot < NUM_SS_STRUCTURALS; slot++) {
    if (v2_spaceship_place_available(
          &self->spaceship, SSHIP_PLACE_STRUCTURAL, slot)) {
      v2_buffer_add_spaceship_action(
        buffer, AGENT_V2_ACTION_SPACESHIP_PLACE, self,
        SSHIP_PLACE_STRUCTURAL, slot);
    }
  }
  for (part = SSHIP_PLACE_FUEL;
       part <= SSHIP_PLACE_SOLAR_PANELS; part++) {
    int value;

    switch (part) {
    case SSHIP_PLACE_FUEL:
      value = self->spaceship.fuel + 1;
      break;
    case SSHIP_PLACE_PROPULSION:
      value = self->spaceship.propulsion + 1;
      break;
    case SSHIP_PLACE_HABITATION:
      value = self->spaceship.habitation + 1;
      break;
    case SSHIP_PLACE_LIFE_SUPPORT:
      value = self->spaceship.life_support + 1;
      break;
    case SSHIP_PLACE_SOLAR_PANELS:
      value = self->spaceship.solar_panels + 1;
      break;
    default:
      continue;
    }
    if (v2_spaceship_place_available(&self->spaceship, part, value)) {
      v2_buffer_add_spaceship_action(
        buffer, AGENT_V2_ACTION_SPACESHIP_PLACE, self, part, value);
    }
  }
  if (v2_spaceship_launch_available(self)) {
    v2_buffer_add_spaceship_action(
      buffer, AGENT_V2_ACTION_SPACESHIP_LAUNCH, self, -1, -1);
  }
}

static bool v2_multiplier_value_valid(const struct multiplier *pmul,
                                      int value)
{
  return pmul != NULL && pmul->step > 0 && pmul->stop >= pmul->start
         && value >= pmul->start && value <= pmul->stop
         && ((int64_t) value - (int64_t) pmul->start) % pmul->step == 0;
}

static int v2_multiplier_choice_count(const struct multiplier *pmul)
{
  int64_t span;
  int64_t count;

  if (pmul == NULL || pmul->step <= 0 || pmul->stop < pmul->start) {
    return -1;
  }
  span = (int64_t) pmul->stop - (int64_t) pmul->start;
  if (span % pmul->step != 0) {
    return -1;
  }
  count = span / pmul->step + 1;
  return count > 0 && count <= INT_MAX ? (int) count : -1;
}

static bool v2_multiplier_action_still_legal(
  struct player *self, const struct agent_v2_action *action)
{
  struct multiplier *pmul;

  if (self == NULL || action == NULL
      || action->player_id != player_number(self)
      || action->player_incarnation == 0
      || action->player_incarnation
         != v2_existing_incarnation(AGENT_V2_ENTITY_PLAYER,
                                    player_number(self))
      || action->target_multiplier < 0
      || action->target_multiplier >= multiplier_count()) {
    return FALSE;
  }
  pmul = multiplier_by_number(action->target_multiplier);
  return pmul != NULL && !pmul->ruledit_disabled
         && multiplier_can_be_changed(pmul, self)
         && v2_multiplier_value_valid(pmul, action->multiplier_value)
         && player_multiplier_target_value(self, pmul)
            != action->multiplier_value;
}

static void v2_build_multiplier_actions(
  struct player *self, struct agent_v2_action_buffer *buffer)
{
  multipliers_re_active_iterate(pmul) {
    int count = v2_multiplier_choice_count(pmul);
    int choice;

    if (count < 1) {
      buffer->overflow = TRUE;
      return;
    }
    if (!multiplier_can_be_changed(pmul, self)) {
      continue;
    }
    for (choice = 0; choice < count; choice++) {
      int64_t candidate = (int64_t) pmul->start
                          + (int64_t) choice * pmul->step;
      struct agent_v2_action *entry;

      if (candidate == player_multiplier_target_value(self, pmul)) {
        continue;
      }
      if (candidate < INT_MIN || candidate > INT_MAX
          || buffer->count >= buffer->capacity) {
        buffer->overflow = TRUE;
        return;
      }
      entry = &buffer->actions[buffer->count++];
      v2_action_init(entry);
      entry->kind = AGENT_V2_ACTION_MULTIPLIER_SET;
      entry->player_id = player_number(self);
      entry->player_incarnation = v2_existing_incarnation(
        AGENT_V2_ENTITY_PLAYER, entry->player_id);
      entry->target_multiplier = multiplier_number(pmul);
      entry->multiplier_value = (int) candidate;
      entry->probability_kind = AGENT_V2_PROBABILITY_EXACT;
      entry->probability_min = action_prob_new_certain().min;
      entry->probability_max = action_prob_new_certain().max;
    }
  } multipliers_re_active_iterate_end;
}

static bool v2_research_can_target(const struct research *research,
                                   Tech_type_id tech)
{
  if (research == NULL || tech == A_UNSET) {
    return FALSE;
  }
  if (tech == A_FUTURE) {
    return research_future_next(research);
  }
  return valid_advance_by_number(tech) != NULL
         && research_invention_state(research, tech)
            == TECH_PREREQS_KNOWN;
}

static bool v2_research_can_goal(const struct research *research,
                                 Tech_type_id tech)
{
  if (research == NULL) {
    return FALSE;
  }
  if (tech == A_UNSET) {
    return TRUE;
  }
  if (tech == A_FUTURE) {
    return research_future_next(research);
  }
  return valid_advance_by_number(tech) != NULL
         && research_invention_reachable(research, tech)
         && research_invention_state(research, tech) != TECH_KNOWN;
}

static bool v2_actions_ready(
  const struct fc_agent_v2_phase_evidence *phase)
{
  return phase != NULL
         && fc_agent_v2_action_phase_ready(
           v2_seat_authorized, v2_cache_coherent(),
           can_client_issue_orders(), phase->alive, phase->active_phase,
           phase->phase_done, is_server_busy());
}

static bool v2_vote_active(const struct voteinfo *vote)
{
  return vote != NULL && vote->vote_no >= 0
         && !vote->resolved && vote->remove_time == 0;
}

static bool v2_vote_can_vote(const struct player *self,
                             const struct voteinfo *vote)
{
  return v2_vote_active(vote) && self != NULL && is_human(self)
         && v2_seat_authorized && v2_cache_coherent()
         && can_client_control() && !is_server_busy();
}

static const char *v2_client_vote_name(enum client_vote_type vote)
{
  switch (vote) {
  case CVT_NONE:
    return "none";
  case CVT_YES:
    return "yes";
  case CVT_NO:
    return "no";
  case CVT_ABSTAIN:
    return "abstain";
  }
  return NULL;
}

static uint64_t v2_vote_signature(const struct voteinfo *vote)
{
  uint64_t hash = UINT64_C(1469598103934665603);

  if (vote == NULL) {
    return 0;
  }
  hash = v2_hash_bytes(hash, &vote->vote_no, sizeof(vote->vote_no));
  hash = v2_hash_bytes(hash, vote->user, strlen(vote->user) + 1);
  hash = v2_hash_bytes(hash, vote->desc, strlen(vote->desc) + 1);
  hash = v2_hash_bytes(hash, &vote->percent_required,
                       sizeof(vote->percent_required));
  hash = v2_hash_bytes(hash, &vote->flags, sizeof(vote->flags));
  hash = v2_hash_bytes(hash, &vote->yes, sizeof(vote->yes));
  hash = v2_hash_bytes(hash, &vote->no, sizeof(vote->no));
  hash = v2_hash_bytes(hash, &vote->abstain, sizeof(vote->abstain));
  hash = v2_hash_bytes(hash, &vote->num_voters, sizeof(vote->num_voters));
  hash = v2_hash_bytes(hash, &vote->client_vote,
                       sizeof(vote->client_vote));
  return hash;
}

static const struct voteinfo *v2_vote_by_number(int vote_no)
{
  int size = voteinfo_queue_size();
  int index;

  if (vote_no < 0 || size < 0 || size > FC_AGENT_V2_MAX_VOTES) {
    return NULL;
  }
  for (index = 0; index < size; index++) {
    const struct voteinfo *vote = voteinfo_queue_get(index);

    if (vote != NULL && vote->vote_no == vote_no) {
      return vote;
    }
  }
  return NULL;
}

static bool v2_vote_action_still_legal(
  const struct player *self, const struct agent_v2_action *action)
{
  const struct voteinfo *vote;

  if (self == NULL || action == NULL
      || action->kind != AGENT_V2_ACTION_PLAYER_CAST_VOTE
      || action->player_id != player_number(self)
      || action->player_incarnation == 0
      || action->player_incarnation
         != v2_existing_incarnation(AGENT_V2_ENTITY_PLAYER,
                                    player_number(self))
      || (vote = v2_vote_by_number(action->vote_no)) == NULL) {
    return FALSE;
  }
  return v2_vote_can_vote(self, vote)
         && v2_vote_signature(vote) == action->vote_signature;
}

static bool v2_parse_vote_argument(const char *text,
                                   enum client_vote_type *vote)
{
  if (text == NULL || vote == NULL || strncmp(text, "vote=", 5) != 0) {
    return FALSE;
  }
  if (strcmp(text + 5, "yes") == 0) {
    *vote = CVT_YES;
  } else if (strcmp(text + 5, "no") == 0) {
    *vote = CVT_NO;
  } else if (strcmp(text + 5, "abstain") == 0) {
    *vote = CVT_ABSTAIN;
  } else {
    return FALSE;
  }
  return TRUE;
}

static void v2_build_vote_rows(const struct player *self)
{
  int size = voteinfo_queue_size();
  int index;

  if (size < 0 || size > FC_AGENT_V2_MAX_VOTES) {
    v2_overflow = TRUE;
    return;
  }
  for (index = 0; index < size; index++) {
    const struct voteinfo *vote = voteinfo_queue_get(index);
    const char *current_vote;
    char description[AGENT_V2_ROW_MAX];

    if (vote == NULL || vote->vote_no < 0) {
      v2_overflow = TRUE;
      return;
    }
    if (!v2_vote_active(vote)) {
      continue;
    }
    current_vote = v2_client_vote_name(vote->client_vote);
    if (current_vote == NULL
        || vote->percent_required < 0 || vote->percent_required > 100
        || vote->yes < 0 || vote->no < 0 || vote->abstain < 0
        || vote->num_voters < 0
        || !v2_encode_row_value(vote->desc, description,
                                sizeof(description))) {
      v2_overflow = TRUE;
      return;
    }
    v2_add_row(FC_AGENT_V2_ROW_VOTE,
               vote->vote_no, description, vote->yes, vote->no,
               vote->abstain, vote->num_voters, vote->percent_required,
               (vote->flags & AGENT_V2_VOTE_TEAM_ONLY) != 0 ? 1 : 0,
               current_vote, v2_vote_can_vote(self, vote) ? 1 : 0);
  }
}

static void v2_build_vote_actions(
  struct player *self, struct agent_v2_action_buffer *buffer)
{
  int size = voteinfo_queue_size();
  int index;

  if (size < 0 || size > FC_AGENT_V2_MAX_VOTES) {
    buffer->overflow = TRUE;
    return;
  }
  for (index = 0; index < size; index++) {
    const struct voteinfo *vote = voteinfo_queue_get(index);
    struct agent_v2_action *entry;

    if (vote == NULL) {
      buffer->overflow = TRUE;
      return;
    }
    if (!v2_vote_can_vote(self, vote)) {
      continue;
    }
    if (buffer->count >= buffer->capacity) {
      buffer->overflow = TRUE;
      return;
    }
    entry = &buffer->actions[buffer->count++];
    v2_action_init(entry);
    entry->kind = AGENT_V2_ACTION_PLAYER_CAST_VOTE;
    entry->player_id = player_number(self);
    entry->player_incarnation = v2_existing_incarnation(
      AGENT_V2_ENTITY_PLAYER, entry->player_id);
    entry->vote_no = vote->vote_no;
    entry->vote_signature = v2_vote_signature(vote);
    entry->probability_kind = AGENT_V2_PROBABILITY_EXACT;
    entry->probability_min = action_prob_new_certain().min;
    entry->probability_max = action_prob_new_certain().max;
  }
}

static void v2_build_player_actions(
  struct player *self, const struct fc_agent_v2_phase_evidence *phase,
  struct agent_v2_action_buffer *buffer)
{
  const struct research *research;

  if (self == NULL) {
    return;
  }
  v2_build_vote_actions(self, buffer);
  if (!v2_actions_ready(phase)) {
    return;
  }
  if (fc_agent_v2_phase_end_action_count(phase) == 1) {
    v2_buffer_add_action(buffer, AGENT_V2_ACTION_PHASE_END, NULL, NULL,
                         ACTION_NONE, action_prob_new_certain());
  }

  research = research_get(self);
  if (research != NULL) {
    advance_re_active_iterate(padvance) {
      Tech_type_id tech = advance_number(padvance);

      if (v2_research_can_target(research, tech)
          && tech != research->researching) {
        v2_buffer_add_player_action(
          buffer, AGENT_V2_ACTION_RESEARCH_TARGET, tech, 0);
      }
      if (v2_research_can_goal(research, tech)
          && tech != research->tech_goal) {
        v2_buffer_add_player_action(
          buffer, AGENT_V2_ACTION_RESEARCH_GOAL, tech, 0);
      }
    } advance_re_active_iterate_end;
    if (v2_research_can_target(research, A_FUTURE)) {
      if (research->researching != A_FUTURE) {
        v2_buffer_add_player_action(
          buffer, AGENT_V2_ACTION_RESEARCH_TARGET, A_FUTURE, 0);
      }
      if (v2_research_can_goal(research, A_FUTURE)
          && research->tech_goal != A_FUTURE) {
        v2_buffer_add_player_action(
          buffer, AGENT_V2_ACTION_RESEARCH_GOAL, A_FUTURE, 0);
      }
    }
    if (v2_research_can_goal(research, A_UNSET)
        && research->tech_goal != A_UNSET) {
      v2_buffer_add_player_action(
        buffer, AGENT_V2_ACTION_RESEARCH_GOAL, A_UNSET, 0);
    }
  }
  if (game.info.changable_tax) {
    v2_buffer_add_player_action(
      buffer, AGENT_V2_ACTION_ECONOMY_RATES, -1,
      v2_player_max_rate(self));
  }
}

static bool v2_communication_ready(const struct player *self)
{
  return self != NULL && client_state() == C_S_RUNNING
         && v2_seat_authorized && v2_cache_coherent()
         && can_client_control() && is_human(self) && !is_server_busy();
}

static void v2_buffer_add_communication_action(
  struct agent_v2_action_buffer *buffer,
  enum agent_v2_action_kind kind, const struct player *self)
{
  struct agent_v2_action *entry;

  if (self == NULL || buffer->count >= buffer->capacity) {
    buffer->overflow = TRUE;
    return;
  }
  entry = &buffer->actions[buffer->count++];
  v2_action_init(entry);
  entry->kind = kind;
  entry->player_id = player_number(self);
  entry->player_incarnation = v2_existing_incarnation(
    AGENT_V2_ENTITY_PLAYER, entry->player_id);
  entry->probability_kind = AGENT_V2_PROBABILITY_EXACT;
  entry->probability_min = action_prob_new_certain().min;
  entry->probability_max = action_prob_new_certain().max;
}

static void v2_build_communication_actions(
  struct player *self, struct agent_v2_action_buffer *buffer)
{
  if (!v2_communication_ready(self)) {
    return;
  }
  v2_buffer_add_communication_action(
    buffer, AGENT_V2_ACTION_PLAYER_SEND_CHAT, self);
}

static void v2_build_unit_actions(
  const struct unit *punit, struct agent_v2_action_buffer *buffer)
{
  static const action_id move_actions[] = {
    ACTION_UNIT_MOVE, ACTION_UNIT_MOVE2, ACTION_UNIT_MOVE3
  };
  static const action_id attack_actions[] = {
    ACTION_ATTACK, ACTION_ATTACK2, ACTION_SUICIDE_ATTACK,
    ACTION_SUICIDE_ATTACK2
  };
  struct tile *origin;
  struct act_prob found_probability;

  if (punit == NULL || punit->ssa_controller != SSA_NONE
      || (origin = unit_tile(punit)) == NULL) {
    return;
  }
  found_probability = action_prob_vs_tile(&wld.map, punit,
                                          ACTION_FOUND_CITY,
                                          origin, NULL);
  if (action_prob_possible(found_probability)) {
    v2_buffer_add_action(buffer, AGENT_V2_ACTION_FOUND_CITY, punit, origin,
                         ACTION_FOUND_CITY, found_probability);
  }
  if (punit->moves_left <= 0) {
    return;
  }
  adjc_iterate(&wld.map, origin, target) {
      bool target_unknown = client_tile_get_known(target) == TILE_UNKNOWN;
      size_t i;

      if (!target_unknown) {
        for (i = 0; i < ARRAY_SIZE(attack_actions); i++) {
          struct act_prob probability = action_prob_vs_stack(
            &wld.map, punit, attack_actions[i], target);

          if (fc_agent_v2_target_action_policy(
                FALSE, FALSE, action_prob_possible(probability))
              == FC_AGENT_V2_TARGET_ACTION_PRESERVE_PROBABILITY) {
            v2_buffer_add_action(buffer, AGENT_V2_ACTION_ATTACK,
                                 punit, target, attack_actions[i],
                                 probability);
          }
        }
      }
      for (i = 0; i < ARRAY_SIZE(move_actions); i++) {
        struct act_prob probability;
        enum fc_agent_v2_action_query query =
          fc_agent_v2_action_query_policy(target_unknown, TRUE);

        if (query == FC_AGENT_V2_ACTION_QUERY_ACTOR_ONLY) {
          int index = tile_index(target);

          /* Unknown targets are opaque. Actor-only ruleset feasibility plus
           * adjacency is the entire public capability test; never inspect
           * target terrain, extras, ownership, cities, units, or a
           * target-dependent action probability here. The server remains
           * authoritative when the capability is executed. */
          if (!action_maybe_possible_actor_unit(&wld.map, move_actions[i],
                                                punit)) {
            continue;
          }
          probability = action_prob_new_unknown();
          if (buffer->export_unknown_rows
              && !buffer->unknown_exported[index]) {
            char row[AGENT_V2_ROW_MAX];

            if (!fc_agent_v2_format_unknown_tile(
                  row, sizeof(row), index, TILE_XY(target))) {
              buffer->overflow = TRUE;
            } else {
              v2_add_row("%s", row);
              buffer->unknown_exported[index] = TRUE;
            }
          }
          v2_buffer_add_action(buffer, AGENT_V2_ACTION_MOVE,
                               punit, target, move_actions[i], probability);
        } else if (query == FC_AGENT_V2_ACTION_QUERY_TARGET) {
          probability = action_prob_vs_tile(
            &wld.map, punit, move_actions[i], target, NULL);
          if (action_prob_possible(probability)) {
            v2_buffer_add_action(buffer, AGENT_V2_ACTION_MOVE,
                                 punit, target, move_actions[i], probability);
          }
        }
      }
  } adjc_iterate_end;
}

/* Results selected from the normal action dialog that have a complete v2
 * semantic. Paid espionage is admitted only after a request-correlated,
 * non-punitive server quote is frozen into the opaque action row. */
static bool v2_special_result_supported(enum action_result result,
                                        enum action_target_kind target_kind)
{
  switch (result) {
  case ACTRES_SPY_BRIBE_UNIT:
  case ACTRES_SPY_BRIBE_STACK:
  case ACTRES_SPY_INCITE_CITY:
  case ACTRES_ESTABLISH_EMBASSY:
  case ACTRES_SPY_INVESTIGATE_CITY:
  case ACTRES_SPY_POISON:
  case ACTRES_SPY_STEAL_GOLD:
  case ACTRES_SPY_SABOTAGE_CITY:
  case ACTRES_SPY_TARGETED_SABOTAGE_CITY:
  case ACTRES_SPY_SABOTAGE_CITY_PRODUCTION:
  case ACTRES_SPY_STEAL_TECH:
  case ACTRES_SPY_TARGETED_STEAL_TECH:
  case ACTRES_SPY_SABOTAGE_UNIT:
  case ACTRES_CAPTURE_UNITS:
  case ACTRES_STEAL_MAPS:
  case ACTRES_BOMBARD:
  case ACTRES_SPY_NUKE:
  case ACTRES_NUKE:
  case ACTRES_NUKE_UNITS:
  case ACTRES_DESTROY_CITY:
  case ACTRES_EXPEL_UNIT:
  case ACTRES_STRIKE_PRODUCTION:
  case ACTRES_CONQUER_CITY:
  case ACTRES_HEAL_UNIT:
  case ACTRES_COLLECT_RANSOM:
  case ACTRES_SPY_SPREAD_PLAGUE:
  case ACTRES_SPY_ATTACK:
  case ACTRES_PARADROP_CONQUER:
  case ACTRES_WIPE_UNITS:
  case ACTRES_SPY_ESCAPE:
  case ACTRES_TELEPORT_CONQUER:
  case ACTRES_CONQUER_EXTRAS:
  case ACTRES_HUT_ENTER:
  case ACTRES_HUT_FRIGHTEN:
    break;
  default:
    return FALSE;
  }
  switch (result) {
  case ACTRES_SPY_INCITE_CITY:
  case ACTRES_ESTABLISH_EMBASSY:
  case ACTRES_SPY_INVESTIGATE_CITY:
  case ACTRES_SPY_POISON:
  case ACTRES_SPY_STEAL_GOLD:
  case ACTRES_SPY_SABOTAGE_CITY:
  case ACTRES_SPY_TARGETED_SABOTAGE_CITY:
  case ACTRES_SPY_SABOTAGE_CITY_PRODUCTION:
  case ACTRES_SPY_STEAL_TECH:
  case ACTRES_SPY_TARGETED_STEAL_TECH:
  case ACTRES_STEAL_MAPS:
  case ACTRES_SPY_NUKE:
  case ACTRES_DESTROY_CITY:
  case ACTRES_STRIKE_PRODUCTION:
  case ACTRES_CONQUER_CITY:
  case ACTRES_SPY_SPREAD_PLAGUE:
  case ACTRES_SPY_ESCAPE:
    return target_kind == ATK_CITY;
  case ACTRES_SPY_BRIBE_UNIT:
  case ACTRES_SPY_SABOTAGE_UNIT:
  case ACTRES_EXPEL_UNIT:
  case ACTRES_HEAL_UNIT:
    return target_kind == ATK_UNIT;
  case ACTRES_CAPTURE_UNITS:
  case ACTRES_SPY_BRIBE_STACK:
  case ACTRES_BOMBARD:
  case ACTRES_NUKE_UNITS:
  case ACTRES_COLLECT_RANSOM:
  case ACTRES_SPY_ATTACK:
  case ACTRES_WIPE_UNITS:
    return target_kind == ATK_STACK;
  case ACTRES_NUKE:
    return target_kind == ATK_CITY || target_kind == ATK_TILE;
  case ACTRES_PARADROP_CONQUER:
  case ACTRES_TELEPORT_CONQUER:
  case ACTRES_HUT_ENTER:
  case ACTRES_HUT_FRIGHTEN:
    return target_kind == ATK_TILE;
  case ACTRES_CONQUER_EXTRAS:
    return target_kind == ATK_EXTRAS;
  default:
    return FALSE;
  }
}

static bool v2_paid_special_action(const struct action *paction)
{
  if (paction == NULL) {
    return FALSE;
  }
  switch (paction->id) {
  case ACTION_SPY_BRIBE_UNIT:
    return paction->result == ACTRES_SPY_BRIBE_UNIT
           && action_get_target_kind(paction) == ATK_UNIT;
  case ACTION_SPY_BRIBE_STACK:
    return paction->result == ACTRES_SPY_BRIBE_STACK
           && action_get_target_kind(paction) == ATK_STACK;
  case ACTION_SPY_INCITE_CITY:
  case ACTION_SPY_INCITE_CITY_ESC:
    return paction->result == ACTRES_SPY_INCITE_CITY
           && action_get_target_kind(paction) == ATK_CITY;
  default:
    return FALSE;
  }
}

static bool v2_targeted_sabotage_action(const struct action *paction)
{
  return paction != NULL
         && paction->result == ACTRES_SPY_TARGETED_SABOTAGE_CITY
         && action_get_target_kind(paction) == ATK_CITY
         && action_get_sub_target_kind(paction) == ASTK_BUILDING
         && (paction->id == ACTION_SPY_TARGETED_SABOTAGE_CITY
             || paction->id == ACTION_SPY_TARGETED_SABOTAGE_CITY_ESC);
}

static bool v2_classic_nuke_action(const struct action *paction)
{
  if (paction == NULL) {
    return FALSE;
  }
  switch (paction->id) {
  case ACTION_NUKE:
  case ACTION_NUKE_CITY:
  case ACTION_NUKE_UNITS:
    return TRUE;
  default:
    return FALSE;
  }
}

static bool v2_classic_immediate_combat_action(
  const struct action *paction)
{
  if (paction == NULL || paction->result != ACTRES_ATTACK
      || action_get_target_kind(paction) != ATK_STACK) {
    return FALSE;
  }
  return paction->id == ACTION_ATTACK
         || paction->id == ACTION_SUICIDE_ATTACK;
}

static bool v2_classic_collect_ransom_action(
  const struct action *paction)
{
  return paction != NULL && paction->id == ACTION_COLLECT_RANSOM
         && paction->result == ACTRES_COLLECT_RANSOM
         && action_get_target_kind(paction) == ATK_STACK;
}

/* The normal action event carries no native action id.  Bind only the exact
 * classic city actions whose frozen capability was sent; targeted building
 * sabotage remains on its separate complex-target lane. */
static enum event_type v2_city_espionage_success_event(
  const struct action *paction)
{
  if (paction == NULL || action_get_target_kind(paction) != ATK_CITY) {
    return E_COUNT;
  }
  switch (paction->id) {
  case ACTION_SPY_POISON:
  case ACTION_SPY_POISON_ESC:
    return paction->result == ACTRES_SPY_POISON
           ? E_MY_DIPLOMAT_POISON : E_COUNT;
  case ACTION_SPY_SABOTAGE_CITY:
  case ACTION_SPY_SABOTAGE_CITY_ESC:
    return paction->result == ACTRES_SPY_SABOTAGE_CITY
           ? E_MY_DIPLOMAT_SABOTAGE : E_COUNT;
  case ACTION_SPY_SABOTAGE_CITY_PRODUCTION:
  case ACTION_SPY_SABOTAGE_CITY_PRODUCTION_ESC:
    return paction->result == ACTRES_SPY_SABOTAGE_CITY_PRODUCTION
           ? E_MY_DIPLOMAT_SABOTAGE : E_COUNT;
  default:
    return E_COUNT;
  }
}

static bool v2_paid_quote_accepted(const struct action *paction,
                                   int maximum_cost, int current_cost)
{
  const struct player *self = client_player();

  return v2_paid_special_action(paction)
         && maximum_cost >= 0 && current_cost >= 0
         && current_cost <= maximum_cost
         && self != NULL && current_cost <= self->economic.gold
         && (paction->result != ACTRES_SPY_INCITE_CITY
             || current_cost < INCITE_IMPOSSIBLE_COST);
}

static bool v2_special_action_shape_supported(const struct action *paction)
{
  enum action_sub_result allowed_subresult = ACT_SUB_RES_COUNT;
  bool targeted_tech;
  bool targeted_building;
  int subresult;

  if (paction == NULL
      || !v2_special_result_supported(
           paction->result, action_get_target_kind(paction))) {
    return FALSE;
  }
  targeted_tech = paction->result == ACTRES_SPY_TARGETED_STEAL_TECH;
  targeted_building =
    paction->result == ACTRES_SPY_TARGETED_SABOTAGE_CITY;
  if (action_id_has_complex_target(paction->id)
        != (targeted_tech || targeted_building)
      || action_get_sub_target_kind(paction)
         != (targeted_tech ? ASTK_TECH
                           : targeted_building ? ASTK_BUILDING : ASTK_NONE)) {
    return FALSE;
  }
  switch (paction->result) {
  case ACTRES_SPY_BRIBE_UNIT:
    if (paction->id != ACTION_SPY_BRIBE_UNIT) {
      return FALSE;
    }
    break;
  case ACTRES_SPY_BRIBE_STACK:
    if (paction->id != ACTION_SPY_BRIBE_STACK) {
      return FALSE;
    }
    break;
  case ACTRES_SPY_INCITE_CITY:
    if (paction->id != ACTION_SPY_INCITE_CITY
        && paction->id != ACTION_SPY_INCITE_CITY_ESC) {
      return FALSE;
    }
    break;
  case ACTRES_SPY_SABOTAGE_CITY:
    if (paction->id != ACTION_SPY_SABOTAGE_CITY
        && paction->id != ACTION_SPY_SABOTAGE_CITY_ESC) {
      return FALSE;
    }
    break;
  case ACTRES_SPY_TARGETED_SABOTAGE_CITY:
    if (paction->id != ACTION_SPY_TARGETED_SABOTAGE_CITY
        && paction->id != ACTION_SPY_TARGETED_SABOTAGE_CITY_ESC) {
      return FALSE;
    }
    break;
  case ACTRES_SPY_SABOTAGE_CITY_PRODUCTION:
    if (paction->id != ACTION_SPY_SABOTAGE_CITY_PRODUCTION_ESC) {
      return FALSE;
    }
    break;
  case ACTRES_SPY_STEAL_TECH:
    if (paction->id != ACTION_SPY_STEAL_TECH
        && paction->id != ACTION_SPY_STEAL_TECH_ESC) {
      return FALSE;
    }
    break;
  case ACTRES_SPY_TARGETED_STEAL_TECH:
    if (paction->id != ACTION_SPY_TARGETED_STEAL_TECH_ESC) {
      return FALSE;
    }
    break;
  case ACTRES_PARADROP_CONQUER:
    if (paction->id != ACTION_PARADROP_ENTER_CONQUER) {
      return FALSE;
    }
    allowed_subresult = ACT_SUB_RES_HUT_ENTER;
    break;
  case ACTRES_CONQUER_EXTRAS:
    if (paction->id != ACTION_CONQUER_EXTRAS
        && paction->id != ACTION_CONQUER_EXTRAS2) {
      return FALSE;
    }
    break;
  case ACTRES_HUT_ENTER:
    if (paction->id != ACTION_HUT_ENTER
        && paction->id != ACTION_HUT_ENTER2) {
      return FALSE;
    }
    allowed_subresult = ACT_SUB_RES_HUT_ENTER;
    break;
  case ACTRES_HUT_FRIGHTEN:
    if (paction->id != ACTION_HUT_FRIGHTEN
        && paction->id != ACTION_HUT_FRIGHTEN2) {
      return FALSE;
    }
    allowed_subresult = ACT_SUB_RES_HUT_FRIGHTEN;
    break;
  default:
    break;
  }
  for (subresult = 0; subresult < ACT_SUB_RES_COUNT; subresult++) {
    if (BV_ISSET(paction->sub_results, subresult)
        != (subresult == allowed_subresult)) {
      return FALSE;
    }
  }
  return TRUE;
}

static bool v2_special_not_implemented_allowed(const struct action *paction)
{
  if (paction == NULL || !v2_special_action_shape_supported(paction)) {
    return FALSE;
  }
  switch (paction->result) {
  case ACTRES_SPY_SABOTAGE_CITY:
  case ACTRES_SPY_TARGETED_SABOTAGE_CITY:
  case ACTRES_SPY_SABOTAGE_CITY_PRODUCTION:
  case ACTRES_SPY_STEAL_TECH:
  case ACTRES_SPY_TARGETED_STEAL_TECH:
  case ACTRES_PARADROP_CONQUER:
  case ACTRES_HUT_ENTER:
  case ACTRES_HUT_FRIGHTEN:
    return TRUE;
  default:
    return FALSE;
  }
}

static bool v2_special_probability_supported(
  const struct action *paction, struct act_prob probability)
{
  struct act_prob not_implemented = action_prob_new_not_impl();

  return v2_special_not_implemented_allowed(paction)
         == are_action_probabilitys_equal(&probability, &not_implemented);
}

static uint64_t v2_target_research_digest(const struct player *target)
{
  const struct research *research = target != NULL
                                    ? research_get(target) : NULL;
  uint64_t digest = UINT64_C(1469598103934665603);
  int player_id;
  int research_id;

  if (target == NULL || research == NULL) {
    return 0;
  }
  player_id = player_number(target);
  research_id = research_number(research);
  digest = v2_hash_bytes(digest, &player_id, sizeof(player_id));
  digest = v2_hash_bytes(digest, &research_id, sizeof(research_id));
  digest = v2_hash_bytes(
    digest, &research->future_tech, sizeof(research->future_tech));
  advance_index_iterate(A_FIRST, tech) {
    enum tech_state state = research_invention_state(research, tech);

    digest = v2_hash_bytes(digest, &tech, sizeof(tech));
    digest = v2_hash_bytes(digest, &state, sizeof(state));
  } advance_index_iterate_end;
  return digest;
}

static uint64_t v2_sabotage_catalog_digest(
  const struct packet_city_sabotage_list *packet)
{
  uint64_t digest = UINT64_C(1469598103934665603);
  int choice_count = 0;

  if (packet == NULL) {
    return 0;
  }
  digest = v2_hash_bytes(
    digest, &packet->actor_id, sizeof(packet->actor_id));
  digest = v2_hash_bytes(
    digest, &packet->city_id, sizeof(packet->city_id));
  digest = v2_hash_bytes(
    digest, &packet->act_id, sizeof(packet->act_id));
  improvement_iterate(pimprove) {
    int improvement_id = improvement_number(pimprove);

    if (BV_ISSET(packet->improvements, improvement_index(pimprove))
        && pimprove->sabotage > 0) {
      digest = v2_hash_bytes(
        digest, &improvement_id, sizeof(improvement_id));
      choice_count++;
    }
  } improvement_iterate_end;
  digest = v2_hash_bytes(digest, &choice_count, sizeof(choice_count));
  return digest != 0 ? digest : UINT64_C(1);
}

static bool v2_targeted_tech_choice_current(
  const struct player *self, const struct city *target_city,
  Tech_type_id tech, uint64_t expected_digest)
{
  const struct player *victim = target_city != NULL
                                ? city_owner(target_city) : NULL;
  const struct research *self_research = self != NULL
                                         ? research_get(self) : NULL;
  const struct research *victim_research = victim != NULL
                                           ? research_get(victim) : NULL;

  return self != NULL && victim != NULL && victim != self
         && self_research != NULL && victim_research != NULL
         && self_research != victim_research
         && can_see_techs_of_target(self, victim)
         && expected_digest != 0
         && v2_target_research_digest(victim) == expected_digest
         && valid_advance_by_number(tech) != NULL
         && research_invention_state(self_research, tech) != TECH_KNOWN
         && research_invention_state(victim_research, tech) == TECH_KNOWN
         && research_invention_gettable(
              self_research, tech, game.info.tech_steal_allow_holes);
}

static bool v2_add_server_special_action_one(
  struct agent_v2_action_buffer *buffer, const struct unit *actor,
  const struct unit *target_unit, const struct city *target_city,
  const struct tile *target_tile, const struct action *paction,
  struct act_prob probability, Tech_type_id target_tech,
  uint64_t target_research_digest)
{
  struct agent_v2_action *entry;
  struct city *lease_city = NULL;
  struct player *lease_extra_owner = NULL;

  if (paction->id == ACTION_PARADROP_ENTER_CONQUER) {
    lease_city = target_tile != NULL ? tile_city(target_tile) : NULL;
    lease_extra_owner = target_tile != NULL
                        ? extra_owner(target_tile) : NULL;
    if (target_tile == NULL || unit_tile(actor) == NULL
        || unit_owner(actor) != client_player()
        || actor->client.lifecycle_id == 0
        || actor->moves_left <= 0 || actor->paradropped
        || unit_tile(actor) == target_tile
        || client_tile_get_known(target_tile) != TILE_KNOWN_SEEN
        || (lease_city != NULL
            && (lease_city->client.lifecycle_id == 0
                || city_owner(lease_city) == client_player()))
        || (lease_extra_owner != NULL
            && lease_extra_owner == client_player())) {
      return FALSE;
    }
  }

  if (buffer->count >= buffer->capacity) {
    buffer->overflow = TRUE;
    return FALSE;
  }
  entry = &buffer->actions[buffer->count++];
  v2_action_init(entry);
  entry->kind = AGENT_V2_ACTION_UNIT_SPECIAL;
  entry->unit_id = actor->id;
  entry->unit_incarnation = v2_existing_incarnation(
    AGENT_V2_ENTITY_UNIT, actor->id);
  entry->unit_lifecycle_id = actor->client.lifecycle_id;
  entry->target_tile = target_tile != NULL ? tile_index(target_tile) : -1;
  if (paction->id == ACTION_PARADROP_ENTER_CONQUER) {
    entry->source_unit_tile = tile_index(unit_tile(actor));
    entry->source_unit_moves = actor->moves_left;
    entry->source_unit_paradropped = actor->paradropped;
    entry->special_target_known_seen = TRUE;
    entry->special_target_extras = target_tile->extras;
    entry->special_target_hut_extras = v2_hut_extras_on_tile(target_tile);
    entry->special_target_extra_owner = lease_extra_owner != NULL
                                        ? player_number(lease_extra_owner) : -1;
    if (lease_city != NULL) {
      entry->special_target_city_id = lease_city->id;
      entry->special_target_city_incarnation = v2_existing_incarnation(
        AGENT_V2_ENTITY_CITY, lease_city->id);
      entry->special_target_city_lifecycle_id =
        lease_city->client.lifecycle_id;
      entry->special_target_city_owner = player_number(
        city_owner(lease_city));
    }
  }
  if (action_get_target_kind(paction) == ATK_UNIT
      && target_unit != NULL && target_unit->client.lifecycle_id != 0) {
    entry->target_unit_id = target_unit->id;
    entry->target_unit_incarnation = v2_existing_incarnation(
      AGENT_V2_ENTITY_UNIT, target_unit->id);
    entry->target_unit_lifecycle_id = target_unit->client.lifecycle_id;
  }
  if (action_get_target_kind(paction) == ATK_CITY
      && target_city != NULL && target_city->client.lifecycle_id != 0) {
    entry->destination_city_id = target_city->id;
    entry->destination_city_incarnation = v2_existing_incarnation(
      AGENT_V2_ENTITY_CITY, target_city->id);
    entry->destination_city_lifecycle_id = target_city->client.lifecycle_id;
    entry->destination_city_tile = tile_index(city_tile(target_city));
  }
  entry->target_tech = target_tech;
  entry->target_research_digest = target_research_digest;
  if (paction->id == ACTION_NUKE_UNITS) {
    entry->target_stack_signature = v2_visible_stack_signature(
      unit_owner(actor), entry->target_tile);
  } else if (paction->id == ACTION_SPY_BRIBE_STACK) {
    entry->target_stack_signature = v2_visible_bribe_stack_signature(
      unit_owner(actor), entry->target_tile);
  }
  entry->target_extra = EXTRA_NONE;
  entry->action = paction->id;
  if (!v2_normalize_probability(probability, &entry->probability_kind,
                                &entry->probability_min,
                                &entry->probability_max)) {
    buffer->count--;
    buffer->overflow = TRUE;
    return FALSE;
  }
  return TRUE;
}

static bool v2_add_server_special_action(
  struct agent_v2_action_buffer *buffer, const struct unit *actor,
  const struct unit *target_unit, const struct city *target_city,
  const struct tile *target_tile, const struct action *paction,
  struct act_prob probability)
{
  size_t before_count = buffer->count;
  enum action_sub_target_kind subtarget;

  if (buffer->count >= buffer->capacity || actor == NULL
      || actor->client.lifecycle_id == 0 || paction == NULL
      || !v2_special_action_shape_supported(paction)
      || !v2_special_probability_supported(paction, probability)
      || !action_prob_possible(probability)) {
    if (buffer->count >= buffer->capacity) {
      buffer->overflow = TRUE;
    }
    return FALSE;
  }
  subtarget = action_get_sub_target_kind(paction);
  if (subtarget != ASTK_NONE && subtarget != ASTK_TECH
      && subtarget != ASTK_BUILDING) {
    return FALSE;
  }
  switch (action_get_target_kind(paction)) {
  case ATK_CITY:
    if (target_city == NULL || target_city->client.lifecycle_id == 0) {
      return FALSE;
    }
    break;
  case ATK_UNIT:
    if (target_unit == NULL || target_unit->client.lifecycle_id == 0) {
      return FALSE;
    }
    break;
  case ATK_STACK:
  case ATK_TILE:
  case ATK_EXTRAS:
    if (target_tile == NULL) {
      return FALSE;
    }
    break;
  case ATK_SELF:
    if (target_tile == NULL || unit_tile(actor) != target_tile) {
      return FALSE;
    }
    break;
  case ATK_COUNT:
    return FALSE;
  }
  if (paction->id == ACTION_SPY_BRIBE_STACK
      && !v2_visible_bribe_stack_bounded(unit_owner(actor), target_tile)) {
    return FALSE;
  }
  if (paction->result == ACTRES_SPY_TARGETED_STEAL_TECH) {
    const struct player *self = unit_owner(actor);
    const struct player *victim = target_city != NULL
                                  ? city_owner(target_city) : NULL;
    uint64_t digest;

    if (subtarget != ASTK_TECH || self == NULL || victim == NULL
        || !can_see_techs_of_target(self, victim)) {
      return FALSE;
    }
    digest = v2_target_research_digest(victim);
    advance_index_iterate(A_FIRST, tech) {
      if (v2_targeted_tech_choice_current(
            self, target_city, tech, digest)
          && !v2_add_server_special_action_one(
               buffer, actor, target_unit, target_city, target_tile,
               paction, probability, tech, digest)) {
        break;
      }
    } advance_index_iterate_end;
    return buffer->count > before_count;
  }
  if (paction->result == ACTRES_SPY_TARGETED_SABOTAGE_CITY) {
    if (subtarget != ASTK_BUILDING || target_city == NULL) {
      return FALSE;
    }
    /* The authoritative normal-GUI building list arrives separately. Keep
     * this private placeholder only until its exact request-correlated reply
     * expands it into one frozen action per selectable improvement. */
    return v2_add_server_special_action_one(
      buffer, actor, target_unit, target_city, target_tile, paction,
      probability, -1, 0);
  }
  if (subtarget != ASTK_NONE) {
    return FALSE;
  }
  return v2_add_server_special_action_one(
    buffer, actor, target_unit, target_city, target_tile, paction,
    probability, -1, 0);
}

static void v2_build_noncombat_mobility_actions(
  const struct unit *punit, struct agent_v2_action_buffer *buffer)
{
  static const action_id paradrop_actions[] = {
    ACTION_PARADROP, ACTION_PARADROP_FRIGHTEN, ACTION_PARADROP_ENTER
  };
  static const action_id teleport_actions[] = {
    ACTION_TELEPORT, ACTION_TELEPORT2, ACTION_TELEPORT3,
    ACTION_TELEPORT_FRIGHTEN, ACTION_TELEPORT_ENTER
  };
  struct player *self = client_player();
  struct tile *origin;
  struct city *source;
  struct act_prob not_implemented = action_prob_new_not_impl();
  bool paradrop_maybe[ARRAY_SIZE(paradrop_actions)];
  bool teleport_maybe[ARRAY_SIZE(teleport_actions)];
  bool any_tile_mobility = FALSE;
  size_t i;

  if (self == NULL || punit == NULL
      || punit->ssa_controller != SSA_NONE
      || (origin = unit_tile(punit)) == NULL) {
    return;
  }

  /* This first slice intentionally exposes only owned-to-owned airlift.
   * Allied city sites need their own fog-safe public lifetime model. */
  source = tile_city(origin);
  if (source != NULL && city_owner(source) == self
      && action_maybe_possible_actor_unit(
           &wld.map, ACTION_AIRLIFT, punit)) {
    city_list_iterate(self->cities, destination) {
      struct act_prob probability;

      if (destination == source) {
        continue;
      }
      probability = action_prob_vs_city(
        &wld.map, punit, ACTION_AIRLIFT, destination);
      if (action_prob_possible(probability)
          && !are_action_probabilitys_equal(
               &probability, &not_implemented)) {
        v2_buffer_add_relocation_action(
          buffer, AGENT_V2_ACTION_UNIT_AIRLIFT, punit, NULL,
          source, destination, ACTION_AIRLIFT, probability);
        if (buffer->overflow) {
          break;
        }
      }
    } city_list_iterate_end;
  }

  for (i = 0; i < ARRAY_SIZE(paradrop_actions); i++) {
    paradrop_maybe[i] = action_maybe_possible_actor_unit(
      &wld.map, paradrop_actions[i], punit);
    any_tile_mobility = any_tile_mobility || paradrop_maybe[i];
  }
  for (i = 0; i < ARRAY_SIZE(teleport_actions); i++) {
    teleport_maybe[i] = action_maybe_possible_actor_unit(
      &wld.map, teleport_actions[i], punit);
    any_tile_mobility = any_tile_mobility || teleport_maybe[i];
  }
  if (!any_tile_mobility) {
    return;
  }

  whole_map_iterate(&wld.map, target) {
    if (buffer->overflow) {
      break;
    }
    if (target == origin || client_tile_get_known(target) != TILE_KNOWN_SEEN) {
      continue;
    }
    for (i = 0; i < ARRAY_SIZE(paradrop_actions); i++) {
      if (buffer->overflow) {
        break;
      }
      if (!paradrop_maybe[i]) {
        continue;
      }
      struct act_prob probability = action_prob_vs_tile(
        &wld.map, punit, paradrop_actions[i], target, NULL);

      /* NOT_IMPLEMENTED still means potentially legal. The server remains
       * authoritative and the public descriptor preserves that uncertainty. */
      if (action_prob_possible(probability)) {
        v2_buffer_add_relocation_action(
          buffer, AGENT_V2_ACTION_UNIT_PARADROP, punit, target,
          NULL, NULL, paradrop_actions[i], probability);
      }
    }
    for (i = 0; i < ARRAY_SIZE(teleport_actions); i++) {
      if (buffer->overflow) {
        break;
      }
      if (!teleport_maybe[i]) {
        continue;
      }
      struct act_prob probability = action_prob_vs_tile(
        &wld.map, punit, teleport_actions[i], target, NULL);

      if (action_prob_possible(probability)
          && !are_action_probabilitys_equal(
               &probability, &not_implemented)) {
        v2_buffer_add_relocation_action(
          buffer, AGENT_V2_ACTION_UNIT_TELEPORT, punit, target,
          NULL, NULL, teleport_actions[i], probability);
      }
    }
  } whole_map_iterate_end;
}

static void v2_build_unit_target_paradrop_actions(
  const struct unit *punit, const struct tile *target,
  struct agent_v2_action_buffer *buffer)
{
  static const action_id paradrop_actions[] = {
    ACTION_PARADROP, ACTION_PARADROP_FRIGHTEN, ACTION_PARADROP_ENTER
  };
  struct player *self = client_player();
  enum known_type known;
  size_t i;

  if (self == NULL || punit == NULL || unit_owner(punit) != self
      || punit->client.lifecycle_id == 0
      || punit->ssa_controller != SSA_NONE
      || unit_tile(punit) == NULL || target == NULL
      || target == unit_tile(punit)
      || (known = client_tile_get_known(target)) == TILE_UNKNOWN) {
    return;
  }
  for (i = 0; i < ARRAY_SIZE(paradrop_actions); i++) {
    const struct action *paction = action_by_number(paradrop_actions[i]);
    struct act_prob probability;

    if (paction == NULL
        || !action_maybe_possible_actor_unit(
             &wld.map, paradrop_actions[i], punit)) {
      continue;
    }
    probability = action_prob_unit_vs_tgt(
      &wld.map, paction, punit, tile_city(target), NULL, target, NULL);
    if (fc_agent_v2_target_action_policy(
          FALSE, FALSE, action_prob_possible(probability))
        == FC_AGENT_V2_TARGET_ACTION_PRESERVE_PROBABILITY) {
      v2_buffer_add_relocation_action(
        buffer, AGENT_V2_ACTION_UNIT_PARADROP, punit, target,
        NULL, NULL, paradrop_actions[i], probability);
    }
  }

  /* Visible targets use the request-correlated server discovery lane below.
   * For remembered targets, reconstruct the normal client's cached
   * paradrop-conquer variants locally without asking the server about fog. */
  if (known != TILE_KNOWN_UNSEEN) {
    return;
  }
  action_iterate(act_id) {
    const struct action *paction = action_by_number(act_id);
    struct act_prob probability;

    if (paction == NULL || paction->result != ACTRES_PARADROP_CONQUER
        || !action_maybe_possible_actor_unit(&wld.map, act_id, punit)) {
      continue;
    }
    probability = action_prob_unit_vs_tgt(
      &wld.map, paction, punit, tile_city(target), NULL, target, NULL);
    (void) v2_add_server_special_action(
      buffer, punit, NULL, tile_city(target), target, paction, probability);
  } action_iterate_end;
}

static bool v2_noncombat_mobility_action_allowed(
  enum agent_v2_action_kind kind, action_id action)
{
  switch (kind) {
  case AGENT_V2_ACTION_UNIT_AIRLIFT:
    return action == ACTION_AIRLIFT;
  case AGENT_V2_ACTION_UNIT_PARADROP:
    return action == ACTION_PARADROP
           || action == ACTION_PARADROP_FRIGHTEN
           || action == ACTION_PARADROP_ENTER;
  case AGENT_V2_ACTION_UNIT_TELEPORT:
    return action == ACTION_TELEPORT || action == ACTION_TELEPORT2
           || action == ACTION_TELEPORT3
           || action == ACTION_TELEPORT_FRIGHTEN
           || action == ACTION_TELEPORT_ENTER;
  default:
    return FALSE;
  }
}

static void v2_build_city_actions(
  const struct city *pcity, struct agent_v2_action_buffer *buffer)
{
  struct player *self = client_player();

  if (pcity == NULL || self == NULL || city_owner(pcity) != self) {
    return;
  }
  if (city_can_change_build(pcity)) {
    improvement_iterate(pimprove) {
      struct universal target = {
        .kind = VUT_IMPROVEMENT,
        .value = { .building = pimprove }
      };

      if (!are_universals_equal(&pcity->production, &target)
          && can_city_build_now(&wld.map, pcity, &target, RPT_CERTAIN)) {
        v2_buffer_add_city_action(
          buffer, AGENT_V2_ACTION_CITY_PRODUCTION, pcity, &target);
      }
    } improvement_iterate_end;
    unit_type_iterate(putype) {
      struct universal target = {
        .kind = VUT_UTYPE,
        .value = { .utype = putype }
      };

      if (!are_universals_equal(&pcity->production, &target)
          && can_city_build_now(&wld.map, pcity, &target, RPT_CERTAIN)) {
        v2_buffer_add_city_action(
          buffer, AGENT_V2_ACTION_CITY_PRODUCTION, pcity, &target);
      }
    } unit_type_iterate_end;
  }
  if (city_can_buy(pcity)
      && pcity->client.buy_cost > 0
      && pcity->client.buy_cost <= self->economic.gold) {
    v2_buffer_add_city_action(
      buffer, AGENT_V2_ACTION_CITY_BUY, pcity, &pcity->production);
  }
}

static struct act_prob v2_worker_probability(
  const struct unit *punit, const struct action *paction,
  const struct extra_type *target)
{
  switch (action_get_target_kind(paction)) {
  case ATK_TILE:
    return action_prob_vs_tile(
      &wld.map, punit, paction->id, unit_tile(punit), target);
  case ATK_EXTRAS:
    return action_prob_vs_extras(
      &wld.map, punit, paction->id, unit_tile(punit), target);
  default:
    return ACTPROB_IMPOSSIBLE;
  }
}

static void v2_build_worker_actions(
  const struct unit *punit, struct agent_v2_action_buffer *buffer)
{
  static const enum unit_activity worker_activities[] = {
    ACTIVITY_CULTIVATE, ACTIVITY_MINE, ACTIVITY_IRRIGATE,
    ACTIVITY_PILLAGE, ACTIVITY_TRANSFORM, ACTIVITY_CLEAN,
    ACTIVITY_BASE, ACTIVITY_GEN_ROAD, ACTIVITY_PLANT
  };
  size_t i;

  if (punit == NULL || punit->ssa_controller != SSA_NONE
      || unit_tile(punit) == NULL) {
    return;
  }
  for (i = 0; i < ARRAY_SIZE(worker_activities); i++) {
    enum unit_activity activity = worker_activities[i];

    /* The current client cache cannot prove a same-activity switch to a
     * different extra target reliably. Require an explicit cancel first. */
    if (punit->activity == activity) {
      continue;
    }
    action_by_activity_iterate(paction, activity) {
      if (action_get_actor_kind(paction) != AAK_UNIT
          || paction->actor_consuming_always
          || (action_get_target_kind(paction) != ATK_TILE
              && action_get_target_kind(paction) != ATK_EXTRAS)) {
        continue;
      }
      if (activity_requires_target(activity)) {
        extra_type_iterate(target) {
          struct act_prob probability = v2_worker_probability(
            punit, paction, target);

          if (action_prob_possible(probability)) {
            v2_buffer_add_worker_action(
              buffer, AGENT_V2_ACTION_WORKER_START,
              punit, activity, target, paction->id, probability);
          }
        } extra_type_iterate_end;
      } else {
        struct act_prob probability = v2_worker_probability(
          punit, paction, NULL);

        if (action_prob_possible(probability)) {
          v2_buffer_add_worker_action(
            buffer, AGENT_V2_ACTION_WORKER_START,
            punit, activity, NULL, paction->id, probability);
        }
      }
    } action_by_activity_iterate_end;
  }
  if (punit->ssa_controller == SSA_NONE
      && punit->activity != ACTIVITY_IDLE
      && can_unit_do_activity_client(punit, ACTIVITY_IDLE)) {
    v2_buffer_add_worker_action(
      buffer, AGENT_V2_ACTION_CANCEL_ACTIVITY,
      punit, ACTIVITY_IDLE, NULL, ACTION_NONE,
      action_prob_new_certain());
  }
}

static void v2_buffer_add_unit_automation_action(
  struct agent_v2_action_buffer *buffer,
  enum agent_v2_action_kind kind, const struct unit *punit)
{
  struct agent_v2_action *entry;

  if (punit == NULL || punit->client.lifecycle_id == 0
      || buffer->count >= buffer->capacity) {
    buffer->overflow = TRUE;
    return;
  }
  entry = &buffer->actions[buffer->count++];
  v2_action_init(entry);
  entry->kind = kind;
  entry->unit_id = punit->id;
  entry->unit_incarnation = v2_existing_incarnation(
    AGENT_V2_ENTITY_UNIT, punit->id);
  entry->unit_lifecycle_id = punit->client.lifecycle_id;
  entry->probability_kind = AGENT_V2_PROBABILITY_EXACT;
  entry->probability_min = action_prob_new_certain().min;
  entry->probability_max = action_prob_new_certain().max;
}

static bool v2_unit_automation_start_clean(const struct unit *punit)
{
  return punit != NULL && unit_tile(punit) != NULL
         && punit->ssa_controller == SSA_NONE
         && punit->activity == ACTIVITY_IDLE
         && punit->activity_target == NULL
         && !punit->has_orders && punit->goto_tile == NULL;
}

static void v2_build_unit_automation_actions(
  const struct unit *punit, struct agent_v2_action_buffer *buffer)
{
  struct player *self = client_player();

  if (punit == NULL || self == NULL || unit_owner(punit) != self
      || punit->client.lifecycle_id == 0) {
    return;
  }
  if (punit->ssa_controller == SSA_AUTOWORKER
      || punit->ssa_controller == SSA_AUTOEXPLORE) {
    v2_buffer_add_unit_automation_action(
      buffer, AGENT_V2_ACTION_UNIT_CANCEL_AUTOMATION, punit);
    return;
  }
  if (!v2_unit_automation_start_clean(punit)) {
    return;
  }
  if (can_unit_do_autoworker(punit)) {
    v2_buffer_add_unit_automation_action(
      buffer, AGENT_V2_ACTION_UNIT_AUTO_WORK, punit);
  }
  if (can_unit_do_activity(
        &wld.map, punit, ACTIVITY_EXPLORE,
        activity_default_action(ACTIVITY_EXPLORE))) {
    v2_buffer_add_unit_automation_action(
      buffer, AGENT_V2_ACTION_UNIT_AUTO_EXPLORE, punit);
  }
}

static bool v2_unit_cancel_orders_available(const struct unit *punit)
{
  return punit != NULL && unit_tile(punit) != NULL
         && punit->ssa_controller == SSA_NONE
         && punit->activity == ACTIVITY_IDLE
         && punit->activity_target == NULL
         && punit->has_orders;
}

static void v2_build_unit_cancel_orders_action(
  const struct unit *punit, struct agent_v2_action_buffer *buffer)
{
  struct player *self = client_player();

  if (punit == NULL || self == NULL || unit_owner(punit) != self
      || punit->client.lifecycle_id == 0
      || !v2_unit_cancel_orders_available(punit)) {
    return;
  }
  v2_buffer_add_unit_automation_action(
    buffer, AGENT_V2_ACTION_UNIT_CANCEL_ORDERS, punit);
}

static bool v2_unit_goto_actor_clean(const struct unit *punit)
{
  return v2_unit_automation_start_clean(punit)
         && !unit_transported(punit)
         && unit_list_size(unit_transport_cargo(punit)) == 0;
}

struct agent_v2_goto_candidate {
  int distance;
  int tile_id;
};

static int v2_goto_candidate_compare(const void *left, const void *right)
{
  const struct agent_v2_goto_candidate *a = left;
  const struct agent_v2_goto_candidate *b = right;

  if (fc_agent_v2_goto_candidate_precedes(
        a->distance, a->tile_id, b->distance, b->tile_id)) {
    return -1;
  }
  if (fc_agent_v2_goto_candidate_precedes(
        b->distance, b->tile_id, a->distance, a->tile_id)) {
    return 1;
  }
  return 0;
}

static void v2_buffer_add_unit_goto_action(
  struct agent_v2_action_buffer *buffer, const struct unit *punit,
  const struct tile *target, const struct client_goto_path_info *path)
{
  struct agent_v2_action *entry;

  if (punit == NULL || unit_tile(punit) == NULL || target == NULL
      || path == NULL || punit->client.lifecycle_id == 0
      || buffer->count >= buffer->capacity) {
    buffer->overflow = TRUE;
    return;
  }
  entry = &buffer->actions[buffer->count++];
  v2_action_init(entry);
  entry->kind = AGENT_V2_ACTION_UNIT_GOTO;
  entry->unit_id = punit->id;
  entry->unit_incarnation = v2_existing_incarnation(
    AGENT_V2_ENTITY_UNIT, punit->id);
  entry->unit_lifecycle_id = punit->client.lifecycle_id;
  entry->source_unit_tile = tile_index(unit_tile(punit));
  entry->target_tile = tile_index(target);
  entry->goto_destination_tile = path->destination_tile;
  entry->goto_order_count = path->order_count;
  entry->goto_orders_digest = path->orders_digest;
  entry->goto_route_signature = path->route_signature;
  entry->goto_action_move = path->action_move;
  entry->probability_kind = AGENT_V2_PROBABILITY_EXACT;
  entry->probability_min = action_prob_new_certain().min;
  entry->probability_max = action_prob_new_certain().max;
}

static bool v2_build_unit_goto_target_action(
  const struct unit *punit, const struct tile *target,
  struct client_goto_pathfinder *finder,
  struct agent_v2_action *action)
{
  struct player *self = client_player();
  struct client_goto_path_info path = {0};
  struct agent_v2_action_buffer buffer = {
    .actions = action,
    .count = 0,
    .capacity = 1,
    .overflow = FALSE,
    .export_unknown_rows = FALSE,
    .unknown_exported = NULL
  };

  if (punit == NULL || self == NULL || unit_owner(punit) != self
      || punit->client.lifecycle_id == 0
      || !v2_unit_goto_actor_clean(punit)
      || unit_tile(punit) == NULL || target == NULL
      || target == unit_tile(punit)
      || finder == NULL
      || !client_goto_pathfinder_destination(finder, target, &path)
      || path.order_count < 1
      || path.order_count >= MAX_LEN_ROUTE) {
    return FALSE;
  }
  v2_buffer_add_unit_goto_action(&buffer, punit, target, &path);
  return buffer.count == 1 && !buffer.overflow;
}

static bool v2_goto_and_perform_shape_supported(
  const struct action *paction)
{
  enum action_target_kind target_kind;

  if (paction == NULL || !v2_special_action_shape_supported(paction)
      || paction->actor_consuming_always || v2_paid_special_action(paction)
      || paction->result == ACTRES_SPY_NUKE
      || paction->result == ACTRES_NUKE
      || paction->result == ACTRES_NUKE_UNITS
      || action_get_sub_target_kind(paction) != ASTK_NONE) {
    return FALSE;
  }
  target_kind = action_get_target_kind(paction);
  /* Queued unit-target orders carry only a tile. The server later selects
   * the first generally actionable unit there, which cannot prove the exact
   * lifecycle frozen by v2. */
  return target_kind == ATK_CITY || target_kind == ATK_STACK
         || target_kind == ATK_TILE;
}

/* Check the same actor and target requirements the final native order will
 * face, but with the actor on the route's exact stopping tile.  The normal
 * target query probability is evaluated at the source and therefore cannot
 * establish whether an out-of-range action will be permitted after moving. */
static bool v2_goto_and_perform_possible_at_plan(
  const struct unit *actor, const struct unit *target_unit,
  const struct city *target_city, const struct tile *target_tile,
  const struct action *paction,
  const struct client_unit_route_plan *plan)
{
  const struct client_unit_route_plan_info *info =
    client_unit_route_plan_get_info(plan);
  struct tile *actor_tile;
  struct act_prob probability = action_prob_new_impossible();

  if (actor == NULL || target_tile == NULL || paction == NULL
      || info == NULL || map_is_empty()
      || info->destination_tile < 0
      || info->destination_tile >= map_num_tiles()
      || (actor_tile = index_to_tile(
            &wld.map, info->destination_tile)) == NULL) {
    return FALSE;
  }
  switch (action_get_target_kind(paction)) {
  case ATK_CITY:
    if (target_city == NULL || city_tile(target_city) != target_tile) {
      return FALSE;
    }
    probability = action_speculate_unit_on_city(
      &wld.map, paction->id, actor, unit_home(actor), actor_tile,
      FALSE, target_city);
    break;
  case ATK_UNIT:
    if (target_unit == NULL || unit_tile(target_unit) != target_tile) {
      return FALSE;
    }
    probability = action_speculate_unit_on_unit(
      &wld.map, paction->id, actor, unit_home(actor), actor_tile,
      FALSE, target_unit);
    break;
  case ATK_STACK:
    probability = action_speculate_unit_on_stack(
      &wld.map, paction->id, actor, unit_home(actor), actor_tile,
      FALSE, target_tile);
    break;
  case ATK_TILE:
    probability = action_speculate_unit_on_tile(
      &wld.map, paction->id, actor, unit_home(actor), actor_tile,
      FALSE, target_tile, NULL);
    break;
  case ATK_SELF:
  case ATK_EXTRAS:
  case ATK_COUNT:
    return FALSE;
  }
  return action_prob_possible(probability);
}

static bool v2_add_goto_and_perform_action(
  struct agent_v2_action_buffer *buffer, const struct unit *actor,
  const struct unit *target_unit, const struct city *target_city,
  const struct tile *target_tile, const struct action *paction)
{
  struct client_unit_route_plan *plan;
  const struct client_unit_route_plan_info *info;
  struct agent_v2_action *entry;
  struct act_prob unknown = action_prob_new_unknown();

  if (buffer->count >= buffer->capacity || actor == NULL
      || actor->client.lifecycle_id == 0 || target_tile == NULL
      || unit_tile(actor) == NULL || target_tile == unit_tile(actor)
      || !v2_unit_goto_actor_clean(actor)
      || !v2_goto_and_perform_shape_supported(paction)
      || (action_get_target_kind(paction) == ATK_UNIT
          && (target_unit == NULL
              || target_unit->client.lifecycle_id == 0))
      || (action_get_target_kind(paction) == ATK_CITY
          && (target_city == NULL
              || target_city->client.lifecycle_id == 0))) {
    if (buffer->count >= buffer->capacity) {
      buffer->overflow = TRUE;
    }
    return FALSE;
  }
  plan = client_unit_action_route_plan_new(
    (struct unit *) actor, (struct tile *) target_tile,
    paction->id, NO_TARGET);
  info = client_unit_route_plan_get_info(plan);
  if (info == NULL || info->source_tile != tile_index(unit_tile(actor))
      || info->target_tile != tile_index(target_tile)
      || info->order_count < 1 || info->order_count >= MAX_LEN_ROUTE
      || info->orders_digest == 0 || info->final_action != paction->id
      || info->final_subtarget != NO_TARGET
      || !v2_goto_and_perform_possible_at_plan(
           actor, target_unit, target_city, target_tile, paction, plan)) {
    client_unit_route_plan_destroy(plan);
    return FALSE;
  }

  entry = &buffer->actions[buffer->count++];
  v2_action_init(entry);
  entry->kind = AGENT_V2_ACTION_UNIT_GOTO_AND_PERFORM;
  entry->unit_id = actor->id;
  entry->unit_incarnation = v2_existing_incarnation(
    AGENT_V2_ENTITY_UNIT, actor->id);
  entry->unit_lifecycle_id = actor->client.lifecycle_id;
  entry->source_unit_tile = info->source_tile;
  entry->target_tile = info->target_tile;
  entry->goto_destination_tile = info->destination_tile;
  entry->goto_order_count = info->order_count;
  entry->goto_orders_digest = info->orders_digest;
  entry->goto_route_signature = info->route_signature;
  entry->goto_action_move = FALSE;
  if (action_get_target_kind(paction) == ATK_UNIT) {
    entry->target_unit_id = target_unit->id;
    entry->target_unit_incarnation = v2_existing_incarnation(
      AGENT_V2_ENTITY_UNIT, target_unit->id);
    entry->target_unit_lifecycle_id = target_unit->client.lifecycle_id;
  } else if (action_get_target_kind(paction) == ATK_CITY) {
    entry->destination_city_id = target_city->id;
    entry->destination_city_incarnation = v2_existing_incarnation(
      AGENT_V2_ENTITY_CITY, target_city->id);
    entry->destination_city_lifecycle_id = target_city->client.lifecycle_id;
    entry->destination_city_tile = tile_index(city_tile(target_city));
  } else if (action_get_target_kind(paction) == ATK_STACK) {
    entry->target_stack_signature = v2_visible_stack_signature(
      unit_owner(actor), info->target_tile);
  }
  entry->action = paction->id;
  if (!v2_normalize_probability(unknown, &entry->probability_kind,
                                &entry->probability_min,
                                &entry->probability_max)) {
    buffer->count--;
    buffer->overflow = TRUE;
    client_unit_route_plan_destroy(plan);
    return FALSE;
  }
  client_unit_route_plan_destroy(plan);
  return TRUE;
}

static bool v2_add_connect_route_action(
  struct agent_v2_action_buffer *buffer, const struct unit *actor,
  const struct tile *target_tile, enum unit_activity activity,
  struct extra_type *extra)
{
  struct client_unit_route_plan *plan;
  const struct client_unit_route_plan_info *info;
  struct agent_v2_action *entry;

  if (buffer->count >= buffer->capacity || actor == NULL
      || actor->client.lifecycle_id == 0 || target_tile == NULL
      || extra == NULL || !v2_unit_goto_actor_clean(actor)
      || !can_unit_do_connect((struct unit *) actor, activity, extra)) {
    if (buffer->count >= buffer->capacity) {
      buffer->overflow = TRUE;
    }
    return FALSE;
  }
  plan = client_unit_connect_plan_new(
    (struct unit *) actor, (struct tile *) target_tile, activity, extra);
  info = client_unit_route_plan_get_info(plan);
  if (info == NULL || info->source_tile != tile_index(unit_tile(actor))
      || info->destination_tile != tile_index(target_tile)
      || info->target_tile != tile_index(target_tile)
      || info->order_count < 1 || info->order_count > MAX_LEN_ROUTE
      || info->orders_digest == 0
      || info->final_action != activity_default_action(activity)
      || info->final_subtarget != extra_index(extra)) {
    client_unit_route_plan_destroy(plan);
    return FALSE;
  }

  entry = &buffer->actions[buffer->count++];
  v2_action_init(entry);
  entry->kind = AGENT_V2_ACTION_UNIT_CONNECT_ROUTE;
  entry->unit_id = actor->id;
  entry->unit_incarnation = v2_existing_incarnation(
    AGENT_V2_ENTITY_UNIT, actor->id);
  entry->unit_lifecycle_id = actor->client.lifecycle_id;
  entry->source_unit_tile = info->source_tile;
  entry->target_tile = info->target_tile;
  entry->goto_destination_tile = info->destination_tile;
  entry->goto_order_count = info->order_count;
  entry->goto_orders_digest = info->orders_digest;
  entry->goto_route_signature = info->route_signature;
  entry->target_activity = activity;
  entry->target_extra = extra_index(extra);
  entry->action = activity_default_action(activity);
  entry->probability_kind = AGENT_V2_PROBABILITY_EXACT;
  entry->probability_min = action_prob_new_certain().min;
  entry->probability_max = action_prob_new_certain().max;
  client_unit_route_plan_destroy(plan);
  return TRUE;
}

static bool v2_build_player_infrastructure_target_action(
  const struct player *pplayer, const struct tile *target,
  struct agent_v2_action *action)
{
  size_t used = 0;

  if (pplayer == NULL || pplayer != client_player() || target == NULL
      || !terrain_control.infrapoints
      || client_tile_get_known(target) != TILE_KNOWN_SEEN
      || target->placing != NULL
      || extra_count() > FC_AGENT_V2_MAX_INFRA_CHOICES) {
    return FALSE;
  }
  v2_action_init(action);
  action->kind = AGENT_V2_ACTION_PLAYER_PLACE_INFRA;
  action->player_id = player_number(pplayer);
  action->player_incarnation = v2_existing_incarnation(
    AGENT_V2_ENTITY_PLAYER, action->player_id);
  action->target_tile = tile_index(target);
  action->infrastructure_choices[0] = '\0';
  extra_type_iterate(pextra) {
    int written;

    if (pextra->infracost <= 0
        || pplayer->economic.infra_points < pextra->infracost
        || !player_can_place_extra(pextra, pplayer, target)) {
      continue;
    }
    written = fc_snprintf(
      action->infrastructure_choices + used,
      sizeof(action->infrastructure_choices) - used,
      "%s%d", action->infrastructure_choice_count > 0 ? "," : "",
      extra_number(pextra));
    if (written < 0
        || (size_t) written
           >= sizeof(action->infrastructure_choices) - used) {
      return FALSE;
    }
    used += (size_t) written;
    action->infrastructure_choice_count++;
  } extra_type_iterate_end;
  if (action->infrastructure_choice_count < 1) {
    return FALSE;
  }
  action->probability_kind = AGENT_V2_PROBABILITY_EXACT;
  action->probability_min = action_prob_new_certain().min;
  action->probability_max = action_prob_new_certain().max;
  return TRUE;
}

static void v2_build_unit_goto_actions(
  const struct unit *punit, struct agent_v2_action_buffer *buffer)
{
  struct player *self = client_player();
  struct client_goto_pathfinder *finder;
  const struct tile *origin;
  struct agent_v2_goto_candidate
    candidates[AGENT_V2_GOTO_MAX_NEIGHBORHOOD];
  size_t candidate_count = 0;
  size_t added = 0;
  size_t i;

  if (punit == NULL || self == NULL || unit_owner(punit) != self
      || punit->client.lifecycle_id == 0
      || !v2_unit_goto_actor_clean(punit)
      || (origin = unit_tile(punit)) == NULL) {
    return;
  }
  iterate_outward(&wld.map, origin, AGENT_V2_GOTO_MAX_DISTANCE, target) {
    int distance;

    if (target == origin || client_tile_get_known(target) == TILE_UNKNOWN) {
      continue;
    }
    distance = real_map_distance(origin, target);
    if (distance < 1 || distance > AGENT_V2_GOTO_MAX_DISTANCE
        || candidate_count >= ARRAY_SIZE(candidates)) {
      if (candidate_count >= ARRAY_SIZE(candidates)) {
        buffer->overflow = TRUE;
        break;
      }
      continue;
    }
    candidates[candidate_count].distance = distance;
    candidates[candidate_count].tile_id = tile_index(target);
    candidate_count++;
  } iterate_outward_end;
  if (buffer->overflow) {
    return;
  }
  qsort(candidates, candidate_count, sizeof(candidates[0]),
        v2_goto_candidate_compare);
  finder = client_goto_pathfinder_new(punit);
  if (finder == NULL) {
    buffer->overflow = TRUE;
    return;
  }
  for (i = 0;
       i < candidate_count && added < AGENT_V2_GOTO_MAX_DESTINATIONS;
       i++) {
    struct tile *target = index_to_tile(
      &wld.map, candidates[i].tile_id);
    struct agent_v2_action action;

    if (!v2_build_unit_goto_target_action(
          punit, target, finder, &action)) {
      continue;
    }
    if (buffer->count >= buffer->capacity) {
      buffer->overflow = TRUE;
      break;
    }
    buffer->actions[buffer->count++] = action;
    if (buffer->overflow) {
      break;
    }
    added++;
  }
  client_goto_pathfinder_destroy(finder);
}

static void v2_build_unit_set_route_action(
  const struct unit *punit, struct agent_v2_action_buffer *buffer)
{
  size_t index;

  if (!v2_unit_goto_actor_clean(punit)) {
    return;
  }
  index = buffer->count;
  v2_buffer_add_unit_automation_action(
    buffer, AGENT_V2_ACTION_UNIT_SET_ROUTE, punit);
  if (!buffer->overflow && buffer->count == index + 1) {
    buffer->actions[index].route_waypoint_limit =
      CLIENT_UNIT_ROUTE_MAX_WAYPOINTS;
  }
}

static void v2_buffer_add_self_action(
  struct agent_v2_action_buffer *buffer,
  enum agent_v2_action_kind kind, const struct unit *punit,
  enum unit_activity activity, const struct unit_type *target_type,
  action_id action, struct act_prob probability)
{
  enum agent_v2_probability_kind candidate_kind;
  int candidate_min;
  int candidate_max;
  int target_type_id = target_type != NULL ? utype_number(target_type) : -1;
  size_t i;

  if (!v2_normalize_probability(probability, &candidate_kind,
                                &candidate_min, &candidate_max)) {
    buffer->overflow = TRUE;
    return;
  }
  if (candidate_kind == AGENT_V2_PROBABILITY_NOT_IMPLEMENTED) {
    return;
  }

  /* Multiple ruleset variants that install the same provable result are one
   * public capability. Retain the strongest deterministic native variant;
   * the trust boundary exposes only an opaque variant identifier. */
  for (i = 0; i < buffer->count; i++) {
    struct agent_v2_action *existing = &buffer->actions[i];

    if (existing->kind == kind
        && existing->unit_id == punit->id
        && existing->target_activity == activity
        && existing->target_build_kind
           == (target_type != NULL ? VUT_UTYPE : VUT_NONE)
        && existing->target_build_id == target_type_id) {
      if (v2_probability_preferred(
            candidate_kind, candidate_min, candidate_max, action,
            existing)) {
        existing->action = action;
        existing->probability_kind = candidate_kind;
        existing->probability_min = candidate_min;
        existing->probability_max = candidate_max;
      }
      return;
    }
  }

  if (buffer->count >= buffer->capacity) {
    buffer->overflow = TRUE;
    return;
  }
  {
    struct agent_v2_action *entry = &buffer->actions[buffer->count++];

    v2_action_init(entry);
    entry->kind = kind;
    entry->unit_id = punit->id;
    entry->unit_incarnation = v2_existing_incarnation(
      AGENT_V2_ENTITY_UNIT, punit->id);
    entry->unit_lifecycle_id = punit->client.lifecycle_id;
    entry->target_build_kind = target_type != NULL ? VUT_UTYPE : VUT_NONE;
    entry->target_build_id = target_type_id;
    entry->target_extra = EXTRA_NONE;
    entry->target_activity = activity;
    entry->action = action;
    entry->probability_kind = candidate_kind;
    entry->probability_min = candidate_min;
    entry->probability_max = candidate_max;
  }
}

static void v2_build_self_unit_result(
  const struct unit *punit, enum action_result result,
  enum agent_v2_action_kind kind, enum unit_activity activity,
  const struct unit_type *target_type,
  struct agent_v2_action_buffer *buffer)
{
  action_by_result_iterate(paction, result) {
    struct act_prob probability;

    if (action_get_actor_kind(paction) != AAK_UNIT
        || action_get_target_kind(paction) != ATK_SELF
        || paction->actor_consuming_always
           != (result == ACTRES_DISBAND_UNIT)) {
      continue;
    }
    probability = action_prob_self(&wld.map, punit, paction->id);
    if (action_prob_possible(probability)) {
      v2_buffer_add_self_action(buffer, kind, punit, activity, target_type,
                                paction->id, probability);
    }
  } action_by_result_iterate_end;
}

static void v2_build_self_unit_actions(
  const struct unit *punit, struct agent_v2_action_buffer *buffer)
{
  const struct unit_type *current;
  const struct unit_type *converted;

  if (punit == NULL || punit->ssa_controller != SSA_NONE
      || unit_tile(punit) == NULL) {
    return;
  }
  current = unit_type_get(punit);
  converted = current != NULL ? current->converted_to : NULL;

  if (punit->activity != ACTIVITY_FORTIFYING
      && punit->activity != ACTIVITY_FORTIFIED) {
    v2_build_self_unit_result(
      punit, ACTRES_FORTIFY, AGENT_V2_ACTION_UNIT_FORTIFY,
      ACTIVITY_FORTIFYING, NULL, buffer);
  }
  if (converted != NULL && converted != current
      && punit->activity != ACTIVITY_CONVERT) {
    v2_build_self_unit_result(
      punit, ACTRES_CONVERT, AGENT_V2_ACTION_UNIT_CONVERT,
      ACTIVITY_CONVERT, converted, buffer);
  }
  v2_build_self_unit_result(
    punit, ACTRES_DISBAND_UNIT, AGENT_V2_ACTION_UNIT_DISBAND,
    ACTIVITY_LAST, NULL, buffer);
  if (punit->homecity != IDENTITY_NUMBER_ZERO) {
    v2_build_self_unit_result(
      punit, ACTRES_HOMELESS, AGENT_V2_ACTION_UNIT_HOMELESS,
      ACTIVITY_LAST, NULL, buffer);
  }
  if (punit->activity != ACTIVITY_SENTRY
      && can_unit_do_activity_client(punit, ACTIVITY_SENTRY)) {
    v2_buffer_add_self_action(
      buffer, AGENT_V2_ACTION_UNIT_SENTRY, punit, ACTIVITY_SENTRY,
      NULL, ACTION_NONE, action_prob_new_certain());
  }
}

static bool v2_city_site_known(const struct city *pcity)
{
  return pcity != NULL && city_tile(pcity) != NULL
         && pcity->client.lifecycle_id != 0
         && client_tile_get_known(city_tile(pcity)) != TILE_UNKNOWN;
}

static void v2_buffer_add_city_target_unit_action(
  struct agent_v2_action_buffer *buffer,
  enum agent_v2_action_kind kind, const struct unit *punit,
  const struct city *source_city, const struct city *destination_city,
  const struct unit_type *target_type, action_id action,
  struct act_prob probability)
{
  enum agent_v2_probability_kind candidate_kind;
  int candidate_min;
  int candidate_max;
  int target_type_id = target_type != NULL
                       ? utype_number(target_type) : -1;
  size_t i;

  if (punit == NULL || punit->client.lifecycle_id == 0
      || !v2_city_site_known(destination_city)
      || (source_city != NULL && !v2_city_site_known(source_city))
      || !v2_normalize_probability(probability, &candidate_kind,
                                   &candidate_min, &candidate_max)) {
    buffer->overflow = TRUE;
    return;
  }
  for (i = 0; i < buffer->count; i++) {
    struct agent_v2_action *existing = &buffer->actions[i];

    if (existing->kind == kind
        && existing->unit_id == punit->id
        && existing->source_city_id
           == (source_city != NULL ? source_city->id : -1)
        && existing->destination_city_id == destination_city->id
        && existing->target_build_kind
           == (target_type != NULL ? VUT_UTYPE : VUT_NONE)
        && existing->target_build_id == target_type_id) {
      if (v2_probability_preferred(candidate_kind, candidate_min,
                                   candidate_max, action, existing)) {
        existing->action = action;
        existing->probability_kind = candidate_kind;
        existing->probability_min = candidate_min;
        existing->probability_max = candidate_max;
      }
      return;
    }
  }
  if (buffer->count >= buffer->capacity) {
    buffer->overflow = TRUE;
    return;
  }
  {
    struct agent_v2_action *entry = &buffer->actions[buffer->count++];

    v2_action_init(entry);
    entry->kind = kind;
    entry->unit_id = punit->id;
    entry->unit_incarnation = v2_existing_incarnation(
      AGENT_V2_ENTITY_UNIT, punit->id);
    entry->unit_lifecycle_id = punit->client.lifecycle_id;
    if (source_city != NULL) {
      entry->source_city_id = source_city->id;
      entry->source_city_incarnation = v2_existing_incarnation(
        AGENT_V2_ENTITY_CITY, source_city->id);
      entry->source_city_lifecycle_id = source_city->client.lifecycle_id;
      entry->source_city_tile = tile_index(city_tile(source_city));
    }
    entry->destination_city_id = destination_city->id;
    entry->destination_city_incarnation = v2_existing_incarnation(
      AGENT_V2_ENTITY_CITY, destination_city->id);
    entry->destination_city_lifecycle_id =
      destination_city->client.lifecycle_id;
    entry->destination_city_tile = tile_index(city_tile(destination_city));
    entry->target_build_kind = target_type != NULL ? VUT_UTYPE : VUT_NONE;
    entry->target_build_id = target_type_id;
    entry->action = action;
    entry->probability_kind = candidate_kind;
    entry->probability_min = candidate_min;
    entry->probability_max = candidate_max;
  }
}

static enum agent_v2_action_kind v2_city_target_unit_kind(
  enum action_result result)
{
  switch (result) {
  case ACTRES_UPGRADE_UNIT:
    return AGENT_V2_ACTION_UNIT_UPGRADE;
  case ACTRES_HOME_CITY:
    return AGENT_V2_ACTION_UNIT_REHOME;
  case ACTRES_JOIN_CITY:
    return AGENT_V2_ACTION_UNIT_JOIN_CITY;
  case ACTRES_TRADE_ROUTE:
    return AGENT_V2_ACTION_UNIT_ESTABLISH_TRADE;
  case ACTRES_MARKETPLACE:
    return AGENT_V2_ACTION_UNIT_MARKETPLACE;
  case ACTRES_HELP_WONDER:
    return AGENT_V2_ACTION_UNIT_HELP_WONDER;
  case ACTRES_DISBAND_UNIT_RECOVER:
    return AGENT_V2_ACTION_UNIT_DISBAND_RECOVER;
  default:
    return AGENT_V2_ACTION_KIND_COUNT;
  }
}

static void v2_build_city_target_unit_destination(
  const struct unit *punit, enum agent_v2_action_kind kind,
  const struct city *source_city, const struct city *destination_city,
  const struct unit_type *target_type,
  const action_id *eligible_actions, size_t eligible_count,
  bool unlimited_distance, int maximum_distance,
  struct agent_v2_action_buffer *buffer)
{
  struct player *self = client_player();
  int distance;
  size_t i;

  if (self == NULL || destination_city == NULL
      || !v2_city_site_known(destination_city)
      || !player_can_see_city_externals(self, destination_city)) {
    return;
  }
  distance = real_map_distance(unit_tile(punit),
                               city_tile(destination_city));
  if (!fc_agent_v2_city_target_distance_candidate(
        distance, unlimited_distance, maximum_distance)) {
    return;
  }
  for (i = 0; i < eligible_count; i++) {
    const struct action *paction = action_by_number(eligible_actions[i]);
    struct act_prob probability;

    if (!action_distance_accepted(paction, distance)) {
      continue;
    }
    probability = action_prob_vs_city(
      &wld.map, punit, paction->id, destination_city);
    if (action_prob_possible(probability)) {
      v2_buffer_add_city_target_unit_action(
        buffer, kind, punit, source_city, destination_city,
        target_type, paction->id, probability);
    }
  }
}

static void v2_build_city_target_unit_result(
  const struct unit *punit, enum action_result result,
  struct agent_v2_action_buffer *buffer)
{
  struct player *self = client_player();
  enum agent_v2_action_kind kind = v2_city_target_unit_kind(result);
  const struct city *source_city = NULL;
  const struct unit_type *actor_type;
  const struct unit_type *target_type = NULL;
  action_id eligible_actions[ACTION_COUNT];
  size_t eligible_count = 0;
  int maximum_distance = -1;
  bool unlimited_distance = FALSE;
  bool consuming = result == ACTRES_JOIN_CITY
                   || result == ACTRES_TRADE_ROUTE
                   || result == ACTRES_MARKETPLACE
                   || result == ACTRES_HELP_WONDER
                   || result == ACTRES_DISBAND_UNIT_RECOVER;

  if (self == NULL || punit == NULL || unit_owner(punit) != self
      || punit->ssa_controller != SSA_NONE || unit_tile(punit) == NULL
      || punit->client.lifecycle_id == 0
      || kind == AGENT_V2_ACTION_KIND_COUNT) {
    return;
  }
  actor_type = unit_type_get(punit);
  if (!utype_can_do_action_result(actor_type, result)) {
    /* The catalog visits the supported city results for every idle unit.
     * Reject incapable unit types before considering any city. */
    return;
  }
  action_by_result_iterate(paction, result) {
    if (action_get_actor_kind(paction) != AAK_UNIT
        || action_get_target_kind(paction) != ATK_CITY
        || paction->actor_consuming_always != consuming
        || !utype_can_do_action(actor_type, paction->id)) {
      continue;
    }
    if (eligible_count >= ARRAY_SIZE(eligible_actions)) {
      buffer->overflow = TRUE;
      return;
    }
    eligible_actions[eligible_count++] = paction->id;
    if (paction->max_distance == ACTION_DISTANCE_UNLIMITED) {
      unlimited_distance = TRUE;
    } else if (paction->max_distance > maximum_distance) {
      maximum_distance = paction->max_distance;
    }
  } action_by_result_iterate_end;
  if (eligible_count == 0) {
    return;
  }
  if (result == ACTRES_TRADE_ROUTE || result == ACTRES_MARKETPLACE) {
    source_city = player_city_by_number(self, punit->homecity);
    if (!v2_city_site_known(source_city)) {
      return;
    }
  }
  if (result == ACTRES_UPGRADE_UNIT) {
    target_type = can_upgrade_unittype(self, actor_type);
    if (target_type == NULL || target_type == actor_type) {
      return;
    }
  }
  if (unlimited_distance) {
    /* Unlimited ruleset actions have no safe tile radius. They alone retain
     * the all-city fallback; finite variants are still rejected below by
     * action_distance_accepted(). */
    players_iterate(owner) {
      city_list_iterate(owner->cities, destination_city) {
        v2_build_city_target_unit_destination(
          punit, kind, source_city, destination_city, target_type,
          eligible_actions, eligible_count, TRUE, maximum_distance, buffer);
        if (buffer->overflow) {
          return;
        }
      } city_list_iterate_end;
    } players_iterate_end;
  } else {
    /* Finite city actions enumerate only tiles inside the widest capable
     * action radius. iterate_outward yields each map tile at most once, so a
     * city cannot be duplicated even on wrapping maps. */
    iterate_outward(&wld.map, unit_tile(punit), maximum_distance,
                    destination_tile) {
      const struct city *destination_city = tile_city(destination_tile);

      if (destination_city == NULL) {
        continue;
      }
      v2_build_city_target_unit_destination(
        punit, kind, source_city, destination_city, target_type,
        eligible_actions, eligible_count, FALSE, maximum_distance, buffer);
      if (buffer->overflow) {
        break;
      }
    } iterate_outward_end;
  }
}

static void v2_build_city_target_unit_actions(
  const struct unit *punit, struct agent_v2_action_buffer *buffer)
{
  static const enum action_result results[] = {
    ACTRES_UPGRADE_UNIT, ACTRES_HOME_CITY, ACTRES_JOIN_CITY,
    ACTRES_TRADE_ROUTE, ACTRES_MARKETPLACE, ACTRES_HELP_WONDER,
    ACTRES_DISBAND_UNIT_RECOVER
  };
  size_t i;

  for (i = 0; i < ARRAY_SIZE(results); i++) {
    v2_build_city_target_unit_result(punit, results[i], buffer);
    if (buffer->overflow) {
      return;
    }
  }
}

enum agent_v2_transport_state {
  AGENT_V2_TRANSPORT_UNTRANSPORTED,
  AGENT_V2_TRANSPORT_TRANSPORTED,
  AGENT_V2_TRANSPORT_UNRESOLVED
};

struct agent_v2_transport_projection {
  bool parent_changes;
  const struct unit *cargo;
  const struct unit *new_transporter;
  const struct unit *moved_root;
  const struct tile *moved_tile;
};

static bool v2_transport_unit_visible(const struct player *self,
                                      const struct unit *unit)
{
  const struct player *owner;

  if (self == NULL || unit == NULL || unit_tile(unit) == NULL
      || unit->client.lifecycle_id == 0
      || (owner = unit_owner(unit)) == NULL
      || owner->client.lifecycle_id == 0) {
    return FALSE;
  }
  return owner == self
         || (pplayers_allied(self, owner)
             && can_player_see_unit(self, unit));
}

static const struct unit *v2_transport_root(const struct unit *unit)
{
  const struct unit *current = unit;
  int depth;

  for (depth = 0; current != NULL; depth++) {
    const struct unit *parent = unit_transport_get(current);

    if (current->client.transported_by < 0) {
      return parent == NULL ? current : NULL;
    }
    if (parent == NULL || parent == current
        || parent->id != current->client.transported_by
        || depth >= GAME_TRANSPORT_MAX_RECURSIVE) {
      return NULL;
    }
    current = parent;
  }
  return NULL;
}

static bool v2_transport_unit_cache_exact(
  const struct player *self, const struct unit *unit,
  const struct unit *component_root)
{
  const struct unit *parent;
  struct unit_list *cargo;
  int occupancy;

  if (!v2_transport_unit_visible(self, unit)
      || v2_transport_root(unit) != component_root
      || (cargo = unit_transport_cargo(unit)) == NULL) {
    return FALSE;
  }
  parent = unit_transport_get(unit);
  if ((parent == NULL) != (unit->client.transported_by < 0)) {
    return FALSE;
  }
  if (parent != NULL
      && (!v2_transport_unit_visible(self, parent)
          || parent->id != unit->client.transported_by
          || !pplayers_allied(unit_owner(unit), unit_owner(parent))
          || !same_pos(unit_tile(unit), unit_tile(parent))
          || unit_transport_cargo(parent) == NULL
          || !unit_list_search(unit_transport_cargo(parent), unit))) {
    return FALSE;
  }
  occupancy = unit_list_size(cargo);
  if (!fc_agent_v2_transport_occupancy_exact(
        unit->client.occupied, occupancy,
        get_transporter_capacity(unit), unit_owner(unit) == self)) {
    return FALSE;
  }
  unit_list_iterate(cargo, carried) {
    if (carried == unit
        || !v2_transport_unit_visible(self, carried)
        || v2_transport_root(carried) != component_root
        || carried->client.transported_by != unit->id
        || unit_transport_get(carried) != unit
        || !pplayers_allied(unit_owner(carried), unit_owner(unit))
        || !same_pos(unit_tile(carried), unit_tile(unit))
        || !can_unit_transport(unit, carried)
        || !unit_transport_check(carried, unit)) {
      return FALSE;
    }
  } unit_list_iterate_end;
  return TRUE;
}

static bool v2_transport_root_is_selected(
  const struct unit *root, const struct unit *const roots[], size_t count)
{
  size_t i;

  for (i = 0; i < count; i++) {
    if (roots[i] == root) {
      return TRUE;
    }
  }
  return FALSE;
}

static bool v2_transport_add_root(const struct unit *unit,
                                  const struct unit *roots[],
                                  size_t *count)
{
  const struct unit *root;

  if (unit == NULL) {
    return TRUE;
  }
  root = v2_transport_root(unit);
  if (root == NULL) {
    return FALSE;
  }
  if (!v2_transport_root_is_selected(root, roots, *count)) {
    roots[(*count)++] = root;
  }
  return TRUE;
}

static const struct unit *v2_transport_projected_parent(
  const struct unit *unit,
  const struct agent_v2_transport_projection *projection)
{
  if (projection != NULL && projection->parent_changes
      && projection->cargo == unit) {
    return projection->new_transporter;
  }
  return unit_transport_get(unit);
}

static bool v2_transport_projected_moved(
  const struct unit *unit,
  const struct agent_v2_transport_projection *projection)
{
  return projection != NULL && projection->moved_root != NULL
         && (unit == projection->moved_root
             || unit_contained_in(unit, projection->moved_root));
}

/* Hash the complete caller-visible transport components touched by a
 * command.  Native identifiers remain private inside the opaque slot; the
 * public surface receives only lifecycle-backed entity references. */
static bool v2_transport_component_signature(
  const struct player *self, const struct unit *first,
  const struct unit *second, const struct unit *third,
  const struct agent_v2_transport_projection *projection,
  uint64_t *signature)
{
  const struct unit *roots[3];
  size_t root_count = 0;
  size_t member_count = 0;
  uint64_t hash = UINT64_C(1469598103934665603);

  if (self == NULL || signature == NULL
      || !v2_transport_add_root(first, roots, &root_count)
      || !v2_transport_add_root(second, roots, &root_count)
      || !v2_transport_add_root(third, roots, &root_count)
      || root_count == 0) {
    return FALSE;
  }
  players_iterate(owner) {
    unit_list_iterate(owner->units, unit) {
      const struct unit *root = v2_transport_root(unit);
      const struct unit *parent;
      const struct player_diplstate *forward;
      const struct player_diplstate *reverse;
      int owner_id;
      uint64_t owner_lifecycle;
      int unit_type;
      int tile;
      int capacity;
      int occupancy;
      int parent_id;
      uint64_t parent_lifecycle;

      if (!v2_transport_root_is_selected(root, roots, root_count)) {
        continue;
      }
      if (!v2_transport_unit_cache_exact(self, unit, root)) {
        return FALSE;
      }
      parent = v2_transport_projected_parent(unit, projection);
      owner_id = player_number(owner);
      owner_lifecycle = owner->client.lifecycle_id;
      unit_type = utype_number(unit_type_get(unit));
      tile = v2_transport_projected_moved(unit, projection)
             ? tile_index(projection->moved_tile)
             : tile_index(unit_tile(unit));
      capacity = get_transporter_capacity(unit);
      occupancy = get_transporter_occupancy(unit);
      if (projection != NULL && projection->parent_changes
          && projection->cargo != NULL) {
        const struct unit *old_parent = unit_transport_get(
          projection->cargo);

        if (unit == old_parent && old_parent != projection->new_transporter) {
          occupancy--;
        }
        if (unit == projection->new_transporter
            && old_parent != projection->new_transporter) {
          occupancy++;
        }
      }
      if (tile < 0 || capacity < 0 || occupancy < 0
          || occupancy > capacity
          || (projection != NULL
              && !fc_agent_v2_transport_occupancy_exact(
                   occupancy > 0, occupancy, capacity,
                   unit_owner(unit) == self))) {
        return FALSE;
      }
      parent_id = parent != NULL ? parent->id : -1;
      parent_lifecycle = parent != NULL
                         ? parent->client.lifecycle_id : 0;
      forward = player_diplstate_get(self, owner);
      reverse = player_diplstate_get(owner, self);
      if (forward == NULL || reverse == NULL) {
        return FALSE;
      }
      hash = v2_hash_bytes(hash, &unit->id, sizeof(unit->id));
      hash = v2_hash_bytes(
        hash, &unit->client.lifecycle_id,
        sizeof(unit->client.lifecycle_id));
      hash = v2_hash_bytes(hash, &owner_id, sizeof(owner_id));
      hash = v2_hash_bytes(
        hash, &owner_lifecycle, sizeof(owner_lifecycle));
      hash = v2_hash_bytes(hash, &forward->type, sizeof(forward->type));
      hash = v2_hash_bytes(
        hash, &forward->contact_turns_left,
        sizeof(forward->contact_turns_left));
      hash = v2_hash_bytes(hash, &reverse->type, sizeof(reverse->type));
      hash = v2_hash_bytes(
        hash, &reverse->contact_turns_left,
        sizeof(reverse->contact_turns_left));
      hash = v2_hash_bytes(hash, &unit_type, sizeof(unit_type));
      hash = v2_hash_bytes(hash, &tile, sizeof(tile));
      hash = v2_hash_bytes(hash, &capacity, sizeof(capacity));
      hash = v2_hash_bytes(hash, &occupancy, sizeof(occupancy));
      hash = v2_hash_bytes(hash, &parent_id, sizeof(parent_id));
      hash = v2_hash_bytes(
        hash, &parent_lifecycle, sizeof(parent_lifecycle));
      member_count++;
    } unit_list_iterate_end;
  } players_iterate_end;
  if (member_count == 0) {
    return FALSE;
  }
  hash = v2_hash_bytes(hash, &member_count, sizeof(member_count));
  *signature = hash != 0 ? hash : UINT64_C(1);
  return TRUE;
}

static enum agent_v2_transport_state v2_transport_inbound_state(
  const struct player *self, const struct unit *cargo,
  const struct unit **transporter)
{
  const struct unit *linked;

  if (transporter != NULL) {
    *transporter = NULL;
  }
  if (!v2_transport_unit_visible(self, cargo)) {
    return AGENT_V2_TRANSPORT_UNRESOLVED;
  }
  linked = unit_transport_get(cargo);
  if (cargo->client.transported_by == -1 && linked == NULL) {
    return AGENT_V2_TRANSPORT_UNTRANSPORTED;
  }
  if (cargo->client.transported_by < 0 || linked == NULL
      || linked == cargo || linked->id != cargo->client.transported_by
      || !v2_transport_unit_visible(self, linked)
      || !pplayers_allied(unit_owner(cargo), unit_owner(linked))
      || !same_pos(unit_tile(cargo), unit_tile(linked))
      || unit_transport_cargo(linked) == NULL
      || !unit_list_search(unit_transport_cargo(linked), cargo)) {
    return AGENT_V2_TRANSPORT_UNRESOLVED;
  }
  if (transporter != NULL) {
    *transporter = linked;
  }
  return AGENT_V2_TRANSPORT_TRANSPORTED;
}

static bool v2_transport_outbound_exact(const struct player *self,
                                        const struct unit *transporter)
{
  struct unit_list *cargo;
  int occupancy;

  if (!v2_transport_unit_visible(self, transporter)
      || (cargo = unit_transport_cargo(transporter)) == NULL) {
    return FALSE;
  }
  occupancy = unit_list_size(cargo);
  if (!fc_agent_v2_transport_occupancy_exact(
        transporter->client.occupied, occupancy,
        get_transporter_capacity(transporter),
        unit_owner(transporter) == self)) {
    return FALSE;
  }
  unit_list_iterate(cargo, carried) {
    const struct unit *resolved = NULL;

    if (v2_transport_inbound_state(self, carried, &resolved)
        != AGENT_V2_TRANSPORT_TRANSPORTED
        || resolved != transporter) {
      return FALSE;
    }
  } unit_list_iterate_end;
  return TRUE;
}

static enum agent_v2_transport_state v2_transport_state(
  const struct player *self, const struct unit *unit,
  const struct unit **transporter, int *occupied)
{
  enum agent_v2_transport_state state = v2_transport_inbound_state(
    self, unit, transporter);

  if (!v2_transport_outbound_exact(self, unit)) {
    if (transporter != NULL) {
      *transporter = NULL;
    }
    if (occupied != NULL) {
      *occupied = -1;
    }
    return AGENT_V2_TRANSPORT_UNRESOLVED;
  }
  if (occupied != NULL) {
    *occupied = get_transporter_occupancy(unit);
  }
  return state;
}

static const char *v2_transport_state_name(
  enum agent_v2_transport_state state)
{
  switch (state) {
  case AGENT_V2_TRANSPORT_UNTRANSPORTED:
    return "untransported";
  case AGENT_V2_TRANSPORT_TRANSPORTED:
    return "transported";
  case AGENT_V2_TRANSPORT_UNRESOLVED:
    return "unresolved";
  }
  return NULL;
}

static bool v2_transport_load_pair(const struct player *self,
                                   const struct unit *cargo,
                                   const struct unit *transporter)
{
  uint64_t signature;

  return cargo != NULL && transporter != NULL && cargo != transporter
         && v2_transport_unit_visible(self, cargo)
         && v2_transport_unit_visible(self, transporter)
         && pplayers_allied(unit_owner(cargo), unit_owner(transporter))
         && get_transporter_capacity(transporter) > 0
         && unit_transport_get(cargo) != transporter
         && !unit_contained_in(transporter, cargo)
         && v2_transport_component_signature(
              self, cargo, transporter, unit_transport_get(cargo),
              NULL, &signature);
}

static bool v2_transport_linked_pair(const struct player *self,
                                     const struct unit *cargo,
                                     const struct unit *transporter)
{
  const struct unit *resolved = NULL;
  uint64_t signature;

  return cargo != NULL && transporter != NULL && cargo != transporter
         && v2_transport_unit_visible(self, cargo)
         && v2_transport_unit_visible(self, transporter)
         && pplayers_allied(unit_owner(cargo), unit_owner(transporter))
         && get_transporter_capacity(transporter) > 0
         && v2_transport_inbound_state(self, cargo, &resolved)
            == AGENT_V2_TRANSPORT_TRANSPORTED
         && resolved == transporter
         && v2_transport_component_signature(
              self, cargo, transporter, NULL, NULL, &signature);
}

static bool v2_probability_is_certain(struct act_prob probability,
                                      enum agent_v2_probability_kind *kind,
                                      int *minimum, int *maximum)
{
  struct act_prob certain = action_prob_new_certain();

  return v2_normalize_probability(probability, kind, minimum, maximum)
         && *kind == AGENT_V2_PROBABILITY_EXACT
         && *minimum == certain.min && *maximum == certain.max;
}

static void v2_buffer_add_transport_action(
  struct agent_v2_action_buffer *buffer,
  enum agent_v2_action_kind kind, const struct unit *actor,
  const struct unit *target_unit, const struct tile *target_tile,
  const struct unit *transport_context, action_id action,
  struct act_prob probability)
{
  enum agent_v2_probability_kind candidate_kind;
  int candidate_min;
  int candidate_max;
  int target_unit_id = target_unit != NULL ? target_unit->id : -1;
  int target_tile_id = target_tile != NULL ? tile_index(target_tile) : -1;
  int context_id = transport_context != NULL ? transport_context->id : -1;
  const struct unit *cargo;
  const struct unit *new_transporter;
  struct agent_v2_transport_projection projection = {
    .parent_changes = TRUE,
    .cargo = NULL,
    .new_transporter = NULL,
    .moved_root = NULL,
    .moved_tile = NULL
  };
  uint64_t before_signature;
  uint64_t after_signature;
  size_t i;

  if (!v2_probability_is_certain(probability, &candidate_kind,
                                 &candidate_min, &candidate_max)) {
    return;
  }
  switch (kind) {
  case AGENT_V2_ACTION_TRANSPORT_BOARD:
  case AGENT_V2_ACTION_TRANSPORT_EMBARK:
    cargo = actor;
    new_transporter = target_unit;
    break;
  case AGENT_V2_ACTION_TRANSPORT_LOAD:
    cargo = target_unit;
    new_transporter = actor;
    break;
  case AGENT_V2_ACTION_TRANSPORT_DEBOARD:
  case AGENT_V2_ACTION_TRANSPORT_DISEMBARK:
    cargo = actor;
    new_transporter = NULL;
    break;
  case AGENT_V2_ACTION_TRANSPORT_UNLOAD:
    cargo = target_unit;
    new_transporter = NULL;
    break;
  default:
    return;
  }
  projection.cargo = cargo;
  projection.new_transporter = new_transporter;
  if (kind == AGENT_V2_ACTION_TRANSPORT_EMBARK
      || kind == AGENT_V2_ACTION_TRANSPORT_DISEMBARK) {
    projection.moved_root = cargo;
    projection.moved_tile = kind == AGENT_V2_ACTION_TRANSPORT_EMBARK
                            ? unit_tile(new_transporter) : target_tile;
  }
  if (cargo == NULL
      || (projection.moved_root != NULL
          && projection.moved_tile == NULL)
      || !v2_transport_component_signature(
           unit_owner(actor), actor, target_unit, transport_context,
           NULL, &before_signature)
      || !v2_transport_component_signature(
           unit_owner(actor), actor, target_unit, transport_context,
           &projection, &after_signature)
      || before_signature == after_signature) {
    return;
  }
  for (i = 0; i < buffer->count; i++) {
    struct agent_v2_action *existing = &buffer->actions[i];

    if (existing->kind == kind && existing->unit_id == actor->id
        && existing->target_unit_id == target_unit_id
        && existing->target_tile == target_tile_id
        && existing->transport_context_id == context_id) {
      if (v2_probability_preferred(
            candidate_kind, candidate_min, candidate_max, action,
            existing)) {
        existing->action = action;
      }
      return;
    }
  }
  if (buffer->count >= buffer->capacity) {
    buffer->overflow = TRUE;
    return;
  }
  {
    struct agent_v2_action *entry = &buffer->actions[buffer->count++];

    v2_action_init(entry);
    entry->kind = kind;
    entry->unit_id = actor->id;
    entry->unit_incarnation = v2_existing_incarnation(
      AGENT_V2_ENTITY_UNIT, actor->id);
    entry->unit_lifecycle_id = actor->client.lifecycle_id;
    entry->target_tile = target_tile_id;
    entry->target_unit_id = target_unit_id;
    entry->target_unit_incarnation = target_unit != NULL
      ? v2_existing_incarnation(AGENT_V2_ENTITY_UNIT, target_unit->id) : 0;
    entry->target_unit_lifecycle_id = target_unit != NULL
      ? target_unit->client.lifecycle_id : 0;
    entry->transport_context_id = context_id;
    entry->transport_context_incarnation = transport_context != NULL
      ? v2_existing_incarnation(AGENT_V2_ENTITY_UNIT,
                                transport_context->id) : 0;
    entry->transport_context_lifecycle_id = transport_context != NULL
      ? transport_context->client.lifecycle_id : 0;
    entry->transport_before_signature = before_signature;
    entry->transport_after_signature = after_signature;
    entry->action = action;
    entry->probability_kind = candidate_kind;
    entry->probability_min = candidate_min;
    entry->probability_max = candidate_max;
  }
}

static void v2_build_transport_unit_result(
  const struct unit *actor, const struct unit *target,
  const struct unit *transport_context, enum action_result result,
  enum agent_v2_action_kind kind, struct agent_v2_action_buffer *buffer)
{
  action_by_result_iterate(paction, result) {
    struct act_prob probability;

    if (action_get_actor_kind(paction) != AAK_UNIT
        || action_get_target_kind(paction) != ATK_UNIT
        || paction->actor_consuming_always) {
      continue;
    }
    probability = action_prob_vs_unit(
      &wld.map, actor, paction->id, target);
    if (action_prob_possible(probability)) {
      v2_buffer_add_transport_action(
        buffer, kind, actor, target, NULL, transport_context,
        paction->id, probability);
    }
  } action_by_result_iterate_end;
}

static void v2_build_transport_disembark(
  const struct unit *actor, const struct unit *transport_context,
  const struct tile *target, struct agent_v2_action_buffer *buffer)
{
  action_by_result_iterate(paction, ACTRES_TRANSPORT_DISEMBARK) {
    struct act_prob probability;

    if (action_get_actor_kind(paction) != AAK_UNIT
        || action_get_target_kind(paction) != ATK_TILE
        || paction->actor_consuming_always) {
      continue;
    }
    probability = action_prob_vs_tile(
      &wld.map, actor, paction->id, target, NULL);
    if (action_prob_possible(probability)) {
      v2_buffer_add_transport_action(
        buffer, AGENT_V2_ACTION_TRANSPORT_DISEMBARK,
        actor, NULL, target, transport_context, paction->id, probability);
    }
  } action_by_result_iterate_end;
}

static void v2_build_transport_actions(
  const struct unit *actor, struct agent_v2_action_buffer *buffer)
{
  struct player *self = client_player();
  const struct unit *current_transport = NULL;
  enum agent_v2_transport_state actor_state;
  struct tile *origin;
  uint64_t actor_signature;

  if (self == NULL || actor == NULL || unit_owner(actor) != self
      || actor->ssa_controller != SSA_NONE
      || actor->client.lifecycle_id == 0
      || (origin = unit_tile(actor)) == NULL
      || !v2_transport_component_signature(
           self, actor, NULL, NULL, NULL, &actor_signature)) {
    return;
  }
  actor_state = v2_transport_inbound_state(
    self, actor, &current_transport);
  if (actor_state == AGENT_V2_TRANSPORT_TRANSPORTED) {
    if (!v2_transport_linked_pair(self, actor, current_transport)) {
      return;
    }
    v2_build_transport_unit_result(
      actor, current_transport, current_transport,
      ACTRES_TRANSPORT_DEBOARD, AGENT_V2_ACTION_TRANSPORT_DEBOARD, buffer);
    adjc_iterate(&wld.map, origin, target) {
      v2_build_transport_disembark(
        actor, current_transport, target, buffer);
    } adjc_iterate_end;
  } else if (actor_state != AGENT_V2_TRANSPORT_UNTRANSPORTED) {
    return;
  }

  unit_list_iterate(origin->units, target) {
    if (v2_transport_load_pair(self, actor, target)) {
      v2_build_transport_unit_result(
        actor, target, current_transport, ACTRES_TRANSPORT_BOARD,
        AGENT_V2_ACTION_TRANSPORT_BOARD, buffer);
    }
    if (v2_transport_load_pair(self, target, actor)) {
      v2_build_transport_unit_result(
        actor, target, unit_transport_get(target), ACTRES_TRANSPORT_LOAD,
        AGENT_V2_ACTION_TRANSPORT_LOAD, buffer);
    }
  } unit_list_iterate_end;

  unit_list_iterate(unit_transport_cargo(actor), cargo) {
    if (v2_transport_linked_pair(self, cargo, actor)) {
      v2_build_transport_unit_result(
        actor, cargo, actor, ACTRES_TRANSPORT_UNLOAD,
        AGENT_V2_ACTION_TRANSPORT_UNLOAD, buffer);
    }
  } unit_list_iterate_end;

  adjc_iterate(&wld.map, origin, target_tile) {
    if (client_tile_get_known(target_tile) != TILE_KNOWN_SEEN) {
      continue;
    }
    unit_list_iterate(target_tile->units, target) {
      if (v2_transport_load_pair(self, actor, target)) {
        v2_build_transport_unit_result(
          actor, target, current_transport, ACTRES_TRANSPORT_EMBARK,
          AGENT_V2_ACTION_TRANSPORT_EMBARK, buffer);
      }
    } unit_list_iterate_end;
  } adjc_iterate_end;
}

static uint64_t v2_hash_actor_catalog(
  uint64_t hash, enum agent_v2_entity_kind kind, int id,
  const struct fc_agent_v2_phase_evidence *phase)
{
  struct player *self = client_player();
  struct agent_v2_action_buffer buffer = {
    .actions = v2_scope_actions,
    .count = 0,
    .capacity = AGENT_V2_MAX_ACTIONS,
    .overflow = FALSE,
    .export_unknown_rows = FALSE,
    .unknown_exported = NULL
  };
  uint64_t incarnation = v2_existing_incarnation(kind, id);
  size_t i;

  if (self != NULL) {
    if (kind == AGENT_V2_ENTITY_PLAYER) {
      v2_build_player_actions(self, phase, &buffer);
    }
    if (v2_actions_ready(phase)) {
      if (kind == AGENT_V2_ENTITY_PLAYER) {
      v2_build_government_actions(self, &buffer);
      v2_build_multiplier_actions(self, &buffer);
      v2_build_spaceship_actions(self, &buffer);
      } else if (kind == AGENT_V2_ENTITY_CITY) {
      v2_build_city_actions(player_city_by_number(self, id), &buffer);
      v2_build_city_citizen_actions(player_city_by_number(self, id),
                                    &buffer);
      v2_build_city_management_actions(player_city_by_number(self, id),
                                       &buffer);
      v2_build_city_worker_task_actions(player_city_by_number(self, id),
                                        &buffer);
    } else if (kind == AGENT_V2_ENTITY_UNIT) {
      v2_build_unit_actions(player_unit_by_number(self, id), &buffer);
      v2_build_worker_actions(player_unit_by_number(self, id), &buffer);
      v2_build_unit_automation_actions(
        player_unit_by_number(self, id), &buffer);
      v2_build_unit_cancel_orders_action(
        player_unit_by_number(self, id), &buffer);
      v2_build_unit_goto_actions(
        player_unit_by_number(self, id), &buffer);
      v2_build_unit_set_route_action(
        player_unit_by_number(self, id), &buffer);
      v2_build_self_unit_actions(player_unit_by_number(self, id), &buffer);
      v2_build_city_target_unit_actions(
        player_unit_by_number(self, id), &buffer);
      v2_build_transport_actions(player_unit_by_number(self, id), &buffer);
      v2_build_noncombat_mobility_actions(
        player_unit_by_number(self, id), &buffer);
      }
    }
  }
  if (!buffer.overflow) {
    qsort(buffer.actions, buffer.count, sizeof(buffer.actions[0]),
          v2_action_compare);
  }
  hash = v2_hash_bytes(hash, &kind, sizeof(kind));
  hash = v2_hash_bytes(hash, &id, sizeof(id));
  hash = v2_hash_bytes(hash, &incarnation, sizeof(incarnation));
  hash = v2_hash_bytes(hash, &buffer.overflow, sizeof(buffer.overflow));
  hash = v2_hash_bytes(hash, &buffer.count, sizeof(buffer.count));
  for (i = 0; i < buffer.count; i++) {
    hash = v2_hash_bytes(hash, &buffer.actions[i],
                         offsetof(struct agent_v2_action, slot));
  }
  return hash;
}

static uint64_t v2_hash_scoped_catalogs(
  uint64_t hash, struct player *self,
  const struct fc_agent_v2_phase_evidence *phase)
{
  if (self == NULL) {
    return hash;
  }
  hash = v2_hash_actor_catalog(
    hash, AGENT_V2_ENTITY_PLAYER, player_number(self), phase);
  city_list_iterate(self->cities, pcity) {
    hash = v2_hash_actor_catalog(
      hash, AGENT_V2_ENTITY_CITY, pcity->id, phase);
  } city_list_iterate_end;
  unit_list_iterate(self->units, punit) {
    hash = v2_hash_actor_catalog(
      hash, AGENT_V2_ENTITY_UNIT, punit->id, phase);
  } unit_list_iterate_end;
  return hash;
}

static void v2_build_actions(
  struct player *self, const struct fc_agent_v2_phase_evidence *phase)
{
  struct agent_v2_action_buffer buffer = {
    .actions = v2_work_actions,
    .count = 0,
    .capacity = AGENT_V2_MAX_ACTIONS,
    .overflow = FALSE,
    .export_unknown_rows = FALSE,
    .unknown_exported = NULL
  };

  if (self == NULL) {
    return;
  }
  v2_build_communication_actions(self, &buffer);
  v2_build_player_actions(self, phase, &buffer);
  v2_work_action_count = buffer.count;
  if (buffer.overflow) {
    v2_overflow = TRUE;
  }
}

static bool v2_collect_phase_evidence(
  const struct player *self, struct fc_agent_v2_phase_evidence *evidence)
{
  int mode;
  int team_number_value = -1;

  if (client_state() != C_S_RUNNING || self == NULL) {
    return FALSE;
  }
  switch (game.info.phase_mode) {
  case PMT_CONCURRENT:
    mode = FC_AGENT_V2_PHASE_CONCURRENT;
    break;
  case PMT_PLAYERS_ALTERNATE:
    mode = FC_AGENT_V2_PHASE_PLAYERS_ALTERNATE;
    break;
  case PMT_TEAMS_ALTERNATE:
    if (self->team == NULL) {
      return FALSE;
    }
    mode = FC_AGENT_V2_PHASE_TEAMS_ALTERNATE;
    team_number_value = team_number(self->team);
    break;
  default:
    return FALSE;
  }
  return fc_agent_v2_build_phase_evidence(
    mode, player_count(), team_count(), game.info.turn, game.info.phase,
    player_number(self), team_number_value, TRUE, v2_seat_authorized,
    self->is_alive, self->phase_done, can_end_turn(), evidence);
}

static const char *v2_research_choice_name(Tech_type_id tech)
{
  const struct advance *advance = valid_advance_by_number(tech);

  if (advance != NULL) {
    return advance_rule_name(advance);
  }
  if (tech == A_FUTURE) {
    return "Future Tech";
  }
  if (tech == A_UNSET) {
    return "Unset";
  }
  return "Unavailable";
}

static bool v2_production_supported(const struct universal *target)
{
  return target != NULL
         && (target->kind == VUT_UTYPE
             || target->kind == VUT_IMPROVEMENT)
         && universal_number(target) >= 0
         && universal_rule_name(target) != NULL;
}

static bool v2_worklist_contains(const struct worklist *worklist,
                                 const struct universal *target)
{
  int length = worklist != NULL ? worklist_length(worklist) : -1;
  int i;

  if (length < 0 || target == NULL) {
    return FALSE;
  }
  for (i = 0; i < length; i++) {
    struct universal candidate;

    if (worklist_peek_ith(worklist, &candidate, i)
        && are_universals_equal(&candidate, target)) {
      return TRUE;
    }
  }
  return FALSE;
}

static int v2_worklist_count(const struct worklist *worklist,
                             const struct universal *target)
{
  int length = worklist != NULL ? worklist_length(worklist) : -1;
  int count = 0;
  int i;

  if (length < 0 || target == NULL) {
    return 0;
  }
  for (i = 0; i < length; i++) {
    struct universal candidate;

    if (worklist_peek_ith(worklist, &candidate, i)
        && are_universals_equal(&candidate, target)) {
      count++;
    }
  }
  return count;
}

static void v2_add_research_choice(uint64_t *digest, int *choice_count,
                                   int *previous_id, Tech_type_id tech,
                                   const char *name, const char *state,
                                   bool can_target, bool can_goal)
{
  char encoded_name[AGENT_V2_ROW_MAX];

  if (digest == NULL || choice_count == NULL || previous_id == NULL
      || tech <= *previous_id
      || !v2_encode_row_value(name, encoded_name, sizeof(encoded_name))
      || !fc_agent_v2_research_choices_digest_add(
        digest, tech, name, state, can_target, can_goal)) {
    v2_overflow = TRUE;
    return;
  }
  v2_add_row(FC_AGENT_V2_ROW_RESEARCH_TECH,
             tech, encoded_name, state,
             can_target ? 1 : 0, can_goal ? 1 : 0);
  *previous_id = tech;
  (*choice_count)++;
}

static struct agent_v2_relation_state *v2_relation_state(
  const struct player *other, bool meeting_open)
{
  int id = player_number(other);
  uint64_t incarnation = v2_existing_incarnation(
    AGENT_V2_ENTITY_PLAYER, id);
  struct agent_v2_relation_state *state = NULL;
  size_t i;

  for (i = 0; i < v2_relation_count; i++) {
    if (v2_relations[i].counterpart_id == id) {
      state = &v2_relations[i];
      break;
    }
  }
  if (state == NULL) {
    if (v2_relation_count >= ARRAY_SIZE(v2_relations)) {
      v2_overflow = TRUE;
      return NULL;
    }
    state = &v2_relations[v2_relation_count++];
    memset(state, 0, sizeof(*state));
    state->counterpart_id = id;
  }
  if (state->counterpart_incarnation != incarnation) {
    state->counterpart_incarnation = incarnation;
    state->meeting_generation = 0;
    state->meeting_open = FALSE;
  }
  if (meeting_open && !state->meeting_open) {
    state->meeting_generation++;
    if (state->meeting_generation == 0) {
      v2_overflow = TRUE;
      return NULL;
    }
  }
  state->meeting_open = meeting_open;
  return state;
}

void fc_agent_v2_diplomacy_meeting_opened(int counterpart)
{
  struct player *other = player_by_number(counterpart);

  if (other != NULL && other != client_player()) {
    (void) v2_relation_state(other, TRUE);
  }
  if (v2_pending.active
      && v2_pending.processing_started
      && v2_pending.baseline_captured
      && v2_pending.terminal == FC_AGENT_V2_TERMINAL_NONE
      && v2_pending.action.kind
         == AGENT_V2_ACTION_DIPLOMACY_OPEN_MEETING
      && v2_pending.action.counterpart_id == counterpart
      && client.conn.client.request_id_of_currently_handled_packet
         == v2_pending.request_id
      && client_player() != NULL && other != NULL
      && find_treaty(client_player(), other) != NULL) {
    v2_pending.diplomacy_echo_latched = TRUE;
  }
}

void fc_agent_v2_diplomacy_meeting_closed(int counterpart)
{
  struct player *other = player_by_number(counterpart);

  if (other != NULL && other != client_player()) {
    (void) v2_relation_state(other, FALSE);
  }
  if (v2_pending.active
      && v2_pending.processing_started
      && v2_pending.baseline_captured
      && v2_pending.terminal == FC_AGENT_V2_TERMINAL_NONE
      && v2_pending.action.kind
         == AGENT_V2_ACTION_DIPLOMACY_CLOSE_MEETING
      && v2_pending.action.counterpart_id == counterpart
      && client.conn.client.request_id_of_currently_handled_packet
         == v2_pending.request_id) {
    /* The shared client removes the treaty immediately after this callback. */
    v2_pending.diplomacy_echo_latched = TRUE;
  }
}

void fc_agent_v2_diplomacy_clause_changed(int counterpart)
{
  struct player *other = player_by_number(counterpart);
  struct treaty *treaty = other != NULL
                           ? find_treaty(client_player(), other) : NULL;
  bool present;

  if (!v2_pending.active
      || !v2_pending.processing_started
      || !v2_pending.baseline_captured
      || v2_pending.terminal != FC_AGENT_V2_TERMINAL_NONE
      || v2_pending.action.counterpart_id != counterpart
      || client.conn.client.request_id_of_currently_handled_packet
         != v2_pending.request_id
      || (v2_pending.action.kind
            != AGENT_V2_ACTION_DIPLOMACY_PROPOSE_CLAUSE
          && v2_pending.action.kind
             != AGENT_V2_ACTION_DIPLOMACY_REMOVE_CLAUSE)) {
    return;
  }
  present = v2_treaty_has_clause(
    treaty, v2_pending.action.clause_giver_id,
    v2_pending.action.clause_type, v2_pending.action.clause_value);
  if ((v2_pending.action.kind
       == AGENT_V2_ACTION_DIPLOMACY_PROPOSE_CLAUSE && present)
      || (v2_pending.action.kind
          == AGENT_V2_ACTION_DIPLOMACY_REMOVE_CLAUSE && !present)) {
    v2_pending.diplomacy_echo_latched = TRUE;
  }
}

void fc_agent_v2_diplomacy_acceptance_changed(int counterpart)
{
  struct player *other = player_by_number(counterpart);
  struct treaty *treaty = other != NULL
                           ? find_treaty(client_player(), other) : NULL;
  bool self_accepted;

  if (!v2_pending.active || !v2_pending.processing_started
      || !v2_pending.baseline_captured
      || v2_pending.terminal != FC_AGENT_V2_TERMINAL_NONE
      || treaty == NULL
      || v2_pending.action.counterpart_id != counterpart
      || client.conn.client.request_id_of_currently_handled_packet
         != v2_pending.request_id
      || (v2_pending.action.kind != AGENT_V2_ACTION_DIPLOMACY_ACCEPT
          && v2_pending.action.kind
             != AGENT_V2_ACTION_DIPLOMACY_WITHDRAW_ACCEPTANCE)) {
    return;
  }
  self_accepted = treaty->plr0 == client_player()
                  ? treaty->accept0 : treaty->accept1;
  if (self_accepted
      == (v2_pending.action.desired_acceptance == 1)) {
    v2_pending.diplomacy_echo_latched = TRUE;
  }
}

/* Compatibility anchor for the cross-language digest contract test:
 * static bool v2_treaty_clause_keys( */
static int v2_clause_key_compare(const void *left, const void *right)
{
  const struct agent_v2_clause_key *a = left;
  const struct agent_v2_clause_key *b = right;

  if (a->giver != b->giver) {
    return a->giver < b->giver ? -1 : 1;
  }
  if (a->type != b->type) {
    return a->type < b->type ? -1 : 1;
  }
  if (a->value != b->value) {
    return a->value < b->value ? -1 : 1;
  }
  return 0;
}

static bool v2_treaty_clause_keys(
  const struct treaty *treaty, struct agent_v2_clause_key **keys_result,
  size_t *count_result)
{
  struct agent_v2_clause_key *keys = NULL;
  size_t count = 0;
  size_t expected = treaty != NULL
                    ? (size_t) clause_list_size(treaty->clauses) : 0;

  if (expected > 0) {
    keys = fc_malloc(expected * sizeof(*keys));
  }
  if (treaty != NULL) {
    clause_list_iterate(treaty->clauses, clause) {
      if (count >= expected || clause->from == NULL
          || !clause_type_is_valid(clause->type)) {
        FC_FREE(keys);
        return FALSE;
      }
      keys[count].clause = clause;
      keys[count].giver = player_number(clause->from);
      keys[count].type = (int) clause->type;
      keys[count].value = clause->value;
      count++;
    } clause_list_iterate_end;
  }
  if (count != expected) {
    FC_FREE(keys);
    return FALSE;
  }
  qsort(keys, count, sizeof(*keys), v2_clause_key_compare);
  *keys_result = keys;
  *count_result = count;
  return TRUE;
}

static bool v2_treaty_clause_summary(
  const struct treaty *treaty, size_t *count_result,
  uint64_t *digest_result)
{
  struct agent_v2_clause_key *keys = NULL;
  size_t count = 0;
  uint64_t digest = UINT64_C(14695981039346656037);

  if (!v2_treaty_clause_keys(treaty, &keys, &count)) {
    return FALSE;
  }
  for (size_t i = 0; i < count; i++) {
    char canonical[96];
    int length = fc_snprintf(
      canonical, sizeof(canonical), "%d:%d:%d;",
      keys[i].giver, keys[i].type, keys[i].value);

    if (length < 0 || (size_t) length >= sizeof(canonical)) {
      FC_FREE(keys);
      return FALSE;
    }
    digest = v2_hash_bytes(digest, canonical, (size_t) length);
  }
  FC_FREE(keys);
  *count_result = count;
  *digest_result = digest;
  return TRUE;
}

/* The rows formerly emitted by static bool v2_add_diplomacy_clause_rows(
 * are now available only through the relation-scoped state catalog. */

static bool v2_add_diplomacy_clause_state_row(
  const struct player *other, uint64_t generation, size_t position,
  const struct Clause *clause)
{
  const char *value_kind = "none";
  const char *name = "none";
  char other_ref[48];
  char giver_ref[48];
  char type_value[AGENT_V2_ROW_MAX];
  char name_value[AGENT_V2_ROW_MAX];

  if (clause == NULL || clause->from == NULL
      || !clause_type_is_valid(clause->type)) {
    return FALSE;
  }
  v2_entity_ref(AGENT_V2_ENTITY_PLAYER, player_number(other),
                other_ref, sizeof(other_ref));
  v2_entity_ref(AGENT_V2_ENTITY_PLAYER, player_number(clause->from),
                giver_ref, sizeof(giver_ref));
  switch (clause->type) {
  case CLAUSE_ADVANCE: {
    const struct advance *advance = advance_by_number(clause->value);

    if (advance == NULL) {
      return FALSE;
    }
    value_kind = "technology";
    name = advance_rule_name(advance);
    break;
  }
  case CLAUSE_GOLD:
    value_kind = "gold";
    name = "gold";
    break;
  case CLAUSE_CITY: {
    const struct city *city = game_city_by_number(clause->value);

    if (city == NULL || !v2_city_site_known(city)) {
      value_kind = "city_unavailable";
      name = "unavailable";
      break;
    }
    value_kind = "city";
    name = city_name_get(city);
    break;
  }
  case CLAUSE_MAP:
  case CLAUSE_SEAMAP:
  case CLAUSE_CEASEFIRE:
  case CLAUSE_PEACE:
  case CLAUSE_ALLIANCE:
  case CLAUSE_VISION:
  case CLAUSE_EMBASSY:
  case CLAUSE_SHARED_TILES:
    break;
  case CLAUSE_COUNT:
    return FALSE;
  }
  if (!v2_encode_row_value(clause_type_name(clause->type),
                           type_value, sizeof(type_value))
      || !v2_encode_row_value(name, name_value, sizeof(name_value))) {
    return FALSE;
  }
  v2_state_add_row(FC_AGENT_V2_ROW_DIPLOMACY_CLAUSE,
                   other_ref, (unsigned long long) generation,
                   (int) position, giver_ref, type_value, value_kind,
                   clause->value, name_value);
  return TRUE;
}

static void v2_build_chat_rows(void)
{
  size_t offset;

  for (offset = 0; offset < v2_chat_history_count; offset++) {
    const struct agent_v2_chat_entry *entry = &v2_chat_history[
      (v2_chat_history_start + offset) % FC_AGENT_V2_MAX_CHAT_HISTORY];
    char sender_kind[64];
    char sender_name[AGENT_V2_ROW_MAX];
    char channel[64];
    char event[256];
    char message[AGENT_V2_ROW_MAX];

    if (v2_encode_row_value(entry->sender_kind, sender_kind,
                            sizeof(sender_kind))
        && v2_encode_row_value(entry->sender_name, sender_name,
                               sizeof(sender_name))
        && v2_encode_row_value(entry->channel, channel, sizeof(channel))
        && v2_encode_row_value(entry->event, event, sizeof(event))
        && v2_encode_row_value(entry->message, message, sizeof(message))) {
      v2_add_row(
        FC_AGENT_V2_ROW_CHAT,
        (unsigned long long) entry->sequence, entry->turn, entry->phase,
        sender_kind, sender_name, entry->self ? 1 : 0, channel, event,
        entry->truncated ? 1 : 0, message);
    }
  }
}

static void v2_collect_positive_tech_requirements(
  const struct requirement_vector *requirements, bv_techs *techs)
{
  requirement_vector_iterate(requirements, requirement) {
    if (requirement->present && requirement->source.kind == VUT_ADVANCE
        && requirement->source.value.advance != NULL) {
      Tech_type_id tech = advance_number(requirement->source.value.advance);

      if (valid_advance_by_number(tech) != NULL) {
        BV_SET(*techs, tech);
      }
    }
  } requirement_vector_iterate_end;
}

static void v2_add_research_unlock_row(Tech_type_id tech,
                                       const char *kind, int native_id,
                                       const char *name,
                                       const char *scope)
{
  char kind_value[AGENT_V2_ROW_MAX];
  char name_value[AGENT_V2_ROW_MAX];
  char scope_value[AGENT_V2_ROW_MAX];

  if (!v2_encode_row_value(kind, kind_value, sizeof(kind_value))
      || !v2_encode_row_value(name, name_value, sizeof(name_value))
      || !v2_encode_row_value(scope, scope_value, sizeof(scope_value))) {
    v2_overflow = TRUE;
    return;
  }
  v2_add_row(FC_AGENT_V2_ROW_RESEARCH_UNLOCK,
             tech, kind_value, native_id, name_value, scope_value);
}

static void v2_add_unlock_rows_for_requirements(
  const struct requirement_vector *requirements, const char *kind,
  int native_id, const char *name, const char *scope)
{
  bv_techs techs;

  BV_CLR_ALL(techs);
  v2_collect_positive_tech_requirements(requirements, &techs);
  advance_re_active_iterate(padvance) {
    Tech_type_id tech = advance_number(padvance);

    if (BV_ISSET(techs, tech)) {
      v2_add_research_unlock_row(tech, kind, native_id, name, scope);
    }
  } advance_re_active_iterate_end;
}

static void v2_add_research_graph_rows(const struct research *research)
{
  advance_re_active_iterate(padvance) {
    Tech_type_id tech = advance_number(padvance);
    Tech_type_id required_one = advance_required(tech, AR_ONE);
    Tech_type_id required_two = advance_required(tech, AR_TWO);
    Tech_type_id required_root = advance_required(tech, AR_ROOT);
    bool reachable = research_invention_reachable(research, tech);
    Tech_type_id next_step = reachable
                            ? research_goal_step(research, tech) : A_UNSET;
    int unknown_prerequisites = reachable
      ? research_goal_unknown_techs(research, tech) : -1;
    char name_value[AGENT_V2_ROW_MAX];

    if (unknown_prerequisites > 0
        && research_invention_state(research, tech) != TECH_KNOWN) {
      unknown_prerequisites--;
    }

    if (!v2_encode_row_value(advance_rule_name(padvance), name_value,
                             sizeof(name_value))) {
      v2_overflow = TRUE;
      continue;
    }
    v2_add_row(FC_AGENT_V2_ROW_RESEARCH_GRAPH,
               tech, name_value, reachable ? 1 : 0,
               next_step == A_UNSET ? -1 : next_step,
               unknown_prerequisites,
               reachable ? research_goal_bulbs_required(research, tech) : -1);
    if (required_one != A_NONE
        && valid_advance_by_number(required_one) != NULL) {
      v2_add_row(FC_AGENT_V2_ROW_RESEARCH_EDGE,
                 tech, required_one, "direct");
    }
    if (required_two != A_NONE && required_two != required_one
        && valid_advance_by_number(required_two) != NULL) {
      v2_add_row(FC_AGENT_V2_ROW_RESEARCH_EDGE,
                 tech, required_two, "direct");
    }
    if (required_root != A_NONE
        && valid_advance_by_number(required_root) != NULL) {
      v2_add_row(FC_AGENT_V2_ROW_RESEARCH_EDGE,
                 tech, required_root, "root");
    }
  } advance_re_active_iterate_end;

  unit_type_re_active_iterate(unit_type) {
    v2_add_unlock_rows_for_requirements(
      &unit_type->build_reqs, "unit", utype_number(unit_type),
      utype_rule_name(unit_type), "build");
  } unit_type_re_active_iterate_end;
  improvement_re_active_iterate(improvement) {
    v2_add_unlock_rows_for_requirements(
      &improvement->reqs, "building", improvement_number(improvement),
      improvement_rule_name(improvement), "build");
  } improvement_re_active_iterate_end;
  governments_re_active_iterate(government) {
    v2_add_unlock_rows_for_requirements(
      &government->reqs, "government", government_number(government),
      government_rule_name(government), "change");
  } governments_re_active_iterate_end;
  action_iterate(action_id) {
    struct action *action = action_by_number(action_id);
    bv_techs actor_techs;
    bv_techs target_techs;

    if (action == NULL || !action_is_in_use(action)
        || action_is_internal(action)) {
      continue;
    }
    BV_CLR_ALL(actor_techs);
    BV_CLR_ALL(target_techs);
    action_enabler_list_re_iterate(
      action_enablers_for_action(action_id), enabler) {
      v2_collect_positive_tech_requirements(&enabler->actor_reqs,
                                            &actor_techs);
      v2_collect_positive_tech_requirements(&enabler->target_reqs,
                                            &target_techs);
    } action_enabler_list_re_iterate_end;
    advance_re_active_iterate(padvance) {
      Tech_type_id tech = advance_number(padvance);
      bool actor = BV_ISSET(actor_techs, tech);
      bool target = BV_ISSET(target_techs, tech);

      if (actor || target) {
        v2_add_research_unlock_row(
          tech, "action", action_id, action_rule_name(action),
          actor && target ? "both" : (actor ? "actor" : "target"));
      }
    } advance_re_active_iterate_end;
  } action_iterate_end;
}

static bool v2_known_tech_summary(const struct research *research,
                                  int *known_count, uint64_t *known_digest,
                                  char *known_ids, size_t known_ids_size)
{
  size_t used = 0;

  *known_count = 0;
  *known_digest = UINT64_C(14695981039346656037);
  known_ids[0] = '\0';
  advance_re_active_iterate(padvance) {
    Tech_type_id tech = advance_number(padvance);

    if (research_invention_state(research, tech) == TECH_KNOWN) {
      int length = fc_snprintf(known_ids + used, known_ids_size - used,
                               "%s%d", *known_count > 0 ? "," : "", tech);

      if (length < 0 || (size_t) length >= known_ids_size - used) {
        return FALSE;
      }
      used += (size_t) length;
      (*known_count)++;
    }
  } advance_re_active_iterate_end;
  if (*known_count == 0) {
    fc_strlcpy(known_ids, "-", known_ids_size);
  }
  *known_digest = v2_hash_bytes(*known_digest, known_ids, strlen(known_ids));
  return TRUE;
}

static void v2_build_rows(
  const struct fc_agent_v2_phase_evidence *phase)
{
  /* FC_AGENT_V2_ROW_TOMBSTONE remains part of the legacy row grammar, but
   * public tombstones are synthesized by the bounded supervisor cache. */
  struct player *self = client_player();
  const char *phase_mode = fc_agent_v2_phase_mode_name(phase->mode);
  const char *topology = current_topo_has_flag(TF_HEX)
                         ? (current_topo_has_flag(TF_ISO)
                            ? "isometric_hex" : "hex")
                         : (current_topo_has_flag(TF_ISO)
                            ? "isometric_square" : "square");
  int known_tile_count = 0;

  v2_work_row_count = 0;
  v2_work_action_count = 0;
  v2_overflow = FALSE;
  if (!map_is_empty()) {
    whole_map_iterate(&wld.map, count_tile) {
      if (client_tile_get_known(count_tile) != TILE_UNKNOWN) {
        known_tile_count++;
      }
    } whole_map_iterate_end;
  }

  v2_add_row(FC_AGENT_V2_ROW_META, "running",
             phase->turn, phase->phase, phase_mode, phase->phase_count,
             phase->active_phase ? 1 : 0, phase->phase_ready ? 1 : 0,
             wld.map.xsize, wld.map.ysize, topology,
             current_wrap_has_flag(WRAP_X) ? 1 : 0,
             current_wrap_has_flag(WRAP_Y) ? 1 : 0,
             known_tile_count);

  if (self != NULL) {
    const struct government *government = government_of_player(self);
    const struct nation_type *nation = nation_of_player(self);
    const struct research *research = research_get(self);
    const char *researching = research != NULL
                              ? v2_research_choice_name(
                                research->researching)
                              : "Unavailable";
    const char *research_goal = research != NULL
                                ? v2_research_choice_name(
                                  research->tech_goal)
                                : "Unavailable";
    char self_reference[48];
    char player_value[AGENT_V2_ROW_MAX];
    char nation_value[AGENT_V2_ROW_MAX];
    char government_value[AGENT_V2_ROW_MAX];
    char research_value[AGENT_V2_ROW_MAX];
    char goal_value[AGENT_V2_ROW_MAX];

    v2_build_vote_rows(self);

    v2_entity_ref(AGENT_V2_ENTITY_PLAYER, player_number(self),
                  self_reference, sizeof(self_reference));
    if (v2_encode_row_value(player_name(self), player_value,
                            sizeof(player_value))
        && v2_encode_row_value(nation != NULL ? nation_rule_name(nation)
                                              : "none",
                               nation_value, sizeof(nation_value))
        && v2_encode_row_value(government != NULL
                               ? government_rule_name(government) : "none",
                               government_value, sizeof(government_value))) {
      v2_add_row(FC_AGENT_V2_ROW_PLAYER,
                 self_reference, player_value, nation_value,
                 government_value, self->economic.gold, self->economic.tax,
                 self->economic.science, self->economic.luxury,
                 phase->alive ? 1 : 0, phase->phase_done ? 1 : 0,
                 game.info.changable_tax ? 1 : 0,
                 v2_player_max_rate(self),
                 terrain_control.infrapoints ? 1 : 0,
                 self->economic.infra_points);
    }
    if (extra_count() > FC_AGENT_V2_MAX_INFRA_CHOICES) {
      v2_overflow = TRUE;
    } else {
      extra_type_iterate(pextra) {
        char extra_name[AGENT_V2_ROW_MAX];

        if (!v2_encode_row_value(extra_rule_name(pextra), extra_name,
                                 sizeof(extra_name))) {
          v2_overflow = TRUE;
          break;
        }
        v2_add_row(FC_AGENT_V2_ROW_INFRASTRUCTURE_EXTRA,
                   extra_number(pextra), extra_name, pextra->infracost,
                   pextra->build_time, pextra->build_time_factor);
      } extra_type_iterate_end;
    }
    if (government_count() < 1
        || government_count() > FC_AGENT_V2_MAX_GOVERNMENTS
        || government == NULL
        || game.government_during_revolution == NULL) {
      v2_overflow = TRUE;
    } else {
      const struct government *target = self->target_government;
      const struct government *during = game.government_during_revolution;
      const char *method = v2_revolution_method_name(game.info.revolentype);
      enum fc_agent_v2_government_status status =
        fc_agent_v2_government_status(
          government_number(government), v2_government_id(target),
          government_number(during), self->revolution_finishes,
          game.info.turn);
      const char *status_name = fc_agent_v2_government_status_name(status);
      char status_value[AGENT_V2_ROW_MAX];
      char method_value[AGENT_V2_ROW_MAX];
      int turns_remaining = MAX(0, self->revolution_finishes
                                   - game.info.turn);

      if (method == NULL || status_name == NULL
          || !v2_encode_row_value(status_name, status_value,
                                  sizeof(status_value))
          || !v2_encode_row_value(method, method_value,
                                  sizeof(method_value))) {
        v2_overflow = TRUE;
      } else {
        v2_add_row(FC_AGENT_V2_ROW_GOVERNANCE,
                   government_number(government),
                   v2_government_id(target), government_number(during),
                   status_value, self->revolution_finishes, turns_remaining,
                   method_value, v2_revolution_max_turns(),
                   untargeted_revolution_allowed() ? 1 : 0,
                   get_player_bonus(self, EFT_NO_ANARCHY) > 0 ? 1 : 0,
                   v2_government_revolution_available(self) ? 1 : 0,
                   government_count());
        governments_iterate(candidate) {
          char candidate_name[AGENT_V2_ROW_MAX];

          if (v2_encode_row_value(government_rule_name(candidate),
                                  candidate_name,
                                  sizeof(candidate_name))) {
            v2_add_row(FC_AGENT_V2_ROW_GOVERNMENT,
                       government_number(candidate), candidate_name,
                       candidate == government ? 1 : 0,
                       candidate == target ? 1 : 0,
                       candidate == during ? 1 : 0,
                       v2_government_change_available(self, candidate)
                         ? 1 : 0);
          }
        } governments_iterate_end;
      }
    }
    multipliers_re_active_iterate(pmul) {
      int id = multiplier_number(pmul);
      int choices = v2_multiplier_choice_count(pmul);
      char multiplier_name[AGENT_V2_ROW_MAX];

      if (id < 0 || id >= MAX_NUM_MULTIPLIERS || choices < 1
          || !v2_multiplier_value_valid(
               pmul, player_multiplier_value(self, pmul))
          || !v2_multiplier_value_valid(
               pmul, player_multiplier_target_value(self, pmul))
          || !v2_encode_row_value(multiplier_rule_name(pmul),
                                  multiplier_name,
                                  sizeof(multiplier_name))) {
        v2_overflow = TRUE;
      } else {
        v2_add_row(FC_AGENT_V2_ROW_MULTIPLIER,
                   id, multiplier_name,
                   player_multiplier_value(self, pmul),
                   player_multiplier_target_value(self, pmul),
                   pmul->start, pmul->stop, pmul->step,
                   pmul->minimum_turns, self->multipliers[id].changed,
                   multiplier_can_be_changed(pmul, self) ? 1 : 0,
                   choices);
      }
    } multipliers_re_active_iterate_end;
    {
      const struct player_spaceship *ship = &self->spaceship;
      const char *ship_state = v2_spaceship_state_name(ship->state);
      char ship_state_value[AGENT_V2_ROW_MAX];
      int placed = num_spaceship_structurals_placed(ship);
      int slot;

      if (ship_state == NULL
          || !v2_encode_row_value(ship_state, ship_state_value,
                                  sizeof(ship_state_value))) {
        v2_overflow = TRUE;
      } else {
        v2_add_row(FC_AGENT_V2_ROW_SPACESHIP,
                   ship_state_value, ship->structurals, placed,
                   ship->components, ship->fuel, ship->propulsion,
                   ship->modules, ship->habitation, ship->life_support,
                   ship->solar_panels, ship->launch_year,
                   ship->population, ship->mass,
                   v2_scaled_nonnegative(ship->support_rate, 1000.0),
                   v2_scaled_nonnegative(ship->energy_rate, 1000.0),
                   v2_scaled_nonnegative(ship->success_rate, 1000.0),
                   v2_scaled_nonnegative(ship->travel_time, 1000.0),
                   player_primary_capital(self) != NULL ? 1 : 0,
                   v2_spaceship_launch_available(self) ? 1 : 0);
        for (slot = 0; slot < NUM_SS_STRUCTURALS; slot++) {
          int required = slot == 0 ? -1 : structurals_info[slot].required;

          v2_add_row(FC_AGENT_V2_ROW_SPACESHIP_STRUCTURAL,
                     slot, structurals_info[slot].x,
                     structurals_info[slot].y, required,
                     BV_ISSET(ship->structure, slot) ? 1 : 0,
                     slot == 0 || BV_ISSET(ship->structure, required)
                       ? 1 : 0,
                     v2_spaceship_place_available(
                       ship, SSHIP_PLACE_STRUCTURAL, slot) ? 1 : 0);
        }
      }
    }
    if (research != NULL
        && v2_encode_row_value(researching, research_value,
                               sizeof(research_value))
        && v2_encode_row_value(research_goal, goal_value,
                               sizeof(goal_value))) {
      uint64_t choices_digest =
        fc_agent_v2_research_choices_digest_init();
      int choices_count = 0;
      int previous_choice_id = -1;
      char choices_digest_value[32];

      advance_re_active_iterate(padvance) {
        Tech_type_id tech = advance_number(padvance);
        enum tech_state state = research_invention_state(
          research, tech);

        if (state == TECH_KNOWN || research_invention_reachable(research,
                                                                tech)) {
          v2_add_research_choice(
            &choices_digest, &choices_count, &previous_choice_id,
            tech, advance_rule_name(padvance),
            state == TECH_KNOWN ? "known"
            : (state == TECH_PREREQS_KNOWN ? "available" : "reachable"),
            v2_research_can_target(research, tech),
            v2_research_can_goal(research, tech));
        }
      } advance_re_active_iterate_end;
      if (research_future_next(research)
          || research->researching == A_FUTURE
          || research->tech_goal == A_FUTURE) {
        v2_add_research_choice(
          &choices_digest, &choices_count, &previous_choice_id,
          A_FUTURE, "Future Tech", "future",
          v2_research_can_target(research, A_FUTURE),
          v2_research_can_goal(research, A_FUTURE));
      }
      v2_add_research_choice(
        &choices_digest, &choices_count, &previous_choice_id,
        A_UNSET, "Unset", "unset", FALSE, TRUE);
      fc_snprintf(choices_digest_value, sizeof(choices_digest_value),
                  "fnv1a64-%016llx",
                  (unsigned long long) choices_digest);
      v2_add_row(FC_AGENT_V2_ROW_RESEARCH,
                 research->techs_researched, research->future_tech,
                 research_value, research->researching,
                 goal_value, research->tech_goal,
                 research->bulbs_researched,
                 research->client.researching_cost,
                 research->client.total_bulbs_prod,
                 choices_count, choices_digest_value);
      v2_add_research_graph_rows(research);
    }

    if (client_state() >= C_S_PREPARING) {
      players_iterate(other) {
        if (other != self) {
          const struct player_diplstate *state =
            player_diplstate_get(self, other);
          const struct treaty *treaty = find_treaty(self, other);
          struct agent_v2_relation_state *relation;
          uint64_t clauses_digest;
          size_t clause_count;
          char other_reference[48];
          char state_value[AGENT_V2_ROW_MAX];
          char name_value[AGENT_V2_ROW_MAX];
          char relation_nation_value[AGENT_V2_ROW_MAX];
          char intel_level_value[AGENT_V2_ROW_MAX];
          char team_name_value[AGENT_V2_ROW_MAX];
          char controller_value[AGENT_V2_ROW_MAX];
          char other_government_value[AGENT_V2_ROW_MAX];
          char digest_value[32];
          bool embassy_intel = other->is_alive
                               && team_has_embassy(self->team, other);
          bool contact_intel = other->is_alive
                               && could_intel_with_player(self, other);
          const char *intel_level = embassy_intel ? "embassy"
                                    : (contact_intel ? "contact" : "none");
          const struct government *other_government = contact_intel
            ? government_of_player(other) : NULL;
          const enum dipl_reason cancel_reason =
            pplayer_can_cancel_treaty(self, other);
          const char *cancel_reason_value =
            v2_diplomacy_cancel_reason_name(cancel_reason);

          v2_entity_ref(AGENT_V2_ENTITY_PLAYER, player_number(other),
                        other_reference, sizeof(other_reference));
          relation = v2_relation_state(other, treaty != NULL);
          if (relation != NULL
              && v2_treaty_clause_summary(
                   treaty, &clause_count, &clauses_digest)
              && v2_encode_row_value(diplstate_type_name(state->type),
                                     state_value, sizeof(state_value))
              && v2_encode_row_value(player_name(other), name_value,
                                     sizeof(name_value))
              && v2_encode_row_value(
                   nation_of_player(other) != NULL
                     ? nation_rule_name(nation_of_player(other))
                     : "unselected",
                   relation_nation_value,
                   sizeof(relation_nation_value))
              && v2_encode_row_value(intel_level, intel_level_value,
                                     sizeof(intel_level_value))
              && v2_encode_row_value(
                   other->team != NULL ? team_rule_name(other->team) : "none",
                   team_name_value, sizeof(team_name_value))
              && v2_encode_row_value(is_ai(other) ? "ai" : "human",
                                     controller_value,
                                     sizeof(controller_value))
              && v2_encode_row_value(
                   other_government != NULL
                     ? government_rule_name(other_government) : "unknown",
                   other_government_value,
                   sizeof(other_government_value))
              && cancel_reason_value != NULL) {
            bool self_accepted = FALSE;
            bool other_accepted = FALSE;

            if (treaty != NULL) {
              self_accepted = treaty->plr0 == self
                              ? treaty->accept0 : treaty->accept1;
              other_accepted = treaty->plr0 == self
                               ? treaty->accept1 : treaty->accept0;
            }
            fc_snprintf(digest_value, sizeof(digest_value),
                        "fnv1a64-%016llx",
                        (unsigned long long) clauses_digest);
            v2_add_row(FC_AGENT_V2_ROW_DIPLOMACY,
                       other_reference, name_value, relation_nation_value,
                       state_value,
                       state->contact_turns_left, other->is_alive ? 1 : 0,
                       state->turns_left,
                       treaty == NULL && can_meet_with_player(other) ? 1 : 0,
                       treaty != NULL ? 1 : 0,
                       (unsigned long long) relation->meeting_generation,
                       self_accepted ? 1 : 0, other_accepted ? 1 : 0,
                       (int) clause_count, digest_value,
                       intel_level_value,
                       other->team != NULL ? team_number(other->team) : -1,
                       team_name_value,
                       self->team != NULL && other->team == self->team ? 1 : 0,
                       controller_value, other->is_connected ? 1 : 0,
                       contact_intel && other->score.game >= 0
                         ? other->score.game : -1,
                       contact_intel ? other->economic.gold : -1,
                       other_government_value,
                       player_has_real_embassy(self, other) ? 1 : 0,
                       player_has_real_embassy(other, self) ? 1 : 0,
                       gives_shared_vision(self, other) ? 1 : 0,
                       gives_shared_vision(other, self) ? 1 : 0,
                       gives_shared_tiles(self, other) ? 1 : 0,
                       gives_shared_tiles(other, self) ? 1 : 0,
                       cancel_reason == DIPL_OK ? 1 : 0,
                       cancel_reason_value);
            if (embassy_intel) {
              const struct research *other_research = research_get(other);
              const char *other_research_name = other_research != NULL
                ? v2_research_choice_name(other_research->researching)
                : "Unknown";
              char other_research_value[AGENT_V2_ROW_MAX];
              char known_ids_value[AGENT_V2_ROW_MAX];
              char known_digest_value[32];
              int known_count;
              uint64_t known_digest;

              if (other_research == NULL
                  || !v2_known_tech_summary(
                       other_research, &known_count, &known_digest,
                       known_ids_value, sizeof(known_ids_value))
                  || !v2_encode_row_value(
                       other_research_name, other_research_value,
                       sizeof(other_research_value))) {
                v2_overflow = TRUE;
              } else {
                fc_snprintf(known_digest_value, sizeof(known_digest_value),
                            "fnv1a64-%016llx",
                            (unsigned long long) known_digest);
                v2_add_row(FC_AGENT_V2_ROW_DIPLOMACY_INTEL,
                           other_reference,
                           other->economic.tax, other->economic.science,
                           other->economic.luxury, other->client.culture,
                           other_research->researching,
                           other_research_value,
                           other_research->bulbs_researched,
                           other_research->client.researching_cost,
                           known_count, known_digest_value, known_ids_value);
              }
            }
          }
        }
      } players_iterate_end;
    }
  }

  /* Unbounded tiles and entities are exported only through STATE_SCOPE.
   * OBS remains the small revision anchor and global strategic summary. */

  v2_build_chat_rows();
  v2_build_actions(self, phase);
  qsort(v2_work_rows, v2_work_row_count, sizeof(v2_work_rows[0]),
        v2_row_compare);
  qsort(v2_work_actions, v2_work_action_count,
        sizeof(v2_work_actions[0]), v2_action_compare);
}

static bool v2_pregame_team_command_name_safe(const char *name)
{
  const unsigned char *cursor = (const unsigned char *) name;

  if (cursor == NULL || *cursor == '\0') {
    return FALSE;
  }
  for (; *cursor != '\0'; cursor++) {
    if (*cursor == '"' || *cursor < 0x20 || *cursor == 0x7f) {
      return FALSE;
    }
  }
  return TRUE;
}

static bool v2_pregame_team_context(
  const struct player *self, int *current_team, int *current_team_members,
  int *first_unused_team, int *team_choices)
{
  struct team_slot *current_slot;
  bool self_is_member = FALSE;
  int current;
  int members;
  int first_unused = -1;
  int choices = 0;

  if (self == NULL || self->team == NULL || !team_slots_initialised()) {
    return FALSE;
  }
  current = team_number(self->team);
  current_slot = team_slot_by_number(current);
  members = player_list_size(team_members(self->team));
  if (current_slot == NULL || team_slot_get_team(current_slot) != self->team
      || members < 1) {
    return FALSE;
  }
  player_list_iterate(team_members(self->team), member) {
    if (member == self) {
      self_is_member = TRUE;
    }
  } player_list_iterate_end;
  if (!self_is_member) {
    return FALSE;
  }
  team_slots_iterate(tslot) {
    if (team_slot_is_used(tslot)) {
      choices++;
    } else if (members > 1 && first_unused < 0) {
      first_unused = team_slot_index(tslot);
      choices++;
    }
  } team_slots_iterate_end;
  if (choices < 1) {
    return FALSE;
  }
  if (current_team != NULL) {
    *current_team = current;
  }
  if (current_team_members != NULL) {
    *current_team_members = members;
  }
  if (first_unused_team != NULL) {
    *first_unused_team = first_unused;
  }
  if (team_choices != NULL) {
    *team_choices = choices;
  }
  return TRUE;
}

static bool v2_pregame_team_choice_still_allowed(
  const struct player *self, int desired_team)
{
  struct team_slot *desired_slot = team_slot_by_number(desired_team);
  int current_team;
  int current_team_members;
  int first_unused_team;

  return desired_slot != NULL
         && v2_pregame_team_context(
              self, &current_team, &current_team_members,
              &first_unused_team, NULL)
         && fc_agent_v2_pregame_team_choice_allowed(
              client_state() == C_S_PREPARING && game.info.is_new_game,
              !self->is_ready, current_team, current_team_members,
              desired_team, team_slot_is_used(desired_slot),
              first_unused_team);
}

static bool v2_add_pregame_team_state_rows(void)
{
  struct player *self = client_player();
  int current_team;
  int first_unused_team;

  if (!v2_pregame_team_context(
        self, &current_team, NULL,
        &first_unused_team, NULL)) {
    return FALSE;
  }
  team_slots_iterate(tslot) {
    struct team *team = team_slot_get_team(tslot);
    bool occupied = team_slot_is_used(tslot);
    int slot = team_slot_index(tslot);
    int member_count;
    int emitted_members = 0;
    char name_value[AGENT_V2_ROW_MAX];

    if (!occupied && slot != first_unused_team) {
      continue;
    }
    member_count = occupied ? player_list_size(team_members(team)) : 0;
    if (member_count < 0
        || !v2_encode_row_value(
             team_slot_name_translation(tslot), name_value,
             sizeof(name_value))) {
      return FALSE;
    }
    v2_state_add_row(FC_AGENT_V2_ROW_PREGAME_TEAM,
                     slot, name_value, slot == current_team ? 1 : 0,
                     occupied ? 1 : 0, member_count);
    if (!occupied) {
      continue;
    }
    /* Player-slot order is stable and makes membership rows canonical even
     * if a team's internal list was rebuilt in a different insertion order. */
    players_iterate(member) {
      char player_reference[48];
      char leader_value[AGENT_V2_ROW_MAX];

      if (member->team != team) {
        continue;
      }
      if (member->client.lifecycle_id == 0
          || !v2_encode_row_value(
               player_name(member), leader_value, sizeof(leader_value))) {
        return FALSE;
      }
      v2_entity_ref(AGENT_V2_ENTITY_PLAYER, player_number(member),
                    player_reference, sizeof(player_reference));
      v2_state_add_row(FC_AGENT_V2_ROW_PREGAME_TEAM_MEMBER,
                       slot, player_reference, leader_value);
      emitted_members++;
    } players_iterate_end;
    if (emitted_members != member_count) {
      return FALSE;
    }
  } team_slots_iterate_end;
  return !v2_overflow;
}

static bool v2_add_investigation_state_rows(const char *selector)
{
  const struct agent_v2_investigation_payload *payload =
    &v2_investigation.payload;
  struct city *city;
  struct universal production;
  const char *production_kind;
  char city_reference[48];
  char city_name[AGENT_V2_ROW_MAX];
  char production_name[AGENT_V2_ROW_MAX];
  int improvement_count = 0;

  if (!v2_investigation.valid || v2_investigation.consumed
      || v2_investigation.seat_epoch != v2_seat_epoch
      || v2_investigation.revision != v2_revision
      || strcmp(selector, v2_investigation.token) != 0
      || !v2_investigation_payload_exportable(payload)
      || (city = game_city_by_number(payload->city_id)) == NULL
      || city->client.lifecycle_id != payload->city_lifecycle_id
      || v2_existing_incarnation(AGENT_V2_ENTITY_CITY, city->id)
         != payload->city_incarnation
      || tile_index(city_tile(city)) != payload->tile) {
    return FALSE;
  }
  production = universal_by_number(
    payload->production_kind, payload->production_value);
  production_kind = v2_build_kind_name(production.kind);
  improvement_iterate(pimprove) {
    if (BV_ISSET(payload->improvements, improvement_index(pimprove))) {
      improvement_count++;
    }
  } improvement_iterate_end;
  v2_entity_ref(AGENT_V2_ENTITY_CITY, payload->city_id,
                city_reference, sizeof(city_reference));
  if (production_kind == NULL
      || !v2_encode_row_value(payload->name, city_name, sizeof(city_name))
      || !v2_encode_row_value(
           universal_rule_name(&production), production_name,
           sizeof(production_name))) {
    return FALSE;
  }
  v2_state_add_row(
    FC_AGENT_V2_ROW_INVESTIGATION,
    city_reference, (unsigned long long) payload->city_lifecycle_id,
    payload->tile, city_name, payload->size, production_kind,
    payload->production_value, production_name, payload->shield_stock,
    payload->shield_surplus, improvement_count, FEELING_LAST,
    payload->specialists_size);
  improvement_iterate(pimprove) {
    char improvement_name[AGENT_V2_ROW_MAX];

    if (!BV_ISSET(payload->improvements, improvement_index(pimprove))) {
      continue;
    }
    if (!v2_encode_row_value(
          improvement_rule_name(pimprove), improvement_name,
          sizeof(improvement_name))) {
      return FALSE;
    }
    v2_state_add_row(
      FC_AGENT_V2_ROW_INVESTIGATION_IMPROVEMENT,
      city_reference, improvement_number(pimprove), improvement_name);
  } improvement_iterate_end;
  for (int feeling = 0; feeling < FEELING_LAST; feeling++) {
    v2_state_add_row(
      FC_AGENT_V2_ROW_INVESTIGATION_CITIZENS,
      city_reference, feeling,
      payload->feelings[CITIZEN_HAPPY][feeling],
      payload->feelings[CITIZEN_CONTENT][feeling],
      payload->feelings[CITIZEN_UNHAPPY][feeling],
      payload->feelings[CITIZEN_ANGRY][feeling]);
  }
  for (int specialist = 0;
       specialist < payload->specialists_size; specialist++) {
    const struct specialist *pspecialist = specialist_by_number(specialist);
    char specialist_name[AGENT_V2_ROW_MAX];

    if (pspecialist == NULL
        || !v2_encode_row_value(
             specialist_rule_name(pspecialist), specialist_name,
             sizeof(specialist_name))) {
      return FALSE;
    }
    v2_state_add_row(
      FC_AGENT_V2_ROW_INVESTIGATION_SPECIALIST,
      city_reference, specialist, specialist_name,
      payload->specialists[specialist]);
  }
  return !v2_overflow;
}

static void v2_build_pregame_rows(void)
{
  /* These native catalog rows use the same canonical schema grammar as the
   * observation row below. */
  /* FC_AGENT_V2_ROW_PREGAME_NATION FC_AGENT_V2_ROW_PREGAME_STYLE
   * FC_AGENT_V2_ROW_PREGAME_TEAM FC_AGENT_V2_ROW_PREGAME_TEAM_MEMBER */
  struct player *self = client_player();
  const struct nation_type *nation;
  const struct nation_style *style;
  struct agent_v2_action *action;
  char self_reference[48];
  char leader_value[AGENT_V2_ROW_MAX];
  char nation_value[AGENT_V2_ROW_MAX];
  char style_value[AGENT_V2_ROW_MAX];
  const char *sex;
  int nation_choices = 0;
  int team_choices = 0;

  v2_work_row_count = 0;
  v2_work_action_count = 0;
  v2_overflow = FALSE;
  if (self == NULL || self->client.lifecycle_id == 0
      || !v2_pregame_team_context(
           self, NULL, NULL, NULL, &team_choices)) {
    v2_overflow = TRUE;
    return;
  }

  nations_iterate(candidate) {
    if (is_nation_pickable(candidate)
        && (candidate->player == NULL || candidate->player == self)) {
      nation_choices++;
    }
  } nations_iterate_end;

  nation = nation_of_player(self);
  style = self->style;
  sex = self->is_male ? "male" : "female";
  v2_entity_ref(AGENT_V2_ENTITY_PLAYER, player_number(self),
                self_reference, sizeof(self_reference));
  if (!v2_encode_row_value(player_name(self), leader_value,
                           sizeof(leader_value))
      || !v2_encode_row_value(nation != NULL ? nation_rule_name(nation)
                                             : "none",
                              nation_value, sizeof(nation_value))
      || !v2_encode_row_value(style != NULL ? style_rule_name(style)
                                            : "none",
                              style_value, sizeof(style_value))) {
    v2_overflow = TRUE;
    return;
  }
  v2_add_row(FC_AGENT_V2_ROW_META, "preparing", 0, 0, "concurrent",
             1, 0, 0, 1, 1, "square", 0, 0, 0);
  v2_add_row(FC_AGENT_V2_ROW_PREGAME, self_reference, leader_value,
             nation_value, sex, style_value, self->is_ready ? 1 : 0,
             nation_choices, style_count(), team_choices);
  v2_build_vote_rows(self);

  if (!self->is_ready) {
    if (v2_work_action_count >= AGENT_V2_MAX_ACTIONS) {
      v2_overflow = TRUE;
      return;
    }
    action = &v2_work_actions[v2_work_action_count++];
    v2_action_init(action);
    action->kind = AGENT_V2_ACTION_PREGAME_CONFIGURE;
    action->player_id = player_number(self);
    action->player_incarnation = v2_existing_incarnation(
      AGENT_V2_ENTITY_PLAYER, player_number(self));
    action->probability_kind = AGENT_V2_PROBABILITY_EXACT;
    action->probability_min = 200;
    action->probability_max = 200;

    if (game.info.is_new_game
        && v2_pregame_team_command_name_safe(player_name(self))) {
      if (v2_work_action_count >= AGENT_V2_MAX_ACTIONS) {
        v2_overflow = TRUE;
        return;
      }
      action = &v2_work_actions[v2_work_action_count++];
      v2_action_init(action);
      action->kind = AGENT_V2_ACTION_PREGAME_SET_TEAM;
      action->player_id = player_number(self);
      action->player_incarnation = v2_existing_incarnation(
        AGENT_V2_ENTITY_PLAYER, player_number(self));
      action->probability_kind = AGENT_V2_PROBABILITY_EXACT;
      action->probability_min = 200;
      action->probability_max = 200;
    }
  }

  if (v2_work_action_count >= AGENT_V2_MAX_ACTIONS) {
    v2_overflow = TRUE;
    return;
  }
  action = &v2_work_actions[v2_work_action_count++];
  v2_action_init(action);
  action->kind = AGENT_V2_ACTION_PREGAME_SET_READY;
  action->player_id = player_number(self);
  action->player_incarnation = v2_existing_incarnation(
    AGENT_V2_ENTITY_PLAYER, player_number(self));
  action->desired_acceptance = self->is_ready ? 0 : 1;
  action->probability_kind = AGENT_V2_PROBABILITY_EXACT;
  action->probability_min = 200;
  action->probability_max = 200;

  {
    struct agent_v2_action_buffer buffer = {
      .actions = v2_work_actions,
      .count = v2_work_action_count,
      .capacity = AGENT_V2_MAX_ACTIONS,
      .overflow = FALSE,
      .export_unknown_rows = FALSE,
      .unknown_exported = NULL
    };

    v2_build_vote_actions(self, &buffer);
    v2_work_action_count = buffer.count;
    if (buffer.overflow) {
      v2_overflow = TRUE;
    }
  }

  qsort(v2_work_rows, v2_work_row_count, sizeof(v2_work_rows[0]),
        v2_row_compare);
  qsort(v2_work_actions, v2_work_action_count,
        sizeof(v2_work_actions[0]), v2_action_compare);
}

static void v2_build_city_site_state_rows(const struct player *self)
{
  players_iterate(owner) {
    city_list_iterate(owner->cities, pcity) {
      enum known_type known;
      const char *visibility;
      char reference[48];
      char owner_reference[48];
      char name_value[AGENT_V2_ROW_MAX];

      if (!v2_city_site_known(pcity)) {
        continue;
      }
      known = client_tile_get_known(city_tile(pcity));
      visibility = city_owner(pcity) == self
                   ? "own"
                   : (known == TILE_KNOWN_SEEN ? "visible" : "known");
      v2_entity_ref(AGENT_V2_ENTITY_CITY, pcity->id,
                    reference, sizeof(reference));
      v2_entity_ref(AGENT_V2_ENTITY_PLAYER,
                    player_number(city_owner(pcity)),
                    owner_reference, sizeof(owner_reference));
      if (v2_encode_row_value(city_name_get(pcity), name_value,
                              sizeof(name_value))) {
        v2_state_add_row(FC_AGENT_V2_ROW_CITY_SITE,
                         reference, owner_reference, name_value,
                         tile_index(city_tile(pcity)), TILE_XY(city_tile(pcity)),
                         city_size_get(pcity), visibility);
      }
    } city_list_iterate_end;
  } players_iterate_end;
}

static void v2_build_city_state_rows(const struct player *self)
{
  city_list_iterate(self->cities, pcity) {
    char reference[48];
    char name_value[AGENT_V2_ROW_MAX];
    char production_name[AGENT_V2_ROW_MAX];
    char rally_digest_value[32];
    const char *production_kind = v2_build_kind_name(
      pcity->production.kind);
    int citizen_tile_count = 0;
    int build_choice_count = 0;
    int improvement_count = 0;
      int citizen_specialists = city_specialists(pcity);
      int citizen_workers = city_size_get(pcity) - citizen_specialists;
      int worklist_count = worklist_length(&pcity->worklist);
      const char *new_citizens = v2_new_citizens_name(pcity);
      bool can_buy = city_can_buy(pcity)
                     && pcity->client.buy_cost <= self->economic.gold;
      bool rally_active = v2_city_rally_active(pcity);
      struct cm_parameter governor_parameter;
      bool governor_enabled = cma_is_city_under_agent(
        pcity, &governor_parameter);
      int rally_order_count = rally_active
                              ? (int) pcity->rally_point.length : 0;
      uint64_t rally_digest = v2_city_rally_orders_digest(pcity);

      v2_entity_ref(AGENT_V2_ENTITY_CITY, pcity->id,
                  reference, sizeof(reference));
    city_tile_iterate(&wld.map, city_map_radius_sq_get(pcity),
                      city_tile(pcity), count_tile) {
      if (client_tile_get_known(count_tile) == TILE_KNOWN_SEEN
          || tile_worked(count_tile) == pcity
          || is_free_worked(pcity, count_tile)) {
        citizen_tile_count++;
      }
    } city_tile_iterate_end;
    improvement_iterate(pimprove) {
      struct universal target = {
        .kind = VUT_IMPROVEMENT,
        .value = { .building = pimprove }
      };

      if (can_city_build_later(&wld.map, pcity, &target)
          || v2_worklist_contains(&pcity->worklist, &target)) {
        build_choice_count++;
      }
      if (city_has_building(pcity, pimprove)) {
        improvement_count++;
      }
    } improvement_iterate_end;
    unit_type_iterate(putype) {
      struct universal target = {
        .kind = VUT_UTYPE,
        .value = { .utype = putype }
      };

      if (can_city_build_later(&wld.map, pcity, &target)
          || v2_worklist_contains(&pcity->worklist, &target)) {
        build_choice_count++;
      }
      } unit_type_iterate_end;
      if (!v2_production_supported(&pcity->production)
          || production_kind == NULL || new_citizens == NULL
          || worklist_count < 0 || worklist_count > MAX_LEN_WORKLIST
          || build_choice_count > FC_AGENT_V2_MAX_CITY_BUILD_CHOICES
          || (governor_enabled
              && !v2_cma_parameter_valid(&governor_parameter))
          || (pcity->rally_point.length > 0) != rally_active
          || !fc_agent_v2_rally_state_canonical(
               rally_active,
               rally_active && pcity->rally_point.persistent,
               rally_active && pcity->rally_point.vigilant,
             rally_order_count, rally_digest)) {
      v2_overflow = TRUE;
    } else if (v2_encode_row_value(city_name_get(pcity), name_value,
                                   sizeof(name_value))
               && v2_encode_row_value(
                 universal_rule_name(&pcity->production), production_name,
                 sizeof(production_name))) {
      v2_state_add_row(FC_AGENT_V2_ROW_CITY,
                       reference, name_value, tile_index(city_tile(pcity)),
                       TILE_XY(city_tile(pcity)), city_size_get(pcity),
                       pcity->surplus[O_FOOD], pcity->surplus[O_SHIELD],
                       pcity->surplus[O_TRADE], production_kind,
                       universal_number(&pcity->production), production_name,
                       pcity->shield_stock,
                       city_production_build_shield_cost(pcity),
                       pcity->client.buy_cost, can_buy ? 1 : 0,
                       city_can_change_build(pcity) ? 1 : 0,
                       citizen_tile_count, specialist_count(),
                       worklist_count, build_choice_count, improvement_count,
                       pcity->did_sell ? 1 : 0,
                       BV_ISSET(pcity->city_options, CITYO_DISBAND) ? 1 : 0,
                       new_citizens,
                       BV_ISSET(pcity->city_options, CITYO_SCIENCE_SPECIALISTS)
                       && BV_ISSET(pcity->city_options,
                                   CITYO_GOLD_SPECIALISTS) ? 1 : 0,
                       pcity->airlift, city_airlift_max(pcity),
                       governor_enabled ? 1 : 0,
                       pcity->feel[CITIZEN_HAPPY][FEELING_FINAL],
                       pcity->feel[CITIZEN_CONTENT][FEELING_FINAL],
                       pcity->feel[CITIZEN_UNHAPPY][FEELING_FINAL],
                       pcity->feel[CITIZEN_ANGRY][FEELING_FINAL],
                       citizen_workers, citizen_specialists,
                       pcity->food_stock,
                       city_granary_size(city_size_get(pcity)),
                       city_turns_to_grow(pcity), pcity->pollution,
                       pcity->citizen_base[O_FOOD], pcity->prod[O_FOOD],
                       pcity->surplus[O_FOOD],
                       pcity->usage[O_FOOD], pcity->waste[O_FOOD],
                       pcity->unhappy_penalty[O_FOOD],
                       pcity->citizen_base[O_SHIELD], pcity->prod[O_SHIELD],
                       pcity->surplus[O_SHIELD],
                       pcity->usage[O_SHIELD], pcity->waste[O_SHIELD],
                       pcity->unhappy_penalty[O_SHIELD],
                       pcity->citizen_base[O_TRADE], pcity->prod[O_TRADE],
                       pcity->surplus[O_TRADE],
                       pcity->usage[O_TRADE], pcity->waste[O_TRADE],
                       pcity->unhappy_penalty[O_TRADE],
                       pcity->citizen_base[O_GOLD], pcity->prod[O_GOLD],
                       pcity->surplus[O_GOLD],
                       pcity->usage[O_GOLD], pcity->waste[O_GOLD],
                       pcity->unhappy_penalty[O_GOLD],
                       pcity->citizen_base[O_LUXURY], pcity->prod[O_LUXURY],
                       pcity->surplus[O_LUXURY],
                       pcity->usage[O_LUXURY], pcity->waste[O_LUXURY],
                       pcity->unhappy_penalty[O_LUXURY],
                       pcity->citizen_base[O_SCIENCE], pcity->prod[O_SCIENCE],
                       pcity->surplus[O_SCIENCE],
                       pcity->usage[O_SCIENCE], pcity->waste[O_SCIENCE],
                       pcity->unhappy_penalty[O_SCIENCE]);
      fc_snprintf(rally_digest_value, sizeof(rally_digest_value),
                  "fnv1a64-%016llx",
                  (unsigned long long) rally_digest);
      v2_state_add_row(FC_AGENT_V2_ROW_CITY_RALLY,
                       reference, rally_active ? 1 : 0,
                       rally_active && pcity->rally_point.persistent ? 1 : 0,
                       rally_active && pcity->rally_point.vigilant ? 1 : 0,
                       rally_order_count, rally_digest_value);
      worker_task_list_iterate(pcity->task_reqs, ptask) {
        const char *task_activity = v2_worker_activity_name(ptask->act);
        const char *task_extra = ptask->tgt != NULL
                                 ? extra_rule_name(ptask->tgt) : "none";
        char task_extra_value[AGENT_V2_ROW_MAX];

        if (!worker_task_is_sane(ptask) || task_activity == NULL
            || ptask->ptile == NULL || ptask->want < 0
            || ptask->want > UINT16_MAX) {
          v2_overflow = TRUE;
          break;
        }
        if (v2_encode_row_value(task_extra, task_extra_value,
                                sizeof(task_extra_value))) {
          v2_state_add_row(
            FC_AGENT_V2_ROW_CITY_WORKER_TASK,
            reference, tile_index(ptask->ptile), task_activity,
            ptask->tgt != NULL ? extra_number(ptask->tgt) : EXTRA_NONE,
            task_extra_value, ptask->want);
        }
      } worker_task_list_iterate_end;
    }
  } city_list_iterate_end;
}

static void v2_build_unit_state_rows(const struct player *self)
{
  unit_list_iterate(self->units, punit) {
    char reference[48];
    char owner_reference[48];
    char home_reference[48];
    char type_value[AGENT_V2_ROW_MAX];
    char converted_type_value[AGENT_V2_ROW_MAX];
    char activity_value[AGENT_V2_ROW_MAX];
    char activity_target_value[AGENT_V2_ROW_MAX];
    char veteran_name_value[AGENT_V2_ROW_MAX];
    char transporter_reference[48];
    const struct unit *transporter = NULL;
    int occupied = -1;
    enum agent_v2_transport_state transport_state = v2_transport_state(
      self, punit, &transporter, &occupied);
    const char *transport_state_value = v2_transport_state_name(
      transport_state);
    const struct unit_type *unit_type = unit_type_get(punit);
    const struct unit_type *converted_type = unit_type != NULL
                                             ? unit_type->converted_to : NULL;
    const struct veteran_level *veteran_level = unit_type != NULL
      ? utype_veteran_level(unit_type, punit->veteran) : NULL;
    const char *activity = v2_worker_activity_name(punit->activity);
    int activity_target = punit->activity_target != NULL
                          ? extra_index(punit->activity_target) : EXTRA_NONE;
    const char *activity_target_name = punit->activity_target != NULL
                                       ? extra_rule_name(
                                         punit->activity_target) : "none";
    const char *controller = v2_unit_controller_name(
      punit->ssa_controller);
    char orders_digest[32];

    fc_snprintf(
      orders_digest, sizeof(orders_digest), "fnv1a64-%016llx",
      (unsigned long long) (punit->has_orders
        ? unit_orders_digest(punit->orders.length, punit->orders.list) : 0));

    v2_entity_ref(AGENT_V2_ENTITY_UNIT, punit->id,
                  reference, sizeof(reference));
    v2_entity_ref(AGENT_V2_ENTITY_PLAYER, player_number(self),
                  owner_reference, sizeof(owner_reference));
    if (punit->homecity != IDENTITY_NUMBER_ZERO) {
      struct city *home = player_city_by_number(self, punit->homecity);

      if (home == NULL) {
        v2_overflow = TRUE;
        fc_strlcpy(home_reference, "none", sizeof(home_reference));
      } else {
        v2_entity_ref(AGENT_V2_ENTITY_CITY, home->id,
                      home_reference, sizeof(home_reference));
      }
    } else {
      fc_strlcpy(home_reference, "none", sizeof(home_reference));
    }
    if (transport_state == AGENT_V2_TRANSPORT_TRANSPORTED
        && transporter != NULL) {
      v2_entity_ref(AGENT_V2_ENTITY_UNIT, transporter->id,
                    transporter_reference, sizeof(transporter_reference));
    } else {
      fc_strlcpy(transporter_reference, "none",
                 sizeof(transporter_reference));
    }
    if (activity == NULL || controller == NULL || unit_type == NULL
        || veteran_level == NULL || transport_state_value == NULL) {
      v2_overflow = TRUE;
    } else if (v2_encode_row_value(utype_rule_name(unit_type),
                                   type_value, sizeof(type_value))
               && v2_encode_row_value(
                 converted_type != NULL
                   ? utype_rule_name(converted_type) : "none",
                 converted_type_value, sizeof(converted_type_value))
               && v2_encode_row_value(activity, activity_value,
                                      sizeof(activity_value))
               && v2_encode_row_value(activity_target_name,
                                      activity_target_value,
                                      sizeof(activity_target_value))
               && v2_encode_row_value(
                 name_translation_get(&veteran_level->name),
                 veteran_name_value, sizeof(veteran_name_value))) {
      v2_state_add_row(FC_AGENT_V2_ROW_UNIT_OWN,
                       reference, owner_reference, utype_number(unit_type),
                       type_value, home_reference,
                       converted_type != NULL
                         ? utype_number(converted_type) : -1,
                       converted_type_value,
                       tile_index(unit_tile(punit)), TILE_XY(unit_tile(punit)),
                       punit->hp, punit->veteran, veteran_name_value,
                       utype_veteran_levels(unit_type),
                       veteran_level->power_fact, veteran_level->move_bonus,
                       punit->fuel, unit_type->hp, unit_type->fuel,
                       unit_type->move_rate, unit_type->attack_strength,
                       unit_type->defense_strength, unit_type->firepower,
                       unit_type->upkeep[O_FOOD],
                       unit_type->upkeep[O_SHIELD],
                       unit_type->upkeep[O_TRADE],
                       unit_type->upkeep[O_GOLD],
                       unit_type->upkeep[O_LUXURY],
                       unit_type->upkeep[O_SCIENCE],
                       punit->upkeep[O_FOOD], punit->upkeep[O_SHIELD],
                       punit->upkeep[O_TRADE], punit->upkeep[O_GOLD],
                       punit->upkeep[O_LUXURY], punit->upkeep[O_SCIENCE],
                       punit->moves_left, activity_value,
                       activity_target, activity_target_value,
                       punit->activity_count, transport_state_value,
                       transporter_reference, get_transporter_capacity(punit),
                       occupied, punit->paradropped ? 1 : 0,
                       unit_type->paratroopers_range, controller,
                       punit->has_orders ? 1 : 0,
                       punit->has_orders && punit->orders.repeat ? 1 : 0,
                       punit->has_orders && punit->orders.vigilant ? 1 : 0,
                       punit->has_orders ? punit->orders.length : 0,
                       orders_digest,
                       punit->has_orders && punit->goto_tile != NULL
                         ? tile_index(punit->goto_tile) : -1);
    }
  } unit_list_iterate_end;

  players_iterate(other) {
    if (other == self) {
      continue;
    }
    unit_list_iterate(other->units, punit) {
      char reference[48];
      char owner_reference[48];
      char type_value[AGENT_V2_ROW_MAX];
      char veteran_name_value[AGENT_V2_ROW_MAX];
      const struct unit_type *unit_type = unit_type_get(punit);
      const struct veteran_level *veteran_level = unit_type != NULL
        ? utype_veteran_level(unit_type, punit->veteran) : NULL;

      if (!can_player_see_unit(self, punit)) {
        continue;
      }
      v2_entity_ref(AGENT_V2_ENTITY_UNIT, punit->id,
                    reference, sizeof(reference));
      v2_entity_ref(AGENT_V2_ENTITY_PLAYER, player_number(other),
                    owner_reference, sizeof(owner_reference));
      if (unit_type != NULL && veteran_level != NULL
          && v2_encode_row_value(utype_rule_name(unit_type),
                                 type_value, sizeof(type_value))
          && v2_encode_row_value(
               name_translation_get(&veteran_level->name),
               veteran_name_value, sizeof(veteran_name_value))) {
        v2_state_add_row(FC_AGENT_V2_ROW_UNIT_VISIBLE,
                         reference, owner_reference,
                         utype_number(unit_type), type_value,
                         tile_index(unit_tile(punit)), TILE_XY(unit_tile(punit)),
                         punit->hp, punit->veteran, veteran_name_value,
                         utype_veteran_levels(unit_type),
                         veteran_level->power_fact,
                         veteran_level->move_bonus,
                         unit_type->hp, unit_type->fuel,
                         unit_type->move_rate, unit_type->attack_strength,
                         unit_type->defense_strength, unit_type->firepower,
                         unit_type->upkeep[O_FOOD],
                         unit_type->upkeep[O_SHIELD],
                         unit_type->upkeep[O_TRADE],
                         unit_type->upkeep[O_GOLD],
                         unit_type->upkeep[O_LUXURY],
                         unit_type->upkeep[O_SCIENCE]);
      }
    } unit_list_iterate_end;
  } players_iterate_end;
}

static bool v2_state_add_tile(const struct tile *ptile, bool local_detail)
{
  enum known_type known;
  const struct terrain *terrain;
  const struct extra_type *resource;
  const struct player *owner;
  char owner_reference[48];
  char terrain_value[AGENT_V2_ROW_MAX];
  char placing_value[AGENT_V2_ROW_MAX];
  char resource_value[AGENT_V2_ROW_MAX];
  char label_value[AGENT_V2_ROW_MAX];
  bool has_label;
  int index;
  int x;
  int y;

  if (ptile == NULL || map_is_empty()) {
    return FALSE;
  }
  index = tile_index(ptile);
  if (index < 0 || index >= map_num_tiles()
      || index_to_tile(&wld.map, index) != ptile) {
    return FALSE;
  }
  index_to_map_pos(&x, &y, index);
  if (!is_normal_map_pos(x, y)
      || map_pos_to_tile(&wld.map, x, y) != ptile) {
    return FALSE;
  }
  known = client_tile_get_known(ptile);
  if (known == TILE_UNKNOWN) {
    struct agent_v2_row row;

    if (!(local_detail
          ? fc_agent_v2_format_unknown_local_tile(
              row.text, sizeof(row.text), index, x, y)
          : fc_agent_v2_format_unknown_tile(
              row.text, sizeof(row.text), index, x, y))) {
      return FALSE;
    }
    v2_state_add_row("%s", row.text);
    return !v2_overflow;
  }
  terrain = tile_terrain(ptile);
  resource = tile_resource_is_valid(ptile) ? tile_resource(ptile) : NULL;
  owner = tile_owner(ptile);
  has_label = ptile->label != NULL && ptile->label[0] != '\0';
  if (owner != NULL) {
    v2_entity_ref(AGENT_V2_ENTITY_PLAYER, player_number(owner),
                  owner_reference, sizeof(owner_reference));
  } else {
    fc_strlcpy(owner_reference, "none", sizeof(owner_reference));
  }
  if (!v2_encode_row_value(
        terrain != NULL ? terrain_rule_name(terrain) : "unknown",
        terrain_value, sizeof(terrain_value))
      || !v2_encode_row_value(
           ptile->placing != NULL ? extra_rule_name(ptile->placing) : "none",
           placing_value, sizeof(placing_value))
      || !v2_encode_row_value(
           resource != NULL ? extra_rule_name(resource) : "none",
           resource_value, sizeof(resource_value))
      || !v2_encode_row_value(
           has_label ? ptile->label : "none",
           label_value, sizeof(label_value))) {
    return FALSE;
  }
  if (!local_detail) {
    v2_state_add_row(FC_AGENT_V2_ROW_TILE, index, x, y, (int) known,
                     terrain_value, owner_reference,
                     ptile->placing != NULL
                       ? extra_number(ptile->placing) : -1,
                     placing_value,
                     ptile->placing != NULL ? ptile->infra_turns : 0,
                     terrain != NULL ? terrain->placing_time : -1);
    return !v2_overflow;
  }
  v2_state_add_row(
    FC_AGENT_V2_ROW_TILE_LOCAL, index, x, y, (int) known,
    terrain_value, owner_reference,
    ptile->placing != NULL ? extra_number(ptile->placing) : -1,
    placing_value, ptile->placing != NULL ? ptile->infra_turns : 0,
    terrain != NULL ? terrain->placing_time : -1,
    resource != NULL ? extra_number(resource) : -1, resource_value,
    has_label ? 1 : 0, label_value,
    city_tile_output(NULL, ptile, FALSE, O_FOOD),
    city_tile_output(NULL, ptile, FALSE, O_SHIELD),
    city_tile_output(NULL, ptile, FALSE, O_TRADE));
  extra_type_iterate(pextra) {
    char extra_value[AGENT_V2_ROW_MAX];

    if (tile_has_extra(ptile, pextra)) {
      if (!v2_encode_row_value(extra_rule_name(pextra), extra_value,
                               sizeof(extra_value))) {
        return FALSE;
      }
      v2_state_add_row(FC_AGENT_V2_ROW_TILE_EXTRA, index,
                       extra_number(pextra), extra_value,
                       (unsigned int) pextra->causes);
    }
  } extra_type_iterate_end;
  return !v2_overflow;
}

/* The public known_tiles catalog deliberately stays compact.  Hash the
 * additional bounded-window/target facts separately so resource, cached
 * extras, labels, or generic-yield changes still advance the native revision
 * before any detailed scope is opened. */
static bool v2_hash_local_tile_telemetry(uint64_t *hash)
{
  static const char domain[] = "local-tile-telemetry-v1";

  if (hash == NULL) {
    return FALSE;
  }
  *hash = v2_hash_bytes(*hash, domain, sizeof(domain));
  whole_map_iterate(&wld.map, ptile) {
    enum known_type known = client_tile_get_known(ptile);
    int values[6];
    int end = -1;

    if (known == TILE_UNKNOWN) {
      continue;
    }
    values[0] = tile_index(ptile);
    values[1] = (int) known;
    values[2] = tile_resource_is_valid(ptile)
                ? extra_number(tile_resource(ptile)) : -1;
    values[3] = city_tile_output(NULL, ptile, FALSE, O_FOOD);
    values[4] = city_tile_output(NULL, ptile, FALSE, O_SHIELD);
    values[5] = city_tile_output(NULL, ptile, FALSE, O_TRADE);
    *hash = v2_hash_bytes(*hash, values, sizeof(values));
    if (ptile->label != NULL && ptile->label[0] != '\0') {
      *hash = v2_hash_bytes(*hash, ptile->label, strlen(ptile->label) + 1);
    } else {
      *hash = v2_hash_bytes(*hash, &end, sizeof(end));
    }
    extra_type_iterate(pextra) {
      if (tile_has_extra(ptile, pextra)) {
        int extra_values[2] = {
          extra_number(pextra), (int) pextra->causes
        };

        *hash = v2_hash_bytes(*hash, extra_values, sizeof(extra_values));
      }
    } extra_type_iterate_end;
    *hash = v2_hash_bytes(*hash, &end, sizeof(end));
  } whole_map_iterate_end;
  return TRUE;
}

static int v2_compare_tile_indices(const void *left, const void *right)
{
  int a = *(const int *) left;
  int b = *(const int *) right;

  return a < b ? -1 : a > b ? 1 : 0;
}

static bool v2_build_state_scope_rows(
  const char *section, const char *selector, bool capture)
{
  struct city *pcity = NULL;
  struct tile *center = NULL;
  struct player *counterpart = NULL;
  struct treaty *treaty = NULL;
  struct agent_v2_relation_state *relation = NULL;
  bool actor_target_tiles = FALSE;
  size_t actor_action_count = 0;
  bool actor_action_overflow = FALSE;
  size_t parsed_center;
  size_t radius;
  char canonical_selector[64];

  FC_FREE(v2_state_scope_rows);
  v2_state_scope_row_capacity = 0;
  v2_state_scope_total = 0;
  v2_state_scope_bytes = 0;
  v2_state_scope_capture = capture;
  v2_state_scope_digest = UINT64_C(1469598103934665603);
  v2_state_scope_digest = v2_hash_bytes(
    v2_state_scope_digest, section, strlen(section) + 1);
  v2_state_scope_digest = v2_hash_bytes(
    v2_state_scope_digest, selector, strlen(selector) + 1);
  v2_overflow = FALSE;

  if (strcmp(section, "investigation") == 0) {
    return v2_add_investigation_state_rows(selector);
  }

  if (strcmp(section, "pregame_nations") == 0
      || strcmp(section, "pregame_styles") == 0
      || strcmp(section, "pregame_teams") == 0) {
    if (client_state() != C_S_PREPARING || strcmp(selector, "-") != 0) {
      return FALSE;
    }
  } else if (strcmp(section, "known_tiles") == 0
      || strcmp(section, "map_tiles") == 0
      || strcmp(section, "cities") == 0
      || strcmp(section, "units") == 0
      || strcmp(section, "city_sites") == 0) {
    if (strcmp(selector, "-") != 0) {
      return FALSE;
    }
  } else if (strcmp(section, "tile_window") == 0) {
    char trailing;

    if (sscanf(selector, "t%zu-r%zu%c",
               &parsed_center, &radius, &trailing) != 2
        || parsed_center > INT_MAX || radius > 8) {
      return FALSE;
    }
    fc_snprintf(canonical_selector, sizeof(canonical_selector),
                "t%zu-r%zu", parsed_center, radius);
    center = index_to_tile(&wld.map, (int) parsed_center);
    if (strcmp(selector, canonical_selector) != 0 || center == NULL
        || client_tile_get_known(center) == TILE_UNKNOWN) {
      return FALSE;
    }
  } else if (strcmp(section, "diplomacy_clauses") == 0) {
    char prefix;
    int counterpart_id;
    uint64_t incarnation;

    if (!fc_agent_v2_parse_entity_ref(
          selector, &prefix, &counterpart_id, &incarnation)
        || prefix != 'p'
        || (counterpart = player_by_number(counterpart_id)) == NULL
        || counterpart == client_player()
        || incarnation != v2_existing_incarnation(
             AGENT_V2_ENTITY_PLAYER, counterpart_id)) {
      return FALSE;
    }
    treaty = find_treaty(client_player(), counterpart);
    relation = v2_relation_state(counterpart, treaty != NULL);
    if (relation == NULL) {
      return FALSE;
    }
  } else if (strcmp(section, "target_tiles") == 0) {
    enum agent_v2_entity_kind kind;
    int actor_id;
    uint64_t incarnation;

    if (!v2_resolve_owned_actor(
          selector, &kind, &actor_id, &incarnation)
        || kind != AGENT_V2_ENTITY_UNIT
        || !v2_build_actor_scope(
             selector, v2_scope_actions, &actor_action_count,
             &actor_action_overflow)
        || actor_action_overflow) {
      return FALSE;
    }
    actor_target_tiles = TRUE;
  } else {
    enum agent_v2_entity_kind kind;
    int city_id;
    uint64_t incarnation;

    if (strcmp(section, "city_citizens") != 0
        && strcmp(section, "city_build_choices") != 0
        && strcmp(section, "city_worklist") != 0
        && strcmp(section, "city_improvements") != 0
        && strcmp(section, "city_governor") != 0) {
      return FALSE;
    }
    if (!v2_resolve_owned_actor(selector, &kind, &city_id, &incarnation)
        || kind != AGENT_V2_ENTITY_CITY
        || (pcity = player_city_by_number(client_player(), city_id)) == NULL) {
      return FALSE;
    }
  }

  if (strcmp(section, "pregame_nations") == 0) {
    struct player *self = client_player();

    nations_iterate(candidate) {
      char name_value[AGENT_V2_ROW_MAX];

      if (!is_nation_pickable(candidate)
          || (candidate->player != NULL && candidate->player != self)) {
        continue;
      }
      if (v2_encode_row_value(nation_rule_name(candidate), name_value,
                              sizeof(name_value))) {
        v2_state_add_row(FC_AGENT_V2_ROW_PREGAME_NATION,
                         nation_number(candidate), name_value,
                         style_number(candidate->style));
      }
    } nations_iterate_end;
    return !v2_overflow;
  }
  if (strcmp(section, "pregame_styles") == 0) {
    styles_iterate(candidate) {
      char name_value[AGENT_V2_ROW_MAX];

      if (v2_encode_row_value(style_rule_name(candidate), name_value,
                              sizeof(name_value))) {
        v2_state_add_row(FC_AGENT_V2_ROW_PREGAME_STYLE,
                         style_number(candidate), name_value);
      }
    } styles_iterate_end;
    return !v2_overflow;
  }
  if (strcmp(section, "pregame_teams") == 0) {
    return v2_add_pregame_team_state_rows();
  }

  if (strcmp(section, "known_tiles") == 0
      || strcmp(section, "map_tiles") == 0
      || strcmp(section, "tile_window") == 0) {
    whole_map_iterate(&wld.map, ptile) {
      enum known_type known = client_tile_get_known(ptile);

      if (strcmp(section, "known_tiles") == 0 && known == TILE_UNKNOWN) {
        continue;
      }
      if (center != NULL && real_map_distance(center, ptile) > (int) radius) {
        continue;
      }
      if (!v2_state_add_tile(
            ptile, strcmp(section, "tile_window") == 0)) {
        v2_overflow = TRUE;
        break;
      }
    } whole_map_iterate_end;
    return !v2_overflow;
  }

  if (actor_target_tiles) {
    int targets[AGENT_V2_MAX_ACTIONS];
    size_t target_count = 0;

    for (size_t i = 0; i < actor_action_count; i++) {
      if (v2_scope_actions[i].target_tile >= 0) {
        targets[target_count++] = v2_scope_actions[i].target_tile;
      }
    }
    qsort(targets, target_count, sizeof(targets[0]),
          v2_compare_tile_indices);
    for (size_t i = 0; i < target_count; i++) {
      struct tile *target;

      if (i > 0 && targets[i] == targets[i - 1]) {
        continue;
      }
      target = index_to_tile(&wld.map, targets[i]);
      if (target == NULL || !v2_state_add_tile(target, TRUE)) {
        v2_overflow = TRUE;
        break;
      }
    }
    return !v2_overflow;
  }

  if (strcmp(section, "cities") == 0
      || strcmp(section, "units") == 0
      || strcmp(section, "city_sites") == 0) {
    struct player *self = client_player();

    if (self == NULL) {
      return FALSE;
    }
    if (strcmp(section, "cities") == 0) {
      v2_build_city_state_rows(self);
    } else if (strcmp(section, "units") == 0) {
      v2_build_unit_state_rows(self);
    } else {
      v2_build_city_site_state_rows(self);
    }
    return !v2_overflow;
  }

  if (strcmp(section, "diplomacy_clauses") == 0) {
    struct agent_v2_clause_key *keys = NULL;
    size_t clause_count = 0;

    if (!v2_treaty_clause_keys(treaty, &keys, &clause_count)) {
      return FALSE;
    }
    for (size_t position = 0; position < clause_count; position++) {
      if (!v2_add_diplomacy_clause_state_row(
            counterpart, relation->meeting_generation, position,
            keys[position].clause)) {
        FC_FREE(keys);
        return FALSE;
      }
    }
    FC_FREE(keys);
    return !v2_overflow;
  }

  {
    char reference[48];

    v2_entity_ref(AGENT_V2_ENTITY_CITY, pcity->id,
                  reference, sizeof(reference));
    if (strcmp(section, "city_governor") == 0) {
      struct cm_parameter parameter;

      if (cma_is_city_under_agent(pcity, &parameter)) {
        if (!v2_cma_parameter_valid(&parameter)) {
          v2_overflow = TRUE;
        } else {
          v2_state_add_row(
            FC_AGENT_V2_ROW_CITY_GOVERNOR, reference,
            parameter.minimal_surplus[O_FOOD],
            parameter.minimal_surplus[O_SHIELD],
            parameter.minimal_surplus[O_TRADE],
            parameter.minimal_surplus[O_GOLD],
            parameter.minimal_surplus[O_LUXURY],
            parameter.minimal_surplus[O_SCIENCE],
            parameter.factor[O_FOOD], parameter.factor[O_SHIELD],
            parameter.factor[O_TRADE], parameter.factor[O_GOLD],
            parameter.factor[O_LUXURY], parameter.factor[O_SCIENCE],
            parameter.happy_factor, parameter.require_happy ? 1 : 0,
            parameter.max_growth ? 1 : 0);
        }
      }
    } else if (strcmp(section, "city_citizens") == 0) {
      city_tile_iterate(&wld.map, city_map_radius_sq_get(pcity),
                        city_tile(pcity), citizen_tile) {
        if (client_tile_get_known(citizen_tile) != TILE_KNOWN_SEEN
            && tile_worked(citizen_tile) != pcity
            && !is_free_worked(pcity, citizen_tile)) {
          continue;
        }
        v2_state_add_row(FC_AGENT_V2_ROW_CITY_TILE,
                         reference, tile_index(citizen_tile),
                         tile_worked(citizen_tile) == pcity ? 1 : 0,
                         is_free_worked(pcity, citizen_tile) ? 1 : 0,
                         city_can_work_tile(pcity, citizen_tile) ? 1 : 0,
                         city_tile_output(
                           pcity, citizen_tile,
                           base_city_celebrating(pcity), O_FOOD),
                         city_tile_output(
                           pcity, citizen_tile,
                           base_city_celebrating(pcity), O_SHIELD),
                         city_tile_output(
                           pcity, citizen_tile,
                           base_city_celebrating(pcity), O_TRADE),
                         city_tile_output(
                           pcity, citizen_tile,
                           base_city_celebrating(pcity), O_GOLD),
                         city_tile_output(
                           pcity, citizen_tile,
                           base_city_celebrating(pcity), O_LUXURY),
                         city_tile_output(
                           pcity, citizen_tile,
                           base_city_celebrating(pcity), O_SCIENCE));
      } city_tile_iterate_end;
      specialist_type_iterate(specialist) {
        char specialist_name[AGENT_V2_ROW_MAX];

        if (v2_encode_row_value(
              specialist_rule_name(specialist_by_number(specialist)),
              specialist_name, sizeof(specialist_name))) {
          v2_state_add_row(FC_AGENT_V2_ROW_CITY_SPECIALIST,
                           reference, specialist, specialist_name,
                           pcity->specialists[specialist],
                           specialist < normal_specialist_count() ? 1 : 0,
                           specialist < normal_specialist_count()
                           && city_can_use_specialist(pcity, specialist)
                             ? 1 : 0,
                           specialist == DEFAULT_SPECIALIST ? 1 : 0,
                           get_specialist_output(
                             pcity, specialist, O_FOOD),
                           get_specialist_output(
                             pcity, specialist, O_SHIELD),
                           get_specialist_output(
                             pcity, specialist, O_TRADE),
                           get_specialist_output(
                             pcity, specialist, O_GOLD),
                           get_specialist_output(
                             pcity, specialist, O_LUXURY),
                           get_specialist_output(
                             pcity, specialist, O_SCIENCE));
        }
      } specialist_type_iterate_end;
    } else if (strcmp(section, "city_worklist") == 0) {
      int worklist_count = worklist_length(&pcity->worklist);

      for (int position = 0; position < worklist_count; position++) {
        struct universal target;
        const char *kind;
        char target_name[AGENT_V2_ROW_MAX];

        if (!worklist_peek_ith(&pcity->worklist, &target, position)
            || !v2_production_supported(&target)
            || (kind = v2_build_kind_name(target.kind)) == NULL
            || !v2_encode_row_value(universal_rule_name(&target),
                                    target_name, sizeof(target_name))) {
          v2_overflow = TRUE;
          break;
        }
        v2_state_add_row(FC_AGENT_V2_ROW_CITY_WORKLIST,
                         reference, position, kind,
                         universal_number(&target), target_name);
      }
    } else if (strcmp(section, "city_improvements") == 0) {
      improvement_iterate(pimprove) {
        char target_name[AGENT_V2_ROW_MAX];

        if (!city_has_building(pcity, pimprove)) {
          continue;
        }
        if (!v2_encode_row_value(improvement_rule_name(pimprove),
                                 target_name, sizeof(target_name))) {
          v2_overflow = TRUE;
          break;
        }
        v2_state_add_row(FC_AGENT_V2_ROW_CITY_IMPROVEMENT,
                         reference, improvement_number(pimprove), target_name,
                         !pcity->did_sell
                         && can_city_sell_building(pcity, pimprove) ? 1 : 0,
                         impr_sell_gold(pimprove));
      } improvement_iterate_end;
    } else {
      improvement_iterate(pimprove) {
        struct universal target = {
          .kind = VUT_IMPROVEMENT,
          .value = { .building = pimprove }
        };
        char target_name[AGENT_V2_ROW_MAX];

        if ((can_city_build_later(&wld.map, pcity, &target)
             || v2_worklist_contains(&pcity->worklist, &target))
            && v2_encode_row_value(universal_rule_name(&target),
                                   target_name, sizeof(target_name))) {
          v2_state_add_row(FC_AGENT_V2_ROW_CITY_BUILD_CHOICE,
                           reference, "improvement",
                           improvement_number(pimprove), target_name,
                           can_city_build_later(&wld.map, pcity, &target)
                             ? 1 : 0,
                           can_city_build_now(&wld.map, pcity, &target,
                                              RPT_CERTAIN) ? 1 : 0);
        }
      } improvement_iterate_end;
      unit_type_iterate(putype) {
        struct universal target = {
          .kind = VUT_UTYPE,
          .value = { .utype = putype }
        };
        char target_name[AGENT_V2_ROW_MAX];

        if ((can_city_build_later(&wld.map, pcity, &target)
             || v2_worklist_contains(&pcity->worklist, &target))
            && v2_encode_row_value(universal_rule_name(&target),
                                   target_name, sizeof(target_name))) {
          v2_state_add_row(FC_AGENT_V2_ROW_CITY_BUILD_CHOICE,
                           reference, "unit", utype_number(putype),
                           target_name,
                           can_city_build_later(&wld.map, pcity, &target)
                             ? 1 : 0,
                           can_city_build_now(&wld.map, pcity, &target,
                                              RPT_CERTAIN) ? 1 : 0);
        }
      } unit_type_iterate_end;
    }
  }
  return !v2_overflow;
}

static bool v2_hash_state_catalog(
  uint64_t *hash, const char *section, const char *selector)
{
  if (!v2_build_state_scope_rows(section, selector, FALSE)) {
    return FALSE;
  }
  *hash = v2_hash_bytes(*hash, section, strlen(section) + 1);
  *hash = v2_hash_bytes(*hash, selector, strlen(selector) + 1);
  *hash = v2_hash_bytes(
    *hash, &v2_state_scope_total, sizeof(v2_state_scope_total));
  *hash = v2_hash_bytes(
    *hash, &v2_state_scope_digest, sizeof(v2_state_scope_digest));
  return TRUE;
}

static bool v2_hash_state_catalogs(uint64_t *hash)
{
  static const char *city_sections[] = {
    "city_citizens", "city_build_choices", "city_worklist",
    "city_improvements", "city_governor"
  };
  struct player *self = client_player();

  if (client_state() == C_S_PREPARING) {
    return v2_hash_state_catalog(hash, "pregame_nations", "-")
           && v2_hash_state_catalog(hash, "pregame_styles", "-")
           && v2_hash_state_catalog(hash, "pregame_teams", "-");
  }

  if (!v2_hash_state_catalog(hash, "known_tiles", "-")
      || !v2_hash_local_tile_telemetry(hash)
      || !v2_hash_state_catalog(hash, "cities", "-")
      || !v2_hash_state_catalog(hash, "units", "-")
      || !v2_hash_state_catalog(hash, "city_sites", "-")) {
    return FALSE;
  }
  if (self == NULL) {
    return TRUE;
  }
  city_list_iterate(self->cities, pcity) {
    char reference[48];

    v2_entity_ref(AGENT_V2_ENTITY_CITY, pcity->id,
                  reference, sizeof(reference));
    for (size_t i = 0; i < ARRAY_SIZE(city_sections); i++) {
      if (!v2_hash_state_catalog(hash, city_sections[i], reference)) {
        return FALSE;
      }
    }
  } city_list_iterate_end;
  /* Each relation summary already hashes its canonical clause count and
   * digest. The potentially large clause rows are pinned and bounded only
   * when that exact relation scope is opened. */
  return TRUE;
}

static bool v2_action_equal(const struct agent_v2_action *a,
                            const struct agent_v2_action *b)
{
  return a->kind == b->kind && a->unit_id == b->unit_id
         && a->player_id == b->player_id
         && a->player_incarnation == b->player_incarnation
         && a->unit_incarnation == b->unit_incarnation
         && a->unit_lifecycle_id == b->unit_lifecycle_id
         && a->city_id == b->city_id
         && a->city_incarnation == b->city_incarnation
         && a->city_lifecycle_id == b->city_lifecycle_id
         && a->source_unit_tile == b->source_unit_tile
         && a->source_unit_moves == b->source_unit_moves
         && a->source_unit_paradropped == b->source_unit_paradropped
         && a->special_target_known_seen == b->special_target_known_seen
         && a->special_target_city_id == b->special_target_city_id
         && a->special_target_city_incarnation
            == b->special_target_city_incarnation
         && a->special_target_city_lifecycle_id
            == b->special_target_city_lifecycle_id
         && a->special_target_city_owner == b->special_target_city_owner
         && a->special_target_extra_owner == b->special_target_extra_owner
         && BV_ARE_EQUAL(a->special_target_extras,
                         b->special_target_extras)
         && BV_ARE_EQUAL(a->special_target_hut_extras,
                         b->special_target_hut_extras)
         && a->target_tile == b->target_tile
         && a->goto_destination_tile == b->goto_destination_tile
         && a->goto_order_count == b->goto_order_count
         && a->goto_orders_digest == b->goto_orders_digest
         && a->goto_route_signature == b->goto_route_signature
         && a->goto_action_move == b->goto_action_move
         && a->route_waypoint_limit == b->route_waypoint_limit
         && a->rally_source_tile == b->rally_source_tile
         && a->rally_production_unit_type
            == b->rally_production_unit_type
         && a->rally_veteran_level == b->rally_veteran_level
         && a->rally_order_count == b->rally_order_count
         && a->rally_orders_digest == b->rally_orders_digest
         && a->rally_action_move == b->rally_action_move
         && a->source_city_id == b->source_city_id
         && a->source_city_incarnation == b->source_city_incarnation
         && a->source_city_lifecycle_id == b->source_city_lifecycle_id
         && a->source_city_tile == b->source_city_tile
         && a->destination_city_id == b->destination_city_id
         && a->destination_city_incarnation
            == b->destination_city_incarnation
         && a->destination_city_lifecycle_id
            == b->destination_city_lifecycle_id
         && a->destination_city_tile == b->destination_city_tile
         && a->target_unit_id == b->target_unit_id
         && a->target_unit_incarnation == b->target_unit_incarnation
         && a->target_unit_lifecycle_id == b->target_unit_lifecycle_id
         && a->transport_context_id == b->transport_context_id
         && a->transport_context_incarnation
            == b->transport_context_incarnation
         && a->transport_context_lifecycle_id
            == b->transport_context_lifecycle_id
         && a->transport_before_signature == b->transport_before_signature
         && a->transport_after_signature == b->transport_after_signature
         && a->target_tech == b->target_tech
         && a->target_research_digest == b->target_research_digest
         && a->target_stack_signature == b->target_stack_signature
         && a->vote_no == b->vote_no
         && a->vote_signature == b->vote_signature
         && a->target_government == b->target_government
         && a->target_build_kind == b->target_build_kind
         && a->target_build_id == b->target_build_id
         && a->target_building_catalog_request_id
            == b->target_building_catalog_request_id
         && a->target_building_catalog_revision
            == b->target_building_catalog_revision
         && a->target_building_catalog_digest
            == b->target_building_catalog_digest
         && a->spaceship_part == b->spaceship_part
         && a->spaceship_value == b->spaceship_value
         && a->target_multiplier == b->target_multiplier
         && a->multiplier_value == b->multiplier_value
         && a->source_specialist == b->source_specialist
         && a->target_specialist == b->target_specialist
         && a->worker_task_baseline_present
            == b->worker_task_baseline_present
         && a->worker_task_baseline_activity
            == b->worker_task_baseline_activity
         && a->worker_task_baseline_extra
            == b->worker_task_baseline_extra
         && a->worker_task_baseline_want
            == b->worker_task_baseline_want
         && a->target_extra == b->target_extra
         && a->infrastructure_cost == b->infrastructure_cost
         && a->infrastructure_turns == b->infrastructure_turns
         && a->infrastructure_choice_count
            == b->infrastructure_choice_count
         && strcmp(a->infrastructure_choices,
                   b->infrastructure_choices) == 0
         && a->target_activity == b->target_activity
         && a->counterpart_id == b->counterpart_id
         && a->counterpart_incarnation == b->counterpart_incarnation
         && a->meeting_generation == b->meeting_generation
         && a->clauses_digest == b->clauses_digest
         && a->self_accepted == b->self_accepted
         && a->other_accepted == b->other_accepted
         && a->relation_state == b->relation_state
         && a->outgoing_vision == b->outgoing_vision
         && a->outgoing_shared_tiles == b->outgoing_shared_tiles
         && a->clause_giver_id == b->clause_giver_id
         && a->clause_type == b->clause_type
         && a->clause_value == b->clause_value
         && a->desired_acceptance == b->desired_acceptance
         && a->max_rate == b->max_rate && a->action == b->action
         && a->gold_cost == b->gold_cost
         && a->probability_kind == b->probability_kind
         && a->probability_min == b->probability_min
         && a->probability_max == b->probability_max;
}

static bool v2_current_equal(void)
{
  size_t i;

  if (!v2_have_current
      || v2_current_row_count != v2_work_row_count
      || v2_current_action_count != v2_work_action_count) {
    return FALSE;
  }
  for (i = 0; i < v2_work_row_count; i++) {
    if (strcmp(v2_current_rows[i].text, v2_work_rows[i].text) != 0) {
      return FALSE;
    }
  }
  for (i = 0; i < v2_work_action_count; i++) {
    const struct agent_v2_action *a = &v2_current_actions[i];
    const struct agent_v2_action *b = &v2_work_actions[i];

    if (!v2_action_equal(a, b)) {
      return FALSE;
    }
  }
  return TRUE;
}

static void v2_assign_slot(struct agent_v2_action *action)
{
  fc_agent_v2_make_slot(action->slot, sizeof(action->slot),
                        v2_secret, v2_revision, action,
                        offsetof(struct agent_v2_action, slot));
}

static bool v2_assign_target_slot(struct agent_v2_action *action,
                                  int native_tile)
{
  return native_tile >= 0
         && fc_agent_v2_make_target_slot(
              action->slot, sizeof(action->slot), v2_secret, v2_revision,
              (uint32_t) native_tile, action,
              offsetof(struct agent_v2_action, slot));
}

static bool v2_refresh(void)
{
  struct fc_agent_v2_phase_evidence phase;
  uint64_t hash = UINT64_C(1469598103934665603);
  bool running;
  bool phase_changed;
  size_t i;

  if (!v2_seat_authorized || !v2_cache_coherent()) {
    return FALSE;
  }
  running = client_state() == C_S_RUNNING;
  if (client_state() == C_S_PREPARING) {
    memset(&phase, 0, sizeof(phase));
    phase.mode = FC_AGENT_V2_PHASE_CONCURRENT;
    phase.phase_count = 1;
    v2_build_pregame_rows();
  } else if (running
             && v2_collect_phase_evidence(client_player(), &phase)) {
    v2_build_rows(&phase);
  } else {
    return FALSE;
  }
  if (v2_overflow
      || v2_work_row_count + v2_work_action_count > AGENT_V2_MAX_ROWS) {
    return FALSE;
  }
  for (i = 0; i < v2_work_row_count; i++) {
    hash = v2_hash_bytes(hash, v2_work_rows[i].text,
                         strlen(v2_work_rows[i].text) + 1);
  }
  for (i = 0; i < v2_work_action_count; i++) {
    hash = v2_hash_bytes(hash, &v2_work_actions[i],
                         offsetof(struct agent_v2_action, slot));
  }
  if (running) {
    hash = v2_hash_bytes(hash, &phase.mode, sizeof(phase.mode));
    hash = v2_hash_bytes(hash, &phase.turn, sizeof(phase.turn));
    hash = v2_hash_bytes(hash, &phase.phase, sizeof(phase.phase));
    hash = v2_hash_bytes(hash, &phase.phase_count,
                         sizeof(phase.phase_count));
    hash = v2_hash_bytes(hash, &phase.active_phase,
                         sizeof(phase.active_phase));
    hash = v2_hash_bytes(hash, &phase.alive, sizeof(phase.alive));
    hash = v2_hash_bytes(hash, &phase.phase_done,
                         sizeof(phase.phase_done));
    hash = v2_hash_bytes(hash, &phase.phase_ready,
                         sizeof(phase.phase_ready));
    hash = v2_hash_scoped_catalogs(hash, client_player(), &phase);
  }
  if (!v2_hash_state_catalogs(&hash)) {
    return FALSE;
  }
  if (v2_investigation.valid) {
    hash = v2_hash_bytes(
      hash, &v2_investigation.serial, sizeof(v2_investigation.serial));
    hash = v2_hash_bytes(
      hash, &v2_investigation.payload.digest,
      sizeof(v2_investigation.payload.digest));
  }

  phase_changed = fc_agent_v2_phase_revision_changed(
    running, v2_have_current_phase, &v2_current_phase, &phase);
  if (hash != v2_hash || !v2_current_equal() || phase_changed) {
    v2_revision++;
    for (i = 0; i < AGENT_V2_SCOPE_PINNED; i++) {
      if (v2_target_scopes[i].valid
          && v2_target_scopes[i].revision != v2_revision) {
        v2_target_scopes[i].valid = FALSE;
      }
    }
    v2_hash = hash;
    v2_current_row_count = v2_work_row_count;
    memcpy(v2_current_rows, v2_work_rows,
           v2_work_row_count * sizeof(v2_work_rows[0]));
    v2_current_action_count = v2_work_action_count;
    memcpy(v2_current_actions, v2_work_actions,
           v2_work_action_count * sizeof(v2_work_actions[0]));
    for (i = 0; i < v2_current_action_count; i++) {
      v2_assign_slot(&v2_current_actions[i]);
    }
    v2_have_current = TRUE;
  }
  v2_current_phase = phase;
  v2_have_current_phase = running;
  return TRUE;
}

static const char *v2_action_kind_name(enum agent_v2_action_kind kind)
{
  switch (kind) {
  case AGENT_V2_ACTION_PREGAME_CONFIGURE:
    return "pregame.configure";
  case AGENT_V2_ACTION_PREGAME_SET_TEAM:
    return "pregame.set_team";
  case AGENT_V2_ACTION_PREGAME_SET_READY:
    return "pregame.set_ready";
  case AGENT_V2_ACTION_PLAYER_CAST_VOTE:
    return "player.cast_vote";
  case AGENT_V2_ACTION_PHASE_END:
    return "phase.end";
  case AGENT_V2_ACTION_MOVE:
    return "unit.move";
  case AGENT_V2_ACTION_ATTACK:
    return "unit.attack";
  case AGENT_V2_ACTION_FOUND_CITY:
    return "city.found";
  case AGENT_V2_ACTION_RESEARCH_TARGET:
    return "research.set_target";
  case AGENT_V2_ACTION_RESEARCH_GOAL:
    return "research.set_goal";
  case AGENT_V2_ACTION_ECONOMY_RATES:
    return "economy.set_rates";
  case AGENT_V2_ACTION_PLAYER_SEND_CHAT:
    return "player.send_chat";
  case AGENT_V2_ACTION_CITY_PRODUCTION:
    return "city.set_production";
  case AGENT_V2_ACTION_CITY_BUY:
    return "city.buy_production";
  case AGENT_V2_ACTION_CITY_WORK_TILE:
    return "city.work_tile";
  case AGENT_V2_ACTION_CITY_UNWORK_TILE:
    return "city.unwork_tile";
  case AGENT_V2_ACTION_CITY_SET_SPECIALIST:
    return "city.set_specialist";
  case AGENT_V2_ACTION_CITY_SET_WORKLIST:
    return "city.set_worklist";
  case AGENT_V2_ACTION_CITY_SET_OPTIONS:
    return "city.set_options";
  case AGENT_V2_ACTION_CITY_RENAME:
    return "city.rename";
  case AGENT_V2_ACTION_CITY_SELL_IMPROVEMENT:
    return "city.sell_improvement";
  case AGENT_V2_ACTION_CITY_SET_RALLY:
    return "city.set_rally";
  case AGENT_V2_ACTION_CITY_CLEAR_RALLY:
    return "city.clear_rally";
  case AGENT_V2_ACTION_CITY_SET_GOVERNOR:
    return "city.set_governor";
  case AGENT_V2_ACTION_CITY_CLEAR_GOVERNOR:
    return "city.clear_governor";
  case AGENT_V2_ACTION_CITY_REQUEST_WORKER_TASK:
    return "city.request_worker_task";
  case AGENT_V2_ACTION_CITY_CHANGE_WORKER_TASK:
    return "city.change_worker_task";
  case AGENT_V2_ACTION_CITY_REMOVE_WORKER_TASK:
    return "city.remove_worker_task";
  case AGENT_V2_ACTION_WORKER_START:
    return "unit.start_activity";
  case AGENT_V2_ACTION_CANCEL_ACTIVITY:
    return "unit.cancel_activity";
  case AGENT_V2_ACTION_UNIT_SENTRY:
    return "unit.sentry";
  case AGENT_V2_ACTION_UNIT_FORTIFY:
    return "unit.fortify";
  case AGENT_V2_ACTION_UNIT_CONVERT:
    return "unit.convert";
  case AGENT_V2_ACTION_UNIT_DISBAND:
    return "unit.disband";
  case AGENT_V2_ACTION_UNIT_HOMELESS:
    return "unit.homeless";
  case AGENT_V2_ACTION_UNIT_UPGRADE:
    return "unit.upgrade";
  case AGENT_V2_ACTION_UNIT_REHOME:
    return "unit.rehome";
  case AGENT_V2_ACTION_UNIT_JOIN_CITY:
    return "unit.join_city";
  case AGENT_V2_ACTION_UNIT_ESTABLISH_TRADE:
    return "unit.establish_trade";
  case AGENT_V2_ACTION_UNIT_MARKETPLACE:
    return "unit.marketplace";
  case AGENT_V2_ACTION_UNIT_HELP_WONDER:
    return "unit.help_wonder";
  case AGENT_V2_ACTION_UNIT_DISBAND_RECOVER:
    return "unit.disband_recover";
  case AGENT_V2_ACTION_UNIT_AIRLIFT:
    return "unit.airlift";
  case AGENT_V2_ACTION_UNIT_PARADROP:
    return "unit.paradrop";
  case AGENT_V2_ACTION_UNIT_TELEPORT:
    return "unit.teleport";
  case AGENT_V2_ACTION_TRANSPORT_BOARD:
    return "unit.board";
  case AGENT_V2_ACTION_TRANSPORT_DEBOARD:
    return "unit.deboard";
  case AGENT_V2_ACTION_TRANSPORT_EMBARK:
    return "unit.embark";
  case AGENT_V2_ACTION_TRANSPORT_DISEMBARK:
    return "unit.disembark";
  case AGENT_V2_ACTION_TRANSPORT_LOAD:
    return "unit.load";
  case AGENT_V2_ACTION_TRANSPORT_UNLOAD:
    return "unit.unload";
  case AGENT_V2_ACTION_UNIT_AUTO_WORK:
    return "unit.auto_work";
  case AGENT_V2_ACTION_UNIT_AUTO_EXPLORE:
    return "unit.auto_explore";
  case AGENT_V2_ACTION_UNIT_CANCEL_AUTOMATION:
    return "unit.cancel_automation";
  case AGENT_V2_ACTION_UNIT_CANCEL_ORDERS:
    return "unit.cancel_orders";
  case AGENT_V2_ACTION_UNIT_GOTO:
    return "unit.goto";
  case AGENT_V2_ACTION_UNIT_GOTO_AND_PERFORM:
    return "unit.goto_and_perform";
  case AGENT_V2_ACTION_UNIT_CONNECT_ROUTE:
    return "unit.connect_route";
  case AGENT_V2_ACTION_UNIT_SET_ROUTE:
    return "unit.set_route";
  case AGENT_V2_ACTION_UNIT_SPECIAL:
    return "unit.special";
  case AGENT_V2_ACTION_PLAYER_PLACE_INFRA:
    return "player.place_infrastructure";
  case AGENT_V2_ACTION_GOVERNMENT_REVOLUTION:
    return "government.revolution";
  case AGENT_V2_ACTION_GOVERNMENT_CHANGE:
    return "government.change";
  case AGENT_V2_ACTION_MULTIPLIER_SET:
    return "player.set_multiplier";
  case AGENT_V2_ACTION_SPACESHIP_PLACE:
    return "spaceship.place_component";
  case AGENT_V2_ACTION_SPACESHIP_LAUNCH:
    return "spaceship.launch";
  case AGENT_V2_ACTION_DIPLOMACY_OPEN_MEETING:
    return "diplomacy.open_meeting";
  case AGENT_V2_ACTION_DIPLOMACY_CLOSE_MEETING:
    return "diplomacy.close_meeting";
  case AGENT_V2_ACTION_DIPLOMACY_PROPOSE_CLAUSE:
    return "diplomacy.propose_clause";
  case AGENT_V2_ACTION_DIPLOMACY_REMOVE_CLAUSE:
    return "diplomacy.remove_clause";
  case AGENT_V2_ACTION_DIPLOMACY_ACCEPT:
    return "diplomacy.accept";
  case AGENT_V2_ACTION_DIPLOMACY_WITHDRAW_ACCEPTANCE:
    return "diplomacy.withdraw_acceptance";
  case AGENT_V2_ACTION_DIPLOMACY_BREAK_RELATION:
    return "diplomacy.break_relation";
  case AGENT_V2_ACTION_DIPLOMACY_WITHDRAW_VISION:
    return "diplomacy.withdraw_vision";
  case AGENT_V2_ACTION_DIPLOMACY_WITHDRAW_SHARED_TILES:
    return "diplomacy.withdraw_shared_tiles";
  case AGENT_V2_ACTION_KIND_COUNT:
    break;
  }
  return "unknown";
}

static const char *v2_probability_kind_name(
  enum agent_v2_probability_kind kind)
{
  switch (kind) {
  case AGENT_V2_PROBABILITY_EXACT:
    return "exact";
  case AGENT_V2_PROBABILITY_RANGE:
    return "range";
  case AGENT_V2_PROBABILITY_UNKNOWN:
    return "unknown";
  case AGENT_V2_PROBABILITY_NOT_IMPLEMENTED:
    return "not_implemented";
  }
  return "invalid";
}

static bool v2_format_action_row(const struct agent_v2_action *action,
                                 struct agent_v2_row *row)
{
  const struct action *native = action->action != ACTION_NONE
                                ? action_by_number(action->action) : NULL;
  const char *native_rule;
  const char *target_kind;
  const char *result_name;
  const char *build_kind = v2_build_kind_name(action->target_build_kind);
  const char *spaceship_part = "none";
  const char *activity = v2_worker_activity_name(action->target_activity);
  const char *target_name = "none";
  const char *clause_name = "none";
  const char *argument_contract = "none";
  bool consuming = native != NULL && native->actor_consuming_always;
  const char *legality = action->probability_kind
                         == AGENT_V2_PROBABILITY_NOT_IMPLEMENTED
                         ? "unresolved"
                         : (action->probability_kind
                            == AGENT_V2_PROBABILITY_EXACT
                            ? "legal" : "possibly_legal");
  char actor[48];
  char counterpart[48];
  char clause_giver[48];
  char clauses_digest[32];
  char clause_type_value[AGENT_V2_ROW_MAX];
  char clause_name_value[AGENT_V2_ROW_MAX];
  char relation_state_value[AGENT_V2_ROW_MAX];
  char rule_value[AGENT_V2_ROW_MAX];
  char target_value[AGENT_V2_ROW_MAX];
  char result_value[AGENT_V2_ROW_MAX];
  char build_kind_value[AGENT_V2_ROW_MAX];
  char spaceship_part_value[AGENT_V2_ROW_MAX];
  char activity_value[AGENT_V2_ROW_MAX];
  char target_name_value[AGENT_V2_ROW_MAX];
  char source_city[48];
  char destination_city[48];
  char target_unit[48];
  char transport_context[48];
  int length;

  if (action->kind == AGENT_V2_ACTION_PREGAME_CONFIGURE) {
    if (native != NULL || action->player_id < 0) {
      return FALSE;
    }
    native_rule = "pregame.configure";
    target_kind = "Pregame Configuration";
    result_name = "Configuration Changed";
    target_name = "nation leader sex style";
    argument_contract = "pregame-config-required";
    consuming = FALSE;
  } else if (action->kind == AGENT_V2_ACTION_PREGAME_SET_TEAM) {
    if (native != NULL || action->player_id < 0
        || action->desired_acceptance != -1) {
      return FALSE;
    }
    native_rule = "pregame.set_team";
    target_kind = "Pregame Team";
    result_name = "Team Changed";
    target_name = "team";
    argument_contract = "pregame-team-required";
    consuming = FALSE;
  } else if (action->kind == AGENT_V2_ACTION_PREGAME_SET_READY) {
    if (native != NULL || action->player_id < 0
        || (action->desired_acceptance != 0
            && action->desired_acceptance != 1)) {
      return FALSE;
    }
    native_rule = "pregame.set_ready";
    target_kind = "Pregame Readiness";
    result_name = "Readiness Changed";
    target_name = action->desired_acceptance ? "ready" : "not ready";
    argument_contract = "pregame-ready-required";
    consuming = FALSE;
  } else if (action->kind == AGENT_V2_ACTION_PLAYER_SEND_CHAT) {
    if (native != NULL || action->player_id < 0) {
      return FALSE;
    }
    native_rule = "player.send_chat";
    target_kind = "Chat Channel";
    result_name = "Chat Echo Received";
    argument_contract = "chat-required";
    consuming = FALSE;
  } else if (action->kind == AGENT_V2_ACTION_PLAYER_CAST_VOTE) {
    if (native != NULL || action->player_id < 0 || action->vote_no < 0) {
      return FALSE;
    }
    native_rule = "player.cast_vote";
    target_kind = "Vote";
    result_name = "Vote Recorded";
    target_name = "vote";
    argument_contract = "vote-required";
    consuming = FALSE;
  } else if (action->kind >= AGENT_V2_ACTION_DIPLOMACY_OPEN_MEETING
      && action->kind
         <= AGENT_V2_ACTION_DIPLOMACY_WITHDRAW_SHARED_TILES) {
    if (native != NULL || action->player_id < 0
        || action->counterpart_id < 0) {
      return FALSE;
    }
    native_rule = v2_action_kind_name(action->kind);
    target_kind = "Diplomatic Relation";
    consuming = FALSE;
    switch (action->kind) {
    case AGENT_V2_ACTION_DIPLOMACY_OPEN_MEETING:
      result_name = "Meeting Opened";
      target_name = "meeting";
      break;
    case AGENT_V2_ACTION_DIPLOMACY_CLOSE_MEETING:
      result_name = "Meeting Closed";
      target_name = "meeting";
      break;
    case AGENT_V2_ACTION_DIPLOMACY_PROPOSE_CLAUSE:
      if (!clause_type_is_valid(action->clause_type)
          || action->clause_giver_id < 0) {
        return FALSE;
      }
      result_name = "Clause Proposed";
      target_name = clause_type_name(action->clause_type);
      if (action->clause_type == CLAUSE_GOLD) {
        native_rule = "diplomacy.propose_gold";
        argument_contract = "gold-required";
      }
      break;
    case AGENT_V2_ACTION_DIPLOMACY_REMOVE_CLAUSE:
      if (!clause_type_is_valid(action->clause_type)
          || action->clause_giver_id < 0) {
        return FALSE;
      }
      result_name = "Clause Removed";
      target_name = clause_type_name(action->clause_type);
      break;
    case AGENT_V2_ACTION_DIPLOMACY_ACCEPT:
      if (action->desired_acceptance != 1) {
        return FALSE;
      }
      result_name = "Acceptance Recorded";
      target_name = "accepted";
      break;
    case AGENT_V2_ACTION_DIPLOMACY_WITHDRAW_ACCEPTANCE:
      if (action->desired_acceptance != 0) {
        return FALSE;
      }
      result_name = "Acceptance Withdrawn";
      target_name = "not accepted";
      break;
    case AGENT_V2_ACTION_DIPLOMACY_BREAK_RELATION:
      result_name = "Relation Changed";
      target_name = "lower relation";
      break;
    case AGENT_V2_ACTION_DIPLOMACY_WITHDRAW_VISION:
      result_name = "Vision Withdrawn";
      target_name = "outgoing vision";
      break;
    case AGENT_V2_ACTION_DIPLOMACY_WITHDRAW_SHARED_TILES:
      result_name = "Shared Tiles Withdrawn";
      target_name = "outgoing shared tiles";
      break;
    default:
      return FALSE;
    }
  } else if (action->kind == AGENT_V2_ACTION_CITY_SET_RALLY) {
    if (native != NULL || action->city_id < 0
        || action->rally_source_tile < 0 || action->target_tile < 0
        || action->rally_production_unit_type < 0
        || action->rally_veteran_level < 0
        || action->rally_order_count < 1
        || action->rally_order_count >= MAX_LEN_ROUTE
        || action->rally_action_move) {
      return FALSE;
    }
    native_rule = "city.set_rally";
    target_kind = "Tile";
    result_name = "Rally Point Set";
    target_name = "destination";
    argument_contract = "persistent-required";
    consuming = FALSE;
  } else if (action->kind == AGENT_V2_ACTION_CITY_REQUEST_WORKER_TASK
             || action->kind == AGENT_V2_ACTION_CITY_CHANGE_WORKER_TASK
             || action->kind == AGENT_V2_ACTION_CITY_REMOVE_WORKER_TASK) {
    const struct extra_type *task_extra =
      action->target_extra != EXTRA_NONE
      ? extra_by_number(action->target_extra) : NULL;
    bool remove = action->kind
                  == AGENT_V2_ACTION_CITY_REMOVE_WORKER_TASK;

    if (native != NULL || action->city_id < 0 || action->target_tile < 0
        || activity == NULL
        || (remove
            ? (action->target_activity != ACTIVITY_LAST
               || action->target_extra != EXTRA_NONE
               || !action->worker_task_baseline_present)
            : (action->target_activity == ACTIVITY_LAST
               || (activity_requires_target(action->target_activity)
                   != (task_extra != NULL))))) {
      return FALSE;
    }
    native_rule = v2_action_kind_name(action->kind);
    target_kind = "City Worker Task";
    result_name = remove ? "Worker Task Removed"
                  : action->kind
                    == AGENT_V2_ACTION_CITY_REQUEST_WORKER_TASK
                    ? "Worker Task Requested" : "Worker Task Changed";
    target_name = remove ? "standing task"
                  : task_extra != NULL ? extra_rule_name(task_extra) : activity;
    consuming = FALSE;
  } else if (action->kind == AGENT_V2_ACTION_UNIT_GOTO) {
    if (native != NULL || action->source_unit_tile < 0
        || action->target_tile < 0 || action->goto_order_count < 1
        || action->goto_destination_tile != action->target_tile
        || action->goto_orders_digest == 0
        || action->goto_order_count >= MAX_LEN_ROUTE) {
      return FALSE;
    }
    native_rule = "unit.goto";
    target_kind = "Tile";
    result_name = "Orders Queued";
    target_name = "destination";
    consuming = FALSE;
  } else if (action->kind == AGENT_V2_ACTION_UNIT_GOTO_AND_PERFORM) {
    if (native == NULL || action->source_unit_tile < 0
        || action->target_tile < 0 || action->goto_destination_tile < 0
        || action->goto_order_count < 1
        || action->goto_order_count >= MAX_LEN_ROUTE
        || action->goto_orders_digest == 0
        || native->actor_consuming_always
        || action_get_actor_kind(native) != AAK_UNIT
        || action_get_sub_target_kind(native) != ASTK_NONE) {
      return FALSE;
    }
    native_rule = "unit.goto_and_perform";
    target_kind = "Action Route";
    result_name = "Orders Queued";
    target_name = action_result_name(native->result);
    consuming = FALSE;
  } else if (action->kind == AGENT_V2_ACTION_UNIT_CONNECT_ROUTE) {
    const struct extra_type *route_extra =
      action->target_extra != EXTRA_NONE
      ? extra_by_number(action->target_extra) : NULL;

    if (native == NULL || action->source_unit_tile < 0
        || action->target_tile < 0
        || action->goto_destination_tile != action->target_tile
        || action->goto_order_count < 1
        || action->goto_order_count > MAX_LEN_ROUTE
        || action->goto_orders_digest == 0 || route_extra == NULL
        || (action->target_activity != ACTIVITY_GEN_ROAD
            && action->target_activity != ACTIVITY_IRRIGATE)
        || action->action
           != activity_default_action(action->target_activity)) {
      return FALSE;
    }
    native_rule = "unit.connect_route";
    target_kind = "Construction Route";
    result_name = "Orders Queued";
    target_name = extra_rule_name(route_extra);
    consuming = FALSE;
  } else if (action->kind == AGENT_V2_ACTION_UNIT_SET_ROUTE) {
    if (native != NULL || action->unit_id < 0
        || action->target_tile != -1
        || action->route_waypoint_limit
           != CLIENT_UNIT_ROUTE_MAX_WAYPOINTS) {
      return FALSE;
    }
    native_rule = "unit.set_route";
    target_kind = "Route";
    result_name = "Orders Queued";
    target_name = "route";
    argument_contract = "route-required";
    consuming = FALSE;
  } else if (action->kind == AGENT_V2_ACTION_UNIT_SPECIAL) {
    const struct extra_type *target_extra =
      action->target_extra != EXTRA_NONE
      ? extra_by_number(action->target_extra) : NULL;
    const struct impr_type *target_improvement =
      action->target_build_kind == VUT_IMPROVEMENT
      ? improvement_by_number(action->target_build_id) : NULL;
    const struct city *target_city = game_city_by_number(
      action->destination_city_id);
    const struct research *self_research = client_player() != NULL
                                             ? research_get(client_player())
                                             : NULL;

    if (native == NULL || action_get_actor_kind(native) != AAK_UNIT
        || action->unit_id < 0 || action->target_tile < 0
        || action->gold_cost < -1) {
      return FALSE;
    }
    if (native->result == ACTRES_SPY_TARGETED_SABOTAGE_CITY) {
      if (target_improvement == NULL || target_city == NULL
          || target_improvement->sabotage <= 0
          || action->target_building_catalog_request_id <= 0
          || action->target_building_catalog_revision != v2_revision
          || action->target_building_catalog_digest == 0) {
        return FALSE;
      }
    } else if (action->target_build_kind != VUT_NONE
               || action->target_build_id != -1
               || action->target_building_catalog_request_id != 0
               || action->target_building_catalog_revision != 0
               || action->target_building_catalog_digest != 0) {
      return FALSE;
    }
    native_rule = action_id_rule_name(action->action);
    target_kind = action_target_kind_name(native->target_kind);
    result_name = action_result_name(native->result);
    target_name = target_extra != NULL
                  ? extra_rule_name(target_extra)
                  : native->result == ACTRES_SPY_TARGETED_SABOTAGE_CITY
                    && target_improvement != NULL && target_city != NULL
                    ? city_improvement_name_translation(
                        target_city, target_improvement)
                  : action->target_tech >= A_FIRST
                    && action->target_tech < A_LAST
                    && self_research != NULL
                    ? research_advance_name_translation(
                        self_research, action->target_tech)
                    : "target";
  } else if (action->kind == AGENT_V2_ACTION_PLAYER_PLACE_INFRA) {
    if (native != NULL || action->player_id < 0
        || action->target_tile < 0 || action->target_extra != EXTRA_NONE
        || action->infrastructure_cost != 0
        || action->infrastructure_turns != 0
        || action->infrastructure_choice_count < 1
        || action->infrastructure_choice_count
           > FC_AGENT_V2_MAX_INFRA_CHOICES
        || strcmp(action->infrastructure_choices, "-") == 0) {
      return FALSE;
    }
    native_rule = "player.place_infrastructure";
    target_kind = "Tile";
    result_name = "Infrastructure Placement Started";
    target_name = "infrastructure";
    argument_contract = "infrastructure-extra-required";
    consuming = FALSE;
  } else if (action->kind == AGENT_V2_ACTION_WORKER_START) {
    const struct extra_type *extra = action->target_extra != EXTRA_NONE
                                     ? extra_by_number(
                                       action->target_extra) : NULL;

    if (native == NULL || activity == NULL
        || action_get_activity(native) != action->target_activity
        || (action->target_extra != EXTRA_NONE && extra == NULL)) {
      return FALSE;
    }
    native_rule = "unit.start_activity";
    target_kind = "Worker Activity";
    result_name = "Activity Installed";
    target_name = extra != NULL ? extra_rule_name(extra) : activity;
    consuming = FALSE;
  } else if (action->kind == AGENT_V2_ACTION_UNIT_AIRLIFT
             || action->kind == AGENT_V2_ACTION_UNIT_PARADROP
             || action->kind == AGENT_V2_ACTION_UNIT_TELEPORT) {
    enum action_result expected_result;
    enum action_target_kind expected_target;

    if (native == NULL || action_get_actor_kind(native) != AAK_UNIT
        || native->actor_consuming_always
        || !v2_noncombat_mobility_action_allowed(
             action->kind, action->action)) {
      return FALSE;
    }
    if (action->kind == AGENT_V2_ACTION_UNIT_AIRLIFT) {
      const struct player *self = client_player();
      const struct city *source = self != NULL
                                  ? player_city_by_number(
                                      self, action->source_city_id) : NULL;
      const struct city *destination = self != NULL
                                       ? player_city_by_number(
                                           self,
                                           action->destination_city_id) : NULL;

      expected_result = ACTRES_AIRLIFT;
      expected_target = ATK_CITY;
      if (action->action != ACTION_AIRLIFT || source == NULL
          || destination == NULL || source == destination
          || action->target_tile != -1) {
        return FALSE;
      }
      target_name = city_name_get(destination);
    } else {
      expected_result = action->kind == AGENT_V2_ACTION_UNIT_PARADROP
                        ? ACTRES_PARADROP : ACTRES_TELEPORT;
      expected_target = ATK_TILE;
      if (action->source_city_id != -1
          || action->destination_city_id != -1
          || action->target_tile < 0) {
        return FALSE;
      }
      target_name = "destination";
    }
    if (native->result != expected_result
        || action_get_target_kind(native) != expected_target) {
      return FALSE;
    }
    native_rule = action_id_rule_name(action->action);
    target_kind = action_target_kind_name(native->target_kind);
    result_name = action_result_name(native->result);
    consuming = FALSE;
  } else if (action->kind >= AGENT_V2_ACTION_UNIT_UPGRADE
             && action->kind <= AGENT_V2_ACTION_UNIT_DISBAND_RECOVER) {
    enum action_result expected_result;
    bool expected_consuming;
    const struct city *destination = game_city_by_number(
      action->destination_city_id);

    if (native == NULL || action_get_actor_kind(native) != AAK_UNIT
        || action_get_target_kind(native) != ATK_CITY
        || destination == NULL || action->destination_city_id < 0
        || action->destination_city_tile < 0
        || action->target_tile != -1) {
      return FALSE;
    }
    switch (action->kind) {
    case AGENT_V2_ACTION_UNIT_UPGRADE: {
      const struct unit_type *target = utype_by_number(
        action->target_build_id);

      expected_result = ACTRES_UPGRADE_UNIT;
      expected_consuming = FALSE;
      if (action->target_build_kind != VUT_UTYPE || target == NULL) {
        return FALSE;
      }
      target_name = utype_rule_name(target);
      break;
    }
    case AGENT_V2_ACTION_UNIT_REHOME:
      expected_result = ACTRES_HOME_CITY;
      expected_consuming = FALSE;
      target_name = city_name_get(destination);
      break;
    case AGENT_V2_ACTION_UNIT_JOIN_CITY:
      expected_result = ACTRES_JOIN_CITY;
      expected_consuming = TRUE;
      target_name = city_name_get(destination);
      break;
    case AGENT_V2_ACTION_UNIT_ESTABLISH_TRADE:
      expected_result = ACTRES_TRADE_ROUTE;
      expected_consuming = TRUE;
      target_name = city_name_get(destination);
      break;
    case AGENT_V2_ACTION_UNIT_MARKETPLACE:
      expected_result = ACTRES_MARKETPLACE;
      expected_consuming = TRUE;
      target_name = city_name_get(destination);
      break;
    case AGENT_V2_ACTION_UNIT_HELP_WONDER:
      expected_result = ACTRES_HELP_WONDER;
      expected_consuming = TRUE;
      target_name = city_name_get(destination);
      break;
    case AGENT_V2_ACTION_UNIT_DISBAND_RECOVER:
      expected_result = ACTRES_DISBAND_UNIT_RECOVER;
      expected_consuming = TRUE;
      target_name = city_name_get(destination);
      if (action->action != ACTION_DISBAND_UNIT_RECOVER) {
        return FALSE;
      }
      break;
    default:
      return FALSE;
    }
    if (native->result != expected_result
        || native->actor_consuming_always != expected_consuming
        || (expected_result != ACTRES_UPGRADE_UNIT
            && (action->target_build_kind != VUT_NONE
                || action->target_build_id != -1))
        || ((expected_result == ACTRES_TRADE_ROUTE
             || expected_result == ACTRES_MARKETPLACE)
            != (action->source_city_id >= 0))) {
      return FALSE;
    }
    native_rule = action_id_rule_name(action->action);
    target_kind = action_target_kind_name(native->target_kind);
    result_name = action_result_name(native->result);
    consuming = expected_consuming;
  } else if (action->kind >= AGENT_V2_ACTION_TRANSPORT_BOARD
             && action->kind <= AGENT_V2_ACTION_TRANSPORT_UNLOAD) {
    enum action_result expected_result;
    enum action_target_kind expected_target;

    if (native == NULL || action_get_actor_kind(native) != AAK_UNIT
        || native->actor_consuming_always) {
      return FALSE;
    }
    switch (action->kind) {
    case AGENT_V2_ACTION_TRANSPORT_BOARD:
      expected_result = ACTRES_TRANSPORT_BOARD;
      expected_target = ATK_UNIT;
      target_name = "transporter";
      break;
    case AGENT_V2_ACTION_TRANSPORT_DEBOARD:
      expected_result = ACTRES_TRANSPORT_DEBOARD;
      expected_target = ATK_UNIT;
      target_name = "transporter";
      break;
    case AGENT_V2_ACTION_TRANSPORT_EMBARK:
      expected_result = ACTRES_TRANSPORT_EMBARK;
      expected_target = ATK_UNIT;
      target_name = "transporter";
      break;
    case AGENT_V2_ACTION_TRANSPORT_DISEMBARK:
      expected_result = ACTRES_TRANSPORT_DISEMBARK;
      expected_target = ATK_TILE;
      target_name = "destination";
      break;
    case AGENT_V2_ACTION_TRANSPORT_LOAD:
      expected_result = ACTRES_TRANSPORT_LOAD;
      expected_target = ATK_UNIT;
      target_name = "cargo";
      break;
    case AGENT_V2_ACTION_TRANSPORT_UNLOAD:
      expected_result = ACTRES_TRANSPORT_UNLOAD;
      expected_target = ATK_UNIT;
      target_name = "cargo";
      break;
    default:
      return FALSE;
    }
    if (native->result != expected_result
        || action_get_target_kind(native) != expected_target
        || (expected_target == ATK_UNIT)
           != (action->target_unit_id >= 0)
        || (expected_target == ATK_TILE) != (action->target_tile >= 0)) {
      return FALSE;
    }
    native_rule = action_id_rule_name(action->action);
    target_kind = action_target_kind_name(native->target_kind);
    result_name = action_result_name(native->result);
    consuming = FALSE;
  } else if (action->kind == AGENT_V2_ACTION_UNIT_FORTIFY
             || action->kind == AGENT_V2_ACTION_UNIT_CONVERT
             || action->kind == AGENT_V2_ACTION_UNIT_DISBAND
             || action->kind == AGENT_V2_ACTION_UNIT_HOMELESS) {
    enum action_result expected_result;

    if (native == NULL || action_get_actor_kind(native) != AAK_UNIT
        || action_get_target_kind(native) != ATK_SELF) {
      return FALSE;
    }
    switch (action->kind) {
    case AGENT_V2_ACTION_UNIT_FORTIFY:
      expected_result = ACTRES_FORTIFY;
      result_name = "Fortify Installed";
      target_name = "fortifying";
      break;
    case AGENT_V2_ACTION_UNIT_CONVERT: {
      struct unit_type *converted = utype_by_number(action->target_build_id);

      expected_result = ACTRES_CONVERT;
      result_name = "Conversion Installed";
      if (action->target_build_kind != VUT_UTYPE || converted == NULL) {
        return FALSE;
      }
      target_name = utype_rule_name(converted);
      break;
    }
    case AGENT_V2_ACTION_UNIT_DISBAND:
      expected_result = ACTRES_DISBAND_UNIT;
      result_name = "Unit Disbanded";
      target_name = "self";
      break;
    case AGENT_V2_ACTION_UNIT_HOMELESS:
      expected_result = ACTRES_HOMELESS;
      result_name = "Home City Cleared";
      target_name = "self";
      break;
    default:
      return FALSE;
    }
    if (native->result != expected_result
        || native->actor_consuming_always
           != (expected_result == ACTRES_DISBAND_UNIT)) {
      return FALSE;
    }
    native_rule = action_id_rule_name(action->action);
    target_kind = "Self";
    consuming = native->actor_consuming_always;
  } else if (native != NULL) {
    native_rule = action_id_rule_name(action->action);
    target_kind = action_target_kind_name(native->target_kind);
    result_name = action_result_name(native->result);
    if (action->kind == AGENT_V2_ACTION_FOUND_CITY) {
      argument_contract = "city_name-required";
    }
  } else {
    switch (action->kind) {
    case AGENT_V2_ACTION_PHASE_END:
      native_rule = "phase.end";
      target_kind = "player";
      result_name = "phase_end";
      break;
    case AGENT_V2_ACTION_RESEARCH_TARGET:
      native_rule = "research.set_target";
      target_kind = "Technology";
      result_name = "Research Target";
      break;
    case AGENT_V2_ACTION_RESEARCH_GOAL:
      native_rule = "research.set_goal";
      target_kind = "Technology";
      result_name = "Research Goal";
      break;
    case AGENT_V2_ACTION_ECONOMY_RATES:
      native_rule = "economy.set_rates";
      target_kind = "Player";
      result_name = "Economic Rates";
      argument_contract = "rates-required";
      break;
    case AGENT_V2_ACTION_GOVERNMENT_REVOLUTION:
    case AGENT_V2_ACTION_GOVERNMENT_CHANGE: {
      const struct government *target = government_by_number(
        action->target_government);

      if (target == NULL) {
        return FALSE;
      }
      native_rule = action->kind == AGENT_V2_ACTION_GOVERNMENT_REVOLUTION
                    ? "government.revolution" : "government.change";
      target_kind = "Government";
      result_name = action->kind == AGENT_V2_ACTION_GOVERNMENT_REVOLUTION
                    ? "Revolution Started" : "Government Choice Recorded";
      target_name = government_rule_name(target);
      break;
    }
    case AGENT_V2_ACTION_MULTIPLIER_SET: {
      struct multiplier *pmul = multiplier_by_number(
        action->target_multiplier);

      if (pmul == NULL || pmul->ruledit_disabled
          || !v2_multiplier_value_valid(pmul,
                                        action->multiplier_value)) {
        return FALSE;
      }
      native_rule = "player.set_multiplier";
      target_kind = "Multiplier";
      result_name = "Multiplier Target Changed";
      target_name = multiplier_rule_name(pmul);
      break;
    }
    case AGENT_V2_ACTION_SPACESHIP_PLACE:
      spaceship_part = v2_spaceship_part_name(action->spaceship_part);
      if (spaceship_part == NULL || action->spaceship_value < 0) {
        return FALSE;
      }
      native_rule = "spaceship.place_component";
      target_kind = "Spaceship Part";
      result_name = "Spaceship Part Placed";
      target_name = spaceship_part;
      break;
    case AGENT_V2_ACTION_SPACESHIP_LAUNCH:
      if (action->spaceship_part != -1 || action->spaceship_value != -1) {
        return FALSE;
      }
      native_rule = "spaceship.launch";
      target_kind = "Spaceship";
      result_name = "Spaceship Launched";
      target_name = "launch";
      break;
    case AGENT_V2_ACTION_CITY_PRODUCTION:
    case AGENT_V2_ACTION_CITY_BUY: {
      struct universal production = universal_by_number(
        action->target_build_kind, action->target_build_id);

      if (!v2_production_supported(&production)
          || build_kind == NULL) {
        return FALSE;
      }
      native_rule = action->kind == AGENT_V2_ACTION_CITY_PRODUCTION
                    ? "city.set_production" : "city.buy_production";
      target_kind = "Production";
      result_name = action->kind == AGENT_V2_ACTION_CITY_PRODUCTION
                    ? "Production Changed" : "Production Bought";
      target_name = universal_rule_name(&production);
      break;
    }
    case AGENT_V2_ACTION_CITY_WORK_TILE:
      native_rule = "city.work_tile";
      target_kind = "City Tile";
      result_name = "Citizen Assigned";
      target_name = "worked tile";
      break;
    case AGENT_V2_ACTION_CITY_UNWORK_TILE:
      native_rule = "city.unwork_tile";
      target_kind = "City Tile";
      result_name = "Citizen Unassigned";
      target_name = "default specialist";
      break;
    case AGENT_V2_ACTION_CITY_SET_SPECIALIST: {
      struct specialist *target = specialist_by_number(
        action->target_specialist);

      if (target == NULL) {
        return FALSE;
      }
      native_rule = "city.set_specialist";
      target_kind = "Specialist";
      result_name = "Specialist Changed";
      target_name = specialist_rule_name(target);
      break;
    }
    case AGENT_V2_ACTION_CITY_SET_WORKLIST:
      native_rule = "city.set_worklist";
      target_kind = "City";
      result_name = "Worklist Changed";
      target_name = "worklist";
      argument_contract = "worklist-required";
      break;
    case AGENT_V2_ACTION_CITY_SET_OPTIONS:
      native_rule = "city.set_options";
      target_kind = "City";
      result_name = "City Options Changed";
      target_name = "options";
      argument_contract = "city-options-required";
      break;
    case AGENT_V2_ACTION_CITY_RENAME:
      native_rule = "city.rename";
      target_kind = "City";
      result_name = "City Renamed";
      target_name = "name";
      argument_contract = "city_name-required";
      break;
    case AGENT_V2_ACTION_CITY_SELL_IMPROVEMENT: {
      struct impr_type *improvement = improvement_by_number(
        action->target_build_id);

      if (action->target_build_kind != VUT_IMPROVEMENT
          || improvement == NULL) {
        return FALSE;
      }
      native_rule = "city.sell_improvement";
      target_kind = "Improvement";
      result_name = "Improvement Sold";
      target_name = improvement_rule_name(improvement);
      break;
    }
    case AGENT_V2_ACTION_CITY_CLEAR_RALLY:
      if (action->rally_source_tile < 0 || action->target_tile != -1) {
        return FALSE;
      }
      native_rule = "city.clear_rally";
      target_kind = "City";
      result_name = "Rally Point Cleared";
      target_name = "rally";
      break;
    case AGENT_V2_ACTION_CITY_SET_GOVERNOR:
      native_rule = "city.set_governor";
      target_kind = "City";
      result_name = "Governor Goal Set";
      target_name = "governor";
      argument_contract = "governor-goal-required";
      break;
    case AGENT_V2_ACTION_CITY_CLEAR_GOVERNOR:
      native_rule = "city.clear_governor";
      target_kind = "City";
      result_name = "Governor Cleared";
      target_name = "governor";
      break;
    case AGENT_V2_ACTION_CANCEL_ACTIVITY:
      native_rule = "unit.cancel_activity";
      target_kind = "Unit";
      result_name = "Activity Cancelled";
      break;
    case AGENT_V2_ACTION_UNIT_SENTRY:
      native_rule = "unit.sentry";
      target_kind = "Unit";
      result_name = "Sentry Installed";
      target_name = "sentry";
      break;
    case AGENT_V2_ACTION_UNIT_AUTO_WORK:
      native_rule = "unit.auto_work";
      target_kind = "Unit";
      result_name = "Auto Work Installed";
      target_name = "auto_work";
      break;
    case AGENT_V2_ACTION_UNIT_AUTO_EXPLORE:
      native_rule = "unit.auto_explore";
      target_kind = "Unit";
      result_name = "Auto Explore Installed";
      target_name = "auto_explore";
      break;
    case AGENT_V2_ACTION_UNIT_CANCEL_AUTOMATION:
      native_rule = "unit.cancel_automation";
      target_kind = "Unit";
      result_name = "Automation Cancelled";
      target_name = "none";
      break;
    case AGENT_V2_ACTION_UNIT_CANCEL_ORDERS:
      native_rule = "unit.cancel_orders";
      target_kind = "Unit";
      result_name = "Orders Cancelled";
      target_name = "orders";
      break;
    default:
      return FALSE;
    }
  }
  if (action->player_id >= 0) {
    fc_snprintf(actor, sizeof(actor), "p:%d:%llu", action->player_id,
                (unsigned long long) action->player_incarnation);
  } else if (action->unit_id >= 0) {
    fc_snprintf(actor, sizeof(actor), "u:%d:%llu", action->unit_id,
                (unsigned long long) action->unit_incarnation);
  } else if (action->city_id >= 0) {
    fc_snprintf(actor, sizeof(actor), "c:%d:%llu", action->city_id,
                (unsigned long long) action->city_incarnation);
  } else {
    fc_strlcpy(actor, "none", sizeof(actor));
  }
  if (action->counterpart_id >= 0) {
    fc_snprintf(counterpart, sizeof(counterpart), "p:%d:%llu",
                action->counterpart_id,
                (unsigned long long) action->counterpart_incarnation);
  } else {
    fc_strlcpy(counterpart, "none", sizeof(counterpart));
  }
  if (action->clause_giver_id >= 0) {
    fc_snprintf(clause_giver, sizeof(clause_giver), "p:%d:%llu",
                action->clause_giver_id,
                (unsigned long long) v2_existing_incarnation(
                  AGENT_V2_ENTITY_PLAYER, action->clause_giver_id));
  } else {
    fc_strlcpy(clause_giver, "none", sizeof(clause_giver));
  }
  fc_snprintf(clauses_digest, sizeof(clauses_digest), "fnv1a64-%016llx",
              (unsigned long long) action->clauses_digest);
  if (action->target_unit_id >= 0) {
    fc_snprintf(target_unit, sizeof(target_unit), "u:%d:%llu",
                action->target_unit_id,
                (unsigned long long) action->target_unit_incarnation);
  } else {
    fc_strlcpy(target_unit, "none", sizeof(target_unit));
  }
  if (action->source_city_id >= 0) {
    fc_snprintf(source_city, sizeof(source_city), "c:%d:%llu",
                action->source_city_id,
                (unsigned long long) action->source_city_incarnation);
  } else {
    fc_strlcpy(source_city, "none", sizeof(source_city));
  }
  if (action->destination_city_id >= 0) {
    fc_snprintf(destination_city, sizeof(destination_city), "c:%d:%llu",
                action->destination_city_id,
                (unsigned long long) action->destination_city_incarnation);
  } else {
    fc_strlcpy(destination_city, "none", sizeof(destination_city));
  }
  if (action->transport_context_id >= 0) {
    fc_snprintf(transport_context, sizeof(transport_context), "u:%d:%llu",
                action->transport_context_id,
                (unsigned long long) action->transport_context_incarnation);
  } else {
    fc_strlcpy(transport_context, "none", sizeof(transport_context));
  }
  if (clause_type_is_valid(action->clause_type)) {
    switch (action->clause_type) {
    case CLAUSE_ADVANCE:
      clause_name = v2_research_choice_name(action->clause_value);
      break;
    case CLAUSE_GOLD:
      clause_name = "gold";
      break;
    case CLAUSE_CITY: {
      const struct city *city = game_city_by_number(action->clause_value);

      clause_name = city != NULL ? city_name_get(city) : "Unavailable";
      break;
    }
    default:
      clause_name = "none";
      break;
    }
  }
  if (!v2_encode_row_value(native_rule, rule_value, sizeof(rule_value))
      || !v2_encode_row_value(target_kind, target_value,
                              sizeof(target_value))
      || !v2_encode_row_value(result_name, result_value,
                              sizeof(result_value))
      || build_kind == NULL
      || activity == NULL
      || !v2_encode_row_value(build_kind, build_kind_value,
                              sizeof(build_kind_value))
      || !v2_encode_row_value(spaceship_part, spaceship_part_value,
                              sizeof(spaceship_part_value))
      || !v2_encode_row_value(activity, activity_value,
                              sizeof(activity_value))
      || !v2_encode_row_value(target_name, target_name_value,
                              sizeof(target_name_value))
      || !v2_encode_row_value(
           clause_type_is_valid(action->clause_type)
             ? clause_type_name(action->clause_type) : "none",
           clause_type_value, sizeof(clause_type_value))
      || !v2_encode_row_value(
           clause_name, clause_name_value, sizeof(clause_name_value))
      || !v2_encode_row_value(
           action->relation_state != DS_LAST
             ? diplstate_type_name(action->relation_state) : "none",
           relation_state_value, sizeof(relation_state_value))) {
    return FALSE;
  }
  length = fc_snprintf(
    row->text, sizeof(row->text), FC_AGENT_V2_ROW_ACTION,
    action->slot, v2_action_kind_name(action->kind), actor, counterpart,
    (unsigned long long) action->meeting_generation, clauses_digest,
    action->self_accepted ? 1 : 0, action->other_accepted ? 1 : 0,
    relation_state_value, action->outgoing_vision ? 1 : 0,
    action->outgoing_shared_tiles ? 1 : 0,
    clause_giver, clause_type_value, action->clause_value, clause_name_value,
    action->desired_acceptance,
    action->target_tile, source_city, destination_city,
    target_unit, transport_context,
    action->target_tech, action->vote_no, action->target_government,
    action->max_rate,
    action->route_waypoint_limit,
    action->infrastructure_cost, action->infrastructure_turns,
    action->infrastructure_choice_count, action->infrastructure_choices,
    build_kind_value, action->target_build_id,
    spaceship_part_value, action->spaceship_value,
    action->target_multiplier, action->multiplier_value,
    action->source_specialist, action->target_specialist,
    action->target_extra, activity_value, target_name_value,
    rule_value, target_value, result_value, consuming ? 1 : 0, legality,
    v2_probability_kind_name(action->probability_kind),
    action->probability_min, action->probability_max, action->gold_cost,
    argument_contract);
  return length >= 0 && (size_t) length < sizeof(row->text);
}

static bool v2_pin_snapshot(struct agent_v2_snapshot **result)
{
  struct agent_v2_snapshot *snapshot;
  size_t i;

  if (!v2_refresh()) {
    return FALSE;
  }
  if (v2_current_action_count
      > AGENT_V2_MAX_ROWS - v2_current_row_count) {
    return FALSE;
  }
  fc_assert(v2_current_row_count + v2_current_action_count
            <= AGENT_V2_MAX_ROWS);
  snapshot = &v2_snapshots[v2_snapshot_serial % AGENT_V2_PINNED];
  v2_snapshot_serial++;
  snapshot->valid = TRUE;
  snapshot->revision = v2_revision;
  snapshot->row_count = v2_current_row_count;
  memcpy(snapshot->rows, v2_current_rows,
         v2_current_row_count * sizeof(v2_current_rows[0]));
  fc_snprintf(snapshot->id, sizeof(snapshot->id), "s%llu-%u",
              (unsigned long long) v2_revision, v2_snapshot_serial);

  for (i = 0; i < v2_current_action_count; i++) {
    const struct agent_v2_action *action = &v2_current_actions[i];
    struct agent_v2_row *row = &snapshot->rows[snapshot->row_count++];
    if (!v2_format_action_row(action, row)) {
      snapshot->valid = FALSE;
      return FALSE;
    }
  }
  qsort(snapshot->rows, snapshot->row_count, sizeof(snapshot->rows[0]),
        v2_row_compare);
  *result = snapshot;
  return TRUE;
}

static struct agent_v2_snapshot *v2_snapshot_by_id(const char *id)
{
  size_t i;

  for (i = 0; i < AGENT_V2_PINNED; i++) {
    if (v2_snapshots[i].valid && strcmp(v2_snapshots[i].id, id) == 0) {
      return &v2_snapshots[i];
    }
  }
  return NULL;
}

static bool v2_parse_size(const char *text, size_t *value)
{
  char *end = NULL;
  unsigned long parsed;

  if (text[0] == '\0') {
    return FALSE;
  }
  errno = 0;
  parsed = strtoul(text, &end, 10);
  if (errno != 0 || end == text || *end != '\0') {
    return FALSE;
  }
  *value = (size_t) parsed;
  return (unsigned long) *value == parsed;
}

static bool v2_parse_revision(const char *text, uint64_t *value)
{
  const char *cursor = text;
  uint64_t parsed = 0;

  if (text == NULL || value == NULL || *cursor < '1' || *cursor > '9') {
    return FALSE;
  }
  while (*cursor >= '0' && *cursor <= '9') {
    unsigned int digit = (unsigned int) (*cursor - '0');

    if (parsed > (UINT64_MAX - digit) / 10) {
      return FALSE;
    }
    parsed = parsed * 10 + digit;
    cursor++;
  }
  if (*cursor != '\0') {
    return FALSE;
  }
  *value = parsed;
  return TRUE;
}

static bool v2_resolve_owned_actor(const char *actor_ref,
                                   enum agent_v2_entity_kind *kind,
                                   int *id, uint64_t *incarnation)
{
  struct player *self = client_player();
  char prefix;
  enum agent_v2_entity_kind parsed_kind;
  int parsed_id;
  uint64_t parsed_incarnation;

  if (self == NULL
      || !fc_agent_v2_parse_entity_ref(actor_ref, &prefix, &parsed_id,
                                       &parsed_incarnation)) {
    return FALSE;
  }
  switch (prefix) {
  case 'p':
    parsed_kind = AGENT_V2_ENTITY_PLAYER;
    if (parsed_id != player_number(self)) {
      return FALSE;
    }
    break;
  case 'c':
    parsed_kind = AGENT_V2_ENTITY_CITY;
    if (player_city_by_number(self, parsed_id) == NULL) {
      return FALSE;
    }
    break;
  case 'u':
    parsed_kind = AGENT_V2_ENTITY_UNIT;
    if (player_unit_by_number(self, parsed_id) == NULL) {
      return FALSE;
    }
    break;
  default:
    return FALSE;
  }
  if (v2_existing_incarnation(parsed_kind, parsed_id)
      != parsed_incarnation) {
    return FALSE;
  }
  *kind = parsed_kind;
  *id = parsed_id;
  *incarnation = parsed_incarnation;
  return TRUE;
}

static bool v2_resolve_relation_pair(
  const char *actor_ref, const char *counterpart_ref,
  struct player **self_result, struct player **other_result)
{
  enum agent_v2_entity_kind actor_kind;
  int actor_id;
  uint64_t actor_incarnation;
  char counterpart_kind;
  int counterpart_id;
  uint64_t counterpart_incarnation;
  struct player *self = client_player();
  struct player *other;

  if (self == NULL
      || !v2_resolve_owned_actor(actor_ref, &actor_kind, &actor_id,
                                 &actor_incarnation)
      || actor_kind != AGENT_V2_ENTITY_PLAYER
      || actor_id != player_number(self)
      || !fc_agent_v2_parse_entity_ref(
           counterpart_ref, &counterpart_kind, &counterpart_id,
           &counterpart_incarnation)
      || counterpart_kind != 'p' || counterpart_id == actor_id
      || (other = player_by_number(counterpart_id)) == NULL
      || v2_existing_incarnation(
           AGENT_V2_ENTITY_PLAYER, counterpart_id)
         != counterpart_incarnation) {
    return FALSE;
  }
  *self_result = self;
  *other_result = other;
  return TRUE;
}

static bool v2_treaty_candidate_possible(
  const struct treaty *current, struct player *self,
  struct player *other, struct player *giver,
  enum clause_type type, int value)
{
  struct treaty candidate;
  bool possible;

  init_treaty(&candidate, self, other);
  if (current != NULL) {
    clause_list_iterate(current->clauses, clause) {
      struct Clause *copy = fc_malloc(sizeof(*copy));

      *copy = *clause;
      clause_list_append(candidate.clauses, copy);
    } clause_list_iterate_end;
    candidate.accept0 = current->plr0 == self
                        ? current->accept0 : current->accept1;
    candidate.accept1 = current->plr0 == self
                        ? current->accept1 : current->accept0;
  }
  possible = add_clause(&candidate, giver, type, value, self);
  clear_treaty(&candidate);
  return possible;
}

static void v2_buffer_add_relation_action(
  struct agent_v2_action_buffer *buffer,
  enum agent_v2_action_kind kind, struct player *self,
  struct player *other, uint64_t meeting_generation,
  uint64_t clauses_digest, bool self_accepted, bool other_accepted,
  struct player *giver, enum clause_type type, int value,
  const struct city *city, int desired_acceptance)
{
  struct agent_v2_action *entry;

  if (buffer->count >= buffer->capacity) {
    buffer->overflow = TRUE;
    return;
  }
  entry = &buffer->actions[buffer->count++];
  v2_action_init(entry);
  entry->kind = kind;
  entry->player_id = player_number(self);
  entry->player_incarnation = v2_existing_incarnation(
    AGENT_V2_ENTITY_PLAYER, entry->player_id);
  entry->counterpart_id = player_number(other);
  entry->counterpart_incarnation = v2_existing_incarnation(
    AGENT_V2_ENTITY_PLAYER, entry->counterpart_id);
  entry->meeting_generation = meeting_generation;
  entry->clauses_digest = clauses_digest;
  entry->self_accepted = self_accepted;
  entry->other_accepted = other_accepted;
  entry->relation_state = player_diplstate_get(self, other)->type;
  entry->outgoing_vision = gives_shared_vision(self, other);
  entry->outgoing_shared_tiles = gives_shared_tiles(self, other);
  if (giver != NULL) {
    entry->clause_giver_id = player_number(giver);
    entry->clause_type = type;
    entry->clause_value = value;
  }
  if (city != NULL) {
    entry->source_city_id = city->id;
    entry->source_city_incarnation = v2_existing_incarnation(
      AGENT_V2_ENTITY_CITY, city->id);
    entry->source_city_lifecycle_id = city->client.lifecycle_id;
    entry->source_city_tile = city_tile(city) != NULL
                              ? tile_index(city_tile(city)) : -1;
  }
  entry->desired_acceptance = desired_acceptance;
  entry->probability_kind = AGENT_V2_PROBABILITY_EXACT;
  entry->probability_min = action_prob_new_certain().min;
  entry->probability_max = action_prob_new_certain().max;
}

static void v2_build_relation_clause_candidates(
  struct agent_v2_action_buffer *buffer, struct player *self,
  struct player *other, const struct treaty *treaty,
  uint64_t meeting_generation, uint64_t clauses_digest,
  bool self_accepted, bool other_accepted)
{
  struct player *givers[2] = { self, other };
  const enum clause_type directional[] = {
    CLAUSE_MAP, CLAUSE_SEAMAP, CLAUSE_VISION,
    CLAUSE_EMBASSY, CLAUSE_SHARED_TILES
  };
  const enum clause_type pacts[] = {
    CLAUSE_CEASEFIRE, CLAUSE_PEACE, CLAUSE_ALLIANCE
  };
  size_t giver_index;
  size_t type_index;

  for (giver_index = 0; giver_index < ARRAY_SIZE(givers); giver_index++) {
    struct player *giver = givers[giver_index];
    struct player *receiver = giver == self ? other : self;
    const struct research *giver_research = research_get(giver);
    const struct research *receiver_research = research_get(receiver);

    for (type_index = 0; type_index < ARRAY_SIZE(directional);
         type_index++) {
      enum clause_type type = directional[type_index];

      if (v2_treaty_candidate_possible(
            treaty, self, other, giver, type, 0)) {
        v2_buffer_add_relation_action(
          buffer, AGENT_V2_ACTION_DIPLOMACY_PROPOSE_CLAUSE,
          self, other, meeting_generation, clauses_digest,
          self_accepted, other_accepted, giver, type, 0, NULL, -1);
      }
    }
    if (giver->economic.gold > 0
        && v2_treaty_candidate_possible(
             treaty, self, other, giver, CLAUSE_GOLD,
             giver->economic.gold)) {
      v2_buffer_add_relation_action(
        buffer, AGENT_V2_ACTION_DIPLOMACY_PROPOSE_CLAUSE,
        self, other, meeting_generation, clauses_digest,
        self_accepted, other_accepted, giver, CLAUSE_GOLD,
        giver->economic.gold, NULL, -1);
    }
    if (giver_research != NULL && receiver_research != NULL) {
      advance_iterate(advance) {
        Tech_type_id tech = advance_number(advance);

        if (research_invention_state(giver_research, tech) == TECH_KNOWN
            && research_invention_gettable(
                 receiver_research, tech,
                 game.info.tech_trade_allow_holes)
            && (research_invention_state(receiver_research, tech)
                  == TECH_UNKNOWN
                || research_invention_state(receiver_research, tech)
                   == TECH_PREREQS_KNOWN)
            && v2_treaty_candidate_possible(
                 treaty, self, other, giver, CLAUSE_ADVANCE, tech)) {
          v2_buffer_add_relation_action(
            buffer, AGENT_V2_ACTION_DIPLOMACY_PROPOSE_CLAUSE,
            self, other, meeting_generation, clauses_digest,
            self_accepted, other_accepted, giver, CLAUSE_ADVANCE,
            tech, NULL, -1);
        }
      } advance_iterate_end;
    }
    city_list_iterate(giver->cities, city) {
      if (!is_capital(city)
          && city->client.lifecycle_id != 0
          && city_tile(city) != NULL
          && (giver == self
              || player_can_see_city_externals(self, city))
          && v2_existing_incarnation(
               AGENT_V2_ENTITY_CITY, city->id) != 0
          && v2_treaty_candidate_possible(
               treaty, self, other, giver, CLAUSE_CITY, city->id)) {
        v2_buffer_add_relation_action(
          buffer, AGENT_V2_ACTION_DIPLOMACY_PROPOSE_CLAUSE,
          self, other, meeting_generation, clauses_digest,
          self_accepted, other_accepted, giver, CLAUSE_CITY,
          city->id, city, -1);
      }
    } city_list_iterate_end;
  }
  for (type_index = 0; type_index < ARRAY_SIZE(pacts); type_index++) {
    enum clause_type type = pacts[type_index];

    if (v2_treaty_candidate_possible(
          treaty, self, other, self, type, 0)) {
      v2_buffer_add_relation_action(
        buffer, AGENT_V2_ACTION_DIPLOMACY_PROPOSE_CLAUSE,
        self, other, meeting_generation, clauses_digest,
        self_accepted, other_accepted, self, type, 0, NULL, -1);
    }
  }
}

static bool v2_build_relation_scope(
  const char *actor_ref, const char *counterpart_ref,
  struct agent_v2_action *actions, size_t *action_count, bool *overflow)
{
  struct player *self;
  struct player *other;
  struct treaty *treaty;
  struct agent_v2_relation_state *relation;
  struct agent_v2_action_buffer buffer = {
    .actions = actions,
    .count = 0,
    .capacity = FC_AGENT_V2_MAX_RELATION_ACTIONS,
    .overflow = FALSE,
    .export_unknown_rows = FALSE,
    .unknown_exported = NULL
  };
  uint64_t clauses_digest;
  size_t clause_count;
  bool self_accepted = FALSE;
  bool other_accepted = FALSE;
  size_t i;

  if (!v2_resolve_relation_pair(
        actor_ref, counterpart_ref, &self, &other)
      || !self->is_alive || !other->is_alive) {
    return FALSE;
  }
  treaty = find_treaty(self, other);
  relation = v2_relation_state(other, treaty != NULL);
  if (relation == NULL
      || !v2_treaty_clause_summary(
           treaty, &clause_count, &clauses_digest)) {
    return FALSE;
  }
  (void) clause_count;
  if (treaty != NULL) {
    self_accepted = treaty->plr0 == self
                    ? treaty->accept0 : treaty->accept1;
    other_accepted = treaty->plr0 == self
                     ? treaty->accept1 : treaty->accept0;
    v2_buffer_add_relation_action(
      &buffer, AGENT_V2_ACTION_DIPLOMACY_CLOSE_MEETING,
      self, other, relation->meeting_generation, clauses_digest,
      self_accepted, other_accepted, NULL, CLAUSE_COUNT, -1, NULL, -1);
    v2_buffer_add_relation_action(
      &buffer,
      self_accepted
        ? AGENT_V2_ACTION_DIPLOMACY_WITHDRAW_ACCEPTANCE
        : AGENT_V2_ACTION_DIPLOMACY_ACCEPT,
      self, other, relation->meeting_generation, clauses_digest,
      self_accepted, other_accepted, NULL, CLAUSE_COUNT, -1, NULL,
      self_accepted ? 0 : 1);
    clause_list_iterate(treaty->clauses, clause) {
      struct player *giver = clause->from;
      const struct city *city = clause->type == CLAUSE_CITY
                                ? game_city_by_number(clause->value) : NULL;

      if (giver == NULL || !clause_type_is_valid(clause->type)) {
        buffer.overflow = TRUE;
        break;
      }
      v2_buffer_add_relation_action(
        &buffer, AGENT_V2_ACTION_DIPLOMACY_REMOVE_CLAUSE,
        self, other, relation->meeting_generation, clauses_digest,
        self_accepted, other_accepted, giver, clause->type,
        clause->value, city, -1);
    } clause_list_iterate_end;
    if (!buffer.overflow) {
      v2_build_relation_clause_candidates(
        &buffer, self, other, treaty, relation->meeting_generation,
        clauses_digest, self_accepted, other_accepted);
    }
  } else if (can_meet_with_player(other)) {
    v2_buffer_add_relation_action(
      &buffer, AGENT_V2_ACTION_DIPLOMACY_OPEN_MEETING,
      self, other, relation->meeting_generation, clauses_digest,
      FALSE, FALSE, NULL, CLAUSE_COUNT, -1, NULL, -1);
  }
  if (pplayer_can_cancel_treaty(self, other) == DIPL_OK
      && cancel_pact_result(
           player_diplstate_get(self, other)->type)
         != player_diplstate_get(self, other)->type) {
    v2_buffer_add_relation_action(
      &buffer, AGENT_V2_ACTION_DIPLOMACY_BREAK_RELATION,
      self, other, relation->meeting_generation, clauses_digest,
      self_accepted, other_accepted, NULL, CLAUSE_COUNT, -1, NULL, -1);
  }
  if (gives_shared_vision(self, other)) {
    v2_buffer_add_relation_action(
      &buffer, AGENT_V2_ACTION_DIPLOMACY_WITHDRAW_VISION,
      self, other, relation->meeting_generation, clauses_digest,
      self_accepted, other_accepted, NULL, CLAUSE_COUNT, -1, NULL, -1);
  }
  if (gives_shared_tiles(self, other)) {
    v2_buffer_add_relation_action(
      &buffer, AGENT_V2_ACTION_DIPLOMACY_WITHDRAW_SHARED_TILES,
      self, other, relation->meeting_generation, clauses_digest,
      self_accepted, other_accepted, NULL, CLAUSE_COUNT, -1, NULL, -1);
  }
  if (!buffer.overflow) {
    qsort(buffer.actions, buffer.count, sizeof(buffer.actions[0]),
          v2_action_compare);
    for (i = 0; i < buffer.count; i++) {
      v2_assign_slot(&buffer.actions[i]);
    }
  }
  *action_count = buffer.count;
  *overflow = buffer.overflow;
  return TRUE;
}

static bool v2_build_actor_scope(
  const char *actor_ref, struct agent_v2_action *actions,
  size_t *action_count, bool *overflow)
{
  enum agent_v2_entity_kind kind;
  int id;
  uint64_t incarnation;
  struct player *self = client_player();
  struct fc_agent_v2_phase_evidence phase;
  struct agent_v2_action_buffer buffer = {
    .actions = actions,
    .count = 0,
    .capacity = AGENT_V2_MAX_ACTIONS,
    .overflow = FALSE,
    .export_unknown_rows = FALSE,
    .unknown_exported = NULL
  };
  size_t i;

  if (!v2_resolve_owned_actor(actor_ref, &kind, &id, &incarnation)
      || !v2_collect_phase_evidence(self, &phase)) {
    return FALSE;
  }
  if (kind == AGENT_V2_ENTITY_PLAYER) {
    v2_build_player_actions(self, &phase, &buffer);
  }
  if (v2_actions_ready(&phase)) {
    switch (kind) {
    case AGENT_V2_ENTITY_PLAYER:
      v2_build_government_actions(self, &buffer);
      v2_build_multiplier_actions(self, &buffer);
      v2_build_spaceship_actions(self, &buffer);
      break;
    case AGENT_V2_ENTITY_CITY:
      v2_build_city_actions(player_city_by_number(self, id), &buffer);
      v2_build_city_citizen_actions(player_city_by_number(self, id),
                                    &buffer);
      v2_build_city_management_actions(player_city_by_number(self, id),
                                       &buffer);
      v2_build_city_worker_task_actions(player_city_by_number(self, id),
                                        &buffer);
      break;
    case AGENT_V2_ENTITY_UNIT:
      v2_build_unit_actions(player_unit_by_number(self, id), &buffer);
      v2_build_worker_actions(player_unit_by_number(self, id), &buffer);
      v2_build_unit_automation_actions(
        player_unit_by_number(self, id), &buffer);
      v2_build_unit_cancel_orders_action(
        player_unit_by_number(self, id), &buffer);
      v2_build_unit_goto_actions(
        player_unit_by_number(self, id), &buffer);
      v2_build_unit_set_route_action(
        player_unit_by_number(self, id), &buffer);
      v2_build_self_unit_actions(player_unit_by_number(self, id), &buffer);
      v2_build_city_target_unit_actions(
        player_unit_by_number(self, id), &buffer);
      v2_build_transport_actions(player_unit_by_number(self, id), &buffer);
      v2_build_noncombat_mobility_actions(
        player_unit_by_number(self, id), &buffer);
      break;
    }
  }
  if (!buffer.overflow) {
    qsort(buffer.actions, buffer.count, sizeof(buffer.actions[0]),
          v2_action_compare);
    for (i = 0; i < buffer.count; i++) {
      v2_assign_slot(&buffer.actions[i]);
    }
  }
  *action_count = buffer.count;
  *overflow = buffer.overflow;
  return TRUE;
}

static struct agent_v2_scope *v2_scope_by_id(const char *id)
{
  size_t i;

  for (i = 0; i < AGENT_V2_SCOPE_PINNED; i++) {
    if (v2_scopes[i].valid && strcmp(v2_scopes[i].id, id) == 0) {
      return &v2_scopes[i];
    }
  }
  return NULL;
}

static struct agent_v2_state_scope *v2_state_scope_by_id(const char *id)
{
  size_t i;

  for (i = 0; i < AGENT_V2_STATE_SCOPE_PINNED; i++) {
    if (v2_state_scopes[i].valid
        && strcmp(v2_state_scopes[i].id, id) == 0) {
      return &v2_state_scopes[i];
    }
  }
  return NULL;
}

static void v2_state_scope_release(struct agent_v2_state_scope *scope)
{
  if (scope != NULL) {
    FC_FREE(scope->rows);
    memset(scope, 0, sizeof(*scope));
  }
}

static void v2_state_scopes_release_all(void)
{
  size_t i;

  for (i = 0; i < AGENT_V2_STATE_SCOPE_PINNED; i++) {
    v2_state_scope_release(&v2_state_scopes[i]);
  }
  FC_FREE(v2_state_scope_rows);
  v2_state_scope_row_capacity = 0;
  v2_state_scope_total = 0;
  v2_state_scope_bytes = 0;
  v2_state_scope_capture = FALSE;
}

static struct agent_v2_relation_scope *v2_relation_scope_by_id(
  const char *id)
{
  size_t i;

  for (i = 0; i < AGENT_V2_RELATION_SCOPE_PINNED; i++) {
    if (v2_relation_scopes[i].valid
        && strcmp(v2_relation_scopes[i].id, id) == 0) {
      return &v2_relation_scopes[i];
    }
  }
  return NULL;
}

static bool v2_tile_index_valid(int tile_id)
{
  return !map_is_empty() && tile_id >= 0 && tile_id < map_num_tiles();
}

static bool v2_visible_bribe_stack_bounded(const struct player *self,
                                           const struct tile *tile)
{
  size_t count = 0;
  if (self == NULL || tile == NULL) {
    return FALSE;
  }
  unit_list_iterate(tile->units, punit) {
    if (can_player_see_unit(self, punit)
        && ++count > AGENT_V2_MAX_VISIBLE_BRIBE_STACK) {
      return FALSE;
    }
  } unit_list_iterate_end;
  return TRUE;
}

static uint64_t v2_visible_stack_signature(const struct player *self,
                                           int tile_id)
{
  const struct tile *tile;
  uint64_t hash = UINT64_C(1469598103934665603);

  if (!v2_tile_index_valid(tile_id) || self == NULL) {
    return hash;
  }
  tile = index_to_tile(&wld.map, tile_id);
  unit_list_iterate(tile->units, punit) {
    if (can_player_see_unit(self, punit)) {
      hash = v2_hash_bytes(hash, &punit->id, sizeof(punit->id));
      hash = v2_hash_bytes(hash, &punit->hp, sizeof(punit->hp));
      hash = v2_hash_bytes(hash, &punit->moves_left,
                           sizeof(punit->moves_left));
    }
  } unit_list_iterate_end;
  return hash;
}

static uint64_t v2_visible_bribe_stack_signature(
  const struct player *self, int tile_id)
{
  const struct tile *tile;
  uint64_t hash = UINT64_C(1469598103934665603);

  if (!v2_tile_index_valid(tile_id) || self == NULL) {
    return hash;
  }
  tile = index_to_tile(&wld.map, tile_id);
  unit_list_iterate(tile->units, punit) {
    if (can_player_see_unit(self, punit)) {
      int owner = player_number(unit_owner(punit));
      int unit_type = utype_number(unit_type_get(punit));
      uint64_t incarnation = v2_existing_incarnation(
        AGENT_V2_ENTITY_UNIT, punit->id);

      hash = v2_hash_bytes(hash, &punit->id, sizeof(punit->id));
      hash = v2_hash_bytes(hash, &incarnation, sizeof(incarnation));
      hash = v2_hash_bytes(hash, &punit->client.lifecycle_id,
                           sizeof(punit->client.lifecycle_id));
      hash = v2_hash_bytes(hash, &owner, sizeof(owner));
      hash = v2_hash_bytes(hash, &unit_type, sizeof(unit_type));
      hash = v2_hash_bytes(hash, &punit->hp, sizeof(punit->hp));
      hash = v2_hash_bytes(hash, &punit->moves_left,
                           sizeof(punit->moves_left));
    }
  } unit_list_iterate_end;
  return hash;
}

static bool v2_visible_bribe_fingerprint_matches(
  const struct agent_v2_visible_bribe_member *member,
  const struct unit *punit)
{
  const struct player *nationality = unit_nationality(punit);

  return member != NULL && punit != NULL && nationality != NULL
         && member->unit_type == utype_number(unit_type_get(punit))
         && member->nationality == player_number(nationality)
         && member->veteran == punit->veteran
         && member->hp == punit->hp
         && member->moves_left == punit->moves_left
         && member->fuel == punit->fuel
         && member->birth_turn == punit->birth_turn
         && member->current_form_turn == punit->current_form_turn
         && member->paradropped == punit->paradropped;
}

static void v2_visible_bribe_stack_capture(
  const struct player *self, const struct tile *target)
{
  const struct action *native = action_by_number(v2_pending.action.action);

  if (self == NULL || target == NULL || native == NULL
      || native->result != ACTRES_SPY_BRIBE_STACK
      || !v2_pending.before_special_target_exact
      || !v2_visible_bribe_stack_bounded(self, target)) {
    return;
  }
  v2_pending.bribe_visible_baseline_exact = TRUE;
  unit_list_iterate(target->units, punit) {
    struct agent_v2_visible_bribe_member *member;
    const struct player *nationality;

    if (!can_player_see_unit(self, punit)) {
      continue;
    }
    if (v2_pending.bribe_visible_count
        >= AGENT_V2_MAX_VISIBLE_BRIBE_STACK
        || punit->client.lifecycle_id == 0
        || (nationality = unit_nationality(punit)) == NULL) {
      v2_pending.bribe_visible_baseline_exact = FALSE;
      return;
    }
    member = &v2_pending.bribe_visible[v2_pending.bribe_visible_count++];
    member->old_id = punit->id;
    member->old_incarnation = v2_existing_incarnation(
      AGENT_V2_ENTITY_UNIT, punit->id);
    member->old_lifecycle_id = punit->client.lifecycle_id;
    member->unit_type = utype_number(unit_type_get(punit));
    member->nationality = player_number(nationality);
    member->veteran = punit->veteran;
    member->hp = punit->hp;
    member->moves_left = punit->moves_left;
    member->fuel = punit->fuel;
    member->birth_turn = punit->birth_turn;
    member->current_form_turn = punit->current_form_turn;
    member->paradropped = punit->paradropped;
    if (member->old_incarnation == 0) {
      v2_pending.bribe_visible_baseline_exact = FALSE;
      return;
    }
  } unit_list_iterate_end;
}

static void v2_visible_bribe_replacement_observe(
  const struct player *self, const struct unit *punit)
{
  const struct action *native = action_by_number(v2_pending.action.action);
  struct agent_v2_visible_bribe_member *candidate = NULL;
  size_t index;

  if (self == NULL || punit == NULL || native == NULL
      || native->result != ACTRES_SPY_BRIBE_STACK
      || !v2_pending.bribe_visible_baseline_exact
      || unit_owner(punit) != self
      || punit->id == v2_pending.action.unit_id
      || punit->client.lifecycle_id == 0) {
    return;
  }
  for (index = 0; index < v2_pending.bribe_visible_count; index++) {
    struct agent_v2_visible_bribe_member *member =
      &v2_pending.bribe_visible[index];

    if (punit->id == member->old_id) {
      return;
    }
    if (!member->replacement_latched
        && v2_visible_bribe_fingerprint_matches(member, punit)) {
      candidate = member;
      break;
    }
  }
  if (candidate == NULL) {
    return;
  }
  candidate->replacement_latched = TRUE;
  candidate->replacement_id = punit->id;
  candidate->replacement_incarnation = v2_existing_incarnation(
    AGENT_V2_ENTITY_UNIT, punit->id);
  candidate->replacement_lifecycle_id = punit->client.lifecycle_id;
  if (candidate->replacement_incarnation == 0) {
    v2_pending.bribe_visible_mapping_conflict = TRUE;
  }
}

static bool v2_visible_bribe_mapping_matches(const struct player *self)
{
  size_t index;
  if (self == NULL || !v2_pending.bribe_visible_baseline_exact
      || v2_pending.bribe_visible_count == 0
      || v2_pending.bribe_visible_mapping_conflict) {
    return FALSE;
  }
  for (index = 0; index < v2_pending.bribe_visible_count; index++) {
    const struct agent_v2_visible_bribe_member *member =
      &v2_pending.bribe_visible[index];
    struct unit *replacement;

    if (!member->replacement_latched
        || game_unit_by_number(member->old_id) != NULL
        || (replacement = game_unit_by_number(member->replacement_id)) == NULL
        || unit_owner(replacement) != self
        || replacement->client.lifecycle_id
           != member->replacement_lifecycle_id
        || v2_existing_incarnation(AGENT_V2_ENTITY_UNIT, replacement->id)
           != member->replacement_incarnation) {
      return FALSE;
    }
  }
  return TRUE;
}

static bool v2_city_name_valid(const char *name)
{
  size_t length = strlen(name);
  size_t i;

  if (length == 0 || length >= MAX_LEN_CITYNAME
      || !fc_agent_ipc_valid_utf8(name, length)) {
    return FALSE;
  }
  for (i = 0; i < length; i++) {
    unsigned char value = (unsigned char) name[i];

    if (value == '\t' || value == '\n' || value == '\r' || value == 0x7f
        || value == '\0' || value < 0x20) {
      return FALSE;
    }
  }
  return TRUE;
}

static bool v2_parse_nonnegative_int(const char *text, int *value)
{
  unsigned long parsed = 0;
  const char *cursor = text;

  if (text == NULL || value == NULL || *cursor == '\0'
      || (*cursor == '0' && cursor[1] != '\0')) {
    return FALSE;
  }
  while (*cursor >= '0' && *cursor <= '9') {
    unsigned int digit = (unsigned int) (*cursor - '0');

    if (parsed > ((unsigned long) INT_MAX - digit) / 10) {
      return FALSE;
    }
    parsed = parsed * 10 + digit;
    cursor++;
  }
  if (*cursor != '\0') {
    return FALSE;
  }
  *value = (int) parsed;
  return TRUE;
}

static bool v2_parse_bounded_signed_int(const char *text, int minimum,
                                        int maximum, int *value)
{
  bool negative = FALSE;
  int64_t parsed = 0;
  const char *cursor = text;

  if (text == NULL || value == NULL || *cursor == '\0') {
    return FALSE;
  }
  if (*cursor == '-') {
    negative = TRUE;
    cursor++;
    if (*cursor == '\0' || *cursor == '0') {
      return FALSE;
    }
  } else if (*cursor == '+' || (*cursor == '0' && cursor[1] != '\0')) {
    return FALSE;
  }
  while (*cursor >= '0' && *cursor <= '9') {
    parsed = parsed * 10 + (*cursor - '0');
    if (parsed > (int64_t) INT_MAX + 1) {
      return FALSE;
    }
    cursor++;
  }
  if (*cursor != '\0') {
    return FALSE;
  }
  if (negative) {
    parsed = -parsed;
  }
  if (parsed < minimum || parsed > maximum) {
    return FALSE;
  }
  *value = (int) parsed;
  return TRUE;
}

static bool v2_parse_cma_argument(const char *argument,
                                  struct cm_parameter *parameter)
{
  static const char *const names[] = {
    "min_food", "min_production", "min_trade", "min_gold",
    "min_luxury", "min_science", "weight_food", "weight_production",
    "weight_trade", "weight_gold", "weight_luxury", "weight_science",
    "celebration_weight", "require_happy", "maximize_growth"
  };
  char copy[FC_AGENT_IPC_MAX_PAYLOAD + 1];
  int values[ARRAY_SIZE(names)];
  char *cursor;
  size_t i;

  if (argument == NULL || parameter == NULL
      || strlen(argument) >= sizeof(copy)) {
    return FALSE;
  }
  fc_strlcpy(copy, argument, sizeof(copy));
  cursor = copy;
  for (i = 0; i < ARRAY_SIZE(names); i++) {
    char *separator = strchr(cursor, ',');
    size_t prefix_length = strlen(names[i]);
    int minimum = i < 6 ? -100 : 0;
    int maximum = i < 6 ? 100 : (i < 12 ? 25 : (i == 12 ? 50 : 1));

    if ((i + 1 < ARRAY_SIZE(names) && separator == NULL)
        || (i + 1 == ARRAY_SIZE(names) && separator != NULL)) {
      return FALSE;
    }
    if (separator != NULL) {
      *separator = '\0';
    }
    if (strncmp(cursor, names[i], prefix_length) != 0
        || cursor[prefix_length] != '='
        || !v2_parse_bounded_signed_int(
             cursor + prefix_length + 1, minimum, maximum, &values[i])) {
      return FALSE;
    }
    cursor = separator != NULL ? separator + 1 : cursor + strlen(cursor);
  }
  cm_init_parameter(parameter);
  parameter->minimal_surplus[O_FOOD] = values[0];
  parameter->minimal_surplus[O_SHIELD] = values[1];
  parameter->minimal_surplus[O_TRADE] = values[2];
  parameter->minimal_surplus[O_GOLD] = values[3];
  parameter->minimal_surplus[O_LUXURY] = values[4];
  parameter->minimal_surplus[O_SCIENCE] = values[5];
  parameter->factor[O_FOOD] = values[6];
  parameter->factor[O_SHIELD] = values[7];
  parameter->factor[O_TRADE] = values[8];
  parameter->factor[O_GOLD] = values[9];
  parameter->factor[O_LUXURY] = values[10];
  parameter->factor[O_SCIENCE] = values[11];
  parameter->happy_factor = values[12];
  parameter->require_happy = values[13] != 0;
  parameter->max_growth = values[14] != 0;
  return v2_cma_parameter_valid(parameter);
}

static bool v2_parse_worklist_argument(const char *argument,
                                       const struct city *pcity,
                                       struct worklist *result)
{
  static const char prefix[] = "worklist=";
  char copy[FC_AGENT_IPC_MAX_PAYLOAD + 1];
  char *cursor;

  if (argument == NULL || pcity == NULL || result == NULL
      || strncmp(argument, prefix, sizeof(prefix) - 1) != 0
      || strlen(argument) >= sizeof(copy)) {
    return FALSE;
  }
  worklist_init(result);
  fc_strlcpy(copy, argument + sizeof(prefix) - 1, sizeof(copy));
  if (copy[0] == '\0') {
    return TRUE;
  }
  cursor = copy;
  while (*cursor != '\0') {
    char *separator = strchr(cursor, ',');
    char *number;
    int native_id;
    struct universal target;

    if (separator != NULL) {
      *separator = '\0';
    }
    if (strncmp(cursor, "unit:", strlen("unit:")) == 0) {
      number = cursor + strlen("unit:");
      if (!v2_parse_nonnegative_int(number, &native_id)) {
        return FALSE;
      }
      target = universal_by_number(VUT_UTYPE, native_id);
    } else if (strncmp(cursor, "improvement:",
                       strlen("improvement:")) == 0) {
      number = cursor + strlen("improvement:");
      if (!v2_parse_nonnegative_int(number, &native_id)) {
        return FALSE;
      }
      target = universal_by_number(VUT_IMPROVEMENT, native_id);
    } else {
      return FALSE;
    }
    if (!v2_production_supported(&target)
        || !fc_agent_v2_worklist_append_allowed(
          (size_t) worklist_length(result),
          can_city_build_later(&wld.map, pcity, &target),
          (size_t) v2_worklist_count(result, &target),
          (size_t) v2_worklist_count(&pcity->worklist, &target))
        || !worklist_append(result, &target)) {
      return FALSE;
    }
    if (separator == NULL) {
      break;
    }
    cursor = separator + 1;
    if (*cursor == '\0') {
      return FALSE;
    }
  }
  return TRUE;
}

static bool v2_parse_city_options_argument(
  const char *argument, const struct city *pcity,
  bv_city_options *options, enum city_wl_cancel_behavior *wlcb)
{
  static const char prefix[] = "allow_disband=";
  const char *new_citizens;
  bool allow_disband;

  if (argument == NULL || pcity == NULL || options == NULL || wlcb == NULL
      || strncmp(argument, prefix, sizeof(prefix) - 1) != 0) {
    return FALSE;
  }
  if (strncmp(argument + sizeof(prefix) - 1, "0,new_citizens=",
              strlen("0,new_citizens=")) == 0) {
    allow_disband = FALSE;
    new_citizens = argument + sizeof(prefix) - 1
                   + strlen("0,new_citizens=");
  } else if (strncmp(argument + sizeof(prefix) - 1, "1,new_citizens=",
                     strlen("1,new_citizens=")) == 0) {
    allow_disband = TRUE;
    new_citizens = argument + sizeof(prefix) - 1
                   + strlen("1,new_citizens=");
  } else {
    return FALSE;
  }
  if (strcmp(new_citizens, "default") != 0
      && strcmp(new_citizens, "science") != 0
      && strcmp(new_citizens, "gold") != 0) {
    return FALSE;
  }
  *options = pcity->city_options;
  BV_CLR(*options, CITYO_DISBAND);
  BV_CLR(*options, CITYO_SCIENCE_SPECIALISTS);
  BV_CLR(*options, CITYO_GOLD_SPECIALISTS);
  if (allow_disband) {
    BV_SET(*options, CITYO_DISBAND);
  }
  if (strcmp(new_citizens, "science") == 0) {
    BV_SET(*options, CITYO_SCIENCE_SPECIALISTS);
  } else if (strcmp(new_citizens, "gold") == 0) {
    BV_SET(*options, CITYO_GOLD_SPECIALISTS);
  }
  *wlcb = pcity->wlcb;
  return TRUE;
}

static const struct agent_v2_action *v2_action_by_slot(const char *slot)
{
  size_t i;

  for (i = 0; i < v2_current_action_count; i++) {
    if (strcmp(v2_current_actions[i].slot, slot) == 0) {
      return &v2_current_actions[i];
    }
  }
  return NULL;
}

static void v2_pending_clear(void)
{
  if (v2_pending.rally_plan != NULL) {
    client_rally_plan_destroy(v2_pending.rally_plan);
    v2_pending.rally_plan = NULL;
  }
  if (v2_pending.unit_route_plan != NULL) {
    client_unit_route_plan_destroy(v2_pending.unit_route_plan);
    v2_pending.unit_route_plan = NULL;
  }
  if (v2_pending.started_token != NULL) {
    (void) update_queue_cancel_processing_started_direct(
      v2_pending.first_request_id, v2_action_processing_started,
      v2_pending.started_token);
    v2_pending.started_token = NULL;
  }
  if (v2_pending.finished_token != NULL) {
    (void) update_queue_cancel_processing_finished_direct(
      v2_pending.request_id, v2_action_processing_finished,
      v2_pending.finished_token);
    v2_pending.finished_token = NULL;
  }
  if (v2_pending.first_finished_token != NULL) {
    (void) update_queue_cancel_processing_finished_direct(
      v2_pending.first_request_id, v2_action_first_processing_finished,
      v2_pending.first_finished_token);
    v2_pending.first_finished_token = NULL;
  }
  if (v2_pending.last_started_token != NULL) {
    (void) update_queue_cancel_processing_started_direct(
      v2_pending.request_id, v2_action_last_processing_started,
      v2_pending.last_started_token);
    v2_pending.last_started_token = NULL;
  }
  if (v2_pending.timer != NULL) {
    timer_destroy(v2_pending.timer);
  }
  memset(&v2_pending, 0, sizeof(v2_pending));
}

static void v2_target_query_clear(void)
{
  if (v2_target_query.timer != NULL) {
    timer_destroy(v2_target_query.timer);
  }
  memset(&v2_target_query, 0, sizeof(v2_target_query));
}

static void v2_special_revalidation_clear(void)
{
  if (v2_special_revalidation.timer != NULL) {
    timer_destroy(v2_special_revalidation.timer);
  }
  memset(&v2_special_revalidation, 0, sizeof(v2_special_revalidation));
}

static void v2_special_revalidation_desynchronize(const char *detail)
{
  if (v2_special_revalidation.active
      || v2_target_query.detail_query_pending) {
    v2_error(v2_special_revalidation.request, "REVALIDATION_DESYNC", detail);
  }
  v2_special_revalidation_clear();
  v2_special_revalidation_desynchronized = TRUE;
}

static void v2_target_query_desynchronize(const char *detail)
{
  if (v2_target_query.active) {
    v2_error(v2_target_query.request, "STREAM_DESYNC", detail);
  }
  v2_target_query_clear();
  v2_target_query_desynchronized = TRUE;
}

static void v2_invalidate_seat_epoch(const char *reason)
{
  bool had_pending = v2_pending.active;
  enum fc_agent_v2_terminal_result terminal
    = fc_agent_v2_terminal_after_epoch_change(v2_pending.terminal);
  char request[AGENT_V2_TOKEN_MAX + 1] = "";
  char slot[32] = "";
  int request_id = 0;

  if (had_pending) {
    fc_strlcpy(request, v2_pending.request, sizeof(request));
    fc_strlcpy(slot, v2_pending.slot, sizeof(slot));
    request_id = v2_pending.request_id;
  }
  v2_pending_clear();
  memset(v2_snapshots, 0, sizeof(v2_snapshots));
  memset(v2_scopes, 0, sizeof(v2_scopes));
  memset(v2_target_scopes, 0, sizeof(v2_target_scopes));
  v2_state_scopes_release_all();
  memset(v2_relation_scopes, 0, sizeof(v2_relation_scopes));
  memset(v2_relations, 0, sizeof(v2_relations));
  v2_hash = 0;
  v2_have_current = FALSE;
  v2_have_current_phase = FALSE;
  memset(&v2_phase_notice, 0, sizeof(v2_phase_notice));
  v2_overflow = FALSE;
  v2_snapshot_serial = 0;
  v2_scope_serial = 0;
  if (v2_target_query.active) {
    v2_target_query_desynchronize(
      "seat epoch changed during target action discovery");
  } else {
    v2_target_query_clear();
  }
  if (v2_special_revalidation.active) {
    v2_special_revalidation_desynchronize(
      "seat epoch changed during action preflight");
  } else {
    v2_special_revalidation_clear();
  }
  v2_state_scope_serial = 0;
  memset(&v2_investigation, 0, sizeof(v2_investigation));
  v2_current_row_count = 0;
  v2_current_action_count = 0;
  v2_relation_count = 0;
  memset(v2_chat_history, 0, sizeof(v2_chat_history));
  v2_chat_history_start = 0;
  v2_chat_history_count = 0;
  v2_chat_sequence = 0;
  v2_seat_epoch++;
  v2_revision++;
  if (v2_revision == 0) {
    /* A process cannot safely reuse public revisions after uint64 wrap. */
    v2_overflow = TRUE;
  }
  if (had_pending) {
    const char *status = "rejected";
    const char *terminal_reason = reason;

    switch (terminal) {
    case FC_AGENT_V2_TERMINAL_APPLIED:
      status = "applied";
      terminal_reason = "POSTCONDITION_VERIFIED";
      break;
    case FC_AGENT_V2_TERMINAL_POSTCONDITION_NOT_MET:
      terminal_reason = "POSTCONDITION_NOT_MET";
      break;
    case FC_AGENT_V2_TERMINAL_PROCESSING_BOUNDARY_MISMATCH:
      terminal_reason = "PROCESSING_BOUNDARY_MISMATCH";
      break;
    case FC_AGENT_V2_TERMINAL_SEAT_EPOCH_CHANGED:
      terminal_reason = "SEAT_EPOCH_CHANGED";
      break;
    case FC_AGENT_V2_TERMINAL_NONE:
      break;
    }
    v2_sendf("ACT_RESULT\t%s\t%s\t%s\t%s\t%d\t%llu\t-",
             request, slot, status, terminal_reason, request_id,
             (unsigned long long) v2_revision);
  }
}

/* This is deliberately a read-only predicate. Direct update-queue callbacks
 * must not refresh caches, mutate the tracked epoch, or emit IPC. */
static bool v2_exact_seat_epoch_current(void)
{
  struct fc_agent_v2_epoch_identity expected = {
    .authorized = v2_seat_authorized,
    .player = (uintptr_t) v2_seat_player,
    .player_number = v2_seat_player_number,
    .map_tiles = (uintptr_t) v2_seat_map_tiles,
    .map_xsize = v2_seat_map_xsize,
    .map_ysize = v2_seat_map_ysize,
    .map_topology = v2_seat_map_topology,
    .map_wrap = v2_seat_map_wrap,
    .game_epoch = v2_seat_game_epoch
  };
  const struct player *seat;
  struct fc_agent_v2_epoch_identity current;

  if ((client_state() != C_S_RUNNING
       && client_state() != C_S_PREPARING)
      || !v2_seat_known || !v2_seat_authorized
      || v2_authorized == NULL || !v2_authorized()) {
    return FALSE;
  }
  seat = client_player();
  if (seat == NULL) {
    return FALSE;
  }
  current.authorized = TRUE;
  current.player = (uintptr_t) seat;
  current.player_number = player_number(seat);
  current.map_tiles = (uintptr_t) wld.map.tiles;
  current.map_xsize = wld.map.xsize;
  current.map_ysize = wld.map.ysize;
  current.map_topology = wld.map.topology_id;
  current.map_wrap = wld.map.wrap_id;
  current.game_epoch = client_game_epoch();
  return !fc_agent_v2_epoch_changed(TRUE, &expected, &current);
}

static bool v2_sync_seat_epoch(void)
{
  struct fc_agent_v2_epoch_identity previous = {
    .authorized = v2_seat_authorized,
    .player = (uintptr_t) v2_seat_player,
    .player_number = v2_seat_player_number,
    .map_tiles = (uintptr_t) v2_seat_map_tiles,
    .map_xsize = v2_seat_map_xsize,
    .map_ysize = v2_seat_map_ysize,
    .map_topology = v2_seat_map_topology,
    .map_wrap = v2_seat_map_wrap,
    .game_epoch = v2_seat_game_epoch
  };
  bool authorized = (client_state() == C_S_RUNNING
                     || client_state() == C_S_PREPARING)
                    && v2_authorized != NULL && v2_authorized();
  const struct player *seat = authorized ? client_player() : NULL;
  int number = seat != NULL ? player_number(seat) : -1;
  const struct tile *tiles = authorized ? wld.map.tiles : NULL;
  int xsize = authorized ? wld.map.xsize : 0;
  int ysize = authorized ? wld.map.ysize : 0;
  int topology = authorized ? wld.map.topology_id : 0;
  int wrap = authorized ? wld.map.wrap_id : 0;
  uint64_t game_epoch = client_game_epoch();
  struct fc_agent_v2_epoch_identity current = {
    .authorized = authorized,
    .player = (uintptr_t) seat,
    .player_number = number,
    .map_tiles = (uintptr_t) tiles,
    .map_xsize = xsize,
    .map_ysize = ysize,
    .map_topology = topology,
    .map_wrap = wrap,
    .game_epoch = game_epoch
  };
  bool changed = fc_agent_v2_epoch_changed(v2_seat_known, &previous,
                                           &current);

  if (changed) {
    if (v2_pending.active
        && v2_pending.action.kind == AGENT_V2_ACTION_PREGAME_SET_READY
        && v2_pending.action.desired_acceptance == 1
        && v2_seat_client_state == C_S_PREPARING
        && client_state() == C_S_RUNNING
        && seat != NULL && v2_seat_player == seat
        && v2_seat_player_number == number) {
      /* The last ready packet starts the game before its pregame echo can be
       * observed. The exact same pinned seat entering RUNNING is the native
       * postcondition, so preserve an applied receipt across epoch rotation. */
      v2_pending.terminal = FC_AGENT_V2_TERMINAL_APPLIED;
    }
    v2_invalidate_seat_epoch("SEAT_EPOCH_CHANGED");
  }
  v2_seat_known = TRUE;
  v2_seat_authorized = authorized;
  v2_seat_player = seat;
  v2_seat_player_number = number;
  v2_seat_map_tiles = tiles;
  v2_seat_map_xsize = xsize;
  v2_seat_map_ysize = ysize;
  v2_seat_map_topology = topology;
  v2_seat_map_wrap = wrap;
  v2_seat_game_epoch = game_epoch;
  v2_seat_client_state = client_state();
  return authorized;
}

static bool v2_cache_coherent(void)
{
  return (client_state() == C_S_RUNNING
          || client_state() == C_S_PREPARING)
         && fc_agent_v2_boundary_ready(
    v2_seat_authorized,
    client.conn.client.request_id_of_currently_handled_packet,
    fc_agent_v2_agents_busy_if_ready(client_state() == C_S_RUNNING,
                                     agents_busy));
}

static bool v2_processing_idle(void)
{
  return client.conn.client.request_id_of_currently_handled_packet == 0
         && !fc_agent_v2_agents_busy_if_ready(
           client_state() == C_S_RUNNING, agents_busy);
}

static void v2_action_result(const char *status, const char *reason)
{
  const struct action *native = action_by_number(v2_pending.action.action);
  const char *observation = "-";

  if (strcmp(status, "applied") == 0
      && native != NULL
      && native->result == ACTRES_SPY_INVESTIGATE_CITY) {
    uint64_t token_hash;

    v2_investigation_serial++;
    if (v2_investigation_serial == 0) {
      status = "rejected";
      reason = "PROCESSING_BOUNDARY_MISMATCH";
    } else {
      memset(&v2_investigation, 0, sizeof(v2_investigation));
      v2_investigation.valid = TRUE;
      v2_investigation.seat_epoch = v2_seat_epoch;
      v2_investigation.serial = v2_investigation_serial;
      v2_investigation.payload = v2_pending.investigation;
      if (!v2_refresh()) {
        memset(&v2_investigation, 0, sizeof(v2_investigation));
        status = "rejected";
        reason = "PROCESSING_BOUNDARY_MISMATCH";
      } else {
        v2_investigation.revision = v2_revision;
        token_hash = v2_hash_bytes(
          v2_secret ^ v2_investigation.payload.digest,
          &v2_investigation.revision,
          sizeof(v2_investigation.revision));
        token_hash = v2_hash_bytes(
          token_hash, &v2_investigation.serial,
          sizeof(v2_investigation.serial));
        fc_snprintf(
          v2_investigation.token, sizeof(v2_investigation.token),
          "i%016llx", (unsigned long long) token_hash);
        observation = v2_investigation.token;
      }
    }
  }
  v2_sendf("ACT_RESULT\t%s\t%s\t%s\t%s\t%d\t%llu\t%s",
           v2_pending.request, v2_pending.slot, status, reason,
           v2_pending.request_id, (unsigned long long) v2_revision,
           observation);
  v2_pending_clear();
}

static bool v2_transport_action_still_legal(
  const struct player *self, const struct unit *actor,
  const struct agent_v2_action *action, struct unit **target_result);

static bool v2_callback_matches(const struct agent_v2_callback_token *token)
{
  return token != NULL
         && v2_pending.active && token->nonce == v2_pending.nonce
         && (token->request_id == v2_pending.first_request_id
             || token->request_id == v2_pending.request_id);
}

static bv_extras v2_hut_extras_on_tile(const struct tile *ptile)
{
  bv_extras result;

  BV_CLR_ALL(result);
  if (ptile == NULL) {
    return result;
  }
  extra_type_by_rmcause_iterate(ERM_ENTER, pextra) {
    if (tile_has_extra(ptile, pextra)) {
      BV_SET(result, extra_number(pextra));
    }
  } extra_type_by_rmcause_iterate_end;
  return result;
}

static bool v2_hut_extra_removed(const bv_extras *before,
                                 const bv_extras *current)
{
  int extra_id;

  for (extra_id = 0; extra_id < MAX_EXTRA_TYPES; extra_id++) {
    if (BV_ISSET(*before, extra_id) && !BV_ISSET(*current, extra_id)) {
      return TRUE;
    }
  }
  return FALSE;
}

static bv_imprs v2_visible_city_improvements(const struct city *pcity,
                                             bool exact)
{
  bv_imprs result;

  BV_CLR_ALL(result);
  if (pcity == NULL || !exact) {
    return result;
  }
  improvement_iterate(pimprove) {
    if (city_has_building(pcity, pimprove)) {
      BV_SET(result, improvement_number(pimprove));
    }
  } improvement_iterate_end;
  return result;
}

static bool v2_visible_city_improvement_removed(const bv_imprs *before,
                                                const bv_imprs *current)
{
  int improvement_id;

  for (improvement_id = 0; improvement_id < B_LAST; improvement_id++) {
    if (BV_ISSET(*before, improvement_id)
        && !BV_ISSET(*current, improvement_id)) {
      return TRUE;
    }
  }
  return FALSE;
}

static bv_techs v2_known_technologies(const struct research *research)
{
  bv_techs result;

  BV_CLR_ALL(result);
  if (research == NULL) {
    return result;
  }
  advance_index_iterate(A_FIRST, tech) {
    if (research_invention_state(research, tech) == TECH_KNOWN) {
      BV_SET(result, tech);
    }
  } advance_index_iterate_end;
  return result;
}

static bool v2_technology_acquired(const bv_techs *before,
                                   const struct research *current)
{
  if (current == NULL) {
    return FALSE;
  }
  advance_index_iterate(A_FIRST, tech) {
    if (!BV_ISSET(*before, tech)
        && research_invention_state(current, tech) == TECH_KNOWN) {
      return TRUE;
    }
  } advance_index_iterate_end;
  return FALSE;
}

static void v2_action_processing_started(void *data)
{
  const struct agent_v2_callback_token *token = data;
  struct player *self;
  struct unit *unit;
  struct unit *target_unit;
  struct unit *transport_context;
  struct city *city;
  struct city *source_city;
  struct city *destination_city;
  const struct research *research;
  struct player *diplomacy_other;
  struct tile *infrastructure_target;
  struct tile *special_target;
  struct player *special_extra_owner;
  const struct impr_type *target_building;
  int special_target_id = -1;
  int special_subtarget_id = NO_TARGET;

  if (v2_pending.started_token == token) {
    v2_pending.started_token = NULL;
  }
  if (!v2_callback_matches(token)
      || v2_pending.terminal != FC_AGENT_V2_TERMINAL_NONE
      || v2_pending.seat_epoch != v2_seat_epoch
      || !v2_exact_seat_epoch_current()) {
    return;
  }
  self = client_player();
  unit = self != NULL && v2_pending.action.unit_id >= 0
         ? player_unit_by_number(self, v2_pending.action.unit_id) : NULL;
  target_unit = v2_pending.action.target_unit_id >= 0
                ? (v2_pending.action.kind == AGENT_V2_ACTION_UNIT_SPECIAL
                   || (v2_pending.action.kind
                         >= AGENT_V2_ACTION_TRANSPORT_BOARD
                       && v2_pending.action.kind
                          <= AGENT_V2_ACTION_TRANSPORT_UNLOAD)
                   ? game_unit_by_number(v2_pending.action.target_unit_id)
                   : (self != NULL
                      ? player_unit_by_number(
                          self, v2_pending.action.target_unit_id) : NULL))
                : NULL;
  transport_context =
    v2_pending.action.transport_context_id >= 0
    ? game_unit_by_number(v2_pending.action.transport_context_id) : NULL;
  city = self != NULL && v2_pending.action.city_id >= 0
         ? player_city_by_number(self, v2_pending.action.city_id) : NULL;
  source_city = self != NULL && v2_pending.action.source_city_id >= 0
                ? player_city_by_number(
                    self, v2_pending.action.source_city_id) : NULL;
  destination_city =
    self != NULL && v2_pending.action.destination_city_id >= 0
    ? game_city_by_number(v2_pending.action.destination_city_id) : NULL;
  research = self != NULL ? research_get(self) : NULL;
  diplomacy_other = self != NULL
                    && v2_pending.action.counterpart_id >= 0
                    ? player_by_number(
                        v2_pending.action.counterpart_id) : NULL;
  infrastructure_target =
    v2_pending.action.kind == AGENT_V2_ACTION_PLAYER_PLACE_INFRA
    && v2_tile_index_valid(v2_pending.action.target_tile)
    ? index_to_tile(&wld.map, v2_pending.action.target_tile) : NULL;
  special_target =
    v2_pending.action.kind == AGENT_V2_ACTION_UNIT_SPECIAL
    && v2_tile_index_valid(v2_pending.action.target_tile)
    ? index_to_tile(&wld.map, v2_pending.action.target_tile) : NULL;
  special_extra_owner = special_target != NULL
                        ? extra_owner(special_target) : NULL;
  target_building =
    v2_pending.action.target_build_kind == VUT_IMPROVEMENT
    ? improvement_by_number(v2_pending.action.target_build_id) : NULL;
  v2_pending.processing_started = TRUE;
  v2_pending.before_turn = game.info.turn;
  v2_pending.before_phase_done = self != NULL && self->phase_done;
  v2_pending.before_unit_tile = unit != NULL && unit_tile(unit) != NULL
                                ? tile_index(unit_tile(unit)) : -1;
  v2_pending.before_unit_hp = unit != NULL ? unit->hp : -1;
  v2_pending.before_unit_type = unit != NULL
                                ? utype_number(unit_type_get(unit)) : -1;
  v2_pending.before_unit_homecity = unit != NULL ? unit->homecity : -1;
  v2_pending.before_unit_paradropped =
    unit != NULL && unit->paradropped;
  v2_pending.before_unit_ssa = unit != NULL
                               ? unit->ssa_controller : SSA_COUNT;
  v2_pending.before_unit_activity = unit != NULL
                                    ? unit->activity : ACTIVITY_LAST;
  v2_pending.before_unit_activity_target_none =
    unit != NULL && unit->activity_target == NULL;
  v2_pending.before_unit_has_orders = unit != NULL && unit->has_orders;
  v2_pending.before_unit_goto_none =
    unit != NULL && unit->goto_tile == NULL;
  v2_pending.before_unit_untransported =
    unit != NULL && !unit_transported(unit);
  v2_pending.before_unit_cargo_empty =
    unit != NULL && unit_list_size(unit_transport_cargo(unit)) == 0;
  v2_pending.before_infrastructure_points =
    self != NULL ? self->economic.infra_points : -1;
  v2_pending.before_infrastructure_unplaced =
    infrastructure_target != NULL && infrastructure_target->placing == NULL;
  v2_pending.before_special_target_exact =
    special_target != NULL
    && v2_special_action_still_bound(
      self, unit, &v2_pending.action,
      &special_target_id, &special_subtarget_id);
  v2_pending.before_extra_owner = special_extra_owner != NULL
                                  ? player_number(special_extra_owner) : -1;
  v2_pending.before_hut_extras = v2_hut_extras_on_tile(special_target);
  v2_pending.before_unit_present =
    unit != NULL
    && v2_existing_incarnation(AGENT_V2_ENTITY_UNIT, unit->id)
       == v2_pending.action.unit_incarnation
    && unit->client.lifecycle_id != 0
    && unit->client.lifecycle_id == v2_pending.action.unit_lifecycle_id;
  v2_pending.before_unit_lifecycle_id =
    unit != NULL ? unit->client.lifecycle_id : 0;
  v2_pending.before_target_unit_tile =
    target_unit != NULL && unit_tile(target_unit) != NULL
    ? tile_index(unit_tile(target_unit)) : -1;
  v2_pending.before_target_unit_type =
    target_unit != NULL ? utype_number(unit_type_get(target_unit)) : -1;
  v2_pending.before_target_unit_hp =
    target_unit != NULL ? target_unit->hp : -1;
  v2_pending.before_target_unit_present =
    target_unit != NULL
    && v2_existing_incarnation(AGENT_V2_ENTITY_UNIT, target_unit->id)
       == v2_pending.action.target_unit_incarnation
    && target_unit->client.lifecycle_id != 0
    && target_unit->client.lifecycle_id
       == v2_pending.action.target_unit_lifecycle_id;
  v2_pending.before_target_unit_lifecycle_id =
    target_unit != NULL ? target_unit->client.lifecycle_id : 0;
  v2_pending.before_transport_context_tile =
    transport_context != NULL && unit_tile(transport_context) != NULL
    ? tile_index(unit_tile(transport_context)) : -1;
  v2_pending.before_transport_context_present =
    transport_context != NULL
    && v2_existing_incarnation(AGENT_V2_ENTITY_UNIT,
                               transport_context->id)
       == v2_pending.action.transport_context_incarnation
    && transport_context->client.lifecycle_id != 0
    && transport_context->client.lifecycle_id
       == v2_pending.action.transport_context_lifecycle_id;
  v2_pending.before_transport_context_lifecycle_id =
    transport_context != NULL ? transport_context->client.lifecycle_id : 0;
  v2_pending.before_transport_baseline_exact =
    v2_pending.action.kind >= AGENT_V2_ACTION_TRANSPORT_BOARD
    && v2_pending.action.kind <= AGENT_V2_ACTION_TRANSPORT_UNLOAD
    && v2_transport_action_still_legal(
      self, unit, &v2_pending.action, NULL);
  v2_pending.before_source_city_present =
    source_city != NULL
    && source_city->client.lifecycle_id != 0
    && source_city->client.lifecycle_id
       == v2_pending.action.source_city_lifecycle_id
    && v2_existing_incarnation(AGENT_V2_ENTITY_CITY, source_city->id)
       == v2_pending.action.source_city_incarnation;
  v2_pending.before_source_city_lifecycle_id =
    source_city != NULL ? source_city->client.lifecycle_id : 0;
  v2_pending.before_destination_city_present =
    destination_city != NULL
    && destination_city->client.lifecycle_id != 0
    && destination_city->client.lifecycle_id
       == v2_pending.action.destination_city_lifecycle_id
    && v2_existing_incarnation(AGENT_V2_ENTITY_CITY,
                               destination_city->id)
       == v2_pending.action.destination_city_incarnation;
  v2_pending.before_destination_city_lifecycle_id =
    destination_city != NULL ? destination_city->client.lifecycle_id : 0;
  v2_pending.before_destination_city_size =
    destination_city != NULL ? city_size_get(destination_city) : -1;
  v2_pending.before_destination_city_shield_stock =
    destination_city != NULL ? destination_city->shield_stock : -1;
  v2_pending.before_destination_city_owned =
    destination_city != NULL && city_owner(destination_city) == self;
  v2_pending.before_destination_city_internals_exact =
    destination_city != NULL
    && can_player_see_city_internals(self, destination_city);
  v2_pending.before_destination_city_improvements =
    v2_visible_city_improvements(
      destination_city,
      v2_pending.before_destination_city_internals_exact);
  v2_pending.before_target_building_present =
    destination_city != NULL && target_building != NULL
    && is_improvement_visible(target_building)
    && city_has_building(destination_city, target_building);
  v2_pending.before_trade_route_exists =
    source_city != NULL && destination_city != NULL
    && have_cities_trade_route(source_city, destination_city);
  v2_pending.expected_unit_population =
    unit != NULL ? unit_pop_value(unit) : 0;
  v2_pending.expected_help_shields =
    unit != NULL
    && (v2_pending.action.kind == AGENT_V2_ACTION_UNIT_HELP_WONDER
        || v2_pending.action.kind
           == AGENT_V2_ACTION_UNIT_DISBAND_RECOVER)
    && action_by_number(v2_pending.action.action) != NULL
    ? unit_shield_value(unit, unit_type_get(unit),
                        action_by_number(v2_pending.action.action)) : 0;
  v2_pending.before_target_signature = v2_visible_stack_signature(
    self, v2_pending.action.target_tile);
  v2_visible_bribe_stack_capture(self, special_target);
  v2_pending.before_research_target = research != NULL
                                      ? research->researching : A_UNSET;
  v2_pending.before_research_exact = research != NULL;
  v2_pending.before_known_techs = v2_known_technologies(research);
  v2_pending.before_future_tech = research != NULL
                                  ? research->future_tech : -1;
  v2_pending.before_city_did_buy = city != NULL && city->did_buy;
  v2_pending.before_city_present =
    city != NULL
    && v2_existing_incarnation(AGENT_V2_ENTITY_CITY, city->id)
       == v2_pending.action.city_incarnation
    && city->client.lifecycle_id != 0
    && city->client.lifecycle_id == v2_pending.action.city_lifecycle_id;
  v2_pending.before_city_lifecycle_id =
    city != NULL ? city->client.lifecycle_id : 0;
  v2_pending.before_city_shield_stock = city != NULL
                                        ? city->shield_stock : -1;
  v2_pending.before_city_specialists = city != NULL
                                        ? city_specialists(city) : -1;
  v2_pending.before_source_specialists =
    city != NULL
    && is_normal_specialist_id(v2_pending.action.source_specialist)
    ? city->specialists[v2_pending.action.source_specialist] : -1;
  v2_pending.before_target_specialists =
    city != NULL
    && is_normal_specialist_id(v2_pending.action.target_specialist)
    ? city->specialists[v2_pending.action.target_specialist] : -1;
  v2_pending.before_city_tile_worked =
    city != NULL && v2_tile_index_valid(v2_pending.action.target_tile)
    && tile_worked(index_to_tile(&wld.map,
                                 v2_pending.action.target_tile)) == city;
  {
    const struct worker_task *task =
      city != NULL && v2_tile_index_valid(v2_pending.action.target_tile)
      ? v2_city_worker_task_at(
          city, index_to_tile(&wld.map, v2_pending.action.target_tile)) : NULL;

    v2_pending.before_worker_task_present = task != NULL;
    v2_pending.before_worker_task_activity = task != NULL
                                                   ? task->act : ACTIVITY_LAST;
    v2_pending.before_worker_task_extra = task != NULL && task->tgt != NULL
                                          ? extra_number(task->tgt) : EXTRA_NONE;
    v2_pending.before_worker_task_want = task != NULL ? task->want : 0;
  }
  v2_pending.before_player_gold = self != NULL ? self->economic.gold : -1;
  v2_pending.before_buy_cost = city != NULL ? city->client.buy_cost : -1;
  if (city != NULL) {
    worklist_copy(&v2_pending.before_city_worklist, &city->worklist);
    v2_pending.before_city_options = city->city_options;
    v2_pending.before_city_wlcb = city->wlcb;
    fc_strlcpy(v2_pending.before_city_name, city_name_get(city),
               sizeof(v2_pending.before_city_name));
  } else {
    worklist_init(&v2_pending.before_city_worklist);
    BV_CLR_ALL(v2_pending.before_city_options);
    v2_pending.before_city_wlcb = WLCB_LAST;
    v2_pending.before_city_name[0] = '\0';
  }
  v2_pending.before_city_did_sell = city != NULL && city->did_sell;
  v2_pending.before_city_had_improvement =
    city != NULL && v2_pending.desired_improvement != NULL
    && city_has_building(city, v2_pending.desired_improvement);
  v2_pending.before_city_source_tile =
    city != NULL && city_tile(city) != NULL
    ? tile_index(city_tile(city)) : -1;
  v2_pending.before_city_rally_active = v2_city_rally_active(city);
  v2_pending.before_city_rally_persistent =
    v2_pending.before_city_rally_active && city->rally_point.persistent;
  v2_pending.before_city_rally_vigilant =
    v2_pending.before_city_rally_active && city->rally_point.vigilant;
  v2_pending.before_city_rally_order_count =
    v2_pending.before_city_rally_active
    ? (int) city->rally_point.length : 0;
  v2_pending.before_city_rally_orders_digest =
    v2_pending.before_city_rally_active
    ? v2_city_rally_orders_digest(city) : 0;
  v2_pending.before_government = self != NULL
                                 ? v2_government_id(
                                     government_of_player(self)) : -1;
  v2_pending.before_target_government = self != NULL
                                        ? v2_government_id(
                                            self->target_government) : -1;
  v2_pending.before_revolution_finishes = self != NULL
                                           ? self->revolution_finishes : -1;
  if (self != NULL) {
    v2_pending.before_spaceship = self->spaceship;
  } else {
    spaceship_init(&v2_pending.before_spaceship);
  }
  v2_pending.before_spaceship_year = game.info.year;
  v2_pending.before_multiplier_count = multiplier_count();
  if (self == NULL || v2_pending.before_multiplier_count < 0
      || v2_pending.before_multiplier_count > MAX_NUM_MULTIPLIERS) {
    v2_pending.terminal =
      FC_AGENT_V2_TERMINAL_PROCESSING_BOUNDARY_MISMATCH;
  } else {
    int multiplier_id;

    for (multiplier_id = 0;
         multiplier_id < v2_pending.before_multiplier_count;
         multiplier_id++) {
      struct multiplier *pmul = multiplier_by_number(multiplier_id);

      if (pmul == NULL) {
        v2_pending.terminal =
          FC_AGENT_V2_TERMINAL_PROCESSING_BOUNDARY_MISMATCH;
        break;
      }
      v2_pending.before_multiplier_values[multiplier_id] =
        player_multiplier_value(self, pmul);
      v2_pending.before_multiplier_targets[multiplier_id] =
        player_multiplier_target_value(self, pmul);
    }
  }
  v2_pending.before_diplstate = self != NULL && diplomacy_other != NULL
                                ? player_diplstate_get(
                                    self, diplomacy_other)->type : DS_LAST;
  v2_pending.before_gives_vision =
    self != NULL && diplomacy_other != NULL
    && gives_shared_vision(self, diplomacy_other);
  v2_pending.before_gives_shared_tiles =
    self != NULL && diplomacy_other != NULL
    && gives_shared_tiles(self, diplomacy_other);
  if (v2_pending.action.kind
        >= AGENT_V2_ACTION_DIPLOMACY_OPEN_MEETING
      && v2_pending.action.kind
         <= AGENT_V2_ACTION_DIPLOMACY_WITHDRAW_SHARED_TILES
      && !v2_relation_action_still_legal(
           &v2_pending.action,
           v2_pending.action.kind
             == AGENT_V2_ACTION_DIPLOMACY_PROPOSE_CLAUSE
             && v2_pending.action.clause_type == CLAUSE_GOLD
             ? v2_pending.action.clause_value : -1)) {
    /* A request already escaped against a different relation baseline.  Its
     * outcome is ambiguous.  Revalidate the complete treaty generation,
     * canonical clause digest, both acceptance bits, relation state, and
     * outgoing grants before any echo callback is allowed to latch.  This
     * prevents a close/reopen or acceptance race in the server queue from
     * being attributed to this request. */
    v2_pending.terminal =
      FC_AGENT_V2_TERMINAL_PROCESSING_BOUNDARY_MISMATCH;
  }
  if ((v2_pending.action.kind
         == AGENT_V2_ACTION_CITY_REQUEST_WORKER_TASK
       || v2_pending.action.kind
          == AGENT_V2_ACTION_CITY_CHANGE_WORKER_TASK
       || v2_pending.action.kind
          == AGENT_V2_ACTION_CITY_REMOVE_WORKER_TASK)
      && (v2_pending.before_worker_task_present
            != v2_pending.action.worker_task_baseline_present
          || v2_pending.before_worker_task_activity
             != v2_pending.action.worker_task_baseline_activity
          || v2_pending.before_worker_task_extra
             != v2_pending.action.worker_task_baseline_extra
          || v2_pending.before_worker_task_want
             != v2_pending.action.worker_task_baseline_want)) {
    /* The request escaped against a different task slot. Never attribute a
     * later matching cache state to this command. */
    v2_pending.terminal =
      FC_AGENT_V2_TERMINAL_PROCESSING_BOUNDARY_MISMATCH;
  }
  v2_pending.baseline_captured = TRUE;
  if (v2_pending.request_count == 1) {
    v2_pending.last_processing_started = TRUE;
  }
}

static void v2_action_first_processing_finished(void *data)
{
  const struct agent_v2_callback_token *token = data;

  if (v2_pending.first_finished_token == token) {
    v2_pending.first_finished_token = NULL;
  }
  if (!v2_callback_matches(token)
      || token->request_id != v2_pending.first_request_id
      || v2_pending.request_count != 2
      || v2_pending.terminal != FC_AGENT_V2_TERMINAL_NONE) {
    return;
  }
  if (!v2_pending.processing_started || !v2_pending.baseline_captured
      || v2_pending.seat_epoch != v2_seat_epoch
      || !v2_exact_seat_epoch_current()) {
    v2_pending.terminal = FC_AGENT_V2_TERMINAL_PROCESSING_BOUNDARY_MISMATCH;
    return;
  }
  v2_pending.first_processing_finished = TRUE;
}

static void v2_action_last_processing_started(void *data)
{
  const struct agent_v2_callback_token *token = data;

  if (v2_pending.last_started_token == token) {
    v2_pending.last_started_token = NULL;
  }
  if (!v2_callback_matches(token)
      || token->request_id != v2_pending.request_id
      || v2_pending.request_count != 2
      || v2_pending.terminal != FC_AGENT_V2_TERMINAL_NONE) {
    return;
  }
  if (!v2_pending.processing_started || !v2_pending.baseline_captured
      || !v2_pending.first_processing_finished
      || v2_pending.seat_epoch != v2_seat_epoch
      || !v2_exact_seat_epoch_current()) {
    v2_pending.terminal = FC_AGENT_V2_TERMINAL_PROCESSING_BOUNDARY_MISMATCH;
    return;
  }
  v2_pending.last_processing_started = TRUE;
}

static bool v2_pending_automation_start_baseline_exact(void)
{
  return v2_pending.before_unit_present
         && v2_pending.before_unit_lifecycle_id
            == v2_pending.action.unit_lifecycle_id
         && v2_pending.before_unit_ssa == SSA_NONE
         && v2_pending.before_unit_activity == ACTIVITY_IDLE
         && v2_pending.before_unit_activity_target_none
         && !v2_pending.before_unit_has_orders
         && v2_pending.before_unit_goto_none;
}

static bool v2_pending_unit_lifetime_current(void)
{
  struct player *self = client_player();
  struct unit *unit = self != NULL
                      ? player_unit_by_number(
                          self, v2_pending.action.unit_id) : NULL;

  return unit != NULL
         && unit->client.lifecycle_id != 0
         && unit->client.lifecycle_id == v2_pending.action.unit_lifecycle_id
         && v2_existing_incarnation(AGENT_V2_ENTITY_UNIT, unit->id)
            == v2_pending.action.unit_incarnation;
}

static bool v2_pending_unit_absent(void)
{
  return game_unit_by_number(v2_pending.action.unit_id) == NULL;
}

static void v2_action_processing_finished(void *data)
{
  const struct agent_v2_callback_token *token = data;
  bool seat_epoch_current;
  bool postcondition_met;

  if (v2_pending.finished_token == token) {
    v2_pending.finished_token = NULL;
  }
  if (!v2_callback_matches(token)
      || v2_pending.terminal != FC_AGENT_V2_TERMINAL_NONE) {
    return;
  }
  seat_epoch_current = v2_pending.seat_epoch == v2_seat_epoch
                       && v2_exact_seat_epoch_current();
  postcondition_met = seat_epoch_current
                      && v2_pending.processing_started
                      && v2_pending.baseline_captured
                      && v2_pending.last_processing_started
                      && (v2_pending.request_count == 1
                          || v2_pending.first_processing_finished)
                      && v2_action_postcondition();
  if (v2_pending.action.kind == AGENT_V2_ACTION_PLAYER_CAST_VOTE) {
    v2_pending.terminal = postcondition_met
                          ? FC_AGENT_V2_TERMINAL_APPLIED
                          : FC_AGENT_V2_TERMINAL_PROCESSING_BOUNDARY_MISMATCH;
    return;
  } else if (v2_pending.action.kind == AGENT_V2_ACTION_UNIT_SPECIAL) {
    /* A rejected packet and a stochastic action whose desired effect failed
     * can have the same final cache. Only a positive family postcondition is
     * safe to call applied; every other post-send outcome is ambiguous and
     * must never be retried automatically. */
    const struct action *native =
      action_by_number(v2_pending.action.action);
    bool paid = v2_paid_special_action(native);
    bool exact_boundaries = seat_epoch_current
                            && v2_pending.processing_started
                            && v2_pending.baseline_captured
                            && v2_pending.first_processing_finished
                            && v2_pending.last_processing_started;

    if (postcondition_met) {
      v2_pending.terminal = FC_AGENT_V2_TERMINAL_APPLIED;
    } else if (paid && exact_boundaries
               && v2_pending.paid_failure_event_latched) {
      /* The shared failure event proves processing, but not non-mutation:
       * infiltration combat may consume the spy or defenders, and incite
       * failures may lose gold. It is therefore terminal ambiguous just like
       * a missing positive paid receipt, and must never be replayed. */
      v2_pending.terminal =
        FC_AGENT_V2_TERMINAL_PROCESSING_BOUNDARY_MISMATCH;
    } else {
      v2_pending.terminal =
        FC_AGENT_V2_TERMINAL_PROCESSING_BOUNDARY_MISMATCH;
    }
    return;
  }
  if (v2_pending.action.kind == AGENT_V2_ACTION_UNIT_CANCEL_ORDERS
      || v2_pending.action.kind == AGENT_V2_ACTION_UNIT_GOTO
      || v2_pending.action.kind == AGENT_V2_ACTION_UNIT_GOTO_AND_PERFORM
      || v2_pending.action.kind == AGENT_V2_ACTION_UNIT_CONNECT_ROUTE
      || v2_pending.action.kind == AGENT_V2_ACTION_UNIT_SET_ROUTE) {
    /* Either normal client request may already have changed the route.
     * Anything short of both exact boundaries and the command's exact route
     * postcondition is therefore ambiguous, never a clean rejection. */
    v2_pending.terminal = fc_agent_v2_capture_group_terminal(
      seat_epoch_current, v2_pending.processing_started,
      v2_pending.baseline_captured,
      v2_pending.first_processing_finished,
      v2_pending.last_processing_started, postcondition_met);
    return;
  }
  if (v2_pending.action.kind == AGENT_V2_ACTION_CITY_SET_RALLY
      || v2_pending.action.kind == AGENT_V2_ACTION_CITY_CLEAR_RALLY) {
    v2_pending.terminal = fc_agent_v2_rally_terminal(
      seat_epoch_current, v2_pending.processing_started,
      v2_pending.baseline_captured,
      v2_pending.last_processing_started, postcondition_met);
    return;
  }
  if (v2_pending.action.kind
        == AGENT_V2_ACTION_CITY_REQUEST_WORKER_TASK
      || v2_pending.action.kind
         == AGENT_V2_ACTION_CITY_CHANGE_WORKER_TASK
      || v2_pending.action.kind
         == AGENT_V2_ACTION_CITY_REMOVE_WORKER_TASK) {
    v2_pending.terminal = fc_agent_v2_rally_terminal(
      seat_epoch_current, v2_pending.processing_started,
      v2_pending.baseline_captured,
      v2_pending.last_processing_started, postcondition_met);
    return;
  }
  if (v2_pending.action.kind == AGENT_V2_ACTION_UNIT_AUTO_WORK
      || v2_pending.action.kind == AGENT_V2_ACTION_UNIT_AUTO_EXPLORE
      || v2_pending.action.kind == AGENT_V2_ACTION_UNIT_CANCEL_AUTOMATION) {
    /* Request one may already have cleared the SSA. A later rejection,
     * disappearance, or stale final cache can no longer prove non-application. */
    v2_pending.terminal = fc_agent_v2_automation_terminal(
      v2_automation_command(v2_pending.action.kind), seat_epoch_current,
      v2_pending.processing_started,
      v2_pending.baseline_captured, v2_pending.first_processing_finished,
      v2_pending.last_processing_started,
      v2_pending_automation_start_baseline_exact(),
      v2_pending_unit_lifetime_current(), postcondition_met);
    return;
  }
  if (v2_pending.action.kind == AGENT_V2_ACTION_UNIT_JOIN_CITY
      || v2_pending.action.kind == AGENT_V2_ACTION_UNIT_ESTABLISH_TRADE
      || v2_pending.action.kind == AGENT_V2_ACTION_UNIT_MARKETPLACE
      || v2_pending.action.kind == AGENT_V2_ACTION_UNIT_HELP_WONDER
      || v2_pending.action.kind
         == AGENT_V2_ACTION_UNIT_DISBAND_RECOVER) {
    v2_pending.terminal = fc_agent_v2_consuming_city_terminal(
      seat_epoch_current, v2_pending.processing_started,
      v2_pending.baseline_captured,
      v2_pending.first_processing_finished,
      v2_pending.last_processing_started,
      v2_pending_unit_lifetime_current(),
      v2_pending_unit_absent(), postcondition_met);
    return;
  }
  fc_agent_v2_capture_terminal(
    &v2_pending.terminal, seat_epoch_current,
    v2_pending.processing_started
      && v2_pending.last_processing_started
      && (v2_pending.request_count == 1
          || v2_pending.first_processing_finished),
    v2_pending.baseline_captured,
    postcondition_met);
}

static void v2_worker_task_observer(
  const struct packet_worker_task *packet, const struct city *pcity,
  int request_id, void *data)
{
  const struct player *self = client_player();
  const struct worker_task *cached = NULL;
  const struct tile *target = NULL;
  bool remove;
  bool exact_cache = FALSE;
  int expected_activity;
  int expected_extra;
  int expected_want;

  (void) data;
  if (packet == NULL || pcity == NULL
      || (v2_pending.action.kind
            != AGENT_V2_ACTION_CITY_REQUEST_WORKER_TASK
          && v2_pending.action.kind
             != AGENT_V2_ACTION_CITY_CHANGE_WORKER_TASK
          && v2_pending.action.kind
             != AGENT_V2_ACTION_CITY_REMOVE_WORKER_TASK)) {
    return;
  }
  remove = !v2_pending.desired_worker_task_present;
  expected_activity = remove ? ACTIVITY_LAST
                              : v2_pending.desired_activity;
  expected_extra = remove ? EXTRA_NONE : v2_pending.desired_extra;
  expected_want = remove ? 0 : v2_pending.desired_worker_task_want;
  if (v2_tile_index_valid(v2_pending.action.target_tile)) {
    target = index_to_tile(&wld.map, v2_pending.action.target_tile);
    cached = v2_city_worker_task_at(pcity, target);
  }
  if (remove) {
    exact_cache = cached == NULL;
  } else if (cached != NULL) {
    exact_cache = cached->act == v2_pending.desired_activity
                  && (cached->tgt != NULL
                      ? extra_number(cached->tgt) : EXTRA_NONE)
                     == v2_pending.desired_extra
                  && cached->want == v2_pending.desired_worker_task_want;
  }
  if (self != NULL && city_owner(pcity) == self
      && pcity->client.lifecycle_id
         == v2_pending.action.city_lifecycle_id
      && v2_existing_incarnation(AGENT_V2_ENTITY_CITY, pcity->id)
         == v2_pending.action.city_incarnation
      && fc_agent_v2_worker_task_echo_matches(
           v2_pending.active, v2_pending.baseline_captured,
           v2_pending.seat_epoch == v2_seat_epoch
             && v2_exact_seat_epoch_current(),
           request_id, v2_pending.request_id,
           packet->city_id, v2_pending.action.city_id,
           packet->tile_id, v2_pending.action.target_tile,
           packet->activity, expected_activity,
           packet->tgt, expected_extra,
           packet->want, expected_want, exact_cache)) {
    v2_pending.worker_task_echo_latched = TRUE;
  }
}

static void v2_full_unit_info_observer(const struct unit *punit,
                                       int request_id, void *data)
{
  struct player *self;
  uint64_t incarnation;
  const struct action *native;

  (void) data;
  if (punit == NULL) {
    return;
  }
  self = client_player();
  incarnation = v2_existing_incarnation(AGENT_V2_ENTITY_UNIT, punit->id);
  native = action_by_number(v2_pending.action.action);
  if (v2_pending.active && v2_pending.baseline_captured
      && v2_pending.seat_epoch == v2_seat_epoch
      && v2_exact_seat_epoch_current()
      && request_id == v2_pending.request_id
      && v2_pending.request_count == 2 && self != NULL
      && unit_owner(punit) == self
      && punit->id == v2_pending.action.unit_id
      && punit->client.lifecycle_id != 0
      && punit->client.lifecycle_id
         == v2_pending.action.unit_lifecycle_id
      && incarnation == v2_pending.action.unit_incarnation
      && (v2_pending.action.kind == AGENT_V2_ACTION_UNIT_GOTO
          || v2_pending.action.kind
             == AGENT_V2_ACTION_UNIT_GOTO_AND_PERFORM
          || v2_pending.action.kind
             == AGENT_V2_ACTION_UNIT_CONNECT_ROUTE)
      && unit_tile(punit) != NULL
      && tile_index(unit_tile(punit))
         == v2_pending.requested_unit_source_tile
      && punit->has_orders
      && punit->orders.length == v2_pending.desired_route_order_count
      && punit->orders.index == 0
      && punit->orders.list != NULL
      && unit_orders_digest(punit->orders.length, punit->orders.list)
         == v2_pending.desired_route_orders_digest
      && punit->orders.repeat == v2_pending.desired_route_repeat
      && punit->orders.vigilant == v2_pending.desired_route_vigilant
      && punit->goto_tile != NULL
      && tile_index(punit->goto_tile)
         == v2_pending.desired_route_destination_tile) {
    v2_pending.exact_route_state_latched = TRUE;
  }
  if (v2_pending.active && v2_pending.baseline_captured
      && v2_pending.seat_epoch == v2_seat_epoch
      && v2_exact_seat_epoch_current()
      && request_id == v2_pending.request_id && self != NULL
      && native != NULL) {
    v2_visible_bribe_replacement_observe(self, punit);
  }
  if (v2_pending.active && v2_pending.baseline_captured
      && v2_pending.seat_epoch == v2_seat_epoch
      && v2_exact_seat_epoch_current()
      && request_id == v2_pending.request_id && self != NULL
      && native != NULL && native->result == ACTRES_SPY_BRIBE_UNIT
      && unit_owner(punit) == self
      && punit->id != v2_pending.action.unit_id
      && punit->id != v2_pending.action.target_unit_id
      && punit->client.lifecycle_id != 0
      && punit->client.lifecycle_id
         != v2_pending.action.target_unit_lifecycle_id
      && utype_number(unit_type_get(punit))
         == v2_pending.before_target_unit_type
      && punit->homecity == v2_pending.before_unit_homecity) {
    if (!v2_pending.paid_replacement_latched) {
      v2_pending.paid_replacement_latched = TRUE;
      v2_pending.paid_replacement_unit_id = punit->id;
      v2_pending.paid_replacement_unit_incarnation = incarnation;
      v2_pending.paid_replacement_unit_lifecycle_id =
        punit->client.lifecycle_id;
    } else if (punit->id != v2_pending.paid_replacement_unit_id
               || incarnation
                  != v2_pending.paid_replacement_unit_incarnation
               || punit->client.lifecycle_id
                  != v2_pending.paid_replacement_unit_lifecycle_id) {
      v2_pending.paid_replacement_conflict = TRUE;
    }
  }
  if (v2_pending.request_count != 1) {
    return;
  }
  if (!fc_agent_v2_unit_automation_latch_matches(
        v2_automation_command(v2_pending.action.kind),
        v2_pending.active,
        v2_pending.seat_epoch == v2_seat_epoch
          && v2_exact_seat_epoch_current(),
        request_id, v2_pending.request_id,
        self != NULL && unit_owner(punit) == self,
        punit->id, v2_pending.action.unit_id,
        punit->client.lifecycle_id,
        v2_pending.action.unit_lifecycle_id,
        incarnation, v2_pending.action.unit_incarnation,
        v2_automation_controller(punit->ssa_controller),
        punit->activity == ACTIVITY_EXPLORE,
        punit->activity_target == NULL)) {
    return;
  }
  v2_pending.exact_unit_state_latched = TRUE;
}

static void v2_unit_combat_info_observer(
  const struct packet_unit_combat_info *packet, int request_id, void *data)
{
  const struct action *native;
  struct unit *defender;
  bool classic_combat_action;
  bool actor_binding_exact;
  bool defender_on_expected_target;

  (void) data;
  if (packet == NULL) {
    return;
  }
  native = action_by_number(v2_pending.action.action);
  classic_combat_action =
    (v2_pending.action.kind == AGENT_V2_ACTION_ATTACK
     && v2_classic_immediate_combat_action(native))
    || (v2_pending.action.kind == AGENT_V2_ACTION_UNIT_SPECIAL
        && v2_classic_collect_ransom_action(native));
  actor_binding_exact =
    v2_pending.before_unit_present
    && v2_pending.action.unit_lifecycle_id != 0
    && v2_pending.before_unit_lifecycle_id
       == v2_pending.action.unit_lifecycle_id;
  defender = game_unit_by_number(packet->defender_unit_id);
  defender_on_expected_target =
    defender != NULL && unit_tile(defender) != NULL
    && tile_index(unit_tile(defender)) == v2_pending.action.target_tile;
  if (fc_agent_v2_combat_observer_matches(
        v2_pending.active, v2_pending.baseline_captured,
        v2_pending.seat_epoch == v2_seat_epoch
          && v2_exact_seat_epoch_current(),
        classic_combat_action, actor_binding_exact,
        request_id, v2_pending.request_id,
        packet->attacker_unit_id, v2_pending.action.unit_id,
        packet->defender_unit_id, defender_on_expected_target)) {
    v2_pending.combat_info_latched = TRUE;
  }
}

static bool v2_nullable_text_equal(const char *left, const char *right)
{
  return left == NULL ? right == NULL
                      : right != NULL && strcmp(left, right) == 0;
}

static bool v2_tag_has_color(const struct text_tag *tag,
                             const struct ft_color *color,
                             size_t plain_length)
{
  return tag != NULL && color != NULL && text_tag_type(tag) == TTT_COLOR
         && text_tag_start_offset(tag) == 0
         && text_tag_stop_offset(tag) == (ft_offset_t) plain_length
         && v2_nullable_text_equal(text_tag_color_foreground(tag),
                                   color->foreground)
         && v2_nullable_text_equal(text_tag_color_background(tag),
                                   color->background);
}

static const char *v2_chat_channel(enum event_type event,
                                   const struct text_tag_list *tags,
                                   size_t plain_length)
{
  if (event != E_CHAT_MSG) {
    return "event";
  }
  text_tag_list_iterate(tags, tag) {
    if (v2_tag_has_color(tag, &ftc_chat_public, plain_length)) {
      return "global";
    }
    if (v2_tag_has_color(tag, &ftc_chat_ally, plain_length)) {
      return "allied";
    }
    if (v2_tag_has_color(tag, &ftc_chat_private, plain_length)) {
      return "private";
    }
  } text_tag_list_iterate_end;
  return "chat";
}

static void v2_capture_chat_packet(const struct packet_chat_msg *packet,
                                   const char *plain,
                                   const char *channel)
{
  struct agent_v2_chat_entry *entry;
  struct connection *sender;
  const char *event;
  const char *name = "server";
  const char *kind = "server";
  size_t slot;
  size_t plain_length;

  if (!v2_seat_authorized || !v2_exact_seat_epoch_current()
      || client_state() != C_S_RUNNING || packet == NULL || plain == NULL
      || channel == NULL || plain[0] == '\0'
      || v2_chat_sequence == UINT64_MAX) {
    return;
  }
  sender = conn_by_number(packet->conn_id);
  if (sender != NULL) {
    if (sender->playing != NULL && !sender->observer) {
      kind = "player";
      name = player_name(sender->playing);
    } else {
      kind = sender->observer ? "observer" : "connection";
      name = sender->username[0] != '\0' ? sender->username : "unknown";
    }
  } else if (packet->conn_id >= 0) {
    kind = "unknown";
    name = "unknown";
  }
  event = get_event_tag(packet->event);
  if (event == NULL || event[0] == '\0') {
    event = "unknown";
  }
  if (v2_chat_history_count < FC_AGENT_V2_MAX_CHAT_HISTORY) {
    slot = (v2_chat_history_start + v2_chat_history_count)
           % FC_AGENT_V2_MAX_CHAT_HISTORY;
    v2_chat_history_count++;
  } else {
    slot = v2_chat_history_start;
    v2_chat_history_start = (v2_chat_history_start + 1)
                            % FC_AGENT_V2_MAX_CHAT_HISTORY;
  }
  entry = &v2_chat_history[slot];
  memset(entry, 0, sizeof(*entry));
  entry->sequence = ++v2_chat_sequence;
  entry->turn = packet->turn;
  entry->phase = packet->phase;
  entry->self = sender != NULL && sender->id == client.conn.id;
  plain_length = strlen(plain);
  entry->truncated = plain_length > FC_AGENT_V2_MAX_CHAT_MESSAGE_BYTES;
  fc_utf8_strlcpy_trunc(entry->message, plain, sizeof(entry->message));
  fc_strlcpy(entry->sender_kind, kind, sizeof(entry->sender_kind));
  fc_utf8_strlcpy_trunc(entry->sender_name, name,
                        sizeof(entry->sender_name));
  fc_strlcpy(entry->channel, channel, sizeof(entry->channel));
  fc_strlcpy(entry->event, event, sizeof(entry->event));
}

static void v2_chat_msg_observer(
  const struct packet_chat_msg *packet, int request_id, void *data)
{
  const struct action *native;
  enum event_type city_success_event;
  bool actor_binding_exact;
  bool city_binding_exact;
  struct text_tag_list *tags = NULL;
  char plain[MAX_LEN_MSG];
  size_t plain_length;
  const char *channel;

  (void) data;
  if (packet == NULL) {
    return;
  }
  plain_length = featured_text_to_plain_text(
    packet->message, plain, sizeof(plain), &tags, TRUE);
  channel = v2_chat_channel(packet->event, tags, plain_length);
  v2_capture_chat_packet(packet, plain, channel);

  if (v2_pending.active && v2_pending.baseline_captured
      && request_id == v2_pending.request_id
      && v2_pending.seat_epoch == v2_seat_epoch
      && v2_exact_seat_epoch_current()
      && v2_pending.action.kind == AGENT_V2_ACTION_PLAYER_SEND_CHAT) {
    size_t desired_length = strlen(v2_pending.desired_chat_message);

    if (packet->event == E_CHAT_ERROR) {
      v2_pending.chat_error_latched = TRUE;
    } else if (packet->event == E_CHAT_MSG
               && packet->conn_id == client.conn.id
               && strcmp(channel, v2_pending.desired_chat_allied
                                  ? "allied" : "global") == 0
               && plain_length >= desired_length
               && strcmp(plain + plain_length - desired_length,
                         v2_pending.desired_chat_message) == 0) {
      v2_pending.chat_echo_latched = TRUE;
    }
  }
  text_tag_list_destroy(tags);

  if (!v2_pending.active
      || !v2_pending.baseline_captured
      || request_id != v2_pending.request_id
      || v2_pending.seat_epoch != v2_seat_epoch
      || !v2_exact_seat_epoch_current()
      || (native = action_by_number(v2_pending.action.action)) == NULL) {
    return;
  }
  if (v2_pending.action.kind == AGENT_V2_ACTION_UNIT_DISBAND_RECOVER
      && native->id == ACTION_DISBAND_UNIT_RECOVER
      && native->result == ACTRES_DISBAND_UNIT_RECOVER
      && packet->event == E_CARAVAN_ACTION
      && packet->tile == v2_pending.action.destination_city_tile) {
    v2_pending.caravan_action_event_latched = TRUE;
    return;
  }
  if (v2_paid_special_action(native)) {
    if ((native->result == ACTRES_SPY_BRIBE_UNIT
         && packet->event == E_MY_DIPLOMAT_BRIBE)
        || (native->result == ACTRES_SPY_INCITE_CITY
            && packet->event == E_MY_DIPLOMAT_INCITE)) {
      v2_pending.paid_success_event_latched = TRUE;
    } else if (packet->event == E_MY_DIPLOMAT_FAILED
               || packet->event == E_ENEMY_DIPLOMAT_FAILED) {
      v2_pending.paid_failure_event_latched = TRUE;
    }
    return;
  }
  if (v2_pending.action.kind != AGENT_V2_ACTION_UNIT_SPECIAL
      || packet->tile != v2_pending.action.target_tile) {
    return;
  }
  city_success_event = v2_city_espionage_success_event(native);
  actor_binding_exact =
    v2_pending.before_unit_present
    && v2_pending.action.unit_lifecycle_id != 0
    && v2_pending.before_unit_lifecycle_id
       == v2_pending.action.unit_lifecycle_id;
  city_binding_exact =
    v2_pending.before_special_target_exact
    && v2_pending.before_destination_city_present
    && v2_pending.action.destination_city_lifecycle_id != 0
    && v2_pending.before_destination_city_lifecycle_id
       == v2_pending.action.destination_city_lifecycle_id;
  if (city_success_event != E_COUNT
      && fc_agent_v2_city_espionage_event_matches(
           v2_pending.active, v2_pending.processing_started,
           v2_pending.baseline_captured,
           v2_pending.seat_epoch == v2_seat_epoch
             && v2_exact_seat_epoch_current(),
           v2_pending.revision, TRUE,
           actor_binding_exact, city_binding_exact,
           request_id, v2_pending.request_id,
           packet->tile, v2_pending.action.target_tile,
           packet->event, city_success_event)) {
    if (city_success_event == E_MY_DIPLOMAT_POISON) {
      v2_pending.poison_city_success_event_latched = TRUE;
    } else {
      v2_pending.sabotage_city_success_event_latched = TRUE;
    }
  } else if (native->id == ACTION_SPY_ATTACK
      && native->result == ACTRES_SPY_ATTACK
      && action_get_target_kind(native) == ATK_STACK) {
    if (packet->event == E_MY_DIPLOMAT_FAILED) {
      v2_pending.spy_attack_actor_loss_event_latched = TRUE;
    } else if (packet->event == E_ENEMY_DIPLOMAT_FAILED) {
      v2_pending.spy_attack_target_loss_event_latched = TRUE;
    }
  } else if (native->id == ACTION_SPY_SABOTAGE_UNIT_ESC
             && native->result == ACTRES_SPY_SABOTAGE_UNIT
             && action_get_target_kind(native) == ATK_UNIT
             && packet->event == E_MY_DIPLOMAT_SABOTAGE) {
    v2_pending.sabotage_unit_success_event_latched = TRUE;
  }
}

static void v2_nuke_tile_info_observer(
  const struct packet_nuke_tile_info *packet, int request_id, void *data)
{
  const struct action *native;

  (void) data;
  native = action_by_number(v2_pending.action.action);
  if (packet != NULL
      && fc_agent_v2_nuke_observer_matches(
           v2_pending.active, v2_pending.baseline_captured,
           v2_pending.seat_epoch == v2_seat_epoch
             && v2_exact_seat_epoch_current(),
           v2_pending.action.kind == AGENT_V2_ACTION_UNIT_SPECIAL
             && v2_classic_nuke_action(native),
           request_id, v2_pending.request_id,
           packet->tile, v2_pending.action.target_tile)) {
    v2_pending.nuke_tile_info_latched = TRUE;
  }
}

static bool v2_action_postcondition(void)
{
  struct player *self = client_player();
  const struct research *research = self != NULL ? research_get(self) : NULL;
  struct unit *unit;
  struct unit *target_unit;
  struct unit *transport_context;
  struct city *city;
  struct city *source_city;
  struct city *destination_city;
  struct extra_type *extra;
  struct tile *target;

  switch (v2_pending.action.kind) {
  case AGENT_V2_ACTION_PREGAME_CONFIGURE:
    return self != NULL && client_state() == C_S_PREPARING
           && nation_of_player(self) != NULL && self->style != NULL
           && nation_number(nation_of_player(self))
              == v2_pending.desired_pregame_nation
           && style_number(self->style) == v2_pending.desired_pregame_style
           && self->is_male == v2_pending.desired_pregame_male
           && strcmp(player_name(self),
                     v2_pending.desired_pregame_leader) == 0;
  case AGENT_V2_ACTION_PREGAME_SET_TEAM:
    return self != NULL && self->team != NULL
           && team_number(self->team) == v2_pending.desired_pregame_team;
  case AGENT_V2_ACTION_PREGAME_SET_READY:
    return self != NULL
           && (self->is_ready == v2_pending.desired_pregame_ready
               || (v2_pending.desired_pregame_ready
                   && client_state() == C_S_RUNNING));
  case AGENT_V2_ACTION_PLAYER_CAST_VOTE: {
    const struct voteinfo *vote =
      v2_vote_by_number(v2_pending.action.vote_no);

    return v2_vote_active(vote)
           && vote->client_vote == v2_pending.desired_client_vote;
  }
  case AGENT_V2_ACTION_PHASE_END:
    return self != NULL
           && (game.info.turn != v2_pending.before_turn
               || (!v2_pending.before_phase_done && self->phase_done));
  case AGENT_V2_ACTION_MOVE:
    unit = self != NULL
           ? player_unit_by_number(self, v2_pending.action.unit_id) : NULL;
    return unit != NULL && unit_tile(unit) != NULL
           && tile_index(unit_tile(unit)) == v2_pending.action.target_tile;
  case AGENT_V2_ACTION_ATTACK: {
    const struct action *native =
      action_by_number(v2_pending.action.action);
    bool combat_transition;

    unit = self != NULL
           ? player_unit_by_number(self, v2_pending.action.unit_id) : NULL;
    combat_transition =
      unit == NULL
      || (unit_tile(unit) != NULL
          && tile_index(unit_tile(unit))
             == v2_pending.action.target_tile)
      || unit->hp != v2_pending.before_unit_hp
      || v2_visible_stack_signature(self,
                                    v2_pending.action.target_tile)
         != v2_pending.before_target_signature;
    return v2_classic_immediate_combat_action(native)
           && v2_pending.combat_info_latched
           && combat_transition;
  }
  case AGENT_V2_ACTION_FOUND_CITY:
    if (self == NULL
        || !v2_tile_index_valid(v2_pending.action.target_tile)) {
      return FALSE;
    }
    target = index_to_tile(&wld.map, v2_pending.action.target_tile);
    return tile_city(target) != NULL
           && city_owner(tile_city(target)) == self;
  case AGENT_V2_ACTION_RESEARCH_TARGET:
    if (research == NULL) {
      return FALSE;
    }
    if (research->researching == v2_pending.action.target_tech) {
      return TRUE;
    }
    return valid_advance_by_number(v2_pending.action.target_tech) != NULL
           && research_invention_state(research,
                                       v2_pending.action.target_tech)
              == TECH_KNOWN
           && v2_pending.before_research_target
              != v2_pending.action.target_tech;
  case AGENT_V2_ACTION_RESEARCH_GOAL:
    return research != NULL
           && research->tech_goal == v2_pending.action.target_tech;
  case AGENT_V2_ACTION_ECONOMY_RATES:
    return self != NULL
           && self->economic.tax == v2_pending.desired_tax
           && self->economic.luxury == v2_pending.desired_luxury
           && self->economic.science == v2_pending.desired_science;
  case AGENT_V2_ACTION_PLAYER_SEND_CHAT:
    return v2_pending.chat_echo_latched
           && !v2_pending.chat_error_latched;
  case AGENT_V2_ACTION_CITY_PRODUCTION:
    city = self != NULL
           ? player_city_by_number(self, v2_pending.action.city_id) : NULL;
    return city != NULL
           && v2_pending.before_city_present
           && v2_pending.before_city_lifecycle_id
              == v2_pending.action.city_lifecycle_id
           && city->client.lifecycle_id
              == v2_pending.action.city_lifecycle_id
           && v2_existing_incarnation(AGENT_V2_ENTITY_CITY, city->id)
              == v2_pending.action.city_incarnation
           && are_universals_equal(
             &city->production, &v2_pending.desired_production);
  case AGENT_V2_ACTION_CITY_BUY:
    city = self != NULL
           ? player_city_by_number(self, v2_pending.action.city_id) : NULL;
    return city != NULL
           && v2_pending.before_city_present
           && v2_pending.before_city_lifecycle_id
              == v2_pending.action.city_lifecycle_id
           && city->client.lifecycle_id
              == v2_pending.action.city_lifecycle_id
           && v2_existing_incarnation(AGENT_V2_ENTITY_CITY, city->id)
              == v2_pending.action.city_incarnation
           && !v2_pending.before_city_did_buy
           && city->did_buy
           && are_universals_equal(
             &city->production, &v2_pending.desired_production)
           && city->shield_stock
              >= city_production_build_shield_cost(city)
           && v2_pending.before_city_shield_stock < city->shield_stock
           && v2_pending.before_buy_cost > 0
           && self->economic.gold
              == v2_pending.before_player_gold - v2_pending.before_buy_cost;
  case AGENT_V2_ACTION_CITY_WORK_TILE:
    city = self != NULL
           ? player_city_by_number(self, v2_pending.action.city_id) : NULL;
    target = v2_tile_index_valid(v2_pending.action.target_tile)
             ? index_to_tile(&wld.map,
                             v2_pending.action.target_tile) : NULL;
    return city != NULL && target != NULL
           && v2_pending.before_city_present
           && v2_pending.before_city_lifecycle_id
              == v2_pending.action.city_lifecycle_id
           && city->client.lifecycle_id
              == v2_pending.action.city_lifecycle_id
           && v2_existing_incarnation(AGENT_V2_ENTITY_CITY, city->id)
              == v2_pending.action.city_incarnation
           && !v2_pending.before_city_tile_worked
           && tile_worked(target) == city
           && city_specialists(city)
              == v2_pending.before_city_specialists - 1
           && city->specialists[v2_pending.action.source_specialist]
              == v2_pending.before_source_specialists - 1;
  case AGENT_V2_ACTION_CITY_UNWORK_TILE:
    city = self != NULL
           ? player_city_by_number(self, v2_pending.action.city_id) : NULL;
    target = v2_tile_index_valid(v2_pending.action.target_tile)
             ? index_to_tile(&wld.map,
                             v2_pending.action.target_tile) : NULL;
    return city != NULL && target != NULL
           && v2_pending.before_city_present
           && v2_pending.before_city_lifecycle_id
              == v2_pending.action.city_lifecycle_id
           && city->client.lifecycle_id
              == v2_pending.action.city_lifecycle_id
           && v2_existing_incarnation(AGENT_V2_ENTITY_CITY, city->id)
              == v2_pending.action.city_incarnation
           && v2_pending.before_city_tile_worked
           && tile_worked(target) != city
           && city_specialists(city)
              == v2_pending.before_city_specialists + 1
           && city->specialists[DEFAULT_SPECIALIST]
              == v2_pending.before_target_specialists + 1;
  case AGENT_V2_ACTION_CITY_SET_SPECIALIST:
    city = self != NULL
           ? player_city_by_number(self, v2_pending.action.city_id) : NULL;
    return city != NULL
           && v2_pending.before_city_present
           && v2_pending.before_city_lifecycle_id
              == v2_pending.action.city_lifecycle_id
           && city->client.lifecycle_id
              == v2_pending.action.city_lifecycle_id
           && v2_existing_incarnation(AGENT_V2_ENTITY_CITY, city->id)
              == v2_pending.action.city_incarnation
           && city_specialists(city) == v2_pending.before_city_specialists
           && city->specialists[v2_pending.action.source_specialist]
              == v2_pending.before_source_specialists - 1
           && city->specialists[v2_pending.action.target_specialist]
              == v2_pending.before_target_specialists + 1;
  case AGENT_V2_ACTION_CITY_SET_WORKLIST:
    city = self != NULL
           ? player_city_by_number(self, v2_pending.action.city_id) : NULL;
    return city != NULL
           && v2_pending.before_city_present
           && v2_pending.before_city_lifecycle_id
              == v2_pending.action.city_lifecycle_id
           && city->client.lifecycle_id
              == v2_pending.action.city_lifecycle_id
           && v2_existing_incarnation(AGENT_V2_ENTITY_CITY, city->id)
              == v2_pending.action.city_incarnation
           && !are_worklists_equal(&v2_pending.before_city_worklist,
                                   &v2_pending.desired_worklist)
           && are_worklists_equal(&city->worklist,
                                  &v2_pending.desired_worklist);
  case AGENT_V2_ACTION_CITY_SET_OPTIONS:
    city = self != NULL
           ? player_city_by_number(self, v2_pending.action.city_id) : NULL;
    return city != NULL
           && v2_pending.before_city_present
           && v2_pending.before_city_lifecycle_id
              == v2_pending.action.city_lifecycle_id
           && city->client.lifecycle_id
              == v2_pending.action.city_lifecycle_id
           && v2_existing_incarnation(AGENT_V2_ENTITY_CITY, city->id)
              == v2_pending.action.city_incarnation
           && (!BV_ARE_EQUAL(v2_pending.before_city_options,
                             v2_pending.desired_city_options)
               || v2_pending.before_city_wlcb
                  != v2_pending.desired_wlcb)
           && BV_ARE_EQUAL(city->city_options,
                           v2_pending.desired_city_options)
           && city->wlcb == v2_pending.desired_wlcb;
  case AGENT_V2_ACTION_CITY_RENAME:
    city = self != NULL
           ? player_city_by_number(self, v2_pending.action.city_id) : NULL;
    return city != NULL
           && v2_pending.before_city_present
           && v2_pending.before_city_lifecycle_id
              == v2_pending.action.city_lifecycle_id
           && city->client.lifecycle_id
              == v2_pending.action.city_lifecycle_id
           && v2_existing_incarnation(AGENT_V2_ENTITY_CITY, city->id)
              == v2_pending.action.city_incarnation
           && strcmp(v2_pending.before_city_name,
                     v2_pending.city_name) != 0
           && strcmp(city_name_get(city), v2_pending.city_name) == 0;
  case AGENT_V2_ACTION_CITY_SELL_IMPROVEMENT:
    city = self != NULL
           ? player_city_by_number(self, v2_pending.action.city_id) : NULL;
    return city != NULL && v2_pending.desired_improvement != NULL
           && v2_pending.before_city_present
           && v2_pending.before_city_lifecycle_id
              == v2_pending.action.city_lifecycle_id
           && city->client.lifecycle_id
              == v2_pending.action.city_lifecycle_id
           && v2_existing_incarnation(AGENT_V2_ENTITY_CITY, city->id)
              == v2_pending.action.city_incarnation
           && !v2_pending.before_city_did_sell
           && v2_pending.before_city_had_improvement
           && city->did_sell
           && !city_has_building(city, v2_pending.desired_improvement);
  case AGENT_V2_ACTION_CITY_SET_RALLY:
  case AGENT_V2_ACTION_CITY_CLEAR_RALLY: {
    bool active;
    int count;
    uint64_t digest;

    city = self != NULL
           ? player_city_by_number(self, v2_pending.action.city_id) : NULL;
    active = v2_city_rally_active(city);
    count = active ? (int) city->rally_point.length : 0;
    digest = active ? v2_city_rally_orders_digest(city) : 0;
    return fc_agent_v2_rally_postcondition(
      v2_pending.action.city_lifecycle_id,
      v2_pending.before_city_present,
      v2_pending.before_city_lifecycle_id,
      v2_pending.action.rally_source_tile,
      v2_pending.before_city_source_tile,
      city != NULL
        && v2_existing_incarnation(AGENT_V2_ENTITY_CITY, city->id)
           == v2_pending.action.city_incarnation,
      city != NULL ? city->client.lifecycle_id : 0,
      city != NULL && city_tile(city) != NULL
        ? tile_index(city_tile(city)) : -1,
      v2_pending.desired_rally_active,
      v2_pending.desired_rally_persistent,
      v2_pending.desired_rally_order_count,
      v2_pending.desired_rally_orders_digest,
      active,
      active && city->rally_point.persistent,
      active && city->rally_point.vigilant,
      count, digest);
  }
  case AGENT_V2_ACTION_CITY_REQUEST_WORKER_TASK:
  case AGENT_V2_ACTION_CITY_CHANGE_WORKER_TASK:
  case AGENT_V2_ACTION_CITY_REMOVE_WORKER_TASK: {
    const struct worker_task *task;
    struct tile *task_tile;
    bool remove = !v2_pending.desired_worker_task_present;

    city = self != NULL
           ? player_city_by_number(self, v2_pending.action.city_id) : NULL;
    task_tile = v2_tile_index_valid(v2_pending.action.target_tile)
                ? index_to_tile(&wld.map,
                                v2_pending.action.target_tile) : NULL;
    task = city != NULL ? v2_city_worker_task_at(city, task_tile) : NULL;
    return v2_pending.worker_task_echo_latched
           && city != NULL && city_owner(city) == self
           && v2_pending.before_city_present
           && v2_pending.before_city_lifecycle_id
              == v2_pending.action.city_lifecycle_id
           && city->client.lifecycle_id
              == v2_pending.action.city_lifecycle_id
           && v2_existing_incarnation(AGENT_V2_ENTITY_CITY, city->id)
              == v2_pending.action.city_incarnation
           && v2_pending.before_worker_task_present
              == v2_pending.action.worker_task_baseline_present
           && v2_pending.before_worker_task_activity
              == v2_pending.action.worker_task_baseline_activity
           && v2_pending.before_worker_task_extra
              == v2_pending.action.worker_task_baseline_extra
           && v2_pending.before_worker_task_want
              == v2_pending.action.worker_task_baseline_want
           && (remove
               ? task == NULL
               : task != NULL
                 && task->act == v2_pending.desired_activity
                 && (task->tgt != NULL
                     ? extra_number(task->tgt) : EXTRA_NONE)
                    == v2_pending.desired_extra
                 && task->want == v2_pending.desired_worker_task_want);
  }
  case AGENT_V2_ACTION_CITY_SET_GOVERNOR:
  case AGENT_V2_ACTION_CITY_CLEAR_GOVERNOR:
    /* CMA uses the dedicated synchronous local-agent receipt path. */
    return FALSE;
  case AGENT_V2_ACTION_WORKER_START:
  case AGENT_V2_ACTION_CANCEL_ACTIVITY:
  case AGENT_V2_ACTION_UNIT_SENTRY:
    unit = self != NULL
           ? player_unit_by_number(self, v2_pending.action.unit_id) : NULL;
    extra = v2_pending.desired_extra != EXTRA_NONE
            ? extra_by_number(v2_pending.desired_extra) : NULL;
    return unit != NULL
           && v2_pending.before_unit_present
           && v2_pending.before_unit_lifecycle_id
              == v2_pending.action.unit_lifecycle_id
           && unit->client.lifecycle_id
              == v2_pending.action.unit_lifecycle_id
           && v2_existing_incarnation(AGENT_V2_ENTITY_UNIT, unit->id)
              == v2_pending.action.unit_incarnation
           && unit->activity == v2_pending.desired_activity
           && unit->activity_target == extra;
  case AGENT_V2_ACTION_UNIT_AUTO_WORK:
  case AGENT_V2_ACTION_UNIT_AUTO_EXPLORE:
  case AGENT_V2_ACTION_UNIT_CANCEL_AUTOMATION:
    unit = self != NULL
           ? player_unit_by_number(self, v2_pending.action.unit_id) : NULL;
    return fc_agent_v2_unit_automation_postcondition(
      v2_automation_command(v2_pending.action.kind),
      v2_pending.action.unit_lifecycle_id,
      v2_pending.before_unit_present,
      v2_pending.before_unit_lifecycle_id,
      v2_automation_controller(v2_pending.before_unit_ssa),
      v2_pending.before_unit_activity == ACTIVITY_IDLE,
      v2_pending.before_unit_activity_target_none,
      v2_pending.before_unit_has_orders,
      v2_pending.before_unit_goto_none,
      unit != NULL
        && v2_existing_incarnation(AGENT_V2_ENTITY_UNIT, unit->id)
           == v2_pending.action.unit_incarnation,
      unit != NULL ? unit->client.lifecycle_id : 0,
      v2_automation_controller(
        unit != NULL ? unit->ssa_controller : SSA_COUNT),
      unit != NULL && unit->activity == ACTIVITY_IDLE,
      unit != NULL && unit->activity == ACTIVITY_EXPLORE,
      unit != NULL && unit->activity_target == NULL,
      v2_pending.exact_unit_state_latched);
  case AGENT_V2_ACTION_UNIT_CANCEL_ORDERS:
    unit = self != NULL
           ? player_unit_by_number(self, v2_pending.action.unit_id) : NULL;
    return fc_agent_v2_unit_cancel_orders_postcondition(
      v2_pending.action.unit_lifecycle_id,
      v2_pending.before_unit_present,
      v2_pending.before_unit_lifecycle_id,
      v2_pending.requested_unit_source_tile,
      v2_pending.before_unit_tile,
      v2_automation_controller(v2_pending.before_unit_ssa),
      v2_pending.before_unit_activity == ACTIVITY_IDLE,
      v2_pending.before_unit_activity_target_none,
      v2_pending.before_unit_has_orders,
      v2_pending.before_unit_goto_none,
      unit != NULL
        && v2_existing_incarnation(AGENT_V2_ENTITY_UNIT, unit->id)
           == v2_pending.action.unit_incarnation,
      unit != NULL ? unit->client.lifecycle_id : 0,
      unit != NULL && unit->has_orders,
      unit != NULL && unit->goto_tile == NULL);
  case AGENT_V2_ACTION_UNIT_GOTO:
  case AGENT_V2_ACTION_UNIT_GOTO_AND_PERFORM:
  case AGENT_V2_ACTION_UNIT_CONNECT_ROUTE:
    return fc_agent_v2_unit_route_install_postcondition(
      v2_pending.action.unit_lifecycle_id,
      v2_pending.before_unit_present,
      v2_pending.before_unit_lifecycle_id,
      v2_pending.requested_unit_source_tile,
      v2_pending.before_unit_tile,
      v2_automation_controller(v2_pending.before_unit_ssa),
      v2_pending.before_unit_activity == ACTIVITY_IDLE,
      v2_pending.before_unit_activity_target_none,
      v2_pending.before_unit_has_orders,
      v2_pending.before_unit_goto_none,
      v2_pending.before_unit_untransported,
      v2_pending.before_unit_cargo_empty,
      v2_pending.desired_route_destination_tile,
      v2_pending.desired_route_order_count,
      v2_pending.desired_route_orders_digest,
      v2_pending.desired_route_repeat,
      v2_pending.desired_route_vigilant,
      v2_pending.action.kind == AGENT_V2_ACTION_UNIT_GOTO_AND_PERFORM,
      v2_pending.exact_route_state_latched);
  case AGENT_V2_ACTION_UNIT_SET_ROUTE:
    unit = self != NULL
           ? player_unit_by_number(self, v2_pending.action.unit_id) : NULL;
    return fc_agent_v2_unit_route_postcondition(
      v2_pending.action.unit_lifecycle_id,
      v2_pending.before_unit_present,
      v2_pending.before_unit_lifecycle_id,
      v2_pending.requested_unit_source_tile,
      v2_pending.before_unit_tile,
      v2_automation_controller(v2_pending.before_unit_ssa),
      v2_pending.before_unit_activity == ACTIVITY_IDLE,
      v2_pending.before_unit_activity_target_none,
      v2_pending.before_unit_has_orders,
      v2_pending.before_unit_goto_none,
      v2_pending.before_unit_untransported,
      v2_pending.before_unit_cargo_empty,
      v2_pending.desired_route_destination_tile,
      v2_pending.desired_route_order_count,
      v2_pending.desired_route_orders_digest,
      v2_pending.desired_route_repeat,
      v2_pending.desired_route_vigilant,
      unit != NULL
        && v2_existing_incarnation(AGENT_V2_ENTITY_UNIT, unit->id)
           == v2_pending.action.unit_incarnation,
      unit != NULL ? unit->client.lifecycle_id : 0,
      unit != NULL && unit_tile(unit) != NULL
        ? tile_index(unit_tile(unit)) : -1,
      unit != NULL && unit->has_orders,
      unit != NULL && unit->goto_tile != NULL
        ? tile_index(unit->goto_tile) : -1,
      unit != NULL && unit->has_orders ? unit->orders.length : 0,
      unit != NULL && unit->has_orders
        ? unit_orders_digest(unit->orders.length, unit->orders.list) : 0,
      unit != NULL && unit->has_orders && unit->orders.repeat,
      unit != NULL && unit->has_orders && unit->orders.vigilant);
  case AGENT_V2_ACTION_PLAYER_PLACE_INFRA:
    target = v2_tile_index_valid(v2_pending.action.target_tile)
             ? index_to_tile(&wld.map, v2_pending.action.target_tile) : NULL;
    return fc_agent_v2_infrastructure_postcondition(
      v2_pending.before_infrastructure_points,
      v2_pending.action.infrastructure_cost,
      self != NULL ? self->economic.infra_points : -1,
      v2_pending.before_infrastructure_unplaced,
      v2_pending.action.target_extra,
      target != NULL && target->placing != NULL
        ? extra_number(target->placing) : EXTRA_NONE);
  case AGENT_V2_ACTION_UNIT_FORTIFY:
    unit = self != NULL
           ? player_unit_by_number(self, v2_pending.action.unit_id) : NULL;
    return fc_agent_v2_unit_activity_postcondition(
      v2_pending.action.unit_lifecycle_id,
      v2_pending.before_unit_present,
      v2_pending.before_unit_lifecycle_id,
      unit != NULL, unit != NULL ? unit->client.lifecycle_id : 0,
      unit != NULL ? unit->activity : ACTIVITY_LAST,
      unit != NULL && unit->activity_target == NULL,
      ACTIVITY_FORTIFYING, ACTIVITY_FORTIFIED);
  case AGENT_V2_ACTION_UNIT_CONVERT:
    unit = self != NULL
           ? player_unit_by_number(self, v2_pending.action.unit_id) : NULL;
    return fc_agent_v2_unit_conversion_postcondition(
      v2_pending.action.unit_lifecycle_id,
      v2_pending.before_unit_present,
      v2_pending.before_unit_lifecycle_id,
      unit != NULL, unit != NULL ? unit->client.lifecycle_id : 0,
      unit != NULL ? unit->activity : ACTIVITY_LAST,
      unit != NULL && unit->activity_target == NULL,
      ACTIVITY_CONVERT, v2_pending.before_unit_type,
      v2_pending.desired_unit_type,
      unit != NULL ? utype_number(unit_type_get(unit)) : -1);
  case AGENT_V2_ACTION_UNIT_DISBAND:
    unit = self != NULL
           ? player_unit_by_number(self, v2_pending.action.unit_id) : NULL;
    /* An owned unit cannot disappear through fog. Exact presence and client
     * lifetime at processing start plus absence at finish prove consumption;
     * a same-ID replacement has a new lifetime and remains present. */
    return fc_agent_v2_unit_consumed_postcondition(
      v2_pending.action.unit_lifecycle_id,
      v2_pending.before_unit_present,
      v2_pending.before_unit_lifecycle_id,
      unit != NULL);
  case AGENT_V2_ACTION_UNIT_HOMELESS:
    unit = self != NULL
           ? player_unit_by_number(self, v2_pending.action.unit_id) : NULL;
    return fc_agent_v2_unit_home_cleared_postcondition(
      v2_pending.action.unit_lifecycle_id,
      v2_pending.before_unit_present,
      v2_pending.before_unit_lifecycle_id,
      unit != NULL, unit != NULL ? unit->client.lifecycle_id : 0,
      v2_pending.before_unit_homecity,
      unit != NULL ? unit->homecity : IDENTITY_NUMBER_ZERO);
  case AGENT_V2_ACTION_UNIT_UPGRADE:
    unit = self != NULL
           ? player_unit_by_number(self, v2_pending.action.unit_id) : NULL;
    return fc_agent_v2_unit_upgrade_postcondition(
      v2_pending.action.unit_lifecycle_id,
      v2_pending.before_unit_present,
      v2_pending.before_unit_lifecycle_id,
      v2_pending.before_unit_type,
      unit != NULL
        && v2_existing_incarnation(AGENT_V2_ENTITY_UNIT, unit->id)
           == v2_pending.action.unit_incarnation,
      unit != NULL ? unit->client.lifecycle_id : 0,
      v2_pending.desired_unit_type,
      unit != NULL ? utype_number(unit_type_get(unit)) : -1);
  case AGENT_V2_ACTION_UNIT_REHOME:
    unit = self != NULL
           ? player_unit_by_number(self, v2_pending.action.unit_id) : NULL;
    destination_city = game_city_by_number(
      v2_pending.action.destination_city_id);
    return destination_city != NULL
           && destination_city->client.lifecycle_id
              == v2_pending.action.destination_city_lifecycle_id
           && v2_existing_incarnation(
                AGENT_V2_ENTITY_CITY, destination_city->id)
              == v2_pending.action.destination_city_incarnation
           && fc_agent_v2_unit_rehome_postcondition(
             v2_pending.action.unit_lifecycle_id,
             v2_pending.before_unit_present,
             v2_pending.before_unit_lifecycle_id,
             v2_pending.before_unit_homecity,
             unit != NULL
               && v2_existing_incarnation(AGENT_V2_ENTITY_UNIT, unit->id)
                  == v2_pending.action.unit_incarnation,
             unit != NULL ? unit->client.lifecycle_id : 0,
             v2_pending.action.destination_city_id,
             unit != NULL ? unit->homecity : IDENTITY_NUMBER_ZERO);
  case AGENT_V2_ACTION_UNIT_JOIN_CITY:
    unit = self != NULL
           ? player_unit_by_number(self, v2_pending.action.unit_id) : NULL;
    destination_city = game_city_by_number(
      v2_pending.action.destination_city_id);
    return fc_agent_v2_join_city_postcondition(
      v2_pending.action.unit_lifecycle_id,
      v2_pending.before_unit_present,
      v2_pending.before_unit_lifecycle_id,
      unit != NULL,
      v2_pending.action.destination_city_lifecycle_id,
      v2_pending.before_destination_city_present,
      v2_pending.before_destination_city_lifecycle_id,
      v2_pending.before_destination_city_size,
      v2_pending.expected_unit_population,
      destination_city != NULL
        && v2_existing_incarnation(
             AGENT_V2_ENTITY_CITY, destination_city->id)
           == v2_pending.action.destination_city_incarnation,
      destination_city != NULL
        ? destination_city->client.lifecycle_id : 0,
      destination_city != NULL ? city_size_get(destination_city) : -1);
  case AGENT_V2_ACTION_UNIT_ESTABLISH_TRADE:
    unit = self != NULL
           ? player_unit_by_number(self, v2_pending.action.unit_id) : NULL;
    source_city = self != NULL
                  ? player_city_by_number(
                      self, v2_pending.action.source_city_id) : NULL;
    destination_city = game_city_by_number(
      v2_pending.action.destination_city_id);
    return fc_agent_v2_trade_route_postcondition(
      v2_pending.action.unit_lifecycle_id,
      v2_pending.before_unit_present,
      v2_pending.before_unit_lifecycle_id,
      unit != NULL,
      v2_pending.action.source_city_lifecycle_id,
      v2_pending.before_source_city_present,
      v2_pending.before_source_city_lifecycle_id,
      source_city != NULL
        && v2_existing_incarnation(AGENT_V2_ENTITY_CITY, source_city->id)
           == v2_pending.action.source_city_incarnation,
      source_city != NULL ? source_city->client.lifecycle_id : 0,
      v2_pending.action.destination_city_lifecycle_id,
      v2_pending.before_destination_city_present,
      v2_pending.before_destination_city_lifecycle_id,
      destination_city != NULL
        && v2_existing_incarnation(
             AGENT_V2_ENTITY_CITY, destination_city->id)
           == v2_pending.action.destination_city_incarnation,
      destination_city != NULL
        ? destination_city->client.lifecycle_id : 0,
      v2_pending.before_trade_route_exists,
      source_city != NULL && destination_city != NULL
        && have_cities_trade_route(source_city, destination_city));
  case AGENT_V2_ACTION_UNIT_MARKETPLACE:
    unit = self != NULL
           ? player_unit_by_number(self, v2_pending.action.unit_id) : NULL;
    source_city = self != NULL
                  ? player_city_by_number(
                      self, v2_pending.action.source_city_id) : NULL;
    destination_city = game_city_by_number(
      v2_pending.action.destination_city_id);
    return fc_agent_v2_marketplace_postcondition(
      v2_pending.action.unit_lifecycle_id,
      v2_pending.before_unit_present,
      v2_pending.before_unit_lifecycle_id,
      unit != NULL,
      v2_pending.action.source_city_lifecycle_id,
      v2_pending.before_source_city_present,
      v2_pending.before_source_city_lifecycle_id,
      source_city != NULL
        && v2_existing_incarnation(AGENT_V2_ENTITY_CITY, source_city->id)
           == v2_pending.action.source_city_incarnation,
      source_city != NULL ? source_city->client.lifecycle_id : 0,
      v2_pending.action.destination_city_lifecycle_id,
      v2_pending.before_destination_city_present,
      v2_pending.before_destination_city_lifecycle_id,
      destination_city != NULL
        && v2_existing_incarnation(
             AGENT_V2_ENTITY_CITY, destination_city->id)
           == v2_pending.action.destination_city_incarnation,
      destination_city != NULL
        ? destination_city->client.lifecycle_id : 0);
  case AGENT_V2_ACTION_UNIT_HELP_WONDER:
    unit = self != NULL
           ? player_unit_by_number(self, v2_pending.action.unit_id) : NULL;
    destination_city = game_city_by_number(
      v2_pending.action.destination_city_id);
    return fc_agent_v2_help_wonder_postcondition(
      v2_pending.action.unit_lifecycle_id,
      v2_pending.before_unit_present,
      v2_pending.before_unit_lifecycle_id,
      unit != NULL,
      v2_pending.action.destination_city_lifecycle_id,
      v2_pending.before_destination_city_present,
      v2_pending.before_destination_city_lifecycle_id,
      v2_pending.before_destination_city_internals_exact,
      v2_pending.before_destination_city_shield_stock,
      v2_pending.expected_help_shields,
      destination_city != NULL
        && v2_existing_incarnation(
             AGENT_V2_ENTITY_CITY, destination_city->id)
           == v2_pending.action.destination_city_incarnation,
      destination_city != NULL
        ? destination_city->client.lifecycle_id : 0,
      destination_city != NULL ? destination_city->shield_stock : -1);
  case AGENT_V2_ACTION_UNIT_DISBAND_RECOVER:
    unit = self != NULL
           ? player_unit_by_number(self, v2_pending.action.unit_id) : NULL;
    destination_city = game_city_by_number(
      v2_pending.action.destination_city_id);
    return fc_agent_v2_disband_recover_postcondition(
      v2_pending.action.unit_lifecycle_id,
      v2_pending.before_unit_present,
      v2_pending.before_unit_lifecycle_id,
      unit != NULL,
      v2_pending.action.destination_city_lifecycle_id,
      v2_pending.before_destination_city_present,
      v2_pending.before_destination_city_lifecycle_id,
      v2_pending.caravan_action_event_latched,
      v2_pending.before_destination_city_owned,
      v2_pending.before_destination_city_shield_stock,
      v2_pending.expected_help_shields,
      destination_city != NULL,
      destination_city != NULL
        ? destination_city->client.lifecycle_id : 0,
      destination_city != NULL && self != NULL
        && city_owner(destination_city) == self,
      destination_city != NULL ? destination_city->shield_stock : -1);
  case AGENT_V2_ACTION_UNIT_AIRLIFT:
    unit = self != NULL
           ? player_unit_by_number(self, v2_pending.action.unit_id) : NULL;
    source_city = self != NULL
                  ? player_city_by_number(
                      self, v2_pending.action.source_city_id) : NULL;
    destination_city = self != NULL
                       ? player_city_by_number(
                           self,
                           v2_pending.action.destination_city_id) : NULL;
    return source_city != NULL && destination_city != NULL
           && v2_pending.before_source_city_present
           && v2_pending.before_destination_city_present
           && v2_pending.before_source_city_lifecycle_id
              == v2_pending.action.source_city_lifecycle_id
           && v2_pending.before_destination_city_lifecycle_id
              == v2_pending.action.destination_city_lifecycle_id
           && source_city->client.lifecycle_id
              == v2_pending.action.source_city_lifecycle_id
           && destination_city->client.lifecycle_id
              == v2_pending.action.destination_city_lifecycle_id
           && v2_existing_incarnation(
                AGENT_V2_ENTITY_CITY, source_city->id)
              == v2_pending.action.source_city_incarnation
           && v2_existing_incarnation(
                AGENT_V2_ENTITY_CITY, destination_city->id)
              == v2_pending.action.destination_city_incarnation
           && tile_index(city_tile(source_city))
              == v2_pending.action.source_city_tile
           && tile_index(city_tile(destination_city))
              == v2_pending.action.destination_city_tile
           && fc_agent_v2_unit_relocation_postcondition(
                v2_pending.action.unit_lifecycle_id,
                v2_pending.before_unit_present,
                v2_pending.before_unit_lifecycle_id,
                v2_pending.before_unit_tile,
                unit != NULL, unit != NULL ? unit->client.lifecycle_id : 0,
                unit != NULL && unit_tile(unit) != NULL
                  ? tile_index(unit_tile(unit)) : -1,
                v2_pending.action.destination_city_tile,
                FALSE, v2_pending.before_unit_paradropped,
                unit != NULL && unit->paradropped);
  case AGENT_V2_ACTION_UNIT_PARADROP:
  case AGENT_V2_ACTION_UNIT_TELEPORT:
    unit = self != NULL
           ? player_unit_by_number(self, v2_pending.action.unit_id) : NULL;
    return fc_agent_v2_unit_relocation_postcondition(
      v2_pending.action.unit_lifecycle_id,
      v2_pending.before_unit_present,
      v2_pending.before_unit_lifecycle_id,
      v2_pending.before_unit_tile,
      unit != NULL, unit != NULL ? unit->client.lifecycle_id : 0,
      unit != NULL && unit_tile(unit) != NULL
        ? tile_index(unit_tile(unit)) : -1,
      v2_pending.action.target_tile,
      v2_pending.action.kind == AGENT_V2_ACTION_UNIT_PARADROP,
      v2_pending.before_unit_paradropped,
      unit != NULL && unit->paradropped);
  case AGENT_V2_ACTION_TRANSPORT_BOARD:
  case AGENT_V2_ACTION_TRANSPORT_DEBOARD:
  case AGENT_V2_ACTION_TRANSPORT_EMBARK:
  case AGENT_V2_ACTION_TRANSPORT_DISEMBARK:
  case AGENT_V2_ACTION_TRANSPORT_LOAD:
  case AGENT_V2_ACTION_TRANSPORT_UNLOAD: {
    enum fc_agent_v2_transport_command command;
    bool relationship_exact;
    bool detached_exact = FALSE;
    bool chain_transition_exact;
    uint64_t current_signature;

    unit = self != NULL
           ? player_unit_by_number(self, v2_pending.action.unit_id) : NULL;
    target_unit = v2_pending.action.target_unit_id >= 0
                  ? game_unit_by_number(
                      v2_pending.action.target_unit_id) : NULL;
    transport_context =
      v2_pending.action.transport_context_id >= 0
      ? game_unit_by_number(
          v2_pending.action.transport_context_id) : NULL;
    chain_transition_exact =
      v2_transport_component_signature(
        self, unit, target_unit, transport_context, NULL,
        &current_signature)
      && current_signature == v2_pending.action.transport_after_signature;
    switch (v2_pending.action.kind) {
    case AGENT_V2_ACTION_TRANSPORT_BOARD:
      command = FC_AGENT_V2_TRANSPORT_BOARD;
      relationship_exact = v2_transport_linked_pair(
        self, unit, target_unit);
      break;
    case AGENT_V2_ACTION_TRANSPORT_DEBOARD:
      command = FC_AGENT_V2_TRANSPORT_DEBOARD;
      relationship_exact = v2_transport_linked_pair(
        self, unit, target_unit);
      detached_exact = unit != NULL
                       && v2_transport_inbound_state(self, unit, NULL)
                          == AGENT_V2_TRANSPORT_UNTRANSPORTED
                       && chain_transition_exact;
      break;
    case AGENT_V2_ACTION_TRANSPORT_EMBARK:
      command = FC_AGENT_V2_TRANSPORT_EMBARK;
      relationship_exact = v2_transport_linked_pair(
        self, unit, target_unit);
      break;
    case AGENT_V2_ACTION_TRANSPORT_DISEMBARK:
      command = FC_AGENT_V2_TRANSPORT_DISEMBARK;
      relationship_exact = v2_transport_linked_pair(
        self, unit, transport_context);
      detached_exact = unit != NULL
                       && v2_transport_inbound_state(self, unit, NULL)
                          == AGENT_V2_TRANSPORT_UNTRANSPORTED
                       && chain_transition_exact;
      break;
    case AGENT_V2_ACTION_TRANSPORT_LOAD:
      command = FC_AGENT_V2_TRANSPORT_LOAD;
      relationship_exact = v2_transport_linked_pair(
        self, target_unit, unit);
      break;
    case AGENT_V2_ACTION_TRANSPORT_UNLOAD:
      command = FC_AGENT_V2_TRANSPORT_UNLOAD;
      relationship_exact = v2_transport_linked_pair(
        self, target_unit, unit);
      detached_exact = target_unit != NULL
                       && v2_transport_inbound_state(
                         self, target_unit, NULL)
                          == AGENT_V2_TRANSPORT_UNTRANSPORTED
                       && chain_transition_exact;
      break;
    default:
      return FALSE;
    }
    return chain_transition_exact
           && fc_agent_v2_transport_postcondition(
      command,
      v2_pending.action.unit_lifecycle_id,
      v2_pending.before_unit_present,
      v2_pending.before_unit_lifecycle_id,
      v2_pending.before_unit_tile,
      v2_pending.action.target_unit_lifecycle_id,
      v2_pending.before_target_unit_present,
      v2_pending.before_target_unit_lifecycle_id,
      v2_pending.before_target_unit_tile,
      v2_pending.action.transport_context_lifecycle_id,
      v2_pending.before_transport_context_present,
      v2_pending.before_transport_context_lifecycle_id,
      v2_pending.before_transport_context_tile,
      v2_pending.before_transport_baseline_exact,
      unit != NULL, unit != NULL ? unit->client.lifecycle_id : 0,
      unit != NULL && unit_tile(unit) != NULL
        ? tile_index(unit_tile(unit)) : -1,
      target_unit != NULL,
      target_unit != NULL ? target_unit->client.lifecycle_id : 0,
      target_unit != NULL && unit_tile(target_unit) != NULL
        ? tile_index(unit_tile(target_unit)) : -1,
      transport_context != NULL,
      transport_context != NULL
        ? transport_context->client.lifecycle_id : 0,
      transport_context != NULL && unit_tile(transport_context) != NULL
        ? tile_index(unit_tile(transport_context)) : -1,
      relationship_exact, detached_exact,
      v2_pending.action.target_tile);
  }
  case AGENT_V2_ACTION_GOVERNMENT_REVOLUTION:
  case AGENT_V2_ACTION_GOVERNMENT_CHANGE:
    if (self == NULL || game.government_during_revolution == NULL) {
      return FALSE;
    }
    return fc_agent_v2_government_postcondition(
      v2_pending.action.kind == AGENT_V2_ACTION_GOVERNMENT_REVOLUTION
        ? FC_AGENT_V2_GOV_REVOLUTION : FC_AGENT_V2_GOV_CHANGE,
      v2_pending.before_government,
      v2_pending.before_target_government,
      v2_pending.before_revolution_finishes,
      v2_government_id(government_of_player(self)),
      v2_government_id(self->target_government),
      self->revolution_finishes,
      government_number(game.government_during_revolution),
      v2_pending.desired_government);
  case AGENT_V2_ACTION_MULTIPLIER_SET: {
    int multiplier_id;

    if (self == NULL
        || multiplier_count() != v2_pending.before_multiplier_count
        || v2_pending.desired_multiplier < 0
        || v2_pending.desired_multiplier
           >= v2_pending.before_multiplier_count
        || v2_pending.before_multiplier_targets[
             v2_pending.desired_multiplier]
           == v2_pending.desired_multiplier_value) {
      return FALSE;
    }
    for (multiplier_id = 0;
         multiplier_id < v2_pending.before_multiplier_count;
         multiplier_id++) {
      struct multiplier *pmul = multiplier_by_number(multiplier_id);
      int expected_target =
        multiplier_id == v2_pending.desired_multiplier
        ? v2_pending.desired_multiplier_value
        : v2_pending.before_multiplier_targets[multiplier_id];

      if (pmul == NULL
          || player_multiplier_value(self, pmul)
             != v2_pending.before_multiplier_values[multiplier_id]
          || player_multiplier_target_value(self, pmul)
             != expected_target) {
        return FALSE;
      }
    }
    return TRUE;
  }
  case AGENT_V2_ACTION_SPACESHIP_PLACE:
    if (self == NULL
        || v2_pending.before_spaceship.state != SSHIP_STARTED
        || self->spaceship.state != SSHIP_STARTED) {
      return FALSE;
    }
    if (v2_pending.desired_spaceship_part == SSHIP_PLACE_STRUCTURAL) {
      return v2_spaceship_unchanged_except_structural(
        &v2_pending.before_spaceship, &self->spaceship,
        v2_pending.desired_spaceship_value);
    }
    return v2_spaceship_unchanged_except_counter(
      &v2_pending.before_spaceship, &self->spaceship,
      v2_pending.desired_spaceship_part,
      v2_pending.desired_spaceship_value);
  case AGENT_V2_ACTION_SPACESHIP_LAUNCH:
    return self != NULL
           && v2_pending.before_spaceship.state == SSHIP_STARTED
           && self->spaceship.state == SSHIP_LAUNCHED
           && self->spaceship.launch_year == v2_pending.before_spaceship_year
           && self->spaceship.structurals
              == v2_pending.before_spaceship.structurals
           && self->spaceship.components
              == v2_pending.before_spaceship.components
           && self->spaceship.modules == v2_pending.before_spaceship.modules
           && BV_ARE_EQUAL(self->spaceship.structure,
                           v2_pending.before_spaceship.structure)
           && self->spaceship.fuel == v2_pending.before_spaceship.fuel
           && self->spaceship.propulsion
              == v2_pending.before_spaceship.propulsion
           && self->spaceship.habitation
              == v2_pending.before_spaceship.habitation
           && self->spaceship.life_support
              == v2_pending.before_spaceship.life_support
           && self->spaceship.solar_panels
              == v2_pending.before_spaceship.solar_panels;
  case AGENT_V2_ACTION_DIPLOMACY_OPEN_MEETING:
  case AGENT_V2_ACTION_DIPLOMACY_CLOSE_MEETING:
  case AGENT_V2_ACTION_DIPLOMACY_PROPOSE_CLAUSE:
  case AGENT_V2_ACTION_DIPLOMACY_REMOVE_CLAUSE:
  case AGENT_V2_ACTION_DIPLOMACY_ACCEPT:
  case AGENT_V2_ACTION_DIPLOMACY_WITHDRAW_ACCEPTANCE:
    return v2_pending.diplomacy_echo_latched;
  case AGENT_V2_ACTION_DIPLOMACY_BREAK_RELATION: {
    struct player *other = self != NULL
                           ? player_by_number(
                               v2_pending.action.counterpart_id) : NULL;

    return other != NULL && v2_pending.before_diplstate != DS_LAST
           && player_diplstate_get(self, other)->type
              == cancel_pact_result(v2_pending.before_diplstate)
           && player_diplstate_get(self, other)->type
              != v2_pending.before_diplstate;
  }
  case AGENT_V2_ACTION_DIPLOMACY_WITHDRAW_VISION: {
    struct player *other = self != NULL
                           ? player_by_number(
                               v2_pending.action.counterpart_id) : NULL;

    return other != NULL && v2_pending.before_gives_vision
           && !gives_shared_vision(self, other);
  }
  case AGENT_V2_ACTION_DIPLOMACY_WITHDRAW_SHARED_TILES: {
    struct player *other = self != NULL
                           ? player_by_number(
                               v2_pending.action.counterpart_id) : NULL;

    return other != NULL && v2_pending.before_gives_shared_tiles
           && !gives_shared_tiles(self, other);
  }
  case AGENT_V2_ACTION_UNIT_SPECIAL: {
    const struct action *native =
      action_by_number(v2_pending.action.action);
    struct unit *any_actor;
    bool current_unit_present;
    int current_unit_tile;

    if (native == NULL || self == NULL) {
      return FALSE;
    }
    unit = player_unit_by_number(self, v2_pending.action.unit_id);
    any_actor = game_unit_by_number(v2_pending.action.unit_id);
    current_unit_present =
      unit != NULL
      && unit->client.lifecycle_id == v2_pending.action.unit_lifecycle_id
      && v2_existing_incarnation(AGENT_V2_ENTITY_UNIT, unit->id)
         == v2_pending.action.unit_incarnation;
    current_unit_tile = current_unit_present && unit_tile(unit) != NULL
                        ? tile_index(unit_tile(unit)) : -1;
    target = v2_tile_index_valid(v2_pending.action.target_tile)
             ? index_to_tile(&wld.map, v2_pending.action.target_tile) : NULL;
    destination_city = game_city_by_number(
      v2_pending.action.destination_city_id);
    target_unit = game_unit_by_number(v2_pending.action.target_unit_id);
    switch (native->result) {
    case ACTRES_SPY_BRIBE_STACK: {
      bool visible_mapping_required;

      v2_pending.bribe_visible_mapping_corroborated =
        v2_visible_bribe_mapping_matches(self);
      visible_mapping_required =
        v2_pending.bribe_visible_baseline_exact
        && v2_pending.bribe_visible_count > 0;
      return v2_pending.before_special_target_exact
             && v2_pending.before_unit_present
             && v2_pending.action.unit_lifecycle_id != 0
             && v2_pending.before_unit_lifecycle_id
                == v2_pending.action.unit_lifecycle_id
             && v2_pending.action_success_receipt_latched
             && !v2_pending.bribe_visible_mapping_conflict
             && (!visible_mapping_required
                 || v2_pending.bribe_visible_mapping_corroborated);
    }
    case ACTRES_SPY_BRIBE_UNIT: {
      struct unit *replacement =
        game_unit_by_number(v2_pending.paid_replacement_unit_id);

      return v2_pending.before_special_target_exact
             && v2_pending.before_target_unit_present
             && v2_pending.paid_success_event_latched
             && v2_pending.paid_replacement_latched
             && !v2_pending.paid_replacement_conflict
             && target_unit == NULL && replacement != NULL
             && unit_owner(replacement) == self
             && replacement->client.lifecycle_id
                == v2_pending.paid_replacement_unit_lifecycle_id
             && v2_existing_incarnation(
                  AGENT_V2_ENTITY_UNIT, replacement->id)
                == v2_pending.paid_replacement_unit_incarnation
             && utype_number(unit_type_get(replacement))
                == v2_pending.before_target_unit_type;
    }
    case ACTRES_SPY_INCITE_CITY:
      return v2_pending.before_special_target_exact
             && v2_pending.before_destination_city_present
             && v2_pending.paid_success_event_latched
             && destination_city != NULL
             && destination_city->id
                == v2_pending.action.destination_city_id
             && city_owner(destination_city) == self
             && city_tile(destination_city) != NULL
             && tile_index(city_tile(destination_city))
                == v2_pending.action.destination_city_tile
             && destination_city->client.lifecycle_id != 0
             && destination_city->client.lifecycle_id
                != v2_pending.before_destination_city_lifecycle_id;
    case ACTRES_SPY_INVESTIGATE_CITY:
      return v2_pending.before_special_target_exact
             && v2_pending.before_destination_city_present
             && v2_pending.investigation_started_latched
             && v2_pending.investigation_city_info_latched
             && v2_pending.investigation_finished_latched
             && v2_pending.investigation.city_id
                == v2_pending.action.destination_city_id
             && v2_pending.investigation.city_incarnation
                == v2_pending.action.destination_city_incarnation
             && v2_pending.investigation.city_lifecycle_id
                == v2_pending.action.destination_city_lifecycle_id
             && v2_pending.investigation.tile
                == v2_pending.action.destination_city_tile
             && v2_investigation_payload_exportable(
                  &v2_pending.investigation);
    case ACTRES_SPY_POISON: {
      bool city_on_target =
        target != NULL && tile_city(target) != NULL;
      bool current_city_present = destination_city != NULL
                                  || city_on_target;
      bool current_city_binding_exact =
        destination_city != NULL && city_tile(destination_city) == target
        && destination_city->client.lifecycle_id
           == v2_pending.action.destination_city_lifecycle_id
        && v2_existing_incarnation(
             AGENT_V2_ENTITY_CITY, destination_city->id)
           == v2_pending.action.destination_city_incarnation;

      return fc_agent_v2_poison_city_postcondition(
        v2_pending.before_special_target_exact,
        v2_pending.action_success_receipt_latched,
        v2_pending.action.destination_city_lifecycle_id,
        v2_pending.before_destination_city_present,
        v2_pending.before_destination_city_lifecycle_id,
        v2_pending.before_destination_city_size,
        current_city_present, current_city_binding_exact,
        destination_city != NULL
          ? destination_city->client.lifecycle_id : 0,
        destination_city != NULL ? city_size_get(destination_city) : -1);
    }
    case ACTRES_ESTABLISH_EMBASSY:
      return destination_city != NULL
             && player_has_real_embassy(
               self, city_owner(destination_city));
    case ACTRES_CONQUER_CITY:
      return destination_city != NULL
             && city_owner(destination_city) == self;
    case ACTRES_DESTROY_CITY:
      return destination_city == NULL;
    case ACTRES_SPY_SABOTAGE_CITY: {
      bool current_exact = destination_city != NULL
                           && can_player_see_city_internals(
                             self, destination_city);
      bv_imprs current_improvements =
        v2_visible_city_improvements(destination_city, current_exact);
      bool current_city_binding_exact =
        destination_city != NULL && city_tile(destination_city) == target
        && destination_city->client.lifecycle_id
           == v2_pending.action.destination_city_lifecycle_id
        && v2_existing_incarnation(
             AGENT_V2_ENTITY_CITY, destination_city->id)
           == v2_pending.action.destination_city_incarnation;
      bool visible_effect_corroborated =
        v2_pending.before_destination_city_internals_exact && current_exact
        && (v2_visible_city_improvement_removed(
              &v2_pending.before_destination_city_improvements,
              &current_improvements)
            || destination_city->shield_stock
               < v2_pending.before_destination_city_shield_stock);

      return fc_agent_v2_sabotage_city_postcondition(
        v2_pending.before_special_target_exact,
        v2_pending.action_success_receipt_latched,
        v2_pending.action.destination_city_lifecycle_id,
        v2_pending.before_destination_city_present,
        v2_pending.before_destination_city_lifecycle_id,
        destination_city != NULL, current_city_binding_exact,
        destination_city != NULL
          ? destination_city->client.lifecycle_id : 0,
        visible_effect_corroborated);
    }
    case ACTRES_SPY_TARGETED_SABOTAGE_CITY: {
      const struct impr_type *improvement =
        v2_pending.action.target_build_kind == VUT_IMPROVEMENT
        ? improvement_by_number(v2_pending.action.target_build_id) : NULL;
      bool current_city_present =
        destination_city != NULL && city_tile(destination_city) == target
        && v2_existing_incarnation(
             AGENT_V2_ENTITY_CITY, destination_city->id)
           == v2_pending.action.destination_city_incarnation;
      bool externally_visible = improvement != NULL
                                && is_improvement_visible(improvement);
      bool current_building_present =
        externally_visible && destination_city != NULL
        && city_has_building(destination_city, improvement);

      return improvement != NULL
             && fc_agent_v2_targeted_sabotage_postcondition(
                  v2_pending.before_special_target_exact,
                  v2_pending.action_success_receipt_latched,
                  externally_visible,
                  v2_pending.action.destination_city_lifecycle_id,
                  v2_pending.before_destination_city_present,
                  v2_pending.before_destination_city_lifecycle_id,
                  current_city_present,
                  destination_city != NULL
                    ? destination_city->client.lifecycle_id : 0,
                  v2_pending.before_target_building_present,
                  current_building_present);
    }
    case ACTRES_SPY_SABOTAGE_CITY_PRODUCTION: {
      bool current_city_binding_exact =
        destination_city != NULL && city_tile(destination_city) == target
        && destination_city->client.lifecycle_id
           == v2_pending.action.destination_city_lifecycle_id
        && v2_existing_incarnation(
             AGENT_V2_ENTITY_CITY, destination_city->id)
           == v2_pending.action.destination_city_incarnation;
      bool visible_effect_corroborated =
        destination_city != NULL
        && v2_pending.before_destination_city_internals_exact
        && can_player_see_city_internals(self, destination_city)
        && destination_city->shield_stock
           < v2_pending.before_destination_city_shield_stock;

      return fc_agent_v2_sabotage_city_postcondition(
        v2_pending.before_special_target_exact,
        v2_pending.action_success_receipt_latched,
        v2_pending.action.destination_city_lifecycle_id,
        v2_pending.before_destination_city_present,
        v2_pending.before_destination_city_lifecycle_id,
        destination_city != NULL, current_city_binding_exact,
        destination_city != NULL
          ? destination_city->client.lifecycle_id : 0,
        visible_effect_corroborated);
    }
    case ACTRES_SPY_STEAL_TECH:
      return fc_agent_v2_espionage_effect_postcondition(
        v2_pending.before_special_target_exact,
        v2_pending.before_research_exact,
        v2_pending.action.destination_city_lifecycle_id,
        v2_pending.before_destination_city_present,
        v2_pending.before_destination_city_lifecycle_id,
        destination_city != NULL
          && v2_existing_incarnation(
               AGENT_V2_ENTITY_CITY, destination_city->id)
             == v2_pending.action.destination_city_incarnation,
        destination_city != NULL
          ? destination_city->client.lifecycle_id : 0,
        v2_technology_acquired(
          &v2_pending.before_known_techs, research)
          || (research != NULL && v2_pending.before_future_tech >= 0
              && research->future_tech > v2_pending.before_future_tech));
    case ACTRES_SPY_TARGETED_STEAL_TECH:
      return fc_agent_v2_espionage_effect_postcondition(
        v2_pending.before_special_target_exact,
        v2_pending.before_research_exact
          && v2_pending.action.target_tech >= A_FIRST
          && v2_pending.action.target_tech < A_LAST,
        v2_pending.action.destination_city_lifecycle_id,
        v2_pending.before_destination_city_present,
        v2_pending.before_destination_city_lifecycle_id,
        destination_city != NULL
          && v2_existing_incarnation(
               AGENT_V2_ENTITY_CITY, destination_city->id)
             == v2_pending.action.destination_city_incarnation,
        destination_city != NULL
          ? destination_city->client.lifecycle_id : 0,
        research != NULL
          && !BV_ISSET(v2_pending.before_known_techs,
                       v2_pending.action.target_tech)
          && research_invention_state(
               research, v2_pending.action.target_tech) == TECH_KNOWN);
    case ACTRES_STRIKE_PRODUCTION:
      return destination_city != NULL
             && v2_pending.before_destination_city_internals_exact
             && destination_city->shield_stock
                < v2_pending.before_destination_city_shield_stock;
    case ACTRES_SPY_STEAL_GOLD:
      return self->economic.gold != v2_pending.before_player_gold;
    case ACTRES_SPY_SABOTAGE_UNIT: {
      bool current_target_binding_exact =
        target_unit != NULL
        && target_unit->client.lifecycle_id
           == v2_pending.action.target_unit_lifecycle_id
        && v2_existing_incarnation(
             AGENT_V2_ENTITY_UNIT, target_unit->id)
           == v2_pending.action.target_unit_incarnation;

      return native->id == ACTION_SPY_SABOTAGE_UNIT_ESC
             && fc_agent_v2_sabotage_unit_postcondition(
                  v2_pending.before_special_target_exact,
                  v2_pending.sabotage_unit_success_event_latched,
                  v2_pending.action.target_unit_lifecycle_id,
                  v2_pending.before_target_unit_present,
                  v2_pending.before_target_unit_lifecycle_id,
                  v2_pending.before_target_unit_hp,
                  target_unit != NULL, current_target_binding_exact,
                  target_unit != NULL ? target_unit->hp : -1);
    }
    case ACTRES_PARADROP_CONQUER: {
      struct city *current_city = target != NULL ? tile_city(target) : NULL;
      struct player *current_extra_owner = target != NULL
                                           ? extra_owner(target) : NULL;
      bv_extras current_huts = v2_hut_extras_on_tile(target);

      return native->id == ACTION_PARADROP_ENTER_CONQUER
             && target != NULL
             && client_tile_get_known(target) == TILE_KNOWN_SEEN
             && fc_agent_v2_paradrop_enter_conquer_postcondition(
                  v2_pending.before_special_target_exact,
                  v2_pending.action.unit_lifecycle_id,
                  v2_pending.before_unit_present,
                  v2_pending.before_unit_lifecycle_id,
                  v2_pending.before_unit_tile,
                  v2_pending.before_unit_paradropped,
                  current_unit_present,
                  current_unit_present ? unit->client.lifecycle_id : 0,
                  current_unit_tile,
                  current_unit_present && unit->paradropped,
                  v2_pending.action.target_tile, player_number(self),
                  v2_pending.action.special_target_city_id,
                  v2_pending.action.special_target_city_owner,
                  current_city != NULL ? current_city->id : -1,
                  current_city != NULL
                    ? player_number(city_owner(current_city)) : -1,
                  v2_pending.before_extra_owner,
                  current_extra_owner != NULL
                    ? player_number(current_extra_owner) : -1,
                  BV_ISSET_ANY(v2_pending.before_hut_extras),
                  v2_hut_extra_removed(
                    &v2_pending.before_hut_extras, &current_huts));
    }
    case ACTRES_CONQUER_EXTRAS: {
      struct player *owner = target != NULL ? extra_owner(target) : NULL;

      return fc_agent_v2_conquer_extras_postcondition(
        v2_pending.before_special_target_exact,
        v2_pending.before_extra_owner, player_number(self),
        owner != NULL ? player_number(owner) : -1,
        v2_pending.action.unit_lifecycle_id,
        current_unit_present,
        current_unit_present ? unit->client.lifecycle_id : 0,
        current_unit_tile, v2_pending.action.target_tile);
    }
    case ACTRES_HUT_ENTER:
    case ACTRES_HUT_FRIGHTEN: {
      bv_extras current_huts = v2_hut_extras_on_tile(target);

      return fc_agent_v2_hut_transition_postcondition(
        v2_pending.before_special_target_exact,
        BV_ISSET_ANY(v2_pending.before_hut_extras),
        v2_hut_extra_removed(
          &v2_pending.before_hut_extras, &current_huts),
        v2_pending.action.unit_lifecycle_id,
        current_unit_present,
        current_unit_present ? unit->client.lifecycle_id : 0,
        current_unit_tile, v2_pending.action.target_tile);
    }
    case ACTRES_CAPTURE_UNITS:
    case ACTRES_BOMBARD:
    case ACTRES_EXPEL_UNIT:
    case ACTRES_HEAL_UNIT:
    case ACTRES_WIPE_UNITS:
      return FALSE;
    case ACTRES_COLLECT_RANSOM:
      return v2_classic_collect_ransom_action(native)
             && v2_pending.before_special_target_exact
             && v2_pending.before_unit_present
             && v2_pending.action.unit_lifecycle_id != 0
             && v2_pending.before_unit_lifecycle_id
                == v2_pending.action.unit_lifecycle_id
             && v2_pending.combat_info_latched;
    case ACTRES_SPY_ATTACK:
      return native->id == ACTION_SPY_ATTACK
             && fc_agent_v2_spy_attack_postcondition(
                  v2_pending.before_special_target_exact,
                  v2_pending.action.unit_lifecycle_id,
                  v2_pending.before_unit_present,
                  v2_pending.before_unit_lifecycle_id,
                  v2_pending.spy_attack_actor_loss_event_latched,
                  v2_pending.spy_attack_target_loss_event_latched,
                  any_actor != NULL, current_unit_present,
                  v2_pending.before_target_signature,
                  v2_visible_stack_signature(
                    self, v2_pending.action.target_tile));
    case ACTRES_NUKE:
    case ACTRES_NUKE_UNITS:
      return v2_classic_nuke_action(native)
             && fc_agent_v2_nuke_postcondition(
                  v2_pending.before_special_target_exact,
                  v2_pending.nuke_tile_info_latched,
                  v2_pending.action.unit_lifecycle_id,
                  v2_pending.before_unit_present,
                  v2_pending.before_unit_lifecycle_id,
                  game_unit_by_number(v2_pending.action.unit_id) != NULL);
    default:
      return FALSE;
    }
  }
  case AGENT_V2_ACTION_KIND_COUNT:
    return FALSE;
  }
  return FALSE;
}

static void v2_progress_pending(void)
{
  if (!v2_pending.active) {
    return;
  }
  switch (v2_pending.terminal) {
  case FC_AGENT_V2_TERMINAL_APPLIED:
    v2_action_result("applied", "POSTCONDITION_VERIFIED");
    return;
  case FC_AGENT_V2_TERMINAL_POSTCONDITION_NOT_MET:
    v2_action_result("rejected", "POSTCONDITION_NOT_MET");
    return;
  case FC_AGENT_V2_TERMINAL_PROCESSING_BOUNDARY_MISMATCH:
    v2_action_result("rejected", "PROCESSING_BOUNDARY_MISMATCH");
    return;
  case FC_AGENT_V2_TERMINAL_SEAT_EPOCH_CHANGED:
    v2_action_result("rejected", "SEAT_EPOCH_CHANGED");
    return;
  case FC_AGENT_V2_TERMINAL_NONE:
    break;
  }
  if (v2_pending.timer != NULL
      && timer_read_seconds(v2_pending.timer) >= AGENT_V2_ACTION_TIMEOUT) {
    v2_action_result("timeout", "PROCESSING_TIMEOUT");
    return;
  }
}

static bool v2_validate_open(char **fields, size_t count)
{
  if (count != 3 || !v2_token_valid(fields[1])
      || strcmp(fields[2], "state") != 0) {
    v2_error(count > 1 ? fields[1] : NULL, "BAD_REQUEST",
             "OBS_OPEN requires request and state");
    return FALSE;
  }
  return TRUE;
}

static bool v2_validate_page(char **fields, size_t count)
{
  size_t offset;
  size_t limit;

  if (count != 5 || !v2_token_valid(fields[1])
      || !v2_parse_size(fields[3], &offset)
      || !v2_parse_size(fields[4], &limit)
      || limit == 0 || limit > AGENT_V2_PAGE_MAX) {
    v2_error(count > 1 ? fields[1] : NULL, "BAD_REQUEST",
             "OBS_PAGE requires snapshot offset and limit 1 through 16");
    return FALSE;
  }
  return TRUE;
}

static bool v2_validate_action(char **fields, size_t count)
{
  if (count != 4 || !v2_token_valid(fields[1])
      || !v2_token_valid(fields[2])) {
    v2_error(count > 1 ? fields[1] : NULL, "BAD_REQUEST",
             "ACT requires request slot and args");
    return FALSE;
  }
  return TRUE;
}

static bool v2_validate_scope_open(char **fields, size_t count)
{
  uint64_t revision;
  char kind;
  int id;
  uint64_t incarnation;

  if (count != 4 || !v2_token_valid(fields[1])
      || !v2_parse_revision(fields[2], &revision)
      || !fc_agent_v2_parse_entity_ref(fields[3], &kind, &id,
                                       &incarnation)) {
    v2_error(count > 1 ? fields[1] : NULL, "BAD_REQUEST",
             "SCOPE_OPEN requires request revision and actor");
    return FALSE;
  }
  return TRUE;
}

static bool v2_validate_scope_page(char **fields, size_t count)
{
  size_t offset;
  size_t limit;

  if (count != 5 || !v2_token_valid(fields[1])
      || !v2_token_valid(fields[2])
      || !v2_parse_size(fields[3], &offset)
      || !v2_parse_size(fields[4], &limit)
      || limit == 0 || limit > AGENT_V2_PAGE_MAX) {
    v2_error(count > 1 ? fields[1] : NULL, "BAD_REQUEST",
             "SCOPE_PAGE requires view offset and limit 1 through 16");
    return FALSE;
  }
  return TRUE;
}

static bool v2_validate_state_scope_open(char **fields, size_t count)
{
  uint64_t revision;

  if (count != 5 || !v2_token_valid(fields[1])
      || !v2_parse_revision(fields[2], &revision)
      || !v2_token_valid(fields[3])
      || fields[4][0] == '\0' || strlen(fields[4]) >= 64) {
    v2_error(count > 1 ? fields[1] : NULL, "BAD_REQUEST",
             "STATE_SCOPE_OPEN requires request revision section selector");
    return FALSE;
  }
  return TRUE;
}

static bool v2_validate_state_scope_page(char **fields, size_t count)
{
  size_t offset;
  size_t limit;

  if (count != 5 || !v2_token_valid(fields[1])
      || !v2_token_valid(fields[2])
      || !v2_parse_size(fields[3], &offset)
      || !v2_parse_size(fields[4], &limit)
      || limit == 0 || limit > AGENT_V2_PAGE_MAX) {
    v2_error(count > 1 ? fields[1] : NULL, "BAD_REQUEST",
             "STATE_SCOPE_PAGE requires view offset and limit 1 through 16");
    return FALSE;
  }
  return TRUE;
}

static bool v2_validate_relation_scope_open(char **fields, size_t count)
{
  uint64_t revision;
  char actor_kind;
  char counterpart_kind;
  int actor_id;
  int counterpart_id;
  uint64_t actor_incarnation;
  uint64_t counterpart_incarnation;

  if (count != 5 || !v2_token_valid(fields[1])
      || !v2_parse_revision(fields[2], &revision)
      || !fc_agent_v2_parse_entity_ref(
           fields[3], &actor_kind, &actor_id, &actor_incarnation)
      || !fc_agent_v2_parse_entity_ref(
           fields[4], &counterpart_kind, &counterpart_id,
           &counterpart_incarnation)) {
    v2_error(count > 1 ? fields[1] : NULL, "BAD_REQUEST",
             "RELATION_SCOPE_OPEN requires request revision actor and counterpart");
    return FALSE;
  }
  return TRUE;
}

static bool v2_validate_relation_scope_page(char **fields, size_t count)
{
  size_t offset;
  size_t limit;

  if (count != 5 || !v2_token_valid(fields[1])
      || !v2_token_valid(fields[2])
      || !v2_parse_size(fields[3], &offset)
      || !v2_parse_size(fields[4], &limit)
      || limit == 0 || limit > AGENT_V2_PAGE_MAX) {
    v2_error(count > 1 ? fields[1] : NULL, "BAD_REQUEST",
             "RELATION_SCOPE_PAGE requires view offset and limit 1 through 16");
    return FALSE;
  }
  return TRUE;
}

static bool v2_validate_cap_action(char **fields, size_t count)
{
  uint64_t revision;
  char kind;
  int id;
  uint64_t incarnation;

  if (count != 6 || !v2_token_valid(fields[1])
      || !v2_parse_revision(fields[2], &revision)
      || !fc_agent_v2_parse_entity_ref(fields[3], &kind, &id,
                                       &incarnation)
      || !v2_token_valid(fields[4])) {
    v2_error(count > 1 ? fields[1] : NULL, "BAD_REQUEST",
             "ACT_CAP requires request revision actor slot and args");
    return FALSE;
  }
  return TRUE;
}

static bool v2_validate_relation_cap_action(char **fields, size_t count)
{
  uint64_t revision;
  char actor_kind;
  char counterpart_kind;
  int actor_id;
  int counterpart_id;
  uint64_t actor_incarnation;
  uint64_t counterpart_incarnation;

  if (count != 7 || !v2_token_valid(fields[1])
      || !v2_parse_revision(fields[2], &revision)
      || !fc_agent_v2_parse_entity_ref(
           fields[3], &actor_kind, &actor_id, &actor_incarnation)
      || !fc_agent_v2_parse_entity_ref(
           fields[4], &counterpart_kind, &counterpart_id,
           &counterpart_incarnation)
      || !v2_token_valid(fields[5])) {
    v2_error(count > 1 ? fields[1] : NULL, "BAD_REQUEST",
             "ACT_RELATION_CAP requires request revision actor counterpart slot and args");
    return FALSE;
  }
  return TRUE;
}

static bool v2_validate_target_action(char **fields, size_t count)
{
  uint64_t revision;
  char kind;
  int id;
  uint64_t incarnation;
  size_t native_tile;

  if (count != 5 || !v2_token_valid(fields[1])
      || !v2_parse_revision(fields[2], &revision)
      || !fc_agent_v2_parse_entity_ref(fields[3], &kind, &id,
                                       &incarnation)
      || !v2_parse_size(fields[4], &native_tile)
      || native_tile > INT_MAX) {
    v2_error(count > 1 ? fields[1] : NULL, "BAD_REQUEST",
             "TARGET_ACTION requires request revision actor and tile");
    return FALSE;
  }
  return TRUE;
}

static bool v2_research_action_still_legal(
  const struct research *research, const struct agent_v2_action *action)
{
  Tech_type_id tech = action->target_tech;

  if (research == NULL) {
    return FALSE;
  }
  switch (action->kind) {
  case AGENT_V2_ACTION_RESEARCH_TARGET:
    return v2_research_can_target(research, tech)
           && research->researching != tech;
  case AGENT_V2_ACTION_RESEARCH_GOAL:
    return v2_research_can_goal(research, tech)
           && research->tech_goal != tech;
  default:
    return FALSE;
  }
}

static bool v2_action_production(
  const struct agent_v2_action *action, struct universal *production)
{
  if (action == NULL || production == NULL
      || (action->target_build_kind != VUT_UTYPE
          && action->target_build_kind != VUT_IMPROVEMENT)
      || action->target_build_id < 0) {
    return FALSE;
  }
  *production = universal_by_number(
    action->target_build_kind, action->target_build_id);
  return v2_production_supported(production)
         && production->kind == action->target_build_kind
         && universal_number(production) == action->target_build_id;
}

static bool v2_city_action_still_legal(
  const struct player *self, struct city *pcity,
  const struct agent_v2_action *action, struct universal *production)
{
  if (self == NULL || pcity == NULL || city_owner(pcity) != self
      || !fc_agent_v2_city_lifetime_matches(
        action->city_lifecycle_id, pcity->client.lifecycle_id)
      || v2_existing_incarnation(AGENT_V2_ENTITY_CITY, pcity->id)
         != action->city_incarnation) {
    return FALSE;
  }
  if (action->kind == AGENT_V2_ACTION_CITY_PRODUCTION) {
    return v2_action_production(action, production)
           && city_can_change_build(pcity)
           && !are_universals_equal(&pcity->production, production)
           && can_city_build_now(&wld.map, pcity, production, RPT_CERTAIN);
  }
  if (action->kind == AGENT_V2_ACTION_CITY_BUY) {
    return v2_action_production(action, production)
           && are_universals_equal(&pcity->production, production)
           && city_can_buy(pcity)
           && pcity->client.buy_cost > 0
           && pcity->client.buy_cost <= self->economic.gold;
  }
  if (action->kind == AGENT_V2_ACTION_CITY_SET_WORKLIST) {
    return v2_city_worklist_action_available(pcity)
           && action->target_build_kind == VUT_NONE
           && action->target_build_id == -1
           && action->target_tile == -1
           && action->source_specialist == -1
           && action->target_specialist == -1;
  }
  if (action->kind == AGENT_V2_ACTION_CITY_SET_OPTIONS
      || action->kind == AGENT_V2_ACTION_CITY_RENAME) {
    return action->target_build_kind == VUT_NONE
           && action->target_build_id == -1
           && action->target_tile == -1
           && action->source_specialist == -1
           && action->target_specialist == -1;
  }
  if (action->kind == AGENT_V2_ACTION_CITY_SELL_IMPROVEMENT) {
    return !pcity->did_sell
           && v2_action_production(action, production)
           && production->kind == VUT_IMPROVEMENT
           && can_city_sell_building(pcity, production->value.building);
  }
  if (action->kind == AGENT_V2_ACTION_CITY_SET_RALLY) {
    const struct unit_type *putype;

    if (pcity->production.kind != VUT_UTYPE
        || (putype = pcity->production.value.utype) == NULL
        || city_tile(pcity) == NULL
        || !v2_tile_index_valid(action->target_tile)
        || client_tile_get_known(
             index_to_tile(&wld.map, action->target_tile)) == TILE_UNKNOWN) {
      return FALSE;
    }
    return action->rally_source_tile == tile_index(city_tile(pcity))
           && action->rally_production_unit_type == utype_number(putype)
           && action->rally_veteran_level
              == city_production_unit_veteran_level(pcity, putype)
           && action->target_tile != action->rally_source_tile
           && action->rally_order_count > 0
           && action->rally_order_count < MAX_LEN_ROUTE
           && !action->rally_action_move;
  }
  if (action->kind == AGENT_V2_ACTION_CITY_CLEAR_RALLY) {
    return v2_city_rally_active(pcity)
           && action->target_build_kind == VUT_NONE
           && action->target_build_id == -1
           && action->target_tile == -1
           && action->source_specialist == -1
           && action->target_specialist == -1;
  }
  if (action->kind == AGENT_V2_ACTION_CITY_SET_GOVERNOR
      || action->kind == AGENT_V2_ACTION_CITY_CLEAR_GOVERNOR) {
    return action->target_build_kind == VUT_NONE
           && action->target_build_id == -1
           && action->target_tile == -1
           && action->source_specialist == -1
           && action->target_specialist == -1
           && (action->kind == AGENT_V2_ACTION_CITY_SET_GOVERNOR
               || cma_is_city_under_agent(pcity, NULL));
  }
  if (action->kind == AGENT_V2_ACTION_CITY_REQUEST_WORKER_TASK
      || action->kind == AGENT_V2_ACTION_CITY_CHANGE_WORKER_TASK
      || action->kind == AGENT_V2_ACTION_CITY_REMOVE_WORKER_TASK) {
    struct tile *target;
    const struct worker_task *current;
    const struct extra_type *legal_target = NULL;
    int current_extra;

    if (!v2_tile_index_valid(action->target_tile)
        || (target = index_to_tile(&wld.map, action->target_tile)) == NULL
        || !v2_city_worker_task_tile_allowed(self, pcity, target)) {
      return FALSE;
    }
    current = v2_city_worker_task_at(pcity, target);
    current_extra = current != NULL && current->tgt != NULL
                    ? extra_number(current->tgt) : EXTRA_NONE;
    if ((current != NULL) != action->worker_task_baseline_present
        || (current != NULL
            && (current->act != action->worker_task_baseline_activity
                || current_extra != action->worker_task_baseline_extra
                || current->want != action->worker_task_baseline_want))) {
      return FALSE;
    }
    if (action->kind == AGENT_V2_ACTION_CITY_REMOVE_WORKER_TASK) {
      return current != NULL && action->target_activity == ACTIVITY_LAST
             && action->target_extra == EXTRA_NONE;
    }
    if ((action->kind == AGENT_V2_ACTION_CITY_REQUEST_WORKER_TASK)
        != (current == NULL)
        || !v2_city_worker_task_choice(
             self, target, action->target_activity, &legal_target)) {
      return FALSE;
    }
    return action->target_extra
           == (legal_target != NULL
               ? extra_number(legal_target) : EXTRA_NONE)
           && (current == NULL
               || current->act != action->target_activity
               || current_extra != action->target_extra
               || current->want != AGENT_V2_CITY_WORKER_TASK_WANT);
  }
  if (action->kind == AGENT_V2_ACTION_CITY_WORK_TILE
      || action->kind == AGENT_V2_ACTION_CITY_UNWORK_TILE) {
    struct tile *target;

    if (cma_is_city_under_agent(pcity, NULL)
        || !v2_tile_index_valid(action->target_tile)) {
      return FALSE;
    }
    target = index_to_tile(&wld.map, action->target_tile);
    if (target == NULL || !city_map_includes_tile(pcity, target)
        || is_free_worked(pcity, target)) {
      return FALSE;
    }
    if (action->kind == AGENT_V2_ACTION_CITY_WORK_TILE) {
      return action->source_specialist == v2_first_city_specialist(pcity)
             && action->source_specialist >= 0
             && action->target_specialist == -1
             && tile_worked(target) != pcity
             && city_can_work_tile(pcity, target);
    }
    return action->source_specialist == -1
           && action->target_specialist == DEFAULT_SPECIALIST
           && tile_worked(target) == pcity;
  }
  return action->kind == AGENT_V2_ACTION_CITY_SET_SPECIALIST
         && !cma_is_city_under_agent(pcity, NULL)
         && action->target_tile == -1
         && is_normal_specialist_id(action->source_specialist)
         && is_normal_specialist_id(action->target_specialist)
         && action->source_specialist != action->target_specialist
         && pcity->specialists[action->source_specialist] > 0
         && city_can_use_specialist(pcity, action->target_specialist);
}

static bool v2_worker_action_still_legal(
  const struct unit *punit, const struct agent_v2_action *action,
  struct extra_type **target)
{
  struct act_prob probability;

  *target = action->target_extra != EXTRA_NONE
            ? extra_by_number(action->target_extra) : NULL;
  if (punit == NULL || unit_tile(punit) == NULL
      || v2_existing_incarnation(AGENT_V2_ENTITY_UNIT, punit->id)
         != action->unit_incarnation) {
    return FALSE;
  }
  if (action->kind == AGENT_V2_ACTION_CANCEL_ACTIVITY) {
    return action->target_activity == ACTIVITY_IDLE
           && action->target_extra == EXTRA_NONE
           && punit->activity != ACTIVITY_IDLE
           && can_unit_do_activity_client(punit, ACTIVITY_IDLE);
  }
  if (action->kind != AGENT_V2_ACTION_WORKER_START
      || action->action == ACTION_NONE
      || action_by_number(action->action) == NULL
      || action_get_activity(action_by_number(action->action))
         != action->target_activity
      || punit->activity == action->target_activity
      || (action->target_extra != EXTRA_NONE && *target == NULL)
      || (activity_requires_target(action->target_activity)
          != (action->target_extra != EXTRA_NONE))) {
    return FALSE;
  }
  probability = v2_worker_probability(
    punit, action_by_number(action->action), *target);
  return action_prob_possible(probability);
}

static bool v2_unit_automation_action_still_legal(
  const struct player *self, const struct unit *punit,
  const struct agent_v2_action *action)
{
  if (self == NULL || punit == NULL || unit_owner(punit) != self
      || punit->client.lifecycle_id == 0
      || punit->client.lifecycle_id != action->unit_lifecycle_id
      || v2_existing_incarnation(AGENT_V2_ENTITY_UNIT, punit->id)
         != action->unit_incarnation) {
    return FALSE;
  }
  switch (action->kind) {
  case AGENT_V2_ACTION_UNIT_AUTO_WORK:
    return v2_unit_automation_start_clean(punit)
           && can_unit_do_autoworker(punit);
  case AGENT_V2_ACTION_UNIT_AUTO_EXPLORE:
    return v2_unit_automation_start_clean(punit)
           && can_unit_do_activity(
                &wld.map, punit, ACTIVITY_EXPLORE,
                activity_default_action(ACTIVITY_EXPLORE));
  case AGENT_V2_ACTION_UNIT_CANCEL_AUTOMATION:
    return (punit->ssa_controller == SSA_AUTOWORKER
            || punit->ssa_controller == SSA_AUTOEXPLORE)
           && can_unit_do_activity_client(punit, ACTIVITY_IDLE);
  default:
    return FALSE;
  }
}

static bool v2_unit_cancel_orders_action_still_legal(
  const struct player *self, const struct unit *punit,
  const struct agent_v2_action *action)
{
  return action->kind == AGENT_V2_ACTION_UNIT_CANCEL_ORDERS
         && self != NULL && punit != NULL && unit_owner(punit) == self
         && punit->client.lifecycle_id != 0
         && punit->client.lifecycle_id == action->unit_lifecycle_id
         && v2_existing_incarnation(AGENT_V2_ENTITY_UNIT, punit->id)
            == action->unit_incarnation
         && v2_unit_cancel_orders_available(punit);
}

static bool v2_unit_goto_action_still_legal(
  const struct player *self, const struct unit *punit,
  const struct agent_v2_action *action)
{
  struct client_goto_pathfinder *finder;
  struct client_goto_path_info path = {0};
  struct tile *target;
  bool legal;

  if (action->kind != AGENT_V2_ACTION_UNIT_GOTO
      || self == NULL || punit == NULL || unit_owner(punit) != self
      || punit->client.lifecycle_id == 0
      || punit->client.lifecycle_id != action->unit_lifecycle_id
      || v2_existing_incarnation(AGENT_V2_ENTITY_UNIT, punit->id)
         != action->unit_incarnation
      || !v2_unit_goto_actor_clean(punit)
      || unit_tile(punit) == NULL
      || tile_index(unit_tile(punit)) != action->source_unit_tile
      || !v2_tile_index_valid(action->target_tile)
      || action->target_tile == action->source_unit_tile
      || action->goto_order_count < 1
      || action->goto_order_count >= MAX_LEN_ROUTE) {
    return FALSE;
  }
  target = index_to_tile(&wld.map, action->target_tile);
  if ((action->slot[0] != 't'
          && real_map_distance(unit_tile(punit), target)
             > AGENT_V2_GOTO_MAX_DISTANCE)) {
    return FALSE;
  }
  finder = client_goto_pathfinder_new(punit);
  if (finder == NULL) {
    return FALSE;
  }
  legal = client_goto_pathfinder_destination(finder, target, &path)
          && path.action_move == action->goto_action_move
          && path.destination_tile == action->goto_destination_tile
          && path.order_count == action->goto_order_count
          && path.orders_digest == action->goto_orders_digest
          && path.route_signature == action->goto_route_signature;
  client_goto_pathfinder_destroy(finder);
  return legal;
}

static bool v2_route_plan_matches_action(
  const struct client_unit_route_plan *plan,
  const struct agent_v2_action *action)
{
  const struct client_unit_route_plan_info *info =
    client_unit_route_plan_get_info(plan);

  return info != NULL && action != NULL
         && info->source_tile == action->source_unit_tile
         && info->destination_tile == action->goto_destination_tile
         && info->target_tile == action->target_tile
         && info->order_count == action->goto_order_count
         && info->orders_digest == action->goto_orders_digest
         && info->route_signature == action->goto_route_signature
         && info->action_move == action->goto_action_move
         && !info->repeat && !info->vigilant
         && fc_agent_v2_unit_route_shape_matches(
              action->kind, info->action_move,
              info->final_action, info->final_subtarget,
              ACTION_NONE, NO_TARGET,
              action->kind == AGENT_V2_ACTION_UNIT_GOTO
              ? ACTION_NONE : action->action,
              action->kind == AGENT_V2_ACTION_UNIT_CONNECT_ROUTE
              ? action->target_extra : NO_TARGET);
}

static bool v2_routed_action_target_still_bound(
  const struct player *self, const struct agent_v2_action *action)
{
  const struct action *native;
  struct tile *target;

  if (self == NULL || action == NULL
      || action->kind != AGENT_V2_ACTION_UNIT_GOTO_AND_PERFORM
      || (native = action_by_number(action->action)) == NULL
      || !v2_goto_and_perform_shape_supported(native)
      || !v2_tile_index_valid(action->target_tile)
      || (target = index_to_tile(&wld.map, action->target_tile)) == NULL) {
    return FALSE;
  }
  switch (action_get_target_kind(native)) {
  case ATK_CITY: {
    struct city *city = game_city_by_number(action->destination_city_id);

    return city != NULL && city_tile(city) == target
           && city->client.lifecycle_id != 0
           && city->client.lifecycle_id
              == action->destination_city_lifecycle_id
           && v2_existing_incarnation(AGENT_V2_ENTITY_CITY, city->id)
              == action->destination_city_incarnation;
  }
  case ATK_UNIT: {
    struct unit *unit = game_unit_by_number(action->target_unit_id);

    return unit != NULL && unit_tile(unit) == target
           && unit->client.lifecycle_id != 0
           && unit->client.lifecycle_id == action->target_unit_lifecycle_id
           && v2_existing_incarnation(AGENT_V2_ENTITY_UNIT, unit->id)
              == action->target_unit_incarnation;
  }
  case ATK_STACK:
    return action->target_stack_signature
           == v2_visible_stack_signature(self, action->target_tile);
  case ATK_TILE:
    return TRUE;
  case ATK_SELF:
  case ATK_EXTRAS:
  case ATK_COUNT:
    return FALSE;
  }
  return FALSE;
}

static bool v2_connect_route_still_bound(
  const struct player *self, const struct unit *punit,
  const struct agent_v2_action *action, struct extra_type **extra)
{
  struct tile *target;

  if (self == NULL || punit == NULL || action == NULL || extra == NULL
      || action->kind != AGENT_V2_ACTION_UNIT_CONNECT_ROUTE
      || unit_owner(punit) != self || punit->client.lifecycle_id == 0
      || punit->client.lifecycle_id != action->unit_lifecycle_id
      || v2_existing_incarnation(AGENT_V2_ENTITY_UNIT, punit->id)
         != action->unit_incarnation
      || !v2_unit_goto_actor_clean(punit)
      || unit_tile(punit) == NULL
      || tile_index(unit_tile(punit)) != action->source_unit_tile
      || !v2_tile_index_valid(action->target_tile)
      || (target = index_to_tile(&wld.map, action->target_tile)) == NULL
      || (*extra = extra_by_number(action->target_extra)) == NULL
      || (action->target_activity != ACTIVITY_GEN_ROAD
          && action->target_activity != ACTIVITY_IRRIGATE)
      || action->action
         != activity_default_action(action->target_activity)) {
    return FALSE;
  }
  return client_tile_get_known(target) != TILE_UNKNOWN
         && can_unit_do_connect((struct unit *) punit,
                                action->target_activity, *extra);
}

static bool v2_unit_set_route_action_still_legal(
  const struct player *self, const struct unit *punit,
  const struct agent_v2_action *action)
{
  return action->kind == AGENT_V2_ACTION_UNIT_SET_ROUTE
         && self != NULL && punit != NULL && unit_owner(punit) == self
         && punit->client.lifecycle_id != 0
         && punit->client.lifecycle_id == action->unit_lifecycle_id
         && v2_existing_incarnation(AGENT_V2_ENTITY_UNIT, punit->id)
            == action->unit_incarnation
         && action->route_waypoint_limit
            == CLIENT_UNIT_ROUTE_MAX_WAYPOINTS
         && v2_unit_goto_actor_clean(punit);
}

static bool v2_self_unit_action_still_legal(
  const struct unit *punit, const struct agent_v2_action *action)
{
  const struct action *native;
  enum action_result expected_result;
  enum agent_v2_probability_kind probability_kind;
  struct act_prob probability;
  int probability_min;
  int probability_max;

  if (punit == NULL || unit_tile(punit) == NULL
      || v2_existing_incarnation(AGENT_V2_ENTITY_UNIT, punit->id)
         != action->unit_incarnation) {
    return FALSE;
  }
  if (action->kind == AGENT_V2_ACTION_UNIT_SENTRY) {
    return action->action == ACTION_NONE
           && action->target_activity == ACTIVITY_SENTRY
           && action->target_build_kind == VUT_NONE
           && action->target_build_id == -1
           && punit->activity != ACTIVITY_SENTRY
           && can_unit_do_activity_client(punit, ACTIVITY_SENTRY);
  }
  native = action_by_number(action->action);
  if (native == NULL || action_get_actor_kind(native) != AAK_UNIT
      || action_get_target_kind(native) != ATK_SELF) {
    return FALSE;
  }
  switch (action->kind) {
  case AGENT_V2_ACTION_UNIT_FORTIFY:
    expected_result = ACTRES_FORTIFY;
    if (action->target_activity != ACTIVITY_FORTIFYING
        || action->target_build_kind != VUT_NONE
        || punit->activity == ACTIVITY_FORTIFYING
        || punit->activity == ACTIVITY_FORTIFIED) {
      return FALSE;
    }
    break;
  case AGENT_V2_ACTION_UNIT_CONVERT:
    expected_result = ACTRES_CONVERT;
    if (action->target_activity != ACTIVITY_CONVERT
        || action->target_build_kind != VUT_UTYPE
        || punit->activity == ACTIVITY_CONVERT
        || unit_type_get(punit)->converted_to == NULL
        || utype_number(unit_type_get(punit)->converted_to)
           != action->target_build_id) {
      return FALSE;
    }
    break;
  case AGENT_V2_ACTION_UNIT_DISBAND:
    expected_result = ACTRES_DISBAND_UNIT;
    if (action->target_activity != ACTIVITY_LAST
        || action->target_build_kind != VUT_NONE) {
      return FALSE;
    }
    break;
  case AGENT_V2_ACTION_UNIT_HOMELESS:
    expected_result = ACTRES_HOMELESS;
    if (action->target_activity != ACTIVITY_LAST
        || action->target_build_kind != VUT_NONE
        || punit->homecity == IDENTITY_NUMBER_ZERO) {
      return FALSE;
    }
    break;
  default:
    return FALSE;
  }
  if (native->result != expected_result
      || native->actor_consuming_always
         != (expected_result == ACTRES_DISBAND_UNIT)) {
    return FALSE;
  }
  probability = action_prob_self(&wld.map, punit, native->id);
  return action_prob_possible(probability)
         && v2_normalize_probability(
              probability, &probability_kind,
              &probability_min, &probability_max)
         && probability_kind == action->probability_kind
         && probability_min == action->probability_min
         && probability_max == action->probability_max;
}

static bool v2_city_target_unit_action_still_legal(
  const struct player *self, const struct unit *punit,
  const struct agent_v2_action *action, struct city **destination_result)
{
  const struct action *native = action_by_number(action->action);
  const struct unit_type *upgrade = NULL;
  struct city *source = NULL;
  struct city *destination = game_city_by_number(
    action->destination_city_id);
  enum action_result expected_result;
  bool expected_consuming;
  struct act_prob probability;

  if (self == NULL || punit == NULL || unit_owner(punit) != self
      || unit_tile(punit) == NULL || punit->client.lifecycle_id == 0
      || punit->client.lifecycle_id != action->unit_lifecycle_id
      || v2_existing_incarnation(AGENT_V2_ENTITY_UNIT, punit->id)
         != action->unit_incarnation
      || native == NULL || action_get_actor_kind(native) != AAK_UNIT
      || action_get_target_kind(native) != ATK_CITY
      || !v2_city_site_known(destination)
      || !player_can_see_city_externals(self, destination)
      || destination->client.lifecycle_id
         != action->destination_city_lifecycle_id
      || v2_existing_incarnation(AGENT_V2_ENTITY_CITY, destination->id)
         != action->destination_city_incarnation
      || tile_index(city_tile(destination)) != action->destination_city_tile
      || action->target_tile != -1 || action->target_unit_id != -1
      || action->transport_context_id != -1 || action->target_tech != -1
      || action->target_government != -1
      || action->source_specialist != -1
      || action->target_specialist != -1
      || action->target_extra != EXTRA_NONE
      || action->target_activity != ACTIVITY_LAST
      || action->max_rate != 0) {
    return FALSE;
  }
  switch (action->kind) {
  case AGENT_V2_ACTION_UNIT_UPGRADE:
    expected_result = ACTRES_UPGRADE_UNIT;
    expected_consuming = FALSE;
    upgrade = can_upgrade_unittype(self, unit_type_get(punit));
    if (upgrade == NULL || action->target_build_kind != VUT_UTYPE
        || action->target_build_id != utype_number(upgrade)) {
      return FALSE;
    }
    break;
  case AGENT_V2_ACTION_UNIT_REHOME:
    expected_result = ACTRES_HOME_CITY;
    expected_consuming = FALSE;
    break;
  case AGENT_V2_ACTION_UNIT_JOIN_CITY:
    expected_result = ACTRES_JOIN_CITY;
    expected_consuming = TRUE;
    break;
  case AGENT_V2_ACTION_UNIT_ESTABLISH_TRADE:
    expected_result = ACTRES_TRADE_ROUTE;
    expected_consuming = TRUE;
    break;
  case AGENT_V2_ACTION_UNIT_MARKETPLACE:
    expected_result = ACTRES_MARKETPLACE;
    expected_consuming = TRUE;
    break;
  case AGENT_V2_ACTION_UNIT_HELP_WONDER:
    expected_result = ACTRES_HELP_WONDER;
    expected_consuming = TRUE;
    break;
  case AGENT_V2_ACTION_UNIT_DISBAND_RECOVER:
    expected_result = ACTRES_DISBAND_UNIT_RECOVER;
    expected_consuming = TRUE;
    if (action->action != ACTION_DISBAND_UNIT_RECOVER) {
      return FALSE;
    }
    break;
  default:
    return FALSE;
  }
  if (native->result != expected_result
      || native->actor_consuming_always != expected_consuming
      || (expected_result != ACTRES_UPGRADE_UNIT
          && (action->target_build_kind != VUT_NONE
              || action->target_build_id != -1))) {
    return FALSE;
  }
  if (expected_result == ACTRES_TRADE_ROUTE
      || expected_result == ACTRES_MARKETPLACE) {
    source = player_city_by_number(self, action->source_city_id);
    if (source == NULL || source->id != punit->homecity
        || source->client.lifecycle_id == 0
        || source->client.lifecycle_id != action->source_city_lifecycle_id
        || v2_existing_incarnation(AGENT_V2_ENTITY_CITY, source->id)
           != action->source_city_incarnation
        || city_tile(source) == NULL
        || tile_index(city_tile(source)) != action->source_city_tile) {
      return FALSE;
    }
  } else if (action->source_city_id != -1
             || action->source_city_lifecycle_id != 0
             || action->source_city_incarnation != 0
             || action->source_city_tile != -1) {
    return FALSE;
  }
  probability = action_prob_vs_city(
    &wld.map, punit, native->id, destination);
  if (!v2_action_probability_matches(probability, action)) {
    return FALSE;
  }
  if (destination_result != NULL) {
    *destination_result = destination;
  }
  return TRUE;
}

static bool v2_action_city_matches(
  const struct player *self, int id, uint64_t incarnation,
  uint64_t lifecycle, struct city **result)
{
  struct city *city = self != NULL && id >= 0
                      ? player_city_by_number(self, id) : NULL;

  if (city == NULL || lifecycle == 0
      || city->client.lifecycle_id != lifecycle
      || v2_existing_incarnation(AGENT_V2_ENTITY_CITY, id) != incarnation) {
    return FALSE;
  }
  if (result != NULL) {
    *result = city;
  }
  return TRUE;
}

static bool v2_action_probability_matches(
  struct act_prob probability, const struct agent_v2_action *action)
{
  enum agent_v2_probability_kind kind;
  int minimum;
  int maximum;

  return action_prob_possible(probability)
         && v2_normalize_probability(
              probability, &kind, &minimum, &maximum)
         && kind == action->probability_kind
         && minimum == action->probability_min
         && maximum == action->probability_max;
}

static bool v2_noncombat_mobility_action_still_legal(
  const struct player *self, const struct unit *actor,
  const struct agent_v2_action *action)
{
  const struct action *native = action_by_number(action->action);
  struct act_prob probability;
  struct act_prob not_implemented = action_prob_new_not_impl();

  if (self == NULL || actor == NULL || unit_tile(actor) == NULL
      || native == NULL
      || !v2_noncombat_mobility_action_allowed(
           action->kind, action->action)
      || action_get_actor_kind(native) != AAK_UNIT
      || native->actor_consuming_always
      || action->target_unit_id != -1
      || action->transport_context_id != -1
      || action->target_tech != -1
      || action->target_government != -1
      || action->target_build_kind != VUT_NONE
      || action->target_build_id != -1
      || action->source_specialist != -1
      || action->target_specialist != -1
      || action->target_extra != EXTRA_NONE
      || action->target_activity != ACTIVITY_LAST
      || action->max_rate != 0) {
    return FALSE;
  }
  if (action->kind == AGENT_V2_ACTION_UNIT_AIRLIFT) {
    struct city *source = NULL;
    struct city *destination = NULL;

    if (native->result != ACTRES_AIRLIFT
        || action_get_target_kind(native) != ATK_CITY
        || action->target_tile != -1
        || !v2_action_city_matches(
             self, action->source_city_id,
             action->source_city_incarnation,
             action->source_city_lifecycle_id, &source)
        || !v2_action_city_matches(
             self, action->destination_city_id,
             action->destination_city_incarnation,
             action->destination_city_lifecycle_id, &destination)
        || source == destination || tile_city(unit_tile(actor)) != source
        || tile_index(city_tile(source)) != action->source_city_tile
        || tile_index(unit_tile(actor)) != action->source_city_tile
        || tile_index(city_tile(destination))
           != action->destination_city_tile) {
      return FALSE;
    }
    probability = action_prob_vs_city(
      &wld.map, actor, native->id, destination);
    if (are_action_probabilitys_equal(&probability, &not_implemented)) {
      return FALSE;
    }
  } else {
    struct tile *target;
    enum action_result expected =
      action->kind == AGENT_V2_ACTION_UNIT_PARADROP
      ? ACTRES_PARADROP : ACTRES_TELEPORT;

    if (native->result != expected
        || action_get_target_kind(native) != ATK_TILE
        || action->source_city_id != -1
        || action->destination_city_id != -1
        || !v2_tile_index_valid(action->target_tile)
        || (target = index_to_tile(&wld.map, action->target_tile)) == NULL
        || target == unit_tile(actor)
        || (action->kind == AGENT_V2_ACTION_UNIT_PARADROP
            ? client_tile_get_known(target) == TILE_UNKNOWN
            : client_tile_get_known(target) != TILE_KNOWN_SEEN)) {
      return FALSE;
    }
    probability = action_prob_vs_tile(
      &wld.map, actor, native->id, target, NULL);
    if (action->kind == AGENT_V2_ACTION_UNIT_TELEPORT
        && are_action_probabilitys_equal(
             &probability, &not_implemented)) {
      return FALSE;
    }
  }
  return v2_action_probability_matches(probability, action);
}

static bool v2_action_visible_unit_matches(
  const struct player *self, int id, uint64_t incarnation,
  uint64_t lifecycle, struct unit **result)
{
  struct unit *unit = self != NULL && id >= 0
                      ? game_unit_by_number(id) : NULL;

  if (!v2_transport_unit_visible(self, unit) || lifecycle == 0
      || unit->client.lifecycle_id != lifecycle
      || v2_existing_incarnation(AGENT_V2_ENTITY_UNIT, id) != incarnation) {
    return FALSE;
  }
  if (result != NULL) {
    *result = unit;
  }
  return TRUE;
}

static bool v2_transport_projection_for_action(
  const struct unit *actor, const struct unit *target,
  const struct tile *target_tile, enum agent_v2_action_kind kind,
  struct agent_v2_transport_projection *projection)
{
  memset(projection, 0, sizeof(*projection));
  projection->parent_changes = TRUE;
  switch (kind) {
  case AGENT_V2_ACTION_TRANSPORT_BOARD:
    projection->cargo = actor;
    projection->new_transporter = target;
    break;
  case AGENT_V2_ACTION_TRANSPORT_EMBARK:
    projection->cargo = actor;
    projection->new_transporter = target;
    projection->moved_root = actor;
    projection->moved_tile = target != NULL ? unit_tile(target) : NULL;
    break;
  case AGENT_V2_ACTION_TRANSPORT_LOAD:
    projection->cargo = target;
    projection->new_transporter = actor;
    break;
  case AGENT_V2_ACTION_TRANSPORT_DEBOARD:
    projection->cargo = actor;
    break;
  case AGENT_V2_ACTION_TRANSPORT_DISEMBARK:
    projection->cargo = actor;
    projection->moved_root = actor;
    projection->moved_tile = target_tile;
    break;
  case AGENT_V2_ACTION_TRANSPORT_UNLOAD:
    projection->cargo = target;
    break;
  default:
    return FALSE;
  }
  return projection->cargo != NULL
         && (projection->moved_root == NULL
             || projection->moved_tile != NULL);
}

static bool v2_transport_action_still_legal(
  const struct player *self, const struct unit *actor,
  const struct agent_v2_action *action, struct unit **target_result)
{
  const struct action *native = action_by_number(action->action);
  struct unit *target = NULL;
  struct unit *context = NULL;
  struct tile *target_tile = NULL;
  enum action_result expected_result;
  enum action_target_kind expected_target;
  enum agent_v2_probability_kind probability_kind;
  struct act_prob probability;
  struct agent_v2_transport_projection projection;
  uint64_t before_signature;
  uint64_t after_signature;
  int probability_min;
  int probability_max;

  if (self == NULL || actor == NULL || unit_owner(actor) != self
      || native == NULL || unit_tile(actor) == NULL
      || actor->client.lifecycle_id != action->unit_lifecycle_id
      || v2_existing_incarnation(AGENT_V2_ENTITY_UNIT, actor->id)
         != action->unit_incarnation
      || action_get_actor_kind(native) != AAK_UNIT
      || native->actor_consuming_always) {
    return FALSE;
  }
  switch (action->kind) {
  case AGENT_V2_ACTION_TRANSPORT_BOARD:
    expected_result = ACTRES_TRANSPORT_BOARD;
    expected_target = ATK_UNIT;
    break;
  case AGENT_V2_ACTION_TRANSPORT_DEBOARD:
    expected_result = ACTRES_TRANSPORT_DEBOARD;
    expected_target = ATK_UNIT;
    break;
  case AGENT_V2_ACTION_TRANSPORT_EMBARK:
    expected_result = ACTRES_TRANSPORT_EMBARK;
    expected_target = ATK_UNIT;
    break;
  case AGENT_V2_ACTION_TRANSPORT_DISEMBARK:
    expected_result = ACTRES_TRANSPORT_DISEMBARK;
    expected_target = ATK_TILE;
    break;
  case AGENT_V2_ACTION_TRANSPORT_LOAD:
    expected_result = ACTRES_TRANSPORT_LOAD;
    expected_target = ATK_UNIT;
    break;
  case AGENT_V2_ACTION_TRANSPORT_UNLOAD:
    expected_result = ACTRES_TRANSPORT_UNLOAD;
    expected_target = ATK_UNIT;
    break;
  default:
    return FALSE;
  }
  if (native->result != expected_result
      || action_get_target_kind(native) != expected_target) {
    return FALSE;
  }
  if (expected_target == ATK_UNIT) {
    if (action->target_tile != -1
        || !v2_action_visible_unit_matches(
          self, action->target_unit_id, action->target_unit_incarnation,
          action->target_unit_lifecycle_id, &target)) {
      return FALSE;
    }
    probability = action_prob_vs_unit(
      &wld.map, actor, native->id, target);
  } else {
    if (action->target_unit_id != -1
        || !v2_tile_index_valid(action->target_tile)
        || (target_tile = index_to_tile(&wld.map, action->target_tile)) == NULL
        || !is_tiles_adjacent(unit_tile(actor), target_tile)) {
      return FALSE;
    }
    probability = action_prob_vs_tile(
      &wld.map, actor, native->id, target_tile, NULL);
  }
  if (action->transport_context_id >= 0
      && !v2_action_visible_unit_matches(
        self, action->transport_context_id,
        action->transport_context_incarnation,
        action->transport_context_lifecycle_id, &context)) {
    return FALSE;
  }
  switch (action->kind) {
  case AGENT_V2_ACTION_TRANSPORT_BOARD:
  case AGENT_V2_ACTION_TRANSPORT_LOAD:
    if (context != unit_transport_get(
                     action->kind == AGENT_V2_ACTION_TRANSPORT_LOAD
                     ? target : actor)
        || !same_pos(unit_tile(actor), unit_tile(target))
        || !v2_transport_load_pair(
          self,
          action->kind == AGENT_V2_ACTION_TRANSPORT_LOAD ? target : actor,
          action->kind == AGENT_V2_ACTION_TRANSPORT_LOAD ? actor : target)) {
      return FALSE;
    }
    break;
  case AGENT_V2_ACTION_TRANSPORT_EMBARK:
    if (context != unit_transport_get(actor)
        || client_tile_get_known(unit_tile(target)) != TILE_KNOWN_SEEN
        || !is_tiles_adjacent(unit_tile(actor), unit_tile(target))
        || !v2_transport_load_pair(self, actor, target)) {
      return FALSE;
    }
    break;
  case AGENT_V2_ACTION_TRANSPORT_DEBOARD:
    if (context != target
        || !v2_transport_linked_pair(self, actor, target)) {
      return FALSE;
    }
    break;
  case AGENT_V2_ACTION_TRANSPORT_UNLOAD:
    if (context != actor
        || !v2_transport_linked_pair(self, target, actor)) {
      return FALSE;
    }
    break;
  case AGENT_V2_ACTION_TRANSPORT_DISEMBARK:
    if (target != NULL || context == NULL
        || !v2_transport_linked_pair(self, actor, context)) {
      return FALSE;
    }
    break;
  default:
    return FALSE;
  }
  if (!v2_transport_projection_for_action(
        actor, target, target_tile, action->kind, &projection)
      || !v2_transport_component_signature(
           self, actor, target, context, NULL, &before_signature)
      || before_signature != action->transport_before_signature
      || !v2_transport_component_signature(
           self, actor, target, context, &projection, &after_signature)
      || after_signature != action->transport_after_signature) {
    return FALSE;
  }
  if (!v2_probability_is_certain(probability, &probability_kind,
                                 &probability_min, &probability_max)
      || probability_kind != action->probability_kind
      || probability_min != action->probability_min
      || probability_max != action->probability_max) {
    return FALSE;
  }
  if (target_result != NULL) {
    *target_result = target;
  }
  return TRUE;
}

static bool v2_government_action_still_legal(
  struct player *self, const struct agent_v2_action *action,
  struct government **target)
{
  struct government *candidate;

  if (self == NULL || action == NULL || target == NULL
      || action->player_id != player_number(self)
      || action->player_incarnation
         != v2_existing_incarnation(AGENT_V2_ENTITY_PLAYER,
                                    player_number(self))
      || (candidate = government_by_number(action->target_government))
         == NULL) {
    return FALSE;
  }
  if (action->kind == AGENT_V2_ACTION_GOVERNMENT_REVOLUTION) {
    if (candidate != game.government_during_revolution
        || !v2_government_revolution_available(self)) {
      return FALSE;
    }
  } else if (action->kind == AGENT_V2_ACTION_GOVERNMENT_CHANGE) {
    if (!v2_government_change_available(self, candidate)) {
      return FALSE;
    }
  } else {
    return FALSE;
  }
  *target = candidate;
  return TRUE;
}

static void v2_handle_open(char **fields)
{
  struct agent_v2_snapshot *snapshot;

  if (!v2_pin_snapshot(&snapshot)) {
    v2_error(fields[1], "OBS_TOO_LARGE",
             "client cache exceeds bounded snapshot capacity");
    return;
  }
  v2_sendf("OBS_OPENED\t%s\t%s\t%llu\t%zu",
           fields[1], snapshot->id,
           (unsigned long long) snapshot->revision, snapshot->row_count);
}

static void v2_handle_page(char **fields)
{
  struct agent_v2_snapshot *snapshot;
  size_t offset;
  size_t limit;
  size_t end;
  size_t i;

  (void) v2_parse_size(fields[3], &offset);
  (void) v2_parse_size(fields[4], &limit);
  snapshot = v2_snapshot_by_id(fields[2]);
  if (snapshot == NULL) {
    v2_error(fields[1], "SNAPSHOT_GONE", "snapshot is not pinned");
    return;
  }
  if (offset > snapshot->row_count) {
    v2_error(fields[1], "BAD_OFFSET", "offset exceeds snapshot rows");
    return;
  }
  end = offset + limit;
  if (end < offset || end > snapshot->row_count) {
    end = snapshot->row_count;
  }
  v2_sendf("PAGE_BEGIN\t%s\t%s\t%llu\t%zu\t%zu\t%zu",
           fields[1], snapshot->id,
           (unsigned long long) snapshot->revision,
           offset, end - offset, snapshot->row_count);
  for (i = offset; i < end; i++) {
    char encoded[AGENT_V2_ROW_MAX * 3 + 1];

    if (!fc_agent_v2_percent_encode(snapshot->rows[i].text,
                                    encoded, sizeof(encoded))) {
      v2_error(fields[1], "ENCODE_FAILED", "snapshot row encoding failed");
      return;
    }
    v2_sendf("ROW\t%s\t%s\t%zu\t%s",
             fields[1], snapshot->id, i, encoded);
  }
  v2_sendf("PAGE_END\t%s\t%s\t%zu",
           fields[1], snapshot->id, end);
}

static int v2_special_action_target_id(const struct agent_v2_action *action)
{
  const struct action *native = action_by_number(action->action);

  if (native == NULL) {
    return -1;
  }
  switch (action_get_target_kind(native)) {
  case ATK_CITY:
    return action->destination_city_id;
  case ATK_UNIT:
    return action->target_unit_id;
  case ATK_STACK:
  case ATK_TILE:
  case ATK_EXTRAS:
    return action->target_tile;
  case ATK_SELF:
    return action->unit_id;
  case ATK_COUNT:
    break;
  }
  return -1;
}

static int v2_target_scope_find(const char *actor_ref, uint64_t revision,
                                int target_tile)
{
  size_t i;

  for (i = 0; i < AGENT_V2_SCOPE_PINNED; i++) {
    if (v2_target_scopes[i].valid
        && v2_target_scopes[i].revision == revision
        && v2_target_scopes[i].target_tile == target_tile
        && strcmp(v2_target_scopes[i].actor_ref, actor_ref) == 0) {
      return (int) i;
    }
  }
  return -1;
}

static int v2_target_scope_free(void)
{
  size_t i;

  for (i = 0; i < AGENT_V2_SCOPE_PINNED; i++) {
    if (!v2_target_scopes[i].valid) {
      return (int) i;
    }
  }
  return -1;
}

static void v2_target_query_prepare_emit(void)
{
  size_t i;

  if (!v2_target_query.active || v2_target_query.scope_index < 0) {
    return;
  }
  qsort(v2_target_query.actions, v2_target_query.action_count,
        sizeof(v2_target_query.actions[0]), v2_action_compare);
  for (i = 0; i < v2_target_query.action_count; i++) {
    if (!v2_assign_target_slot(&v2_target_query.actions[i],
                               v2_target_query.target_tile)) {
      v2_error(v2_target_query.request, "ENCODE_FAILED",
               "target action slot could not be encoded");
      v2_target_query_clear();
      return;
    }
  }
  if (!fc_agent_v2_percent_encode(
        v2_target_query.actor_ref, v2_target_query.encoded_actor,
        sizeof(v2_target_query.encoded_actor))) {
    v2_error(v2_target_query.request, "ENCODE_FAILED",
             "target actor encoding failed");
    v2_target_query_clear();
    return;
  }
  if (v2_target_query.timer != NULL) {
    timer_destroy(v2_target_query.timer);
    v2_target_query.timer = NULL;
  }
  v2_target_query.emitting = TRUE;
  v2_target_query.stream_started = FALSE;
  v2_target_query.emit_index = 0;
}

static void v2_target_query_request_next_detail(void)
{
  int before_request;

  while (v2_target_query.detail_action_index
         < v2_target_query.action_count) {
    struct agent_v2_action *action =
      &v2_target_query.actions[v2_target_query.detail_action_index];
    const struct action *native = action_by_number(action->action);

    if (!v2_targeted_sabotage_action(native)
        || action->target_build_kind == VUT_IMPROVEMENT) {
      v2_target_query.detail_action_index++;
      continue;
    }
    if (action->target_build_kind != VUT_NONE
        || action->target_build_id != -1
        || action->destination_city_id < 0
        || v2_target_query.detail_query_pending
        || v2_special_revalidation.detail_query_pending) {
      v2_target_query_desynchronize(
        "targeted sabotage detail query has an invalid binding");
      return;
    }
    before_request = client.conn.client.last_request_id_used;
    v2_target_query.detail_request_id = dsend_packet_unit_action_query(
      &client.conn, action->unit_id, action->destination_city_id,
      action->action, AGENT_V2_ACTION_QUERY_KIND);
    if (v2_target_query.detail_request_id
          != get_next_request_id(before_request)
        || client.conn.client.last_request_id_used
           != v2_target_query.detail_request_id) {
      v2_error(v2_target_query.request, "NOT_SENT",
               "targeted sabotage building query was not sent");
      v2_target_query_clear();
      return;
    }
    v2_target_query.detail_query_pending = TRUE;
    return;
  }
  v2_target_query.cost_action_index = 0;
  v2_target_query_request_next_cost();
}

static void v2_target_query_request_next_cost(void)
{
  int before_request;
  int target_id;

  while (v2_target_query.cost_action_index
         < v2_target_query.action_count) {
    struct agent_v2_action *action =
      &v2_target_query.actions[v2_target_query.cost_action_index];
    const struct action *native = action_by_number(action->action);

    if (!v2_paid_special_action(native)) {
      v2_target_query.cost_action_index++;
      continue;
    }
    target_id = v2_special_action_target_id(action);
    if (target_id < 0) {
      v2_target_query_desynchronize(
        "paid target action has no exact native target");
      return;
    }
    before_request = client.conn.client.last_request_id_used;
    v2_target_query.cost_request_id = dsend_packet_unit_action_query(
      &client.conn, action->unit_id, target_id, action->action,
      AGENT_V2_ACTION_QUERY_KIND);
    if (v2_target_query.cost_request_id != get_next_request_id(before_request)
        || client.conn.client.last_request_id_used
           != v2_target_query.cost_request_id) {
      v2_error(v2_target_query.request, "NOT_SENT",
               "paid target price query was not sent");
      v2_target_query_clear();
      return;
    }
    v2_target_query.cost_query_pending = TRUE;
    return;
  }
  v2_target_query_prepare_emit();
}

/* Attempt at most one target-catalog frame per event-loop tick. This reduces
 * queue pressure but does not guarantee free capacity because ticks can
 * outrun socket drainage. Every enqueue is checked; refusal terminally
 * poisons the stream and commits no native scope. */
static void v2_target_query_emit_one(void)
{
  struct agent_v2_target_scope *scope;

  if (!v2_target_query.active || !v2_target_query.emitting) {
    return;
  }
  if (v2_target_query.revision != v2_revision) {
    v2_error(v2_target_query.request, "STALE_REVISION",
             "state changed while streaming target actions");
    v2_target_query_clear();
    return;
  }
  if (!v2_target_query.stream_started) {
    if (!v2_sendf("TARGET_BEGIN\t%s\t%llu\t%s\t%d\t%zu",
                  v2_target_query.request,
                  (unsigned long long) v2_target_query.revision,
                  v2_target_query.encoded_actor,
                  v2_target_query.target_tile,
                  v2_target_query.action_count)) {
      v2_target_query_desynchronize(
        "IPC rejected target catalog begin frame");
      return;
    }
    v2_target_query.stream_started = TRUE;
    return;
  }
  if (v2_target_query.emit_index < v2_target_query.action_count) {
    struct agent_v2_row row;
    char encoded_row[AGENT_V2_ROW_MAX * 3 + 1];
    size_t index = v2_target_query.emit_index;

    if (!v2_format_action_row(&v2_target_query.actions[index], &row)
        || !fc_agent_v2_percent_encode(row.text, encoded_row,
                                       sizeof(encoded_row))) {
      v2_error(v2_target_query.request, "ENCODE_FAILED",
               "target row encoding failed");
      v2_target_query_clear();
      return;
    }
    if (!v2_sendf("TARGET_ROW\t%s\t%zu\t%s",
                  v2_target_query.request, index, encoded_row)) {
      v2_target_query_desynchronize(
        "IPC rejected target catalog row frame");
      return;
    }
    v2_target_query.emit_index++;
    return;
  }
  if (!v2_sendf("TARGET_END\t%s\t%zu", v2_target_query.request,
                v2_target_query.action_count)) {
    v2_target_query_desynchronize("IPC rejected target catalog end frame");
    return;
  }
  scope = &v2_target_scopes[v2_target_query.scope_index];
  memset(scope, 0, sizeof(*scope));
  fc_strlcpy(scope->actor_ref, v2_target_query.actor_ref,
             sizeof(scope->actor_ref));
  scope->revision = v2_target_query.revision;
  scope->target_tile = v2_target_query.target_tile;
  scope->action_count = v2_target_query.action_count;
  memcpy(scope->actions, v2_target_query.actions,
         scope->action_count * sizeof(scope->actions[0]));
  scope->valid = TRUE;
  v2_target_query_clear();
}

enum agent_v2_target_build_result {
  AGENT_V2_TARGET_BUILD_INVALID_ACTOR,
  AGENT_V2_TARGET_BUILD_EMPTY,
  AGENT_V2_TARGET_BUILD_ONE
};

static enum agent_v2_target_build_result v2_build_target_action(
  const char *actor_ref, int native_tile, struct agent_v2_action *action)
{
  enum agent_v2_entity_kind kind;
  int actor_id;
  uint64_t incarnation;
  struct fc_agent_v2_phase_evidence phase;
  struct unit *punit;
  struct city *pcity;
  struct tile *target;
  struct client_goto_pathfinder *finder;
  struct client_rally_plan *rally_plan;
  bool built;

  if (!v2_resolve_owned_actor(actor_ref, &kind, &actor_id, &incarnation)
      || (kind != AGENT_V2_ENTITY_PLAYER
          && kind != AGENT_V2_ENTITY_UNIT
          && kind != AGENT_V2_ENTITY_CITY)) {
    return AGENT_V2_TARGET_BUILD_INVALID_ACTOR;
  }
  if (!v2_tile_index_valid(native_tile)
      || !v2_collect_phase_evidence(client_player(), &phase)
      || !v2_actions_ready(&phase)) {
    return AGENT_V2_TARGET_BUILD_EMPTY;
  }
  target = index_to_tile(&wld.map, native_tile);
  if (target == NULL) {
    return AGENT_V2_TARGET_BUILD_EMPTY;
  }
  if (kind == AGENT_V2_ENTITY_PLAYER) {
    if (actor_id != player_number(client_player())
        || !v2_build_player_infrastructure_target_action(
             client_player(), target, action)
        || !v2_assign_target_slot(action, native_tile)) {
      return AGENT_V2_TARGET_BUILD_EMPTY;
    }
    return AGENT_V2_TARGET_BUILD_ONE;
  }
  if (kind == AGENT_V2_ENTITY_CITY) {
    pcity = player_city_by_number(client_player(), actor_id);
    rally_plan = client_rally_plan_new(pcity, target);
    if (rally_plan == NULL) {
      return AGENT_V2_TARGET_BUILD_EMPTY;
    }
    built = v2_build_city_rally_target_action(
      pcity, target, rally_plan, action);
    client_rally_plan_destroy(rally_plan);
    if (!built || !v2_assign_target_slot(action, native_tile)) {
      return AGENT_V2_TARGET_BUILD_EMPTY;
    }
    return AGENT_V2_TARGET_BUILD_ONE;
  }
  punit = player_unit_by_number(client_player(), actor_id);
  if (punit == NULL) {
    return AGENT_V2_TARGET_BUILD_EMPTY;
  }
  finder = client_goto_pathfinder_new(punit);
  if (finder == NULL) {
    return AGENT_V2_TARGET_BUILD_EMPTY;
  }
  built = v2_build_unit_goto_target_action(
    punit, target, finder, action);
  client_goto_pathfinder_destroy(finder);
  if (!built || !v2_assign_target_slot(action, native_tile)) {
    return AGENT_V2_TARGET_BUILD_EMPTY;
  }
  return AGENT_V2_TARGET_BUILD_ONE;
}

static bool v2_special_action_still_bound(
  const struct player *self, const struct unit *actor,
  const struct agent_v2_action *action, int *target_id, int *subtarget_id)
{
  const struct action *native = action_by_number(action->action);
  struct unit *target_unit = NULL;
  struct city *target_city = NULL;
  struct tile *target_tile = index_to_tile(&wld.map, action->target_tile);
  struct player *target_extra_owner;
  struct act_prob current_probability;
  enum action_sub_target_kind subtarget;

  if (self == NULL || actor == NULL || native == NULL || target_tile == NULL
      || action->kind != AGENT_V2_ACTION_UNIT_SPECIAL
      || unit_owner(actor) != self || actor->id != action->unit_id
      || actor->client.lifecycle_id == 0
      || actor->client.lifecycle_id != action->unit_lifecycle_id
      || v2_existing_incarnation(AGENT_V2_ENTITY_UNIT, actor->id)
         != action->unit_incarnation
      || action_get_actor_kind(native) != AAK_UNIT
      || !v2_special_action_shape_supported(native)
      || ((action->probability_kind
           == AGENT_V2_PROBABILITY_NOT_IMPLEMENTED)
          != v2_special_not_implemented_allowed(native))
      || (v2_paid_special_action(native)
          ? action->gold_cost < 0 : action->gold_cost != -1)) {
    return FALSE;
  }
  if (native->id == ACTION_PARADROP_ENTER_CONQUER) {
    struct city *lease_city = tile_city(target_tile);
    bv_extras current_huts = v2_hut_extras_on_tile(target_tile);

    target_extra_owner = extra_owner(target_tile);
    if (!action->special_target_known_seen
        || client_tile_get_known(target_tile) != TILE_KNOWN_SEEN
        || unit_tile(actor) == NULL
        || tile_index(unit_tile(actor)) != action->source_unit_tile
        || action->source_unit_tile == action->target_tile
        || action->source_unit_moves <= 0
        || actor->moves_left != action->source_unit_moves
        || action->source_unit_paradropped || actor->paradropped
        || !BV_ARE_EQUAL(action->special_target_extras,
                         target_tile->extras)
        || !BV_ARE_EQUAL(action->special_target_hut_extras,
                         current_huts)
        || (action->special_target_extra_owner >= 0
            ? (target_extra_owner == NULL
               || player_number(target_extra_owner)
                  != action->special_target_extra_owner
               || target_extra_owner == self)
            : target_extra_owner != NULL)
        || (action->special_target_city_id >= 0
            ? (lease_city == NULL
               || lease_city->id != action->special_target_city_id
               || lease_city->client.lifecycle_id == 0
               || lease_city->client.lifecycle_id
                  != action->special_target_city_lifecycle_id
               || v2_existing_incarnation(
                    AGENT_V2_ENTITY_CITY, lease_city->id)
                  != action->special_target_city_incarnation
               || player_number(city_owner(lease_city))
                  != action->special_target_city_owner
               || city_owner(lease_city) == self)
            : (lease_city != NULL
               || action->special_target_city_incarnation != 0
               || action->special_target_city_lifecycle_id != 0
               || action->special_target_city_owner != -1))) {
      return FALSE;
    }
  }
  *target_id = v2_special_action_target_id(action);
  if (*target_id < 0) {
    return FALSE;
  }
  if (!fc_agent_v2_nuke_stack_binding_matches(
        native->id == ACTION_NUKE_UNITS
        || native->id == ACTION_SPY_BRIBE_STACK,
        action->target_stack_signature,
        native->id == ACTION_SPY_BRIBE_STACK
        ? v2_visible_bribe_stack_signature(self, action->target_tile)
        : v2_visible_stack_signature(self, action->target_tile))) {
    return FALSE;
  }
  switch (action_get_target_kind(native)) {
  case ATK_CITY:
    target_city = game_city_by_number(action->destination_city_id);
    if (target_city == NULL || target_city->client.lifecycle_id == 0
        || target_city->client.lifecycle_id
           != action->destination_city_lifecycle_id
        || v2_existing_incarnation(AGENT_V2_ENTITY_CITY, target_city->id)
           != action->destination_city_incarnation
        || city_tile(target_city) != target_tile) {
      return FALSE;
    }
    current_probability = action_prob_vs_city(
      &wld.map, actor, native->id, target_city);
    break;
  case ATK_UNIT:
    target_unit = game_unit_by_number(action->target_unit_id);
    if (target_unit == NULL || target_unit->client.lifecycle_id == 0
        || target_unit->client.lifecycle_id
           != action->target_unit_lifecycle_id
        || v2_existing_incarnation(AGENT_V2_ENTITY_UNIT, target_unit->id)
           != action->target_unit_incarnation
        || unit_tile(target_unit) != target_tile) {
      return FALSE;
    }
    current_probability = action_prob_vs_unit(
      &wld.map, actor, native->id, target_unit);
    break;
  case ATK_STACK:
    current_probability = action_prob_vs_stack(
      &wld.map, actor, native->id, target_tile);
    break;
  case ATK_TILE:
    current_probability = action_prob_vs_tile(
      &wld.map, actor, native->id, target_tile, NULL);
    break;
  case ATK_EXTRAS:
    if (action->target_extra != EXTRA_NONE) {
      return FALSE;
    }
    current_probability = action_prob_vs_extras(
      &wld.map, actor, native->id, target_tile, NULL);
    break;
  case ATK_SELF:
    if (unit_tile(actor) != target_tile) {
      return FALSE;
    }
    current_probability = action_prob_self(&wld.map, actor, native->id);
    break;
  case ATK_COUNT:
    return FALSE;
  }
  subtarget = action_get_sub_target_kind(native);
  if (action->target_extra != EXTRA_NONE) {
    return FALSE;
  }
  if (subtarget == ASTK_TECH) {
    if (native->result != ACTRES_SPY_TARGETED_STEAL_TECH
        || target_city == NULL
        || action->target_build_kind != VUT_NONE
        || action->target_build_id != -1
        || action->target_building_catalog_request_id != 0
        || action->target_building_catalog_revision != 0
        || action->target_building_catalog_digest != 0
        || !v2_targeted_tech_choice_current(
             self, target_city, action->target_tech,
             action->target_research_digest)) {
      return FALSE;
    }
    *subtarget_id = action->target_tech;
  } else if (subtarget == ASTK_BUILDING) {
    const struct impr_type *improvement =
      action->target_build_kind == VUT_IMPROVEMENT
      ? improvement_by_number(action->target_build_id) : NULL;

    if (!v2_targeted_sabotage_action(native)
        || target_city == NULL || improvement == NULL
        || improvement->sabotage <= 0
        || action->target_tech != -1
        || action->target_research_digest != 0
        || action->target_building_catalog_request_id <= 0
        || action->target_building_catalog_revision != v2_revision
        || action->target_building_catalog_digest == 0) {
      return FALSE;
    }
    /* Presence is re-queried from the authoritative normal-GUI detail list
     * after the action-probability preflight. Do not trust or require a
     * hidden foreign-building cache here. */
    *subtarget_id = action->target_build_id;
  } else if (subtarget == ASTK_NONE) {
    if (action->target_tech != -1 || action->target_research_digest != 0
        || action->target_build_kind != VUT_NONE
        || action->target_build_id != -1
        || action->target_building_catalog_request_id != 0
        || action->target_building_catalog_revision != 0
        || action->target_building_catalog_digest != 0) {
      return FALSE;
    }
    *subtarget_id = NO_TARGET;
  } else {
    return FALSE;
  }
  return v2_special_probability_supported(native, current_probability)
         && v2_action_probability_matches(current_probability, action);
}

static bool v2_special_revalidation_reply(
  const struct packet_unit_actions *packet)
{
  const struct agent_v2_action *action = &v2_special_revalidation.action;
  const struct action *native;
  struct act_prob probability;

  if (v2_special_revalidation_desynchronized) {
    return TRUE;
  }
  if (!v2_special_revalidation.active) {
    v2_special_revalidation_desynchronized = TRUE;
    return TRUE;
  }
  if (packet->actor_unit_id != action->unit_id
      || packet->target_tile_id != action->target_tile) {
    v2_special_revalidation_desynchronize(
      "unit actions reply did not match the pending action preflight");
    return TRUE;
  }
  if (v2_special_revalidation.revision != v2_revision) {
    v2_error(v2_special_revalidation.request, "STALE_REVISION",
             "state changed during action preflight");
    v2_special_revalidation_clear();
    return TRUE;
  }
  native = action_by_number(action->action);
  if (native == NULL || action->kind != AGENT_V2_ACTION_UNIT_SPECIAL
      || action_get_actor_kind(native) != AAK_UNIT
      || !v2_special_action_shape_supported(native)
      || (v2_paid_special_action(native)
          ? action->gold_cost < 0 : action->gold_cost != -1)
      || action->target_extra != EXTRA_NONE
      || ((action->probability_kind
           == AGENT_V2_PROBABILITY_NOT_IMPLEMENTED)
          != v2_special_not_implemented_allowed(native))) {
    v2_error(v2_special_revalidation.request, "STALE_SLOT",
             "server-discovered unit capability is no longer bound");
    v2_special_revalidation_clear();
    return TRUE;
  }
  switch (action_get_target_kind(native)) {
  case ATK_CITY:
    if (packet->target_city_id != action->destination_city_id) {
      v2_error(v2_special_revalidation.request, "STALE_SLOT",
               "server preflight resolved a different city target");
      v2_special_revalidation_clear();
      return TRUE;
    }
    break;
  case ATK_UNIT:
    if (packet->target_unit_id != action->target_unit_id) {
      v2_error(v2_special_revalidation.request, "STALE_SLOT",
               "server preflight resolved a different unit target");
      v2_special_revalidation_clear();
      return TRUE;
    }
    break;
  case ATK_STACK:
  case ATK_TILE:
    break;
  case ATK_EXTRAS:
    if (packet->target_extra_id != EXTRA_NONE) {
      v2_error(v2_special_revalidation.request, "STALE_SLOT",
               "server preflight resolved a selected extra");
      v2_special_revalidation_clear();
      return TRUE;
    }
    break;
  case ATK_SELF:
  case ATK_COUNT:
    v2_error(v2_special_revalidation.request, "STALE_SLOT",
             "server preflight target kind is unsupported");
    v2_special_revalidation_clear();
    return TRUE;
  }
  probability = packet->action_probabilities[action->action];
  if (!action_prob_possible(probability)
      || !v2_special_probability_supported(native, probability)
      || !v2_action_probability_matches(probability, action)) {
    v2_error(v2_special_revalidation.request, "STALE_SLOT",
             "server preflight no longer matches the frozen capability");
    v2_special_revalidation_clear();
    return TRUE;
  }
  if (v2_targeted_sabotage_action(native)) {
    int before_request = client.conn.client.last_request_id_used;

    if (v2_target_query.detail_query_pending
        || v2_special_revalidation.detail_query_pending) {
      v2_error(v2_special_revalidation.request, "BUSY",
               "one sabotage detail query is already pending");
      v2_special_revalidation_clear();
      return TRUE;
    }
    v2_special_revalidation.detail_request_id =
      dsend_packet_unit_action_query(
        &client.conn, action->unit_id, action->destination_city_id,
        action->action, AGENT_V2_ACTION_QUERY_KIND);
    if (v2_special_revalidation.detail_request_id
          != get_next_request_id(before_request)
        || client.conn.client.last_request_id_used
           != v2_special_revalidation.detail_request_id
        || v2_special_revalidation.detail_request_id
           == action->target_building_catalog_request_id) {
      v2_error(v2_special_revalidation.request, "NOT_SENT",
               "targeted sabotage preflight query was not sent exactly once");
      v2_special_revalidation_clear();
      return TRUE;
    }
    v2_special_revalidation.detail_query_pending = TRUE;
    return TRUE;
  }
  if (v2_paid_special_action(native)) {
    int before_request = client.conn.client.last_request_id_used;
    int target_id = v2_special_action_target_id(action);

    v2_special_revalidation.cost_request_id =
      dsend_packet_unit_action_query(
        &client.conn, action->unit_id, target_id, action->action,
        AGENT_V2_ACTION_QUERY_KIND);
    if (v2_special_revalidation.cost_request_id
          != get_next_request_id(before_request)
        || client.conn.client.last_request_id_used
           != v2_special_revalidation.cost_request_id) {
      v2_error(v2_special_revalidation.request, "NOT_SENT",
               "paid action price revalidation was not sent");
      v2_special_revalidation_clear();
      return TRUE;
    }
    v2_special_revalidation.cost_query_pending = TRUE;
    return TRUE;
  }
  if (v2_special_revalidation.timer != NULL) {
    timer_destroy(v2_special_revalidation.timer);
    v2_special_revalidation.timer = NULL;
  }
  v2_special_revalidation.ready = TRUE;
  return TRUE;
}

static bool v2_unit_actions_observer(
  const struct packet_unit_actions *packet, void *data)
{
  struct unit *actor;
  struct unit *target_unit;
  struct city *target_city;
  struct tile *target_tile;
  struct act_prob not_implemented = action_prob_new_not_impl();
  struct agent_v2_action_buffer buffer;

  (void) data;
  if (packet->request_kind == AGENT_V2_ACTION_REVALIDATE_KIND) {
    return v2_special_revalidation_reply(packet);
  }
  if (packet->request_kind != AGENT_V2_ACTION_QUERY_KIND) {
    return FALSE;
  }
  if (v2_target_query_desynchronized) {
    return TRUE;
  }
  if (!v2_target_query.active
      || packet->actor_unit_id != v2_target_query.actor_id
      || packet->target_tile_id != v2_target_query.target_tile) {
    if (v2_target_query.active) {
      v2_target_query_desynchronize(
        "unit actions reply did not match the pending target query");
    }
    return TRUE;
  }
  if (v2_target_query.revision != v2_revision) {
    v2_error(v2_target_query.request, "STALE_REVISION",
             "state changed during target action discovery");
    v2_target_query_clear();
    return TRUE;
  }
  actor = player_unit_by_number(client_player(), packet->actor_unit_id);
  target_unit = game_unit_by_number(packet->target_unit_id);
  target_city = game_city_by_number(packet->target_city_id);
  target_tile = index_to_tile(&wld.map, packet->target_tile_id);
  if (actor == NULL || target_tile == NULL) {
    v2_error(v2_target_query.request, "STALE_REVISION",
             "target query actor or tile is no longer current");
    v2_target_query_clear();
    return TRUE;
  }
  buffer.actions = v2_target_query.actions;
  buffer.count = v2_target_query.action_count;
  buffer.capacity = FC_AGENT_V2_MAX_TARGET_ACTIONS;
  buffer.overflow = FALSE;
  buffer.export_unknown_rows = FALSE;
  buffer.unknown_exported = NULL;
  action_noninternal_iterate(act) {
    const struct action *paction = action_by_number(act);
    struct act_prob probability = packet->action_probabilities[act];

    if (paction == NULL || action_get_actor_kind(paction) != AAK_UNIT) {
      continue;
    }
    (void) v2_add_goto_and_perform_action(
      &buffer, actor, target_unit, target_city, target_tile, paction);
    if (are_action_probabilitys_equal(&probability, &not_implemented)
        && !v2_special_not_implemented_allowed(paction)) {
      continue;
    }
    (void) v2_add_server_special_action(
      &buffer, actor, target_unit, target_city, target_tile, paction,
      probability);
  } action_noninternal_iterate_end;
  extra_type_by_cause_iterate(EC_ROAD, extra) {
    (void) v2_add_connect_route_action(
      &buffer, actor, target_tile, ACTIVITY_GEN_ROAD, extra);
  } extra_type_by_cause_iterate_end;
  extra_type_by_cause_iterate(EC_IRRIGATION, extra) {
    (void) v2_add_connect_route_action(
      &buffer, actor, target_tile, ACTIVITY_IRRIGATE, extra);
  } extra_type_by_cause_iterate_end;
  if (buffer.overflow) {
    v2_error(v2_target_query.request, "SCOPE_TOO_LARGE",
             "target action scope exceeds bounded capacity");
    v2_target_query_clear();
    return TRUE;
  }
  v2_target_query.action_count = buffer.count;
  v2_target_query.detail_action_index = 0;
  v2_target_query_request_next_detail();
  return TRUE;
}

static void v2_unit_action_answer_observer(
  const struct packet_unit_action_answer *packet, int request_id, void *data)
{
  struct agent_v2_action *action = NULL;
  const struct action *native;
  int expected_target;

  (void) data;
  if (packet == NULL) {
    return;
  }
  if (packet->request_kind == AGENT_V2_ACTION_RECEIPT_KIND) {
    expected_target = v2_special_action_target_id(&v2_pending.action);
    if (fc_agent_v2_action_receipt_matches(
          v2_pending.active, v2_pending.processing_started,
          v2_pending.baseline_captured,
          v2_pending.seat_epoch == v2_seat_epoch
            && v2_exact_seat_epoch_current(),
          v2_pending.terminal == FC_AGENT_V2_TERMINAL_NONE,
          v2_pending.before_unit_present,
          v2_pending.before_special_target_exact,
          request_id, v2_pending.request_id, packet->request_kind,
          AGENT_V2_ACTION_RECEIPT_KIND,
          packet->actor_id, v2_pending.action.unit_id,
          packet->target_id, expected_target,
          packet->action_type, v2_pending.action.action,
          packet->cost)) {
      v2_pending.action_success_receipt_latched = TRUE;
    }
    return;
  }
  if (v2_target_query.active && v2_target_query.detail_query_pending
      && request_id == v2_target_query.detail_request_id) {
    size_t index = v2_target_query.detail_action_index;

    if (index >= v2_target_query.action_count) {
      v2_target_query_desynchronize(
        "targeted sabotage failure reply lost its pending action");
      return;
    }
    action = &v2_target_query.actions[index];
    if (packet->request_kind != AGENT_V2_ACTION_QUERY_KIND
        || packet->actor_id != action->unit_id
        || packet->target_id != action->destination_city_id
        || packet->action_type != ACTION_NONE) {
      v2_target_query_desynchronize(
        "targeted sabotage failure reply did not match its exact request");
      return;
    }
    memmove(action, action + 1,
            (v2_target_query.action_count - index - 1) * sizeof(*action));
    v2_target_query.action_count--;
    v2_target_query.detail_query_pending = FALSE;
    v2_target_query.detail_request_id = 0;
    v2_target_query_request_next_detail();
    return;
  }
  if (v2_special_revalidation.active
      && v2_special_revalidation.detail_query_pending
      && request_id == v2_special_revalidation.detail_request_id) {
    action = &v2_special_revalidation.action;
    if (packet->request_kind != AGENT_V2_ACTION_QUERY_KIND
        || packet->actor_id != action->unit_id
        || packet->target_id != action->destination_city_id
        || packet->action_type != ACTION_NONE) {
      v2_special_revalidation_desynchronize(
        "targeted sabotage failure reply did not match its exact preflight");
      return;
    }
    v2_error(v2_special_revalidation.request, "STALE_SLOT",
             "selected sabotage building is no longer available");
    v2_special_revalidation_clear();
    return;
  }
  if (v2_target_query.active && v2_target_query.cost_query_pending
      && request_id == v2_target_query.cost_request_id) {
    if (v2_target_query.revision != v2_revision
        || v2_target_query.cost_action_index
           >= v2_target_query.action_count) {
      v2_error(v2_target_query.request, "STALE_REVISION",
               "state changed during paid target price discovery");
      v2_target_query_clear();
      return;
    }
    action = &v2_target_query.actions[v2_target_query.cost_action_index];
  } else if (v2_special_revalidation.active
             && v2_special_revalidation.cost_query_pending
             && request_id == v2_special_revalidation.cost_request_id) {
    if (v2_special_revalidation.revision != v2_revision) {
      v2_error(v2_special_revalidation.request, "STALE_REVISION",
               "state changed during paid action price revalidation");
      v2_special_revalidation_clear();
      return;
    }
    action = &v2_special_revalidation.action;
  } else {
    return;
  }
  native = action_by_number(action->action);
  expected_target = v2_special_action_target_id(action);
  if (packet->request_kind != AGENT_V2_ACTION_QUERY_KIND
      || packet->actor_id != action->unit_id
      || packet->target_id != expected_target
      || (packet->action_type != ACTION_NONE
          && packet->action_type != action->action)) {
    if (action == &v2_special_revalidation.action) {
      v2_special_revalidation_desynchronize(
        "paid action price reply did not match its exact request");
    } else {
      v2_target_query_desynchronize(
        "paid target price reply did not match its exact request");
    }
    return;
  }
  if (action == &v2_special_revalidation.action) {
    v2_special_revalidation.cost_query_pending = FALSE;
    if (packet->action_type == ACTION_NONE
        || !v2_paid_quote_accepted(
             native, action->gold_cost, packet->cost)) {
      v2_error(v2_special_revalidation.request, "STALE_SLOT",
               "paid action quote is unavailable or exceeds its frozen maximum");
      v2_special_revalidation_clear();
      return;
    }
    if (v2_special_revalidation.timer != NULL) {
      timer_destroy(v2_special_revalidation.timer);
      v2_special_revalidation.timer = NULL;
    }
    v2_special_revalidation.ready = TRUE;
    return;
  }
  v2_target_query.cost_query_pending = FALSE;
  if (packet->action_type == ACTION_NONE
      || !v2_paid_quote_accepted(native, INT_MAX, packet->cost)) {
    memmove(action, action + 1,
            (v2_target_query.action_count
             - v2_target_query.cost_action_index - 1) * sizeof(*action));
    v2_target_query.action_count--;
  } else {
    action->gold_cost = packet->cost;
    v2_target_query.cost_action_index++;
  }
  v2_target_query_request_next_cost();
}

static bool v2_sabotage_list_matches_action(
  const struct packet_city_sabotage_list *packet, int request_id,
  int expected_request_id, const struct agent_v2_action *action)
{
  const struct player *self = client_player();
  const struct action *native = action != NULL
                                ? action_by_number(action->action) : NULL;
  const struct unit *actor = self != NULL && action != NULL
                             ? player_unit_by_number(
                                 self, action->unit_id) : NULL;
  const struct city *city = action != NULL
                            ? game_city_by_number(
                                action->destination_city_id) : NULL;

  return packet != NULL && request_id > 0
         && request_id == expected_request_id
         && packet->request_kind == AGENT_V2_ACTION_QUERY_KIND
         && v2_targeted_sabotage_action(native)
         && packet->actor_id == action->unit_id
         && packet->city_id == action->destination_city_id
         && packet->act_id == action->action
         && actor != NULL && actor->client.lifecycle_id != 0
         && actor->client.lifecycle_id == action->unit_lifecycle_id
         && v2_existing_incarnation(AGENT_V2_ENTITY_UNIT, actor->id)
            == action->unit_incarnation
         && city != NULL && city->client.lifecycle_id != 0
         && city->client.lifecycle_id == action->destination_city_lifecycle_id
         && v2_existing_incarnation(AGENT_V2_ENTITY_CITY, city->id)
            == action->destination_city_incarnation
         && city_tile(city) != NULL
         && tile_index(city_tile(city)) == action->target_tile;
}

static bool v2_city_sabotage_list_observer(
  const struct packet_city_sabotage_list *packet, int request_id, void *data)
{
  (void) data;
  if (packet == NULL
      || packet->request_kind != AGENT_V2_ACTION_QUERY_KIND) {
    return FALSE;
  }

  /* Reserved v2 replies are consumed before the normal handler hydrates the
   * foreign city's hidden improvement cache. The exact GUI-authorized rows
   * below are the sole public projection of this transient list. */
  if (v2_target_query.active && v2_target_query.detail_query_pending) {
    struct agent_v2_action base;
    size_t index = v2_target_query.detail_action_index;
    size_t choice_count = 0;
    uint64_t digest;

    if (index >= v2_target_query.action_count
        || !v2_sabotage_list_matches_action(
             packet, request_id, v2_target_query.detail_request_id,
             &v2_target_query.actions[index])) {
      v2_target_query_desynchronize(
        "sabotage list reply did not match the pending target query");
      return TRUE;
    }
    if (v2_target_query.revision != v2_revision) {
      v2_error(v2_target_query.request, "STALE_REVISION",
               "state changed during targeted sabotage discovery");
      v2_target_query_clear();
      return TRUE;
    }
    base = v2_target_query.actions[index];
    improvement_iterate(pimprove) {
      if (BV_ISSET(packet->improvements, improvement_index(pimprove))
          && pimprove->sabotage > 0) {
        choice_count++;
      }
    } improvement_iterate_end;
    if (v2_target_query.action_count - 1 + choice_count
        > FC_AGENT_V2_MAX_TARGET_ACTIONS) {
      v2_error(v2_target_query.request, "SCOPE_TOO_LARGE",
               "targeted sabotage choices exceed bounded capacity");
      v2_target_query_clear();
      return TRUE;
    }
    memmove(&v2_target_query.actions[index],
            &v2_target_query.actions[index + 1],
            (v2_target_query.action_count - index - 1) * sizeof(base));
    v2_target_query.action_count--;
    digest = v2_sabotage_catalog_digest(packet);
    improvement_iterate(pimprove) {
      if (BV_ISSET(packet->improvements, improvement_index(pimprove))
          && pimprove->sabotage > 0) {
        struct agent_v2_action *choice =
          &v2_target_query.actions[v2_target_query.action_count++];

        *choice = base;
        choice->target_build_kind = VUT_IMPROVEMENT;
        choice->target_build_id = improvement_number(pimprove);
        choice->target_building_catalog_request_id = request_id;
        choice->target_building_catalog_revision = v2_target_query.revision;
        choice->target_building_catalog_digest = digest;
      }
    } improvement_iterate_end;
    v2_target_query.detail_query_pending = FALSE;
    v2_target_query.detail_request_id = 0;
    v2_target_query_request_next_detail();
    return TRUE;
  }

  if (v2_special_revalidation.active
      && v2_special_revalidation.detail_query_pending) {
    const struct agent_v2_action *action =
      &v2_special_revalidation.action;
    const struct impr_type *improvement =
      action->target_build_kind == VUT_IMPROVEMENT
      ? improvement_by_number(action->target_build_id) : NULL;

    if (!v2_sabotage_list_matches_action(
          packet, request_id, v2_special_revalidation.detail_request_id,
          action)) {
      v2_special_revalidation_desynchronize(
        "sabotage list reply did not match the pending action preflight");
      return TRUE;
    }
    if (v2_special_revalidation.revision != v2_revision) {
      v2_error(v2_special_revalidation.request, "STALE_REVISION",
               "state changed during targeted sabotage preflight");
      v2_special_revalidation_clear();
      return TRUE;
    }
    if (improvement == NULL || improvement->sabotage <= 0
        || action->target_building_catalog_request_id <= 0
        || action->target_building_catalog_revision
           != v2_special_revalidation.revision
        || action->target_building_catalog_digest
           != v2_sabotage_catalog_digest(packet)
        || !BV_ISSET(packet->improvements,
                     improvement_index(improvement))) {
      v2_error(v2_special_revalidation.request, "STALE_SLOT",
               "selected sabotage building is no longer in the exact catalog");
      v2_special_revalidation_clear();
      return TRUE;
    }
    v2_special_revalidation.detail_query_pending = FALSE;
    v2_special_revalidation.detail_request_id = 0;
    if (v2_special_revalidation.timer != NULL) {
      timer_destroy(v2_special_revalidation.timer);
      v2_special_revalidation.timer = NULL;
    }
    v2_special_revalidation.ready = TRUE;
    return TRUE;
  }

  /* A late reserved reply must never fall through into persistent city
   * cache state, even after its request has already timed out. */
  return TRUE;
}

static void v2_handle_target_action(char **fields)
{
  uint64_t expected_revision;
  size_t parsed_tile;
  int native_tile;
  struct agent_v2_action action;
  enum agent_v2_target_build_result result;
  enum agent_v2_entity_kind actor_kind;
  int actor_id;
  int existing_scope;
  int free_scope;
  uint64_t actor_incarnation;

  (void) v2_parse_revision(fields[2], &expected_revision);
  (void) v2_parse_size(fields[4], &parsed_tile);
  native_tile = (int) parsed_tile;
  if (!v2_refresh()) {
    v2_error(fields[1], "OBS_TOO_LARGE", "state registry is unavailable");
    return;
  }
  if (expected_revision != v2_revision) {
    v2_error(fields[1], "STALE_REVISION", "target revision is not current");
    return;
  }
  if (!v2_tile_index_valid(native_tile)) {
    v2_error(fields[1], "BAD_REQUEST", "target tile is not current");
    return;
  }
  if (v2_target_query.active || v2_pending.active) {
    v2_error(fields[1], "BUSY", "another control request is pending");
    return;
  }
  if (v2_target_query_desynchronized) {
    v2_error(fields[1], "STREAM_DESYNC",
             "target action discovery requires a fresh sidecar process");
    return;
  }
  if (!v2_resolve_owned_actor(fields[3], &actor_kind, &actor_id,
                              &actor_incarnation)
      || (actor_kind != AGENT_V2_ENTITY_PLAYER
          && actor_kind != AGENT_V2_ENTITY_UNIT
          && actor_kind != AGENT_V2_ENTITY_CITY)) {
    v2_error(fields[1], "INVALID_ACTOR",
             "target actor is not the current player or an owned unit/city");
    return;
  }
  existing_scope = v2_target_scope_find(
    fields[3], v2_revision, native_tile);
  free_scope = existing_scope >= 0 ? existing_scope : v2_target_scope_free();
  if (free_scope < 0) {
    v2_error(fields[1], "SCOPE_TOO_LARGE",
             "all target catalog leases are in use for this revision");
    return;
  }
  v2_target_query_clear();
  v2_target_query.active = TRUE;
  v2_target_query.scope_index = free_scope;
  fc_strlcpy(v2_target_query.request, fields[1],
             sizeof(v2_target_query.request));
  fc_strlcpy(v2_target_query.actor_ref, fields[3],
             sizeof(v2_target_query.actor_ref));
  v2_target_query.revision = v2_revision;
  v2_target_query.actor_id = actor_id;
  v2_target_query.target_tile = native_tile;
  v2_target_query.timer = timer_new(
    TIMER_USER, TIMER_ACTIVE, "agent protocol 2 target query");
  timer_start(v2_target_query.timer);

  if (existing_scope >= 0) {
    struct agent_v2_target_scope *scope =
      &v2_target_scopes[existing_scope];

    v2_target_query.action_count = scope->action_count;
    memcpy(v2_target_query.actions, scope->actions,
           scope->action_count * sizeof(scope->actions[0]));
    v2_target_query_prepare_emit();
    return;
  }
  result = v2_build_target_action(fields[3], native_tile, &action);
  if (result == AGENT_V2_TARGET_BUILD_INVALID_ACTOR) {
    v2_error(fields[1], "INVALID_ACTOR",
             "target actor is not the current player or an owned unit/city");
    v2_target_query_clear();
    return;
  }
  if (result == AGENT_V2_TARGET_BUILD_ONE) {
    v2_target_query.actions[v2_target_query.action_count++] = action;
  }
  if (actor_kind != AGENT_V2_ENTITY_UNIT) {
    v2_target_query_prepare_emit();
    return;
  }
  {
    struct unit *actor = player_unit_by_number(client_player(), actor_id);
    struct tile *target = index_to_tile(&wld.map, native_tile);
    struct agent_v2_action_buffer buffer = {
      .actions = v2_target_query.actions,
      .count = v2_target_query.action_count,
      .capacity = FC_AGENT_V2_MAX_TARGET_ACTIONS,
      .overflow = FALSE,
      .export_unknown_rows = FALSE,
      .unknown_exported = NULL
    };
    enum known_type known = client_tile_get_known(target);

    v2_build_unit_target_paradrop_actions(actor, target, &buffer);
    if (buffer.overflow) {
      v2_error(v2_target_query.request, "SCOPE_TOO_LARGE",
               "target action scope exceeds bounded capacity");
      v2_target_query_clear();
      return;
    }
    v2_target_query.action_count = buffer.count;
    if (!fc_agent_v2_target_server_query_allowed(
          known != TILE_UNKNOWN, known == TILE_KNOWN_SEEN)) {
      v2_target_query.cost_action_index = 0;
      v2_target_query_request_next_cost();
      return;
    }
  }
  dsend_packet_unit_get_actions(
    &client.conn, actor_id, IDENTITY_NUMBER_ZERO, native_tile, EXTRA_NONE,
    AGENT_V2_ACTION_QUERY_KIND);
}

static void v2_begin_special_revalidation(
  char **fields, const struct agent_v2_action *action)
{
  const struct action *native;
  struct player *self = client_player();
  struct unit *actor;
  int target_id;
  int subtarget_id;
  int before_request;

  if (v2_special_revalidation.active) {
    v2_error(fields[1], "BUSY", "one action preflight is already pending");
    return;
  }
  if (v2_special_revalidation_desynchronized) {
    v2_error(fields[1], "REVALIDATION_DESYNC",
             "action preflight requires a fresh sidecar process");
    return;
  }
  actor = self != NULL
          ? player_unit_by_number(self, action->unit_id) : NULL;
  native = action_by_number(action->action);
  if (actor == NULL || native == NULL
      || !v2_special_action_still_bound(
           self, actor, action, &target_id, &subtarget_id)) {
    v2_error(fields[1], "STALE_SLOT",
             "server-discovered unit capability is no longer bound");
    return;
  }
  v2_special_revalidation_clear();
  v2_special_revalidation.active = TRUE;
  fc_strlcpy(v2_special_revalidation.request, fields[1],
             sizeof(v2_special_revalidation.request));
  fc_strlcpy(v2_special_revalidation.slot, fields[2],
             sizeof(v2_special_revalidation.slot));
  v2_special_revalidation.revision = v2_revision;
  v2_special_revalidation.action = *action;
  v2_special_revalidation.timer = timer_new(
    TIMER_USER, TIMER_ACTIVE, "agent protocol 2 action preflight");
  timer_start(v2_special_revalidation.timer);
  before_request = client.conn.client.last_request_id_used;
  dsend_packet_unit_get_actions(
    &client.conn, action->unit_id, IDENTITY_NUMBER_ZERO,
    action->target_tile, EXTRA_NONE, AGENT_V2_ACTION_REVALIDATE_KIND);
  if (client.conn.client.last_request_id_used == before_request) {
    v2_special_revalidation_clear();
    v2_error(fields[1], "NOT_SENT", "server action preflight was not sent");
  }
}

static void v2_handle_scope_open(char **fields)
{
  struct agent_v2_scope *scope;
  uint64_t expected_revision;
  size_t action_count = 0;
  bool overflow = FALSE;
  char encoded_actor[160];
  size_t i;

  (void) v2_parse_revision(fields[2], &expected_revision);
  if (!v2_refresh()) {
    v2_error(fields[1], "OBS_TOO_LARGE", "state registry is unavailable");
    return;
  }
  if (expected_revision != v2_revision) {
    v2_error(fields[1], "STALE_REVISION", "scope revision is not current");
    return;
  }
  if (!v2_build_actor_scope(fields[3], v2_scope_actions,
                            &action_count, &overflow)) {
    v2_error(fields[1], "INVALID_ACTOR", "scope actor is not current and owned");
    return;
  }
  if (!fc_agent_v2_percent_encode(fields[3], encoded_actor,
                                  sizeof(encoded_actor))) {
    v2_error(fields[1], "ENCODE_FAILED", "scope actor encoding failed");
    return;
  }
  if (overflow) {
    v2_sendf("SCOPE_OPENED\t%s\t-\t%llu\t%s\t0\t0\t1",
             fields[1], (unsigned long long) v2_revision, encoded_actor);
    return;
  }
  scope = &v2_scopes[v2_scope_serial % AGENT_V2_SCOPE_PINNED];
  v2_scope_serial++;
  memset(scope, 0, sizeof(*scope));
  scope->valid = TRUE;
  scope->revision = v2_revision;
  scope->action_count = action_count;
  fc_strlcpy(scope->actor_ref, fields[3], sizeof(scope->actor_ref));
  memcpy(scope->actions, v2_scope_actions,
         action_count * sizeof(v2_scope_actions[0]));
  fc_snprintf(scope->id, sizeof(scope->id), "v%llu-%u",
              (unsigned long long) scope->revision, v2_scope_serial);
  /* A slot collision makes execution ambiguous and therefore invalidates the
   * complete scope before any prefix can escape. */
  for (i = 0; i < action_count; i++) {
    size_t j;

    for (j = i + 1; j < action_count; j++) {
      if (strcmp(scope->actions[i].slot, scope->actions[j].slot) == 0) {
        scope->valid = FALSE;
        v2_sendf("SCOPE_OPENED\t%s\t-\t%llu\t%s\t0\t0\t1",
                 fields[1], (unsigned long long) v2_revision, encoded_actor);
        return;
      }
    }
  }
  v2_sendf("SCOPE_OPENED\t%s\t%s\t%llu\t%s\t%zu\t1\t0",
           fields[1], scope->id, (unsigned long long) scope->revision,
           encoded_actor, scope->action_count);
}

static void v2_handle_scope_page(char **fields)
{
  struct agent_v2_scope *scope = v2_scope_by_id(fields[2]);
  size_t current_action_count = 0;
  size_t offset;
  size_t limit;
  size_t end;
  size_t i;
  bool current_overflow = FALSE;
  char encoded_actor[160];

  (void) v2_parse_size(fields[3], &offset);
  (void) v2_parse_size(fields[4], &limit);
  if (scope == NULL) {
    v2_error(fields[1], "SCOPE_GONE", "scope view is not pinned");
    return;
  }
  if (!v2_refresh() || scope->revision != v2_revision) {
    scope->valid = FALSE;
    v2_error(fields[1], "STALE_REVISION", "scope revision is not current");
    return;
  }
  if (!v2_build_actor_scope(scope->actor_ref, v2_scope_actions,
                            &current_action_count, &current_overflow)
      || current_overflow
      || current_action_count != scope->action_count) {
    scope->valid = FALSE;
    v2_error(fields[1], "STALE_REVISION",
             "actor capability catalog changed during paging");
    return;
  }
  for (i = 0; i < current_action_count; i++) {
    if (!v2_action_equal(&scope->actions[i], &v2_scope_actions[i])
        || strcmp(scope->actions[i].slot,
                  v2_scope_actions[i].slot) != 0) {
      scope->valid = FALSE;
      v2_error(fields[1], "STALE_REVISION",
               "actor capability catalog changed during paging");
      return;
    }
  }
  if (offset > scope->action_count) {
    v2_error(fields[1], "BAD_OFFSET", "offset exceeds scope actions");
    return;
  }
  end = offset + limit;
  if (end < offset || end > scope->action_count) {
    end = scope->action_count;
  }
  if (!fc_agent_v2_percent_encode(scope->actor_ref, encoded_actor,
                                  sizeof(encoded_actor))) {
    v2_error(fields[1], "ENCODE_FAILED", "scope actor encoding failed");
    return;
  }
  v2_sendf("SCOPE_BEGIN\t%s\t%s\t%llu\t%s\t%zu\t%zu\t%zu",
           fields[1], scope->id, (unsigned long long) scope->revision,
           encoded_actor, offset, end - offset, scope->action_count);
  for (i = offset; i < end; i++) {
    struct agent_v2_row row;
    char encoded[AGENT_V2_ROW_MAX * 3 + 1];

    if (!v2_format_action_row(&scope->actions[i], &row)
        || !fc_agent_v2_percent_encode(row.text, encoded, sizeof(encoded))) {
      scope->valid = FALSE;
      v2_error(fields[1], "ENCODE_FAILED", "scope action encoding failed");
      return;
    }
    v2_sendf("SCOPE_ACTION\t%s\t%s\t%zu\t%s",
             fields[1], scope->id, i, encoded);
  }
  v2_sendf("SCOPE_END\t%s\t%s\t%zu", fields[1], scope->id, end);
}

static void v2_handle_state_scope_open(char **fields)
{
  struct agent_v2_state_scope *scope;
  uint64_t expected_revision;
  char encoded_section[sizeof(scope->section) * 3 + 1];
  char encoded_selector[sizeof(scope->selector) * 3 + 1];

  (void) v2_parse_revision(fields[2], &expected_revision);
  if (!v2_refresh()) {
    v2_error(fields[1], "OBS_TOO_LARGE", "state registry is unavailable");
    return;
  }
  if (expected_revision != v2_revision) {
    v2_error(fields[1], "STALE_REVISION",
             "state scope revision is not current");
    return;
  }
  if (!v2_build_state_scope_rows(fields[3], fields[4], TRUE)) {
    FC_FREE(v2_state_scope_rows);
    v2_state_scope_row_capacity = 0;
    if (v2_overflow) {
      v2_error(fields[1], "STATE_SCOPE_TOO_LARGE",
               "state scope exceeds its row or byte limit");
      return;
    }
    v2_error(fields[1], "INVALID_ACTOR",
             "state scope selector is not current and authorized");
    return;
  }
  scope = &v2_state_scopes[
    v2_state_scope_serial % AGENT_V2_STATE_SCOPE_PINNED];
  v2_state_scope_serial++;
  v2_state_scope_release(scope);
  scope->valid = TRUE;
  scope->revision = v2_revision;
  scope->total = v2_state_scope_total;
  scope->bytes = v2_state_scope_bytes;
  scope->digest = v2_state_scope_digest;
  scope->rows = v2_state_scope_rows;
  v2_state_scope_rows = NULL;
  v2_state_scope_row_capacity = 0;
  fc_strlcpy(scope->section, fields[3], sizeof(scope->section));
  fc_strlcpy(scope->selector, fields[4], sizeof(scope->selector));
  fc_snprintf(scope->id, sizeof(scope->id), "q%llu-%u",
              (unsigned long long) scope->revision, v2_state_scope_serial);
  if (!fc_agent_v2_percent_encode(
        scope->section, encoded_section, sizeof(encoded_section))
      || !fc_agent_v2_percent_encode(
           scope->selector, encoded_selector, sizeof(encoded_selector))) {
    v2_state_scope_release(scope);
    v2_error(fields[1], "ENCODE_FAILED",
             "state scope identity encoding failed");
    return;
  }
  v2_sendf("STATE_SCOPE_OPENED\t%s\t%s\t%llu\t%s\t%s\t%zu\t1\t0",
           fields[1], scope->id, (unsigned long long) scope->revision,
           encoded_section, encoded_selector, scope->total);
  if (strcmp(fields[3], "investigation") == 0) {
    v2_investigation.consumed = TRUE;
  }
}

static void v2_handle_state_scope_page(char **fields)
{
  struct agent_v2_state_scope *scope = v2_state_scope_by_id(fields[2]);
  size_t offset;
  size_t limit;
  size_t end;
  char encoded_section[sizeof(scope->section) * 3 + 1];
  char encoded_selector[sizeof(scope->selector) * 3 + 1];

  (void) v2_parse_size(fields[3], &offset);
  (void) v2_parse_size(fields[4], &limit);
  if (scope == NULL) {
    v2_error(fields[1], "SCOPE_GONE", "state scope view is not pinned");
    return;
  }
  if (offset > scope->total) {
    v2_error(fields[1], "BAD_OFFSET", "offset exceeds state scope rows");
    return;
  }
  end = offset + limit;
  if (end < offset || end > scope->total) {
    end = scope->total;
  }
  if (!fc_agent_v2_percent_encode(
        scope->section, encoded_section, sizeof(encoded_section))
      || !fc_agent_v2_percent_encode(
           scope->selector, encoded_selector, sizeof(encoded_selector))) {
    v2_error(fields[1], "ENCODE_FAILED",
             "state scope identity encoding failed");
    return;
  }
  v2_sendf("STATE_SCOPE_BEGIN\t%s\t%s\t%llu\t%s\t%s\t%zu\t%zu\t%zu",
           fields[1], scope->id, (unsigned long long) scope->revision,
           encoded_section, encoded_selector, offset,
           end - offset, scope->total);
  for (size_t i = offset; i < end; i++) {
    char encoded[AGENT_V2_ROW_MAX * 3 + 1];

    if (!fc_agent_v2_percent_encode(scope->rows[i].text,
                                    encoded, sizeof(encoded))) {
      v2_error(fields[1], "ENCODE_FAILED",
               "state scope row encoding failed");
      return;
    }
    v2_sendf("STATE_SCOPE_ROW\t%s\t%s\t%zu\t%s",
             fields[1], scope->id, i, encoded);
  }
  v2_sendf("STATE_SCOPE_END\t%s\t%s\t%zu",
           fields[1], scope->id, end);
}

static void v2_handle_relation_scope_open(char **fields)
{
  struct agent_v2_relation_scope *scope;
  uint64_t expected_revision;
  size_t action_count = 0;
  bool overflow = FALSE;
  char encoded_actor[160];
  char encoded_counterpart[160];
  size_t i;

  (void) v2_parse_revision(fields[2], &expected_revision);
  if (!v2_refresh()) {
    v2_error(fields[1], "OBS_TOO_LARGE", "state registry is unavailable");
    return;
  }
  if (expected_revision != v2_revision) {
    v2_error(fields[1], "STALE_REVISION",
             "relation scope revision is not current");
    return;
  }
  if (!v2_build_relation_scope(
        fields[3], fields[4], v2_relation_scope_actions,
        &action_count, &overflow)) {
    v2_error(fields[1], "INVALID_RELATION",
             "relation scope pair is not current and player-scoped");
    return;
  }
  if (!fc_agent_v2_percent_encode(
        fields[3], encoded_actor, sizeof(encoded_actor))
      || !fc_agent_v2_percent_encode(
           fields[4], encoded_counterpart, sizeof(encoded_counterpart))) {
    v2_error(fields[1], "ENCODE_FAILED",
             "relation scope pair encoding failed");
    return;
  }
  if (overflow) {
    v2_sendf(
      "RELATION_SCOPE_OPENED\t%s\t-\t%llu\t%s\t%s\t0\t0\t1",
      fields[1], (unsigned long long) v2_revision,
      encoded_actor, encoded_counterpart);
    return;
  }
  scope = &v2_relation_scopes[
    v2_scope_serial % AGENT_V2_RELATION_SCOPE_PINNED];
  v2_scope_serial++;
  memset(scope, 0, sizeof(*scope));
  scope->valid = TRUE;
  scope->revision = v2_revision;
  scope->action_count = action_count;
  fc_strlcpy(scope->actor_ref, fields[3], sizeof(scope->actor_ref));
  fc_strlcpy(scope->counterpart_ref, fields[4],
             sizeof(scope->counterpart_ref));
  memcpy(scope->actions, v2_relation_scope_actions,
         action_count * sizeof(v2_relation_scope_actions[0]));
  fc_snprintf(scope->id, sizeof(scope->id), "r%llu-%u",
              (unsigned long long) scope->revision, v2_scope_serial);
  for (i = 0; i < action_count; i++) {
    size_t j;

    for (j = i + 1; j < action_count; j++) {
      if (strcmp(scope->actions[i].slot, scope->actions[j].slot) == 0) {
        scope->valid = FALSE;
        v2_sendf(
          "RELATION_SCOPE_OPENED\t%s\t-\t%llu\t%s\t%s\t0\t0\t1",
          fields[1], (unsigned long long) v2_revision,
          encoded_actor, encoded_counterpart);
        return;
      }
    }
  }
  v2_sendf(
    "RELATION_SCOPE_OPENED\t%s\t%s\t%llu\t%s\t%s\t%zu\t1\t0",
    fields[1], scope->id, (unsigned long long) scope->revision,
    encoded_actor, encoded_counterpart, scope->action_count);
}

static void v2_handle_relation_scope_page(char **fields)
{
  struct agent_v2_relation_scope *scope =
    v2_relation_scope_by_id(fields[2]);
  size_t current_action_count = 0;
  size_t offset;
  size_t limit;
  size_t end;
  size_t i;
  bool current_overflow = FALSE;
  char encoded_actor[160];
  char encoded_counterpart[160];

  (void) v2_parse_size(fields[3], &offset);
  (void) v2_parse_size(fields[4], &limit);
  if (scope == NULL) {
    v2_error(fields[1], "SCOPE_GONE", "relation scope view is not pinned");
    return;
  }
  if (!v2_refresh() || scope->revision != v2_revision) {
    scope->valid = FALSE;
    v2_error(fields[1], "STALE_REVISION",
             "relation scope revision is not current");
    return;
  }
  if (!v2_build_relation_scope(
        scope->actor_ref, scope->counterpart_ref,
        v2_relation_scope_actions, &current_action_count,
        &current_overflow)
      || current_overflow
      || current_action_count != scope->action_count) {
    scope->valid = FALSE;
    v2_error(fields[1], "STALE_REVISION",
             "relation capability catalog changed during paging");
    return;
  }
  for (i = 0; i < current_action_count; i++) {
    if (!v2_action_equal(
          &scope->actions[i], &v2_relation_scope_actions[i])
        || strcmp(scope->actions[i].slot,
                  v2_relation_scope_actions[i].slot) != 0) {
      scope->valid = FALSE;
      v2_error(fields[1], "STALE_REVISION",
               "relation capability catalog changed during paging");
      return;
    }
  }
  if (offset > scope->action_count) {
    v2_error(fields[1], "BAD_OFFSET",
             "offset exceeds relation scope actions");
    return;
  }
  end = offset + limit;
  if (end < offset || end > scope->action_count) {
    end = scope->action_count;
  }
  if (!fc_agent_v2_percent_encode(
        scope->actor_ref, encoded_actor, sizeof(encoded_actor))
      || !fc_agent_v2_percent_encode(
           scope->counterpart_ref, encoded_counterpart,
           sizeof(encoded_counterpart))) {
    v2_error(fields[1], "ENCODE_FAILED",
             "relation scope pair encoding failed");
    return;
  }
  v2_sendf(
    "RELATION_SCOPE_BEGIN\t%s\t%s\t%llu\t%s\t%s\t%zu\t%zu\t%zu",
    fields[1], scope->id, (unsigned long long) scope->revision,
    encoded_actor, encoded_counterpart, offset, end - offset,
    scope->action_count);
  for (i = offset; i < end; i++) {
    struct agent_v2_row row;
    char encoded[AGENT_V2_ROW_MAX * 3 + 1];

    if (!v2_format_action_row(&scope->actions[i], &row)
        || !fc_agent_v2_percent_encode(
             row.text, encoded, sizeof(encoded))) {
      scope->valid = FALSE;
      v2_error(fields[1], "ENCODE_FAILED",
               "relation scope action encoding failed");
      return;
    }
    v2_sendf("RELATION_SCOPE_ACTION\t%s\t%s\t%zu\t%s",
             fields[1], scope->id, i, encoded);
  }
  v2_sendf("RELATION_SCOPE_END\t%s\t%s\t%zu",
           fields[1], scope->id, end);
}

static bool v2_treaty_has_clause(
  const struct treaty *treaty, int giver_id,
  enum clause_type type, int value)
{
  if (treaty == NULL) {
    return FALSE;
  }
  clause_list_iterate(treaty->clauses, clause) {
    if (clause->from != NULL
        && player_number(clause->from) == giver_id
        && clause->type == type && clause->value == value) {
      return TRUE;
    }
  } clause_list_iterate_end;
  return FALSE;
}

static bool v2_relation_action_still_legal(
  const struct agent_v2_action *action, int proposed_gold)
{
  struct player *self = client_player();
  struct player *other;
  struct player *giver = NULL;
  struct treaty *treaty;
  struct agent_v2_relation_state *relation;
  size_t clause_count;
  uint64_t clauses_digest;
  bool self_accepted = FALSE;
  bool other_accepted = FALSE;
  int clause_value = action->clause_value;

  if (action == NULL || self == NULL
      || action->player_id != player_number(self)
      || action->player_incarnation != v2_existing_incarnation(
           AGENT_V2_ENTITY_PLAYER, player_number(self))
      || (other = player_by_number(action->counterpart_id)) == NULL
      || other == self
      || action->counterpart_incarnation != v2_existing_incarnation(
           AGENT_V2_ENTITY_PLAYER, action->counterpart_id)
      || !self->is_alive || !other->is_alive) {
    return FALSE;
  }
  treaty = find_treaty(self, other);
  relation = v2_relation_state(other, treaty != NULL);
  if (relation == NULL
      || !v2_treaty_clause_summary(
           treaty, &clause_count, &clauses_digest)) {
    return FALSE;
  }
  (void) clause_count;
  if (treaty != NULL) {
    self_accepted = treaty->plr0 == self
                    ? treaty->accept0 : treaty->accept1;
    other_accepted = treaty->plr0 == self
                     ? treaty->accept1 : treaty->accept0;
  }
  if (!fc_agent_v2_relation_baseline_matches(
        action->meeting_generation, relation->meeting_generation,
        action->clauses_digest, clauses_digest,
        action->self_accepted, self_accepted,
        action->other_accepted, other_accepted,
        action->relation_state, player_diplstate_get(self, other)->type,
        action->outgoing_vision, gives_shared_vision(self, other),
        action->outgoing_shared_tiles, gives_shared_tiles(self, other))) {
    return FALSE;
  }
  if (action->clause_giver_id >= 0) {
    giver = player_by_number(action->clause_giver_id);
    if (giver == NULL || (giver != self && giver != other)
        || !clause_type_is_valid(action->clause_type)) {
      return FALSE;
    }
  }
  if (proposed_gold >= 0) {
    if (action->clause_type != CLAUSE_GOLD
        || proposed_gold < 1 || proposed_gold > action->clause_value
        || giver == NULL || proposed_gold > giver->economic.gold) {
      return FALSE;
    }
    clause_value = proposed_gold;
  }
  switch (action->kind) {
  case AGENT_V2_ACTION_DIPLOMACY_OPEN_MEETING:
    return treaty == NULL && can_meet_with_player(other);
  case AGENT_V2_ACTION_DIPLOMACY_CLOSE_MEETING:
    return treaty != NULL;
  case AGENT_V2_ACTION_DIPLOMACY_PROPOSE_CLAUSE:
    if (treaty == NULL || giver == NULL
        || (action->clause_type == CLAUSE_GOLD) != (proposed_gold >= 0)) {
      return FALSE;
    }
    if (action->clause_type == CLAUSE_CITY) {
      struct city *city = game_city_by_number(clause_value);

      if (city == NULL || city_owner(city) != giver
          || city->client.lifecycle_id == 0
          || city->client.lifecycle_id != action->source_city_lifecycle_id
          || v2_existing_incarnation(AGENT_V2_ENTITY_CITY, city->id)
             != action->source_city_incarnation
          || city_tile(city) == NULL
          || tile_index(city_tile(city)) != action->source_city_tile
          || is_capital(city)) {
        return FALSE;
      }
    }
    return v2_treaty_candidate_possible(
      treaty, self, other, giver, action->clause_type, clause_value);
  case AGENT_V2_ACTION_DIPLOMACY_REMOVE_CLAUSE:
    return treaty != NULL && giver != NULL
           && v2_treaty_has_clause(
                treaty, action->clause_giver_id,
                action->clause_type, action->clause_value);
  case AGENT_V2_ACTION_DIPLOMACY_ACCEPT:
    return treaty != NULL && action->desired_acceptance == 1
           && !self_accepted;
  case AGENT_V2_ACTION_DIPLOMACY_WITHDRAW_ACCEPTANCE:
    return treaty != NULL && action->desired_acceptance == 0
           && self_accepted;
  case AGENT_V2_ACTION_DIPLOMACY_BREAK_RELATION:
    return pplayer_can_cancel_treaty(self, other) == DIPL_OK
           && cancel_pact_result(
                player_diplstate_get(self, other)->type)
              != player_diplstate_get(self, other)->type;
  case AGENT_V2_ACTION_DIPLOMACY_WITHDRAW_VISION:
    return gives_shared_vision(self, other);
  case AGENT_V2_ACTION_DIPLOMACY_WITHDRAW_SHARED_TILES:
    return gives_shared_tiles(self, other);
  default:
    return FALSE;
  }
}

static bool v2_parse_gold_argument(const char *text, int *amount)
{
  size_t parsed;

  if (text == NULL || strncmp(text, "gold=", 5) != 0
      || !v2_parse_size(text + 5, &parsed)
      || parsed == 0 || parsed > INT_MAX) {
    return FALSE;
  }
  *amount = (int) parsed;
  return TRUE;
}

static bool v2_parse_pregame_config_argument(
  const char *text, int *nation, char *leader, size_t leader_size,
  bool *is_male, int *style)
{
  const char *leader_start;
  const char *sex_start;
  const char *style_start;
  char nation_text[24];
  char leader_text[AGENT_V2_ROW_MAX];
  size_t nation_length;
  size_t leader_length;
  size_t parsed_nation;
  size_t parsed_style;

  if (text == NULL || nation == NULL || leader == NULL || leader_size == 0
      || is_male == NULL || style == NULL
      || strncmp(text, "nation=", 7) != 0
      || (leader_start = strstr(text + 7, ",leader=")) == NULL
      || (sex_start = strstr(leader_start + 8, ",is_male=")) == NULL
      || (style_start = strstr(sex_start + 9, ",style=")) == NULL
      || strchr(style_start + 7, ',') != NULL) {
    return FALSE;
  }
  nation_length = (size_t) (leader_start - (text + 7));
  leader_length = (size_t) (sex_start - (leader_start + 8));
  if (nation_length == 0 || nation_length >= sizeof(nation_text)
      || leader_length == 0 || leader_length >= sizeof(leader_text)) {
    return FALSE;
  }
  memcpy(nation_text, text + 7, nation_length);
  nation_text[nation_length] = '\0';
  memcpy(leader_text, leader_start + 8, leader_length);
  leader_text[leader_length] = '\0';
  if (!v2_parse_size(nation_text, &parsed_nation)
      || parsed_nation > INT_MAX
      || style_start != sex_start + 10
      || (sex_start[9] != '0' && sex_start[9] != '1')
      || !v2_parse_size(style_start + 7, &parsed_style)
      || parsed_style > INT_MAX
      || !fc_agent_v2_percent_decode(leader_text, leader, leader_size)
      || leader[0] == '\0') {
    return FALSE;
  }
  *nation = (int) parsed_nation;
  *style = (int) parsed_style;
  *is_male = sex_start[9] == '1';
  return TRUE;
}

static struct client_unit_route_plan *v2_parse_unit_route_argument(
  struct unit *punit, const char *text)
{
  struct tile *waypoints[CLIENT_UNIT_ROUTE_MAX_WAYPOINTS];
  enum client_unit_route_mode mode;
  const char *cursor;
  size_t count = 0;

  if (punit == NULL || text == NULL) {
    return NULL;
  }
  if (strncmp(text, "mode=goto;waypoints=", 20) == 0) {
    mode = CLIENT_UNIT_ROUTE_GOTO;
    cursor = text + 20;
  } else if (strncmp(text, "mode=patrol;waypoints=", 22) == 0) {
    mode = CLIENT_UNIT_ROUTE_PATROL;
    cursor = text + 22;
  } else {
    return NULL;
  }
  while (*cursor != '\0') {
    const char *comma = strchr(cursor, ',');
    size_t length = comma != NULL ? (size_t) (comma - cursor)
                                  : strlen(cursor);
    char token[32];
    char canonical[32];
    size_t parsed;
    struct tile *tile;

    if (count >= ARRAY_SIZE(waypoints) || length == 0
        || length >= sizeof(token)) {
      return NULL;
    }
    memcpy(token, cursor, length);
    token[length] = '\0';
    if (!v2_parse_size(token, &parsed) || parsed > INT_MAX) {
      return NULL;
    }
    fc_snprintf(canonical, sizeof(canonical), "%zu", parsed);
    if (strcmp(token, canonical) != 0
        || !v2_tile_index_valid((int) parsed)
        || (tile = index_to_tile(&wld.map, (int) parsed)) == NULL
        || client_tile_get_known(tile) == TILE_UNKNOWN) {
      return NULL;
    }
    waypoints[count++] = tile;
    if (comma == NULL) {
      break;
    }
    cursor = comma + 1;
  }
  if (count < 1) {
    return NULL;
  }
  if (waypoints[0] == unit_tile(punit)
      || (mode == CLIENT_UNIT_ROUTE_GOTO
          && waypoints[count - 1] == unit_tile(punit))) {
    return NULL;
  }
  return client_unit_route_plan_new(punit, mode, waypoints, count);
}

static bool v2_parse_infrastructure_extra_argument(
  const struct agent_v2_action *action, const char *text,
  struct extra_type **result)
{
  size_t parsed;
  char canonical[32];
  const char *cursor;

  if (action == NULL || text == NULL || result == NULL
      || strncmp(text, "extra=", 6) != 0
      || !v2_parse_size(text + 6, &parsed) || parsed > INT_MAX) {
    return FALSE;
  }
  fc_snprintf(canonical, sizeof(canonical), "extra=%zu", parsed);
  if (strcmp(text, canonical) != 0
      || (*result = extra_by_number((int) parsed)) == NULL) {
    return FALSE;
  }
  cursor = action->infrastructure_choices;
  while (*cursor != '\0') {
    const char *comma = strchr(cursor, ',');
    size_t length = comma != NULL ? (size_t) (comma - cursor)
                                  : strlen(cursor);

    if (strlen(text + 6) == length
        && memcmp(cursor, text + 6, length) == 0) {
      return TRUE;
    }
    if (comma == NULL) {
      break;
    }
    cursor = comma + 1;
  }
  *result = NULL;
  return FALSE;
}

static bool v2_infrastructure_choice_still_legal(
  const struct player *self, const struct agent_v2_action *action,
  const struct extra_type *extra)
{
  struct tile *target;

  return self != NULL && action != NULL && extra != NULL
         && terrain_control.infrapoints
         && action->player_id == player_number(self)
         && action->player_incarnation == v2_existing_incarnation(
              AGENT_V2_ENTITY_PLAYER, player_number(self))
         && v2_tile_index_valid(action->target_tile)
         && (target = index_to_tile(&wld.map, action->target_tile)) != NULL
         && client_tile_get_known(target) == TILE_KNOWN_SEEN
         && target->placing == NULL
         && extra->infracost > 0
         && self->economic.infra_points >= extra->infracost
         && player_can_place_extra(extra, self, target);
}

static bool v2_parse_chat_argument(
  const char *text, bool *allied, char *message, size_t message_size)
{
  static const char global_prefix[] = "channel=global;message=";
  static const char allied_prefix[] = "channel=allied;message=";
  const char *encoded;
  char canonical[FC_AGENT_V2_MAX_CHAT_MESSAGE_BYTES * 3 + 1];
  const unsigned char *cursor;
  size_t length;

  if (text == NULL || allied == NULL || message == NULL
      || message_size < FC_AGENT_V2_MAX_CHAT_MESSAGE_BYTES + 1) {
    return FALSE;
  }
  if (strncmp(text, global_prefix, sizeof(global_prefix) - 1) == 0) {
    *allied = FALSE;
    encoded = text + sizeof(global_prefix) - 1;
  } else if (strncmp(text, allied_prefix,
                     sizeof(allied_prefix) - 1) == 0) {
    *allied = TRUE;
    encoded = text + sizeof(allied_prefix) - 1;
  } else {
    return FALSE;
  }
  if (!fc_agent_v2_percent_decode(encoded, message, message_size)
      || !fc_agent_v2_percent_encode(message, canonical,
                                     sizeof(canonical))
      || strcmp(encoded, canonical) != 0) {
    return FALSE;
  }
  length = strlen(message);
  if (length == 0 || length > FC_AGENT_V2_MAX_CHAT_MESSAGE_BYTES
      || message[0] == SERVER_COMMAND_PREFIX
      || message[0] == CHAT_ALLIES_PREFIX
      || message[0] == CHAT_DIRECT_PREFIX
      || message[0] == ' ' || message[length - 1] == ' ') {
    return FALSE;
  }
  for (cursor = (const unsigned char *) message; *cursor != '\0'; cursor++) {
    if (*cursor < 0x20 || *cursor == 0x7f
        || (*cursor == 0xc2 && cursor[1] >= 0x80 && cursor[1] <= 0x9f)
        || *cursor == (unsigned char) CHAT_DIRECT_PREFIX) {
      return FALSE;
    }
  }
  return TRUE;
}

static void v2_execute_action(char **fields,
                              const struct agent_v2_action *action)
{
  struct agent_v2_action frozen_action;
  struct player *self = client_player();
  const struct research *research;
  struct unit *unit = NULL;
  struct unit *target_unit = NULL;
  struct city *city = NULL;
  struct city *destination_city = NULL;
  struct extra_type *extra = NULL;
  struct government *government = NULL;
  struct universal production;
  struct worklist requested_worklist;
  struct client_rally_plan *rally_plan = NULL;
  struct client_unit_route_plan *unit_route_plan = NULL;
  bv_city_options requested_city_options;
  enum city_wl_cancel_behavior requested_wlcb = WLCB_LAST;
  bool requested_rally_persistent = FALSE;
  struct cm_parameter requested_governor;
  int requested_pregame_nation = -1;
  int requested_pregame_style = -1;
  int requested_pregame_team = -1;
  bool requested_pregame_male = FALSE;
  bool requested_pregame_ready = FALSE;
  char requested_pregame_leader[MAX_LEN_NAME] = "";
  int requested_gold = -1;
  bool requested_chat_allied = FALSE;
  char requested_chat_message[FC_AGENT_V2_MAX_CHAT_MESSAGE_BYTES + 1] = "";
  enum client_vote_type requested_vote = CVT_NONE;
  int special_target = -1;
  int special_subtarget = NO_TARGET;
  int before_request;
  uint64_t selected_revision = v2_revision;
  bool pregame_action;
  bool communication_action;
  bool vote_action;

  if (action->kind == AGENT_V2_ACTION_PREGAME_SET_TEAM
      || action->kind == AGENT_V2_ACTION_PLAYER_CAST_VOTE) {
    /* A refresh immediately before dispatch must compare against the
     * exact action selected at entry, not an aliased current-registry row. */
    frozen_action = *action;
    action = &frozen_action;
  }
  pregame_action =
    action->kind == AGENT_V2_ACTION_PREGAME_CONFIGURE
    || action->kind == AGENT_V2_ACTION_PREGAME_SET_TEAM
    || action->kind == AGENT_V2_ACTION_PREGAME_SET_READY;
  communication_action =
    action->kind == AGENT_V2_ACTION_PLAYER_SEND_CHAT;
  vote_action = action->kind == AGENT_V2_ACTION_PLAYER_CAST_VOTE;

  worklist_init(&requested_worklist);
  BV_CLR_ALL(requested_city_options);

  if (v2_pending.active) {
    v2_error(fields[1], "BUSY", "one native action is already pending");
    return;
  }
  if (vote_action) {
    if ((client_state() != C_S_PREPARING
         && client_state() != C_S_RUNNING)
        || !v2_vote_action_still_legal(self, action)) {
      v2_error(fields[1], "NOT_READY", "client cannot cast this vote now");
      return;
    }
  } else if (pregame_action) {
    if (client_state() != C_S_PREPARING || !v2_seat_authorized
        || !v2_cache_coherent() || self == NULL || !is_human(self)
        || action->player_id != player_number(self)
        || action->player_incarnation != v2_existing_incarnation(
             AGENT_V2_ENTITY_PLAYER, player_number(self))
        || (action->kind == AGENT_V2_ACTION_PREGAME_SET_TEAM
            && (!game.info.is_new_game || self->is_ready
                || !v2_pregame_team_command_name_safe(player_name(self))))
        || is_server_busy()) {
      v2_error(fields[1], "NOT_READY",
               "client cannot issue pregame commands now");
      return;
    }
  } else if (communication_action) {
    if (!v2_communication_ready(self)
        || action->player_id != player_number(self)
        || action->player_incarnation != v2_existing_incarnation(
             AGENT_V2_ENTITY_PLAYER, player_number(self))) {
      v2_error(fields[1], "NOT_READY",
               "client cannot issue communication now");
      return;
    }
  } else if (!fc_agent_v2_action_phase_ready(
               v2_seat_authorized, v2_cache_coherent(),
               can_client_issue_orders(), self != NULL && self->is_alive,
               self != NULL && is_player_phase(self, game.info.phase),
               self != NULL && self->phase_done, is_server_busy())) {
    v2_error(fields[1], "NOT_READY", "client cannot issue orders now");
    return;
  }
  if (action->kind >= AGENT_V2_ACTION_DIPLOMACY_OPEN_MEETING
      && action->kind
         <= AGENT_V2_ACTION_DIPLOMACY_WITHDRAW_SHARED_TILES
      && !(action->kind == AGENT_V2_ACTION_DIPLOMACY_PROPOSE_CLAUSE
           && action->clause_type == CLAUSE_GOLD)
      && !v2_relation_action_still_legal(action, -1)) {
    v2_error(fields[1], "STALE_SLOT",
             "diplomacy capability is no longer legal");
    return;
  }
  if (action->unit_id >= 0) {
    unit = player_unit_by_number(self, action->unit_id);
    if (unit == NULL
        || action->unit_lifecycle_id == 0
        || unit->client.lifecycle_id != action->unit_lifecycle_id
        || v2_existing_incarnation(AGENT_V2_ENTITY_UNIT, unit->id)
           != action->unit_incarnation) {
      v2_error(fields[1], "STALE_ENTITY", "action actor no longer exists");
      return;
    }
  }
  if (action->city_id >= 0) {
    city = player_city_by_number(self, action->city_id);
    if (!v2_city_action_still_legal(
          self, city, action, &production)) {
      v2_error(fields[1], "STALE_SLOT", "city capability is no longer legal");
      return;
    }
  }
  if ((action->kind == AGENT_V2_ACTION_WORKER_START
       || action->kind == AGENT_V2_ACTION_CANCEL_ACTIVITY)
      && !v2_worker_action_still_legal(unit, action, &extra)) {
    v2_error(fields[1], "STALE_SLOT", "unit activity is no longer legal");
    return;
  }
  if ((action->kind == AGENT_V2_ACTION_UNIT_AUTO_WORK
       || action->kind == AGENT_V2_ACTION_UNIT_AUTO_EXPLORE
       || action->kind == AGENT_V2_ACTION_UNIT_CANCEL_AUTOMATION)
      && !v2_unit_automation_action_still_legal(self, unit, action)) {
    v2_error(fields[1], "STALE_SLOT",
             "unit automation capability is no longer legal");
    return;
  }
  if (action->kind == AGENT_V2_ACTION_UNIT_CANCEL_ORDERS
      && !v2_unit_cancel_orders_action_still_legal(self, unit, action)) {
    v2_error(fields[1], "STALE_SLOT",
             "unit order capability is no longer legal");
    return;
  }
  if (action->kind == AGENT_V2_ACTION_UNIT_GOTO) {
    if (!v2_unit_goto_action_still_legal(self, unit, action)) {
      v2_error(fields[1], "STALE_SLOT",
               "unit goto capability is no longer legal");
      return;
    }
  }
  if (action->kind == AGENT_V2_ACTION_UNIT_GOTO_AND_PERFORM
      && (!v2_unit_goto_actor_clean(unit)
          || unit_tile(unit) == NULL
          || tile_index(unit_tile(unit)) != action->source_unit_tile
          || !v2_routed_action_target_still_bound(self, action))) {
    v2_error(fields[1], "STALE_SLOT",
             "goto-and-perform capability is no longer bound");
    return;
  }
  if (action->kind == AGENT_V2_ACTION_UNIT_CONNECT_ROUTE
      && !v2_connect_route_still_bound(self, unit, action, &extra)) {
    v2_error(fields[1], "STALE_SLOT",
             "connect-route capability is no longer bound");
    return;
  }
  if (action->kind == AGENT_V2_ACTION_UNIT_SPECIAL
      && !v2_special_action_still_bound(
           self, unit, action, &special_target, &special_subtarget)) {
    v2_error(fields[1], "STALE_SLOT",
             "server-discovered unit capability is no longer bound");
    return;
  }
  if (action->kind == AGENT_V2_ACTION_UNIT_SET_ROUTE
      && !v2_unit_set_route_action_still_legal(self, unit, action)) {
    v2_error(fields[1], "STALE_SLOT",
             "unit route capability is no longer legal");
    return;
  }
  if ((action->kind == AGENT_V2_ACTION_UNIT_SENTRY
       || action->kind == AGENT_V2_ACTION_UNIT_FORTIFY
       || action->kind == AGENT_V2_ACTION_UNIT_CONVERT
       || action->kind == AGENT_V2_ACTION_UNIT_DISBAND
       || action->kind == AGENT_V2_ACTION_UNIT_HOMELESS)
      && !v2_self_unit_action_still_legal(unit, action)) {
    v2_error(fields[1], "STALE_SLOT", "unit capability is no longer legal");
    return;
  }
  if ((action->kind >= AGENT_V2_ACTION_UNIT_UPGRADE
       && action->kind <= AGENT_V2_ACTION_UNIT_DISBAND_RECOVER)
      && !v2_city_target_unit_action_still_legal(
        self, unit, action, &destination_city)) {
    v2_error(fields[1], "STALE_SLOT",
             "city-target unit capability is no longer legal");
    return;
  }
  if ((action->kind >= AGENT_V2_ACTION_TRANSPORT_BOARD
       && action->kind <= AGENT_V2_ACTION_TRANSPORT_UNLOAD)
      && !v2_transport_action_still_legal(
        self, unit, action, &target_unit)) {
    v2_error(fields[1], "STALE_SLOT",
             "transport capability is no longer legal");
    return;
  }
  if ((action->kind == AGENT_V2_ACTION_UNIT_AIRLIFT
       || action->kind == AGENT_V2_ACTION_UNIT_PARADROP
       || action->kind == AGENT_V2_ACTION_UNIT_TELEPORT)
      && !v2_noncombat_mobility_action_still_legal(self, unit, action)) {
    v2_error(fields[1], "STALE_SLOT",
             "relocation capability is no longer legal");
    return;
  }
  if ((action->kind == AGENT_V2_ACTION_GOVERNMENT_REVOLUTION
       || action->kind == AGENT_V2_ACTION_GOVERNMENT_CHANGE)
      && !v2_government_action_still_legal(self, action, &government)) {
    v2_error(fields[1], "STALE_SLOT",
             "government capability is no longer legal");
    return;
  }
  if (action->kind == AGENT_V2_ACTION_MULTIPLIER_SET
      && !v2_multiplier_action_still_legal(self, action)) {
    v2_error(fields[1], "STALE_SLOT",
             "multiplier capability is no longer legal");
    return;
  }
  if ((action->kind == AGENT_V2_ACTION_SPACESHIP_PLACE
       || action->kind == AGENT_V2_ACTION_SPACESHIP_LAUNCH)
      && !v2_spaceship_action_still_legal(self, action)) {
    v2_error(fields[1], "STALE_SLOT",
             "spaceship capability is no longer legal");
    return;
  }
  research = !pregame_action && !vote_action ? research_get(self) : NULL;
  if ((action->kind == AGENT_V2_ACTION_RESEARCH_TARGET
       || action->kind == AGENT_V2_ACTION_RESEARCH_GOAL)
      && !v2_research_action_still_legal(research, action)) {
    v2_error(fields[1], "STALE_SLOT", "research choice is no longer legal");
    return;
  }
  if (action->kind == AGENT_V2_ACTION_PLAYER_CAST_VOTE) {
    if (!v2_parse_vote_argument(fields[3], &requested_vote)
        || !v2_vote_action_still_legal(self, action)) {
      v2_error(fields[1], "BAD_ARGUMENT",
               "player.cast_vote requires yes, no, or abstain");
      return;
    }
  } else if (action->kind == AGENT_V2_ACTION_PREGAME_CONFIGURE) {
    struct nation_type *nation;
    struct nation_style *style;
    const unsigned char *cursor;
    size_t leader_length;

    if (self->is_ready
        || !v2_parse_pregame_config_argument(
             fields[3], &requested_pregame_nation,
             requested_pregame_leader, sizeof(requested_pregame_leader),
             &requested_pregame_male, &requested_pregame_style)
        || (nation = nation_by_number(requested_pregame_nation)) == NULL
        || (style = style_by_number(requested_pregame_style)) == NULL
        || !is_nation_pickable(nation)
        || (nation->player != NULL && nation->player != self)) {
      v2_error(fields[1], "BAD_ARGUMENT",
               "pregame.configure requires a current nation, leader, sex, and style");
      return;
    }
    leader_length = strlen(requested_pregame_leader);
    if (leader_length == 0 || leader_length >= MAX_LEN_NAME
        || requested_pregame_leader[0] == ' '
        || requested_pregame_leader[leader_length - 1] == ' ') {
      v2_error(fields[1], "BAD_ARGUMENT", "leader name is not bounded");
      return;
    }
    for (cursor = (const unsigned char *) requested_pregame_leader;
         *cursor != '\0'; cursor++) {
      if (*cursor < 0x20 || *cursor == 0x7f) {
        v2_error(fields[1], "BAD_ARGUMENT",
                 "leader name contains control characters");
        return;
      }
    }
    /* Match server_player_set_name_full() before no-op comparison, latching,
     * dispatch, and postcondition checking. */
    requested_pregame_leader[0] = fc_toupper(
      requested_pregame_leader[0]);
    if (nation_of_player(self) == nation && self->style == style
        && self->is_male == requested_pregame_male
        && strcmp(player_name(self), requested_pregame_leader) == 0) {
      v2_error(fields[1], "BAD_ARGUMENT",
               "pregame.configure cannot be a no-op");
      return;
    }
  } else if (action->kind == AGENT_V2_ACTION_PREGAME_SET_TEAM) {
    if (!fc_agent_v2_parse_pregame_team_argument(
          fields[3], team_slot_count(), &requested_pregame_team)) {
      v2_error(fields[1], "BAD_ARGUMENT",
               "pregame.set_team requires one canonical team slot");
      return;
    }
    if (self->team != NULL
        && team_number(self->team) == requested_pregame_team) {
      v2_error(fields[1], "BAD_ARGUMENT",
               "pregame.set_team cannot be a no-op");
      return;
    }
    if (!v2_pregame_team_choice_still_allowed(
          self, requested_pregame_team)) {
      v2_error(fields[1], "STALE_SLOT",
               "pregame team choice is no longer advertised");
      return;
    }
  } else if (action->kind == AGENT_V2_ACTION_PREGAME_SET_READY) {
    if ((strcmp(fields[3], "ready=0") != 0
         && strcmp(fields[3], "ready=1") != 0)
        || action->desired_acceptance != (fields[3][6] - '0')) {
      v2_error(fields[1], "BAD_ARGUMENT",
               "pregame.set_ready requires the advertised desired state");
      return;
    }
    requested_pregame_ready = fields[3][6] == '1';
    if (self->is_ready == requested_pregame_ready) {
      v2_error(fields[1], "BAD_ARGUMENT",
               "pregame.set_ready cannot be a no-op");
      return;
    }
  } else if (action->kind == AGENT_V2_ACTION_FOUND_CITY
      || action->kind == AGENT_V2_ACTION_CITY_RENAME) {
    if (strncmp(fields[3], "city_name=", strlen("city_name=")) != 0
        || !v2_city_name_valid(fields[3] + strlen("city_name="))) {
      v2_error(fields[1], "BAD_ARGUMENT",
               "city action requires a bounded city_name");
      return;
    }
    if (action->kind == AGENT_V2_ACTION_CITY_RENAME
        && strcmp(city_name_get(city),
                  fields[3] + strlen("city_name=")) == 0) {
      v2_error(fields[1], "BAD_ARGUMENT", "city.rename cannot be a no-op");
      return;
    }
  } else if (action->kind == AGENT_V2_ACTION_CITY_SET_WORKLIST) {
    if (!v2_parse_worklist_argument(fields[3], city,
                                    &requested_worklist)) {
      v2_error(fields[1], "BAD_ARGUMENT",
               "city.set_worklist requires up to 64 queueable or preserved entries");
      return;
    }
    if (are_worklists_equal(&city->worklist, &requested_worklist)) {
      v2_error(fields[1], "BAD_ARGUMENT",
               "city.set_worklist cannot be a no-op");
      return;
    }
  } else if (action->kind == AGENT_V2_ACTION_CITY_SET_OPTIONS) {
    if (!v2_parse_city_options_argument(
          fields[3], city, &requested_city_options, &requested_wlcb)) {
      v2_error(fields[1], "BAD_ARGUMENT",
               "city.set_options requires normalized city options");
      return;
    }
    if (BV_ARE_EQUAL(city->city_options, requested_city_options)
        && city->wlcb == requested_wlcb) {
      v2_error(fields[1], "BAD_ARGUMENT",
               "city.set_options cannot be a no-op");
      return;
    }
  } else if (action->kind == AGENT_V2_ACTION_CITY_SET_RALLY) {
    if (strcmp(fields[3], "persistent=0") == 0) {
      requested_rally_persistent = FALSE;
    } else if (strcmp(fields[3], "persistent=1") == 0) {
      requested_rally_persistent = TRUE;
    } else {
      v2_error(fields[1], "BAD_ARGUMENT",
               "city.set_rally requires exactly persistent boolean");
      return;
    }
  } else if (action->kind == AGENT_V2_ACTION_CITY_SET_GOVERNOR) {
    struct cm_parameter current_governor;

    if (!v2_parse_cma_argument(fields[3], &requested_governor)) {
      v2_error(fields[1], "BAD_ARGUMENT",
               "city.set_governor requires one bounded governor goal");
      return;
    }
    if (cma_is_city_under_agent(city, &current_governor)
        && cm_are_parameter_equal(&current_governor,
                                  &requested_governor)) {
      v2_error(fields[1], "BAD_ARGUMENT",
               "city.set_governor cannot be a no-op");
      return;
    }
  } else if (action->kind == AGENT_V2_ACTION_CITY_CLEAR_GOVERNOR) {
    if (strcmp(fields[3], "-") != 0) {
      v2_error(fields[1], "BAD_ARGUMENT",
               "city.clear_governor accepts no arguments");
      return;
    }
  } else if (action->kind == AGENT_V2_ACTION_ECONOMY_RATES) {
    int tax;
    int luxury;
    int science;
    int max_rate = v2_player_max_rate(self);

    if (action->max_rate != max_rate
        || !fc_agent_v2_parse_rates(
          fields[3], game.info.changable_tax, max_rate,
          &tax, &luxury, &science)) {
      v2_error(fields[1], "BAD_ARGUMENT",
               "economy.set_rates requires valid tax luxury science rates");
      return;
    }
  } else if (action->kind == AGENT_V2_ACTION_PLAYER_SEND_CHAT) {
    if (!v2_parse_chat_argument(
          fields[3], &requested_chat_allied, requested_chat_message,
          sizeof(requested_chat_message))) {
      v2_error(fields[1], "BAD_ARGUMENT",
               "player.send_chat requires bounded global or allied text");
      return;
    }
  } else if (action->kind == AGENT_V2_ACTION_UNIT_SET_ROUTE) {
    unit_route_plan = v2_parse_unit_route_argument(unit, fields[3]);
    if (unit_route_plan == NULL) {
      v2_error(fields[1], "BAD_ARGUMENT",
               "unit.set_route requires 1-64 current tile waypoints, "
               "first != source and goto final != source");
      return;
    }
  } else if (action->kind == AGENT_V2_ACTION_PLAYER_PLACE_INFRA) {
    if (!v2_parse_infrastructure_extra_argument(action, fields[3], &extra)
        || !v2_infrastructure_choice_still_legal(self, action, extra)) {
      v2_error(fields[1], "BAD_ARGUMENT",
               "player.set_infrastructure requires one advertised current extra");
      return;
    }
  } else if (action->kind == AGENT_V2_ACTION_DIPLOMACY_PROPOSE_CLAUSE
             && action->clause_type == CLAUSE_GOLD) {
    if (!v2_parse_gold_argument(fields[3], &requested_gold)
        || !v2_relation_action_still_legal(action, requested_gold)) {
      v2_error(fields[1], "BAD_ARGUMENT",
               "gold proposal requires gold between 1 and the advertised maximum");
      return;
    }
  } else if (action->kind >= AGENT_V2_ACTION_DIPLOMACY_OPEN_MEETING
             && action->kind
                <= AGENT_V2_ACTION_DIPLOMACY_WITHDRAW_SHARED_TILES) {
    if (strcmp(fields[3], "-") != 0
        || !v2_relation_action_still_legal(action, -1)) {
      v2_error(fields[1], "STALE_SLOT",
               "diplomacy action changed or accepts no arguments");
      return;
    }
  } else if (strcmp(fields[3], "-") != 0) {
    v2_error(fields[1], "BAD_ARGUMENT", "this action accepts no arguments");
    return;
  }

  if (action->kind == AGENT_V2_ACTION_UNIT_GOTO) {
    unit_route_plan = client_unit_goto_plan_new(
      unit, index_to_tile(&wld.map, action->target_tile));
  } else if (action->kind == AGENT_V2_ACTION_UNIT_GOTO_AND_PERFORM) {
    unit_route_plan = client_unit_action_route_plan_new(
      unit, index_to_tile(&wld.map, action->target_tile),
      action->action, NO_TARGET);
  } else if (action->kind == AGENT_V2_ACTION_UNIT_CONNECT_ROUTE) {
    unit_route_plan = client_unit_connect_plan_new(
      unit, index_to_tile(&wld.map, action->target_tile),
      action->target_activity, extra);
  }
  if ((action->kind == AGENT_V2_ACTION_UNIT_GOTO
       || action->kind == AGENT_V2_ACTION_UNIT_GOTO_AND_PERFORM
       || action->kind == AGENT_V2_ACTION_UNIT_CONNECT_ROUTE)
      && !v2_route_plan_matches_action(unit_route_plan, action)) {
    client_unit_route_plan_destroy(unit_route_plan);
    v2_error(fields[1], "STALE_SLOT",
             "exact unit route changed before dispatch");
    return;
  }
  if (action->kind == AGENT_V2_ACTION_UNIT_GOTO_AND_PERFORM) {
    const struct action *native = action_by_number(action->action);
    struct tile *routed_target = index_to_tile(
      &wld.map, action->target_tile);
    struct unit *routed_target_unit =
      native != NULL && action_get_target_kind(native) == ATK_UNIT
      ? game_unit_by_number(action->target_unit_id) : NULL;
    struct city *routed_target_city =
      native != NULL && action_get_target_kind(native) == ATK_CITY
      ? game_city_by_number(action->destination_city_id) : NULL;

    if (native == NULL
        || !v2_goto_and_perform_possible_at_plan(
             unit, routed_target_unit, routed_target_city, routed_target,
             native, unit_route_plan)) {
      client_unit_route_plan_destroy(unit_route_plan);
      v2_error(fields[1], "STALE_SLOT",
               "goto-and-perform permission changed before dispatch");
      return;
    }
  }

  if (action->kind == AGENT_V2_ACTION_CITY_SET_RALLY) {
    struct tile *rally_target = v2_tile_index_valid(action->target_tile)
                                ? index_to_tile(
                                    &wld.map, action->target_tile) : NULL;

    /* This is the single execution-time prepare/revalidation.  ACT_CAP has
     * already rebuilt and constant-time verified the target-bound slot; keep
     * this exact materialized plan alive and send the same packet below. */
    rally_plan = client_rally_plan_new(city, rally_target);
    if (rally_plan == NULL
        || !v2_rally_plan_matches_action(rally_plan, action)) {
      client_rally_plan_destroy(rally_plan);
      v2_error(fields[1], "STALE_SLOT",
               "city rally capability is no longer legal");
      return;
    }
    if (client_rally_plan_matches_city(
          rally_plan, city, requested_rally_persistent)) {
      client_rally_plan_destroy(rally_plan);
      v2_error(fields[1], "BAD_ARGUMENT",
               "city.set_rally cannot be a no-op");
      return;
    }
  }

  if (action->kind == AGENT_V2_ACTION_CITY_SET_GOVERNOR
      || action->kind == AGENT_V2_ACTION_CITY_CLEAR_GOVERNOR) {
    struct city *result_city;
    struct cm_parameter result_governor;
    bool postcondition_met;
    bool requests_advanced;
    int operation_id;

    if (v2_next_local_operation_id <= 0
        || v2_next_local_operation_id == INT_MAX) {
      v2_error(fields[1], "NONCE_EXHAUSTED",
               "local action correlation space exhausted");
      return;
    }
    operation_id = v2_next_local_operation_id++;
    memset(&v2_pending, 0, sizeof(v2_pending));
    v2_pending.active = TRUE;
    v2_pending.seat_epoch = v2_seat_epoch;
    v2_pending.revision = selected_revision;
    v2_pending.request_id = operation_id;
    fc_strlcpy(v2_pending.request, fields[1], sizeof(v2_pending.request));
    fc_strlcpy(v2_pending.slot, fields[2], sizeof(v2_pending.slot));
    v2_pending.action = *action;
    before_request = client.conn.client.last_request_id_used;

    /* CMA is a synchronous local client agent.  It can issue zero or many
     * server requests and waits for its own groups before returning, so the
     * ordinary one/two-packet callback protocol cannot describe it.  Keep
     * the stream boundary closed while CMA runs, then prove its exact local
     * state and emit the terminal receipt immediately. */
    v2_sendf("ACT_ACCEPTED\t%s\t%s\t%d\t%llu",
             v2_pending.request, v2_pending.slot, operation_id,
             (unsigned long long) v2_revision);
    if (action->kind == AGENT_V2_ACTION_CITY_SET_GOVERNOR) {
      cma_put_city_under_agent(city, &requested_governor);
    } else {
      cma_release_city(city);
    }
    requests_advanced =
      client.conn.client.last_request_id_used != before_request;

    if (v2_processing_idle()) {
      (void) v2_sync_seat_epoch();
      if (!v2_pending.active) {
        return;
      }
      (void) v2_refresh();
    }
    result_city = client_player() != NULL
                  ? player_city_by_number(client_player(), action->city_id)
                  : NULL;
    postcondition_met =
      result_city != NULL
      && result_city->client.lifecycle_id == action->city_lifecycle_id
      && v2_existing_incarnation(AGENT_V2_ENTITY_CITY, result_city->id)
         == action->city_incarnation;
    if (postcondition_met
        && action->kind == AGENT_V2_ACTION_CITY_SET_GOVERNOR) {
      postcondition_met =
        cma_is_city_under_agent(result_city, &result_governor)
        && cm_are_parameter_equal(&result_governor, &requested_governor);
    } else if (postcondition_met) {
      postcondition_met = !cma_is_city_under_agent(result_city, NULL);
    }
    if (postcondition_met) {
      v2_action_result("applied", "POSTCONDITION_VERIFIED");
    } else if (requests_advanced) {
      v2_action_result("rejected", "PROCESSING_BOUNDARY_MISMATCH");
    } else {
      v2_action_result("rejected", "POSTCONDITION_NOT_MET");
    }
    return;
  }

  memset(&v2_pending, 0, sizeof(v2_pending));
  v2_pending.active = TRUE;
  v2_pending.nonce = v2_next_action_nonce++;
  v2_pending.seat_epoch = v2_seat_epoch;
  v2_pending.revision = selected_revision;
  if (v2_pending.nonce == 0 || v2_next_action_nonce == 0) {
    v2_pending_clear();
    v2_error(fields[1], "NONCE_EXHAUSTED", "action nonce space exhausted");
    return;
  }
  fc_strlcpy(v2_pending.request, fields[1], sizeof(v2_pending.request));
  fc_strlcpy(v2_pending.slot, fields[2], sizeof(v2_pending.slot));
  v2_pending.action = *action;
  v2_pending.desired_chat_allied = requested_chat_allied;
  fc_strlcpy(v2_pending.desired_chat_message, requested_chat_message,
             sizeof(v2_pending.desired_chat_message));
  v2_pending.desired_pregame_nation = requested_pregame_nation;
  v2_pending.desired_pregame_style = requested_pregame_style;
  v2_pending.desired_pregame_team = requested_pregame_team;
  v2_pending.desired_pregame_male = requested_pregame_male;
  v2_pending.desired_pregame_ready = requested_pregame_ready;
  v2_pending.desired_client_vote = requested_vote;
  fc_strlcpy(v2_pending.desired_pregame_leader,
             requested_pregame_leader,
             sizeof(v2_pending.desired_pregame_leader));
  if (action->kind == AGENT_V2_ACTION_PLAYER_PLACE_INFRA) {
    const struct terrain *terrain = tile_terrain(
      index_to_tile(&wld.map, action->target_tile));

    v2_pending.action.target_extra = extra_number(extra);
    v2_pending.action.infrastructure_cost = extra->infracost;
    v2_pending.action.infrastructure_turns = extra->build_time > 0
      ? extra->build_time : terrain->placing_time * extra->build_time_factor;
  }
  if (requested_gold >= 0) {
    v2_pending.action.clause_value = requested_gold;
  }
  v2_pending.rally_plan = rally_plan;
  v2_pending.unit_route_plan = unit_route_plan;
  if (action->kind == AGENT_V2_ACTION_FOUND_CITY) {
    fc_strlcpy(v2_pending.city_name,
               fields[3] + strlen("city_name="),
               sizeof(v2_pending.city_name));
  }
  if (action->kind == AGENT_V2_ACTION_CITY_RENAME) {
    fc_strlcpy(v2_pending.city_name,
               fields[3] + strlen("city_name="),
               sizeof(v2_pending.city_name));
  }
  if (action->kind == AGENT_V2_ACTION_CITY_SET_WORKLIST) {
    worklist_copy(&v2_pending.desired_worklist, &requested_worklist);
  }
  if (action->kind == AGENT_V2_ACTION_CITY_SET_OPTIONS) {
    v2_pending.desired_city_options = requested_city_options;
    v2_pending.desired_wlcb = requested_wlcb;
  }
  if (action->kind == AGENT_V2_ACTION_ECONOMY_RATES) {
    if (!fc_agent_v2_parse_rates(
          fields[3], game.info.changable_tax, action->max_rate,
          &v2_pending.desired_tax, &v2_pending.desired_luxury,
          &v2_pending.desired_science)) {
      v2_pending_clear();
      v2_error(fields[1], "BAD_ARGUMENT", "rates changed during validation");
      return;
    }
  }
  if (action->kind == AGENT_V2_ACTION_CITY_PRODUCTION
      || action->kind == AGENT_V2_ACTION_CITY_BUY) {
    v2_pending.desired_production = production;
  }
  if (action->kind == AGENT_V2_ACTION_CITY_SELL_IMPROVEMENT) {
    v2_pending.desired_improvement = production.value.building;
  }
  if (action->kind == AGENT_V2_ACTION_CITY_SET_RALLY) {
    v2_pending.desired_rally_active = TRUE;
    v2_pending.desired_rally_persistent = requested_rally_persistent;
    v2_pending.desired_rally_order_count = action->rally_order_count;
    v2_pending.desired_rally_orders_digest = action->rally_orders_digest;
  } else if (action->kind == AGENT_V2_ACTION_CITY_CLEAR_RALLY) {
    v2_pending.desired_rally_active = FALSE;
    v2_pending.desired_rally_persistent = FALSE;
    v2_pending.desired_rally_order_count = 0;
    v2_pending.desired_rally_orders_digest = 0;
  }
  if (action->kind == AGENT_V2_ACTION_CITY_REQUEST_WORKER_TASK
      || action->kind == AGENT_V2_ACTION_CITY_CHANGE_WORKER_TASK
      || action->kind == AGENT_V2_ACTION_CITY_REMOVE_WORKER_TASK) {
    v2_pending.desired_worker_task_present =
      action->kind != AGENT_V2_ACTION_CITY_REMOVE_WORKER_TASK;
    v2_pending.desired_activity = action->target_activity;
    v2_pending.desired_extra = action->target_extra;
    v2_pending.desired_worker_task_want =
      v2_pending.desired_worker_task_present
      ? AGENT_V2_CITY_WORKER_TASK_WANT : 0;
  }
  if (action->kind == AGENT_V2_ACTION_UNIT_CANCEL_ORDERS
      || action->kind == AGENT_V2_ACTION_UNIT_GOTO
      || action->kind == AGENT_V2_ACTION_UNIT_GOTO_AND_PERFORM
      || action->kind == AGENT_V2_ACTION_UNIT_CONNECT_ROUTE
      || action->kind == AGENT_V2_ACTION_UNIT_SET_ROUTE) {
    v2_pending.requested_unit_source_tile = tile_index(unit_tile(unit));
  }
  if (action->kind == AGENT_V2_ACTION_UNIT_GOTO
      || action->kind == AGENT_V2_ACTION_UNIT_GOTO_AND_PERFORM
      || action->kind == AGENT_V2_ACTION_UNIT_CONNECT_ROUTE
      || action->kind == AGENT_V2_ACTION_UNIT_SET_ROUTE) {
    const struct client_unit_route_plan_info *info =
      client_unit_route_plan_get_info(v2_pending.unit_route_plan);

    if (info == NULL) {
      v2_pending_clear();
      v2_error(fields[1], "BAD_ARGUMENT", "unit route plan disappeared");
      return;
    }
    v2_pending.desired_route_destination_tile = info->destination_tile;
    v2_pending.desired_route_order_count = info->order_count;
    v2_pending.desired_route_orders_digest = info->orders_digest;
    v2_pending.desired_route_repeat = info->repeat;
    v2_pending.desired_route_vigilant = info->vigilant;
  }
  if (action->kind == AGENT_V2_ACTION_WORKER_START
      || action->kind == AGENT_V2_ACTION_CANCEL_ACTIVITY
      || action->kind == AGENT_V2_ACTION_UNIT_SENTRY
      || action->kind == AGENT_V2_ACTION_UNIT_FORTIFY
      || action->kind == AGENT_V2_ACTION_UNIT_CONVERT) {
    v2_pending.desired_activity = action->target_activity;
    v2_pending.desired_extra = action->target_extra;
  }
  if (action->kind == AGENT_V2_ACTION_UNIT_CONVERT
      || action->kind == AGENT_V2_ACTION_UNIT_UPGRADE) {
    v2_pending.desired_unit_type = action->target_build_id;
  }
  if (action->kind == AGENT_V2_ACTION_UNIT_AUTO_WORK) {
    v2_pending.desired_ssa = SSA_AUTOWORKER;
  } else if (action->kind == AGENT_V2_ACTION_UNIT_AUTO_EXPLORE) {
    v2_pending.desired_ssa = SSA_AUTOEXPLORE;
  } else if (action->kind == AGENT_V2_ACTION_UNIT_CANCEL_AUTOMATION) {
    v2_pending.desired_ssa = SSA_NONE;
    v2_pending.desired_activity = ACTIVITY_IDLE;
    v2_pending.desired_extra = EXTRA_NONE;
  }
  if (action->kind == AGENT_V2_ACTION_GOVERNMENT_REVOLUTION
      || action->kind == AGENT_V2_ACTION_GOVERNMENT_CHANGE) {
    v2_pending.desired_government = action->target_government;
  }
  if (action->kind == AGENT_V2_ACTION_SPACESHIP_PLACE) {
    v2_pending.desired_spaceship_part = action->spaceship_part;
    v2_pending.desired_spaceship_value = action->spaceship_value;
  }
  if (action->kind == AGENT_V2_ACTION_MULTIPLIER_SET) {
    v2_pending.desired_multiplier = action->target_multiplier;
    v2_pending.desired_multiplier_value = action->multiplier_value;
  }

  if (action->kind == AGENT_V2_ACTION_PLAYER_PLACE_INFRA
      && !v2_infrastructure_choice_still_legal(self, action, extra)) {
    v2_pending_clear();
    v2_error(fields[1], "STALE_SLOT",
             "infrastructure capability changed before send");
    return;
  }
  if (action->kind >= AGENT_V2_ACTION_DIPLOMACY_OPEN_MEETING
      && action->kind
         <= AGENT_V2_ACTION_DIPLOMACY_WITHDRAW_SHARED_TILES
      && !v2_relation_action_still_legal(
           action, requested_gold)) {
    v2_pending_clear();
    v2_error(fields[1], "STALE_SLOT",
             "diplomacy capability changed before send");
    return;
  }
  if (action->kind == AGENT_V2_ACTION_PREGAME_SET_TEAM
      || action->kind == AGENT_V2_ACTION_PLAYER_CAST_VOTE) {
    const struct agent_v2_action *current_action;

    if (!v2_refresh() || v2_revision != selected_revision) {
      v2_pending_clear();
      v2_error(fields[1], "STALE_REVISION",
               "action revision changed before send");
      return;
    }
    current_action = v2_action_by_slot(fields[2]);
    if (current_action == NULL || !v2_action_equal(current_action, action)
        || (action->kind == AGENT_V2_ACTION_PREGAME_SET_TEAM
            && !v2_pregame_team_choice_still_allowed(
                 self, requested_pregame_team))
        || (vote_action && !v2_vote_action_still_legal(self, action))) {
      v2_pending_clear();
      v2_error(fields[1], "STALE_SLOT",
               "action binding changed before send");
      return;
    }
  }
  if ((action->kind == AGENT_V2_ACTION_CITY_REQUEST_WORKER_TASK
       || action->kind == AGENT_V2_ACTION_CITY_CHANGE_WORKER_TASK
       || action->kind == AGENT_V2_ACTION_CITY_REMOVE_WORKER_TASK)
      && !v2_city_action_still_legal(self, city, action, &production)) {
    v2_pending_clear();
    v2_error(fields[1], "STALE_SLOT",
             "city worker task capability changed before send");
    return;
  }
  before_request = client.conn.client.last_request_id_used;
  switch (action->kind) {
  case AGENT_V2_ACTION_PREGAME_CONFIGURE:
    dsend_packet_nation_select_req(
      &client.conn, player_number(self),
      v2_pending.desired_pregame_nation,
      v2_pending.desired_pregame_male,
      v2_pending.desired_pregame_leader,
      v2_pending.desired_pregame_style);
    break;
  case AGENT_V2_ACTION_PREGAME_SET_TEAM:
    send_chat_printf("/team \"%s\" %d", player_name(self),
                     v2_pending.desired_pregame_team);
    break;
  case AGENT_V2_ACTION_PREGAME_SET_READY:
    dsend_packet_player_ready(&client.conn, player_number(self),
                              v2_pending.desired_pregame_ready);
    break;
  case AGENT_V2_ACTION_PLAYER_CAST_VOTE:
    voteinfo_do_vote(action->vote_no, v2_pending.desired_client_vote);
    break;
  case AGENT_V2_ACTION_PHASE_END:
    send_turn_done();
    break;
  case AGENT_V2_ACTION_MOVE:
  case AGENT_V2_ACTION_ATTACK:
    request_do_action(action->action, action->unit_id,
                      action->target_tile, 0, "");
    break;
  case AGENT_V2_ACTION_FOUND_CITY:
    request_do_action(ACTION_FOUND_CITY, action->unit_id,
                      action->target_tile, 0, v2_pending.city_name);
    break;
  case AGENT_V2_ACTION_RESEARCH_TARGET:
    dsend_packet_player_research(&client.conn, action->target_tech);
    break;
  case AGENT_V2_ACTION_RESEARCH_GOAL:
    dsend_packet_player_tech_goal(&client.conn, action->target_tech);
    break;
  case AGENT_V2_ACTION_ECONOMY_RATES:
    dsend_packet_player_rates(&client.conn,
                              v2_pending.desired_tax,
                              v2_pending.desired_luxury,
                              v2_pending.desired_science);
    break;
  case AGENT_V2_ACTION_PLAYER_SEND_CHAT:
    if (v2_pending.desired_chat_allied) {
      char outgoing[FC_AGENT_V2_MAX_CHAT_MESSAGE_BYTES + 2];

      outgoing[0] = CHAT_ALLIES_PREFIX;
      fc_strlcpy(outgoing + 1, v2_pending.desired_chat_message,
                 sizeof(outgoing) - 1);
      send_chat(outgoing);
    } else {
      send_chat(v2_pending.desired_chat_message);
    }
    break;
  case AGENT_V2_ACTION_CITY_PRODUCTION:
    city_change_production(city, &v2_pending.desired_production);
    break;
  case AGENT_V2_ACTION_CITY_BUY:
    city_buy_production(city);
    break;
  case AGENT_V2_ACTION_CITY_WORK_TILE:
    dsend_packet_city_make_worker(&client.conn, city->id,
                                  action->target_tile);
    break;
  case AGENT_V2_ACTION_CITY_UNWORK_TILE:
    dsend_packet_city_make_specialist(&client.conn, city->id,
                                      action->target_tile);
    break;
  case AGENT_V2_ACTION_CITY_SET_SPECIALIST:
    dsend_packet_city_change_specialist(
      &client.conn, city->id, action->source_specialist,
      action->target_specialist);
    break;
  case AGENT_V2_ACTION_CITY_SET_WORKLIST:
    city_set_worklist(city, &v2_pending.desired_worklist);
    break;
  case AGENT_V2_ACTION_CITY_SET_OPTIONS:
    dsend_packet_city_options_req(
      &client.conn, city->id, v2_pending.desired_city_options,
      v2_pending.desired_wlcb);
    break;
  case AGENT_V2_ACTION_CITY_RENAME:
    city_rename(city, v2_pending.city_name);
    break;
  case AGENT_V2_ACTION_CITY_SELL_IMPROVEMENT:
    city_sell_improvement(
      city, improvement_number(v2_pending.desired_improvement));
    break;
  case AGENT_V2_ACTION_CITY_SET_RALLY:
    (void) client_rally_plan_send(
      v2_pending.rally_plan, v2_pending.desired_rally_persistent);
    break;
  case AGENT_V2_ACTION_CITY_CLEAR_RALLY:
    (void) client_rally_point_clear_forced(city);
    break;
  case AGENT_V2_ACTION_CITY_SET_GOVERNOR:
  case AGENT_V2_ACTION_CITY_CLEAR_GOVERNOR:
    /* Synchronous local-agent actions return before this switch. */
    break;
  case AGENT_V2_ACTION_CITY_REQUEST_WORKER_TASK:
  case AGENT_V2_ACTION_CITY_CHANGE_WORKER_TASK:
  case AGENT_V2_ACTION_CITY_REMOVE_WORKER_TASK: {
    struct packet_worker_task task = {
      .city_id = city->id,
      .tile_id = action->target_tile,
      .activity = v2_pending.desired_worker_task_present
                  ? v2_pending.desired_activity : ACTIVITY_LAST,
      .tgt = v2_pending.desired_worker_task_present
             ? v2_pending.desired_extra : EXTRA_NONE,
      .want = v2_pending.desired_worker_task_want
    };

    send_packet_worker_task(&client.conn, &task);
    break;
  }
  case AGENT_V2_ACTION_WORKER_START:
    request_do_action(action->action, action->unit_id,
                      tile_index(unit_tile(unit)),
                      extra != NULL ? extra_index(extra) : NO_TARGET, "");
    break;
  case AGENT_V2_ACTION_CANCEL_ACTIVITY:
    request_new_unit_activity(unit, ACTIVITY_IDLE);
    break;
  case AGENT_V2_ACTION_UNIT_SENTRY:
    request_new_unit_activity(unit, ACTIVITY_SENTRY);
    break;
  case AGENT_V2_ACTION_UNIT_FORTIFY:
  case AGENT_V2_ACTION_UNIT_CONVERT:
  case AGENT_V2_ACTION_UNIT_DISBAND:
  case AGENT_V2_ACTION_UNIT_HOMELESS:
    request_do_action(action->action, action->unit_id,
                      action->unit_id, 0, "");
    break;
  case AGENT_V2_ACTION_UNIT_UPGRADE:
  case AGENT_V2_ACTION_UNIT_REHOME:
  case AGENT_V2_ACTION_UNIT_JOIN_CITY:
  case AGENT_V2_ACTION_UNIT_ESTABLISH_TRADE:
  case AGENT_V2_ACTION_UNIT_MARKETPLACE:
  case AGENT_V2_ACTION_UNIT_HELP_WONDER:
  case AGENT_V2_ACTION_UNIT_DISBAND_RECOVER:
    request_do_action(action->action, action->unit_id,
                      destination_city->id, 0, "");
    break;
  case AGENT_V2_ACTION_UNIT_AIRLIFT:
    request_do_action(action->action, action->unit_id,
                      action->destination_city_id, 0, "");
    break;
  case AGENT_V2_ACTION_UNIT_PARADROP:
  case AGENT_V2_ACTION_UNIT_TELEPORT:
    request_do_action(action->action, action->unit_id,
                      action->target_tile, 0, "");
    break;
  case AGENT_V2_ACTION_TRANSPORT_BOARD:
  case AGENT_V2_ACTION_TRANSPORT_DEBOARD:
  case AGENT_V2_ACTION_TRANSPORT_EMBARK:
  case AGENT_V2_ACTION_TRANSPORT_LOAD:
  case AGENT_V2_ACTION_TRANSPORT_UNLOAD:
    request_do_action(action->action, action->unit_id,
                      target_unit->id, 0, "");
    break;
  case AGENT_V2_ACTION_TRANSPORT_DISEMBARK:
    request_do_action(action->action, action->unit_id,
                      action->target_tile, 0, "");
    break;
  case AGENT_V2_ACTION_UNIT_AUTO_WORK:
    request_unit_ssa_set(unit, SSA_AUTOWORKER);
    break;
  case AGENT_V2_ACTION_UNIT_AUTO_EXPLORE:
    request_unit_ssa_set(unit, SSA_AUTOEXPLORE);
    break;
  case AGENT_V2_ACTION_UNIT_CANCEL_AUTOMATION:
    request_new_unit_activity(unit, ACTIVITY_IDLE);
    break;
  case AGENT_V2_ACTION_UNIT_CANCEL_ORDERS:
    request_orders_cleared(unit);
    break;
  case AGENT_V2_ACTION_UNIT_GOTO:
  case AGENT_V2_ACTION_UNIT_GOTO_AND_PERFORM:
  case AGENT_V2_ACTION_UNIT_CONNECT_ROUTE:
  case AGENT_V2_ACTION_UNIT_SET_ROUTE:
    (void) client_unit_route_plan_send(v2_pending.unit_route_plan);
    break;
  case AGENT_V2_ACTION_UNIT_SPECIAL:
    if (v2_paid_special_action(action_by_number(action->action))) {
      char maximum_cost[MAX_LEN_NAME];

      fc_snprintf(maximum_cost, sizeof(maximum_cost),
                  "agent-v2-max-cost:%d", action->gold_cost);
      request_do_action(action->action, action->unit_id,
                        special_target, special_subtarget, maximum_cost);
    } else {
      request_do_action(action->action, action->unit_id,
                        special_target, special_subtarget, "");
    }
    break;
  case AGENT_V2_ACTION_PLAYER_PLACE_INFRA:
    dsend_packet_player_place_infra(
      &client.conn, action->target_tile, extra_number(extra));
    break;
  case AGENT_V2_ACTION_GOVERNMENT_REVOLUTION:
    start_revolution();
    break;
  case AGENT_V2_ACTION_GOVERNMENT_CHANGE:
    set_government_choice(government);
    break;
  case AGENT_V2_ACTION_MULTIPLIER_SET: {
    struct packet_player_multiplier packet;
    int multiplier_id;

    memset(&packet, 0, sizeof(packet));
    packet.count = multiplier_count();
    for (multiplier_id = 0; multiplier_id < packet.count;
         multiplier_id++) {
      struct multiplier *pmul = multiplier_by_number(multiplier_id);

      packet.multipliers[multiplier_id] =
        player_multiplier_target_value(self, pmul);
    }
    packet.multipliers[v2_pending.desired_multiplier] =
      v2_pending.desired_multiplier_value;
    send_packet_player_multiplier(&client.conn, &packet);
    break;
  }
  case AGENT_V2_ACTION_SPACESHIP_PLACE:
    dsend_packet_spaceship_place(
      &client.conn,
      (enum spaceship_place_type) v2_pending.desired_spaceship_part,
      v2_pending.desired_spaceship_value);
    break;
  case AGENT_V2_ACTION_SPACESHIP_LAUNCH:
    send_packet_spaceship_launch(&client.conn);
    break;
  case AGENT_V2_ACTION_DIPLOMACY_OPEN_MEETING:
    dsend_packet_diplomacy_init_meeting_req(
      &client.conn, action->counterpart_id);
    break;
  case AGENT_V2_ACTION_DIPLOMACY_CLOSE_MEETING:
    dsend_packet_diplomacy_cancel_meeting_req(
      &client.conn, action->counterpart_id);
    break;
  case AGENT_V2_ACTION_DIPLOMACY_PROPOSE_CLAUSE:
    dsend_packet_diplomacy_create_clause_req(
      &client.conn, action->counterpart_id, action->clause_giver_id,
      action->clause_type, v2_pending.action.clause_value);
    break;
  case AGENT_V2_ACTION_DIPLOMACY_REMOVE_CLAUSE:
    dsend_packet_diplomacy_remove_clause_req(
      &client.conn, action->counterpart_id, action->clause_giver_id,
      action->clause_type, action->clause_value);
    break;
  case AGENT_V2_ACTION_DIPLOMACY_ACCEPT:
  case AGENT_V2_ACTION_DIPLOMACY_WITHDRAW_ACCEPTANCE:
    dsend_packet_diplomacy_accept_treaty_req(
      &client.conn, action->counterpart_id);
    break;
  case AGENT_V2_ACTION_DIPLOMACY_BREAK_RELATION:
    dsend_packet_diplomacy_cancel_pact(
      &client.conn, action->counterpart_id, CLAUSE_CEASEFIRE);
    break;
  case AGENT_V2_ACTION_DIPLOMACY_WITHDRAW_VISION:
    dsend_packet_diplomacy_cancel_pact(
      &client.conn, action->counterpart_id, CLAUSE_VISION);
    break;
  case AGENT_V2_ACTION_DIPLOMACY_WITHDRAW_SHARED_TILES:
    dsend_packet_diplomacy_cancel_pact(
      &client.conn, action->counterpart_id, CLAUSE_SHARED_TILES);
    break;
  case AGENT_V2_ACTION_KIND_COUNT:
    break;
  }
  if (client.conn.client.last_request_id_used == before_request) {
    v2_pending_clear();
    v2_error(fields[1], "NOT_SENT", "normal client API sent no request");
    return;
  }

  v2_pending.first_request_id = get_next_request_id(before_request);
  v2_pending.request_id = client.conn.client.last_request_id_used;
  v2_pending.request_count =
    fc_agent_v2_expected_request_count(action->kind);
  if (v2_pending.request_count == 0) {
    /* Requests may already have escaped.  This is therefore ambiguous, not a
     * safe pre-send validation failure. */
    v2_pending.terminal =
      FC_AGENT_V2_TERMINAL_PROCESSING_BOUNDARY_MISMATCH;
  }
  if (!fc_agent_v2_request_group_exact(
        before_request, v2_pending.first_request_id,
        v2_pending.request_id, v2_pending.request_count)) {
    /* At least one request escaped. Never report a pre-send error once a
     * partial native group may have applied. */
    v2_pending.terminal =
      FC_AGENT_V2_TERMINAL_PROCESSING_BOUNDARY_MISMATCH;
  }
  if (v2_pending.terminal == FC_AGENT_V2_TERMINAL_NONE) {
    struct agent_v2_callback_token *started = fc_malloc(sizeof(*started));
    struct agent_v2_callback_token *finished = fc_malloc(sizeof(*finished));

    started->nonce = v2_pending.nonce;
    started->request_id = v2_pending.first_request_id;
    finished->nonce = v2_pending.nonce;
    finished->request_id = v2_pending.request_id;
    v2_pending.started_token = started;
    v2_pending.finished_token = finished;
    update_queue_connect_processing_started_direct_full(
      v2_pending.first_request_id, v2_action_processing_started,
      started, free);
    update_queue_connect_processing_finished_direct_full(
      v2_pending.request_id, v2_action_processing_finished, finished, free);
    if (v2_pending.request_count == 2) {
      struct agent_v2_callback_token *first_finished =
        fc_malloc(sizeof(*first_finished));
      struct agent_v2_callback_token *last_started =
        fc_malloc(sizeof(*last_started));

      first_finished->nonce = v2_pending.nonce;
      first_finished->request_id = v2_pending.first_request_id;
      last_started->nonce = v2_pending.nonce;
      last_started->request_id = v2_pending.request_id;
      v2_pending.first_finished_token = first_finished;
      v2_pending.last_started_token = last_started;
      update_queue_connect_processing_finished_direct_full(
        v2_pending.first_request_id,
        v2_action_first_processing_finished, first_finished, free);
      update_queue_connect_processing_started_direct_full(
        v2_pending.request_id, v2_action_last_processing_started,
        last_started, free);
    }
  }
  v2_pending.timer = timer_new(TIMER_USER, TIMER_ACTIVE,
                               "agent protocol 2 action");
  timer_start(v2_pending.timer);
  v2_sendf("ACT_ACCEPTED\t%s\t%s\t%d\t%llu",
           v2_pending.request, v2_pending.slot, v2_pending.request_id,
           (unsigned long long) v2_revision);
}

static void v2_handle_action(char **fields)
{
  const struct agent_v2_action *action;

  if (!v2_refresh()) {
    v2_error(fields[1], "OBS_TOO_LARGE", "state registry is unavailable");
    return;
  }
  action = v2_action_by_slot(fields[2]);
  if (action == NULL) {
    v2_error(fields[1], "STALE_SLOT", "action slot is not current");
    return;
  }
  v2_execute_action(fields, action);
}

static bool v2_special_uses_cached_target_revalidation(
  const struct agent_v2_action *action)
{
  const struct action *native;
  struct tile *target;

  if (action == NULL || action->kind != AGENT_V2_ACTION_UNIT_SPECIAL
      || (native = action_by_number(action->action)) == NULL
      || native->result != ACTRES_PARADROP_CONQUER
      || !v2_tile_index_valid(action->target_tile)
      || (target = index_to_tile(&wld.map, action->target_tile)) == NULL) {
    return FALSE;
  }
  return client_tile_get_known(target) == TILE_KNOWN_UNSEEN;
}

static void v2_handle_cap_action(char **fields)
{
  uint64_t expected_revision;
  uint32_t target_selector;
  size_t action_count = 0;
  size_t matches = 0;
  size_t i;
  bool overflow = FALSE;
  struct agent_v2_action selected;
  char *normalized[4] = { "ACT", fields[1], fields[4], fields[5] };

  (void) v2_parse_revision(fields[2], &expected_revision);
  if (!v2_refresh()) {
    v2_error(fields[1], "OBS_TOO_LARGE", "state registry is unavailable");
    return;
  }
  if (expected_revision != v2_revision) {
    v2_error(fields[1], "STALE_REVISION", "action revision is not current");
    return;
  }
  if (fc_agent_v2_parse_target_slot(fields[4], &target_selector)) {
    size_t scope_index;

    if (target_selector > INT_MAX
        || !v2_tile_index_valid((int) target_selector)) {
      v2_error(fields[1], "STALE_SLOT", "target action is not current");
      return;
    }
    for (scope_index = 0; scope_index < AGENT_V2_SCOPE_PINNED;
         scope_index++) {
      struct agent_v2_target_scope *scope =
        &v2_target_scopes[scope_index];
      size_t action_index;

      if (!scope->valid || scope->revision != expected_revision
          || scope->target_tile != (int) target_selector
          || strcmp(scope->actor_ref, fields[3]) != 0) {
        continue;
      }
      for (action_index = 0; action_index < scope->action_count;
           action_index++) {
        if (strcmp(scope->actions[action_index].slot, fields[4]) == 0) {
          selected = scope->actions[action_index];
          matches++;
        }
      }
    }
    if (matches != 1
        || !fc_agent_v2_target_slot_matches(selected.slot, fields[4])) {
      v2_error(fields[1], "STALE_SLOT", "target action is not current");
      return;
    }
    if (selected.kind == AGENT_V2_ACTION_UNIT_SPECIAL) {
      if (v2_special_uses_cached_target_revalidation(&selected)) {
        v2_execute_action(normalized, &selected);
      } else {
        v2_begin_special_revalidation(normalized, &selected);
      }
      return;
    }
    v2_execute_action(normalized, &selected);
    return;
  }
  if (!v2_build_actor_scope(fields[3], v2_scope_actions,
                            &action_count, &overflow)) {
    v2_error(fields[1], "INVALID_ACTOR", "action actor is not current and owned");
    return;
  }
  if (overflow) {
    v2_error(fields[1], "SCOPE_TOO_LARGE", "actor scope exceeds bounded capacity");
    return;
  }
  for (i = 0; i < action_count; i++) {
    if (strcmp(v2_scope_actions[i].slot, fields[4]) == 0) {
      selected = v2_scope_actions[i];
      matches++;
    }
  }
  if (matches != 1) {
    v2_error(fields[1], "STALE_SLOT", "scoped action is not uniquely current");
    return;
  }
  if (selected.kind == AGENT_V2_ACTION_UNIT_SPECIAL) {
    if (v2_special_uses_cached_target_revalidation(&selected)) {
      v2_execute_action(normalized, &selected);
    } else {
      v2_begin_special_revalidation(normalized, &selected);
    }
    return;
  }
  v2_execute_action(normalized, &selected);
}

static void v2_handle_relation_cap_action(char **fields)
{
  uint64_t expected_revision;
  size_t action_count = 0;
  size_t matches = 0;
  size_t i;
  bool overflow = FALSE;
  struct agent_v2_action selected;
  char *normalized[4] = { "ACT", fields[1], fields[5], fields[6] };

  (void) v2_parse_revision(fields[2], &expected_revision);
  if (!v2_refresh()) {
    v2_error(fields[1], "OBS_TOO_LARGE", "state registry is unavailable");
    return;
  }
  if (expected_revision != v2_revision) {
    v2_error(fields[1], "STALE_REVISION", "action revision is not current");
    return;
  }
  if (!v2_build_relation_scope(
        fields[3], fields[4], v2_relation_scope_actions,
        &action_count, &overflow)) {
    v2_error(fields[1], "INVALID_RELATION",
             "action relation is not current and player-scoped");
    return;
  }
  if (overflow) {
    v2_error(fields[1], "SCOPE_TOO_LARGE",
             "relation scope exceeds bounded capacity");
    return;
  }
  for (i = 0; i < action_count; i++) {
    if (strcmp(v2_relation_scope_actions[i].slot, fields[5]) == 0) {
      selected = v2_relation_scope_actions[i];
      matches++;
    }
  }
  if (matches != 1) {
    v2_error(fields[1], "STALE_SLOT",
             "relation action is not uniquely current");
    return;
  }
  v2_execute_action(normalized, &selected);
}

void fc_agent_v2_init(fc_agent_v2_emit_fn emit,
                      fc_agent_v2_authorized_fn authorized)
{
  fc_assert(AGENT_V2_MANIFEST_COUNT == 42);
  fc_agent_v2_reset();
  v2_emit = emit;
  v2_authorized = authorized;
  v2_secret = ((uint64_t) fc_rand(1 << 30) << 34)
              ^ ((uint64_t) fc_rand(1 << 30) << 4)
              ^ (uint64_t) fc_rand(16);
  packhand_set_full_unit_info_observer(v2_full_unit_info_observer, NULL);
  packhand_set_unit_actions_observer(v2_unit_actions_observer, NULL);
  packhand_set_unit_action_answer_observer(
    v2_unit_action_answer_observer, NULL);
  packhand_set_city_sabotage_list_observer(
    v2_city_sabotage_list_observer, NULL);
  packhand_set_chat_msg_observer(v2_chat_msg_observer, NULL);
  packhand_set_unit_combat_info_observer(
    v2_unit_combat_info_observer, NULL);
  packhand_set_nuke_tile_info_observer(
    v2_nuke_tile_info_observer, NULL);
  packhand_set_investigation_observer(v2_investigation_observer, NULL);
  packhand_set_worker_task_observer(v2_worker_task_observer, NULL);
}

void fc_agent_v2_reset(void)
{
  packhand_set_full_unit_info_observer(NULL, NULL);
  packhand_set_unit_actions_observer(NULL, NULL);
  packhand_set_unit_action_answer_observer(NULL, NULL);
  packhand_set_city_sabotage_list_observer(NULL, NULL);
  packhand_set_chat_msg_observer(NULL, NULL);
  packhand_set_unit_combat_info_observer(NULL, NULL);
  packhand_set_nuke_tile_info_observer(NULL, NULL);
  packhand_set_investigation_observer(NULL, NULL);
  packhand_set_worker_task_observer(NULL, NULL);
  v2_pending_clear();
  v2_special_revalidation_clear();
  memset(v2_snapshots, 0, sizeof(v2_snapshots));
  memset(v2_scopes, 0, sizeof(v2_scopes));
  memset(v2_target_scopes, 0, sizeof(v2_target_scopes));
  v2_state_scopes_release_all();
  memset(v2_relation_scopes, 0, sizeof(v2_relation_scopes));
  memset(v2_relations, 0, sizeof(v2_relations));
  v2_emit = NULL;
  v2_authorized = NULL;
  v2_secret = 0;
  v2_revision = 0;
  v2_hash = 0;
  v2_notified_revision = 0;
  memset(&v2_phase_notice, 0, sizeof(v2_phase_notice));
  memset(&v2_current_phase, 0, sizeof(v2_current_phase));
  v2_have_current_phase = FALSE;
  v2_have_current = FALSE;
  v2_overflow = FALSE;
  v2_snapshot_serial = 0;
  v2_scope_serial = 0;
  memset(&v2_investigation, 0, sizeof(v2_investigation));
  v2_investigation_serial = 0;
  v2_target_query_desynchronized = FALSE;
  v2_special_revalidation_desynchronized = FALSE;
  v2_target_query_clear();
  v2_current_row_count = 0;
  v2_current_action_count = 0;
  v2_relation_count = 0;
  memset(v2_chat_history, 0, sizeof(v2_chat_history));
  v2_chat_history_start = 0;
  v2_chat_history_count = 0;
  v2_chat_sequence = 0;
  v2_seat_known = FALSE;
  v2_seat_authorized = FALSE;
  v2_seat_player = NULL;
  v2_seat_player_number = -1;
  v2_seat_map_tiles = NULL;
  v2_seat_map_xsize = 0;
  v2_seat_map_ysize = 0;
  v2_seat_map_topology = 0;
  v2_seat_map_wrap = 0;
  v2_seat_game_epoch = 0;
  v2_seat_client_state = C_S_INITIAL;
  v2_seat_epoch = 0;
  v2_next_action_nonce = 1;
  v2_next_local_operation_id = 1;
}

void fc_agent_v2_advertise(void)
{
  v2_sendf(FC_AGENT_V2_CAPS_FRAME);
}

void fc_agent_v2_tick(void)
{
  bool processing_idle;
  bool authorized;

  if (v2_emit == NULL) {
    return;
  }
  if (v2_target_query.active && !v2_target_query.emitting
      && v2_target_query.timer != NULL
      && timer_read_seconds(v2_target_query.timer)
         >= AGENT_V2_TARGET_QUERY_TIMEOUT) {
    v2_target_query_desynchronize(
      "server did not finish target action discovery");
  }
  if (v2_special_revalidation.active
      && !v2_special_revalidation.ready
      && v2_special_revalidation.timer != NULL
      && timer_read_seconds(v2_special_revalidation.timer)
         >= AGENT_V2_TARGET_QUERY_TIMEOUT) {
    v2_special_revalidation_desynchronize(
      "server did not finish action preflight");
  }
  processing_idle = v2_processing_idle();
  authorized = v2_seat_authorized;
  if (processing_idle) {
    /* When cache reads are safe, an epoch transition wins over an unresolved
     * timeout.  Already-latched FINISHED outcomes survive invalidation. */
    authorized = v2_sync_seat_epoch();
  }
  /* Terminal delivery and timeouts do not depend on observation-cache
   * coherence.  In particular, a missing FINISHED boundary still times out
   * while another packet group or an agent callback is busy. */
  v2_progress_pending();
  if (!fc_agent_v2_stream_notification_allowed(v2_pending.active)) {
    /* ACT_ACCEPTED..ACT_RESULT is one correlated stream boundary.  Do not
     * interleave unsolicited availability frames while the sidecar is
     * waiting for the terminal result; refresh and notification resume in
     * this same tick immediately after v2_action_result() clears pending. */
    return;
  }
  if (!processing_idle) {
    return;
  }
  if (!authorized || !v2_cache_coherent()) {
    return;
  }
  if (!v2_refresh()) {
    return;
  }
  if (v2_special_revalidation.active
      && v2_special_revalidation.ready) {
    char request[AGENT_V2_TOKEN_MAX + 1];
    char slot[32];
    struct agent_v2_action action = v2_special_revalidation.action;
    char *fields[4] = { "ACT", request, slot, "-" };

    fc_strlcpy(request, v2_special_revalidation.request, sizeof(request));
    fc_strlcpy(slot, v2_special_revalidation.slot, sizeof(slot));
    if (v2_special_revalidation.revision != v2_revision) {
      v2_error(request, "STALE_REVISION",
               "state changed after action preflight");
      v2_special_revalidation_clear();
      return;
    }
    v2_special_revalidation_clear();
    v2_execute_action(fields, &action);
    return;
  }
  if (v2_special_revalidation.active) {
    return;
  }
  if (v2_target_query.active && v2_target_query.emitting) {
    v2_target_query_emit_one();
    return;
  }
  if (v2_revision != v2_notified_revision) {
    if (v2_sendf("STATE_AVAILABLE\t%llu",
                 (unsigned long long) v2_revision)) {
      v2_notified_revision = v2_revision;
    }
  }
  if (v2_have_current_phase
      && fc_agent_v2_phase_notice_needed(
        &v2_phase_notice, v2_seat_epoch, v2_revision, &v2_current_phase)) {
    char frame[256];

    if (fc_agent_v2_format_phase_available(
          frame, sizeof(frame), v2_revision, &v2_current_phase)
        && v2_sendf("%s", frame)) {
      fc_agent_v2_phase_notice_record(
        &v2_phase_notice, v2_seat_epoch, v2_revision, &v2_current_phase);
    }
  }
}

bool fc_agent_v2_handle(const char *payload, size_t length)
{
  char frame[FC_AGENT_IPC_MAX_PAYLOAD + 1];
  char decoded[AGENT_V2_MAX_FIELDS][FC_AGENT_IPC_MAX_PAYLOAD + 1];
  char *encoded_fields[AGENT_V2_MAX_FIELDS];
  char *fields[AGENT_V2_MAX_FIELDS];
  size_t count = 0;
  size_t i;
  char *cursor;

  if (!((length >= strlen("OBS_OPEN")
         && memcmp(payload, "OBS_OPEN", strlen("OBS_OPEN")) == 0)
        || (length >= strlen("OBS_PAGE")
            && memcmp(payload, "OBS_PAGE", strlen("OBS_PAGE")) == 0)
        || (length >= strlen("SCOPE_OPEN")
            && memcmp(payload, "SCOPE_OPEN", strlen("SCOPE_OPEN")) == 0)
        || (length >= strlen("SCOPE_PAGE")
            && memcmp(payload, "SCOPE_PAGE", strlen("SCOPE_PAGE")) == 0)
        || (length >= strlen("STATE_SCOPE_OPEN")
            && memcmp(payload, "STATE_SCOPE_OPEN",
                      strlen("STATE_SCOPE_OPEN")) == 0)
        || (length >= strlen("STATE_SCOPE_PAGE")
            && memcmp(payload, "STATE_SCOPE_PAGE",
                      strlen("STATE_SCOPE_PAGE")) == 0)
        || (length >= strlen("RELATION_SCOPE_OPEN")
            && memcmp(payload, "RELATION_SCOPE_OPEN",
                      strlen("RELATION_SCOPE_OPEN")) == 0)
        || (length >= strlen("RELATION_SCOPE_PAGE")
            && memcmp(payload, "RELATION_SCOPE_PAGE",
                      strlen("RELATION_SCOPE_PAGE")) == 0)
        || (length >= strlen("TARGET_ACTION")
            && memcmp(payload, "TARGET_ACTION",
                      strlen("TARGET_ACTION")) == 0)
        || (length >= strlen("ACT")
            && memcmp(payload, "ACT", strlen("ACT")) == 0))) {
    return FALSE;
  }
  if (length > FC_AGENT_IPC_MAX_PAYLOAD) {
    v2_error(NULL, "BAD_REQUEST", "protocol 2 frame is too long");
    return TRUE;
  }
  memcpy(frame, payload, length);
  frame[length] = '\0';

  cursor = frame;
  while (count < AGENT_V2_MAX_FIELDS) {
    char *tab = strchr(cursor, '\t');

    encoded_fields[count++] = cursor;
    if (tab == NULL) {
      break;
    }
    *tab = '\0';
    cursor = tab + 1;
  }
  if (strchr(cursor, '\t') != NULL || count == AGENT_V2_MAX_FIELDS) {
    v2_error(NULL, "BAD_REQUEST", "too many protocol 2 fields");
    return TRUE;
  }
  fields[0] = encoded_fields[0];
  for (i = 1; i < count; i++) {
    if (!fc_agent_v2_percent_decode(encoded_fields[i], decoded[i],
                                    sizeof(decoded[i]))) {
      v2_error(i > 1 ? fields[1] : NULL, "BAD_ENCODING",
               "fields require strict uppercase percent encoding");
      return TRUE;
    }
    fields[i] = decoded[i];
  }

  if (strcmp(fields[0], "OBS_OPEN") == 0) {
    if (!v2_validate_open(fields, count)) {
      return TRUE;
    }
  } else if (strcmp(fields[0], "OBS_PAGE") == 0) {
    if (!v2_validate_page(fields, count)) {
      return TRUE;
    }
  } else if (strcmp(fields[0], "SCOPE_OPEN") == 0) {
    if (!v2_validate_scope_open(fields, count)) {
      return TRUE;
    }
  } else if (strcmp(fields[0], "SCOPE_PAGE") == 0) {
    if (!v2_validate_scope_page(fields, count)) {
      return TRUE;
    }
  } else if (strcmp(fields[0], "STATE_SCOPE_OPEN") == 0) {
    if (!v2_validate_state_scope_open(fields, count)) {
      return TRUE;
    }
  } else if (strcmp(fields[0], "STATE_SCOPE_PAGE") == 0) {
    if (!v2_validate_state_scope_page(fields, count)) {
      return TRUE;
    }
  } else if (strcmp(fields[0], "RELATION_SCOPE_OPEN") == 0) {
    if (!v2_validate_relation_scope_open(fields, count)) {
      return TRUE;
    }
  } else if (strcmp(fields[0], "RELATION_SCOPE_PAGE") == 0) {
    if (!v2_validate_relation_scope_page(fields, count)) {
      return TRUE;
    }
  } else if (strcmp(fields[0], "TARGET_ACTION") == 0) {
    if (!v2_validate_target_action(fields, count)) {
      return TRUE;
    }
  } else if (strcmp(fields[0], "ACT_CAP") == 0) {
    if (!v2_validate_cap_action(fields, count)) {
      return TRUE;
    }
  } else if (strcmp(fields[0], "ACT_RELATION_CAP") == 0) {
    if (!v2_validate_relation_cap_action(fields, count)) {
      return TRUE;
    }
  } else if (strcmp(fields[0], "ACT") == 0) {
    if (!v2_validate_action(fields, count)) {
      return TRUE;
    }
  } else {
    v2_error(count > 1 ? fields[1] : NULL,
             "BAD_REQUEST", "unknown protocol 2 command");
    return TRUE;
  }
  if (v2_special_revalidation.active) {
    v2_error(fields[1], "BUSY", "one action preflight is already pending");
    return TRUE;
  }
  if (!v2_processing_idle()) {
    v2_error(fields[1], "NOT_READY",
             "client cache is inside a processing group");
    return TRUE;
  }
  if (!v2_sync_seat_epoch()) {
    v2_error(fields[1], "NOT_READY",
             "exact target human seat is not authorized");
    return TRUE;
  }
  if (!v2_cache_coherent()) {
    v2_error(fields[1], "NOT_READY",
             "client cache is inside a processing group");
    return TRUE;
  }

  if (strcmp(fields[0], "OBS_OPEN") == 0) {
    v2_handle_open(fields);
  } else if (strcmp(fields[0], "OBS_PAGE") == 0) {
    v2_handle_page(fields);
  } else if (strcmp(fields[0], "SCOPE_OPEN") == 0) {
    v2_handle_scope_open(fields);
  } else if (strcmp(fields[0], "SCOPE_PAGE") == 0) {
    v2_handle_scope_page(fields);
  } else if (strcmp(fields[0], "STATE_SCOPE_OPEN") == 0) {
    v2_handle_state_scope_open(fields);
  } else if (strcmp(fields[0], "STATE_SCOPE_PAGE") == 0) {
    v2_handle_state_scope_page(fields);
  } else if (strcmp(fields[0], "RELATION_SCOPE_OPEN") == 0) {
    v2_handle_relation_scope_open(fields);
  } else if (strcmp(fields[0], "RELATION_SCOPE_PAGE") == 0) {
    v2_handle_relation_scope_page(fields);
  } else if (strcmp(fields[0], "TARGET_ACTION") == 0) {
    v2_handle_target_action(fields);
  } else if (strcmp(fields[0], "ACT_CAP") == 0) {
    v2_handle_cap_action(fields);
  } else if (strcmp(fields[0], "ACT_RELATION_CAP") == 0) {
    v2_handle_relation_cap_action(fields);
  } else {
    v2_handle_action(fields);
  }
  return TRUE;
}
