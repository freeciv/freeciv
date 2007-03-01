/********************************************************************** 
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

#include "fc_types.h"
#include "tech.h"
#include "terrain.h"
#include "unittype.h"

/* The type of a requirement source.  This must correspond to req_type_names[]
 * in requirements.c. */
enum req_source_type {
  REQ_NONE,
  REQ_TECH,
  REQ_GOV,
  REQ_BUILDING,
  REQ_SPECIAL,
  REQ_TERRAIN,
  REQ_NATION,
  REQ_UNITTYPE,
  REQ_UNITFLAG,
  REQ_UNITCLASS,
  REQ_OUTPUTTYPE,
  REQ_SPECIALIST,
  REQ_MINSIZE, /* Minimum size: at city range means city size */
  REQ_AI,      /* AI level of the player */
  REQ_LAST
};

/* Range of requirements.  This must correspond to req_range_names[]
 * in requirements.c. */
enum req_range {
  REQ_RANGE_LOCAL,
  REQ_RANGE_ADJACENT,
  REQ_RANGE_CITY,
  REQ_RANGE_CONTINENT,
  REQ_RANGE_PLAYER,
  REQ_RANGE_WORLD,
  REQ_RANGE_LAST   /* keep this last */
};

/* A requirement source. */
struct req_source {
  enum req_source_type type;            /* source type */

  union {
    Tech_type_id tech;                  /* source tech */
    struct government *gov;             /* source government */
    Impr_type_id building;              /* source building */
    enum tile_special_type special;     /* source special */
    struct terrain *terrain;            /* source terrain type */
    struct nation_type *nation;         /* source nation type */
    struct unit_type *unittype;         /* source unittype */
    enum unit_flag_id unitflag;         /* source unit flag */
    struct unit_class *unitclass;       /* source unit class */
    Output_type_id outputtype;          /* source output type */
    Specialist_type_id specialist;      /* specialist type */
    int minsize;                        /* source minsize type */
    enum ai_level level;                /* source AI level */
  } value;                              /* source value */
};

/* A requirement. This requirement is basically a conditional; it may or
 * may not be active on a target.  If it is active then something happens.
 * For instance units and buildings have requirements to be built, techs
 * have requirements to be researched, and effects have requirements to be
 * active. */
struct requirement {
  struct req_source source;		/* requirement source */
  enum req_range range;			/* requirement range */
  bool survives; /* set if destroyed sources satisfy the req*/
  bool negated;	 /* set if the requirement is to be negated */
};

#define SPECLIST_TAG requirement
#define SPECLIST_TYPE struct requirement
#include "speclist.h"
#define requirement_list_iterate(req_list, preq) \
  TYPED_LIST_ITERATE(struct requirement, req_list, preq)
#define requirement_list_iterate_end LIST_ITERATE_END

#define SPECVEC_TAG requirement
#define SPECVEC_TYPE struct requirement
#include "specvec.h"
#define requirement_vector_iterate(req_vec, preq) \
  TYPED_VECTOR_ITERATE(struct requirement, req_vec, preq)
#define requirement_vector_iterate_end VECTOR_ITERATE_END

struct req_source req_source_from_str(const char *type, const char *value);
struct req_source req_source_from_values(int type, int value);
void req_source_get_values(const struct req_source *source,
			   int *type, int *value);

enum req_range req_range_from_str(const char *str);
struct requirement req_from_str(const char *type, const char *range,
				bool survives, bool negated,
				const char *value);

void req_get_values(const struct requirement *req, int *type,
		    int *range, bool *survives, bool *negated,
		    int *value);
struct requirement req_from_values(int type, int range,
				   bool survives, bool negated, int value);

bool are_requirements_equal(const struct requirement *req1,
			    const struct requirement *req2);

bool is_req_active(const struct player *target_player,
		   const struct city *target_city,
		   const struct impr_type *target_building,
		   const struct tile *target_tile,
		   const struct unit_type *target_unittype,
		   const struct output_type *target_output,
		   const struct specialist *target_specialist,
		   const struct requirement *req);
bool are_reqs_active(const struct player *target_player,
		     const struct city *target_city,
		     const struct impr_type *target_building,
		     const struct tile *target_tile,
		     const struct unit_type *target_unittype,
		     const struct output_type *target_output,
		     const struct specialist *target_specialist,
		     const struct requirement_vector *reqs);

bool is_req_unchanging(const struct requirement *req);

/* Req-source helper functions. */
bool are_req_sources_equal(const struct req_source *psource1,
			   const struct req_source *psource2);
char *get_req_source_text(const struct req_source *psource,
			  char *buf, size_t bufsz);

#endif  /* FC__REQUIREMENTS_H */
