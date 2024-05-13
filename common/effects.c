/***********************************************************************
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
#include <fc_config.h>
#endif

#include <ctype.h>
#include <string.h>

/* utility */
#include "astring.h"
#include "fcintl.h"
#include "log.h"
#include "mem.h"
#include "support.h"
#include "shared.h" /* ARRAY_SIZE */
#include "string_vector.h"

/* common */
#include "city.h"
#include "game.h"
#include "government.h"
#include "improvement.h"
#include "map.h"
#include "packets.h"
#include "player.h"
#include "tech.h"

#include "effects.h"


static bool initialized = FALSE;

struct user_effect {
  enum effect_type ai_value_as;
};

struct user_effect ueffects[EFT_USER_EFFECT_LAST + 1 - EFT_USER_EFFECT_1];

/**************************************************************************
  The code creates a ruleset cache on ruleset load. This constant cache
  is used to speed up effects queries. After the cache is created it is
  not modified again (though it may later be freed).

  Since the cache is constant, the server only needs to send effects data to
  the client upon connect. It also means that an AI can do fast searches in
  the effects space by trying the possible combinations of addition or
  removal of buildings with the effects it cares about.


  To know how much a target is being affected, simply use the convenience
  functions:

  * get_world_bonus()
  * get_player_bonus()
  * get_player_output_bonus()
  * get_city_bonus()
  * get_city_output_bonus()
  * get_city_tile_output_bonus()
  * get_city_specialist_output_bonus()
  * get_tile_bonus()
  * get_tile_output_bonus()
  * get_building_bonus()
  * get_unittype_bonus()
  * get_unit_bonus()
  * get_unit_vs_tile_bonus()

  These functions require as arguments the target and the effect type to be
  queried.

  Effect sources are unique and at a well known place in the
  data structures. This allows the above queries to be fast:
    - Look up the possible sources for the effect (O(1) lookup)
    - For each source, find out if it is present (O(1) lookup per source).
  The first is commonly called the "ruleset cache" and is stored statically
  in this file. The second is the "sources cache" and is stored all over.

  Any type of effect range and "survives" is possible if we have a sources
  cache for that combination. For instance
    - There is a sources cache of all existing buildings in a city; thus any
      building effect in a city can have city range.
    - There is a sources cache of all wonders in the world; thus any wonder
      effect can have world range.
    - There is a sources cache of all wonders for each player; thus any
      wonder effect can have player range.
    - There is a sources cache of all wonders ever built; thus any wonder
      effect that survives can have world range.
  However there is no sources cache for many of the possible sources. For
  instance non-unique buildings do not have a world-range sources cache, so
  you can't have a non-wonder building have a world-ranged effect.

  The sources caches could easily be extended by generalizing it to a set
  of arrays
    game.buildings[], pplayer->buildings[],
    pisland->buildings[], pcity->buildings[]
  which would store the number of buildings of that type present by game,
  player, island (continent) or city. This would allow non-surviving effects
  to come from any building at any range. However to allow surviving effects
  a second set of arrays would be needed. This should enable basic support
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
  struct effect_list *effects[EFT_COUNT];

  struct {
    /* This cache shows for each building, which effects it provides. */
    struct effect_list *buildings[B_LAST];
    /* Same for governments */
    struct effect_list *govs[G_LAST];
    /* ...advances... */
    struct effect_list *advances[A_LAST];
  } reqs;
} ruleset_cache;


/**********************************************************************//**
  Get a list of effects of this type.
**************************************************************************/
struct effect_list *get_effects(enum effect_type effect_type)
{
  return ruleset_cache.effects[effect_type];
}

/**********************************************************************//**
  Get a list of effects with this requirement source.

  Currently only buildings, advances, and governments are supported.
**************************************************************************/
struct effect_list *get_req_source_effects(const struct universal *psource)
{
  int type, value;

  universal_extraction(psource, &type, &value);

  switch (type) {
  case VUT_GOVERNMENT:
    if (value >= 0 && value < government_count()) {
      return ruleset_cache.reqs.govs[value];
    } else {
      return NULL;
    }
  case VUT_IMPROVEMENT:
    if (value >= 0 && value < improvement_count()) {
      return ruleset_cache.reqs.buildings[value];
    } else {
      return NULL;
    }
  case VUT_ADVANCE:
    if (value >= 0 && value < advance_count()) {
      return ruleset_cache.reqs.advances[value];
    } else {
      return NULL;
    }
  default:
    return NULL;
  }
}

/**********************************************************************//**
  Add effect to ruleset cache.
**************************************************************************/
struct effect *effect_new(enum effect_type type, int value,
                          struct multiplier *pmul)
{
  struct effect *peffect;

  /* Create the effect. */
  peffect = fc_malloc(sizeof(*peffect));
  peffect->type = type;
  peffect->value = value;
  peffect->multiplier = pmul;

  requirement_vector_init(&peffect->reqs);

