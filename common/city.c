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

#include <game.h>
#include <map.h>
#include <city.h>
#include <tech.h>


/****************************************************************
all the city improvements
use get_improvement_type(id) to access the array
*****************************************************************/
struct improvement_type improvement_types[B_LAST]={
  /* Buildings */
  {"Airport", 0, A_RADIO, 160, 3, A_NONE},
  {"Aqueduct", 0, A_CONSTRUCTION, 80, 2, A_NONE},
  {"Bank", 0, A_BANKING, 120, 3, A_NONE}, 
  {"Barracks",0, A_NONE,  40, 1, A_GUNPOWDER},
  {"Barracks II",0, A_GUNPOWDER,  40, 1, A_COMBUSTION},
  {"Barracks III",0, A_COMBUSTION,  40, 1, A_NONE},
  {"Cathedral",0, A_MONOTHEISM, 120, 3, A_NONE},
  {"City Walls",0, A_MASONRY, 80, 0, A_NONE},
  {"Coastal Defense",0, A_METALLURGY, 80, 1, A_NONE},
  {"Colosseum",0, A_CONSTRUCTION, 100, 4, A_NONE},
  {"Courthouse", 0, A_CODE, 80, 1, A_NONE},
  {"Factory", 0, A_INDUSTRIALIZATION, 200, 4, A_NONE},
  {"Granary", 0, A_POTTERY, 60, 1, A_NONE},
  {"Harbour", 0, A_SEAFARING, 60, 1, A_NONE},
  {"Hydro Plant", 0, A_ELECTRONICS, 240, 4, A_NONE},
  {"Library", 0, A_WRITING, 80, 1, A_NONE},
  {"Marketplace", 0, A_CURRENCY, 80, 1, A_NONE},
  {"Mass Transit", 0, A_MASS, 160, 4, A_NONE},
  {"Mfg. Plant", 0, A_ROBOTICS, 320, 6, A_NONE},
  {"Nuclear Plant", 0, A_POWER, 160, 2, A_NONE},
  {"Offshore Platform", 0, A_MINIATURIZATION, 160, 3, A_NONE}, 
  {"Palace", 0, A_MASONRY, 100, 0, A_NONE},
  {"Police Station", 0, A_COMMUNISM, 60, 2, A_NONE},
  {"Port Facility", 0, A_AMPHIBIOUS, 80, 3, A_NONE},   
  {"Power Plant",0, A_REFINING, 160, 4, A_NONE},
  {"Recycling Center", 0, A_RECYCLING, 200, 2, A_NONE},
  {"Research Lab", 0, A_COMPUTERS, 160, 3, A_NONE},
  {"SAM Battery", 0, A_ROCKETRY, 100, 2, A_NONE},
  {"SDI Defense", 0, A_LASER, 200, 4, A_NONE}, 
  {"Sewer System", 0, A_SANITATION, 120, 2, A_NONE},  
  {"Solar Plant", 0, A_ENVIRONMENTALISM, 320, 4, A_NONE},/*can't be build yet*/
  {"Space Component", 0, A_PLASTICS, 160, 0, A_NONE},    /*can't be build yet*/
  {"Space Module", 0, A_SUPERCONDUCTOR, 320, 0, A_NONE}, /*can't be build yet*/
  {"Space Structural", 0, A_SPACEFLIGHT, 80, 0, A_NONE}, /*can't be build yet*/
  {"Stock Exchange", 0, A_ECONOMICS, 160, 4, A_NONE},
  {"Super Highways", 0, A_AUTOMOBILE, 160, 3, A_NONE},
  {"Supermarket", 0, A_REFRIGERATION, 120, 3, A_NONE}, 
  {"Temple", 0, A_CEREMONIAL, 40, 1, A_NONE},
  {"University", 0, A_UNIVERSITY, 160, 3, A_NONE},
  /* Wonders */
  {"Apollo Program", 1, A_SPACEFLIGHT, 600, 0, A_NONE},
  {"A.Smith's Trading Co.", 1, A_ECONOMICS, 400, 0, A_NONE},
  {"Colossus", 1, A_BRONZE, 200, 0, A_FLIGHT}, 
  {"Copernicus' Observatory", 1, A_ASTRONOMY, 300, 0, A_NONE},
  {"Cure For Cancer", 1, A_GENETIC, 600, 0, A_NONE},
  {"Darwin's Voyage", 1, A_RAILROAD, 300, 0, A_NONE},
  {"Eiffel Tower", 1, A_LAST, 300, 0, A_NONE},
  {"Great Library", 1, A_LITERACY, 300, 0, A_ELECTRICITY},
  {"Great Wall", 1, A_MASONRY, 300, 0, A_METALLURGY},
  {"Hanging Gardens", 1, A_POTTERY, 200, 0, A_RAILROAD},
  {"Hoover Dam", 1, A_ELECTRONICS, 600, 0, A_NONE},
  {"Isaac Newton's College", 1, A_THEORY, 400, 0, A_NONE},
  {"J.S. Bach's Cathedral", 1, A_THEOLOGY, 400, 0, A_NONE},
  {"King Richard's Crusade", 1, A_ENGINEERING, 300, 0,  A_INDUSTRIALIZATION},
  {"Leonardo's Workshop", 1, A_INVENTION, 400, 0, A_AUTOMOBILE}, 
  {"Lighthouse", 1, A_MAPMAKING, 200, 0, A_MAGNETISM},
  {"Magellan's Expedition", 1, A_NAVIGATION, 400, 0, A_NONE},
  {"Manhattan Project", 1, A_FISSION, 600, 0, A_NONE},
  {"Marco Polo's Embassy", 1, A_LAST, 200, 0, A_COMMUNISM},
  {"Michelangelo's Chapel", 1, A_MONOTHEISM, 400, 0, A_NONE},
  {"Oracle", 1, A_MYSTICISM, 300, 0, A_THEOLOGY},
  {"Pyramids",1, A_MASONRY, 200, 0, A_NONE},
  {"SETI Program", 1, A_COMPUTERS, 600, 0, A_NONE},
  {"Shakespeare's Theatre", 1, A_MEDICINE, 300,0, A_NONE},
  {"Statue of Liberty", 1, A_DEMOCRACY, 400, 0, A_NONE},
  {"Sun Tzu's War Academy", 1, A_FEUDALISM, 300, 0, A_MOBILE},
  {"United Nations", 1, A_COMMUNISM, 600, 0, A_NONE},
  {"Women's Suffrage", 1, A_INDUSTRIALIZATION, 600, 0, A_NONE},
  /* special */
  {"Capitalization", 0, A_CORPORATION, 999, 0, A_NONE}
};


