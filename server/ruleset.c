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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "capability.h"
#include "city.h"
#include "game.h"
#include "log.h"
#include "packets.h"
#include "registry.h"
#include "tech.h"
#include "unit.h"
#include "map.h"

#include "ruleset.h"

static char *openload_ruleset_file(struct section_file *file,
				   char *subdir, char *whichset);
static char *check_ruleset_capabilities(struct section_file *file,
					char *required,	char *filename);
static int lookup_unit_type(struct section_file *file, char *prefix,
			    char *entry, int required, char *filename,
			    char *description);
static int lookup_tech(struct section_file *file, char *prefix,
		       char *entry, int required, char *filename,
		       char *description);
static int match_name_from_list(char *name, char **list, int n_list);
static enum tile_terrain_type lookup_terrain(char *name, int this);

static void load_ruleset_techs(char *ruleset_subdir);
static void load_ruleset_units(char *ruleset_subdir);
static void load_ruleset_buildings(char *ruleset_subdir);
static void load_ruleset_terrain(char *ruleset_subdir);

static void send_ruleset_techs(struct player *dest);
static void send_ruleset_units(struct player *dest);
static void send_ruleset_buildings(struct player *dest);
static void send_ruleset_terrain(struct player *dest);

static void old_load_ruleset_units(char *filename, char *datafile_options,
				   struct section_file *file);
static void new_load_ruleset_units(char *filename, char *datafile_options,
				   struct section_file *file);
static void after_load_ruleset_units(char *filename);


/**************************************************************************
  Do initial section_file_load on a ruleset file.
  "subdir" = "default", "civ1", "custom", ...
  "whichset" = "techs", "units", "buildings", "terrain" (...)
  returns ptr to static memory with full filename including data directory. 
**************************************************************************/
static char *openload_ruleset_file(struct section_file *file,
				   char *subdir, char *whichset)
{
  char filename1[512], filename2[512], *dfilename;

  sprintf(filename1, "%s_%s.ruleset", subdir, whichset);
  dfilename = datafilename(filename1);
  if (!dfilename) {
    sprintf(filename2, "%s/%s.ruleset", subdir, whichset);
    dfilename = datafilename(filename2);
    if (!dfilename) {
      freelog(LOG_FATAL, "Could not find readable ruleset file \"%s\"",
	      filename1);
      freelog(LOG_FATAL, "or \"%s\" in data path", filename2);
      freelog(LOG_FATAL, "The data path may be set via"
	      " the environment variable FREECIV_PATH");
      freelog(LOG_FATAL, "Current data path is: \"%s\"", datafilename(NULL));
      exit(1);
    }
  }
  if (!section_file_load(file,dfilename)) {
    freelog(LOG_FATAL, "Could not load ruleset file %s", dfilename);
    exit(1);
  }
  return dfilename;
}

/**************************************************************************
  Ruleset files should have a capabilities string datafile.options
  This gets and returns that string, and checks that the required
  capabilites specified are satisified.
**************************************************************************/
static char *check_ruleset_capabilities(struct section_file *file,
					char *required, char *filename)
{
  char *datafile_options;
  
  datafile_options = secfile_lookup_str(file, "datafile.options");
  if (!has_capability(required, datafile_options)) {
    freelog(LOG_FATAL, "Datafile capability problem for file %s", filename );
    freelog(LOG_FATAL, "File capability: \"%s\", required: \"%s\"",
	 datafile_options, required);
    exit(1);
  }
  return datafile_options;
}


/**************************************************************************
 Lookup a string prefix.entry in the file and return the corresponding
 unit_type id.  If (!required), return -1 if match "None" or can't match.
 If (required), die if can't match.
 If description is not NULL, it is used in the warning message
 instead of prefix (eg pass unit->name instead of prefix="units2.u27")
**************************************************************************/
static int lookup_unit_type(struct section_file *file, char *prefix,
			    char *entry, int required, char *filename,
			    char *description)
{
  char *sval;
  int i;
  
  sval = secfile_lookup_str(file, "%s.%s", prefix, entry);
  if (!required && strcmp(sval, "None")==0) {
    i = -1;
  } else {
    i = find_unit_type_by_name(sval);
    if (i==U_LAST) {
      freelog((required?LOG_FATAL:LOG_NORMAL),
	   "for %s %s couldn't match unit_type \"%s\" (%s)",
	   (description?description:prefix), entry, sval, filename);
      if (required) {
	exit(1);
      } else {
	i = -1;
      }
    }
  }
  return i;
}

