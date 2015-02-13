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

static DIO_PUT_CONV_FUN put_conv_callback = NULL;
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
  Sets string conversion callback to be used when putting text.
**************************************************************************/
void dio_set_put_conv_callback(DIO_PUT_CONV_FUN fun)
{
  put_conv_callback = fun;
}

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
  Sets string conversion callback to use when getting text.
**************************************************************************/
void dio_set_get_conv_callback(DIO_GET_CONV_FUN fun)
{
  get_conv_callback = fun;
}

/**************************************************************************
  Returns TRUE iff the output has size bytes available.
**************************************************************************/
static bool enough_space(struct data_out *dout, size_t size)
{
  if (dout->current + size > dout->dest_size) {
    dout->too_short = TRUE;
    return FALSE;
  } else {
    dout->used = MAX(dout->used, dout->current + size);
    return TRUE;
  }
}

/**************************************************************************
  Returns TRUE iff the input contains size unread bytes.
**************************************************************************/
static bool enough_data(struct data_in *din, size_t size)
{
  return dio_input_remaining(din) >= size;
}

/**************************************************************************
  Initializes the output to the given output buffer and the given
  buffer size.
**************************************************************************/
void dio_output_init(struct data_out *dout, void *destination,
		     size_t dest_size)
{
  dout->dest = destination;
  dout->dest_size = dest_size;
  dout->current = 0;
  dout->used = 0;
  dout->too_short = FALSE;
}

/**************************************************************************
  Return the maximum number of bytes used.
**************************************************************************/
size_t dio_output_used(struct data_out *dout)
{
  return dout->used;
}

/**************************************************************************
  Rewinds the stream so that the put-functions start from the
  beginning.
**************************************************************************/
void dio_output_rewind(struct data_out *dout)
{
  dout->current = 0;
}

/**************************************************************************
  Initializes the input to the given input buffer and the given
  number of valid input bytes.
**************************************************************************/
void dio_input_init(struct data_in *din, const void *src, size_t src_size)
{
  din->src = src;
  din->src_size = src_size;
  din->current = 0;
}

/**************************************************************************
  Rewinds the stream so that the get-functions start from the
  beginning.
**************************************************************************/
void dio_input_rewind(struct data_in *din)
{
  din->current = 0;
}

/**************************************************************************
  Return the number of unread bytes.
**************************************************************************/
size_t dio_input_remaining(struct data_in *din)
{
  return din->src_size - din->current;
}

/**************************************************************************
  Return the size of the data_type in bytes.
**************************************************************************/
size_t data_type_size(enum data_type type)
{
  switch (type) {
  case DIOT_UINT8:
  case DIOT_SINT8:
    return 1;
  case DIOT_UINT16:
  case DIOT_SINT16:
    return 2;
  case DIOT_UINT32:
  case DIOT_SINT32:
    return 4;
  case DIOT_LAST:
    break;
  }

  fc_assert_msg(FALSE, "data_type %d not handled.", type);
  return 0;
}

/**************************************************************************
   Skips 'n' bytes.
**************************************************************************/
bool dio_input_skip(struct data_in *din, size_t size)
{
  if (enough_data(din, size)) {
    din->current += size;
    return TRUE;
  } else {
    return FALSE;
  }
}

/**************************************************************************
  Insert 8 bit value with json.
**************************************************************************/
void dio_put_uint8_json(struct data_out *dout, char *key, int value)
{
  json_object_set_new(dout->json, key, json_integer(value));
}

/**************************************************************************
  Insert value using 8 bits. May overflow.
**************************************************************************/
void dio_put_uint8_raw(struct data_out *dout, int value)
{
  uint8_t x = value;
  FC_STATIC_ASSERT(sizeof(x) == 1, uint8_not_1_byte);

  FIELD_RANGE_TEST((int) x != value, ,
                   "Trying to put %d into 8 bits; "
                   "it will result %d at receiving side.",
                   value, (int) x);

  if (enough_space(dout, 1)) {
    memcpy(ADD_TO_POINTER(dout->dest, dout->current), &x, 1);
    dout->current++;
  }
}

/**************************************************************************
  Insert value using 16 bits. May overflow.
**************************************************************************/
void dio_put_uint16_raw(struct data_out *dout, int value)
{
  uint16_t x = htons(value);
  FC_STATIC_ASSERT(sizeof(x) == 2, uint16_not_2_bytes);

  FIELD_RANGE_TEST((int) ntohs(x) != value, ,
                   "Trying to put %d into 16 bits; "
                   "it will result %d at receiving side.",
                   value, (int) ntohs(x));

  if (enough_space(dout, 2)) {
    memcpy(ADD_TO_POINTER(dout->dest, dout->current), &x, 2);
    dout->current += 2;
  }
}

