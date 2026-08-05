/***********************************************************************
 Freeciv - Copyright (C) 1996 - The Freeciv Project

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2, or (at your option)
 any later version.
***********************************************************************/

#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

/* utility */
#include "support.h"

#include "ipc_codec.h"

static bool ipc_payload_bytes_allowed(const char *payload, size_t length)
{
  size_t i;

  for (i = 0; i < length; i++) {
    unsigned char value = (unsigned char) payload[i];

    if (value == '\0' || value == '\n' || value == '\r'
        || value == 0x7f || (value < 0x20 && value != '\t')) {
      return FALSE;
    }
  }

  return TRUE;
}

/**********************************************************************//**
  Validate a complete UTF-8 string. Overlong sequences, surrogates and
  values outside Unicode are rejected.
**************************************************************************/
bool fc_agent_ipc_valid_utf8(const char *payload, size_t length)
{
  size_t i = 0;

  while (i < length) {
    const unsigned char first = (unsigned char) payload[i];
    uint32_t value;
    size_t count;
    size_t j;

    if (first <= 0x7f) {
      i++;
      continue;
    } else if (first >= 0xc2 && first <= 0xdf) {
      value = first & 0x1f;
      count = 1;
    } else if (first >= 0xe0 && first <= 0xef) {
      value = first & 0x0f;
      count = 2;
    } else if (first >= 0xf0 && first <= 0xf4) {
      value = first & 0x07;
      count = 3;
    } else {
      return FALSE;
    }

    if (length - i - 1 < count) {
      return FALSE;
    }

    for (j = 1; j <= count; j++) {
      const unsigned char next = (unsigned char) payload[i + j];

      if ((next & 0xc0) != 0x80) {
        return FALSE;
      }
      value = (value << 6) | (next & 0x3f);
    }

    if ((count == 2 && value < 0x800)
        || (count == 3 && value < 0x10000)
        || value > 0x10ffff
        || (value >= 0xd800 && value <= 0xdfff)) {
      return FALSE;
    }

    i += count + 1;
  }

  return TRUE;
}

/**********************************************************************//**
  Accept only a connected, full-duplex AF_UNIX stream socket.
**************************************************************************/
bool fc_agent_ipc_validate_fd(int fd, char *error, size_t error_size)
{
  int flags;
  int socket_type;
  int socket_error;
  struct sockaddr_storage local_address;
  struct sockaddr_storage peer_address;
  socklen_t value_length;

  if (fd < 3 || fd >= FD_SETSIZE) {
    fc_strlcpy(error, "IPC descriptor is outside the supported range",
               error_size);
    return FALSE;
  }

  flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0 || fcntl(fd, F_GETFD, 0) < 0) {
    fc_strlcpy(error, "IPC descriptor is not open", error_size);
    return FALSE;
  }
  if ((flags & O_ACCMODE) != O_RDWR) {
    fc_strlcpy(error, "IPC descriptor is not full-duplex", error_size);
    return FALSE;
  }

  value_length = sizeof(socket_type);
  if (getsockopt(fd, SOL_SOCKET, SO_TYPE, &socket_type, &value_length) < 0
      || socket_type != SOCK_STREAM) {
    fc_strlcpy(error, "IPC descriptor is not a stream socket", error_size);
    return FALSE;
  }

  value_length = sizeof(local_address);
  if (getsockname(fd, (struct sockaddr *) &local_address, &value_length) < 0
      || local_address.ss_family != AF_UNIX) {
    fc_strlcpy(error, "IPC descriptor is not an AF_UNIX socket", error_size);
    return FALSE;
  }
  value_length = sizeof(peer_address);
  if (getpeername(fd, (struct sockaddr *) &peer_address, &value_length) < 0
      || peer_address.ss_family != AF_UNIX) {
    fc_strlcpy(error, "IPC descriptor is not connected", error_size);
    return FALSE;
  }

  value_length = sizeof(socket_error);
  if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &socket_error, &value_length) < 0
      || socket_error != 0) {
    fc_strlcpy(error, "IPC socket has a pending error", error_size);
    return FALSE;
  }

  return TRUE;
}

/**********************************************************************//**
  Initialize the private inherited descriptor and make it non-blocking and
  close-on-exec after client startup has inherited it.
**************************************************************************/
bool fc_agent_ipc_init(struct fc_agent_ipc *ipc, int fd,
                       char *error, size_t error_size)
{
  int flags;
  int descriptor_flags;

  memset(ipc, 0, sizeof(*ipc));
  ipc->fd = -1;

  if (!fc_agent_ipc_validate_fd(fd, error, error_size)) {
    return FALSE;
  }

  flags = fcntl(fd, F_GETFL, 0);
  if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
    fc_strlcpy(error, "IPC descriptor could not be made non-blocking",
               error_size);
    return FALSE;
  }
  descriptor_flags = fcntl(fd, F_GETFD, 0);
  if (descriptor_flags < 0
      || fcntl(fd, F_SETFD, descriptor_flags | FD_CLOEXEC) < 0) {
    fc_strlcpy(error, "IPC descriptor could not be made close-on-exec",
               error_size);
    return FALSE;
  }

#ifdef SO_NOSIGPIPE
  {
    int enabled = 1;

    if (setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE,
                   &enabled, sizeof(enabled)) < 0) {
      fc_strlcpy(error, "IPC descriptor could not disable SIGPIPE",
                 error_size);
      return FALSE;
    }
  }
#endif /* SO_NOSIGPIPE */

  ipc->fd = fd;
  ipc->initialized = TRUE;
  return TRUE;
}

