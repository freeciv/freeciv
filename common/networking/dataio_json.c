/***********************************************************************
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
#include <fc_config.h>
#endif

#ifdef FREECIV_JSON_CONNECTION

#include "fc_prehdrs.h"

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

/* common/aicore */
#include "cm.h"

#include "dataio.h"

static bool dio_get_bool8_json_internal(json_t *json_packet,
                                        const struct plocation *location,
                                        bool *dest);

/**********************************************************************//**
  Returns a CURL easy handle for name encoding and decoding
**************************************************************************/
static CURL *get_curl(void)
{
  static CURL *curl_easy_handle = nullptr;

  if (curl_easy_handle == nullptr) {
    curl_easy_handle = curl_easy_init();
  } else {
    /* Reuse the existing CURL easy handle */
    curl_easy_reset(curl_easy_handle);
  }

  return curl_easy_handle;
}

static int plocation_write_data(json_t *item,
                                const struct plocation *location,
                                json_t *data);

/**********************************************************************//**
  Helper for plocation_write_data(). Use it instead of this.
**************************************************************************/
static int plocation_write_field(json_t *item,
                                 const struct plocation *location,
                                 json_t *data)
{
  int e = -1;

  if (location->sub_location == nullptr) {
    e = json_object_set_new(item, location->name, data);
  } else {
    e = plocation_write_data(json_object_get(item, location->name),
                             location->sub_location, data);
  }

  return e;
}

/**********************************************************************//**
  Helper for plocation_write_data(). Use it instead of this.
**************************************************************************/
static int plocation_write_elem(json_t *item,
                                const struct plocation *location,
                                json_t *data)
{
  int e = -1;

  if (location->sub_location == nullptr) {
    if (location->number == -1) {
      e = json_array_append_new(item, data);
    } else {
      e = json_array_set_new(item, location->number, data);
    }
  } else {
    size_t n = (location->number == -1)
      ? (json_array_size(item) - 1)
      : location->number;

    e = plocation_write_data(json_array_get(item, n),
                             location->sub_location, data);
  }

  return e;
}

/**********************************************************************//**
  Write the specified JSON data to the given location in the provided
  JSON item.
**************************************************************************/
static int plocation_write_data(json_t *item,
                                const struct plocation *location,
                                json_t *data)
{
  int e = -1;

  switch (location->kind) {
  case PADR_FIELD:
    e = plocation_write_field(item, location, data);
    break;
  case PADR_ELEMENT:
    e = plocation_write_elem(item, location, data);
    break;
  default:
    log_error("Unknown packet part location kind.");
    break;
  }

  return e;
}

static json_t *plocation_read_data(json_t *item,
                                   const struct plocation *location);

/**********************************************************************//**
  Helper for plocation_read_data(). Use it instead of this.
**************************************************************************/
static json_t *plocation_read_field(json_t *item,
                                    const struct plocation *location)
{
  if (location->sub_location == nullptr) {
    return json_object_get(item, location->name);
  } else {
    return plocation_read_data(json_object_get(item, location->name),
                               location->sub_location);
  }
}

/**********************************************************************//**
  Helper for plocation_read_data(). Use it instead of this.
**************************************************************************/
static json_t *plocation_read_elem(json_t *item,
                                   const struct plocation *location)
{
  if (location->sub_location == nullptr) {
    return json_array_get(item, location->number);
  } else {
    return plocation_read_data(json_array_get(item, location->number),
                               location->sub_location);
  }
}

/**********************************************************************//**
  Read JSON data from the given location in the provided JSON item.
**************************************************************************/
static json_t *plocation_read_data(json_t *item,
                                   const struct plocation *location)
{
  if (json_is_null(item)) { // PTZ200719 sanity checks stops the massacre
    return item;
  }

  switch (location->kind) {
  case PADR_FIELD:
    return plocation_read_field(item, location);
  case PADR_ELEMENT:
    return plocation_read_elem(item, location);
  default:
    log_error("Unknown packet part location kind.");

    return nullptr;
  }
}

/**********************************************************************//**
  Insert 8 bit value with json.
**************************************************************************/
int dio_put_uint8_json(struct json_data_out *dout,
                       const struct plocation *location,
                       int value)
{
  int e;

  if (dout->json) {
    e = plocation_write_data(dout->json, location, json_integer(value));
  } else {
    e = dio_put_uint8_raw(&dout->raw, value);
  }

  return e;
}

/**********************************************************************//**
  Insert 8 bit value with json.
**************************************************************************/
int dio_put_sint8_json(struct json_data_out *dout,
                       const struct plocation *location,
                       int value)
{
  int e;

  if (dout->json) {
    e = plocation_write_data(dout->json, location, json_integer(value));
  } else {
    e = dio_put_sint8_raw(&dout->raw, value);
  }

  return e;
}

