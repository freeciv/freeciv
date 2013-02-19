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

#include <limits.h>
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
  Insert value using 8 bits. May overflow.
**************************************************************************/
void dio_put_uint8(struct data_out *dout, int value)
{
  if (value < 0x00 || 0xff < value) {
    log_error("Trying to put %d into 8 bits", value);
  }

  if (enough_space(dout, 1)) {
    uint8_t x = value;

    FC_STATIC_ASSERT(sizeof(x) == 1, uint8_not_1_byte);
    memcpy(ADD_TO_POINTER(dout->dest, dout->current), &x, 1);
    dout->current++;
  }
}

/**************************************************************************
  Insert value using 16 bits. May overflow.
**************************************************************************/
void dio_put_uint16(struct data_out *dout, int value)
{
  if (value < 0x0000 || 0xffff < value) {
    log_error("Trying to put %d into 16 bits", value);
  }

  if (enough_space(dout, 2)) {
    uint16_t x = htons(value);

    FC_STATIC_ASSERT(sizeof(x) == 2, uint16_not_2_bytes);
    memcpy(ADD_TO_POINTER(dout->dest, dout->current), &x, 2);
    dout->current += 2;
  }
}

/**************************************************************************
  Insert value using 32 bits. May overflow.
**************************************************************************/
void dio_put_uint32(struct data_out *dout, int value)
{
  if (sizeof(value) > 4 && (value < 0x00000000 || 0xffffffff < value)) {
    log_error("Trying to put %d into 32 bits", value);
  }

  if (enough_space(dout, 4)) {
    uint32_t x = htonl(value);

    FC_STATIC_ASSERT(sizeof(x) == 4, uint32_not_4_bytes);
    memcpy(ADD_TO_POINTER(dout->dest, dout->current), &x, 4);
    dout->current += 4;
  }
}

/**************************************************************************
  Insert value using 8 bits. May overflow.
**************************************************************************/
void dio_put_sint8(struct data_out *dout, int value)
{
  dio_put_uint8(dout, 0 <= value ? value : value + 0x100);
}

/**************************************************************************
  Insert value using 16 bits. May overflow.
**************************************************************************/
void dio_put_sint16(struct data_out *dout, int value)
{
  dio_put_uint16(dout, 0 <= value ? value : value + 0x10000);
}

/**************************************************************************
  Insert value using 32 bits. May overflow.
**************************************************************************/
void dio_put_sint32(struct data_out *dout, int value)
{
  dio_put_uint32(dout, (0 <= value || sizeof(value) == 4
                        ? value : value + 0x100000000));
}

/**************************************************************************
  Insert value 0 or 1 using 8 bits.
**************************************************************************/
void dio_put_bool8(struct data_out *dout, bool value)
{
  if (value != TRUE && value != FALSE) {
    log_error("Trying to put a non-boolean: %d", (int) value);
    value = FALSE;
  }

  dio_put_uint8(dout, value ? 1 : 0);
}

/**************************************************************************
  Insert value 0 or 1 using 32 bits.
**************************************************************************/
void dio_put_bool32(struct data_out *dout, bool value)
{
  if (value != TRUE && value != FALSE) {
    log_error("Trying to put a non-boolean: %d", (int) value);
    value = FALSE;
  }

  dio_put_uint32(dout, value ? 1 : 0);
}

/**************************************************************************
  Insert a float number, which is multiplied by 'float_factor' before
  being encoded into a uint32.
**************************************************************************/
void dio_put_float(struct data_out *dout, float value, int float_factor)
{
  dio_put_uint32(dout, value * float_factor);
}

/**************************************************************************
  Insert number of values brefore stop_value using 8 bits. Then
  insert values using 8 bits for each. stop_value is not required to
  fit in 8 bits. Actual values may overflow.
**************************************************************************/
void dio_put_uint8_vec8(struct data_out *dout, int *values, int stop_value)
{
  size_t count;

  for (count = 0; values[count] != stop_value; count++) {
    /* nothing */
  }

  if (enough_space(dout, 1 + count)) {
    size_t i;

    dio_put_uint8(dout, count);

    for (i = 0; i < count; i++) {
      dio_put_uint8(dout, values[i]);
    }
  }
}

/**************************************************************************
  Insert number of values brefore stop_value using 8 bits. Then
  insert values using 16 bits for each. stop_value is not required to
  fit in 16 bits. Actual values may overflow.
**************************************************************************/
void dio_put_uint16_vec8(struct data_out *dout, int *values, int stop_value)
{
  size_t count;

  for (count = 0; values[count] != stop_value; count++) {
    /* nothing */
  }

  if (enough_space(dout, 1 + 2 * count)) {
    size_t i;

    dio_put_uint8(dout, count);

    for (i = 0; i < count; i++) {
      dio_put_uint16(dout, values[i]);
    }
  }
}