/**********************************************************************//**
  Close the private IPC descriptor.
**************************************************************************/
void fc_agent_ipc_close(struct fc_agent_ipc *ipc)
{
  if (ipc->initialized) {
    close(ipc->fd);
  }
  ipc->fd = -1;
  ipc->initialized = FALSE;
}

static enum fc_agent_ipc_read_result
ipc_read_part(int fd, void *buffer, size_t *used, size_t wanted)
{
  ssize_t result;

  while (*used < wanted) {
    result = read(fd, ((uint8_t *) buffer) + *used, wanted - *used);
    if (result > 0) {
      *used += (size_t) result;
      continue;
    }
    if (result == 0) {
      return FC_AGENT_IPC_READ_EOF;
    }
    if (errno == EINTR) {
      continue;
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      return FC_AGENT_IPC_READ_AGAIN;
    }
    return FC_AGENT_IPC_READ_ERROR;
  }

  return FC_AGENT_IPC_READ_MESSAGE;
}

/**********************************************************************//**
  Read one bounded 4-byte-big-endian frame. Partial input is retained.
**************************************************************************/
enum fc_agent_ipc_read_result
fc_agent_ipc_read(struct fc_agent_ipc *ipc, const char **payload,
                  size_t *payload_length)
{
  enum fc_agent_ipc_read_result result;

  result = ipc_read_part(ipc->fd, ipc->header, &ipc->header_used,
                         sizeof(ipc->header));
  if (result != FC_AGENT_IPC_READ_MESSAGE) {
    if (result == FC_AGENT_IPC_READ_EOF && ipc->header_used != 0) {
      return FC_AGENT_IPC_READ_PROTOCOL_ERROR;
    }
    return result;
  }

  if (ipc->payload_length == 0) {
    ipc->payload_length = ((size_t) ipc->header[0] << 24)
                          | ((size_t) ipc->header[1] << 16)
                          | ((size_t) ipc->header[2] << 8)
                          | (size_t) ipc->header[3];
    if (ipc->payload_length == 0
        || ipc->payload_length > FC_AGENT_IPC_MAX_PAYLOAD) {
      return FC_AGENT_IPC_READ_PROTOCOL_ERROR;
    }
  }

  result = ipc_read_part(ipc->fd, ipc->payload, &ipc->payload_used,
                         ipc->payload_length);
  if (result != FC_AGENT_IPC_READ_MESSAGE) {
    if (result == FC_AGENT_IPC_READ_EOF) {
      return FC_AGENT_IPC_READ_PROTOCOL_ERROR;
    }
    return result;
  }

  ipc->payload[ipc->payload_length] = '\0';
  if (!fc_agent_ipc_valid_utf8(ipc->payload, ipc->payload_length)
      || !ipc_payload_bytes_allowed(ipc->payload, ipc->payload_length)) {
    return FC_AGENT_IPC_READ_PROTOCOL_ERROR;
  }

  *payload = ipc->payload;
  *payload_length = ipc->payload_length;

  ipc->header_used = 0;
  ipc->payload_length = 0;
  ipc->payload_used = 0;

  return FC_AGENT_IPC_READ_MESSAGE;
}

/**********************************************************************//**
  Queue one complete outbound frame without blocking the Freeciv loop.
**************************************************************************/
bool fc_agent_ipc_queue(struct fc_agent_ipc *ipc, const char *payload,
                        size_t payload_length)
{
  struct fc_agent_ipc_frame *frame;
  size_t index;

  if (!ipc->initialized || payload_length == 0
      || payload_length > FC_AGENT_IPC_MAX_PAYLOAD
      || ipc->output_count == FC_AGENT_IPC_QUEUE_DEPTH
      || !fc_agent_ipc_valid_utf8(payload, payload_length)
      || !ipc_payload_bytes_allowed(payload, payload_length)) {
    return FALSE;
  }

  index = (ipc->output_head + ipc->output_count)
          % FC_AGENT_IPC_QUEUE_DEPTH;
  frame = &ipc->output[index];
  frame->bytes[0] = (uint8_t) ((payload_length >> 24) & 0xff);
  frame->bytes[1] = (uint8_t) ((payload_length >> 16) & 0xff);
  frame->bytes[2] = (uint8_t) ((payload_length >> 8) & 0xff);
  frame->bytes[3] = (uint8_t) (payload_length & 0xff);
  memcpy(frame->bytes + 4, payload, payload_length);
  frame->length = 4 + payload_length;
  frame->offset = 0;
  ipc->output_count++;

  return TRUE;
}

/**********************************************************************//**
  Flush as many queued bytes as the non-blocking descriptor accepts.
**************************************************************************/
bool fc_agent_ipc_flush(struct fc_agent_ipc *ipc)
{
  while (ipc->output_count > 0) {
    struct fc_agent_ipc_frame *frame = &ipc->output[ipc->output_head];
    ssize_t result;

#ifdef MSG_NOSIGNAL
    result = send(ipc->fd, frame->bytes + frame->offset,
                  frame->length - frame->offset, MSG_NOSIGNAL);
#else
    result = write(ipc->fd, frame->bytes + frame->offset,
                   frame->length - frame->offset);
#endif
    if (result > 0) {
      frame->offset += (size_t) result;
      if (frame->offset == frame->length) {
        ipc->output_head = (ipc->output_head + 1)
                           % FC_AGENT_IPC_QUEUE_DEPTH;
        ipc->output_count--;
      }
      continue;
    }
    if (result < 0 && errno == EINTR) {
      continue;
    }
    if (result < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      return TRUE;
    }
    return FALSE;
  }

  return TRUE;
}

bool fc_agent_ipc_wants_write(const struct fc_agent_ipc *ipc)
{
  return ipc->output_count > 0;
}
