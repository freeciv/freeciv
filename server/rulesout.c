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

/**********************************************************************
  rulesout.c:

  Write selected ruleset information to files.  Initially writes
  'techs' info for use with 'techtree' utility program.
  
  Original author:  David Pfitzner <dwp@mso.anu.edu.au>

***********************************************************************/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <ctype.h>
#include <string.h>

#include "city.h"
#include "fcintl.h"
#include "game.h"
#include "government.h"
#include "registry.h"
#include "support.h"
#include "tech.h"
#include "unit.h"

#include "rulesout.h"

/**********************************************************************
  Add data for one tech to file, in section ("tech_%d", itech)
**********************************************************************/
static void add_one_tech(struct section_file *file, Tech_Type_id itech)
{
  int ieffect, j, k;
  char prefix[32];
  char buf[MAX_LEN_NAME+64];

  /* these are based on common/tech.c, but want slightly different here: */
  const char * const tech_flag_desc[] = {
    N_("+1 tech to 1st"),
    N_("boats move +1"),
    N_("bridges"),
    N_("railroads"),
    N_("fortresses"),
    N_("pop. pollution"),
    N_("trade reduced"),
    N_("airbases"),
    N_("farmland")
  };

  my_snprintf(prefix, sizeof(prefix), "tech_%d", itech);
  
  /* tech name: */
  secfile_insert_str(file, advances[itech].name, "%s.name", prefix);

  if (!tech_exists(itech)) {
    secfile_insert_int(file, 0, "%s.exists", prefix);
    return;
  }
  if (itech == A_NONE) {
    return;
  }
  
  for(j=0; j<2; j++) {
    int req = advances[itech].req[j];
    if (req != A_NONE) {
      secfile_insert_str(file, advances[req].name, "%s.req%d", prefix, j);
    }
  }

  if (advances[itech].preset_cost != -1) {
    secfile_insert_int(file, advances[itech].preset_cost, "%s.cost",
		       prefix);
  }

  ieffect = 0;

  /* The effect tags ("gov: " etc) below are not translated, because
   * want to be able to recognise them using external programs.
   */

  /* Governments */
  for(j=0; j<game.government_count; j++ ) {
    struct government *g = get_government(j);
    if (g->required_tech == itech) {
      strcpy(buf, "gov: ");
      sz_strlcat(buf, g->name);
      secfile_insert_str(file, buf, "%s.effect%d", prefix, ieffect++);
    }
  }
    
  /* Units */
  for(j=0; j<game.num_unit_types; j++ ) {
    struct unit_type *ut = get_unit_type(j);
    if (ut->tech_requirement == itech) {
      strcpy(buf, "unit: ");
      sz_strlcat(buf, ut->name);
      secfile_insert_str(file, buf, "%s.effect%d", prefix, ieffect++);
    }
  }
    
  /* Improvements and Wonders */
  for(j=0; j<game.num_impr_types; j++) {
    struct impr_type *it = &improvement_types[j];
    if (it->tech_req == itech) {
      if (it->is_wonder ) {
	strcpy(buf, "wonder: ");
      } else {
	strcpy(buf, "impr: ");
      }
      sz_strlcat(buf, it->name);
      secfile_insert_str(file, buf, "%s.effect%d", prefix, ieffect++);
    }
  }
  
  /* Obsoleted units: */
  for(j=0; j<game.num_unit_types; j++ ) {
    struct unit_type *ut = get_unit_type(j);
    k = ut->obsoleted_by;
    if (unit_type_exists(j)
	&& k != -1
	&& unit_type_exists(k)
	&& get_unit_type(k)->tech_requirement == itech) {
      strcpy(buf, "obs-unit: ");
      sz_strlcat(buf, ut->name);
      secfile_insert_str(file, buf, "%s.effect%d", prefix, ieffect++);
    }
  }
    
  /* Obsoleted buildings: */
  for(j=0; j<game.num_impr_types; j++) {
    struct impr_type *it = &improvement_types[j];
    if (improvement_exists(j) && it->obsolete_by == itech) {
      if (it->is_wonder ) {
	strcpy(buf, "obs-wonder: ");
      } else {
	strcpy(buf, "obs-impr: ");
      }
      sz_strlcat(buf, it->name);
      secfile_insert_str(file, buf, "%s.effect%d", prefix, ieffect++);
    }
  }
  
  /* Flags: */
  for (j = 0; j < ARRAY_SIZE(tech_flag_desc); j++) {
    if (tech_flag(itech, j)) {
      strcpy(buf, "special: ");
      sz_strlcat(buf, _(tech_flag_desc[j]));
      secfile_insert_str(file, buf, "%s.effect%d", prefix, ieffect++);
    }
  }

  /* building special techs (not yet generalised) */
  if (itech == game.rtech.cathedral_plus) {
    my_snprintf(buf, sizeof(buf), "special: %s +1",
		improvement_types[B_CATHEDRAL].name);
    secfile_insert_str(file, buf, "%s.effect%d", prefix, ieffect++);
  }
  if (itech == game.rtech.cathedral_minus) {
    my_snprintf(buf, sizeof(buf), "special: %s -1",
		improvement_types[B_CATHEDRAL].name);
    secfile_insert_str(file, buf, "%s.effect%d", prefix, ieffect++);
  }
  if (itech == game.rtech.colosseum_plus) {
    my_snprintf(buf, sizeof(buf), "special: %s +1",
		improvement_types[B_COLOSSEUM].name);
    secfile_insert_str(file, buf, "%s.effect%d", prefix, ieffect++);
  }
  if (itech == game.rtech.temple_plus) {
    my_snprintf(buf, sizeof(buf), "special: %s +1",
		improvement_types[B_TEMPLE].name);
    secfile_insert_str(file, buf, "%s.effect%d", prefix, ieffect++);
  }
  
}

/**********************************************************************
  Write a file with tech information; specifically for use by techtree
  utility program, but may be useful for others as well.
  Ruleset info must have been loaded.
  Returns 1 for success, 0 for failure.
**********************************************************************/
int rulesout_techs(const char *filename)
{
  struct section_file file;
  Tech_Type_id itech;
  int retval;
  
  if (game.num_tech_types==0) {
    return FALSE;
  }
  section_file_init(&file);
  for(itech=0; itech<game.num_tech_types; itech++) {
    add_one_tech(&file, itech);
  }
  retval = section_file_save(&file, filename, 0);
  section_file_free(&file);
  return retval;
}
