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

/* utility */
#include "hash.h"
#include "log.h"
#include "support.h"            /* bool */

/* common */
#include "city.h"
#include "player.h"

/* include */
#include "citydlg_g.h"
#include "cityrep_g.h"
#include "gui_main_g.h"
#include "menu_g.h"
#include "plrdlg_g.h"
#include "repodlgs_g.h"

#include "update_queue.h"


#define Q_CALLBACK(fn) ((queue_callback_t) fn)

static struct hash_table *update_queue = NULL;
static struct hash_table *processing_started_waiting_queue = NULL;
static struct hash_table *processing_finished_waiting_queue = NULL;
static int update_queue_frozen_level = 0;
static bool update_queue_has_idle_callback = FALSE;

static void update_unqueue(void *data);

/****************************************************************************
  Initialize the update queue.
****************************************************************************/
void update_queue_init(void)
{
  if (NULL != update_queue) {
    update_queue_free();
  }

  update_queue = hash_new(hash_fval_keyval, hash_fcmp_keyval);
  processing_started_waiting_queue = hash_new(hash_fval_keyval,
                                              hash_fcmp_keyval);
  processing_finished_waiting_queue = hash_new(hash_fval_keyval,
                                               hash_fcmp_keyval);
  update_queue_frozen_level = 0;
  update_queue_has_idle_callback = FALSE;
}

/****************************************************************************
  Free the update queue.
****************************************************************************/
void update_queue_free(void)
{
  if (NULL != update_queue) {
    hash_free(update_queue);
    update_queue = NULL;
  }
  if (NULL != processing_started_waiting_queue) {
    hash_free(processing_started_waiting_queue);
    processing_started_waiting_queue = NULL;
  }
  if (NULL != processing_finished_waiting_queue) {
    hash_free(processing_finished_waiting_queue);
    processing_finished_waiting_queue = NULL;
  }

  update_queue_frozen_level = 0;
  update_queue_has_idle_callback = FALSE;
}

/****************************************************************************
  Freezes the update queue.
****************************************************************************/
void update_queue_freeze(void)
{
  update_queue_frozen_level++;
}

/****************************************************************************
  Free the update queue.
****************************************************************************/
void update_queue_thaw(void)
{
  update_queue_frozen_level--;
  if (0 == update_queue_frozen_level
      && !update_queue_has_idle_callback
      && NULL != update_queue
      && 0 < hash_num_entries(update_queue)) {
    update_queue_has_idle_callback = TRUE;
    add_idle_callback(update_unqueue, NULL);
  } else if (0 > update_queue_frozen_level) {
    log_error("update_queue_frozen_level < 0, reparing...");
    update_queue_frozen_level = 0;
  }
}

/****************************************************************************
  Free the update queue.
****************************************************************************/
void update_queue_force_thaw(void)
{
  while (update_queue_is_frozen()) {
    update_queue_thaw();
  }
}

/****************************************************************************
  Free the update queue.
****************************************************************************/
bool update_queue_is_frozen(void)
{
  return (0 < update_queue_frozen_level);
}

/****************************************************************************
  Moves the instances waiting to the request_id to the callback queue.
****************************************************************************/
void update_queue_processing_started(int request_id)
{
  if (NULL == processing_started_waiting_queue) {
    return;
  }

  hash_iterate(processing_started_waiting_queue, iter) {
    update_queue_add(Q_CALLBACK(hash_iter_get_key(iter)),
                     hash_iter_get_value(iter));
  } hash_iterate_end;
  hash_delete_all_entries(processing_started_waiting_queue);
}

/****************************************************************************
  Moves the instances waiting to the request_id to the callback queue.
****************************************************************************/
void update_queue_processing_finished(int request_id)
{
  if (NULL == processing_finished_waiting_queue) {
    return;
  }

  hash_iterate(processing_finished_waiting_queue, iter) {
    update_queue_add(Q_CALLBACK(hash_iter_get_key(iter)),
                     hash_iter_get_value(iter));
  } hash_iterate_end;
  hash_delete_all_entries(processing_finished_waiting_queue);
}

/****************************************************************************
  Unqueue all updates.
****************************************************************************/
static void update_unqueue(void *data)
{
  struct hash_table *hash;

  if (NULL == update_queue) {
    update_queue_has_idle_callback = FALSE;
    return;
  }

  if (update_queue_is_frozen()) {
    /* Cannot update now, let's add it again. */
    update_queue_has_idle_callback = FALSE;
    return;
  }

  /* Replace the old list, don't do recursive stuff, and don't write in the
   * hash table when we are reading it. */
  hash = update_queue;
  update_queue = hash_new(hash_fval_keyval, hash_fcmp_keyval);
  update_queue_has_idle_callback = FALSE;

  /* Invoke callbacks. */
  hash_iterate(hash, iter) {
    Q_CALLBACK(hash_iter_get_key(iter)) (hash_iter_get_value(iter));
  } hash_iterate_end;
  hash_free(hash);
}

/****************************************************************************
  Add a callback to the update queue.  NB: you can only set a callback
  once.  Setting a callback twice will overwrite the previous.
****************************************************************************/
void update_queue_add(queue_callback_t callback, void *data)
{
  if (NULL != update_queue) {
    (void) hash_replace(update_queue, callback, data);

    if (!update_queue_has_idle_callback
        && !update_queue_is_frozen()) {
      update_queue_has_idle_callback = TRUE;
      add_idle_callback(update_unqueue, NULL);
    }
  }
}

