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
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "capability.h"
#include "city.h"
#include "fcintl.h"
#include "game.h"
#include "idex.h"
#include "log.h"
#include "map.h"
#include "mem.h"
#include "rand.h"
#include "registry.h"
#include "shared.h"
#include "support.h"
#include "unit.h"
#include "version.h"

#include "citytools.h"
#include "cityhand.h"
#include "cityturn.h"
#include "mapgen.h"
#include "maphand.h"
#include "meta.h"
#include "ruleset.h"
#include "spacerace.h"
#include "srv_main.h"

#include "aicity.h"
#include "aiunit.h"

#include "savegame.h"


static char dec2hex[] = "0123456789abcdef";
static char terrain_chars[] = "adfghjm prstu";

/* Following does not include "unirandom", used previously; add it if
 * appropriate.  (Code no longer looks at "unirandom", but should still
 * include it when appropriate for maximum savegame compatibility.)
 */
#define SAVEFILE_OPTIONS "1.7 startoptions spacerace2 rulesets" \
" diplchance_percent worklists2"


/***************************************************************
load starting positions for the players from a savegame file
Now we don't know how many start positions there are nor how many
should be because rulesets are loaded later. So try to load as
many as they are; there should be at least enough for every
player.  This could be changed/improved in future.
***************************************************************/
static void map_startpos_load(struct section_file *file)
{
  int i=0, pos;

  while( (pos = secfile_lookup_int_default(file, -1, "map.r%dsx", i)) != -1) {
    map.start_positions[i].x = pos;
    map.start_positions[i].y = secfile_lookup_int(file, "map.r%dsy", i);
    i++;
  }

  if (i < game.max_players) {
    freelog(LOG_VERBOSE,
	    _("Number of starts (%d) are lower than max_players (%d),"
	      " lowering max_players."),
 	    i, game.max_players);
    game.max_players = i;
  }

  map.num_start_positions = i;
}

/***************************************************************
load the tile map from a savegame file
***************************************************************/
static void map_tiles_load(struct section_file *file)
{
  int x, y;

  map.is_earth=secfile_lookup_int(file, "map.is_earth");

  /* In some cases we read these before, but not always, and
   * its safe to read them again:
   */
  map.xsize=secfile_lookup_int(file, "map.width");
  map.ysize=secfile_lookup_int(file, "map.height");

  map_allocate();

  /* get the terrain type */
  for(y=0; y<map.ysize; y++) {
    char *terline=secfile_lookup_str(file, "map.t%03d", y);
    for(x=0; x<map.xsize; x++) {
      char *pch;
      if(!(pch=strchr(terrain_chars, terline[x]))) {
 	freelog(LOG_FATAL, "unknown terrain type (map.t) in map "
		"at position (%d,%d): %d '%c'", x, y, terline[x], terline[x]);
	exit(1);
      }
      map_get_tile(x, y)->terrain=pch-terrain_chars;
    }
  }

  assign_continent_numbers();
}

/***************************************************************
load the rivers overlay map from a savegame file

(This does not need to be called from map_load(), because
 map_load() loads the rivers overlay along with the rest of
 the specials.  Call this only if you've already called
 map_tiles_load(), and want to overlay rivers defined as
 specials, rather than as terrain types.)
***************************************************************/
static void map_rivers_overlay_load(struct section_file *file)
{
  int x, y;

  /* get "next" 4 bits of special flags;
     extract the rivers overlay from them */
  for(y=0; y<map.ysize; y++) {
    char *terline=secfile_lookup_str_default(file, NULL, "map.n%03d", y);

    if (terline) {
      for(x=0; x<map.xsize; x++) {
	char ch=terline[x];

	if(isxdigit(ch)) {
	  map_get_tile(x, y)->special |=
	    ((ch-(isdigit(ch) ? '0' : 'a'-10))<<8) & S_RIVER;
	} else if(ch!=' ') {
	  freelog(LOG_FATAL, "unknown rivers overlay flag (map.n) in map "
		  "at position(%d,%d): %d '%c'", x, y, ch, ch);
	  exit(1);
	}
      }
    }
  }
  map.have_rivers_overlay = 1;
}

/***************************************************************
  Save the rivers overlay map; this is a special case to allow
  re-saving scenarios which have rivers overlay data.  This
  only applies if don't have rest of specials.
***************************************************************/
static void map_rivers_overlay_save(struct section_file *file)
{
  char *pbuf = fc_malloc(map.xsize+1);
  int x, y;
  
  /* put "next" 4 bits of special flags */
  for(y=0; y<map.ysize; y++) {
    for(x=0; x<map.xsize; x++) {
      pbuf[x]=dec2hex[(map_get_tile(x, y)->special&0xf00)>>8];
    }
    pbuf[x]='\0';
    secfile_insert_str(file, pbuf, "map.n%03d", y);
  }
  free(pbuf);
}

/***************************************************************
load a complete map from a savegame file
***************************************************************/
static void map_load(struct section_file *file)
{
  int x ,y;

  /* map_init();
   * This is already called in game_init(), and calling it
   * here stomps on map.huts etc.  --dwp
   */

  map_tiles_load(file);
  map_startpos_load(file);

  /* get lower 4 bits of special flags */
  for(y=0; y<map.ysize; y++) {
    char *terline=secfile_lookup_str(file, "map.l%03d", y);

    for(x=0; x<map.xsize; x++) {
      char ch=terline[x];

      if(isxdigit(ch)) {
	map_get_tile(x, y)->special=ch-(isdigit(ch) ? '0' : ('a'-10));
      } else if(ch!=' ') {
 	freelog(LOG_FATAL, "unknown special flag(lower) (map.l) in map "
 	    "at position(%d,%d): %d '%c'", x, y, ch, ch);
	exit(1);
      }
      else
	map_get_tile(x, y)->special=S_NO_SPECIAL;
    }
  }

  /* get upper 4 bits of special flags */
  for(y=0; y<map.ysize; y++) {
    char *terline=secfile_lookup_str(file, "map.u%03d", y);

    for(x=0; x<map.xsize; x++) {
      char ch=terline[x];

      if(isxdigit(ch)) {
	map_get_tile(x, y)->special|=(ch-(isdigit(ch) ? '0' : 'a'-10))<<4;
      } else if(ch!=' ') {
 	freelog(LOG_FATAL, "unknown special flag(upper) (map.u) in map "
		"at position(%d,%d): %d '%c'", x, y, ch, ch);
	exit(1);
      }
    }
  }

  /* get "next" 4 bits of special flags */
  for(y=0; y<map.ysize; y++) {
    char *terline=secfile_lookup_str_default(file, NULL, "map.n%03d", y);

    if (terline) {
      for(x=0; x<map.xsize; x++) {
	char ch=terline[x];

	if(isxdigit(ch)) {
	  map_get_tile(x, y)->special|=(ch-(isdigit(ch) ? '0' : 'a'-10))<<8;
	} else if(ch!=' ') {
	  freelog(LOG_FATAL, "unknown special flag(next) (map.n) in map "
		  "at position(%d,%d): %d '%c'", x, y, ch, ch);
	  exit(1);
	}
      }
    }
  }

  /* get "final" 4 bits of special flags */
  for(y=0; y<map.ysize; y++) {
    char *terline=secfile_lookup_str_default(file, NULL, "map.f%03d", y);

    if (terline) {
      for(x=0; x<map.xsize; x++) {
	char ch=terline[x];

	if(isxdigit(ch)) {
	  map_get_tile(x, y)->special|=(ch-(isdigit(ch) ? '0' : 'a'-10))<<12;
	} else if(ch!=' ') {
	  freelog(LOG_FATAL, "unknown special flag(final) (map.f) in map "
		  "at position(%d,%d): %d '%c'", x, y, ch, ch);
	  exit(1);
	}
      }
    }
  }

  /* get bits 0-3 of known flags */
  for(y=0; y<map.ysize; y++) {
    char *terline=secfile_lookup_str(file, "map.a%03d", y);
    for(x=0; x<map.xsize; x++) {
      char ch=terline[x];
      map_get_tile(x,y)->sent=0;
      if(isxdigit(ch)) {
	map_get_tile(x, y)->known=ch-(isdigit(ch) ? '0' : ('a'-10));
      } else if(ch!=' ') {
 	freelog(LOG_FATAL, "unknown known flag (map.a) in map "
		"at position(%d,%d): %d '%c'", x, y, ch, ch);
	exit(1);
      }
      else
	map_get_tile(x, y)->known=0;
    }
  }

  /* get bits 4-7 of known flags */
  for(y=0; y<map.ysize; y++) {
    char *terline=secfile_lookup_str(file, "map.b%03d", y);
    for(x=0; x<map.xsize; x++) {
      char ch=terline[x];
      if(isxdigit(ch)) {
	map_get_tile(x, y)->known|=(ch-(isdigit(ch) ? '0' : 'a'-10))<<4;
      } else if(ch!=' ') {
 	freelog(LOG_FATAL, "unknown known flag (map.b) in map "
		"at position(%d,%d): %d '%c'", x, y, ch, ch);
	exit(1);
      }
    }
  }

  /* get bits 8-11 of known flags */
  for(y=0; y<map.ysize; y++) {
    char *terline=secfile_lookup_str(file, "map.c%03d", y);
    for(x=0; x<map.xsize; x++) {
      char ch=terline[x];
      if(isxdigit(ch)) {
	map_get_tile(x, y)->known|=(ch-(isdigit(ch) ? '0' : 'a'-10))<<8;
      } else if(ch!=' ') {
 	freelog(LOG_FATAL, "unknown known flag (map.c) in map "
		"at position(%d,%d): %d '%c'", x, y, ch, ch);
	exit(1);
      }
    }
  }

  /* get bits 12-15 of known flags */
  for(y=0; y<map.ysize; y++) {
    char *terline=secfile_lookup_str(file, "map.d%03d", y);
    for(x=0; x<map.xsize; x++) {
      char ch=terline[x];

      if(isxdigit(ch)) {
	map_get_tile(x, y)->known|=(ch-(isdigit(ch) ? '0' : 'a'-10))<<12;
      } else if(ch!=' ') {
 	freelog(LOG_FATAL, "unknown known flag (map.d) in map "
		"at position(%d,%d): %d '%c'", x, y, ch, ch);
	exit(1);
      }
    }
  }
  map.have_specials = 1;

  /* Should be handled as part of send_all_know_tiles,
     but do it here too for safety */
  for(y=0; y<map.ysize; y++)
    for(x=0; x<map.xsize; x++)
      map_get_tile(x,y)->sent = 0;
}