char *default_roman_city_names[] = {
  "Roma", "Syracusae", "Capua", "Tarentum", "Massilia", 
  "Tingis", "Nova Carthago", "Numantia", "Tarraco", "Narbo Martius",
  "Utica", "Chartago", "Caesarea", "Petra", "Ierusalem",
  "Trapezus", "Durcotorum", "Lugudunum", "Leptis Magna", "Mediolanum",
  "Messana", "Magontiacum", "Ravenna", "Eburacum", "Aquileia",
  "Salonae", "Cyrene", "Dyrrachium", "Luni", "Noreia", (char *)0
};

char *default_babylonian_city_names[] = {
  "Babylon", "Lagash", "Nippur", "Ur", "Kish", 
  "Shuruppak", "Kisurra", "Cutha", "Adab", "Umma",
  "Akkad", "Eridu", "Larsa", "Borsippa", "Sippar",
  "Nineveh", "Ashur", "Calach", "Carcar", "Hamat", /* Assyria */
  "Opis", "Cunaxa", "Arbela", "Nisibis", "Carrhae", /* Persian Babylonia */
  "Seleucia", "Ctesiphon", "Sittace", "Orchoe", "Apamea", /* Greek Babylonia */
  (char *)0
};

/* Important German cities in the 17th century */
char *default_german_city_names[] = {
  "Frankfurt", "Mainz", "Trier", "Aachen", "Münster",
  "Bremen", "Lübeck", "Hamburg", "Berlin", "Dresden",
  "Wien", "Fulda", "Würzburg", "Bamberg", "Nürnberg",
  "München", "Brixien", "Salzburg", "Passau", "Köln",
  "Koblenz", "Leipzig", "Regensburg", "Heidelberg", "Verden",
  "Kassel", "Wittenberg", "Wismar", "Ulm", "Nordlingen", (char *)0
};

