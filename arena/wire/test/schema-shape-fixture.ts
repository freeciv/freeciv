/**
 * The declared shape of every named struct in `@arena/wire`: its field names,
 * and which of them are optional.  A trailing `?` means `Schema.optional(...)`.
 *
 * ## Why this file exists
 *
 * Required-ness was essentially untested.  Wrapping a randomly sampled
 * non-optional field in `Schema.optional` left `bun test`, `bun run typecheck`
 * and `bun run lint` all green for 18 of 22 fields tried — because every
 * corpus fixture *carries* the field, so "each archived fixture decodes" and
 * every round-trip test pass whether the schema demands it or not.  Only ten
 * hand-written negatives ever exercised a missing key.
 *
 * A port whose schema is too permissive accepts payloads the Python refuses,
 * which is a silent parity break in exactly the direction the negative
 * fixtures were built to catch.  This table is the second copy that makes
 * loosening a field a visible, reviewable diff instead of a green run.
 *
 * ## Maintaining it
 *
 * This is a snapshot, so it proves *change*, not correctness — it cannot tell
 * you a field should be required, only that someone changed their mind about
 * it.  When a change here is intended, update the row **and say why in the
 * commit**; when it is not, the schema is the thing to fix.
 *
 * Regenerate with the walker in `test/schema-shape.test.ts` — it is the same
 * code that checks it, so a mismatch is always a real difference in the AST.
 *
 * @module
 */