/**********************************************************************//**
  Insert value using 32 bits. May overflow.
**************************************************************************/
int dio_put_uint16_json(struct json_data_out *dout,
                        const struct plocation *location, int value)
{
  int e;

  if (dout->json) {
    e = plocation_write_data(dout->json, location, json_integer(value));
  } else {
    e = dio_put_uint16_raw(&dout->raw, value);
  }

  return e;
}

/**********************************************************************//**
  Insert value using 32 bits. May overflow.
**************************************************************************/
int dio_put_sint16_json(struct json_data_out *dout,
                        const struct plocation *location, int value)
{
  int e;

  if (dout->json) {
    e = plocation_write_data(dout->json, location, json_integer(value));
  } else {
    e = dio_put_sint16_raw(&dout->raw, value);
  }

  return e;
}

/**********************************************************************//**
  Insert the given cm_parameter struct
**************************************************************************/
int dio_put_cm_parameter_json(struct json_data_out *dout,
                              struct plocation *location,
                              const struct cm_parameter *param)
{
  int e = 0;

  if (dout->json) {
    json_t *obj = json_object();
    json_t *min_surplus = json_array();
    json_t *factor = json_array();
    int i;

    for (i = 0; i < O_LAST; i++) {
      e |= json_array_append_new(min_surplus,
                                 json_integer(param->minimal_surplus[i]));
      e |= json_array_append_new(factor,
                                 json_integer(param->factor[i]));
    }

    e |= json_object_set_new(obj, "minimal_surplus", min_surplus);
    e |= json_object_set_new(obj, "factor", factor);
    e |= json_object_set_new(obj, "max_growth", json_boolean(param->max_growth));
    e |= json_object_set_new(obj, "require_happy",
                             json_boolean(param->require_happy));
    e |= json_object_set_new(obj, "allow_disorder",
                             json_boolean(param->allow_disorder));
    e |= json_object_set_new(obj, "allow_specialists",
                             json_boolean(param->allow_specialists));
    e |= json_object_set_new(obj, "happy_factor",
                             json_integer(param->happy_factor));
    e |= plocation_write_data(dout->json, location, obj);
  } else {
    e = dio_put_cm_parameter_raw(&dout->raw, param);
  }

  return e;
}

/**********************************************************************//**
  Insert the given unit_order struct
**************************************************************************/
int dio_put_unit_order_json(struct json_data_out *dout,
                            struct plocation *location,
                            const struct unit_order *order)
{
  int e = 0;

  if (dout->json) {
    json_t *obj = json_object();

    e |= json_object_set_new(obj, "order", json_integer(order->order));
    e |= json_object_set_new(obj, "activity", json_integer(order->activity));
    e |= json_object_set_new(obj, "target", json_integer(order->target));
    e |= json_object_set_new(obj, "sub_target", json_integer(order->sub_target));
    e |= json_object_set_new(obj, "action", json_integer(order->action));
    if (order->dir == -1) {
      /* Avoid integer underflow */
      e |= json_object_set_new(obj, "dir", json_integer(-1));
    } else {
      e |= json_object_set_new(obj, "dir", json_integer(order->dir));
    }
    e |= plocation_write_data(dout->json, location, obj);
  } else {
    e = dio_put_unit_order_raw(&dout->raw, order);
  }

  return e;
}

/**********************************************************************//**
  Insert worklist information.
**************************************************************************/
int dio_put_worklist_json(struct json_data_out *dout,
                          struct plocation *location,
                          const struct worklist *pwl)
{
  int e = 0;

  if (dout->json) {
    int i;
    const int size = worklist_length(pwl);

    /* Must create the array before insertion. */
    e |= dio_put_farray_json(dout, location, size);

    location->sub_location = plocation_elem_new(0);

    for (i = 0; i < size; i++) {
      const struct universal *pcp = &(pwl->entries[i]);
      json_t *universal = json_object();

      location->sub_location->number = i;

      e |= json_object_set_new(universal, "kind", json_integer(pcp->kind));
      e |= json_object_set_new(universal, "value",
                               json_integer(universal_number(pcp)));

      e |= plocation_write_data(dout->json, location, universal);
    }

    FC_FREE(location->sub_location);
  } else {
    e = dio_put_worklist_raw(&dout->raw, pwl);
  }

  return e;
}

/**********************************************************************//**
  Receive uint8 value to dest with json.
**************************************************************************/
static bool dio_get_uint8_json_internal(json_t *json_packet,
                                        const struct plocation *location,
                                        int *dest)
{
  json_t *pint = plocation_read_data(json_packet, location);

  if (!pint) {
    log_error("ERROR: Unable to get uint8 from location: %s", plocation_name(location));
    return FALSE;
  }
  *dest = json_integer_value(pint);

  return TRUE;
}

/**********************************************************************//**
  Receive uint8 value to dest with json.
**************************************************************************/
bool dio_get_uint8_json(struct connection *pc, struct data_in *din,
                        const struct plocation *location, int *dest)
{
  if (pc->json_mode) {
    return dio_get_uint8_json_internal(pc->json_packet, location, dest);
  } else {
    return dio_get_uint8_raw(din, dest);
  }
}

