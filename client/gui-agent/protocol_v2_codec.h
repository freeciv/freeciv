/***********************************************************************
 Freeciv - Copyright (C) 1996 - The Freeciv Project

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2, or (at your option)
 any later version.
***********************************************************************/

#ifndef FC__AGENT_PROTOCOL_V2_CODEC_H
#define FC__AGENT_PROTOCOL_V2_CODEC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FC_AGENT_V2_MAX_ACTIONS 2048
#define FC_AGENT_V2_MAX_RELATION_ACTIONS 8192
#define FC_AGENT_V2_MAX_PINNED_RELATION_SCOPES 4
#define FC_AGENT_V2_MAX_PINNED_SCOPES 8
#define FC_AGENT_V2_MAX_PINNED_STATE_SCOPES 4
#define FC_AGENT_V2_PAGE_MAX 16
#define FC_AGENT_V2_MAX_GOVERNMENTS 127
#define FC_AGENT_V2_MAX_CITY_BUILD_CHOICES 1024
#define FC_AGENT_V2_MAX_CITY_WORKLIST 64
#define FC_AGENT_V2_MAX_RALLY_ORDERS 2000
#define FC_AGENT_V2_MAX_TARGET_ACTIONS 256
#define FC_AGENT_V2_MAX_UNIT_ROUTE_WAYPOINTS 64
#define FC_AGENT_V2_MAX_INFRA_CHOICES 250
#define FC_AGENT_V2_MAX_VOTES 256
#define FC_AGENT_V2_MAX_VOTE_HISTORY 64
#define FC_AGENT_V2_INFRA_CHOICES_TEXT 1024
#define FC_AGENT_V2_MAX_CHAT_HISTORY 64
#define FC_AGENT_V2_MAX_CHAT_MESSAGE_BYTES 512
#define FC_AGENT_V2_MAX_STATE_SCOPE_ROWS 40000
#define FC_AGENT_V2_MAX_STATE_SCOPE_BYTES (16 * 1024 * 1024)

/* Generated from agent_eval.v2_control's canonical private row/action
 * grammar.  Python tests pin this literal to that derivation so a projection
 * grammar change cannot silently keep accepting an older native client. */
#define FC_AGENT_V2_SCHEMA_ID \
  "sha256-3471520648d923f16fda4e1b58858301f343a64165b7e6cd2e3dd93af79cd3f4"
#define FC_AGENT_V2_CAPS_FRAME \
  "CAPS\t2\tACT,ACT_CAP,ACT_RELATION_CAP,OBS_OPEN,OBS_PAGE," \
  "PHASE_AVAILABLE,SCOPE_OPEN," \
  "SCOPE_PAGE,STATE_AVAILABLE,STATE_SCOPE_OPEN,STATE_SCOPE_PAGE," \
  "TARGET_ACTION,RELATION_SCOPE_OPEN," \
  "RELATION_SCOPE_PAGE" \
  "\tpercent-tab\t8192\t" FC_AGENT_V2_SCHEMA_ID

#define FC_AGENT_V2_FRAME_SCOPE_OPEN \
  "SCOPE_OPEN request expected_revision actor_ref"
#define FC_AGENT_V2_FRAME_SCOPE_OPENED \
  "SCOPE_OPENED request view revision actor_ref total complete overflow"
#define FC_AGENT_V2_FRAME_SCOPE_PAGE \
  "SCOPE_PAGE request view offset limit"
#define FC_AGENT_V2_FRAME_SCOPE_BEGIN \
  "SCOPE_BEGIN request view revision actor_ref offset count total"
#define FC_AGENT_V2_FRAME_SCOPE_ACTION \
  "SCOPE_ACTION request view index encoded_action_row"
#define FC_AGENT_V2_FRAME_SCOPE_END \
  "SCOPE_END request view next_offset"
#define FC_AGENT_V2_FRAME_STATE_SCOPE_OPEN \
  "STATE_SCOPE_OPEN request expected_revision section selector"
#define FC_AGENT_V2_FRAME_STATE_SCOPE_OPENED \
  "STATE_SCOPE_OPENED request view revision section selector total complete overflow"
#define FC_AGENT_V2_FRAME_STATE_SCOPE_PAGE \
  "STATE_SCOPE_PAGE request view offset limit"
#define FC_AGENT_V2_FRAME_STATE_SCOPE_BEGIN \
  "STATE_SCOPE_BEGIN request view revision section selector offset count total"
#define FC_AGENT_V2_FRAME_STATE_SCOPE_ROW \
  "STATE_SCOPE_ROW request view index encoded_state_row"
#define FC_AGENT_V2_FRAME_STATE_SCOPE_END \
  "STATE_SCOPE_END request view next_offset"
#define FC_AGENT_V2_FRAME_ACT_CAP \
  "ACT_CAP request expected_revision actor_ref slot arguments"
#define FC_AGENT_V2_FRAME_ACT_RELATION_CAP \
  "ACT_RELATION_CAP request expected_revision actor_ref counterpart_ref " \
  "slot arguments"
#define FC_AGENT_V2_FRAME_TARGET_ACTION \
  "TARGET_ACTION request expected_revision actor_ref native_tile"
#define FC_AGENT_V2_FRAME_TARGET_BEGIN \
  "TARGET_BEGIN request revision actor_ref native_tile count"
#define FC_AGENT_V2_FRAME_TARGET_ROW \
  "TARGET_ROW request index encoded_action_row"
#define FC_AGENT_V2_FRAME_TARGET_END \
  "TARGET_END request count"
#define FC_AGENT_V2_FRAME_RELATION_SCOPE_OPEN \
  "RELATION_SCOPE_OPEN request expected_revision actor_ref counterpart_ref"
#define FC_AGENT_V2_FRAME_RELATION_SCOPE_OPENED \
  "RELATION_SCOPE_OPENED request view revision actor_ref counterpart_ref " \
  "total complete overflow"
#define FC_AGENT_V2_FRAME_RELATION_SCOPE_PAGE \
  "RELATION_SCOPE_PAGE request view offset limit"
#define FC_AGENT_V2_FRAME_RELATION_SCOPE_BEGIN \
  "RELATION_SCOPE_BEGIN request view revision actor_ref counterpart_ref " \
  "offset count total"
#define FC_AGENT_V2_FRAME_RELATION_SCOPE_ACTION \
  "RELATION_SCOPE_ACTION request view index encoded_action_row"
#define FC_AGENT_V2_FRAME_RELATION_SCOPE_END \
  "RELATION_SCOPE_END request view next_offset"

/* Canonical ordered row grammars.  Native emitters consume these exact
 * formats, and the Python drift test extracts their keys before accepting the
 * schema ID above. */