/** Struct identifier -> its sorted field list, `?`-suffixed where optional. */
export const SCHEMA_SHAPES: ReadonlyArray<readonly [string, ReadonlyArray<string>]> = [
  ['ArchiveResult', ['action_timeout_s?', 'artifact_id', 'artifact_urls', 'benchmark_valid', 'invalid_reasons', 'leaderboard', 'outcome', 'schema_version', 'score', 'state', 'timing_mode?']],
  ['ArchiveUrls', ['frames_url', 'join_url', 'replay_url', 'result_url', 'status_url', 'video_url', 'watch_json_url', 'watch_url']],
  ['ArtifactUrls', ['frames', 'replay', 'status', 'video', 'watch']],
  ['AutoEnd', ['armed', 'enabled', 'grace_s', 'remaining_s']],
  ['BatchDisposition', ['agent_id', 'batch_id', 'control_protocol', 'disposition', 'error', 'game_id', 'receipt', 'schema_version']],
  ['BoardCity', ['capital', 'id', 'name', 'player_id', 'size', 'x', 'y']],
  ['BoardExtra', ['id', 'name']],
  ['BoardPlayer', ['controller_label', 'controller_type', 'model', 'nation', 'place', 'player_color', 'player_id', 'player_name', 'scored', 'seat_id']],
  ['BoardQuery', ['turn']],
  ['BoardResponse', ['altitude_rows', 'cities', 'extra_layers', 'extras_catalog', 'game_id', 'height', 'owner_rows', 'players', 'schema_version', 'terrain_catalog', 'terrain_rows', 'topology', 'turn', 'unit_stacks', 'width', 'wrap']],
  ['BoardTerrain', ['code', 'name']],
  ['BoardUnitStack', ['count', 'player_id', 'types', 'x', 'y']],
  ['BoardUnitType', ['count', 'name']],
  ['CityCitizens', ['feelings', 'specialists']],
  ['CityFeeling(base)', ['angry', 'content', 'happy', 'stage', 'unhappy']],
  ['CityFeeling(effects)', ['angry', 'content', 'happy', 'stage', 'unhappy']],
  ['CityFeeling(final)', ['angry', 'content', 'happy', 'stage', 'unhappy']],
  ['CityFeeling(luxury)', ['angry', 'content', 'happy', 'stage', 'unhappy']],
  ['CityFeeling(martial_law)', ['angry', 'content', 'happy', 'stage', 'unhappy']],
  ['CityFeeling(nationality)', ['angry', 'content', 'happy', 'stage', 'unhappy']],
  ['CityImprovement', ['id', 'name']],
  ['CityInvestigationObservation', ['city', 'freshness', 'id', 'source', 'state_revision', 'type']],
  ['CityProduction', ['id', 'kind', 'name']],
  ['CityShields', ['stock', 'surplus']],
  ['CitySpecialist', ['count', 'id', 'name']],
  ['Command', ['action_id', 'arguments']],
  ['CommandBatch', ['agent_id', 'batch_id', 'commands', 'control_protocol', 'game_id', 'schema_version', 'state_revision']],
  ['CommandReceipt', ['agent_id', 'batch_id', 'control_protocol', 'error', 'game_id', 'idempotent', 'observation', 'receipt_state', 'schema_version', 'state_revision']],
  ['EvaluationContext', ['max_turns', 'objective', 'turns_remaining']],
  ['FrameManifest', ['frames', 'game_id', 'label', 'latest_png_url', 'schema_version']],
  ['GameEvent', ['actors', 'data', 'kind', 'summary', 'turn', 'weight']],
  ['GameEventsResponse', ['available', 'complete', 'event_counts', 'event_warnings', 'events', 'game_id', 'last_turn', 'min_included_weight', 'omitted_counts', 'schema_version', 'total_events', 'truncated']],
  ['GamePlace', ['ai_difficulty?', 'controller', 'controller_fingerprint?', 'controller_label?', 'controller_metadata?', 'controller_type?', 'joined', 'model?', 'place', 'player_color', 'player_name', 'seat_id']],
  ['GameRow', ['action_timeout_s?', 'ai_difficulty', 'benchmark_valid', 'control_protocol?', 'created_at', 'current_turn', 'finished_at', 'game_id', 'joined_agents', 'leaderboard', 'max_agents', 'mode', 'outcome', 'places', 'resolved_places', 'state', 'timing_mode?', 'turns', 'watch_path']],
  ['GameStatus', ['action_timeout_s?', 'ai_difficulty', 'barrier?', 'benchmark_valid', 'control_protocol?', 'created_at?', 'current_turn', 'error', 'finished_at?', 'frames_url', 'game_id', 'invalid_reasons', 'join_url', 'joined_agents', 'leaderboard', 'max_agents', 'mode', 'objective', 'outcome', 'phase?', 'phase_events_url?', 'places', 'replay_url', 'resolved_places', 'result_url', 'schema_version', 'state', 'status_url', 'timing_mode?', 'turns', 'video_url', 'watch_json_url', 'watch_url']],
  ['GamesIndexResponse', ['games', 'schema_version']],
  ['GatewayIdentity', ['cache_root', 'host', 'identity', 'kind', 'ok', 'pid', 'port', 'protocol_version', 'repo_root', 'runs_root', 'schema_version', 'upstream_service_url', 'url', 'viewer_public_url?']],
  ['GatewayProblem', ['error']],
  ['HealthAgent', ['agent_id', 'controller_label']],
  ['HealthEnvelope', ['agent', 'control_protocol', 'game_id', 'game_state', 'last_phase_end', 'last_recovery?', 'legal_actions_available', 'max_turns?', 'objective?', 'observation_available', 'phase', 'schema_version', 'seat', 'sidecar', 'turns_remaining?']],
  ['HealthSeat', ['place', 'player_name', 'seat_id', 'standing?']],
  ['InterruptedGameRow', ['action_timeout_s?', 'ai_difficulty', 'benchmark_valid', 'control_protocol?', 'created_at', 'current_turn', 'finished_at', 'game_id', 'joined_agents', 'leaderboard', 'max_agents', 'mode', 'outcome', 'places', 'resolved_places', 'state', 'timing_mode?', 'turns', 'watch_path']],
  ['InterruptedGameRow.outcome', ['leaders', 'margin', 'score_turn', 'status', 'summary', 'victory']],
  ['InvestigatedCity', ['citizens', 'id', 'improvements', 'name', 'production', 'shields', 'size']],
  ['LeaderboardEntry', ['ai_difficulty?', 'alive?', 'controller_label', 'controller_type', 'model', 'place', 'player_color', 'player_name', 'rank', 'score', 'score_turn', 'seat_id']],
  ['LegalActionDescriptor', ['action_id', 'arguments_schema', 'kind', 'label', 'state_revision', 'subject']],
  ['LegalActionPageBody', ['catalog_complete?', 'catalog_id?', 'cursor_expires_at?', 'items', 'next_cursor', 'scope?', 'section', 'total_items']],
  ['LegalActionPageEnvelope', ['agent_id', 'control_protocol', 'game_id', 'page', 'schema_version', 'state_revision']],
  ['Manifest', ['benchmark_valid', 'bridge_status_file', 'checkpoints', 'commands_file', 'config', 'control_protocol?', 'created_at', 'current_turn', 'error', 'finished_at', 'frames', 'game_id', 'invalid_reasons', 'joined_agents', 'recovery?', 'resolved_places', 'returncode', 'schema_version', 'scorelog_file', 'start_count', 'started_at', 'state', 'status', 'trace_file', 'video_file']],
  ['ManifestConfig', ['action_timeout_s', 'control_protocol?', 'difficulty?', 'lobby_timeout_s', 'max_agents', 'mode', 'name', 'objective', 'places', 'ruleset', 'schema_version', 'seats', 'seeds', 'server', 'timing_mode?', 'turns']],
  ['ManifestSeatConfig', ['ai_difficulty?', 'base_url', 'controller_fingerprint', 'controller_label', 'controller_metadata', 'id', 'instructions', 'model', 'name', 'options', 'type']],
  ['ManifestServerConfig', ['frame_interval', 'frame_zoom']],
  ['MapPlayer', ['ai_difficulty?', 'controller_label', 'controller_type', 'model?', 'place', 'player_color', 'player_id', 'player_name', 'scored', 'seat_id']],
  ['MatchOutcome', ['leaders', 'margin', 'score_turn', 'status', 'summary', 'victory']],
  ['MatchVictory', ['code', 'label', 'turn?', 'winners', 'year?']],
  ['PageScope', ['actor_id', 'actor_type', 'target_id?', 'target_type?']],
  ['PhaseBlock', ['active', 'auto_end?', 'phase', 'prior_end?', 'state', 'timing', 'turn', 'waiting_on?']],
  ['PhaseEndEvent', ['controller_label', 'controller_type', 'deadline_started_at', 'elapsed_s', 'ended_at', 'incarnation?', 'orders_submitted?', 'phase', 'place', 'player_color', 'player_name', 'receipt_state', 'resolution', 'seat_id', 'sequence', 'source', 'turn']],
  ['PhaseTiming', ['deadline_at', 'deadline_started_at', 'elapsed_s', 'mode', 'remaining_s', 'timeout_s']],
  ['PriorEnd', ['controller_label', 'elapsed_s', 'orders_submitted', 'phase', 'place', 'player_name', 'receipt_state', 'resolution', 'seat_id', 'source', 'turn']],
  ['RecoveryEvent', ['attempt', 'client_state', 'exit_code', 'exit_signal', 'format', 'game_id', 'kind', 'outcome', 'place', 'recovered_to_turn', 'rewound_applied_actions', 'schema_version', 'seat_id', 'sidecar_generation', 'timestamp', 'trigger', 'turn']],
  ['RecoverySummary', ['attempts', 'by_kind', 'by_outcome', 'recovered_to_turns', 'rewound_applied_actions']],
  ['ReplayCatalog', ['schema_version', 'technologies']],
  ['ReplayFrame', ['index', 'map_players', 'png_url', 'source_name', 'turn']],
  ['ReplayPlayer', ['ai_difficulty?', 'alive', 'cities', 'citizens', 'controller_label', 'controller_type', 'culture', 'future_techs', 'gained_tech_ids', 'gold', 'government', 'known_tech_ids', 'known_tech_names?', 'lost_tech_ids', 'model', 'nation', 'place', 'player_color', 'player_id', 'player_name', 'population', 'research', 'score', 'scored', 'seat_id', 'team_no?', 'units']],
  ['ReplayQuery', ['after_turn', 'limit']],
  ['ReplayResponse', ['available', 'catalog', 'complete', 'game_id', 'has_more', 'next_after_turn', 'replay_warnings', 'schema_version', 'snapshots', 'warnings?']],
  ['ReplaySnapshot', ['game_id', 'players', 'schema_version', 'turn', 'year']],
  ['ReplayWarning', ['message', 'turn?']],
  ['Report', ['episode', 'manifest', 'recovery?', 'score', 'seat_stats']],
  ['ResearchState', ['bulbs', 'cost', 'name', 'tech_id']],
  ['ResolvedPlace', ['ai_difficulty?', 'controller', 'controller_fingerprint?', 'controller_label?', 'controller_metadata?', 'controller_type?', 'joined', 'model?', 'place', 'player_color?', 'player_name', 'seat_id']],
  ['ResultPlayer', ['metrics', 'name', 'player_id', 'rank', 'score', 'seat_id']],
  ['ResultScore', ['final_turn', 'players']],
  ['Revision', ['revision', 'state_token', 'turn']],
  ['Score', ['final_turn', 'players']],
  ['ScorePlayer', ['added_turn?', 'alive?', 'controller_fingerprint', 'last_score_turn?', 'metrics', 'name', 'player_id', 'rank', 'removed_turn?', 'score', 'seat_id']],
  ['SeatStats', ['controller_fingerprint', 'decisions', 'fallbacks', 'input_tokens', 'latency_ms?', 'mean_latency_ms', 'output_tokens', 'turns']],
  ['SessionIdentity', ['agentId', 'controllerLabel', 'evaluation', 'gameId', 'place', 'playerName', 'seatId']],
  ['SidecarBlock', ['generation', 'state']],
  ['StatePageBody', ['catalog_complete?', 'catalog_id?', 'cursor_expires_at?', 'items', 'next_cursor', 'scope?', 'section', 'total_items']],
  ['StatePageEnvelope', ['agent_id', 'control_protocol', 'game_id', 'page', 'schema_version', 'state_revision']],
  ['StructuredError', ['control_protocol', 'error', 'schema_version', 'state_revision']],
  ['StructuredErrorBody', ['code', 'details', 'message', 'retryable']],
  ['Technology', ['cost_base', 'depth?', 'id', 'name', 'requires?', 'rule_name']],
  ['TechnologyCatalog', ['schema_version', 'technologies']],
  ['TechnologyEntry', ['cost_base', 'depth?', 'id', 'name', 'requires?', 'rule_name']],
  ['TurnTimelineEntry', ['resolved_at?', 'responded_seats?', 'timed_out_seats?', 'turn', 'year?']],
  ['UpstreamResult', ['artifact_id', 'artifact_urls', 'benchmark_valid', 'invalid_reasons', 'leaderboard', 'manifest', 'outcome', 'recovery?', 'score?', 'seat_stats?', 'state']],
  ['VictoryRecord', ['schema_version?', 'turn?', 'victory', 'winners', 'year?']],
  ['WaitEnvelope', ['agent_id', 'control_protocol', 'game_id', 'health', 'schema_version', 'state_revision', 'wake_reason']],
  ['WaitingOn', ['kind', 'seats', 'summary', 'waiting_s']],
  ['WaitingOnSeat', ['controller_label', 'is_self', 'place', 'player_name', 'seat_id', 'standing']],
  ['WatchReplayLink', ['available', 'url']],
  ['WatchResponse', ['frames', 'game', 'label', 'replay', 'schema_version', 'timeline', 'video']],
  ['WatchVideoLink', ['available', 'kind', 'url']],
];