/**************************************************************************
  Insert block directly from memory.
**************************************************************************/
void dio_put_memory(struct data_out *dout, const void *value, size_t size)
{
  if (enough_space(dout, size)) {
    memcpy(ADD_TO_POINTER(dout->dest, dout->current), value, size);
    dout->current += size;
  }
}

/**************************************************************************
  Insert NULL-terminated string. Conversion callback is used if set.
**************************************************************************/
void dio_put_string(struct data_out *dout, const char *value)
{
  if (put_conv_callback) {
    size_t length;
    char *buffer;

    if ((buffer = (*put_conv_callback) (value, &length))) {
      dio_put_memory(dout, buffer, length + 1);
      free(buffer);
    }
  } else {
    dio_put_memory(dout, value, strlen(value) + 1);
  }
}

/**************************************************************************
  Insert number of bits as 16 bit value and then insert bits. In incoming
  value string each bit is represented by one character, value '1' indicating
  TRUE bit.
**************************************************************************/
void dio_put_bit_string(struct data_out *dout, const char *value)
{
  /* Note that size_t is often an unsigned type, so we must be careful
   * with the math when calculating 'bytes'. */
  size_t bits = strlen(value), bytes;
  size_t max = (unsigned short)(-1);

  if (bits > max) {
    log_error("Bit string too long: %lu bits.", (unsigned long) bits);
    bits = max;
  }
  bytes = (bits + 7) / 8;

  if (enough_space(dout, bytes + 1)) {
    size_t i;

    dio_put_uint16(dout, bits);

    for (i = 0; i < bits;) {
      int bit, data = 0;

      for (bit = 0; bit < 8 && i < bits; bit++, i++) {
	if (value[i] == '1') {
	  data |= (1 << bit);
	}
      }
      dio_put_uint8(dout, data);
    }
  }
}

/**************************************************************************
  Insert tech numbers from value array as 8 bit values until there is value
  A_LAST or MAX_NUM_TECH_LIST tech numbers have been inserted.
**************************************************************************/
void dio_put_tech_list(struct data_out *dout, const int *value)
{
  int i;

  for (i = 0; i < MAX_NUM_TECH_LIST; i++) {
    dio_put_uint8(dout, value[i]);
    if (value[i] == A_LAST) {
      break;
    }
  }
}

/**************************************************************************
  Insert unit type numbers from value array as 8 bit values until there is
  value U_LAST or MAX_NUM_UNIT_LIST numbers have been inserted.
**************************************************************************/
void dio_put_unit_list(struct data_out *dout, const int *value)
{
  int i;

  for (i = 0; i < MAX_NUM_UNIT_LIST; i++) {
    dio_put_uint8(dout, value[i]);
    if (value[i] == U_LAST) {
      break;
    }
  }
}

/**************************************************************************
  Insert building type numbers from value array as 8 bit values until there
  is value B_LAST or MAX_NUM_BUILDING_LIST numbers have been inserted.
**************************************************************************/
void dio_put_building_list(struct data_out *dout, const int *value)
{
  int i;

  for (i = 0; i < MAX_NUM_BUILDING_LIST; i++) {
    dio_put_uint8(dout, value[i]);
    if (value[i] == B_LAST) {
      break;
    }
  }
}

/**************************************************************************
  Insert number of worklist items as 8 bit value and then insert
  8 bit kind and 8 bit number for each worklist item.
**************************************************************************/
void dio_put_worklist(struct data_out *dout, const struct worklist *pwl)
{
  int i, length = worklist_length(pwl);

  dio_put_uint8(dout, length);
  for (i = 0; i < length; i++) {
    const struct universal *pcp = &(pwl->entries[i]);

    dio_put_uint8(dout, pcp->kind);
    dio_put_uint8(dout, universal_number(pcp));
  }
}

/**************************************************************************
 Receive uint8 value to dest.
**************************************************************************/
bool dio_get_uint8(struct data_in *din, int *dest)
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
 Receive uint16 value to dest.
**************************************************************************/
bool dio_get_uint16(struct data_in *din, int *dest)
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
 Receive uint32 value to dest.
**************************************************************************/
bool dio_get_uint32(struct data_in *din, int *dest)
{
  uint32_t x;

  FC_STATIC_ASSERT(sizeof(x) == 4, uint32_not_4_bytes);

  if (!enough_data(din, 4)) {
    log_packet("Packet too short to read 4 bytes");

    return FALSE;
  }

  memcpy(&x, ADD_TO_POINTER(din->src, din->current), 4);
  *dest = ntohl(x);
  din->current += 4;
  return TRUE;
}

