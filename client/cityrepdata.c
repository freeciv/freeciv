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

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "fcintl.h"
#include "support.h"

#include "city.h"
#include "game.h"
#include "map.h"
#include "unitlist.h"

#include "cma_fec.h"
#include "options.h"

#include "cityrepdata.h"

/************************************************************************
 cr_entry = return an entry (one column for one city) for the city report
 These return ptrs to filled in static strings.
 Note the returned string may not be exactly the right length; that
 is handled later.
*************************************************************************/

static const char *cr_entry_cityname(const struct city *pcity,
				     const void *data)
{
  /* We used to truncate the name to 14 bytes.  This should not be needed
   * in any modern GUI library and may give an invalid string if a
   * multibyte character is clipped. */
  return pcity->name;
}

static const char *cr_entry_size(const struct city *pcity,
				 const void *data)
{
  static char buf[8];
  my_snprintf(buf, sizeof(buf), "%2d", pcity->size);
  return buf;
}

static const char *cr_entry_hstate_concise(const struct city *pcity,
					   const void *data)
{
  static char buf[4];
  my_snprintf(buf, sizeof(buf), "%s", (city_celebrating(pcity) ? "*" :
				       (city_unhappy(pcity) ? "X" : " ")));
  return buf;
}

static const char *cr_entry_hstate_verbose(const struct city *pcity,
					   const void *data)
{
  static char buf[16];
  my_snprintf(buf, sizeof(buf), "%s",
	      (city_celebrating(pcity) ? Q_("?city_state:Rapture") :
	       (city_unhappy(pcity) ? Q_("?city_state:Disorder") :
		Q_("?city_state:Peace"))));
  return buf;
}

static const char *cr_entry_workers(const struct city *pcity,
				    const void *data)
{
  static char buf[32];
  my_snprintf(buf, sizeof(buf), "%d/%d/%d/%d", pcity->ppl_happy[4],
	      pcity->ppl_content[4], pcity->ppl_unhappy[4],
	      pcity->ppl_angry[4]);
  return buf;
}

static const char *cr_entry_happy(const struct city *pcity,
				  const void *data)
{
  static char buf[8];
  my_snprintf(buf, sizeof(buf), "%2d", pcity->ppl_happy[4]);
  return buf;
}

static const char *cr_entry_content(const struct city *pcity,
				    const void *data)
{
  static char buf[8];
  my_snprintf(buf, sizeof(buf), "%2d", pcity->ppl_content[4]);
  return buf;
}

static const char *cr_entry_unhappy(const struct city *pcity,
				    const void *data)
{
  static char buf[8];
  my_snprintf(buf, sizeof(buf), "%2d", pcity->ppl_unhappy[4]);
  return buf;
}

static const char *cr_entry_angry(const struct city *pcity,
				  const void *data)
{
  static char buf[8];
  my_snprintf(buf, sizeof(buf), "%2d", pcity->ppl_angry[4]);
  return buf;
}

static const char *cr_entry_specialists(const struct city *pcity,
					const void *data)
{
  return specialists_string(pcity->specialists);
}

static const char *cr_entry_specialist(const struct city *pcity,
				       const void *data)
{
  static char buf[8];
  const struct specialist *sp = data;

  my_snprintf(buf, sizeof(buf), "%2d", pcity->specialists[sp->index]);
  return buf;
}

static const char *cr_entry_attack(const struct city *pcity,
				   const void *data)
{
  static char buf[32];
  int attack_best[4] = {-1, -1, -1, -1}, i;

  unit_list_iterate(pcity->tile->units, punit) {
    /* What about allied units?  Should we just count them? */
    attack_best[3] = punit->type->attack_strength;

    /* Now that the element is appended to the end of the list, we simply
       do an insertion sort. */
    for (i = 2; i >= 0 && attack_best[i] < attack_best[i + 1]; i--) {
      int tmp = attack_best[i];
      attack_best[i] = attack_best[i + 1];
      attack_best[i + 1] = tmp;
    }
  } unit_list_iterate_end;

  buf[0] = '\0';
  for (i = 0; i < 3; i++) {
    if (attack_best[i] >= 0) {
      cat_snprintf(buf, sizeof(buf), "%s%d", (i > 0) ? "/" : "",
		   attack_best[i]);
    } else {
      cat_snprintf(buf, sizeof(buf), "%s-", (i > 0) ? "/" : "");
    }
  }

  return buf;
}

