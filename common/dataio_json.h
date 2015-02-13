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
#ifndef FC__DATAIO_JSON_H
#define FC__DATAIO_JSON_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include <jansson.h>

#include "bitvector.h"
#include "support.h"            /* bool type */

struct worklist;
struct requirement;

struct data_in {
  const void *src;
  size_t src_size, current;
};

struct data_out {
  void *dest;
  json_t *json;
  size_t dest_size, used, current;
  bool too_short;		/* set to 1 if try to read past end */
};

/* Used for dio_<put|get>_type() methods.
 * NB: we only support integer handling currently. */
enum data_type {
  DIOT_UINT8,
  DIOT_UINT16,
  DIOT_UINT32,
  DIOT_SINT8,
  DIOT_SINT16,
  DIOT_SINT32,

  DIOT_LAST
};

/* network string conversion */
typedef char *(*DIO_PUT_CONV_FUN) (const char *src, size_t *length);
void dio_set_put_conv_callback(DIO_PUT_CONV_FUN fun);

typedef bool(*DIO_GET_CONV_FUN) (char *dst, size_t ndst,
				 const char *src, size_t nsrc);
void dio_set_get_conv_callback(DIO_GET_CONV_FUN fun);

/* General functions */
void dio_output_init(struct data_out *dout, void *destination,
		     size_t dest_size);
void dio_output_rewind(struct data_out *dout);
size_t dio_output_used(struct data_out *dout);

void dio_input_init(struct data_in *dout, const void *src, size_t src_size);
void dio_input_rewind(struct data_in *din);
size_t dio_input_remaining(struct data_in *din);
bool dio_input_skip(struct data_in *din, size_t size);

size_t data_type_size(enum data_type type);

/* gets */
bool dio_get_type_json(struct data_in *din, enum data_type type, int *dest)
    fc__attribute((nonnull (3)));

bool dio_get_uint8_json(json_t *json_packet, char *key, int *dest);
bool dio_get_uint16_json(json_t *json_packet, char *key, int *dest);
bool dio_get_uint32_json(json_t *json_packet, char *key, int *dest);

bool dio_get_uint16_raw(struct data_in *din, int *dest);
bool dio_get_uint8_raw(struct data_in *din, int *dest);
bool dio_get_sint8_json(json_t *json_packet, char *key, int *dest);
bool dio_get_sint16_json(json_t *json_packet, char *key, int *dest);
#define dio_get_sint32_json(d,v,x) dio_get_uint32_json(d,v,x)


bool dio_get_bool8_json(json_t *json_packet, char *key, bool *dest);
bool dio_get_bool32_json(json_t *json_packet, char *key, bool *dest);
bool dio_get_ufloat_json(json_t *json_packet, char *key, float *dest, int float_factor);
bool dio_get_sfloat_json(json_t *json_packet, char *key, float *dest, int float_factor);
bool dio_get_memory_json(json_t *json_packet, char *key, void *dest, size_t dest_size);
bool dio_get_string_json(json_t *json_packet, char *key, char *dest, size_t max_dest_size);
bool dio_get_string_raw(struct data_in *din, char *dest, size_t max_dest_size);
bool dio_get_bit_string_json(json_t *json_packet, char *key, char *dest,
                             size_t max_dest_size);
bool dio_get_tech_list_json(json_t *json_packet, char *key, int *dest);
bool dio_get_unit_list_json(json_t *json_packet, char *key, int *dest);
bool dio_get_building_list_json(json_t *json_packet, char *key, int *dest);
bool dio_get_worklist_json(json_t *json_packet, char *key, struct worklist *pwl);
bool dio_get_requirement_json(json_t *json_packet, char *key, struct requirement *preq);

bool dio_get_uint8_vec8_json(json_t *json_packet, char *key, int **values, int stop_value);
bool dio_get_uint16_vec8_json(json_t *json_packet, char *key, int **values, int stop_value);

/* Should be a function but we need some macro magic. */
#define DIO_BV_GET(pdin, bv) \
  dio_get_memory_json(pc->json_packet, "mem", (bv).vec, sizeof((bv).vec))

#define DIO_GET(f, d, k, ...) dio_get_##f##_json(pc->json_packet, k, ## __VA_ARGS__)

/* puts */
void dio_put_type_json(struct data_out *dout, enum data_type type, char *key, int value);

void dio_put_uint8_json(struct data_out *dout, char *key, int value);
void dio_put_uint8_raw(struct data_out *dout, int value);
void dio_put_uint16_json(struct data_out *dout, char *key, int value);
void dio_put_uint32_json(struct data_out *dout, char *key, int value);
void dio_put_uint16_raw(struct data_out *dout, int value);

void dio_put_array_uint8_json(struct data_out *dout, char *key, int *values, int size);
void dio_put_array_uint32_json(struct data_out *dout, char *key, int *values, int size);
void dio_put_array_sint8_json(struct data_out *dout, char *key, int *values, int size);
void dio_put_array_sint16_json(struct data_out *dout, char *key, int *values, int size);
void dio_put_array_sint32_json(struct data_out *dout, char *key, int *values, int size);
void dio_put_array_bool8_json(struct data_out *dout, char *key, bool *values, int size);

#define dio_put_sint8_json(d,k,v) dio_put_uint8_json(d,k,v)
#define dio_put_sint16_json(d,k,v) dio_put_uint16_json(d,k,v)
#define dio_put_sint32_json(d,k,v) dio_put_uint32_json(d,k,v)

void dio_put_bool8_json(struct data_out *dout, char *key, bool value);
void dio_put_bool32_json(struct data_out *dout, char *key, bool value);
void dio_put_ufloat_json(struct data_out *dout, char *key, float value, int float_factor);
void dio_put_sfloat_json(struct data_out *dout, char *key, float value, int float_factor);

void dio_put_memory_json(struct data_out *dout, char *key, const void *value, size_t size);
void dio_put_string_json(struct data_out *dout, char *key, const char *value);
void dio_put_bit_string_json(struct data_out *dout, char *key, const char *value);
void dio_put_city_map_json(struct data_out *dout, char *key, const char *value);
void dio_put_tech_list_json(struct data_out *dout, char *key, const int *value);
void dio_put_unit_list_json(struct data_out *dout, char *key, const int *value);
void dio_put_building_list_json(struct data_out *dout, char *key, const int *value);
void dio_put_worklist_json(struct data_out *dout, char *key, const struct worklist *pwl);
void dio_put_requirement_json(struct data_out *dout, char *key,
                              const struct requirement *preq, int size);

void dio_put_uint8_vec8_json(struct data_out *dout, char *key, int *values, int stop_value);
void dio_put_uint16_vec8_json(struct data_out *dout, char *key, int *values, int stop_value);
void dio_put_string_raw(struct data_out *dout, const char *value);
void dio_put_memory_raw(struct data_out *dout, const void *value, size_t size);
void dio_put_string_array_json(struct data_out *dout, char *key, const char *value, int size);

/* Should be a function but we need some macro magic. */
#define DIO_BV_PUT(pdout, type, bv) \
  dio_put_memory_json((pdout), type, (bv).vec, sizeof((bv).vec))

#define DIO_PUT(f, d, k, ...) dio_put_##f##_json(d, k, ## __VA_ARGS__)

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif  /* FC__DATAIO_JSON_H */
