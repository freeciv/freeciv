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

#include <curl/curl.h>

#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef FREECIV_HAVE_SYS_TYPES_H
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
  Returns a CURL easy handle for name encoding and decoding
**************************************************************************/
static CURL *get_curl(void)
{
  static CURL *curl_easy_handle = NULL;

  if (curl_easy_handle == NULL) {
    curl_easy_handle = curl_easy_init();
  } else {
    /* Reuse the existing CURL easy handle */
    curl_easy_reset(curl_easy_handle);
  }

  return curl_easy_handle;
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

static void plocation_write_data(json_t *item,
                                 const struct plocation *location,
                                 json_t *data);

/**************************************************************************
  Helper for plocation_write_data(). Use it in stead of this.
**************************************************************************/
static void plocation_write_field(json_t *item,
                                  const struct plocation *location,
                                  json_t *data)
{
  if (location->sub_location == NULL) {
    json_object_set_new(item, location->name, data);
  } else {
    plocation_write_data(json_object_get(item, location->name),
                         location->sub_location, data);
  }
}

/**************************************************************************
  Helper for plocation_write_data(). Use it in stead of this.
**************************************************************************/
static void plocation_write_elem(json_t *item,
                                 const struct plocation *location,
                                 json_t *data)
{
  if (location->sub_location == NULL) {
    json_array_set_new(item, location->number, data);
  } else {
    plocation_write_data(json_array_get(item, location->number),
                         location->sub_location, data);
  }
}

/**************************************************************************
  Write the specified JSON data to the given location in the provided
  JSON item.
**************************************************************************/
static void plocation_write_data(json_t *item,
                                 const struct plocation *location,
                                 json_t *data)
{
  switch (location->kind) {
  case PADR_FIELD:
    plocation_write_field(item, location, data);
    return;
  case PADR_ELEMENT:
    plocation_write_elem(item, location, data);
    return;
  default:
    log_error("Unknown packet part location kind.");
    return;
  }
}

static json_t *plocation_read_data(json_t *item,
                                   const struct plocation *location);

/**************************************************************************
  Helper for plocation_read_data(). Use it in stead of this.
**************************************************************************/
static json_t *plocation_read_field(json_t *item,
                                    const struct plocation *location)
{
  if (location->sub_location == NULL) {
    return json_object_get(item, location->name);
  } else {
    return plocation_read_data(json_object_get(item, location->name),
                               location->sub_location);
  }
}

/**************************************************************************
  Helper for plocation_read_data(). Use it in stead of this.
**************************************************************************/
static json_t *plocation_read_elem(json_t *item,
                                   const struct plocation *location)
{
  if (location->sub_location == NULL) {
    return json_array_get(item, location->number);
  } else {
    return plocation_read_data(json_array_get(item, location->number),
                               location->sub_location);
  }
}

/**************************************************************************
  Read JSON data from the given location in the provided JSON item.
**************************************************************************/
static json_t *plocation_read_data(json_t *item,
                                   const struct plocation *location)
{
  switch (location->kind) {
  case PADR_FIELD:
    return plocation_read_field(item, location);
  case PADR_ELEMENT:
    return plocation_read_elem(item, location);
  default:
    log_error("Unknown packet part location kind.");
    return NULL;
  }
}

/**************************************************************************
  Insert 8 bit value with json.
**************************************************************************/
void dio_put_uint8_json(struct json_data_out *dout,
                        char *key, const struct plocation* location,
                        int value)
{
  plocation_write_data(dout->json, location, json_integer(value));
}

/**************************************************************************
  Insert value using 32 bits. May overflow.
**************************************************************************/
void dio_put_uint16_json(struct json_data_out *dout, char *key,
                         const struct plocation* location, int value)
{
  plocation_write_data(dout->json, location, json_integer(value));
}

/**************************************************************************
  Insert unit type numbers from value array as 8 bit values until there is
  value U_LAST or MAX_NUM_UNIT_LIST numbers have been inserted.
**************************************************************************/
void dio_put_unit_list_json(struct json_data_out *dout, char *key,
                            const struct plocation* location,
                            const int *value)
{
  /* TODO: implement */
}

/**************************************************************************
  Insert building type numbers from value array as 8 bit values until there
  is value B_LAST or MAX_NUM_BUILDING_LIST numbers have been inserted.
**************************************************************************/
void dio_put_building_list_json(struct json_data_out *dout, char *key,
                                const struct plocation* location,
                                const int *value)
{
  /* TODO: implement */
}

/**************************************************************************
...
**************************************************************************/
void dio_put_worklist_json(struct json_data_out *dout, char *key,
                           const struct plocation* location,
                           const struct worklist *pwl)
{
  /* TODO: implement */
}

/**************************************************************************
 Receive uint8 value to dest with json.
**************************************************************************/
bool dio_get_uint8_json(json_t *json_packet, char *key,
                        const struct plocation* location, int *dest)
{
  json_t *pint = plocation_read_data(json_packet, location);

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
bool dio_get_uint16_json(json_t *json_packet, char *key,
                         const struct plocation* location, int *dest)
{
  json_t *pint = plocation_read_data(json_packet, location);

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
bool dio_get_uint32_json(json_t *json_packet, char *key,
                         const struct plocation* location, int *dest)
{
  json_t *pint = plocation_read_data(json_packet, location);

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
bool dio_get_tech_list_json(json_t *json_packet, char *key,
                            const struct plocation* location, int *dest)
{
  /* TODO: implement */
  return TRUE;
}

/**************************************************************************
  Take unit type numbers until UTYF_LAST encountered, or MAX_NUM_UNIT_LIST
  types retrieved.
**************************************************************************/
bool dio_get_unit_list_json(json_t *json_packet, char *key,
                            const struct plocation* location, int *dest)
{
  /* TODO: implement */
  return TRUE;
}

/**************************************************************************
 ..
**************************************************************************/
bool dio_get_building_list_json(json_t *json_packet, char *key,
                                const struct plocation* location, int *dest)
{
  /* TODO: implement */
  return TRUE;
}

/**************************************************************************
 ..
**************************************************************************/
bool dio_get_worklist_json(json_t *json_packet, char *key,
                           const struct plocation* location,
                           struct worklist *pwl)
{
  /* TODO: implement */
  return TRUE;
}

/**************************************************************************
...
**************************************************************************/
bool dio_get_uint8_vec8_json(json_t *json_packet, char *key,
                             const struct plocation* location,
                             int **values, int stop_value)
{
  /* TODO: implement */
  return TRUE;
}

/**************************************************************************
...
**************************************************************************/
bool dio_get_uint16_vec8_json(json_t *json_packet, char *key,
                              const struct plocation* location,
                              int **values,
                              int stop_value)
{
  /* TODO: implement */
  return TRUE;
}

/**************************************************************************
  Read a single requirement.
**************************************************************************/
bool dio_get_requirement_json(json_t *json_packet, char *key,
                              const struct plocation* location,
                              struct requirement *preq)
{
  int kind, range, value;
  bool survives, present, quiet;

  struct plocation req_field;

  /* Find the requirement object. */
  json_t *requirement = plocation_read_data(json_packet, location);
  if (!requirement) {
    log_error("ERROR: Unable to get requirement with key: %s", key);
    return FALSE;
  }

  /* Find the requirement object fields and translate their values. */
  req_field = *plocation_field_new("kind");
  if (!dio_get_uint8_json(requirement, "kind", &req_field, &kind)) {
    log_error("ERROR: Unable to get part of requirement with key: %s",
              key);
    return FALSE;
  }

  req_field.name = "value";
  if (!dio_get_sint32_json(requirement, "value", &req_field, &value)) {
    log_error("ERROR: Unable to get part of requirement with key: %s",
              key);
    return FALSE;
  }

  req_field.name = "range";
  if (!dio_get_uint8_json(requirement, "range", &req_field, &range)) {
    log_error("ERROR: Unable to get part of requirement with key: %s",
              key);
    return FALSE;
  }

  req_field.name = "survives";
  if (!dio_get_bool8_json(requirement, "survives", &req_field, &survives)) {
    log_error("ERROR: Unable to get part of requirement with key: %s",
              key);
    return FALSE;
  }

  req_field.name = "present";
  if (!dio_get_bool8_json(requirement, "present", &req_field, &present)) {
    log_error("ERROR: Unable to get part of requirement with key: %s",
              key);
    return FALSE;
  }

  req_field.name = "quiet";
  if (!dio_get_bool8_json(requirement, "quiet", &req_field, &quiet)) {
    log_error("ERROR: Unable to get part of requirement with key: %s",
              key);
    return FALSE;
  }

  /* Create a requirement with the values sent over the network. */
  *preq = req_from_values(kind, range, survives, present, quiet, value);

  return TRUE;
}

/**************************************************************************
  Create an empthy field array.
**************************************************************************/
void dio_put_farray_json(struct json_data_out *dout, char *key,
                         const struct plocation* location, int size)
{
  int i;
  json_t *farray = json_array();

  /* Jansson's json_array_set_new() refuses to create array elements so
   * they must be created with the array. */
  for (i = 0; i < size; i++) {
    json_array_append_new(farray, json_null());
  }

  plocation_write_data(dout->json, location, farray);
}

/**************************************************************************
...
**************************************************************************/
void dio_put_uint32_json(struct json_data_out *dout, char *key,
                         const struct plocation* location, int value)
{
  plocation_write_data(dout->json, location, json_integer(value));
}

/**************************************************************************
...
**************************************************************************/
void dio_put_bool8_json(struct json_data_out *dout, char *key,
                        const struct plocation* location, bool value)
{
  plocation_write_data(dout->json, location, value ? json_true() : json_false());
}

/**************************************************************************
...
**************************************************************************/
void dio_put_bool32_json(struct json_data_out *dout, char *key,
                         const struct plocation* location, bool value)
{
  plocation_write_data(dout->json, location, value ? json_true() : json_false());
}

/**************************************************************************
...
**************************************************************************/
void dio_put_ufloat_json(struct json_data_out *dout, char *key,
                         const struct plocation* location,
                         float value, int float_factor)
{
  plocation_write_data(dout->json, location, json_real(value));
}

/**************************************************************************
...
**************************************************************************/
void dio_put_sfloat_json(struct json_data_out *dout, char *key,
                         const struct plocation* location,
                         float value, int float_factor)
{
  plocation_write_data(dout->json, location, json_real(value));
}

/**************************************************************************
...
**************************************************************************/
void dio_put_uint8_vec8_json(struct json_data_out *dout, char *key,
                             const struct plocation* location,
                             int *values, int stop_value)
{
  /* TODO: implement. */
}

/**************************************************************************
...
**************************************************************************/
void dio_put_uint16_vec8_json(struct json_data_out *dout, char *key,
                              const struct plocation* location, int *values,
                              int stop_value)
{
  /* TODO: implement. */
}

/**************************************************************************
  Send block of memory as byte array
**************************************************************************/
void dio_put_memory_json(struct json_data_out *dout, char *key,
                         const struct plocation* location,
                         const void *value,
                         size_t size)
{
  int i;
  char fullkey[512];
  struct plocation ploc;

  /* TODO: Should probably be a JSON array. */
  ploc = *plocation_field_new(NULL);

  for (i = 0; i < size; i++) {
    fc_snprintf(fullkey, sizeof(fullkey), "%s_%d", key, i);
    ploc.name = fullkey;

    dio_put_uint8_json(dout, fullkey, &ploc, ((unsigned char *)value)[i]);
  }
}

/**************************************************************************
...
**************************************************************************/
void dio_put_string_json(struct json_data_out *dout, char *key,
                         const struct plocation* location,
                         const char *value)
{
  plocation_write_data(dout->json, location, json_string(value));
}

/**************************************************************************
  Encode and write the specified string to the specified location.
**************************************************************************/
void dio_put_estring_json(struct json_data_out *dout, char *key,
                          const struct plocation* location,
                          const char *value)
{
  char *escaped_value;

  /* Let CURL find the length it self by passing 0 */
  escaped_value = curl_easy_escape(get_curl(), value, 0);

  /* Handle as a regular string from now on. */
  dio_put_string_json(dout, key, location, escaped_value);

  /* CURL's memory management wants to free this it self. */
  curl_free(escaped_value);
}

/**************************************************************************
...
**************************************************************************/
void dio_put_tech_list_json(struct json_data_out *dout, char *key,
                            const struct plocation* location,
                            const int *value)
{
  /* TODO: implement */
}

/**************************************************************************
  Insert a single requirement.
**************************************************************************/
void dio_put_requirement_json(struct json_data_out *dout, char *key,
                              const struct plocation* location,
                              const struct requirement *preq)
{
  int kind, range, value;
  bool survives, present, quiet;

  /* Create the requirement object. */
  json_t *requirement = json_object();

  /* Read the requirment values. */
  req_get_values(preq, &kind, &range, &survives, &present, &quiet, &value);

  /* Write the requirement values to the fields of the requirment
   * object. */
  json_object_set(requirement, "kind", json_integer(kind));
  json_object_set(requirement, "value", json_integer(value));

  json_object_set(requirement, "range", json_integer(range));

  json_object_set(requirement, "survives", json_boolean(survives));
  json_object_set(requirement, "present", json_boolean(present));
  json_object_set(requirement, "quiet", json_boolean(quiet));

  /* Put the requirement object in the packet. */
  plocation_write_data(dout->json, location, requirement);
}

/**************************************************************************
...
**************************************************************************/
bool dio_get_bool8_json(json_t *json_packet, char *key,
                        const struct plocation* location, bool *dest)
{
  json_t *pbool = plocation_read_data(json_packet, location);

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
bool dio_get_bool32_json(json_t *json_packet, char *key,
                         const struct plocation* location, bool *dest)
{
  json_t *pbool = plocation_read_data(json_packet, location);

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
bool dio_get_ufloat_json(json_t *json_packet, char *key,
                         const struct plocation* location,
                         float *dest, int float_factor)
{
  json_t *preal = plocation_read_data(json_packet, location);

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
bool dio_get_sfloat_json(json_t *json_packet, char *key,
                         const struct plocation* location,
                         float *dest, int float_factor)
{
  json_t *preal = plocation_read_data(json_packet, location);

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
bool dio_get_sint8_json(json_t *json_packet, char *key,
                        const struct plocation* location, int *dest)
{
  json_t *pint = plocation_read_data(json_packet, location);

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
bool dio_get_sint16_json(json_t *json_packet, char *key,
                         const struct plocation* location, int *dest)
{
  json_t *pint = plocation_read_data(json_packet, location);

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
bool dio_get_memory_json(json_t *json_packet, char *key,
                         const struct plocation* location,
                         void *dest, size_t dest_size)
{
   int i;
  char fullkey[512];
  struct plocation ploc;

  /* TODO: Should probably be a JSON array. */
  ploc = *plocation_field_new(NULL);

  for (i = 0; i < dest_size; i++) {
    int val;

    fc_snprintf(fullkey, sizeof(fullkey), "%s_%d", key, i);
    ploc.name = fullkey;

    if (!dio_get_uint8_json(json_packet, fullkey, &ploc, &val)) {
      return FALSE;
    }
    ((unsigned char *)dest)[i] = val;
  }

  return TRUE;
}

/**************************************************************************
...
**************************************************************************/
bool dio_get_string_json(json_t *json_packet, char *key,
                         const struct plocation* location,
                         char *dest, size_t max_dest_size)
{
  json_t *pstring = plocation_read_data(json_packet, location);

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

/**************************************************************************
  Read and decode the string in the specified location.

  max_dest_size applies to both the encoded and to the decoded string.
**************************************************************************/
bool dio_get_estring_json(json_t *json_packet, char *key,
                          const struct plocation* location,
                          char *dest, size_t max_dest_size)
{
  char *escaped_value;
  char *unescaped_value;

  /* The encoded string has the same size limit as the decoded string. */
  escaped_value = fc_malloc(max_dest_size);

  if (!dio_get_string_json(json_packet, key, location,
                           escaped_value, max_dest_size)) {
    /* dio_get_string_json() has logged this already. */
    return FALSE;
  }

  /* Let CURL find the length it self by passing 0 */
  unescaped_value = curl_easy_unescape(get_curl(), escaped_value, 0, NULL);

  /* Done with the escaped value. */
  FC_FREE(escaped_value);

  /* Copy the unescaped value so CURL can free its own copy. */
  memcpy(dest, unescaped_value, max_dest_size);

  /* CURL's memory management wants to free this it self. */
  curl_free(unescaped_value);

  return TRUE;
}

#endif /* FREECIV_JSON_CONNECTION */
