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
#ifndef FC__PACKETS_JSON_H
#define FC__PACKETS_JSON_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include <jansson.h>

void *get_packet_from_connection_json(struct connection *pc,
                                      enum packet_type *ptype);

#define SEND_PACKET_START(packet_type) \
  unsigned char buffer[MAX_LEN_PACKET]; \
  char *json_buffer = NULL; \
  struct json_data_out dout; \
  dout.json = json_object(); \
  \
  dio_output_init(&(dout.raw), buffer, sizeof(buffer)); \
  dio_put_uint16_raw(&(dout.raw), 0);                   \
  dio_put_uint16_raw(&(dout.raw), packet_type);         \
  dio_put_uint8_json(&dout, "pid", packet_type);

#define SEND_PACKET_END(packet_type) \
  { \
    json_buffer = json_dumps(dout.json, JSON_COMPACT | JSON_ENSURE_ASCII); \
    if (json_buffer) { \
      dio_put_string_raw(&(dout.raw), json_buffer);     \
    } \
    size_t size = dio_output_used(&(dout.raw)); \
    \
    dio_output_rewind(&(dout.raw));  \
    dio_put_uint16_raw(&(dout.raw), size);      \
    free(json_buffer); \
    json_decref(dout.json); \
    fc_assert(!dout.raw.too_short); \
    return send_packet_data(pc, buffer, size, packet_type); \
  }

#define RECEIVE_PACKET_START(packet_type, result) \
  struct packet_type packet_buf, *result = &packet_buf;

#define RECEIVE_PACKET_END(result) \
  json_decref(pc->json_packet); \
  result = fc_malloc(sizeof(*result)); \
  *result = packet_buf; \
  return result;

#define RECEIVE_PACKET_FIELD_ERROR(field, ...) \
  log_packet("Error on field '" #field "'" __VA_ARGS__); \
  return NULL

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

#endif  /* FC__PACKETS_JSON_H */
