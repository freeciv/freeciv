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

/* utility */
#include "capability.h"
#include "fcintl.h"
#include "log.h"
#include "mem.h"
#include "support.h"

/* commmon */
#include "dataio.h"
#include "game.h"
#include "events.h"
#include "map.h"

#include "packets.h"

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
 * All compressed packets over this size are sent as a jumbo packet.
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

/********************************************************************** 
 The current packet functions don't handle signed values
 correct. This will probably lead to problems when compiling
 freeciv for a platform which has 64 bit ints. Also 16 and 8
 bits values cannot be signed without using tricks (look in e.g
 receive_packet_city_info() )

  TODO to solve these problems:
    o use the new signed functions where they are necessary
    o change the prototypes of the unsigned functions
       to unsigned int instead of int (but only on the 32
       bit functions, because otherwhere it's not necessary)

  Possibe enhancements:
    o check in configure for ints with size others than 4
    o write real put functions and check for the limit
***********************************************************************/

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

  error = compress2(compressed, &compressed_size,
                    pconn->compression.queue.p,
                    pconn->compression.queue.size,
                    compression_level);
  fc_assert_ret_val(error == Z_OK, FALSE);
  if (compressed_size + 2 < pconn->compression.queue.size) {
    struct data_out dout;

    log_compress("COMPRESS: compressed %lu bytes to %ld (level %d)",
                 (unsigned long) pconn->compression.queue.size,
                 compressed_size, compression_level);
    stat_size_uncompressed += pconn->compression.queue.size;
    stat_size_compressed += compressed_size;

    if (compressed_size <= JUMBO_BORDER) {
      unsigned char header[2];

      log_compress("COMPRESS: sending %ld as normal", compressed_size);

      dio_output_init(&dout, header, sizeof(header));
      dio_put_uint16(&dout, 2 + compressed_size + COMPRESSION_BORDER);
      connection_send_data(pconn, header, sizeof(header));
      connection_send_data(pconn, compressed, compressed_size);
    } else {
      unsigned char header[6];

      log_compress("COMPRESS: sending %ld as jumbo", compressed_size);
      dio_output_init(&dout, header, sizeof(header));
      dio_put_uint16(&dout, JUMBO_SIZE);
      dio_put_uint32(&dout, 6 + compressed_size);
      connection_send_data(pconn, header, sizeof(header));
      connection_send_data(pconn, compressed, compressed_size);
    }
  } else {
    log_compress("COMPRESS: would enlarging %lu bytes to %ld; "
                 "sending uncompressed",
                 (unsigned long) pconn->compression.queue.size,
                 compressed_size);
    connection_send_data(pconn, pconn->compression.queue.p,
                         pconn->compression.queue.size);
    stat_size_no_compression += pconn->compression.queue.size;
  }
  return pconn->used;
}
#endif /* USE_COMPRESSION */

/****************************************************************************
  Thaw the connection. Then maybe compress the data waiting to send them
  to the connection. Returns TRUE on success. See also
  conn_compression_freeze().
****************************************************************************/
bool conn_compression_thaw(struct connection *pconn)
{
#ifdef USE_COMPRESSION
  pconn->compression.frozen_level--;
  if (0 == pconn->compression.frozen_level) {
    return conn_compression_flush(pconn);
  }
#endif /* USE_COMPRESSION */
  return pconn->used;
}


