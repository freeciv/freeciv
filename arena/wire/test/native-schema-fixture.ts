/**
 * The structure `_derive_native_schema_id` hashes, transcribed from
 * agent_eval/v2_control.py into TypeScript values.
 *
 * This is the acceptance fixture for the canonical writer: sha256 over its
 * canonical ASCII bytes must equal `NATIVE_OBSERVATION_ACTION_SCHEMA_ID`, the
 * id the native client refuses to talk without.
 *
 * Mechanically transcribed once (spike S2's `s2-fixture-codegen.py`) and
 * carried over verbatim.  Python ints are bigints here, including the
 * FNV-1a-64 offset basis 14695981039346656037 which is > 2**53 and cannot
 * survive a JS number.  Object keys are in Python insertion order on purpose:
 * the canonical writer must be the thing that sorts them.
 */
import type { CanonValue } from 'src/canon';

export const NATIVE_SCHEMA_CANONICAL: CanonValue =
  {
    "format": "freeciv-agent-observation-action-schema-v1",
    "row_fields": [
      [
        "meta",
        [
          "state",
          "turn",
          "phase",
          "cache",
          "phase_mode",
          "phase_count",
          "active_phase",
          "phase_ready",
          "map_width",
          "map_height",
          "topology",
          "wrap_x",
          "wrap_y",
          "known_tile_count"
        ]
      ],
      [
        "pregame",
        [
          "ref",
          "leader",
          "nation",
          "sex",
          "style",
          "ready",
          "nation_choices",
          "style_choices",
          "team_choices"
        ]
      ],
      [
        "pregame_nation",
        [
          "id",
          "name",
          "default_style"
        ]
      ],
      [
        "pregame_style",
        [
          "id",
          "name"
        ]
      ],
      [
        "pregame_team",
        [
          "id",
          "name",
          "selected",
          "occupied",
          "member_count"
        ]
      ],
      [
        "pregame_team_member",
        [
          "team",
          "player",
          "leader"
        ]
      ],
      [
        "vote",
        [
          "vote_no",
          "caller",
          "description",
          "yes",
          "no",
          "abstain",
          "num_voters",
          "percent_required",
          "team_only",
          "current_vote",
          "can_vote",
          "status",
          "outcome_turn",
          "outcome_phase"
        ]
      ],
      [
        "player",
        [
          "ref",
          "name",
          "nation",
          "government",
          "gold",
          "tax",
          "science",
          "luxury",
          "alive",
          "phase_done",
          "changeable_tax",
          "max_rate",
          "infrastructure_enabled",
          "infrastructure_points"
        ]
      ],
      [
        "governance",
        [
          "current_id",
          "target_id",
          "during_id",
          "status",
          "finish_turn",
          "turns_remaining",
          "method",
          "max_turns",
          "untargeted_allowed",
          "no_anarchy",
          "can_revolution",
          "choices_count"
        ]
      ],
      [
        "government",
        [
          "id",
          "name",
          "current",
          "target",
          "during",
          "can_change"
        ]
      ],
      [
        "multiplier",
        [
          "id",
          "name",
          "value",
          "target",
          "start",
          "stop",
          "step",
          "minimum_turns",
          "changed_turn",
          "can_change",
          "choice_count"
        ]
      ],
      [
        "spaceship",
        [
          "state",
          "structurals",
          "structurals_placed",
          "components",
          "fuel",
          "propulsion",
          "modules",
          "habitation",
          "life_support",
          "solar_panels",
          "launch_year",
          "population",
          "mass",
          "support_permille",
          "energy_permille",
          "success_permille",
          "travel_time_millis",
          "has_capital",
          "can_launch"
        ]
      ],
      [
        "spaceship_structural",
        [
          "slot",
          "x",
          "y",
          "required_slot",
          "placed",
          "required_connected",
          "can_place"
        ]
      ],
      [
        "research",
        [
          "techs",
          "future",
          "target",
          "target_id",
          "goal",
          "goal_id",
          "bulbs",
          "cost",
          "output",
          "choices_count",
          "choices_digest"
        ]
      ],
      [
        "research_tech",
        [
          "id",
          "name",
          "state",
          "can_target",
          "can_goal"
        ]
      ],
      [
        "research_graph",
        [
          "id",
          "name",
          "reachable",
          "next_step",
          "unknown_prerequisites",
          "path_cost"
        ]
      ],
      [
        "research_edge",
        [
          "tech",
          "prerequisite",
          "kind"
        ]
      ],
      [
        "research_unlock",
        [
          "tech",
          "kind",
          "native_id",
          "name",
          "scope"
        ]
      ],
      [
        "diplomacy",
        [
          "other",
          "name",
          "nation",
          "state",
          "contact",
          "alive",
          "turns_left",
          "can_meet",
          "meeting",
          "generation",
          "self_accepted",
          "other_accepted",
          "clause_count",
          "clauses_digest",
          "intel_level",
          "team",
          "team_name",
          "same_team",
          "controller",
          "connected",
          "score",
          "gold",
          "government",
          "has_embassy",
          "other_has_embassy",
          "gives_vision",
          "receives_vision",
          "gives_shared_tiles",
          "receives_shared_tiles",
          "can_cancel",
          "cancel_reason"
        ]
      ],
      [
        "diplomacy_intel",
        [
          "other",
          "tax",
          "science",
          "luxury",
          "culture",
          "research_id",
          "research_name",
          "bulbs",
          "cost",
          "known_count",
          "known_digest",
          "known_ids"
        ]
      ],
      [
        "diplomacy_clause",
        [
          "other",
          "generation",
          "position",
          "giver",
          "type",
          "value_kind",
          "value",
          "name"
        ]
      ],
      [
        "tile",
        [
          "index",
          "x",
          "y",
          "known",
          "terrain",
          "owner",
          "placing_extra",
          "placing_extra_name",
          "placing_turns",
          "placing_time"
        ]
      ],
      [
        "tile_local",
        [
          "index",
          "x",
          "y",
          "known",
          "terrain",
          "owner",
          "placing_extra",
          "placing_extra_name",
          "placing_turns",
          "placing_time",
          "resource_extra",
          "resource_name",
          "has_label",
          "label",
          "food",
          "shields",
          "trade"
        ]
      ],
      [
        "tile_extra",
        [
          "tile",
          "extra",
          "name",
          "cause_mask"
        ]
      ],
      [
        "infrastructure_extra",
        [
          "id",
          "name",
          "cost",
          "build_time",
          "build_time_factor"
        ]
      ],
      [
        "city",
        [
          "ref",
          "name",
          "tile",
          "x",
          "y",
          "size",
          "food",
          "shields",
          "trade",
          "production_kind",
          "production_id",
          "production_name",
          "shield_stock",
          "shield_cost",
          "buy_cost",
          "can_buy",
          "can_change",
          "citizen_tile_count",
          "specialist_type_count",
          "worklist_length",
          "build_choice_count",
          "improvement_count",
          "trade_route_count",
          "trade_route_capacity",
          "did_sell",
          "allow_disband",
          "new_citizens",
          "options_conflict",
          "airlift_remaining",
          "airlift_max",
          "governor_enabled",
          "citizen_happy",
          "citizen_content",
          "citizen_unhappy",
          "citizen_angry",
          "citizen_workers",
          "citizen_specialists",
          "food_stock",
          "granary_size",
          "growth_turns",
          "pollution",
          "food_citizen_base",
          "food_net",
          "food_surplus",
          "food_usage",
          "food_waste",
          "food_unhappy_penalty",
          "shield_citizen_base",
          "shield_net",
          "shield_surplus",
          "shield_usage",
          "shield_waste",
          "shield_unhappy_penalty",
          "trade_citizen_base",
          "trade_net",
          "trade_surplus",
          "trade_usage",
          "trade_waste",
          "trade_unhappy_penalty",
          "gold_citizen_base",
          "gold_net",
          "gold_surplus",
          "gold_usage",
          "gold_waste",
          "gold_unhappy_penalty",
          "luxury_citizen_base",
          "luxury_net",
          "luxury_surplus",
          "luxury_usage",
          "luxury_waste",
          "luxury_unhappy_penalty",
          "science_citizen_base",
          "science_net",
          "science_surplus",
          "science_usage",
          "science_waste",
          "science_unhappy_penalty"
        ]
      ],
      [
        "city_site",
        [
          "ref",
          "owner",
          "name",
          "tile",
          "x",
          "y",
          "size",
          "visibility"
        ]
      ],
      [
        "city_tile",
        [
          "city",
          "tile",
          "worked",
          "free_worked",
          "can_work",
          "food",
          "shields",
          "trade",
          "gold",
          "luxury",
          "science"
        ]
      ],
      [
        "city_worker_task",
        [
          "city",
          "tile",
          "activity",
          "target_extra",
          "target_extra_name",
          "want"
        ]
      ],
      [
        "city_specialist",
        [
          "city",
          "specialist",
          "name",
          "count",
          "counts_toward_population",
          "can_use",
          "is_default",
          "food",
          "shields",
          "trade",
          "gold",
          "luxury",
          "science"
        ]
      ],
      [
        "city_worklist",
        [
          "city",
          "position",
          "production_kind",
          "production_id",
          "production_name"
        ]
      ],
      [
        "city_build_choice",
        [
          "city",
          "production_kind",
          "production_id",
          "production_name",
          "can_queue",
          "can_build_now",
          "shield_cost",
          "shield_stock_after_change",
          "turns",
          "turns_with_stock",
          "upkeep_food",
          "upkeep_shield",
          "upkeep_trade",
          "upkeep_gold",
          "upkeep_luxury",
          "upkeep_science",
          "happy_cost",
          "unit_attack",
          "unit_defense",
          "unit_move_rate",
          "unit_hp",
          "unit_firepower",
          "unit_vision_radius_sq",
          "unit_transport_capacity",
          "unit_fuel",
          "unit_pop_cost",
          "unit_bombard_rate",
          "unit_city_size",
          "unit_paradrop_range",
          "building_genus",
          "building_obsolete",
          "building_redundant",
          "building_convert",
          "building_allows_units",
          "building_allows_extras",
          "building_prevents_disaster",
          "building_protects_vs_actions",
          "building_allows_actions"
        ]
      ],
      [
        "city_improvement",
        [
          "city",
          "improvement_id",
          "name",
          "sellable",
          "sell_price"
        ]
      ],
      [
        "city_trade_route",
        [
          "city",
          "position",
          "partner",
          "partner_visibility",
          "partner_name",
          "base_value",
          "effective_value",
          "direction",
          "goods_id",
          "goods_name"
        ]
      ],
      [
        "investigation",
        [
          "city",
          "lifecycle",
          "tile",
          "name",
          "size",
          "production_kind",
          "production_id",
          "production_name",
          "shield_stock",
          "shield_surplus",
          "improvement_count",
          "feeling_count",
          "specialist_count"
        ]
      ],
      [
        "investigation_improvement",
        [
          "city",
          "improvement_id",
          "name"
        ]
      ],
      [
        "investigation_citizens",
        [
          "city",
          "stage",
          "happy",
          "content",
          "unhappy",
          "angry"
        ]
      ],
      [
        "investigation_specialist",
        [
          "city",
          "specialist",
          "name",
          "count"
        ]
      ],
      [
        "city_rally",
        [
          "city",
          "active",
          "persistent",
          "vigilant",
          "order_count",
          "orders_digest"
        ]
      ],
      [
        "city_governor",
        [
          "city",
          "min_food",
          "min_production",
          "min_trade",
          "min_gold",
          "min_luxury",
          "min_science",
          "weight_food",
          "weight_production",
          "weight_trade",
          "weight_gold",
          "weight_luxury",
          "weight_science",
          "celebration_weight",
          "require_happy",
          "maximize_growth"
        ]
      ],
      [
        "unit_own",
        [
          "ref",
          "scope",
          "owner",
          "type_id",
          "type",
          "home_city",
          "converts_to_id",
          "converts_to",
          "tile",
          "x",
          "y",
          "hp",
          "veteran",
          "veteran_name",
          "veteran_levels",
          "veteran_power",
          "veteran_move_bonus",
          "fuel",
          "max_hp",
          "max_fuel",
          "move_rate",
          "attack",
          "defense",
          "firepower",
          "base_upkeep_food",
          "base_upkeep_shield",
          "base_upkeep_trade",
          "base_upkeep_gold",
          "base_upkeep_luxury",
          "base_upkeep_science",
          "upkeep_food",
          "upkeep_shield",
          "upkeep_trade",
          "upkeep_gold",
          "upkeep_luxury",
          "upkeep_science",
          "moves",
          "activity",
          "activity_target",
          "activity_target_name",
          "activity_progress",
          "transport_state",
          "transporter",
          "transport_capacity",
          "occupied",
          "paradropped",
          "paradrop_range",
          "controller",
          "has_orders",
          "orders_repeat",
          "orders_vigilant",
          "order_count",
          "orders_digest",
          "orders_destination",
          "action_decision_want",
          "action_decision_tile"
        ]
      ],
      [
        "unit_route",
        [
          "unit",
          "order_index",
          "reconstructable",
          "step_count"
        ]
      ],
      [
        "unit_route_step",
        [
          "unit",
          "sequence",
          "kind",
          "tile"
        ]
      ],
      [
        "unit_visible",
        [
          "ref",
          "scope",
          "owner",
          "type_id",
          "type",
          "tile",
          "x",
          "y",
          "hp",
          "veteran",
          "veteran_name",
          "veteran_levels",
          "veteran_power",
          "veteran_move_bonus",
          "max_hp",
          "max_fuel",
          "move_rate",
          "attack",
          "defense",
          "firepower",
          "base_upkeep_food",
          "base_upkeep_shield",
          "base_upkeep_trade",
          "base_upkeep_gold",
          "base_upkeep_luxury",
          "base_upkeep_science"
        ]
      ],
      [
        "tombstone",
        [
          "ref",
          "kind"
        ]
      ],
      [
        "chat",
        [
          "sequence",
          "turn",
          "phase",
          "sender",
          "sender_name",
          "self",
          "channel",
          "event",
          "truncated",
          "message"
        ]
      ],
      [
        "chat_recipient",
        [
          "ref",
          "name",
          "self",
          "connected",
          "can_message"
        ]
      ],
      [
        "action",
        [
          "slot",
          "kind",
          "actor",
          "counterpart",
          "meeting_generation",
          "clauses_digest",
          "self_accepted",
          "other_accepted",
          "relation_state",
          "outgoing_vision",
          "outgoing_shared_tiles",
          "clause_giver",
          "clause_type",
          "clause_value",
          "clause_name",
          "desired_acceptance",
          "target_tile",
          "source_city",
          "destination_city",
          "target_unit",
          "transport_context",
          "target_tech",
          "vote_no",
          "server_setting_id",
          "server_setting_type",
          "server_setting_min",
          "server_setting_max",
          "server_setting_current",
          "server_setting_value",
          "target_government",
          "max_rate",
          "route_waypoint_limit",
          "infrastructure_cost",
          "infrastructure_turns",
          "infrastructure_choice_count",
          "infrastructure_choices",
          "target_build_kind",
          "target_build",
          "spaceship_part",
          "spaceship_value",
          "target_multiplier",
          "multiplier_value",
          "source_specialist",
          "target_specialist",
          "target_extra",
          "subtarget_kind",
          "subresults",
          "activity",
          "target_name",
          "native_rule",
          "target_kind",
          "result",
          "actor_consuming_always",
          "legality",
          "probability_kind",
          "probability_min",
          "probability_max",
          "gold_cost",
          "args"
        ]
      ]
    ],
    "row_formats": [
      [
        "meta",
        "meta state=%s turn=%d phase=%d cache=human-client phase_mode=%s phase_count=%d active_phase=%d phase_ready=%d map_width=%d map_height=%d topology=%s wrap_x=%d wrap_y=%d known_tile_count=%d"
      ],
      [
        "pregame",
        "pregame ref=%s leader=%s nation=%s sex=%s style=%s ready=%d nation_choices=%d style_choices=%d team_choices=%d"
      ],
      [
        "pregame_nation",
        "pregame_nation id=%d name=%s default_style=%d"
      ],
      [
        "pregame_style",
        "pregame_style id=%d name=%s"
      ],
      [
        "pregame_team",
        "pregame_team id=%d name=%s selected=%d occupied=%d member_count=%d"
      ],
      [
        "pregame_team_member",
        "pregame_team_member team=%d player=%s leader=%s"
      ],
      [
        "vote",
        "vote vote_no=%d caller=%s description=%s yes=%d no=%d abstain=%d num_voters=%d percent_required=%d team_only=%d current_vote=%s can_vote=%d status=%s outcome_turn=%d outcome_phase=%d"
      ],
      [
        "player",
        "player ref=%s name=%s nation=%s government=%s gold=%d tax=%d science=%d luxury=%d alive=%d phase_done=%d changeable_tax=%d max_rate=%d infrastructure_enabled=%d infrastructure_points=%d"
      ],
      [
        "governance",
        "governance current_id=%d target_id=%d during_id=%d status=%s finish_turn=%d turns_remaining=%d method=%s max_turns=%d untargeted_allowed=%d no_anarchy=%d can_revolution=%d choices_count=%d"
      ],
      [
        "government",
        "government id=%d name=%s current=%d target=%d during=%d can_change=%d"
      ],
      [
        "multiplier",
        "multiplier id=%d name=%s value=%d target=%d start=%d stop=%d step=%d minimum_turns=%d changed_turn=%d can_change=%d choice_count=%llu"
      ],
      [
        "spaceship",
        "spaceship state=%s structurals=%d structurals_placed=%d components=%d fuel=%d propulsion=%d modules=%d habitation=%d life_support=%d solar_panels=%d launch_year=%d population=%d mass=%d support_permille=%d energy_permille=%d success_permille=%d travel_time_millis=%d has_capital=%d can_launch=%d"
      ],
      [
        "spaceship_structural",
        "spaceship_structural slot=%d x=%d y=%d required_slot=%d placed=%d required_connected=%d can_place=%d"
      ],
      [
        "research",
        "research techs=%d future=%d target=%s target_id=%d goal=%s goal_id=%d bulbs=%d cost=%d output=%d choices_count=%d choices_digest=%s"
      ],
      [
        "research_tech",
        "research_tech id=%d name=%s state=%s can_target=%d can_goal=%d"
      ],
      [
        "research_graph",
        "research_graph id=%d name=%s reachable=%d next_step=%d unknown_prerequisites=%d path_cost=%d"
      ],
      [
        "research_edge",
        "research_edge tech=%d prerequisite=%d kind=%s"
      ],
      [
        "research_unlock",
        "research_unlock tech=%d kind=%s native_id=%d name=%s scope=%s"
      ],
      [
        "diplomacy",
        "diplomacy other=%s name=%s nation=%s state=%s contact=%d alive=%d turns_left=%d can_meet=%d meeting=%d generation=%llu self_accepted=%d other_accepted=%d clause_count=%d clauses_digest=%s intel_level=%s team=%d team_name=%s same_team=%d controller=%s connected=%d score=%d gold=%d government=%s has_embassy=%d other_has_embassy=%d gives_vision=%d receives_vision=%d gives_shared_tiles=%d receives_shared_tiles=%d can_cancel=%d cancel_reason=%s"
      ],
      [
        "diplomacy_intel",
        "diplomacy_intel other=%s tax=%d science=%d luxury=%d culture=%d research_id=%d research_name=%s bulbs=%d cost=%d known_count=%d known_digest=%s known_ids=%s"
      ],
      [
        "diplomacy_clause",
        "diplomacy_clause other=%s generation=%llu position=%d giver=%s type=%s value_kind=%s value=%d name=%s"
      ],
      [
        "tile",
        "tile index=%d x=%d y=%d known=%d terrain=%s owner=%s placing_extra=%d placing_extra_name=%s placing_turns=%d placing_time=%d"
      ],
      [
        "tile_local",
        "tile_local index=%d x=%d y=%d known=%d terrain=%s owner=%s placing_extra=%d placing_extra_name=%s placing_turns=%d placing_time=%d resource_extra=%d resource_name=%s has_label=%d label=%s food=%d shields=%d trade=%d"
      ],
      [
        "tile_extra",
        "tile_extra tile=%d extra=%d name=%s cause_mask=%u"
      ],
      [
        "infrastructure_extra",
        "infrastructure_extra id=%d name=%s cost=%d build_time=%d build_time_factor=%d"
      ],
      [
        "city",
        "city ref=%s name=%s tile=%d x=%d y=%d size=%d food=%d shields=%d trade=%d production_kind=%s production_id=%d production_name=%s shield_stock=%d shield_cost=%d buy_cost=%d can_buy=%d can_change=%d citizen_tile_count=%d specialist_type_count=%d worklist_length=%d build_choice_count=%d improvement_count=%d trade_route_count=%d trade_route_capacity=%u did_sell=%d allow_disband=%d new_citizens=%s options_conflict=%d airlift_remaining=%d airlift_max=%d governor_enabled=%d citizen_happy=%d citizen_content=%d citizen_unhappy=%d citizen_angry=%d citizen_workers=%d citizen_specialists=%d food_stock=%d granary_size=%d growth_turns=%d pollution=%d food_citizen_base=%d food_net=%d food_surplus=%d food_usage=%d food_waste=%d food_unhappy_penalty=%d shield_citizen_base=%d shield_net=%d shield_surplus=%d shield_usage=%d shield_waste=%d shield_unhappy_penalty=%d trade_citizen_base=%d trade_net=%d trade_surplus=%d trade_usage=%d trade_waste=%d trade_unhappy_penalty=%d gold_citizen_base=%d gold_net=%d gold_surplus=%d gold_usage=%d gold_waste=%d gold_unhappy_penalty=%d luxury_citizen_base=%d luxury_net=%d luxury_surplus=%d luxury_usage=%d luxury_waste=%d luxury_unhappy_penalty=%d science_citizen_base=%d science_net=%d science_surplus=%d science_usage=%d science_waste=%d science_unhappy_penalty=%d"
      ],
      [
        "city_site",
        "city_site ref=%s owner=%s name=%s tile=%d x=%d y=%d size=%d visibility=%s"
      ],
      [
        "city_tile",
        "city_tile city=%s tile=%d worked=%d free_worked=%d can_work=%d food=%d shields=%d trade=%d gold=%d luxury=%d science=%d"
      ],
      [
        "city_worker_task",
        "city_worker_task city=%s tile=%d activity=%s target_extra=%d target_extra_name=%s want=%d"
      ],
      [
        "city_specialist",
        "city_specialist city=%s specialist=%d name=%s count=%d counts_toward_population=%d can_use=%d is_default=%d food=%d shields=%d trade=%d gold=%d luxury=%d science=%d"
      ],
      [
        "city_worklist",
        "city_worklist city=%s position=%d production_kind=%s production_id=%d production_name=%s"
      ],
      [
        "city_build_choice",
        "city_build_choice city=%s production_kind=%s production_id=%d production_name=%s can_queue=%d can_build_now=%d shield_cost=%d shield_stock_after_change=%d turns=%d turns_with_stock=%d upkeep_food=%d upkeep_shield=%d upkeep_trade=%d upkeep_gold=%d upkeep_luxury=%d upkeep_science=%d happy_cost=%d unit_attack=%d unit_defense=%d unit_move_rate=%d unit_hp=%d unit_firepower=%d unit_vision_radius_sq=%d unit_transport_capacity=%d unit_fuel=%d unit_pop_cost=%d unit_bombard_rate=%d unit_city_size=%d unit_paradrop_range=%d building_genus=%s building_obsolete=%d building_redundant=%d building_convert=%d building_allows_units=%d building_allows_extras=%d building_prevents_disaster=%d building_protects_vs_actions=%d building_allows_actions=%d"
      ],
      [
        "city_improvement",
        "city_improvement city=%s improvement_id=%d name=%s sellable=%d sell_price=%d"
      ],
      [
        "city_trade_route",
        "city_trade_route city=%s position=%d partner=%s partner_visibility=%s partner_name=%s base_value=%d effective_value=%d direction=%s goods_id=%d goods_name=%s"
      ],
      [
        "investigation",
        "investigation city=%s lifecycle=%llu tile=%d name=%s size=%d production_kind=%s production_id=%d production_name=%s shield_stock=%d shield_surplus=%d improvement_count=%d feeling_count=%d specialist_count=%d"
      ],
      [
        "investigation_improvement",
        "investigation_improvement city=%s improvement_id=%d name=%s"
      ],
      [
        "investigation_citizens",
        "investigation_citizens city=%s stage=%d happy=%d content=%d unhappy=%d angry=%d"
      ],
      [
        "investigation_specialist",
        "investigation_specialist city=%s specialist=%d name=%s count=%d"
      ],
      [
        "city_rally",
        "city_rally city=%s active=%d persistent=%d vigilant=%d order_count=%d orders_digest=%s"
      ],
      [
        "city_governor",
        "city_governor city=%s min_food=%d min_production=%d min_trade=%d min_gold=%d min_luxury=%d min_science=%d weight_food=%d weight_production=%d weight_trade=%d weight_gold=%d weight_luxury=%d weight_science=%d celebration_weight=%d require_happy=%d maximize_growth=%d"
      ],
      [
        "unit_own",
        "unit ref=%s scope=own owner=%s type_id=%d type=%s home_city=%s converts_to_id=%d converts_to=%s tile=%d x=%d y=%d hp=%d veteran=%d veteran_name=%s veteran_levels=%d veteran_power=%d veteran_move_bonus=%d fuel=%d max_hp=%d max_fuel=%d move_rate=%d attack=%d defense=%d firepower=%d base_upkeep_food=%d base_upkeep_shield=%d base_upkeep_trade=%d base_upkeep_gold=%d base_upkeep_luxury=%d base_upkeep_science=%d upkeep_food=%d upkeep_shield=%d upkeep_trade=%d upkeep_gold=%d upkeep_luxury=%d upkeep_science=%d moves=%d activity=%s activity_target=%d activity_target_name=%s activity_progress=%d transport_state=%s transporter=%s transport_capacity=%d occupied=%d paradropped=%d paradrop_range=%d controller=%s has_orders=%d orders_repeat=%d orders_vigilant=%d order_count=%d orders_digest=%s orders_destination=%d action_decision_want=%s action_decision_tile=%d"
      ],
      [
        "unit_route",
        "unit_route unit=%s order_index=%d reconstructable=%d step_count=%d"
      ],
      [
        "unit_route_step",
        "unit_route_step unit=%s sequence=%d kind=%s tile=%d"
      ],
      [
        "unit_visible",
        "unit ref=%s scope=visible owner=%s type_id=%d type=%s tile=%d x=%d y=%d hp=%d veteran=%d veteran_name=%s veteran_levels=%d veteran_power=%d veteran_move_bonus=%d max_hp=%d max_fuel=%d move_rate=%d attack=%d defense=%d firepower=%d base_upkeep_food=%d base_upkeep_shield=%d base_upkeep_trade=%d base_upkeep_gold=%d base_upkeep_luxury=%d base_upkeep_science=%d"
      ],
      [
        "tombstone",
        "tombstone ref=%c:%d:%llu kind=%s"
      ],
      [
        "chat",
        "chat sequence=%llu turn=%d phase=%d sender=%s sender_name=%s self=%d channel=%s event=%s truncated=%d message=%s"
      ],
      [
        "chat_recipient",
        "chat_recipient ref=%s name=%s self=%d connected=%d can_message=%d"
      ],
      [
        "action",
        "action slot=%s kind=%s actor=%s counterpart=%s meeting_generation=%llu clauses_digest=%s self_accepted=%d other_accepted=%d relation_state=%s outgoing_vision=%d outgoing_shared_tiles=%d clause_giver=%s clause_type=%s clause_value=%d clause_name=%s desired_acceptance=%d target_tile=%d source_city=%s destination_city=%s target_unit=%s transport_context=%s target_tech=%d vote_no=%d server_setting_id=%d server_setting_type=%s server_setting_min=%d server_setting_max=%d server_setting_current=%d server_setting_value=%d target_government=%d max_rate=%d route_waypoint_limit=%d infrastructure_cost=%d infrastructure_turns=%d infrastructure_choice_count=%d infrastructure_choices=%s target_build_kind=%s target_build=%d spaceship_part=%s spaceship_value=%d target_multiplier=%d multiplier_value=%d source_specialist=%d target_specialist=%d target_extra=%d subtarget_kind=%s subresults=%s activity=%s target_name=%s native_rule=%s target_kind=%s result=%s actor_consuming_always=%d legality=%s probability_kind=%s probability_min=%d probability_max=%d gold_cost=%d args=%s"
      ]
    ],
    "action_rule_fields": [
      "native_kind",
      "public_kind",
      "operation",
      "variant",
      "target_kind",
      "result",
      "consuming",
      "args"
    ],
    "action_rules": [
      [
        "Airlift Unit",
        "unit.airlift",
        "unit.perform_action",
        "airlift",
        "opaque",
        "City",
        "Unit Airlift",
        false,
        "none"
      ],
      [
        "Attack",
        "unit.attack",
        "unit.perform_action",
        "attack",
        "standard",
        "Stack",
        "Unit Attack",
        false,
        "none"
      ],
      [
        "Attack 2",
        "unit.attack",
        "unit.perform_action",
        "attack",
        "alternative_2",
        "Stack",
        "Unit Attack",
        false,
        "none"
      ],
      [
        "Convert Unit",
        "unit.convert",
        "unit.perform_action",
        "convert",
        "opaque",
        "Self",
        "Conversion Installed",
        false,
        "none"
      ],
      [
        "Disband Unit",
        "unit.disband",
        "unit.perform_action",
        "disband",
        "opaque",
        "Self",
        "Unit Disbanded",
        true,
        "none"
      ],
      [
        "Disband Unit Recover",
        "unit.disband_recover",
        "unit.perform_action",
        "disband_recover",
        "opaque",
        "City",
        "Unit Disband Recover",
        true,
        "none"
      ],
      [
        "Enter Marketplace",
        "unit.marketplace",
        "unit.perform_action",
        "marketplace",
        "opaque",
        "City",
        "Unit Enter Marketplace",
        true,
        "none"
      ],
      [
        "Establish Trade Route",
        "unit.establish_trade",
        "unit.perform_action",
        "establish_trade",
        "opaque",
        "City",
        "Unit Establish Trade Route",
        true,
        "none"
      ],
      [
        "Fortify",
        "unit.fortify",
        "unit.perform_action",
        "fortify",
        "opaque",
        "Self",
        "Fortify Installed",
        false,
        "none"
      ],
      [
        "Fortify 2",
        "unit.fortify",
        "unit.perform_action",
        "fortify",
        "opaque",
        "Self",
        "Fortify Installed",
        false,
        "none"
      ],
      [
        "Found City",
        "city.found",
        "unit.perform_action",
        "found_city",
        "standard",
        "Tile",
        "Unit Found City",
        true,
        "city_name-required"
      ],
      [
        "Help Wonder",
        "unit.help_wonder",
        "unit.perform_action",
        "help_wonder",
        "opaque",
        "City",
        "Unit Help Wonder",
        true,
        "none"
      ],
      [
        "Home City",
        "unit.rehome",
        "unit.perform_action",
        "rehome",
        "opaque",
        "City",
        "Unit Home City",
        false,
        "none"
      ],
      [
        "Join City",
        "unit.join_city",
        "unit.perform_action",
        "join_city",
        "opaque",
        "City",
        "Unit Join City",
        true,
        "none"
      ],
      [
        "Paradrop Unit",
        "unit.paradrop",
        "unit.perform_action",
        "paradrop",
        "opaque",
        "Tile",
        "Unit Paradrop",
        false,
        "none"
      ],
      [
        "Paradrop Unit Enter",
        "unit.paradrop",
        "unit.perform_action",
        "paradrop",
        "opaque",
        "Tile",
        "Unit Paradrop",
        false,
        "none"
      ],
      [
        "Paradrop Unit Frighten",
        "unit.paradrop",
        "unit.perform_action",
        "paradrop",
        "opaque",
        "Tile",
        "Unit Paradrop",
        false,
        "none"
      ],
      [
        "Suicide Attack",
        "unit.attack",
        "unit.perform_action",
        "suicide_attack",
        "standard",
        "Stack",
        "Unit Attack",
        true,
        "none"
      ],
      [
        "Suicide Attack 2",
        "unit.attack",
        "unit.perform_action",
        "suicide_attack",
        "alternative_2",
        "Stack",
        "Unit Attack",
        true,
        "none"
      ],
      [
        "Teleport",
        "unit.teleport",
        "unit.perform_action",
        "teleport",
        "opaque",
        "Tile",
        "Teleport",
        false,
        "none"
      ],
      [
        "Teleport Enter",
        "unit.teleport",
        "unit.perform_action",
        "teleport",
        "opaque",
        "Tile",
        "Teleport",
        false,
        "none"
      ],
      [
        "Teleport Frighten",
        "unit.teleport",
        "unit.perform_action",
        "teleport",
        "opaque",
        "Tile",
        "Teleport",
        false,
        "none"
      ],
      [
        "Teleport2",
        "unit.teleport",
        "unit.perform_action",
        "teleport",
        "opaque",
        "Tile",
        "Teleport",
        false,
        "none"
      ],
      [
        "Teleport3",
        "unit.teleport",
        "unit.perform_action",
        "teleport",
        "opaque",
        "Tile",
        "Teleport",
        false,
        "none"
      ],
      [
        "Transport Board",
        "unit.board",
        "unit.perform_action",
        "board",
        "opaque",
        "Unit",
        "Unit Transport Board",
        false,
        "none"
      ],
      [
        "Transport Board 2",
        "unit.board",
        "unit.perform_action",
        "board",
        "opaque",
        "Unit",
        "Unit Transport Board",
        false,
        "none"
      ],
      [
        "Transport Board_3",
        "unit.board",
        "unit.perform_action",
        "board",
        "opaque",
        "Unit",
        "Unit Transport Board",
        false,
        "none"
      ],
      [
        "Transport Deboard",
        "unit.deboard",
        "unit.perform_action",
        "deboard",
        "opaque",
        "Unit",
        "Unit Transport Deboard",
        false,
        "none"
      ],
      [
        "Transport Disembark",
        "unit.disembark",
        "unit.perform_action",
        "disembark",
        "opaque",
        "Tile",
        "Unit Transport Disembark",
        false,
        "none"
      ],
      [
        "Transport Disembark 2",
        "unit.disembark",
        "unit.perform_action",
        "disembark",
        "opaque",
        "Tile",
        "Unit Transport Disembark",
        false,
        "none"
      ],
      [
        "Transport Disembark 3",
        "unit.disembark",
        "unit.perform_action",
        "disembark",
        "opaque",
        "Tile",
        "Unit Transport Disembark",
        false,
        "none"
      ],
      [
        "Transport Disembark 4",
        "unit.disembark",
        "unit.perform_action",
        "disembark",
        "opaque",
        "Tile",
        "Unit Transport Disembark",
        false,
        "none"
      ],
      [
        "Transport Embark",
        "unit.embark",
        "unit.perform_action",
        "embark",
        "opaque",
        "Unit",
        "Unit Transport Embark",
        false,
        "none"
      ],
      [
        "Transport Embark 2",
        "unit.embark",
        "unit.perform_action",
        "embark",
        "opaque",
        "Unit",
        "Unit Transport Embark",
        false,
        "none"
      ],
      [
        "Transport Embark 3",
        "unit.embark",
        "unit.perform_action",
        "embark",
        "opaque",
        "Unit",
        "Unit Transport Embark",
        false,
        "none"
      ],
      [
        "Transport Embark 4",
        "unit.embark",
        "unit.perform_action",
        "embark",
        "opaque",
        "Unit",
        "Unit Transport Embark",
        false,
        "none"
      ],
      [
        "Transport Load",
        "unit.load",
        "unit.perform_action",
        "load",
        "opaque",
        "Unit",
        "Unit Transport Load",
        false,
        "none"
      ],
      [
        "Transport Load 2",
        "unit.load",
        "unit.perform_action",
        "load",
        "opaque",
        "Unit",
        "Unit Transport Load",
        false,
        "none"
      ],
      [
        "Transport Load 3",
        "unit.load",
        "unit.perform_action",
        "load",
        "opaque",
        "Unit",
        "Unit Transport Load",
        false,
        "none"
      ],
      [
        "Transport Unload",
        "unit.unload",
        "unit.perform_action",
        "unload",
        "opaque",
        "Unit",
        "Unit Transport Unload",
        false,
        "none"
      ],
      [
        "Unit Make Homeless",
        "unit.homeless",
        "unit.perform_action",
        "make_homeless",
        "opaque",
        "Self",
        "Home City Cleared",
        false,
        "none"
      ],
      [
        "Unit Move",
        "unit.move",
        "unit.order",
        "move",
        "standard",
        "Tile",
        "Unit Move",
        false,
        "none"
      ],
      [
        "Unit Move 2",
        "unit.move",
        "unit.order",
        "move",
        "alternative_2",
        "Tile",
        "Unit Move",
        false,
        "none"
      ],
      [
        "Unit Move 3",
        "unit.move",
        "unit.order",
        "move",
        "alternative_3",
        "Tile",
        "Unit Move",
        false,
        "none"
      ],
      [
        "Upgrade Unit",
        "unit.upgrade",
        "unit.perform_action",
        "upgrade",
        "opaque",
        "City",
        "Unit Upgrade",
        false,
        "none"
      ],
      [
        "city.buy_production",
        "city.buy_production",
        "city.buy_production",
        "buy_production",
        "standard",
        "Production",
        "Production Bought",
        false,
        "none"
      ],
      [
        "city.change_worker_task",
        "city.change_worker_task",
        "city.manage_worker_task",
        "change_worker_task",
        "standard",
        "City Worker Task",
        "Worker Task Changed",
        false,
        "none"
      ],
      [
        "city.clear_governor",
        "city.clear_governor",
        "city.set_governor",
        "clear_governor",
        "standard",
        "City",
        "Governor Cleared",
        false,
        "none"
      ],
      [
        "city.clear_rally",
        "city.clear_rally",
        "city.set_rally",
        "clear_rally",
        "opaque",
        "City",
        "Rally Point Cleared",
        false,
        "none"
      ],
      [
        "city.remove_worker_task",
        "city.remove_worker_task",
        "city.manage_worker_task",
        "remove_worker_task",
        "standard",
        "City Worker Task",
        "Worker Task Removed",
        false,
        "none"
      ],
      [
        "city.rename",
        "city.rename",
        "city.rename",
        "rename",
        "standard",
        "City",
        "City Renamed",
        false,
        "city_name-required"
      ],
      [
        "city.request_worker_task",
        "city.request_worker_task",
        "city.manage_worker_task",
        "request_worker_task",
        "standard",
        "City Worker Task",
        "Worker Task Requested",
        false,
        "none"
      ],
      [
        "city.sell_improvement",
        "city.sell_improvement",
        "city.sell_improvement",
        "sell_improvement",
        "standard",
        "Improvement",
        "Improvement Sold",
        false,
        "none"
      ],
      [
        "city.set_governor",
        "city.set_governor",
        "city.set_governor",
        "set_governor",
        "standard",
        "City",
        "Governor Goal Set",
        false,
        "governor-goal-required"
      ],
      [
        "city.set_options",
        "city.set_options",
        "city.set_options",
        "set_options",
        "standard",
        "City",
        "City Options Changed",
        false,
        "city-options-required"
      ],
      [
        "city.set_production",
        "city.set_production",
        "city.set_production",
        "set_production",
        "standard",
        "Production",
        "Production Changed",
        false,
        "none"
      ],
      [
        "city.set_rally",
        "city.set_rally",
        "city.set_rally",
        "set_rally",
        "opaque",
        "Tile",
        "Rally Point Set",
        false,
        "persistent-required"
      ],
      [
        "city.set_specialist",
        "city.set_specialist",
        "city.set_specialist",
        "set_specialist",
        "standard",
        "Specialist",
        "Specialist Changed",
        false,
        "none"
      ],
      [
        "city.set_worklist",
        "city.set_worklist",
        "city.set_worklist",
        "set_worklist",
        "standard",
        "City",
        "Worklist Changed",
        false,
        "worklist-required"
      ],
      [
        "city.unwork_tile",
        "city.unwork_tile",
        "city.assign_citizen",
        "unwork_tile",
        "standard",
        "City Tile",
        "Citizen Unassigned",
        false,
        "none"
      ],
      [
        "city.work_tile",
        "city.work_tile",
        "city.assign_citizen",
        "work_tile",
        "standard",
        "City Tile",
        "Citizen Assigned",
        false,
        "none"
      ],
      [
        "diplomacy.accept",
        "diplomacy.accept",
        "diplomacy.acceptance",
        "accept",
        "standard",
        "Diplomatic Relation",
        "Acceptance Recorded",
        false,
        "none"
      ],
      [
        "diplomacy.break_relation",
        "diplomacy.break_relation",
        "diplomacy.relation",
        "break_relation",
        "standard",
        "Diplomatic Relation",
        "Relation Changed",
        false,
        "none"
      ],
      [
        "diplomacy.close_meeting",
        "diplomacy.close_meeting",
        "diplomacy.meeting",
        "close_meeting",
        "standard",
        "Diplomatic Relation",
        "Meeting Closed",
        false,
        "none"
      ],
      [
        "diplomacy.open_meeting",
        "diplomacy.open_meeting",
        "diplomacy.meeting",
        "open_meeting",
        "standard",
        "Diplomatic Relation",
        "Meeting Opened",
        false,
        "none"
      ],
      [
        "diplomacy.propose_clause",
        "diplomacy.propose_clause",
        "diplomacy.clause",
        "propose_clause",
        "standard",
        "Diplomatic Relation",
        "Clause Proposed",
        false,
        "none"
      ],
      [
        "diplomacy.propose_gold",
        "diplomacy.propose_clause",
        "diplomacy.clause",
        "propose_clause",
        "standard",
        "Diplomatic Relation",
        "Clause Proposed",
        false,
        "gold-required"
      ],
      [
        "diplomacy.remove_clause",
        "diplomacy.remove_clause",
        "diplomacy.clause",
        "remove_clause",
        "standard",
        "Diplomatic Relation",
        "Clause Removed",
        false,
        "none"
      ],
      [
        "diplomacy.withdraw_acceptance",
        "diplomacy.withdraw_acceptance",
        "diplomacy.acceptance",
        "withdraw_acceptance",
        "standard",
        "Diplomatic Relation",
        "Acceptance Withdrawn",
        false,
        "none"
      ],
      [
        "diplomacy.withdraw_shared_tiles",
        "diplomacy.withdraw_shared_tiles",
        "diplomacy.withdraw",
        "withdraw_shared_tiles",
        "standard",
        "Diplomatic Relation",
        "Shared Tiles Withdrawn",
        false,
        "none"
      ],
      [
        "diplomacy.withdraw_vision",
        "diplomacy.withdraw_vision",
        "diplomacy.withdraw",
        "withdraw_vision",
        "standard",
        "Diplomatic Relation",
        "Vision Withdrawn",
        false,
        "none"
      ],
      [
        "economy.set_rates",
        "economy.set_rates",
        "economy.set_rates",
        "set_rates",
        "standard",
        "Player",
        "Economic Rates",
        false,
        "rates-required"
      ],
      [
        "government.change",
        "government.change",
        "government.change",
        "change",
        "standard",
        "Government",
        "Government Choice Recorded",
        false,
        "none"
      ],
      [
        "government.revolution",
        "government.revolution",
        "government.revolution",
        "revolution",
        "standard",
        "Government",
        "Revolution Started",
        false,
        "none"
      ],
      [
        "phase.end",
        "phase.end",
        "phase.end",
        "end",
        "standard",
        "player",
        "phase_end",
        false,
        "none"
      ],
      [
        "player.cancel_vote",
        "player.cancel_vote",
        "player.cancel_vote",
        "cancel_vote",
        "standard",
        "Vote",
        "Vote Cancelled",
        false,
        "none"
      ],
      [
        "player.cast_vote",
        "player.cast_vote",
        "player.cast_vote",
        "cast_vote",
        "standard",
        "Vote",
        "Vote Recorded",
        false,
        "vote-required"
      ],
      [
        "player.place_infrastructure",
        "player.place_infrastructure",
        "player.set_infrastructure",
        "place_infrastructure",
        "opaque",
        "Tile",
        "Infrastructure Placement Started",
        false,
        "infrastructure-extra-required"
      ],
      [
        "player.propose_server_setting_bitwise",
        "player.propose_server_setting",
        "player.propose_server_setting",
        "propose_server_setting",
        "bitwise",
        "Server Setting Vote",
        "Vote Proposed Or Setting Applied",
        false,
        "server-setting-bitwise-required"
      ],
      [
        "player.propose_server_setting_boolean",
        "player.propose_server_setting",
        "player.propose_server_setting",
        "propose_server_setting",
        "boolean",
        "Server Setting Vote",
        "Vote Proposed Or Setting Applied",
        false,
        "none"
      ],
      [
        "player.propose_server_setting_enum",
        "player.propose_server_setting",
        "player.propose_server_setting",
        "propose_server_setting",
        "enum",
        "Server Setting Vote",
        "Vote Proposed Or Setting Applied",
        false,
        "none"
      ],
      [
        "player.propose_server_setting_integer",
        "player.propose_server_setting",
        "player.propose_server_setting",
        "propose_server_setting",
        "integer",
        "Server Setting Vote",
        "Vote Proposed Or Setting Applied",
        false,
        "server-setting-integer-required"
      ],
      [
        "player.propose_server_setting_string",
        "player.propose_server_setting",
        "player.propose_server_setting",
        "propose_server_setting",
        "string",
        "Server Setting Vote",
        "Vote Proposed Or Setting Applied",
        false,
        "server-setting-string-required"
      ],
      [
        "player.send_chat",
        "player.send_chat",
        "player.send_chat",
        "send_chat",
        "standard",
        "Chat Channel",
        "Chat Echo Received",
        false,
        "chat-required"
      ],
      [
        "player.set_multiplier",
        "player.set_multiplier",
        "player.set_multiplier",
        "set_multiplier",
        "standard",
        "Multiplier",
        "Multiplier Target Changed",
        false,
        "multiplier-value-required"
      ],
      [
        "player.surrender",
        "player.surrender",
        "player.surrender",
        "surrender",
        "standard",
        "Player",
        "Surrender Recorded",
        false,
        "none"
      ],
      [
        "pregame.configure",
        "pregame.configure",
        "pregame.configure",
        "configure",
        "standard",
        "Pregame Configuration",
        "Configuration Changed",
        false,
        "pregame-config-required"
      ],
      [
        "pregame.set_ready",
        "pregame.set_ready",
        "pregame.set_ready",
        "set_ready",
        "standard",
        "Pregame Readiness",
        "Readiness Changed",
        false,
        "pregame-ready-required"
      ],
      [
        "pregame.set_team",
        "pregame.set_team",
        "pregame.set_team",
        "set_team",
        "standard",
        "Pregame Team",
        "Team Changed",
        false,
        "pregame-team-required"
      ],
      [
        "research.set_goal",
        "research.set_goal",
        "research.set_goal",
        "set_goal",
        "standard",
        "Technology",
        "Research Goal",
        false,
        "none"
      ],
      [
        "research.set_target",
        "research.set_target",
        "research.set_target",
        "set_target",
        "standard",
        "Technology",
        "Research Target",
        false,
        "none"
      ],
      [
        "spaceship.launch",
        "spaceship.launch",
        "spaceship.launch",
        "launch",
        "standard",
        "Spaceship",
        "Spaceship Launched",
        false,
        "none"
      ],
      [
        "spaceship.place_component",
        "spaceship.place_component",
        "spaceship.place_component",
        "place_component",
        "standard",
        "Spaceship Part",
        "Spaceship Part Placed",
        false,
        "none"
      ],
      [
        "unit.attack_route",
        "unit.attack_route",
        "unit.order",
        "attack_route",
        "opaque",
        "Attack Route",
        "Orders Queued",
        false,
        "attack-route-required"
      ],
      [
        "unit.auto_explore",
        "unit.auto_explore",
        "unit.order",
        "auto_explore",
        "opaque",
        "Unit",
        "Auto Explore Installed",
        false,
        "none"
      ],
      [
        "unit.auto_work",
        "unit.auto_work",
        "unit.order",
        "auto_work",
        "opaque",
        "Unit",
        "Auto Work Installed",
        false,
        "none"
      ],
      [
        "unit.cancel_activity",
        "unit.cancel_activity",
        "unit.order",
        "cancel_activity",
        "standard",
        "Unit",
        "Activity Cancelled",
        false,
        "none"
      ],
      [
        "unit.cancel_automation",
        "unit.cancel_automation",
        "unit.order",
        "cancel_automation",
        "opaque",
        "Unit",
        "Automation Cancelled",
        false,
        "none"
      ],
      [
        "unit.cancel_orders",
        "unit.cancel_orders",
        "unit.order",
        "cancel_orders",
        "opaque",
        "Unit",
        "Orders Cancelled",
        false,
        "none"
      ],
      [
        "unit.clear_action_decision",
        "unit.clear_action_decision",
        "unit.order",
        "clear_action_decision",
        "opaque",
        "Action Decision",
        "Action Decision Cleared",
        false,
        "none"
      ],
      [
        "unit.connect_route",
        "unit.connect_route",
        "unit.order",
        "connect_route",
        "opaque",
        "Construction Route",
        "Orders Queued",
        false,
        "none"
      ],
      [
        "unit.goto",
        "unit.goto",
        "unit.order",
        "goto",
        "opaque",
        "Tile",
        "Orders Queued",
        false,
        "none"
      ],
      [
        "unit.goto_and_perform",
        "unit.goto_and_perform",
        "unit.order",
        "goto_and_perform",
        "opaque",
        "Action Route",
        "Orders Queued",
        false,
        "none"
      ],
      [
        "unit.sentry",
        "unit.sentry",
        "unit.order",
        "sentry",
        "opaque",
        "Unit",
        "Sentry Installed",
        false,
        "none"
      ],
      [
        "unit.set_route",
        "unit.set_route",
        "unit.order",
        "set_route",
        "opaque",
        "Route",
        "Orders Queued",
        false,
        "route-required"
      ],
      [
        "unit.start_activity",
        "unit.start_activity",
        "unit.perform_action",
        "start_activity",
        "standard",
        "Worker Activity",
        "Activity Installed",
        false,
        "none"
      ]
    ],
    "special_action_contract": [
      {
        "native_result": "Collect Ransom",
        "target_kind": "Stack",
        "operation": "collect_ransom",
        "cost": "none",
        "subtarget": "none",
        "subresult_sets": [
          []
        ],
        "probability_policy": "resolved",
        "native_rules": []
      },
      {
        "native_result": "Teleport Conquer",
        "target_kind": "Tile",
        "operation": "teleport_conquer",
        "cost": "none",
        "subtarget": "none",
        "subresult_sets": [
          [],
          [
            "hut_enter"
          ],
          [
            "hut_frighten"
          ]
        ],
        "probability_policy": "resolved",
        "native_rules": []
      },
      {
        "native_result": "Unit Bombard",
        "target_kind": "Stack",
        "operation": "bombard",
        "cost": "none",
        "subtarget": "none",
        "subresult_sets": [
          [
            "non_lethal"
          ]
        ],
        "probability_policy": "resolved",
        "native_rules": []
      },
      {
        "native_result": "Unit Bribe Stack",
        "target_kind": "Stack",
        "operation": "bribe_stack",
        "cost": "quoted_maximum",
        "subtarget": "none",
        "subresult_sets": [
          []
        ],
        "probability_policy": "resolved",
        "native_rules": [
          "Bribe Stack"
        ]
      },
      {
        "native_result": "Unit Bribe Unit",
        "target_kind": "Unit",
        "operation": "bribe_unit",
        "cost": "quoted_maximum",
        "subtarget": "none",
        "subresult_sets": [
          []
        ],
        "probability_policy": "resolved",
        "native_rules": [
          "Bribe Unit"
        ]
      },
      {
        "native_result": "Unit Capture Units",
        "target_kind": "Stack",
        "operation": "capture_units",
        "cost": "none",
        "subtarget": "none",
        "subresult_sets": [
          []
        ],
        "probability_policy": "resolved",
        "native_rules": []
      },
      {
        "native_result": "Unit Conquer City",
        "target_kind": "City",
        "operation": "conquer_city",
        "cost": "none",
        "subtarget": "none",
        "subresult_sets": [
          []
        ],
        "probability_policy": "resolved",
        "native_rules": []
      },
      {
        "native_result": "Unit Conquer Extras",
        "target_kind": "Extras",
        "operation": "conquer_extras",
        "cost": "none",
        "subtarget": "none",
        "subresult_sets": [
          []
        ],
        "probability_policy": "resolved",
        "native_rules": []
      },
      {
        "native_result": "Unit Destroy City",
        "target_kind": "City",
        "operation": "destroy_city",
        "cost": "none",
        "subtarget": "none",
        "subresult_sets": [
          []
        ],
        "probability_policy": "resolved",
        "native_rules": []
      },
      {
        "native_result": "Unit Enter Hut",
        "target_kind": "Tile",
        "operation": "enter_hut",
        "cost": "none",
        "subtarget": "none",
        "subresult_sets": [
          [
            "hut_enter"
          ]
        ],
        "probability_policy": "unresolved",
        "native_rules": [
          "Enter Hut",
          "Enter Hut 2",
          "Enter Hut 3",
          "Enter Hut 4"
        ]
      },
      {
        "native_result": "Unit Establish Embassy",
        "target_kind": "City",
        "operation": "establish_embassy",
        "cost": "none",
        "subtarget": "none",
        "subresult_sets": [
          []
        ],
        "probability_policy": "resolved",
        "native_rules": []
      },
      {
        "native_result": "Unit Expel Unit",
        "target_kind": "Unit",
        "operation": "expel_unit",
        "cost": "none",
        "subtarget": "none",
        "subresult_sets": [
          []
        ],
        "probability_policy": "resolved",
        "native_rules": []
      },
      {
        "native_result": "Unit Frighten Hut",
        "target_kind": "Tile",
        "operation": "frighten_hut",
        "cost": "none",
        "subtarget": "none",
        "subresult_sets": [
          [
            "hut_frighten"
          ]
        ],
        "probability_policy": "unresolved",
        "native_rules": [
          "Frighten Hut",
          "Frighten Hut 2",
          "Frighten Hut 3",
          "Frighten Hut 4"
        ]
      },
      {
        "native_result": "Unit Heal Unit",
        "target_kind": "Unit",
        "operation": "heal_unit",
        "cost": "none",
        "subtarget": "none",
        "subresult_sets": [
          []
        ],
        "probability_policy": "resolved",
        "native_rules": []
      },
      {
        "native_result": "Unit Incite City",
        "target_kind": "City",
        "operation": "incite_city",
        "cost": "quoted_maximum",
        "subtarget": "none",
        "subresult_sets": [
          []
        ],
        "probability_policy": "unresolved",
        "native_rules": [
          "Incite City",
          "Incite City Escape"
        ]
      },
      {
        "native_result": "Unit Investigate City",
        "target_kind": "City",
        "operation": "investigate_city",
        "cost": "none",
        "subtarget": "none",
        "subresult_sets": [
          []
        ],
        "probability_policy": "resolved",
        "native_rules": []
      },
      {
        "native_result": "Unit Nuke",
        "target_kind": "City",
        "operation": "nuke_city",
        "cost": "none",
        "subtarget": "none",
        "subresult_sets": [
          []
        ],
        "probability_policy": "resolved",
        "native_rules": []
      },
      {
        "native_result": "Unit Nuke",
        "target_kind": "Tile",
        "operation": "nuke_tile",
        "cost": "none",
        "subtarget": "none",
        "subresult_sets": [
          []
        ],
        "probability_policy": "resolved",
        "native_rules": []
      },
      {
        "native_result": "Unit Nuke Units",
        "target_kind": "Stack",
        "operation": "nuke_units",
        "cost": "none",
        "subtarget": "none",
        "subresult_sets": [
          []
        ],
        "probability_policy": "resolved",
        "native_rules": []
      },
      {
        "native_result": "Unit Paradrop Conquer",
        "target_kind": "Tile",
        "operation": "paradrop_conquer",
        "cost": "none",
        "subtarget": "none",
        "subresult_sets": [
          [],
          [
            "may_embark"
          ],
          [
            "hut_enter"
          ],
          [
            "hut_enter",
            "may_embark"
          ],
          [
            "hut_frighten"
          ],
          [
            "hut_frighten",
            "may_embark"
          ]
        ],
        "probability_policy": "unresolved",
        "native_rules": [
          "Paradrop Unit Conquer",
          "Paradrop Unit Frighten Conquer",
          "Paradrop Unit Enter Conquer"
        ]
      },
      {
        "native_result": "Unit Poison City",
        "target_kind": "City",
        "operation": "poison_city",
        "cost": "none",
        "subtarget": "none",
        "subresult_sets": [
          []
        ],
        "probability_policy": "resolved",
        "native_rules": []
      },
      {
        "native_result": "Unit Sabotage City",
        "target_kind": "City",
        "operation": "sabotage_city",
        "cost": "none",
        "subtarget": "none",
        "subresult_sets": [
          []
        ],
        "probability_policy": "unresolved",
        "native_rules": [
          "Sabotage City",
          "Sabotage City Escape"
        ]
      },
      {
        "native_result": "Unit Sabotage City Production",
        "target_kind": "City",
        "operation": "sabotage_production",
        "cost": "none",
        "subtarget": "none",
        "subresult_sets": [
          []
        ],
        "probability_policy": "unresolved",
        "native_rules": [
          "Sabotage City Production Escape"
        ]
      },
      {
        "native_result": "Unit Sabotage Unit",
        "target_kind": "Unit",
        "operation": "sabotage_unit",
        "cost": "none",
        "subtarget": "none",
        "subresult_sets": [
          []
        ],
        "probability_policy": "resolved",
        "native_rules": []
      },
      {
        "native_result": "Unit Spread Plague",
        "target_kind": "City",
        "operation": "spread_plague",
        "cost": "none",
        "subtarget": "none",
        "subresult_sets": [
          []
        ],
        "probability_policy": "resolved",
        "native_rules": []
      },
      {
        "native_result": "Unit Spy Attack",
        "target_kind": "Stack",
        "operation": "spy_attack",
        "cost": "none",
        "subtarget": "none",
        "subresult_sets": [
          []
        ],
        "probability_policy": "resolved",
        "native_rules": []
      },
      {
        "native_result": "Unit Spy Escape",
        "target_kind": "City",
        "operation": "spy_escape",
        "cost": "none",
        "subtarget": "none",
        "subresult_sets": [
          []
        ],
        "probability_policy": "resolved",
        "native_rules": []
      },
      {
        "native_result": "Unit Steal Gold",
        "target_kind": "City",
        "operation": "steal_gold",
        "cost": "none",
        "subtarget": "none",
        "subresult_sets": [
          []
        ],
        "probability_policy": "resolved",
        "native_rules": []
      },
      {
        "native_result": "Unit Steal Maps",
        "target_kind": "City",
        "operation": "steal_maps",
        "cost": "none",
        "subtarget": "none",
        "subresult_sets": [
          []
        ],
        "probability_policy": "resolved",
        "native_rules": []
      },
      {
        "native_result": "Unit Steal Tech",
        "target_kind": "City",
        "operation": "steal_technology",
        "cost": "none",
        "subtarget": "none",
        "subresult_sets": [
          []
        ],
        "probability_policy": "unresolved",
        "native_rules": [
          "Steal Tech",
          "Steal Tech Escape Expected"
        ]
      },
      {
        "native_result": "Unit Suitcase Nuke",
        "target_kind": "City",
        "operation": "suitcase_nuke",
        "cost": "none",
        "subtarget": "none",
        "subresult_sets": [
          []
        ],
        "probability_policy": "resolved",
        "native_rules": []
      },
      {
        "native_result": "Unit Surgical Strike Building",
        "target_kind": "City",
        "operation": "strike_building",
        "cost": "none",
        "subtarget": "building",
        "subresult_sets": [
          []
        ],
        "probability_policy": "resolved",
        "native_rules": [
          "Surgical Strike Building"
        ]
      },
      {
        "native_result": "Unit Surgical Strike Production",
        "target_kind": "City",
        "operation": "strike_production",
        "cost": "none",
        "subtarget": "none",
        "subresult_sets": [
          []
        ],
        "probability_policy": "resolved",
        "native_rules": []
      },
      {
        "native_result": "Unit Targeted Sabotage City",
        "target_kind": "City",
        "operation": "sabotage_building",
        "cost": "none",
        "subtarget": "building",
        "subresult_sets": [
          []
        ],
        "probability_policy": "unresolved",
        "native_rules": [
          "Targeted Sabotage City",
          "Targeted Sabotage City Escape"
        ]
      },
      {
        "native_result": "Unit Targeted Steal Tech",
        "target_kind": "City",
        "operation": "steal_technology",
        "cost": "none",
        "subtarget": "technology",
        "subresult_sets": [
          []
        ],
        "probability_policy": "native",
        "native_rules": [
          "Targeted Steal Tech",
          "Targeted Steal Tech Escape Expected"
        ]
      },
      {
        "native_result": "Wipe Units",
        "target_kind": "Stack",
        "operation": "wipe_units",
        "cost": "none",
        "subtarget": "none",
        "subresult_sets": [
          []
        ],
        "probability_policy": "resolved",
        "native_rules": []
      }
    ],
    "generic_special_action_contract": {
      "native_result": "Ruleset Custom",
      "native_rules": [
        "User Action 1",
        "User Action 2",
        "User Action 3",
        "User Action 4"
      ],
      "target_kinds": [
        "City",
        "Extras",
        "Self",
        "Stack",
        "Tile",
        "Unit"
      ],
      "operation": "ruleset_action",
      "cost": "none",
      "subtarget": "none",
      "subresult_sets": [
        []
      ],
      "probability_policy": "resolved",
      "label": "native_ui_name"
    },
    "non_special_subresults": [
      [
        "Paradrop Unit",
        [
          [],
          [
            "may_embark"
          ]
        ]
      ],
      [
        "Paradrop Unit Enter",
        [
          [
            "hut_enter"
          ],
          [
            "hut_enter",
            "may_embark"
          ]
        ]
      ],
      [
        "Paradrop Unit Frighten",
        [
          [
            "hut_frighten"
          ],
          [
            "hut_frighten",
            "may_embark"
          ]
        ]
      ],
      [
        "Teleport Enter",
        [
          [
            "hut_enter"
          ]
        ]
      ],
      [
        "Teleport Frighten",
        [
          [
            "hut_frighten"
          ]
        ]
      ]
    ],
    "value_domains": {
      "action_subtarget_kinds": [
        "building",
        "extra",
        "extra_not_there",
        "none",
        "specialist",
        "technology"
      ],
      "action_subresults": [
        "hut_enter",
        "hut_frighten",
        "may_embark",
        "non_lethal"
      ],
      "action_subresult_effects": {
        "hut_enter": "enter_huts",
        "hut_frighten": "frighten_huts",
        "may_embark": "may_embark",
        "non_lethal": "non_lethal_to_target_units"
      },
      "map_topologies": [
        "hex",
        "isometric_hex",
        "isometric_square",
        "square"
      ],
      "client_states": [
        "disconnected",
        "initial",
        "over",
        "preparing",
        "running"
      ],
      "phase_modes": [
        "concurrent",
        "players_alternate",
        "teams_alternate"
      ],
      "vote_statuses": [
        "active",
        "failed",
        "passed",
        "removed"
      ],
      "diplomacy_states": [
        "Alliance",
        "Armistice",
        "Cease-fire",
        "Never met",
        "Peace",
        "Team",
        "War"
      ],
      "diplomacy_cancel_reasons": [
        "alliance_problem_them",
        "alliance_problem_us",
        "allowed",
        "not_allowed",
        "senate_blocking"
      ],
      "diplomacy_clause_types": [
        "Advance",
        "Alliance",
        "Ceasefire",
        "City",
        "Embassy",
        "Gold",
        "Map",
        "Peace",
        "Seamap",
        "SharedTiles",
        "Vision"
      ],
      "diplomacy_clause_value_kinds": [
        "city",
        "city_unavailable",
        "gold",
        "none",
        "technology"
      ],
      "research_states": [
        "available",
        "future",
        "known",
        "reachable",
        "unset"
      ],
      "research_edge_kinds": [
        "direct",
        "root"
      ],
      "research_unlock_kinds": [
        "action",
        "building",
        "government",
        "unit"
      ],
      "research_unlock_scopes": [
        "actor",
        "both",
        "build",
        "change",
        "target"
      ],
      "intel_levels": [
        "contact",
        "embassy",
        "none"
      ],
      "player_controllers": [
        "ai",
        "human"
      ],
      "legality": [
        "legal",
        "possibly_legal",
        "unresolved"
      ],
      "probability_kinds": [
        "exact",
        "not_implemented",
        "range",
        "unknown"
      ],
      "build_kinds": [
        "improvement",
        "none",
        "unit"
      ],
      "transport_states": [
        "transported",
        "unresolved",
        "untransported"
      ],
      "unit_controllers": [
        "auto_explore",
        "auto_work",
        "none"
      ],
      "activities": [
        "base",
        "clean",
        "convert",
        "cultivate",
        "explore",
        "fortified",
        "fortifying",
        "goto",
        "idle",
        "irrigate",
        "mine",
        "none",
        "pillage",
        "plant",
        "road",
        "sentry",
        "transform"
      ],
      "worker_start_activities": [
        "base",
        "clean",
        "cultivate",
        "irrigate",
        "mine",
        "pillage",
        "plant",
        "road",
        "transform"
      ],
      "targeted_activities": [
        "base",
        "clean",
        "irrigate",
        "mine",
        "pillage",
        "road"
      ],
      "government_statuses": [
        "anarchy",
        "anarchy_targeted",
        "choice_required",
        "enactment_pending",
        "stable"
      ],
      "revolution_methods": [
        "fixed",
        "quickening",
        "random",
        "random_quickening"
      ],
      "spaceship_states": [
        "arrived",
        "launched",
        "none",
        "started"
      ],
      "spaceship_parts": [
        "fuel",
        "habitation",
        "life_support",
        "none",
        "propulsion",
        "solar_panels",
        "structural"
      ],
      "new_citizens": [
        "default",
        "gold",
        "science"
      ],
      "city_site_visibilities": [
        "known",
        "own",
        "visible"
      ],
      "trade_route_partner_visibilities": [
        "own",
        "unavailable",
        "visible"
      ],
      "trade_route_directions": [
        "bidirectional",
        "from",
        "to"
      ],
      "chat_senders": [
        "connection",
        "observer",
        "player",
        "server",
        "unknown"
      ],
      "chat_channels": [
        "allied",
        "chat",
        "event",
        "global",
        "private"
      ],
      "chat_send_channels": [
        "global",
        "allied",
        "private"
      ],
      "extra_cause_tags_by_bit": [
        "irrigation",
        "mine",
        "road",
        "base",
        "pollution",
        "fallout",
        "hut",
        "appearance",
        "resource"
      ],
      "unit_scopes": [
        "own",
        "visible"
      ],
      "unit_route_step_kinds": [
        "action_move",
        "move",
        "wait"
      ],
      "tile_known": [
        0n,
        1n,
        2n
      ],
      "tombstone_kinds": [
        "player",
        "city",
        "unit"
      ],
      "booleans": [
        0n,
        1n
      ]
    },
    "sentinels": {
      "cache": "human-client",
      "absent_reference": "none",
      "unknown_terrain": "unknown",
      "no_native_target": -1n,
      "no_government_target": -1n,
      "future_tech_name": "Future Tech",
      "unset_tech_name": "Unset",
      "city_growth_turns_never": 1000000000n
    },
    "scalar_contracts": {
      "entity_ref": "^(?P<kind>[pcu]):(?P<number>0|[1-9][0-9]*):(?P<incarnation>[1-9][0-9]*)$",
      "action_slot": "^(?:a[0-9A-F]{16}|t[0-9A-F]{24})$",
      "signed_integer": "^(?:0|-[1-9][0-9]*|[1-9][0-9]*)$",
      "unsigned_integer": "^(?:0|[1-9][0-9]*)$",
      "i64_min": -9223372036854775808n,
      "i64_max": 9223372036854775807n,
      "u64_max": 18446744073709551615n,
      "i32_max": 2147483647n,
      "max_rows": 8192n,
      "max_native_state_scope_rows": 40000n,
      "max_row_bytes": 2047n,
      "max_page_items": 16n,
      "max_scoped_actions": 2048n,
      "max_pinned_scope_views": 8n,
      "max_relation_scoped_actions": 8192n,
      "max_pinned_relation_scope_views": 4n,
      "max_governments": 127n,
      "max_multipliers": 50n,
      "max_multiplier_choices": 4294967296n,
      "max_city_build_choices": 1024n,
      "max_city_worklist": 64n,
      "max_city_trade_routes": 20n,
      "max_goods_types": 25n,
      "max_rally_orders": 2000n,
      "max_unit_route_waypoints": 64n,
      "unit_route_waypoint_rules": [
        "first-waypoint-differs-from-source",
        "goto-final-waypoint-differs-from-source",
        "consecutive-waypoints-differ"
      ],
      "max_infrastructure_choices": 250n,
      "max_chat_message_bytes": 512n,
      "chat_forbidden_codepoint_ranges": [
        [
          0n,
          31n
        ],
        [
          127n,
          159n
        ],
        [
          173n,
          173n
        ],
        [
          1536n,
          1541n
        ],
        [
          1564n,
          1564n
        ],
        [
          1757n,
          1757n
        ],
        [
          1807n,
          1807n
        ],
        [
          2192n,
          2193n
        ],
        [
          2274n,
          2274n
        ],
        [
          6158n,
          6158n
        ],
        [
          8203n,
          8207n
        ],
        [
          8234n,
          8238n
        ],
        [
          8288n,
          8292n
        ],
        [
          8294n,
          8303n
        ],
        [
          65279n,
          65279n
        ],
        [
          65529n,
          65531n
        ],
        [
          69821n,
          69821n
        ],
        [
          69837n,
          69837n
        ],
        [
          78896n,
          78911n
        ],
        [
          113824n,
          113827n
        ],
        [
          119155n,
          119162n
        ],
        [
          917505n,
          917505n
        ],
        [
          917536n,
          917631n
        ]
      ],
      "chat_native_argument_grammar": "channel=<global|allied|private>;recipient=<none|p:id:incarnation>;message=<percent-encoded>",
      "chat_message_edge_policy": "no-leading-or-trailing-U+0020",
      "max_vote_history": 64n,
      "governor_minimum_surplus_min": -100n,
      "governor_minimum_surplus_max": 100n,
      "governor_weight_min": 0n,
      "governor_weight_max": 25n,
      "governor_celebration_weight_min": 0n,
      "governor_celebration_weight_max": 50n,
      "native_state_scope_sections": [
        "action_decision_tile",
        "chat_recipients",
        "cities",
        "city_build_choices",
        "city_citizens",
        "city_governor",
        "city_improvements",
        "city_sites",
        "city_trade_routes",
        "city_worklist",
        "diplomacy_clauses",
        "investigation",
        "known_tiles",
        "map_tiles",
        "pregame_nations",
        "pregame_styles",
        "pregame_teams",
        "target_tiles",
        "tile_window",
        "unit_route",
        "units"
      ]
    },
    "private_frame_contracts": [
      [
        "SCOPE_OPEN",
        "SCOPE_OPEN request expected_revision actor_ref"
      ],
      [
        "SCOPE_OPENED",
        "SCOPE_OPENED request view revision actor_ref total complete overflow"
      ],
      [
        "SCOPE_PAGE",
        "SCOPE_PAGE request view offset limit"
      ],
      [
        "SCOPE_BEGIN",
        "SCOPE_BEGIN request view revision actor_ref offset count total"
      ],
      [
        "SCOPE_ACTION",
        "SCOPE_ACTION request view index encoded_action_row"
      ],
      [
        "SCOPE_END",
        "SCOPE_END request view next_offset"
      ],
      [
        "STATE_SCOPE_OPEN",
        "STATE_SCOPE_OPEN request expected_revision section selector"
      ],
      [
        "STATE_SCOPE_OPENED",
        "STATE_SCOPE_OPENED request view revision section selector total complete overflow"
      ],
      [
        "STATE_SCOPE_PAGE",
        "STATE_SCOPE_PAGE request view offset limit"
      ],
      [
        "STATE_SCOPE_BEGIN",
        "STATE_SCOPE_BEGIN request view revision section selector offset count total"
      ],
      [
        "STATE_SCOPE_ROW",
        "STATE_SCOPE_ROW request view index encoded_state_row"
      ],
      [
        "STATE_SCOPE_END",
        "STATE_SCOPE_END request view next_offset"
      ],
      [
        "ACT_CAP",
        "ACT_CAP request expected_revision actor_ref slot arguments"
      ],
      [
        "ACT_RELATION_CAP",
        "ACT_RELATION_CAP request expected_revision actor_ref counterpart_ref slot arguments"
      ],
      [
        "TARGET_ACTION",
        "TARGET_ACTION request expected_revision actor_ref native_tile"
      ],
      [
        "TARGET_BEGIN",
        "TARGET_BEGIN request revision actor_ref native_tile count"
      ],
      [
        "TARGET_ROW",
        "TARGET_ROW request index encoded_action_row"
      ],
      [
        "TARGET_END",
        "TARGET_END request count"
      ],
      [
        "RELATION_SCOPE_OPEN",
        "RELATION_SCOPE_OPEN request expected_revision actor_ref counterpart_ref"
      ],
      [
        "RELATION_SCOPE_OPENED",
        "RELATION_SCOPE_OPENED request view revision actor_ref counterpart_ref total complete overflow"
      ],
      [
        "RELATION_SCOPE_PAGE",
        "RELATION_SCOPE_PAGE request view offset limit"
      ],
      [
        "RELATION_SCOPE_BEGIN",
        "RELATION_SCOPE_BEGIN request view revision actor_ref counterpart_ref offset count total"
      ],
      [
        "RELATION_SCOPE_ACTION",
        "RELATION_SCOPE_ACTION request view index encoded_action_row"
      ],
      [
        "RELATION_SCOPE_END",
        "RELATION_SCOPE_END request view next_offset"
      ]
    ],
    "research_choices_digest": {
      "algorithm": "FNV-1a-64",
      "offset_basis": 14695981039346656037n,
      "prime": 1099511628211n,
      "record_order": "ascending-native-id",
      "record_bytes": [
        "native-id-u32-be",
        "name-utf8-length-u32-be",
        "name-utf8",
        "state-ascii-length-u8",
        "state-ascii",
        "can-target-u8",
        "can-goal-u8"
      ],
      "text_format": "fnv1a64-%016x"
    },
    "treaty_clauses_digest": {
      "algorithm": "FNV-1a-64",
      "offset_basis": 14695981039346656037n,
      "prime": 1099511628211n,
      "record_order": "giver-number-clause-type-value",
      "record_bytes": "ascii giver:type:value;",
      "text_format": "fnv1a64-%016x"
    }
  };