  /* Now add the effect to the ruleset cache. */
  effect_list_append(ruleset_cache.tracker, peffect);
  effect_list_append(get_effects(type), peffect);

  /* Only relevant for ruledit and other rulesave users. */
  peffect->rulesave.do_not_save = FALSE;
  peffect->rulesave.comment = NULL;

  return peffect;
}

/**********************************************************************//**
  Free resources reserved for the effect.

  This does not remove effect from the cache lists.
  See effect_remove() for that.
**************************************************************************/
void effect_free(struct effect *peffect)
{
  requirement_vector_free(&peffect->reqs);
  if (peffect->rulesave.comment != NULL) {
    free(peffect->rulesave.comment);
  }
  free(peffect);
}

/**********************************************************************//**
  Remove effect from the caches.

  This does not free effect itself. See effect_free() for that.
**************************************************************************/
void effect_remove(struct effect *peffect)
{
  effect_list_remove(ruleset_cache.tracker, peffect);
  effect_list_remove(get_effects(peffect->type), peffect);
}

/**********************************************************************//**
  Create copy of the effect. It gets fully registered to
  the ruleset caches.

  @param old           Original effect to copy
  @param override_type Type of the effect to create, or effect_type_invalid()
                       to copy type from the original effect

  @return              Newly created effect
**************************************************************************/
struct effect *effect_copy(struct effect *old,
                           enum effect_type override_type)
{
  struct effect *new_eff;
  enum effect_type type = (effect_type_is_valid(override_type)
                           ? override_type
                           : old->type);

  new_eff = effect_new(type, old->value, old->multiplier);

  requirement_vector_iterate(&old->reqs, preq) {
    effect_req_append(new_eff, *preq);
  } requirement_vector_iterate_end;

  return new_eff;
}

/**********************************************************************//**
  Append requirement to effect.
**************************************************************************/
void effect_req_append(struct effect *peffect, struct requirement req)
{
  struct effect_list *eff_list = get_req_source_effects(&req.source);

  requirement_vector_append(&peffect->reqs, req);

  if (eff_list != NULL) {
    effect_list_append(eff_list, peffect);
  }

  if (req.source.kind == VUT_IMPR_FLAG) {
    improvement_iterate(impr) {
      if (improvement_has_flag(impr, req.source.value.impr_flag)) {
        eff_list = get_req_source_effects(&(const struct universal) {
                                            .kind = VUT_IMPROVEMENT,
                                            .value.building = impr
                                          });

        if (eff_list != NULL) {
          effect_list_append(eff_list, peffect);
        }
      }
    } improvement_iterate_end;
  }
}

/**********************************************************************//**
  Initialize the ruleset cache.  The ruleset cache should be empty
  before this is done (so if it's previously been initialized, it needs
  to be freed (see ruleset_cache_free) before it can be reused).
**************************************************************************/
void ruleset_cache_init(void)
{
  int i;

  initialized = TRUE;

  ruleset_cache.tracker = effect_list_new();

  for (i = 0; i < ARRAY_SIZE(ruleset_cache.effects); i++) {
    ruleset_cache.effects[i] = effect_list_new();
  }
  for (i = 0; i < ARRAY_SIZE(ruleset_cache.reqs.buildings); i++) {
    ruleset_cache.reqs.buildings[i] = effect_list_new();
  }
  for (i = 0; i < ARRAY_SIZE(ruleset_cache.reqs.govs); i++) {
    ruleset_cache.reqs.govs[i] = effect_list_new();
  }
  for (i = 0; i < ARRAY_SIZE(ruleset_cache.reqs.advances); i++) {
    ruleset_cache.reqs.advances[i] = effect_list_new();
  }

  /* By default, user effects are valued as themselves
   * (currently meaning that they get no value at all) */
  for (i = EFT_USER_EFFECT_1 ; i <= EFT_USER_EFFECT_LAST; i++) {
    ueffects[USER_EFFECT_NUMBER(i)].ai_value_as = i;
  }
}

/**********************************************************************//**
  Free the ruleset cache. This should be called at the end of the game or
  when the client disconnects from the server. See ruleset_cache_init().
**************************************************************************/
void ruleset_cache_free(void)
{
  int i;
  struct effect_list *tracker_list = ruleset_cache.tracker;

  if (tracker_list) {
    effect_list_iterate(tracker_list, peffect) {
      effect_free(peffect);
    } effect_list_iterate_end;
    effect_list_destroy(tracker_list);
    ruleset_cache.tracker = NULL;
  }

  for (i = 0; i < ARRAY_SIZE(ruleset_cache.effects); i++) {
    struct effect_list *plist = ruleset_cache.effects[i];

    if (plist) {
      effect_list_destroy(plist);
      ruleset_cache.effects[i] = NULL;
    }
  }

  for (i = 0; i < ARRAY_SIZE(ruleset_cache.reqs.buildings); i++) {
    struct effect_list *plist = ruleset_cache.reqs.buildings[i];

    if (plist) {
      effect_list_destroy(plist);
      ruleset_cache.reqs.buildings[i] = NULL;
    }
  }

  for (i = 0; i < ARRAY_SIZE(ruleset_cache.reqs.govs); i++) {
    struct effect_list *plist = ruleset_cache.reqs.govs[i];

    if (plist) {
      effect_list_destroy(plist);
      ruleset_cache.reqs.govs[i] = NULL;
    }
  }

  for (i = 0; i < ARRAY_SIZE(ruleset_cache.reqs.advances); i++) {
    struct effect_list *plist = ruleset_cache.reqs.advances[i];

    if (plist) {
      effect_list_destroy(plist);
      ruleset_cache.reqs.advances[i] = NULL;
    }
  }

  initialized = FALSE;
}