#define FC_AGENT_V2_ROW_META \
  "meta state=%s turn=%d phase=%d cache=human-client " \
  "phase_mode=%s phase_count=%d active_phase=%d phase_ready=%d " \
  "map_width=%d map_height=%d topology=%s wrap_x=%d wrap_y=%d " \
  "known_tile_count=%d"
#define FC_AGENT_V2_ROW_PREGAME \
  "pregame ref=%s leader=%s nation=%s sex=%s style=%s ready=%d " \
  "nation_choices=%d style_choices=%d team_choices=%d"
#define FC_AGENT_V2_ROW_PREGAME_NATION \
  "pregame_nation id=%d name=%s default_style=%d"
#define FC_AGENT_V2_ROW_PREGAME_STYLE \
  "pregame_style id=%d name=%s"
#define FC_AGENT_V2_ROW_PREGAME_TEAM \
  "pregame_team id=%d name=%s selected=%d occupied=%d member_count=%d"
#define FC_AGENT_V2_ROW_PREGAME_TEAM_MEMBER \
  "pregame_team_member team=%d player=%s leader=%s"
#define FC_AGENT_V2_ROW_CHAT_RECIPIENT \
  "chat_recipient ref=%s name=%s self=%d connected=%d can_message=%d"
#define FC_AGENT_V2_ROW_VOTE \
  "vote vote_no=%d caller=%s description=%s yes=%d no=%d abstain=%d " \
  "num_voters=%d percent_required=%d team_only=%d current_vote=%s " \
  "can_vote=%d status=%s outcome_turn=%d outcome_phase=%d"
#define FC_AGENT_V2_ROW_PLAYER \
  "player ref=%s name=%s nation=%s government=%s gold=%d tax=%d " \
  "science=%d luxury=%d alive=%d phase_done=%d changeable_tax=%d " \
  "max_rate=%d infrastructure_enabled=%d infrastructure_points=%d"
#define FC_AGENT_V2_ROW_GOVERNANCE \
  "governance current_id=%d target_id=%d during_id=%d status=%s " \
  "finish_turn=%d turns_remaining=%d method=%s max_turns=%d " \
  "untargeted_allowed=%d no_anarchy=%d can_revolution=%d choices_count=%d"
#define FC_AGENT_V2_ROW_GOVERNMENT \
  "government id=%d name=%s current=%d target=%d during=%d can_change=%d"
#define FC_AGENT_V2_ROW_MULTIPLIER \
  "multiplier id=%d name=%s value=%d target=%d start=%d stop=%d step=%d " \
  "minimum_turns=%d changed_turn=%d can_change=%d choice_count=%llu"
#define FC_AGENT_V2_ROW_SPACESHIP \
  "spaceship state=%s structurals=%d structurals_placed=%d " \
  "components=%d fuel=%d propulsion=%d modules=%d habitation=%d " \
  "life_support=%d solar_panels=%d launch_year=%d population=%d mass=%d " \
  "support_permille=%d energy_permille=%d success_permille=%d " \
  "travel_time_millis=%d has_capital=%d can_launch=%d"
#define FC_AGENT_V2_ROW_SPACESHIP_STRUCTURAL \
  "spaceship_structural slot=%d x=%d y=%d required_slot=%d placed=%d " \
  "required_connected=%d can_place=%d"
#define FC_AGENT_V2_ROW_RESEARCH \
  "research techs=%d future=%d target=%s target_id=%d goal=%s " \
  "goal_id=%d bulbs=%d cost=%d output=%d choices_count=%d " \
  "choices_digest=%s"
#define FC_AGENT_V2_ROW_RESEARCH_TECH \
  "research_tech id=%d name=%s state=%s can_target=%d can_goal=%d"
#define FC_AGENT_V2_ROW_RESEARCH_GRAPH \
  "research_graph id=%d name=%s reachable=%d next_step=%d " \
  "unknown_prerequisites=%d path_cost=%d"
#define FC_AGENT_V2_ROW_RESEARCH_EDGE \
  "research_edge tech=%d prerequisite=%d kind=%s"
#define FC_AGENT_V2_ROW_RESEARCH_UNLOCK \
  "research_unlock tech=%d kind=%s native_id=%d name=%s scope=%s"
#define FC_AGENT_V2_ROW_DIPLOMACY \
  "diplomacy other=%s name=%s nation=%s state=%s contact=%d alive=%d " \
  "turns_left=%d can_meet=%d meeting=%d generation=%llu self_accepted=%d " \
  "other_accepted=%d clause_count=%d clauses_digest=%s " \
  "intel_level=%s team=%d team_name=%s same_team=%d controller=%s " \
  "connected=%d score=%d gold=%d government=%s " \
  "has_embassy=%d other_has_embassy=%d " \
  "gives_vision=%d receives_vision=%d gives_shared_tiles=%d " \
  "receives_shared_tiles=%d can_cancel=%d cancel_reason=%s"
#define FC_AGENT_V2_ROW_DIPLOMACY_INTEL \
  "diplomacy_intel other=%s tax=%d science=%d luxury=%d culture=%d " \
  "research_id=%d research_name=%s bulbs=%d cost=%d known_count=%d " \
  "known_digest=%s known_ids=%s"
#define FC_AGENT_V2_ROW_DIPLOMACY_CLAUSE \
  "diplomacy_clause other=%s generation=%llu position=%d giver=%s " \
  "type=%s value_kind=%s value=%d name=%s"
#define FC_AGENT_V2_ROW_TILE \
  "tile index=%d x=%d y=%d known=%d terrain=%s owner=%s " \
  "placing_extra=%d placing_extra_name=%s placing_turns=%d " \
  "placing_time=%d"
#define FC_AGENT_V2_ROW_TILE_LOCAL \
  "tile_local index=%d x=%d y=%d known=%d terrain=%s owner=%s " \
  "placing_extra=%d placing_extra_name=%s placing_turns=%d " \
  "placing_time=%d resource_extra=%d resource_name=%s has_label=%d " \
  "label=%s food=%d shields=%d trade=%d"
#define FC_AGENT_V2_ROW_TILE_EXTRA \
  "tile_extra tile=%d extra=%d name=%s cause_mask=%u"
#define FC_AGENT_V2_ROW_INFRASTRUCTURE_EXTRA \
  "infrastructure_extra id=%d name=%s cost=%d build_time=%d " \
  "build_time_factor=%d"
