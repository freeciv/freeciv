/********************************************************************** 
 Freeciv - Copyright (C) 2004 - The Freeciv Team
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
#include <ctype.h>
#include <string.h>

#include "fcintl.h"
#include "log.h"
#include "mem.h"
#include "support.h"
#include "shared.h" /* ARRAY_SIZE */

#include "city.h"
#include "effects.h"
#include "game.h"
#include "government.h"
#include "improvement.h"
#include "map.h"
#include "packets.h"
#include "player.h"
#include "tech.h"
 
/* Names of effect types.
 * (These must correspond to enum effect_type_id in effects.h.) */
static const char *effect_type_names[EFT_LAST] = {
  "Tech_Parasite",
  "Airlift",
  "Any_Government",
  "Capital_City",
  "Corrupt_Pct",
  "Waste_Pct",
  "Enable_Nuke",
  "Enable_Space",
  "Food_Add_Tile",
  /* TODO: "Food_Bonus", */
  /* TODO: "Food_Pct", */
  "Food_Inc_Tile",
  "Food_Per_Tile",
  "Force_Content",
  /* TODO: "Force_Content_Pct", */
  "Give_Imm_Tech",
  "Growth_Food",
  "Have_Embassies",
  "Luxury_Bonus",
  /* TODO: "Luxury_Pct", */
  "Make_Content",
  "Make_Content_Mil",
  "Make_Content_Mil_Per",
  /* TODO: "Make_Content_Pct", */
  "Make_Happy",
  "No_Anarchy",
  "No_Sink_Deep",
  "Nuke_Proof",
  /* TODO: "Pollu_Adj", */
  /* TODO: "Pollu_Pct", */
  /* TODO: "Pollu_Pop_Adj", */
  "Pollu_Pop_Pct",
  /* TODO: "Pollu_Prod_Adj", */
  "Pollu_Prod_Pct",
  "Prod_Add_Tile",
  "Prod_Bonus",
  /* TODO: "Prod_Pct", */
  "Prod_Inc_Tile",
  "Prod_Per_Tile",
  "Prod_To_Gold",
  "Reveal_Cities",
  "Reveal_Map",
  /* TODO: "Incite_Dist_Adj", */
  "Incite_Dist_Pct",
  "Science_Bonus",
  /* TODO: "Science_Pct", */
  "Size_Adj",
  "Size_Unlimit",
  "SS_Structural",
  "SS_Component",
  "SS_Module",
  "Spy_Resistant",
  "Tax_Bonus",
  /* TODO: "Tax_Pct", */
  "Trade_Add_Tile",
  /* TODO: "Trade_Bonus", */
  /* TODO: "Trade_Pct", */
  "Trade_Inc_Tile",
  "Trade_Per_Tile",
  "Sea_Move",
  "Unit_No_Lose_Pop",
  "Unit_Recover",
  "Upgrade_Unit",
  "Upkeep_Free",
  "No_Unhappy",
  "Land_Veteran",
  "Sea_Veteran",
  "Air_Veteran",
  "Land_Vet_Combat",
  /* TODO: "Sea_Vet_Combat", */
  /* TODO: "Air_Vet_Combat", */
  "Land_Regen",
  "Sea_Regen",
  "Air_Regen",
  "Land_Defend",
  "Sea_Defend",
  "Air_Defend",
  "Missile_Defend",
  "No_Incite",
  "Regen_Reputation",
  "Gain_AI_Love"
};

/**************************************************************************
  Convert effect type names to enum; case insensitive;
  returns EFT_LAST if can't match.
**************************************************************************/
enum effect_type effect_type_from_str(const char *str)
{
  enum effect_type effect_type;

  assert(ARRAY_SIZE(effect_type_names) == EFT_LAST);

  for (effect_type = 0; effect_type < EFT_LAST; effect_type++) {
    if (0 == mystrcasecmp(effect_type_names[effect_type], str)) {
      return effect_type;
    }
  }

  return EFT_LAST;
}

/**************************************************************************
  Return the name for an effect type enumeration.  Returns NULL if the
  effect_type is invalid.  This is the inverse of effect_type_from_str.
**************************************************************************/
const char *effect_type_name(enum effect_type effect_type)
{
  assert(ARRAY_SIZE(effect_type_names) == EFT_LAST);
  if (effect_type >= 0 && effect_type < EFT_LAST) {
    return effect_type_names[effect_type];
  } else {
    assert(0);
    return NULL;
  }
}

