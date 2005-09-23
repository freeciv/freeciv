/********************************************************************** 
 Freeciv - Copyright (C) 2001 - R. Falke
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

#include <assert.h>
#include <string.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include "city.h"
#include "dataio.h"
#include "events.h"
#include "fcintl.h"
#include "government.h"
#include "hash.h"
#include "log.h"
#include "mem.h"
#include "packets.h"
#include "shared.h"		/* for MIN() */
#include "support.h"
#include "timing.h"

#include "agents.h"
#include "attribute.h"
#include "chatline_g.h"
#include "citydlg_g.h"
#include "cityrep_g.h"
#include "civclient.h"
#include "climisc.h"
#include "clinet.h"
#include "messagewin_g.h"
#include "packhand.h"

#include "cma_core.h"

/*
 * The CMA is an agent. The CMA will subscribe itself to all city
 * events. So if a city changes the callback function city_changed is
 * called. handle_city will be called from city_changed to update the
 * given city. handle_city will call cma_query_result and
 * apply_result_on_server to update the server city state.
 */

/****************************************************************************
 defines, structs, globals, forward declarations
*****************************************************************************/

#define APPLY_RESULT_LOG_LEVEL				LOG_DEBUG
#define HANDLE_CITY_LOG_LEVEL				LOG_DEBUG
#define HANDLE_CITY_LOG_LEVEL2				LOG_DEBUG
#define RESULTS_ARE_EQUAL_LOG_LEVEL			LOG_DEBUG

#define SHOW_TIME_STATS                                 FALSE
#define SHOW_APPLY_RESULT_ON_SERVER_ERRORS              FALSE
#define ALWAYS_APPLY_AT_SERVER                          FALSE

#define SAVED_PARAMETER_SIZE				29

/*
 * Misc statistic to analyze performance.
 */
static struct {
  struct timer *wall_timer;
  int apply_result_ignored, apply_result_applied, refresh_forced;
} stats;

#define my_city_map_iterate(pcity, cx, cy) {                           \
  city_map_checked_iterate(pcity->tile, cx, cy, _ptile) { \
    if(!is_city_center(cx, cy)) {

#define my_city_map_iterate_end \
    }                                \
  } city_map_checked_iterate_end;    \
}


#define T(x) if (result1->x != result2->x) { \
	freelog(RESULTS_ARE_EQUAL_LOG_LEVEL, #x); \
	return FALSE; }

/****************************************************************************
 Returns TRUE iff the two results are equal. Both results have to be
 results for the given city.
*****************************************************************************/
static bool results_are_equal(struct city *pcity,
			     const struct cm_result *const result1,
			     const struct cm_result *const result2)
{
  enum cm_stat stat;

  T(disorder);
  T(happy);
  T(specialists[SP_ELVIS]);
  T(specialists[SP_SCIENTIST]);
  T(specialists[SP_TAXMAN]);

  for (stat = 0; stat < NUM_STATS; stat++) {
    T(surplus[stat]);
  }

  my_city_map_iterate(pcity, x, y) {
    if (result1->worker_positions_used[x][y] !=
	result2->worker_positions_used[x][y]) {
      freelog(RESULTS_ARE_EQUAL_LOG_LEVEL, "worker_positions_used");
      return FALSE;
    }
  } my_city_map_iterate_end;

  return TRUE;
}

#undef T


/****************************************************************************
 Copy the current city state (citizen assignment, production stats and
 happy state) in the given result.
*****************************************************************************/
static void get_current_as_result(struct city *pcity,
				  struct cm_result *result)
{
  int worker = 0, specialist = 0;

  memset(result->worker_positions_used, 0,
	 sizeof(result->worker_positions_used));

  my_city_map_iterate(pcity, x, y) {
    result->worker_positions_used[x][y] =
	(pcity->city_map[x][y] == C_TILE_WORKER);
    if (result->worker_positions_used[x][y]) {
      worker++;
    }
  } my_city_map_iterate_end;