static const char *cr_entry_defense(const struct city *pcity,
				    const void *data)
{
  static char buf[32];
  int defense_best[4] = {-1, -1, -1, -1}, i;

  unit_list_iterate(pcity->tile->units, punit) {
    /* What about allied units?  Should we just count them? */
    defense_best[3] = punit->type->defense_strength;

    /* Now that the element is appended to the end of the list, we simply
       do an insertion sort. */
    for (i = 2; i >= 0 && defense_best[i] < defense_best[i + 1]; i--) {
      int tmp = defense_best[i];
      defense_best[i] = defense_best[i + 1];
      defense_best[i + 1] = tmp;
    }
  } unit_list_iterate_end;

  buf[0] = '\0';
  for (i = 0; i < 3; i++) {
    if (defense_best[i] >= 0) {
      cat_snprintf(buf, sizeof(buf), "%s%d", (i > 0) ? "/" : "",
		   defense_best[i]);
    } else {
      cat_snprintf(buf, sizeof(buf), "%s-", (i > 0) ? "/" : "");
    }
  }

  return buf;
}

static const char *cr_entry_supported(const struct city *pcity,
				      const void *data)
{
  static char buf[8];
  int num_supported = 0;

  unit_list_iterate(pcity->units_supported, punit) {
    num_supported++;
  } unit_list_iterate_end;

  my_snprintf(buf, sizeof(buf), "%2d", num_supported);

  return buf;
}

static const char *cr_entry_present(const struct city *pcity,
				    const void *data)
{
  static char buf[8];
  int num_present = 0;

  unit_list_iterate(pcity->tile->units, punit) {
    num_present++;
  } unit_list_iterate_end;

  my_snprintf(buf, sizeof(buf), "%2d", num_present);

  return buf;
}

static const char *cr_entry_resources(const struct city *pcity,
				      const void *data)
{
  static char buf[32];
  my_snprintf(buf, sizeof(buf), "%d/%d/%d",
	      pcity->surplus[O_FOOD], 
	      pcity->surplus[O_SHIELD], 
	      pcity->surplus[O_TRADE]);
  return buf;
}

static const char *cr_entry_foodplus(const struct city *pcity,
				     const void *data)
{
  static char buf[8];
  my_snprintf(buf, sizeof(buf), "%3d",
	      pcity->surplus[O_FOOD]);
  return buf;
}

static const char *cr_entry_prodplus(const struct city *pcity,
				     const void *data)
{
  static char buf[8];
  my_snprintf(buf, sizeof(buf), "%3d",
	      pcity->surplus[O_SHIELD]);
  return buf;
}

static const char *cr_entry_tradeplus(const struct city *pcity,
				      const void *data)
{
  static char buf[8];
  my_snprintf(buf, sizeof(buf), "%3d",
	      pcity->surplus[O_TRADE]);
  return buf;
}

static const char *cr_entry_output(const struct city *pcity,
				   const void *data)
{
  static char buf[32];
  int goldie = pcity->surplus[O_GOLD];

  my_snprintf(buf, sizeof(buf), "%s%d/%d/%d",
	      (goldie < 0) ? "-" : (goldie > 0) ? "+" : "",
	      (goldie < 0) ? (-goldie) : goldie,
	      pcity->prod[O_LUXURY],
	      pcity->prod[O_SCIENCE]);
  return buf;
}