/***************************************************************
...
***************************************************************/
static void map_save(struct section_file *file)
{
  int i, x, y;
  char *pbuf=fc_malloc(map.xsize+1);

  /* map.xsize and map.ysize (saved as map.width and map.height)
   * are now always saved in game_save()
   */
  secfile_insert_int(file, map.is_earth, "map.is_earth");

  for(i=0; i<map.num_start_positions; i++) {
    secfile_insert_int(file, map.start_positions[i].x, "map.r%dsx", i);
    secfile_insert_int(file, map.start_positions[i].y, "map.r%dsy", i);
  }
    
  /* put the terrain type */
  for(y=0; y<map.ysize; y++) {
    for(x=0; x<map.xsize; x++)
      pbuf[x]=terrain_chars[map_get_tile(x, y)->terrain];
    pbuf[x]='\0';

    secfile_insert_str(file, pbuf, "map.t%03d", y);
  }

  if (!map.have_specials) {
    if (map.have_rivers_overlay) {
      map_rivers_overlay_save(file);
    }
    free(pbuf);
    return;
  }

  /* put lower 4 bits of special flags */
  for(y=0; y<map.ysize; y++) {
    for(x=0; x<map.xsize; x++)
      pbuf[x]=dec2hex[map_get_tile(x, y)->special&0xf];
    pbuf[x]='\0';

    secfile_insert_str(file, pbuf, "map.l%03d", y);
  }

  /* put upper 4 bits of special flags */
  for(y=0; y<map.ysize; y++) {
    for(x=0; x<map.xsize; x++)
      pbuf[x]=dec2hex[(map_get_tile(x, y)->special&0xf0)>>4];
    pbuf[x]='\0';

    secfile_insert_str(file, pbuf, "map.u%03d", y);
  }

  /* put "next" 4 bits of special flags */
  for(y=0; y<map.ysize; y++) {
    for(x=0; x<map.xsize; x++)
      pbuf[x]=dec2hex[(map_get_tile(x, y)->special&0xf00)>>8];
    pbuf[x]='\0';

    secfile_insert_str(file, pbuf, "map.n%03d", y);
  }

  /* put "final" 4 bits of special flags */
  for(y=0; y<map.ysize; y++) {
    for(x=0; x<map.xsize; x++)
      pbuf[x]=dec2hex[(map_get_tile(x, y)->special&0xf000)>>12];
    pbuf[x]='\0';

    secfile_insert_str(file, pbuf, "map.f%03d", y);
  }

  /* put bit 0-3 of known bits */
  for(y=0; y<map.ysize; y++) {
    for(x=0; x<map.xsize; x++)
      pbuf[x]=dec2hex[(map_get_tile(x, y)->known&0xf)];
    pbuf[x]='\0';
    secfile_insert_str(file, pbuf, "map.a%03d", y);
  }

  /* put bit 4-7 of known bits */
  for(y=0; y<map.ysize; y++) {
    for(x=0; x<map.xsize; x++)
      pbuf[x]=dec2hex[((map_get_tile(x, y)->known&0xf0))>>4];
    pbuf[x]='\0';
    secfile_insert_str(file, pbuf, "map.b%03d", y);
  }

  /* put bit 8-11 of known bits */
  for(y=0; y<map.ysize; y++) {
    for(x=0; x<map.xsize; x++)
      pbuf[x]=dec2hex[((map_get_tile(x, y)->known&0xf00))>>8];
    pbuf[x]='\0';
    secfile_insert_str(file, pbuf, "map.c%03d", y);
  }

  /* put bit 12-15 of known bits */
  for(y=0; y<map.ysize; y++) {
    for(x=0; x<map.xsize; x++)
      pbuf[x]=dec2hex[((map_get_tile(x, y)->known&0xf000))>>12];
    pbuf[x]='\0';
    secfile_insert_str(file, pbuf, "map.d%03d", y);
  }
  
  free(pbuf);
}

/***************************************************************
Load the worklist elements specified by path, given the arguments
plrno and wlinx, into the worklist pointed to by pwl.
***************************************************************/
static void worklist_load(struct section_file *file,
			  char *path, int plrno, int wlinx,
			  struct worklist *pwl)
{
  char efpath[64];
  char idpath[64];
  int i, end = 0;

  sz_strlcpy(efpath, path);
  sz_strlcat(efpath, ".wlef%d");
  sz_strlcpy(idpath, path);
  sz_strlcat(idpath, ".wlid%d");

  for (i = 0; i < MAX_LEN_WORKLIST; i++) {
    if (end) {
      pwl->wlefs[i] = WEF_END;
      pwl->wlids[i] = 0;
      section_file_lookup(file, efpath, plrno, wlinx, i);
      section_file_lookup(file, idpath, plrno, wlinx, i);
    } else {
      pwl->wlefs[i] =
	secfile_lookup_int_default(file, WEF_END, efpath, plrno, wlinx, i);
      pwl->wlids[i] =
	secfile_lookup_int_default(file, 0, idpath, plrno, wlinx, i);

      if ((pwl->wlefs[i] <= WEF_END) || (pwl->wlefs[i] >= WEF_LAST) ||
	  ((pwl->wlefs[i] == WEF_UNIT) && !unit_type_exists(pwl->wlids[i])) ||
	  ((pwl->wlefs[i] == WEF_IMPR) && !improvement_exists(pwl->wlids[i]))) {
	pwl->wlefs[i] = WEF_END;
	pwl->wlids[i] = 0;
	end = 1;
      }
    }
  }
}

/***************************************************************
Load the worklist elements specified by path, given the arguments
plrno and wlinx, into the worklist pointed to by pwl.
Assumes original save-file format.  Use for backward compatibility.
***************************************************************/
static void worklist_load_old(struct section_file *file,
			      char *path, int plrno, int wlinx,
			      struct worklist *pwl)
{
  int i, id, end = 0;

  for (i = 0; i < MAX_LEN_WORKLIST; i++) {
    if (end) {
      pwl->wlefs[i] = WEF_END;
      pwl->wlids[i] = 0;
      section_file_lookup(file, path, plrno, wlinx, i);
    } else {
      id = secfile_lookup_int_default(file, -1, path, plrno, wlinx, i);

      if ((id < 0) || (id >= 284)) {	/* 284 was flag value for end of list */
	pwl->wlefs[i] = WEF_END;
	pwl->wlids[i] = 0;
	end = 1;
      } else if (id >= 68) {		/* 68 was offset to unit ids */
	pwl->wlefs[i] = WEF_UNIT;
	pwl->wlids[i] = id - 68;
	end = !unit_type_exists(pwl->wlids[i]);
      } else {				/* must be an improvement id */
	pwl->wlefs[i] = WEF_IMPR;
	pwl->wlids[i] = id;
	end = !improvement_exists(pwl->wlids[i]);
      }
    }
  }
}

/***************************************************************
...
***************************************************************/
static void player_load(struct player *plr, int plrno,
			struct section_file *file)
{
  int i, j, x, y, nunits, ncities;
  char *p;
  char *savefile_options = " ";

  player_map_allocate(plr);

  if ((game.version == 10604 && section_file_lookup(file,"savefile.options"))
      || (game.version > 10604))
    savefile_options = secfile_lookup_str(file,"savefile.options");  
  /* else leave savefile_options empty */

  /* Note -- as of v1.6.4 you should use savefile_options (instead of
     game.version) to determine which variables you can expect to 
     find in a savegame file.  Or even better (IMO --dwp) is to use
     section_file_lookup(), secfile_lookup_int_default(),
     secfile_lookup_str_default(), etc.
  */

  plr->ai.is_barbarian = secfile_lookup_int_default(file, 0, "player%d.ai.is_barbarian",
                                                    plrno);
  if (is_barbarian(plr)) game.nbarbarians++;

  sz_strlcpy(plr->name, secfile_lookup_str(file, "player%d.name", plrno));
  sz_strlcpy(plr->username,
	     secfile_lookup_str_default(file, "", "player%d.username", plrno));
  plr->nation=secfile_lookup_int(file, "player%d.race", plrno);
  if (plr->ai.is_barbarian) {
    plr->nation=game.nation_count-1;
  }
  plr->government=secfile_lookup_int(file, "player%d.government", plrno);
  plr->embassy=secfile_lookup_int(file, "player%d.embassy", plrno);
  plr->city_style=secfile_lookup_int_default(file, get_nation_city_style(plr->nation),
                                             "player%d.city_style", plrno);

  for (i = 0; i < MAX_NUM_WORKLISTS; i++) {
    plr->worklists[i].is_valid =
      secfile_lookup_int_default(file, 0,
				 "player%d.worklist%d.is_valid", plrno, i);
    strcpy(plr->worklists[i].name,
	   secfile_lookup_str_default(file, "",
				      "player%d.worklist%d.name", plrno, i));
    if (has_capability("worklists2", savefile_options)) {
      worklist_load(file, "player%d.worklist%d", plrno, i, &(plr->worklists[i]));
    } else {
      worklist_load_old(file, "player%d.worklist%d.ids%d",
			plrno, i, &(plr->worklists[i]));
    }
  }

  plr->nturns_idle=0;
  plr->is_male=secfile_lookup_int_default(file, 1, "player%d.is_male", plrno);
  plr->is_alive=secfile_lookup_int(file, "player%d.is_alive", plrno);
  plr->ai.control = secfile_lookup_int(file, "player%d.ai.control", plrno);
  plr->ai.tech_goal = secfile_lookup_int(file, "player%d.ai.tech_goal", plrno);
  plr->ai.handicap = 0;		/* set later */
  plr->ai.fuzzy = 0;		/* set later */
  plr->ai.expand = 100;		/* set later */
  plr->ai.skill_level =
    secfile_lookup_int_default(file, game.skill_level,
			       "player%d.ai.skill_level", plrno);
  if (plr->ai.control && plr->ai.skill_level==0) {
    plr->ai.skill_level = GAME_OLD_DEFAULT_SKILL_LEVEL;
  }

  plr->economic.gold=secfile_lookup_int(file, "player%d.gold", plrno);
  plr->economic.tax=secfile_lookup_int(file, "player%d.tax", plrno);
  plr->economic.science=secfile_lookup_int(file, "player%d.science", plrno);
  plr->economic.luxury=secfile_lookup_int(file, "player%d.luxury", plrno);