/**********************************************************************//**
  Get the maximum effect value in this ruleset for the universal
  (that is, the sum of all positive effects clauses that apply specifically
  to this universal -- this can be an overestimate in the case of
  mutually exclusive effects).
  for_uni can be NULL to get max effect value ignoring requirements.
**************************************************************************/
int effect_cumulative_max(enum effect_type type, struct universal *unis,
                          size_t n_unis)
{
  struct effect_list *plist = ruleset_cache.tracker;
  int value = 0;

  fc_assert_ret_val(((unis == NULL && n_unis == 0)
                     || (unis != NULL && n_unis > 0)),
                    0);

  if (plist) {
    effect_list_iterate(plist, peffect) {
      if (peffect->type == type) {
        if (peffect->value > 0) {
          if (unis == NULL
              || !universals_mean_unfulfilled(&(peffect->reqs), unis, n_unis)) {
            value += peffect->value;
          }
        } else if (requirement_vector_size(&peffect->reqs) == 0) {
          /* Always active negative effect */
          value += peffect->value;
        }
      }
    } effect_list_iterate_end;
  }

  return value;
}

/**********************************************************************//**
  Get the minimum effect value in this ruleset for the universal
  (that is, the sum of all negative effects clauses that apply specifically
  to this universal -- this can be an overestimate in the case of
  mutually exclusive effects).
  for_uni can be NULL to get min effect value ignoring requirements.
**************************************************************************/
int effect_cumulative_min(enum effect_type type, struct universal *for_uni)
{
  struct effect_list *plist = ruleset_cache.tracker;
  int value = 0;

  if (plist) {
    effect_list_iterate(plist, peffect) {
      if (peffect->type == type) {
        if (peffect->value < 0) {
          if (for_uni == NULL
              || universal_fulfills_requirements(FALSE, &(peffect->reqs),
                                                 for_uni)) {
            value += peffect->value;
          }
        } else if (requirement_vector_size(&peffect->reqs) == 0) {
          /* Always active positive effect */
          value += peffect->value;
        }
      }
    } effect_list_iterate_end;
  }

  return value;
}

/**********************************************************************//**
  Return the base value of a given effect that can always be expected from
  just the sources in the list, independent of other factors.
  Adds up all the effects that rely only on these universals; effects that
  have extra conditions are ignored. In effect, 'unis' is a template
  against which effects are matched.
  The first universal in the list is special: effects must have a
  condition that specifically applies to that source to be included
  (may be a superset requirement, e.g. ExtraFlag for VUT_EXTRA source).
**************************************************************************/
int effect_value_from_universals(enum effect_type type,
                                 struct universal *unis, size_t n_unis)
{
  int value = 0;
  struct effect_list *el = get_effects(type);

  effect_list_iterate(el, peffect) {
    bool effect_applies = TRUE;
    bool first_source_mentioned = FALSE;

    if (peffect->multiplier) {
      /* Discount any effects with multipliers; we are looking for constant
       * effects */
      continue;
    }

    requirement_vector_iterate(&(peffect->reqs), preq) {
      int i;
      bool req_mentioned_a_source = FALSE;

      for (i = 0; effect_applies && i < n_unis; i++) {
        switch (universal_fulfills_requirement(preq, &(unis[i]))) {
        case ITF_NOT_APPLICABLE:
          /* This req not applicable to this source (but might be relevant
           * to another source in our template). Keep looking. */
          break;
        case ITF_NO:
          req_mentioned_a_source = TRUE; /* this req matched this source */
          if (preq->present) {
            /* Requirement contradicts template. Effect doesn't apply. */
            effect_applies = FALSE;
          } /* but negative req doesn't count for first_source_mentioned */
          break;
        case ITF_YES:
          req_mentioned_a_source = TRUE; /* this req matched this source */
          if (preq->present) {
            if (i == 0) {
              first_source_mentioned = TRUE;
            }
            /* keep looking */
          } else /* !present */ {
            /* Requirement contradicts template. Effect doesn't apply. */
            effect_applies = FALSE;
          }
          break;
        }
      }
      if (!req_mentioned_a_source) {
        /* This requirement isn't relevant to any source in our template,
         * so it's an extra condition and the effect should be ignored. */
        effect_applies = FALSE;
      }
      if (!effect_applies) {
        /* Already known to be irrelevant, bail out early */
        break;
      }
    } requirement_vector_iterate_end;

    if (!first_source_mentioned) {
      /* First source not positively mentioned anywhere in requirements,
       * so ignore this effect */
      continue;
    }
    if (effect_applies) {
      value += peffect->value;
    }
  } effect_list_iterate_end;