static const char *cr_entry_gold(const struct city *pcity,
				 const void *data)
{
  static char buf[8];

  if (pcity->surplus[O_GOLD] > 0) {
    my_snprintf(buf, sizeof(buf), "+%d", pcity->surplus[O_GOLD]);
  } else {
    my_snprintf(buf, sizeof(buf), "%3d", pcity->surplus[O_GOLD]);
  }
  return buf;
}

static const char *cr_entry_luxury(const struct city *pcity,
				   const void *data)
{
  static char buf[8];
  my_snprintf(buf, sizeof(buf), "%3d",
	      pcity->prod[O_LUXURY]);
  return buf;
}

static const char *cr_entry_science(const struct city *pcity,
				    const void *data)
{
  static char buf[8];
  my_snprintf(buf, sizeof(buf), "%3d",
	      pcity->prod[O_SCIENCE]);
  return buf;
}

static const char *cr_entry_food(const struct city *pcity,
				 const void *data)
{
  static char buf[32];
  my_snprintf(buf, sizeof(buf), "%d/%d",
	      pcity->food_stock,
	      city_granary_size(pcity->size) );
  return buf;
}

static const char *cr_entry_growturns(const struct city *pcity,
				      const void *data)
{
  static char buf[8];
  int turns = city_turns_to_grow(pcity);
  if (turns == FC_INFINITY) {
    /* 'never' wouldn't be easily translatable here. */
    my_snprintf(buf, sizeof(buf), "-");
  } else {
    /* Shrinking cities get a negative value. */
    my_snprintf(buf, sizeof(buf), "%4d", turns);
  }
  return buf;
}

static const char *cr_entry_pollution(const struct city *pcity,
				      const void *data)
{
  static char buf[8];
  my_snprintf(buf, sizeof(buf), "%3d", pcity->pollution);
  return buf;
}

static const char *cr_entry_num_trade(const struct city *pcity,
				      const void *data)
{
  static char buf[8];
  my_snprintf(buf, sizeof(buf), "%d", city_num_trade_routes(pcity));
  return buf;
}

static const char *cr_entry_building(const struct city *pcity,
				     const void *data)
{
  static char buf[128];
  const char *from_worklist =
    worklist_is_empty(&pcity->worklist) ? "" :
    concise_city_production ? "+" : _("(worklist)");
	
  if (!pcity->production.is_unit
      && impr_flag(pcity->production.value, IF_GOLD)) {
    my_snprintf(buf, sizeof(buf), "%s (%d/X/X/X)%s",
		get_impr_name_ex(pcity, pcity->production.value),
		MAX(0, pcity->surplus[O_SHIELD]), from_worklist);
  } else {
    int turns = city_turns_to_build(pcity, pcity->production, TRUE);
    char time[32];
    const char *name;
    int cost;

    if (turns < 999) {
      my_snprintf(time, sizeof(time), "%d", turns);
    } else {
      my_snprintf(time, sizeof(time), "-");
    }

    if(pcity->production.is_unit) {
      name = get_unit_type(pcity->production.value)->name;
      cost = unit_build_shield_cost(get_unit_type(pcity->production.value));
    } else {
      name = get_impr_name_ex(pcity, pcity->production.value);
      cost = impr_build_shield_cost(pcity->production.value);
    }

    my_snprintf(buf, sizeof(buf), "%s (%d/%d/%s/%d)%s", name,
		pcity->shield_stock, cost, time, city_buy_cost(pcity),
		from_worklist);
  }

  return buf;
}

static const char *cr_entry_corruption(const struct city *pcity,
				       const void *data)
{
  static char buf[8];
  my_snprintf(buf, sizeof(buf), "%3d", pcity->waste[O_TRADE]);
  return buf;
}

static const char *cr_entry_waste(const struct city *pcity,
				  const void *data)
{
  static char buf[8];
  my_snprintf(buf, sizeof(buf), "%3d", pcity->waste[O_SHIELD]);
  return buf;
}

static const char *cr_entry_cma(const struct city *pcity,
				const void *data)
{
  return cmafec_get_short_descr_of_city(pcity);
}