/**********************************************************************//**
  Receive uint16 value to dest with json.
**************************************************************************/
bool dio_get_uint16_json(struct connection *pc, struct data_in *din,
                         const struct plocation *location, int *dest)
{
  if (pc->json_mode) {
    json_t *pint = plocation_read_data(pc->json_packet, location);

    if (!pint) {
      log_error("ERROR: Unable to get uint16 from location: %s", plocation_name(location));
      return FALSE;
    }
    *dest = json_integer_value(pint);
  } else {
    return dio_get_uint16_raw(din, dest);
  }

  return TRUE;
}

/**********************************************************************//**
  Receive uint32 value to dest with json.
**************************************************************************/
static bool dio_get_uint32_json_internal(json_t *json_packet,
                                         const struct plocation *location,
                                         int *dest)
{
  json_t *pint = plocation_read_data(json_packet, location);

  if (!pint) {
    log_error("ERROR: Unable to get uint32 from location: %s", plocation_name(location));
    return FALSE;
  }
  *dest = json_integer_value(pint);

  return TRUE;
}

/**********************************************************************//**
  Receive uint32 value to dest with json.
**************************************************************************/
bool dio_get_uint32_json(struct connection *pc, struct data_in *din,
                         const struct plocation *location, int *dest)
{
  if (pc->json_mode) {
    return dio_get_uint32_json_internal(pc->json_packet, location, dest);
  } else {
    return dio_get_uint32_raw(din, dest);
  }
}

/**********************************************************************//**
  Receive sint32 value to dest with json.
**************************************************************************/
bool dio_get_sint32_json(struct connection *pc, struct data_in *din,
                         const struct plocation *location, int *dest)
{
  if (pc->json_mode) {
    return dio_get_uint32_json_internal(pc->json_packet, location, dest);
  } else {
    return dio_get_sint32_raw(din, dest);
  }
}

/**********************************************************************//**
  Retrieve a cm_parameter
**************************************************************************/
bool dio_get_cm_parameter_json(struct connection *pc, struct data_in *din,
                               struct plocation *location,
                               struct cm_parameter *param)
{
  if (pc->json_mode) {
    int i;

    cm_init_parameter(param);

    location->sub_location = plocation_field_new("max_growth");
    if (!dio_get_bool8_json(pc, din, location, &param->max_growth)) {
      log_packet("Corrupt cm_parameter.max_growth");
      FC_FREE(location->sub_location);
      return FALSE;
    }

    location->sub_location->name = "require_happy";
    if (!dio_get_bool8_json(pc, din, location, &param->require_happy)) {
      log_packet("Corrupt cm_parameter.require_happy");
      FC_FREE(location->sub_location);
      return FALSE;
    }

    location->sub_location->name = "allow_disorder";
    if (!dio_get_bool8_json(pc, din, location, &param->allow_disorder)) {
      log_packet("Corrupt cm_parameter.allow_disorder");
      FC_FREE(location->sub_location);
      return FALSE;
    }

    location->sub_location->name = "allow_specialists";
    if (!dio_get_bool8_json(pc, din, location, &param->allow_specialists)) {
      log_packet("Corrupt cm_parameter.allow_specialists");
      FC_FREE(location->sub_location);
      FC_FREE(location->sub_location);
      return FALSE;
    }

    location->sub_location->name = "happy_factor";
    if (!dio_get_uint16_json(pc, din, location, &param->happy_factor)) {
      log_packet("Corrupt cm_parameter.happy_factor");
      FC_FREE(location->sub_location);
      return FALSE;
    }

    location->sub_location->name = "factor";
    location->sub_location->sub_location = plocation_elem_new(0);
    for (i = 0; i < O_LAST; i++) {
      location->sub_location->sub_location->number = i;
      if (!dio_get_uint16_json(pc, din, location, &param->factor[i])) {
        log_packet("Corrupt cm_parameter.factor");
        FC_FREE(location->sub_location->sub_location);
        FC_FREE(location->sub_location);
        return FALSE;
      }
    }

    location->sub_location->name = "minimal_surplus";
    for (i = 0; i < O_LAST; i++) {
      location->sub_location->sub_location->number = i;
      if (!dio_get_sint16_json(pc, din, location,
                               &param->minimal_surplus[i])) {
        log_packet("Corrupt cm_parameter.minimal_surplus");
        FC_FREE(location->sub_location->sub_location);
        FC_FREE(location->sub_location);
        return FALSE;
      }
    }

    FC_FREE(location->sub_location->sub_location);
    FC_FREE(location->sub_location);
  } else {
    return dio_get_cm_parameter_raw(din, param);
  }

  return TRUE;
}