/**************************************************************************
  The code creates a ruleset cache on ruleset load. This constant cache
  is used to speed up effects queries.  After the cache is created it is
  not modified again (though it may later be freed).

  Since the cache is constant, the server only needs to send effects data to
  the client upon connect. It also means that an AI can do fast searches in
  the effects space by trying the possible combinations of addition or
  removal of buildings with the effects it cares about.


  To know how much a target is being affected, simply use the convenience
  functions:

  * get_player_bonus
  * get_city_bonus
  * get_city_tile_bonus
  * get_building_bonus

  These functions require as arguments the target and the effect type to be
  queried.

  Effect sources are unique and at a well known place in the
  data structures.  This allows the above queries to be fast:
    - Look up the possible sources for the effect (O(1) lookup)
    - For each source, find out if it is present (O(1) lookup per source).
  The first is commonly called the "ruleset cache" and is stored statically
  in this file.  The second is the "sources cache" and is stored all over.

  Any type of effect range and "survives" is possible if we have a sources
  cache for that combination.  For instance
    - There is a sources cache of all existing buildings in a city; thus any
      building effect in a city can have city range.
    - There is a sources cache of all wonders in the world; thus any wonder
      effect can have world range.
    - There is a sources cache of all wonders for each player; thus any
      wonder effect can have player range.
    - There is a sources cache of all wonders ever built; thus any wonder
      effect that survives can have world range.
  However there is no sources cache for many of the possible sources.  For
  instance non-unique buildings do not have a world-range sources cahce, so
  you can't have a non-wonder building have a world-ranged effect.

  The sources caches could easily be extended by generalizing it to a set
  of arrays
    game.buildings[], pplayer->buildings[],
    pisland->builidngs[], pcity->buildings[]
  which would store the number of buildings of that type present by game,
  player, island (continent) or city.  This would allow non-surviving effects
  to come from any building at any range.  However to allow surviving effects
  a second set of arrays would be needed.  This should enable basic support
  for small wonders and satellites.

  No matter which sources caches are present, we should always know where
  to look for a source and so the lookups will always be fast even as the
  number of possible sources increases.
**************************************************************************/

/**************************************************************************
  Ruleset cache. The cache is created during ruleset loading and the data
  is organized to enable fast queries.
**************************************************************************/
static struct {
  /* A single list containing every effect. */
  struct effect_list *tracker;

  /* This array provides a full list of the effects of this type
   * (It's not really a cache, it's the real data.) */
  struct effect_list *effects[EFT_LAST];

  struct {
    /* This cache shows for each building, which effects it provides. */
    struct effect_list *buildings[B_LAST];
  } reqs;
} ruleset_cache;


/**************************************************************************
  Get a list of effects of this type.
**************************************************************************/
static struct effect_list *get_effects(enum effect_type effect_type)
{
  return ruleset_cache.effects[effect_type];
}

/**************************************************************************
  Get a list of effects with this requirement source.

  Note: currently only buildings are supported.
**************************************************************************/
struct effect_list *get_req_source_effects(struct req_source *psource)
{
  switch (psource->type) {
  case REQ_BUILDING:
    return ruleset_cache.reqs.buildings[psource->value.building];
  default:
    return NULL;
  }
}

/**************************************************************************
  Add effect to ruleset cache.
**************************************************************************/
struct effect *effect_new(enum effect_type type, int value)
{
  struct effect *peffect;

  /* Create the effect. */
  peffect = fc_malloc(sizeof(*peffect));
  peffect->type = type;
  peffect->value = value;

  peffect->reqs = requirement_list_new();
  peffect->nreqs = requirement_list_new();

  /* Now add the effect to the ruleset cache. */
  effect_list_append(ruleset_cache.tracker, peffect);
  effect_list_append(get_effects(type), peffect);
  return peffect;
}

