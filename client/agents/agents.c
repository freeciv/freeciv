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

#include <assert.h>
#include <string.h>

#include "capability.h"
#include "hash.h"
#include "mem.h"
#include "civclient.h"
#include "log.h"
#include "clinet.h"
#include "timing.h"

#include "cma_core.h"
#include "cma_fec.h"

#include "agents.h"

#define DEBUG_REQUEST_IDS		0
#define DEBUG_TODO_LISTS		0
#define META_CALLBACKS_LOGLEVEL		LOG_DEBUG
#define PRINT_STATS_LOGLEVEL		LOG_DEBUG
#define DEBUG_FREEZE			0
#define MAX_AGENTS			10

struct my_agent;

struct call {
  struct my_agent *agent;
  enum oct { OCT_NEW_TURN, OCT_UNIT, OCT_CITY } type;
  enum callback_type cb_type;
  void *arg;
};

#define SPECLIST_TAG call
#define SPECLIST_TYPE struct call
#include "speclist.h"

#define SPECLIST_TAG call
#define SPECLIST_TYPE struct call
#include "speclist_c.h"

#define call_list_iterate(calllist, pcall) \
    TYPED_LIST_ITERATE(struct call, calllist, pcall)
#define call_list_iterate_end  LIST_ITERATE_END

static struct {
  int entries_used;
  struct my_agent {
    struct agent agent;
    int first_outstanding_request_id, last_outstanding_request_id;
    struct {
      struct timer *network_wall_timer;
      int wait_at_network, wait_at_network_requests;
    } stats;
  } entries[MAX_AGENTS];
  struct call_list calls;
} agents;

static int initialized = 0;
static int frozen_level;

/***********************************************************************
...
***********************************************************************/
void agents_init(void)
{
  agents.entries_used = 0;
  call_list_init(&agents.calls);

  /* Add init calls of agents here */
  cma_init();
  cmafec_init();
}

/***********************************************************************
...
***********************************************************************/
void register_agent(struct agent *agent)
{
  struct my_agent *priv_agent = &agents.entries[agents.entries_used];

  assert(agents.entries_used < MAX_AGENTS);
  assert(agent->level > 0);

  memcpy(&priv_agent->agent, agent, sizeof(struct agent));

  priv_agent->first_outstanding_request_id = 0;
  priv_agent->last_outstanding_request_id = 0;

  priv_agent->stats.network_wall_timer = new_timer(TIMER_USER, TIMER_ACTIVE);
  priv_agent->stats.wait_at_network = 0;
  priv_agent->stats.wait_at_network_requests = 0;

  agents.entries_used++;
}

/***********************************************************************
...
***********************************************************************/
static void enqueue_call(struct my_agent *agent,
			 enum oct type,
			 enum callback_type cb_type, void *arg)
{
  struct call *pcall2;

  call_list_iterate(agents.calls, pcall) {
    if (pcall->type == type && pcall->cb_type == cb_type
	&& pcall->arg == arg && pcall->agent == agent) {
      return;
    }
  }
  call_list_iterate_end;

  pcall2 = fc_malloc(sizeof(struct call));

  pcall2->agent = agent;
  pcall2->type = type;
  pcall2->cb_type = cb_type;
  pcall2->arg = arg;

  call_list_insert(&agents.calls, pcall2);

  if (DEBUG_TODO_LISTS) {
    freelog(LOG_NORMAL, "A: adding call");
  }
}

/***********************************************************************
...
***********************************************************************/
static int my_call_sort(const void *a, const void *b)
{
  const struct call *c1 = (const struct call *) *(const void **) a;
  const struct call *c2 = (const struct call *) *(const void **) b;

  return c1->agent->agent.level - c2->agent->agent.level;
}

/***********************************************************************
...
***********************************************************************/
static struct call *remove_and_return_a_call(void)
{
  struct call *result;

  if (call_list_size(&agents.calls) == 0) {
    return NULL;
  }

  /* get calls to agents with low levels first */
  call_list_sort(&agents.calls, my_call_sort);

