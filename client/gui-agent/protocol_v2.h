/***********************************************************************
 Freeciv - Copyright (C) 1996 - The Freeciv Project

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2, or (at your option)
 any later version.
***********************************************************************/

#ifndef FC__AGENT_PROTOCOL_V2_H
#define FC__AGENT_PROTOCOL_V2_H

#include <stddef.h>

/* utility */
#include "support.h"

typedef bool (*fc_agent_v2_emit_fn)(const char *message);
typedef bool (*fc_agent_v2_authorized_fn)(void);

void fc_agent_v2_init(fc_agent_v2_emit_fn emit,
                      fc_agent_v2_authorized_fn authorized);
void fc_agent_v2_reset(void);
void fc_agent_v2_advertise(void);
void fc_agent_v2_tick(void);

/* Returns TRUE when PAYLOAD is a protocol-2 command, including malformed
 * instances for which a deterministic ERR frame has been emitted. */
bool fc_agent_v2_handle(const char *payload, size_t length);

/* Agent-GUI treaty callbacks.  These are cache notifications only; packet
 * handling remains in the shared normal client. */
void fc_agent_v2_diplomacy_meeting_opened(int counterpart);
void fc_agent_v2_diplomacy_meeting_closed(int counterpart);
void fc_agent_v2_diplomacy_clause_changed(int counterpart);
void fc_agent_v2_diplomacy_acceptance_changed(int counterpart);

#endif /* FC__AGENT_PROTOCOL_V2_H */