#define FC_AGENT_V2_ROW_CITY \
  "city ref=%s name=%s tile=%d x=%d y=%d size=%d food=%d shields=%d " \
  "trade=%d production_kind=%s production_id=%d production_name=%s " \
  "shield_stock=%d shield_cost=%d buy_cost=%d can_buy=%d can_change=%d " \
  "citizen_tile_count=%d specialist_type_count=%d worklist_length=%d " \
  "build_choice_count=%d improvement_count=%d trade_route_count=%d " \
  "trade_route_capacity=%u did_sell=%d " \
  "allow_disband=%d new_citizens=%s options_conflict=%d " \
  "airlift_remaining=%d airlift_max=%d governor_enabled=%d " \
  "citizen_happy=%d citizen_content=%d citizen_unhappy=%d " \
  "citizen_angry=%d citizen_workers=%d citizen_specialists=%d " \
  "food_stock=%d granary_size=%d growth_turns=%d pollution=%d " \
  "food_citizen_base=%d food_net=%d food_surplus=%d food_usage=%d " \
  "food_waste=%d food_unhappy_penalty=%d shield_citizen_base=%d " \
  "shield_net=%d shield_surplus=%d shield_usage=%d shield_waste=%d " \
  "shield_unhappy_penalty=%d trade_citizen_base=%d trade_net=%d " \
  "trade_surplus=%d trade_usage=%d trade_waste=%d " \
  "trade_unhappy_penalty=%d gold_citizen_base=%d gold_net=%d " \
  "gold_surplus=%d gold_usage=%d gold_waste=%d " \
  "gold_unhappy_penalty=%d luxury_citizen_base=%d luxury_net=%d " \
  "luxury_surplus=%d luxury_usage=%d luxury_waste=%d " \
  "luxury_unhappy_penalty=%d science_citizen_base=%d science_net=%d " \
  "science_surplus=%d science_usage=%d science_waste=%d " \
  "science_unhappy_penalty=%d"
#define FC_AGENT_V2_ROW_CITY_SITE \
  "city_site ref=%s owner=%s name=%s tile=%d x=%d y=%d size=%d " \
  "visibility=%s"
#define FC_AGENT_V2_ROW_CITY_TILE \
  "city_tile city=%s tile=%d worked=%d free_worked=%d can_work=%d " \
  "food=%d shields=%d trade=%d gold=%d luxury=%d science=%d"
#define FC_AGENT_V2_ROW_CITY_WORKER_TASK \
  "city_worker_task city=%s tile=%d activity=%s target_extra=%d " \
  "target_extra_name=%s want=%d"
#define FC_AGENT_V2_ROW_CITY_SPECIALIST \
  "city_specialist city=%s specialist=%d name=%s count=%d " \
  "counts_toward_population=%d can_use=%d is_default=%d food=%d " \
  "shields=%d trade=%d gold=%d luxury=%d science=%d"
#define FC_AGENT_V2_ROW_CITY_WORKLIST \
  "city_worklist city=%s position=%d production_kind=%s " \
  "production_id=%d production_name=%s"
#define FC_AGENT_V2_ROW_CITY_BUILD_CHOICE \
  "city_build_choice city=%s production_kind=%s production_id=%d " \
  "production_name=%s can_queue=%d can_build_now=%d shield_cost=%d " \
  "shield_stock_after_change=%d turns=%d turns_with_stock=%d " \
  "upkeep_food=%d upkeep_shield=%d upkeep_trade=%d upkeep_gold=%d " \
  "upkeep_luxury=%d upkeep_science=%d happy_cost=%d unit_attack=%d " \
  "unit_defense=%d unit_move_rate=%d unit_hp=%d unit_firepower=%d " \
  "unit_vision_radius_sq=%d unit_transport_capacity=%d unit_fuel=%d " \
  "unit_pop_cost=%d unit_bombard_rate=%d unit_city_size=%d " \
  "unit_paradrop_range=%d building_genus=%s building_obsolete=%d " \
  "building_redundant=%d building_convert=%d building_allows_units=%d " \
  "building_allows_extras=%d building_prevents_disaster=%d " \
  "building_protects_vs_actions=%d building_allows_actions=%d"
#define FC_AGENT_V2_ROW_CITY_IMPROVEMENT \
  "city_improvement city=%s improvement_id=%d name=%s sellable=%d " \
  "sell_price=%d"
#define FC_AGENT_V2_ROW_CITY_TRADE_ROUTE \
  "city_trade_route city=%s position=%d partner=%s " \
  "partner_visibility=%s partner_name=%s base_value=%d " \
  "effective_value=%d direction=%s goods_id=%d goods_name=%s"
#define FC_AGENT_V2_ROW_INVESTIGATION \
  "investigation city=%s lifecycle=%llu tile=%d name=%s size=%d " \
  "production_kind=%s production_id=%d production_name=%s " \
  "shield_stock=%d shield_surplus=%d improvement_count=%d " \
  "feeling_count=%d specialist_count=%d"
#define FC_AGENT_V2_ROW_INVESTIGATION_IMPROVEMENT \
  "investigation_improvement city=%s improvement_id=%d name=%s"
#define FC_AGENT_V2_ROW_INVESTIGATION_CITIZENS \
  "investigation_citizens city=%s stage=%d happy=%d content=%d " \
  "unhappy=%d angry=%d"
#define FC_AGENT_V2_ROW_INVESTIGATION_SPECIALIST \
  "investigation_specialist city=%s specialist=%d name=%s count=%d"
#define FC_AGENT_V2_ROW_CITY_RALLY \
  "city_rally city=%s active=%d persistent=%d vigilant=%d " \
  "order_count=%d orders_digest=%s"
#define FC_AGENT_V2_ROW_CITY_GOVERNOR \
  "city_governor city=%s min_food=%d min_production=%d min_trade=%d " \
  "min_gold=%d min_luxury=%d min_science=%d weight_food=%d " \
  "weight_production=%d weight_trade=%d weight_gold=%d weight_luxury=%d " \
  "weight_science=%d celebration_weight=%d require_happy=%d " \
  "maximize_growth=%d"
#define FC_AGENT_V2_ROW_UNIT_OWN \
  "unit ref=%s scope=own owner=%s type_id=%d type=%s home_city=%s " \
  "converts_to_id=%d converts_to=%s tile=%d x=%d y=%d hp=%d " \
  "veteran=%d veteran_name=%s veteran_levels=%d veteran_power=%d " \
  "veteran_move_bonus=%d fuel=%d max_hp=%d max_fuel=%d move_rate=%d " \
  "attack=%d defense=%d firepower=%d base_upkeep_food=%d " \
  "base_upkeep_shield=%d base_upkeep_trade=%d base_upkeep_gold=%d " \
  "base_upkeep_luxury=%d base_upkeep_science=%d upkeep_food=%d " \
  "upkeep_shield=%d upkeep_trade=%d upkeep_gold=%d upkeep_luxury=%d " \
  "upkeep_science=%d " \
  "moves=%d activity=%s activity_target=%d activity_target_name=%s " \
  "activity_progress=%d transport_state=%s transporter=%s " \
  "transport_capacity=%d occupied=%d paradropped=%d paradrop_range=%d " \
  "controller=%s has_orders=%d orders_repeat=%d orders_vigilant=%d " \
  "order_count=%d orders_digest=%s orders_destination=%d " \
  "action_decision_want=%s action_decision_tile=%d"