  result = call_list_get(&agents.calls, 0);
  call_list_unlink(&agents.calls, result);

  if (DEBUG_TODO_LISTS) {
    freelog(LOG_NORMAL, "A: removed call");
  }
  return result;
}

/***********************************************************************
...
***********************************************************************/
static void execute_call(struct call *call)
{
  if (call->type == OCT_NEW_TURN) {
    call->agent->agent.turn_start_notify();
  } else if (call->type == OCT_UNIT) {
    call->agent->agent.unit_callbacks[call->
				      cb_type] ((struct unit *) call->arg);
  } else if (call->type == OCT_CITY) {
    call->agent->agent.city_callbacks[call->
				      cb_type] ((struct city *) call->arg);
  } else
    assert(0);
}

/***********************************************************************
...
***********************************************************************/
static void call_handle_methods(void)
{
  static int currently_running = 0;

  if (currently_running) {
    return;
  }
  if (frozen_level > 0) {
    return;
  }
  currently_running = 1;

  /*
   * The following should ensure that the methods of agents which have
   * a lower level are called first.
   */
  for (;;) {
    struct call *pcall;

    pcall = remove_and_return_a_call();
    if (pcall == NULL) {
      break;
    }

    execute_call(pcall);
    free(pcall);
  }

  currently_running = 0;
}

/***********************************************************************
...
***********************************************************************/
static void freeze(void)
{
  if (!initialized) {
    frozen_level = 0;
    initialized = 1;
  }
  if (DEBUG_FREEZE) {
    freelog(LOG_NORMAL, "A: freeze() current level=%d", frozen_level);
  }
  frozen_level++;
}

/***********************************************************************
...
***********************************************************************/
static void thaw(void)
{
  if (DEBUG_FREEZE) {
    freelog(LOG_NORMAL, "A: thaw() current level=%d", frozen_level);
  }
  frozen_level--;
  assert(frozen_level >= 0);
  if (frozen_level == 0) {
    call_handle_methods();
  }
}

/***********************************************************************
...
***********************************************************************/
static struct my_agent *find_agent_by_name(char *agent_name)
{
  int i;

  for (i = 0; i < agents.entries_used; i++) {
    if (strcmp(agent_name, agents.entries[i].agent.name) == 0)
      return &agents.entries[i];
  }

  assert(0);
  return NULL;
}

/***********************************************************************
...
***********************************************************************/
static int is_outstanding_request(struct my_agent *agent)
{
  if (agent->first_outstanding_request_id &&
      aconnection.client.request_id_of_currently_handled_packet &&
      agent->first_outstanding_request_id <=
      aconnection.client.request_id_of_currently_handled_packet &&
      agent->last_outstanding_request_id >=
      aconnection.client.request_id_of_currently_handled_packet) {
    freelog(LOG_DEBUG,
	    "A:%s: ignoring packet; outstanding [%d..%d] got=%d",
	    agent->agent.name,
	    agent->first_outstanding_request_id,
	    agent->last_outstanding_request_id,
	    aconnection.client.request_id_of_currently_handled_packet);
    return 1;
  }
  return 0;
}

/***********************************************************************
...
***********************************************************************/
static void print_stats(struct my_agent *agent)
{
  freelog(PRINT_STATS_LOGLEVEL,
	  "A:%s: waited %fs in total for network; "
	  "requests=%d; waited %d times",
	  agent->agent.name,
	  read_timer_seconds(agent->stats.network_wall_timer),
	  agent->stats.wait_at_network_requests,
	  agent->stats.wait_at_network);
}

/***********************************************************************
...
***********************************************************************/
void agents_disconnect(void)
{
  freelog(META_CALLBACKS_LOGLEVEL, "agents_disconnect()");
  initialized = 0;
}

/***********************************************************************
...
***********************************************************************/
void agents_processing_started(void)
{
  freelog(META_CALLBACKS_LOGLEVEL, "agents_processing_started()");
  freeze();
}

/***********************************************************************
...
***********************************************************************/
void agents_processing_finished(void)
{
  freelog(META_CALLBACKS_LOGLEVEL, "agents_processing_finished()");
  thaw();
}

