/***********************************************************************
 Freeciv - Copyright (C) 1996-2004 - The Freeciv Project
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
***********************************************************************/

#ifndef FC__REQUIREMENTS_H
#define FC__REQUIREMENTS_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* common */
#include "fc_types.h"
#include "map_types.h"

struct astring;

/* Range of requirements.
 * Used in the network protocol.
 * Order is important -- wider ranges should come later -- some code
 * assumes a total order, or tests for e.g. >= REQ_RANGE_PLAYER.
 * Ranges of similar types should be supersets, for example:
 *  - the set of Adjacent tiles contains the set of CAdjacent tiles,
 *    and both contain the center Local tile (a requirement on the local
 *    tile is also within Adjacent range);
 *  - World contains Alliance contains Player (a requirement we ourselves
 *    have is also within Alliance range). */
#define SPECENUM_NAME req_range
#define SPECENUM_VALUE0 REQ_RANGE_LOCAL
#define SPECENUM_VALUE0NAME "Local"
#define SPECENUM_VALUE1 REQ_RANGE_TILE
#define SPECENUM_VALUE1NAME "Tile"
#define SPECENUM_VALUE2 REQ_RANGE_CADJACENT
#define SPECENUM_VALUE2NAME "CAdjacent"
#define SPECENUM_VALUE3 REQ_RANGE_ADJACENT
#define SPECENUM_VALUE3NAME "Adjacent"
#define SPECENUM_VALUE4 REQ_RANGE_CITY
#define SPECENUM_VALUE4NAME "City"
#define SPECENUM_VALUE5 REQ_RANGE_TRADE_ROUTE
/* FIXME: -> "Trade Route" */
#define SPECENUM_VALUE5NAME "Traderoute"
#define SPECENUM_VALUE6 REQ_RANGE_CONTINENT
#define SPECENUM_VALUE6NAME "Continent"
#define SPECENUM_VALUE7 REQ_RANGE_PLAYER
#define SPECENUM_VALUE7NAME "Player"
#define SPECENUM_VALUE8 REQ_RANGE_TEAM
#define SPECENUM_VALUE8NAME "Team"
#define SPECENUM_VALUE9 REQ_RANGE_ALLIANCE
#define SPECENUM_VALUE9NAME "Alliance"
#define SPECENUM_VALUE10 REQ_RANGE_WORLD
#define SPECENUM_VALUE10NAME "World"
#define SPECENUM_COUNT REQ_RANGE_COUNT      /* Keep this last */
#define REQ_RANGE_MAX (REQ_RANGE_COUNT - 1) /* REQ_RANGE_WORLD */
#include "specenum_gen.h"

#define req_range_iterate(_range_) \
  {                                \
    enum req_range _range_;        \
    for (_range_ = REQ_RANGE_LOCAL ; _range_ < REQ_RANGE_COUNT ; \
         _range_ = (enum req_range)(_range_ + 1)) {

#define req_range_iterate_end \
    }                         \
  }

/* A requirement. This requirement is basically a conditional; it may or
 * may not be active on a target. If it is active then something happens.
 * For instance units and buildings have requirements to be built, techs
 * have requirements to be researched, and effects have requirements to be
 * active.
 * Used in the network protocol. */
struct requirement {
  struct universal source;  /* Requirement source */
  enum req_range range;     /* Requirement range */
  bool survives;            /* Set if destroyed sources satisfy the req*/
  bool present;             /* Set if the requirement is to be present */
  bool quiet;               /* Do not list this in helptext */
};

#define SPECVEC_TAG requirement
#define SPECVEC_TYPE struct requirement
#include "specvec.h"
#define requirement_vector_iterate(req_vec, preq) \
  TYPED_VECTOR_ITERATE(struct requirement, req_vec, preq)
#define requirement_vector_iterate_end VECTOR_ITERATE_END

/* A set of targets to evaluate requirements against. Depending on what the
 * requirements in question are for, most of these entries will usually be
 * nullptr. For instance, when evaluating the construction requirements for a
 * building, there is no target unit, specialist etc. */
struct req_context {
  const struct player *player;
  const struct city *city;
  const struct tile *tile;

  /* For local-ranged requirements only */
  const struct unit *unit;
  const struct unit_type *unittype;
  const struct impr_type *building;
  const struct extra_type *extra;
  const struct output_type *output;
  const struct specialist *specialist;
  const struct action *action;
  const enum unit_activity activity;
};