/****************************************************************************
  Returns whether this callback is listed in the update queue and
  get the its data.  data can be NULL.
****************************************************************************/
bool update_queue_has_callback(queue_callback_t callback, const void **data)
{
  return (NULL != update_queue
          && hash_lookup(update_queue, callback, NULL, data));
}

/****************************************************************************
  Connects the callback to the start of the processing (in server side) of
  the request.
****************************************************************************/
void update_queue_connect_processing_started(int request_id,
                                             queue_callback_t callback,
                                             void *data)
{
  if (NULL != processing_started_waiting_queue) {
    (void) hash_replace(processing_started_waiting_queue, callback, data);
  }
}

/****************************************************************************
  Connects the callback to the end of the processing (in server side) of
  the request.
****************************************************************************/
void update_queue_connect_processing_finished(int request_id,
                                              queue_callback_t callback,
                                              void *data)
{
  if (NULL != processing_finished_waiting_queue) {
    (void) hash_replace(processing_finished_waiting_queue, callback, data);
  }
}


/****************************************************************************
  Set the client page.
****************************************************************************/
static void set_client_page_callback(void *data)
{
  real_set_client_page(FC_PTR_TO_INT(data));
}

/****************************************************************************
  Set the client page.
****************************************************************************/
void set_client_page(enum client_pages page)
{
  log_debug("Requested page: %s.", client_pages_name(page));

  update_queue_add(set_client_page_callback, FC_INT_TO_PTR(page));
}

/****************************************************************************
  Returns the next client page.
****************************************************************************/
enum client_pages get_client_page(void)
{
  const void *data;

  if (update_queue_has_callback(set_client_page_callback, &data)) {
    return FC_PTR_TO_INT(data);
  } else {
    return get_current_client_page();
  }
}


/****************************************************************************
  Update the menus.
****************************************************************************/
static void menus_update_callback(void *data)
{
  if (FC_PTR_TO_INT(data)) {
    real_menus_init();
  }
  real_menus_update();
}

/****************************************************************************
  Request the menus to be initialized and updated.
****************************************************************************/
void menus_init(void)
{
  update_queue_add(menus_update_callback, FC_INT_TO_PTR(TRUE));
}

/****************************************************************************
  Request the menus to be updated.
****************************************************************************/
void menus_update(void)
{
  if (!update_queue_has_callback(menus_update_callback, NULL)) {
    update_queue_add(menus_update_callback, FC_INT_TO_PTR(FALSE));
  }
}


/****************************************************************************
  Update cities gui.
****************************************************************************/
static void cities_update_callback(void *data)
{
#ifdef DEBUG
#define NEED_UPDATE(city_update, action)                                    \
  if (city_update & need_update) {                                          \
    action;                                                                 \
    need_update &= ~city_update;                                            \
  }
#else
#define NEED_UPDATE(city_update, action)                                    \
  if (city_update & need_update) {                                          \
    action;                                                                 \
  }
#endif /* DEBUG */

  cities_iterate(pcity) {
    enum city_updates need_update = pcity->client.need_updates;

    if (CU_NO_UPDATE == need_update) {
      continue;
    }

    /* Clear all updates. */
    pcity->client.need_updates = CU_NO_UPDATE;

    NEED_UPDATE(CU_UPDATE_REPORT, real_city_report_update_city(pcity));
    NEED_UPDATE(CU_UPDATE_DIALOG, real_city_dialog_refresh(pcity));
    NEED_UPDATE(CU_POPUP_DIALOG, real_city_dialog_popup(pcity));

#ifdef DEBUG
    if (CU_NO_UPDATE != need_update) {
      log_error("Some city updates not handled "
                "for city %s (id %d): %d left.",
                city_name(pcity), pcity->id, need_update);
    }
#endif /* DEBUG */
  } cities_iterate_end;
#undef NEED_UPDATE
}

/****************************************************************************
  Request the city dialog to be popped up for the city.
****************************************************************************/
void popup_city_dialog(struct city *pcity)
{
  pcity->client.need_updates |= CU_POPUP_DIALOG;
  update_queue_add(cities_update_callback, NULL);
}

/****************************************************************************
  Request the city dialog to be updated for the city.
****************************************************************************/
void refresh_city_dialog(struct city *pcity)
{
  pcity->client.need_updates |= CU_UPDATE_DIALOG;
  update_queue_add(cities_update_callback, NULL);
}

/****************************************************************************
  Request the city to be updated in the city report.
****************************************************************************/
void city_report_dialog_update_city(struct city *pcity)
{
  pcity->client.need_updates |= CU_UPDATE_REPORT;
  update_queue_add(cities_update_callback, NULL);
}


/****************************************************************************
  Update the connection list in the start page.
****************************************************************************/
void conn_list_dialog_update(void)
{
  update_queue_add(Q_CALLBACK(real_conn_list_dialog_update), NULL);
}


/****************************************************************************
  Update the nation report.
****************************************************************************/
void players_dialog_update(void)
{
  update_queue_add(Q_CALLBACK(real_players_dialog_update), NULL);
}

/****************************************************************************
  Update the science report.
****************************************************************************/
void science_report_dialog_update(void)
{
  update_queue_add(Q_CALLBACK(real_science_report_dialog_update), NULL);
}

/****************************************************************************
  Update the economy report.
****************************************************************************/
void economy_report_dialog_update(void)
{
  update_queue_add(Q_CALLBACK(real_economy_report_dialog_update), NULL);
}

/****************************************************************************
  Update the units report.
****************************************************************************/
void units_report_dialog_update(void)
{
  update_queue_add(Q_CALLBACK(real_units_report_dialog_update), NULL);
}