/* City report options (which columns get shown)
 * To add a new entry, you should just have to:
 * - increment NUM_CREPORT_COLS in cityrepdata.h
 * - add a function like those above
 * - add an entry in the city_report_specs[] table
 */

/* This generates the function name and the tagname: */
#define FUNC_TAG(var)  cr_entry_##var, #var 

static const struct city_report_spec base_city_report_specs[] = {
  { TRUE, -15, 0, NULL,  N_("?city:Name"),      N_("City Name"),
    NULL, FUNC_TAG(cityname) },
  { TRUE, 2, 1, NULL,  N_("?size:Sz"),        N_("Size"),
    NULL, FUNC_TAG(size) },
  { TRUE,  -8, 1, NULL,  N_("State"),     N_("Rapture/Peace/Disorder"),
    NULL, FUNC_TAG(hstate_verbose) },
  { FALSE,  1, 1, NULL,  NULL,            N_("Concise *=Rapture, X=Disorder"),
    NULL, FUNC_TAG(hstate_concise) },
  { TRUE, 10, 1, N_("Workers"),
    N_("?happy/content/unhappy/angry:H/C/U/A"),
    N_("Workers: Happy, Content, Unhappy, Angry"),
    NULL, FUNC_TAG(workers) },
  { FALSE, 2, 1, NULL, N_("?Happy workers:H"), N_("Workers: Happy"),
    NULL, FUNC_TAG(happy) },
  { FALSE, 2, 1, NULL, N_("?Content workers:C"), N_("Workers: Content"),
    NULL, FUNC_TAG(content) },
  { FALSE, 2, 1, NULL, N_("?Unhappy workers:U"), N_("Workers: Unhappy"),
    NULL, FUNC_TAG(unhappy) },
  { FALSE, 2, 1, NULL, N_("?Angry workers:A"), N_("Workers: Angry"),
    NULL, FUNC_TAG(angry) },
  { FALSE, 7, 1, N_("Special"),
    N_("?entertainers/scientists/taxmen:E/S/T"),
    N_("Entertainers, Scientists, Taxmen"),
    NULL, FUNC_TAG(specialists) },
  { FALSE, 8, 1, N_("Best"), N_("attack"),
    N_("Best attacking units"), NULL, FUNC_TAG(attack)},
  { FALSE, 8, 1, N_("Best"), N_("defense"),
    N_("Best defending units"), NULL, FUNC_TAG(defense)},
  { FALSE, 2, 1, N_("Units"), N_("?Supported (units):Sup"),
    N_("Number of units supported"), NULL, FUNC_TAG(supported) },
  { FALSE, 2, 1, N_("Units"), N_("?Present (units):Prs"),
    N_("Number of units present"), NULL, FUNC_TAG(present) },

  { TRUE,  10, 1, N_("Surplus"), N_("?food/prod/trade:F/P/T"),
                                 N_("Surplus: Food, Production, Trade"),
    NULL, FUNC_TAG(resources) },
  { FALSE, 3, 1, NULL, N_("?Food surplus:F+"), N_("Surplus: Food"),
    NULL, FUNC_TAG(foodplus) },
  { FALSE, 3, 1, NULL, N_("?Production surplus:P+"),
    N_("Surplus: Production"), NULL, FUNC_TAG(prodplus) },
  { FALSE, 3, 1, NULL, N_("?Trade surplus:T+"), N_("Surplus: Trade"),
    NULL, FUNC_TAG(tradeplus) },
  { TRUE,  10, 1, N_("Economy"), N_("?gold/lux/sci:G/L/S"),
                                 N_("Economy: Gold, Luxuries, Science"),
    NULL, FUNC_TAG(output) },
  { FALSE, 3, 1, NULL, N_("?Gold:G"), N_("Economy: Gold"),
    NULL, FUNC_TAG(gold) },
  { FALSE, 3, 1, NULL, N_("?Luxury:L"), N_("Economy: Luxury"),
    NULL, FUNC_TAG(luxury) },
  { FALSE, 3, 1, NULL, N_("?Science:S"), N_("Economy: Science"),
    NULL, FUNC_TAG(science) },
  { FALSE,  1, 1, N_("?trade_routes:n"), N_("?trade_routes:T"),
                                         N_("Number of Trade Routes"),
    NULL, FUNC_TAG(num_trade) },
  { TRUE,   7, 1, N_("Food"), N_("Stock"), N_("Food Stock"),
    NULL, FUNC_TAG(food) },
  { FALSE,  3, 1, NULL, N_("?pollution:Pol"),        N_("Pollution"),
    NULL, FUNC_TAG(pollution) },
  { FALSE,  4, 1, N_("Grow"), N_("Turns"), N_("Turns until growth/famine"),
    NULL, FUNC_TAG(growturns) },
  { FALSE,  3, 1, NULL, N_("?corruption:Cor"),        N_("Corruption"),
    NULL, FUNC_TAG(corruption) },
  { FALSE,  3, 1, NULL, N_("?waste:Was"), N_("Waste"),
    NULL, FUNC_TAG(waste) },
  { TRUE,  15, 1, NULL, N_("?cma:Governor"),	      N_("Citizen Governor"),
    NULL, FUNC_TAG(cma) },
  { TRUE,   0, 1, N_("Currently Building"), N_("(Stock,Target,Turns,Buy)"),
                                            N_("Currently Building"),
    NULL, FUNC_TAG(building) }
};

