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

/****************************************************************
 This module is for generic handling of help data, independent
 of gui considerations.
*****************************************************************/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <stdio.h>
#include <assert.h>

#include "city.h"
#include "fcintl.h"
#include "game.h"
#include "genlist.h"
#include "government.h"
#include "log.h"
#include "map.h"
#include "mem.h"
#include "registry.h"
#include "support.h"
#include "unit.h"

#include "helpdata.h"


static const char * const help_type_names[] = {
  "(Any)", "(Text)", "Units", "Improvements", "Wonders",
  "Techs", "Terrain", "Governments", 0
};


#define MAX_LAST (MAX(MAX(MAX(A_LAST,B_LAST),U_LAST),T_COUNT))


/* speclist? */
#define help_list_iterate(helplist, pitem) { \
  struct genlist_iterator myiter; \
  struct help_item *pitem; \
  for( genlist_iterator_init(&myiter, &helplist, 0); \
       (pitem=ITERATOR_PTR(myiter)); \
       ITERATOR_NEXT(myiter) ) {
#define help_list_iterate_end }}

static struct genlist help_nodes;
static struct genlist_iterator help_nodes_iterator;
static int help_nodes_init = 0;
/* helpnodes_init is not quite the same as booted in boot_help_texts();
   latter can be 0 even after call, eg if couldn't find helpdata.txt.
*/

char long_buffer[64000];

/****************************************************************
  Make sure help_nodes is initialised.
  Should call this just about everywhere which uses help_nodes,
  to be careful...  or at least where called by external
  (non-static) functions.
*****************************************************************/
static void check_help_nodes_init(void)
{
  if (!help_nodes_init) {
    genlist_init(&help_nodes);
    help_nodes_init = 1;    /* before help_iter_start to avoid recursion! */
    help_iter_start();
  }
}

/****************************************************************
  Free all allocations associated with help_nodes.
*****************************************************************/
static void free_help_nodes(void)
{
  check_help_nodes_init();
  help_list_iterate(help_nodes, ptmp) {
    free(ptmp->topic);
    free(ptmp->text);
    free(ptmp);
  }
  help_list_iterate_end;
  genlist_unlink_all(&help_nodes);
  help_iter_start();
}

/****************************************************************
...
 FIXME: check buffer length
*****************************************************************/
static void insert_generated_table(const char* name, char* outbuf)
{
  if (0 == strcmp (name, "TerrainAlterations"))
    {
      int i;
      strcat (outbuf, _("Terrain     Road   Irrigation     Mining         Transform\n"));
      strcat (outbuf, "---------------------------------------------------------------\n");
      for (i = T_FIRST; i < T_COUNT; i++)
	{
	  if (*(tile_types[i].terrain_name))
	    {
	      outbuf = strchr (outbuf, '\0');
	      sprintf(outbuf,
		 "%-10s %3d    %3d %-10s %3d %-10s %3d %-10s\n",
		 tile_types[i].terrain_name,
		 tile_types[i].road_time,
		 tile_types[i].irrigation_time,
		 ((tile_types[i].irrigation_result == i) ||
		  (tile_types[i].irrigation_result == T_LAST)) ? "" :
		  tile_types[tile_types[i].irrigation_result].terrain_name,
		 tile_types[i].mining_time,
		 ((tile_types[i].mining_result == i) ||
		  (tile_types[i].mining_result == T_LAST)) ? "" :
		  tile_types[tile_types[i].mining_result].terrain_name,
		 tile_types[i].transform_time,
		 ((tile_types[i].transform_result == i) ||
		  (tile_types[i].transform_result == T_LAST)) ? "" :
		  tile_types[tile_types[i].transform_result].terrain_name
                );
	    }
	}
      strcat (outbuf, "\n");
      strcat (outbuf, _("(Railroads and fortresses require 3 turns, regardless of terrain.)"));
    }
  return;
}

/****************************************************************
...
*****************************************************************/
static struct help_item *new_help_item(int type)
{
  struct help_item *pitem;
  
  pitem = fc_malloc(sizeof(struct help_item));
  pitem->topic = NULL;
  pitem->text = NULL;
  pitem->type = type;
  return pitem;
}

/****************************************************************
 for genlist_sort(); sort by topic via compare_strings()
 (sort topics with more leading spaces after those with fewer)
*****************************************************************/
static int help_item_compar(const void *a, const void *b)
{
  const struct help_item *ha, *hb;
  char *ta, *tb;
  ha = (const struct help_item*) *(const void**)a;
  hb = (const struct help_item*) *(const void**)b;
  for (ta = ha->topic, tb = hb->topic; *ta && *tb; ta++, tb++) {
    if (*ta != ' ') {
      if (*tb == ' ') return -1;
      break;
    } else if (*tb != ' ') {
      if (*ta == ' ') return 1;
      break;
    }
  }
  return compare_strings(ta, tb);
}

/****************************************************************
...
*****************************************************************/
void boot_help_texts(void)
{
  static int booted = 0;

  struct section_file file, *sf = &file;
  char *filename;
  struct help_item *pitem = NULL;
  int i, isec;
  char **sec, **paras;
  int nsec, npara;

  check_help_nodes_init();
  
  /* need to do something like this or bad things happen */
  popdown_help_dialog();
  
  if(!booted) {
    freelog(LOG_VERBOSE, "Booting help texts");
  } else {
    /* free memory allocated last time booted */
    free_help_nodes();
    freelog(LOG_VERBOSE, "Rebooting help texts");
  }    
  
  filename = datafilename("helpdata.txt");
  if (filename == NULL) {
    freelog(LOG_ERROR, "Could not find readable helpdata.txt in data path");
    freelog(LOG_ERROR, "The data path may be set via"
	                " the environment variable FREECIV_PATH");
    freelog(LOG_ERROR, "Current data path is: \"%s\"", datafilename(NULL));
    freelog(LOG_ERROR, "Did not read help texts");
    return;
  }
  /* after following call filename may be clobbered; use sf->filename instead */
  if (!section_file_load(sf, filename)) {
    /* this is now unlikely to happen */
    freelog(LOG_ERROR, "failed reading help-texts");
    return;
  }

  sec = secfile_get_secnames_prefix(sf, "help_", &nsec);
  
  for(isec=0; isec<nsec; isec++) {
    
    enum help_page_type current_type = HELP_ANY;
    char *gen_str =
      secfile_lookup_str_default(sf, NULL, "%s.generate", sec[isec]);
    
    if (gen_str) {
      if (!booted) {
	continue; /* on initial boot data tables are empty */
      }
      current_type = HELP_ANY;
      for(i=2; help_type_names[i]; i++) {
	if(strcmp(gen_str, help_type_names[i])==0) {
	  current_type = i;
	  break;
	}
      }
      if (current_type == HELP_ANY) {
	freelog(LOG_ERROR, "bad help-generate category \"%s\"", gen_str);
	continue;
      }
      {
	/* Note these should really fill in pitem->text from auto-gen
	   data instead of doing it later on the fly, but I don't want
	   to change that now.  --dwp
	*/
	char name[MAX_LEN_NAME+2];
	struct genlist category_nodes;
	
	genlist_init(&category_nodes);
	if(current_type==HELP_UNIT) {
	  for(i=0; i<game.num_unit_types; i++) {
	    if(unit_type_exists(i)) {
	      pitem = new_help_item(current_type);
	      my_snprintf(name, sizeof(name), " %s", unit_name(i));
	      pitem->topic = mystrdup(name);
	      pitem->text = mystrdup("");
	      genlist_insert(&category_nodes, pitem, -1);
	    }
	  }
	} else if(current_type==HELP_TECH) {
	  for(i=A_FIRST; i<game.num_tech_types; i++) {  /* skip A_NONE */
	    if(tech_exists(i)) {
	      pitem = new_help_item(current_type);
	      my_snprintf(name, sizeof(name), " %s", advances[i].name);
	      pitem->topic = mystrdup(name);
	      pitem->text = mystrdup("");
	      genlist_insert(&category_nodes, pitem, -1);
	    }
	  }
	} else if(current_type==HELP_TERRAIN) {
	  for(i=T_FIRST; i<T_COUNT; i++) {
	    if(*(tile_types[i].terrain_name)) {
	      pitem = new_help_item(current_type);
	      my_snprintf(name, sizeof(name), " %s", tile_types[i].terrain_name);
	      pitem->topic = mystrdup(name);
	      pitem->text = mystrdup("");
	      genlist_insert(&category_nodes, pitem, -1);
	    }
	  }
	  /* Add special Civ2-style river help text if it's supplied. */
	  if (terrain_control.river_help_text) {
	    pitem = new_help_item(HELP_TEXT);
	    pitem->topic = mystrdup(_("  Rivers"));
	    strcpy(long_buffer, _(terrain_control.river_help_text));
	    wordwrap_string(long_buffer, 68);
	    pitem->text = mystrdup(long_buffer);
	    genlist_insert(&category_nodes, pitem, -1);
	  }
	} else if(current_type==HELP_GOVERNMENT) {
	  for(i=0; i<game.government_count; i++) {
	      pitem = new_help_item(current_type);
	      my_snprintf(name, sizeof(name), " %s", get_government(i)->name);
	      pitem->topic = mystrdup(name);
	      pitem->text = mystrdup("");
	      genlist_insert(&category_nodes, pitem, -1);
	  }
	} else if(current_type==HELP_IMPROVEMENT) {
	  for(i=0; i<B_LAST; i++) {
	    if(improvement_exists(i) && !is_wonder(i)) {
	      pitem = new_help_item(current_type);
	      my_snprintf(name, sizeof(name), " %s", improvement_types[i].name);
	      pitem->topic = mystrdup(name);
	      pitem->text = mystrdup("");
	      genlist_insert(&category_nodes, pitem, -1);
	    }
	  }
	} else if(current_type==HELP_WONDER) {
	  for(i=0; i<B_LAST; i++) {
	    if(improvement_exists(i) && is_wonder(i)) {
	      pitem = new_help_item(current_type);
	      my_snprintf(name, sizeof(name), " %s", improvement_types[i].name);
	      pitem->topic = mystrdup(name);
	      pitem->text = mystrdup("");
	      genlist_insert(&category_nodes, pitem, -1);
	    }
	  }
	} else {
	  freelog(LOG_FATAL, "Bad current_type %d", current_type);
	  exit(1);
	}
	genlist_sort(&category_nodes, help_item_compar);
	help_list_iterate(category_nodes, ptmp) {
	  genlist_insert(&help_nodes, ptmp, -1);
	}
	help_list_iterate_end;
	genlist_unlink_all(&category_nodes);
	continue;
      }
    }
    
    /* It wasn't a "generate" node: */
    
    pitem = new_help_item(HELP_TEXT);
    pitem->topic = mystrdup(_(secfile_lookup_str(sf, "%s.name", sec[isec])));
    
    paras = secfile_lookup_str_vec(sf, &npara, "%s.text", sec[isec]);
    
    long_buffer[0] = '\0';
    for (i=0; i<npara; i++) {
      char *para = paras[i];
      if(strncmp(para, "$", 1)==0) {
	insert_generated_table(para+1, long_buffer+strlen(long_buffer));
      } else {
	strcat(long_buffer, _(para));
      }
      if (i!=npara-1) {
	strcat(long_buffer, "\n\n");
      }
    }
    free(paras);
    wordwrap_string(long_buffer, 68);
    pitem->text=mystrdup(long_buffer);
    genlist_insert(&help_nodes, pitem, -1);
  }

  free(sec);
  section_file_check_unused(sf, sf->filename);
  section_file_free(sf);
  booted = 1;
  freelog(LOG_VERBOSE, "Booted help texts ok");
}

/****************************************************************
  The following few functions are essentially wrappers for the
  help_nodes genlist.  This allows us to avoid exporting the
  genlist, and instead only access it through a controlled
  interface.
*****************************************************************/

/****************************************************************
  Number of help items.
*****************************************************************/
int num_help_items(void)
{
  check_help_nodes_init();
  return genlist_size(&help_nodes);
}

/****************************************************************
  Return pointer to given help_item.
  Returns NULL for 1 past end.
  Returns NULL and prints error message for other out-of bounds.
*****************************************************************/
const struct help_item *get_help_item(int pos)
{
  int size;
  
  check_help_nodes_init();
  size = genlist_size(&help_nodes);
  if (pos < 0 || pos > size) {
    freelog(LOG_ERROR, "Bad index %d to get_help_item (size %d)", pos, size);
    return NULL;
  }
  if (pos == size) {
    return NULL;
  }
  return genlist_get(&help_nodes, pos);
}

/****************************************************************
  Find help item by name and type.
  Returns help item, and sets (*pos) to position in list.
  If no item, returns pointer to static internal item with
  some faked data, and sets (*pos) to -1.
*****************************************************************/
const struct help_item*
get_help_item_spec(const char *name, enum help_page_type htype, int *pos)
{
  int idx;
  const struct help_item *pitem = NULL;
  static struct help_item vitem; /* v = virtual */
  static char vtopic[128];
  static char vtext[256];

  check_help_nodes_init();
  idx = 0;
  help_list_iterate(help_nodes, ptmp) {
    char *p=ptmp->topic;
    while(*p==' ')
      ++p;
    if(strcmp(name, p)==0 && (htype==HELP_ANY || htype==ptmp->type)) {
      pitem = ptmp;
      break;
    }
    ++idx;
  }
  help_list_iterate_end;
  
  if(!pitem) {
    idx = -1;
    vitem.topic = vtopic;
    sz_strlcpy(vtopic, name);
    vitem.text = vtext;
    if(htype==HELP_ANY || htype==HELP_TEXT) {
      my_snprintf(vtext, sizeof(vtext),
		  _("Sorry, no help topic for %s.\n"), vitem.topic);
      vitem.type = HELP_TEXT;
    } else {
      my_snprintf(vtext, sizeof(vtext),
		  _("Sorry, no help topic for %s.\n"
		    "This page was auto-generated.\n\n"),
		  vitem.topic);
      vitem.type = htype;
    }
    pitem = &vitem;
  }
  *pos = idx;
  return pitem;
}

/****************************************************************
  Start iterating through help items;
  that is, reset iterator to start position.
  (Could iterate using get_help_item(), but that would be
  less efficient due to scanning to find pos.)
*****************************************************************/
void help_iter_start(void)
{
  check_help_nodes_init();
  genlist_iterator_init(&help_nodes_iterator, &help_nodes, 0);
}

/****************************************************************
  Returns next help item; after help_iter_start(), this is
  the first item.  At end, returns NULL.
*****************************************************************/
const struct help_item *help_iter_next(void)
{
  const struct help_item *pitem;
  
  check_help_nodes_init();
  pitem = ITERATOR_PTR(help_nodes_iterator);
  ITERATOR_NEXT(help_nodes_iterator);
  return pitem;
}


/****************************************************************
  FIXME:
  All these helptext_* functions have a pretty crappy interface:
  we just write to buf and hope that its long enough.
  But I'm not going to fix it right now --dwp.
  
  Could also reduce amount/length of strlen's by inserting
  a few 'buf += strlen(buf)'.

  These functions should always ensure final buf is null-terminated.
  
  Also, in principle these could be auto-generated once, inserted
  into pitem->text, and then don't need to keep re-generating them.
  Only thing to be careful of would be changeable data, but don't
  have that here (for ruleset change or spacerace change must
  re-boot helptexts anyway).  Eg, genuinely dynamic information
  which could be useful would be if help system said which wonders
  have been built (or are being built and by who/where?)
*****************************************************************/

/****************************************************************
  Write text about cathedral techs: used by both
  Cathedrals proper and Mich's Chapel Wonder.
  Returns new end-of-string ptr.
*****************************************************************/
static void helptext_cathedral_techs(char *buf)
{
  int t;

  assert(buf);
  buf[0] = '\0';
  t=game.rtech.cathedral_minus;
  if(tech_exists(t)) {
    sprintf(buf, _("The discovery of %s will reduce this by 1.  "),
	    advances[t].name);
    buf += strlen(buf);
  }
  t=game.rtech.cathedral_plus;
  if(tech_exists(t)) {
    sprintf(buf, _("The discovery of %s will increase this by 1.  "),
	   advances[t].name);
    buf += strlen(buf);
  }
}

/****************************************************************
  Write misc dynamic text for improvements (not wonders).
  user_text is written after some extra, and before others.
*****************************************************************/
void helptext_improvement(char *buf, int which, const char *user_text)
{
  struct impr_type *imp = &improvement_types[which];
  
  assert(buf&&user_text);
  buf[0] = '\0';
  if(which==B_AQUEDUCT) {
    sprintf(buf+strlen(buf), _("Allows a city to grow larger than size %d.  "),
	    game.aqueduct_size);
    if(improvement_exists(B_SEWER)) {
      char *s = improvement_types[B_SEWER].name;
      sprintf(buf+strlen(buf),
	      _("(A%s %s is also required for a city to grow larger"
		" than size %d.)  "),
	      n_if_vowel(*s), s, game.sewer_size);
    }
    strcat(buf,"\n");
  }
  if(which==B_SEWER) {
    sprintf(buf+strlen(buf), _("Allows a city to grow larger than size %d.  "),
	   game.sewer_size);
  }
  if (imp->helptext) {
    sprintf(buf+strlen(buf), "%s  ", _(imp->helptext));
  }
  if(which==B_CATHEDRAL) {
    helptext_cathedral_techs(buf+strlen(buf));
  }
  if(which==B_COLOSSEUM) {
    int t=game.rtech.colosseum_plus;
    if(tech_exists(t)) {
      int n = strlen(buf);
      if(n && buf[n-1] == '\n') buf[n-1] = ' ';
      sprintf(buf+n, _("The discovery of %s will increase this by 1.  "),
	     advances[t].name);
    }
  }
  if(which==B_BARRACKS
     && tech_exists(improvement_types[B_BARRACKS].obsolete_by)
     && tech_exists(improvement_types[B_BARRACKS2].obsolete_by)) {
    sprintf(buf+strlen(buf),
	   _("\n\nNote that discovering %s or %s will obsolete"
	   " any existing %s.  "),
	   advances[improvement_types[B_BARRACKS].obsolete_by].name,
	   advances[improvement_types[B_BARRACKS2].obsolete_by].name,
	   improvement_types[B_BARRACKS].name);
  }
  if(which==B_BARRACKS2
     && tech_exists(improvement_types[B_BARRACKS2].obsolete_by)) {
    sprintf(buf+strlen(buf),
	   _("\n\nThe discovery of %s will make %s obsolete.  "),
	   advances[improvement_types[B_BARRACKS2].obsolete_by].name,
	   improvement_types[B_BARRACKS2].name);
  }
  if (strcmp(user_text, "")!=0) {
    sprintf(buf+strlen(buf), "\n\n%s", user_text);
  }
  wordwrap_string(buf, 68);
}

/****************************************************************
  Append misc dynamic text for wonders.
*****************************************************************/
void helptext_wonder(char *buf, int which,
			    const char *user_text)
{
  struct impr_type *imp = &improvement_types[which];
  
  assert(buf&&user_text);
  buf[0] = '\0';
  if(which==B_MANHATTEN && num_role_units(F_NUCLEAR)>0) {
    int u, t;
    u = get_role_unit(F_NUCLEAR, 0);
    assert(u<game.num_unit_types);
    t = get_unit_type(u)->tech_requirement;
    assert(t<game.num_tech_types);
    sprintf(buf+strlen(buf),
	   _("Allows all players with knowledge of %s to build %s units.  "),
	   advances[t].name, get_unit_type(u)->name);
  }
  if (imp->helptext) {
    sprintf(buf+strlen(buf), "%s  ", _(imp->helptext));
  }
  if(which==B_MICHELANGELO) {
    if (improvement_variant(B_MICHELANGELO)==0)
      helptext_cathedral_techs(buf+strlen(buf));
  }
  if (strcmp(user_text, "")!=0) {
    sprintf(buf+strlen(buf), "\n\n%s", user_text);
  }
  wordwrap_string(buf, 68);
}

/****************************************************************
  Append misc dynamic text for units.
  Transport capacity, unit flags, fuel.
*****************************************************************/
void helptext_unit(char *buf, int i, const char *user_text)
{
  struct unit_type *utype;

  assert(buf&&user_text);
  if (!unit_type_exists(i)) {
    strcpy(buf, user_text);
    return;
  }
  utype = get_unit_type(i);
  
  buf[0] = '\0';
  if (utype->transport_capacity>0) {
    if (unit_flag(i, F_CARRIER)) {
      sprintf(buf+strlen(buf), _("* Can carry and refuel %d air units.\n"),
	      utype->transport_capacity);
    } else if (unit_flag(i, F_MISSILE_CARRIER)) {
      sprintf(buf+strlen(buf), _("* Can carry and refuel %d missile units.\n"),
	      utype->transport_capacity);
    } else {
      sprintf(buf+strlen(buf), _("* Can carry %d ground units across water.\n"),
	      utype->transport_capacity);
    }
  }
  if (unit_flag(i, F_CARAVAN)) {
    sprintf(buf+strlen(buf),
	    _("* Can establish trade routes and help build wonders.\n"));
  }
  if (unit_flag(i, F_CITIES)) {
    sprintf(buf+strlen(buf), _("* Can build new cities.\n"));
  }
  if (unit_flag(i, F_SETTLERS)) {
    sprintf(buf+strlen(buf), _("* Can perform settler actions.\n"));
  }
  if (unit_flag(i, F_DIPLOMAT)) {
    if (unit_flag(i, F_SPY)) 
      sprintf(buf+strlen(buf), _("* Can perform diplomatic actions,"
				 " plus special spy abilities.\n"));
    else 
      sprintf(buf+strlen(buf), _("* Can perform diplomatic actions.\n"));
  }
  if (unit_flag(i, F_FIGHTER)) {
    sprintf(buf+strlen(buf), _("* Can attack enemy air units.\n"));
  }
  if (unit_flag(i, F_PARTIAL_INVIS)) {
    sprintf(buf+strlen(buf), _("* Is invisible except when next to an"
			       " enemy unit or city.\n"));
  }
  if (unit_flag(i, F_NO_LAND_ATTACK)) {
    sprintf(buf+strlen(buf), _("* Can only attack units on ocean squares"
			       " (no land attacks).\n"));
  }
  if (unit_flag(i, F_MARINES)) {
    sprintf(buf+strlen(buf), _("* Can attack from aboard sea units: against"
			       " enemy cities and onto land squares."));
  }
  if (unit_flag(i, F_PARATROOPERS)) {
    sprintf(buf+strlen(buf), _("* Can be paradropped from a friendly city"
			       " (Range: %d)."), utype->paratroopers_range);
  }
  if (unit_flag(i, F_PIKEMEN)) {
    sprintf(buf+strlen(buf), _("* Gets double defense against units"
			       " specified as 'mounted'.\n"));
  }
  if (unit_flag(i, F_HORSE)) {
    sprintf(buf+strlen(buf),
	    _("* Counts as 'mounted' against certain defenders.\n"));
  }
  if (unit_flag(i, F_MISSILE)) {
    sprintf(buf+strlen(buf),
	    _("* A missile unit: gets used up in making an attack.\n"));
  } else if(unit_flag(i, F_ONEATTACK)) {
    sprintf(buf+strlen(buf), _("* Making an attack ends this unit's turn.\n"));
  }
  if (unit_flag(i, F_NUCLEAR)) {
    sprintf(buf+strlen(buf),
	    _("* This unit's attack causes a nuclear explosion!\n"));
  }
  if (unit_flag(i, F_IGWALL)) {
    sprintf(buf+strlen(buf), _("* Ignores the effects of city walls.\n"));
  }
  if (unit_flag(i, F_AEGIS)) {
    sprintf(buf+strlen(buf),
	    _("* Gets quintuple defence against missiles and aircraft.\n"));
  }
  if (unit_flag(i, F_IGTER)) {
    sprintf(buf+strlen(buf),
	    _("* Ignores terrain effects (treats all squares as roads).\n"));
  }
  if (unit_flag(i, F_IGTIRED)) {
    sprintf(buf+strlen(buf),
	    _("* Attacks with full strength even if less than one movement left.\n"));
  }
  if (unit_flag(i, F_IGZOC)) {
    sprintf(buf+strlen(buf), _("* Ignores zones of control.\n"));
  }
  if (unit_flag(i, F_NONMIL)) {
    sprintf(buf+strlen(buf), _("* A non-military unit"
			       " (cannot attack; no martial law).\n"));
  }
  if (unit_flag(i, F_FIELDUNIT)) {
    sprintf(buf+strlen(buf), _("* A field unit: one unhappiness applies"
			       " even when non-aggressive.\n"));
  }
  if (unit_flag(i, F_TRIREME)) {
    sprintf(buf+strlen(buf),
	    _("* Must end turn in a city or next to land,"
	      " or has a 50%% risk of being lost at sea."));
  }
  if (utype->fuel>0) {
    sprintf(buf+strlen(buf), _("* Must end "));
    if (utype->fuel==2) {
      sprintf(buf+strlen(buf), _("second "));
    } else if (utype->fuel==3) {
      sprintf(buf+strlen(buf), _("third "));
    } else if (utype->fuel>=4) {
      sprintf(buf+strlen(buf), _("%dth "), utype->fuel);
    }
    /* FIXME: should use something like get_units_with_flag_string() */
    sprintf(buf+strlen(buf), _("turn in a city, or on a Carrier"));
    if (unit_flag(i, F_MISSILE) &&
	num_role_units(F_MISSILE_CARRIER)>0 &&
	get_unit_type(get_role_unit(F_MISSILE_CARRIER,0))->transport_capacity) {
      sprintf(buf+strlen(buf), _(" or Submarine"));
    }
    sprintf(buf+strlen(buf), _(", or will run out of fuel and be lost.\n"));
  }
  if (strlen(buf)) {
    sprintf(buf+strlen(buf), "\n");
  } 
  if (utype->helptext) {
    sprintf(buf+strlen(buf), "%s\n\n", _(utype->helptext));
  }
  strcpy(buf+strlen(buf), user_text);
  wordwrap_string(buf, 68);
}

/****************************************************************
  Append misc dynamic text for techs.
*****************************************************************/
void helptext_tech(char *buf, int i, const char *user_text)
{
  int gov;
  
  assert(buf&&user_text);
  strcpy(buf, user_text);

  for(gov=0; gov<game.government_count; gov++) {
    struct government *g = get_government(gov);
    if (g->required_tech == i) {
      sprintf(buf+strlen(buf), _("Allows changing government to %s.\n"),
	      g->name);
    }
  }
  if(tech_flag(i,TF_BONUS_TECH)) {
    sprintf(buf+strlen(buf),
	    _("The first player to research %s gets an immediate advance.\n"),
	    advances[i].name);
  }
  if(tech_flag(i,TF_BOAT_FAST))
    sprintf(buf+strlen(buf), _("Gives sea units one extra move.\n"));
  if(tech_flag(i,TF_POPULATION_POLLUTION_INC))
    sprintf(buf+strlen(buf), _("Increases the pollution generated by the population.\n"));
  if(game.rtech.cathedral_plus == i) 
    sprintf(buf+strlen(buf), _("Improves the effect of Cathedrals.\n"));
  if(game.rtech.cathedral_minus == i) 
    sprintf(buf+strlen(buf), _("Reduces the effect of Cathedrals.\n"));
  if(game.rtech.colosseum_plus == i) 
    sprintf(buf+strlen(buf), _("Improves the effect of Colosseums.\n"));
  if(game.rtech.temple_plus == i) 
    sprintf(buf+strlen(buf), _("Improves the effect of Temples.\n"));

  if(tech_flag(i,TF_BRIDGE)) {
    char *units_str = get_units_with_flag_string(F_SETTLERS);
    sprintf(buf+strlen(buf), _("Allows %s to build roads on river squares.\n"),units_str);
    free(units_str);
  }

  if(tech_flag(i,TF_FORTRESS)) {
    char *units_str = get_units_with_flag_string(F_SETTLERS);
    sprintf(buf+strlen(buf), _("Allows %s to build fortresses.\n"),units_str);
    free(units_str);
  }

  if(tech_flag(i,TF_AIRBASE)) {
    char *units_str = get_units_with_flag_string(F_AIRBASE);
    if (units_str) {
      sprintf(buf+strlen(buf), _("Allows %s to build airbases.\n"),units_str);
      free(units_str);
    }
  }

  if(tech_flag(i,TF_RAILROAD)) {
    char *units_str = get_units_with_flag_string(F_SETTLERS);
    sprintf(buf+strlen(buf), _("Allows %s to upgrade roads to railroads.\n"),units_str);
    free(units_str);
  }

  if(tech_flag(i,TF_FARMLAND)) {
    char *units_str = get_units_with_flag_string(F_SETTLERS);
    sprintf(buf+strlen(buf), _("Allows %s to upgrade irrigation to farmland.\n"),units_str);
    free(units_str);
  }
}

/****************************************************************
  Append text for terrain.
*****************************************************************/
void helptext_terrain(char *buf, int i, const char *user_text)
{
  struct tile_type *pt;
  
  buf[0] = '\0';
  
  if (i<0 || i>=T_COUNT)
    return;

  pt = &tile_types[i];
  if (pt->helptext) {
    sprintf(buf, "%s\n\n", _(pt->helptext));
  }
  strcat(buf, user_text);
  wordwrap_string(buf, 68);
}

/****************************************************************
  Append text for government.
*****************************************************************/
void helptext_government(char *buf, int i, const char *user_text)
{
  struct government *gov = get_government(i);
  
  buf[0] = '\0';
  
  if (gov->helptext) {
    sprintf(buf, "%s\n\n", _(gov->helptext));
  }
  strcat(buf, user_text);
  wordwrap_string(buf, 68);
}

/****************************************************************
  Returns pointer to static string with eg: "1 shield, 1 unhappy"
*****************************************************************/
char *helptext_unit_upkeep_str(int i)
{
  static char buf[128];
  struct unit_type *utype = get_unit_type(i);

  if (utype->shield_cost || utype->food_cost
      || utype->gold_cost || utype->happy_cost) {
    int any = 0;
    buf[0] = '\0';
    if (utype->shield_cost) {
      sprintf(buf+strlen(buf), _("%s%d shield"),
	      (any++ ? ", " : ""), utype->shield_cost);
    }
    if (utype->food_cost) {
      sprintf(buf+strlen(buf), _("%s%d food"),
	      (any++ ? ", " : ""), utype->food_cost);
    }
    if (utype->happy_cost) {
      sprintf(buf+strlen(buf), _("%s%d unhappy"),
	      (any++ ? ", " : ""), utype->happy_cost);
    }
    if (utype->gold_cost) {
      sprintf(buf+strlen(buf), _("%s%d gold"),
	      (any++ ? ", " : ""), utype->gold_cost);
    }
  } else {
    /* strcpy(buf, _("None")); */
    sprintf(buf, "%d", 0);
  }
  return buf;
}

#if 0
/****************************************************************
  Similar to above, with eg: "1s/1h"
*****************************************************************/
static char *helptext_unit_upkeep_str_short(int i)
{
  static char buf[32];
  struct unit_type *utype = get_unit_type(i);

  if (utype->shield_cost || utype->food_cost
      || utype->gold_cost || utype->happy_cost) {
    int any = 0;
    buf[0] = '\0';
    if (utype->shield_cost) {
      sprintf(buf+strlen(buf), /*s=shields*/ _("%s%ds"),
	      (any++ ? "/" : ""), utype->shield_cost);
    }
    if (utype->food_cost) {
      sprintf(buf+strlen(buf), /*f=food*/ _("%s%df"),
	      (any++ ? "/" : ""), utype->food_cost);
    }
    if (utype->happy_cost) {
      sprintf(buf+strlen(buf), /*h=(un)happy*/ _("%s%dh"),
	      (any++ ? "/" : ""), utype->happy_cost);
    }
    if (utype->gold_cost) {
      sprintf(buf+strlen(buf), /*g=gold*/ _("%s%dg"),
	      (any++ ? "/" : ""), utype->gold_cost);
    }
  } else {
    sprintf(buf, "%d", 0);
  }
  return buf;
}
#endif