/* Greek/Latin names */
char *default_egyptian_city_names[] = {
  "Thebae", "Memphis", "Heliopolis", "Syene", "Arsinoë",
  "Ammonium", "Alexandria", "Pelusium", "Canopus", "Naucratis",
  "Diospolis",  "Sais", "Acoris", "Cynopolis", "Thinis", 
  "Timonepsis", "Nilopolis", "Chemmis", "Antaeopolis", "Hermonthis",
  "Letopolis", "Aphroditopolis", "Heracleopolis", "Oxyrynchus",
  "Ibiu", "Busiris", "Andropolis", "Sele", "Antiphrae", "Apis", (char *)0
};

char *default_american_city_names[] = {
  "Washington", "New York", "Boston", "Philadelphia", "Los Angeles", 
  "Atlanta", "Chicago", "Buffalo", "St. Louis", "Detroit",
  "New Orleans", "Baltimore", "Denver", "Cincinnati", "Dallas", 
  "Minneapolis", "Houston", "San Antonio", "San Francisco", "Seattle",
  "Portland", "Miami", "Cleveland", "Buffalo", "Pittsburgh",
  "Salt Lake City", "San Diego", "Phoenix", "Kansas City", "Indianapolis",
  (char *)0
};

/* Latinized names */
char *default_greek_city_names[] = {
  "Athenae", "Sparta", "Thebae", "Corinthus", "Byzanthium",
  "Miletus", "Ephesus", "Nichopolis", "Argos", "Megalopolis",
  "Megara", "Plateae", "Chalcis", "Heraclea", "Thessalonica", 
  "Lysimachia", "Perinthus", "Gortyna", "Apollonia", "Didyma", 
  "Rhodus", "Doriscus", "Halicarnassus", "Chius", "Mytilene",
  "Phocaea", "Messene", "Olympia", "Delphi", "Delos", (char *)0
};

char *default_indian_city_names[] = {
  "Delhi", "Bombay", "Madras", "Bangalore", "Calcutta",
  "Lahore", "Karachi", "Ahmadabad", "Kanpur", "Hyderabad",
  "Dacca", "Lucknow", "Benares", "Chandrigarh", "Rawalpindi",
  "Colombo", "Madurai", "Coimbatore", "Calicut", "Trivandrum",
  "Nagpur", "Chittagong", "Multan", "Amritsar", "Srinagar",
  "Agra", "Bhopal", "Patna", "Surat", "Jaipur", (char *)0
};

/* Soviet names */
char *default_russian_city_names[] = {
  "Moskva", "Leningrad", "Kiev", "Gor'kiy", "Sverdlovsk",
  "Kuybyshev", "Donetsk", "Khar'kov", "Dnepropetrovsk", "Baku",
  "Novosibirsk", "Tashkent", "Minsk", "Riga", "Odessa", 
  "Tbilisi", "Yerevan", "Vyborg", "Perm'", "Ufa", 
  "Saratov", "Stalingrad", "Rostov", "Alma-Ata", "Omsk",
  "Chelyabinsk", "Irkutsk", "Vladivostok", "Kabarovsk", "Krasnoyarsk",
  (char *)0
};

/* We need more Zulu names */
char *default_zulu_city_names[] = {
  "Zimbabwe", "Ulundi", "Bapedi", "Hlobane", "Isandhlwana",
  "Intombe", "Mpondo", "Ngome", "Swazi", "Tugela", "Umtata",
  "Umfolozi", "Ibabanago", "Isipezi", "Amatikulu",
  "Zunguin", (char *)0
};

char *default_french_city_names[] = {
  "Paris", "Lyon", "Marseille", "Lille", "Bordeaux",
  "Nantes", "Rouen", "Reims", "Tours", "Angers", 
  "Toulouse", "Montpellier", "Nîmes", "Toulon", "Orléans",
  "Chartres", "Avignon", "Grenoble", "Dijon", "Amiens",
  "Cherbourg", "Caen", "Calais", "Limoges", "Clermont-Ferrand",
  "Nancy", "Besançon", "St. Etienne", "Brest", "Perpignan", (char *)0
};