  plr->future_tech=secfile_lookup_int(file, "player%d.futuretech", plrno);

  plr->research.researched=secfile_lookup_int(file, 
					     "player%d.researched", plrno);
  plr->research.researchpoints=secfile_lookup_int(file, 
					     "player%d.researchpoints", plrno);
  plr->research.researching=secfile_lookup_int(file, 
					     "player%d.researching", plrno);

  p=secfile_lookup_str(file, "player%d.invs", plrno);
    
  plr->capital=secfile_lookup_int(file, "player%d.capital", plrno);
  plr->revolution=secfile_lookup_int_default(file, 0, "player%d.revolution",
                                             plrno);

  for(i=0; i<game.num_tech_types; i++)
    set_invention(plr, i, (p[i]=='1') ? TECH_KNOWN : TECH_UNKNOWN);

  plr->reputation=secfile_lookup_int_default(file, GAME_DEFAULT_REPUTATION,
					     "player%d.reputation", plrno);
  for(i=0; i<MAX_NUM_PLAYERS+MAX_NUM_BARBARIANS; i++) {
    plr->diplstates[i].type = 
      secfile_lookup_int_default(file, DS_NEUTRAL,
				 "player%d.diplstate%d.type", plrno, i);
    plr->diplstates[i].turns_left = 
      secfile_lookup_int_default(file, 0,
				 "player%d.diplstate%d.turns_left", plrno, i);
    plr->diplstates[i].has_reason_to_cancel = 
      secfile_lookup_int_default(file, 0,
				 "player%d.diplstate%d.has_reason_to_cancel",
				 plrno, i);
  }
  
  if (has_capability("spacerace", savefile_options)) {
    struct player_spaceship *ship = &plr->spaceship;
    char prefix[32];
    int arrival_year;
    
    my_snprintf(prefix, sizeof(prefix), "player%d.spaceship", plrno);
    spaceship_init(ship);
    arrival_year = secfile_lookup_int(file, "%s.arrival_year", prefix);
    ship->structurals = secfile_lookup_int(file, "%s.structurals", prefix);
    ship->components = secfile_lookup_int(file, "%s.components", prefix);
    ship->modules = secfile_lookup_int(file, "%s.modules", prefix);
    ship->state = secfile_lookup_int(file, "%s.state", prefix);
    if (ship->state >= SSHIP_LAUNCHED) {
      ship->launch_year = (arrival_year - 15);
    }
    /* auto-assign to individual parts: */
    ship->habitation = (ship->modules + 2)/3;
    ship->life_support = (ship->modules + 1)/3;
    ship->solar_panels = ship->modules/3;
    ship->fuel = (ship->components + 1)/2;
    ship->propulsion = ship->components/2;
    for(i=0; i<ship->structurals; i++) {
      ship->structure[i] = 1;
    }
    spaceship_calc_derived(ship);
  }
  else if(has_capability("spacerace2", savefile_options)) {
    struct player_spaceship *ship = &plr->spaceship;
    char prefix[32];
    char *st;
    
    my_snprintf(prefix, sizeof(prefix), "player%d.spaceship", plrno);
    spaceship_init(ship);
    ship->state = secfile_lookup_int(file, "%s.state", prefix);

    if (ship->state != SSHIP_NONE) {
      ship->structurals = secfile_lookup_int(file, "%s.structurals", prefix);
      ship->components = secfile_lookup_int(file, "%s.components", prefix);
      ship->modules = secfile_lookup_int(file, "%s.modules", prefix);
      ship->fuel = secfile_lookup_int(file, "%s.fuel", prefix);
      ship->propulsion = secfile_lookup_int(file, "%s.propulsion", prefix);
      ship->habitation = secfile_lookup_int(file, "%s.habitation", prefix);
      ship->life_support = secfile_lookup_int(file, "%s.life_support", prefix);
      ship->solar_panels = secfile_lookup_int(file, "%s.solar_panels", prefix);
      st = secfile_lookup_str(file, "%s.structure", prefix);
      for(i=0; i<NUM_SS_STRUCTURALS && *st; i++, st++) {
	ship->structure[i] = ((*st) == '1');
      }
      if (ship->state >= SSHIP_LAUNCHED) {
	ship->launch_year = secfile_lookup_int(file, "%s.launch_year", prefix);
      }
      spaceship_calc_derived(ship);
    }
  }

  city_list_init(&plr->cities);
  ncities=secfile_lookup_int(file, "player%d.ncities", plrno);
  
  for(i=0; i<ncities; i++) { /* read the cities */
    struct city *pcity;
    
    pcity=fc_malloc(sizeof(struct city));
    pcity->ai.ai_role = AICITY_NONE;
    pcity->ai.trade_want = TRADE_WEIGHTING;
    memset(pcity->ai.building_want, 0, sizeof(pcity->ai.building_want));
    pcity->ai.workremain = 1; /* there's always work to be done! */
    pcity->ai.danger = -1; /* flag, may come in handy later */
    pcity->corruption = 0;
    pcity->shield_bonus = 100;
    pcity->tax_bonus = 100;
    pcity->science_bonus = 100;
 
    pcity->id=secfile_lookup_int(file, "player%d.c%d.id", plrno, i);
    alloc_id(pcity->id);
    idex_register_city(pcity);
    pcity->owner=plrno;
    pcity->x=secfile_lookup_int(file, "player%d.c%d.x", plrno, i);
    pcity->y=secfile_lookup_int(file, "player%d.c%d.y", plrno, i);
    
    sz_strlcpy(pcity->name,
	       secfile_lookup_str(file, "player%d.c%d.name", plrno, i));
    if (section_file_lookup(file, "player%d.c%d.original", plrno, i))
      pcity->original = secfile_lookup_int(file, "player%d.c%d.original", 
					   plrno,i);
    else 
      pcity->original = plrno;
    pcity->size=secfile_lookup_int(file, "player%d.c%d.size", plrno, i);

    pcity->steal=secfile_lookup_int(file, "player%d.c%d.steal", plrno, i);

    pcity->ppl_elvis=secfile_lookup_int(file, "player%d.c%d.nelvis", plrno, i);
    pcity->ppl_scientist=secfile_lookup_int(file, 
					  "player%d.c%d.nscientist", plrno, i);
    pcity->ppl_taxman=secfile_lookup_int(file, "player%d.c%d.ntaxman",
					 plrno, i);

    for(j=0; j<4; j++)
      pcity->trade[j]=secfile_lookup_int(file, "player%d.c%d.traderoute%d",
					 plrno, i, j);
    
    pcity->food_stock=secfile_lookup_int(file, "player%d.c%d.food_stock", 
						 plrno, i);
    pcity->shield_stock=secfile_lookup_int(file, "player%d.c%d.shield_stock", 
						   plrno, i);
    pcity->tile_trade=pcity->trade_prod=0;
    pcity->anarchy=secfile_lookup_int(file, "player%d.c%d.anarchy", plrno,i);
    pcity->rapture=secfile_lookup_int_default(file, 0, "player%d.c%d.rapture", plrno,i);
    pcity->was_happy=secfile_lookup_int(file, "player%d.c%d.was_happy", plrno,i);
    pcity->is_building_unit=
      secfile_lookup_int(file, 
			 "player%d.c%d.is_building_unit", plrno, i);
    pcity->currently_building=
      secfile_lookup_int(file, 
			 "player%d.c%d.currently_building", plrno, i);
    pcity->turn_last_built=
      secfile_lookup_int_default(file, GAME_START_YEAR,
				 "player%d.c%d.turn_last_built", plrno, i);
    pcity->turn_changed_target=
      secfile_lookup_int_default(file, GAME_START_YEAR,
				 "player%d.c%d.turn_changed_target", plrno, i);
    pcity->changed_from_id=
      secfile_lookup_int_default(file, pcity->currently_building,
				 "player%d.c%d.changed_from_id", plrno, i);
    pcity->changed_from_is_unit=
      secfile_lookup_int_default(file, pcity->is_building_unit,
				 "player%d.c%d.changed_from_is_unit", plrno, i);
    pcity->before_change_shields=
      secfile_lookup_int_default(file, pcity->shield_stock,
				 "player%d.c%d.before_change_shields", plrno, i);

    pcity->did_buy=secfile_lookup_int(file,
				      "player%d.c%d.did_buy", plrno,i);
    pcity->did_sell =
      secfile_lookup_int_default(file, 0, "player%d.c%d.did_sell", plrno,i);
    
    if (game.version >=10300) 
      pcity->airlift=secfile_lookup_int(file,
					"player%d.c%d.airlift", plrno,i);
    else
      pcity->airlift=0;

    pcity->city_options =
      secfile_lookup_int_default(file, CITYOPT_DEFAULT,
				 "player%d.c%d.options", plrno, i);
    
    /* adding the cities contribution to fog-of-war */
    map_unfog_pseudo_city_area(&game.players[plrno],pcity->x,pcity->y);

    unit_list_init(&pcity->units_supported);

    /* Initialize pcity->city_map[][], using set_worker_city() so that
       ptile->worked gets initialized correctly.  The pre-initialisation
       to C_TILE_EMPTY is necessary because set_worker_city() accesses
       the existing value to possibly adjust ptile->worked, so need to
       initialize a non-worked value so ptile->worked (possibly already
       set from neighbouring city) does not get unset for C_TILE_EMPTY
       or C_TILE_UNAVAILABLE here.  -- dwp
    */
    p=secfile_lookup_str(file, "player%d.c%d.workers", plrno, i);
    for(y=0; y<CITY_MAP_SIZE; y++) {
      for(x=0; x<CITY_MAP_SIZE; x++) {
	pcity->city_map[x][y] = C_TILE_EMPTY;
        if (*p=='0') {
	  set_worker_city(pcity, x, y, C_TILE_EMPTY);
	} else if (*p=='1') {
	  if (map_get_tile(pcity->x+x-2, pcity->y+y-2)->worked) {
	    /* oops, inconsistent savegame; minimal fix: */
	    freelog(LOG_VERBOSE, "Inconsistent worked for %s (%d,%d), "
		    "converting to elvis", pcity->name, x, y);
	    pcity->ppl_elvis++;
	    set_worker_city(pcity, x, y, C_TILE_UNAVAILABLE);
	  } else {
	    set_worker_city(pcity, x, y, C_TILE_WORKER);
	  }
	} else {
	  set_worker_city(pcity, x, y, C_TILE_UNAVAILABLE);
	}
        p++;
      }
    }

    p=secfile_lookup_str(file, "player%d.c%d.improvements", plrno, i);
    for(x=0; x<game.num_impr_types; x++) {
      if (*p) {
	pcity->improvements[x]=(*p++=='1') ? 1 : 0;
      } else {
	pcity->improvements[x]=0;
      }
    }

    pcity->worklist = create_worklist();
    if (has_capability("worklists2", savefile_options)) {
      worklist_load(file, "player%d.c%d", plrno, i, pcity->worklist);
    } else {
      worklist_load_old(file, "player%d.c%d.worklist%d",
			plrno, i, pcity->worklist);
    }

    map_set_city(pcity->x, pcity->y, pcity);

    pcity->incite_revolt_cost = -1; /* flag value */

    city_list_insert_back(&plr->cities, pcity);
  }

