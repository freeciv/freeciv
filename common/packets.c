/********************************************************************** 
 Freeciv - Copyright (C) 1996 - A Kjeldberg, L Gregersen, P Unold
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
***********************************************************************/
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <limits.h>

#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif
#ifdef HAVE_ARPA_INET_H
#include <arpa/inet.h>
#endif
#ifdef HAVE_WINSOCK
#include <winsock.h>
#endif

#include "capability.h"
#include "dataio.h"
#include "events.h"
#include "fcintl.h"
#include "log.h"
#include "mem.h"
#include "support.h"

#include "packets.h"

#define PACKET_SIZE_STATISTICS 0

/********************************************************************** 
 The current packet functions don't handle signed values
 correct. This will probably lead to problems when compiling
 freeciv for a platform which has 64 bit ints. Also 16 and 8
 bits values cannot be signed without using tricks (look in e.g
 receive_packet_city_info() )

  TODO to solve these problems:
    o use the new signed functions where they are necessary
    o change the prototypes of the unsigned functions
       to unsigned int instead of int (but only on the 32
       bit functions, because otherwhere it's not necessary)

  Possibe enhancements:
    o check in configure for ints with size others than 4
    o write real put functions and check for the limit
***********************************************************************/
#define SEND_PACKET_START(type) \
  unsigned char buffer[MAX_LEN_PACKET]; \
  struct data_out dout; \
  \
  dio_output_init(&dout, buffer, sizeof(buffer)); \
  dio_put_uint16(&dout, 0); \
  dio_put_uint8(&dout, type);

#define SEND_PACKET_END \
  { \
    size_t size = dio_output_used(&dout); \
    \
    dio_output_rewind(&dout); \
    dio_put_uint16(&dout, size); \
    return send_packet_data(pc, buffer, size); \
  }

#define RECEIVE_PACKET_START(type, result) \
  struct data_in din; \
  struct type *result = fc_malloc(sizeof(*result)); \
  \
  dio_input_init(&din, pc->buffer->data, 2); \
  { \
    int size; \
  \
    dio_get_uint16(&din, &size); \
    dio_input_init(&din, pc->buffer->data, MIN(size, pc->buffer->ndata)); \
  } \
  dio_get_uint16(&din, NULL); \
  dio_get_uint8(&din, NULL);

#define RECEIVE_PACKET_END(result) \
  check_packet(&din, pc); \
  remove_packet_from_buffer(pc->buffer); \
  return result;

static int send_packet_data(struct connection *pc, unsigned char *data,
			    int len);

/**************************************************************************
It returns the request id of the outgoing packet or 0 if the packet
was no request (i.e. server sends packet).
**************************************************************************/
static int send_packet_data(struct connection *pc, unsigned char *data,
			    int len)
{
  /* default for the server */
  int result = 0;

  freelog(LOG_DEBUG, "sending packet type=%d len=%d", data[2], len);

  if (!is_server) {
    pc->client.last_request_id_used =
	get_next_request_id(pc->client.last_request_id_used);
    result = pc->client.last_request_id_used;
    freelog(LOG_DEBUG, "sending request %d", result);
  }

  if (pc->outgoing_packet_notify) {
    pc->outgoing_packet_notify(pc, data[2], len, result);
  }

#if PACKET_SIZE_STATISTICS
  {
    static struct {
      int counter;
      int size;
    } packets_stats[PACKET_LAST];
    static int packet_counter = 0;

    int packet_type = data[2];
    int size = len;

    if (!packet_counter) {
      int i;

      for (i = 0; i < PACKET_LAST; i++) {
	packets_stats[i].counter = 0;
	packets_stats[i].size = 0;
      }
    }

    packets_stats[packet_type].counter++;
    packets_stats[packet_type].size += size;

    packet_counter++;
    if ((!is_server && (packet_counter % 10 == 0))
	|| (is_server && (packet_counter % 1000 == 0))) {
      int i, sum = 0;

      freelog(LOG_NORMAL, "Transmitted packets:");
      for (i = 0; i < PACKET_LAST; i++) {
	if (packets_stats[i].counter == 0)
	  continue;
	sum += packets_stats[i].size;
	freelog(LOG_NORMAL,
		"  [%2d]: %6d packets; %8d bytes total; "
		"%5d bytes/packet average",
		i, packets_stats[i].counter,
		packets_stats[i].size,
		packets_stats[i].size / packets_stats[i].counter);
      }
      freelog(LOG_NORMAL,
	      "transmitted %d bytes in %d packets;average size "
	      "per packet %d bytes",
	      sum, packet_counter, sum / packet_counter);
    }
  }
#endif

  send_connection_data(pc, data, len);

  return result;
}

/**************************************************************************
presult indicates if there is more packets in the cache. We return result
instead of just testing if the returning package is NULL as we sometimes
return a NULL packet even if everything is OK (receive_packet_goto_route).
**************************************************************************/
void *get_packet_from_connection(struct connection *pc,
				 enum packet_type *ptype, bool * presult)
{
  int len, itype;
  enum packet_type type;
  struct data_in din;

  *presult = FALSE;

  if (!pc->used)
    return NULL;		/* connection was closed, stop reading */
  
  if(pc->buffer->ndata<3)
    return NULL;           /* length and type not read */

  dio_input_init(&din, pc->buffer->data, pc->buffer->ndata);
  dio_get_uint16(&din, &len);
  dio_get_uint8(&din, &itype);
  type = itype;

  if(pc->first_packet) {
    /* the first packet better be short: */
    freelog(LOG_DEBUG, "first packet type %d len %d", type, len);
    pc->first_packet = FALSE;
  }

  if (len > pc->buffer->ndata) {
    return NULL;		/* not all data has been read */
  }

  freelog(LOG_DEBUG, "got packet type=%d len=%d", type, len);

  *ptype=type;
  *presult = TRUE;

  if (pc->incoming_packet_notify) {
    pc->incoming_packet_notify(pc, type, len);
  }

#if PACKET_SIZE_STATISTICS
  {
    static struct {
      int counter;
      int size;
    } packets_stats[PACKET_LAST];
    static int packet_counter = 0;

    int packet_type = type;
    int size = len;

    if (!packet_counter) {
      int i;

      for (i = 0; i < PACKET_LAST; i++) {
	packets_stats[i].counter = 0;
	packets_stats[i].size = 0;
      }
    }

    packets_stats[packet_type].counter++;
    packets_stats[packet_type].size += size;

    packet_counter++;
    if ((is_server && (packet_counter % 10 == 0))
	|| (!is_server && (packet_counter % 1000 == 0))) {
      int i, sum = 0;

      freelog(LOG_NORMAL, "Received packets:");
      for (i = 0; i < PACKET_LAST; i++) {
	if (packets_stats[i].counter == 0)
	  continue;
	sum += packets_stats[i].size;
	freelog(LOG_NORMAL,
		"  [%2d]: %6d packets; %8d bytes total; "
		"%5d bytes/packet average",
		i, packets_stats[i].counter,
		packets_stats[i].size,
		packets_stats[i].size / packets_stats[i].counter);
      }
      freelog(LOG_NORMAL,
	      "received %d bytes in %d packets;average size "
	      "per packet %d bytes",
	      sum, packet_counter, sum / packet_counter);
    }
  }
#endif

  switch(type) {

  case PACKET_REQUEST_JOIN_GAME:
    return receive_packet_req_join_game(pc);

  case PACKET_JOIN_GAME_REPLY:
    return receive_packet_join_game_reply(pc);

  case PACKET_SERVER_SHUTDOWN:
    return receive_packet_generic_message(pc);

  case PACKET_UNIT_INFO:
    return receive_packet_unit_info(pc);

  case PACKET_CITY_INFO:
    return receive_packet_city_info(pc);

  case PACKET_SHORT_CITY:
    return receive_packet_short_city(pc);

  case PACKET_MOVE_UNIT:
    return receive_packet_move_unit(pc);

  case PACKET_TURN_DONE:
    return receive_packet_generic_message(pc);

  case PACKET_CONN_PING:
  case PACKET_CONN_PONG:
  case PACKET_BEFORE_NEW_YEAR:
  case PACKET_PROCESSING_STARTED:
  case PACKET_PROCESSING_FINISHED:
  case PACKET_START_TURN:
  case PACKET_SELECT_NATION_OK:
  case PACKET_FREEZE_HINT:
  case PACKET_THAW_HINT:
    return receive_packet_generic_empty(pc);

  case PACKET_NEW_YEAR:
    return receive_packet_new_year(pc);

  case PACKET_TILE_INFO:
    return receive_packet_tile_info(pc);

  case PACKET_SELECT_NATION:
    return receive_packet_nations_used(pc);

  case PACKET_REMOVE_UNIT:
  case PACKET_REMOVE_CITY:
  case PACKET_GAME_STATE:
  case PACKET_REPORT_REQUEST:
  case PACKET_REMOVE_PLAYER:  
  case PACKET_CITY_REFRESH:
  case PACKET_INCITE_INQ:
  case PACKET_CITY_NAME_SUGGEST_REQ:
  case PACKET_ADVANCE_FOCUS:
  case PACKET_PLAYER_CANCEL_PACT:
  case PACKET_PLAYER_REMOVE_VISION:
    return receive_packet_generic_integer(pc);
    
  case PACKET_ALLOC_NATION:
    return receive_packet_alloc_nation(pc);

  case PACKET_SHOW_MESSAGE:
    return receive_packet_generic_message(pc);

  case PACKET_PLAYER_INFO:
    return receive_packet_player_info(pc);

  case PACKET_GAME_INFO:
    return receive_packet_game_info(pc);

  case PACKET_MAP_INFO:
    return receive_packet_map_info(pc);

  case PACKET_CHAT_MSG:
  case PACKET_PAGE_MSG:
    return receive_packet_generic_message(pc);
    
  case PACKET_CITY_SELL:
  case PACKET_CITY_BUY:
  case PACKET_CITY_CHANGE:
  case PACKET_CITY_WORKLIST:
  case PACKET_CITY_MAKE_SPECIALIST:
  case PACKET_CITY_MAKE_WORKER:
  case PACKET_CITY_CHANGE_SPECIALIST:
  case PACKET_CITY_RENAME:
    return receive_packet_city_request(pc);

  case PACKET_PLAYER_RATES:
  case PACKET_PLAYER_REVOLUTION:
  case PACKET_PLAYER_GOVERNMENT:
  case PACKET_PLAYER_RESEARCH:
  case PACKET_PLAYER_TECH_GOAL:
  case PACKET_PLAYER_ATTRIBUTE_BLOCK:
    return receive_packet_player_request(pc);

  case PACKET_UNIT_BUILD_CITY:
  case PACKET_UNIT_DISBAND:
  case PACKET_UNIT_CHANGE_HOMECITY:
  case PACKET_UNIT_ESTABLISH_TRADE:
  case PACKET_UNIT_HELP_BUILD_WONDER:
  case PACKET_UNIT_GOTO_TILE:
  case PACKET_UNIT_AUTO:
  case PACKET_UNIT_UNLOAD:
  case PACKET_UNIT_UPGRADE:
  case PACKET_UNIT_NUKE:
  case PACKET_UNIT_PARADROP_TO:
    return receive_packet_unit_request(pc);
  case PACKET_UNIT_CONNECT:
    return receive_packet_unit_connect(pc);
  case PACKET_UNITTYPE_UPGRADE:
    return receive_packet_unittype_info(pc);
  case PACKET_UNIT_COMBAT:
    return receive_packet_unit_combat(pc);
  case PACKET_NUKE_TILE:
    return receive_packet_nuke_tile(pc);
  case PACKET_DIPLOMAT_ACTION:
    return receive_packet_diplomat_action(pc);

  case PACKET_DIPLOMACY_INIT_MEETING:
  case PACKET_DIPLOMACY_CREATE_CLAUSE:
  case PACKET_DIPLOMACY_REMOVE_CLAUSE:
  case PACKET_DIPLOMACY_CANCEL_MEETING:
  case PACKET_DIPLOMACY_ACCEPT_TREATY:
  case PACKET_DIPLOMACY_SIGN_TREATY:
    return receive_packet_diplomacy_info(pc);

  case PACKET_INCITE_COST:
  case PACKET_CITY_OPTIONS:
    return receive_packet_generic_values(pc);

  case PACKET_RULESET_CONTROL:
    return receive_packet_ruleset_control(pc);
  case PACKET_RULESET_TECH:
    return receive_packet_ruleset_tech(pc);
  case PACKET_RULESET_UNIT:
    return receive_packet_ruleset_unit(pc);
  case PACKET_RULESET_BUILDING:
    return receive_packet_ruleset_building(pc);
  case PACKET_RULESET_TERRAIN:
    return receive_packet_ruleset_terrain(pc);
  case PACKET_RULESET_TERRAIN_CONTROL:
    return receive_packet_ruleset_terrain_control(pc);
  case PACKET_RULESET_GOVERNMENT:
    return receive_packet_ruleset_government(pc);
  case PACKET_RULESET_GOVERNMENT_RULER_TITLE:
    return receive_packet_ruleset_government_ruler_title(pc);
  case PACKET_RULESET_NATION:
    return receive_packet_ruleset_nation(pc);
  case PACKET_RULESET_CITY:
    return receive_packet_ruleset_city(pc);
  case PACKET_RULESET_GAME:
    return receive_packet_ruleset_game(pc);

  case PACKET_SPACESHIP_INFO:
    return receive_packet_spaceship_info(pc);

  case PACKET_SPACESHIP_ACTION:
    return receive_packet_spaceship_action(pc);
    
  case PACKET_CITY_NAME_SUGGESTION:
    return receive_packet_city_name_suggestion(pc);

  case PACKET_SABOTAGE_LIST:
    return receive_packet_sabotage_list(pc);

  case PACKET_CONN_INFO:
    return receive_packet_conn_info(pc);

  case PACKET_GOTO_ROUTE:
  case PACKET_PATROL_ROUTE:
    return receive_packet_goto_route(pc);

  case PACKET_UNIT_AIRLIFT:
    return receive_packet_unit_request(pc);

  case PACKET_ATTRIBUTE_CHUNK:
    return receive_packet_attribute_chunk(pc);

  default:
    freelog(LOG_ERROR, "unknown packet type %d received from %s",
	    type, conn_description(pc));
    remove_packet_from_buffer(pc->buffer);
    return NULL;
  };
}

