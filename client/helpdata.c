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

#include <string.h>
#include <stdio.h>
#include <assert.h>

#include "city.h"
#include "game.h"
#include "genlist.h"
#include "log.h"
#include "map.h"
#include "mem.h"
#include "unit.h"

#include "helpdata.h"


static const char * const help_type_names[] = {
  "", "TEXT", "UNIT", "IMPROVEMENT", "WONDER", "TECH", "TERRAIN", 0
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
*****************************************************************/
static void insert_generated_table(const char* name, char* outbuf)
{
  if (0 == strcmp (name, "TerrainAlterations"))
    {
      int i;
      strcat (outbuf, "Terrain     Road   Irrigation     Mining         Transform\n");
      strcat (outbuf, "---------------------------------------------------------------\n");
      for (i = T_FIRST; i < T_COUNT; i++)
	{
	  if (*(tile_types[i].terrain_name))
	    {
	      outbuf = strchr (outbuf, '\0');
	      sprintf
		(
		 outbuf,
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
      strcat (outbuf, "(Railroads and fortresses require 3 turns, regardless of terrain.)\n");
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
 for genlist_sort(); sort by topic via strcmp
*****************************************************************/
static int help_item_compar(const void *a, const void *b)
{
  const struct help_item *ha, *hb;
  ha = (const struct help_item*) *(const void**)a;
  hb = (const struct help_item*) *(const void**)b;
  return strcmp(ha->topic, hb->topic);
}

/****************************************************************
...
*****************************************************************/
void boot_help_texts(void)
{
  static int booted = 0;
  
  FILE *fs;
  char *dfname;
  char buf[512], *p;
  char expect[32], name[MAX_LEN_NAME+2];
  char seen[MAX_LAST], *pname;
  int len;
  struct help_item *pitem = NULL;
  enum help_page_type current_type = HELP_TEXT;
  struct genlist category_nodes;
  int i, filter_this;

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
  
  dfname = datafilename("helpdata.txt");
  if (dfname == NULL) {
    freelog(LOG_NORMAL, "Could not find readable helpdata.txt in data path");
    freelog(LOG_NORMAL, "The data path may be set via"
	                " the environment variable FREECIV_PATH");
    freelog(LOG_NORMAL, "Current data path is: \"%s\"", datafilename(NULL));
    freelog(LOG_NORMAL, "Did not read help texts");
    return;
  }
  fs = fopen(dfname, "r");
  if (fs == NULL) {
    /* this is now unlikely to happen */
    freelog(LOG_NORMAL, "failed reading help-texts");
    return;
  }

  while(1) {
    fgets(buf, 512, fs);
    buf[strlen(buf)-1]='\0';
    if(!strncmp(buf, "%%", 2))
      continue;
    len=strlen(buf);

    if (len>0 && buf[0] == '@') {
      if(current_type==HELP_TEXT) {
	current_type = -1;
	for(i=2; help_type_names[i]; i++) {
	  sprintf(expect, "START_%sS", help_type_names[i]);
	  if(strcmp(expect,buf+1)==0) {
	    current_type = i;
	    break;
	  }
	}
	if (current_type==-1) {
	  freelog(LOG_NORMAL, "bad help category \"%s\"", buf+1);
	  current_type = HELP_TEXT;
	} else {
	  genlist_init(&category_nodes);
	  for(i=0; i<MAX_LAST; i++) {
	    seen[i] = (booted?0:1); /* on initial boot data tables are empty */
	  }
	  freelog(LOG_DEBUG, "Help category %s",
		  help_type_names[current_type]);
	}
      } else {
	sprintf(expect, "END_%sS", help_type_names[current_type]);
	if(strcmp(expect,buf+1)!=0) {
	  freelog(LOG_FATAL, "bad end to help category \"%s\"", buf+1);
	  exit(1);
	}
	/* add defaults for those not seen: */
	if(current_type==HELP_UNIT) {
	  for(i=0; i<U_LAST; i++) {
	    if(!seen[i] && unit_type_exists(i)) {
	      pitem = new_help_item(current_type);
	      sprintf(name, " %s", unit_name(i));
	      pitem->topic = mystrdup(name);
	      pitem->text = mystrdup("");
	      genlist_insert(&category_nodes, pitem, -1);
	    }
	  }
	} else if(current_type==HELP_TECH) {
	  for(i=1; i<A_LAST; i++) {                 /* skip A_NONE */
	    if(!seen[i] && tech_exists(i)) {
	      pitem = new_help_item(current_type);
	      sprintf(name, " %s", advances[i].name);
	      pitem->topic = mystrdup(name);
	      pitem->text = mystrdup("");
	      genlist_insert(&category_nodes, pitem, -1);
	    }
	  }
	} else if(current_type==HELP_TERRAIN) {
	  for(i=T_FIRST; i<T_COUNT; i++) {
	    if(!seen[i] && *(tile_types[i].terrain_name)) {
	      pitem = new_help_item(current_type);
	      sprintf(name, " %s", tile_types[i].terrain_name);
	      pitem->topic = mystrdup(name);
	      pitem->text = mystrdup("");
	      genlist_insert(&category_nodes, pitem, -1);
	    }
	  }
	} else if(current_type==HELP_IMPROVEMENT) {
	  for(i=0; i<B_LAST; i++) {
	    if(!seen[i] && improvement_exists(i) && !is_wonder(i)) {
	      pitem = new_help_item(current_type);
	      sprintf(name, " %s", improvement_types[i].name);
	      pitem->topic = mystrdup(name);
	      pitem->text = mystrdup("");
	      genlist_insert(&category_nodes, pitem, -1);
	    }
	  }
	} else if(current_type==HELP_WONDER) {
	  for(i=0; i<B_LAST; i++) {
	    if(!seen[i] && improvement_exists(i) && is_wonder(i)) {
	      pitem = new_help_item(current_type);
	      sprintf(name, " %s", improvement_types[i].name);
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
	current_type = HELP_TEXT;
      }
      continue;
    }
    
    p=strchr(buf, '#');
    if(!p) {
      break;
    }
    pname = p;
    while(*(++pname)==' ')
      ;

    /* i==-1 is text; filter_this==1 is to be left out, but we have to
     * read in the help text so we're at the right place
     */
    i = -1;
    filter_this = 0;
    switch(current_type) {
    case HELP_UNIT:
      i = find_unit_type_by_name(pname);
      if(!unit_type_exists(i)) {
	if(booted)
	  freelog(LOG_VERBOSE, "Filtering unit type %s from help", pname);
	filter_this = 1;
      }
      break;
    case HELP_TECH:
      i = find_tech_by_name(pname);
      if(!tech_exists(i)) {
	if(booted)
	  freelog(LOG_VERBOSE, "Filtering tech %s from help", pname);
	filter_this = 1;
      }
      break;
    case HELP_TERRAIN:
      i = get_terrain_by_name(pname);
      if(i >= T_COUNT) {
	if(booted)
	  freelog(LOG_VERBOSE, "Filtering terrain %s from help", pname);
	filter_this = 1;
      }
      break;
    case HELP_IMPROVEMENT:
      i = find_improvement_by_name(pname);
      if(!improvement_exists(i) || is_wonder(i)) {
	if(booted)
	  freelog(LOG_VERBOSE, "Filtering city improvement %s from help", pname);
	filter_this = 1;
      }
      break;
    case HELP_WONDER:
      i = find_improvement_by_name(pname);
      if(!improvement_exists(i) || !is_wonder(i)) {
	if(booted)
	  freelog(LOG_VERBOSE, "Filtering wonder %s from help", pname);
	filter_this = 1;
      }
      break;
    default:
      /* nothing */
      {} /* placate Solaris cc/xmkmf/makedepend */
    }
    if(i>=0) {
      seen[i] = 1;
    }

    if(!filter_this) {
      pitem = new_help_item(current_type);
      pitem->topic = mystrdup(p+1);
    }

    long_buffer[0]='\0';
    while(1) {
      fgets(buf, 512, fs);
      buf[strlen(buf)-1]='\0';
      if(!strncmp(buf, "%%", 2))
	continue;
      if(!strncmp(buf, "$", 1)) {
	insert_generated_table (buf+1, long_buffer);
	continue;
      }
      if(!strcmp(buf, "---"))
	break;
      if(!filter_this) {
	strcat(long_buffer, buf);
	strcat(long_buffer, "\n");
      }
    }

    if(!filter_this) {
      pitem->text=mystrdup(long_buffer);
      if(current_type == HELP_TEXT) {
	genlist_insert(&help_nodes, pitem, -1);
      } else {
	genlist_insert(&category_nodes, pitem, -1);
      }
    }
  }

  if(current_type != HELP_TEXT) {
    freelog(LOG_FATAL, "Didn't finish help category %s",
	 help_type_names[current_type]);
    exit(1);
  }
  
  fclose(fs);
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
    freelog(LOG_NORMAL, "Bad index %d to get_help_item (size %d)", pos, size);
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
    strncpy(vtopic, name, sizeof(vtopic)-1);
    vitem.text = vtext;
    if(htype==HELP_ANY || htype==HELP_TEXT) {
      sprintf(vtext, "Sorry, no help topic for %s.\n", vitem.topic);
      vitem.type = HELP_TEXT;
    } else {
      sprintf(vtext, "Sorry, no help topic for %s.\n"
	      "This page was auto-generated.\n\n",
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
    sprintf(buf, "The discovery of %s will reduce this by 1.\n",
	    advances[t].name);
    buf += strlen(buf);
  }
  t=game.rtech.cathedral_plus;
  if(tech_exists(t)) {
    sprintf(buf, "The discovery of %s will increase this by 1.\n",
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
  assert(buf&&user_text);
  buf[0] = '\0';
  if(which==B_AQUEDUCT) {
    sprintf(buf+strlen(buf), "Allows a city to grow larger than size %d.",
	    game.aqueduct_size);
    if(improvement_exists(B_SEWER)) {
      char *s = improvement_types[B_SEWER].name;
      sprintf(buf+strlen(buf), "  (A%s %s is also\n"
	      "required for a city to grow larger than size %d.)",
	      n_if_vowel(*s), s, game.sewer_size);
    }
    strcat(buf,"\n");
  }
  if(which==B_SEWER) {
    sprintf(buf+strlen(buf), "Allows a city to grow larger than size %d.\n",
	   game.sewer_size);
  }
  strcat(buf, user_text);
  if(which==B_CATHEDRAL) {
    helptext_cathedral_techs(buf+strlen(buf));
  }
  if(which==B_COLOSSEUM) {
    int t=game.rtech.colosseum_plus;
    if(tech_exists(t)) {
      int n = strlen(buf);
      if(n && buf[n-1] == '\n') buf[n-1] = ' ';
      sprintf(buf+n, "The discovery of %s will increase this by 1.\n",
	     advances[t].name);
    }
  }
  if(which==B_BARRACKS
     && tech_exists(improvement_types[B_BARRACKS].obsolete_by)
     && tech_exists(improvement_types[B_BARRACKS2].obsolete_by)) {
    sprintf(buf+strlen(buf),
	   "\nNote that discovering %s or %s will obsolete\n"
	   "any existing %s.\n",
	   advances[improvement_types[B_BARRACKS].obsolete_by].name,
	   advances[improvement_types[B_BARRACKS2].obsolete_by].name,
	   improvement_types[B_BARRACKS].name);
  }
  if(which==B_BARRACKS2
     && tech_exists(improvement_types[B_BARRACKS2].obsolete_by)) {
    sprintf(buf+strlen(buf),
	   "\nThe discovery of %s will make %s obsolete.\n",
	   advances[improvement_types[B_BARRACKS2].obsolete_by].name,
	   improvement_types[B_BARRACKS2].name);
  }
}

/****************************************************************
  Append misc dynamic text for wonders.
*****************************************************************/
void helptext_wonder(char *buf, int which,
			    const char *user_text)
{
  assert(buf&&user_text);
  buf[0] = '\0';
  if(which==B_MANHATTEN && num_role_units(F_NUCLEAR)>0) {
    int u, t;
    u = get_role_unit(F_NUCLEAR, 0);
    assert(u<U_LAST);
    t = get_unit_type(u)->tech_requirement;
    assert(t<A_LAST);
    sprintf(buf+strlen(buf),
	   "Allows all players with knowledge of %s to build %s units.\n",
	   advances[t].name, get_unit_type(u)->name);
  }
  strcat(buf, user_text);
  if(which==B_MICHELANGELO) {
    helptext_cathedral_techs(buf+strlen(buf));
  }
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
    if (unit_flag(i, F_SUBMARINE)) {
      sprintf(buf+strlen(buf), "* Can carry and refuel %d missile units.\n",
	      utype->transport_capacity);
    } else if (unit_flag(i, F_CARRIER)) {
      sprintf(buf+strlen(buf), "* Can carry and refuel %d air units.\n",
	      utype->transport_capacity);
    } else {
      sprintf(buf+strlen(buf), "* Can carry %d ground units across water.\n",
	      utype->transport_capacity);
    }
  }
  if (unit_flag(i, F_CARAVAN)) {
    sprintf(buf+strlen(buf), "* Can establish trade routes and help build wonders.\n");
  }
  if (unit_flag(i, F_SETTLERS)) {
    sprintf(buf+strlen(buf), "* Can perform settler actions.\n");
  }
  if (unit_flag(i, F_DIPLOMAT)) {
    if (unit_flag(i, F_SPY)) 
      sprintf(buf+strlen(buf), "* Can perform diplomatic actions, plus special spy abilities.\n");
    else 
      sprintf(buf+strlen(buf), "* Can perform diplomatic actions.\n");
  }
  if (unit_flag(i, F_FIGHTER)) {
    sprintf(buf+strlen(buf), "* Can attack enemy air units.\n");
  }
  if (unit_flag(i, F_MARINES)) {
    sprintf(buf+strlen(buf), "* Can attack from aboard sea units: against enemy cities and\n  onto land squares.");
  }
  if (unit_flag(i, F_PIKEMEN)) {
    sprintf(buf+strlen(buf), "* Gets double defense against units specified as 'mounted'.\n");
  }
  if (unit_flag(i, F_HORSE)) {
    sprintf(buf+strlen(buf), "* Counts as 'mounted' against certain defenders.\n");
  }
  if (unit_flag(i, F_MISSILE)) {
    sprintf(buf+strlen(buf), "* A missile unit: gets used up in making an attack.\n");
  } else if(unit_flag(i, F_ONEATTACK)) {
    sprintf(buf+strlen(buf), "* Making an attack ends this unit's turn.\n");
  }
  if (unit_flag(i, F_NUCLEAR)) {
    sprintf(buf+strlen(buf), "* This unit's attack causes a nuclear explosion!\n");
  }
  if (unit_flag(i, F_IGWALL)) {
    sprintf(buf+strlen(buf), "* Ignores the effects of city walls.\n");
  }
  if (unit_flag(i, F_AEGIS)) {
    sprintf(buf+strlen(buf), "* Gets quintuple defence against missiles and aircraft.\n");
  }
  if (unit_flag(i, F_IGTER)) {
    sprintf(buf+strlen(buf), "* Ignores terrain effects (treats all squares as roads).\n");
  }
  if (unit_flag(i, F_IGZOC)) {
    sprintf(buf+strlen(buf), "* Ignores zones of control.\n");
  }
  if (unit_flag(i, F_NONMIL)) {
    sprintf(buf+strlen(buf), "* A non-military unit (no shield upkeep).\n");
  }
  if (unit_flag(i, F_TRIREME)) {
    sprintf(buf+strlen(buf), "* Must end turn in a city or next to land, or has a 50%% risk of\n  being lost at sea.");
  }
  if (utype->fuel>0) {
    sprintf(buf+strlen(buf), "* Must end ");
    if (utype->fuel==2) {
      sprintf(buf+strlen(buf), "second ");
    } else if (utype->fuel==3) {
      sprintf(buf+strlen(buf), "third ");
    } else if (utype->fuel>=4) {
      sprintf(buf+strlen(buf), "%dth ", utype->fuel);
    }
    sprintf(buf+strlen(buf), "turn in a city, or on a Carrier");
    if (unit_flag(i, F_MISSILE) &&
	num_role_units(F_SUBMARINE)>0 &&
	get_unit_type(get_role_unit(F_SUBMARINE,0))->transport_capacity) {
      sprintf(buf+strlen(buf), " or Submarine");
    }
    sprintf(buf+strlen(buf), ",\n  or will run out of fuel and be lost.\n");
  }
  if (strlen(buf)) {
    sprintf(buf+strlen(buf), "\n");
  } 
  strcpy(buf+strlen(buf), user_text);
}

/****************************************************************
  Append misc dynamic text for techs.
*****************************************************************/
void helptext_tech(char *buf, int i, const char *user_text)
{
  assert(buf&&user_text);
  strcpy(buf, user_text);
  if(game.rtech.get_bonus_tech == i) {
    sprintf(buf+strlen(buf),
	    "The first player to research %s gets an immediate advance.\n",
	    advances[i].name);
  }
  if(game.rtech.boat_fast == i) 
    sprintf(buf+strlen(buf), "Gives sea units one extra move.\n");
  if(game.rtech.cathedral_plus == i) 
    sprintf(buf+strlen(buf), "Improves the effect of Cathedrals.\n");
  if(game.rtech.cathedral_minus == i) 
    sprintf(buf+strlen(buf), "Reduces the effect of Cathedrals.\n");
  if(game.rtech.colosseum_plus == i) 
    sprintf(buf+strlen(buf), "Improves the effect of Colosseums.\n");
}