#define FC_AGENT_V2_ROW_UNIT_ROUTE \
  "unit_route unit=%s order_index=%d reconstructable=%d step_count=%d"
#define FC_AGENT_V2_ROW_UNIT_ROUTE_STEP \
  "unit_route_step unit=%s sequence=%d kind=%s tile=%d"
#define FC_AGENT_V2_ROW_UNIT_VISIBLE \
  "unit ref=%s scope=visible owner=%s type_id=%d type=%s tile=%d x=%d " \
  "y=%d hp=%d veteran=%d veteran_name=%s veteran_levels=%d " \
  "veteran_power=%d veteran_move_bonus=%d max_hp=%d max_fuel=%d " \
  "move_rate=%d attack=%d defense=%d firepower=%d base_upkeep_food=%d " \
  "base_upkeep_shield=%d base_upkeep_trade=%d base_upkeep_gold=%d " \
  "base_upkeep_luxury=%d base_upkeep_science=%d"
#define FC_AGENT_V2_ROW_TOMBSTONE \
  "tombstone ref=%c:%d:%llu kind=%s"
#define FC_AGENT_V2_ROW_CHAT \
  "chat sequence=%llu turn=%d phase=%d sender=%s sender_name=%s " \
  "self=%d channel=%s event=%s truncated=%d message=%s"
#define FC_AGENT_V2_ROW_ACTION \
  "action slot=%s kind=%s actor=%s counterpart=%s " \
  "meeting_generation=%llu clauses_digest=%s self_accepted=%d " \
  "other_accepted=%d relation_state=%s outgoing_vision=%d " \
  "outgoing_shared_tiles=%d clause_giver=%s clause_type=%s clause_value=%d " \
  "clause_name=%s desired_acceptance=%d target_tile=%d source_city=%s " \
  "destination_city=%s target_unit=%s " \
  "transport_context=%s target_tech=%d " \
  "vote_no=%d server_setting_id=%d server_setting_type=%s " \
  "server_setting_min=%d server_setting_max=%d server_setting_current=%d " \
  "server_setting_value=%d " \
  "target_government=%d max_rate=%d route_waypoint_limit=%d " \
  "infrastructure_cost=%d infrastructure_turns=%d " \
  "infrastructure_choice_count=%d infrastructure_choices=%s " \
  "target_build_kind=%s target_build=%d " \
  "spaceship_part=%s spaceship_value=%d " \
  "target_multiplier=%d multiplier_value=%d " \
  "source_specialist=%d target_specialist=%d " \
  "target_extra=%d subtarget_kind=%s subresults=%s activity=%s " \
  "target_name=%s native_rule=%s " \
  "target_kind=%s result=%s actor_consuming_always=%d legality=%s " \
  "probability_kind=%s probability_min=%d probability_max=%d gold_cost=%d " \
  "args=%s"

enum fc_agent_v2_government_status {
  FC_AGENT_V2_GOV_STABLE,
  FC_AGENT_V2_GOV_ANARCHY,
  FC_AGENT_V2_GOV_ANARCHY_TARGETED,
  FC_AGENT_V2_GOV_CHOICE_REQUIRED,
  FC_AGENT_V2_GOV_ENACTMENT_PENDING
};

enum fc_agent_v2_government_command {
  FC_AGENT_V2_GOV_REVOLUTION,
  FC_AGENT_V2_GOV_CHANGE
};

enum fc_agent_v2_transport_command {
  FC_AGENT_V2_TRANSPORT_BOARD,
  FC_AGENT_V2_TRANSPORT_DEBOARD,
  FC_AGENT_V2_TRANSPORT_EMBARK,
  FC_AGENT_V2_TRANSPORT_DISEMBARK,
  FC_AGENT_V2_TRANSPORT_LOAD,
  FC_AGENT_V2_TRANSPORT_UNLOAD
};

enum fc_agent_v2_completion {
  FC_AGENT_V2_COMPLETION_WAITING,
  FC_AGENT_V2_COMPLETION_APPLIED,
  FC_AGENT_V2_COMPLETION_REJECTED
};

enum fc_agent_v2_automation_command {
  FC_AGENT_V2_AUTOMATION_WORK,
  FC_AGENT_V2_AUTOMATION_EXPLORE,
  FC_AGENT_V2_AUTOMATION_CANCEL
};

enum fc_agent_v2_automation_controller {
  FC_AGENT_V2_CONTROLLER_NONE,
  FC_AGENT_V2_CONTROLLER_AUTO_WORK,
  FC_AGENT_V2_CONTROLLER_AUTO_EXPLORE
};

enum fc_agent_v2_target_action_policy {
  FC_AGENT_V2_TARGET_ACTION_REJECT,
  FC_AGENT_V2_TARGET_ACTION_PRESERVE_PROBABILITY,
  FC_AGENT_V2_TARGET_ACTION_REDACT_TO_UNKNOWN
};

enum fc_agent_v2_action_query {
  FC_AGENT_V2_ACTION_QUERY_REJECT,
  FC_AGENT_V2_ACTION_QUERY_ACTOR_ONLY,
  FC_AGENT_V2_ACTION_QUERY_TARGET
};

/* Shared with the standalone codec test so every action must have an explicit
 * normal-client request cardinality. */
