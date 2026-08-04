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

/* common */
#include "diptreaty.h"
#include "player.h"

/* gui main header */
#include "gui_agent.h"

#include "diplodlg_g.h"
#include "protocol_v2.h"

void gui_gui_recv_accept_treaty(struct treaty *ptreaty,
                                struct player *they)
{
  fc_agent_v2_diplomacy_acceptance_changed(player_number(they));
  (void) ptreaty;
}

void gui_gui_init_meeting(struct treaty *ptreaty, struct player *they,
                          struct player *initiator)
{
  fc_agent_v2_diplomacy_meeting_opened(player_number(they));
  (void) ptreaty;
  (void) initiator;
}

void gui_gui_recv_create_clause(struct treaty *ptreaty,
                                struct player *they)
{
  fc_agent_v2_diplomacy_clause_changed(player_number(they));
  (void) ptreaty;
}

void gui_gui_recv_cancel_meeting(struct treaty *ptreaty,
                                 struct player *they,
                                 struct player *initiator)
{
  fc_agent_v2_diplomacy_meeting_closed(player_number(they));
  (void) ptreaty;
  (void) initiator;
}

void gui_gui_recv_remove_clause(struct treaty *ptreaty,
                                struct player *they)
{
  fc_agent_v2_diplomacy_clause_changed(player_number(they));
  (void) ptreaty;
}

void gui_gui_prepare_clause_updt(struct treaty *ptreaty,
                                 struct player *they)
{
  (void) ptreaty;
  (void) they;
}

void close_all_diplomacy_dialogs(void)
{
  /* The shared client treaty cache is torn down by client lifecycle code. */
}