  return value;
}

/**********************************************************************//**
  Returns TRUE iff the specified effect type is guaranteed to always have
  a value at or above the specified value given the presence of the
  specified list of universals.
  Note that it may be true that the effect type always will be at or above
  the specified value a even if this function refuses to guarantee it by
  returning FALSE. It may simply be unable to detect that it is so.
  @param type      the effect type
  @param unis      the list of present universals
  @param n_unis    the number of universals in unis
  @param min_value the value the effect always should be at or above
  @return TRUE iff the function promises that the value of the effect type
               never will be below min_value given the specified
               universals.
**************************************************************************/
bool effect_universals_value_never_below(enum effect_type type,
                                         struct universal *unis,
                                         size_t n_unis,
                                         int min_value)
{
  int guaranteed_min_effect_value = 0;

  effect_list_iterate(get_effects(type), peffect) {
    if (universals_mean_unfulfilled(&peffect->reqs, unis, n_unis)) {
      /* Not relevant. */
      continue;
    }

    if (peffect->multiplier) {
      /* Multipliers may change during the game. */
      return FALSE;
    }

    if (universals_say_everything(&peffect->reqs, unis, n_unis)) {
      /* This value is guaranteed to be there since the universals alone
       * fulfill all the requirements in the vector. */
      guaranteed_min_effect_value += peffect->value;
    } else if (peffect->value < 0) {
      /* Can't be ignored. It may apply. It subtracts. */
      guaranteed_min_effect_value += peffect->value;
    }
  } effect_list_iterate_end;

  /* The value of this effect type is never below the guaranteed
   * minimum. */
  return guaranteed_min_effect_value >= min_value;
}

/**********************************************************************//**
  Returns a value that, if found in an effect value, always will make the
  result of any evaluation where it is active positive.
**************************************************************************/
int effect_value_will_make_positive(enum effect_type type)
{
  return 1 + (effect_cumulative_min(type, NULL) * -1);
}

/**********************************************************************//**
  Receives a new effect.  This is called by the client when the packet
  arrives.
**************************************************************************/
void recv_ruleset_effect(const struct packet_ruleset_effect *packet)
{
  struct effect *peffect;
  struct multiplier *pmul;

  pmul = packet->has_multiplier ? multiplier_by_number(packet->multiplier)
                                : NULL;
  peffect = effect_new(packet->effect_type, packet->effect_value, pmul);

  requirement_vector_iterate(&(packet->reqs), preq) {
    effect_req_append(peffect, *preq);
  } requirement_vector_iterate_end;
  fc_assert(peffect->reqs.size == packet->reqs.size);
}

/**********************************************************************//**
  Send the ruleset cache data over the network.
**************************************************************************/
void send_ruleset_cache(struct conn_list *dest)
{
  effect_list_iterate(ruleset_cache.tracker, peffect) {
    struct packet_ruleset_effect effect_packet;

    effect_packet.effect_type = peffect->type;
    effect_packet.effect_value = peffect->value;
    if (peffect->multiplier) {
      effect_packet.has_multiplier = TRUE;
      effect_packet.multiplier = multiplier_number(peffect->multiplier);
    } else {
      effect_packet.has_multiplier = FALSE;
      effect_packet.multiplier = 0; /* arbitrary */
    }

    /* Shallow-copy (borrow) requirement vector */
    effect_packet.reqs = peffect->reqs;

    lsend_packet_ruleset_effect(dest, &effect_packet);
  } effect_list_iterate_end;
}

/**********************************************************************//**
  Returns TRUE if the building has any effect bonuses of the given type.

  Note that this function returns a boolean rather than an integer value
  giving the exact bonus.  Finding the exact bonus requires knowing the
  effect range and may take longer.  This function should only be used
  in situations where the range doesn't matter.
**************************************************************************/
bool building_has_effect(const struct impr_type *pimprove,
                         enum effect_type effect_type)
{
  struct universal source = {
    .kind = VUT_IMPROVEMENT,
    /* just to bamboozle the annoying compiler warning */
    .value = {.building = improvement_by_number(improvement_number(pimprove))}
  };
  struct effect_list *plist = get_req_source_effects(&source);

  if (!plist) {
    return FALSE;
  }

  effect_list_iterate(plist, peffect) {
    if (peffect->type == effect_type) {
      return TRUE;
    }
  } effect_list_iterate_end;

  return FALSE;
}

