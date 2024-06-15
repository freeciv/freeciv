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

#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

/* common */
#include "idex.h"
#include "map.h"
#include "world_object.h"

/* server/advisors */
#include "infracache.h"

/* ai/tex */
#include "texaiplayer.h"

#include "texaiworld.h"

static struct world texai_world;

struct texai_tile_info_msg
{
  int index;
  struct terrain *terrain;
  bv_extras extras;
};

struct texai_city_info_msg
{
  int id;
  int owner;
  int tindex;
};

struct texai_id_msg
{
  int id;
};

struct texai_unit_info_msg
{
  int id;
  int owner;
  int tindex;
  int type;
};

struct texai_unit_move_msg
{
  int id;
  int tindex;
};

/**********************************************************************//**
  Initialize world object for texai
**************************************************************************/
void texai_world_init(void)
{
  idex_init(&texai_world);
}

/**********************************************************************//**
  Free resources allocated for texai world object
**************************************************************************/
void texai_world_close(void)
{
  idex_free(&texai_world);
}

/**********************************************************************//**
  Initialize world map for texai
**************************************************************************/
void texai_map_init(void)
{
  map_init(&(texai_world.map), TRUE);
  map_init_topology(&(texai_world.map));
  map_allocate(&(texai_world.map));
}

/**********************************************************************//**
  Return tex worldmap
**************************************************************************/
struct civ_map *texai_map_get(void)
{
  return &(texai_world.map);
}

/**********************************************************************//**
  Free resources allocated for texai world map
**************************************************************************/
void texai_map_close(void)
{
  map_free(&(texai_world.map), TRUE);
}

/**********************************************************************//**
  Tile info updated on main map. Send update to tex map.
**************************************************************************/
void texai_tile_info(struct tile *ptile)
{
  if (texai_thread_running()) {
    struct texai_tile_info_msg *info = fc_malloc(sizeof(struct texai_tile_info_msg));

    info->index = tile_index(ptile);
    info->terrain = ptile->terrain;
    info->extras = ptile->extras;

    texai_send_msg(TEXAI_MSG_TILE_INFO, NULL, info);
  }
}

/**********************************************************************//**
  Receive tile update to the thread.
**************************************************************************/
void texai_tile_info_recv(void *data)
{
  struct texai_tile_info_msg *info = (struct texai_tile_info_msg *)data;

  if (texai_world.map.tiles != NULL) {
    struct tile *ptile;

    ptile = index_to_tile(&(texai_world.map), info->index);
    ptile->terrain = info->terrain;
    ptile->extras = info->extras;
  }

  free(info);
}

/**********************************************************************//**
  Send city information to the thread.
**************************************************************************/
static void texai_city_update(struct city *pcity, enum texaimsgtype msgtype)
{
  if (texai_thread_running()) {
    struct texai_city_info_msg *info
      = fc_malloc(sizeof(struct texai_city_info_msg));

    info->id = pcity->id;
    info->owner = player_number(city_owner(pcity));
    info->tindex = tile_index(city_tile(pcity));

    texai_send_msg(msgtype, NULL, info);
  }
}

/**********************************************************************//**
  New city has been added to the main map.
**************************************************************************/
void texai_city_created(struct city *pcity)
{
  texai_city_update(pcity, TEXAI_MSG_CITY_CREATED);
}

/**********************************************************************//**
  City on main map has (potentially) changed.
**************************************************************************/
void texai_city_changed(struct city *pcity)
{
  texai_city_update(pcity, TEXAI_MSG_CITY_CHANGED);
}

/**********************************************************************//**
  Receive city update to the thread.
**************************************************************************/
void texai_city_info_recv(void *data, enum texaimsgtype msgtype)
{
  struct texai_city_info_msg *info = (struct texai_city_info_msg *)data;
  struct city *pcity;
  struct player *pplayer = player_by_number(info->owner);

  if (msgtype == TEXAI_MSG_CITY_CREATED) {
    struct tile *ptile;
    struct texai_plr *plr_data;

    if (idex_lookup_city(&texai_world, info->id) != NULL) {
      return;
    }

    ptile = index_to_tile(&(texai_world.map), info->tindex);

    pcity = create_city_virtual(pplayer, ptile, "");
    adv_city_alloc(pcity);
    pcity->id = info->id;

    idex_register_city(&texai_world, pcity);
    plr_data = player_ai_data(pplayer, texai_get_self());
    city_list_prepend(plr_data->cities, pcity);
    tile_set_worked(ptile, pcity);
  } else {
    pcity = idex_lookup_city(&texai_world, info->id);

    if (pcity != NULL) {
      pcity->owner = pplayer;
    } else {
      log_error("Tex: requested change on city id %d that's not known.",
                info->id);
    }
  }

  free(info);
}

/**********************************************************************//**
  Get city from the tex map
**************************************************************************/
struct city *texai_map_city(int city_id)
{
  return idex_lookup_city(&texai_world, city_id);
}

/**********************************************************************//**
  City has been removed from the main map.
**************************************************************************/
void texai_city_destroyed(struct city *pcity)
{
  if (texai_thread_running()) {
    struct texai_id_msg *info = fc_malloc(sizeof(struct texai_id_msg));

    info->id = pcity->id;

    texai_send_msg(TEXAI_MSG_CITY_DESTROYED, NULL, info);
  }
}