/**************************************************************************
  Take boolean value from 8 bits.
**************************************************************************/
bool dio_get_bool8(struct data_in *din, bool *dest)
{
  int ival;

  if (!dio_get_uint8(din, &ival)) {
    return FALSE;
  }

  if (ival != 0 && ival != 1) {
    log_packet("Got a bad boolean: %d", ival);
    return FALSE;
  }

  *dest = (ival != 0);
  return TRUE;
}

/**************************************************************************
  Take boolean value from 32 bits.
**************************************************************************/
bool dio_get_bool32(struct data_in *din, bool * dest)
{
  int ival;

  if (!dio_get_uint32(din, &ival)) {
    return FALSE;
  }

  if (ival != 0 && ival != 1) {
    log_packet("Got a bad boolean: %d", ival);
    return FALSE;
  }

  *dest = (ival != 0);
  return TRUE;
}

/**************************************************************************
  Get a float number, which have been multiplied by 'float_factor' and
  encoded into a uint32 by dio_put_float().
**************************************************************************/
bool dio_get_float(struct data_in *din, float *dest, int float_factor)
{
  int ival;

  if (!dio_get_uint32(din, &ival)) {
    return FALSE;
  }

  *dest = (float) ival / float_factor;
  return TRUE;
}

/**************************************************************************
  Take value from 8 bits.
**************************************************************************/
bool dio_get_sint8(struct data_in *din, int *dest)
{
  int tmp;

  if (!dio_get_uint8(din, &tmp)) {
    return FALSE;
  }

  if (tmp > 0x7f) {
    tmp -= 0x100;
  }
  *dest = tmp;
  return TRUE;
}

/**************************************************************************
  Take value from 16 bits.
**************************************************************************/
bool dio_get_sint16(struct data_in *din, int *dest)
{
  int tmp;

  if (!dio_get_uint16(din, &tmp)) {
    return FALSE;
  }

  if (tmp > 0x7fff) {
    tmp -= 0x10000;
  }
  *dest = tmp;
  return TRUE;
}

/**************************************************************************
  Take value from 32 bits.
**************************************************************************/
bool dio_get_sint32(struct data_in *din, int *dest)
{
  int tmp;

  if (!dio_get_uint32(din, &tmp)) {
    return FALSE;
  }

  if (tmp > 0x7fffffff) {
    tmp -= 0x100000000;
  }
  *dest = tmp;
  return TRUE;
}

/**************************************************************************
  Take memory block directly.
**************************************************************************/
bool dio_get_memory(struct data_in *din, void *dest, size_t dest_size)
{
  if (!enough_data(din, dest_size)) {
    log_packet("Got too short memory");
    return FALSE;
  }

  memcpy(dest, ADD_TO_POINTER(din->src, din->current), dest_size);
  din->current += dest_size;
  return TRUE;
}

/**************************************************************************
  Take string. Conversion callback is used.
**************************************************************************/
bool dio_get_string(struct data_in *din, char *dest, size_t max_dest_size)
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
  Take bits and produce string containing chars '0' and '1'
**************************************************************************/
bool dio_get_bit_string(struct data_in *din, char *dest,
			size_t max_dest_size)
{
  int npack = 0;		/* number claimed in packet */
  int i;			/* iterate the bytes */

  fc_assert(max_dest_size > 0);

  if (!dio_get_uint16(din, &npack)) {
    log_packet("Got a bad bit string");
    return FALSE;
  }

  if (npack >= max_dest_size) {
    log_packet("Have size for %lu, got %d",
               (unsigned long) max_dest_size, npack);
    return FALSE;
  }

  for (i = 0; i < npack;) {
    int bit, byte_value;

    if (!dio_get_uint8(din, &byte_value)) {
      log_packet("Got a too short bit string");
      return FALSE;
    }
    for (bit = 0; bit < 8 && i < npack; bit++, i++) {
      if (TEST_BIT(byte_value, bit)) {
	dest[i] = '1';
      } else {
	dest[i] = '0';
      }
    }
  }

  dest[npack] = '\0';
  return TRUE;
}

/**************************************************************************
  Take tech numbers until A_LAST encountered, or MAX_NUM_TECH_LIST techs
  retrieved.
**************************************************************************/
bool dio_get_tech_list(struct data_in *din, int *dest)
{
  int i;

  for (i = 0; i < MAX_NUM_TECH_LIST; i++) {
    if (!dio_get_uint8(din, &dest[i])) {
      log_packet("Got a too short tech list");
      return FALSE;
    }
    if (dest[i] == A_LAST) {
      break;
    }
  }

  for (; i < MAX_NUM_TECH_LIST; i++) {
    dest[i] = A_LAST;
  }

  return TRUE;
}