/**************************************************************************
 Lookup a string prefix.entry in the file and return the corresponding
 advances id.  If (!required), return A_LAST if match "Never" or can't match.
 If (required), die if can't match.  Note the first tech should have
 name "None" so that will always match.
 If description is not NULL, it is used in the warning message
 instead of prefix (eg pass unit->name instead of prefix="units2.u27")
**************************************************************************/
static int lookup_tech(struct section_file *file, char *prefix,
		       char *entry, int required, char *filename,
		       char *description)
{
  char *sval;
  int i;
  
  sval = secfile_lookup_str(file, "%s.%s", prefix, entry);
  if (!required && strcmp(sval, "Never")==0) {
    i = A_LAST;
  } else {
    i = find_tech_by_name(sval);
    if (i==A_LAST) {
      freelog((required?LOG_FATAL:LOG_NORMAL),
	   "for %s %s couldn't match tech \"%s\" (%s)",
	   (description?description:prefix), entry, sval, filename);
      if (required) {
	exit(1);
      } else {
	i = A_LAST;
      }
    }
  }
  return i;
}

/**************************************************************************
 Return the index of the name in the list, or -1
**************************************************************************/
static int match_name_from_list(char *name, char **list, int n_list)
{
  int i;

  for(i=0; i<n_list; i++) {
    if(strcmp(name,list[i])==0) {
      return i;
    }
  }
  return -1;
}


/**************************************************************************
 Lookup a terrain name in the tile_types array; return its index.
**************************************************************************/
static enum tile_terrain_type lookup_terrain(char *name, int this)
{
  int i;

  if ((!(*name)) || (0 == strcmp(name, "none")) || (0 == strcmp(name, "no")))
    {
      return (T_LAST);
    }
  else if (0 == strcmp(name, "yes"))
    {
      return (this);
    }

  for (i = T_FIRST; i < T_COUNT; i++)
    {
      if (0 == strcmp(name, tile_types[i].terrain_name))
	{
	  return (i);
	}
    }

  return (T_UNKNOWN);
}

/**************************************************************************
  ...  
  This must be done before the other load_ruleset_ functions,
  since other rulesets want to match tech names.
**************************************************************************/
static void load_ruleset_techs(char *ruleset_subdir)
{
  struct section_file file;
  char *filename, *datafile_options;
  char prefix[64];
  struct advance *a;
  int i;
  
  filename = openload_ruleset_file(&file, ruleset_subdir, "techs");
  datafile_options = check_ruleset_capabilities(&file, "1.7", filename);
  section_file_lookup(&file,"datafile.description"); /* unused */

  /* have to read all names first, so can lookup names for reqs! */
  for( i=0; i<A_LAST; i++ ) {
    a = &advances[i];
    strcpy(a->name, secfile_lookup_str(&file, "advances.a%d.name", i));
  }
  
  for( i=0; i<A_LAST; i++ ) {
    a = &advances[i];
    sprintf(prefix, "advances.a%d", i);

    a->req[0] = lookup_tech(&file, prefix, "req1", 0, filename, a->name);
    a->req[1] = lookup_tech(&file, prefix, "req2", 0, filename, a->name);
    
    if ((a->req[0]==A_LAST && a->req[1]!=A_LAST) ||
	(a->req[0]!=A_LAST && a->req[1]==A_LAST)) {
      freelog(LOG_NORMAL, "for tech %s: \"Never\" with non-\"Never\" (%s)",
	   a->name, filename);
      a->req[0] = a->req[1] = A_LAST;
    }
    if (a->req[0]==A_NONE && a->req[1]!=A_NONE) {
      freelog(LOG_NORMAL, "tech %s: should have \"None\" second (%s)",
	   a->name, filename);
      a->req[0] = a->req[1];
      a->req[1] = A_NONE;
    }
  }

  /* Some more consistency checking: 
     Non-removed techs depending on removed techs is too
     broken to fix by default, so die.
  */   
  for( i=0; i<A_LAST; i++ ) {
    if (tech_exists(i)) {
      a = &advances[i];
      if (!tech_exists(a->req[0])) {
	freelog(LOG_FATAL, "tech \"%s\": req1 leads to removed tech \"%s\" (%s)",
	     a->name, advances[a->req[0]].name, filename);
	exit(1);
      } 
      if (!tech_exists(a->req[1])) {
	freelog(LOG_FATAL, "tech \"%s\": req2 leads to removed tech \"%s\" (%s)",
	     a->name, advances[a->req[1]].name, filename);
	exit(1);
      }
    }
  } 
  
  game.rtech.get_bonus_tech =
    lookup_tech(&file, "a_special", "bonus_tech", 0, filename, NULL);
  game.rtech.boat_fast =
    lookup_tech(&file, "a_special", "boat_fast", 0, filename, NULL);
  
  section_file_check_unused(&file, filename);
  section_file_free(&file);
}

