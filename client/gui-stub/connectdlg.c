/* connectdlg.c -- PLACEHOLDER */

#include <errno.h>

#include "log.h"

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
	      server_host, server_port, connect_name);
  append_output_window(buf);
  outcome = get_server_address(server_host, server_port, buf, sizeof(buf));
  if (outcome < 0) {
    freelog(LOG_FATAL,
	    _("Error contacting server \"%s\" at port %d "
	      "as \"%s\":\n %s\n"),
	    server_host, server_port, connect_name, buf);
    exit(1);
  }
  try_to_autoconnect();
}

/**************************************************************************
  Make an attempt to autoconnect to the server.  If the server isn't
  there yet, arrange for this routine to be called again in about
  AUTOCONNECT_INTERVAL milliseconds.  If anything else goes wrong, log
  a fatal error.
**************************************************************************/
static int try_to_autoconnect()
{
  char errbuf[512];
  static int count = 0;

  count++;

  /* abort if after 10 seconds the server couldn't be reached */

  if (AUTOCONNECT_INTERVAL * count >= 10000) {
    freelog(LOG_FATAL,
	    _("Failed to contact server \"%s\" at port "
	      "%d as \"%s\" after %d attempts"),
	    server_host, server_port, connect_name, count);
    gtk_exit(1);
  }

  switch (try_to_connect(connect_name, errbuf, sizeof(errbuf))) {
  case 0:			/* Success! */
    return;
  case ECONNREFUSED:		/* Server not available (yet) - wait & retry */
     /*PORTME*/ schedule_timer
	(AUTOCONNECT_INTERVAL, try_to_autoconnect, NULL);
  default:			/* All other errors are fatal */
    freelog(LOG_FATAL,
	    _("Error contacting server \"%s\" at port %d "
	      "as \"%s\":\n %s\n"),
	    server_host, server_port, connect_name, errbuf);
     /*PORTME*/ exit_application(error code);
  }
}