/**************************************************************************
  Remove the packet from the buffer
**************************************************************************/
void remove_packet_from_buffer(struct socket_packet_buffer *buffer)
{
  struct data_in din;
  int len;

  dio_input_init(&din, buffer->data, buffer->ndata);
  dio_get_uint16(&din, &len);
  memmove(buffer->data, buffer->data + len, buffer->ndata - len);
  buffer->ndata -= len;
}

/**************************************************************************
  ...
**************************************************************************/
static void check_packet(struct data_in *din, struct connection *pc)
{
  size_t rem = dio_input_remaining(din);

  if (din->bad_string || din->bad_bit_string || rem != 0) {
    char from[MAX_LEN_ADDR + MAX_LEN_NAME + 128];
    int type, len;

    assert(pc != NULL);
    my_snprintf(from, sizeof(from), " from %s", conn_description(pc));

    dio_input_rewind(din);
    dio_get_uint16(din, &len);
    dio_get_uint8(din, &type);

    if (din->bad_string) {
      freelog(LOG_ERROR,
	      "received bad string in packet (type %d, len %d)%s",
	      type, len, from);
    }

    if (din->bad_bit_string) {
      freelog(LOG_ERROR,
	      "received bad bit string in packet (type %d, len %d)%s",
	      type, len, from);
    }

    if (din->too_short) {
      freelog(LOG_ERROR, "received short packet (type %d, len %d)%s",
	      type, len, from);
    }

    if (rem > 0) {
      /* This may be ok, eg a packet from a newer version with extra info
       * which we should just ignore */
      freelog(LOG_VERBOSE,
	      "received long packet (type %d, len %d, rem %lu)%s", type,
	      len, (unsigned long)rem, from);
    }
  }
}

/*************************************************************************
...
**************************************************************************/
int send_packet_diplomacy_info(struct connection *pc, enum packet_type pt,
			       const struct packet_diplomacy_info *packet)
{
  SEND_PACKET_START(pt);

  dio_put_uint32(&dout, packet->plrno0);
  dio_put_uint32(&dout, packet->plrno1);
  dio_put_uint32(&dout, packet->plrno_from);
  dio_put_uint32(&dout, packet->clause_type);
  dio_put_uint32(&dout, packet->value);

  SEND_PACKET_END;
}

/*************************************************************************
...
**************************************************************************/
struct packet_diplomacy_info *receive_packet_diplomacy_info(struct connection
							    *pc)
{
  RECEIVE_PACKET_START(packet_diplomacy_info, preq);

  dio_get_uint32(&din, &preq->plrno0);
  dio_get_uint32(&din, &preq->plrno1);
  dio_get_uint32(&din, &preq->plrno_from);
  dio_get_uint32(&din, &preq->clause_type);
  dio_get_uint32(&din, &preq->value);

  RECEIVE_PACKET_END(preq);
}

/*************************************************************************
...
**************************************************************************/
int send_packet_diplomat_action(struct connection *pc,
				const struct packet_diplomat_action *packet)
{
  SEND_PACKET_START(PACKET_DIPLOMAT_ACTION);

  dio_put_uint8(&dout, packet->action_type);
  dio_put_uint16(&dout, packet->diplomat_id);
  dio_put_uint16(&dout, packet->target_id);
  dio_put_uint16(&dout, packet->value);

  SEND_PACKET_END;
}

/*************************************************************************
...
**************************************************************************/
struct packet_diplomat_action *receive_packet_diplomat_action(struct
							      connection *pc)
{
  RECEIVE_PACKET_START(packet_diplomat_action, preq);

  dio_get_uint8(&din, &preq->action_type);
  dio_get_uint16(&din, &preq->diplomat_id);
  dio_get_uint16(&din, &preq->target_id);
  dio_get_uint16(&din, &preq->value);

  RECEIVE_PACKET_END(preq);
}

/*************************************************************************
...
**************************************************************************/
int send_packet_nuke_tile(struct connection *pc,
			  const struct packet_nuke_tile *packet)
{
  SEND_PACKET_START(PACKET_NUKE_TILE);

  dio_put_uint8(&dout, packet->x);
  dio_put_uint8(&dout, packet->y);

  SEND_PACKET_END;
}

/*************************************************************************
...
**************************************************************************/
struct packet_nuke_tile *receive_packet_nuke_tile(struct connection *pc)
{
  RECEIVE_PACKET_START(packet_nuke_tile, preq);

  dio_get_uint8(&din, &preq->x);
  dio_get_uint8(&din, &preq->y);

  RECEIVE_PACKET_END(preq);
}


/*************************************************************************
...
**************************************************************************/
int send_packet_unit_combat(struct connection *pc,
			    const struct packet_unit_combat *packet)
{
  SEND_PACKET_START(PACKET_UNIT_COMBAT);

  dio_put_uint16(&dout, packet->attacker_unit_id);
  dio_put_uint16(&dout, packet->defender_unit_id);
  dio_put_uint8(&dout, packet->attacker_hp);
  dio_put_uint8(&dout, packet->defender_hp);
  dio_put_uint8(&dout, packet->make_winner_veteran);

  SEND_PACKET_END;
}

/*************************************************************************
...
**************************************************************************/
struct packet_unit_combat *receive_packet_unit_combat(struct connection *pc)
{
  RECEIVE_PACKET_START(packet_unit_combat, preq);

  dio_get_uint16(&din, &preq->attacker_unit_id);
  dio_get_uint16(&din, &preq->defender_unit_id);
  dio_get_uint8(&din, &preq->attacker_hp);
  dio_get_uint8(&din, &preq->defender_hp);
  dio_get_uint8(&din, &preq->make_winner_veteran);

  RECEIVE_PACKET_END(preq);
}


/*************************************************************************
...
**************************************************************************/
int send_packet_unit_request(struct connection *pc,
			     const struct packet_unit_request *packet,
			     enum packet_type req_type)
{
  SEND_PACKET_START(req_type);

  dio_put_uint16(&dout, packet->unit_id);
  dio_put_uint16(&dout, packet->city_id);
  dio_put_uint8(&dout, packet->x);
  dio_put_uint8(&dout, packet->y);
  dio_put_string(&dout, packet->name);

  SEND_PACKET_END;
}


/*************************************************************************
...
**************************************************************************/
struct packet_unit_request *receive_packet_unit_request(struct connection
							*pc)
{
  RECEIVE_PACKET_START(packet_unit_request, preq);

  dio_get_uint16(&din, &preq->unit_id);
  dio_get_uint16(&din, &preq->city_id);
  dio_get_uint8(&din, &preq->x);
  dio_get_uint8(&din, &preq->y);
  dio_get_string(&din, preq->name, sizeof(preq->name));

  RECEIVE_PACKET_END(preq);
}

/*************************************************************************
...
**************************************************************************/
int send_packet_unit_connect(struct connection *pc,
			     const struct packet_unit_connect *packet)
{
  SEND_PACKET_START(PACKET_UNIT_CONNECT);

  dio_put_uint8(&dout, packet->activity_type);
  dio_put_uint16(&dout, packet->unit_id);
  dio_put_uint16(&dout, packet->dest_x);
  dio_put_uint16(&dout, packet->dest_y);

  SEND_PACKET_END;
}

/*************************************************************************
...
**************************************************************************/
struct packet_unit_connect *receive_packet_unit_connect(struct connection
							*pc)
{
  RECEIVE_PACKET_START(packet_unit_connect, preq);

  dio_get_uint8(&din, &preq->activity_type);
  dio_get_uint16(&din, &preq->unit_id);
  dio_get_uint16(&din, &preq->dest_x);
  dio_get_uint16(&din, &preq->dest_y);

  RECEIVE_PACKET_END(preq);
}

/*************************************************************************
...
**************************************************************************/
int send_packet_player_request(struct connection *pc,
			       const struct packet_player_request *packet,
			       enum packet_type req_type)
{
  SEND_PACKET_START(req_type);

  dio_put_uint8(&dout, packet->tax);
  dio_put_uint8(&dout, packet->luxury);
  dio_put_uint8(&dout, packet->science);
  dio_put_uint8(&dout, packet->government);
  dio_put_uint8(&dout, packet->tech);
  dio_put_bool8(&dout, req_type == PACKET_PLAYER_ATTRIBUTE_BLOCK);

  SEND_PACKET_END;
}

/*************************************************************************
...
**************************************************************************/
struct packet_player_request *receive_packet_player_request(struct connection
							    *pc)
{
  RECEIVE_PACKET_START(packet_player_request, preq);

  dio_get_uint8(&din, &preq->tax);
  dio_get_uint8(&din, &preq->luxury);
  dio_get_uint8(&din, &preq->science);
  dio_get_uint8(&din, &preq->government);
  dio_get_uint8(&din, &preq->tech);
  dio_get_bool8(&din, &preq->attribute_block);

  RECEIVE_PACKET_END(preq);
}

/*************************************************************************
...
**************************************************************************/
int send_packet_city_request(struct connection *pc,
			     const struct packet_city_request *packet,
			     enum packet_type req_type)
{
  /* can't modify the packet directly */
  struct worklist copy;
  SEND_PACKET_START(req_type);

  if (req_type == PACKET_CITY_WORKLIST) {
    assert(packet->worklist.is_valid);
    copy_worklist(&copy, &packet->worklist);
  } else {
    copy.is_valid = FALSE;
  }

  dio_put_uint16(&dout, packet->city_id);
  dio_put_uint8(&dout, packet->build_id);
  if (req_type == PACKET_CITY_CHANGE) {
    dio_put_bool8(&dout, packet->is_build_id_unit_id);
  } else {
    dio_put_bool8(&dout, FALSE);
  }
  dio_put_uint8(&dout, packet->worker_x);
  dio_put_uint8(&dout, packet->worker_y);
  dio_put_uint8(&dout, packet->specialist_from);
  dio_put_uint8(&dout, packet->specialist_to);
  dio_put_worklist(&dout, &copy, TRUE);
  if (req_type == PACKET_CITY_RENAME) {
    dio_put_string(&dout, packet->name);
  } else {
    dio_put_string(&dout, "");
  }

  SEND_PACKET_END;
}

/*************************************************************************
...
**************************************************************************/
struct packet_city_request *receive_packet_city_request(struct connection
							*pc)
{
  RECEIVE_PACKET_START(packet_city_request, preq);

  dio_get_uint16(&din, &preq->city_id);
  dio_get_uint8(&din, &preq->build_id);
  dio_get_bool8(&din, &preq->is_build_id_unit_id);
  dio_get_uint8(&din, &preq->worker_x);
  dio_get_uint8(&din, &preq->worker_y);
  dio_get_uint8(&din, &preq->specialist_from);
  dio_get_uint8(&din, &preq->specialist_to);
  dio_get_worklist(&din, &preq->worklist);
  dio_get_string(&din, preq->name, sizeof(preq->name));

  RECEIVE_PACKET_END(preq);
}

/*************************************************************************
...
**************************************************************************/
int send_packet_player_info(struct connection *pc,
			    const struct packet_player_info *pinfo)
{
  int i;
  SEND_PACKET_START(PACKET_PLAYER_INFO);

  dio_put_uint8(&dout, pinfo->playerno);
  dio_put_string(&dout, pinfo->name);

  dio_put_bool8(&dout, pinfo->is_male);
  if (has_capability("team", pc->capability)) {
    dio_put_uint8(&dout, pinfo->team);
  }
  dio_put_uint8(&dout, pinfo->government);
  dio_put_uint32(&dout, pinfo->embassy);
  dio_put_uint8(&dout, pinfo->city_style);
  dio_put_uint8(&dout, pinfo->nation);
  dio_put_bool8(&dout, pinfo->turn_done);
  dio_put_uint16(&dout, pinfo->nturns_idle);
  dio_put_bool8(&dout, pinfo->is_alive);

  dio_put_uint32(&dout, pinfo->reputation);
  for (i = 0; i < MAX_NUM_PLAYERS + MAX_NUM_BARBARIANS; i++) {
    dio_put_uint32(&dout, pinfo->diplstates[i].type);
    dio_put_uint32(&dout, pinfo->diplstates[i].turns_left);
    dio_put_uint32(&dout, pinfo->diplstates[i].has_reason_to_cancel);
  }

  dio_put_uint32(&dout, pinfo->gold);
  dio_put_uint8(&dout, pinfo->tax);
  dio_put_uint8(&dout, pinfo->science);
  dio_put_uint8(&dout, pinfo->luxury);

  dio_put_uint32(&dout, pinfo->bulbs_researched);
  dio_put_uint32(&dout, pinfo->techs_researched);
  dio_put_uint8(&dout, pinfo->researching);

  dio_put_bit_string(&dout, (char *) pinfo->inventions);
  dio_put_uint16(&dout, pinfo->future_tech);

  dio_put_bool8(&dout, pinfo->is_connected);

  dio_put_uint8(&dout, pinfo->revolution);
  dio_put_uint8(&dout, pinfo->tech_goal);
  dio_put_bool8(&dout, pinfo->ai);
  dio_put_uint8(&dout, pinfo->barbarian_type);

  dio_put_uint32(&dout, pinfo->gives_shared_vision);

  SEND_PACKET_END;
}


/*************************************************************************
...
**************************************************************************/
struct packet_player_info *receive_packet_player_info(struct connection *pc)
{
  int i;
  RECEIVE_PACKET_START(packet_player_info, pinfo);

  dio_get_uint8(&din, &pinfo->playerno);
  dio_get_string(&din, pinfo->name, sizeof(pinfo->name));

  dio_get_bool8(&din, &pinfo->is_male);
  if (has_capability("team", pc->capability)) {
    dio_get_uint8(&din, &pinfo->team);
  } else {
    pinfo->team = TEAM_NONE;
  }
  dio_get_uint8(&din, &pinfo->government);
  dio_get_uint32(&din, &pinfo->embassy);
  dio_get_uint8(&din, &pinfo->city_style);
  dio_get_uint8(&din, &pinfo->nation);
  dio_get_bool8(&din, &pinfo->turn_done);
  dio_get_uint16(&din, &pinfo->nturns_idle);
  dio_get_bool8(&din, &pinfo->is_alive);