/**************************************************************************
...  
**************************************************************************/
static void load_ruleset_units(char *ruleset_subdir)
{
  struct section_file file;
  char *filename, *datafile_options;

  filename = openload_ruleset_file(&file, ruleset_subdir, "units");
  datafile_options = check_ruleset_capabilities(&file, "1.7", filename);
  section_file_lookup(&file,"datafile.description"); /* unused */

  if (has_capability("nontabular", datafile_options)) {
    new_load_ruleset_units(filename, datafile_options, &file);
  } else {
    old_load_ruleset_units(filename, datafile_options, &file);
  }
  section_file_check_unused(&file, filename);
  section_file_free(&file);
  after_load_ruleset_units(filename);
  return;
}

/**************************************************************************
  This is for old-style (tabular) units.ruleset files.
  This support could disappear in future.
**************************************************************************/
static void old_load_ruleset_units(char *filename, char *datafile_options,
				   struct section_file *file)
{
  char prefix[64];
  struct unit_type *u;
  int max_hp, max_firepower;
  char **flag_names;
  int i, j, ival, nval, num_flag_names;
  char *sval, **slist;

  section_file_lookup(file,"datafile.description"); /* unused */

  max_hp =
    secfile_lookup_int_default(file, 0, "unit_adjustments.max_hitpoints");
  max_firepower =
    secfile_lookup_int_default(file, 0, "unit_adjustments.max_firepower");
  game.firepower_factor =
    secfile_lookup_int_default(file, 1, "unit_adjustments.firepower_factor");

  /* The names: */
  for( i=0; i<U_LAST; i++ ) {
    u = &unit_types[i];
    strcpy(u->name, secfile_lookup_str(file, "units.u1%d.name", i));
  }

  /* Tech requirement is used to flag removed unit_types, which
     we might want to know for other fields.  After this we
     can use unit_type_exists()
  */
  for( i=0; i<U_LAST; i++ ) {
    u = &unit_types[i];
    sprintf(prefix, "units.u1%d", i);
    u->tech_requirement = lookup_tech(file, prefix, "tech_requirement",
				      0, filename, u->name);
  }
  for( i=0; i<U_LAST; i++ ) {
    u = &unit_types[i];
    sprintf(prefix, "units.u1%d", i);
    if (unit_type_exists(i)) {
      u->obsoleted_by = lookup_unit_type(file, prefix, "obsoleted_by", 0,
					 filename, u->name);
    } else {
      section_file_lookup(file, "%s.obsoleted_by", prefix);  /* unused */
      u->obsoleted_by = -1;
    }
  }
  
  /* second block: */
  for( i=0; i<U_LAST; i++ ) {
    u = &unit_types[i];
    sprintf(prefix, "units.u2%d", i);
    
    sval = secfile_lookup_str(file, "%s.name", prefix);
    if (strcmp(u->name, sval) != 0) {
      freelog(LOG_NORMAL, "for unit_type \"%s\": mismatch name \"%s\" in "
	   "block 2 (%s)", u->name, sval, filename);
    }

    u->move_type = ival = secfile_lookup_int(file,"%s.move_type", prefix);
    if (ival<LAND_MOVING || ival>AIR_MOVING) {
      freelog(LOG_FATAL, "for unit_type \"%s\": bad move_type %d (%s)",
	   u->name, ival, filename);
      exit(1);
    }
    u->graphics =
      secfile_lookup_int(file,"%s.graphics", prefix);
    u->build_cost =
      secfile_lookup_int(file,"%s.build_cost", prefix);
    u->attack_strength =
      secfile_lookup_int(file,"%s.attack_strength", prefix);
    u->defense_strength =
      secfile_lookup_int(file,"%s.defense_strength", prefix);
    u->move_rate =
      3*secfile_lookup_int(file,"%s.move_rate", prefix);
    
    u->vision_range =
      secfile_lookup_int(file,"%s.vision_range", prefix);
    u->transport_capacity =
      secfile_lookup_int(file,"%s.transport_capacity", prefix);
    u->hp = secfile_lookup_int(file,"%s.hitpoints", prefix);
    if( max_hp && u->hp > max_hp ) {
      u->hp = max_hp;
    }
    u->firepower = secfile_lookup_int(file,"%s.firepower", prefix);
    if( max_firepower && u->firepower > max_firepower ) {
      u->firepower = max_firepower;
    }
    u->fuel = secfile_lookup_int(file,"%s.fuel", prefix);
  }
  
  /* third block: flags */
  flag_names = secfile_lookup_str_vec(file, &num_flag_names,
				      "units.flag_names");
  if (num_flag_names==0) {
    freelog(LOG_FATAL, "Couldn't find units.flag_names in %s", filename);
    exit(1);
  }
    
