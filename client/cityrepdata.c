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

#include "city.h"
#include "fcintl.h"
#include "game.h"
#include "map.h"
#include "support.h"

#include "cma_fec.h"
#include "options.h"

#include "cityrepdata.h"

/************************************************************************
 cr_entry = return an entry (one column for one city) for the city report
 These return ptrs to filled in static strings.
 Note the returned string may not be exactly the right length; that
 is handled later.
*************************************************************************/

static const char *cr_entry_cityname(const struct city *pcity)
{
  /* We used to truncate the name to 14 bytes.  This should not be needed
   * in any modern GUI library and may give an invalid string if a
   * multibyte character is clipped. */
  return pcity->name;
}

static const char *cr_entry_size(const struct city *pcity)
{
  static char buf[8];
  my_snprintf(buf, sizeof(buf), "%2d", pcity->size);
  return buf;
}

static const char *cr_entry_hstate_concise(const struct city *pcity)
{
  static char buf[4];
  my_snprintf(buf, sizeof(buf), "%s", (city_celebrating(pcity) ? "*" :
				       (city_unhappy(pcity) ? "X" : " ")));
  return buf;
}

static const char *cr_entry_hstate_verbose(const struct city *pcity)
{
  static char buf[16];
  my_snprintf(buf, sizeof(buf), "%s",
	      (city_celebrating(pcity) ? Q_("?city_state:Rapture") :
	       (city_unhappy(pcity) ? Q_("?city_state:Disorder") :
		Q_("?city_state:Peace"))));
  return buf;
}

static const char *cr_entry_workers(const struct city *pcity)
{
  static char buf[32];
  my_snprintf(buf, sizeof(buf), "%d/%d/%d/%d", pcity->ppl_happy[4],
	      pcity->ppl_content[4], pcity->ppl_unhappy[4],
	      pcity->ppl_angry[4]);
  return buf;
}

static const char *cr_entry_happy(const struct city *pcity)
{
  static char buf[8];
  my_snprintf(buf, sizeof(buf), "%2d", pcity->ppl_happy[4]);
  return buf;
}

static const char *cr_entry_content(const struct city *pcity)
{
  static char buf[8];
  my_snprintf(buf, sizeof(buf), "%2d", pcity->ppl_content[4]);
  return buf;
}

static const char *cr_entry_unhappy(const struct city *pcity)
{
  static char buf[8];
  my_snprintf(buf, sizeof(buf), "%2d", pcity->ppl_unhappy[4]);
  return buf;
}

static const char *cr_entry_angry(const struct city *pcity)
{
  static char buf[8];
  my_snprintf(buf, sizeof(buf), "%2d", pcity->ppl_angry[4]);
  return buf;
}

static const char *cr_entry_specialists(const struct city *pcity)
{
  return specialists_string(pcity->specialists);
}

static const char *cr_entry_entertainers(const struct city *pcity)
{
  static char buf[8];
  my_snprintf(buf, sizeof(buf), "%2d", pcity->specialists[SP_ELVIS]);
  return buf;
}

static const char *cr_entry_scientists(const struct city *pcity)
{
  static char buf[8];
  my_snprintf(buf, sizeof(buf), "%2d", pcity->specialists[SP_SCIENTIST]);
  return buf;
}

static const char *cr_entry_taxmen(const struct city *pcity)
{
  static char buf[8];
  my_snprintf(buf, sizeof(buf), "%2d", pcity->specialists[SP_TAXMAN]);
  return buf;
}

static const char *cr_entry_attack(const struct city *pcity)
{
  static char buf[32];
  int attack_best[4] = {-1, -1, -1, -1}, i;

  unit_list_iterate(pcity->tile->units, punit) {
    /* What about allied units?  Should we just count them? */
    attack_best[3] = get_unit_type(punit->type)->attack_strength;

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
      my_snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf),
		  "%s%d", (i > 0) ? "/" : "", attack_best[i]);
    } else {
      my_snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf),
		  "%s-", (i > 0) ? "/" : "");
    }
  }

  return buf;
}