enum req_unchanging_status {
  REQUCH_NO = 0, /* Changes regularly */
  REQUCH_CTRL, /* Can't be changed by game means as long as target player
                * is in control of target city or unit */
  REQUCH_ACT, /* Can't be easily changed by expected player's activity
               * (without destroying teammates, inducing global warming...) */
  REQUCH_SCRIPTS, /* May be changed by script callbacks */
  REQUCH_HACK, /* May be changed by server commands/editor */
  REQUCH_YES /* Really never changes unless savegame is edited */
};

/* A callback that may transform kind-specific default unchanging status
 * to another one (usually higher but not always)
 * Passing other_context is just not needed for it in any known cases */
typedef enum req_unchanging_status
  (*req_unchanging_cond_cb)(const struct civ_map *nmap,
                            enum req_unchanging_status def,
                            const struct req_context *context,
                            const struct requirement *req);

/* req_context-related functions */
const struct req_context *req_context_empty(void);

/* General requirement functions. */
struct requirement req_from_str(const char *type, const char *range,
                                bool survives, bool present, bool quiet,
                                const char *value);
const char *req_to_fstring(const struct requirement *req,
                           struct astring *astr);

void req_get_values(const struct requirement *req, int *type,
                    int *range, bool *survives, bool *present, bool *quiet,
                    int *value);
struct requirement req_from_values(int type, int range,
                                   bool survives, bool present, bool quiet,
                                   int value);

void req_copy(struct requirement *dst, const struct requirement *src);

bool are_requirements_equal(const struct requirement *req1,
                            const struct requirement *req2);

bool are_requirements_contradictions(const struct requirement *req1,
                                     const struct requirement *req2);

bool req_implies_req(const struct requirement *req1,
                     const struct requirement *req2);

struct requirement *
req_vec_first_contradiction_in_vec(const struct requirement *req,
                                   const struct requirement_vector *vec);
bool does_req_contradicts_reqs(const struct requirement *req,
                               const struct requirement_vector *vec);

enum fc_tristate tri_req_active(const struct req_context *context,
                                const struct req_context *other_context,
                                const struct requirement *req);
bool is_req_active(const struct req_context *context,
                   const struct req_context *other_context,
                   const struct requirement *req,
                   const enum   req_problem_type prob_type);
bool are_reqs_active(const struct req_context *context,
                     const struct req_context *other_context,
                     const struct requirement_vector *reqs,
                     const enum   req_problem_type prob_type);
bool are_reqs_active_ranges(const enum req_range min_range,
                            const enum req_range max_range,
                            const struct req_context *context,
                            const struct req_context *other_context,
                            const struct requirement_vector *reqs,
                            const enum   req_problem_type prob_type);
enum fc_tristate
tri_req_active_turns(int pass, int period,
                     const struct req_context *context,
                     const struct req_context *other_context,
                     const struct requirement *req);

/* Type of a callback that tests requirements due to a context
 * and something else in some manner different from tri_req_active() */
typedef enum fc_tristate
   (*req_tester_cb)(const struct req_context *context,
                    const struct req_context *other_context,
                    const struct requirement *req,
                    void *data, int n_data);

enum fc_tristate
default_tester_cb(const struct req_context *context,
                  const struct req_context *other_context,
                  const struct requirement *req,
                  void *data, int n_data);
enum fc_tristate
  tri_reqs_cb_active(const struct req_context *context,
                     const struct req_context *other_context,
                     const struct requirement_vector *reqs,
                     struct requirement_vector *maybe_reqs,
                     req_tester_cb tester,
                     void *data, int n_data);

enum req_unchanging_status
  is_req_unchanging(const struct req_context *context,
                    const struct requirement *req);
enum req_unchanging_status
  is_req_preventing(const struct req_context *context,
                    const struct req_context *other_context,
                    const struct requirement *req,
                    enum req_problem_type prob_type);

bool is_req_in_vec(const struct requirement *req,
                   const struct requirement_vector *vec);

bool req_vec_wants_type(const struct requirement_vector *reqs,
                        enum universals_n kind);

bool universal_never_there(const struct universal *source);
bool req_is_impossible_to_fulfill(const struct requirement *req);
bool req_vec_is_impossible_to_fulfill(const struct requirement_vector *reqs);

/**
 * @brief req_vec_num_in_item a requirement vectors number in an item.
 *
 * A ruleset item can have more than one requirement vector. This numbers
 * them. Allows applying a change suggested based on one ruleset item to
 * another ruleset item of the same kind - say to a copy.
 */
typedef signed char req_vec_num_in_item;