/**************************************************************************
  It returns the request id of the outgoing packet (or 0 if is_server()).
**************************************************************************/
int send_packet_data(struct connection *pc, unsigned char *data, int len)
{
  /* default for the server */
  int result = 0;
  int packet_type = ntohs((data[3] << 8) + data[2]);

  log_packet("sending packet type=%s(%d) len=%d to %s",
             packet_name(packet_type), packet_type, len,
             is_server() ? pc->username : "server");

  if (!is_server()) {
    pc->client.last_request_id_used =
        get_next_request_id(pc->client.last_request_id_used);
    result = pc->client.last_request_id_used;
    log_packet("sending request %d", result);
  }

  if (pc->outgoing_packet_notify) {
    pc->outgoing_packet_notify(pc, packet_type, len, result);
  }

#ifdef USE_COMPRESSION
  if (TRUE) {
    int size = len;

    if (conn_compression_frozen(pc)) {
      size_t old_size = pc->compression.queue.size;

      byte_vector_reserve(&pc->compression.queue, old_size + len);
      memcpy(pc->compression.queue.p + old_size, data, len);
      log_compress2("COMPRESS: putting %s into the queue",
                    packet_name(packet_type));
      if (MAX_LEN_BUFFER < byte_vector_size(&pc->compression.queue)) {
        log_compress2("COMPRESS: huge queue, forcing to flush (%lu/%lu)",
                      (long unsigned)
                      byte_vector_size(&pc->compression.queue),
                      (long unsigned) MAX_LEN_BUFFER);
        if (!conn_compression_flush(pc)) {
          return -1;
        }
        byte_vector_reserve(&pc->compression.queue, 0);
      }
    } else {
      stat_size_alone += size;
      log_compress("COMPRESS: sending %s alone (%d bytes total)",
                   packet_name(packet_type), stat_size_alone);
      connection_send_data(pc, data, len);
    }

    log_compress2("COMPRESS: STATS: alone=%d compression-expand=%d "
                  "compression (before/after) = %d/%d",
                  stat_size_alone, stat_size_no_compression,
                  stat_size_uncompressed, stat_size_compressed);
  }
#else  /* USE_COMPRESSION */
  connection_send_data(pc, data, len);
#endif /* USE_COMPRESSION */

#if PACKET_SIZE_STATISTICS
  {
    static struct {
      int counter;
      int size;
    } packets_stats[PACKET_LAST];
    static int packet_counter = 0;
    static int last_start_turn_seen = -1;
    static bool start_turn_seen = FALSE;

    int size = len;
    bool print = FALSE;
    bool clear = FALSE;

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
    if (packet_type == PACKET_START_TURN
	&& last_start_turn_seen != game.turn) {
	start_turn_seen=TRUE;
      last_start_turn_seen = game.turn;
    }

    if ((packet_type ==
	 PACKET_PROCESSING_FINISHED || packet_type == PACKET_THAW_HINT)
	&& start_turn_seen) {
      start_turn_seen = FALSE;
      print = TRUE;
      clear = TRUE;
    }

    if(print) {
      int i, sum = 0;
#define log_ll log_debug

#if PACKET_SIZE_STATISTICS == 2
      delta_stats_report();
#endif
      log_ll("Transmitted packets:");
      log_ll("%8s %8s %8s %s", "Packets", "Bytes", "Byt/Pac", "Name");

      for (i = 0; i < PACKET_LAST; i++) {
	if (packets_stats[i].counter == 0) {
	  continue;
	}
	sum += packets_stats[i].size;
        log_ll("%8d %8d %8d %s(%i)",
               packets_stats[i].counter, packets_stats[i].size,
               packets_stats[i].size / packets_stats[i].counter,
               packet_name(i),i);
      }
      log_test("turn=%d; transmitted %d bytes in %d packets;average size "
               "per packet %d bytes", game.turn, sum, packet_counter,
               sum / packet_counter);
      log_test("turn=%d; transmitted %d bytes", game.turn,
               pc->statistics.bytes_send);
    }
    if (clear) {
      int i;

      for (i = 0; i < PACKET_LAST; i++) {
	packets_stats[i].counter = 0;
	packets_stats[i].size = 0;
      }
      packet_counter = 0;
      pc->statistics.bytes_send = 0;
      delta_stats_reset();
    }
  }
#undef log_ll
#endif /* PACKET_SIZE_STATISTICS */

  return result;
}

/**************************************************************************
presult indicates if there is more packets in the cache. We return result
instead of just testing if the returning package is NULL as we sometimes
return a NULL packet even if everything is OK (receive_packet_goto_route).
**************************************************************************/
void *get_packet_from_connection(struct connection *pc,
				 enum packet_type *ptype, bool *presult)
{
  int len_read;
  int whole_packet_len;
  struct {
    enum packet_type type;
    int itype;
  } utype;
  int typeb1, typeb2;
  struct data_in din;
#ifdef USE_COMPRESSION
  bool compressed_packet = FALSE;
#endif
  int header_size = 4;

  *presult = FALSE;

  if (!pc->used) {
    return NULL;		/* connection was closed, stop reading */
  }
  