static const char *cr_entry_defense(const struct city *pcity)
{
  static char buf[32];
  int defense_best[4] = {-1, -1, -1, -1}, i;

  unit_list_iterate(pcity->tile->units, punit) {
    /* What about allied units?  Should we just count them? */
    defense_best[3] = get_unit_type(punit->type)->defense_strength;

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
      my_snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf),
		  "%s%d", (i > 0) ? "/" : "", defense_best[i]);
    } else {
      my_snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf),
		  "%s-", (i > 0) ? "/" : "");
    }
  }

  return buf;
}

static const char *cr_entry_supported(const struct city *pcity)
{
  static char buf[8];
  int num_supported = 0;

  unit_list_iterate(pcity->units_supported, punit) {
    num_supported++;
  } unit_list_iterate_end;

  my_snprintf(buf, sizeof(buf), "%2d", num_supported);

  return buf;
}

static const char *cr_entry_present(const struct city *pcity)
{
  static char buf[8];
  int num_present = 0;

  unit_list_iterate(pcity->tile->units, punit) {
    num_present++;
  } unit_list_iterate_end;

  my_snprintf(buf, sizeof(buf), "%2d", num_present);

  return buf;
}

static const char *cr_entry_resources(const struct city *pcity)
{
  static char buf[32];
  my_snprintf(buf, sizeof(buf), "%d/%d/%d",
	      pcity->food_surplus, 
	      pcity->shield_surplus, 
	      pcity->trade_prod);
  return buf;
}

static const char *cr_entry_foodplus(const struct city *pcity)
{
  static char buf[8];
  my_snprintf(buf, sizeof(buf), "%3d",
	      pcity->food_surplus);
  return buf;
}

static const char *cr_entry_prodplus(const struct city *pcity)
{
  static char buf[8];
  my_snprintf(buf, sizeof(buf), "%3d",
	      pcity->shield_surplus);
  return buf;
}

static const char *cr_entry_tradeplus(const struct city *pcity)
{
  static char buf[8];
  my_snprintf(buf, sizeof(buf), "%3d",
	      pcity->trade_prod);
  return buf;
}

static const char *cr_entry_output(const struct city *pcity)
{
  static char buf[32];
  int goldie;

  goldie = city_gold_surplus(pcity, pcity->tax_total);
  my_snprintf(buf, sizeof(buf), "%s%d/%d/%d",
	      (goldie < 0) ? "-" : (goldie > 0) ? "+" : "",
	      (goldie < 0) ? (-goldie) : goldie,
	      pcity->luxury_total,
	      pcity->science_total);
  return buf;
}

static const char *cr_entry_gold(const struct city *pcity)
{
  static char buf[8];
  int income = city_gold_surplus(pcity, pcity->tax_total);
  if (income > 0) {
    my_snprintf(buf, sizeof(buf), "+%d", income);
  } else {
    my_snprintf(buf, sizeof(buf), "%3d", city_gold_surplus(pcity, pcity->tax_total));
  }
  return buf;
}

static const char *cr_entry_luxury(const struct city *pcity)
{
  static char buf[8];
  my_snprintf(buf, sizeof(buf), "%3d",
	      pcity->luxury_total);
  return buf;
}

static const char *cr_entry_science(const struct city *pcity)
{
  static char buf[8];
  my_snprintf(buf, sizeof(buf), "%3d",
	      pcity->science_total);
  return buf;
}

static const char *cr_entry_food(const struct city *pcity)
{
  static char buf[32];
  my_snprintf(buf, sizeof(buf), "%d/%d",
	      pcity->food_stock,
	      city_granary_size(pcity->size) );
  return buf;
}

static const char *cr_entry_growturns(const struct city *pcity)
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

static const char *cr_entry_pollution(const struct city *pcity)
{
  static char buf[8];
  my_snprintf(buf, sizeof(buf), "%3d", pcity->pollution);
  return buf;
}

static const char *cr_entry_num_trade(const struct city *pcity)
{
  static char buf[8];
  my_snprintf(buf, sizeof(buf), "%d", city_num_trade_routes(pcity));
  return buf;
}

