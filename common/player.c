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
#include <stdlib.h>
#include <string.h>

#include <game.h>
#include <player.h>
#include <unit.h>
#include <city.h>
#include <map.h>
#include <shared.h>
#include <tech.h>

struct player_race races[]= { /* additional goals added by Syela */
  {"Roman", "Romans",           1, 2, 2,
     {100,100,100,100,100,100,100},
   { {A_REPUBLIC, A_WHEEL, A_IRON, A_CONSTRUCTION, A_RAILROAD, 
      A_NONE, A_NONE, A_NONE, A_NONE, A_NONE},           
    B_LEONARDO  , G_DEMOCRACY} /* wonder not actually checked in choose_build */
  },
  {"Babylonian", "Babylonians", 0, 0, 2,
     {100,100,100,100,100,100,100},
   { {A_MONARCHY, A_PHILOSOPHY, A_MATHEMATICS, A_CHIVALRY, A_THEOLOGY,
      A_NONE, A_NONE, A_NONE, A_NONE, A_NONE},          
     B_HANGING   , G_MONARCHY}
  },
  {"German", "Germans",         2, 0, 2,        
   {100,100,100,100,100,100,100},
   { {A_REPUBLIC, A_IRON, A_GUNPOWDER, A_EXPLOSIVES, A_FLIGHT,
      A_NONE, A_NONE, A_NONE, A_NONE, A_NONE},            
     B_BACH      , G_REPUBLIC}
  },
  {"Egyptian", "Egyptians",     1, 1, 2,    
   {100,100,100,100,100,100,100}, /* different order for experiment and flavor */
   { {A_MONARCHY, A_PHILOSOPHY, A_NAVIGATION, A_IRON, A_RAILROAD,
      A_NONE, A_NONE, A_NONE, A_NONE, A_NONE}, 
     B_PYRAMIDS  , G_MONARCHY}
  },
  {"American", "Americans",     0, 1, 2,    
   {100,100,100,100,100,100,100},
   { {A_REPUBLIC, A_TRADE, A_ENGINEERING, A_DEMOCRACY, A_RAILROAD,
        A_EXPLOSIVES, A_AUTOMOBILE, A_NONE, A_NONE, A_NONE},
     B_LIBERTY   , G_DEMOCRACY}
  },
  {"Greek", "Greeks",           1, 2, 0,          
   {100,100,100,100,100,100,100},
   { {A_REPUBLIC, A_PHILOSOPHY, A_TRADE, A_ENGINEERING, A_IRON,
      A_RAILROAD, A_NONE, A_NONE, A_NONE, A_NONE},
     B_LIGHTHOUSE, G_REPUBLIC}
  },
  {"Indian", "Indians",         0, 0, 1,        
   {100,100,100,100,100,100,100},
   { {A_MONARCHY, A_PHILOSOPHY, A_REPUBLIC, A_IRON, A_ENGINEERING,
      A_RAILROAD, A_NONE, A_NONE, A_NONE, A_NONE},
     B_ORACLE    , G_REPUBLIC}
  },
  {"Russian", "Russians",       2, 1, 0,      
   {100,100,100,100,100,100,100},
   { {A_MONARCHY, A_PHILOSOPHY, A_CHIVALRY, A_TRADE, A_BRIDGE,
      A_RAILROAD, A_COMMUNISM, A_NONE, A_NONE, A_NONE},
     B_WOMENS    , G_COMMUNISM}
  },
  {"Zulu", "Zulus",             2, 1, 1,            
   {100,100,100,100,100,100,100},
   { {A_MONARCHY, A_PHILOSOPHY, A_CHIVALRY, A_TRADE, A_BRIDGE,
      A_RAILROAD, A_COMMUNISM, A_NONE, A_NONE, A_NONE},
     B_APOLLO    , G_COMMUNISM}
  },
  {"French", "French",          2, 2, 2,         
   {100,100,100,100,100,100,100},
   { {A_MONARCHY, A_CHIVALRY, A_PHILOSOPHY, A_REPUBLIC, A_MONOTHEISM,
      A_ENGINEERING, A_NAVIGATION, A_RAILROAD, A_NONE, A_NONE},
     B_MAGELLAN  , G_REPUBLIC}
  },
  {"Aztec", "Aztecs",           1, 0, 2,          
   {100,100,100,100,100,100,100},
   { {A_MONARCHY, A_CHIVALRY, A_IRON, A_TRADE, A_NAVIGATION,
      A_RAILROAD, A_COMMUNISM, A_NONE, A_NONE, A_NONE}, 
     B_HOOVER    , G_COMMUNISM}
  },
  {"Chinese", "Chinese",        1, 1, 2,     
   {100,100,100,100,100,100,100},
   { {A_MONARCHY, A_TRADE, A_PHILOSOPHY, A_BRIDGE, A_RAILROAD, 
      A_COMMUNISM, A_NONE ,A_NONE ,A_NONE, A_NONE}, 
     B_WALL      , G_COMMUNISM}
  },
  {"English", "English",        1, 2, 1,     
     {100,100,100,100,100,100,100},
   { {A_MONARCHY, A_CHIVALRY, A_TRADE, A_THEOLOGY, A_NAVIGATION,
      A_DEMOCRACY,A_RAILROAD, A_NONE, A_NONE, A_NONE}, 
     B_RICHARDS  , G_DEMOCRACY}
  },
  {"Mongol", "Mongols",         2, 2, 0,      
   {100,100,100,100,100,100,100},
   { {A_MONARCHY, A_CHIVALRY, A_TRADE, A_BRIDGE, A_RAILROAD,
      A_NONE, A_NONE ,A_NONE ,A_NONE, A_NONE}, 
     B_SUNTZU    , G_MONARCHY}
  }
};