enum agent_v2_action_kind {
  AGENT_V2_ACTION_PREGAME_CONFIGURE,
  AGENT_V2_ACTION_PREGAME_SET_TEAM,
  AGENT_V2_ACTION_PREGAME_SET_READY,
  AGENT_V2_ACTION_PLAYER_CAST_VOTE,
  AGENT_V2_ACTION_PLAYER_PROPOSE_SETTING,
  AGENT_V2_ACTION_PLAYER_CANCEL_VOTE,
  AGENT_V2_ACTION_PLAYER_SURRENDER,
  AGENT_V2_ACTION_PHASE_END,
  AGENT_V2_ACTION_MOVE,
  AGENT_V2_ACTION_ATTACK,
  AGENT_V2_ACTION_FOUND_CITY,
  AGENT_V2_ACTION_RESEARCH_TARGET,
  AGENT_V2_ACTION_RESEARCH_GOAL,
  AGENT_V2_ACTION_ECONOMY_RATES,
  AGENT_V2_ACTION_PLAYER_SEND_CHAT,
  AGENT_V2_ACTION_CITY_PRODUCTION,
  AGENT_V2_ACTION_CITY_BUY,
  AGENT_V2_ACTION_CITY_WORK_TILE,
  AGENT_V2_ACTION_CITY_UNWORK_TILE,
  AGENT_V2_ACTION_CITY_SET_SPECIALIST,
  AGENT_V2_ACTION_CITY_SET_WORKLIST,
  AGENT_V2_ACTION_CITY_SET_OPTIONS,
  AGENT_V2_ACTION_CITY_RENAME,
  AGENT_V2_ACTION_CITY_SELL_IMPROVEMENT,
  AGENT_V2_ACTION_CITY_SET_RALLY,
  AGENT_V2_ACTION_CITY_CLEAR_RALLY,
  AGENT_V2_ACTION_CITY_SET_GOVERNOR,
  AGENT_V2_ACTION_CITY_CLEAR_GOVERNOR,
  AGENT_V2_ACTION_CITY_REQUEST_WORKER_TASK,
  AGENT_V2_ACTION_CITY_CHANGE_WORKER_TASK,
  AGENT_V2_ACTION_CITY_REMOVE_WORKER_TASK,
  AGENT_V2_ACTION_WORKER_START,
  AGENT_V2_ACTION_CANCEL_ACTIVITY,
  AGENT_V2_ACTION_UNIT_SENTRY,
  AGENT_V2_ACTION_UNIT_FORTIFY,
  AGENT_V2_ACTION_UNIT_CONVERT,
  AGENT_V2_ACTION_UNIT_DISBAND,
  AGENT_V2_ACTION_UNIT_HOMELESS,
  AGENT_V2_ACTION_UNIT_UPGRADE,
  AGENT_V2_ACTION_UNIT_REHOME,
  AGENT_V2_ACTION_UNIT_JOIN_CITY,
  AGENT_V2_ACTION_UNIT_ESTABLISH_TRADE,
  AGENT_V2_ACTION_UNIT_MARKETPLACE,
  AGENT_V2_ACTION_UNIT_HELP_WONDER,
  AGENT_V2_ACTION_UNIT_DISBAND_RECOVER,
  AGENT_V2_ACTION_UNIT_AIRLIFT,
  AGENT_V2_ACTION_UNIT_PARADROP,
  AGENT_V2_ACTION_UNIT_TELEPORT,
  AGENT_V2_ACTION_TRANSPORT_BOARD,
  AGENT_V2_ACTION_TRANSPORT_DEBOARD,
  AGENT_V2_ACTION_TRANSPORT_EMBARK,
  AGENT_V2_ACTION_TRANSPORT_DISEMBARK,
  AGENT_V2_ACTION_TRANSPORT_LOAD,
  AGENT_V2_ACTION_TRANSPORT_UNLOAD,
  AGENT_V2_ACTION_UNIT_AUTO_WORK,
  AGENT_V2_ACTION_UNIT_AUTO_EXPLORE,
  AGENT_V2_ACTION_UNIT_CANCEL_AUTOMATION,
  AGENT_V2_ACTION_UNIT_CANCEL_ORDERS,
  AGENT_V2_ACTION_UNIT_CLEAR_ACTION_DECISION,
  AGENT_V2_ACTION_UNIT_GOTO,
  AGENT_V2_ACTION_UNIT_GOTO_AND_PERFORM,
  AGENT_V2_ACTION_UNIT_CONNECT_ROUTE,
  AGENT_V2_ACTION_UNIT_SET_ROUTE,
  AGENT_V2_ACTION_UNIT_ATTACK_ROUTE,
  AGENT_V2_ACTION_UNIT_SPECIAL,
  AGENT_V2_ACTION_PLAYER_PLACE_INFRA,
  AGENT_V2_ACTION_GOVERNMENT_REVOLUTION,
  AGENT_V2_ACTION_GOVERNMENT_CHANGE,
  AGENT_V2_ACTION_MULTIPLIER_SET,
  AGENT_V2_ACTION_SPACESHIP_PLACE,
  AGENT_V2_ACTION_SPACESHIP_LAUNCH,
  AGENT_V2_ACTION_DIPLOMACY_OPEN_MEETING,
  AGENT_V2_ACTION_DIPLOMACY_CLOSE_MEETING,
  AGENT_V2_ACTION_DIPLOMACY_PROPOSE_CLAUSE,
  AGENT_V2_ACTION_DIPLOMACY_REMOVE_CLAUSE,
  AGENT_V2_ACTION_DIPLOMACY_ACCEPT,
  AGENT_V2_ACTION_DIPLOMACY_WITHDRAW_ACCEPTANCE,
  AGENT_V2_ACTION_DIPLOMACY_BREAK_RELATION,
  AGENT_V2_ACTION_DIPLOMACY_WITHDRAW_VISION,
  AGENT_V2_ACTION_DIPLOMACY_WITHDRAW_SHARED_TILES,
  AGENT_V2_ACTION_KIND_COUNT
};

enum fc_agent_v2_terminal_result {
  FC_AGENT_V2_TERMINAL_NONE,
  FC_AGENT_V2_TERMINAL_APPLIED,
  FC_AGENT_V2_TERMINAL_POSTCONDITION_NOT_MET,
  FC_AGENT_V2_TERMINAL_PROCESSING_BOUNDARY_MISMATCH,
  FC_AGENT_V2_TERMINAL_SEAT_EPOCH_CHANGED
};

struct fc_agent_v2_epoch_identity {
  bool authorized;
  uintptr_t player;
  int player_number;
  uintptr_t map_tiles;
  int map_xsize;
  int map_ysize;
  int map_topology;
  int map_wrap;
  uint64_t game_epoch;
};

enum fc_agent_v2_phase_mode {
  FC_AGENT_V2_PHASE_CONCURRENT,
  FC_AGENT_V2_PHASE_PLAYERS_ALTERNATE,
  FC_AGENT_V2_PHASE_TEAMS_ALTERNATE
};

struct fc_agent_v2_phase_evidence {
  enum fc_agent_v2_phase_mode mode;
  int turn;
  int phase;
  int phase_count;
  bool active_phase;
  bool alive;
  bool phase_done;
  bool phase_ready;
};

struct fc_agent_v2_phase_notice {
  bool valid;
  uint64_t seat_epoch;
  uint64_t revision;
  struct fc_agent_v2_phase_evidence evidence;
};

bool fc_agent_v2_percent_encode(const char *raw, char *encoded,
                                size_t encoded_size);
bool fc_agent_v2_percent_decode(const char *encoded, char *decoded,
                                size_t decoded_size);
bool fc_agent_v2_chat_message_safe(const char *message);
bool fc_agent_v2_chat_echo_matches(
  bool active, bool baseline_captured, bool exact_seat_epoch,
  int request_id, int expected_request_id,
  int sender_connection_id, int self_connection_id,
  const char *channel, const char *expected_channel,
  const char *plain, const char *message, const char *recipient_name);
