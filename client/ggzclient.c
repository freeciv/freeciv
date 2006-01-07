/********************************************************************** 
 Freeciv - Copyright (C) 2005 - Freeciv Development Team
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

#ifdef GGZ_CLIENT

#include <ggzmod.h>

#include "fciconv.h"
#include "fcintl.h"

#include "gui_main_g.h"

#include "clinet.h"
#include "ggzclient.h"


bool with_ggz = FALSE;

static GGZMod *ggzmod;

/****************************************************************************
  A callback that GGZ calls when we are connected to the freeciv
  server.
****************************************************************************/
static void handle_ggzmod_server(GGZMod * ggzmod, GGZModEvent e,
				 const void *data)
{
  const int *socket = data;
  const char *username = ggzmod_get_player(ggzmod, NULL, NULL);

  if (!username) {
    username = "NONE";
  }
  make_connection(*socket, username);
}

/****************************************************************************
  Connect to the GGZ client, if GGZ is being used.
****************************************************************************/
void ggz_initialize(void)
{
  int ggz_socket;

  /* We're in GGZ mode */
  ggzmod = ggzmod_new(GGZMOD_GAME);
  ggzmod_set_handler(ggzmod, GGZMOD_EVENT_SERVER, &handle_ggzmod_server);
  if (ggzmod_connect(ggzmod) < 0) {
    exit(EXIT_FAILURE);
  }
  ggz_socket = ggzmod_get_fd(ggzmod);
  if (ggz_socket < 0) {
    fc_fprintf(stderr, _("Only the GGZ client must call civclient"
			 " in ggz mode!\n"));
    exit(EXIT_FAILURE);
  }
  add_ggz_input(ggz_socket);
}

/****************************************************************************
  Called when the ggz socket has data pending.
****************************************************************************/
void input_from_ggz(int socket)
{
  ggzmod_dispatch(ggzmod);
}

#endif
