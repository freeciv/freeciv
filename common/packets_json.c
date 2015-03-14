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

#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

#ifdef FREECIV_JSON_CONNECTION

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#ifdef HAVE_ARPA_INET_H
#include <arpa/inet.h>
#endif
#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif
#ifdef HAVE_WINSOCK
#include <winsock.h>
#endif

#include <jansson.h>

/* utility */
#include "capability.h"
#include "fcintl.h"
#include "log.h"
#include "mem.h"
#include "shared.h"
#include "support.h"

/* commmon */
#include "dataio.h"
#include "game.h"
#include "events.h"
#include "map.h"

#include "packets_json.h"

#ifdef USE_COMPRESSION
#include <zlib.h>
/*
 * Value for the 16bit size to indicate a jumbo packet
 */
#define JUMBO_SIZE		0xffff

/*
 * All values 0<=size<COMPRESSION_BORDER are uncompressed packets.
 */
#define COMPRESSION_BORDER	(16*1024+1)

/*
 * All compressed packets this size or greater are sent as a jumbo packet.
 */
#define JUMBO_BORDER 		(64*1024-COMPRESSION_BORDER-1)
#endif

#define log_compress    log_debug
#define log_compress2   log_debug

/* 
 * Valid values are 0, 1 and 2. For 2 you have to set generate_stats
 * to 1 in generate_packets.py.
 */
#define PACKET_SIZE_STATISTICS 0

extern const char *const packet_functional_capability;

#define SPECHASH_TAG packet_handler
#define SPECHASH_ASTR_KEY_TYPE
#define SPECHASH_IDATA_TYPE struct packet_handlers *
#define SPECHASH_IDATA_FREE (packet_handler_hash_data_free_fn_t) free
#include "spechash.h"

#ifdef USE_COMPRESSION
static int stat_size_alone = 0;
static int stat_size_uncompressed = 0;
static int stat_size_compressed = 0;
static int stat_size_no_compression = 0;

/****************************************************************************
  Returns the compression level. Initilialize it if needed.
****************************************************************************/
static inline int get_compression_level(void)
{
  static int level = -2;        /* Magic not initialized, see below. */

  if (-2 == level) {
    const char *s = getenv("FREECIV_COMPRESSION_LEVEL");

    if (NULL == s || !str_to_int(s, &level) || -1 > level || 9 < level) {
      level = -1;
    }
  }

  return level;
}

/****************************************************************************
  Send all waiting data. Return TRUE on success.
****************************************************************************/
static bool conn_compression_flush(struct connection *pconn)
{
  int compression_level = get_compression_level();
  uLongf compressed_size = 12 + 1.001 * pconn->compression.queue.size;
  int error;
  Bytef compressed[compressed_size];
  bool jumbo;
  unsigned long compressed_packet_len;

  error = compress2(compressed, &compressed_size,
                    pconn->compression.queue.p,
                    pconn->compression.queue.size,
                    compression_level);
  fc_assert_ret_val(error == Z_OK, FALSE);

  /* Compression signalling currently assumes a 2-byte packet length; if that
   * changes, the protocol should probably be changed */
  fc_assert_ret_val(data_type_size(pconn->packet_header.length) == 2, FALSE);

  /* Include normal length field in decision */
  jumbo = (compressed_size+2 >= JUMBO_BORDER);

  compressed_packet_len = compressed_size + (jumbo ? 6 : 2);
  if (compressed_packet_len < pconn->compression.queue.size) {
    struct data_out dout;

    log_compress("COMPRESS: compressed %lu bytes to %ld (level %d)",
                 (unsigned long) pconn->compression.queue.size,
                 compressed_size, compression_level);
    stat_size_uncompressed += pconn->compression.queue.size;
    stat_size_compressed += compressed_size;

    if (!jumbo) {
      unsigned char header[2];
      FC_STATIC_ASSERT(COMPRESSION_BORDER > MAX_LEN_PACKET,
                       uncompressed_compressed_packet_len_overlap);

      log_compress("COMPRESS: sending %ld as normal", compressed_size);

      dio_output_init(&dout, header, sizeof(header));
      dio_put_uint16(&dout, 2 + compressed_size + COMPRESSION_BORDER);
      connection_send_data(pconn, header, sizeof(header));
      connection_send_data(pconn, compressed, compressed_size);
    } else {
      unsigned char header[6];
      FC_STATIC_ASSERT(JUMBO_SIZE >= JUMBO_BORDER+COMPRESSION_BORDER,
                       compressed_normal_jumbo_packet_len_overlap);

      log_compress("COMPRESS: sending %ld as jumbo", compressed_size);
      dio_output_init(&dout, header, sizeof(header));
      dio_put_uint16(&dout, JUMBO_SIZE);
      dio_put_uint32(&dout, 6 + compressed_size);
      connection_send_data(pconn, header, sizeof(header));
      connection_send_data(pconn, compressed, compressed_size);
    }
  } else {
    log_compress("COMPRESS: would enlarge %lu bytes to %ld; "
                 "sending uncompressed",
                 (unsigned long) pconn->compression.queue.size,
                 compressed_packet_len);
    connection_send_data(pconn, pconn->compression.queue.p,
                         pconn->compression.queue.size);
    stat_size_no_compression += pconn->compression.queue.size;
  }
  return pconn->used;
}
#endif /* USE_COMPRESSION */

