/***********************************************************************
 Freeciv - Copyright (C) 1996 - The Freeciv Project

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2, or (at your option)
 any later version.
***********************************************************************/

#ifndef FC__AGENT_IPC_CODEC_H
#define FC__AGENT_IPC_CODEC_H

#include <stddef.h>
#include <stdint.h>

/* utility */
#include "support.h"

#define FC_AGENT_IPC_MAX_PAYLOAD 8192
#define FC_AGENT_IPC_QUEUE_DEPTH 32

enum fc_agent_ipc_read_result {
  FC_AGENT_IPC_READ_MESSAGE,
  FC_AGENT_IPC_READ_AGAIN,
  FC_AGENT_IPC_READ_EOF,
  FC_AGENT_IPC_READ_ERROR,
  FC_AGENT_IPC_READ_PROTOCOL_ERROR
};

struct fc_agent_ipc_frame {
  size_t length;
  size_t offset;
  uint8_t bytes[4 + FC_AGENT_IPC_MAX_PAYLOAD];
};

struct fc_agent_ipc {
  int fd;
  bool initialized;
  uint8_t header[4];
  size_t header_used;
  size_t payload_length;
  size_t payload_used;
  char payload[FC_AGENT_IPC_MAX_PAYLOAD + 1];
  struct fc_agent_ipc_frame output[FC_AGENT_IPC_QUEUE_DEPTH];
  size_t output_head;
  size_t output_count;
};

bool fc_agent_ipc_validate_fd(int fd, char *error, size_t error_size);
bool fc_agent_ipc_init(struct fc_agent_ipc *ipc, int fd,
                       char *error, size_t error_size);
void fc_agent_ipc_close(struct fc_agent_ipc *ipc);

enum fc_agent_ipc_read_result
fc_agent_ipc_read(struct fc_agent_ipc *ipc, const char **payload,
                  size_t *payload_length);

bool fc_agent_ipc_queue(struct fc_agent_ipc *ipc, const char *payload,
                        size_t payload_length);
bool fc_agent_ipc_flush(struct fc_agent_ipc *ipc);
bool fc_agent_ipc_wants_write(const struct fc_agent_ipc *ipc);

bool fc_agent_ipc_valid_utf8(const char *payload, size_t payload_length);

#endif /* FC__AGENT_IPC_CODEC_H */