/**********************************************************************//**
  Return TRUE iff any of the disabling requirements for this effect are
  active, which would prevent it from taking effect.
  (Assumes that any requirement specified in the ruleset with a negative
  sense is an impediment.)

  context may be NULL. This is equivalent to passing an empty context.
**************************************************************************/
static bool is_effect_prevented(const struct req_context *context,
                                const struct req_context *other_context,
                                const struct effect *peffect,
                                const enum   req_problem_type prob_type)
{
  requirement_vector_iterate(&peffect->reqs, preq) {
    /* Only check present=FALSE requirements; these will return _FALSE_
     * from is_req_active() if met, and need reversed prob_type */
    if (!preq->present
        && !is_req_active(context, other_context,
                          preq, REVERSED_RPT(prob_type))) {
      return TRUE;
    }
  } requirement_vector_iterate_end;

  return FALSE;
}

/**********************************************************************//**
  Returns TRUE if a building is replaced. To be replaced, all its effects
  must be made redundant by groups that it is in.
  prob_type CERTAIN or POSSIBLE is answer to function name.
**************************************************************************/
bool is_building_replaced(const struct city *pcity,
                          const struct impr_type *pimprove,
                          const enum req_problem_type prob_type)
{
  struct effect_list *plist;
  struct universal source = {
    .kind = VUT_IMPROVEMENT,
    .value = {.building = pimprove}
  };

  plist = get_req_source_effects(&source);

  /* A building with no effects and no flags is always redundant! */
  if (!plist) {
    return TRUE;
  }

  effect_list_iterate(plist, peffect) {
    /* We use TARGET_BUILDING as the lowest common denominator.  Note that
     * the building is its own target - but whether this is actually
     * checked depends on the range of the effect. */
    /* Prob_type is not reversed here. disabled is equal to replaced, not
     * reverse */
    if (!is_effect_prevented(&(const struct req_context) {
                               .player = city_owner(pcity),
                               .city = pcity,
                               .building = pimprove,
                             },
                             NULL,
                             peffect, prob_type)) {
      return FALSE;
    }
  } effect_list_iterate_end;

  return TRUE;
}

/**********************************************************************//**
  Returns the effect bonus of a given type for any target.

  context gives the target (or targets) to evaluate requirements against
  effect_type gives the effect type to be considered

  context and other_context may be NULL. This is equivalent to passing
  empty contexts.

  Returns the effect sources of this type _currently active_.

  The returned vector must be freed (building_vector_free) when the caller
  is done with it.
**************************************************************************/
int get_target_bonus_effects(struct effect_list *plist,
                             const struct req_context *context,
                             const struct req_context *other_context,
                             enum effect_type effect_type)
{
  int bonus = 0;

  if (context == NULL) {
    context = req_context_empty();
  }

  /* Loop over all effects of this type. */
  effect_list_iterate(get_effects(effect_type), peffect) {
    /* For each effect, see if it is active. */
    if (are_reqs_active(context, other_context,
                        &peffect->reqs, RPT_CERTAIN)) {
      /* This code will add value of effect. If there's multiplier for
       * effect and target_player aren't null, then value is multiplied
       * by player's multiplier factor. */
      if (peffect->multiplier) {
        if (context->player) {
          bonus += (peffect->value
            * player_multiplier_effect_value(context->player,
                                             peffect->multiplier)) / 100;
        }
      } else {
        bonus += peffect->value;
      }

      if (plist) {
        effect_list_append(plist, peffect);
      }
    }
  } effect_list_iterate_end;

  return bonus;
}

/**********************************************************************//**
  Returns the expected value of the effect of given type for given context,
  calculating value weighted with probability for each individual effect
  with given callback using arbitrary additional data passed to it.

  The way of using multipliers is left on the callback.
**************************************************************************/
double get_effect_expected_value(const struct req_context *context,
                                 const struct player *other_player,
                                 enum effect_type effect_type,
                                 eft_value_filter_cb weighter,
                                 void *data, int n_data)
{
  double sum = 0.;

  if (context == NULL) {
    context = req_context_empty();
  }

  /* Loop over all effects of this type. */
  effect_list_iterate(get_effects(effect_type), peffect) {
    sum += weighter(peffect, context, other_player, data, n_data);
  } effect_list_iterate_end;

  return sum;
}

/**********************************************************************//**
  Returns the effect bonus for the whole world.
**************************************************************************/
int get_world_bonus(enum effect_type effect_type)
{
  if (!initialized) {
    return 0;
  }

  return get_target_bonus_effects(NULL, NULL, NULL, effect_type);
}

/**********************************************************************//**
  Returns the effect bonus for a player.
**************************************************************************/
int get_player_bonus(const struct player *pplayer,
                     enum effect_type effect_type)
{
  if (!initialized) {
    return 0;
  }

  return get_target_bonus_effects(NULL,
                                  &(const struct req_context) {
                                    .player = pplayer,
                                  },
                                  NULL,
                                  effect_type);
}

