/***********************************************************************
 Freeciv - Copyright (C) 2005 - The Freeciv Team
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

/* utility */
#include "shared.h"

/* common */
#include "diptreaty.h"

/* client */
#include "client_main.h"
#include "diplodlg_g.h"

#include "clitreaty.h"

/**********************************************************************//**
  Handle new meeting request.
**************************************************************************/
void client_init_meeting(int counterpart, int initiated_from)
{
  struct treaty *ptreaty;
  struct player *we;
  struct player *they;

  if (!can_client_issue_orders()) {
    return;
  }

  we = client_player();
  they = player_by_number(counterpart);
  ptreaty = find_treaty(we, they);

  if (ptreaty == NULL) {
    ptreaty = fc_malloc(sizeof(*ptreaty));
    init_treaty(ptreaty, we, they);
    treaty_add(ptreaty);
  }

  gui_init_meeting(ptreaty, they, player_by_number(initiated_from));
}

/**********************************************************************//**
  Handle server information about treaty acceptance.
**************************************************************************/
void client_recv_accept_treaty(int counterpart, bool I_accepted,
                               bool other_accepted)
{
  struct treaty *ptreaty;
  struct player *we;
  struct player *they;

  we = client_player();
  they = player_by_number(counterpart);
  ptreaty = find_treaty(we, they);

  if (ptreaty == NULL) {
    return;
  }

  ptreaty->accept0 = I_accepted;
  ptreaty->accept1 = other_accepted;

  gui_recv_accept_treaty(ptreaty, they);
}

/**********************************************************************//**
  Handle server information about meeting cancellation.
**************************************************************************/
void client_recv_cancel_meeting(int counterpart, int initiated_from)
{
  struct treaty *ptreaty;
  struct player *we;
  struct player *they;

  we = client_player();
  they = player_by_number(counterpart);
  ptreaty = find_treaty(we, they);

  if (ptreaty == NULL) {
    return;
  }

  gui_recv_cancel_meeting(ptreaty, they, player_by_number(initiated_from));

  treaty_remove(ptreaty);
}

/**********************************************************************//**
  Handle server information about addition of a clause
**************************************************************************/
void client_recv_create_clause(int counterpart, int giver,
                               enum clause_type type, int value)
{
  struct treaty *ptreaty;
  struct player *we;
  struct player *they;

  we = client_player();
  they = player_by_number(counterpart);
  ptreaty = find_treaty(we, they);

  if (ptreaty == NULL) {
    return;
  }

  gui_prepare_clause_updt(ptreaty, they);

  add_clause(ptreaty, player_by_number(giver), type, value,
             client_player());

  gui_recv_create_clause(ptreaty, they);
}

/**********************************************************************//**
  Handle server information about removal of a clause
**************************************************************************/
void client_recv_remove_clause(int counterpart, int giver,
                               enum clause_type type, int value)
{
  struct treaty *ptreaty;
  struct player *we;
  struct player *they;

  we = client_player();
  they = player_by_number(counterpart);
  ptreaty = find_treaty(we, they);

  if (ptreaty == NULL) {
    return;
  }

  gui_prepare_clause_updt(ptreaty, they);

  remove_clause(ptreaty, player_by_number(giver), type, value);

  gui_recv_remove_clause(ptreaty, they);
}