char *default_aztec_city_names[] = {
  "Michoacan", "Axaca", "Tuxpan", "Metztitlan", "Otumba",
  "Tlacopán", "Theotihucanán", "Tezcuco", "Jalapa", "Tlaxcala",
  "Cholula", "Tenochtitlán", "Yopitzingo", "Teotitlán", "Theuantepec",
  "Chiauhtia", "Chapultepec", "Coatepec", "Ayotzinco", "Itzapalapa",
  "Iztapam", "Mitxcoac", "Tacubaya", "Tecamac", "Tepezinco",
  "Ticomán", "Xaltocán", "Xicalango", "Zumpanco", (char *)0
};

/* Pin Yin transliteration */
char *default_chinese_city_names[] = {
  "Beijing", "Shanghai", "Guangzhou", "Nanjing", "Tianjin",
  "Xi'an", "Taiyuan", "Shenyang", "Changchun", "Harbin",
  "Fushun", "Wuhan", "Chengdu", "Chongquing", "Kunming",
  "Hong Kong", "Guiyang", "Lanzhou", "Zhengzhou", "Changsha", 
  "Nanchang", "Fuzhou", "Hangzhou", "Wuxi", "Suzhou",
  "Xuzhou", "Jinan", "Lüda", "Jilin", "Qiqihar", (char *)0
};


char *default_english_city_names[] = {
  "London", "Birmingham", "Liverpool", "Manchester", "Leeds",
  "Newcastle", "Glasgow", "Belfast", "Edinburgh", "Middlesborough",
  "Sheffield", "Nottingham", "Coventry", "Cardiff", "Bristol",
  "Brighton", "Portsmouth", "Hull", "Leicester", "Plymouth",
  "Sunderland", "Stoke-on-Trent", "Bolton", "Norwich", "Oxford",
  "Swansea", "Dundee", "Cambridge", "Aberdeen", "Blackpool", (char *)0
};

char *default_mongol_city_names[] = {
  /* Chagatai */
  "Almaligh", "Khotan", "Turfan", "Samarkand", "Bokhara",
  "Tashkent", "Kashgar", "Balkh", "Kabul",
  /* Quipciaq (Golden Horde) */
  "Sarai", "Astrachan", "Tana", "Kazan", "Caffa", "Bulgar",
  /* Gran Khan */
  "Karakorum", "Hami", "Dunhuang", "Lhasa", "Tali",
  /* Ilkhan */
  "Tabriz", "Mosul", "Baghdad", "Basra", "Shiraz",
  "Esfahan", "Rayy", "Kerman", "Hormuz", "Daybul", (char *)0
};

char **race_default_city_names[R_LAST]={
  default_roman_city_names,
  default_babylonian_city_names,
  default_german_city_names,
  default_egyptian_city_names,
  default_american_city_names,
  default_greek_city_names,
  default_indian_city_names,
  default_russian_city_names,
  default_zulu_city_names,
  default_french_city_names,
  default_aztec_city_names,
  default_chinese_city_names,
  default_english_city_names,
  default_mongol_city_names
};

/****************************************************************
...
*****************************************************************/
int wonder_replacement(struct city *pcity, enum improvement_type_id id)
{
  switch (id) {
  case B_BARRACKS:
  case B_BARRACKS2:
  case B_BARRACKS3:
    if (city_affected_by_wonder(pcity, B_SUNTZU))
      return 1;
    break;
  case B_GRANARY:
    if (city_affected_by_wonder(pcity, B_PYRAMIDS)) 
      return 1;
    break;
  case B_CATHEDRAL:
    if (city_affected_by_wonder(pcity, B_MICHELANGELO))
      return 1;
    break;
  case B_CITY:  
    if (city_affected_by_wonder(pcity, B_WALL))
      return 1;
    break;
  case B_HYDRO:
  case B_POWER:
  case B_NUCLEAR:
    if (city_affected_by_wonder(pcity, B_HOOVER))
      return 1;
    break;
  case B_POLICE:
    if (city_affected_by_wonder(pcity, B_POLICE))
      return 1;
    break;
  case B_RESEARCH:
    if (city_affected_by_wonder(pcity, B_SETI))
      return 1;
    break;
  default:
    break;
  }
  return 0;
}

char *get_imp_name_ex(struct city *pcity, enum improvement_type_id id)
{
  static char buffer[256];
  char state ='w';
  if (wonder_replacement(pcity, id)) {
    sprintf(buffer, "%s(*)", get_improvement_type(id)->name);
    return buffer;
  }
  if (!is_wonder(id)) 
    return get_improvement_name(id);

  if (game.global_wonders[id]) state='B';
  if (wonder_obsolete(id)) state='O';
  sprintf(buffer, "%s(%c)", get_improvement_type(id)->name, state); 
  return buffer;
}
/****************************************************************
...
*****************************************************************/