  unit_list_init(&plr->units);
  nunits=secfile_lookup_int(file, "player%d.nunits", plrno);
  
  /* this should def. be placed in unit.c later - PU */
  for(i=0; i<nunits; i++) { /* read the units */
    struct unit *punit;
    struct city *pcity;
    
    punit=fc_malloc(sizeof(struct unit));
    punit->id=secfile_lookup_int(file, "player%d.u%d.id", plrno, i);
    alloc_id(punit->id);
    idex_register_unit(punit);
    punit->owner=plrno;
    punit->x=secfile_lookup_int(file, "player%d.u%d.x", plrno, i);
    punit->y=secfile_lookup_int(file, "player%d.u%d.y", plrno, i);

    punit->veteran=secfile_lookup_int(file, "player%d.u%d.veteran", plrno, i);
    punit->foul=secfile_lookup_int_default(file, 0, "player%d.u%d.foul",
					   plrno, i);
    punit->homecity=secfile_lookup_int(file, "player%d.u%d.homecity",
				       plrno, i);

    if((pcity=find_city_by_id(punit->homecity)))
      unit_list_insert(&pcity->units_supported, punit);
    else
      punit->homecity=0;
    
    punit->type=secfile_lookup_int(file, "player%d.u%d.type", plrno, i);

    punit->moves_left=secfile_lookup_int(file, "player%d.u%d.moves", plrno, i);
    punit->fuel= secfile_lookup_int(file, "player%d.u%d.fuel", plrno, i);
    set_unit_activity(punit, secfile_lookup_int(file, "player%d.u%d.activity",plrno, i));
/* need to do this to assign/deassign settlers correctly -- Syela */
/* was punit->activity=secfile_lookup_int(file, "player%d.u%d.activity",plrno, i); */
    punit->activity_count=secfile_lookup_int(file, 
					     "player%d.u%d.activity_count",
					     plrno, i);
    punit->activity_target=secfile_lookup_int_default(file, 0,
						"player%d.u%d.activity_target",
						plrno, i);

    punit->connecting=secfile_lookup_int_default(file, 0,
						"player%d.u%d.connecting",
						plrno, i);

    punit->goto_dest_x=secfile_lookup_int(file, 
					  "player%d.u%d.goto_x", plrno,i);
    punit->goto_dest_y=secfile_lookup_int(file, 
					  "player%d.u%d.goto_y", plrno,i);
    punit->ai.control=secfile_lookup_int(file, "player%d.u%d.ai", plrno,i);
    punit->ai.ai_role = AIUNIT_NONE;
    punit->ai.ferryboat = 0;
    punit->ai.passenger = 0;
    punit->ai.bodyguard = 0;
    punit->ai.charge = 0;
    punit->hp=secfile_lookup_int(file, "player%d.u%d.hp", plrno, i);
    punit->bribe_cost=-1;	/* flag value */
    
    punit->ord_map=secfile_lookup_int_default(file, 0, "player%d.u%d.ord_map",
					      plrno, i);
    punit->ord_city=secfile_lookup_int_default(file, 0, "player%d.u%d.ord_city",
					       plrno, i);
    punit->moved=secfile_lookup_int_default(file, 0, "player%d.u%d.moved",
					    plrno, i);
    punit->paradropped=secfile_lookup_int_default(file, 0, "player%d.u%d.paradropped",
                                                  plrno, i);
    punit->transported_by=secfile_lookup_int_default(file, -1, "player%d.u%d.transported_by",
                                                  plrno, i);
    /* Initialize upkeep values: these are hopefully initialized
       elsewhere before use (specifically, in city_support(); but
       fixme: check whether always correctly initialized?).
       Below is mainly for units which don't have homecity --
       otherwise these don't get initialized (and AI calculations
       etc may use junk values).
    */
    punit->unhappiness = 0;
    punit->upkeep      = 0;
    punit->upkeep_food = 0;
    punit->upkeep_gold = 0;
    
    /* allocate the unit's contribution to fog of war */
    unfog_area(&game.players[punit->owner],
	       punit->x,punit->y,get_unit_type(punit->type)->vision_range);

    unit_list_insert_back(&plr->units, punit);

    unit_list_insert(&map_get_tile(punit->x, punit->y)->units, punit);
  }
}

/********************************************************************** 
The private map for fog of war
***********************************************************************/
static void player_map_load(struct player *plr, int plrno,
			    struct section_file *file)
{
  int x,y,i;

  if (!plr->is_alive)
    for (x=0; x<map.xsize; x++)
      for (y=0; y<map.ysize; y++)
	map_change_seen(x, y, plrno, +1);

  /* load map if:
     1) it from a fog of war build
     2) fog of war was on (otherwise the private map wasn't saved)
     3) is not from a "unit only" fog of war save file
  */
  if (secfile_lookup_int_default(file, -1, "game.fogofwar") != -1
      && game.fogofwar == 1
      && secfile_lookup_int_default(file, -1,"player%d.total_ncities", plrno) != -1) {
    /* get the terrain type */
    for(y=0; y<map.ysize; y++) {
      char *terline=secfile_lookup_str(file, "player%d.map_t%03d", plrno, y);
      for(x=0; x<map.xsize; x++) {
	char *pch;
	if(!(pch=strchr(terrain_chars, terline[x]))) {
	  freelog(LOG_FATAL, "unknown terrain type (map.t) in map "
		  "at position (%d,%d): %d '%c'", x, y, terline[x], terline[x]);
	  exit(1);
	}
	map_get_player_tile(x, y, plrno)->terrain=pch-terrain_chars;
      }
    }
    
    /* get lower 4 bits of special flags */
    for(y=0; y<map.ysize; y++) {
      char *terline=secfile_lookup_str(file, "player%d.map_l%03d",plrno, y);
      
      for(x=0; x<map.xsize; x++) {
	char ch=terline[x];
	
	if(isxdigit(ch)) {
	  map_get_player_tile(x, y, plrno)->special=ch-(isdigit(ch) ? '0' : ('a'-10));
	} else if(ch!=' ') {
	  freelog(LOG_FATAL, "unknown special flag(lower) (map.l) in map "
		  "at position(%d,%d): %d '%c'", x, y, ch, ch);
	  exit(1);
	}
	else
	  map_get_player_tile(x, y, plrno)->special=S_NO_SPECIAL;
      }
    }
    
    /* get upper 4 bits of special flags */
    for(y=0; y<map.ysize; y++) {
      char *terline=secfile_lookup_str(file, "player%d.map_u%03d", plrno, y);
      
      for(x=0; x<map.xsize; x++) {
	char ch=terline[x];
	
	if(isxdigit(ch)) {
	  map_get_player_tile(x, y, plrno)->special|=(ch-(isdigit(ch) ? '0' : 'a'-10))<<4;
	} else if(ch!=' ') {
	  freelog(LOG_FATAL, "unknown special flag(upper) (map.u) in map "
		  "at position(%d,%d): %d '%c'", x, y, ch, ch);
	  exit(1);
	}
      }
    }
    
    /* get "next" 4 bits of special flags */
    for(y=0; y<map.ysize; y++) {
      char *terline=secfile_lookup_str_default(file, NULL, "player%d.map_n%03d", plrno, y);
      
      if (terline) {
	for(x=0; x<map.xsize; x++) {
	  char ch=terline[x];
	  
	  if(isxdigit(ch)) {
	    map_get_player_tile(x, y, plrno)->special|=(ch-(isdigit(ch) ? '0' : 'a'-10))<<8;
	  } else if(ch!=' ') {
	    freelog(LOG_FATAL, "unknown special flag(next) (map.n) in map "
		    "at position(%d,%d): %d '%c'", x, y, ch, ch);
	    exit(1);
	  }
	}
      }
    }
    
    
    /* get lower 4 bits of updated flags */
    for(y=0; y<map.ysize; y++) {
      char *terline=secfile_lookup_str(file, "player%d.map_ua%03d",plrno, y);
      
      for(x=0; x<map.xsize; x++) {
	char ch=terline[x];
	map_get_player_tile(x, y, plrno)->last_updated = ch-(isdigit(ch) ? '0' : ('a'-10));
      }
    }
    
    /* get upper 4 bits of updated flags */
    for(y=0; y<map.ysize; y++) {
      char *terline=secfile_lookup_str(file, "player%d.map_ub%03d", plrno, y);
      
      for(x=0; x<map.xsize; x++) {
	char ch=terline[x];
	map_get_player_tile(x, y, plrno)->last_updated |= (ch-(isdigit(ch) ? '0' : 'a'-10))<<4;
      }
    }
    
    /* get "next" 4 bits of updated flags */
    for(y=0; y<map.ysize; y++) {
      char *terline=secfile_lookup_str(file, "player%d.map_uc%03d", plrno, y);
      
      if (terline) {
	for(x=0; x<map.xsize; x++) {
	  char ch=terline[x];
	  map_get_player_tile(x, y, plrno)->last_updated |= (ch-(isdigit(ch) ? '0' : 'a'-10))<<8;
	}
      }
    }
    
    /* get "last" 4 bits of updated flags */
    for(y=0; y<map.ysize; y++) {
      char *terline=secfile_lookup_str(file, "player%d.map_ud%03d", plrno, y);
      
      if (terline) {
	for(x=0; x<map.xsize; x++) {
	  char ch=terline[x];
	  map_get_player_tile(x, y, plrno)->last_updated |= (ch-(isdigit(ch) ? '0' : 'a'-10))<<12;
	}
      }
    }
    
    {
      int j;
      struct dumb_city *pdcity;
      i = secfile_lookup_int(file, "player%d.total_ncities", plrno);
      for (j = 0; j < i; j++) {
	pdcity = fc_malloc(sizeof(struct dumb_city));
	pdcity->id = secfile_lookup_int(file, "player%d.dc%d.id", plrno, j);
	x = secfile_lookup_int(file, "player%d.dc%d.x", plrno, j);
	y = secfile_lookup_int(file, "player%d.dc%d.y", plrno, j);
	sz_strlcpy(pdcity->name, secfile_lookup_str(file, "player%d.dc%d.name", plrno, j));
	pdcity->size = secfile_lookup_int(file, "player%d.dc%d.size", plrno, j);
	pdcity->has_walls = secfile_lookup_int(file, "player%d.dc%d.has_walls", plrno, j);    
	pdcity->owner = secfile_lookup_int(file, "player%d.dc%d.owner", plrno, j);
	map_get_player_tile(x, y, plrno)->city = pdcity;
	alloc_id(pdcity->id);
      }
    }

    /* This shouldn't be neccesary if the savegame was consistent, but there
       is a bug in some pre-1.11 savegames. Anyway, it can't hurt */
    for (x=0; x<map.xsize; x++) {
      for (y=0; y<map.ysize; y++) {
	if (map_get_known_and_seen(x, y, plrno)) {
	  update_tile_knowledge(plr, x, y);
	  reality_check_city(plr, x, y);
	  if (map_get_city(x, y)) {
	    update_dumb_city(plr, map_get_city(x, y));
	  }
	}
      }
    }

  } else {
    /* We have an old savegame or fog of war was turned off; the players private
       knowledge is set to be what he could see without fog of war */
    for(y=0; y<map.ysize; y++)
      for(x=0; x<map.xsize; x++)
	if (map_get_known(x, y, plr)) {
	  struct city *pcity = map_get_city(x,y);
	  update_player_tile_last_seen(plr, x, y);
	  update_tile_knowledge(plr, x, y);
	  if (pcity)
	    update_dumb_city(plr, pcity);
	}
  }
}