  for(i=0; i<U_LAST; i++) {
    u = &unit_types[i];
    u->flags = 0;
    sprintf(prefix, "units.u3%d", i);
    
    sval = secfile_lookup_str(file, "%s.name", prefix);
    if (strcmp(u->name, sval) != 0) {
      freelog(LOG_NORMAL, "for unit_type \"%s\": mismatch name \"%s\" in "
	   "block 3 (%s)", u->name, sval, filename);
    }
    slist = secfile_lookup_str_vec(file, &nval, "%s.flags", prefix );
    for(j=0; j<nval; j++) {
      sval = slist[j];
      if(strcmp(sval,"")==0) {
	continue;
      }
      ival = match_name_from_list(sval, flag_names, num_flag_names);
      if (ival==-1) {
	freelog(LOG_NORMAL, "for unit_type \"%s\": unmatched flag name \"%s\" (%s)",
	     u->name, sval, filename);
      }
      u->flags |= (1<<ival);
    }
    free(slist);
  }
  free(flag_names);
    
  /* fourth block: roles */
  flag_names = secfile_lookup_str_vec(file, &num_flag_names,
				      "units.role_names");
  if (num_flag_names==0) {
    freelog(LOG_FATAL, "Couldn't find units.role_names in %s", filename);
    exit(1);
  }
    
  for(i=0; i<U_LAST; i++) {
    u = &unit_types[i];
    u->roles = 0;
    sprintf(prefix, "units.u4%d", i);
    
    sval = secfile_lookup_str(file, "%s.name", prefix);
    if (strcmp(u->name, sval) != 0) {
      freelog(LOG_NORMAL, "for unit_type \"%s\": mismatch name \"%s\" in "
	   "block 4 (%s)", u->name, sval, filename);
    }
    slist = secfile_lookup_str_vec(file, &nval, "%s.roles", prefix );
    for(j=0; j<nval; j++) {
      sval = slist[j];
      if(strcmp(sval,"")==0) {
	continue;
      }
      ival = match_name_from_list(sval, flag_names, num_flag_names);
      if (ival==-1) {
	freelog(LOG_NORMAL, "for unit_type \"%s\": unmatched role name \"%s\" (%s)",
	     u->name, sval, filename);
      }
      u->roles |= (1<<ival);
    }
    free(slist);
  }
  free(flag_names);
}

/**************************************************************************
...  
**************************************************************************/
static void new_load_ruleset_units(char *filename, char *datafile_options,
				   struct section_file *file)
{
  struct unit_type *u;
  int max_hp, max_firepower;
  int i, j, ival, nval;
  char *sval, **slist, **sec;

  section_file_lookup(file,"datafile.description"); /* unused */

  max_hp =
    secfile_lookup_int_default(file, 0, "units_adjust.max_hitpoints");
  max_firepower =
    secfile_lookup_int_default(file, 0, "units_adjust.max_firepower");
  game.firepower_factor =
    secfile_lookup_int_default(file, 1, "units_adjust.firepower_factor");

  /* The names: */
  sec = secfile_get_secnames_prefix(file, "unit_", &nval);
  if(nval != U_LAST) {
    /* sometime this restriction should be removed */
    freelog(LOG_FATAL, "Bad number of units %d (%s)", nval, filename);
    exit(1);
  }
  for( i=0; i<U_LAST; i++ ) {
    u = &unit_types[i];
    strcpy(u->name, secfile_lookup_str(file, "%s.name", sec[i]));
  }

  /* Tech requirement is used to flag removed unit_types, which
     we might want to know for other fields.  After this we
     can use unit_type_exists()
  */
  for( i=0; i<U_LAST; i++ ) {
    u = &unit_types[i];
    u->tech_requirement = lookup_tech(file, sec[i], "tech_req",
				      0, filename, u->name);
  }
  for( i=0; i<U_LAST; i++ ) {
    u = &unit_types[i];
    if (unit_type_exists(i)) {
      u->obsoleted_by = lookup_unit_type(file, sec[i],
					 "obsolete_by", 0, filename, u->name);
    } else {
      section_file_lookup(file, "%s.obsolete_by", sec[i]);  /* unused */
      u->obsoleted_by = -1;
    }
  }