char *get_improvement_name(enum improvement_type_id id)
{
  return get_improvement_type(id)->name; 
}

/****************************************************************
...
*****************************************************************/

char *city_name_suggestion(struct player *pplayer)
{
  char **nptr;
  int i;
  static char tempname[100];
  for(nptr=race_default_city_names[pplayer->race]; *nptr; nptr++) {
    if(!game_find_city_by_name(*nptr))
      return *nptr;
  }
  for (i = 0; i < 1000;i++ ) {
    sprintf(tempname, "city %d", i);
    if (!game_find_city_by_name(tempname)) 
      return tempname;
  }
  return "";
}

/**************************************************************************
...
**************************************************************************/

int improvement_value(enum improvement_type_id id)
{
  return (improvement_types[id].build_cost);
}

/**************************************************************************
...
**************************************************************************/

int is_wonder(enum improvement_type_id id)
{
  return (improvement_types[id].is_wonder);
}

/**************************************************************************
...
**************************************************************************/

int improvement_obsolete(struct player *pplayer, enum improvement_type_id id) 
{
  if (improvement_types[id].obsolete_by==A_NONE) 
    return 0;
  return (get_invention(pplayer, improvement_types[id].obsolete_by)
	  ==TECH_KNOWN);
}

/**************************************************************************
...
**************************************************************************/
  
int city_buy_cost(struct city *pcity)
{
  int total,cost;
  int build=pcity->shield_stock;
  if (pcity->is_building_unit) {
    total=unit_value(pcity->currently_building);
    if (build>=total)
      return 0;
    cost=(total-build)*2+(total-build)*(total-build)/20; 
    if (!build)
      cost*=2;
  } else {
    total=improvement_value(pcity->currently_building);
    if (build>=total)
      return 0;
    cost=(total-build)*2;
    if(!build)
      cost*=2;
    if(is_wonder(pcity->currently_building))
      cost*=2;
  }
  return cost;
}

/**************************************************************************
...
**************************************************************************/

int wonder_obsolete(enum improvement_type_id id)
{ 
  if (improvement_types[id].obsolete_by==A_NONE)
    return 0;
  return (game.global_advances[improvement_types[id].obsolete_by]);
}

/**************************************************************************
...
**************************************************************************/

struct improvement_type *get_improvement_type(enum improvement_type_id id)
{
  return &improvement_types[id];
}

/**************************************************************************
...
**************************************************************************/

struct player *city_owner(struct city *pcity)
{
  return (&game.players[pcity->owner]);
}

/****************************************************************
...
*****************************************************************/

int could_build_improvement(struct city *pcity, enum improvement_type_id id)
{ /* modularized so the AI can choose the tech it wants -- Syela */
  struct player *p=city_owner(pcity);
  if (id<0 || id>B_LAST)
    return 0;
  if (city_got_building(pcity, id))
    return 0;
  if ((city_got_building(pcity, B_HYDRO)|| city_got_building(pcity, B_POWER) ||
      city_got_building(pcity, B_NUCLEAR)) && (id==B_POWER || id==B_HYDRO || id==B_NUCLEAR))
    return 0;
  if (id==B_RESEARCH && !city_got_building(pcity, B_UNIVERSITY))
    return 0;
  if (id==B_UNIVERSITY && !city_got_building(pcity, B_LIBRARY))
    return 0;
  if (id==B_STOCK && !city_got_building(pcity, B_BANK))
    return 0;
  if (id == B_SEWER && !city_got_building(pcity, B_AQUEDUCT))
    return 0;
  if (id==B_BANK && !city_got_building(pcity, B_MARKETPLACE))
    return 0;
  if (id==B_MFG && !city_got_building(pcity, B_FACTORY))
    return 0;
  if ((id==B_HARBOUR || id==B_COASTAL || id == B_OFFSHORE || id == B_PORT) && !is_terrain_near_tile(pcity->x, pcity->y, T_OCEAN))
    return 0;
  if ((id == B_HYDRO || id == B_HOOVER) && (!is_terrain_near_tile(pcity->x, pcity->y, T_OCEAN) && !is_terrain_near_tile(pcity->x, pcity->y, T_RIVER)))
    return 0;
  if(improvement_obsolete(p, id)) return 0;
  if (is_wonder(id) && game.global_wonders[id])
    return 0;
  return !wonder_replacement(pcity, id);
}