bool fc_agent_v2_parse_entity_ref(const char *text, char *kind, int *id,
                                  uint64_t *incarnation);
uint64_t fc_agent_v2_research_choices_digest_init(void);
bool fc_agent_v2_research_choices_digest_add(
  uint64_t *digest, int native_id, const char *canonical_name,
  const char *state, bool can_target, bool can_goal);
bool fc_agent_v2_format_unknown_tile(char *buffer, size_t buffer_size,
                                     int tile_index, int x, int y);
bool fc_agent_v2_format_unknown_local_tile(char *buffer, size_t buffer_size,
                                           int tile_index, int x, int y);
bool fc_agent_v2_parse_rates(const char *text, bool changeable,
                             int max_rate, int *tax, int *luxury,
                             int *science);
bool fc_agent_v2_parse_pregame_team_argument(
  const char *text, int team_slot_count, int *team_slot);
bool fc_agent_v2_pregame_team_choice_allowed(
  bool initial_pregame, bool unready, int current_team,
  int current_team_members, int desired_team, bool desired_team_used,
  int first_unused_team);
bool fc_agent_v2_worklist_append_allowed(
  size_t desired_length, bool can_queue,
  size_t desired_occurrences, size_t current_occurrences);
bool fc_agent_v2_government_change_observable(int revolution_finishes,
                                              int current_turn,
                                              bool has_no_anarchy);
bool fc_agent_v2_revolution_available(bool untargeted_allowed,
                                      bool can_change_during,
                                      bool current_is_during,
                                      bool target_is_during);
enum fc_agent_v2_government_status fc_agent_v2_government_status(
  int current_government, int target_government, int during_government,
  int revolution_finishes, int current_turn);
const char *fc_agent_v2_government_status_name(
  enum fc_agent_v2_government_status status);
bool fc_agent_v2_government_postcondition(
  enum fc_agent_v2_government_command command,
  int before_current, int before_target, int before_finish,
  int after_current, int after_target, int after_finish,
  int during_government, int desired_government,
  bool change_event_latched);
bool fc_agent_v2_probability_candidate_preferred(
  int candidate_rank, int candidate_min, int candidate_max,
  int candidate_action, int existing_rank, int existing_min,
  int existing_max, int existing_action);
bool fc_agent_v2_unit_lifetime_matches(uint64_t tracked_lifecycle,
                                       uint64_t current_lifecycle);
bool fc_agent_v2_city_lifetime_matches(uint64_t tracked_lifecycle,
                                       uint64_t current_lifecycle);
bool fc_agent_v2_unit_activity_postcondition(
  uint64_t expected_lifecycle, bool before_present,
  uint64_t before_lifecycle, bool current_present,
  uint64_t current_lifecycle, int current_activity,
  bool activity_target_none, int requested_activity,
  int completed_activity);
bool fc_agent_v2_unit_conversion_postcondition(
  uint64_t expected_lifecycle, bool before_present,
  uint64_t before_lifecycle, bool current_present,
  uint64_t current_lifecycle, int current_activity,
  bool activity_target_none, int convert_activity,
  int before_type, int desired_type, int current_type);
bool fc_agent_v2_unit_consumed_postcondition(
  uint64_t expected_lifecycle, bool before_present,
  uint64_t before_lifecycle, bool current_present);
bool fc_agent_v2_unit_home_cleared_postcondition(
  uint64_t expected_lifecycle, bool before_present,
  uint64_t before_lifecycle, bool current_present,
  uint64_t current_lifecycle, int before_home, int current_home);
bool fc_agent_v2_unit_upgrade_postcondition(
  uint64_t expected_lifecycle, bool before_present,
  uint64_t before_lifecycle, int before_type,
  bool current_present, uint64_t current_lifecycle,
  int desired_type, int current_type);
bool fc_agent_v2_unit_rehome_postcondition(
  uint64_t expected_lifecycle, bool before_present,
  uint64_t before_lifecycle, int before_home,
  bool current_present, uint64_t current_lifecycle,
  int desired_home, int current_home);
bool fc_agent_v2_consumed_city_postcondition(
  uint64_t expected_unit_lifecycle, bool before_unit_present,
  uint64_t before_unit_lifecycle, bool current_unit_present,
  uint64_t expected_city_lifecycle, bool before_city_present,
  uint64_t before_city_lifecycle, bool current_city_present,
  uint64_t current_city_lifecycle);
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
  uint64_t current_destination_lifecycle);
bool fc_agent_v2_join_city_postcondition(
  uint64_t expected_unit_lifecycle, bool before_unit_present,
  uint64_t before_unit_lifecycle, bool current_unit_present,
  uint64_t expected_city_lifecycle, bool before_city_present,
  uint64_t before_city_lifecycle, int before_size, int population_added,
  bool current_city_present, uint64_t current_city_lifecycle,
  int current_size);
bool fc_agent_v2_help_wonder_postcondition(
  uint64_t expected_unit_lifecycle, bool before_unit_present,
  uint64_t before_unit_lifecycle, bool current_unit_present,
  uint64_t expected_city_lifecycle, bool before_city_present,
  uint64_t before_city_lifecycle, bool exact_city_internals,
  int before_shields, int shields_added,
  bool current_city_present, uint64_t current_city_lifecycle,
  int current_shields);
bool fc_agent_v2_disband_recover_postcondition(
  uint64_t expected_unit_lifecycle, bool before_unit_present,
  uint64_t before_unit_lifecycle, bool current_unit_present,
  uint64_t expected_city_lifecycle, bool before_city_present,
  uint64_t before_city_lifecycle, bool caravan_action_event,
  bool before_city_owned, int before_shields, int shields_added,
  bool current_city_present, uint64_t current_city_lifecycle,
  bool current_city_owned, int current_shields);
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
  bool before_route, bool current_route);
bool fc_agent_v2_unit_relocation_postcondition(
  uint64_t expected_lifecycle, bool before_present,
  uint64_t before_lifecycle, int before_tile,
  bool current_present, uint64_t current_lifecycle, int current_tile,
  int target_tile, bool require_paradropped, bool before_paradropped,
  bool current_paradropped);
bool fc_agent_v2_paradrop_enter_conquer_postcondition(
  bool baseline_exact,
  uint64_t expected_lifecycle, bool before_present,
  uint64_t before_lifecycle, int before_tile, bool before_paradropped,
  bool current_present, uint64_t current_lifecycle, int current_tile,
  bool current_paradropped, int target_tile, int self_player,
  int before_city_id, int before_city_owner,
  int current_city_id, int current_city_owner,
  int before_extra_owner, int current_extra_owner,
  bool before_hut_present, bool hut_removed);
