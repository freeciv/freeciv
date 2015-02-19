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
#ifndef FC__DATAIO_H
#define FC__DATAIO_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include "bitvector.h"
#include "support.h"            /* bool type */

struct worklist;
struct requirement;

struct data_in {
  const void *src;
  size_t src_size, current;
};

struct raw_data_out {
  void *dest;
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

#ifdef FREECIV_JSON_CONNECTION
#include "dataio_json.h"
#endif

/* network string conversion */
typedef char *(*DIO_PUT_CONV_FUN) (const char *src, size_t *length);
void dio_set_put_conv_callback(DIO_PUT_CONV_FUN fun);

typedef bool(*DIO_GET_CONV_FUN) (char *dst, size_t ndst,
				 const char *src, size_t nsrc);
void dio_set_get_conv_callback(DIO_GET_CONV_FUN fun);

/* General functions */
void dio_output_init(struct raw_data_out *dout, void *destination,
		     size_t dest_size);
void dio_output_rewind(struct raw_data_out *dout);
size_t dio_output_used(struct raw_data_out *dout);

void dio_input_init(struct data_in *dout, const void *src, size_t src_size);
void dio_input_rewind(struct data_in *din);
size_t dio_input_remaining(struct data_in *din);
bool dio_input_skip(struct data_in *din, size_t size);

size_t data_type_size(enum data_type type);

/* gets */
bool dio_get_type_raw(struct data_in *din, enum data_type type, int *dest)
    fc__attribute((nonnull (3)));

bool dio_get_uint8_raw(struct data_in *din, int *dest)
    fc__attribute((nonnull (2)));
bool dio_get_uint16_raw(struct data_in *din, int *dest)
    fc__attribute((nonnull (2)));
bool dio_get_uint32_raw(struct data_in *din, int *dest)
    fc__attribute((nonnull (2)));

bool dio_get_sint8_raw(struct data_in *din, int *dest)
    fc__attribute((nonnull (2)));
bool dio_get_sint16_raw(struct data_in *din, int *dest)
    fc__attribute((nonnull (2)));
bool dio_get_sint32_raw(struct data_in *din, int *dest)
    fc__attribute((nonnull (2)));

bool dio_get_bool8_raw(struct data_in *din, bool *dest)
    fc__attribute((nonnull (2)));
bool dio_get_bool32_raw(struct data_in *din, bool *dest)
    fc__attribute((nonnull (2)));
bool dio_get_ufloat_raw(struct data_in *din, float *dest, int float_factor)
    fc__attribute((nonnull (2)));
bool dio_get_sfloat_raw(struct data_in *din, float *dest, int float_factor)
    fc__attribute((nonnull (2)));
bool dio_get_memory_raw(struct data_in *din, void *dest, size_t dest_size)
    fc__attribute((nonnull (2)));
bool dio_get_string_raw(struct data_in *din, char *dest, size_t max_dest_size)
    fc__attribute((nonnull (2)));
bool dio_get_tech_list_raw(struct data_in *din, int *dest)
    fc__attribute((nonnull (2)));
bool dio_get_unit_list_raw(struct data_in *din, int *dest)
    fc__attribute((nonnull (2)));
bool dio_get_building_list_raw(struct data_in *din, int *dest)
    fc__attribute((nonnull (2)));
bool dio_get_worklist_raw(struct data_in *din, struct worklist *pwl)
    fc__attribute((nonnull (2)));
bool dio_get_requirement_raw(struct data_in *din, struct requirement *preq)
    fc__attribute((nonnull (2)));

bool dio_get_uint8_vec8_raw(struct data_in *din, int **values, int stop_value)
    fc__attribute((nonnull (2)));
bool dio_get_uint16_vec8_raw(struct data_in *din, int **values, int stop_value)
    fc__attribute((nonnull (2)));

#ifndef FREECIV_JSON_CONNECTION

/* Should be a function but we need some macro magic. */
#define DIO_BV_GET(pdin, bv)                          \
  dio_get_memory_raw((pdin), (bv).vec, sizeof((bv).vec))

#define DIO_GET(f, d, k, ...) dio_get_##f##_raw(d, ## __VA_ARGS__)

#endif /* FREECIV_JSON_CONNECTION */

/* puts */
void dio_put_type_raw(struct raw_data_out *dout, enum data_type type, int value);

void dio_put_uint8_raw(struct raw_data_out *dout, int value);
void dio_put_uint16_raw(struct raw_data_out *dout, int value);
void dio_put_uint32_raw(struct raw_data_out *dout, int value);

void dio_put_sint8_raw(struct raw_data_out *dout, int value);
void dio_put_sint16_raw(struct raw_data_out *dout, int value);
void dio_put_sint32_raw(struct raw_data_out *dout, int value);

void dio_put_bool8_raw(struct raw_data_out *dout, bool value);
void dio_put_bool32_raw(struct raw_data_out *dout, bool value);
void dio_put_ufloat_raw(struct raw_data_out *dout, float value, int float_factor);
void dio_put_sfloat_raw(struct raw_data_out *dout, float value, int float_factor);

void dio_put_memory_raw(struct raw_data_out *dout, const void *value, size_t size);
void dio_put_string_raw(struct raw_data_out *dout, const char *value);
void dio_put_city_map_raw(struct raw_data_out *dout, const char *value);
void dio_put_tech_list_raw(struct raw_data_out *dout, const int *value);
void dio_put_unit_list_raw(struct raw_data_out *dout, const int *value);
void dio_put_building_list_raw(struct raw_data_out *dout, const int *value);
void dio_put_worklist_raw(struct raw_data_out *dout, const struct worklist *pwl);
void dio_put_requirement_raw(struct raw_data_out *dout, const struct requirement *preq);

void dio_put_uint8_vec8_raw(struct raw_data_out *dout, int *values, int stop_value);
void dio_put_uint16_vec8_raw(struct raw_data_out *dout, int *values, int stop_value);

#ifndef FREECIV_JSON_CONNECTION

/* Should be a function but we need some macro magic. */
#define DIO_BV_PUT(pdout, k, bv)                         \
  dio_put_memory_raw((pdout), (bv).vec, sizeof((bv).vec))

#define DIO_PUT(f, d, k, ...) dio_put_##f##_raw(d, ## __VA_ARGS__)

#endif /* FREECIV_JSON_CONNECTION */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif  /* FC__DATAIO_H */