  if (pc->buffer->ndata < 2+2) {
    /* length and type not read */
    return NULL;
  }

  dio_input_init(&din, pc->buffer->data, pc->buffer->ndata);
  dio_get_uint16(&din, &len_read);

  /* The non-compressed case */
  whole_packet_len = len_read;

#ifdef USE_COMPRESSION
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

  if (whole_packet_len < header_size) {
    log_verbose("The packet size is reported to be less than header alone. "
                "The connection will be closed now.");
    connection_close(pc, _("illegal packet size"));

    return NULL;
  }

#ifdef USE_COMPRESSION
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

    return get_packet_from_connection(pc, ptype, presult);
  }
#endif /* USE_COMPRESSION */

  /*
   * At this point the packet is a plain uncompressed one. These have
   * to have to be at least 4 bytes in size.
   */
  if (whole_packet_len < 2+2) {
    log_verbose("The packet stream is corrupt. The connection "
                "will be closed now.");
    connection_close(pc, _("decoding error"));
    return NULL;
  }

  /* Instead of one dio_get_uint16() we do twice dio_get_uint8().
   * Older (<= 2.4) versions had 8bit type field, and we detect
   * here if this is initial PACKET_SERVER_JOIN_REQ from such a client. */
  dio_get_uint8(&din, &typeb1);

  if (typeb1 == PACKET_SERVER_JOIN_REQ) {
    utype.itype = typeb1;
  } else {
    dio_get_uint8(&din, &typeb2);
    utype.itype = ntohs((typeb2 << 8) + typeb1);
  }

  utype.type = utype.itype;

  log_packet("got packet type=(%s)%d len=%d from %s",
             packet_name(utype.type), utype.itype, whole_packet_len,
             is_server() ? pc->username : "server");

  *ptype = utype.type;
  *presult = TRUE;

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
  return get_packet_from_connection_helper(pc, utype.type);
}

/**************************************************************************
  Remove the packet from the buffer
**************************************************************************/
void remove_packet_from_buffer(struct socket_packet_buffer *buffer)
{
  struct data_in din;
  int len;

  dio_input_init(&din, buffer->data, buffer->ndata);
  dio_get_uint16(&din, &len);
  memmove(buffer->data, buffer->data + len, buffer->ndata - len);
  buffer->ndata -= len;
  log_debug("remove_packet_from_buffer: remove %d; remaining %d",
            len, buffer->ndata);
}

/**************************************************************************
  Sanity check packet
**************************************************************************/
void check_packet(struct data_in *din, struct connection *pc)
{
  size_t rem = dio_input_remaining(din);

  if (din->bad_string || din->bad_bit_string || rem != 0) {
    char from[MAX_LEN_ADDR + MAX_LEN_NAME + 128];
    int type, len;

    fc_assert_ret(pc != NULL);
    fc_snprintf(from, sizeof(from), " from %s", conn_description(pc));

    dio_input_rewind(din);
    dio_get_uint16(din, &len);
    dio_get_uint16(din, &type);

    if (din->bad_string) {
      log_error("received bad string in packet (type %d, len %d)%s",
                type, len, from);
    }

    if (din->bad_bit_string) {
      log_error("received bad bit string in packet (type %d, len %d)%s",
                type, len, from);
    }

    if (din->too_short) {
      log_error("received short packet (type %d, len %d)%s",
                type, len, from);
    }

    if (rem > 0) {
      /* This may be ok, eg a packet from a newer version with extra info
       * which we should just ignore */
      log_verbose("received long packet (type %d, len %d, rem %lu)%s", type,
                  len, (unsigned long)rem, from);
    }
  }
}