char *default_race_leader_names[] = {
  "Caesar",
  "Hammurabi",
  "Frederick",
  "Ramesses",
  "Lincoln",
  "Alexander",
  "Gandi",
  "Stalin",
  "Shaka",
  "Napoleon",
  "Montezuma",
  "Mao",
  "Elizabeth",
  "Genghis"
};

char *government_names[G_LAST] = {
  "Anarchy", "Despotism", "Monarchy",
  "Communism", "Republic", "Democracy"
};

char *ruler_titles[G_LAST] = {
  "Mr.", "Emperor", "King", /* even for Elizabeth */
  "Comrade", "President", "President"
};

/***************************************************************
...
***************************************************************/
int player_has_embassy(struct player *pplayer, struct player *pplayer2)
{
  return pplayer->embassy & (1<<pplayer2->player_no);
}

/****************************************************************
...
*****************************************************************/

int player_owns_city(struct player *pplayer, struct city *pcity)
{
  if (!pcity || !pplayer) return 0; /* better safe than sorry */
  return (pcity->owner==pplayer->player_no);
}

/***************************************************************
...
***************************************************************/
char *get_race_name(enum race_type race)
{
  return races[race].name;
}

/***************************************************************
...
***************************************************************/
char *get_race_name_plural(enum race_type race)
{
  return races[race].name_plural;
}

struct player_race *get_race(struct player *plr)
{
  return &races[plr->race];
}

/***************************************************************
...
***************************************************************/
char *get_ruler_title(enum government_type type)
{
  return ruler_titles[type];
}

/***************************************************************
...
***************************************************************/
char *get_government_name(enum government_type type)
{
  return government_names[type];
}

/***************************************************************
...
***************************************************************/
int can_change_to_government(struct player *pplayer, enum government_type gov)
{
  struct city *pcity;

  if (gov>=G_LAST)
    return 0;

  switch (gov) {
  case G_ANARCHY:
  case G_DESPOTISM: 
    return 1;
    break;
  case G_MONARCHY:
    if (get_invention(pplayer, A_MONARCHY)==TECH_KNOWN)
      return 1;
    break;
  case G_COMMUNISM:
    if (get_invention(pplayer, A_COMMUNISM)==TECH_KNOWN)
      return 1;
    break;
  case G_REPUBLIC:
    if (get_invention(pplayer, A_REPUBLIC)==TECH_KNOWN)
      return 1;
    break;
  case G_DEMOCRACY:
    if (get_invention(pplayer, A_DEMOCRACY)==TECH_KNOWN)
        return 1;
    break;
  default:
    return 0;
    break;
  }
  pcity=find_city_by_id(game.global_wonders[B_LIBERTY]);
  return (pcity && player_owns_city(pplayer, pcity));
}

/***************************************************************
...
***************************************************************/
void player_init(struct player *plr)
{
  plr->player_no=plr-game.players;

  strcpy(plr->name, "YourName");
  plr->government=G_DESPOTISM;
  plr->race=R_LAST;
  plr->capital=0;
  unit_list_init(&plr->units);
  city_list_init(&plr->cities);
  strcpy(plr->addr, "---.---.---.---");
  plr->is_alive=1;
  plr->embassy=0;
  plr->ai.control=0;
  plr->ai.tech_goal = A_NONE;
  plr->ai.handicap = 0;
  plr->future_tech=0;
  plr->economic.tax=PLAYER_DEFAULT_TAX_RATE;
  plr->economic.science=PLAYER_DEFAULT_SCIENCE_RATE;
  plr->economic.luxury=PLAYER_DEFAULT_LUXURY_RATE;
}

/***************************************************************
...
***************************************************************/
void player_set_unit_focus_status(struct player *pplayer)
{
  unit_list_iterate(pplayer->units, punit) 
    punit->focus_status=FOCUS_AVAIL;
  unit_list_iterate_end;
}

/***************************************************************
...
***************************************************************/
struct player *find_player_by_name(char *name)
{
  int i;

  for(i=0; i<game.nplayers; i++)
     if(!mystrcasecmp(name, game.players[i].name))
	return &game.players[i];

  return 0;
}

/***************************************************************
no map visibility check here!
***************************************************************/
int player_can_see_unit(struct player *pplayer, struct unit *punit)
{
  if(punit->owner==pplayer->player_no)
    return 1;
  if(unit_flag(punit->type, F_SUBMARINE)) {
    int x, y;
    for(y=punit->y-1; y<punit->y+2; ++y) { 
      if(y<0 || y>=map.ysize)
	continue;
      for(x=punit->x-1; x<punit->x+2; ++x) { 
	struct city *pcity;
	struct unit_list *srclist;
	struct genlist_iterator myiter;
	srclist=&map_get_tile(x, y)->units;
	genlist_iterator_init(&myiter, &srclist->list, 0);
	for(; ITERATOR_PTR(myiter); ITERATOR_NEXT(myiter)) {
	  struct unit *pu=(struct unit *)ITERATOR_PTR(myiter);
	  if(pu->owner==pplayer->player_no)
	    return 1;
	}
	if((pcity=map_get_city(x, y)) && pcity->owner==pplayer->player_no)
	  return 1;
      }
    }
    return 0;
  }
  
  return 1;
}