  /* main stats: */
  for( i=0; i<U_LAST; i++ ) {
    u = &unit_types[i];
    
    sval = secfile_lookup_str(file, "%s.move_type", sec[i]);
    ival = unit_move_type_from_str(sval);
    if (ival==0) {
      freelog(LOG_FATAL, "for unit_type \"%s\": bad move_type %s (%s)",
	   u->name, sval, filename);
      exit(1);
    }
    u->move_type = ival;
    u->graphics =
      secfile_lookup_int(file,"%s.graphic", sec[i]);
    u->build_cost =
      secfile_lookup_int(file,"%s.build_cost", sec[i]);
    u->attack_strength =
      secfile_lookup_int(file,"%s.attack", sec[i]);
    u->defense_strength =
      secfile_lookup_int(file,"%s.defense", sec[i]);
    u->move_rate =
      3*secfile_lookup_int(file,"%s.move_rate", sec[i]);
    
    u->vision_range =
      secfile_lookup_int(file,"%s.vision_range", sec[i]);
    u->transport_capacity =
      secfile_lookup_int(file,"%s.transport_cap", sec[i]);
    u->hp = secfile_lookup_int(file,"%s.hitpoints", sec[i]);
    if( max_hp && u->hp > max_hp ) {
      u->hp = max_hp;
    }
    u->firepower = secfile_lookup_int(file,"%s.firepower", sec[i]);
    if( max_firepower && u->firepower > max_firepower ) {
      u->firepower = max_firepower;
    }
    u->fuel = secfile_lookup_int(file,"%s.fuel", sec[i]);
  }
  
  /* flags */
  for(i=0; i<U_LAST; i++) {
    u = &unit_types[i];
    u->flags = 0;
    
    slist = secfile_lookup_str_vec(file, &nval, "%s.flags", sec[i]);
    for(j=0; j<nval; j++) {
      sval = slist[j];
      if(strcmp(sval,"")==0) {
	continue;
      }
      ival = unit_flag_from_str(sval);
      if (ival==F_LAST) {
	freelog(LOG_NORMAL, "for unit_type \"%s\": bad flag name \"%s\" (%s)",
	     u->name, sval, filename);
      }
      u->flags |= (1<<ival);
    }
    free(slist);
  }
    
  /* roles */
  for(i=0; i<U_LAST; i++) {
    u = &unit_types[i];
    u->roles = 0;
    
    slist = secfile_lookup_str_vec(file, &nval, "%s.roles", sec[i] );
    for(j=0; j<nval; j++) {
      sval = slist[j];
      if(strcmp(sval,"")==0) {
	continue;
      }
      ival = unit_role_from_str(sval);
      if (ival==L_LAST) {
	freelog(LOG_NORMAL, "for unit_type \"%s\": bad role name \"%s\" (%s)",
	     u->name, sval, filename);
      }
      u->roles |= (1<<ival);
    }
    free(slist);
  }
  free(sec);
}

/**************************************************************************
  This does some consistency checking, and pre-calcs, after loading
  units ruleset data.
**************************************************************************/
static void after_load_ruleset_units(char *filename)
{
  struct unit_type *u;
  int i, j;

  /* Some more consistency checking: */
  for( i=0; i<U_LAST; i++ ) {
    if (unit_type_exists(i)) {
      u = &unit_types[i];
      if (!tech_exists(u->tech_requirement)) {
	freelog(LOG_NORMAL, "unit_type \"%s\" depends on removed tech \"%s\" (%s)",
	     u->name, advances[u->tech_requirement].name, filename);
	u->tech_requirement = A_LAST;
      }
      if (u->obsoleted_by!=-1 && !unit_type_exists(u->obsoleted_by)) {
	freelog(LOG_NORMAL, "unit_type \"%s\" obsoleted by removed unit \"%s\" (%s)",
	     u->name, unit_types[u->obsoleted_by].name, filename);
	u->obsoleted_by = -1;
      }
    }
  }

  /* Setup roles and flags pre-calcs: */
  role_unit_precalcs();
     
  /* Check some required flags and roles etc: */
  if(num_role_units(F_SETTLERS)==0) {
    freelog(LOG_FATAL, "No flag=settler units? (%s)", filename);
    exit(1);
  }
  if(num_role_units(L_EXPLORER)==0) {
    freelog(LOG_FATAL, "No role=explorer units? (%s)", filename);
    exit(1);
  }
  if(num_role_units(L_FERRYBOAT)==0) {
    freelog(LOG_FATAL, "No role=ferryboat units? (%s)", filename);
    exit(1);
  }
  if(num_role_units(L_HUT)==0) {
    freelog(LOG_FATAL, "No role=hut units? (%s)", filename);
    exit(1);
  }
  if(num_role_units(L_FIRSTBUILD)==0) {
    freelog(LOG_FATAL, "No role=firstbuild units? (%s)", filename);
    exit(1);
  }
  u = &unit_types[get_role_unit(L_FIRSTBUILD,0)];
  if(u->tech_requirement!=A_NONE) {
    freelog(LOG_FATAL, "First role=firstbuild unit (%s) should have req None (%s)",
	 u->name, filename);
    exit(1);
  }

  /* pre-calculate game.rtech.nav (tech for non-trireme ferryboat) */
  game.rtech.nav = A_LAST;
  for(i=0; i<num_role_units(L_FERRYBOAT); i++) {
    j = get_role_unit(L_FERRYBOAT,i);
    if (!unit_flag(j, F_TRIREME)) {
      j = get_unit_type(j)->tech_requirement;
      freelog(LOG_DEBUG, "nav tech is %s", advances[j].name);
      game.rtech.nav = j;
      break;
    }
  }

}
  
