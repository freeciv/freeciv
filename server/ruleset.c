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

#include "game.h"
#include "registry.h"
#include "packets.h"
#include "log.h"
#include "tech.h"
#include "unit.h"
#include "city.h"
#include "ruleset.h"
#include "capability.h"

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

static void load_ruleset_techs(char *ruleset_subdir);
static void load_ruleset_units(char *ruleset_subdir);
static void load_ruleset_buildings(char *ruleset_subdir);

static void send_ruleset_techs(struct player *dest);
static void send_ruleset_units(struct player *dest);
static void send_ruleset_buildings(struct player *dest);

/**************************************************************************
  Do initial section_file_load on a ruleset file.
  "subdir" = "default", "civ1", "custom", ...
  "whichset" = "techs", "units", "buildings" (...)
  returns ptr to static memory with full filename including data directory. 
**************************************************************************/
static char *openload_ruleset_file(struct section_file *file,
				   char *subdir, char *whichset)
{
  char filename[512], *dfilename;

  sprintf(filename, "%s/%s.ruleset", subdir, whichset);
  dfilename = datafilename(filename);
  if(!section_file_load(file,dfilename)) {
    flog(LOG_FATAL, "Could not load ruleset file %s", dfilename);
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
    flog(LOG_FATAL, "Datafile capability problem for file %s", filename );
    flog(LOG_FATAL, "File capability: \"%s\", required: \"%s\"",
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
      flog((required?LOG_FATAL:LOG_NORMAL),
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
      flog((required?LOG_FATAL:LOG_NORMAL),
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
      flog(LOG_NORMAL, "for tech %s: \"Never\" with non-\"Never\" (%s)",
	   a->name, filename);
      a->req[0] = a->req[1] = A_LAST;
    }
    if (a->req[0]==A_NONE && a->req[1]!=A_NONE) {
      flog(LOG_NORMAL, "tech %s: should have \"None\" second (%s)",
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
	flog(LOG_FATAL, "tech \"%s\": req1 leads to removed tech \"%s\" (%s)",
	     a->name, advances[a->req[0]].name, filename);
	exit(1);
      } 
      if (!tech_exists(a->req[1])) {
	flog(LOG_FATAL, "tech \"%s\": req2 leads to removed tech \"%s\" (%s)",
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
  char prefix[64];
  struct unit_type *u;
  int max_hp, max_firepower;
  char **flag_names;
  int i, j, ival, nval, num_flag_names;
  char *sval, **slist;

  filename = openload_ruleset_file(&file, ruleset_subdir, "units");
  datafile_options = check_ruleset_capabilities(&file, "1.7", filename);
  section_file_lookup(&file,"datafile.description"); /* unused */

  max_hp =
    secfile_lookup_int_default(&file, 0, "unit_adjustments.max_hitpoints");
  max_firepower =
    secfile_lookup_int_default(&file, 0, "unit_adjustments.max_firepower");
  game.firepower_factor =
    secfile_lookup_int_default(&file, 1, "unit_adjustments.firepower_factor");

  /* The names: */
  for( i=0; i<U_LAST; i++ ) {
    u = &unit_types[i];
    strcpy(u->name, secfile_lookup_str(&file, "units.u1%d.name", i));
  }

  /* Tech requirement is used to flag removed unit_types, which
     we might want to know for other fields.  After this we
     can use unit_type_exists()
  */
  for( i=0; i<U_LAST; i++ ) {
    u = &unit_types[i];
    sprintf(prefix, "units.u1%d", i);
    u->tech_requirement = lookup_tech(&file, prefix, "tech_requirement",
				      0, filename, u->name);
  }
  for( i=0; i<U_LAST; i++ ) {
    u = &unit_types[i];
    sprintf(prefix, "units.u1%d", i);
    if (unit_type_exists(i)) {
      u->obsoleted_by = lookup_unit_type(&file, prefix, "obsoleted_by", 0,
					 filename, u->name);
    } else {
      section_file_lookup(&file, "%s.obsoleted_by", prefix);  /* unused */
      u->obsoleted_by = -1;
    }
  }
  
  /* second block: */
  for( i=0; i<U_LAST; i++ ) {
    u = &unit_types[i];
    sprintf(prefix, "units.u2%d", i);
    
    sval = secfile_lookup_str(&file, "%s.name", prefix);
    if (strcmp(u->name, sval) != 0) {
      flog(LOG_NORMAL, "for unit_type \"%s\": mismatch name \"%s\" in "
	   "block 2 (%s)", u->name, sval, filename);
    }

    u->move_type = ival = secfile_lookup_int(&file,"%s.move_type", prefix);
    if (ival<LAND_MOVING || ival>AIR_MOVING) {
      flog(LOG_FATAL, "for unit_type \"%s\": bad move_type %d (%s)",
	   u->name, ival, filename);
      exit(1);
    }
    u->graphics =
      secfile_lookup_int(&file,"%s.graphics", prefix);
    u->build_cost =
      secfile_lookup_int(&file,"%s.build_cost", prefix);
    u->attack_strength =
      secfile_lookup_int(&file,"%s.attack_strength", prefix);
    u->defense_strength =
      secfile_lookup_int(&file,"%s.defense_strength", prefix);
    u->move_rate =
      3*secfile_lookup_int(&file,"%s.move_rate", prefix);
    
    u->vision_range =
      secfile_lookup_int(&file,"%s.vision_range", prefix);
    u->transport_capacity =
      secfile_lookup_int(&file,"%s.transport_capacity", prefix);
    u->hp = secfile_lookup_int(&file,"%s.hitpoints", prefix);
    if( max_hp && u->hp > max_hp ) {
      u->hp = max_hp;
    }
    u->firepower = secfile_lookup_int(&file,"%s.firepower", prefix);
    if( max_firepower && u->firepower > max_firepower ) {
      u->firepower = max_firepower;
    }
    u->fuel = secfile_lookup_int(&file,"%s.fuel", prefix);
  }
  
  /* third block: flags */
  flag_names = secfile_lookup_str_vec(&file, &num_flag_names,
				      "units.flag_names");
  if (num_flag_names==0) {
    flog(LOG_FATAL, "Couldn't find units.flag_names in %s", filename);
    exit(1);
  }
    
  for(i=0; i<U_LAST; i++) {
    u = &unit_types[i];
    u->flags = 0;
    sprintf(prefix, "units.u3%d", i);
    
    sval = secfile_lookup_str(&file, "%s.name", prefix);
    if (strcmp(u->name, sval) != 0) {
      flog(LOG_NORMAL, "for unit_type \"%s\": mismatch name \"%s\" in "
	   "block 3 (%s)", u->name, sval, filename);
    }
    slist = secfile_lookup_str_vec(&file, &nval, "%s.flags", prefix );
    for(j=0; j<nval; j++) {
      sval = slist[j];
      if(strcmp(sval,"")==0) {
	continue;
      }
      ival = match_name_from_list(sval, flag_names, num_flag_names);
      if (ival==-1) {
	flog(LOG_NORMAL, "for unit_type \"%s\": unmatched flag name \"%s\" (%s)",
	     u->name, sval, filename);
      }
      u->flags |= (1<<ival);
    }
    free(slist);
  }
  free(flag_names);
    
  /* fourth block: roles */
  flag_names = secfile_lookup_str_vec(&file, &num_flag_names,
				      "units.role_names");
  if (num_flag_names==0) {
    flog(LOG_FATAL, "Couldn't find units.role_names in %s", filename);
    exit(1);
  }
    
  for(i=0; i<U_LAST; i++) {
    u = &unit_types[i];
    u->roles = 0;
    sprintf(prefix, "units.u4%d", i);
    
    sval = secfile_lookup_str(&file, "%s.name", prefix);
    if (strcmp(u->name, sval) != 0) {
      flog(LOG_NORMAL, "for unit_type \"%s\": mismatch name \"%s\" in "
	   "block 4 (%s)", u->name, sval, filename);
    }
    slist = secfile_lookup_str_vec(&file, &nval, "%s.roles", prefix );
    for(j=0; j<nval; j++) {
      sval = slist[j];
      if(strcmp(sval,"")==0) {
	continue;
      }
      ival = match_name_from_list(sval, flag_names, num_flag_names);
      if (ival==-1) {
	flog(LOG_NORMAL, "for unit_type \"%s\": unmatched role name \"%s\" (%s)",
	     u->name, sval, filename);
      }
      u->roles |= (1<<ival);
    }
    free(slist);
  }
  free(flag_names);
    
  /* Some more consistency checking: */
  for( i=0; i<U_LAST; i++ ) {
    if (unit_type_exists(i)) {
      u = &unit_types[i];
      if (!tech_exists(u->tech_requirement)) {
	flog(LOG_NORMAL, "unit_type \"%s\" depends on removed tech \"%s\" (%s)",
	     u->name, advances[u->tech_requirement].name, filename);
	u->tech_requirement = A_LAST;
      }
      if (u->obsoleted_by!=-1 && !unit_type_exists(u->obsoleted_by)) {
	flog(LOG_NORMAL, "unit_type \"%s\" obsoleted by removed unit \"%s\" (%s)",
	     u->name, unit_types[u->obsoleted_by].name, filename);
	u->obsoleted_by = -1;
      }
    }
  }

  /* Setup roles and flags pre-calcs: */
  role_unit_precalcs();
     
  /* Check some required flags and roles etc: */
  if(num_role_units(F_SETTLERS)==0) {
    flog(LOG_FATAL, "No flag=settler units? (%s)", filename);
    exit(1);
  }
  if(num_role_units(L_EXPLORER)==0) {
    flog(LOG_FATAL, "No role=explorer units? (%s)", filename);
    exit(1);
  }
  if(num_role_units(L_FERRYBOAT)==0) {
    flog(LOG_FATAL, "No role=ferryboat units? (%s)", filename);
    exit(1);
  }
  if(num_role_units(L_HUT)==0) {
    flog(LOG_FATAL, "No role=hut units? (%s)", filename);
    exit(1);
  }
  if(num_role_units(L_FIRSTBUILD)==0) {
    flog(LOG_FATAL, "No role=firstbuild units? (%s)", filename);
    exit(1);
  }
  u = &unit_types[get_role_unit(L_FIRSTBUILD,0)];
  if(u->tech_requirement!=A_NONE) {
    flog(LOG_FATAL, "First role=firstbuild unit (%s) should have req None (%s)",
	 u->name, filename);
    exit(1);
  }

  /* pre-calculate game.rtech.nav (tech for non-trireme ferryboat) */
  game.rtech.nav = A_LAST;
  for(i=0; i<num_role_units(L_FERRYBOAT); i++) {
    j = get_role_unit(L_FERRYBOAT,i);
    if (!unit_flag(j, F_TRIREME)) {
      j = get_unit_type(j)->tech_requirement;
      if(0) flog(LOG_DEBUG, "nav tech is %s", advances[j].name);
      game.rtech.nav = j;
      break;
    }
  }

  section_file_check_unused(&file, filename);
  section_file_free(&file);
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
	flog(LOG_NORMAL, "improvement \"%s\": depends on removed tech \"%s\" (%s)",
	     b->name, advances[b->tech_requirement].name, filename);
	b->tech_requirement = A_LAST;
      }
      if (!tech_exists(b->obsolete_by)) {
	flog(LOG_NORMAL, "improvement \"%s\": obsoleted by removed tech \"%s\" (%s)",
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
void load_rulesets(void)
{
  flog(LOG_NORMAL, "Loading rulesets");
  load_ruleset_techs(game.ruleset.techs);
  load_ruleset_units(game.ruleset.units);
  load_ruleset_buildings(game.ruleset.buildings);
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
  
  for(to=0; to<game.nplayers; to++) {
    if(dest==0 || get_player(to)==dest) {
      connection_do_unbuffer(get_player(to)->conn);
    }
  }
}