static const char *cr_entry_building(const struct city *pcity)
{
  static char buf[128];
  const char *from_worklist =
    worklist_is_empty(&pcity->worklist) ? "" :
    concise_city_production ? "*" : _("(worklist)");
	
  if (get_current_construction_bonus(pcity, EFT_PROD_TO_GOLD) > 0) {
    my_snprintf(buf, sizeof(buf), "%s (%d/X/X/X)%s",
		get_impr_name_ex(pcity, pcity->currently_building),
		MAX(0, pcity->shield_surplus), from_worklist);
  } else {
    int turns = city_turns_to_build(pcity, pcity->currently_building,
				    pcity->is_building_unit, TRUE);
    char time[32];
    const char *name;
    int cost;

    if (turns < 999) {
      my_snprintf(time, sizeof(time), "%d", turns);
    } else {
      my_snprintf(time, sizeof(time), "-");
    }

    if(pcity->is_building_unit) {
      name = get_unit_type(pcity->currently_building)->name;
      cost = unit_build_shield_cost(pcity->currently_building);
    } else {
      name = get_impr_name_ex(pcity, pcity->currently_building);
      cost = impr_build_shield_cost(pcity->currently_building);
    }

    my_snprintf(buf, sizeof(buf), "%s (%d/%d/%s/%d)%s", name,
		pcity->shield_stock, cost, time, city_buy_cost(pcity),
		from_worklist);
  }

  return buf;
}

static const char *cr_entry_corruption(const struct city *pcity)
{
  static char buf[8];
  my_snprintf(buf, sizeof(buf), "%3d", pcity->corruption);
  return buf;
}

static const char *cr_entry_waste(const struct city *pcity)
{
  static char buf[8];
  my_snprintf(buf, sizeof(buf), "%3d", pcity->shield_waste);
  return buf;
}

static const char *cr_entry_cma(const struct city *pcity)
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

struct city_report_spec city_report_specs[] = {
  { TRUE, -15, 0, NULL,  N_("?city:Name"),      N_("City Name"),
                                      FUNC_TAG(cityname) },
  { TRUE, 2, 1, NULL,  N_("?size:Sz"),        N_("Size"),
                                      FUNC_TAG(size) },
  { TRUE,  -8, 1, NULL,  N_("State"),     N_("Rapture/Peace/Disorder"),
                                      FUNC_TAG(hstate_verbose) },
  { FALSE,  1, 1, NULL,  NULL,            N_("Concise *=Rapture, X=Disorder"),
                                      FUNC_TAG(hstate_concise) },
  { TRUE, 10, 1, N_("Workers"),
    N_("?happy/content/unhappy/angry:H/C/U/A"),
    N_("Workers: Happy, Content, Unhappy, Angry"),
    FUNC_TAG(workers) },
  { FALSE, 2, 1, NULL, N_("?Happy workers:H"), N_("Workers: Happy"),
    FUNC_TAG(happy) },
  { FALSE, 2, 1, NULL, N_("?Content workers:C"), N_("Workers: Content"),
    FUNC_TAG(content) },
  { FALSE, 2, 1, NULL, N_("?Unhappy workers:U"), N_("Workers: Unhappy"),
    FUNC_TAG(unhappy) },
  { FALSE, 2, 1, NULL, N_("?Angry workers:A"), N_("Workers: Angry"),
    FUNC_TAG(angry) },
  { FALSE, 7, 1, N_("Special"),
    N_("?entertainers/scientists/taxmen:E/S/T"),
    N_("Entertainers, Scientists, Taxmen"),
    FUNC_TAG(specialists) },
  { FALSE, 2, 1, NULL, N_("?Entertainers:E"), N_("Entertainers"),
    FUNC_TAG(entertainers) },
  { FALSE, 2, 1, NULL, N_("?Scientists:S"), N_("Scientists"),
    FUNC_TAG(scientists) },
  { FALSE, 2, 1, NULL, N_("?Taxmen:T"), N_("Taxmen"),
    FUNC_TAG(taxmen) },
  { FALSE, 8, 1, N_("Best"), N_("attack"),
    N_("Best attacking units"), FUNC_TAG(attack)},
  { FALSE, 8, 1, N_("Best"), N_("defense"),
    N_("Best defending units"), FUNC_TAG(defense)},
  { FALSE, 2, 1, N_("Units"), N_("?Supported (units):Sup"),
    N_("Number of units supported"), FUNC_TAG(supported) },
  { FALSE, 2, 1, N_("Units"), N_("?Present (units):Prs"),
    N_("Number of units present"), FUNC_TAG(present) },