/**************************************************************************
  Insert value using 32 bits. May overflow.
**************************************************************************/
void dio_put_uint16_json(struct data_out *dout, char *key, int value)
{
  json_object_set_new(dout->json, key, json_integer(value));
}

/**************************************************************************
  Insert block directly from memory.
**************************************************************************/
void dio_put_memory_raw(struct data_out *dout, const void *value, size_t size)
{
  if (enough_space(dout, size)) {
    memcpy(ADD_TO_POINTER(dout->dest, dout->current), value, size);
    dout->current += size;
  }
}

/**************************************************************************
  Insert NULL-terminated string. Conversion callback is used if set.
**************************************************************************/
void dio_put_string_raw(struct data_out *dout, const char *value)
{
  if (put_conv_callback) {
    size_t length;
    char *buffer;

    if ((buffer = (*put_conv_callback) (value, &length))) {
      dio_put_memory_raw(dout, buffer, length + 1);
      free(buffer);
    }
  } else {
    dio_put_memory_raw(dout, value, strlen(value) + 1);
  }
}

/**************************************************************************
  Insert unit type numbers from value array as 8 bit values until there is
  value U_LAST or MAX_NUM_UNIT_LIST numbers have been inserted.
**************************************************************************/
void dio_put_unit_list_json(struct data_out *dout, char *key, const int *value)
{
  /* TODO: implement */
}

/**************************************************************************
  Insert building type numbers from value array as 8 bit values until there
  is value B_LAST or MAX_NUM_BUILDING_LIST numbers have been inserted.
**************************************************************************/
void dio_put_building_list_json(struct data_out *dout, char *key,
                                const int *value)
{
  /* TODO: implement */
}

/**************************************************************************
...
**************************************************************************/
void dio_put_worklist_json(struct data_out *dout, char *key,
                           const struct worklist *pwl)
{
  /* TODO: implement */
}

/**************************************************************************
...
**************************************************************************/
void dio_put_array_uint8_json(struct data_out *dout, char *key,
                              int *values, int size)
{
  int i;
  json_t *array = json_array();
  for (i = 0; i < size; i++) {
    json_array_append_new(array, json_integer(values[i]));
  }
  
  json_object_set_new(dout->json, key, array);
}

/**************************************************************************
...
**************************************************************************/
void dio_put_array_uint32_json(struct data_out *dout, char *key,
                               int *values, int size)
{
  int i;
  json_t *array = json_array();

  for (i = 0; i < size; i++) {
    json_array_append_new(array, json_integer(values[i]));
  }
  
  json_object_set_new(dout->json, key, array);
}

/**************************************************************************
...
**************************************************************************/
void dio_put_array_sint8_json(struct data_out *dout, char *key,
                              int *values, int size)
{
  int i;
  json_t *array = json_array();
  for (i = 0; i < size; i++) {
    json_array_append_new(array, json_integer(values[i]));
  }
  
  json_object_set_new(dout->json, key, array);
}

/**************************************************************************
...
**************************************************************************/
void dio_put_array_sint16_json(struct data_out *dout, char *key,
                               int *values, int size)
{
  int i;
  json_t *array = json_array();
  for (i = 0; i < size; i++) {
    json_array_append_new(array, json_integer(values[i]));
  }
  
  json_object_set_new(dout->json, key, array);
}

/**************************************************************************
...
**************************************************************************/
void dio_put_array_sint32_json(struct data_out *dout, char *key,
                               int *values, int size)
{
  int i;
  json_t *array = json_array();
  for (i = 0; i < size; i++) {
    json_array_append_new(array, json_integer(values[i]));
  }
  
  json_object_set_new(dout->json, key, array);
}

/**************************************************************************
...
**************************************************************************/
void dio_put_array_bool8_json(struct data_out *dout, char *key,
                              bool *values, int size)
{
  int i;
  json_t *array = json_array();
  for (i = 0; i < size; i++) {
    json_array_append_new(array, values[i] ? json_true() : json_false());
  }
  
  json_object_set_new(dout->json, key, array);
}