  dio_get_uint32(&din, &pinfo->reputation);
  for (i = 0; i < MAX_NUM_PLAYERS + MAX_NUM_BARBARIANS; i++) {
    dio_get_uint32(&din, (int *) &pinfo->diplstates[i].type);
    dio_get_uint32(&din, &pinfo->diplstates[i].turns_left);
    dio_get_uint32(&din, &pinfo->diplstates[i].has_reason_to_cancel);
  }

  dio_get_uint32(&din, &pinfo->gold);
  dio_get_uint8(&din, &pinfo->tax);
  dio_get_uint8(&din, &pinfo->science);
  dio_get_uint8(&din, &pinfo->luxury);

  dio_get_uint32(&din, &pinfo->bulbs_researched);
  dio_get_uint32(&din, &pinfo->techs_researched);
  dio_get_uint8(&din, &pinfo->researching);
  dio_get_bit_string(&din, (char *) pinfo->inventions,
		     sizeof(pinfo->inventions));
  dio_get_uint16(&din, &pinfo->future_tech);

  dio_get_bool8(&din, &pinfo->is_connected);

  dio_get_uint8(&din, &pinfo->revolution);
  dio_get_uint8(&din, &pinfo->tech_goal);
  dio_get_bool8(&din, &pinfo->ai);
  dio_get_uint8(&din, &pinfo->barbarian_type);

  /* Unfortunately the second argument to iget_uint32 is int, not uint: */
  dio_get_uint32(&din, &i);
  pinfo->gives_shared_vision = i;

  RECEIVE_PACKET_END(pinfo);
}

/*************************************************************************
  Send connection.id as uint32 even though currently only use ushort
  range, in case want to use it for more later, eg global user-id...?
...
**************************************************************************/
int send_packet_conn_info(struct connection *pc,
			  const struct packet_conn_info *pinfo)
{
  SEND_PACKET_START(PACKET_CONN_INFO);

  dio_put_uint32(&dout, pinfo->id);

  dio_put_uint8(&dout, (COND_SET_BIT(pinfo->used, 0) |
			COND_SET_BIT(pinfo->established, 1) |
			COND_SET_BIT(pinfo->observer, 2)));

  dio_put_uint8(&dout, pinfo->player_num);
  dio_put_uint8(&dout, pinfo->access_level);

  dio_put_string(&dout, pinfo->name);
  dio_put_string(&dout, pinfo->addr);
  dio_put_string(&dout, pinfo->capability);

  SEND_PACKET_END;
}

/*************************************************************************
...
**************************************************************************/
struct packet_conn_info *receive_packet_conn_info(struct connection *pc)
{
  int data;
  RECEIVE_PACKET_START(packet_conn_info, pinfo);

  dio_get_uint32(&din, &pinfo->id);

  dio_get_uint8(&din, &data);
  pinfo->used = TEST_BIT(data, 0);
  pinfo->established = TEST_BIT(data, 1);
  pinfo->observer = TEST_BIT(data, 2);

  dio_get_uint8(&din, &pinfo->player_num);
  dio_get_uint8(&din, &data);
  pinfo->access_level = data;

  dio_get_string(&din, pinfo->name, sizeof(pinfo->name));
  dio_get_string(&din, pinfo->addr, sizeof(pinfo->addr));
  dio_get_string(&din, pinfo->capability, sizeof(pinfo->capability));

  RECEIVE_PACKET_END(pinfo);
}


/*************************************************************************
...
**************************************************************************/
int send_packet_game_info(struct connection *pc,
			  const struct packet_game_info *pinfo)
{
  int i;
  SEND_PACKET_START(PACKET_GAME_INFO);

  dio_put_uint16(&dout, pinfo->gold);
  dio_put_uint32(&dout, pinfo->tech);
  dio_put_uint8(&dout, pinfo->researchcost);
  dio_put_uint32(&dout, pinfo->skill_level);
  dio_put_uint32(&dout, pinfo->timeout);
  dio_put_uint32(&dout, pinfo->end_year);
  dio_put_uint32(&dout, pinfo->year);
  dio_put_uint8(&dout, pinfo->min_players);
  dio_put_uint8(&dout, pinfo->max_players);
  dio_put_uint8(&dout, pinfo->nplayers);
  dio_put_uint8(&dout, pinfo->player_idx);
  dio_put_uint32(&dout, pinfo->globalwarming);
  dio_put_uint32(&dout, pinfo->heating);
  dio_put_uint32(&dout, pinfo->nuclearwinter);
  dio_put_uint32(&dout, pinfo->cooling);
  dio_put_uint8(&dout, pinfo->cityfactor);
  dio_put_uint8(&dout, pinfo->diplcost);
  dio_put_uint8(&dout, pinfo->freecost);
  dio_put_uint8(&dout, pinfo->conquercost);
  dio_put_uint8(&dout, pinfo->unhappysize);
  dio_put_bool8(&dout, pinfo->angrycitizen);

  for (i = 0; i < A_LAST /*game.num_tech_types */ ; i++)
    dio_put_uint8(&dout, pinfo->global_advances[i]);
  for (i = 0; i < B_LAST /*game.num_impr_types */ ; i++)
    dio_put_uint16(&dout, pinfo->global_wonders[i]);

  dio_put_uint8(&dout, pinfo->techpenalty);
  dio_put_uint8(&dout, pinfo->foodbox);
  dio_put_uint8(&dout, pinfo->civstyle);
  dio_put_bool8(&dout, pinfo->spacerace);

  /* computed values */
  dio_put_uint32(&dout, pinfo->seconds_to_turndone);

  dio_put_uint32(&dout, pinfo->turn);

  SEND_PACKET_END;
}

/*************************************************************************
...
**************************************************************************/
struct packet_game_info *receive_packet_game_info(struct connection *pc)
{
  int i;
  RECEIVE_PACKET_START(packet_game_info, pinfo);

  dio_get_uint16(&din, &pinfo->gold);
  dio_get_uint32(&din, &pinfo->tech);
  dio_get_uint8(&din, &pinfo->researchcost);
  dio_get_uint32(&din, &pinfo->skill_level);
  dio_get_uint32(&din, &pinfo->timeout);
  dio_get_uint32(&din, &pinfo->end_year);
  dio_get_uint32(&din, &pinfo->year);
  dio_get_uint8(&din, &pinfo->min_players);
  dio_get_uint8(&din, &pinfo->max_players);
  dio_get_uint8(&din, &pinfo->nplayers);
  dio_get_uint8(&din, &pinfo->player_idx);
  dio_get_uint32(&din, &pinfo->globalwarming);
  dio_get_uint32(&din, &pinfo->heating);
  dio_get_uint32(&din, &pinfo->nuclearwinter);
  dio_get_uint32(&din, &pinfo->cooling);
  dio_get_uint8(&din, &pinfo->cityfactor);
  dio_get_uint8(&din, &pinfo->diplcost);
  dio_get_uint8(&din, &pinfo->freecost);
  dio_get_uint8(&din, &pinfo->conquercost);
  dio_get_uint8(&din, &pinfo->unhappysize);
  dio_get_bool8(&din, &pinfo->angrycitizen);

  for (i = 0; i < A_LAST /*game.num_tech_types */ ; i++)
    dio_get_uint8(&din, &pinfo->global_advances[i]);
  for (i = 0; i < B_LAST /*game.num_impr_types */ ; i++)
    dio_get_uint16(&din, &pinfo->global_wonders[i]);

  dio_get_uint8(&din, &pinfo->techpenalty);
  dio_get_uint8(&din, &pinfo->foodbox);
  dio_get_uint8(&din, &pinfo->civstyle);
  dio_get_bool8(&din, &pinfo->spacerace);

  /* computed values */
  dio_get_uint32(&din, &pinfo->seconds_to_turndone);

  dio_get_uint32(&din, &pinfo->turn);

  RECEIVE_PACKET_END(pinfo);
}

/*************************************************************************
...
**************************************************************************/
int send_packet_map_info(struct connection *pc,
			 const struct packet_map_info *pinfo)
{
  SEND_PACKET_START(PACKET_MAP_INFO);

  dio_put_uint8(&dout, pinfo->xsize);
  dio_put_uint8(&dout, pinfo->ysize);
  dio_put_bool8(&dout, pinfo->is_earth);

  SEND_PACKET_END;
}

/*************************************************************************
...
**************************************************************************/
struct packet_map_info *receive_packet_map_info(struct connection *pc)
{
  RECEIVE_PACKET_START(packet_map_info, pinfo);

  dio_get_uint8(&din, &pinfo->xsize);
  dio_get_uint8(&din, &pinfo->ysize);
  dio_get_bool8(&din, &pinfo->is_earth);

  RECEIVE_PACKET_END(pinfo);
}

/*************************************************************************
...
**************************************************************************/
struct packet_tile_info *receive_packet_tile_info(struct connection *pc)
{
  RECEIVE_PACKET_START(packet_tile_info, packet);

  dio_get_uint8(&din, &packet->x);
  dio_get_uint8(&din, &packet->y);
  dio_get_uint8(&din, &packet->type);
  dio_get_uint16(&din, &packet->special);
  dio_get_uint8(&din, &packet->known);

  RECEIVE_PACKET_END(packet);
}

struct packet_unittype_info *receive_packet_unittype_info(struct connection
							  *pc)
{
  RECEIVE_PACKET_START(packet_unittype_info, packet);

  dio_get_uint8(&din, &packet->type);
  dio_get_uint8(&din, &packet->action);

  RECEIVE_PACKET_END(packet);
}

/*************************************************************************
...
**************************************************************************/
int send_packet_tile_info(struct connection *pc,
			  const struct packet_tile_info *pinfo)
 {
  SEND_PACKET_START(PACKET_TILE_INFO);

  dio_put_uint8(&dout, pinfo->x);
  dio_put_uint8(&dout, pinfo->y);
  dio_put_uint8(&dout, pinfo->type);
  dio_put_uint16(&dout, pinfo->special);
  dio_put_uint8(&dout, pinfo->known);

  SEND_PACKET_END;
}

/**************************************************************************
...
**************************************************************************/
int send_packet_new_year(struct connection *pc,
			 const struct packet_new_year *request)
{
  SEND_PACKET_START(PACKET_NEW_YEAR);

  dio_put_uint32(&dout, request->year);
  dio_put_uint32(&dout, request->turn);

  SEND_PACKET_END;
}

/**************************************************************************
...
**************************************************************************/
int send_packet_unittype_info(struct connection *pc, int type, int action)
{
  SEND_PACKET_START(PACKET_UNITTYPE_UPGRADE);

  dio_put_uint8(&dout, type);
  dio_put_uint8(&dout, action);

  SEND_PACKET_END;
}

/**************************************************************************
...
**************************************************************************/
struct packet_generic_empty *receive_packet_generic_empty(struct connection
							  *pc)
{
  RECEIVE_PACKET_START(packet_generic_empty, packet);

  RECEIVE_PACKET_END(packet);
}

/**************************************************************************
...
**************************************************************************/
int send_packet_generic_empty(struct connection *pc, enum packet_type type)
{
  SEND_PACKET_START(type);

  SEND_PACKET_END;
}

/*************************************************************************
...
**************************************************************************/
int send_packet_unit_info(struct connection *pc,
			  const struct packet_unit_info *req)
{
  unsigned char pack;
  SEND_PACKET_START(PACKET_UNIT_INFO);

  dio_put_uint16(&dout, req->id);
  dio_put_uint8(&dout, req->owner);
  pack = (COND_SET_BIT(req->select_it, 2) |
	  COND_SET_BIT(req->carried, 3) |
	  COND_SET_BIT(req->veteran, 4) |
	  COND_SET_BIT(req->ai, 5) |
	  COND_SET_BIT(req->paradropped, 6) |
	  COND_SET_BIT(req->connecting, 7));
  dio_put_uint8(&dout, pack);
  dio_put_uint8(&dout, req->x);
  dio_put_uint8(&dout, req->y);
  dio_put_uint16(&dout, req->homecity);
  dio_put_uint8(&dout, req->type);
  dio_put_uint8(&dout, req->movesleft);
  dio_put_uint8(&dout, req->hp);
  dio_put_uint8(&dout, req->upkeep);
  dio_put_uint8(&dout, req->upkeep_food);
  dio_put_uint8(&dout, req->upkeep_gold);
  dio_put_uint8(&dout, req->unhappiness);
  dio_put_uint8(&dout, req->activity);
  dio_put_uint8(&dout, req->activity_count);
  dio_put_uint8(&dout, req->goto_dest_x);
  dio_put_uint8(&dout, req->goto_dest_y);
  dio_put_uint16(&dout, req->activity_target);
  dio_put_uint8(&dout, req->packet_use);
  dio_put_uint16(&dout, req->info_city_id);
  dio_put_uint16(&dout, req->serial_num);

  if (req->fuel > 0)
    dio_put_uint8(&dout, req->fuel);

  SEND_PACKET_END;
}

