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
#include <string.h>

#include "city.h"
#include "fcintl.h"
#include "game.h"
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

static char *cr_entry_cityname(struct city *pcity)
{
  static char buf[REPORT_CITYNAME_ABBREV+1];
  if (strlen(pcity->name) <= REPORT_CITYNAME_ABBREV) {
    return pcity->name;
  } else {
    strncpy(buf, pcity->name, REPORT_CITYNAME_ABBREV-1);
    buf[REPORT_CITYNAME_ABBREV-1] = '.';
    buf[REPORT_CITYNAME_ABBREV] = '\0';
    return buf;
  }
}

static char *cr_entry_size(struct city *pcity)
{
  static char buf[8];
  my_snprintf(buf, sizeof(buf), "%2d", pcity->size);
  return buf;
}

static char *cr_entry_hstate_concise(struct city *pcity)
{
  static char buf[4];
  my_snprintf(buf, sizeof(buf), "%s", (city_celebrating(pcity) ? "*" :
				       (city_unhappy(pcity) ? "X" : " ")));
  return buf;
}

static char *cr_entry_hstate_verbose(struct city *pcity)
{
  static char buf[16];
  my_snprintf(buf, sizeof(buf), "%s",
	      (city_celebrating(pcity) ? Q_("?city_state:Rapture") :
	       (city_unhappy(pcity) ? Q_("?city_state:Disorder") :
		Q_("?city_state:Peace"))));
  return buf;
}

static char *cr_entry_workers(struct city *pcity)
{
  static char buf[32];
  my_snprintf(buf, sizeof(buf), "%d/%d/%d/%d", pcity->ppl_happy[4],
	      pcity->ppl_content[4], pcity->ppl_unhappy[4],
	      pcity->ppl_angry[4]);
  return buf;
}

static char *cr_entry_specialists(struct city *pcity)
{
  static char buf[32];
  my_snprintf(buf, sizeof(buf), "%d/%d/%d",
	      pcity->ppl_elvis,
	      pcity->ppl_scientist,
	      pcity->ppl_taxman);
  return buf;
}

static char *cr_entry_resources(struct city *pcity)
{
  static char buf[32];
  my_snprintf(buf, sizeof(buf), "%d/%d/%d",
	      pcity->food_surplus, 
	      pcity->shield_surplus, 
	      pcity->trade_prod);
  return buf;
}

static char *cr_entry_output(struct city *pcity)
{
  static char buf[32];
  int goldie;

  goldie = city_gold_surplus(pcity);
  my_snprintf(buf, sizeof(buf), "%s%d/%d/%d",
	      (goldie < 0) ? "-" : (goldie > 0) ? "+" : "",
	      (goldie < 0) ? (-goldie) : goldie,
	      pcity->luxury_total,
	      pcity->science_total);
  return buf;
}

static char *cr_entry_food(struct city *pcity)
{
  static char buf[32];
  my_snprintf(buf, sizeof(buf), "%d/%d",
	      pcity->food_stock,
	      city_granary_size(pcity->size) );
  return buf;
}

static char *cr_entry_pollution(struct city *pcity)
{
  static char buf[8];
  my_snprintf(buf, sizeof(buf), "%3d", pcity->pollution);
  return buf;
}

static char *cr_entry_num_trade(struct city *pcity)
{
  static char buf[8];
  my_snprintf(buf, sizeof(buf), "%d", city_num_trade_routes(pcity));
  return buf;
}

static char *cr_entry_building(struct city *pcity)
{
  static char buf[128];
  char *from_worklist =
    worklist_is_empty(&pcity->worklist) ? "" :
    concise_city_production ? "*" : _("(worklist)");
	
  if (!pcity->is_building_unit && pcity->currently_building == B_CAPITAL) {
    my_snprintf(buf, sizeof(buf), "%s (%d/X/X/X)%s",
		get_impr_name_ex(pcity, pcity->currently_building),
		MAX(0, pcity->shield_surplus), from_worklist);
  } else {
    int turns = city_turns_to_build(pcity, pcity->currently_building,
				    pcity->is_building_unit, TRUE);
    char time[32], *name;
    int cost;

    if (turns < 999) {
      my_snprintf(time, sizeof(time), "%d", turns);
    } else {
      my_snprintf(time, sizeof(time), "-");
    }

    if(pcity->is_building_unit) {
      name = get_unit_type(pcity->currently_building)->name;
      cost = get_unit_type(pcity->currently_building)->build_cost;
    } else {
      name = get_impr_name_ex(pcity, pcity->currently_building);
      cost = get_improvement_type(pcity->currently_building)->build_cost;
    }

    my_snprintf(buf, sizeof(buf), "%s (%d/%d/%s/%d)%s", name,
		pcity->shield_stock, cost, time, city_buy_cost(pcity),
		from_worklist);
  }

  return buf;
}

static char *cr_entry_corruption(struct city *pcity)
{
  static char buf[8];
  my_snprintf(buf, sizeof(buf), "%3d", pcity->corruption);
  return buf;
}

static char *cr_entry_cma(struct city *pcity)
{
  return (char *) cmafec_get_short_descr_of_city(pcity);
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
  { TRUE, -15, 0, NULL,  N_("Name"),      N_("City Name"),
                                      FUNC_TAG(cityname) },
  { FALSE,  2, 1, NULL,  N_("Sz"),        N_("Size"),
                                      FUNC_TAG(size) },
  { TRUE,  -8, 1, NULL,  N_("State"),     N_("Rapture/Peace/Disorder"),
                                      FUNC_TAG(hstate_verbose) },
  { FALSE,  1, 1, NULL,  NULL,            N_("Concise *=Rapture, X=Disorder"),
                                      FUNC_TAG(hstate_concise) },
  { TRUE,  10, 1, N_("Workers"), N_("H/C/U/A"),
                                      N_("Workers: Happy, Content, Unhappy, Angry"),
                                      FUNC_TAG(workers) },
  { FALSE,  7, 1, N_("Special"), N_("E/S/T"),
                                      N_("Entertainers, Scientists, Taxmen"),
                                      FUNC_TAG(specialists) },
  { TRUE,  10, 1, N_("Surplus"), N_("F/P/T"),
                                      N_("Surplus: Food, Production, Trade"),
                                      FUNC_TAG(resources) },
  { TRUE,  10, 1, N_("Economy"), N_("G/L/S"),
                                      N_("Economy: Gold, Luxuries, Science"),
                                      FUNC_TAG(output) },
  { FALSE,  1, 1, N_("n"), N_("T"),       N_("Number of Trade Routes"),
                                      FUNC_TAG(num_trade) },
  { TRUE,   7, 1, N_("Food"), N_("Stock"), N_("Food Stock"),
                                      FUNC_TAG(food) },
  { FALSE,  3, 1, NULL, N_("Pol"),        N_("Pollution"),
                                      FUNC_TAG(pollution) },
  { FALSE,  3, 1, NULL, N_("Cor"),        N_("Corruption"),
                                      FUNC_TAG(corruption) },
  { TRUE,  15, 1, NULL, N_("CMA"),	      N_("City Management Agent"),
                                      FUNC_TAG(cma) },
  { TRUE,   0, 1, N_("Currently Building"), N_("(Stock,Target,Turns,Buy)"),
                                      N_("Currently Building"),
                                      FUNC_TAG(building) }
};

/* #define NUM_CREPORT_COLS \
   sizeof(city_report_specs)/sizeof(city_report_specs[0])
*/
     
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
char *city_report_spec_tagname(int i)
{
  return city_report_specs[i].tagname;
}