/**********************************************************************//**
  Retrieve an unit_order
**************************************************************************/
bool dio_get_unit_order_json(struct connection *pc, struct data_in *din,
                             struct plocation *location,
                             struct unit_order *order)
{
  if (pc->json_mode) {
    struct plocation *loc;
    int iorder, iactivity, idir; /* These fields are enums */

    /* Orders may be located in a nested field (as items in an array) */
    loc = location;
    while (loc->sub_location) {
      loc = loc->sub_location;
    }

    loc->sub_location = plocation_field_new("order");
    if (!dio_get_uint8_json(pc, din, location, &iorder)) {
      log_packet("Corrupt order.order");
      FC_FREE(loc->sub_location);
      return FALSE;
    }

    loc->sub_location->name = "activity";
    if (!dio_get_uint8_json(pc, din, location, &iactivity)) {
      log_packet("Corrupt order.activity");
      FC_FREE(loc->sub_location);
      return FALSE;
    }

    loc->sub_location->name = "target";
    if (!dio_get_sint32_json(pc, din, location, &order->target)) {
      log_packet("Corrupt order.target");
      FC_FREE(loc->sub_location);
      return FALSE;
    }

    loc->sub_location->name = "sub_target";
    if (!dio_get_sint16_json(pc, din, location, &order->sub_target)) {
      log_packet("Corrupt order.sub_target");
      FC_FREE(loc->sub_location);
      return FALSE;
    }

    loc->sub_location->name = "action";
    if (!dio_get_uint8_json(pc, din, location, &order->action)) {
      log_packet("Corrupt order.action");
      FC_FREE(loc->sub_location);
      return FALSE;
    }

    loc->sub_location->name = "dir";
    if (!dio_get_uint8_json(pc, din, location, &idir)) {
      log_packet("Corrupt order.dir");
      FC_FREE(loc->sub_location);
      return FALSE;
    }

    /*
     * FIXME: The values should be checked!
     */
    order->order = iorder;
    order->activity = iactivity;
    order->dir = idir;

    FC_FREE(loc->sub_location);
  } else {
    return dio_get_unit_order_raw(din, order);
  }

  return TRUE;
}

/**********************************************************************//**
  Receive worklist information.
**************************************************************************/
bool dio_get_worklist_json(struct connection *pc, struct data_in *din,
                           struct plocation *location,
                           struct worklist *pwl)
{
  if (pc->json_mode) {
    int i, length;

    const json_t *wlist = plocation_read_data(pc->json_packet, location);

    worklist_init(pwl);

    if (!json_is_array(wlist)) {
      log_packet("Not a worklist");
      return FALSE;
    }

    /* Safe. Checked that it was an array above. */
    length = json_array_size(wlist);

    /* A worklist is an array... */
    location->sub_location = plocation_elem_new(0);

    /* ... of universal objects. */
    location->sub_location->sub_location = plocation_field_new("kind");

    for (i = 0; i < length; i++) {
      int value;
      int kind;
      struct universal univ;

      location->sub_location->number = i;

      location->sub_location->sub_location->name = "kind";
      if (!dio_get_uint8_json_internal(pc->json_packet, location, &kind)) {
        log_packet("Corrupt worklist element kind");
        FC_FREE(location->sub_location->sub_location);
        FC_FREE(location->sub_location);
        return FALSE;
      }

      location->sub_location->sub_location->name = "value";
      if (!dio_get_uint8_json_internal(pc->json_packet, location, &value)) {
        log_packet("Corrupt worklist element value");
        FC_FREE(location->sub_location->sub_location);
        FC_FREE(location->sub_location);
        return FALSE;
      }

      /*
       * FIXME: the value returned by universal_by_number() should be checked!
       */
      univ = universal_by_number(kind, value);
      worklist_append(pwl, &univ);
    }

    FC_FREE(location->sub_location->sub_location);
    FC_FREE(location->sub_location);
  } else {
    return dio_get_worklist_raw(din, pwl);
  }

  return TRUE;
}

/**********************************************************************//**
  Receive array length value to dest with json. In json mode, this will
  simply read the length of the array at that location.
**************************************************************************/
bool dio_get_arraylen_json(struct connection *pc, struct data_in *din,
                           const struct plocation *location, int *dest)
{
  if (pc->json_mode) {
    const json_t *arr = plocation_read_data(pc->json_packet, location);

    if (!json_is_array(arr)) {
      log_packet("Not an array");
      return FALSE;
    }

    *dest = json_array_size(arr);
  } else {
    return dio_get_arraylen_raw(din, dest);
  }

  return TRUE;
}

/**********************************************************************//**
  Receive vector of 8 bit values, terminated by stop_value.
**************************************************************************/
bool dio_get_uint8_vec8_json(struct connection *pc, struct data_in *din,
                             const struct plocation *location,
                             int **values, int stop_value)
{
  if (pc->json_mode) {
    /* TODO: implement */
    log_warn("Received unimplemeted data type uint8_vec8.");
  } else {
    return dio_get_uint8_vec8_raw(din, values, stop_value);
  }

  return TRUE;
}