/**************************************************************************
  Free effect.
**************************************************************************/
static void effect_free(struct effect *peffect)
{
  requirement_list_iterate(peffect->reqs, preq) {
    free(preq);
  } requirement_list_iterate_end;
  requirement_list_unlink_all(peffect->reqs);
  requirement_list_free(peffect->reqs);

  requirement_list_iterate(peffect->nreqs, preq) {
    free(preq);
  } requirement_list_iterate_end;
  requirement_list_unlink_all(peffect->nreqs);
  requirement_list_free(peffect->nreqs);

  free(peffect);
}

/**************************************************************************
  Append requirement to effect.
**************************************************************************/
void effect_req_append(struct effect *peffect, bool neg,
		       struct requirement *preq)
{
  struct requirement_list *req_list;

  if (neg) {
    req_list = peffect->nreqs;
  } else {
    req_list = peffect->reqs;
  }

  /* Append requirement to the effect. */
  requirement_list_append(req_list, preq);

  /* Add effect to the source's effect list. */
  if (!neg) {
    struct effect_list *eff_list = get_req_source_effects(&preq->source);

    if (eff_list) {
      effect_list_append(eff_list, peffect);
    }
  }
}

/**************************************************************************
  Initialize the ruleset cache.  The ruleset cache should be emtpy
  before this is done (so if it's previously been initialized, it needs
  to be freed (see ruleset_cache_free) before it can be reused).
**************************************************************************/
void ruleset_cache_init(void)
{
  int i;

  ruleset_cache.tracker = effect_list_new();

  for (i = 0; i < ARRAY_SIZE(ruleset_cache.effects); i++) {
    ruleset_cache.effects[i] = effect_list_new();
  }
  for (i = 0; i < ARRAY_SIZE(ruleset_cache.reqs.buildings); i++) {
    ruleset_cache.reqs.buildings[i] = effect_list_new();
  }
}

/**************************************************************************
  Free the ruleset cache.  This should be called at the end of the game or
  when the client disconnects from the server.  See ruleset_cache_init.
**************************************************************************/
void ruleset_cache_free(void)
{
  int i;
  struct effect_list *plist = ruleset_cache.tracker;

  if (plist) {
    effect_list_iterate(plist, peffect) {
      effect_free(peffect);
    } effect_list_iterate_end;
    effect_list_unlink_all(plist);
    effect_list_free(plist);
    ruleset_cache.tracker = NULL;
  }

  for (i = 0; i < ARRAY_SIZE(ruleset_cache.effects); i++) {
    struct effect_list *plist = get_effects(i);

    if (plist) {
      effect_list_unlink_all(plist);
      effect_list_free(plist);
      ruleset_cache.effects[i] = NULL;
    }
  }

  for (i = 0; i < ARRAY_SIZE(ruleset_cache.reqs.buildings); i++) {
    struct req_source source = {
      .type = REQ_BUILDING,
      .value.building = i
    };
    struct effect_list *plist = get_req_source_effects(&source);

    if (plist) {
      effect_list_unlink_all(plist);
      effect_list_free(plist);
      ruleset_cache.reqs.buildings[i] = NULL;
    }
  }
}

/**************************************************************************
  Receives a new effect.  This is called by the client when the packet
  arrives.
**************************************************************************/
void recv_ruleset_effect(struct packet_ruleset_effect *packet)
{
  effect_new(packet->effect_type, packet->effect_value);
}

/**************************************************************************
  Receives a new effect *requirement*.  This is called by the client when
  the packet arrives.
**************************************************************************/
void recv_ruleset_effect_req(struct packet_ruleset_effect_req *packet)
{
  if (packet->effect_id != effect_list_size(ruleset_cache.tracker) - 1) {
    freelog(LOG_ERROR, "Bug in recv_ruleset_effect_req.");
  } else {
    struct effect *peffect = effect_list_get(ruleset_cache.tracker, -1);
    struct requirement req, *preq;

    req = req_from_values(packet->source_type, packet->range, packet->survives,
	packet->source_value);

    preq = fc_malloc(sizeof(*preq));
    *preq = req;

    effect_req_append(peffect, packet->neg, preq);
  }
}