bool fc_agent_v2_transport_occupancy_exact(bool advertised_occupied,
                                           int known_occupied,
                                           int capacity,
                                           bool all_cargo_visible);
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
  bool current_detached_exact, int requested_tile);
enum fc_agent_v2_target_action_policy fc_agent_v2_target_action_policy(
  bool target_unknown, bool movement_action, bool probability_possible);
enum fc_agent_v2_action_query fc_agent_v2_action_query_policy(
  bool target_unknown, bool movement_action);
bool fc_agent_v2_target_server_query_allowed(
  bool target_known, bool target_visible);
bool fc_agent_v2_action_decision_state_valid(int want, int tile);
bool fc_agent_v2_action_decision_target_query_allowed(
  bool target_known, bool target_visible, bool owned_actor,
  int want, int decision_tile, int requested_tile);
bool fc_agent_v2_route_paused_for_decision(
  bool same_actor_lifetime, int want, int decision_tile,
  const int *action_move_tiles, size_t action_move_tile_count);

void fc_agent_v2_make_slot(char *slot, size_t slot_size,
                           uint64_t secret, uint64_t revision,
                           const void *semantic, size_t semantic_size);
bool fc_agent_v2_make_target_slot(char *slot, size_t slot_size,
                                  uint64_t secret, uint64_t revision,
                                  uint32_t native_tile,
                                  const void *semantic,
                                  size_t semantic_size);
bool fc_agent_v2_parse_target_slot(const char *slot,
                                   uint32_t *native_tile);
bool fc_agent_v2_target_slot_matches(const char *left, const char *right);

bool fc_agent_v2_vote_update_matches(
  bool pending_active, bool processing_started, bool baseline_captured,
  bool seat_epoch_current, bool cast_vote_action,
  int observed_request_id, int expected_request_id,
  int observed_vote_no, int expected_vote_no);

enum fc_agent_v2_completion
fc_agent_v2_classify_completion(bool request_processed,
                                bool postcondition_met);

bool fc_agent_v2_epoch_changed(
  bool previous_known,
  const struct fc_agent_v2_epoch_identity *previous,
  const struct fc_agent_v2_epoch_identity *current);
bool fc_agent_v2_boundary_ready(bool authorized, int processing_request_id,
                                bool agents_are_busy);
bool fc_agent_v2_relation_baseline_matches(
  uint64_t expected_generation, uint64_t current_generation,
  uint64_t expected_clauses_digest, uint64_t current_clauses_digest,
  bool expected_self_accepted, bool current_self_accepted,
  bool expected_other_accepted, bool current_other_accepted,
  int expected_relation_state, int current_relation_state,
  bool expected_outgoing_vision, bool current_outgoing_vision,
  bool expected_outgoing_shared_tiles,
  bool current_outgoing_shared_tiles);
bool fc_agent_v2_stream_notification_allowed(bool pending_active);
bool fc_agent_v2_agents_busy_if_ready(bool client_cache_initialized,
                                      bool (*agents_are_busy)(void));
bool fc_agent_v2_action_phase_ready(bool authorized, bool coherent,
                                    bool can_issue_orders, bool is_alive,
                                    bool is_active_phase, bool phase_done,
                                    bool server_is_busy);
const char *fc_agent_v2_phase_mode_name(enum fc_agent_v2_phase_mode mode);
bool fc_agent_v2_build_phase_evidence(
  int mode, int player_count, int team_count, int turn, int phase,
  int player_number, int team_number, bool running, bool authorized,
  bool alive, bool phase_done, bool can_end_turn,
  struct fc_agent_v2_phase_evidence *evidence);
size_t fc_agent_v2_phase_end_action_count(
  const struct fc_agent_v2_phase_evidence *evidence);
bool fc_agent_v2_format_phase_available(
  char *buffer, size_t buffer_size, uint64_t revision,
  const struct fc_agent_v2_phase_evidence *evidence);
bool fc_agent_v2_phase_notice_needed(
  const struct fc_agent_v2_phase_notice *notice, uint64_t seat_epoch,
  uint64_t revision, const struct fc_agent_v2_phase_evidence *evidence);
bool fc_agent_v2_phase_revision_changed(
  bool running, bool have_current,
  const struct fc_agent_v2_phase_evidence *current,
  const struct fc_agent_v2_phase_evidence *next);
void fc_agent_v2_phase_notice_record(
  struct fc_agent_v2_phase_notice *notice, uint64_t seat_epoch,
  uint64_t revision, const struct fc_agent_v2_phase_evidence *evidence);
bool fc_agent_v2_callback_matches(bool pending_active,
                                  int pending_request_id,
                                  uint64_t pending_nonce,
                                  int callback_request_id,
                                  uint64_t callback_nonce);
bool fc_agent_v2_request_group_exact(int before_request_id,
                                     int first_request_id,
                                     int last_request_id,
                                     size_t request_count);
size_t fc_agent_v2_expected_request_count(enum agent_v2_action_kind kind);
bool fc_agent_v2_unit_automation_latch_matches(
  enum fc_agent_v2_automation_command command,
  bool pending_active, bool seat_epoch_current,
  int packet_request_id, int expected_request_id,
  bool owned, int packet_unit_id, int expected_unit_id,
  uint64_t packet_lifecycle, uint64_t expected_lifecycle,
  uint64_t packet_incarnation, uint64_t expected_incarnation,
  enum fc_agent_v2_automation_controller controller,
  bool activity_explore, bool activity_target_none);
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
  bool exact_explore_latch);
bool fc_agent_v2_unit_cancel_orders_postcondition(
  uint64_t expected_lifecycle,
  bool before_present, uint64_t before_lifecycle,
  int expected_source_tile, int before_tile,
  enum fc_agent_v2_automation_controller before_controller,
  bool before_idle, bool before_target_none,
  bool before_has_orders, bool before_goto_none,
  bool current_present, uint64_t current_lifecycle,
  bool current_has_orders, bool current_goto_none);
bool fc_agent_v2_worker_task_echo_matches(
  bool pending_active, bool baseline_captured, bool seat_epoch_current,
  int packet_request_id, int expected_request_id,
  int packet_city, int expected_city,
  int packet_tile, int expected_tile,
  int packet_activity, int expected_activity,
  int packet_extra, int expected_extra,
  int packet_want, int expected_want,
  bool exact_cache_state);
bool fc_agent_v2_rally_state_canonical(
  bool active, bool persistent, bool vigilant,
  int order_count, uint64_t orders_digest);
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
  uint64_t current_orders_digest);
enum fc_agent_v2_terminal_result fc_agent_v2_rally_terminal(
  bool seat_epoch_current, bool processing_started,
  bool baseline_captured, bool last_started,
  bool postcondition_met);
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
  int current_tile, bool current_has_orders, int current_goto_tile);
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
  bool current_repeat, bool current_vigilant);
