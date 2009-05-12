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
#include <stdarg.h>
#include <string.h>

/* utility */
#include "astring.h"
#include "fcintl.h"
#include "log.h"
#include "support.h"

/* common */
#include "packets.h"

#include "chatline_g.h"

/* client */
#include "chatline_common.h"
#include "client_main.h"


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

static struct remaining_list *remains;

/**************************************************************************
  Initialize data structures.
**************************************************************************/
void chatline_common_init(void)
{
  remains = remaining_list_new();
}

/**************************************************************************
  Clean up.
**************************************************************************/
void chatline_common_done(void)
{
  remaining_list_free(remains);
}

/**************************************************************************
  Send the message as a chat to the server.
**************************************************************************/
void send_chat(const char *message)
{
  dsend_packet_chat_msg_req(&client.conn, message);
}

/**************************************************************************
  Send the message as a chat to the server. Message is constructed
  in printf style.
**************************************************************************/
void send_chat_printf(const char *format, ...)
{
  char msg[250];
  int maxlen = sizeof(msg);

  va_list ap;
  va_start(ap, format);
  /* FIXME: terminating like this can lead to invalid utf-8, a major no-no. */
  my_vsnprintf(msg, maxlen, format, ap);
  msg[maxlen - 1] = '\0'; /* Make sure there is always ending zero */
  send_chat(msg);
  va_end(ap);
}


static int frozen_level = 0;

/**************************************************************************
  Turn on buffering, using a counter so that calls may be nested.
**************************************************************************/
void output_window_freeze()
{
  frozen_level++;

  if (frozen_level == 1) {
    assert(remaining_list_size(remains) == 0);
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
    remaining_list_clear(remains);
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

    remaining_list_append(remains, premain);
    premain->text = mystrdup(astring);
    premain->conn_id = conn_id;
  }
}

/****************************************************************************
  Standard welcome message.
****************************************************************************/
void chat_welcome_message(void)
{
  append_output_window(_("Freeciv is free software and you are welcome to "
			 "distribute copies of it under certain conditions;"));
  append_output_window(_("See the \"Copying\" item on the Help menu."));
  append_output_window(_("Now ... Go give 'em hell!"));
}