/**************************************************************************
  Send the ruleset cache data over the network.
**************************************************************************/
void send_ruleset_cache(struct conn_list *dest)
{
  unsigned id = 0;

  effect_list_iterate(ruleset_cache.tracker, peffect) {
    struct packet_ruleset_effect packet;

    packet.effect_type = peffect->type;
    packet.effect_value = peffect->value;

    lsend_packet_ruleset_effect(dest, &packet);

    requirement_list_iterate(peffect->reqs, preq) {
      struct packet_ruleset_effect_req packet;
      int type, range, value;
      bool survives;

      req_get_values(preq, &type, &range, &survives, &value);
      packet.effect_id = id;
      packet.neg = FALSE;
      packet.source_type = type;
      packet.source_value = value;
      packet.range = range;
      packet.survives = survives;

      lsend_packet_ruleset_effect_req(dest, &packet);
    } requirement_list_iterate_end;

    requirement_list_iterate(peffect->nreqs, preq) {
      struct packet_ruleset_effect_req packet;
      int type, range, value;
      bool survives;

      req_get_values(preq, &type, &range, &survives, &value);
      packet.effect_id = id;
      packet.neg = TRUE;
      packet.source_type = type;
      packet.source_value = value;
      packet.range = range;
      packet.survives = survives;

      lsend_packet_ruleset_effect_req(dest, &packet);
    } requirement_list_iterate_end;

    id++;
  } effect_list_iterate_end;
}

/**************************************************************************
  Returns a buildable, non-obsolete building that can provide the effect.

  Note: this function is an inefficient hack to be used by the old AI.  It
  will never find wonders, since that's not what the AI wants.
**************************************************************************/
Impr_Type_id ai_find_source_building(struct player *pplayer,
				     enum effect_type effect_type)
{
  /* FIXME: this just returns the first building. it should return the best
   * building instead. */
  effect_list_iterate(get_effects(effect_type), peffect) {
    requirement_list_iterate(peffect->reqs, preq) {
      if (preq->source.type == REQ_BUILDING) {
	Impr_Type_id id = preq->source.value.building;

	if (can_player_build_improvement(pplayer, id)
	    && !improvement_obsolete(pplayer, id)
	    && is_improvement(id)) {
	  return id;
	}
      }
    } requirement_list_iterate_end;
  } effect_list_iterate_end;
  return B_LAST;
}

/**************************************************************************
  Get a building which grants this effect. Returns B_LAST if there is none.
**************************************************************************/
Impr_Type_id get_building_for_effect(enum effect_type effect_type)
{
  effect_list_iterate(get_effects(effect_type), peffect) {
    requirement_list_iterate(peffect->reqs, preq) {
      if (preq->source.type == REQ_BUILDING) {
	return preq->source.value.building;
      }
    } requirement_list_iterate_end;
  } effect_list_iterate_end;
  return B_LAST;
}

/**************************************************************************
  Returns TRUE if the building has any effect bonuses of the given type.

  Note that this function returns a boolean rather than an integer value
  giving the exact bonus.  Finding the exact bonus requires knowing the
  effect range and may take longer.  This function should only be used
  in situations where the range doesn't matter.
**************************************************************************/
bool building_has_effect(Impr_Type_id id, enum effect_type effect)
{
  struct req_source source;
  struct effect_list *plist;

  source.type = REQ_BUILDING;
  source.value.building = id;

  plist = get_req_source_effects(&source);

  if (!plist) {
    return FALSE;
  }

  effect_list_iterate(plist, peffect) {
    if (peffect->type == effect) {
      return TRUE;
    }
  } effect_list_iterate_end;
  return FALSE;
}

/**************************************************************************
  Return TRUE iff any of the disabling requirements for this effect are
  active (an effect is active if all of its enabling requirements and
  none of its disabling ones are active).
**************************************************************************/
bool is_effect_disabled(enum target_type target,
		        const struct player *target_player,
		        const struct city *target_city,
		        Impr_Type_id target_building,
		        const struct tile *target_tile,
		        const struct effect *peffect)
{
  requirement_list_iterate(peffect->nreqs, preq) {
    if (is_req_active(target, target_player, target_city, target_building,
		      target_tile, preq)) {
      return TRUE;
    }
  } requirement_list_iterate_end;
  return FALSE;
}