  specialist_type_iterate(sp) {
    result->specialists[sp] = pcity->specialists[sp];
    specialist += pcity->specialists[sp];
  } specialist_type_iterate_end;

  assert(worker + specialist == pcity->size);

  result->found_a_valid = TRUE;

  cm_copy_result_from_city(pcity, result);
}

/****************************************************************************
  Returns TRUE if the city is valid for CMA. Fills parameter if TRUE
  is returned. Parameter can be NULL.
*****************************************************************************/
static bool check_city(int city_id, struct cm_parameter *parameter)
{
  struct city *pcity = find_city_by_id(city_id);
  struct cm_parameter dummy;

  if (!parameter) {
    parameter = &dummy;
  }

  if (!pcity
      || !cma_get_parameter(ATTR_CITY_CMA_PARAMETER, city_id, parameter)) {
    return FALSE;
  }

  if (city_owner(pcity) != game.player_ptr) {
    cma_release_city(pcity);
    create_event(pcity->tile, E_CITY_CMA_RELEASE,
		 _("CMA: You lost control of %s. Detaching from city."),
		 pcity->name);
    return FALSE;
  }

  return TRUE;
}  

/****************************************************************************
 Change the actual city setting to the given result. Returns TRUE iff
 the actual data matches the calculated one.
*****************************************************************************/
static bool apply_result_on_server(struct city *pcity,
				   const struct cm_result *const result)
{
  int first_request_id = 0, last_request_id = 0, i, sp;
  struct cm_result current_state;
  bool success;

  assert(result->found_a_valid);
  get_current_as_result(pcity, &current_state);

  if (results_are_equal(pcity, result, &current_state)
      && !ALWAYS_APPLY_AT_SERVER) {
    stats.apply_result_ignored++;
    return TRUE;
  }

  stats.apply_result_applied++;

  freelog(APPLY_RESULT_LOG_LEVEL, "apply_result(city='%s'(%d))",
	  pcity->name, pcity->id);

  connection_do_buffer(&aconnection);

  /* Do checks */
  if (pcity->size != (cm_count_worker(pcity, result)
		      + cm_count_specialist(pcity, result))) {
    cm_print_city(pcity);
    cm_print_result(pcity, result);
    assert(0);
  }

  /* Remove all surplus workers */
  my_city_map_iterate(pcity, x, y) {
    if ((pcity->city_map[x][y] == C_TILE_WORKER) &&
	!result->worker_positions_used[x][y]) {
      freelog(APPLY_RESULT_LOG_LEVEL, "Removing worker at %d,%d.", x, y);
      last_request_id = city_toggle_worker(pcity, x, y);
      if (first_request_id == 0) {
	first_request_id = last_request_id;
      }
    }
  } my_city_map_iterate_end;

  /* Change the excess non-elvis specialists to elvises. */
  assert(SP_ELVIS == 0);
  for (sp = 1; sp < SP_COUNT; sp++) {
    for (i = 0; i < pcity->specialists[sp] - result->specialists[sp]; i++) {
      freelog(APPLY_RESULT_LOG_LEVEL, "Change specialist from %d to %d.",
	      sp, SP_ELVIS);
      last_request_id = city_change_specialist(pcity, sp, SP_ELVIS);
      if (first_request_id == 0) {
	first_request_id = last_request_id;
      }
    }
  }

  /* now all surplus people are enterainers */

  /* Set workers */
  /* FIXME: This code assumes that any toggled worker will turn into an
   * elvis! */
  my_city_map_iterate(pcity, x, y) {
    if (result->worker_positions_used[x][y] &&
	pcity->city_map[x][y] != C_TILE_WORKER) {
      assert(pcity->city_map[x][y] == C_TILE_EMPTY);
      freelog(APPLY_RESULT_LOG_LEVEL, "Putting worker at %d,%d.", x, y);
      last_request_id = city_toggle_worker(pcity, x, y);
      if (first_request_id == 0) {
	first_request_id = last_request_id;
      }
    }
  } my_city_map_iterate_end;

  /* Set all specialists except SP_ELVIS (all the unchanged ones remain
   * as elvises). */
  assert(SP_ELVIS == 0);
  for (sp = 1; sp < SP_COUNT; sp++) {
    for (i = 0; i < result->specialists[sp] - pcity->specialists[sp]; i++) {
      freelog(APPLY_RESULT_LOG_LEVEL, "Changing specialist from %d to %d.",
	      SP_ELVIS, sp);
      last_request_id = city_change_specialist(pcity, SP_ELVIS, sp);
      if (first_request_id == 0) {
	first_request_id = last_request_id;
      }
    }
  }

  if (last_request_id == 0 || ALWAYS_APPLY_AT_SERVER) {
      /*
       * If last_request is 0 no change request was send. But it also
       * means that the results are different or the results_are_equal
       * test at the start of the function would be true. So this
       * means that the client has other results for the same
       * allocation of citizen than the server. We just send a
       * PACKET_CITY_REFRESH to bring them in sync.
       */
    first_request_id = last_request_id =
	dsend_packet_city_refresh(&aconnection, pcity->id);
    stats.refresh_forced++;
  }
  reports_freeze_till(last_request_id);

  connection_do_unbuffer(&aconnection);

  if (last_request_id != 0) {
    int city_id = pcity->id;

    wait_for_requests("CMA", first_request_id, last_request_id);
    if (!check_city(city_id, NULL)) {
      return FALSE;
    }
  }

  /* Return. */
  get_current_as_result(pcity, &current_state);

  freelog(APPLY_RESULT_LOG_LEVEL, "apply_result: return");

  success = results_are_equal(pcity, result, &current_state);
  if (!success) {
    cm_clear_cache(pcity);

    if (SHOW_APPLY_RESULT_ON_SERVER_ERRORS) {
      freelog(LOG_NORMAL, "expected");
      cm_print_result(pcity, result);
      freelog(LOG_NORMAL, "got");
      cm_print_result(pcity, &current_state);
    }
  }
  return success;
}