/***************************************************************
Save the worklist elements specified by path, given the arguments
plrno and wlinx, from the worklist pointed to by pwl.
***************************************************************/
static void worklist_save(struct section_file *file,
			  char *path, int plrno, int wlinx,
			  struct worklist *pwl)
{
  char efpath[64];
  char idpath[64];
  int i;

  sz_strlcpy(efpath, path);
  sz_strlcat(efpath, ".wlef%d");
  sz_strlcpy(idpath, path);
  sz_strlcat(idpath, ".wlid%d");

  for (i = 0; i < MAX_LEN_WORKLIST; i++) {
    secfile_insert_int(file, pwl->wlefs[i], efpath, plrno, wlinx, i);
    secfile_insert_int(file, pwl->wlids[i], idpath, plrno, wlinx, i);
  }
}

/***************************************************************
...
***************************************************************/
static void player_save(struct player *plr, int plrno,
			struct section_file *file)
{
  int i, x, y;
  char invs[A_LAST+1];
  struct player_spaceship *ship = &plr->spaceship;
  char *pbuf=fc_malloc(map.xsize+1);

  secfile_insert_str(file, plr->name, "player%d.name", plrno);
  secfile_insert_str(file, plr->username, "player%d.username", plrno);
  secfile_insert_int(file, plr->nation, "player%d.race", plrno);
  secfile_insert_int(file, plr->government, "player%d.government", plrno);
  secfile_insert_int(file, plr->embassy, "player%d.embassy", plrno);

  for (i = 0; i < MAX_NUM_WORKLISTS; i++) {
    secfile_insert_int(file, plr->worklists[i].is_valid,
		       "player%d.worklist%d.is_valid", plrno, i);
    secfile_insert_str(file, plr->worklists[i].name,
		       "player%d.worklist%d.name", plrno, i);
    worklist_save(file, "player%d.worklist%d", plrno, i, &(plr->worklists[i]));
  }

  secfile_insert_int(file, plr->city_style, "player%d.city_style", plrno);
  secfile_insert_int(file, plr->is_male, "player%d.is_male", plrno);
  secfile_insert_int(file, plr->is_alive, "player%d.is_alive", plrno);
  secfile_insert_int(file, plr->ai.control, "player%d.ai.control", plrno);
  secfile_insert_int(file, plr->ai.tech_goal, "player%d.ai.tech_goal", plrno);
  secfile_insert_int(file, plr->ai.skill_level,
		     "player%d.ai.skill_level", plrno);
  secfile_insert_int(file, plr->ai.is_barbarian, "player%d.ai.is_barbarian", plrno);
  secfile_insert_int(file, plr->economic.gold, "player%d.gold", plrno);
  secfile_insert_int(file, plr->economic.tax, "player%d.tax", plrno);
  secfile_insert_int(file, plr->economic.science, "player%d.science", plrno);
  secfile_insert_int(file, plr->economic.luxury, "player%d.luxury", plrno);

  secfile_insert_int(file,plr->future_tech,"player%d.futuretech", plrno);

  secfile_insert_int(file, plr->research.researched, 
		     "player%d.researched", plrno);
  secfile_insert_int(file, plr->research.researchpoints,
		     "player%d.researchpoints", plrno);
  secfile_insert_int(file, plr->research.researching,
		     "player%d.researching", plrno);  

  secfile_insert_int(file, plr->capital, "player%d.capital", plrno);
  secfile_insert_int(file, plr->revolution, "player%d.revolution", plrno);

  for(i=0; i<game.num_tech_types; i++)
    invs[i]=(get_invention(plr, i)==TECH_KNOWN) ? '1' : '0';
  invs[i]='\0';
  secfile_insert_str(file, invs, "player%d.invs", plrno);

  secfile_insert_int(file, plr->reputation, "player%d.reputation", plrno);
  for (i = 0; i < MAX_NUM_PLAYERS+MAX_NUM_BARBARIANS; i++) {
    secfile_insert_int(file, plr->diplstates[i].type,
		       "player%d.diplstate%d.type", plrno, i);
    secfile_insert_int(file, plr->diplstates[i].turns_left,
		       "player%d.diplstate%d.turns_left", plrno, i);
    secfile_insert_int(file, plr->diplstates[i].has_reason_to_cancel,
		       "player%d.diplstate%d.has_reason_to_cancel", plrno, i);
  }

  {
    char vision[MAX_NUM_PLAYERS+MAX_NUM_BARBARIANS+1];

    for (i=0; i < MAX_NUM_PLAYERS+MAX_NUM_BARBARIANS; i++)
      vision[i] = plr->gives_shared_vision & (1 << i) ? '1' : '0';
    vision[i] = '\0';
    secfile_insert_str(file, vision, "player%d.gives_shared_vision", plrno);
  }

  secfile_insert_int(file, ship->state, "player%d.spaceship.state", plrno);

  if (ship->state != SSHIP_NONE) {
    char prefix[32];
    char st[NUM_SS_STRUCTURALS+1];
    int i;
    
    my_snprintf(prefix, sizeof(prefix), "player%d.spaceship", plrno);

    secfile_insert_int(file, ship->structurals, "%s.structurals", prefix);
    secfile_insert_int(file, ship->components, "%s.components", prefix);
    secfile_insert_int(file, ship->modules, "%s.modules", prefix);
    secfile_insert_int(file, ship->fuel, "%s.fuel", prefix);
    secfile_insert_int(file, ship->propulsion, "%s.propulsion", prefix);
    secfile_insert_int(file, ship->habitation, "%s.habitation", prefix);
    secfile_insert_int(file, ship->life_support, "%s.life_support", prefix);
    secfile_insert_int(file, ship->solar_panels, "%s.solar_panels", prefix);
    
    for(i=0; i<NUM_SS_STRUCTURALS; i++) {
      st[i] = (ship->structure[i]) ? '1' : '0';
    }
    st[i] = '\0';
    secfile_insert_str(file, st, "%s.structure", prefix);
    if (ship->state >= SSHIP_LAUNCHED) {
      secfile_insert_int(file, ship->launch_year, "%s.launch_year", prefix);
    }
  }

  secfile_insert_int(file, unit_list_size(&plr->units), "player%d.nunits", 
		     plrno);
  secfile_insert_int(file, city_list_size(&plr->cities), "player%d.ncities", 
		     plrno);

  i = -1;
  unit_list_iterate(plr->units, punit) {
    i++;
    secfile_insert_int(file, punit->id, "player%d.u%d.id", plrno, i);
    secfile_insert_int(file, punit->x, "player%d.u%d.x", plrno, i);
    secfile_insert_int(file, punit->y, "player%d.u%d.y", plrno, i);
    secfile_insert_int(file, punit->veteran, "player%d.u%d.veteran", 
				plrno, i);
    secfile_insert_int(file, punit->foul, "player%d.u%d.foul", 
				plrno, i);
    secfile_insert_int(file, punit->hp, "player%d.u%d.hp", plrno, i);
    secfile_insert_int(file, punit->homecity, "player%d.u%d.homecity",
				plrno, i);
    secfile_insert_int(file, punit->type, "player%d.u%d.type",
				plrno, i);
    secfile_insert_int(file, punit->activity, "player%d.u%d.activity",
				plrno, i);
    secfile_insert_int(file, punit->activity_count, 
				"player%d.u%d.activity_count",
				plrno, i);
    secfile_insert_int(file, punit->activity_target, 
				"player%d.u%d.activity_target",
				plrno, i);
    secfile_insert_int(file, punit->connecting, 
				"player%d.u%d.connecting",
				plrno, i);
    secfile_insert_int(file, punit->moves_left, "player%d.u%d.moves",
		                plrno, i);
    secfile_insert_int(file, punit->fuel, "player%d.u%d.fuel",
		                plrno, i);

    secfile_insert_int(file, punit->goto_dest_x, "player%d.u%d.goto_x", plrno, i);
    secfile_insert_int(file, punit->goto_dest_y, "player%d.u%d.goto_y", plrno, i);
    secfile_insert_int(file, punit->ai.control, "player%d.u%d.ai", plrno, i);
    secfile_insert_int(file, punit->ord_map, "player%d.u%d.ord_map", plrno, i);
    secfile_insert_int(file, punit->ord_city, "player%d.u%d.ord_city", plrno, i);
    secfile_insert_int(file, punit->moved, "player%d.u%d.moved", plrno, i);
    secfile_insert_int(file, punit->paradropped, "player%d.u%d.paradropped", plrno, i);
    secfile_insert_int(file, punit->transported_by,
		       "player%d.u%d.transported_by", plrno, i);
  }
  unit_list_iterate_end;

