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

#include "accessarea.h"

struct access_info {
  const struct unit_type *access_unit;
};

static struct access_info ainfo = { nullptr };

static struct aarea_list *aalist[MAX_NUM_PLAYERS];

static void area_list_clear(struct aarea_list *alist);

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
static void area_list_clear(struct aarea_list *alist)
{
  aarea_list_iterate(alist, parea) {
    city_list_destroy(parea->cities);
    free(parea);
  } aarea_list_iterate_end;

  aarea_list_destroy(alist);
}

/*********************************************************************//**
  Construct access areas
  @param nmap Map to use when determining access
  @param plr  Player to construct areas for
*************************************************************************/
void access_areas_refresh(struct civ_map *nmap, struct player *plr)
{
  if (ainfo.access_unit != nullptr) {
    int plridx = player_number(plr);
    struct unit *access_unit;

    area_list_clear(aalist[plridx]);
    aalist[plridx] = aarea_list_new();

    city_list_iterate(plr->cities, pcity) {
      pcity->server.aarea = nullptr;
    } city_list_iterate_end;

    access_unit = unit_virtual_create(plr, nullptr,
                                      ainfo.access_unit, 0);

    city_list_iterate(plr->cities, pcity) {
      if (pcity->server.aarea == nullptr) {
        struct access_area *aarea = fc_malloc(sizeof(struct access_area));
        struct pf_parameter parameter;
        struct pf_map *pfm;

        aarea->cities = city_list_new();
        aarea->capital = is_capital(pcity);

        pcity->server.aarea = aarea;
        city_list_append(aarea->cities, pcity);
        aarea_list_append(aalist[plridx], aarea);

        unit_tile_set(access_unit, city_tile(pcity));
        pft_fill_unit_parameter(&parameter, nmap, access_unit);
        pfm = pf_map_new(&parameter);

        city_list_iterate(plr->cities, pcity2) {
          if (pcity2->server.aarea == nullptr) {
            struct pf_path *path;

            path = pf_map_path(pfm, city_tile(pcity2));
            if (path != nullptr) {
              pcity2->server.aarea = aarea;
              city_list_append(aarea->cities, pcity2);

              if (!aarea->capital && is_capital(pcity2)) {
                aarea->capital = TRUE;
              }
            }

            pf_path_destroy(path);
          }
        } city_list_iterate_end;

        pf_map_destroy(pfm);
      }
    } city_list_iterate_end;

    unit_virtual_destroy(access_unit);
  }
}