/**********************************************************************//**
  Receive vector of uint16 values.
**************************************************************************/
bool dio_get_uint16_vec8_json(struct connection *pc, struct data_in *din,
                              const struct plocation *location,
                              int **values,
                              int stop_value)
{
  if (pc->json_mode) {
    /* TODO: implement */
    log_warn("Received unimplemeted data type uint16_vec8.");
  } else {
    return dio_get_uint16_vec8_raw(din, values, stop_value);
  }

  return TRUE;
}

/**********************************************************************//**
  Read a single requirement.
**************************************************************************/
bool dio_get_requirement_json(struct connection *pc, struct data_in *din,
                              const struct plocation *location,
                              struct requirement *preq)
{
  if (pc->json_mode) {
    int kind, range, value;
    bool survives, present, quiet;

    struct plocation *req_field;

    /* Find the requirement object. */
    json_t *requirement = plocation_read_data(pc->json_packet, location);

    if (!requirement) {
      log_error("ERROR: Unable to get requirement from location: %s", plocation_name(location));
      return FALSE;
    }

    /* Find the requirement object fields and translate their values. */
    req_field = plocation_field_new("kind");
    if (!dio_get_uint8_json_internal(requirement, req_field, &kind)) {
      log_error("ERROR: Unable to get part of requirement from location: %s",
                plocation_name(location));
      return FALSE;
    }

    req_field->name = "value";
    if (!dio_get_uint32_json_internal(requirement, req_field, &value)) {
      log_error("ERROR: Unable to get part of requirement from location: %s",
                plocation_name(location));
      return FALSE;
    }

    req_field->name = "range";
    if (!dio_get_uint8_json_internal(requirement, req_field, &range)) {
      log_error("ERROR: Unable to get part of requirement from location: %s",
                plocation_name(location));
      return FALSE;
    }

    req_field->name = "survives";
    if (!dio_get_bool8_json_internal(requirement, req_field, &survives)) {
      log_error("ERROR: Unable to get part of requirement from location: %s",
                plocation_name(location));
      return FALSE;
    }

    req_field->name = "present";
    if (!dio_get_bool8_json_internal(requirement, req_field, &present)) {
      log_error("ERROR: Unable to get part of requirement from location: %s",
                plocation_name(location));
      return FALSE;
    }

    req_field->name = "quiet";
    if (!dio_get_bool8_json_internal(requirement, req_field, &quiet)) {
      log_error("ERROR: Unable to get part of requirement from location: %s",
                plocation_name(location));
      return FALSE;
    }

    FC_FREE(req_field);

    /* Create a requirement with the values sent over the network. */
    *preq = req_from_values(kind, range, survives, present, quiet, value);
  } else {
    return dio_get_requirement_raw(din, preq);
  }

  return TRUE;
}

/**********************************************************************//**
  De-serialize an action probability.
**************************************************************************/
bool dio_get_action_probability_json(struct connection *pc, struct data_in *din,
                                     const struct plocation *location,
                                     struct act_prob *prob)
{
  if (pc->json_mode) {
    struct plocation *ap_field;

    /* Find the action probability object. */
    json_t *action_probability = plocation_read_data(pc->json_packet, location);

    if (!action_probability) {
      log_error("ERROR: Unable to get action probability from location: %s",
                plocation_name(location));
      return FALSE;
    }

    /* Find the action probability object fields and translate their
     * values. */
    ap_field = plocation_field_new("min");
    if (!dio_get_uint8_json_internal(action_probability, ap_field, &prob->min)) {
      log_error("ERROR: Unable to get part of action probability "
                "from location: %s",
                plocation_name(location));
      return FALSE;
    }

    ap_field->name = "max";
    if (!dio_get_uint8_json_internal(action_probability, ap_field, &prob->max)) {
      log_error("ERROR: Unable to get part of action probability "
                "from location: %s",
                plocation_name(location));
      return FALSE;
    }

    FC_FREE(ap_field);
  } else {
    return dio_get_action_probability_raw(din, prob);
  }

  return TRUE;
}

/**********************************************************************//**
  Create an empty field array.
**************************************************************************/
int dio_put_farray_json(struct json_data_out *dout,
                        const struct plocation *location, int size)
{
  int e = 0;

  if (dout->json) {
    int i;
    json_t *farray = json_array();

    /* Jansson's json_array_set_new() refuses to create array elements so
     * they must be created with the array. */
    for (i = 0; i < size; i++) {
      e |= json_array_append_new(farray, json_null());
    }

    e |= plocation_write_data(dout->json, location, farray);
  } else {
    /* No caller needs this */
  }

  return e;
}

/**********************************************************************//**
  Create an empty JSON object.
**************************************************************************/
int dio_put_object_json(struct json_data_out *dout,
                        const struct plocation *location)
{
  int e = 0;

  if (dout->json) {
    e |= plocation_write_data(dout->json, location, json_object());
  } else {
    /* No caller needs this */
  }

  return e;
}