/*************************************************************************
...
**************************************************************************/
int send_packet_city_info(struct connection *pc,
			  const struct packet_city_info *req)
{
  int data;
  SEND_PACKET_START(PACKET_CITY_INFO);

  dio_put_uint16(&dout, req->id);
  dio_put_uint8(&dout, req->owner);
  dio_put_uint8(&dout, req->x);
  dio_put_uint8(&dout, req->y);
  dio_put_string(&dout, req->name);

  dio_put_uint8(&dout, req->size);

  for (data = 0; data < 5; data++) {
    dio_put_uint8(&dout, req->ppl_angry[data]);
    dio_put_uint8(&dout, req->ppl_happy[data]);
    dio_put_uint8(&dout, req->ppl_content[data]);
    dio_put_uint8(&dout, req->ppl_unhappy[data]);
  }

  dio_put_uint8(&dout, req->ppl_elvis);
  dio_put_uint8(&dout, req->ppl_scientist);
  dio_put_uint8(&dout, req->ppl_taxman);

  dio_put_uint8(&dout, req->food_prod);
  dio_put_uint8(&dout, req->food_surplus);
  dio_put_uint16(&dout, req->shield_prod);
  dio_put_uint16(&dout, req->shield_surplus);
  dio_put_uint16(&dout, req->trade_prod);
  dio_put_uint16(&dout, req->tile_trade);
  dio_put_uint16(&dout, req->corruption);

  dio_put_uint16(&dout, req->luxury_total);
  dio_put_uint16(&dout, req->tax_total);
  dio_put_uint16(&dout, req->science_total);

  dio_put_uint16(&dout, req->food_stock);
  dio_put_uint16(&dout, req->shield_stock);
  dio_put_uint16(&dout, req->pollution);
  dio_put_uint8(&dout, req->currently_building);

  dio_put_sint16(&dout, req->turn_last_built);
  dio_put_sint16(&dout, req->turn_changed_target);
  dio_put_uint8(&dout, req->changed_from_id);
  dio_put_uint16(&dout, req->before_change_shields);

  dio_put_uint16(&dout, req->disbanded_shields);
  dio_put_uint16(&dout, req->caravan_shields);

  dio_put_worklist(&dout, &req->worklist, TRUE);

  dio_put_uint8(&dout, (COND_SET_BIT(req->is_building_unit, 0) |
			COND_SET_BIT(req->did_buy, 1) |
			COND_SET_BIT(req->did_sell, 2) |
			COND_SET_BIT(req->was_happy, 3) |
			COND_SET_BIT(req->airlift, 4) |
			COND_SET_BIT(req->diplomat_investigate, 5) |
			COND_SET_BIT(req->changed_from_is_unit, 6)));

  dio_put_city_map(&dout, (char *) req->city_map);
  dio_put_bit_string(&dout, (char *) req->improvements);

  /* only 8 options allowed before need to extend protocol */
  dio_put_uint8(&dout, req->city_options);

  dio_put_uint32(&dout, req->turn_founded);

  for (data = 0; data < NUM_TRADEROUTES; data++) {
    if (req->trade[data] != 0) {
      dio_put_uint16(&dout, req->trade[data]);
      dio_put_uint8(&dout, req->trade_value[data]);
    }
  }

  SEND_PACKET_END;
}

/*************************************************************************
...
**************************************************************************/
struct packet_city_info *receive_packet_city_info(struct connection *pc)
{
  int data;
  RECEIVE_PACKET_START(packet_city_info, packet);

  dio_get_uint16(&din, &packet->id);
  dio_get_uint8(&din, &packet->owner);
  dio_get_uint8(&din, &packet->x);
  dio_get_uint8(&din, &packet->y);
  dio_get_string(&din, packet->name, sizeof(packet->name));

  dio_get_uint8(&din, &packet->size);
  for (data = 0; data < 5; data++) {
    dio_get_uint8(&din, &packet->ppl_angry[data]);
    dio_get_uint8(&din, &packet->ppl_happy[data]);
    dio_get_uint8(&din, &packet->ppl_content[data]);
    dio_get_uint8(&din, &packet->ppl_unhappy[data]);
  }
  dio_get_uint8(&din, &packet->ppl_elvis);
  dio_get_uint8(&din, &packet->ppl_scientist);
  dio_get_uint8(&din, &packet->ppl_taxman);

  dio_get_uint8(&din, &packet->food_prod);
  dio_get_uint8(&din, &packet->food_surplus);
  if (packet->food_surplus > 127)
    packet->food_surplus -= 256;
  dio_get_uint16(&din, &packet->shield_prod);
  dio_get_uint16(&din, &packet->shield_surplus);
  if (packet->shield_surplus > 32767)
    packet->shield_surplus -= 65536;
  dio_get_uint16(&din, &packet->trade_prod);
  dio_get_uint16(&din, &packet->tile_trade);
  dio_get_uint16(&din, &packet->corruption);

  dio_get_uint16(&din, &packet->luxury_total);
  dio_get_uint16(&din, &packet->tax_total);
  dio_get_uint16(&din, &packet->science_total);

  dio_get_uint16(&din, &packet->food_stock);
  dio_get_uint16(&din, &packet->shield_stock);
  dio_get_uint16(&din, &packet->pollution);
  dio_get_uint8(&din, &packet->currently_building);

  dio_get_sint16(&din, &packet->turn_last_built);
  dio_get_sint16(&din, &packet->turn_changed_target);
  dio_get_uint8(&din, &packet->changed_from_id);
  dio_get_uint16(&din, &packet->before_change_shields);

  dio_get_uint16(&din, &packet->disbanded_shields);
  dio_get_uint16(&din, &packet->caravan_shields);

  dio_get_worklist(&din, &packet->worklist);

  dio_get_uint8(&din, &data);
  packet->is_building_unit = TEST_BIT(data, 0);
  packet->did_buy = TEST_BIT(data, 1);
  packet->did_sell = TEST_BIT(data, 2);
  packet->was_happy = TEST_BIT(data, 3);
  packet->airlift = TEST_BIT(data, 4);
  packet->diplomat_investigate = TEST_BIT(data, 5);
  packet->changed_from_is_unit = TEST_BIT(data, 6);

  dio_get_city_map(&din, (char *) packet->city_map,
		   sizeof(packet->city_map));
  dio_get_bit_string(&din, (char *) packet->improvements,
		     sizeof(packet->improvements));

  dio_get_uint8(&din, &packet->city_options);

  dio_get_uint32(&din, &packet->turn_founded);

  for (data = 0; data < NUM_TRADEROUTES; data++) {
    if (dio_input_remaining(&din) < 3)
      break;
    dio_get_uint16(&din, &packet->trade[data]);
    dio_get_uint8(&din, &packet->trade_value[data]);
  }
  for (; data < NUM_TRADEROUTES; data++) {
    packet->trade_value[data] = packet->trade[data] = 0;
  }

  RECEIVE_PACKET_END(packet);
}

/*************************************************************************
...
**************************************************************************/
int send_packet_short_city(struct connection *pc,
			   const struct packet_short_city *req)
{
  SEND_PACKET_START(PACKET_SHORT_CITY);

  dio_put_uint16(&dout, req->id);
  dio_put_uint8(&dout, req->owner);
  dio_put_uint8(&dout, req->x);
  dio_put_uint8(&dout, req->y);
  dio_put_string(&dout, req->name);

  dio_put_uint8(&dout, req->size);

  dio_put_uint8(&dout, (COND_SET_BIT(req->happy, 0) |
			COND_SET_BIT(req->capital, 1) |
			COND_SET_BIT(req->walls, 2)));

  dio_put_uint16(&dout, req->tile_trade);

  SEND_PACKET_END;
}


/*************************************************************************
...
**************************************************************************/
struct packet_short_city *receive_packet_short_city(struct connection *pc)
{
  int i;
  RECEIVE_PACKET_START(packet_short_city, packet);

  dio_get_uint16(&din, &packet->id);
  dio_get_uint8(&din, &packet->owner);
  dio_get_uint8(&din, &packet->x);
  dio_get_uint8(&din, &packet->y);
  dio_get_string(&din, packet->name, sizeof(packet->name));

  dio_get_uint8(&din, &packet->size);

  dio_get_uint8(&din, &i);
  packet->happy = TEST_BIT(i, 0);
  packet->capital = TEST_BIT(i, 1);
  packet->walls = TEST_BIT(i, 2);

  dio_get_uint16(&din, &packet->tile_trade);

  RECEIVE_PACKET_END(packet);
}

/*************************************************************************
...
**************************************************************************/
struct packet_unit_info *receive_packet_unit_info(struct connection *pc)
{
  int pack;
  RECEIVE_PACKET_START(packet_unit_info, packet);

  dio_get_uint16(&din, &packet->id);
  dio_get_uint8(&din, &packet->owner);
  dio_get_uint8(&din, &pack);

  packet->select_it = TEST_BIT(pack, 2);
  packet->carried = TEST_BIT(pack, 3);
  packet->veteran = TEST_BIT(pack, 4);
  packet->ai = TEST_BIT(pack, 5);
  packet->paradropped = TEST_BIT(pack, 6);
  packet->connecting = TEST_BIT(pack, 7);
  dio_get_uint8(&din, &packet->x);
  dio_get_uint8(&din, &packet->y);
  dio_get_uint16(&din, &packet->homecity);
  dio_get_uint8(&din, &packet->type);
  dio_get_uint8(&din, &packet->movesleft);
  dio_get_uint8(&din, &packet->hp);
  dio_get_uint8(&din, &packet->upkeep);
  dio_get_uint8(&din, &packet->upkeep_food);
  dio_get_uint8(&din, &packet->upkeep_gold);
  dio_get_uint8(&din, &packet->unhappiness);
  dio_get_uint8(&din, &packet->activity);
  dio_get_uint8(&din, &packet->activity_count);
  dio_get_uint8(&din, &packet->goto_dest_x);
  dio_get_uint8(&din, &packet->goto_dest_y);
  dio_get_uint16(&din, (int *) &packet->activity_target);
  dio_get_uint8(&din, &packet->packet_use);
  dio_get_uint16(&din, &packet->info_city_id);
  dio_get_uint16(&din, &packet->serial_num);

  if (dio_input_remaining(&din) >= 1) {
    dio_get_uint8(&din, &packet->fuel);
  } else {
    packet->fuel = 0;
  }

  RECEIVE_PACKET_END(packet);
}

/**************************************************************************
...
**************************************************************************/
struct packet_new_year *receive_packet_new_year(struct connection *pc)
{
  RECEIVE_PACKET_START(packet_new_year, packet);

  dio_get_uint32(&din, &packet->year);
  dio_get_uint32(&din, &packet->turn);

  RECEIVE_PACKET_END(packet);
}


/**************************************************************************
...
**************************************************************************/
int send_packet_move_unit(struct connection *pc,
			  const struct packet_move_unit *request)
{
  SEND_PACKET_START(PACKET_MOVE_UNIT);

  dio_put_uint8(&dout, request->x);
  dio_put_uint8(&dout, request->y);
  dio_put_uint16(&dout, request->unid);

  SEND_PACKET_END;
}

/**************************************************************************
...
**************************************************************************/
struct packet_move_unit *receive_packet_move_unit(struct connection *pc)
{
  RECEIVE_PACKET_START(packet_move_unit, packet);

  dio_get_uint8(&din, &packet->x);
  dio_get_uint8(&din, &packet->y);
  dio_get_uint16(&din, &packet->unid);

  RECEIVE_PACKET_END(packet);
}

/**************************************************************************
...
**************************************************************************/
int send_packet_req_join_game(struct connection *pc,
			      const struct packet_req_join_game *request)
{
  SEND_PACKET_START(PACKET_REQUEST_JOIN_GAME);

  dio_put_string(&dout, request->short_name);
  dio_put_uint32(&dout, request->major_version);
  dio_put_uint32(&dout, request->minor_version);
  dio_put_uint32(&dout, request->patch_version);
  dio_put_string(&dout, request->capability);
  dio_put_string(&dout, request->name);
  dio_put_string(&dout, request->version_label);

  SEND_PACKET_END;
}

/**************************************************************************
Fills in conn.id automatically, no need to set in packet_join_game_reply.
**************************************************************************/
int send_packet_join_game_reply(struct connection *pc,
				const struct packet_join_game_reply *reply)
{
  SEND_PACKET_START(PACKET_JOIN_GAME_REPLY);

  dio_put_bool32(&dout, reply->you_can_join);
  dio_put_string(&dout, reply->message);
  dio_put_string(&dout, reply->capability);

  /* This must stay even at new releases! */
  if (has_capability("conn_info", pc->capability)) {
    dio_put_uint32(&dout, pc->id);
  }

  SEND_PACKET_END;
}

/**************************************************************************
...
**************************************************************************/
int send_packet_generic_message(struct connection *pc, enum packet_type type,
				const struct packet_generic_message *packet)
{
  SEND_PACKET_START(type);

  if (packet->x == -1) {
    /* since we can currently only send unsigned ints... */
    assert(MAP_MAX_WIDTH <= 255 && MAP_MAX_HEIGHT <= 255);
    dio_put_uint8(&dout, 255);
    dio_put_uint8(&dout, 255);
  } else {
    dio_put_uint8(&dout, packet->x);
    dio_put_uint8(&dout, packet->y);
  }
  dio_put_uint32(&dout, packet->event);

  dio_put_string(&dout, packet->message);

  SEND_PACKET_END;
}

/**************************************************************************
...
**************************************************************************/
int send_packet_generic_integer(struct connection *pc, enum packet_type type,
				const struct packet_generic_integer *packet)
{
  SEND_PACKET_START(type);

  dio_put_uint32(&dout, packet->value);

  SEND_PACKET_END;
}

/**************************************************************************
...
**************************************************************************/
struct packet_req_join_game *
receive_packet_req_join_game(struct connection *pc)
{
  RECEIVE_PACKET_START(packet_req_join_game, packet);

  dio_get_string(&din, packet->short_name, sizeof(packet->short_name));
  dio_get_uint32(&din, &packet->major_version);
  dio_get_uint32(&din, &packet->minor_version);
  dio_get_uint32(&din, &packet->patch_version);
  dio_get_string(&din, packet->capability, sizeof(packet->capability));
  if (dio_input_remaining(&din) > 0) {
    dio_get_string(&din, packet->name, sizeof(packet->name));
  } else {
    sz_strlcpy(packet->name, packet->short_name);
  }
  if (dio_input_remaining(&din) > 0) {
    dio_get_string(&din, packet->version_label, sizeof(packet->version_label));
  } else {
    packet->version_label[0] = '\0';
  }

  RECEIVE_PACKET_END(packet);
}

/**************************************************************************
...
**************************************************************************/
struct packet_join_game_reply *
receive_packet_join_game_reply(struct connection *pc)
{
  RECEIVE_PACKET_START(packet_join_game_reply, packet);

  dio_get_bool32(&din, &packet->you_can_join);
  dio_get_string(&din, packet->message, sizeof(packet->message));
  dio_get_string(&din, packet->capability, sizeof(packet->capability));

  /* This must stay even at new releases! */
  /* NOTE: pc doesn't yet have capability filled in!  Use packet value: */
  if (has_capability("conn_info", packet->capability)) {
    dio_get_uint32(&din, &packet->conn_id);
  } else {
    packet->conn_id = 0;
  }

  RECEIVE_PACKET_END(packet);
}