/**************************************************************************
  Take unit type numbers until U_LAST encountered, or MAX_NUM_UNIT_LIST
  types retrieved.
**************************************************************************/
bool dio_get_unit_list(struct data_in *din, int *dest)
{
  int i;

  for (i = 0; i < MAX_NUM_UNIT_LIST; i++) {
    if (!dio_get_uint8(din, &dest[i])) {
      log_packet("Got a too short unit list");
      return FALSE;
    }
    if (dest[i] == U_LAST) {
      break;
    }
  }

  for (; i < MAX_NUM_UNIT_LIST; i++) {
    dest[i] = U_LAST;
  }

  return TRUE;
}

/**************************************************************************
  Take building type numbers until B_LAST encountered, or
  MAX_NUM_BUILDING_LIST types retrieved.
**************************************************************************/
bool dio_get_building_list(struct data_in *din, int *dest)
{
  int i;

  for (i = 0; i < MAX_NUM_BUILDING_LIST; i++) {
    if (!dio_get_uint8(din, &dest[i])) {
      log_packet("Got a too short building list");
      return FALSE;
    }
    if (dest[i] == B_LAST) {
      break;
    }
  }

  for (; i < MAX_NUM_BUILDING_LIST; i++) {
    dest[i] = B_LAST;
  }

  return TRUE;
}

/**************************************************************************
  Take worklist item count and then kind and number for each item, and
  put them to provided worklist.
**************************************************************************/
bool dio_get_worklist(struct data_in *din, struct worklist *pwl)
{
  int i, length;

  worklist_init(pwl);

  if (!dio_get_uint8(din, &length)) {
    log_packet("Got a bad worklist");
    return FALSE;
  }

  for (i = 0; i < length; i++) {
    int identifier;
    int kind;

    if (!dio_get_uint8(din, &kind)
        || !dio_get_uint8(din, &identifier)) {
      log_packet("Got a too short worklist");
      return FALSE;
    }

    /*
     * FIXME: the value returned by universal_by_number() should be checked!
     */
    worklist_append(pwl, universal_by_number(kind, identifier));
  }

  return TRUE;
}

/**************************************************************************
  Take vector of 8 bit values and insert stop_value after them. stop_value
  does not need to fit in 8 bits.
**************************************************************************/
bool dio_get_uint8_vec8(struct data_in *din, int **values, int stop_value)
{
  int count, inx;
  int *vec;

  if (!dio_get_uint8(din, &count)) {
    return FALSE;
  }

  vec = fc_calloc(count + 1, sizeof(*vec));
  for (inx = 0; inx < count; inx++) {
    if (!dio_get_uint8(din, vec + inx)) {
      free (vec);
      return FALSE;
    }
  }
  vec[inx] = stop_value;
  *values = vec;

  return TRUE;
}

/**************************************************************************
 Receive vector of uint6 values.
**************************************************************************/
bool dio_get_uint16_vec8(struct data_in *din, int **values, int stop_value)
{
  int count, inx;
  int *vec;

  if (!dio_get_uint8(din, &count)) {
    return FALSE;
  }

  vec = fc_calloc(count + 1, sizeof(*vec));
  for (inx = 0; inx < count; inx++) {
    if (!dio_get_uint16(din, vec + inx)) {
      free (vec);
      return FALSE;
    }
  }
  vec[inx] = stop_value;
  *values = vec;

  return TRUE;
}

/**************************************************************************
  De-serialize a requirement.
**************************************************************************/
bool dio_get_requirement(struct data_in *din, struct requirement *preq)
{
  int type, range, value;
  bool survives, negated;

  if (!dio_get_uint8(din, &type)
      || !dio_get_sint32(din, &value)
      || !dio_get_uint8(din, &range)
      || !dio_get_bool8(din, &survives)
      || !dio_get_bool8(din, &negated)) {
    log_packet("Got a bad requirement");
    return FALSE;
  }

  /*
   * FIXME: the value returned by req_from_values() should be checked!
   */
  *preq = req_from_values(type, range, survives, negated, value);

  return TRUE;
}

/**************************************************************************
  Serialize a requirement.
**************************************************************************/
void dio_put_requirement(struct data_out *dout, const struct requirement *preq)
{
  int type, range, value;
  bool survives, negated;

  req_get_values(preq, &type, &range, &survives, &negated, &value);

  dio_put_uint8(dout, type);
  dio_put_sint32(dout, value);
  dio_put_uint8(dout, range);
  dio_put_bool8(dout, survives);
  dio_put_bool8(dout, negated);
}
