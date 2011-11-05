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
#include "unittype.h"
#include "worklist.h"


#define MAX_LEN_USERNAME        10        /* see below */
#define MAX_LEN_MSG             1536
#define MAX_LEN_ROUTE		2000	  /* MAX_LEN_PACKET/2 - header */

/* The size of opaque (void *) data sent in the network packet.  To avoid
 * fragmentation issues, this SHOULD NOT be larger than the standard
 * ethernet or PPP 1500 byte frame size (with room for headers).
 *
 * Do not spend much time optimizing, you have no idea of the actual dynamic
 * path characteristics between systems, such as VPNs and tunnels.
 */
#define ATTRIBUTE_CHUNK_SIZE    (1400)

enum report_type {
  REPORT_WONDERS_OF_THE_WORLD,
  REPORT_TOP_5_CITIES,
  REPORT_DEMOGRAPHIC
};

enum spaceship_place_type {
  SSHIP_PLACE_STRUCTURAL,
  SSHIP_PLACE_FUEL,
  SSHIP_PLACE_PROPULSION,
  SSHIP_PLACE_HABITATION,
  SSHIP_PLACE_LIFE_SUPPORT,
  SSHIP_PLACE_SOLAR_PANELS
};

enum unit_info_use {
  UNIT_INFO_IDENTITY,
  UNIT_INFO_CITY_SUPPORTED,
  UNIT_INFO_CITY_PRESENT
};

enum authentication_type {
  AUTH_LOGIN_FIRST,   /* request a password for a returning user */
  AUTH_NEWUSER_FIRST, /* request a password for a new user */
  AUTH_LOGIN_RETRY,   /* inform the client to try a different password */
  AUTH_NEWUSER_RETRY  /* inform the client to try a different [new] password */
};

#include "packets_gen.h"

void *get_packet_from_connection(struct connection *pc, enum packet_type *ptype, bool *presult);
void remove_packet_from_buffer(struct socket_packet_buffer *buffer);

void send_attribute_block(const struct player *pplayer,
			  struct connection *pconn);
void generic_handle_player_attribute_chunk(struct player *pplayer,
					   const struct
					   packet_player_attribute_chunk
					   *chunk);
const char *packet_name(enum packet_type type);
bool packet_has_game_info_flag(enum packet_type type);

void pre_send_packet_player_attribute_chunk(struct connection *pc,
					    struct packet_player_attribute_chunk
					    *packet);

#ifdef DEBUG
#define PACKET_TYPE_SANITY(_type_) \
  if (((_type_ & 0xff00) >> 8) == PACKET_SERVER_JOIN_REQ) { \
    log_error("Packet type %s (%d) has upper byte matching old PACKET_SERVER_JOIN_REQ.", \
              packet_name(_type_), _type_); \
  }
#else  /* DEBUG */
#define PACKET_TYPE_SANITY(_type_)
#endif /* DEBUG */

#define SEND_PACKET_START(type) \
  unsigned char buffer[MAX_LEN_PACKET]; \
  struct data_out dout; \
  \
  dio_output_init(&dout, buffer, sizeof(buffer)); \
  dio_put_uint16(&dout, 0); \
  dio_put_uint16(&dout, type); \
  PACKET_TYPE_SANITY(type)

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
  dio_get_uint16(&din, NULL);

#define RECEIVE_PACKET_END(result) \
  check_packet(&din, pc); \
  remove_packet_from_buffer(pc->buffer); \
  return result;

int send_packet_data(struct connection *pc, unsigned char *data, int len);
void check_packet(struct data_in *din, struct connection *pc);

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