struct city_report_spec *city_report_specs;
static int num_creport_cols;

/******************************************************************
Some simple wrappers:
******************************************************************/
int num_city_report_spec(void)
{
  return num_creport_cols;
}
bool *city_report_spec_show_ptr(int i)
{
  return &(city_report_specs[i].show);
}
const char *city_report_spec_tagname(int i)
{
  return city_report_specs[i].tagname;
}

/******************************************************************
  Initialize city report data.  Currently all this does is
  pre-translate the fields (to make things easier on the GUI
  writers).  Should be called before the GUI starts up.
******************************************************************/
void init_city_report_game_data(void)
{
  int i;

  num_creport_cols = ARRAY_SIZE(base_city_report_specs) + SP_COUNT;
  city_report_specs
    = fc_realloc(city_report_specs,
		 num_creport_cols * sizeof(*city_report_specs));
  memcpy(city_report_specs, base_city_report_specs,
	 sizeof(base_city_report_specs));

  for (i = 0; i < ARRAY_SIZE(base_city_report_specs); i++) {
    struct city_report_spec* p = &city_report_specs[i];

    if (p->title1) {
      p->title1 = Q_(p->title1);
    }
    if (p->title2) {
      p->title2 = Q_(p->title2);
    }
    p->explanation = _(p->explanation);
  }

  specialist_type_iterate(sp) {
    struct city_report_spec *p = &city_report_specs[i];
    static char explanation[SP_MAX][128];

    p->show = FALSE;
    p->width = 2;
    p->space = 1;
    p->title1 = Q_("?specialist:S");
    p->title2 = Q_(get_specialist(sp)->short_name);
    my_snprintf(explanation[sp], sizeof(explanation[sp]),
		_("Specialists: %s"), _(get_specialist(sp)->name));
    p->explanation = explanation[sp];
    p->data = get_specialist(sp);
    p->func = cr_entry_specialist;
    p->tagname = get_specialist(sp)->name;

    i++;
  } specialist_type_iterate_end;

  assert(NUM_CREPORT_COLS == ARRAY_SIZE(base_city_report_specs) + SP_COUNT);
}

/**********************************************************************
  The following several functions allow intelligent sorting city report 
  fields by column.  This doesn't necessarily do the right thing, but
  it's better than sorting alphabetically.

  The GUI gives us two values to compare (as strings).  We try to split
  them into an array of integer and string fields, then we compare
  lexicographically.  Two integer fields are compared in the obvious
  way, two character fields are compared alphabetically.  Arbitrarily, a
  numeric field is sorted before a character field (for "justification"
  note that numbers are before letters in the ASCII table).
**********************************************************************/