/**********************************************************************//**
  Insert uint32 value.
**************************************************************************/
int dio_put_uint32_json(struct json_data_out *dout,
                        const struct plocation *location, int value)
{
  int e;

  if (dout->json) {
    e = plocation_write_data(dout->json, location, json_integer(value));
  } else {
    e = dio_put_uint32_raw(&dout->raw, value);
  }

  return e;
}

/**********************************************************************//**
  Insert sint32 value.
**************************************************************************/
int dio_put_sint32_json(struct json_data_out *dout,
                        const struct plocation *location, int value)
{
  int e;

  if (dout->json) {
    e = plocation_write_data(dout->json, location, json_integer(value));
  } else {
    e = dio_put_sint32_raw(&dout->raw, value);
  }

  return e;
}

/**********************************************************************//**
  Insert bool value.
**************************************************************************/
int dio_put_bool8_json(struct json_data_out *dout,
                       const struct plocation *location, bool value)
{
  int e;

  if (dout->json) {
    e = plocation_write_data(dout->json, location, value ? json_true() : json_false());
  } else {
    e = dio_put_bool8_raw(&dout->raw, value);
  }

  return e;
}

/**********************************************************************//**
  Insert bool value.
**************************************************************************/
int dio_put_bool32_json(struct json_data_out *dout,
                        const struct plocation *location, bool value)
{
  int e;

  if (dout->json) {
    e = plocation_write_data(dout->json, location, value ? json_true() : json_false());
  } else {
    e = dio_put_bool32_raw(&dout->raw, value);
  }

  return e;
}

/**********************************************************************//**
  Insert unsigned floating point value.
**************************************************************************/
int dio_put_ufloat_json(struct json_data_out *dout,
                        const struct plocation *location,
                        float value, int float_factor)
{
  int e;

  if (dout->json) {
    e = plocation_write_data(dout->json, location, json_real(value));
  } else {
    e = dio_put_ufloat_raw(&dout->raw, value, float_factor);
  }

  return e;
}

/**********************************************************************//**
  Insert signed floating point value.
**************************************************************************/
int dio_put_sfloat_json(struct json_data_out *dout,
                        const struct plocation *location,
                        float value, int float_factor)
{
  int e;

  if (dout->json) {
    e = plocation_write_data(dout->json, location, json_real(value));
  } else {
    e = dio_put_sfloat_raw(&dout->raw, value, float_factor);
  }

  return e;
}

/**********************************************************************//**
  Insert array length. In json mode, this will create an array at that
  location.
**************************************************************************/
int dio_put_arraylen_json(struct json_data_out *dout,
                          const struct plocation *location, int size)
{
  int e;

  if (dout->json) {
    e = dio_put_farray_json(dout, location, size);
  } else {
    e = dio_put_arraylen_raw(&dout->raw, size);
  }

  return e;
}

/**********************************************************************//**
  Insert vector of uint8 values, terminated by stop_value.
**************************************************************************/
int dio_put_uint8_vec8_json(struct json_data_out *dout,
                            const struct plocation *location,
                            int *values, int stop_value)
{
  int e;

  if (dout->json) {
    /* TODO: implement. */
    log_error("Tried to send unimplemeted data type uint8_vec8.");
    e = -1;
  } else {
    e = dio_put_uint8_vec8_raw(&dout->raw, values, stop_value);
  }

  return e;
}

/**********************************************************************//**
  Insert vector of uint16 values, terminated by stop_value.
**************************************************************************/
int dio_put_uint16_vec8_json(struct json_data_out *dout,
                             const struct plocation *location, int *values,
                             int stop_value)
{
  int e;

  if (dout->json) {
    /* TODO: implement. */
    log_error("Tried to send unimplemeted data type uint16_vec8.");
    e = -1;
  } else {
    e = dio_put_uint16_vec8_raw(&dout->raw, values, stop_value);
  }

  return e;
}

/**********************************************************************//**
  Send block of memory as byte array.
**************************************************************************/
int dio_put_memory_json(struct json_data_out *dout,
                        struct plocation *location,
                        const void *value,
                        size_t size)
{
  int e;

  if (dout->json) {
    int i;

    e = dio_put_farray_json(dout, location, size);

    location->sub_location = plocation_elem_new(0);

    for (i = 0; i < size; i++) {
      location->sub_location->number = i;

      e |= dio_put_uint8_json(dout, location,
                              ((unsigned char *)value)[i]);
    }

    FC_FREE(location->sub_location);
  } else {
    e = dio_put_memory_raw(&dout->raw, value, size);
  }

  return e;
}

/**********************************************************************//**
  Insert nullptr-terminated string.
**************************************************************************/
int dio_put_string_json(struct json_data_out *dout,
                        const struct plocation *location,
                        const char *value)
{
  int e;

  if (dout->json) {
    e = plocation_write_data(dout->json, location, json_string(value));
  } else {
    e = dio_put_string_raw(&dout->raw, value);
  }

  return e;
}