/**************************************************************************
  Read and return a packet from the connection 'pc'. The type of the
  packet is written in 'ptype'. On error, the connection is closed and
  the function returns NULL.
**************************************************************************/
void *get_packet_from_connection_json(struct connection *pc,
                                      enum packet_type *ptype)
{
  int len_read;
  int whole_packet_len;
  struct {
    enum packet_type type;
    int itype;
  } utype;
  struct data_in din;
#ifdef USE_COMPRESSION
  bool compressed_packet = FALSE;
  int header_size = 0;
#endif
  void *data;
  void *(*receive_handler)(struct connection *);
  json_error_t error;
  json_t *pint;

  if (!pc->used) {
    return NULL;		/* connection was closed, stop reading */
  }
  
  if (pc->buffer->ndata < data_type_size(pc->packet_header.length)) {
    /* Not got enough for a length field yet */
    return NULL;
  }

  dio_input_init(&din, pc->buffer->data, pc->buffer->ndata);
  dio_get_uint16_raw(&din, &len_read);

  /* The non-compressed case */
  whole_packet_len = len_read;

#ifdef USE_COMPRESSION
  /* Compression signalling currently assumes a 2-byte packet length; if that
   * changes, the protocol should probably be changed */
  fc_assert(data_type_size(pc->packet_header.length) == 2);
  if (len_read == JUMBO_SIZE) {
    compressed_packet = TRUE;
    header_size = 6;
    if (dio_input_remaining(&din) >= 4) {
      dio_get_uint32(&din, &whole_packet_len);
      log_compress("COMPRESS: got a jumbo packet of size %d",
                   whole_packet_len);
    } else {
      /* to return NULL below */
      whole_packet_len = 6;
    }
  } else if (len_read >= COMPRESSION_BORDER) {
    compressed_packet = TRUE;
    header_size = 2;
    whole_packet_len = len_read - COMPRESSION_BORDER;
    log_compress("COMPRESS: got a normal packet of size %d",
                 whole_packet_len);
  }
#endif /* USE_COMPRESSION */

  if ((unsigned)whole_packet_len > pc->buffer->ndata) {
    return NULL;		/* not all data has been read */
  }

#ifdef USE_COMPRESSION
  if (whole_packet_len < header_size) {
    log_verbose("The packet size is reported to be less than header alone. "
                "The connection will be closed now.");
    connection_close(pc, _("illegal packet size"));

    return NULL;
  }

  if (compressed_packet) {
    uLong compressed_size = whole_packet_len - header_size;
    /* 
     * We don't know the decompressed size. We assume a bad case
     * here: an expansion by an factor of 100. 
     */
    unsigned long int decompressed_size = 100 * compressed_size;
    void *decompressed = fc_malloc(decompressed_size);
    int error;
    struct socket_packet_buffer *buffer = pc->buffer;
    
    error =
	uncompress(decompressed, &decompressed_size,
		   ADD_TO_POINTER(buffer->data, header_size), 
		   compressed_size);
    if (error != Z_OK) {
      log_verbose("Uncompressing of the packet stream failed. "
                  "The connection will be closed now.");
      connection_close(pc, _("decoding error"));
      return NULL;
    }

    buffer->ndata -= whole_packet_len;
    /* 
     * Remove the packet with the compressed data and shift all the
     * remaining data to the front. 
     */
    memmove(buffer->data, buffer->data + whole_packet_len, buffer->ndata);

    if (buffer->ndata + decompressed_size > buffer->nsize) {
      buffer->nsize += decompressed_size;
      buffer->data = fc_realloc(buffer->data, buffer->nsize);
    }

    /*
     * Make place for the uncompressed data by moving the remaining
     * data.
     */
    memmove(buffer->data + decompressed_size, buffer->data, buffer->ndata);

    /* 
     * Copy the uncompressed data.
     */
    memcpy(buffer->data, decompressed, decompressed_size);

    free(decompressed);

    buffer->ndata += decompressed_size;
    
    log_compress("COMPRESS: decompressed %ld into %ld",
                 compressed_size, decompressed_size);

    return get_packet_from_connection(pc, ptype);
  }
#endif /* USE_COMPRESSION */