/* A datum is one short string, or one number.
   A datum_vector represents a long string of alternating strings and
   numbers. */
struct datum {
  long numeric_value;
  char *string_value;
  bool is_numeric;
};
#define SPECVEC_TAG datum
#include "specvec.h"

/**********************************************************************
  Init a datum from a substring.
**********************************************************************/
static void init_datum_string(struct datum *dat, const char *left, 
                              const char *right)
{
  int len = right - left;

  dat->is_numeric = FALSE;
  dat->string_value = fc_malloc(len + 1);
  memcpy(dat->string_value, left, len);
  dat->string_value[len] = 0;
}

/**********************************************************************
  Init a datum from a number (a long because we happen to use
  strtol).
**********************************************************************/
static void init_datum_number(struct datum *dat, long val)
{
  dat->is_numeric = TRUE;
  dat->numeric_value = val;
}

/**********************************************************************
  Free the data associated with a datum -- that is, free the string if
  it was allocated.
**********************************************************************/
static void free_datum(struct datum *dat)
{
  if(!dat->is_numeric) {
    free(dat->string_value);
  }
}

/**********************************************************************
  Compare two data items as described above:
  - numbers in the obvious way
  - strings alphabetically
  - number < string for no good reason
**********************************************************************/
static int datum_compare(const struct datum *a, const struct datum *b)
{
  if(a->is_numeric == b->is_numeric) {
    if(a->is_numeric) {
      return a->numeric_value - b->numeric_value;
    } else {
      return strcmp(a->string_value, b->string_value);
    }
  } else {
    if(a->is_numeric) {
      return -1;
  } else {
      return 1;
  }
  }
}

/**********************************************************************
  Compare two strings of data lexicographically.
**********************************************************************/
static int data_compare(const struct datum_vector *a, 
    const struct datum_vector *b)
{
  int i, n;

  n = MIN(a->size, b->size);

  for(i = 0; i < n; i++) {
    int cmp = datum_compare(&a->p[i], &b->p[i]);

    if(cmp != 0) {
      return cmp;
    }
  }

  /* The first n fields match; whoever has more fields goes last.
     If they have equal numbers, the two really are equal. */
  return a->size - b->size;
}


/**********************************************************************
  Split a string into a vector of datum.
**********************************************************************/
static void split_string(struct datum_vector *data, const char *str)
{
  const char *string_start;

  datum_vector_init(data);
  string_start = str;
  while(*str) {
    char *endptr;
    long value;

    value = strtol(str, &endptr, 0);
    if(endptr == str) {
      /* that wasn't an integer; go on */
      str++;
    } else {
      /* that was an integer, so stop the string we were parsing, add 
         it (unless it's empty), then add the integer we just parsed */
      struct datum d;

      if(str != string_start) {
        init_datum_string(&d, string_start, str);
        datum_vector_append(data, &d);
      }

      init_datum_number(&d, value);
      datum_vector_append(data, &d);

      /* finally, update the string position pointers */
      string_start = str;
      str = endptr;
    }
  }

  /* if we have anything leftover then it's a string */
  if(str != string_start) {
    struct datum d;

    init_datum_string(&d, string_start, str);
    datum_vector_append(data, &d);
  }
}

/**********************************************************************
  Free every datum in the vector.
**********************************************************************/
static void free_data(struct datum_vector *data)
{
  int i;

  for(i = 0; i < data->size; i++) {
    free_datum(&data->p[i]);
  }
  datum_vector_free(data);
}


/**********************************************************************
  The real function: split the two strings, and compare them.
**********************************************************************/
int cityrepfield_compare(const char *str1, const char *str2)
{
  struct datum_vector data1, data2;
  int retval;

  split_string(&data1, str1);
  split_string(&data2, str2);

  retval = data_compare(&data1, &data2);

  free_data(&data1);
  free_data(&data2);

  return retval;
}