int can_build_improvement(struct city *pcity, enum improvement_type_id id)
{
  struct player *p=city_owner(pcity);
  if (get_invention(p,improvement_types[id].tech_requirement)!=TECH_KNOWN)
    return 0;
  return(could_build_improvement(pcity, id));
}

/****************************************************************
...
*****************************************************************/
int can_build_unit_direct(struct city *pcity, enum unit_type_id id)
{  
  struct player *p=city_owner(pcity);
  if (id<0 || id>=U_LAST) return 0;
  if (id==U_NUCLEAR && !game.global_wonders[B_MANHATTEN])
    return 0;
  if (get_invention(p,unit_types[id].tech_requirement)!=TECH_KNOWN)
    return 0;
  if (!is_terrain_near_tile(pcity->x, pcity->y, T_OCEAN) && is_water_unit(id))
    return 0;
  return 1;
}

int can_build_unit(struct city *pcity, enum unit_type_id id)
{  
  struct player *p=city_owner(pcity);
  if (id<0 || id>=U_LAST) return 0;
  if (id==U_NUCLEAR && !game.global_wonders[B_MANHATTEN])
    return 0;
  if (get_invention(p,unit_types[id].tech_requirement)!=TECH_KNOWN)
    return 0;
  if (!is_terrain_near_tile(pcity->x, pcity->y, T_OCEAN) && is_water_unit(id))
    return 0;
  if (can_build_unit_direct(pcity, unit_types[id].obsoleted_by))
    return 0;
  return 1;
}

/**************************************************************************
...
**************************************************************************/

int city_population(struct city *pcity)
{
  int i;
  int res=0;
  for (i=1;i<=pcity->size;i++) res+=i;
  return res*10000;
}

/**************************************************************************
...
**************************************************************************/

int city_got_building(struct city *pcity,  enum improvement_type_id id) 
{
  if (id<0 || id>B_LAST)
    return 0;
  else 
    return (pcity->improvements[id]);
}

/**************************************************************************
...
**************************************************************************/
int improvement_upkeep(struct city *pcity, int i) 
{
  if (is_wonder(i)) return 0;
  if (city_affected_by_wonder(pcity, B_ASMITHS) && improvement_types[i].shield_upkeep == 1) 
    return 0;
  return (improvement_types[i].shield_upkeep);
}

/**************************************************************************
...
**************************************************************************/

int get_shields_tile(int x, int y, struct city *pcity)
{
  int s=0;
  enum tile_special_type spec_t=map_get_special(pcity->x+x-2, pcity->y+y-2);
  enum tile_terrain_type tile_t=map_get_terrain(pcity->x+x-2, pcity->y+y-2);

  int gov=get_government(pcity->owner);
  if (city_celebrating(pcity))
    gov+=2;
  if (spec_t & S_SPECIAL) 
    s=get_tile_type(tile_t)->shield_special;
  else
    s=get_tile_type(tile_t)->shield;

  if (x == 2 &&  y == 2 && !s) /* Always give a single shield on city square */
    s=1;
  
  if (spec_t & S_MINE) {
    s++;
    if (tile_t == T_HILLS) 
      s+=2;
  }
  if (spec_t & S_RAILROAD)
    s+=(s*game.rail_prod)/100;
  if (city_affected_by_wonder(pcity, B_RICHARDS))
    s++;
  if (city_got_building(pcity, B_OFFSHORE) && tile_t==T_OCEAN)
    s++;
  if (s>2 && gov <=G_DESPOTISM) 
    s--;
  if (spec_t & S_POLLUTION) return s=(s+1)/2; /* The shields here is icky */
  return s;
}

/**************************************************************************
...
**************************************************************************/

