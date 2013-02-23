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
#ifndef FC__PACKETS_H
#define FC__PACKETS_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

struct connection;
struct data_in;

#include "connection.h"		/* struct connection, MAX_LEN_* */
#include "diptreaty.h"
#include "effects.h"
#include "events.h"
#include "improvement.h"	/* bv_imprs */
#include "map.h"
#include "player.h"
#include "requirements.h"
#include "shared.h"		/* MAX_LEN_ADDR */
#include "spaceship.h"
#include "team.h"
#include "traderoutes.h"
#include "unittype.h"
#include "worklist.h"


#define MAX_LEN_USERNAME        10        /* see below */
/* Used in network protocol. */
#define MAX_LEN_MSG             1536
#define MAX_LEN_ROUTE		2000	  /* MAX_LEN_PACKET/2 - header */

/* The size of opaque (void *) data sent in the network packet.  To avoid
 * fragmentation issues, this SHOULD NOT be larger than the standard
 * ethernet or PPP 1500 byte frame size (with room for headers).
 *
 * Do not spend much time optimizing, you have no idea of the actual dynamic
 * path characteristics between systems, such as VPNs and tunnels.
 *
 * Used in network protocol.
 */
#define ATTRIBUTE_CHUNK_SIZE    (1400)

/* Used in network protocol. */
enum report_type {
  REPORT_WONDERS_OF_THE_WORLD,
  REPORT_TOP_5_CITIES,
  REPORT_DEMOGRAPHIC
};

/* Used in network protocol. */
enum spaceship_place_type {
  SSHIP_PLACE_STRUCTURAL,
  SSHIP_PLACE_FUEL,
  SSHIP_PLACE_PROPULSION,
  SSHIP_PLACE_HABITATION,
  SSHIP_PLACE_LIFE_SUPPORT,
  SSHIP_PLACE_SOLAR_PANELS
};

/* Used in network protocol. */
enum unit_info_use {
  UNIT_INFO_IDENTITY,
  UNIT_INFO_CITY_SUPPORTED,
  UNIT_INFO_CITY_PRESENT
};

/* Used in network protocol. */
enum authentication_type {
  AUTH_LOGIN_FIRST,   /* request a password for a returning user */
  AUTH_NEWUSER_FIRST, /* request a password for a new user */
  AUTH_LOGIN_RETRY,   /* inform the client to try a different password */
  AUTH_NEWUSER_RETRY  /* inform the client to try a different [new] password */
};

#include "packets_gen.h"

void *get_packet_from_connection(struct connection *pc,
                                 enum packet_type *ptype);
void remove_packet_from_buffer(struct socket_packet_buffer *buffer);

void send_attribute_block(const struct player *pplayer,
			  struct connection *pconn);
void generic_handle_player_attribute_chunk(struct player *pplayer,
					   const struct
					   packet_player_attribute_chunk
					   *chunk);
const char *packet_name(enum packet_type type);
bool packet_has_game_info_flag(enum packet_type type);

inline void packet_header_init(struct packet_header *packet_header);
void post_send_packet_server_join_reply(struct connection *pconn,
                                        const struct packet_server_join_reply
                                        *packet);
void post_receive_packet_server_join_reply(struct connection *pconn,
                                           const struct
                                           packet_server_join_reply *packet);

void pre_send_packet_player_attribute_chunk(struct connection *pc,
					    struct packet_player_attribute_chunk
					    *packet);

#define SEND_PACKET_START(packet_type) \
  unsigned char buffer[MAX_LEN_PACKET]; \
  struct data_out dout; \
  \
  dio_output_init(&dout, buffer, sizeof(buffer)); \
  dio_put_type(&dout, pc->packet_header.length, 0); \
  dio_put_type(&dout, pc->packet_header.type, packet_type);

#define SEND_PACKET_END(packet_type) \
  { \
    size_t size = dio_output_used(&dout); \
    \
    dio_output_rewind(&dout); \
    dio_put_type(&dout, pc->packet_header.length, size); \
    fc_assert(!dout.too_short); \
    return send_packet_data(pc, buffer, size, packet_type); \
  }

#define RECEIVE_PACKET_START(packet_type, result) \
  struct data_in din; \
  struct packet_type packet_buf, *result = &packet_buf; \
  \
  dio_input_init(&din, pc->buffer->data, \
                 data_type_size(pc->packet_header.length)); \
  { \
    int size; \
  \
    dio_get_type(&din, pc->packet_header.length, &size); \
    dio_input_init(&din, pc->buffer->data, MIN(size, pc->buffer->ndata)); \
  } \
  dio_input_skip(&din, (data_type_size(pc->packet_header.length) \
                        + data_type_size(pc->packet_header.type)));

#define RECEIVE_PACKET_END(result) \
  if (!packet_check(&din, pc)) { \
    return NULL; \
  } \
  remove_packet_from_buffer(pc->buffer); \
  real_packet = fc_malloc(sizeof(*result)); \
  *result = packet_buf; \
  return result;

#define RECEIVE_PACKET_FIELD_ERROR(field, ...) \
  log_packet("Error on field '" #field "'" __VA_ARGS__); \
  return NULL

int send_packet_data(struct connection *pc, unsigned char *data, int len,
                     enum packet_type packet_type);
bool packet_check(struct data_in *din, struct connection *pc);

/* Utilities to exchange strings and string vectors. */
#define PACKET_STRVEC_SEPARATOR '\3'
#define PACKET_STRVEC_COMPUTE(str, strvec)                                  \
  if (NULL != strvec) {                                                     \
    strvec_to_str(strvec, PACKET_STRVEC_SEPARATOR, str, sizeof(str));       \
  } else {                                                                  \
    str[0] = '\0';                                                          \
  }
#define PACKET_STRVEC_EXTRACT(strvec, str)                                  \
  if ('\0' != str[0]) {                                                     \
    strvec = strvec_new();                                                  \
    strvec_from_str(strvec, PACKET_STRVEC_SEPARATOR, str);                  \
  } else {                                                                  \
    strvec = NULL;                                                          \
  }

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif  /* FC__PACKETS_H */