/**************************************************************************
...
**************************************************************************/
struct packet_generic_message *
receive_packet_generic_message(struct connection *pc)
{
  RECEIVE_PACKET_START(packet_generic_message, packet);

  dio_get_uint8(&din, &packet->x);
  dio_get_uint8(&din, &packet->y);
  if (packet->x == 255) { /* unsigned encoding for no position */
    packet->x = -1;
    packet->y = -1;
  }

  dio_get_uint32(&din, &packet->event);
  dio_get_string(&din, packet->message, sizeof(packet->message));
  
  RECEIVE_PACKET_END(packet);
}

/**************************************************************************
...
**************************************************************************/
struct packet_generic_integer *
receive_packet_generic_integer(struct connection *pc)
{
  RECEIVE_PACKET_START(packet_generic_integer, packet);

  dio_get_uint32(&din, &packet->value);

  RECEIVE_PACKET_END(packet);
}

/**************************************************************************
...
**************************************************************************/
int send_packet_alloc_nation(struct connection *pc, 
			     const struct packet_alloc_nation *packet)
{
  SEND_PACKET_START(PACKET_ALLOC_NATION);

  dio_put_uint32(&dout, packet->nation_no);
  dio_put_string(&dout, packet->name);
  dio_put_bool8(&dout,packet->is_male);
  dio_put_uint8(&dout,packet->city_style);

  SEND_PACKET_END;
}

/**************************************************************************
...
**************************************************************************/
struct packet_alloc_nation *
receive_packet_alloc_nation(struct connection *pc)
{
  RECEIVE_PACKET_START(packet_alloc_nation, packet);

  dio_get_uint32(&din, &packet->nation_no);
  dio_get_string(&din, packet->name, sizeof(packet->name));
  dio_get_bool8(&din, &packet->is_male);
  dio_get_uint8(&din, &packet->city_style);

  RECEIVE_PACKET_END(packet);
}

/**************************************************************************
...
**************************************************************************/
int send_packet_generic_values(struct connection *pc, enum packet_type type,
			       const struct packet_generic_values *req)
{
  SEND_PACKET_START(type);

  dio_put_uint16(&dout, req->id);
  dio_put_uint32(&dout, req->value1);
  dio_put_uint32(&dout, req->value2);

  SEND_PACKET_END;
}

/**************************************************************************
...
**************************************************************************/
struct packet_generic_values *
receive_packet_generic_values(struct connection *pc)
{
  RECEIVE_PACKET_START(packet_generic_values, packet);

  dio_get_uint16(&din, &packet->id);
  if (dio_input_remaining(&din) >= 4) {
    dio_get_uint32(&din, &packet->value1);
  } else {
    packet->value1 = 0;
  }
  if (dio_input_remaining(&din) >= 4) {
    dio_get_uint32(&din, &packet->value2);
  } else {
    packet->value2 = 0;
  }

  RECEIVE_PACKET_END(packet);
}

/*************************************************************************
...
**************************************************************************/
int send_packet_ruleset_control(struct connection *pc, 
				const struct packet_ruleset_control *packet)
{
  SEND_PACKET_START(PACKET_RULESET_CONTROL);
  
  dio_put_uint8(&dout, packet->aqueduct_size);
  dio_put_uint8(&dout, packet->sewer_size);
  dio_put_uint8(&dout, packet->add_to_size_limit);
  dio_put_uint8(&dout, packet->notradesize);
  dio_put_uint8(&dout, packet->fulltradesize);

  dio_put_uint8(&dout, packet->rtech.cathedral_plus);
  dio_put_uint8(&dout, packet->rtech.cathedral_minus);
  dio_put_uint8(&dout, packet->rtech.colosseum_plus);
  dio_put_uint8(&dout, packet->rtech.temple_plus);

  dio_put_uint8(&dout, packet->government_count);
  dio_put_uint8(&dout, packet->default_government);
  dio_put_uint8(&dout, packet->government_when_anarchy);

  dio_put_uint8(&dout, packet->num_unit_types);
  dio_put_uint8(&dout, packet->num_impr_types);
  dio_put_uint8(&dout, packet->num_tech_types);
 
  dio_put_uint8(&dout, packet->nation_count);
  dio_put_uint8(&dout, packet->playable_nation_count);
  dio_put_uint8(&dout, packet->style_count);

  dio_put_tech_list(&dout, packet->rtech.partisan_req);

  if (has_capability("team", pc->capability)) {
    int i;

    for (i = 0; i < MAX_NUM_TEAMS; i++) {
      dio_put_string(&dout, packet->team_name[i]);
    }
  }

  SEND_PACKET_END;
}

/*************************************************************************
...
**************************************************************************/
struct packet_ruleset_control *
receive_packet_ruleset_control(struct connection *pc)
{
  int i;

  RECEIVE_PACKET_START(packet_ruleset_control, packet);

  dio_get_uint8(&din, &packet->aqueduct_size);
  dio_get_uint8(&din, &packet->sewer_size);
  dio_get_uint8(&din, &packet->add_to_size_limit);
  dio_get_uint8(&din, &packet->notradesize);
  dio_get_uint8(&din, &packet->fulltradesize);

  dio_get_uint8(&din, &packet->rtech.cathedral_plus);
  dio_get_uint8(&din, &packet->rtech.cathedral_minus);
  dio_get_uint8(&din, &packet->rtech.colosseum_plus);
  dio_get_uint8(&din, &packet->rtech.temple_plus);
  
  dio_get_uint8(&din, &packet->government_count);
  dio_get_uint8(&din, &packet->default_government);
  dio_get_uint8(&din, &packet->government_when_anarchy);

  dio_get_uint8(&din, &packet->num_unit_types);
  dio_get_uint8(&din, &packet->num_impr_types);
  dio_get_uint8(&din, &packet->num_tech_types);

  dio_get_uint8(&din, &packet->nation_count);
  dio_get_uint8(&din, &packet->playable_nation_count);
  dio_get_uint8(&din, &packet->style_count);

  dio_get_tech_list(&din, packet->rtech.partisan_req);

  for (i = 0; i < MAX_NUM_TEAMS; i++) {
    if (has_capability("team", pc->capability)) {
      dio_get_string(&din, packet->team_name[i], 
                     sizeof(packet->team_name[i]));
    } else {
      packet->team_name[i][0] = '\0';
    }
  }

  RECEIVE_PACKET_END(packet);
}

/**************************************************************************
...
**************************************************************************/
int send_packet_ruleset_unit(struct connection *pc,
			     const struct packet_ruleset_unit *packet)
{
  SEND_PACKET_START(PACKET_RULESET_UNIT);

  dio_put_uint8(&dout, packet->id);
  dio_put_uint8(&dout, packet->move_type);
  dio_put_uint16(&dout, packet->build_cost);
  dio_put_uint8(&dout, packet->attack_strength);
  dio_put_uint8(&dout, packet->defense_strength);
  dio_put_uint8(&dout, packet->move_rate);
  dio_put_uint8(&dout, packet->tech_requirement);
  dio_put_uint8(&dout, packet->vision_range);
  dio_put_uint8(&dout, packet->transport_capacity);
  dio_put_uint8(&dout, packet->hp);
  dio_put_uint8(&dout, packet->firepower);
  dio_put_uint8(&dout, packet->obsoleted_by);
  dio_put_uint8(&dout, packet->fuel);
  DIO_BV_PUT(&dout, packet->flags);
  DIO_BV_PUT(&dout, packet->roles);
  dio_put_uint8(&dout, packet->happy_cost);   /* unit upkeep -- SKi */
  dio_put_uint8(&dout, packet->shield_cost);
  dio_put_uint8(&dout, packet->food_cost);
  dio_put_uint8(&dout, packet->gold_cost);
  dio_put_string(&dout, packet->name);
  dio_put_string(&dout, packet->graphic_str);
  dio_put_string(&dout, packet->graphic_alt);
  dio_put_string(&dout, packet->sound_move);
  dio_put_string(&dout, packet->sound_move_alt);
  dio_put_string(&dout, packet->sound_fight);
  dio_put_string(&dout, packet->sound_fight_alt);

  if (BV_ISSET(packet->flags, F_PARATROOPERS)) {
    dio_put_uint16(&dout, packet->paratroopers_range);
    dio_put_uint8(&dout, packet->paratroopers_mr_req);
    dio_put_uint8(&dout, packet->paratroopers_mr_sub);
  }
  dio_put_uint8(&dout, packet->pop_cost);

  /* This must be last, so client can determine length: */
  if(packet->helptext) {
    dio_put_string(&dout, packet->helptext);
  }

  SEND_PACKET_END;
}

/**************************************************************************
...
**************************************************************************/
struct packet_ruleset_unit *
receive_packet_ruleset_unit(struct connection *pc)
{
  int len;
  RECEIVE_PACKET_START(packet_ruleset_unit, packet);

  dio_get_uint8(&din, &packet->id);
  dio_get_uint8(&din, &packet->move_type);
  dio_get_uint16(&din, &packet->build_cost);
  dio_get_uint8(&din, &packet->attack_strength);
  dio_get_uint8(&din, &packet->defense_strength);
  dio_get_uint8(&din, &packet->move_rate);
  dio_get_uint8(&din, &packet->tech_requirement);
  dio_get_uint8(&din, &packet->vision_range);
  dio_get_uint8(&din, &packet->transport_capacity);
  dio_get_uint8(&din, &packet->hp);
  dio_get_uint8(&din, &packet->firepower);
  dio_get_uint8(&din, &packet->obsoleted_by);
  if (packet->obsoleted_by > 127) {
    packet->obsoleted_by-=256;
  }
  dio_get_uint8(&din, &packet->fuel);
  DIO_BV_GET(&din, packet->flags);
  DIO_BV_GET(&din, packet->roles);
  dio_get_uint8(&din, &packet->happy_cost);   /* unit upkeep -- SKi */
  dio_get_uint8(&din, &packet->shield_cost);
  dio_get_uint8(&din, &packet->food_cost);
  dio_get_uint8(&din, &packet->gold_cost);
  dio_get_string(&din, packet->name, sizeof(packet->name));
  dio_get_string(&din, packet->graphic_str, sizeof(packet->graphic_str));
  dio_get_string(&din, packet->graphic_alt, sizeof(packet->graphic_alt));
  dio_get_string(&din, packet->sound_move, sizeof(packet->sound_move));
  dio_get_string(&din, packet->sound_move_alt, sizeof(packet->sound_move_alt));
  dio_get_string(&din, packet->sound_fight, sizeof(packet->sound_fight));
  dio_get_string(&din, packet->sound_fight_alt,
	      sizeof(packet->sound_fight_alt));

  if (BV_ISSET(packet->flags, F_PARATROOPERS)) {
    dio_get_uint16(&din, &packet->paratroopers_range);
    dio_get_uint8(&din, &packet->paratroopers_mr_req);
    dio_get_uint8(&din, &packet->paratroopers_mr_sub);
  } else {
    packet->paratroopers_range=0;
    packet->paratroopers_mr_req=0;
    packet->paratroopers_mr_sub=0;
  }
  dio_get_uint8(&din, &packet->pop_cost);

  len = dio_input_remaining(&din);
  if (len > 0) {
    packet->helptext = fc_malloc(len);
    dio_get_string(&din, packet->helptext, len);
  } else {
    packet->helptext = NULL;
  }

  RECEIVE_PACKET_END(packet);
}


/**************************************************************************
...
**************************************************************************/
int send_packet_ruleset_tech(struct connection *pc,
			     const struct packet_ruleset_tech *packet)
{
  SEND_PACKET_START(PACKET_RULESET_TECH);

  dio_put_uint8(&dout, packet->id);
  dio_put_uint8(&dout, packet->req[0]);
  dio_put_uint8(&dout, packet->req[1]);
  dio_put_uint32(&dout, packet->flags);
  dio_put_uint32(&dout, packet->preset_cost);
  dio_put_uint32(&dout, packet->num_reqs);
  dio_put_string(&dout, packet->name);
  
  /* This must be last, so client can determine length: */
  if(packet->helptext) {
    dio_put_string(&dout, packet->helptext);
  }

  SEND_PACKET_END;
}

/**************************************************************************
...
**************************************************************************/
struct packet_ruleset_tech *
receive_packet_ruleset_tech(struct connection *pc)
{
  int len;
  RECEIVE_PACKET_START(packet_ruleset_tech, packet);

  dio_get_uint8(&din, &packet->id);
  dio_get_uint8(&din, &packet->req[0]);
  dio_get_uint8(&din, &packet->req[1]);
  dio_get_uint32(&din, &packet->flags);
  dio_get_uint32(&din, &packet->preset_cost);
  dio_get_uint32(&din, &packet->num_reqs);
  dio_get_string(&din, packet->name, sizeof(packet->name));

  len = dio_input_remaining(&din);
  if (len > 0) {
    packet->helptext = fc_malloc(len);
    dio_get_string(&din, packet->helptext, len);
  } else {
    packet->helptext = NULL;
  }

  RECEIVE_PACKET_END(packet);
}