/**********************************************************************//**
  Returns the effect bonus at a city.
**************************************************************************/
int get_city_bonus(const struct city *pcity, enum effect_type effect_type)
{
  if (!initialized) {
    return 0;
  }

  return get_target_bonus_effects(NULL,
                                  &(const struct req_context) {
                                    .player = city_owner(pcity),
                                    .city = pcity,
                                    .tile = city_tile(pcity),
                                  },
                                  NULL, effect_type);
}

/**********************************************************************//**
  Returns the effect bonus at a tile.
  Even if the tile has a city, player requirements are not about city owner
  but tile owner.
**************************************************************************/
int get_tile_bonus(const struct tile *ptile, enum effect_type effect_type)
{
  if (!initialized) {
    return 0;
  }

  return get_target_bonus_effects(NULL,
                                  &(const struct req_context) {
                                    .player = tile_owner(ptile),
                                    .city = tile_city(ptile),
                                    .tile = ptile,
                                  },
                                  NULL,
                                  effect_type);
}

/**********************************************************************//**
  Returns the effect bonus of a specialist in a city.
**************************************************************************/
int get_city_specialist_output_bonus(const struct city *pcity,
                                     const struct specialist *pspecialist,
                                     const struct output_type *poutput,
                                     enum effect_type effect_type)
{
  fc_assert_ret_val(pcity != NULL, 0);
  fc_assert_ret_val(pspecialist != NULL, 0);
  fc_assert_ret_val(poutput != NULL, 0);
  return get_target_bonus_effects(NULL,
                                  &(const struct req_context) {
                                    .player = city_owner(pcity),
                                    .city = pcity,
                                    .output = poutput,
                                    .specialist = pspecialist,
                                  },
                                  NULL,
                                  effect_type);
}

/**********************************************************************//**
  Returns the effect bonus at a city tile.
  pcity must be supplied.

  FIXME: this is now used both for tile bonuses, tile-output bonuses,
  and city-output bonuses. Thus ptile or poutput may be NULL for
  certain callers. This could be changed by adding 2 new functions to
  the interface but they'd be almost identical and their likely names
  would conflict with functions already in city.c.
  It's also very similar to get_tile_output_bonus(); it should be
  called when the city is mandatory.
**************************************************************************/
int get_city_tile_output_bonus(const struct city *pcity,
                               const struct tile *ptile,
                               const struct output_type *poutput,
                               enum effect_type effect_type)
{
  fc_assert_ret_val(pcity != NULL, 0);
  return get_target_bonus_effects(NULL,
                                  &(const struct req_context) {
                                    .player = city_owner(pcity),
                                    .city = pcity,
                                    .tile = ptile,
                                    .output = poutput,
                                  },
                                  NULL,
                                  effect_type);
}

/**********************************************************************//**
  Returns the effect bonus at a tile for given output type (or NULL for
  output-type-independent bonus).
  If pcity is supplied, it's the bonus for that particular city, otherwise
  it's the player/city-independent bonus (and any city on the tile is
  ignored).
**************************************************************************/
int get_tile_output_bonus(const struct city *pcity,
                          const struct tile *ptile,
                          const struct output_type *poutput,
                          enum effect_type effect_type)
{
  const struct player *pplayer = pcity ? city_owner(pcity) : NULL;

  return get_target_bonus_effects(NULL,
                                  &(const struct req_context) {
                                    .player = pplayer,
                                    .city = pcity,
                                    .tile = ptile,
                                    .output = poutput,
                                  },
                                  NULL,
                                  effect_type);
}

/**********************************************************************//**
  Returns the player effect bonus of an output.
**************************************************************************/
int get_player_output_bonus(const struct player *pplayer,
                            const struct output_type *poutput,
                            enum effect_type effect_type)
{
  if (!initialized) {
    return 0;
  }

  fc_assert_ret_val(pplayer != NULL, 0);
  fc_assert_ret_val(poutput != NULL, 0);
  fc_assert_ret_val(effect_type != EFT_COUNT, 0);
  return get_target_bonus_effects(NULL,
                                  &(const struct req_context) {
                                    .player = pplayer,
                                    .output= poutput,
                                  },
                                  NULL,
                                  effect_type);
}

/**********************************************************************//**
  Returns the player effect bonus of an output.
**************************************************************************/
int get_city_output_bonus(const struct city *pcity,
                          const struct output_type *poutput,
                          enum effect_type effect_type)
{
  if (!initialized) {
    return 0;
  }

  fc_assert_ret_val(pcity != NULL, 0);
  fc_assert_ret_val(poutput != NULL, 0);
  fc_assert_ret_val(effect_type != EFT_COUNT, 0);
  return get_target_bonus_effects(NULL,
                                  &(const struct req_context) {
                                    .player = city_owner(pcity),
                                    .city = pcity,
                                    .output = poutput,
                                  },
                                  NULL,
                                  effect_type);
}

/**********************************************************************//**
  Returns the effect bonus at a building.
**************************************************************************/
int get_building_bonus(const struct city *pcity,
                       const struct impr_type *building,
                       enum effect_type effect_type)
{
  if (!initialized) {
    return 0;
  }