  i = -1;
  city_list_iterate(plr->cities, pcity) {
    int j, x, y;
    char buf[512];

    i++;
    secfile_insert_int(file, pcity->id, "player%d.c%d.id", plrno, i);
    secfile_insert_int(file, pcity->x, "player%d.c%d.x", plrno, i);
    secfile_insert_int(file, pcity->y, "player%d.c%d.y", plrno, i);
    secfile_insert_str(file, pcity->name, "player%d.c%d.name", plrno, i);
    secfile_insert_int(file, pcity->original, "player%d.c%d.original", 
		       plrno, i);
    secfile_insert_int(file, pcity->size, "player%d.c%d.size", plrno, i);
    secfile_insert_int(file, pcity->steal, "player%d.c%d.steal", plrno, i);
    secfile_insert_int(file, pcity->ppl_elvis, "player%d.c%d.nelvis", plrno, i);
    secfile_insert_int(file, pcity->ppl_scientist, "player%d.c%d.nscientist", 
		       plrno, i);
    secfile_insert_int(file, pcity->ppl_taxman, "player%d.c%d.ntaxman", plrno, i);

    for(j=0; j<4; j++)
      secfile_insert_int(file, pcity->trade[j], "player%d.c%d.traderoute%d", 
			 plrno, i, j);
    
    secfile_insert_int(file, pcity->food_stock, "player%d.c%d.food_stock", 
		       plrno, i);
    secfile_insert_int(file, pcity->shield_stock, "player%d.c%d.shield_stock", 
		       plrno, i);
    secfile_insert_int(file, pcity->turn_last_built,
		       "player%d.c%d.turn_last_built", plrno, i);
    secfile_insert_int(file, pcity->turn_changed_target,
		       "player%d.c%d.turn_changed_target", plrno, i);
    secfile_insert_int(file, pcity->changed_from_id,
		       "player%d.c%d.changed_from_id", plrno, i);
    secfile_insert_int(file, pcity->changed_from_is_unit,
		       "player%d.c%d.changed_from_is_unit", plrno, i);
    secfile_insert_int(file, pcity->before_change_shields,
		       "player%d.c%d.before_change_shields", plrno, i);
    secfile_insert_int(file, pcity->anarchy, "player%d.c%d.anarchy", plrno,i);
    secfile_insert_int(file, pcity->rapture, "player%d.c%d.rapture", plrno,i);
    secfile_insert_int(file, pcity->was_happy, "player%d.c%d.was_happy", plrno,i);
    secfile_insert_int(file, pcity->did_buy, "player%d.c%d.did_buy", plrno,i);
    secfile_insert_int(file, pcity->did_sell, "player%d.c%d.did_sell", plrno,i);
    secfile_insert_int(file, pcity->airlift, "player%d.c%d.airlift", plrno,i);

    /* for auto_attack */
    secfile_insert_int(file, pcity->city_options,
		       "player%d.c%d.options", plrno, i);
    
    j=0;
    for(y=0; y<CITY_MAP_SIZE; y++) {
      for(x=0; x<CITY_MAP_SIZE; x++) {
	switch (get_worker_city(pcity, x, y)) {
	  case C_TILE_EMPTY:       buf[j++] = '0'; break;
	  case C_TILE_WORKER:      buf[j++] = '1'; break;
	  case C_TILE_UNAVAILABLE: buf[j++] = '2'; break;
	}
      }
    }
    buf[j]='\0';
    secfile_insert_str(file, buf, "player%d.c%d.workers", plrno, i);

    secfile_insert_int(file, pcity->is_building_unit, 
		       "player%d.c%d.is_building_unit", plrno, i);
    secfile_insert_int(file, pcity->currently_building, 
		       "player%d.c%d.currently_building", plrno, i);

    for(j=0; j<game.num_impr_types; j++)
      buf[j]=(pcity->improvements[j]) ? '1' : '0';
    buf[j]='\0';
    secfile_insert_str(file, buf, "player%d.c%d.improvements", plrno, i);

    worklist_save(file, "player%d.c%d", plrno, i, pcity->worklist);
  }
  city_list_iterate_end;

  /********** Put the players private map **********/
 /* Otherwise the player can see all, and there's no reason to save the private map. */
  if (game.fogofwar) {
    /* put the terrain type */
    for(y=0; y<map.ysize; y++) {
      for(x=0; x<map.xsize; x++)
	pbuf[x]=terrain_chars[map_get_player_tile(x, y, plrno)->terrain];
      pbuf[x]='\0';
      
      secfile_insert_str(file, pbuf, "player%d.map_t%03d", plrno, y);
    }
    
    /* put lower 4 bits of special flags */
    for(y=0; y<map.ysize; y++) {
      for(x=0; x<map.xsize; x++)
      pbuf[x]=dec2hex[map_get_player_tile(x, y, plrno)->special&0xf];
      pbuf[x]='\0';
      
      secfile_insert_str(file, pbuf, "player%d.map_l%03d",plrno,  y);
    }
    
    /* put upper 4 bits of special flags */
    for(y=0; y<map.ysize; y++) {
      for(x=0; x<map.xsize; x++)
	pbuf[x]=dec2hex[(map_get_player_tile(x, y, plrno)->special&0xf0)>>4];
      pbuf[x]='\0';
      
      secfile_insert_str(file, pbuf, "player%d.map_u%03d", plrno,  y);
    }
    
    /* put "next" 4 bits of special flags */
    for(y=0; y<map.ysize; y++) {
      for(x=0; x<map.xsize; x++)
	pbuf[x]=dec2hex[(map_get_player_tile(x, y, plrno)->special&0xf00)>>8];
      pbuf[x]='\0';
      
      secfile_insert_str(file, pbuf, "player%d.map_n%03d", plrno, y);
    }
    
    /* put lower 4 bits of updated */
    for(y=0; y<map.ysize; y++) {
      for(x=0; x<map.xsize; x++)
	pbuf[x]=dec2hex[map_get_player_tile(x, y, plrno)->last_updated&0xf];
      pbuf[x]='\0';
      
      secfile_insert_str(file, pbuf, "player%d.map_ua%03d",plrno,  y);
    }
    
    /* put upper 4 bits of updated */
    for(y=0; y<map.ysize; y++) {
      for(x=0; x<map.xsize; x++)
	pbuf[x]=dec2hex[(map_get_player_tile(x, y, plrno)->last_updated&0xf0)>>4];
      pbuf[x]='\0';
      
      secfile_insert_str(file, pbuf, "player%d.map_ub%03d", plrno,  y);
    }
    
    /* put "next" 4 bits of updated */
    for(y=0; y<map.ysize; y++) {
      for(x=0; x<map.xsize; x++)
	pbuf[x]=dec2hex[(map_get_player_tile(x, y, plrno)->last_updated&0xf00)>>8];
      pbuf[x]='\0';
      
      secfile_insert_str(file, pbuf, "player%d.map_uc%03d", plrno, y);
    }
    
    /* put "yet next" 4 bits of updated */
    for(y=0; y<map.ysize; y++) {
      for(x=0; x<map.xsize; x++)
	pbuf[x]=dec2hex[(map_get_player_tile(x, y, plrno)->last_updated&0xf000)>>12];
      pbuf[x]='\0';
      
      secfile_insert_str(file, pbuf, "player%d.map_ud%03d", plrno, y);
    }
    
    free(pbuf);
    
    if (1) {
      struct dumb_city *pdcity;
      i = 0;
      
      for (x = 0; x < map.xsize; x++)
	for (y = 0; y < map.ysize; y++)
	  if ((pdcity = map_get_player_tile(x, y, plrno)->city)) {
	    secfile_insert_int(file, pdcity->id, "player%d.dc%d.id", plrno, i);
	    secfile_insert_int(file, x, "player%d.dc%d.x", plrno, i);
	    secfile_insert_int(file, y, "player%d.dc%d.y", plrno, i);
	    secfile_insert_str(file, pdcity->name, "player%d.dc%d.name", plrno, i);
	    secfile_insert_int(file, pdcity->size, "player%d.dc%d.size", plrno, i);
	    secfile_insert_int(file, pdcity->has_walls, "player%d.dc%d.has_walls", plrno, i);    
	    secfile_insert_int(file, pdcity->owner, "player%d.dc%d.owner", plrno, i);
	    i++;
	  }
    }
    secfile_insert_int(file, i, "player%d.total_ncities", plrno);
  }
}


/***************************************************************
 Assign values to ord_city and ord_map for each unit, so the
 values can be saved.
***************************************************************/
static void calc_unit_ordering(void)
{
  int i, j, x, y;
  
  for(i=0; i<game.nplayers; i++) {
    /* to avoid junk values for unsupported units: */
    unit_list_iterate(get_player(i)->units, punit) 
      punit->ord_city = 0;
    unit_list_iterate_end;
    city_list_iterate(get_player(i)->cities, pcity) {
      j = 0;
      unit_list_iterate(pcity->units_supported, punit) 
	punit->ord_city = j++;
      unit_list_iterate_end;
    }
    city_list_iterate_end;
  }

  for(y=0; y<map.ysize; y++) {
    for(x=0; x<map.xsize; x++) {
      j = 0;
      unit_list_iterate(map_get_tile(x,y)->units, punit) 
	punit->ord_map = j++;
      unit_list_iterate_end;
    }
  }
}
/***************************************************************
 For each city and tile, sort unit lists according to
 ord_city and ord_map values.
***************************************************************/
static void apply_unit_ordering(void)
{
  int i, x, y;
  
  for(i=0; i<game.nplayers; i++) {
    city_list_iterate(get_player(i)->cities, pcity) {
      unit_list_sort_ord_city(&pcity->units_supported);
    }
    city_list_iterate_end;
  }

  for(y=0; y<map.ysize; y++) {
    for(x=0; x<map.xsize; x++) { 
      unit_list_sort_ord_map(&map_get_tile(x,y)->units);
    }
  }
}