int get_trade_tile(int x, int y, struct city *pcity)
{
  enum tile_special_type spec_t=map_get_special(pcity->x+x-2, pcity->y+y-2);
  enum tile_terrain_type tile_t=map_get_terrain(pcity->x+x-2, pcity->y+y-2);
  int t;
  int gov=get_government(pcity->owner);
  if (city_celebrating(pcity)) 
    gov+=2;
 
  if (spec_t & S_SPECIAL) 
    t=get_tile_type(tile_t)->trade_special;
  else
    t=get_tile_type(tile_t)->trade;
    
  if (spec_t & S_ROAD) {
    switch (tile_t) {
    case T_DESERT:
    case T_GRASSLAND:
    case T_PLAINS:
      t++;
    default:
      break;
    }
  }
  if (gov >=G_REPUBLIC && t)
    t++; 

  if(t && city_affected_by_wonder(pcity, B_COLLOSSUS)) 
    t++;
  if (spec_t & S_RAILROAD) {
    t+=(t*game.rail_trade)/100;
    if (city_got_building(pcity, B_SUPERHIGHWAYS))
      t*=1.5;
  }
  
  
  if (t>2 && gov <=G_DESPOTISM) 
    t--;
  if (spec_t & S_POLLUTION) t=(t+1)/2; /* The trade here is dirty */
  return t;
}

/***************************************************************
Here the exact food production should be calculated. That is
including ALL modifiers. 
Center tile acts as irrigated...
***************************************************************/

int get_food_tile(int x, int y, struct city *pcity)
{
  int f;
  enum tile_special_type spec_t=map_get_special(pcity->x+x-2, pcity->y+y-2);
  enum tile_terrain_type tile_t=map_get_terrain(pcity->x+x-2, pcity->y+y-2);
  struct tile_type *type;
  int gov=get_government(pcity->owner);

  type=get_tile_type(tile_t);

  if (city_celebrating(pcity))
    gov+=2;

  if (spec_t & S_SPECIAL) 
    f=get_tile_type(tile_t)->food_special;
  else
    f=get_tile_type(tile_t)->food;

  if (spec_t & S_IRRIGATION || (x==2 && y==2 && tile_t==type->irrigation_result)) {
    f++;
  }
  if (city_got_building(pcity, B_HARBOUR) && tile_t==T_OCEAN)
    f++;

  if (spec_t & S_RAILROAD)
    f+=(f*game.rail_food)/100;

  if (city_got_building(pcity, B_SUPERMARKET) && (spec_t & S_IRRIGATION))
    f *= 1.5;

  if (f>2 && gov <=G_DESPOTISM) 
    f--;

  if (spec_t & S_POLLUTION)
    f=(f+1)/2;

  return f;
}

/**************************************************************************
...
**************************************************************************/

int can_establish_trade_route(struct city *pc1, struct city *pc2)
{
  int i;
  int r=1;
  if(!pc1 || !pc2 || pc1==pc2 ||
     (pc1->owner==pc2->owner && 
      map_distance(pc1->x, pc1->y, pc2->x, pc2->y)<=8))
    return 0;
  
  for(i=0;i<4;i++) {
    r*=pc1->trade[i];
    if (pc1->trade[i]==pc2->id) return 0;
  }
  if (r) return 0;
  r = 1;
  for (i=0;i<4;i++) 
    r*=pc2->trade[i];
  return (!r);
}


/**************************************************************************
...
**************************************************************************/

int trade_between_cities(struct city *pc1, struct city *pc2)
{
  int bonus=0;

  if (pc2 && pc1) {
    bonus=(pc1->trade_prod+pc2->trade_prod+4)/8;
    if (map_get_continent(pc1->x, pc1->y) == map_get_continent(pc2->x, pc2->y))
      bonus/=2;
	
    if (pc1->owner==pc2->owner)
      bonus/=2;
  }
  return bonus;
}

/*************************************************************************
Calculate amount of gold remaining in city after paying for buildings
*************************************************************************/

int city_gold_surplus(struct city *pcity)
{
  int cost=0;
  int i;
  for (i=0;i<B_LAST;i++) 
    if (city_got_building(pcity, i)) 
      cost+=improvement_upkeep(pcity, i);
  return pcity->tax_total-cost;
}


/**************************************************************************
...
**************************************************************************/
int city_got_citywalls(struct city *pcity)
{
  if (city_got_building(pcity, B_CITY))
    return 1;
  return (city_affected_by_wonder(pcity, B_WALL));
}