/**********************************************************************//**
  Encode and write the specified string to the specified location.
**************************************************************************/
int dio_put_estring_json(struct json_data_out *dout,
                         const struct plocation *location,
                         const char *value)
{
  int e;

  if (dout->json) {
    char *escaped_value;

    /* Let CURL find the length itself by passing 0 */
    escaped_value = curl_easy_escape(get_curl(), value, 0);

    /* Handle as a regular string from now on. */
    e = dio_put_string_json(dout, location, escaped_value);

    /* CURL's memory management wants to free this itself. */
    curl_free(escaped_value);
  } else {
    e = dio_put_estring_raw(&dout->raw, value);
  }

  return e;
}

/**********************************************************************//**
  Insert a single requirement.
**************************************************************************/
int dio_put_requirement_json(struct json_data_out *dout,
                             const struct plocation *location,
                             const struct requirement *preq)
{
  int e = 0;

  if (dout->json) {
    int kind, range, value;
    bool survives, present, quiet;

    /* Create the requirement object. */
    json_t *requirement = json_object();

    /* Read the requirement values. */
    req_get_values(preq, &kind, &range, &survives, &present, &quiet, &value);

    /* Write the requirement values to the fields of the requirement
     * object. */
    e |= json_object_set_new(requirement, "kind", json_integer(kind));
    e |= json_object_set_new(requirement, "value", json_integer(value));

    e |= json_object_set_new(requirement, "range", json_integer(range));

    e |= json_object_set_new(requirement, "survives", json_boolean(survives));
    e |= json_object_set_new(requirement, "present", json_boolean(present));
    e |= json_object_set_new(requirement, "quiet", json_boolean(quiet));

    /* Put the requirement object in the packet. */
    e |= plocation_write_data(dout->json, location, requirement);
  } else {
    e = dio_put_requirement_raw(&dout->raw, preq);
  }

  return e;
}

/**********************************************************************//**
  Serialize an action probability.
**************************************************************************/
int dio_put_action_probability_json(struct json_data_out *dout,
                                    const struct plocation *location,
                                    const struct act_prob *prob)
{
  int e = 0;

  if (dout->json) {
    /* Create the action probability object. */
    json_t *action_probability = json_object();

    /* Write the action probability values to the fields of the action
     * probability object. */
    e |= json_object_set_new(action_probability, "min", json_integer(prob->min));
    e |= json_object_set_new(action_probability, "max", json_integer(prob->max));

    /* Put the action probability object in the packet. */
    e |= plocation_write_data(dout->json, location, action_probability);
  } else {
    e = dio_put_action_probability_raw(&dout->raw, prob);
  }

  return e;
}

/**********************************************************************//**
  Receive bool value.
**************************************************************************/
static bool dio_get_bool8_json_internal(json_t *json_packet,
                                        const struct plocation *location,
                                        bool *dest)
{
  json_t *pbool = plocation_read_data(json_packet, location);

  if (!pbool) {
    log_error("ERROR: Unable to get bool8 from location: %s", plocation_name(location));
    return FALSE;
  }
  *dest = json_is_true(pbool);

  return TRUE;
}

/**********************************************************************//**
  Receive bool value.
**************************************************************************/
bool dio_get_bool8_json(struct connection *pc, struct data_in *din,
                        const struct plocation *location, bool *dest)
{
  if (pc->json_mode) {
    return dio_get_bool8_json_internal(pc->json_packet, location, dest);
  } else {
    return dio_get_bool8_raw(din, dest);
  }
}

/**********************************************************************//**
  Receive bool value.
**************************************************************************/
bool dio_get_bool32_json(struct connection *pc, struct data_in *din,
                         const struct plocation *location, bool *dest)
{
  if (pc->json_mode) {
    json_t *pbool = plocation_read_data(pc->json_packet, location);

    if (!pbool) {
      log_error("ERROR: Unable to get bool32 from location: %s", plocation_name(location));
      return FALSE;
    }
    *dest = json_is_true(pbool);
  } else {
    return dio_get_bool32_raw(din, dest);
  }

  return TRUE;
}

/**********************************************************************//**
  Receive unsigned floating point value.
**************************************************************************/
bool dio_get_ufloat_json(struct connection *pc, struct data_in *din,
                         const struct plocation *location,
                         float *dest, int float_factor)
{
  if (pc->json_mode) {
    json_t *preal = plocation_read_data(pc->json_packet, location);

    if (!preal) {
      log_error("ERROR: Unable to get real from location: %s", plocation_name(location));
      return FALSE;
    }
    *dest = json_real_value(preal);
  } else {
    return dio_get_ufloat_raw(din, dest, float_factor);
  }

  return TRUE;
}

/**********************************************************************//**
  Receive signed floating point value.
**************************************************************************/
bool dio_get_sfloat_json(struct connection *pc, struct data_in *din,
                         const struct plocation *location,
                         float *dest, int float_factor)
{
  if (pc->json_mode) {
    json_t *preal = plocation_read_data(pc->json_packet, location);

    if (!preal) {
      log_error("ERROR: Unable to get real from location: %s", plocation_name(location));
      return FALSE;
    }
    *dest = json_real_value(preal);
  } else {
    return dio_get_sfloat_raw(din, dest, float_factor);
  }

  return TRUE;
}