/**********************************************************************//**
  Returns the requirement vector number of the specified requirement
  vector in the specified parent item or -1 if the vector doesn't belong to
  the parent item.

  @param parent_item the item that may own the vector.
  @param vec the requirement vector to number.
  @return the requirement vector number the vector has in the parent item.
**************************************************************************/
typedef req_vec_num_in_item
(*requirement_vector_number)(const void *parent_item,
                             const struct requirement_vector *vec);

/********************************************************************//**
  Returns a writable pointer to the specified requirement vector in the
  specified parent item or nullptr if the parent item doesn't have a
  requirement vector with that requirement vector number.

  @param parent_item the item that should have the requirement vector.
  @param number the item's requirement vector number.
  @return a pointer to the specified requirement vector.
************************************************************************/
typedef struct requirement_vector *
(*requirement_vector_by_number)(const void *parent_item,
                                req_vec_num_in_item number);

/*********************************************************************//**
  Returns the name of the specified requirement vector number in the
  parent item or nullptr if parent items of the kind this function is for
  don't have a requirement vector with that number.

  @param number the requirement vector to name
  @return the requirement vector name or nullptr.
**************************************************************************/
typedef const char *(*requirement_vector_namer)(req_vec_num_in_item number);

req_vec_num_in_item
req_vec_vector_number(const void *parent_item,
                      const struct requirement_vector *vec);
struct requirement_vector *
req_vec_by_number(const void *parent_item, req_vec_num_in_item number);

/* Interactive friendly requirement vector change suggestions and
 * reasoning. */
#define SPECENUM_NAME req_vec_change_operation
#define SPECENUM_VALUE0 RVCO_REMOVE
#define SPECENUM_VALUE0NAME N_("Remove")
#define SPECENUM_VALUE1 RVCO_APPEND
#define SPECENUM_VALUE1NAME N_("Append")
#define SPECENUM_COUNT RVCO_NOOP
#include "specenum_gen.h"

struct req_vec_change {
  enum req_vec_change_operation operation;
  struct requirement req;

  req_vec_num_in_item vector_number;
};

struct req_vec_problem {
  /* Can't use name_translation because it is MAX_LEN_NAME long and a
   * description may contain more than one name. */
  char description[500];
  char description_translated[500];

  int num_suggested_solutions;
  struct req_vec_change *suggested_solutions;
};

const char *req_vec_change_translation(const struct req_vec_change *change,
                                       const requirement_vector_namer namer);

bool req_vec_change_apply(const struct req_vec_change *modification,
                          requirement_vector_by_number getter,
                          const void *parent_item);

struct req_vec_problem *req_vec_problem_new(int num_suggested_solutions,
                                            const char *description, ...);
struct req_vec_problem *
req_vec_problem_new_transl(int num_suggested_solutions,
                           const char *description,
                           const char *description_translated);
void req_vec_problem_free(struct req_vec_problem *issue);

struct req_vec_problem *
req_vec_get_first_contradiction(const struct requirement_vector *vec,
                                requirement_vector_number get_num,
                                const void *parent_item);
struct req_vec_problem *
req_vec_suggest_repair(const struct requirement_vector *vec,
                       requirement_vector_number get_num,
                       const void *parent_item);
struct req_vec_problem *
req_vec_get_first_missing_univ(const struct requirement_vector *vec,
                               requirement_vector_number get_num,
                               const void *parent_item);
struct req_vec_problem *
req_vec_get_first_redundant_req(const struct requirement_vector *vec,
                                requirement_vector_number get_num,
                                const void *parent_item);
struct req_vec_problem *
req_vec_suggest_improvement(const struct requirement_vector *vec,
                            requirement_vector_number get_num,
                            const void *parent_item);


/* General universal functions. */
int universal_number(const struct universal *source);

struct universal universal_by_number(const enum universals_n kind,
                                     const int value);
struct universal universal_by_rule_name(const char *kind,
                                        const char *value);
void universal_value_from_str(struct universal *source, const char *value);
void universal_copy(struct universal *dst, const struct universal *src);
void universal_extraction(const struct universal *source,
                          int *kind, int *value);

bool are_universals_equal(const struct universal *psource1,
                          const struct universal *psource2);

const char *universal_rule_name(const struct universal *psource);
const char *universal_name_translation(const struct universal *psource,
                                       char *buf, size_t bufsz);
const char *universal_type_rule_name(const struct universal *psource);

int universal_build_shield_cost(const struct city *pcity,
                                const struct universal *target);

bool universal_replace_in_req_vec(struct requirement_vector *reqs,
                                  const struct universal *to_replace,
                                  const struct universal *replacement);

