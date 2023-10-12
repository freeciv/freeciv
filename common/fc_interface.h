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
#ifndef FC__FC_INTERFACE_H
#define FC__FC_INTERFACE_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* utility */
#include "support.h"

/* common */
#include "fc_types.h"
#include "vision.h"

struct player;
struct tile;
struct color;
struct extra_type;

/* The existence of each function should be checked in interface_init()! */
struct functions {
  server_setting_id (*server_setting_by_name)(const char *name);
  const char *(*server_setting_name_get)(server_setting_id id);
  enum sset_type (*server_setting_type_get)(server_setting_id id);
  bool (*server_setting_val_bool_get)(server_setting_id id);
  int (*server_setting_val_int_get)(server_setting_id id);
  unsigned int (*server_setting_val_bitwise_get)(server_setting_id id);
  void (*create_extra)(struct tile *ptile, struct extra_type *pextra,
                       struct player *pplayer);
  void (*destroy_extra)(struct tile *ptile, struct extra_type *pextra);
  void (*destroy_city)(struct city *pcity);
  /* Returns iff the player 'pplayer' has the vision in the layer
     'vision' at tile given by 'ptile'. */
  bool (*player_tile_vision_get)(const struct tile *ptile,
                                 const struct player *pplayer,
                                 enum vision_layer vision);
  /* Returns the id of the city 'pplayer' believes exists at 'ptile' or
   * IDENTITY_NUMBER_ZERO when the player is unaware of a city at that
   * location. */
  int (*player_tile_city_id_get)(const struct tile *ptile,
                                 const struct player *pplayer);
  void (*gui_color_free)(struct color *pcolor);
};

extern const struct functions *fc_funcs;

struct functions *fc_interface_funcs(void);
void libfreeciv_init(bool check_fc_interface);
void libfreeciv_free(void);

extern bool am_i_server;


/**********************************************************************//**
  Is the program type server?
**************************************************************************/
static inline bool is_server(void)
{
  return am_i_server;
}

void i_am_server(void);
void i_am_client(void);

/**********************************************************************//**
  Set program type to tool.
**************************************************************************/
static inline void i_am_tool(void)
{
  i_am_server(); /* No difference between a tool and server at the moment */
}

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* FC__FC_INTERFACE_H */