/**********************************************************************//**
  Receive signed 8 bit value.
**************************************************************************/
bool dio_get_sint8_json(struct connection *pc, struct data_in *din,
                        const struct plocation *location, int *dest)
{
  if (pc->json_mode) {
    json_t *pint = plocation_read_data(pc->json_packet, location);

    if (!pint) {
      log_error("ERROR: Unable to get sint8 from location: %s", plocation_name(location));
      return FALSE;
    }
    *dest = json_integer_value(pint);
  } else {
    return dio_get_sint8_raw(din, dest);
  }

  return TRUE;
}

/**********************************************************************//**
  Receive signed 16 bit value.
**************************************************************************/
bool dio_get_sint16_json(struct connection *pc, struct data_in *din,
                         const struct plocation *location, int *dest)
{
  if (pc->json_mode) {
    json_t *pint = plocation_read_data(pc->json_packet, location);

    if (!pint) {
      log_error("ERROR: Unable to get sint16 from location: %s", plocation_name(location));
      return FALSE;
    }
    *dest = json_integer_value(pint);
  } else {
    return dio_get_sint16_raw(din, dest);
  }

  return TRUE;
}

/**********************************************************************//**
  Receive block of memory as byte array.
**************************************************************************/
bool dio_get_memory_json(struct connection *pc, struct data_in *din,
                         struct plocation *location,
                         void *dest, size_t dest_size)
{
  if (pc->json_mode) {
    int i;

    location->sub_location = plocation_elem_new(0);

    for (i = 0; i < dest_size; i++) {
      int val;

      location->sub_location->number = i;

      if (!dio_get_uint8_json_internal(pc->json_packet, location, &val)) {
        free(location->sub_location);
        return FALSE;
      }
      ((unsigned char *)dest)[i] = val;
    }

    FC_FREE(location->sub_location);
  } else {
    return dio_get_memory_raw(din, dest, dest_size);
  }

  return TRUE;
}

/**********************************************************************//**
  Receive at max max_dest_size bytes long nullptr-terminated string.
**************************************************************************/
static bool dio_get_string_json_internal(json_t *json_packet,
                                         const struct plocation *location,
                                         char *dest, size_t max_dest_size)
{
  json_t *pstring = plocation_read_data(json_packet, location);
  const char *result_str;

  if (!pstring) {
    log_error("ERROR: Unable to get string from location: %s", plocation_name(location));
    return FALSE;
  }

  result_str = json_string_value(pstring);

  if (dest
      && !dataio_get_conv_callback(dest, max_dest_size, result_str, strlen(result_str))) {
    log_error("ERROR: Unable to get string from location: %s", plocation_name(location));
    return FALSE;
  }

  return TRUE;
}

/**********************************************************************//**
  Receive at max max_dest_size bytes long nullptr-terminated string.
**************************************************************************/
bool dio_get_string_json(struct connection *pc, struct data_in *din,
                         const struct plocation *location,
                         char *dest, size_t max_dest_size)
{
  if (pc->json_mode) {
    return dio_get_string_json_internal(pc->json_packet, location,
                                        dest, max_dest_size);
  } else {
    return dio_get_string_raw(din, dest, max_dest_size);
  }
}

/**********************************************************************//**
  Read and decode the string in the specified location.

  max_dest_size applies to both the encoded and to the decoded string.
**************************************************************************/
bool dio_get_estring_json(struct connection *pc, struct data_in *din,
                          const struct plocation *location,
                          char *dest, size_t max_dest_size)
{
  if (pc->json_mode) {
    char *escaped_value;
    char *unescaped_value;

    /* The encoded string has the same size limit as the decoded string. */
    escaped_value = fc_malloc(max_dest_size);

    if (!dio_get_string_json_internal(pc->json_packet, location,
                                      escaped_value, max_dest_size)) {
      /* dio_get_string_json() has logged this already. */
      return FALSE;
    }

    /* Let CURL find the length itself by passing 0 */
    unescaped_value = curl_easy_unescape(get_curl(), escaped_value,
                                         0, nullptr);

    /* Done with the escaped value. */
    FC_FREE(escaped_value);

    /* Copy the unescaped value so CURL can free its own copy. */
    memcpy(dest, unescaped_value,
           /* Don't copy the memory following unescaped_value. */
           MIN(max_dest_size, strlen(unescaped_value) + 1));

    /* CURL's memory management wants to free this itself. */
    curl_free(unescaped_value);

    /* Make sure that the string is terminated. */
    dest[max_dest_size - 1] = '\0';
  } else {
    return dio_get_estring_raw(din, dest, max_dest_size);
  }

  return TRUE;
}

#endif /* FREECIV_JSON_CONNECTION */
