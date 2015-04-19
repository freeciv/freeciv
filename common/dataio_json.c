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

/*
 * The DataIO module provides a system independent (endianess and
 * sizeof(int) independent) way to write and read data. It supports
 * multiple datas which are collected in a buffer. It provides
 * recognition of error cases like "not enough space" or "not enough
 * data".
 */

#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

#ifdef FREECIV_JSON_CONNECTION

#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
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

/* utility */
#include "bitvector.h"
#include "capability.h"
#include "log.h"
#include "mem.h"
#include "support.h"

/* common */
#include "events.h"
#include "player.h"
#include "requirements.h"
#include "tech.h"
#include "worklist.h"

#include "dataio.h"

static bool get_conv(char *dst, size_t ndst, const char *src,
		     size_t nsrc);

static DIO_GET_CONV_FUN get_conv_callback = get_conv;

/* Uncomment to make field range tests to asserts, fatal with -F */
/* #define FIELD_RANGE_ASSERT */

#ifdef FIELD_RANGE_ASSERT
/* This evaluates _test_ twice. If that's a problem,
 * it should evaluate it just once and store result to variable.
 * That would lose verbosity of the assert message. */
#define FIELD_RANGE_TEST(_test_, _action_, _format_, ...) \
  fc_assert(!(_test_));                                   \
  if (_test_) {                                           \
    _action_                                              \
  }