/**************************************************************************
...
**************************************************************************/
int send_packet_ruleset_building(struct connection *pc,
			        const struct packet_ruleset_building *packet)
{
  struct impr_effect *eff;
  int count;
  SEND_PACKET_START(PACKET_RULESET_BUILDING);

  dio_put_uint8(&dout, packet->id);
  dio_put_uint8(&dout, packet->tech_req);
  dio_put_uint8(&dout, packet->bldg_req);
  dio_put_uint8_vec8(&dout, (int *)packet->terr_gate, T_LAST);
  dio_put_uint16_vec8(&dout, (int *)packet->spec_gate, S_NO_SPECIAL);
  dio_put_uint8(&dout, packet->equiv_range);
  dio_put_uint8_vec8(&dout, packet->equiv_dupl, B_LAST);
  dio_put_uint8_vec8(&dout, packet->equiv_repl, B_LAST);
  dio_put_uint8(&dout, packet->obsolete_by);
  dio_put_bool8(&dout, packet->is_wonder);
  dio_put_uint16(&dout, packet->build_cost);
  dio_put_uint8(&dout, packet->upkeep);
  dio_put_uint8(&dout, packet->sabotage);
  for (count = 0, eff = packet->effect; eff->type != EFT_LAST;
       count++, eff++) {
    /* nothing */
  }
  dio_put_uint8(&dout, count);
  for (eff = packet->effect; eff->type != EFT_LAST; eff++) {
    dio_put_uint8(&dout, eff->type);
    dio_put_uint8(&dout, eff->range);
    dio_put_sint16(&dout, eff->amount);
    dio_put_uint8(&dout, eff->survives);
    dio_put_uint8(&dout, eff->cond_bldg);
    dio_put_uint8(&dout, eff->cond_gov);
    dio_put_uint8(&dout, eff->cond_adv);
    dio_put_uint8(&dout, eff->cond_eff);
    dio_put_uint8(&dout, eff->aff_unit);
    dio_put_uint8(&dout, eff->aff_terr);
    dio_put_uint16(&dout, eff->aff_spec);
  }
  dio_put_uint8(&dout, packet->variant);	/* FIXME: remove when gen-impr obsoletes */
  dio_put_string(&dout, packet->name);

  dio_put_string(&dout, packet->soundtag);
  dio_put_string(&dout, packet->soundtag_alt);

  /* This must be last, so client can determine length: */
  if(packet->helptext) {
    dio_put_string(&dout, packet->helptext);
  }
  
  SEND_PACKET_END;
}

/**************************************************************************
...
**************************************************************************/
struct packet_ruleset_building *
receive_packet_ruleset_building(struct connection *pc)
{
  int len, inx, count;
  RECEIVE_PACKET_START(packet_ruleset_building, packet);

  dio_get_uint8(&din, &packet->id);
  dio_get_uint8(&din, &packet->tech_req);
  dio_get_uint8(&din, &packet->bldg_req);
  dio_get_uint8_vec8(&din, (int **)&packet->terr_gate, T_LAST);
  dio_get_uint16_vec8(&din, (int **)&packet->spec_gate, S_NO_SPECIAL);
  dio_get_uint8(&din, (int *)&packet->equiv_range);
  dio_get_uint8_vec8(&din, &packet->equiv_dupl, B_LAST);
  dio_get_uint8_vec8(&din, &packet->equiv_repl, B_LAST);
  dio_get_uint8(&din, &packet->obsolete_by);
  dio_get_bool8(&din, &packet->is_wonder);
  dio_get_uint16(&din, &packet->build_cost);
  dio_get_uint8(&din, &packet->upkeep);
  dio_get_uint8(&din, &packet->sabotage);
  dio_get_uint8(&din, &count);
  packet->effect = fc_malloc((count + 1) * sizeof(struct impr_effect));
  for (inx = 0; inx < count; inx++) {
    dio_get_uint8(&din, (int *)&(packet->effect[inx].type));
    dio_get_uint8(&din, (int *)&(packet->effect[inx].range));
    dio_get_sint16(&din, &(packet->effect[inx].amount));
    dio_get_uint8(&din, &(packet->effect[inx].survives));
    dio_get_uint8(&din, &(packet->effect[inx].cond_bldg));
    dio_get_uint8(&din, &(packet->effect[inx].cond_gov));
    dio_get_uint8(&din, &(packet->effect[inx].cond_adv));
    dio_get_uint8(&din, (int *)&(packet->effect[inx].cond_eff));
    dio_get_uint8(&din, (int *)&(packet->effect[inx].aff_unit));
    dio_get_uint8(&din, (int *)&(packet->effect[inx].aff_terr));
    dio_get_uint16(&din, (int *)&(packet->effect[inx].aff_spec));
  }
  packet->effect[count].type = EFT_LAST;
  dio_get_uint8(&din, &packet->variant);	/* FIXME: remove when gen-impr obsoletes */
  dio_get_string(&din, packet->name, sizeof(packet->name));

  dio_get_string(&din, packet->soundtag, sizeof(packet->soundtag));
  dio_get_string(&din, packet->soundtag_alt, sizeof(packet->soundtag_alt));

  len = dio_input_remaining(&din);
  if (len > 0) {
    packet->helptext = fc_malloc(len);
    dio_get_string(&din, packet->helptext, len);
  } else {
    packet->helptext = NULL;
  }

  RECEIVE_PACKET_END(packet);
}

/**************************************************************************
...
**************************************************************************/
int send_packet_ruleset_terrain(struct connection *pc,
				const struct packet_ruleset_terrain *packet)
{
  int i;
  SEND_PACKET_START(PACKET_RULESET_TERRAIN);

  dio_put_uint8(&dout, packet->id);
  dio_put_string(&dout, packet->terrain_name);
  dio_put_uint8(&dout, packet->movement_cost);
  dio_put_uint8(&dout, packet->defense_bonus);
  dio_put_uint8(&dout, packet->food);
  dio_put_uint8(&dout, packet->shield);
  dio_put_uint8(&dout, packet->trade);
  dio_put_string(&dout, packet->special_1_name);
  dio_put_uint8(&dout, packet->food_special_1);
  dio_put_uint8(&dout, packet->shield_special_1);
  dio_put_uint8(&dout, packet->trade_special_1);
  dio_put_string(&dout, packet->special_2_name);
  dio_put_uint8(&dout, packet->food_special_2);
  dio_put_uint8(&dout, packet->shield_special_2);
  dio_put_uint8(&dout, packet->trade_special_2);
  dio_put_uint8(&dout, packet->road_trade_incr);
  dio_put_uint8(&dout, packet->road_time);
  dio_put_uint8(&dout, packet->irrigation_result);
  dio_put_uint8(&dout, packet->irrigation_food_incr);
  dio_put_uint8(&dout, packet->irrigation_time);
  dio_put_uint8(&dout, packet->mining_result);
  dio_put_uint8(&dout, packet->mining_shield_incr);
  dio_put_uint8(&dout, packet->mining_time);
  dio_put_uint8(&dout, packet->transform_result);
  dio_put_uint8(&dout, packet->transform_time);
  dio_put_string(&dout, packet->graphic_str);
  dio_put_string(&dout, packet->graphic_alt);
  for(i=0; i<2; i++) {
    dio_put_string(&dout, packet->special[i].graphic_str);
    dio_put_string(&dout, packet->special[i].graphic_alt);
  }

  /* This must be last, so client can determine length: */
  if(packet->helptext) {
    dio_put_string(&dout, packet->helptext);
  }
  
  SEND_PACKET_END;
}

/**************************************************************************
...
**************************************************************************/
struct packet_ruleset_terrain *
receive_packet_ruleset_terrain(struct connection *pc)
{
  int i, len;
  RECEIVE_PACKET_START(packet_ruleset_terrain, packet);

  dio_get_uint8(&din, &packet->id);
  dio_get_string(&din, packet->terrain_name, sizeof(packet->terrain_name));
  dio_get_uint8(&din, &packet->movement_cost);
  dio_get_uint8(&din, &packet->defense_bonus);
  dio_get_uint8(&din, &packet->food);
  dio_get_uint8(&din, &packet->shield);
  dio_get_uint8(&din, &packet->trade);
  dio_get_string(&din, packet->special_1_name, sizeof(packet->special_1_name));
  dio_get_uint8(&din, &packet->food_special_1);
  dio_get_uint8(&din, &packet->shield_special_1);
  dio_get_uint8(&din, &packet->trade_special_1);
  dio_get_string(&din, packet->special_2_name, sizeof(packet->special_2_name));
  dio_get_uint8(&din, &packet->food_special_2);
  dio_get_uint8(&din, &packet->shield_special_2);
  dio_get_uint8(&din, &packet->trade_special_2);
  dio_get_uint8(&din, &packet->road_trade_incr);
  dio_get_uint8(&din, &packet->road_time);
  dio_get_uint8(&din, (int*)&packet->irrigation_result);
  dio_get_uint8(&din, &packet->irrigation_food_incr);
  dio_get_uint8(&din, &packet->irrigation_time);
  dio_get_uint8(&din, (int*)&packet->mining_result);
  dio_get_uint8(&din, &packet->mining_shield_incr);
  dio_get_uint8(&din, &packet->mining_time);
  dio_get_uint8(&din, (int*)&packet->transform_result);
  dio_get_uint8(&din, &packet->transform_time);
  
  dio_get_string(&din, packet->graphic_str, sizeof(packet->graphic_str));
  dio_get_string(&din, packet->graphic_alt, sizeof(packet->graphic_alt));
  for(i=0; i<2; i++) {
    dio_get_string(&din, packet->special[i].graphic_str,
		sizeof(packet->special[i].graphic_str));
    dio_get_string(&din, packet->special[i].graphic_alt,
		sizeof(packet->special[i].graphic_alt));
  }

  len = dio_input_remaining(&din);
  if (len > 0) {
    packet->helptext = fc_malloc(len);
    dio_get_string(&din, packet->helptext, len);
  } else {
    packet->helptext = NULL;
  }

  RECEIVE_PACKET_END(packet);
}

/**************************************************************************
...
**************************************************************************/
int send_packet_ruleset_terrain_control(struct connection *pc,
					const struct terrain_misc *packet)
{
  SEND_PACKET_START(PACKET_RULESET_TERRAIN_CONTROL);

  dio_put_uint8(&dout, packet->river_style);
  dio_put_bool8(&dout, packet->may_road);
  dio_put_bool8(&dout, packet->may_irrigate);
  dio_put_bool8(&dout, packet->may_mine);
  dio_put_bool8(&dout, packet->may_transform);
  dio_put_uint8(&dout, packet->ocean_reclaim_requirement);
  dio_put_uint8(&dout, packet->land_channel_requirement);
  dio_put_uint8(&dout, packet->river_move_mode);
  dio_put_uint16(&dout, packet->river_defense_bonus);
  dio_put_uint16(&dout, packet->river_trade_incr);
  dio_put_uint16(&dout, packet->fortress_defense_bonus);
  dio_put_uint16(&dout, packet->road_superhighway_trade_bonus);
  dio_put_uint16(&dout, packet->rail_food_bonus);
  dio_put_uint16(&dout, packet->rail_shield_bonus);
  dio_put_uint16(&dout, packet->rail_trade_bonus);
  dio_put_uint16(&dout, packet->farmland_supermarket_food_bonus);
  dio_put_uint16(&dout, packet->pollution_food_penalty);
  dio_put_uint16(&dout, packet->pollution_shield_penalty);
  dio_put_uint16(&dout, packet->pollution_trade_penalty);
  dio_put_uint16(&dout, packet->fallout_food_penalty);
  dio_put_uint16(&dout, packet->fallout_shield_penalty);
  dio_put_uint16(&dout, packet->fallout_trade_penalty);

  if (packet->river_help_text) {
    dio_put_string(&dout, packet->river_help_text);
  }

  SEND_PACKET_END;
}

/**************************************************************************
...
**************************************************************************/
struct terrain_misc *
receive_packet_ruleset_terrain_control(struct connection *pc)
{
  int len;
  RECEIVE_PACKET_START(terrain_misc, packet);

  dio_get_uint8(&din, (int*)&packet->river_style);
  dio_get_bool8(&din, &packet->may_road);
  dio_get_bool8(&din, &packet->may_irrigate);
  dio_get_bool8(&din, &packet->may_mine);
  dio_get_bool8(&din, &packet->may_transform);
  dio_get_uint8(&din, (int*)&packet->ocean_reclaim_requirement);
  dio_get_uint8(&din, (int*)&packet->land_channel_requirement);
  dio_get_uint8(&din, (int*)&packet->river_move_mode);
  dio_get_uint16(&din, &packet->river_defense_bonus);
  dio_get_uint16(&din, &packet->river_trade_incr);
  dio_get_uint16(&din, &packet->fortress_defense_bonus);
  dio_get_uint16(&din, &packet->road_superhighway_trade_bonus);
  dio_get_uint16(&din, &packet->rail_food_bonus);
  dio_get_uint16(&din, &packet->rail_shield_bonus);
  dio_get_uint16(&din, &packet->rail_trade_bonus);
  dio_get_uint16(&din, &packet->farmland_supermarket_food_bonus);
  dio_get_uint16(&din, &packet->pollution_food_penalty);
  dio_get_uint16(&din, &packet->pollution_shield_penalty);
  dio_get_uint16(&din, &packet->pollution_trade_penalty);
  dio_get_uint16(&din, &packet->fallout_food_penalty);
  dio_get_uint16(&din, &packet->fallout_shield_penalty);
  dio_get_uint16(&din, &packet->fallout_trade_penalty);

  len = dio_input_remaining(&din);
  if (len > 0) {
    packet->river_help_text = fc_malloc(len);
    dio_get_string(&din, packet->river_help_text, len);
  } else {
    packet->river_help_text = NULL;
  }

  RECEIVE_PACKET_END(packet);
}