bool fc_agent_v2_unit_route_shape_matches(
  enum agent_v2_action_kind kind, bool action_move,
  int final_action, int final_subtarget,
  int action_none, int no_target,
  int expected_action, int expected_subtarget);
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
  bool exact_install_latched);
bool fc_agent_v2_infrastructure_postcondition(
  int before_points, int cost, int current_points,
  bool before_unplaced, int expected_extra, int current_placing_extra);
bool fc_agent_v2_hut_transition_postcondition(
  bool baseline_exact, bool baseline_hut_present,
  bool baseline_hut_removed, uint64_t expected_unit_lifecycle,
  bool current_unit_present, uint64_t current_unit_lifecycle,
  int current_unit_tile, int target_tile);
bool fc_agent_v2_conquer_extras_postcondition(
  bool baseline_exact, int before_owner, int self_player,
  int current_owner, uint64_t expected_unit_lifecycle,
  bool current_unit_present, uint64_t current_unit_lifecycle,
  int current_unit_tile, int target_tile);
bool fc_agent_v2_espionage_effect_postcondition(
  bool target_binding_exact, bool effect_baseline_exact,
  uint64_t expected_city_lifecycle,
  bool before_city_present, uint64_t before_city_lifecycle,
  bool current_city_present, uint64_t current_city_lifecycle,
  bool effect_transition_proven);
bool fc_agent_v2_targeted_sabotage_postcondition(
  bool target_binding_exact, bool success_receipt_latched,
  bool building_externally_visible,
  uint64_t expected_city_lifecycle,
  bool before_city_present, uint64_t before_city_lifecycle,
  bool current_city_present, uint64_t current_city_lifecycle,
  bool before_building_present, bool current_building_present);
bool fc_agent_v2_city_espionage_event_matches(
  bool active, bool processing_started, bool baseline_captured,
  bool seat_epoch_current, uint64_t frozen_revision,
  bool exact_action_family, bool actor_binding_exact,
  bool city_binding_exact, int observed_request, int expected_request,
  int observed_tile, int expected_tile,
  int observed_event, int expected_event);
bool fc_agent_v2_action_receipt_matches(
  bool active, bool processing_started, bool baseline_captured,
  bool seat_epoch_current, bool terminal_clear,
  bool actor_binding_exact, bool target_binding_exact,
  int observed_request, int expected_request,
  int observed_request_kind, int expected_request_kind,
  int observed_actor, int expected_actor,
  int observed_target, int expected_target,
  int observed_action, int expected_action, int observed_status);
bool fc_agent_v2_custom_action_postcondition(
  bool target_binding_exact, bool success_receipt_latched,
  uint64_t expected_actor_lifecycle,
  bool before_actor_present, uint64_t before_actor_lifecycle);
bool fc_agent_v2_poison_city_postcondition(
  bool target_binding_exact, bool success_receipt_latched,
  uint64_t expected_city_lifecycle,
  bool before_city_present, uint64_t before_city_lifecycle,
  int before_city_size, bool current_city_present,
  bool current_city_binding_exact, uint64_t current_city_lifecycle,
  int current_city_size);
bool fc_agent_v2_sabotage_city_postcondition(
  bool target_binding_exact, bool success_receipt_latched,
  uint64_t expected_city_lifecycle,
  bool before_city_present, uint64_t before_city_lifecycle,
  bool current_city_present, bool current_city_binding_exact,
  uint64_t current_city_lifecycle, bool visible_effect_corroborated);
bool fc_agent_v2_combat_observer_matches(
  bool active, bool baseline_captured, bool seat_epoch_current,
  bool classic_combat_action, bool actor_binding_exact,
  int observed_request, int expected_request,
  int observed_attacker, int expected_actor,
  int observed_defender, bool defender_on_expected_target);
bool fc_agent_v2_spy_attack_postcondition(
  bool target_binding_exact, uint64_t expected_actor_lifecycle,
  bool before_actor_present, uint64_t before_actor_lifecycle,
  bool actor_loss_event, bool target_loss_event,
  bool current_actor_present, bool current_actor_binding_exact,
  uint64_t before_stack_signature, uint64_t current_stack_signature);
bool fc_agent_v2_sabotage_unit_postcondition(
  bool target_binding_exact, bool success_event_latched,
  uint64_t expected_target_lifecycle,
  bool before_target_present, uint64_t before_target_lifecycle,
  int before_target_hp, bool current_target_present,
  bool current_target_binding_exact, int current_target_hp);
bool fc_agent_v2_nuke_observer_matches(
  bool active, bool baseline_captured, bool seat_epoch_current,
  bool classic_nuke_action, int observed_request, int expected_request,
  int observed_tile, int expected_tile);
bool fc_agent_v2_nuke_stack_binding_matches(
  bool stack_target_action, uint64_t frozen_signature,
  uint64_t current_signature);
bool fc_agent_v2_nuke_postcondition(
  bool target_binding_exact, bool nuke_tile_info_latched,
  uint64_t expected_actor_lifecycle,
  bool before_actor_present, uint64_t before_actor_lifecycle,
  bool current_actor_present);
bool fc_agent_v2_goto_candidate_precedes(
  int left_distance, int left_tile,
  int right_distance, int right_tile);
bool fc_agent_v2_city_target_distance_candidate(
  int distance, bool unlimited, int maximum_distance);
enum fc_agent_v2_terminal_result fc_agent_v2_capture_group_terminal(
  bool seat_epoch_current, bool first_started, bool baseline_captured,
  bool first_finished, bool last_started, bool postcondition_met);
enum fc_agent_v2_terminal_result fc_agent_v2_automation_terminal(
  enum fc_agent_v2_automation_command command,
  bool seat_epoch_current, bool first_started, bool baseline_captured,
  bool first_finished, bool last_started,
  bool exact_start_baseline, bool exact_unit_lifetime_current,
  bool postcondition_met);
enum fc_agent_v2_terminal_result fc_agent_v2_consuming_city_terminal(
  bool seat_epoch_current, bool first_started, bool baseline_captured,
  bool first_finished, bool last_started,
  bool exact_actor_present, bool actor_absent,
  bool postcondition_met);
void fc_agent_v2_capture_terminal(
  enum fc_agent_v2_terminal_result *terminal,
  bool seat_epoch_current, bool processing_started,
  bool baseline_captured, bool postcondition_met);
enum fc_agent_v2_terminal_result fc_agent_v2_terminal_after_epoch_change(
  enum fc_agent_v2_terminal_result terminal);
uint64_t fc_agent_v2_take_incarnation(uint64_t *next_incarnation);

#endif /* FC__AGENT_PROTOCOL_V2_CODEC_H */
