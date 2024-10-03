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
#ifndef FC__DIPLHAND_H
#define FC__DIPLHAND_H

/* common */
#include "fc_types.h"

/* server */
#include <hand_gen.h>       /* <> so looked from the build directory first. */

struct packet_diplomacy_info;
struct connection;

void establish_embassy(struct player *pplayer, struct player *aplayer);

void send_diplomatic_meetings(struct connection *dest);
void cancel_all_meetings(struct player *pplayer);
void reject_all_treaties(struct player *pplayer);

void set_diplstate_type(struct player_diplstate *state1,
                        struct player_diplstate *state2,
                        enum diplstate_type type);

enum diplstate_type valid_dst_closest(struct player_diplstate *dst);

#endif /* FC__DIPLHAND_H */
