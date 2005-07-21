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

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "astring.h"
#include "city.h"
#include "fcintl.h"
#include "game.h"
#include "genlist.h"
#include "government.h"
#include "log.h"
#include "map.h"
#include "mem.h"
#include "packets.h"
#include "registry.h"
#include "support.h"
#include "unit.h"

#include "helpdata.h"

static const char * const help_type_names[] = {
  "(Any)", "(Text)", "Units", "Improvements", "Wonders",
  "Techs", "Terrain", "Governments", NULL
};

#define MAX_LAST (MAX(MAX(MAX(A_LAST,B_LAST),U_LAST),T_COUNT))

#define SPECLIST_TAG help
#define SPECLIST_TYPE struct help_item
#include "speclist.h"

#define help_list_iterate(helplist, phelp) \
    TYPED_LIST_ITERATE(struct help_item, helplist, phelp)
#define help_list_iterate_end  LIST_ITERATE_END

static struct genlist_link *help_nodes_iterator;
static struct help_list help_nodes;
static bool help_nodes_init = FALSE;
/* helpnodes_init is not quite the same as booted in boot_help_texts();
   latter can be 0 even after call, eg if couldn't find helpdata.txt.
*/

/****************************************************************
  Make sure help_nodes is initialised.
  Should call this just about everywhere which uses help_nodes,
  to be careful...  or at least where called by external
  (non-static) functions.
*****************************************************************/
static void check_help_nodes_init(void)
{
  if (!help_nodes_init) {
    help_list_init(&help_nodes);
    help_nodes_init = TRUE;    /* before help_iter_start to avoid recursion! */
    help_iter_start();
  }
}

/****************************************************************
  Free all allocations associated with help_nodes.
*****************************************************************/
void free_help_texts(void)
{
  check_help_nodes_init();
  help_list_iterate(help_nodes, ptmp) {
    free(ptmp->topic);
    free(ptmp->text);
    free(ptmp);
  } help_list_iterate_end;
  help_list_unlink_all(&help_nodes);
}