  /*
   * At this point the packet is a plain uncompressed one. These have
   * to have to be at least the header bytes in size.
   */
  if (whole_packet_len < (data_type_size(pc->packet_header.length))) {
    log_verbose("The packet stream is corrupt. The connection "
                "will be closed now.");
    connection_close(pc, _("decoding error"));
    return NULL;
  }

  /* Parse JSON packet. Note that json string has '\0' */
  pc->json_packet = json_loadb((char*)pc->buffer->data + 2, whole_packet_len - 3, 0, &error);

  /* Log errors before we scrap the data */
  if (!pc->json_packet) {
    log_error("ERROR: Unable to parse packet: %s", pc->buffer->data + 2);
    log_error("%s", error.text);
  }

  log_packet_json("Json in: %s", pc->buffer->data + 2);

  /* Shift remaining data to the front */
  pc->buffer->ndata -= whole_packet_len;
  memmove(pc->buffer->data, pc->buffer->data + whole_packet_len, pc->buffer->ndata);

  if (!pc->json_packet) {
    return NULL;
  }

  pint = json_object_get(pc->json_packet, "pid");

  if (!pint) {
    log_error("ERROR: Unable to get packet type.");
    return NULL;
  } 

  json_int_t packet_type = json_integer_value(pint);
  utype.type = packet_type;

  if (utype.type < 0
      || utype.type >= PACKET_LAST
      || (receive_handler = pc->phs.handlers->receive[utype.type]) == NULL) {
    log_verbose("Received unsupported packet type %d (%s). The connection "
                "will be closed now.",
                utype.type, packet_name(utype.type));
    connection_close(pc, _("unsupported packet type"));
    return NULL;
  }

  log_packet("got packet type=(%s) len=%d from %s",
             packet_name(utype.type), whole_packet_len,
             is_server() ? pc->username : "server");

  *ptype = utype.type;

  if (pc->incoming_packet_notify) {
    pc->incoming_packet_notify(pc, utype.type, whole_packet_len);
  }

#if PACKET_SIZE_STATISTICS 
  {
    static struct {
      int counter;
      int size;
    } packets_stats[PACKET_LAST];
    static int packet_counter = 0;

    int packet_type = utype.itype;
    int size = whole_packet_len;

    if (!packet_counter) {
      int i;

      for (i = 0; i < PACKET_LAST; i++) {
	packets_stats[i].counter = 0;
	packets_stats[i].size = 0;
      }
    }

    packets_stats[packet_type].counter++;
    packets_stats[packet_type].size += size;

    packet_counter++;
    if (packet_counter % 100 == 0) {
      int i, sum = 0;

      log_test("Received packets:");
      for (i = 0; i < PACKET_LAST; i++) {
	if (packets_stats[i].counter == 0)
	  continue;
	sum += packets_stats[i].size;
        log_test("  [%-25.25s %3d]: %6d packets; %8d bytes total; "
                 "%5d bytes/packet average",
                 packet_name(i), i, packets_stats[i].counter,
                 packets_stats[i].size,
                 packets_stats[i].size / packets_stats[i].counter);
      }
      log_test("received %d bytes in %d packets;average size "
               "per packet %d bytes",
               sum, packet_counter, sum / packet_counter);
    }
  }
#endif /* PACKET_SIZE_STATISTICS */
  data = receive_handler(pc);
  if (!data) {
    connection_close(pc, _("incompatible packet contents"));
    return NULL;
  } else {
    return data;
  }
}

#endif /* FREECIV_JSON_CONNECTION */