  { TRUE,  10, 1, N_("Surplus"), N_("?food/prod/trade:F/P/T"),
                                 N_("Surplus: Food, Production, Trade"),
                                      FUNC_TAG(resources) },
  { FALSE, 3, 1, NULL, N_("?Food surplus:F+"), N_("Surplus: Food"),
    FUNC_TAG(foodplus) },
  { FALSE, 3, 1, NULL, N_("?Production surplus:P+"),
    N_("Surplus: Production"), FUNC_TAG(prodplus) },
  { FALSE, 3, 1, NULL, N_("?Trade surplus:T+"), N_("Surplus: Trade"),
    FUNC_TAG(tradeplus) },
  { TRUE,  10, 1, N_("Economy"), N_("?gold/lux/sci:G/L/S"),
                                 N_("Economy: Gold, Luxuries, Science"),
                                      FUNC_TAG(output) },
  { FALSE, 3, 1, NULL, N_("?Gold:G"), N_("Economy: Gold"),
    FUNC_TAG(gold) },
  { FALSE, 3, 1, NULL, N_("?Luxury:L"), N_("Economy: Luxury"),
    FUNC_TAG(luxury) },
  { FALSE, 3, 1, NULL, N_("?Science:S"), N_("Economy: Science"),
    FUNC_TAG(science) },
  { FALSE,  1, 1, N_("?trade_routes:n"), N_("?trade_routes:T"),
                                         N_("Number of Trade Routes"),
                                      FUNC_TAG(num_trade) },
  { TRUE,   7, 1, N_("Food"), N_("Stock"), N_("Food Stock"),
                                      FUNC_TAG(food) },
  { FALSE,  3, 1, NULL, N_("?pollution:Pol"),        N_("Pollution"),
                                      FUNC_TAG(pollution) },
  { FALSE,  4, 1, N_("Grow"), N_("Turns"), N_("Turns until growth/famine"),
    FUNC_TAG(growturns) },
  { FALSE,  3, 1, NULL, N_("?corruption:Cor"),        N_("Corruption"),
                                      FUNC_TAG(corruption) },
  { FALSE,  3, 1, NULL, N_("?waste:Was"), N_("Waste"), FUNC_TAG(waste) },
  { TRUE,  15, 1, NULL, N_("CMA"),	      N_("City Management Agent"),
                                      FUNC_TAG(cma) },
  { TRUE,   0, 1, N_("Currently Building"), N_("(Stock,Target,Turns,Buy)"),
                                            N_("Currently Building"),
                                      FUNC_TAG(building) }
};

#if 0
const int num_report_cols = ARRAY_SIZE(city_report_specs);
#endif

/******************************************************************
Some simple wrappers:
******************************************************************/
int num_city_report_spec(void)
{
  return NUM_CREPORT_COLS;
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
void init_city_report_data(void)
{
  int i;

  for (i = 0; i < NUM_CREPORT_COLS; i++) {
    struct city_report_spec* p = &city_report_specs[i];

    if (p->title1) {
      p->title1 = Q_(p->title1);
    }
    if (p->title2) {
      p->title2 = Q_(p->title2);
    }
    p->explanation = _(p->explanation);
  }

  assert(NUM_CREPORT_COLS == ARRAY_SIZE(city_report_specs));
}

/**********************************************************************
  This allows more intelligent sorting city report fields by column,
  although it still does not give the preferred behavior for all
  fields.

  The GUI can give us the two fields and we will try to guess if
  they are text or numeric fields. It returns a number less then,
  equal to, or greater than 0 if field1 is less than, equal to, or
  greater than field2, respectively. If we are given two text
  fields, we will compare them as text. If we are given one text and
  one numerical field, we will place the numerical field first.
**********************************************************************/
int cityrepfield_compare(const char *field1, const char *field2)
{
  int scanned1, scanned2;
  int number1, number2;

  scanned1 = sscanf(field1, "%d", &number1);
  scanned2 = sscanf(field2, "%d", &number2);

  if (scanned1 == 1 && scanned2 == 1) {
    /* Both fields are numerical.  Compare them numerically. */
    return number1 - number2;
  } else if (scanned1 == 0 && scanned2 == 0) {
    /* Both fields are text.  Compare them as strings. */
    return strcmp(field1, field2);
  } else {
    /* One field is numerical and one field is text.  To preserve
     * the logic of comparison sorting we must always sort one before
     * the other. */
    return scanned1 - scanned2;
  }
}