  fc_assert_ret_val(NULL != pcity && NULL != building, 0);
  return get_target_bonus_effects(NULL,
                                  &(const struct req_context) {
                                    .player = city_owner(pcity),
                                    .city = pcity,
                                    .building = building,
                                  },
                                  NULL,
                                  effect_type);
}

/**********************************************************************//**
  Returns the effect bonus that applies at a tile for a given unittype.

  For instance with EFT_DEFEND_BONUS the attacker's unittype and the
  defending tile should be passed in. Slightly counter-intuitive!
  See doc/README.effects to see how the unittype applies for
  each effect here.
**************************************************************************/
int get_unittype_bonus(const struct player *pplayer,
                       const struct tile *ptile,
                       const struct unit_type *punittype,
                       const struct action *paction,
                       enum effect_type effect_type)
{
  struct city *pcity;

  if (!initialized) {
    return 0;
  }

  fc_assert_ret_val(punittype != nullptr, 0);

  if (ptile != nullptr) {
    pcity = tile_city(ptile);
  } else {
    pcity = nullptr;
  }

  return get_target_bonus_effects(nullptr,
                                  &(const struct req_context) {
                                    .player = pplayer,
                                    .city = pcity,
                                    .tile = ptile,
                                    .unittype = punittype,
                                    .action = paction,
                                  },
                                  nullptr,
                                  effect_type);
}

/**********************************************************************//**
  Returns the effect bonus at a unit
**************************************************************************/
int get_unit_bonus(const struct unit *punit, enum effect_type effect_type)
{
  if (!initialized) {
    return 0;
  }

  fc_assert_ret_val(punit != NULL, 0);
  return get_target_bonus_effects(NULL,
                                  &(const struct req_context) {
                                    .player = unit_owner(punit),
                                    .city = unit_tile(punit)
                                            ? tile_city(unit_tile(punit))
                                            : NULL,
                                    .tile = unit_tile(punit),
                                    .unit = punit,
                                    .unittype = unit_type_get(punit),
                                  },
                                  NULL,
                                  effect_type);
}

/**********************************************************************//**
  Returns the effect bonus at a tile
**************************************************************************/
int get_unit_vs_tile_bonus(const struct tile *ptile,
                           const struct unit *punit,
                           enum effect_type etype)
{
  struct player *pplayer = NULL;
  const struct unit_type *utype = NULL;

  if (!initialized) {
    return 0;
  }

  fc_assert_ret_val(ptile != NULL, 0);

  if (punit != NULL) {
    pplayer = unit_owner(punit);
    utype = unit_type_get(punit);
  }

  return get_target_bonus_effects(NULL,
                                  &(const struct req_context) {
                                    .player = pplayer,
                                    .city = tile_city(ptile),
                                    .tile = ptile,
                                    .unit = punit,
                                    .unittype = utype,
                                  },
                                  &(const struct req_context) {
                                    .player = tile_owner(ptile),
                                  },
                                  etype);
}

/**********************************************************************//**
  Returns the effect sources of this type _currently active_ at the player.

  The returned vector must be freed (building_vector_free) when the caller
  is done with it.
**************************************************************************/
int get_player_bonus_effects(struct effect_list *plist,
                             const struct player *pplayer,
                             enum effect_type effect_type)
{
  if (!initialized) {
    return 0;
  }

  fc_assert_ret_val(pplayer != NULL, 0);
  return get_target_bonus_effects(plist,
                                  &(const struct req_context) {
                                    .player = pplayer,
                                  },
                                  NULL,
                                  effect_type);
}

/**********************************************************************//**
  Returns the effect sources of this type _currently active_ at the city.

  The returned vector must be freed (building_vector_free) when the caller
  is done with it.
**************************************************************************/
int get_city_bonus_effects(struct effect_list *plist,
                           const struct city *pcity,
                           const struct output_type *poutput,
                           enum effect_type effect_type)
{
  if (!initialized) {
    return 0;
  }

  fc_assert_ret_val(pcity != NULL, 0);
  return get_target_bonus_effects(plist,
                                  &(const struct req_context) {
                                    .player = city_owner(pcity),
                                    .city = pcity,
                                    .output = poutput,
                                  },
                                  NULL,
                                  effect_type);
}

/**********************************************************************//**
  Returns the effect bonus the currently-in-construction-item will provide.

  Note this is not called get_current_production_bonus because that would
  be confused with EFT_PROD_BONUS.

  Problem type tells if we need to be CERTAIN about bonus before counting
  it or is POSSIBLE bonus enough.
**************************************************************************/
int get_current_construction_bonus(const struct city *pcity,
                                   enum effect_type effect_type,
                                   const enum req_problem_type prob_type)
{
  if (!initialized) {
    return 0;
  }

  if (VUT_IMPROVEMENT == pcity->production.kind) {
    return get_potential_improvement_bonus(pcity->production.value.building,
                                           pcity, effect_type, prob_type, TRUE);
  }

  return 0;
}