/****************************************************************************
 Prints the data of the stats struct via freelog(LOG_NORMAL,...).
*****************************************************************************/
static void report_stats(void)
{
#if SHOW_TIME_STATS
  int total, per_mill;

  freelog(LOG_NORMAL, "CMA: overall=%fs queries=%d %fms / query",
	  read_timer_seconds(stats.wall_timer), stats.queries,
	  (1000.0 * read_timer_seconds(stats.wall_timer)) /
	  ((double) stats.queries));
  total = stats.apply_result_ignored + stats.apply_result_applied;
  per_mill = (stats.apply_result_ignored * 1000) / (total ? total : 1);

  freelog(LOG_NORMAL,
	  "CMA: apply_result: ignored=%2d.%d%% (%d) "
	  "applied=%2d.%d%% (%d) total=%d",
	  per_mill / 10, per_mill % 10, stats.apply_result_ignored,
	  (1000 - per_mill) / 10, (1000 - per_mill) % 10,
	  stats.apply_result_applied, total);
#endif
}

/****************************************************************************
...
*****************************************************************************/
static void release_city(int city_id)
{
  attr_city_set(ATTR_CITY_CMA_PARAMETER, city_id, 0, NULL);
}

/****************************************************************************
                           algorithmic functions
*****************************************************************************/