/**************************************************************************
...
**************************************************************************/
int send_packet_ruleset_government(struct connection *pc,
			     const struct packet_ruleset_government *packet)
{
  SEND_PACKET_START(PACKET_RULESET_GOVERNMENT);
  
  dio_put_uint8(&dout, packet->id);
  
  dio_put_uint8(&dout, packet->required_tech);
  dio_put_uint8(&dout, packet->max_rate);
  dio_put_uint8(&dout, packet->civil_war);
  dio_put_uint8(&dout, packet->martial_law_max);
  dio_put_uint8(&dout, packet->martial_law_per);
  dio_put_uint8(&dout, packet->empire_size_mod);
  dio_put_uint8(&dout, packet->empire_size_inc);
  dio_put_uint8(&dout, packet->rapture_size);
  
  dio_put_uint8(&dout, packet->unit_happy_cost_factor);
  dio_put_uint8(&dout, packet->unit_shield_cost_factor);
  dio_put_uint8(&dout, packet->unit_food_cost_factor);
  dio_put_uint8(&dout, packet->unit_gold_cost_factor);
  
  dio_put_uint8(&dout, packet->free_happy);
  dio_put_uint8(&dout, packet->free_shield);
  dio_put_uint8(&dout, packet->free_food);
  dio_put_uint8(&dout, packet->free_gold);

  dio_put_uint8(&dout, packet->trade_before_penalty);
  dio_put_uint8(&dout, packet->shields_before_penalty);
  dio_put_uint8(&dout, packet->food_before_penalty);

  dio_put_uint8(&dout, packet->celeb_trade_before_penalty);
  dio_put_uint8(&dout, packet->celeb_shields_before_penalty);
  dio_put_uint8(&dout, packet->celeb_food_before_penalty);

  dio_put_uint8(&dout, packet->trade_bonus);
  dio_put_uint8(&dout, packet->shield_bonus);
  dio_put_uint8(&dout, packet->food_bonus);

  dio_put_uint8(&dout, packet->celeb_trade_bonus);
  dio_put_uint8(&dout, packet->celeb_shield_bonus);
  dio_put_uint8(&dout, packet->celeb_food_bonus);

  dio_put_uint8(&dout, packet->corruption_level);
  dio_put_uint8(&dout, packet->corruption_modifier);
  dio_put_uint8(&dout, packet->fixed_corruption_distance);
  dio_put_uint8(&dout, packet->corruption_distance_factor);
  dio_put_uint8(&dout, packet->extra_corruption_distance);

  dio_put_uint16(&dout, packet->flags);
  dio_put_uint8(&dout, packet->hints);

  dio_put_uint8(&dout, packet->num_ruler_titles);

  dio_put_string(&dout, packet->name);
  dio_put_string(&dout, packet->graphic_str);
  dio_put_string(&dout, packet->graphic_alt);

  /* This must be last, so client can determine length: */
  if(packet->helptext) {
    dio_put_string(&dout, packet->helptext);
  }
  
  SEND_PACKET_END;
}

int send_packet_ruleset_government_ruler_title(struct connection *pc,
		    const struct packet_ruleset_government_ruler_title *packet)
{
  SEND_PACKET_START(PACKET_RULESET_GOVERNMENT_RULER_TITLE);
  
  dio_put_uint8(&dout, packet->gov);
  dio_put_uint8(&dout, packet->id);
  dio_put_uint8(&dout, packet->nation);

  dio_put_string(&dout, packet->male_title);
  dio_put_string(&dout, packet->female_title);
  
  SEND_PACKET_END;
}

/**************************************************************************
...
**************************************************************************/
struct packet_ruleset_government *
receive_packet_ruleset_government(struct connection *pc)
{
  int len;
  RECEIVE_PACKET_START(packet_ruleset_government, packet);

  dio_get_uint8(&din, &packet->id);
  
  dio_get_uint8(&din, &packet->required_tech);
  dio_get_uint8(&din, &packet->max_rate);
  dio_get_uint8(&din, &packet->civil_war);
  dio_get_uint8(&din, &packet->martial_law_max);
  dio_get_uint8(&din, &packet->martial_law_per);
  dio_get_uint8(&din, &packet->empire_size_mod);
  if(packet->empire_size_mod > 127) packet->empire_size_mod-=256;
  dio_get_uint8(&din, &packet->empire_size_inc);
  dio_get_uint8(&din, &packet->rapture_size);
  
  dio_get_uint8(&din, &packet->unit_happy_cost_factor);
  dio_get_uint8(&din, &packet->unit_shield_cost_factor);
  dio_get_uint8(&din, &packet->unit_food_cost_factor);
  dio_get_uint8(&din, &packet->unit_gold_cost_factor);
  
  dio_get_uint8(&din, &packet->free_happy);
  dio_get_uint8(&din, &packet->free_shield);
  dio_get_uint8(&din, &packet->free_food);
  dio_get_uint8(&din, &packet->free_gold);

  dio_get_uint8(&din, &packet->trade_before_penalty);
  dio_get_uint8(&din, &packet->shields_before_penalty);
  dio_get_uint8(&din, &packet->food_before_penalty);

  dio_get_uint8(&din, &packet->celeb_trade_before_penalty);
  dio_get_uint8(&din, &packet->celeb_shields_before_penalty);
  dio_get_uint8(&din, &packet->celeb_food_before_penalty);

  dio_get_uint8(&din, &packet->trade_bonus);
  dio_get_uint8(&din, &packet->shield_bonus);
  dio_get_uint8(&din, &packet->food_bonus);

  dio_get_uint8(&din, &packet->celeb_trade_bonus);
  dio_get_uint8(&din, &packet->celeb_shield_bonus);
  dio_get_uint8(&din, &packet->celeb_food_bonus);

  dio_get_uint8(&din, &packet->corruption_level);
  dio_get_uint8(&din, &packet->corruption_modifier);
  dio_get_uint8(&din, &packet->fixed_corruption_distance);
  dio_get_uint8(&din, &packet->corruption_distance_factor);
  dio_get_uint8(&din, &packet->extra_corruption_distance);

  dio_get_uint16(&din, &packet->flags);
  dio_get_uint8(&din, &packet->hints);

  dio_get_uint8(&din, &packet->num_ruler_titles);

  dio_get_string(&din, packet->name, sizeof(packet->name));
  dio_get_string(&din, packet->graphic_str, sizeof(packet->graphic_str));
  dio_get_string(&din, packet->graphic_alt, sizeof(packet->graphic_alt));
  
  len = dio_input_remaining(&din);
  if (len > 0) {
    packet->helptext = fc_malloc(len);
    dio_get_string(&din, packet->helptext, len);
  } else {
    packet->helptext = NULL;
  }

  freelog(LOG_DEBUG, "recv gov %s", packet->name);

  RECEIVE_PACKET_END(packet);
}

struct packet_ruleset_government_ruler_title *
receive_packet_ruleset_government_ruler_title(struct connection *pc)
{
  RECEIVE_PACKET_START(packet_ruleset_government_ruler_title, packet);

  dio_get_uint8(&din, &packet->gov);
  dio_get_uint8(&din, &packet->id);
  dio_get_uint8(&din, &packet->nation);

  dio_get_string(&din, packet->male_title, sizeof(packet->male_title));
  dio_get_string(&din, packet->female_title, sizeof(packet->female_title));

  RECEIVE_PACKET_END(packet);
}

/**************************************************************************
...
**************************************************************************/
int send_packet_ruleset_nation(struct connection *pc,
			       const struct packet_ruleset_nation *packet)
{
  int i;
  SEND_PACKET_START(PACKET_RULESET_NATION);

  dio_put_uint8(&dout, packet->id);

  dio_put_string(&dout, packet->name);
  dio_put_string(&dout, packet->name_plural);
  dio_put_string(&dout, packet->graphic_str);
  dio_put_string(&dout, packet->graphic_alt);
  dio_put_uint8(&dout, packet->leader_count);
  for( i=0; i<packet->leader_count; i++ ) {
    dio_put_string(&dout, packet->leader_name[i]);
    dio_put_bool8(&dout, packet->leader_sex[i]);
  }
  dio_put_uint8(&dout, packet->city_style);
  dio_put_tech_list(&dout, packet->init_techs);

  SEND_PACKET_END;
}


/**************************************************************************
...
**************************************************************************/
struct packet_ruleset_nation *
receive_packet_ruleset_nation(struct connection *pc)
{
  int i;
  RECEIVE_PACKET_START(packet_ruleset_nation, packet);

  dio_get_uint8(&din, &packet->id);
  dio_get_string(&din, packet->name, sizeof(packet->name));
  dio_get_string(&din, packet->name_plural, sizeof(packet->name_plural));
  dio_get_string(&din, packet->graphic_str, sizeof(packet->graphic_str));
  dio_get_string(&din, packet->graphic_alt, sizeof(packet->graphic_alt));
  dio_get_uint8(&din, &packet->leader_count);

  if (packet->leader_count > MAX_NUM_LEADERS) {
    packet->leader_count = MAX_NUM_LEADERS;
  }

  for (i = 0; i < packet->leader_count; i++) {
    dio_get_string(&din, packet->leader_name[i],
		   sizeof(packet->leader_name[i]));
    dio_get_bool8(&din, &packet->leader_sex[i]);
  }

  dio_get_uint8(&din, &packet->city_style);
  dio_get_tech_list(&din, packet->init_techs);

  RECEIVE_PACKET_END(packet);
}

/**************************************************************************
...
**************************************************************************/
int send_packet_ruleset_city(struct connection *pc,
                             const struct packet_ruleset_city *packet)
{
  SEND_PACKET_START(PACKET_RULESET_CITY);

  dio_put_uint8(&dout, packet->style_id);
  dio_put_uint8(&dout, packet->techreq);
  dio_put_sint16(&dout, packet->replaced_by);           /* I may send -1 */

  dio_put_string(&dout, packet->name);
  dio_put_string(&dout, packet->graphic);
  dio_put_string(&dout, packet->graphic_alt);

  SEND_PACKET_END;
}

/**************************************************************************
...
**************************************************************************/
struct packet_ruleset_city *
receive_packet_ruleset_city(struct connection *pc)
{
  RECEIVE_PACKET_START(packet_ruleset_city, packet);

  dio_get_uint8(&din, &packet->style_id);
  dio_get_uint8(&din, &packet->techreq);
  dio_get_sint16(&din, &packet->replaced_by);           /* may be -1 */

  dio_get_string(&din, packet->name, MAX_LEN_NAME);
  dio_get_string(&din, packet->graphic, MAX_LEN_NAME);
  dio_get_string(&din, packet->graphic_alt, MAX_LEN_NAME);

  RECEIVE_PACKET_END(packet);
}

/**************************************************************************
...
**************************************************************************/
int send_packet_ruleset_game(struct connection *pc,
                             const struct packet_ruleset_game *packet)
{
  SEND_PACKET_START(PACKET_RULESET_GAME);

  dio_put_uint8(&dout, packet->min_city_center_food);
  dio_put_uint8(&dout, packet->min_city_center_shield);
  dio_put_uint8(&dout, packet->min_city_center_trade);
  dio_put_uint8(&dout, packet->min_dist_bw_cities);
  dio_put_uint8(&dout, packet->init_vis_radius_sq);
  dio_put_uint8(&dout, packet->hut_overflight);
  dio_put_bool8(&dout, packet->pillage_select);
  dio_put_uint8(&dout, packet->nuke_contamination);
  dio_put_uint8(&dout, packet->granary_food_ini);
  dio_put_uint8(&dout, packet->granary_food_inc);
  dio_put_uint8(&dout, packet->tech_cost_style);
  dio_put_uint8(&dout, packet->tech_leakage);
  dio_put_tech_list(&dout, packet->global_init_techs);

  SEND_PACKET_END;
}

/**************************************************************************
...
**************************************************************************/
struct packet_ruleset_game *
receive_packet_ruleset_game(struct connection *pc)
{
  RECEIVE_PACKET_START(packet_ruleset_game, packet);

  dio_get_uint8(&din, &packet->min_city_center_food);
  dio_get_uint8(&din, &packet->min_city_center_shield);
  dio_get_uint8(&din, &packet->min_city_center_trade);
  dio_get_uint8(&din, &packet->min_dist_bw_cities);
  dio_get_uint8(&din, &packet->init_vis_radius_sq);
  dio_get_uint8(&din, &packet->hut_overflight);
  dio_get_bool8(&din, &packet->pillage_select);
  dio_get_uint8(&din, &packet->nuke_contamination);
  dio_get_uint8(&din, &packet->granary_food_ini);
  dio_get_uint8(&din, &packet->granary_food_inc);
  dio_get_uint8(&din, &packet->tech_cost_style);
  dio_get_uint8(&din, &packet->tech_leakage);
  dio_get_tech_list(&din, packet->global_init_techs);

  RECEIVE_PACKET_END(packet);
}

/**************************************************************************
...
**************************************************************************/
int send_packet_spaceship_info(struct connection *pc,
			       const struct packet_spaceship_info *packet)
{
  SEND_PACKET_START(PACKET_SPACESHIP_INFO);
  
  dio_put_uint8(&dout, packet->player_num);
  dio_put_uint8(&dout, packet->sship_state);
  dio_put_uint8(&dout, packet->structurals);
  dio_put_uint8(&dout, packet->components);
  dio_put_uint8(&dout, packet->modules);
  dio_put_uint8(&dout, packet->fuel);
  dio_put_uint8(&dout, packet->propulsion);
  dio_put_uint8(&dout, packet->habitation);
  dio_put_uint8(&dout, packet->life_support);
  dio_put_uint8(&dout, packet->solar_panels);
  dio_put_uint16(&dout, packet->launch_year);
  dio_put_uint8(&dout, (packet->population/1000));
  dio_put_uint32(&dout, packet->mass);
  dio_put_uint32(&dout, (int) (packet->support_rate*10000));
  dio_put_uint32(&dout, (int) (packet->energy_rate*10000));
  dio_put_uint32(&dout, (int) (packet->success_rate*10000));
  dio_put_uint32(&dout, (int) (packet->travel_time*10000));
  dio_put_bit_string(&dout, (char*)packet->structure);

  SEND_PACKET_END;
}

/**************************************************************************
...
**************************************************************************/
struct packet_spaceship_info *
receive_packet_spaceship_info(struct connection *pc)
{
  int tmp;
  RECEIVE_PACKET_START(packet_spaceship_info, packet);

  dio_get_uint8(&din, &packet->player_num);
  dio_get_uint8(&din, &packet->sship_state);
  dio_get_uint8(&din, &packet->structurals);
  dio_get_uint8(&din, &packet->components);
  dio_get_uint8(&din, &packet->modules);
  dio_get_uint8(&din, &packet->fuel);
  dio_get_uint8(&din, &packet->propulsion);
  dio_get_uint8(&din, &packet->habitation);
  dio_get_uint8(&din, &packet->life_support);
  dio_get_uint8(&din, &packet->solar_panels);
  dio_get_uint16(&din, &packet->launch_year);
  
  if(packet->launch_year > 32767) packet->launch_year-=65536;
  
  dio_get_uint8(&din, &packet->population);
  packet->population *= 1000;
  dio_get_uint32(&din, &packet->mass);
  
  dio_get_uint32(&din, &tmp);
  packet->support_rate = tmp * 0.0001;
  dio_get_uint32(&din, &tmp);
  packet->energy_rate = tmp * 0.0001;
  dio_get_uint32(&din, &tmp);
  packet->success_rate = tmp * 0.0001;
  dio_get_uint32(&din, &tmp);
  packet->travel_time = tmp * 0.0001;

  dio_get_bit_string(&din, (char *) packet->structure,
		     sizeof(packet->structure));
  
  RECEIVE_PACKET_END(packet);
}