bool universal_is_legal_in_requirement(const struct universal *univ);

#define universal_is_mentioned_by_requirement(preq, psource)               \
  are_universals_equal(&preq->source, psource)
bool universal_is_mentioned_by_requirements(
    const struct requirement_vector *reqs,
    const struct universal *psource);

/* An item contradicts, fulfills or is irrelevant to the requirement */
enum req_item_found {ITF_NO, ITF_YES, ITF_NOT_APPLICABLE};

void universal_found_functions_init(void);
enum req_item_found
universal_fulfills_requirement(const struct requirement *preq,
                               const struct universal *source);
bool universal_fulfills_requirements(bool check_necessary,
                                     const struct requirement_vector *reqs,
                                     const struct universal *source);
bool universal_is_relevant_to_requirement(const struct requirement *req,
                                          const struct universal *source);
bool universals_mean_unfulfilled(struct requirement_vector *reqs,
                                 struct universal *unis,
                                 size_t n_unis);
bool universals_say_everything(struct requirement_vector *reqs,
                               struct universal *unis,
                               size_t n_unis);

#define universals_iterate(_univ_) \
  {                                \
    enum universals_n _univ_;      \
    for (_univ_ = VUT_NONE; _univ_ < VUT_COUNT; _univ_ = (enum universals_n)(_univ_ + 1)) {

#define universals_iterate_end \
    }                          \
  }

/* Accessors to determine if a universal fulfills a requirement vector.
 * When adding an additional accessor, be sure to add the appropriate
 * item_found function in universal_found_functions_init(). */
/* XXX Some versions of g++ can't cope with the struct literals */
#define requirement_fulfilled_by_government(_gov_, _rqs_)                  \
  universal_fulfills_requirements(FALSE, (_rqs_),                          \
      &(struct universal){.kind = VUT_GOVERNMENT, .value = {.govern = (_gov_)}})
#define requirement_fulfilled_by_nation(_nat_, _rqs_)                      \
  universal_fulfills_requirements(FALSE, (_rqs_),                          \
      &(struct universal){.kind = VUT_NATION, .value = {.nation = (_nat_)}})
#define requirement_fulfilled_by_improvement(_imp_, _rqs_)                 \
  universal_fulfills_requirements(FALSE, (_rqs_),                          \
    &(struct universal){.kind = VUT_IMPROVEMENT,                           \
                        .value = {.building = (_imp_)}})
#define requirement_fulfilled_by_terrain(_ter_, _rqs_)                 \
  universal_fulfills_requirements(FALSE, (_rqs_),                      \
    &(struct universal){.kind = VUT_TERRAIN,                           \
                        .value = {.terrain = (_ter_)}})
#define requirement_fulfilled_by_unit_class(_uc_, _rqs_)                   \
  universal_fulfills_requirements(FALSE, (_rqs_),                          \
      &(struct universal){.kind = VUT_UCLASS, .value = {.uclass = (_uc_)}})
#define requirement_fulfilled_by_unit_type(_ut_, _rqs_)                    \
  universal_fulfills_requirements(FALSE, (_rqs_),                          \
      &(struct universal){.kind = VUT_UTYPE, .value = {.utype = (_ut_)}})
#define requirement_fulfilled_by_extra(_ex_, _rqs_)                        \
  universal_fulfills_requirements(FALSE, (_rqs_),                          \
      &(struct universal){.kind = VUT_EXTRA, .value = {.extra = (_ex_)}})
#define requirement_fulfilled_by_output_type(_o_, _rqs_)                   \
  universal_fulfills_requirements(FALSE, (_rqs_),                          \
      &(struct universal){.kind = VUT_OTYPE, .value = {.outputtype = (_o_)}})
#define requirement_fulfilled_by_action(_act_, _rqs_)                      \
  universal_fulfills_requirements(FALSE, (_rqs_),                          \
      &(struct universal){.kind = VUT_ACTION, .value = {.action = (_act_)}})

#define requirement_needs_improvement(_imp_, _rqs_)                        \
  universal_fulfills_requirements(TRUE, (_rqs_),                           \
    &(struct universal){.kind = VUT_IMPROVEMENT,                           \
                        .value = {.building = (_imp_)}})

int requirement_kind_ereq(const int value,
                          const enum req_range range,
                          const bool present,
                          const int max_value);

#define requirement_diplrel_ereq(_id_, _range_, _present_)                \
  requirement_kind_ereq(_id_, _range_, _present_, DRO_LAST)

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* FC__REQUIREMENTS_H */