/****************************************************************************
 The given city has changed. handle_city ensures that either the city
 follows the set CMA goal or that the CMA detaches itself from the
 city.
*****************************************************************************/
static void handle_city(struct city *pcity)
{
  struct cm_result result;
  bool handled;
  int i, city_id = pcity->id;

  freelog(HANDLE_CITY_LOG_LEVEL,
	  "handle_city(city='%s'(%d) pos=(%d,%d) owner=%s)", pcity->name,
	  pcity->id, TILE_XY(pcity->tile), city_owner(pcity)->name);

  freelog(HANDLE_CITY_LOG_LEVEL2, "START handle city='%s'(%d)",
	  pcity->name, pcity->id);

  handled = FALSE;
  for (i = 0; i < 5; i++) {
    struct cm_parameter parameter;

    freelog(HANDLE_CITY_LOG_LEVEL2, "  try %d", i);

    if (!check_city(city_id, &parameter)) {
      handled = TRUE;	
      break;
    }

    pcity = find_city_by_id(city_id);

    cm_query_result(pcity, &parameter, &result);
    if (!result.found_a_valid) {
      freelog(HANDLE_CITY_LOG_LEVEL2, "  no valid found result");

      cma_release_city(pcity);

      create_event(pcity->tile, E_CITY_CMA_RELEASE,
		   _("CMA: The agent can't fulfill the requirements "
		     "for %s. Passing back control."), pcity->name);
      handled = TRUE;
      break;
    } else {
      if (!apply_result_on_server(pcity, &result)) {
	freelog(HANDLE_CITY_LOG_LEVEL2, "  doesn't cleanly apply");
	if (check_city(city_id, NULL) && i == 0) {
	  create_event(pcity->tile, E_NOEVENT,
		       _("CMA: %s has changed and the calculated "
			 "result can't be applied. Will retry."),
		       pcity->name);
	}
      } else {
	freelog(HANDLE_CITY_LOG_LEVEL2, "  ok");
	/* Everything ok */
	handled = TRUE;
	break;
      }
    }
  }

  pcity = find_city_by_id(city_id);

  if (!handled) {
    assert(pcity != NULL);
    freelog(HANDLE_CITY_LOG_LEVEL2, "  not handled");

    create_event(pcity->tile, E_CITY_CMA_RELEASE,
		 _("CMA: %s has changed multiple times. This may be "
		   "an error in Freeciv or bad luck. The CMA will detach "
		   "itself from the city now."), pcity->name);

    cma_release_city(pcity);

    freelog(LOG_ERROR, "CMA: %s has changed multiple times due to "
            "an error in Freeciv. Please send a savegame that can reproduce "
            "this bug to <bugs@freeciv.org>. Thank you.", pcity->name);
  }

  freelog(HANDLE_CITY_LOG_LEVEL2, "END handle city=(%d)", city_id);
}

/****************************************************************************
 Callback for the agent interface.
*****************************************************************************/
static void city_changed(int city_id)
{
  struct city *pcity = find_city_by_id(city_id);

  if (pcity) {
    cm_clear_cache(pcity);
    handle_city(pcity);
  }
}

/****************************************************************************
 Callback for the agent interface.
*****************************************************************************/
static void city_remove(int city_id)
{
  release_city(city_id);
}

/****************************************************************************
 Callback for the agent interface.
*****************************************************************************/
static void new_turn(void)
{
  report_stats();
}

/*************************** public interface *******************************/
/****************************************************************************
...
*****************************************************************************/
void cma_init(void)
{
  struct agent self;
  struct timer *timer = stats.wall_timer;

  freelog(LOG_DEBUG, "sizeof(struct cm_result)=%d",
	  (unsigned int) sizeof(struct cm_result));
  freelog(LOG_DEBUG, "sizeof(struct cm_parameter)=%d",
	  (unsigned int) sizeof(struct cm_parameter));

  /* reset cache counters */
  memset(&stats, 0, sizeof(stats));
  stats.wall_timer = renew_timer(timer, TIMER_USER, TIMER_ACTIVE);

  memset(&self, 0, sizeof(self));
  strcpy(self.name, "CMA");
  self.level = 1;
  self.city_callbacks[CB_CHANGE] = city_changed;
  self.city_callbacks[CB_NEW] = city_changed;
  self.city_callbacks[CB_REMOVE] = city_remove;
  self.turn_start_notify = new_turn;
  register_agent(&self);
}

