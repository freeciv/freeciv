
 /****************************************************************************
 *                       THIS FILE WAS GENERATED                             *
 * Script: common/generate_packets.py                                        *
 * Input:  common/packets.def                                                *
 *                       DO NOT CHANGE THIS FILE                             *
 ****************************************************************************/



#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "packets.h"

#include "packhand_gen.h"
    
bool client_handle_packet(enum packet_type type, void *packet)
{
  switch(type) {
  case PACKET_PROCESSING_STARTED:
    handle_processing_started();
    return TRUE;

  case PACKET_PROCESSING_FINISHED:
    handle_processing_finished();
    return TRUE;

  case PACKET_FREEZE_HINT:
    handle_freeze_hint();
    return TRUE;

  case PACKET_THAW_HINT:
    handle_thaw_hint();
    return TRUE;

  case PACKET_SERVER_JOIN_REPLY:
    handle_server_join_reply(
      ((struct packet_server_join_reply *)packet)->you_can_join,
      ((struct packet_server_join_reply *)packet)->message,
      ((struct packet_server_join_reply *)packet)->capability,
      ((struct packet_server_join_reply *)packet)->challenge_file,
      ((struct packet_server_join_reply *)packet)->conn_id);
    return TRUE;

  case PACKET_AUTHENTICATION_REQ:
    handle_authentication_req(
      ((struct packet_authentication_req *)packet)->type,
      ((struct packet_authentication_req *)packet)->message);
    return TRUE;

  case PACKET_SERVER_SHUTDOWN:
    handle_server_shutdown();
    return TRUE;

  case PACKET_NATION_UNAVAILABLE:
    handle_nation_unavailable(
      ((struct packet_nation_unavailable *)packet)->nation);
    return TRUE;

  case PACKET_SELECT_RACES:
    handle_select_races();
    return TRUE;

  case PACKET_NATION_SELECT_OK:
    handle_nation_select_ok();
    return TRUE;

  case PACKET_GAME_STATE:
    handle_game_state(
      ((struct packet_game_state *)packet)->value);
    return TRUE;

  case PACKET_ENDGAME_REPORT:
    handle_endgame_report(packet);
    return TRUE;

  case PACKET_TILE_INFO:
    handle_tile_info(packet);
    return TRUE;

  case PACKET_GAME_INFO:
    handle_game_info(packet);
    return TRUE;

  case PACKET_MAP_INFO:
    handle_map_info(
      ((struct packet_map_info *)packet)->xsize,
      ((struct packet_map_info *)packet)->ysize,
      ((struct packet_map_info *)packet)->topology_id);
    return TRUE;

  case PACKET_NUKE_TILE_INFO:
    handle_nuke_tile_info(
      ((struct packet_nuke_tile_info *)packet)->x,
      ((struct packet_nuke_tile_info *)packet)->y);
    return TRUE;

  case PACKET_CHAT_MSG:
    handle_chat_msg(
      ((struct packet_chat_msg *)packet)->message,
      ((struct packet_chat_msg *)packet)->x,
      ((struct packet_chat_msg *)packet)->y,
      ((struct packet_chat_msg *)packet)->event,
      ((struct packet_chat_msg *)packet)->conn_id);
    return TRUE;

  case PACKET_CITY_REMOVE:
    handle_city_remove(
      ((struct packet_city_remove *)packet)->city_id);
    return TRUE;

  case PACKET_CITY_INFO:
    handle_city_info(packet);
    return TRUE;

  case PACKET_CITY_SHORT_INFO:
    handle_city_short_info(packet);
    return TRUE;

  case PACKET_CITY_INCITE_INFO:
    handle_city_incite_info(
      ((struct packet_city_incite_info *)packet)->city_id,
      ((struct packet_city_incite_info *)packet)->cost);
    return TRUE;

  case PACKET_CITY_NAME_SUGGESTION_INFO:
    handle_city_name_suggestion_info(
      ((struct packet_city_name_suggestion_info *)packet)->unit_id,
      ((struct packet_city_name_suggestion_info *)packet)->name);
    return TRUE;

  case PACKET_CITY_SABOTAGE_LIST:
    handle_city_sabotage_list(
      ((struct packet_city_sabotage_list *)packet)->diplomat_id,
      ((struct packet_city_sabotage_list *)packet)->city_id,
      ((struct packet_city_sabotage_list *)packet)->improvements);
    return TRUE;

  case PACKET_PLAYER_REMOVE:
    handle_player_remove(
      ((struct packet_player_remove *)packet)->player_id);
    return TRUE;

  case PACKET_PLAYER_INFO:
    handle_player_info(packet);
    return TRUE;

  case PACKET_PLAYER_ATTRIBUTE_CHUNK:
    handle_player_attribute_chunk(packet);
    return TRUE;

  case PACKET_UNIT_REMOVE:
    handle_unit_remove(
      ((struct packet_unit_remove *)packet)->unit_id);
    return TRUE;

  case PACKET_UNIT_INFO:
    handle_unit_info(packet);
    return TRUE;

  case PACKET_UNIT_SHORT_INFO:
    handle_unit_short_info(packet);
    return TRUE;

  case PACKET_UNIT_COMBAT_INFO:
    handle_unit_combat_info(
      ((struct packet_unit_combat_info *)packet)->attacker_unit_id,
      ((struct packet_unit_combat_info *)packet)->defender_unit_id,
      ((struct packet_unit_combat_info *)packet)->attacker_hp,
      ((struct packet_unit_combat_info *)packet)->defender_hp,
      ((struct packet_unit_combat_info *)packet)->make_winner_veteran);
    return TRUE;

  case PACKET_UNIT_BRIBE_INFO:
    handle_unit_bribe_info(
      ((struct packet_unit_bribe_info *)packet)->unit_id,
      ((struct packet_unit_bribe_info *)packet)->cost);
    return TRUE;

  case PACKET_UNIT_DIPLOMAT_POPUP_DIALOG:
    handle_unit_diplomat_popup_dialog(
      ((struct packet_unit_diplomat_popup_dialog *)packet)->diplomat_id,
      ((struct packet_unit_diplomat_popup_dialog *)packet)->target_id);
    return TRUE;

  case PACKET_DIPLOMACY_INIT_MEETING:
    handle_diplomacy_init_meeting(
      ((struct packet_diplomacy_init_meeting *)packet)->counterpart,
      ((struct packet_diplomacy_init_meeting *)packet)->initiated_from);
    return TRUE;

  case PACKET_DIPLOMACY_CANCEL_MEETING:
    handle_diplomacy_cancel_meeting(
      ((struct packet_diplomacy_cancel_meeting *)packet)->counterpart,
      ((struct packet_diplomacy_cancel_meeting *)packet)->initiated_from);
    return TRUE;

  case PACKET_DIPLOMACY_CREATE_CLAUSE:
    handle_diplomacy_create_clause(
      ((struct packet_diplomacy_create_clause *)packet)->counterpart,
      ((struct packet_diplomacy_create_clause *)packet)->giver,
      ((struct packet_diplomacy_create_clause *)packet)->type,
      ((struct packet_diplomacy_create_clause *)packet)->value);
    return TRUE;

  case PACKET_DIPLOMACY_REMOVE_CLAUSE:
    handle_diplomacy_remove_clause(
      ((struct packet_diplomacy_remove_clause *)packet)->counterpart,
      ((struct packet_diplomacy_remove_clause *)packet)->giver,
      ((struct packet_diplomacy_remove_clause *)packet)->type,
      ((struct packet_diplomacy_remove_clause *)packet)->value);
    return TRUE;

  case PACKET_DIPLOMACY_ACCEPT_TREATY:
    handle_diplomacy_accept_treaty(
      ((struct packet_diplomacy_accept_treaty *)packet)->counterpart,
      ((struct packet_diplomacy_accept_treaty *)packet)->I_accepted,
      ((struct packet_diplomacy_accept_treaty *)packet)->other_accepted);
    return TRUE;

  case PACKET_PAGE_MSG:
    handle_page_msg(
      ((struct packet_page_msg *)packet)->message,
      ((struct packet_page_msg *)packet)->event);
    return TRUE;

  case PACKET_CONN_INFO:
    handle_conn_info(packet);
    return TRUE;

  case PACKET_CONN_PING_INFO:
    handle_conn_ping_info(packet);
    return TRUE;

  case PACKET_CONN_PING:
    handle_conn_ping();
    return TRUE;

  case PACKET_BEFORE_NEW_YEAR:
    handle_before_new_year();
    return TRUE;

  case PACKET_START_TURN:
    handle_start_turn();
    return TRUE;

  case PACKET_NEW_YEAR:
    handle_new_year(
      ((struct packet_new_year *)packet)->year,
      ((struct packet_new_year *)packet)->turn);
    return TRUE;

  case PACKET_FREEZE_CLIENT:
    handle_freeze_client();
    return TRUE;

  case PACKET_THAW_CLIENT:
    handle_thaw_client();
    return TRUE;

  case PACKET_SPACESHIP_INFO:
    handle_spaceship_info(packet);
    return TRUE;

  case PACKET_RULESET_UNIT:
    handle_ruleset_unit(packet);
    return TRUE;

  case PACKET_RULESET_GAME:
    handle_ruleset_game(packet);
    return TRUE;

  case PACKET_RULESET_GOVERNMENT_RULER_TITLE:
    handle_ruleset_government_ruler_title(packet);
    return TRUE;

  case PACKET_RULESET_TECH:
    handle_ruleset_tech(packet);
    return TRUE;

  case PACKET_RULESET_GOVERNMENT:
    handle_ruleset_government(packet);
    return TRUE;

  case PACKET_RULESET_TERRAIN_CONTROL:
    handle_ruleset_terrain_control(packet);
    return TRUE;

  case PACKET_RULESET_NATION:
    handle_ruleset_nation(packet);
    return TRUE;

  case PACKET_RULESET_CITY:
    handle_ruleset_city(packet);
    return TRUE;

  case PACKET_RULESET_BUILDING:
    handle_ruleset_building(packet);
    return TRUE;

  case PACKET_RULESET_TERRAIN:
    handle_ruleset_terrain(packet);
    return TRUE;

  case PACKET_RULESET_CONTROL:
    handle_ruleset_control(packet);
    return TRUE;

  case PACKET_SINGLE_WANT_HACK_REPLY:
    handle_single_want_hack_reply(
      ((struct packet_single_want_hack_reply *)packet)->you_have_hack);
    return TRUE;

  case PACKET_GAME_LOAD:
    handle_game_load(packet);
    return TRUE;

  case PACKET_OPTIONS_SETTABLE_CONTROL:
    handle_options_settable_control(packet);
    return TRUE;

  case PACKET_OPTIONS_SETTABLE:
    handle_options_settable(packet);
    return TRUE;

  case PACKET_RULESET_CACHE_GROUP:
    handle_ruleset_cache_group(packet);
    return TRUE;

  case PACKET_RULESET_CACHE_EFFECT:
    handle_ruleset_cache_effect(packet);
    return TRUE;

  default:
    return FALSE;
  }
}
