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
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <limits.h>

#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
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

#include "capability.h"
#include "events.h"
#include "log.h"
#include "mem.h"
#include "support.h"
#include "tech.h"
#include "worklist.h"

#include "dataio.h"

static const int city_map_index[20] = {
  1, 2, 3, 5, 6, 7, 8, 9, 10, 11, 13, 14, 15, 16, 17, 18, 19, 21, 22, 23
};

/**************************************************************************
...
**************************************************************************/
static DIO_PUT_CONV_FUN put_conv_callback = NULL;

/**************************************************************************
...
**************************************************************************/
void dio_set_put_conv_callback(DIO_PUT_CONV_FUN fun)
{
  put_conv_callback = fun;
}

/**************************************************************************
 Returns FALSE if the destination isn't large enough or the source was
 bad.
**************************************************************************/
static bool get_conv(char *dst, size_t ndst, const unsigned char *src,
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
...
**************************************************************************/
static DIO_GET_CONV_FUN get_conv_callback = get_conv;

/**************************************************************************
...
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
  if (ADD_TO_POINTER(dout->current, size) >=
      ADD_TO_POINTER(dout->dest, dout->dest_size)) {
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
  if (dio_input_remaining(din) < size) {
    din->too_short = TRUE;
    return FALSE;
  } else {
    return TRUE;
  }
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
  din->too_short = FALSE;
  din->bad_string = FALSE;
  din->bad_bit_string = FALSE;
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
...
**************************************************************************/
void dio_put_uint8(struct data_out *dout, int value)
{
  if (enough_space(dout, 1)) {
    unsigned char x = value;

    memcpy(ADD_TO_POINTER(dout->dest, dout->current), &x, 1);
    dout->current++;
  }
}

/**************************************************************************
...
**************************************************************************/
void dio_put_uint16(struct data_out *dout, int value)
{
  if (enough_space(dout, 2)) {
    unsigned short x = htons(value);

    memcpy(ADD_TO_POINTER(dout->dest, dout->current), &x, 2);
    dout->current += 2;
  }
}

/**************************************************************************
...
**************************************************************************/
void dio_put_uint32(struct data_out *dout, int value)
{
  if (enough_space(dout, 4)) {
    unsigned long x = htonl(value);

    memcpy(ADD_TO_POINTER(dout->dest, dout->current), &x, 4);
    dout->current += 4;
  }
}

/**************************************************************************
...
**************************************************************************/
void dio_put_bool8(struct data_out *dout, bool value)
{
  if (value != TRUE && value != FALSE) {
    freelog(LOG_ERROR, "Trying to put a non-boolean: %d", (int) value);
    value = FALSE;
  }

  dio_put_uint8(dout, value ? 1 : 0);
}

/**************************************************************************
...
**************************************************************************/
void dio_put_bool32(struct data_out *dout, bool value)
{
  if (value != TRUE && value != FALSE) {
    freelog(LOG_ERROR, "Trying to put a non-boolean: %d", (int) value);
    value = FALSE;
  }

  dio_put_uint32(dout, value ? 1 : 0);
}

/**************************************************************************
...
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
...
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
...
**************************************************************************/
void dio_put_memory(struct data_out *dout, const void *value, size_t size)
{
  if (enough_space(dout, size)) {
    memcpy(ADD_TO_POINTER(dout->dest, dout->current), value, size);
    dout->current += size;
  }
}

/**************************************************************************
...
**************************************************************************/
void dio_put_string(struct data_out *dout, const char *value)
{
  if (put_conv_callback) {
    size_t length;
    unsigned char *buffer;

    if ((buffer = (*put_conv_callback) (value, &length))) {
      dio_put_memory(dout, buffer, length + 1);
      free(buffer);
    }
  } else {
    dio_put_memory(dout, value, strlen(value) + 1);
  }
}

/**************************************************************************
...
**************************************************************************/
void dio_put_bit_string(struct data_out *dout, const char *value)
{
  /* Note that size_t is often an unsigned type, so we must be careful
   * with the math when calculating 'bytes'. */
  size_t bits = strlen(value), bytes;
  size_t max = (unsigned short)(-1);

  if (bits > max) {
    freelog(LOG_ERROR, "Bit string too long: %lu bits.", (unsigned long)bits);
    assert(FALSE);
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
...
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
...
**************************************************************************/
void dio_put_worklist(struct data_out *dout, const struct worklist *pwl,
		      bool real_wl)
{
  dio_put_bool8(dout, pwl->is_valid);

  if (pwl->is_valid) {
    int i, length = worklist_length(pwl);

    if (real_wl) {
      dio_put_string(dout, pwl->name);
    } else {
      dio_put_string(dout, "\0");
    }

    dio_put_uint8(dout, length);
    for (i = 0; i < length; i++) {
      dio_put_uint8(dout, pwl->wlefs[i]);
      dio_put_uint8(dout, pwl->wlids[i]);
    }
  }
}

/**************************************************************************
...
**************************************************************************/
void dio_put_city_map(struct data_out *dout, const char *value)
{
  int i;

  for (i = 0; i < 20; i += 5) {
    dio_put_uint8(dout, (value[city_map_index[i]] - '0') * 81 +
		  (value[city_map_index[i + 1]] - '0') * 27 +
		  (value[city_map_index[i + 2]] - '0') * 9 +
		  (value[city_map_index[i + 3]] - '0') * 3 +
		  (value[city_map_index[i + 4]] - '0') * 1);
  }
}

/**************************************************************************
...
**************************************************************************/
void dio_get_uint8(struct data_in *din, int *dest)
{
  if (enough_data(din, 1)) {
    if (dest) {
      unsigned char x;

      memcpy(&x, ADD_TO_POINTER(din->src, din->current), 1);
      *dest = x;
    }
    din->current++;
  }
}

/**************************************************************************
...
**************************************************************************/
void dio_get_uint16(struct data_in *din, int *dest)
{
  if (enough_data(din, 2)) {
    if (dest) {
      unsigned short x;

      memcpy(&x, ADD_TO_POINTER(din->src, din->current), 2);
      *dest = ntohs(x);
    }
    din->current += 2;
  }
}

/**************************************************************************
...
**************************************************************************/
void dio_get_uint32(struct data_in *din, int *dest)
{
  if (enough_data(din, 4)) {
    if (dest) {
      unsigned long x;

      memcpy(&x, ADD_TO_POINTER(din->src, din->current), 4);
      *dest = ntohl(x);
    }
    din->current += 4;
  }
}

/**************************************************************************
...
**************************************************************************/
void dio_get_bool8(struct data_in *din, bool * dest)
{
  int ival;

  dio_get_uint8(din, &ival);

  if (ival != 0 && ival != 1) {
    freelog(LOG_ERROR, "Received value isn't boolean: %d", ival);
    ival = 1;
  }

  *dest = (ival != 0);
}

/**************************************************************************
...
**************************************************************************/
void dio_get_bool32(struct data_in *din, bool * dest)
{
  int ival;

  dio_get_uint32(din, &ival);

  if (ival != 0 && ival != 1) {
    freelog(LOG_ERROR, "Received value isn't boolean: %d", ival);
    ival = 1;
  }

  *dest = (ival != 0);
}

/**************************************************************************
...
**************************************************************************/
void dio_get_sint16(struct data_in *din, int *dest)
{
  int tmp;

  dio_get_uint16(din, &tmp);
  if (dest) {
    if (tmp > 0x7fff) {
      tmp -= 0x10000;
    }
    *dest = tmp;
  }
}

/**************************************************************************
...
**************************************************************************/
void dio_get_memory(struct data_in *din, void *dest, size_t dest_size)
{
  if (enough_data(din, dest_size)) {
    if (dest) {
      memcpy(dest, ADD_TO_POINTER(din->src, din->current), dest_size);
    }
    din->current += dest_size;
  }
}

/**************************************************************************
...
**************************************************************************/
void dio_get_string(struct data_in *din, char *dest, size_t max_dest_size)
{
  unsigned char *c;
  size_t ps_len;		/* length in packet, not including null */
  size_t offset, remaining;

  assert(max_dest_size > 0 || dest == NULL);

  if (!enough_data(din, 1)) {
    dest[0] = '\0';
    return;
  }

  remaining = dio_input_remaining(din);
  c = ADD_TO_POINTER(din->src, din->current);

  /* avoid using strlen (or strcpy) on an (unsigned char*)  --dwp */
  for (offset = 0; c[offset] != '\0' && offset < remaining; offset++) {
    /* nothing */
  }

  if (offset >= remaining) {
    ps_len = remaining;
    din->too_short = TRUE;
    din->bad_string = TRUE;
  } else {
    ps_len = offset;
  }

  if (dest && !(*get_conv_callback) (dest, max_dest_size, c, ps_len)) {
    din->bad_string = TRUE;
  }

  if (!din->too_short) {
    din->current += (ps_len + 1);	/* past terminator */
  }
}

/**************************************************************************
...
**************************************************************************/
void dio_get_bit_string(struct data_in *din, char *dest,
			size_t max_dest_size)
{
  int npack;			/* number claimed in packet */
  int i;			/* iterate the bytes */

  assert(dest != NULL && max_dest_size > 0);

  if (!enough_data(din, 1)) {
    dest[0] = '\0';
    return;
  }

  dio_get_uint16(din, &npack);
  if (npack >= max_dest_size) {
    din->bad_bit_string = TRUE;
    dest[0] = '\0';
    return;
  }

  for (i = 0; i < npack;) {
    int bit, byte_value;

    dio_get_uint8(din, &byte_value);
    for (bit = 0; bit < 8 && i < npack; bit++, i++) {
      if (TEST_BIT(byte_value, bit)) {
	dest[i] = '1';
      } else {
	dest[i] = '0';
      }
    }
  }

  dest[npack] = '\0';

  if (din->too_short) {
    din->bad_bit_string = TRUE;
  }
}

/**************************************************************************
...
**************************************************************************/
void dio_get_city_map(struct data_in *din, char *dest, size_t max_dest_size)
{
  int i;

  if (dest) {
    assert(max_dest_size >= 26);
    dest[0] = '2';
    dest[4] = '2';
    dest[12] = '1';
    dest[20] = '2';
    dest[24] = '2';
    dest[25] = '\0';
  }

  if (!enough_data(din, 4)) {
    if (dest) {
      for (i = 0; i < 20;) {
	int j;

	for (j = 0; j < 5; j++) {
	  dest[city_map_index[i++]] = '0';
	}
      }
    }
    return;
  }

  for (i = 0; i < 20;) {
    int j;

    dio_get_uint8(din, &j);

    if (dest) {
      dest[city_map_index[i++]] = '0' + j / 81;
      j %= 81;
      dest[city_map_index[i++]] = '0' + j / 27;
      j %= 27;
      dest[city_map_index[i++]] = '0' + j / 9;
      j %= 9;
      dest[city_map_index[i++]] = '0' + j / 3;
      j %= 3;
      dest[city_map_index[i++]] = '0' + j;
    }
  }
}

/**************************************************************************
...
**************************************************************************/
void dio_get_tech_list(struct data_in *din, int *dest)
{
  int i;

  for (i = 0; i < MAX_NUM_TECH_LIST; i++) {
    dio_get_uint8(din, &dest[i]);
    if (dest[i] == A_LAST) {
      break;
    }
  }

  for (; i < MAX_NUM_TECH_LIST; i++) {
    dest[i] = A_LAST;
  }
}

/**************************************************************************
...
**************************************************************************/
void dio_get_worklist(struct data_in *din, struct worklist *pwl)
{
  dio_get_bool8(din, &pwl->is_valid);

  if (pwl->is_valid) {
    int i, length;

    dio_get_string(din, pwl->name, MAX_LEN_NAME);

    dio_get_uint8(din, &length);

    if (length < MAX_LEN_WORKLIST) {
      pwl->wlefs[length] = WEF_END;
      pwl->wlids[length] = 0;
    }

    if (length > MAX_LEN_WORKLIST) {
      length = MAX_LEN_WORKLIST;
    }

    for (i = 0; i < length; i++) {
      dio_get_uint8(din, (int *) &pwl->wlefs[i]);
      dio_get_uint8(din, &pwl->wlids[i]);
    }
  }
}

/**************************************************************************
...
**************************************************************************/
void dio_get_uint8_vec8(struct data_in *din, int **values, int stop_value)
{
  int count, inx;

  dio_get_uint8(din, &count);
  if (values) {
    *values = fc_malloc((count + 1) * sizeof(int));
  }
  for (inx = 0; inx < count; inx++) {
    dio_get_uint8(din, values ? &((*values)[inx]) : NULL);
  }
  if (values) {
    (*values)[inx] = stop_value;
  }
}

/**************************************************************************
...
**************************************************************************/
void dio_get_uint16_vec8(struct data_in *din, int **values, int stop_value)
{
  int count, inx;

  dio_get_uint8(din, &count);
  if (values) {
    *values = fc_malloc((count + 1) * sizeof(int));
  }
  for (inx = 0; inx < count; inx++) {
    dio_get_uint16(din, values ? &((*values)[inx]) : NULL);
  }
  if (values) {
    (*values)[inx] = stop_value;
  }
}