/**************************************************************************
...
**************************************************************************/
int send_packet_spaceship_action(struct connection *pc,
				 const struct packet_spaceship_action *packet)
{
  SEND_PACKET_START(PACKET_SPACESHIP_ACTION);
  
  dio_put_uint8(&dout, packet->action);
  dio_put_uint8(&dout, packet->num);

  SEND_PACKET_END;
}

/**************************************************************************
...
**************************************************************************/
struct packet_spaceship_action *
receive_packet_spaceship_action(struct connection *pc)
{
  RECEIVE_PACKET_START(packet_spaceship_action, packet);

  dio_get_uint8(&din, &packet->action);
  dio_get_uint8(&din, &packet->num);

  RECEIVE_PACKET_END(packet);
}

/**************************************************************************
...
**************************************************************************/
int send_packet_city_name_suggestion(struct connection *pc,
			      const struct packet_city_name_suggestion *packet)
{
  SEND_PACKET_START(PACKET_CITY_NAME_SUGGESTION);
  
  dio_put_uint16(&dout, packet->id);
  dio_put_string(&dout, packet->name);

  SEND_PACKET_END;
}

/**************************************************************************
...
**************************************************************************/
struct packet_city_name_suggestion *
receive_packet_city_name_suggestion(struct connection *pc)
{
  RECEIVE_PACKET_START(packet_city_name_suggestion, packet);

  dio_get_uint16(&din, &packet->id);
  dio_get_string(&din, packet->name, sizeof(packet->name));

  RECEIVE_PACKET_END(packet);
}

/**************************************************************************
...
**************************************************************************/
int send_packet_sabotage_list(struct connection *pc,
			      const struct packet_sabotage_list *packet)
{
  SEND_PACKET_START(PACKET_SABOTAGE_LIST);

  dio_put_uint16(&dout, packet->diplomat_id);
  dio_put_uint16(&dout, packet->city_id);
  dio_put_bit_string(&dout, (char *)packet->improvements);

  SEND_PACKET_END;
}

/**************************************************************************
...
**************************************************************************/
struct packet_sabotage_list *
receive_packet_sabotage_list(struct connection *pc)
{
  RECEIVE_PACKET_START(packet_sabotage_list, packet);

  dio_get_uint16(&din, &packet->diplomat_id);
  dio_get_uint16(&din, &packet->city_id);
  dio_get_bit_string(&din, (char *) packet->improvements,
		     sizeof(packet->improvements));

  RECEIVE_PACKET_END(packet);
}

enum packet_goto_route_type {
  GR_FIRST_MORE, GR_FIRST_LAST, GR_MORE, GR_LAST
};
#define GOTO_CHUNK 20
/**************************************************************************
Chop the route up and send the pieces one by one.
**************************************************************************/
int send_packet_goto_route(struct connection *pc,
			   const struct packet_goto_route *packet,
			   enum goto_route_type type)
{
  int i;
  int num_poses = packet->last_index > packet->first_index ?
    packet->last_index - packet->first_index :
    packet->length - packet->first_index + packet->last_index;
  int num_chunks = (num_poses + GOTO_CHUNK - 1) / GOTO_CHUNK;
  int this_chunk = 1;

  i = packet->first_index;
  assert(num_chunks > 0);
  while (i != packet->last_index) {
    unsigned char buffer[MAX_LEN_PACKET];
    struct data_out dout;
    int chunk_pos;

    dio_output_init(&dout, buffer, sizeof(buffer));
    dio_put_uint16(&dout, 0);

    switch (type) {
    case ROUTE_GOTO:
      dio_put_uint8(&dout, PACKET_GOTO_ROUTE);
      break;
    case ROUTE_PATROL:
      dio_put_uint8(&dout, PACKET_PATROL_ROUTE);
      break;
    default:
      abort();
    }

    chunk_pos = 0;
    if (this_chunk == 1) {
      if (num_chunks == 1)
	dio_put_uint8(&dout, GR_FIRST_LAST);
      else
	dio_put_uint8(&dout, GR_FIRST_MORE);
    } else {
      if (this_chunk == num_chunks)
	dio_put_uint8(&dout, GR_LAST);
      else
	dio_put_uint8(&dout, GR_MORE);
    }

    while (i != packet->last_index && chunk_pos < GOTO_CHUNK) {
      dio_put_uint8(&dout, packet->pos[i].x);
      dio_put_uint8(&dout, packet->pos[i].y);
      i++; i%=packet->length;
      chunk_pos++;
    }
    /* if we finished fill the last chunk with NOPs */
    for (; chunk_pos < GOTO_CHUNK; chunk_pos++) {
      dio_put_uint8(&dout, map.xsize);
      dio_put_uint8(&dout, map.ysize);
    }

    dio_put_uint16(&dout, packet->unit_id);

    {
      size_t size = dio_output_used(&dout);

      dio_output_rewind(&dout);
      dio_put_uint16(&dout, size);
      send_packet_data(pc, buffer, size);
    }
    this_chunk++;
  }

  return 0;
}

/**************************************************************************
Pick up and reassemble the pieces. Note that this means it will return NULL
if the received piece isn't the last one.
**************************************************************************/
struct packet_goto_route *receive_packet_goto_route(struct connection *pc)
{
  struct data_in din;
  int i, num_valid = 0, itype;
  enum packet_goto_route_type type;
  struct map_position pos[GOTO_CHUNK];
  struct map_position *pos2;
  struct packet_goto_route *packet;
  int length, unit_id;

  dio_input_init(&din, pc->buffer->data, pc->buffer->ndata);
  dio_get_uint16(&din, NULL);
  dio_get_uint8(&din, NULL);

  dio_get_uint8(&din, &itype);
  type = itype;
  for (i = 0; i < GOTO_CHUNK; i++) {
    dio_get_uint8(&din, &pos[i].x);
    dio_get_uint8(&din, &pos[i].y);
    if (is_normal_map_pos(pos[i].x, pos[i].y)) {
      num_valid++;
    }
  }
  dio_get_uint16(&din, &unit_id);

  check_packet(&din, pc);
  remove_packet_from_buffer(pc->buffer);

  /* sanity check */
  if (!pc->route)
    pc->route_length = 0;

  switch (type) {
  case GR_FIRST_MORE:
    free(pc->route);
    pc->route = fc_malloc(GOTO_CHUNK * sizeof(struct map_position));
    pc->route_length = GOTO_CHUNK;
    for (i = 0; i < GOTO_CHUNK; i++) {
      pc->route[i].x = pos[i].x;
      pc->route[i].y = pos[i].y;
    }
    return NULL;
  case GR_LAST:
    packet = fc_malloc(sizeof(struct packet_goto_route));
    packet->unit_id = unit_id;
    length = pc->route_length+num_valid+1;
    if (!pc->route)
      freelog(LOG_ERROR, "Got a GR_LAST packet with NULL without previous route");
    packet->pos = fc_malloc(length * sizeof(struct map_position));
    packet->length = length;
    packet->first_index = 0;
    packet->last_index = length-1;
    for (i = 0; i < pc->route_length; i++)
      packet->pos[i] = pc->route[i];
    for (i = 0; i < num_valid; i++)
      packet->pos[i+pc->route_length] = pos[i];
    free(pc->route);
    pc->route = NULL;
    return packet;
  case GR_FIRST_LAST:
    packet = fc_malloc(sizeof(struct packet_goto_route));
    packet->unit_id = unit_id;
    packet->pos = fc_malloc((num_valid+1) * sizeof(struct map_position));
    packet->length = num_valid + 1;
    packet->first_index = 0;
    packet->last_index = num_valid;
    for (i = 0; i < num_valid; i++)
      packet->pos[i] = pos[i];
    return packet;
  case GR_MORE:
    pos2 = fc_malloc((GOTO_CHUNK+pc->route_length) * sizeof(struct map_position));
    if (!pc->route)
      freelog(LOG_ERROR, "Got a GR_MORE packet with NULL without previous route");
    for (i = 0; i < pc->route_length; i++)
      pos2[i] = pc->route[i];
    for (i = 0; i < GOTO_CHUNK; i++)
      pos2[i+pc->route_length] = pos[i];
    free(pc->route);
    pc->route = pos2;
    pc->route_length += GOTO_CHUNK;
    return NULL;
  default:
    freelog(LOG_ERROR, "invalid type in receive_packet_goto_route()");
    return NULL;
  }
}

/**************************************************************************
...
**************************************************************************/
int send_packet_nations_used(struct connection *pc,
			     const struct packet_nations_used *packet)
{
  int i;
  SEND_PACKET_START(PACKET_SELECT_NATION);

  for (i = 0; i < packet->num_nations_used; i++) {
    assert((packet->nations_used[i] & 0xffff) == packet->nations_used[i]);
    dio_put_uint16(&dout, packet->nations_used[i]);
  }

  SEND_PACKET_END;
}

/**************************************************************************
...
**************************************************************************/
struct packet_nations_used *receive_packet_nations_used(struct connection
							*pc)
{
  RECEIVE_PACKET_START(packet_nations_used, packet);

  packet->num_nations_used = 0;

  while (dio_input_remaining(&din) >= 2 &&
	 packet->num_nations_used < MAX_NUM_PLAYERS) {

    dio_get_uint16(&din, &packet->nations_used[packet->num_nations_used]);
    packet->num_nations_used++;
  }

  RECEIVE_PACKET_END(packet);
}

/**************************************************************************
...
**************************************************************************/
int send_packet_attribute_chunk(struct connection *pc,
				struct packet_attribute_chunk *packet)
{
  SEND_PACKET_START(PACKET_ATTRIBUTE_CHUNK);

  assert(packet->total_length > 0
	 && packet->total_length < MAX_ATTRIBUTE_BLOCK);
  /* 500 bytes header, just to be sure */
  assert(packet->chunk_length > 0
	 && packet->chunk_length < MAX_LEN_PACKET - 500);
  assert(packet->chunk_length <= packet->total_length);
  assert(packet->offset >= 0 && packet->offset < packet->total_length);

  freelog(LOG_DEBUG, "sending attribute chunk %d/%d %d", packet->offset,
	  packet->total_length, packet->chunk_length);

  dio_put_uint32(&dout, packet->offset);
  dio_put_uint32(&dout, packet->total_length);
  dio_put_uint32(&dout, packet->chunk_length);
  dio_put_memory(&dout, packet->data, packet->chunk_length);

  SEND_PACKET_END;
}

/**************************************************************************
..
**************************************************************************/
struct packet_attribute_chunk *receive_packet_attribute_chunk(struct
							      connection
							      *pc)
{
  RECEIVE_PACKET_START(packet_attribute_chunk, packet);

  dio_get_uint32(&din, &packet->offset);
  dio_get_uint32(&din, &packet->total_length);
  dio_get_uint32(&din, &packet->chunk_length);

  /*
   * Because of the changes in enum packet_type during the 1.12.1
   * timeframe an old server will trigger the following condition.
   */
  if (packet->total_length <= 0
      || packet->total_length >= MAX_ATTRIBUTE_BLOCK) {
    freelog(LOG_FATAL, _("The server you tried to connect is too old "
			 "(1.12.0 or earlier). Please choose another "
			 "server next time. Good bye."));
    exit(EXIT_FAILURE);
  }
  assert(packet->total_length > 0
	 && packet->total_length < MAX_ATTRIBUTE_BLOCK);
  /* 500 bytes header, just to be sure */
  assert(packet->chunk_length > 0
	 && packet->chunk_length < MAX_LEN_PACKET - 500);
  assert(packet->chunk_length <= packet->total_length);
  assert(packet->offset >= 0 && packet->offset < packet->total_length);

  assert(dio_input_remaining(&din) != -1);
  assert(dio_input_remaining(&din) == packet->chunk_length);

  dio_get_memory(&din, packet->data, packet->chunk_length);

  freelog(LOG_DEBUG, "received attribute chunk %d/%d %d", packet->offset,
	  packet->total_length, packet->chunk_length);

  RECEIVE_PACKET_END(packet);
}

/**************************************************************************
 Updates pplayer->attribute_block according to the given packet.
**************************************************************************/
void generic_handle_attribute_chunk(struct player *pplayer,
				    struct packet_attribute_chunk *chunk)
{
  /* first one in a row */
  if (chunk->offset == 0) {
    if (pplayer->attribute_block.data) {
      free(pplayer->attribute_block.data);
      pplayer->attribute_block.data = NULL;
    }
    pplayer->attribute_block.data = fc_malloc(chunk->total_length);
    pplayer->attribute_block.length = chunk->total_length;
  }
  memcpy((char *) (pplayer->attribute_block.data) + chunk->offset,
	 chunk->data, chunk->chunk_length);
}

/**************************************************************************
 Split the attribute block into chunks and send them over pconn.
**************************************************************************/
void send_attribute_block(const struct player *pplayer,
			  struct connection *pconn)
{
  struct packet_attribute_chunk packet;
  int current_chunk, chunks, bytes_left;

  if (!pplayer || !pplayer->attribute_block.data) {
    return;
  }

  assert(pplayer->attribute_block.length > 0 &&
	 pplayer->attribute_block.length < MAX_ATTRIBUTE_BLOCK);

  chunks =
      (pplayer->attribute_block.length - 1) / ATTRIBUTE_CHUNK_SIZE + 1;
  bytes_left = pplayer->attribute_block.length;

  connection_do_buffer(pconn);

  for (current_chunk = 0; current_chunk < chunks; current_chunk++) {
    int size_of_current_chunk = MIN(bytes_left, ATTRIBUTE_CHUNK_SIZE);

    packet.offset = ATTRIBUTE_CHUNK_SIZE * current_chunk;
    packet.total_length = pplayer->attribute_block.length;
    packet.chunk_length = size_of_current_chunk;

    memcpy(packet.data,
	   (char *) (pplayer->attribute_block.data) + packet.offset,
	   packet.chunk_length);
    bytes_left -= packet.chunk_length;

    send_packet_attribute_chunk(pconn, &packet);
  }

  connection_do_unbuffer(pconn);
}
