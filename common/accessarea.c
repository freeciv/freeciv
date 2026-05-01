/****************************************************************************
 Freeciv - Copyright (C) 2004 - The Freeciv Team
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
****************************************************************************/

#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

/* common/aicore */
#include "pf_tools.h"

/* common */
#include "player.h"
#include "tiledef.h"

#include "accessarea.h"

struct access_info {
  const struct unit_type *access_unit;
};

static struct access_info ainfo = { nullptr };

static struct aarea_list *aalist[MAX_NUM_PLAYERS];

/*********************************************************************//**
  Initialize access info.
  @param aunit Access unit for the access info
*************************************************************************/
void access_info_init(const struct unit_type *aunit)
{
  if (aunit != nullptr) {
    int i;

    ainfo.access_unit = aunit;

    for (i = 0; i < MAX_NUM_PLAYERS; i++) {
      aalist[i] = aarea_list_new();
    }
  }
}

/*********************************************************************//**
  Close the access info.
*************************************************************************/
void access_info_close(void)
{
  int i;

  if (ainfo.access_unit != nullptr) {
    ainfo.access_unit = nullptr;

    for (i = 0; i < MAX_NUM_PLAYERS; i++) {
      area_list_clear(aalist[i]);
    }
  }
}

/*********************************************************************//**
  Get access_unit of the access_info
*************************************************************************/
const struct unit_type *access_info_access_unit(void)
{
  return ainfo.access_unit;
}

/*********************************************************************//**
  Free access area list.
  @param alist List to clear
*************************************************************************/
void area_list_clear(struct aarea_list *alist)
{
  aarea_list_iterate(alist, parea) {
    city_list_destroy(parea->cities);
    free(parea);
  } aarea_list_iterate_end;

  aarea_list_destroy(alist);
}

/*********************************************************************//**
  Free access area list of player.
  @param pplayer Whose list to clear
*************************************************************************/
void area_list_clear_plr(struct player *pplayer)
{
  area_list_clear(aalist[player_index(pplayer)]);
}

/*********************************************************************//**
  Set access area list for player
  @param pplayer Whose list to set
*************************************************************************/
void area_list_for_player_set(struct player *pplayer, struct aarea_list *alist)
{
  aalist[player_index(pplayer)] = alist;
}
