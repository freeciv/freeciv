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

#include <stdio.h>

#include <nanohttp/nanohttp-logging.h>
#include <libcsoap/soap-server.h>

#include <pthread.h>

#include "fciconv.h"
#include "hash.h"
#include "log.h"

#include "canvas.h"
#include "civclient.h"
#include "chatline.h"
#include "gui_main.h"
#include "options.h"
#include "mapview_common.h"
#include "tilespec.h"
#include "sprite.h"

#include "webservice.h"


static const char *url = "/civwebserver";
static const char *urn = "urn:freeciv.org";


/****************************************************************************
 ...
****************************************************************************/
static herror_t get_freeciv_messages(SoapCtx * req, SoapCtx * res)
{

  herror_t err;
  pthread_mutex_lock (&civmutex);

  err = soap_env_new_with_response(req->env, &res->env);
  if (err != H_OK)
  {
    return err;
  }

  soap_env_add_itemf(res->env, "xsd:string", "messages", get_output_window_text ());

  pthread_mutex_unlock (&civmutex);

  return H_OK;
}

/****************************************************************************
 ...
****************************************************************************/
static herror_t get_freeciv_state(SoapCtx * req, SoapCtx * res)
{

  herror_t err;
  pthread_mutex_lock (&civmutex);

  err = soap_env_new_with_response(req->env, &res->env);
  if (err != H_OK)
  {
    return err;
  }

  soap_env_add_itemf(res->env, "xsd:int", "state", "%d", get_client_state());
  pthread_mutex_unlock (&civmutex);

  return H_OK;
}


/****************************************************************************
 ...
****************************************************************************/
static herror_t add_freeciv_message(SoapCtx * req, SoapCtx * res)
{

  herror_t err;
  char *message;
  pthread_mutex_lock (&civmutex);

  xmlNodePtr method, node;

  err = soap_env_new_with_response(req->env, &res->env);
  if (err != H_OK)
  {
    return err;
  }

  method = soap_env_get_method(req->env);
  node = soap_xml_get_children(method);

  while (node)
  {
    message = (char *) xmlNodeListGetString(node->doc, node->xmlChildrenNode, 1);
    send_chat(message);
    soap_env_add_itemf(res->env, "xsd:string", "messages", "ok");
    node = soap_xml_get_next(node);
    xmlFree(message);
  }

  pthread_mutex_unlock (&civmutex);
  return H_OK;
}

/****************************************************************************
 ...
****************************************************************************/
static herror_t get_freeciv_tileset(SoapCtx * req, SoapCtx * res)
{

  herror_t err;
  pthread_mutex_lock (&civmutex);

  err = soap_env_new_with_response(req->env, &res->env);
  if (err != H_OK)
  {
    return err;
  }

 if (tileset->sprite_hash) {
    int i, entries = hash_num_entries(tileset->sprite_hash);

    for (i = 0; i < entries; i++) {
      const char *spritef = NULL;
      const char *tag_name = hash_key_by_number(tileset->sprite_hash, i);
      struct small_sprite *ss = hash_lookup_data(tileset->sprite_hash, tag_name);
      if (ss->sprite) {
        spritef = ss->sprite->filename;
      } else {
        continue;
      }
      soap_env_add_itemf(res->env, "xsd:string", "1", "%s", tag_name);
      soap_env_add_itemf(res->env, "xsd:string", "2", "%s", spritef);
      soap_env_add_itemf(res->env, "xsd:string", "3", "%d", ss->x);
      soap_env_add_itemf(res->env, "xsd:string", "4", "%d", ss->y);
      soap_env_add_itemf(res->env, "xsd:string", "5", "%d", ss->width);
      soap_env_add_itemf(res->env, "xsd:string", "6", "%d", ss->height);
    }
  }
  pthread_mutex_unlock (&civmutex);

  return H_OK;
}


/****************************************************************************
 ...
****************************************************************************/
static herror_t get_freeciv_mapview(SoapCtx * req, SoapCtx * res)
{

  herror_t err;
  int i;
  pthread_mutex_lock (&civmutex);

  err = soap_env_new_with_response(req->env, &res->env);
  if (err != H_OK)
  {
    return err;
  }

  update_map_canvas_visible();
  center_tile_overviewcanvas();
  unqueue_mapview_updates(TRUE);

  int *x, *y; 

  for (i = 0; i < genlist_size(web_draw_queue); i += 3) {
    soap_env_add_itemf(res->env, "xsd:string", "1", "%s", 
                       genlist_get(web_draw_queue, i));
    x = genlist_get(web_draw_queue, i + 1); 
    soap_env_add_itemf(res->env, "xsd:string", "2", "%u", *x);
    y = genlist_get(web_draw_queue, i + 2); 
    soap_env_add_itemf(res->env, "xsd:string", "3", "%u", *y);
  }

  genlist_unlink_all(web_draw_queue);

  pthread_mutex_unlock (&civmutex);

  return H_OK;
}



/****************************************************************************
 ...
****************************************************************************/
void init_webservice(void)
{
  SoapRouter *router;

  router = soap_router_new();
  soap_router_register_service(router, get_freeciv_messages, "get_freeciv_messages", urn);
  soap_router_register_service(router, get_freeciv_state, "get_freeciv_state", urn);
  soap_router_register_service(router, add_freeciv_message, "add_freeciv_message", urn);
  soap_router_register_service(router, get_freeciv_tileset, "get_freeciv_tileset", urn);
  soap_router_register_service(router, get_freeciv_mapview, "get_freeciv_mapview", urn);
  soap_server_register_router(router, url);

  log_info1("press ctrl-c to shutdown");
  soap_server_run();

  log_info1("shutting down\n");
  soap_server_destroy();


}