/****************************************************************************
...
*****************************************************************************/
bool cma_apply_result(struct city *pcity,
		     const struct cm_result *const result)
{
  assert(!cma_is_city_under_agent(pcity, NULL));
  if (result->found_a_valid) {
    return apply_result_on_server(pcity, result);
  } else
    return TRUE; /* ???????? */
}

/****************************************************************************
...
*****************************************************************************/
void cma_put_city_under_agent(struct city *pcity,
			      const struct cm_parameter *const parameter)
{
  freelog(LOG_DEBUG, "cma_put_city_under_agent(city='%s'(%d))",
	  pcity->name, pcity->id);

  assert(city_owner(pcity) == game.player_ptr);

  cma_set_parameter(ATTR_CITY_CMA_PARAMETER, pcity->id, parameter);

  cause_a_city_changed_for_agent("CMA", pcity);

  freelog(LOG_DEBUG, "cma_put_city_under_agent: return");
}

/****************************************************************************
...
*****************************************************************************/
void cma_release_city(struct city *pcity)
{
  release_city(pcity->id);
  refresh_city_dialog(pcity);
  city_report_dialog_update_city(pcity);
}

/****************************************************************************
...
*****************************************************************************/
bool cma_is_city_under_agent(const struct city *pcity,
			     struct cm_parameter *parameter)
{
  struct cm_parameter my_parameter;

  if (!cma_get_parameter(ATTR_CITY_CMA_PARAMETER, pcity->id, &my_parameter)) {
    return FALSE;
  }

  if (parameter) {
    memcpy(parameter, &my_parameter, sizeof(struct cm_parameter));
  }
  return TRUE;
}

/**************************************************************************
  Get the parameter.

  Don't bother to cm_init_parameter, since we set all the fields anyway.
  But leave the comment here so we can find this place when searching
  for all the creators of a parameter.
**************************************************************************/
bool cma_get_parameter(enum attr_city attr, int city_id,
		       struct cm_parameter *parameter)
{
  size_t len;
  char buffer[SAVED_PARAMETER_SIZE];
  struct data_in din;
  int i, version, dummy;

  /* Changing this function is likely to break compatability with old
   * savegames that store these values. */

  len = attr_city_get(attr, city_id, sizeof(buffer), buffer);
  if (len == 0) {
    return FALSE;
  }
  assert(len == SAVED_PARAMETER_SIZE);

  dio_input_init(&din, buffer, len);

  dio_get_uint8(&din, &version);
  assert(version == 2);

  for (i = 0; i < NUM_STATS; i++) {
    dio_get_sint16(&din, &parameter->minimal_surplus[i]);
    dio_get_sint16(&din, &parameter->factor[i]);
  }

  dio_get_sint16(&din, &parameter->happy_factor);
  dio_get_uint8(&din, &dummy); /* Dummy value; used to be factor_target. */
  dio_get_bool8(&din, &parameter->require_happy);
  /* These options are only for server-AI use. */
  parameter->allow_disorder = FALSE;
  parameter->allow_specialists = TRUE;

  return TRUE;
}

/**************************************************************************
 ...
**************************************************************************/
void cma_set_parameter(enum attr_city attr, int city_id,
		       const struct cm_parameter *parameter)
{
  char buffer[SAVED_PARAMETER_SIZE];
  struct data_out dout;
  int i;

  /* Changing this function is likely to break compatability with old
   * savegames that store these values. */

  dio_output_init(&dout, buffer, sizeof(buffer));

  dio_put_uint8(&dout, 2);

  for (i = 0; i < NUM_STATS; i++) {
    dio_put_sint16(&dout, parameter->minimal_surplus[i]);
    dio_put_sint16(&dout, parameter->factor[i]);
  }

  dio_put_sint16(&dout, parameter->happy_factor);
  dio_put_uint8(&dout, 0); /* Dummy value; used to be factor_target. */
  dio_put_bool8(&dout, parameter->require_happy);

  assert(dio_output_used(&dout) == SAVED_PARAMETER_SIZE);

  attr_city_set(attr, city_id, SAVED_PARAMETER_SIZE, buffer);
}
