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

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "capability.h"
#include "city.h"
#include "fcintl.h"
#include "game.h"
#include "government.h"
#include "log.h"
#include "map.h"
#include "mem.h"
#include "nation.h"
#include "packets.h"
#include "registry.h"
#include "support.h"
#include "tech.h"
#include "unit.h"

#include "citytools.h"

#include "ruleset.h"

static const char name_too_long[] = "Name \"%s\" too long; truncating.";
#define check_name(name) check_strlen(name, MAX_LEN_NAME, name_too_long)
#define name_strlcpy(dst, src) sz_loud_strlcpy(dst, src, name_too_long)

static void openload_ruleset_file(struct section_file *file,
				   char *subdir, char *whichset);
static char *check_ruleset_capabilities(struct section_file *file,
					char *required,	const char *filename);

static int lookup_tech(struct section_file *file, char *prefix,
		       char *entry, int required, const char *filename,
		       char *description);
static void lookup_tech_list(struct section_file *file, char *prefix,
			     char *entry, int *output, const char *filename);
static int lookup_unit_type(struct section_file *file, char *prefix,
			    char *entry, int required, const char *filename,
			    char *description);
static Impr_Type_id lookup_impr_type(struct section_file *file, char *prefix,
				     char *entry, int required,
				     const char *filename, char *description);
static int lookup_government(struct section_file *file, char *entry,
			     const char *filename);
static int lookup_city_cost(struct section_file *file, char *prefix,
			    char *entry, const char *filename);
static char *lookup_helptext(struct section_file *file, char *prefix);

static enum tile_terrain_type lookup_terrain(char *name, int tthis);

static void load_tech_names(struct section_file *file);
static void load_unit_names(struct section_file *file);
static void load_building_names(struct section_file *file);
static void load_government_names(struct section_file *file);
static void load_terrain_names(struct section_file *file);
static void load_citystyle_names(struct section_file *file);
static void load_nation_names(struct section_file *file);

static void load_ruleset_techs(struct section_file *file);
static void load_ruleset_units(struct section_file *file);
static void load_ruleset_buildings(struct section_file *file);
static void load_ruleset_governments(struct section_file *file);
static void load_ruleset_terrain(struct section_file *file);
static void load_ruleset_cities(struct section_file *file);
static void load_ruleset_nations(struct section_file *file);

static void load_ruleset_game(char *ruleset_subdir);

static void send_ruleset_techs(struct conn_list *dest);
static void send_ruleset_units(struct conn_list *dest);
static void send_ruleset_buildings(struct conn_list *dest);
static void send_ruleset_terrain(struct conn_list *dest);
static void send_ruleset_governments(struct conn_list *dest);
static void send_ruleset_nations(struct conn_list *dest);
static void send_ruleset_cities(struct conn_list *dest);
static void send_ruleset_game(struct conn_list *dest);

/**************************************************************************
  Do initial section_file_load on a ruleset file.
  "subdir" = "default", "civ1", "custom", ...
  "whichset" = "techs", "units", "buildings", "terrain", ...
  Calls exit(1) on failure.
  This no longer returns the full filename opened; used secfile_filename()
  if you want it.
**************************************************************************/
static void openload_ruleset_file(struct section_file *file,
				  char *subdir, char *whichset)
{
  char filename1[512], filename2[512], *dfilename;
  char sfilename[512];

  my_snprintf(filename1, sizeof(filename1), "%s_%s.ruleset", subdir, whichset);
  dfilename = datafilename(filename1);
  if (!dfilename) {
    my_snprintf(filename2, sizeof(filename2), "%s/%s.ruleset", subdir, whichset);
    dfilename = datafilename(filename2);
    if (!dfilename) {
      freelog(LOG_FATAL,
	      _("Could not find readable ruleset"
		" file \"%s\" or \"%s\" in data path."), filename1, filename2);
      freelog(LOG_FATAL, _("The data path may be set via"
			   " the environment variable FREECIV_PATH."));
      freelog(LOG_FATAL, _("Current data path is: \"%s\""), datafilename(NULL));
      exit(1);
    }
  }
  /* Need to save a copy of the filename for following message, since
     section_file_load() may call datafilename() for includes. */
  sz_strlcpy(sfilename, dfilename);
  
  if (!section_file_load(file,sfilename)) {
    freelog(LOG_FATAL, _("Could not load ruleset file \"%s\"."), sfilename);
    exit(1);
  }
}