/***************************************************************
...
***************************************************************/
void game_load(struct section_file *file)
{
  int i, o;
  enum server_states tmp_server_state;
  char *savefile_options=" ";
  char *string;

  game.version = secfile_lookup_int_default(file, 0, "game.version");
  tmp_server_state = (enum server_states)
    secfile_lookup_int_default(file, RUN_GAME_STATE, "game.server_state");

  sz_strlcpy(srvarg.metaserver_info_line,
	     secfile_lookup_str_default(file, default_meta_server_info_string(),
					"game.metastring"));
  sz_strlcpy(srvarg.metaserver_addr,
	     secfile_lookup_str_default(file, DEFAULT_META_SERVER_ADDR,
					"game.metaserver"));
  meta_addr_split();

  game.gold          = secfile_lookup_int(file, "game.gold");
  game.tech          = secfile_lookup_int(file, "game.tech");
  game.skill_level   = secfile_lookup_int(file, "game.skill_level");
  if (game.skill_level==0)
    game.skill_level = GAME_OLD_DEFAULT_SKILL_LEVEL;
  game.timeout       = secfile_lookup_int(file, "game.timeout");
  game.end_year      = secfile_lookup_int(file, "game.end_year");
  game.techlevel     = secfile_lookup_int(file, "game.techlevel");
  game.year          = secfile_lookup_int(file, "game.year");
  game.min_players   = secfile_lookup_int(file, "game.min_players");
  game.max_players   = secfile_lookup_int(file, "game.max_players");
  game.nplayers      = secfile_lookup_int(file, "game.nplayers");
  game.globalwarming = secfile_lookup_int(file, "game.globalwarming");
  game.warminglevel  = secfile_lookup_int(file, "game.warminglevel");
  game.nuclearwinter = secfile_lookup_int_default(file, 0, "game.nuclearwinter");
  game.coolinglevel  = secfile_lookup_int_default(file, 8, "game.coolinglevel");
  game.unhappysize   = secfile_lookup_int(file, "game.unhappysize");

  if (game.version >= 10100) {
    game.cityfactor  = secfile_lookup_int(file, "game.cityfactor");
    game.diplcost    = secfile_lookup_int(file, "game.diplcost");
    game.freecost    = secfile_lookup_int(file, "game.freecost");
    game.conquercost = secfile_lookup_int(file, "game.conquercost");
    game.foodbox     = secfile_lookup_int(file, "game.foodbox");
    game.techpenalty = secfile_lookup_int(file, "game.techpenalty");
    game.razechance  = secfile_lookup_int(file, "game.razechance");

    /* suppress warnings about unused entries in old savegames: */
    section_file_lookup(file, "game.rail_food");
    section_file_lookup(file, "game.rail_prod");
    section_file_lookup(file, "game.rail_trade");
    section_file_lookup(file, "game.farmfood");
  }
  if (game.version >= 10300) {
    game.civstyle = secfile_lookup_int_default(file, 0, "game.civstyle");
    game.save_nturns = secfile_lookup_int(file, "game.save_nturns");
  }

  if ((game.version == 10604 && section_file_lookup(file,"savefile.options"))
      || (game.version > 10604)) 
    savefile_options = secfile_lookup_str(file,"savefile.options");
  /* else leave savefile_options empty */

  /* Note -- as of v1.6.4 you should use savefile_options (instead of
     game.version) to determine which variables you can expect to 
     find in a savegame file.  Or even better (IMO --dwp) is to use
     section_file_lookup(), secfile_lookup_int_default(),
     secfile_lookup_str_default(), etc.
  */

  sz_strlcpy(game.save_name,
	     secfile_lookup_str_default(file, GAME_DEFAULT_SAVE_NAME,
					"game.save_name"));

  game.aifill = secfile_lookup_int_default(file, 0, "game.aifill");

  game.scorelog = secfile_lookup_int_default(file, 0, "game.scorelog");

  game.fogofwar = secfile_lookup_int_default(file, 0, "game.fogofwar");
  game.fogofwar_old = game.fogofwar;
  
  game.civilwarsize =
    secfile_lookup_int_default(file, GAME_DEFAULT_CIVILWARSIZE,
			       "game.civilwarsize");
  
  if(has_capability("diplchance_percent", savefile_options)) {
    game.diplchance = secfile_lookup_int_default(file, game.diplchance,
						 "game.diplchance");
  } else {
    game.diplchance = secfile_lookup_int_default(file, 3, /* old default */
						 "game.diplchance");
    if (game.diplchance < 2) {
      game.diplchance = GAME_MAX_DIPLCHANCE;
    } else if (game.diplchance > 10) {
      game.diplchance = GAME_MIN_DIPLCHANCE;
    } else {
      game.diplchance = 100 - (10 * (game.diplchance - 1));
    }
  }
  game.aqueductloss = secfile_lookup_int_default(file, game.aqueductloss,
						 "game.aqueductloss");
  game.killcitizen = secfile_lookup_int_default(file, game.killcitizen,
						 "game.killcitizen");
  game.turnblock = secfile_lookup_int_default(file,game.turnblock,
					      "game.turnblock");
  game.fixedlength = secfile_lookup_int_default(file,game.fixedlength,
						"game.fixedlength");
  game.barbarianrate = secfile_lookup_int_default(file, game.barbarianrate,
						  "game.barbarians");
  game.onsetbarbarian = secfile_lookup_int_default(file, game.onsetbarbarian,
						   "game.onsetbarbs");
  game.nbarbarians = 0; /* counted in player_load for compatibility with 
			   1.10.0 */
  game.occupychance = secfile_lookup_int_default(file, game.occupychance,
  						 "game.occupychance");
  
  game.randseed = secfile_lookup_int_default(file, game.randseed,
					     "game.randseed");
  
  if (section_file_lookup(file, "random.index_J")) {
    RANDOM_STATE rstate;
    rstate.j = secfile_lookup_int(file,"random.index_J");
    rstate.k = secfile_lookup_int(file,"random.index_K");
    rstate.x = secfile_lookup_int(file,"random.index_X");
    for(i=0;i<8;i++) {
      char name[20];
      my_snprintf(name, sizeof(name), "random.table%d",i);
      string=secfile_lookup_str(file,name);
      sscanf(string,"%8x %8x %8x %8x %8x %8x %8x", &rstate.v[7*i],
	     &rstate.v[7*i+1], &rstate.v[7*i+2], &rstate.v[7*i+3],
	     &rstate.v[7*i+4], &rstate.v[7*i+5], &rstate.v[7*i+6]);
    }
    rstate.is_init = 1;
    set_myrand_state(rstate);
  }

  sz_strlcpy(game.ruleset.techs,
	     secfile_lookup_str_default(file, "default", "game.ruleset.techs"));
  sz_strlcpy(game.ruleset.units,
	     secfile_lookup_str_default(file, "default", "game.ruleset.units"));
  sz_strlcpy(game.ruleset.buildings,
	     secfile_lookup_str_default(file, "default",
					"game.ruleset.buildings"));
  sz_strlcpy(game.ruleset.terrain,
	     secfile_lookup_str_default(file, "classic",
					"game.ruleset.terrain"));
  sz_strlcpy(game.ruleset.governments,
	     secfile_lookup_str_default(file, "default",
					"game.ruleset.governments"));
  sz_strlcpy(game.ruleset.nations,
	     secfile_lookup_str_default(file, "default",
					"game.ruleset.nations"));
  sz_strlcpy(game.ruleset.cities,
	     secfile_lookup_str_default(file, "default", "game.ruleset.cities"));
  if(game.civstyle == 1) {
    string = "civ1";
  } else {
    string = "default";
    game.civstyle = GAME_DEFAULT_CIVSTYLE;
  }
  sz_strlcpy(game.ruleset.game,
	     secfile_lookup_str_default(file, string,
					"game.ruleset.game"));

  sz_strlcpy(game.demography,
	     secfile_lookup_str_default(file, GAME_DEFAULT_DEMOGRAPHY,
					"game.demography"));

  game.spacerace = secfile_lookup_int_default(file, game.spacerace,
					      "game.spacerace");

  game.auto_ai_toggle = secfile_lookup_int_default(file, game.auto_ai_toggle,
						   "game.auto_ai_toggle");

  game.heating=0;
  game.cooling=0;
  if(tmp_server_state==PRE_GAME_STATE 
     || has_capability("startoptions", savefile_options)) {
    if (game.version >= 10300) {
      game.settlers = secfile_lookup_int(file, "game.settlers");
      game.explorer = secfile_lookup_int(file, "game.explorer");
      game.dispersion =
	secfile_lookup_int_default(file, GAME_DEFAULT_DISPERSION, "game.dispersion");

      map.riches = secfile_lookup_int(file, "map.riches");
      map.huts = secfile_lookup_int(file, "map.huts");
      map.generator = secfile_lookup_int(file, "map.generator");
      map.seed = secfile_lookup_int(file, "map.seed");
      map.landpercent = secfile_lookup_int(file, "map.landpercent");
      map.grasssize =
	secfile_lookup_int_default(file, MAP_DEFAULT_GRASS, "map.grasssize");
      map.swampsize = secfile_lookup_int(file, "map.swampsize");
      map.deserts = secfile_lookup_int(file, "map.deserts");
      map.riverlength = secfile_lookup_int(file, "map.riverlength");
      map.mountains = secfile_lookup_int(file, "map.mountains");
      map.forestsize = secfile_lookup_int(file, "map.forestsize");

      if (has_capability("startoptions", savefile_options)) {
	map.xsize = secfile_lookup_int(file, "map.width");
	map.ysize = secfile_lookup_int(file, "map.height");
      } else {
	/* old versions saved with these names in PRE_GAME_STATE: */
	map.xsize = secfile_lookup_int(file, "map.xsize");
	map.ysize = secfile_lookup_int(file, "map.ysize");
      }

      if (tmp_server_state==PRE_GAME_STATE &&
	  map.generator == 0) {
	/* generator 0 = map done with map editor */
	/* aka a "scenario" */
        if (has_capability("specials",savefile_options)) {
          map_load(file);
	  map.fixed_start_positions = 1;
          section_file_check_unused(file, NULL);
          section_file_free(file);
          return;
        }
        map_tiles_load(file);
        if (has_capability("riversoverlay",savefile_options)) {
	  map_rivers_overlay_load(file);
	}
        if (has_capability("startpos",savefile_options)) {
          map_startpos_load(file);
	  map.fixed_start_positions = 1;
          return;
        }
	return;
      }
    }
    if(tmp_server_state==PRE_GAME_STATE) {
      return;
    }
  }

  game.is_new_game = 0;
  
  load_rulesets();

  map_load(file);

  /* destroyed wonders: */
  string = secfile_lookup_str_default(file, "", "game.destroyed_wonders");
  for(i=0; i<game.num_impr_types && string[i]; i++) {
    if (string[i] == '1') {
      game.global_wonders[i] = -1; /* destroyed! */
    }
  }
  
  for(i=0; i<game.nplayers; i++) {
    player_load(&game.players[i], i, file); 
  }
  /* Since the cities must be placed on the map to put them on the
     player map we do this afterwards */
  for(i=0; i<game.nplayers; i++) {
    player_map_load(&game.players[i], i, file); 
  }
  /* FIXME: This is a kluge to keep the AI working for the moment. */
  /*        When the AI is taught to handle diplomacy, remove this. */
  for (i = 0; i < game.nplayers; i++) {
    struct player *pplayer = get_player(i);
    if (pplayer->ai.control) {
      for (o = 0; o < game.nplayers; o++) {
	struct player *pother = get_player(o);
	if (pplayer != pother) {
	  pplayer->diplstates[pother->player_no].type =
	    pother->diplstates[pplayer->player_no].type =
	    DS_WAR;
	  pplayer->diplstates[pother->player_no].turns_left =
	    pother->diplstates[pplayer->player_no].turns_left =
	    16;
	}
      }
    }
  }

  /* We do this here since if the did it in player_load, player 1
     would try to unfog (unloaded) player 2's map when player 1's units
     were loaded */
  players_iterate(pplayer) {
    pplayer->really_gives_vision = 0;
    pplayer->gives_shared_vision = 0;
  } players_iterate_end;
  players_iterate(pplayer) {
    char *vision;
    int plrno = pplayer->player_no;
    vision = secfile_lookup_str_default(file, NULL, "player%d.gives_shared_vision",
					plrno);
    if (vision)
      for (i=0; i < MAX_NUM_PLAYERS+MAX_NUM_BARBARIANS; i++)
	if (vision[i] == '1')
	  give_shared_vision(pplayer, get_player(i));
  } players_iterate_end;

  initialize_globals();
  apply_unit_ordering();

  for(i=0; i<game.nplayers; i++) {
    update_research(&game.players[i]);
    city_list_iterate(game.players[i].cities, pcity)
      city_refresh(pcity);
    city_list_iterate_end;
  }

  game.player_idx=0;
  game.player_ptr=&game.players[0];  

  return;
}