/**************************************************************************
 Updates pplayer->attribute_block according to the given packet.
**************************************************************************/
void generic_handle_player_attribute_chunk(struct player *pplayer,
					   const struct
					   packet_player_attribute_chunk
					   *chunk)
{
  log_packet("received attribute chunk %u/%u %u",
             (unsigned int) chunk->offset,
             (unsigned int) chunk->total_length,
             (unsigned int) chunk->chunk_length);

  if (chunk->total_length < 0
      || chunk->chunk_length < 0
      || chunk->total_length >= MAX_ATTRIBUTE_BLOCK
      || chunk->offset < 0
      || chunk->offset > chunk->total_length /* necessary check on 32 bit systems */
      || chunk->chunk_length > chunk->total_length
      || chunk->offset + chunk->chunk_length > chunk->total_length
      || (chunk->offset != 0
          && chunk->total_length != pplayer->attribute_block_buffer.length)) {
    /* wrong attribute data */
    if (pplayer->attribute_block_buffer.data) {
      free(pplayer->attribute_block_buffer.data);
      pplayer->attribute_block_buffer.data = NULL;
    }
    pplayer->attribute_block_buffer.length = 0;
    log_error("Received wrong attribute chunk");
    return;
  }
  /* first one in a row */
  if (chunk->offset == 0) {
    if (pplayer->attribute_block_buffer.data) {
      free(pplayer->attribute_block_buffer.data);
      pplayer->attribute_block_buffer.data = NULL;
    }
    pplayer->attribute_block_buffer.data = fc_malloc(chunk->total_length);
    pplayer->attribute_block_buffer.length = chunk->total_length;
  }
  memcpy((char *) (pplayer->attribute_block_buffer.data) + chunk->offset,
	 chunk->data, chunk->chunk_length);
  
  if (chunk->offset + chunk->chunk_length == chunk->total_length) {
    /* Received full attribute block */
    if (pplayer->attribute_block.data != NULL) {
      free(pplayer->attribute_block.data);
    }
    pplayer->attribute_block.data = pplayer->attribute_block_buffer.data;
    pplayer->attribute_block.length = pplayer->attribute_block_buffer.length;
    
    pplayer->attribute_block_buffer.data = NULL;
    pplayer->attribute_block_buffer.length = 0;
  }
}

/**************************************************************************
 Split the attribute block into chunks and send them over pconn.
**************************************************************************/
void send_attribute_block(const struct player *pplayer,
			  struct connection *pconn)
{
  struct packet_player_attribute_chunk packet;
  int current_chunk, chunks, bytes_left;

  if (!pplayer || !pplayer->attribute_block.data) {
    return;
  }

  fc_assert_ret(pplayer->attribute_block.length > 0
                && pplayer->attribute_block.length < MAX_ATTRIBUTE_BLOCK);

  chunks =
      (pplayer->attribute_block.length - 1) / ATTRIBUTE_CHUNK_SIZE + 1;
  bytes_left = pplayer->attribute_block.length;

  connection_do_buffer(pconn);

  for (current_chunk = 0; current_chunk < chunks; current_chunk++) {
    int size_of_current_chunk = MIN(bytes_left, ATTRIBUTE_CHUNK_SIZE);

    packet.offset = ATTRIBUTE_CHUNK_SIZE * current_chunk;
    packet.total_length = pplayer->attribute_block.length;
    packet.chunk_length = size_of_current_chunk;

    memcpy(packet.data,
	   (char *) (pplayer->attribute_block.data) + packet.offset,
	   packet.chunk_length);
    bytes_left -= packet.chunk_length;

    if (packet.chunk_length < ATTRIBUTE_CHUNK_SIZE) {
      /* Last chunk is not full. Make sure that delta does
       * not use random data. */
      memset(packet.data + packet.chunk_length, 0,
             ATTRIBUTE_CHUNK_SIZE - packet.chunk_length);
    }

    send_packet_player_attribute_chunk(pconn, &packet);
  }

  connection_do_unbuffer(pconn);
}

/**************************************************************************
  Test and log for sending player attribute_block
**************************************************************************/
void pre_send_packet_player_attribute_chunk(struct connection *pc,
                                            struct packet_player_attribute_chunk
                                            *packet)
{
  fc_assert(packet->total_length > 0
            && packet->total_length < MAX_ATTRIBUTE_BLOCK);
  /* 500 bytes header, just to be sure */
  fc_assert(packet->chunk_length > 0
            && packet->chunk_length < MAX_LEN_PACKET - 500);
  fc_assert(packet->chunk_length <= packet->total_length);
  fc_assert(packet->offset >= 0 && packet->offset < packet->total_length);

  log_packet("sending attribute chunk %d/%d %d",
             packet->offset, packet->total_length, packet->chunk_length);

}
