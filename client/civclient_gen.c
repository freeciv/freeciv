
 /****************************************************************************
 *                       THIS FILE WAS GENERATED                             *
 * Script: common/generate_packets.py                                        *
 * Input:  common/packets.def                                                *
 *                       DO NOT CHANGE THIS FILE                             *
 ****************************************************************************/

  case PACKET_PROCESSING_STARTED:
    handle_processing_started();
    break;

  case PACKET_PROCESSING_FINISHED:
    handle_processing_finished();
    break;

  case PACKET_FREEZE_HINT:
    handle_freeze_hint();
    break;

  case PACKET_THAW_HINT:
    handle_thaw_hint();
    break;

  case PACKET_SERVER_JOIN_REPLY:
    handle_server_join_reply(
      ((struct packet_server_join_reply *)packet)->you_can_join,
      ((struct packet_server_join_reply *)packet)->message,
      ((struct packet_server_join_reply *)packet)->capability,
      ((struct packet_server_join_reply *)packet)->conn_id);
    break;

  case PACKET_AUTHENTICATION_REQ:
    handle_authentication_req(
      ((struct packet_authentication_req *)packet)->type,
      ((struct packet_authentication_req *)packet)->message);
    break;

  case PACKET_SERVER_SHUTDOWN:
    handle_server_shutdown();
    break;

  case PACKET_NATIONS_SELECTED_INFO:
    handle_nations_selected_info(
      ((struct packet_nations_selected_info *)packet)->num_nations_used,
      ((struct packet_nations_selected_info *)packet)->nations_used);
    break;

  case PACKET_NATION_SELECT_OK:
    handle_nation_select_ok();
    break;

  case PACKET_GAME_STATE:
    handle_game_state(
      ((struct packet_game_state *)packet)->value);
    break;

  case PACKET_ENDGAME_REPORT:
    handle_endgame_report(packet);
    break;

  case PACKET_TILE_INFO:
    handle_tile_info(packet);
    break;

  case PACKET_GAME_INFO:
    handle_game_info(packet);
    break;

  case PACKET_MAP_INFO:
    handle_map_info(
      ((struct packet_map_info *)packet)->xsize,
      ((struct packet_map_info *)packet)->ysize,
      ((struct packet_map_info *)packet)->is_earth,
      ((struct packet_map_info *)packet)->topology_id);
    break;

  case PACKET_NUKE_TILE_INFO:
    handle_nuke_tile_info(
      ((struct packet_nuke_tile_info *)packet)->x,
      ((struct packet_nuke_tile_info *)packet)->y);
    break;

  case PACKET_CHAT_MSG:
    handle_chat_msg(
      ((struct packet_chat_msg *)packet)->message,
      ((struct packet_chat_msg *)packet)->x,
      ((struct packet_chat_msg *)packet)->y,
      ((struct packet_chat_msg *)packet)->event);
    break;

  case PACKET_CITY_REMOVE:
    handle_city_remove(
      ((struct packet_city_remove *)packet)->city_id);
    break;

  case PACKET_CITY_INFO:
    handle_city_info(packet);
    break;

  case PACKET_CITY_SHORT_INFO:
    handle_city_short_info(packet);
    break;

  case PACKET_CITY_INCITE_INFO:
    handle_city_incite_info(
      ((struct packet_city_incite_info *)packet)->city_id,
      ((struct packet_city_incite_info *)packet)->cost);
    break;

  case PACKET_CITY_NAME_SUGGESTION_INFO:
    handle_city_name_suggestion_info(
      ((struct packet_city_name_suggestion_info *)packet)->unit_id,
      ((struct packet_city_name_suggestion_info *)packet)->name);
    break;

  case PACKET_CITY_SABOTAGE_LIST:
    handle_city_sabotage_list(
      ((struct packet_city_sabotage_list *)packet)->diplomat_id,
      ((struct packet_city_sabotage_list *)packet)->city_id,
      ((struct packet_city_sabotage_list *)packet)->improvements);
    break;

  case PACKET_PLAYER_REMOVE:
    handle_player_remove(
      ((struct packet_player_remove *)packet)->player_id);
    break;

  case PACKET_PLAYER_INFO:
    handle_player_info(packet);
    break;

  case PACKET_PLAYER_ATTRIBUTE_CHUNK:
    handle_player_attribute_chunk(packet);
    break;

  case PACKET_UNIT_REMOVE:
    handle_unit_remove(
      ((struct packet_unit_remove *)packet)->unit_id);
    break;

  case PACKET_UNIT_INFO:
    handle_unit_info(packet);
    break;

  case PACKET_UNIT_SHORT_INFO:
    handle_unit_short_info(packet);
    break;

  case PACKET_UNIT_COMBAT_INFO:
    handle_unit_combat_info(
      ((struct packet_unit_combat_info *)packet)->attacker_unit_id,
      ((struct packet_unit_combat_info *)packet)->defender_unit_id,
      ((struct packet_unit_combat_info *)packet)->attacker_hp,
      ((struct packet_unit_combat_info *)packet)->defender_hp,
      ((struct packet_unit_combat_info *)packet)->make_winner_veteran);
    break;

  case PACKET_UNIT_BRIBE_INFO:
    handle_unit_bribe_info(
      ((struct packet_unit_bribe_info *)packet)->unit_id,
      ((struct packet_unit_bribe_info *)packet)->cost);
    break;

  case PACKET_UNIT_DIPLOMAT_POPUP_DIALOG:
    handle_unit_diplomat_popup_dialog(
      ((struct packet_unit_diplomat_popup_dialog *)packet)->diplomat_id,
      ((struct packet_unit_diplomat_popup_dialog *)packet)->target_id);
    break;

  case PACKET_DIPLOMACY_INIT_MEETING:
    handle_diplomacy_init_meeting(
      ((struct packet_diplomacy_init_meeting *)packet)->counterpart,
      ((struct packet_diplomacy_init_meeting *)packet)->initiated_from);
    break;

  case PACKET_DIPLOMACY_CANCEL_MEETING:
    handle_diplomacy_cancel_meeting(
      ((struct packet_diplomacy_cancel_meeting *)packet)->counterpart,
      ((struct packet_diplomacy_cancel_meeting *)packet)->initiated_from);
    break;

  case PACKET_DIPLOMACY_CREATE_CLAUSE:
    handle_diplomacy_create_clause(
      ((struct packet_diplomacy_create_clause *)packet)->counterpart,
      ((struct packet_diplomacy_create_clause *)packet)->giver,
      ((struct packet_diplomacy_create_clause *)packet)->type,
      ((struct packet_diplomacy_create_clause *)packet)->value);
    break;

  case PACKET_DIPLOMACY_REMOVE_CLAUSE:
    handle_diplomacy_remove_clause(
      ((struct packet_diplomacy_remove_clause *)packet)->counterpart,
      ((struct packet_diplomacy_remove_clause *)packet)->giver,
      ((struct packet_diplomacy_remove_clause *)packet)->type,
      ((struct packet_diplomacy_remove_clause *)packet)->value);
    break;

  case PACKET_DIPLOMACY_ACCEPT_TREATY:
    handle_diplomacy_accept_treaty(
      ((struct packet_diplomacy_accept_treaty *)packet)->counterpart,
      ((struct packet_diplomacy_accept_treaty *)packet)->I_accepted,
      ((struct packet_diplomacy_accept_treaty *)packet)->other_accepted);
    break;

  case PACKET_PAGE_MSG:
    handle_page_msg(
      ((struct packet_page_msg *)packet)->message,
      ((struct packet_page_msg *)packet)->event);
    break;

  case PACKET_CONN_INFO:
    handle_conn_info(packet);
    break;

  case PACKET_CONN_PING_INFO:
    handle_conn_ping_info(
      ((struct packet_conn_ping_info *)packet)->connections,
      ((struct packet_conn_ping_info *)packet)->conn_id,
      ((struct packet_conn_ping_info *)packet)->ping_time);
    break;

  case PACKET_CONN_PING:
    handle_conn_ping();
    break;

  case PACKET_BEFORE_NEW_YEAR:
    handle_before_new_year();
    break;

  case PACKET_START_TURN:
    handle_start_turn();
    break;

  case PACKET_NEW_YEAR:
    handle_new_year(
      ((struct packet_new_year *)packet)->year,
      ((struct packet_new_year *)packet)->turn);
    break;

  case PACKET_SPACESHIP_INFO:
    handle_spaceship_info(packet);
    break;

  case PACKET_RULESET_UNIT:
    handle_ruleset_unit(packet);
    break;

  case PACKET_RULESET_GAME:
    handle_ruleset_game(packet);
    break;

  case PACKET_RULESET_GOVERNMENT_RULER_TITLE:
    handle_ruleset_government_ruler_title(packet);
    break;

  case PACKET_RULESET_TECH:
    handle_ruleset_tech(packet);
    break;

  case PACKET_RULESET_GOVERNMENT:
    handle_ruleset_government(packet);
    break;

  case PACKET_RULESET_TERRAIN_CONTROL:
    handle_ruleset_terrain_control(packet);
    break;

  case PACKET_RULESET_NATION:
    handle_ruleset_nation(packet);
    break;

  case PACKET_RULESET_CITY:
    handle_ruleset_city(packet);
    break;

  case PACKET_RULESET_BUILDING:
    handle_ruleset_building(packet);
    break;

  case PACKET_RULESET_TERRAIN:
    handle_ruleset_terrain(packet);
    break;

  case PACKET_RULESET_CONTROL:
    handle_ruleset_control(packet);
    break;