/**************************************************************************
...  
**************************************************************************/
static void load_ruleset_buildings(char *ruleset_subdir)
{
  struct section_file file;
  char *filename, *datafile_options;
  char prefix[64];
  int i, j;
  struct improvement_type *b;

  filename = openload_ruleset_file(&file, ruleset_subdir, "buildings");
  datafile_options = check_ruleset_capabilities(&file, "1.7", filename);
  section_file_lookup(&file,"datafile.description"); /* unused */

  for( i=0, j=0; i<B_LAST; i++ ) {
    b = &improvement_types[i];
    sprintf(prefix, "buildings.b%d", j++);
    
    strcpy(b->name, secfile_lookup_str(&file, "%s.name", prefix));
    b->is_wonder = secfile_lookup_int(&file, "%s.is_wonder", prefix);
    b->tech_requirement = lookup_tech(&file, prefix, "tech_requirement",
				      0, filename, b->name);

    b->obsolete_by = lookup_tech(&file, prefix, "obsolete_by", 
				 0, filename, b->name);
    if (b->obsolete_by==A_LAST || !tech_exists(b->obsolete_by)) {
      b->obsolete_by = A_NONE;
    }

    b->build_cost = secfile_lookup_int(&file, "%s.build_cost", prefix);
    b->shield_upkeep = secfile_lookup_int(&file, "%s.shield_upkeep", prefix);
    b->variant = secfile_lookup_int(&file, "%s.variant", prefix);
  }

  /* Some more consistency checking: */
  for( i=0; i<B_LAST; i++ ) {
    b = &improvement_types[i];
    if (improvement_exists(i)) {
      if (!tech_exists(b->tech_requirement)) {
	freelog(LOG_NORMAL, "improvement \"%s\": depends on removed tech \"%s\" (%s)",
	     b->name, advances[b->tech_requirement].name, filename);
	b->tech_requirement = A_LAST;
      }
      if (!tech_exists(b->obsolete_by)) {
	freelog(LOG_NORMAL, "improvement \"%s\": obsoleted by removed tech \"%s\" (%s)",
	     b->name, advances[b->obsolete_by].name, filename);
	b->obsolete_by = A_NONE;
      }
    }
  }

  game.aqueduct_size = secfile_lookup_int(&file, "b_special.aqueduct_size");
  game.sewer_size = secfile_lookup_int(&file, "b_special.sewer_size");
  
  game.rtech.cathedral_plus =
    lookup_tech(&file, "b_special", "cathedral_plus", 0, filename, NULL);
  game.rtech.cathedral_minus =
    lookup_tech(&file, "b_special", "cathedral_minus", 0, filename, NULL);
  game.rtech.colosseum_plus =
    lookup_tech(&file, "b_special", "colosseum_plus", 0, filename, NULL);

  section_file_check_unused(&file, filename);
  section_file_free(&file);
}