/***********************************************************************
...
***********************************************************************/
void agents_game_joined(void)
{
  freelog(META_CALLBACKS_LOGLEVEL, "agents_game_joined()");
  assert(has_capability("turn", aconnection.capability));
  assert(has_capability("attributes", aconnection.capability));
  assert(has_capability("processing_packets", aconnection.capability));
  assert(has_capability("tile_trade", aconnection.capability));
  freeze();
}

/***********************************************************************
...
***********************************************************************/
void agents_game_start(void)
{
  freelog(META_CALLBACKS_LOGLEVEL, "agents_game_start()");
}

/***********************************************************************
...
***********************************************************************/
void agents_before_new_turn(void)
{
  freelog(META_CALLBACKS_LOGLEVEL, "agents_before_new_turn()");
  freeze();
}

/***********************************************************************
...
***********************************************************************/
void agents_start_turn(void)
{
  freelog(META_CALLBACKS_LOGLEVEL, "agents_start_turn()");
  thaw();
}

/***********************************************************************
...
***********************************************************************/
void agents_new_turn(void)
{
  int i;

  for (i = 0; i < agents.entries_used; i++) {
    struct my_agent *agent = &agents.entries[i];

    if (is_outstanding_request(agent)) {
      continue;
    }
    if (agent->agent.turn_start_notify != NULL) {
      enqueue_call(agent, OCT_NEW_TURN, CB_LAST, NULL);
    }
  }
  /*
   * call_handle_methods() isn't called here because the agents are
   * frozen anyway.
   */
}

/***********************************************************************
...
***********************************************************************/
void agents_unit_changed(struct unit *punit)
{
  int i;

  freelog(LOG_DEBUG,
	  "A: agents_unit_changed(unit=%d) type=%s pos=(%d,%d) owner=%s",
	  punit->id, unit_types[punit->type].name, punit->x, punit->y,
	  unit_owner(punit)->name);

  for (i = 0; i < agents.entries_used; i++) {
    struct my_agent *agent = &agents.entries[i];

    if (is_outstanding_request(agent)) {
      continue;
    }
    if (agent->agent.unit_callbacks[CB_CHANGE] != NULL) {
      enqueue_call(agent, OCT_UNIT, CB_CHANGE, punit);
    }
  }
  call_handle_methods();
}

/***********************************************************************
...
***********************************************************************/
void agents_unit_new(struct unit *punit)
{
  int i;

  freelog(LOG_DEBUG,
	  "A: agents_new_unit(unit=%d) type=%s pos=(%d,%d) owner=%s",
	  punit->id, unit_types[punit->type].name, punit->x, punit->y,
	  unit_owner(punit)->name);

  for (i = 0; i < agents.entries_used; i++) {
    struct my_agent *agent = &agents.entries[i];

    if (is_outstanding_request(agent)) {
      continue;
    }
    if (agent->agent.unit_callbacks[CB_NEW] != NULL) {
      enqueue_call(agent, OCT_UNIT, CB_NEW, punit);
    }
  }

  call_handle_methods();
}

/***********************************************************************
...
***********************************************************************/
void agents_unit_remove(struct unit *punit)
{
  int i;

  freelog(LOG_DEBUG,
	  "A: agents_remove_unit(unit=%d) type=%s pos=(%d,%d) owner=%s",
	  punit->id, unit_types[punit->type].name, punit->x, punit->y,
	  unit_owner(punit)->name);

  for (i = 0; i < agents.entries_used; i++) {
    struct my_agent *agent = &agents.entries[i];

    if (is_outstanding_request(agent)) {
      continue;
    }
    if (agent->agent.unit_callbacks[CB_REMOVE] != NULL) {
      enqueue_call(agent, OCT_UNIT, CB_REMOVE, punit);
    }
  }

  call_handle_methods();
}

