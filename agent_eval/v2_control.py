"""Fog-safe public projection of native ``freeciv-agent`` observations.

The native protocol intentionally uses short-lived implementation identifiers.
This module is the trust boundary which validates that protocol, replaces every
native identifier with a seat-scoped opaque identifier, and retains only the
small amount of private data required to execute a later action.
"""

from __future__ import annotations

import base64
from collections import Counter, OrderedDict, defaultdict
from dataclasses import dataclass, replace
from datetime import datetime, timezone
import hashlib
import hmac
import json
import math
import re
import secrets
import threading
import time
from types import MappingProxyType
# Mapping and Sequence come from collections.abc rather than typing because
# they are used for runtime isinstance checks on every projected row -- about
# 113k of them per known-tiles page -- and typing's deprecated aliases route
# each check through the generic-alias machinery.  Measured at 3-5% of every
# request shape; small, but paid on literally every page the boundary serves.
from collections.abc import Mapping, Sequence
from typing import Any
import unicodedata

from .full_control_v2 import (
    FULL_CONTROL_SCHEMA_VERSION,
    FULL_CONTROL_V2,
    FullControlSchemaError,
    assert_projected_action_contract,
    validate_legal_action_descriptor,
    validate_state_revision,
)


MAX_NATIVE_ROWS = 8192
MAX_NATIVE_ROW_BYTES = 2047
MAX_PAGE_ITEMS = 16
MAX_PUBLIC_PAGE_BYTES = 64 * 1024
MAX_CACHED_REVISIONS = 2
MAX_PROJECTED_BYTES = 16 * 1024 * 1024
MAX_CURSORS = 8192
CURSOR_TTL_SECONDS = 300.0
# How long a scoped reservation may stay in flight before it is treated as
# abandoned.  One reservation covers a single bounded native page read (five
# seconds) plus its projection, so this is more than an order of magnitude of
# headroom; it exists only so a reservation whose caller never returned cannot
# hold a registry slot until the seat generation ends.
CURSOR_IN_FLIGHT_LEASE_SECONDS = 60.0
RETIRED_CURSOR_TTL_SECONDS = 900.0
MAX_RETIRED_CURSORS = 8192
MAX_CURSOR_RECORDS = MAX_CURSORS + MAX_RETIRED_CURSORS
MAX_ACTIVE_CURSOR_CHAINS = 512
# The unfiltered legal-action catalog is how a seat reaches `phase.end`, so
# ordinary reads may never consume the whole chain budget: the last slots are
# admissible only to that traversal.
RESERVED_CATALOG_CHAINS = 64
MAX_RETIRED_CURSOR_CHAINS = 4096
MAX_CURSOR_CHAIN_PAGES = 40000
MAX_CURSOR_CHAIN_SLOTS = 65536
MAX_CURSOR_CHAIN_BYTES = 32 * 1024 * 1024
MAX_SCOPED_ACTIONS = 2048
MAX_PINNED_SCOPE_VIEWS = 8
MAX_SCOPED_ACTION_BINDINGS = 12288
MAX_RELATION_SCOPED_ACTIONS = 8192
MAX_PINNED_RELATION_SCOPE_VIEWS = 4
MAX_NATIVE_STATE_SCOPE_ROWS = 40000
MAX_RELATION_OVERLAY_ENTRIES = 32
MAX_RELATION_OVERLAY_BYTES = 64 * 1024 * 1024
MAX_BUNDLED_ROWS = MAX_NATIVE_ROWS + 3 * MAX_NATIVE_STATE_SCOPE_ROWS
MAX_GOVERNMENTS = 127
MAX_MULTIPLIERS = 50
MAX_MULTIPLIER_CHOICES = 1 << 32
MAX_CITY_BUILD_CHOICES = 1024
MAX_CITY_WORKLIST = 64
MAX_CITY_TRADE_ROUTES = 20
MAX_GOODS_TYPES = 25
MAX_RALLY_ORDERS = 2000
MAX_UNIT_ROUTE_WAYPOINTS = 64
MAX_INFRASTRUCTURE_CHOICES = 250
MAX_INVESTIGATION_IMPROVEMENTS = 1024
MAX_INVESTIGATION_SPECIALISTS = 256
INVESTIGATION_FEELING_STAGES = (
    "base", "luxury", "effects", "nationality", "martial_law", "final",
)
MAX_VOTES = 256
MAX_VOTE_HISTORY = 64

_PRIVATE_FRAME_CONTRACTS: Mapping[str, str] = MappingProxyType({
    "SCOPE_OPEN": "SCOPE_OPEN request expected_revision actor_ref",
    "SCOPE_OPENED": (
        "SCOPE_OPENED request view revision actor_ref total complete overflow"
    ),
    "SCOPE_PAGE": "SCOPE_PAGE request view offset limit",
    "SCOPE_BEGIN": (
        "SCOPE_BEGIN request view revision actor_ref offset count total"
    ),
    "SCOPE_ACTION": (
        "SCOPE_ACTION request view index encoded_action_row"
    ),
    "SCOPE_END": "SCOPE_END request view next_offset",
    "STATE_SCOPE_OPEN": (
        "STATE_SCOPE_OPEN request expected_revision section selector"
    ),
    "STATE_SCOPE_OPENED": (
        "STATE_SCOPE_OPENED request view revision section selector "
        "total complete overflow"
    ),
    "STATE_SCOPE_PAGE": "STATE_SCOPE_PAGE request view offset limit",
    "STATE_SCOPE_BEGIN": (
        "STATE_SCOPE_BEGIN request view revision section selector "
        "offset count total"
    ),
    "STATE_SCOPE_ROW": (
        "STATE_SCOPE_ROW request view index encoded_state_row"
    ),
    "STATE_SCOPE_END": "STATE_SCOPE_END request view next_offset",
    "ACT_CAP": (
        "ACT_CAP request expected_revision actor_ref slot arguments"
    ),
    "ACT_RELATION_CAP": (
        "ACT_RELATION_CAP request expected_revision actor_ref "
        "counterpart_ref slot arguments"
    ),
    "TARGET_ACTION": (
        "TARGET_ACTION request expected_revision actor_ref native_tile"
    ),
    "TARGET_BEGIN": (
        "TARGET_BEGIN request revision actor_ref native_tile count"
    ),
    "TARGET_ROW": "TARGET_ROW request index encoded_action_row",
    "TARGET_END": "TARGET_END request count",
    "RELATION_SCOPE_OPEN": (
        "RELATION_SCOPE_OPEN request expected_revision actor_ref "
        "counterpart_ref"
    ),
    "RELATION_SCOPE_OPENED": (
        "RELATION_SCOPE_OPENED request view revision actor_ref "
        "counterpart_ref total complete overflow"
    ),
    "RELATION_SCOPE_PAGE": "RELATION_SCOPE_PAGE request view offset limit",
    "RELATION_SCOPE_BEGIN": (
        "RELATION_SCOPE_BEGIN request view revision actor_ref counterpart_ref "
        "offset count total"
    ),
    "RELATION_SCOPE_ACTION": (
        "RELATION_SCOPE_ACTION request view index encoded_action_row"
    ),
    "RELATION_SCOPE_END": "RELATION_SCOPE_END request view next_offset",
})

_I64_MIN = -(1 << 63)
_I64_MAX = (1 << 63) - 1
_U64_MAX = (1 << 64) - 1
_I32_MAX = (1 << 31) - 1
_FC_INFINITY = 1_000_000_000
_CITY_OUTPUTS = (
    "food", "shield", "trade", "gold", "luxury", "science",
)
_CITY_YIELD_FIELDS = (
    "food", "shields", "trade", "gold", "luxury", "science",
)
_EXTRA_CAUSE_TAGS = (
    "irrigation", "mine", "road", "base", "pollution", "fallout",
    "hut", "appearance", "resource",
)
MAX_CHAT_HISTORY = 64
MAX_CHAT_MESSAGE_BYTES = 512

_OPAQUE_OWNER = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._:-]{0,127}$")
_PUBLIC_ACTOR_ID = re.compile(r"^(?:player|city|unit)_[0-9a-f]{32}$")
_PUBLIC_TILE_ID = re.compile(r"^tile_[0-9a-f]{32}$")
_SIGNED = re.compile(r"^(?:0|-[1-9][0-9]*|[1-9][0-9]*)$")
_UNSIGNED = re.compile(r"^(?:0|[1-9][0-9]*)$")
_ENTITY_REF = re.compile(
    r"^(?P<kind>[pcu]):(?P<number>0|[1-9][0-9]*):"
    r"(?P<incarnation>[1-9][0-9]*)$"
)
_ACTION_SLOT = re.compile(r"^(?:a[0-9A-F]{16}|t[0-9A-F]{24})$")
_SCOPE_VIEW = re.compile(r"^v[1-9][0-9]*-[1-9][0-9]*$")
_STATE_SCOPE_VIEW = re.compile(r"^q[1-9][0-9]*-[1-9][0-9]*$")
_RELATION_SCOPE_VIEW = re.compile(r"^r[1-9][0-9]*-[1-9][0-9]*$")
_INVESTIGATION_SELECTOR = re.compile(r"^i[0-9a-f]{16}$")
_FNV1A64_DIGEST = re.compile(r"^fnv1a64-[0-9a-f]{16}$")
_UNRESERVED = frozenset(
    b"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._~-"
)
# The same set as a pattern, for text that needs no percent-decoding at all.
_UNRESERVED_TEXT = re.compile(r"[A-Za-z0-9._~-]*")

_ROW_FIELDS: Mapping[str, tuple[str, ...]] = MappingProxyType({
    "meta": (
        "state", "turn", "phase", "cache", "phase_mode", "phase_count",
        "active_phase", "phase_ready", "map_width", "map_height",
        "topology", "wrap_x", "wrap_y", "known_tile_count",
    ),
    "pregame": (
        "ref", "leader", "nation", "sex", "style", "ready",
        "nation_choices", "style_choices", "team_choices",
    ),
    "pregame_nation": ("id", "name", "default_style"),
    "pregame_style": ("id", "name"),
    "pregame_team": (
        "id", "name", "selected", "occupied", "member_count",
    ),
    "pregame_team_member": ("team", "player", "leader"),
    "vote": (
        "vote_no", "caller", "description", "yes", "no", "abstain",
        "num_voters", "percent_required", "team_only", "current_vote",
        "can_vote", "status", "outcome_turn", "outcome_phase",
    ),
    "player": (
        "ref", "name", "nation", "government", "gold", "tax",
        "science", "luxury", "alive", "phase_done", "changeable_tax",
        "max_rate", "infrastructure_enabled", "infrastructure_points",
    ),
    "governance": (
        "current_id", "target_id", "during_id", "status", "finish_turn",
        "turns_remaining", "method", "max_turns", "untargeted_allowed",
        "no_anarchy", "can_revolution", "choices_count",
    ),
    "government": (
        "id", "name", "current", "target", "during", "can_change",
    ),
    "multiplier": (
        "id", "name", "value", "target", "start", "stop", "step",
        "minimum_turns", "changed_turn", "can_change", "choice_count",
    ),
    "spaceship": (
        "state", "structurals", "structurals_placed", "components", "fuel",
        "propulsion", "modules", "habitation", "life_support",
        "solar_panels", "launch_year", "population", "mass",
        "support_permille", "energy_permille", "success_permille",
        "travel_time_millis", "has_capital", "can_launch",
    ),
    "spaceship_structural": (
        "slot", "x", "y", "required_slot", "placed",
        "required_connected", "can_place",
    ),
    "research": (
        "techs", "future", "target", "target_id", "goal", "goal_id",
        "bulbs", "cost", "output", "choices_count", "choices_digest",
    ),
    "research_tech": (
        "id", "name", "state", "can_target", "can_goal",
    ),
    "research_graph": (
        "id", "name", "reachable", "next_step", "unknown_prerequisites",
        "path_cost",
    ),
    "research_edge": ("tech", "prerequisite", "kind"),
    "research_unlock": ("tech", "kind", "native_id", "name", "scope"),
    "diplomacy": (
        "other", "name", "nation", "state", "contact", "alive",
        "turns_left", "can_meet", "meeting", "generation", "self_accepted",
        "other_accepted", "clause_count", "clauses_digest",
        "intel_level", "team", "team_name", "same_team", "controller",
        "connected", "score", "gold", "government",
        "has_embassy", "other_has_embassy",
        "gives_vision", "receives_vision", "gives_shared_tiles",
        "receives_shared_tiles", "can_cancel", "cancel_reason",
    ),
    "diplomacy_intel": (
        "other", "tax", "science", "luxury", "culture", "research_id",
        "research_name", "bulbs", "cost", "known_count", "known_digest",
        "known_ids",
    ),
    "diplomacy_clause": (
        "other", "generation", "position", "giver", "type",
        "value_kind", "value", "name",
    ),
    "tile": (
        "index", "x", "y", "known", "terrain", "owner",
        "placing_extra", "placing_extra_name", "placing_turns",
        "placing_time",
    ),
    "tile_local": (
        "index", "x", "y", "known", "terrain", "owner",
        "placing_extra", "placing_extra_name", "placing_turns",
        "placing_time", "resource_extra", "resource_name", "has_label",
        "label", "food", "shields", "trade",
    ),
    "tile_extra": ("tile", "extra", "name", "cause_mask"),
    "infrastructure_extra": (
        "id", "name", "cost", "build_time", "build_time_factor",
    ),
    "city": (
        "ref", "name", "tile", "x", "y", "size", "food", "shields",
        "trade", "production_kind", "production_id", "production_name",
        "shield_stock", "shield_cost", "buy_cost", "can_buy", "can_change",
        "citizen_tile_count", "specialist_type_count", "worklist_length",
        "build_choice_count", "improvement_count", "trade_route_count",
        "trade_route_capacity", "did_sell", "allow_disband",
        "new_citizens", "options_conflict",
        "airlift_remaining", "airlift_max", "governor_enabled",
        "citizen_happy", "citizen_content", "citizen_unhappy",
        "citizen_angry", "citizen_workers", "citizen_specialists",
        "food_stock", "granary_size", "growth_turns", "pollution",
        *(
            field
            for output in _CITY_OUTPUTS
            for field in (
                f"{output}_citizen_base", f"{output}_net",
                f"{output}_surplus", f"{output}_usage",
                f"{output}_waste", f"{output}_unhappy_penalty",
            )
        ),
    ),
    "city_site": (
        "ref", "owner", "name", "tile", "x", "y", "size", "visibility",
    ),
    "city_tile": (
        "city", "tile", "worked", "free_worked", "can_work",
        "food", "shields", "trade", "gold", "luxury", "science",
    ),
    "city_worker_task": (
        "city", "tile", "activity", "target_extra", "target_extra_name",
        "want",
    ),
    "city_specialist": (
        "city", "specialist", "name", "count", "counts_toward_population",
        "can_use", "is_default", "food", "shields", "trade", "gold",
        "luxury", "science",
    ),
    "city_worklist": (
        "city", "position", "production_kind", "production_id",
        "production_name",
    ),
    "city_build_choice": (
        "city", "production_kind", "production_id", "production_name",
        "can_queue", "can_build_now", "shield_cost",
        "shield_stock_after_change", "turns", "turns_with_stock",
        "upkeep_food", "upkeep_shield", "upkeep_trade", "upkeep_gold",
        "upkeep_luxury", "upkeep_science", "happy_cost", "unit_attack",
        "unit_defense", "unit_move_rate", "unit_hp", "unit_firepower",
        "unit_vision_radius_sq", "unit_transport_capacity", "unit_fuel",
        "unit_pop_cost", "unit_bombard_rate", "unit_city_size",
        "unit_paradrop_range", "building_genus", "building_obsolete",
        "building_redundant", "building_convert", "building_allows_units",
        "building_allows_extras", "building_prevents_disaster",
        "building_protects_vs_actions", "building_allows_actions",
    ),
    "city_improvement": (
        "city", "improvement_id", "name", "sellable", "sell_price",
    ),
    "city_trade_route": (
        "city", "position", "partner", "partner_visibility",
        "partner_name", "base_value", "effective_value", "direction",
        "goods_id", "goods_name",
    ),
    "investigation": (
        "city", "lifecycle", "tile", "name", "size", "production_kind",
        "production_id", "production_name", "shield_stock",
        "shield_surplus", "improvement_count", "feeling_count",
        "specialist_count",
    ),
    "investigation_improvement": ("city", "improvement_id", "name"),
    "investigation_citizens": (
        "city", "stage", "happy", "content", "unhappy", "angry",
    ),
    "investigation_specialist": ("city", "specialist", "name", "count"),
    "city_rally": (
        "city", "active", "persistent", "vigilant", "order_count",
        "orders_digest",
    ),
    "city_governor": (
        "city", "min_food", "min_production", "min_trade", "min_gold",
        "min_luxury", "min_science", "weight_food", "weight_production",
        "weight_trade", "weight_gold", "weight_luxury", "weight_science",
        "celebration_weight", "require_happy", "maximize_growth",
    ),
    "unit_own": (
        "ref", "scope", "owner", "type_id", "type", "home_city",
        "converts_to_id", "converts_to", "tile", "x", "y", "hp",
        "veteran", "veteran_name", "veteran_levels", "veteran_power",
        "veteran_move_bonus", "fuel", "max_hp", "max_fuel", "move_rate",
        "attack", "defense", "firepower", "base_upkeep_food",
        "base_upkeep_shield", "base_upkeep_trade", "base_upkeep_gold",
        "base_upkeep_luxury", "base_upkeep_science", "upkeep_food",
        "upkeep_shield", "upkeep_trade", "upkeep_gold", "upkeep_luxury",
        "upkeep_science",
        "moves", "activity", "activity_target", "activity_target_name",
        "activity_progress", "transport_state", "transporter",
        "transport_capacity", "occupied", "paradropped", "paradrop_range",
        "controller", "has_orders", "orders_repeat", "orders_vigilant",
        "order_count", "orders_digest", "orders_destination",
        "action_decision_want", "action_decision_tile",
    ),
    "unit_route": (
        "unit", "order_index", "reconstructable", "step_count",
    ),
    "unit_route_step": ("unit", "sequence", "kind", "tile"),
    "unit_visible": (
        "ref", "scope", "owner", "type_id", "type", "tile", "x", "y",
        "hp", "veteran", "veteran_name", "veteran_levels",
        "veteran_power", "veteran_move_bonus", "max_hp", "max_fuel",
        "move_rate", "attack", "defense", "firepower",
        "base_upkeep_food", "base_upkeep_shield", "base_upkeep_trade",
        "base_upkeep_gold", "base_upkeep_luxury", "base_upkeep_science",
    ),
    "tombstone": ("ref", "kind"),
    "chat": (
        "sequence", "turn", "phase", "sender", "sender_name", "self",
        "channel", "event", "truncated", "message",
    ),
    "chat_recipient": (
        "ref", "name", "self", "connected", "can_message",
    ),
    "action": (
        "slot", "kind", "actor", "counterpart", "meeting_generation",
        "clauses_digest", "self_accepted", "other_accepted",
        "relation_state", "outgoing_vision", "outgoing_shared_tiles",
        "clause_giver", "clause_type", "clause_value", "clause_name",
        "desired_acceptance", "target_tile", "source_city",
        "destination_city", "target_unit",
        "transport_context", "target_tech",
        "vote_no", "server_setting_id", "server_setting_type",
        "server_setting_min", "server_setting_max", "server_setting_current",
        "server_setting_value",
        "target_government", "max_rate", "route_waypoint_limit",
        "infrastructure_cost", "infrastructure_turns",
        "infrastructure_choice_count", "infrastructure_choices",
        "target_build_kind", "target_build", "spaceship_part",
        "spaceship_value", "target_multiplier", "multiplier_value",
        "source_specialist",
        "target_specialist", "target_extra", "subtarget_kind",
        "subresults", "activity",
        "target_name", "native_rule",
        "target_kind", "result", "actor_consuming_always", "legality",
        "probability_kind", "probability_min", "probability_max",
        "gold_cost", "args",
    ),
})

_ROW_FORMAT_CONTRACTS: Mapping[str, str] = MappingProxyType({
    "meta": (
        "meta state=%s turn=%d phase=%d cache=human-client "
        "phase_mode=%s phase_count=%d active_phase=%d phase_ready=%d "
        "map_width=%d map_height=%d topology=%s wrap_x=%d wrap_y=%d "
        "known_tile_count=%d"
    ),
    "pregame": (
        "pregame ref=%s leader=%s nation=%s sex=%s style=%s ready=%d "
        "nation_choices=%d style_choices=%d team_choices=%d"
    ),
    "pregame_nation": "pregame_nation id=%d name=%s default_style=%d",
    "pregame_style": "pregame_style id=%d name=%s",
    "pregame_team": (
        "pregame_team id=%d name=%s selected=%d occupied=%d "
        "member_count=%d"
    ),
    "pregame_team_member": (
        "pregame_team_member team=%d player=%s leader=%s"
    ),
    "vote": (
        "vote vote_no=%d caller=%s description=%s yes=%d no=%d abstain=%d "
        "num_voters=%d percent_required=%d team_only=%d current_vote=%s "
        "can_vote=%d status=%s outcome_turn=%d outcome_phase=%d"
    ),
    "player": (
        "player ref=%s name=%s nation=%s government=%s gold=%d tax=%d "
        "science=%d luxury=%d alive=%d phase_done=%d changeable_tax=%d "
        "max_rate=%d infrastructure_enabled=%d infrastructure_points=%d"
    ),
    "governance": (
        "governance current_id=%d target_id=%d during_id=%d status=%s "
        "finish_turn=%d turns_remaining=%d method=%s max_turns=%d "
        "untargeted_allowed=%d no_anarchy=%d can_revolution=%d "
        "choices_count=%d"
    ),
    "government": (
        "government id=%d name=%s current=%d target=%d during=%d "
        "can_change=%d"
    ),
    "multiplier": (
        "multiplier id=%d name=%s value=%d target=%d start=%d stop=%d "
        "step=%d minimum_turns=%d changed_turn=%d can_change=%d "
        "choice_count=%llu"
    ),
    "spaceship": (
        "spaceship state=%s structurals=%d structurals_placed=%d "
        "components=%d fuel=%d propulsion=%d modules=%d habitation=%d "
        "life_support=%d solar_panels=%d launch_year=%d population=%d "
        "mass=%d support_permille=%d energy_permille=%d "
        "success_permille=%d travel_time_millis=%d has_capital=%d "
        "can_launch=%d"
    ),
    "spaceship_structural": (
        "spaceship_structural slot=%d x=%d y=%d required_slot=%d "
        "placed=%d required_connected=%d can_place=%d"
    ),
    "research": (
        "research techs=%d future=%d target=%s target_id=%d goal=%s "
        "goal_id=%d bulbs=%d cost=%d output=%d choices_count=%d "
        "choices_digest=%s"
    ),
    "research_tech": (
        "research_tech id=%d name=%s state=%s can_target=%d can_goal=%d"
    ),
    "research_graph": (
        "research_graph id=%d name=%s reachable=%d next_step=%d "
        "unknown_prerequisites=%d path_cost=%d"
    ),
    "research_edge": (
        "research_edge tech=%d prerequisite=%d kind=%s"
    ),
    "research_unlock": (
        "research_unlock tech=%d kind=%s native_id=%d name=%s scope=%s"
    ),
    "diplomacy": (
        "diplomacy other=%s name=%s nation=%s state=%s contact=%d "
        "alive=%d turns_left=%d can_meet=%d meeting=%d generation=%llu "
        "self_accepted=%d other_accepted=%d clause_count=%d "
        "clauses_digest=%s intel_level=%s team=%d team_name=%s "
        "same_team=%d controller=%s connected=%d score=%d gold=%d "
        "government=%s has_embassy=%d other_has_embassy=%d "
        "gives_vision=%d receives_vision=%d "
        "gives_shared_tiles=%d receives_shared_tiles=%d can_cancel=%d"
        " cancel_reason=%s"
    ),
    "diplomacy_intel": (
        "diplomacy_intel other=%s tax=%d science=%d luxury=%d culture=%d "
        "research_id=%d research_name=%s bulbs=%d cost=%d known_count=%d "
        "known_digest=%s known_ids=%s"
    ),
    "diplomacy_clause": (
        "diplomacy_clause other=%s generation=%llu position=%d giver=%s "
        "type=%s value_kind=%s value=%d name=%s"
    ),
    "tile": (
        "tile index=%d x=%d y=%d known=%d terrain=%s owner=%s "
        "placing_extra=%d placing_extra_name=%s placing_turns=%d "
        "placing_time=%d"
    ),
    "tile_local": (
        "tile_local index=%d x=%d y=%d known=%d terrain=%s owner=%s "
        "placing_extra=%d placing_extra_name=%s placing_turns=%d "
        "placing_time=%d resource_extra=%d resource_name=%s has_label=%d "
        "label=%s food=%d shields=%d trade=%d"
    ),
    "tile_extra": "tile_extra tile=%d extra=%d name=%s cause_mask=%u",
    "infrastructure_extra": (
        "infrastructure_extra id=%d name=%s cost=%d build_time=%d "
        "build_time_factor=%d"
    ),
    "city": (
        "city ref=%s name=%s tile=%d x=%d y=%d size=%d food=%d "
        "shields=%d trade=%d production_kind=%s production_id=%d "
        "production_name=%s shield_stock=%d shield_cost=%d buy_cost=%d "
        "can_buy=%d can_change=%d citizen_tile_count=%d "
        "specialist_type_count=%d worklist_length=%d build_choice_count=%d "
        "improvement_count=%d trade_route_count=%d "
        "trade_route_capacity=%u did_sell=%d allow_disband=%d "
        "new_citizens=%s options_conflict=%d airlift_remaining=%d "
        "airlift_max=%d governor_enabled=%d citizen_happy=%d "
        "citizen_content=%d citizen_unhappy=%d citizen_angry=%d "
        "citizen_workers=%d citizen_specialists=%d food_stock=%d "
        "granary_size=%d growth_turns=%d pollution=%d "
        "food_citizen_base=%d food_net=%d food_surplus=%d food_usage=%d "
        "food_waste=%d food_unhappy_penalty=%d shield_citizen_base=%d "
        "shield_net=%d shield_surplus=%d shield_usage=%d shield_waste=%d "
        "shield_unhappy_penalty=%d trade_citizen_base=%d trade_net=%d "
        "trade_surplus=%d trade_usage=%d trade_waste=%d "
        "trade_unhappy_penalty=%d gold_citizen_base=%d gold_net=%d "
        "gold_surplus=%d gold_usage=%d gold_waste=%d "
        "gold_unhappy_penalty=%d luxury_citizen_base=%d luxury_net=%d "
        "luxury_surplus=%d luxury_usage=%d luxury_waste=%d "
        "luxury_unhappy_penalty=%d science_citizen_base=%d science_net=%d "
        "science_surplus=%d science_usage=%d science_waste=%d "
        "science_unhappy_penalty=%d"
    ),
    "city_site": (
        "city_site ref=%s owner=%s name=%s tile=%d x=%d y=%d size=%d "
        "visibility=%s"
    ),
    "city_tile": (
        "city_tile city=%s tile=%d worked=%d free_worked=%d can_work=%d "
        "food=%d shields=%d trade=%d gold=%d luxury=%d science=%d"
    ),
    "city_worker_task": (
        "city_worker_task city=%s tile=%d activity=%s target_extra=%d "
        "target_extra_name=%s want=%d"
    ),
    "city_specialist": (
        "city_specialist city=%s specialist=%d name=%s count=%d "
        "counts_toward_population=%d can_use=%d is_default=%d food=%d "
        "shields=%d trade=%d gold=%d luxury=%d science=%d"
    ),
    "city_worklist": (
        "city_worklist city=%s position=%d production_kind=%s "
        "production_id=%d production_name=%s"
    ),
    "city_build_choice": (
        "city_build_choice city=%s production_kind=%s production_id=%d "
        "production_name=%s can_queue=%d can_build_now=%d shield_cost=%d "
        "shield_stock_after_change=%d turns=%d turns_with_stock=%d "
        "upkeep_food=%d upkeep_shield=%d upkeep_trade=%d upkeep_gold=%d "
        "upkeep_luxury=%d upkeep_science=%d happy_cost=%d unit_attack=%d "
        "unit_defense=%d unit_move_rate=%d unit_hp=%d unit_firepower=%d "
        "unit_vision_radius_sq=%d unit_transport_capacity=%d unit_fuel=%d "
        "unit_pop_cost=%d unit_bombard_rate=%d unit_city_size=%d "
        "unit_paradrop_range=%d building_genus=%s building_obsolete=%d "
        "building_redundant=%d building_convert=%d "
        "building_allows_units=%d building_allows_extras=%d "
        "building_prevents_disaster=%d building_protects_vs_actions=%d "
        "building_allows_actions=%d"
    ),
    "city_improvement": (
        "city_improvement city=%s improvement_id=%d name=%s sellable=%d "
        "sell_price=%d"
    ),
    "city_trade_route": (
        "city_trade_route city=%s position=%d partner=%s "
        "partner_visibility=%s partner_name=%s base_value=%d "
        "effective_value=%d direction=%s goods_id=%d goods_name=%s"
    ),
    "investigation": (
        "investigation city=%s lifecycle=%llu tile=%d name=%s size=%d "
        "production_kind=%s production_id=%d production_name=%s "
        "shield_stock=%d shield_surplus=%d improvement_count=%d "
        "feeling_count=%d specialist_count=%d"
    ),
    "investigation_improvement": (
        "investigation_improvement city=%s improvement_id=%d name=%s"
    ),
    "investigation_citizens": (
        "investigation_citizens city=%s stage=%d happy=%d content=%d "
        "unhappy=%d angry=%d"
    ),
    "investigation_specialist": (
        "investigation_specialist city=%s specialist=%d name=%s count=%d"
    ),
    "city_rally": (
        "city_rally city=%s active=%d persistent=%d vigilant=%d "
        "order_count=%d orders_digest=%s"
    ),
    "city_governor": (
        "city_governor city=%s min_food=%d min_production=%d min_trade=%d "
        "min_gold=%d min_luxury=%d min_science=%d weight_food=%d "
        "weight_production=%d weight_trade=%d weight_gold=%d "
        "weight_luxury=%d weight_science=%d celebration_weight=%d "
        "require_happy=%d maximize_growth=%d"
    ),
    "unit_own": (
        "unit ref=%s scope=own owner=%s type_id=%d type=%s home_city=%s "
        "converts_to_id=%d converts_to=%s tile=%d x=%d y=%d hp=%d "
        "veteran=%d veteran_name=%s veteran_levels=%d veteran_power=%d "
        "veteran_move_bonus=%d fuel=%d max_hp=%d max_fuel=%d move_rate=%d "
        "attack=%d defense=%d firepower=%d base_upkeep_food=%d "
        "base_upkeep_shield=%d base_upkeep_trade=%d base_upkeep_gold=%d "
        "base_upkeep_luxury=%d base_upkeep_science=%d upkeep_food=%d "
        "upkeep_shield=%d upkeep_trade=%d upkeep_gold=%d upkeep_luxury=%d "
        "upkeep_science=%d "
        "moves=%d activity=%s activity_target=%d activity_target_name=%s "
        "activity_progress=%d transport_state=%s transporter=%s "
        "transport_capacity=%d occupied=%d paradropped=%d paradrop_range=%d "
        "controller=%s has_orders=%d orders_repeat=%d orders_vigilant=%d "
        "order_count=%d orders_digest=%s orders_destination=%d "
        "action_decision_want=%s action_decision_tile=%d"
    ),
    "unit_route": (
        "unit_route unit=%s order_index=%d reconstructable=%d step_count=%d"
    ),
    "unit_route_step": (
        "unit_route_step unit=%s sequence=%d kind=%s tile=%d"
    ),
    "unit_visible": (
        "unit ref=%s scope=visible owner=%s type_id=%d type=%s tile=%d "
        "x=%d y=%d hp=%d veteran=%d veteran_name=%s veteran_levels=%d "
        "veteran_power=%d veteran_move_bonus=%d max_hp=%d max_fuel=%d "
        "move_rate=%d attack=%d defense=%d firepower=%d "
        "base_upkeep_food=%d base_upkeep_shield=%d base_upkeep_trade=%d "
        "base_upkeep_gold=%d base_upkeep_luxury=%d base_upkeep_science=%d"
    ),
    "tombstone": "tombstone ref=%c:%d:%llu kind=%s",
    "chat": (
        "chat sequence=%llu turn=%d phase=%d sender=%s sender_name=%s "
        "self=%d channel=%s event=%s truncated=%d message=%s"
    ),
    "chat_recipient": (
        "chat_recipient ref=%s name=%s self=%d connected=%d can_message=%d"
    ),
    "action": (
        "action slot=%s kind=%s actor=%s counterpart=%s "
        "meeting_generation=%llu clauses_digest=%s self_accepted=%d "
        "other_accepted=%d relation_state=%s outgoing_vision=%d "
        "outgoing_shared_tiles=%d clause_giver=%s clause_type=%s "
        "clause_value=%d clause_name=%s "
        "desired_acceptance=%d target_tile=%d source_city=%s "
        "destination_city=%s target_unit=%s "
        "transport_context=%s target_tech=%d "
        "vote_no=%d server_setting_id=%d server_setting_type=%s "
        "server_setting_min=%d server_setting_max=%d "
        "server_setting_current=%d server_setting_value=%d "
        "target_government=%d max_rate=%d "
        "route_waypoint_limit=%d "
        "infrastructure_cost=%d infrastructure_turns=%d "
        "infrastructure_choice_count=%d infrastructure_choices=%s "
        "target_build_kind=%s "
        "target_build=%d spaceship_part=%s spaceship_value=%d "
        "target_multiplier=%d multiplier_value=%d "
        "source_specialist=%d target_specialist=%d "
        "target_extra=%d subtarget_kind=%s subresults=%s activity=%s "
        "target_name=%s "
        "native_rule=%s target_kind=%s result=%s actor_consuming_always=%d "
        "legality=%s probability_kind=%s probability_min=%d "
        "probability_max=%d gold_cost=%d args=%s"
    ),
})

_STATE_SECTIONS = frozenset({
    "overview", "votes", "research", "diplomacy", "diplomacy_clauses",
    "known_tiles", "map_tiles", "cities", "units", "city_sites",
    "governments",
    "multipliers", "spaceship", "infrastructure",
    "tombstones", "city_detail", "city_citizens",
    "city_build_choices", "city_worklist", "city_improvements",
    "city_trade_routes", "tile_window", "city_governor",
    "city_worker_tasks",
    "pregame_nations", "pregame_styles", "pregame_teams", "chat",
    "chat_recipients", "unit_route",
})
_CITY_STATE_SECTIONS = frozenset({
    "city_detail", "city_citizens", "city_build_choices", "city_worklist",
    "city_improvements", "city_trade_routes", "city_governor",
    "city_worker_tasks",
})
_NATIVE_STATE_SCOPE_SECTIONS = frozenset({
    "known_tiles", "map_tiles", "tile_window", "cities", "units", "city_sites",
    "diplomacy_clauses", "city_citizens",
    "city_build_choices", "city_worklist", "city_improvements",
    "city_trade_routes", "city_governor", "target_tiles", "unit_route",
    "pregame_nations", "pregame_styles",
    "pregame_teams", "chat_recipients", "investigation",
    "action_decision_tile",
})
_UNIT_ROUTE_STEP_KINDS = frozenset({"move", "action_move", "wait"})
_MAP_TOPOLOGIES = frozenset({
    "square", "isometric_square", "hex", "isometric_hex",
})
MAX_TILE_WINDOW_RADIUS = 8
_CLIENT_STATES = frozenset({
    "initial", "disconnected", "preparing", "running", "over",
})
_PHASE_MODES = frozenset({
    "concurrent", "players_alternate", "teams_alternate",
})
_VOTE_STATUSES = frozenset({"active", "passed", "failed", "removed"})
_DIPLOMACY_STATES = frozenset({
    "Armistice", "War", "Cease-fire", "Peace", "Alliance", "Never met",
    "Team",
})
_DIPLOMACY_CANCEL_REASONS = frozenset({
    "allowed", "not_allowed", "senate_blocking",
    "alliance_problem_us", "alliance_problem_them",
})
_RESEARCH_STATES = frozenset({
    "known", "available", "reachable", "future", "unset",
})
_RESEARCH_EDGE_KINDS = frozenset({"direct", "root"})
_RESEARCH_UNLOCK_KINDS = frozenset({
    "unit", "building", "government", "action",
})
_RESEARCH_UNLOCK_SCOPES = frozenset({
    "build", "change", "actor", "target", "both",
})
_INTEL_LEVELS = frozenset({"none", "contact", "embassy"})
_PLAYER_CONTROLLERS = frozenset({"human", "ai"})
_LEGALITY = frozenset({"legal", "possibly_legal", "unresolved"})
_PROBABILITY_KINDS = frozenset({
    "exact", "range", "unknown", "not_implemented",
})
_ACTION_SUBTARGET_KINDS = frozenset({
    "none", "building", "technology", "extra", "extra_not_there",
    "specialist",
})
_ACTION_SUBRESULTS = (
    "hut_enter", "hut_frighten", "may_embark", "non_lethal",
)
_ACTION_SUBRESULT_EFFECTS: Mapping[str, str] = MappingProxyType({
    "hut_enter": "enter_huts",
    "hut_frighten": "frighten_huts",
    "may_embark": "may_embark",
    "non_lethal": "non_lethal_to_target_units",
})
_BUILD_KINDS = frozenset({"none", "unit", "improvement"})
_TRANSPORT_STATES = frozenset({"untransported", "transported", "unresolved"})
_UNIT_CONTROLLERS = frozenset({"none", "auto_work", "auto_explore"})
_ACTIVITIES = frozenset({
    "none", "idle", "cultivate", "mine", "irrigate", "fortified",
    "sentry", "pillage", "goto", "explore", "transform", "fortifying",
    "clean", "base", "road", "convert", "plant",
})
_WORKER_START_ACTIVITIES = frozenset({
    "cultivate", "mine", "irrigate", "pillage", "transform", "clean",
    "base", "road", "plant",
})
_TARGETED_ACTIVITIES = frozenset({
    "mine", "irrigate", "pillage", "clean", "base", "road",
})
_CITY_WORKER_TASK_ACTIVITIES = frozenset({
    "cultivate", "mine", "irrigate", "transform", "clean", "road",
    "plant",
})
_CITY_WORKER_TASK_TARGETED_ACTIVITIES = frozenset({
    "mine", "irrigate", "clean", "road",
})
_GOVERNMENT_STATUSES = frozenset({
    "stable", "anarchy", "anarchy_targeted", "choice_required",
    "enactment_pending",
})
_REVOLUTION_METHODS = frozenset({
    "fixed", "random", "quickening", "random_quickening",
})
_SPACESHIP_STATES = frozenset({"none", "started", "launched", "arrived"})
_SPACESHIP_PARTS = frozenset({
    "none", "structural", "fuel", "propulsion", "habitation",
    "life_support", "solar_panels",
})
_SERVER_SETTING_TYPES = frozenset({
    "none", "boolean", "integer", "string", "enum", "bitwise",
})
_NEW_CITIZENS = frozenset({"default", "science", "gold"})
_CITY_SITE_VISIBILITIES = frozenset({"own", "visible", "known"})
_TRADE_ROUTE_PARTNER_VISIBILITIES = frozenset({
    "own", "visible", "unavailable",
})
_TRADE_ROUTE_DIRECTIONS = frozenset({
    "from", "to", "bidirectional",
})
_CHAT_SENDERS = frozenset({
    "player", "observer", "connection", "server", "unknown",
})
_CHAT_CHANNELS = frozenset({"global", "allied", "private", "chat", "event"})
_CHAT_SEND_CHANNELS = ("global", "allied", "private")
_CHAT_FORBIDDEN_CODEPOINT_RANGES = (
    (0x0000, 0x001F), (0x007F, 0x009F), (0x00AD, 0x00AD),
    (0x0600, 0x0605), (0x061C, 0x061C), (0x06DD, 0x06DD),
    (0x070F, 0x070F), (0x0890, 0x0891), (0x08E2, 0x08E2),
    (0x180E, 0x180E), (0x200B, 0x200F), (0x202A, 0x202E),
    (0x2060, 0x2064), (0x2066, 0x206F), (0xFEFF, 0xFEFF),
    (0xFFF9, 0xFFFB), (0x110BD, 0x110BD), (0x110CD, 0x110CD),
    (0x13430, 0x1343F), (0x1BCA0, 0x1BCA3),
    (0x1D173, 0x1D17A), (0xE0001, 0xE0001),
    (0xE0020, 0xE007F),
)


def _chat_message_safe(message: str, encoded: bytes) -> bool:
    return (
        1 <= len(encoded) <= MAX_CHAT_MESSAGE_BYTES
        and not encoded.startswith(b" ")
        and not encoded.endswith(b" ")
        and not any(
            lower <= ord(character) <= upper
            for character in message
            for lower, upper in _CHAT_FORBIDDEN_CODEPOINT_RANGES
        )
    )
_DIPLOMACY_CLAUSE_TYPES = frozenset({
    "Advance", "Gold", "Map", "Seamap", "City", "Ceasefire", "Peace",
    "Alliance", "Vision", "Embassy", "SharedTiles",
})
_DIPLOMACY_CLAUSE_NATIVE_TYPES = MappingProxyType({
    "Advance": 0, "Gold": 1, "Map": 2, "Seamap": 3, "City": 4,
    "Ceasefire": 5, "Peace": 6, "Alliance": 7, "Vision": 8,
    "Embassy": 9, "SharedTiles": 10,
})
_DIPLOMACY_CLAUSE_PUBLIC_TYPES = MappingProxyType({
    "Advance": "technology",
    "Gold": "gold",
    "Map": "map",
    "Seamap": "sea_map",
    "City": "city",
    "Ceasefire": "ceasefire",
    "Peace": "peace",
    "Alliance": "alliance",
    "Vision": "vision",
    "Embassy": "embassy",
    "SharedTiles": "shared_tiles",
})
_DIPLOMACY_CLAUSE_VALUE_KINDS = frozenset({
    "none", "technology", "gold", "city", "city_unavailable",
})


class V2ControlError(RuntimeError):
    """A detail-free error safe to map to the public v2 error envelope."""

    def __init__(
        self, code: str, *, details: Mapping[str, Any] | None = None,
    ) -> None:
        self.code = code
        self.details = dict(details or {})
        super().__init__(code)


class _ObservationError(Exception):
    pass


@dataclass(frozen=True)
class _NativeActionRule:
    native_kind: str
    public_kind: str
    operation: str
    variant: str
    target_kind: str
    result: str
    consuming: bool
    args: str


# These are generated action rule names from actions_enums_gen.h, paired with
# their fixed target/result contracts from actres. Unknown rules fail closed.
_ACTION_RULES: Mapping[str, _NativeActionRule] = MappingProxyType({
    "pregame.configure": _NativeActionRule(
        "pregame.configure", "pregame.configure", "configure", "standard",
        "Pregame Configuration", "Configuration Changed", False,
        "pregame-config-required",
    ),
    "pregame.set_ready": _NativeActionRule(
        "pregame.set_ready", "pregame.set_ready", "set_ready", "standard",
        "Pregame Readiness", "Readiness Changed", False,
        "pregame-ready-required",
    ),
    "pregame.set_team": _NativeActionRule(
        "pregame.set_team", "pregame.set_team", "set_team", "standard",
        "Pregame Team", "Team Changed", False,
        "pregame-team-required",
    ),
    "player.cast_vote": _NativeActionRule(
        "player.cast_vote", "player.cast_vote", "cast_vote", "standard",
        "Vote", "Vote Recorded", False, "vote-required",
    ),
    "player.cancel_vote": _NativeActionRule(
        "player.cancel_vote", "player.cancel_vote", "cancel_vote", "standard",
        "Vote", "Vote Cancelled", False, "none",
    ),
    "player.propose_server_setting_boolean": _NativeActionRule(
        "player.propose_server_setting", "player.propose_server_setting",
        "propose_server_setting", "boolean", "Server Setting Vote",
        "Vote Proposed Or Setting Applied", False, "none",
    ),
    "player.propose_server_setting_integer": _NativeActionRule(
        "player.propose_server_setting", "player.propose_server_setting",
        "propose_server_setting", "integer", "Server Setting Vote",
        "Vote Proposed Or Setting Applied", False,
        "server-setting-integer-required",
    ),
    "player.propose_server_setting_string": _NativeActionRule(
        "player.propose_server_setting", "player.propose_server_setting",
        "propose_server_setting", "string", "Server Setting Vote",
        "Vote Proposed Or Setting Applied", False,
        "server-setting-string-required",
    ),
    "player.propose_server_setting_enum": _NativeActionRule(
        "player.propose_server_setting", "player.propose_server_setting",
        "propose_server_setting", "enum", "Server Setting Vote",
        "Vote Proposed Or Setting Applied", False, "none",
    ),
    "player.propose_server_setting_bitwise": _NativeActionRule(
        "player.propose_server_setting", "player.propose_server_setting",
        "propose_server_setting", "bitwise", "Server Setting Vote",
        "Vote Proposed Or Setting Applied", False,
        "server-setting-bitwise-required",
    ),
    "player.surrender": _NativeActionRule(
        "player.surrender", "player.surrender", "surrender", "standard",
        "Player", "Surrender Recorded", False, "none",
    ),
    "phase.end": _NativeActionRule(
        "phase.end", "phase.end", "end", "standard", "player",
        "phase_end", False, "none",
    ),
    "research.set_target": _NativeActionRule(
        "research.set_target", "research.set_target", "set_target",
        "standard", "Technology", "Research Target", False, "none",
    ),
    "research.set_goal": _NativeActionRule(
        "research.set_goal", "research.set_goal", "set_goal", "standard",
        "Technology", "Research Goal", False, "none",
    ),
    "economy.set_rates": _NativeActionRule(
        "economy.set_rates", "economy.set_rates", "set_rates", "standard",
        "Player", "Economic Rates", False, "rates-required",
    ),
    "player.send_chat": _NativeActionRule(
        "player.send_chat", "player.send_chat", "send_chat", "standard",
        "Chat Channel", "Chat Echo Received", False, "chat-required",
    ),
    "government.revolution": _NativeActionRule(
        "government.revolution", "government.revolution", "revolution",
        "standard", "Government", "Revolution Started", False, "none",
    ),
    "government.change": _NativeActionRule(
        "government.change", "government.change", "change", "standard",
        "Government", "Government Choice Recorded", False, "none",
    ),
    "player.set_multiplier": _NativeActionRule(
        "player.set_multiplier", "player.set_multiplier", "set_multiplier",
        "standard", "Multiplier", "Multiplier Target Changed", False,
        "multiplier-value-required",
    ),
    "spaceship.place_component": _NativeActionRule(
        "spaceship.place_component", "spaceship.place_component",
        "place_component", "standard", "Spaceship Part",
        "Spaceship Part Placed", False, "none",
    ),
    "spaceship.launch": _NativeActionRule(
        "spaceship.launch", "spaceship.launch", "launch", "standard",
        "Spaceship", "Spaceship Launched", False, "none",
    ),
    "diplomacy.open_meeting": _NativeActionRule(
        "diplomacy.open_meeting", "diplomacy.meeting", "open_meeting",
        "standard", "Diplomatic Relation", "Meeting Opened", False, "none",
    ),
    "diplomacy.close_meeting": _NativeActionRule(
        "diplomacy.close_meeting", "diplomacy.meeting", "close_meeting",
        "standard", "Diplomatic Relation", "Meeting Closed", False, "none",
    ),
    "diplomacy.propose_clause": _NativeActionRule(
        "diplomacy.propose_clause", "diplomacy.clause", "propose_clause",
        "standard", "Diplomatic Relation", "Clause Proposed", False, "none",
    ),
    "diplomacy.propose_gold": _NativeActionRule(
        "diplomacy.propose_clause", "diplomacy.clause", "propose_clause",
        "standard", "Diplomatic Relation", "Clause Proposed", False,
        "gold-required",
    ),
    "diplomacy.remove_clause": _NativeActionRule(
        "diplomacy.remove_clause", "diplomacy.clause", "remove_clause",
        "standard", "Diplomatic Relation", "Clause Removed", False, "none",
    ),
    "diplomacy.accept": _NativeActionRule(
        "diplomacy.accept", "diplomacy.acceptance", "accept", "standard",
        "Diplomatic Relation", "Acceptance Recorded", False, "none",
    ),
    "diplomacy.withdraw_acceptance": _NativeActionRule(
        "diplomacy.withdraw_acceptance", "diplomacy.acceptance",
        "withdraw_acceptance", "standard", "Diplomatic Relation",
        "Acceptance Withdrawn", False, "none",
    ),
    "diplomacy.break_relation": _NativeActionRule(
        "diplomacy.break_relation", "diplomacy.relation", "break_relation",
        "standard", "Diplomatic Relation", "Relation Changed", False, "none",
    ),
    "diplomacy.withdraw_vision": _NativeActionRule(
        "diplomacy.withdraw_vision", "diplomacy.withdraw",
        "withdraw_vision", "standard", "Diplomatic Relation",
        "Vision Withdrawn", False, "none",
    ),
    "diplomacy.withdraw_shared_tiles": _NativeActionRule(
        "diplomacy.withdraw_shared_tiles", "diplomacy.withdraw",
        "withdraw_shared_tiles", "standard", "Diplomatic Relation",
        "Shared Tiles Withdrawn", False, "none",
    ),
    "city.set_production": _NativeActionRule(
        "city.set_production", "city.set_production", "set_production",
        "standard", "Production", "Production Changed", False, "none",
    ),
    "city.buy_production": _NativeActionRule(
        "city.buy_production", "city.buy_production", "buy_production",
        "standard", "Production", "Production Bought", False, "none",
    ),
    "city.work_tile": _NativeActionRule(
        "city.work_tile", "city.assign_citizen", "work_tile", "standard",
        "City Tile", "Citizen Assigned", False, "none",
    ),
    "city.unwork_tile": _NativeActionRule(
        "city.unwork_tile", "city.assign_citizen", "unwork_tile", "standard",
        "City Tile", "Citizen Unassigned", False, "none",
    ),
    "city.set_specialist": _NativeActionRule(
        "city.set_specialist", "city.set_specialist", "set_specialist",
        "standard", "Specialist", "Specialist Changed", False, "none",
    ),
    "city.set_worklist": _NativeActionRule(
        "city.set_worklist", "city.set_worklist", "set_worklist",
        "standard", "City", "Worklist Changed", False,
        "worklist-required",
    ),
    "city.set_options": _NativeActionRule(
        "city.set_options", "city.set_options", "set_options",
        "standard", "City", "City Options Changed", False,
        "city-options-required",
    ),
    "city.rename": _NativeActionRule(
        "city.rename", "city.rename", "rename", "standard", "City",
        "City Renamed", False, "city_name-required",
    ),
    "city.sell_improvement": _NativeActionRule(
        "city.sell_improvement", "city.sell_improvement",
        "sell_improvement", "standard", "Improvement",
        "Improvement Sold", False, "none",
    ),
    "city.set_rally": _NativeActionRule(
        "city.set_rally", "city.set_rally", "set_rally", "opaque",
        "Tile", "Rally Point Set", False, "persistent-required",
    ),
    "city.clear_rally": _NativeActionRule(
        "city.clear_rally", "city.set_rally", "clear_rally", "opaque",
        "City", "Rally Point Cleared", False, "none",
    ),
    "city.set_governor": _NativeActionRule(
        "city.set_governor", "city.set_governor", "set_governor",
        "standard", "City", "Governor Goal Set", False,
        "governor-goal-required",
    ),
    "city.clear_governor": _NativeActionRule(
        "city.clear_governor", "city.set_governor", "clear_governor",
        "standard", "City", "Governor Cleared", False, "none",
    ),
    "city.request_worker_task": _NativeActionRule(
        "city.request_worker_task", "city.manage_worker_task",
        "request_worker_task", "standard", "City Worker Task",
        "Worker Task Requested", False, "none",
    ),
    "city.change_worker_task": _NativeActionRule(
        "city.change_worker_task", "city.manage_worker_task",
        "change_worker_task", "standard", "City Worker Task",
        "Worker Task Changed", False, "none",
    ),
    "city.remove_worker_task": _NativeActionRule(
        "city.remove_worker_task", "city.manage_worker_task",
        "remove_worker_task", "standard", "City Worker Task",
        "Worker Task Removed", False, "none",
    ),
    "unit.start_activity": _NativeActionRule(
        "unit.start_activity", "unit.perform_action", "start_activity",
        "standard", "Worker Activity", "Activity Installed", False, "none",
    ),
    "unit.cancel_activity": _NativeActionRule(
        "unit.cancel_activity", "unit.order", "cancel_activity", "standard",
        "Unit", "Activity Cancelled", False, "none",
    ),
    "unit.sentry": _NativeActionRule(
        "unit.sentry", "unit.order", "sentry", "opaque", "Unit",
        "Sentry Installed", False, "none",
    ),
    "unit.auto_work": _NativeActionRule(
        "unit.auto_work", "unit.order", "auto_work", "opaque", "Unit",
        "Auto Work Installed", False, "none",
    ),
    "unit.auto_explore": _NativeActionRule(
        "unit.auto_explore", "unit.order", "auto_explore", "opaque", "Unit",
        "Auto Explore Installed", False, "none",
    ),
    "unit.cancel_automation": _NativeActionRule(
        "unit.cancel_automation", "unit.order", "cancel_automation", "opaque",
        "Unit", "Automation Cancelled", False, "none",
    ),
    "unit.cancel_orders": _NativeActionRule(
        "unit.cancel_orders", "unit.order", "cancel_orders", "opaque",
        "Unit", "Orders Cancelled", False, "none",
    ),
    "unit.clear_action_decision": _NativeActionRule(
        "unit.clear_action_decision", "unit.order",
        "clear_action_decision", "opaque", "Action Decision",
        "Action Decision Cleared", False, "none",
    ),
    "unit.goto": _NativeActionRule(
        "unit.goto", "unit.order", "goto", "opaque", "Tile",
        "Orders Queued", False, "none",
    ),
    "unit.goto_and_perform": _NativeActionRule(
        "unit.goto_and_perform", "unit.order", "goto_and_perform",
        "opaque", "Action Route", "Orders Queued", False, "none",
    ),
    "unit.connect_route": _NativeActionRule(
        "unit.connect_route", "unit.order", "connect_route", "opaque",
        "Construction Route", "Orders Queued", False, "none",
    ),
    "unit.set_route": _NativeActionRule(
        "unit.set_route", "unit.order", "set_route", "opaque", "Route",
        "Orders Queued", False, "route-required",
    ),
    "unit.attack_route": _NativeActionRule(
        "unit.attack_route", "unit.order", "attack_route", "opaque",
        "Attack Route", "Orders Queued", False, "attack-route-required",
    ),
    "player.place_infrastructure": _NativeActionRule(
        "player.place_infrastructure", "player.set_infrastructure",
        "place_infrastructure", "opaque", "Tile",
        "Infrastructure Placement Started", False,
        "infrastructure-extra-required",
    ),
    "Fortify": _NativeActionRule(
        "unit.fortify", "unit.perform_action", "fortify", "opaque", "Self",
        "Fortify Installed", False, "none",
    ),
    "Fortify 2": _NativeActionRule(
        "unit.fortify", "unit.perform_action", "fortify", "opaque", "Self",
        "Fortify Installed", False, "none",
    ),
    "Convert Unit": _NativeActionRule(
        "unit.convert", "unit.perform_action", "convert", "opaque", "Self",
        "Conversion Installed", False, "none",
    ),
    "Disband Unit": _NativeActionRule(
        "unit.disband", "unit.perform_action", "disband", "opaque", "Self",
        "Unit Disbanded", True, "none",
    ),
    "Unit Make Homeless": _NativeActionRule(
        "unit.homeless", "unit.perform_action", "make_homeless", "opaque",
        "Self", "Home City Cleared", False, "none",
    ),
    "Airlift Unit": _NativeActionRule(
        "unit.airlift", "unit.perform_action", "airlift", "opaque", "City",
        "Unit Airlift", False, "none",
    ),
    "Upgrade Unit": _NativeActionRule(
        "unit.upgrade", "unit.perform_action", "upgrade", "opaque", "City",
        "Unit Upgrade", False, "none",
    ),
    "Home City": _NativeActionRule(
        "unit.rehome", "unit.perform_action", "rehome", "opaque", "City",
        "Unit Home City", False, "none",
    ),
    "Join City": _NativeActionRule(
        "unit.join_city", "unit.perform_action", "join_city", "opaque", "City",
        "Unit Join City", True, "none",
    ),
    "Establish Trade Route": _NativeActionRule(
        "unit.establish_trade", "unit.perform_action", "establish_trade",
        "opaque", "City", "Unit Establish Trade Route", True, "none",
    ),
    "Enter Marketplace": _NativeActionRule(
        "unit.marketplace", "unit.perform_action", "marketplace", "opaque",
        "City", "Unit Enter Marketplace", True, "none",
    ),
    "Help Wonder": _NativeActionRule(
        "unit.help_wonder", "unit.perform_action", "help_wonder", "opaque",
        "City", "Unit Help Wonder", True, "none",
    ),
    "Disband Unit Recover": _NativeActionRule(
        "unit.disband_recover", "unit.perform_action", "disband_recover",
        "opaque", "City", "Unit Disband Recover", True, "none",
    ),
    "Paradrop Unit": _NativeActionRule(
        "unit.paradrop", "unit.perform_action", "paradrop", "opaque", "Tile",
        "Unit Paradrop", False, "none",
    ),
    "Paradrop Unit Frighten": _NativeActionRule(
        "unit.paradrop", "unit.perform_action", "paradrop", "opaque", "Tile",
        "Unit Paradrop", False, "none",
    ),
    "Paradrop Unit Enter": _NativeActionRule(
        "unit.paradrop", "unit.perform_action", "paradrop", "opaque", "Tile",
        "Unit Paradrop", False, "none",
    ),
    "Teleport": _NativeActionRule(
        "unit.teleport", "unit.perform_action", "teleport", "opaque", "Tile",
        "Teleport", False, "none",
    ),
    "Teleport2": _NativeActionRule(
        "unit.teleport", "unit.perform_action", "teleport", "opaque", "Tile",
        "Teleport", False, "none",
    ),
    "Teleport3": _NativeActionRule(
        "unit.teleport", "unit.perform_action", "teleport", "opaque", "Tile",
        "Teleport", False, "none",
    ),
    "Teleport Frighten": _NativeActionRule(
        "unit.teleport", "unit.perform_action", "teleport", "opaque", "Tile",
        "Teleport", False, "none",
    ),
    "Teleport Enter": _NativeActionRule(
        "unit.teleport", "unit.perform_action", "teleport", "opaque", "Tile",
        "Teleport", False, "none",
    ),
    "Transport Board": _NativeActionRule(
        "unit.board", "unit.perform_action", "board", "opaque", "Unit",
        "Unit Transport Board", False, "none",
    ),
    "Transport Board 2": _NativeActionRule(
        "unit.board", "unit.perform_action", "board", "opaque", "Unit",
        "Unit Transport Board", False, "none",
    ),
    "Transport Board_3": _NativeActionRule(
        "unit.board", "unit.perform_action", "board", "opaque", "Unit",
        "Unit Transport Board", False, "none",
    ),
    "Transport Deboard": _NativeActionRule(
        "unit.deboard", "unit.perform_action", "deboard", "opaque", "Unit",
        "Unit Transport Deboard", False, "none",
    ),
    "Transport Embark": _NativeActionRule(
        "unit.embark", "unit.perform_action", "embark", "opaque", "Unit",
        "Unit Transport Embark", False, "none",
    ),
    "Transport Embark 2": _NativeActionRule(
        "unit.embark", "unit.perform_action", "embark", "opaque", "Unit",
        "Unit Transport Embark", False, "none",
    ),
    "Transport Embark 3": _NativeActionRule(
        "unit.embark", "unit.perform_action", "embark", "opaque", "Unit",
        "Unit Transport Embark", False, "none",
    ),
    "Transport Embark 4": _NativeActionRule(
        "unit.embark", "unit.perform_action", "embark", "opaque", "Unit",
        "Unit Transport Embark", False, "none",
    ),
    "Transport Disembark": _NativeActionRule(
        "unit.disembark", "unit.perform_action", "disembark", "opaque",
        "Tile", "Unit Transport Disembark", False, "none",
    ),
    "Transport Disembark 2": _NativeActionRule(
        "unit.disembark", "unit.perform_action", "disembark", "opaque",
        "Tile", "Unit Transport Disembark", False, "none",
    ),
    "Transport Disembark 3": _NativeActionRule(
        "unit.disembark", "unit.perform_action", "disembark", "opaque",
        "Tile", "Unit Transport Disembark", False, "none",
    ),
    "Transport Disembark 4": _NativeActionRule(
        "unit.disembark", "unit.perform_action", "disembark", "opaque",
        "Tile", "Unit Transport Disembark", False, "none",
    ),
    "Transport Load": _NativeActionRule(
        "unit.load", "unit.perform_action", "load", "opaque", "Unit",
        "Unit Transport Load", False, "none",
    ),
    "Transport Load 2": _NativeActionRule(
        "unit.load", "unit.perform_action", "load", "opaque", "Unit",
        "Unit Transport Load", False, "none",
    ),
    "Transport Load 3": _NativeActionRule(
        "unit.load", "unit.perform_action", "load", "opaque", "Unit",
        "Unit Transport Load", False, "none",
    ),
    "Transport Unload": _NativeActionRule(
        "unit.unload", "unit.perform_action", "unload", "opaque", "Unit",
        "Unit Transport Unload", False, "none",
    ),
    "Found City": _NativeActionRule(
        "city.found", "unit.perform_action", "found_city", "standard",
        "Tile", "Unit Found City", True, "city_name-required",
    ),
    "Unit Move": _NativeActionRule(
        "unit.move", "unit.order", "move", "standard", "Tile", "Unit Move",
        False, "none",
    ),
    "Unit Move 2": _NativeActionRule(
        "unit.move", "unit.order", "move", "alternative_2", "Tile",
        "Unit Move", False, "none",
    ),
    "Unit Move 3": _NativeActionRule(
        "unit.move", "unit.order", "move", "alternative_3", "Tile",
        "Unit Move", False, "none",
    ),
    "Attack": _NativeActionRule(
        "unit.attack", "unit.perform_action", "attack", "standard", "Stack",
        "Unit Attack", False, "none",
    ),
    "Attack 2": _NativeActionRule(
        "unit.attack", "unit.perform_action", "attack", "alternative_2",
        "Stack", "Unit Attack", False, "none",
    ),
    "Suicide Attack": _NativeActionRule(
        "unit.attack", "unit.perform_action", "suicide_attack", "standard",
        "Stack", "Unit Attack", True, "none",
    ),
    "Suicide Attack 2": _NativeActionRule(
        "unit.attack", "unit.perform_action", "suicide_attack",
        "alternative_2", "Stack", "Unit Attack", True, "none",
    ),
})

_PROJECTED_PUBLIC_ACTION_KINDS = frozenset(
    rule.public_kind for rule in _ACTION_RULES.values()
)
_PROJECTED_NATIVE_ACTION_KINDS = frozenset(
    rule.native_kind for rule in _ACTION_RULES.values()
)
assert_projected_action_contract(
    _PROJECTED_PUBLIC_ACTION_KINDS, _PROJECTED_NATIVE_ACTION_KINDS,
)


@dataclass(frozen=True)
class _SpecialActionRule:
    operation: str
    target_kind: str
    label: str
    native_subtarget: str = "none"
    allowed_subresult_sets: tuple[tuple[str, ...], ...] = ((),)
    probability_policy: str = "resolved"
    native_rules: tuple[str, ...] = ()
    gold_cost_policy: str = "none"


# Freeciv has four fixed ruleset-defined user-action slots.  Their native
# result is intentionally empty: a ruleset supplies the effects and the normal
# server remains authoritative.  Project them through one generic semantic
# operation while keeping the concrete slot and target binding private.
_RULESET_CUSTOM_ACTION_RESULT = "Ruleset Custom"
_RULESET_CUSTOM_ACTION_RULES = frozenset({
    "User Action 1", "User Action 2", "User Action 3", "User Action 4",
})
_RULESET_CUSTOM_TARGET_KINDS = frozenset({
    "City", "Unit", "Stack", "Tile", "Extras", "Self",
})

# These sub-results are fixed metadata on otherwise ordinary native actions.
# They are not caller-controlled arguments; the opaque action slot selects the
# complete semantic.  Paradrop's may-embark bit is ruleset-dependent.
_NON_SPECIAL_ACTION_SUBRESULT_SETS: Mapping[
    str, tuple[tuple[str, ...], ...]
] = MappingProxyType({
    "Paradrop Unit": ((), ("may_embark",)),
    "Paradrop Unit Frighten": (
        ("hut_frighten",), ("hut_frighten", "may_embark"),
    ),
    "Paradrop Unit Enter": (
        ("hut_enter",), ("hut_enter", "may_embark"),
    ),
    "Teleport Frighten": (("hut_frighten",),),
    "Teleport Enter": (("hut_enter",),),
})


def _non_special_action_metadata_supported(
    action: Mapping[str, Any],
) -> bool:
    if action["native_rule"] == "unit.start_activity":
        targeted = action["activity"] in _TARGETED_ACTIVITIES
        subtarget_supported = action["subtarget_kind"] in (
            {"extra", "extra_not_there"} if targeted else {"none"}
        )
    else:
        subtarget_supported = action["subtarget_kind"] == "none"
    return (
        subtarget_supported
        and action["subresults"] in _NON_SPECIAL_ACTION_SUBRESULT_SETS.get(
            action["native_rule"], ((),),
        )
    )


# This is a semantic allowlist for hard-coded action results plus the generic
# closed custom-action family above. Native target discovery may report more
# Freeciv actions, but quoted costs, selectable subtargets, and special results
# stay fail-closed until their full server-authoritative contract is modeled.
_SPECIAL_ACTION_RESULTS: Mapping[
    tuple[str, str], _SpecialActionRule
] = MappingProxyType({
    ("Unit Establish Embassy", "City"): _SpecialActionRule(
        "establish_embassy", "City", "Establish an embassy",
    ),
    ("Unit Investigate City", "City"): _SpecialActionRule(
        "investigate_city", "City", "Investigate city",
    ),
    ("Unit Poison City", "City"): _SpecialActionRule(
        "poison_city", "City", "Poison city water supply",
    ),
    ("Unit Steal Gold", "City"): _SpecialActionRule(
        "steal_gold", "City", "Steal gold from city",
    ),
    ("Unit Bribe Unit", "Unit"): _SpecialActionRule(
        "bribe_unit", "Unit", "Bribe target unit",
        native_rules=("Bribe Unit",),
        gold_cost_policy="quoted_maximum",
    ),
    ("Unit Bribe Stack", "Stack"): _SpecialActionRule(
        "bribe_stack", "Stack", "Bribe target unit stack",
        native_rules=("Bribe Stack",),
        gold_cost_policy="quoted_maximum",
    ),
    ("Unit Incite City", "City"): _SpecialActionRule(
        "incite_city", "City", "Incite city revolt",
        probability_policy="unresolved",
        native_rules=("Incite City", "Incite City Escape"),
        gold_cost_policy="quoted_maximum",
    ),
    ("Unit Sabotage City", "City"): _SpecialActionRule(
        "sabotage_city", "City", "Sabotage a random city improvement",
        probability_policy="unresolved",
        native_rules=("Sabotage City", "Sabotage City Escape"),
    ),
    ("Unit Targeted Sabotage City", "City"): _SpecialActionRule(
        "sabotage_building", "City",
        "Sabotage selected city improvement",
        native_subtarget="building",
        probability_policy="unresolved",
        native_rules=(
            "Targeted Sabotage City",
            "Targeted Sabotage City Escape",
        ),
    ),
    ("Unit Sabotage City Production", "City"): _SpecialActionRule(
        "sabotage_production", "City", "Sabotage city production",
        probability_policy="unresolved",
        native_rules=("Sabotage City Production Escape",),
    ),
    ("Unit Steal Tech", "City"): _SpecialActionRule(
        "steal_technology", "City", "Steal a random technology",
        probability_policy="unresolved",
        native_rules=("Steal Tech", "Steal Tech Escape Expected"),
    ),
    ("Unit Targeted Steal Tech", "City"): _SpecialActionRule(
        "steal_technology", "City", "Steal selected technology",
        native_subtarget="technology",
        probability_policy="native",
        native_rules=(
            "Targeted Steal Tech",
            "Targeted Steal Tech Escape Expected",
        ),
    ),
    ("Unit Sabotage Unit", "Unit"): _SpecialActionRule(
        "sabotage_unit", "Unit", "Sabotage unit",
    ),
    ("Unit Capture Units", "Stack"): _SpecialActionRule(
        "capture_units", "Stack", "Capture unit stack",
    ),
    ("Unit Steal Maps", "City"): _SpecialActionRule(
        "steal_maps", "City", "Steal maps",
    ),
    ("Unit Bombard", "Stack"): _SpecialActionRule(
        "bombard", "Stack", "Bombard target stack",
        allowed_subresult_sets=(("non_lethal",),),
    ),
    ("Unit Suitcase Nuke", "City"): _SpecialActionRule(
        "suitcase_nuke", "City", "Detonate suitcase nuclear device",
    ),
    ("Unit Nuke", "City"): _SpecialActionRule(
        "nuke_city", "City", "Launch nuclear attack on city",
    ),
    ("Unit Nuke", "Tile"): _SpecialActionRule(
        "nuke_tile", "Tile", "Launch nuclear attack on tile",
    ),
    ("Unit Nuke Units", "Stack"): _SpecialActionRule(
        "nuke_units", "Stack", "Launch nuclear attack on unit stack",
    ),
    ("Unit Destroy City", "City"): _SpecialActionRule(
        "destroy_city", "City", "Destroy city",
    ),
    ("Unit Expel Unit", "Unit"): _SpecialActionRule(
        "expel_unit", "Unit", "Expel unit",
    ),
    ("Unit Surgical Strike Production", "City"): _SpecialActionRule(
        "strike_production", "City", "Strike city production",
    ),
    ("Unit Surgical Strike Building", "City"): _SpecialActionRule(
        "strike_building", "City", "Strike selected city improvement",
        native_subtarget="building",
        native_rules=("Surgical Strike Building",),
    ),
    ("Unit Conquer City", "City"): _SpecialActionRule(
        "conquer_city", "City", "Conquer city",
    ),
    ("Unit Heal Unit", "Unit"): _SpecialActionRule(
        "heal_unit", "Unit", "Heal unit",
    ),
    ("Collect Ransom", "Stack"): _SpecialActionRule(
        "collect_ransom", "Stack", "Collect ransom",
    ),
    ("Unit Spread Plague", "City"): _SpecialActionRule(
        "spread_plague", "City", "Spread plague",
    ),
    ("Unit Spy Attack", "Stack"): _SpecialActionRule(
        "spy_attack", "Stack", "Attack spy stack",
    ),
    ("Unit Paradrop Conquer", "Tile"): _SpecialActionRule(
        "paradrop_conquer", "Tile", "Paradrop and conquer tile",
        allowed_subresult_sets=(
            (), ("may_embark",),
            ("hut_enter",), ("hut_enter", "may_embark"),
            ("hut_frighten",), ("hut_frighten", "may_embark"),
        ),
        probability_policy="unresolved",
        native_rules=(
            "Paradrop Unit Conquer", "Paradrop Unit Frighten Conquer",
            "Paradrop Unit Enter Conquer",
        ),
    ),
    ("Wipe Units", "Stack"): _SpecialActionRule(
        "wipe_units", "Stack", "Wipe unit stack",
    ),
    ("Unit Spy Escape", "City"): _SpecialActionRule(
        "spy_escape", "City", "Escape after spy mission",
    ),
    ("Teleport Conquer", "Tile"): _SpecialActionRule(
        "teleport_conquer", "Tile", "Teleport and conquer tile",
        allowed_subresult_sets=(
            (), ("hut_enter",), ("hut_frighten",),
        ),
    ),
    ("Unit Conquer Extras", "Extras"): _SpecialActionRule(
        "conquer_extras", "Extras", "Conquer tile extras",
    ),
    ("Unit Enter Hut", "Tile"): _SpecialActionRule(
        "enter_hut", "Tile", "Enter hut",
        allowed_subresult_sets=(("hut_enter",),),
        probability_policy="unresolved",
        native_rules=(
            "Enter Hut", "Enter Hut 2", "Enter Hut 3", "Enter Hut 4",
        ),
    ),
    ("Unit Frighten Hut", "Tile"): _SpecialActionRule(
        "frighten_hut", "Tile", "Frighten hut",
        allowed_subresult_sets=(("hut_frighten",),),
        probability_policy="unresolved",
        native_rules=(
            "Frighten Hut", "Frighten Hut 2", "Frighten Hut 3",
            "Frighten Hut 4",
        ),
    ),
})


def _special_action_rule(
    action: Mapping[str, Any],
) -> _SpecialActionRule | None:
    rule = _SPECIAL_ACTION_RESULTS.get((
        action["result"], action["target_kind"],
    ))
    if rule is not None:
        return rule
    if (
        action["result"] == _RULESET_CUSTOM_ACTION_RESULT
        and action["native_rule"] in _RULESET_CUSTOM_ACTION_RULES
        and action["target_kind"] in _RULESET_CUSTOM_TARGET_KINDS
        and action["subtarget_kind"] == "none"
        and action["subresults"] == ()
    ):
        return _SpecialActionRule(
            "ruleset_action", action["target_kind"],
            action["target_name"],
            native_rules=tuple(
                sorted(_RULESET_CUSTOM_ACTION_RULES)
            ),
        )
    return None


def _derive_native_schema_id() -> str:
    """Fingerprint the exact private row and action-contract grammar.

    The native client carries the resulting literal in its CAPS frame.  Keep
    the canonical representation deliberately small and independent of the
    public JSON representation: any ordered row-field change or any native
    action binding change must require a freshly built native client.
    """
    action_fields = tuple(_NativeActionRule.__dataclass_fields__)
    canonical = {
        "format": "freeciv-agent-observation-action-schema-v1",
        "row_fields": [
            [row_kind, list(fields)]
            for row_kind, fields in _ROW_FIELDS.items()
        ],
        "row_formats": [
            [row_kind, _ROW_FORMAT_CONTRACTS[row_kind]]
            for row_kind in _ROW_FIELDS
        ],
        "action_rule_fields": list(action_fields),
        "action_rules": [
            [name, *[getattr(rule, field) for field in action_fields]]
            for name, rule in sorted(_ACTION_RULES.items())
        ],
        "special_action_contract": [
            {
                "native_result": result,
                "target_kind": target_kind,
                "operation": rule.operation,
                "cost": rule.gold_cost_policy,
                "subtarget": rule.native_subtarget,
                "subresult_sets": rule.allowed_subresult_sets,
                "probability_policy": rule.probability_policy,
                "native_rules": rule.native_rules,
            }
            for (result, target_kind), rule in sorted(
                _SPECIAL_ACTION_RESULTS.items()
            )
        ],
        "generic_special_action_contract": {
            "native_result": _RULESET_CUSTOM_ACTION_RESULT,
            "native_rules": sorted(_RULESET_CUSTOM_ACTION_RULES),
            "target_kinds": sorted(_RULESET_CUSTOM_TARGET_KINDS),
            "operation": "ruleset_action",
            "cost": "none",
            "subtarget": "none",
            "subresult_sets": [[]],
            "probability_policy": "resolved",
            "label": "native_ui_name",
        },
        "non_special_subresults": [
            [name, result_sets]
            for name, result_sets in sorted(
                _NON_SPECIAL_ACTION_SUBRESULT_SETS.items()
            )
        ],
        "value_domains": {
            "action_subtarget_kinds": sorted(_ACTION_SUBTARGET_KINDS),
            "action_subresults": list(_ACTION_SUBRESULTS),
            "action_subresult_effects": dict(_ACTION_SUBRESULT_EFFECTS),
            "map_topologies": sorted(_MAP_TOPOLOGIES),
            "client_states": sorted(_CLIENT_STATES),
            "phase_modes": sorted(_PHASE_MODES),
            "vote_statuses": sorted(_VOTE_STATUSES),
            "diplomacy_states": sorted(_DIPLOMACY_STATES),
            "diplomacy_cancel_reasons": sorted(
                _DIPLOMACY_CANCEL_REASONS
            ),
            "diplomacy_clause_types": sorted(_DIPLOMACY_CLAUSE_TYPES),
            "diplomacy_clause_value_kinds": sorted(
                _DIPLOMACY_CLAUSE_VALUE_KINDS
            ),
            "research_states": sorted(_RESEARCH_STATES),
            "research_edge_kinds": sorted(_RESEARCH_EDGE_KINDS),
            "research_unlock_kinds": sorted(_RESEARCH_UNLOCK_KINDS),
            "research_unlock_scopes": sorted(_RESEARCH_UNLOCK_SCOPES),
            "intel_levels": sorted(_INTEL_LEVELS),
            "player_controllers": sorted(_PLAYER_CONTROLLERS),
            "legality": sorted(_LEGALITY),
            "probability_kinds": sorted(_PROBABILITY_KINDS),
            "build_kinds": sorted(_BUILD_KINDS),
            "transport_states": sorted(_TRANSPORT_STATES),
            "unit_controllers": sorted(_UNIT_CONTROLLERS),
            "activities": sorted(_ACTIVITIES),
            "worker_start_activities": sorted(_WORKER_START_ACTIVITIES),
            "targeted_activities": sorted(_TARGETED_ACTIVITIES),
            "government_statuses": sorted(_GOVERNMENT_STATUSES),
            "revolution_methods": sorted(_REVOLUTION_METHODS),
            "spaceship_states": sorted(_SPACESHIP_STATES),
            "spaceship_parts": sorted(_SPACESHIP_PARTS),
            "new_citizens": sorted(_NEW_CITIZENS),
            "city_site_visibilities": sorted(_CITY_SITE_VISIBILITIES),
            "trade_route_partner_visibilities": sorted(
                _TRADE_ROUTE_PARTNER_VISIBILITIES
            ),
            "trade_route_directions": sorted(_TRADE_ROUTE_DIRECTIONS),
            "chat_senders": sorted(_CHAT_SENDERS),
            "chat_channels": sorted(_CHAT_CHANNELS),
            "chat_send_channels": list(_CHAT_SEND_CHANNELS),
            "extra_cause_tags_by_bit": list(_EXTRA_CAUSE_TAGS),
            "unit_scopes": ["own", "visible"],
            "unit_route_step_kinds": sorted(_UNIT_ROUTE_STEP_KINDS),
            "tile_known": [0, 1, 2],
            "tombstone_kinds": ["player", "city", "unit"],
            "booleans": [0, 1],
        },
        "sentinels": {
            "cache": "human-client",
            "absent_reference": "none",
            "unknown_terrain": "unknown",
            "no_native_target": -1,
            "no_government_target": -1,
            "future_tech_name": "Future Tech",
            "unset_tech_name": "Unset",
            "city_growth_turns_never": _FC_INFINITY,
        },
        "scalar_contracts": {
            "entity_ref": _ENTITY_REF.pattern,
            "action_slot": _ACTION_SLOT.pattern,
            "signed_integer": _SIGNED.pattern,
            "unsigned_integer": _UNSIGNED.pattern,
            "i64_min": _I64_MIN,
            "i64_max": _I64_MAX,
            "u64_max": _U64_MAX,
            "i32_max": _I32_MAX,
            "max_rows": MAX_NATIVE_ROWS,
            "max_native_state_scope_rows": MAX_NATIVE_STATE_SCOPE_ROWS,
            "max_row_bytes": MAX_NATIVE_ROW_BYTES,
            "max_page_items": MAX_PAGE_ITEMS,
            "max_scoped_actions": MAX_SCOPED_ACTIONS,
            "max_pinned_scope_views": MAX_PINNED_SCOPE_VIEWS,
            "max_relation_scoped_actions": MAX_RELATION_SCOPED_ACTIONS,
            "max_pinned_relation_scope_views": (
                MAX_PINNED_RELATION_SCOPE_VIEWS
            ),
            "max_governments": MAX_GOVERNMENTS,
            "max_multipliers": MAX_MULTIPLIERS,
            "max_multiplier_choices": MAX_MULTIPLIER_CHOICES,
            "max_city_build_choices": MAX_CITY_BUILD_CHOICES,
            "max_city_worklist": MAX_CITY_WORKLIST,
            "max_city_trade_routes": MAX_CITY_TRADE_ROUTES,
            "max_goods_types": MAX_GOODS_TYPES,
            "max_rally_orders": MAX_RALLY_ORDERS,
            "max_unit_route_waypoints": MAX_UNIT_ROUTE_WAYPOINTS,
            "unit_route_waypoint_rules": [
                "first-waypoint-differs-from-source",
                "goto-final-waypoint-differs-from-source",
                "consecutive-waypoints-differ",
            ],
            "max_infrastructure_choices": MAX_INFRASTRUCTURE_CHOICES,
            "max_chat_message_bytes": MAX_CHAT_MESSAGE_BYTES,
            "chat_forbidden_codepoint_ranges": [
                [lower, upper]
                for lower, upper in _CHAT_FORBIDDEN_CODEPOINT_RANGES
            ],
            "chat_native_argument_grammar": (
                "channel=<global|allied|private>;"
                "recipient=<none|p:id:incarnation>;message=<percent-encoded>"
            ),
            "chat_message_edge_policy": "no-leading-or-trailing-U+0020",
            "max_vote_history": MAX_VOTE_HISTORY,
            "governor_minimum_surplus_min": -100,
            "governor_minimum_surplus_max": 100,
            "governor_weight_min": 0,
            "governor_weight_max": 25,
            "governor_celebration_weight_min": 0,
            "governor_celebration_weight_max": 50,
            "native_state_scope_sections": sorted(
                _NATIVE_STATE_SCOPE_SECTIONS
            ),
        },
        "private_frame_contracts": list(_PRIVATE_FRAME_CONTRACTS.items()),
        "research_choices_digest": {
            "algorithm": "FNV-1a-64",
            "offset_basis": 14695981039346656037,
            "prime": 1099511628211,
            "record_order": "ascending-native-id",
            "record_bytes": [
                "native-id-u32-be", "name-utf8-length-u32-be", "name-utf8",
                "state-ascii-length-u8", "state-ascii", "can-target-u8",
                "can-goal-u8",
            ],
            "text_format": "fnv1a64-%016x",
        },
        "treaty_clauses_digest": {
            "algorithm": "FNV-1a-64",
            "offset_basis": 14695981039346656037,
            "prime": 1099511628211,
            "record_order": "giver-number-clause-type-value",
            "record_bytes": "ascii giver:type:value;",
            "text_format": "fnv1a64-%016x",
        },
    }
    encoded = json.dumps(
        canonical,
        ensure_ascii=True,
        allow_nan=False,
        sort_keys=True,
        separators=(",", ":"),
    ).encode("ascii")
    return f"sha256-{hashlib.sha256(encoded).hexdigest()}"


NATIVE_OBSERVATION_ACTION_SCHEMA_ID = _derive_native_schema_id()


@dataclass(frozen=True)
class _ActionBinding:
    slot: str
    native_revision: int
    argument_contract: str
    public_kind: str
    operation: str
    turn: int
    phase: int
    max_rate: int
    argument_max: int = 0
    argument_min: int = 0
    argument_step: int = 1
    argument_excluded: int | None = None
    actor_ref: str | None = None
    vote_id: str | None = None
    vote_choices: tuple[str, ...] = ()
    server_setting_type: str = "none"
    server_setting_min: int = 0
    server_setting_max: int = 0
    server_setting_current: int = -1
    server_setting_value: int = -1
    server_setting_name: str = ""
    counterpart_ref: str | None = None
    infrastructure_choices: tuple[tuple[str, int], ...] = ()
    scoped: bool = False
    relation_scoped: bool = False
    target_scoped: bool = False


@dataclass(frozen=True)
class V2ActionResolution:
    """Immutable server-private execution data for one current action."""

    native_slot: str
    native_revision: int
    native_arguments: str
    public_kind: str
    operation: str
    turn: int
    phase: int
    native_actor_ref: str | None = None
    native_counterpart_ref: str | None = None
    scoped: bool = False
    relation_scoped: bool = False


@dataclass(frozen=True)
class V2StateScopeRequest:
    """Private coordinates for one bounded native state catalog."""

    section: str
    selector: str
    native_revision: int
    limit: int
    actor_id: str | None = None
    relation_id: str | None = None
    center_id: str | None = None
    radius: int | None = None
    offset: int = 0
    native_view_id: str | None = None
    total_count: int | None = None


@dataclass(frozen=True)
class V2ActorScopeRequest:
    """Private actor scope coordinates retained behind opaque HTTP IDs."""

    actor_id: str
    actor_kind: str
    native_actor_ref: str
    native_revision: int
    limit: int
    offset: int = 0
    native_view_id: str | None = None
    total_count: int | None = None
    seen_slots: tuple[str, ...] = ()
    seen_capabilities: tuple[str, ...] = ()
    pending_scope_bindings: tuple[
        tuple[str, _ActionBinding], ...
    ] = ()


@dataclass(frozen=True)
class V2RelationScopeRequest:
    """Private coordinates for one owned-player/counterpart relation."""

    actor_id: str
    native_actor_ref: str
    relation_id: str
    native_counterpart_ref: str
    native_revision: int
    limit: int
    offset: int = 0
    native_view_id: str | None = None
    total_count: int | None = None
    seen_slots: tuple[str, ...] = ()
    seen_capabilities: tuple[str, ...] = ()
    pending_scope_bindings: tuple[
        tuple[str, _ActionBinding], ...
    ] = ()


@dataclass(frozen=True)
class V2TargetActionRequest:
    """Private coordinates for one current owned actor and known tile."""

    actor_id: str
    actor_kind: str
    native_actor_ref: str
    target_id: str
    native_target_tile: int
    native_revision: int
    limit: int
    action_decision: bool = False


@dataclass(frozen=True)
class _ActorBinding:
    kind: str
    native_ref: str


@dataclass(frozen=True)
class _RelationBinding:
    native_counterpart_ref: str
    counterpart_player_id: str


@dataclass(frozen=True)
class _ActionDecisionBinding:
    actor_id: str
    native_target_tile: int


@dataclass(frozen=True)
class _ProjectedSnapshot:
    native_revision: int
    row_digest: str
    state_revision: Mapping[str, Any]
    sections: Mapping[str, tuple[Mapping[str, Any], ...]]
    legal_actions: tuple[Mapping[str, Any], ...]
    action_bindings: Mapping[str, _ActionBinding]
    actor_bindings: Mapping[str, _ActorBinding]
    relation_bindings: Mapping[str, _RelationBinding]
    tile_bindings: Mapping[str, int]
    action_decision_bindings: Mapping[str, _ActionDecisionBinding]
    parsed: _ParsedObservation
    canonical_bytes: int


@dataclass(frozen=True)
class _Cursor:
    endpoint: str
    section: str
    native_revision: int
    next_offset: int
    limit: int
    actor_id: str | None
    center_id: str | None
    radius: int | None
    expires_at: float
    expires_at_wall: float
    in_flight: bool = False
    response: Mapping[str, Any] | None = None


@dataclass(frozen=True)
class _StateScopeCursor:
    endpoint: str
    section: str
    selector: str
    native_revision: int
    next_offset: int
    limit: int
    actor_id: str | None
    center_id: str | None
    radius: int | None
    native_view_id: str
    total_count: int
    expires_at: float
    expires_at_wall: float
    in_flight: bool = False
    response: Mapping[str, Any] | None = None


@dataclass(frozen=True)
class _ActorScopeCursor:
    endpoint: str
    native_revision: int
    actor_id: str
    actor_kind: str
    native_actor_ref: str
    native_view_id: str
    total_count: int
    next_offset: int
    limit: int
    seen_slots: tuple[str, ...]
    seen_capabilities: tuple[str, ...]
    pending_scope_bindings: tuple[tuple[str, _ActionBinding], ...]
    expires_at: float
    expires_at_wall: float
    in_flight: bool = False
    response: Mapping[str, Any] | None = None


@dataclass(frozen=True)
class _RelationScopeCursor:
    endpoint: str
    native_revision: int
    actor_id: str
    native_actor_ref: str
    relation_id: str
    native_counterpart_ref: str
    native_view_id: str
    total_count: int
    next_offset: int
    limit: int
    seen_slots: tuple[str, ...]
    seen_capabilities: tuple[str, ...]
    pending_scope_bindings: tuple[tuple[str, _ActionBinding], ...]
    expires_at: float
    expires_at_wall: float
    in_flight: bool = False
    response: Mapping[str, Any] | None = None


@dataclass(frozen=True)
class _RetiredCursor:
    endpoint: str
    code: str
    details: Mapping[str, Any]
    forget_at: float


@dataclass
class _PageChain:
    """One atomically admitted, snapshot-independent public traversal."""

    nonce: bytes
    endpoint: str
    section: str
    scope: Mapping[str, Any] | None
    catalog_id: str | None
    state_revision: Mapping[str, Any]
    values: tuple[Mapping[str, Any], ...]
    ranges: tuple[tuple[int, int], ...]
    tokens: tuple[str, ...]
    restart: Mapping[str, Any]
    charge_bytes: int
    deadlines: dict[int, float]
    expiry_walls: dict[int, float]
    responses: dict[int, Mapping[str, Any]]
    pending_bindings: tuple[tuple[str, _ActionBinding], ...]
    bindings_published: bool
    exposed_through: int
    frontier: int | None


@dataclass(frozen=True)
class _RetiredPageChain:
    endpoint: str
    exposed_through: int
    restart: Mapping[str, Any]
    forget_at: float
    code: str = "cursor_expired"


@dataclass(frozen=True)
class _ParsedObservation:
    meta: Mapping[str, Any]
    pregame: Mapping[str, Any] | None
    votes: tuple[Mapping[str, Any], ...]
    player: Mapping[str, Any] | None
    governance: Mapping[str, Any] | None
    governments: tuple[Mapping[str, Any], ...]
    multipliers: tuple[Mapping[str, Any], ...]
    spaceship: Mapping[str, Any] | None
    spaceship_structurals: tuple[Mapping[str, Any], ...]
    research: Mapping[str, Any] | None
    research_techs: tuple[Mapping[str, Any], ...]
    research_graph: tuple[Mapping[str, Any], ...]
    research_edges: tuple[Mapping[str, Any], ...]
    research_unlocks: tuple[Mapping[str, Any], ...]
    diplomacy: tuple[Mapping[str, Any], ...]
    diplomacy_intel: tuple[Mapping[str, Any], ...]
    diplomacy_clauses: tuple[Mapping[str, Any], ...]
    tiles: tuple[Mapping[str, Any], ...]
    infrastructure_extras: tuple[Mapping[str, Any], ...]
    cities: tuple[Mapping[str, Any], ...]
    city_sites: tuple[Mapping[str, Any], ...]
    city_tiles: tuple[Mapping[str, Any], ...]
    city_worker_tasks: tuple[Mapping[str, Any], ...]
    city_specialists: tuple[Mapping[str, Any], ...]
    city_worklists: tuple[Mapping[str, Any], ...]
    city_build_choices: tuple[Mapping[str, Any], ...]
    city_improvements: tuple[Mapping[str, Any], ...]
    city_rallies: tuple[Mapping[str, Any], ...]
    city_governors: tuple[Mapping[str, Any], ...]
    units: tuple[Mapping[str, Any], ...]
    unit_routes: tuple[Mapping[str, Any], ...]
    tombstones: tuple[Mapping[str, Any], ...]
    chats: tuple[Mapping[str, Any], ...]
    actions: tuple[Mapping[str, Any], ...]


def _fail() -> None:
    raise _ObservationError()


def _integer(raw: str, *, unsigned: bool = False, maximum: int | None = None) -> int:
    pattern = _UNSIGNED if unsigned else _SIGNED
    if pattern.fullmatch(raw) is None:
        _fail()
    try:
        value = int(raw, 10)
    except ValueError:
        _fail()
    lower = 0 if unsigned else _I64_MIN
    upper = _U64_MAX if unsigned else _I64_MAX
    if value < lower or value > upper or (maximum is not None and value > maximum):
        _fail()
    return value


def _boolean(raw: str) -> bool:
    if raw == "0":
        return False
    if raw == "1":
        return True
    _fail()


def _i32(raw: str, *, unsigned: bool = False) -> int:
    value = _integer(raw, unsigned=unsigned, maximum=_I32_MAX)
    if not unsigned and value < -(1 << 31):
        _fail()
    return value


def _city_yields(raw: Mapping[str, str]) -> dict[str, int]:
    return {name: _i32(raw[name]) for name in _CITY_YIELD_FIELDS}


def _city_citizen_metrics_match(
    city: Mapping[str, Any],
    tiles: Sequence[Mapping[str, Any]],
    specialists: Sequence[Mapping[str, Any]],
) -> bool:
    counts = city["citizen_counts"]
    if (
        counts["workers"]
        != sum(item["worked"] and not item["free_worked"] for item in tiles)
        or counts["specialists"]
        != sum(
            item["count"] for item in specialists
            if item["counts_toward_population"]
        )
    ):
        return False
    for output in _CITY_OUTPUTS:
        child_field = "shields" if output == "shield" else output
        citizen_base = sum(
            item["yields"][child_field]
            for item in tiles if item["worked"]
        ) + sum(
            item["count"] * item["yields"][child_field]
            for item in specialists
        )
        if citizen_base != city["outputs"][output]["citizen_base"]:
            return False
    return True


def _percent_encode(raw: bytes) -> str:
    encoded: list[str] = []
    for value in raw:
        if value in _UNRESERVED:
            encoded.append(chr(value))
        else:
            encoded.append(f"%{value:02X}")
    return "".join(encoded)


def _text(
    encoded: str, *, nonempty: bool = True, allow_controls: bool = False,
) -> str:
    # Almost every name, label and identifier a projection decodes carries no
    # escape at all, and for those the loop below is provably the identity: an
    # unreserved character decodes to itself, re-encodes to itself (so the
    # canonicity check passes), is ASCII, is never NUL, and is never in a
    # control category.  Recognizing that in one C-level match skips a Python
    # iteration per character plus a full re-encode of the result.
    if _UNRESERVED_TEXT.fullmatch(encoded) is not None:
        if nonempty and not encoded:
            _fail()
        return encoded
    if not encoded.isascii():
        _fail()
    decoded = bytearray()
    index = 0
    while index < len(encoded):
        char = encoded[index]
        if char == "%":
            if index + 2 >= len(encoded):
                _fail()
            pair = encoded[index + 1:index + 3]
            if re.fullmatch(r"[0-9A-F]{2}", pair) is None:
                _fail()
            value = int(pair, 16)
            index += 3
        else:
            value = ord(char)
            if value not in _UNRESERVED:
                _fail()
            index += 1
        if value == 0:
            _fail()
        decoded.append(value)
    if _percent_encode(bytes(decoded)) != encoded:
        _fail()
    try:
        value = bytes(decoded).decode("utf-8", "strict")
    except UnicodeDecodeError:
        _fail()
    if nonempty and not value:
        _fail()
    if not allow_controls and any(
        unicodedata.category(char).startswith("C") for char in value
    ):
        _fail()
    return value


def _research_choices_digest(techs: Sequence[Mapping[str, Any]]) -> str:
    digest = 14695981039346656037

    def update(data: bytes) -> None:
        nonlocal digest
        for value in data:
            digest ^= value
            digest = (digest * 1099511628211) & _U64_MAX

    for tech in sorted(techs, key=lambda item: item["native_id"]):
        name = tech["name"].encode("utf-8", "strict")
        state = tech["state"].encode("ascii", "strict")
        if len(name) > 0xFFFFFFFF or len(state) > 0xFF:
            _fail()
        update(tech["native_id"].to_bytes(4, "big"))
        update(len(name).to_bytes(4, "big"))
        update(name)
        update(bytes((len(state),)))
        update(state)
        update(bytes((int(tech["can_target"]), int(tech["can_goal"]))))
    return f"fnv1a64-{digest:016x}"


def _known_techs_digest(native_ids: Sequence[int]) -> str:
    digest = 14695981039346656037
    canonical = (
        ",".join(str(native_id) for native_id in native_ids) or "-"
    ).encode("ascii", "strict")
    for value in canonical:
        digest ^= value
        digest = (digest * 1099511628211) & _U64_MAX
    return f"fnv1a64-{digest:016x}"


def _diplomacy_clauses_digest(
    clauses: Sequence[Mapping[str, Any]],
) -> str:
    digest = 14695981039346656037
    def canonical_key(clause: Mapping[str, Any]) -> tuple[int, int, int]:
        _, giver_number, _ = _entity_ref(clause["giver_ref"], "p")
        return giver_number, clause["native_type"], clause["native_value"]

    for clause in sorted(clauses, key=canonical_key):
        _, giver_number, _ = _entity_ref(clause["giver_ref"], "p")
        canonical = (
            f"{giver_number}:{clause['native_type']}:{clause['native_value']};"
        ).encode("ascii", "strict")
        for value in canonical:
            digest ^= value
            digest = (digest * 1099511628211) & _U64_MAX
    return f"fnv1a64-{digest:016x}"


def _entity_ref(raw: str, expected_kind: str | None = None) -> tuple[str, int, int]:
    match = _ENTITY_REF.fullmatch(raw)
    if match is None or (
        expected_kind is not None and match.group("kind") != expected_kind
    ):
        _fail()
    number = _integer(match.group("number"), unsigned=True, maximum=_I32_MAX)
    incarnation = _integer(match.group("incarnation"), unsigned=True)
    if incarnation == 0:
        _fail()
    return match.group("kind"), number, incarnation


# Every projected value that is already immutable ends the recursion below.
# Freezing a snapshot visits far more leaves than containers -- a parsed row is
# a couple of dozen scalars -- so recognizing a leaf by its exact class in one
# lookup, before asking whether it is any kind of container, is what keeps the
# per-revision projection off the critical path.
_ATOMIC_TYPES = frozenset({str, int, float, bool, type(None)})


def _freeze(value: Any) -> Any:
    if value.__class__ in _ATOMIC_TYPES:
        return value
    if isinstance(value, dict):
        return MappingProxyType({key: _freeze(item) for key, item in value.items()})
    if isinstance(value, list):
        return tuple(_freeze(item) for item in value)
    if isinstance(value, tuple):
        return tuple(_freeze(item) for item in value)
    return value


def _thaw(value: Any) -> Any:
    if value.__class__ in _ATOMIC_TYPES:
        return value
    if isinstance(value, Mapping):
        return {key: _thaw(item) for key, item in value.items()}
    if isinstance(value, tuple):
        return [_thaw(item) for item in value]
    return value


class V2SeatControl:
    """Validate and project observations for one exact sidecar generation."""

    def __init__(self, game_id: str, agent_id: str, generation: int) -> None:
        if (
            not isinstance(game_id, str)
            or _OPAQUE_OWNER.fullmatch(game_id) is None
            or not isinstance(agent_id, str)
            or _OPAQUE_OWNER.fullmatch(agent_id) is None
            or isinstance(generation, bool)
            or not isinstance(generation, int)
            or generation < 1
            or generation > _U64_MAX
        ):
            raise V2ControlError("invalid_request")
        self.game_id = game_id
        self.agent_id = agent_id
        self.generation = generation
        self._secret = bytearray(secrets.token_bytes(32))
        self._lock = threading.RLock()
        self._closed = False
        self._snapshots: OrderedDict[int, _ProjectedSnapshot] = OrderedDict()
        self._highest_native_revision = 0
        self._projected_bytes = 0
        # Native-state anomalies this seat observed and kept playing through,
        # by kind.  These are faults the native client itself treats as
        # recoverable -- rejecting them would brick the seat forever -- so
        # they are counted and attributed here instead of being either silent
        # or fatal.
        self.native_anomalies: dict[str, int] = {}
        self._cursors: OrderedDict[
            str,
            _Cursor | _StateScopeCursor | _ActorScopeCursor
            | _RelationScopeCursor,
        ] = OrderedDict()
        self._retired_cursors: OrderedDict[str, _RetiredCursor] = OrderedDict()
        # cursor -> the deadline by which the caller that reserved it must
        # commit or abort.  A reservation is released by its own completion,
        # so without this a caller that never completes (a killed request
        # thread, a `BaseException` past the abort handler) would hold the
        # slot until the seat generation ended.  Enough of those and the
        # registry is permanently full: legal-action enumeration stops for the
        # rest of the game, which is the wedge shape this bounds.
        self._cursor_leases: dict[str, float] = {}
        self._page_chains: OrderedDict[bytes, _PageChain] = OrderedDict()
        self._retired_page_chains: OrderedDict[
            bytes, _RetiredPageChain
        ] = OrderedDict()
        self._page_chain_slots = 0
        self._page_chain_bytes = 0
        self._scoped_action_bindings: OrderedDict[
            str, _ActionBinding
        ] = OrderedDict()
        self._scoped_tile_bindings: OrderedDict[
            str, tuple[int, int, int, int]
        ] = (
            OrderedDict()
        )
        self._scoped_tile_metadata: OrderedDict[
            tuple[int, int], Mapping[str, Any]
        ] = OrderedDict()
        self._actor_tile_overlays: OrderedDict[
            tuple[int, str], tuple[Mapping[str, Any], ...]
        ] = OrderedDict()
        self._city_state_overlays: dict[
            tuple[int, str, str], tuple[Mapping[str, Any], ...]
        ] = {}
        self._pregame_state_overlays: dict[
            tuple[int, str], tuple[Mapping[str, Any], ...]
        ] = {}
        self._chat_recipient_overlays: dict[
            int, tuple[Mapping[str, Any], ...]
        ] = {}
        self._relation_state_overlays: dict[
            tuple[int, str], tuple[Mapping[str, Any], ...]
        ] = {}
        self._relation_clause_overlays: dict[
            tuple[int, str], tuple[Mapping[str, Any], ...]
        ] = {}
        self._relation_overlay_charges: OrderedDict[
            tuple[int, str], int
        ] = OrderedDict()
        self._relation_overlay_bytes = 0
        # Standalone projection keeps the complete native catalog; the owner
        # supervisor installs the all-seats policy before serving any page or
        # resolving any batch.
        self._pregame_ready_allowed = True

    def set_pregame_ready_allowed(self, allowed: bool) -> None:
        """Apply the supervisor's all-seats barrier to the public catalog."""
        if type(allowed) is not bool:
            raise V2ControlError("invalid_request")
        with self._lock:
            self._require_open()
            if self._pregame_ready_allowed == allowed:
                return
            self._pregame_ready_allowed = allowed
            # The policy bit participates in pregame state identity. Retire
            # every capability/cursor/overlay so a single native revision can
            # never describe both blocked and executable readiness catalogs.
            self._snapshots.clear()
            self._cursors.clear()
            self._cursor_leases.clear()
            self._retired_cursors.clear()
            self._page_chains.clear()
            self._retired_page_chains.clear()
            self._page_chain_slots = 0
            self._page_chain_bytes = 0
            self._scoped_action_bindings.clear()
            self._scoped_tile_bindings.clear()
            self._scoped_tile_metadata.clear()
            self._actor_tile_overlays.clear()
            self._city_state_overlays.clear()
            self._pregame_state_overlays.clear()
            self._chat_recipient_overlays.clear()
            self._relation_state_overlays.clear()
            self._relation_clause_overlays.clear()
            self._relation_overlay_charges.clear()
            self._relation_overlay_bytes = 0
            self._projected_bytes = 0
            self._highest_native_revision = 0

    @property
    def has_snapshot(self) -> bool:
        """Whether this exact, still-open seat generation cached state."""
        with self._lock:
            return not self._closed and bool(self._snapshots)

    def close(self) -> None:
        """Irreversibly discard every generation-scoped secret and handle."""
        with self._lock:
            if self._closed:
                return
            for index in range(len(self._secret)):
                self._secret[index] = 0
            self._snapshots.clear()
            self._cursors.clear()
            self._cursor_leases.clear()
            self._retired_cursors.clear()
            self._page_chains.clear()
            self._retired_page_chains.clear()
            self._page_chain_slots = 0
            self._page_chain_bytes = 0
            self._scoped_action_bindings.clear()
            self._scoped_tile_bindings.clear()
            self._scoped_tile_metadata.clear()
            self._actor_tile_overlays.clear()
            self._city_state_overlays.clear()
            self._pregame_state_overlays.clear()
            self._chat_recipient_overlays.clear()
            self._relation_state_overlays.clear()
            self._relation_clause_overlays.clear()
            self._relation_overlay_charges.clear()
            self._relation_overlay_bytes = 0
            self._projected_bytes = 0
            self._highest_native_revision = 0
            self._closed = True

    def _require_open(self) -> None:
        if self._closed:
            raise V2ControlError("sidecar_unavailable")

    def prepare_observation_scopes(
        self, observation: Mapping[str, Any],
    ) -> tuple[V2StateScopeRequest, ...]:
        """Describe the entity catalogs required to complete compact OBS."""
        with self._lock:
            self._require_open()
            try:
                if not isinstance(observation, Mapping) or set(observation) != {
                    "generation", "native_revision", "rows",
                }:
                    _fail()
                generation = observation["generation"]
                revision = observation["native_revision"]
                rows = observation["rows"]
                if (
                    isinstance(generation, bool)
                    or not isinstance(generation, int)
                    or generation != self.generation
                    or isinstance(revision, bool)
                    or not isinstance(revision, int)
                    or not 1 <= revision <= _U64_MAX
                    or not isinstance(rows, tuple)
                    or not 1 <= len(rows) <= MAX_NATIVE_ROWS
                ):
                    _fail()
                forbidden = {
                    "city", "city_site", "unit", "tombstone",
                    "diplomacy_clause", "unit_route",
                }
                for row in rows:
                    if not isinstance(row, str) or row.split(" ", 1)[0] in forbidden:
                        _fail()
            except _ObservationError as exc:
                raise V2ControlError("internal_error") from exc
            if any(row.startswith("meta state=preparing ") for row in rows):
                return ()
            return tuple(V2StateScopeRequest(
                section=section,
                selector="-",
                native_revision=revision,
                limit=MAX_PAGE_ITEMS,
            ) for section in ("cities", "units", "city_sites"))

    def materialize_observation_catalogs(
        self,
        observation: Mapping[str, Any],
        native_catalogs: Mapping[str, Mapping[str, Any]],
    ) -> Mapping[str, Any]:
        """Validate and merge fully drained entity catalogs into compact OBS."""
        requests = self.prepare_observation_scopes(observation)
        if not isinstance(native_catalogs, Mapping) or set(native_catalogs) != {
            request.section for request in requests
        }:
            raise V2ControlError("internal_error")
        revision = observation["native_revision"]
        merged = list(observation["rows"])
        allowed = {
            "cities": frozenset({"city", "city_rally", "city_worker_task"}),
            "units": frozenset({"unit", "unit_route"}),
            "city_sites": frozenset({"city_site"}),
        }
        try:
            for request in requests:
                catalog = native_catalogs[request.section]
                if not isinstance(catalog, Mapping) or set(catalog) != {
                    "generation", "native_revision", "section", "selector",
                    "view_id", "offset", "count", "total_count",
                    "next_offset", "complete", "overflow", "rows",
                }:
                    _fail()
                rows = catalog["rows"]
                total = catalog["total_count"]
                view_id = catalog["view_id"]
                if (
                    catalog["generation"] != self.generation
                    or catalog["native_revision"] != revision
                    or catalog["section"] != request.section
                    or catalog["selector"] != "-"
                    or not isinstance(view_id, str)
                    or _STATE_SCOPE_VIEW.fullmatch(view_id) is None
                    or int(view_id[1:].split("-", 1)[0]) != revision
                    or isinstance(total, bool) or not isinstance(total, int)
                    or not 0 <= total <= MAX_NATIVE_STATE_SCOPE_ROWS
                    or catalog["offset"] != 0
                    or catalog["count"] != total
                    or catalog["next_offset"] != total
                    or catalog["complete"] is not True
                    or catalog["overflow"] is not False
                    or not isinstance(rows, tuple) or len(rows) != total
                ):
                    _fail()
                for row in rows:
                    kind, _ = self._parse_state_scope_row(
                        row, allowed[request.section],
                    )
                    if kind not in allowed[request.section]:
                        _fail()
                merged.extend(rows)
            if not 1 <= len(merged) <= MAX_BUNDLED_ROWS:
                _fail()
            merged.sort(key=lambda row: row.encode("ascii", "strict"))
            if len(merged) != len(set(merged)):
                _fail()
        except (UnicodeEncodeError, _ObservationError) as exc:
            raise V2ControlError("internal_error") from exc
        return MappingProxyType({
            "generation": self.generation,
            "native_revision": revision,
            "rows": tuple(merged),
        })

    def state_page(
        self,
        observation: Mapping[str, Any],
        section: str = "overview",
        limit: int = MAX_PAGE_ITEMS,
        *,
        actor_id: str | None = None,
        relation_id: str | None = None,
        center_id: str | None = None,
        radius: int | None = None,
    ) -> dict[str, Any]:
        if section not in _STATE_SECTIONS:
            raise V2ControlError("invalid_request")
        clean_limit = self._limit(limit)
        with self._lock:
            self._require_open()
            snapshot = self._snapshot(observation)
            self._state_section_values(
                snapshot, section, actor_id, relation_id, center_id, radius,
            )
            return self._page(
                snapshot, "state", section, clean_limit, 0,
                actor_id=actor_id, relation_id=relation_id,
                center_id=center_id, radius=radius,
            )

    def decision_load(
        self, observation: Mapping[str, Any],
    ) -> dict[str, Any]:
        """Count every actor of this seat that still owes the agent a decision.

        This exists so that a phase can be ended for an agent that has nothing
        to decide, and it therefore has to be answered from the whole
        projection rather than from anything an agent reads.  Every agent-
        facing view of this question is a page: the briefing counts idle units
        over the sixteen rows it printed and says so, and the decision rows cap
        at forty.  A phase ended because one page looked quiet is a phase
        ended with actors still waiting on the second page, which is exactly
        the autopilot this must never become.  The sections walked below are
        the fully drained projection -- every own unit, every city.

        ``pending`` is the sum, and zero is the only value that means
        anything: a caller may end a phase on zero and must never act on any
        other number.  Each contributing count is reported separately so a
        refusal to auto-end can name what the seat still had to decide.
        """
        with self._lock:
            self._require_open()
            snapshot = self._snapshot(observation)
            return self._decision_load(snapshot)

    @staticmethod
    def _decision_load(snapshot: _ProjectedSnapshot) -> dict[str, Any]:
        sections = snapshot.sections
        overview = next(iter(sections.get("overview", ())), None)
        units = sections.get("units", ())
        idle_units = 0
        action_decisions = 0
        for unit in units:
            if unit.get("scope") != "own":
                continue
            if unit.get("action_decision", {}).get("pending"):
                # The native client is holding a popup question open against
                # this unit.  Nothing is more literally a pending decision.
                action_decisions += 1
            automation = unit.get("automation") or {}
            # A unit awaits orders only if it could still carry one out.  A
            # unit that spent its moves this phase reports the same idle
            # activity and empty order queue as one that has not been touched,
            # and counting those would mean a seat that played its whole turn
            # still looks undecided -- the check would never pass and the
            # phase would always burn to the deadline.  ``moves <= 0`` is the
            # same exhaustion test the action rules already apply.
            if (
                unit.get("activity", {}).get("name") == "idle"
                and not automation.get("has_orders")
                and automation.get("controller") == "none"
                and unit.get("moves", 0) > 0
            ):
                idle_units += 1
        # A city whose shield box is full is choosing what to build next, and
        # that choice is the agent's.  Freeciv would pick for it; that is the
        # autopilot this refuses to be.
        completed_production = sum(
            1 for city in sections.get("cities", ())
            if city.get("production", {}).get("shield_stock", 0)
            >= city.get("production", {}).get("shield_cost", 0)
        )
        # ``v2_research_choice_name`` in protocol_v2.c names A_UNSET "Unset"
        # and an absent research "Unavailable"; the row's name field is never
        # empty, so a falsiness test on it would never fire.
        research = (overview or {}).get("research")
        target = (research or {}).get("target")
        research_unset = int(
            research is None
            or not target
            or str(target) in {"Unset", "Unavailable"}
        )
        # An open treaty meeting this seat has not accepted is a counterpart
        # waiting on an answer.  Ending the phase under it answers for them.
        open_meetings = sum(
            1 for relation in sections.get("diplomacy", ())
            if isinstance(relation.get("meeting"), Mapping)
            and not relation["meeting"].get("self_accepted")
        )
        counts = {
            "idle_units": idle_units,
            "action_decisions": action_decisions,
            "completed_production": completed_production,
            "research_unset": research_unset,
            "open_meetings": open_meetings,
        }
        return {
            # ``can_end_turn()`` in the native client.  The seat cannot hand
            # the phase back without it, so a caller must not try.
            "phase_ready": bool((overview or {}).get("phase_ready")),
            "turn": (overview or {}).get("turn"),
            "phase": (overview or {}).get("phase"),
            "own_units": sum(
                1 for unit in units if unit.get("scope") == "own"
            ),
            "cities": len(sections.get("cities", ())),
            **counts,
            "pending": sum(counts.values()),
        }

    def prepare_state_scope(
        self,
        observation: Mapping[str, Any],
        section: Any,
        limit: int = MAX_PAGE_ITEMS,
        *,
        actor_id: Any = None,
        relation_id: Any = None,
        center_id: Any = None,
        radius: Any = None,
    ) -> V2StateScopeRequest:
        """Resolve an opaque state query to one private native selector."""
        clean_limit = self._limit(limit)
        if section not in _NATIVE_STATE_SCOPE_SECTIONS:
            raise V2ControlError("invalid_request")
        with self._lock:
            self._require_open()
            snapshot = self._snapshot(observation)
            if snapshot.native_revision != max(self._snapshots):
                raise V2ControlError("stale_revision")
            if section == "chat_recipients":
                if (
                    snapshot.parsed.meta["state"] not in {
                        "preparing", "running",
                    }
                    or actor_id is not None or relation_id is not None
                    or center_id is not None or radius is not None
                ):
                    raise V2ControlError("invalid_request")
                return V2StateScopeRequest(
                    section=section,
                    selector="-",
                    native_revision=snapshot.native_revision,
                    limit=clean_limit,
                )
            if section in {
                "pregame_nations", "pregame_styles", "pregame_teams",
            }:
                if (
                    snapshot.parsed.meta["state"] != "preparing"
                    or actor_id is not None or relation_id is not None
                    or center_id is not None or radius is not None
                ):
                    raise V2ControlError("invalid_request")
                return V2StateScopeRequest(
                    section=section,
                    selector="-",
                    native_revision=snapshot.native_revision,
                    limit=clean_limit,
                )
            if section == "diplomacy_clauses":
                binding = (
                    snapshot.relation_bindings.get(relation_id)
                    if isinstance(relation_id, str) else None
                )
                if (
                    binding is None or actor_id is not None
                    or center_id is not None or radius is not None
                ):
                    raise V2ControlError("invalid_request")
                return V2StateScopeRequest(
                    section=section,
                    selector=binding.native_counterpart_ref,
                    native_revision=snapshot.native_revision,
                    limit=clean_limit,
                    relation_id=relation_id,
                )
            if section == "target_tiles":
                binding = (
                    snapshot.actor_bindings.get(actor_id)
                    if isinstance(actor_id, str) else None
                )
                if (
                    binding is None or binding.kind != "unit"
                    or relation_id is not None
                    or center_id is not None or radius is not None
                ):
                    raise V2ControlError("invalid_request")
                return V2StateScopeRequest(
                    section=section,
                    selector=binding.native_ref,
                    native_revision=snapshot.native_revision,
                    limit=clean_limit,
                    actor_id=actor_id,
                )
            if section == "unit_route":
                binding = (
                    snapshot.actor_bindings.get(actor_id)
                    if isinstance(actor_id, str) else None
                )
                unit = next((
                    item for item in snapshot.sections["units"]
                    if item["id"] == actor_id
                ), None)
                if (
                    binding is None or binding.kind != "unit"
                    or unit is None or unit.get("route") is None
                    or not unit["route"]["path_available"]
                    or relation_id is not None
                    or center_id is not None or radius is not None
                ):
                    raise V2ControlError("invalid_request")
                return V2StateScopeRequest(
                    section=section,
                    selector=binding.native_ref,
                    native_revision=snapshot.native_revision,
                    limit=clean_limit,
                    actor_id=actor_id,
                )
            if section in _CITY_STATE_SECTIONS:
                binding = (
                    snapshot.actor_bindings.get(actor_id)
                    if isinstance(actor_id, str) else None
                )
                if (
                    binding is None or binding.kind != "city"
                    or relation_id is not None
                    or center_id is not None or radius is not None
                ):
                    raise V2ControlError("invalid_request")
                selector = binding.native_ref
                return V2StateScopeRequest(
                    section=section,
                    selector=selector,
                    native_revision=snapshot.native_revision,
                    limit=clean_limit,
                    actor_id=actor_id,
                )
            if section in {"known_tiles", "map_tiles"}:
                if (
                    actor_id is not None or relation_id is not None
                    or center_id is not None or radius is not None
                ):
                    raise V2ControlError("invalid_request")
                return V2StateScopeRequest(
                    section=section,
                    selector="-",
                    native_revision=snapshot.native_revision,
                    limit=clean_limit,
                )
            if (
                actor_id is not None or relation_id is not None
                or not isinstance(center_id, str)
                or isinstance(radius, bool) or not isinstance(radius, int)
                or not 0 <= radius <= MAX_TILE_WINDOW_RADIUS
            ):
                raise V2ControlError("invalid_request")
            native_tile = snapshot.tile_bindings.get(center_id)
            if native_tile is None:
                scoped = self._scoped_tile_bindings.get(center_id)
                if scoped is not None and scoped[0] == snapshot.native_revision:
                    native_tile = scoped[1]
            if native_tile is None:
                raise V2ControlError("invalid_request")
            return V2StateScopeRequest(
                section=section,
                selector=f"t{native_tile}-r{radius}",
                native_revision=snapshot.native_revision,
                limit=clean_limit,
                center_id=center_id,
                radius=radius,
            )

    def prepare_investigation_scope(
        self,
        observation: Mapping[str, Any],
        selector: Any,
    ) -> V2StateScopeRequest:
        """Bind one native-only investigation token to its exact revision."""
        if (
            not isinstance(selector, str)
            or _INVESTIGATION_SELECTOR.fullmatch(selector) is None
        ):
            raise V2ControlError("invalid_request")
        with self._lock:
            self._require_open()
            snapshot = self._snapshot(observation)
            if snapshot.native_revision != max(self._snapshots):
                raise V2ControlError("stale_revision")
            return V2StateScopeRequest(
                section="investigation",
                selector=selector,
                native_revision=snapshot.native_revision,
                limit=MAX_PAGE_ITEMS,
            )

    def project_investigation_observation(
        self,
        observation: Mapping[str, Any],
        request: V2StateScopeRequest,
        native_catalog: Mapping[str, Any],
    ) -> dict[str, Any]:
        """Project one consumed CITY_INFO capture without retaining internals."""
        with self._lock:
            self._require_open()
            snapshot = self._snapshot(observation)
            if (
                request.native_revision != snapshot.native_revision
                or request.native_revision != max(self._snapshots)
            ):
                raise V2ControlError("stale_revision")
            try:
                if (
                    request.section != "investigation"
                    or _INVESTIGATION_SELECTOR.fullmatch(request.selector) is None
                    or not isinstance(native_catalog, Mapping)
                    or set(native_catalog) != {
                        "generation", "native_revision", "section", "selector",
                        "view_id", "offset", "count", "total_count",
                        "next_offset", "complete", "overflow", "rows",
                    }
                ):
                    _fail()
                view_id = native_catalog["view_id"]
                total = native_catalog["total_count"]
                rows = native_catalog["rows"]
                if (
                    native_catalog["generation"] != self.generation
                    or native_catalog["native_revision"] != request.native_revision
                    or native_catalog["section"] != "investigation"
                    or native_catalog["selector"] != request.selector
                    or not isinstance(view_id, str)
                    or _STATE_SCOPE_VIEW.fullmatch(view_id) is None
                    or int(view_id[1:].split("-", 1)[0])
                       != request.native_revision
                    or isinstance(total, bool) or not isinstance(total, int)
                    or not 1 <= total <= (
                        1 + MAX_INVESTIGATION_IMPROVEMENTS
                        + len(INVESTIGATION_FEELING_STAGES)
                        + MAX_INVESTIGATION_SPECIALISTS
                    )
                    or native_catalog["offset"] != 0
                    or native_catalog["count"] != total
                    or native_catalog["next_offset"] != total
                    or native_catalog["complete"] is not True
                    or native_catalog["overflow"] is not False
                    or not isinstance(rows, tuple) or len(rows) != total
                    or len(set(rows)) != total
                ):
                    _fail()
                allowed = frozenset({
                    "investigation", "investigation_improvement",
                    "investigation_citizens", "investigation_specialist",
                })
                parsed_pairs = tuple(
                    self._parse_state_scope_row(row, allowed) for row in rows
                )
                summaries = tuple(
                    item for kind, item in parsed_pairs
                    if kind == "investigation"
                )
                improvements = tuple(
                    item for kind, item in parsed_pairs
                    if kind == "investigation_improvement"
                )
                feelings = tuple(
                    item for kind, item in parsed_pairs
                    if kind == "investigation_citizens"
                )
                specialists = tuple(
                    item for kind, item in parsed_pairs
                    if kind == "investigation_specialist"
                )
                if len(summaries) != 1:
                    _fail()
                summary = summaries[0]
                city_ref = summary["city_ref"]
                if (
                    summary["lifecycle"] == 0 or summary["size"] == 0
                    or summary["improvement_count"] != len(improvements)
                    or summary["feeling_count"] != len(feelings)
                    or summary["feeling_count"]
                       != len(INVESTIGATION_FEELING_STAGES)
                    or summary["specialist_count"] != len(specialists)
                    or any(
                        item["city_ref"] != city_ref
                        for _, item in parsed_pairs
                    )
                    or len({item["native_id"] for item in improvements})
                       != len(improvements)
                    or len({item["name"] for item in improvements})
                       != len(improvements)
                    or {item["stage"] for item in feelings}
                       != set(range(len(INVESTIGATION_FEELING_STAGES)))
                    or {item["native_id"] for item in specialists}
                       != set(range(len(specialists)))
                    or len({item["name"] for item in specialists})
                       != len(specialists)
                ):
                    _fail()
                specialist_population = sum(
                    item["count"] for item in specialists
                )
                for item in feelings:
                    if (
                        item["happy"] + item["content"] + item["unhappy"]
                        + item["angry"] + specialist_population
                        != summary["size"]
                    ):
                        _fail()
                matching_sites = tuple(
                    item for item in snapshot.parsed.city_sites
                    if item["ref"] == city_ref
                )
                if (
                    len(matching_sites) != 1
                    or matching_sites[0]["native_tile"]
                       != summary["native_tile"]
                    or matching_sites[0]["name"] != summary["name"]
                    or matching_sites[0]["size"] != summary["size"]
                ):
                    _fail()
            except (UnicodeEncodeError, _ObservationError) as exc:
                raise V2ControlError("internal_error") from exc

            return {
                "id": self._mac(
                    "observation", "city_investigation",
                    request.native_revision, request.selector,
                ),
                "type": "city_investigation",
                "source": "human_client_city_info",
                "freshness": "captured_at_receipt_revision",
                "state_revision": _thaw(snapshot.state_revision),
                "city": {
                    "id": self._entity_id("city", city_ref),
                    "name": summary["name"],
                    "size": summary["size"],
                    "production": {
                        "id": self._production_id(
                            summary["production_kind"],
                            summary["production_native_id"],
                        ),
                        "kind": summary["production_kind"],
                        "name": summary["production_name"],
                    },
                    "shields": {
                        "stock": summary["shield_stock"],
                        "surplus": summary["shield_surplus"],
                    },
                    "improvements": [{
                        "id": self._production_id(
                            "improvement", item["native_id"],
                        ),
                        "name": item["name"],
                    } for item in sorted(
                        improvements, key=lambda item: item["native_id"],
                    )],
                    "citizens": {
                        "feelings": [{
                            "stage": INVESTIGATION_FEELING_STAGES[item["stage"]],
                            "happy": item["happy"],
                            "content": item["content"],
                            "unhappy": item["unhappy"],
                            "angry": item["angry"],
                        } for item in sorted(
                            feelings, key=lambda item: item["stage"],
                        )],
                        "specialists": [{
                            "id": self._specialist_id(item["native_id"]),
                            "name": item["name"],
                            "count": item["count"],
                        } for item in sorted(
                            specialists, key=lambda item: item["native_id"],
                        )],
                    },
                },
            }

    def materialize_state_scope(
        self,
        request: V2StateScopeRequest,
        native_catalog: Mapping[str, Any],
    ) -> dict[str, Any]:
        """Validate and freeze a complete native state catalog before page one."""
        with self._lock:
            self._require_open()
            snapshot = self._snapshots.get(request.native_revision)
            if (
                snapshot is None
                or request.native_revision != max(self._snapshots)
                or request.offset != 0
                or request.native_view_id is not None
            ):
                raise V2ControlError("stale_revision")
            total = (
                native_catalog.get("total_count")
                if isinstance(native_catalog, Mapping) else None
            )
            if (
                not isinstance(total, bool) and isinstance(total, int)
                and total > MAX_NATIVE_STATE_SCOPE_ROWS
            ):
                raise V2ControlError("scope_too_large")
            try:
                items, tile_bindings, parsed_rows = self._validate_state_scope_catalog(
                    snapshot, request, native_catalog,
                )
            except _ObservationError as exc:
                raise V2ControlError("internal_error") from exc
            restart_query: dict[str, Any] = {
                "section": request.section,
                "limit": request.limit,
            }
            if request.actor_id is not None:
                restart_query["actor_id"] = request.actor_id
            if request.relation_id is not None:
                restart_query["relation_id"] = request.relation_id
            if request.center_id is not None:
                restart_query["center_id"] = request.center_id
            if request.radius is not None:
                restart_query["radius"] = request.radius
            if request.relation_id is not None:
                self._publish_state_scope_private(
                    snapshot, request, items, tile_bindings, parsed_rows,
                )
            page = self._start_page_chain(
                snapshot,
                "state",
                request.section,
                request.limit,
                items,
                {"endpoint": "state", "query": restart_query},
            )
            if request.relation_id is None:
                self._publish_state_scope_private(
                    snapshot, request, items, tile_bindings, parsed_rows,
                )
            return page

    def hydrate_state_scope(
        self,
        request: V2StateScopeRequest,
        native_catalog: Mapping[str, Any],
    ) -> None:
        """Validate a support catalog and retain only private actor metadata."""
        with self._lock:
            self._require_open()
            snapshot = self._snapshots.get(request.native_revision)
            if snapshot is None or request.native_revision != max(self._snapshots):
                raise V2ControlError("stale_revision")
            total = (
                native_catalog.get("total_count")
                if isinstance(native_catalog, Mapping) else None
            )
            if (
                not isinstance(total, bool) and isinstance(total, int)
                and total > MAX_NATIVE_STATE_SCOPE_ROWS
            ):
                raise V2ControlError("scope_too_large")
            try:
                items, tile_bindings, parsed_rows = self._validate_state_scope_catalog(
                    snapshot, request, native_catalog,
                )
            except _ObservationError as exc:
                raise V2ControlError("internal_error") from exc
            self._publish_state_scope_private(
                snapshot, request, items, tile_bindings, parsed_rows,
            )

    def prepare_city_support_scopes(
        self,
        observation: Mapping[str, Any],
        actor_id: Any,
    ) -> tuple[V2StateScopeRequest, ...]:
        """Return the complete native support set for a city action catalog."""
        with self._lock:
            self._require_open()
            snapshot = self._snapshot(observation)
            binding = (
                snapshot.actor_bindings.get(actor_id)
                if isinstance(actor_id, str) else None
            )
            city = next((
                item for item in snapshot.parsed.cities
                if binding is not None and item["ref"] == binding.native_ref
            ), None)
            if binding is None or binding.kind != "city" or city is None:
                raise V2ControlError("invalid_request")
            requests = [V2StateScopeRequest(
                section=section,
                selector=binding.native_ref,
                native_revision=snapshot.native_revision,
                limit=MAX_PAGE_ITEMS,
                actor_id=actor_id,
            ) for section in (
                "city_worklist", "city_build_choices", "city_citizens",
                "city_improvements", "city_governor",
            )]
            center_id = self._tile_id(city["native_tile"])
            requests.append(V2StateScopeRequest(
                section="tile_window",
                selector=f"t{city['native_tile']}-r{MAX_TILE_WINDOW_RADIUS}",
                native_revision=snapshot.native_revision,
                limit=MAX_PAGE_ITEMS,
                center_id=center_id,
                radius=MAX_TILE_WINDOW_RADIUS,
            ))
            return tuple(requests)

    def prepare_unit_support_scopes(
        self,
        observation: Mapping[str, Any],
        actor_id: Any,
    ) -> tuple[V2StateScopeRequest, ...]:
        """Return the exact tile closure referenced by a unit catalog."""
        with self._lock:
            self._require_open()
            snapshot = self._snapshot(observation)
            binding = (
                snapshot.actor_bindings.get(actor_id)
                if isinstance(actor_id, str) else None
            )
            unit = next((
                item for item in snapshot.parsed.units
                if binding is not None and item["scope"] == "own"
                and item["ref"] == binding.native_ref
            ), None)
            if binding is None or binding.kind != "unit" or unit is None:
                raise V2ControlError("invalid_request")
            return (V2StateScopeRequest(
                section="target_tiles",
                selector=binding.native_ref,
                native_revision=snapshot.native_revision,
                limit=MAX_PAGE_ITEMS,
                actor_id=actor_id,
            ),)

    def _publish_state_scope_private(
        self,
        snapshot: _ProjectedSnapshot,
        request: V2StateScopeRequest,
        public_items: Sequence[Mapping[str, Any]],
        tile_bindings: Sequence[tuple[str, int, int, int]],
        parsed_rows: Sequence[Mapping[str, Any]],
    ) -> None:
        for public_id, native_tile, x, y in tile_bindings:
            self._scoped_tile_bindings[public_id] = (
                snapshot.native_revision, native_tile, x, y,
            )
            self._scoped_tile_bindings.move_to_end(public_id)
        while len(self._scoped_tile_bindings) > MAX_NATIVE_STATE_SCOPE_ROWS:
            self._scoped_tile_bindings.popitem(last=False)
        if request.section in {
            "known_tiles", "map_tiles", "tile_window", "target_tiles",
            "action_decision_tile",
        }:
            for item in parsed_rows:
                key = (snapshot.native_revision, item["native_index"])
                self._scoped_tile_metadata[key] = _freeze(item)
                self._scoped_tile_metadata.move_to_end(key)
            while len(self._scoped_tile_metadata) > MAX_NATIVE_STATE_SCOPE_ROWS:
                self._scoped_tile_metadata.popitem(last=False)
        if request.section == "target_tiles":
            self._actor_tile_overlays[
                (snapshot.native_revision, request.selector)
            ] = tuple(_freeze(item) for item in parsed_rows)
            self._actor_tile_overlays.move_to_end(
                (snapshot.native_revision, request.selector)
            )
            while len(self._actor_tile_overlays) > MAX_PINNED_SCOPE_VIEWS:
                self._actor_tile_overlays.popitem(last=False)
        elif (
            request.actor_id is not None
            and request.section in _CITY_STATE_SECTIONS
        ):
            self._city_state_overlays[
                (snapshot.native_revision, request.selector, request.section)
            ] = tuple(_freeze(item) for item in parsed_rows)
        if request.section in {
            "pregame_nations", "pregame_styles", "pregame_teams",
        }:
            self._pregame_state_overlays[
                (snapshot.native_revision, request.section)
            ] = tuple(_freeze(item) for item in parsed_rows)
        if request.section == "chat_recipients":
            recipients = tuple(
                _freeze(item) for item in parsed_rows
            )
            existing = self._chat_recipient_overlays.get(
                snapshot.native_revision,
            )
            if existing is not None and existing != recipients:
                raise V2ControlError("internal_error")
            self._chat_recipient_overlays[snapshot.native_revision] = recipients
        if request.relation_id is not None:
            key = (snapshot.native_revision, request.selector)
            public_frozen = tuple(_freeze(item) for item in public_items)
            parsed_frozen = tuple(_freeze(item) for item in parsed_rows)
            charge = len(json.dumps(
                {
                    "public": [_thaw(item) for item in public_frozen],
                    "parsed": [_thaw(item) for item in parsed_frozen],
                },
                ensure_ascii=False, allow_nan=False, sort_keys=True,
                separators=(",", ":"),
            ).encode("utf-8"))
            if charge > MAX_RELATION_OVERLAY_BYTES:
                raise V2ControlError("scope_too_large")
            self._drop_relation_overlay(key)
            while (
                len(self._relation_overlay_charges)
                    >= MAX_RELATION_OVERLAY_ENTRIES
                or self._relation_overlay_bytes + charge
                    > MAX_RELATION_OVERLAY_BYTES
            ):
                if not self._relation_overlay_charges:
                    raise V2ControlError("scope_too_large")
                oldest = next(iter(self._relation_overlay_charges))
                self._drop_relation_overlay(oldest)
            self._relation_state_overlays[
                key
            ] = public_frozen
            self._relation_clause_overlays[
                key
            ] = parsed_frozen
            self._relation_overlay_charges[key] = charge
            self._relation_overlay_bytes += charge

    def _drop_relation_overlay(self, key: tuple[int, str]) -> None:
        charge = self._relation_overlay_charges.pop(key, 0)
        self._relation_overlay_bytes -= charge
        self._relation_state_overlays.pop(key, None)
        self._relation_clause_overlays.pop(key, None)

    def legal_actions_page(
        self,
        observation: Mapping[str, Any],
        limit: int = MAX_PAGE_ITEMS,
    ) -> dict[str, Any]:
        clean_limit = self._limit(limit)
        with self._lock:
            self._require_open()
            snapshot = self._snapshot(observation)
            return self._page(
                snapshot, "legal_actions", "legal_actions", clean_limit, 0,
            )

    def prepare_actor_scope(
        self,
        observation: Mapping[str, Any],
        actor_id: Any,
        limit: int = MAX_PAGE_ITEMS,
    ) -> V2ActorScopeRequest:
        """Resolve a current owned opaque actor to a private native ref."""
        clean_limit = self._limit(limit)
        with self._lock:
            self._require_open()
            snapshot = self._snapshot(observation)
            if not self._snapshots or snapshot.native_revision != max(self._snapshots):
                raise V2ControlError("stale_revision")
            binding = (
                snapshot.actor_bindings.get(actor_id)
                if isinstance(actor_id, str) else None
            )
            if binding is None:
                raise V2ControlError("invalid_request")
            return V2ActorScopeRequest(
                actor_id=actor_id,
                actor_kind=binding.kind,
                native_actor_ref=binding.native_ref,
                native_revision=snapshot.native_revision,
                limit=clean_limit,
            )

    def prepare_relation_scope(
        self,
        observation: Mapping[str, Any],
        actor_id: Any,
        relation_id: Any,
        limit: int = MAX_PAGE_ITEMS,
    ) -> V2RelationScopeRequest:
        """Resolve ``self player + opaque relation`` to private native refs."""
        clean_limit = self._limit(limit)
        with self._lock:
            self._require_open()
            snapshot = self._snapshot(observation)
            if not self._snapshots or snapshot.native_revision != max(
                self._snapshots
            ):
                raise V2ControlError("stale_revision")
            actor = (
                snapshot.actor_bindings.get(actor_id)
                if isinstance(actor_id, str) else None
            )
            relation = (
                snapshot.relation_bindings.get(relation_id)
                if isinstance(relation_id, str) else None
            )
            if (
                actor is None or actor.kind != "player"
                or relation is None
                or snapshot.parsed.player is None
                or actor.native_ref != snapshot.parsed.player["ref"]
            ):
                raise V2ControlError("invalid_request")
            return V2RelationScopeRequest(
                actor_id=actor_id,
                native_actor_ref=actor.native_ref,
                relation_id=relation_id,
                native_counterpart_ref=relation.native_counterpart_ref,
                native_revision=snapshot.native_revision,
                limit=clean_limit,
            )

    def prepare_relation_support_scope(
        self,
        observation: Mapping[str, Any],
        relation_id: Any,
    ) -> V2StateScopeRequest:
        """Return the clause catalog required by a relation action scope."""
        return self.prepare_state_scope(
            observation,
            "diplomacy_clauses",
            MAX_PAGE_ITEMS,
            relation_id=relation_id,
        )

    def prepare_target_action(
        self,
        observation: Mapping[str, Any],
        actor_id: Any,
        target_id: Any,
        limit: int = MAX_PAGE_ITEMS,
    ) -> V2TargetActionRequest:
        """Resolve one current owned player/unit/city and known target tile."""
        clean_limit = self._limit(limit)
        with self._lock:
            self._require_open()
            snapshot = self._snapshot(observation)
            if not self._snapshots or snapshot.native_revision != max(
                self._snapshots
            ):
                raise V2ControlError("stale_revision")
            actor = (
                snapshot.actor_bindings.get(actor_id)
                if isinstance(actor_id, str)
                and _PUBLIC_ACTOR_ID.fullmatch(actor_id) is not None
                else None
            )
            native_tile = (
                snapshot.tile_bindings.get(target_id)
                if isinstance(target_id, str)
                and _PUBLIC_TILE_ID.fullmatch(target_id) is not None
                else None
            )
            if (
                native_tile is None
                and isinstance(target_id, str)
                and _PUBLIC_TILE_ID.fullmatch(target_id) is not None
            ):
                scoped = self._scoped_tile_bindings.get(target_id)
                if scoped is not None and scoped[0] == snapshot.native_revision:
                    native_tile = scoped[1]
            decision = (
                snapshot.action_decision_bindings.get(target_id)
                if native_tile is None and isinstance(target_id, str)
                else None
            )
            action_decision = bool(
                decision is not None and decision.actor_id == actor_id
            )
            if action_decision:
                native_tile = decision.native_target_tile
            if (
                actor is None or actor.kind not in {"player", "unit", "city"}
                or native_tile is None
            ):
                raise V2ControlError("invalid_request")
            return V2TargetActionRequest(
                actor_id=actor_id,
                actor_kind=actor.kind,
                native_actor_ref=actor.native_ref,
                target_id=target_id,
                native_target_tile=native_tile,
                native_revision=snapshot.native_revision,
                limit=clean_limit,
                action_decision=action_decision,
            )

    def target_action_page(
        self,
        request: V2TargetActionRequest,
        native_result: Mapping[str, Any],
    ) -> dict[str, Any]:
        """Validate, freeze, and page one target-bound capability catalog."""
        with self._lock:
            self._require_open()
            snapshot = self._snapshots.get(request.native_revision)
            if snapshot is None or request.native_revision != max(
                self._snapshots
            ):
                raise V2ControlError("stale_revision")
            actor = snapshot.actor_bindings.get(request.actor_id)
            scoped_tile = self._scoped_tile_bindings.get(request.target_id)
            bound_tile = snapshot.tile_bindings.get(request.target_id)
            if (
                bound_tile is None and scoped_tile is not None
                and scoped_tile[0] == snapshot.native_revision
            ):
                bound_tile = scoped_tile[1]
            if request.action_decision:
                decision = snapshot.action_decision_bindings.get(
                    request.target_id,
                )
                bound_tile = (
                    decision.native_target_tile
                    if decision is not None
                    and decision.actor_id == request.actor_id
                    else None
                )
            if (
                actor is None or actor.kind != request.actor_kind
                or actor.kind not in {"player", "unit", "city"}
                or actor.native_ref != request.native_actor_ref
                or bound_tile != request.native_target_tile
            ):
                raise V2ControlError("invalid_request")
            try:
                items, pending = self._validate_target_action_result(
                    snapshot, request, native_result,
                )
            except _ObservationError as exc:
                raise V2ControlError("internal_error") from exc
            scope = {
                "actor_id": request.actor_id,
                "actor_type": request.actor_kind,
                "target_id": request.target_id,
                "target_type": "tile",
            }
            return self._start_page_chain(
                snapshot,
                "legal_actions",
                "legal_actions",
                request.limit,
                items,
                {
                    "endpoint": "legal_actions",
                    "query": {
                        "actor_id": request.actor_id,
                        "target_id": request.target_id,
                        "limit": request.limit,
                    },
                },
                scope=scope,
                catalog_id=self._mac(
                    "catalog", "target", request.native_revision,
                    request.native_actor_ref, request.native_target_tile,
                ),
                pending_bindings=pending,
            )

    def prepare_target_tile_support(
        self, request: V2TargetActionRequest,
    ) -> V2StateScopeRequest:
        """Bind one target query to its exact fog-safe native tile row."""
        with self._lock:
            self._require_open()
            snapshot = self._snapshots.get(request.native_revision)
            if (
                snapshot is None
                or request.native_revision != max(self._snapshots)
            ):
                raise V2ControlError("stale_revision")
            bound_tile = snapshot.tile_bindings.get(request.target_id)
            if bound_tile is None:
                scoped = self._scoped_tile_bindings.get(request.target_id)
                if scoped is not None and scoped[0] == snapshot.native_revision:
                    bound_tile = scoped[1]
            if request.action_decision:
                decision = snapshot.action_decision_bindings.get(
                    request.target_id,
                )
                bound_tile = (
                    decision.native_target_tile
                    if decision is not None
                    and decision.actor_id == request.actor_id
                    else None
                )
            if bound_tile != request.native_target_tile:
                raise V2ControlError("invalid_request")
            return V2StateScopeRequest(
                section=(
                    "action_decision_tile"
                    if request.action_decision else "tile_window"
                ),
                selector=(
                    request.native_actor_ref
                    if request.action_decision
                    else f"t{request.native_target_tile}-r0"
                ),
                native_revision=request.native_revision,
                limit=MAX_PAGE_ITEMS,
                actor_id=(request.actor_id if request.action_decision else None),
                center_id=(None if request.action_decision else request.target_id),
                radius=(None if request.action_decision else 0),
            )

    def take_actor_scope_cursor(
        self, cursor: str, *, endpoint: str,
    ) -> V2ActorScopeRequest | dict[str, Any] | None:
        """Reserve a scoped cursor, or replay its committed public page.

        Reservation is deliberately non-destructive.  The supervisor commits
        only after the fallible native page read and projection both succeed;
        aborting a reservation leaves the exact cursor reusable.
        """
        if not isinstance(cursor, str) or not cursor or endpoint != "legal_actions":
            raise V2ControlError("invalid_request")
        with self._lock:
            self._require_open()
            record = self._cursor_record(cursor, endpoint)
            if not isinstance(record, _ActorScopeCursor):
                return None
            if record.response is not None:
                return _thaw(record.response)
            if record.in_flight:
                raise V2ControlError("cursor_in_progress")
            snapshot = self._snapshots.get(record.native_revision)
            if snapshot is None or record.native_revision != max(self._snapshots):
                self._retire_cursor(cursor, record, "stale_revision")
                # This was an authentic, actor-scoped capability for this
                # seat generation.  A newer observation makes it stale, not
                # malformed; callers can safely restart the actor query.
                raise V2ControlError("stale_revision")
            self._cursors[cursor] = replace(record, in_flight=True)
            self._cursor_leases[cursor] = (
                time.monotonic() + CURSOR_IN_FLIGHT_LEASE_SECONDS
            )
            return V2ActorScopeRequest(
                actor_id=record.actor_id,
                actor_kind=record.actor_kind,
                native_actor_ref=record.native_actor_ref,
                native_revision=record.native_revision,
                limit=record.limit,
                offset=record.next_offset,
                native_view_id=record.native_view_id,
                total_count=record.total_count,
                seen_slots=record.seen_slots,
                seen_capabilities=record.seen_capabilities,
                pending_scope_bindings=(
                    record.pending_scope_bindings
                ),
            )

    def take_relation_scope_cursor(
        self, cursor: str, *, endpoint: str,
    ) -> V2RelationScopeRequest | dict[str, Any] | None:
        """Reserve a relation cursor, or replay its committed public page."""
        if not isinstance(cursor, str) or not cursor or endpoint != "legal_actions":
            raise V2ControlError("invalid_request")
        with self._lock:
            self._require_open()
            record = self._cursor_record(cursor, endpoint)
            if not isinstance(record, _RelationScopeCursor):
                return None
            if record.response is not None:
                return _thaw(record.response)
            if record.in_flight:
                raise V2ControlError("cursor_in_progress")
            snapshot = self._snapshots.get(record.native_revision)
            if snapshot is None or record.native_revision != max(self._snapshots):
                self._retire_cursor(cursor, record, "stale_revision")
                raise V2ControlError("stale_revision")
            self._cursors[cursor] = replace(record, in_flight=True)
            self._cursor_leases[cursor] = (
                time.monotonic() + CURSOR_IN_FLIGHT_LEASE_SECONDS
            )
            return V2RelationScopeRequest(
                actor_id=record.actor_id,
                native_actor_ref=record.native_actor_ref,
                relation_id=record.relation_id,
                native_counterpart_ref=record.native_counterpart_ref,
                native_revision=record.native_revision,
                limit=record.limit,
                offset=record.next_offset,
                native_view_id=record.native_view_id,
                total_count=record.total_count,
                seen_slots=record.seen_slots,
                seen_capabilities=record.seen_capabilities,
                pending_scope_bindings=record.pending_scope_bindings,
            )

    def is_actor_scope_cursor(self, cursor: str, *, endpoint: str) -> bool:
        """Identify a cursor without reserving or consuming it."""
        if not isinstance(cursor, str) or not cursor or endpoint != "legal_actions":
            raise V2ControlError("invalid_request")
        with self._lock:
            self._require_open()
            if self._is_page_chain_cursor(cursor, endpoint):
                return False
            record = self._cursor_record(cursor, endpoint)
            return isinstance(record, _ActorScopeCursor)

    def is_relation_scope_cursor(self, cursor: str, *, endpoint: str) -> bool:
        """Identify a relation cursor without consuming it."""
        if not isinstance(cursor, str) or not cursor or endpoint != "legal_actions":
            raise V2ControlError("invalid_request")
        with self._lock:
            self._require_open()
            if self._is_page_chain_cursor(cursor, endpoint):
                return False
            record = self._cursor_record(cursor, endpoint)
            return isinstance(record, _RelationScopeCursor)

    def _is_page_chain_cursor(self, cursor: str, endpoint: str) -> bool:
        decoded = self._decode_page_chain_token(cursor, endpoint)
        if decoded is None:
            return False
        self._expire_page_chains()
        nonce, page_index = decoded
        chain = self._page_chains.get(nonce)
        if chain is not None:
            if chain.endpoint != endpoint or not (
                1 <= page_index <= chain.exposed_through
            ):
                raise V2ControlError("invalid_request")
            if chain.deadlines[page_index] <= time.monotonic():
                raise V2ControlError(
                    "cursor_expired",
                    details={"restart": _thaw(chain.restart)},
                )
            return True
        retired = self._retired_page_chains.get(nonce)
        if (
            retired is not None and retired.endpoint == endpoint
            and 1 <= page_index <= retired.exposed_through
        ):
            raise V2ControlError(
                retired.code,
                details={"restart": _thaw(retired.restart)},
            )
        raise V2ControlError("invalid_request")

    def commit_scope_cursor(
        self,
        cursor: str,
        request: V2ActorScopeRequest | V2RelationScopeRequest,
        page: Mapping[str, Any],
    ) -> dict[str, Any]:
        """Atomically commit one successful scoped continuation for replay."""
        with self._lock:
            self._require_open()
            record = self._cursor_record(cursor, "legal_actions")
            if record.response is not None:
                return _thaw(record.response)
            if not record.in_flight:
                raise V2ControlError("invalid_request")
            if isinstance(record, _ActorScopeCursor):
                valid_request = isinstance(request, V2ActorScopeRequest) and (
                    request.actor_id == record.actor_id
                    and request.actor_kind == record.actor_kind
                    and request.native_actor_ref == record.native_actor_ref
                    and request.native_revision == record.native_revision
                    and request.native_view_id == record.native_view_id
                    and request.offset == record.next_offset
                    and request.limit == record.limit
                )
            elif isinstance(record, _RelationScopeCursor):
                valid_request = isinstance(request, V2RelationScopeRequest) and (
                    request.actor_id == record.actor_id
                    and request.native_actor_ref == record.native_actor_ref
                    and request.relation_id == record.relation_id
                    and request.native_counterpart_ref
                       == record.native_counterpart_ref
                    and request.native_revision == record.native_revision
                    and request.native_view_id == record.native_view_id
                    and request.offset == record.next_offset
                    and request.limit == record.limit
                )
            else:
                valid_request = False
            if not valid_request or not isinstance(page, Mapping):
                raise V2ControlError("invalid_request")
            checked = self._checked_public_page(dict(page))
            now = time.monotonic()
            wall = time.time()
            committed = replace(
                record,
                in_flight=False,
                response=_freeze(checked),
                seen_slots=(),
                seen_capabilities=(),
                pending_scope_bindings=(),
                expires_at=now + CURSOR_TTL_SECONDS,
                expires_at_wall=wall + CURSOR_TTL_SECONDS,
            )
            self._cursors[cursor] = committed
            self._cursors.move_to_end(cursor)
            self._cursor_leases.pop(cursor, None)
            return _thaw(committed.response)

    def abort_scope_cursor(self, cursor: str) -> None:
        """Release a scoped reservation after a fallible continuation fails."""
        with self._lock:
            self._cursor_leases.pop(cursor, None)
            record = self._cursors.get(cursor)
            if isinstance(record, (_ActorScopeCursor, _RelationScopeCursor)):
                if record.in_flight and record.response is None:
                    self._cursors[cursor] = replace(record, in_flight=False)

    def actor_scope_page(
        self,
        request: V2ActorScopeRequest,
        native_page: Mapping[str, Any],
    ) -> dict[str, Any]:
        """Validate and project one bounded actor-scoped native page."""
        with self._lock:
            self._require_open()
            snapshot = self._snapshots.get(request.native_revision)
            if snapshot is None or request.native_revision != max(self._snapshots):
                raise V2ControlError("stale_revision")
            binding = snapshot.actor_bindings.get(request.actor_id)
            if (
                binding is None
                or binding.kind != request.actor_kind
                or binding.native_ref != request.native_actor_ref
            ):
                raise V2ControlError("invalid_request")
            try:
                page = self._validate_actor_scope_page(
                    snapshot, request, native_page, request.seen_slots,
                )
            except _ObservationError as exc:
                raise V2ControlError("internal_error") from exc
            if not isinstance(page, dict):
                raise V2ControlError("internal_error")
            return self._checked_public_page(page)

    def materialize_actor_scope(
        self,
        request: V2ActorScopeRequest,
        native_catalog: Mapping[str, Any],
    ) -> dict[str, Any]:
        """Validate a complete native actor catalog before page one escapes."""
        with self._lock:
            self._require_open()
            snapshot = self._snapshots.get(request.native_revision)
            binding = snapshot.actor_bindings.get(request.actor_id) if snapshot else None
            if (
                snapshot is None
                or request.native_revision != max(self._snapshots)
                or binding is None
                or binding.kind != request.actor_kind
                or binding.native_ref != request.native_actor_ref
                or request.offset != 0
                or request.native_view_id is not None
            ):
                raise V2ControlError("stale_revision")
            try:
                result = self._validate_actor_scope_page(
                    snapshot, request, native_catalog, (), materialize=True,
                )
            except _ObservationError as exc:
                raise V2ControlError("internal_error") from exc
            if not isinstance(result, tuple):
                raise V2ControlError("internal_error")
            projected, pending = result
            page = projected["page"]
            return self._start_page_chain(
                snapshot,
                "legal_actions",
                "legal_actions",
                request.limit,
                tuple(page["items"]),
                {
                    "endpoint": "legal_actions",
                    "query": {
                        "actor_id": request.actor_id,
                        "limit": request.limit,
                    },
                },
                scope=page["scope"],
                catalog_id=page["catalog_id"],
                pending_bindings=pending,
            )

    def relation_scope_page(
        self,
        request: V2RelationScopeRequest,
        native_page: Mapping[str, Any],
    ) -> dict[str, Any]:
        """Validate and project one bounded relation-scoped native page."""
        with self._lock:
            self._require_open()
            snapshot = self._snapshots.get(request.native_revision)
            if snapshot is None or request.native_revision != max(self._snapshots):
                raise V2ControlError("stale_revision")
            actor = snapshot.actor_bindings.get(request.actor_id)
            relation = snapshot.relation_bindings.get(request.relation_id)
            if (
                actor is None or actor.kind != "player"
                or actor.native_ref != request.native_actor_ref
                or relation is None
                or relation.native_counterpart_ref
                   != request.native_counterpart_ref
            ):
                raise V2ControlError("invalid_request")
            try:
                page = self._validate_relation_scope_page(
                    snapshot, request, native_page,
                )
            except _ObservationError as exc:
                raise V2ControlError("internal_error") from exc
            if not isinstance(page, dict):
                raise V2ControlError("internal_error")
            return self._checked_public_page(page)

    def materialize_relation_scope(
        self,
        request: V2RelationScopeRequest,
        native_catalog: Mapping[str, Any],
    ) -> dict[str, Any]:
        """Validate a complete native relation catalog before page one."""
        with self._lock:
            self._require_open()
            snapshot = self._snapshots.get(request.native_revision)
            actor = (
                snapshot.actor_bindings.get(request.actor_id)
                if snapshot is not None else None
            )
            relation = (
                snapshot.relation_bindings.get(request.relation_id)
                if snapshot is not None else None
            )
            if (
                snapshot is None
                or request.native_revision != max(self._snapshots)
                or actor is None or actor.kind != "player"
                or actor.native_ref != request.native_actor_ref
                or relation is None
                or relation.native_counterpart_ref
                   != request.native_counterpart_ref
                or request.offset != 0
                or request.native_view_id is not None
            ):
                raise V2ControlError("stale_revision")
            try:
                result = self._validate_relation_scope_page(
                    snapshot, request, native_catalog, materialize=True,
                )
            except _ObservationError as exc:
                raise V2ControlError("internal_error") from exc
            if not isinstance(result, tuple):
                raise V2ControlError("internal_error")
            projected, pending = result
            page = projected["page"]
            return self._start_page_chain(
                snapshot,
                "legal_actions",
                "legal_actions",
                request.limit,
                tuple(page["items"]),
                {
                    "endpoint": "legal_actions",
                    "query": {
                        "actor_id": request.actor_id,
                        "target_id": request.relation_id,
                    },
                },
                scope=page["scope"],
                catalog_id=page["catalog_id"],
                pending_bindings=pending,
            )

    def resolve_action(
        self,
        observation: Mapping[str, Any],
        state_revision: Any,
        action_id: Any,
        arguments: Any,
    ) -> V2ActionResolution:
        """Resolve one public capability into current, private native inputs.

        The returned argument is the decoded ACT payload expected by
        ``HeadlessSidecar.execute_action``.  That adapter applies the protocol's
        canonical percent encoding to the entire field exactly once.
        """
        with self._lock:
            self._require_open()
            # Public state reads reject evicted native regressions as corrupt.
            # At this command boundary, however, a structurally plausible
            # earlier revision is simply an expired capability and must not be
            # reported as an internal failure.
            if (
                isinstance(observation, Mapping)
                and set(observation) == {"generation", "native_revision", "rows"}
                and isinstance(observation.get("rows"), tuple)
            ):
                observed_generation = observation.get("generation")
                observed_revision = observation.get("native_revision")
                if (
                    not isinstance(observed_generation, bool)
                    and isinstance(observed_generation, int)
                    and observed_generation == self.generation
                    and not isinstance(observed_revision, bool)
                    and isinstance(observed_revision, int)
                    and 1 <= observed_revision <= _U64_MAX
                    and observed_revision <= self._highest_native_revision
                    and observed_revision not in self._snapshots
                ):
                    raise V2ControlError("stale_revision")
            snapshot = self._snapshot(observation)
            if (
                not self._snapshots
                or snapshot.native_revision != max(self._snapshots)
            ):
                raise V2ControlError("stale_revision")

            expected = snapshot.state_revision
            candidate_token = (
                state_revision.get("state_token")
                if isinstance(state_revision, dict) else None
            )
            token_matches = isinstance(candidate_token, str) and hmac.compare_digest(
                candidate_token, expected["state_token"],
            )
            try:
                requested = validate_state_revision(state_revision)
            except FullControlSchemaError as exc:
                raise V2ControlError("stale_revision") from exc
            if (
                not token_matches
                or requested["turn"] != expected["turn"]
                or requested["revision"] != expected["revision"]
            ):
                raise V2ControlError("stale_revision")

            binding = (
                snapshot.action_bindings.get(action_id)
                if isinstance(action_id, str) else None
            )
            if binding is None and isinstance(action_id, str):
                binding = self._scoped_action_bindings.get(action_id)
                if (
                    binding is not None
                    and binding.native_revision != snapshot.native_revision
                ):
                    binding = None
            if binding is None:
                raise V2ControlError("action_expired")
            self._reject_phase_control_proposal(binding)
            native_arguments = self._resolve_arguments(
                snapshot, binding, arguments,
            )
            return V2ActionResolution(
                native_slot=binding.slot,
                native_revision=binding.native_revision,
                native_arguments=native_arguments,
                public_kind=binding.public_kind,
                operation=binding.operation,
                turn=binding.turn,
                phase=binding.phase,
                native_actor_ref=binding.actor_ref,
                native_counterpart_ref=binding.counterpart_ref,
                scoped=binding.scoped,
                relation_scoped=binding.relation_scoped,
            )

    @staticmethod
    def _reject_phase_control_proposal(binding: _ActionBinding) -> None:
        """Refuse governance proposals that would strand this seat's phase.

        Two server settings provably break full-control-v2's phase handover:

        ``fixedlength`` enabled makes Freeciv's own ``can_end_turn()``
        (client/mapctrl_common.c) return false unconditionally, regardless of
        ``timeout``.  That flag is what the native boundary reports as this
        seat's phase readiness, and phase.end is only advertised while it
        holds, so the seat can never end its phase again.  The server's
        matching guard in ``check_for_full_turn_done`` only honours
        ``fixedlength`` when ``timeout`` is nonzero, and these games run with
        ``timeout 0``, so the server would still be waiting on a phase_done
        the agent has lost every means of sending.

        ``phasemode`` other than PLAYER breaks the one-active-seat invariant
        the phase ledger enforces, which fails the whole game.

        Turning ``fixedlength`` back off stays legal so a game that somehow
        acquired it can recover.
        """
        if binding.operation != "propose_server_setting":
            return
        name = binding.server_setting_name
        if name == "phasemode" or (
            name == "fixedlength" and binding.server_setting_value
        ):
            raise V2ControlError(
                "invalid_request",
                details={"rejection_reason": "phase_control_conflict"},
            )

    def continue_page(self, cursor: str, *, endpoint: str) -> dict[str, Any]:
        if (
            not isinstance(cursor, str)
            or not cursor
            or endpoint not in {"state", "legal_actions"}
        ):
            raise V2ControlError("invalid_request")
        with self._lock:
            self._require_open()
            chained = self._continue_page_chain(cursor, endpoint)
            if chained is not None:
                return chained
            record = self._cursor_record(cursor, endpoint)
            if not isinstance(record, _Cursor):
                raise V2ControlError("invalid_request")
            if record.response is not None:
                return _thaw(record.response)
            snapshot = self._snapshots.get(record.native_revision)
            if snapshot is None:
                self._retire_cursor(cursor, record, "stale_revision")
                raise V2ControlError("stale_revision")
            page = self._page(
                snapshot,
                record.endpoint,
                record.section,
                record.limit,
                record.next_offset,
                actor_id=record.actor_id,
                center_id=record.center_id,
                radius=record.radius,
            )
            now = time.monotonic()
            wall = time.time()
            committed = replace(
                record,
                response=_freeze(page),
                expires_at=now + CURSOR_TTL_SECONDS,
                expires_at_wall=wall + CURSOR_TTL_SECONDS,
            )
            self._cursors[cursor] = committed
            self._cursors.move_to_end(cursor)
            return _thaw(committed.response)

    def _continue_page_chain(
        self, cursor: str, endpoint: str,
    ) -> dict[str, Any] | None:
        decoded = self._decode_page_chain_token(cursor, endpoint)
        if decoded is None:
            return None
        self._expire_page_chains()
        nonce, page_index = decoded
        chain = self._page_chains.get(nonce)
        if chain is None:
            retired = self._retired_page_chains.get(nonce)
            if retired is None or retired.endpoint != endpoint:
                raise V2ControlError("invalid_request")
            if 1 <= page_index <= retired.exposed_through:
                raise V2ControlError(
                    retired.code,
                    details={"restart": _thaw(retired.restart)},
                )
            raise V2ControlError("invalid_request")
        if chain.endpoint != endpoint or not 1 <= page_index <= chain.exposed_through:
            raise V2ControlError("invalid_request")
        now = time.monotonic()
        if chain.deadlines[page_index] <= now:
            if chain.frontier == page_index:
                self._retire_page_chain(nonce, chain)
            raise V2ControlError(
                "cursor_expired",
                details={"restart": _thaw(chain.restart)},
            )
        cached = chain.responses.get(page_index)
        if cached is not None:
            return _thaw(cached)
        if (
            chain.pending_bindings
            and not chain.bindings_published
            and (
                not self._snapshots
                or chain.state_revision.get("revision") != max(self._snapshots)
            )
        ):
            raise V2ControlError(
                "stale_revision", details={"restart": _thaw(chain.restart)},
            )
        if chain.frontier != page_index:
            raise V2ControlError("invalid_request")
        wall = time.time()
        chain.deadlines[page_index] = now + CURSOR_TTL_SECONDS
        chain.expiry_walls[page_index] = wall + CURSOR_TTL_SECONDS
        next_index = page_index + 1
        if next_index < len(chain.ranges):
            next_cursor = chain.tokens[next_index]
            next_wall = wall + CURSOR_TTL_SECONDS
            chain.deadlines[next_index] = now + CURSOR_TTL_SECONDS
            chain.expiry_walls[next_index] = next_wall
            chain.exposed_through = next_index
            chain.frontier = next_index
            expires_at = self._cursor_expiry_text(next_wall)
        else:
            next_cursor = None
            expires_at = None
            chain.frontier = None
        start, end = chain.ranges[page_index]
        rendered = self._checked_public_page(self._chain_public_page(
            chain.state_revision,
            chain.section,
            chain.values,
            start,
            end,
            next_cursor,
            expires_at,
            scope=chain.scope,
            catalog_id=chain.catalog_id,
        ))
        if next_cursor is None and not chain.bindings_published:
            for action_id, binding in chain.pending_bindings:
                self._publish_scoped_binding(action_id, binding)
            chain.bindings_published = True
        chain.responses[page_index] = _freeze(rendered)
        self._page_chains.move_to_end(nonce)
        return _thaw(chain.responses[page_index])

    @staticmethod
    def _limit(limit: int) -> int:
        if (
            isinstance(limit, bool)
            or not isinstance(limit, int)
            or limit < 1
            or limit > MAX_PAGE_ITEMS
        ):
            raise V2ControlError("invalid_request")
        return limit

    def _state_section_values(
        self,
        snapshot: _ProjectedSnapshot,
        section: str,
        actor_id: str | None,
        relation_id: str | None,
        center_id: str | None,
        radius: int | None,
    ) -> tuple[Mapping[str, Any], ...]:
        """Resolve one typed state query without exposing private bindings."""
        if section in _CITY_STATE_SECTIONS:
            binding = (
                snapshot.actor_bindings.get(actor_id)
                if isinstance(actor_id, str) else None
            )
            if (
                binding is None or binding.kind != "city"
                or relation_id is not None
                or center_id is not None or radius is not None
            ):
                raise V2ControlError("invalid_request")
            identity_key = "id" if section == "city_detail" else "city_id"
            return tuple(
                item for item in snapshot.sections[section]
                if item.get(identity_key) == actor_id
            )
        if section == "diplomacy_clauses":
            if relation_id is None and snapshot.parsed.diplomacy_clauses:
                if actor_id is not None or center_id is not None or radius is not None:
                    raise V2ControlError("invalid_request")
                return snapshot.sections[section]
            binding = (
                snapshot.relation_bindings.get(relation_id)
                if isinstance(relation_id, str) else None
            )
            if (
                binding is None or actor_id is not None
                or center_id is not None or radius is not None
            ):
                raise V2ControlError("invalid_request")
            return self._relation_state_overlays.get(
                (snapshot.native_revision, binding.native_counterpart_ref),
                (),
            )
        if section == "chat_recipients":
            if (
                actor_id is not None or relation_id is not None
                or center_id is not None or radius is not None
            ):
                raise V2ControlError("invalid_request")
            return tuple(_freeze({
                "id": self._entity_id("player", item["ref"]),
                "name": item["name"],
                "self": item["self"],
                "connected": item["connected"],
                "can_message": item["can_message"],
            }) for item in self._chat_recipient_overlays.get(
                snapshot.native_revision, (),
            ))
        if section == "tile_window":
            if (
                actor_id is not None
                or relation_id is not None
                or not isinstance(center_id, str)
                or isinstance(radius, bool) or not isinstance(radius, int)
                or not 0 <= radius <= MAX_TILE_WINDOW_RADIUS
                or center_id not in snapshot.tile_bindings
            ):
                raise V2ControlError("invalid_request")
            center = next((
                item for item in snapshot.sections["known_tiles"]
                if item["id"] == center_id
            ), None)
            if center is None or center["visibility"] == "unknown":
                raise V2ControlError("invalid_request")
            selected: list[dict[str, Any]] = []
            for item in snapshot.sections["tile_window"]:
                distance = self._map_distance(
                    snapshot.parsed.meta, center["x"], center["y"],
                    item["x"], item["y"],
                )
                if distance <= radius:
                    public = _thaw(item)
                    public["distance"] = distance
                    selected.append(public)
            selected.sort(key=lambda item: (
                item["distance"], item["y"], item["x"], item["id"],
            ))
            return tuple(_freeze(item) for item in selected)
        if (
            actor_id is not None or relation_id is not None
            or center_id is not None or radius is not None
        ):
            raise V2ControlError("invalid_request")
        return snapshot.sections[section]

    @staticmethod
    def _map_coordinates_for_native_index(
        meta: Mapping[str, Any], native_index: int,
    ) -> tuple[int, int]:
        width = meta["map_width"]
        height = meta["map_height"]
        if not 0 <= native_index < width * height:
            _fail()
        native_x = native_index % width
        native_y = native_index // width
        if not meta["topology"].startswith("isometric_"):
            return native_x, native_y
        x = native_x + (native_y + (native_y & 1)) // 2
        return x, width + native_y - x

    @staticmethod
    def _validate_tile_coordinates(
        meta: Mapping[str, Any], tile: Mapping[str, Any], *,
        canonical: bool = True,
    ) -> None:
        """Prove that one native tile index and map coordinate are identical."""
        width = meta["map_width"]
        height = meta["map_height"]
        x = tile["x"]
        y = tile["y"]
        if meta["topology"].startswith("isometric_"):
            native_y = x + y - width
            native_x = (2 * x - native_y - (native_y & 1)) // 2
        else:
            native_x = x
            native_y = y
        if (
            not 0 <= native_x < width
            or not 0 <= native_y < height
            or not 0 <= tile["native_index"] < width * height
            or canonical
               and tile["native_index"] != native_x + native_y * width
        ):
            _fail()

    @staticmethod
    def _map_distance(
        meta: Mapping[str, Any], x0: int, y0: int, x1: int, y1: int,
    ) -> int:
        """Mirror Freeciv's wrapping/topology distance in map coordinates."""
        width = meta["map_width"]
        height = meta["map_height"]
        isometric = meta["topology"].startswith("isometric_")
        is_hex = meta["topology"].endswith("hex")
        native_coordinates = isometric or is_hex

        def native(x: int, y: int) -> tuple[int, int]:
            if not native_coordinates:
                return x, y
            native_y = x + y - width
            native_x = (2 * x - native_y - (native_y & 1)) // 2
            return native_x, native_y

        nx0, ny0 = native(x0, y0)
        nx1, ny1 = native(x1, y1)
        dx = nx1 - nx0
        dy = ny1 - ny0
        if meta["wrap_x"]:
            dx = (dx + width // 2) % width - width // 2
        if meta["wrap_y"]:
            dy = (dy + height // 2) % height - height // 2
        if native_coordinates:
            virtual_x0 = (ny0 + (ny0 & 1)) // 2 + nx0
            virtual_y0 = ny0 - virtual_x0 + width
            virtual_x1 = (
                (ny0 + dy + ((ny0 + dy) & 1)) // 2 + nx0 + dx
            )
            virtual_y1 = ny0 + dy - virtual_x1 + width
            dx = virtual_x1 - virtual_x0
            dy = virtual_y1 - virtual_y0
        absolute_x = abs(dx)
        absolute_y = abs(dy)
        if not is_hex:
            return max(absolute_x, absolute_y)
        blocked_diagonal = (
            (dx < 0 and dy > 0) or (dx > 0 and dy < 0)
            if isometric else
            (dx > 0 and dy > 0) or (dx < 0 and dy < 0)
        )
        return (
            absolute_x + absolute_y
            if blocked_diagonal else max(absolute_x, absolute_y)
        )

    @staticmethod
    def _canonical_public_bytes(value: Mapping[str, Any]) -> int:
        return len(json.dumps(
            value, ensure_ascii=False, allow_nan=False, sort_keys=True,
            separators=(",", ":"),
        ).encode("utf-8"))

    def _checked_public_page(self, page: dict[str, Any]) -> dict[str, Any]:
        if self._canonical_public_bytes(page) > MAX_PUBLIC_PAGE_BYTES:
            raise V2ControlError("scope_too_large")
        return page

    def _scoped_public_page(
        self,
        snapshot: _ProjectedSnapshot,
        scope: Mapping[str, Any],
        items: Sequence[Mapping[str, Any]],
        total: int,
        next_cursor: str | None,
        expires_at: str | None,
        catalog_id: str,
        catalog_complete: bool,
    ) -> dict[str, Any]:
        """Build a scoped page without publishing any private capability."""
        return {
            "schema_version": FULL_CONTROL_SCHEMA_VERSION,
            "control_protocol": FULL_CONTROL_V2,
            "game_id": self.game_id,
            "agent_id": self.agent_id,
            "state_revision": _thaw(snapshot.state_revision),
            "page": {
                "section": "legal_actions",
                "scope": dict(scope),
                "items": [_thaw(item) for item in items],
                "total_items": total,
                "next_cursor": next_cursor,
                "cursor_expires_at": expires_at,
                "catalog_id": catalog_id,
                "catalog_complete": catalog_complete,
            },
        }

    def _mac(self, prefix: str, *parts: object) -> str:
        message = b"\x00".join(str(part).encode("utf-8") for part in parts)
        digest = hmac.new(self._secret, message, hashlib.sha256).hexdigest()[:32]
        return f"{prefix}_{digest}"

    def _entity_id(self, kind: str, raw_ref: str) -> str:
        return self._mac(kind, "entity", kind, raw_ref)

    def _tile_id(self, native_index: int) -> str:
        return self._mac("tile", "entity", "tile", native_index)

    def _tech_id(self, native_index: int) -> str:
        return self._mac("tech", "entity", "tech", native_index)

    def _government_id(self, native_index: int) -> str:
        return self._mac(
            "government", "entity", "government", native_index,
        )

    def _team_id(self, native_index: int) -> str:
        return self._mac("team", "entity", "team", native_index)

    def _multiplier_id(self, native_index: int) -> str:
        return self._mac(
            "multiplier", "entity", "multiplier", native_index,
        )

    def _spaceship_slot_id(self, native_index: int) -> str:
        return self._mac(
            "spaceship_slot", "entity", "spaceship_structural", native_index,
        )

    def _spaceship_part_id(self, part: str, value: int) -> str:
        return self._mac(
            "spaceship_part", "entity", "spaceship_part", part, value,
        )

    def _production_id(self, kind: str, native_index: int) -> str:
        return self._mac("production", "entity", kind, native_index)

    def _public_build_choice(
        self, value: Mapping[str, Any], city_id: str,
    ) -> dict[str, Any]:
        def public_turn(turns: int | None) -> int | None:
            return None if turns in {None, _FC_INFINITY} else turns

        return {
            "city_id": city_id,
            "id": self._production_id(
                value["production_kind"], value["production_native_id"],
            ),
            "kind": value["production_kind"],
            "name": value["production_name"],
            "can_queue": value["can_queue"],
            "can_build_now": value["can_build_now"],
            "cost": {
                "shields": value["shield_cost"],
                "shield_stock_after_change": value[
                    "shield_stock_after_change"
                ],
                "turns": public_turn(value["turns"]),
                "turns_with_stock": public_turn(value["turns_with_stock"]),
            },
            "upkeep": dict(value["upkeep"]),
            "happy_cost": value["happy_cost"],
            "unit": (
                dict(value["unit_stats"])
                if value["unit_stats"] is not None else None
            ),
            "building": (
                dict(value["building_stats"])
                if value["building_stats"] is not None else None
            ),
        }

    def _unit_type_id(self, native_index: int) -> str:
        return self._mac("unit_type", "entity", "unit_type", native_index)

    def _research_unlock_id(self, kind: str, native_index: int) -> str:
        if kind == "unit":
            return self._unit_type_id(native_index)
        if kind == "building":
            return self._production_id("improvement", native_index)
        if kind == "government":
            return self._government_id(native_index)
        return self._mac("action_rule", "entity", "action", native_index)

    def _specialist_id(self, native_index: int) -> str:
        return self._mac("specialist", "entity", "specialist", native_index)

    def _action_variant_id(
        self, native_rule: str, operation: str, native_target: int,
    ) -> str:
        return self._mac(
            "variant", "unit-action", native_rule, operation, native_target,
        )

    def _extra_id(self, native_index: int) -> str:
        return self._mac("extra", "entity", "extra", native_index)

    def _activity_id(self, activity: str, native_extra: int) -> str:
        return self._mac("activity", "entity", activity, native_extra)

    def _city_worker_task_id(self, city_ref: str, native_tile: int) -> str:
        return self._mac(
            "city_worker_task", "entity", city_ref, native_tile,
        )

    def _snapshot(self, observation: Mapping[str, Any]) -> _ProjectedSnapshot:
        try:
            if not isinstance(observation, Mapping) or set(observation) != {
                "generation", "native_revision", "rows",
            }:
                _fail()
            generation = observation["generation"]
            native_revision = observation["native_revision"]
            rows = observation["rows"]
            if (
                isinstance(generation, bool)
                or not isinstance(generation, int)
                or generation != self.generation
                or isinstance(native_revision, bool)
                or not isinstance(native_revision, int)
                or native_revision < 1
                or native_revision > _U64_MAX
                or not isinstance(rows, tuple)
                or not (1 <= len(rows) <= MAX_BUNDLED_ROWS)
            ):
                _fail()

            row_bytes: list[bytes] = []
            previous: bytes | None = None
            for row in rows:
                if not isinstance(row, str):
                    _fail()
                try:
                    encoded = row.encode("ascii", "strict")
                except UnicodeEncodeError:
                    _fail()
                if not (1 <= len(encoded) <= MAX_NATIVE_ROW_BYTES):
                    _fail()
                if previous is not None and previous >= encoded:
                    _fail()
                previous = encoded
                row_bytes.append(encoded)
            row_digest = hashlib.sha256(b"\x00".join(row_bytes)).hexdigest()
            cached = self._snapshots.get(native_revision)
            if cached is not None:
                if not hmac.compare_digest(cached.row_digest, row_digest):
                    _fail()
                return cached
            # Native revisions are monotonic for one exact sidecar generation.
            # An older revision is usable only while its immutable snapshot is
            # still cached; rejecting an evicted regression keeps lifetime
            # bookkeeping bounded without weakening contradiction detection.
            if native_revision <= self._highest_native_revision:
                _fail()

            parsed = self._parse_rows(rows)
            projected = self._project(native_revision, row_digest, parsed)
            self._install(projected)
            self._highest_native_revision = native_revision
            return projected
        except _ObservationError as exc:
            raise V2ControlError("internal_error") from exc
        except V2ControlError:
            raise
        except Exception as exc:
            # Anything the projector raises that is not an _ObservationError --
            # a KeyError from a scope migration, an arithmetic fault on a
            # field that changed shape -- used to escape state_page entirely:
            # no public envelope, no error code, nothing naming the row family.
            # Failing closed with a code is the contract; failing open with a
            # traceback is not a fail-closed at all.
            raise V2ControlError("internal_error") from exc

    def _parse_rows(self, rows: Sequence[str]) -> _ParsedObservation:
        buckets: dict[str, list[dict[str, Any]]] = {
            name: [] for name in (
                "meta", "pregame", "player", "governance", "government", "research",
                "multiplier", "spaceship", "spaceship_structural",
                "research_tech", "research_graph", "research_edge",
                "research_unlock", "diplomacy", "diplomacy_intel",
                "diplomacy_clause", "tile",
                "infrastructure_extra",
                "city", "unit", "unit_route", "unit_route_step", "city_site",
                "city_tile", "city_specialist", "city_worklist",
                "city_build_choice", "city_improvement", "city_rally",
                "city_governor", "city_worker_task",
                "tombstone",
                "vote", "chat",
                "action",
            )
        }
        for row in rows:
            parts = row.split(" ")
            kind = parts[0]
            if kind not in buckets or len(parts) < 2:
                _fail()
            pairs: list[tuple[str, str]] = []
            for token in parts[1:]:
                if token.count("=") != 1:
                    _fail()
                key, raw = token.split("=", 1)
                if not key or not raw:
                    _fail()
                pairs.append((key, raw))
            keys = tuple(key for key, _ in pairs)
            schema_key = kind
            if kind == "unit":
                if len(pairs) < 2 or pairs[1][0] != "scope":
                    _fail()
                scope = pairs[1][1]
                schema_key = f"unit_{scope}"
            if schema_key not in _ROW_FIELDS or keys != _ROW_FIELDS[schema_key]:
                _fail()
            raw_fields = dict(pairs)
            buckets[kind].append(self._parse_row(kind, raw_fields))

        if (
            len(buckets["meta"]) != 1
            or len(buckets["pregame"]) > 1
            or len(buckets["player"]) > 1
            or len(buckets["governance"]) > 1
            or len(buckets["spaceship"]) > 1
            or len(buckets["research"]) > 1
        ):
            _fail()
        pregame = buckets["pregame"][0] if buckets["pregame"] else None
        player = buckets["player"][0] if buckets["player"] else None
        governance = (
            buckets["governance"][0] if buckets["governance"] else None
        )
        research = buckets["research"][0] if buckets["research"] else None
        spaceship = (
            buckets["spaceship"][0] if buckets["spaceship"] else None
        )
        state = buckets["meta"][0]["state"]
        if state == "preparing":
            if pregame is None or player is not None or governance is not None \
                    or research is not None or spaceship is not None \
                    or any(buckets[name] for name in (
                        "government", "multiplier", "spaceship_structural",
                        "research_tech", "research_graph", "research_edge",
                        "research_unlock", "diplomacy", "diplomacy_intel",
                        "diplomacy_clause",
                        "tile", "infrastructure_extra", "city", "unit",
                        "unit_route", "unit_route_step",
                        "city_site", "city_tile", "city_specialist",
                        "city_worklist", "city_build_choice",
                        "city_improvement", "city_rally", "city_governor",
                        "city_worker_task", "tombstone",
                    )):
                _fail()
        elif pregame is not None:
            _fail()
        if (
            (research is not None or buckets["research_tech"]
             or buckets["research_graph"] or buckets["research_edge"]
             or buckets["research_unlock"])
            and player is None
        ) or (
            (governance is not None or buckets["government"])
            and player is None
        ) or ((governance is None) is not (player is None)):
            _fail()
        self._validate_cross_links(buckets, player)
        return _ParsedObservation(
            meta=_freeze(buckets["meta"][0]),
            pregame=_freeze(pregame) if pregame is not None else None,
            votes=tuple(_freeze(item) for item in buckets["vote"]),
            player=_freeze(player) if player is not None else None,
            governance=(
                _freeze(governance) if governance is not None else None
            ),
            governments=tuple(
                _freeze(item) for item in buckets["government"]
            ),
            multipliers=tuple(
                _freeze(item) for item in buckets["multiplier"]
            ),
            spaceship=(
                _freeze(spaceship) if spaceship is not None else None
            ),
            spaceship_structurals=tuple(
                _freeze(item) for item in buckets["spaceship_structural"]
            ),
            research=_freeze(research) if research is not None else None,
            research_techs=tuple(_freeze(item) for item in buckets["research_tech"]),
            research_graph=tuple(
                _freeze(item) for item in buckets["research_graph"]
            ),
            research_edges=tuple(
                _freeze(item) for item in buckets["research_edge"]
            ),
            research_unlocks=tuple(
                _freeze(item) for item in buckets["research_unlock"]
            ),
            diplomacy=tuple(_freeze(item) for item in buckets["diplomacy"]),
            diplomacy_intel=tuple(
                _freeze(item) for item in buckets["diplomacy_intel"]
            ),
            diplomacy_clauses=tuple(
                _freeze(item) for item in buckets["diplomacy_clause"]
            ),
            tiles=tuple(_freeze(item) for item in buckets["tile"]),
            infrastructure_extras=tuple(
                _freeze(item) for item in buckets["infrastructure_extra"]
            ),
            cities=tuple(_freeze(item) for item in buckets["city"]),
            city_sites=tuple(
                _freeze(item) for item in buckets["city_site"]
            ),
            city_tiles=tuple(
                _freeze(item) for item in buckets["city_tile"]
            ),
            city_worker_tasks=tuple(
                _freeze(item) for item in buckets["city_worker_task"]
            ),
            city_specialists=tuple(
                _freeze(item) for item in buckets["city_specialist"]
            ),
            city_worklists=tuple(
                _freeze(item) for item in buckets["city_worklist"]
            ),
            city_build_choices=tuple(
                _freeze(item) for item in buckets["city_build_choice"]
            ),
            city_improvements=tuple(
                _freeze(item) for item in buckets["city_improvement"]
            ),
            city_rallies=tuple(
                _freeze(item) for item in buckets["city_rally"]
            ),
            city_governors=tuple(
                _freeze(item) for item in buckets["city_governor"]
            ),
            units=tuple(_freeze(item) for item in buckets["unit"]),
            unit_routes=tuple(
                _freeze(item) for item in buckets["unit_route"]
            ),
            tombstones=tuple(_freeze(item) for item in buckets["tombstone"]),
            chats=tuple(_freeze(item) for item in buckets["chat"]),
            actions=tuple(_freeze(item) for item in buckets["action"]),
        )

    def _parse_row(self, kind: str, raw: Mapping[str, str]) -> dict[str, Any]:
        if kind == "meta":
            if (
                raw["state"] not in _CLIENT_STATES
                or raw["cache"] != "human-client"
                or raw["phase_mode"] not in _PHASE_MODES
                or raw["topology"] not in _MAP_TOPOLOGIES
            ):
                _fail()
            phase = _integer(
                raw["phase"], unsigned=True, maximum=_I32_MAX,
            )
            phase_count = _integer(
                raw["phase_count"], unsigned=True, maximum=_I32_MAX,
            )
            if phase_count == 0 or phase >= phase_count:
                _fail()
            map_width = _integer(
                raw["map_width"], unsigned=True, maximum=_I32_MAX,
            )
            map_height = _integer(
                raw["map_height"], unsigned=True, maximum=_I32_MAX,
            )
            if (
                map_width == 0 or map_height == 0
                or map_width > _I32_MAX // map_height
            ):
                _fail()
            known_tile_count = _integer(
                raw["known_tile_count"], unsigned=True,
                maximum=map_width * map_height,
            )
            return {
                "state": raw["state"],
                "turn": _integer(raw["turn"], unsigned=True, maximum=_I32_MAX),
                "phase": phase,
                "phase_mode": raw["phase_mode"],
                "phase_count": phase_count,
                "active_phase": _boolean(raw["active_phase"]),
                "phase_ready": _boolean(raw["phase_ready"]),
                "map_width": map_width,
                "map_height": map_height,
                "topology": raw["topology"],
                "wrap_x": _boolean(raw["wrap_x"]),
                "wrap_y": _boolean(raw["wrap_y"]),
                "known_tile_count": known_tile_count,
            }
        if kind == "pregame":
            sex = raw["sex"]
            if sex not in {"male", "female"}:
                _fail()
            return {
                "ref": raw["ref"],
                "parsed_ref": _entity_ref(raw["ref"], "p"),
                "leader": _text(raw["leader"]),
                "nation": _text(raw["nation"]),
                "sex": sex,
                "style": _text(raw["style"]),
                "ready": _boolean(raw["ready"]),
                "nation_choices": _integer(
                    raw["nation_choices"], unsigned=True,
                    maximum=MAX_NATIVE_STATE_SCOPE_ROWS,
                ),
                "style_choices": _integer(
                    raw["style_choices"], unsigned=True,
                    maximum=MAX_NATIVE_STATE_SCOPE_ROWS,
                ),
                "team_choices": _integer(
                    raw["team_choices"], unsigned=True,
                    maximum=MAX_NATIVE_STATE_SCOPE_ROWS,
                ),
            }
        if kind == "pregame_nation":
            return {
                "native_id": _integer(
                    raw["id"], unsigned=True, maximum=_I32_MAX,
                ),
                "name": _text(raw["name"]),
                "default_style_native_id": _integer(
                    raw["default_style"], unsigned=True, maximum=_I32_MAX,
                ),
            }
        if kind == "pregame_style":
            return {
                "native_id": _integer(
                    raw["id"], unsigned=True, maximum=_I32_MAX,
                ),
                "name": _text(raw["name"]),
            }
        if kind == "pregame_team":
            member_count = _integer(
                raw["member_count"], unsigned=True,
                maximum=MAX_NATIVE_STATE_SCOPE_ROWS,
            )
            occupied = _boolean(raw["occupied"])
            if occupied is not (member_count > 0):
                _fail()
            return {
                "native_id": _integer(
                    raw["id"], unsigned=True, maximum=_I32_MAX,
                ),
                "name": _text(raw["name"]),
                "selected": _boolean(raw["selected"]),
                "occupied": occupied,
                "member_count": member_count,
            }
        if kind == "pregame_team_member":
            player_ref = raw["player"]
            return {
                "native_team_id": _integer(
                    raw["team"], unsigned=True, maximum=_I32_MAX,
                ),
                "player_ref": player_ref,
                "parsed_player_ref": _entity_ref(player_ref, "p"),
                "leader_name": _text(raw["leader"]),
            }
        if kind == "vote":
            yes = _integer(raw["yes"], unsigned=True, maximum=_I32_MAX)
            no = _integer(raw["no"], unsigned=True, maximum=_I32_MAX)
            abstain = _integer(
                raw["abstain"], unsigned=True, maximum=_I32_MAX,
            )
            num_voters = _integer(
                raw["num_voters"], unsigned=True, maximum=_I32_MAX,
            )
            percent_required = _integer(
                raw["percent_required"], unsigned=True, maximum=100,
            )
            current_vote = raw["current_vote"]
            can_vote = _boolean(raw["can_vote"])
            status = raw["status"]
            outcome_turn = _integer(raw["outcome_turn"])
            outcome_phase = _integer(raw["outcome_phase"])
            if yes + no + abstain > num_voters or current_vote not in {
                "none", "yes", "no", "abstain",
            } or status not in _VOTE_STATUSES or (
                outcome_turn < -1 or outcome_phase < -1
            ) or (
                status == "active"
                and (outcome_turn != -1 or outcome_phase != -1)
            ) or (
                status != "active"
                and (can_vote or outcome_turn < 0 or outcome_phase < 0)
            ):
                _fail()
            return {
                "native_vote_no": _integer(
                    raw["vote_no"], unsigned=True, maximum=_I32_MAX,
                ),
                "caller": _text(raw["caller"]),
                "description": _text(raw["description"]),
                "yes": yes,
                "no": no,
                "abstain": abstain,
                "num_voters": num_voters,
                "percent_required": percent_required,
                "team_only": _boolean(raw["team_only"]),
                "current_vote": current_vote,
                "can_vote": can_vote,
                "status": status,
                "outcome_turn": (
                    None if outcome_turn == -1 else outcome_turn
                ),
                "outcome_phase": (
                    None if outcome_phase == -1 else outcome_phase
                ),
            }
        if kind == "player":
            ref = _entity_ref(raw["ref"], "p")
            tax = _integer(raw["tax"], unsigned=True, maximum=100)
            science = _integer(raw["science"], unsigned=True, maximum=100)
            luxury = _integer(raw["luxury"], unsigned=True, maximum=100)
            if tax + science + luxury != 100:
                _fail()
            max_rate = _integer(raw["max_rate"], unsigned=True, maximum=100)
            changeable_tax = _boolean(raw["changeable_tax"])
            # `max(...) > max_rate` is safe only because of a ruleset-wide
            # property, not because the server enforces it continuously.
            # player_limit_to_max_rates() (server/plrhand.c) is called ONLY on
            # government transitions, never when an EFT_MAX_RATES effect's
            # requirements change for any other reason, and v2_player_max_rate
            # (protocol_v2.c) mirrors get_player_maxrate and reports the live
            # effect value.  Every shipped ruleset (civ2civ3, classic, sandbox,
            # civ1, civ2, multiplayer, alien, granularity, goldkeep, stub)
            # scopes Max_Rates on "Gov"/"Player" alone, so the invariant holds.
            # A ruleset that tied Max_Rates to a building, wonder or tech could
            # drop the effect below a rate already set -- and this would then
            # reject every observation for the rest of the game.  If custom
            # rulesets are ever admitted, weaken this to a recorded anomaly the
            # same way the city citizen-count identity was.
            if max_rate < 34 or max(tax, science, luxury) > max_rate:
                _fail()
            return {
                "ref": raw["ref"], "parsed_ref": ref,
                "name": _text(raw["name"]), "nation": _text(raw["nation"]),
                "government": _text(raw["government"]),
                "gold": _integer(raw["gold"]), "tax": tax, "science": science,
                "luxury": luxury, "alive": _boolean(raw["alive"]),
                "phase_done": _boolean(raw["phase_done"]),
                "changeable_tax": changeable_tax,
                "max_rate": max_rate,
                "infrastructure_enabled": _boolean(
                    raw["infrastructure_enabled"],
                ),
                "infrastructure_points": _integer(
                    raw["infrastructure_points"], unsigned=True,
                    maximum=_I32_MAX,
                ),
            }
        if kind == "governance":
            status = raw["status"]
            method = raw["method"]
            if status not in _GOVERNMENT_STATUSES or method not in (
                _REVOLUTION_METHODS
            ):
                _fail()
            current_id = _integer(
                raw["current_id"], unsigned=True, maximum=_I32_MAX,
            )
            target_id = _integer(raw["target_id"])
            during_id = _integer(
                raw["during_id"], unsigned=True, maximum=_I32_MAX,
            )
            finish_turn = _integer(raw["finish_turn"])
            turns_remaining = _integer(
                raw["turns_remaining"], unsigned=True, maximum=20,
            )
            max_turns = _integer(raw["max_turns"])
            choices_count = _integer(
                raw["choices_count"], unsigned=True,
                maximum=MAX_GOVERNMENTS,
            )
            if (
                target_id < -1
                or target_id > _I32_MAX
                or finish_turn < -1
                or finish_turn > _I32_MAX
                or max_turns < -1
                or max_turns > 20
                or choices_count < 1
            ):
                _fail()
            return {
                "current_native_id": current_id,
                "target_native_id": target_id,
                "during_native_id": during_id,
                "status": status,
                "finish_turn": finish_turn,
                "turns_remaining": turns_remaining,
                "method": method,
                "max_turns": None if max_turns == -1 else max_turns,
                "untargeted_allowed": _boolean(raw["untargeted_allowed"]),
                "no_anarchy": _boolean(raw["no_anarchy"]),
                "can_revolution": _boolean(raw["can_revolution"]),
                "choices_count": choices_count,
            }
        if kind == "government":
            return {
                "native_id": _integer(
                    raw["id"], unsigned=True, maximum=_I32_MAX,
                ),
                "name": _text(raw["name"]),
                "current": _boolean(raw["current"]),
                "target": _boolean(raw["target"]),
                "during": _boolean(raw["during"]),
                "can_change": _boolean(raw["can_change"]),
            }
        if kind == "multiplier":
            values = {
                "native_id": _integer(
                    raw["id"], unsigned=True, maximum=MAX_MULTIPLIERS - 1,
                ),
                "name": _text(raw["name"]),
                "value": _integer(raw["value"]),
                "target": _integer(raw["target"]),
                "start": _integer(raw["start"]),
                "stop": _integer(raw["stop"]),
                "step": _integer(raw["step"]),
                "minimum_turns": _integer(
                    raw["minimum_turns"], unsigned=True, maximum=_I32_MAX,
                ),
                "changed_turn": _integer(
                    raw["changed_turn"], unsigned=True, maximum=_I32_MAX,
                ),
                "can_change": _boolean(raw["can_change"]),
                "choice_count": _integer(
                    raw["choice_count"], unsigned=True,
                    maximum=MAX_MULTIPLIER_CHOICES,
                ),
            }
            start = values["start"]
            stop = values["stop"]
            step = values["step"]
            if (
                not all(-_I32_MAX - 1 <= values[key] <= _I32_MAX for key in (
                    "value", "target", "start", "stop", "step",
                ))
                or step <= 0 or stop < start
                or (stop - start) % step != 0
                or values["choice_count"] != (stop - start) // step + 1
                or values["choice_count"] < 1
                or not all(
                    start <= values[key] <= stop
                    and (values[key] - start) % step == 0
                    for key in ("value", "target")
                )
            ):
                _fail()
            return values
        if kind == "spaceship":
            state = raw["state"]
            if state not in _SPACESHIP_STATES:
                _fail()
            values = {
                "state": state,
                "structurals": _integer(
                    raw["structurals"], unsigned=True, maximum=32,
                ),
                "structurals_placed": _integer(
                    raw["structurals_placed"], unsigned=True, maximum=32,
                ),
                "components": _integer(
                    raw["components"], unsigned=True, maximum=16,
                ),
                "fuel": _integer(raw["fuel"], unsigned=True, maximum=8),
                "propulsion": _integer(
                    raw["propulsion"], unsigned=True, maximum=8,
                ),
                "modules": _integer(
                    raw["modules"], unsigned=True, maximum=12,
                ),
                "habitation": _integer(
                    raw["habitation"], unsigned=True, maximum=4,
                ),
                "life_support": _integer(
                    raw["life_support"], unsigned=True, maximum=4,
                ),
                "solar_panels": _integer(
                    raw["solar_panels"], unsigned=True, maximum=4,
                ),
                "launch_year": _integer(raw["launch_year"]),
                "population": _integer(
                    raw["population"], unsigned=True, maximum=_I32_MAX,
                ),
                "mass": _integer(raw["mass"], unsigned=True, maximum=_I32_MAX),
                "support_permille": _integer(
                    raw["support_permille"], unsigned=True,
                    maximum=_I32_MAX,
                ),
                "energy_permille": _integer(
                    raw["energy_permille"], unsigned=True,
                    maximum=_I32_MAX,
                ),
                "success_permille": _integer(
                    raw["success_permille"], unsigned=True,
                    maximum=_I32_MAX,
                ),
                "travel_time_millis": _integer(
                    raw["travel_time_millis"], unsigned=True,
                    maximum=_I32_MAX,
                ),
                "has_capital": _boolean(raw["has_capital"]),
                "can_launch": _boolean(raw["can_launch"]),
            }
            if (
                values["structurals_placed"] > values["structurals"]
                or values["fuel"] + values["propulsion"]
                   > values["components"]
                or values["habitation"] + values["life_support"]
                   + values["solar_panels"] > values["modules"]
                or values["can_launch"] and (
                    state != "started" or not values["has_capital"]
                    or values["success_permille"] == 0
                )
            ):
                _fail()
            return values
        if kind == "spaceship_structural":
            slot = _integer(raw["slot"], unsigned=True, maximum=31)
            required = _integer(raw["required_slot"])
            if required < -1 or required > 31 or (slot == 0) is not (required == -1):
                _fail()
            return {
                "native_slot": slot,
                "x": _integer(raw["x"]),
                "y": _integer(raw["y"]),
                "required_native_slot": required,
                "placed": _boolean(raw["placed"]),
                "required_connected": _boolean(raw["required_connected"]),
                "can_place": _boolean(raw["can_place"]),
            }
        if kind == "research":
            values = {
                "techs": _integer(raw["techs"], unsigned=True),
                "future": _integer(raw["future"], unsigned=True),
                "target": _text(raw["target"]),
                "target_native_id": _integer(
                    raw["target_id"], unsigned=True, maximum=_I32_MAX,
                ),
                "goal": _text(raw["goal"]),
                "goal_native_id": _integer(
                    raw["goal_id"], unsigned=True, maximum=_I32_MAX,
                ),
                "bulbs": _integer(raw["bulbs"]),
                "cost": _integer(raw["cost"]),
                "output": _integer(raw["output"]),
                "choices_count": _integer(
                    raw["choices_count"], unsigned=True,
                    maximum=MAX_NATIVE_ROWS,
                ),
                "choices_digest": raw["choices_digest"],
            }
            if _FNV1A64_DIGEST.fullmatch(
                values["choices_digest"],
            ) is None:
                _fail()
            return values
        if kind == "research_tech":
            if raw["state"] not in _RESEARCH_STATES:
                _fail()
            return {
                "native_id": _integer(raw["id"], unsigned=True, maximum=_I32_MAX),
                "name": _text(raw["name"]), "state": raw["state"],
                "can_target": _boolean(raw["can_target"]),
                "can_goal": _boolean(raw["can_goal"]),
            }
        if kind == "research_graph":
            next_step = _integer(raw["next_step"])
            unknown = _integer(raw["unknown_prerequisites"])
            path_cost = _integer(raw["path_cost"])
            reachable = _boolean(raw["reachable"])
            if (
                next_step < -1 or next_step > _I32_MAX
                or unknown < -1 or unknown > _I32_MAX
                or path_cost < -1 or path_cost > _I32_MAX
                or reachable is not (unknown >= 0 and path_cost >= 0)
            ):
                _fail()
            return {
                "native_id": _integer(
                    raw["id"], unsigned=True, maximum=_I32_MAX,
                ),
                "name": _text(raw["name"]),
                "reachable": reachable,
                "next_step_native_id": next_step,
                "unknown_prerequisites": (
                    unknown if reachable else None
                ),
                "path_cost": path_cost if reachable else None,
            }
        if kind == "research_edge":
            if raw["kind"] not in _RESEARCH_EDGE_KINDS:
                _fail()
            return {
                "tech_native_id": _integer(
                    raw["tech"], unsigned=True, maximum=_I32_MAX,
                ),
                "prerequisite_native_id": _integer(
                    raw["prerequisite"], unsigned=True, maximum=_I32_MAX,
                ),
                "kind": raw["kind"],
            }
        if kind == "research_unlock":
            unlock_kind = raw["kind"]
            scope = raw["scope"]
            if (
                unlock_kind not in _RESEARCH_UNLOCK_KINDS
                or scope not in _RESEARCH_UNLOCK_SCOPES
                or unlock_kind in {"unit", "building"} and scope != "build"
                or unlock_kind == "government" and scope != "change"
                or unlock_kind == "action"
                   and scope not in {"actor", "target", "both"}
            ):
                _fail()
            return {
                "tech_native_id": _integer(
                    raw["tech"], unsigned=True, maximum=_I32_MAX,
                ),
                "kind": unlock_kind,
                "native_id": _integer(
                    raw["native_id"], unsigned=True, maximum=_I32_MAX,
                ),
                "name": _text(raw["name"]),
                "scope": scope,
            }
        if kind == "diplomacy":
            state = _text(raw["state"])
            cancel_reason = raw["cancel_reason"]
            intel_level = raw["intel_level"]
            controller = raw["controller"]
            if (
                state not in _DIPLOMACY_STATES
                or cancel_reason not in _DIPLOMACY_CANCEL_REASONS
                or intel_level not in _INTEL_LEVELS
                or controller not in _PLAYER_CONTROLLERS
            ):
                _fail()
            team_native_id = _integer(raw["team"])
            score = _integer(raw["score"])
            gold = _integer(raw["gold"])
            government = _text(raw["government"])
            if (
                team_native_id < -1 or team_native_id > _I32_MAX
                or score < -1 or score > _I32_MAX
                or gold < -1 or gold > _I32_MAX
                or intel_level == "none"
                   and (score != -1 or gold != -1 or government != "unknown")
                or intel_level != "none"
                   and (gold < 0 or government == "unknown")
            ):
                _fail()
            values = {
                "other_ref": raw["other"],
                "parsed_ref": _entity_ref(raw["other"], "p"),
                "name": _text(raw["name"]),
                "nation": _text(raw["nation"]), "state": state,
                "contact": _integer(
                    raw["contact"], unsigned=True, maximum=100,
                ),
                "alive": _boolean(raw["alive"]),
                "turns_left": _integer(
                    raw["turns_left"], unsigned=True, maximum=127,
                ),
                "can_meet": _boolean(raw["can_meet"]),
                "meeting": _boolean(raw["meeting"]),
                "generation": _integer(raw["generation"], unsigned=True),
                "self_accepted": _boolean(raw["self_accepted"]),
                "other_accepted": _boolean(raw["other_accepted"]),
                "clause_count": _integer(
                    raw["clause_count"], unsigned=True,
                    maximum=_I32_MAX,
                ),
                "clauses_digest": raw["clauses_digest"],
                "intel_level": intel_level,
                "team_native_id": team_native_id,
                "team_name": _text(raw["team_name"]),
                "same_team": _boolean(raw["same_team"]),
                "controller": controller,
                "connected": _boolean(raw["connected"]),
                "score": None if score == -1 else score,
                "gold": None if gold == -1 else gold,
                "government": None if government == "unknown" else government,
                "has_embassy": _boolean(raw["has_embassy"]),
                "other_has_embassy": _boolean(
                    raw["other_has_embassy"],
                ),
                "gives_vision": _boolean(raw["gives_vision"]),
                "receives_vision": _boolean(raw["receives_vision"]),
                "gives_shared_tiles": _boolean(raw["gives_shared_tiles"]),
                "receives_shared_tiles": _boolean(
                    raw["receives_shared_tiles"],
                ),
                "can_cancel": _boolean(raw["can_cancel"]),
                "cancel_reason": cancel_reason,
            }
            if _FNV1A64_DIGEST.fullmatch(values["clauses_digest"]) is None:
                _fail()
            if values["can_cancel"] is not (
                values["cancel_reason"] == "allowed"
            ):
                _fail()
            return values
        if kind == "diplomacy_intel":
            known_count = _integer(
                raw["known_count"], unsigned=True, maximum=_I32_MAX,
            )
            if raw["known_ids"] == "-":
                known_ids: tuple[int, ...] = ()
            else:
                parts = raw["known_ids"].split(",")
                if any(_UNSIGNED.fullmatch(part) is None for part in parts):
                    _fail()
                known_ids = tuple(
                    _integer(part, unsigned=True, maximum=_I32_MAX)
                    for part in parts
                )
            digest = raw["known_digest"]
            if (
                known_ids != tuple(sorted(set(known_ids)))
                or known_count != len(known_ids)
                or _FNV1A64_DIGEST.fullmatch(digest) is None
                or digest != _known_techs_digest(known_ids)
            ):
                _fail()
            return {
                "other_ref": raw["other"],
                "parsed_ref": _entity_ref(raw["other"], "p"),
                "tax": _integer(raw["tax"], unsigned=True, maximum=100),
                "science": _integer(
                    raw["science"], unsigned=True, maximum=100,
                ),
                "luxury": _integer(
                    raw["luxury"], unsigned=True, maximum=100,
                ),
                "culture": _integer(
                    raw["culture"], unsigned=True, maximum=_I32_MAX,
                ),
                "research_native_id": _integer(
                    raw["research_id"], unsigned=True, maximum=_I32_MAX,
                ),
                "research_name": _text(raw["research_name"]),
                "bulbs": _integer(raw["bulbs"]),
                "cost": _integer(raw["cost"]),
                "known_native_ids": known_ids,
                "known_digest": digest,
            }
        if kind == "diplomacy_clause":
            clause_type = _text(raw["type"])
            value_kind = raw["value_kind"]
            if (
                clause_type not in _DIPLOMACY_CLAUSE_TYPES
                or value_kind not in _DIPLOMACY_CLAUSE_VALUE_KINDS
            ):
                _fail()
            return {
                "other_ref": raw["other"],
                "generation": _integer(raw["generation"], unsigned=True),
                "position": _integer(
                    raw["position"], unsigned=True,
                    maximum=MAX_NATIVE_STATE_SCOPE_ROWS,
                ),
                "giver_ref": raw["giver"],
                "giver_parsed_ref": _entity_ref(raw["giver"], "p"),
                "clause_type": clause_type,
                "native_type": _DIPLOMACY_CLAUSE_NATIVE_TYPES[clause_type],
                "value_kind": value_kind,
                "native_value": _integer(
                    raw["value"], unsigned=True, maximum=_I32_MAX,
                ),
                "name": _text(raw["name"]),
            }
        if kind in {"tile", "tile_local"}:
            known = _integer(raw["known"], unsigned=True)
            if known not in {0, 1, 2}:
                _fail()
            terrain = _text(raw["terrain"])
            placing_extra = _integer(raw["placing_extra"])
            placing_name = _text(raw["placing_extra_name"])
            placing_turns = _integer(
                raw["placing_turns"], unsigned=True, maximum=_I32_MAX,
            )
            placing_time = _integer(raw["placing_time"])
            if (
                placing_extra < -1 or placing_extra > _I32_MAX
                or placing_time < -1 or placing_time > _I32_MAX
                or (placing_extra == -1) is not (placing_name == "none")
                or placing_extra == -1 and placing_turns != 0
                or placing_extra >= 0 and placing_turns == 0
            ):
                _fail()
            if known == 0:
                if (
                    raw["terrain"] != "unknown" or raw["owner"] != "none"
                    or placing_extra != -1 or placing_name != "none"
                    or placing_turns != 0 or placing_time != -1
                ):
                    _fail()
                terrain = None
                owner_ref = None
                placing_name = None
            else:
                owner_ref = None if raw["owner"] == "none" else raw["owner"]
                if owner_ref is not None:
                    _entity_ref(owner_ref, "p")
            values: dict[str, Any] = {
                "native_index": _integer(raw["index"], unsigned=True, maximum=_I32_MAX),
                "x": _integer(raw["x"], unsigned=True, maximum=_I32_MAX),
                "y": _integer(raw["y"], unsigned=True, maximum=_I32_MAX),
                "known": known, "terrain": terrain,
                "owner_ref": owner_ref,
                "placing_extra": placing_extra,
                "placing_extra_name": placing_name,
                "placing_turns": placing_turns,
                "placing_time": placing_time,
            }
            if kind == "tile_local":
                resource_extra = _i32(raw["resource_extra"])
                resource_name = _text(raw["resource_name"])
                has_label = _boolean(raw["has_label"])
                label = _text(raw["label"])
                yields = {
                    name: _i32(raw[name])
                    for name in ("food", "shields", "trade")
                }
                if (
                    resource_extra < -1
                    or (resource_extra == -1)
                       is not (resource_name == "none")
                    or not has_label and label != "none"
                    or known == 0 and (
                        resource_extra != -1 or resource_name != "none"
                        or has_label or label != "none"
                        or set(yields.values()) != {-1}
                    )
                ):
                    _fail()
                values.update({
                    "resource_extra": resource_extra,
                    "resource_name": (
                        None if resource_extra == -1 else resource_name
                    ),
                    "label": label if has_label else None,
                    "yields": yields if known != 0 else None,
                })
            return values
        if kind == "tile_extra":
            return {
                "native_tile": _integer(
                    raw["tile"], unsigned=True, maximum=_I32_MAX,
                ),
                "native_id": _integer(
                    raw["extra"], unsigned=True, maximum=_I32_MAX,
                ),
                "name": _text(raw["name"]),
                "cause_mask": _integer(
                    raw["cause_mask"], unsigned=True,
                    maximum=(1 << len(_EXTRA_CAUSE_TAGS)) - 1,
                ),
            }
        if kind == "infrastructure_extra":
            return {
                "native_id": _integer(
                    raw["id"], unsigned=True,
                    maximum=MAX_INFRASTRUCTURE_CHOICES - 1,
                ),
                "name": _text(raw["name"]),
                "cost": _integer(
                    raw["cost"], unsigned=True, maximum=_I32_MAX,
                ),
                "build_time": _integer(
                    raw["build_time"], unsigned=True, maximum=_I32_MAX,
                ),
                "build_time_factor": _integer(
                    raw["build_time_factor"], unsigned=True,
                    maximum=_I32_MAX,
                ),
            }
        if kind == "city":
            production_kind = raw["production_kind"]
            new_citizens = raw["new_citizens"]
            if (
                production_kind not in _BUILD_KINDS - {"none"}
                or new_citizens not in _NEW_CITIZENS
            ):
                _fail()
            food = _i32(raw["food"])
            shields = _i32(raw["shields"])
            trade = _i32(raw["trade"])
            citizen_counts = {
                name: _integer(
                    raw[f"citizen_{name}"], unsigned=True,
                    maximum=_I32_MAX,
                )
                for name in (
                    "happy", "content", "unhappy", "angry", "workers",
                    "specialists",
                )
            }
            food_stock = _integer(
                raw["food_stock"], unsigned=True, maximum=_I32_MAX,
            )
            granary_size = _integer(
                raw["granary_size"], unsigned=True, maximum=_I32_MAX,
            )
            growth_turns = _i32(raw["growth_turns"])
            pollution = _integer(
                raw["pollution"], unsigned=True, maximum=_I32_MAX,
            )
            outputs: dict[str, dict[str, int]] = {}
            for output in _CITY_OUTPUTS:
                metrics = {
                    "citizen_base": _i32(raw[f"{output}_citizen_base"]),
                    "net": _i32(raw[f"{output}_net"]),
                    "surplus": _i32(raw[f"{output}_surplus"]),
                    "usage": _i32(raw[f"{output}_usage"]),
                    "waste": _i32(raw[f"{output}_waste"]),
                    "unhappy_penalty": _i32(
                        raw[f"{output}_unhappy_penalty"],
                    ),
                }
                if (
                    any(
                        metrics[name] < 0 for name in (
                            "net", "usage", "waste", "unhappy_penalty",
                        )
                    )
                    or metrics["surplus"]
                       != metrics["net"] - metrics["usage"]
                ):
                    _fail()
                outputs[output] = metrics
            size = _integer(
                raw["size"], unsigned=True, maximum=_I32_MAX,
            )
            # Freeciv's mood counters describe only the citizens who are not
            # specialists: ``citizen_base_mood`` subtracts ``city_specialists``
            # before it distributes content, angry and unhappy, and the client
            # reassembles size as the mood total plus the normal specialists.
            # The native row reports ``workers`` as exactly
            # ``size - specialists``, so the mood counters total the workers,
            # not the size.  (Asserting ``size`` is what bricked turn 52: it
            # holds only while a city has no specialist at all.)
            #
            # Deliberately NOT bundle-fatal.  handle_city_info()
            # (client/packhand.c) treats a server/client citizen disagreement
            # as recoverable: it logs "%d citizens not equal %d city size" and
            # OVERRIDES with city_size_set(pcity, packet->size), leaving feel[]
            # and specialists[] alone.  After that self-heal this identity is
            # permanently false, and the city row is in every OBS bundle -- so
            # rejecting on it would fail the seat closed forever over a state
            # the native client explicitly keeps playing through.  Record it,
            # name it, and keep the seat usable.
            citizen_counts_consistent = sum(
                citizen_counts[name] for name in (
                    "happy", "content", "unhappy", "angry",
                )
            ) == citizen_counts["workers"]
            if (
                size == 0 or granary_size == 0
                or growth_turns > _FC_INFINITY
                or citizen_counts["workers"]
                   + citizen_counts["specialists"] != size
                or (food, shields, trade) != (
                    outputs["food"]["surplus"],
                    outputs["shield"]["surplus"],
                    outputs["trade"]["surplus"],
                )
            ):
                _fail()
            return {
                "ref": raw["ref"], "parsed_ref": _entity_ref(raw["ref"], "c"),
                "name": _text(raw["name"]),
                "native_tile": _integer(raw["tile"], unsigned=True, maximum=_I32_MAX),
                "x": _integer(raw["x"], unsigned=True, maximum=_I32_MAX),
                "y": _integer(raw["y"], unsigned=True, maximum=_I32_MAX),
                "size": size,
                "food": food,
                "shields": shields,
                "trade": trade,
                "production_kind": production_kind,
                "production_native_id": _integer(
                    raw["production_id"], unsigned=True, maximum=_I32_MAX,
                ),
                "production_name": _text(raw["production_name"]),
                "shield_stock": _integer(
                    raw["shield_stock"], unsigned=True, maximum=_I32_MAX,
                ),
                "shield_cost": _integer(
                    raw["shield_cost"], unsigned=True, maximum=_I32_MAX,
                ),
                "buy_cost": _integer(
                    raw["buy_cost"], unsigned=True, maximum=_I32_MAX,
                ),
                "can_buy": _boolean(raw["can_buy"]),
                "can_change": _boolean(raw["can_change"]),
                "citizen_tile_count": _integer(
                    raw["citizen_tile_count"], unsigned=True, maximum=91,
                ),
                "specialist_type_count": _integer(
                    raw["specialist_type_count"], unsigned=True, maximum=20,
                ),
                "worklist_length": _integer(
                    raw["worklist_length"], unsigned=True,
                    maximum=MAX_CITY_WORKLIST,
                ),
                "build_choice_count": _integer(
                    raw["build_choice_count"], unsigned=True,
                    maximum=MAX_CITY_BUILD_CHOICES,
                ),
                "improvement_count": _integer(
                    raw["improvement_count"], unsigned=True,
                    maximum=MAX_CITY_BUILD_CHOICES,
                ),
                "trade_route_count": _integer(
                    raw["trade_route_count"], unsigned=True,
                    maximum=MAX_CITY_TRADE_ROUTES,
                ),
                "trade_route_capacity": _integer(
                    raw["trade_route_capacity"], unsigned=True,
                    maximum=MAX_CITY_TRADE_ROUTES,
                ),
                "did_sell": _boolean(raw["did_sell"]),
                "allow_disband": _boolean(raw["allow_disband"]),
                "new_citizens": new_citizens,
                "options_conflict": _boolean(raw["options_conflict"]),
                "airlift_remaining": _integer(
                    raw["airlift_remaining"], unsigned=True,
                    maximum=_I32_MAX,
                ),
                "airlift_max": _integer(
                    raw["airlift_max"], unsigned=True, maximum=_I32_MAX,
                ),
                "governor_enabled": _boolean(raw["governor_enabled"]),
                "citizen_counts": citizen_counts,
                "citizen_counts_consistent": citizen_counts_consistent,
                "food_storage": {
                    "stock": food_stock,
                    "granary_size": granary_size,
                    "growth_turns": (
                        None if growth_turns == _FC_INFINITY
                        else growth_turns
                    ),
                },
                "pollution": pollution,
                "outputs": outputs,
            }
        if kind == "city_site":
            visibility = raw["visibility"]
            if visibility not in _CITY_SITE_VISIBILITIES:
                _fail()
            size = _integer(raw["size"], unsigned=True, maximum=_I32_MAX)
            if size == 0:
                _fail()
            return {
                "ref": raw["ref"],
                "parsed_ref": _entity_ref(raw["ref"], "c"),
                "owner_ref": raw["owner"],
                "owner_parsed_ref": _entity_ref(raw["owner"], "p"),
                "name": _text(raw["name"]),
                "native_tile": _integer(
                    raw["tile"], unsigned=True, maximum=_I32_MAX,
                ),
                "x": _integer(raw["x"], unsigned=True, maximum=_I32_MAX),
                "y": _integer(raw["y"], unsigned=True, maximum=_I32_MAX),
                "size": size,
                "visibility": visibility,
            }
        if kind == "city_tile":
            return {
                "city_ref": raw["city"],
                "city_parsed_ref": _entity_ref(raw["city"], "c"),
                "native_tile": _integer(
                    raw["tile"], unsigned=True, maximum=_I32_MAX,
                ),
                "worked": _boolean(raw["worked"]),
                "free_worked": _boolean(raw["free_worked"]),
                "can_work": _boolean(raw["can_work"]),
                "yields": _city_yields(raw),
            }
        if kind == "city_worker_task":
            activity = raw["activity"]
            target_extra = _integer(raw["target_extra"])
            target_extra_name = _text(raw["target_extra_name"])
            if (
                activity not in _CITY_WORKER_TASK_ACTIVITIES
                or target_extra < -1 or target_extra > _I32_MAX
                or (target_extra >= 0)
                   is not (
                       activity in _CITY_WORKER_TASK_TARGETED_ACTIVITIES
                   )
                or (target_extra == -1)
                   is not (target_extra_name == "none")
            ):
                _fail()
            return {
                "city_ref": raw["city"],
                "city_parsed_ref": _entity_ref(raw["city"], "c"),
                "native_tile": _integer(
                    raw["tile"], unsigned=True, maximum=_I32_MAX,
                ),
                "activity": activity,
                "native_target_extra": target_extra,
                "target_extra_name": (
                    None if target_extra == -1 else target_extra_name
                ),
                "want": _integer(
                    raw["want"], unsigned=True, maximum=(1 << 16) - 1,
                ),
            }
        if kind == "city_specialist":
            return {
                "city_ref": raw["city"],
                "city_parsed_ref": _entity_ref(raw["city"], "c"),
                "native_id": _integer(
                    raw["specialist"], unsigned=True, maximum=_I32_MAX,
                ),
                "name": _text(raw["name"]),
                "count": _integer(
                    raw["count"], unsigned=True, maximum=_I32_MAX,
                ),
                "counts_toward_population": _boolean(
                    raw["counts_toward_population"]
                ),
                "can_use": _boolean(raw["can_use"]),
                "is_default": _boolean(raw["is_default"]),
                "yields": _city_yields(raw),
            }
        if kind == "city_worklist":
            production_kind = raw["production_kind"]
            if production_kind not in _BUILD_KINDS - {"none"}:
                _fail()
            return {
                "city_ref": raw["city"],
                "city_parsed_ref": _entity_ref(raw["city"], "c"),
                "position": _integer(
                    raw["position"], unsigned=True,
                    maximum=MAX_CITY_WORKLIST - 1,
                ),
                "production_kind": production_kind,
                "production_native_id": _integer(
                    raw["production_id"], unsigned=True,
                    maximum=_I32_MAX,
                ),
                "production_name": _text(raw["production_name"]),
            }
        if kind == "city_build_choice":
            production_kind = raw["production_kind"]
            if production_kind not in _BUILD_KINDS - {"none"}:
                _fail()
            shield_cost = _integer(raw["shield_cost"])
            shield_stock_after_change = _integer(
                raw["shield_stock_after_change"]
            )
            turns = _integer(raw["turns"])
            turns_with_stock = _integer(raw["turns_with_stock"])
            upkeep = {
                output: _integer(
                    raw[f"upkeep_{output}"], unsigned=True,
                    maximum=_I32_MAX,
                )
                for output in _CITY_OUTPUTS
            }
            happy_cost = _integer(raw["happy_cost"])
            unit_fields = {
                key: _integer(raw[f"unit_{key}"])
                for key in (
                    "attack", "defense", "move_rate", "hp", "firepower",
                    "vision_radius_sq", "transport_capacity", "fuel",
                    "pop_cost", "bombard_rate", "city_size",
                    "paradrop_range",
                )
            }
            building_genus = _text(raw["building_genus"])
            building_fields = {
                key: _integer(raw[f"building_{key}"])
                for key in (
                    "obsolete", "redundant", "convert", "allows_units",
                    "allows_extras", "prevents_disaster",
                    "protects_vs_actions", "allows_actions",
                )
            }
            if production_kind == "unit":
                if (
                    shield_cost < 1
                    or shield_stock_after_change < 0
                    or turns < 1 or turns > _FC_INFINITY
                    or turns_with_stock < 1
                    or turns_with_stock > _FC_INFINITY
                    or happy_cost < 0
                    or any(value < 0 for value in unit_fields.values())
                    or unit_fields["hp"] < 1
                    or unit_fields["firepower"] < 1
                    or building_genus != "none"
                    or any(value != -1 for value in building_fields.values())
                ):
                    _fail()
                unit_stats: dict[str, Any] | None = unit_fields
                building_stats: dict[str, Any] | None = None
            else:
                if (
                    happy_cost != -1
                    or any(value != -1 for value in unit_fields.values())
                    or building_genus == "none"
                    or any(value not in {0, 1} for value in building_fields.values())
                    or any(
                        upkeep[output] != 0
                        for output in _CITY_OUTPUTS if output != "gold"
                    )
                    or (
                        building_fields["convert"] == 1
                        and (
                            shield_cost != -1
                            or shield_stock_after_change != -1
                            or turns != -1 or turns_with_stock != -1
                        )
                    )
                    or (
                        building_fields["convert"] == 0
                        and (
                            shield_cost < 1
                            or shield_stock_after_change < 0
                            or turns < 1 or turns > _FC_INFINITY
                            or turns_with_stock < 1
                            or turns_with_stock > _FC_INFINITY
                        )
                    )
                ):
                    _fail()
                unit_stats = None
                building_stats = {
                    "genus": building_genus,
                    **{
                        key: bool(value)
                        for key, value in building_fields.items()
                    },
                }
            return {
                "city_ref": raw["city"],
                "city_parsed_ref": _entity_ref(raw["city"], "c"),
                "production_kind": production_kind,
                "production_native_id": _integer(
                    raw["production_id"], unsigned=True,
                    maximum=_I32_MAX,
                ),
                "production_name": _text(raw["production_name"]),
                "can_queue": _boolean(raw["can_queue"]),
                "can_build_now": _boolean(raw["can_build_now"]),
                "shield_cost": (
                    None if shield_cost == -1 else shield_cost
                ),
                "shield_stock_after_change": (
                    None if shield_stock_after_change == -1
                    else shield_stock_after_change
                ),
                "turns": None if turns == -1 else turns,
                "turns_with_stock": (
                    None if turns_with_stock == -1 else turns_with_stock
                ),
                "upkeep": upkeep,
                "happy_cost": None if happy_cost == -1 else happy_cost,
                "unit_stats": unit_stats,
                "building_stats": building_stats,
            }
        if kind == "city_improvement":
            sell_price = _integer(
                raw["sell_price"], unsigned=True, maximum=_I32_MAX,
            )
            if sell_price < 1:
                _fail()
            return {
                "city_ref": raw["city"],
                "city_parsed_ref": _entity_ref(raw["city"], "c"),
                "native_id": _integer(
                    raw["improvement_id"], unsigned=True,
                    maximum=_I32_MAX,
                ),
                "name": _text(raw["name"]),
                "sellable": _boolean(raw["sellable"]),
                "sell_price": sell_price,
            }
        if kind == "city_trade_route":
            visibility = raw["partner_visibility"]
            direction = raw["direction"]
            partner_ref = None if raw["partner"] == "none" else raw["partner"]
            partner_name = _text(raw["partner_name"])
            if partner_ref is not None:
                _entity_ref(partner_ref, "c")
            if (
                visibility not in _TRADE_ROUTE_PARTNER_VISIBILITIES
                or direction not in _TRADE_ROUTE_DIRECTIONS
                or (visibility == "unavailable")
                   is not (partner_ref is None)
                or visibility == "unavailable"
                   and partner_name != "unavailable"
            ):
                _fail()
            return {
                "city_ref": raw["city"],
                "city_parsed_ref": _entity_ref(raw["city"], "c"),
                "position": _integer(
                    raw["position"], unsigned=True,
                    maximum=MAX_CITY_TRADE_ROUTES - 1,
                ),
                "partner_ref": partner_ref,
                "partner_visibility": visibility,
                "partner_name": (
                    None if partner_ref is None else partner_name
                ),
                "base_value": _integer(
                    raw["base_value"], unsigned=True, maximum=_I32_MAX,
                ),
                "effective_value": _integer(
                    raw["effective_value"], unsigned=True,
                    maximum=_I32_MAX,
                ),
                "direction": direction,
                "native_goods_id": _integer(
                    raw["goods_id"], unsigned=True,
                    maximum=MAX_GOODS_TYPES - 1,
                ),
                "goods_name": _text(raw["goods_name"]),
            }
        if kind == "investigation":
            production_kind = raw["production_kind"]
            if production_kind not in _BUILD_KINDS - {"none"}:
                _fail()
            return {
                "city_ref": raw["city"],
                "city_parsed_ref": _entity_ref(raw["city"], "c"),
                "lifecycle": _integer(
                    raw["lifecycle"], unsigned=True, maximum=_U64_MAX,
                ),
                "native_tile": _integer(
                    raw["tile"], unsigned=True, maximum=_I32_MAX,
                ),
                "name": _text(raw["name"]),
                "size": _integer(
                    raw["size"], unsigned=True, maximum=_I32_MAX,
                ),
                "production_kind": production_kind,
                "production_native_id": _integer(
                    raw["production_id"], unsigned=True, maximum=_I32_MAX,
                ),
                "production_name": _text(raw["production_name"]),
                "shield_stock": _integer(
                    raw["shield_stock"], unsigned=True, maximum=_I32_MAX,
                ),
                "shield_surplus": _integer(raw["shield_surplus"]),
                "improvement_count": _integer(
                    raw["improvement_count"], unsigned=True,
                    maximum=MAX_INVESTIGATION_IMPROVEMENTS,
                ),
                "feeling_count": _integer(
                    raw["feeling_count"], unsigned=True,
                    maximum=len(INVESTIGATION_FEELING_STAGES),
                ),
                "specialist_count": _integer(
                    raw["specialist_count"], unsigned=True,
                    maximum=MAX_INVESTIGATION_SPECIALISTS,
                ),
            }
        if kind == "investigation_improvement":
            return {
                "city_ref": raw["city"],
                "city_parsed_ref": _entity_ref(raw["city"], "c"),
                "native_id": _integer(
                    raw["improvement_id"], unsigned=True,
                    maximum=_I32_MAX,
                ),
                "name": _text(raw["name"]),
            }
        if kind == "investigation_citizens":
            return {
                "city_ref": raw["city"],
                "city_parsed_ref": _entity_ref(raw["city"], "c"),
                "stage": _integer(
                    raw["stage"], unsigned=True,
                    maximum=len(INVESTIGATION_FEELING_STAGES) - 1,
                ),
                "happy": _integer(
                    raw["happy"], unsigned=True, maximum=_I32_MAX,
                ),
                "content": _integer(
                    raw["content"], unsigned=True, maximum=_I32_MAX,
                ),
                "unhappy": _integer(
                    raw["unhappy"], unsigned=True, maximum=_I32_MAX,
                ),
                "angry": _integer(
                    raw["angry"], unsigned=True, maximum=_I32_MAX,
                ),
            }
        if kind == "investigation_specialist":
            return {
                "city_ref": raw["city"],
                "city_parsed_ref": _entity_ref(raw["city"], "c"),
                "native_id": _integer(
                    raw["specialist"], unsigned=True,
                    maximum=MAX_INVESTIGATION_SPECIALISTS - 1,
                ),
                "name": _text(raw["name"]),
                "count": _integer(
                    raw["count"], unsigned=True, maximum=_I32_MAX,
                ),
            }
        if kind == "city_rally":
            active = _boolean(raw["active"])
            persistent = _boolean(raw["persistent"])
            vigilant = _boolean(raw["vigilant"])
            order_count = _integer(
                raw["order_count"], unsigned=True,
                maximum=MAX_RALLY_ORDERS - 1,
            )
            digest = raw["orders_digest"]
            if (
                _FNV1A64_DIGEST.fullmatch(digest) is None
                or not active and (
                    persistent or vigilant or order_count != 0
                    or digest != "fnv1a64-0000000000000000"
                )
                or active and order_count == 0
            ):
                _fail()
            return {
                "city_ref": raw["city"],
                "city_parsed_ref": _entity_ref(raw["city"], "c"),
                "active": active,
                "persistent": persistent,
                "vigilant": vigilant,
                "order_count": order_count,
                "orders_digest": digest,
            }
        if kind == "city_governor":
            minimum_surplus = {
                name: _integer(raw[f"min_{name}"])
                for name in (
                    "food", "production", "trade", "gold", "luxury",
                    "science",
                )
            }
            weights = {
                name: _integer(raw[f"weight_{name}"])
                for name in (
                    "food", "production", "trade", "gold", "luxury",
                    "science",
                )
            }
            celebration_weight = _integer(raw["celebration_weight"])
            if (
                any(value < -100 or value > 100
                    for value in minimum_surplus.values())
                or any(value < 0 or value > 25
                       for value in weights.values())
                or celebration_weight < 0 or celebration_weight > 50
            ):
                _fail()
            return {
                "city_ref": raw["city"],
                "city_parsed_ref": _entity_ref(raw["city"], "c"),
                "minimum_surplus": minimum_surplus,
                "weights": weights,
                "celebration_weight": celebration_weight,
                "require_happy": _boolean(raw["require_happy"]),
                "maximize_growth": _boolean(raw["maximize_growth"]),
            }
        if kind == "unit_route":
            order_index = _integer(
                raw["order_index"], unsigned=True,
                maximum=MAX_RALLY_ORDERS - 1,
            )
            reconstructable = _boolean(raw["reconstructable"])
            step_count = _integer(
                raw["step_count"], unsigned=True,
                maximum=MAX_RALLY_ORDERS - 1,
            )
            if reconstructable is not (step_count > 0):
                _fail()
            return {
                "unit_ref": raw["unit"],
                "unit_parsed_ref": _entity_ref(raw["unit"], "u"),
                "order_index": order_index,
                "reconstructable": reconstructable,
                "step_count": step_count,
            }
        if kind == "unit_route_step":
            step_kind = raw["kind"]
            if step_kind not in _UNIT_ROUTE_STEP_KINDS:
                _fail()
            return {
                "unit_ref": raw["unit"],
                "unit_parsed_ref": _entity_ref(raw["unit"], "u"),
                "sequence": _integer(
                    raw["sequence"], unsigned=True,
                    maximum=MAX_RALLY_ORDERS - 1,
                ),
                "kind": step_kind,
                "native_tile": _integer(
                    raw["tile"], unsigned=True, maximum=_I32_MAX,
                ),
            }
        if kind == "unit":
            scope = raw["scope"]
            if scope not in {"own", "visible"}:
                _fail()
            native_type_id = _integer(
                raw["type_id"], unsigned=True, maximum=_I32_MAX,
            )
            item = {
                "ref": raw["ref"], "parsed_ref": _entity_ref(raw["ref"], "u"),
                "scope": scope, "owner_ref": raw["owner"],
                "owner_parsed_ref": _entity_ref(raw["owner"], "p"),
                "native_type_id": native_type_id,
                "type": _text(raw["type"]),
                "native_tile": _integer(raw["tile"], unsigned=True, maximum=_I32_MAX),
                "x": _integer(raw["x"], unsigned=True, maximum=_I32_MAX),
                "y": _integer(raw["y"], unsigned=True, maximum=_I32_MAX),
                "hp": _integer(raw["hp"], unsigned=True),
            }
            veteran = _integer(
                raw["veteran"], unsigned=True, maximum=_I32_MAX,
            )
            veteran_levels = _integer(
                raw["veteran_levels"], unsigned=True, maximum=_I32_MAX,
            )
            veteran_power = _integer(
                raw["veteran_power"], unsigned=True, maximum=_I32_MAX,
            )
            veteran_move_bonus = _integer(
                raw["veteran_move_bonus"], maximum=_I32_MAX,
            )
            type_stats = {
                "max_hp": _integer(
                    raw["max_hp"], unsigned=True, maximum=_I32_MAX,
                ),
                "max_fuel": _integer(
                    raw["max_fuel"], unsigned=True, maximum=_I32_MAX,
                ),
                "move_rate": _integer(
                    raw["move_rate"], unsigned=True, maximum=_I32_MAX,
                ),
                "attack": _integer(
                    raw["attack"], unsigned=True, maximum=_I32_MAX,
                ),
                "defense": _integer(
                    raw["defense"], unsigned=True, maximum=_I32_MAX,
                ),
                "firepower": _integer(
                    raw["firepower"], unsigned=True, maximum=_I32_MAX,
                ),
                "base_upkeep": {
                    output: _integer(
                        raw[f"base_upkeep_{output}"],
                        unsigned=True, maximum=_I32_MAX,
                    )
                    for output in (
                        "food", "shield", "trade", "gold", "luxury",
                        "science",
                    )
                },
            }
            if (
                veteran_levels < 1 or veteran >= veteran_levels
                or veteran_power < 1
                or veteran_move_bonus < 0
                or type_stats["max_hp"] < 1
                or type_stats["move_rate"] < 0
                or type_stats["firepower"] < 1
                or item["hp"] > type_stats["max_hp"]
            ):
                _fail()
            item["veteran"] = veteran
            item["veteran_name"] = _text(raw["veteran_name"])
            item["veteran_levels"] = veteran_levels
            item["veteran_power"] = veteran_power
            item["veteran_move_bonus"] = veteran_move_bonus
            item["type_stats"] = type_stats
            if scope == "own":
                home_ref = (
                    None if raw["home_city"] == "none"
                    else raw["home_city"]
                )
                if home_ref is not None:
                    _entity_ref(home_ref, "c")
                converted_type_id = _integer(raw["converts_to_id"])
                converted_type = _text(raw["converts_to"])
                if (
                    converted_type_id < -1
                    or converted_type_id > _I32_MAX
                    or (converted_type_id == -1)
                       is not (converted_type == "none")
                    or converted_type_id == native_type_id
                ):
                    _fail()
                item["home_ref"] = home_ref
                item["converted_type_native_id"] = converted_type_id
                item["converted_type"] = converted_type
                item["fuel"] = _integer(
                    raw["fuel"], unsigned=True, maximum=_I32_MAX,
                )
                if item["fuel"] > type_stats["max_fuel"]:
                    _fail()
                item["upkeep"] = {
                    output: _integer(
                        raw[f"upkeep_{output}"],
                        unsigned=True, maximum=_I32_MAX,
                    )
                    for output in (
                        "food", "shield", "trade", "gold", "luxury",
                        "science",
                    )
                }
                item["moves"] = _integer(raw["moves"], unsigned=True)
                activity = raw["activity"]
                if activity not in _ACTIVITIES - {"none"}:
                    _fail()
                activity_target = _integer(raw["activity_target"])
                if activity_target < -1 or activity_target > _I32_MAX:
                    _fail()
                target_name = _text(raw["activity_target_name"])
                if (activity_target == -1) is not (target_name == "none"):
                    _fail()
                if (
                    (activity in _TARGETED_ACTIVITIES)
                    is not (activity_target >= 0)
                ):
                    _fail()
                item["activity"] = activity
                item["activity_target"] = activity_target
                item["activity_target_name"] = target_name
                item["activity_progress"] = _integer(
                    raw["activity_progress"], unsigned=True,
                    maximum=_I32_MAX,
                )
                transport_state = raw["transport_state"]
                if transport_state not in _TRANSPORT_STATES:
                    _fail()
                transporter_ref = (
                    None if raw["transporter"] == "none"
                    else raw["transporter"]
                )
                if transporter_ref is not None:
                    _entity_ref(transporter_ref, "u")
                capacity = _integer(
                    raw["transport_capacity"], unsigned=True,
                    maximum=_I32_MAX,
                )
                occupied = _integer(raw["occupied"])
                if (
                    occupied < -1
                    or occupied > _I32_MAX
                    or transport_state == "transported"
                       and (transporter_ref is None or occupied < 0)
                    or transport_state == "untransported"
                       and (transporter_ref is not None or occupied < 0)
                    or transport_state == "unresolved"
                       and (transporter_ref is not None or occupied != -1)
                    or occupied >= 0 and occupied > capacity
                ):
                    _fail()
                item["transport_state"] = transport_state
                item["transporter_ref"] = transporter_ref
                item["transport_capacity"] = capacity
                item["occupied"] = occupied
                item["paradropped"] = _boolean(raw["paradropped"])
                item["paradrop_range"] = _integer(
                    raw["paradrop_range"], unsigned=True,
                    maximum=_I32_MAX,
                )
                controller = raw["controller"]
                if controller not in _UNIT_CONTROLLERS:
                    _fail()
                item["controller"] = controller
                has_orders = _boolean(raw["has_orders"])
                orders_repeat = _boolean(raw["orders_repeat"])
                orders_vigilant = _boolean(raw["orders_vigilant"])
                order_count = _integer(
                    raw["order_count"], unsigned=True,
                    maximum=MAX_RALLY_ORDERS - 1,
                )
                orders_digest = raw["orders_digest"]
                orders_destination = _integer(raw["orders_destination"])
                if (
                    _FNV1A64_DIGEST.fullmatch(orders_digest) is None
                    or orders_destination < -1
                    or orders_destination > _I32_MAX
                    or not has_orders and (
                        orders_repeat or orders_vigilant or order_count != 0
                        or orders_digest != "fnv1a64-0000000000000000"
                        or orders_destination != -1
                    )
                    or has_orders and (
                        order_count == 0
                        or orders_digest == "fnv1a64-0000000000000000"
                        or orders_repeat and orders_destination != -1
                        or not orders_repeat and orders_destination < 0
                    )
                ):
                    _fail()
                item["has_orders"] = has_orders
                item["orders_repeat"] = orders_repeat
                item["orders_vigilant"] = orders_vigilant
                item["order_count"] = order_count
                item["orders_digest"] = orders_digest
                item["orders_destination"] = orders_destination
                action_decision_want = raw["action_decision_want"]
                action_decision_tile = _integer(raw["action_decision_tile"])
                if (
                    action_decision_want not in {
                        "nothing", "passive", "active",
                    }
                    or action_decision_tile < -1
                    or action_decision_tile > _I32_MAX
                    or (action_decision_want == "nothing")
                       is not (action_decision_tile == -1)
                ):
                    _fail()
                item["action_decision_want"] = action_decision_want
                item["action_decision_tile"] = action_decision_tile
            return item
        if kind == "tombstone":
            expected = {"player": "p", "city": "c", "unit": "u"}.get(raw["kind"])
            if expected is None:
                _fail()
            return {
                "ref": raw["ref"],
                "parsed_ref": _entity_ref(raw["ref"], expected),
                "kind": raw["kind"],
            }
        if kind == "chat_recipient":
            return {
                "ref": raw["ref"],
                "parsed_ref": _entity_ref(raw["ref"], "p"),
                "name": _text(raw["name"]),
                "self": _boolean(raw["self"]),
                "connected": _boolean(raw["connected"]),
                "can_message": _boolean(raw["can_message"]),
            }
        if kind == "chat":
            sender = raw["sender"]
            channel = raw["channel"]
            if sender not in _CHAT_SENDERS or channel not in _CHAT_CHANNELS:
                _fail()
            message = _text(
                raw["message"], nonempty=False, allow_controls=True,
            )
            message = "".join(
                " " if unicodedata.category(char).startswith("C") else char
                for char in message
            )
            if len(message.encode("utf-8")) > MAX_CHAT_MESSAGE_BYTES:
                _fail()
            return {
                "sequence": _integer(raw["sequence"], unsigned=True),
                "turn": _integer(
                    raw["turn"], unsigned=True, maximum=_I32_MAX,
                ),
                "phase": _integer(
                    raw["phase"], unsigned=True, maximum=_I32_MAX,
                ),
                "sender": sender,
                "sender_name": _text(raw["sender_name"]),
                "self": _boolean(raw["self"]),
                "channel": channel,
                "event": _text(raw["event"]),
                "truncated": _boolean(raw["truncated"]),
                "message": message,
            }
        if kind == "action":
            if _ACTION_SLOT.fullmatch(raw["slot"]) is None:
                _fail()
            if raw["legality"] not in _LEGALITY or raw[
                "probability_kind"
            ] not in _PROBABILITY_KINDS:
                _fail()
            actor = None if raw["actor"] == "none" else raw["actor"]
            if actor is not None:
                match = _ENTITY_REF.fullmatch(actor)
                if match is None or match.group("kind") not in {"p", "u", "c"}:
                    _fail()
            counterpart = (
                None if raw["counterpart"] == "none"
                else raw["counterpart"]
            )
            if counterpart is not None:
                _entity_ref(counterpart, "p")
            clause_giver = (
                None if raw["clause_giver"] == "none"
                else raw["clause_giver"]
            )
            if clause_giver is not None:
                _entity_ref(clause_giver, "p")
            clause_type = raw["clause_type"]
            if (
                clause_type != "none"
                and clause_type not in _DIPLOMACY_CLAUSE_TYPES
            ):
                _fail()
            relation_state = raw["relation_state"]
            if (
                relation_state != "none"
                and relation_state not in _DIPLOMACY_STATES
            ):
                _fail()
            clauses_digest = raw["clauses_digest"]
            if _FNV1A64_DIGEST.fullmatch(clauses_digest) is None:
                _fail()
            desired_acceptance = _integer(raw["desired_acceptance"])
            if desired_acceptance not in {-1, 0, 1}:
                _fail()
            target_unit = (
                None if raw["target_unit"] == "none"
                else raw["target_unit"]
            )
            source_city = (
                None if raw["source_city"] == "none" else raw["source_city"]
            )
            destination_city = (
                None if raw["destination_city"] == "none"
                else raw["destination_city"]
            )
            if source_city is not None:
                _entity_ref(source_city, "c")
            if destination_city is not None:
                _entity_ref(destination_city, "c")
            transport_context = (
                None if raw["transport_context"] == "none"
                else raw["transport_context"]
            )
            if target_unit is not None:
                _entity_ref(target_unit, "u")
            if transport_context is not None:
                _entity_ref(transport_context, "u")
            build_kind = raw["target_build_kind"]
            if build_kind not in _BUILD_KINDS:
                _fail()
            target_build = _integer(raw["target_build"])
            if (
                target_build < -1 or target_build > _I32_MAX
                or (build_kind == "none") is not (target_build == -1)
            ):
                _fail()
            target_extra = _integer(raw["target_extra"])
            if target_extra < -1 or target_extra > _I32_MAX:
                _fail()
            subtarget_kind = raw["subtarget_kind"]
            if subtarget_kind not in _ACTION_SUBTARGET_KINDS:
                _fail()
            if raw["subresults"] == "none":
                subresults: tuple[str, ...] = ()
            else:
                subresults = tuple(raw["subresults"].split(","))
                if (
                    not subresults
                    or any(item not in _ACTION_SUBRESULTS
                           for item in subresults)
                    or tuple(sorted(
                        set(subresults), key=_ACTION_SUBRESULTS.index,
                    )) != subresults
                ):
                    _fail()
            source_specialist = _integer(raw["source_specialist"])
            target_specialist = _integer(raw["target_specialist"])
            if (
                source_specialist < -1 or source_specialist > _I32_MAX
                or target_specialist < -1
                or target_specialist > _I32_MAX
            ):
                _fail()
            activity = raw["activity"]
            if activity not in _ACTIVITIES:
                _fail()
            infrastructure_choice_count = _integer(
                raw["infrastructure_choice_count"], unsigned=True,
                maximum=MAX_INFRASTRUCTURE_CHOICES,
            )
            infrastructure_choices: tuple[int, ...]
            if raw["infrastructure_choices"] == "-":
                infrastructure_choices = ()
            else:
                tokens = raw["infrastructure_choices"].split(",")
                if any(_UNSIGNED.fullmatch(token) is None for token in tokens):
                    _fail()
                infrastructure_choices = tuple(
                    _integer(token, unsigned=True, maximum=_I32_MAX)
                    for token in tokens
                )
            if (
                len(infrastructure_choices) != infrastructure_choice_count
                or bool(infrastructure_choices)
                   is (raw["infrastructure_choices"] == "-")
                or tuple(sorted(set(infrastructure_choices)))
                   != infrastructure_choices
            ):
                _fail()
            spaceship_part = raw["spaceship_part"]
            spaceship_value = _integer(raw["spaceship_value"])
            if (
                spaceship_part not in _SPACESHIP_PARTS
                or spaceship_value < -1 or spaceship_value > 31
                or (spaceship_part == "none") is not (spaceship_value == -1)
            ):
                _fail()
            target_multiplier = _integer(raw["target_multiplier"])
            multiplier_value = _integer(raw["multiplier_value"])
            if (
                target_multiplier < -1
                or target_multiplier >= MAX_MULTIPLIERS
                or multiplier_value < -_I32_MAX - 1
                or multiplier_value > _I32_MAX
                or target_multiplier == -1 and multiplier_value != -1
            ):
                _fail()
            gold_cost = _integer(raw["gold_cost"])
            if gold_cost < -1 or gold_cost > _I32_MAX:
                _fail()
            server_setting_id = _integer(raw["server_setting_id"])
            server_setting_type = raw["server_setting_type"]
            server_setting_min = _integer(raw["server_setting_min"])
            server_setting_max = _integer(raw["server_setting_max"])
            server_setting_current = _integer(raw["server_setting_current"])
            server_setting_value = _integer(raw["server_setting_value"])
            if (
                server_setting_id < -1 or server_setting_id > _I32_MAX
                or server_setting_type not in _SERVER_SETTING_TYPES
                or server_setting_min < -_I32_MAX - 1
                or server_setting_min > _I32_MAX
                or server_setting_max < -_I32_MAX - 1
                or server_setting_max > _I32_MAX
                or server_setting_current < -_I32_MAX - 1
                or server_setting_current > _I32_MAX
                or server_setting_value < -_I32_MAX - 1
                or server_setting_value > _I32_MAX
            ):
                _fail()
            return {
                "slot": raw["slot"], "native_kind": raw["kind"], "actor_ref": actor,
                "counterpart_ref": counterpart,
                "meeting_generation": _integer(
                    raw["meeting_generation"], unsigned=True,
                ),
                "clauses_digest": clauses_digest,
                "self_accepted": _boolean(raw["self_accepted"]),
                "other_accepted": _boolean(raw["other_accepted"]),
                "relation_state": relation_state,
                "outgoing_vision": _boolean(raw["outgoing_vision"]),
                "outgoing_shared_tiles": _boolean(
                    raw["outgoing_shared_tiles"],
                ),
                "clause_giver_ref": clause_giver,
                "clause_type": clause_type,
                "native_clause_value": _integer(raw["clause_value"]),
                "clause_name": _text(raw["clause_name"]),
                "desired_acceptance": desired_acceptance,
                "native_target_tile": _integer(raw["target_tile"]),
                "source_city_ref": source_city,
                "destination_city_ref": destination_city,
                "target_unit_ref": target_unit,
                "transport_context_ref": transport_context,
                "native_target_tech": _integer(raw["target_tech"]),
                "native_vote_no": _integer(raw["vote_no"]),
                "native_server_setting_id": server_setting_id,
                "server_setting_type": server_setting_type,
                "server_setting_min": server_setting_min,
                "server_setting_max": server_setting_max,
                "server_setting_current": server_setting_current,
                "server_setting_value": server_setting_value,
                "native_target_government": _integer(
                    raw["target_government"],
                ),
                "max_rate": _integer(
                    raw["max_rate"], unsigned=True, maximum=100,
                ),
                "route_waypoint_limit": _integer(
                    raw["route_waypoint_limit"], unsigned=True,
                    maximum=MAX_UNIT_ROUTE_WAYPOINTS,
                ),
                "infrastructure_cost": _integer(
                    raw["infrastructure_cost"], unsigned=True,
                    maximum=_I32_MAX,
                ),
                "infrastructure_turns": _integer(
                    raw["infrastructure_turns"], unsigned=True,
                    maximum=_I32_MAX,
                ),
                "infrastructure_choice_count": infrastructure_choice_count,
                "infrastructure_choices": infrastructure_choices,
                "target_build_kind": build_kind,
                "native_target_build": target_build,
                "spaceship_part": spaceship_part,
                "spaceship_value": spaceship_value,
                "native_target_multiplier": target_multiplier,
                "multiplier_value": multiplier_value,
                "native_source_specialist": source_specialist,
                "native_target_specialist": target_specialist,
                "native_target_extra": target_extra,
                "subtarget_kind": subtarget_kind,
                "subresults": subresults,
                "activity": activity,
                "target_name": _text(raw["target_name"]),
                "native_rule": _text(raw["native_rule"]),
                "target_kind": _text(raw["target_kind"]),
                "result": _text(raw["result"]),
                "consuming": _boolean(raw["actor_consuming_always"]),
                "legality": raw["legality"],
                "probability_kind": raw["probability_kind"],
                "probability_min": _integer(raw["probability_min"]),
                "probability_max": _integer(raw["probability_max"]),
                "gold_cost": gold_cost,
                "args": raw["args"],
            }
        _fail()

    @staticmethod
    def _setting_fields_are_empty(action: Mapping[str, Any]) -> bool:
        return (
            action["native_server_setting_id"] == -1
            and action["server_setting_type"] == "none"
            and action["server_setting_min"] == 0
            and action["server_setting_max"] == 0
            and action["server_setting_current"] == -1
            and action["server_setting_value"] == -1
        )

    @staticmethod
    def _setting_action_is_well_formed(
        action: Mapping[str, Any], rule: _NativeActionRule,
    ) -> bool:
        setting_type = action["server_setting_type"]
        minimum = action["server_setting_min"]
        maximum = action["server_setting_max"]
        current = action["server_setting_current"]
        value = action["server_setting_value"]
        if (
            action["native_server_setting_id"] < 0
            or setting_type != rule.variant
            or minimum > maximum
        ):
            return False
        if setting_type == "boolean":
            return (
                minimum == 0 and maximum == 1
                and current in {0, 1} and value in {0, 1}
                and value != current
            )
        if setting_type == "enum":
            return (
                minimum == 0 and maximum >= 0
                and minimum <= current <= maximum
                and minimum <= value <= maximum and value != current
            )
        if setting_type == "integer":
            return minimum <= current <= maximum and value == -1
        if setting_type == "string":
            return (
                minimum == 0 and maximum > 0
                and current == -1 and value == -1
            )
        if setting_type == "bitwise":
            return (
                minimum == 0 and maximum >= 0
                and minimum <= current <= maximum and value == -1
            )
        return False

    @staticmethod
    def _governance_targets_are_empty(action: Mapping[str, Any]) -> bool:
        return (
            action["native_target_tile"] == -1
            and action["native_target_tech"] == -1
            and action["native_target_government"] == -1
            and action["max_rate"] == 0
            and action["route_waypoint_limit"] == 0
            and action["infrastructure_cost"] == 0
            and action["infrastructure_turns"] == 0
            and action["infrastructure_choice_count"] == 0
            and not action["infrastructure_choices"]
            and action["target_build_kind"] == "none"
            and action["native_target_build"] == -1
            and action["spaceship_part"] == "none"
            and action["spaceship_value"] == -1
            and action["native_target_multiplier"] == -1
            and action["multiplier_value"] == -1
            and action["native_source_specialist"] == -1
            and action["native_target_specialist"] == -1
            and action["native_target_extra"] == -1
            and action["activity"] == "none"
            and action["source_city_ref"] is None
            and action["destination_city_ref"] is None
            and action["target_unit_ref"] is None
            and action["transport_context_ref"] is None
            and action["gold_cost"] == -1
        )

    def _validate_cross_links(
        self,
        buckets: Mapping[str, list[dict[str, Any]]],
        player: Mapping[str, Any] | None,
    ) -> None:
        def unique(values: Sequence[Any]) -> None:
            if len(values) != len(set(values)):
                _fail()

        meta = buckets["meta"][0]
        votes = buckets["vote"]
        if len(votes) > MAX_VOTES or sum(
            item["status"] != "active" for item in votes
        ) > MAX_VOTE_HISTORY:
            _fail()
        unique([item["native_vote_no"] for item in votes])
        vote_by_no = {item["native_vote_no"]: item for item in votes}
        if meta["state"] == "preparing":
            pregame = buckets["pregame"][0]
            chats = buckets["chat"]
            actions = buckets["action"]
            cast_actions = [
                item for item in actions
                if item["native_rule"] == "player.cast_vote"
            ]
            expected_vote_nos = {
                item["native_vote_no"] for item in votes if item["can_vote"]
            }
            if len(chats) > MAX_CHAT_HISTORY:
                _fail()
            unique([item["sequence"] for item in chats])
            if any(
                item["sequence"] == 0
                or item["turn"] != 0 or item["phase"] != 0
                or item["self"] and item["sender"] == "server"
                for item in chats
            ):
                _fail()
            unique([item["slot"] for item in actions])
            if (
                meta["turn"] != 0 or meta["phase"] != 0
                or meta["phase_count"] != 1 or meta["active_phase"]
                or meta["phase_ready"]
                or len(actions)
                   < (2 if pregame["ready"] else 4) + len(expected_vote_nos)
                or {item["native_rule"] for item in actions}
                   - {
                       "pregame.configure", "pregame.set_ready",
                       "pregame.set_team", "player.cast_vote",
                       "player.send_chat",
                       "player.cancel_vote",
                       "player.propose_server_setting_boolean",
                       "player.propose_server_setting_integer",
                       "player.propose_server_setting_string",
                       "player.propose_server_setting_enum",
                       "player.propose_server_setting_bitwise",
                   }
                or {item["native_vote_no"] for item in cast_actions}
                   != expected_vote_nos
                or sum(item["native_rule"] == "pregame.set_ready"
                       for item in actions) != 1
                or sum(item["native_rule"] == "player.send_chat"
                       for item in actions) != 1
                or (not pregame["ready"])
                   != any(item["native_rule"] == "pregame.configure"
                          for item in actions)
                or (not pregame["ready"])
                   != any(item["native_rule"] == "pregame.set_team"
                          for item in actions)
                or pregame["team_choices"] < 1
            ):
                _fail()
            setting_keys: set[tuple[int, str, int]] = set()
            for action in actions:
                rule = _ACTION_RULES.get(action["native_rule"])
                operation = rule.operation if rule is not None else ""
                is_vote = operation in {"cast_vote", "cancel_vote"}
                is_setting = operation == "propose_server_setting"
                is_chat = operation == "send_chat"
                if (
                    rule is None or action["native_kind"] != rule.native_kind
                    or action["target_kind"] != rule.target_kind
                    or action["result"] != rule.result
                    or action["args"] != rule.args
                    or not _non_special_action_metadata_supported(action)
                    or action["actor_ref"] != pregame["ref"]
                    or action["legality"] != "legal"
                    or action["probability_kind"] != "exact"
                    or action["probability_min"] != 200
                    or action["probability_max"] != 200
                    or (action["native_vote_no"] == -1) is is_vote
                    or (is_vote and (
                        action["native_vote_no"] not in vote_by_no
                        or action["target_name"] != (
                            "vote" if operation == "cast_vote" else "own vote"
                        )
                        or action["native_target_tile"] != -1
                        or action["native_target_tech"] != -1
                        or action["max_rate"] != 0
                    ))
                    or (operation in {
                        "cast_vote", "cancel_vote", "propose_server_setting",
                    } and not self._governance_targets_are_empty(action))
                    or (is_setting and not self._setting_action_is_well_formed(
                        action, rule,
                    ))
                    or (not is_setting and not self._setting_fields_are_empty(
                        action,
                    ))
                    or is_chat and (
                        action["native_target_tile"] != -1
                        or action["native_target_tech"] != -1
                        or action["max_rate"] != 0
                        or action["target_name"] != "none"
                    )
                    or action["desired_acceptance"]
                       != ((not pregame["ready"])
                           if action["native_rule"] == "pregame.set_ready"
                           else -1)
                ):
                    _fail()
                if is_setting:
                    setting_key = (
                        action["native_server_setting_id"],
                        action["server_setting_type"],
                        action["server_setting_value"],
                    )
                    if setting_key in setting_keys:
                        _fail()
                    setting_keys.add(setting_key)
            return
        tiles = buckets["tile"]
        unique([item["native_index"] for item in tiles])
        unique([(item["x"], item["y"]) for item in tiles])
        tile_by_index = {item["native_index"]: item for item in tiles}
        infrastructure_extras = buckets["infrastructure_extra"]
        unique([item["native_id"] for item in infrastructure_extras])
        unique([item["name"] for item in infrastructure_extras])
        infrastructure_by_id = {
            item["native_id"]: item for item in infrastructure_extras
        }
        if (
            len(infrastructure_extras) > MAX_INFRASTRUCTURE_CHOICES
            or player is None and bool(infrastructure_extras)
            or infrastructure_extras
               and set(infrastructure_by_id)
                   != set(range(len(infrastructure_extras)))
        ):
            _fail()
        for tile in tiles:
            if tile["placing_extra"] == -1:
                continue
            extra = infrastructure_by_id.get(tile["placing_extra"])
            if (
                extra is None
                or tile["placing_extra_name"] != extra["name"]
                or tile["known"] == 0
            ):
                _fail()

        governance = (
            buckets["governance"][0] if buckets["governance"] else None
        )
        governments = buckets["government"]
        if player is not None:
            if governance is None:
                _fail()
            unique([item["native_id"] for item in governments])
            unique([item["name"] for item in governments])
            government_by_id = {
                item["native_id"]: item for item in governments
            }
            if (
                len(governments) != governance["choices_count"]
                or len(governments) > MAX_GOVERNMENTS
                or set(government_by_id) != set(range(len(governments)))
                or governance["current_native_id"] not in government_by_id
                or governance["during_native_id"] not in government_by_id
                or (
                    governance["target_native_id"] != -1
                    and governance["target_native_id"] not in government_by_id
                )
            ):
                _fail()
            current = government_by_id[governance["current_native_id"]]
            during = government_by_id[governance["during_native_id"]]
            targets = [item for item in governments if item["target"]]
            if (
                sum(item["current"] for item in governments) != 1
                or sum(item["during"] for item in governments) != 1
                or current["current"] is not True
                or during["during"] is not True
                or current["name"] != player["government"]
                or len(targets) != (
                    0 if governance["target_native_id"] == -1 else 1
                )
                or (
                    targets
                    and targets[0]["native_id"]
                    != governance["target_native_id"]
                )
            ):
                _fail()
            expected_status: str
            current_id = governance["current_native_id"]
            target_id = governance["target_native_id"]
            during_id = governance["during_native_id"]
            finish_turn = governance["finish_turn"]
            current_turn = buckets["meta"][0]["turn"]
            if current_id != during_id:
                expected_status = (
                    "stable" if target_id == -1 else "enactment_pending"
                )
            elif target_id in {-1, during_id}:
                expected_status = (
                    "choice_required"
                    if finish_turn <= current_turn else "anarchy"
                )
            else:
                expected_status = (
                    "enactment_pending"
                    if finish_turn <= current_turn else "anarchy_targeted"
                )
            if (
                governance["status"] != expected_status
                or governance["turns_remaining"]
                != max(0, finish_turn - current_turn)
                or governance["untargeted_allowed"]
                is not (
                    governance["method"]
                    not in {"quickening", "random_quickening"}
                )
                or governance["can_revolution"] and (
                    not governance["untargeted_allowed"]
                    or current_id == during_id and target_id == during_id
                )
                or (
                    governance["max_turns"] is not None
                    and not 1 <= governance["max_turns"] <= 20
                )
            ):
                _fail()
            change_observable = (
                governance["no_anarchy"]
                or finish_turn > current_turn
                or finish_turn <= 0
            )
            for item in governments:
                if item["can_change"] and (
                    item["current"] or item["target"] or item["during"]
                    or not change_observable
                ):
                    _fail()
        elif governance is not None or governments:
            _fail()

        multipliers = buckets["multiplier"]
        if player is None:
            if multipliers:
                _fail()
        else:
            if len(multipliers) > MAX_MULTIPLIERS:
                _fail()
            unique([item["native_id"] for item in multipliers])
            unique([item["name"] for item in multipliers])
            current_turn = buckets["meta"][0]["turn"]
            if any(
                item["changed_turn"] > current_turn
                or item["can_change"] and (
                    item["changed_turn"] > 0
                    and current_turn - item["changed_turn"]
                        < item["minimum_turns"]
                )
                for item in multipliers
            ):
                _fail()

        spaceship = (
            buckets["spaceship"][0] if buckets["spaceship"] else None
        )
        structurals = buckets["spaceship_structural"]
        if player is None:
            if spaceship is not None or structurals:
                _fail()
        else:
            if spaceship is None or len(structurals) != 32:
                _fail()
            unique([item["native_slot"] for item in structurals])
            if {item["native_slot"] for item in structurals} != set(range(32)):
                _fail()
            structural_by_slot = {
                item["native_slot"]: item for item in structurals
            }
            placed_count = sum(item["placed"] for item in structurals)
            if placed_count != spaceship["structurals_placed"]:
                _fail()
            for slot, item in structural_by_slot.items():
                required = item["required_native_slot"]
                expected_connected = (
                    slot == 0 or structural_by_slot[required]["placed"]
                )
                expected_can_place = (
                    spaceship["state"] == "started"
                    and not item["placed"]
                    and placed_count < spaceship["structurals"]
                    and expected_connected
                )
                if (
                    item["required_connected"] is not expected_connected
                    or item["can_place"] is not expected_can_place
                ):
                    _fail()

        techs = buckets["research_tech"]
        unique([item["native_id"] for item in techs])
        unique([item["name"] for item in techs])
        tech_by_id = {item["native_id"]: item for item in techs}
        graph = buckets["research_graph"]
        unique([item["native_id"] for item in graph])
        unique([item["name"] for item in graph])
        graph_by_id = {item["native_id"]: item for item in graph}
        unique([
            (item["tech_native_id"], item["prerequisite_native_id"],
             item["kind"])
            for item in buckets["research_edge"]
        ])
        for edge in buckets["research_edge"]:
            if (
                edge["tech_native_id"] not in graph_by_id
                or edge["prerequisite_native_id"] not in graph_by_id
                or (
                    edge["tech_native_id"]
                    == edge["prerequisite_native_id"]
                    and edge["kind"] != "root"
                )
            ):
                _fail()
        unique([
            (item["tech_native_id"], item["kind"], item["native_id"],
             item["scope"])
            for item in buckets["research_unlock"]
        ])
        unlock_names: dict[tuple[str, int], str] = {}
        for unlock in buckets["research_unlock"]:
            key = (unlock["kind"], unlock["native_id"])
            prior_name = unlock_names.setdefault(key, unlock["name"])
            if (
                unlock["tech_native_id"] not in graph_by_id
                or prior_name != unlock["name"]
            ):
                _fail()
        for graph_tech in graph:
            next_step = graph_tech["next_step_native_id"]
            if (
                next_step != -1 and next_step not in graph_by_id
                or not graph_tech["reachable"] and next_step != -1
            ):
                _fail()
        research = buckets["research"][0] if buckets["research"] else None
        if research is not None:
            unset_techs = [tech for tech in techs if tech["state"] == "unset"]
            if (
                len(unset_techs) != 1
                or research["choices_count"] != len(techs)
                or research["choices_digest"]
                != _research_choices_digest(techs)
            ):
                _fail()
            if (
                research["target_native_id"] not in tech_by_id
                or research["goal_native_id"] not in tech_by_id
            ):
                _fail()
            target_tech = tech_by_id[research["target_native_id"]]
            goal_tech = tech_by_id[research["goal_native_id"]]
            if (
                research["target"] != target_tech["name"]
                or research["goal"] != goal_tech["name"]
                or target_tech["state"]
                not in {"available", "future", "unset"}
                or goal_tech["state"]
                not in {"available", "reachable", "future", "unset"}
                or (target_tech["state"] == "available"
                    and not target_tech["can_target"])
                or (goal_tech["state"] != "future"
                    and not goal_tech["can_goal"])
            ):
                _fail()
        for tech in techs:
            if tech["state"] == "future":
                flags_valid = (
                    (tech["can_target"], tech["can_goal"])
                    in {(True, True), (False, False)}
                    and (
                        tech["can_target"]
                        or research is not None
                        and tech["native_id"] in {
                            research["target_native_id"],
                            research["goal_native_id"],
                        }
                    )
                )
            else:
                expected_flags = {
                    "known": (False, False),
                    "available": (True, True),
                    "reachable": (False, True),
                    "unset": (False, True),
                }[tech["state"]]
                flags_valid = (
                    (tech["can_target"], tech["can_goal"])
                    == expected_flags
                )
            if (
                not flags_valid
                or (tech["state"] == "future"
                    and tech["name"] != "Future Tech")
                or (tech["state"] == "unset" and tech["name"] != "Unset")
            ):
                _fail()
            if tech["state"] not in {"future", "unset"}:
                graph_tech = graph_by_id.get(tech["native_id"])
                if (
                    graph_tech is None
                    or graph_tech["name"] != tech["name"]
                    or not graph_tech["reachable"]
                ):
                    _fail()
        if research is not None and not graph:
            _fail()
        unique([item["other_ref"] for item in buckets["diplomacy"]])
        relation_by_ref = {
            item["other_ref"]: item for item in buckets["diplomacy"]
        }
        unique([item["other_ref"] for item in buckets["diplomacy_intel"]])
        intel_by_ref = {
            item["other_ref"]: item for item in buckets["diplomacy_intel"]
        }
        for relation_ref, relation in relation_by_ref.items():
            intel = intel_by_ref.get(relation_ref)
            if (
                (relation["team_native_id"] == -1)
                is not (relation["team_name"] == "none")
                or relation["same_team"]
                   and relation["team_native_id"] == -1
                or (relation["intel_level"] == "embassy") is not (
                    intel is not None
                )
                or relation["intel_level"] == "none" and (
                    relation["score"] is not None
                    or relation["gold"] is not None
                    or relation["government"] is not None
                )
            ):
                _fail()
            if intel is not None:
                if (
                    intel["tax"] + intel["science"] + intel["luxury"]
                    != 100
                    or any(
                        native_id not in graph_by_id
                        for native_id in intel["known_native_ids"]
                    )
                ):
                    _fail()
                research_graph = graph_by_id.get(
                    intel["research_native_id"]
                )
                if (
                    research_graph is not None
                    and research_graph["name"] != intel["research_name"]
                ):
                    _fail()
        if set(intel_by_ref) - set(relation_by_ref):
            _fail()
        clauses_by_relation: dict[str, list[dict[str, Any]]] = {
            ref: [] for ref in relation_by_ref
        }
        unique([
            (item["other_ref"], item["generation"], item["position"])
            for item in buckets["diplomacy_clause"]
        ])
        for clause in buckets["diplomacy_clause"]:
            relation = relation_by_ref.get(clause["other_ref"])
            if (
                relation is None or not relation["meeting"]
                or clause["generation"] != relation["generation"]
                or clause["giver_ref"] not in {
                    player["ref"] if player is not None else None,
                    relation["other_ref"],
                }
            ):
                _fail()
            expected_value_kind = {
                "Advance": "technology", "Gold": "gold", "City": "city",
            }.get(clause["clause_type"], "none")
            if clause["clause_type"] == "City":
                if clause["value_kind"] not in {"city", "city_unavailable"}:
                    _fail()
            elif clause["value_kind"] != expected_value_kind:
                _fail()
            if expected_value_kind == "none" and (
                clause["native_value"] != 0 or clause["name"] != "none"
            ):
                _fail()
            clauses_by_relation[clause["other_ref"]].append(clause)
        for relation_ref, relation_clauses in clauses_by_relation.items():
            relation = relation_by_ref[relation_ref]
            ordered = sorted(
                relation_clauses, key=lambda item: item["position"],
            )
            if (
                relation["can_meet"] and relation["meeting"]
                or relation["meeting"] and relation["generation"] == 0
                or (not relation["meeting"] and (
                    relation["self_accepted"]
                    or relation["other_accepted"]
                    or relation["clause_count"] != 0
                ))
                or ordered and (
                    [item["position"] for item in ordered]
                    != list(range(len(ordered)))
                    or len(ordered) != relation["clause_count"]
                    or _diplomacy_clauses_digest(ordered)
                    != relation["clauses_digest"]
                )
            ):
                _fail()

        city_sites = buckets["city_site"]
        unique([item["ref"] for item in city_sites])
        city_site_by_ref = {item["ref"]: item for item in city_sites}
        city_site_by_number = {
            item["parsed_ref"][1]: item for item in city_sites
        }
        for clause in buckets["diplomacy_clause"]:
            if clause["clause_type"] != "City":
                continue
            site = city_site_by_number.get(clause["native_value"])
            if clause["value_kind"] == "city":
                if site is None or site["name"] != clause["name"]:
                    _fail()
            elif site is not None or clause["name"] != "unavailable":
                _fail()

        live_refs: list[str] = []
        if player is not None:
            live_refs.append(player["ref"])
        live_refs.extend(item["ref"] for item in city_sites)
        live_refs.extend(item["ref"] for item in buckets["unit"])
        unique(live_refs)
        # Native numeric IDs cannot denote two live incarnations at once.
        unique([
            (item["parsed_ref"][0], item["parsed_ref"][1])
            for item in ([player] if player is not None else [])
            + city_sites + buckets["unit"]
        ])

        tombstone_refs = [item["ref"] for item in buckets["tombstone"]]
        unique(tombstone_refs)
        if set(live_refs).intersection(tombstone_refs):
            _fail()

        # One currently visible native player lifetime must have one exact
        # incarnation everywhere it is referenced, even when that foreign
        # player has no full player row in this seat's observation.
        current_player_refs = []
        if player is not None:
            current_player_refs.append(player["ref"])
        current_player_refs.extend(
            item["other_ref"] for item in buckets["diplomacy"]
        )
        current_player_refs.extend(
            item["other_ref"] for item in buckets["diplomacy_intel"]
        )
        current_player_refs.extend(
            item["giver_ref"] for item in buckets["diplomacy_clause"]
        )
        current_player_refs.extend(
            item["owner_ref"] for item in buckets["tile"]
            if item["owner_ref"] is not None
        )
        current_player_refs.extend(item["owner_ref"] for item in buckets["unit"])
        current_player_refs.extend(item["owner_ref"] for item in city_sites)
        player_lifetimes: dict[int, str] = {}
        for ref in current_player_refs:
            _, number, _ = _entity_ref(ref, "p")
            prior = player_lifetimes.setdefault(number, ref)
            if prior != ref:
                _fail()

        self_ref = player["ref"] if player is not None else None
        full_tile_catalog = bool(buckets["tile"])
        if meta["known_tile_count"] < 0:
            _fail()
        for relation in buckets["diplomacy"]:
            if self_ref is None or relation["other_ref"] == self_ref:
                _fail()
        for item in buckets["city"] + buckets["unit"]:
            tile = tile_by_index.get(item["native_tile"])
            if full_tile_catalog and (
                tile is None
                or tile["known"] != 2
                or (item["x"], item["y"]) != (tile["x"], tile["y"])
            ):
                _fail()
        for site in city_sites:
            tile = tile_by_index.get(site["native_tile"])
            expected_visibility = (
                "own" if site["owner_ref"] == self_ref
                else "visible" if tile is not None and tile["known"] == 2
                else "known"
            )
            if full_tile_catalog and (
                self_ref is None
                or tile is None
                or tile["known"] == 0
                or (site["x"], site["y"]) != (tile["x"], tile["y"])
                or site["visibility"] != expected_visibility
                or site["visibility"] == "own" and tile["known"] != 2
                or site["visibility"] == "known" and tile["known"] != 1
            ):
                _fail()
        for city in buckets["city"]:
            site = city_site_by_ref.get(city["ref"])
            if (
                self_ref is None
                or site is None
                or site["owner_ref"] != self_ref
                or site["visibility"] != "own"
                or (
                    city["name"], city["native_tile"], city["x"], city["y"],
                    city["size"],
                ) != (
                    site["name"], site["native_tile"], site["x"], site["y"],
                    site["size"],
                )
                or city["size"] == 0
                or city["can_buy"] and (
                    city["buy_cost"] <= 0
                    or city["buy_cost"] > player["gold"]
                )
            ):
                _fail()
        city_by_ref = {city["ref"]: city for city in buckets["city"]}
        if {
            site["ref"] for site in city_sites
            if site["visibility"] == "own"
        } != set(city_by_ref):
            _fail()
        city_tiles_by_ref: dict[str, list[dict[str, Any]]] = {
            ref: [] for ref in city_by_ref
        }
        city_worker_tasks_by_ref: dict[str, list[dict[str, Any]]] = {
            ref: [] for ref in city_by_ref
        }
        city_specialists_by_ref: dict[str, list[dict[str, Any]]] = {
            ref: [] for ref in city_by_ref
        }
        city_worklists_by_ref: dict[str, list[dict[str, Any]]] = {
            ref: [] for ref in city_by_ref
        }
        city_build_choices_by_ref: dict[str, list[dict[str, Any]]] = {
            ref: [] for ref in city_by_ref
        }
        city_improvements_by_ref: dict[str, list[dict[str, Any]]] = {
            ref: [] for ref in city_by_ref
        }
        city_rallies_by_ref: dict[str, list[dict[str, Any]]] = {
            ref: [] for ref in city_by_ref
        }
        unique([
            (item["city_ref"], item["native_tile"])
            for item in buckets["city_tile"]
        ])
        unique([
            (item["city_ref"], item["native_tile"])
            for item in buckets["city_worker_task"]
        ])
        unique([
            item["native_tile"] for item in buckets["city_tile"]
            if item["worked"]
        ])
        worked_city_by_tile = {
            item["native_tile"]: item["city_ref"]
            for item in buckets["city_tile"] if item["worked"]
        }
        if any(
            item["can_work"]
            and worked_city_by_tile.get(item["native_tile"])
                not in {None, item["city_ref"]}
            for item in buckets["city_tile"]
        ):
            _fail()
        unique([
            (item["city_ref"], item["native_id"])
            for item in buckets["city_specialist"]
        ])
        unique([
            (item["city_ref"], item["position"])
            for item in buckets["city_worklist"]
        ])
        unique([
            (
                item["city_ref"], item["production_kind"],
                item["production_native_id"],
            )
            for item in buckets["city_build_choice"]
        ])
        unique([
            (item["city_ref"], item["native_id"])
            for item in buckets["city_improvement"]
        ])
        unique([item["city_ref"] for item in buckets["city_rally"]])
        if buckets["city_governor"]:
            # Full CMA parameters are fetched only through the bounded,
            # owned-city state scope; OBS carries only governor_enabled.
            _fail()
        specialist_names: dict[int, str] = {}
        specialist_defaults: dict[int, bool] = {}
        specialist_population_counts: dict[int, bool] = {}
        for item in buckets["city_tile"]:
            city = city_by_ref.get(item["city_ref"])
            tile = tile_by_index.get(item["native_tile"])
            if (
                city is None
                # Citizen tiles are exported by the "city_citizens" scope
                # (protocol_v2.c v2_build_state_scope_rows) while map tiles
                # come from the separate "known_tiles"/"map_tiles"/
                # "tile_window" scopes, so a bundle can legitimately carry the
                # citizen rows without any tile row.  Demanding the map tile
                # unconditionally would reject that bundle forever, exactly
                # like the worker-task check below; the tile facts are only
                # cross-checkable at full catalog.
                or full_tile_catalog and (
                    tile is None or tile["known"] == 0
                    or item["can_work"] and tile["known"] != 2
                    or tile["known"] == 1
                       and not item["worked"] and not item["free_worked"]
                )
            ):
                _fail()
            city_tiles_by_ref[item["city_ref"]].append(item)
        worker_task_extra_names: dict[int, str] = {}
        for item in buckets["city_worker_task"]:
            city = city_by_ref.get(item["city_ref"])
            tile = tile_by_index.get(item["native_tile"])
            if (
                city is None
                # Worker tasks travel with the cities catalog and so reach a
                # compact observation, but tiles are exported only through
                # STATE_SCOPE and do not.  Demanding a matching tile row
                # unconditionally would make any persisted worker task reject
                # every observation for the rest of the game, exactly as the
                # neighbouring city and city-site checks would without their
                # own catalog guard.
                or full_tile_catalog and (tile is None or tile["known"] != 2)
                # No citizen-row requirement.  The C promises nothing here:
                # worker_task_is_sane() (common/workertask.c) checks only that
                # the tile is non-null with a bounded activity and consistent
                # extra -- it never constrains the task tile to the city work
                # radius -- while the city_citizens section emits a city_tile
                # row only when the tile is TILE_KNOWN_SEEN, worked by this
                # city, or free-worked (protocol_v2.c).  So a task on a fogged
                # in-radius tile (any ruleset with
                # vision_radius_sq < city_radius_sq) or on an out-of-radius
                # tile legitimately has no citizen row, and demanding one is
                # the same unreachable-until-it-wasn't shape that produced the
                # original worker-task wedge.  `city_tile` is looked up BY
                # (city_ref, native_tile), so its presence carries no further
                # fact to cross-check.
            ):
                _fail()
            if item["native_target_extra"] >= 0:
                prior = worker_task_extra_names.setdefault(
                    item["native_target_extra"], item["target_extra_name"],
                )
                if prior != item["target_extra_name"]:
                    _fail()
            city_worker_tasks_by_ref[item["city_ref"]].append(item)
        for item in buckets["city_specialist"]:
            if item["city_ref"] not in city_by_ref:
                _fail()
            prior = specialist_names.setdefault(item["native_id"], item["name"])
            prior_default = specialist_defaults.setdefault(
                item["native_id"], item["is_default"],
            )
            prior_population_count = specialist_population_counts.setdefault(
                item["native_id"], item["counts_toward_population"],
            )
            if (
                prior != item["name"]
                or prior_default is not item["is_default"]
                or prior_population_count
                   is not item["counts_toward_population"]
            ):
                _fail()
            city_specialists_by_ref[item["city_ref"]].append(item)
        production_names: dict[tuple[str, int], str] = {}
        # Every city child catalog is an independent STATE_SCOPE section in the
        # native client: protocol_v2.c v2_build_state_scope_rows dispatches
        # "city_citizens", "city_build_choices", "city_worklist" and
        # "city_improvements" separately, and only "city_citizens" emits both
        # city_tile and city_specialist rows.  A bundle therefore routinely
        # carries one of them without the others, so a single "any child row is
        # present" flag makes the citizens catalog demand a worklist catalog
        # that was never requested -- the worker-task wedge one level up.
        # Guard each family by its own catalog, exactly like
        # ``full_tile_catalog`` above.  The strict per-section count identities
        # still run in ``_validate_state_scope_catalog``, which is where these
        # rows actually arrive today.
        # Evaluated PER CITY, not per bundle.  Every city_citizens page is
        # single-city by construction (_validate_state_scope_catalog rejects
        # any row whose city_ref is not the selector, and protocol_v2.c
        # resolves one pcity from that selector), so a bundle-global flag went
        # true for the whole bundle the moment one city's page was merged into
        # it -- and then every OTHER city failed "not city_tiles or not
        # specialists".  That is precisely the failure mode splitting the old
        # single have_city_children flag was meant to eliminate, one level up.
        # The strict per-section count identity in
        # _validate_state_scope_catalog is unaffected and remains the real
        # enforcement point for the single-city page.
        for city in buckets["city"]:
            key = (city["production_kind"], city["production_native_id"])
            prior = production_names.setdefault(key, city["production_name"])
            if prior != city["production_name"]:
                _fail()
        for item in buckets["city_build_choice"]:
            if item["city_ref"] not in city_by_ref:
                _fail()
            key = (item["production_kind"], item["production_native_id"])
            prior = production_names.setdefault(key, item["production_name"])
            if (
                prior != item["production_name"]
                or item["can_build_now"] and not item["can_queue"]
            ):
                _fail()
            city_build_choices_by_ref[item["city_ref"]].append(item)
        for item in buckets["city_worklist"]:
            if item["city_ref"] not in city_by_ref:
                _fail()
            key = (item["production_kind"], item["production_native_id"])
            prior = production_names.setdefault(key, item["production_name"])
            if prior != item["production_name"]:
                _fail()
            city_worklists_by_ref[item["city_ref"]].append(item)
        improvement_names: dict[int, str] = {}
        for item in buckets["city_improvement"]:
            if item["city_ref"] not in city_by_ref:
                _fail()
            prior = improvement_names.setdefault(item["native_id"], item["name"])
            if prior != item["name"]:
                _fail()
            production_name = production_names.get(
                ("improvement", item["native_id"]),
            )
            if production_name is not None and production_name != item["name"]:
                _fail()
            city_improvements_by_ref[item["city_ref"]].append(item)
        for item in buckets["city_rally"]:
            if item["city_ref"] not in city_by_ref:
                _fail()
            city_rallies_by_ref[item["city_ref"]].append(item)
        expected_specialist_ids: set[int] | None = None
        for ref, city in city_by_ref.items():
            city_tiles = city_tiles_by_ref[ref]
            specialists = city_specialists_by_ref[ref]
            worklist = city_worklists_by_ref[ref]
            build_choices = city_build_choices_by_ref[ref]
            improvements = city_improvements_by_ref[ref]
            rallies = city_rallies_by_ref[ref]
            ids = {item["native_id"] for item in specialists}
            population_ids = {
                item["native_id"] for item in specialists
                if item["counts_toward_population"]
            }
            choices_by_key = {
                (item["production_kind"], item["production_native_id"]): item
                for item in build_choices
            }
            worklist_keys = {
                (item["production_kind"], item["production_native_id"])
                for item in worklist
            }
            if city["options_conflict"] and city["new_citizens"] != "science":
                # protocol_v2.c v2_new_citizens_name() resolves the legacy
                # conflicting option bits science-first, so the conflict flag
                # can only accompany "science".
                _fail()
            if (city_tiles or specialists) and (
                not city_tiles or not specialists
                or len(city_tiles) != city["citizen_tile_count"]
                or len(specialists) != city["specialist_type_count"]
                or ids != set(range(len(ids)))
                or population_ids != set(range(len(population_ids)))
                or sum(item["free_worked"] for item in city_tiles) != 1
                or sum(
                    item["native_tile"] == city["native_tile"]
                    and item["worked"] and item["free_worked"]
                    for item in city_tiles
                ) != 1
                or any(
                    item["free_worked"] and not item["worked"]
                    for item in city_tiles
                )
                # common/city.c city_specialists() sums only
                # ``normal_specialist_type_iterate``, and client/packhand.c
                # handle_city_info() rebuilds size from the mood counters plus
                # exactly the normal specialists, so superspecialists are
                # outside "size" and must be excluded here.
                or sum(
                    item["worked"] and not item["free_worked"]
                    for item in city_tiles
                ) + sum(
                    item["count"] for item in specialists
                    if item["counts_toward_population"]
                )
                   != city["size"]
                or not _city_citizen_metrics_match(
                    city, city_tiles, specialists,
                )
                or sum(item["is_default"] for item in specialists) != 1
                or any(
                    item["is_default"]
                    and not item["counts_toward_population"]
                    for item in specialists
                )
                or any(
                    item["can_use"]
                    and not item["counts_toward_population"]
                    for item in specialists
                )
            ):
                _fail()
            if worklist and (
                len(worklist) != city["worklist_length"]
                or {item["position"] for item in worklist}
                   != set(range(city["worklist_length"]))
            ):
                _fail()
            if build_choices and (
                len(build_choices) != city["build_choice_count"]
            ):
                _fail()
            if improvements and (
                len(improvements) != city["improvement_count"]
                or city["did_sell"] and any(
                    item["sellable"] for item in improvements
                )
            ):
                _fail()
            # The cities catalog emits one FC_AGENT_V2_ROW_CITY_RALLY next to
            # every FC_AGENT_V2_ROW_CITY, back to back inside the same encode
            # branch (protocol_v2.c v2_build_city_state_rows), and `city` rows
            # reach a bundle only through the `cities` catalog, which carries
            # city_rally with them.  So the rally row travels with the city row
            # itself: it is not an independent scope and needs no catalog
            # guard.  Guarding on the bucket let a bundle with city rows and no
            # rally rows through here, and _project then indexed
            # native_city_rallies[ref] unguarded -- a bare KeyError out of
            # state_page, with no envelope, no code and no attribution, which
            # is strictly worse than a rejection.
            if len(rallies) != 1:
                _fail()
            # Cross-catalog identities need both catalogs in the bundle.
            if worklist and build_choices \
                    and (
                        any(
                            (
                                item["production_kind"],
                                item["production_native_id"],
                            ) not in choices_by_key
                            for item in worklist
                        )
                        or any(
                            not item["can_queue"]
                            and (
                                item["production_kind"],
                                item["production_native_id"],
                            ) not in worklist_keys
                            for item in build_choices
                        )
                    ):
                _fail()
            if specialists:
                if expected_specialist_ids is None:
                    expected_specialist_ids = ids
                elif ids != expected_specialist_ids:
                    _fail()
        own_units: dict[str, dict[str, Any]] = {}
        city_refs = {city["ref"] for city in buckets["city"]}
        unit_type_names: dict[int, str] = {}
        unit_type_stats: dict[int, Mapping[str, Any]] = {}
        unit_type_veteran_counts: dict[int, int] = {}
        veteran_levels: dict[tuple[int, int], tuple[str, int, int]] = {}
        own_unit_conversions: dict[int, tuple[int | None, str | None]] = {}
        for unit in buckets["unit"]:
            prior_name = unit_type_names.setdefault(
                unit["native_type_id"], unit["type"]
            )
            if prior_name != unit["type"]:
                _fail()
            prior_stats = unit_type_stats.setdefault(
                unit["native_type_id"], unit["type_stats"],
            )
            prior_veteran_count = unit_type_veteran_counts.setdefault(
                unit["native_type_id"], unit["veteran_levels"],
            )
            veteran_key = (unit["native_type_id"], unit["veteran"])
            veteran_value = (
                unit["veteran_name"], unit["veteran_power"],
                unit["veteran_move_bonus"],
            )
            prior_veteran = veteran_levels.setdefault(
                veteran_key, veteran_value,
            )
            if (
                prior_stats != unit["type_stats"]
                or prior_veteran_count != unit["veteran_levels"]
                or prior_veteran != veteran_value
            ):
                _fail()
            if unit["scope"] == "own":
                conversion = (
                    unit["converted_type_native_id"],
                    unit["converted_type"],
                )
                prior_conversion = own_unit_conversions.setdefault(
                    unit["native_type_id"], conversion
                )
                if (
                    self_ref is None
                    or unit["owner_ref"] != self_ref
                    or unit["home_ref"] is not None
                       and unit["home_ref"] not in city_refs
                    or prior_conversion != conversion
                ):
                    _fail()
                own_units[unit["ref"]] = unit
            elif self_ref is None or unit["owner_ref"] == self_ref:
                _fail()
        if buckets["unit_route_step"]:
            _fail()
        route_summaries = buckets["unit_route"]
        unique([item["unit_ref"] for item in route_summaries])
        ordered_unit_refs = {
            unit["ref"] for unit in own_units.values() if unit["has_orders"]
        }
        if {item["unit_ref"] for item in route_summaries} != ordered_unit_refs:
            _fail()
        for route in route_summaries:
            unit = own_units[route["unit_ref"]]
            if (
                route["order_index"] >= unit["order_count"]
                or route["step_count"] > unit["order_count"]
                or unit["orders_repeat"] and route["reconstructable"]
                   and route["step_count"] != unit["order_count"]
                or not unit["orders_repeat"] and route["reconstructable"]
                   and route["step_count"]
                       != unit["order_count"] - route["order_index"]
            ):
                _fail()
        for unit in own_units.values():
            converted_type_id = unit["converted_type_native_id"]
            if (
                converted_type_id != -1
                and converted_type_id in unit_type_names
                and unit_type_names[converted_type_id]
                    != unit["converted_type"]
            ):
                _fail()

        all_units = {unit["ref"]: unit for unit in buckets["unit"]}
        allied_player_refs = {
            relation["other_ref"] for relation in buckets["diplomacy"]
            if relation["state"] in {"Alliance", "Team"}
        }
        carried_counts: Counter[str] = Counter()
        for unit in own_units.values():
            transporter_ref = unit["transporter_ref"]
            if unit["transport_state"] == "transported":
                transporter = all_units.get(transporter_ref)
                if (
                    transporter is None
                    or transporter is unit
                    or transporter["native_tile"] != unit["native_tile"]
                    or transporter["scope"] == "visible" and (
                        transporter["owner_ref"] not in allied_player_refs
                    )
                    or transporter["scope"] == "own" and (
                        transporter["transport_state"] == "unresolved"
                        or transporter["transport_capacity"] <= 0
                    )
                ):
                    _fail()
                if transporter["scope"] == "own":
                    carried_counts[transporter_ref] += 1
        for unit in own_units.values():
            if unit["transport_state"] != "unresolved" and (
                unit["occupied"] < carried_counts[unit["ref"]]
                or unit["occupied"] > unit["transport_capacity"]
            ):
                _fail()
            visited: set[str] = set()
            cursor = unit
            while cursor["transport_state"] == "transported":
                if cursor["ref"] in visited:
                    _fail()
                visited.add(cursor["ref"])
                next_unit = all_units.get(cursor["transporter_ref"])
                if next_unit is None:
                    _fail()
                if next_unit["scope"] == "visible":
                    break
                cursor = next_unit

        slots = [item["slot"] for item in buckets["action"]]
        unique(slots)
        meta = buckets["meta"][0]
        chats = buckets["chat"]
        if len(chats) > MAX_CHAT_HISTORY:
            _fail()
        unique([item["sequence"] for item in chats])
        if any(
            item["sequence"] == 0
            or item["turn"] > meta["turn"]
            # Freeciv uses phase_count as a turn-boundary sentinel while
            # emitting end-of-turn and next-year events.
            or item["phase"] > meta["phase_count"]
            or item["self"] and item["sender"] == "server"
            for item in chats
        ):
            _fail()
        if meta["phase_mode"] == "concurrent" and (
            meta["phase_count"] != 1 or meta["phase"] != 0
        ):
            _fail()
        if player is None:
            if meta["active_phase"] or meta["phase_ready"]:
                _fail()
        elif meta["phase_mode"] == "players_alternate":
            expected_active = player["parsed_ref"][1] == meta["phase"]
            if meta["active_phase"] is not expected_active:
                _fail()
        elif meta["phase_mode"] == "concurrent" and not meta["active_phase"]:
            _fail()

        action_eligible = (
            player is not None
            and player["alive"]
            and meta["active_phase"]
            and not player["phase_done"]
            and meta["state"] == "running"
        )
        ordinary_actions = [
            item for item in buckets["action"]
            if _ACTION_RULES.get(item["native_rule"]) is None
            or _ACTION_RULES[item["native_rule"]].operation not in {
                "send_chat", "cast_vote", "cancel_vote",
                "propose_server_setting", "surrender",
            }
        ]
        if ordinary_actions and not action_eligible:
            _fail()
        phase_end_count = sum(
            item["native_rule"] == "phase.end"
            for item in buckets["action"]
        )
        if phase_end_count > 1:
            _fail()
        if meta["phase_mode"] == "players_alternate" and (
            meta["phase_ready"] is not (phase_end_count == 1)
        ):
            _fail()
        if meta["phase_ready"] and not action_eligible:
            _fail()
        unknown_move_targets: set[int] = set()
        unknown_targets_by_actor: dict[str, set[int]] = {}
        research_target_actions: set[int] = set()
        research_goal_actions: set[int] = set()
        rates_action_count = 0
        chat_action_count = 0
        vote_action_nos: set[int] = set()
        cancel_vote_action_nos: set[int] = set()
        setting_action_keys: set[tuple[int, str, int]] = set()
        surrender_action_count = 0
        for action in buckets["action"]:
            rule = _ACTION_RULES.get(action["native_rule"])
            if rule is None or (
                action["native_kind"] != rule.native_kind
                or action["target_kind"] != rule.target_kind
                or action["result"] != rule.result
                or action["consuming"] is not rule.consuming
                or action["args"] != rule.args
                or not _non_special_action_metadata_supported(action)
            ):
                _fail()
            self._validate_probability(action)
            is_setting = rule.operation == "propose_server_setting"
            if (
                (
                    is_setting
                    and not self._setting_action_is_well_formed(action, rule)
                )
                or (
                    not is_setting
                    and not self._setting_fields_are_empty(action)
                )
            ):
                _fail()
            if (
                action["counterpart_ref"] is not None
                or action["meeting_generation"] != 0
                or action["clauses_digest"]
                   != "fnv1a64-0000000000000000"
                or action["self_accepted"]
                or action["other_accepted"]
                or action["relation_state"] != "none"
                or action["outgoing_vision"]
                or action["outgoing_shared_tiles"]
                or action["clause_giver_ref"] is not None
                or action["clause_type"] != "none"
                or action["native_clause_value"] != -1
                or action["clause_name"] != "none"
                or action["desired_acceptance"] != -1
            ):
                _fail()
            if rule.operation == "cast_vote":
                vote = vote_by_no.get(action["native_vote_no"])
                if (
                    player is None or vote is None or not vote["can_vote"]
                    or action["actor_ref"] != player["ref"]
                    or action["native_vote_no"] in vote_action_nos
                    or action["native_target_tile"] != -1
                    or action["native_target_tech"] != -1
                    or action["native_target_government"] != -1
                    or action["target_name"] != "vote"
                    or action["max_rate"] != 0
                    or action["target_build_kind"] != "none"
                    or action["native_target_build"] != -1
                    or action["native_target_multiplier"] != -1
                    or action["native_source_specialist"] != -1
                    or action["native_target_specialist"] != -1
                    or action["native_target_extra"] != -1
                    or action["activity"] != "none"
                ):
                    _fail()
                vote_action_nos.add(action["native_vote_no"])
                continue
            if rule.operation == "cancel_vote":
                vote = vote_by_no.get(action["native_vote_no"])
                if (
                    meta["state"] != "running"
                    or player is None or vote is None
                    or action["actor_ref"] != player["ref"]
                    or action["native_vote_no"] in cancel_vote_action_nos
                    or action["target_name"] != "own vote"
                    or not self._governance_targets_are_empty(action)
                ):
                    _fail()
                cancel_vote_action_nos.add(action["native_vote_no"])
                continue
            if action["native_vote_no"] != -1:
                _fail()
            if rule.operation == "propose_server_setting":
                setting_key = (
                    action["native_server_setting_id"],
                    action["server_setting_type"],
                    action["server_setting_value"],
                )
                if (
                    meta["state"] != "running"
                    or player is None or action["actor_ref"] != player["ref"]
                    or action["target_name"] == "none"
                    or not self._governance_targets_are_empty(action)
                    or setting_key in setting_action_keys
                ):
                    _fail()
                setting_action_keys.add(setting_key)
                continue
            if rule.operation == "surrender":
                surrender_action_count += 1
                if (
                    meta["state"] != "running"
                    or player is None or not player["alive"]
                    or action["actor_ref"] != player["ref"]
                    or action["target_name"] != "self"
                    or not self._governance_targets_are_empty(action)
                    or surrender_action_count > 1
                ):
                    _fail()
                continue
            # City, worker, and government capabilities are actor-scoped
            # only.  The complete global catalog retains its original grammar
            # and must carry exact sentinels in every new target field.
            if (
                rule.operation in {
                    "set_production", "buy_production", "start_activity",
                    "cancel_activity", "revolution", "change", "board",
                    "set_multiplier",
                    "deboard", "embark", "disembark", "load", "unload",
                    "work_tile", "unwork_tile", "set_specialist",
                    "set_worklist", "set_options", "rename",
                    "sell_improvement", "set_governor", "clear_governor",
                    "upgrade", "rehome", "join_city", "establish_trade",
                    "marketplace", "help_wonder", "disband_recover",
                    "set_route", "attack_route", "place_infrastructure",
                }
                or action["target_unit_ref"] is not None
                or action["transport_context_ref"] is not None
                or action["source_city_ref"] is not None
                or action["destination_city_ref"] is not None
                or action["native_target_government"] != -1
                or action["target_build_kind"] != "none"
                or action["native_target_build"] != -1
                or action["spaceship_part"] != "none"
                or action["spaceship_value"] != -1
                or action["native_target_multiplier"] != -1
                or action["multiplier_value"] != -1
                or action["native_source_specialist"] != -1
                or action["native_target_specialist"] != -1
                or action["native_target_extra"] != -1
                or action["activity"] != "none"
                or action["target_name"] != "none"
                or action["route_waypoint_limit"] != 0
                or action["infrastructure_cost"] != 0
                or action["infrastructure_turns"] != 0
                or action["infrastructure_choice_count"] != 0
                or action["infrastructure_choices"]
            ):
                _fail()
            if rule.native_kind == "phase.end":
                if (
                    action["actor_ref"] is not None
                    or action["native_target_tile"] != -1
                    or action["native_target_tech"] != -1
                    or action["max_rate"] != 0
                ):
                    _fail()
            elif rule.native_kind == "player.send_chat":
                if (
                    player is None
                    or action["actor_ref"] != player["ref"]
                    or action["native_target_tile"] != -1
                    or action["native_target_tech"] != -1
                    or action["max_rate"] != 0
                ):
                    _fail()
                chat_action_count += 1
                if chat_action_count > 1:
                    _fail()
            elif rule.native_kind in {
                "research.set_target", "research.set_goal",
            }:
                target_tech = tech_by_id.get(action["native_target_tech"])
                if (
                    research is None
                    or action["actor_ref"] is not None
                    or action["native_target_tile"] != -1
                    or action["max_rate"] != 0
                    or target_tech is None
                ):
                    _fail()
                if rule.operation == "set_target":
                    if (
                        not target_tech["can_target"]
                        or action["native_target_tech"]
                        == research["target_native_id"]
                    ):
                        _fail()
                    if action["native_target_tech"] in research_target_actions:
                        _fail()
                    research_target_actions.add(action["native_target_tech"])
                elif (
                    not target_tech["can_goal"]
                    or action["native_target_tech"]
                    == research["goal_native_id"]
                ):
                    _fail()
                else:
                    if action["native_target_tech"] in research_goal_actions:
                        _fail()
                    research_goal_actions.add(action["native_target_tech"])
            elif rule.native_kind == "economy.set_rates":
                if (
                    player is None
                    or action["actor_ref"] is not None
                    or action["native_target_tile"] != -1
                    or action["native_target_tech"] != -1
                    or not player["changeable_tax"]
                    or action["max_rate"] != player["max_rate"]
                ):
                    _fail()
                rates_action_count += 1
                if rates_action_count > 1:
                    _fail()
            else:
                if action["native_target_tech"] != -1 or action["max_rate"] != 0:
                    _fail()
                actor = own_units.get(action["actor_ref"])
                # Deliberately NOT guarded by full_tile_catalog, unlike the
                # worker-task check above: tile-targeted action rows never
                # appear in the compact OBS (its builder emits only global
                # player/communication actions), and scoped payloads bundle
                # every referenced target tile by construction
                # (protocol_v2.c v2_emit_actor_scope, actor_target_tiles).
                # A tile-targeted action without its tile is therefore a
                # genuine contract fault, not a catalog-scope artifact.
                target = tile_by_index.get(action["native_target_tile"])
                if actor is None or target is None:
                    _fail()
                if rule.operation in {"move", "attack", "suicide_attack"} \
                        and actor["moves"] <= 0:
                    _fail()
                if target["known"] == 0:
                    if (
                        rule.operation != "move"
                        or action["probability_kind"] != "unknown"
                    ):
                        _fail()
                    unknown_move_targets.add(target["native_index"])
                    actor_targets = unknown_targets_by_actor.setdefault(
                        actor["ref"], set(),
                    )
                    actor_targets.add(target["native_index"])
                    if len(actor_targets) > 8:
                        _fail()
                elif rule.operation in {"attack", "suicide_attack", "found_city"} \
                        and target["known"] != 2:
                    _fail()
                if rule.operation == "found_city" and actor[
                    "native_tile"
                ] != action["native_target_tile"]:
                    _fail()
        if {
            tile["native_index"] for tile in tiles if tile["known"] == 0
        } != unknown_move_targets:
            _fail()
        if vote_action_nos != {
            item["native_vote_no"] for item in votes if item["can_vote"]
        }:
            _fail()
        expected_targets: set[int] = set()
        expected_goals: set[int] = set()
        if action_eligible and research is not None:
            expected_targets = {
                tech["native_id"] for tech in techs
                if tech["can_target"]
                and tech["native_id"] != research["target_native_id"]
            }
            expected_goals = {
                tech["native_id"] for tech in techs
                if tech["can_goal"]
                and tech["native_id"] != research["goal_native_id"]
            }
        expected_rates = (
            1 if action_eligible and player is not None
            and player["changeable_tax"] else 0
        )
        # Freeciv gates the phase-scoped player catalog on
        # ``v2_actions_ready`` (protocol_v2.c), i.e.
        # ``fc_agent_v2_action_phase_ready`` in protocol_v2_codec.c, which is
        # strictly stronger than anything these rows expose: it also demands
        # ``can_client_issue_orders()`` and ``!is_server_busy()``.
        # ``server_busy`` is latched by handle_end_turn() and only cleared by
        # handle_start_phase() for phase 0 (client/packhand.c), so across every
        # turn change -- and for every non-zero phase of an alternating-phase
        # game -- the native client legitimately emits an empty research and
        # rates catalog while this seat still looks eligible in the rows.
        # Demanding the full catalog there rejects every observation taken at a
        # turn boundary.  Accept the complete catalog or the wholly withheld
        # one; a partial catalog, or any of these actions once the seat is
        # ineligible, is still a genuine contract fault.
        #
        # The withheld case has an exact discriminator already in the bundle,
        # and it must be used rather than accepting emptiness unconditionally.
        # ``meta.phase_ready`` is ``can_end_turn()``
        # (client/mapctrl_common.c), which also requires ``!is_server_busy()``
        # -- so at exactly the turn boundary this relaxation exists for,
        # ``phase_ready`` is 0.  Conversely ``can_end_turn()`` implies every
        # conjunct of ``v2_actions_ready()`` that gates the catalog, and
        # ``v2_refresh()`` publishes no rows at all unless the seat is
        # authorized and the cache coherent -- so whenever an observation
        # exists AND ``phase_ready`` is 1, the native MUST have emitted the
        # complete catalog.  Accepting an empty one there would silently brick
        # research and taxation for the rest of the game, with no rejection
        # receipt and no attribution.
        catalog_withheld = (
            not research_target_actions and not research_goal_actions
            and rates_action_count == 0
        )
        if not (catalog_withheld and not meta["phase_ready"]) and (
            research_target_actions != expected_targets
            or research_goal_actions != expected_goals
            or rates_action_count != expected_rates
        ):
            _fail()

    @staticmethod
    def _validate_probability(action: Mapping[str, Any]) -> None:
        kind = action["probability_kind"]
        minimum = action["probability_min"]
        maximum = action["probability_max"]
        legality = action["legality"]
        if kind == "not_implemented":
            valid = minimum == maximum == -1 and legality == "unresolved"
        elif kind == "unknown":
            valid = minimum == 0 and maximum == 200 and legality == "possibly_legal"
        elif kind == "exact":
            valid = (
                0 < minimum == maximum <= 200 and legality == "legal"
            )
        else:
            valid = (
                0 <= minimum < maximum <= 200 and legality == "possibly_legal"
            )
        if not valid:
            _fail()

    def _project(
        self,
        native_revision: int,
        row_digest: str,
        parsed: _ParsedObservation,
    ) -> _ProjectedSnapshot:
        meta = parsed.meta
        if meta["state"] == "preparing":
            return self._project_pregame(
                native_revision, row_digest, parsed,
            )
        state_revision = {
            "turn": meta["turn"],
            "revision": native_revision,
            "state_token": self._mac(
                "state", "state", self.generation, native_revision, row_digest,
            ),
        }
        vote_items = [{
            "vote_ref": self._mac(
                "vote_ref", "vote", self.generation,
                item["native_vote_no"],
            ),
            "vote_id": self._mac(
                "vote", "vote", native_revision, item["native_vote_no"],
            ),
            "caller": item["caller"],
            "description": item["description"],
            "yes": item["yes"],
            "no": item["no"],
            "abstain": item["abstain"],
            "num_voters": item["num_voters"],
            "percent_required": item["percent_required"],
            "team_only": item["team_only"],
            "current_vote": item["current_vote"],
            "can_vote": item["can_vote"],
            "status": item["status"],
            "outcome_turn": item["outcome_turn"],
            "outcome_phase": item["outcome_phase"],
        } for item in sorted(
            parsed.votes, key=lambda value: value["native_vote_no"],
        )]
        vote_by_native = {
            native["native_vote_no"]: public
            for native, public in zip(
                sorted(parsed.votes, key=lambda value: value["native_vote_no"]),
                vote_items,
                strict=True,
            )
        }
        player_id = None
        public_player = None
        if parsed.player is not None:
            if parsed.governance is None:
                _fail()
            player_id = self._entity_id("player", parsed.player["ref"])
            public_player = {
                "id": player_id,
                "name": parsed.player["name"],
                "nation": parsed.player["nation"],
                "government": parsed.player["government"],
                "government_state": {
                    "current_id": self._government_id(
                        parsed.governance["current_native_id"],
                    ),
                    "target_id": (
                        self._government_id(
                            parsed.governance["target_native_id"],
                        )
                        if parsed.governance["target_native_id"] != -1
                        else None
                    ),
                    "during_revolution_id": self._government_id(
                        parsed.governance["during_native_id"],
                    ),
                    "status": parsed.governance["status"],
                    "finish_turn": (
                        parsed.governance["finish_turn"]
                        if parsed.governance["finish_turn"] >= 0 else None
                    ),
                    "turns_remaining": parsed.governance["turns_remaining"],
                    "method": parsed.governance["method"],
                    "max_turns": parsed.governance["max_turns"],
                    "untargeted_allowed": parsed.governance[
                        "untargeted_allowed"
                    ],
                    "no_anarchy": parsed.governance["no_anarchy"],
                    "can_revolution": parsed.governance["can_revolution"],
                },
                "alive": parsed.player["alive"],
                "phase_done": parsed.player["phase_done"],
                "economy": {
                    "gold": parsed.player["gold"],
                    "tax": parsed.player["tax"],
                    "science": parsed.player["science"],
                    "luxury": parsed.player["luxury"],
                    "changeable_tax": parsed.player["changeable_tax"],
                    "max_rate": parsed.player["max_rate"],
                },
                "infrastructure": {
                    "enabled": parsed.player["infrastructure_enabled"],
                    "points": parsed.player["infrastructure_points"],
                },
            }
        multiplier_items = [{
            "id": self._multiplier_id(item["native_id"]),
            "name": item["name"],
            "value": item["value"],
            "target": item["target"],
            "start": item["start"],
            "stop": item["stop"],
            "step": item["step"],
            "minimum_turns": item["minimum_turns"],
            "changed_turn": item["changed_turn"],
            "can_change": item["can_change"],
            "choice_count": item["choice_count"],
        } for item in sorted(
            parsed.multipliers, key=lambda value: value["native_id"],
        )]
        public_spaceship = None
        if parsed.spaceship is not None:
            ship = parsed.spaceship
            public_spaceship = {
                "state": ship["state"],
                "inventory": {
                    "structurals": ship["structurals"],
                    "structurals_placed": ship["structurals_placed"],
                    "components": ship["components"],
                    "components_placed": ship["fuel"] + ship["propulsion"],
                    "modules": ship["modules"],
                    "modules_placed": (
                        ship["habitation"] + ship["life_support"]
                        + ship["solar_panels"]
                    ),
                },
                "placed": {
                    "fuel": ship["fuel"],
                    "propulsion": ship["propulsion"],
                    "habitation": ship["habitation"],
                    "life_support": ship["life_support"],
                    "solar_panels": ship["solar_panels"],
                },
                "launch_year": (
                    ship["launch_year"] if ship["state"] in {
                        "launched", "arrived",
                    } else None
                ),
                "population": ship["population"],
                "mass": ship["mass"],
                "support_rate": ship["support_permille"] / 1000,
                "energy_rate": ship["energy_permille"] / 1000,
                "success_rate": ship["success_permille"] / 1000,
                "travel_time": ship["travel_time_millis"] / 1000,
                "has_capital": ship["has_capital"],
                "can_launch": ship["can_launch"],
                "structural_slots": [{
                    "id": self._spaceship_slot_id(item["native_slot"]),
                    "x": item["x"],
                    "y": item["y"],
                    "required_slot_id": (
                        self._spaceship_slot_id(item["required_native_slot"])
                        if item["required_native_slot"] != -1 else None
                    ),
                    "placed": item["placed"],
                    "required_connected": item["required_connected"],
                    "can_place": item["can_place"],
                } for item in sorted(
                    parsed.spaceship_structurals,
                    key=lambda value: value["native_slot"],
                )],
            }
        public_research = None
        if parsed.research is not None:
            public_research = {
                "techs_researched": parsed.research["techs"],
                "future_tech": parsed.research["future"],
                "target": parsed.research["target"],
                "target_id": self._tech_id(
                    parsed.research["target_native_id"],
                ),
                "goal": parsed.research["goal"],
                "goal_id": self._tech_id(
                    parsed.research["goal_native_id"],
                ),
                "bulbs_researched": parsed.research["bulbs"],
                "cost": parsed.research["cost"],
                "output": parsed.research["output"],
            }

        choice_by_native = {
            item["native_id"]: item for item in parsed.research_techs
        }
        prerequisites: dict[int, list[Mapping[str, Any]]] = defaultdict(list)
        for edge in parsed.research_edges:
            prerequisites[edge["tech_native_id"]].append(edge)
        unlocks: dict[int, list[Mapping[str, Any]]] = defaultdict(list)
        for unlock in parsed.research_unlocks:
            unlocks[unlock["tech_native_id"]].append(unlock)
        research_items = []
        research_by_native: dict[int, dict[str, Any]] = {}
        for graph_tech in sorted(
            parsed.research_graph, key=lambda item: item["native_id"],
        ):
            choice = choice_by_native.get(graph_tech["native_id"])
            public = {
                "id": self._tech_id(graph_tech["native_id"]),
                "name": graph_tech["name"],
                "state": (
                    choice["state"] if choice is not None else "unreachable"
                ),
                "reachable": graph_tech["reachable"],
                "can_target": (
                    choice["can_target"] if choice is not None else False
                ),
                "can_goal": (
                    choice["can_goal"] if choice is not None else False
                ),
                "next_step_id": (
                    self._tech_id(graph_tech["next_step_native_id"])
                    if graph_tech["next_step_native_id"] != -1 else None
                ),
                "unknown_prerequisite_count": graph_tech[
                    "unknown_prerequisites"
                ],
                "path_cost": graph_tech["path_cost"],
                "prerequisites": [{
                    "id": self._tech_id(edge["prerequisite_native_id"]),
                    "kind": edge["kind"],
                } for edge in sorted(
                    prerequisites[graph_tech["native_id"]],
                    key=lambda item: (
                        item["kind"], item["prerequisite_native_id"],
                    ),
                )],
                "unlocks": [{
                    "id": self._research_unlock_id(
                        unlock["kind"], unlock["native_id"],
                    ),
                    "kind": unlock["kind"],
                    "name": unlock["name"],
                    "scope": unlock["scope"],
                } for unlock in sorted(
                    unlocks[graph_tech["native_id"]],
                    key=lambda item: (
                        item["kind"], item["native_id"], item["scope"],
                    ),
                )],
            }
            research_items.append(public)
            research_by_native[graph_tech["native_id"]] = public
        for choice in parsed.research_techs:
            if choice["state"] not in {"future", "unset"}:
                continue
            public = {
                "id": self._tech_id(choice["native_id"]),
                "name": choice["name"],
                "state": choice["state"],
                "reachable": choice["state"] == "future",
                "can_target": choice["can_target"],
                "can_goal": choice["can_goal"],
                "next_step_id": None,
                "unknown_prerequisite_count": None,
                "path_cost": None,
                "prerequisites": [],
                "unlocks": [],
            }
            research_items.append(public)
            research_by_native[choice["native_id"]] = public
        research_choice_order = {
            choice["native_id"]: position
            for position, choice in enumerate(parsed.research_techs)
        }
        research_items.sort(key=lambda item: (
            research_choice_order.get(
                next(
                    native_id for native_id, candidate
                    in research_by_native.items() if candidate is item
                ),
                len(research_choice_order),
            ),
            item["name"],
        ))
        if public_research is not None:
            goal_graph = next((
                item for item in parsed.research_graph
                if item["native_id"] == parsed.research["goal_native_id"]
            ), None)
            public_research["next_goal_step_id"] = (
                self._tech_id(goal_graph["next_step_native_id"])
                if goal_graph is not None
                and goal_graph["next_step_native_id"] != -1 else None
            )
        government_items = [{
            "id": self._government_id(item["native_id"]),
            "name": item["name"],
            "current": item["current"],
            "target": item["target"],
            "during_revolution": item["during"],
            "can_change": item["can_change"],
        } for item in parsed.governments]
        diplomacy_items = []
        diplomacy_by_ref: dict[str, dict[str, Any]] = {}
        relation_bindings: dict[str, _RelationBinding] = {}
        diplomacy_intel_by_ref = {
            item["other_ref"]: item for item in parsed.diplomacy_intel
        }
        for item in parsed.diplomacy:
            intel = diplomacy_intel_by_ref.get(item["other_ref"])
            relation_id = self._mac("relation", "relation", item["other_ref"])
            meeting_id = (
                self._mac(
                    "meeting", "meeting", item["other_ref"],
                    item["generation"],
                )
                if item["meeting"] else None
            )
            public = {
                "relation_id": relation_id,
                "player_id": self._entity_id("player", item["other_ref"]),
                "player_name": item["name"],
                "nation": item["nation"],
                "alive": item["alive"],
                "intel_level": item["intel_level"],
                "team": {
                    "id": (
                        self._team_id(item["team_native_id"])
                        if item["team_native_id"] != -1 else None
                    ),
                    "name": (
                        item["team_name"]
                        if item["team_native_id"] != -1 else None
                    ),
                    "same_as_self": item["same_team"],
                },
                "controller": {
                    "kind": item["controller"],
                    "connected": item["connected"],
                },
                "score": item["score"],
                "gold": item["gold"],
                "government": item["government"],
                "rates": ({
                    "tax": intel["tax"],
                    "science": intel["science"],
                    "luxury": intel["luxury"],
                } if intel is not None else None),
                "culture": intel["culture"] if intel is not None else None,
                "research": ({
                    "id": self._tech_id(intel["research_native_id"]),
                    "name": intel["research_name"],
                    "bulbs_researched": intel["bulbs"],
                    "cost": intel["cost"],
                } if intel is not None else None),
                "known_techs": ([{
                    "id": self._tech_id(native_id),
                    "name": next(
                        graph_tech["name"]
                        for graph_tech in parsed.research_graph
                        if graph_tech["native_id"] == native_id
                    ),
                } for native_id in intel["known_native_ids"]]
                    if intel is not None else None),
                "known_techs_digest": (
                    self._mac(
                        "techset", "known", item["other_ref"],
                        intel["known_digest"],
                    ) if intel is not None else None
                ),
                "state": item["state"],
                "contact_turns_left": item["contact"],
                "treaty_turns_left": item["turns_left"],
                "can_open_meeting": item["can_meet"],
                "can_break_relation": item["can_cancel"],
                "cancel_relation": {
                    "allowed": item["can_cancel"],
                    "reason": item["cancel_reason"],
                },
                "has_embassy": item["has_embassy"],
                "other_has_embassy": item["other_has_embassy"],
                "gives_vision": item["gives_vision"],
                "receives_vision": item["receives_vision"],
                "gives_shared_tiles": item["gives_shared_tiles"],
                "receives_shared_tiles": item["receives_shared_tiles"],
                "meeting": (
                    {
                        "meeting_id": meeting_id,
                        "generation": item["generation"],
                        "self_accepted": item["self_accepted"],
                        "other_accepted": item["other_accepted"],
                        "clause_count": item["clause_count"],
                        "clauses_token": self._mac(
                            "treaty", "clauses", item["other_ref"],
                            item["generation"], item["clauses_digest"],
                        ),
                    }
                    if item["meeting"] else None
                ),
            }
            diplomacy_items.append(public)
            diplomacy_by_ref[item["other_ref"]] = public
            relation_bindings[relation_id] = _RelationBinding(
                native_counterpart_ref=item["other_ref"],
                counterpart_player_id=public["player_id"],
            )
        diplomacy_clause_items = []
        city_site_by_number = {
            item["parsed_ref"][1]: item for item in parsed.city_sites
        }
        self_ref = parsed.player["ref"] if parsed.player is not None else None
        for item in sorted(
            parsed.diplomacy_clauses,
            key=lambda value: (
                value["other_ref"], value["generation"], value["position"],
            ),
        ):
            relation = diplomacy_by_ref[item["other_ref"]]
            meeting_id = relation["meeting"]["meeting_id"]
            giver_is_self = item["giver_ref"] == self_ref
            other_player_id = relation["player_id"]
            self_player_id = (
                self._entity_id("player", self_ref)
                if self_ref is not None else None
            )
            value: dict[str, Any] | None = None
            if item["value_kind"] == "technology":
                value = {
                    "type": "technology",
                    "id": self._tech_id(item["native_value"]),
                    "name": item["name"],
                }
            elif item["value_kind"] == "gold":
                value = {"type": "gold", "amount": item["native_value"]}
            elif item["value_kind"] == "city":
                site = city_site_by_number[item["native_value"]]
                value = {
                    "type": "city",
                    "id": self._entity_id("city", site["ref"]),
                    "name": site["name"],
                }
            elif item["value_kind"] == "city_unavailable":
                value = {
                    "type": "city",
                    "id": self._mac(
                        "city", "unavailable-treaty-city",
                        item["other_ref"], item["generation"],
                        item["giver_ref"], item["native_value"],
                    ),
                    "name": "Unavailable city",
                    "available": False,
                }
            diplomacy_clause_items.append({
                "clause_id": self._mac(
                    "clause", "clause", item["other_ref"],
                    item["generation"], item["giver_ref"], item["clause_type"],
                    item["native_value"],
                ),
                "relation_id": relation["relation_id"],
                "meeting_id": meeting_id,
                "position": item["position"],
                "type": _DIPLOMACY_CLAUSE_PUBLIC_TYPES[
                    item["clause_type"]
                ],
                "giver_player_id": (
                    self_player_id if giver_is_self else other_player_id
                ),
                "receiver_player_id": (
                    other_player_id if giver_is_self else self_player_id
                ),
                "value": value,
            })
        tile_items = []
        for item in parsed.tiles:
            public_tile = {
                "id": self._tile_id(item["native_index"]),
                "x": item["x"],
                "y": item["y"],
                "visibility": (
                    "unknown" if item["known"] == 0
                    else "remembered" if item["known"] == 1
                    else "visible"
                ),
            }
            if item["known"] != 0:
                public_tile["terrain"] = item["terrain"]
                public_tile["owner_player_id"] = (
                    self._entity_id("player", item["owner_ref"])
                    if item["owner_ref"] is not None else None
                )
                public_tile["infrastructure_placement"] = ({
                    "extra_id": self._extra_id(item["placing_extra"]),
                    "name": item["placing_extra_name"],
                    "turns_remaining": item["placing_turns"],
                } if item["placing_extra"] != -1 else None)
            tile_items.append(public_tile)
        infrastructure_items = [{
            "extra_id": self._extra_id(item["native_id"]),
            "name": item["name"],
            "cost": item["cost"],
            "build_time": item["build_time"],
            "build_time_factor": item["build_time_factor"],
        } for item in parsed.infrastructure_extras]
        city_site_items = [{
            "id": self._entity_id("city", item["ref"]),
            "owner_player_id": self._entity_id("player", item["owner_ref"]),
            "name": item["name"],
            "tile_id": self._tile_id(item["native_tile"]),
            "x": item["x"],
            "y": item["y"],
            "size": item["size"],
            "visibility": item["visibility"],
        } for item in parsed.city_sites]
        native_city_tiles: dict[str, list[Mapping[str, Any]]] = {}
        native_city_worker_tasks: dict[str, list[Mapping[str, Any]]] = {}
        native_city_specialists: dict[str, list[Mapping[str, Any]]] = {}
        native_city_worklists: dict[str, list[Mapping[str, Any]]] = {}
        native_city_build_choices: dict[str, list[Mapping[str, Any]]] = {}
        native_city_improvements: dict[str, list[Mapping[str, Any]]] = {}
        native_city_rallies: dict[str, Mapping[str, Any]] = {}
        for item in parsed.city_tiles:
            native_city_tiles.setdefault(item["city_ref"], []).append(item)
        for item in parsed.city_worker_tasks:
            native_city_worker_tasks.setdefault(
                item["city_ref"], [],
            ).append(item)
        for item in parsed.city_specialists:
            native_city_specialists.setdefault(item["city_ref"], []).append(item)
        for item in parsed.city_worklists:
            native_city_worklists.setdefault(item["city_ref"], []).append(item)
        for item in parsed.city_build_choices:
            native_city_build_choices.setdefault(
                item["city_ref"], [],
            ).append(item)
        for item in parsed.city_improvements:
            native_city_improvements.setdefault(
                item["city_ref"], [],
            ).append(item)
        for item in parsed.city_rallies:
            native_city_rallies[item["city_ref"]] = item
        city_items = []
        city_detail_items = []
        citizen_anomalies: list[str] = []
        city_citizen_items = []
        city_worker_task_items = []
        city_worklist_items = []
        city_build_choice_items = []
        city_improvement_items = []
        for item in parsed.cities:
            citizen_tiles = sorted(
                native_city_tiles.get(item["ref"], []),
                key=lambda value: value["native_tile"],
            )
            specialists = sorted(
                native_city_specialists.get(item["ref"], []),
                key=lambda value: value["native_id"],
            )
            worker_tasks = sorted(
                native_city_worker_tasks.get(item["ref"], []),
                key=lambda value: value["native_tile"],
            )
            worklist = sorted(
                native_city_worklists.get(item["ref"], []),
                key=lambda value: value["position"],
            )
            build_choices = sorted(
                native_city_build_choices.get(item["ref"], []),
                key=lambda value: (
                    value["production_kind"],
                    value["production_native_id"],
                ),
            )
            improvements = sorted(
                native_city_improvements.get(item["ref"], []),
                key=lambda value: value["native_id"],
            )
            rally = native_city_rallies[item["ref"]]
            city_id = self._entity_id("city", item["ref"])
            production = {
                "id": self._production_id(
                    item["production_kind"], item["production_native_id"],
                ),
                "kind": item["production_kind"],
                "name": item["production_name"],
                "shield_stock": item["shield_stock"],
                "shield_cost": item["shield_cost"],
                "buy_cost": item["buy_cost"],
                "can_buy": item["can_buy"],
                "can_change": item["can_change"],
            }
            airlift = {
                "remaining": item["airlift_remaining"],
                "maximum": item["airlift_max"],
            }
            summary = {
                "id": city_id,
                "owner_player_id": player_id,
                "name": item["name"],
                "tile_id": self._tile_id(item["native_tile"]),
                "x": item["x"],
                "y": item["y"],
                "size": item["size"],
                "surplus": {
                    "food": item["food"], "shields": item["shields"],
                    "trade": item["trade"],
                },
                "production": production,
                "airlift": airlift,
                "trade_routes": {
                    "count": item["trade_route_count"],
                    "capacity": item["trade_route_capacity"],
                },
                "governor_enabled": item["governor_enabled"],
            }
            city_items.append(summary)
            if not item["citizen_counts_consistent"]:
                # Named, counted and visible rather than silent -- and rather
                # than fatal.  The seat keeps playing; the fault has an owner.
                citizen_anomalies.append(city_id)
            city_detail_items.append({
                **summary,
                "citizens": dict(item["citizen_counts"]),
                # False only after a server/client citizen desync that the
                # native client self-healed by overriding city size.  The
                # mood breakdown below is then not a partition of the
                # non-specialist citizens and must not be arithmetic on.
                "citizen_counts_consistent": item["citizen_counts_consistent"],
                "food_storage": dict(item["food_storage"]),
                "pollution": item["pollution"],
                "outputs": {
                    ("shields" if output == "shield" else output): {
                        **dict(metrics),
                        "gross": (
                            metrics["net"] + metrics["waste"]
                            + metrics["unhappy_penalty"]
                        ),
                    }
                    for output, metrics in item["outputs"].items()
                },
                "counts": {
                    "citizen_tiles": item["citizen_tile_count"],
                    "specialist_types": item["specialist_type_count"],
                    "worklist": item["worklist_length"],
                    "build_choices": item["build_choice_count"],
                    "improvements": item["improvement_count"],
                    "trade_routes": item["trade_route_count"],
                },
                "management": {
                    "did_sell": item["did_sell"],
                    "rally": {
                        "active": rally["active"],
                        "persistent": rally["persistent"],
                        "vigilant": rally["vigilant"],
                        "order_count": rally["order_count"],
                        "plan_id": (
                            self._mac(
                                "rally", "plan", item["ref"],
                                rally["order_count"],
                                rally["orders_digest"],
                                int(rally["persistent"]),
                                int(rally["vigilant"]),
                            )
                            if rally["active"] else None
                        ),
                    },
                    "governor": {
                        "enabled": item["governor_enabled"],
                    },
                    "options": {
                        "allow_disband": item["allow_disband"],
                        "new_citizens": item["new_citizens"],
                        "conflict": item["options_conflict"],
                    },
                },
            })
            city_citizen_items.extend({
                "city_id": city_id,
                "kind": "tile",
                "tile_id": self._tile_id(value["native_tile"]),
                "worked": value["worked"],
                "free_worked": value["free_worked"],
                "can_work": value["can_work"],
                "yields": dict(value["yields"]),
            } for value in citizen_tiles)
            city_citizen_items.extend({
                "city_id": city_id,
                "kind": "specialist",
                "id": self._specialist_id(value["native_id"]),
                "name": value["name"],
                "count": value["count"],
                "counts_toward_population": value[
                    "counts_toward_population"
                ],
                "can_use": value["can_use"],
                "is_default": value["is_default"],
                "yields": dict(value["yields"]),
            } for value in specialists)
            city_worker_task_items.extend({
                "id": self._city_worker_task_id(
                    item["ref"], value["native_tile"],
                ),
                "city_id": city_id,
                "tile_id": self._tile_id(value["native_tile"]),
                "activity": {
                    "id": self._activity_id(
                        value["activity"], value["native_target_extra"],
                    ),
                    "name": value["activity"],
                    "target_extra": ({
                        "id": self._extra_id(
                            value["native_target_extra"],
                        ),
                        "name": value["target_extra_name"],
                    } if value["native_target_extra"] >= 0 else None),
                },
                "priority": value["want"],
            } for value in worker_tasks)
            city_worklist_items.extend({
                "city_id": city_id,
                "position": value["position"],
                "production_id": self._production_id(
                    value["production_kind"], value["production_native_id"],
                ),
                "kind": value["production_kind"],
                "name": value["production_name"],
            } for value in worklist)
            city_build_choice_items.extend({
                **self._public_build_choice(value, city_id),
                "preservable_count": sum(
                    existing["production_kind"] == value["production_kind"]
                    and existing["production_native_id"]
                    == value["production_native_id"]
                    for existing in worklist
                ),
            } for value in build_choices)
            city_improvement_items.extend({
                "city_id": city_id,
                "id": self._production_id("improvement", value["native_id"]),
                "name": value["name"],
                "sellable": value["sellable"],
                "sell_price": value["sell_price"],
            } for value in improvements)
        if citizen_anomalies:
            self.native_anomalies["city_citizen_counts"] = (
                self.native_anomalies.get("city_citizen_counts", 0)
                + len(citizen_anomalies)
            )
        route_summaries = {
            item["unit_ref"]: item for item in parsed.unit_routes
        }
        unit_items = []
        for item in parsed.units:
            public = {
                "id": self._entity_id("unit", item["ref"]),
                "scope": item["scope"],
                "owner_player_id": self._entity_id("player", item["owner_ref"]),
                "type": item["type"],
                "type_id": self._unit_type_id(item["native_type_id"]),
                "tile_id": self._tile_id(item["native_tile"]),
                "x": item["x"], "y": item["y"], "hp": item["hp"],
                "veterancy": {
                    "level": item["veteran"],
                    "name": item["veteran_name"],
                    "levels": item["veteran_levels"],
                    "power_factor_percent": item["veteran_power"],
                    "move_bonus": item["veteran_move_bonus"],
                },
                "type_stats": {
                    "max_hp": item["type_stats"]["max_hp"],
                    "max_fuel": item["type_stats"]["max_fuel"],
                    "move_rate": item["type_stats"]["move_rate"],
                    "attack": item["type_stats"]["attack"],
                    "defense": item["type_stats"]["defense"],
                    "firepower": item["type_stats"]["firepower"],
                    "base_upkeep": dict(
                        item["type_stats"]["base_upkeep"]
                    ),
                },
            }
            if item["scope"] == "own":
                route_summary = route_summaries.get(item["ref"])
                public["home_city_id"] = (
                    self._entity_id("city", item["home_ref"])
                    if item["home_ref"] is not None else None
                )
                public["conversion"] = ({
                    "target_type_id": self._unit_type_id(
                        item["converted_type_native_id"],
                    ),
                    "target_type": item["converted_type"],
                } if item["converted_type_native_id"] != -1 else None)
                public["moves"] = item["moves"]
                public["fuel"] = item["fuel"]
                public["upkeep"] = dict(item["upkeep"])
                public["paradrop"] = {
                    "used_this_turn": item["paradropped"],
                    "range": item["paradrop_range"],
                }
                public["automation"] = {
                    "controller": item["controller"],
                    "has_orders": item["has_orders"],
                }
                destination = next((
                    tile for tile in parsed.tiles
                    if tile["native_index"] == item["orders_destination"]
                ), None) if item["orders_destination"] >= 0 else None
                public_destination = (
                    {
                        "tile_id": self._tile_id(item["orders_destination"]),
                        **({
                            "x": destination["x"],
                            "y": destination["y"],
                        } if destination is not None else {}),
                    }
                    if item["orders_destination"] >= 0 else None
                )
                public["route"] = ({
                    "id": self._mac(
                        "route", "unit-orders", item["ref"],
                        item["orders_digest"],
                    ),
                    "mode": "patrol" if item["orders_repeat"] else "goto",
                    "vigilant": item["orders_vigilant"],
                    "order_count": item["order_count"],
                    "path_available": route_summary["reconstructable"],
                    "path_step_count": route_summary["step_count"],
                    "destination": public_destination,
                } if item["has_orders"] else None)
                decision_tile = item["action_decision_tile"]
                public["action_decision"] = {
                    "want": item["action_decision_want"],
                    "pending": decision_tile >= 0,
                    "tile_id": (
                        self._tile_id(decision_tile)
                        if decision_tile >= 0 else None
                    ),
                }
                public["activity"] = {
                    "id": self._activity_id(
                        item["activity"], item["activity_target"],
                    ),
                    "name": item["activity"],
                    "progress": item["activity_progress"],
                    "target": ({
                        "type": "extra",
                        "id": self._extra_id(item["activity_target"]),
                        "name": item["activity_target_name"],
                    } if item["activity_target"] != -1 else None),
                }
                public["transport"] = {
                    "state": item["transport_state"],
                    "transporter_unit_id": (
                        self._entity_id("unit", item["transporter_ref"])
                        if item["transporter_ref"] is not None else None
                    ),
                    "capacity": item["transport_capacity"],
                    "occupied": (
                        None if item["transport_state"] == "unresolved"
                        else item["occupied"]
                    ),
                }
            unit_items.append(public)
        def live_refs(source: _ParsedObservation) -> dict[str, str]:
            result: dict[str, str] = {}
            if source.player is not None:
                result[source.player["ref"]] = "player"
            result.update(
                (item["other_ref"], "player") for item in source.diplomacy
            )
            result.update((item["ref"], "city") for item in source.city_sites)
            result.update((item["ref"], "unit") for item in source.units)
            return result

        retired = {
            item["ref"]: item["kind"] for item in parsed.tombstones
        }
        if self._snapshots:
            previous = next(reversed(self._snapshots.values()))
            current_refs = live_refs(parsed)
            for ref, kind in live_refs(previous.parsed).items():
                if ref not in current_refs:
                    retired.setdefault(ref, kind)
        tombstone_items = [{
            "id": self._entity_id(kind, ref),
            "type": kind,
        } for ref, kind in sorted(retired.items())]
        chat_items = [{
            "sequence": item["sequence"],
            "turn": item["turn"],
            "phase": item["phase"],
            "sender": {
                "kind": item["sender"],
                "name": item["sender_name"],
                "self": item["self"],
            },
            "channel": item["channel"],
            "event": item["event"],
            "message": item["message"],
            "truncated": item["truncated"],
        } for item in sorted(parsed.chats, key=lambda value: value["sequence"])]

        actor_bindings: dict[str, _ActorBinding] = {}
        if parsed.player is not None and player_id is not None:
            actor_bindings[player_id] = _ActorBinding(
                kind="player", native_ref=parsed.player["ref"],
            )
        for native_city, public_city in zip(parsed.cities, city_items):
            actor_bindings[public_city["id"]] = _ActorBinding(
                kind="city", native_ref=native_city["ref"],
            )
        for native_unit, public_unit in zip(parsed.units, unit_items):
            if native_unit["scope"] == "own":
                actor_bindings[public_unit["id"]] = _ActorBinding(
                    kind="unit", native_ref=native_unit["ref"],
                )

        action_decision_bindings: dict[str, _ActionDecisionBinding] = {}
        for native_unit, public_unit in zip(parsed.units, unit_items):
            if (
                native_unit["scope"] != "own"
                or native_unit["action_decision_tile"] < 0
            ):
                continue
            target_id = self._tile_id(
                native_unit["action_decision_tile"],
            )
            binding = _ActionDecisionBinding(
                actor_id=public_unit["id"],
                native_target_tile=native_unit["action_decision_tile"],
            )
            if (
                target_id in action_decision_bindings
                and action_decision_bindings[target_id] != binding
            ):
                _fail()
            action_decision_bindings[target_id] = binding

        tile_bindings: dict[str, int] = {}
        for tile in parsed.tiles:
            if tile["known"] not in {1, 2}:
                continue
            public_tile = self._tile_id(tile["native_index"])
            existing_tile = tile_bindings.get(public_tile)
            if (
                existing_tile is not None
                and existing_tile != tile["native_index"]
            ):
                _fail()
            tile_bindings[public_tile] = tile["native_index"]
        for item in (*parsed.cities, *parsed.units, *parsed.city_sites):
            native_tile = item["native_tile"]
            tile_bindings.setdefault(self._tile_id(native_tile), native_tile)

        tile_by_native = {
            item["native_index"]: item for item in parsed.tiles
        }
        own_by_ref = {
            item["ref"]: item for item in parsed.units if item["scope"] == "own"
        }
        descriptors: list[dict[str, Any]] = []
        bindings: dict[str, _ActionBinding] = {}
        for action in parsed.actions:
            if not action["slot"].startswith("a"):
                _fail()
            rule = _ACTION_RULES[action["native_rule"]]
            action_id = self._mac(
                "action", "action", native_revision, action["slot"],
            )
            actor = own_by_ref.get(action["actor_ref"])
            target = tile_by_native.get(action["native_target_tile"])
            target_tech = research_by_native.get(
                action["native_target_tech"],
            )
            vote = vote_by_native.get(action["native_vote_no"])
            vote_choices = (
                tuple(
                    choice for choice in ("yes", "no", "abstain")
                    if choice != vote["current_vote"]
                ) if rule.operation == "cast_vote" and vote is not None
                else ()
            )
            if rule.operation in {"cast_vote", "cancel_vote"}:
                public_actor = (
                    {"type": "player", "id": player_id}
                    if player_id is not None else None
                )
                public_target = (
                    {"type": "vote", "vote_id": vote["vote_id"]}
                    if vote is not None else None
                )
            elif rule.operation == "propose_server_setting":
                public_actor = (
                    {"type": "player", "id": player_id}
                    if player_id is not None else None
                )
                public_target = self._server_setting_target(
                    action, native_revision,
                )
            elif rule.operation == "surrender":
                public_actor = (
                    {"type": "player", "id": player_id}
                    if player_id is not None else None
                )
                public_target = public_actor
            elif rule.operation == "send_chat":
                public_actor = (
                    {"type": "player", "id": player_id}
                    if player_id is not None else None
                )
                public_target = {
                    "type": "chat_channel",
                    "channels": list(_CHAT_SEND_CHANNELS),
                    "recipients_from": "chat_recipients",
                }
            elif rule.operation in {"set_target", "set_goal"}:
                public_actor = None
                public_target = {
                    "type": "technology",
                    "id": target_tech["id"],
                    "name": target_tech["name"],
                    "state": target_tech["state"],
                } if target_tech is not None else None
            elif rule.operation == "set_rates":
                public_actor = (
                    {"type": "player", "id": player_id}
                    if player_id is not None else None
                )
                public_target = None
            else:
                public_actor = ({
                    "type": "unit",
                    "id": self._entity_id("unit", actor["ref"]),
                } if actor is not None else None)
                public_target = ({
                    "type": "tile",
                    "id": self._tile_id(target["native_index"]),
                    "x": target["x"],
                    "y": target["y"],
                } if target is not None else None)
            subject = {
                "actor": public_actor,
                "target": public_target,
                "operation": rule.operation,
                "variant": rule.variant,
                "consuming": rule.consuming,
                "legality": action["legality"],
                "probability": self._public_probability(action),
            }
            descriptor = {
                "action_id": action_id,
                "kind": rule.public_kind,
                "label": (
                    "Cast vote" if rule.operation == "cast_vote"
                    else "Cancel own vote"
                    if rule.operation == "cancel_vote"
                    else f"Propose changing {action['target_name']}"
                    if rule.operation == "propose_server_setting"
                    else "Surrender"
                    if rule.operation == "surrender"
                    else self._action_label(
                        rule, actor,
                        target_tech
                        if rule.operation in {"set_target", "set_goal"}
                        else target,
                    )
                ),
                "subject": subject,
                "arguments_schema": self._arguments_schema(
                    rule, action["max_rate"],
                    vote_id=vote["vote_id"] if vote is not None else None,
                    vote_choices=vote_choices,
                    server_setting=(
                        action
                        if rule.operation == "propose_server_setting"
                        else None
                    ),
                ),
                "state_revision": state_revision,
            }
            try:
                descriptor = validate_legal_action_descriptor(descriptor)
            except FullControlSchemaError:
                _fail()
            descriptors.append(descriptor)
            bindings[action_id] = _ActionBinding(
                slot=action["slot"],
                native_revision=native_revision,
                argument_contract=rule.args,
                public_kind=rule.public_kind,
                operation=rule.operation,
                turn=meta["turn"],
                phase=meta["phase"],
                max_rate=action["max_rate"],
                actor_ref=action["actor_ref"],
                vote_id=vote["vote_id"] if vote is not None else None,
                vote_choices=vote_choices,
                server_setting_type=action["server_setting_type"],
                server_setting_min=action["server_setting_min"],
                server_setting_max=action["server_setting_max"],
                server_setting_current=action["server_setting_current"],
                server_setting_value=action["server_setting_value"],
                server_setting_name=(
                    action["target_name"]
                    if rule.operation == "propose_server_setting" else ""
                ),
            )

        legal_counts = dict(sorted(Counter(
            item["kind"] for item in descriptors
        ).items()))
        overview = {
            "client_state": meta["state"],
            "turn": meta["turn"],
            "phase": meta["phase"],
            "phase_mode": meta["phase_mode"],
            "phase_count": meta["phase_count"],
            "active_phase": meta["active_phase"],
            "phase_ready": meta["phase_ready"],
            "map": {
                "width": meta["map_width"],
                "height": meta["map_height"],
                "topology": meta["topology"],
                "wrap_x": meta["wrap_x"],
                "wrap_y": meta["wrap_y"],
            },
            "player": public_player,
            "research": public_research,
            "score": self._own_score(parsed, public_spaceship),
            "counts": {
                "research": len(research_items),
                "governments": len(government_items),
                "multipliers": len(multiplier_items),
                "spaceship": 1 if public_spaceship is not None else 0,
                "votes": len(vote_items),
                "diplomacy": len(diplomacy_items),
                "diplomacy_clauses": sum(
                    item["clause_count"] for item in parsed.diplomacy
                ),
                "known_tiles": meta["known_tile_count"],
                "infrastructure": len(infrastructure_items),
                "cities": len(city_items),
                "city_detail": len(city_detail_items),
                "city_citizens": sum(
                    item["citizen_tile_count"]
                    + item["specialist_type_count"]
                    for item in parsed.cities
                ),
                "city_worker_tasks": len(city_worker_task_items),
                "city_build_choices": sum(
                    item["build_choice_count"] for item in parsed.cities
                ),
                "city_worklist": sum(
                    item["worklist_length"] for item in parsed.cities
                ),
                "city_improvements": sum(
                    item["improvement_count"] for item in parsed.cities
                ),
                "city_trade_routes": sum(
                    item["trade_route_count"] for item in parsed.cities
                ),
                "city_governor": sum(
                    int(item["governor_enabled"]) for item in parsed.cities
                ),
                "city_sites": len(city_site_items),
                "units": len(unit_items),
                "tombstones": len(tombstone_items),
                "chat": len(chat_items),
                "legal_actions": len(descriptors),
            },
            "legal_action_counts": legal_counts,
        }
        sections: dict[str, list[dict[str, Any]]] = {
            "overview": [overview],
            "research": research_items,
            "governments": government_items,
            "multipliers": multiplier_items,
            "spaceship": (
                [public_spaceship] if public_spaceship is not None else []
            ),
            "votes": vote_items,
            "diplomacy": diplomacy_items,
            "diplomacy_clauses": diplomacy_clause_items,
            "known_tiles": tile_items,
            "tile_window": tile_items,
            "infrastructure": infrastructure_items,
            "cities": city_items,
            "city_detail": city_detail_items,
            "city_citizens": city_citizen_items,
            "city_worker_tasks": city_worker_task_items,
            "city_build_choices": city_build_choice_items,
            "city_worklist": city_worklist_items,
            "city_improvements": city_improvement_items,
            "city_trade_routes": [],
            "city_governor": [],
            "city_sites": city_site_items,
            "units": unit_items,
            "tombstones": tombstone_items,
            "chat": chat_items,
            "chat_recipients": [],
        }
        size_payload = {
            "state_revision": state_revision,
            "sections": sections,
            "legal_actions": descriptors,
        }
        canonical_bytes = len(json.dumps(
            size_payload,
            ensure_ascii=False,
            allow_nan=False,
            sort_keys=True,
            separators=(",", ":"),
        ).encode("utf-8"))
        if canonical_bytes > MAX_PROJECTED_BYTES:
            _fail()
        return _ProjectedSnapshot(
            native_revision=native_revision,
            row_digest=row_digest,
            state_revision=_freeze(state_revision),
            sections=_freeze({
                key: tuple(values) for key, values in sections.items()
            }),
            legal_actions=tuple(_freeze(item) for item in descriptors),
            action_bindings=MappingProxyType(dict(bindings)),
            actor_bindings=MappingProxyType(actor_bindings),
            relation_bindings=MappingProxyType(relation_bindings),
            tile_bindings=MappingProxyType(tile_bindings),
            action_decision_bindings=MappingProxyType(
                action_decision_bindings,
            ),
            parsed=parsed,
            canonical_bytes=canonical_bytes,
        )

    @staticmethod
    def _own_score(
        parsed: _ParsedObservation,
        public_spaceship: Mapping[str, Any] | None,
    ) -> dict[str, Any]:
        """Project what this seat can prove about its own civilization score.

        The eval optimizes the final civilization score, and Freeciv computes
        it from citizens, known techs, great wonders, an arrived spaceship,
        units built and killed, and culture.  Only the seat's own rows feed
        this, so nothing here is visible to or about another player; the terms
        the private observation does not carry are named rather than guessed,
        and every one of them is non-negative, which makes the total a true
        lower bound instead of an estimate that can read high.
        """
        citizens = sum(city["size"] for city in parsed.cities)
        techs = 0
        if parsed.research is not None:
            # `techs` counts the always-known root advance, which scores zero.
            techs = max(0, parsed.research["techs"] - 1)
            techs += parsed.research["future"] * 5 // 2
        spaceship = 0
        if (
            public_spaceship is not None
            and public_spaceship["state"] == "arrived"
        ):
            spaceship = int(
                public_spaceship["population"]
                * public_spaceship["success_rate"] / 100
            )
        return {
            "exact": None,
            "lower_bound": citizens + 2 * techs + spaceship,
            "components": {
                "citizens": citizens,
                "techs": techs,
                "spaceship": spaceship,
            },
            "unobserved": [
                "wonders", "units_built", "units_killed", "culture",
            ],
        }

    def _project_pregame(
        self,
        native_revision: int,
        row_digest: str,
        parsed: _ParsedObservation,
    ) -> _ProjectedSnapshot:
        pregame = parsed.pregame
        if pregame is None:
            _fail()
        state_revision = {
            "turn": 0,
            "revision": native_revision,
            "state_token": self._mac(
                "state", "state", self.generation, native_revision,
                row_digest, int(self._pregame_ready_allowed),
            ),
        }
        player_id = self._entity_id("player", pregame["ref"])
        actor_bindings = {
            player_id: _ActorBinding("player", pregame["ref"]),
        }
        descriptors: list[dict[str, Any]] = []
        bindings: dict[str, _ActionBinding] = {}
        vote_items = [{
            "vote_ref": self._mac(
                "vote_ref", "vote", self.generation,
                item["native_vote_no"],
            ),
            "vote_id": self._mac(
                "vote", "vote", native_revision, item["native_vote_no"],
            ),
            "caller": item["caller"],
            "description": item["description"],
            "yes": item["yes"],
            "no": item["no"],
            "abstain": item["abstain"],
            "num_voters": item["num_voters"],
            "percent_required": item["percent_required"],
            "team_only": item["team_only"],
            "current_vote": item["current_vote"],
            "can_vote": item["can_vote"],
            "status": item["status"],
            "outcome_turn": item["outcome_turn"],
            "outcome_phase": item["outcome_phase"],
        } for item in sorted(
            parsed.votes, key=lambda value: value["native_vote_no"],
        )]
        vote_by_native = {
            native["native_vote_no"]: public
            for native, public in zip(
                sorted(parsed.votes, key=lambda value: value["native_vote_no"]),
                vote_items,
                strict=True,
            )
        }
        chat_items = [{
            "sequence": item["sequence"],
            "turn": item["turn"],
            "phase": item["phase"],
            "sender": {
                "kind": item["sender"],
                "name": item["sender_name"],
                "self": item["self"],
            },
            "channel": item["channel"],
            "event": item["event"],
            "message": item["message"],
            "truncated": item["truncated"],
        } for item in sorted(
            parsed.chats, key=lambda value: value["sequence"],
        )]
        for action in parsed.actions:
            rule = _ACTION_RULES[action["native_rule"]]
            if (
                rule.public_kind == "pregame.set_ready"
                and action["desired_acceptance"] == 1
                and not self._pregame_ready_allowed
            ):
                continue
            action_id = self._mac(
                "action", "action", native_revision, action["slot"],
            )
            desired = action["desired_acceptance"]
            vote = vote_by_native.get(action["native_vote_no"])
            vote_choices = (
                tuple(
                    choice for choice in ("yes", "no", "abstain")
                    if choice != vote["current_vote"]
                ) if rule.operation == "cast_vote" and vote is not None
                else ()
            )
            if rule.operation == "propose_server_setting":
                public_target = self._server_setting_target(
                    action, native_revision,
                )
            elif rule.operation in {"cast_vote", "cancel_vote"}:
                public_target = (
                    {"type": "vote", "vote_id": vote["vote_id"]}
                    if vote is not None else None
                )
            elif rule.operation == "send_chat":
                public_target = {
                    "type": "chat_channel",
                    "channels": list(_CHAT_SEND_CHANNELS),
                    "recipients_from": "chat_recipients",
                }
            else:
                public_target = {
                    "type": {
                        "configure": "pregame_configuration",
                        "set_team": "pregame_team",
                        "set_ready": "pregame_readiness",
                    }[rule.operation],
                    "desired_ready": desired == 1
                    if rule.operation == "set_ready" else None,
                    "choices_from": "pregame_teams"
                    if rule.operation == "set_team" else None,
                }
            descriptor = {
                "action_id": action_id,
                "kind": rule.public_kind,
                "label": (
                    "Cast vote" if rule.operation == "cast_vote"
                    else "Send chat message"
                    if rule.operation == "send_chat" else {
                        "cancel_vote": "Cancel own vote",
                        "propose_server_setting": (
                            f"Propose changing {action['target_name']}"
                        ),
                        "configure": "Choose nation, leader, sex, and style",
                        "set_team": "Choose team",
                        "set_ready": (
                            "Mark ready" if desired == 1
                            else "Withdraw readiness"
                        ),
                    }[rule.operation]
                ),
                "subject": {
                    "actor": {"type": "player", "id": player_id},
                    "target": public_target,
                    "operation": rule.operation,
                    "variant": rule.variant,
                    "consuming": False,
                    "legality": action["legality"],
                    "probability": self._public_probability(action),
                },
                "arguments_schema": self._arguments_schema(
                    rule, desired if rule.operation == "set_ready" else 0,
                    vote_id=vote["vote_id"] if vote is not None else None,
                    vote_choices=vote_choices,
                    server_setting=(
                        action
                        if rule.operation == "propose_server_setting"
                        else None
                    ),
                ),
                "state_revision": state_revision,
            }
            try:
                descriptor = validate_legal_action_descriptor(descriptor)
            except FullControlSchemaError:
                _fail()
            descriptors.append(descriptor)
            bindings[action_id] = _ActionBinding(
                slot=action["slot"],
                native_revision=native_revision,
                argument_contract=rule.args,
                public_kind=rule.public_kind,
                operation=rule.operation,
                turn=0,
                phase=0,
                max_rate=desired if rule.operation == "set_ready" else 0,
                actor_ref=pregame["ref"],
                vote_id=vote["vote_id"] if vote is not None else None,
                vote_choices=vote_choices,
                server_setting_type=action["server_setting_type"],
                server_setting_min=action["server_setting_min"],
                server_setting_max=action["server_setting_max"],
                server_setting_current=action["server_setting_current"],
                server_setting_value=action["server_setting_value"],
                server_setting_name=(
                    action["target_name"]
                    if rule.operation == "propose_server_setting" else ""
                ),
            )
        sections = {
            "overview": [{
                "client_state": "preparing",
                "turn": 0,
                "phase": None,
                "player": {
                    "id": player_id,
                    "leader_name": pregame["leader"],
                    "nation": None if pregame["nation"] == "none"
                              else pregame["nation"],
                    "sex": pregame["sex"],
                    "style": None if pregame["style"] == "none"
                             else pregame["style"],
                    "ready": pregame["ready"],
                },
                "counts": {
                    "pregame_nations": pregame["nation_choices"],
                    "pregame_styles": pregame["style_choices"],
                    "pregame_teams": pregame["team_choices"],
                    "votes": len(vote_items),
                    "chat": len(chat_items),
                    "legal_actions": len(descriptors),
                },
                "legal_action_counts": dict(sorted(Counter(
                    item["kind"] for item in descriptors
                ).items())),
            }],
            "pregame_nations": [],
            "pregame_styles": [],
            "pregame_teams": [],
            "votes": vote_items,
            "chat": chat_items,
            "chat_recipients": [],
        }
        size_payload = {
            "state_revision": state_revision,
            "sections": sections,
            "legal_actions": descriptors,
        }
        canonical_bytes = len(json.dumps(
            size_payload, ensure_ascii=False, allow_nan=False,
            sort_keys=True, separators=(",", ":"),
        ).encode("utf-8"))
        if canonical_bytes > MAX_PROJECTED_BYTES:
            _fail()
        return _ProjectedSnapshot(
            native_revision=native_revision,
            row_digest=row_digest,
            state_revision=_freeze(state_revision),
            sections=_freeze({
                key: tuple(values) for key, values in sections.items()
            }),
            legal_actions=tuple(_freeze(item) for item in descriptors),
            action_bindings=MappingProxyType(bindings),
            actor_bindings=MappingProxyType(actor_bindings),
            relation_bindings=MappingProxyType({}),
            tile_bindings=MappingProxyType({}),
            action_decision_bindings=MappingProxyType({}),
            parsed=parsed,
            canonical_bytes=canonical_bytes,
        )

    def _validate_target_action_result(
        self,
        snapshot: _ProjectedSnapshot,
        request: V2TargetActionRequest,
        native_result: Mapping[str, Any],
    ) -> tuple[
        tuple[dict[str, Any], ...],
        tuple[tuple[str, _ActionBinding], ...],
    ]:
        if not isinstance(native_result, Mapping) or set(native_result) != {
            "generation", "native_revision", "actor_ref", "native_tile",
            "count", "rows",
        }:
            _fail()
        count = native_result["count"]
        rows = native_result["rows"]
        if (
            native_result["generation"] != self.generation
            or native_result["native_revision"] != request.native_revision
            or native_result["actor_ref"] != request.native_actor_ref
            or native_result["native_tile"] != request.native_target_tile
            or isinstance(count, bool) or not isinstance(count, int)
            or not 0 <= count <= 256
            or not isinstance(rows, tuple) or len(rows) != count
        ):
            _fail()
        projected: list[dict[str, Any]] = []
        pending: list[tuple[str, _ActionBinding]] = []
        seen_slots: set[str] = set()
        seen_action_ids: set[str] = set()
        for row in rows:
            if not isinstance(row, str):
                _fail()
            try:
                encoded = row.encode("ascii", "strict")
            except UnicodeEncodeError:
                _fail()
            if not 1 <= len(encoded) <= MAX_NATIVE_ROW_BYTES:
                _fail()
            parts = row.split(" ")
            if (
                parts[0] != "action"
                or len(parts) != len(_ROW_FIELDS["action"]) + 1
            ):
                _fail()
            pairs: list[tuple[str, str]] = []
            for token in parts[1:]:
                if token.count("=") != 1:
                    _fail()
                key, value = token.split("=", 1)
                if not key or not value:
                    _fail()
                pairs.append((key, value))
            if tuple(key for key, _ in pairs) != _ROW_FIELDS["action"]:
                _fail()
            action = self._parse_row("action", dict(pairs))
            slot = action["slot"]
            if (
                not slot.startswith("t")
                or int(slot[1:9], 16) != request.native_target_tile
                or slot in seen_slots
                or action["actor_ref"] != request.native_actor_ref
                or action["native_target_tile"]
                   != request.native_target_tile
            ):
                _fail()
            seen_slots.add(slot)
            action_id = self._mac(
                "action", "target", request.native_revision,
                request.native_actor_ref, slot,
            )
            if action_id in seen_action_ids:
                _fail()
            seen_action_ids.add(action_id)
            if action["native_kind"] == "unit.special":
                if request.actor_kind != "unit":
                    _fail()
                descriptor, binding = self._project_special_target_action(
                    snapshot, request, action, action_id,
                )
            else:
                expected_rules = (
                    frozenset({
                        "unit.goto", "unit.goto_and_perform",
                        "unit.connect_route", "Paradrop Unit",
                        "Paradrop Unit Frighten", "Paradrop Unit Enter",
                        "Teleport", "Teleport2", "Teleport3",
                        "Teleport Frighten", "Teleport Enter",
                    })
                    if request.actor_kind == "unit"
                    else frozenset({"city.set_rally"})
                    if request.actor_kind == "city"
                    else frozenset({"player.place_infrastructure"})
                )
                if action["native_rule"] not in expected_rules:
                    _fail()
                descriptor, binding = self._project_scoped_action(
                    snapshot, request, action, action_id,
                )
                if (
                    request.actor_kind == "unit" and (
                        binding.operation in {
                            "goto", "goto_and_perform", "connect_route",
                        }
                        and binding.public_kind != "unit.order"
                        or binding.operation in {"paradrop", "teleport"}
                        and binding.public_kind != "unit.perform_action"
                        or binding.operation not in {
                            "goto", "goto_and_perform", "connect_route",
                            "paradrop", "teleport",
                        }
                    )
                    or request.actor_kind == "city" and (
                        binding.operation != "set_rally"
                        or binding.public_kind != "city.set_rally"
                    )
                    or request.actor_kind == "player" and (
                        binding.operation != "place_infrastructure"
                        or binding.public_kind != "player.set_infrastructure"
                    )
                ):
                    _fail()
            projected.append(descriptor)
            pending.append((action_id, binding))
        return tuple(projected), tuple(pending)

    def _project_special_target_action(
        self,
        snapshot: _ProjectedSnapshot,
        request: V2TargetActionRequest,
        action: Mapping[str, Any],
        action_id: str,
    ) -> tuple[dict[str, Any], _ActionBinding]:
        """Translate one safe server-discovered action without enum leakage."""
        rule = _special_action_rule(action)
        parsed = snapshot.parsed
        ruleset_custom = (
            rule is not None and rule.operation == "ruleset_action"
        )
        targeted_technology = (
            rule is not None and rule.native_subtarget == "technology"
        )
        targeted_building = (
            rule is not None and rule.native_subtarget == "building"
        )
        targeted_extra = (
            rule is not None
            and rule.native_subtarget in {"extra", "extra_not_there"}
        )
        targeted_specialist = (
            rule is not None and rule.native_subtarget == "specialist"
        )
        paid = rule is not None and rule.gold_cost_policy == "quoted_maximum"
        target_technology = next((
            tech for tech in parsed.research_techs
            if tech["native_id"] == action["native_target_tech"]
        ), None)
        if (
            targeted_technology and target_technology is None
            and action["target_name"] == "Future Tech"
            and action["native_target_tech"] >= 0
        ):
            # The victim may own future levels before this seat is itself
            # eligible to research Future Tech, in which case the ordinary
            # self-research catalog has no row for it.  The request-bound
            # native action row is still the authoritative selectable choice.
            target_technology = {
                "native_id": action["native_target_tech"],
                "name": "Future Tech",
                "state": "future",
            }
        actor = next((
            unit for unit in parsed.units
            if unit["ref"] == request.native_actor_ref
            and unit["scope"] == "own"
        ), None)
        tile = self._known_tile_for_native(
            snapshot, request.native_target_tile,
        )
        decision_binding = snapshot.action_decision_bindings.get(
            request.target_id,
        )
        exact_action_decision = (
            request.action_decision
            and decision_binding is not None
            and decision_binding.actor_id == request.actor_id
            and decision_binding.native_target_tile
               == request.native_target_tile
        )
        if (
            rule is None
            or actor is None
            or tile is None
            or (tile["known"] == 0 and not exact_action_decision)
            or (
                tile["known"] == 1
                and rule.operation != "paradrop_conquer"
            )
            or action["target_kind"] != rule.target_kind
            or action["args"] != "none"
            or action["source_city_ref"] is not None
            or action["transport_context_ref"] is not None
            or (targeted_technology and (
                target_technology is None
                or target_technology["state"] not in {
                    "available", "reachable", "future",
                }
                or action["target_name"] != target_technology["name"]
            ))
            or (not targeted_technology
                and action["native_target_tech"] != -1)
            or (targeted_building and (
                action["target_build_kind"] != "improvement"
                or action["native_target_build"] < 0
            ))
            or (not targeted_building and (
                action["target_build_kind"] != "none"
                or action["native_target_build"] != -1
            ))
            or (targeted_extra
                and action["native_target_extra"] < 0)
            or (not targeted_extra
                and action["native_target_extra"] != -1)
            or (targeted_specialist
                and action["native_target_specialist"] < 0)
            or (not targeted_specialist
                and action["native_target_specialist"] != -1)
            or action["subtarget_kind"] != rule.native_subtarget
            or action["subresults"] not in rule.allowed_subresult_sets
            or (not targeted_technology and not targeted_building
                and not targeted_extra and not targeted_specialist
                and not ruleset_custom
                and action["target_name"] != "target")
            or action["native_target_government"] != -1
            or action["max_rate"] != 0
            or action["route_waypoint_limit"] != 0
            or action["infrastructure_cost"] != 0
            or action["infrastructure_turns"] != 0
            or action["infrastructure_choices"]
            or action["spaceship_part"] != "none"
            or action["spaceship_value"] != -1
            or action["native_target_multiplier"] != -1
            or action["multiplier_value"] != -1
            or action["native_source_specialist"] != -1
            or action["activity"] != "none"
            or (rule.probability_policy == "unresolved"
                and action["probability_kind"] != "not_implemented")
            or (rule.probability_policy == "resolved"
                and action["probability_kind"] == "not_implemented")
            or rule.probability_policy not in {
                "resolved", "unresolved", "native",
            }
            or rule.gold_cost_policy not in {"none", "quoted_maximum"}
            or (rule.native_rules
                and action["native_rule"] not in rule.native_rules)
            or (paid and (
                parsed.player is None
                or action["gold_cost"] < 0
                or action["gold_cost"] > parsed.player["gold"]
                or (rule.operation == "incite_city"
                    and action["gold_cost"] >= 1_000_000_000)
            ))
            or (not paid and action["gold_cost"] != -1)
        ):
            _fail()
        if rule.operation == "paradrop_conquer" and (
            tile["known"] != 2
            or actor["native_tile"] == request.native_target_tile
            or actor["moves"] <= 0
            or actor["paradropped"]
        ):
            _fail()
        self._validate_probability(action)
        public_actor = {
            "type": "unit",
            "id": self._entity_id("unit", request.native_actor_ref),
        }
        public_target: dict[str, Any]
        if rule.target_kind == "City":
            city = next((
                item for item in (*parsed.cities, *parsed.city_sites)
                if item["ref"] == action["destination_city_ref"]
                and item["native_tile"] == request.native_target_tile
            ), None)
            if city is None or action["target_unit_ref"] is not None:
                _fail()
            public_target = {
                "type": "city",
                "id": self._entity_id("city", city["ref"]),
                "name": city["name"],
                "tile_id": request.target_id,
            }
        elif rule.target_kind == "Unit":
            unit = next((
                item for item in parsed.units
                if item["ref"] == action["target_unit_ref"]
                and item["native_tile"] == request.native_target_tile
            ), None)
            if unit is None or action["destination_city_ref"] is not None:
                _fail()
            public_target = {
                "type": "unit",
                "id": self._entity_id("unit", unit["ref"]),
                "tile_id": request.target_id,
            }
        elif rule.target_kind in {"Stack", "Tile", "Extras"}:
            if (
                action["destination_city_ref"] is not None
                or action["target_unit_ref"] is not None
            ):
                _fail()
            public_target = {
                "type": {
                    "Stack": "unit_stack",
                    "Tile": "tile",
                    "Extras": "tile",
                }[rule.target_kind],
                "id": request.target_id,
            }
            if tile["known"] != 0:
                public_target["x"] = tile["x"]
                public_target["y"] = tile["y"]
        elif rule.target_kind == "Self":
            if (
                action["destination_city_ref"] is not None
                or action["target_unit_ref"] is not None
                or request.native_target_tile != actor["native_tile"]
            ):
                _fail()
            public_target = dict(public_actor)
        else:
            _fail()
        subject: dict[str, Any] = {
            "actor": public_actor,
            "target": public_target,
            "operation": rule.operation,
            "variant": self._action_variant_id(
                action["native_rule"], rule.operation,
                (action["native_target_tech"] if targeted_technology
                 else action["native_target_build"] if targeted_building
                 else action["native_target_extra"]),
            ),
            "consuming": action["consuming"],
            "legality": action["legality"],
            "probability": self._public_probability(action),
        }
        if targeted_technology:
            subject["technology_choice"] = {
                "id": self._mac(
                    "technology_choice", "target_action", action_id,
                    action["native_target_tech"],
                ),
                "name": target_technology["name"],
            }
        if targeted_building:
            subject["building_choice"] = {
                "id": self._mac(
                    "building_choice", "target_action",
                    snapshot.native_revision, request.native_actor_ref,
                    action["destination_city_ref"], action["native_rule"],
                    action_id, action["native_target_build"],
                ),
                "name": action["target_name"],
            }
        if targeted_extra:
            subject["extra_choice"] = {
                "id": self._mac(
                    "extra_choice", "target_action", action_id,
                    action["subtarget_kind"],
                    action["native_target_extra"],
                ),
                "name": action["target_name"],
                "presence": (
                    "present" if action["subtarget_kind"] == "extra"
                    else "absent"
                ),
            }
        if targeted_specialist:
            subject["specialist_choice"] = {
                "id": self._mac(
                    "specialist_choice", "target_action", action_id,
                    action["native_target_specialist"],
                ),
                "name": action["target_name"],
            }
        if action["subresults"]:
            subject["effects"] = [
                _ACTION_SUBRESULT_EFFECTS[item]
                for item in action["subresults"]
            ]
        if ruleset_custom:
            subject["ruleset_action"] = {
                "name": action["target_name"],
            }
        if paid:
            subject["gold_cost"] = action["gold_cost"]
        descriptor = {
            "action_id": action_id,
            "kind": "unit.perform_action",
            "label": rule.label,
            "subject": subject,
            "arguments_schema": {
                "type": "object",
                "properties": {},
                "required": [],
                "additionalProperties": False,
            },
            "state_revision": _thaw(snapshot.state_revision),
        }
        try:
            descriptor = validate_legal_action_descriptor(descriptor)
        except FullControlSchemaError:
            _fail()
        binding = _ActionBinding(
            slot=action["slot"],
            native_revision=snapshot.native_revision,
            argument_contract="none",
            public_kind="unit.perform_action",
            operation=rule.operation,
            turn=parsed.meta["turn"],
            phase=parsed.meta["phase"],
            max_rate=0,
            actor_ref=request.native_actor_ref,
            scoped=True,
            target_scoped=True,
        )
        return descriptor, binding

    def _city_scope_rows(
        self,
        snapshot: _ProjectedSnapshot,
        city_ref: str,
        section: str,
        *,
        kind: str | None = None,
    ) -> tuple[Mapping[str, Any], ...]:
        source = {
            "city_citizens": (
                tuple(snapshot.parsed.city_tiles)
                + tuple(snapshot.parsed.city_specialists)
            ),
            "city_build_choices": tuple(snapshot.parsed.city_build_choices),
            "city_worklist": tuple(snapshot.parsed.city_worklists),
            "city_improvements": tuple(snapshot.parsed.city_improvements),
            "city_trade_routes": (),
            "city_governor": tuple(snapshot.parsed.city_governors),
        }.get(section)
        if source is None:
            _fail()
        selected = tuple(
            item for item in source if item["city_ref"] == city_ref
        )
        if not selected:
            selected = self._city_state_overlays.get(
                (snapshot.native_revision, city_ref, section),
                (),
            )
        if kind == "city_tile":
            selected = tuple(item for item in selected if "native_tile" in item)
        elif kind == "city_specialist":
            selected = tuple(item for item in selected if "native_id" in item)
        return selected

    def _relation_clause_rows(
        self, snapshot: _ProjectedSnapshot, counterpart_ref: str,
    ) -> tuple[Mapping[str, Any], ...]:
        selected = tuple(
            item for item in snapshot.parsed.diplomacy_clauses
            if item["other_ref"] == counterpart_ref
        )
        if selected:
            return selected
        return self._relation_clause_overlays.get(
            (snapshot.native_revision, counterpart_ref), (),
        )

    def _known_tile_for_native(
        self, snapshot: _ProjectedSnapshot, native_tile: int,
        actor_ref: str | None = None,
    ) -> Mapping[str, Any] | None:
        if actor_ref is not None:
            key = (snapshot.native_revision, actor_ref)
            if key in self._actor_tile_overlays:
                return next((
                    item for item in self._actor_tile_overlays[key]
                    if item["native_index"] == native_tile
                ), None)
        direct = next((
            item for item in snapshot.parsed.tiles
            if item["native_index"] == native_tile
        ), None)
        if direct is not None:
            return direct
        return self._scoped_tile_metadata.get(
            (snapshot.native_revision, native_tile),
        )

    def _validate_actor_scope_page(
        self,
        snapshot: _ProjectedSnapshot,
        request: V2ActorScopeRequest,
        native_page: Mapping[str, Any],
        seen_slots: tuple[str, ...],
        *,
        materialize: bool = False,
    ) -> dict[str, Any] | tuple[
        dict[str, Any], tuple[tuple[str, _ActionBinding], ...]
    ]:
        if not isinstance(native_page, Mapping) or set(native_page) != {
            "generation", "native_revision", "actor_ref", "view_id",
            "offset", "count", "total_count", "next_offset", "complete",
            "overflow", "rows",
        }:
            _fail()
        revision = native_page["native_revision"]
        view_id = native_page["view_id"]
        total = native_page["total_count"]
        offset = native_page["offset"]
        count = native_page["count"]
        native_next_offset = native_page["next_offset"]
        rows = native_page["rows"]
        if (
            native_page["generation"] != self.generation
            or revision != request.native_revision
            or native_page["actor_ref"] != request.native_actor_ref
            or not isinstance(view_id, str)
            or _SCOPE_VIEW.fullmatch(view_id) is None
            or int(view_id[1:].split("-", 1)[0]) != revision
            or isinstance(total, bool) or not isinstance(total, int)
            or not 0 <= total <= MAX_SCOPED_ACTIONS
            or isinstance(offset, bool) or not isinstance(offset, int)
            or not 0 <= offset <= total
            or offset != request.offset
            or isinstance(count, bool) or not isinstance(count, int)
            or count != (
                total - offset if materialize
                else min(request.limit, total - offset)
            )
            or isinstance(native_next_offset, bool)
            or not isinstance(native_next_offset, int)
            or native_next_offset != offset + count
            or native_page["complete"] is not True
            or native_page["overflow"] is not False
            or not isinstance(rows, tuple) or len(rows) != count
            or (request.native_view_id is not None
                and view_id != request.native_view_id)
            or (request.total_count is not None
                and total != request.total_count)
        ):
            _fail()

        parsed = snapshot.parsed
        player = parsed.player
        action_eligible = (
            player is not None
            and player["alive"]
            and parsed.meta["active_phase"]
            and not player["phase_done"]
            and parsed.meta["state"] == "running"
        )
        if total > 0 and not action_eligible:
            _fail()
        player_rules = {
            "phase.end", "research.set_target", "research.set_goal",
            "economy.set_rates",
        }
        player_global_operations = {
            "cast_vote", "cancel_vote", "propose_server_setting", "surrender",
        }
        if request.actor_kind == "player":
            # The native player scope repeats actorless strategic actions and
            # player-bound global governance actions (but not send_chat), then
            # adds scoped-only government, multiplier, and spaceship controls.
            expected = {
                action["slot"]: action for action in parsed.actions
                if (
                    action["actor_ref"] is None
                    and action["native_rule"] in player_rules
                ) or (
                    action["actor_ref"] == request.native_actor_ref
                    and _ACTION_RULES[action["native_rule"]].operation
                        in player_global_operations
                )
            }
            if parsed.governance is None:
                _fail()
            expected_government_capabilities = {
                f"change:{item['native_id']}"
                for item in parsed.governments
                if action_eligible and item["can_change"]
            }
            if action_eligible and parsed.governance["can_revolution"]:
                expected_government_capabilities.add(
                    "revolution:"
                    f"{parsed.governance['during_native_id']}"
                )
            expected_spaceship_capabilities: set[str] = set()
            expected_multiplier_capabilities: set[str] = set()
            if action_eligible:
                for item in parsed.multipliers:
                    if item["can_change"] and item["choice_count"] > 1:
                        expected_multiplier_capabilities.add(
                            f"set_multiplier:{item['native_id']}"
                        )
            ship = parsed.spaceship
            if action_eligible and ship is not None and ship["state"] == "started":
                expected_spaceship_capabilities.update(
                    f"place_component:structural:{item['native_slot']}"
                    for item in parsed.spaceship_structurals
                    if item["can_place"]
                )
                placed_components = ship["fuel"] + ship["propulsion"]
                if placed_components < ship["components"]:
                    if ship["fuel"] < 8:
                        expected_spaceship_capabilities.add(
                            f"place_component:fuel:{ship['fuel'] + 1}"
                        )
                    if ship["propulsion"] < 8:
                        expected_spaceship_capabilities.add(
                            "place_component:propulsion:"
                            f"{ship['propulsion'] + 1}"
                        )
                placed_modules = (
                    ship["habitation"] + ship["life_support"]
                    + ship["solar_panels"]
                )
                if placed_modules < ship["modules"]:
                    for part in ("habitation", "life_support", "solar_panels"):
                        if ship[part] < 4:
                            expected_spaceship_capabilities.add(
                                f"place_component:{part}:{ship[part] + 1}"
                            )
                if ship["can_launch"]:
                    expected_spaceship_capabilities.add("launch:none:-1")
        elif request.actor_kind == "unit":
            expected = {
                action["slot"]: action for action in parsed.actions
                if action["actor_ref"] == request.native_actor_ref
            }
        elif request.actor_kind == "city":
            expected = {}
            city = next((
                item for item in parsed.cities
                if item["ref"] == request.native_actor_ref
            ), None)
            if city is None:
                _fail()
            city_tiles = list(self._city_scope_rows(
                snapshot, request.native_actor_ref, "city_citizens",
                kind="city_tile",
            ))
            specialists = list(self._city_scope_rows(
                snapshot, request.native_actor_ref, "city_citizens",
                kind="city_specialist",
            ))
            build_choices = self._city_scope_rows(
                snapshot, request.native_actor_ref, "city_build_choices",
            )
            worklist = self._city_scope_rows(
                snapshot, request.native_actor_ref, "city_worklist",
            )
            improvements = self._city_scope_rows(
                snapshot, request.native_actor_ref, "city_improvements",
            )
            rally = next((
                item for item in parsed.city_rallies
                if item["city_ref"] == request.native_actor_ref
            ), None)
            governor_rows = self._city_scope_rows(
                snapshot, request.native_actor_ref, "city_governor",
            )
            if (
                rally is None
                or len(governor_rows) != int(city["governor_enabled"])
            ):
                _fail()
            governor = governor_rows[0] if governor_rows else None
            population_specialists = [
                item for item in specialists
                if item["counts_toward_population"]
            ]
            positive = sorted(
                item["native_id"] for item in population_specialists
                if item["count"] > 0
            )
            default = next(
                item["native_id"] for item in population_specialists
                if item["is_default"]
            )
            expected_city_capabilities = set()
            if action_eligible:
                expected_city_capabilities.update({
                    "set_options", "rename", "set_governor",
                })
                if governor is not None:
                    expected_city_capabilities.add("clear_governor")
                if rally["active"]:
                    expected_city_capabilities.add("clear_rally")
                if any(item["can_queue"] for item in build_choices) or worklist:
                    expected_city_capabilities.add("set_worklist")
                expected_city_capabilities.update(
                    f"sell_improvement:{item['native_id']}"
                    for item in improvements if item["sellable"]
                )
                for choice in build_choices:
                    if (
                        city["can_change"]
                        and choice["can_build_now"]
                        and (
                            choice["production_kind"],
                            choice["production_native_id"],
                        ) != (
                            city["production_kind"],
                            city["production_native_id"],
                        )
                    ):
                        expected_city_capabilities.add(
                            "set_production:"
                            f"{choice['production_kind']}:"
                            f"{choice['production_native_id']}"
                        )
                if city["can_buy"]:
                    expected_city_capabilities.add(
                        "buy_production:"
                        f"{city['production_kind']}:"
                        f"{city['production_native_id']}"
                    )
                if governor is None:
                    for tile in city_tiles:
                        if tile["free_worked"]:
                            continue
                        if tile["worked"]:
                            expected_city_capabilities.add(
                                f"unwork_tile:{tile['native_tile']}:{default}"
                            )
                        elif tile["can_work"] and positive:
                            expected_city_capabilities.add(
                                f"work_tile:{tile['native_tile']}:{positive[0]}"
                            )
                    for source in population_specialists:
                        if source["count"] <= 0:
                            continue
                        for target in population_specialists:
                            if (
                                source["native_id"] != target["native_id"]
                                and target["can_use"]
                            ):
                                expected_city_capabilities.add(
                                    "set_specialist:"
                                    f"{source['native_id']}:"
                                    f"{target['native_id']}"
                                )
        else:
            _fail()
        expected_total = len(expected)
        if request.actor_kind == "player":
            expected_total += (
                len(expected_government_capabilities)
                + len(expected_multiplier_capabilities)
                + len(expected_spaceship_capabilities)
            )
        elif request.actor_kind == "city":
            expected_total += len(expected_city_capabilities)
        if (
            total < len(expected)
            or (request.actor_kind == "player" and total != expected_total)
            or (request.actor_kind == "city" and total < expected_total)
        ):
            _fail()

        descriptors_by_slot: dict[str, Mapping[str, Any]] = {}
        bindings_by_slot: dict[str, _ActionBinding] = {}
        for descriptor in snapshot.legal_actions:
            action_id = descriptor["action_id"]
            binding = snapshot.action_bindings.get(action_id)
            if binding is None or binding.slot in descriptors_by_slot:
                _fail()
            descriptors_by_slot[binding.slot] = descriptor
            bindings_by_slot[binding.slot] = binding
        global_by_slot = {
            action["slot"]: action for action in parsed.actions
        }

        prior_slots = set(seen_slots)
        prior_capabilities = set(request.seen_capabilities)
        prior_scope_bindings = dict(
            request.pending_scope_bindings
        )
        if (
            len(prior_slots) != len(seen_slots)
            or len(seen_slots) != offset
            or len(prior_capabilities) != len(request.seen_capabilities)
            or len(request.seen_capabilities) != offset
            or len(prior_scope_bindings)
            != len(request.pending_scope_bindings)
            or len(prior_scope_bindings) != len(request.seen_capabilities)
        ):
            _fail()
        for action_id, binding in prior_scope_bindings.items():
            if (
                action_id != self._mac(
                    "action", "scope", revision,
                    request.native_actor_ref, binding.slot,
                )
                or binding.slot not in prior_slots
                or binding.native_revision != revision
                or binding.actor_ref != request.native_actor_ref
                or not binding.scoped
                or (
                    request.actor_kind == "city"
                    and binding.public_kind not in {
                        "city.set_production", "city.buy_production",
                        "city.assign_citizen", "city.set_specialist",
                        "city.set_worklist", "city.set_options",
                        "city.rename", "city.sell_improvement",
                        "city.set_rally", "city.set_governor",
                        "city.manage_worker_task",
                    }
                )
                or (
                    request.actor_kind == "unit"
                    and binding.public_kind not in {
                        "unit.perform_action", "unit.order",
                    }
                )
            ):
                _fail()
        items: list[dict[str, Any]] = []
        page_slots: list[str] = []
        page_capabilities: list[str] = []
        page_pending_bindings: list[tuple[str, _ActionBinding]] = []
        catalog_target_tiles: set[int] = set()
        catalog_id = self._mac(
            "catalog", "actor-scope", snapshot.native_revision,
            request.native_actor_ref, view_id,
        )
        public_scope = {
            "actor_id": request.actor_id,
            "actor_type": request.actor_kind,
        }
        for row in rows:
            if not isinstance(row, str):
                _fail()
            try:
                encoded = row.encode("ascii", "strict")
            except UnicodeEncodeError:
                _fail()
            if not 1 <= len(encoded) <= MAX_NATIVE_ROW_BYTES:
                _fail()
            parts = row.split(" ")
            if parts[0] != "action" or len(parts) != len(_ROW_FIELDS["action"]) + 1:
                _fail()
            pairs: list[tuple[str, str]] = []
            for token in parts[1:]:
                if token.count("=") != 1:
                    _fail()
                key, value = token.split("=", 1)
                if not key or not value:
                    _fail()
                pairs.append((key, value))
            if tuple(key for key, _ in pairs) != _ROW_FIELDS["action"]:
                _fail()
            action = self._parse_row("action", dict(pairs))
            if (
                request.actor_kind == "unit"
                and action["native_target_tile"] >= 0
            ):
                catalog_target_tiles.add(action["native_target_tile"])
            slot = action["slot"]
            if (
                not slot.startswith("a")
                or slot in prior_slots or slot in page_slots
            ):
                _fail()
            scoped_id = self._mac(
                "action", "scope", revision, request.native_actor_ref, slot,
            )
            global_action = global_by_slot.get(slot)
            if global_action is not None:
                if expected.get(slot) != action:
                    _fail()
                descriptor = descriptors_by_slot.get(slot)
                base_binding = bindings_by_slot.get(slot)
                if descriptor is None or base_binding is None:
                    _fail()
                public = _thaw(descriptor)
                public["action_id"] = scoped_id
                scoped_binding = _ActionBinding(
                    slot=base_binding.slot,
                    native_revision=base_binding.native_revision,
                    argument_contract=base_binding.argument_contract,
                    public_kind=base_binding.public_kind,
                    operation=base_binding.operation,
                    turn=base_binding.turn,
                    phase=base_binding.phase,
                    max_rate=base_binding.max_rate,
                    argument_max=base_binding.argument_max,
                    argument_min=base_binding.argument_min,
                    argument_step=base_binding.argument_step,
                    argument_excluded=base_binding.argument_excluded,
                    actor_ref=request.native_actor_ref,
                    scoped=True,
                )
                capability_key = f"global:{slot}"
            else:
                public, scoped_binding = self._project_scoped_action(
                    snapshot, request, action, scoped_id,
                )
                capability_key = (
                    (
                        f"{scoped_binding.operation}:"
                        f"{action['spaceship_part']}:"
                        f"{action['spaceship_value']}"
                        if scoped_binding.operation in {
                            "place_component", "launch",
                        }
                        else f"set_multiplier:"
                             f"{action['native_target_multiplier']}"
                        if scoped_binding.operation == "set_multiplier"
                        else f"{scoped_binding.operation}:"
                             f"{action['native_target_government']}"
                    )
                    if request.actor_kind == "player"
                    else (
                        f"{scoped_binding.operation}:"
                        f"{action['native_target_tile']}:"
                        f"{action['activity']}:"
                        f"{action['native_target_extra']}"
                        if request.actor_kind == "city"
                        and scoped_binding.operation in {
                            "request_worker_task", "change_worker_task",
                            "remove_worker_task",
                        }
                        else
                        f"{scoped_binding.operation}:"
                        f"{action['target_build_kind']}:"
                        f"{action['native_target_build']}"
                        if request.actor_kind == "city"
                        and scoped_binding.operation in {
                            "set_production", "buy_production",
                        }
                        else
                        f"{scoped_binding.operation}:"
                        f"{action['native_target_tile']}:"
                        f"{action['native_source_specialist']}"
                        if request.actor_kind == "city"
                        and scoped_binding.operation == "work_tile"
                        else f"{scoped_binding.operation}:"
                             f"{action['native_target_tile']}:"
                             f"{action['native_target_specialist']}"
                        if request.actor_kind == "city"
                        and scoped_binding.operation == "unwork_tile"
                        else f"{scoped_binding.operation}:"
                             f"{action['native_source_specialist']}:"
                             f"{action['native_target_specialist']}"
                        if request.actor_kind == "city"
                        and scoped_binding.operation == "set_specialist"
                        else f"sell_improvement:"
                             f"{action['native_target_build']}"
                        if request.actor_kind == "city"
                        and scoped_binding.operation == "sell_improvement"
                        else scoped_binding.operation
                        if request.actor_kind == "city"
                        and scoped_binding.operation in {
                            "set_worklist", "set_options", "rename",
                            "clear_rally", "set_governor",
                            "clear_governor",
                        }
                        else f"{scoped_binding.operation}:"
                             f"{action['source_city_ref'] or 'none'}:"
                             f"{action['destination_city_ref']}:"
                             f"{action['target_build_kind']}:"
                             f"{action['native_target_build']}"
                        if request.actor_kind == "unit"
                        and scoped_binding.operation in {
                            "upgrade", "rehome", "join_city",
                            "establish_trade", "marketplace", "help_wonder",
                            "disband_recover",
                        }
                        else f"scoped:{slot}"
                    )
                )
            if (
                capability_key in prior_capabilities
                or capability_key in page_capabilities
            ):
                _fail()
            candidate_offset = offset + len(items) + 1
            probe = self._scoped_public_page(
                snapshot,
                public_scope,
                (*items, public),
                total,
                (
                    "cursor_" + "x" * 32
                    if candidate_offset < total else None
                ),
                (
                    "2000-01-01T00:00:00.000Z"
                    if candidate_offset < total else None
                ),
                catalog_id,
                candidate_offset == total,
            )
            if (
                not materialize
                and self._canonical_public_bytes(probe)
                    > MAX_PUBLIC_PAGE_BYTES
            ):
                if not items:
                    raise V2ControlError("scope_too_large")
                break
            items.append(public)
            page_slots.append(slot)
            page_capabilities.append(capability_key)
            # A scoped ID is never executable from a prefix page, including a
            # player-scoped alias of an independently executable global ID.
            # Publish the complete scoped catalog atomically only after all
            # native count, uniqueness, and completeness checks pass.
            page_pending_bindings.append((scoped_id, scoped_binding))

        all_seen = seen_slots + tuple(page_slots)
        all_capabilities = request.seen_capabilities + tuple(page_capabilities)
        pending_scope_bindings = (
            request.pending_scope_bindings
            + tuple(page_pending_bindings)
        )
        public_next_offset = offset + len(items)
        if public_next_offset == total:
            if not set(expected).issubset(all_seen):
                _fail()
            if request.actor_kind == "player" and set(all_capabilities) != (
                {f"global:{slot}" for slot in expected}
                | expected_government_capabilities
                | expected_multiplier_capabilities
                | expected_spaceship_capabilities
            ):
                _fail()
            if (
                request.actor_kind == "city"
                and not expected_city_capabilities.issubset(all_capabilities)
            ):
                _fail()
        if materialize:
            if public_next_offset != total:
                _fail()
            if request.actor_kind == "unit":
                overlay_key = (
                    snapshot.native_revision, request.native_actor_ref,
                )
                overlay = self._actor_tile_overlays.get(overlay_key)
                if overlay is None or {
                    item["native_index"] for item in overlay
                } != catalog_target_tiles:
                    _fail()
            return (
                self._scoped_public_page(
                    snapshot, public_scope, items, total, None, None,
                    catalog_id, True,
                ),
                pending_scope_bindings,
            )
        staged = self._scoped_public_page(
            snapshot,
            public_scope,
            items,
            total,
            (
                "cursor_" + "x" * 32
                if public_next_offset < total else None
            ),
            (
                "2000-01-01T00:00:00.000Z"
                if public_next_offset < total else None
            ),
            catalog_id,
            public_next_offset == total,
        )
        self._checked_public_page(staged)
        next_cursor = None
        if public_next_offset < total:
            next_cursor = self._new_actor_scope_cursor(
                request, view_id, total, public_next_offset, all_seen,
                all_capabilities, pending_scope_bindings,
            )
        else:
            for action_id, binding in pending_scope_bindings:
                self._publish_scoped_binding(action_id, binding)
        return self._checked_public_page(self._scoped_public_page(
            snapshot,
            public_scope,
            items,
            total,
            next_cursor,
            self._cursor_expires_at(next_cursor),
            catalog_id,
            public_next_offset == total,
        ))

    def _parse_scoped_action_row(self, row: Any) -> Mapping[str, Any]:
        if not isinstance(row, str):
            _fail()
        try:
            encoded = row.encode("ascii", "strict")
        except UnicodeEncodeError:
            _fail()
        if not 1 <= len(encoded) <= MAX_NATIVE_ROW_BYTES:
            _fail()
        parts = row.split(" ")
        if parts[0] != "action" or len(parts) != len(_ROW_FIELDS["action"]) + 1:
            _fail()
        pairs: list[tuple[str, str]] = []
        for token in parts[1:]:
            if token.count("=") != 1:
                _fail()
            key, value = token.split("=", 1)
            if not key or not value:
                _fail()
            pairs.append((key, value))
        if tuple(key for key, _ in pairs) != _ROW_FIELDS["action"]:
            _fail()
        return self._parse_row("action", dict(pairs))

    def _parse_state_scope_row(
        self, row: Any, allowed_kinds: frozenset[str],
    ) -> tuple[str, Mapping[str, Any]]:
        if not isinstance(row, str):
            _fail()
        try:
            encoded = row.encode("ascii", "strict")
        except UnicodeEncodeError:
            _fail()
        if not 1 <= len(encoded) <= MAX_NATIVE_ROW_BYTES:
            _fail()
        parts = row.split(" ")
        kind = parts[0]
        if kind not in allowed_kinds:
            _fail()
        pairs: list[tuple[str, str]] = []
        for token in parts[1:]:
            if token.count("=") != 1:
                _fail()
            key, value = token.split("=", 1)
            if not key or not value:
                _fail()
            pairs.append((key, value))
        schema_kind = kind
        if kind == "unit":
            if len(pairs) < 2 or pairs[1][0] != "scope":
                _fail()
            schema_kind = f"unit_{pairs[1][1]}"
        if (
            schema_kind not in _ROW_FIELDS
            or tuple(key for key, _ in pairs) != _ROW_FIELDS[schema_kind]
        ):
            _fail()
        return kind, self._parse_row(kind, dict(pairs))

    def _validate_state_scope_catalog(
        self,
        snapshot: _ProjectedSnapshot,
        request: V2StateScopeRequest,
        native_catalog: Mapping[str, Any],
    ) -> tuple[
        tuple[Mapping[str, Any], ...],
        tuple[tuple[str, int, int, int], ...],
        tuple[Mapping[str, Any], ...],
    ]:
        if not isinstance(native_catalog, Mapping) or set(native_catalog) != {
            "generation", "native_revision", "section", "selector",
            "view_id", "offset", "count", "total_count", "next_offset",
            "complete", "overflow", "rows",
        }:
            _fail()
        view_id = native_catalog["view_id"]
        total = native_catalog["total_count"]
        rows = native_catalog["rows"]
        if (
            native_catalog["generation"] != self.generation
            or native_catalog["native_revision"] != request.native_revision
            or native_catalog["section"] != request.section
            or native_catalog["selector"] != request.selector
            or not isinstance(view_id, str)
            or _STATE_SCOPE_VIEW.fullmatch(view_id) is None
            or int(view_id[1:].split("-", 1)[0]) != request.native_revision
            or isinstance(total, bool) or not isinstance(total, int)
            or not 0 <= total <= MAX_NATIVE_STATE_SCOPE_ROWS
            or native_catalog["offset"] != 0
            or native_catalog["count"] != total
            or native_catalog["next_offset"] != total
            or native_catalog["complete"] is not True
            or native_catalog["overflow"] is not False
            or not isinstance(rows, tuple) or len(rows) != total
        ):
            _fail()
        allowed = {
            "chat_recipients": frozenset({"chat_recipient"}),
            "pregame_nations": frozenset({"pregame_nation"}),
            "pregame_styles": frozenset({"pregame_style"}),
            "pregame_teams": frozenset({
                "pregame_team", "pregame_team_member",
            }),
            "known_tiles": frozenset({"tile"}),
            "map_tiles": frozenset({"tile"}),
            "tile_window": frozenset({"tile_local", "tile_extra"}),
            "target_tiles": frozenset({"tile_local", "tile_extra"}),
            "action_decision_tile": frozenset({
                "tile_local", "tile_extra",
            }),
            "diplomacy_clauses": frozenset({"diplomacy_clause"}),
            "city_citizens": frozenset({"city_tile", "city_specialist"}),
            "city_build_choices": frozenset({"city_build_choice"}),
            "city_worklist": frozenset({"city_worklist"}),
            "city_improvements": frozenset({"city_improvement"}),
            "city_trade_routes": frozenset({"city_trade_route"}),
            "city_governor": frozenset({"city_governor"}),
            "unit_route": frozenset({"unit_route_step"}),
        }.get(request.section)
        if allowed is None:
            _fail()
        parsed_pairs = tuple(
            self._parse_state_scope_row(row, allowed) for row in rows
        )
        parsed_rows = tuple(item for _, item in parsed_pairs)
        public: list[dict[str, Any]] = []
        tile_bindings: list[tuple[str, int, int, int]] = []
        if request.section == "chat_recipients":
            self_ref = (
                snapshot.parsed.pregame["ref"]
                if snapshot.parsed.pregame is not None
                else snapshot.parsed.player["ref"]
                if snapshot.parsed.player is not None
                else None
            )
            self_rows = tuple(item for item in parsed_rows if item["self"])
            if (
                request.selector != "-"
                or snapshot.parsed.meta["state"] not in {
                    "preparing", "running",
                }
                or self_ref is None or total < 1
                or len({item["ref"] for item in parsed_rows}) != total
                or len({item["name"] for item in parsed_rows}) != total
                or len(self_rows) != 1 or self_rows[0]["ref"] != self_ref
                or any(
                    item["can_message"] and not item["connected"]
                    for item in parsed_rows
                )
            ):
                _fail()
            ordered = tuple(sorted(
                parsed_rows, key=lambda item: item["parsed_ref"],
            ))
            public.extend({
                "id": self._entity_id("player", item["ref"]),
                "name": item["name"],
                "self": item["self"],
                "connected": item["connected"],
                "can_message": item["can_message"],
            } for item in ordered)
            return (
                tuple(_freeze(item) for item in public), (),
                tuple(_freeze(item) for item in ordered),
            )
        if request.section == "unit_route":
            binding = snapshot.actor_bindings.get(request.actor_id)
            native_unit = next((
                item for item in snapshot.parsed.units
                if item["scope"] == "own"
                and item["ref"] == request.selector
            ), None)
            summary = next((
                item for item in snapshot.parsed.unit_routes
                if item["unit_ref"] == request.selector
            ), None)
            public_unit = next((
                item for item in snapshot.sections["units"]
                if item["id"] == request.actor_id
            ), None)
            if (
                binding is None or binding.kind != "unit"
                or binding.native_ref != request.selector
                or native_unit is None or summary is None
                or public_unit is None or public_unit.get("route") is None
                or not summary["reconstructable"]
                or total != summary["step_count"]
                or request.relation_id is not None
                or request.center_id is not None or request.radius is not None
            ):
                _fail()
            ordered = sorted(parsed_rows, key=lambda item: item["sequence"])
            if (
                [item["sequence"] for item in ordered] != list(range(total))
                or any(item["unit_ref"] != request.selector for item in ordered)
            ):
                _fail()
            prior_x, prior_y = self._map_coordinates_for_native_index(
                snapshot.parsed.meta, native_unit["native_tile"],
            )
            for item in ordered:
                x, y = self._map_coordinates_for_native_index(
                    snapshot.parsed.meta, item["native_tile"],
                )
                distance = self._map_distance(
                    snapshot.parsed.meta, prior_x, prior_y, x, y,
                )
                if (
                    item["kind"] == "wait" and distance != 0
                    or item["kind"] != "wait" and distance != 1
                ):
                    _fail()
                tile_id = self._tile_id(item["native_tile"])
                public.append({
                    "unit_id": request.actor_id,
                    "route_id": public_unit["route"]["id"],
                    "sequence": item["sequence"],
                    "kind": item["kind"],
                    "tile": {"id": tile_id, "x": x, "y": y},
                })
                tile_bindings.append((tile_id, item["native_tile"], x, y))
                prior_x, prior_y = x, y
            return (
                tuple(_freeze(item) for item in public),
                tuple(tile_bindings), parsed_rows,
            )
        if request.section == "pregame_nations":
            pregame = snapshot.parsed.pregame
            if (
                request.selector != "-" or pregame is None
                or total != pregame["nation_choices"]
                or len({item["native_id"] for item in parsed_rows}) != total
                or len({item["name"] for item in parsed_rows}) != total
            ):
                _fail()
            public.extend({
                "id": self._mac("nation", "nation", item["native_id"]),
                "name": item["name"],
                "default_style_id": self._mac(
                    "style", "style", item["default_style_native_id"],
                ),
            } for item in sorted(parsed_rows, key=lambda item: item["native_id"]))
            return tuple(_freeze(item) for item in public), (), parsed_rows
        if request.section == "pregame_styles":
            pregame = snapshot.parsed.pregame
            if (
                request.selector != "-" or pregame is None
                or total != pregame["style_choices"]
                or len({item["native_id"] for item in parsed_rows}) != total
                or len({item["name"] for item in parsed_rows}) != total
            ):
                _fail()
            public.extend({
                "id": self._mac("style", "style", item["native_id"]),
                "name": item["name"],
            } for item in sorted(parsed_rows, key=lambda item: item["native_id"]))
            return tuple(_freeze(item) for item in public), (), parsed_rows
        if request.section == "pregame_teams":
            pregame = snapshot.parsed.pregame
            teams = tuple(
                item for kind, item in parsed_pairs
                if kind == "pregame_team"
            )
            members = tuple(
                item for kind, item in parsed_pairs
                if kind == "pregame_team_member"
            )
            if (
                request.selector != "-" or pregame is None
                or len(teams) != pregame["team_choices"]
                or total != len(teams) + len(members)
                or len({item["native_id"] for item in teams}) != len(teams)
                or len({item["name"] for item in teams}) != len(teams)
                or len({item["player_ref"] for item in members}) != len(members)
            ):
                _fail()
            by_id = {item["native_id"]: item for item in teams}
            members_by_team: dict[int, list[Mapping[str, Any]]] = {
                native_id: [] for native_id in by_id
            }
            for member in members:
                team_members = members_by_team.get(member["native_team_id"])
                if team_members is None:
                    _fail()
                team_members.append(member)
            selected = tuple(item for item in teams if item["selected"])
            if len(selected) != 1:
                _fail()
            selected_team = selected[0]
            selected_members = members_by_team[selected_team["native_id"]]
            if (
                all(item["player_ref"] != pregame["ref"]
                    for item in selected_members)
                or any(
                    item["player_ref"] == pregame["ref"]
                    for team in teams if not team["selected"]
                    for item in members_by_team[team["native_id"]]
                )
            ):
                _fail()
            unused_count = sum(not item["occupied"] for item in teams)
            if (
                unused_count > 1
                or unused_count and len(selected_members) <= 1
            ):
                _fail()
            enriched: list[dict[str, Any]] = []
            for team in sorted(teams, key=lambda item: item["native_id"]):
                team_members = sorted(
                    members_by_team[team["native_id"]],
                    key=lambda item: item["parsed_player_ref"],
                )
                if (
                    len(team_members) != team["member_count"]
                    or team["occupied"] is not bool(team_members)
                ):
                    _fail()
                enriched_team = dict(team)
                enriched_team["members"] = tuple(team_members)
                enriched.append(enriched_team)
                public.append({
                    "id": self._mac(
                        "team", "team", snapshot.native_revision,
                        team["native_id"],
                    ),
                    "name": team["name"],
                    "selected": team["selected"],
                    "occupied": team["occupied"],
                    "member_count": team["member_count"],
                    "members": [{
                        "id": self._entity_id(
                            "player", member["player_ref"],
                        ),
                        "leader_name": member["leader_name"],
                        "self": member["player_ref"] == pregame["ref"],
                    } for member in team_members],
                })
            return (
                tuple(_freeze(item) for item in public), (),
                tuple(_freeze(item) for item in enriched),
            )
        if request.section in {
            "known_tiles", "map_tiles", "tile_window", "target_tiles",
            "action_decision_tile",
        }:
            tile_rows = tuple(
                item for kind, item in parsed_pairs
                if kind in {"tile", "tile_local"}
            )
            extra_rows = tuple(
                item for kind, item in parsed_pairs if kind == "tile_extra"
            )
            if (
                request.section in {"known_tiles", "map_tiles"}
                and (len(tile_rows) != total or extra_rows)
                or request.section not in {"known_tiles", "map_tiles"}
                   and len(tile_rows) + len(extra_rows) != total
                or len({item["native_index"] for item in tile_rows})
                   != len(tile_rows)
            ):
                _fail()
            if len({(item["x"], item["y"]) for item in tile_rows}) \
                    != len(tile_rows):
                _fail()
            for item in tile_rows:
                self._validate_tile_coordinates(
                    snapshot.parsed.meta, item,
                    canonical=request.section in {"map_tiles", "target_tiles"},
                )
            tile_by_native = {
                item["native_index"]: item for item in tile_rows
            }
            extras_by_tile: dict[int, list[Mapping[str, Any]]] = {
                native_index: [] for native_index in tile_by_native
            }
            extra_contracts: dict[int, tuple[str, int]] = {}
            seen_tile_extras: set[tuple[int, int]] = set()
            for extra in extra_rows:
                parent = tile_by_native.get(extra["native_tile"])
                key = (extra["native_tile"], extra["native_id"])
                contract = (extra["name"], extra["cause_mask"])
                prior = extra_contracts.setdefault(extra["native_id"], contract)
                if (
                    parent is None or parent["known"] == 0
                    or key in seen_tile_extras or prior != contract
                ):
                    _fail()
                seen_tile_extras.add(key)
                extras_by_tile[extra["native_tile"]].append(extra)
            resource_bit = 1 << _EXTRA_CAUSE_TAGS.index("resource")
            for item in tile_rows:
                tile_extras = extras_by_tile[item["native_index"]]
                resources = [
                    extra for extra in tile_extras
                    if extra["cause_mask"] & resource_bit
                ]
                if request.section not in {"known_tiles", "map_tiles"} and (
                    item["known"] == 0 and tile_extras
                    or item["known"] != 0 and (
                        item["resource_extra"] == -1 and resources
                        or item["resource_extra"] != -1 and (
                            len(resources) != 1
                            or resources[0]["native_id"]
                               != item["resource_extra"]
                            or resources[0]["name"]
                               != item["resource_name"]
                        )
                    )
                ):
                    _fail()
            if request.section == "known_tiles":
                if (
                    request.selector != "-"
                    or total != snapshot.parsed.meta["known_tile_count"]
                    or any(item["known"] == 0 for item in tile_rows)
                ):
                    _fail()
                center: Mapping[str, Any] | None = None
            elif request.section == "map_tiles":
                meta = snapshot.parsed.meta
                if (
                    request.selector != "-"
                    or len(tile_rows) != meta["map_width"] * meta["map_height"]
                    or {item["native_index"] for item in tile_rows}
                       != set(range(len(tile_rows)))
                ):
                    _fail()
                center = None
            elif request.section == "tile_window":
                match = re.fullmatch(r"t([0-9]+)-r([0-8])", request.selector)
                if match is None or request.radius != int(match.group(2)):
                    _fail()
                center_native = int(match.group(1))
                center = next((
                    item for item in tile_rows
                    if item["native_index"] == center_native
                ), None)
                if center is None or center["known"] == 0:
                    _fail()
            elif request.section == "action_decision_tile":
                binding = snapshot.actor_bindings.get(request.actor_id)
                if (
                    binding is None or binding.kind != "unit"
                    or binding.native_ref != request.selector
                    or request.relation_id is not None
                    or request.center_id is not None
                    or request.radius is not None
                    or len(tile_rows) != 1 or extra_rows
                ):
                    _fail()
                expected = next((
                    value for value in snapshot.action_decision_bindings.values()
                    if value.actor_id == request.actor_id
                ), None)
                if (
                    expected is None
                    or tile_rows[0]["native_index"]
                       != expected.native_target_tile
                ):
                    _fail()
                center = None
            else:
                binding = snapshot.actor_bindings.get(request.actor_id)
                if (
                    binding is None or binding.kind != "unit"
                    or binding.native_ref != request.selector
                    or request.relation_id is not None
                    or request.center_id is not None or request.radius is not None
                ):
                    _fail()
                center = None
            for item in tile_rows:
                public_id = self._tile_id(item["native_index"])
                projected: dict[str, Any] = {
                    "id": public_id,
                    "x": item["x"],
                    "y": item["y"],
                    "visibility": (
                        "unknown" if item["known"] == 0
                        else "remembered" if item["known"] == 1
                        else "visible"
                    ),
                }
                if item["known"] != 0:
                    projected["terrain"] = item["terrain"]
                    projected["owner_player_id"] = (
                        self._entity_id("player", item["owner_ref"])
                        if item["owner_ref"] is not None else None
                    )
                    if request.section not in {"known_tiles", "map_tiles"}:
                        projected["infrastructure_placement"] = ({
                            "extra_id": self._extra_id(
                                item["placing_extra"],
                            ),
                            "name": item["placing_extra_name"],
                            "turns_remaining": item["placing_turns"],
                        } if item["placing_extra"] != -1 else None)
                        projected["resource"] = ({
                            "extra_id": self._extra_id(
                                item["resource_extra"],
                            ),
                            "name": item["resource_name"],
                        } if item["resource_extra"] != -1 else None)
                        projected["label"] = item["label"]
                        projected["yields"] = dict(item["yields"])
                        projected["extras"] = [{
                            "extra_id": self._extra_id(extra["native_id"]),
                            "name": extra["name"],
                            "causes": (
                                ["special"] if extra["cause_mask"] == 0
                                else [
                                    cause
                                    for bit, cause in enumerate(
                                        _EXTRA_CAUSE_TAGS,
                                    )
                                    if extra["cause_mask"] & (1 << bit)
                                ]
                            ),
                        } for extra in sorted(
                            extras_by_tile[item["native_index"]],
                            key=lambda value: value["native_id"],
                        )]
                if item["known"] != 0 or request.section == "map_tiles":
                    tile_bindings.append((
                        public_id, item["native_index"], item["x"], item["y"],
                    ))
                if center is not None:
                    distance = self._map_distance(
                        snapshot.parsed.meta,
                        center["x"], center["y"], item["x"], item["y"],
                    )
                    if distance > request.radius:
                        _fail()
                    projected["distance"] = distance
                public.append(projected)
            if center is not None:
                public.sort(key=lambda item: (
                    item["distance"], item["y"], item["x"], item["id"],
                ))
            else:
                public.sort(key=lambda item: (item["y"], item["x"], item["id"]))
            return (
                tuple(_freeze(item) for item in public),
                tuple(tile_bindings),
                tile_rows,
            )

        if request.section == "diplomacy_clauses":
            relation = next((
                item for item in snapshot.parsed.diplomacy
                if item["other_ref"] == request.selector
            ), None)
            public_relation = next((
                item for item in snapshot.sections["diplomacy"]
                if item["relation_id"] == request.relation_id
            ), None)
            binding = (
                snapshot.relation_bindings.get(request.relation_id)
                if request.relation_id is not None else None
            )
            self_ref = (
                snapshot.parsed.player["ref"]
                if snapshot.parsed.player is not None else None
            )
            if (
                relation is None or public_relation is None or binding is None
                or binding.native_counterpart_ref != request.selector
                or total != relation["clause_count"]
                or [item["position"] for item in parsed_rows]
                   != list(range(total))
                or any(
                    item["other_ref"] != request.selector
                    or item["generation"] != relation["generation"]
                    or item["giver_ref"] not in {self_ref, request.selector}
                    for item in parsed_rows
                )
                or _diplomacy_clauses_digest(parsed_rows)
                   != relation["clauses_digest"]
                or bool(total) and not relation["meeting"]
            ):
                _fail()
            site_by_number = {
                item["parsed_ref"][1]: item
                for item in snapshot.parsed.city_sites
            }
            self_player_id = (
                self._entity_id("player", self_ref)
                if self_ref is not None else None
            )
            for item in parsed_rows:
                expected_kind = {
                    "Advance": "technology", "Gold": "gold",
                    "City": "city",
                }.get(item["clause_type"], "none")
                if item["clause_type"] == "City":
                    if item["value_kind"] not in {
                        "city", "city_unavailable",
                    }:
                        _fail()
                elif item["value_kind"] != expected_kind:
                    _fail()
                if expected_kind == "none" and (
                    item["native_value"] != 0 or item["name"] != "none"
                ):
                    _fail()
                value: dict[str, Any] | None = None
                if item["value_kind"] == "technology":
                    value = {
                        "type": "technology",
                        "id": self._tech_id(item["native_value"]),
                        "name": item["name"],
                    }
                elif item["value_kind"] == "gold":
                    value = {
                        "type": "gold", "amount": item["native_value"],
                    }
                elif item["value_kind"] == "city":
                    site = site_by_number.get(item["native_value"])
                    if site is None or site["name"] != item["name"]:
                        _fail()
                    value = {
                        "type": "city",
                        "id": self._entity_id("city", site["ref"]),
                        "name": site["name"],
                    }
                elif item["value_kind"] == "city_unavailable":
                    if (
                        item["native_value"] in site_by_number
                        or item["name"] != "unavailable"
                    ):
                        _fail()
                    value = {
                        "type": "city",
                        "id": self._mac(
                            "city", "unavailable-treaty-city",
                            item["other_ref"], item["generation"],
                            item["giver_ref"], item["native_value"],
                        ),
                        "name": "Unavailable city",
                        "available": False,
                    }
                giver_is_self = item["giver_ref"] == self_ref
                public.append({
                    "clause_id": self._mac(
                        "clause", "clause", item["other_ref"],
                        item["generation"], item["giver_ref"],
                        item["clause_type"], item["native_value"],
                    ),
                    "relation_id": request.relation_id,
                    "meeting_id": public_relation["meeting"]["meeting_id"],
                    "position": item["position"],
                    "type": _DIPLOMACY_CLAUSE_PUBLIC_TYPES[
                        item["clause_type"]
                    ],
                    "giver_player_id": (
                        self_player_id if giver_is_self
                        else public_relation["player_id"]
                    ),
                    "receiver_player_id": (
                        public_relation["player_id"] if giver_is_self
                        else self_player_id
                    ),
                    "value": value,
                })
            return (
                tuple(_freeze(item) for item in public), (), parsed_rows,
            )

        city = next((
            item for item in snapshot.parsed.cities
            if item["ref"] == request.selector
        ), None)
        binding = (
            snapshot.actor_bindings.get(request.actor_id)
            if isinstance(request.actor_id, str) else None
        )
        if (
            city is None or binding is None or binding.kind != "city"
            or binding.native_ref != request.selector
            or any(item["city_ref"] != request.selector for item in parsed_rows)
        ):
            _fail()
        expected_total = {
            "city_citizens": (
                city["citizen_tile_count"] + city["specialist_type_count"]
            ),
            "city_build_choices": city["build_choice_count"],
            "city_worklist": city["worklist_length"],
            "city_improvements": city["improvement_count"],
            "city_trade_routes": city["trade_route_count"],
            "city_governor": int(city["governor_enabled"]),
        }[request.section]
        if total != expected_total:
            _fail()
        city_id = request.actor_id
        if request.section == "city_citizens":
            tiles = [
                item for kind, item in parsed_pairs if kind == "city_tile"
            ]
            specialists = [
                item for kind, item in parsed_pairs if kind == "city_specialist"
            ]
            population_ids = {
                item["native_id"] for item in specialists
                if item["counts_toward_population"]
            }
            if (
                len(tiles) != city["citizen_tile_count"]
                or len(specialists) != city["specialist_type_count"]
                or len({item["native_tile"] for item in tiles}) != len(tiles)
                or len({item["native_id"] for item in specialists})
                   != len(specialists)
                or {item["native_id"] for item in specialists}
                   != set(range(len(specialists)))
                or population_ids != set(range(len(population_ids)))
                or sum(item["free_worked"] for item in tiles) != 1
                or sum(
                    item["native_tile"] == city["native_tile"]
                    and item["worked"] and item["free_worked"]
                    for item in tiles
                ) != 1
                or any(
                    item["free_worked"] and not item["worked"]
                    for item in tiles
                )
                or sum(
                    item["worked"] and not item["free_worked"]
                    for item in tiles
                ) + sum(
                    item["count"] for item in specialists
                    if item["counts_toward_population"]
                )
                   != city["size"]
                or not _city_citizen_metrics_match(
                    city, tiles, specialists,
                )
                or sum(item["is_default"] for item in specialists) != 1
                or any(
                    item["is_default"]
                    and not item["counts_toward_population"]
                    for item in specialists
                )
                or any(
                    item["can_use"]
                    and not item["counts_toward_population"]
                    for item in specialists
                )
            ):
                _fail()
            public.extend({
                "city_id": city_id,
                "kind": "tile",
                "tile_id": self._tile_id(item["native_tile"]),
                "worked": item["worked"],
                "free_worked": item["free_worked"],
                "can_work": item["can_work"],
                "yields": dict(item["yields"]),
            } for item in sorted(tiles, key=lambda item: item["native_tile"]))
            public.extend({
                "city_id": city_id,
                "kind": "specialist",
                "id": self._specialist_id(item["native_id"]),
                "name": item["name"],
                "count": item["count"],
                "counts_toward_population": item[
                    "counts_toward_population"
                ],
                "can_use": item["can_use"],
                "is_default": item["is_default"],
                "yields": dict(item["yields"]),
            } for item in sorted(specialists, key=lambda item: item["native_id"]))
        elif request.section == "city_worklist":
            ordered = sorted(parsed_rows, key=lambda item: item["position"])
            if [item["position"] for item in ordered] != list(range(total)):
                _fail()
            public.extend({
                "city_id": city_id,
                "position": item["position"],
                "production_id": self._production_id(
                    item["production_kind"], item["production_native_id"],
                ),
                "kind": item["production_kind"],
                "name": item["production_name"],
            } for item in ordered)
        elif request.section == "city_build_choices":
            if len({
                (item["production_kind"], item["production_native_id"])
                for item in parsed_rows
            }) != total:
                _fail()
            worklist = self._city_state_overlays.get(
                (snapshot.native_revision, request.selector, "city_worklist"),
                (),
            )
            public.extend({
                **self._public_build_choice(item, city_id),
                "preservable_count": sum(
                    prior["production_kind"] == item["production_kind"]
                    and prior["production_native_id"]
                        == item["production_native_id"]
                    for prior in worklist
                ),
            } for item in sorted(parsed_rows, key=lambda item: (
                item["production_kind"], item["production_native_id"],
            )))
        elif request.section == "city_improvements":
            if len({item["native_id"] for item in parsed_rows}) != total:
                _fail()
            public.extend({
                "city_id": city_id,
                "id": self._production_id("improvement", item["native_id"]),
                "name": item["name"],
                "sellable": item["sellable"],
                "sell_price": item["sell_price"],
            } for item in sorted(parsed_rows, key=lambda item: item["native_id"]))
        elif request.section == "city_trade_routes":
            ordered_routes = sorted(
                parsed_rows, key=lambda item: item["position"],
            )
            if (
                [item["position"] for item in ordered_routes]
                != list(range(total))
                or len({
                    item["partner_ref"] for item in ordered_routes
                    if item["partner_ref"] is not None
                }) != sum(
                    item["partner_ref"] is not None
                    for item in ordered_routes
                )
            ):
                _fail()
            sites = {
                item["ref"]: item for item in snapshot.parsed.city_sites
            }
            goods: dict[int, str] = {}
            for item in ordered_routes:
                previous = goods.setdefault(
                    item["native_goods_id"], item["goods_name"],
                )
                partner = (
                    sites.get(item["partner_ref"])
                    if item["partner_ref"] is not None else None
                )
                if (
                    previous != item["goods_name"]
                    or item["partner_ref"] == request.selector
                    or item["partner_ref"] is not None and (
                        partner is None
                        or partner["visibility"]
                           != item["partner_visibility"]
                        or partner["visibility"] not in {"own", "visible"}
                        or partner["name"] != item["partner_name"]
                    )
                    or item["partner_ref"] is None and (
                        item["partner_visibility"] != "unavailable"
                        or item["partner_name"] is not None
                    )
                ):
                    _fail()
            public.extend({
                "route_id": self._mac(
                    "trade_route", "city-route", request.selector,
                    item["position"], item["partner_ref"] or "unavailable",
                    item["native_goods_id"], item["direction"],
                ),
                "city_id": city_id,
                "position": item["position"],
                "partner": ({
                    "available": True,
                    "city_id": self._entity_id(
                        "city", item["partner_ref"],
                    ),
                    "name": item["partner_name"],
                    "visibility": item["partner_visibility"],
                } if item["partner_ref"] is not None else {
                    "available": False,
                }),
                "value": {
                    "base": item["base_value"],
                    "effective": item["effective_value"],
                },
                "direction": item["direction"],
                "goods": {
                    "id": self._mac(
                        "goods", "goods", item["native_goods_id"],
                    ),
                    "name": item["goods_name"],
                },
            } for item in ordered_routes)
        else:
            if total not in {0, 1}:
                _fail()
            public.extend({
                "city_id": city_id,
                "minimum_surplus": dict(item["minimum_surplus"]),
                "weights": dict(item["weights"]),
                "celebration_weight": item["celebration_weight"],
                "require_happy": item["require_happy"],
                "maximize_growth": item["maximize_growth"],
            } for item in parsed_rows)
        return (
            tuple(_freeze(item) for item in public),
            (),
            parsed_rows,
        )

    def _validate_relation_scope_page(
        self,
        snapshot: _ProjectedSnapshot,
        request: V2RelationScopeRequest,
        native_page: Mapping[str, Any],
        *,
        materialize: bool = False,
    ) -> dict[str, Any] | tuple[
        dict[str, Any], tuple[tuple[str, _ActionBinding], ...]
    ]:
        if not isinstance(native_page, Mapping) or set(native_page) != {
            "generation", "native_revision", "actor_ref", "counterpart_ref",
            "view_id", "offset", "count", "total_count", "next_offset",
            "complete", "overflow", "rows",
        }:
            _fail()
        revision = native_page["native_revision"]
        view_id = native_page["view_id"]
        total = native_page["total_count"]
        offset = native_page["offset"]
        count = native_page["count"]
        native_next_offset = native_page["next_offset"]
        rows = native_page["rows"]
        if (
            native_page["generation"] != self.generation
            or revision != request.native_revision
            or native_page["actor_ref"] != request.native_actor_ref
            or native_page["counterpart_ref"]
               != request.native_counterpart_ref
            or not isinstance(view_id, str)
            or _RELATION_SCOPE_VIEW.fullmatch(view_id) is None
            or int(view_id[1:].split("-", 1)[0]) != revision
            or isinstance(total, bool) or not isinstance(total, int)
            or not 0 <= total <= MAX_RELATION_SCOPED_ACTIONS
            or isinstance(offset, bool) or not isinstance(offset, int)
            or not 0 <= offset <= total or offset != request.offset
            or isinstance(count, bool) or not isinstance(count, int)
            or count != (
                total - offset if materialize
                else min(request.limit, total - offset)
            )
            or isinstance(native_next_offset, bool)
            or not isinstance(native_next_offset, int)
            or native_next_offset != offset + count
            or native_page["complete"] is not True
            or native_page["overflow"] is not False
            or not isinstance(rows, tuple) or len(rows) != count
            or (request.native_view_id is not None
                and view_id != request.native_view_id)
            or (request.total_count is not None
                and total != request.total_count)
        ):
            _fail()

        prior_slots = set(request.seen_slots)
        prior_capabilities = set(request.seen_capabilities)
        prior_bindings = dict(request.pending_scope_bindings)
        if (
            len(prior_slots) != len(request.seen_slots)
            or len(prior_capabilities) != len(request.seen_capabilities)
            or len(prior_bindings) != len(request.pending_scope_bindings)
            or len(request.seen_slots) != offset
            or len(request.seen_capabilities) != offset
            or len(request.pending_scope_bindings) != offset
        ):
            _fail()
        for action_id, binding in prior_bindings.items():
            if (
                action_id != self._mac(
                    "action", "relation-scope", revision,
                    request.native_actor_ref,
                    request.native_counterpart_ref, binding.slot,
                )
                or binding.native_revision != revision
                or binding.actor_ref != request.native_actor_ref
                or binding.counterpart_ref
                   != request.native_counterpart_ref
                or not binding.scoped or not binding.relation_scoped
                or binding.slot not in prior_slots
            ):
                _fail()

        items: list[dict[str, Any]] = []
        page_slots: list[str] = []
        page_capabilities: list[str] = []
        page_bindings: list[tuple[str, _ActionBinding]] = []
        catalog_id = self._mac(
            "catalog", "relation-scope", snapshot.native_revision,
            request.native_actor_ref, request.native_counterpart_ref, view_id,
        )
        public_scope = {
            "actor_id": request.actor_id,
            "actor_type": "player",
            "target_id": request.relation_id,
            "target_type": "diplomatic_relation",
        }
        for row in rows:
            action = self._parse_scoped_action_row(row)
            slot = action["slot"]
            if (
                not slot.startswith("a")
                or slot in prior_slots or slot in page_slots
            ):
                _fail()
            action_id = self._mac(
                "action", "relation-scope", revision,
                request.native_actor_ref,
                request.native_counterpart_ref, slot,
            )
            descriptor, binding, capability = self._project_relation_action(
                snapshot, request, action, action_id,
            )
            if capability in prior_capabilities or capability in page_capabilities:
                _fail()
            candidate_offset = offset + len(items) + 1
            probe = self._scoped_public_page(
                snapshot,
                public_scope,
                (*items, descriptor),
                total,
                (
                    "cursor_" + "x" * 32
                    if candidate_offset < total else None
                ),
                (
                    "2000-01-01T00:00:00.000Z"
                    if candidate_offset < total else None
                ),
                catalog_id,
                candidate_offset == total,
            )
            if (
                not materialize
                and self._canonical_public_bytes(probe)
                    > MAX_PUBLIC_PAGE_BYTES
            ):
                if not items:
                    raise V2ControlError("scope_too_large")
                break
            items.append(descriptor)
            page_slots.append(slot)
            page_capabilities.append(capability)
            page_bindings.append((action_id, binding))

        all_slots = request.seen_slots + tuple(page_slots)
        all_capabilities = request.seen_capabilities + tuple(page_capabilities)
        all_bindings = request.pending_scope_bindings + tuple(page_bindings)
        public_next_offset = offset + len(items)
        if public_next_offset == total:
            required = self._required_relation_capabilities(snapshot, request)
            if len(all_slots) != total or not required.issubset(all_capabilities):
                _fail()
        if materialize:
            if public_next_offset != total:
                _fail()
            return (
                self._scoped_public_page(
                    snapshot, public_scope, items, total, None, None,
                    catalog_id, True,
                ),
                all_bindings,
            )
        staged = self._scoped_public_page(
            snapshot,
            public_scope,
            items,
            total,
            (
                "cursor_" + "x" * 32
                if public_next_offset < total else None
            ),
            (
                "2000-01-01T00:00:00.000Z"
                if public_next_offset < total else None
            ),
            catalog_id,
            public_next_offset == total,
        )
        self._checked_public_page(staged)
        next_cursor = None
        if public_next_offset < total:
            next_cursor = self._new_relation_scope_cursor(
                request, view_id, total, public_next_offset, all_slots,
                all_capabilities, all_bindings,
            )
        else:
            for action_id, binding in all_bindings:
                self._publish_scoped_binding(action_id, binding)
        return self._checked_public_page(self._scoped_public_page(
            snapshot,
            public_scope,
            items,
            total,
            next_cursor,
            self._cursor_expires_at(next_cursor),
            catalog_id,
            public_next_offset == total,
        ))

    def _required_relation_capabilities(
        self,
        snapshot: _ProjectedSnapshot,
        request: V2RelationScopeRequest,
    ) -> set[str]:
        relation = next((
            item for item in snapshot.parsed.diplomacy
            if item["other_ref"] == request.native_counterpart_ref
        ), None)
        if relation is None:
            _fail()
        required: set[str] = set()
        if relation["meeting"]:
            required.add("close_meeting")
            required.add(
                "withdraw_acceptance" if relation["self_accepted"]
                else "accept"
            )
            required.update(
                "remove_clause:"
                f"{clause['giver_ref']}:{clause['clause_type']}:"
                f"{clause['native_value']}"
                for clause in self._relation_clause_rows(
                    snapshot, request.native_counterpart_ref,
                )
            )
        elif relation["can_meet"]:
            required.add("open_meeting")
        if relation["can_cancel"]:
            required.add("break_relation")
        if relation["gives_vision"]:
            required.add("withdraw_vision")
        if relation["gives_shared_tiles"]:
            required.add("withdraw_shared_tiles")
        return required

    def _project_relation_action(
        self,
        snapshot: _ProjectedSnapshot,
        request: V2RelationScopeRequest,
        action: Mapping[str, Any],
        action_id: str,
    ) -> tuple[dict[str, Any], _ActionBinding, str]:
        parsed = snapshot.parsed
        player = parsed.player
        relation = next((
            item for item in parsed.diplomacy
            if item["other_ref"] == request.native_counterpart_ref
        ), None)
        public_relation = next((
            item for item in snapshot.sections["diplomacy"]
            if item["relation_id"] == request.relation_id
        ), None)
        rule = _ACTION_RULES.get(action["native_rule"])
        if (
            player is None or relation is None or public_relation is None
            or rule is None
            or action["native_kind"] != rule.native_kind
            or action["target_kind"] != rule.target_kind
            or action["result"] != rule.result
            or action["consuming"] is not False
            or action["args"] != rule.args
            or not _non_special_action_metadata_supported(action)
            or action["actor_ref"] != request.native_actor_ref
            or action["counterpart_ref"]
               != request.native_counterpart_ref
            or action["meeting_generation"] != relation["generation"]
            or action["clauses_digest"] != relation["clauses_digest"]
            or action["self_accepted"] is not relation["self_accepted"]
            or action["other_accepted"] is not relation["other_accepted"]
            or action["relation_state"] != relation["state"]
            or action["outgoing_vision"] is not relation["gives_vision"]
            or action["outgoing_shared_tiles"]
               is not relation["gives_shared_tiles"]
            or action["native_target_tile"] != -1
            or action["destination_city_ref"] is not None
            or action["target_unit_ref"] is not None
            or action["transport_context_ref"] is not None
            or action["native_target_tech"] != -1
            or action["native_target_government"] != -1
            or action["max_rate"] != 0
            or action["target_build_kind"] != "none"
            or action["native_target_build"] != -1
            or action["spaceship_part"] != "none"
            or action["spaceship_value"] != -1
            or action["native_target_multiplier"] != -1
            or action["multiplier_value"] != -1
            or action["native_source_specialist"] != -1
            or action["native_target_specialist"] != -1
            or action["native_target_extra"] != -1
            or action["activity"] != "none"
            or action["probability_kind"] != "exact"
            or action["probability_min"] != 200
            or action["probability_max"] != 200
            or action["legality"] != "legal"
        ):
            _fail()

        clause_types = {
            "Advance", "Gold", "Map", "Seamap", "City", "Ceasefire",
            "Peace", "Alliance", "Vision", "Embassy", "SharedTiles",
        }
        is_clause = rule.operation in {"propose_clause", "remove_clause"}
        if (
            is_clause is not (action["clause_type"] in clause_types)
            or is_clause is not (action["clause_giver_ref"] is not None)
            or (not is_clause and (
                action["native_clause_value"] != -1
                or action["source_city_ref"] is not None
                or action["clause_name"] != "none"
            ))
            or (rule.operation not in {"accept", "withdraw_acceptance"}
                and action["desired_acceptance"] != -1)
        ):
            _fail()

        self_id = request.actor_id
        other_id = public_relation["player_id"]
        target = {
            "type": "diplomatic_relation",
            "id": request.relation_id,
            "counterpart": {
                "player_id": other_id,
                "name": public_relation["player_name"],
                "nation": public_relation["nation"],
            },
        }
        subject: dict[str, Any] = {
            "actor": {"type": "player", "id": self_id},
            "target": target,
            "operation": rule.operation,
            "variant": rule.variant,
            "consuming": False,
            "legality": "legal",
            "probability": self._public_probability(action),
        }
        capability = rule.operation
        argument_max = 0
        labels = {
            "open_meeting": f"Open negotiations with {public_relation['player_name']}",
            "close_meeting": f"Close negotiations with {public_relation['player_name']}",
            "accept": "Accept the current treaty",
            "withdraw_acceptance": "Withdraw acceptance of the current treaty",
            "break_relation": f"Lower relations with {public_relation['player_name']}",
            "withdraw_vision": f"Withdraw shared vision from {public_relation['player_name']}",
            "withdraw_shared_tiles": (
                f"Withdraw shared tiles from {public_relation['player_name']}"
            ),
        }
        label = labels.get(rule.operation)

        if rule.operation == "open_meeting":
            if relation["meeting"] or not relation["can_meet"]:
                _fail()
        elif rule.operation == "close_meeting":
            if not relation["meeting"]:
                _fail()
        elif rule.operation in {"accept", "withdraw_acceptance"}:
            desired = rule.operation == "accept"
            if (
                not relation["meeting"]
                or action["desired_acceptance"] != int(desired)
                or relation["self_accepted"] is desired
                or action["target_name"]
                   != ("accepted" if desired else "not accepted")
            ):
                _fail()
            subject["desired_acceptance"] = desired
        elif rule.operation == "break_relation":
            if not relation["can_cancel"]:
                _fail()
        elif rule.operation == "withdraw_vision":
            if not relation["gives_vision"]:
                _fail()
        elif rule.operation == "withdraw_shared_tiles":
            if not relation["gives_shared_tiles"]:
                _fail()
        elif is_clause:
            if not relation["meeting"]:
                _fail()
            giver_ref = action["clause_giver_ref"]
            if giver_ref not in {player["ref"], relation["other_ref"]}:
                _fail()
            giver_id = self_id if giver_ref == player["ref"] else other_id
            receiver_id = other_id if giver_ref == player["ref"] else self_id
            native_type = action["clause_type"]
            native_value = action["native_clause_value"]
            public_type = _DIPLOMACY_CLAUSE_PUBLIC_TYPES[native_type]
            if action["target_name"] != native_type:
                _fail()
            existing_native = next((
                item for item in self._relation_clause_rows(
                    snapshot, request.native_counterpart_ref,
                )
                if item["giver_ref"] == giver_ref
                and item["clause_type"] == native_type
                and item["native_value"] == native_value
            ), None)
            clause: dict[str, Any] = {
                "type": public_type,
                "giver_player_id": giver_id,
                "receiver_player_id": receiver_id,
                "value": None,
            }
            if native_type == "Advance":
                if (
                    native_value < 0 or native_value > _I32_MAX
                    or action["source_city_ref"] is not None
                    or action["clause_name"].lower()
                       in {"none", "unavailable", "gold"}
                ):
                    _fail()
                clause["value"] = {
                    "type": "technology",
                    "id": self._tech_id(native_value),
                    "name": action["clause_name"],
                }
            elif native_type == "Gold":
                if (
                    native_value < 1 or native_value > _I32_MAX
                    or action["source_city_ref"] is not None
                    or action["clause_name"] != "gold"
                    or rule.args != (
                        "gold-required"
                        if rule.operation == "propose_clause" else "none"
                    )
                    or giver_ref == player["ref"]
                       and native_value > player["gold"]
                ):
                    _fail()
                argument_max = native_value
                clause["value"] = {
                    "type": "gold", "minimum": 1,
                    "maximum": native_value,
                }
            elif native_type == "City":
                site = next((
                    item for item in parsed.city_sites
                    if item["parsed_ref"][1] == native_value
                ), None)
                unavailable_removal = (
                    rule.operation == "remove_clause"
                    and existing_native is not None
                    and existing_native["value_kind"] == "city_unavailable"
                )
                if unavailable_removal:
                    if (
                        site is not None
                        or action["source_city_ref"] is not None
                        or action["clause_name"] != "unavailable"
                        or existing_native["name"] != "unavailable"
                    ):
                        _fail()
                else:
                    if (
                        site is None or site["owner_ref"] != giver_ref
                        or action["source_city_ref"] != site["ref"]
                        or action["clause_name"] != site["name"]
                    ):
                        _fail()
                    clause["value"] = {
                        "type": "city",
                        "id": self._entity_id("city", site["ref"]),
                        "name": site["name"],
                    }
            elif (
                native_value != 0 or action["source_city_ref"] is not None
                or action["clause_name"] != "none"
                or rule.operation == "propose_clause"
                   and native_type in {"Ceasefire", "Peace", "Alliance"}
                   and giver_ref != player["ref"]
            ):
                _fail()

            if rule.operation == "remove_clause":
                if existing_native is None:
                    _fail()
                public_clauses = self._relation_state_overlays.get(
                    (snapshot.native_revision,
                     request.native_counterpart_ref),
                    snapshot.sections["diplomacy_clauses"],
                )
                existing_public = next((
                    item for item in public_clauses
                    if item["relation_id"] == request.relation_id
                    and item["giver_player_id"] == giver_id
                    and item["type"] == public_type
                    and item["position"] == existing_native["position"]
                ), None)
                if existing_public is None or rule.args != "none":
                    _fail()
                clause = _thaw(existing_public)
                capability = (
                    f"remove_clause:{giver_ref}:{native_type}:{native_value}"
                )
                label = f"Remove {public_type.replace('_', ' ')} clause"
            else:
                capability = (
                    f"propose_clause:{giver_ref}:{native_type}:"
                    f"{'bounded' if native_type == 'Gold' else native_value}"
                )
                label = f"Propose {public_type.replace('_', ' ')} clause"
            subject["clause"] = clause
        else:
            _fail()

        expected_target_names = {
            "open_meeting": "meeting", "close_meeting": "meeting",
            "break_relation": "lower relation",
            "withdraw_vision": "outgoing vision",
            "withdraw_shared_tiles": "outgoing shared tiles",
        }
        if (
            rule.operation in expected_target_names
            and action["target_name"] != expected_target_names[rule.operation]
        ):
            _fail()
        if label is None:
            _fail()
        descriptor = {
            "action_id": action_id,
            "kind": rule.public_kind,
            "label": label,
            "subject": subject,
            "arguments_schema": self._arguments_schema(rule, argument_max),
            "state_revision": _thaw(snapshot.state_revision),
        }
        try:
            descriptor = validate_legal_action_descriptor(descriptor)
        except FullControlSchemaError:
            _fail()
        binding = _ActionBinding(
            slot=action["slot"],
            native_revision=snapshot.native_revision,
            argument_contract=rule.args,
            public_kind=rule.public_kind,
            operation=rule.operation,
            turn=parsed.meta["turn"],
            phase=parsed.meta["phase"],
            max_rate=0,
            argument_max=argument_max,
            actor_ref=request.native_actor_ref,
            counterpart_ref=request.native_counterpart_ref,
            scoped=True,
            relation_scoped=True,
        )
        return descriptor, binding, capability

    def _project_scoped_action(
        self,
        snapshot: _ProjectedSnapshot,
        request: V2ActorScopeRequest,
        action: Mapping[str, Any],
        action_id: str,
    ) -> tuple[dict[str, Any], _ActionBinding]:
        """Validate and project one capability absent from the global catalog.

        Scope completeness and native legality are attested by the pinned,
        non-overflowing native scope.  This boundary independently proves the
        exact actor and target grammar before minting any public capability.
        """
        rule = _ACTION_RULES.get(action["native_rule"])
        if rule is None or (
            action["native_kind"] != rule.native_kind
            or action["target_kind"] != rule.target_kind
            or action["result"] != rule.result
            or action["consuming"] is not rule.consuming
            or action["args"] != rule.args
            or not _non_special_action_metadata_supported(action)
            or action["actor_ref"] != request.native_actor_ref
            or action["native_target_tech"] != -1
            or action["max_rate"] != 0
            or action["gold_cost"] != -1
            or action["route_waypoint_limit"] != (
                MAX_UNIT_ROUTE_WAYPOINTS
                if rule.operation == "set_route" else 0
            )
            or action["infrastructure_cost"] != 0
            or action["infrastructure_turns"] != 0
            or rule.operation == "place_infrastructure"
               and not action["infrastructure_choices"]
            or rule.operation != "place_infrastructure"
               and bool(action["infrastructure_choices"])
        ):
            _fail()
        self._validate_probability(action)

        parsed = snapshot.parsed
        state_revision = _thaw(snapshot.state_revision)
        public_actor: dict[str, Any]
        public_target: dict[str, Any]
        public_source_city: dict[str, Any] | None = None
        public_upgrade_target: dict[str, Any] | None = None
        infrastructure_binding_choices: tuple[tuple[str, int], ...] = ()
        argument_min = 0
        argument_max = action["route_waypoint_limit"]
        argument_step = 1
        argument_excluded: int | None = None
        label: str
        if request.actor_kind == "player":
            player = parsed.player
            if (
                player is None
                or rule.operation not in {
                    "revolution", "change", "set_multiplier",
                    "place_component", "launch", "place_infrastructure",
                }
                or action["source_city_ref"] is not None
                or action["destination_city_ref"] is not None
                or action["target_unit_ref"] is not None
                or action["transport_context_ref"] is not None
                or action["target_build_kind"] != "none"
                or action["native_target_build"] != -1
                or action["native_source_specialist"] != -1
                or action["native_target_specialist"] != -1
                or action["native_target_extra"] != -1
                or action["activity"] != "none"
                or action["probability_kind"] != "exact"
                or action["probability_min"] != 200
                or action["probability_max"] != 200
            ):
                _fail()
            public_actor = {
                "type": "player",
                "id": self._entity_id("player", player["ref"]),
            }
            if rule.operation == "place_infrastructure":
                target_tile = self._known_tile_for_native(
                    snapshot, action["native_target_tile"],
                )
                extras = {
                    item["native_id"]: item
                    for item in parsed.infrastructure_extras
                }
                choices: list[dict[str, Any]] = []
                private_choices: list[tuple[str, int]] = []
                if (
                    action["native_target_government"] != -1
                    or action["target_name"] != "infrastructure"
                    or action["target_build_kind"] != "none"
                    or action["native_target_build"] != -1
                    or action["native_source_specialist"] != -1
                    or action["native_target_specialist"] != -1
                    or action["route_waypoint_limit"] != 0
                    or action["spaceship_part"] != "none"
                    or action["spaceship_value"] != -1
                    or action["native_target_multiplier"] != -1
                    or action["multiplier_value"] != -1
                    or not player["infrastructure_enabled"]
                    or target_tile is None
                    or target_tile["known"] != 2
                    or target_tile["placing_extra"] != -1
                    or target_tile["placing_time"] < 0
                ):
                    _fail()
                for native_id in action["infrastructure_choices"]:
                    extra = extras.get(native_id)
                    if extra is None or not 0 < extra["cost"] <= player[
                        "infrastructure_points"
                    ]:
                        _fail()
                    public_id = self._extra_id(native_id)
                    turns = (
                        extra["build_time"] if extra["build_time"] > 0
                        else target_tile["placing_time"]
                             * extra["build_time_factor"]
                    )
                    choices.append({
                        "extra_id": public_id,
                        "name": extra["name"],
                        "cost": extra["cost"],
                        "turns": turns,
                    })
                    private_choices.append((public_id, native_id))
                infrastructure_binding_choices = tuple(private_choices)
                public_target = {
                    "type": "tile",
                    "id": self._tile_id(target_tile["native_index"]),
                    "x": target_tile["x"],
                    "y": target_tile["y"],
                    "choices": choices,
                }
                label = (
                    "Place infrastructure at "
                    f"({target_tile['x']}, {target_tile['y']})"
                )
            elif rule.operation in {"revolution", "change"}:
                governance = parsed.governance
                government = next((
                    item for item in parsed.governments
                    if item["native_id"]
                       == action["native_target_government"]
                ), None)
                if (
                    governance is None or government is None
                    or action["native_target_tile"] != -1
                    or action["spaceship_part"] != "none"
                    or action["spaceship_value"] != -1
                    or action["native_target_multiplier"] != -1
                    or action["multiplier_value"] != -1
                    or action["target_name"] != government["name"]
                ):
                    _fail()
                if rule.operation == "revolution":
                    if (
                        not governance["can_revolution"]
                        or government["native_id"]
                        != governance["during_native_id"]
                    ):
                        _fail()
                    label = "Start an untargeted revolution"
                else:
                    if not government["can_change"]:
                        _fail()
                    label = f"Change government to {government['name']}"
                public_target = {
                    "type": "government",
                    "id": self._government_id(government["native_id"]),
                    "name": government["name"],
                }
            elif rule.operation == "set_multiplier":
                multiplier = next((
                    item for item in parsed.multipliers
                    if item["native_id"]
                       == action["native_target_multiplier"]
                ), None)
                if (
                    multiplier is None
                    or action["native_target_tile"] != -1
                    or action["native_target_government"] != -1
                    or action["spaceship_part"] != "none"
                    or action["spaceship_value"] != -1
                    or not multiplier["can_change"]
                    or multiplier["choice_count"] <= 1
                    or action["multiplier_value"] != -1
                    or action["target_name"] != multiplier["name"]
                ):
                    _fail()
                argument_min = multiplier["start"]
                argument_max = multiplier["stop"]
                argument_step = multiplier["step"]
                argument_excluded = multiplier["target"]
                public_target = {
                    "type": "multiplier",
                    "id": self._multiplier_id(multiplier["native_id"]),
                    "name": multiplier["name"],
                    "value": multiplier["value"],
                    "target": multiplier["target"],
                    "minimum": multiplier["start"],
                    "maximum": multiplier["stop"],
                    "step": multiplier["step"],
                    "choice_count": multiplier["choice_count"],
                }
                label = f"Set {multiplier['name']} target"
            else:
                ship = parsed.spaceship
                if (
                    ship is None
                    or action["native_target_tile"] != -1
                    or action["native_target_government"] != -1
                    or action["native_target_multiplier"] != -1
                    or action["multiplier_value"] != -1
                    or ship["state"] != "started"
                ):
                    _fail()
                if rule.operation == "launch":
                    if (
                        action["spaceship_part"] != "none"
                        or action["spaceship_value"] != -1
                        or action["target_name"] != "launch"
                        or not ship["can_launch"]
                    ):
                        _fail()
                    public_target = {
                        "type": "spaceship",
                        "id": self._mac("spaceship", "entity", "spaceship"),
                    }
                    label = "Launch spaceship"
                else:
                    part = action["spaceship_part"]
                    value = action["spaceship_value"]
                    if part == "structural":
                        structural = next((
                            item for item in parsed.spaceship_structurals
                            if item["native_slot"] == value
                        ), None)
                        if structural is None or not structural["can_place"]:
                            _fail()
                        public_target = {
                            "type": "spaceship_structural",
                            "id": self._spaceship_slot_id(value),
                            "part": part,
                            "x": structural["x"],
                            "y": structural["y"],
                        }
                    else:
                        expected = ship.get(part, -1) + 1
                        components_ok = part in {"fuel", "propulsion"} and (
                            ship["fuel"] + ship["propulsion"]
                            < ship["components"] and expected <= 8
                        )
                        modules_ok = part in {
                            "habitation", "life_support", "solar_panels",
                        } and (
                            ship["habitation"] + ship["life_support"]
                            + ship["solar_panels"] < ship["modules"]
                            and expected <= 4
                        )
                        if value != expected or not (components_ok or modules_ok):
                            _fail()
                        public_target = {
                            "type": "spaceship_part",
                            "id": self._spaceship_part_id(part, value),
                            "part": part,
                            "next_count": value,
                        }
                    if action["target_name"] != part:
                        _fail()
                    label = f"Place spaceship {part.replace('_', ' ')}"
        elif request.actor_kind == "city":
            worker_task_operations = {
                "request_worker_task", "change_worker_task",
                "remove_worker_task",
            }
            city = next((
                item for item in parsed.cities
                if item["ref"] == request.native_actor_ref
            ), None)
            city_tiles = {
                item["native_tile"]: item for item in self._city_scope_rows(
                    snapshot, request.native_actor_ref, "city_citizens",
                    kind="city_tile",
                )
            }
            specialists = {
                item["native_id"]: item for item in self._city_scope_rows(
                    snapshot, request.native_actor_ref, "city_citizens",
                    kind="city_specialist",
                )
            }
            build_choices = {
                (item["production_kind"], item["production_native_id"]): item
                for item in self._city_scope_rows(
                    snapshot, request.native_actor_ref, "city_build_choices",
                )
            }
            improvements = {
                item["native_id"]: item
                for item in self._city_scope_rows(
                    snapshot, request.native_actor_ref, "city_improvements",
                )
            }
            worker_tasks = {
                item["native_tile"]: item
                for item in parsed.city_worker_tasks
                if item["city_ref"] == request.native_actor_ref
            }
            rally = next((
                item for item in parsed.city_rallies
                if item["city_ref"] == request.native_actor_ref
            ), None)
            governor_rows = self._city_scope_rows(
                snapshot, request.native_actor_ref, "city_governor",
            )
            if city is not None and len(governor_rows) != int(
                city["governor_enabled"]
            ):
                _fail()
            governor = governor_rows[0] if governor_rows else None
            if (
                city is None
                or rally is None
                or rule.operation not in {
                    "set_production", "buy_production", "work_tile",
                    "unwork_tile", "set_specialist", "set_worklist",
                    "set_options", "rename", "sell_improvement",
                    "set_rally", "clear_rally", "set_governor",
                    "clear_governor", *worker_task_operations,
                }
                or action["source_city_ref"] is not None
                or action["destination_city_ref"] is not None
                or action["target_unit_ref"] is not None
                or action["transport_context_ref"] is not None
                or (
                    rule.operation not in {
                        "request_worker_task", "change_worker_task",
                    }
                    and action["native_target_extra"] != -1
                )
                or action["native_target_government"] != -1
                or action["spaceship_part"] != "none"
                or action["spaceship_value"] != -1
                or action["native_target_multiplier"] != -1
                or action["multiplier_value"] != -1
                or (
                    rule.operation not in worker_task_operations
                    and action["activity"] != "none"
                )
                or action["target_name"] == "none"
                or action["probability_kind"] != "exact"
                or action["probability_min"] != 200
                or action["probability_max"] != 200
            ):
                _fail()
            public_actor = {
                "type": "city",
                "id": self._entity_id("city", city["ref"]),
            }
            if rule.operation in worker_task_operations:
                tile = city_tiles.get(action["native_target_tile"])
                known_tile = self._known_tile_for_native(
                    snapshot, action["native_target_tile"],
                )
                current = worker_tasks.get(action["native_target_tile"])
                remove = rule.operation == "remove_worker_task"
                if (
                    tile is None or known_tile is None
                    or known_tile["known"] != 2
                    or parsed.player is None
                    or known_tile["owner_ref"] not in {
                        None, parsed.player["ref"],
                    }
                    or action["target_build_kind"] != "none"
                    or action["native_target_build"] != -1
                    or action["native_source_specialist"] != -1
                    or action["native_target_specialist"] != -1
                    or (rule.operation == "request_worker_task")
                       is not (current is None)
                ):
                    _fail()
                desired: dict[str, Any] | None
                if remove:
                    if (
                        current is None or action["activity"] != "none"
                        or action["native_target_extra"] != -1
                        or action["target_name"] != "standing task"
                    ):
                        _fail()
                    desired = None
                else:
                    targeted = (
                        action["activity"]
                        in _CITY_WORKER_TASK_TARGETED_ACTIVITIES
                    )
                    if (
                        action["activity"]
                           not in _CITY_WORKER_TASK_ACTIVITIES
                        or targeted is not (
                            action["native_target_extra"] >= 0
                        )
                        or (
                            not targeted
                            and action["target_name"] != action["activity"]
                        )
                        or (
                            current is not None
                            and current["activity"] == action["activity"]
                            and current["native_target_extra"]
                                == action["native_target_extra"]
                            and current["want"] == 100
                        )
                    ):
                        _fail()
                    desired = {
                        "activity_id": self._activity_id(
                            action["activity"],
                            action["native_target_extra"],
                        ),
                        "name": action["activity"],
                        "target_extra": ({
                            "id": self._extra_id(
                                action["native_target_extra"],
                            ),
                            "name": action["target_name"],
                        } if targeted else None),
                        "priority": 100,
                    }
                current_public = ({
                    "activity_id": self._activity_id(
                        current["activity"],
                        current["native_target_extra"],
                    ),
                    "name": current["activity"],
                    "target_extra": ({
                        "id": self._extra_id(
                            current["native_target_extra"],
                        ),
                        "name": current["target_extra_name"],
                    } if current["native_target_extra"] >= 0 else None),
                    "priority": current["want"],
                } if current is not None else None)
                public_target = {
                    "type": "city_worker_task",
                    "id": self._city_worker_task_id(
                        city["ref"], action["native_target_tile"],
                    ),
                    "tile": {
                        "id": self._tile_id(action["native_target_tile"]),
                        "x": known_tile["x"],
                        "y": known_tile["y"],
                    },
                    "current": current_public,
                    "desired": desired,
                }
                label = {
                    "request_worker_task": "Request",
                    "change_worker_task": "Change",
                    "remove_worker_task": "Remove",
                }[rule.operation] + f" {city['name']} standing worker task"
            elif rule.operation == "set_rally":
                target_tile = self._known_tile_for_native(
                    snapshot, action["native_target_tile"],
                )
                if (
                    target_tile is None
                    or target_tile["known"] not in {1, 2}
                    or action["native_target_tile"] == city["native_tile"]
                    or action["target_build_kind"] != "none"
                    or action["native_target_build"] != -1
                    or action["native_source_specialist"] != -1
                    or action["native_target_specialist"] != -1
                    or action["target_name"] != "destination"
                ):
                    _fail()
                public_target = {
                    "type": "tile",
                    "id": self._tile_id(action["native_target_tile"]),
                    "x": target_tile["x"],
                    "y": target_tile["y"],
                }
                label = (
                    f"Set {city['name']} rally point to "
                    f"({target_tile['x']}, {target_tile['y']})"
                )
            elif rule.operation == "clear_rally":
                if (
                    not rally["active"]
                    or action["native_target_tile"] != -1
                    or action["target_build_kind"] != "none"
                    or action["native_target_build"] != -1
                    or action["native_source_specialist"] != -1
                    or action["native_target_specialist"] != -1
                    or action["target_name"] != "rally"
                ):
                    _fail()
                public_target = dict(public_actor)
                label = f"Clear {city['name']} rally point"
            elif rule.operation in {"set_governor", "clear_governor"}:
                if (
                    action["native_target_tile"] != -1
                    or action["target_build_kind"] != "none"
                    or action["native_target_build"] != -1
                    or action["native_source_specialist"] != -1
                    or action["native_target_specialist"] != -1
                    or action["target_name"] != "governor"
                    or rule.operation == "clear_governor"
                       and governor is None
                ):
                    _fail()
                public_target = dict(public_actor)
                label = (
                    f"Set {city['name']} governor goal"
                    if rule.operation == "set_governor"
                    else f"Clear {city['name']} governor"
                )
            elif rule.operation in {"set_production", "buy_production"}:
                if (
                    action["native_target_tile"] != -1
                    or action["target_build_kind"]
                       not in _BUILD_KINDS - {"none"}
                    or action["native_target_build"] < 0
                    or action["native_source_specialist"] != -1
                    or action["native_target_specialist"] != -1
                ):
                    _fail()
                is_current = (
                    action["target_build_kind"] == city["production_kind"]
                    and action["native_target_build"]
                    == city["production_native_id"]
                )
                if rule.operation == "set_production":
                    if is_current or not city["can_change"]:
                        _fail()
                    label = (
                        f"Set {city['name']} production to "
                        f"{action['target_name']}"
                    )
                else:
                    if (
                        not is_current
                        or action["target_name"] != city["production_name"]
                        or not city["can_buy"]
                        or city["buy_cost"] <= 0
                        or parsed.player is None
                        or city["buy_cost"] > parsed.player["gold"]
                    ):
                        _fail()
                    label = (
                        f"Buy {action['target_name']} production in "
                        f"{city['name']}"
                    )
                public_target = {
                    "type": "production",
                    "id": self._production_id(
                        action["target_build_kind"],
                        action["native_target_build"],
                    ),
                    "kind": action["target_build_kind"],
                    "name": action["target_name"],
                }
            elif rule.operation in {"work_tile", "unwork_tile"}:
                tile = city_tiles.get(action["native_target_tile"])
                known_tile = self._known_tile_for_native(
                    snapshot, action["native_target_tile"],
                )
                if (
                    governor is not None
                    or tile is None or known_tile is None or tile["free_worked"]
                    or action["target_build_kind"] != "none"
                    or action["native_target_build"] != -1
                ):
                    _fail()
                if rule.operation == "work_tile":
                    positive = sorted(
                        item["native_id"] for item in specialists.values()
                        if item["counts_toward_population"]
                        and item["count"] > 0
                    )
                    if (
                        tile["worked"] or not tile["can_work"] or not positive
                        or action["native_source_specialist"] != positive[0]
                        or action["native_target_specialist"] != -1
                        or action["target_name"] != "worked tile"
                    ):
                        _fail()
                    label = f"Assign a {city['name']} citizen to a tile"
                else:
                    default = next((
                        item for item in specialists.values()
                        if item["counts_toward_population"]
                        and item["is_default"]
                    ), None)
                    if (
                        not tile["worked"] or default is None
                        or action["native_source_specialist"] != -1
                        or action["native_target_specialist"]
                           != default["native_id"]
                        or action["target_name"] != "default specialist"
                    ):
                        _fail()
                    label = f"Unassign a {city['name']} tile worker"
                public_target = {
                    "type": "tile",
                    "id": self._tile_id(action["native_target_tile"]),
                    "x": known_tile["x"],
                    "y": known_tile["y"],
                }
            elif rule.operation == "set_specialist":
                source = specialists.get(action["native_source_specialist"])
                target = specialists.get(action["native_target_specialist"])
                if (
                    governor is not None
                    or action["native_target_tile"] != -1
                    or action["target_build_kind"] != "none"
                    or action["native_target_build"] != -1
                    or source is None or source["count"] <= 0
                    or not source["counts_toward_population"]
                    or target is None
                    or not target["counts_toward_population"]
                    or not target["can_use"]
                    or source["native_id"] == target["native_id"]
                    or action["target_name"] != target["name"]
                ):
                    _fail()
                label = (
                    f"Change a {city['name']} {source['name']} specialist "
                    f"to {target['name']}"
                )
                public_target = {
                    "type": "specialist",
                    "id": self._specialist_id(target["native_id"]),
                    "name": target["name"],
                    "from": {
                        "id": self._specialist_id(source["native_id"]),
                        "name": source["name"],
                    },
                }
            elif rule.operation in {"set_worklist", "set_options", "rename"}:
                expected_name = {
                    "set_worklist": "worklist",
                    "set_options": "options",
                    "rename": "name",
                }[rule.operation]
                if (
                    action["native_target_tile"] != -1
                    or action["target_build_kind"] != "none"
                    or action["native_target_build"] != -1
                    or action["native_source_specialist"] != -1
                    or action["native_target_specialist"] != -1
                    or action["target_name"] != expected_name
                ):
                    _fail()
                public_target = dict(public_actor)
                label = {
                    "set_worklist": f"Set {city['name']} worklist",
                    "set_options": f"Set {city['name']} options",
                    "rename": f"Rename {city['name']}",
                }[rule.operation]
            else:
                improvement = improvements.get(action["native_target_build"])
                if (
                    rule.operation != "sell_improvement"
                    or action["native_target_tile"] != -1
                    or action["target_build_kind"] != "improvement"
                    or action["native_source_specialist"] != -1
                    or action["native_target_specialist"] != -1
                    or improvement is None
                    or not improvement["sellable"]
                    or city["did_sell"]
                    or action["target_name"] != improvement["name"]
                ):
                    _fail()
                public_target = {
                    "type": "improvement",
                    "id": self._production_id(
                        "improvement", improvement["native_id"],
                    ),
                    "name": improvement["name"],
                    "sell_price": improvement["sell_price"],
                }
                label = f"Sell {improvement['name']} in {city['name']}"
        elif request.actor_kind == "unit":
            unit = next((
                item for item in parsed.units
                if item["scope"] == "own"
                and item["ref"] == request.native_actor_ref
            ), None)
            city_target_operations = {
                "upgrade", "rehome", "join_city", "establish_trade",
                "marketplace", "help_wonder", "disband_recover",
            }
            city_reference_operations = city_target_operations | {
                "airlift", "goto_and_perform",
            }
            if (
                unit is None
                or rule.operation not in {
                    "start_activity", "cancel_activity", "sentry", "fortify",
                    "convert", "disband", "make_homeless", "board",
                    "deboard", "embark", "disembark", "load", "unload",
                    "airlift", "paradrop", "teleport",
                    *city_target_operations,
                    "auto_work", "auto_explore", "cancel_automation",
                    "cancel_orders", "clear_action_decision", "goto",
                    "goto_and_perform",
                    "connect_route", "set_route", "attack_route",
                    "move", "attack", "suicide_attack", "found_city",
                }
                or action["native_target_government"] != -1
                or action["native_source_specialist"] != -1
                or action["native_target_specialist"] != -1
                or rule.operation not in city_reference_operations and (
                    action["source_city_ref"] is not None
                    or action["destination_city_ref"] is not None
                )
            ):
                _fail()
            public_actor = {
                "type": "unit",
                "id": self._entity_id("unit", unit["ref"]),
            }
            transport_operations = {
                "board", "deboard", "embark", "disembark", "load", "unload",
            }
            mobility_operations = {"airlift", "paradrop", "teleport"}
            tile_operations = {
                "move", "attack", "suicide_attack", "found_city",
            }
            if rule.operation == "clear_action_decision":
                target_id = (
                    unit["action_decision_tile"] >= 0
                    and self._tile_id(unit["action_decision_tile"])
                )
                decision = (
                    snapshot.action_decision_bindings.get(target_id)
                    if isinstance(target_id, str) else None
                )
                if (
                    decision is None
                    or decision.actor_id != public_actor["id"]
                    or decision.native_target_tile
                       != action["native_target_tile"]
                    or action["native_target_tile"]
                       != unit["action_decision_tile"]
                    or unit["action_decision_want"] not in {
                        "passive", "active",
                    }
                    or action["target_name"]
                       != unit["action_decision_want"]
                    or action["source_city_ref"] is not None
                    or action["destination_city_ref"] is not None
                    or action["target_unit_ref"] is not None
                    or action["transport_context_ref"] is not None
                    or action["target_build_kind"] != "none"
                    or action["native_target_build"] != -1
                    or action["native_target_extra"] != -1
                    or action["activity"] != "none"
                    or action["probability_kind"] != "exact"
                    or action["probability_min"] != 200
                    or action["probability_max"] != 200
                ):
                    _fail()
                public_target = {
                    "type": "action_decision",
                    "tile_id": target_id,
                    "want": unit["action_decision_want"],
                }
                label = "Clear pending unit action decision"
            elif rule.operation in {"set_route", "attack_route"}:
                if (
                    action["native_target_tile"] != -1
                    or action["source_city_ref"] is not None
                    or action["destination_city_ref"] is not None
                    or action["target_unit_ref"] is not None
                    or action["transport_context_ref"] is not None
                    or action["target_build_kind"] != "none"
                    or action["native_target_build"] != -1
                    or action["native_target_extra"] != -1
                    or action["activity"] != "none"
                    or action["target_name"] != (
                        "route" if rule.operation == "set_route"
                        else "destination"
                    )
                    or action["route_waypoint_limit"] != (
                        MAX_UNIT_ROUTE_WAYPOINTS
                        if rule.operation == "set_route" else 0
                    )
                    or action["legality"] != "legal"
                    or action["probability_kind"] != "exact"
                    or action["probability_min"] != 200
                    or action["probability_max"] != 200
                    or unit["controller"] != "none"
                    or unit["activity"] != "idle"
                    or unit["activity_target"] != -1
                    or unit["has_orders"]
                    or unit["transport_state"] != "untransported"
                    or unit["occupied"] != 0
                ):
                    _fail()
                public_target = (
                    {"type": "route"}
                    if rule.operation == "set_route"
                    else {"type": "route", "mode": "attack"}
                )
                label = (
                    "Set an ordered unit route"
                    if rule.operation == "set_route"
                    else "Attack along a route"
                )
            elif rule.operation in tile_operations:
                origin_tile = {
                    "native_index": unit["native_tile"],
                    "x": unit["x"], "y": unit["y"], "known": 2,
                }
                target_tile = self._known_tile_for_native(
                    snapshot, action["native_target_tile"],
                    request.native_actor_ref,
                )
                if (
                    origin_tile is None or origin_tile["known"] != 2
                    or target_tile is None
                    or action["source_city_ref"] is not None
                    or action["destination_city_ref"] is not None
                    or action["target_unit_ref"] is not None
                    or action["transport_context_ref"] is not None
                    or action["target_build_kind"] != "none"
                    or action["native_target_build"] != -1
                    or action["native_target_extra"] != -1
                    or action["activity"] != "none"
                    or action["target_name"] != "none"
                ):
                    _fail()
                distance = self._map_distance(
                    parsed.meta, origin_tile["x"], origin_tile["y"],
                    target_tile["x"], target_tile["y"],
                )
                if rule.operation == "found_city":
                    if (
                        target_tile["known"] != 2 or distance != 0
                        or action["native_target_tile"]
                           != unit["native_tile"]
                    ):
                        _fail()
                else:
                    if unit["moves"] <= 0 or distance != 1:
                        _fail()
                    if target_tile["known"] == 0:
                        if (
                            rule.operation != "move"
                            or action["probability_kind"] != "unknown"
                            or action["legality"] != "possibly_legal"
                            or action["probability_min"] != 0
                            or action["probability_max"] != 200
                        ):
                            _fail()
                    elif rule.operation in {"attack", "suicide_attack"} \
                            and target_tile["known"] != 2:
                        _fail()
                public_target = {
                    "type": "tile",
                    "id": self._tile_id(target_tile["native_index"]),
                    "x": target_tile["x"],
                    "y": target_tile["y"],
                    "visibility": (
                        "unknown" if target_tile["known"] == 0
                        else "remembered" if target_tile["known"] == 1
                        else "visible"
                    ),
                }
                label = self._action_label(rule, unit, target_tile)
            elif rule.operation in {
                "goto", "goto_and_perform", "connect_route",
            }:
                target_tile = self._known_tile_for_native(
                    snapshot, action["native_target_tile"],
                    request.native_actor_ref,
                )
                common_invalid = (
                    target_tile is None
                    or target_tile["known"] not in {0, 1, 2}
                    or (
                        target_tile["known"] == 0
                        and (
                            rule.operation != "goto"
                            or not isinstance(request, V2TargetActionRequest)
                        )
                    )
                    or target_tile["native_index"] == unit["native_tile"]
                    or action["transport_context_ref"] is not None
                    or action["target_build_kind"] != "none"
                    or action["native_target_build"] != -1
                    or unit["controller"] != "none"
                    or unit["activity"] != "idle"
                    or unit["activity_target"] != -1
                    or unit["has_orders"]
                    or unit["transport_state"] != "untransported"
                    or unit["occupied"] != 0
                )
                if common_invalid:
                    _fail()
                public_target = {
                    "type": "tile",
                    "id": self._tile_id(target_tile["native_index"]),
                    "x": target_tile["x"],
                    "y": target_tile["y"],
                }
                if rule.operation == "goto":
                    if (
                        action["source_city_ref"] is not None
                        or action["destination_city_ref"] is not None
                        or action["target_unit_ref"] is not None
                        or action["native_target_extra"] != -1
                        or action["activity"] != "none"
                        or action["target_name"] != "destination"
                        or action["legality"] != "legal"
                        or action["probability_kind"] != "exact"
                        or action["probability_min"] != 200
                        or action["probability_max"] != 200
                    ):
                        _fail()
                    label = (
                        f"Go to ({target_tile['x']}, {target_tile['y']})"
                    )
                elif rule.operation == "goto_and_perform":
                    target_unit = next((
                        item for item in parsed.units
                        if item["ref"] == action["target_unit_ref"]
                    ), None)
                    target_city = next((
                        item for item in parsed.city_sites
                        if item["ref"] == action["destination_city_ref"]
                    ), None)
                    if (
                        action["source_city_ref"] is not None
                        or action["native_target_extra"] != -1
                        or action["activity"] != "none"
                        or action["target_name"] == "none"
                        or action["legality"] != "possibly_legal"
                        or action["probability_kind"] != "unknown"
                        or action["probability_min"] != 0
                        or action["probability_max"] != 200
                        or (action["target_unit_ref"] is not None)
                           is not (target_unit is not None)
                        or (action["destination_city_ref"] is not None)
                           is not (target_city is not None)
                        or target_unit is not None and target_city is not None
                    ):
                        _fail()
                    public_target["action"] = {
                        "type": "native_action",
                        "name": action["target_name"],
                    }
                    if target_unit is not None:
                        public_target["entity"] = {
                            "type": "unit",
                            "id": self._entity_id(
                                "unit", target_unit["ref"],
                            ),
                        }
                    elif target_city is not None:
                        public_target["entity"] = {
                            "type": "city",
                            "id": self._entity_id(
                                "city", target_city["ref"],
                            ),
                            "name": target_city["name"],
                        }
                    label = (
                        f"Go to ({target_tile['x']}, {target_tile['y']}) "
                        f"and perform {action['target_name']}"
                    )
                else:
                    if (
                        action["source_city_ref"] is not None
                        or action["destination_city_ref"] is not None
                        or action["target_unit_ref"] is not None
                        or action["native_target_extra"] < 0
                        or action["activity"] not in {"road", "irrigate"}
                        or action["target_name"] == "none"
                        or action["legality"] != "legal"
                        or action["probability_kind"] != "exact"
                        or action["probability_min"] != 200
                        or action["probability_max"] != 200
                    ):
                        _fail()
                    public_target["construction"] = {
                        "type": "extra",
                        "id": self._extra_id(
                            action["native_target_extra"],
                        ),
                        "name": action["target_name"],
                        "activity": action["activity"],
                    }
                    label = (
                        f"Connect {action['target_name']} to "
                        f"({target_tile['x']}, {target_tile['y']})"
                    )
            elif rule.operation in mobility_operations:
                target_tile = self._known_tile_for_native(
                    snapshot, action["native_target_tile"],
                    request.native_actor_ref,
                )
                own_cities = {item["ref"]: item for item in parsed.cities}
                city_sites = {
                    item["ref"]: item for item in parsed.city_sites
                }
                source_city = own_cities.get(action["source_city_ref"])
                destination_city = city_sites.get(
                    action["destination_city_ref"],
                )
                if (
                    action["target_unit_ref"] is not None
                    or action["transport_context_ref"] is not None
                    or action["target_build_kind"] != "none"
                    or action["native_target_build"] != -1
                    or action["native_target_extra"] != -1
                    or action["activity"] != "none"
                ):
                    _fail()
                if rule.operation == "airlift":
                    if (
                        action["native_rule"] != "Airlift Unit"
                        or action["native_target_tile"] != -1
                        or source_city is None
                        or destination_city is None
                        or source_city["ref"] == destination_city["ref"]
                        or source_city["native_tile"] != unit["native_tile"]
                        or action["target_name"] != destination_city["name"]
                        or action["probability_kind"] == "not_implemented"
                    ):
                        _fail()
                    public_target = {
                        "type": "city",
                        "id": self._entity_id(
                            "city", destination_city["ref"],
                        ),
                        "name": destination_city["name"],
                        "owner_player_id": self._entity_id(
                            "player", destination_city["owner_ref"],
                        ),
                        "tile_id": self._tile_id(
                            destination_city["native_tile"],
                        ),
                        "x": destination_city["x"],
                        "y": destination_city["y"],
                        "size": destination_city["size"],
                        "visibility": destination_city["visibility"],
                    }
                    label = f"Airlift unit to {destination_city['name']}"
                else:
                    if (
                        action["source_city_ref"] is not None
                        or action["destination_city_ref"] is not None
                        or target_tile is None
                        or (
                            target_tile["known"] in {0, 1}
                            and not isinstance(request, V2TargetActionRequest)
                        )
                        or target_tile["native_index"] == unit["native_tile"]
                        or action["target_name"] != "destination"
                        or (
                            rule.operation == "teleport"
                            and action["probability_kind"]
                                == "not_implemented"
                            and not isinstance(request, V2TargetActionRequest)
                        )
                        or (
                            target_tile["known"] == 0
                            and (
                                action["legality"] != "possibly_legal"
                                or action["probability_kind"] != "unknown"
                                or action["probability_min"] != 0
                                or action["probability_max"] != 200
                            )
                        )
                    ):
                        _fail()
                    public_target = {
                        "type": "tile",
                        "id": self._tile_id(target_tile["native_index"]),
                        "x": target_tile["x"],
                        "y": target_tile["y"],
                        "visibility": (
                            "unknown" if target_tile["known"] == 0
                            else "remembered" if target_tile["known"] == 1
                            else "visible"
                        ),
                    }
                    label = (
                        "Paradrop unit" if rule.operation == "paradrop"
                        else "Teleport unit"
                    ) + f" to ({target_tile['x']}, {target_tile['y']})"
            elif rule.operation in city_target_operations:
                city_sites = {item["ref"]: item for item in parsed.city_sites}
                own_cities = {item["ref"]: item for item in parsed.cities}
                destination = city_sites.get(action["destination_city_ref"])
                source = own_cities.get(action["source_city_ref"])
                source_required = rule.operation in {
                    "establish_trade", "marketplace",
                }
                upgrade = rule.operation == "upgrade"
                if (
                    destination is None
                    or action["native_target_tile"] != -1
                    or action["target_unit_ref"] is not None
                    or action["transport_context_ref"] is not None
                    or action["native_target_extra"] != -1
                    or action["activity"] != "none"
                    or source_required is not (source is not None)
                    or source_required and (
                        unit["home_ref"] != source["ref"]
                        or source["ref"] == destination["ref"]
                    )
                    or upgrade is not (
                        action["target_build_kind"] == "unit"
                        and action["native_target_build"] >= 0
                    )
                    or not upgrade and (
                        action["target_build_kind"] != "none"
                        or action["native_target_build"] != -1
                    )
                    or upgrade and action["target_name"] == "none"
                    or not upgrade
                       and action["target_name"] != destination["name"]
                ):
                    _fail()
                public_target = {
                    "type": "city",
                    "id": self._entity_id("city", destination["ref"]),
                    "name": destination["name"],
                    "owner_player_id": self._entity_id(
                        "player", destination["owner_ref"],
                    ),
                    "tile_id": self._tile_id(destination["native_tile"]),
                    "x": destination["x"],
                    "y": destination["y"],
                    "size": destination["size"],
                    "visibility": destination["visibility"],
                }
                if source is not None:
                    public_source_city = {
                        "type": "city",
                        "id": self._entity_id("city", source["ref"]),
                        "name": source["name"],
                        "tile_id": self._tile_id(source["native_tile"]),
                    }
                if upgrade:
                    public_upgrade_target = {
                        "type": "unit_type",
                        "id": self._unit_type_id(
                            action["native_target_build"],
                        ),
                        "name": action["target_name"],
                    }
                    label = (
                        f"Upgrade unit to {action['target_name']} in "
                        f"{destination['name']}"
                    )
                else:
                    label = {
                        "rehome": f"Set home city to {destination['name']}",
                        "join_city": f"Join {destination['name']}",
                        "establish_trade": (
                            f"Establish a trade route to {destination['name']}"
                        ),
                        "marketplace": (
                            f"Enter the marketplace in {destination['name']}"
                        ),
                        "help_wonder": (
                            f"Help production in {destination['name']}"
                        ),
                        "disband_recover": (
                            f"Disband unit into production in "
                            f"{destination['name']}"
                        ),
                    }[rule.operation]
            elif rule.operation in transport_operations:
                own_units = {
                    item["ref"]: item for item in parsed.units
                    if item["scope"] == "own"
                }
                all_units = {item["ref"]: item for item in parsed.units}
                allied_player_refs = {
                    relation["other_ref"] for relation in parsed.diplomacy
                    if relation["state"] in {"Alliance", "Team"}
                }

                def transport_unit(ref: str | None) -> Mapping[str, Any] | None:
                    candidate = all_units.get(ref)
                    if candidate is None or (
                        candidate["scope"] == "visible"
                        and candidate["owner_ref"] not in allied_player_refs
                    ):
                        return None
                    return candidate

                target_unit = transport_unit(action["target_unit_ref"])
                context_unit = transport_unit(
                    action["transport_context_ref"],
                )
                target_tile = self._known_tile_for_native(
                    snapshot, action["native_target_tile"],
                    request.native_actor_ref,
                )

                def current_parent_matches(
                    cargo: Mapping[str, Any],
                    context: Mapping[str, Any] | None,
                ) -> bool:
                    if cargo["scope"] != "own":
                        return context is None or (
                            context["native_tile"] == cargo["native_tile"]
                        )
                    if cargo["transport_state"] == "unresolved":
                        return False
                    if cargo["transport_state"] == "untransported":
                        return context is None
                    return (
                        context is not None
                        and cargo["transporter_ref"] == context["ref"]
                        and context["native_tile"] == cargo["native_tile"]
                    )

                def would_create_known_cycle(
                    cargo: Mapping[str, Any],
                    transporter: Mapping[str, Any],
                ) -> bool:
                    cursor = transporter
                    visited: set[str] = set()
                    while cursor["scope"] == "own":
                        if cursor["ref"] == cargo["ref"]:
                            return True
                        if (
                            cursor["ref"] in visited
                            or cursor["transport_state"] != "transported"
                        ):
                            return False
                        visited.add(cursor["ref"])
                        parent = transport_unit(cursor["transporter_ref"])
                        if parent is None:
                            return True
                        cursor = parent
                    return cursor["ref"] == cargo["ref"]

                if (
                    action["target_build_kind"] != "none"
                    or action["native_target_build"] != -1
                    or action["native_target_extra"] != -1
                    or action["activity"] != "none"
                    or action["legality"] != "legal"
                    or action["probability_kind"] != "exact"
                    or action["probability_min"] != 200
                    or action["probability_max"] != 200
                ):
                    _fail()
                context_public = None
                if rule.operation == "disembark":
                    if (
                        target_unit is not None
                        or target_tile is None
                        or target_tile["known"] not in {0, 1, 2}
                        or target_tile["native_index"] == unit["native_tile"]
                        or context_unit is None
                        or unit["transport_state"] != "transported"
                        or unit["transporter_ref"] != context_unit["ref"]
                        or context_unit["native_tile"] != unit["native_tile"]
                        or self._map_distance(
                            parsed.meta,
                            unit["x"], unit["y"],
                            target_tile["x"], target_tile["y"],
                        ) != 1
                        or action["target_name"] != "destination"
                    ):
                        _fail()
                    public_target = {
                        "type": "tile",
                        "id": self._tile_id(target_tile["native_index"]),
                        "x": target_tile["x"],
                        "y": target_tile["y"],
                        "visibility": (
                            "unknown" if target_tile["known"] == 0
                            else "remembered" if target_tile["known"] == 1
                            else "visible"
                        ),
                    }
                    context_public = {
                        "type": "unit",
                        "id": self._entity_id("unit", context_unit["ref"]),
                    }
                    label = "Disembark cargo onto adjacent tile"
                else:
                    if (
                        action["native_target_tile"] != -1
                        or target_unit is None
                    ):
                        _fail()
                    public_target = {
                        "type": "unit",
                        "id": self._entity_id("unit", target_unit["ref"]),
                    }
                    if rule.operation in {"board", "embark"}:
                        if (
                            target_unit is unit
                            or target_unit["scope"] == "own" and (
                                target_unit["transport_capacity"] <= 0
                                or target_unit["transport_state"]
                                    == "unresolved"
                            )
                            or not current_parent_matches(unit, context_unit)
                            or context_unit is target_unit
                            or would_create_known_cycle(unit, target_unit)
                            or (
                                rule.operation == "board"
                                and unit["native_tile"]
                                    != target_unit["native_tile"]
                            )
                            or (
                                rule.operation == "embark"
                                and (
                                    unit["native_tile"]
                                        == target_unit["native_tile"]
                                    or self._map_distance(
                                        parsed.meta,
                                        unit["x"], unit["y"],
                                        target_unit["x"], target_unit["y"],
                                    ) != 1
                                )
                            )
                            or action["target_name"] != "transporter"
                        ):
                            _fail()
                        if context_unit is not None:
                            context_public = {
                                "type": "unit",
                                "id": self._entity_id(
                                    "unit", context_unit["ref"],
                                ),
                            }
                        label = (
                            "Board cargo onto transporter"
                            if rule.operation == "board"
                            else "Embark cargo onto adjacent transporter"
                        )
                    elif rule.operation == "load":
                        if (
                            target_unit is unit
                            or unit["transport_capacity"] <= 0
                            or unit["transport_state"] == "unresolved"
                            or not current_parent_matches(
                                target_unit, context_unit,
                            )
                            or context_unit is unit
                            or would_create_known_cycle(target_unit, unit)
                            or unit["native_tile"]
                                != target_unit["native_tile"]
                            or action["target_name"] != "cargo"
                        ):
                            _fail()
                        if context_unit is not None:
                            context_public = {
                                "type": "unit",
                                "id": self._entity_id(
                                    "unit", context_unit["ref"],
                                ),
                            }
                        label = "Load cargo onto transporter"
                    elif rule.operation == "deboard":
                        if (
                            context_unit is not target_unit
                            or unit["transport_state"] != "transported"
                            or unit["transporter_ref"] != target_unit["ref"]
                            or unit["native_tile"]
                                != target_unit["native_tile"]
                            or action["target_name"] != "transporter"
                        ):
                            _fail()
                        context_public = dict(public_target)
                        label = "Deboard cargo from transporter"
                    else:
                        if (
                            rule.operation != "unload"
                            or context_unit is not unit
                            or unit["transport_capacity"] <= 0
                            or unit["transport_state"] == "unresolved"
                            or target_unit["scope"] == "own" and (
                                target_unit["transport_state"]
                                    != "transported"
                                or target_unit["transporter_ref"]
                                    != unit["ref"]
                            )
                            or unit["native_tile"]
                                != target_unit["native_tile"]
                            or action["target_name"] != "cargo"
                        ):
                            _fail()
                        context_public = dict(public_actor)
                        label = "Unload cargo from transporter"
                public_transport_context = context_public
            elif rule.operation in {
                "auto_work", "auto_explore", "cancel_automation",
                "cancel_orders",
            }:
                expected_controller = {
                    "auto_work": "auto_work",
                    "auto_explore": "auto_explore",
                    "cancel_automation": "none",
                    "cancel_orders": "orders",
                }[rule.operation]
                if (
                    action["native_target_tile"] != -1
                    or action["target_unit_ref"] is not None
                    or action["transport_context_ref"] is not None
                    or action["target_build_kind"] != "none"
                    or action["native_target_build"] != -1
                    or action["native_target_extra"] != -1
                    or action["activity"] != "none"
                    or action["target_name"] != expected_controller
                    or action["probability_kind"] != "exact"
                    or action["probability_min"] != 200
                    or action["probability_max"] != 200
                    or rule.operation == "cancel_automation"
                       and unit["controller"] not in {
                           "auto_work", "auto_explore",
                       }
                    or rule.operation == "cancel_orders"
                       and (
                           unit["controller"] != "none"
                           or unit["activity"] != "idle"
                           or unit["activity_target"] != -1
                           or not unit["has_orders"]
                       )
                    or rule.operation not in {
                        "cancel_automation", "cancel_orders",
                    }
                       and (
                           unit["controller"] != "none"
                           or unit["activity"] != "idle"
                           or unit["activity_target"] != -1
                           or unit["has_orders"]
                       )
                ):
                    _fail()
                public_target = dict(public_actor)
                label = {
                    "auto_work": "Start automatic worker control",
                    "auto_explore": "Start automatic exploration",
                    "cancel_automation": "Cancel unit automation",
                    "cancel_orders": "Cancel queued unit orders",
                }[rule.operation]
            elif rule.operation == "start_activity":
                if (
                    action["native_target_tile"] != -1
                    or action["target_unit_ref"] is not None
                    or action["transport_context_ref"] is not None
                ):
                    _fail()
                if (
                    action["target_build_kind"] != "none"
                    or action["native_target_build"] != -1
                ):
                    _fail()
                activity = action["activity"]
                targeted = activity in _TARGETED_ACTIVITIES
                if (
                    activity not in _WORKER_START_ACTIVITIES
                    or activity == unit["activity"]
                    or (action["native_target_extra"] >= 0) is not targeted
                    or (targeted and action["target_name"] == "none")
                    or (not targeted and action["target_name"] != activity)
                    or action["probability_kind"] == "not_implemented"
                ):
                    _fail()
                extra = ({
                    "type": "extra",
                    "id": self._extra_id(action["native_target_extra"]),
                    "name": action["target_name"],
                } if targeted else None)
                public_target = {
                    "type": "worker_activity",
                    "id": self._activity_id(
                        activity, action["native_target_extra"],
                    ),
                    "name": activity,
                    "extra": extra,
                }
                label = f"Start {activity}"
                if targeted:
                    label += f" targeting {action['target_name']}"
            elif rule.operation == "cancel_activity":
                if (
                    action["native_target_tile"] != -1
                    or action["target_unit_ref"] is not None
                    or action["transport_context_ref"] is not None
                    or
                    action["target_build_kind"] != "none"
                    or action["native_target_build"] != -1
                    or action["activity"] != "idle"
                    or action["native_target_extra"] != -1
                    or action["target_name"] != "none"
                    or unit["activity"] == "idle"
                    or action["probability_kind"] != "exact"
                    or action["probability_min"] != 200
                ):
                    _fail()
                public_target = dict(public_actor)
                label = f"Cancel {unit['activity']} activity"
            elif rule.operation == "sentry":
                if (
                    action["native_target_tile"] != -1
                    or action["target_unit_ref"] is not None
                    or action["transport_context_ref"] is not None
                    or
                    action["target_build_kind"] != "none"
                    or action["native_target_build"] != -1
                    or action["native_target_extra"] != -1
                    or action["activity"] != "sentry"
                    or action["target_name"] != "sentry"
                    or unit["activity"] == "sentry"
                    or action["probability_kind"] != "exact"
                    or action["probability_min"] != 200
                    or action["probability_max"] != 200
                ):
                    _fail()
                public_target = {
                    "type": "unit_activity",
                    "id": self._activity_id("sentry", -1),
                    "name": "sentry",
                }
                label = "Set unit to sentry"
            elif rule.operation == "fortify":
                if (
                    action["native_target_tile"] != -1
                    or action["target_unit_ref"] is not None
                    or action["transport_context_ref"] is not None
                    or
                    action["target_build_kind"] != "none"
                    or action["native_target_build"] != -1
                    or action["native_target_extra"] != -1
                    or action["activity"] != "fortifying"
                    or action["target_name"] != "fortifying"
                    or unit["activity"] in {"fortifying", "fortified"}
                    or action["probability_kind"] == "not_implemented"
                ):
                    _fail()
                public_target = {
                    "type": "unit_activity",
                    "id": self._activity_id("fortifying", -1),
                    "name": "fortifying",
                }
                label = "Fortify unit"
            elif rule.operation == "convert":
                conversion = unit["converted_type_native_id"]
                if (
                    action["native_target_tile"] != -1
                    or action["target_unit_ref"] is not None
                    or action["transport_context_ref"] is not None
                    or
                    conversion == -1
                    or unit["activity"] == "convert"
                    or action["target_build_kind"] != "unit"
                    or action["native_target_build"] != conversion
                    or action["native_target_extra"] != -1
                    or action["activity"] != "convert"
                    or action["target_name"] != unit["converted_type"]
                    or action["probability_kind"] == "not_implemented"
                ):
                    _fail()
                public_target = {
                    "type": "unit_type",
                    "id": self._unit_type_id(conversion),
                    "name": unit["converted_type"],
                }
                label = f"Convert unit to {unit['converted_type']}"
            elif rule.operation == "disband":
                if (
                    action["native_target_tile"] != -1
                    or action["target_unit_ref"] is not None
                    or action["transport_context_ref"] is not None
                    or
                    action["target_build_kind"] != "none"
                    or action["native_target_build"] != -1
                    or action["native_target_extra"] != -1
                    or action["activity"] != "none"
                    or action["target_name"] != "self"
                    or action["probability_kind"] == "not_implemented"
                ):
                    _fail()
                public_target = dict(public_actor)
                label = "Disband unit"
            else:
                if (
                    action["native_target_tile"] != -1
                    or action["target_unit_ref"] is not None
                    or action["transport_context_ref"] is not None
                    or
                    unit["home_ref"] is None
                    or action["target_build_kind"] != "none"
                    or action["native_target_build"] != -1
                    or action["native_target_extra"] != -1
                    or action["activity"] != "none"
                    or action["target_name"] != "self"
                    or action["probability_kind"] == "not_implemented"
                ):
                    _fail()
                public_target = {
                    "type": "city",
                    "id": self._entity_id("city", unit["home_ref"]),
                }
                label = "Remove unit home city"
        else:
            _fail()

        subject = {
            "actor": public_actor,
            "target": public_target,
            "operation": rule.operation,
            "variant": (
                self._mac(
                    "variant", "unit-route-action", action_id,
                    action["target_name"],
                ) if rule.operation == "goto_and_perform" else
                self._action_variant_id(
                    action["native_rule"], rule.operation,
                    (
                        action["native_target_extra"]
                        if rule.operation == "connect_route"
                        else action["native_target_tile"]
                        if rule.operation in {
                            "goto", "set_rally", "place_infrastructure",
                        }
                        else action["native_target_build"]
                    ),
                )
                if rule.variant == "opaque" else rule.variant
            ),
            "consuming": rule.consuming,
            "legality": action["legality"],
            "probability": self._public_probability(action),
        }
        if (
            request.actor_kind == "unit"
            and rule.operation in transport_operations
            and public_transport_context is not None
        ):
            subject["transport_context"] = public_transport_context
        if public_source_city is not None:
            subject["source_city"] = public_source_city
        if public_upgrade_target is not None:
            subject["upgrade_to"] = public_upgrade_target
        if action["subresults"]:
            subject["effects"] = [
                _ACTION_SUBRESULT_EFFECTS[item]
                for item in action["subresults"]
            ]
        descriptor = {
            "action_id": action_id,
            "kind": rule.public_kind,
            "label": label,
            "subject": subject,
            "arguments_schema": self._arguments_schema(
                rule, argument_max,
                tuple(
                    public_id
                    for public_id, _ in infrastructure_binding_choices
                ),
                argument_min=argument_min,
                argument_step=argument_step,
                argument_excluded=argument_excluded,
            ),
            "state_revision": state_revision,
        }
        try:
            descriptor = validate_legal_action_descriptor(descriptor)
        except FullControlSchemaError:
            _fail()
        binding = _ActionBinding(
            slot=action["slot"],
            native_revision=snapshot.native_revision,
            argument_contract=rule.args,
            public_kind=rule.public_kind,
            operation=rule.operation,
            turn=parsed.meta["turn"],
            phase=parsed.meta["phase"],
            max_rate=0,
            argument_max=argument_max,
            argument_min=argument_min,
            argument_step=argument_step,
            argument_excluded=argument_excluded,
            actor_ref=request.native_actor_ref,
            infrastructure_choices=infrastructure_binding_choices,
            scoped=True,
        )
        return descriptor, binding

    @staticmethod
    def _public_probability(action: Mapping[str, Any]) -> dict[str, Any]:
        kind = action["probability_kind"]
        if kind == "not_implemented":
            return {"kind": kind, "minimum_percent": None, "maximum_percent": None}
        return {
            "kind": kind,
            "minimum_percent": action["probability_min"] / 2,
            "maximum_percent": action["probability_max"] / 2,
        }

    def _server_setting_target(
        self, action: Mapping[str, Any], native_revision: int,
    ) -> dict[str, Any]:
        setting_type = action["server_setting_type"]
        fixed_value: bool | int | None
        if setting_type == "boolean":
            fixed_value = bool(action["server_setting_value"])
            current_value: bool | int | None = bool(
                action["server_setting_current"],
            )
        elif setting_type == "enum":
            fixed_value = action["server_setting_value"]
            current_value = action["server_setting_current"]
        elif setting_type in {"integer", "bitwise"}:
            fixed_value = None
            current_value = action["server_setting_current"]
        else:
            fixed_value = None
            current_value = None
        return {
            "type": "server_setting",
            "id": self._mac(
                "server_setting", "server_setting", native_revision,
                action["native_server_setting_id"], setting_type,
                action["server_setting_min"], action["server_setting_max"],
                action["server_setting_current"],
                action["server_setting_value"],
            ),
            "name": action["target_name"],
            "value_type": setting_type,
            "minimum": action["server_setting_min"],
            "maximum": action["server_setting_max"],
            "current_value": current_value,
            "proposed_value": fixed_value,
        }

    @staticmethod
    def _action_label(
        rule: _NativeActionRule,
        actor: Mapping[str, Any] | None,
        target: Mapping[str, Any] | None,
    ) -> str:
        if rule.operation == "end":
            return "End phase"
        if rule.operation == "set_rates":
            return "Set tax, luxury, and science rates"
        if rule.operation == "send_chat":
            return "Send chat message"
        if rule.operation == "set_target":
            if target is None:
                _fail()
            return f"Research {target['name']}"
        if rule.operation == "set_goal":
            if target is None:
                _fail()
            return f"Set research goal to {target['name']}"
        if actor is None or target is None:
            _fail()
        coordinates = f"({target['x']}, {target['y']})"
        if rule.operation == "move":
            return f"Move unit to {coordinates}"
        if rule.operation == "attack":
            return f"Attack at {coordinates} with unit"
        if rule.operation == "suicide_attack":
            return f"Suicide attack at {coordinates} with unit"
        return f"Found a city with unit at {coordinates}"

    @staticmethod
    def _arguments_schema(
        rule: _NativeActionRule, argument_max: int,
        infrastructure_choices: tuple[str, ...] = (),
        *, vote_id: str | None = None, argument_min: int = 0,
        vote_choices: tuple[str, ...] = (),
        argument_step: int = 1, argument_excluded: int | None = None,
        server_setting: Mapping[str, Any] | None = None,
    ) -> dict[str, Any]:
        if rule.operation == "configure":
            return {
                "type": "object",
                "properties": {
                    "nation_id": {"type": "string"},
                    "leader_name": {
                        "type": "string", "minLength": 1,
                        "maxLength": 47,
                        "metadata": {"max_utf8_bytes": 47},
                    },
                    "is_male": {"type": "boolean"},
                    "style_id": {"type": "string"},
                },
                "required": [
                    "nation_id", "leader_name", "is_male", "style_id",
                ],
                "additionalProperties": False,
                "metadata": {
                    "nation_ids_from": "pregame_nations",
                    "style_ids_from": "pregame_styles",
                },
            }
        if rule.operation == "set_ready":
            if argument_max not in {0, 1}:
                _fail()
            return {
                "type": "object",
                "properties": {
                    "ready": {"type": "boolean", "enum": [bool(argument_max)]},
                },
                "required": ["ready"],
                "additionalProperties": False,
            }
        if rule.operation == "cast_vote":
            if vote_id is None or not vote_choices or any(
                choice not in {"yes", "no", "abstain"}
                for choice in vote_choices
            ):
                _fail()
            return {
                "type": "object",
                "properties": {
                    "vote_id": {"type": "string", "enum": [vote_id]},
                    "vote": {
                        "type": "string", "enum": list(vote_choices),
                    },
                },
                "required": ["vote_id", "vote"],
                "additionalProperties": False,
            }
        if rule.operation == "propose_server_setting":
            if server_setting is None:
                _fail()
            setting_type = server_setting["server_setting_type"]
            minimum = server_setting["server_setting_min"]
            maximum = server_setting["server_setting_max"]
            if setting_type in {"boolean", "enum"}:
                return {
                    "type": "object",
                    "properties": {},
                    "additionalProperties": False,
                    "metadata": {
                        "value_bound_by_descriptor": True,
                    },
                }
            if setting_type in {"integer", "bitwise"}:
                return {
                    "type": "object",
                    "properties": {
                        "value": {
                            "type": "integer",
                            "minimum": minimum,
                            "maximum": maximum,
                            "multipleOf": 1,
                        },
                    },
                    "required": ["value"],
                    "additionalProperties": False,
                }
            if setting_type == "string":
                return {
                    "type": "object",
                    "properties": {
                        "value": {
                            "type": "string", "minLength": 0,
                            "maxLength": maximum,
                            "metadata": {
                                "max_utf8_bytes": maximum,
                                "double_quote_allowed": False,
                            },
                        },
                    },
                    "required": ["value"],
                    "additionalProperties": False,
                }
            _fail()
        if rule.operation == "set_team":
            return {
                "type": "object",
                "properties": {
                    "team_id": {"type": "string"},
                },
                "required": ["team_id"],
                "additionalProperties": False,
                "metadata": {"team_ids_from": "pregame_teams"},
            }
        if rule.operation == "send_chat":
            return {
                "type": "object",
                "properties": {
                    "channel": {
                        "type": "string",
                        "enum": list(_CHAT_SEND_CHANNELS),
                    },
                    "recipient_id": {
                        "type": "string",
                        "pattern": r"^player_[0-9a-f]{32}$",
                        "metadata": {
                            "opaque_ids_from": "state.chat_recipients",
                            "requires_item_field": {"can_message": True},
                        },
                    },
                    "message": {
                        "type": "string", "minLength": 1,
                        "maxLength": MAX_CHAT_MESSAGE_BYTES,
                        "metadata": {
                            "max_utf8_bytes": MAX_CHAT_MESSAGE_BYTES,
                            "commands_allowed": False,
                            "channel_prefixes_server_generated": True,
                            "colons_allowed": True,
                            "forbidden_codepoint_ranges": [
                                [lower, upper]
                                for lower, upper in (
                                    _CHAT_FORBIDDEN_CODEPOINT_RANGES
                                )
                            ],
                            "leading_trailing_ascii_space_allowed": False,
                        },
                    },
                },
                "required": ["channel", "message"],
                "allOf": [{
                    "if": {
                        "properties": {"channel": {"const": "private"}},
                        "required": ["channel"],
                    },
                    "then": {"required": ["recipient_id"]},
                    "else": {"not": {"required": ["recipient_id"]}},
                }],
                "additionalProperties": False,
                "metadata": {
                    "recipient_ids_from": "chat_recipients",
                    "private_recipients_require": {"can_message": True},
                },
            }
        if rule.operation == "place_infrastructure":
            if not infrastructure_choices:
                _fail()
            return {
                "type": "object",
                "properties": {
                    "extra_id": {
                        "type": "string",
                        "enum": list(infrastructure_choices),
                        "metadata": {
                            "opaque_ids_from": (
                                "legal_action.subject.target.choices"
                            ),
                        },
                    },
                },
                "required": ["extra_id"],
                "additionalProperties": False,
            }
        if rule.operation == "set_route":
            if argument_max != MAX_UNIT_ROUTE_WAYPOINTS:
                _fail()
            return {
                "type": "object",
                "properties": {
                    "mode": {
                        "type": "string",
                        "enum": ["goto", "patrol"],
                    },
                    "waypoints": {
                        "type": "array",
                        "minItems": 1,
                        "maxItems": argument_max,
                        "items": {"type": "string"},
                        "metadata": {
                            "opaque_ids_from": (
                                "state.known_tiles or state.tile_window"
                            ),
                            "ordered": True,
                            "duplicates_allowed": True,
                            "consecutive_duplicates_allowed": False,
                            "first_item_must_differ_from_actor_source": True,
                            "goto_final_item_must_differ_from_actor_source": (
                                True
                            ),
                        },
                    },
                },
                "required": ["mode", "waypoints"],
                "additionalProperties": False,
            }
        if rule.operation == "attack_route":
            return {
                "type": "object",
                "properties": {
                    "destination_id": {
                        "type": "string",
                        "metadata": {
                            "opaque_ids_from": (
                                "state.known_tiles or state.tile_window"
                            ),
                            "must_differ_from_actor_source": True,
                        },
                    },
                },
                "required": ["destination_id"],
                "additionalProperties": False,
            }
        if rule.operation == "set_rates":
            rate = {
                "type": "integer", "minimum": 0, "maximum": argument_max,
                "multipleOf": 1,
            }
            return {
                "type": "object",
                "properties": {
                    "tax": dict(rate),
                    "luxury": dict(rate),
                    "science": dict(rate),
                },
                "required": ["tax", "luxury", "science"],
                "additionalProperties": False,
                "metadata": {
                    "exact_sum": {
                        "fields": ["tax", "luxury", "science"],
                        "equals": 100,
                    },
                    "server_step": 1,
                },
            }
        if rule.operation == "set_multiplier":
            if (
                argument_step < 1
                or argument_max < argument_min
                or argument_excluded is None
                or not argument_min <= argument_excluded <= argument_max
                or (argument_excluded - argument_min) % argument_step != 0
            ):
                _fail()
            return {
                "type": "object",
                "properties": {
                    "value": {
                        "type": "integer",
                        "minimum": argument_min,
                        "maximum": argument_max,
                        "multipleOf": 1,
                    },
                },
                "required": ["value"],
                "additionalProperties": False,
                "metadata": {
                    "integer_grid": {
                        "field": "value",
                        "origin": argument_min,
                        "step": argument_step,
                    },
                    "not_equal": {
                        "field": "value",
                        "value": argument_excluded,
                        "reason": "current_target_is_a_no_op",
                    },
                },
            }
        if rule.args == "gold-required":
            if argument_max < 1:
                _fail()
            return {
                "type": "object",
                "properties": {
                    "gold": {
                        "type": "integer",
                        "minimum": 1,
                        "maximum": argument_max,
                        "multipleOf": 1,
                        "examples": [min(argument_max, 10)],
                    },
                },
                "required": ["gold"],
                "additionalProperties": False,
            }
        if rule.operation == "set_worklist":
            return {
                "type": "object",
                "properties": {
                    "items": {
                        "type": "array",
                        "minItems": 0,
                        "maxItems": MAX_CITY_WORKLIST,
                        "items": {"type": "string"},
                        "metadata": {
                            "opaque_ids_from": (
                                "city_build_choices"
                            ),
                            "eligibility": (
                                "can_queue or occurrence does not exceed "
                                "preservable_count"
                            ),
                            "ordered": True,
                            "duplicates_allowed": True,
                            "new_stale_occurrences_allowed": False,
                        },
                    },
                },
                "required": ["items"],
                "additionalProperties": False,
            }
        if rule.operation == "set_options":
            return {
                "type": "object",
                "properties": {
                    "allow_disband": {"type": "boolean"},
                    "new_citizens": {
                        "type": "string",
                        "enum": ["default", "science", "gold"],
                    },
                },
                "required": ["allow_disband", "new_citizens"],
                "additionalProperties": False,
            }
        if rule.operation == "set_rally":
            return {
                "type": "object",
                "properties": {"persistent": {"type": "boolean"}},
                "required": ["persistent"],
                "additionalProperties": False,
            }
        if rule.operation == "set_governor":
            outputs = (
                "food", "production", "trade", "gold", "luxury",
                "science",
            )
            return {
                "type": "object",
                "properties": {
                    "minimum_surplus": {
                        "type": "object",
                        "properties": {
                            name: {
                                "type": "integer", "minimum": -100,
                                "maximum": 100, "multipleOf": 1,
                            } for name in outputs
                        },
                        "required": list(outputs),
                        "additionalProperties": False,
                    },
                    "weights": {
                        "type": "object",
                        "properties": {
                            name: {
                                "type": "integer", "minimum": 0,
                                "maximum": 25, "multipleOf": 1,
                            } for name in outputs
                        },
                        "required": list(outputs),
                        "additionalProperties": False,
                    },
                    "celebration_weight": {
                        "type": "integer", "minimum": 0, "maximum": 50,
                        "multipleOf": 1,
                    },
                    "require_happy": {"type": "boolean"},
                    "maximize_growth": {"type": "boolean"},
                },
                "required": [
                    "minimum_surplus", "weights", "celebration_weight",
                    "require_happy", "maximize_growth",
                ],
                "additionalProperties": False,
            }
        if rule.operation not in {"found_city", "rename"}:
            return {
                "type": "object",
                "properties": {},
                "additionalProperties": False,
            }
        return {
            "type": "object",
            "properties": {
                "city_name": {
                    "type": "string",
                    "minLength": 1,
                    "maxLength": 119,
                    "metadata": {"max_utf8_bytes": 119},
                },
            },
            "required": ["city_name"],
            "additionalProperties": False,
        }

    def _resolve_arguments(
        self, snapshot: _ProjectedSnapshot, binding: _ActionBinding,
        arguments: Any,
    ) -> str:
        contract = binding.argument_contract
        max_rate = binding.max_rate
        if contract == "pregame-config-required":
            if type(arguments) is not dict or set(arguments) != {
                "nation_id", "leader_name", "is_male", "style_id",
            }:
                raise V2ControlError("invalid_request")
            nation_id = arguments["nation_id"]
            leader_name = arguments["leader_name"]
            is_male = arguments["is_male"]
            style_id = arguments["style_id"]
            if (
                not isinstance(nation_id, str)
                or not isinstance(style_id, str)
                or not isinstance(leader_name, str)
                or type(is_male) is not bool
            ):
                raise V2ControlError("invalid_request")
            if (
                not leader_name or leader_name != leader_name.strip()
                or any(unicodedata.category(char).startswith("C")
                       for char in leader_name)
            ):
                raise V2ControlError(
                    "invalid_request",
                    details={"rejection_reason": "pregame_leader_invalid"},
                )
            encoded_leader = leader_name.encode("utf-8", "strict")
            if len(encoded_leader) >= 48:
                raise V2ControlError(
                    "invalid_request",
                    details={"rejection_reason": "pregame_leader_invalid"},
                )
            if "a" <= leader_name[0] <= "z":
                leader_name = leader_name[0].upper() + leader_name[1:]
                encoded_leader = leader_name.encode("utf-8", "strict")
            nations = self._pregame_state_overlays.get(
                (snapshot.native_revision, "pregame_nations"), (),
            )
            styles = self._pregame_state_overlays.get(
                (snapshot.native_revision, "pregame_styles"), (),
            )
            nation = next((
                item for item in nations
                if self._mac("nation", "nation", item["native_id"])
                   == nation_id
            ), None)
            style = next((
                item for item in styles
                if self._mac("style", "style", item["native_id"])
                   == style_id
            ), None)
            if snapshot.parsed.pregame is None:
                raise V2ControlError("invalid_request")
            # One refusal per field: a four-argument lobby command that only
            # says "invalid" costs a new seat one probe per argument.
            if nation is None:
                raise V2ControlError(
                    "invalid_request",
                    details={"rejection_reason": "pregame_nation_unknown"},
                )
            if style is None:
                raise V2ControlError(
                    "invalid_request",
                    details={"rejection_reason": "pregame_style_unknown"},
                )
            current = snapshot.parsed.pregame
            if (
                current["nation"] == nation["name"]
                and current["style"] == style["name"]
                and current["leader"] == leader_name
                and current["sex"] == ("male" if is_male else "female")
            ):
                raise V2ControlError(
                    "invalid_request",
                    details={
                        "rejection_reason": "pregame_configuration_unchanged",
                    },
                )
            return (
                f"nation={nation['native_id']},"
                f"leader={_percent_encode(encoded_leader)},"
                f"is_male={int(is_male)},style={style['native_id']}"
            )
        if contract == "pregame-ready-required":
            if (
                type(arguments) is not dict or set(arguments) != {"ready"}
                or type(arguments["ready"]) is not bool
                or max_rate not in {0, 1}
                or arguments["ready"] is not bool(max_rate)
            ):
                raise V2ControlError("invalid_request")
            return f"ready={max_rate}"
        if contract == "pregame-team-required":
            if (
                type(arguments) is not dict or set(arguments) != {"team_id"}
                or not isinstance(arguments["team_id"], str)
                or snapshot.parsed.pregame is None
                or snapshot.parsed.pregame["ready"]
            ):
                raise V2ControlError("invalid_request")
            teams = self._pregame_state_overlays.get(
                (snapshot.native_revision, "pregame_teams"), (),
            )
            selected = next((
                team for team in teams
                if self._mac(
                    "team", "team", snapshot.native_revision,
                    team["native_id"],
                ) == arguments["team_id"]
            ), None)
            if selected is None or selected["selected"]:
                raise V2ControlError("invalid_request")
            return f"team={selected['native_id']}"
        if contract == "chat-required":
            if type(arguments) is not dict:
                raise V2ControlError("invalid_request")
            channel = arguments.get("channel")
            message = arguments.get("message")
            if (
                not isinstance(channel, str)
                or channel not in _CHAT_SEND_CHANNELS
                or not isinstance(message, str)
                or set(arguments) != (
                    {"channel", "recipient_id", "message"}
                    if channel == "private" else {"channel", "message"}
                )
            ):
                raise V2ControlError("invalid_request")
            try:
                encoded = message.encode("utf-8", "strict")
            except UnicodeEncodeError as exc:
                raise V2ControlError("invalid_request") from exc
            if not _chat_message_safe(message, encoded):
                raise V2ControlError("invalid_request")
            native_recipient = "none"
            if channel == "private":
                recipient_id = arguments["recipient_id"]
                if not isinstance(recipient_id, str):
                    raise V2ControlError("invalid_request")
                recipient = next((
                    item for item in self._chat_recipient_overlays.get(
                        snapshot.native_revision, (),
                    )
                    if self._entity_id("player", item["ref"])
                       == recipient_id
                ), None)
                if recipient is None:
                    raise V2ControlError("invalid_request")
                if not recipient["can_message"]:
                    raise V2ControlError("invalid_request")
                native_recipient = recipient["ref"]
            return (
                f"channel={channel};recipient={native_recipient};"
                f"message={_percent_encode(encoded)}"
            )
        if contract == "vote-required":
            if (
                type(arguments) is not dict
                or set(arguments) != {"vote_id", "vote"}
                or not isinstance(arguments["vote_id"], str)
                or arguments["vote_id"] != binding.vote_id
                or not isinstance(arguments["vote"], str)
                or arguments["vote"] not in binding.vote_choices
            ):
                raise V2ControlError("invalid_request")
            return f"vote={arguments['vote']}"
        if contract == "multiplier-value-required":
            if type(arguments) is not dict or set(arguments) != {"value"}:
                raise V2ControlError("invalid_request")
            value = arguments["value"]
            if (
                isinstance(value, bool) or not isinstance(value, int)
                or binding.argument_step < 1
                or value < binding.argument_min
                or value > binding.argument_max
                or (value - binding.argument_min) % binding.argument_step != 0
                or value == binding.argument_excluded
            ):
                raise V2ControlError("invalid_request")
            return f"value={value}"
        if contract in {
            "server-setting-integer-required",
            "server-setting-bitwise-required",
        }:
            if type(arguments) is not dict or set(arguments) != {"value"}:
                raise V2ControlError("invalid_request")
            value = arguments["value"]
            if isinstance(value, bool) or not isinstance(value, int):
                raise V2ControlError("invalid_request")
            if value == binding.server_setting_current:
                raise V2ControlError(
                    "invalid_request",
                    details={"rejection_reason": "server_setting_unchanged"},
                )
            if (
                value < binding.server_setting_min
                or value > binding.server_setting_max
                or (
                    contract == "server-setting-bitwise-required"
                    and value < 0
                )
            ):
                raise V2ControlError(
                    "invalid_request",
                    details={
                        "rejection_reason": "server_setting_out_of_range",
                    },
                )
            return f"value={value}"
        if contract == "server-setting-string-required":
            if (
                type(arguments) is not dict
                or set(arguments) != {"value"}
                or not isinstance(arguments["value"], str)
            ):
                raise V2ControlError("invalid_request")
            value = arguments["value"]
            try:
                encoded = value.encode("utf-8", "strict")
            except UnicodeEncodeError as exc:
                raise V2ControlError("invalid_request") from exc
            if len(encoded) > binding.server_setting_max:
                raise V2ControlError(
                    "invalid_request",
                    details={
                        "rejection_reason": "server_setting_out_of_range",
                    },
                )
            if (
                any(byte < 0x20 or byte == 0x7F for byte in encoded)
                or any(0x80 <= ord(character) <= 0x9F for character in value)
                or '"' in value
            ):
                raise V2ControlError("invalid_request")
            return f"value={_percent_encode(encoded)}"
        if contract == "none":
            if type(arguments) is not dict or arguments:
                raise V2ControlError("invalid_request")
            return "-"
        if contract == "rates-required":
            if type(arguments) is not dict or set(arguments) != {
                "tax", "luxury", "science",
            }:
                raise V2ControlError("invalid_request")
            values = tuple(arguments[key] for key in (
                "tax", "luxury", "science",
            ))
            if any(
                isinstance(value, bool) or not isinstance(value, int)
                or value < 0 or value > max_rate
                for value in values
            ) or sum(values) != 100:
                raise V2ControlError("invalid_request")
            tax, luxury, science = values
            return f"tax={tax},luxury={luxury},science={science}"
        if contract == "gold-required":
            if type(arguments) is not dict or set(arguments) != {"gold"}:
                raise V2ControlError("invalid_request")
            gold = arguments["gold"]
            if (
                isinstance(gold, bool) or not isinstance(gold, int)
                or gold < 1 or gold > binding.argument_max
            ):
                raise V2ControlError("invalid_request")
            return f"gold={gold}"
        if contract == "infrastructure-extra-required":
            if type(arguments) is not dict or set(arguments) != {"extra_id"}:
                raise V2ControlError("invalid_request")
            extra_id = arguments["extra_id"]
            native_by_public = dict(binding.infrastructure_choices)
            if not isinstance(extra_id, str) or extra_id not in native_by_public:
                raise V2ControlError("invalid_request")
            return f"extra={native_by_public[extra_id]}"
        if contract == "route-required":
            if (
                type(arguments) is not dict
                or set(arguments) != {"mode", "waypoints"}
                or arguments["mode"] not in {"goto", "patrol"}
                or type(arguments["waypoints"]) is not list
                or not 1 <= len(arguments["waypoints"]) <= binding.argument_max
                or binding.actor_ref is None
                or binding.argument_max != MAX_UNIT_ROUTE_WAYPOINTS
            ):
                raise V2ControlError("invalid_request")
            unit = next((
                item for item in snapshot.parsed.units
                if item["scope"] == "own" and item["ref"] == binding.actor_ref
            ), None)
            if unit is None:
                raise V2ControlError("internal_error")
            native_waypoints: list[int] = []
            for tile_id in arguments["waypoints"]:
                native_tile = (
                    snapshot.tile_bindings.get(tile_id)
                    if isinstance(tile_id, str) else None
                )
                if native_tile is None and isinstance(tile_id, str):
                    scoped = self._scoped_tile_bindings.get(tile_id)
                    if (
                        scoped is not None
                        and scoped[0] == snapshot.native_revision
                    ):
                        native_tile = scoped[1]
                if (
                    native_tile is None
                    or not native_waypoints
                       and native_tile == unit["native_tile"]
                    or native_waypoints and native_tile == native_waypoints[-1]
                ):
                    raise V2ControlError("invalid_request")
                native_waypoints.append(native_tile)
            if (
                arguments["mode"] == "goto"
                and native_waypoints[-1] == unit["native_tile"]
            ):
                raise V2ControlError("invalid_request")
            return (
                f"mode={arguments['mode']};waypoints="
                + ",".join(str(value) for value in native_waypoints)
            )
        if contract == "attack-route-required":
            if (
                type(arguments) is not dict
                or set(arguments) != {"destination_id"}
                or binding.actor_ref is None
                or binding.argument_max != 0
            ):
                raise V2ControlError("invalid_request")
            destination_id = arguments["destination_id"]
            native_tile = (
                snapshot.tile_bindings.get(destination_id)
                if isinstance(destination_id, str) else None
            )
            if native_tile is None and isinstance(destination_id, str):
                scoped = self._scoped_tile_bindings.get(destination_id)
                if (
                    scoped is not None
                    and scoped[0] == snapshot.native_revision
                ):
                    native_tile = scoped[1]
            unit = next((
                item for item in snapshot.parsed.units
                if item["scope"] == "own"
                and item["ref"] == binding.actor_ref
            ), None)
            if (
                unit is None
                or native_tile is None
                or native_tile == unit["native_tile"]
            ):
                raise V2ControlError("invalid_request")
            return f"destination={native_tile}"
        if contract == "worklist-required":
            if (
                type(arguments) is not dict
                or set(arguments) != {"items"}
                or type(arguments["items"]) is not list
                or len(arguments["items"]) > MAX_CITY_WORKLIST
                or binding.actor_ref is None
            ):
                raise V2ControlError("invalid_request")
            choice_rows = self._city_scope_rows(
                snapshot, binding.actor_ref, "city_build_choices",
            )
            worklist_rows = self._city_scope_rows(
                snapshot, binding.actor_ref, "city_worklist",
            )
            choices = {
                self._production_id(
                    item["production_kind"], item["production_native_id"],
                ): item
                for item in choice_rows
            }
            current = next((
                city for city in snapshot.parsed.cities
                if city["ref"] == binding.actor_ref
            ), None)
            current_items = [
                (
                    item["production_kind"],
                    item["production_native_id"],
                )
                for item in sorted(
                    (
                        row for row in worklist_rows
                    ),
                    key=lambda row: row["position"],
                )
            ]
            current_counts: dict[tuple[str, int], int] = {}
            for key in current_items:
                current_counts[key] = current_counts.get(key, 0) + 1
            preserved_counts: dict[tuple[str, int], int] = {}
            encoded: list[str] = []
            desired_items: list[tuple[str, int]] = []
            for item_id in arguments["items"]:
                choice = choices.get(item_id) if isinstance(item_id, str) else None
                if choice is None:
                    raise V2ControlError("invalid_request")
                key = (
                    choice["production_kind"],
                    choice["production_native_id"],
                )
                if not choice["can_queue"]:
                    preserved = preserved_counts.get(key, 0)
                    if preserved >= current_counts.get(key, 0):
                        raise V2ControlError("invalid_request")
                    preserved_counts[key] = preserved + 1
                desired_items.append(key)
                encoded.append(f"{key[0]}:{key[1]}")
            if current is None or desired_items == current_items:
                raise V2ControlError("invalid_request")
            native = "worklist=" + ",".join(encoded)
            if len(native.encode("ascii")) > 4096:
                raise V2ControlError("invalid_request")
            return native
        if contract == "city-options-required":
            if type(arguments) is not dict or set(arguments) != {
                "allow_disband", "new_citizens",
            }:
                raise V2ControlError("invalid_request")
            allow_disband = arguments["allow_disband"]
            new_citizens = arguments["new_citizens"]
            if (
                type(allow_disband) is not bool
                or not isinstance(new_citizens, str)
                or new_citizens not in _NEW_CITIZENS
            ):
                raise V2ControlError("invalid_request")
            if binding.actor_ref is None:
                raise V2ControlError("internal_error")
            city = next((
                item for item in snapshot.parsed.cities
                if item["ref"] == binding.actor_ref
            ), None)
            if city is None:
                raise V2ControlError("internal_error")
            if (
                not city["options_conflict"]
                and city["allow_disband"] is allow_disband
                and city["new_citizens"] == new_citizens
            ):
                raise V2ControlError("invalid_request")
            return (
                f"allow_disband={int(allow_disband)},"
                f"new_citizens={new_citizens}"
            )
        if contract == "persistent-required":
            if (
                type(arguments) is not dict
                or set(arguments) != {"persistent"}
                or type(arguments["persistent"]) is not bool
            ):
                raise V2ControlError("invalid_request")
            return f"persistent={int(arguments['persistent'])}"
        if contract == "governor-goal-required":
            outputs = (
                "food", "production", "trade", "gold", "luxury",
                "science",
            )
            if type(arguments) is not dict or set(arguments) != {
                "minimum_surplus", "weights", "celebration_weight",
                "require_happy", "maximize_growth",
            }:
                raise V2ControlError("invalid_request")
            minimum_surplus = arguments["minimum_surplus"]
            weights = arguments["weights"]
            celebration_weight = arguments["celebration_weight"]
            require_happy = arguments["require_happy"]
            maximize_growth = arguments["maximize_growth"]
            if (
                type(minimum_surplus) is not dict
                or set(minimum_surplus) != set(outputs)
                or type(weights) is not dict
                or set(weights) != set(outputs)
                or any(
                    isinstance(minimum_surplus[name], bool)
                    or not isinstance(minimum_surplus[name], int)
                    or minimum_surplus[name] < -100
                    or minimum_surplus[name] > 100
                    for name in outputs
                )
                or any(
                    isinstance(weights[name], bool)
                    or not isinstance(weights[name], int)
                    or weights[name] < 0 or weights[name] > 25
                    for name in outputs
                )
                or isinstance(celebration_weight, bool)
                or not isinstance(celebration_weight, int)
                or celebration_weight < 0 or celebration_weight > 50
                or type(require_happy) is not bool
                or type(maximize_growth) is not bool
                or binding.actor_ref is None
            ):
                raise V2ControlError("invalid_request")
            desired = {
                "minimum_surplus": minimum_surplus,
                "weights": weights,
                "celebration_weight": celebration_weight,
                "require_happy": require_happy,
                "maximize_growth": maximize_growth,
            }
            governors = self._city_scope_rows(
                snapshot, binding.actor_ref, "city_governor",
            )
            if len(governors) > 1:
                raise V2ControlError("internal_error")
            current = governors[0] if governors else None
            if current is not None and all(
                current[key] == value for key, value in desired.items()
            ):
                raise V2ControlError("invalid_request")
            parts = [
                *(f"min_{name}={minimum_surplus[name]}" for name in outputs),
                *(f"weight_{name}={weights[name]}" for name in outputs),
                f"celebration_weight={celebration_weight}",
                f"require_happy={int(require_happy)}",
                f"maximize_growth={int(maximize_growth)}",
            ]
            return ",".join(parts)
        if contract != "city_name-required":
            raise V2ControlError("internal_error")
        if type(arguments) is not dict or set(arguments) != {"city_name"}:
            raise V2ControlError("invalid_request")
        city_name = arguments["city_name"]
        if not isinstance(city_name, str):
            raise V2ControlError("invalid_request")
        try:
            encoded = city_name.encode("utf-8", "strict")
        except UnicodeEncodeError as exc:
            raise V2ControlError("invalid_request") from exc
        if (
            not 1 <= len(encoded) <= 119
            or any(
                unicodedata.category(character).startswith("C")
                for character in city_name
            )
        ):
            raise V2ControlError("invalid_request")
        if binding.operation == "rename" and binding.actor_ref is not None:
            city = next((
                item for item in snapshot.parsed.cities
                if item["ref"] == binding.actor_ref
            ), None)
            if city is None:
                raise V2ControlError("internal_error")
            if city_name == city["name"]:
                raise V2ControlError("invalid_request")
        # Do not normalize or percent-encode the name here.  The sidecar
        # percent-encodes this complete native field, and the C protocol
        # decodes it before checking the city_name= grammar.
        return f"city_name={city_name}"

    def _install(self, snapshot: _ProjectedSnapshot) -> None:
        self._snapshots[snapshot.native_revision] = snapshot
        self._projected_bytes += snapshot.canonical_bytes
        while (
            len(self._snapshots) > MAX_CACHED_REVISIONS
            or self._projected_bytes > MAX_PROJECTED_BYTES
        ):
            revision = min(self._snapshots)
            evicted = self._snapshots.pop(revision)
            self._projected_bytes -= evicted.canonical_bytes
            self._evict_cursors(revision)
        retained_revisions = set(self._snapshots)
        for action_id, binding in tuple(self._scoped_action_bindings.items()):
            if binding.native_revision not in retained_revisions:
                self._scoped_action_bindings.pop(action_id, None)
        for tile_id, binding in tuple(self._scoped_tile_bindings.items()):
            if binding[0] not in retained_revisions:
                self._scoped_tile_bindings.pop(tile_id, None)
        for key in tuple(self._scoped_tile_metadata):
            if key[0] not in retained_revisions:
                self._scoped_tile_metadata.pop(key, None)
        for key in tuple(self._actor_tile_overlays):
            if key[0] not in retained_revisions:
                self._actor_tile_overlays.pop(key, None)
        for key in tuple(self._city_state_overlays):
            if key[0] not in retained_revisions:
                self._city_state_overlays.pop(key, None)
        for key in tuple(self._pregame_state_overlays):
            if key[0] not in retained_revisions:
                self._pregame_state_overlays.pop(key, None)
        for revision in tuple(self._chat_recipient_overlays):
            if revision not in retained_revisions:
                self._chat_recipient_overlays.pop(revision, None)
        for key in tuple(self._relation_state_overlays):
            if key[0] not in retained_revisions:
                self._drop_relation_overlay(key)
        self._release_superseded_scopes()
        if snapshot.native_revision not in self._snapshots:
            _fail()

    def _page(
        self,
        snapshot: _ProjectedSnapshot,
        endpoint: str,
        section: str,
        limit: int,
        offset: int,
        *,
        actor_id: str | None = None,
        relation_id: str | None = None,
        center_id: str | None = None,
        radius: int | None = None,
    ) -> dict[str, Any]:
        values = (
            snapshot.legal_actions
            if endpoint == "legal_actions"
            else self._state_section_values(
                snapshot, section, actor_id, relation_id, center_id, radius,
            )
        )
        if offset != 0:
            raise V2ControlError("invalid_request")
        restart_query: dict[str, Any]
        if endpoint == "state":
            restart_query = {"section": section, "limit": limit}
            if actor_id is not None:
                restart_query["actor_id"] = actor_id
            if relation_id is not None:
                restart_query["relation_id"] = relation_id
            if center_id is not None:
                restart_query["center_id"] = center_id
            if radius is not None:
                restart_query["radius"] = radius
        else:
            restart_query = {"limit": limit}
        return self._start_page_chain(
            snapshot,
            endpoint,
            section,
            limit,
            values,
            {"endpoint": endpoint, "query": restart_query},
            # The unscoped catalog is the only way a seat can reach its
            # `phase.end` capability, so it draws on the reserve.
            reserved=endpoint == "legal_actions",
        )

    def _ordinary_public_page(
        self,
        state_revision: Mapping[str, Any],
        section: str,
        values: Sequence[Mapping[str, Any]],
        start: int,
        end: int,
        next_cursor: str | None,
        expires_at: str | None,
    ) -> dict[str, Any]:
        return self._public_page_from_thawed(
            _thaw(state_revision),
            section,
            [_thaw(item) for item in values[start:end]],
            len(values),
            next_cursor,
            expires_at,
        )

    def _public_page_from_thawed(
        self,
        state_revision: Mapping[str, Any],
        section: str,
        items: list[Any],
        total_items: int,
        next_cursor: str | None,
        expires_at: str | None,
        *,
        scope: Mapping[str, Any] | None = None,
        catalog_id: str | None = None,
        catalog_complete: bool = False,
    ) -> dict[str, Any]:
        """Assemble one public page whose parts are already mutable copies.

        Every public page has exactly this shape, and the size planner builds
        hundreds of throwaway copies of it to find where pages must split.
        Taking the thawed parts as arguments is what lets the planner deep-copy
        the catalog once instead of once per candidate page size.
        """
        page: dict[str, Any] = {"section": section}
        if scope is not None:
            page["scope"] = dict(scope)
        page.update({
            "items": items,
            "total_items": total_items,
            "next_cursor": next_cursor,
            "cursor_expires_at": expires_at,
        })
        if scope is not None:
            page["catalog_id"] = catalog_id
            page["catalog_complete"] = catalog_complete
        return {
            "schema_version": FULL_CONTROL_SCHEMA_VERSION,
            "control_protocol": FULL_CONTROL_V2,
            "game_id": self.game_id,
            "agent_id": self.agent_id,
            "state_revision": state_revision,
            "page": page,
        }

    def _chain_public_page(
        self,
        state_revision: Mapping[str, Any],
        section: str,
        values: Sequence[Mapping[str, Any]],
        start: int,
        end: int,
        next_cursor: str | None,
        expires_at: str | None,
        *,
        scope: Mapping[str, Any] | None = None,
        catalog_id: str | None = None,
    ) -> dict[str, Any]:
        if scope is None:
            return self._ordinary_public_page(
                state_revision, section, values, start, end,
                next_cursor, expires_at,
            )
        if catalog_id is None:
            raise V2ControlError("internal_error")
        return self._public_page_from_thawed(
            _thaw(state_revision),
            section,
            [_thaw(item) for item in values[start:end]],
            len(values),
            next_cursor,
            expires_at,
            scope=scope,
            catalog_id=catalog_id,
            catalog_complete=end == len(values),
        )

    def _page_chain_token(
        self, nonce: bytes, page_index: int, endpoint: str,
    ) -> str:
        body = nonce + page_index.to_bytes(4, "big")
        tag = hmac.new(
            self._secret,
            b"page-chain\x00" + endpoint.encode("ascii") + b"\x00" + body,
            hashlib.sha256,
        ).digest()[:8]
        encoded = base64.urlsafe_b64encode(body + tag).decode("ascii")
        return "cursor_" + encoded.rstrip("=")

    def _decode_page_chain_token(
        self, cursor: str, endpoint: str,
    ) -> tuple[bytes, int] | None:
        if not isinstance(cursor, str) or not cursor.startswith("cursor_"):
            return None
        encoded = cursor[7:]
        if len(encoded) != 32:
            return None
        try:
            payload = base64.b64decode(
                encoded, altchars=b"-_", validate=True,
            )
        except (ValueError, TypeError):
            return None
        if len(payload) != 24:
            return None
        nonce = payload[:12]
        page_index = int.from_bytes(payload[12:16], "big")
        expected = self._page_chain_token(nonce, page_index, endpoint)
        if not hmac.compare_digest(expected, cursor):
            return None
        return nonce, page_index

    def _plan_page_chain(
        self,
        snapshot: _ProjectedSnapshot,
        section: str,
        limit: int,
        values: Sequence[Mapping[str, Any]],
        *,
        scope: Mapping[str, Any] | None = None,
        catalog_id: str | None = None,
    ) -> tuple[tuple[tuple[int, int], ...], int]:
        placeholder_cursor = "cursor_" + "x" * 32
        placeholder_expiry = "2000-01-01T00:00:00.000Z"
        ranges: list[tuple[int, int]] = []
        start = 0
        total = len(values)
        # Planning asks the same question of every candidate page size -- how
        # many bytes would this page be -- and the answer used to be paid for
        # with a fresh deep copy of every item in the candidate.  Copying the
        # catalog once here makes the planner linear in the catalog rather
        # than in catalog times page limit; the copies are private to this
        # call and are only ever measured, never published.
        thawed_values = [_thaw(item) for item in values]
        thawed_revision = _thaw(snapshot.state_revision)
        charge = self._canonical_public_bytes({"values": thawed_values})

        def probe_page(first: int, last: int) -> dict[str, Any]:
            more = last < total
            return self._public_page_from_thawed(
                thawed_revision,
                section,
                thawed_values[first:last],
                total,
                placeholder_cursor if more else None,
                placeholder_expiry if more else None,
                scope=scope,
                catalog_id=catalog_id,
                catalog_complete=not more,
            )

        if scope is not None and catalog_id is None:
            raise V2ControlError("internal_error")
        while start < total:
            end = start
            maximum = min(start + limit, total)
            for candidate in range(start + 1, maximum + 1):
                if self._canonical_public_bytes(
                    probe_page(start, candidate),
                ) > MAX_PUBLIC_PAGE_BYTES:
                    break
                end = candidate
            if end == start:
                raise V2ControlError("scope_too_large")
            ranges.append((start, end))
            charge += self._canonical_public_bytes(probe_page(start, end))
            start = end
        if not ranges:
            empty = self._chain_public_page(
                snapshot.state_revision, section, values, 0, 0, None, None,
                scope=scope, catalog_id=catalog_id,
            )
            self._checked_public_page(empty)
        return tuple(ranges), charge

    def _start_page_chain(
        self,
        snapshot: _ProjectedSnapshot,
        endpoint: str,
        section: str,
        limit: int,
        values: Sequence[Mapping[str, Any]],
        restart: Mapping[str, Any],
        *,
        scope: Mapping[str, Any] | None = None,
        catalog_id: str | None = None,
        pending_bindings: tuple[tuple[str, _ActionBinding], ...] = (),
        reserved: bool = False,
    ) -> dict[str, Any]:
        self._expire_page_chains()
        ranges, charge = self._plan_page_chain(
            snapshot, section, limit, values,
            scope=scope, catalog_id=catalog_id,
        )
        if len(ranges) <= 1:
            end = ranges[0][1] if ranges else 0
            page = self._checked_public_page(self._chain_public_page(
                snapshot.state_revision, section, values, 0, end, None, None,
                scope=scope, catalog_id=catalog_id,
            ))
            for action_id, binding in pending_bindings:
                self._publish_scoped_binding(action_id, binding)
            return page
        slots = len(ranges) - 1
        if (
            len(ranges) > MAX_CURSOR_CHAIN_PAGES
            or slots > MAX_CURSOR_CHAIN_SLOTS
            or charge > MAX_CURSOR_CHAIN_BYTES
        ):
            raise V2ControlError("scope_too_large")
        admissible = MAX_ACTIVE_CURSOR_CHAINS - (
            0 if reserved else RESERVED_CATALOG_CHAINS
        )
        self._reclaim_drained_chains(slots, charge, admissible)
        if (
            len(self._page_chains) >= admissible
            or len(self._retired_page_chains) >= MAX_RETIRED_CURSOR_CHAINS
            or self._page_chain_slots + slots > MAX_CURSOR_CHAIN_SLOTS
            or self._page_chain_bytes + charge > MAX_CURSOR_CHAIN_BYTES
        ):
            raise V2ControlError(
                "rate_limited", details=self._capacity_retry_details(),
            )
        nonce = secrets.token_bytes(12)
        while nonce in self._page_chains or nonce in self._retired_page_chains:
            nonce = secrets.token_bytes(12)
        tokens = tuple(
            "" if index == 0 else self._page_chain_token(
                nonce, index, endpoint,
            )
            for index in range(len(ranges))
        )
        now = time.monotonic()
        wall = time.time()
        next_cursor = tokens[1]
        next_wall = wall + CURSOR_TTL_SECONDS
        first = self._checked_public_page(self._chain_public_page(
            snapshot.state_revision,
            section,
            values,
            ranges[0][0],
            ranges[0][1],
            next_cursor,
            self._cursor_expiry_text(next_wall),
            scope=scope,
            catalog_id=catalog_id,
        ))
        chain = _PageChain(
            nonce=nonce,
            endpoint=endpoint,
            section=section,
            scope=_freeze(scope) if scope is not None else None,
            catalog_id=catalog_id,
            state_revision=_freeze(snapshot.state_revision),
            values=tuple(_freeze(item) for item in values),
            ranges=ranges,
            tokens=tokens,
            restart=_freeze(restart),
            charge_bytes=charge,
            deadlines={1: now + CURSOR_TTL_SECONDS},
            expiry_walls={1: next_wall},
            responses={},
            pending_bindings=pending_bindings,
            bindings_published=False,
            exposed_through=1,
            frontier=1,
        )
        self._page_chains[nonce] = chain
        self._page_chain_slots += slots
        self._page_chain_bytes += charge
        return first

    def _new_cursor(
        self,
        endpoint: str,
        section: str,
        native_revision: int,
        next_offset: int,
        limit: int,
        *,
        actor_id: str | None = None,
        center_id: str | None = None,
        radius: int | None = None,
    ) -> str:
        self._make_cursor_room()
        cursor = f"cursor_{secrets.token_urlsafe(24)}"
        while cursor in self._cursors or cursor in self._retired_cursors:
            cursor = f"cursor_{secrets.token_urlsafe(24)}"
        self._cursors[cursor] = _Cursor(
            endpoint=endpoint,
            section=section,
            native_revision=native_revision,
            next_offset=next_offset,
            limit=limit,
            actor_id=actor_id,
            center_id=center_id,
            radius=radius,
            expires_at=time.monotonic() + CURSOR_TTL_SECONDS,
            expires_at_wall=time.time() + CURSOR_TTL_SECONDS,
        )
        return cursor

    def _publish_scoped_binding(
        self, action_id: str, binding: _ActionBinding,
    ) -> None:
        self._scoped_action_bindings[action_id] = binding
        self._scoped_action_bindings.move_to_end(action_id)
        while len(self._scoped_action_bindings) > MAX_SCOPED_ACTION_BINDINGS:
            victim = next((
                candidate
                for candidate, existing in self._scoped_action_bindings.items()
                if not existing.target_scoped
            ), None)
            if victim is None:
                raise V2ControlError("scope_too_large")
            self._scoped_action_bindings.pop(victim, None)

    def _new_actor_scope_cursor(
        self,
        request: V2ActorScopeRequest,
        view_id: str,
        total_count: int,
        next_offset: int,
        seen_slots: tuple[str, ...],
        seen_capabilities: tuple[str, ...],
        pending_scope_bindings: tuple[
            tuple[str, _ActionBinding], ...
        ],
    ) -> str:
        self._make_cursor_room()
        cursor = f"cursor_{secrets.token_urlsafe(24)}"
        while cursor in self._cursors or cursor in self._retired_cursors:
            cursor = f"cursor_{secrets.token_urlsafe(24)}"
        self._cursors[cursor] = _ActorScopeCursor(
            endpoint="legal_actions",
            native_revision=request.native_revision,
            actor_id=request.actor_id,
            actor_kind=request.actor_kind,
            native_actor_ref=request.native_actor_ref,
            native_view_id=view_id,
            total_count=total_count,
            next_offset=next_offset,
            limit=request.limit,
            seen_slots=seen_slots,
            seen_capabilities=seen_capabilities,
            pending_scope_bindings=pending_scope_bindings,
            expires_at=time.monotonic() + CURSOR_TTL_SECONDS,
            expires_at_wall=time.time() + CURSOR_TTL_SECONDS,
        )
        return cursor

    def _new_relation_scope_cursor(
        self,
        request: V2RelationScopeRequest,
        view_id: str,
        total_count: int,
        next_offset: int,
        seen_slots: tuple[str, ...],
        seen_capabilities: tuple[str, ...],
        pending_scope_bindings: tuple[tuple[str, _ActionBinding], ...],
    ) -> str:
        self._make_cursor_room()
        cursor = f"cursor_{secrets.token_urlsafe(24)}"
        while cursor in self._cursors or cursor in self._retired_cursors:
            cursor = f"cursor_{secrets.token_urlsafe(24)}"
        self._cursors[cursor] = _RelationScopeCursor(
            endpoint="legal_actions",
            native_revision=request.native_revision,
            actor_id=request.actor_id,
            native_actor_ref=request.native_actor_ref,
            relation_id=request.relation_id,
            native_counterpart_ref=request.native_counterpart_ref,
            native_view_id=view_id,
            total_count=total_count,
            next_offset=next_offset,
            limit=request.limit,
            seen_slots=seen_slots,
            seen_capabilities=seen_capabilities,
            pending_scope_bindings=pending_scope_bindings,
            expires_at=time.monotonic() + CURSOR_TTL_SECONDS,
            expires_at_wall=time.time() + CURSOR_TTL_SECONDS,
        )
        return cursor

    @staticmethod
    def _cursor_expiry_text(expires_at_wall: float) -> str:
        return (
            datetime.fromtimestamp(expires_at_wall, tz=timezone.utc)
            .isoformat(timespec="milliseconds")
            .replace("+00:00", "Z")
        )

    def _cursor_expires_at(self, cursor: str | None) -> str | None:
        if cursor is None:
            return None
        record = self._cursors.get(cursor)
        if record is None:
            raise V2ControlError("internal_error")
        return self._cursor_expiry_text(record.expires_at_wall)

    @staticmethod
    def _cursor_restart(
        record: _Cursor | _StateScopeCursor | _ActorScopeCursor
        | _RelationScopeCursor,
    ) -> dict[str, Any]:
        if isinstance(record, _StateScopeCursor):
            query: dict[str, Any] = {
                "section": record.section, "limit": record.limit,
            }
            if record.actor_id is not None:
                query["actor_id"] = record.actor_id
            if record.center_id is not None:
                query["center_id"] = record.center_id
            if record.radius is not None:
                query["radius"] = record.radius
            return {"endpoint": "state", "query": query}
        if isinstance(record, _ActorScopeCursor):
            return {
                "endpoint": "legal_actions",
                "query": {
                    "actor_id": record.actor_id,
                    "limit": record.limit,
                },
            }
        if isinstance(record, _RelationScopeCursor):
            return {
                "endpoint": "legal_actions",
                "query": {
                    "actor_id": record.actor_id,
                    "target_id": record.relation_id,
                },
            }
        query: dict[str, Any]
        if record.endpoint == "state":
            query = {"section": record.section, "limit": record.limit}
            if record.actor_id is not None:
                query["actor_id"] = record.actor_id
            if record.center_id is not None:
                query["center_id"] = record.center_id
            if record.radius is not None:
                query["radius"] = record.radius
        else:
            query = {"limit": record.limit}
        return {"endpoint": record.endpoint, "query": query}

    def _retire_cursor(
        self,
        cursor: str,
        record: _Cursor | _StateScopeCursor | _ActorScopeCursor
        | _RelationScopeCursor,
        code: str,
    ) -> None:
        self._cursors.pop(cursor, None)
        self._cursor_leases.pop(cursor, None)
        details = MappingProxyType({
            "restart": self._cursor_restart(record),
        })
        self._retired_cursors[cursor] = _RetiredCursor(
            endpoint=record.endpoint,
            code=code,
            details=details,
            forget_at=time.monotonic() + RETIRED_CURSOR_TTL_SECONDS,
        )
        self._retired_cursors.move_to_end(cursor)

    def _cursor_record(
        self, cursor: str, endpoint: str,
    ) -> (
        _Cursor | _StateScopeCursor | _ActorScopeCursor
        | _RelationScopeCursor
    ):
        self._expire_cursors()
        record = self._cursors.get(cursor)
        if record is not None:
            if record.endpoint != endpoint:
                raise V2ControlError("invalid_request")
            return record
        retired = self._retired_cursors.get(cursor)
        if retired is not None:
            if retired.endpoint != endpoint:
                raise V2ControlError("invalid_request")
            raise V2ControlError(
                retired.code, details=_thaw(retired.details),
            )
        raise V2ControlError("invalid_request")

    def _make_cursor_room(self) -> None:
        self._expire_cursors()
        if (
            len(self._cursors) >= MAX_CURSORS
            or len(self._cursors) + len(self._retired_cursors)
               >= MAX_CURSOR_RECORDS
        ):
            raise V2ControlError(
                "rate_limited", details=self._capacity_retry_details(),
            )

    def _capacity_retry_details(self) -> dict[str, Any]:
        """Say when capacity frees up, since nothing the caller does releases it.

        Reaching here means every reclaimable record has already been
        reclaimed, so what remains is held until its own deadline and the
        earliest of those deadlines is the earliest a retry can succeed.  It
        is a floor, not a promise: another caller may take the slot first.
        """
        now = time.monotonic()
        deadlines = [
            record.expires_at for record in self._cursors.values()
            # A reservation in flight is released by its own completion, not
            # by the clock, so it cannot date a retry.
            if not record.in_flight
        ]
        for chain in self._page_chains.values():
            if not chain.deadlines:
                continue
            deadlines.append(
                chain.deadlines[chain.frontier]
                if chain.frontier is not None
                else max(chain.deadlines.values())
            )
        if not deadlines:
            return {}
        wait = max(0.0, min(deadlines) - now)
        return {
            "retry_after_seconds": math.ceil(wait),
            "retry_after": self._cursor_expiry_text(time.time() + wait),
        }

    def _expire_cursors(self) -> None:
        now = time.monotonic()
        for cursor, record in tuple(self._cursors.items()):
            if record.in_flight:
                # An abandoned reservation is released to the clock rather
                # than held forever.  Releasing (not retiring) is the weakest
                # thing that works: `take_*_scope_cursor` is non-destructive,
                # so the exact cursor stays usable and a late commit fails
                # only its own request.
                if self._cursor_leases.get(cursor, now) <= now:
                    self._cursor_leases.pop(cursor, None)
                    self._cursors[cursor] = replace(record, in_flight=False)
                continue
            self._cursor_leases.pop(cursor, None)
            if record.expires_at <= now:
                self._retire_cursor(cursor, record, "cursor_expired")
        for cursor in tuple(self._cursor_leases):
            if cursor not in self._cursors:
                self._cursor_leases.pop(cursor, None)
        for cursor, record in tuple(self._retired_cursors.items()):
            if record.forget_at <= now:
                self._retired_cursors.pop(cursor, None)

    def _retire_page_chain(
        self, nonce: bytes, chain: _PageChain, code: str = "cursor_expired",
    ) -> None:
        self._page_chains.pop(nonce, None)
        self._page_chain_slots -= len(chain.ranges) - 1
        self._page_chain_bytes -= chain.charge_bytes
        self._retired_page_chains[nonce] = _RetiredPageChain(
            endpoint=chain.endpoint,
            exposed_through=chain.exposed_through,
            restart=chain.restart,
            forget_at=time.monotonic() + RETIRED_CURSOR_TTL_SECONDS,
            code=code,
        )
        self._retired_page_chains.move_to_end(nonce)

    def _reclaim_drained_chains(
        self, slots: int, charge: int, admissible: int,
    ) -> None:
        """Under pressure, give up replay of traversals already finished.

        A chain whose frontier is gone handed out its terminal page with
        ``next_cursor: null``: the caller holds no cursor it was promised a
        future for, and every page it did consume is already in its hands.
        What the chain still buys is a byte-identical replay of pages it has
        served, and that is the only thing surrendered here -- and only when
        the alternative is refusing a caller that has no cursor at all.  A
        chain still owing a continuation is never touched, and a reclaimed
        chain stays an authentic tombstone that names its restart query.
        """
        while (
            len(self._page_chains) >= admissible
            or self._page_chain_slots + slots > MAX_CURSOR_CHAIN_SLOTS
            or self._page_chain_bytes + charge > MAX_CURSOR_CHAIN_BYTES
        ):
            victim = next((
                (nonce, chain)
                for nonce, chain in self._page_chains.items()
                if chain.frontier is None
            ), None)
            if victim is None:
                return
            self._retire_page_chain(*victim)

    def _expire_page_chains(self) -> None:
        now = time.monotonic()
        for nonce, chain in tuple(self._page_chains.items()):
            live_deadlines = tuple(
                deadline for deadline in chain.deadlines.values()
                if deadline > now
            )
            frontier_expired = (
                chain.frontier is not None
                and chain.deadlines[chain.frontier] <= now
            )
            if frontier_expired or (chain.frontier is None and not live_deadlines):
                self._retire_page_chain(nonce, chain)
        for nonce, record in tuple(self._retired_page_chains.items()):
            if record.forget_at <= now:
                self._retired_page_chains.pop(nonce, None)

    def _release_superseded_scopes(self) -> None:
        """Free every scoped record a newer revision has already invalidated.

        A scoped catalog is promoted atomically or not at all, and both
        ``take_*_scope_cursor`` and the chain continuation refuse an
        unpublished catalog once a newer revision lands.  Holding those
        records for the rest of their five minutes reserves capacity for
        traversals that can no longer return anything but ``stale_revision``,
        which is what exhausted the registry in live play.  Records for the
        current revision, and ordinary self-contained page chains, keep their
        full lifetime and their repeat-safety.
        """
        if not self._snapshots:
            return
        current = max(self._snapshots)
        for cursor, record in tuple(self._cursors.items()):
            if (
                isinstance(record, (_ActorScopeCursor, _RelationScopeCursor))
                and record.native_revision != current
                and not record.in_flight
            ):
                self._retire_cursor(cursor, record, "stale_revision")
        for nonce, chain in tuple(self._page_chains.items()):
            if (
                chain.pending_bindings
                and not chain.bindings_published
                and chain.state_revision.get("revision") != current
            ):
                self._retire_page_chain(nonce, chain, "stale_revision")

    def _evict_cursors(self, native_revision: int) -> None:
        for cursor, record in tuple(self._cursors.items()):
            # Ordinary pages need their materialized snapshot.  Actor-scoped
            # cursors retain only bounded opaque provenance; preserving that
            # record until consume/TTL/cap lets an authentic old cursor return
            # retryable stale_revision instead of looking random or foreign.
            if (
                isinstance(record, _Cursor)
                and record.native_revision == native_revision
            ):
                self._retire_cursor(cursor, record, "stale_revision")


__all__ = [
    "MAX_PUBLIC_PAGE_BYTES",
    "MAX_TILE_WINDOW_RADIUS",
    "NATIVE_OBSERVATION_ACTION_SCHEMA_ID",
    "V2ActorScopeRequest",
    "V2ActionResolution",
    "V2ControlError",
    "V2RelationScopeRequest",
    "V2SeatControl",
    "V2TargetActionRequest",
]