/**************************************************************************
...  
**************************************************************************/
static void load_ruleset_terrain(char *ruleset_subdir)
{
  struct section_file file;
  char *filename, *datafile_options;
  char prefix[64];
  int i;
  struct tile_type *t;

  filename = openload_ruleset_file(&file, ruleset_subdir, "terrain");
  datafile_options = check_ruleset_capabilities(&file, "1.8.2", filename);
  section_file_lookup(&file,"datafile.description"); /* unused */

  /* options */

  terrain_control.river_style =
    secfile_lookup_int_default(&file, R_AS_SPECIAL, "options.river_style");

  terrain_control.may_road =
    secfile_lookup_int_default(&file, TRUE, "options.may_road");
  terrain_control.may_irrigate =
    secfile_lookup_int_default(&file, TRUE, "options.may_irrigate");
  terrain_control.may_mine =
    secfile_lookup_int_default(&file, TRUE, "options.may_mine");
  terrain_control.may_transform =
    secfile_lookup_int_default(&file, TRUE, "options.may_transform");

  /* graphics */

  terrain_control.border_base =
    secfile_lookup_int_default(&file, NO_SUCH_GRAPHIC, "graphics.border_base");
  terrain_control.corner_base =
    secfile_lookup_int_default(&file, NO_SUCH_GRAPHIC, "graphics.corner_base");
  terrain_control.river_base =
    secfile_lookup_int_default(&file, NO_SUCH_GRAPHIC, "graphics.river_base");
  terrain_control.outlet_base =
    secfile_lookup_int_default(&file, NO_SUCH_GRAPHIC, "graphics.outlet_base");
  terrain_control.denmark_base =
    secfile_lookup_int_default(&file, NO_SUCH_GRAPHIC, "graphics.denmark_base");

  /* terrain names */

  for (i = T_FIRST; i < T_COUNT; i++)
    {
      t = &(tile_types[i]);

      strcpy(t->terrain_name,
	     secfile_lookup_str(&file, "terrains.t%d.terrain_name", i));
      if (0 == strcmp(t->terrain_name, "unused")) *(t->terrain_name) = '\0';
    }

  /* terrain details */

  for (i = T_FIRST; i < T_COUNT; i++)
    {
      t = &(tile_types[i]);
      sprintf(prefix, "terrains.t%d", i);

      t->graphic_base = secfile_lookup_int(&file, "%s.graphic_base", prefix);
      t->graphic_count = secfile_lookup_int(&file, "%s.graphic_count", prefix);

      t->movement_cost = secfile_lookup_int(&file, "%s.movement_cost", prefix);
      t->defense_bonus = secfile_lookup_int(&file, "%s.defense_bonus", prefix);

      t->food = secfile_lookup_int(&file, "%s.food", prefix);
      t->shield = secfile_lookup_int(&file, "%s.shield", prefix);
      t->trade = secfile_lookup_int(&file, "%s.trade", prefix);

      strcpy(t->special_1_name, secfile_lookup_str(&file, "%s.special_1_name", prefix));
      if (0 == strcmp(t->special_1_name, "none")) *(t->special_1_name) = '\0';
      t->graphic_special_1 = secfile_lookup_int(&file, "%s.graphic_special_1", prefix);
      t->food_special_1 = secfile_lookup_int(&file, "%s.food_special_1", prefix);
      t->shield_special_1 = secfile_lookup_int(&file, "%s.shield_special_1", prefix);
      t->trade_special_1 = secfile_lookup_int(&file, "%s.trade_special_1", prefix);

      strcpy(t->special_2_name, secfile_lookup_str(&file, "%s.special_2_name", prefix));
      if (0 == strcmp(t->special_2_name, "none")) *(t->special_2_name) = '\0';
      t->graphic_special_2 = secfile_lookup_int(&file, "%s.graphic_special_2", prefix);
      t->food_special_2 = secfile_lookup_int(&file, "%s.food_special_2", prefix);
      t->shield_special_2 = secfile_lookup_int(&file, "%s.shield_special_2", prefix);
      t->trade_special_2 = secfile_lookup_int(&file, "%s.trade_special_2", prefix);

      t->road_trade_incr =
	secfile_lookup_int(&file, "%s.road_trade_incr", prefix);
      t->road_time = secfile_lookup_int(&file, "%s.road_time", prefix);

      t->irrigation_result =
	lookup_terrain(secfile_lookup_str(&file, "%s.irrigation_result", prefix), i);
      t->irrigation_food_incr =
	secfile_lookup_int(&file, "%s.irrigation_food_incr", prefix);
      t->irrigation_time = secfile_lookup_int(&file, "%s.irrigation_time", prefix);

      t->mining_result =
	lookup_terrain(secfile_lookup_str(&file, "%s.mining_result", prefix), i);
      t->mining_shield_incr =
	secfile_lookup_int(&file, "%s.mining_shield_incr", prefix);
      t->mining_time = secfile_lookup_int(&file, "%s.mining_time", prefix);

      t->transform_result =
	lookup_terrain(secfile_lookup_str(&file, "%s.transform_result", prefix), i);
      t->transform_time = secfile_lookup_int(&file, "%s.transform_time", prefix);
    }

  section_file_check_unused(&file, filename);
  section_file_free(&file);
}

/**************************************************************************
...
**************************************************************************/
void send_ruleset_units(struct player *dest)
{
  struct packet_ruleset_unit packet;
  struct unit_type *u;
  int to;

  for(u=unit_types; u<unit_types+U_LAST; u++) {
    packet.id = u-unit_types;
    strcpy(packet.name, u->name);
    packet.graphics = u->graphics;
    packet.move_type = u->move_type;
    packet.build_cost = u->build_cost;
    packet.attack_strength = u->attack_strength;
    packet.defense_strength = u->defense_strength;
    packet.move_rate = u->move_rate;
    packet.tech_requirement = u->tech_requirement;
    packet.vision_range = u->vision_range;
    packet.transport_capacity = u->transport_capacity;
    packet.hp = u->hp / game.firepower_factor;
    packet.firepower = u->firepower;
    packet.obsoleted_by = u->obsoleted_by;
    packet.fuel = u->fuel;
    packet.flags = u->flags;
    packet.roles = u->roles;

    for(to=0; to<game.nplayers; to++) {           /* dests */
      if(dest==0 || get_player(to)==dest) {
	send_packet_ruleset_unit(get_player(to)->conn, &packet);
      }
    }
  }
}

