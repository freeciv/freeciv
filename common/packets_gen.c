
 /****************************************************************************
 *                       THIS FILE WAS GENERATED                             *
 * Script: common/generate_packets.py                                        *
 * Input:  common/packets.def                                                *
 *                       DO NOT CHANGE THIS FILE                             *
 ****************************************************************************/


#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <assert.h>
#include <string.h>

#include "capability.h"
#include "capstr.h"
#include "connection.h"
#include "dataio.h"
#include "hash.h"
#include "log.h"
#include "mem.h"
#include "support.h"

#include "packets.h"

static unsigned int hash_const(const void *vkey, unsigned int num_buckets)
{
  return 0;
}

static int cmp_const(const void *vkey1, const void *vkey2)
{
  return 0;
}

void delta_stats_report(void) {}

void delta_stats_reset(void) {}

void *get_packet_from_connection_helper(struct connection *pc,
    enum packet_type type)
{
  switch(type) {

  case PACKET_PROCESSING_STARTED:
    return receive_packet_processing_started(pc, type);

  case PACKET_PROCESSING_FINISHED:
    return receive_packet_processing_finished(pc, type);

  case PACKET_FREEZE_HINT:
    return receive_packet_freeze_hint(pc, type);

  case PACKET_THAW_HINT:
    return receive_packet_thaw_hint(pc, type);

  case PACKET_SERVER_JOIN_REQ:
    return receive_packet_server_join_req(pc, type);

  case PACKET_SERVER_JOIN_REPLY:
    return receive_packet_server_join_reply(pc, type);

  case PACKET_AUTHENTICATION_REQ:
    return receive_packet_authentication_req(pc, type);

  case PACKET_AUTHENTICATION_REPLY:
    return receive_packet_authentication_reply(pc, type);

  case PACKET_SERVER_SHUTDOWN:
    return receive_packet_server_shutdown(pc, type);

  case PACKET_NATION_UNAVAILABLE:
    return receive_packet_nation_unavailable(pc, type);

  case PACKET_SELECT_RACES:
    return receive_packet_select_races(pc, type);

  case PACKET_NATION_SELECT_REQ:
    return receive_packet_nation_select_req(pc, type);

  case PACKET_NATION_SELECT_OK:
    return receive_packet_nation_select_ok(pc, type);

  case PACKET_GAME_STATE:
    return receive_packet_game_state(pc, type);

  case PACKET_ENDGAME_REPORT:
    return receive_packet_endgame_report(pc, type);

  case PACKET_TILE_INFO:
    return receive_packet_tile_info(pc, type);

  case PACKET_GAME_INFO:
    return receive_packet_game_info(pc, type);

  case PACKET_MAP_INFO:
    return receive_packet_map_info(pc, type);

  case PACKET_NUKE_TILE_INFO:
    return receive_packet_nuke_tile_info(pc, type);

  case PACKET_CHAT_MSG:
    return receive_packet_chat_msg(pc, type);

  case PACKET_CHAT_MSG_REQ:
    return receive_packet_chat_msg_req(pc, type);

  case PACKET_CITY_REMOVE:
    return receive_packet_city_remove(pc, type);

  case PACKET_CITY_INFO:
    return receive_packet_city_info(pc, type);

  case PACKET_CITY_SHORT_INFO:
    return receive_packet_city_short_info(pc, type);

  case PACKET_CITY_SELL:
    return receive_packet_city_sell(pc, type);

  case PACKET_CITY_BUY:
    return receive_packet_city_buy(pc, type);

  case PACKET_CITY_CHANGE:
    return receive_packet_city_change(pc, type);

  case PACKET_CITY_WORKLIST:
    return receive_packet_city_worklist(pc, type);

  case PACKET_CITY_MAKE_SPECIALIST:
    return receive_packet_city_make_specialist(pc, type);

  case PACKET_CITY_MAKE_WORKER:
    return receive_packet_city_make_worker(pc, type);

  case PACKET_CITY_CHANGE_SPECIALIST:
    return receive_packet_city_change_specialist(pc, type);

  case PACKET_CITY_RENAME:
    return receive_packet_city_rename(pc, type);

  case PACKET_CITY_OPTIONS_REQ:
    return receive_packet_city_options_req(pc, type);

  case PACKET_CITY_REFRESH:
    return receive_packet_city_refresh(pc, type);

  case PACKET_CITY_INCITE_INQ:
    return receive_packet_city_incite_inq(pc, type);

  case PACKET_CITY_INCITE_INFO:
    return receive_packet_city_incite_info(pc, type);

  case PACKET_CITY_NAME_SUGGESTION_REQ:
    return receive_packet_city_name_suggestion_req(pc, type);

  case PACKET_CITY_NAME_SUGGESTION_INFO:
    return receive_packet_city_name_suggestion_info(pc, type);

  case PACKET_CITY_SABOTAGE_LIST:
    return receive_packet_city_sabotage_list(pc, type);

  case PACKET_PLAYER_REMOVE:
    return receive_packet_player_remove(pc, type);

  case PACKET_PLAYER_INFO:
    return receive_packet_player_info(pc, type);

  case PACKET_PLAYER_TURN_DONE:
    return receive_packet_player_turn_done(pc, type);

  case PACKET_PLAYER_RATES:
    return receive_packet_player_rates(pc, type);

  case PACKET_PLAYER_CHANGE_GOVERNMENT:
    return receive_packet_player_change_government(pc, type);

  case PACKET_PLAYER_RESEARCH:
    return receive_packet_player_research(pc, type);

  case PACKET_PLAYER_TECH_GOAL:
    return receive_packet_player_tech_goal(pc, type);

  case PACKET_PLAYER_ATTRIBUTE_BLOCK:
    return receive_packet_player_attribute_block(pc, type);

  case PACKET_PLAYER_ATTRIBUTE_CHUNK:
    return receive_packet_player_attribute_chunk(pc, type);

  case PACKET_UNIT_REMOVE:
    return receive_packet_unit_remove(pc, type);

  case PACKET_UNIT_INFO:
    return receive_packet_unit_info(pc, type);

  case PACKET_UNIT_SHORT_INFO:
    return receive_packet_unit_short_info(pc, type);

  case PACKET_UNIT_COMBAT_INFO:
    return receive_packet_unit_combat_info(pc, type);

  case PACKET_UNIT_MOVE:
    return receive_packet_unit_move(pc, type);

  case PACKET_UNIT_BUILD_CITY:
    return receive_packet_unit_build_city(pc, type);

  case PACKET_UNIT_DISBAND:
    return receive_packet_unit_disband(pc, type);

  case PACKET_UNIT_CHANGE_HOMECITY:
    return receive_packet_unit_change_homecity(pc, type);

  case PACKET_UNIT_ESTABLISH_TRADE:
    return receive_packet_unit_establish_trade(pc, type);

  case PACKET_UNIT_HELP_BUILD_WONDER:
    return receive_packet_unit_help_build_wonder(pc, type);

  case PACKET_UNIT_GOTO:
    return receive_packet_unit_goto(pc, type);

  case PACKET_UNIT_ORDERS:
    return receive_packet_unit_orders(pc, type);

  case PACKET_UNIT_AUTO:
    return receive_packet_unit_auto(pc, type);

  case PACKET_UNIT_LOAD:
    return receive_packet_unit_load(pc, type);

  case PACKET_UNIT_UNLOAD:
    return receive_packet_unit_unload(pc, type);

  case PACKET_UNIT_UPGRADE:
    return receive_packet_unit_upgrade(pc, type);

  case PACKET_UNIT_NUKE:
    return receive_packet_unit_nuke(pc, type);

  case PACKET_UNIT_PARADROP_TO:
    return receive_packet_unit_paradrop_to(pc, type);

  case PACKET_UNIT_AIRLIFT:
    return receive_packet_unit_airlift(pc, type);

  case PACKET_UNIT_BRIBE_INQ:
    return receive_packet_unit_bribe_inq(pc, type);

  case PACKET_UNIT_BRIBE_INFO:
    return receive_packet_unit_bribe_info(pc, type);

  case PACKET_UNIT_TYPE_UPGRADE:
    return receive_packet_unit_type_upgrade(pc, type);

  case PACKET_UNIT_DIPLOMAT_ACTION:
    return receive_packet_unit_diplomat_action(pc, type);

  case PACKET_UNIT_DIPLOMAT_POPUP_DIALOG:
    return receive_packet_unit_diplomat_popup_dialog(pc, type);

  case PACKET_UNIT_CHANGE_ACTIVITY:
    return receive_packet_unit_change_activity(pc, type);

  case PACKET_DIPLOMACY_INIT_MEETING_REQ:
    return receive_packet_diplomacy_init_meeting_req(pc, type);

  case PACKET_DIPLOMACY_INIT_MEETING:
    return receive_packet_diplomacy_init_meeting(pc, type);

  case PACKET_DIPLOMACY_CANCEL_MEETING_REQ:
    return receive_packet_diplomacy_cancel_meeting_req(pc, type);

  case PACKET_DIPLOMACY_CANCEL_MEETING:
    return receive_packet_diplomacy_cancel_meeting(pc, type);

  case PACKET_DIPLOMACY_CREATE_CLAUSE_REQ:
    return receive_packet_diplomacy_create_clause_req(pc, type);

  case PACKET_DIPLOMACY_CREATE_CLAUSE:
    return receive_packet_diplomacy_create_clause(pc, type);

  case PACKET_DIPLOMACY_REMOVE_CLAUSE_REQ:
    return receive_packet_diplomacy_remove_clause_req(pc, type);

  case PACKET_DIPLOMACY_REMOVE_CLAUSE:
    return receive_packet_diplomacy_remove_clause(pc, type);

  case PACKET_DIPLOMACY_ACCEPT_TREATY_REQ:
    return receive_packet_diplomacy_accept_treaty_req(pc, type);

  case PACKET_DIPLOMACY_ACCEPT_TREATY:
    return receive_packet_diplomacy_accept_treaty(pc, type);

  case PACKET_DIPLOMACY_CANCEL_PACT:
    return receive_packet_diplomacy_cancel_pact(pc, type);

  case PACKET_PAGE_MSG:
    return receive_packet_page_msg(pc, type);

  case PACKET_REPORT_REQ:
    return receive_packet_report_req(pc, type);

  case PACKET_CONN_INFO:
    return receive_packet_conn_info(pc, type);

  case PACKET_CONN_PING_INFO:
    return receive_packet_conn_ping_info(pc, type);

  case PACKET_CONN_PING:
    return receive_packet_conn_ping(pc, type);

  case PACKET_CONN_PONG:
    return receive_packet_conn_pong(pc, type);

  case PACKET_BEFORE_NEW_YEAR:
    return receive_packet_before_new_year(pc, type);

  case PACKET_START_TURN:
    return receive_packet_start_turn(pc, type);

  case PACKET_NEW_YEAR:
    return receive_packet_new_year(pc, type);

  case PACKET_FREEZE_CLIENT:
    return receive_packet_freeze_client(pc, type);

  case PACKET_THAW_CLIENT:
    return receive_packet_thaw_client(pc, type);

  case PACKET_SPACESHIP_LAUNCH:
    return receive_packet_spaceship_launch(pc, type);

  case PACKET_SPACESHIP_PLACE:
    return receive_packet_spaceship_place(pc, type);

  case PACKET_SPACESHIP_INFO:
    return receive_packet_spaceship_info(pc, type);

  case PACKET_RULESET_UNIT:
    return receive_packet_ruleset_unit(pc, type);

  case PACKET_RULESET_GAME:
    return receive_packet_ruleset_game(pc, type);

  case PACKET_RULESET_GOVERNMENT_RULER_TITLE:
    return receive_packet_ruleset_government_ruler_title(pc, type);

  case PACKET_RULESET_TECH:
    return receive_packet_ruleset_tech(pc, type);

  case PACKET_RULESET_GOVERNMENT:
    return receive_packet_ruleset_government(pc, type);

  case PACKET_RULESET_TERRAIN_CONTROL:
    return receive_packet_ruleset_terrain_control(pc, type);

  case PACKET_RULESET_NATION:
    return receive_packet_ruleset_nation(pc, type);

  case PACKET_RULESET_CITY:
    return receive_packet_ruleset_city(pc, type);

  case PACKET_RULESET_BUILDING:
    return receive_packet_ruleset_building(pc, type);

  case PACKET_RULESET_TERRAIN:
    return receive_packet_ruleset_terrain(pc, type);

  case PACKET_RULESET_CONTROL:
    return receive_packet_ruleset_control(pc, type);

  case PACKET_SINGLE_WANT_HACK_REQ:
    return receive_packet_single_want_hack_req(pc, type);

  case PACKET_SINGLE_WANT_HACK_REPLY:
    return receive_packet_single_want_hack_reply(pc, type);

  case PACKET_GAME_LOAD:
    return receive_packet_game_load(pc, type);

  case PACKET_OPTIONS_SETTABLE_CONTROL:
    return receive_packet_options_settable_control(pc, type);

  case PACKET_OPTIONS_SETTABLE:
    return receive_packet_options_settable(pc, type);

  case PACKET_RULESET_CACHE_GROUP:
    return receive_packet_ruleset_cache_group(pc, type);

  case PACKET_RULESET_CACHE_EFFECT:
    return receive_packet_ruleset_cache_effect(pc, type);

  default:
    freelog(LOG_ERROR, "unknown packet type %d received from %s",
	    type, conn_description(pc));
    remove_packet_from_buffer(pc->buffer);
    return NULL;
  };
}

const char *get_packet_name(enum packet_type type)
{
  switch(type) {

  case PACKET_PROCESSING_STARTED:
    return "PACKET_PROCESSING_STARTED";

  case PACKET_PROCESSING_FINISHED:
    return "PACKET_PROCESSING_FINISHED";

  case PACKET_FREEZE_HINT:
    return "PACKET_FREEZE_HINT";

  case PACKET_THAW_HINT:
    return "PACKET_THAW_HINT";

  case PACKET_SERVER_JOIN_REQ:
    return "PACKET_SERVER_JOIN_REQ";

  case PACKET_SERVER_JOIN_REPLY:
    return "PACKET_SERVER_JOIN_REPLY";

  case PACKET_AUTHENTICATION_REQ:
    return "PACKET_AUTHENTICATION_REQ";

  case PACKET_AUTHENTICATION_REPLY:
    return "PACKET_AUTHENTICATION_REPLY";

  case PACKET_SERVER_SHUTDOWN:
    return "PACKET_SERVER_SHUTDOWN";

  case PACKET_NATION_UNAVAILABLE:
    return "PACKET_NATION_UNAVAILABLE";

  case PACKET_SELECT_RACES:
    return "PACKET_SELECT_RACES";

  case PACKET_NATION_SELECT_REQ:
    return "PACKET_NATION_SELECT_REQ";

  case PACKET_NATION_SELECT_OK:
    return "PACKET_NATION_SELECT_OK";

  case PACKET_GAME_STATE:
    return "PACKET_GAME_STATE";

  case PACKET_ENDGAME_REPORT:
    return "PACKET_ENDGAME_REPORT";

  case PACKET_TILE_INFO:
    return "PACKET_TILE_INFO";

  case PACKET_GAME_INFO:
    return "PACKET_GAME_INFO";

  case PACKET_MAP_INFO:
    return "PACKET_MAP_INFO";

  case PACKET_NUKE_TILE_INFO:
    return "PACKET_NUKE_TILE_INFO";

  case PACKET_CHAT_MSG:
    return "PACKET_CHAT_MSG";

  case PACKET_CHAT_MSG_REQ:
    return "PACKET_CHAT_MSG_REQ";

  case PACKET_CITY_REMOVE:
    return "PACKET_CITY_REMOVE";

  case PACKET_CITY_INFO:
    return "PACKET_CITY_INFO";

  case PACKET_CITY_SHORT_INFO:
    return "PACKET_CITY_SHORT_INFO";

  case PACKET_CITY_SELL:
    return "PACKET_CITY_SELL";

  case PACKET_CITY_BUY:
    return "PACKET_CITY_BUY";

  case PACKET_CITY_CHANGE:
    return "PACKET_CITY_CHANGE";

  case PACKET_CITY_WORKLIST:
    return "PACKET_CITY_WORKLIST";

  case PACKET_CITY_MAKE_SPECIALIST:
    return "PACKET_CITY_MAKE_SPECIALIST";

  case PACKET_CITY_MAKE_WORKER:
    return "PACKET_CITY_MAKE_WORKER";

  case PACKET_CITY_CHANGE_SPECIALIST:
    return "PACKET_CITY_CHANGE_SPECIALIST";

  case PACKET_CITY_RENAME:
    return "PACKET_CITY_RENAME";

  case PACKET_CITY_OPTIONS_REQ:
    return "PACKET_CITY_OPTIONS_REQ";

  case PACKET_CITY_REFRESH:
    return "PACKET_CITY_REFRESH";

  case PACKET_CITY_INCITE_INQ:
    return "PACKET_CITY_INCITE_INQ";

  case PACKET_CITY_INCITE_INFO:
    return "PACKET_CITY_INCITE_INFO";

  case PACKET_CITY_NAME_SUGGESTION_REQ:
    return "PACKET_CITY_NAME_SUGGESTION_REQ";

  case PACKET_CITY_NAME_SUGGESTION_INFO:
    return "PACKET_CITY_NAME_SUGGESTION_INFO";

  case PACKET_CITY_SABOTAGE_LIST:
    return "PACKET_CITY_SABOTAGE_LIST";

  case PACKET_PLAYER_REMOVE:
    return "PACKET_PLAYER_REMOVE";

  case PACKET_PLAYER_INFO:
    return "PACKET_PLAYER_INFO";

  case PACKET_PLAYER_TURN_DONE:
    return "PACKET_PLAYER_TURN_DONE";

  case PACKET_PLAYER_RATES:
    return "PACKET_PLAYER_RATES";

  case PACKET_PLAYER_CHANGE_GOVERNMENT:
    return "PACKET_PLAYER_CHANGE_GOVERNMENT";

  case PACKET_PLAYER_RESEARCH:
    return "PACKET_PLAYER_RESEARCH";

  case PACKET_PLAYER_TECH_GOAL:
    return "PACKET_PLAYER_TECH_GOAL";

  case PACKET_PLAYER_ATTRIBUTE_BLOCK:
    return "PACKET_PLAYER_ATTRIBUTE_BLOCK";

  case PACKET_PLAYER_ATTRIBUTE_CHUNK:
    return "PACKET_PLAYER_ATTRIBUTE_CHUNK";

  case PACKET_UNIT_REMOVE:
    return "PACKET_UNIT_REMOVE";

  case PACKET_UNIT_INFO:
    return "PACKET_UNIT_INFO";

  case PACKET_UNIT_SHORT_INFO:
    return "PACKET_UNIT_SHORT_INFO";

  case PACKET_UNIT_COMBAT_INFO:
    return "PACKET_UNIT_COMBAT_INFO";

  case PACKET_UNIT_MOVE:
    return "PACKET_UNIT_MOVE";

  case PACKET_UNIT_BUILD_CITY:
    return "PACKET_UNIT_BUILD_CITY";

  case PACKET_UNIT_DISBAND:
    return "PACKET_UNIT_DISBAND";

  case PACKET_UNIT_CHANGE_HOMECITY:
    return "PACKET_UNIT_CHANGE_HOMECITY";

  case PACKET_UNIT_ESTABLISH_TRADE:
    return "PACKET_UNIT_ESTABLISH_TRADE";

  case PACKET_UNIT_HELP_BUILD_WONDER:
    return "PACKET_UNIT_HELP_BUILD_WONDER";

  case PACKET_UNIT_GOTO:
    return "PACKET_UNIT_GOTO";

  case PACKET_UNIT_ORDERS:
    return "PACKET_UNIT_ORDERS";

  case PACKET_UNIT_AUTO:
    return "PACKET_UNIT_AUTO";

  case PACKET_UNIT_LOAD:
    return "PACKET_UNIT_LOAD";

  case PACKET_UNIT_UNLOAD:
    return "PACKET_UNIT_UNLOAD";

  case PACKET_UNIT_UPGRADE:
    return "PACKET_UNIT_UPGRADE";

  case PACKET_UNIT_NUKE:
    return "PACKET_UNIT_NUKE";

  case PACKET_UNIT_PARADROP_TO:
    return "PACKET_UNIT_PARADROP_TO";

  case PACKET_UNIT_AIRLIFT:
    return "PACKET_UNIT_AIRLIFT";

  case PACKET_UNIT_BRIBE_INQ:
    return "PACKET_UNIT_BRIBE_INQ";

  case PACKET_UNIT_BRIBE_INFO:
    return "PACKET_UNIT_BRIBE_INFO";

  case PACKET_UNIT_TYPE_UPGRADE:
    return "PACKET_UNIT_TYPE_UPGRADE";

  case PACKET_UNIT_DIPLOMAT_ACTION:
    return "PACKET_UNIT_DIPLOMAT_ACTION";

  case PACKET_UNIT_DIPLOMAT_POPUP_DIALOG:
    return "PACKET_UNIT_DIPLOMAT_POPUP_DIALOG";

  case PACKET_UNIT_CHANGE_ACTIVITY:
    return "PACKET_UNIT_CHANGE_ACTIVITY";

  case PACKET_DIPLOMACY_INIT_MEETING_REQ:
    return "PACKET_DIPLOMACY_INIT_MEETING_REQ";

  case PACKET_DIPLOMACY_INIT_MEETING:
    return "PACKET_DIPLOMACY_INIT_MEETING";

  case PACKET_DIPLOMACY_CANCEL_MEETING_REQ:
    return "PACKET_DIPLOMACY_CANCEL_MEETING_REQ";

  case PACKET_DIPLOMACY_CANCEL_MEETING:
    return "PACKET_DIPLOMACY_CANCEL_MEETING";

  case PACKET_DIPLOMACY_CREATE_CLAUSE_REQ:
    return "PACKET_DIPLOMACY_CREATE_CLAUSE_REQ";

  case PACKET_DIPLOMACY_CREATE_CLAUSE:
    return "PACKET_DIPLOMACY_CREATE_CLAUSE";

  case PACKET_DIPLOMACY_REMOVE_CLAUSE_REQ:
    return "PACKET_DIPLOMACY_REMOVE_CLAUSE_REQ";

  case PACKET_DIPLOMACY_REMOVE_CLAUSE:
    return "PACKET_DIPLOMACY_REMOVE_CLAUSE";

  case PACKET_DIPLOMACY_ACCEPT_TREATY_REQ:
    return "PACKET_DIPLOMACY_ACCEPT_TREATY_REQ";

  case PACKET_DIPLOMACY_ACCEPT_TREATY:
    return "PACKET_DIPLOMACY_ACCEPT_TREATY";

  case PACKET_DIPLOMACY_CANCEL_PACT:
    return "PACKET_DIPLOMACY_CANCEL_PACT";

  case PACKET_PAGE_MSG:
    return "PACKET_PAGE_MSG";

  case PACKET_REPORT_REQ:
    return "PACKET_REPORT_REQ";

  case PACKET_CONN_INFO:
    return "PACKET_CONN_INFO";

  case PACKET_CONN_PING_INFO:
    return "PACKET_CONN_PING_INFO";

  case PACKET_CONN_PING:
    return "PACKET_CONN_PING";

  case PACKET_CONN_PONG:
    return "PACKET_CONN_PONG";

  case PACKET_BEFORE_NEW_YEAR:
    return "PACKET_BEFORE_NEW_YEAR";

  case PACKET_START_TURN:
    return "PACKET_START_TURN";

  case PACKET_NEW_YEAR:
    return "PACKET_NEW_YEAR";

  case PACKET_FREEZE_CLIENT:
    return "PACKET_FREEZE_CLIENT";

  case PACKET_THAW_CLIENT:
    return "PACKET_THAW_CLIENT";

  case PACKET_SPACESHIP_LAUNCH:
    return "PACKET_SPACESHIP_LAUNCH";

  case PACKET_SPACESHIP_PLACE:
    return "PACKET_SPACESHIP_PLACE";

  case PACKET_SPACESHIP_INFO:
    return "PACKET_SPACESHIP_INFO";

  case PACKET_RULESET_UNIT:
    return "PACKET_RULESET_UNIT";

  case PACKET_RULESET_GAME:
    return "PACKET_RULESET_GAME";

  case PACKET_RULESET_GOVERNMENT_RULER_TITLE:
    return "PACKET_RULESET_GOVERNMENT_RULER_TITLE";

  case PACKET_RULESET_TECH:
    return "PACKET_RULESET_TECH";

  case PACKET_RULESET_GOVERNMENT:
    return "PACKET_RULESET_GOVERNMENT";

  case PACKET_RULESET_TERRAIN_CONTROL:
    return "PACKET_RULESET_TERRAIN_CONTROL";

  case PACKET_RULESET_NATION:
    return "PACKET_RULESET_NATION";

  case PACKET_RULESET_CITY:
    return "PACKET_RULESET_CITY";

  case PACKET_RULESET_BUILDING:
    return "PACKET_RULESET_BUILDING";

  case PACKET_RULESET_TERRAIN:
    return "PACKET_RULESET_TERRAIN";

  case PACKET_RULESET_CONTROL:
    return "PACKET_RULESET_CONTROL";

  case PACKET_SINGLE_WANT_HACK_REQ:
    return "PACKET_SINGLE_WANT_HACK_REQ";

  case PACKET_SINGLE_WANT_HACK_REPLY:
    return "PACKET_SINGLE_WANT_HACK_REPLY";

  case PACKET_GAME_LOAD:
    return "PACKET_GAME_LOAD";

  case PACKET_OPTIONS_SETTABLE_CONTROL:
    return "PACKET_OPTIONS_SETTABLE_CONTROL";

  case PACKET_OPTIONS_SETTABLE:
    return "PACKET_OPTIONS_SETTABLE";

  case PACKET_RULESET_CACHE_GROUP:
    return "PACKET_RULESET_CACHE_GROUP";

  case PACKET_RULESET_CACHE_EFFECT:
    return "PACKET_RULESET_CACHE_EFFECT";

  default:
    return "unknown";
  }
}

static struct packet_processing_started *receive_packet_processing_started_100(struct connection *pc, enum packet_type type)
{
  RECEIVE_PACKET_START(packet_processing_started, real_packet);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_processing_started_100(struct connection *pc)
{
  SEND_PACKET_START(PACKET_PROCESSING_STARTED);
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_processing_started(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_PROCESSING_STARTED] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_PROCESSING_STARTED] = variant;
}

struct packet_processing_started *receive_packet_processing_started(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Receiving packet_processing_started at the server.");
  }
  ensure_valid_variant_packet_processing_started(pc);

  switch(pc->phs.variant[PACKET_PROCESSING_STARTED]) {
    case 100: return receive_packet_processing_started_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_processing_started(struct connection *pc)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Sending packet_processing_started from the client.");
  }
  ensure_valid_variant_packet_processing_started(pc);

  switch(pc->phs.variant[PACKET_PROCESSING_STARTED]) {
    case 100: return send_packet_processing_started_100(pc);
    default: die("unknown variant"); return -1;
  }
}

static struct packet_processing_finished *receive_packet_processing_finished_100(struct connection *pc, enum packet_type type)
{
  RECEIVE_PACKET_START(packet_processing_finished, real_packet);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_processing_finished_100(struct connection *pc)
{
  SEND_PACKET_START(PACKET_PROCESSING_FINISHED);
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_processing_finished(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_PROCESSING_FINISHED] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_PROCESSING_FINISHED] = variant;
}

struct packet_processing_finished *receive_packet_processing_finished(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Receiving packet_processing_finished at the server.");
  }
  ensure_valid_variant_packet_processing_finished(pc);

  switch(pc->phs.variant[PACKET_PROCESSING_FINISHED]) {
    case 100: return receive_packet_processing_finished_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_processing_finished(struct connection *pc)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Sending packet_processing_finished from the client.");
  }
  ensure_valid_variant_packet_processing_finished(pc);

  switch(pc->phs.variant[PACKET_PROCESSING_FINISHED]) {
    case 100: return send_packet_processing_finished_100(pc);
    default: die("unknown variant"); return -1;
  }
}

static struct packet_freeze_hint *receive_packet_freeze_hint_100(struct connection *pc, enum packet_type type)
{
  RECEIVE_PACKET_START(packet_freeze_hint, real_packet);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_freeze_hint_100(struct connection *pc)
{
  SEND_PACKET_START(PACKET_FREEZE_HINT);
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_freeze_hint(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_FREEZE_HINT] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_FREEZE_HINT] = variant;
}

struct packet_freeze_hint *receive_packet_freeze_hint(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Receiving packet_freeze_hint at the server.");
  }
  ensure_valid_variant_packet_freeze_hint(pc);

  switch(pc->phs.variant[PACKET_FREEZE_HINT]) {
    case 100: return receive_packet_freeze_hint_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_freeze_hint(struct connection *pc)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Sending packet_freeze_hint from the client.");
  }
  ensure_valid_variant_packet_freeze_hint(pc);

  switch(pc->phs.variant[PACKET_FREEZE_HINT]) {
    case 100: return send_packet_freeze_hint_100(pc);
    default: die("unknown variant"); return -1;
  }
}

void lsend_packet_freeze_hint(struct conn_list *dest)
{
  conn_list_iterate(*dest, pconn) {
    send_packet_freeze_hint(pconn);
  } conn_list_iterate_end;
}

static struct packet_thaw_hint *receive_packet_thaw_hint_100(struct connection *pc, enum packet_type type)
{
  RECEIVE_PACKET_START(packet_thaw_hint, real_packet);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_thaw_hint_100(struct connection *pc)
{
  SEND_PACKET_START(PACKET_THAW_HINT);
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_thaw_hint(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_THAW_HINT] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_THAW_HINT] = variant;
}

struct packet_thaw_hint *receive_packet_thaw_hint(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Receiving packet_thaw_hint at the server.");
  }
  ensure_valid_variant_packet_thaw_hint(pc);

  switch(pc->phs.variant[PACKET_THAW_HINT]) {
    case 100: return receive_packet_thaw_hint_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_thaw_hint(struct connection *pc)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Sending packet_thaw_hint from the client.");
  }
  ensure_valid_variant_packet_thaw_hint(pc);

  switch(pc->phs.variant[PACKET_THAW_HINT]) {
    case 100: return send_packet_thaw_hint_100(pc);
    default: die("unknown variant"); return -1;
  }
}

void lsend_packet_thaw_hint(struct conn_list *dest)
{
  conn_list_iterate(*dest, pconn) {
    send_packet_thaw_hint(pconn);
  } conn_list_iterate_end;
}

static struct packet_server_join_req *receive_packet_server_join_req_100(struct connection *pc, enum packet_type type)
{
  RECEIVE_PACKET_START(packet_server_join_req, real_packet);
  dio_get_string(&din, real_packet->username, sizeof(real_packet->username));
  dio_get_string(&din, real_packet->capability, sizeof(real_packet->capability));
  dio_get_string(&din, real_packet->version_label, sizeof(real_packet->version_label));
  {
    int readin;
  
    dio_get_uint32(&din, &readin);
    real_packet->major_version = readin;
  }
  {
    int readin;
  
    dio_get_uint32(&din, &readin);
    real_packet->minor_version = readin;
  }
  {
    int readin;
  
    dio_get_uint32(&din, &readin);
    real_packet->patch_version = readin;
  }

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_server_join_req_100(struct connection *pc, const struct packet_server_join_req *packet)
{
  const struct packet_server_join_req *real_packet = packet;
  SEND_PACKET_START(PACKET_SERVER_JOIN_REQ);

  dio_put_string(&dout, real_packet->username);
  dio_put_string(&dout, real_packet->capability);
  dio_put_string(&dout, real_packet->version_label);
  dio_put_uint32(&dout, real_packet->major_version);
  dio_put_uint32(&dout, real_packet->minor_version);
  dio_put_uint32(&dout, real_packet->patch_version);

  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_server_join_req(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_SERVER_JOIN_REQ] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_SERVER_JOIN_REQ] = variant;
}

struct packet_server_join_req *receive_packet_server_join_req(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Receiving packet_server_join_req at the client.");
  }
  ensure_valid_variant_packet_server_join_req(pc);

  switch(pc->phs.variant[PACKET_SERVER_JOIN_REQ]) {
    case 100: return receive_packet_server_join_req_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_server_join_req(struct connection *pc, const struct packet_server_join_req *packet)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Sending packet_server_join_req from the server.");
  }
  ensure_valid_variant_packet_server_join_req(pc);

  switch(pc->phs.variant[PACKET_SERVER_JOIN_REQ]) {
    case 100: return send_packet_server_join_req_100(pc, packet);
    default: die("unknown variant"); return -1;
  }
}

int dsend_packet_server_join_req(struct connection *pc, const char *username, const char *capability, const char *version_label, int major_version, int minor_version, int patch_version)
{
  struct packet_server_join_req packet, *real_packet = &packet;

  sz_strlcpy(real_packet->username, username);
  sz_strlcpy(real_packet->capability, capability);
  sz_strlcpy(real_packet->version_label, version_label);
  real_packet->major_version = major_version;
  real_packet->minor_version = minor_version;
  real_packet->patch_version = patch_version;
  
  return send_packet_server_join_req(pc, real_packet);
}

static struct packet_server_join_reply *receive_packet_server_join_reply_100(struct connection *pc, enum packet_type type)
{
  RECEIVE_PACKET_START(packet_server_join_reply, real_packet);
  dio_get_bool8(&din, &real_packet->you_can_join);
  dio_get_string(&din, real_packet->message, sizeof(real_packet->message));
  dio_get_string(&din, real_packet->capability, sizeof(real_packet->capability));
  dio_get_string(&din, real_packet->challenge_file, sizeof(real_packet->challenge_file));
  {
    int readin;
  
    dio_get_uint8(&din, &readin);
    real_packet->conn_id = readin;
  }

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_server_join_reply_100(struct connection *pc, const struct packet_server_join_reply *packet)
{
  const struct packet_server_join_reply *real_packet = packet;
  SEND_PACKET_START(PACKET_SERVER_JOIN_REPLY);

  dio_put_bool8(&dout, real_packet->you_can_join);
  dio_put_string(&dout, real_packet->message);
  dio_put_string(&dout, real_packet->capability);
  dio_put_string(&dout, real_packet->challenge_file);
  dio_put_uint8(&dout, real_packet->conn_id);

  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_server_join_reply(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_SERVER_JOIN_REPLY] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_SERVER_JOIN_REPLY] = variant;
}

struct packet_server_join_reply *receive_packet_server_join_reply(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Receiving packet_server_join_reply at the server.");
  }
  ensure_valid_variant_packet_server_join_reply(pc);

  switch(pc->phs.variant[PACKET_SERVER_JOIN_REPLY]) {
    case 100: return receive_packet_server_join_reply_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_server_join_reply(struct connection *pc, const struct packet_server_join_reply *packet)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Sending packet_server_join_reply from the client.");
  }
  ensure_valid_variant_packet_server_join_reply(pc);

  switch(pc->phs.variant[PACKET_SERVER_JOIN_REPLY]) {
    case 100: return send_packet_server_join_reply_100(pc, packet);
    default: die("unknown variant"); return -1;
  }
}

#define hash_packet_authentication_req_100 hash_const

#define cmp_packet_authentication_req_100 cmp_const

BV_DEFINE(packet_authentication_req_100_fields, 2);

static struct packet_authentication_req *receive_packet_authentication_req_100(struct connection *pc, enum packet_type type)
{
  packet_authentication_req_100_fields fields;
  struct packet_authentication_req *old;
  struct hash_table **hash = &pc->phs.received[type];
  struct packet_authentication_req *clone;
  RECEIVE_PACKET_START(packet_authentication_req, real_packet);

  DIO_BV_GET(&din, fields);


  if (!*hash) {
    *hash = hash_new(hash_packet_authentication_req_100, cmp_packet_authentication_req_100);
  }
  old = hash_delete_entry(*hash, real_packet);

  if (old) {
    *real_packet = *old;
  } else {
    memset(real_packet, 0, sizeof(*real_packet));
  }

  if (BV_ISSET(fields, 0)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->type = readin;
    }
  }
  if (BV_ISSET(fields, 1)) {
    dio_get_string(&din, real_packet->message, sizeof(real_packet->message));
  }

  clone = fc_malloc(sizeof(*clone));
  *clone = *real_packet;
  if (old) {
    free(old);
  }
  hash_insert(*hash, clone, clone);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_authentication_req_100(struct connection *pc, const struct packet_authentication_req *packet)
{
  const struct packet_authentication_req *real_packet = packet;
  packet_authentication_req_100_fields fields;
  struct packet_authentication_req *old, *clone;
  bool differ, old_from_hash, force_send_of_unchanged = TRUE;
  struct hash_table **hash = &pc->phs.sent[PACKET_AUTHENTICATION_REQ];
  int different = 0;
  SEND_PACKET_START(PACKET_AUTHENTICATION_REQ);

  if (!*hash) {
    *hash = hash_new(hash_packet_authentication_req_100, cmp_packet_authentication_req_100);
  }
  BV_CLR_ALL(fields);

  old = hash_lookup_data(*hash, real_packet);
  old_from_hash = (old != NULL);
  if (!old) {
    old = fc_malloc(sizeof(*old));
    memset(old, 0, sizeof(*old));
    force_send_of_unchanged = TRUE;
  }

  differ = (old->type != real_packet->type);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 0);}

  differ = (strcmp(old->message, real_packet->message) != 0);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 1);}

  if (different == 0 && !force_send_of_unchanged) {
    return 0;
  }

  DIO_BV_PUT(&dout, fields);

  if (BV_ISSET(fields, 0)) {
    dio_put_uint8(&dout, real_packet->type);
  }
  if (BV_ISSET(fields, 1)) {
    dio_put_string(&dout, real_packet->message);
  }


  if (old_from_hash) {
    hash_delete_entry(*hash, old);
  }

  clone = old;

  *clone = *real_packet;
  hash_insert(*hash, clone, clone);
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_authentication_req(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_AUTHENTICATION_REQ] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_AUTHENTICATION_REQ] = variant;
}

struct packet_authentication_req *receive_packet_authentication_req(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Receiving packet_authentication_req at the server.");
  }
  ensure_valid_variant_packet_authentication_req(pc);

  switch(pc->phs.variant[PACKET_AUTHENTICATION_REQ]) {
    case 100: return receive_packet_authentication_req_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_authentication_req(struct connection *pc, const struct packet_authentication_req *packet)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Sending packet_authentication_req from the client.");
  }
  ensure_valid_variant_packet_authentication_req(pc);

  switch(pc->phs.variant[PACKET_AUTHENTICATION_REQ]) {
    case 100: return send_packet_authentication_req_100(pc, packet);
    default: die("unknown variant"); return -1;
  }
}

int dsend_packet_authentication_req(struct connection *pc, enum authentication_type type, const char *message)
{
  struct packet_authentication_req packet, *real_packet = &packet;

  real_packet->type = type;
  sz_strlcpy(real_packet->message, message);
  
  return send_packet_authentication_req(pc, real_packet);
}

#define hash_packet_authentication_reply_100 hash_const

#define cmp_packet_authentication_reply_100 cmp_const

BV_DEFINE(packet_authentication_reply_100_fields, 1);

static struct packet_authentication_reply *receive_packet_authentication_reply_100(struct connection *pc, enum packet_type type)
{
  packet_authentication_reply_100_fields fields;
  struct packet_authentication_reply *old;
  struct hash_table **hash = &pc->phs.received[type];
  struct packet_authentication_reply *clone;
  RECEIVE_PACKET_START(packet_authentication_reply, real_packet);

  DIO_BV_GET(&din, fields);


  if (!*hash) {
    *hash = hash_new(hash_packet_authentication_reply_100, cmp_packet_authentication_reply_100);
  }
  old = hash_delete_entry(*hash, real_packet);

  if (old) {
    *real_packet = *old;
  } else {
    memset(real_packet, 0, sizeof(*real_packet));
  }

  if (BV_ISSET(fields, 0)) {
    dio_get_string(&din, real_packet->password, sizeof(real_packet->password));
  }

  clone = fc_malloc(sizeof(*clone));
  *clone = *real_packet;
  if (old) {
    free(old);
  }
  hash_insert(*hash, clone, clone);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_authentication_reply_100(struct connection *pc, const struct packet_authentication_reply *packet)
{
  const struct packet_authentication_reply *real_packet = packet;
  packet_authentication_reply_100_fields fields;
  struct packet_authentication_reply *old, *clone;
  bool differ, old_from_hash, force_send_of_unchanged = TRUE;
  struct hash_table **hash = &pc->phs.sent[PACKET_AUTHENTICATION_REPLY];
  int different = 0;
  SEND_PACKET_START(PACKET_AUTHENTICATION_REPLY);

  if (!*hash) {
    *hash = hash_new(hash_packet_authentication_reply_100, cmp_packet_authentication_reply_100);
  }
  BV_CLR_ALL(fields);

  old = hash_lookup_data(*hash, real_packet);
  old_from_hash = (old != NULL);
  if (!old) {
    old = fc_malloc(sizeof(*old));
    memset(old, 0, sizeof(*old));
    force_send_of_unchanged = TRUE;
  }

  differ = (strcmp(old->password, real_packet->password) != 0);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 0);}

  if (different == 0 && !force_send_of_unchanged) {
    return 0;
  }

  DIO_BV_PUT(&dout, fields);

  if (BV_ISSET(fields, 0)) {
    dio_put_string(&dout, real_packet->password);
  }


  if (old_from_hash) {
    hash_delete_entry(*hash, old);
  }

  clone = old;

  *clone = *real_packet;
  hash_insert(*hash, clone, clone);
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_authentication_reply(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_AUTHENTICATION_REPLY] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_AUTHENTICATION_REPLY] = variant;
}

struct packet_authentication_reply *receive_packet_authentication_reply(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Receiving packet_authentication_reply at the client.");
  }
  ensure_valid_variant_packet_authentication_reply(pc);

  switch(pc->phs.variant[PACKET_AUTHENTICATION_REPLY]) {
    case 100: return receive_packet_authentication_reply_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_authentication_reply(struct connection *pc, const struct packet_authentication_reply *packet)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Sending packet_authentication_reply from the server.");
  }
  ensure_valid_variant_packet_authentication_reply(pc);

  switch(pc->phs.variant[PACKET_AUTHENTICATION_REPLY]) {
    case 100: return send_packet_authentication_reply_100(pc, packet);
    default: die("unknown variant"); return -1;
  }
}

static struct packet_server_shutdown *receive_packet_server_shutdown_100(struct connection *pc, enum packet_type type)
{
  RECEIVE_PACKET_START(packet_server_shutdown, real_packet);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_server_shutdown_100(struct connection *pc)
{
  SEND_PACKET_START(PACKET_SERVER_SHUTDOWN);
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_server_shutdown(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_SERVER_SHUTDOWN] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_SERVER_SHUTDOWN] = variant;
}

struct packet_server_shutdown *receive_packet_server_shutdown(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Receiving packet_server_shutdown at the server.");
  }
  ensure_valid_variant_packet_server_shutdown(pc);

  switch(pc->phs.variant[PACKET_SERVER_SHUTDOWN]) {
    case 100: return receive_packet_server_shutdown_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_server_shutdown(struct connection *pc)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Sending packet_server_shutdown from the client.");
  }
  ensure_valid_variant_packet_server_shutdown(pc);

  switch(pc->phs.variant[PACKET_SERVER_SHUTDOWN]) {
    case 100: return send_packet_server_shutdown_100(pc);
    default: die("unknown variant"); return -1;
  }
}

void lsend_packet_server_shutdown(struct conn_list *dest)
{
  conn_list_iterate(*dest, pconn) {
    send_packet_server_shutdown(pconn);
  } conn_list_iterate_end;
}

#define hash_packet_nation_unavailable_100 hash_const

#define cmp_packet_nation_unavailable_100 cmp_const

BV_DEFINE(packet_nation_unavailable_100_fields, 1);

static struct packet_nation_unavailable *receive_packet_nation_unavailable_100(struct connection *pc, enum packet_type type)
{
  packet_nation_unavailable_100_fields fields;
  struct packet_nation_unavailable *old;
  struct hash_table **hash = &pc->phs.received[type];
  struct packet_nation_unavailable *clone;
  RECEIVE_PACKET_START(packet_nation_unavailable, real_packet);

  DIO_BV_GET(&din, fields);


  if (!*hash) {
    *hash = hash_new(hash_packet_nation_unavailable_100, cmp_packet_nation_unavailable_100);
  }
  old = hash_delete_entry(*hash, real_packet);

  if (old) {
    *real_packet = *old;
  } else {
    memset(real_packet, 0, sizeof(*real_packet));
  }

  if (BV_ISSET(fields, 0)) {
    {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->nation = readin;
    }
  }

  clone = fc_malloc(sizeof(*clone));
  *clone = *real_packet;
  if (old) {
    free(old);
  }
  hash_insert(*hash, clone, clone);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_nation_unavailable_100(struct connection *pc, const struct packet_nation_unavailable *packet)
{
  const struct packet_nation_unavailable *real_packet = packet;
  packet_nation_unavailable_100_fields fields;
  struct packet_nation_unavailable *old, *clone;
  bool differ, old_from_hash, force_send_of_unchanged = TRUE;
  struct hash_table **hash = &pc->phs.sent[PACKET_NATION_UNAVAILABLE];
  int different = 0;
  SEND_PACKET_START(PACKET_NATION_UNAVAILABLE);

  if (!*hash) {
    *hash = hash_new(hash_packet_nation_unavailable_100, cmp_packet_nation_unavailable_100);
  }
  BV_CLR_ALL(fields);

  old = hash_lookup_data(*hash, real_packet);
  old_from_hash = (old != NULL);
  if (!old) {
    old = fc_malloc(sizeof(*old));
    memset(old, 0, sizeof(*old));
    force_send_of_unchanged = TRUE;
  }

  differ = (old->nation != real_packet->nation);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 0);}

  if (different == 0 && !force_send_of_unchanged) {
    return 0;
  }

  DIO_BV_PUT(&dout, fields);

  if (BV_ISSET(fields, 0)) {
    dio_put_uint16(&dout, real_packet->nation);
  }


  if (old_from_hash) {
    hash_delete_entry(*hash, old);
  }

  clone = old;

  *clone = *real_packet;
  hash_insert(*hash, clone, clone);
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_nation_unavailable(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_NATION_UNAVAILABLE] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_NATION_UNAVAILABLE] = variant;
}

struct packet_nation_unavailable *receive_packet_nation_unavailable(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Receiving packet_nation_unavailable at the server.");
  }
  ensure_valid_variant_packet_nation_unavailable(pc);

  switch(pc->phs.variant[PACKET_NATION_UNAVAILABLE]) {
    case 100: return receive_packet_nation_unavailable_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_nation_unavailable(struct connection *pc, const struct packet_nation_unavailable *packet)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Sending packet_nation_unavailable from the client.");
  }
  ensure_valid_variant_packet_nation_unavailable(pc);

  switch(pc->phs.variant[PACKET_NATION_UNAVAILABLE]) {
    case 100: return send_packet_nation_unavailable_100(pc, packet);
    default: die("unknown variant"); return -1;
  }
}

void lsend_packet_nation_unavailable(struct conn_list *dest, const struct packet_nation_unavailable *packet)
{
  conn_list_iterate(*dest, pconn) {
    send_packet_nation_unavailable(pconn, packet);
  } conn_list_iterate_end;
}

static struct packet_select_races *receive_packet_select_races_100(struct connection *pc, enum packet_type type)
{
  RECEIVE_PACKET_START(packet_select_races, real_packet);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_select_races_100(struct connection *pc)
{
  SEND_PACKET_START(PACKET_SELECT_RACES);
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_select_races(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_SELECT_RACES] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_SELECT_RACES] = variant;
}

struct packet_select_races *receive_packet_select_races(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Receiving packet_select_races at the server.");
  }
  ensure_valid_variant_packet_select_races(pc);

  switch(pc->phs.variant[PACKET_SELECT_RACES]) {
    case 100: return receive_packet_select_races_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_select_races(struct connection *pc)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Sending packet_select_races from the client.");
  }
  ensure_valid_variant_packet_select_races(pc);

  switch(pc->phs.variant[PACKET_SELECT_RACES]) {
    case 100: return send_packet_select_races_100(pc);
    default: die("unknown variant"); return -1;
  }
}

void lsend_packet_select_races(struct conn_list *dest)
{
  conn_list_iterate(*dest, pconn) {
    send_packet_select_races(pconn);
  } conn_list_iterate_end;
}

#define hash_packet_nation_select_req_100 hash_const

#define cmp_packet_nation_select_req_100 cmp_const

BV_DEFINE(packet_nation_select_req_100_fields, 4);

static struct packet_nation_select_req *receive_packet_nation_select_req_100(struct connection *pc, enum packet_type type)
{
  packet_nation_select_req_100_fields fields;
  struct packet_nation_select_req *old;
  struct hash_table **hash = &pc->phs.received[type];
  struct packet_nation_select_req *clone;
  RECEIVE_PACKET_START(packet_nation_select_req, real_packet);

  DIO_BV_GET(&din, fields);


  if (!*hash) {
    *hash = hash_new(hash_packet_nation_select_req_100, cmp_packet_nation_select_req_100);
  }
  old = hash_delete_entry(*hash, real_packet);

  if (old) {
    *real_packet = *old;
  } else {
    memset(real_packet, 0, sizeof(*real_packet));
  }

  if (BV_ISSET(fields, 0)) {
    {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->nation_no = readin;
    }
  }
  real_packet->is_male = BV_ISSET(fields, 1);
  if (BV_ISSET(fields, 2)) {
    dio_get_string(&din, real_packet->name, sizeof(real_packet->name));
  }
  if (BV_ISSET(fields, 3)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->city_style = readin;
    }
  }

  clone = fc_malloc(sizeof(*clone));
  *clone = *real_packet;
  if (old) {
    free(old);
  }
  hash_insert(*hash, clone, clone);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_nation_select_req_100(struct connection *pc, const struct packet_nation_select_req *packet)
{
  const struct packet_nation_select_req *real_packet = packet;
  packet_nation_select_req_100_fields fields;
  struct packet_nation_select_req *old, *clone;
  bool differ, old_from_hash, force_send_of_unchanged = TRUE;
  struct hash_table **hash = &pc->phs.sent[PACKET_NATION_SELECT_REQ];
  int different = 0;
  SEND_PACKET_START(PACKET_NATION_SELECT_REQ);

  if (!*hash) {
    *hash = hash_new(hash_packet_nation_select_req_100, cmp_packet_nation_select_req_100);
  }
  BV_CLR_ALL(fields);

  old = hash_lookup_data(*hash, real_packet);
  old_from_hash = (old != NULL);
  if (!old) {
    old = fc_malloc(sizeof(*old));
    memset(old, 0, sizeof(*old));
    force_send_of_unchanged = TRUE;
  }

  differ = (old->nation_no != real_packet->nation_no);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 0);}

  differ = (old->is_male != real_packet->is_male);
  if(differ) {different++;}
  if(packet->is_male) {BV_SET(fields, 1);}

  differ = (strcmp(old->name, real_packet->name) != 0);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 2);}

  differ = (old->city_style != real_packet->city_style);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 3);}

  if (different == 0 && !force_send_of_unchanged) {
    return 0;
  }

  DIO_BV_PUT(&dout, fields);

  if (BV_ISSET(fields, 0)) {
    dio_put_uint16(&dout, real_packet->nation_no);
  }
  /* field 1 is folded into the header */
  if (BV_ISSET(fields, 2)) {
    dio_put_string(&dout, real_packet->name);
  }
  if (BV_ISSET(fields, 3)) {
    dio_put_uint8(&dout, real_packet->city_style);
  }


  if (old_from_hash) {
    hash_delete_entry(*hash, old);
  }

  clone = old;

  *clone = *real_packet;
  hash_insert(*hash, clone, clone);
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_nation_select_req(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_NATION_SELECT_REQ] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_NATION_SELECT_REQ] = variant;
}

struct packet_nation_select_req *receive_packet_nation_select_req(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Receiving packet_nation_select_req at the client.");
  }
  ensure_valid_variant_packet_nation_select_req(pc);

  switch(pc->phs.variant[PACKET_NATION_SELECT_REQ]) {
    case 100: return receive_packet_nation_select_req_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_nation_select_req(struct connection *pc, const struct packet_nation_select_req *packet)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Sending packet_nation_select_req from the server.");
  }
  ensure_valid_variant_packet_nation_select_req(pc);

  switch(pc->phs.variant[PACKET_NATION_SELECT_REQ]) {
    case 100: return send_packet_nation_select_req_100(pc, packet);
    default: die("unknown variant"); return -1;
  }
}

int dsend_packet_nation_select_req(struct connection *pc, Nation_Type_id nation_no, bool is_male, const char *name, int city_style)
{
  struct packet_nation_select_req packet, *real_packet = &packet;

  real_packet->nation_no = nation_no;
  real_packet->is_male = is_male;
  sz_strlcpy(real_packet->name, name);
  real_packet->city_style = city_style;
  
  return send_packet_nation_select_req(pc, real_packet);
}

static struct packet_nation_select_ok *receive_packet_nation_select_ok_100(struct connection *pc, enum packet_type type)
{
  RECEIVE_PACKET_START(packet_nation_select_ok, real_packet);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_nation_select_ok_100(struct connection *pc)
{
  SEND_PACKET_START(PACKET_NATION_SELECT_OK);
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_nation_select_ok(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_NATION_SELECT_OK] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_NATION_SELECT_OK] = variant;
}

struct packet_nation_select_ok *receive_packet_nation_select_ok(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Receiving packet_nation_select_ok at the server.");
  }
  ensure_valid_variant_packet_nation_select_ok(pc);

  switch(pc->phs.variant[PACKET_NATION_SELECT_OK]) {
    case 100: return receive_packet_nation_select_ok_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_nation_select_ok(struct connection *pc)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Sending packet_nation_select_ok from the client.");
  }
  ensure_valid_variant_packet_nation_select_ok(pc);

  switch(pc->phs.variant[PACKET_NATION_SELECT_OK]) {
    case 100: return send_packet_nation_select_ok_100(pc);
    default: die("unknown variant"); return -1;
  }
}

void lsend_packet_nation_select_ok(struct conn_list *dest)
{
  conn_list_iterate(*dest, pconn) {
    send_packet_nation_select_ok(pconn);
  } conn_list_iterate_end;
}

#define hash_packet_game_state_100 hash_const

#define cmp_packet_game_state_100 cmp_const

BV_DEFINE(packet_game_state_100_fields, 1);

static struct packet_game_state *receive_packet_game_state_100(struct connection *pc, enum packet_type type)
{
  packet_game_state_100_fields fields;
  struct packet_game_state *old;
  struct hash_table **hash = &pc->phs.received[type];
  struct packet_game_state *clone;
  RECEIVE_PACKET_START(packet_game_state, real_packet);

  DIO_BV_GET(&din, fields);


  if (!*hash) {
    *hash = hash_new(hash_packet_game_state_100, cmp_packet_game_state_100);
  }
  old = hash_delete_entry(*hash, real_packet);

  if (old) {
    *real_packet = *old;
  } else {
    memset(real_packet, 0, sizeof(*real_packet));
  }

  if (BV_ISSET(fields, 0)) {
    {
      int readin;
    
      dio_get_uint32(&din, &readin);
      real_packet->value = readin;
    }
  }

  clone = fc_malloc(sizeof(*clone));
  *clone = *real_packet;
  if (old) {
    free(old);
  }
  hash_insert(*hash, clone, clone);

  post_receive_packet_game_state(pc, real_packet);
  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_game_state_100(struct connection *pc, const struct packet_game_state *packet)
{
  const struct packet_game_state *real_packet = packet;
  packet_game_state_100_fields fields;
  struct packet_game_state *old, *clone;
  bool differ, old_from_hash, force_send_of_unchanged = TRUE;
  struct hash_table **hash = &pc->phs.sent[PACKET_GAME_STATE];
  int different = 0;
  SEND_PACKET_START(PACKET_GAME_STATE);

  if (!*hash) {
    *hash = hash_new(hash_packet_game_state_100, cmp_packet_game_state_100);
  }
  BV_CLR_ALL(fields);

  old = hash_lookup_data(*hash, real_packet);
  old_from_hash = (old != NULL);
  if (!old) {
    old = fc_malloc(sizeof(*old));
    memset(old, 0, sizeof(*old));
    force_send_of_unchanged = TRUE;
  }

  differ = (old->value != real_packet->value);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 0);}

  if (different == 0 && !force_send_of_unchanged) {
    return 0;
  }

  DIO_BV_PUT(&dout, fields);

  if (BV_ISSET(fields, 0)) {
    dio_put_uint32(&dout, real_packet->value);
  }


  if (old_from_hash) {
    hash_delete_entry(*hash, old);
  }

  clone = old;

  *clone = *real_packet;
  hash_insert(*hash, clone, clone);
    post_send_packet_game_state(pc, real_packet);
SEND_PACKET_END;
}

static void ensure_valid_variant_packet_game_state(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_GAME_STATE] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_GAME_STATE] = variant;
}

struct packet_game_state *receive_packet_game_state(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Receiving packet_game_state at the server.");
  }
  ensure_valid_variant_packet_game_state(pc);

  switch(pc->phs.variant[PACKET_GAME_STATE]) {
    case 100: return receive_packet_game_state_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_game_state(struct connection *pc, const struct packet_game_state *packet)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Sending packet_game_state from the client.");
  }
  ensure_valid_variant_packet_game_state(pc);

  switch(pc->phs.variant[PACKET_GAME_STATE]) {
    case 100: return send_packet_game_state_100(pc, packet);
    default: die("unknown variant"); return -1;
  }
}

void lsend_packet_game_state(struct conn_list *dest, const struct packet_game_state *packet)
{
  conn_list_iterate(*dest, pconn) {
    send_packet_game_state(pconn, packet);
  } conn_list_iterate_end;
}

int dsend_packet_game_state(struct connection *pc, int value)
{
  struct packet_game_state packet, *real_packet = &packet;

  real_packet->value = value;
  
  return send_packet_game_state(pc, real_packet);
}

void dlsend_packet_game_state(struct conn_list *dest, int value)
{
  struct packet_game_state packet, *real_packet = &packet;

  real_packet->value = value;
  
  lsend_packet_game_state(dest, real_packet);
}

#define hash_packet_endgame_report_100 hash_const

#define cmp_packet_endgame_report_100 cmp_const

BV_DEFINE(packet_endgame_report_100_fields, 15);

static struct packet_endgame_report *receive_packet_endgame_report_100(struct connection *pc, enum packet_type type)
{
  packet_endgame_report_100_fields fields;
  struct packet_endgame_report *old;
  struct hash_table **hash = &pc->phs.received[type];
  struct packet_endgame_report *clone;
  RECEIVE_PACKET_START(packet_endgame_report, real_packet);

  DIO_BV_GET(&din, fields);


  if (!*hash) {
    *hash = hash_new(hash_packet_endgame_report_100, cmp_packet_endgame_report_100);
  }
  old = hash_delete_entry(*hash, real_packet);

  if (old) {
    *real_packet = *old;
  } else {
    memset(real_packet, 0, sizeof(*real_packet));
  }

  if (BV_ISSET(fields, 0)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->nscores = readin;
    }
  }
  if (BV_ISSET(fields, 1)) {
    
    {
      int i;
    
      if(real_packet->nscores > MAX_NUM_PLAYERS) {
        freelog(LOG_ERROR, "packets_gen.c: WARNING: truncation array");
        real_packet->nscores = MAX_NUM_PLAYERS;
      }
      for (i = 0; i < real_packet->nscores; i++) {
        {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->id[i] = readin;
    }
      }
    }
  }
  if (BV_ISSET(fields, 2)) {
    
    {
      int i;
    
      if(real_packet->nscores > MAX_NUM_PLAYERS) {
        freelog(LOG_ERROR, "packets_gen.c: WARNING: truncation array");
        real_packet->nscores = MAX_NUM_PLAYERS;
      }
      for (i = 0; i < real_packet->nscores; i++) {
        {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->score[i] = readin;
    }
      }
    }
  }
  if (BV_ISSET(fields, 3)) {
    
    {
      int i;
    
      if(real_packet->nscores > MAX_NUM_PLAYERS) {
        freelog(LOG_ERROR, "packets_gen.c: WARNING: truncation array");
        real_packet->nscores = MAX_NUM_PLAYERS;
      }
      for (i = 0; i < real_packet->nscores; i++) {
        {
      int readin;
    
      dio_get_uint32(&din, &readin);
      real_packet->pop[i] = readin;
    }
      }
    }
  }
  if (BV_ISSET(fields, 4)) {
    
    {
      int i;
    
      if(real_packet->nscores > MAX_NUM_PLAYERS) {
        freelog(LOG_ERROR, "packets_gen.c: WARNING: truncation array");
        real_packet->nscores = MAX_NUM_PLAYERS;
      }
      for (i = 0; i < real_packet->nscores; i++) {
        {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->bnp[i] = readin;
    }
      }
    }
  }
  if (BV_ISSET(fields, 5)) {
    
    {
      int i;
    
      if(real_packet->nscores > MAX_NUM_PLAYERS) {
        freelog(LOG_ERROR, "packets_gen.c: WARNING: truncation array");
        real_packet->nscores = MAX_NUM_PLAYERS;
      }
      for (i = 0; i < real_packet->nscores; i++) {
        {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->mfg[i] = readin;
    }
      }
    }
  }
  if (BV_ISSET(fields, 6)) {
    
    {
      int i;
    
      if(real_packet->nscores > MAX_NUM_PLAYERS) {
        freelog(LOG_ERROR, "packets_gen.c: WARNING: truncation array");
        real_packet->nscores = MAX_NUM_PLAYERS;
      }
      for (i = 0; i < real_packet->nscores; i++) {
        {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->cities[i] = readin;
    }
      }
    }
  }
  if (BV_ISSET(fields, 7)) {
    
    {
      int i;
    
      if(real_packet->nscores > MAX_NUM_PLAYERS) {
        freelog(LOG_ERROR, "packets_gen.c: WARNING: truncation array");
        real_packet->nscores = MAX_NUM_PLAYERS;
      }
      for (i = 0; i < real_packet->nscores; i++) {
        {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->techs[i] = readin;
    }
      }
    }
  }
  if (BV_ISSET(fields, 8)) {
    
    {
      int i;
    
      if(real_packet->nscores > MAX_NUM_PLAYERS) {
        freelog(LOG_ERROR, "packets_gen.c: WARNING: truncation array");
        real_packet->nscores = MAX_NUM_PLAYERS;
      }
      for (i = 0; i < real_packet->nscores; i++) {
        {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->mil_service[i] = readin;
    }
      }
    }
  }
  if (BV_ISSET(fields, 9)) {
    
    {
      int i;
    
      if(real_packet->nscores > MAX_NUM_PLAYERS) {
        freelog(LOG_ERROR, "packets_gen.c: WARNING: truncation array");
        real_packet->nscores = MAX_NUM_PLAYERS;
      }
      for (i = 0; i < real_packet->nscores; i++) {
        {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->wonders[i] = readin;
    }
      }
    }
  }
  if (BV_ISSET(fields, 10)) {
    
    {
      int i;
    
      if(real_packet->nscores > MAX_NUM_PLAYERS) {
        freelog(LOG_ERROR, "packets_gen.c: WARNING: truncation array");
        real_packet->nscores = MAX_NUM_PLAYERS;
      }
      for (i = 0; i < real_packet->nscores; i++) {
        {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->research[i] = readin;
    }
      }
    }
  }
  if (BV_ISSET(fields, 11)) {
    
    {
      int i;
    
      if(real_packet->nscores > MAX_NUM_PLAYERS) {
        freelog(LOG_ERROR, "packets_gen.c: WARNING: truncation array");
        real_packet->nscores = MAX_NUM_PLAYERS;
      }
      for (i = 0; i < real_packet->nscores; i++) {
        {
      int readin;
    
      dio_get_uint32(&din, &readin);
      real_packet->landarea[i] = readin;
    }
      }
    }
  }
  if (BV_ISSET(fields, 12)) {
    
    {
      int i;
    
      if(real_packet->nscores > MAX_NUM_PLAYERS) {
        freelog(LOG_ERROR, "packets_gen.c: WARNING: truncation array");
        real_packet->nscores = MAX_NUM_PLAYERS;
      }
      for (i = 0; i < real_packet->nscores; i++) {
        {
      int readin;
    
      dio_get_uint32(&din, &readin);
      real_packet->settledarea[i] = readin;
    }
      }
    }
  }
  if (BV_ISSET(fields, 13)) {
    
    {
      int i;
    
      if(real_packet->nscores > MAX_NUM_PLAYERS) {
        freelog(LOG_ERROR, "packets_gen.c: WARNING: truncation array");
        real_packet->nscores = MAX_NUM_PLAYERS;
      }
      for (i = 0; i < real_packet->nscores; i++) {
        {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->literacy[i] = readin;
    }
      }
    }
  }
  if (BV_ISSET(fields, 14)) {
    
    {
      int i;
    
      if(real_packet->nscores > MAX_NUM_PLAYERS) {
        freelog(LOG_ERROR, "packets_gen.c: WARNING: truncation array");
        real_packet->nscores = MAX_NUM_PLAYERS;
      }
      for (i = 0; i < real_packet->nscores; i++) {
        {
      int readin;
    
      dio_get_uint32(&din, &readin);
      real_packet->spaceship[i] = readin;
    }
      }
    }
  }

  clone = fc_malloc(sizeof(*clone));
  *clone = *real_packet;
  if (old) {
    free(old);
  }
  hash_insert(*hash, clone, clone);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_endgame_report_100(struct connection *pc, const struct packet_endgame_report *packet)
{
  const struct packet_endgame_report *real_packet = packet;
  packet_endgame_report_100_fields fields;
  struct packet_endgame_report *old, *clone;
  bool differ, old_from_hash, force_send_of_unchanged = TRUE;
  struct hash_table **hash = &pc->phs.sent[PACKET_ENDGAME_REPORT];
  int different = 0;
  SEND_PACKET_START(PACKET_ENDGAME_REPORT);

  if (!*hash) {
    *hash = hash_new(hash_packet_endgame_report_100, cmp_packet_endgame_report_100);
  }
  BV_CLR_ALL(fields);

  old = hash_lookup_data(*hash, real_packet);
  old_from_hash = (old != NULL);
  if (!old) {
    old = fc_malloc(sizeof(*old));
    memset(old, 0, sizeof(*old));
    force_send_of_unchanged = TRUE;
  }

  differ = (old->nscores != real_packet->nscores);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 0);}


    {
      differ = (old->nscores != real_packet->nscores);
      if(!differ) {
        int i;
        for (i = 0; i < real_packet->nscores; i++) {
          if (old->id[i] != real_packet->id[i]) {
            differ = TRUE;
            break;
          }
        }
      }
    }
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 1);}


    {
      differ = (old->nscores != real_packet->nscores);
      if(!differ) {
        int i;
        for (i = 0; i < real_packet->nscores; i++) {
          if (old->score[i] != real_packet->score[i]) {
            differ = TRUE;
            break;
          }
        }
      }
    }
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 2);}


    {
      differ = (old->nscores != real_packet->nscores);
      if(!differ) {
        int i;
        for (i = 0; i < real_packet->nscores; i++) {
          if (old->pop[i] != real_packet->pop[i]) {
            differ = TRUE;
            break;
          }
        }
      }
    }
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 3);}


    {
      differ = (old->nscores != real_packet->nscores);
      if(!differ) {
        int i;
        for (i = 0; i < real_packet->nscores; i++) {
          if (old->bnp[i] != real_packet->bnp[i]) {
            differ = TRUE;
            break;
          }
        }
      }
    }
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 4);}


    {
      differ = (old->nscores != real_packet->nscores);
      if(!differ) {
        int i;
        for (i = 0; i < real_packet->nscores; i++) {
          if (old->mfg[i] != real_packet->mfg[i]) {
            differ = TRUE;
            break;
          }
        }
      }
    }
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 5);}


    {
      differ = (old->nscores != real_packet->nscores);
      if(!differ) {
        int i;
        for (i = 0; i < real_packet->nscores; i++) {
          if (old->cities[i] != real_packet->cities[i]) {
            differ = TRUE;
            break;
          }
        }
      }
    }
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 6);}


    {
      differ = (old->nscores != real_packet->nscores);
      if(!differ) {
        int i;
        for (i = 0; i < real_packet->nscores; i++) {
          if (old->techs[i] != real_packet->techs[i]) {
            differ = TRUE;
            break;
          }
        }
      }
    }
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 7);}


    {
      differ = (old->nscores != real_packet->nscores);
      if(!differ) {
        int i;
        for (i = 0; i < real_packet->nscores; i++) {
          if (old->mil_service[i] != real_packet->mil_service[i]) {
            differ = TRUE;
            break;
          }
        }
      }
    }
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 8);}


    {
      differ = (old->nscores != real_packet->nscores);
      if(!differ) {
        int i;
        for (i = 0; i < real_packet->nscores; i++) {
          if (old->wonders[i] != real_packet->wonders[i]) {
            differ = TRUE;
            break;
          }
        }
      }
    }
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 9);}


    {
      differ = (old->nscores != real_packet->nscores);
      if(!differ) {
        int i;
        for (i = 0; i < real_packet->nscores; i++) {
          if (old->research[i] != real_packet->research[i]) {
            differ = TRUE;
            break;
          }
        }
      }
    }
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 10);}


    {
      differ = (old->nscores != real_packet->nscores);
      if(!differ) {
        int i;
        for (i = 0; i < real_packet->nscores; i++) {
          if (old->landarea[i] != real_packet->landarea[i]) {
            differ = TRUE;
            break;
          }
        }
      }
    }
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 11);}


    {
      differ = (old->nscores != real_packet->nscores);
      if(!differ) {
        int i;
        for (i = 0; i < real_packet->nscores; i++) {
          if (old->settledarea[i] != real_packet->settledarea[i]) {
            differ = TRUE;
            break;
          }
        }
      }
    }
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 12);}


    {
      differ = (old->nscores != real_packet->nscores);
      if(!differ) {
        int i;
        for (i = 0; i < real_packet->nscores; i++) {
          if (old->literacy[i] != real_packet->literacy[i]) {
            differ = TRUE;
            break;
          }
        }
      }
    }
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 13);}


    {
      differ = (old->nscores != real_packet->nscores);
      if(!differ) {
        int i;
        for (i = 0; i < real_packet->nscores; i++) {
          if (old->spaceship[i] != real_packet->spaceship[i]) {
            differ = TRUE;
            break;
          }
        }
      }
    }
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 14);}

  if (different == 0 && !force_send_of_unchanged) {
    return 0;
  }

  DIO_BV_PUT(&dout, fields);

  if (BV_ISSET(fields, 0)) {
    dio_put_uint8(&dout, real_packet->nscores);
  }
  if (BV_ISSET(fields, 1)) {
  
    {
      int i;

      for (i = 0; i < real_packet->nscores; i++) {
        dio_put_uint8(&dout, real_packet->id[i]);
      }
    } 
  }
  if (BV_ISSET(fields, 2)) {
  
    {
      int i;

      for (i = 0; i < real_packet->nscores; i++) {
        dio_put_uint16(&dout, real_packet->score[i]);
      }
    } 
  }
  if (BV_ISSET(fields, 3)) {
  
    {
      int i;

      for (i = 0; i < real_packet->nscores; i++) {
        dio_put_uint32(&dout, real_packet->pop[i]);
      }
    } 
  }
  if (BV_ISSET(fields, 4)) {
  
    {
      int i;

      for (i = 0; i < real_packet->nscores; i++) {
        dio_put_uint16(&dout, real_packet->bnp[i]);
      }
    } 
  }
  if (BV_ISSET(fields, 5)) {
  
    {
      int i;

      for (i = 0; i < real_packet->nscores; i++) {
        dio_put_uint16(&dout, real_packet->mfg[i]);
      }
    } 
  }
  if (BV_ISSET(fields, 6)) {
  
    {
      int i;

      for (i = 0; i < real_packet->nscores; i++) {
        dio_put_uint16(&dout, real_packet->cities[i]);
      }
    } 
  }
  if (BV_ISSET(fields, 7)) {
  
    {
      int i;

      for (i = 0; i < real_packet->nscores; i++) {
        dio_put_uint16(&dout, real_packet->techs[i]);
      }
    } 
  }
  if (BV_ISSET(fields, 8)) {
  
    {
      int i;

      for (i = 0; i < real_packet->nscores; i++) {
        dio_put_uint16(&dout, real_packet->mil_service[i]);
      }
    } 
  }
  if (BV_ISSET(fields, 9)) {
  
    {
      int i;

      for (i = 0; i < real_packet->nscores; i++) {
        dio_put_uint8(&dout, real_packet->wonders[i]);
      }
    } 
  }
  if (BV_ISSET(fields, 10)) {
  
    {
      int i;

      for (i = 0; i < real_packet->nscores; i++) {
        dio_put_uint16(&dout, real_packet->research[i]);
      }
    } 
  }
  if (BV_ISSET(fields, 11)) {
  
    {
      int i;

      for (i = 0; i < real_packet->nscores; i++) {
        dio_put_uint32(&dout, real_packet->landarea[i]);
      }
    } 
  }
  if (BV_ISSET(fields, 12)) {
  
    {
      int i;

      for (i = 0; i < real_packet->nscores; i++) {
        dio_put_uint32(&dout, real_packet->settledarea[i]);
      }
    } 
  }
  if (BV_ISSET(fields, 13)) {
  
    {
      int i;

      for (i = 0; i < real_packet->nscores; i++) {
        dio_put_uint16(&dout, real_packet->literacy[i]);
      }
    } 
  }
  if (BV_ISSET(fields, 14)) {
  
    {
      int i;

      for (i = 0; i < real_packet->nscores; i++) {
        dio_put_uint32(&dout, real_packet->spaceship[i]);
      }
    } 
  }


  if (old_from_hash) {
    hash_delete_entry(*hash, old);
  }

  clone = old;

  *clone = *real_packet;
  hash_insert(*hash, clone, clone);
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_endgame_report(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_ENDGAME_REPORT] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_ENDGAME_REPORT] = variant;
}

struct packet_endgame_report *receive_packet_endgame_report(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Receiving packet_endgame_report at the server.");
  }
  ensure_valid_variant_packet_endgame_report(pc);

  switch(pc->phs.variant[PACKET_ENDGAME_REPORT]) {
    case 100: return receive_packet_endgame_report_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_endgame_report(struct connection *pc, const struct packet_endgame_report *packet)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Sending packet_endgame_report from the client.");
  }
  ensure_valid_variant_packet_endgame_report(pc);

  switch(pc->phs.variant[PACKET_ENDGAME_REPORT]) {
    case 100: return send_packet_endgame_report_100(pc, packet);
    default: die("unknown variant"); return -1;
  }
}

void lsend_packet_endgame_report(struct conn_list *dest, const struct packet_endgame_report *packet)
{
  conn_list_iterate(*dest, pconn) {
    send_packet_endgame_report(pconn, packet);
  } conn_list_iterate_end;
}

static unsigned int hash_packet_tile_info_100(const void *vkey, unsigned int num_buckets)
{
  const struct packet_tile_info *key = (const struct packet_tile_info *) vkey;

  return (((key->x << 8) ^ key->y) % num_buckets);
}

static int cmp_packet_tile_info_100(const void *vkey1, const void *vkey2)
{
  const struct packet_tile_info *key1 = (const struct packet_tile_info *) vkey1;
  const struct packet_tile_info *key2 = (const struct packet_tile_info *) vkey2;
  int diff;

  diff = key1->x - key2->x;
  if (diff != 0) {
    return diff;
  }

  diff = key1->y - key2->y;
  if (diff != 0) {
    return diff;
  }

  return 0;
}

BV_DEFINE(packet_tile_info_100_fields, 6);

static struct packet_tile_info *receive_packet_tile_info_100(struct connection *pc, enum packet_type type)
{
  packet_tile_info_100_fields fields;
  struct packet_tile_info *old;
  struct hash_table **hash = &pc->phs.received[type];
  struct packet_tile_info *clone;
  RECEIVE_PACKET_START(packet_tile_info, real_packet);

  DIO_BV_GET(&din, fields);
  {
    int readin;
  
    dio_get_uint8(&din, &readin);
    real_packet->x = readin;
  }
  {
    int readin;
  
    dio_get_uint8(&din, &readin);
    real_packet->y = readin;
  }


  if (!*hash) {
    *hash = hash_new(hash_packet_tile_info_100, cmp_packet_tile_info_100);
  }
  old = hash_delete_entry(*hash, real_packet);

  if (old) {
    *real_packet = *old;
  } else {
    int x = real_packet->x;
    int y = real_packet->y;

    memset(real_packet, 0, sizeof(*real_packet));

    real_packet->x = x;
    real_packet->y = y;
  }

  if (BV_ISSET(fields, 0)) {
    {
      int readin;
    
      dio_get_sint16(&din, &readin);
      real_packet->type = readin;
    }
  }
  if (BV_ISSET(fields, 1)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->known = readin;
    }
  }
  if (BV_ISSET(fields, 2)) {
    {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->special = readin;
    }
  }
  if (BV_ISSET(fields, 3)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->owner = readin;
    }
  }
  if (BV_ISSET(fields, 4)) {
    {
      int readin;
    
      dio_get_sint16(&din, &readin);
      real_packet->continent = readin;
    }
  }
  if (BV_ISSET(fields, 5)) {
    dio_get_string(&din, real_packet->spec_sprite, sizeof(real_packet->spec_sprite));
  }

  clone = fc_malloc(sizeof(*clone));
  *clone = *real_packet;
  if (old) {
    free(old);
  }
  hash_insert(*hash, clone, clone);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_tile_info_100(struct connection *pc, const struct packet_tile_info *packet)
{
  const struct packet_tile_info *real_packet = packet;
  packet_tile_info_100_fields fields;
  struct packet_tile_info *old, *clone;
  bool differ, old_from_hash, force_send_of_unchanged = FALSE;
  struct hash_table **hash = &pc->phs.sent[PACKET_TILE_INFO];
  int different = 0;
  SEND_PACKET_START(PACKET_TILE_INFO);

  if (!*hash) {
    *hash = hash_new(hash_packet_tile_info_100, cmp_packet_tile_info_100);
  }
  BV_CLR_ALL(fields);

  old = hash_lookup_data(*hash, real_packet);
  old_from_hash = (old != NULL);
  if (!old) {
    old = fc_malloc(sizeof(*old));
    memset(old, 0, sizeof(*old));
    force_send_of_unchanged = TRUE;
  }

  differ = (old->type != real_packet->type);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 0);}

  differ = (old->known != real_packet->known);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 1);}

  differ = (old->special != real_packet->special);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 2);}

  differ = (old->owner != real_packet->owner);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 3);}

  differ = (old->continent != real_packet->continent);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 4);}

  differ = (strcmp(old->spec_sprite, real_packet->spec_sprite) != 0);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 5);}

  if (different == 0 && !force_send_of_unchanged) {
    return 0;
  }

  DIO_BV_PUT(&dout, fields);
  dio_put_uint8(&dout, real_packet->x);
  dio_put_uint8(&dout, real_packet->y);

  if (BV_ISSET(fields, 0)) {
    dio_put_sint16(&dout, real_packet->type);
  }
  if (BV_ISSET(fields, 1)) {
    dio_put_uint8(&dout, real_packet->known);
  }
  if (BV_ISSET(fields, 2)) {
    dio_put_uint16(&dout, real_packet->special);
  }
  if (BV_ISSET(fields, 3)) {
    dio_put_uint8(&dout, real_packet->owner);
  }
  if (BV_ISSET(fields, 4)) {
    dio_put_sint16(&dout, real_packet->continent);
  }
  if (BV_ISSET(fields, 5)) {
    dio_put_string(&dout, real_packet->spec_sprite);
  }


  if (old_from_hash) {
    hash_delete_entry(*hash, old);
  }

  clone = old;

  *clone = *real_packet;
  hash_insert(*hash, clone, clone);
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_tile_info(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_TILE_INFO] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_TILE_INFO] = variant;
}

struct packet_tile_info *receive_packet_tile_info(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Receiving packet_tile_info at the server.");
  }
  ensure_valid_variant_packet_tile_info(pc);

  switch(pc->phs.variant[PACKET_TILE_INFO]) {
    case 100: return receive_packet_tile_info_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_tile_info(struct connection *pc, const struct packet_tile_info *packet)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Sending packet_tile_info from the client.");
  }
  ensure_valid_variant_packet_tile_info(pc);

  switch(pc->phs.variant[PACKET_TILE_INFO]) {
    case 100: return send_packet_tile_info_100(pc, packet);
    default: die("unknown variant"); return -1;
  }
}

void lsend_packet_tile_info(struct conn_list *dest, const struct packet_tile_info *packet)
{
  conn_list_iterate(*dest, pconn) {
    send_packet_tile_info(pconn, packet);
  } conn_list_iterate_end;
}

#define hash_packet_game_info_100 hash_const

#define cmp_packet_game_info_100 cmp_const

BV_DEFINE(packet_game_info_100_fields, 30);

static struct packet_game_info *receive_packet_game_info_100(struct connection *pc, enum packet_type type)
{
  packet_game_info_100_fields fields;
  struct packet_game_info *old;
  struct hash_table **hash = &pc->phs.received[type];
  struct packet_game_info *clone;
  RECEIVE_PACKET_START(packet_game_info, real_packet);

  DIO_BV_GET(&din, fields);


  if (!*hash) {
    *hash = hash_new(hash_packet_game_info_100, cmp_packet_game_info_100);
  }
  old = hash_delete_entry(*hash, real_packet);

  if (old) {
    *real_packet = *old;
  } else {
    memset(real_packet, 0, sizeof(*real_packet));
  }

  if (BV_ISSET(fields, 0)) {
    {
      int readin;
    
      dio_get_uint32(&din, &readin);
      real_packet->gold = readin;
    }
  }
  if (BV_ISSET(fields, 1)) {
    {
      int readin;
    
      dio_get_uint32(&din, &readin);
      real_packet->tech = readin;
    }
  }
  if (BV_ISSET(fields, 2)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->researchcost = readin;
    }
  }
  if (BV_ISSET(fields, 3)) {
    {
      int readin;
    
      dio_get_uint32(&din, &readin);
      real_packet->skill_level = readin;
    }
  }
  if (BV_ISSET(fields, 4)) {
    {
      int readin;
    
      dio_get_uint32(&din, &readin);
      real_packet->seconds_to_turndone = readin;
    }
  }
  if (BV_ISSET(fields, 5)) {
    {
      int readin;
    
      dio_get_uint32(&din, &readin);
      real_packet->timeout = readin;
    }
  }
  if (BV_ISSET(fields, 6)) {
    {
      int readin;
    
      dio_get_sint16(&din, &readin);
      real_packet->turn = readin;
    }
  }
  if (BV_ISSET(fields, 7)) {
    {
      int readin;
    
      dio_get_sint16(&din, &readin);
      real_packet->year = readin;
    }
  }
  if (BV_ISSET(fields, 8)) {
    {
      int readin;
    
      dio_get_sint16(&din, &readin);
      real_packet->end_year = readin;
    }
  }
  if (BV_ISSET(fields, 9)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->min_players = readin;
    }
  }
  if (BV_ISSET(fields, 10)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->max_players = readin;
    }
  }
  if (BV_ISSET(fields, 11)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->nplayers = readin;
    }
  }
  if (BV_ISSET(fields, 12)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->player_idx = readin;
    }
  }
  if (BV_ISSET(fields, 13)) {
    {
      int readin;
    
      dio_get_uint32(&din, &readin);
      real_packet->globalwarming = readin;
    }
  }
  if (BV_ISSET(fields, 14)) {
    {
      int readin;
    
      dio_get_uint32(&din, &readin);
      real_packet->heating = readin;
    }
  }
  if (BV_ISSET(fields, 15)) {
    {
      int readin;
    
      dio_get_uint32(&din, &readin);
      real_packet->nuclearwinter = readin;
    }
  }
  if (BV_ISSET(fields, 16)) {
    {
      int readin;
    
      dio_get_uint32(&din, &readin);
      real_packet->cooling = readin;
    }
  }
  if (BV_ISSET(fields, 17)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->cityfactor = readin;
    }
  }
  if (BV_ISSET(fields, 18)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->diplcost = readin;
    }
  }
  if (BV_ISSET(fields, 19)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->freecost = readin;
    }
  }
  if (BV_ISSET(fields, 20)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->conquercost = readin;
    }
  }
  if (BV_ISSET(fields, 21)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->unhappysize = readin;
    }
  }
  if (BV_ISSET(fields, 22)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->angrycitizen = readin;
    }
  }
  if (BV_ISSET(fields, 23)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->techpenalty = readin;
    }
  }
  if (BV_ISSET(fields, 24)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->foodbox = readin;
    }
  }
  if (BV_ISSET(fields, 25)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->civstyle = readin;
    }
  }
  if (BV_ISSET(fields, 26)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->diplomacy = readin;
    }
  }
  real_packet->spacerace = BV_ISSET(fields, 27);
  if (BV_ISSET(fields, 28)) {
    
    for (;;) {
      int i;
    
      dio_get_uint8(&din, &i);
      if(i == 255) {
        break;
      }
      if(i > A_LAST) {
        freelog(LOG_ERROR, "packets_gen.c: WARNING: ignoring intra array diff");
      } else {
        {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->global_advances[i] = readin;
    }
      }
    }
  }
  if (BV_ISSET(fields, 29)) {
    
    for (;;) {
      int i;
    
      dio_get_uint8(&din, &i);
      if(i == 255) {
        break;
      }
      if(i > B_LAST) {
        freelog(LOG_ERROR, "packets_gen.c: WARNING: ignoring intra array diff");
      } else {
        {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->global_wonders[i] = readin;
    }
      }
    }
  }

  clone = fc_malloc(sizeof(*clone));
  *clone = *real_packet;
  if (old) {
    free(old);
  }
  hash_insert(*hash, clone, clone);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_game_info_100(struct connection *pc, const struct packet_game_info *packet)
{
  const struct packet_game_info *real_packet = packet;
  packet_game_info_100_fields fields;
  struct packet_game_info *old, *clone;
  bool differ, old_from_hash, force_send_of_unchanged = TRUE;
  struct hash_table **hash = &pc->phs.sent[PACKET_GAME_INFO];
  int different = 0;
  SEND_PACKET_START(PACKET_GAME_INFO);

  if (!*hash) {
    *hash = hash_new(hash_packet_game_info_100, cmp_packet_game_info_100);
  }
  BV_CLR_ALL(fields);

  old = hash_lookup_data(*hash, real_packet);
  old_from_hash = (old != NULL);
  if (!old) {
    old = fc_malloc(sizeof(*old));
    memset(old, 0, sizeof(*old));
    force_send_of_unchanged = TRUE;
  }

  differ = (old->gold != real_packet->gold);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 0);}

  differ = (old->tech != real_packet->tech);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 1);}

  differ = (old->researchcost != real_packet->researchcost);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 2);}

  differ = (old->skill_level != real_packet->skill_level);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 3);}

  differ = (old->seconds_to_turndone != real_packet->seconds_to_turndone);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 4);}

  differ = (old->timeout != real_packet->timeout);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 5);}

  differ = (old->turn != real_packet->turn);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 6);}

  differ = (old->year != real_packet->year);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 7);}

  differ = (old->end_year != real_packet->end_year);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 8);}

  differ = (old->min_players != real_packet->min_players);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 9);}

  differ = (old->max_players != real_packet->max_players);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 10);}

  differ = (old->nplayers != real_packet->nplayers);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 11);}

  differ = (old->player_idx != real_packet->player_idx);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 12);}

  differ = (old->globalwarming != real_packet->globalwarming);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 13);}

  differ = (old->heating != real_packet->heating);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 14);}

  differ = (old->nuclearwinter != real_packet->nuclearwinter);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 15);}

  differ = (old->cooling != real_packet->cooling);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 16);}

  differ = (old->cityfactor != real_packet->cityfactor);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 17);}

  differ = (old->diplcost != real_packet->diplcost);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 18);}

  differ = (old->freecost != real_packet->freecost);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 19);}

  differ = (old->conquercost != real_packet->conquercost);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 20);}

  differ = (old->unhappysize != real_packet->unhappysize);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 21);}

  differ = (old->angrycitizen != real_packet->angrycitizen);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 22);}

  differ = (old->techpenalty != real_packet->techpenalty);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 23);}

  differ = (old->foodbox != real_packet->foodbox);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 24);}

  differ = (old->civstyle != real_packet->civstyle);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 25);}

  differ = (old->diplomacy != real_packet->diplomacy);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 26);}

  differ = (old->spacerace != real_packet->spacerace);
  if(differ) {different++;}
  if(packet->spacerace) {BV_SET(fields, 27);}


    {
      differ = (A_LAST != A_LAST);
      if(!differ) {
        int i;
        for (i = 0; i < A_LAST; i++) {
          if (old->global_advances[i] != real_packet->global_advances[i]) {
            differ = TRUE;
            break;
          }
        }
      }
    }
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 28);}


    {
      differ = (B_LAST != B_LAST);
      if(!differ) {
        int i;
        for (i = 0; i < B_LAST; i++) {
          if (old->global_wonders[i] != real_packet->global_wonders[i]) {
            differ = TRUE;
            break;
          }
        }
      }
    }
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 29);}

  if (different == 0 && !force_send_of_unchanged) {
    return 0;
  }

  DIO_BV_PUT(&dout, fields);

  if (BV_ISSET(fields, 0)) {
    dio_put_uint32(&dout, real_packet->gold);
  }
  if (BV_ISSET(fields, 1)) {
    dio_put_uint32(&dout, real_packet->tech);
  }
  if (BV_ISSET(fields, 2)) {
    dio_put_uint8(&dout, real_packet->researchcost);
  }
  if (BV_ISSET(fields, 3)) {
    dio_put_uint32(&dout, real_packet->skill_level);
  }
  if (BV_ISSET(fields, 4)) {
    dio_put_uint32(&dout, real_packet->seconds_to_turndone);
  }
  if (BV_ISSET(fields, 5)) {
    dio_put_uint32(&dout, real_packet->timeout);
  }
  if (BV_ISSET(fields, 6)) {
    dio_put_sint16(&dout, real_packet->turn);
  }
  if (BV_ISSET(fields, 7)) {
    dio_put_sint16(&dout, real_packet->year);
  }
  if (BV_ISSET(fields, 8)) {
    dio_put_sint16(&dout, real_packet->end_year);
  }
  if (BV_ISSET(fields, 9)) {
    dio_put_uint8(&dout, real_packet->min_players);
  }
  if (BV_ISSET(fields, 10)) {
    dio_put_uint8(&dout, real_packet->max_players);
  }
  if (BV_ISSET(fields, 11)) {
    dio_put_uint8(&dout, real_packet->nplayers);
  }
  if (BV_ISSET(fields, 12)) {
    dio_put_uint8(&dout, real_packet->player_idx);
  }
  if (BV_ISSET(fields, 13)) {
    dio_put_uint32(&dout, real_packet->globalwarming);
  }
  if (BV_ISSET(fields, 14)) {
    dio_put_uint32(&dout, real_packet->heating);
  }
  if (BV_ISSET(fields, 15)) {
    dio_put_uint32(&dout, real_packet->nuclearwinter);
  }
  if (BV_ISSET(fields, 16)) {
    dio_put_uint32(&dout, real_packet->cooling);
  }
  if (BV_ISSET(fields, 17)) {
    dio_put_uint8(&dout, real_packet->cityfactor);
  }
  if (BV_ISSET(fields, 18)) {
    dio_put_uint8(&dout, real_packet->diplcost);
  }
  if (BV_ISSET(fields, 19)) {
    dio_put_uint8(&dout, real_packet->freecost);
  }
  if (BV_ISSET(fields, 20)) {
    dio_put_uint8(&dout, real_packet->conquercost);
  }
  if (BV_ISSET(fields, 21)) {
    dio_put_uint8(&dout, real_packet->unhappysize);
  }
  if (BV_ISSET(fields, 22)) {
    dio_put_uint8(&dout, real_packet->angrycitizen);
  }
  if (BV_ISSET(fields, 23)) {
    dio_put_uint8(&dout, real_packet->techpenalty);
  }
  if (BV_ISSET(fields, 24)) {
    dio_put_uint8(&dout, real_packet->foodbox);
  }
  if (BV_ISSET(fields, 25)) {
    dio_put_uint8(&dout, real_packet->civstyle);
  }
  if (BV_ISSET(fields, 26)) {
    dio_put_uint8(&dout, real_packet->diplomacy);
  }
  /* field 27 is folded into the header */
  if (BV_ISSET(fields, 28)) {
  
    {
      int i;

      assert(A_LAST < 255);

      for (i = 0; i < A_LAST; i++) {
        if(old->global_advances[i] != real_packet->global_advances[i]) {
          dio_put_uint8(&dout, i);
          dio_put_uint8(&dout, real_packet->global_advances[i]);
        }
      }
      dio_put_uint8(&dout, 255);
    } 
  }
  if (BV_ISSET(fields, 29)) {
  
    {
      int i;

      assert(B_LAST < 255);

      for (i = 0; i < B_LAST; i++) {
        if(old->global_wonders[i] != real_packet->global_wonders[i]) {
          dio_put_uint8(&dout, i);
          dio_put_uint16(&dout, real_packet->global_wonders[i]);
        }
      }
      dio_put_uint8(&dout, 255);
    } 
  }


  if (old_from_hash) {
    hash_delete_entry(*hash, old);
  }

  clone = old;

  *clone = *real_packet;
  hash_insert(*hash, clone, clone);
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_game_info(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_GAME_INFO] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_GAME_INFO] = variant;
}

struct packet_game_info *receive_packet_game_info(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Receiving packet_game_info at the server.");
  }
  ensure_valid_variant_packet_game_info(pc);

  switch(pc->phs.variant[PACKET_GAME_INFO]) {
    case 100: return receive_packet_game_info_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_game_info(struct connection *pc, const struct packet_game_info *packet)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Sending packet_game_info from the client.");
  }
  ensure_valid_variant_packet_game_info(pc);

  switch(pc->phs.variant[PACKET_GAME_INFO]) {
    case 100: return send_packet_game_info_100(pc, packet);
    default: die("unknown variant"); return -1;
  }
}

#define hash_packet_map_info_100 hash_const

#define cmp_packet_map_info_100 cmp_const

BV_DEFINE(packet_map_info_100_fields, 3);

static struct packet_map_info *receive_packet_map_info_100(struct connection *pc, enum packet_type type)
{
  packet_map_info_100_fields fields;
  struct packet_map_info *old;
  struct hash_table **hash = &pc->phs.received[type];
  struct packet_map_info *clone;
  RECEIVE_PACKET_START(packet_map_info, real_packet);

  DIO_BV_GET(&din, fields);


  if (!*hash) {
    *hash = hash_new(hash_packet_map_info_100, cmp_packet_map_info_100);
  }
  old = hash_delete_entry(*hash, real_packet);

  if (old) {
    *real_packet = *old;
  } else {
    memset(real_packet, 0, sizeof(*real_packet));
  }

  if (BV_ISSET(fields, 0)) {
    {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->xsize = readin;
    }
  }
  if (BV_ISSET(fields, 1)) {
    {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->ysize = readin;
    }
  }
  if (BV_ISSET(fields, 2)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->topology_id = readin;
    }
  }

  clone = fc_malloc(sizeof(*clone));
  *clone = *real_packet;
  if (old) {
    free(old);
  }
  hash_insert(*hash, clone, clone);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_map_info_100(struct connection *pc, const struct packet_map_info *packet)
{
  const struct packet_map_info *real_packet = packet;
  packet_map_info_100_fields fields;
  struct packet_map_info *old, *clone;
  bool differ, old_from_hash, force_send_of_unchanged = FALSE;
  struct hash_table **hash = &pc->phs.sent[PACKET_MAP_INFO];
  int different = 0;
  SEND_PACKET_START(PACKET_MAP_INFO);

  if (!*hash) {
    *hash = hash_new(hash_packet_map_info_100, cmp_packet_map_info_100);
  }
  BV_CLR_ALL(fields);

  old = hash_lookup_data(*hash, real_packet);
  old_from_hash = (old != NULL);
  if (!old) {
    old = fc_malloc(sizeof(*old));
    memset(old, 0, sizeof(*old));
    force_send_of_unchanged = TRUE;
  }

  differ = (old->xsize != real_packet->xsize);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 0);}

  differ = (old->ysize != real_packet->ysize);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 1);}

  differ = (old->topology_id != real_packet->topology_id);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 2);}

  if (different == 0 && !force_send_of_unchanged) {
    return 0;
  }

  DIO_BV_PUT(&dout, fields);

  if (BV_ISSET(fields, 0)) {
    dio_put_uint16(&dout, real_packet->xsize);
  }
  if (BV_ISSET(fields, 1)) {
    dio_put_uint16(&dout, real_packet->ysize);
  }
  if (BV_ISSET(fields, 2)) {
    dio_put_uint8(&dout, real_packet->topology_id);
  }


  if (old_from_hash) {
    hash_delete_entry(*hash, old);
  }

  clone = old;

  *clone = *real_packet;
  hash_insert(*hash, clone, clone);
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_map_info(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_MAP_INFO] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_MAP_INFO] = variant;
}

struct packet_map_info *receive_packet_map_info(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Receiving packet_map_info at the server.");
  }
  ensure_valid_variant_packet_map_info(pc);

  switch(pc->phs.variant[PACKET_MAP_INFO]) {
    case 100: return receive_packet_map_info_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_map_info(struct connection *pc, const struct packet_map_info *packet)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Sending packet_map_info from the client.");
  }
  ensure_valid_variant_packet_map_info(pc);

  switch(pc->phs.variant[PACKET_MAP_INFO]) {
    case 100: return send_packet_map_info_100(pc, packet);
    default: die("unknown variant"); return -1;
  }
}

void lsend_packet_map_info(struct conn_list *dest, const struct packet_map_info *packet)
{
  conn_list_iterate(*dest, pconn) {
    send_packet_map_info(pconn, packet);
  } conn_list_iterate_end;
}

#define hash_packet_nuke_tile_info_100 hash_const

#define cmp_packet_nuke_tile_info_100 cmp_const

BV_DEFINE(packet_nuke_tile_info_100_fields, 2);

static struct packet_nuke_tile_info *receive_packet_nuke_tile_info_100(struct connection *pc, enum packet_type type)
{
  packet_nuke_tile_info_100_fields fields;
  struct packet_nuke_tile_info *old;
  struct hash_table **hash = &pc->phs.received[type];
  struct packet_nuke_tile_info *clone;
  RECEIVE_PACKET_START(packet_nuke_tile_info, real_packet);

  DIO_BV_GET(&din, fields);


  if (!*hash) {
    *hash = hash_new(hash_packet_nuke_tile_info_100, cmp_packet_nuke_tile_info_100);
  }
  old = hash_delete_entry(*hash, real_packet);

  if (old) {
    *real_packet = *old;
  } else {
    memset(real_packet, 0, sizeof(*real_packet));
  }

  if (BV_ISSET(fields, 0)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->x = readin;
    }
  }
  if (BV_ISSET(fields, 1)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->y = readin;
    }
  }

  clone = fc_malloc(sizeof(*clone));
  *clone = *real_packet;
  if (old) {
    free(old);
  }
  hash_insert(*hash, clone, clone);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_nuke_tile_info_100(struct connection *pc, const struct packet_nuke_tile_info *packet)
{
  const struct packet_nuke_tile_info *real_packet = packet;
  packet_nuke_tile_info_100_fields fields;
  struct packet_nuke_tile_info *old, *clone;
  bool differ, old_from_hash, force_send_of_unchanged = TRUE;
  struct hash_table **hash = &pc->phs.sent[PACKET_NUKE_TILE_INFO];
  int different = 0;
  SEND_PACKET_START(PACKET_NUKE_TILE_INFO);

  if (!*hash) {
    *hash = hash_new(hash_packet_nuke_tile_info_100, cmp_packet_nuke_tile_info_100);
  }
  BV_CLR_ALL(fields);

  old = hash_lookup_data(*hash, real_packet);
  old_from_hash = (old != NULL);
  if (!old) {
    old = fc_malloc(sizeof(*old));
    memset(old, 0, sizeof(*old));
    force_send_of_unchanged = TRUE;
  }

  differ = (old->x != real_packet->x);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 0);}

  differ = (old->y != real_packet->y);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 1);}

  if (different == 0 && !force_send_of_unchanged) {
    return 0;
  }

  DIO_BV_PUT(&dout, fields);

  if (BV_ISSET(fields, 0)) {
    dio_put_uint8(&dout, real_packet->x);
  }
  if (BV_ISSET(fields, 1)) {
    dio_put_uint8(&dout, real_packet->y);
  }


  if (old_from_hash) {
    hash_delete_entry(*hash, old);
  }

  clone = old;

  *clone = *real_packet;
  hash_insert(*hash, clone, clone);
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_nuke_tile_info(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_NUKE_TILE_INFO] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_NUKE_TILE_INFO] = variant;
}

struct packet_nuke_tile_info *receive_packet_nuke_tile_info(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Receiving packet_nuke_tile_info at the server.");
  }
  ensure_valid_variant_packet_nuke_tile_info(pc);

  switch(pc->phs.variant[PACKET_NUKE_TILE_INFO]) {
    case 100: return receive_packet_nuke_tile_info_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_nuke_tile_info(struct connection *pc, const struct packet_nuke_tile_info *packet)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Sending packet_nuke_tile_info from the client.");
  }
  ensure_valid_variant_packet_nuke_tile_info(pc);

  switch(pc->phs.variant[PACKET_NUKE_TILE_INFO]) {
    case 100: return send_packet_nuke_tile_info_100(pc, packet);
    default: die("unknown variant"); return -1;
  }
}

void lsend_packet_nuke_tile_info(struct conn_list *dest, const struct packet_nuke_tile_info *packet)
{
  conn_list_iterate(*dest, pconn) {
    send_packet_nuke_tile_info(pconn, packet);
  } conn_list_iterate_end;
}

int dsend_packet_nuke_tile_info(struct connection *pc, int x, int y)
{
  struct packet_nuke_tile_info packet, *real_packet = &packet;

  real_packet->x = x;
  real_packet->y = y;
  
  return send_packet_nuke_tile_info(pc, real_packet);
}

void dlsend_packet_nuke_tile_info(struct conn_list *dest, int x, int y)
{
  struct packet_nuke_tile_info packet, *real_packet = &packet;

  real_packet->x = x;
  real_packet->y = y;
  
  lsend_packet_nuke_tile_info(dest, real_packet);
}

#define hash_packet_chat_msg_100 hash_const

#define cmp_packet_chat_msg_100 cmp_const

BV_DEFINE(packet_chat_msg_100_fields, 5);

static struct packet_chat_msg *receive_packet_chat_msg_100(struct connection *pc, enum packet_type type)
{
  packet_chat_msg_100_fields fields;
  struct packet_chat_msg *old;
  struct hash_table **hash = &pc->phs.received[type];
  struct packet_chat_msg *clone;
  RECEIVE_PACKET_START(packet_chat_msg, real_packet);

  DIO_BV_GET(&din, fields);


  if (!*hash) {
    *hash = hash_new(hash_packet_chat_msg_100, cmp_packet_chat_msg_100);
  }
  old = hash_delete_entry(*hash, real_packet);

  if (old) {
    *real_packet = *old;
  } else {
    memset(real_packet, 0, sizeof(*real_packet));
  }

  if (BV_ISSET(fields, 0)) {
    dio_get_string(&din, real_packet->message, sizeof(real_packet->message));
  }
  if (BV_ISSET(fields, 1)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->x = readin;
    }
  }
  if (BV_ISSET(fields, 2)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->y = readin;
    }
  }
  if (BV_ISSET(fields, 3)) {
    {
      int readin;
    
      dio_get_sint16(&din, &readin);
      real_packet->event = readin;
    }
  }
  if (BV_ISSET(fields, 4)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->conn_id = readin;
    }
  }

  clone = fc_malloc(sizeof(*clone));
  *clone = *real_packet;
  if (old) {
    free(old);
  }
  hash_insert(*hash, clone, clone);

  post_receive_packet_chat_msg(pc, real_packet);
  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_chat_msg_100(struct connection *pc, const struct packet_chat_msg *packet)
{
  const struct packet_chat_msg *real_packet = packet;
  packet_chat_msg_100_fields fields;
  struct packet_chat_msg *old, *clone;
  bool differ, old_from_hash, force_send_of_unchanged = TRUE;
  struct hash_table **hash = &pc->phs.sent[PACKET_CHAT_MSG];
  int different = 0;
  SEND_PACKET_START(PACKET_CHAT_MSG);

  {
    struct packet_chat_msg *tmp = fc_malloc(sizeof(*tmp));

    *tmp = *packet;
    pre_send_packet_chat_msg(pc, tmp);
    real_packet = tmp;
  }

  if (!*hash) {
    *hash = hash_new(hash_packet_chat_msg_100, cmp_packet_chat_msg_100);
  }
  BV_CLR_ALL(fields);

  old = hash_lookup_data(*hash, real_packet);
  old_from_hash = (old != NULL);
  if (!old) {
    old = fc_malloc(sizeof(*old));
    memset(old, 0, sizeof(*old));
    force_send_of_unchanged = TRUE;
  }

  differ = (strcmp(old->message, real_packet->message) != 0);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 0);}

  differ = (old->x != real_packet->x);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 1);}

  differ = (old->y != real_packet->y);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 2);}

  differ = (old->event != real_packet->event);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 3);}

  differ = (old->conn_id != real_packet->conn_id);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 4);}

  if (different == 0 && !force_send_of_unchanged) {

  if (real_packet != packet) {
    free((void *) real_packet);
  }
    return 0;
  }

  DIO_BV_PUT(&dout, fields);

  if (BV_ISSET(fields, 0)) {
    dio_put_string(&dout, real_packet->message);
  }
  if (BV_ISSET(fields, 1)) {
    dio_put_uint8(&dout, real_packet->x);
  }
  if (BV_ISSET(fields, 2)) {
    dio_put_uint8(&dout, real_packet->y);
  }
  if (BV_ISSET(fields, 3)) {
    dio_put_sint16(&dout, real_packet->event);
  }
  if (BV_ISSET(fields, 4)) {
    dio_put_uint8(&dout, real_packet->conn_id);
  }


  if (old_from_hash) {
    hash_delete_entry(*hash, old);
  }

  clone = old;

  *clone = *real_packet;
  hash_insert(*hash, clone, clone);

  if (real_packet != packet) {
    free((void *) real_packet);
  }
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_chat_msg(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_CHAT_MSG] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_CHAT_MSG] = variant;
}

struct packet_chat_msg *receive_packet_chat_msg(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Receiving packet_chat_msg at the server.");
  }
  ensure_valid_variant_packet_chat_msg(pc);

  switch(pc->phs.variant[PACKET_CHAT_MSG]) {
    case 100: return receive_packet_chat_msg_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_chat_msg(struct connection *pc, const struct packet_chat_msg *packet)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Sending packet_chat_msg from the client.");
  }
  ensure_valid_variant_packet_chat_msg(pc);

  switch(pc->phs.variant[PACKET_CHAT_MSG]) {
    case 100: return send_packet_chat_msg_100(pc, packet);
    default: die("unknown variant"); return -1;
  }
}

void lsend_packet_chat_msg(struct conn_list *dest, const struct packet_chat_msg *packet)
{
  conn_list_iterate(*dest, pconn) {
    send_packet_chat_msg(pconn, packet);
  } conn_list_iterate_end;
}

int dsend_packet_chat_msg(struct connection *pc, const char *message, int x, int y, enum event_type event, int conn_id)
{
  struct packet_chat_msg packet, *real_packet = &packet;

  sz_strlcpy(real_packet->message, message);
  real_packet->x = x;
  real_packet->y = y;
  real_packet->event = event;
  real_packet->conn_id = conn_id;
  
  return send_packet_chat_msg(pc, real_packet);
}

void dlsend_packet_chat_msg(struct conn_list *dest, const char *message, int x, int y, enum event_type event, int conn_id)
{
  struct packet_chat_msg packet, *real_packet = &packet;

  sz_strlcpy(real_packet->message, message);
  real_packet->x = x;
  real_packet->y = y;
  real_packet->event = event;
  real_packet->conn_id = conn_id;
  
  lsend_packet_chat_msg(dest, real_packet);
}

#define hash_packet_chat_msg_req_100 hash_const

#define cmp_packet_chat_msg_req_100 cmp_const

BV_DEFINE(packet_chat_msg_req_100_fields, 1);

static struct packet_chat_msg_req *receive_packet_chat_msg_req_100(struct connection *pc, enum packet_type type)
{
  packet_chat_msg_req_100_fields fields;
  struct packet_chat_msg_req *old;
  struct hash_table **hash = &pc->phs.received[type];
  struct packet_chat_msg_req *clone;
  RECEIVE_PACKET_START(packet_chat_msg_req, real_packet);

  DIO_BV_GET(&din, fields);


  if (!*hash) {
    *hash = hash_new(hash_packet_chat_msg_req_100, cmp_packet_chat_msg_req_100);
  }
  old = hash_delete_entry(*hash, real_packet);

  if (old) {
    *real_packet = *old;
  } else {
    memset(real_packet, 0, sizeof(*real_packet));
  }

  if (BV_ISSET(fields, 0)) {
    dio_get_string(&din, real_packet->message, sizeof(real_packet->message));
  }

  clone = fc_malloc(sizeof(*clone));
  *clone = *real_packet;
  if (old) {
    free(old);
  }
  hash_insert(*hash, clone, clone);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_chat_msg_req_100(struct connection *pc, const struct packet_chat_msg_req *packet)
{
  const struct packet_chat_msg_req *real_packet = packet;
  packet_chat_msg_req_100_fields fields;
  struct packet_chat_msg_req *old, *clone;
  bool differ, old_from_hash, force_send_of_unchanged = TRUE;
  struct hash_table **hash = &pc->phs.sent[PACKET_CHAT_MSG_REQ];
  int different = 0;
  SEND_PACKET_START(PACKET_CHAT_MSG_REQ);

  if (!*hash) {
    *hash = hash_new(hash_packet_chat_msg_req_100, cmp_packet_chat_msg_req_100);
  }
  BV_CLR_ALL(fields);

  old = hash_lookup_data(*hash, real_packet);
  old_from_hash = (old != NULL);
  if (!old) {
    old = fc_malloc(sizeof(*old));
    memset(old, 0, sizeof(*old));
    force_send_of_unchanged = TRUE;
  }

  differ = (strcmp(old->message, real_packet->message) != 0);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 0);}

  if (different == 0 && !force_send_of_unchanged) {
    return 0;
  }

  DIO_BV_PUT(&dout, fields);

  if (BV_ISSET(fields, 0)) {
    dio_put_string(&dout, real_packet->message);
  }


  if (old_from_hash) {
    hash_delete_entry(*hash, old);
  }

  clone = old;

  *clone = *real_packet;
  hash_insert(*hash, clone, clone);
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_chat_msg_req(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_CHAT_MSG_REQ] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_CHAT_MSG_REQ] = variant;
}

struct packet_chat_msg_req *receive_packet_chat_msg_req(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Receiving packet_chat_msg_req at the client.");
  }
  ensure_valid_variant_packet_chat_msg_req(pc);

  switch(pc->phs.variant[PACKET_CHAT_MSG_REQ]) {
    case 100: return receive_packet_chat_msg_req_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_chat_msg_req(struct connection *pc, const struct packet_chat_msg_req *packet)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Sending packet_chat_msg_req from the server.");
  }
  ensure_valid_variant_packet_chat_msg_req(pc);

  switch(pc->phs.variant[PACKET_CHAT_MSG_REQ]) {
    case 100: return send_packet_chat_msg_req_100(pc, packet);
    default: die("unknown variant"); return -1;
  }
}

int dsend_packet_chat_msg_req(struct connection *pc, const char *message)
{
  struct packet_chat_msg_req packet, *real_packet = &packet;

  sz_strlcpy(real_packet->message, message);
  
  return send_packet_chat_msg_req(pc, real_packet);
}

#define hash_packet_city_remove_100 hash_const

#define cmp_packet_city_remove_100 cmp_const

BV_DEFINE(packet_city_remove_100_fields, 1);

static struct packet_city_remove *receive_packet_city_remove_100(struct connection *pc, enum packet_type type)
{
  packet_city_remove_100_fields fields;
  struct packet_city_remove *old;
  struct hash_table **hash = &pc->phs.received[type];
  struct packet_city_remove *clone;
  RECEIVE_PACKET_START(packet_city_remove, real_packet);

  DIO_BV_GET(&din, fields);


  if (!*hash) {
    *hash = hash_new(hash_packet_city_remove_100, cmp_packet_city_remove_100);
  }
  old = hash_delete_entry(*hash, real_packet);

  if (old) {
    *real_packet = *old;
  } else {
    memset(real_packet, 0, sizeof(*real_packet));
  }

  if (BV_ISSET(fields, 0)) {
    {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->city_id = readin;
    }
  }

  clone = fc_malloc(sizeof(*clone));
  *clone = *real_packet;
  if (old) {
    free(old);
  }
  hash_insert(*hash, clone, clone);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_city_remove_100(struct connection *pc, const struct packet_city_remove *packet)
{
  const struct packet_city_remove *real_packet = packet;
  packet_city_remove_100_fields fields;
  struct packet_city_remove *old, *clone;
  bool differ, old_from_hash, force_send_of_unchanged = TRUE;
  struct hash_table **hash = &pc->phs.sent[PACKET_CITY_REMOVE];
  int different = 0;
  SEND_PACKET_START(PACKET_CITY_REMOVE);

  if (!*hash) {
    *hash = hash_new(hash_packet_city_remove_100, cmp_packet_city_remove_100);
  }
  BV_CLR_ALL(fields);

  old = hash_lookup_data(*hash, real_packet);
  old_from_hash = (old != NULL);
  if (!old) {
    old = fc_malloc(sizeof(*old));
    memset(old, 0, sizeof(*old));
    force_send_of_unchanged = TRUE;
  }

  differ = (old->city_id != real_packet->city_id);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 0);}

  if (different == 0 && !force_send_of_unchanged) {
    return 0;
  }

  DIO_BV_PUT(&dout, fields);

  if (BV_ISSET(fields, 0)) {
    dio_put_uint16(&dout, real_packet->city_id);
  }


  if (old_from_hash) {
    hash_delete_entry(*hash, old);
  }

  clone = old;

  *clone = *real_packet;
  hash_insert(*hash, clone, clone);
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_city_remove(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_CITY_REMOVE] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_CITY_REMOVE] = variant;
}

struct packet_city_remove *receive_packet_city_remove(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Receiving packet_city_remove at the server.");
  }
  ensure_valid_variant_packet_city_remove(pc);

  switch(pc->phs.variant[PACKET_CITY_REMOVE]) {
    case 100: return receive_packet_city_remove_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_city_remove(struct connection *pc, const struct packet_city_remove *packet)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Sending packet_city_remove from the client.");
  }
  ensure_valid_variant_packet_city_remove(pc);

  switch(pc->phs.variant[PACKET_CITY_REMOVE]) {
    case 100: return send_packet_city_remove_100(pc, packet);
    default: die("unknown variant"); return -1;
  }
}

void lsend_packet_city_remove(struct conn_list *dest, const struct packet_city_remove *packet)
{
  conn_list_iterate(*dest, pconn) {
    send_packet_city_remove(pconn, packet);
  } conn_list_iterate_end;
}

int dsend_packet_city_remove(struct connection *pc, int city_id)
{
  struct packet_city_remove packet, *real_packet = &packet;

  real_packet->city_id = city_id;
  
  return send_packet_city_remove(pc, real_packet);
}

void dlsend_packet_city_remove(struct conn_list *dest, int city_id)
{
  struct packet_city_remove packet, *real_packet = &packet;

  real_packet->city_id = city_id;
  
  lsend_packet_city_remove(dest, real_packet);
}

static unsigned int hash_packet_city_info_100(const void *vkey, unsigned int num_buckets)
{
  const struct packet_city_info *key = (const struct packet_city_info *) vkey;

  return ((key->id) % num_buckets);
}

static int cmp_packet_city_info_100(const void *vkey1, const void *vkey2)
{
  const struct packet_city_info *key1 = (const struct packet_city_info *) vkey1;
  const struct packet_city_info *key2 = (const struct packet_city_info *) vkey2;
  int diff;

  diff = key1->id - key2->id;
  if (diff != 0) {
    return diff;
  }

  return 0;
}

BV_DEFINE(packet_city_info_100_fields, 45);

static struct packet_city_info *receive_packet_city_info_100(struct connection *pc, enum packet_type type)
{
  packet_city_info_100_fields fields;
  struct packet_city_info *old;
  struct hash_table **hash = &pc->phs.received[type];
  struct packet_city_info *clone;
  RECEIVE_PACKET_START(packet_city_info, real_packet);

  DIO_BV_GET(&din, fields);
  {
    int readin;
  
    dio_get_uint16(&din, &readin);
    real_packet->id = readin;
  }


  if (!*hash) {
    *hash = hash_new(hash_packet_city_info_100, cmp_packet_city_info_100);
  }
  old = hash_delete_entry(*hash, real_packet);

  if (old) {
    *real_packet = *old;
  } else {
    int id = real_packet->id;

    memset(real_packet, 0, sizeof(*real_packet));

    real_packet->id = id;
  }

  if (BV_ISSET(fields, 0)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->owner = readin;
    }
  }
  if (BV_ISSET(fields, 1)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->x = readin;
    }
  }
  if (BV_ISSET(fields, 2)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->y = readin;
    }
  }
  if (BV_ISSET(fields, 3)) {
    dio_get_string(&din, real_packet->name, sizeof(real_packet->name));
  }
  if (BV_ISSET(fields, 4)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->size = readin;
    }
  }
  if (BV_ISSET(fields, 5)) {
    
    {
      int i;
    
      for (i = 0; i < 5; i++) {
        {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->ppl_happy[i] = readin;
    }
      }
    }
  }
  if (BV_ISSET(fields, 6)) {
    
    {
      int i;
    
      for (i = 0; i < 5; i++) {
        {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->ppl_content[i] = readin;
    }
      }
    }
  }
  if (BV_ISSET(fields, 7)) {
    
    {
      int i;
    
      for (i = 0; i < 5; i++) {
        {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->ppl_unhappy[i] = readin;
    }
      }
    }
  }
  if (BV_ISSET(fields, 8)) {
    
    {
      int i;
    
      for (i = 0; i < 5; i++) {
        {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->ppl_angry[i] = readin;
    }
      }
    }
  }
  if (BV_ISSET(fields, 9)) {
    
    {
      int i;
    
      for (i = 0; i < SP_COUNT; i++) {
        {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->specialists[i] = readin;
    }
      }
    }
  }
  if (BV_ISSET(fields, 10)) {
    {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->food_prod = readin;
    }
  }
  if (BV_ISSET(fields, 11)) {
    {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->shield_prod = readin;
    }
  }
  if (BV_ISSET(fields, 12)) {
    {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->trade_prod = readin;
    }
  }
  if (BV_ISSET(fields, 13)) {
    {
      int readin;
    
      dio_get_sint16(&din, &readin);
      real_packet->food_surplus = readin;
    }
  }
  if (BV_ISSET(fields, 14)) {
    {
      int readin;
    
      dio_get_sint16(&din, &readin);
      real_packet->shield_surplus = readin;
    }
  }
  if (BV_ISSET(fields, 15)) {
    {
      int readin;
    
      dio_get_sint16(&din, &readin);
      real_packet->tile_trade = readin;
    }
  }
  if (BV_ISSET(fields, 16)) {
    {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->food_stock = readin;
    }
  }
  if (BV_ISSET(fields, 17)) {
    {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->shield_stock = readin;
    }
  }
  if (BV_ISSET(fields, 18)) {
    {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->corruption = readin;
    }
  }
  if (BV_ISSET(fields, 19)) {
    
    {
      int i;
    
      for (i = 0; i < NUM_TRADEROUTES; i++) {
        {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->trade[i] = readin;
    }
      }
    }
  }
  if (BV_ISSET(fields, 20)) {
    
    {
      int i;
    
      for (i = 0; i < NUM_TRADEROUTES; i++) {
        {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->trade_value[i] = readin;
    }
      }
    }
  }
  if (BV_ISSET(fields, 21)) {
    {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->luxury_total = readin;
    }
  }
  if (BV_ISSET(fields, 22)) {
    {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->tax_total = readin;
    }
  }
  if (BV_ISSET(fields, 23)) {
    {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->science_total = readin;
    }
  }
  if (BV_ISSET(fields, 24)) {
    {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->pollution = readin;
    }
  }
  if (BV_ISSET(fields, 25)) {
    {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->shield_waste = readin;
    }
  }
  if (BV_ISSET(fields, 26)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->currently_building = readin;
    }
  }
  real_packet->is_building_unit = BV_ISSET(fields, 27);
  if (BV_ISSET(fields, 28)) {
    {
      int readin;
    
      dio_get_sint16(&din, &readin);
      real_packet->turn_last_built = readin;
    }
  }
  if (BV_ISSET(fields, 29)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->changed_from_id = readin;
    }
  }
  real_packet->changed_from_is_unit = BV_ISSET(fields, 30);
  if (BV_ISSET(fields, 31)) {
    {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->before_change_shields = readin;
    }
  }
  if (BV_ISSET(fields, 32)) {
    {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->disbanded_shields = readin;
    }
  }
  if (BV_ISSET(fields, 33)) {
    {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->caravan_shields = readin;
    }
  }
  if (BV_ISSET(fields, 34)) {
    {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->last_turns_shield_surplus = readin;
    }
  }
  if (BV_ISSET(fields, 35)) {
    dio_get_worklist(&din, &real_packet->worklist);
  }
  if (BV_ISSET(fields, 36)) {
    dio_get_bit_string(&din, real_packet->improvements, sizeof(real_packet->improvements));
  }
  if (BV_ISSET(fields, 37)) {
    
    {
      int i;
    
      for (i = 0; i < CITY_MAP_SIZE * CITY_MAP_SIZE; i++) {
        {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->city_map[i] = readin;
    }
      }
    }
  }
  real_packet->did_buy = BV_ISSET(fields, 38);
  real_packet->did_sell = BV_ISSET(fields, 39);
  real_packet->was_happy = BV_ISSET(fields, 40);
  real_packet->airlift = BV_ISSET(fields, 41);
  real_packet->diplomat_investigate = BV_ISSET(fields, 42);
  if (BV_ISSET(fields, 43)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->city_options = readin;
    }
  }
  if (BV_ISSET(fields, 44)) {
    {
      int readin;
    
      dio_get_sint16(&din, &readin);
      real_packet->turn_founded = readin;
    }
  }

  clone = fc_malloc(sizeof(*clone));
  *clone = *real_packet;
  if (old) {
    free(old);
  }
  hash_insert(*hash, clone, clone);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_city_info_100(struct connection *pc, const struct packet_city_info *packet)
{
  const struct packet_city_info *real_packet = packet;
  packet_city_info_100_fields fields;
  struct packet_city_info *old, *clone;
  bool differ, old_from_hash, force_send_of_unchanged = TRUE;
  struct hash_table **hash = &pc->phs.sent[PACKET_CITY_INFO];
  int different = 0;
  SEND_PACKET_START(PACKET_CITY_INFO);

  if (!*hash) {
    *hash = hash_new(hash_packet_city_info_100, cmp_packet_city_info_100);
  }
  BV_CLR_ALL(fields);

  old = hash_lookup_data(*hash, real_packet);
  old_from_hash = (old != NULL);
  if (!old) {
    old = fc_malloc(sizeof(*old));
    memset(old, 0, sizeof(*old));
    force_send_of_unchanged = TRUE;
  }

  differ = (old->owner != real_packet->owner);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 0);}

  differ = (old->x != real_packet->x);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 1);}

  differ = (old->y != real_packet->y);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 2);}

  differ = (strcmp(old->name, real_packet->name) != 0);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 3);}

  differ = (old->size != real_packet->size);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 4);}


    {
      differ = (5 != 5);
      if(!differ) {
        int i;
        for (i = 0; i < 5; i++) {
          if (old->ppl_happy[i] != real_packet->ppl_happy[i]) {
            differ = TRUE;
            break;
          }
        }
      }
    }
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 5);}


    {
      differ = (5 != 5);
      if(!differ) {
        int i;
        for (i = 0; i < 5; i++) {
          if (old->ppl_content[i] != real_packet->ppl_content[i]) {
            differ = TRUE;
            break;
          }
        }
      }
    }
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 6);}


    {
      differ = (5 != 5);
      if(!differ) {
        int i;
        for (i = 0; i < 5; i++) {
          if (old->ppl_unhappy[i] != real_packet->ppl_unhappy[i]) {
            differ = TRUE;
            break;
          }
        }
      }
    }
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 7);}


    {
      differ = (5 != 5);
      if(!differ) {
        int i;
        for (i = 0; i < 5; i++) {
          if (old->ppl_angry[i] != real_packet->ppl_angry[i]) {
            differ = TRUE;
            break;
          }
        }
      }
    }
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 8);}


    {
      differ = (SP_COUNT != SP_COUNT);
      if(!differ) {
        int i;
        for (i = 0; i < SP_COUNT; i++) {
          if (old->specialists[i] != real_packet->specialists[i]) {
            differ = TRUE;
            break;
          }
        }
      }
    }
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 9);}

  differ = (old->food_prod != real_packet->food_prod);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 10);}

  differ = (old->shield_prod != real_packet->shield_prod);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 11);}

  differ = (old->trade_prod != real_packet->trade_prod);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 12);}

  differ = (old->food_surplus != real_packet->food_surplus);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 13);}

  differ = (old->shield_surplus != real_packet->shield_surplus);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 14);}

  differ = (old->tile_trade != real_packet->tile_trade);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 15);}

  differ = (old->food_stock != real_packet->food_stock);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 16);}

  differ = (old->shield_stock != real_packet->shield_stock);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 17);}

  differ = (old->corruption != real_packet->corruption);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 18);}


    {
      differ = (NUM_TRADEROUTES != NUM_TRADEROUTES);
      if(!differ) {
        int i;
        for (i = 0; i < NUM_TRADEROUTES; i++) {
          if (old->trade[i] != real_packet->trade[i]) {
            differ = TRUE;
            break;
          }
        }
      }
    }
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 19);}


    {
      differ = (NUM_TRADEROUTES != NUM_TRADEROUTES);
      if(!differ) {
        int i;
        for (i = 0; i < NUM_TRADEROUTES; i++) {
          if (old->trade_value[i] != real_packet->trade_value[i]) {
            differ = TRUE;
            break;
          }
        }
      }
    }
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 20);}

  differ = (old->luxury_total != real_packet->luxury_total);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 21);}

  differ = (old->tax_total != real_packet->tax_total);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 22);}

  differ = (old->science_total != real_packet->science_total);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 23);}

  differ = (old->pollution != real_packet->pollution);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 24);}

  differ = (old->shield_waste != real_packet->shield_waste);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 25);}

  differ = (old->currently_building != real_packet->currently_building);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 26);}

  differ = (old->is_building_unit != real_packet->is_building_unit);
  if(differ) {different++;}
  if(packet->is_building_unit) {BV_SET(fields, 27);}

  differ = (old->turn_last_built != real_packet->turn_last_built);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 28);}

  differ = (old->changed_from_id != real_packet->changed_from_id);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 29);}

  differ = (old->changed_from_is_unit != real_packet->changed_from_is_unit);
  if(differ) {different++;}
  if(packet->changed_from_is_unit) {BV_SET(fields, 30);}

  differ = (old->before_change_shields != real_packet->before_change_shields);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 31);}

  differ = (old->disbanded_shields != real_packet->disbanded_shields);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 32);}

  differ = (old->caravan_shields != real_packet->caravan_shields);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 33);}

  differ = (old->last_turns_shield_surplus != real_packet->last_turns_shield_surplus);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 34);}

  differ = !are_worklists_equal(&old->worklist, &real_packet->worklist);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 35);}

  differ = (strcmp(old->improvements, real_packet->improvements) != 0);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 36);}


    {
      differ = (CITY_MAP_SIZE * CITY_MAP_SIZE != CITY_MAP_SIZE * CITY_MAP_SIZE);
      if(!differ) {
        int i;
        for (i = 0; i < CITY_MAP_SIZE * CITY_MAP_SIZE; i++) {
          if (old->city_map[i] != real_packet->city_map[i]) {
            differ = TRUE;
            break;
          }
        }
      }
    }
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 37);}

  differ = (old->did_buy != real_packet->did_buy);
  if(differ) {different++;}
  if(packet->did_buy) {BV_SET(fields, 38);}

  differ = (old->did_sell != real_packet->did_sell);
  if(differ) {different++;}
  if(packet->did_sell) {BV_SET(fields, 39);}

  differ = (old->was_happy != real_packet->was_happy);
  if(differ) {different++;}
  if(packet->was_happy) {BV_SET(fields, 40);}

  differ = (old->airlift != real_packet->airlift);
  if(differ) {different++;}
  if(packet->airlift) {BV_SET(fields, 41);}

  differ = (old->diplomat_investigate != real_packet->diplomat_investigate);
  if(differ) {different++;}
  if(packet->diplomat_investigate) {BV_SET(fields, 42);}

  differ = (old->city_options != real_packet->city_options);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 43);}

  differ = (old->turn_founded != real_packet->turn_founded);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 44);}

  if (different == 0 && !force_send_of_unchanged) {
    return 0;
  }

  DIO_BV_PUT(&dout, fields);
  dio_put_uint16(&dout, real_packet->id);

  if (BV_ISSET(fields, 0)) {
    dio_put_uint8(&dout, real_packet->owner);
  }
  if (BV_ISSET(fields, 1)) {
    dio_put_uint8(&dout, real_packet->x);
  }
  if (BV_ISSET(fields, 2)) {
    dio_put_uint8(&dout, real_packet->y);
  }
  if (BV_ISSET(fields, 3)) {
    dio_put_string(&dout, real_packet->name);
  }
  if (BV_ISSET(fields, 4)) {
    dio_put_uint8(&dout, real_packet->size);
  }
  if (BV_ISSET(fields, 5)) {
  
    {
      int i;

      for (i = 0; i < 5; i++) {
        dio_put_uint8(&dout, real_packet->ppl_happy[i]);
      }
    } 
  }
  if (BV_ISSET(fields, 6)) {
  
    {
      int i;

      for (i = 0; i < 5; i++) {
        dio_put_uint8(&dout, real_packet->ppl_content[i]);
      }
    } 
  }
  if (BV_ISSET(fields, 7)) {
  
    {
      int i;

      for (i = 0; i < 5; i++) {
        dio_put_uint8(&dout, real_packet->ppl_unhappy[i]);
      }
    } 
  }
  if (BV_ISSET(fields, 8)) {
  
    {
      int i;

      for (i = 0; i < 5; i++) {
        dio_put_uint8(&dout, real_packet->ppl_angry[i]);
      }
    } 
  }
  if (BV_ISSET(fields, 9)) {
  
    {
      int i;

      for (i = 0; i < SP_COUNT; i++) {
        dio_put_uint8(&dout, real_packet->specialists[i]);
      }
    } 
  }
  if (BV_ISSET(fields, 10)) {
    dio_put_uint16(&dout, real_packet->food_prod);
  }
  if (BV_ISSET(fields, 11)) {
    dio_put_uint16(&dout, real_packet->shield_prod);
  }
  if (BV_ISSET(fields, 12)) {
    dio_put_uint16(&dout, real_packet->trade_prod);
  }
  if (BV_ISSET(fields, 13)) {
    dio_put_sint16(&dout, real_packet->food_surplus);
  }
  if (BV_ISSET(fields, 14)) {
    dio_put_sint16(&dout, real_packet->shield_surplus);
  }
  if (BV_ISSET(fields, 15)) {
    dio_put_sint16(&dout, real_packet->tile_trade);
  }
  if (BV_ISSET(fields, 16)) {
    dio_put_uint16(&dout, real_packet->food_stock);
  }
  if (BV_ISSET(fields, 17)) {
    dio_put_uint16(&dout, real_packet->shield_stock);
  }
  if (BV_ISSET(fields, 18)) {
    dio_put_uint16(&dout, real_packet->corruption);
  }
  if (BV_ISSET(fields, 19)) {
  
    {
      int i;

      for (i = 0; i < NUM_TRADEROUTES; i++) {
        dio_put_uint16(&dout, real_packet->trade[i]);
      }
    } 
  }
  if (BV_ISSET(fields, 20)) {
  
    {
      int i;

      for (i = 0; i < NUM_TRADEROUTES; i++) {
        dio_put_uint8(&dout, real_packet->trade_value[i]);
      }
    } 
  }
  if (BV_ISSET(fields, 21)) {
    dio_put_uint16(&dout, real_packet->luxury_total);
  }
  if (BV_ISSET(fields, 22)) {
    dio_put_uint16(&dout, real_packet->tax_total);
  }
  if (BV_ISSET(fields, 23)) {
    dio_put_uint16(&dout, real_packet->science_total);
  }
  if (BV_ISSET(fields, 24)) {
    dio_put_uint16(&dout, real_packet->pollution);
  }
  if (BV_ISSET(fields, 25)) {
    dio_put_uint16(&dout, real_packet->shield_waste);
  }
  if (BV_ISSET(fields, 26)) {
    dio_put_uint8(&dout, real_packet->currently_building);
  }
  /* field 27 is folded into the header */
  if (BV_ISSET(fields, 28)) {
    dio_put_sint16(&dout, real_packet->turn_last_built);
  }
  if (BV_ISSET(fields, 29)) {
    dio_put_uint8(&dout, real_packet->changed_from_id);
  }
  /* field 30 is folded into the header */
  if (BV_ISSET(fields, 31)) {
    dio_put_uint16(&dout, real_packet->before_change_shields);
  }
  if (BV_ISSET(fields, 32)) {
    dio_put_uint16(&dout, real_packet->disbanded_shields);
  }
  if (BV_ISSET(fields, 33)) {
    dio_put_uint16(&dout, real_packet->caravan_shields);
  }
  if (BV_ISSET(fields, 34)) {
    dio_put_uint16(&dout, real_packet->last_turns_shield_surplus);
  }
  if (BV_ISSET(fields, 35)) {
    dio_put_worklist(&dout, &real_packet->worklist);
  }
  if (BV_ISSET(fields, 36)) {
    dio_put_bit_string(&dout, real_packet->improvements);
  }
  if (BV_ISSET(fields, 37)) {
  
    {
      int i;

      for (i = 0; i < CITY_MAP_SIZE * CITY_MAP_SIZE; i++) {
        dio_put_uint8(&dout, real_packet->city_map[i]);
      }
    } 
  }
  /* field 38 is folded into the header */
  /* field 39 is folded into the header */
  /* field 40 is folded into the header */
  /* field 41 is folded into the header */
  /* field 42 is folded into the header */
  if (BV_ISSET(fields, 43)) {
    dio_put_uint8(&dout, real_packet->city_options);
  }
  if (BV_ISSET(fields, 44)) {
    dio_put_sint16(&dout, real_packet->turn_founded);
  }


  if (old_from_hash) {
    hash_delete_entry(*hash, old);
  }

  clone = old;

  *clone = *real_packet;
  hash_insert(*hash, clone, clone);
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_city_info(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_CITY_INFO] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_CITY_INFO] = variant;
}

struct packet_city_info *receive_packet_city_info(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Receiving packet_city_info at the server.");
  }
  ensure_valid_variant_packet_city_info(pc);

  switch(pc->phs.variant[PACKET_CITY_INFO]) {
    case 100: return receive_packet_city_info_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_city_info(struct connection *pc, const struct packet_city_info *packet)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Sending packet_city_info from the client.");
  }
  ensure_valid_variant_packet_city_info(pc);

  switch(pc->phs.variant[PACKET_CITY_INFO]) {
    case 100: return send_packet_city_info_100(pc, packet);
    default: die("unknown variant"); return -1;
  }
}

void lsend_packet_city_info(struct conn_list *dest, const struct packet_city_info *packet)
{
  conn_list_iterate(*dest, pconn) {
    send_packet_city_info(pconn, packet);
  } conn_list_iterate_end;
}

static unsigned int hash_packet_city_short_info_100(const void *vkey, unsigned int num_buckets)
{
  const struct packet_city_short_info *key = (const struct packet_city_short_info *) vkey;

  return ((key->id) % num_buckets);
}

static int cmp_packet_city_short_info_100(const void *vkey1, const void *vkey2)
{
  const struct packet_city_short_info *key1 = (const struct packet_city_short_info *) vkey1;
  const struct packet_city_short_info *key2 = (const struct packet_city_short_info *) vkey2;
  int diff;

  diff = key1->id - key2->id;
  if (diff != 0) {
    return diff;
  }

  return 0;
}

BV_DEFINE(packet_city_short_info_100_fields, 11);

static struct packet_city_short_info *receive_packet_city_short_info_100(struct connection *pc, enum packet_type type)
{
  packet_city_short_info_100_fields fields;
  struct packet_city_short_info *old;
  struct hash_table **hash = &pc->phs.received[type];
  struct packet_city_short_info *clone;
  RECEIVE_PACKET_START(packet_city_short_info, real_packet);

  DIO_BV_GET(&din, fields);
  {
    int readin;
  
    dio_get_uint16(&din, &readin);
    real_packet->id = readin;
  }


  if (!*hash) {
    *hash = hash_new(hash_packet_city_short_info_100, cmp_packet_city_short_info_100);
  }
  old = hash_delete_entry(*hash, real_packet);

  if (old) {
    *real_packet = *old;
  } else {
    int id = real_packet->id;

    memset(real_packet, 0, sizeof(*real_packet));

    real_packet->id = id;
  }

  if (BV_ISSET(fields, 0)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->owner = readin;
    }
  }
  if (BV_ISSET(fields, 1)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->x = readin;
    }
  }
  if (BV_ISSET(fields, 2)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->y = readin;
    }
  }
  if (BV_ISSET(fields, 3)) {
    dio_get_string(&din, real_packet->name, sizeof(real_packet->name));
  }
  if (BV_ISSET(fields, 4)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->size = readin;
    }
  }
  real_packet->happy = BV_ISSET(fields, 5);
  real_packet->unhappy = BV_ISSET(fields, 6);
  real_packet->capital = BV_ISSET(fields, 7);
  real_packet->walls = BV_ISSET(fields, 8);
  real_packet->occupied = BV_ISSET(fields, 9);
  if (BV_ISSET(fields, 10)) {
    {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->tile_trade = readin;
    }
  }

  clone = fc_malloc(sizeof(*clone));
  *clone = *real_packet;
  if (old) {
    free(old);
  }
  hash_insert(*hash, clone, clone);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_city_short_info_100(struct connection *pc, const struct packet_city_short_info *packet)
{
  const struct packet_city_short_info *real_packet = packet;
  packet_city_short_info_100_fields fields;
  struct packet_city_short_info *old, *clone;
  bool differ, old_from_hash, force_send_of_unchanged = TRUE;
  struct hash_table **hash = &pc->phs.sent[PACKET_CITY_SHORT_INFO];
  int different = 0;
  SEND_PACKET_START(PACKET_CITY_SHORT_INFO);

  if (!*hash) {
    *hash = hash_new(hash_packet_city_short_info_100, cmp_packet_city_short_info_100);
  }
  BV_CLR_ALL(fields);

  old = hash_lookup_data(*hash, real_packet);
  old_from_hash = (old != NULL);
  if (!old) {
    old = fc_malloc(sizeof(*old));
    memset(old, 0, sizeof(*old));
    force_send_of_unchanged = TRUE;
  }

  differ = (old->owner != real_packet->owner);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 0);}

  differ = (old->x != real_packet->x);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 1);}

  differ = (old->y != real_packet->y);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 2);}

  differ = (strcmp(old->name, real_packet->name) != 0);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 3);}

  differ = (old->size != real_packet->size);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 4);}

  differ = (old->happy != real_packet->happy);
  if(differ) {different++;}
  if(packet->happy) {BV_SET(fields, 5);}

  differ = (old->unhappy != real_packet->unhappy);
  if(differ) {different++;}
  if(packet->unhappy) {BV_SET(fields, 6);}

  differ = (old->capital != real_packet->capital);
  if(differ) {different++;}
  if(packet->capital) {BV_SET(fields, 7);}

  differ = (old->walls != real_packet->walls);
  if(differ) {different++;}
  if(packet->walls) {BV_SET(fields, 8);}

  differ = (old->occupied != real_packet->occupied);
  if(differ) {different++;}
  if(packet->occupied) {BV_SET(fields, 9);}

  differ = (old->tile_trade != real_packet->tile_trade);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 10);}

  if (different == 0 && !force_send_of_unchanged) {
    return 0;
  }

  DIO_BV_PUT(&dout, fields);
  dio_put_uint16(&dout, real_packet->id);

  if (BV_ISSET(fields, 0)) {
    dio_put_uint8(&dout, real_packet->owner);
  }
  if (BV_ISSET(fields, 1)) {
    dio_put_uint8(&dout, real_packet->x);
  }
  if (BV_ISSET(fields, 2)) {
    dio_put_uint8(&dout, real_packet->y);
  }
  if (BV_ISSET(fields, 3)) {
    dio_put_string(&dout, real_packet->name);
  }
  if (BV_ISSET(fields, 4)) {
    dio_put_uint8(&dout, real_packet->size);
  }
  /* field 5 is folded into the header */
  /* field 6 is folded into the header */
  /* field 7 is folded into the header */
  /* field 8 is folded into the header */
  /* field 9 is folded into the header */
  if (BV_ISSET(fields, 10)) {
    dio_put_uint16(&dout, real_packet->tile_trade);
  }


  if (old_from_hash) {
    hash_delete_entry(*hash, old);
  }

  clone = old;

  *clone = *real_packet;
  hash_insert(*hash, clone, clone);
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_city_short_info(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_CITY_SHORT_INFO] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_CITY_SHORT_INFO] = variant;
}

struct packet_city_short_info *receive_packet_city_short_info(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Receiving packet_city_short_info at the server.");
  }
  ensure_valid_variant_packet_city_short_info(pc);

  switch(pc->phs.variant[PACKET_CITY_SHORT_INFO]) {
    case 100: return receive_packet_city_short_info_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_city_short_info(struct connection *pc, const struct packet_city_short_info *packet)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Sending packet_city_short_info from the client.");
  }
  ensure_valid_variant_packet_city_short_info(pc);

  switch(pc->phs.variant[PACKET_CITY_SHORT_INFO]) {
    case 100: return send_packet_city_short_info_100(pc, packet);
    default: die("unknown variant"); return -1;
  }
}

void lsend_packet_city_short_info(struct conn_list *dest, const struct packet_city_short_info *packet)
{
  conn_list_iterate(*dest, pconn) {
    send_packet_city_short_info(pconn, packet);
  } conn_list_iterate_end;
}

#define hash_packet_city_sell_100 hash_const

#define cmp_packet_city_sell_100 cmp_const

BV_DEFINE(packet_city_sell_100_fields, 2);

static struct packet_city_sell *receive_packet_city_sell_100(struct connection *pc, enum packet_type type)
{
  packet_city_sell_100_fields fields;
  struct packet_city_sell *old;
  struct hash_table **hash = &pc->phs.received[type];
  struct packet_city_sell *clone;
  RECEIVE_PACKET_START(packet_city_sell, real_packet);

  DIO_BV_GET(&din, fields);


  if (!*hash) {
    *hash = hash_new(hash_packet_city_sell_100, cmp_packet_city_sell_100);
  }
  old = hash_delete_entry(*hash, real_packet);

  if (old) {
    *real_packet = *old;
  } else {
    memset(real_packet, 0, sizeof(*real_packet));
  }

  if (BV_ISSET(fields, 0)) {
    {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->city_id = readin;
    }
  }
  if (BV_ISSET(fields, 1)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->build_id = readin;
    }
  }

  clone = fc_malloc(sizeof(*clone));
  *clone = *real_packet;
  if (old) {
    free(old);
  }
  hash_insert(*hash, clone, clone);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_city_sell_100(struct connection *pc, const struct packet_city_sell *packet)
{
  const struct packet_city_sell *real_packet = packet;
  packet_city_sell_100_fields fields;
  struct packet_city_sell *old, *clone;
  bool differ, old_from_hash, force_send_of_unchanged = TRUE;
  struct hash_table **hash = &pc->phs.sent[PACKET_CITY_SELL];
  int different = 0;
  SEND_PACKET_START(PACKET_CITY_SELL);

  if (!*hash) {
    *hash = hash_new(hash_packet_city_sell_100, cmp_packet_city_sell_100);
  }
  BV_CLR_ALL(fields);

  old = hash_lookup_data(*hash, real_packet);
  old_from_hash = (old != NULL);
  if (!old) {
    old = fc_malloc(sizeof(*old));
    memset(old, 0, sizeof(*old));
    force_send_of_unchanged = TRUE;
  }

  differ = (old->city_id != real_packet->city_id);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 0);}

  differ = (old->build_id != real_packet->build_id);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 1);}

  if (different == 0 && !force_send_of_unchanged) {
    return 0;
  }

  DIO_BV_PUT(&dout, fields);

  if (BV_ISSET(fields, 0)) {
    dio_put_uint16(&dout, real_packet->city_id);
  }
  if (BV_ISSET(fields, 1)) {
    dio_put_uint8(&dout, real_packet->build_id);
  }


  if (old_from_hash) {
    hash_delete_entry(*hash, old);
  }

  clone = old;

  *clone = *real_packet;
  hash_insert(*hash, clone, clone);
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_city_sell(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_CITY_SELL] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_CITY_SELL] = variant;
}

struct packet_city_sell *receive_packet_city_sell(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Receiving packet_city_sell at the client.");
  }
  ensure_valid_variant_packet_city_sell(pc);

  switch(pc->phs.variant[PACKET_CITY_SELL]) {
    case 100: return receive_packet_city_sell_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_city_sell(struct connection *pc, const struct packet_city_sell *packet)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Sending packet_city_sell from the server.");
  }
  ensure_valid_variant_packet_city_sell(pc);

  switch(pc->phs.variant[PACKET_CITY_SELL]) {
    case 100: return send_packet_city_sell_100(pc, packet);
    default: die("unknown variant"); return -1;
  }
}

int dsend_packet_city_sell(struct connection *pc, int city_id, int build_id)
{
  struct packet_city_sell packet, *real_packet = &packet;

  real_packet->city_id = city_id;
  real_packet->build_id = build_id;
  
  return send_packet_city_sell(pc, real_packet);
}

#define hash_packet_city_buy_100 hash_const

#define cmp_packet_city_buy_100 cmp_const

BV_DEFINE(packet_city_buy_100_fields, 1);

static struct packet_city_buy *receive_packet_city_buy_100(struct connection *pc, enum packet_type type)
{
  packet_city_buy_100_fields fields;
  struct packet_city_buy *old;
  struct hash_table **hash = &pc->phs.received[type];
  struct packet_city_buy *clone;
  RECEIVE_PACKET_START(packet_city_buy, real_packet);

  DIO_BV_GET(&din, fields);


  if (!*hash) {
    *hash = hash_new(hash_packet_city_buy_100, cmp_packet_city_buy_100);
  }
  old = hash_delete_entry(*hash, real_packet);

  if (old) {
    *real_packet = *old;
  } else {
    memset(real_packet, 0, sizeof(*real_packet));
  }

  if (BV_ISSET(fields, 0)) {
    {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->city_id = readin;
    }
  }

  clone = fc_malloc(sizeof(*clone));
  *clone = *real_packet;
  if (old) {
    free(old);
  }
  hash_insert(*hash, clone, clone);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_city_buy_100(struct connection *pc, const struct packet_city_buy *packet)
{
  const struct packet_city_buy *real_packet = packet;
  packet_city_buy_100_fields fields;
  struct packet_city_buy *old, *clone;
  bool differ, old_from_hash, force_send_of_unchanged = TRUE;
  struct hash_table **hash = &pc->phs.sent[PACKET_CITY_BUY];
  int different = 0;
  SEND_PACKET_START(PACKET_CITY_BUY);

  if (!*hash) {
    *hash = hash_new(hash_packet_city_buy_100, cmp_packet_city_buy_100);
  }
  BV_CLR_ALL(fields);

  old = hash_lookup_data(*hash, real_packet);
  old_from_hash = (old != NULL);
  if (!old) {
    old = fc_malloc(sizeof(*old));
    memset(old, 0, sizeof(*old));
    force_send_of_unchanged = TRUE;
  }

  differ = (old->city_id != real_packet->city_id);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 0);}

  if (different == 0 && !force_send_of_unchanged) {
    return 0;
  }

  DIO_BV_PUT(&dout, fields);

  if (BV_ISSET(fields, 0)) {
    dio_put_uint16(&dout, real_packet->city_id);
  }


  if (old_from_hash) {
    hash_delete_entry(*hash, old);
  }

  clone = old;

  *clone = *real_packet;
  hash_insert(*hash, clone, clone);
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_city_buy(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_CITY_BUY] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_CITY_BUY] = variant;
}

struct packet_city_buy *receive_packet_city_buy(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Receiving packet_city_buy at the client.");
  }
  ensure_valid_variant_packet_city_buy(pc);

  switch(pc->phs.variant[PACKET_CITY_BUY]) {
    case 100: return receive_packet_city_buy_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_city_buy(struct connection *pc, const struct packet_city_buy *packet)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Sending packet_city_buy from the server.");
  }
  ensure_valid_variant_packet_city_buy(pc);

  switch(pc->phs.variant[PACKET_CITY_BUY]) {
    case 100: return send_packet_city_buy_100(pc, packet);
    default: die("unknown variant"); return -1;
  }
}

int dsend_packet_city_buy(struct connection *pc, int city_id)
{
  struct packet_city_buy packet, *real_packet = &packet;

  real_packet->city_id = city_id;
  
  return send_packet_city_buy(pc, real_packet);
}

#define hash_packet_city_change_100 hash_const

#define cmp_packet_city_change_100 cmp_const

BV_DEFINE(packet_city_change_100_fields, 3);

static struct packet_city_change *receive_packet_city_change_100(struct connection *pc, enum packet_type type)
{
  packet_city_change_100_fields fields;
  struct packet_city_change *old;
  struct hash_table **hash = &pc->phs.received[type];
  struct packet_city_change *clone;
  RECEIVE_PACKET_START(packet_city_change, real_packet);

  DIO_BV_GET(&din, fields);


  if (!*hash) {
    *hash = hash_new(hash_packet_city_change_100, cmp_packet_city_change_100);
  }
  old = hash_delete_entry(*hash, real_packet);

  if (old) {
    *real_packet = *old;
  } else {
    memset(real_packet, 0, sizeof(*real_packet));
  }

  if (BV_ISSET(fields, 0)) {
    {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->city_id = readin;
    }
  }
  if (BV_ISSET(fields, 1)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->build_id = readin;
    }
  }
  real_packet->is_build_id_unit_id = BV_ISSET(fields, 2);

  clone = fc_malloc(sizeof(*clone));
  *clone = *real_packet;
  if (old) {
    free(old);
  }
  hash_insert(*hash, clone, clone);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_city_change_100(struct connection *pc, const struct packet_city_change *packet)
{
  const struct packet_city_change *real_packet = packet;
  packet_city_change_100_fields fields;
  struct packet_city_change *old, *clone;
  bool differ, old_from_hash, force_send_of_unchanged = TRUE;
  struct hash_table **hash = &pc->phs.sent[PACKET_CITY_CHANGE];
  int different = 0;
  SEND_PACKET_START(PACKET_CITY_CHANGE);

  if (!*hash) {
    *hash = hash_new(hash_packet_city_change_100, cmp_packet_city_change_100);
  }
  BV_CLR_ALL(fields);

  old = hash_lookup_data(*hash, real_packet);
  old_from_hash = (old != NULL);
  if (!old) {
    old = fc_malloc(sizeof(*old));
    memset(old, 0, sizeof(*old));
    force_send_of_unchanged = TRUE;
  }

  differ = (old->city_id != real_packet->city_id);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 0);}

  differ = (old->build_id != real_packet->build_id);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 1);}

  differ = (old->is_build_id_unit_id != real_packet->is_build_id_unit_id);
  if(differ) {different++;}
  if(packet->is_build_id_unit_id) {BV_SET(fields, 2);}

  if (different == 0 && !force_send_of_unchanged) {
    return 0;
  }

  DIO_BV_PUT(&dout, fields);

  if (BV_ISSET(fields, 0)) {
    dio_put_uint16(&dout, real_packet->city_id);
  }
  if (BV_ISSET(fields, 1)) {
    dio_put_uint8(&dout, real_packet->build_id);
  }
  /* field 2 is folded into the header */


  if (old_from_hash) {
    hash_delete_entry(*hash, old);
  }

  clone = old;

  *clone = *real_packet;
  hash_insert(*hash, clone, clone);
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_city_change(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_CITY_CHANGE] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_CITY_CHANGE] = variant;
}

struct packet_city_change *receive_packet_city_change(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Receiving packet_city_change at the client.");
  }
  ensure_valid_variant_packet_city_change(pc);

  switch(pc->phs.variant[PACKET_CITY_CHANGE]) {
    case 100: return receive_packet_city_change_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_city_change(struct connection *pc, const struct packet_city_change *packet)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Sending packet_city_change from the server.");
  }
  ensure_valid_variant_packet_city_change(pc);

  switch(pc->phs.variant[PACKET_CITY_CHANGE]) {
    case 100: return send_packet_city_change_100(pc, packet);
    default: die("unknown variant"); return -1;
  }
}

int dsend_packet_city_change(struct connection *pc, int city_id, int build_id, bool is_build_id_unit_id)
{
  struct packet_city_change packet, *real_packet = &packet;

  real_packet->city_id = city_id;
  real_packet->build_id = build_id;
  real_packet->is_build_id_unit_id = is_build_id_unit_id;
  
  return send_packet_city_change(pc, real_packet);
}

#define hash_packet_city_worklist_100 hash_const

#define cmp_packet_city_worklist_100 cmp_const

BV_DEFINE(packet_city_worklist_100_fields, 2);

static struct packet_city_worklist *receive_packet_city_worklist_100(struct connection *pc, enum packet_type type)
{
  packet_city_worklist_100_fields fields;
  struct packet_city_worklist *old;
  struct hash_table **hash = &pc->phs.received[type];
  struct packet_city_worklist *clone;
  RECEIVE_PACKET_START(packet_city_worklist, real_packet);

  DIO_BV_GET(&din, fields);


  if (!*hash) {
    *hash = hash_new(hash_packet_city_worklist_100, cmp_packet_city_worklist_100);
  }
  old = hash_delete_entry(*hash, real_packet);

  if (old) {
    *real_packet = *old;
  } else {
    memset(real_packet, 0, sizeof(*real_packet));
  }

  if (BV_ISSET(fields, 0)) {
    {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->city_id = readin;
    }
  }
  if (BV_ISSET(fields, 1)) {
    dio_get_worklist(&din, &real_packet->worklist);
  }

  clone = fc_malloc(sizeof(*clone));
  *clone = *real_packet;
  if (old) {
    free(old);
  }
  hash_insert(*hash, clone, clone);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_city_worklist_100(struct connection *pc, const struct packet_city_worklist *packet)
{
  const struct packet_city_worklist *real_packet = packet;
  packet_city_worklist_100_fields fields;
  struct packet_city_worklist *old, *clone;
  bool differ, old_from_hash, force_send_of_unchanged = TRUE;
  struct hash_table **hash = &pc->phs.sent[PACKET_CITY_WORKLIST];
  int different = 0;
  SEND_PACKET_START(PACKET_CITY_WORKLIST);

  if (!*hash) {
    *hash = hash_new(hash_packet_city_worklist_100, cmp_packet_city_worklist_100);
  }
  BV_CLR_ALL(fields);

  old = hash_lookup_data(*hash, real_packet);
  old_from_hash = (old != NULL);
  if (!old) {
    old = fc_malloc(sizeof(*old));
    memset(old, 0, sizeof(*old));
    force_send_of_unchanged = TRUE;
  }

  differ = (old->city_id != real_packet->city_id);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 0);}

  differ = !are_worklists_equal(&old->worklist, &real_packet->worklist);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 1);}

  if (different == 0 && !force_send_of_unchanged) {
    return 0;
  }

  DIO_BV_PUT(&dout, fields);

  if (BV_ISSET(fields, 0)) {
    dio_put_uint16(&dout, real_packet->city_id);
  }
  if (BV_ISSET(fields, 1)) {
    dio_put_worklist(&dout, &real_packet->worklist);
  }


  if (old_from_hash) {
    hash_delete_entry(*hash, old);
  }

  clone = old;

  *clone = *real_packet;
  hash_insert(*hash, clone, clone);
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_city_worklist(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_CITY_WORKLIST] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_CITY_WORKLIST] = variant;
}

struct packet_city_worklist *receive_packet_city_worklist(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Receiving packet_city_worklist at the client.");
  }
  ensure_valid_variant_packet_city_worklist(pc);

  switch(pc->phs.variant[PACKET_CITY_WORKLIST]) {
    case 100: return receive_packet_city_worklist_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_city_worklist(struct connection *pc, const struct packet_city_worklist *packet)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Sending packet_city_worklist from the server.");
  }
  ensure_valid_variant_packet_city_worklist(pc);

  switch(pc->phs.variant[PACKET_CITY_WORKLIST]) {
    case 100: return send_packet_city_worklist_100(pc, packet);
    default: die("unknown variant"); return -1;
  }
}

int dsend_packet_city_worklist(struct connection *pc, int city_id, struct worklist *worklist)
{
  struct packet_city_worklist packet, *real_packet = &packet;

  real_packet->city_id = city_id;
  copy_worklist(&real_packet->worklist, worklist);
  
  return send_packet_city_worklist(pc, real_packet);
}

#define hash_packet_city_make_specialist_100 hash_const

#define cmp_packet_city_make_specialist_100 cmp_const

BV_DEFINE(packet_city_make_specialist_100_fields, 3);

static struct packet_city_make_specialist *receive_packet_city_make_specialist_100(struct connection *pc, enum packet_type type)
{
  packet_city_make_specialist_100_fields fields;
  struct packet_city_make_specialist *old;
  struct hash_table **hash = &pc->phs.received[type];
  struct packet_city_make_specialist *clone;
  RECEIVE_PACKET_START(packet_city_make_specialist, real_packet);

  DIO_BV_GET(&din, fields);


  if (!*hash) {
    *hash = hash_new(hash_packet_city_make_specialist_100, cmp_packet_city_make_specialist_100);
  }
  old = hash_delete_entry(*hash, real_packet);

  if (old) {
    *real_packet = *old;
  } else {
    memset(real_packet, 0, sizeof(*real_packet));
  }

  if (BV_ISSET(fields, 0)) {
    {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->city_id = readin;
    }
  }
  if (BV_ISSET(fields, 1)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->worker_x = readin;
    }
  }
  if (BV_ISSET(fields, 2)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->worker_y = readin;
    }
  }

  clone = fc_malloc(sizeof(*clone));
  *clone = *real_packet;
  if (old) {
    free(old);
  }
  hash_insert(*hash, clone, clone);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_city_make_specialist_100(struct connection *pc, const struct packet_city_make_specialist *packet)
{
  const struct packet_city_make_specialist *real_packet = packet;
  packet_city_make_specialist_100_fields fields;
  struct packet_city_make_specialist *old, *clone;
  bool differ, old_from_hash, force_send_of_unchanged = TRUE;
  struct hash_table **hash = &pc->phs.sent[PACKET_CITY_MAKE_SPECIALIST];
  int different = 0;
  SEND_PACKET_START(PACKET_CITY_MAKE_SPECIALIST);

  if (!*hash) {
    *hash = hash_new(hash_packet_city_make_specialist_100, cmp_packet_city_make_specialist_100);
  }
  BV_CLR_ALL(fields);

  old = hash_lookup_data(*hash, real_packet);
  old_from_hash = (old != NULL);
  if (!old) {
    old = fc_malloc(sizeof(*old));
    memset(old, 0, sizeof(*old));
    force_send_of_unchanged = TRUE;
  }

  differ = (old->city_id != real_packet->city_id);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 0);}

  differ = (old->worker_x != real_packet->worker_x);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 1);}

  differ = (old->worker_y != real_packet->worker_y);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 2);}

  if (different == 0 && !force_send_of_unchanged) {
    return 0;
  }

  DIO_BV_PUT(&dout, fields);

  if (BV_ISSET(fields, 0)) {
    dio_put_uint16(&dout, real_packet->city_id);
  }
  if (BV_ISSET(fields, 1)) {
    dio_put_uint8(&dout, real_packet->worker_x);
  }
  if (BV_ISSET(fields, 2)) {
    dio_put_uint8(&dout, real_packet->worker_y);
  }


  if (old_from_hash) {
    hash_delete_entry(*hash, old);
  }

  clone = old;

  *clone = *real_packet;
  hash_insert(*hash, clone, clone);
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_city_make_specialist(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_CITY_MAKE_SPECIALIST] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_CITY_MAKE_SPECIALIST] = variant;
}

struct packet_city_make_specialist *receive_packet_city_make_specialist(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Receiving packet_city_make_specialist at the client.");
  }
  ensure_valid_variant_packet_city_make_specialist(pc);

  switch(pc->phs.variant[PACKET_CITY_MAKE_SPECIALIST]) {
    case 100: return receive_packet_city_make_specialist_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_city_make_specialist(struct connection *pc, const struct packet_city_make_specialist *packet)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Sending packet_city_make_specialist from the server.");
  }
  ensure_valid_variant_packet_city_make_specialist(pc);

  switch(pc->phs.variant[PACKET_CITY_MAKE_SPECIALIST]) {
    case 100: return send_packet_city_make_specialist_100(pc, packet);
    default: die("unknown variant"); return -1;
  }
}

int dsend_packet_city_make_specialist(struct connection *pc, int city_id, int worker_x, int worker_y)
{
  struct packet_city_make_specialist packet, *real_packet = &packet;

  real_packet->city_id = city_id;
  real_packet->worker_x = worker_x;
  real_packet->worker_y = worker_y;
  
  return send_packet_city_make_specialist(pc, real_packet);
}

#define hash_packet_city_make_worker_100 hash_const

#define cmp_packet_city_make_worker_100 cmp_const

BV_DEFINE(packet_city_make_worker_100_fields, 3);

static struct packet_city_make_worker *receive_packet_city_make_worker_100(struct connection *pc, enum packet_type type)
{
  packet_city_make_worker_100_fields fields;
  struct packet_city_make_worker *old;
  struct hash_table **hash = &pc->phs.received[type];
  struct packet_city_make_worker *clone;
  RECEIVE_PACKET_START(packet_city_make_worker, real_packet);

  DIO_BV_GET(&din, fields);


  if (!*hash) {
    *hash = hash_new(hash_packet_city_make_worker_100, cmp_packet_city_make_worker_100);
  }
  old = hash_delete_entry(*hash, real_packet);

  if (old) {
    *real_packet = *old;
  } else {
    memset(real_packet, 0, sizeof(*real_packet));
  }

  if (BV_ISSET(fields, 0)) {
    {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->city_id = readin;
    }
  }
  if (BV_ISSET(fields, 1)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->worker_x = readin;
    }
  }
  if (BV_ISSET(fields, 2)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->worker_y = readin;
    }
  }

  clone = fc_malloc(sizeof(*clone));
  *clone = *real_packet;
  if (old) {
    free(old);
  }
  hash_insert(*hash, clone, clone);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_city_make_worker_100(struct connection *pc, const struct packet_city_make_worker *packet)
{
  const struct packet_city_make_worker *real_packet = packet;
  packet_city_make_worker_100_fields fields;
  struct packet_city_make_worker *old, *clone;
  bool differ, old_from_hash, force_send_of_unchanged = TRUE;
  struct hash_table **hash = &pc->phs.sent[PACKET_CITY_MAKE_WORKER];
  int different = 0;
  SEND_PACKET_START(PACKET_CITY_MAKE_WORKER);

  if (!*hash) {
    *hash = hash_new(hash_packet_city_make_worker_100, cmp_packet_city_make_worker_100);
  }
  BV_CLR_ALL(fields);

  old = hash_lookup_data(*hash, real_packet);
  old_from_hash = (old != NULL);
  if (!old) {
    old = fc_malloc(sizeof(*old));
    memset(old, 0, sizeof(*old));
    force_send_of_unchanged = TRUE;
  }

  differ = (old->city_id != real_packet->city_id);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 0);}

  differ = (old->worker_x != real_packet->worker_x);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 1);}

  differ = (old->worker_y != real_packet->worker_y);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 2);}

  if (different == 0 && !force_send_of_unchanged) {
    return 0;
  }

  DIO_BV_PUT(&dout, fields);

  if (BV_ISSET(fields, 0)) {
    dio_put_uint16(&dout, real_packet->city_id);
  }
  if (BV_ISSET(fields, 1)) {
    dio_put_uint8(&dout, real_packet->worker_x);
  }
  if (BV_ISSET(fields, 2)) {
    dio_put_uint8(&dout, real_packet->worker_y);
  }


  if (old_from_hash) {
    hash_delete_entry(*hash, old);
  }

  clone = old;

  *clone = *real_packet;
  hash_insert(*hash, clone, clone);
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_city_make_worker(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_CITY_MAKE_WORKER] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_CITY_MAKE_WORKER] = variant;
}

struct packet_city_make_worker *receive_packet_city_make_worker(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Receiving packet_city_make_worker at the client.");
  }
  ensure_valid_variant_packet_city_make_worker(pc);

  switch(pc->phs.variant[PACKET_CITY_MAKE_WORKER]) {
    case 100: return receive_packet_city_make_worker_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_city_make_worker(struct connection *pc, const struct packet_city_make_worker *packet)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Sending packet_city_make_worker from the server.");
  }
  ensure_valid_variant_packet_city_make_worker(pc);

  switch(pc->phs.variant[PACKET_CITY_MAKE_WORKER]) {
    case 100: return send_packet_city_make_worker_100(pc, packet);
    default: die("unknown variant"); return -1;
  }
}

int dsend_packet_city_make_worker(struct connection *pc, int city_id, int worker_x, int worker_y)
{
  struct packet_city_make_worker packet, *real_packet = &packet;

  real_packet->city_id = city_id;
  real_packet->worker_x = worker_x;
  real_packet->worker_y = worker_y;
  
  return send_packet_city_make_worker(pc, real_packet);
}

#define hash_packet_city_change_specialist_100 hash_const

#define cmp_packet_city_change_specialist_100 cmp_const

BV_DEFINE(packet_city_change_specialist_100_fields, 3);

static struct packet_city_change_specialist *receive_packet_city_change_specialist_100(struct connection *pc, enum packet_type type)
{
  packet_city_change_specialist_100_fields fields;
  struct packet_city_change_specialist *old;
  struct hash_table **hash = &pc->phs.received[type];
  struct packet_city_change_specialist *clone;
  RECEIVE_PACKET_START(packet_city_change_specialist, real_packet);

  DIO_BV_GET(&din, fields);


  if (!*hash) {
    *hash = hash_new(hash_packet_city_change_specialist_100, cmp_packet_city_change_specialist_100);
  }
  old = hash_delete_entry(*hash, real_packet);

  if (old) {
    *real_packet = *old;
  } else {
    memset(real_packet, 0, sizeof(*real_packet));
  }

  if (BV_ISSET(fields, 0)) {
    {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->city_id = readin;
    }
  }
  if (BV_ISSET(fields, 1)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->from = readin;
    }
  }
  if (BV_ISSET(fields, 2)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->to = readin;
    }
  }

  clone = fc_malloc(sizeof(*clone));
  *clone = *real_packet;
  if (old) {
    free(old);
  }
  hash_insert(*hash, clone, clone);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_city_change_specialist_100(struct connection *pc, const struct packet_city_change_specialist *packet)
{
  const struct packet_city_change_specialist *real_packet = packet;
  packet_city_change_specialist_100_fields fields;
  struct packet_city_change_specialist *old, *clone;
  bool differ, old_from_hash, force_send_of_unchanged = TRUE;
  struct hash_table **hash = &pc->phs.sent[PACKET_CITY_CHANGE_SPECIALIST];
  int different = 0;
  SEND_PACKET_START(PACKET_CITY_CHANGE_SPECIALIST);

  if (!*hash) {
    *hash = hash_new(hash_packet_city_change_specialist_100, cmp_packet_city_change_specialist_100);
  }
  BV_CLR_ALL(fields);

  old = hash_lookup_data(*hash, real_packet);
  old_from_hash = (old != NULL);
  if (!old) {
    old = fc_malloc(sizeof(*old));
    memset(old, 0, sizeof(*old));
    force_send_of_unchanged = TRUE;
  }

  differ = (old->city_id != real_packet->city_id);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 0);}

  differ = (old->from != real_packet->from);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 1);}

  differ = (old->to != real_packet->to);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 2);}

  if (different == 0 && !force_send_of_unchanged) {
    return 0;
  }

  DIO_BV_PUT(&dout, fields);

  if (BV_ISSET(fields, 0)) {
    dio_put_uint16(&dout, real_packet->city_id);
  }
  if (BV_ISSET(fields, 1)) {
    dio_put_uint8(&dout, real_packet->from);
  }
  if (BV_ISSET(fields, 2)) {
    dio_put_uint8(&dout, real_packet->to);
  }


  if (old_from_hash) {
    hash_delete_entry(*hash, old);
  }

  clone = old;

  *clone = *real_packet;
  hash_insert(*hash, clone, clone);
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_city_change_specialist(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_CITY_CHANGE_SPECIALIST] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_CITY_CHANGE_SPECIALIST] = variant;
}

struct packet_city_change_specialist *receive_packet_city_change_specialist(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Receiving packet_city_change_specialist at the client.");
  }
  ensure_valid_variant_packet_city_change_specialist(pc);

  switch(pc->phs.variant[PACKET_CITY_CHANGE_SPECIALIST]) {
    case 100: return receive_packet_city_change_specialist_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_city_change_specialist(struct connection *pc, const struct packet_city_change_specialist *packet)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Sending packet_city_change_specialist from the server.");
  }
  ensure_valid_variant_packet_city_change_specialist(pc);

  switch(pc->phs.variant[PACKET_CITY_CHANGE_SPECIALIST]) {
    case 100: return send_packet_city_change_specialist_100(pc, packet);
    default: die("unknown variant"); return -1;
  }
}

int dsend_packet_city_change_specialist(struct connection *pc, int city_id, Specialist_type_id from, Specialist_type_id to)
{
  struct packet_city_change_specialist packet, *real_packet = &packet;

  real_packet->city_id = city_id;
  real_packet->from = from;
  real_packet->to = to;
  
  return send_packet_city_change_specialist(pc, real_packet);
}

#define hash_packet_city_rename_100 hash_const

#define cmp_packet_city_rename_100 cmp_const

BV_DEFINE(packet_city_rename_100_fields, 2);

static struct packet_city_rename *receive_packet_city_rename_100(struct connection *pc, enum packet_type type)
{
  packet_city_rename_100_fields fields;
  struct packet_city_rename *old;
  struct hash_table **hash = &pc->phs.received[type];
  struct packet_city_rename *clone;
  RECEIVE_PACKET_START(packet_city_rename, real_packet);

  DIO_BV_GET(&din, fields);


  if (!*hash) {
    *hash = hash_new(hash_packet_city_rename_100, cmp_packet_city_rename_100);
  }
  old = hash_delete_entry(*hash, real_packet);

  if (old) {
    *real_packet = *old;
  } else {
    memset(real_packet, 0, sizeof(*real_packet));
  }

  if (BV_ISSET(fields, 0)) {
    {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->city_id = readin;
    }
  }
  if (BV_ISSET(fields, 1)) {
    dio_get_string(&din, real_packet->name, sizeof(real_packet->name));
  }

  clone = fc_malloc(sizeof(*clone));
  *clone = *real_packet;
  if (old) {
    free(old);
  }
  hash_insert(*hash, clone, clone);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_city_rename_100(struct connection *pc, const struct packet_city_rename *packet)
{
  const struct packet_city_rename *real_packet = packet;
  packet_city_rename_100_fields fields;
  struct packet_city_rename *old, *clone;
  bool differ, old_from_hash, force_send_of_unchanged = TRUE;
  struct hash_table **hash = &pc->phs.sent[PACKET_CITY_RENAME];
  int different = 0;
  SEND_PACKET_START(PACKET_CITY_RENAME);

  if (!*hash) {
    *hash = hash_new(hash_packet_city_rename_100, cmp_packet_city_rename_100);
  }
  BV_CLR_ALL(fields);

  old = hash_lookup_data(*hash, real_packet);
  old_from_hash = (old != NULL);
  if (!old) {
    old = fc_malloc(sizeof(*old));
    memset(old, 0, sizeof(*old));
    force_send_of_unchanged = TRUE;
  }

  differ = (old->city_id != real_packet->city_id);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 0);}

  differ = (strcmp(old->name, real_packet->name) != 0);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 1);}

  if (different == 0 && !force_send_of_unchanged) {
    return 0;
  }

  DIO_BV_PUT(&dout, fields);

  if (BV_ISSET(fields, 0)) {
    dio_put_uint16(&dout, real_packet->city_id);
  }
  if (BV_ISSET(fields, 1)) {
    dio_put_string(&dout, real_packet->name);
  }


  if (old_from_hash) {
    hash_delete_entry(*hash, old);
  }

  clone = old;

  *clone = *real_packet;
  hash_insert(*hash, clone, clone);
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_city_rename(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_CITY_RENAME] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_CITY_RENAME] = variant;
}

struct packet_city_rename *receive_packet_city_rename(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Receiving packet_city_rename at the client.");
  }
  ensure_valid_variant_packet_city_rename(pc);

  switch(pc->phs.variant[PACKET_CITY_RENAME]) {
    case 100: return receive_packet_city_rename_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_city_rename(struct connection *pc, const struct packet_city_rename *packet)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Sending packet_city_rename from the server.");
  }
  ensure_valid_variant_packet_city_rename(pc);

  switch(pc->phs.variant[PACKET_CITY_RENAME]) {
    case 100: return send_packet_city_rename_100(pc, packet);
    default: die("unknown variant"); return -1;
  }
}

int dsend_packet_city_rename(struct connection *pc, int city_id, const char *name)
{
  struct packet_city_rename packet, *real_packet = &packet;

  real_packet->city_id = city_id;
  sz_strlcpy(real_packet->name, name);
  
  return send_packet_city_rename(pc, real_packet);
}

#define hash_packet_city_options_req_100 hash_const

#define cmp_packet_city_options_req_100 cmp_const

BV_DEFINE(packet_city_options_req_100_fields, 2);

static struct packet_city_options_req *receive_packet_city_options_req_100(struct connection *pc, enum packet_type type)
{
  packet_city_options_req_100_fields fields;
  struct packet_city_options_req *old;
  struct hash_table **hash = &pc->phs.received[type];
  struct packet_city_options_req *clone;
  RECEIVE_PACKET_START(packet_city_options_req, real_packet);

  DIO_BV_GET(&din, fields);


  if (!*hash) {
    *hash = hash_new(hash_packet_city_options_req_100, cmp_packet_city_options_req_100);
  }
  old = hash_delete_entry(*hash, real_packet);

  if (old) {
    *real_packet = *old;
  } else {
    memset(real_packet, 0, sizeof(*real_packet));
  }

  if (BV_ISSET(fields, 0)) {
    {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->city_id = readin;
    }
  }
  if (BV_ISSET(fields, 1)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->value = readin;
    }
  }

  clone = fc_malloc(sizeof(*clone));
  *clone = *real_packet;
  if (old) {
    free(old);
  }
  hash_insert(*hash, clone, clone);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_city_options_req_100(struct connection *pc, const struct packet_city_options_req *packet)
{
  const struct packet_city_options_req *real_packet = packet;
  packet_city_options_req_100_fields fields;
  struct packet_city_options_req *old, *clone;
  bool differ, old_from_hash, force_send_of_unchanged = TRUE;
  struct hash_table **hash = &pc->phs.sent[PACKET_CITY_OPTIONS_REQ];
  int different = 0;
  SEND_PACKET_START(PACKET_CITY_OPTIONS_REQ);

  if (!*hash) {
    *hash = hash_new(hash_packet_city_options_req_100, cmp_packet_city_options_req_100);
  }
  BV_CLR_ALL(fields);

  old = hash_lookup_data(*hash, real_packet);
  old_from_hash = (old != NULL);
  if (!old) {
    old = fc_malloc(sizeof(*old));
    memset(old, 0, sizeof(*old));
    force_send_of_unchanged = TRUE;
  }

  differ = (old->city_id != real_packet->city_id);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 0);}

  differ = (old->value != real_packet->value);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 1);}

  if (different == 0 && !force_send_of_unchanged) {
    return 0;
  }

  DIO_BV_PUT(&dout, fields);

  if (BV_ISSET(fields, 0)) {
    dio_put_uint16(&dout, real_packet->city_id);
  }
  if (BV_ISSET(fields, 1)) {
    dio_put_uint8(&dout, real_packet->value);
  }


  if (old_from_hash) {
    hash_delete_entry(*hash, old);
  }

  clone = old;

  *clone = *real_packet;
  hash_insert(*hash, clone, clone);
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_city_options_req(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_CITY_OPTIONS_REQ] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_CITY_OPTIONS_REQ] = variant;
}

struct packet_city_options_req *receive_packet_city_options_req(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Receiving packet_city_options_req at the client.");
  }
  ensure_valid_variant_packet_city_options_req(pc);

  switch(pc->phs.variant[PACKET_CITY_OPTIONS_REQ]) {
    case 100: return receive_packet_city_options_req_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_city_options_req(struct connection *pc, const struct packet_city_options_req *packet)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Sending packet_city_options_req from the server.");
  }
  ensure_valid_variant_packet_city_options_req(pc);

  switch(pc->phs.variant[PACKET_CITY_OPTIONS_REQ]) {
    case 100: return send_packet_city_options_req_100(pc, packet);
    default: die("unknown variant"); return -1;
  }
}

int dsend_packet_city_options_req(struct connection *pc, int city_id, int value)
{
  struct packet_city_options_req packet, *real_packet = &packet;

  real_packet->city_id = city_id;
  real_packet->value = value;
  
  return send_packet_city_options_req(pc, real_packet);
}

#define hash_packet_city_refresh_100 hash_const

#define cmp_packet_city_refresh_100 cmp_const

BV_DEFINE(packet_city_refresh_100_fields, 1);

static struct packet_city_refresh *receive_packet_city_refresh_100(struct connection *pc, enum packet_type type)
{
  packet_city_refresh_100_fields fields;
  struct packet_city_refresh *old;
  struct hash_table **hash = &pc->phs.received[type];
  struct packet_city_refresh *clone;
  RECEIVE_PACKET_START(packet_city_refresh, real_packet);

  DIO_BV_GET(&din, fields);


  if (!*hash) {
    *hash = hash_new(hash_packet_city_refresh_100, cmp_packet_city_refresh_100);
  }
  old = hash_delete_entry(*hash, real_packet);

  if (old) {
    *real_packet = *old;
  } else {
    memset(real_packet, 0, sizeof(*real_packet));
  }

  if (BV_ISSET(fields, 0)) {
    {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->city_id = readin;
    }
  }

  clone = fc_malloc(sizeof(*clone));
  *clone = *real_packet;
  if (old) {
    free(old);
  }
  hash_insert(*hash, clone, clone);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_city_refresh_100(struct connection *pc, const struct packet_city_refresh *packet)
{
  const struct packet_city_refresh *real_packet = packet;
  packet_city_refresh_100_fields fields;
  struct packet_city_refresh *old, *clone;
  bool differ, old_from_hash, force_send_of_unchanged = TRUE;
  struct hash_table **hash = &pc->phs.sent[PACKET_CITY_REFRESH];
  int different = 0;
  SEND_PACKET_START(PACKET_CITY_REFRESH);

  if (!*hash) {
    *hash = hash_new(hash_packet_city_refresh_100, cmp_packet_city_refresh_100);
  }
  BV_CLR_ALL(fields);

  old = hash_lookup_data(*hash, real_packet);
  old_from_hash = (old != NULL);
  if (!old) {
    old = fc_malloc(sizeof(*old));
    memset(old, 0, sizeof(*old));
    force_send_of_unchanged = TRUE;
  }

  differ = (old->city_id != real_packet->city_id);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 0);}

  if (different == 0 && !force_send_of_unchanged) {
    return 0;
  }

  DIO_BV_PUT(&dout, fields);

  if (BV_ISSET(fields, 0)) {
    dio_put_uint16(&dout, real_packet->city_id);
  }


  if (old_from_hash) {
    hash_delete_entry(*hash, old);
  }

  clone = old;

  *clone = *real_packet;
  hash_insert(*hash, clone, clone);
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_city_refresh(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_CITY_REFRESH] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_CITY_REFRESH] = variant;
}

struct packet_city_refresh *receive_packet_city_refresh(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Receiving packet_city_refresh at the client.");
  }
  ensure_valid_variant_packet_city_refresh(pc);

  switch(pc->phs.variant[PACKET_CITY_REFRESH]) {
    case 100: return receive_packet_city_refresh_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_city_refresh(struct connection *pc, const struct packet_city_refresh *packet)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Sending packet_city_refresh from the server.");
  }
  ensure_valid_variant_packet_city_refresh(pc);

  switch(pc->phs.variant[PACKET_CITY_REFRESH]) {
    case 100: return send_packet_city_refresh_100(pc, packet);
    default: die("unknown variant"); return -1;
  }
}

int dsend_packet_city_refresh(struct connection *pc, int city_id)
{
  struct packet_city_refresh packet, *real_packet = &packet;

  real_packet->city_id = city_id;
  
  return send_packet_city_refresh(pc, real_packet);
}

#define hash_packet_city_incite_inq_100 hash_const

#define cmp_packet_city_incite_inq_100 cmp_const

BV_DEFINE(packet_city_incite_inq_100_fields, 1);

static struct packet_city_incite_inq *receive_packet_city_incite_inq_100(struct connection *pc, enum packet_type type)
{
  packet_city_incite_inq_100_fields fields;
  struct packet_city_incite_inq *old;
  struct hash_table **hash = &pc->phs.received[type];
  struct packet_city_incite_inq *clone;
  RECEIVE_PACKET_START(packet_city_incite_inq, real_packet);

  DIO_BV_GET(&din, fields);


  if (!*hash) {
    *hash = hash_new(hash_packet_city_incite_inq_100, cmp_packet_city_incite_inq_100);
  }
  old = hash_delete_entry(*hash, real_packet);

  if (old) {
    *real_packet = *old;
  } else {
    memset(real_packet, 0, sizeof(*real_packet));
  }

  if (BV_ISSET(fields, 0)) {
    {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->city_id = readin;
    }
  }

  clone = fc_malloc(sizeof(*clone));
  *clone = *real_packet;
  if (old) {
    free(old);
  }
  hash_insert(*hash, clone, clone);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_city_incite_inq_100(struct connection *pc, const struct packet_city_incite_inq *packet)
{
  const struct packet_city_incite_inq *real_packet = packet;
  packet_city_incite_inq_100_fields fields;
  struct packet_city_incite_inq *old, *clone;
  bool differ, old_from_hash, force_send_of_unchanged = TRUE;
  struct hash_table **hash = &pc->phs.sent[PACKET_CITY_INCITE_INQ];
  int different = 0;
  SEND_PACKET_START(PACKET_CITY_INCITE_INQ);

  if (!*hash) {
    *hash = hash_new(hash_packet_city_incite_inq_100, cmp_packet_city_incite_inq_100);
  }
  BV_CLR_ALL(fields);

  old = hash_lookup_data(*hash, real_packet);
  old_from_hash = (old != NULL);
  if (!old) {
    old = fc_malloc(sizeof(*old));
    memset(old, 0, sizeof(*old));
    force_send_of_unchanged = TRUE;
  }

  differ = (old->city_id != real_packet->city_id);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 0);}

  if (different == 0 && !force_send_of_unchanged) {
    return 0;
  }

  DIO_BV_PUT(&dout, fields);

  if (BV_ISSET(fields, 0)) {
    dio_put_uint16(&dout, real_packet->city_id);
  }


  if (old_from_hash) {
    hash_delete_entry(*hash, old);
  }

  clone = old;

  *clone = *real_packet;
  hash_insert(*hash, clone, clone);
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_city_incite_inq(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_CITY_INCITE_INQ] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_CITY_INCITE_INQ] = variant;
}

struct packet_city_incite_inq *receive_packet_city_incite_inq(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Receiving packet_city_incite_inq at the client.");
  }
  ensure_valid_variant_packet_city_incite_inq(pc);

  switch(pc->phs.variant[PACKET_CITY_INCITE_INQ]) {
    case 100: return receive_packet_city_incite_inq_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_city_incite_inq(struct connection *pc, const struct packet_city_incite_inq *packet)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Sending packet_city_incite_inq from the server.");
  }
  ensure_valid_variant_packet_city_incite_inq(pc);

  switch(pc->phs.variant[PACKET_CITY_INCITE_INQ]) {
    case 100: return send_packet_city_incite_inq_100(pc, packet);
    default: die("unknown variant"); return -1;
  }
}

int dsend_packet_city_incite_inq(struct connection *pc, int city_id)
{
  struct packet_city_incite_inq packet, *real_packet = &packet;

  real_packet->city_id = city_id;
  
  return send_packet_city_incite_inq(pc, real_packet);
}

#define hash_packet_city_incite_info_100 hash_const

#define cmp_packet_city_incite_info_100 cmp_const

BV_DEFINE(packet_city_incite_info_100_fields, 2);

static struct packet_city_incite_info *receive_packet_city_incite_info_100(struct connection *pc, enum packet_type type)
{
  packet_city_incite_info_100_fields fields;
  struct packet_city_incite_info *old;
  struct hash_table **hash = &pc->phs.received[type];
  struct packet_city_incite_info *clone;
  RECEIVE_PACKET_START(packet_city_incite_info, real_packet);

  DIO_BV_GET(&din, fields);


  if (!*hash) {
    *hash = hash_new(hash_packet_city_incite_info_100, cmp_packet_city_incite_info_100);
  }
  old = hash_delete_entry(*hash, real_packet);

  if (old) {
    *real_packet = *old;
  } else {
    memset(real_packet, 0, sizeof(*real_packet));
  }

  if (BV_ISSET(fields, 0)) {
    {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->city_id = readin;
    }
  }
  if (BV_ISSET(fields, 1)) {
    {
      int readin;
    
      dio_get_uint32(&din, &readin);
      real_packet->cost = readin;
    }
  }

  clone = fc_malloc(sizeof(*clone));
  *clone = *real_packet;
  if (old) {
    free(old);
  }
  hash_insert(*hash, clone, clone);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_city_incite_info_100(struct connection *pc, const struct packet_city_incite_info *packet)
{
  const struct packet_city_incite_info *real_packet = packet;
  packet_city_incite_info_100_fields fields;
  struct packet_city_incite_info *old, *clone;
  bool differ, old_from_hash, force_send_of_unchanged = TRUE;
  struct hash_table **hash = &pc->phs.sent[PACKET_CITY_INCITE_INFO];
  int different = 0;
  SEND_PACKET_START(PACKET_CITY_INCITE_INFO);

  if (!*hash) {
    *hash = hash_new(hash_packet_city_incite_info_100, cmp_packet_city_incite_info_100);
  }
  BV_CLR_ALL(fields);

  old = hash_lookup_data(*hash, real_packet);
  old_from_hash = (old != NULL);
  if (!old) {
    old = fc_malloc(sizeof(*old));
    memset(old, 0, sizeof(*old));
    force_send_of_unchanged = TRUE;
  }

  differ = (old->city_id != real_packet->city_id);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 0);}

  differ = (old->cost != real_packet->cost);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 1);}

  if (different == 0 && !force_send_of_unchanged) {
    return 0;
  }

  DIO_BV_PUT(&dout, fields);

  if (BV_ISSET(fields, 0)) {
    dio_put_uint16(&dout, real_packet->city_id);
  }
  if (BV_ISSET(fields, 1)) {
    dio_put_uint32(&dout, real_packet->cost);
  }


  if (old_from_hash) {
    hash_delete_entry(*hash, old);
  }

  clone = old;

  *clone = *real_packet;
  hash_insert(*hash, clone, clone);
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_city_incite_info(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_CITY_INCITE_INFO] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_CITY_INCITE_INFO] = variant;
}

struct packet_city_incite_info *receive_packet_city_incite_info(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Receiving packet_city_incite_info at the server.");
  }
  ensure_valid_variant_packet_city_incite_info(pc);

  switch(pc->phs.variant[PACKET_CITY_INCITE_INFO]) {
    case 100: return receive_packet_city_incite_info_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_city_incite_info(struct connection *pc, const struct packet_city_incite_info *packet)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Sending packet_city_incite_info from the client.");
  }
  ensure_valid_variant_packet_city_incite_info(pc);

  switch(pc->phs.variant[PACKET_CITY_INCITE_INFO]) {
    case 100: return send_packet_city_incite_info_100(pc, packet);
    default: die("unknown variant"); return -1;
  }
}

int dsend_packet_city_incite_info(struct connection *pc, int city_id, int cost)
{
  struct packet_city_incite_info packet, *real_packet = &packet;

  real_packet->city_id = city_id;
  real_packet->cost = cost;
  
  return send_packet_city_incite_info(pc, real_packet);
}

#define hash_packet_city_name_suggestion_req_100 hash_const

#define cmp_packet_city_name_suggestion_req_100 cmp_const

BV_DEFINE(packet_city_name_suggestion_req_100_fields, 1);

static struct packet_city_name_suggestion_req *receive_packet_city_name_suggestion_req_100(struct connection *pc, enum packet_type type)
{
  packet_city_name_suggestion_req_100_fields fields;
  struct packet_city_name_suggestion_req *old;
  struct hash_table **hash = &pc->phs.received[type];
  struct packet_city_name_suggestion_req *clone;
  RECEIVE_PACKET_START(packet_city_name_suggestion_req, real_packet);

  DIO_BV_GET(&din, fields);


  if (!*hash) {
    *hash = hash_new(hash_packet_city_name_suggestion_req_100, cmp_packet_city_name_suggestion_req_100);
  }
  old = hash_delete_entry(*hash, real_packet);

  if (old) {
    *real_packet = *old;
  } else {
    memset(real_packet, 0, sizeof(*real_packet));
  }

  if (BV_ISSET(fields, 0)) {
    {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->unit_id = readin;
    }
  }

  clone = fc_malloc(sizeof(*clone));
  *clone = *real_packet;
  if (old) {
    free(old);
  }
  hash_insert(*hash, clone, clone);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_city_name_suggestion_req_100(struct connection *pc, const struct packet_city_name_suggestion_req *packet)
{
  const struct packet_city_name_suggestion_req *real_packet = packet;
  packet_city_name_suggestion_req_100_fields fields;
  struct packet_city_name_suggestion_req *old, *clone;
  bool differ, old_from_hash, force_send_of_unchanged = TRUE;
  struct hash_table **hash = &pc->phs.sent[PACKET_CITY_NAME_SUGGESTION_REQ];
  int different = 0;
  SEND_PACKET_START(PACKET_CITY_NAME_SUGGESTION_REQ);

  if (!*hash) {
    *hash = hash_new(hash_packet_city_name_suggestion_req_100, cmp_packet_city_name_suggestion_req_100);
  }
  BV_CLR_ALL(fields);

  old = hash_lookup_data(*hash, real_packet);
  old_from_hash = (old != NULL);
  if (!old) {
    old = fc_malloc(sizeof(*old));
    memset(old, 0, sizeof(*old));
    force_send_of_unchanged = TRUE;
  }

  differ = (old->unit_id != real_packet->unit_id);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 0);}

  if (different == 0 && !force_send_of_unchanged) {
    return 0;
  }

  DIO_BV_PUT(&dout, fields);

  if (BV_ISSET(fields, 0)) {
    dio_put_uint16(&dout, real_packet->unit_id);
  }


  if (old_from_hash) {
    hash_delete_entry(*hash, old);
  }

  clone = old;

  *clone = *real_packet;
  hash_insert(*hash, clone, clone);
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_city_name_suggestion_req(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_CITY_NAME_SUGGESTION_REQ] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_CITY_NAME_SUGGESTION_REQ] = variant;
}

struct packet_city_name_suggestion_req *receive_packet_city_name_suggestion_req(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Receiving packet_city_name_suggestion_req at the client.");
  }
  ensure_valid_variant_packet_city_name_suggestion_req(pc);

  switch(pc->phs.variant[PACKET_CITY_NAME_SUGGESTION_REQ]) {
    case 100: return receive_packet_city_name_suggestion_req_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_city_name_suggestion_req(struct connection *pc, const struct packet_city_name_suggestion_req *packet)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Sending packet_city_name_suggestion_req from the server.");
  }
  ensure_valid_variant_packet_city_name_suggestion_req(pc);

  switch(pc->phs.variant[PACKET_CITY_NAME_SUGGESTION_REQ]) {
    case 100: return send_packet_city_name_suggestion_req_100(pc, packet);
    default: die("unknown variant"); return -1;
  }
}

int dsend_packet_city_name_suggestion_req(struct connection *pc, int unit_id)
{
  struct packet_city_name_suggestion_req packet, *real_packet = &packet;

  real_packet->unit_id = unit_id;
  
  return send_packet_city_name_suggestion_req(pc, real_packet);
}

#define hash_packet_city_name_suggestion_info_100 hash_const

#define cmp_packet_city_name_suggestion_info_100 cmp_const

BV_DEFINE(packet_city_name_suggestion_info_100_fields, 2);

static struct packet_city_name_suggestion_info *receive_packet_city_name_suggestion_info_100(struct connection *pc, enum packet_type type)
{
  packet_city_name_suggestion_info_100_fields fields;
  struct packet_city_name_suggestion_info *old;
  struct hash_table **hash = &pc->phs.received[type];
  struct packet_city_name_suggestion_info *clone;
  RECEIVE_PACKET_START(packet_city_name_suggestion_info, real_packet);

  DIO_BV_GET(&din, fields);


  if (!*hash) {
    *hash = hash_new(hash_packet_city_name_suggestion_info_100, cmp_packet_city_name_suggestion_info_100);
  }
  old = hash_delete_entry(*hash, real_packet);

  if (old) {
    *real_packet = *old;
  } else {
    memset(real_packet, 0, sizeof(*real_packet));
  }

  if (BV_ISSET(fields, 0)) {
    {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->unit_id = readin;
    }
  }
  if (BV_ISSET(fields, 1)) {
    dio_get_string(&din, real_packet->name, sizeof(real_packet->name));
  }

  clone = fc_malloc(sizeof(*clone));
  *clone = *real_packet;
  if (old) {
    free(old);
  }
  hash_insert(*hash, clone, clone);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_city_name_suggestion_info_100(struct connection *pc, const struct packet_city_name_suggestion_info *packet)
{
  const struct packet_city_name_suggestion_info *real_packet = packet;
  packet_city_name_suggestion_info_100_fields fields;
  struct packet_city_name_suggestion_info *old, *clone;
  bool differ, old_from_hash, force_send_of_unchanged = TRUE;
  struct hash_table **hash = &pc->phs.sent[PACKET_CITY_NAME_SUGGESTION_INFO];
  int different = 0;
  SEND_PACKET_START(PACKET_CITY_NAME_SUGGESTION_INFO);

  if (!*hash) {
    *hash = hash_new(hash_packet_city_name_suggestion_info_100, cmp_packet_city_name_suggestion_info_100);
  }
  BV_CLR_ALL(fields);

  old = hash_lookup_data(*hash, real_packet);
  old_from_hash = (old != NULL);
  if (!old) {
    old = fc_malloc(sizeof(*old));
    memset(old, 0, sizeof(*old));
    force_send_of_unchanged = TRUE;
  }

  differ = (old->unit_id != real_packet->unit_id);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 0);}

  differ = (strcmp(old->name, real_packet->name) != 0);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 1);}

  if (different == 0 && !force_send_of_unchanged) {
    return 0;
  }

  DIO_BV_PUT(&dout, fields);

  if (BV_ISSET(fields, 0)) {
    dio_put_uint16(&dout, real_packet->unit_id);
  }
  if (BV_ISSET(fields, 1)) {
    dio_put_string(&dout, real_packet->name);
  }


  if (old_from_hash) {
    hash_delete_entry(*hash, old);
  }

  clone = old;

  *clone = *real_packet;
  hash_insert(*hash, clone, clone);
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_city_name_suggestion_info(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_CITY_NAME_SUGGESTION_INFO] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_CITY_NAME_SUGGESTION_INFO] = variant;
}

struct packet_city_name_suggestion_info *receive_packet_city_name_suggestion_info(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Receiving packet_city_name_suggestion_info at the server.");
  }
  ensure_valid_variant_packet_city_name_suggestion_info(pc);

  switch(pc->phs.variant[PACKET_CITY_NAME_SUGGESTION_INFO]) {
    case 100: return receive_packet_city_name_suggestion_info_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_city_name_suggestion_info(struct connection *pc, const struct packet_city_name_suggestion_info *packet)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Sending packet_city_name_suggestion_info from the client.");
  }
  ensure_valid_variant_packet_city_name_suggestion_info(pc);

  switch(pc->phs.variant[PACKET_CITY_NAME_SUGGESTION_INFO]) {
    case 100: return send_packet_city_name_suggestion_info_100(pc, packet);
    default: die("unknown variant"); return -1;
  }
}

void lsend_packet_city_name_suggestion_info(struct conn_list *dest, const struct packet_city_name_suggestion_info *packet)
{
  conn_list_iterate(*dest, pconn) {
    send_packet_city_name_suggestion_info(pconn, packet);
  } conn_list_iterate_end;
}

int dsend_packet_city_name_suggestion_info(struct connection *pc, int unit_id, const char *name)
{
  struct packet_city_name_suggestion_info packet, *real_packet = &packet;

  real_packet->unit_id = unit_id;
  sz_strlcpy(real_packet->name, name);
  
  return send_packet_city_name_suggestion_info(pc, real_packet);
}

void dlsend_packet_city_name_suggestion_info(struct conn_list *dest, int unit_id, const char *name)
{
  struct packet_city_name_suggestion_info packet, *real_packet = &packet;

  real_packet->unit_id = unit_id;
  sz_strlcpy(real_packet->name, name);
  
  lsend_packet_city_name_suggestion_info(dest, real_packet);
}

#define hash_packet_city_sabotage_list_100 hash_const

#define cmp_packet_city_sabotage_list_100 cmp_const

BV_DEFINE(packet_city_sabotage_list_100_fields, 3);

static struct packet_city_sabotage_list *receive_packet_city_sabotage_list_100(struct connection *pc, enum packet_type type)
{
  packet_city_sabotage_list_100_fields fields;
  struct packet_city_sabotage_list *old;
  struct hash_table **hash = &pc->phs.received[type];
  struct packet_city_sabotage_list *clone;
  RECEIVE_PACKET_START(packet_city_sabotage_list, real_packet);

  DIO_BV_GET(&din, fields);


  if (!*hash) {
    *hash = hash_new(hash_packet_city_sabotage_list_100, cmp_packet_city_sabotage_list_100);
  }
  old = hash_delete_entry(*hash, real_packet);

  if (old) {
    *real_packet = *old;
  } else {
    memset(real_packet, 0, sizeof(*real_packet));
  }

  if (BV_ISSET(fields, 0)) {
    {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->diplomat_id = readin;
    }
  }
  if (BV_ISSET(fields, 1)) {
    {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->city_id = readin;
    }
  }
  if (BV_ISSET(fields, 2)) {
    dio_get_bit_string(&din, real_packet->improvements, sizeof(real_packet->improvements));
  }

  clone = fc_malloc(sizeof(*clone));
  *clone = *real_packet;
  if (old) {
    free(old);
  }
  hash_insert(*hash, clone, clone);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_city_sabotage_list_100(struct connection *pc, const struct packet_city_sabotage_list *packet)
{
  const struct packet_city_sabotage_list *real_packet = packet;
  packet_city_sabotage_list_100_fields fields;
  struct packet_city_sabotage_list *old, *clone;
  bool differ, old_from_hash, force_send_of_unchanged = TRUE;
  struct hash_table **hash = &pc->phs.sent[PACKET_CITY_SABOTAGE_LIST];
  int different = 0;
  SEND_PACKET_START(PACKET_CITY_SABOTAGE_LIST);

  if (!*hash) {
    *hash = hash_new(hash_packet_city_sabotage_list_100, cmp_packet_city_sabotage_list_100);
  }
  BV_CLR_ALL(fields);

  old = hash_lookup_data(*hash, real_packet);
  old_from_hash = (old != NULL);
  if (!old) {
    old = fc_malloc(sizeof(*old));
    memset(old, 0, sizeof(*old));
    force_send_of_unchanged = TRUE;
  }

  differ = (old->diplomat_id != real_packet->diplomat_id);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 0);}

  differ = (old->city_id != real_packet->city_id);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 1);}

  differ = (strcmp(old->improvements, real_packet->improvements) != 0);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 2);}

  if (different == 0 && !force_send_of_unchanged) {
    return 0;
  }

  DIO_BV_PUT(&dout, fields);

  if (BV_ISSET(fields, 0)) {
    dio_put_uint16(&dout, real_packet->diplomat_id);
  }
  if (BV_ISSET(fields, 1)) {
    dio_put_uint16(&dout, real_packet->city_id);
  }
  if (BV_ISSET(fields, 2)) {
    dio_put_bit_string(&dout, real_packet->improvements);
  }


  if (old_from_hash) {
    hash_delete_entry(*hash, old);
  }

  clone = old;

  *clone = *real_packet;
  hash_insert(*hash, clone, clone);
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_city_sabotage_list(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_CITY_SABOTAGE_LIST] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_CITY_SABOTAGE_LIST] = variant;
}

struct packet_city_sabotage_list *receive_packet_city_sabotage_list(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Receiving packet_city_sabotage_list at the server.");
  }
  ensure_valid_variant_packet_city_sabotage_list(pc);

  switch(pc->phs.variant[PACKET_CITY_SABOTAGE_LIST]) {
    case 100: return receive_packet_city_sabotage_list_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_city_sabotage_list(struct connection *pc, const struct packet_city_sabotage_list *packet)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Sending packet_city_sabotage_list from the client.");
  }
  ensure_valid_variant_packet_city_sabotage_list(pc);

  switch(pc->phs.variant[PACKET_CITY_SABOTAGE_LIST]) {
    case 100: return send_packet_city_sabotage_list_100(pc, packet);
    default: die("unknown variant"); return -1;
  }
}

void lsend_packet_city_sabotage_list(struct conn_list *dest, const struct packet_city_sabotage_list *packet)
{
  conn_list_iterate(*dest, pconn) {
    send_packet_city_sabotage_list(pconn, packet);
  } conn_list_iterate_end;
}

#define hash_packet_player_remove_100 hash_const

#define cmp_packet_player_remove_100 cmp_const

BV_DEFINE(packet_player_remove_100_fields, 1);

static struct packet_player_remove *receive_packet_player_remove_100(struct connection *pc, enum packet_type type)
{
  packet_player_remove_100_fields fields;
  struct packet_player_remove *old;
  struct hash_table **hash = &pc->phs.received[type];
  struct packet_player_remove *clone;
  RECEIVE_PACKET_START(packet_player_remove, real_packet);

  DIO_BV_GET(&din, fields);


  if (!*hash) {
    *hash = hash_new(hash_packet_player_remove_100, cmp_packet_player_remove_100);
  }
  old = hash_delete_entry(*hash, real_packet);

  if (old) {
    *real_packet = *old;
  } else {
    memset(real_packet, 0, sizeof(*real_packet));
  }

  if (BV_ISSET(fields, 0)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->player_id = readin;
    }
  }

  clone = fc_malloc(sizeof(*clone));
  *clone = *real_packet;
  if (old) {
    free(old);
  }
  hash_insert(*hash, clone, clone);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_player_remove_100(struct connection *pc, const struct packet_player_remove *packet)
{
  const struct packet_player_remove *real_packet = packet;
  packet_player_remove_100_fields fields;
  struct packet_player_remove *old, *clone;
  bool differ, old_from_hash, force_send_of_unchanged = TRUE;
  struct hash_table **hash = &pc->phs.sent[PACKET_PLAYER_REMOVE];
  int different = 0;
  SEND_PACKET_START(PACKET_PLAYER_REMOVE);

  if (!*hash) {
    *hash = hash_new(hash_packet_player_remove_100, cmp_packet_player_remove_100);
  }
  BV_CLR_ALL(fields);

  old = hash_lookup_data(*hash, real_packet);
  old_from_hash = (old != NULL);
  if (!old) {
    old = fc_malloc(sizeof(*old));
    memset(old, 0, sizeof(*old));
    force_send_of_unchanged = TRUE;
  }

  differ = (old->player_id != real_packet->player_id);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 0);}

  if (different == 0 && !force_send_of_unchanged) {
    return 0;
  }

  DIO_BV_PUT(&dout, fields);

  if (BV_ISSET(fields, 0)) {
    dio_put_uint8(&dout, real_packet->player_id);
  }


  if (old_from_hash) {
    hash_delete_entry(*hash, old);
  }

  clone = old;

  *clone = *real_packet;
  hash_insert(*hash, clone, clone);
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_player_remove(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_PLAYER_REMOVE] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_PLAYER_REMOVE] = variant;
}

struct packet_player_remove *receive_packet_player_remove(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Receiving packet_player_remove at the server.");
  }
  ensure_valid_variant_packet_player_remove(pc);

  switch(pc->phs.variant[PACKET_PLAYER_REMOVE]) {
    case 100: return receive_packet_player_remove_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_player_remove(struct connection *pc, const struct packet_player_remove *packet)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Sending packet_player_remove from the client.");
  }
  ensure_valid_variant_packet_player_remove(pc);

  switch(pc->phs.variant[PACKET_PLAYER_REMOVE]) {
    case 100: return send_packet_player_remove_100(pc, packet);
    default: die("unknown variant"); return -1;
  }
}

void lsend_packet_player_remove(struct conn_list *dest, const struct packet_player_remove *packet)
{
  conn_list_iterate(*dest, pconn) {
    send_packet_player_remove(pconn, packet);
  } conn_list_iterate_end;
}

int dsend_packet_player_remove(struct connection *pc, int player_id)
{
  struct packet_player_remove packet, *real_packet = &packet;

  real_packet->player_id = player_id;
  
  return send_packet_player_remove(pc, real_packet);
}

void dlsend_packet_player_remove(struct conn_list *dest, int player_id)
{
  struct packet_player_remove packet, *real_packet = &packet;

  real_packet->player_id = player_id;
  
  lsend_packet_player_remove(dest, real_packet);
}

static unsigned int hash_packet_player_info_100(const void *vkey, unsigned int num_buckets)
{
  const struct packet_player_info *key = (const struct packet_player_info *) vkey;

  return ((key->playerno) % num_buckets);
}

static int cmp_packet_player_info_100(const void *vkey1, const void *vkey2)
{
  const struct packet_player_info *key1 = (const struct packet_player_info *) vkey1;
  const struct packet_player_info *key2 = (const struct packet_player_info *) vkey2;
  int diff;

  diff = key1->playerno - key2->playerno;
  if (diff != 0) {
    return diff;
  }

  return 0;
}

BV_DEFINE(packet_player_info_100_fields, 31);

static struct packet_player_info *receive_packet_player_info_100(struct connection *pc, enum packet_type type)
{
  packet_player_info_100_fields fields;
  struct packet_player_info *old;
  struct hash_table **hash = &pc->phs.received[type];
  struct packet_player_info *clone;
  RECEIVE_PACKET_START(packet_player_info, real_packet);

  DIO_BV_GET(&din, fields);
  {
    int readin;
  
    dio_get_uint8(&din, &readin);
    real_packet->playerno = readin;
  }


  if (!*hash) {
    *hash = hash_new(hash_packet_player_info_100, cmp_packet_player_info_100);
  }
  old = hash_delete_entry(*hash, real_packet);

  if (old) {
    *real_packet = *old;
  } else {
    int playerno = real_packet->playerno;

    memset(real_packet, 0, sizeof(*real_packet));

    real_packet->playerno = playerno;
  }

  if (BV_ISSET(fields, 0)) {
    dio_get_string(&din, real_packet->name, sizeof(real_packet->name));
  }
  if (BV_ISSET(fields, 1)) {
    dio_get_string(&din, real_packet->username, sizeof(real_packet->username));
  }
  real_packet->is_male = BV_ISSET(fields, 2);
  if (BV_ISSET(fields, 3)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->government = readin;
    }
  }
  if (BV_ISSET(fields, 4)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->target_government = readin;
    }
  }
  if (BV_ISSET(fields, 5)) {
    {
      int readin;
    
      dio_get_uint32(&din, &readin);
      real_packet->embassy = readin;
    }
  }
  if (BV_ISSET(fields, 6)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->city_style = readin;
    }
  }
  if (BV_ISSET(fields, 7)) {
    {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->nation = readin;
    }
  }
  if (BV_ISSET(fields, 8)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->team = readin;
    }
  }
  real_packet->turn_done = BV_ISSET(fields, 9);
  if (BV_ISSET(fields, 10)) {
    {
      int readin;
    
      dio_get_sint16(&din, &readin);
      real_packet->nturns_idle = readin;
    }
  }
  real_packet->is_alive = BV_ISSET(fields, 11);
  if (BV_ISSET(fields, 12)) {
    {
      int readin;
    
      dio_get_uint32(&din, &readin);
      real_packet->reputation = readin;
    }
  }
  if (BV_ISSET(fields, 13)) {
    
    {
      int i;
    
      for (i = 0; i < MAX_NUM_PLAYERS + MAX_NUM_BARBARIANS; i++) {
        dio_get_diplstate(&din, &real_packet->diplstates[i]);
      }
    }
  }
  if (BV_ISSET(fields, 14)) {
    {
      int readin;
    
      dio_get_uint32(&din, &readin);
      real_packet->gold = readin;
    }
  }
  if (BV_ISSET(fields, 15)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->tax = readin;
    }
  }
  if (BV_ISSET(fields, 16)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->science = readin;
    }
  }
  if (BV_ISSET(fields, 17)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->luxury = readin;
    }
  }
  if (BV_ISSET(fields, 18)) {
    {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->bulbs_last_turn = readin;
    }
  }
  if (BV_ISSET(fields, 19)) {
    {
      int readin;
    
      dio_get_uint32(&din, &readin);
      real_packet->bulbs_researched = readin;
    }
  }
  if (BV_ISSET(fields, 20)) {
    {
      int readin;
    
      dio_get_uint32(&din, &readin);
      real_packet->techs_researched = readin;
    }
  }
  if (BV_ISSET(fields, 21)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->researching = readin;
    }
  }
  if (BV_ISSET(fields, 22)) {
    {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->future_tech = readin;
    }
  }
  if (BV_ISSET(fields, 23)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->tech_goal = readin;
    }
  }
  real_packet->is_connected = BV_ISSET(fields, 24);
  if (BV_ISSET(fields, 25)) {
    {
      int readin;
    
      dio_get_sint16(&din, &readin);
      real_packet->revolution_finishes = readin;
    }
  }
  real_packet->ai = BV_ISSET(fields, 26);
  if (BV_ISSET(fields, 27)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->barbarian_type = readin;
    }
  }
  if (BV_ISSET(fields, 28)) {
    {
      int readin;
    
      dio_get_uint32(&din, &readin);
      real_packet->gives_shared_vision = readin;
    }
  }
  if (BV_ISSET(fields, 29)) {
    dio_get_bit_string(&din, real_packet->inventions, sizeof(real_packet->inventions));
  }
  if (BV_ISSET(fields, 30)) {
    
    {
      int i;
    
      for (i = 0; i < MAX_NUM_PLAYERS + MAX_NUM_BARBARIANS; i++) {
        {
      int readin;
    
      dio_get_sint16(&din, &readin);
      real_packet->love[i] = readin;
    }
      }
    }
  }

  clone = fc_malloc(sizeof(*clone));
  *clone = *real_packet;
  if (old) {
    free(old);
  }
  hash_insert(*hash, clone, clone);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_player_info_100(struct connection *pc, const struct packet_player_info *packet)
{
  const struct packet_player_info *real_packet = packet;
  packet_player_info_100_fields fields;
  struct packet_player_info *old, *clone;
  bool differ, old_from_hash, force_send_of_unchanged = FALSE;
  struct hash_table **hash = &pc->phs.sent[PACKET_PLAYER_INFO];
  int different = 0;
  SEND_PACKET_START(PACKET_PLAYER_INFO);

  if (!*hash) {
    *hash = hash_new(hash_packet_player_info_100, cmp_packet_player_info_100);
  }
  BV_CLR_ALL(fields);

  old = hash_lookup_data(*hash, real_packet);
  old_from_hash = (old != NULL);
  if (!old) {
    old = fc_malloc(sizeof(*old));
    memset(old, 0, sizeof(*old));
    force_send_of_unchanged = TRUE;
  }

  differ = (strcmp(old->name, real_packet->name) != 0);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 0);}

  differ = (strcmp(old->username, real_packet->username) != 0);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 1);}

  differ = (old->is_male != real_packet->is_male);
  if(differ) {different++;}
  if(packet->is_male) {BV_SET(fields, 2);}

  differ = (old->government != real_packet->government);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 3);}

  differ = (old->target_government != real_packet->target_government);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 4);}

  differ = (old->embassy != real_packet->embassy);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 5);}

  differ = (old->city_style != real_packet->city_style);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 6);}

  differ = (old->nation != real_packet->nation);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 7);}

  differ = (old->team != real_packet->team);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 8);}

  differ = (old->turn_done != real_packet->turn_done);
  if(differ) {different++;}
  if(packet->turn_done) {BV_SET(fields, 9);}

  differ = (old->nturns_idle != real_packet->nturns_idle);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 10);}

  differ = (old->is_alive != real_packet->is_alive);
  if(differ) {different++;}
  if(packet->is_alive) {BV_SET(fields, 11);}

  differ = (old->reputation != real_packet->reputation);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 12);}


    {
      differ = (MAX_NUM_PLAYERS + MAX_NUM_BARBARIANS != MAX_NUM_PLAYERS + MAX_NUM_BARBARIANS);
      if(!differ) {
        int i;
        for (i = 0; i < MAX_NUM_PLAYERS + MAX_NUM_BARBARIANS; i++) {
          if (!are_diplstates_equal(&old->diplstates[i], &real_packet->diplstates[i])) {
            differ = TRUE;
            break;
          }
        }
      }
    }
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 13);}

  differ = (old->gold != real_packet->gold);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 14);}

  differ = (old->tax != real_packet->tax);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 15);}

  differ = (old->science != real_packet->science);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 16);}

  differ = (old->luxury != real_packet->luxury);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 17);}

  differ = (old->bulbs_last_turn != real_packet->bulbs_last_turn);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 18);}

  differ = (old->bulbs_researched != real_packet->bulbs_researched);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 19);}

  differ = (old->techs_researched != real_packet->techs_researched);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 20);}

  differ = (old->researching != real_packet->researching);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 21);}

  differ = (old->future_tech != real_packet->future_tech);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 22);}

  differ = (old->tech_goal != real_packet->tech_goal);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 23);}

  differ = (old->is_connected != real_packet->is_connected);
  if(differ) {different++;}
  if(packet->is_connected) {BV_SET(fields, 24);}

  differ = (old->revolution_finishes != real_packet->revolution_finishes);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 25);}

  differ = (old->ai != real_packet->ai);
  if(differ) {different++;}
  if(packet->ai) {BV_SET(fields, 26);}

  differ = (old->barbarian_type != real_packet->barbarian_type);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 27);}

  differ = (old->gives_shared_vision != real_packet->gives_shared_vision);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 28);}

  differ = (strcmp(old->inventions, real_packet->inventions) != 0);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 29);}


    {
      differ = (MAX_NUM_PLAYERS + MAX_NUM_BARBARIANS != MAX_NUM_PLAYERS + MAX_NUM_BARBARIANS);
      if(!differ) {
        int i;
        for (i = 0; i < MAX_NUM_PLAYERS + MAX_NUM_BARBARIANS; i++) {
          if (old->love[i] != real_packet->love[i]) {
            differ = TRUE;
            break;
          }
        }
      }
    }
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 30);}

  if (different == 0 && !force_send_of_unchanged) {
    return 0;
  }

  DIO_BV_PUT(&dout, fields);
  dio_put_uint8(&dout, real_packet->playerno);

  if (BV_ISSET(fields, 0)) {
    dio_put_string(&dout, real_packet->name);
  }
  if (BV_ISSET(fields, 1)) {
    dio_put_string(&dout, real_packet->username);
  }
  /* field 2 is folded into the header */
  if (BV_ISSET(fields, 3)) {
    dio_put_uint8(&dout, real_packet->government);
  }
  if (BV_ISSET(fields, 4)) {
    dio_put_uint8(&dout, real_packet->target_government);
  }
  if (BV_ISSET(fields, 5)) {
    dio_put_uint32(&dout, real_packet->embassy);
  }
  if (BV_ISSET(fields, 6)) {
    dio_put_uint8(&dout, real_packet->city_style);
  }
  if (BV_ISSET(fields, 7)) {
    dio_put_uint16(&dout, real_packet->nation);
  }
  if (BV_ISSET(fields, 8)) {
    dio_put_uint8(&dout, real_packet->team);
  }
  /* field 9 is folded into the header */
  if (BV_ISSET(fields, 10)) {
    dio_put_sint16(&dout, real_packet->nturns_idle);
  }
  /* field 11 is folded into the header */
  if (BV_ISSET(fields, 12)) {
    dio_put_uint32(&dout, real_packet->reputation);
  }
  if (BV_ISSET(fields, 13)) {
  
    {
      int i;

      for (i = 0; i < MAX_NUM_PLAYERS + MAX_NUM_BARBARIANS; i++) {
        dio_put_diplstate(&dout, &real_packet->diplstates[i]);
      }
    } 
  }
  if (BV_ISSET(fields, 14)) {
    dio_put_uint32(&dout, real_packet->gold);
  }
  if (BV_ISSET(fields, 15)) {
    dio_put_uint8(&dout, real_packet->tax);
  }
  if (BV_ISSET(fields, 16)) {
    dio_put_uint8(&dout, real_packet->science);
  }
  if (BV_ISSET(fields, 17)) {
    dio_put_uint8(&dout, real_packet->luxury);
  }
  if (BV_ISSET(fields, 18)) {
    dio_put_uint16(&dout, real_packet->bulbs_last_turn);
  }
  if (BV_ISSET(fields, 19)) {
    dio_put_uint32(&dout, real_packet->bulbs_researched);
  }
  if (BV_ISSET(fields, 20)) {
    dio_put_uint32(&dout, real_packet->techs_researched);
  }
  if (BV_ISSET(fields, 21)) {
    dio_put_uint8(&dout, real_packet->researching);
  }
  if (BV_ISSET(fields, 22)) {
    dio_put_uint16(&dout, real_packet->future_tech);
  }
  if (BV_ISSET(fields, 23)) {
    dio_put_uint8(&dout, real_packet->tech_goal);
  }
  /* field 24 is folded into the header */
  if (BV_ISSET(fields, 25)) {
    dio_put_sint16(&dout, real_packet->revolution_finishes);
  }
  /* field 26 is folded into the header */
  if (BV_ISSET(fields, 27)) {
    dio_put_uint8(&dout, real_packet->barbarian_type);
  }
  if (BV_ISSET(fields, 28)) {
    dio_put_uint32(&dout, real_packet->gives_shared_vision);
  }
  if (BV_ISSET(fields, 29)) {
    dio_put_bit_string(&dout, real_packet->inventions);
  }
  if (BV_ISSET(fields, 30)) {
  
    {
      int i;

      for (i = 0; i < MAX_NUM_PLAYERS + MAX_NUM_BARBARIANS; i++) {
        dio_put_sint16(&dout, real_packet->love[i]);
      }
    } 
  }


  if (old_from_hash) {
    hash_delete_entry(*hash, old);
  }

  clone = old;

  *clone = *real_packet;
  hash_insert(*hash, clone, clone);
  SEND_PACKET_END;
}

static unsigned int hash_packet_player_info_101(const void *vkey, unsigned int num_buckets)
{
  const struct packet_player_info *key = (const struct packet_player_info *) vkey;

  return ((key->playerno) % num_buckets);
}

static int cmp_packet_player_info_101(const void *vkey1, const void *vkey2)
{
  const struct packet_player_info *key1 = (const struct packet_player_info *) vkey1;
  const struct packet_player_info *key2 = (const struct packet_player_info *) vkey2;
  int diff;

  diff = key1->playerno - key2->playerno;
  if (diff != 0) {
    return diff;
  }

  return 0;
}

BV_DEFINE(packet_player_info_101_fields, 30);

static struct packet_player_info *receive_packet_player_info_101(struct connection *pc, enum packet_type type)
{
  packet_player_info_101_fields fields;
  struct packet_player_info *old;
  struct hash_table **hash = &pc->phs.received[type];
  struct packet_player_info *clone;
  RECEIVE_PACKET_START(packet_player_info, real_packet);

  DIO_BV_GET(&din, fields);
  {
    int readin;
  
    dio_get_uint8(&din, &readin);
    real_packet->playerno = readin;
  }


  if (!*hash) {
    *hash = hash_new(hash_packet_player_info_101, cmp_packet_player_info_101);
  }
  old = hash_delete_entry(*hash, real_packet);

  if (old) {
    *real_packet = *old;
  } else {
    int playerno = real_packet->playerno;

    memset(real_packet, 0, sizeof(*real_packet));

    real_packet->playerno = playerno;
  }

  if (BV_ISSET(fields, 0)) {
    dio_get_string(&din, real_packet->name, sizeof(real_packet->name));
  }
  real_packet->is_male = BV_ISSET(fields, 1);
  if (BV_ISSET(fields, 2)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->government = readin;
    }
  }
  if (BV_ISSET(fields, 3)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->target_government = readin;
    }
  }
  if (BV_ISSET(fields, 4)) {
    {
      int readin;
    
      dio_get_uint32(&din, &readin);
      real_packet->embassy = readin;
    }
  }
  if (BV_ISSET(fields, 5)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->city_style = readin;
    }
  }
  if (BV_ISSET(fields, 6)) {
    {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->nation = readin;
    }
  }
  if (BV_ISSET(fields, 7)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->team = readin;
    }
  }
  real_packet->turn_done = BV_ISSET(fields, 8);
  if (BV_ISSET(fields, 9)) {
    {
      int readin;
    
      dio_get_sint16(&din, &readin);
      real_packet->nturns_idle = readin;
    }
  }
  real_packet->is_alive = BV_ISSET(fields, 10);
  if (BV_ISSET(fields, 11)) {
    {
      int readin;
    
      dio_get_uint32(&din, &readin);
      real_packet->reputation = readin;
    }
  }
  if (BV_ISSET(fields, 12)) {
    
    {
      int i;
    
      for (i = 0; i < MAX_NUM_PLAYERS + MAX_NUM_BARBARIANS; i++) {
        dio_get_diplstate(&din, &real_packet->diplstates[i]);
      }
    }
  }
  if (BV_ISSET(fields, 13)) {
    {
      int readin;
    
      dio_get_uint32(&din, &readin);
      real_packet->gold = readin;
    }
  }
  if (BV_ISSET(fields, 14)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->tax = readin;
    }
  }
  if (BV_ISSET(fields, 15)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->science = readin;
    }
  }
  if (BV_ISSET(fields, 16)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->luxury = readin;
    }
  }
  if (BV_ISSET(fields, 17)) {
    {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->bulbs_last_turn = readin;
    }
  }
  if (BV_ISSET(fields, 18)) {
    {
      int readin;
    
      dio_get_uint32(&din, &readin);
      real_packet->bulbs_researched = readin;
    }
  }
  if (BV_ISSET(fields, 19)) {
    {
      int readin;
    
      dio_get_uint32(&din, &readin);
      real_packet->techs_researched = readin;
    }
  }
  if (BV_ISSET(fields, 20)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->researching = readin;
    }
  }
  if (BV_ISSET(fields, 21)) {
    {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->future_tech = readin;
    }
  }
  if (BV_ISSET(fields, 22)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->tech_goal = readin;
    }
  }
  real_packet->is_connected = BV_ISSET(fields, 23);
  if (BV_ISSET(fields, 24)) {
    {
      int readin;
    
      dio_get_sint16(&din, &readin);
      real_packet->revolution_finishes = readin;
    }
  }
  real_packet->ai = BV_ISSET(fields, 25);
  if (BV_ISSET(fields, 26)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->barbarian_type = readin;
    }
  }
  if (BV_ISSET(fields, 27)) {
    {
      int readin;
    
      dio_get_uint32(&din, &readin);
      real_packet->gives_shared_vision = readin;
    }
  }
  if (BV_ISSET(fields, 28)) {
    dio_get_bit_string(&din, real_packet->inventions, sizeof(real_packet->inventions));
  }
  if (BV_ISSET(fields, 29)) {
    
    {
      int i;
    
      for (i = 0; i < MAX_NUM_PLAYERS + MAX_NUM_BARBARIANS; i++) {
        {
      int readin;
    
      dio_get_sint16(&din, &readin);
      real_packet->love[i] = readin;
    }
      }
    }
  }

  clone = fc_malloc(sizeof(*clone));
  *clone = *real_packet;
  if (old) {
    free(old);
  }
  hash_insert(*hash, clone, clone);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_player_info_101(struct connection *pc, const struct packet_player_info *packet)
{
  const struct packet_player_info *real_packet = packet;
  packet_player_info_101_fields fields;
  struct packet_player_info *old, *clone;
  bool differ, old_from_hash, force_send_of_unchanged = FALSE;
  struct hash_table **hash = &pc->phs.sent[PACKET_PLAYER_INFO];
  int different = 0;
  SEND_PACKET_START(PACKET_PLAYER_INFO);

  if (!*hash) {
    *hash = hash_new(hash_packet_player_info_101, cmp_packet_player_info_101);
  }
  BV_CLR_ALL(fields);

  old = hash_lookup_data(*hash, real_packet);
  old_from_hash = (old != NULL);
  if (!old) {
    old = fc_malloc(sizeof(*old));
    memset(old, 0, sizeof(*old));
    force_send_of_unchanged = TRUE;
  }

  differ = (strcmp(old->name, real_packet->name) != 0);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 0);}

  differ = (old->is_male != real_packet->is_male);
  if(differ) {different++;}
  if(packet->is_male) {BV_SET(fields, 1);}

  differ = (old->government != real_packet->government);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 2);}

  differ = (old->target_government != real_packet->target_government);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 3);}

  differ = (old->embassy != real_packet->embassy);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 4);}

  differ = (old->city_style != real_packet->city_style);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 5);}

  differ = (old->nation != real_packet->nation);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 6);}

  differ = (old->team != real_packet->team);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 7);}

  differ = (old->turn_done != real_packet->turn_done);
  if(differ) {different++;}
  if(packet->turn_done) {BV_SET(fields, 8);}

  differ = (old->nturns_idle != real_packet->nturns_idle);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 9);}

  differ = (old->is_alive != real_packet->is_alive);
  if(differ) {different++;}
  if(packet->is_alive) {BV_SET(fields, 10);}

  differ = (old->reputation != real_packet->reputation);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 11);}


    {
      differ = (MAX_NUM_PLAYERS + MAX_NUM_BARBARIANS != MAX_NUM_PLAYERS + MAX_NUM_BARBARIANS);
      if(!differ) {
        int i;
        for (i = 0; i < MAX_NUM_PLAYERS + MAX_NUM_BARBARIANS; i++) {
          if (!are_diplstates_equal(&old->diplstates[i], &real_packet->diplstates[i])) {
            differ = TRUE;
            break;
          }
        }
      }
    }
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 12);}

  differ = (old->gold != real_packet->gold);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 13);}

  differ = (old->tax != real_packet->tax);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 14);}

  differ = (old->science != real_packet->science);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 15);}

  differ = (old->luxury != real_packet->luxury);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 16);}

  differ = (old->bulbs_last_turn != real_packet->bulbs_last_turn);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 17);}

  differ = (old->bulbs_researched != real_packet->bulbs_researched);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 18);}

  differ = (old->techs_researched != real_packet->techs_researched);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 19);}

  differ = (old->researching != real_packet->researching);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 20);}

  differ = (old->future_tech != real_packet->future_tech);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 21);}

  differ = (old->tech_goal != real_packet->tech_goal);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 22);}

  differ = (old->is_connected != real_packet->is_connected);
  if(differ) {different++;}
  if(packet->is_connected) {BV_SET(fields, 23);}

  differ = (old->revolution_finishes != real_packet->revolution_finishes);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 24);}

  differ = (old->ai != real_packet->ai);
  if(differ) {different++;}
  if(packet->ai) {BV_SET(fields, 25);}

  differ = (old->barbarian_type != real_packet->barbarian_type);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 26);}

  differ = (old->gives_shared_vision != real_packet->gives_shared_vision);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 27);}

  differ = (strcmp(old->inventions, real_packet->inventions) != 0);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 28);}


    {
      differ = (MAX_NUM_PLAYERS + MAX_NUM_BARBARIANS != MAX_NUM_PLAYERS + MAX_NUM_BARBARIANS);
      if(!differ) {
        int i;
        for (i = 0; i < MAX_NUM_PLAYERS + MAX_NUM_BARBARIANS; i++) {
          if (old->love[i] != real_packet->love[i]) {
            differ = TRUE;
            break;
          }
        }
      }
    }
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 29);}

  if (different == 0 && !force_send_of_unchanged) {
    return 0;
  }

  DIO_BV_PUT(&dout, fields);
  dio_put_uint8(&dout, real_packet->playerno);

  if (BV_ISSET(fields, 0)) {
    dio_put_string(&dout, real_packet->name);
  }
  /* field 1 is folded into the header */
  if (BV_ISSET(fields, 2)) {
    dio_put_uint8(&dout, real_packet->government);
  }
  if (BV_ISSET(fields, 3)) {
    dio_put_uint8(&dout, real_packet->target_government);
  }
  if (BV_ISSET(fields, 4)) {
    dio_put_uint32(&dout, real_packet->embassy);
  }
  if (BV_ISSET(fields, 5)) {
    dio_put_uint8(&dout, real_packet->city_style);
  }
  if (BV_ISSET(fields, 6)) {
    dio_put_uint16(&dout, real_packet->nation);
  }
  if (BV_ISSET(fields, 7)) {
    dio_put_uint8(&dout, real_packet->team);
  }
  /* field 8 is folded into the header */
  if (BV_ISSET(fields, 9)) {
    dio_put_sint16(&dout, real_packet->nturns_idle);
  }
  /* field 10 is folded into the header */
  if (BV_ISSET(fields, 11)) {
    dio_put_uint32(&dout, real_packet->reputation);
  }
  if (BV_ISSET(fields, 12)) {
  
    {
      int i;

      for (i = 0; i < MAX_NUM_PLAYERS + MAX_NUM_BARBARIANS; i++) {
        dio_put_diplstate(&dout, &real_packet->diplstates[i]);
      }
    } 
  }
  if (BV_ISSET(fields, 13)) {
    dio_put_uint32(&dout, real_packet->gold);
  }
  if (BV_ISSET(fields, 14)) {
    dio_put_uint8(&dout, real_packet->tax);
  }
  if (BV_ISSET(fields, 15)) {
    dio_put_uint8(&dout, real_packet->science);
  }
  if (BV_ISSET(fields, 16)) {
    dio_put_uint8(&dout, real_packet->luxury);
  }
  if (BV_ISSET(fields, 17)) {
    dio_put_uint16(&dout, real_packet->bulbs_last_turn);
  }
  if (BV_ISSET(fields, 18)) {
    dio_put_uint32(&dout, real_packet->bulbs_researched);
  }
  if (BV_ISSET(fields, 19)) {
    dio_put_uint32(&dout, real_packet->techs_researched);
  }
  if (BV_ISSET(fields, 20)) {
    dio_put_uint8(&dout, real_packet->researching);
  }
  if (BV_ISSET(fields, 21)) {
    dio_put_uint16(&dout, real_packet->future_tech);
  }
  if (BV_ISSET(fields, 22)) {
    dio_put_uint8(&dout, real_packet->tech_goal);
  }
  /* field 23 is folded into the header */
  if (BV_ISSET(fields, 24)) {
    dio_put_sint16(&dout, real_packet->revolution_finishes);
  }
  /* field 25 is folded into the header */
  if (BV_ISSET(fields, 26)) {
    dio_put_uint8(&dout, real_packet->barbarian_type);
  }
  if (BV_ISSET(fields, 27)) {
    dio_put_uint32(&dout, real_packet->gives_shared_vision);
  }
  if (BV_ISSET(fields, 28)) {
    dio_put_bit_string(&dout, real_packet->inventions);
  }
  if (BV_ISSET(fields, 29)) {
  
    {
      int i;

      for (i = 0; i < MAX_NUM_PLAYERS + MAX_NUM_BARBARIANS; i++) {
        dio_put_sint16(&dout, real_packet->love[i]);
      }
    } 
  }


  if (old_from_hash) {
    hash_delete_entry(*hash, old);
  }

  clone = old;

  *clone = *real_packet;
  hash_insert(*hash, clone, clone);
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_player_info(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_PLAYER_INFO] != -1) {
    return;
  }

  if(FALSE) {
  } else if((has_capability("username_info", pc->capability) && has_capability("username_info", our_capability))) {
    variant = 100;
  } else if(!(has_capability("username_info", pc->capability) && has_capability("username_info", our_capability))) {
    variant = 101;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_PLAYER_INFO] = variant;
}

struct packet_player_info *receive_packet_player_info(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Receiving packet_player_info at the server.");
  }
  ensure_valid_variant_packet_player_info(pc);

  switch(pc->phs.variant[PACKET_PLAYER_INFO]) {
    case 100: return receive_packet_player_info_100(pc, type);
    case 101: return receive_packet_player_info_101(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_player_info(struct connection *pc, const struct packet_player_info *packet)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Sending packet_player_info from the client.");
  }
  ensure_valid_variant_packet_player_info(pc);

  switch(pc->phs.variant[PACKET_PLAYER_INFO]) {
    case 100: return send_packet_player_info_100(pc, packet);
    case 101: return send_packet_player_info_101(pc, packet);
    default: die("unknown variant"); return -1;
  }
}

static struct packet_player_turn_done *receive_packet_player_turn_done_100(struct connection *pc, enum packet_type type)
{
  RECEIVE_PACKET_START(packet_player_turn_done, real_packet);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_player_turn_done_100(struct connection *pc)
{
  SEND_PACKET_START(PACKET_PLAYER_TURN_DONE);
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_player_turn_done(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_PLAYER_TURN_DONE] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_PLAYER_TURN_DONE] = variant;
}

struct packet_player_turn_done *receive_packet_player_turn_done(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Receiving packet_player_turn_done at the client.");
  }
  ensure_valid_variant_packet_player_turn_done(pc);

  switch(pc->phs.variant[PACKET_PLAYER_TURN_DONE]) {
    case 100: return receive_packet_player_turn_done_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_player_turn_done(struct connection *pc)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Sending packet_player_turn_done from the server.");
  }
  ensure_valid_variant_packet_player_turn_done(pc);

  switch(pc->phs.variant[PACKET_PLAYER_TURN_DONE]) {
    case 100: return send_packet_player_turn_done_100(pc);
    default: die("unknown variant"); return -1;
  }
}

#define hash_packet_player_rates_100 hash_const

#define cmp_packet_player_rates_100 cmp_const

BV_DEFINE(packet_player_rates_100_fields, 3);

static struct packet_player_rates *receive_packet_player_rates_100(struct connection *pc, enum packet_type type)
{
  packet_player_rates_100_fields fields;
  struct packet_player_rates *old;
  struct hash_table **hash = &pc->phs.received[type];
  struct packet_player_rates *clone;
  RECEIVE_PACKET_START(packet_player_rates, real_packet);

  DIO_BV_GET(&din, fields);


  if (!*hash) {
    *hash = hash_new(hash_packet_player_rates_100, cmp_packet_player_rates_100);
  }
  old = hash_delete_entry(*hash, real_packet);

  if (old) {
    *real_packet = *old;
  } else {
    memset(real_packet, 0, sizeof(*real_packet));
  }

  if (BV_ISSET(fields, 0)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->tax = readin;
    }
  }
  if (BV_ISSET(fields, 1)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->luxury = readin;
    }
  }
  if (BV_ISSET(fields, 2)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->science = readin;
    }
  }

  clone = fc_malloc(sizeof(*clone));
  *clone = *real_packet;
  if (old) {
    free(old);
  }
  hash_insert(*hash, clone, clone);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_player_rates_100(struct connection *pc, const struct packet_player_rates *packet)
{
  const struct packet_player_rates *real_packet = packet;
  packet_player_rates_100_fields fields;
  struct packet_player_rates *old, *clone;
  bool differ, old_from_hash, force_send_of_unchanged = TRUE;
  struct hash_table **hash = &pc->phs.sent[PACKET_PLAYER_RATES];
  int different = 0;
  SEND_PACKET_START(PACKET_PLAYER_RATES);

  if (!*hash) {
    *hash = hash_new(hash_packet_player_rates_100, cmp_packet_player_rates_100);
  }
  BV_CLR_ALL(fields);

  old = hash_lookup_data(*hash, real_packet);
  old_from_hash = (old != NULL);
  if (!old) {
    old = fc_malloc(sizeof(*old));
    memset(old, 0, sizeof(*old));
    force_send_of_unchanged = TRUE;
  }

  differ = (old->tax != real_packet->tax);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 0);}

  differ = (old->luxury != real_packet->luxury);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 1);}

  differ = (old->science != real_packet->science);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 2);}

  if (different == 0 && !force_send_of_unchanged) {
    return 0;
  }

  DIO_BV_PUT(&dout, fields);

  if (BV_ISSET(fields, 0)) {
    dio_put_uint8(&dout, real_packet->tax);
  }
  if (BV_ISSET(fields, 1)) {
    dio_put_uint8(&dout, real_packet->luxury);
  }
  if (BV_ISSET(fields, 2)) {
    dio_put_uint8(&dout, real_packet->science);
  }


  if (old_from_hash) {
    hash_delete_entry(*hash, old);
  }

  clone = old;

  *clone = *real_packet;
  hash_insert(*hash, clone, clone);
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_player_rates(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_PLAYER_RATES] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_PLAYER_RATES] = variant;
}

struct packet_player_rates *receive_packet_player_rates(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Receiving packet_player_rates at the client.");
  }
  ensure_valid_variant_packet_player_rates(pc);

  switch(pc->phs.variant[PACKET_PLAYER_RATES]) {
    case 100: return receive_packet_player_rates_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_player_rates(struct connection *pc, const struct packet_player_rates *packet)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Sending packet_player_rates from the server.");
  }
  ensure_valid_variant_packet_player_rates(pc);

  switch(pc->phs.variant[PACKET_PLAYER_RATES]) {
    case 100: return send_packet_player_rates_100(pc, packet);
    default: die("unknown variant"); return -1;
  }
}

int dsend_packet_player_rates(struct connection *pc, int tax, int luxury, int science)
{
  struct packet_player_rates packet, *real_packet = &packet;

  real_packet->tax = tax;
  real_packet->luxury = luxury;
  real_packet->science = science;
  
  return send_packet_player_rates(pc, real_packet);
}

#define hash_packet_player_change_government_100 hash_const

#define cmp_packet_player_change_government_100 cmp_const

BV_DEFINE(packet_player_change_government_100_fields, 1);

static struct packet_player_change_government *receive_packet_player_change_government_100(struct connection *pc, enum packet_type type)
{
  packet_player_change_government_100_fields fields;
  struct packet_player_change_government *old;
  struct hash_table **hash = &pc->phs.received[type];
  struct packet_player_change_government *clone;
  RECEIVE_PACKET_START(packet_player_change_government, real_packet);

  DIO_BV_GET(&din, fields);


  if (!*hash) {
    *hash = hash_new(hash_packet_player_change_government_100, cmp_packet_player_change_government_100);
  }
  old = hash_delete_entry(*hash, real_packet);

  if (old) {
    *real_packet = *old;
  } else {
    memset(real_packet, 0, sizeof(*real_packet));
  }

  if (BV_ISSET(fields, 0)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->government = readin;
    }
  }

  clone = fc_malloc(sizeof(*clone));
  *clone = *real_packet;
  if (old) {
    free(old);
  }
  hash_insert(*hash, clone, clone);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_player_change_government_100(struct connection *pc, const struct packet_player_change_government *packet)
{
  const struct packet_player_change_government *real_packet = packet;
  packet_player_change_government_100_fields fields;
  struct packet_player_change_government *old, *clone;
  bool differ, old_from_hash, force_send_of_unchanged = TRUE;
  struct hash_table **hash = &pc->phs.sent[PACKET_PLAYER_CHANGE_GOVERNMENT];
  int different = 0;
  SEND_PACKET_START(PACKET_PLAYER_CHANGE_GOVERNMENT);

  if (!*hash) {
    *hash = hash_new(hash_packet_player_change_government_100, cmp_packet_player_change_government_100);
  }
  BV_CLR_ALL(fields);

  old = hash_lookup_data(*hash, real_packet);
  old_from_hash = (old != NULL);
  if (!old) {
    old = fc_malloc(sizeof(*old));
    memset(old, 0, sizeof(*old));
    force_send_of_unchanged = TRUE;
  }

  differ = (old->government != real_packet->government);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 0);}

  if (different == 0 && !force_send_of_unchanged) {
    return 0;
  }

  DIO_BV_PUT(&dout, fields);

  if (BV_ISSET(fields, 0)) {
    dio_put_uint8(&dout, real_packet->government);
  }


  if (old_from_hash) {
    hash_delete_entry(*hash, old);
  }

  clone = old;

  *clone = *real_packet;
  hash_insert(*hash, clone, clone);
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_player_change_government(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_PLAYER_CHANGE_GOVERNMENT] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_PLAYER_CHANGE_GOVERNMENT] = variant;
}

struct packet_player_change_government *receive_packet_player_change_government(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Receiving packet_player_change_government at the client.");
  }
  ensure_valid_variant_packet_player_change_government(pc);

  switch(pc->phs.variant[PACKET_PLAYER_CHANGE_GOVERNMENT]) {
    case 100: return receive_packet_player_change_government_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_player_change_government(struct connection *pc, const struct packet_player_change_government *packet)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Sending packet_player_change_government from the server.");
  }
  ensure_valid_variant_packet_player_change_government(pc);

  switch(pc->phs.variant[PACKET_PLAYER_CHANGE_GOVERNMENT]) {
    case 100: return send_packet_player_change_government_100(pc, packet);
    default: die("unknown variant"); return -1;
  }
}

int dsend_packet_player_change_government(struct connection *pc, int government)
{
  struct packet_player_change_government packet, *real_packet = &packet;

  real_packet->government = government;
  
  return send_packet_player_change_government(pc, real_packet);
}

#define hash_packet_player_research_100 hash_const

#define cmp_packet_player_research_100 cmp_const

BV_DEFINE(packet_player_research_100_fields, 1);

static struct packet_player_research *receive_packet_player_research_100(struct connection *pc, enum packet_type type)
{
  packet_player_research_100_fields fields;
  struct packet_player_research *old;
  struct hash_table **hash = &pc->phs.received[type];
  struct packet_player_research *clone;
  RECEIVE_PACKET_START(packet_player_research, real_packet);

  DIO_BV_GET(&din, fields);


  if (!*hash) {
    *hash = hash_new(hash_packet_player_research_100, cmp_packet_player_research_100);
  }
  old = hash_delete_entry(*hash, real_packet);

  if (old) {
    *real_packet = *old;
  } else {
    memset(real_packet, 0, sizeof(*real_packet));
  }

  if (BV_ISSET(fields, 0)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->tech = readin;
    }
  }

  clone = fc_malloc(sizeof(*clone));
  *clone = *real_packet;
  if (old) {
    free(old);
  }
  hash_insert(*hash, clone, clone);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_player_research_100(struct connection *pc, const struct packet_player_research *packet)
{
  const struct packet_player_research *real_packet = packet;
  packet_player_research_100_fields fields;
  struct packet_player_research *old, *clone;
  bool differ, old_from_hash, force_send_of_unchanged = TRUE;
  struct hash_table **hash = &pc->phs.sent[PACKET_PLAYER_RESEARCH];
  int different = 0;
  SEND_PACKET_START(PACKET_PLAYER_RESEARCH);

  if (!*hash) {
    *hash = hash_new(hash_packet_player_research_100, cmp_packet_player_research_100);
  }
  BV_CLR_ALL(fields);

  old = hash_lookup_data(*hash, real_packet);
  old_from_hash = (old != NULL);
  if (!old) {
    old = fc_malloc(sizeof(*old));
    memset(old, 0, sizeof(*old));
    force_send_of_unchanged = TRUE;
  }

  differ = (old->tech != real_packet->tech);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 0);}

  if (different == 0 && !force_send_of_unchanged) {
    return 0;
  }

  DIO_BV_PUT(&dout, fields);

  if (BV_ISSET(fields, 0)) {
    dio_put_uint8(&dout, real_packet->tech);
  }


  if (old_from_hash) {
    hash_delete_entry(*hash, old);
  }

  clone = old;

  *clone = *real_packet;
  hash_insert(*hash, clone, clone);
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_player_research(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_PLAYER_RESEARCH] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_PLAYER_RESEARCH] = variant;
}

struct packet_player_research *receive_packet_player_research(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Receiving packet_player_research at the client.");
  }
  ensure_valid_variant_packet_player_research(pc);

  switch(pc->phs.variant[PACKET_PLAYER_RESEARCH]) {
    case 100: return receive_packet_player_research_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_player_research(struct connection *pc, const struct packet_player_research *packet)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Sending packet_player_research from the server.");
  }
  ensure_valid_variant_packet_player_research(pc);

  switch(pc->phs.variant[PACKET_PLAYER_RESEARCH]) {
    case 100: return send_packet_player_research_100(pc, packet);
    default: die("unknown variant"); return -1;
  }
}

int dsend_packet_player_research(struct connection *pc, int tech)
{
  struct packet_player_research packet, *real_packet = &packet;

  real_packet->tech = tech;
  
  return send_packet_player_research(pc, real_packet);
}

#define hash_packet_player_tech_goal_100 hash_const

#define cmp_packet_player_tech_goal_100 cmp_const

BV_DEFINE(packet_player_tech_goal_100_fields, 1);

static struct packet_player_tech_goal *receive_packet_player_tech_goal_100(struct connection *pc, enum packet_type type)
{
  packet_player_tech_goal_100_fields fields;
  struct packet_player_tech_goal *old;
  struct hash_table **hash = &pc->phs.received[type];
  struct packet_player_tech_goal *clone;
  RECEIVE_PACKET_START(packet_player_tech_goal, real_packet);

  DIO_BV_GET(&din, fields);


  if (!*hash) {
    *hash = hash_new(hash_packet_player_tech_goal_100, cmp_packet_player_tech_goal_100);
  }
  old = hash_delete_entry(*hash, real_packet);

  if (old) {
    *real_packet = *old;
  } else {
    memset(real_packet, 0, sizeof(*real_packet));
  }

  if (BV_ISSET(fields, 0)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->tech = readin;
    }
  }

  clone = fc_malloc(sizeof(*clone));
  *clone = *real_packet;
  if (old) {
    free(old);
  }
  hash_insert(*hash, clone, clone);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_player_tech_goal_100(struct connection *pc, const struct packet_player_tech_goal *packet)
{
  const struct packet_player_tech_goal *real_packet = packet;
  packet_player_tech_goal_100_fields fields;
  struct packet_player_tech_goal *old, *clone;
  bool differ, old_from_hash, force_send_of_unchanged = TRUE;
  struct hash_table **hash = &pc->phs.sent[PACKET_PLAYER_TECH_GOAL];
  int different = 0;
  SEND_PACKET_START(PACKET_PLAYER_TECH_GOAL);

  if (!*hash) {
    *hash = hash_new(hash_packet_player_tech_goal_100, cmp_packet_player_tech_goal_100);
  }
  BV_CLR_ALL(fields);

  old = hash_lookup_data(*hash, real_packet);
  old_from_hash = (old != NULL);
  if (!old) {
    old = fc_malloc(sizeof(*old));
    memset(old, 0, sizeof(*old));
    force_send_of_unchanged = TRUE;
  }

  differ = (old->tech != real_packet->tech);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 0);}

  if (different == 0 && !force_send_of_unchanged) {
    return 0;
  }

  DIO_BV_PUT(&dout, fields);

  if (BV_ISSET(fields, 0)) {
    dio_put_uint8(&dout, real_packet->tech);
  }


  if (old_from_hash) {
    hash_delete_entry(*hash, old);
  }

  clone = old;

  *clone = *real_packet;
  hash_insert(*hash, clone, clone);
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_player_tech_goal(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_PLAYER_TECH_GOAL] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_PLAYER_TECH_GOAL] = variant;
}

struct packet_player_tech_goal *receive_packet_player_tech_goal(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Receiving packet_player_tech_goal at the client.");
  }
  ensure_valid_variant_packet_player_tech_goal(pc);

  switch(pc->phs.variant[PACKET_PLAYER_TECH_GOAL]) {
    case 100: return receive_packet_player_tech_goal_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_player_tech_goal(struct connection *pc, const struct packet_player_tech_goal *packet)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Sending packet_player_tech_goal from the server.");
  }
  ensure_valid_variant_packet_player_tech_goal(pc);

  switch(pc->phs.variant[PACKET_PLAYER_TECH_GOAL]) {
    case 100: return send_packet_player_tech_goal_100(pc, packet);
    default: die("unknown variant"); return -1;
  }
}

int dsend_packet_player_tech_goal(struct connection *pc, int tech)
{
  struct packet_player_tech_goal packet, *real_packet = &packet;

  real_packet->tech = tech;
  
  return send_packet_player_tech_goal(pc, real_packet);
}

static struct packet_player_attribute_block *receive_packet_player_attribute_block_100(struct connection *pc, enum packet_type type)
{
  RECEIVE_PACKET_START(packet_player_attribute_block, real_packet);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_player_attribute_block_100(struct connection *pc)
{
  SEND_PACKET_START(PACKET_PLAYER_ATTRIBUTE_BLOCK);
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_player_attribute_block(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_PLAYER_ATTRIBUTE_BLOCK] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_PLAYER_ATTRIBUTE_BLOCK] = variant;
}

struct packet_player_attribute_block *receive_packet_player_attribute_block(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Receiving packet_player_attribute_block at the client.");
  }
  ensure_valid_variant_packet_player_attribute_block(pc);

  switch(pc->phs.variant[PACKET_PLAYER_ATTRIBUTE_BLOCK]) {
    case 100: return receive_packet_player_attribute_block_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_player_attribute_block(struct connection *pc)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Sending packet_player_attribute_block from the server.");
  }
  ensure_valid_variant_packet_player_attribute_block(pc);

  switch(pc->phs.variant[PACKET_PLAYER_ATTRIBUTE_BLOCK]) {
    case 100: return send_packet_player_attribute_block_100(pc);
    default: die("unknown variant"); return -1;
  }
}

#define hash_packet_player_attribute_chunk_100 hash_const

#define cmp_packet_player_attribute_chunk_100 cmp_const

BV_DEFINE(packet_player_attribute_chunk_100_fields, 4);

static struct packet_player_attribute_chunk *receive_packet_player_attribute_chunk_100(struct connection *pc, enum packet_type type)
{
  packet_player_attribute_chunk_100_fields fields;
  struct packet_player_attribute_chunk *old;
  struct hash_table **hash = &pc->phs.received[type];
  struct packet_player_attribute_chunk *clone;
  RECEIVE_PACKET_START(packet_player_attribute_chunk, real_packet);

  DIO_BV_GET(&din, fields);


  if (!*hash) {
    *hash = hash_new(hash_packet_player_attribute_chunk_100, cmp_packet_player_attribute_chunk_100);
  }
  old = hash_delete_entry(*hash, real_packet);

  if (old) {
    *real_packet = *old;
  } else {
    memset(real_packet, 0, sizeof(*real_packet));
  }

  if (BV_ISSET(fields, 0)) {
    {
      int readin;
    
      dio_get_uint32(&din, &readin);
      real_packet->offset = readin;
    }
  }
  if (BV_ISSET(fields, 1)) {
    {
      int readin;
    
      dio_get_uint32(&din, &readin);
      real_packet->total_length = readin;
    }
  }
  if (BV_ISSET(fields, 2)) {
    {
      int readin;
    
      dio_get_uint32(&din, &readin);
      real_packet->chunk_length = readin;
    }
  }
  if (BV_ISSET(fields, 3)) {
    dio_get_memory(&din, real_packet->data, real_packet->chunk_length);
  }

  clone = fc_malloc(sizeof(*clone));
  *clone = *real_packet;
  if (old) {
    free(old);
  }
  hash_insert(*hash, clone, clone);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_player_attribute_chunk_100(struct connection *pc, const struct packet_player_attribute_chunk *packet)
{
  const struct packet_player_attribute_chunk *real_packet = packet;
  packet_player_attribute_chunk_100_fields fields;
  struct packet_player_attribute_chunk *old, *clone;
  bool differ, old_from_hash, force_send_of_unchanged = TRUE;
  struct hash_table **hash = &pc->phs.sent[PACKET_PLAYER_ATTRIBUTE_CHUNK];
  int different = 0;
  SEND_PACKET_START(PACKET_PLAYER_ATTRIBUTE_CHUNK);

  {
    struct packet_player_attribute_chunk *tmp = fc_malloc(sizeof(*tmp));

    *tmp = *packet;
    pre_send_packet_player_attribute_chunk(pc, tmp);
    real_packet = tmp;
  }

  if (!*hash) {
    *hash = hash_new(hash_packet_player_attribute_chunk_100, cmp_packet_player_attribute_chunk_100);
  }
  BV_CLR_ALL(fields);

  old = hash_lookup_data(*hash, real_packet);
  old_from_hash = (old != NULL);
  if (!old) {
    old = fc_malloc(sizeof(*old));
    memset(old, 0, sizeof(*old));
    force_send_of_unchanged = TRUE;
  }

  differ = (old->offset != real_packet->offset);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 0);}

  differ = (old->total_length != real_packet->total_length);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 1);}

  differ = (old->chunk_length != real_packet->chunk_length);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 2);}

  differ = (memcmp(old->data, real_packet->data, ATTRIBUTE_CHUNK_SIZE) != 0);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 3);}

  if (different == 0 && !force_send_of_unchanged) {

  if (real_packet != packet) {
    free((void *) real_packet);
  }
    return 0;
  }

  DIO_BV_PUT(&dout, fields);

  if (BV_ISSET(fields, 0)) {
    dio_put_uint32(&dout, real_packet->offset);
  }
  if (BV_ISSET(fields, 1)) {
    dio_put_uint32(&dout, real_packet->total_length);
  }
  if (BV_ISSET(fields, 2)) {
    dio_put_uint32(&dout, real_packet->chunk_length);
  }
  if (BV_ISSET(fields, 3)) {
    dio_put_memory(&dout, &real_packet->data, real_packet->chunk_length);
  }


  if (old_from_hash) {
    hash_delete_entry(*hash, old);
  }

  clone = old;

  *clone = *real_packet;
  hash_insert(*hash, clone, clone);

  if (real_packet != packet) {
    free((void *) real_packet);
  }
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_player_attribute_chunk(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_PLAYER_ATTRIBUTE_CHUNK] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_PLAYER_ATTRIBUTE_CHUNK] = variant;
}

struct packet_player_attribute_chunk *receive_packet_player_attribute_chunk(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  ensure_valid_variant_packet_player_attribute_chunk(pc);

  switch(pc->phs.variant[PACKET_PLAYER_ATTRIBUTE_CHUNK]) {
    case 100: return receive_packet_player_attribute_chunk_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_player_attribute_chunk(struct connection *pc, const struct packet_player_attribute_chunk *packet)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  ensure_valid_variant_packet_player_attribute_chunk(pc);

  switch(pc->phs.variant[PACKET_PLAYER_ATTRIBUTE_CHUNK]) {
    case 100: return send_packet_player_attribute_chunk_100(pc, packet);
    default: die("unknown variant"); return -1;
  }
}

#define hash_packet_unit_remove_100 hash_const

#define cmp_packet_unit_remove_100 cmp_const

BV_DEFINE(packet_unit_remove_100_fields, 1);

static struct packet_unit_remove *receive_packet_unit_remove_100(struct connection *pc, enum packet_type type)
{
  packet_unit_remove_100_fields fields;
  struct packet_unit_remove *old;
  struct hash_table **hash = &pc->phs.received[type];
  struct packet_unit_remove *clone;
  RECEIVE_PACKET_START(packet_unit_remove, real_packet);

  DIO_BV_GET(&din, fields);


  if (!*hash) {
    *hash = hash_new(hash_packet_unit_remove_100, cmp_packet_unit_remove_100);
  }
  old = hash_delete_entry(*hash, real_packet);

  if (old) {
    *real_packet = *old;
  } else {
    memset(real_packet, 0, sizeof(*real_packet));
  }

  if (BV_ISSET(fields, 0)) {
    {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->unit_id = readin;
    }
  }

  clone = fc_malloc(sizeof(*clone));
  *clone = *real_packet;
  if (old) {
    free(old);
  }
  hash_insert(*hash, clone, clone);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_unit_remove_100(struct connection *pc, const struct packet_unit_remove *packet)
{
  const struct packet_unit_remove *real_packet = packet;
  packet_unit_remove_100_fields fields;
  struct packet_unit_remove *old, *clone;
  bool differ, old_from_hash, force_send_of_unchanged = TRUE;
  struct hash_table **hash = &pc->phs.sent[PACKET_UNIT_REMOVE];
  int different = 0;
  SEND_PACKET_START(PACKET_UNIT_REMOVE);

  if (!*hash) {
    *hash = hash_new(hash_packet_unit_remove_100, cmp_packet_unit_remove_100);
  }
  BV_CLR_ALL(fields);

  old = hash_lookup_data(*hash, real_packet);
  old_from_hash = (old != NULL);
  if (!old) {
    old = fc_malloc(sizeof(*old));
    memset(old, 0, sizeof(*old));
    force_send_of_unchanged = TRUE;
  }

  differ = (old->unit_id != real_packet->unit_id);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 0);}

  if (different == 0 && !force_send_of_unchanged) {
    return 0;
  }

  DIO_BV_PUT(&dout, fields);

  if (BV_ISSET(fields, 0)) {
    dio_put_uint16(&dout, real_packet->unit_id);
  }


  if (old_from_hash) {
    hash_delete_entry(*hash, old);
  }

  clone = old;

  *clone = *real_packet;
  hash_insert(*hash, clone, clone);
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_unit_remove(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_UNIT_REMOVE] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_UNIT_REMOVE] = variant;
}

struct packet_unit_remove *receive_packet_unit_remove(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Receiving packet_unit_remove at the server.");
  }
  ensure_valid_variant_packet_unit_remove(pc);

  switch(pc->phs.variant[PACKET_UNIT_REMOVE]) {
    case 100: return receive_packet_unit_remove_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_unit_remove(struct connection *pc, const struct packet_unit_remove *packet)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Sending packet_unit_remove from the client.");
  }
  ensure_valid_variant_packet_unit_remove(pc);

  switch(pc->phs.variant[PACKET_UNIT_REMOVE]) {
    case 100: return send_packet_unit_remove_100(pc, packet);
    default: die("unknown variant"); return -1;
  }
}

void lsend_packet_unit_remove(struct conn_list *dest, const struct packet_unit_remove *packet)
{
  conn_list_iterate(*dest, pconn) {
    send_packet_unit_remove(pconn, packet);
  } conn_list_iterate_end;
}

int dsend_packet_unit_remove(struct connection *pc, int unit_id)
{
  struct packet_unit_remove packet, *real_packet = &packet;

  real_packet->unit_id = unit_id;
  
  return send_packet_unit_remove(pc, real_packet);
}

void dlsend_packet_unit_remove(struct conn_list *dest, int unit_id)
{
  struct packet_unit_remove packet, *real_packet = &packet;

  real_packet->unit_id = unit_id;
  
  lsend_packet_unit_remove(dest, real_packet);
}

static unsigned int hash_packet_unit_info_100(const void *vkey, unsigned int num_buckets)
{
  const struct packet_unit_info *key = (const struct packet_unit_info *) vkey;

  return ((key->id) % num_buckets);
}

static int cmp_packet_unit_info_100(const void *vkey1, const void *vkey2)
{
  const struct packet_unit_info *key1 = (const struct packet_unit_info *) vkey1;
  const struct packet_unit_info *key2 = (const struct packet_unit_info *) vkey2;
  int diff;

  diff = key1->id - key2->id;
  if (diff != 0) {
    return diff;
  }

  return 0;
}

BV_DEFINE(packet_unit_info_100_fields, 33);

static struct packet_unit_info *receive_packet_unit_info_100(struct connection *pc, enum packet_type type)
{
  packet_unit_info_100_fields fields;
  struct packet_unit_info *old;
  struct hash_table **hash = &pc->phs.received[type];
  struct packet_unit_info *clone;
  RECEIVE_PACKET_START(packet_unit_info, real_packet);

  DIO_BV_GET(&din, fields);
  {
    int readin;
  
    dio_get_uint16(&din, &readin);
    real_packet->id = readin;
  }


  if (!*hash) {
    *hash = hash_new(hash_packet_unit_info_100, cmp_packet_unit_info_100);
  }
  old = hash_delete_entry(*hash, real_packet);

  if (old) {
    *real_packet = *old;
  } else {
    int id = real_packet->id;

    memset(real_packet, 0, sizeof(*real_packet));

    real_packet->id = id;
  }

  if (BV_ISSET(fields, 0)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->owner = readin;
    }
  }
  if (BV_ISSET(fields, 1)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->x = readin;
    }
  }
  if (BV_ISSET(fields, 2)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->y = readin;
    }
  }
  if (BV_ISSET(fields, 3)) {
    {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->homecity = readin;
    }
  }
  if (BV_ISSET(fields, 4)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->veteran = readin;
    }
  }
  real_packet->ai = BV_ISSET(fields, 5);
  real_packet->paradropped = BV_ISSET(fields, 6);
  real_packet->connecting = BV_ISSET(fields, 7);
  real_packet->transported = BV_ISSET(fields, 8);
  real_packet->done_moving = BV_ISSET(fields, 9);
  if (BV_ISSET(fields, 10)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->type = readin;
    }
  }
  if (BV_ISSET(fields, 11)) {
    {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->transported_by = readin;
    }
  }
  if (BV_ISSET(fields, 12)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->movesleft = readin;
    }
  }
  if (BV_ISSET(fields, 13)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->hp = readin;
    }
  }
  if (BV_ISSET(fields, 14)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->fuel = readin;
    }
  }
  if (BV_ISSET(fields, 15)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->activity_count = readin;
    }
  }
  if (BV_ISSET(fields, 16)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->unhappiness = readin;
    }
  }
  if (BV_ISSET(fields, 17)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->upkeep = readin;
    }
  }
  if (BV_ISSET(fields, 18)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->upkeep_food = readin;
    }
  }
  if (BV_ISSET(fields, 19)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->upkeep_gold = readin;
    }
  }
  if (BV_ISSET(fields, 20)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->occupy = readin;
    }
  }
  if (BV_ISSET(fields, 21)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->goto_dest_x = readin;
    }
  }
  if (BV_ISSET(fields, 22)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->goto_dest_y = readin;
    }
  }
  if (BV_ISSET(fields, 23)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->activity = readin;
    }
  }
  if (BV_ISSET(fields, 24)) {
    {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->activity_target = readin;
    }
  }
  real_packet->has_orders = BV_ISSET(fields, 25);
  if (BV_ISSET(fields, 26)) {
    {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->orders_length = readin;
    }
  }
  if (BV_ISSET(fields, 27)) {
    {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->orders_index = readin;
    }
  }
  real_packet->orders_repeat = BV_ISSET(fields, 28);
  real_packet->orders_vigilant = BV_ISSET(fields, 29);
  if (BV_ISSET(fields, 30)) {
    
    {
      int i;
    
      if(real_packet->orders_length > MAX_LEN_ROUTE) {
        freelog(LOG_ERROR, "packets_gen.c: WARNING: truncation array");
        real_packet->orders_length = MAX_LEN_ROUTE;
      }
      for (i = 0; i < real_packet->orders_length; i++) {
        {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->orders[i] = readin;
    }
      }
    }
  }
  if (BV_ISSET(fields, 31)) {
    
    {
      int i;
    
      if(real_packet->orders_length > MAX_LEN_ROUTE) {
        freelog(LOG_ERROR, "packets_gen.c: WARNING: truncation array");
        real_packet->orders_length = MAX_LEN_ROUTE;
      }
      for (i = 0; i < real_packet->orders_length; i++) {
        {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->orders_dirs[i] = readin;
    }
      }
    }
  }
  if (BV_ISSET(fields, 32)) {
    
    {
      int i;
    
      if(real_packet->orders_length > MAX_LEN_ROUTE) {
        freelog(LOG_ERROR, "packets_gen.c: WARNING: truncation array");
        real_packet->orders_length = MAX_LEN_ROUTE;
      }
      for (i = 0; i < real_packet->orders_length; i++) {
        {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->orders_activities[i] = readin;
    }
      }
    }
  }

  clone = fc_malloc(sizeof(*clone));
  *clone = *real_packet;
  if (old) {
    free(old);
  }
  hash_insert(*hash, clone, clone);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_unit_info_100(struct connection *pc, const struct packet_unit_info *packet)
{
  const struct packet_unit_info *real_packet = packet;
  packet_unit_info_100_fields fields;
  struct packet_unit_info *old, *clone;
  bool differ, old_from_hash, force_send_of_unchanged = FALSE;
  struct hash_table **hash = &pc->phs.sent[PACKET_UNIT_INFO];
  int different = 0;
  SEND_PACKET_START(PACKET_UNIT_INFO);

  if (!*hash) {
    *hash = hash_new(hash_packet_unit_info_100, cmp_packet_unit_info_100);
  }
  BV_CLR_ALL(fields);

  old = hash_lookup_data(*hash, real_packet);
  old_from_hash = (old != NULL);
  if (!old) {
    old = fc_malloc(sizeof(*old));
    memset(old, 0, sizeof(*old));
    force_send_of_unchanged = TRUE;
  }

  differ = (old->owner != real_packet->owner);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 0);}

  differ = (old->x != real_packet->x);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 1);}

  differ = (old->y != real_packet->y);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 2);}

  differ = (old->homecity != real_packet->homecity);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 3);}

  differ = (old->veteran != real_packet->veteran);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 4);}

  differ = (old->ai != real_packet->ai);
  if(differ) {different++;}
  if(packet->ai) {BV_SET(fields, 5);}

  differ = (old->paradropped != real_packet->paradropped);
  if(differ) {different++;}
  if(packet->paradropped) {BV_SET(fields, 6);}

  differ = (old->connecting != real_packet->connecting);
  if(differ) {different++;}
  if(packet->connecting) {BV_SET(fields, 7);}

  differ = (old->transported != real_packet->transported);
  if(differ) {different++;}
  if(packet->transported) {BV_SET(fields, 8);}

  differ = (old->done_moving != real_packet->done_moving);
  if(differ) {different++;}
  if(packet->done_moving) {BV_SET(fields, 9);}

  differ = (old->type != real_packet->type);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 10);}

  differ = (old->transported_by != real_packet->transported_by);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 11);}

  differ = (old->movesleft != real_packet->movesleft);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 12);}

  differ = (old->hp != real_packet->hp);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 13);}

  differ = (old->fuel != real_packet->fuel);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 14);}

  differ = (old->activity_count != real_packet->activity_count);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 15);}

  differ = (old->unhappiness != real_packet->unhappiness);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 16);}

  differ = (old->upkeep != real_packet->upkeep);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 17);}

  differ = (old->upkeep_food != real_packet->upkeep_food);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 18);}

  differ = (old->upkeep_gold != real_packet->upkeep_gold);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 19);}

  differ = (old->occupy != real_packet->occupy);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 20);}

  differ = (old->goto_dest_x != real_packet->goto_dest_x);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 21);}

  differ = (old->goto_dest_y != real_packet->goto_dest_y);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 22);}

  differ = (old->activity != real_packet->activity);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 23);}

  differ = (old->activity_target != real_packet->activity_target);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 24);}

  differ = (old->has_orders != real_packet->has_orders);
  if(differ) {different++;}
  if(packet->has_orders) {BV_SET(fields, 25);}

  differ = (old->orders_length != real_packet->orders_length);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 26);}

  differ = (old->orders_index != real_packet->orders_index);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 27);}

  differ = (old->orders_repeat != real_packet->orders_repeat);
  if(differ) {different++;}
  if(packet->orders_repeat) {BV_SET(fields, 28);}

  differ = (old->orders_vigilant != real_packet->orders_vigilant);
  if(differ) {different++;}
  if(packet->orders_vigilant) {BV_SET(fields, 29);}


    {
      differ = (old->orders_length != real_packet->orders_length);
      if(!differ) {
        int i;
        for (i = 0; i < real_packet->orders_length; i++) {
          if (old->orders[i] != real_packet->orders[i]) {
            differ = TRUE;
            break;
          }
        }
      }
    }
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 30);}


    {
      differ = (old->orders_length != real_packet->orders_length);
      if(!differ) {
        int i;
        for (i = 0; i < real_packet->orders_length; i++) {
          if (old->orders_dirs[i] != real_packet->orders_dirs[i]) {
            differ = TRUE;
            break;
          }
        }
      }
    }
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 31);}


    {
      differ = (old->orders_length != real_packet->orders_length);
      if(!differ) {
        int i;
        for (i = 0; i < real_packet->orders_length; i++) {
          if (old->orders_activities[i] != real_packet->orders_activities[i]) {
            differ = TRUE;
            break;
          }
        }
      }
    }
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 32);}

  if (different == 0 && !force_send_of_unchanged) {
    return 0;
  }

  DIO_BV_PUT(&dout, fields);
  dio_put_uint16(&dout, real_packet->id);

  if (BV_ISSET(fields, 0)) {
    dio_put_uint8(&dout, real_packet->owner);
  }
  if (BV_ISSET(fields, 1)) {
    dio_put_uint8(&dout, real_packet->x);
  }
  if (BV_ISSET(fields, 2)) {
    dio_put_uint8(&dout, real_packet->y);
  }
  if (BV_ISSET(fields, 3)) {
    dio_put_uint16(&dout, real_packet->homecity);
  }
  if (BV_ISSET(fields, 4)) {
    dio_put_uint8(&dout, real_packet->veteran);
  }
  /* field 5 is folded into the header */
  /* field 6 is folded into the header */
  /* field 7 is folded into the header */
  /* field 8 is folded into the header */
  /* field 9 is folded into the header */
  if (BV_ISSET(fields, 10)) {
    dio_put_uint8(&dout, real_packet->type);
  }
  if (BV_ISSET(fields, 11)) {
    dio_put_uint16(&dout, real_packet->transported_by);
  }
  if (BV_ISSET(fields, 12)) {
    dio_put_uint8(&dout, real_packet->movesleft);
  }
  if (BV_ISSET(fields, 13)) {
    dio_put_uint8(&dout, real_packet->hp);
  }
  if (BV_ISSET(fields, 14)) {
    dio_put_uint8(&dout, real_packet->fuel);
  }
  if (BV_ISSET(fields, 15)) {
    dio_put_uint8(&dout, real_packet->activity_count);
  }
  if (BV_ISSET(fields, 16)) {
    dio_put_uint8(&dout, real_packet->unhappiness);
  }
  if (BV_ISSET(fields, 17)) {
    dio_put_uint8(&dout, real_packet->upkeep);
  }
  if (BV_ISSET(fields, 18)) {
    dio_put_uint8(&dout, real_packet->upkeep_food);
  }
  if (BV_ISSET(fields, 19)) {
    dio_put_uint8(&dout, real_packet->upkeep_gold);
  }
  if (BV_ISSET(fields, 20)) {
    dio_put_uint8(&dout, real_packet->occupy);
  }
  if (BV_ISSET(fields, 21)) {
    dio_put_uint8(&dout, real_packet->goto_dest_x);
  }
  if (BV_ISSET(fields, 22)) {
    dio_put_uint8(&dout, real_packet->goto_dest_y);
  }
  if (BV_ISSET(fields, 23)) {
    dio_put_uint8(&dout, real_packet->activity);
  }
  if (BV_ISSET(fields, 24)) {
    dio_put_uint16(&dout, real_packet->activity_target);
  }
  /* field 25 is folded into the header */
  if (BV_ISSET(fields, 26)) {
    dio_put_uint16(&dout, real_packet->orders_length);
  }
  if (BV_ISSET(fields, 27)) {
    dio_put_uint16(&dout, real_packet->orders_index);
  }
  /* field 28 is folded into the header */
  /* field 29 is folded into the header */
  if (BV_ISSET(fields, 30)) {
  
    {
      int i;

      for (i = 0; i < real_packet->orders_length; i++) {
        dio_put_uint8(&dout, real_packet->orders[i]);
      }
    } 
  }
  if (BV_ISSET(fields, 31)) {
  
    {
      int i;

      for (i = 0; i < real_packet->orders_length; i++) {
        dio_put_uint8(&dout, real_packet->orders_dirs[i]);
      }
    } 
  }
  if (BV_ISSET(fields, 32)) {
  
    {
      int i;

      for (i = 0; i < real_packet->orders_length; i++) {
        dio_put_uint8(&dout, real_packet->orders_activities[i]);
      }
    } 
  }


  if (old_from_hash) {
    hash_delete_entry(*hash, old);
  }

  clone = old;

  *clone = *real_packet;
  hash_insert(*hash, clone, clone);
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_unit_info(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_UNIT_INFO] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_UNIT_INFO] = variant;
}

struct packet_unit_info *receive_packet_unit_info(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Receiving packet_unit_info at the server.");
  }
  ensure_valid_variant_packet_unit_info(pc);

  switch(pc->phs.variant[PACKET_UNIT_INFO]) {
    case 100: return receive_packet_unit_info_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_unit_info(struct connection *pc, const struct packet_unit_info *packet)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Sending packet_unit_info from the client.");
  }
  ensure_valid_variant_packet_unit_info(pc);

  switch(pc->phs.variant[PACKET_UNIT_INFO]) {
    case 100: return send_packet_unit_info_100(pc, packet);
    default: die("unknown variant"); return -1;
  }
}

void lsend_packet_unit_info(struct conn_list *dest, const struct packet_unit_info *packet)
{
  conn_list_iterate(*dest, pconn) {
    send_packet_unit_info(pconn, packet);
  } conn_list_iterate_end;
}

static unsigned int hash_packet_unit_short_info_100(const void *vkey, unsigned int num_buckets)
{
  const struct packet_unit_short_info *key = (const struct packet_unit_short_info *) vkey;

  return ((key->id) % num_buckets);
}

static int cmp_packet_unit_short_info_100(const void *vkey1, const void *vkey2)
{
  const struct packet_unit_short_info *key1 = (const struct packet_unit_short_info *) vkey1;
  const struct packet_unit_short_info *key2 = (const struct packet_unit_short_info *) vkey2;
  int diff;

  diff = key1->id - key2->id;
  if (diff != 0) {
    return diff;
  }

  return 0;
}

BV_DEFINE(packet_unit_short_info_100_fields, 14);

static struct packet_unit_short_info *receive_packet_unit_short_info_100(struct connection *pc, enum packet_type type)
{
  packet_unit_short_info_100_fields fields;
  struct packet_unit_short_info *old;
  struct hash_table **hash = &pc->phs.received[type];
  struct packet_unit_short_info *clone;
  RECEIVE_PACKET_START(packet_unit_short_info, real_packet);

  DIO_BV_GET(&din, fields);
  {
    int readin;
  
    dio_get_uint16(&din, &readin);
    real_packet->id = readin;
  }


  if (!*hash) {
    *hash = hash_new(hash_packet_unit_short_info_100, cmp_packet_unit_short_info_100);
  }
  old = hash_delete_entry(*hash, real_packet);

  if (old) {
    *real_packet = *old;
  } else {
    int id = real_packet->id;

    memset(real_packet, 0, sizeof(*real_packet));

    real_packet->id = id;
  }

  if (BV_ISSET(fields, 0)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->owner = readin;
    }
  }
  if (BV_ISSET(fields, 1)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->x = readin;
    }
  }
  if (BV_ISSET(fields, 2)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->y = readin;
    }
  }
  if (BV_ISSET(fields, 3)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->type = readin;
    }
  }
  if (BV_ISSET(fields, 4)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->veteran = readin;
    }
  }
  real_packet->occupied = BV_ISSET(fields, 5);
  real_packet->goes_out_of_sight = BV_ISSET(fields, 6);
  real_packet->transported = BV_ISSET(fields, 7);
  if (BV_ISSET(fields, 8)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->hp = readin;
    }
  }
  if (BV_ISSET(fields, 9)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->activity = readin;
    }
  }
  if (BV_ISSET(fields, 10)) {
    {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->transported_by = readin;
    }
  }
  if (BV_ISSET(fields, 11)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->packet_use = readin;
    }
  }
  if (BV_ISSET(fields, 12)) {
    {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->info_city_id = readin;
    }
  }
  if (BV_ISSET(fields, 13)) {
    {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->serial_num = readin;
    }
  }

  clone = fc_malloc(sizeof(*clone));
  *clone = *real_packet;
  if (old) {
    free(old);
  }
  hash_insert(*hash, clone, clone);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_unit_short_info_100(struct connection *pc, const struct packet_unit_short_info *packet)
{
  const struct packet_unit_short_info *real_packet = packet;
  packet_unit_short_info_100_fields fields;
  struct packet_unit_short_info *old, *clone;
  bool differ, old_from_hash, force_send_of_unchanged = FALSE;
  struct hash_table **hash = &pc->phs.sent[PACKET_UNIT_SHORT_INFO];
  int different = 0;
  SEND_PACKET_START(PACKET_UNIT_SHORT_INFO);

  if (!*hash) {
    *hash = hash_new(hash_packet_unit_short_info_100, cmp_packet_unit_short_info_100);
  }
  BV_CLR_ALL(fields);

  old = hash_lookup_data(*hash, real_packet);
  old_from_hash = (old != NULL);
  if (!old) {
    old = fc_malloc(sizeof(*old));
    memset(old, 0, sizeof(*old));
    force_send_of_unchanged = TRUE;
  }

  differ = (old->owner != real_packet->owner);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 0);}

  differ = (old->x != real_packet->x);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 1);}

  differ = (old->y != real_packet->y);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 2);}

  differ = (old->type != real_packet->type);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 3);}

  differ = (old->veteran != real_packet->veteran);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 4);}

  differ = (old->occupied != real_packet->occupied);
  if(differ) {different++;}
  if(packet->occupied) {BV_SET(fields, 5);}

  differ = (old->goes_out_of_sight != real_packet->goes_out_of_sight);
  if(differ) {different++;}
  if(packet->goes_out_of_sight) {BV_SET(fields, 6);}

  differ = (old->transported != real_packet->transported);
  if(differ) {different++;}
  if(packet->transported) {BV_SET(fields, 7);}

  differ = (old->hp != real_packet->hp);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 8);}

  differ = (old->activity != real_packet->activity);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 9);}

  differ = (old->transported_by != real_packet->transported_by);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 10);}

  differ = (old->packet_use != real_packet->packet_use);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 11);}

  differ = (old->info_city_id != real_packet->info_city_id);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 12);}

  differ = (old->serial_num != real_packet->serial_num);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 13);}

  if (different == 0 && !force_send_of_unchanged) {
    return 0;
  }

  DIO_BV_PUT(&dout, fields);
  dio_put_uint16(&dout, real_packet->id);

  if (BV_ISSET(fields, 0)) {
    dio_put_uint8(&dout, real_packet->owner);
  }
  if (BV_ISSET(fields, 1)) {
    dio_put_uint8(&dout, real_packet->x);
  }
  if (BV_ISSET(fields, 2)) {
    dio_put_uint8(&dout, real_packet->y);
  }
  if (BV_ISSET(fields, 3)) {
    dio_put_uint8(&dout, real_packet->type);
  }
  if (BV_ISSET(fields, 4)) {
    dio_put_uint8(&dout, real_packet->veteran);
  }
  /* field 5 is folded into the header */
  /* field 6 is folded into the header */
  /* field 7 is folded into the header */
  if (BV_ISSET(fields, 8)) {
    dio_put_uint8(&dout, real_packet->hp);
  }
  if (BV_ISSET(fields, 9)) {
    dio_put_uint8(&dout, real_packet->activity);
  }
  if (BV_ISSET(fields, 10)) {
    dio_put_uint16(&dout, real_packet->transported_by);
  }
  if (BV_ISSET(fields, 11)) {
    dio_put_uint8(&dout, real_packet->packet_use);
  }
  if (BV_ISSET(fields, 12)) {
    dio_put_uint16(&dout, real_packet->info_city_id);
  }
  if (BV_ISSET(fields, 13)) {
    dio_put_uint16(&dout, real_packet->serial_num);
  }


  if (old_from_hash) {
    hash_delete_entry(*hash, old);
  }

  clone = old;

  *clone = *real_packet;
  hash_insert(*hash, clone, clone);
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_unit_short_info(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_UNIT_SHORT_INFO] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_UNIT_SHORT_INFO] = variant;
}

struct packet_unit_short_info *receive_packet_unit_short_info(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Receiving packet_unit_short_info at the server.");
  }
  ensure_valid_variant_packet_unit_short_info(pc);

  switch(pc->phs.variant[PACKET_UNIT_SHORT_INFO]) {
    case 100: return receive_packet_unit_short_info_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_unit_short_info(struct connection *pc, const struct packet_unit_short_info *packet)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Sending packet_unit_short_info from the client.");
  }
  ensure_valid_variant_packet_unit_short_info(pc);

  switch(pc->phs.variant[PACKET_UNIT_SHORT_INFO]) {
    case 100: return send_packet_unit_short_info_100(pc, packet);
    default: die("unknown variant"); return -1;
  }
}

void lsend_packet_unit_short_info(struct conn_list *dest, const struct packet_unit_short_info *packet)
{
  conn_list_iterate(*dest, pconn) {
    send_packet_unit_short_info(pconn, packet);
  } conn_list_iterate_end;
}

#define hash_packet_unit_combat_info_100 hash_const

#define cmp_packet_unit_combat_info_100 cmp_const

BV_DEFINE(packet_unit_combat_info_100_fields, 5);

static struct packet_unit_combat_info *receive_packet_unit_combat_info_100(struct connection *pc, enum packet_type type)
{
  packet_unit_combat_info_100_fields fields;
  struct packet_unit_combat_info *old;
  struct hash_table **hash = &pc->phs.received[type];
  struct packet_unit_combat_info *clone;
  RECEIVE_PACKET_START(packet_unit_combat_info, real_packet);

  DIO_BV_GET(&din, fields);


  if (!*hash) {
    *hash = hash_new(hash_packet_unit_combat_info_100, cmp_packet_unit_combat_info_100);
  }
  old = hash_delete_entry(*hash, real_packet);

  if (old) {
    *real_packet = *old;
  } else {
    memset(real_packet, 0, sizeof(*real_packet));
  }

  if (BV_ISSET(fields, 0)) {
    {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->attacker_unit_id = readin;
    }
  }
  if (BV_ISSET(fields, 1)) {
    {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->defender_unit_id = readin;
    }
  }
  if (BV_ISSET(fields, 2)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->attacker_hp = readin;
    }
  }
  if (BV_ISSET(fields, 3)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->defender_hp = readin;
    }
  }
  real_packet->make_winner_veteran = BV_ISSET(fields, 4);

  clone = fc_malloc(sizeof(*clone));
  *clone = *real_packet;
  if (old) {
    free(old);
  }
  hash_insert(*hash, clone, clone);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_unit_combat_info_100(struct connection *pc, const struct packet_unit_combat_info *packet)
{
  const struct packet_unit_combat_info *real_packet = packet;
  packet_unit_combat_info_100_fields fields;
  struct packet_unit_combat_info *old, *clone;
  bool differ, old_from_hash, force_send_of_unchanged = TRUE;
  struct hash_table **hash = &pc->phs.sent[PACKET_UNIT_COMBAT_INFO];
  int different = 0;
  SEND_PACKET_START(PACKET_UNIT_COMBAT_INFO);

  if (!*hash) {
    *hash = hash_new(hash_packet_unit_combat_info_100, cmp_packet_unit_combat_info_100);
  }
  BV_CLR_ALL(fields);

  old = hash_lookup_data(*hash, real_packet);
  old_from_hash = (old != NULL);
  if (!old) {
    old = fc_malloc(sizeof(*old));
    memset(old, 0, sizeof(*old));
    force_send_of_unchanged = TRUE;
  }

  differ = (old->attacker_unit_id != real_packet->attacker_unit_id);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 0);}

  differ = (old->defender_unit_id != real_packet->defender_unit_id);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 1);}

  differ = (old->attacker_hp != real_packet->attacker_hp);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 2);}

  differ = (old->defender_hp != real_packet->defender_hp);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 3);}

  differ = (old->make_winner_veteran != real_packet->make_winner_veteran);
  if(differ) {different++;}
  if(packet->make_winner_veteran) {BV_SET(fields, 4);}

  if (different == 0 && !force_send_of_unchanged) {
    return 0;
  }

  DIO_BV_PUT(&dout, fields);

  if (BV_ISSET(fields, 0)) {
    dio_put_uint16(&dout, real_packet->attacker_unit_id);
  }
  if (BV_ISSET(fields, 1)) {
    dio_put_uint16(&dout, real_packet->defender_unit_id);
  }
  if (BV_ISSET(fields, 2)) {
    dio_put_uint8(&dout, real_packet->attacker_hp);
  }
  if (BV_ISSET(fields, 3)) {
    dio_put_uint8(&dout, real_packet->defender_hp);
  }
  /* field 4 is folded into the header */


  if (old_from_hash) {
    hash_delete_entry(*hash, old);
  }

  clone = old;

  *clone = *real_packet;
  hash_insert(*hash, clone, clone);
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_unit_combat_info(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_UNIT_COMBAT_INFO] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_UNIT_COMBAT_INFO] = variant;
}

struct packet_unit_combat_info *receive_packet_unit_combat_info(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Receiving packet_unit_combat_info at the server.");
  }
  ensure_valid_variant_packet_unit_combat_info(pc);

  switch(pc->phs.variant[PACKET_UNIT_COMBAT_INFO]) {
    case 100: return receive_packet_unit_combat_info_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_unit_combat_info(struct connection *pc, const struct packet_unit_combat_info *packet)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Sending packet_unit_combat_info from the client.");
  }
  ensure_valid_variant_packet_unit_combat_info(pc);

  switch(pc->phs.variant[PACKET_UNIT_COMBAT_INFO]) {
    case 100: return send_packet_unit_combat_info_100(pc, packet);
    default: die("unknown variant"); return -1;
  }
}

void lsend_packet_unit_combat_info(struct conn_list *dest, const struct packet_unit_combat_info *packet)
{
  conn_list_iterate(*dest, pconn) {
    send_packet_unit_combat_info(pconn, packet);
  } conn_list_iterate_end;
}

#define hash_packet_unit_move_100 hash_const

#define cmp_packet_unit_move_100 cmp_const

BV_DEFINE(packet_unit_move_100_fields, 3);

static struct packet_unit_move *receive_packet_unit_move_100(struct connection *pc, enum packet_type type)
{
  packet_unit_move_100_fields fields;
  struct packet_unit_move *old;
  struct hash_table **hash = &pc->phs.received[type];
  struct packet_unit_move *clone;
  RECEIVE_PACKET_START(packet_unit_move, real_packet);

  DIO_BV_GET(&din, fields);


  if (!*hash) {
    *hash = hash_new(hash_packet_unit_move_100, cmp_packet_unit_move_100);
  }
  old = hash_delete_entry(*hash, real_packet);

  if (old) {
    *real_packet = *old;
  } else {
    memset(real_packet, 0, sizeof(*real_packet));
  }

  if (BV_ISSET(fields, 0)) {
    {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->unit_id = readin;
    }
  }
  if (BV_ISSET(fields, 1)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->x = readin;
    }
  }
  if (BV_ISSET(fields, 2)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->y = readin;
    }
  }

  clone = fc_malloc(sizeof(*clone));
  *clone = *real_packet;
  if (old) {
    free(old);
  }
  hash_insert(*hash, clone, clone);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_unit_move_100(struct connection *pc, const struct packet_unit_move *packet)
{
  const struct packet_unit_move *real_packet = packet;
  packet_unit_move_100_fields fields;
  struct packet_unit_move *old, *clone;
  bool differ, old_from_hash, force_send_of_unchanged = TRUE;
  struct hash_table **hash = &pc->phs.sent[PACKET_UNIT_MOVE];
  int different = 0;
  SEND_PACKET_START(PACKET_UNIT_MOVE);

  if (!*hash) {
    *hash = hash_new(hash_packet_unit_move_100, cmp_packet_unit_move_100);
  }
  BV_CLR_ALL(fields);

  old = hash_lookup_data(*hash, real_packet);
  old_from_hash = (old != NULL);
  if (!old) {
    old = fc_malloc(sizeof(*old));
    memset(old, 0, sizeof(*old));
    force_send_of_unchanged = TRUE;
  }

  differ = (old->unit_id != real_packet->unit_id);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 0);}

  differ = (old->x != real_packet->x);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 1);}

  differ = (old->y != real_packet->y);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 2);}

  if (different == 0 && !force_send_of_unchanged) {
    return 0;
  }

  DIO_BV_PUT(&dout, fields);

  if (BV_ISSET(fields, 0)) {
    dio_put_uint16(&dout, real_packet->unit_id);
  }
  if (BV_ISSET(fields, 1)) {
    dio_put_uint8(&dout, real_packet->x);
  }
  if (BV_ISSET(fields, 2)) {
    dio_put_uint8(&dout, real_packet->y);
  }


  if (old_from_hash) {
    hash_delete_entry(*hash, old);
  }

  clone = old;

  *clone = *real_packet;
  hash_insert(*hash, clone, clone);
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_unit_move(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_UNIT_MOVE] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_UNIT_MOVE] = variant;
}

struct packet_unit_move *receive_packet_unit_move(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Receiving packet_unit_move at the client.");
  }
  ensure_valid_variant_packet_unit_move(pc);

  switch(pc->phs.variant[PACKET_UNIT_MOVE]) {
    case 100: return receive_packet_unit_move_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_unit_move(struct connection *pc, const struct packet_unit_move *packet)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Sending packet_unit_move from the server.");
  }
  ensure_valid_variant_packet_unit_move(pc);

  switch(pc->phs.variant[PACKET_UNIT_MOVE]) {
    case 100: return send_packet_unit_move_100(pc, packet);
    default: die("unknown variant"); return -1;
  }
}

int dsend_packet_unit_move(struct connection *pc, int unit_id, int x, int y)
{
  struct packet_unit_move packet, *real_packet = &packet;

  real_packet->unit_id = unit_id;
  real_packet->x = x;
  real_packet->y = y;
  
  return send_packet_unit_move(pc, real_packet);
}

#define hash_packet_unit_build_city_100 hash_const

#define cmp_packet_unit_build_city_100 cmp_const

BV_DEFINE(packet_unit_build_city_100_fields, 2);

static struct packet_unit_build_city *receive_packet_unit_build_city_100(struct connection *pc, enum packet_type type)
{
  packet_unit_build_city_100_fields fields;
  struct packet_unit_build_city *old;
  struct hash_table **hash = &pc->phs.received[type];
  struct packet_unit_build_city *clone;
  RECEIVE_PACKET_START(packet_unit_build_city, real_packet);

  DIO_BV_GET(&din, fields);


  if (!*hash) {
    *hash = hash_new(hash_packet_unit_build_city_100, cmp_packet_unit_build_city_100);
  }
  old = hash_delete_entry(*hash, real_packet);

  if (old) {
    *real_packet = *old;
  } else {
    memset(real_packet, 0, sizeof(*real_packet));
  }

  if (BV_ISSET(fields, 0)) {
    {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->unit_id = readin;
    }
  }
  if (BV_ISSET(fields, 1)) {
    dio_get_string(&din, real_packet->name, sizeof(real_packet->name));
  }

  clone = fc_malloc(sizeof(*clone));
  *clone = *real_packet;
  if (old) {
    free(old);
  }
  hash_insert(*hash, clone, clone);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_unit_build_city_100(struct connection *pc, const struct packet_unit_build_city *packet)
{
  const struct packet_unit_build_city *real_packet = packet;
  packet_unit_build_city_100_fields fields;
  struct packet_unit_build_city *old, *clone;
  bool differ, old_from_hash, force_send_of_unchanged = TRUE;
  struct hash_table **hash = &pc->phs.sent[PACKET_UNIT_BUILD_CITY];
  int different = 0;
  SEND_PACKET_START(PACKET_UNIT_BUILD_CITY);

  if (!*hash) {
    *hash = hash_new(hash_packet_unit_build_city_100, cmp_packet_unit_build_city_100);
  }
  BV_CLR_ALL(fields);

  old = hash_lookup_data(*hash, real_packet);
  old_from_hash = (old != NULL);
  if (!old) {
    old = fc_malloc(sizeof(*old));
    memset(old, 0, sizeof(*old));
    force_send_of_unchanged = TRUE;
  }

  differ = (old->unit_id != real_packet->unit_id);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 0);}

  differ = (strcmp(old->name, real_packet->name) != 0);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 1);}

  if (different == 0 && !force_send_of_unchanged) {
    return 0;
  }

  DIO_BV_PUT(&dout, fields);

  if (BV_ISSET(fields, 0)) {
    dio_put_uint16(&dout, real_packet->unit_id);
  }
  if (BV_ISSET(fields, 1)) {
    dio_put_string(&dout, real_packet->name);
  }


  if (old_from_hash) {
    hash_delete_entry(*hash, old);
  }

  clone = old;

  *clone = *real_packet;
  hash_insert(*hash, clone, clone);
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_unit_build_city(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_UNIT_BUILD_CITY] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_UNIT_BUILD_CITY] = variant;
}

struct packet_unit_build_city *receive_packet_unit_build_city(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Receiving packet_unit_build_city at the client.");
  }
  ensure_valid_variant_packet_unit_build_city(pc);

  switch(pc->phs.variant[PACKET_UNIT_BUILD_CITY]) {
    case 100: return receive_packet_unit_build_city_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_unit_build_city(struct connection *pc, const struct packet_unit_build_city *packet)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Sending packet_unit_build_city from the server.");
  }
  ensure_valid_variant_packet_unit_build_city(pc);

  switch(pc->phs.variant[PACKET_UNIT_BUILD_CITY]) {
    case 100: return send_packet_unit_build_city_100(pc, packet);
    default: die("unknown variant"); return -1;
  }
}

int dsend_packet_unit_build_city(struct connection *pc, int unit_id, const char *name)
{
  struct packet_unit_build_city packet, *real_packet = &packet;

  real_packet->unit_id = unit_id;
  sz_strlcpy(real_packet->name, name);
  
  return send_packet_unit_build_city(pc, real_packet);
}

#define hash_packet_unit_disband_100 hash_const

#define cmp_packet_unit_disband_100 cmp_const

BV_DEFINE(packet_unit_disband_100_fields, 1);

static struct packet_unit_disband *receive_packet_unit_disband_100(struct connection *pc, enum packet_type type)
{
  packet_unit_disband_100_fields fields;
  struct packet_unit_disband *old;
  struct hash_table **hash = &pc->phs.received[type];
  struct packet_unit_disband *clone;
  RECEIVE_PACKET_START(packet_unit_disband, real_packet);

  DIO_BV_GET(&din, fields);


  if (!*hash) {
    *hash = hash_new(hash_packet_unit_disband_100, cmp_packet_unit_disband_100);
  }
  old = hash_delete_entry(*hash, real_packet);

  if (old) {
    *real_packet = *old;
  } else {
    memset(real_packet, 0, sizeof(*real_packet));
  }

  if (BV_ISSET(fields, 0)) {
    {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->unit_id = readin;
    }
  }

  clone = fc_malloc(sizeof(*clone));
  *clone = *real_packet;
  if (old) {
    free(old);
  }
  hash_insert(*hash, clone, clone);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_unit_disband_100(struct connection *pc, const struct packet_unit_disband *packet)
{
  const struct packet_unit_disband *real_packet = packet;
  packet_unit_disband_100_fields fields;
  struct packet_unit_disband *old, *clone;
  bool differ, old_from_hash, force_send_of_unchanged = TRUE;
  struct hash_table **hash = &pc->phs.sent[PACKET_UNIT_DISBAND];
  int different = 0;
  SEND_PACKET_START(PACKET_UNIT_DISBAND);

  if (!*hash) {
    *hash = hash_new(hash_packet_unit_disband_100, cmp_packet_unit_disband_100);
  }
  BV_CLR_ALL(fields);

  old = hash_lookup_data(*hash, real_packet);
  old_from_hash = (old != NULL);
  if (!old) {
    old = fc_malloc(sizeof(*old));
    memset(old, 0, sizeof(*old));
    force_send_of_unchanged = TRUE;
  }

  differ = (old->unit_id != real_packet->unit_id);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 0);}

  if (different == 0 && !force_send_of_unchanged) {
    return 0;
  }

  DIO_BV_PUT(&dout, fields);

  if (BV_ISSET(fields, 0)) {
    dio_put_uint16(&dout, real_packet->unit_id);
  }


  if (old_from_hash) {
    hash_delete_entry(*hash, old);
  }

  clone = old;

  *clone = *real_packet;
  hash_insert(*hash, clone, clone);
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_unit_disband(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_UNIT_DISBAND] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_UNIT_DISBAND] = variant;
}

struct packet_unit_disband *receive_packet_unit_disband(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Receiving packet_unit_disband at the client.");
  }
  ensure_valid_variant_packet_unit_disband(pc);

  switch(pc->phs.variant[PACKET_UNIT_DISBAND]) {
    case 100: return receive_packet_unit_disband_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_unit_disband(struct connection *pc, const struct packet_unit_disband *packet)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Sending packet_unit_disband from the server.");
  }
  ensure_valid_variant_packet_unit_disband(pc);

  switch(pc->phs.variant[PACKET_UNIT_DISBAND]) {
    case 100: return send_packet_unit_disband_100(pc, packet);
    default: die("unknown variant"); return -1;
  }
}

int dsend_packet_unit_disband(struct connection *pc, int unit_id)
{
  struct packet_unit_disband packet, *real_packet = &packet;

  real_packet->unit_id = unit_id;
  
  return send_packet_unit_disband(pc, real_packet);
}

#define hash_packet_unit_change_homecity_100 hash_const

#define cmp_packet_unit_change_homecity_100 cmp_const

BV_DEFINE(packet_unit_change_homecity_100_fields, 2);

static struct packet_unit_change_homecity *receive_packet_unit_change_homecity_100(struct connection *pc, enum packet_type type)
{
  packet_unit_change_homecity_100_fields fields;
  struct packet_unit_change_homecity *old;
  struct hash_table **hash = &pc->phs.received[type];
  struct packet_unit_change_homecity *clone;
  RECEIVE_PACKET_START(packet_unit_change_homecity, real_packet);

  DIO_BV_GET(&din, fields);


  if (!*hash) {
    *hash = hash_new(hash_packet_unit_change_homecity_100, cmp_packet_unit_change_homecity_100);
  }
  old = hash_delete_entry(*hash, real_packet);

  if (old) {
    *real_packet = *old;
  } else {
    memset(real_packet, 0, sizeof(*real_packet));
  }

  if (BV_ISSET(fields, 0)) {
    {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->unit_id = readin;
    }
  }
  if (BV_ISSET(fields, 1)) {
    {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->city_id = readin;
    }
  }

  clone = fc_malloc(sizeof(*clone));
  *clone = *real_packet;
  if (old) {
    free(old);
  }
  hash_insert(*hash, clone, clone);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_unit_change_homecity_100(struct connection *pc, const struct packet_unit_change_homecity *packet)
{
  const struct packet_unit_change_homecity *real_packet = packet;
  packet_unit_change_homecity_100_fields fields;
  struct packet_unit_change_homecity *old, *clone;
  bool differ, old_from_hash, force_send_of_unchanged = TRUE;
  struct hash_table **hash = &pc->phs.sent[PACKET_UNIT_CHANGE_HOMECITY];
  int different = 0;
  SEND_PACKET_START(PACKET_UNIT_CHANGE_HOMECITY);

  if (!*hash) {
    *hash = hash_new(hash_packet_unit_change_homecity_100, cmp_packet_unit_change_homecity_100);
  }
  BV_CLR_ALL(fields);

  old = hash_lookup_data(*hash, real_packet);
  old_from_hash = (old != NULL);
  if (!old) {
    old = fc_malloc(sizeof(*old));
    memset(old, 0, sizeof(*old));
    force_send_of_unchanged = TRUE;
  }

  differ = (old->unit_id != real_packet->unit_id);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 0);}

  differ = (old->city_id != real_packet->city_id);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 1);}

  if (different == 0 && !force_send_of_unchanged) {
    return 0;
  }

  DIO_BV_PUT(&dout, fields);

  if (BV_ISSET(fields, 0)) {
    dio_put_uint16(&dout, real_packet->unit_id);
  }
  if (BV_ISSET(fields, 1)) {
    dio_put_uint16(&dout, real_packet->city_id);
  }


  if (old_from_hash) {
    hash_delete_entry(*hash, old);
  }

  clone = old;

  *clone = *real_packet;
  hash_insert(*hash, clone, clone);
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_unit_change_homecity(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_UNIT_CHANGE_HOMECITY] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_UNIT_CHANGE_HOMECITY] = variant;
}

struct packet_unit_change_homecity *receive_packet_unit_change_homecity(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Receiving packet_unit_change_homecity at the client.");
  }
  ensure_valid_variant_packet_unit_change_homecity(pc);

  switch(pc->phs.variant[PACKET_UNIT_CHANGE_HOMECITY]) {
    case 100: return receive_packet_unit_change_homecity_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_unit_change_homecity(struct connection *pc, const struct packet_unit_change_homecity *packet)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Sending packet_unit_change_homecity from the server.");
  }
  ensure_valid_variant_packet_unit_change_homecity(pc);

  switch(pc->phs.variant[PACKET_UNIT_CHANGE_HOMECITY]) {
    case 100: return send_packet_unit_change_homecity_100(pc, packet);
    default: die("unknown variant"); return -1;
  }
}

int dsend_packet_unit_change_homecity(struct connection *pc, int unit_id, int city_id)
{
  struct packet_unit_change_homecity packet, *real_packet = &packet;

  real_packet->unit_id = unit_id;
  real_packet->city_id = city_id;
  
  return send_packet_unit_change_homecity(pc, real_packet);
}

#define hash_packet_unit_establish_trade_100 hash_const

#define cmp_packet_unit_establish_trade_100 cmp_const

BV_DEFINE(packet_unit_establish_trade_100_fields, 1);

static struct packet_unit_establish_trade *receive_packet_unit_establish_trade_100(struct connection *pc, enum packet_type type)
{
  packet_unit_establish_trade_100_fields fields;
  struct packet_unit_establish_trade *old;
  struct hash_table **hash = &pc->phs.received[type];
  struct packet_unit_establish_trade *clone;
  RECEIVE_PACKET_START(packet_unit_establish_trade, real_packet);

  DIO_BV_GET(&din, fields);


  if (!*hash) {
    *hash = hash_new(hash_packet_unit_establish_trade_100, cmp_packet_unit_establish_trade_100);
  }
  old = hash_delete_entry(*hash, real_packet);

  if (old) {
    *real_packet = *old;
  } else {
    memset(real_packet, 0, sizeof(*real_packet));
  }

  if (BV_ISSET(fields, 0)) {
    {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->unit_id = readin;
    }
  }

  clone = fc_malloc(sizeof(*clone));
  *clone = *real_packet;
  if (old) {
    free(old);
  }
  hash_insert(*hash, clone, clone);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_unit_establish_trade_100(struct connection *pc, const struct packet_unit_establish_trade *packet)
{
  const struct packet_unit_establish_trade *real_packet = packet;
  packet_unit_establish_trade_100_fields fields;
  struct packet_unit_establish_trade *old, *clone;
  bool differ, old_from_hash, force_send_of_unchanged = TRUE;
  struct hash_table **hash = &pc->phs.sent[PACKET_UNIT_ESTABLISH_TRADE];
  int different = 0;
  SEND_PACKET_START(PACKET_UNIT_ESTABLISH_TRADE);

  if (!*hash) {
    *hash = hash_new(hash_packet_unit_establish_trade_100, cmp_packet_unit_establish_trade_100);
  }
  BV_CLR_ALL(fields);

  old = hash_lookup_data(*hash, real_packet);
  old_from_hash = (old != NULL);
  if (!old) {
    old = fc_malloc(sizeof(*old));
    memset(old, 0, sizeof(*old));
    force_send_of_unchanged = TRUE;
  }

  differ = (old->unit_id != real_packet->unit_id);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 0);}

  if (different == 0 && !force_send_of_unchanged) {
    return 0;
  }

  DIO_BV_PUT(&dout, fields);

  if (BV_ISSET(fields, 0)) {
    dio_put_uint16(&dout, real_packet->unit_id);
  }


  if (old_from_hash) {
    hash_delete_entry(*hash, old);
  }

  clone = old;

  *clone = *real_packet;
  hash_insert(*hash, clone, clone);
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_unit_establish_trade(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_UNIT_ESTABLISH_TRADE] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_UNIT_ESTABLISH_TRADE] = variant;
}

struct packet_unit_establish_trade *receive_packet_unit_establish_trade(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Receiving packet_unit_establish_trade at the client.");
  }
  ensure_valid_variant_packet_unit_establish_trade(pc);

  switch(pc->phs.variant[PACKET_UNIT_ESTABLISH_TRADE]) {
    case 100: return receive_packet_unit_establish_trade_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_unit_establish_trade(struct connection *pc, const struct packet_unit_establish_trade *packet)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Sending packet_unit_establish_trade from the server.");
  }
  ensure_valid_variant_packet_unit_establish_trade(pc);

  switch(pc->phs.variant[PACKET_UNIT_ESTABLISH_TRADE]) {
    case 100: return send_packet_unit_establish_trade_100(pc, packet);
    default: die("unknown variant"); return -1;
  }
}

int dsend_packet_unit_establish_trade(struct connection *pc, int unit_id)
{
  struct packet_unit_establish_trade packet, *real_packet = &packet;

  real_packet->unit_id = unit_id;
  
  return send_packet_unit_establish_trade(pc, real_packet);
}

#define hash_packet_unit_help_build_wonder_100 hash_const

#define cmp_packet_unit_help_build_wonder_100 cmp_const

BV_DEFINE(packet_unit_help_build_wonder_100_fields, 1);

static struct packet_unit_help_build_wonder *receive_packet_unit_help_build_wonder_100(struct connection *pc, enum packet_type type)
{
  packet_unit_help_build_wonder_100_fields fields;
  struct packet_unit_help_build_wonder *old;
  struct hash_table **hash = &pc->phs.received[type];
  struct packet_unit_help_build_wonder *clone;
  RECEIVE_PACKET_START(packet_unit_help_build_wonder, real_packet);

  DIO_BV_GET(&din, fields);


  if (!*hash) {
    *hash = hash_new(hash_packet_unit_help_build_wonder_100, cmp_packet_unit_help_build_wonder_100);
  }
  old = hash_delete_entry(*hash, real_packet);

  if (old) {
    *real_packet = *old;
  } else {
    memset(real_packet, 0, sizeof(*real_packet));
  }

  if (BV_ISSET(fields, 0)) {
    {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->unit_id = readin;
    }
  }

  clone = fc_malloc(sizeof(*clone));
  *clone = *real_packet;
  if (old) {
    free(old);
  }
  hash_insert(*hash, clone, clone);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_unit_help_build_wonder_100(struct connection *pc, const struct packet_unit_help_build_wonder *packet)
{
  const struct packet_unit_help_build_wonder *real_packet = packet;
  packet_unit_help_build_wonder_100_fields fields;
  struct packet_unit_help_build_wonder *old, *clone;
  bool differ, old_from_hash, force_send_of_unchanged = TRUE;
  struct hash_table **hash = &pc->phs.sent[PACKET_UNIT_HELP_BUILD_WONDER];
  int different = 0;
  SEND_PACKET_START(PACKET_UNIT_HELP_BUILD_WONDER);

  if (!*hash) {
    *hash = hash_new(hash_packet_unit_help_build_wonder_100, cmp_packet_unit_help_build_wonder_100);
  }
  BV_CLR_ALL(fields);

  old = hash_lookup_data(*hash, real_packet);
  old_from_hash = (old != NULL);
  if (!old) {
    old = fc_malloc(sizeof(*old));
    memset(old, 0, sizeof(*old));
    force_send_of_unchanged = TRUE;
  }

  differ = (old->unit_id != real_packet->unit_id);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 0);}

  if (different == 0 && !force_send_of_unchanged) {
    return 0;
  }

  DIO_BV_PUT(&dout, fields);

  if (BV_ISSET(fields, 0)) {
    dio_put_uint16(&dout, real_packet->unit_id);
  }


  if (old_from_hash) {
    hash_delete_entry(*hash, old);
  }

  clone = old;

  *clone = *real_packet;
  hash_insert(*hash, clone, clone);
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_unit_help_build_wonder(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_UNIT_HELP_BUILD_WONDER] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_UNIT_HELP_BUILD_WONDER] = variant;
}

struct packet_unit_help_build_wonder *receive_packet_unit_help_build_wonder(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Receiving packet_unit_help_build_wonder at the client.");
  }
  ensure_valid_variant_packet_unit_help_build_wonder(pc);

  switch(pc->phs.variant[PACKET_UNIT_HELP_BUILD_WONDER]) {
    case 100: return receive_packet_unit_help_build_wonder_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_unit_help_build_wonder(struct connection *pc, const struct packet_unit_help_build_wonder *packet)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Sending packet_unit_help_build_wonder from the server.");
  }
  ensure_valid_variant_packet_unit_help_build_wonder(pc);

  switch(pc->phs.variant[PACKET_UNIT_HELP_BUILD_WONDER]) {
    case 100: return send_packet_unit_help_build_wonder_100(pc, packet);
    default: die("unknown variant"); return -1;
  }
}

int dsend_packet_unit_help_build_wonder(struct connection *pc, int unit_id)
{
  struct packet_unit_help_build_wonder packet, *real_packet = &packet;

  real_packet->unit_id = unit_id;
  
  return send_packet_unit_help_build_wonder(pc, real_packet);
}

#define hash_packet_unit_goto_100 hash_const

#define cmp_packet_unit_goto_100 cmp_const

BV_DEFINE(packet_unit_goto_100_fields, 3);

static struct packet_unit_goto *receive_packet_unit_goto_100(struct connection *pc, enum packet_type type)
{
  packet_unit_goto_100_fields fields;
  struct packet_unit_goto *old;
  struct hash_table **hash = &pc->phs.received[type];
  struct packet_unit_goto *clone;
  RECEIVE_PACKET_START(packet_unit_goto, real_packet);

  DIO_BV_GET(&din, fields);


  if (!*hash) {
    *hash = hash_new(hash_packet_unit_goto_100, cmp_packet_unit_goto_100);
  }
  old = hash_delete_entry(*hash, real_packet);

  if (old) {
    *real_packet = *old;
  } else {
    memset(real_packet, 0, sizeof(*real_packet));
  }

  if (BV_ISSET(fields, 0)) {
    {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->unit_id = readin;
    }
  }
  if (BV_ISSET(fields, 1)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->x = readin;
    }
  }
  if (BV_ISSET(fields, 2)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->y = readin;
    }
  }

  clone = fc_malloc(sizeof(*clone));
  *clone = *real_packet;
  if (old) {
    free(old);
  }
  hash_insert(*hash, clone, clone);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_unit_goto_100(struct connection *pc, const struct packet_unit_goto *packet)
{
  const struct packet_unit_goto *real_packet = packet;
  packet_unit_goto_100_fields fields;
  struct packet_unit_goto *old, *clone;
  bool differ, old_from_hash, force_send_of_unchanged = TRUE;
  struct hash_table **hash = &pc->phs.sent[PACKET_UNIT_GOTO];
  int different = 0;
  SEND_PACKET_START(PACKET_UNIT_GOTO);

  if (!*hash) {
    *hash = hash_new(hash_packet_unit_goto_100, cmp_packet_unit_goto_100);
  }
  BV_CLR_ALL(fields);

  old = hash_lookup_data(*hash, real_packet);
  old_from_hash = (old != NULL);
  if (!old) {
    old = fc_malloc(sizeof(*old));
    memset(old, 0, sizeof(*old));
    force_send_of_unchanged = TRUE;
  }

  differ = (old->unit_id != real_packet->unit_id);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 0);}

  differ = (old->x != real_packet->x);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 1);}

  differ = (old->y != real_packet->y);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 2);}

  if (different == 0 && !force_send_of_unchanged) {
    return 0;
  }

  DIO_BV_PUT(&dout, fields);

  if (BV_ISSET(fields, 0)) {
    dio_put_uint16(&dout, real_packet->unit_id);
  }
  if (BV_ISSET(fields, 1)) {
    dio_put_uint8(&dout, real_packet->x);
  }
  if (BV_ISSET(fields, 2)) {
    dio_put_uint8(&dout, real_packet->y);
  }


  if (old_from_hash) {
    hash_delete_entry(*hash, old);
  }

  clone = old;

  *clone = *real_packet;
  hash_insert(*hash, clone, clone);
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_unit_goto(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_UNIT_GOTO] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_UNIT_GOTO] = variant;
}

struct packet_unit_goto *receive_packet_unit_goto(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Receiving packet_unit_goto at the client.");
  }
  ensure_valid_variant_packet_unit_goto(pc);

  switch(pc->phs.variant[PACKET_UNIT_GOTO]) {
    case 100: return receive_packet_unit_goto_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_unit_goto(struct connection *pc, const struct packet_unit_goto *packet)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Sending packet_unit_goto from the server.");
  }
  ensure_valid_variant_packet_unit_goto(pc);

  switch(pc->phs.variant[PACKET_UNIT_GOTO]) {
    case 100: return send_packet_unit_goto_100(pc, packet);
    default: die("unknown variant"); return -1;
  }
}

int dsend_packet_unit_goto(struct connection *pc, int unit_id, int x, int y)
{
  struct packet_unit_goto packet, *real_packet = &packet;

  real_packet->unit_id = unit_id;
  real_packet->x = x;
  real_packet->y = y;
  
  return send_packet_unit_goto(pc, real_packet);
}

#define hash_packet_unit_orders_100 hash_const

#define cmp_packet_unit_orders_100 cmp_const

BV_DEFINE(packet_unit_orders_100_fields, 9);

static struct packet_unit_orders *receive_packet_unit_orders_100(struct connection *pc, enum packet_type type)
{
  packet_unit_orders_100_fields fields;
  struct packet_unit_orders *old;
  struct hash_table **hash = &pc->phs.received[type];
  struct packet_unit_orders *clone;
  RECEIVE_PACKET_START(packet_unit_orders, real_packet);

  DIO_BV_GET(&din, fields);


  if (!*hash) {
    *hash = hash_new(hash_packet_unit_orders_100, cmp_packet_unit_orders_100);
  }
  old = hash_delete_entry(*hash, real_packet);

  if (old) {
    *real_packet = *old;
  } else {
    memset(real_packet, 0, sizeof(*real_packet));
  }

  if (BV_ISSET(fields, 0)) {
    {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->unit_id = readin;
    }
  }
  if (BV_ISSET(fields, 1)) {
    {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->length = readin;
    }
  }
  real_packet->repeat = BV_ISSET(fields, 2);
  real_packet->vigilant = BV_ISSET(fields, 3);
  if (BV_ISSET(fields, 4)) {
    
    {
      int i;
    
      if(real_packet->length > MAX_LEN_ROUTE) {
        freelog(LOG_ERROR, "packets_gen.c: WARNING: truncation array");
        real_packet->length = MAX_LEN_ROUTE;
      }
      for (i = 0; i < real_packet->length; i++) {
        {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->orders[i] = readin;
    }
      }
    }
  }
  if (BV_ISSET(fields, 5)) {
    
    {
      int i;
    
      if(real_packet->length > MAX_LEN_ROUTE) {
        freelog(LOG_ERROR, "packets_gen.c: WARNING: truncation array");
        real_packet->length = MAX_LEN_ROUTE;
      }
      for (i = 0; i < real_packet->length; i++) {
        {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->dir[i] = readin;
    }
      }
    }
  }
  if (BV_ISSET(fields, 6)) {
    
    {
      int i;
    
      if(real_packet->length > MAX_LEN_ROUTE) {
        freelog(LOG_ERROR, "packets_gen.c: WARNING: truncation array");
        real_packet->length = MAX_LEN_ROUTE;
      }
      for (i = 0; i < real_packet->length; i++) {
        {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->activity[i] = readin;
    }
      }
    }
  }
  if (BV_ISSET(fields, 7)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->dest_x = readin;
    }
  }
  if (BV_ISSET(fields, 8)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->dest_y = readin;
    }
  }

  clone = fc_malloc(sizeof(*clone));
  *clone = *real_packet;
  if (old) {
    free(old);
  }
  hash_insert(*hash, clone, clone);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_unit_orders_100(struct connection *pc, const struct packet_unit_orders *packet)
{
  const struct packet_unit_orders *real_packet = packet;
  packet_unit_orders_100_fields fields;
  struct packet_unit_orders *old, *clone;
  bool differ, old_from_hash, force_send_of_unchanged = TRUE;
  struct hash_table **hash = &pc->phs.sent[PACKET_UNIT_ORDERS];
  int different = 0;
  SEND_PACKET_START(PACKET_UNIT_ORDERS);

  if (!*hash) {
    *hash = hash_new(hash_packet_unit_orders_100, cmp_packet_unit_orders_100);
  }
  BV_CLR_ALL(fields);

  old = hash_lookup_data(*hash, real_packet);
  old_from_hash = (old != NULL);
  if (!old) {
    old = fc_malloc(sizeof(*old));
    memset(old, 0, sizeof(*old));
    force_send_of_unchanged = TRUE;
  }

  differ = (old->unit_id != real_packet->unit_id);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 0);}

  differ = (old->length != real_packet->length);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 1);}

  differ = (old->repeat != real_packet->repeat);
  if(differ) {different++;}
  if(packet->repeat) {BV_SET(fields, 2);}

  differ = (old->vigilant != real_packet->vigilant);
  if(differ) {different++;}
  if(packet->vigilant) {BV_SET(fields, 3);}


    {
      differ = (old->length != real_packet->length);
      if(!differ) {
        int i;
        for (i = 0; i < real_packet->length; i++) {
          if (old->orders[i] != real_packet->orders[i]) {
            differ = TRUE;
            break;
          }
        }
      }
    }
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 4);}


    {
      differ = (old->length != real_packet->length);
      if(!differ) {
        int i;
        for (i = 0; i < real_packet->length; i++) {
          if (old->dir[i] != real_packet->dir[i]) {
            differ = TRUE;
            break;
          }
        }
      }
    }
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 5);}


    {
      differ = (old->length != real_packet->length);
      if(!differ) {
        int i;
        for (i = 0; i < real_packet->length; i++) {
          if (old->activity[i] != real_packet->activity[i]) {
            differ = TRUE;
            break;
          }
        }
      }
    }
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 6);}

  differ = (old->dest_x != real_packet->dest_x);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 7);}

  differ = (old->dest_y != real_packet->dest_y);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 8);}

  if (different == 0 && !force_send_of_unchanged) {
    return 0;
  }

  DIO_BV_PUT(&dout, fields);

  if (BV_ISSET(fields, 0)) {
    dio_put_uint16(&dout, real_packet->unit_id);
  }
  if (BV_ISSET(fields, 1)) {
    dio_put_uint16(&dout, real_packet->length);
  }
  /* field 2 is folded into the header */
  /* field 3 is folded into the header */
  if (BV_ISSET(fields, 4)) {
  
    {
      int i;

      for (i = 0; i < real_packet->length; i++) {
        dio_put_uint8(&dout, real_packet->orders[i]);
      }
    } 
  }
  if (BV_ISSET(fields, 5)) {
  
    {
      int i;

      for (i = 0; i < real_packet->length; i++) {
        dio_put_uint8(&dout, real_packet->dir[i]);
      }
    } 
  }
  if (BV_ISSET(fields, 6)) {
  
    {
      int i;

      for (i = 0; i < real_packet->length; i++) {
        dio_put_uint8(&dout, real_packet->activity[i]);
      }
    } 
  }
  if (BV_ISSET(fields, 7)) {
    dio_put_uint8(&dout, real_packet->dest_x);
  }
  if (BV_ISSET(fields, 8)) {
    dio_put_uint8(&dout, real_packet->dest_y);
  }


  if (old_from_hash) {
    hash_delete_entry(*hash, old);
  }

  clone = old;

  *clone = *real_packet;
  hash_insert(*hash, clone, clone);
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_unit_orders(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_UNIT_ORDERS] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_UNIT_ORDERS] = variant;
}

struct packet_unit_orders *receive_packet_unit_orders(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Receiving packet_unit_orders at the client.");
  }
  ensure_valid_variant_packet_unit_orders(pc);

  switch(pc->phs.variant[PACKET_UNIT_ORDERS]) {
    case 100: return receive_packet_unit_orders_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_unit_orders(struct connection *pc, const struct packet_unit_orders *packet)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Sending packet_unit_orders from the server.");
  }
  ensure_valid_variant_packet_unit_orders(pc);

  switch(pc->phs.variant[PACKET_UNIT_ORDERS]) {
    case 100: return send_packet_unit_orders_100(pc, packet);
    default: die("unknown variant"); return -1;
  }
}

#define hash_packet_unit_auto_100 hash_const

#define cmp_packet_unit_auto_100 cmp_const

BV_DEFINE(packet_unit_auto_100_fields, 1);

static struct packet_unit_auto *receive_packet_unit_auto_100(struct connection *pc, enum packet_type type)
{
  packet_unit_auto_100_fields fields;
  struct packet_unit_auto *old;
  struct hash_table **hash = &pc->phs.received[type];
  struct packet_unit_auto *clone;
  RECEIVE_PACKET_START(packet_unit_auto, real_packet);

  DIO_BV_GET(&din, fields);


  if (!*hash) {
    *hash = hash_new(hash_packet_unit_auto_100, cmp_packet_unit_auto_100);
  }
  old = hash_delete_entry(*hash, real_packet);

  if (old) {
    *real_packet = *old;
  } else {
    memset(real_packet, 0, sizeof(*real_packet));
  }

  if (BV_ISSET(fields, 0)) {
    {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->unit_id = readin;
    }
  }

  clone = fc_malloc(sizeof(*clone));
  *clone = *real_packet;
  if (old) {
    free(old);
  }
  hash_insert(*hash, clone, clone);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_unit_auto_100(struct connection *pc, const struct packet_unit_auto *packet)
{
  const struct packet_unit_auto *real_packet = packet;
  packet_unit_auto_100_fields fields;
  struct packet_unit_auto *old, *clone;
  bool differ, old_from_hash, force_send_of_unchanged = TRUE;
  struct hash_table **hash = &pc->phs.sent[PACKET_UNIT_AUTO];
  int different = 0;
  SEND_PACKET_START(PACKET_UNIT_AUTO);

  if (!*hash) {
    *hash = hash_new(hash_packet_unit_auto_100, cmp_packet_unit_auto_100);
  }
  BV_CLR_ALL(fields);

  old = hash_lookup_data(*hash, real_packet);
  old_from_hash = (old != NULL);
  if (!old) {
    old = fc_malloc(sizeof(*old));
    memset(old, 0, sizeof(*old));
    force_send_of_unchanged = TRUE;
  }

  differ = (old->unit_id != real_packet->unit_id);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 0);}

  if (different == 0 && !force_send_of_unchanged) {
    return 0;
  }

  DIO_BV_PUT(&dout, fields);

  if (BV_ISSET(fields, 0)) {
    dio_put_uint16(&dout, real_packet->unit_id);
  }


  if (old_from_hash) {
    hash_delete_entry(*hash, old);
  }

  clone = old;

  *clone = *real_packet;
  hash_insert(*hash, clone, clone);
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_unit_auto(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_UNIT_AUTO] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_UNIT_AUTO] = variant;
}

struct packet_unit_auto *receive_packet_unit_auto(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Receiving packet_unit_auto at the client.");
  }
  ensure_valid_variant_packet_unit_auto(pc);

  switch(pc->phs.variant[PACKET_UNIT_AUTO]) {
    case 100: return receive_packet_unit_auto_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_unit_auto(struct connection *pc, const struct packet_unit_auto *packet)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Sending packet_unit_auto from the server.");
  }
  ensure_valid_variant_packet_unit_auto(pc);

  switch(pc->phs.variant[PACKET_UNIT_AUTO]) {
    case 100: return send_packet_unit_auto_100(pc, packet);
    default: die("unknown variant"); return -1;
  }
}

int dsend_packet_unit_auto(struct connection *pc, int unit_id)
{
  struct packet_unit_auto packet, *real_packet = &packet;

  real_packet->unit_id = unit_id;
  
  return send_packet_unit_auto(pc, real_packet);
}

#define hash_packet_unit_load_100 hash_const

#define cmp_packet_unit_load_100 cmp_const

BV_DEFINE(packet_unit_load_100_fields, 2);

static struct packet_unit_load *receive_packet_unit_load_100(struct connection *pc, enum packet_type type)
{
  packet_unit_load_100_fields fields;
  struct packet_unit_load *old;
  struct hash_table **hash = &pc->phs.received[type];
  struct packet_unit_load *clone;
  RECEIVE_PACKET_START(packet_unit_load, real_packet);

  DIO_BV_GET(&din, fields);


  if (!*hash) {
    *hash = hash_new(hash_packet_unit_load_100, cmp_packet_unit_load_100);
  }
  old = hash_delete_entry(*hash, real_packet);

  if (old) {
    *real_packet = *old;
  } else {
    memset(real_packet, 0, sizeof(*real_packet));
  }

  if (BV_ISSET(fields, 0)) {
    {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->cargo_id = readin;
    }
  }
  if (BV_ISSET(fields, 1)) {
    {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->transporter_id = readin;
    }
  }

  clone = fc_malloc(sizeof(*clone));
  *clone = *real_packet;
  if (old) {
    free(old);
  }
  hash_insert(*hash, clone, clone);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_unit_load_100(struct connection *pc, const struct packet_unit_load *packet)
{
  const struct packet_unit_load *real_packet = packet;
  packet_unit_load_100_fields fields;
  struct packet_unit_load *old, *clone;
  bool differ, old_from_hash, force_send_of_unchanged = TRUE;
  struct hash_table **hash = &pc->phs.sent[PACKET_UNIT_LOAD];
  int different = 0;
  SEND_PACKET_START(PACKET_UNIT_LOAD);

  if (!*hash) {
    *hash = hash_new(hash_packet_unit_load_100, cmp_packet_unit_load_100);
  }
  BV_CLR_ALL(fields);

  old = hash_lookup_data(*hash, real_packet);
  old_from_hash = (old != NULL);
  if (!old) {
    old = fc_malloc(sizeof(*old));
    memset(old, 0, sizeof(*old));
    force_send_of_unchanged = TRUE;
  }

  differ = (old->cargo_id != real_packet->cargo_id);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 0);}

  differ = (old->transporter_id != real_packet->transporter_id);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 1);}

  if (different == 0 && !force_send_of_unchanged) {
    return 0;
  }

  DIO_BV_PUT(&dout, fields);

  if (BV_ISSET(fields, 0)) {
    dio_put_uint16(&dout, real_packet->cargo_id);
  }
  if (BV_ISSET(fields, 1)) {
    dio_put_uint16(&dout, real_packet->transporter_id);
  }


  if (old_from_hash) {
    hash_delete_entry(*hash, old);
  }

  clone = old;

  *clone = *real_packet;
  hash_insert(*hash, clone, clone);
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_unit_load(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_UNIT_LOAD] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_UNIT_LOAD] = variant;
}

struct packet_unit_load *receive_packet_unit_load(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Receiving packet_unit_load at the client.");
  }
  ensure_valid_variant_packet_unit_load(pc);

  switch(pc->phs.variant[PACKET_UNIT_LOAD]) {
    case 100: return receive_packet_unit_load_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_unit_load(struct connection *pc, const struct packet_unit_load *packet)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Sending packet_unit_load from the server.");
  }
  ensure_valid_variant_packet_unit_load(pc);

  switch(pc->phs.variant[PACKET_UNIT_LOAD]) {
    case 100: return send_packet_unit_load_100(pc, packet);
    default: die("unknown variant"); return -1;
  }
}

int dsend_packet_unit_load(struct connection *pc, int cargo_id, int transporter_id)
{
  struct packet_unit_load packet, *real_packet = &packet;

  real_packet->cargo_id = cargo_id;
  real_packet->transporter_id = transporter_id;
  
  return send_packet_unit_load(pc, real_packet);
}

#define hash_packet_unit_unload_100 hash_const

#define cmp_packet_unit_unload_100 cmp_const

BV_DEFINE(packet_unit_unload_100_fields, 2);

static struct packet_unit_unload *receive_packet_unit_unload_100(struct connection *pc, enum packet_type type)
{
  packet_unit_unload_100_fields fields;
  struct packet_unit_unload *old;
  struct hash_table **hash = &pc->phs.received[type];
  struct packet_unit_unload *clone;
  RECEIVE_PACKET_START(packet_unit_unload, real_packet);

  DIO_BV_GET(&din, fields);


  if (!*hash) {
    *hash = hash_new(hash_packet_unit_unload_100, cmp_packet_unit_unload_100);
  }
  old = hash_delete_entry(*hash, real_packet);

  if (old) {
    *real_packet = *old;
  } else {
    memset(real_packet, 0, sizeof(*real_packet));
  }

  if (BV_ISSET(fields, 0)) {
    {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->cargo_id = readin;
    }
  }
  if (BV_ISSET(fields, 1)) {
    {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->transporter_id = readin;
    }
  }

  clone = fc_malloc(sizeof(*clone));
  *clone = *real_packet;
  if (old) {
    free(old);
  }
  hash_insert(*hash, clone, clone);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_unit_unload_100(struct connection *pc, const struct packet_unit_unload *packet)
{
  const struct packet_unit_unload *real_packet = packet;
  packet_unit_unload_100_fields fields;
  struct packet_unit_unload *old, *clone;
  bool differ, old_from_hash, force_send_of_unchanged = TRUE;
  struct hash_table **hash = &pc->phs.sent[PACKET_UNIT_UNLOAD];
  int different = 0;
  SEND_PACKET_START(PACKET_UNIT_UNLOAD);

  if (!*hash) {
    *hash = hash_new(hash_packet_unit_unload_100, cmp_packet_unit_unload_100);
  }
  BV_CLR_ALL(fields);

  old = hash_lookup_data(*hash, real_packet);
  old_from_hash = (old != NULL);
  if (!old) {
    old = fc_malloc(sizeof(*old));
    memset(old, 0, sizeof(*old));
    force_send_of_unchanged = TRUE;
  }

  differ = (old->cargo_id != real_packet->cargo_id);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 0);}

  differ = (old->transporter_id != real_packet->transporter_id);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 1);}

  if (different == 0 && !force_send_of_unchanged) {
    return 0;
  }

  DIO_BV_PUT(&dout, fields);

  if (BV_ISSET(fields, 0)) {
    dio_put_uint16(&dout, real_packet->cargo_id);
  }
  if (BV_ISSET(fields, 1)) {
    dio_put_uint16(&dout, real_packet->transporter_id);
  }


  if (old_from_hash) {
    hash_delete_entry(*hash, old);
  }

  clone = old;

  *clone = *real_packet;
  hash_insert(*hash, clone, clone);
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_unit_unload(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_UNIT_UNLOAD] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_UNIT_UNLOAD] = variant;
}

struct packet_unit_unload *receive_packet_unit_unload(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Receiving packet_unit_unload at the client.");
  }
  ensure_valid_variant_packet_unit_unload(pc);

  switch(pc->phs.variant[PACKET_UNIT_UNLOAD]) {
    case 100: return receive_packet_unit_unload_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_unit_unload(struct connection *pc, const struct packet_unit_unload *packet)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Sending packet_unit_unload from the server.");
  }
  ensure_valid_variant_packet_unit_unload(pc);

  switch(pc->phs.variant[PACKET_UNIT_UNLOAD]) {
    case 100: return send_packet_unit_unload_100(pc, packet);
    default: die("unknown variant"); return -1;
  }
}

int dsend_packet_unit_unload(struct connection *pc, int cargo_id, int transporter_id)
{
  struct packet_unit_unload packet, *real_packet = &packet;

  real_packet->cargo_id = cargo_id;
  real_packet->transporter_id = transporter_id;
  
  return send_packet_unit_unload(pc, real_packet);
}

#define hash_packet_unit_upgrade_100 hash_const

#define cmp_packet_unit_upgrade_100 cmp_const

BV_DEFINE(packet_unit_upgrade_100_fields, 1);

static struct packet_unit_upgrade *receive_packet_unit_upgrade_100(struct connection *pc, enum packet_type type)
{
  packet_unit_upgrade_100_fields fields;
  struct packet_unit_upgrade *old;
  struct hash_table **hash = &pc->phs.received[type];
  struct packet_unit_upgrade *clone;
  RECEIVE_PACKET_START(packet_unit_upgrade, real_packet);

  DIO_BV_GET(&din, fields);


  if (!*hash) {
    *hash = hash_new(hash_packet_unit_upgrade_100, cmp_packet_unit_upgrade_100);
  }
  old = hash_delete_entry(*hash, real_packet);

  if (old) {
    *real_packet = *old;
  } else {
    memset(real_packet, 0, sizeof(*real_packet));
  }

  if (BV_ISSET(fields, 0)) {
    {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->unit_id = readin;
    }
  }

  clone = fc_malloc(sizeof(*clone));
  *clone = *real_packet;
  if (old) {
    free(old);
  }
  hash_insert(*hash, clone, clone);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_unit_upgrade_100(struct connection *pc, const struct packet_unit_upgrade *packet)
{
  const struct packet_unit_upgrade *real_packet = packet;
  packet_unit_upgrade_100_fields fields;
  struct packet_unit_upgrade *old, *clone;
  bool differ, old_from_hash, force_send_of_unchanged = TRUE;
  struct hash_table **hash = &pc->phs.sent[PACKET_UNIT_UPGRADE];
  int different = 0;
  SEND_PACKET_START(PACKET_UNIT_UPGRADE);

  if (!*hash) {
    *hash = hash_new(hash_packet_unit_upgrade_100, cmp_packet_unit_upgrade_100);
  }
  BV_CLR_ALL(fields);

  old = hash_lookup_data(*hash, real_packet);
  old_from_hash = (old != NULL);
  if (!old) {
    old = fc_malloc(sizeof(*old));
    memset(old, 0, sizeof(*old));
    force_send_of_unchanged = TRUE;
  }

  differ = (old->unit_id != real_packet->unit_id);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 0);}

  if (different == 0 && !force_send_of_unchanged) {
    return 0;
  }

  DIO_BV_PUT(&dout, fields);

  if (BV_ISSET(fields, 0)) {
    dio_put_uint16(&dout, real_packet->unit_id);
  }


  if (old_from_hash) {
    hash_delete_entry(*hash, old);
  }

  clone = old;

  *clone = *real_packet;
  hash_insert(*hash, clone, clone);
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_unit_upgrade(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_UNIT_UPGRADE] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_UNIT_UPGRADE] = variant;
}

struct packet_unit_upgrade *receive_packet_unit_upgrade(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Receiving packet_unit_upgrade at the client.");
  }
  ensure_valid_variant_packet_unit_upgrade(pc);

  switch(pc->phs.variant[PACKET_UNIT_UPGRADE]) {
    case 100: return receive_packet_unit_upgrade_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_unit_upgrade(struct connection *pc, const struct packet_unit_upgrade *packet)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Sending packet_unit_upgrade from the server.");
  }
  ensure_valid_variant_packet_unit_upgrade(pc);

  switch(pc->phs.variant[PACKET_UNIT_UPGRADE]) {
    case 100: return send_packet_unit_upgrade_100(pc, packet);
    default: die("unknown variant"); return -1;
  }
}

int dsend_packet_unit_upgrade(struct connection *pc, int unit_id)
{
  struct packet_unit_upgrade packet, *real_packet = &packet;

  real_packet->unit_id = unit_id;
  
  return send_packet_unit_upgrade(pc, real_packet);
}

#define hash_packet_unit_nuke_100 hash_const

#define cmp_packet_unit_nuke_100 cmp_const

BV_DEFINE(packet_unit_nuke_100_fields, 1);

static struct packet_unit_nuke *receive_packet_unit_nuke_100(struct connection *pc, enum packet_type type)
{
  packet_unit_nuke_100_fields fields;
  struct packet_unit_nuke *old;
  struct hash_table **hash = &pc->phs.received[type];
  struct packet_unit_nuke *clone;
  RECEIVE_PACKET_START(packet_unit_nuke, real_packet);

  DIO_BV_GET(&din, fields);


  if (!*hash) {
    *hash = hash_new(hash_packet_unit_nuke_100, cmp_packet_unit_nuke_100);
  }
  old = hash_delete_entry(*hash, real_packet);

  if (old) {
    *real_packet = *old;
  } else {
    memset(real_packet, 0, sizeof(*real_packet));
  }

  if (BV_ISSET(fields, 0)) {
    {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->unit_id = readin;
    }
  }

  clone = fc_malloc(sizeof(*clone));
  *clone = *real_packet;
  if (old) {
    free(old);
  }
  hash_insert(*hash, clone, clone);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_unit_nuke_100(struct connection *pc, const struct packet_unit_nuke *packet)
{
  const struct packet_unit_nuke *real_packet = packet;
  packet_unit_nuke_100_fields fields;
  struct packet_unit_nuke *old, *clone;
  bool differ, old_from_hash, force_send_of_unchanged = TRUE;
  struct hash_table **hash = &pc->phs.sent[PACKET_UNIT_NUKE];
  int different = 0;
  SEND_PACKET_START(PACKET_UNIT_NUKE);

  if (!*hash) {
    *hash = hash_new(hash_packet_unit_nuke_100, cmp_packet_unit_nuke_100);
  }
  BV_CLR_ALL(fields);

  old = hash_lookup_data(*hash, real_packet);
  old_from_hash = (old != NULL);
  if (!old) {
    old = fc_malloc(sizeof(*old));
    memset(old, 0, sizeof(*old));
    force_send_of_unchanged = TRUE;
  }

  differ = (old->unit_id != real_packet->unit_id);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 0);}

  if (different == 0 && !force_send_of_unchanged) {
    return 0;
  }

  DIO_BV_PUT(&dout, fields);

  if (BV_ISSET(fields, 0)) {
    dio_put_uint16(&dout, real_packet->unit_id);
  }


  if (old_from_hash) {
    hash_delete_entry(*hash, old);
  }

  clone = old;

  *clone = *real_packet;
  hash_insert(*hash, clone, clone);
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_unit_nuke(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_UNIT_NUKE] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_UNIT_NUKE] = variant;
}

struct packet_unit_nuke *receive_packet_unit_nuke(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Receiving packet_unit_nuke at the client.");
  }
  ensure_valid_variant_packet_unit_nuke(pc);

  switch(pc->phs.variant[PACKET_UNIT_NUKE]) {
    case 100: return receive_packet_unit_nuke_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_unit_nuke(struct connection *pc, const struct packet_unit_nuke *packet)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Sending packet_unit_nuke from the server.");
  }
  ensure_valid_variant_packet_unit_nuke(pc);

  switch(pc->phs.variant[PACKET_UNIT_NUKE]) {
    case 100: return send_packet_unit_nuke_100(pc, packet);
    default: die("unknown variant"); return -1;
  }
}

int dsend_packet_unit_nuke(struct connection *pc, int unit_id)
{
  struct packet_unit_nuke packet, *real_packet = &packet;

  real_packet->unit_id = unit_id;
  
  return send_packet_unit_nuke(pc, real_packet);
}

#define hash_packet_unit_paradrop_to_100 hash_const

#define cmp_packet_unit_paradrop_to_100 cmp_const

BV_DEFINE(packet_unit_paradrop_to_100_fields, 3);

static struct packet_unit_paradrop_to *receive_packet_unit_paradrop_to_100(struct connection *pc, enum packet_type type)
{
  packet_unit_paradrop_to_100_fields fields;
  struct packet_unit_paradrop_to *old;
  struct hash_table **hash = &pc->phs.received[type];
  struct packet_unit_paradrop_to *clone;
  RECEIVE_PACKET_START(packet_unit_paradrop_to, real_packet);

  DIO_BV_GET(&din, fields);


  if (!*hash) {
    *hash = hash_new(hash_packet_unit_paradrop_to_100, cmp_packet_unit_paradrop_to_100);
  }
  old = hash_delete_entry(*hash, real_packet);

  if (old) {
    *real_packet = *old;
  } else {
    memset(real_packet, 0, sizeof(*real_packet));
  }

  if (BV_ISSET(fields, 0)) {
    {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->unit_id = readin;
    }
  }
  if (BV_ISSET(fields, 1)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->x = readin;
    }
  }
  if (BV_ISSET(fields, 2)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->y = readin;
    }
  }

  clone = fc_malloc(sizeof(*clone));
  *clone = *real_packet;
  if (old) {
    free(old);
  }
  hash_insert(*hash, clone, clone);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_unit_paradrop_to_100(struct connection *pc, const struct packet_unit_paradrop_to *packet)
{
  const struct packet_unit_paradrop_to *real_packet = packet;
  packet_unit_paradrop_to_100_fields fields;
  struct packet_unit_paradrop_to *old, *clone;
  bool differ, old_from_hash, force_send_of_unchanged = TRUE;
  struct hash_table **hash = &pc->phs.sent[PACKET_UNIT_PARADROP_TO];
  int different = 0;
  SEND_PACKET_START(PACKET_UNIT_PARADROP_TO);

  if (!*hash) {
    *hash = hash_new(hash_packet_unit_paradrop_to_100, cmp_packet_unit_paradrop_to_100);
  }
  BV_CLR_ALL(fields);

  old = hash_lookup_data(*hash, real_packet);
  old_from_hash = (old != NULL);
  if (!old) {
    old = fc_malloc(sizeof(*old));
    memset(old, 0, sizeof(*old));
    force_send_of_unchanged = TRUE;
  }

  differ = (old->unit_id != real_packet->unit_id);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 0);}

  differ = (old->x != real_packet->x);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 1);}

  differ = (old->y != real_packet->y);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 2);}

  if (different == 0 && !force_send_of_unchanged) {
    return 0;
  }

  DIO_BV_PUT(&dout, fields);

  if (BV_ISSET(fields, 0)) {
    dio_put_uint16(&dout, real_packet->unit_id);
  }
  if (BV_ISSET(fields, 1)) {
    dio_put_uint8(&dout, real_packet->x);
  }
  if (BV_ISSET(fields, 2)) {
    dio_put_uint8(&dout, real_packet->y);
  }


  if (old_from_hash) {
    hash_delete_entry(*hash, old);
  }

  clone = old;

  *clone = *real_packet;
  hash_insert(*hash, clone, clone);
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_unit_paradrop_to(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_UNIT_PARADROP_TO] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_UNIT_PARADROP_TO] = variant;
}

struct packet_unit_paradrop_to *receive_packet_unit_paradrop_to(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Receiving packet_unit_paradrop_to at the client.");
  }
  ensure_valid_variant_packet_unit_paradrop_to(pc);

  switch(pc->phs.variant[PACKET_UNIT_PARADROP_TO]) {
    case 100: return receive_packet_unit_paradrop_to_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_unit_paradrop_to(struct connection *pc, const struct packet_unit_paradrop_to *packet)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Sending packet_unit_paradrop_to from the server.");
  }
  ensure_valid_variant_packet_unit_paradrop_to(pc);

  switch(pc->phs.variant[PACKET_UNIT_PARADROP_TO]) {
    case 100: return send_packet_unit_paradrop_to_100(pc, packet);
    default: die("unknown variant"); return -1;
  }
}

int dsend_packet_unit_paradrop_to(struct connection *pc, int unit_id, int x, int y)
{
  struct packet_unit_paradrop_to packet, *real_packet = &packet;

  real_packet->unit_id = unit_id;
  real_packet->x = x;
  real_packet->y = y;
  
  return send_packet_unit_paradrop_to(pc, real_packet);
}

#define hash_packet_unit_airlift_100 hash_const

#define cmp_packet_unit_airlift_100 cmp_const

BV_DEFINE(packet_unit_airlift_100_fields, 2);

static struct packet_unit_airlift *receive_packet_unit_airlift_100(struct connection *pc, enum packet_type type)
{
  packet_unit_airlift_100_fields fields;
  struct packet_unit_airlift *old;
  struct hash_table **hash = &pc->phs.received[type];
  struct packet_unit_airlift *clone;
  RECEIVE_PACKET_START(packet_unit_airlift, real_packet);

  DIO_BV_GET(&din, fields);


  if (!*hash) {
    *hash = hash_new(hash_packet_unit_airlift_100, cmp_packet_unit_airlift_100);
  }
  old = hash_delete_entry(*hash, real_packet);

  if (old) {
    *real_packet = *old;
  } else {
    memset(real_packet, 0, sizeof(*real_packet));
  }

  if (BV_ISSET(fields, 0)) {
    {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->unit_id = readin;
    }
  }
  if (BV_ISSET(fields, 1)) {
    {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->city_id = readin;
    }
  }

  clone = fc_malloc(sizeof(*clone));
  *clone = *real_packet;
  if (old) {
    free(old);
  }
  hash_insert(*hash, clone, clone);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_unit_airlift_100(struct connection *pc, const struct packet_unit_airlift *packet)
{
  const struct packet_unit_airlift *real_packet = packet;
  packet_unit_airlift_100_fields fields;
  struct packet_unit_airlift *old, *clone;
  bool differ, old_from_hash, force_send_of_unchanged = TRUE;
  struct hash_table **hash = &pc->phs.sent[PACKET_UNIT_AIRLIFT];
  int different = 0;
  SEND_PACKET_START(PACKET_UNIT_AIRLIFT);

  if (!*hash) {
    *hash = hash_new(hash_packet_unit_airlift_100, cmp_packet_unit_airlift_100);
  }
  BV_CLR_ALL(fields);

  old = hash_lookup_data(*hash, real_packet);
  old_from_hash = (old != NULL);
  if (!old) {
    old = fc_malloc(sizeof(*old));
    memset(old, 0, sizeof(*old));
    force_send_of_unchanged = TRUE;
  }

  differ = (old->unit_id != real_packet->unit_id);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 0);}

  differ = (old->city_id != real_packet->city_id);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 1);}

  if (different == 0 && !force_send_of_unchanged) {
    return 0;
  }

  DIO_BV_PUT(&dout, fields);

  if (BV_ISSET(fields, 0)) {
    dio_put_uint16(&dout, real_packet->unit_id);
  }
  if (BV_ISSET(fields, 1)) {
    dio_put_uint16(&dout, real_packet->city_id);
  }


  if (old_from_hash) {
    hash_delete_entry(*hash, old);
  }

  clone = old;

  *clone = *real_packet;
  hash_insert(*hash, clone, clone);
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_unit_airlift(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_UNIT_AIRLIFT] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_UNIT_AIRLIFT] = variant;
}

struct packet_unit_airlift *receive_packet_unit_airlift(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Receiving packet_unit_airlift at the client.");
  }
  ensure_valid_variant_packet_unit_airlift(pc);

  switch(pc->phs.variant[PACKET_UNIT_AIRLIFT]) {
    case 100: return receive_packet_unit_airlift_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_unit_airlift(struct connection *pc, const struct packet_unit_airlift *packet)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Sending packet_unit_airlift from the server.");
  }
  ensure_valid_variant_packet_unit_airlift(pc);

  switch(pc->phs.variant[PACKET_UNIT_AIRLIFT]) {
    case 100: return send_packet_unit_airlift_100(pc, packet);
    default: die("unknown variant"); return -1;
  }
}

int dsend_packet_unit_airlift(struct connection *pc, int unit_id, int city_id)
{
  struct packet_unit_airlift packet, *real_packet = &packet;

  real_packet->unit_id = unit_id;
  real_packet->city_id = city_id;
  
  return send_packet_unit_airlift(pc, real_packet);
}

#define hash_packet_unit_bribe_inq_100 hash_const

#define cmp_packet_unit_bribe_inq_100 cmp_const

BV_DEFINE(packet_unit_bribe_inq_100_fields, 1);

static struct packet_unit_bribe_inq *receive_packet_unit_bribe_inq_100(struct connection *pc, enum packet_type type)
{
  packet_unit_bribe_inq_100_fields fields;
  struct packet_unit_bribe_inq *old;
  struct hash_table **hash = &pc->phs.received[type];
  struct packet_unit_bribe_inq *clone;
  RECEIVE_PACKET_START(packet_unit_bribe_inq, real_packet);

  DIO_BV_GET(&din, fields);


  if (!*hash) {
    *hash = hash_new(hash_packet_unit_bribe_inq_100, cmp_packet_unit_bribe_inq_100);
  }
  old = hash_delete_entry(*hash, real_packet);

  if (old) {
    *real_packet = *old;
  } else {
    memset(real_packet, 0, sizeof(*real_packet));
  }

  if (BV_ISSET(fields, 0)) {
    {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->unit_id = readin;
    }
  }

  clone = fc_malloc(sizeof(*clone));
  *clone = *real_packet;
  if (old) {
    free(old);
  }
  hash_insert(*hash, clone, clone);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_unit_bribe_inq_100(struct connection *pc, const struct packet_unit_bribe_inq *packet)
{
  const struct packet_unit_bribe_inq *real_packet = packet;
  packet_unit_bribe_inq_100_fields fields;
  struct packet_unit_bribe_inq *old, *clone;
  bool differ, old_from_hash, force_send_of_unchanged = TRUE;
  struct hash_table **hash = &pc->phs.sent[PACKET_UNIT_BRIBE_INQ];
  int different = 0;
  SEND_PACKET_START(PACKET_UNIT_BRIBE_INQ);

  if (!*hash) {
    *hash = hash_new(hash_packet_unit_bribe_inq_100, cmp_packet_unit_bribe_inq_100);
  }
  BV_CLR_ALL(fields);

  old = hash_lookup_data(*hash, real_packet);
  old_from_hash = (old != NULL);
  if (!old) {
    old = fc_malloc(sizeof(*old));
    memset(old, 0, sizeof(*old));
    force_send_of_unchanged = TRUE;
  }

  differ = (old->unit_id != real_packet->unit_id);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 0);}

  if (different == 0 && !force_send_of_unchanged) {
    return 0;
  }

  DIO_BV_PUT(&dout, fields);

  if (BV_ISSET(fields, 0)) {
    dio_put_uint16(&dout, real_packet->unit_id);
  }


  if (old_from_hash) {
    hash_delete_entry(*hash, old);
  }

  clone = old;

  *clone = *real_packet;
  hash_insert(*hash, clone, clone);
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_unit_bribe_inq(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_UNIT_BRIBE_INQ] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_UNIT_BRIBE_INQ] = variant;
}

struct packet_unit_bribe_inq *receive_packet_unit_bribe_inq(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Receiving packet_unit_bribe_inq at the client.");
  }
  ensure_valid_variant_packet_unit_bribe_inq(pc);

  switch(pc->phs.variant[PACKET_UNIT_BRIBE_INQ]) {
    case 100: return receive_packet_unit_bribe_inq_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_unit_bribe_inq(struct connection *pc, const struct packet_unit_bribe_inq *packet)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Sending packet_unit_bribe_inq from the server.");
  }
  ensure_valid_variant_packet_unit_bribe_inq(pc);

  switch(pc->phs.variant[PACKET_UNIT_BRIBE_INQ]) {
    case 100: return send_packet_unit_bribe_inq_100(pc, packet);
    default: die("unknown variant"); return -1;
  }
}

int dsend_packet_unit_bribe_inq(struct connection *pc, int unit_id)
{
  struct packet_unit_bribe_inq packet, *real_packet = &packet;

  real_packet->unit_id = unit_id;
  
  return send_packet_unit_bribe_inq(pc, real_packet);
}

#define hash_packet_unit_bribe_info_100 hash_const

#define cmp_packet_unit_bribe_info_100 cmp_const

BV_DEFINE(packet_unit_bribe_info_100_fields, 2);

static struct packet_unit_bribe_info *receive_packet_unit_bribe_info_100(struct connection *pc, enum packet_type type)
{
  packet_unit_bribe_info_100_fields fields;
  struct packet_unit_bribe_info *old;
  struct hash_table **hash = &pc->phs.received[type];
  struct packet_unit_bribe_info *clone;
  RECEIVE_PACKET_START(packet_unit_bribe_info, real_packet);

  DIO_BV_GET(&din, fields);


  if (!*hash) {
    *hash = hash_new(hash_packet_unit_bribe_info_100, cmp_packet_unit_bribe_info_100);
  }
  old = hash_delete_entry(*hash, real_packet);

  if (old) {
    *real_packet = *old;
  } else {
    memset(real_packet, 0, sizeof(*real_packet));
  }

  if (BV_ISSET(fields, 0)) {
    {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->unit_id = readin;
    }
  }
  if (BV_ISSET(fields, 1)) {
    {
      int readin;
    
      dio_get_uint32(&din, &readin);
      real_packet->cost = readin;
    }
  }

  clone = fc_malloc(sizeof(*clone));
  *clone = *real_packet;
  if (old) {
    free(old);
  }
  hash_insert(*hash, clone, clone);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_unit_bribe_info_100(struct connection *pc, const struct packet_unit_bribe_info *packet)
{
  const struct packet_unit_bribe_info *real_packet = packet;
  packet_unit_bribe_info_100_fields fields;
  struct packet_unit_bribe_info *old, *clone;
  bool differ, old_from_hash, force_send_of_unchanged = TRUE;
  struct hash_table **hash = &pc->phs.sent[PACKET_UNIT_BRIBE_INFO];
  int different = 0;
  SEND_PACKET_START(PACKET_UNIT_BRIBE_INFO);

  if (!*hash) {
    *hash = hash_new(hash_packet_unit_bribe_info_100, cmp_packet_unit_bribe_info_100);
  }
  BV_CLR_ALL(fields);

  old = hash_lookup_data(*hash, real_packet);
  old_from_hash = (old != NULL);
  if (!old) {
    old = fc_malloc(sizeof(*old));
    memset(old, 0, sizeof(*old));
    force_send_of_unchanged = TRUE;
  }

  differ = (old->unit_id != real_packet->unit_id);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 0);}

  differ = (old->cost != real_packet->cost);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 1);}

  if (different == 0 && !force_send_of_unchanged) {
    return 0;
  }

  DIO_BV_PUT(&dout, fields);

  if (BV_ISSET(fields, 0)) {
    dio_put_uint16(&dout, real_packet->unit_id);
  }
  if (BV_ISSET(fields, 1)) {
    dio_put_uint32(&dout, real_packet->cost);
  }


  if (old_from_hash) {
    hash_delete_entry(*hash, old);
  }

  clone = old;

  *clone = *real_packet;
  hash_insert(*hash, clone, clone);
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_unit_bribe_info(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_UNIT_BRIBE_INFO] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_UNIT_BRIBE_INFO] = variant;
}

struct packet_unit_bribe_info *receive_packet_unit_bribe_info(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Receiving packet_unit_bribe_info at the server.");
  }
  ensure_valid_variant_packet_unit_bribe_info(pc);

  switch(pc->phs.variant[PACKET_UNIT_BRIBE_INFO]) {
    case 100: return receive_packet_unit_bribe_info_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_unit_bribe_info(struct connection *pc, const struct packet_unit_bribe_info *packet)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Sending packet_unit_bribe_info from the client.");
  }
  ensure_valid_variant_packet_unit_bribe_info(pc);

  switch(pc->phs.variant[PACKET_UNIT_BRIBE_INFO]) {
    case 100: return send_packet_unit_bribe_info_100(pc, packet);
    default: die("unknown variant"); return -1;
  }
}

int dsend_packet_unit_bribe_info(struct connection *pc, int unit_id, int cost)
{
  struct packet_unit_bribe_info packet, *real_packet = &packet;

  real_packet->unit_id = unit_id;
  real_packet->cost = cost;
  
  return send_packet_unit_bribe_info(pc, real_packet);
}

#define hash_packet_unit_type_upgrade_100 hash_const

#define cmp_packet_unit_type_upgrade_100 cmp_const

BV_DEFINE(packet_unit_type_upgrade_100_fields, 1);

static struct packet_unit_type_upgrade *receive_packet_unit_type_upgrade_100(struct connection *pc, enum packet_type type)
{
  packet_unit_type_upgrade_100_fields fields;
  struct packet_unit_type_upgrade *old;
  struct hash_table **hash = &pc->phs.received[type];
  struct packet_unit_type_upgrade *clone;
  RECEIVE_PACKET_START(packet_unit_type_upgrade, real_packet);

  DIO_BV_GET(&din, fields);


  if (!*hash) {
    *hash = hash_new(hash_packet_unit_type_upgrade_100, cmp_packet_unit_type_upgrade_100);
  }
  old = hash_delete_entry(*hash, real_packet);

  if (old) {
    *real_packet = *old;
  } else {
    memset(real_packet, 0, sizeof(*real_packet));
  }

  if (BV_ISSET(fields, 0)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->type = readin;
    }
  }

  clone = fc_malloc(sizeof(*clone));
  *clone = *real_packet;
  if (old) {
    free(old);
  }
  hash_insert(*hash, clone, clone);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_unit_type_upgrade_100(struct connection *pc, const struct packet_unit_type_upgrade *packet)
{
  const struct packet_unit_type_upgrade *real_packet = packet;
  packet_unit_type_upgrade_100_fields fields;
  struct packet_unit_type_upgrade *old, *clone;
  bool differ, old_from_hash, force_send_of_unchanged = TRUE;
  struct hash_table **hash = &pc->phs.sent[PACKET_UNIT_TYPE_UPGRADE];
  int different = 0;
  SEND_PACKET_START(PACKET_UNIT_TYPE_UPGRADE);

  if (!*hash) {
    *hash = hash_new(hash_packet_unit_type_upgrade_100, cmp_packet_unit_type_upgrade_100);
  }
  BV_CLR_ALL(fields);

  old = hash_lookup_data(*hash, real_packet);
  old_from_hash = (old != NULL);
  if (!old) {
    old = fc_malloc(sizeof(*old));
    memset(old, 0, sizeof(*old));
    force_send_of_unchanged = TRUE;
  }

  differ = (old->type != real_packet->type);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 0);}

  if (different == 0 && !force_send_of_unchanged) {
    return 0;
  }

  DIO_BV_PUT(&dout, fields);

  if (BV_ISSET(fields, 0)) {
    dio_put_uint8(&dout, real_packet->type);
  }


  if (old_from_hash) {
    hash_delete_entry(*hash, old);
  }

  clone = old;

  *clone = *real_packet;
  hash_insert(*hash, clone, clone);
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_unit_type_upgrade(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_UNIT_TYPE_UPGRADE] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_UNIT_TYPE_UPGRADE] = variant;
}

struct packet_unit_type_upgrade *receive_packet_unit_type_upgrade(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Receiving packet_unit_type_upgrade at the client.");
  }
  ensure_valid_variant_packet_unit_type_upgrade(pc);

  switch(pc->phs.variant[PACKET_UNIT_TYPE_UPGRADE]) {
    case 100: return receive_packet_unit_type_upgrade_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_unit_type_upgrade(struct connection *pc, const struct packet_unit_type_upgrade *packet)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Sending packet_unit_type_upgrade from the server.");
  }
  ensure_valid_variant_packet_unit_type_upgrade(pc);

  switch(pc->phs.variant[PACKET_UNIT_TYPE_UPGRADE]) {
    case 100: return send_packet_unit_type_upgrade_100(pc, packet);
    default: die("unknown variant"); return -1;
  }
}

int dsend_packet_unit_type_upgrade(struct connection *pc, Unit_Type_id type)
{
  struct packet_unit_type_upgrade packet, *real_packet = &packet;

  real_packet->type = type;
  
  return send_packet_unit_type_upgrade(pc, real_packet);
}

#define hash_packet_unit_diplomat_action_100 hash_const

#define cmp_packet_unit_diplomat_action_100 cmp_const

BV_DEFINE(packet_unit_diplomat_action_100_fields, 4);

static struct packet_unit_diplomat_action *receive_packet_unit_diplomat_action_100(struct connection *pc, enum packet_type type)
{
  packet_unit_diplomat_action_100_fields fields;
  struct packet_unit_diplomat_action *old;
  struct hash_table **hash = &pc->phs.received[type];
  struct packet_unit_diplomat_action *clone;
  RECEIVE_PACKET_START(packet_unit_diplomat_action, real_packet);

  DIO_BV_GET(&din, fields);


  if (!*hash) {
    *hash = hash_new(hash_packet_unit_diplomat_action_100, cmp_packet_unit_diplomat_action_100);
  }
  old = hash_delete_entry(*hash, real_packet);

  if (old) {
    *real_packet = *old;
  } else {
    memset(real_packet, 0, sizeof(*real_packet));
  }

  if (BV_ISSET(fields, 0)) {
    {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->diplomat_id = readin;
    }
  }
  if (BV_ISSET(fields, 1)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->action_type = readin;
    }
  }
  if (BV_ISSET(fields, 2)) {
    {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->target_id = readin;
    }
  }
  if (BV_ISSET(fields, 3)) {
    {
      int readin;
    
      dio_get_sint16(&din, &readin);
      real_packet->value = readin;
    }
  }

  clone = fc_malloc(sizeof(*clone));
  *clone = *real_packet;
  if (old) {
    free(old);
  }
  hash_insert(*hash, clone, clone);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_unit_diplomat_action_100(struct connection *pc, const struct packet_unit_diplomat_action *packet)
{
  const struct packet_unit_diplomat_action *real_packet = packet;
  packet_unit_diplomat_action_100_fields fields;
  struct packet_unit_diplomat_action *old, *clone;
  bool differ, old_from_hash, force_send_of_unchanged = TRUE;
  struct hash_table **hash = &pc->phs.sent[PACKET_UNIT_DIPLOMAT_ACTION];
  int different = 0;
  SEND_PACKET_START(PACKET_UNIT_DIPLOMAT_ACTION);

  if (!*hash) {
    *hash = hash_new(hash_packet_unit_diplomat_action_100, cmp_packet_unit_diplomat_action_100);
  }
  BV_CLR_ALL(fields);

  old = hash_lookup_data(*hash, real_packet);
  old_from_hash = (old != NULL);
  if (!old) {
    old = fc_malloc(sizeof(*old));
    memset(old, 0, sizeof(*old));
    force_send_of_unchanged = TRUE;
  }

  differ = (old->diplomat_id != real_packet->diplomat_id);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 0);}

  differ = (old->action_type != real_packet->action_type);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 1);}

  differ = (old->target_id != real_packet->target_id);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 2);}

  differ = (old->value != real_packet->value);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 3);}

  if (different == 0 && !force_send_of_unchanged) {
    return 0;
  }

  DIO_BV_PUT(&dout, fields);

  if (BV_ISSET(fields, 0)) {
    dio_put_uint16(&dout, real_packet->diplomat_id);
  }
  if (BV_ISSET(fields, 1)) {
    dio_put_uint8(&dout, real_packet->action_type);
  }
  if (BV_ISSET(fields, 2)) {
    dio_put_uint16(&dout, real_packet->target_id);
  }
  if (BV_ISSET(fields, 3)) {
    dio_put_sint16(&dout, real_packet->value);
  }


  if (old_from_hash) {
    hash_delete_entry(*hash, old);
  }

  clone = old;

  *clone = *real_packet;
  hash_insert(*hash, clone, clone);
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_unit_diplomat_action(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_UNIT_DIPLOMAT_ACTION] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_UNIT_DIPLOMAT_ACTION] = variant;
}

struct packet_unit_diplomat_action *receive_packet_unit_diplomat_action(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Receiving packet_unit_diplomat_action at the client.");
  }
  ensure_valid_variant_packet_unit_diplomat_action(pc);

  switch(pc->phs.variant[PACKET_UNIT_DIPLOMAT_ACTION]) {
    case 100: return receive_packet_unit_diplomat_action_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_unit_diplomat_action(struct connection *pc, const struct packet_unit_diplomat_action *packet)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Sending packet_unit_diplomat_action from the server.");
  }
  ensure_valid_variant_packet_unit_diplomat_action(pc);

  switch(pc->phs.variant[PACKET_UNIT_DIPLOMAT_ACTION]) {
    case 100: return send_packet_unit_diplomat_action_100(pc, packet);
    default: die("unknown variant"); return -1;
  }
}

int dsend_packet_unit_diplomat_action(struct connection *pc, int diplomat_id, enum diplomat_actions action_type, int target_id, int value)
{
  struct packet_unit_diplomat_action packet, *real_packet = &packet;

  real_packet->diplomat_id = diplomat_id;
  real_packet->action_type = action_type;
  real_packet->target_id = target_id;
  real_packet->value = value;
  
  return send_packet_unit_diplomat_action(pc, real_packet);
}

#define hash_packet_unit_diplomat_popup_dialog_100 hash_const

#define cmp_packet_unit_diplomat_popup_dialog_100 cmp_const

BV_DEFINE(packet_unit_diplomat_popup_dialog_100_fields, 2);

static struct packet_unit_diplomat_popup_dialog *receive_packet_unit_diplomat_popup_dialog_100(struct connection *pc, enum packet_type type)
{
  packet_unit_diplomat_popup_dialog_100_fields fields;
  struct packet_unit_diplomat_popup_dialog *old;
  struct hash_table **hash = &pc->phs.received[type];
  struct packet_unit_diplomat_popup_dialog *clone;
  RECEIVE_PACKET_START(packet_unit_diplomat_popup_dialog, real_packet);

  DIO_BV_GET(&din, fields);


  if (!*hash) {
    *hash = hash_new(hash_packet_unit_diplomat_popup_dialog_100, cmp_packet_unit_diplomat_popup_dialog_100);
  }
  old = hash_delete_entry(*hash, real_packet);

  if (old) {
    *real_packet = *old;
  } else {
    memset(real_packet, 0, sizeof(*real_packet));
  }

  if (BV_ISSET(fields, 0)) {
    {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->diplomat_id = readin;
    }
  }
  if (BV_ISSET(fields, 1)) {
    {
      int readin;
    
      dio_get_uint32(&din, &readin);
      real_packet->target_id = readin;
    }
  }

  clone = fc_malloc(sizeof(*clone));
  *clone = *real_packet;
  if (old) {
    free(old);
  }
  hash_insert(*hash, clone, clone);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_unit_diplomat_popup_dialog_100(struct connection *pc, const struct packet_unit_diplomat_popup_dialog *packet)
{
  const struct packet_unit_diplomat_popup_dialog *real_packet = packet;
  packet_unit_diplomat_popup_dialog_100_fields fields;
  struct packet_unit_diplomat_popup_dialog *old, *clone;
  bool differ, old_from_hash, force_send_of_unchanged = TRUE;
  struct hash_table **hash = &pc->phs.sent[PACKET_UNIT_DIPLOMAT_POPUP_DIALOG];
  int different = 0;
  SEND_PACKET_START(PACKET_UNIT_DIPLOMAT_POPUP_DIALOG);

  if (!*hash) {
    *hash = hash_new(hash_packet_unit_diplomat_popup_dialog_100, cmp_packet_unit_diplomat_popup_dialog_100);
  }
  BV_CLR_ALL(fields);

  old = hash_lookup_data(*hash, real_packet);
  old_from_hash = (old != NULL);
  if (!old) {
    old = fc_malloc(sizeof(*old));
    memset(old, 0, sizeof(*old));
    force_send_of_unchanged = TRUE;
  }

  differ = (old->diplomat_id != real_packet->diplomat_id);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 0);}

  differ = (old->target_id != real_packet->target_id);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 1);}

  if (different == 0 && !force_send_of_unchanged) {
    return 0;
  }

  DIO_BV_PUT(&dout, fields);

  if (BV_ISSET(fields, 0)) {
    dio_put_uint16(&dout, real_packet->diplomat_id);
  }
  if (BV_ISSET(fields, 1)) {
    dio_put_uint32(&dout, real_packet->target_id);
  }


  if (old_from_hash) {
    hash_delete_entry(*hash, old);
  }

  clone = old;

  *clone = *real_packet;
  hash_insert(*hash, clone, clone);
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_unit_diplomat_popup_dialog(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_UNIT_DIPLOMAT_POPUP_DIALOG] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_UNIT_DIPLOMAT_POPUP_DIALOG] = variant;
}

struct packet_unit_diplomat_popup_dialog *receive_packet_unit_diplomat_popup_dialog(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Receiving packet_unit_diplomat_popup_dialog at the server.");
  }
  ensure_valid_variant_packet_unit_diplomat_popup_dialog(pc);

  switch(pc->phs.variant[PACKET_UNIT_DIPLOMAT_POPUP_DIALOG]) {
    case 100: return receive_packet_unit_diplomat_popup_dialog_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_unit_diplomat_popup_dialog(struct connection *pc, const struct packet_unit_diplomat_popup_dialog *packet)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Sending packet_unit_diplomat_popup_dialog from the client.");
  }
  ensure_valid_variant_packet_unit_diplomat_popup_dialog(pc);

  switch(pc->phs.variant[PACKET_UNIT_DIPLOMAT_POPUP_DIALOG]) {
    case 100: return send_packet_unit_diplomat_popup_dialog_100(pc, packet);
    default: die("unknown variant"); return -1;
  }
}

void lsend_packet_unit_diplomat_popup_dialog(struct conn_list *dest, const struct packet_unit_diplomat_popup_dialog *packet)
{
  conn_list_iterate(*dest, pconn) {
    send_packet_unit_diplomat_popup_dialog(pconn, packet);
  } conn_list_iterate_end;
}

int dsend_packet_unit_diplomat_popup_dialog(struct connection *pc, int diplomat_id, int target_id)
{
  struct packet_unit_diplomat_popup_dialog packet, *real_packet = &packet;

  real_packet->diplomat_id = diplomat_id;
  real_packet->target_id = target_id;
  
  return send_packet_unit_diplomat_popup_dialog(pc, real_packet);
}

void dlsend_packet_unit_diplomat_popup_dialog(struct conn_list *dest, int diplomat_id, int target_id)
{
  struct packet_unit_diplomat_popup_dialog packet, *real_packet = &packet;

  real_packet->diplomat_id = diplomat_id;
  real_packet->target_id = target_id;
  
  lsend_packet_unit_diplomat_popup_dialog(dest, real_packet);
}

#define hash_packet_unit_change_activity_100 hash_const

#define cmp_packet_unit_change_activity_100 cmp_const

BV_DEFINE(packet_unit_change_activity_100_fields, 3);

static struct packet_unit_change_activity *receive_packet_unit_change_activity_100(struct connection *pc, enum packet_type type)
{
  packet_unit_change_activity_100_fields fields;
  struct packet_unit_change_activity *old;
  struct hash_table **hash = &pc->phs.received[type];
  struct packet_unit_change_activity *clone;
  RECEIVE_PACKET_START(packet_unit_change_activity, real_packet);

  DIO_BV_GET(&din, fields);


  if (!*hash) {
    *hash = hash_new(hash_packet_unit_change_activity_100, cmp_packet_unit_change_activity_100);
  }
  old = hash_delete_entry(*hash, real_packet);

  if (old) {
    *real_packet = *old;
  } else {
    memset(real_packet, 0, sizeof(*real_packet));
  }

  if (BV_ISSET(fields, 0)) {
    {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->unit_id = readin;
    }
  }
  if (BV_ISSET(fields, 1)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->activity = readin;
    }
  }
  if (BV_ISSET(fields, 2)) {
    {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->activity_target = readin;
    }
  }

  clone = fc_malloc(sizeof(*clone));
  *clone = *real_packet;
  if (old) {
    free(old);
  }
  hash_insert(*hash, clone, clone);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_unit_change_activity_100(struct connection *pc, const struct packet_unit_change_activity *packet)
{
  const struct packet_unit_change_activity *real_packet = packet;
  packet_unit_change_activity_100_fields fields;
  struct packet_unit_change_activity *old, *clone;
  bool differ, old_from_hash, force_send_of_unchanged = TRUE;
  struct hash_table **hash = &pc->phs.sent[PACKET_UNIT_CHANGE_ACTIVITY];
  int different = 0;
  SEND_PACKET_START(PACKET_UNIT_CHANGE_ACTIVITY);

  if (!*hash) {
    *hash = hash_new(hash_packet_unit_change_activity_100, cmp_packet_unit_change_activity_100);
  }
  BV_CLR_ALL(fields);

  old = hash_lookup_data(*hash, real_packet);
  old_from_hash = (old != NULL);
  if (!old) {
    old = fc_malloc(sizeof(*old));
    memset(old, 0, sizeof(*old));
    force_send_of_unchanged = TRUE;
  }

  differ = (old->unit_id != real_packet->unit_id);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 0);}

  differ = (old->activity != real_packet->activity);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 1);}

  differ = (old->activity_target != real_packet->activity_target);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 2);}

  if (different == 0 && !force_send_of_unchanged) {
    return 0;
  }

  DIO_BV_PUT(&dout, fields);

  if (BV_ISSET(fields, 0)) {
    dio_put_uint16(&dout, real_packet->unit_id);
  }
  if (BV_ISSET(fields, 1)) {
    dio_put_uint8(&dout, real_packet->activity);
  }
  if (BV_ISSET(fields, 2)) {
    dio_put_uint16(&dout, real_packet->activity_target);
  }


  if (old_from_hash) {
    hash_delete_entry(*hash, old);
  }

  clone = old;

  *clone = *real_packet;
  hash_insert(*hash, clone, clone);
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_unit_change_activity(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_UNIT_CHANGE_ACTIVITY] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_UNIT_CHANGE_ACTIVITY] = variant;
}

struct packet_unit_change_activity *receive_packet_unit_change_activity(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Receiving packet_unit_change_activity at the client.");
  }
  ensure_valid_variant_packet_unit_change_activity(pc);

  switch(pc->phs.variant[PACKET_UNIT_CHANGE_ACTIVITY]) {
    case 100: return receive_packet_unit_change_activity_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_unit_change_activity(struct connection *pc, const struct packet_unit_change_activity *packet)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Sending packet_unit_change_activity from the server.");
  }
  ensure_valid_variant_packet_unit_change_activity(pc);

  switch(pc->phs.variant[PACKET_UNIT_CHANGE_ACTIVITY]) {
    case 100: return send_packet_unit_change_activity_100(pc, packet);
    default: die("unknown variant"); return -1;
  }
}

int dsend_packet_unit_change_activity(struct connection *pc, int unit_id, enum unit_activity activity, enum tile_special_type activity_target)
{
  struct packet_unit_change_activity packet, *real_packet = &packet;

  real_packet->unit_id = unit_id;
  real_packet->activity = activity;
  real_packet->activity_target = activity_target;
  
  return send_packet_unit_change_activity(pc, real_packet);
}

#define hash_packet_diplomacy_init_meeting_req_100 hash_const

#define cmp_packet_diplomacy_init_meeting_req_100 cmp_const

BV_DEFINE(packet_diplomacy_init_meeting_req_100_fields, 1);

static struct packet_diplomacy_init_meeting_req *receive_packet_diplomacy_init_meeting_req_100(struct connection *pc, enum packet_type type)
{
  packet_diplomacy_init_meeting_req_100_fields fields;
  struct packet_diplomacy_init_meeting_req *old;
  struct hash_table **hash = &pc->phs.received[type];
  struct packet_diplomacy_init_meeting_req *clone;
  RECEIVE_PACKET_START(packet_diplomacy_init_meeting_req, real_packet);

  DIO_BV_GET(&din, fields);


  if (!*hash) {
    *hash = hash_new(hash_packet_diplomacy_init_meeting_req_100, cmp_packet_diplomacy_init_meeting_req_100);
  }
  old = hash_delete_entry(*hash, real_packet);

  if (old) {
    *real_packet = *old;
  } else {
    memset(real_packet, 0, sizeof(*real_packet));
  }

  if (BV_ISSET(fields, 0)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->counterpart = readin;
    }
  }

  clone = fc_malloc(sizeof(*clone));
  *clone = *real_packet;
  if (old) {
    free(old);
  }
  hash_insert(*hash, clone, clone);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_diplomacy_init_meeting_req_100(struct connection *pc, const struct packet_diplomacy_init_meeting_req *packet)
{
  const struct packet_diplomacy_init_meeting_req *real_packet = packet;
  packet_diplomacy_init_meeting_req_100_fields fields;
  struct packet_diplomacy_init_meeting_req *old, *clone;
  bool differ, old_from_hash, force_send_of_unchanged = TRUE;
  struct hash_table **hash = &pc->phs.sent[PACKET_DIPLOMACY_INIT_MEETING_REQ];
  int different = 0;
  SEND_PACKET_START(PACKET_DIPLOMACY_INIT_MEETING_REQ);

  if (!*hash) {
    *hash = hash_new(hash_packet_diplomacy_init_meeting_req_100, cmp_packet_diplomacy_init_meeting_req_100);
  }
  BV_CLR_ALL(fields);

  old = hash_lookup_data(*hash, real_packet);
  old_from_hash = (old != NULL);
  if (!old) {
    old = fc_malloc(sizeof(*old));
    memset(old, 0, sizeof(*old));
    force_send_of_unchanged = TRUE;
  }

  differ = (old->counterpart != real_packet->counterpart);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 0);}

  if (different == 0 && !force_send_of_unchanged) {
    return 0;
  }

  DIO_BV_PUT(&dout, fields);

  if (BV_ISSET(fields, 0)) {
    dio_put_uint8(&dout, real_packet->counterpart);
  }


  if (old_from_hash) {
    hash_delete_entry(*hash, old);
  }

  clone = old;

  *clone = *real_packet;
  hash_insert(*hash, clone, clone);
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_diplomacy_init_meeting_req(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_DIPLOMACY_INIT_MEETING_REQ] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_DIPLOMACY_INIT_MEETING_REQ] = variant;
}

struct packet_diplomacy_init_meeting_req *receive_packet_diplomacy_init_meeting_req(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Receiving packet_diplomacy_init_meeting_req at the client.");
  }
  ensure_valid_variant_packet_diplomacy_init_meeting_req(pc);

  switch(pc->phs.variant[PACKET_DIPLOMACY_INIT_MEETING_REQ]) {
    case 100: return receive_packet_diplomacy_init_meeting_req_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_diplomacy_init_meeting_req(struct connection *pc, const struct packet_diplomacy_init_meeting_req *packet)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Sending packet_diplomacy_init_meeting_req from the server.");
  }
  ensure_valid_variant_packet_diplomacy_init_meeting_req(pc);

  switch(pc->phs.variant[PACKET_DIPLOMACY_INIT_MEETING_REQ]) {
    case 100: return send_packet_diplomacy_init_meeting_req_100(pc, packet);
    default: die("unknown variant"); return -1;
  }
}

int dsend_packet_diplomacy_init_meeting_req(struct connection *pc, int counterpart)
{
  struct packet_diplomacy_init_meeting_req packet, *real_packet = &packet;

  real_packet->counterpart = counterpart;
  
  return send_packet_diplomacy_init_meeting_req(pc, real_packet);
}

#define hash_packet_diplomacy_init_meeting_100 hash_const

#define cmp_packet_diplomacy_init_meeting_100 cmp_const

BV_DEFINE(packet_diplomacy_init_meeting_100_fields, 2);

static struct packet_diplomacy_init_meeting *receive_packet_diplomacy_init_meeting_100(struct connection *pc, enum packet_type type)
{
  packet_diplomacy_init_meeting_100_fields fields;
  struct packet_diplomacy_init_meeting *old;
  struct hash_table **hash = &pc->phs.received[type];
  struct packet_diplomacy_init_meeting *clone;
  RECEIVE_PACKET_START(packet_diplomacy_init_meeting, real_packet);

  DIO_BV_GET(&din, fields);


  if (!*hash) {
    *hash = hash_new(hash_packet_diplomacy_init_meeting_100, cmp_packet_diplomacy_init_meeting_100);
  }
  old = hash_delete_entry(*hash, real_packet);

  if (old) {
    *real_packet = *old;
  } else {
    memset(real_packet, 0, sizeof(*real_packet));
  }

  if (BV_ISSET(fields, 0)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->counterpart = readin;
    }
  }
  if (BV_ISSET(fields, 1)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->initiated_from = readin;
    }
  }

  clone = fc_malloc(sizeof(*clone));
  *clone = *real_packet;
  if (old) {
    free(old);
  }
  hash_insert(*hash, clone, clone);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_diplomacy_init_meeting_100(struct connection *pc, const struct packet_diplomacy_init_meeting *packet)
{
  const struct packet_diplomacy_init_meeting *real_packet = packet;
  packet_diplomacy_init_meeting_100_fields fields;
  struct packet_diplomacy_init_meeting *old, *clone;
  bool differ, old_from_hash, force_send_of_unchanged = TRUE;
  struct hash_table **hash = &pc->phs.sent[PACKET_DIPLOMACY_INIT_MEETING];
  int different = 0;
  SEND_PACKET_START(PACKET_DIPLOMACY_INIT_MEETING);

  if (!*hash) {
    *hash = hash_new(hash_packet_diplomacy_init_meeting_100, cmp_packet_diplomacy_init_meeting_100);
  }
  BV_CLR_ALL(fields);

  old = hash_lookup_data(*hash, real_packet);
  old_from_hash = (old != NULL);
  if (!old) {
    old = fc_malloc(sizeof(*old));
    memset(old, 0, sizeof(*old));
    force_send_of_unchanged = TRUE;
  }

  differ = (old->counterpart != real_packet->counterpart);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 0);}

  differ = (old->initiated_from != real_packet->initiated_from);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 1);}

  if (different == 0 && !force_send_of_unchanged) {
    return 0;
  }

  DIO_BV_PUT(&dout, fields);

  if (BV_ISSET(fields, 0)) {
    dio_put_uint8(&dout, real_packet->counterpart);
  }
  if (BV_ISSET(fields, 1)) {
    dio_put_uint8(&dout, real_packet->initiated_from);
  }


  if (old_from_hash) {
    hash_delete_entry(*hash, old);
  }

  clone = old;

  *clone = *real_packet;
  hash_insert(*hash, clone, clone);
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_diplomacy_init_meeting(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_DIPLOMACY_INIT_MEETING] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_DIPLOMACY_INIT_MEETING] = variant;
}

struct packet_diplomacy_init_meeting *receive_packet_diplomacy_init_meeting(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Receiving packet_diplomacy_init_meeting at the server.");
  }
  ensure_valid_variant_packet_diplomacy_init_meeting(pc);

  switch(pc->phs.variant[PACKET_DIPLOMACY_INIT_MEETING]) {
    case 100: return receive_packet_diplomacy_init_meeting_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_diplomacy_init_meeting(struct connection *pc, const struct packet_diplomacy_init_meeting *packet)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Sending packet_diplomacy_init_meeting from the client.");
  }
  ensure_valid_variant_packet_diplomacy_init_meeting(pc);

  switch(pc->phs.variant[PACKET_DIPLOMACY_INIT_MEETING]) {
    case 100: return send_packet_diplomacy_init_meeting_100(pc, packet);
    default: die("unknown variant"); return -1;
  }
}

void lsend_packet_diplomacy_init_meeting(struct conn_list *dest, const struct packet_diplomacy_init_meeting *packet)
{
  conn_list_iterate(*dest, pconn) {
    send_packet_diplomacy_init_meeting(pconn, packet);
  } conn_list_iterate_end;
}

int dsend_packet_diplomacy_init_meeting(struct connection *pc, int counterpart, int initiated_from)
{
  struct packet_diplomacy_init_meeting packet, *real_packet = &packet;

  real_packet->counterpart = counterpart;
  real_packet->initiated_from = initiated_from;
  
  return send_packet_diplomacy_init_meeting(pc, real_packet);
}

void dlsend_packet_diplomacy_init_meeting(struct conn_list *dest, int counterpart, int initiated_from)
{
  struct packet_diplomacy_init_meeting packet, *real_packet = &packet;

  real_packet->counterpart = counterpart;
  real_packet->initiated_from = initiated_from;
  
  lsend_packet_diplomacy_init_meeting(dest, real_packet);
}

#define hash_packet_diplomacy_cancel_meeting_req_100 hash_const

#define cmp_packet_diplomacy_cancel_meeting_req_100 cmp_const

BV_DEFINE(packet_diplomacy_cancel_meeting_req_100_fields, 1);

static struct packet_diplomacy_cancel_meeting_req *receive_packet_diplomacy_cancel_meeting_req_100(struct connection *pc, enum packet_type type)
{
  packet_diplomacy_cancel_meeting_req_100_fields fields;
  struct packet_diplomacy_cancel_meeting_req *old;
  struct hash_table **hash = &pc->phs.received[type];
  struct packet_diplomacy_cancel_meeting_req *clone;
  RECEIVE_PACKET_START(packet_diplomacy_cancel_meeting_req, real_packet);

  DIO_BV_GET(&din, fields);


  if (!*hash) {
    *hash = hash_new(hash_packet_diplomacy_cancel_meeting_req_100, cmp_packet_diplomacy_cancel_meeting_req_100);
  }
  old = hash_delete_entry(*hash, real_packet);

  if (old) {
    *real_packet = *old;
  } else {
    memset(real_packet, 0, sizeof(*real_packet));
  }

  if (BV_ISSET(fields, 0)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->counterpart = readin;
    }
  }

  clone = fc_malloc(sizeof(*clone));
  *clone = *real_packet;
  if (old) {
    free(old);
  }
  hash_insert(*hash, clone, clone);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_diplomacy_cancel_meeting_req_100(struct connection *pc, const struct packet_diplomacy_cancel_meeting_req *packet)
{
  const struct packet_diplomacy_cancel_meeting_req *real_packet = packet;
  packet_diplomacy_cancel_meeting_req_100_fields fields;
  struct packet_diplomacy_cancel_meeting_req *old, *clone;
  bool differ, old_from_hash, force_send_of_unchanged = TRUE;
  struct hash_table **hash = &pc->phs.sent[PACKET_DIPLOMACY_CANCEL_MEETING_REQ];
  int different = 0;
  SEND_PACKET_START(PACKET_DIPLOMACY_CANCEL_MEETING_REQ);

  if (!*hash) {
    *hash = hash_new(hash_packet_diplomacy_cancel_meeting_req_100, cmp_packet_diplomacy_cancel_meeting_req_100);
  }
  BV_CLR_ALL(fields);

  old = hash_lookup_data(*hash, real_packet);
  old_from_hash = (old != NULL);
  if (!old) {
    old = fc_malloc(sizeof(*old));
    memset(old, 0, sizeof(*old));
    force_send_of_unchanged = TRUE;
  }

  differ = (old->counterpart != real_packet->counterpart);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 0);}

  if (different == 0 && !force_send_of_unchanged) {
    return 0;
  }

  DIO_BV_PUT(&dout, fields);

  if (BV_ISSET(fields, 0)) {
    dio_put_uint8(&dout, real_packet->counterpart);
  }


  if (old_from_hash) {
    hash_delete_entry(*hash, old);
  }

  clone = old;

  *clone = *real_packet;
  hash_insert(*hash, clone, clone);
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_diplomacy_cancel_meeting_req(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_DIPLOMACY_CANCEL_MEETING_REQ] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_DIPLOMACY_CANCEL_MEETING_REQ] = variant;
}

struct packet_diplomacy_cancel_meeting_req *receive_packet_diplomacy_cancel_meeting_req(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Receiving packet_diplomacy_cancel_meeting_req at the client.");
  }
  ensure_valid_variant_packet_diplomacy_cancel_meeting_req(pc);

  switch(pc->phs.variant[PACKET_DIPLOMACY_CANCEL_MEETING_REQ]) {
    case 100: return receive_packet_diplomacy_cancel_meeting_req_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_diplomacy_cancel_meeting_req(struct connection *pc, const struct packet_diplomacy_cancel_meeting_req *packet)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Sending packet_diplomacy_cancel_meeting_req from the server.");
  }
  ensure_valid_variant_packet_diplomacy_cancel_meeting_req(pc);

  switch(pc->phs.variant[PACKET_DIPLOMACY_CANCEL_MEETING_REQ]) {
    case 100: return send_packet_diplomacy_cancel_meeting_req_100(pc, packet);
    default: die("unknown variant"); return -1;
  }
}

int dsend_packet_diplomacy_cancel_meeting_req(struct connection *pc, int counterpart)
{
  struct packet_diplomacy_cancel_meeting_req packet, *real_packet = &packet;

  real_packet->counterpart = counterpart;
  
  return send_packet_diplomacy_cancel_meeting_req(pc, real_packet);
}

#define hash_packet_diplomacy_cancel_meeting_100 hash_const

#define cmp_packet_diplomacy_cancel_meeting_100 cmp_const

BV_DEFINE(packet_diplomacy_cancel_meeting_100_fields, 2);

static struct packet_diplomacy_cancel_meeting *receive_packet_diplomacy_cancel_meeting_100(struct connection *pc, enum packet_type type)
{
  packet_diplomacy_cancel_meeting_100_fields fields;
  struct packet_diplomacy_cancel_meeting *old;
  struct hash_table **hash = &pc->phs.received[type];
  struct packet_diplomacy_cancel_meeting *clone;
  RECEIVE_PACKET_START(packet_diplomacy_cancel_meeting, real_packet);

  DIO_BV_GET(&din, fields);


  if (!*hash) {
    *hash = hash_new(hash_packet_diplomacy_cancel_meeting_100, cmp_packet_diplomacy_cancel_meeting_100);
  }
  old = hash_delete_entry(*hash, real_packet);

  if (old) {
    *real_packet = *old;
  } else {
    memset(real_packet, 0, sizeof(*real_packet));
  }

  if (BV_ISSET(fields, 0)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->counterpart = readin;
    }
  }
  if (BV_ISSET(fields, 1)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->initiated_from = readin;
    }
  }

  clone = fc_malloc(sizeof(*clone));
  *clone = *real_packet;
  if (old) {
    free(old);
  }
  hash_insert(*hash, clone, clone);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_diplomacy_cancel_meeting_100(struct connection *pc, const struct packet_diplomacy_cancel_meeting *packet)
{
  const struct packet_diplomacy_cancel_meeting *real_packet = packet;
  packet_diplomacy_cancel_meeting_100_fields fields;
  struct packet_diplomacy_cancel_meeting *old, *clone;
  bool differ, old_from_hash, force_send_of_unchanged = TRUE;
  struct hash_table **hash = &pc->phs.sent[PACKET_DIPLOMACY_CANCEL_MEETING];
  int different = 0;
  SEND_PACKET_START(PACKET_DIPLOMACY_CANCEL_MEETING);

  if (!*hash) {
    *hash = hash_new(hash_packet_diplomacy_cancel_meeting_100, cmp_packet_diplomacy_cancel_meeting_100);
  }
  BV_CLR_ALL(fields);

  old = hash_lookup_data(*hash, real_packet);
  old_from_hash = (old != NULL);
  if (!old) {
    old = fc_malloc(sizeof(*old));
    memset(old, 0, sizeof(*old));
    force_send_of_unchanged = TRUE;
  }

  differ = (old->counterpart != real_packet->counterpart);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 0);}

  differ = (old->initiated_from != real_packet->initiated_from);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 1);}

  if (different == 0 && !force_send_of_unchanged) {
    return 0;
  }

  DIO_BV_PUT(&dout, fields);

  if (BV_ISSET(fields, 0)) {
    dio_put_uint8(&dout, real_packet->counterpart);
  }
  if (BV_ISSET(fields, 1)) {
    dio_put_uint8(&dout, real_packet->initiated_from);
  }


  if (old_from_hash) {
    hash_delete_entry(*hash, old);
  }

  clone = old;

  *clone = *real_packet;
  hash_insert(*hash, clone, clone);
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_diplomacy_cancel_meeting(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_DIPLOMACY_CANCEL_MEETING] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_DIPLOMACY_CANCEL_MEETING] = variant;
}

struct packet_diplomacy_cancel_meeting *receive_packet_diplomacy_cancel_meeting(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Receiving packet_diplomacy_cancel_meeting at the server.");
  }
  ensure_valid_variant_packet_diplomacy_cancel_meeting(pc);

  switch(pc->phs.variant[PACKET_DIPLOMACY_CANCEL_MEETING]) {
    case 100: return receive_packet_diplomacy_cancel_meeting_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_diplomacy_cancel_meeting(struct connection *pc, const struct packet_diplomacy_cancel_meeting *packet)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Sending packet_diplomacy_cancel_meeting from the client.");
  }
  ensure_valid_variant_packet_diplomacy_cancel_meeting(pc);

  switch(pc->phs.variant[PACKET_DIPLOMACY_CANCEL_MEETING]) {
    case 100: return send_packet_diplomacy_cancel_meeting_100(pc, packet);
    default: die("unknown variant"); return -1;
  }
}

void lsend_packet_diplomacy_cancel_meeting(struct conn_list *dest, const struct packet_diplomacy_cancel_meeting *packet)
{
  conn_list_iterate(*dest, pconn) {
    send_packet_diplomacy_cancel_meeting(pconn, packet);
  } conn_list_iterate_end;
}

int dsend_packet_diplomacy_cancel_meeting(struct connection *pc, int counterpart, int initiated_from)
{
  struct packet_diplomacy_cancel_meeting packet, *real_packet = &packet;

  real_packet->counterpart = counterpart;
  real_packet->initiated_from = initiated_from;
  
  return send_packet_diplomacy_cancel_meeting(pc, real_packet);
}

void dlsend_packet_diplomacy_cancel_meeting(struct conn_list *dest, int counterpart, int initiated_from)
{
  struct packet_diplomacy_cancel_meeting packet, *real_packet = &packet;

  real_packet->counterpart = counterpart;
  real_packet->initiated_from = initiated_from;
  
  lsend_packet_diplomacy_cancel_meeting(dest, real_packet);
}

#define hash_packet_diplomacy_create_clause_req_100 hash_const

#define cmp_packet_diplomacy_create_clause_req_100 cmp_const

BV_DEFINE(packet_diplomacy_create_clause_req_100_fields, 4);

static struct packet_diplomacy_create_clause_req *receive_packet_diplomacy_create_clause_req_100(struct connection *pc, enum packet_type type)
{
  packet_diplomacy_create_clause_req_100_fields fields;
  struct packet_diplomacy_create_clause_req *old;
  struct hash_table **hash = &pc->phs.received[type];
  struct packet_diplomacy_create_clause_req *clone;
  RECEIVE_PACKET_START(packet_diplomacy_create_clause_req, real_packet);

  DIO_BV_GET(&din, fields);


  if (!*hash) {
    *hash = hash_new(hash_packet_diplomacy_create_clause_req_100, cmp_packet_diplomacy_create_clause_req_100);
  }
  old = hash_delete_entry(*hash, real_packet);

  if (old) {
    *real_packet = *old;
  } else {
    memset(real_packet, 0, sizeof(*real_packet));
  }

  if (BV_ISSET(fields, 0)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->counterpart = readin;
    }
  }
  if (BV_ISSET(fields, 1)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->giver = readin;
    }
  }
  if (BV_ISSET(fields, 2)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->type = readin;
    }
  }
  if (BV_ISSET(fields, 3)) {
    {
      int readin;
    
      dio_get_uint32(&din, &readin);
      real_packet->value = readin;
    }
  }

  clone = fc_malloc(sizeof(*clone));
  *clone = *real_packet;
  if (old) {
    free(old);
  }
  hash_insert(*hash, clone, clone);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_diplomacy_create_clause_req_100(struct connection *pc, const struct packet_diplomacy_create_clause_req *packet)
{
  const struct packet_diplomacy_create_clause_req *real_packet = packet;
  packet_diplomacy_create_clause_req_100_fields fields;
  struct packet_diplomacy_create_clause_req *old, *clone;
  bool differ, old_from_hash, force_send_of_unchanged = TRUE;
  struct hash_table **hash = &pc->phs.sent[PACKET_DIPLOMACY_CREATE_CLAUSE_REQ];
  int different = 0;
  SEND_PACKET_START(PACKET_DIPLOMACY_CREATE_CLAUSE_REQ);

  if (!*hash) {
    *hash = hash_new(hash_packet_diplomacy_create_clause_req_100, cmp_packet_diplomacy_create_clause_req_100);
  }
  BV_CLR_ALL(fields);

  old = hash_lookup_data(*hash, real_packet);
  old_from_hash = (old != NULL);
  if (!old) {
    old = fc_malloc(sizeof(*old));
    memset(old, 0, sizeof(*old));
    force_send_of_unchanged = TRUE;
  }

  differ = (old->counterpart != real_packet->counterpart);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 0);}

  differ = (old->giver != real_packet->giver);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 1);}

  differ = (old->type != real_packet->type);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 2);}

  differ = (old->value != real_packet->value);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 3);}

  if (different == 0 && !force_send_of_unchanged) {
    return 0;
  }

  DIO_BV_PUT(&dout, fields);

  if (BV_ISSET(fields, 0)) {
    dio_put_uint8(&dout, real_packet->counterpart);
  }
  if (BV_ISSET(fields, 1)) {
    dio_put_uint8(&dout, real_packet->giver);
  }
  if (BV_ISSET(fields, 2)) {
    dio_put_uint8(&dout, real_packet->type);
  }
  if (BV_ISSET(fields, 3)) {
    dio_put_uint32(&dout, real_packet->value);
  }


  if (old_from_hash) {
    hash_delete_entry(*hash, old);
  }

  clone = old;

  *clone = *real_packet;
  hash_insert(*hash, clone, clone);
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_diplomacy_create_clause_req(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_DIPLOMACY_CREATE_CLAUSE_REQ] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_DIPLOMACY_CREATE_CLAUSE_REQ] = variant;
}

struct packet_diplomacy_create_clause_req *receive_packet_diplomacy_create_clause_req(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Receiving packet_diplomacy_create_clause_req at the client.");
  }
  ensure_valid_variant_packet_diplomacy_create_clause_req(pc);

  switch(pc->phs.variant[PACKET_DIPLOMACY_CREATE_CLAUSE_REQ]) {
    case 100: return receive_packet_diplomacy_create_clause_req_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_diplomacy_create_clause_req(struct connection *pc, const struct packet_diplomacy_create_clause_req *packet)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Sending packet_diplomacy_create_clause_req from the server.");
  }
  ensure_valid_variant_packet_diplomacy_create_clause_req(pc);

  switch(pc->phs.variant[PACKET_DIPLOMACY_CREATE_CLAUSE_REQ]) {
    case 100: return send_packet_diplomacy_create_clause_req_100(pc, packet);
    default: die("unknown variant"); return -1;
  }
}

int dsend_packet_diplomacy_create_clause_req(struct connection *pc, int counterpart, int giver, enum clause_type type, int value)
{
  struct packet_diplomacy_create_clause_req packet, *real_packet = &packet;

  real_packet->counterpart = counterpart;
  real_packet->giver = giver;
  real_packet->type = type;
  real_packet->value = value;
  
  return send_packet_diplomacy_create_clause_req(pc, real_packet);
}

#define hash_packet_diplomacy_create_clause_100 hash_const

#define cmp_packet_diplomacy_create_clause_100 cmp_const

BV_DEFINE(packet_diplomacy_create_clause_100_fields, 4);

static struct packet_diplomacy_create_clause *receive_packet_diplomacy_create_clause_100(struct connection *pc, enum packet_type type)
{
  packet_diplomacy_create_clause_100_fields fields;
  struct packet_diplomacy_create_clause *old;
  struct hash_table **hash = &pc->phs.received[type];
  struct packet_diplomacy_create_clause *clone;
  RECEIVE_PACKET_START(packet_diplomacy_create_clause, real_packet);

  DIO_BV_GET(&din, fields);


  if (!*hash) {
    *hash = hash_new(hash_packet_diplomacy_create_clause_100, cmp_packet_diplomacy_create_clause_100);
  }
  old = hash_delete_entry(*hash, real_packet);

  if (old) {
    *real_packet = *old;
  } else {
    memset(real_packet, 0, sizeof(*real_packet));
  }

  if (BV_ISSET(fields, 0)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->counterpart = readin;
    }
  }
  if (BV_ISSET(fields, 1)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->giver = readin;
    }
  }
  if (BV_ISSET(fields, 2)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->type = readin;
    }
  }
  if (BV_ISSET(fields, 3)) {
    {
      int readin;
    
      dio_get_uint32(&din, &readin);
      real_packet->value = readin;
    }
  }

  clone = fc_malloc(sizeof(*clone));
  *clone = *real_packet;
  if (old) {
    free(old);
  }
  hash_insert(*hash, clone, clone);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_diplomacy_create_clause_100(struct connection *pc, const struct packet_diplomacy_create_clause *packet)
{
  const struct packet_diplomacy_create_clause *real_packet = packet;
  packet_diplomacy_create_clause_100_fields fields;
  struct packet_diplomacy_create_clause *old, *clone;
  bool differ, old_from_hash, force_send_of_unchanged = TRUE;
  struct hash_table **hash = &pc->phs.sent[PACKET_DIPLOMACY_CREATE_CLAUSE];
  int different = 0;
  SEND_PACKET_START(PACKET_DIPLOMACY_CREATE_CLAUSE);

  if (!*hash) {
    *hash = hash_new(hash_packet_diplomacy_create_clause_100, cmp_packet_diplomacy_create_clause_100);
  }
  BV_CLR_ALL(fields);

  old = hash_lookup_data(*hash, real_packet);
  old_from_hash = (old != NULL);
  if (!old) {
    old = fc_malloc(sizeof(*old));
    memset(old, 0, sizeof(*old));
    force_send_of_unchanged = TRUE;
  }

  differ = (old->counterpart != real_packet->counterpart);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 0);}

  differ = (old->giver != real_packet->giver);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 1);}

  differ = (old->type != real_packet->type);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 2);}

  differ = (old->value != real_packet->value);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 3);}

  if (different == 0 && !force_send_of_unchanged) {
    return 0;
  }

  DIO_BV_PUT(&dout, fields);

  if (BV_ISSET(fields, 0)) {
    dio_put_uint8(&dout, real_packet->counterpart);
  }
  if (BV_ISSET(fields, 1)) {
    dio_put_uint8(&dout, real_packet->giver);
  }
  if (BV_ISSET(fields, 2)) {
    dio_put_uint8(&dout, real_packet->type);
  }
  if (BV_ISSET(fields, 3)) {
    dio_put_uint32(&dout, real_packet->value);
  }


  if (old_from_hash) {
    hash_delete_entry(*hash, old);
  }

  clone = old;

  *clone = *real_packet;
  hash_insert(*hash, clone, clone);
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_diplomacy_create_clause(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_DIPLOMACY_CREATE_CLAUSE] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_DIPLOMACY_CREATE_CLAUSE] = variant;
}

struct packet_diplomacy_create_clause *receive_packet_diplomacy_create_clause(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Receiving packet_diplomacy_create_clause at the server.");
  }
  ensure_valid_variant_packet_diplomacy_create_clause(pc);

  switch(pc->phs.variant[PACKET_DIPLOMACY_CREATE_CLAUSE]) {
    case 100: return receive_packet_diplomacy_create_clause_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_diplomacy_create_clause(struct connection *pc, const struct packet_diplomacy_create_clause *packet)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Sending packet_diplomacy_create_clause from the client.");
  }
  ensure_valid_variant_packet_diplomacy_create_clause(pc);

  switch(pc->phs.variant[PACKET_DIPLOMACY_CREATE_CLAUSE]) {
    case 100: return send_packet_diplomacy_create_clause_100(pc, packet);
    default: die("unknown variant"); return -1;
  }
}

void lsend_packet_diplomacy_create_clause(struct conn_list *dest, const struct packet_diplomacy_create_clause *packet)
{
  conn_list_iterate(*dest, pconn) {
    send_packet_diplomacy_create_clause(pconn, packet);
  } conn_list_iterate_end;
}

int dsend_packet_diplomacy_create_clause(struct connection *pc, int counterpart, int giver, enum clause_type type, int value)
{
  struct packet_diplomacy_create_clause packet, *real_packet = &packet;

  real_packet->counterpart = counterpart;
  real_packet->giver = giver;
  real_packet->type = type;
  real_packet->value = value;
  
  return send_packet_diplomacy_create_clause(pc, real_packet);
}

void dlsend_packet_diplomacy_create_clause(struct conn_list *dest, int counterpart, int giver, enum clause_type type, int value)
{
  struct packet_diplomacy_create_clause packet, *real_packet = &packet;

  real_packet->counterpart = counterpart;
  real_packet->giver = giver;
  real_packet->type = type;
  real_packet->value = value;
  
  lsend_packet_diplomacy_create_clause(dest, real_packet);
}

#define hash_packet_diplomacy_remove_clause_req_100 hash_const

#define cmp_packet_diplomacy_remove_clause_req_100 cmp_const

BV_DEFINE(packet_diplomacy_remove_clause_req_100_fields, 4);

static struct packet_diplomacy_remove_clause_req *receive_packet_diplomacy_remove_clause_req_100(struct connection *pc, enum packet_type type)
{
  packet_diplomacy_remove_clause_req_100_fields fields;
  struct packet_diplomacy_remove_clause_req *old;
  struct hash_table **hash = &pc->phs.received[type];
  struct packet_diplomacy_remove_clause_req *clone;
  RECEIVE_PACKET_START(packet_diplomacy_remove_clause_req, real_packet);

  DIO_BV_GET(&din, fields);


  if (!*hash) {
    *hash = hash_new(hash_packet_diplomacy_remove_clause_req_100, cmp_packet_diplomacy_remove_clause_req_100);
  }
  old = hash_delete_entry(*hash, real_packet);

  if (old) {
    *real_packet = *old;
  } else {
    memset(real_packet, 0, sizeof(*real_packet));
  }

  if (BV_ISSET(fields, 0)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->counterpart = readin;
    }
  }
  if (BV_ISSET(fields, 1)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->giver = readin;
    }
  }
  if (BV_ISSET(fields, 2)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->type = readin;
    }
  }
  if (BV_ISSET(fields, 3)) {
    {
      int readin;
    
      dio_get_uint32(&din, &readin);
      real_packet->value = readin;
    }
  }

  clone = fc_malloc(sizeof(*clone));
  *clone = *real_packet;
  if (old) {
    free(old);
  }
  hash_insert(*hash, clone, clone);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_diplomacy_remove_clause_req_100(struct connection *pc, const struct packet_diplomacy_remove_clause_req *packet)
{
  const struct packet_diplomacy_remove_clause_req *real_packet = packet;
  packet_diplomacy_remove_clause_req_100_fields fields;
  struct packet_diplomacy_remove_clause_req *old, *clone;
  bool differ, old_from_hash, force_send_of_unchanged = TRUE;
  struct hash_table **hash = &pc->phs.sent[PACKET_DIPLOMACY_REMOVE_CLAUSE_REQ];
  int different = 0;
  SEND_PACKET_START(PACKET_DIPLOMACY_REMOVE_CLAUSE_REQ);

  if (!*hash) {
    *hash = hash_new(hash_packet_diplomacy_remove_clause_req_100, cmp_packet_diplomacy_remove_clause_req_100);
  }
  BV_CLR_ALL(fields);

  old = hash_lookup_data(*hash, real_packet);
  old_from_hash = (old != NULL);
  if (!old) {
    old = fc_malloc(sizeof(*old));
    memset(old, 0, sizeof(*old));
    force_send_of_unchanged = TRUE;
  }

  differ = (old->counterpart != real_packet->counterpart);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 0);}

  differ = (old->giver != real_packet->giver);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 1);}

  differ = (old->type != real_packet->type);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 2);}

  differ = (old->value != real_packet->value);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 3);}

  if (different == 0 && !force_send_of_unchanged) {
    return 0;
  }

  DIO_BV_PUT(&dout, fields);

  if (BV_ISSET(fields, 0)) {
    dio_put_uint8(&dout, real_packet->counterpart);
  }
  if (BV_ISSET(fields, 1)) {
    dio_put_uint8(&dout, real_packet->giver);
  }
  if (BV_ISSET(fields, 2)) {
    dio_put_uint8(&dout, real_packet->type);
  }
  if (BV_ISSET(fields, 3)) {
    dio_put_uint32(&dout, real_packet->value);
  }


  if (old_from_hash) {
    hash_delete_entry(*hash, old);
  }

  clone = old;

  *clone = *real_packet;
  hash_insert(*hash, clone, clone);
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_diplomacy_remove_clause_req(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_DIPLOMACY_REMOVE_CLAUSE_REQ] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_DIPLOMACY_REMOVE_CLAUSE_REQ] = variant;
}

struct packet_diplomacy_remove_clause_req *receive_packet_diplomacy_remove_clause_req(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Receiving packet_diplomacy_remove_clause_req at the client.");
  }
  ensure_valid_variant_packet_diplomacy_remove_clause_req(pc);

  switch(pc->phs.variant[PACKET_DIPLOMACY_REMOVE_CLAUSE_REQ]) {
    case 100: return receive_packet_diplomacy_remove_clause_req_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_diplomacy_remove_clause_req(struct connection *pc, const struct packet_diplomacy_remove_clause_req *packet)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Sending packet_diplomacy_remove_clause_req from the server.");
  }
  ensure_valid_variant_packet_diplomacy_remove_clause_req(pc);

  switch(pc->phs.variant[PACKET_DIPLOMACY_REMOVE_CLAUSE_REQ]) {
    case 100: return send_packet_diplomacy_remove_clause_req_100(pc, packet);
    default: die("unknown variant"); return -1;
  }
}

int dsend_packet_diplomacy_remove_clause_req(struct connection *pc, int counterpart, int giver, enum clause_type type, int value)
{
  struct packet_diplomacy_remove_clause_req packet, *real_packet = &packet;

  real_packet->counterpart = counterpart;
  real_packet->giver = giver;
  real_packet->type = type;
  real_packet->value = value;
  
  return send_packet_diplomacy_remove_clause_req(pc, real_packet);
}

#define hash_packet_diplomacy_remove_clause_100 hash_const

#define cmp_packet_diplomacy_remove_clause_100 cmp_const

BV_DEFINE(packet_diplomacy_remove_clause_100_fields, 4);

static struct packet_diplomacy_remove_clause *receive_packet_diplomacy_remove_clause_100(struct connection *pc, enum packet_type type)
{
  packet_diplomacy_remove_clause_100_fields fields;
  struct packet_diplomacy_remove_clause *old;
  struct hash_table **hash = &pc->phs.received[type];
  struct packet_diplomacy_remove_clause *clone;
  RECEIVE_PACKET_START(packet_diplomacy_remove_clause, real_packet);

  DIO_BV_GET(&din, fields);


  if (!*hash) {
    *hash = hash_new(hash_packet_diplomacy_remove_clause_100, cmp_packet_diplomacy_remove_clause_100);
  }
  old = hash_delete_entry(*hash, real_packet);

  if (old) {
    *real_packet = *old;
  } else {
    memset(real_packet, 0, sizeof(*real_packet));
  }

  if (BV_ISSET(fields, 0)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->counterpart = readin;
    }
  }
  if (BV_ISSET(fields, 1)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->giver = readin;
    }
  }
  if (BV_ISSET(fields, 2)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->type = readin;
    }
  }
  if (BV_ISSET(fields, 3)) {
    {
      int readin;
    
      dio_get_uint32(&din, &readin);
      real_packet->value = readin;
    }
  }

  clone = fc_malloc(sizeof(*clone));
  *clone = *real_packet;
  if (old) {
    free(old);
  }
  hash_insert(*hash, clone, clone);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_diplomacy_remove_clause_100(struct connection *pc, const struct packet_diplomacy_remove_clause *packet)
{
  const struct packet_diplomacy_remove_clause *real_packet = packet;
  packet_diplomacy_remove_clause_100_fields fields;
  struct packet_diplomacy_remove_clause *old, *clone;
  bool differ, old_from_hash, force_send_of_unchanged = TRUE;
  struct hash_table **hash = &pc->phs.sent[PACKET_DIPLOMACY_REMOVE_CLAUSE];
  int different = 0;
  SEND_PACKET_START(PACKET_DIPLOMACY_REMOVE_CLAUSE);

  if (!*hash) {
    *hash = hash_new(hash_packet_diplomacy_remove_clause_100, cmp_packet_diplomacy_remove_clause_100);
  }
  BV_CLR_ALL(fields);

  old = hash_lookup_data(*hash, real_packet);
  old_from_hash = (old != NULL);
  if (!old) {
    old = fc_malloc(sizeof(*old));
    memset(old, 0, sizeof(*old));
    force_send_of_unchanged = TRUE;
  }

  differ = (old->counterpart != real_packet->counterpart);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 0);}

  differ = (old->giver != real_packet->giver);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 1);}

  differ = (old->type != real_packet->type);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 2);}

  differ = (old->value != real_packet->value);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 3);}

  if (different == 0 && !force_send_of_unchanged) {
    return 0;
  }

  DIO_BV_PUT(&dout, fields);

  if (BV_ISSET(fields, 0)) {
    dio_put_uint8(&dout, real_packet->counterpart);
  }
  if (BV_ISSET(fields, 1)) {
    dio_put_uint8(&dout, real_packet->giver);
  }
  if (BV_ISSET(fields, 2)) {
    dio_put_uint8(&dout, real_packet->type);
  }
  if (BV_ISSET(fields, 3)) {
    dio_put_uint32(&dout, real_packet->value);
  }


  if (old_from_hash) {
    hash_delete_entry(*hash, old);
  }

  clone = old;

  *clone = *real_packet;
  hash_insert(*hash, clone, clone);
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_diplomacy_remove_clause(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_DIPLOMACY_REMOVE_CLAUSE] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_DIPLOMACY_REMOVE_CLAUSE] = variant;
}

struct packet_diplomacy_remove_clause *receive_packet_diplomacy_remove_clause(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Receiving packet_diplomacy_remove_clause at the server.");
  }
  ensure_valid_variant_packet_diplomacy_remove_clause(pc);

  switch(pc->phs.variant[PACKET_DIPLOMACY_REMOVE_CLAUSE]) {
    case 100: return receive_packet_diplomacy_remove_clause_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_diplomacy_remove_clause(struct connection *pc, const struct packet_diplomacy_remove_clause *packet)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Sending packet_diplomacy_remove_clause from the client.");
  }
  ensure_valid_variant_packet_diplomacy_remove_clause(pc);

  switch(pc->phs.variant[PACKET_DIPLOMACY_REMOVE_CLAUSE]) {
    case 100: return send_packet_diplomacy_remove_clause_100(pc, packet);
    default: die("unknown variant"); return -1;
  }
}

void lsend_packet_diplomacy_remove_clause(struct conn_list *dest, const struct packet_diplomacy_remove_clause *packet)
{
  conn_list_iterate(*dest, pconn) {
    send_packet_diplomacy_remove_clause(pconn, packet);
  } conn_list_iterate_end;
}

int dsend_packet_diplomacy_remove_clause(struct connection *pc, int counterpart, int giver, enum clause_type type, int value)
{
  struct packet_diplomacy_remove_clause packet, *real_packet = &packet;

  real_packet->counterpart = counterpart;
  real_packet->giver = giver;
  real_packet->type = type;
  real_packet->value = value;
  
  return send_packet_diplomacy_remove_clause(pc, real_packet);
}

void dlsend_packet_diplomacy_remove_clause(struct conn_list *dest, int counterpart, int giver, enum clause_type type, int value)
{
  struct packet_diplomacy_remove_clause packet, *real_packet = &packet;

  real_packet->counterpart = counterpart;
  real_packet->giver = giver;
  real_packet->type = type;
  real_packet->value = value;
  
  lsend_packet_diplomacy_remove_clause(dest, real_packet);
}

#define hash_packet_diplomacy_accept_treaty_req_100 hash_const

#define cmp_packet_diplomacy_accept_treaty_req_100 cmp_const

BV_DEFINE(packet_diplomacy_accept_treaty_req_100_fields, 1);

static struct packet_diplomacy_accept_treaty_req *receive_packet_diplomacy_accept_treaty_req_100(struct connection *pc, enum packet_type type)
{
  packet_diplomacy_accept_treaty_req_100_fields fields;
  struct packet_diplomacy_accept_treaty_req *old;
  struct hash_table **hash = &pc->phs.received[type];
  struct packet_diplomacy_accept_treaty_req *clone;
  RECEIVE_PACKET_START(packet_diplomacy_accept_treaty_req, real_packet);

  DIO_BV_GET(&din, fields);


  if (!*hash) {
    *hash = hash_new(hash_packet_diplomacy_accept_treaty_req_100, cmp_packet_diplomacy_accept_treaty_req_100);
  }
  old = hash_delete_entry(*hash, real_packet);

  if (old) {
    *real_packet = *old;
  } else {
    memset(real_packet, 0, sizeof(*real_packet));
  }

  if (BV_ISSET(fields, 0)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->counterpart = readin;
    }
  }

  clone = fc_malloc(sizeof(*clone));
  *clone = *real_packet;
  if (old) {
    free(old);
  }
  hash_insert(*hash, clone, clone);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_diplomacy_accept_treaty_req_100(struct connection *pc, const struct packet_diplomacy_accept_treaty_req *packet)
{
  const struct packet_diplomacy_accept_treaty_req *real_packet = packet;
  packet_diplomacy_accept_treaty_req_100_fields fields;
  struct packet_diplomacy_accept_treaty_req *old, *clone;
  bool differ, old_from_hash, force_send_of_unchanged = TRUE;
  struct hash_table **hash = &pc->phs.sent[PACKET_DIPLOMACY_ACCEPT_TREATY_REQ];
  int different = 0;
  SEND_PACKET_START(PACKET_DIPLOMACY_ACCEPT_TREATY_REQ);

  if (!*hash) {
    *hash = hash_new(hash_packet_diplomacy_accept_treaty_req_100, cmp_packet_diplomacy_accept_treaty_req_100);
  }
  BV_CLR_ALL(fields);

  old = hash_lookup_data(*hash, real_packet);
  old_from_hash = (old != NULL);
  if (!old) {
    old = fc_malloc(sizeof(*old));
    memset(old, 0, sizeof(*old));
    force_send_of_unchanged = TRUE;
  }

  differ = (old->counterpart != real_packet->counterpart);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 0);}

  if (different == 0 && !force_send_of_unchanged) {
    return 0;
  }

  DIO_BV_PUT(&dout, fields);

  if (BV_ISSET(fields, 0)) {
    dio_put_uint8(&dout, real_packet->counterpart);
  }


  if (old_from_hash) {
    hash_delete_entry(*hash, old);
  }

  clone = old;

  *clone = *real_packet;
  hash_insert(*hash, clone, clone);
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_diplomacy_accept_treaty_req(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_DIPLOMACY_ACCEPT_TREATY_REQ] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_DIPLOMACY_ACCEPT_TREATY_REQ] = variant;
}

struct packet_diplomacy_accept_treaty_req *receive_packet_diplomacy_accept_treaty_req(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Receiving packet_diplomacy_accept_treaty_req at the client.");
  }
  ensure_valid_variant_packet_diplomacy_accept_treaty_req(pc);

  switch(pc->phs.variant[PACKET_DIPLOMACY_ACCEPT_TREATY_REQ]) {
    case 100: return receive_packet_diplomacy_accept_treaty_req_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_diplomacy_accept_treaty_req(struct connection *pc, const struct packet_diplomacy_accept_treaty_req *packet)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Sending packet_diplomacy_accept_treaty_req from the server.");
  }
  ensure_valid_variant_packet_diplomacy_accept_treaty_req(pc);

  switch(pc->phs.variant[PACKET_DIPLOMACY_ACCEPT_TREATY_REQ]) {
    case 100: return send_packet_diplomacy_accept_treaty_req_100(pc, packet);
    default: die("unknown variant"); return -1;
  }
}

int dsend_packet_diplomacy_accept_treaty_req(struct connection *pc, int counterpart)
{
  struct packet_diplomacy_accept_treaty_req packet, *real_packet = &packet;

  real_packet->counterpart = counterpart;
  
  return send_packet_diplomacy_accept_treaty_req(pc, real_packet);
}

#define hash_packet_diplomacy_accept_treaty_100 hash_const

#define cmp_packet_diplomacy_accept_treaty_100 cmp_const

BV_DEFINE(packet_diplomacy_accept_treaty_100_fields, 3);

static struct packet_diplomacy_accept_treaty *receive_packet_diplomacy_accept_treaty_100(struct connection *pc, enum packet_type type)
{
  packet_diplomacy_accept_treaty_100_fields fields;
  struct packet_diplomacy_accept_treaty *old;
  struct hash_table **hash = &pc->phs.received[type];
  struct packet_diplomacy_accept_treaty *clone;
  RECEIVE_PACKET_START(packet_diplomacy_accept_treaty, real_packet);

  DIO_BV_GET(&din, fields);


  if (!*hash) {
    *hash = hash_new(hash_packet_diplomacy_accept_treaty_100, cmp_packet_diplomacy_accept_treaty_100);
  }
  old = hash_delete_entry(*hash, real_packet);

  if (old) {
    *real_packet = *old;
  } else {
    memset(real_packet, 0, sizeof(*real_packet));
  }

  if (BV_ISSET(fields, 0)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->counterpart = readin;
    }
  }
  real_packet->I_accepted = BV_ISSET(fields, 1);
  real_packet->other_accepted = BV_ISSET(fields, 2);

  clone = fc_malloc(sizeof(*clone));
  *clone = *real_packet;
  if (old) {
    free(old);
  }
  hash_insert(*hash, clone, clone);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_diplomacy_accept_treaty_100(struct connection *pc, const struct packet_diplomacy_accept_treaty *packet)
{
  const struct packet_diplomacy_accept_treaty *real_packet = packet;
  packet_diplomacy_accept_treaty_100_fields fields;
  struct packet_diplomacy_accept_treaty *old, *clone;
  bool differ, old_from_hash, force_send_of_unchanged = TRUE;
  struct hash_table **hash = &pc->phs.sent[PACKET_DIPLOMACY_ACCEPT_TREATY];
  int different = 0;
  SEND_PACKET_START(PACKET_DIPLOMACY_ACCEPT_TREATY);

  if (!*hash) {
    *hash = hash_new(hash_packet_diplomacy_accept_treaty_100, cmp_packet_diplomacy_accept_treaty_100);
  }
  BV_CLR_ALL(fields);

  old = hash_lookup_data(*hash, real_packet);
  old_from_hash = (old != NULL);
  if (!old) {
    old = fc_malloc(sizeof(*old));
    memset(old, 0, sizeof(*old));
    force_send_of_unchanged = TRUE;
  }

  differ = (old->counterpart != real_packet->counterpart);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 0);}

  differ = (old->I_accepted != real_packet->I_accepted);
  if(differ) {different++;}
  if(packet->I_accepted) {BV_SET(fields, 1);}

  differ = (old->other_accepted != real_packet->other_accepted);
  if(differ) {different++;}
  if(packet->other_accepted) {BV_SET(fields, 2);}

  if (different == 0 && !force_send_of_unchanged) {
    return 0;
  }

  DIO_BV_PUT(&dout, fields);

  if (BV_ISSET(fields, 0)) {
    dio_put_uint8(&dout, real_packet->counterpart);
  }
  /* field 1 is folded into the header */
  /* field 2 is folded into the header */


  if (old_from_hash) {
    hash_delete_entry(*hash, old);
  }

  clone = old;

  *clone = *real_packet;
  hash_insert(*hash, clone, clone);
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_diplomacy_accept_treaty(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_DIPLOMACY_ACCEPT_TREATY] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_DIPLOMACY_ACCEPT_TREATY] = variant;
}

struct packet_diplomacy_accept_treaty *receive_packet_diplomacy_accept_treaty(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Receiving packet_diplomacy_accept_treaty at the server.");
  }
  ensure_valid_variant_packet_diplomacy_accept_treaty(pc);

  switch(pc->phs.variant[PACKET_DIPLOMACY_ACCEPT_TREATY]) {
    case 100: return receive_packet_diplomacy_accept_treaty_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_diplomacy_accept_treaty(struct connection *pc, const struct packet_diplomacy_accept_treaty *packet)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Sending packet_diplomacy_accept_treaty from the client.");
  }
  ensure_valid_variant_packet_diplomacy_accept_treaty(pc);

  switch(pc->phs.variant[PACKET_DIPLOMACY_ACCEPT_TREATY]) {
    case 100: return send_packet_diplomacy_accept_treaty_100(pc, packet);
    default: die("unknown variant"); return -1;
  }
}

void lsend_packet_diplomacy_accept_treaty(struct conn_list *dest, const struct packet_diplomacy_accept_treaty *packet)
{
  conn_list_iterate(*dest, pconn) {
    send_packet_diplomacy_accept_treaty(pconn, packet);
  } conn_list_iterate_end;
}

int dsend_packet_diplomacy_accept_treaty(struct connection *pc, int counterpart, bool I_accepted, bool other_accepted)
{
  struct packet_diplomacy_accept_treaty packet, *real_packet = &packet;

  real_packet->counterpart = counterpart;
  real_packet->I_accepted = I_accepted;
  real_packet->other_accepted = other_accepted;
  
  return send_packet_diplomacy_accept_treaty(pc, real_packet);
}

void dlsend_packet_diplomacy_accept_treaty(struct conn_list *dest, int counterpart, bool I_accepted, bool other_accepted)
{
  struct packet_diplomacy_accept_treaty packet, *real_packet = &packet;

  real_packet->counterpart = counterpart;
  real_packet->I_accepted = I_accepted;
  real_packet->other_accepted = other_accepted;
  
  lsend_packet_diplomacy_accept_treaty(dest, real_packet);
}

#define hash_packet_diplomacy_cancel_pact_100 hash_const

#define cmp_packet_diplomacy_cancel_pact_100 cmp_const

BV_DEFINE(packet_diplomacy_cancel_pact_100_fields, 2);

static struct packet_diplomacy_cancel_pact *receive_packet_diplomacy_cancel_pact_100(struct connection *pc, enum packet_type type)
{
  packet_diplomacy_cancel_pact_100_fields fields;
  struct packet_diplomacy_cancel_pact *old;
  struct hash_table **hash = &pc->phs.received[type];
  struct packet_diplomacy_cancel_pact *clone;
  RECEIVE_PACKET_START(packet_diplomacy_cancel_pact, real_packet);

  DIO_BV_GET(&din, fields);


  if (!*hash) {
    *hash = hash_new(hash_packet_diplomacy_cancel_pact_100, cmp_packet_diplomacy_cancel_pact_100);
  }
  old = hash_delete_entry(*hash, real_packet);

  if (old) {
    *real_packet = *old;
  } else {
    memset(real_packet, 0, sizeof(*real_packet));
  }

  if (BV_ISSET(fields, 0)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->other_player_id = readin;
    }
  }
  if (BV_ISSET(fields, 1)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->clause = readin;
    }
  }

  clone = fc_malloc(sizeof(*clone));
  *clone = *real_packet;
  if (old) {
    free(old);
  }
  hash_insert(*hash, clone, clone);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_diplomacy_cancel_pact_100(struct connection *pc, const struct packet_diplomacy_cancel_pact *packet)
{
  const struct packet_diplomacy_cancel_pact *real_packet = packet;
  packet_diplomacy_cancel_pact_100_fields fields;
  struct packet_diplomacy_cancel_pact *old, *clone;
  bool differ, old_from_hash, force_send_of_unchanged = TRUE;
  struct hash_table **hash = &pc->phs.sent[PACKET_DIPLOMACY_CANCEL_PACT];
  int different = 0;
  SEND_PACKET_START(PACKET_DIPLOMACY_CANCEL_PACT);

  if (!*hash) {
    *hash = hash_new(hash_packet_diplomacy_cancel_pact_100, cmp_packet_diplomacy_cancel_pact_100);
  }
  BV_CLR_ALL(fields);

  old = hash_lookup_data(*hash, real_packet);
  old_from_hash = (old != NULL);
  if (!old) {
    old = fc_malloc(sizeof(*old));
    memset(old, 0, sizeof(*old));
    force_send_of_unchanged = TRUE;
  }

  differ = (old->other_player_id != real_packet->other_player_id);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 0);}

  differ = (old->clause != real_packet->clause);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 1);}

  if (different == 0 && !force_send_of_unchanged) {
    return 0;
  }

  DIO_BV_PUT(&dout, fields);

  if (BV_ISSET(fields, 0)) {
    dio_put_uint8(&dout, real_packet->other_player_id);
  }
  if (BV_ISSET(fields, 1)) {
    dio_put_uint8(&dout, real_packet->clause);
  }


  if (old_from_hash) {
    hash_delete_entry(*hash, old);
  }

  clone = old;

  *clone = *real_packet;
  hash_insert(*hash, clone, clone);
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_diplomacy_cancel_pact(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_DIPLOMACY_CANCEL_PACT] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_DIPLOMACY_CANCEL_PACT] = variant;
}

struct packet_diplomacy_cancel_pact *receive_packet_diplomacy_cancel_pact(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Receiving packet_diplomacy_cancel_pact at the client.");
  }
  ensure_valid_variant_packet_diplomacy_cancel_pact(pc);

  switch(pc->phs.variant[PACKET_DIPLOMACY_CANCEL_PACT]) {
    case 100: return receive_packet_diplomacy_cancel_pact_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_diplomacy_cancel_pact(struct connection *pc, const struct packet_diplomacy_cancel_pact *packet)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Sending packet_diplomacy_cancel_pact from the server.");
  }
  ensure_valid_variant_packet_diplomacy_cancel_pact(pc);

  switch(pc->phs.variant[PACKET_DIPLOMACY_CANCEL_PACT]) {
    case 100: return send_packet_diplomacy_cancel_pact_100(pc, packet);
    default: die("unknown variant"); return -1;
  }
}

int dsend_packet_diplomacy_cancel_pact(struct connection *pc, int other_player_id, enum clause_type clause)
{
  struct packet_diplomacy_cancel_pact packet, *real_packet = &packet;

  real_packet->other_player_id = other_player_id;
  real_packet->clause = clause;
  
  return send_packet_diplomacy_cancel_pact(pc, real_packet);
}

#define hash_packet_page_msg_100 hash_const

#define cmp_packet_page_msg_100 cmp_const

BV_DEFINE(packet_page_msg_100_fields, 2);

static struct packet_page_msg *receive_packet_page_msg_100(struct connection *pc, enum packet_type type)
{
  packet_page_msg_100_fields fields;
  struct packet_page_msg *old;
  struct hash_table **hash = &pc->phs.received[type];
  struct packet_page_msg *clone;
  RECEIVE_PACKET_START(packet_page_msg, real_packet);

  DIO_BV_GET(&din, fields);


  if (!*hash) {
    *hash = hash_new(hash_packet_page_msg_100, cmp_packet_page_msg_100);
  }
  old = hash_delete_entry(*hash, real_packet);

  if (old) {
    *real_packet = *old;
  } else {
    memset(real_packet, 0, sizeof(*real_packet));
  }

  if (BV_ISSET(fields, 0)) {
    dio_get_string(&din, real_packet->message, sizeof(real_packet->message));
  }
  if (BV_ISSET(fields, 1)) {
    {
      int readin;
    
      dio_get_sint16(&din, &readin);
      real_packet->event = readin;
    }
  }

  clone = fc_malloc(sizeof(*clone));
  *clone = *real_packet;
  if (old) {
    free(old);
  }
  hash_insert(*hash, clone, clone);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_page_msg_100(struct connection *pc, const struct packet_page_msg *packet)
{
  const struct packet_page_msg *real_packet = packet;
  packet_page_msg_100_fields fields;
  struct packet_page_msg *old, *clone;
  bool differ, old_from_hash, force_send_of_unchanged = TRUE;
  struct hash_table **hash = &pc->phs.sent[PACKET_PAGE_MSG];
  int different = 0;
  SEND_PACKET_START(PACKET_PAGE_MSG);

  if (!*hash) {
    *hash = hash_new(hash_packet_page_msg_100, cmp_packet_page_msg_100);
  }
  BV_CLR_ALL(fields);

  old = hash_lookup_data(*hash, real_packet);
  old_from_hash = (old != NULL);
  if (!old) {
    old = fc_malloc(sizeof(*old));
    memset(old, 0, sizeof(*old));
    force_send_of_unchanged = TRUE;
  }

  differ = (strcmp(old->message, real_packet->message) != 0);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 0);}

  differ = (old->event != real_packet->event);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 1);}

  if (different == 0 && !force_send_of_unchanged) {
    return 0;
  }

  DIO_BV_PUT(&dout, fields);

  if (BV_ISSET(fields, 0)) {
    dio_put_string(&dout, real_packet->message);
  }
  if (BV_ISSET(fields, 1)) {
    dio_put_sint16(&dout, real_packet->event);
  }


  if (old_from_hash) {
    hash_delete_entry(*hash, old);
  }

  clone = old;

  *clone = *real_packet;
  hash_insert(*hash, clone, clone);
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_page_msg(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_PAGE_MSG] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_PAGE_MSG] = variant;
}

struct packet_page_msg *receive_packet_page_msg(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Receiving packet_page_msg at the server.");
  }
  ensure_valid_variant_packet_page_msg(pc);

  switch(pc->phs.variant[PACKET_PAGE_MSG]) {
    case 100: return receive_packet_page_msg_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_page_msg(struct connection *pc, const struct packet_page_msg *packet)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Sending packet_page_msg from the client.");
  }
  ensure_valid_variant_packet_page_msg(pc);

  switch(pc->phs.variant[PACKET_PAGE_MSG]) {
    case 100: return send_packet_page_msg_100(pc, packet);
    default: die("unknown variant"); return -1;
  }
}

void lsend_packet_page_msg(struct conn_list *dest, const struct packet_page_msg *packet)
{
  conn_list_iterate(*dest, pconn) {
    send_packet_page_msg(pconn, packet);
  } conn_list_iterate_end;
}

#define hash_packet_report_req_100 hash_const

#define cmp_packet_report_req_100 cmp_const

BV_DEFINE(packet_report_req_100_fields, 1);

static struct packet_report_req *receive_packet_report_req_100(struct connection *pc, enum packet_type type)
{
  packet_report_req_100_fields fields;
  struct packet_report_req *old;
  struct hash_table **hash = &pc->phs.received[type];
  struct packet_report_req *clone;
  RECEIVE_PACKET_START(packet_report_req, real_packet);

  DIO_BV_GET(&din, fields);


  if (!*hash) {
    *hash = hash_new(hash_packet_report_req_100, cmp_packet_report_req_100);
  }
  old = hash_delete_entry(*hash, real_packet);

  if (old) {
    *real_packet = *old;
  } else {
    memset(real_packet, 0, sizeof(*real_packet));
  }

  if (BV_ISSET(fields, 0)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->type = readin;
    }
  }

  clone = fc_malloc(sizeof(*clone));
  *clone = *real_packet;
  if (old) {
    free(old);
  }
  hash_insert(*hash, clone, clone);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_report_req_100(struct connection *pc, const struct packet_report_req *packet)
{
  const struct packet_report_req *real_packet = packet;
  packet_report_req_100_fields fields;
  struct packet_report_req *old, *clone;
  bool differ, old_from_hash, force_send_of_unchanged = TRUE;
  struct hash_table **hash = &pc->phs.sent[PACKET_REPORT_REQ];
  int different = 0;
  SEND_PACKET_START(PACKET_REPORT_REQ);

  if (!*hash) {
    *hash = hash_new(hash_packet_report_req_100, cmp_packet_report_req_100);
  }
  BV_CLR_ALL(fields);

  old = hash_lookup_data(*hash, real_packet);
  old_from_hash = (old != NULL);
  if (!old) {
    old = fc_malloc(sizeof(*old));
    memset(old, 0, sizeof(*old));
    force_send_of_unchanged = TRUE;
  }

  differ = (old->type != real_packet->type);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 0);}

  if (different == 0 && !force_send_of_unchanged) {
    return 0;
  }

  DIO_BV_PUT(&dout, fields);

  if (BV_ISSET(fields, 0)) {
    dio_put_uint8(&dout, real_packet->type);
  }


  if (old_from_hash) {
    hash_delete_entry(*hash, old);
  }

  clone = old;

  *clone = *real_packet;
  hash_insert(*hash, clone, clone);
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_report_req(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_REPORT_REQ] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_REPORT_REQ] = variant;
}

struct packet_report_req *receive_packet_report_req(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Receiving packet_report_req at the client.");
  }
  ensure_valid_variant_packet_report_req(pc);

  switch(pc->phs.variant[PACKET_REPORT_REQ]) {
    case 100: return receive_packet_report_req_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_report_req(struct connection *pc, const struct packet_report_req *packet)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Sending packet_report_req from the server.");
  }
  ensure_valid_variant_packet_report_req(pc);

  switch(pc->phs.variant[PACKET_REPORT_REQ]) {
    case 100: return send_packet_report_req_100(pc, packet);
    default: die("unknown variant"); return -1;
  }
}

int dsend_packet_report_req(struct connection *pc, enum report_type type)
{
  struct packet_report_req packet, *real_packet = &packet;

  real_packet->type = type;
  
  return send_packet_report_req(pc, real_packet);
}

static unsigned int hash_packet_conn_info_100(const void *vkey, unsigned int num_buckets)
{
  const struct packet_conn_info *key = (const struct packet_conn_info *) vkey;

  return ((key->id) % num_buckets);
}

static int cmp_packet_conn_info_100(const void *vkey1, const void *vkey2)
{
  const struct packet_conn_info *key1 = (const struct packet_conn_info *) vkey1;
  const struct packet_conn_info *key2 = (const struct packet_conn_info *) vkey2;
  int diff;

  diff = key1->id - key2->id;
  if (diff != 0) {
    return diff;
  }

  return 0;
}

BV_DEFINE(packet_conn_info_100_fields, 8);

static struct packet_conn_info *receive_packet_conn_info_100(struct connection *pc, enum packet_type type)
{
  packet_conn_info_100_fields fields;
  struct packet_conn_info *old;
  struct hash_table **hash = &pc->phs.received[type];
  struct packet_conn_info *clone;
  RECEIVE_PACKET_START(packet_conn_info, real_packet);

  DIO_BV_GET(&din, fields);
  {
    int readin;
  
    dio_get_uint8(&din, &readin);
    real_packet->id = readin;
  }


  if (!*hash) {
    *hash = hash_new(hash_packet_conn_info_100, cmp_packet_conn_info_100);
  }
  old = hash_delete_entry(*hash, real_packet);

  if (old) {
    *real_packet = *old;
  } else {
    int id = real_packet->id;

    memset(real_packet, 0, sizeof(*real_packet));

    real_packet->id = id;
  }

  real_packet->used = BV_ISSET(fields, 0);
  real_packet->established = BV_ISSET(fields, 1);
  real_packet->observer = BV_ISSET(fields, 2);
  if (BV_ISSET(fields, 3)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->player_num = readin;
    }
  }
  if (BV_ISSET(fields, 4)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->access_level = readin;
    }
  }
  if (BV_ISSET(fields, 5)) {
    dio_get_string(&din, real_packet->username, sizeof(real_packet->username));
  }
  if (BV_ISSET(fields, 6)) {
    dio_get_string(&din, real_packet->addr, sizeof(real_packet->addr));
  }
  if (BV_ISSET(fields, 7)) {
    dio_get_string(&din, real_packet->capability, sizeof(real_packet->capability));
  }

  clone = fc_malloc(sizeof(*clone));
  *clone = *real_packet;
  if (old) {
    free(old);
  }
  hash_insert(*hash, clone, clone);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_conn_info_100(struct connection *pc, const struct packet_conn_info *packet)
{
  const struct packet_conn_info *real_packet = packet;
  packet_conn_info_100_fields fields;
  struct packet_conn_info *old, *clone;
  bool differ, old_from_hash, force_send_of_unchanged = FALSE;
  struct hash_table **hash = &pc->phs.sent[PACKET_CONN_INFO];
  int different = 0;
  SEND_PACKET_START(PACKET_CONN_INFO);

  if (!*hash) {
    *hash = hash_new(hash_packet_conn_info_100, cmp_packet_conn_info_100);
  }
  BV_CLR_ALL(fields);

  old = hash_lookup_data(*hash, real_packet);
  old_from_hash = (old != NULL);
  if (!old) {
    old = fc_malloc(sizeof(*old));
    memset(old, 0, sizeof(*old));
    force_send_of_unchanged = TRUE;
  }

  differ = (old->used != real_packet->used);
  if(differ) {different++;}
  if(packet->used) {BV_SET(fields, 0);}

  differ = (old->established != real_packet->established);
  if(differ) {different++;}
  if(packet->established) {BV_SET(fields, 1);}

  differ = (old->observer != real_packet->observer);
  if(differ) {different++;}
  if(packet->observer) {BV_SET(fields, 2);}

  differ = (old->player_num != real_packet->player_num);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 3);}

  differ = (old->access_level != real_packet->access_level);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 4);}

  differ = (strcmp(old->username, real_packet->username) != 0);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 5);}

  differ = (strcmp(old->addr, real_packet->addr) != 0);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 6);}

  differ = (strcmp(old->capability, real_packet->capability) != 0);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 7);}

  if (different == 0 && !force_send_of_unchanged) {
    return 0;
  }

  DIO_BV_PUT(&dout, fields);
  dio_put_uint8(&dout, real_packet->id);

  /* field 0 is folded into the header */
  /* field 1 is folded into the header */
  /* field 2 is folded into the header */
  if (BV_ISSET(fields, 3)) {
    dio_put_uint8(&dout, real_packet->player_num);
  }
  if (BV_ISSET(fields, 4)) {
    dio_put_uint8(&dout, real_packet->access_level);
  }
  if (BV_ISSET(fields, 5)) {
    dio_put_string(&dout, real_packet->username);
  }
  if (BV_ISSET(fields, 6)) {
    dio_put_string(&dout, real_packet->addr);
  }
  if (BV_ISSET(fields, 7)) {
    dio_put_string(&dout, real_packet->capability);
  }


  if (old_from_hash) {
    hash_delete_entry(*hash, old);
  }

  clone = old;

  *clone = *real_packet;
  hash_insert(*hash, clone, clone);
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_conn_info(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_CONN_INFO] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_CONN_INFO] = variant;
}

struct packet_conn_info *receive_packet_conn_info(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Receiving packet_conn_info at the server.");
  }
  ensure_valid_variant_packet_conn_info(pc);

  switch(pc->phs.variant[PACKET_CONN_INFO]) {
    case 100: return receive_packet_conn_info_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_conn_info(struct connection *pc, const struct packet_conn_info *packet)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Sending packet_conn_info from the client.");
  }
  ensure_valid_variant_packet_conn_info(pc);

  switch(pc->phs.variant[PACKET_CONN_INFO]) {
    case 100: return send_packet_conn_info_100(pc, packet);
    default: die("unknown variant"); return -1;
  }
}

void lsend_packet_conn_info(struct conn_list *dest, const struct packet_conn_info *packet)
{
  conn_list_iterate(*dest, pconn) {
    send_packet_conn_info(pconn, packet);
  } conn_list_iterate_end;
}

#define hash_packet_conn_ping_info_100 hash_const

#define cmp_packet_conn_ping_info_100 cmp_const

BV_DEFINE(packet_conn_ping_info_100_fields, 3);

static struct packet_conn_ping_info *receive_packet_conn_ping_info_100(struct connection *pc, enum packet_type type)
{
  packet_conn_ping_info_100_fields fields;
  struct packet_conn_ping_info *old;
  struct hash_table **hash = &pc->phs.received[type];
  struct packet_conn_ping_info *clone;
  RECEIVE_PACKET_START(packet_conn_ping_info, real_packet);

  DIO_BV_GET(&din, fields);


  if (!*hash) {
    *hash = hash_new(hash_packet_conn_ping_info_100, cmp_packet_conn_ping_info_100);
  }
  old = hash_delete_entry(*hash, real_packet);

  if (old) {
    *real_packet = *old;
  } else {
    memset(real_packet, 0, sizeof(*real_packet));
  }

  if (BV_ISSET(fields, 0)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->connections = readin;
    }
  }
  if (BV_ISSET(fields, 1)) {
    
    {
      int i;
    
      if(real_packet->connections > MAX_NUM_CONNECTIONS) {
        freelog(LOG_ERROR, "packets_gen.c: WARNING: truncation array");
        real_packet->connections = MAX_NUM_CONNECTIONS;
      }
      for (i = 0; i < real_packet->connections; i++) {
        {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->conn_id[i] = readin;
    }
      }
    }
  }
  if (BV_ISSET(fields, 2)) {
    
    {
      int i;
    
      if(real_packet->connections > MAX_NUM_CONNECTIONS) {
        freelog(LOG_ERROR, "packets_gen.c: WARNING: truncation array");
        real_packet->connections = MAX_NUM_CONNECTIONS;
      }
      for (i = 0; i < real_packet->connections; i++) {
        int tmp;
    
        dio_get_uint32(&din, &tmp);
        real_packet->ping_time[i] = (float)(tmp) / 1000000.0;
      }
    }
  }

  clone = fc_malloc(sizeof(*clone));
  *clone = *real_packet;
  if (old) {
    free(old);
  }
  hash_insert(*hash, clone, clone);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_conn_ping_info_100(struct connection *pc, const struct packet_conn_ping_info *packet)
{
  const struct packet_conn_ping_info *real_packet = packet;
  packet_conn_ping_info_100_fields fields;
  struct packet_conn_ping_info *old, *clone;
  bool differ, old_from_hash, force_send_of_unchanged = TRUE;
  struct hash_table **hash = &pc->phs.sent[PACKET_CONN_PING_INFO];
  int different = 0;
  SEND_PACKET_START(PACKET_CONN_PING_INFO);

  if (!*hash) {
    *hash = hash_new(hash_packet_conn_ping_info_100, cmp_packet_conn_ping_info_100);
  }
  BV_CLR_ALL(fields);

  old = hash_lookup_data(*hash, real_packet);
  old_from_hash = (old != NULL);
  if (!old) {
    old = fc_malloc(sizeof(*old));
    memset(old, 0, sizeof(*old));
    force_send_of_unchanged = TRUE;
  }

  differ = (old->connections != real_packet->connections);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 0);}


    {
      differ = (old->connections != real_packet->connections);
      if(!differ) {
        int i;
        for (i = 0; i < real_packet->connections; i++) {
          if (old->conn_id[i] != real_packet->conn_id[i]) {
            differ = TRUE;
            break;
          }
        }
      }
    }
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 1);}


    {
      differ = (old->connections != real_packet->connections);
      if(!differ) {
        int i;
        for (i = 0; i < real_packet->connections; i++) {
          if (old->ping_time[i] != real_packet->ping_time[i]) {
            differ = TRUE;
            break;
          }
        }
      }
    }
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 2);}

  if (different == 0 && !force_send_of_unchanged) {
    return 0;
  }

  DIO_BV_PUT(&dout, fields);

  if (BV_ISSET(fields, 0)) {
    dio_put_uint8(&dout, real_packet->connections);
  }
  if (BV_ISSET(fields, 1)) {
  
    {
      int i;

      for (i = 0; i < real_packet->connections; i++) {
        dio_put_uint8(&dout, real_packet->conn_id[i]);
      }
    } 
  }
  if (BV_ISSET(fields, 2)) {
  
    {
      int i;

      for (i = 0; i < real_packet->connections; i++) {
          dio_put_uint32(&dout, (int)(real_packet->ping_time[i] * 1000000));
      }
    } 
  }


  if (old_from_hash) {
    hash_delete_entry(*hash, old);
  }

  clone = old;

  *clone = *real_packet;
  hash_insert(*hash, clone, clone);
  SEND_PACKET_END;
}

#define hash_packet_conn_ping_info_101 hash_const

#define cmp_packet_conn_ping_info_101 cmp_const

BV_DEFINE(packet_conn_ping_info_101_fields, 3);

static struct packet_conn_ping_info *receive_packet_conn_ping_info_101(struct connection *pc, enum packet_type type)
{
  packet_conn_ping_info_101_fields fields;
  struct packet_conn_ping_info *old;
  struct hash_table **hash = &pc->phs.received[type];
  struct packet_conn_ping_info *clone;
  RECEIVE_PACKET_START(packet_conn_ping_info, real_packet);

  DIO_BV_GET(&din, fields);


  if (!*hash) {
    *hash = hash_new(hash_packet_conn_ping_info_101, cmp_packet_conn_ping_info_101);
  }
  old = hash_delete_entry(*hash, real_packet);

  if (old) {
    *real_packet = *old;
  } else {
    memset(real_packet, 0, sizeof(*real_packet));
  }

  if (BV_ISSET(fields, 0)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->old_connections = readin;
    }
  }
  if (BV_ISSET(fields, 1)) {
    
    {
      int i;
    
      if(real_packet->connections > MAX_NUM_PLAYERS) {
        freelog(LOG_ERROR, "packets_gen.c: WARNING: truncation array");
        real_packet->connections = MAX_NUM_PLAYERS;
      }
      for (i = 0; i < real_packet->connections; i++) {
        {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->old_conn_id[i] = readin;
    }
      }
    }
  }
  if (BV_ISSET(fields, 2)) {
    
    {
      int i;
    
      if(real_packet->connections > MAX_NUM_PLAYERS) {
        freelog(LOG_ERROR, "packets_gen.c: WARNING: truncation array");
        real_packet->connections = MAX_NUM_PLAYERS;
      }
      for (i = 0; i < real_packet->connections; i++) {
        int tmp;
    
        dio_get_uint32(&din, &tmp);
        real_packet->old_ping_time[i] = (float)(tmp) / 1000000.0;
      }
    }
  }

  clone = fc_malloc(sizeof(*clone));
  *clone = *real_packet;
  if (old) {
    free(old);
  }
  hash_insert(*hash, clone, clone);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_conn_ping_info_101(struct connection *pc, const struct packet_conn_ping_info *packet)
{
  const struct packet_conn_ping_info *real_packet = packet;
  packet_conn_ping_info_101_fields fields;
  struct packet_conn_ping_info *old, *clone;
  bool differ, old_from_hash, force_send_of_unchanged = TRUE;
  struct hash_table **hash = &pc->phs.sent[PACKET_CONN_PING_INFO];
  int different = 0;
  SEND_PACKET_START(PACKET_CONN_PING_INFO);

  if (!*hash) {
    *hash = hash_new(hash_packet_conn_ping_info_101, cmp_packet_conn_ping_info_101);
  }
  BV_CLR_ALL(fields);

  old = hash_lookup_data(*hash, real_packet);
  old_from_hash = (old != NULL);
  if (!old) {
    old = fc_malloc(sizeof(*old));
    memset(old, 0, sizeof(*old));
    force_send_of_unchanged = TRUE;
  }

  differ = (old->old_connections != real_packet->old_connections);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 0);}


    {
      differ = (old->connections != real_packet->connections);
      if(!differ) {
        int i;
        for (i = 0; i < real_packet->connections; i++) {
          if (old->old_conn_id[i] != real_packet->old_conn_id[i]) {
            differ = TRUE;
            break;
          }
        }
      }
    }
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 1);}


    {
      differ = (old->connections != real_packet->connections);
      if(!differ) {
        int i;
        for (i = 0; i < real_packet->connections; i++) {
          if (old->old_ping_time[i] != real_packet->old_ping_time[i]) {
            differ = TRUE;
            break;
          }
        }
      }
    }
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 2);}

  if (different == 0 && !force_send_of_unchanged) {
    return 0;
  }

  DIO_BV_PUT(&dout, fields);

  if (BV_ISSET(fields, 0)) {
    dio_put_uint8(&dout, real_packet->old_connections);
  }
  if (BV_ISSET(fields, 1)) {
  
    {
      int i;

      for (i = 0; i < real_packet->connections; i++) {
        dio_put_uint8(&dout, real_packet->old_conn_id[i]);
      }
    } 
  }
  if (BV_ISSET(fields, 2)) {
  
    {
      int i;

      for (i = 0; i < real_packet->connections; i++) {
          dio_put_uint32(&dout, (int)(real_packet->old_ping_time[i] * 1000000));
      }
    } 
  }


  if (old_from_hash) {
    hash_delete_entry(*hash, old);
  }

  clone = old;

  *clone = *real_packet;
  hash_insert(*hash, clone, clone);
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_conn_ping_info(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_CONN_PING_INFO] != -1) {
    return;
  }

  if(FALSE) {
  } else if((has_capability("conn_ping_info", pc->capability) && has_capability("conn_ping_info", our_capability))) {
    variant = 100;
  } else if(!(has_capability("conn_ping_info", pc->capability) && has_capability("conn_ping_info", our_capability))) {
    variant = 101;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_CONN_PING_INFO] = variant;
}

struct packet_conn_ping_info *receive_packet_conn_ping_info(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Receiving packet_conn_ping_info at the server.");
  }
  ensure_valid_variant_packet_conn_ping_info(pc);

  switch(pc->phs.variant[PACKET_CONN_PING_INFO]) {
    case 100: return receive_packet_conn_ping_info_100(pc, type);
    case 101: return receive_packet_conn_ping_info_101(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_conn_ping_info(struct connection *pc, const struct packet_conn_ping_info *packet)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Sending packet_conn_ping_info from the client.");
  }
  ensure_valid_variant_packet_conn_ping_info(pc);

  switch(pc->phs.variant[PACKET_CONN_PING_INFO]) {
    case 100: return send_packet_conn_ping_info_100(pc, packet);
    case 101: return send_packet_conn_ping_info_101(pc, packet);
    default: die("unknown variant"); return -1;
  }
}

void lsend_packet_conn_ping_info(struct conn_list *dest, const struct packet_conn_ping_info *packet)
{
  conn_list_iterate(*dest, pconn) {
    send_packet_conn_ping_info(pconn, packet);
  } conn_list_iterate_end;
}

static struct packet_conn_ping *receive_packet_conn_ping_100(struct connection *pc, enum packet_type type)
{
  RECEIVE_PACKET_START(packet_conn_ping, real_packet);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_conn_ping_100(struct connection *pc)
{
  SEND_PACKET_START(PACKET_CONN_PING);
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_conn_ping(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_CONN_PING] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_CONN_PING] = variant;
}

struct packet_conn_ping *receive_packet_conn_ping(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Receiving packet_conn_ping at the server.");
  }
  ensure_valid_variant_packet_conn_ping(pc);

  switch(pc->phs.variant[PACKET_CONN_PING]) {
    case 100: return receive_packet_conn_ping_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_conn_ping(struct connection *pc)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Sending packet_conn_ping from the client.");
  }
  ensure_valid_variant_packet_conn_ping(pc);

  switch(pc->phs.variant[PACKET_CONN_PING]) {
    case 100: return send_packet_conn_ping_100(pc);
    default: die("unknown variant"); return -1;
  }
}

static struct packet_conn_pong *receive_packet_conn_pong_100(struct connection *pc, enum packet_type type)
{
  RECEIVE_PACKET_START(packet_conn_pong, real_packet);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_conn_pong_100(struct connection *pc)
{
  SEND_PACKET_START(PACKET_CONN_PONG);
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_conn_pong(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_CONN_PONG] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_CONN_PONG] = variant;
}

struct packet_conn_pong *receive_packet_conn_pong(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Receiving packet_conn_pong at the client.");
  }
  ensure_valid_variant_packet_conn_pong(pc);

  switch(pc->phs.variant[PACKET_CONN_PONG]) {
    case 100: return receive_packet_conn_pong_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_conn_pong(struct connection *pc)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Sending packet_conn_pong from the server.");
  }
  ensure_valid_variant_packet_conn_pong(pc);

  switch(pc->phs.variant[PACKET_CONN_PONG]) {
    case 100: return send_packet_conn_pong_100(pc);
    default: die("unknown variant"); return -1;
  }
}

static struct packet_before_new_year *receive_packet_before_new_year_100(struct connection *pc, enum packet_type type)
{
  RECEIVE_PACKET_START(packet_before_new_year, real_packet);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_before_new_year_100(struct connection *pc)
{
  SEND_PACKET_START(PACKET_BEFORE_NEW_YEAR);
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_before_new_year(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_BEFORE_NEW_YEAR] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_BEFORE_NEW_YEAR] = variant;
}

struct packet_before_new_year *receive_packet_before_new_year(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Receiving packet_before_new_year at the server.");
  }
  ensure_valid_variant_packet_before_new_year(pc);

  switch(pc->phs.variant[PACKET_BEFORE_NEW_YEAR]) {
    case 100: return receive_packet_before_new_year_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_before_new_year(struct connection *pc)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Sending packet_before_new_year from the client.");
  }
  ensure_valid_variant_packet_before_new_year(pc);

  switch(pc->phs.variant[PACKET_BEFORE_NEW_YEAR]) {
    case 100: return send_packet_before_new_year_100(pc);
    default: die("unknown variant"); return -1;
  }
}

void lsend_packet_before_new_year(struct conn_list *dest)
{
  conn_list_iterate(*dest, pconn) {
    send_packet_before_new_year(pconn);
  } conn_list_iterate_end;
}

static struct packet_start_turn *receive_packet_start_turn_100(struct connection *pc, enum packet_type type)
{
  RECEIVE_PACKET_START(packet_start_turn, real_packet);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_start_turn_100(struct connection *pc)
{
  SEND_PACKET_START(PACKET_START_TURN);
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_start_turn(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_START_TURN] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_START_TURN] = variant;
}

struct packet_start_turn *receive_packet_start_turn(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Receiving packet_start_turn at the server.");
  }
  ensure_valid_variant_packet_start_turn(pc);

  switch(pc->phs.variant[PACKET_START_TURN]) {
    case 100: return receive_packet_start_turn_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_start_turn(struct connection *pc)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Sending packet_start_turn from the client.");
  }
  ensure_valid_variant_packet_start_turn(pc);

  switch(pc->phs.variant[PACKET_START_TURN]) {
    case 100: return send_packet_start_turn_100(pc);
    default: die("unknown variant"); return -1;
  }
}

void lsend_packet_start_turn(struct conn_list *dest)
{
  conn_list_iterate(*dest, pconn) {
    send_packet_start_turn(pconn);
  } conn_list_iterate_end;
}

#define hash_packet_new_year_100 hash_const

#define cmp_packet_new_year_100 cmp_const

BV_DEFINE(packet_new_year_100_fields, 2);

static struct packet_new_year *receive_packet_new_year_100(struct connection *pc, enum packet_type type)
{
  packet_new_year_100_fields fields;
  struct packet_new_year *old;
  struct hash_table **hash = &pc->phs.received[type];
  struct packet_new_year *clone;
  RECEIVE_PACKET_START(packet_new_year, real_packet);

  DIO_BV_GET(&din, fields);


  if (!*hash) {
    *hash = hash_new(hash_packet_new_year_100, cmp_packet_new_year_100);
  }
  old = hash_delete_entry(*hash, real_packet);

  if (old) {
    *real_packet = *old;
  } else {
    memset(real_packet, 0, sizeof(*real_packet));
  }

  if (BV_ISSET(fields, 0)) {
    {
      int readin;
    
      dio_get_sint16(&din, &readin);
      real_packet->year = readin;
    }
  }
  if (BV_ISSET(fields, 1)) {
    {
      int readin;
    
      dio_get_sint16(&din, &readin);
      real_packet->turn = readin;
    }
  }

  clone = fc_malloc(sizeof(*clone));
  *clone = *real_packet;
  if (old) {
    free(old);
  }
  hash_insert(*hash, clone, clone);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_new_year_100(struct connection *pc, const struct packet_new_year *packet)
{
  const struct packet_new_year *real_packet = packet;
  packet_new_year_100_fields fields;
  struct packet_new_year *old, *clone;
  bool differ, old_from_hash, force_send_of_unchanged = TRUE;
  struct hash_table **hash = &pc->phs.sent[PACKET_NEW_YEAR];
  int different = 0;
  SEND_PACKET_START(PACKET_NEW_YEAR);

  if (!*hash) {
    *hash = hash_new(hash_packet_new_year_100, cmp_packet_new_year_100);
  }
  BV_CLR_ALL(fields);

  old = hash_lookup_data(*hash, real_packet);
  old_from_hash = (old != NULL);
  if (!old) {
    old = fc_malloc(sizeof(*old));
    memset(old, 0, sizeof(*old));
    force_send_of_unchanged = TRUE;
  }

  differ = (old->year != real_packet->year);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 0);}

  differ = (old->turn != real_packet->turn);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 1);}

  if (different == 0 && !force_send_of_unchanged) {
    return 0;
  }

  DIO_BV_PUT(&dout, fields);

  if (BV_ISSET(fields, 0)) {
    dio_put_sint16(&dout, real_packet->year);
  }
  if (BV_ISSET(fields, 1)) {
    dio_put_sint16(&dout, real_packet->turn);
  }


  if (old_from_hash) {
    hash_delete_entry(*hash, old);
  }

  clone = old;

  *clone = *real_packet;
  hash_insert(*hash, clone, clone);
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_new_year(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_NEW_YEAR] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_NEW_YEAR] = variant;
}

struct packet_new_year *receive_packet_new_year(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Receiving packet_new_year at the server.");
  }
  ensure_valid_variant_packet_new_year(pc);

  switch(pc->phs.variant[PACKET_NEW_YEAR]) {
    case 100: return receive_packet_new_year_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_new_year(struct connection *pc, const struct packet_new_year *packet)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Sending packet_new_year from the client.");
  }
  ensure_valid_variant_packet_new_year(pc);

  switch(pc->phs.variant[PACKET_NEW_YEAR]) {
    case 100: return send_packet_new_year_100(pc, packet);
    default: die("unknown variant"); return -1;
  }
}

void lsend_packet_new_year(struct conn_list *dest, const struct packet_new_year *packet)
{
  conn_list_iterate(*dest, pconn) {
    send_packet_new_year(pconn, packet);
  } conn_list_iterate_end;
}

static struct packet_freeze_client *receive_packet_freeze_client_100(struct connection *pc, enum packet_type type)
{
  RECEIVE_PACKET_START(packet_freeze_client, real_packet);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_freeze_client_100(struct connection *pc)
{
  SEND_PACKET_START(PACKET_FREEZE_CLIENT);
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_freeze_client(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_FREEZE_CLIENT] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_FREEZE_CLIENT] = variant;
}

struct packet_freeze_client *receive_packet_freeze_client(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Receiving packet_freeze_client at the server.");
  }
  ensure_valid_variant_packet_freeze_client(pc);

  switch(pc->phs.variant[PACKET_FREEZE_CLIENT]) {
    case 100: return receive_packet_freeze_client_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_freeze_client(struct connection *pc)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Sending packet_freeze_client from the client.");
  }
  ensure_valid_variant_packet_freeze_client(pc);

  switch(pc->phs.variant[PACKET_FREEZE_CLIENT]) {
    case 100: return send_packet_freeze_client_100(pc);
    default: die("unknown variant"); return -1;
  }
}

void lsend_packet_freeze_client(struct conn_list *dest)
{
  conn_list_iterate(*dest, pconn) {
    send_packet_freeze_client(pconn);
  } conn_list_iterate_end;
}

static struct packet_thaw_client *receive_packet_thaw_client_100(struct connection *pc, enum packet_type type)
{
  RECEIVE_PACKET_START(packet_thaw_client, real_packet);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_thaw_client_100(struct connection *pc)
{
  SEND_PACKET_START(PACKET_THAW_CLIENT);
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_thaw_client(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_THAW_CLIENT] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_THAW_CLIENT] = variant;
}

struct packet_thaw_client *receive_packet_thaw_client(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Receiving packet_thaw_client at the server.");
  }
  ensure_valid_variant_packet_thaw_client(pc);

  switch(pc->phs.variant[PACKET_THAW_CLIENT]) {
    case 100: return receive_packet_thaw_client_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_thaw_client(struct connection *pc)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Sending packet_thaw_client from the client.");
  }
  ensure_valid_variant_packet_thaw_client(pc);

  switch(pc->phs.variant[PACKET_THAW_CLIENT]) {
    case 100: return send_packet_thaw_client_100(pc);
    default: die("unknown variant"); return -1;
  }
}

void lsend_packet_thaw_client(struct conn_list *dest)
{
  conn_list_iterate(*dest, pconn) {
    send_packet_thaw_client(pconn);
  } conn_list_iterate_end;
}

static struct packet_spaceship_launch *receive_packet_spaceship_launch_100(struct connection *pc, enum packet_type type)
{
  RECEIVE_PACKET_START(packet_spaceship_launch, real_packet);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_spaceship_launch_100(struct connection *pc)
{
  SEND_PACKET_START(PACKET_SPACESHIP_LAUNCH);
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_spaceship_launch(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_SPACESHIP_LAUNCH] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_SPACESHIP_LAUNCH] = variant;
}

struct packet_spaceship_launch *receive_packet_spaceship_launch(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Receiving packet_spaceship_launch at the client.");
  }
  ensure_valid_variant_packet_spaceship_launch(pc);

  switch(pc->phs.variant[PACKET_SPACESHIP_LAUNCH]) {
    case 100: return receive_packet_spaceship_launch_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_spaceship_launch(struct connection *pc)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Sending packet_spaceship_launch from the server.");
  }
  ensure_valid_variant_packet_spaceship_launch(pc);

  switch(pc->phs.variant[PACKET_SPACESHIP_LAUNCH]) {
    case 100: return send_packet_spaceship_launch_100(pc);
    default: die("unknown variant"); return -1;
  }
}

#define hash_packet_spaceship_place_100 hash_const

#define cmp_packet_spaceship_place_100 cmp_const

BV_DEFINE(packet_spaceship_place_100_fields, 2);

static struct packet_spaceship_place *receive_packet_spaceship_place_100(struct connection *pc, enum packet_type type)
{
  packet_spaceship_place_100_fields fields;
  struct packet_spaceship_place *old;
  struct hash_table **hash = &pc->phs.received[type];
  struct packet_spaceship_place *clone;
  RECEIVE_PACKET_START(packet_spaceship_place, real_packet);

  DIO_BV_GET(&din, fields);


  if (!*hash) {
    *hash = hash_new(hash_packet_spaceship_place_100, cmp_packet_spaceship_place_100);
  }
  old = hash_delete_entry(*hash, real_packet);

  if (old) {
    *real_packet = *old;
  } else {
    memset(real_packet, 0, sizeof(*real_packet));
  }

  if (BV_ISSET(fields, 0)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->type = readin;
    }
  }
  if (BV_ISSET(fields, 1)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->num = readin;
    }
  }

  clone = fc_malloc(sizeof(*clone));
  *clone = *real_packet;
  if (old) {
    free(old);
  }
  hash_insert(*hash, clone, clone);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_spaceship_place_100(struct connection *pc, const struct packet_spaceship_place *packet)
{
  const struct packet_spaceship_place *real_packet = packet;
  packet_spaceship_place_100_fields fields;
  struct packet_spaceship_place *old, *clone;
  bool differ, old_from_hash, force_send_of_unchanged = TRUE;
  struct hash_table **hash = &pc->phs.sent[PACKET_SPACESHIP_PLACE];
  int different = 0;
  SEND_PACKET_START(PACKET_SPACESHIP_PLACE);

  if (!*hash) {
    *hash = hash_new(hash_packet_spaceship_place_100, cmp_packet_spaceship_place_100);
  }
  BV_CLR_ALL(fields);

  old = hash_lookup_data(*hash, real_packet);
  old_from_hash = (old != NULL);
  if (!old) {
    old = fc_malloc(sizeof(*old));
    memset(old, 0, sizeof(*old));
    force_send_of_unchanged = TRUE;
  }

  differ = (old->type != real_packet->type);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 0);}

  differ = (old->num != real_packet->num);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 1);}

  if (different == 0 && !force_send_of_unchanged) {
    return 0;
  }

  DIO_BV_PUT(&dout, fields);

  if (BV_ISSET(fields, 0)) {
    dio_put_uint8(&dout, real_packet->type);
  }
  if (BV_ISSET(fields, 1)) {
    dio_put_uint8(&dout, real_packet->num);
  }


  if (old_from_hash) {
    hash_delete_entry(*hash, old);
  }

  clone = old;

  *clone = *real_packet;
  hash_insert(*hash, clone, clone);
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_spaceship_place(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_SPACESHIP_PLACE] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_SPACESHIP_PLACE] = variant;
}

struct packet_spaceship_place *receive_packet_spaceship_place(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Receiving packet_spaceship_place at the client.");
  }
  ensure_valid_variant_packet_spaceship_place(pc);

  switch(pc->phs.variant[PACKET_SPACESHIP_PLACE]) {
    case 100: return receive_packet_spaceship_place_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_spaceship_place(struct connection *pc, const struct packet_spaceship_place *packet)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Sending packet_spaceship_place from the server.");
  }
  ensure_valid_variant_packet_spaceship_place(pc);

  switch(pc->phs.variant[PACKET_SPACESHIP_PLACE]) {
    case 100: return send_packet_spaceship_place_100(pc, packet);
    default: die("unknown variant"); return -1;
  }
}

int dsend_packet_spaceship_place(struct connection *pc, enum spaceship_place_type type, int num)
{
  struct packet_spaceship_place packet, *real_packet = &packet;

  real_packet->type = type;
  real_packet->num = num;
  
  return send_packet_spaceship_place(pc, real_packet);
}

static unsigned int hash_packet_spaceship_info_100(const void *vkey, unsigned int num_buckets)
{
  const struct packet_spaceship_info *key = (const struct packet_spaceship_info *) vkey;

  return ((key->player_num) % num_buckets);
}

static int cmp_packet_spaceship_info_100(const void *vkey1, const void *vkey2)
{
  const struct packet_spaceship_info *key1 = (const struct packet_spaceship_info *) vkey1;
  const struct packet_spaceship_info *key2 = (const struct packet_spaceship_info *) vkey2;
  int diff;

  diff = key1->player_num - key2->player_num;
  if (diff != 0) {
    return diff;
  }

  return 0;
}

BV_DEFINE(packet_spaceship_info_100_fields, 17);

static struct packet_spaceship_info *receive_packet_spaceship_info_100(struct connection *pc, enum packet_type type)
{
  packet_spaceship_info_100_fields fields;
  struct packet_spaceship_info *old;
  struct hash_table **hash = &pc->phs.received[type];
  struct packet_spaceship_info *clone;
  RECEIVE_PACKET_START(packet_spaceship_info, real_packet);

  DIO_BV_GET(&din, fields);
  {
    int readin;
  
    dio_get_uint8(&din, &readin);
    real_packet->player_num = readin;
  }


  if (!*hash) {
    *hash = hash_new(hash_packet_spaceship_info_100, cmp_packet_spaceship_info_100);
  }
  old = hash_delete_entry(*hash, real_packet);

  if (old) {
    *real_packet = *old;
  } else {
    int player_num = real_packet->player_num;

    memset(real_packet, 0, sizeof(*real_packet));

    real_packet->player_num = player_num;
  }

  if (BV_ISSET(fields, 0)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->sship_state = readin;
    }
  }
  if (BV_ISSET(fields, 1)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->structurals = readin;
    }
  }
  if (BV_ISSET(fields, 2)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->components = readin;
    }
  }
  if (BV_ISSET(fields, 3)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->modules = readin;
    }
  }
  if (BV_ISSET(fields, 4)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->fuel = readin;
    }
  }
  if (BV_ISSET(fields, 5)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->propulsion = readin;
    }
  }
  if (BV_ISSET(fields, 6)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->habitation = readin;
    }
  }
  if (BV_ISSET(fields, 7)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->life_support = readin;
    }
  }
  if (BV_ISSET(fields, 8)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->solar_panels = readin;
    }
  }
  if (BV_ISSET(fields, 9)) {
    {
      int readin;
    
      dio_get_sint16(&din, &readin);
      real_packet->launch_year = readin;
    }
  }
  if (BV_ISSET(fields, 10)) {
    {
      int readin;
    
      dio_get_uint32(&din, &readin);
      real_packet->population = readin;
    }
  }
  if (BV_ISSET(fields, 11)) {
    {
      int readin;
    
      dio_get_uint32(&din, &readin);
      real_packet->mass = readin;
    }
  }
  if (BV_ISSET(fields, 12)) {
    dio_get_bit_string(&din, real_packet->structure, sizeof(real_packet->structure));
  }
  if (BV_ISSET(fields, 13)) {
    {
      int tmp;
      
      dio_get_uint32(&din, &tmp);
      real_packet->support_rate = (float)(tmp) / 10000.0;
    }
  }
  if (BV_ISSET(fields, 14)) {
    {
      int tmp;
      
      dio_get_uint32(&din, &tmp);
      real_packet->energy_rate = (float)(tmp) / 10000.0;
    }
  }
  if (BV_ISSET(fields, 15)) {
    {
      int tmp;
      
      dio_get_uint32(&din, &tmp);
      real_packet->success_rate = (float)(tmp) / 10000.0;
    }
  }
  if (BV_ISSET(fields, 16)) {
    {
      int tmp;
      
      dio_get_uint32(&din, &tmp);
      real_packet->travel_time = (float)(tmp) / 10000.0;
    }
  }

  clone = fc_malloc(sizeof(*clone));
  *clone = *real_packet;
  if (old) {
    free(old);
  }
  hash_insert(*hash, clone, clone);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_spaceship_info_100(struct connection *pc, const struct packet_spaceship_info *packet)
{
  const struct packet_spaceship_info *real_packet = packet;
  packet_spaceship_info_100_fields fields;
  struct packet_spaceship_info *old, *clone;
  bool differ, old_from_hash, force_send_of_unchanged = FALSE;
  struct hash_table **hash = &pc->phs.sent[PACKET_SPACESHIP_INFO];
  int different = 0;
  SEND_PACKET_START(PACKET_SPACESHIP_INFO);

  if (!*hash) {
    *hash = hash_new(hash_packet_spaceship_info_100, cmp_packet_spaceship_info_100);
  }
  BV_CLR_ALL(fields);

  old = hash_lookup_data(*hash, real_packet);
  old_from_hash = (old != NULL);
  if (!old) {
    old = fc_malloc(sizeof(*old));
    memset(old, 0, sizeof(*old));
    force_send_of_unchanged = TRUE;
  }

  differ = (old->sship_state != real_packet->sship_state);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 0);}

  differ = (old->structurals != real_packet->structurals);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 1);}

  differ = (old->components != real_packet->components);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 2);}

  differ = (old->modules != real_packet->modules);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 3);}

  differ = (old->fuel != real_packet->fuel);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 4);}

  differ = (old->propulsion != real_packet->propulsion);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 5);}

  differ = (old->habitation != real_packet->habitation);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 6);}

  differ = (old->life_support != real_packet->life_support);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 7);}

  differ = (old->solar_panels != real_packet->solar_panels);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 8);}

  differ = (old->launch_year != real_packet->launch_year);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 9);}

  differ = (old->population != real_packet->population);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 10);}

  differ = (old->mass != real_packet->mass);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 11);}

  differ = (strcmp(old->structure, real_packet->structure) != 0);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 12);}

  differ = (old->support_rate != real_packet->support_rate);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 13);}

  differ = (old->energy_rate != real_packet->energy_rate);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 14);}

  differ = (old->success_rate != real_packet->success_rate);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 15);}

  differ = (old->travel_time != real_packet->travel_time);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 16);}

  if (different == 0 && !force_send_of_unchanged) {
    return 0;
  }

  DIO_BV_PUT(&dout, fields);
  dio_put_uint8(&dout, real_packet->player_num);

  if (BV_ISSET(fields, 0)) {
    dio_put_uint8(&dout, real_packet->sship_state);
  }
  if (BV_ISSET(fields, 1)) {
    dio_put_uint8(&dout, real_packet->structurals);
  }
  if (BV_ISSET(fields, 2)) {
    dio_put_uint8(&dout, real_packet->components);
  }
  if (BV_ISSET(fields, 3)) {
    dio_put_uint8(&dout, real_packet->modules);
  }
  if (BV_ISSET(fields, 4)) {
    dio_put_uint8(&dout, real_packet->fuel);
  }
  if (BV_ISSET(fields, 5)) {
    dio_put_uint8(&dout, real_packet->propulsion);
  }
  if (BV_ISSET(fields, 6)) {
    dio_put_uint8(&dout, real_packet->habitation);
  }
  if (BV_ISSET(fields, 7)) {
    dio_put_uint8(&dout, real_packet->life_support);
  }
  if (BV_ISSET(fields, 8)) {
    dio_put_uint8(&dout, real_packet->solar_panels);
  }
  if (BV_ISSET(fields, 9)) {
    dio_put_sint16(&dout, real_packet->launch_year);
  }
  if (BV_ISSET(fields, 10)) {
    dio_put_uint32(&dout, real_packet->population);
  }
  if (BV_ISSET(fields, 11)) {
    dio_put_uint32(&dout, real_packet->mass);
  }
  if (BV_ISSET(fields, 12)) {
    dio_put_bit_string(&dout, real_packet->structure);
  }
  if (BV_ISSET(fields, 13)) {
    dio_put_uint32(&dout, (int)(real_packet->support_rate * 10000));
  }
  if (BV_ISSET(fields, 14)) {
    dio_put_uint32(&dout, (int)(real_packet->energy_rate * 10000));
  }
  if (BV_ISSET(fields, 15)) {
    dio_put_uint32(&dout, (int)(real_packet->success_rate * 10000));
  }
  if (BV_ISSET(fields, 16)) {
    dio_put_uint32(&dout, (int)(real_packet->travel_time * 10000));
  }


  if (old_from_hash) {
    hash_delete_entry(*hash, old);
  }

  clone = old;

  *clone = *real_packet;
  hash_insert(*hash, clone, clone);
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_spaceship_info(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_SPACESHIP_INFO] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_SPACESHIP_INFO] = variant;
}

struct packet_spaceship_info *receive_packet_spaceship_info(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Receiving packet_spaceship_info at the server.");
  }
  ensure_valid_variant_packet_spaceship_info(pc);

  switch(pc->phs.variant[PACKET_SPACESHIP_INFO]) {
    case 100: return receive_packet_spaceship_info_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_spaceship_info(struct connection *pc, const struct packet_spaceship_info *packet)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Sending packet_spaceship_info from the client.");
  }
  ensure_valid_variant_packet_spaceship_info(pc);

  switch(pc->phs.variant[PACKET_SPACESHIP_INFO]) {
    case 100: return send_packet_spaceship_info_100(pc, packet);
    default: die("unknown variant"); return -1;
  }
}

void lsend_packet_spaceship_info(struct conn_list *dest, const struct packet_spaceship_info *packet)
{
  conn_list_iterate(*dest, pconn) {
    send_packet_spaceship_info(pconn, packet);
  } conn_list_iterate_end;
}

#define hash_packet_ruleset_unit_100 hash_const

#define cmp_packet_ruleset_unit_100 cmp_const

BV_DEFINE(packet_ruleset_unit_100_fields, 36);

static struct packet_ruleset_unit *receive_packet_ruleset_unit_100(struct connection *pc, enum packet_type type)
{
  packet_ruleset_unit_100_fields fields;
  struct packet_ruleset_unit *old;
  struct hash_table **hash = &pc->phs.received[type];
  struct packet_ruleset_unit *clone;
  RECEIVE_PACKET_START(packet_ruleset_unit, real_packet);

  DIO_BV_GET(&din, fields);


  if (!*hash) {
    *hash = hash_new(hash_packet_ruleset_unit_100, cmp_packet_ruleset_unit_100);
  }
  old = hash_delete_entry(*hash, real_packet);

  if (old) {
    *real_packet = *old;
  } else {
    memset(real_packet, 0, sizeof(*real_packet));
  }

  if (BV_ISSET(fields, 0)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->id = readin;
    }
  }
  if (BV_ISSET(fields, 1)) {
    dio_get_string(&din, real_packet->name, sizeof(real_packet->name));
  }
  if (BV_ISSET(fields, 2)) {
    dio_get_string(&din, real_packet->graphic_str, sizeof(real_packet->graphic_str));
  }
  if (BV_ISSET(fields, 3)) {
    dio_get_string(&din, real_packet->graphic_alt, sizeof(real_packet->graphic_alt));
  }
  if (BV_ISSET(fields, 4)) {
    dio_get_string(&din, real_packet->sound_move, sizeof(real_packet->sound_move));
  }
  if (BV_ISSET(fields, 5)) {
    dio_get_string(&din, real_packet->sound_move_alt, sizeof(real_packet->sound_move_alt));
  }
  if (BV_ISSET(fields, 6)) {
    dio_get_string(&din, real_packet->sound_fight, sizeof(real_packet->sound_fight));
  }
  if (BV_ISSET(fields, 7)) {
    dio_get_string(&din, real_packet->sound_fight_alt, sizeof(real_packet->sound_fight_alt));
  }
  if (BV_ISSET(fields, 8)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->move_type = readin;
    }
  }
  if (BV_ISSET(fields, 9)) {
    {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->build_cost = readin;
    }
  }
  if (BV_ISSET(fields, 10)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->pop_cost = readin;
    }
  }
  if (BV_ISSET(fields, 11)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->attack_strength = readin;
    }
  }
  if (BV_ISSET(fields, 12)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->defense_strength = readin;
    }
  }
  if (BV_ISSET(fields, 13)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->move_rate = readin;
    }
  }
  if (BV_ISSET(fields, 14)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->tech_requirement = readin;
    }
  }
  if (BV_ISSET(fields, 15)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->impr_requirement = readin;
    }
  }
  if (BV_ISSET(fields, 16)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->vision_range = readin;
    }
  }
  if (BV_ISSET(fields, 17)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->transport_capacity = readin;
    }
  }
  if (BV_ISSET(fields, 18)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->hp = readin;
    }
  }
  if (BV_ISSET(fields, 19)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->firepower = readin;
    }
  }
  if (BV_ISSET(fields, 20)) {
    {
      int readin;
    
      dio_get_sint8(&din, &readin);
      real_packet->obsoleted_by = readin;
    }
  }
  if (BV_ISSET(fields, 21)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->fuel = readin;
    }
  }
  if (BV_ISSET(fields, 22)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->happy_cost = readin;
    }
  }
  if (BV_ISSET(fields, 23)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->shield_cost = readin;
    }
  }
  if (BV_ISSET(fields, 24)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->food_cost = readin;
    }
  }
  if (BV_ISSET(fields, 25)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->gold_cost = readin;
    }
  }
  if (BV_ISSET(fields, 26)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->paratroopers_range = readin;
    }
  }
  if (BV_ISSET(fields, 27)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->paratroopers_mr_req = readin;
    }
  }
  if (BV_ISSET(fields, 28)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->paratroopers_mr_sub = readin;
    }
  }
  if (BV_ISSET(fields, 29)) {
    
    {
      int i;
    
      for (i = 0; i < MAX_VET_LEVELS; i++) {
        dio_get_string(&din, real_packet->veteran_name[i], sizeof(real_packet->veteran_name[i]));
      }
    }
  }
  if (BV_ISSET(fields, 30)) {
    
    {
      int i;
    
      for (i = 0; i < MAX_VET_LEVELS; i++) {
        int tmp;
    
        dio_get_uint32(&din, &tmp);
        real_packet->power_fact[i] = (float)(tmp) / 10000.0;
      }
    }
  }
  if (BV_ISSET(fields, 31)) {
    
    {
      int i;
    
      for (i = 0; i < MAX_VET_LEVELS; i++) {
        {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->move_bonus[i] = readin;
    }
      }
    }
  }
  if (BV_ISSET(fields, 32)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->bombard_rate = readin;
    }
  }
  if (BV_ISSET(fields, 33)) {
    dio_get_string(&din, real_packet->helptext, sizeof(real_packet->helptext));
  }
  if (BV_ISSET(fields, 34)) {
    DIO_BV_GET(&din, real_packet->flags);
  }
  if (BV_ISSET(fields, 35)) {
    DIO_BV_GET(&din, real_packet->roles);
  }

  clone = fc_malloc(sizeof(*clone));
  *clone = *real_packet;
  if (old) {
    free(old);
  }
  hash_insert(*hash, clone, clone);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_ruleset_unit_100(struct connection *pc, const struct packet_ruleset_unit *packet)
{
  const struct packet_ruleset_unit *real_packet = packet;
  packet_ruleset_unit_100_fields fields;
  struct packet_ruleset_unit *old, *clone;
  bool differ, old_from_hash, force_send_of_unchanged = TRUE;
  struct hash_table **hash = &pc->phs.sent[PACKET_RULESET_UNIT];
  int different = 0;
  SEND_PACKET_START(PACKET_RULESET_UNIT);

  if (!*hash) {
    *hash = hash_new(hash_packet_ruleset_unit_100, cmp_packet_ruleset_unit_100);
  }
  BV_CLR_ALL(fields);

  old = hash_lookup_data(*hash, real_packet);
  old_from_hash = (old != NULL);
  if (!old) {
    old = fc_malloc(sizeof(*old));
    memset(old, 0, sizeof(*old));
    force_send_of_unchanged = TRUE;
  }

  differ = (old->id != real_packet->id);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 0);}

  differ = (strcmp(old->name, real_packet->name) != 0);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 1);}

  differ = (strcmp(old->graphic_str, real_packet->graphic_str) != 0);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 2);}

  differ = (strcmp(old->graphic_alt, real_packet->graphic_alt) != 0);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 3);}

  differ = (strcmp(old->sound_move, real_packet->sound_move) != 0);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 4);}

  differ = (strcmp(old->sound_move_alt, real_packet->sound_move_alt) != 0);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 5);}

  differ = (strcmp(old->sound_fight, real_packet->sound_fight) != 0);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 6);}

  differ = (strcmp(old->sound_fight_alt, real_packet->sound_fight_alt) != 0);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 7);}

  differ = (old->move_type != real_packet->move_type);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 8);}

  differ = (old->build_cost != real_packet->build_cost);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 9);}

  differ = (old->pop_cost != real_packet->pop_cost);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 10);}

  differ = (old->attack_strength != real_packet->attack_strength);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 11);}

  differ = (old->defense_strength != real_packet->defense_strength);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 12);}

  differ = (old->move_rate != real_packet->move_rate);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 13);}

  differ = (old->tech_requirement != real_packet->tech_requirement);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 14);}

  differ = (old->impr_requirement != real_packet->impr_requirement);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 15);}

  differ = (old->vision_range != real_packet->vision_range);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 16);}

  differ = (old->transport_capacity != real_packet->transport_capacity);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 17);}

  differ = (old->hp != real_packet->hp);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 18);}

  differ = (old->firepower != real_packet->firepower);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 19);}

  differ = (old->obsoleted_by != real_packet->obsoleted_by);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 20);}

  differ = (old->fuel != real_packet->fuel);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 21);}

  differ = (old->happy_cost != real_packet->happy_cost);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 22);}

  differ = (old->shield_cost != real_packet->shield_cost);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 23);}

  differ = (old->food_cost != real_packet->food_cost);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 24);}

  differ = (old->gold_cost != real_packet->gold_cost);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 25);}

  differ = (old->paratroopers_range != real_packet->paratroopers_range);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 26);}

  differ = (old->paratroopers_mr_req != real_packet->paratroopers_mr_req);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 27);}

  differ = (old->paratroopers_mr_sub != real_packet->paratroopers_mr_sub);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 28);}


    {
      differ = (MAX_VET_LEVELS != MAX_VET_LEVELS);
      if(!differ) {
        int i;
        for (i = 0; i < MAX_VET_LEVELS; i++) {
          if (strcmp(old->veteran_name[i], real_packet->veteran_name[i]) != 0) {
            differ = TRUE;
            break;
          }
        }
      }
    }
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 29);}


    {
      differ = (MAX_VET_LEVELS != MAX_VET_LEVELS);
      if(!differ) {
        int i;
        for (i = 0; i < MAX_VET_LEVELS; i++) {
          if (old->power_fact[i] != real_packet->power_fact[i]) {
            differ = TRUE;
            break;
          }
        }
      }
    }
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 30);}


    {
      differ = (MAX_VET_LEVELS != MAX_VET_LEVELS);
      if(!differ) {
        int i;
        for (i = 0; i < MAX_VET_LEVELS; i++) {
          if (old->move_bonus[i] != real_packet->move_bonus[i]) {
            differ = TRUE;
            break;
          }
        }
      }
    }
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 31);}

  differ = (old->bombard_rate != real_packet->bombard_rate);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 32);}

  differ = (strcmp(old->helptext, real_packet->helptext) != 0);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 33);}

  differ = !BV_ARE_EQUAL(old->flags, real_packet->flags);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 34);}

  differ = !BV_ARE_EQUAL(old->roles, real_packet->roles);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 35);}

  if (different == 0 && !force_send_of_unchanged) {
    return 0;
  }

  DIO_BV_PUT(&dout, fields);

  if (BV_ISSET(fields, 0)) {
    dio_put_uint8(&dout, real_packet->id);
  }
  if (BV_ISSET(fields, 1)) {
    dio_put_string(&dout, real_packet->name);
  }
  if (BV_ISSET(fields, 2)) {
    dio_put_string(&dout, real_packet->graphic_str);
  }
  if (BV_ISSET(fields, 3)) {
    dio_put_string(&dout, real_packet->graphic_alt);
  }
  if (BV_ISSET(fields, 4)) {
    dio_put_string(&dout, real_packet->sound_move);
  }
  if (BV_ISSET(fields, 5)) {
    dio_put_string(&dout, real_packet->sound_move_alt);
  }
  if (BV_ISSET(fields, 6)) {
    dio_put_string(&dout, real_packet->sound_fight);
  }
  if (BV_ISSET(fields, 7)) {
    dio_put_string(&dout, real_packet->sound_fight_alt);
  }
  if (BV_ISSET(fields, 8)) {
    dio_put_uint8(&dout, real_packet->move_type);
  }
  if (BV_ISSET(fields, 9)) {
    dio_put_uint16(&dout, real_packet->build_cost);
  }
  if (BV_ISSET(fields, 10)) {
    dio_put_uint8(&dout, real_packet->pop_cost);
  }
  if (BV_ISSET(fields, 11)) {
    dio_put_uint8(&dout, real_packet->attack_strength);
  }
  if (BV_ISSET(fields, 12)) {
    dio_put_uint8(&dout, real_packet->defense_strength);
  }
  if (BV_ISSET(fields, 13)) {
    dio_put_uint8(&dout, real_packet->move_rate);
  }
  if (BV_ISSET(fields, 14)) {
    dio_put_uint8(&dout, real_packet->tech_requirement);
  }
  if (BV_ISSET(fields, 15)) {
    dio_put_uint8(&dout, real_packet->impr_requirement);
  }
  if (BV_ISSET(fields, 16)) {
    dio_put_uint8(&dout, real_packet->vision_range);
  }
  if (BV_ISSET(fields, 17)) {
    dio_put_uint8(&dout, real_packet->transport_capacity);
  }
  if (BV_ISSET(fields, 18)) {
    dio_put_uint8(&dout, real_packet->hp);
  }
  if (BV_ISSET(fields, 19)) {
    dio_put_uint8(&dout, real_packet->firepower);
  }
  if (BV_ISSET(fields, 20)) {
    dio_put_sint8(&dout, real_packet->obsoleted_by);
  }
  if (BV_ISSET(fields, 21)) {
    dio_put_uint8(&dout, real_packet->fuel);
  }
  if (BV_ISSET(fields, 22)) {
    dio_put_uint8(&dout, real_packet->happy_cost);
  }
  if (BV_ISSET(fields, 23)) {
    dio_put_uint8(&dout, real_packet->shield_cost);
  }
  if (BV_ISSET(fields, 24)) {
    dio_put_uint8(&dout, real_packet->food_cost);
  }
  if (BV_ISSET(fields, 25)) {
    dio_put_uint8(&dout, real_packet->gold_cost);
  }
  if (BV_ISSET(fields, 26)) {
    dio_put_uint8(&dout, real_packet->paratroopers_range);
  }
  if (BV_ISSET(fields, 27)) {
    dio_put_uint8(&dout, real_packet->paratroopers_mr_req);
  }
  if (BV_ISSET(fields, 28)) {
    dio_put_uint8(&dout, real_packet->paratroopers_mr_sub);
  }
  if (BV_ISSET(fields, 29)) {
  
    {
      int i;

      for (i = 0; i < MAX_VET_LEVELS; i++) {
        dio_put_string(&dout, real_packet->veteran_name[i]);
      }
    } 
  }
  if (BV_ISSET(fields, 30)) {
  
    {
      int i;

      for (i = 0; i < MAX_VET_LEVELS; i++) {
          dio_put_uint32(&dout, (int)(real_packet->power_fact[i] * 10000));
      }
    } 
  }
  if (BV_ISSET(fields, 31)) {
  
    {
      int i;

      for (i = 0; i < MAX_VET_LEVELS; i++) {
        dio_put_uint8(&dout, real_packet->move_bonus[i]);
      }
    } 
  }
  if (BV_ISSET(fields, 32)) {
    dio_put_uint8(&dout, real_packet->bombard_rate);
  }
  if (BV_ISSET(fields, 33)) {
    dio_put_string(&dout, real_packet->helptext);
  }
  if (BV_ISSET(fields, 34)) {
  DIO_BV_PUT(&dout, packet->flags);
  }
  if (BV_ISSET(fields, 35)) {
  DIO_BV_PUT(&dout, packet->roles);
  }


  if (old_from_hash) {
    hash_delete_entry(*hash, old);
  }

  clone = old;

  *clone = *real_packet;
  hash_insert(*hash, clone, clone);
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_ruleset_unit(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_RULESET_UNIT] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_RULESET_UNIT] = variant;
}

struct packet_ruleset_unit *receive_packet_ruleset_unit(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Receiving packet_ruleset_unit at the server.");
  }
  ensure_valid_variant_packet_ruleset_unit(pc);

  switch(pc->phs.variant[PACKET_RULESET_UNIT]) {
    case 100: return receive_packet_ruleset_unit_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_ruleset_unit(struct connection *pc, const struct packet_ruleset_unit *packet)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Sending packet_ruleset_unit from the client.");
  }
  ensure_valid_variant_packet_ruleset_unit(pc);

  switch(pc->phs.variant[PACKET_RULESET_UNIT]) {
    case 100: return send_packet_ruleset_unit_100(pc, packet);
    default: die("unknown variant"); return -1;
  }
}

void lsend_packet_ruleset_unit(struct conn_list *dest, const struct packet_ruleset_unit *packet)
{
  conn_list_iterate(*dest, pconn) {
    send_packet_ruleset_unit(pconn, packet);
  } conn_list_iterate_end;
}

#define hash_packet_ruleset_game_100 hash_const

#define cmp_packet_ruleset_game_100 cmp_const

BV_DEFINE(packet_ruleset_game_100_fields, 26);

static struct packet_ruleset_game *receive_packet_ruleset_game_100(struct connection *pc, enum packet_type type)
{
  packet_ruleset_game_100_fields fields;
  struct packet_ruleset_game *old;
  struct hash_table **hash = &pc->phs.received[type];
  struct packet_ruleset_game *clone;
  RECEIVE_PACKET_START(packet_ruleset_game, real_packet);

  DIO_BV_GET(&din, fields);


  if (!*hash) {
    *hash = hash_new(hash_packet_ruleset_game_100, cmp_packet_ruleset_game_100);
  }
  old = hash_delete_entry(*hash, real_packet);

  if (old) {
    *real_packet = *old;
  } else {
    memset(real_packet, 0, sizeof(*real_packet));
  }

  if (BV_ISSET(fields, 0)) {
    
    {
      int i;
    
      for (i = 0; i < SP_COUNT; i++) {
        dio_get_string(&din, real_packet->specialist_name[i], sizeof(real_packet->specialist_name[i]));
      }
    }
  }
  if (BV_ISSET(fields, 1)) {
    
    {
      int i;
    
      for (i = 0; i < SP_COUNT; i++) {
        {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->specialist_min_size[i] = readin;
    }
      }
    }
  }
  if (BV_ISSET(fields, 2)) {
    
    {
      int i;
    
      for (i = 0; i < SP_COUNT; i++) {
        {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->specialist_bonus[i] = readin;
    }
      }
    }
  }
  real_packet->changable_tax = BV_ISSET(fields, 3);
  if (BV_ISSET(fields, 4)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->forced_science = readin;
    }
  }
  if (BV_ISSET(fields, 5)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->forced_luxury = readin;
    }
  }
  if (BV_ISSET(fields, 6)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->forced_gold = readin;
    }
  }
  if (BV_ISSET(fields, 7)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->min_city_center_food = readin;
    }
  }
  if (BV_ISSET(fields, 8)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->min_city_center_shield = readin;
    }
  }
  if (BV_ISSET(fields, 9)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->min_city_center_trade = readin;
    }
  }
  if (BV_ISSET(fields, 10)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->min_dist_bw_cities = readin;
    }
  }
  if (BV_ISSET(fields, 11)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->init_vis_radius_sq = readin;
    }
  }
  if (BV_ISSET(fields, 12)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->hut_overflight = readin;
    }
  }
  real_packet->pillage_select = BV_ISSET(fields, 13);
  if (BV_ISSET(fields, 14)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->nuke_contamination = readin;
    }
  }
  if (BV_ISSET(fields, 15)) {
    
    {
      int i;
    
      for (i = 0; i < MAX_GRANARY_INIS; i++) {
        {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->granary_food_ini[i] = readin;
    }
      }
    }
  }
  if (BV_ISSET(fields, 16)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->granary_num_inis = readin;
    }
  }
  if (BV_ISSET(fields, 17)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->granary_food_inc = readin;
    }
  }
  if (BV_ISSET(fields, 18)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->tech_cost_style = readin;
    }
  }
  if (BV_ISSET(fields, 19)) {
    {
      int readin;
    
      dio_get_sint16(&din, &readin);
      real_packet->tech_cost_double_year = readin;
    }
  }
  if (BV_ISSET(fields, 20)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->tech_leakage = readin;
    }
  }
  if (BV_ISSET(fields, 21)) {
    dio_get_tech_list(&din, real_packet->global_init_techs);
  }
  real_packet->killstack = BV_ISSET(fields, 22);
  if (BV_ISSET(fields, 23)) {
    
    {
      int i;
    
      for (i = 0; i < MAX_VET_LEVELS; i++) {
        {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->trireme_loss_chance[i] = readin;
    }
      }
    }
  }
  if (BV_ISSET(fields, 24)) {
    
    {
      int i;
    
      for (i = 0; i < MAX_VET_LEVELS; i++) {
        {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->work_veteran_chance[i] = readin;
    }
      }
    }
  }
  if (BV_ISSET(fields, 25)) {
    
    {
      int i;
    
      for (i = 0; i < MAX_VET_LEVELS; i++) {
        {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->veteran_chance[i] = readin;
    }
      }
    }
  }

  clone = fc_malloc(sizeof(*clone));
  *clone = *real_packet;
  if (old) {
    free(old);
  }
  hash_insert(*hash, clone, clone);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_ruleset_game_100(struct connection *pc, const struct packet_ruleset_game *packet)
{
  const struct packet_ruleset_game *real_packet = packet;
  packet_ruleset_game_100_fields fields;
  struct packet_ruleset_game *old, *clone;
  bool differ, old_from_hash, force_send_of_unchanged = TRUE;
  struct hash_table **hash = &pc->phs.sent[PACKET_RULESET_GAME];
  int different = 0;
  SEND_PACKET_START(PACKET_RULESET_GAME);

  if (!*hash) {
    *hash = hash_new(hash_packet_ruleset_game_100, cmp_packet_ruleset_game_100);
  }
  BV_CLR_ALL(fields);

  old = hash_lookup_data(*hash, real_packet);
  old_from_hash = (old != NULL);
  if (!old) {
    old = fc_malloc(sizeof(*old));
    memset(old, 0, sizeof(*old));
    force_send_of_unchanged = TRUE;
  }


    {
      differ = (SP_COUNT != SP_COUNT);
      if(!differ) {
        int i;
        for (i = 0; i < SP_COUNT; i++) {
          if (strcmp(old->specialist_name[i], real_packet->specialist_name[i]) != 0) {
            differ = TRUE;
            break;
          }
        }
      }
    }
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 0);}


    {
      differ = (SP_COUNT != SP_COUNT);
      if(!differ) {
        int i;
        for (i = 0; i < SP_COUNT; i++) {
          if (old->specialist_min_size[i] != real_packet->specialist_min_size[i]) {
            differ = TRUE;
            break;
          }
        }
      }
    }
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 1);}


    {
      differ = (SP_COUNT != SP_COUNT);
      if(!differ) {
        int i;
        for (i = 0; i < SP_COUNT; i++) {
          if (old->specialist_bonus[i] != real_packet->specialist_bonus[i]) {
            differ = TRUE;
            break;
          }
        }
      }
    }
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 2);}

  differ = (old->changable_tax != real_packet->changable_tax);
  if(differ) {different++;}
  if(packet->changable_tax) {BV_SET(fields, 3);}

  differ = (old->forced_science != real_packet->forced_science);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 4);}

  differ = (old->forced_luxury != real_packet->forced_luxury);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 5);}

  differ = (old->forced_gold != real_packet->forced_gold);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 6);}

  differ = (old->min_city_center_food != real_packet->min_city_center_food);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 7);}

  differ = (old->min_city_center_shield != real_packet->min_city_center_shield);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 8);}

  differ = (old->min_city_center_trade != real_packet->min_city_center_trade);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 9);}

  differ = (old->min_dist_bw_cities != real_packet->min_dist_bw_cities);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 10);}

  differ = (old->init_vis_radius_sq != real_packet->init_vis_radius_sq);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 11);}

  differ = (old->hut_overflight != real_packet->hut_overflight);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 12);}

  differ = (old->pillage_select != real_packet->pillage_select);
  if(differ) {different++;}
  if(packet->pillage_select) {BV_SET(fields, 13);}

  differ = (old->nuke_contamination != real_packet->nuke_contamination);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 14);}


    {
      differ = (MAX_GRANARY_INIS != MAX_GRANARY_INIS);
      if(!differ) {
        int i;
        for (i = 0; i < MAX_GRANARY_INIS; i++) {
          if (old->granary_food_ini[i] != real_packet->granary_food_ini[i]) {
            differ = TRUE;
            break;
          }
        }
      }
    }
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 15);}

  differ = (old->granary_num_inis != real_packet->granary_num_inis);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 16);}

  differ = (old->granary_food_inc != real_packet->granary_food_inc);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 17);}

  differ = (old->tech_cost_style != real_packet->tech_cost_style);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 18);}

  differ = (old->tech_cost_double_year != real_packet->tech_cost_double_year);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 19);}

  differ = (old->tech_leakage != real_packet->tech_leakage);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 20);}


    {
      differ = (MAX_NUM_TECH_LIST != MAX_NUM_TECH_LIST);
      if(!differ) {
        int i;
        for (i = 0; i < MAX_NUM_TECH_LIST; i++) {
          if (old->global_init_techs[i] != real_packet->global_init_techs[i]) {
            differ = TRUE;
            break;
          }
        }
      }
    }
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 21);}

  differ = (old->killstack != real_packet->killstack);
  if(differ) {different++;}
  if(packet->killstack) {BV_SET(fields, 22);}


    {
      differ = (MAX_VET_LEVELS != MAX_VET_LEVELS);
      if(!differ) {
        int i;
        for (i = 0; i < MAX_VET_LEVELS; i++) {
          if (old->trireme_loss_chance[i] != real_packet->trireme_loss_chance[i]) {
            differ = TRUE;
            break;
          }
        }
      }
    }
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 23);}


    {
      differ = (MAX_VET_LEVELS != MAX_VET_LEVELS);
      if(!differ) {
        int i;
        for (i = 0; i < MAX_VET_LEVELS; i++) {
          if (old->work_veteran_chance[i] != real_packet->work_veteran_chance[i]) {
            differ = TRUE;
            break;
          }
        }
      }
    }
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 24);}


    {
      differ = (MAX_VET_LEVELS != MAX_VET_LEVELS);
      if(!differ) {
        int i;
        for (i = 0; i < MAX_VET_LEVELS; i++) {
          if (old->veteran_chance[i] != real_packet->veteran_chance[i]) {
            differ = TRUE;
            break;
          }
        }
      }
    }
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 25);}

  if (different == 0 && !force_send_of_unchanged) {
    return 0;
  }

  DIO_BV_PUT(&dout, fields);

  if (BV_ISSET(fields, 0)) {
  
    {
      int i;

      for (i = 0; i < SP_COUNT; i++) {
        dio_put_string(&dout, real_packet->specialist_name[i]);
      }
    } 
  }
  if (BV_ISSET(fields, 1)) {
  
    {
      int i;

      for (i = 0; i < SP_COUNT; i++) {
        dio_put_uint8(&dout, real_packet->specialist_min_size[i]);
      }
    } 
  }
  if (BV_ISSET(fields, 2)) {
  
    {
      int i;

      for (i = 0; i < SP_COUNT; i++) {
        dio_put_uint8(&dout, real_packet->specialist_bonus[i]);
      }
    } 
  }
  /* field 3 is folded into the header */
  if (BV_ISSET(fields, 4)) {
    dio_put_uint8(&dout, real_packet->forced_science);
  }
  if (BV_ISSET(fields, 5)) {
    dio_put_uint8(&dout, real_packet->forced_luxury);
  }
  if (BV_ISSET(fields, 6)) {
    dio_put_uint8(&dout, real_packet->forced_gold);
  }
  if (BV_ISSET(fields, 7)) {
    dio_put_uint8(&dout, real_packet->min_city_center_food);
  }
  if (BV_ISSET(fields, 8)) {
    dio_put_uint8(&dout, real_packet->min_city_center_shield);
  }
  if (BV_ISSET(fields, 9)) {
    dio_put_uint8(&dout, real_packet->min_city_center_trade);
  }
  if (BV_ISSET(fields, 10)) {
    dio_put_uint8(&dout, real_packet->min_dist_bw_cities);
  }
  if (BV_ISSET(fields, 11)) {
    dio_put_uint8(&dout, real_packet->init_vis_radius_sq);
  }
  if (BV_ISSET(fields, 12)) {
    dio_put_uint8(&dout, real_packet->hut_overflight);
  }
  /* field 13 is folded into the header */
  if (BV_ISSET(fields, 14)) {
    dio_put_uint8(&dout, real_packet->nuke_contamination);
  }
  if (BV_ISSET(fields, 15)) {
  
    {
      int i;

      for (i = 0; i < MAX_GRANARY_INIS; i++) {
        dio_put_uint8(&dout, real_packet->granary_food_ini[i]);
      }
    } 
  }
  if (BV_ISSET(fields, 16)) {
    dio_put_uint8(&dout, real_packet->granary_num_inis);
  }
  if (BV_ISSET(fields, 17)) {
    dio_put_uint8(&dout, real_packet->granary_food_inc);
  }
  if (BV_ISSET(fields, 18)) {
    dio_put_uint8(&dout, real_packet->tech_cost_style);
  }
  if (BV_ISSET(fields, 19)) {
    dio_put_sint16(&dout, real_packet->tech_cost_double_year);
  }
  if (BV_ISSET(fields, 20)) {
    dio_put_uint8(&dout, real_packet->tech_leakage);
  }
  if (BV_ISSET(fields, 21)) {
    dio_put_tech_list(&dout, real_packet->global_init_techs);
  }
  /* field 22 is folded into the header */
  if (BV_ISSET(fields, 23)) {
  
    {
      int i;

      for (i = 0; i < MAX_VET_LEVELS; i++) {
        dio_put_uint8(&dout, real_packet->trireme_loss_chance[i]);
      }
    } 
  }
  if (BV_ISSET(fields, 24)) {
  
    {
      int i;

      for (i = 0; i < MAX_VET_LEVELS; i++) {
        dio_put_uint8(&dout, real_packet->work_veteran_chance[i]);
      }
    } 
  }
  if (BV_ISSET(fields, 25)) {
  
    {
      int i;

      for (i = 0; i < MAX_VET_LEVELS; i++) {
        dio_put_uint8(&dout, real_packet->veteran_chance[i]);
      }
    } 
  }


  if (old_from_hash) {
    hash_delete_entry(*hash, old);
  }

  clone = old;

  *clone = *real_packet;
  hash_insert(*hash, clone, clone);
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_ruleset_game(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_RULESET_GAME] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_RULESET_GAME] = variant;
}

struct packet_ruleset_game *receive_packet_ruleset_game(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Receiving packet_ruleset_game at the server.");
  }
  ensure_valid_variant_packet_ruleset_game(pc);

  switch(pc->phs.variant[PACKET_RULESET_GAME]) {
    case 100: return receive_packet_ruleset_game_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_ruleset_game(struct connection *pc, const struct packet_ruleset_game *packet)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Sending packet_ruleset_game from the client.");
  }
  ensure_valid_variant_packet_ruleset_game(pc);

  switch(pc->phs.variant[PACKET_RULESET_GAME]) {
    case 100: return send_packet_ruleset_game_100(pc, packet);
    default: die("unknown variant"); return -1;
  }
}

void lsend_packet_ruleset_game(struct conn_list *dest, const struct packet_ruleset_game *packet)
{
  conn_list_iterate(*dest, pconn) {
    send_packet_ruleset_game(pconn, packet);
  } conn_list_iterate_end;
}

#define hash_packet_ruleset_government_ruler_title_100 hash_const

#define cmp_packet_ruleset_government_ruler_title_100 cmp_const

BV_DEFINE(packet_ruleset_government_ruler_title_100_fields, 5);

static struct packet_ruleset_government_ruler_title *receive_packet_ruleset_government_ruler_title_100(struct connection *pc, enum packet_type type)
{
  packet_ruleset_government_ruler_title_100_fields fields;
  struct packet_ruleset_government_ruler_title *old;
  struct hash_table **hash = &pc->phs.received[type];
  struct packet_ruleset_government_ruler_title *clone;
  RECEIVE_PACKET_START(packet_ruleset_government_ruler_title, real_packet);

  DIO_BV_GET(&din, fields);


  if (!*hash) {
    *hash = hash_new(hash_packet_ruleset_government_ruler_title_100, cmp_packet_ruleset_government_ruler_title_100);
  }
  old = hash_delete_entry(*hash, real_packet);

  if (old) {
    *real_packet = *old;
  } else {
    memset(real_packet, 0, sizeof(*real_packet));
  }

  if (BV_ISSET(fields, 0)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->gov = readin;
    }
  }
  if (BV_ISSET(fields, 1)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->id = readin;
    }
  }
  if (BV_ISSET(fields, 2)) {
    {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->nation = readin;
    }
  }
  if (BV_ISSET(fields, 3)) {
    dio_get_string(&din, real_packet->male_title, sizeof(real_packet->male_title));
  }
  if (BV_ISSET(fields, 4)) {
    dio_get_string(&din, real_packet->female_title, sizeof(real_packet->female_title));
  }

  clone = fc_malloc(sizeof(*clone));
  *clone = *real_packet;
  if (old) {
    free(old);
  }
  hash_insert(*hash, clone, clone);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_ruleset_government_ruler_title_100(struct connection *pc, const struct packet_ruleset_government_ruler_title *packet)
{
  const struct packet_ruleset_government_ruler_title *real_packet = packet;
  packet_ruleset_government_ruler_title_100_fields fields;
  struct packet_ruleset_government_ruler_title *old, *clone;
  bool differ, old_from_hash, force_send_of_unchanged = TRUE;
  struct hash_table **hash = &pc->phs.sent[PACKET_RULESET_GOVERNMENT_RULER_TITLE];
  int different = 0;
  SEND_PACKET_START(PACKET_RULESET_GOVERNMENT_RULER_TITLE);

  if (!*hash) {
    *hash = hash_new(hash_packet_ruleset_government_ruler_title_100, cmp_packet_ruleset_government_ruler_title_100);
  }
  BV_CLR_ALL(fields);

  old = hash_lookup_data(*hash, real_packet);
  old_from_hash = (old != NULL);
  if (!old) {
    old = fc_malloc(sizeof(*old));
    memset(old, 0, sizeof(*old));
    force_send_of_unchanged = TRUE;
  }

  differ = (old->gov != real_packet->gov);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 0);}

  differ = (old->id != real_packet->id);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 1);}

  differ = (old->nation != real_packet->nation);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 2);}

  differ = (strcmp(old->male_title, real_packet->male_title) != 0);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 3);}

  differ = (strcmp(old->female_title, real_packet->female_title) != 0);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 4);}

  if (different == 0 && !force_send_of_unchanged) {
    return 0;
  }

  DIO_BV_PUT(&dout, fields);

  if (BV_ISSET(fields, 0)) {
    dio_put_uint8(&dout, real_packet->gov);
  }
  if (BV_ISSET(fields, 1)) {
    dio_put_uint8(&dout, real_packet->id);
  }
  if (BV_ISSET(fields, 2)) {
    dio_put_uint16(&dout, real_packet->nation);
  }
  if (BV_ISSET(fields, 3)) {
    dio_put_string(&dout, real_packet->male_title);
  }
  if (BV_ISSET(fields, 4)) {
    dio_put_string(&dout, real_packet->female_title);
  }


  if (old_from_hash) {
    hash_delete_entry(*hash, old);
  }

  clone = old;

  *clone = *real_packet;
  hash_insert(*hash, clone, clone);
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_ruleset_government_ruler_title(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_RULESET_GOVERNMENT_RULER_TITLE] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_RULESET_GOVERNMENT_RULER_TITLE] = variant;
}

struct packet_ruleset_government_ruler_title *receive_packet_ruleset_government_ruler_title(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Receiving packet_ruleset_government_ruler_title at the server.");
  }
  ensure_valid_variant_packet_ruleset_government_ruler_title(pc);

  switch(pc->phs.variant[PACKET_RULESET_GOVERNMENT_RULER_TITLE]) {
    case 100: return receive_packet_ruleset_government_ruler_title_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_ruleset_government_ruler_title(struct connection *pc, const struct packet_ruleset_government_ruler_title *packet)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Sending packet_ruleset_government_ruler_title from the client.");
  }
  ensure_valid_variant_packet_ruleset_government_ruler_title(pc);

  switch(pc->phs.variant[PACKET_RULESET_GOVERNMENT_RULER_TITLE]) {
    case 100: return send_packet_ruleset_government_ruler_title_100(pc, packet);
    default: die("unknown variant"); return -1;
  }
}

void lsend_packet_ruleset_government_ruler_title(struct conn_list *dest, const struct packet_ruleset_government_ruler_title *packet)
{
  conn_list_iterate(*dest, pconn) {
    send_packet_ruleset_government_ruler_title(pconn, packet);
  } conn_list_iterate_end;
}

#define hash_packet_ruleset_tech_100 hash_const

#define cmp_packet_ruleset_tech_100 cmp_const

BV_DEFINE(packet_ruleset_tech_100_fields, 10);

static struct packet_ruleset_tech *receive_packet_ruleset_tech_100(struct connection *pc, enum packet_type type)
{
  packet_ruleset_tech_100_fields fields;
  struct packet_ruleset_tech *old;
  struct hash_table **hash = &pc->phs.received[type];
  struct packet_ruleset_tech *clone;
  RECEIVE_PACKET_START(packet_ruleset_tech, real_packet);

  DIO_BV_GET(&din, fields);


  if (!*hash) {
    *hash = hash_new(hash_packet_ruleset_tech_100, cmp_packet_ruleset_tech_100);
  }
  old = hash_delete_entry(*hash, real_packet);

  if (old) {
    *real_packet = *old;
  } else {
    memset(real_packet, 0, sizeof(*real_packet));
  }

  if (BV_ISSET(fields, 0)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->id = readin;
    }
  }
  if (BV_ISSET(fields, 1)) {
    
    {
      int i;
    
      for (i = 0; i < 2; i++) {
        {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->req[i] = readin;
    }
      }
    }
  }
  if (BV_ISSET(fields, 2)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->root_req = readin;
    }
  }
  if (BV_ISSET(fields, 3)) {
    {
      int readin;
    
      dio_get_uint32(&din, &readin);
      real_packet->flags = readin;
    }
  }
  if (BV_ISSET(fields, 4)) {
    {
      int readin;
    
      dio_get_uint32(&din, &readin);
      real_packet->preset_cost = readin;
    }
  }
  if (BV_ISSET(fields, 5)) {
    {
      int readin;
    
      dio_get_uint32(&din, &readin);
      real_packet->num_reqs = readin;
    }
  }
  if (BV_ISSET(fields, 6)) {
    dio_get_string(&din, real_packet->name, sizeof(real_packet->name));
  }
  if (BV_ISSET(fields, 7)) {
    dio_get_string(&din, real_packet->helptext, sizeof(real_packet->helptext));
  }
  if (BV_ISSET(fields, 8)) {
    dio_get_string(&din, real_packet->graphic_str, sizeof(real_packet->graphic_str));
  }
  if (BV_ISSET(fields, 9)) {
    dio_get_string(&din, real_packet->graphic_alt, sizeof(real_packet->graphic_alt));
  }

  clone = fc_malloc(sizeof(*clone));
  *clone = *real_packet;
  if (old) {
    free(old);
  }
  hash_insert(*hash, clone, clone);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_ruleset_tech_100(struct connection *pc, const struct packet_ruleset_tech *packet)
{
  const struct packet_ruleset_tech *real_packet = packet;
  packet_ruleset_tech_100_fields fields;
  struct packet_ruleset_tech *old, *clone;
  bool differ, old_from_hash, force_send_of_unchanged = TRUE;
  struct hash_table **hash = &pc->phs.sent[PACKET_RULESET_TECH];
  int different = 0;
  SEND_PACKET_START(PACKET_RULESET_TECH);

  if (!*hash) {
    *hash = hash_new(hash_packet_ruleset_tech_100, cmp_packet_ruleset_tech_100);
  }
  BV_CLR_ALL(fields);

  old = hash_lookup_data(*hash, real_packet);
  old_from_hash = (old != NULL);
  if (!old) {
    old = fc_malloc(sizeof(*old));
    memset(old, 0, sizeof(*old));
    force_send_of_unchanged = TRUE;
  }

  differ = (old->id != real_packet->id);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 0);}


    {
      differ = (2 != 2);
      if(!differ) {
        int i;
        for (i = 0; i < 2; i++) {
          if (old->req[i] != real_packet->req[i]) {
            differ = TRUE;
            break;
          }
        }
      }
    }
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 1);}

  differ = (old->root_req != real_packet->root_req);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 2);}

  differ = (old->flags != real_packet->flags);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 3);}

  differ = (old->preset_cost != real_packet->preset_cost);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 4);}

  differ = (old->num_reqs != real_packet->num_reqs);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 5);}

  differ = (strcmp(old->name, real_packet->name) != 0);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 6);}

  differ = (strcmp(old->helptext, real_packet->helptext) != 0);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 7);}

  differ = (strcmp(old->graphic_str, real_packet->graphic_str) != 0);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 8);}

  differ = (strcmp(old->graphic_alt, real_packet->graphic_alt) != 0);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 9);}

  if (different == 0 && !force_send_of_unchanged) {
    return 0;
  }

  DIO_BV_PUT(&dout, fields);

  if (BV_ISSET(fields, 0)) {
    dio_put_uint8(&dout, real_packet->id);
  }
  if (BV_ISSET(fields, 1)) {
  
    {
      int i;

      for (i = 0; i < 2; i++) {
        dio_put_uint8(&dout, real_packet->req[i]);
      }
    } 
  }
  if (BV_ISSET(fields, 2)) {
    dio_put_uint8(&dout, real_packet->root_req);
  }
  if (BV_ISSET(fields, 3)) {
    dio_put_uint32(&dout, real_packet->flags);
  }
  if (BV_ISSET(fields, 4)) {
    dio_put_uint32(&dout, real_packet->preset_cost);
  }
  if (BV_ISSET(fields, 5)) {
    dio_put_uint32(&dout, real_packet->num_reqs);
  }
  if (BV_ISSET(fields, 6)) {
    dio_put_string(&dout, real_packet->name);
  }
  if (BV_ISSET(fields, 7)) {
    dio_put_string(&dout, real_packet->helptext);
  }
  if (BV_ISSET(fields, 8)) {
    dio_put_string(&dout, real_packet->graphic_str);
  }
  if (BV_ISSET(fields, 9)) {
    dio_put_string(&dout, real_packet->graphic_alt);
  }


  if (old_from_hash) {
    hash_delete_entry(*hash, old);
  }

  clone = old;

  *clone = *real_packet;
  hash_insert(*hash, clone, clone);
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_ruleset_tech(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_RULESET_TECH] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_RULESET_TECH] = variant;
}

struct packet_ruleset_tech *receive_packet_ruleset_tech(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Receiving packet_ruleset_tech at the server.");
  }
  ensure_valid_variant_packet_ruleset_tech(pc);

  switch(pc->phs.variant[PACKET_RULESET_TECH]) {
    case 100: return receive_packet_ruleset_tech_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_ruleset_tech(struct connection *pc, const struct packet_ruleset_tech *packet)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Sending packet_ruleset_tech from the client.");
  }
  ensure_valid_variant_packet_ruleset_tech(pc);

  switch(pc->phs.variant[PACKET_RULESET_TECH]) {
    case 100: return send_packet_ruleset_tech_100(pc, packet);
    default: die("unknown variant"); return -1;
  }
}

void lsend_packet_ruleset_tech(struct conn_list *dest, const struct packet_ruleset_tech *packet)
{
  conn_list_iterate(*dest, pconn) {
    send_packet_ruleset_tech(pconn, packet);
  } conn_list_iterate_end;
}

#define hash_packet_ruleset_government_100 hash_const

#define cmp_packet_ruleset_government_100 cmp_const

BV_DEFINE(packet_ruleset_government_100_fields, 45);

static struct packet_ruleset_government *receive_packet_ruleset_government_100(struct connection *pc, enum packet_type type)
{
  packet_ruleset_government_100_fields fields;
  struct packet_ruleset_government *old;
  struct hash_table **hash = &pc->phs.received[type];
  struct packet_ruleset_government *clone;
  RECEIVE_PACKET_START(packet_ruleset_government, real_packet);

  DIO_BV_GET(&din, fields);


  if (!*hash) {
    *hash = hash_new(hash_packet_ruleset_government_100, cmp_packet_ruleset_government_100);
  }
  old = hash_delete_entry(*hash, real_packet);

  if (old) {
    *real_packet = *old;
  } else {
    memset(real_packet, 0, sizeof(*real_packet));
  }

  if (BV_ISSET(fields, 0)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->id = readin;
    }
  }
  if (BV_ISSET(fields, 1)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->required_tech = readin;
    }
  }
  if (BV_ISSET(fields, 2)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->max_rate = readin;
    }
  }
  if (BV_ISSET(fields, 3)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->civil_war = readin;
    }
  }
  if (BV_ISSET(fields, 4)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->martial_law_max = readin;
    }
  }
  if (BV_ISSET(fields, 5)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->martial_law_per = readin;
    }
  }
  if (BV_ISSET(fields, 6)) {
    {
      int readin;
    
      dio_get_sint8(&din, &readin);
      real_packet->empire_size_mod = readin;
    }
  }
  if (BV_ISSET(fields, 7)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->empire_size_inc = readin;
    }
  }
  if (BV_ISSET(fields, 8)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->rapture_size = readin;
    }
  }
  if (BV_ISSET(fields, 9)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->unit_happy_cost_factor = readin;
    }
  }
  if (BV_ISSET(fields, 10)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->unit_shield_cost_factor = readin;
    }
  }
  if (BV_ISSET(fields, 11)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->unit_food_cost_factor = readin;
    }
  }
  if (BV_ISSET(fields, 12)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->unit_gold_cost_factor = readin;
    }
  }
  if (BV_ISSET(fields, 13)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->free_happy = readin;
    }
  }
  if (BV_ISSET(fields, 14)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->free_shield = readin;
    }
  }
  if (BV_ISSET(fields, 15)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->free_food = readin;
    }
  }
  if (BV_ISSET(fields, 16)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->free_gold = readin;
    }
  }
  if (BV_ISSET(fields, 17)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->trade_before_penalty = readin;
    }
  }
  if (BV_ISSET(fields, 18)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->shields_before_penalty = readin;
    }
  }
  if (BV_ISSET(fields, 19)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->food_before_penalty = readin;
    }
  }
  if (BV_ISSET(fields, 20)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->celeb_trade_before_penalty = readin;
    }
  }
  if (BV_ISSET(fields, 21)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->celeb_shields_before_penalty = readin;
    }
  }
  if (BV_ISSET(fields, 22)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->celeb_food_before_penalty = readin;
    }
  }
  if (BV_ISSET(fields, 23)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->trade_bonus = readin;
    }
  }
  if (BV_ISSET(fields, 24)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->shield_bonus = readin;
    }
  }
  if (BV_ISSET(fields, 25)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->food_bonus = readin;
    }
  }
  if (BV_ISSET(fields, 26)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->celeb_trade_bonus = readin;
    }
  }
  if (BV_ISSET(fields, 27)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->celeb_shield_bonus = readin;
    }
  }
  if (BV_ISSET(fields, 28)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->celeb_food_bonus = readin;
    }
  }
  if (BV_ISSET(fields, 29)) {
    {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->corruption_level = readin;
    }
  }
  if (BV_ISSET(fields, 30)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->fixed_corruption_distance = readin;
    }
  }
  if (BV_ISSET(fields, 31)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->corruption_distance_factor = readin;
    }
  }
  if (BV_ISSET(fields, 32)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->extra_corruption_distance = readin;
    }
  }
  if (BV_ISSET(fields, 33)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->corruption_max_distance_cap = readin;
    }
  }
  if (BV_ISSET(fields, 34)) {
    {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->waste_level = readin;
    }
  }
  if (BV_ISSET(fields, 35)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->fixed_waste_distance = readin;
    }
  }
  if (BV_ISSET(fields, 36)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->waste_distance_factor = readin;
    }
  }
  if (BV_ISSET(fields, 37)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->extra_waste_distance = readin;
    }
  }
  if (BV_ISSET(fields, 38)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->waste_max_distance_cap = readin;
    }
  }
  if (BV_ISSET(fields, 39)) {
    {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->flags = readin;
    }
  }
  if (BV_ISSET(fields, 40)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->num_ruler_titles = readin;
    }
  }
  if (BV_ISSET(fields, 41)) {
    dio_get_string(&din, real_packet->name, sizeof(real_packet->name));
  }
  if (BV_ISSET(fields, 42)) {
    dio_get_string(&din, real_packet->graphic_str, sizeof(real_packet->graphic_str));
  }
  if (BV_ISSET(fields, 43)) {
    dio_get_string(&din, real_packet->graphic_alt, sizeof(real_packet->graphic_alt));
  }
  if (BV_ISSET(fields, 44)) {
    dio_get_string(&din, real_packet->helptext, sizeof(real_packet->helptext));
  }

  clone = fc_malloc(sizeof(*clone));
  *clone = *real_packet;
  if (old) {
    free(old);
  }
  hash_insert(*hash, clone, clone);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_ruleset_government_100(struct connection *pc, const struct packet_ruleset_government *packet)
{
  const struct packet_ruleset_government *real_packet = packet;
  packet_ruleset_government_100_fields fields;
  struct packet_ruleset_government *old, *clone;
  bool differ, old_from_hash, force_send_of_unchanged = TRUE;
  struct hash_table **hash = &pc->phs.sent[PACKET_RULESET_GOVERNMENT];
  int different = 0;
  SEND_PACKET_START(PACKET_RULESET_GOVERNMENT);

  if (!*hash) {
    *hash = hash_new(hash_packet_ruleset_government_100, cmp_packet_ruleset_government_100);
  }
  BV_CLR_ALL(fields);

  old = hash_lookup_data(*hash, real_packet);
  old_from_hash = (old != NULL);
  if (!old) {
    old = fc_malloc(sizeof(*old));
    memset(old, 0, sizeof(*old));
    force_send_of_unchanged = TRUE;
  }

  differ = (old->id != real_packet->id);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 0);}

  differ = (old->required_tech != real_packet->required_tech);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 1);}

  differ = (old->max_rate != real_packet->max_rate);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 2);}

  differ = (old->civil_war != real_packet->civil_war);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 3);}

  differ = (old->martial_law_max != real_packet->martial_law_max);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 4);}

  differ = (old->martial_law_per != real_packet->martial_law_per);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 5);}

  differ = (old->empire_size_mod != real_packet->empire_size_mod);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 6);}

  differ = (old->empire_size_inc != real_packet->empire_size_inc);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 7);}

  differ = (old->rapture_size != real_packet->rapture_size);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 8);}

  differ = (old->unit_happy_cost_factor != real_packet->unit_happy_cost_factor);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 9);}

  differ = (old->unit_shield_cost_factor != real_packet->unit_shield_cost_factor);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 10);}

  differ = (old->unit_food_cost_factor != real_packet->unit_food_cost_factor);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 11);}

  differ = (old->unit_gold_cost_factor != real_packet->unit_gold_cost_factor);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 12);}

  differ = (old->free_happy != real_packet->free_happy);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 13);}

  differ = (old->free_shield != real_packet->free_shield);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 14);}

  differ = (old->free_food != real_packet->free_food);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 15);}

  differ = (old->free_gold != real_packet->free_gold);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 16);}

  differ = (old->trade_before_penalty != real_packet->trade_before_penalty);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 17);}

  differ = (old->shields_before_penalty != real_packet->shields_before_penalty);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 18);}

  differ = (old->food_before_penalty != real_packet->food_before_penalty);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 19);}

  differ = (old->celeb_trade_before_penalty != real_packet->celeb_trade_before_penalty);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 20);}

  differ = (old->celeb_shields_before_penalty != real_packet->celeb_shields_before_penalty);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 21);}

  differ = (old->celeb_food_before_penalty != real_packet->celeb_food_before_penalty);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 22);}

  differ = (old->trade_bonus != real_packet->trade_bonus);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 23);}

  differ = (old->shield_bonus != real_packet->shield_bonus);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 24);}

  differ = (old->food_bonus != real_packet->food_bonus);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 25);}

  differ = (old->celeb_trade_bonus != real_packet->celeb_trade_bonus);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 26);}

  differ = (old->celeb_shield_bonus != real_packet->celeb_shield_bonus);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 27);}

  differ = (old->celeb_food_bonus != real_packet->celeb_food_bonus);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 28);}

  differ = (old->corruption_level != real_packet->corruption_level);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 29);}

  differ = (old->fixed_corruption_distance != real_packet->fixed_corruption_distance);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 30);}

  differ = (old->corruption_distance_factor != real_packet->corruption_distance_factor);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 31);}

  differ = (old->extra_corruption_distance != real_packet->extra_corruption_distance);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 32);}

  differ = (old->corruption_max_distance_cap != real_packet->corruption_max_distance_cap);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 33);}

  differ = (old->waste_level != real_packet->waste_level);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 34);}

  differ = (old->fixed_waste_distance != real_packet->fixed_waste_distance);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 35);}

  differ = (old->waste_distance_factor != real_packet->waste_distance_factor);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 36);}

  differ = (old->extra_waste_distance != real_packet->extra_waste_distance);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 37);}

  differ = (old->waste_max_distance_cap != real_packet->waste_max_distance_cap);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 38);}

  differ = (old->flags != real_packet->flags);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 39);}

  differ = (old->num_ruler_titles != real_packet->num_ruler_titles);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 40);}

  differ = (strcmp(old->name, real_packet->name) != 0);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 41);}

  differ = (strcmp(old->graphic_str, real_packet->graphic_str) != 0);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 42);}

  differ = (strcmp(old->graphic_alt, real_packet->graphic_alt) != 0);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 43);}

  differ = (strcmp(old->helptext, real_packet->helptext) != 0);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 44);}

  if (different == 0 && !force_send_of_unchanged) {
    return 0;
  }

  DIO_BV_PUT(&dout, fields);

  if (BV_ISSET(fields, 0)) {
    dio_put_uint8(&dout, real_packet->id);
  }
  if (BV_ISSET(fields, 1)) {
    dio_put_uint8(&dout, real_packet->required_tech);
  }
  if (BV_ISSET(fields, 2)) {
    dio_put_uint8(&dout, real_packet->max_rate);
  }
  if (BV_ISSET(fields, 3)) {
    dio_put_uint8(&dout, real_packet->civil_war);
  }
  if (BV_ISSET(fields, 4)) {
    dio_put_uint8(&dout, real_packet->martial_law_max);
  }
  if (BV_ISSET(fields, 5)) {
    dio_put_uint8(&dout, real_packet->martial_law_per);
  }
  if (BV_ISSET(fields, 6)) {
    dio_put_sint8(&dout, real_packet->empire_size_mod);
  }
  if (BV_ISSET(fields, 7)) {
    dio_put_uint8(&dout, real_packet->empire_size_inc);
  }
  if (BV_ISSET(fields, 8)) {
    dio_put_uint8(&dout, real_packet->rapture_size);
  }
  if (BV_ISSET(fields, 9)) {
    dio_put_uint8(&dout, real_packet->unit_happy_cost_factor);
  }
  if (BV_ISSET(fields, 10)) {
    dio_put_uint8(&dout, real_packet->unit_shield_cost_factor);
  }
  if (BV_ISSET(fields, 11)) {
    dio_put_uint8(&dout, real_packet->unit_food_cost_factor);
  }
  if (BV_ISSET(fields, 12)) {
    dio_put_uint8(&dout, real_packet->unit_gold_cost_factor);
  }
  if (BV_ISSET(fields, 13)) {
    dio_put_uint8(&dout, real_packet->free_happy);
  }
  if (BV_ISSET(fields, 14)) {
    dio_put_uint8(&dout, real_packet->free_shield);
  }
  if (BV_ISSET(fields, 15)) {
    dio_put_uint8(&dout, real_packet->free_food);
  }
  if (BV_ISSET(fields, 16)) {
    dio_put_uint8(&dout, real_packet->free_gold);
  }
  if (BV_ISSET(fields, 17)) {
    dio_put_uint8(&dout, real_packet->trade_before_penalty);
  }
  if (BV_ISSET(fields, 18)) {
    dio_put_uint8(&dout, real_packet->shields_before_penalty);
  }
  if (BV_ISSET(fields, 19)) {
    dio_put_uint8(&dout, real_packet->food_before_penalty);
  }
  if (BV_ISSET(fields, 20)) {
    dio_put_uint8(&dout, real_packet->celeb_trade_before_penalty);
  }
  if (BV_ISSET(fields, 21)) {
    dio_put_uint8(&dout, real_packet->celeb_shields_before_penalty);
  }
  if (BV_ISSET(fields, 22)) {
    dio_put_uint8(&dout, real_packet->celeb_food_before_penalty);
  }
  if (BV_ISSET(fields, 23)) {
    dio_put_uint8(&dout, real_packet->trade_bonus);
  }
  if (BV_ISSET(fields, 24)) {
    dio_put_uint8(&dout, real_packet->shield_bonus);
  }
  if (BV_ISSET(fields, 25)) {
    dio_put_uint8(&dout, real_packet->food_bonus);
  }
  if (BV_ISSET(fields, 26)) {
    dio_put_uint8(&dout, real_packet->celeb_trade_bonus);
  }
  if (BV_ISSET(fields, 27)) {
    dio_put_uint8(&dout, real_packet->celeb_shield_bonus);
  }
  if (BV_ISSET(fields, 28)) {
    dio_put_uint8(&dout, real_packet->celeb_food_bonus);
  }
  if (BV_ISSET(fields, 29)) {
    dio_put_uint16(&dout, real_packet->corruption_level);
  }
  if (BV_ISSET(fields, 30)) {
    dio_put_uint8(&dout, real_packet->fixed_corruption_distance);
  }
  if (BV_ISSET(fields, 31)) {
    dio_put_uint8(&dout, real_packet->corruption_distance_factor);
  }
  if (BV_ISSET(fields, 32)) {
    dio_put_uint8(&dout, real_packet->extra_corruption_distance);
  }
  if (BV_ISSET(fields, 33)) {
    dio_put_uint8(&dout, real_packet->corruption_max_distance_cap);
  }
  if (BV_ISSET(fields, 34)) {
    dio_put_uint16(&dout, real_packet->waste_level);
  }
  if (BV_ISSET(fields, 35)) {
    dio_put_uint8(&dout, real_packet->fixed_waste_distance);
  }
  if (BV_ISSET(fields, 36)) {
    dio_put_uint8(&dout, real_packet->waste_distance_factor);
  }
  if (BV_ISSET(fields, 37)) {
    dio_put_uint8(&dout, real_packet->extra_waste_distance);
  }
  if (BV_ISSET(fields, 38)) {
    dio_put_uint8(&dout, real_packet->waste_max_distance_cap);
  }
  if (BV_ISSET(fields, 39)) {
    dio_put_uint16(&dout, real_packet->flags);
  }
  if (BV_ISSET(fields, 40)) {
    dio_put_uint8(&dout, real_packet->num_ruler_titles);
  }
  if (BV_ISSET(fields, 41)) {
    dio_put_string(&dout, real_packet->name);
  }
  if (BV_ISSET(fields, 42)) {
    dio_put_string(&dout, real_packet->graphic_str);
  }
  if (BV_ISSET(fields, 43)) {
    dio_put_string(&dout, real_packet->graphic_alt);
  }
  if (BV_ISSET(fields, 44)) {
    dio_put_string(&dout, real_packet->helptext);
  }


  if (old_from_hash) {
    hash_delete_entry(*hash, old);
  }

  clone = old;

  *clone = *real_packet;
  hash_insert(*hash, clone, clone);
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_ruleset_government(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_RULESET_GOVERNMENT] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_RULESET_GOVERNMENT] = variant;
}

struct packet_ruleset_government *receive_packet_ruleset_government(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Receiving packet_ruleset_government at the server.");
  }
  ensure_valid_variant_packet_ruleset_government(pc);

  switch(pc->phs.variant[PACKET_RULESET_GOVERNMENT]) {
    case 100: return receive_packet_ruleset_government_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_ruleset_government(struct connection *pc, const struct packet_ruleset_government *packet)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Sending packet_ruleset_government from the client.");
  }
  ensure_valid_variant_packet_ruleset_government(pc);

  switch(pc->phs.variant[PACKET_RULESET_GOVERNMENT]) {
    case 100: return send_packet_ruleset_government_100(pc, packet);
    default: die("unknown variant"); return -1;
  }
}

void lsend_packet_ruleset_government(struct conn_list *dest, const struct packet_ruleset_government *packet)
{
  conn_list_iterate(*dest, pconn) {
    send_packet_ruleset_government(pconn, packet);
  } conn_list_iterate_end;
}

#define hash_packet_ruleset_terrain_control_100 hash_const

#define cmp_packet_ruleset_terrain_control_100 cmp_const

BV_DEFINE(packet_ruleset_terrain_control_100_fields, 22);

static struct packet_ruleset_terrain_control *receive_packet_ruleset_terrain_control_100(struct connection *pc, enum packet_type type)
{
  packet_ruleset_terrain_control_100_fields fields;
  struct packet_ruleset_terrain_control *old;
  struct hash_table **hash = &pc->phs.received[type];
  struct packet_ruleset_terrain_control *clone;
  RECEIVE_PACKET_START(packet_ruleset_terrain_control, real_packet);

  DIO_BV_GET(&din, fields);


  if (!*hash) {
    *hash = hash_new(hash_packet_ruleset_terrain_control_100, cmp_packet_ruleset_terrain_control_100);
  }
  old = hash_delete_entry(*hash, real_packet);

  if (old) {
    *real_packet = *old;
  } else {
    memset(real_packet, 0, sizeof(*real_packet));
  }

  real_packet->may_road = BV_ISSET(fields, 0);
  real_packet->may_irrigate = BV_ISSET(fields, 1);
  real_packet->may_mine = BV_ISSET(fields, 2);
  real_packet->may_transform = BV_ISSET(fields, 3);
  if (BV_ISSET(fields, 4)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->ocean_reclaim_requirement_pct = readin;
    }
  }
  if (BV_ISSET(fields, 5)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->land_channel_requirement_pct = readin;
    }
  }
  if (BV_ISSET(fields, 6)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->river_move_mode = readin;
    }
  }
  if (BV_ISSET(fields, 7)) {
    {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->river_defense_bonus = readin;
    }
  }
  if (BV_ISSET(fields, 8)) {
    {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->river_trade_incr = readin;
    }
  }
  if (BV_ISSET(fields, 9)) {
    dio_get_string(&din, real_packet->river_help_text, sizeof(real_packet->river_help_text));
  }
  if (BV_ISSET(fields, 10)) {
    {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->fortress_defense_bonus = readin;
    }
  }
  if (BV_ISSET(fields, 11)) {
    {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->road_superhighway_trade_bonus = readin;
    }
  }
  if (BV_ISSET(fields, 12)) {
    {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->rail_food_bonus = readin;
    }
  }
  if (BV_ISSET(fields, 13)) {
    {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->rail_shield_bonus = readin;
    }
  }
  if (BV_ISSET(fields, 14)) {
    {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->rail_trade_bonus = readin;
    }
  }
  if (BV_ISSET(fields, 15)) {
    {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->farmland_supermarket_food_bonus = readin;
    }
  }
  if (BV_ISSET(fields, 16)) {
    {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->pollution_food_penalty = readin;
    }
  }
  if (BV_ISSET(fields, 17)) {
    {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->pollution_shield_penalty = readin;
    }
  }
  if (BV_ISSET(fields, 18)) {
    {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->pollution_trade_penalty = readin;
    }
  }
  if (BV_ISSET(fields, 19)) {
    {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->fallout_food_penalty = readin;
    }
  }
  if (BV_ISSET(fields, 20)) {
    {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->fallout_shield_penalty = readin;
    }
  }
  if (BV_ISSET(fields, 21)) {
    {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->fallout_trade_penalty = readin;
    }
  }

  clone = fc_malloc(sizeof(*clone));
  *clone = *real_packet;
  if (old) {
    free(old);
  }
  hash_insert(*hash, clone, clone);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_ruleset_terrain_control_100(struct connection *pc, const struct packet_ruleset_terrain_control *packet)
{
  const struct packet_ruleset_terrain_control *real_packet = packet;
  packet_ruleset_terrain_control_100_fields fields;
  struct packet_ruleset_terrain_control *old, *clone;
  bool differ, old_from_hash, force_send_of_unchanged = TRUE;
  struct hash_table **hash = &pc->phs.sent[PACKET_RULESET_TERRAIN_CONTROL];
  int different = 0;
  SEND_PACKET_START(PACKET_RULESET_TERRAIN_CONTROL);

  if (!*hash) {
    *hash = hash_new(hash_packet_ruleset_terrain_control_100, cmp_packet_ruleset_terrain_control_100);
  }
  BV_CLR_ALL(fields);

  old = hash_lookup_data(*hash, real_packet);
  old_from_hash = (old != NULL);
  if (!old) {
    old = fc_malloc(sizeof(*old));
    memset(old, 0, sizeof(*old));
    force_send_of_unchanged = TRUE;
  }

  differ = (old->may_road != real_packet->may_road);
  if(differ) {different++;}
  if(packet->may_road) {BV_SET(fields, 0);}

  differ = (old->may_irrigate != real_packet->may_irrigate);
  if(differ) {different++;}
  if(packet->may_irrigate) {BV_SET(fields, 1);}

  differ = (old->may_mine != real_packet->may_mine);
  if(differ) {different++;}
  if(packet->may_mine) {BV_SET(fields, 2);}

  differ = (old->may_transform != real_packet->may_transform);
  if(differ) {different++;}
  if(packet->may_transform) {BV_SET(fields, 3);}

  differ = (old->ocean_reclaim_requirement_pct != real_packet->ocean_reclaim_requirement_pct);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 4);}

  differ = (old->land_channel_requirement_pct != real_packet->land_channel_requirement_pct);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 5);}

  differ = (old->river_move_mode != real_packet->river_move_mode);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 6);}

  differ = (old->river_defense_bonus != real_packet->river_defense_bonus);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 7);}

  differ = (old->river_trade_incr != real_packet->river_trade_incr);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 8);}

  differ = (strcmp(old->river_help_text, real_packet->river_help_text) != 0);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 9);}

  differ = (old->fortress_defense_bonus != real_packet->fortress_defense_bonus);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 10);}

  differ = (old->road_superhighway_trade_bonus != real_packet->road_superhighway_trade_bonus);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 11);}

  differ = (old->rail_food_bonus != real_packet->rail_food_bonus);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 12);}

  differ = (old->rail_shield_bonus != real_packet->rail_shield_bonus);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 13);}

  differ = (old->rail_trade_bonus != real_packet->rail_trade_bonus);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 14);}

  differ = (old->farmland_supermarket_food_bonus != real_packet->farmland_supermarket_food_bonus);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 15);}

  differ = (old->pollution_food_penalty != real_packet->pollution_food_penalty);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 16);}

  differ = (old->pollution_shield_penalty != real_packet->pollution_shield_penalty);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 17);}

  differ = (old->pollution_trade_penalty != real_packet->pollution_trade_penalty);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 18);}

  differ = (old->fallout_food_penalty != real_packet->fallout_food_penalty);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 19);}

  differ = (old->fallout_shield_penalty != real_packet->fallout_shield_penalty);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 20);}

  differ = (old->fallout_trade_penalty != real_packet->fallout_trade_penalty);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 21);}

  if (different == 0 && !force_send_of_unchanged) {
    return 0;
  }

  DIO_BV_PUT(&dout, fields);

  /* field 0 is folded into the header */
  /* field 1 is folded into the header */
  /* field 2 is folded into the header */
  /* field 3 is folded into the header */
  if (BV_ISSET(fields, 4)) {
    dio_put_uint8(&dout, real_packet->ocean_reclaim_requirement_pct);
  }
  if (BV_ISSET(fields, 5)) {
    dio_put_uint8(&dout, real_packet->land_channel_requirement_pct);
  }
  if (BV_ISSET(fields, 6)) {
    dio_put_uint8(&dout, real_packet->river_move_mode);
  }
  if (BV_ISSET(fields, 7)) {
    dio_put_uint16(&dout, real_packet->river_defense_bonus);
  }
  if (BV_ISSET(fields, 8)) {
    dio_put_uint16(&dout, real_packet->river_trade_incr);
  }
  if (BV_ISSET(fields, 9)) {
    dio_put_string(&dout, real_packet->river_help_text);
  }
  if (BV_ISSET(fields, 10)) {
    dio_put_uint16(&dout, real_packet->fortress_defense_bonus);
  }
  if (BV_ISSET(fields, 11)) {
    dio_put_uint16(&dout, real_packet->road_superhighway_trade_bonus);
  }
  if (BV_ISSET(fields, 12)) {
    dio_put_uint16(&dout, real_packet->rail_food_bonus);
  }
  if (BV_ISSET(fields, 13)) {
    dio_put_uint16(&dout, real_packet->rail_shield_bonus);
  }
  if (BV_ISSET(fields, 14)) {
    dio_put_uint16(&dout, real_packet->rail_trade_bonus);
  }
  if (BV_ISSET(fields, 15)) {
    dio_put_uint16(&dout, real_packet->farmland_supermarket_food_bonus);
  }
  if (BV_ISSET(fields, 16)) {
    dio_put_uint16(&dout, real_packet->pollution_food_penalty);
  }
  if (BV_ISSET(fields, 17)) {
    dio_put_uint16(&dout, real_packet->pollution_shield_penalty);
  }
  if (BV_ISSET(fields, 18)) {
    dio_put_uint16(&dout, real_packet->pollution_trade_penalty);
  }
  if (BV_ISSET(fields, 19)) {
    dio_put_uint16(&dout, real_packet->fallout_food_penalty);
  }
  if (BV_ISSET(fields, 20)) {
    dio_put_uint16(&dout, real_packet->fallout_shield_penalty);
  }
  if (BV_ISSET(fields, 21)) {
    dio_put_uint16(&dout, real_packet->fallout_trade_penalty);
  }


  if (old_from_hash) {
    hash_delete_entry(*hash, old);
  }

  clone = old;

  *clone = *real_packet;
  hash_insert(*hash, clone, clone);
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_ruleset_terrain_control(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_RULESET_TERRAIN_CONTROL] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_RULESET_TERRAIN_CONTROL] = variant;
}

struct packet_ruleset_terrain_control *receive_packet_ruleset_terrain_control(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Receiving packet_ruleset_terrain_control at the server.");
  }
  ensure_valid_variant_packet_ruleset_terrain_control(pc);

  switch(pc->phs.variant[PACKET_RULESET_TERRAIN_CONTROL]) {
    case 100: return receive_packet_ruleset_terrain_control_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_ruleset_terrain_control(struct connection *pc, const struct packet_ruleset_terrain_control *packet)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Sending packet_ruleset_terrain_control from the client.");
  }
  ensure_valid_variant_packet_ruleset_terrain_control(pc);

  switch(pc->phs.variant[PACKET_RULESET_TERRAIN_CONTROL]) {
    case 100: return send_packet_ruleset_terrain_control_100(pc, packet);
    default: die("unknown variant"); return -1;
  }
}

void lsend_packet_ruleset_terrain_control(struct conn_list *dest, const struct packet_ruleset_terrain_control *packet)
{
  conn_list_iterate(*dest, pconn) {
    send_packet_ruleset_terrain_control(pconn, packet);
  } conn_list_iterate_end;
}

#define hash_packet_ruleset_nation_100 hash_const

#define cmp_packet_ruleset_nation_100 cmp_const

BV_DEFINE(packet_ruleset_nation_100_fields, 12);

static struct packet_ruleset_nation *receive_packet_ruleset_nation_100(struct connection *pc, enum packet_type type)
{
  packet_ruleset_nation_100_fields fields;
  struct packet_ruleset_nation *old;
  struct hash_table **hash = &pc->phs.received[type];
  struct packet_ruleset_nation *clone;
  RECEIVE_PACKET_START(packet_ruleset_nation, real_packet);

  DIO_BV_GET(&din, fields);


  if (!*hash) {
    *hash = hash_new(hash_packet_ruleset_nation_100, cmp_packet_ruleset_nation_100);
  }
  old = hash_delete_entry(*hash, real_packet);

  if (old) {
    *real_packet = *old;
  } else {
    memset(real_packet, 0, sizeof(*real_packet));
  }

  if (BV_ISSET(fields, 0)) {
    {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->id = readin;
    }
  }
  if (BV_ISSET(fields, 1)) {
    dio_get_string(&din, real_packet->name, sizeof(real_packet->name));
  }
  if (BV_ISSET(fields, 2)) {
    dio_get_string(&din, real_packet->name_plural, sizeof(real_packet->name_plural));
  }
  if (BV_ISSET(fields, 3)) {
    dio_get_string(&din, real_packet->graphic_str, sizeof(real_packet->graphic_str));
  }
  if (BV_ISSET(fields, 4)) {
    dio_get_string(&din, real_packet->graphic_alt, sizeof(real_packet->graphic_alt));
  }
  if (BV_ISSET(fields, 5)) {
    dio_get_string(&din, real_packet->class, sizeof(real_packet->class));
  }
  if (BV_ISSET(fields, 6)) {
    dio_get_string(&din, real_packet->legend, sizeof(real_packet->legend));
  }
  if (BV_ISSET(fields, 7)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->city_style = readin;
    }
  }
  if (BV_ISSET(fields, 8)) {
    dio_get_tech_list(&din, real_packet->init_techs);
  }
  if (BV_ISSET(fields, 9)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->leader_count = readin;
    }
  }
  if (BV_ISSET(fields, 10)) {
    
    {
      int i;
    
      if(real_packet->leader_count > MAX_NUM_LEADERS) {
        freelog(LOG_ERROR, "packets_gen.c: WARNING: truncation array");
        real_packet->leader_count = MAX_NUM_LEADERS;
      }
      for (i = 0; i < real_packet->leader_count; i++) {
        dio_get_string(&din, real_packet->leader_name[i], sizeof(real_packet->leader_name[i]));
      }
    }
  }
  if (BV_ISSET(fields, 11)) {
    
    {
      int i;
    
      if(real_packet->leader_count > MAX_NUM_LEADERS) {
        freelog(LOG_ERROR, "packets_gen.c: WARNING: truncation array");
        real_packet->leader_count = MAX_NUM_LEADERS;
      }
      for (i = 0; i < real_packet->leader_count; i++) {
        dio_get_bool8(&din, &real_packet->leader_sex[i]);
      }
    }
  }

  clone = fc_malloc(sizeof(*clone));
  *clone = *real_packet;
  if (old) {
    free(old);
  }
  hash_insert(*hash, clone, clone);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_ruleset_nation_100(struct connection *pc, const struct packet_ruleset_nation *packet)
{
  const struct packet_ruleset_nation *real_packet = packet;
  packet_ruleset_nation_100_fields fields;
  struct packet_ruleset_nation *old, *clone;
  bool differ, old_from_hash, force_send_of_unchanged = TRUE;
  struct hash_table **hash = &pc->phs.sent[PACKET_RULESET_NATION];
  int different = 0;
  SEND_PACKET_START(PACKET_RULESET_NATION);

  if (!*hash) {
    *hash = hash_new(hash_packet_ruleset_nation_100, cmp_packet_ruleset_nation_100);
  }
  BV_CLR_ALL(fields);

  old = hash_lookup_data(*hash, real_packet);
  old_from_hash = (old != NULL);
  if (!old) {
    old = fc_malloc(sizeof(*old));
    memset(old, 0, sizeof(*old));
    force_send_of_unchanged = TRUE;
  }

  differ = (old->id != real_packet->id);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 0);}

  differ = (strcmp(old->name, real_packet->name) != 0);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 1);}

  differ = (strcmp(old->name_plural, real_packet->name_plural) != 0);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 2);}

  differ = (strcmp(old->graphic_str, real_packet->graphic_str) != 0);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 3);}

  differ = (strcmp(old->graphic_alt, real_packet->graphic_alt) != 0);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 4);}

  differ = (strcmp(old->class, real_packet->class) != 0);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 5);}

  differ = (strcmp(old->legend, real_packet->legend) != 0);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 6);}

  differ = (old->city_style != real_packet->city_style);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 7);}


    {
      differ = (MAX_NUM_TECH_LIST != MAX_NUM_TECH_LIST);
      if(!differ) {
        int i;
        for (i = 0; i < MAX_NUM_TECH_LIST; i++) {
          if (old->init_techs[i] != real_packet->init_techs[i]) {
            differ = TRUE;
            break;
          }
        }
      }
    }
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 8);}

  differ = (old->leader_count != real_packet->leader_count);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 9);}


    {
      differ = (old->leader_count != real_packet->leader_count);
      if(!differ) {
        int i;
        for (i = 0; i < real_packet->leader_count; i++) {
          if (strcmp(old->leader_name[i], real_packet->leader_name[i]) != 0) {
            differ = TRUE;
            break;
          }
        }
      }
    }
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 10);}


    {
      differ = (old->leader_count != real_packet->leader_count);
      if(!differ) {
        int i;
        for (i = 0; i < real_packet->leader_count; i++) {
          if (old->leader_sex[i] != real_packet->leader_sex[i]) {
            differ = TRUE;
            break;
          }
        }
      }
    }
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 11);}

  if (different == 0 && !force_send_of_unchanged) {
    return 0;
  }

  DIO_BV_PUT(&dout, fields);

  if (BV_ISSET(fields, 0)) {
    dio_put_uint16(&dout, real_packet->id);
  }
  if (BV_ISSET(fields, 1)) {
    dio_put_string(&dout, real_packet->name);
  }
  if (BV_ISSET(fields, 2)) {
    dio_put_string(&dout, real_packet->name_plural);
  }
  if (BV_ISSET(fields, 3)) {
    dio_put_string(&dout, real_packet->graphic_str);
  }
  if (BV_ISSET(fields, 4)) {
    dio_put_string(&dout, real_packet->graphic_alt);
  }
  if (BV_ISSET(fields, 5)) {
    dio_put_string(&dout, real_packet->class);
  }
  if (BV_ISSET(fields, 6)) {
    dio_put_string(&dout, real_packet->legend);
  }
  if (BV_ISSET(fields, 7)) {
    dio_put_uint8(&dout, real_packet->city_style);
  }
  if (BV_ISSET(fields, 8)) {
    dio_put_tech_list(&dout, real_packet->init_techs);
  }
  if (BV_ISSET(fields, 9)) {
    dio_put_uint8(&dout, real_packet->leader_count);
  }
  if (BV_ISSET(fields, 10)) {
  
    {
      int i;

      for (i = 0; i < real_packet->leader_count; i++) {
        dio_put_string(&dout, real_packet->leader_name[i]);
      }
    } 
  }
  if (BV_ISSET(fields, 11)) {
  
    {
      int i;

      for (i = 0; i < real_packet->leader_count; i++) {
        dio_put_bool8(&dout, real_packet->leader_sex[i]);
      }
    } 
  }


  if (old_from_hash) {
    hash_delete_entry(*hash, old);
  }

  clone = old;

  *clone = *real_packet;
  hash_insert(*hash, clone, clone);
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_ruleset_nation(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_RULESET_NATION] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_RULESET_NATION] = variant;
}

struct packet_ruleset_nation *receive_packet_ruleset_nation(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Receiving packet_ruleset_nation at the server.");
  }
  ensure_valid_variant_packet_ruleset_nation(pc);

  switch(pc->phs.variant[PACKET_RULESET_NATION]) {
    case 100: return receive_packet_ruleset_nation_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_ruleset_nation(struct connection *pc, const struct packet_ruleset_nation *packet)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Sending packet_ruleset_nation from the client.");
  }
  ensure_valid_variant_packet_ruleset_nation(pc);

  switch(pc->phs.variant[PACKET_RULESET_NATION]) {
    case 100: return send_packet_ruleset_nation_100(pc, packet);
    default: die("unknown variant"); return -1;
  }
}

void lsend_packet_ruleset_nation(struct conn_list *dest, const struct packet_ruleset_nation *packet)
{
  conn_list_iterate(*dest, pconn) {
    send_packet_ruleset_nation(pconn, packet);
  } conn_list_iterate_end;
}

#define hash_packet_ruleset_city_100 hash_const

#define cmp_packet_ruleset_city_100 cmp_const

BV_DEFINE(packet_ruleset_city_100_fields, 8);

static struct packet_ruleset_city *receive_packet_ruleset_city_100(struct connection *pc, enum packet_type type)
{
  packet_ruleset_city_100_fields fields;
  struct packet_ruleset_city *old;
  struct hash_table **hash = &pc->phs.received[type];
  struct packet_ruleset_city *clone;
  RECEIVE_PACKET_START(packet_ruleset_city, real_packet);

  DIO_BV_GET(&din, fields);


  if (!*hash) {
    *hash = hash_new(hash_packet_ruleset_city_100, cmp_packet_ruleset_city_100);
  }
  old = hash_delete_entry(*hash, real_packet);

  if (old) {
    *real_packet = *old;
  } else {
    memset(real_packet, 0, sizeof(*real_packet));
  }

  if (BV_ISSET(fields, 0)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->style_id = readin;
    }
  }
  if (BV_ISSET(fields, 1)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->techreq = readin;
    }
  }
  if (BV_ISSET(fields, 2)) {
    dio_get_string(&din, real_packet->name, sizeof(real_packet->name));
  }
  if (BV_ISSET(fields, 3)) {
    dio_get_string(&din, real_packet->citizens_graphic, sizeof(real_packet->citizens_graphic));
  }
  if (BV_ISSET(fields, 4)) {
    dio_get_string(&din, real_packet->citizens_graphic_alt, sizeof(real_packet->citizens_graphic_alt));
  }
  if (BV_ISSET(fields, 5)) {
    dio_get_string(&din, real_packet->graphic, sizeof(real_packet->graphic));
  }
  if (BV_ISSET(fields, 6)) {
    dio_get_string(&din, real_packet->graphic_alt, sizeof(real_packet->graphic_alt));
  }
  if (BV_ISSET(fields, 7)) {
    {
      int readin;
    
      dio_get_sint8(&din, &readin);
      real_packet->replaced_by = readin;
    }
  }

  clone = fc_malloc(sizeof(*clone));
  *clone = *real_packet;
  if (old) {
    free(old);
  }
  hash_insert(*hash, clone, clone);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_ruleset_city_100(struct connection *pc, const struct packet_ruleset_city *packet)
{
  const struct packet_ruleset_city *real_packet = packet;
  packet_ruleset_city_100_fields fields;
  struct packet_ruleset_city *old, *clone;
  bool differ, old_from_hash, force_send_of_unchanged = TRUE;
  struct hash_table **hash = &pc->phs.sent[PACKET_RULESET_CITY];
  int different = 0;
  SEND_PACKET_START(PACKET_RULESET_CITY);

  if (!*hash) {
    *hash = hash_new(hash_packet_ruleset_city_100, cmp_packet_ruleset_city_100);
  }
  BV_CLR_ALL(fields);

  old = hash_lookup_data(*hash, real_packet);
  old_from_hash = (old != NULL);
  if (!old) {
    old = fc_malloc(sizeof(*old));
    memset(old, 0, sizeof(*old));
    force_send_of_unchanged = TRUE;
  }

  differ = (old->style_id != real_packet->style_id);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 0);}

  differ = (old->techreq != real_packet->techreq);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 1);}

  differ = (strcmp(old->name, real_packet->name) != 0);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 2);}

  differ = (strcmp(old->citizens_graphic, real_packet->citizens_graphic) != 0);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 3);}

  differ = (strcmp(old->citizens_graphic_alt, real_packet->citizens_graphic_alt) != 0);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 4);}

  differ = (strcmp(old->graphic, real_packet->graphic) != 0);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 5);}

  differ = (strcmp(old->graphic_alt, real_packet->graphic_alt) != 0);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 6);}

  differ = (old->replaced_by != real_packet->replaced_by);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 7);}

  if (different == 0 && !force_send_of_unchanged) {
    return 0;
  }

  DIO_BV_PUT(&dout, fields);

  if (BV_ISSET(fields, 0)) {
    dio_put_uint8(&dout, real_packet->style_id);
  }
  if (BV_ISSET(fields, 1)) {
    dio_put_uint8(&dout, real_packet->techreq);
  }
  if (BV_ISSET(fields, 2)) {
    dio_put_string(&dout, real_packet->name);
  }
  if (BV_ISSET(fields, 3)) {
    dio_put_string(&dout, real_packet->citizens_graphic);
  }
  if (BV_ISSET(fields, 4)) {
    dio_put_string(&dout, real_packet->citizens_graphic_alt);
  }
  if (BV_ISSET(fields, 5)) {
    dio_put_string(&dout, real_packet->graphic);
  }
  if (BV_ISSET(fields, 6)) {
    dio_put_string(&dout, real_packet->graphic_alt);
  }
  if (BV_ISSET(fields, 7)) {
    dio_put_sint8(&dout, real_packet->replaced_by);
  }


  if (old_from_hash) {
    hash_delete_entry(*hash, old);
  }

  clone = old;

  *clone = *real_packet;
  hash_insert(*hash, clone, clone);
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_ruleset_city(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_RULESET_CITY] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_RULESET_CITY] = variant;
}

struct packet_ruleset_city *receive_packet_ruleset_city(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Receiving packet_ruleset_city at the server.");
  }
  ensure_valid_variant_packet_ruleset_city(pc);

  switch(pc->phs.variant[PACKET_RULESET_CITY]) {
    case 100: return receive_packet_ruleset_city_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_ruleset_city(struct connection *pc, const struct packet_ruleset_city *packet)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Sending packet_ruleset_city from the client.");
  }
  ensure_valid_variant_packet_ruleset_city(pc);

  switch(pc->phs.variant[PACKET_RULESET_CITY]) {
    case 100: return send_packet_ruleset_city_100(pc, packet);
    default: die("unknown variant"); return -1;
  }
}

void lsend_packet_ruleset_city(struct conn_list *dest, const struct packet_ruleset_city *packet)
{
  conn_list_iterate(*dest, pconn) {
    send_packet_ruleset_city(pconn, packet);
  } conn_list_iterate_end;
}

#define hash_packet_ruleset_building_100 hash_const

#define cmp_packet_ruleset_building_100 cmp_const

BV_DEFINE(packet_ruleset_building_100_fields, 24);

static struct packet_ruleset_building *receive_packet_ruleset_building_100(struct connection *pc, enum packet_type type)
{
  packet_ruleset_building_100_fields fields;
  struct packet_ruleset_building *old;
  struct hash_table **hash = &pc->phs.received[type];
  struct packet_ruleset_building *clone;
  RECEIVE_PACKET_START(packet_ruleset_building, real_packet);

  DIO_BV_GET(&din, fields);


  if (!*hash) {
    *hash = hash_new(hash_packet_ruleset_building_100, cmp_packet_ruleset_building_100);
  }
  old = hash_delete_entry(*hash, real_packet);

  if (old) {
    *real_packet = *old;
  } else {
    memset(real_packet, 0, sizeof(*real_packet));
  }

  if (BV_ISSET(fields, 0)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->id = readin;
    }
  }
  if (BV_ISSET(fields, 1)) {
    dio_get_string(&din, real_packet->name, sizeof(real_packet->name));
  }
  if (BV_ISSET(fields, 2)) {
    dio_get_string(&din, real_packet->graphic_str, sizeof(real_packet->graphic_str));
  }
  if (BV_ISSET(fields, 3)) {
    dio_get_string(&din, real_packet->graphic_alt, sizeof(real_packet->graphic_alt));
  }
  if (BV_ISSET(fields, 4)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->tech_req = readin;
    }
  }
  if (BV_ISSET(fields, 5)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->obsolete_by = readin;
    }
  }
  if (BV_ISSET(fields, 6)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->bldg_req = readin;
    }
  }
  if (BV_ISSET(fields, 7)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->replaced_by = readin;
    }
  }
  real_packet->is_wonder = BV_ISSET(fields, 8);
  if (BV_ISSET(fields, 9)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->equiv_range = readin;
    }
  }
  if (BV_ISSET(fields, 10)) {
    {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->build_cost = readin;
    }
  }
  if (BV_ISSET(fields, 11)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->upkeep = readin;
    }
  }
  if (BV_ISSET(fields, 12)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->sabotage = readin;
    }
  }
  if (BV_ISSET(fields, 13)) {
    dio_get_string(&din, real_packet->soundtag, sizeof(real_packet->soundtag));
  }
  if (BV_ISSET(fields, 14)) {
    dio_get_string(&din, real_packet->soundtag_alt, sizeof(real_packet->soundtag_alt));
  }
  if (BV_ISSET(fields, 15)) {
    dio_get_string(&din, real_packet->helptext, sizeof(real_packet->helptext));
  }
  if (BV_ISSET(fields, 16)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->terr_gate_count = readin;
    }
  }
  if (BV_ISSET(fields, 17)) {
    
    {
      int i;
    
      if(real_packet->terr_gate_count > 255) {
        freelog(LOG_ERROR, "packets_gen.c: WARNING: truncation array");
        real_packet->terr_gate_count = 255;
      }
      for (i = 0; i < real_packet->terr_gate_count; i++) {
        {
      int readin;
    
      dio_get_sint16(&din, &readin);
      real_packet->terr_gate[i] = readin;
    }
      }
    }
  }
  if (BV_ISSET(fields, 18)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->spec_gate_count = readin;
    }
  }
  if (BV_ISSET(fields, 19)) {
    
    {
      int i;
    
      if(real_packet->spec_gate_count > 255) {
        freelog(LOG_ERROR, "packets_gen.c: WARNING: truncation array");
        real_packet->spec_gate_count = 255;
      }
      for (i = 0; i < real_packet->spec_gate_count; i++) {
        {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->spec_gate[i] = readin;
    }
      }
    }
  }
  if (BV_ISSET(fields, 20)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->equiv_dupl_count = readin;
    }
  }
  if (BV_ISSET(fields, 21)) {
    
    {
      int i;
    
      if(real_packet->equiv_dupl_count > 255) {
        freelog(LOG_ERROR, "packets_gen.c: WARNING: truncation array");
        real_packet->equiv_dupl_count = 255;
      }
      for (i = 0; i < real_packet->equiv_dupl_count; i++) {
        {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->equiv_dupl[i] = readin;
    }
      }
    }
  }
  if (BV_ISSET(fields, 22)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->equiv_repl_count = readin;
    }
  }
  if (BV_ISSET(fields, 23)) {
    
    {
      int i;
    
      if(real_packet->equiv_repl_count > 255) {
        freelog(LOG_ERROR, "packets_gen.c: WARNING: truncation array");
        real_packet->equiv_repl_count = 255;
      }
      for (i = 0; i < real_packet->equiv_repl_count; i++) {
        {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->equiv_repl[i] = readin;
    }
      }
    }
  }

  clone = fc_malloc(sizeof(*clone));
  *clone = *real_packet;
  if (old) {
    free(old);
  }
  hash_insert(*hash, clone, clone);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_ruleset_building_100(struct connection *pc, const struct packet_ruleset_building *packet)
{
  const struct packet_ruleset_building *real_packet = packet;
  packet_ruleset_building_100_fields fields;
  struct packet_ruleset_building *old, *clone;
  bool differ, old_from_hash, force_send_of_unchanged = TRUE;
  struct hash_table **hash = &pc->phs.sent[PACKET_RULESET_BUILDING];
  int different = 0;
  SEND_PACKET_START(PACKET_RULESET_BUILDING);

  if (!*hash) {
    *hash = hash_new(hash_packet_ruleset_building_100, cmp_packet_ruleset_building_100);
  }
  BV_CLR_ALL(fields);

  old = hash_lookup_data(*hash, real_packet);
  old_from_hash = (old != NULL);
  if (!old) {
    old = fc_malloc(sizeof(*old));
    memset(old, 0, sizeof(*old));
    force_send_of_unchanged = TRUE;
  }

  differ = (old->id != real_packet->id);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 0);}

  differ = (strcmp(old->name, real_packet->name) != 0);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 1);}

  differ = (strcmp(old->graphic_str, real_packet->graphic_str) != 0);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 2);}

  differ = (strcmp(old->graphic_alt, real_packet->graphic_alt) != 0);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 3);}

  differ = (old->tech_req != real_packet->tech_req);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 4);}

  differ = (old->obsolete_by != real_packet->obsolete_by);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 5);}

  differ = (old->bldg_req != real_packet->bldg_req);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 6);}

  differ = (old->replaced_by != real_packet->replaced_by);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 7);}

  differ = (old->is_wonder != real_packet->is_wonder);
  if(differ) {different++;}
  if(packet->is_wonder) {BV_SET(fields, 8);}

  differ = (old->equiv_range != real_packet->equiv_range);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 9);}

  differ = (old->build_cost != real_packet->build_cost);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 10);}

  differ = (old->upkeep != real_packet->upkeep);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 11);}

  differ = (old->sabotage != real_packet->sabotage);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 12);}

  differ = (strcmp(old->soundtag, real_packet->soundtag) != 0);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 13);}

  differ = (strcmp(old->soundtag_alt, real_packet->soundtag_alt) != 0);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 14);}

  differ = (strcmp(old->helptext, real_packet->helptext) != 0);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 15);}

  differ = (old->terr_gate_count != real_packet->terr_gate_count);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 16);}


    {
      differ = (old->terr_gate_count != real_packet->terr_gate_count);
      if(!differ) {
        int i;
        for (i = 0; i < real_packet->terr_gate_count; i++) {
          if (old->terr_gate[i] != real_packet->terr_gate[i]) {
            differ = TRUE;
            break;
          }
        }
      }
    }
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 17);}

  differ = (old->spec_gate_count != real_packet->spec_gate_count);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 18);}


    {
      differ = (old->spec_gate_count != real_packet->spec_gate_count);
      if(!differ) {
        int i;
        for (i = 0; i < real_packet->spec_gate_count; i++) {
          if (old->spec_gate[i] != real_packet->spec_gate[i]) {
            differ = TRUE;
            break;
          }
        }
      }
    }
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 19);}

  differ = (old->equiv_dupl_count != real_packet->equiv_dupl_count);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 20);}


    {
      differ = (old->equiv_dupl_count != real_packet->equiv_dupl_count);
      if(!differ) {
        int i;
        for (i = 0; i < real_packet->equiv_dupl_count; i++) {
          if (old->equiv_dupl[i] != real_packet->equiv_dupl[i]) {
            differ = TRUE;
            break;
          }
        }
      }
    }
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 21);}

  differ = (old->equiv_repl_count != real_packet->equiv_repl_count);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 22);}


    {
      differ = (old->equiv_repl_count != real_packet->equiv_repl_count);
      if(!differ) {
        int i;
        for (i = 0; i < real_packet->equiv_repl_count; i++) {
          if (old->equiv_repl[i] != real_packet->equiv_repl[i]) {
            differ = TRUE;
            break;
          }
        }
      }
    }
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 23);}

  if (different == 0 && !force_send_of_unchanged) {
    return 0;
  }

  DIO_BV_PUT(&dout, fields);

  if (BV_ISSET(fields, 0)) {
    dio_put_uint8(&dout, real_packet->id);
  }
  if (BV_ISSET(fields, 1)) {
    dio_put_string(&dout, real_packet->name);
  }
  if (BV_ISSET(fields, 2)) {
    dio_put_string(&dout, real_packet->graphic_str);
  }
  if (BV_ISSET(fields, 3)) {
    dio_put_string(&dout, real_packet->graphic_alt);
  }
  if (BV_ISSET(fields, 4)) {
    dio_put_uint8(&dout, real_packet->tech_req);
  }
  if (BV_ISSET(fields, 5)) {
    dio_put_uint8(&dout, real_packet->obsolete_by);
  }
  if (BV_ISSET(fields, 6)) {
    dio_put_uint8(&dout, real_packet->bldg_req);
  }
  if (BV_ISSET(fields, 7)) {
    dio_put_uint8(&dout, real_packet->replaced_by);
  }
  /* field 8 is folded into the header */
  if (BV_ISSET(fields, 9)) {
    dio_put_uint8(&dout, real_packet->equiv_range);
  }
  if (BV_ISSET(fields, 10)) {
    dio_put_uint16(&dout, real_packet->build_cost);
  }
  if (BV_ISSET(fields, 11)) {
    dio_put_uint8(&dout, real_packet->upkeep);
  }
  if (BV_ISSET(fields, 12)) {
    dio_put_uint8(&dout, real_packet->sabotage);
  }
  if (BV_ISSET(fields, 13)) {
    dio_put_string(&dout, real_packet->soundtag);
  }
  if (BV_ISSET(fields, 14)) {
    dio_put_string(&dout, real_packet->soundtag_alt);
  }
  if (BV_ISSET(fields, 15)) {
    dio_put_string(&dout, real_packet->helptext);
  }
  if (BV_ISSET(fields, 16)) {
    dio_put_uint8(&dout, real_packet->terr_gate_count);
  }
  if (BV_ISSET(fields, 17)) {
  
    {
      int i;

      for (i = 0; i < real_packet->terr_gate_count; i++) {
        dio_put_sint16(&dout, real_packet->terr_gate[i]);
      }
    } 
  }
  if (BV_ISSET(fields, 18)) {
    dio_put_uint8(&dout, real_packet->spec_gate_count);
  }
  if (BV_ISSET(fields, 19)) {
  
    {
      int i;

      for (i = 0; i < real_packet->spec_gate_count; i++) {
        dio_put_uint16(&dout, real_packet->spec_gate[i]);
      }
    } 
  }
  if (BV_ISSET(fields, 20)) {
    dio_put_uint8(&dout, real_packet->equiv_dupl_count);
  }
  if (BV_ISSET(fields, 21)) {
  
    {
      int i;

      for (i = 0; i < real_packet->equiv_dupl_count; i++) {
        dio_put_uint8(&dout, real_packet->equiv_dupl[i]);
      }
    } 
  }
  if (BV_ISSET(fields, 22)) {
    dio_put_uint8(&dout, real_packet->equiv_repl_count);
  }
  if (BV_ISSET(fields, 23)) {
  
    {
      int i;

      for (i = 0; i < real_packet->equiv_repl_count; i++) {
        dio_put_uint8(&dout, real_packet->equiv_repl[i]);
      }
    } 
  }


  if (old_from_hash) {
    hash_delete_entry(*hash, old);
  }

  clone = old;

  *clone = *real_packet;
  hash_insert(*hash, clone, clone);
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_ruleset_building(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_RULESET_BUILDING] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_RULESET_BUILDING] = variant;
}

struct packet_ruleset_building *receive_packet_ruleset_building(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Receiving packet_ruleset_building at the server.");
  }
  ensure_valid_variant_packet_ruleset_building(pc);

  switch(pc->phs.variant[PACKET_RULESET_BUILDING]) {
    case 100: return receive_packet_ruleset_building_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_ruleset_building(struct connection *pc, const struct packet_ruleset_building *packet)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Sending packet_ruleset_building from the client.");
  }
  ensure_valid_variant_packet_ruleset_building(pc);

  switch(pc->phs.variant[PACKET_RULESET_BUILDING]) {
    case 100: return send_packet_ruleset_building_100(pc, packet);
    default: die("unknown variant"); return -1;
  }
}

void lsend_packet_ruleset_building(struct conn_list *dest, const struct packet_ruleset_building *packet)
{
  conn_list_iterate(*dest, pconn) {
    send_packet_ruleset_building(pconn, packet);
  } conn_list_iterate_end;
}

#define hash_packet_ruleset_terrain_100 hash_const

#define cmp_packet_ruleset_terrain_100 cmp_const

BV_DEFINE(packet_ruleset_terrain_100_fields, 38);

static struct packet_ruleset_terrain *receive_packet_ruleset_terrain_100(struct connection *pc, enum packet_type type)
{
  packet_ruleset_terrain_100_fields fields;
  struct packet_ruleset_terrain *old;
  struct hash_table **hash = &pc->phs.received[type];
  struct packet_ruleset_terrain *clone;
  RECEIVE_PACKET_START(packet_ruleset_terrain, real_packet);

  DIO_BV_GET(&din, fields);


  if (!*hash) {
    *hash = hash_new(hash_packet_ruleset_terrain_100, cmp_packet_ruleset_terrain_100);
  }
  old = hash_delete_entry(*hash, real_packet);

  if (old) {
    *real_packet = *old;
  } else {
    memset(real_packet, 0, sizeof(*real_packet));
  }

  if (BV_ISSET(fields, 0)) {
    {
      int readin;
    
      dio_get_sint16(&din, &readin);
      real_packet->id = readin;
    }
  }
  if (BV_ISSET(fields, 1)) {
    DIO_BV_GET(&din, real_packet->flags);
  }
  if (BV_ISSET(fields, 2)) {
    dio_get_string(&din, real_packet->terrain_name, sizeof(real_packet->terrain_name));
  }
  if (BV_ISSET(fields, 3)) {
    dio_get_string(&din, real_packet->graphic_str, sizeof(real_packet->graphic_str));
  }
  if (BV_ISSET(fields, 4)) {
    dio_get_string(&din, real_packet->graphic_alt, sizeof(real_packet->graphic_alt));
  }
  if (BV_ISSET(fields, 5)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->movement_cost = readin;
    }
  }
  if (BV_ISSET(fields, 6)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->defense_bonus = readin;
    }
  }
  if (BV_ISSET(fields, 7)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->food = readin;
    }
  }
  if (BV_ISSET(fields, 8)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->shield = readin;
    }
  }
  if (BV_ISSET(fields, 9)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->trade = readin;
    }
  }
  if (BV_ISSET(fields, 10)) {
    dio_get_string(&din, real_packet->special_1_name, sizeof(real_packet->special_1_name));
  }
  if (BV_ISSET(fields, 11)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->food_special_1 = readin;
    }
  }
  if (BV_ISSET(fields, 12)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->shield_special_1 = readin;
    }
  }
  if (BV_ISSET(fields, 13)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->trade_special_1 = readin;
    }
  }
  if (BV_ISSET(fields, 14)) {
    dio_get_string(&din, real_packet->graphic_str_special_1, sizeof(real_packet->graphic_str_special_1));
  }
  if (BV_ISSET(fields, 15)) {
    dio_get_string(&din, real_packet->graphic_alt_special_1, sizeof(real_packet->graphic_alt_special_1));
  }
  if (BV_ISSET(fields, 16)) {
    dio_get_string(&din, real_packet->special_2_name, sizeof(real_packet->special_2_name));
  }
  if (BV_ISSET(fields, 17)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->food_special_2 = readin;
    }
  }
  if (BV_ISSET(fields, 18)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->shield_special_2 = readin;
    }
  }
  if (BV_ISSET(fields, 19)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->trade_special_2 = readin;
    }
  }
  if (BV_ISSET(fields, 20)) {
    dio_get_string(&din, real_packet->graphic_str_special_2, sizeof(real_packet->graphic_str_special_2));
  }
  if (BV_ISSET(fields, 21)) {
    dio_get_string(&din, real_packet->graphic_alt_special_2, sizeof(real_packet->graphic_alt_special_2));
  }
  if (BV_ISSET(fields, 22)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->road_trade_incr = readin;
    }
  }
  if (BV_ISSET(fields, 23)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->road_time = readin;
    }
  }
  if (BV_ISSET(fields, 24)) {
    {
      int readin;
    
      dio_get_sint16(&din, &readin);
      real_packet->irrigation_result = readin;
    }
  }
  if (BV_ISSET(fields, 25)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->irrigation_food_incr = readin;
    }
  }
  if (BV_ISSET(fields, 26)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->irrigation_time = readin;
    }
  }
  if (BV_ISSET(fields, 27)) {
    {
      int readin;
    
      dio_get_sint16(&din, &readin);
      real_packet->mining_result = readin;
    }
  }
  if (BV_ISSET(fields, 28)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->mining_shield_incr = readin;
    }
  }
  if (BV_ISSET(fields, 29)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->mining_time = readin;
    }
  }
  if (BV_ISSET(fields, 30)) {
    {
      int readin;
    
      dio_get_sint16(&din, &readin);
      real_packet->transform_result = readin;
    }
  }
  if (BV_ISSET(fields, 31)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->transform_time = readin;
    }
  }
  if (BV_ISSET(fields, 32)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->rail_time = readin;
    }
  }
  if (BV_ISSET(fields, 33)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->airbase_time = readin;
    }
  }
  if (BV_ISSET(fields, 34)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->fortress_time = readin;
    }
  }
  if (BV_ISSET(fields, 35)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->clean_pollution_time = readin;
    }
  }
  if (BV_ISSET(fields, 36)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->clean_fallout_time = readin;
    }
  }
  if (BV_ISSET(fields, 37)) {
    dio_get_string(&din, real_packet->helptext, sizeof(real_packet->helptext));
  }

  clone = fc_malloc(sizeof(*clone));
  *clone = *real_packet;
  if (old) {
    free(old);
  }
  hash_insert(*hash, clone, clone);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_ruleset_terrain_100(struct connection *pc, const struct packet_ruleset_terrain *packet)
{
  const struct packet_ruleset_terrain *real_packet = packet;
  packet_ruleset_terrain_100_fields fields;
  struct packet_ruleset_terrain *old, *clone;
  bool differ, old_from_hash, force_send_of_unchanged = TRUE;
  struct hash_table **hash = &pc->phs.sent[PACKET_RULESET_TERRAIN];
  int different = 0;
  SEND_PACKET_START(PACKET_RULESET_TERRAIN);

  if (!*hash) {
    *hash = hash_new(hash_packet_ruleset_terrain_100, cmp_packet_ruleset_terrain_100);
  }
  BV_CLR_ALL(fields);

  old = hash_lookup_data(*hash, real_packet);
  old_from_hash = (old != NULL);
  if (!old) {
    old = fc_malloc(sizeof(*old));
    memset(old, 0, sizeof(*old));
    force_send_of_unchanged = TRUE;
  }

  differ = (old->id != real_packet->id);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 0);}

  differ = !BV_ARE_EQUAL(old->flags, real_packet->flags);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 1);}

  differ = (strcmp(old->terrain_name, real_packet->terrain_name) != 0);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 2);}

  differ = (strcmp(old->graphic_str, real_packet->graphic_str) != 0);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 3);}

  differ = (strcmp(old->graphic_alt, real_packet->graphic_alt) != 0);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 4);}

  differ = (old->movement_cost != real_packet->movement_cost);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 5);}

  differ = (old->defense_bonus != real_packet->defense_bonus);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 6);}

  differ = (old->food != real_packet->food);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 7);}

  differ = (old->shield != real_packet->shield);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 8);}

  differ = (old->trade != real_packet->trade);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 9);}

  differ = (strcmp(old->special_1_name, real_packet->special_1_name) != 0);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 10);}

  differ = (old->food_special_1 != real_packet->food_special_1);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 11);}

  differ = (old->shield_special_1 != real_packet->shield_special_1);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 12);}

  differ = (old->trade_special_1 != real_packet->trade_special_1);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 13);}

  differ = (strcmp(old->graphic_str_special_1, real_packet->graphic_str_special_1) != 0);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 14);}

  differ = (strcmp(old->graphic_alt_special_1, real_packet->graphic_alt_special_1) != 0);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 15);}

  differ = (strcmp(old->special_2_name, real_packet->special_2_name) != 0);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 16);}

  differ = (old->food_special_2 != real_packet->food_special_2);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 17);}

  differ = (old->shield_special_2 != real_packet->shield_special_2);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 18);}

  differ = (old->trade_special_2 != real_packet->trade_special_2);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 19);}

  differ = (strcmp(old->graphic_str_special_2, real_packet->graphic_str_special_2) != 0);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 20);}

  differ = (strcmp(old->graphic_alt_special_2, real_packet->graphic_alt_special_2) != 0);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 21);}

  differ = (old->road_trade_incr != real_packet->road_trade_incr);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 22);}

  differ = (old->road_time != real_packet->road_time);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 23);}

  differ = (old->irrigation_result != real_packet->irrigation_result);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 24);}

  differ = (old->irrigation_food_incr != real_packet->irrigation_food_incr);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 25);}

  differ = (old->irrigation_time != real_packet->irrigation_time);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 26);}

  differ = (old->mining_result != real_packet->mining_result);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 27);}

  differ = (old->mining_shield_incr != real_packet->mining_shield_incr);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 28);}

  differ = (old->mining_time != real_packet->mining_time);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 29);}

  differ = (old->transform_result != real_packet->transform_result);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 30);}

  differ = (old->transform_time != real_packet->transform_time);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 31);}

  differ = (old->rail_time != real_packet->rail_time);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 32);}

  differ = (old->airbase_time != real_packet->airbase_time);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 33);}

  differ = (old->fortress_time != real_packet->fortress_time);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 34);}

  differ = (old->clean_pollution_time != real_packet->clean_pollution_time);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 35);}

  differ = (old->clean_fallout_time != real_packet->clean_fallout_time);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 36);}

  differ = (strcmp(old->helptext, real_packet->helptext) != 0);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 37);}

  if (different == 0 && !force_send_of_unchanged) {
    return 0;
  }

  DIO_BV_PUT(&dout, fields);

  if (BV_ISSET(fields, 0)) {
    dio_put_sint16(&dout, real_packet->id);
  }
  if (BV_ISSET(fields, 1)) {
  DIO_BV_PUT(&dout, packet->flags);
  }
  if (BV_ISSET(fields, 2)) {
    dio_put_string(&dout, real_packet->terrain_name);
  }
  if (BV_ISSET(fields, 3)) {
    dio_put_string(&dout, real_packet->graphic_str);
  }
  if (BV_ISSET(fields, 4)) {
    dio_put_string(&dout, real_packet->graphic_alt);
  }
  if (BV_ISSET(fields, 5)) {
    dio_put_uint8(&dout, real_packet->movement_cost);
  }
  if (BV_ISSET(fields, 6)) {
    dio_put_uint8(&dout, real_packet->defense_bonus);
  }
  if (BV_ISSET(fields, 7)) {
    dio_put_uint8(&dout, real_packet->food);
  }
  if (BV_ISSET(fields, 8)) {
    dio_put_uint8(&dout, real_packet->shield);
  }
  if (BV_ISSET(fields, 9)) {
    dio_put_uint8(&dout, real_packet->trade);
  }
  if (BV_ISSET(fields, 10)) {
    dio_put_string(&dout, real_packet->special_1_name);
  }
  if (BV_ISSET(fields, 11)) {
    dio_put_uint8(&dout, real_packet->food_special_1);
  }
  if (BV_ISSET(fields, 12)) {
    dio_put_uint8(&dout, real_packet->shield_special_1);
  }
  if (BV_ISSET(fields, 13)) {
    dio_put_uint8(&dout, real_packet->trade_special_1);
  }
  if (BV_ISSET(fields, 14)) {
    dio_put_string(&dout, real_packet->graphic_str_special_1);
  }
  if (BV_ISSET(fields, 15)) {
    dio_put_string(&dout, real_packet->graphic_alt_special_1);
  }
  if (BV_ISSET(fields, 16)) {
    dio_put_string(&dout, real_packet->special_2_name);
  }
  if (BV_ISSET(fields, 17)) {
    dio_put_uint8(&dout, real_packet->food_special_2);
  }
  if (BV_ISSET(fields, 18)) {
    dio_put_uint8(&dout, real_packet->shield_special_2);
  }
  if (BV_ISSET(fields, 19)) {
    dio_put_uint8(&dout, real_packet->trade_special_2);
  }
  if (BV_ISSET(fields, 20)) {
    dio_put_string(&dout, real_packet->graphic_str_special_2);
  }
  if (BV_ISSET(fields, 21)) {
    dio_put_string(&dout, real_packet->graphic_alt_special_2);
  }
  if (BV_ISSET(fields, 22)) {
    dio_put_uint8(&dout, real_packet->road_trade_incr);
  }
  if (BV_ISSET(fields, 23)) {
    dio_put_uint8(&dout, real_packet->road_time);
  }
  if (BV_ISSET(fields, 24)) {
    dio_put_sint16(&dout, real_packet->irrigation_result);
  }
  if (BV_ISSET(fields, 25)) {
    dio_put_uint8(&dout, real_packet->irrigation_food_incr);
  }
  if (BV_ISSET(fields, 26)) {
    dio_put_uint8(&dout, real_packet->irrigation_time);
  }
  if (BV_ISSET(fields, 27)) {
    dio_put_sint16(&dout, real_packet->mining_result);
  }
  if (BV_ISSET(fields, 28)) {
    dio_put_uint8(&dout, real_packet->mining_shield_incr);
  }
  if (BV_ISSET(fields, 29)) {
    dio_put_uint8(&dout, real_packet->mining_time);
  }
  if (BV_ISSET(fields, 30)) {
    dio_put_sint16(&dout, real_packet->transform_result);
  }
  if (BV_ISSET(fields, 31)) {
    dio_put_uint8(&dout, real_packet->transform_time);
  }
  if (BV_ISSET(fields, 32)) {
    dio_put_uint8(&dout, real_packet->rail_time);
  }
  if (BV_ISSET(fields, 33)) {
    dio_put_uint8(&dout, real_packet->airbase_time);
  }
  if (BV_ISSET(fields, 34)) {
    dio_put_uint8(&dout, real_packet->fortress_time);
  }
  if (BV_ISSET(fields, 35)) {
    dio_put_uint8(&dout, real_packet->clean_pollution_time);
  }
  if (BV_ISSET(fields, 36)) {
    dio_put_uint8(&dout, real_packet->clean_fallout_time);
  }
  if (BV_ISSET(fields, 37)) {
    dio_put_string(&dout, real_packet->helptext);
  }


  if (old_from_hash) {
    hash_delete_entry(*hash, old);
  }

  clone = old;

  *clone = *real_packet;
  hash_insert(*hash, clone, clone);
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_ruleset_terrain(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_RULESET_TERRAIN] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_RULESET_TERRAIN] = variant;
}

struct packet_ruleset_terrain *receive_packet_ruleset_terrain(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Receiving packet_ruleset_terrain at the server.");
  }
  ensure_valid_variant_packet_ruleset_terrain(pc);

  switch(pc->phs.variant[PACKET_RULESET_TERRAIN]) {
    case 100: return receive_packet_ruleset_terrain_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_ruleset_terrain(struct connection *pc, const struct packet_ruleset_terrain *packet)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Sending packet_ruleset_terrain from the client.");
  }
  ensure_valid_variant_packet_ruleset_terrain(pc);

  switch(pc->phs.variant[PACKET_RULESET_TERRAIN]) {
    case 100: return send_packet_ruleset_terrain_100(pc, packet);
    default: die("unknown variant"); return -1;
  }
}

void lsend_packet_ruleset_terrain(struct conn_list *dest, const struct packet_ruleset_terrain *packet)
{
  conn_list_iterate(*dest, pconn) {
    send_packet_ruleset_terrain(pconn, packet);
  } conn_list_iterate_end;
}

#define hash_packet_ruleset_control_100 hash_const

#define cmp_packet_ruleset_control_100 cmp_const

BV_DEFINE(packet_ruleset_control_100_fields, 24);

static struct packet_ruleset_control *receive_packet_ruleset_control_100(struct connection *pc, enum packet_type type)
{
  packet_ruleset_control_100_fields fields;
  struct packet_ruleset_control *old;
  struct hash_table **hash = &pc->phs.received[type];
  struct packet_ruleset_control *clone;
  RECEIVE_PACKET_START(packet_ruleset_control, real_packet);

  DIO_BV_GET(&din, fields);


  if (!*hash) {
    *hash = hash_new(hash_packet_ruleset_control_100, cmp_packet_ruleset_control_100);
  }
  old = hash_delete_entry(*hash, real_packet);

  if (old) {
    *real_packet = *old;
  } else {
    memset(real_packet, 0, sizeof(*real_packet));
  }

  if (BV_ISSET(fields, 0)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->aqueduct_size = readin;
    }
  }
  if (BV_ISSET(fields, 1)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->add_to_size_limit = readin;
    }
  }
  if (BV_ISSET(fields, 2)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->notradesize = readin;
    }
  }
  if (BV_ISSET(fields, 3)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->fulltradesize = readin;
    }
  }
  if (BV_ISSET(fields, 4)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->num_unit_types = readin;
    }
  }
  if (BV_ISSET(fields, 5)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->num_impr_types = readin;
    }
  }
  if (BV_ISSET(fields, 6)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->num_tech_types = readin;
    }
  }
  if (BV_ISSET(fields, 7)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->rtech_cathedral_plus = readin;
    }
  }
  if (BV_ISSET(fields, 8)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->rtech_cathedral_minus = readin;
    }
  }
  if (BV_ISSET(fields, 9)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->rtech_colosseum_plus = readin;
    }
  }
  if (BV_ISSET(fields, 10)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->rtech_temple_plus = readin;
    }
  }
  if (BV_ISSET(fields, 11)) {
    dio_get_tech_list(&din, real_packet->rtech_partisan_req);
  }
  if (BV_ISSET(fields, 12)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->government_when_anarchy = readin;
    }
  }
  if (BV_ISSET(fields, 13)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->default_government = readin;
    }
  }
  if (BV_ISSET(fields, 14)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->government_count = readin;
    }
  }
  if (BV_ISSET(fields, 15)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->nation_count = readin;
    }
  }
  if (BV_ISSET(fields, 16)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->playable_nation_count = readin;
    }
  }
  if (BV_ISSET(fields, 17)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->style_count = readin;
    }
  }
  if (BV_ISSET(fields, 18)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->terrain_count = readin;
    }
  }
  if (BV_ISSET(fields, 19)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->borders = readin;
    }
  }
  real_packet->happyborders = BV_ISSET(fields, 20);
  real_packet->slow_invasions = BV_ISSET(fields, 21);
  if (BV_ISSET(fields, 22)) {
    
    {
      int i;
    
      for (i = 0; i < MAX_NUM_TEAMS; i++) {
        dio_get_string(&din, real_packet->team_name[i], sizeof(real_packet->team_name[i]));
      }
    }
  }
  if (BV_ISSET(fields, 23)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->default_building = readin;
    }
  }

  clone = fc_malloc(sizeof(*clone));
  *clone = *real_packet;
  if (old) {
    free(old);
  }
  hash_insert(*hash, clone, clone);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_ruleset_control_100(struct connection *pc, const struct packet_ruleset_control *packet)
{
  const struct packet_ruleset_control *real_packet = packet;
  packet_ruleset_control_100_fields fields;
  struct packet_ruleset_control *old, *clone;
  bool differ, old_from_hash, force_send_of_unchanged = TRUE;
  struct hash_table **hash = &pc->phs.sent[PACKET_RULESET_CONTROL];
  int different = 0;
  SEND_PACKET_START(PACKET_RULESET_CONTROL);

  if (!*hash) {
    *hash = hash_new(hash_packet_ruleset_control_100, cmp_packet_ruleset_control_100);
  }
  BV_CLR_ALL(fields);

  old = hash_lookup_data(*hash, real_packet);
  old_from_hash = (old != NULL);
  if (!old) {
    old = fc_malloc(sizeof(*old));
    memset(old, 0, sizeof(*old));
    force_send_of_unchanged = TRUE;
  }

  differ = (old->aqueduct_size != real_packet->aqueduct_size);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 0);}

  differ = (old->add_to_size_limit != real_packet->add_to_size_limit);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 1);}

  differ = (old->notradesize != real_packet->notradesize);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 2);}

  differ = (old->fulltradesize != real_packet->fulltradesize);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 3);}

  differ = (old->num_unit_types != real_packet->num_unit_types);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 4);}

  differ = (old->num_impr_types != real_packet->num_impr_types);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 5);}

  differ = (old->num_tech_types != real_packet->num_tech_types);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 6);}

  differ = (old->rtech_cathedral_plus != real_packet->rtech_cathedral_plus);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 7);}

  differ = (old->rtech_cathedral_minus != real_packet->rtech_cathedral_minus);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 8);}

  differ = (old->rtech_colosseum_plus != real_packet->rtech_colosseum_plus);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 9);}

  differ = (old->rtech_temple_plus != real_packet->rtech_temple_plus);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 10);}


    {
      differ = (MAX_NUM_TECH_LIST != MAX_NUM_TECH_LIST);
      if(!differ) {
        int i;
        for (i = 0; i < MAX_NUM_TECH_LIST; i++) {
          if (old->rtech_partisan_req[i] != real_packet->rtech_partisan_req[i]) {
            differ = TRUE;
            break;
          }
        }
      }
    }
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 11);}

  differ = (old->government_when_anarchy != real_packet->government_when_anarchy);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 12);}

  differ = (old->default_government != real_packet->default_government);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 13);}

  differ = (old->government_count != real_packet->government_count);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 14);}

  differ = (old->nation_count != real_packet->nation_count);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 15);}

  differ = (old->playable_nation_count != real_packet->playable_nation_count);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 16);}

  differ = (old->style_count != real_packet->style_count);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 17);}

  differ = (old->terrain_count != real_packet->terrain_count);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 18);}

  differ = (old->borders != real_packet->borders);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 19);}

  differ = (old->happyborders != real_packet->happyborders);
  if(differ) {different++;}
  if(packet->happyborders) {BV_SET(fields, 20);}

  differ = (old->slow_invasions != real_packet->slow_invasions);
  if(differ) {different++;}
  if(packet->slow_invasions) {BV_SET(fields, 21);}


    {
      differ = (MAX_NUM_TEAMS != MAX_NUM_TEAMS);
      if(!differ) {
        int i;
        for (i = 0; i < MAX_NUM_TEAMS; i++) {
          if (strcmp(old->team_name[i], real_packet->team_name[i]) != 0) {
            differ = TRUE;
            break;
          }
        }
      }
    }
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 22);}

  differ = (old->default_building != real_packet->default_building);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 23);}

  if (different == 0 && !force_send_of_unchanged) {
    return 0;
  }

  DIO_BV_PUT(&dout, fields);

  if (BV_ISSET(fields, 0)) {
    dio_put_uint8(&dout, real_packet->aqueduct_size);
  }
  if (BV_ISSET(fields, 1)) {
    dio_put_uint8(&dout, real_packet->add_to_size_limit);
  }
  if (BV_ISSET(fields, 2)) {
    dio_put_uint8(&dout, real_packet->notradesize);
  }
  if (BV_ISSET(fields, 3)) {
    dio_put_uint8(&dout, real_packet->fulltradesize);
  }
  if (BV_ISSET(fields, 4)) {
    dio_put_uint8(&dout, real_packet->num_unit_types);
  }
  if (BV_ISSET(fields, 5)) {
    dio_put_uint8(&dout, real_packet->num_impr_types);
  }
  if (BV_ISSET(fields, 6)) {
    dio_put_uint8(&dout, real_packet->num_tech_types);
  }
  if (BV_ISSET(fields, 7)) {
    dio_put_uint8(&dout, real_packet->rtech_cathedral_plus);
  }
  if (BV_ISSET(fields, 8)) {
    dio_put_uint8(&dout, real_packet->rtech_cathedral_minus);
  }
  if (BV_ISSET(fields, 9)) {
    dio_put_uint8(&dout, real_packet->rtech_colosseum_plus);
  }
  if (BV_ISSET(fields, 10)) {
    dio_put_uint8(&dout, real_packet->rtech_temple_plus);
  }
  if (BV_ISSET(fields, 11)) {
    dio_put_tech_list(&dout, real_packet->rtech_partisan_req);
  }
  if (BV_ISSET(fields, 12)) {
    dio_put_uint8(&dout, real_packet->government_when_anarchy);
  }
  if (BV_ISSET(fields, 13)) {
    dio_put_uint8(&dout, real_packet->default_government);
  }
  if (BV_ISSET(fields, 14)) {
    dio_put_uint8(&dout, real_packet->government_count);
  }
  if (BV_ISSET(fields, 15)) {
    dio_put_uint8(&dout, real_packet->nation_count);
  }
  if (BV_ISSET(fields, 16)) {
    dio_put_uint8(&dout, real_packet->playable_nation_count);
  }
  if (BV_ISSET(fields, 17)) {
    dio_put_uint8(&dout, real_packet->style_count);
  }
  if (BV_ISSET(fields, 18)) {
    dio_put_uint8(&dout, real_packet->terrain_count);
  }
  if (BV_ISSET(fields, 19)) {
    dio_put_uint8(&dout, real_packet->borders);
  }
  /* field 20 is folded into the header */
  /* field 21 is folded into the header */
  if (BV_ISSET(fields, 22)) {
  
    {
      int i;

      for (i = 0; i < MAX_NUM_TEAMS; i++) {
        dio_put_string(&dout, real_packet->team_name[i]);
      }
    } 
  }
  if (BV_ISSET(fields, 23)) {
    dio_put_uint8(&dout, real_packet->default_building);
  }


  if (old_from_hash) {
    hash_delete_entry(*hash, old);
  }

  clone = old;

  *clone = *real_packet;
  hash_insert(*hash, clone, clone);
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_ruleset_control(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_RULESET_CONTROL] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_RULESET_CONTROL] = variant;
}

struct packet_ruleset_control *receive_packet_ruleset_control(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Receiving packet_ruleset_control at the server.");
  }
  ensure_valid_variant_packet_ruleset_control(pc);

  switch(pc->phs.variant[PACKET_RULESET_CONTROL]) {
    case 100: return receive_packet_ruleset_control_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_ruleset_control(struct connection *pc, const struct packet_ruleset_control *packet)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Sending packet_ruleset_control from the client.");
  }
  ensure_valid_variant_packet_ruleset_control(pc);

  switch(pc->phs.variant[PACKET_RULESET_CONTROL]) {
    case 100: return send_packet_ruleset_control_100(pc, packet);
    default: die("unknown variant"); return -1;
  }
}

void lsend_packet_ruleset_control(struct conn_list *dest, const struct packet_ruleset_control *packet)
{
  conn_list_iterate(*dest, pconn) {
    send_packet_ruleset_control(pconn, packet);
  } conn_list_iterate_end;
}

#define hash_packet_single_want_hack_req_100 hash_const

#define cmp_packet_single_want_hack_req_100 cmp_const

BV_DEFINE(packet_single_want_hack_req_100_fields, 1);

static struct packet_single_want_hack_req *receive_packet_single_want_hack_req_100(struct connection *pc, enum packet_type type)
{
  packet_single_want_hack_req_100_fields fields;
  struct packet_single_want_hack_req *old;
  struct hash_table **hash = &pc->phs.received[type];
  struct packet_single_want_hack_req *clone;
  RECEIVE_PACKET_START(packet_single_want_hack_req, real_packet);

  DIO_BV_GET(&din, fields);


  if (!*hash) {
    *hash = hash_new(hash_packet_single_want_hack_req_100, cmp_packet_single_want_hack_req_100);
  }
  old = hash_delete_entry(*hash, real_packet);

  if (old) {
    *real_packet = *old;
  } else {
    memset(real_packet, 0, sizeof(*real_packet));
  }

  if (BV_ISSET(fields, 0)) {
    dio_get_string(&din, real_packet->token, sizeof(real_packet->token));
  }

  clone = fc_malloc(sizeof(*clone));
  *clone = *real_packet;
  if (old) {
    free(old);
  }
  hash_insert(*hash, clone, clone);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_single_want_hack_req_100(struct connection *pc, const struct packet_single_want_hack_req *packet)
{
  const struct packet_single_want_hack_req *real_packet = packet;
  packet_single_want_hack_req_100_fields fields;
  struct packet_single_want_hack_req *old, *clone;
  bool differ, old_from_hash, force_send_of_unchanged = TRUE;
  struct hash_table **hash = &pc->phs.sent[PACKET_SINGLE_WANT_HACK_REQ];
  int different = 0;
  SEND_PACKET_START(PACKET_SINGLE_WANT_HACK_REQ);

  if (!*hash) {
    *hash = hash_new(hash_packet_single_want_hack_req_100, cmp_packet_single_want_hack_req_100);
  }
  BV_CLR_ALL(fields);

  old = hash_lookup_data(*hash, real_packet);
  old_from_hash = (old != NULL);
  if (!old) {
    old = fc_malloc(sizeof(*old));
    memset(old, 0, sizeof(*old));
    force_send_of_unchanged = TRUE;
  }

  differ = (strcmp(old->token, real_packet->token) != 0);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 0);}

  if (different == 0 && !force_send_of_unchanged) {
    return 0;
  }

  DIO_BV_PUT(&dout, fields);

  if (BV_ISSET(fields, 0)) {
    dio_put_string(&dout, real_packet->token);
  }


  if (old_from_hash) {
    hash_delete_entry(*hash, old);
  }

  clone = old;

  *clone = *real_packet;
  hash_insert(*hash, clone, clone);
  SEND_PACKET_END;
}

#define hash_packet_single_want_hack_req_101 hash_const

#define cmp_packet_single_want_hack_req_101 cmp_const

BV_DEFINE(packet_single_want_hack_req_101_fields, 1);

static struct packet_single_want_hack_req *receive_packet_single_want_hack_req_101(struct connection *pc, enum packet_type type)
{
  packet_single_want_hack_req_101_fields fields;
  struct packet_single_want_hack_req *old;
  struct hash_table **hash = &pc->phs.received[type];
  struct packet_single_want_hack_req *clone;
  RECEIVE_PACKET_START(packet_single_want_hack_req, real_packet);

  DIO_BV_GET(&din, fields);


  if (!*hash) {
    *hash = hash_new(hash_packet_single_want_hack_req_101, cmp_packet_single_want_hack_req_101);
  }
  old = hash_delete_entry(*hash, real_packet);

  if (old) {
    *real_packet = *old;
  } else {
    memset(real_packet, 0, sizeof(*real_packet));
  }

  if (BV_ISSET(fields, 0)) {
    {
      int readin;
    
      dio_get_uint32(&din, &readin);
      real_packet->old_token = readin;
    }
  }

  clone = fc_malloc(sizeof(*clone));
  *clone = *real_packet;
  if (old) {
    free(old);
  }
  hash_insert(*hash, clone, clone);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_single_want_hack_req_101(struct connection *pc, const struct packet_single_want_hack_req *packet)
{
  const struct packet_single_want_hack_req *real_packet = packet;
  packet_single_want_hack_req_101_fields fields;
  struct packet_single_want_hack_req *old, *clone;
  bool differ, old_from_hash, force_send_of_unchanged = TRUE;
  struct hash_table **hash = &pc->phs.sent[PACKET_SINGLE_WANT_HACK_REQ];
  int different = 0;
  SEND_PACKET_START(PACKET_SINGLE_WANT_HACK_REQ);

  if (!*hash) {
    *hash = hash_new(hash_packet_single_want_hack_req_101, cmp_packet_single_want_hack_req_101);
  }
  BV_CLR_ALL(fields);

  old = hash_lookup_data(*hash, real_packet);
  old_from_hash = (old != NULL);
  if (!old) {
    old = fc_malloc(sizeof(*old));
    memset(old, 0, sizeof(*old));
    force_send_of_unchanged = TRUE;
  }

  differ = (old->old_token != real_packet->old_token);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 0);}

  if (different == 0 && !force_send_of_unchanged) {
    return 0;
  }

  DIO_BV_PUT(&dout, fields);

  if (BV_ISSET(fields, 0)) {
    dio_put_uint32(&dout, real_packet->old_token);
  }


  if (old_from_hash) {
    hash_delete_entry(*hash, old);
  }

  clone = old;

  *clone = *real_packet;
  hash_insert(*hash, clone, clone);
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_single_want_hack_req(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_SINGLE_WANT_HACK_REQ] != -1) {
    return;
  }

  if(FALSE) {
  } else if((has_capability("new_hack", pc->capability) && has_capability("new_hack", our_capability))) {
    variant = 100;
  } else if(!(has_capability("new_hack", pc->capability) && has_capability("new_hack", our_capability))) {
    variant = 101;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_SINGLE_WANT_HACK_REQ] = variant;
}

struct packet_single_want_hack_req *receive_packet_single_want_hack_req(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Receiving packet_single_want_hack_req at the client.");
  }
  ensure_valid_variant_packet_single_want_hack_req(pc);

  switch(pc->phs.variant[PACKET_SINGLE_WANT_HACK_REQ]) {
    case 100: return receive_packet_single_want_hack_req_100(pc, type);
    case 101: return receive_packet_single_want_hack_req_101(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_single_want_hack_req(struct connection *pc, const struct packet_single_want_hack_req *packet)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Sending packet_single_want_hack_req from the server.");
  }
  ensure_valid_variant_packet_single_want_hack_req(pc);

  switch(pc->phs.variant[PACKET_SINGLE_WANT_HACK_REQ]) {
    case 100: return send_packet_single_want_hack_req_100(pc, packet);
    case 101: return send_packet_single_want_hack_req_101(pc, packet);
    default: die("unknown variant"); return -1;
  }
}

#define hash_packet_single_want_hack_reply_100 hash_const

#define cmp_packet_single_want_hack_reply_100 cmp_const

BV_DEFINE(packet_single_want_hack_reply_100_fields, 1);

static struct packet_single_want_hack_reply *receive_packet_single_want_hack_reply_100(struct connection *pc, enum packet_type type)
{
  packet_single_want_hack_reply_100_fields fields;
  struct packet_single_want_hack_reply *old;
  struct hash_table **hash = &pc->phs.received[type];
  struct packet_single_want_hack_reply *clone;
  RECEIVE_PACKET_START(packet_single_want_hack_reply, real_packet);

  DIO_BV_GET(&din, fields);


  if (!*hash) {
    *hash = hash_new(hash_packet_single_want_hack_reply_100, cmp_packet_single_want_hack_reply_100);
  }
  old = hash_delete_entry(*hash, real_packet);

  if (old) {
    *real_packet = *old;
  } else {
    memset(real_packet, 0, sizeof(*real_packet));
  }

  real_packet->you_have_hack = BV_ISSET(fields, 0);

  clone = fc_malloc(sizeof(*clone));
  *clone = *real_packet;
  if (old) {
    free(old);
  }
  hash_insert(*hash, clone, clone);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_single_want_hack_reply_100(struct connection *pc, const struct packet_single_want_hack_reply *packet)
{
  const struct packet_single_want_hack_reply *real_packet = packet;
  packet_single_want_hack_reply_100_fields fields;
  struct packet_single_want_hack_reply *old, *clone;
  bool differ, old_from_hash, force_send_of_unchanged = TRUE;
  struct hash_table **hash = &pc->phs.sent[PACKET_SINGLE_WANT_HACK_REPLY];
  int different = 0;
  SEND_PACKET_START(PACKET_SINGLE_WANT_HACK_REPLY);

  if (!*hash) {
    *hash = hash_new(hash_packet_single_want_hack_reply_100, cmp_packet_single_want_hack_reply_100);
  }
  BV_CLR_ALL(fields);

  old = hash_lookup_data(*hash, real_packet);
  old_from_hash = (old != NULL);
  if (!old) {
    old = fc_malloc(sizeof(*old));
    memset(old, 0, sizeof(*old));
    force_send_of_unchanged = TRUE;
  }

  differ = (old->you_have_hack != real_packet->you_have_hack);
  if(differ) {different++;}
  if(packet->you_have_hack) {BV_SET(fields, 0);}

  if (different == 0 && !force_send_of_unchanged) {
    return 0;
  }

  DIO_BV_PUT(&dout, fields);

  /* field 0 is folded into the header */


  if (old_from_hash) {
    hash_delete_entry(*hash, old);
  }

  clone = old;

  *clone = *real_packet;
  hash_insert(*hash, clone, clone);
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_single_want_hack_reply(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_SINGLE_WANT_HACK_REPLY] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_SINGLE_WANT_HACK_REPLY] = variant;
}

struct packet_single_want_hack_reply *receive_packet_single_want_hack_reply(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Receiving packet_single_want_hack_reply at the server.");
  }
  ensure_valid_variant_packet_single_want_hack_reply(pc);

  switch(pc->phs.variant[PACKET_SINGLE_WANT_HACK_REPLY]) {
    case 100: return receive_packet_single_want_hack_reply_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_single_want_hack_reply(struct connection *pc, const struct packet_single_want_hack_reply *packet)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Sending packet_single_want_hack_reply from the client.");
  }
  ensure_valid_variant_packet_single_want_hack_reply(pc);

  switch(pc->phs.variant[PACKET_SINGLE_WANT_HACK_REPLY]) {
    case 100: return send_packet_single_want_hack_reply_100(pc, packet);
    default: die("unknown variant"); return -1;
  }
}

int dsend_packet_single_want_hack_reply(struct connection *pc, bool you_have_hack)
{
  struct packet_single_want_hack_reply packet, *real_packet = &packet;

  real_packet->you_have_hack = you_have_hack;
  
  return send_packet_single_want_hack_reply(pc, real_packet);
}

#define hash_packet_game_load_100 hash_const

#define cmp_packet_game_load_100 cmp_const

BV_DEFINE(packet_game_load_100_fields, 9);

static struct packet_game_load *receive_packet_game_load_100(struct connection *pc, enum packet_type type)
{
  packet_game_load_100_fields fields;
  struct packet_game_load *old;
  struct hash_table **hash = &pc->phs.received[type];
  struct packet_game_load *clone;
  RECEIVE_PACKET_START(packet_game_load, real_packet);

  DIO_BV_GET(&din, fields);


  if (!*hash) {
    *hash = hash_new(hash_packet_game_load_100, cmp_packet_game_load_100);
  }
  old = hash_delete_entry(*hash, real_packet);

  if (old) {
    *real_packet = *old;
  } else {
    memset(real_packet, 0, sizeof(*real_packet));
  }

  real_packet->load_successful = BV_ISSET(fields, 0);
  if (BV_ISSET(fields, 1)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->nplayers = readin;
    }
  }
  if (BV_ISSET(fields, 2)) {
    dio_get_string(&din, real_packet->load_filename, sizeof(real_packet->load_filename));
  }
  if (BV_ISSET(fields, 3)) {
    
    {
      int i;
    
      if(real_packet->nplayers > MAX_NUM_PLAYERS) {
        freelog(LOG_ERROR, "packets_gen.c: WARNING: truncation array");
        real_packet->nplayers = MAX_NUM_PLAYERS;
      }
      for (i = 0; i < real_packet->nplayers; i++) {
        dio_get_string(&din, real_packet->name[i], sizeof(real_packet->name[i]));
      }
    }
  }
  if (BV_ISSET(fields, 4)) {
    
    {
      int i;
    
      if(real_packet->nplayers > MAX_NUM_PLAYERS) {
        freelog(LOG_ERROR, "packets_gen.c: WARNING: truncation array");
        real_packet->nplayers = MAX_NUM_PLAYERS;
      }
      for (i = 0; i < real_packet->nplayers; i++) {
        dio_get_string(&din, real_packet->username[i], sizeof(real_packet->username[i]));
      }
    }
  }
  if (BV_ISSET(fields, 5)) {
    
    {
      int i;
    
      if(real_packet->nplayers > MAX_NUM_PLAYERS) {
        freelog(LOG_ERROR, "packets_gen.c: WARNING: truncation array");
        real_packet->nplayers = MAX_NUM_PLAYERS;
      }
      for (i = 0; i < real_packet->nplayers; i++) {
        dio_get_string(&din, real_packet->nation_name[i], sizeof(real_packet->nation_name[i]));
      }
    }
  }
  if (BV_ISSET(fields, 6)) {
    
    {
      int i;
    
      if(real_packet->nplayers > MAX_NUM_PLAYERS) {
        freelog(LOG_ERROR, "packets_gen.c: WARNING: truncation array");
        real_packet->nplayers = MAX_NUM_PLAYERS;
      }
      for (i = 0; i < real_packet->nplayers; i++) {
        dio_get_string(&din, real_packet->nation_flag[i], sizeof(real_packet->nation_flag[i]));
      }
    }
  }
  if (BV_ISSET(fields, 7)) {
    
    {
      int i;
    
      if(real_packet->nplayers > MAX_NUM_PLAYERS) {
        freelog(LOG_ERROR, "packets_gen.c: WARNING: truncation array");
        real_packet->nplayers = MAX_NUM_PLAYERS;
      }
      for (i = 0; i < real_packet->nplayers; i++) {
        dio_get_bool8(&din, &real_packet->is_alive[i]);
      }
    }
  }
  if (BV_ISSET(fields, 8)) {
    
    {
      int i;
    
      if(real_packet->nplayers > MAX_NUM_PLAYERS) {
        freelog(LOG_ERROR, "packets_gen.c: WARNING: truncation array");
        real_packet->nplayers = MAX_NUM_PLAYERS;
      }
      for (i = 0; i < real_packet->nplayers; i++) {
        dio_get_bool8(&din, &real_packet->is_ai[i]);
      }
    }
  }

  clone = fc_malloc(sizeof(*clone));
  *clone = *real_packet;
  if (old) {
    free(old);
  }
  hash_insert(*hash, clone, clone);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_game_load_100(struct connection *pc, const struct packet_game_load *packet)
{
  const struct packet_game_load *real_packet = packet;
  packet_game_load_100_fields fields;
  struct packet_game_load *old, *clone;
  bool differ, old_from_hash, force_send_of_unchanged = TRUE;
  struct hash_table **hash = &pc->phs.sent[PACKET_GAME_LOAD];
  int different = 0;
  SEND_PACKET_START(PACKET_GAME_LOAD);

  if (!*hash) {
    *hash = hash_new(hash_packet_game_load_100, cmp_packet_game_load_100);
  }
  BV_CLR_ALL(fields);

  old = hash_lookup_data(*hash, real_packet);
  old_from_hash = (old != NULL);
  if (!old) {
    old = fc_malloc(sizeof(*old));
    memset(old, 0, sizeof(*old));
    force_send_of_unchanged = TRUE;
  }

  differ = (old->load_successful != real_packet->load_successful);
  if(differ) {different++;}
  if(packet->load_successful) {BV_SET(fields, 0);}

  differ = (old->nplayers != real_packet->nplayers);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 1);}

  differ = (strcmp(old->load_filename, real_packet->load_filename) != 0);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 2);}


    {
      differ = (old->nplayers != real_packet->nplayers);
      if(!differ) {
        int i;
        for (i = 0; i < real_packet->nplayers; i++) {
          if (strcmp(old->name[i], real_packet->name[i]) != 0) {
            differ = TRUE;
            break;
          }
        }
      }
    }
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 3);}


    {
      differ = (old->nplayers != real_packet->nplayers);
      if(!differ) {
        int i;
        for (i = 0; i < real_packet->nplayers; i++) {
          if (strcmp(old->username[i], real_packet->username[i]) != 0) {
            differ = TRUE;
            break;
          }
        }
      }
    }
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 4);}


    {
      differ = (old->nplayers != real_packet->nplayers);
      if(!differ) {
        int i;
        for (i = 0; i < real_packet->nplayers; i++) {
          if (strcmp(old->nation_name[i], real_packet->nation_name[i]) != 0) {
            differ = TRUE;
            break;
          }
        }
      }
    }
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 5);}


    {
      differ = (old->nplayers != real_packet->nplayers);
      if(!differ) {
        int i;
        for (i = 0; i < real_packet->nplayers; i++) {
          if (strcmp(old->nation_flag[i], real_packet->nation_flag[i]) != 0) {
            differ = TRUE;
            break;
          }
        }
      }
    }
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 6);}


    {
      differ = (old->nplayers != real_packet->nplayers);
      if(!differ) {
        int i;
        for (i = 0; i < real_packet->nplayers; i++) {
          if (old->is_alive[i] != real_packet->is_alive[i]) {
            differ = TRUE;
            break;
          }
        }
      }
    }
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 7);}


    {
      differ = (old->nplayers != real_packet->nplayers);
      if(!differ) {
        int i;
        for (i = 0; i < real_packet->nplayers; i++) {
          if (old->is_ai[i] != real_packet->is_ai[i]) {
            differ = TRUE;
            break;
          }
        }
      }
    }
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 8);}

  if (different == 0 && !force_send_of_unchanged) {
    return 0;
  }

  DIO_BV_PUT(&dout, fields);

  /* field 0 is folded into the header */
  if (BV_ISSET(fields, 1)) {
    dio_put_uint8(&dout, real_packet->nplayers);
  }
  if (BV_ISSET(fields, 2)) {
    dio_put_string(&dout, real_packet->load_filename);
  }
  if (BV_ISSET(fields, 3)) {
  
    {
      int i;

      for (i = 0; i < real_packet->nplayers; i++) {
        dio_put_string(&dout, real_packet->name[i]);
      }
    } 
  }
  if (BV_ISSET(fields, 4)) {
  
    {
      int i;

      for (i = 0; i < real_packet->nplayers; i++) {
        dio_put_string(&dout, real_packet->username[i]);
      }
    } 
  }
  if (BV_ISSET(fields, 5)) {
  
    {
      int i;

      for (i = 0; i < real_packet->nplayers; i++) {
        dio_put_string(&dout, real_packet->nation_name[i]);
      }
    } 
  }
  if (BV_ISSET(fields, 6)) {
  
    {
      int i;

      for (i = 0; i < real_packet->nplayers; i++) {
        dio_put_string(&dout, real_packet->nation_flag[i]);
      }
    } 
  }
  if (BV_ISSET(fields, 7)) {
  
    {
      int i;

      for (i = 0; i < real_packet->nplayers; i++) {
        dio_put_bool8(&dout, real_packet->is_alive[i]);
      }
    } 
  }
  if (BV_ISSET(fields, 8)) {
  
    {
      int i;

      for (i = 0; i < real_packet->nplayers; i++) {
        dio_put_bool8(&dout, real_packet->is_ai[i]);
      }
    } 
  }


  if (old_from_hash) {
    hash_delete_entry(*hash, old);
  }

  clone = old;

  *clone = *real_packet;
  hash_insert(*hash, clone, clone);
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_game_load(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_GAME_LOAD] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_GAME_LOAD] = variant;
}

struct packet_game_load *receive_packet_game_load(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Receiving packet_game_load at the server.");
  }
  ensure_valid_variant_packet_game_load(pc);

  switch(pc->phs.variant[PACKET_GAME_LOAD]) {
    case 100: return receive_packet_game_load_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_game_load(struct connection *pc, const struct packet_game_load *packet)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Sending packet_game_load from the client.");
  }
  ensure_valid_variant_packet_game_load(pc);

  switch(pc->phs.variant[PACKET_GAME_LOAD]) {
    case 100: return send_packet_game_load_100(pc, packet);
    default: die("unknown variant"); return -1;
  }
}

void lsend_packet_game_load(struct conn_list *dest, const struct packet_game_load *packet)
{
  conn_list_iterate(*dest, pconn) {
    send_packet_game_load(pconn, packet);
  } conn_list_iterate_end;
}

#define hash_packet_options_settable_control_100 hash_const

#define cmp_packet_options_settable_control_100 cmp_const

BV_DEFINE(packet_options_settable_control_100_fields, 3);

static struct packet_options_settable_control *receive_packet_options_settable_control_100(struct connection *pc, enum packet_type type)
{
  packet_options_settable_control_100_fields fields;
  struct packet_options_settable_control *old;
  struct hash_table **hash = &pc->phs.received[type];
  struct packet_options_settable_control *clone;
  RECEIVE_PACKET_START(packet_options_settable_control, real_packet);

  DIO_BV_GET(&din, fields);


  if (!*hash) {
    *hash = hash_new(hash_packet_options_settable_control_100, cmp_packet_options_settable_control_100);
  }
  old = hash_delete_entry(*hash, real_packet);

  if (old) {
    *real_packet = *old;
  } else {
    memset(real_packet, 0, sizeof(*real_packet));
  }

  if (BV_ISSET(fields, 0)) {
    {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->nids = readin;
    }
  }
  if (BV_ISSET(fields, 1)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->ncategories = readin;
    }
  }
  if (BV_ISSET(fields, 2)) {
    
    {
      int i;
    
      if(real_packet->ncategories > 256) {
        freelog(LOG_ERROR, "packets_gen.c: WARNING: truncation array");
        real_packet->ncategories = 256;
      }
      for (i = 0; i < real_packet->ncategories; i++) {
        dio_get_string(&din, real_packet->category_names[i], sizeof(real_packet->category_names[i]));
      }
    }
  }

  clone = fc_malloc(sizeof(*clone));
  *clone = *real_packet;
  if (old) {
    free(old);
  }
  hash_insert(*hash, clone, clone);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_options_settable_control_100(struct connection *pc, const struct packet_options_settable_control *packet)
{
  const struct packet_options_settable_control *real_packet = packet;
  packet_options_settable_control_100_fields fields;
  struct packet_options_settable_control *old, *clone;
  bool differ, old_from_hash, force_send_of_unchanged = TRUE;
  struct hash_table **hash = &pc->phs.sent[PACKET_OPTIONS_SETTABLE_CONTROL];
  int different = 0;
  SEND_PACKET_START(PACKET_OPTIONS_SETTABLE_CONTROL);

  if (!*hash) {
    *hash = hash_new(hash_packet_options_settable_control_100, cmp_packet_options_settable_control_100);
  }
  BV_CLR_ALL(fields);

  old = hash_lookup_data(*hash, real_packet);
  old_from_hash = (old != NULL);
  if (!old) {
    old = fc_malloc(sizeof(*old));
    memset(old, 0, sizeof(*old));
    force_send_of_unchanged = TRUE;
  }

  differ = (old->nids != real_packet->nids);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 0);}

  differ = (old->ncategories != real_packet->ncategories);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 1);}


    {
      differ = (old->ncategories != real_packet->ncategories);
      if(!differ) {
        int i;
        for (i = 0; i < real_packet->ncategories; i++) {
          if (strcmp(old->category_names[i], real_packet->category_names[i]) != 0) {
            differ = TRUE;
            break;
          }
        }
      }
    }
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 2);}

  if (different == 0 && !force_send_of_unchanged) {
    return 0;
  }

  DIO_BV_PUT(&dout, fields);

  if (BV_ISSET(fields, 0)) {
    dio_put_uint16(&dout, real_packet->nids);
  }
  if (BV_ISSET(fields, 1)) {
    dio_put_uint8(&dout, real_packet->ncategories);
  }
  if (BV_ISSET(fields, 2)) {
  
    {
      int i;

      for (i = 0; i < real_packet->ncategories; i++) {
        dio_put_string(&dout, real_packet->category_names[i]);
      }
    } 
  }


  if (old_from_hash) {
    hash_delete_entry(*hash, old);
  }

  clone = old;

  *clone = *real_packet;
  hash_insert(*hash, clone, clone);
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_options_settable_control(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_OPTIONS_SETTABLE_CONTROL] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_OPTIONS_SETTABLE_CONTROL] = variant;
}

struct packet_options_settable_control *receive_packet_options_settable_control(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Receiving packet_options_settable_control at the server.");
  }
  ensure_valid_variant_packet_options_settable_control(pc);

  switch(pc->phs.variant[PACKET_OPTIONS_SETTABLE_CONTROL]) {
    case 100: return receive_packet_options_settable_control_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_options_settable_control(struct connection *pc, const struct packet_options_settable_control *packet)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Sending packet_options_settable_control from the client.");
  }
  ensure_valid_variant_packet_options_settable_control(pc);

  switch(pc->phs.variant[PACKET_OPTIONS_SETTABLE_CONTROL]) {
    case 100: return send_packet_options_settable_control_100(pc, packet);
    default: die("unknown variant"); return -1;
  }
}

#define hash_packet_options_settable_100 hash_const

#define cmp_packet_options_settable_100 cmp_const

BV_DEFINE(packet_options_settable_100_fields, 12);

static struct packet_options_settable *receive_packet_options_settable_100(struct connection *pc, enum packet_type type)
{
  packet_options_settable_100_fields fields;
  struct packet_options_settable *old;
  struct hash_table **hash = &pc->phs.received[type];
  struct packet_options_settable *clone;
  RECEIVE_PACKET_START(packet_options_settable, real_packet);

  DIO_BV_GET(&din, fields);


  if (!*hash) {
    *hash = hash_new(hash_packet_options_settable_100, cmp_packet_options_settable_100);
  }
  old = hash_delete_entry(*hash, real_packet);

  if (old) {
    *real_packet = *old;
  } else {
    memset(real_packet, 0, sizeof(*real_packet));
  }

  if (BV_ISSET(fields, 0)) {
    {
      int readin;
    
      dio_get_uint16(&din, &readin);
      real_packet->id = readin;
    }
  }
  if (BV_ISSET(fields, 1)) {
    dio_get_string(&din, real_packet->name, sizeof(real_packet->name));
  }
  if (BV_ISSET(fields, 2)) {
    dio_get_string(&din, real_packet->short_help, sizeof(real_packet->short_help));
  }
  if (BV_ISSET(fields, 3)) {
    dio_get_string(&din, real_packet->extra_help, sizeof(real_packet->extra_help));
  }
  if (BV_ISSET(fields, 4)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->type = readin;
    }
  }
  if (BV_ISSET(fields, 5)) {
    {
      int readin;
    
      dio_get_sint32(&din, &readin);
      real_packet->val = readin;
    }
  }
  if (BV_ISSET(fields, 6)) {
    {
      int readin;
    
      dio_get_sint32(&din, &readin);
      real_packet->default_val = readin;
    }
  }
  if (BV_ISSET(fields, 7)) {
    {
      int readin;
    
      dio_get_sint32(&din, &readin);
      real_packet->min = readin;
    }
  }
  if (BV_ISSET(fields, 8)) {
    {
      int readin;
    
      dio_get_sint32(&din, &readin);
      real_packet->max = readin;
    }
  }
  if (BV_ISSET(fields, 9)) {
    dio_get_string(&din, real_packet->strval, sizeof(real_packet->strval));
  }
  if (BV_ISSET(fields, 10)) {
    dio_get_string(&din, real_packet->default_strval, sizeof(real_packet->default_strval));
  }
  if (BV_ISSET(fields, 11)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->category = readin;
    }
  }

  clone = fc_malloc(sizeof(*clone));
  *clone = *real_packet;
  if (old) {
    free(old);
  }
  hash_insert(*hash, clone, clone);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_options_settable_100(struct connection *pc, const struct packet_options_settable *packet)
{
  const struct packet_options_settable *real_packet = packet;
  packet_options_settable_100_fields fields;
  struct packet_options_settable *old, *clone;
  bool differ, old_from_hash, force_send_of_unchanged = TRUE;
  struct hash_table **hash = &pc->phs.sent[PACKET_OPTIONS_SETTABLE];
  int different = 0;
  SEND_PACKET_START(PACKET_OPTIONS_SETTABLE);

  if (!*hash) {
    *hash = hash_new(hash_packet_options_settable_100, cmp_packet_options_settable_100);
  }
  BV_CLR_ALL(fields);

  old = hash_lookup_data(*hash, real_packet);
  old_from_hash = (old != NULL);
  if (!old) {
    old = fc_malloc(sizeof(*old));
    memset(old, 0, sizeof(*old));
    force_send_of_unchanged = TRUE;
  }

  differ = (old->id != real_packet->id);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 0);}

  differ = (strcmp(old->name, real_packet->name) != 0);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 1);}

  differ = (strcmp(old->short_help, real_packet->short_help) != 0);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 2);}

  differ = (strcmp(old->extra_help, real_packet->extra_help) != 0);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 3);}

  differ = (old->type != real_packet->type);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 4);}

  differ = (old->val != real_packet->val);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 5);}

  differ = (old->default_val != real_packet->default_val);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 6);}

  differ = (old->min != real_packet->min);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 7);}

  differ = (old->max != real_packet->max);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 8);}

  differ = (strcmp(old->strval, real_packet->strval) != 0);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 9);}

  differ = (strcmp(old->default_strval, real_packet->default_strval) != 0);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 10);}

  differ = (old->category != real_packet->category);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 11);}

  if (different == 0 && !force_send_of_unchanged) {
    return 0;
  }

  DIO_BV_PUT(&dout, fields);

  if (BV_ISSET(fields, 0)) {
    dio_put_uint16(&dout, real_packet->id);
  }
  if (BV_ISSET(fields, 1)) {
    dio_put_string(&dout, real_packet->name);
  }
  if (BV_ISSET(fields, 2)) {
    dio_put_string(&dout, real_packet->short_help);
  }
  if (BV_ISSET(fields, 3)) {
    dio_put_string(&dout, real_packet->extra_help);
  }
  if (BV_ISSET(fields, 4)) {
    dio_put_uint8(&dout, real_packet->type);
  }
  if (BV_ISSET(fields, 5)) {
    dio_put_sint32(&dout, real_packet->val);
  }
  if (BV_ISSET(fields, 6)) {
    dio_put_sint32(&dout, real_packet->default_val);
  }
  if (BV_ISSET(fields, 7)) {
    dio_put_sint32(&dout, real_packet->min);
  }
  if (BV_ISSET(fields, 8)) {
    dio_put_sint32(&dout, real_packet->max);
  }
  if (BV_ISSET(fields, 9)) {
    dio_put_string(&dout, real_packet->strval);
  }
  if (BV_ISSET(fields, 10)) {
    dio_put_string(&dout, real_packet->default_strval);
  }
  if (BV_ISSET(fields, 11)) {
    dio_put_uint8(&dout, real_packet->category);
  }


  if (old_from_hash) {
    hash_delete_entry(*hash, old);
  }

  clone = old;

  *clone = *real_packet;
  hash_insert(*hash, clone, clone);
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_options_settable(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_OPTIONS_SETTABLE] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_OPTIONS_SETTABLE] = variant;
}

struct packet_options_settable *receive_packet_options_settable(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Receiving packet_options_settable at the server.");
  }
  ensure_valid_variant_packet_options_settable(pc);

  switch(pc->phs.variant[PACKET_OPTIONS_SETTABLE]) {
    case 100: return receive_packet_options_settable_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_options_settable(struct connection *pc, const struct packet_options_settable *packet)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Sending packet_options_settable from the client.");
  }
  ensure_valid_variant_packet_options_settable(pc);

  switch(pc->phs.variant[PACKET_OPTIONS_SETTABLE]) {
    case 100: return send_packet_options_settable_100(pc, packet);
    default: die("unknown variant"); return -1;
  }
}

#define hash_packet_ruleset_cache_group_100 hash_const

#define cmp_packet_ruleset_cache_group_100 cmp_const

BV_DEFINE(packet_ruleset_cache_group_100_fields, 5);

static struct packet_ruleset_cache_group *receive_packet_ruleset_cache_group_100(struct connection *pc, enum packet_type type)
{
  packet_ruleset_cache_group_100_fields fields;
  struct packet_ruleset_cache_group *old;
  struct hash_table **hash = &pc->phs.received[type];
  struct packet_ruleset_cache_group *clone;
  RECEIVE_PACKET_START(packet_ruleset_cache_group, real_packet);

  DIO_BV_GET(&din, fields);


  if (!*hash) {
    *hash = hash_new(hash_packet_ruleset_cache_group_100, cmp_packet_ruleset_cache_group_100);
  }
  old = hash_delete_entry(*hash, real_packet);

  if (old) {
    *real_packet = *old;
  } else {
    memset(real_packet, 0, sizeof(*real_packet));
  }

  if (BV_ISSET(fields, 0)) {
    dio_get_string(&din, real_packet->name, sizeof(real_packet->name));
  }
  if (BV_ISSET(fields, 1)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->num_elements = readin;
    }
  }
  if (BV_ISSET(fields, 2)) {
    
    {
      int i;
    
      if(real_packet->num_elements > 255) {
        freelog(LOG_ERROR, "packets_gen.c: WARNING: truncation array");
        real_packet->num_elements = 255;
      }
      for (i = 0; i < real_packet->num_elements; i++) {
        {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->source_buildings[i] = readin;
    }
      }
    }
  }
  if (BV_ISSET(fields, 3)) {
    
    {
      int i;
    
      if(real_packet->num_elements > 255) {
        freelog(LOG_ERROR, "packets_gen.c: WARNING: truncation array");
        real_packet->num_elements = 255;
      }
      for (i = 0; i < real_packet->num_elements; i++) {
        {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->ranges[i] = readin;
    }
      }
    }
  }
  if (BV_ISSET(fields, 4)) {
    
    {
      int i;
    
      if(real_packet->num_elements > 255) {
        freelog(LOG_ERROR, "packets_gen.c: WARNING: truncation array");
        real_packet->num_elements = 255;
      }
      for (i = 0; i < real_packet->num_elements; i++) {
        dio_get_bool8(&din, &real_packet->survives[i]);
      }
    }
  }

  clone = fc_malloc(sizeof(*clone));
  *clone = *real_packet;
  if (old) {
    free(old);
  }
  hash_insert(*hash, clone, clone);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_ruleset_cache_group_100(struct connection *pc, const struct packet_ruleset_cache_group *packet)
{
  const struct packet_ruleset_cache_group *real_packet = packet;
  packet_ruleset_cache_group_100_fields fields;
  struct packet_ruleset_cache_group *old, *clone;
  bool differ, old_from_hash, force_send_of_unchanged = TRUE;
  struct hash_table **hash = &pc->phs.sent[PACKET_RULESET_CACHE_GROUP];
  int different = 0;
  SEND_PACKET_START(PACKET_RULESET_CACHE_GROUP);

  if (!*hash) {
    *hash = hash_new(hash_packet_ruleset_cache_group_100, cmp_packet_ruleset_cache_group_100);
  }
  BV_CLR_ALL(fields);

  old = hash_lookup_data(*hash, real_packet);
  old_from_hash = (old != NULL);
  if (!old) {
    old = fc_malloc(sizeof(*old));
    memset(old, 0, sizeof(*old));
    force_send_of_unchanged = TRUE;
  }

  differ = (strcmp(old->name, real_packet->name) != 0);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 0);}

  differ = (old->num_elements != real_packet->num_elements);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 1);}


    {
      differ = (old->num_elements != real_packet->num_elements);
      if(!differ) {
        int i;
        for (i = 0; i < real_packet->num_elements; i++) {
          if (old->source_buildings[i] != real_packet->source_buildings[i]) {
            differ = TRUE;
            break;
          }
        }
      }
    }
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 2);}


    {
      differ = (old->num_elements != real_packet->num_elements);
      if(!differ) {
        int i;
        for (i = 0; i < real_packet->num_elements; i++) {
          if (old->ranges[i] != real_packet->ranges[i]) {
            differ = TRUE;
            break;
          }
        }
      }
    }
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 3);}


    {
      differ = (old->num_elements != real_packet->num_elements);
      if(!differ) {
        int i;
        for (i = 0; i < real_packet->num_elements; i++) {
          if (old->survives[i] != real_packet->survives[i]) {
            differ = TRUE;
            break;
          }
        }
      }
    }
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 4);}

  if (different == 0 && !force_send_of_unchanged) {
    return 0;
  }

  DIO_BV_PUT(&dout, fields);

  if (BV_ISSET(fields, 0)) {
    dio_put_string(&dout, real_packet->name);
  }
  if (BV_ISSET(fields, 1)) {
    dio_put_uint8(&dout, real_packet->num_elements);
  }
  if (BV_ISSET(fields, 2)) {
  
    {
      int i;

      for (i = 0; i < real_packet->num_elements; i++) {
        dio_put_uint8(&dout, real_packet->source_buildings[i]);
      }
    } 
  }
  if (BV_ISSET(fields, 3)) {
  
    {
      int i;

      for (i = 0; i < real_packet->num_elements; i++) {
        dio_put_uint8(&dout, real_packet->ranges[i]);
      }
    } 
  }
  if (BV_ISSET(fields, 4)) {
  
    {
      int i;

      for (i = 0; i < real_packet->num_elements; i++) {
        dio_put_bool8(&dout, real_packet->survives[i]);
      }
    } 
  }


  if (old_from_hash) {
    hash_delete_entry(*hash, old);
  }

  clone = old;

  *clone = *real_packet;
  hash_insert(*hash, clone, clone);
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_ruleset_cache_group(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_RULESET_CACHE_GROUP] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_RULESET_CACHE_GROUP] = variant;
}

struct packet_ruleset_cache_group *receive_packet_ruleset_cache_group(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Receiving packet_ruleset_cache_group at the server.");
  }
  ensure_valid_variant_packet_ruleset_cache_group(pc);

  switch(pc->phs.variant[PACKET_RULESET_CACHE_GROUP]) {
    case 100: return receive_packet_ruleset_cache_group_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_ruleset_cache_group(struct connection *pc, const struct packet_ruleset_cache_group *packet)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Sending packet_ruleset_cache_group from the client.");
  }
  ensure_valid_variant_packet_ruleset_cache_group(pc);

  switch(pc->phs.variant[PACKET_RULESET_CACHE_GROUP]) {
    case 100: return send_packet_ruleset_cache_group_100(pc, packet);
    default: die("unknown variant"); return -1;
  }
}

void lsend_packet_ruleset_cache_group(struct conn_list *dest, const struct packet_ruleset_cache_group *packet)
{
  conn_list_iterate(*dest, pconn) {
    send_packet_ruleset_cache_group(pconn, packet);
  } conn_list_iterate_end;
}

#define hash_packet_ruleset_cache_effect_100 hash_const

#define cmp_packet_ruleset_cache_effect_100 cmp_const

BV_DEFINE(packet_ruleset_cache_effect_100_fields, 8);

static struct packet_ruleset_cache_effect *receive_packet_ruleset_cache_effect_100(struct connection *pc, enum packet_type type)
{
  packet_ruleset_cache_effect_100_fields fields;
  struct packet_ruleset_cache_effect *old;
  struct hash_table **hash = &pc->phs.received[type];
  struct packet_ruleset_cache_effect *clone;
  RECEIVE_PACKET_START(packet_ruleset_cache_effect, real_packet);

  DIO_BV_GET(&din, fields);


  if (!*hash) {
    *hash = hash_new(hash_packet_ruleset_cache_effect_100, cmp_packet_ruleset_cache_effect_100);
  }
  old = hash_delete_entry(*hash, real_packet);

  if (old) {
    *real_packet = *old;
  } else {
    memset(real_packet, 0, sizeof(*real_packet));
  }

  if (BV_ISSET(fields, 0)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->id = readin;
    }
  }
  if (BV_ISSET(fields, 1)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->effect_type = readin;
    }
  }
  if (BV_ISSET(fields, 2)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->range = readin;
    }
  }
  real_packet->survives = BV_ISSET(fields, 3);
  if (BV_ISSET(fields, 4)) {
    {
      int readin;
    
      dio_get_sint32(&din, &readin);
      real_packet->eff_value = readin;
    }
  }
  if (BV_ISSET(fields, 5)) {
    {
      int readin;
    
      dio_get_uint8(&din, &readin);
      real_packet->req_type = readin;
    }
  }
  if (BV_ISSET(fields, 6)) {
    {
      int readin;
    
      dio_get_sint32(&din, &readin);
      real_packet->req_value = readin;
    }
  }
  if (BV_ISSET(fields, 7)) {
    {
      int readin;
    
      dio_get_sint32(&din, &readin);
      real_packet->group_id = readin;
    }
  }

  clone = fc_malloc(sizeof(*clone));
  *clone = *real_packet;
  if (old) {
    free(old);
  }
  hash_insert(*hash, clone, clone);

  RECEIVE_PACKET_END(real_packet);
}

static int send_packet_ruleset_cache_effect_100(struct connection *pc, const struct packet_ruleset_cache_effect *packet)
{
  const struct packet_ruleset_cache_effect *real_packet = packet;
  packet_ruleset_cache_effect_100_fields fields;
  struct packet_ruleset_cache_effect *old, *clone;
  bool differ, old_from_hash, force_send_of_unchanged = TRUE;
  struct hash_table **hash = &pc->phs.sent[PACKET_RULESET_CACHE_EFFECT];
  int different = 0;
  SEND_PACKET_START(PACKET_RULESET_CACHE_EFFECT);

  if (!*hash) {
    *hash = hash_new(hash_packet_ruleset_cache_effect_100, cmp_packet_ruleset_cache_effect_100);
  }
  BV_CLR_ALL(fields);

  old = hash_lookup_data(*hash, real_packet);
  old_from_hash = (old != NULL);
  if (!old) {
    old = fc_malloc(sizeof(*old));
    memset(old, 0, sizeof(*old));
    force_send_of_unchanged = TRUE;
  }

  differ = (old->id != real_packet->id);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 0);}

  differ = (old->effect_type != real_packet->effect_type);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 1);}

  differ = (old->range != real_packet->range);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 2);}

  differ = (old->survives != real_packet->survives);
  if(differ) {different++;}
  if(packet->survives) {BV_SET(fields, 3);}

  differ = (old->eff_value != real_packet->eff_value);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 4);}

  differ = (old->req_type != real_packet->req_type);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 5);}

  differ = (old->req_value != real_packet->req_value);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 6);}

  differ = (old->group_id != real_packet->group_id);
  if(differ) {different++;}
  if(differ) {BV_SET(fields, 7);}

  if (different == 0 && !force_send_of_unchanged) {
    return 0;
  }

  DIO_BV_PUT(&dout, fields);

  if (BV_ISSET(fields, 0)) {
    dio_put_uint8(&dout, real_packet->id);
  }
  if (BV_ISSET(fields, 1)) {
    dio_put_uint8(&dout, real_packet->effect_type);
  }
  if (BV_ISSET(fields, 2)) {
    dio_put_uint8(&dout, real_packet->range);
  }
  /* field 3 is folded into the header */
  if (BV_ISSET(fields, 4)) {
    dio_put_sint32(&dout, real_packet->eff_value);
  }
  if (BV_ISSET(fields, 5)) {
    dio_put_uint8(&dout, real_packet->req_type);
  }
  if (BV_ISSET(fields, 6)) {
    dio_put_sint32(&dout, real_packet->req_value);
  }
  if (BV_ISSET(fields, 7)) {
    dio_put_sint32(&dout, real_packet->group_id);
  }


  if (old_from_hash) {
    hash_delete_entry(*hash, old);
  }

  clone = old;

  *clone = *real_packet;
  hash_insert(*hash, clone, clone);
  SEND_PACKET_END;
}

static void ensure_valid_variant_packet_ruleset_cache_effect(struct connection *pc)
{
  int variant = -1;

  if(pc->phs.variant[PACKET_RULESET_CACHE_EFFECT] != -1) {
    return;
  }

  if(FALSE) {
  } else if(TRUE) {
    variant = 100;
  } else {
    die("unknown variant");
  }
  pc->phs.variant[PACKET_RULESET_CACHE_EFFECT] = variant;
}

struct packet_ruleset_cache_effect *receive_packet_ruleset_cache_effect(struct connection *pc, enum packet_type type)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to read data from the closed connection %s",
	    conn_description(pc));
    return NULL;
  }
  assert(pc->phs.variant != NULL);
  if(is_server) {
    freelog(LOG_ERROR, "Receiving packet_ruleset_cache_effect at the server.");
  }
  ensure_valid_variant_packet_ruleset_cache_effect(pc);

  switch(pc->phs.variant[PACKET_RULESET_CACHE_EFFECT]) {
    case 100: return receive_packet_ruleset_cache_effect_100(pc, type);
    default: die("unknown variant"); return NULL;
  }
}

int send_packet_ruleset_cache_effect(struct connection *pc, const struct packet_ruleset_cache_effect *packet)
{
  if(!pc->used) {
    freelog(LOG_ERROR,
	    "WARNING: trying to send data to the closed connection %s",
	    conn_description(pc));
    return -1;
  }
  assert(pc->phs.variant != NULL);
  if(!is_server) {
    freelog(LOG_ERROR, "Sending packet_ruleset_cache_effect from the client.");
  }
  ensure_valid_variant_packet_ruleset_cache_effect(pc);

  switch(pc->phs.variant[PACKET_RULESET_CACHE_EFFECT]) {
    case 100: return send_packet_ruleset_cache_effect_100(pc, packet);
    default: die("unknown variant"); return -1;
  }
}

void lsend_packet_ruleset_cache_effect(struct conn_list *dest, const struct packet_ruleset_cache_effect *packet)
{
  conn_list_iterate(*dest, pconn) {
    send_packet_ruleset_cache_effect(pconn, packet);
  } conn_list_iterate_end;
}