/**************************************************************************
...
**************************************************************************/
int city_affected_by_wonder(struct city *pcity, enum improvement_type_id id) /*FIX*/
{
  struct city *tmp;
  if (id<0 || id>=B_LAST || !is_wonder(id) || wonder_obsolete(id))
    return 0;
  tmp=find_city_by_id(game.global_wonders[id]);
  if (!tmp)
    return 0;
  if (id==B_MANHATTEN) 
    return 1;
  if (tmp->owner!=pcity->owner)
    return 0;
  if (city_got_building(pcity, id))
    return 1;
  switch (id) {
  case B_ASMITHS:
  case B_APOLLO:
  case B_CURE:
  case B_GREAT:
  case B_WALL:
  case B_HANGING:
  case B_ORACLE:
  case B_UNITED:
  case B_WOMENS:
  case B_DARWIN:
  case B_MAGELLAN:
  case B_MICHELANGELO:
  case B_SETI:
  case B_PYRAMIDS:
  case B_LIBERTY:
  case B_SUNTZU:
    return 1;
  case B_ISAAC:
  case B_COPERNICUS:
  case B_SHAKESPEARE:
  case B_COLLOSSUS:
  case B_RICHARDS:
    return 0;
  case B_HOOVER:
  case B_BACH: /*
    return (map_get_continent(tmp->x, tmp->y) == map_get_continent(pcity->x, pcity->y));
    */
    return 1;
  default:
    return 0;
  }
} 
/***************************************************************
...
***************************************************************/

int city_happy(struct city *pcity)
{
  return (pcity->ppl_happy[4]>=(pcity->size+1)/2 && pcity->ppl_unhappy[4]==0);
}

/**************************************************************************
...
**************************************************************************/
int city_unhappy(struct city *pcity)
{
  return (pcity->ppl_happy[4]<pcity->ppl_unhappy[4]);
}

int city_celebrating(struct city *pcity)
{
  return (pcity->size>=5 && pcity->was_happy/* city_happy(pcity)*/);
}

/***************************************************************
...
***************************************************************/
struct city *find_city_by_id(int id)
{
  int i;

  for(i=0; i<game.nplayers; i++) {
    struct city *pcity;
    
    if((pcity=city_list_find_id(&game.players[i].cities, id)))
      return pcity;
  }
  return 0;
}


/**************************************************************************
...
**************************************************************************/
void city_list_init(struct city_list *This)
{
  genlist_init(&This->list);
}

/**************************************************************************
...
**************************************************************************/
struct city *city_list_get(struct city_list *This, int index)
{
  return (struct city *)genlist_get(&This->list, index);
}


/**************************************************************************
...
**************************************************************************/
struct city *city_list_find_id(struct city_list *This, int id)
{
  if(id) {
    struct genlist_iterator myiter;

    genlist_iterator_init(&myiter, &This->list, 0);
    
    for(; ITERATOR_PTR(myiter); ITERATOR_NEXT(myiter))
    if(((struct city *)ITERATOR_PTR(myiter))->id==id)
	return ITERATOR_PTR(myiter);
  }
  return 0;
}

/**************************************************************************
...
**************************************************************************/
struct city *city_list_find_name(struct city_list *This, char *name)
{
  struct genlist_iterator myiter;

  genlist_iterator_init(&myiter, &This->list, 0);

  for(; ITERATOR_PTR(myiter); ITERATOR_NEXT(myiter))
    if(!mystrcasecmp(name, ((struct city *)ITERATOR_PTR(myiter))->name))
      return ITERATOR_PTR(myiter);

  return 0;
}


/**************************************************************************
...
**************************************************************************/
struct city *city_list_find_coor(struct city_list *This, int x, int y)
{
  struct genlist_iterator myiter;

  genlist_iterator_init(&myiter, &This->list, 0);

  for(; ITERATOR_PTR(myiter); ITERATOR_NEXT(myiter)) {
    struct city *pcity=((struct city *)ITERATOR_PTR(myiter));
    if(same_pos(pcity->x, pcity->y, x, y))
      return pcity;
  }

  return 0;
}


/**************************************************************************
...
**************************************************************************/
void city_list_insert(struct city_list *This, struct city *pcity)
{
  genlist_insert(&This->list, pcity, 0);
}


/**************************************************************************
...
**************************************************************************/
int city_list_size(struct city_list *This)
{
  return genlist_size(&This->list);
}

/**************************************************************************
...
**************************************************************************/
void city_list_unlink(struct city_list *This, struct city *pcity)
{
  genlist_unlink(&This->list, pcity);
}