/**********************************************************************//**
  Receive city destruction to the thread.
**************************************************************************/
void texai_city_destruction_recv(void *data)
{
  struct texai_id_msg *info = (struct texai_id_msg *)data;
  struct city *pcity = idex_lookup_city(&texai_world, info->id);

  if (pcity != NULL) {
    struct texai_plr *plr_data;

    adv_city_free(pcity);
    tile_set_worked(city_tile(pcity), NULL);
    plr_data = player_ai_data(city_owner(pcity), texai_get_self());
    city_list_remove(plr_data->cities, pcity);
    idex_unregister_city(&texai_world, pcity);
    destroy_city_virtual(pcity);
  } else {
    log_error("Tex: requested removal of city id %d that's not known.",
              info->id);
  }

  free(info);
}

/**********************************************************************//**
  Send unit information to the thread.
**************************************************************************/
static void texai_unit_update(struct unit *punit, enum texaimsgtype msgtype)
{
  if (texai_thread_running()) {
    struct texai_unit_info_msg *info
      = fc_malloc(sizeof(struct texai_unit_info_msg));

    info->id = punit->id;
    info->owner = player_number(unit_owner(punit));
    info->tindex = tile_index(unit_tile(punit));
    info->type = utype_number(unit_type_get(punit));

    texai_send_msg(msgtype, NULL, info);
  }
}

/**********************************************************************//**
  New unit has been added to the main map.
**************************************************************************/
void texai_unit_created(struct unit *punit)
{
  texai_unit_update(punit, TEXAI_MSG_UNIT_CREATED);
}

/**********************************************************************//**
  Unit (potentially) changed in main map.
**************************************************************************/
void texai_unit_changed(struct unit *punit)
{
  texai_unit_update(punit, TEXAI_MSG_UNIT_CHANGED);
}

/**********************************************************************//**
  Receive unit update to the thread.
**************************************************************************/
void texai_unit_info_recv(void *data, enum texaimsgtype msgtype)
{
  struct texai_unit_info_msg *info = (struct texai_unit_info_msg *)data;
  struct unit *punit;
  struct player *pplayer = player_by_number(info->owner);
  struct unit_type *type = utype_by_number(info->type);
  struct tile *ptile = index_to_tile(&(texai_world.map), info->tindex);

  if (msgtype == TEXAI_MSG_UNIT_CREATED) {
    struct texai_plr *plr_data;

    if (idex_lookup_unit(&texai_world, info->id) != NULL) {
      return;
    }

    plr_data = player_ai_data(pplayer, texai_get_self());

    punit = unit_virtual_create(pplayer, NULL, type, 0);
    punit->id = info->id;

    idex_register_unit(&texai_world, punit);
    unit_list_prepend(ptile->units, punit);
    unit_list_prepend(plr_data->units, punit);

    unit_tile_set(punit, ptile);
  } else {
    fc_assert(msgtype == TEXAI_MSG_UNIT_CHANGED);

    punit = idex_lookup_unit(&texai_world, info->id);

    punit->utype = type;
  }

  free(info);
}

/**********************************************************************//**
  Unit has been removed from the main map.
**************************************************************************/
void texai_unit_destroyed(struct unit *punit)
{
  if (texai_thread_running()) {
    struct texai_id_msg *info = fc_malloc(sizeof(struct texai_id_msg));

    info->id = punit->id;

    texai_send_msg(TEXAI_MSG_UNIT_DESTROYED, NULL, info);
  }
}

/**********************************************************************//**
  Receive unit destruction to the thread.
**************************************************************************/
void texai_unit_destruction_recv(void *data)
{
  struct texai_id_msg *info = (struct texai_id_msg *)data;
  struct unit *punit = idex_lookup_unit(&texai_world, info->id);

  if (punit != NULL) {
    struct texai_plr *plr_data = player_ai_data(punit->owner,
                                                texai_get_self());

    unit_list_remove(punit->tile->units, punit);
    unit_list_remove(plr_data->units, punit);
    idex_unregister_unit(&texai_world, punit);
    unit_virtual_destroy(punit);
  } else {
    log_error("Tex: requested removal of unit id %d that's not known.",
              info->id);
  }

  free(info);
}

/**********************************************************************//**
  Unit has moved in the main map.
**************************************************************************/
void texai_unit_move_seen(struct unit *punit)
{
  if (texai_thread_running()) {
    struct texai_unit_move_msg *info = fc_malloc(sizeof(struct texai_unit_move_msg));

    info->id = punit->id;
    info->tindex = tile_index(unit_tile(punit));

    texai_send_msg(TEXAI_MSG_UNIT_MOVED, NULL, info);
  }
}

/**********************************************************************//**
  Receive unit move to the thread.
**************************************************************************/
void texai_unit_moved_recv(void *data)
{
  struct texai_unit_move_msg *info = (struct texai_unit_move_msg *)data;
  struct unit *punit = idex_lookup_unit(&texai_world, info->id);
  struct tile *ptile = index_to_tile(&(texai_world.map), info->tindex);

  if (punit != NULL) {
    unit_list_remove(punit->tile->units, punit);
    unit_list_prepend(ptile->units, punit);

    unit_tile_set(punit, ptile);
  } else {
    log_error("Tex: requested moving of unit id %d that's not known.",
              info->id);
  }

  free(info);
}