/**************************************************************************
  Ruleset files should have a capabilities string datafile.options
  This gets and returns that string, and checks that the required
  capabilites specified are satisified.
**************************************************************************/
static char *check_ruleset_capabilities(struct section_file *file,
					char *us_capstr, const char *filename)
{
  char *datafile_options;
  
  datafile_options = secfile_lookup_str(file, "datafile.options");
  if (!has_capabilities(us_capstr, datafile_options)) {
    freelog(LOG_FATAL, _("Ruleset datafile appears incompatible:"));
    freelog(LOG_FATAL, _("file: \"%s\""), filename);
    freelog(LOG_FATAL, _("file options: %s"), datafile_options);
    freelog(LOG_FATAL, _("supported options: %s"), us_capstr);
    exit(1);
  }
  if (!has_capabilities(datafile_options, us_capstr)) {
    freelog(LOG_FATAL, _("Ruleset datafile claims required option(s)"
			 " which we don't support:"));
    freelog(LOG_FATAL, _("file: \"%s\""), filename);
    freelog(LOG_FATAL, _("file options: %s"), datafile_options);
    freelog(LOG_FATAL, _("supported options: %s"), us_capstr);
    exit(1);
  }
  return datafile_options;
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
		       char *entry, int required, const char *filename,
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
      freelog((required?LOG_FATAL:LOG_ERROR),
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
 Lookup a prefix.entry string vector in the file and fill in the
 array, which should hold MAX_NUM_TECH_LIST items.  Output array is
 A_LAST terminated, and all entries before that are guaranteed to
 tech_exist().  There should be at least one value, but it may be
 "", meaning empty list.
**************************************************************************/
static void lookup_tech_list(struct section_file *file, char *prefix,
			     char *entry, int *output, const char *filename)
{
  char **slist;
  int i, nval;

  /* pre-fill with A_LAST: */
  for(i=0; i<MAX_NUM_TECH_LIST; i++) {
    output[i] = A_LAST;
  }
  slist = secfile_lookup_str_vec(file, &nval, "%s.%s", prefix, entry);
  if (nval==0) {
    freelog(LOG_FATAL, "Missing string vector %s.%s (%s)",
	    prefix, entry, filename);
    exit(1);
  }
  if (nval>MAX_NUM_TECH_LIST) {
    freelog(LOG_FATAL, "String vector %s.%s too long (%d, max %d) (%s)",
	    prefix, entry, nval, MAX_NUM_TECH_LIST, filename);
    exit(1);
  }
  if (nval==1 && strcmp(slist[0], "")==0) {
    free(slist);
    return;
  }
  for (i=0; i<nval; i++) {
    char *sval = slist[i];
    int tech = find_tech_by_name(sval);
    if (tech==A_LAST) {
      freelog(LOG_FATAL, "For %s %s (%d) couldn't match tech \"%s\" (%s)",
	      prefix, entry, i, sval, filename);
      exit(1);
    }
    if (!tech_exists(tech)) {
      freelog(LOG_FATAL, "For %s %s (%d) tech \"%s\" is removed (%s)",
	      prefix, entry, i, sval, filename);
      exit(1);
    }
    output[i] = tech;
    freelog(LOG_DEBUG, "%s.%s,%d %s %d", prefix, entry, i, sval, tech);
  }
  free(slist);
  return;
}

/**************************************************************************
 Lookup a string prefix.entry in the file and return the corresponding
 unit_type id.  If (!required), return -1 if match "None" or can't match.
 If (required), die if can't match.
 If description is not NULL, it is used in the warning message
 instead of prefix (eg pass unit->name instead of prefix="units2.u27")
**************************************************************************/
static int lookup_unit_type(struct section_file *file, char *prefix,
			    char *entry, int required, const char *filename,
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
      freelog((required?LOG_FATAL:LOG_ERROR),
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
 Impr_Type_id.  If (!required), return B_LAST if match "None" or can't match.
 If (required), die if can't match.
 If description is not NULL, it is used in the warning message
 instead of prefix (eg pass impr->name instead of prefix="imprs2.b27")
**************************************************************************/
static Impr_Type_id lookup_impr_type(struct section_file *file, char *prefix,
				     char *entry, int required,
				     const char *filename, char *description)
{
  char *sval;
  int id;

  sval = secfile_lookup_str(file, "%s.%s", prefix, entry);
  if (!required && strcmp(sval, "None")==0) {
    id = B_LAST;
  } else {
    id = find_improvement_by_name(sval);
    if (id==B_LAST) {
      freelog((required?LOG_FATAL:LOG_ERROR),
	   "for %s %s couldn't match impr_type \"%s\" (%s)",
	   (description?description:prefix), entry, sval, filename);
      if (required) {
	exit(1);
      }
    }
  }

  return id;
}

/**************************************************************************
  Lookup entry in the file and return the corresponding government index;
  dies if can't find/match.  filename is for error message.
**************************************************************************/
static int lookup_government(struct section_file *file, char *entry,
			     const char *filename)
{
  char *sval;
  struct government *gov;
  
  sval = secfile_lookup_str(file, "%s", entry);
  gov = find_government_by_name(sval);
  if (gov==NULL) {
    freelog(LOG_FATAL, "for %s couldn't match government \"%s\" (%s)",
	    entry, sval, filename);
    exit(1);
  }
  return gov->index;
}

/**************************************************************************
  Lookup entry in the file and return the corresponding city cost:
  value if int, or G_CITY_SIZE_FREE is entry is "City_Size".
  Dies if gets some other string.  filename is for error message.
**************************************************************************/
static int lookup_city_cost(struct section_file *file, char *prefix,
			    char *entry, const char *filename)
{
  char *sval;
  int ival = 0;
  
  sval = secfile_lookup_str_int(file, &ival, "%s.%s", prefix, entry);
  if (sval) {
    if (mystrcasecmp(sval, "City_Size") == 0) {
      ival = G_CITY_SIZE_FREE;
    } else {
      freelog(LOG_FATAL, "Bad %s \"%s\" for %s (%s)",
	      entry, sval, prefix, filename);
      exit(1);
    }
  }
  return ival;
}

/**************************************************************************
  Lookup optional helptext, returning allocated memory or NULL.
**************************************************************************/
static char *lookup_helptext(struct section_file *file, char *prefix)
{
  char *sval;
  
  sval = secfile_lookup_str_default(file, NULL, "%s.helptext", prefix);
  if (sval) {
    sval = skip_leading_spaces(sval);
    if (strlen(sval)) {
      return mystrdup(sval);
    }
  }
  return NULL;
}

/**************************************************************************
  Look up a terrain name in the tile_types array and return its index.
**************************************************************************/
static enum tile_terrain_type lookup_terrain(char *name, int tthis)
{
  int i;

  if ((!(*name)) || (0 == strcmp(name, "none")) || (0 == strcmp(name, "no")))
    {
      return (T_LAST);
    }
  else if (0 == strcmp(name, "yes"))
    {
      return (tthis);
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
**************************************************************************/
static void load_tech_names(struct section_file *file)
{
  char **sec;
  struct advance *a;
  int num_techs; /* number of techs in the ruleset (means without A_NONE)*/
  int i;
  const char *filename = secfile_filename(file);

  section_file_lookup(file,"datafile.description"); /* unused */

  /* The names: */
  sec = secfile_get_secnames_prefix(file, "advance_", &num_techs);
  freelog(LOG_VERBOSE, "%d advances (including possibly unused)", num_techs);
  if(num_techs == 0) {
    freelog(LOG_FATAL, "No Advances?! (%s)", filename);
    exit(1);
  }

  if(num_techs + A_FIRST > A_LAST) {
    freelog(LOG_FATAL, "Too many advances (%d, max %d) (%s)",
	    num_techs, A_LAST-A_FIRST, filename);
    exit(1);
  }

  /* Initialize dummy tech A_NONE */
  sz_strlcpy(advances[A_NONE].name, "None");

  game.num_tech_types = num_techs + 1; /* includes A_NONE */

  a = &advances[A_FIRST];
  for (i = 0; i < num_techs; i++ ) {
    char *name = secfile_lookup_str(file, "%s.name", sec[i]);
    name_strlcpy(a->name, name);
    a++;
  }
  free(sec);
}

/**************************************************************************
  ...
**************************************************************************/
static void load_ruleset_techs(struct section_file *file)
{
  char **sec;
  struct advance *a;
  int num_techs; /* number of techs in the ruleset (means without A_NONE)*/
  int i;
  char *datafile_options;
  const char *filename = secfile_filename(file);
  
  datafile_options = check_ruleset_capabilities(file, "+1.9", filename);
  sec = secfile_get_secnames_prefix(file, "advance_", &num_techs);

  /* Initialize dummy tech A_NONE */
  advances[A_NONE].req[0] = A_NONE;
  advances[A_NONE].req[1] = A_NONE;
  advances[A_NONE].flags = 0;

  a = &advances[A_FIRST];
  
  for( i=0; i<num_techs; i++ ) {
    char *sval, **slist;
    int j,ival,nval;

    a->req[0] = lookup_tech(file, sec[i], "req1", 0, filename, a->name);
    a->req[1] = lookup_tech(file, sec[i], "req2", 0, filename, a->name);
    
    if ((a->req[0]==A_LAST && a->req[1]!=A_LAST) ||
	(a->req[0]!=A_LAST && a->req[1]==A_LAST)) {
      freelog(LOG_ERROR, "for tech %s: \"Never\" with non-\"Never\" (%s)",
	   a->name, filename);
      a->req[0] = a->req[1] = A_LAST;
    }
    if (a->req[0]==A_NONE && a->req[1]!=A_NONE) {
      freelog(LOG_ERROR, "tech %s: should have \"None\" second (%s)",
	   a->name, filename);
      a->req[0] = a->req[1];
      a->req[1] = A_NONE;
    }

    a->flags = 0;

    slist = secfile_lookup_str_vec(file, &nval, "%s.flags", sec[i]);
    for(j=0; j<nval; j++) {
      sval = slist[j];
      if(strcmp(sval,"")==0) {
        continue;
      }
      ival = tech_flag_from_str(sval);
      if (ival==TF_LAST) {
        freelog(LOG_ERROR, "for advance_type \"%s\": bad flag name \"%s\" (%s)",
                a->name, sval, filename);
      }
      a->flags |= (1<<ival);
    }
    free(slist);

    a->helptext = lookup_helptext(file, sec[i]);

    a++;
  }

  /* Some more consistency checking: 
     Non-removed techs depending on removed techs is too
     broken to fix by default, so die.
  */   
  for( i=A_FIRST; i<game.num_tech_types; i++ ) {
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

  /* Should be removed and use the flag directly in the future
     to allow more bonus techs -- sb */  
  game.rtech.get_bonus_tech = find_tech_by_flag(0,TF_BONUS_TECH);

  free(sec);
  section_file_check_unused(file, filename);
  section_file_free(file);
}

/**************************************************************************
  ...  
**************************************************************************/
static void load_unit_names(struct section_file *file)
{
  char **sec;
  int nval, i;
  const char *filename = secfile_filename(file);

  section_file_lookup(file,"datafile.description"); /* unused */

  /* The names: */
  sec = secfile_get_secnames_prefix(file, "unit_", &nval);
  freelog(LOG_VERBOSE, "%d unit types (including possibly unused)", nval);
  if(nval == 0) {
    freelog(LOG_FATAL, "No units?! (%s)", filename);
    exit(1);
  }
  if(nval > U_LAST) {
    freelog(LOG_FATAL, "Too many units (%d, max %d) (%s)",
	    nval, U_LAST, filename);
    exit(1);
  }
  game.num_unit_types = nval;
  for (i = 0; i < game.num_unit_types; i++ ) {
    char *name = secfile_lookup_str(file, "%s.name", sec[i]);
    name_strlcpy(unit_types[i].name, name);
  }
  free(sec);
}

/**************************************************************************
...  
**************************************************************************/
static void load_ruleset_units(struct section_file *file)
{
  char *datafile_options;
  struct unit_type *u;
  int i, j, ival, nval;
  int max_hp, max_firepower;
  char *sval, **slist, **sec;
  const char *filename = secfile_filename(file);

  datafile_options =
    check_ruleset_capabilities(file, "+1.9", filename);

  max_hp =
    secfile_lookup_int_default(file, 0, "units_adjust.max_hitpoints");
  max_firepower =
    secfile_lookup_int_default(file, 0, "units_adjust.max_firepower");
  game.firepower_factor =
    secfile_lookup_int_default(file, 1, "units_adjust.firepower_factor");

  sec = secfile_get_secnames_prefix(file, "unit_", &nval);

  /* Tech requirement is used to flag removed unit_types, which
     we might want to know for other fields.  After this we
     can use unit_type_exists()
  */
  for( i=0; i<game.num_unit_types; i++ ) {
    u = &unit_types[i];
    u->tech_requirement = lookup_tech(file, sec[i], "tech_req",
				      0, filename, u->name);
  }
  for( i=0; i<game.num_unit_types; i++ ) {
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
  for( i=0; i<game.num_unit_types; i++ ) {
    u = &unit_types[i];
    
    sval = secfile_lookup_str(file, "%s.move_type", sec[i]);
    ival = unit_move_type_from_str(sval);
    if (ival==0) {
      freelog(LOG_FATAL, "for unit_type \"%s\": bad move_type %s (%s)",
	   u->name, sval, filename);
      exit(1);
    }
    u->move_type = ival;
    
    sz_strlcpy(u->graphic_str,
	       secfile_lookup_str(file,"%s.graphic", sec[i]));
    sz_strlcpy(u->graphic_alt,
	       secfile_lookup_str(file,"%s.graphic_alt", sec[i]));
    
    u->build_cost =
      secfile_lookup_int(file,"%s.build_cost", sec[i]);
    u->attack_strength =
      secfile_lookup_int(file,"%s.attack", sec[i]);
    u->defense_strength =
      secfile_lookup_int(file,"%s.defense", sec[i]);
    u->move_rate =
      SINGLE_MOVE*secfile_lookup_int(file,"%s.move_rate", sec[i]);
    
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
    if (u->firepower <= 0) {
      freelog(LOG_FATAL, "for unit_type \"%s\": firepower is %d but "
	      "must be at least 1.\nSet the unit's attack strength to 0 "
	      "if you want it to not have any attack ability. (%s)",
	      u->name, u->firepower, filename);
      exit(1);
    }
    u->fuel = secfile_lookup_int(file,"%s.fuel", sec[i]);

    u->happy_cost  = secfile_lookup_int(file, "%s.uk_happy", sec[i]);
    u->shield_cost = secfile_lookup_int(file, "%s.uk_shield", sec[i]);
    u->food_cost   = secfile_lookup_int(file, "%s.uk_food", sec[i]);
    u->gold_cost   = secfile_lookup_int(file, "%s.uk_gold", sec[i]);

    u->helptext = lookup_helptext(file, sec[i]);
  }
  
  /* flags */
  for(i=0; i<game.num_unit_types; i++) {
    u = &unit_types[i];
    u->flags = 0;
    
    slist = secfile_lookup_str_vec(file, &nval, "%s.flags", sec[i]);
    for(j=0; j<nval; j++) {
      sval = slist[j];
      if(strcmp(sval,"")==0) {
	continue;
      }
      if (strcmp(sval, "Submarine")==0) {
	/* Backwards compatibility */
	freelog(LOG_NORMAL, "Old-style \"Submarine\" flag in %s (ok)", filename);
	u->flags |= (1<<F_NO_LAND_ATTACK);
	u->flags |= (1<<F_MISSILE_CARRIER);
	ival = F_PARTIAL_INVIS;
      } else {
	ival = unit_flag_from_str(sval);
      }
      if (ival==F_LAST) {
	freelog(LOG_ERROR, "for unit_type \"%s\": bad flag name \"%s\" (%s)",
	     u->name, sval, filename);
      }
      u->flags |= (1<<ival);

      if(ival == F_PARATROOPERS) {
        u->paratroopers_range = secfile_lookup_int(file,
            "%s.paratroopers_range", sec[i]);
        u->paratroopers_mr_req = 3*secfile_lookup_int(file,
            "%s.paratroopers_mr_req", sec[i]);
        u->paratroopers_mr_sub = 3*secfile_lookup_int(file,
            "%s.paratroopers_mr_sub", sec[i]);
      } else {
        u->paratroopers_range = 0;
        u->paratroopers_mr_req = 0;
        u->paratroopers_mr_sub = 0;
      }
    }
    free(slist);

    /* For now, if F_CITIES flag, we require the F_SETTLERS flag. -- jjm */
    if (unit_flag(i, F_CITIES) && !(unit_flag(i, F_SETTLERS))) {
      freelog(LOG_FATAL,
	      "for unit_type \"%s\": \"Cities\" flag without \"Settlers\" flag (%s)",
	      u->name, filename);
      exit(1);
    }
  }
    
  /* roles */
  for(i=0; i<game.num_unit_types; i++) {
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
	freelog(LOG_ERROR, "for unit_type \"%s\": bad role name \"%s\" (%s)",
	     u->name, sval, filename);
      }
      u->roles |= (1<<(ival-L_FIRST));
    }
    free(slist);
  }

  lookup_tech_list(file, "u_specials", "partisan_req",
		   game.rtech.partisan_req, filename);


  /* Some more consistency checking: */
  for( i=0; i<game.num_unit_types; i++ ) {
    if (unit_type_exists(i)) {
      u = &unit_types[i];
      if (!tech_exists(u->tech_requirement)) {
	freelog(LOG_ERROR,
		"unit_type \"%s\" depends on removed tech \"%s\" (%s)",
		u->name, advances[u->tech_requirement].name, filename);
	u->tech_requirement = A_LAST;
      }
      if (u->obsoleted_by!=-1 && !unit_type_exists(u->obsoleted_by)) {
	freelog(LOG_ERROR,
		"unit_type \"%s\" obsoleted by removed unit \"%s\" (%s)",
		u->name, unit_types[u->obsoleted_by].name, filename);
	u->obsoleted_by = -1;
      }
    }
  }

  /* Setup roles and flags pre-calcs: */
  role_unit_precalcs();
     
  /* Check some required flags and roles etc: */
  if(num_role_units(F_CITIES)==0) {
    freelog(LOG_FATAL, "No flag=cities units? (%s)", filename);
    exit(1);
  }
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
  if(num_role_units(L_BARBARIAN)==0) {
    freelog(LOG_FATAL, "No role=barbarian units? (%s)", filename);
    exit(1);
  }
  if(num_role_units(L_BARBARIAN_LEADER)==0) {
    freelog(LOG_FATAL, "No role=barbarian leader units? (%s)", filename);
    exit(1);
  }
  if(num_role_units(L_BARBARIAN_BUILD)==0) {
    freelog(LOG_FATAL, "No role=barbarian build units? (%s)", filename);
    exit(1);
  }
  if(num_role_units(L_BARBARIAN_BOAT)==0) {
    freelog(LOG_FATAL, "No role=barbarian ship units? (%s)", filename);
    exit(1);
  }
  if(num_role_units(L_BARBARIAN_SEA)==0) {
    freelog(LOG_FATAL, "No role=sea raider barbarian units? (%s)", filename);
    exit(1);
  }
  u = &unit_types[get_role_unit(L_BARBARIAN_BOAT,0)];
  if(u->move_type != SEA_MOVING) {
    freelog(LOG_FATAL, "Barbarian boat (%s) needs to be a sea unit (%s)",
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

  /* pre-calculate game.rtech.u_partisan
     (tech for first partisan unit, or A_LAST */
  if (num_role_units(L_PARTISAN)==0) {
    game.rtech.u_partisan = A_LAST;
  } else {
    j = get_role_unit(L_PARTISAN, 0);
    j = game.rtech.u_partisan = get_unit_type(j)->tech_requirement;
    freelog(LOG_DEBUG, "partisan tech is %s", advances[j].name);
  }
  
  free(sec);
  section_file_check_unused(file, filename);
  section_file_free(file);
}

/**************************************************************************
  ...  
**************************************************************************/
static void load_building_names(struct section_file *file)
{
  char **sec;
  int nval, i;
  const char *filename = secfile_filename(file);

  section_file_lookup(file,"datafile.description"); /* unused */

  /* The names: */
  sec = secfile_get_secnames_prefix(file, "building_", &nval);
  freelog(LOG_VERBOSE, "%d improvement types (including possibly unused)", nval);
  if (nval == 0) {
    freelog(LOG_FATAL, "No improvements?! (%s)", filename);
    exit(1);
  }
  if (nval > B_LAST) {
    freelog(LOG_FATAL, "Too many improvements (%d, max %d) (%s)",
	    nval, B_LAST, filename);
    exit(1);
  }
  /* FIXME: Remove this restriction when gen-impr implemented. */
  if (nval != B_LAST_ENUM) {
    freelog(LOG_FATAL, "Bad number of buildings %d (%s)", nval, filename);
    exit(1);
  }
  /* REMOVE TO HERE when gen-impr implemented. */
  game.num_impr_types = nval;

  for (i = 0; i < game.num_impr_types; i++) {
    char *name = secfile_lookup_str(file, "%s.name", sec[i]);
    name_strlcpy(improvement_types[i].name, name);
    improvement_types[i].name_orig[0] = 0;
  }
  free(sec);
}

/**************************************************************************
...  
**************************************************************************/
static void load_ruleset_buildings(struct section_file *file)
{
  char *datafile_options;
  char **sec, *item, **list;
  int i, j, k, nval, count, problem;
  struct impr_type *b;
  struct impr_effect *e;
  const char *filename = secfile_filename(file);

  datafile_options = check_ruleset_capabilities(file, "+1.10.1", filename);

  sec = secfile_get_secnames_prefix(file, "building_", &nval);

  for (i = 0; i < nval; i++) {
    b = &improvement_types[i];

    b->tech_req = lookup_tech(file, sec[i], "tech_req", 0, filename, b->name);

    b->bldg_req = lookup_impr_type(file, sec[i], "bldg_req", 0, filename, b->name);

    list = secfile_lookup_str_vec(file, &count, "%s.terr_gate", sec[i]);
    b->terr_gate = fc_malloc((count + 1) * sizeof(b->terr_gate[0]));
    k = 0;
    for (j = 0; j < count; j++) {
      b->terr_gate[k] = get_terrain_by_name(list[j]);
      if (b->terr_gate[k] >= T_UNKNOWN) {
	freelog(LOG_ERROR,
		"for %s terr_gate[%d] couldn't match terrain \"%s\" (%s)",
		b->name, j, list[j], filename);
      } else {
	k++;
      }
    }
    b->terr_gate[k] = T_LAST;
    free(list);

    list = secfile_lookup_str_vec(file, &count, "%s.spec_gate", sec[i]);
    b->spec_gate = fc_malloc((count + 1) * sizeof(b->spec_gate[0]));
    k = 0;
    for (j = 0; j < count; j++) {
      b->spec_gate[k] = get_special_by_name(list[j]);
      if (b->spec_gate[k] == S_NO_SPECIAL) {
	freelog(LOG_ERROR,
		"for %s spec_gate[%d] couldn't match special \"%s\" (%s)",
		b->name, j, list[j], filename);
      } else {
	k++;
      }
    }
    b->spec_gate[k] = S_NO_SPECIAL;
    free(list);

    item = secfile_lookup_str(file, "%s.equiv_range", sec[i]);
    b->equiv_range = effect_range_from_str(item);
    if (b->equiv_range == EFR_LAST) {
      freelog(LOG_ERROR,
	      "for %s equiv_range couldn't match range \"%s\" (%s)",
	      b->name, item, filename);
      b->equiv_range = EFR_NONE;
    }

    list = secfile_lookup_str_vec(file, &count, "%s.equiv_dupl", sec[i]);
    b->equiv_dupl = fc_malloc((count + 1) * sizeof(b->equiv_dupl[0]));
    k = 0;
    for (j = 0; j < count; j++) {
      b->equiv_dupl[k] = find_improvement_by_name(list[j]);
      if (b->equiv_dupl[k] == B_LAST) {
	freelog(LOG_ERROR,
		"for %s equiv_dupl[%d] couldn't match improvement \"%s\" (%s)",
		b->name, j, list[j], filename);
      } else {
	k++;
      }
    }
    b->equiv_dupl[k] = B_LAST;
    free(list);

    list = secfile_lookup_str_vec(file, &count, "%s.equiv_repl", sec[i]);
    b->equiv_repl = fc_malloc((count + 1) * sizeof(b->equiv_repl[0]));
    k = 0;
    for (j = 0; j < count; j++) {
      b->equiv_repl[k] = find_improvement_by_name(list[j]);
      if (b->equiv_repl[k] == B_LAST) {
	freelog(LOG_ERROR,
		"for %s equiv_repl[%d] couldn't match improvement \"%s\" (%s)",
		b->name, j, list[j], filename);
      } else {
	k++;
      }
    }
    b->equiv_repl[k] = B_LAST;
    free(list);

    b->obsolete_by = lookup_tech(file, sec[i], "obsolete_by",
				 0, filename, b->name);
    if ((b->obsolete_by == A_LAST) || !tech_exists(b->obsolete_by)) {
      b->obsolete_by = A_NONE;
    }

    b->is_wonder = secfile_lookup_int(file, "%s.is_wonder", sec[i]);

    b->build_cost = secfile_lookup_int(file, "%s.build_cost", sec[i]);

    b->upkeep = secfile_lookup_int(file, "%s.upkeep", sec[i]);

    b->sabotage = secfile_lookup_int(file, "%s.sabotage", sec[i]);

    for (count = 0;
	 secfile_lookup_str_default(file,
				    NULL,
				    "%s.effect%d.type",
				    sec[i], count);
	 count++) ;
    b->effect = fc_malloc((count + 1) * sizeof(b->effect[0]));
    k = 0;
    for (j = 0; j < count; j++) {
      e = &b->effect[k];
      problem = FALSE;

      item = secfile_lookup_str(file, "%s.effect%d.type", sec[i], j);
      e->type = effect_type_from_str(item);
      if (e->type == EFT_LAST) {
	freelog(LOG_ERROR,
		"for %s effect[%d].type couldn't match type \"%s\" (%s)",
		b->name, j, item, filename);
	problem = TRUE;
      }

      item =
	secfile_lookup_str_default(file, "None", "%s.effect%d.range", sec[i], j);
      e->range = effect_range_from_str(item);
      if (e->range == EFR_LAST) {
	freelog(LOG_ERROR,
		"for %s effect[%d].range couldn't match range \"%s\" (%s)",
		b->name, j, item, filename);
	problem = TRUE;
      }

      e->amount =
	secfile_lookup_int_default(file, 0, "%s.effect%d.amount", sec[i], j);

      e->survives =
	secfile_lookup_int_default(file, 0, "%s.effect%d.survives", sec[i], j);

      item =
	secfile_lookup_str_default(file, "", "%s.effect%d.cond_bldg", sec[i], j);
      if (*item) {
	e->cond_bldg = find_improvement_by_name(item);
	if (e->cond_bldg == B_LAST) {
	  freelog(LOG_ERROR,
		  "for %s effect[%d].cond_bldg couldn't match improvement \"%s\" (%s)",
		  b->name, j, item, filename);
	  problem = TRUE;
	}
      } else {
	e->cond_bldg = B_LAST;
      }

      item =
	secfile_lookup_str_default(file, "", "%s.effect%d.cond_gov", sec[i], j);
      if (*item) {
	struct government *g = find_government_by_name(item);
	if (!g) {
	  freelog(LOG_ERROR,
		  "for %s effect[%d].cond_gov couldn't match government \"%s\" (%s)",
		  b->name, j, item, filename);
	  e->cond_gov = game.government_count;
	  problem = TRUE;
	} else {
	  e->cond_gov = g->index;
	}
      } else {
	e->cond_gov = game.government_count;
      }

      item =
	secfile_lookup_str_default(file, "None", "%s.effect%d.cond_adv", sec[i], j);
      if (*item) {
	e->cond_adv = find_tech_by_name(item);
	if (e->cond_adv == A_LAST) {
	  freelog(LOG_ERROR,
		  "for %s effect[%d].cond_adv couldn't match tech \"%s\" (%s)",
		  b->name, j, item, filename);
	  problem = TRUE;
	}
      } else {
	e->cond_adv = A_NONE;
      }

      item =
	secfile_lookup_str_default(file, "", "%s.effect%d.cond_eff", sec[i], j);
      if (*item) {
	e->cond_eff = effect_type_from_str(item);
	if (e->cond_eff == EFT_LAST) {
	  freelog(LOG_ERROR,
		  "for %s effect[%d].cond_eff couldn't match effect \"%s\" (%s)",
		  b->name, j, item, filename);
	  problem = TRUE;
	}
      } else {
	e->cond_eff = EFT_LAST;
      }

      item =
	secfile_lookup_str_default(file, "", "%s.effect%d.aff_unit", sec[i], j);
      if (*item) {
	e->aff_unit = unit_class_from_str(item);
	if (e->aff_unit == UCL_LAST) {
	  freelog(LOG_ERROR,
		  "for %s effect[%d].aff_unit couldn't match class \"%s\" (%s)",
		  b->name, j, item, filename);
	  problem = TRUE;
	}
      } else {
	e->aff_unit = UCL_LAST;
      }

      item =
	secfile_lookup_str_default(file, "", "%s.effect%d.aff_terr", sec[i], j);
      if (*item) {
	if (0 == strcmp("None", item)) {
	  e->aff_terr = T_LAST;
	} else {
	  e->aff_terr = get_terrain_by_name(item);
	  if (e->aff_terr >= T_UNKNOWN) {
	    freelog(LOG_ERROR,
		    "for %s effect[%d].aff_terr couldn't match terrain \"%s\" (%s)",
		    b->name, j, item, filename);
	    e->aff_terr = T_LAST;
	    problem = TRUE;
	  }
	}
      } else {
	e->aff_terr = T_UNKNOWN;
      }

      item =
	secfile_lookup_str_default(file, "", "%s.effect%d.aff_spec", sec[i], j);
      if (*item) {
	if (0 == strcmp("None", item)) {
	  e->aff_spec = S_NO_SPECIAL;
	} else {
	  e->aff_spec = get_special_by_name(item);
	  if (e->aff_spec == S_NO_SPECIAL) {
	    freelog(LOG_ERROR,
		    "for %s effect[%d].aff_spec couldn't match special \"%s\" (%s)",
		    b->name, j, item, filename);
	    problem = TRUE;
	  }
	}
      } else {
	e->aff_spec = S_ALL;
      }

      if (!problem) {
	k++;
      }
    }
    b->effect[k].type = EFT_LAST;

    /* FIXME: remove when gen-impr obsoletes */
    b->variant = secfile_lookup_int_default(file, 0, "%s.variant", sec[i]);

    b->helptext = lookup_helptext(file, sec[i]);
  }

  /* Some more consistency checking: */
  for (i = 0; i < game.num_impr_types; i++) {
    b = &improvement_types[i];
    if (improvement_exists(i)) {
      if (!tech_exists(b->tech_req)) {
	freelog(LOG_ERROR,
		"improvement \"%s\": depends on removed tech \"%s\" (%s)",
		b->name, advances[b->tech_req].name, filename);
	b->tech_req = A_LAST;
      }
      if (!tech_exists(b->obsolete_by)) {
	freelog(LOG_ERROR,
		"improvement \"%s\": obsoleted by removed tech \"%s\" (%s)",
		b->name, advances[b->obsolete_by].name, filename);
	b->obsolete_by = A_NONE;
      }
      for (j = 0; b->effect[j].type != EFT_LAST; j++) {
	if (!tech_exists(b->effect[j].cond_adv)) {
	  freelog(LOG_ERROR,
		  "improvement \"%s\": effect conditional on"
		  " removed tech \"%s\" (%s)",
		  b->name, advances[b->effect[j].cond_adv].name, filename);
	  b->effect[j].cond_adv = A_LAST;
	}
      }
    }
  }

  /* FIXME: remove all of the following when gen-impr implemented... */

  game.aqueduct_size = secfile_lookup_int(file, "b_special.aqueduct_size");
  game.sewer_size = secfile_lookup_int(file, "b_special.sewer_size");
  
  game.rtech.cathedral_plus =
    lookup_tech(file, "b_special", "cathedral_plus", 0, filename, NULL);
  game.rtech.cathedral_minus =
    lookup_tech(file, "b_special", "cathedral_minus", 0, filename, NULL);
  game.rtech.colosseum_plus =
    lookup_tech(file, "b_special", "colosseum_plus", 0, filename, NULL);
  game.rtech.temple_plus =
    lookup_tech(file, "b_special", "temple_plus", 0, filename, NULL);

  free(sec);
  section_file_check_unused(file, filename);
  section_file_free(file);
}

/**************************************************************************
  ...  
**************************************************************************/
static void load_terrain_names(struct section_file *file)
{
  int nval, i;
  char **sec;
  const char *filename = secfile_filename(file);

  section_file_lookup(file,"datafile.description"); /* unused */

  /* terrain names */

  sec = secfile_get_secnames_prefix(file, "terrain_", &nval);
  if (nval != (T_COUNT - T_FIRST))
    {
      /* sometime this restriction should be removed */
      freelog(LOG_FATAL, "Bad number of terrains %d (%s)", nval, filename);
      exit(1);
    }

  for (i = T_FIRST; i < T_COUNT; i++) {
    char *name = secfile_lookup_str(file, "%s.terrain_name", sec[i]);
    name_strlcpy(tile_types[i].terrain_name, name);
    if (0 == strcmp(tile_types[i].terrain_name, "unused")) {
      tile_types[i].terrain_name[0] = 0;
    }
  }
  free(sec);
}

/**************************************************************************
...  
**************************************************************************/
static void load_ruleset_terrain(struct section_file *file)
{
  char *datafile_options;
  int nval;
  char **sec;
  int i, j;
  struct tile_type *t;
  const char *filename = secfile_filename(file);

  datafile_options =
    check_ruleset_capabilities(file, "+1.9", filename);

  /* options */

  terrain_control.river_style =
    secfile_lookup_int_default(file, R_AS_SPECIAL, "options.river_style");

  terrain_control.may_road =
    secfile_lookup_int_default(file, TRUE, "options.may_road");
  terrain_control.may_irrigate =
    secfile_lookup_int_default(file, TRUE, "options.may_irrigate");
  terrain_control.may_mine =
    secfile_lookup_int_default(file, TRUE, "options.may_mine");
  terrain_control.may_transform =
    secfile_lookup_int_default(file, TRUE, "options.may_transform");

  /* parameters */

  terrain_control.ocean_reclaim_requirement =
    secfile_lookup_int_default(file, 9, "parameters.ocean_reclaim_requirement");
  terrain_control.land_channel_requirement =
    secfile_lookup_int_default(file, 9, "parameters.land_channel_requirement");
  terrain_control.river_move_mode =
    secfile_lookup_int_default(file, RMV_FAST_STRICT, "parameters.river_move_mode");
  terrain_control.river_defense_bonus =
    secfile_lookup_int_default(file, 50, "parameters.river_defense_bonus");
  terrain_control.river_trade_incr =
    secfile_lookup_int_default(file, 1, "parameters.river_trade_incr");
  {
    char *s = secfile_lookup_str_default(file, NULL,
      "parameters.river_help_text");
    terrain_control.river_help_text = (s && *s) ? mystrdup(s) : NULL;
  }
  terrain_control.fortress_defense_bonus =
    secfile_lookup_int_default(file, 100, "parameters.fortress_defense_bonus");
  terrain_control.road_superhighway_trade_bonus =
    secfile_lookup_int_default(file, 50, "parameters.road_superhighway_trade_bonus");
  terrain_control.rail_food_bonus =
    secfile_lookup_int_default(file, 0, "parameters.rail_food_bonus");
  terrain_control.rail_shield_bonus =
    secfile_lookup_int_default(file, 50, "parameters.rail_shield_bonus");
  terrain_control.rail_trade_bonus =
    secfile_lookup_int_default(file, 0, "parameters.rail_trade_bonus");
  terrain_control.farmland_supermarket_food_bonus =
    secfile_lookup_int_default(file, 50, "parameters.farmland_supermarket_food_bonus");
  terrain_control.pollution_food_penalty =
    secfile_lookup_int_default(file, 50, "parameters.pollution_food_penalty");
  terrain_control.pollution_shield_penalty =
    secfile_lookup_int_default(file, 50, "parameters.pollution_shield_penalty");
  terrain_control.pollution_trade_penalty =
    secfile_lookup_int_default(file, 50, "parameters.pollution_trade_penalty");
  terrain_control.fallout_food_penalty =
    secfile_lookup_int_default(file, 50, "parameters.fallout_food_penalty");
  terrain_control.fallout_shield_penalty =
    secfile_lookup_int_default(file, 50, "parameters.fallout_shield_penalty");
  terrain_control.fallout_trade_penalty =
    secfile_lookup_int_default(file, 50, "parameters.fallout_trade_penalty");

  sec = secfile_get_secnames_prefix(file, "terrain_", &nval);

  /* terrain details */

  for (i = T_FIRST; i < T_COUNT; i++)
    {
      char *s1_name;
      char *s2_name;
      t = &(tile_types[i]);

      sz_strlcpy(t->graphic_str,
		 secfile_lookup_str(file,"%s.graphic", sec[i]));
      sz_strlcpy(t->graphic_alt,
		 secfile_lookup_str(file,"%s.graphic_alt", sec[i]));

      t->movement_cost = secfile_lookup_int(file, "%s.movement_cost", sec[i]);
      t->defense_bonus = secfile_lookup_int(file, "%s.defense_bonus", sec[i]);

      t->food = secfile_lookup_int(file, "%s.food", sec[i]);
      t->shield = secfile_lookup_int(file, "%s.shield", sec[i]);
      t->trade = secfile_lookup_int(file, "%s.trade", sec[i]);

      s1_name = secfile_lookup_str(file, "%s.special_1_name", sec[i]);
      name_strlcpy(t->special_1_name, s1_name);
      if (0 == strcmp(t->special_1_name, "none")) *(t->special_1_name) = '\0';
      t->food_special_1 = secfile_lookup_int(file, "%s.food_special_1", sec[i]);
      t->shield_special_1 = secfile_lookup_int(file, "%s.shield_special_1", sec[i]);
      t->trade_special_1 = secfile_lookup_int(file, "%s.trade_special_1", sec[i]);

      s2_name = secfile_lookup_str(file, "%s.special_2_name", sec[i]);
      name_strlcpy(t->special_2_name, s2_name);
      if (0 == strcmp(t->special_2_name, "none")) *(t->special_2_name) = '\0';
      t->food_special_2 = secfile_lookup_int(file, "%s.food_special_2", sec[i]);
      t->shield_special_2 = secfile_lookup_int(file, "%s.shield_special_2", sec[i]);
      t->trade_special_2 = secfile_lookup_int(file, "%s.trade_special_2", sec[i]);
      for(j=0; j<2; j++) {
	sz_strlcpy(t->special[j].graphic_str,
		   secfile_lookup_str(file,"%s.graphic_special_%d",
				      sec[i], j+1));
	sz_strlcpy(t->special[j].graphic_alt,
		   secfile_lookup_str(file,"%s.graphic_special_%da",
				      sec[i], j+1));
      }

      t->road_trade_incr =
	secfile_lookup_int(file, "%s.road_trade_incr", sec[i]);
      t->road_time = secfile_lookup_int(file, "%s.road_time", sec[i]);

      t->irrigation_result =
	lookup_terrain(secfile_lookup_str(file, "%s.irrigation_result", sec[i]), i);
      t->irrigation_food_incr =
	secfile_lookup_int(file, "%s.irrigation_food_incr", sec[i]);
      t->irrigation_time = secfile_lookup_int(file, "%s.irrigation_time", sec[i]);

      t->mining_result =
	lookup_terrain(secfile_lookup_str(file, "%s.mining_result", sec[i]), i);
      t->mining_shield_incr =
	secfile_lookup_int(file, "%s.mining_shield_incr", sec[i]);
      t->mining_time = secfile_lookup_int(file, "%s.mining_time", sec[i]);

      t->transform_result =
	lookup_terrain(secfile_lookup_str(file, "%s.transform_result", sec[i]), i);
      t->transform_time = secfile_lookup_int(file, "%s.transform_time", sec[i]);
      
      t->helptext = lookup_helptext(file, sec[i]);
    }

  free(sec);
  section_file_check_unused(file, filename);
  section_file_free(file);
}

/**************************************************************************
  ...  
**************************************************************************/
static void load_government_names(struct section_file *file)
{
  int nval, i;
  char **sec;
  const char *filename = secfile_filename(file);

  section_file_lookup(file,"datafile.description"); /* unused */

  sec = secfile_get_secnames_prefix(file, "government_", &nval);
  if (nval == 0) {
    freelog(LOG_FATAL, "No governments!? (%s)", filename);
    exit(1);
  } else if(nval > G_MAGIC) {
    /* upper limit is really about 255 for 8-bit id values, but
       use G_MAGIC elsewhere as a sanity check, and should be plenty
       big enough --dwp */
    freelog(LOG_FATAL, "Too many governments! (%d, max %d; %s)",
	    nval, G_MAGIC, filename);
    exit(1);
  }
  game.government_count = nval;
  governments = fc_calloc(game.government_count, sizeof(struct government));

  /* first fill in government names so find_government_by_name will work -SKi */
  for(i = 0; i < game.government_count; i++) {
    char *name = secfile_lookup_str(file, "%s.name", sec[i]);
    name_strlcpy(governments[i].name, name);
    governments[i].index = i;
  }
  free(sec);
}

/**************************************************************************
...  
**************************************************************************/
static void load_ruleset_governments(struct section_file *file)
{
  char *datafile_options;
  struct government *g = NULL;
  int i, j, nval;
  char *c;
  char **sec, **slist;
  const char *filename = secfile_filename(file);

  datafile_options =
    check_ruleset_capabilities(file, "+1.9", filename);

  sec = secfile_get_secnames_prefix(file, "government_", &nval);

  game.default_government
    = lookup_government(file, "governments.default", filename);
  
  game.government_when_anarchy
    = lookup_government(file, "governments.when_anarchy", filename);
  
  game.ai_goal_government
    = lookup_government(file, "governments.ai_goal", filename);

  freelog(LOG_DEBUG, "govs: def %d, anarchy %d, ai_goal %d",
	  game.default_government, game.government_when_anarchy,
	  game.ai_goal_government);
  
  /* Because player_init is called before rulesets are loaded we set
   * all players governments here, if they have not been previously
   * set (eg by loading game).
   */
  for(i=0; i<MAX_NUM_PLAYERS+MAX_NUM_BARBARIANS; i++) {
    if (game.players[i].government == G_MAGIC) {
      game.players[i].government = game.default_government;
    }
  }

  /* easy ones: */
  for(i=0; i<game.government_count; i++) {
    g = &governments[i];
    
    g->required_tech
      = lookup_tech(file, sec[i], "tech_req", 0, filename, g->name);
    
    sz_strlcpy(g->graphic_str,
	       secfile_lookup_str(file, "%s.graphic", sec[i]));
    sz_strlcpy(g->graphic_alt,
	       secfile_lookup_str(file, "%s.graphic_alt", sec[i]));
    
    g->martial_law_max = secfile_lookup_int(file, "%s.martial_law_max", sec[i]);
    g->martial_law_per = secfile_lookup_int(file, "%s.martial_law_per", sec[i]);
    g->max_rate = secfile_lookup_int(file, "%s.max_single_rate", sec[i]);
    g->civil_war = secfile_lookup_int(file, "%s.civil_war_chance", sec[i]);
    g->empire_size_mod = secfile_lookup_int(file, "%s.empire_size_mod", sec[i]);
    g->empire_size_inc =
      secfile_lookup_int_default(file, 0, "%s.empire_size_inc", sec[i]);
    g->rapture_size = secfile_lookup_int(file, "%s.rapture_size", sec[i]);
    
    g->free_happy
      = lookup_city_cost(file, sec[i], "unit_free_unhappy", filename);
    g->free_shield
      = lookup_city_cost(file, sec[i], "unit_free_shield", filename);
    g->free_food
      = lookup_city_cost(file, sec[i], "unit_free_food", filename);
    g->free_gold
      = lookup_city_cost(file, sec[i], "unit_free_gold", filename);

    g->unit_happy_cost_factor
      = secfile_lookup_int(file, "%s.unit_unhappy_factor", sec[i]);
    g->unit_shield_cost_factor
      = secfile_lookup_int(file, "%s.unit_shield_factor", sec[i]);
    g->unit_food_cost_factor
      = secfile_lookup_int(file, "%s.unit_food_factor", sec[i]);
    g->unit_gold_cost_factor
      = secfile_lookup_int(file, "%s.unit_gold_factor", sec[i]);

    g->corruption_level
      = secfile_lookup_int(file, "%s.corruption_level", sec[i]);
    g->corruption_modifier
      = secfile_lookup_int(file, "%s.corruption_modifier", sec[i]);
    g->fixed_corruption_distance
      = secfile_lookup_int(file, "%s.corruption_fixed_distance", sec[i]);
    g->corruption_distance_factor
      = secfile_lookup_int(file, "%s.corruption_distance_factor", sec[i]);
    g->extra_corruption_distance
      = secfile_lookup_int(file, "%s.corruption_extra_distance", sec[i]);

    g->trade_bonus
      = secfile_lookup_int(file, "%s.production_trade_bonus", sec[i]);
    g->shield_bonus
      = secfile_lookup_int(file, "%s.production_shield_bonus", sec[i]);
    g->food_bonus
      = secfile_lookup_int(file, "%s.production_food_bonus", sec[i]);

    g->celeb_trade_bonus
      = secfile_lookup_int(file, "%s.production_trade_bonus,1", sec[i]);
    g->celeb_shield_bonus
      = secfile_lookup_int(file, "%s.production_shield_bonus,1", sec[i]);
    g->celeb_food_bonus
      = secfile_lookup_int(file, "%s.production_food_bonus,1", sec[i]);

    g->trade_before_penalty
      = secfile_lookup_int(file, "%s.production_trade_penalty", sec[i]);
    g->shields_before_penalty
      = secfile_lookup_int(file, "%s.production_shield_penalty", sec[i]);
    g->food_before_penalty
      = secfile_lookup_int(file, "%s.production_food_penalty", sec[i]);

    g->celeb_trade_before_penalty
      = secfile_lookup_int(file, "%s.production_trade_penalty,1", sec[i]);
    g->celeb_shields_before_penalty
      = secfile_lookup_int(file, "%s.production_shield_penalty,1", sec[i]);
    g->celeb_food_before_penalty
      = secfile_lookup_int(file, "%s.production_food_penalty,1", sec[i]);
    
    g->helptext = lookup_helptext(file, sec[i]);
  }

  
  /* flags: */
  for(i=0; i<game.government_count; i++) {
    g = &governments[i];
    g->flags = 0;
    slist = secfile_lookup_str_vec(file, &nval, "%s.flags", sec[i]);
    for(j=0; j<nval; j++) {
      char *sval = slist[j];
      enum government_flag_id flag = government_flag_from_str(sval);
      if (strcmp(sval, "-") == 0) {
        continue;
      }
      if (flag == G_LAST_FLAG) {
        freelog(LOG_FATAL, "government %s has unknown flag %s", g->name, sval);
        exit(1);
      } else {
        g->flags |= (1<<flag);
      }
    }
    free(slist);
  }

  /* hints: */
  for(i=0; i<game.government_count; i++) {
    g = &governments[i];
    g->hints = 0;
    slist = secfile_lookup_str_vec(file, &nval, "%s.hints", sec[i]);
    for(j=0; j<nval; j++) {
      char *sval = slist[j];
      enum government_hint_id hint = government_hint_from_str(sval);
      if (strcmp(sval, "-") == 0) {
        continue;
      }
      if (hint == G_LAST_FLAG) {
        freelog(LOG_FATAL, "government %s has unknown hint %s", g->name, sval);
        exit(1);
      } else {
        g->hints |= (1<<hint);
      }
    }
    free(slist);
  }

  /* titles */
  for(i=0; i<game.government_count; i++) {
    struct ruler_title *title;

    g = &governments[i];

    g->num_ruler_titles = 1;
    g->ruler_titles = fc_calloc(1, sizeof(struct ruler_title));
    title = &(g->ruler_titles[0]);

    title->nation = DEFAULT_TITLE;
    sz_strlcpy(title->male_title,
	       secfile_lookup_str(file, "%s.ruler_male_title", sec[i]));
    sz_strlcpy(title->female_title,
	       secfile_lookup_str(file, "%s.ruler_female_title", sec[i]));
  }

  /* subgoals: */
  for(i=0; i<game.government_count; i++) {
    char *sval;
    g = &governments[i];
    sval = secfile_lookup_str(file, "%s.subgoal", sec[i]);
    if (strcmp(sval, "-")==0) {
      g->subgoal = -1;
    } else {
      struct government *subgov = find_government_by_name(sval);
      if (subgov==NULL) {
	freelog(LOG_ERROR, "Bad subgoal government \"%s\" for gov \"%s\" (%s)",
		sval, g->name, filename);
      } else {
	g->subgoal = subgov - governments;
      }
    }
    freelog(LOG_DEBUG, "%s subgoal %d", g->name, g->subgoal);
  }
    
  /* ai tech_hints: */
  j = -1;
  while((c = secfile_lookup_str_default(file, NULL,
					"governments.ai_tech_hints%d.tech",
					++j))) {
    struct ai_gov_tech_hint *hint = &ai_gov_tech_hints[j];

    if (j >= MAX_NUM_TECH_LIST) {
      freelog(LOG_FATAL, "Too many ai tech_hints in %s", filename);
      exit(1);
    }
    hint->tech = find_tech_by_name(c);
    if (hint->tech == A_LAST) {
      freelog(LOG_FATAL, "Could not match tech %s for gov ai_tech_hint %d (%s)",
	      c, j, filename);
      exit(1);
    }
    if (!tech_exists(hint->tech)) {
      freelog(LOG_FATAL, "For gov ai_tech_hint %d, tech \"%s\" is removed (%s)",
	      j, c, filename);
      exit(1);
    }
    hint->turns_factor =
      secfile_lookup_int(file, "governments.ai_tech_hints%d.turns_factor", j);
    hint->const_factor =
      secfile_lookup_int(file, "governments.ai_tech_hints%d.const_factor", j);
    hint->get_first =
      secfile_lookup_int(file, "governments.ai_tech_hints%d.get_first", j);
    hint->done =
      secfile_lookup_int(file, "governments.ai_tech_hints%d.done", j);
  }
  if (j<MAX_NUM_TECH_LIST) {
    ai_gov_tech_hints[j].tech = A_LAST;
  }

  free(sec);
  section_file_check_unused(file, filename);
  section_file_free(file);
}

/**************************************************************************
  Send information in packet_ruleset_control (numbers of units etc, and
  other miscellany) to specified connections.
**************************************************************************/
static void send_ruleset_control(struct conn_list *dest)
{
  struct packet_ruleset_control packet;
  int i;

  packet.aqueduct_size = game.aqueduct_size;
  packet.sewer_size = game.sewer_size;
  packet.add_to_size_limit = game.add_to_size_limit;

  packet.rtech.get_bonus_tech = game.rtech.get_bonus_tech;
  packet.rtech.cathedral_plus = game.rtech.cathedral_plus;
  packet.rtech.cathedral_minus = game.rtech.cathedral_minus;
  packet.rtech.colosseum_plus = game.rtech.colosseum_plus;
  packet.rtech.temple_plus = game.rtech.temple_plus;

  for(i=0; i<MAX_NUM_TECH_LIST; i++) {
    packet.rtech.partisan_req[i] = game.rtech.partisan_req[i];
  }

  packet.government_count = game.government_count;
  packet.government_when_anarchy = game.government_when_anarchy;
  packet.default_government = game.default_government;

  packet.num_unit_types = game.num_unit_types;
  packet.num_impr_types = game.num_impr_types;
  packet.num_tech_types = game.num_tech_types;

  packet.nation_count = game.nation_count;
  packet.playable_nation_count = game.playable_nation_count;
  packet.style_count = game.styles_count;

  lsend_packet_ruleset_control(dest, &packet);
}

/**************************************************************************
This checks if nations[pos] leader names are not already defined in any 
previous nation, or twice in its own leader name table.
If not return NULL, if yes return pointer to name which is repeated.
**************************************************************************/
static char *check_leader_names(int pos)
{
  int i, j, k;
  struct nation_type *n, *nation;
  char *leader;

  nation = get_nation_by_idx(pos);
  for( k = 0; k < nation->leader_count; k++) {
    leader = nation->leader_name[k];
    for( i=0; i<k; i++ ) {
      if( 0 == strcmp(leader, nation->leader_name[i]) )
          return leader;
    }
    for( j = 0; j < pos; j++) {
      n = get_nation_by_idx(j);
      for( i=0; i < n->leader_count; i++) {
        if( 0 == strcmp(leader, n->leader_name[i]) )
          return leader;
      }
    }
  }
  return NULL;
}

/**************************************************************************
  ...
**************************************************************************/
static void load_nation_names(struct section_file *file)
{
  char **sec;
  int i, j;

  section_file_lookup(file,"datafile.description"); /* unused */

  sec = secfile_get_secnames_prefix(file, "nation", &game.nation_count);
  game.playable_nation_count = game.nation_count - 1;
  freelog(LOG_VERBOSE, "There are %d nations defined", game.playable_nation_count);

  if (game.playable_nation_count < 0) {
    freelog(LOG_FATAL,
	    "There must be at least one nation defined; number is %d",
	    game.playable_nation_count);
    exit(1);
  } else if (game.playable_nation_count >= MAX_NUM_NATIONS) {
    freelog(LOG_ERROR,
	    "There are too many nations; only using %d of %d available.",
	    MAX_NUM_NATIONS - 1, game.playable_nation_count);
    game.playable_nation_count = MAX_NUM_NATIONS - 1;
  }
  alloc_nations(game.nation_count);

  for( i=0; i<game.nation_count; i++) {
    char *name        = secfile_lookup_str(file, "%s.name", sec[i]);
    char *name_plural = secfile_lookup_str(file, "%s.plural", sec[i]);
    struct nation_type *pl = get_nation_by_idx(i);

    name_strlcpy(pl->name, name);
    name_strlcpy(pl->name_plural, name_plural);

    /* Check if nation name is already defined. */
    for(j = 0; j < i; j++) {
      if (0 == strcmp(get_nation_name(j), pl->name)
	  || 0 == strcmp(get_nation_name_plural(j), pl->name_plural)) {
        freelog(LOG_FATAL,
		"Nation %s (the %s) defined twice; "
		"in section nation%d and section nation%d",
		pl->name, pl->name_plural, j, i);
        exit(1);
      }
    }
  }
  free(sec);
}

/**************************************************************************
Load nations.ruleset file
**************************************************************************/
static void load_ruleset_nations(struct section_file *file)
{
  char *datafile_options, *bad_leader, *g;
  struct nation_type *pl;
  struct government *gov;
  int *res, dim, val, i, j, nval;
  char temp_name[MAX_LEN_NAME];
  char **cities, **techs, **leaders, **sec;
  const char *filename = secfile_filename(file);

  datafile_options = check_ruleset_capabilities(file, "+1.9", filename);

  sec = secfile_get_secnames_prefix(file, "nation", &nval);

  for( i=0; i<game.nation_count; i++) {
    pl = get_nation_by_idx(i);

    /* nation leaders */

    leaders = secfile_lookup_str_vec(file, &dim, "%s.leader", sec[i]);
    if (dim > MAX_NUM_LEADERS) {
      freelog(LOG_ERROR, "Nation %s: too many leaders; only using %d of %d",
	      pl->name, MAX_NUM_LEADERS, dim);
      dim = MAX_NUM_LEADERS;
    } else if (dim < 1) {
      freelog(LOG_FATAL,
	      "Nation %s: number of leaders is %d; at least one is required.",
	      pl->name, dim);
      exit(1);
    }
    pl->leader_count = dim;
    for(j = 0; j < dim; j++) {
      pl->leader_name[j] = mystrdup(leaders[j]);
      if (check_name(leaders[j])) {
	pl->leader_name[j][MAX_LEN_NAME - 1] = 0;
      }
    }
    free(leaders);

    /* check if leader name is not already defined */
    if( (bad_leader=check_leader_names(i)) ) {
        freelog(LOG_FATAL, "Nation %s: leader %s defined more than once",
		pl->name, bad_leader);
        exit(1);
    }
    /* read leaders'sexes */
    leaders = secfile_lookup_str_vec(file, &dim, "%s.leader_sex", sec[i]);
    if (dim != pl->leader_count) {
      freelog(LOG_FATAL,
	      "Nation %s: the leader sex count (%d) "
	      "is not equal to the number of leaders (%d)",
              pl->name, dim, pl->leader_count);
      exit(1);
    }
    for (j = 0; j < dim; j++) {
      if (0 == mystrcasecmp(leaders[j], "Male")) {
        pl->leader_is_male[j] = 1;
      } else if (0 == mystrcasecmp(leaders[j], "Female")) {
        pl->leader_is_male[j] = 0;
      } else {
        freelog(LOG_ERROR,
		"Nation %s, leader %s: sex must be either Male or Female; "
		"assuming Male",
		pl->name, pl->leader_name[j]);
	pl->leader_is_male[j] = 1;
      }
    }
    free(leaders);

    /* Flags */

    sz_strlcpy(pl->flag_graphic_str,
	       secfile_lookup_str(file, "%s.flag", sec[i]));
    sz_strlcpy(pl->flag_graphic_alt,
	       secfile_lookup_str(file, "%s.flag_alt", sec[i]));

    /* Ruler titles */

    j = -1;
    while ((g = secfile_lookup_str_default(file, NULL,
					   "%s.ruler_titles%d.government",
					   sec[i], ++j))) {
      char *male_name;
      char *female_name;
      
      male_name = secfile_lookup_str(file, "%s.ruler_titles%d.male_title",
				     sec[i], j);
      female_name = secfile_lookup_str(file, "%s.ruler_titles%d.female_title",
				       sec[i], j);

      gov = find_government_by_name(g);
      if (gov != NULL) {
	check_name(male_name);
	check_name(female_name);
	/* Truncation is handled by set_ruler_title(). */
	set_ruler_title(gov, i, male_name, female_name);
      } else {
	/* LOG_VERBOSE rather than LOG_ERROR so that can use single nation
	   ruleset file with variety of government ruleset files: */
        freelog(LOG_VERBOSE,
		"Nation %s: government %s not found", pl->name, g);
      }
    }

    /* City styles */

    sz_strlcpy(temp_name,
	       secfile_lookup_str_default(file, "-", "%s.city_style", sec[i]));
    pl->city_style = get_style_by_name(temp_name);
    if (pl->city_style == -1) {
      freelog(LOG_NORMAL,
	      "Nation %s: city style %s is unknown, using default.", 
	      pl->name_plural, temp_name);
      pl->city_style = 0;
    }

    while (city_styles[pl->city_style].techreq != A_NONE) {
      if (pl->city_style == 0) {
	freelog(LOG_FATAL,
	       "Nation %s: the default city style is not available "
	       "from the beginning", pl->name);
	/* Note that we can't use temp_name here. */
	exit(1);
      }
      freelog(LOG_ERROR,
	      "Nation %s: city style %s is not available from beginning; "
	      "using default.", pl->name, temp_name);
      pl->city_style = 0;
    }

    /* AI stuff */

    pl->attack = secfile_lookup_int_default(file, 2, "%s.attack", sec[i]);
    pl->expand = secfile_lookup_int_default(file, 2, "%s.expand", sec[i]);
    pl->civilized = secfile_lookup_int_default(file, 2, "%s.civilized", sec[i]);

    res = secfile_lookup_int_vec(file, &dim, "%s.advisors", sec[i]);
    if (dim != ADV_LAST) {
      freelog(LOG_FATAL, "Nation %s: number of advisors must be %d but is %d", 
	      pl->name_plural, ADV_LAST, dim);
      exit(1);
    }
    for ( j=0; j<ADV_LAST; j++) 
      pl->advisors[j] = res[j];
    if(res) free(res);

    /* AI techs */

    techs = secfile_lookup_str_vec(file, &dim, "%s.tech_goals", sec[i]);
    if( dim > MAX_NUM_TECH_GOALS ) {
      freelog(LOG_VERBOSE,
	      "Only %d techs can be used from %d defined for nation %s",
	      MAX_NUM_TECH_GOALS, dim, pl->name_plural);
      dim = MAX_NUM_TECH_GOALS;
    }
    /* Below LOG_VERBOSE rather than LOG_ERROR so that can use single
       nation ruleset file with variety of tech ruleset files: */
    for( j=0; j<dim; j++) {
      val = find_tech_by_name(techs[j]);
      if(val == A_LAST) {
	freelog(LOG_VERBOSE, "Could not match tech goal \"%s\" for the %s",
		techs[j], pl->name_plural);
      } else if (!tech_exists(val)) {
	freelog(LOG_VERBOSE, "Goal tech \"%s\" for the %s doesn't exist",
		techs[j], pl->name_plural);
	val = A_LAST;
      }
      if(val != A_LAST && val != A_NONE) {
	freelog(LOG_DEBUG, "%s tech goal (%d) %3d %s", pl->name, j, val, techs[j]);
	pl->goals.tech[j] = val;
      }
    }
    freelog(LOG_DEBUG, "%s %d tech goals", pl->name, j);
    if(j==0) {
      freelog(LOG_VERBOSE, "No valid goal techs for %s", pl->name);
    }
    while( j < MAX_NUM_TECH_GOALS )
      pl->goals.tech[j++] = A_NONE;
    if (techs) free(techs);

    /* AI wonder & government */

    sz_strlcpy(temp_name, secfile_lookup_str(file, "%s.wonder", sec[i]));
    val = find_improvement_by_name(temp_name);
    /* Below LOG_VERBOSE rather than LOG_ERROR so that can use single
       nation ruleset file with variety of building ruleset files: */
    /* for any problems, leave as B_LAST */
    if(val == B_LAST) {
      freelog(LOG_VERBOSE, "Didn't match goal wonder \"%s\" for %s", temp_name, pl->name);
    } else if(!improvement_exists(val)) {
      freelog(LOG_VERBOSE, "Goal wonder \"%s\" for %s doesn't exist", temp_name, pl->name);
      val = B_LAST;
    } else if(!is_wonder(val)) {
      freelog(LOG_VERBOSE, "Goal wonder \"%s\" for %s not a wonder", temp_name, pl->name);
      val = B_LAST;
    }
    pl->goals.wonder = val;
    freelog(LOG_DEBUG, "%s wonder goal %d %s", pl->name, val, temp_name);

    sz_strlcpy(temp_name, secfile_lookup_str(file, "%s.government", sec[i]));
    gov = find_government_by_name(temp_name);
    if(gov == NULL) {
      /* LOG_VERBOSE rather than LOG_ERROR so that can use single nation
	 ruleset file with variety of government ruleset files: */
      freelog(LOG_VERBOSE, "Didn't match goal government name \"%s\" for %s",
	      temp_name, pl->name);
      val = game.government_when_anarchy;  /* flag value (no goal) (?) */
    } else {
      val = gov->index;
    }
    pl->goals.government = val;

    /* read city names */

    cities = secfile_lookup_str_vec(file, &dim, "%s.cities", sec[i]);
    pl->default_city_names = fc_calloc(dim+1, sizeof(char*));
    pl->default_city_names[dim] = NULL;
    for (j = 0; j < dim; j++) {
      pl->default_city_names[j] = mystrdup(cities[j]);
      if (check_name(cities[j])) {
	pl->default_city_names[j][MAX_LEN_NAME - 1] = 0;
      }
    }
    if(cities) free(cities);
  }

  /* read miscellaneous city names */

  cities = secfile_lookup_str_vec(file, &dim, "misc.cities");
  misc_city_names = fc_calloc(dim, sizeof(char*));

  for (j = 0; j < dim; j++) {
    misc_city_names[j] = mystrdup(cities[j]);
    if (check_name(cities[j])) {
      misc_city_names[j][MAX_LEN_NAME - 1] = 0;
    }
  }
  num_misc_city_names = dim;

  if (cities) free(cities);

  free(sec);
  section_file_check_unused(file, filename);
  section_file_free(file);
}

/**************************************************************************
  ...
**************************************************************************/
static void load_citystyle_names(struct section_file *file)
{
  char **styles;
  int nval, i;

  section_file_lookup(file,"datafile.description"); /* unused */

  /* The sections: */
  styles = secfile_get_secnames_prefix(file, "citystyle_", &nval);
  game.styles_count = nval;
  city_styles = fc_calloc( game.styles_count, sizeof(struct citystyle) );

  /* Get names, so can lookup for replacements: */
  for (i = 0; i < game.styles_count; i++) {
    char *style_name = secfile_lookup_str(file, "%s.name", styles[i]);
    name_strlcpy(city_styles[i].name, style_name);
  }
  free(styles);
}

/**************************************************************************
Load cities.ruleset file
**************************************************************************/
static void load_ruleset_cities(struct section_file *file)
{
  char *datafile_options;
  char **styles, *replacement;
  int i, nval;
  const char *filename = secfile_filename(file);

  datafile_options = check_ruleset_capabilities(file, "+1.9", filename);

  /* City Parameters */

  game.add_to_size_limit =
    secfile_lookup_int_default(file, 9, "parameters.add_to_size_limit");

  /* City Styles ... */

  styles = secfile_get_secnames_prefix(file, "citystyle_", &nval);

  /* Get rest: */
  for( i=0; i<game.styles_count; i++) {
    sz_strlcpy(city_styles[i].graphic, 
	       secfile_lookup_str(file, "%s.graphic", styles[i]));
    sz_strlcpy(city_styles[i].graphic_alt, 
	       secfile_lookup_str(file, "%s.graphic_alt", styles[i]));
    city_styles[i].techreq = lookup_tech(file, styles[i], "tech", 1,
                                         filename, city_styles[i].name);
    
    replacement = secfile_lookup_str(file, "%s.replaced_by", styles[i]);
    if( strcmp(replacement, "-") == 0) {
      city_styles[i].replaced_by = -1;
    } else {
      city_styles[i].replaced_by = get_style_by_name(replacement);
      if(city_styles[i].replaced_by == -1) {
        freelog(LOG_FATAL, "Style %s replacement %s not found",
                city_styles[i].name, replacement);
        exit(1);
      }
    }
  }
  free(styles);

  section_file_check_unused(file, filename);
  section_file_free(file);
}

/**************************************************************************
Load game.ruleset file
**************************************************************************/
static void load_ruleset_game(char *ruleset_subdir)
{
  struct section_file file;
  char *datafile_options;
  char *sval;
  const char *filename;

  openload_ruleset_file(&file, ruleset_subdir, "game");
  filename = secfile_filename(&file);
  datafile_options = check_ruleset_capabilities(&file, "+1.11.1", filename);
  section_file_lookup(&file,"datafile.description"); /* unused */

  game.rgame.min_city_center_food =
    secfile_lookup_int(&file, "civstyle.min_city_center_food");
  game.rgame.min_city_center_shield =
    secfile_lookup_int(&file, "civstyle.min_city_center_shield");
  game.rgame.min_city_center_trade =
    secfile_lookup_int(&file, "civstyle.min_city_center_trade");

  game.rgame.min_dist_bw_cities =
    secfile_lookup_int(&file, "civstyle.min_dist_bw_cities");
  if(game.rgame.min_dist_bw_cities<1) {
    freelog(LOG_ERROR, "Bad value %i for min_dist_bw_cities. Using 2.",
	    game.rgame.min_dist_bw_cities);
    game.rgame.min_dist_bw_cities = 2;
  }

  game.rgame.init_vis_radius_sq =
    secfile_lookup_int(&file, "civstyle.init_vis_radius_sq");

  sval = secfile_lookup_str(&file, "civstyle.hut_overflight" );
  if (mystrcasecmp(sval, "Nothing") == 0) {
    game.rgame.hut_overflight = OVERFLIGHT_NOTHING;
  } else if (mystrcasecmp(sval, "Frighten") == 0) {
    game.rgame.hut_overflight = OVERFLIGHT_FRIGHTEN;
  } else {
    freelog(LOG_ERROR, "Bad value %s for hut_overflight. Using "
            "\"Frighten\".", sval);
    game.rgame.hut_overflight = OVERFLIGHT_FRIGHTEN;
  }

  game.rgame.pillage_select =
    secfile_lookup_int(&file, "civstyle.pillage_select");

  sval = secfile_lookup_str(&file, "civstyle.nuke_contamination" );
  if (mystrcasecmp(sval, "Pollution") == 0) {
    game.rgame.nuke_contamination = CONTAMINATION_POLLUTION;
  } else if (mystrcasecmp(sval, "Fallout") == 0) {
    game.rgame.nuke_contamination = CONTAMINATION_FALLOUT;
  } else {
    freelog(LOG_ERROR, "Bad value %s for nuke_contamination. Using "
            "\"Pollution\".", sval);
    game.rgame.nuke_contamination = CONTAMINATION_POLLUTION;
  }

  game.rgame.granary_food_ini =
    secfile_lookup_int(&file, "civstyle.granary_food_ini");
  if (game.rgame.granary_food_ini < 0) {
    freelog(LOG_ERROR, "Bad value %i for granary_food_ini. Using 1.",
	    game.rgame.granary_food_ini);
    game.rgame.granary_food_ini = 1;
  }
  game.rgame.granary_food_inc =
    secfile_lookup_int(&file, "civstyle.granary_food_inc");
  if (game.rgame.granary_food_inc < 0 ||
      /* can't have a granary size of zero */
      (game.rgame.granary_food_ini == 0 && 
       game.rgame.granary_food_inc * game.foodbox / 100 == 0) ) {
    freelog(LOG_ERROR, "Bad value %i for granary_food_inc. Using 100.",
	    game.rgame.granary_food_inc);
    game.rgame.granary_food_inc = 100;
  }

  section_file_check_unused(&file, filename);
  section_file_free(&file);
}

/**************************************************************************
  Send the units ruleset information (all individual units) to the
  specified connections.
**************************************************************************/
static void send_ruleset_units(struct conn_list *dest)
{
  struct packet_ruleset_unit packet;
  struct unit_type *u;

  for(u=unit_types; u<unit_types+game.num_unit_types; u++) {
    packet.id = u-unit_types;
    sz_strlcpy(packet.name, u->name_orig);
    sz_strlcpy(packet.graphic_str, u->graphic_str);
    sz_strlcpy(packet.graphic_alt, u->graphic_alt);
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
    packet.happy_cost = u->happy_cost;
    packet.shield_cost = u->shield_cost;
    packet.food_cost = u->food_cost;
    packet.gold_cost = u->gold_cost;
    packet.paratroopers_range = u->paratroopers_range;
    packet.paratroopers_mr_req = u->paratroopers_mr_req;
    packet.paratroopers_mr_sub = u->paratroopers_mr_sub;
    packet.helptext = u->helptext;   /* pointer assignment */

    lsend_packet_ruleset_unit(dest, &packet);
  }
}

/**************************************************************************
  Send the techs ruleset information (all individual advances) to the
  specified connections.
**************************************************************************/
static void send_ruleset_techs(struct conn_list *dest)
{
  struct packet_ruleset_tech packet;
  struct advance *a;

  for(a=advances; a<advances+game.num_tech_types; a++) {
    packet.id = a-advances;
    sz_strlcpy(packet.name, a->name_orig);
    packet.req[0] = a->req[0];
    packet.req[1] = a->req[1];
    packet.flags = a->flags;
    packet.helptext = a->helptext;   /* pointer assignment */

    lsend_packet_ruleset_tech(dest, &packet);
  }
}

/**************************************************************************
  Send the buildings ruleset information (all individual improvements and
  wonders) to the specified connections.
**************************************************************************/
static void send_ruleset_buildings(struct conn_list *dest)
{
  struct packet_ruleset_building packet;
  struct impr_type *b;

  for(b=improvement_types; b<improvement_types+game.num_impr_types; b++) {
    packet.id = b-improvement_types;
    sz_strlcpy(packet.name, b->name_orig);
    packet.tech_req = b->tech_req;
    packet.bldg_req = b->bldg_req;
    packet.terr_gate = b->terr_gate;		/* pointer assignment */
    packet.spec_gate = b->spec_gate;		/* pointer assignment */
    packet.equiv_range = b->equiv_range;
    packet.equiv_dupl = b->equiv_dupl;		/* pointer assignment */
    packet.equiv_repl = b->equiv_repl;		/* pointer assignment */
    packet.obsolete_by = b->obsolete_by;
    packet.is_wonder = b->is_wonder;
    packet.build_cost = b->build_cost;
    packet.upkeep = b->upkeep;
    packet.sabotage = b->sabotage;
    packet.effect = b->effect;			/* pointer assignment */
    packet.variant = b->variant;	/* FIXME: remove when gen-impr obsoletes */
    packet.helptext = b->helptext;		/* pointer assignment */

    lsend_packet_ruleset_building(dest, &packet);
  }
}

/**************************************************************************
  Send the terrain ruleset information (terrain_control, and the individual
  terrain types) to the specified connections.
**************************************************************************/
static void send_ruleset_terrain(struct conn_list *dest)
{
  struct packet_ruleset_terrain packet;
  struct tile_type *t;
  int i, j;

  lsend_packet_ruleset_terrain_control(dest, &terrain_control);

  for (i = T_FIRST; i < T_COUNT; i++)
    {
      t = &(tile_types[i]);

      packet.id = i;

      sz_strlcpy(packet.terrain_name, t->terrain_name_orig);
      sz_strlcpy(packet.graphic_str, t->graphic_str);
      sz_strlcpy(packet.graphic_alt, t->graphic_alt);

      packet.movement_cost = t->movement_cost;
      packet.defense_bonus = t->defense_bonus;

      packet.food = t->food;
      packet.shield = t->shield;
      packet.trade = t->trade;

      sz_strlcpy(packet.special_1_name, t->special_1_name_orig);
      packet.food_special_1 = t->food_special_1;
      packet.shield_special_1 = t->shield_special_1;
      packet.trade_special_1 = t->trade_special_1;

      sz_strlcpy(packet.special_2_name, t->special_2_name_orig);
      packet.food_special_2 = t->food_special_2;
      packet.shield_special_2 = t->shield_special_2;
      packet.trade_special_2 = t->trade_special_2;

      for(j=0; j<2; j++) {
	sz_strlcpy(packet.special[j].graphic_str, t->special[j].graphic_str);
	sz_strlcpy(packet.special[j].graphic_alt, t->special[j].graphic_alt);
      }

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

      packet.helptext = t->helptext;   /* pointer assignment */
      
      lsend_packet_ruleset_terrain(dest, &packet);
    }
}

/**************************************************************************
  Send the government ruleset information to the specified connections.
  One packet per government type, and for each type one per ruler title.
**************************************************************************/
static void send_ruleset_governments(struct conn_list *dest)
{
  struct packet_ruleset_government gov;
  struct packet_ruleset_government_ruler_title title;
  struct ruler_title *p_title;
  struct government *g;
  int i, j;

  for (i = 0; i < game.government_count; ++i) {
    g = &governments[i];

    /* send one packet_government */
    gov.id                 = i;

    gov.required_tech    = g->required_tech;
    gov.max_rate         = g->max_rate;
    gov.civil_war        = g->civil_war;
    gov.martial_law_max  = g->martial_law_max;
    gov.martial_law_per  = g->martial_law_per;
    gov.empire_size_mod  = g->empire_size_mod;
    gov.empire_size_inc  = g->empire_size_inc;
    gov.rapture_size     = g->rapture_size;
    
    gov.unit_happy_cost_factor  = g->unit_happy_cost_factor;
    gov.unit_shield_cost_factor = g->unit_shield_cost_factor;
    gov.unit_food_cost_factor   = g->unit_food_cost_factor;
    gov.unit_gold_cost_factor   = g->unit_gold_cost_factor;
    
    gov.free_happy  = g->free_happy;
    gov.free_shield = g->free_shield;
    gov.free_food   = g->free_food;
    gov.free_gold   = g->free_gold;

    gov.trade_before_penalty = g->trade_before_penalty;
    gov.shields_before_penalty = g->shields_before_penalty;
    gov.food_before_penalty = g->food_before_penalty;

    gov.celeb_trade_before_penalty = g->celeb_trade_before_penalty;
    gov.celeb_shields_before_penalty = g->celeb_shields_before_penalty;
    gov.celeb_food_before_penalty = g->celeb_food_before_penalty;

    gov.trade_bonus = g->trade_bonus;
    gov.shield_bonus = g->shield_bonus;
    gov.food_bonus = g->food_bonus;

    gov.celeb_trade_bonus = g->celeb_trade_bonus;
    gov.celeb_shield_bonus = g->celeb_shield_bonus;
    gov.celeb_food_bonus = g->celeb_food_bonus;

    gov.corruption_level = g->corruption_level;
    gov.corruption_modifier = g->corruption_modifier;
    gov.fixed_corruption_distance = g->fixed_corruption_distance;
    gov.corruption_distance_factor = g->corruption_distance_factor;
    gov.extra_corruption_distance = g->extra_corruption_distance;
    
    gov.flags = g->flags;
    gov.hints = g->hints;
    gov.num_ruler_titles = g->num_ruler_titles;

    sz_strlcpy(gov.name, g->name_orig);
    sz_strlcpy(gov.graphic_str, g->graphic_str);
    sz_strlcpy(gov.graphic_alt, g->graphic_alt);
    
    gov.helptext = g->helptext;   /* pointer assignment */
      
    lsend_packet_ruleset_government(dest, &gov);
    
    /* send one packet_government_ruler_title per ruler title */
    for(j=0; j<g->num_ruler_titles; j++) {
      p_title = &g->ruler_titles[j];

      title.gov = i;
      title.id = j;
      title.nation = p_title->nation;
      sz_strlcpy(title.male_title, p_title->male_title);
      sz_strlcpy(title.female_title, p_title->female_title);
    
      lsend_packet_ruleset_government_ruler_title(dest, &title);
    }
  }
}

/**************************************************************************
  Send the nations ruleset information (info on each nation) to the
  specified connections.
**************************************************************************/
static void send_ruleset_nations(struct conn_list *dest)
{
  struct packet_ruleset_nation packet;
  struct nation_type *n;
  int i, k;

  for( k=0; k<game.nation_count; k++) {
    n = get_nation_by_idx(k);
    packet.id = k;
    sz_strlcpy(packet.name, n->name_orig);
    sz_strlcpy(packet.name_plural, n->name_plural_orig);
    sz_strlcpy(packet.graphic_str, n->flag_graphic_str);
    sz_strlcpy(packet.graphic_alt, n->flag_graphic_alt);
    packet.leader_count = n->leader_count;
    for(i=0; i < n->leader_count; i++) {
      sz_strlcpy(packet.leader_name[i], n->leader_name[i]);
      packet.leader_sex[i] = n->leader_is_male[i];
    }
    packet.city_style = n->city_style;

    lsend_packet_ruleset_nation(dest, &packet);
  }
}

/**************************************************************************
  Send the city-style ruleset information (each style) to the specified
  connections.
**************************************************************************/
static void send_ruleset_cities(struct conn_list *dest)
{
  struct packet_ruleset_city city_p;
  int k;

  for( k=0; k<game.styles_count; k++) {
    city_p.style_id = k;
    city_p.techreq = city_styles[k].techreq;
    city_p.replaced_by = city_styles[k].replaced_by;

    sz_strlcpy(city_p.name, city_styles[k].name_orig);
    sz_strlcpy(city_p.graphic, city_styles[k].graphic);
    sz_strlcpy(city_p.graphic_alt, city_styles[k].graphic_alt);

    lsend_packet_ruleset_city(dest, &city_p);
  }
}

/**************************************************************************
  Send information in packet_ruleset_game (miscellaneous rules) to the
  specified connections.
**************************************************************************/
static void send_ruleset_game(struct conn_list *dest)
{
  struct packet_ruleset_game misc_p;

  misc_p.min_city_center_food = game.rgame.min_city_center_food;
  misc_p.min_city_center_shield = game.rgame.min_city_center_shield;
  misc_p.min_city_center_trade = game.rgame.min_city_center_trade;
  misc_p.min_dist_bw_cities = game.rgame.min_dist_bw_cities;
  misc_p.init_vis_radius_sq = game.rgame.init_vis_radius_sq;
  misc_p.hut_overflight = game.rgame.hut_overflight;
  misc_p.pillage_select = game.rgame.pillage_select;
  misc_p.nuke_contamination = game.rgame.nuke_contamination;
  misc_p.granary_food_ini = game.rgame.granary_food_ini;
  misc_p.granary_food_inc = game.rgame.granary_food_inc;

  lsend_packet_ruleset_game(dest, &misc_p);
}

/**************************************************************************
...  
**************************************************************************/
void load_rulesets(void)
{
  struct section_file techfile, unitfile, buildfile, govfile, terrfile;
  struct section_file cityfile, nationfile;

  freelog(LOG_NORMAL, _("Loading rulesets"));
  load_ruleset_game(game.ruleset.game);

  openload_ruleset_file(&techfile, game.ruleset.techs, "techs");
  load_tech_names(&techfile);

  openload_ruleset_file(&buildfile, game.ruleset.buildings, "buildings");
  load_building_names(&buildfile);

  openload_ruleset_file(&govfile, game.ruleset.governments, "governments");
  load_government_names(&govfile);

  openload_ruleset_file(&unitfile, game.ruleset.units, "units");
  load_unit_names(&unitfile);

  openload_ruleset_file(&terrfile, game.ruleset.terrain, "terrain");
  load_terrain_names(&terrfile);

  openload_ruleset_file(&cityfile, game.ruleset.cities, "cities");
  load_citystyle_names(&cityfile);

  openload_ruleset_file(&nationfile, game.ruleset.nations, "nations");
  load_nation_names(&nationfile);

  load_ruleset_techs(&techfile);
  load_ruleset_cities(&cityfile);
  load_ruleset_governments(&govfile);
  load_ruleset_units(&unitfile);
  load_ruleset_terrain(&terrfile);
  load_ruleset_buildings(&buildfile);
  load_ruleset_nations(&nationfile);
  translate_data_names();
}

/**************************************************************************
  Send all ruleset information to the specified connections.
**************************************************************************/
void send_rulesets(struct conn_list *dest)
{
  conn_list_do_buffer(dest);

  send_ruleset_control(dest);
  send_ruleset_game(dest);
  send_ruleset_techs(dest);
  send_ruleset_governments(dest);
  send_ruleset_units(dest);
  send_ruleset_terrain(dest);
  send_ruleset_buildings(dest);
  send_ruleset_nations(dest);
  send_ruleset_cities(dest);

  conn_list_do_unbuffer(dest);
}