/***************************************************************
...
***************************************************************/
void game_save(struct section_file *file)
{
  int i;
  int version;
  char options[512];
  char temp[B_LAST+1];

  version = MAJOR_VERSION *10000 + MINOR_VERSION *100 + PATCH_VERSION; 
  secfile_insert_int(file, version, "game.version");

  /* Game state: once the game is no longer a new game (ie, has been
   * started the first time), it should always be considered a running
   * game for savegame purposes:
   */
  secfile_insert_int(file, (int) (game.is_new_game ? server_state :
				  RUN_GAME_STATE), "game.server_state");
  
  secfile_insert_str(file, srvarg.metaserver_info_line, "game.metastring");
  secfile_insert_str(file, meta_addr_port(), "game.metaserver");
  
  sz_strlcpy(options, SAVEFILE_OPTIONS);
  if (myrand_is_init()) {
    sz_strlcat(options, " unirandom");   /* backward compat */
  }
  if (game.is_new_game) {
    if (map.num_start_positions>0) {
      sz_strlcat(options, " startpos");
    }
    if (map.have_specials) {
      sz_strlcat(options, " specials");
    }
    if (map.have_rivers_overlay && !map.have_specials) {
      sz_strlcat(options, " riversoverlay");
    }
  }
  secfile_insert_str(file, options, "savefile.options");
  
  secfile_insert_int(file, game.gold, "game.gold");
  secfile_insert_int(file, game.tech, "game.tech");
  secfile_insert_int(file, game.skill_level, "game.skill_level");
  secfile_insert_int(file, game.timeout, "game.timeout");
  secfile_insert_int(file, game.end_year, "game.end_year");
  secfile_insert_int(file, game.year, "game.year");
  secfile_insert_int(file, game.techlevel, "game.techlevel");
  secfile_insert_int(file, game.min_players, "game.min_players");
  secfile_insert_int(file, game.max_players, "game.max_players");
  secfile_insert_int(file, game.nplayers, "game.nplayers");
  secfile_insert_int(file, game.globalwarming, "game.globalwarming");
  secfile_insert_int(file, game.warminglevel, "game.warminglevel");
  secfile_insert_int(file, game.nuclearwinter, "game.nuclearwinter");
  secfile_insert_int(file, game.coolinglevel, "game.coolinglevel");
  secfile_insert_int(file, game.unhappysize, "game.unhappysize");
  secfile_insert_int(file, game.cityfactor, "game.cityfactor");
  secfile_insert_int(file, game.civilwarsize, "game.civilwarsize");
  secfile_insert_int(file, game.diplcost, "game.diplcost");
  secfile_insert_int(file, game.freecost, "game.freecost");
  secfile_insert_int(file, game.conquercost, "game.conquercost");
  secfile_insert_int(file, game.foodbox, "game.foodbox");
  secfile_insert_int(file, game.techpenalty, "game.techpenalty");
  secfile_insert_int(file, game.razechance, "game.razechance");
  secfile_insert_int(file, game.civstyle, "game.civstyle");
  secfile_insert_int(file, game.save_nturns, "game.save_nturns");
  secfile_insert_str(file, game.save_name, "game.save_name");
  secfile_insert_int(file, game.aifill, "game.aifill");
  secfile_insert_int(file, game.scorelog, "game.scorelog");
  secfile_insert_int(file, game.fogofwar, "game.fogofwar");
  secfile_insert_int(file, game.spacerace, "game.spacerace");
  secfile_insert_int(file, game.auto_ai_toggle, "game.auto_ai_toggle");
  secfile_insert_int(file, game.diplchance, "game.diplchance");
  secfile_insert_int(file, game.aqueductloss, "game.aqueductloss");
  secfile_insert_int(file, game.killcitizen, "game.killcitizen");
  secfile_insert_int(file, game.turnblock, "game.turnblock");
  secfile_insert_int(file, game.fixedlength, "game.fixedlength");
  secfile_insert_int(file, game.barbarianrate, "game.barbarians");
  secfile_insert_int(file, game.onsetbarbarian, "game.onsetbarbs");
  secfile_insert_int(file, game.occupychance, "game.occupychance");
  secfile_insert_str(file, game.ruleset.techs, "game.ruleset.techs");
  secfile_insert_str(file, game.ruleset.units, "game.ruleset.units");
  secfile_insert_str(file, game.ruleset.buildings, "game.ruleset.buildings");
  secfile_insert_str(file, game.ruleset.terrain, "game.ruleset.terrain");
  secfile_insert_str(file, game.ruleset.governments, "game.ruleset.governments");
  secfile_insert_str(file, game.ruleset.nations, "game.ruleset.nations");
  secfile_insert_str(file, game.ruleset.cities, "game.ruleset.cities");
  secfile_insert_str(file, game.ruleset.game, "game.ruleset.game");
  secfile_insert_str(file, game.demography, "game.demography");

  if (1) {
    /* Now always save these, so the server options reflect the
     * actual values used at the start of the game.
     * The first two used to be saved as "map.xsize" and "map.ysize"
     * when PRE_GAME_STATE, but I'm standardizing on width,height --dwp
     */
    secfile_insert_int(file, map.xsize, "map.width");
    secfile_insert_int(file, map.ysize, "map.height");
    secfile_insert_int(file, game.settlers, "game.settlers");
    secfile_insert_int(file, game.explorer, "game.explorer");
    secfile_insert_int(file, game.dispersion, "game.dispersion");
    secfile_insert_int(file, map.seed, "map.seed");
    secfile_insert_int(file, map.landpercent, "map.landpercent");
    secfile_insert_int(file, map.riches, "map.riches");
    secfile_insert_int(file, map.swampsize, "map.swampsize");
    secfile_insert_int(file, map.deserts, "map.deserts");
    secfile_insert_int(file, map.riverlength, "map.riverlength");
    secfile_insert_int(file, map.mountains, "map.mountains");
    secfile_insert_int(file, map.forestsize, "map.forestsize");
    secfile_insert_int(file, map.huts, "map.huts");
    secfile_insert_int(file, map.generator, "map.generator");
  } 

  secfile_insert_int(file, game.randseed, "game.randseed");
  
  if (myrand_is_init()) {
    RANDOM_STATE rstate = get_myrand_state();
    assert(rstate.is_init);

    secfile_insert_int(file, rstate.j, "random.index_J");
    secfile_insert_int(file, rstate.k, "random.index_K");
    secfile_insert_int(file, rstate.x, "random.index_X");

    for(i=0;i<8;i++) {
      char name[20], vec[100];
      my_snprintf(name, sizeof(name), "random.table%d", i);
      my_snprintf(vec, sizeof(vec),
		  "%8x %8x %8x %8x %8x %8x %8x", rstate.v[7*i],
		  rstate.v[7*i+1], rstate.v[7*i+2], rstate.v[7*i+3],
		  rstate.v[7*i+4], rstate.v[7*i+5], rstate.v[7*i+6]);
      secfile_insert_str(file, vec, name);
    }
  }

  if (!map_is_empty())
    map_save(file);
  
  if ((server_state==PRE_GAME_STATE) && game.is_new_game) {
    return; /* want to save scenarios as well */
  }
  
  /* destroyed wonders: */
  for(i=0; i<game.num_impr_types; i++) {
    if (is_wonder(i) && game.global_wonders[i]!=0
	&& !find_city_by_id(game.global_wonders[i])) {
      temp[i] = '1';
    } else {
      temp[i] = '0';
    }
  }
  temp[i] = '\0';
  secfile_insert_str(file, temp, "game.destroyed_wonders");

  calc_unit_ordering();
  
  for(i=0; i<game.nplayers; i++)
    player_save(&game.players[i], i, file);
}