/**********************************************************************//**
  Returns the effect bonus the improvement would or does provide if present.

  Problem type tells if we need to be CERTAIN about bonus before counting
  it or is POSSIBLE bonus enough.
**************************************************************************/
int get_potential_improvement_bonus(const struct impr_type *pimprove,
                                    const struct city *pcity,
                                    enum effect_type effect_type,
                                    const enum req_problem_type prob_type,
                                    bool consider_multipliers)
{
  struct universal source = { .kind = VUT_IMPROVEMENT,
                              .value = {.building = pimprove}};
  struct effect_list *plist = get_req_source_effects(&source);
  struct player *owner = NULL;

  if (pcity != NULL) {
    owner = city_owner(pcity);
  }

  if (plist) {
    int power = 0;
    const struct req_context context = {
      .player = owner,
      .city = pcity,
      .building = pimprove,
    };

    effect_list_iterate(plist, peffect) {
      bool present = TRUE;
      bool useful = TRUE;

      if (peffect->type != effect_type) {
        continue;
      }

      requirement_vector_iterate(&peffect->reqs, preq) {
        if (VUT_IMPROVEMENT == preq->source.kind
            && preq->source.value.building == pimprove) {
          present = preq->present;
          continue;
        }

        if (!is_req_active(&context, NULL, preq, prob_type)) {
          useful = FALSE;
          break;
        }
      } requirement_vector_iterate_end;

      if (useful) {
        int val;

        if (present) {
          val = peffect->value;
        } else {
          val = -peffect->value;
        }

        if (consider_multipliers && peffect->multiplier) {
          val = val * player_multiplier_effect_value(owner, peffect->multiplier) / 100;
        }

        power += val;
      }
    } effect_list_iterate_end;

    return power;
  }

  return 0;
}

/**********************************************************************//**
  Make user-friendly text for the source.  The text is put into a user
  buffer.
**************************************************************************/
void get_effect_req_text(const struct effect *peffect,
                         char *buf, size_t buf_len)
{
  buf[0] = '\0';

  if (peffect->multiplier) {
    fc_strlcat(buf, multiplier_name_translation(peffect->multiplier), buf_len);
  }

  /* FIXME: should we do something for present FALSE reqs?
   * Currently we just ignore them. */
  requirement_vector_iterate(&peffect->reqs, preq) {
    if (!preq->present) {
      continue;
    }
    if (buf[0] != '\0') {
      fc_strlcat(buf, Q_("?req-list-separator:+"), buf_len);
    }

    universal_name_translation(&preq->source,
                               buf + strlen(buf), buf_len - strlen(buf));
  } requirement_vector_iterate_end;
}

/**********************************************************************//**
  Make user-friendly text for an effect list. The text is put into a user
  astring.
**************************************************************************/
void get_effect_list_req_text(const struct effect_list *plist,
                              struct astring *astr)
{
  struct strvec *psv = strvec_new();
  char req_text[512];

  effect_list_iterate(plist, peffect) {
    get_effect_req_text(peffect, req_text, sizeof(req_text));
    strvec_append(psv, req_text);
  } effect_list_iterate_end;

  strvec_to_and_list(psv, astr);
  strvec_destroy(psv);
}

/**********************************************************************//**
  Iterate through all the effects in cache, and call callback for each.
  If any callback returns FALSE, there is no further checking and
  this will return FALSE.
**************************************************************************/
bool iterate_effect_cache(iec_cb cb, void *data)
{
  fc_assert_ret_val(cb != NULL, FALSE);

  effect_list_iterate(ruleset_cache.tracker, peffect) {
    if (!cb(peffect, data)) {
      return FALSE;
    }
  } effect_list_iterate_end;

  return TRUE;
}

/**********************************************************************//**
  Is the effect type an user effect type?
**************************************************************************/
bool is_user_effect(enum effect_type eff)
{
  return eff >= EFT_USER_EFFECT_1 && eff <= EFT_USER_EFFECT_LAST;
}

/**********************************************************************//**
  Set the ai_valued_as effect type for the target effect.
  Target must be an user effect.
**************************************************************************/
void user_effect_ai_valued_set(enum effect_type tgt, enum effect_type valued_as)
{
  fc_assert(is_user_effect(tgt));

  ueffects[USER_EFFECT_NUMBER(tgt)].ai_value_as = valued_as;
}

/**********************************************************************//**
  Get the ai_valued_as effect type for effect. Can be used also
  for non-user effects - then it just returns the effect itself.
**************************************************************************/
enum effect_type user_effect_ai_valued_as(enum effect_type real)
{
  /* Client doesn't know these. */
  fc_assert(is_server());

  if (!is_user_effect(real)) {
    return real;
  }

  if (!is_user_effect(ueffects[USER_EFFECT_NUMBER(real)].ai_value_as)) {
    return ueffects[USER_EFFECT_NUMBER(real)].ai_value_as;
  }

  return real;
}
