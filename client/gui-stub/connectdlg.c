/* connectdlg.c -- PLACEHOLDER */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <errno.h>

#include "fcintl.h"
#include "log.h"
#include "support.h"

#include "chatline_common.h"	/* for append_output_window */
#include "civclient.h"
#include "clinet.h"		/* for get_server_address */

#include "connectdlg.h"

static void try_to_autoconnect(void);


void
gui_server_connect(void)
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
void server_autoconnect()
{
  char buf[512];
  int outcome;

  my_snprintf(buf, sizeof(buf),
	      _("Auto-connecting to server \"%s\" at port %d as \"%s\""),
	      server_host, server_port, player_name);
  append_output_window(buf);
  outcome = get_server_address(server_host, server_port, buf, sizeof(buf));
  if (outcome < 0) {
    freelog(LOG_FATAL,
	    _("Error contacting server \"%s\" at port %d "
	      "as \"%s\":\n %s\n"),
	    server_host, server_port, player_name, buf);
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
	    server_host, server_port, player_name, count);
    exit(EXIT_FAILURE);
  }

  switch (try_to_connect(player_name, errbuf, sizeof(errbuf))) {
  case 0:			/* Success! */
    return;
  case ECONNREFUSED:		/* Server not available (yet) - wait & retry */
#if 0
    /* PORTME */
    schedule_timer(AUTOCONNECT_INTERVAL, try_to_autoconnect, NULL);
#endif
    return;
  default:			/* All other errors are fatal */
    freelog(LOG_FATAL,
	    _("Error contacting server \"%s\" at port %d "
	      "as \"%s\":\n %s\n"),
	    server_host, server_port, player_name, errbuf);
    exit(EXIT_FAILURE);
  }
}
