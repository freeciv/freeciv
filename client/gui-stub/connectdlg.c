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

#include <errno.h>

#include "fcintl.h"
#include "log.h"
#include "support.h"

#include "connectdlg_g.h"

#include "chatline_common.h"	/* for append_output_window */
#include "civclient.h"
#include "clinet.h"		/* for get_server_address */
#include "connectdlg.h"
#include "packhand_gen.h"

static void try_to_autoconnect(void);


/**************************************************************************
 really close and destroy the dialog.
**************************************************************************/
void really_close_connection_dialog(void)
{
  /* PORTME */
}

/**************************************************************************
 close and destroy the dialog.
**************************************************************************/
void close_connection_dialog()
{
  /* PORTME */
}

/**************************************************************************
 configure the dialog depending on what type of authentication request the
 server is making.
**************************************************************************/
void handle_authentication_req(enum authentication_type type, char *message)
{
  switch (type) {
  case AUTH_NEWUSER_FIRST:
     /* PORTME: switch configs if need be */
    break;
  case AUTH_NEWUSER_RETRY:
     /* PORTME: switch configs if need be */
    break;
  case AUTH_LOGIN_FIRST:
    /* if we magically have a password already present in 'password'
     * then, use that and skip the password entry dialog */
    if (password[0] != '\0') {
      struct packet_authentication_reply reply;

      sz_strlcpy(reply.password, password);
      send_packet_authentication_reply(&aconnection, &reply);
      return;
    } else {
     /* PORTME: switch configs if need be */
    }
    break;
  case AUTH_LOGIN_RETRY:
     /* PORTME: switch configs if need be */
    break;
  default:
    assert(0);
  }

  /* PORTME etc. */
}

/**************************************************************************
 provide a packet handler for packet_game_load
**************************************************************************/
void handle_game_load(struct packet_game_load *packet)
{ 
  /* PORTME */
}


/**************************************************************************
  Provide an interface for connecting to a FreeCiv server.
**************************************************************************/
void gui_server_connect(void)
{
  /* PORTME */
}


/**************************************************************************
  Start trying to autoconnect to civserver.
  Calls get_server_address(), then arranges for
  autoconnect_callback(), which calls try_to_connect(), to be called
  roughly every AUTOCONNECT_INTERVAL milliseconds, until success,
  fatal error or user intervention.
**************************************************************************/
void server_autoconnect(void)
{
  char buf[512];
  int outcome;

  my_snprintf(buf, sizeof(buf),
	      _("Auto-connecting to server \"%s\" at port %d as \"%s\""),
	      server_host, server_port, user_name);
  append_output_window(buf);
  outcome = get_server_address(server_host, server_port, buf, sizeof(buf));
  if (outcome < 0) {
    freelog(LOG_FATAL,
	    _("Error contacting server \"%s\" at port %d "
	      "as \"%s\":\n %s\n"),
	    server_host, server_port, user_name, buf);
    exit(EXIT_FAILURE);
  }
  try_to_autoconnect();
}

/**************************************************************************
  Make an attempt to autoconnect to the server.  If the server isn't
  there yet, arrange for this routine to be called again in about
  AUTOCONNECT_INTERVAL milliseconds.  If anything else goes wrong, log
  a fatal error.

  Return FALSE iff autoconnect succeeds.
**************************************************************************/
static void try_to_autoconnect(void)
{
  char errbuf[512];
  static int count = 0;

  count++;

  /* abort if after 10 seconds the server couldn't be reached */

  if (AUTOCONNECT_INTERVAL * count >= 10000) {
    freelog(LOG_FATAL,
	    _("Failed to contact server \"%s\" at port "
	      "%d as \"%s\" after %d attempts"),
	    server_host, server_port, user_name, count);
    exit(EXIT_FAILURE);
  }

  switch (try_to_connect(user_name, errbuf, sizeof(errbuf))) {
  case 0:
    /* Success! */
    return;
  case ECONNREFUSED:
    /* Server not available (yet) - wait & retry */
#if 0
    /* PORTME */
    schedule_timer(AUTOCONNECT_INTERVAL, try_to_autoconnect, NULL);
#endif
    return;
  default:
    /* All other errors are fatal */
    freelog(LOG_FATAL,
	    _("Error contacting server \"%s\" at port %d "
	      "as \"%s\":\n %s\n"),
	    server_host, server_port, user_name, errbuf);
    exit(EXIT_FAILURE);
  }
}