/****************************************************************************
  Insert generated data for the helpdate name.

  Currently only for terrain ("TerrainAlterations") is such a table created.
****************************************************************************/
static void insert_generated_table(const char* name, char* outbuf)
{
  if (0 == strcmp (name, "TerrainAlterations")) {
    int i;

    strcat(outbuf, _("Terrain     Road   Irrigation     Mining         "
		      "Transform\n"));
    strcat(outbuf, "---------------------------------------------------"
	   "------------\n");
    for (i = T_FIRST; i < T_COUNT; i++) {
      struct tile_type *ptype = get_tile_type(i);

      if (*(ptype->terrain_name) != '\0') {
	outbuf = strchr(outbuf, '\0');
	sprintf(outbuf,
		"%-10s %3d    %3d %-10s %3d %-10s %3d %-10s\n",
		ptype->terrain_name,
		ptype->road_time,
		ptype->irrigation_time,
		((ptype->irrigation_result == i
		  || ptype->irrigation_result == T_NONE) ? ""
		 : get_tile_type(ptype->irrigation_result)->terrain_name),
		ptype->mining_time,
		((ptype->mining_result == i
		  || ptype->mining_result == T_NONE) ? ""
		 : get_tile_type(ptype->mining_result)->terrain_name),
		ptype->transform_time,
		((ptype->transform_result == i
		 || ptype->transform_result == T_NONE) ? ""
		 : get_tile_type(ptype->transform_result)->terrain_name));
      }
    }
    strcat(outbuf, "\n");
    strcat(outbuf, _("(Railroads and fortresses require 3 turns, "
		     "regardless of terrain.)"));
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
 for help_list_sort(); sort by topic via compare_strings()
 (sort topics with more leading spaces after those with fewer)
*****************************************************************/
static int help_item_compar(const void *a, const void *b)
{
  const struct help_item *ha, *hb;
  char *ta, *tb;
  ha = (const struct help_item*) *(const void**)a;
  hb = (const struct help_item*) *(const void**)b;
  for (ta = ha->topic, tb = hb->topic; *ta != '\0' && *tb != '\0'; ta++, tb++) {
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
  static bool booted = FALSE;

  struct section_file file, *sf = &file;
  char *filename;
  struct help_item *pitem;
  int i, isec;
  char **sec, **paras;
  int nsec, npara;
  char long_buffer[64000]; /* HACK: this may be overrun. */

  check_help_nodes_init();

  /* need to do something like this or bad things happen */
  popdown_help_dialog();

  if(!booted) {
    freelog(LOG_VERBOSE, "Booting help texts");
  } else {
    /* free memory allocated last time booted */
    free_help_texts();
    freelog(LOG_VERBOSE, "Rebooting help texts");
  }    

  filename = datafilename("helpdata.txt");
  if (!filename) {
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
    const char *gen_str =
      secfile_lookup_str_default(sf, NULL, "%s.generate", sec[isec]);
    
    if (gen_str) {
      enum help_page_type current_type = HELP_ANY;
      if (!booted) {
	continue; /* on initial boot data tables are empty */
      }
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
	char name[2048];
	struct help_list category_nodes;
	
	help_list_init(&category_nodes);
	if (current_type == HELP_UNIT) {
	  unit_type_iterate(i) {
	    if (unit_type_exists(i)) {
	      pitem = new_help_item(current_type);
	      my_snprintf(name, sizeof(name), " %s", unit_name(i));
	      pitem->topic = mystrdup(name);
	      pitem->text = mystrdup("");
	      help_list_insert_back(&category_nodes, pitem);
	    }
	  } unit_type_iterate_end;
	} else if (current_type == HELP_TECH) {
	  tech_type_iterate(i) {
	    if (i != A_NONE && tech_exists(i)) {
	      pitem = new_help_item(current_type);
	      my_snprintf(name, sizeof(name), " %s",
			  get_tech_name(game.player_ptr, i));
	      pitem->topic = mystrdup(name);
	      pitem->text = mystrdup("");
	      help_list_insert_back(&category_nodes, pitem);
	    }
	  } tech_type_iterate_end;
	} else if (current_type == HELP_TERRAIN) {
	  for (i = T_FIRST; i < T_COUNT; i++) {
	    struct tile_type *ptype = get_tile_type(i);

	    if (*(ptype->terrain_name) != '\0') {
	      pitem = new_help_item(current_type);
	      my_snprintf(name, sizeof(name), " %s",
			  ptype->terrain_name);
	      pitem->topic = mystrdup(name);
	      pitem->text = mystrdup("");
	      help_list_insert_back(&category_nodes, pitem);
	    }
	  }
	  /* Add special Civ2-style river help text if it's supplied. */
	  if (terrain_control.river_help_text) {
	    pitem = new_help_item(HELP_TEXT);
	    /* TRANS: preserve single space at beginning */
	    pitem->topic = mystrdup(_(" Rivers"));
	    strcpy(long_buffer, _(terrain_control.river_help_text));
	    wordwrap_string(long_buffer, 68);
	    pitem->text = mystrdup(long_buffer);
	    help_list_insert_back(&category_nodes, pitem);
	  }
	} else if (current_type == HELP_GOVERNMENT) {
	  government_iterate(gov) {
	    pitem = new_help_item(current_type);
	    my_snprintf(name, sizeof(name), " %s", gov->name);
	    pitem->topic = mystrdup(name);
	    pitem->text = mystrdup("");
	    help_list_insert_back(&category_nodes, pitem);
	  } government_iterate_end;
	} else if (current_type == HELP_IMPROVEMENT) {
	  impr_type_iterate(i) {
	    if (improvement_exists(i) && !is_wonder(i)) {
	      pitem = new_help_item(current_type);
	      my_snprintf(name, sizeof(name), " %s",
			  improvement_types[i].name);
	      pitem->topic = mystrdup(name);
	      pitem->text = mystrdup("");
	      help_list_insert_back(&category_nodes, pitem);
	    }
	  } impr_type_iterate_end;
	} else if (current_type == HELP_WONDER) {
	  impr_type_iterate(i) {
	    if (improvement_exists(i) && is_wonder(i)) {
	      pitem = new_help_item(current_type);
	      my_snprintf(name, sizeof(name), " %s",
			  improvement_types[i].name);
	      pitem->topic = mystrdup(name);
	      pitem->text = mystrdup("");
	      help_list_insert_back(&category_nodes, pitem);
	    }
	  } impr_type_iterate_end;
	} else {
	  die("Bad current_type %d", current_type);
	}
	help_list_sort(&category_nodes, help_item_compar);
	help_list_iterate(category_nodes, ptmp) {
	  help_list_insert_back(&help_nodes, ptmp);
	} help_list_iterate_end;
	help_list_unlink_all(&category_nodes);
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
    paras = NULL;
    wordwrap_string(long_buffer, 68);
    pitem->text=mystrdup(long_buffer);
    help_list_insert_back(&help_nodes, pitem);
  }

  free(sec);
  sec = NULL;
  section_file_check_unused(sf, sf->filename);
  section_file_free(sf);
  booted = TRUE;
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
  return help_list_size(&help_nodes);
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
  size = help_list_size(&help_nodes);
  if (pos < 0 || pos > size) {
    freelog(LOG_ERROR, "Bad index %d to get_help_item (size %d)", pos, size);
    return NULL;
  }
  if (pos == size) {
    return NULL;
  }
  return help_list_get(&help_nodes, pos);
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
    while (*p == ' ') {
      p++;
    }
    if(strcmp(name, p)==0 && (htype==HELP_ANY || htype==ptmp->type)) {
      pitem = ptmp;
      break;
    }
    idx++;
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
  help_nodes_iterator = help_nodes.list.head_link;
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
  if (pitem) {
    ITERATOR_NEXT(help_nodes_iterator);
  }

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

/**************************************************************************
  Write dynamic text for buildings (including wonders).  This includes
  the ruleset helptext as well as any automatically generated text.

  user_text, if non-NULL, will be appended to the text.
**************************************************************************/
char *helptext_building(char *buf, size_t bufsz, Impr_Type_id which,
			const char *user_text)
{
  struct impr_type *imp = &improvement_types[which];
  
  assert(buf);
  buf[0] = '\0';

  if (imp->helptext && imp->helptext[0] != '\0') {
    my_snprintf(buf + strlen(buf), bufsz - strlen(buf),
		"%s\n\n", _(imp->helptext));
  }

  if (tech_exists(improvement_types[which].obsolete_by)) {
    my_snprintf(buf + strlen(buf), bufsz - strlen(buf),
		_("* The discovery of %s will make %s obsolete.\n"),
		get_tech_name(game.player_ptr,
			improvement_types[which].obsolete_by),
		improvement_types[which].name);
  }

  if (building_has_effect(which, EFT_ENABLE_NUKE)
      && num_role_units(F_NUCLEAR) > 0) {
    Unit_Type_id u;
    Tech_Type_id t;

    u = get_role_unit(F_NUCLEAR, 0);
    assert(u < game.num_unit_types);
    t = get_unit_type(u)->tech_requirement;
    assert(t < game.num_tech_types);

    my_snprintf(buf + strlen(buf), bufsz - strlen(buf),
		_("* Allows all players with knowledge of %s "
		  "to build %s units.\n"),
		get_tech_name(game.player_ptr, t), get_unit_type(u)->name);
    my_snprintf(buf + strlen(buf), bufsz - strlen(buf), "  ");
  }

  impr_type_iterate(impr) {
    const struct impr_type *b = get_improvement_type(impr);

    if (improvement_exists(impr) && b->bldg_req == which) {
      char req_buf[1024] = "";
      int i;

#define req_append(s)							    \
      (req_buf[0] != '\0'						    \
       ? my_snprintf(req_buf + strlen(req_buf),				    \
		     sizeof(req_buf) - strlen(req_buf),			    \
		     ", %s", (s))					    \
       : sz_strlcpy(req_buf, (s)))

      if (b->tech_req != A_NONE) {
	req_append(get_tech_name(game.player_ptr, b->tech_req));
      }

      for (i = 0; b->terr_gate[i] != T_NONE; i++) {
	req_append(get_terrain_name(b->terr_gate[i]));
      }
      for (i = 0; b->spec_gate[i] != S_NO_SPECIAL; i++) {
	req_append(get_special_name(b->spec_gate[i]));
      }
#undef req_append

      if (req_buf[0] != '\0') {
	my_snprintf(buf + strlen(buf), bufsz - strlen(buf),
		    _("* Allows %s (with %s).\n"), b->name, req_buf);
      } else {
	my_snprintf(buf + strlen(buf), bufsz - strlen(buf),
		    _("* Allows %s.\n"), b->name);
      }
    }
  } impr_type_iterate_end;

  unit_type_iterate(utype) {
    const struct unit_type *u = get_unit_type(utype);

    if (unit_type_exists(utype) && u->impr_requirement == which) {
      if (u->tech_requirement != A_LAST) {
	my_snprintf(buf + strlen(buf), bufsz - strlen(buf),
		    _("* Allows %s (with %s).\n"), u->name,
		    get_tech_name(game.player_ptr, u->tech_requirement));
      } else {
	my_snprintf(buf + strlen(buf), bufsz - strlen(buf),
		    _("* Allows %s.\n"), u->name);
      }
    }
  } unit_type_iterate_end;

  if (user_text && user_text[0] != '\0') {
    my_snprintf(buf + strlen(buf), bufsz - strlen(buf), "\n\n%s", user_text);
  }

  wordwrap_string(buf, 68);
  return buf;
}

#define techs_with_flag_iterate(flag, tech_id)				    \
{									    \
  Tech_Type_id tech_id = 0;						    \
									    \
  while ((tech_id = find_tech_by_flag(tech_id, (flag))) != A_LAST) {

#define techs_with_flag_iterate_end		\
    tech_id++;					\
  }						\
}

/****************************************************************************
  Return a string containing the techs that have the flag.  Returns the
  number of techs found.
****************************************************************************/
static int techs_with_flag_string(enum tech_flag_id flag,
				  char *buf, size_t bufsz)
{
  int count = 0;

  assert(bufsz > 0);
  buf[0] = '\0';
  techs_with_flag_iterate(flag, tech_id) {
    const char *name = get_tech_name(game.player_ptr, tech_id);
    
    if (buf[0] == '\0') {
      my_snprintf(buf, bufsz, "%s", name);
    } else {
      my_snprintf(buf + strlen(buf), bufsz - strlen(buf), ", %s", name);
    }
    count++;
  } techs_with_flag_iterate_end;

  return count;
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
  if (unit_type_flag(i, F_NOBUILD)) {
    sprintf(buf + strlen(buf),
	    _("* May not be built in cities.\n"));
  }
  if (unit_type_flag(i, F_NOHOME)) {
    sprintf(buf + strlen(buf), _("* Never has a home city.\n"));
  }
  if (unit_type_flag(i, F_GAMELOSS)) {
    sprintf(buf + strlen(buf),
	    _("* Losing this unit will lose you the game!\n"));
  }
  if (unit_type_flag(i, F_UNIQUE)) {
    sprintf(buf + strlen(buf),
	    _("* Each player may only have one of this type of unit.\n"));
  }
  if (utype->pop_cost > 0) {
    sprintf(buf + strlen(buf), _("* Requires %d population to build.\n"),
	    utype->pop_cost);
  }
  if (utype->transport_capacity>0) {
    if (unit_type_flag(i, F_CARRIER)) {
      sprintf(buf + strlen(buf),
	      PL_("* Can carry and refuel %d air unit.\n",
		  "* Can carry and refuel %d air units.\n",
		  utype->transport_capacity), utype->transport_capacity);
    } else if (unit_type_flag(i, F_MISSILE_CARRIER)) {
      sprintf(buf + strlen(buf),
	      PL_("* Can carry and refuel %d missile unit.\n",
		  "* Can carry and refuel %d missile units.\n",
		  utype->transport_capacity), utype->transport_capacity);
    } else {
      sprintf(buf + strlen(buf),
	      PL_("* Can carry %d ground unit across water.\n",
		  "* Can carry %d ground units across water.\n",
		  utype->transport_capacity), utype->transport_capacity);
    }
  }
  if (unit_type_flag(i, F_TRADE_ROUTE)) {
    /* TRANS: "Manhattan" distance is the distance along gridlines, with
     * no diagonals allowed. */
    sprintf(buf + strlen(buf), _("* Can establish trade routes (must travel "
				 "to target city and must be at least 9 "
				 "tiles [in Manhattan distance] from this "
				 "unit's home city).\n"));
  }
  if (unit_type_flag(i, F_HELP_WONDER)) {
    sprintf(buf + strlen(buf),
	    _("* Can help build wonders (adds %d production).\n"),
	    utype->build_cost);
  }
  if (unit_type_flag(i, F_UNDISBANDABLE)) {
    sprintf(buf + strlen(buf), _("* May not be disbanded.\n"));
  } else {
    sprintf(buf + strlen(buf), _("* May be disbanded in a city to "
				 "recover 50%% of the production cost.\n"));
  }
  if (unit_type_flag(i, F_CITIES)) {
    sprintf(buf + strlen(buf), _("* Can build new cities.\n"));
  }
  if (unit_type_flag(i, F_ADD_TO_CITY)) {
    sprintf(buf + strlen(buf), _("* Can add on %d population to "
				 "cities of no more than size %d.\n"),
	    unit_pop_value(i), game.add_to_size_limit - unit_pop_value(i));
  }
  if (unit_type_flag(i, F_SETTLERS)) {
    char buf2[1024];

    /* Roads, rail, mines, irrigation. */
    sprintf(buf + strlen(buf), _("* Can build roads and railroads.\n"));
    sprintf(buf + strlen(buf), _("* Can build mines on tiles.\n"));
    sprintf(buf + strlen(buf), _("* Can build irrigation on tiles.\n"));

    /* Farmland. */
    switch (techs_with_flag_string(TF_FARMLAND, buf2, sizeof(buf2))) {
    case 0:
      sprintf(buf + strlen(buf), _("* Can build farmland.\n"));
      break;
    case 1:
      sprintf(buf + strlen(buf),
	      _("* Can build farmland (if %s is known).\n"), buf2);
      break;
    default:
      sprintf(buf + strlen(buf),
	      _("* Can build farmland (if any of the following are "
		"known: %s).\n"), buf2);
      break;
    }

    /* Fortress. */
    switch (techs_with_flag_string(TF_FORTRESS, buf2, sizeof(buf2))) {
    case 0:
      sprintf(buf + strlen(buf), _("* Can build fortresses.\n"));
      break;
    case 1:
      sprintf(buf + strlen(buf),
	      _("* Can build fortresses (if %s is known).\n"), buf2);
      break;
    default:
      sprintf(buf + strlen(buf),
	      _("* Can build fortresses (if any of the following are "
		"known: %s).\n"), buf2);
      break;
    }

    /* Pollution, fallout. */
    sprintf(buf + strlen(buf), _("* Can clean pollution from tiles.\n"));
    sprintf(buf + strlen(buf),
	    _("* Can clean nuclear fallout from tiles.\n"));
  }
  if (unit_type_flag(i, F_TRANSFORM)) {
    sprintf(buf + strlen(buf), _("* Can transform tiles.\n"));
  }
  if (unit_type_flag(i, F_AIRBASE)) {
    sprintf(buf + strlen(buf), _("* Can build airbases.\n"));
  }
  if (is_ground_unittype(i) && !unit_type_flag(i, F_SETTLERS)) {
    sprintf(buf + strlen(buf),
	    _("* May fortify, granting a 50%% defensive bonus.\n"));
  }
  if (is_ground_unittype(i)) {
    sprintf(buf + strlen(buf),
	    _("* May pillage to destroy infrastructure from tiles.\n"));
  }
  if (unit_type_flag(i, F_DIPLOMAT)) {
    if (unit_type_flag(i, F_SPY)) {
      sprintf(buf + strlen(buf), _("* Can perform diplomatic actions,"
				   " plus special spy abilities.\n"));
    } else {
      sprintf(buf + strlen(buf), _("* Can perform diplomatic actions.\n"));
    }
  }
  if (unit_type_flag(i, F_SUPERSPY)) {
    sprintf(buf + strlen(buf), _("* Will never lose a "
				 "diplomat-versus-diplomat fight.\n"));
  }
  if (unit_type_flag(i, F_UNBRIBABLE)) {
    sprintf(buf + strlen(buf), _("* May not be bribed.\n"));
  }
  if (unit_type_flag(i, F_FIGHTER)) {
    sprintf(buf + strlen(buf), _("* Can attack enemy air units.\n"));
  }
  if (unit_type_flag(i, F_PARTIAL_INVIS)) {
    sprintf(buf + strlen(buf), _("* Is invisible except when next to an"
				 " enemy unit or city.\n"));
  }
  if (unit_type_flag(i, F_NO_LAND_ATTACK)) {
    sprintf(buf + strlen(buf), _("* Can only attack units on ocean squares"
				 " (no land attacks).\n"));
  }
  if (unit_type_flag(i, F_MARINES)) {
    sprintf(buf + strlen(buf),
	    _("* Can attack from aboard sea units: against"
	      " enemy cities and onto land squares.\n"));
  }
  if (unit_type_flag(i, F_PARATROOPERS)) {
    sprintf(buf + strlen(buf),
	    _("* Can be paradropped from a friendly city"
	      " (Range: %d).\n"), utype->paratroopers_range);
  }
  if (unit_type_flag(i, F_PIKEMEN)) {
    sprintf(buf + strlen(buf), _("* Gets double defense against units"
				 " specified as 'mounted'.\n"));
  }
  if (unit_type_flag(i, F_HORSE)) {
    sprintf(buf + strlen(buf),
	    _("* Counts as 'mounted' against certain defenders.\n"));
  }
  if (unit_type_flag(i, F_MISSILE)) {
    sprintf(buf + strlen(buf),
	    _("* A missile unit: gets used up in making an attack.\n"));
  } else if(unit_type_flag(i, F_ONEATTACK)) {
    sprintf(buf + strlen(buf),
	    _("* Making an attack ends this unit's turn.\n"));
  }
  if (unit_type_flag(i, F_NUCLEAR)) {
    sprintf(buf + strlen(buf),
	    _("* This unit's attack causes a nuclear explosion!\n"));
  }
  if (unit_type_flag(i, F_CITYBUSTER)) {
    sprintf(buf + strlen(buf),
	    _("* Gets double firepower when attacking cities.\n"));
  }
  if (unit_type_flag(i, F_IGWALL)) {
    sprintf(buf + strlen(buf), _("* Ignores the effects of city walls.\n"));
  }
  if (unit_type_flag(i, F_BOMBARDER)) {
    sprintf(buf + strlen(buf),
	    _("* Does bombard attacks (%d per turn).  These attacks will "
	      "only damage (never kill) the defender but has no risk to "
	      "the attacker.\n"), utype->bombard_rate);
  }
  if (unit_type_flag(i, F_AEGIS)) {
    sprintf(buf + strlen(buf),
	    _("* Gets quintuple defence against missiles and aircraft.\n"));
  }
  if (unit_type_flag(i, F_IGTER)) {
    sprintf(buf + strlen(buf),
	    _("* Ignores terrain effects (treats all squares as roads).\n"));
  }
  if (unit_type_flag(i, F_IGTIRED)) {
    sprintf(buf + strlen(buf),
	    _("* Attacks with full strength even if less than "
	      "one movement left.\n"));
  }
  if (unit_type_flag(i, F_IGZOC)) {
    sprintf(buf + strlen(buf), _("* Ignores zones of control.\n"));
  }
  if (unit_type_flag(i, F_NONMIL)) {
    sprintf(buf + strlen(buf), _("* A non-military unit"
				 " (cannot attack; no martial law).\n"));
  }
  if (unit_type_flag(i, F_FIELDUNIT)) {
    sprintf(buf + strlen(buf), _("* A field unit: one unhappiness applies"
				 " even when non-aggressive.\n"));
  }
  if (unit_type_flag(i, F_NO_VETERAN)) {
    sprintf(buf + strlen(buf),
	    _("* Will never achieve veteran status.\n"));
  } else {
    sprintf(buf + strlen(buf),
	    _("* May become veteran through training or combat.\n"));
  }
  if (unit_type_flag(i, F_TRIREME)) {
    Tech_Type_id tech1 = find_tech_by_flag(0, TF_REDUCE_TRIREME_LOSS1);
    Tech_Type_id tech2 = find_tech_by_flag(0, TF_REDUCE_TRIREME_LOSS2);
    sprintf(buf + strlen(buf),
	    _("* Must end turn in a city or next to land,"
	      " or has a 50%% risk of being lost at sea.\n"));
    if (tech1 != A_LAST) {
      sprintf(buf + strlen(buf),
	      _("* The discovery of %s reduces the risk to 25%%.\n"),
	      get_tech_name(game.player_ptr, tech1));
    }
    if (tech2 != A_LAST) {
      sprintf(buf + strlen(buf),
	      _("* %s reduces the risk to 12%%.\n"),
	      get_tech_name(game.player_ptr, tech2));
    }
  }
  if (utype->fuel > 0) {
    char allowed_units[10][64];
    int num_allowed_units = 0;
    int j, n;
    struct astring astr;

    astr_init(&astr);
    astr_minsize(&astr,1);
    astr.str[0] = '\0';

    n = num_role_units(F_CARRIER);
    for (j = 0; j < n; j++) {
      Unit_Type_id id = get_role_unit(F_CARRIER, j);

      mystrlcpy(allowed_units[num_allowed_units],
		unit_name(id), sizeof(allowed_units[num_allowed_units]));
      num_allowed_units++;
      assert(num_allowed_units < ARRAY_SIZE(allowed_units));
    }

    if (unit_type_flag(i, F_MISSILE)) {
      n = num_role_units(F_MISSILE_CARRIER);

      for (j = 0; j < n; j++) {
	Unit_Type_id id = get_role_unit(F_MISSILE_CARRIER, j);

	if (get_unit_type(id)->transport_capacity > 0) {
	  mystrlcpy(allowed_units[num_allowed_units],
		    unit_name(id), sizeof(allowed_units[num_allowed_units]));
	  num_allowed_units++;
	  assert(num_allowed_units < ARRAY_SIZE(allowed_units));
	}
      }
    }

    for (j = 0; j < num_allowed_units; j++) {
      const char *deli_str = NULL;

      /* there should be something like astr_append() */
      astr_minsize(&astr, astr.n + strlen(allowed_units[j]));
      strcat(astr.str, allowed_units[j]);

      if (j == num_allowed_units - 2) {
	deli_str = _(" or ");
      } else if (j < num_allowed_units - 1) {
	deli_str = Q_("?or:, ");
      }

      if (deli_str) {
	astr_minsize(&astr, astr.n + strlen(deli_str));
	strcat(astr.str, deli_str);
      }
    }
    
    assert(num_allowed_units > 0);

    sprintf(buf + strlen(buf),
	    PL_("* Unit has to be in a city, or on a %s"
		" after %d turn.\n",
		"* Unit has to be in a city, or on a %s"
		" after %d turns.\n", utype->fuel),
	    astr.str, utype->fuel);
    astr_free(&astr);
  }
  if (strlen(buf) > 0) {
    sprintf(buf + strlen(buf), "\n");
  } 
  if (utype->helptext && utype->helptext[0] != '\0') {
    sprintf(buf + strlen(buf), "%s\n\n", _(utype->helptext));
  }
  strcpy(buf + strlen(buf), user_text);
  wordwrap_string(buf, 68);
}

/****************************************************************
  Append misc dynamic text for techs.
*****************************************************************/
void helptext_tech(char *buf, int i, const char *user_text)
{
  assert(buf&&user_text);
  strcpy(buf, user_text);

  if (get_invention(game.player_ptr, i) != TECH_KNOWN) {
    if (get_invention(game.player_ptr, i) == TECH_REACHABLE) {
      sprintf(buf + strlen(buf),
	      _("If we would now start with %s we would need %d bulbs."),
	      get_tech_name(game.player_ptr, i),
	      base_total_bulbs_required(game.player_ptr, i));
    } else if (tech_is_available(game.player_ptr, i)) {
      sprintf(buf + strlen(buf),
	      _("To reach %s we need to obtain %d other "
		"technologies first. The whole project "
		"will require %d bulbs to complete."),
	      get_tech_name(game.player_ptr, i),
	      num_unknown_techs_for_goal(game.player_ptr, i) - 1,
	      total_bulbs_required_for_goal(game.player_ptr, i));
    } else {
      sprintf(buf + strlen(buf),
	      _("You cannot research this technology."));
    }
    if (!techs_have_fixed_costs() && tech_is_available(game.player_ptr, i)) {
      sprintf(buf + strlen(buf),
	      _(" This number may vary depending on what "
		"other players will research.\n"));
    } else {
      sprintf(buf + strlen(buf), "\n");
    }
  }

  government_iterate(g) {
    if (g->required_tech == i) {
      sprintf(buf + strlen(buf), _("* Allows changing government to %s.\n"),
	      g->name);
    }
  } government_iterate_end;
  if (tech_flag(i, TF_BONUS_TECH)) {
    sprintf(buf + strlen(buf), _("* The first player to research %s gets "
				 "an immediate advance.\n"),
	    get_tech_name(game.player_ptr, i));
  }
  if (tech_flag(i, TF_BOAT_FAST))
    sprintf(buf + strlen(buf), _("* Gives sea units one extra move.\n"));
  if (tech_flag(i, TF_REDUCE_TRIREME_LOSS1))
    sprintf(buf + strlen(buf), _("* Reduces the chance of losing boats "
				 "on the high seas to 25%%.\n"));
  if (tech_flag(i, TF_REDUCE_TRIREME_LOSS2))
    sprintf(buf + strlen(buf), _("* Reduces the chance of losing boats "
				 "on the high seas to 12%%.\n"));
  if (tech_flag(i, TF_POPULATION_POLLUTION_INC))
    sprintf(buf + strlen(buf), _("* Increases the pollution generated by "
				 "the population.\n"));
  if (game.rtech.cathedral_plus == i)
    sprintf(buf + strlen(buf), _("* Improves the effect of Cathedrals.\n"));
  if (game.rtech.cathedral_minus == i)
    sprintf(buf + strlen(buf), _("* Reduces the effect of Cathedrals.\n"));
  if (game.rtech.colosseum_plus == i)
    sprintf(buf + strlen(buf), _("* Improves the effect of Colosseums.\n"));
  if (game.rtech.temple_plus == i)
    sprintf(buf + strlen(buf), _("* Improves the effect of Temples.\n"));

  if (tech_flag(i, TF_BRIDGE)) {
    const char *units_str = get_units_with_flag_string(F_SETTLERS);
    sprintf(buf + strlen(buf), _("* Allows %s to build roads on river "
				 "squares.\n"), units_str);
    free((void *) units_str);
  }

  if (tech_flag(i, TF_FORTRESS)) {
    const char *units_str = get_units_with_flag_string(F_SETTLERS);
    sprintf(buf + strlen(buf), _("* Allows %s to build fortresses.\n"),
	    units_str);
    free((void *) units_str);
  }

  if (tech_flag(i, TF_AIRBASE)) {
    const char *units_str = get_units_with_flag_string(F_AIRBASE);
    if (units_str) {
      sprintf(buf + strlen(buf), _("* Allows %s to build airbases.\n"),
	      units_str);
      free((void *) units_str);
    }
  }

  if (tech_flag(i, TF_RAILROAD)) {
    const char *units_str = get_units_with_flag_string(F_SETTLERS);
    sprintf(buf + strlen(buf),
	    _("* Allows %s to upgrade roads to railroads.\n"), units_str);
    free((void *) units_str);
  }

  if (tech_flag(i, TF_FARMLAND)) {
    const char *units_str = get_units_with_flag_string(F_SETTLERS);
    sprintf(buf + strlen(buf),
	    _("* Allows %s to upgrade irrigation to farmland.\n"),
	    units_str);
    free((void *) units_str);
  }
  if (advances[i].helptext && advances[i].helptext[0] != '\0') {
    if (strlen(buf) > 0) {
      sprintf(buf + strlen(buf), "\n");
    }
    sprintf(buf + strlen(buf), "%s\n", _(advances[i].helptext));
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
  pt = get_tile_type(i);

  if (terrain_has_flag(i, TER_NO_POLLUTION)) {
    sprintf(buf + strlen(buf),
	    _("* Pollution cannot be generated on this terrain."));
    strcat(buf, "\n");
  }
  if (terrain_has_flag(i, TER_NO_CITIES)) {
    sprintf(buf + strlen(buf),
	    _("* You cannot build cities on this terrain."));
    strcat(buf, "\n");
  }
  if (terrain_has_flag(i, TER_UNSAFE_COAST)
      && !is_ocean(i)) {
    sprintf(buf + strlen(buf),
	    _("* The coastline of this terrain is unsafe."));
    strcat(buf, "\n");
  }
  if (terrain_has_flag(i, TER_UNSAFE)) {
    sprintf(buf + strlen(buf),
	    _("* This terrain is unsafe for units to travel on."));
    strcat(buf, "\n");
  }
  if (terrain_has_flag(i, TER_OCEANIC)) {
    sprintf(buf + strlen(buf),
	    _("* Land units cannot travel on oceanic terrains."));
    strcat(buf, "\n");
  }

  if (pt->helptext[0] != '\0') {
    if (buf[0] != '\0') {
      strcat(buf, "\n");
    }
    sprintf(buf + strlen(buf), "%s", _(pt->helptext));
  }
  if (user_text && user_text[0] != '\0') {
    strcat(buf, "\n\n");
    strcat(buf, user_text);
  }
  wordwrap_string(buf, 68);
}

/****************************************************************
  Append text for government.
*****************************************************************/
void helptext_government(char *buf, int i, const char *user_text)
{
  struct government *gov = get_government(i);
  
  buf[0] = '\0';
  
  if (gov->helptext[0] != '\0') {
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

  if (utype->shield_cost > 0 || utype->food_cost > 0
      || utype->gold_cost > 0 || utype->happy_cost > 0) {
    int any = 0;
    buf[0] = '\0';
    if (utype->shield_cost > 0) {
      sprintf(buf+strlen(buf), _("%s%d shield"),
	      (any > 0 ? ", " : ""), utype->shield_cost);
      any++;
    }
    if (utype->food_cost > 0) {
      sprintf(buf+strlen(buf), _("%s%d food"),
	      (any > 0 ? ", " : ""), utype->food_cost);
      any++;
    }
    if (utype->happy_cost > 0) {
      sprintf(buf+strlen(buf), _("%s%d unhappy"),
	      (any > 0 ? ", " : ""), utype->happy_cost);
      any++;
    }
    if (utype->gold_cost > 0) {
      sprintf(buf+strlen(buf), _("%s%d gold"),
	      (any > 0 ? ", " : ""), utype->gold_cost);
      any++;
    }
  } else {
    /* strcpy(buf, _("None")); */
    sprintf(buf, "%d", 0);
  }
  return buf;
}
