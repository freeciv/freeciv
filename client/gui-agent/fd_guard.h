/***********************************************************************
 Freeciv - Copyright (C) 1996 - The Freeciv Project

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2, or (at your option)
 any later version.
***********************************************************************/

#ifndef FC__AGENT_FD_GUARD_H
#define FC__AGENT_FD_GUARD_H

#include <stdbool.h>
#include <sys/select.h>

/**********************************************************************//**
  Return whether a descriptor can be represented by this client's select()
  fd_sets. This must be checked before any FD_SET call.
**************************************************************************/
static inline bool fc_agent_fd_selectable(int fd)
{
  return fd >= 0 && fd < FD_SETSIZE;
}

#endif /* FC__AGENT_FD_GUARD_H */
