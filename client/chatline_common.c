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
#include <config.h>
#endif

#include <assert.h>
#include <string.h>

#include "astring.h"
#include "log.h"
#include "packets.h"
#include "support.h"

#include "chatline_g.h"

#include "chatline_common.h"
#include "clinet.h"

/* Stored up buffer of lines for the chatline */
struct remaining {
  char *text;
  int conn_id;
};
#define SPECLIST_TAG remaining
#include "speclist.h"
#define remaining_list_iterate(rlist, pline) \
  TYPED_LIST_ITERATE(struct remaining, rlist, pline)
#define remaining_list_iterate_end LIST_ITERATE_END

static struct remaining_list remains;

/**************************************************************************
  Initialize data structures.
**************************************************************************/
void chatline_common_init(void)
{
  remaining_list_init(&remains);
}

/**************************************************************************
  Send the message as a chat to the server.
**************************************************************************/
void send_chat(const char *message)
{
  dsend_packet_chat_msg_req(&aconnection, message);
}

static int frozen_level = 0;

/**************************************************************************
  Turn on buffering, using a counter so that calls may be nested.
**************************************************************************/
void output_window_freeze()
{
  frozen_level++;

  if (frozen_level == 1) {
    assert(remaining_list_size(&remains) == 0);
  }
}

/**************************************************************************
  Turn off buffering if internal counter of number of times buffering
  was turned on falls to zero, to handle nested freeze/thaw pairs.
  When counter is zero, append the picked up data.
**************************************************************************/
void output_window_thaw()
{
  frozen_level--;
  assert(frozen_level >= 0);

  if (frozen_level == 0) {
    remaining_list_iterate(remains, pline) {
      append_output_window_full(pline->text, pline->conn_id);
      free(pline->text);
      free(pline);
    } remaining_list_iterate_end;
    remaining_list_unlink_all(&remains);
  }
}

/**************************************************************************
  Turn off buffering and append the picked up data.
**************************************************************************/
void output_window_force_thaw()
{
  if (frozen_level > 0) {
    frozen_level = 1;
    output_window_thaw();
  }
}

/**************************************************************************
  Add a line of text to the output ("chatline") window.
**************************************************************************/
void append_output_window(const char *astring)
{
  append_output_window_full(astring, -1);
}

/**************************************************************************
  Same as above, but here we know the connection id of the sender of the
  text in question.
**************************************************************************/
void append_output_window_full(const char *astring, int conn_id)
{
  if (frozen_level == 0) {
    real_append_output_window(astring, conn_id);
  } else {
    struct remaining *premain = fc_malloc(sizeof(*premain));

    remaining_list_insert_back(&remains, premain);
    premain->text = mystrdup(astring);
    premain->conn_id = conn_id;
  }
}