/**************************************************************************
  Return TRUE iff any of the disabling requirements for this effect are
  active (an effect is active if all of its enabling requirements and
  none of its disabling ones are active).
**************************************************************************/
static bool is_effect_enabled(enum target_type target,
			      const struct player *target_player,
			      const struct city *target_city,
			      Impr_Type_id target_building,
			      const struct tile *target_tile,
			      const struct effect *peffect)
{
  requirement_list_iterate(peffect->reqs, preq) {
    if (!is_req_active(target, target_player, target_city, target_building,
		       target_tile, preq)) {
      return FALSE;
    }
  } requirement_list_iterate_end;
  return TRUE;
}

/**************************************************************************
  Is the effect active at a certain target (player, city or building)?

  This checks whether an effect's requirements are met.

  target gives the type of the target
  (player,city,building,tile) give the exact target
  peffect gives the exact effect value
**************************************************************************/
static bool is_effect_active(enum target_type target,
			     const struct player *plr,
			     const struct city *pcity,
			     Impr_Type_id building,
			     const struct tile *ptile,
			     const struct effect *peffect)
{
  return is_effect_enabled(target, plr, pcity, building, ptile, peffect)
    && !is_effect_disabled(target, plr, pcity, building, ptile, peffect);
}

/**************************************************************************
  Can the effect from the source building be active at a certain target
  (player, city or building)?  This doesn't check if the source exists,
  but tells whether the effect from it would be active if the source did
  exist.  It is thus useful to the AI in figuring out what sources to
  try to obtain.

  target gives the type of the target
  (player,city,building,tile) give the exact target
  source gives the source type of the effect
  peffect gives the exact effect value
**************************************************************************/
bool is_effect_useful(enum target_type target,
		      const struct player *target_player,
		      const struct city *target_city,
		      Impr_Type_id target_building,
		      const struct tile *target_tile,
		      Impr_Type_id source, const struct effect *peffect)
{
  if (is_effect_disabled(target, target_player, target_city,
			 target_building, target_tile, peffect)) {
    return FALSE;
  }
  requirement_list_iterate(peffect->reqs, preq) {
    if (preq->source.type == REQ_BUILDING
	&& preq->source.value.building == source) {
      continue;
    }
    if (!is_req_active(target, target_player, target_city, target_building,
		       target_tile, preq)) {
      return FALSE;
    }
  } requirement_list_iterate_end;
  return TRUE;
}

/**************************************************************************
  Returns TRUE if a building is replaced.  To be replaced, all its effects
  must be made redundant by groups that it is in.
**************************************************************************/
bool is_building_replaced(const struct city *pcity, Impr_Type_id building)
{
  struct req_source source;
  struct effect_list *plist;

  source.type = REQ_BUILDING;
  source.value.building = building;

  plist = get_req_source_effects(&source);

  /* A building that has no effects is never redundant. */
  if (!plist) {
    return FALSE;
  }

  effect_list_iterate(plist, peffect) {
    /* We use TARGET_BUILDING as the lowest common denominator.  Note that
     * the building is its own target - but whether this is actually
     * checked depends on the range of the effect. */
    if (!is_effect_disabled(TARGET_BUILDING, city_owner(pcity), pcity,
			    building, NULL, peffect)) {
      return FALSE;
    }
  } effect_list_iterate_end;
  return TRUE;
}

/**************************************************************************
  Returns the effect bonus of a given type for any target.

  target gives the type of the target
  (player,city,building,tile) give the exact target
  effect_type gives the effect type to be considered

  Returns the effect sources of this type _currently active_.

  The returned vector must be freed (building_vector_free) when the caller
  is done with it.
**************************************************************************/
static int get_target_bonus_effects(struct effect_list *plist,
    				    enum target_type target,
			  	    const struct player *target_player,
				    const struct city *target_city,
				    Impr_Type_id target_building,
				    const struct tile *target_tile,
				    enum effect_type effect_type)
{
  int bonus = 0;

  /* Loop over all effects of this type. */
  effect_list_iterate(get_effects(effect_type), peffect) {
    /* For each effect, see if it is active. */
    if (is_effect_active(target, target_player, target_city,
			 target_building, target_tile, peffect)) {
      /* And if so add on the value. */
      bonus += peffect->value;

      if (plist) {
	effect_list_append(plist, peffect);
      }
    }
  } effect_list_iterate_end;

  return bonus;
}

