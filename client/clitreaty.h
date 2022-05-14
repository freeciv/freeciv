/***********************************************************************
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
#ifndef FC__CLITREATY_H
#define FC__CLITREATY_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

void client_init_meeting(int counterpart, int initiated_from);
void client_recv_accept_treaty(int counterpart, bool I_accepted,
                               bool other_accepted);
void client_recv_cancel_meeting(int counterpart, int initiated_from);
void client_recv_create_clause(int counterpart, int giver,
                               enum clause_type type, int value);
void client_recv_remove_clause(int counterpart, int giver,
                               enum clause_type type, int value);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* FC__CLITREATY_H */