/***********************************************************************
...
***********************************************************************/
void agents_city_changed(struct city *pcity)
{
  int i;

  freelog(LOG_DEBUG, "A: agents_city_changed(city='%s'(%d)) owner=%s",
	  pcity->name, pcity->id, city_owner(pcity)->name);

  for (i = 0; i < agents.entries_used; i++) {
    struct my_agent *agent = &agents.entries[i];

    if (is_outstanding_request(agent)) {
      continue;
    }
    if (agent->agent.city_callbacks[CB_CHANGE] != NULL) {
      enqueue_call(agent, OCT_CITY, CB_CHANGE, pcity);
    }
  }
  call_handle_methods();
}

/***********************************************************************
...
***********************************************************************/
void agents_city_new(struct city *pcity)
{
  int i;

  freelog(LOG_DEBUG,
	  "A: agents_city_new(city='%s'(%d)) pos=(%d,%d) owner=%s",
	  pcity->name, pcity->id, pcity->x, pcity->y,
	  city_owner(pcity)->name);

  for (i = 0; i < agents.entries_used; i++) {
    struct my_agent *agent = &agents.entries[i];

    if (is_outstanding_request(agent)) {
      continue;
    }
    if (agent->agent.city_callbacks[CB_NEW] != NULL) {
      enqueue_call(agent, OCT_CITY, CB_NEW, pcity);
    }
  }

  call_handle_methods();
}

/***********************************************************************
...
***********************************************************************/
void agents_city_remove(struct city *pcity)
{
  int i;

  freelog(LOG_DEBUG,
	  "A: agents_city_remove(city='%s'(%d)) pos=(%d,%d) owner=%s",
	  pcity->name, pcity->id, pcity->x, pcity->y,
	  city_owner(pcity)->name);

  for (i = 0; i < agents.entries_used; i++) {
    struct my_agent *agent = &agents.entries[i];

    if (is_outstanding_request(agent)) {
      continue;
    }
    if (agent->agent.city_callbacks[CB_REMOVE] != NULL) {
      enqueue_call(agent, OCT_CITY, CB_REMOVE, pcity);
    }
  }

  call_handle_methods();
}

/***********************************************************************
...
***********************************************************************/
void wait_for_requests(char *agent_name, int first_request_id,
		       int last_request_id)
{
  struct my_agent *agent = find_agent_by_name(agent_name);

  if (DEBUG_REQUEST_IDS) {
    freelog(LOG_NORMAL, "A:%s: wait_for_request(ids=[%d..%d])",
	    agent->agent.name, first_request_id, last_request_id);
  }

  assert(first_request_id != 0 && last_request_id != 0
	 && first_request_id <= last_request_id);
  assert(agent->first_outstanding_request_id == 0);
  agent->first_outstanding_request_id = first_request_id;
  agent->last_outstanding_request_id = last_request_id;

  start_timer(agent->stats.network_wall_timer);
  wait_till_request_got_processed(last_request_id);
  stop_timer(agent->stats.network_wall_timer);

  agent->stats.wait_at_network++;
  agent->stats.wait_at_network_requests +=
      (1 + (last_request_id - first_request_id));

  if (DEBUG_REQUEST_IDS) {
    freelog(LOG_NORMAL, "A:%s: wait_for_request: ids=[%d..%d]; got it",
	    agent->agent.name, first_request_id, last_request_id);
  }

  agent->first_outstanding_request_id = 0;

  print_stats(agent);
}

/***********************************************************************
...
***********************************************************************/
void cause_a_unit_changed_for_agent(char *name_of_calling_agent,
				    struct unit *punit)
{
  struct my_agent *agent = find_agent_by_name(name_of_calling_agent);

  assert(agent->agent.unit_callbacks[CB_CHANGE] != NULL);
  enqueue_call(agent, OCT_UNIT, CB_CHANGE, punit);
  call_handle_methods();
}

/***********************************************************************
...
***********************************************************************/
void cause_a_city_changed_for_agent(char *name_of_calling_agent,
				    struct city *pcity)
{
  struct my_agent *agent = find_agent_by_name(name_of_calling_agent);

  assert(agent->agent.city_callbacks[CB_CHANGE] != NULL);
  enqueue_call(agent, OCT_CITY, CB_CHANGE, pcity);
  call_handle_methods();
}