/**************************************************************************
  Returns the effect bonus for a player.
**************************************************************************/
int get_player_bonus(const struct player *pplayer,
		     enum effect_type effect_type)
{
  return get_target_bonus_effects(NULL, TARGET_PLAYER,
			  	  pplayer, NULL, B_LAST, NULL,
				  effect_type);
}

/**************************************************************************
  Returns the effect bonus at a city.
**************************************************************************/
int get_city_bonus(const struct city *pcity, enum effect_type effect_type)
{
  return get_target_bonus_effects(NULL, TARGET_CITY,
			 	  city_owner(pcity), pcity, B_LAST, NULL,
				  effect_type);
}

/**************************************************************************
  Returns the effect bonus at a city tile.
**************************************************************************/
int get_city_tile_bonus(const struct city *pcity, const struct tile *ptile,
			enum effect_type effect_type)
{
  return get_target_bonus_effects(NULL, TARGET_CITY,
			 	  city_owner(pcity), pcity, B_LAST, ptile,
				  effect_type);
}

/**************************************************************************
  Returns the effect bonus at a building.
**************************************************************************/
int get_building_bonus(const struct city *pcity, Impr_Type_id id,
		       enum effect_type effect_type)
{
  return get_target_bonus_effects(NULL, TARGET_CITY,
			 	  city_owner(pcity), pcity, id, NULL,
				  effect_type);
}

/**************************************************************************
  Returns the effect sources of this type _currently active_ at the player.

  The returned vector must be freed (building_vector_free) when the caller
  is done with it.
**************************************************************************/
int get_player_bonus_effects(struct effect_list *plist,
			     const struct player *pplayer,
			     enum effect_type effect_type)
{
  return get_target_bonus_effects(plist, TARGET_PLAYER,
			  	  pplayer, NULL, B_LAST, NULL,
				  effect_type);
}

/**************************************************************************
  Returns the effect sources of this type _currently active_ at the city.

  The returned vector must be freed (building_vector_free) when the caller
  is done with it.
**************************************************************************/
int get_city_bonus_effects(struct effect_list *plist,
			   const struct city *pcity,
			   enum effect_type effect_type)
{
  return get_target_bonus_effects(plist, TARGET_CITY,
			 	  city_owner(pcity), pcity, B_LAST, NULL,
				  effect_type);
}

/**************************************************************************
  Returns the effect bonus the currently-in-construction-item will provide.

  Note this is not called get_current_production_bonus because that would
  be confused with EFT_PROD_BONUS.
**************************************************************************/
int get_current_construction_bonus(const struct city *pcity,
				   enum effect_type effect_type)
{
  if (!pcity->is_building_unit) {
    Impr_Type_id id = pcity->currently_building;
    int power = 0;

    struct req_source source = {
      .type = REQ_BUILDING,
      .value.building = id
    };
    struct effect_list *plist = get_req_source_effects(&source);

    effect_list_iterate(plist, peffect) {
      if (peffect->type != effect_type) {
	continue;
      }
      if (is_effect_useful(TARGET_BUILDING, city_owner(pcity),
			   pcity, id, NULL, id, peffect)) {
	power += peffect->value;
      }
    } effect_list_iterate_end;

    return power;
  }
  return 0;
}

/**************************************************************************
**************************************************************************/
void get_effect_req_text(struct effect *peffect, char *buf, size_t buf_len)
{
  buf[0] = '\0';

  requirement_list_iterate(peffect->reqs, preq) {
    struct req_source *psource = &preq->source;

    if (buf[0] != '\0') {
      mystrlcat(buf, "+", buf_len);
    }

    switch (psource->type) {
      case REQ_NONE:
	break;
      case REQ_TECH:
	mystrlcat(buf, advances[psource->value.tech].name, buf_len);
	break;
      case REQ_GOV:
	mystrlcat(buf, get_government_name(psource->value.gov), buf_len);
	break;
      case REQ_BUILDING:
	mystrlcat(buf, get_improvement_name(psource->value.building), buf_len);
	break;
      case REQ_SPECIAL:
	mystrlcat(buf, get_special_name(psource->value.special), buf_len);
	break;
      case REQ_TERRAIN:
	mystrlcat(buf, get_terrain_name(psource->value.terrain), buf_len);
	break;
      case REQ_LAST:
	break;
    }
  } requirement_list_iterate_end;
}