/**************************************************************************
...  
**************************************************************************/
void send_ruleset_techs(struct player *dest)
{
  struct packet_ruleset_tech packet;
  struct advance *a;
  int to;

  for(a=advances; a<advances+A_LAST; a++) {
    packet.id = a-advances;
    strcpy(packet.name, a->name);
    packet.req[0] = a->req[0];
    packet.req[1] = a->req[1];

    for(to=0; to<game.nplayers; to++) {           /* dests */
      if(dest==0 || get_player(to)==dest) {
	send_packet_ruleset_tech(get_player(to)->conn, &packet);
      }
    }
  }
}

/**************************************************************************
...  
**************************************************************************/
void send_ruleset_buildings(struct player *dest)
{
  struct packet_ruleset_building packet;
  struct improvement_type *b;
  int to;

  for(b=improvement_types; b<improvement_types+B_LAST; b++) {
    packet.id = b-improvement_types;
    strcpy(packet.name, b->name);
    packet.is_wonder = b->is_wonder;
    packet.tech_requirement = b->tech_requirement;
    packet.build_cost = b->build_cost;
    packet.shield_upkeep = b->shield_upkeep;
    packet.obsolete_by = b->obsolete_by;
    packet.variant = b->variant;

    for(to=0; to<game.nplayers; to++) {           /* dests */
      if(dest==0 || get_player(to)==dest) {
	send_packet_ruleset_building(get_player(to)->conn, &packet);
      }
    }
  }
}

/**************************************************************************
...  
**************************************************************************/
static void send_ruleset_terrain(struct player *dest)
{
  struct packet_ruleset_terrain packet;
  struct tile_type *t;
  int i, to;

  for (to = 0; to < game.nplayers; to++)      /* dests */
    {
      if (dest==0 || get_player(to)==dest)
	{
	  send_packet_ruleset_terrain_control(get_player(to)->conn, &terrain_control);
	}
    }

  for (i = T_FIRST; i < T_COUNT; i++)
    {
      t = &(tile_types[i]);

      packet.id = i;

      strcpy (packet.terrain_name, t->terrain_name);
      packet.graphic_base = t->graphic_base;
      packet.graphic_count = t->graphic_count;

      packet.movement_cost = t->movement_cost;
      packet.defense_bonus = t->defense_bonus;

      packet.food = t->food;
      packet.shield = t->shield;
      packet.trade = t->trade;

      strcpy (packet.special_1_name, t->special_1_name);
      packet.graphic_special_1 = t->graphic_special_1;
      packet.food_special_1 = t->food_special_1;
      packet.shield_special_1 = t->shield_special_1;
      packet.trade_special_1 = t->trade_special_1;

      strcpy (packet.special_2_name, t->special_2_name);
      packet.graphic_special_2 = t->graphic_special_2;
      packet.food_special_2 = t->food_special_2;
      packet.shield_special_2 = t->shield_special_2;
      packet.trade_special_2 = t->trade_special_2;

      packet.road_trade_incr = t->road_trade_incr;
      packet.road_time = t->road_time;

      packet.irrigation_result = t->irrigation_result;
      packet.irrigation_food_incr = t->irrigation_food_incr;
      packet.irrigation_time = t->irrigation_time;

      packet.mining_result = t->mining_result;
      packet.mining_shield_incr = t->mining_shield_incr;
      packet.mining_time = t->mining_time;

      packet.transform_result = t->transform_result;
      packet.transform_time = t->transform_time;

      for (to = 0; to < game.nplayers; to++)      /* dests */
	{
	  if (dest==0 || get_player(to)==dest)
	    {
	      send_packet_ruleset_terrain(get_player(to)->conn, &packet);
	    }
	}
    }
}

/**************************************************************************
...  
**************************************************************************/
void load_rulesets(void)
{
  freelog(LOG_NORMAL, "Loading rulesets");
  load_ruleset_techs(game.ruleset.techs);
  load_ruleset_units(game.ruleset.units);
  load_ruleset_buildings(game.ruleset.buildings);
  load_ruleset_terrain(game.ruleset.terrain);
}

/**************************************************************************
 dest can be NULL meaning all players
**************************************************************************/
void send_rulesets(struct player *dest)
{
  int to;
  
  for(to=0; to<game.nplayers; to++) {
    if(dest==0 || get_player(to)==dest) {
      connection_do_buffer(get_player(to)->conn);
    }
  }
  
  send_ruleset_techs(dest);
  send_ruleset_units(dest);
  send_ruleset_buildings(dest);
  send_ruleset_terrain(dest);
  
  for(to=0; to<game.nplayers; to++) {
    if(dest==0 || get_player(to)==dest) {
      connection_do_unbuffer(get_player(to)->conn);
    }
  }
}