/**************************************************************************
 Receive uint8 value to dest.
**************************************************************************/
bool dio_get_uint8_raw(struct data_in *din, int *dest)
{
  uint8_t x;

  FC_STATIC_ASSERT(sizeof(x) == 1, uint8_not_byte);

  if (!enough_data(din, 1)) {
    log_packet("Packet too short to read 1 byte");

    return FALSE;
  }

  memcpy(&x, ADD_TO_POINTER(din->src, din->current), 1);
  *dest = x;
  din->current++;
  return TRUE;
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
 Receive uint16 value to dest.
**************************************************************************/
bool dio_get_uint16_raw(struct data_in *din, int *dest)
{
  uint16_t x;

  FC_STATIC_ASSERT(sizeof(x) == 2, uint16_not_2_bytes);

  if (!enough_data(din, 2)) {
    log_packet("Packet too short to read 2 bytes");

    return FALSE;
  }

  memcpy(&x, ADD_TO_POINTER(din->src, din->current), 2);
  *dest = ntohs(x);
  din->current += 2;
  return TRUE;
}

/**************************************************************************
  Take string. Conversion callback is used.
**************************************************************************/
bool dio_get_string_raw(struct data_in *din, char *dest, size_t max_dest_size)
{
  char *c;
  size_t offset, remaining;

  fc_assert(max_dest_size > 0);

  if (!enough_data(din, 1)) {
    log_packet("Got a bad string");
    return FALSE;
  }

  remaining = dio_input_remaining(din);
  c = ADD_TO_POINTER(din->src, din->current);

  /* avoid using strlen (or strcpy) on an (unsigned char*)  --dwp */
  for (offset = 0; offset < remaining && c[offset] != '\0'; offset++) {
    /* nothing */
  }

  if (offset >= remaining) {
    log_packet("Got a too short string");
    return FALSE;
  }

  if (!(*get_conv_callback) (dest, max_dest_size, c, offset)) {
    log_packet("Got a bad encoded string");
    return FALSE;
  }

  din->current += offset + 1;
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
  ..
**************************************************************************/
bool dio_get_requirement_json(json_t *json_packet, char *key,
                              struct requirement *preq)
{
  /* TODO: implement */
  return TRUE;
}

/**************************************************************************
...
**************************************************************************/
void dio_put_uint32_json(struct data_out *dout, char *key, int value)
{
  json_object_set_new(dout->json, key, json_integer(value));
}

/**************************************************************************
...
**************************************************************************/
void dio_put_bool8_json(struct data_out *dout, char *key, bool value)
{
  json_object_set_new(dout->json, key, value ? json_true() : json_false());
}

/**************************************************************************
...
**************************************************************************/
void dio_put_bool32_json(struct data_out *dout, char *key, bool value)
{
  json_object_set_new(dout->json, key, value ? json_true() : json_false());
}

/**************************************************************************
...
**************************************************************************/
void dio_put_ufloat_json(struct data_out *dout, char *key,
                         float value, int float_factor)
{
  json_object_set_new(dout->json, key, json_real(value));
}

/**************************************************************************
...
**************************************************************************/
void dio_put_sfloat_json(struct data_out *dout, char *key,
                         float value, int float_factor)
{
  json_object_set_new(dout->json, key, json_real(value));
}

/**************************************************************************
...
**************************************************************************/
void dio_put_uint8_vec8_json(struct data_out *dout, char *key,
                             int *values, int stop_value)
{
  /* TODO: implement. */
}

/**************************************************************************
...
**************************************************************************/
void dio_put_uint16_vec8_json(struct data_out *dout, char *key, int *values,
                              int stop_value)
{
  /* TODO: implement. */
}

/**************************************************************************
...
**************************************************************************/
void dio_put_memory_json(struct data_out *dout, char *key, const void *value,
                         size_t size)
{
  /* TODO: implement */
}

/**************************************************************************
...
**************************************************************************/
void dio_put_string_json(struct data_out *dout, char *key, const char *value)
{
  json_object_set_new(dout->json, key, json_string(value));
}

/**************************************************************************
...
**************************************************************************/
void dio_put_string_array_json(struct data_out *dout, char *key, 
                               const char *value, int size)
{
  int i;

  json_t *array = json_array();
  for (i = 0; i < size; i++) {
    if (value != NULL) {
      json_array_append_new(array, json_string(value + i));
    }
  }
  
  json_object_set_new(dout->json, key, array);
}

/**************************************************************************
...
**************************************************************************/
void dio_put_tech_list_json(struct data_out *dout, char *key, const int *value)
{
  /* TODO: implement */
}

/**************************************************************************
  ..
**************************************************************************/
void dio_put_requirement_json(struct data_out *dout, char *key,
                              const struct requirement *preq, int size)
{
  /* TODO: implement */
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
...
**************************************************************************/
bool dio_get_memory_json(json_t *json_packet, char *key, void *dest,
                         size_t dest_size)
{
  /* TODO: implement */ 
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