#else
#define FIELD_RANGE_TEST(_test_, _action_, _format_, ...) \
  if (_test_) {                                           \
    _action_                                              \
    log_error(_format_, ## __VA_ARGS__);                  \
  }
#endif

/**************************************************************************
 Returns FALSE if the destination isn't large enough or the source was
 bad. This is default get_conv_callback.
**************************************************************************/
static bool get_conv(char *dst, size_t ndst, const char *src,
		     size_t nsrc)
{
  size_t len = nsrc;		/* length to copy, not including null */
  bool ret = TRUE;

  if (ndst > 0 && len >= ndst) {
    ret = FALSE;
    len = ndst - 1;
  }

  memcpy(dst, src, len);
  dst[len] = '\0';

  return ret;
}

/**************************************************************************
  Insert 8 bit value with json.
**************************************************************************/
void dio_put_uint8_json(struct json_data_out *dout, char *key, int value)
{
  json_object_set_new(dout->json, key, json_integer(value));
}

/**************************************************************************
  Insert value using 32 bits. May overflow.
**************************************************************************/
void dio_put_uint16_json(struct json_data_out *dout, char *key, int value)
{
  json_object_set_new(dout->json, key, json_integer(value));
}

/**************************************************************************
  Insert unit type numbers from value array as 8 bit values until there is
  value U_LAST or MAX_NUM_UNIT_LIST numbers have been inserted.
**************************************************************************/
void dio_put_unit_list_json(struct json_data_out *dout, char *key, const int *value)
{
  /* TODO: implement */
}

/**************************************************************************
  Insert building type numbers from value array as 8 bit values until there
  is value B_LAST or MAX_NUM_BUILDING_LIST numbers have been inserted.
**************************************************************************/
void dio_put_building_list_json(struct json_data_out *dout, char *key,
                                const int *value)
{
  /* TODO: implement */
}

/**************************************************************************
...
**************************************************************************/
void dio_put_worklist_json(struct json_data_out *dout, char *key,
                           const struct worklist *pwl)
{
  /* TODO: implement */
}

/**************************************************************************
 Receive uint8 value to dest with json.
**************************************************************************/
bool dio_get_uint8_json(json_t *json_packet, char *key, int *dest)
{
  json_t *pint = json_object_get(json_packet, key);

  if (!pint) {
    log_error("ERROR: Unable to get uint8 with key: %s", key);
    return FALSE;
  } 
  *dest = json_integer_value(pint);

  if (!dest) {
    log_error("ERROR: Unable to get unit8 with key: %s", key);
    return FALSE;
  }

  return TRUE;
}

/**************************************************************************
 Receive uint16 value to dest with json.
**************************************************************************/
bool dio_get_uint16_json(json_t *json_packet, char *key, int *dest)
{
  json_t *pint = json_object_get(json_packet, key);

  if (!pint) {
    log_error("ERROR: Unable to get uint16 with key: %s", key);
    return FALSE;
  } 
  *dest = json_integer_value(pint);

  if (!dest) {
    log_error("ERROR: Unable to get unit16 with key: %s", key);
    return FALSE;
  }

  return TRUE;
}

/**************************************************************************
 ..
**************************************************************************/
bool dio_get_uint32_json(json_t *json_packet, char *key, int *dest)
{
  json_t *pint = json_object_get(json_packet, key);

  if (!pint) {
    log_error("ERROR: Unable to get uint32 with key: %s", key);
    return FALSE;
  } 
  *dest = json_integer_value(pint);

  if (!dest) {
    log_error("ERROR: Unable to get unit32 with key: %s", key);
    return FALSE;
  }

  return TRUE;
}

/**************************************************************************
 ..
**************************************************************************/
bool dio_get_tech_list_json(json_t *json_packet, char *key, int *dest)
{
  /* TODO: implement */
  return TRUE;
}

/**************************************************************************
  Take unit type numbers until UTYF_LAST encountered, or MAX_NUM_UNIT_LIST
  types retrieved.
**************************************************************************/
bool dio_get_unit_list_json(json_t *json_packet, char *key, int *dest)
{
  /* TODO: implement */
  return TRUE;
}

/**************************************************************************
 ..
**************************************************************************/
bool dio_get_building_list_json(json_t *json_packet, char *key, int *dest)
{
  /* TODO: implement */
  return TRUE;
}

/**************************************************************************
 ..
**************************************************************************/
bool dio_get_worklist_json(json_t *json_packet, char *key, struct worklist *pwl)
{
  /* TODO: implement */
  return TRUE;
}

/**************************************************************************
...
**************************************************************************/
bool dio_get_uint8_vec8_json(json_t *json_packet, char *key,
                             int **values, int stop_value)
{
  /* TODO: implement */
  return TRUE;
}

/**************************************************************************
...
**************************************************************************/
bool dio_get_uint16_vec8_json(json_t *json_packet, char *key, int **values,
                              int stop_value)
{
  /* TODO: implement */
  return TRUE;
}

/**************************************************************************
  Read a single requirement.
**************************************************************************/
bool dio_get_requirement_json(json_t *json_packet, char *key,
                              struct requirement *preq)
{
  int kind, range, value;
  bool survives, present;

  /* Find the requirement object. */
  json_t *requirement = json_object_get(json_packet, key);
  if (!requirement) {
    log_error("ERROR: Unable to get requirement with key: %s", key);
    return FALSE;
  }

  /* Find the requirement object fields and translate their values. */
  if (!dio_get_uint8_json(requirement, "kind", &kind)) {
    log_error("ERROR: Unable to get part of requirement with key: %s",
              key);
    return FALSE;
  }

  if (!dio_get_sint32_json(requirement, "value", &value)) {
    log_error("ERROR: Unable to get part of requirement with key: %s",
              key);
    return FALSE;
  }

  if (!dio_get_uint8_json(requirement, "range", &range)) {
    log_error("ERROR: Unable to get part of requirement with key: %s",
              key);
    return FALSE;
  }

  if (!dio_get_bool8_json(requirement, "survives", &survives)) {
    log_error("ERROR: Unable to get part of requirement with key: %s",
              key);
    return FALSE;
  }

  if (!dio_get_bool8_json(requirement, "present", &present)) {
    log_error("ERROR: Unable to get part of requirement with key: %s",
              key);
    return FALSE;
  }

  /* Create a requirement with the values sent over the network. */
  *preq = req_from_values(kind, range, survives, present, value);

  return TRUE;
}

/**************************************************************************
...
**************************************************************************/
void dio_put_uint32_json(struct json_data_out *dout, char *key, int value)
{
  json_object_set_new(dout->json, key, json_integer(value));
}

/**************************************************************************
...
**************************************************************************/
void dio_put_bool8_json(struct json_data_out *dout, char *key, bool value)
{
  json_object_set_new(dout->json, key, value ? json_true() : json_false());
}

/**************************************************************************
...
**************************************************************************/
void dio_put_bool32_json(struct json_data_out *dout, char *key, bool value)
{
  json_object_set_new(dout->json, key, value ? json_true() : json_false());
}

/**************************************************************************
...
**************************************************************************/
void dio_put_ufloat_json(struct json_data_out *dout, char *key,
                         float value, int float_factor)
{
  json_object_set_new(dout->json, key, json_real(value));
}

/**************************************************************************
...
**************************************************************************/
void dio_put_sfloat_json(struct json_data_out *dout, char *key,
                         float value, int float_factor)
{
  json_object_set_new(dout->json, key, json_real(value));
}

/**************************************************************************
...
**************************************************************************/
void dio_put_uint8_vec8_json(struct json_data_out *dout, char *key,
                             int *values, int stop_value)
{
  /* TODO: implement. */
}

/**************************************************************************
...
**************************************************************************/
void dio_put_uint16_vec8_json(struct json_data_out *dout, char *key, int *values,
                              int stop_value)
{
  /* TODO: implement. */
}

/**************************************************************************
  Send block of memory as byte array
**************************************************************************/
void dio_put_memory_json(struct json_data_out *dout, char *key, const void *value,
                         size_t size)
{
  int i;
  char fullkey[512];

  for (i = 0; i < size; i++) {
    fc_snprintf(fullkey, sizeof(fullkey), "%s_%d", key, i);

    dio_put_uint8_json(dout, fullkey, ((unsigned char *)value)[i]);
  }
}

/**************************************************************************
...
**************************************************************************/
void dio_put_string_json(struct json_data_out *dout, char *key, const char *value)
{
  json_object_set_new(dout->json, key, json_string(value));
}

/**************************************************************************
...
**************************************************************************/
void dio_put_tech_list_json(struct json_data_out *dout, char *key, const int *value)
{
  /* TODO: implement */
}

/**************************************************************************
  Insert a single requirement.
**************************************************************************/
void dio_put_requirement_json(struct json_data_out *dout, char *key,
                              const struct requirement *preq)
{
  int kind, range, value;
  bool survives, present;

  /* Create the requirement object. */
  json_t *requirement = json_object();

  /* Read the requirment values. */
  req_get_values(preq, &kind, &range, &survives, &present, &value);

  /* Write the requirement values to the fields of the requirment
   * object. */
  json_object_set(requirement, "kind", json_integer(kind));
  json_object_set(requirement, "value", json_integer(value));

  json_object_set(requirement, "range", json_integer(range));

  json_object_set(requirement, "survives", json_boolean(survives));
  json_object_set(requirement, "present", json_boolean(present));

  /* Put the requirement object in the packet. */
  json_object_set_new(dout->json, key, requirement);
}

/**************************************************************************
...
**************************************************************************/
bool dio_get_bool8_json(json_t *json_packet, char *key, bool *dest)
{
  json_t *pbool = json_object_get(json_packet, key);

  if (!pbool) {
    log_error("ERROR: Unable to get bool8 with key: %s", key);
    return FALSE;
  } 
  *dest = json_is_true(pbool);

  if (!dest) {
    log_error("ERROR: Unable to get bool with key: %s", key);
    return FALSE;
  }

  return TRUE;
}

/**************************************************************************
...
**************************************************************************/
bool dio_get_bool32_json(json_t *json_packet, char *key, bool *dest)
{
  json_t *pbool = json_object_get(json_packet, key);

  if (!pbool) {
    log_error("ERROR: Unable to get bool32 with key: %s", key);
    return FALSE;
  }
  *dest = json_is_true(pbool);

  if (!dest) {
    log_error("ERROR: Unable to get bool32 with key: %s", key);
    return FALSE;
  }

  return TRUE;
}

/**************************************************************************
  ...
**************************************************************************/
bool dio_get_ufloat_json(json_t *json_packet, char *key, float *dest,
                         int float_factor)
{
  json_t *preal = json_object_get(json_packet, key);

  if (!preal) {
    log_error("ERROR: Unable to get real with key: %s", key);
    return FALSE;
  }
  *dest = json_real_value(preal);

  return TRUE;
}

/**************************************************************************
  ...
**************************************************************************/
bool dio_get_sfloat_json(json_t *json_packet, char *key, float *dest,
                         int float_factor)
{
  json_t *preal = json_object_get(json_packet, key);

  if (!preal) {
    log_error("ERROR: Unable to get real with key: %s", key);
    return FALSE;
  }
  *dest = json_real_value(preal);

  return TRUE;
}

/**************************************************************************
...
**************************************************************************/
bool dio_get_sint8_json(json_t *json_packet, char *key, int *dest)
{
  json_t *pint = json_object_get(json_packet, key);

  if (!pint) {
    log_error("ERROR: Unable to get sint8 with key: %s", key);
    return FALSE;
  }
  *dest = json_integer_value(pint);

  if (!dest) {
    log_error("ERROR: Unable to get sint8 with key: %s", key);
    return FALSE;
  }

  return TRUE;
}

/**************************************************************************
...
**************************************************************************/
bool dio_get_sint16_json(json_t *json_packet, char *key, int *dest)
{
  json_t *pint = json_object_get(json_packet, key);

  if (!pint) {
    log_error("ERROR: Unable to get sint16 with key: %s", key);
    return FALSE;
  }
  *dest = json_integer_value(pint);

  if (!dest) {
    log_error("ERROR: Unable to get sint16 with key: %s", key);
    return FALSE;
  }

  return TRUE;
}

/**************************************************************************
  Receive block of memory as byte array
**************************************************************************/
bool dio_get_memory_json(json_t *json_packet, char *key, void *dest,
                         size_t dest_size)
{
   int i;
  char fullkey[512];

  for (i = 0; i < dest_size; i++) {
    int val;

    fc_snprintf(fullkey, sizeof(fullkey), "%s_%d", key, i);

    if (!dio_get_uint8_json(json_packet, fullkey, &val)) {
      return FALSE;
    }
    ((unsigned char *)dest)[i] = val;
  }

  return TRUE;
}

/**************************************************************************
...
**************************************************************************/
bool dio_get_string_json(json_t *json_packet, char *key, char *dest,
                         size_t max_dest_size)
{
  json_t *pstring = json_object_get(json_packet, key);

  if (!pstring) {
    log_error("ERROR: Unable to get string with key: %s", key);
    return FALSE;
  }
  const char *result_str = json_string_value(pstring);

  if (dest && !(*get_conv_callback) (dest, max_dest_size, result_str, strlen(result_str))) {
    log_error("ERROR: Unable to get string with key: %s", key);
    return FALSE;
  }

  return TRUE;
}

#endif /* FREECIV_JSON_CONNECTION */
