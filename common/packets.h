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
#ifndef FC__PACKETS_H
#define FC__PACKETS_H

#include "map.h"
#include "player.h"
#include "shared.h"		/* MAX_LEN_NAME, MAX_LEN_ADDR */
#include "spaceship.h"
#include "worklist.h"

#define MAX_LEN_PACKET    4096
#define MAX_LEN_USERNAME    10	     /* see below */
#define MAX_LEN_MSG       1536
#define MAX_LEN_CAPSTR     512

/* Note that MAX_LEN_USERNAME cannot be expanded, because it
   is used for the name in the first packet sent by the client,
   before we have the capability string.
*/

enum packet_type {
  PACKET_REQUEST_JOIN_GAME,
  PACKET_JOIN_GAME_REPLY,
  PACKET_SERVER_SHUTDOWN,
  PACKET_UNIT_INFO,
  PACKET_MOVE_UNIT,
  PACKET_TURN_DONE,
  PACKET_NEW_YEAR,
  PACKET_TILE_INFO,
  PACKET_SELECT_NATION,
  PACKET_ALLOC_NATION,
  PACKET_SHOW_MESSAGE,
  PACKET_PLAYER_INFO,
  PACKET_GAME_INFO,
  PACKET_MAP_INFO,
  PACKET_CHAT_MSG,
  PACKET_CITY_INFO,
  PACKET_CITY_SELL,
  PACKET_CITY_BUY,
  PACKET_CITY_CHANGE,
  PACKET_CITY_WORKLIST,
  PACKET_CITY_MAKE_SPECIALIST,
  PACKET_CITY_MAKE_WORKER,
  PACKET_CITY_CHANGE_SPECIALIST,
  PACKET_CITY_RENAME,
  PACKET_PLAYER_RATES,
  PACKET_PLAYER_REVOLUTION,
  PACKET_PLAYER_GOVERNMENT,
  PACKET_PLAYER_RESEARCH,
  PACKET_PLAYER_WORKLIST,
  PACKET_UNIT_BUILD_CITY,
  PACKET_UNIT_DISBAND,
  PACKET_REMOVE_UNIT,
  PACKET_REMOVE_CITY,
  PACKET_UNIT_CHANGE_HOMECITY,
  PACKET_UNIT_COMBAT,
  PACKET_UNIT_ESTABLISH_TRADE,
  PACKET_UNIT_HELP_BUILD_WONDER,
  PACKET_UNIT_GOTO_TILE,
  PACKET_GAME_STATE,
  PACKET_NUKE_TILE,
  PACKET_DIPLOMAT_ACTION,
  PACKET_PAGE_MSG,
  PACKET_REPORT_REQUEST,
  PACKET_DIPLOMACY_INIT_MEETING,
  PACKET_DIPLOMACY_CREATE_CLAUSE,
  PACKET_DIPLOMACY_REMOVE_CLAUSE,
  PACKET_DIPLOMACY_CANCEL_MEETING,
  PACKET_DIPLOMACY_ACCEPT_TREATY,
  PACKET_DIPLOMACY_SIGN_TREATY,
  PACKET_UNIT_AUTO,
  PACKET_BEFORE_NEW_YEAR,
  PACKET_REMOVE_PLAYER,
  PACKET_UNITTYPE_UPGRADE,
  PACKET_UNIT_UNLOAD,
  PACKET_PLAYER_TECH_GOAL,
  PACKET_CITY_REFRESH,
  PACKET_INCITE_INQ,
  PACKET_INCITE_COST,
  PACKET_UNIT_UPGRADE,
  PACKET_PLAYER_CANCEL_PACT,
  PACKET_RULESET_TECH,
  PACKET_RULESET_UNIT,
  PACKET_RULESET_BUILDING,
  PACKET_CITY_OPTIONS,
  PACKET_SPACESHIP_INFO,
  PACKET_SPACESHIP_ACTION,
  PACKET_UNIT_NUKE,
  PACKET_RULESET_TERRAIN,
  PACKET_RULESET_TERRAIN_CONTROL,
  PACKET_RULESET_GOVERNMENT,
  PACKET_RULESET_GOVERNMENT_RULER_TITLE,
  PACKET_RULESET_CONTROL,
  PACKET_CITY_NAME_SUGGEST_REQ,
  PACKET_CITY_NAME_SUGGESTION,
  PACKET_RULESET_NATION,
  PACKET_UNIT_PARADROP_TO,
  PACKET_RULESET_CITY,
  PACKET_UNIT_CONNECT,
  PACKET_SABOTAGE_LIST,
  PACKET_ADVANCE_FOCUS,
  PACKET_LAST  /* leave this last */
};

enum report_type {
  REPORT_WONDERS_OF_THE_WORLD,
  REPORT_TOP_5_CITIES,
  REPORT_DEMOGRAPHIC,
  REPORT_SERVER_OPTIONS,   /* obsolete */
  REPORT_SERVER_OPTIONS1,
  REPORT_SERVER_OPTIONS2
};

enum spaceship_action_type {
  SSHIP_ACT_LAUNCH,
  SSHIP_ACT_PLACE_STRUCTURAL,
  SSHIP_ACT_PLACE_FUEL,
  SSHIP_ACT_PLACE_PROPULSION,
  SSHIP_ACT_PLACE_HABITATION,
  SSHIP_ACT_PLACE_LIFE_SUPPORT,
  SSHIP_ACT_PLACE_SOLAR_PANELS
};

/*********************************************************
  diplomacy action!
*********************************************************/
struct packet_diplomacy_info {
  int plrno0, plrno1;
  int plrno_from;
  int clause_type;
  int value;
};


/*********************************************************
  diplomat action!
*********************************************************/
struct packet_diplomat_action
{
  int action_type;
  int value;        
  int diplomat_id;
  int target_id;    /* city_id or unit_id */
};



/*********************************************************
  unit request
*********************************************************/
struct packet_nuke_tile
{
  int x, y;
};



/*********************************************************
  unit request
*********************************************************/
struct packet_unit_combat
{
  int attacker_unit_id;
  int defender_unit_id;
  int attacker_hp;
  int defender_hp;
  int make_winner_veteran;
};


/*********************************************************
  unit request
*********************************************************/
struct packet_unit_request
{
  int unit_id;
  int city_id;
  int x, y;
  char name[MAX_LEN_NAME];
};

/*********************************************************
  unit connect
*********************************************************/
struct packet_unit_connect
{
  int activity_type;
  int unit_id;
  int dest_x;
  int dest_y;
};

/*********************************************************
  unit request
*********************************************************/
struct packet_unittype_info 
{
  int action;
  int type;
};

/*********************************************************
  player request
*********************************************************/
struct packet_player_request
{
  int tax, luxury, science;              /* rates */
  int government;                        /* government */
  int tech;                              /* research */
  struct worklist worklist;              /* one worklist */
  int wl_idx;                            /* which worklist */
};

/*********************************************************
  city request
*********************************************************/
struct packet_city_request
{
  int city_id;                           /* all */
  int build_id;                          /* change, sell */
  int is_build_id_unit_id;               /* change */
  int worker_x, worker_y;                /* make_worker, make_specialist */
  int specialist_from, specialist_to;    /* change_specialist */
  char name[MAX_LEN_NAME];            /* rename */
  struct worklist worklist;              /* worklist */
};


/*********************************************************
  tile info
*********************************************************/
struct packet_tile_info {
  int x, y, type, special, known;
};



/*********************************************************
send to each client whenever the turn has ended.
*********************************************************/
struct packet_new_year {
  int year;
};
struct packet_before_new_year {
  int dummy;
};


/*********************************************************
packet represents a request to the server, for moving the
units with the corresponding id's from the unids array,
to the position x,y
unids[] is a compressed array, containing garbage after
last 0 id.
*********************************************************/
struct packet_move_unit {
  int x, y, unid;
};


/*********************************************************

*********************************************************/
struct packet_unit_info {
  int id;
  int owner;
  int x, y;
  int veteran;
  int homecity;
  int type;
  int movesleft;
  int hp;
  int activity;
  int activity_count;
  int unhappiness;
  int upkeep;
  int upkeep_food;
  int upkeep_gold;
  int ai;
  int fuel;
  int goto_dest_x, goto_dest_y;
  int activity_target;
  int paradropped;
  int connecting;
  int carried;
  int select_it;
};


/*********************************************************
...
*********************************************************/
struct packet_city_info {
  int id;
  int owner;
  int x, y;
  char name[MAX_LEN_NAME];

  int size;
  int ppl_happy, ppl_content, ppl_unhappy;
  int ppl_elvis, ppl_scientist, ppl_taxman;
  int food_prod, food_surplus;
  int shield_prod, shield_surplus;
  int trade_prod, corruption;
  int trade[4],trade_value[4];
  int luxury_total, tax_total, science_total;

  /* the physics */
  int food_stock;
  int shield_stock;
  int pollution;

  int is_building_unit;
  int currently_building;

  struct worklist worklist;

  char improvements[B_LAST+1];
  char city_map[CITY_MAP_SIZE*CITY_MAP_SIZE+1];

  int did_buy, did_sell;
  int was_happy;
  int airlift;
  int diplomat_investigate;
  int city_options;
};



/*********************************************************
 this packet is the very first packet send by the client.
 the player hasn't been accepted yet.
 'short_name' is the same as 'name', but possibly truncated
 (can only add long name at end, to avoid problems with
 connection to/from older versions)
*********************************************************/
struct packet_req_join_game {
  char short_name[MAX_LEN_USERNAME];
  int major_version;
  int minor_version;
  int patch_version;
  char capability[MAX_LEN_CAPSTR];
  char name[MAX_LEN_NAME];
  char version_label[MAX_LEN_NAME];
};


/*********************************************************
 ... and the server replies.
*********************************************************/
struct packet_join_game_reply {
  int you_can_join;             /* true/false */
  char message[MAX_LEN_MSG];
  char capability[MAX_LEN_CAPSTR];
};


/*********************************************************
...
*********************************************************/
struct packet_alloc_nation {
  Nation_Type_id nation_no;
  char name[MAX_LEN_NAME];
  int is_male;
  int city_style;
};


/*********************************************************
 this structure is a generic packet, which is used by a great
 number of different packets. In general it's used by all
 packets, which only requires a message(apart from the type).
 blah blah..
*********************************************************/
struct packet_generic_message {
  char message[MAX_LEN_MSG];
  int x,y,event;
};


/*********************************************************
  like the packet above. 
*********************************************************/
struct packet_generic_integer {
  int value;
};


/*********************************************************
...
*********************************************************/
struct packet_player_info {
  int playerno;
  char name[MAX_LEN_NAME];
  int is_male;
  int government;
  int embassy;
  int city_style;
  int nation;
  int turn_done, nturns_idle;
  int is_alive;
  int reputation;
  struct player_diplstate diplstates[MAX_NUM_PLAYERS + MAX_NUM_BARBARIANS];
  int gold, tax, science, luxury;
  int researched;
  int researchpoints;
  int researching;
  int future_tech;
  int tech_goal;
  unsigned char inventions[A_LAST+1];
  int is_connected;
  char addr[MAX_LEN_ADDR];
  int revolution;
  int ai;
  int is_barbarian;
  char capability[MAX_LEN_CAPSTR];
  struct worklist worklists[MAX_NUM_WORKLISTS];
};

/*********************************************************
The server tells the client all about a spaceship:
*********************************************************/
struct packet_spaceship_info {
  int player_num;
  int sship_state;
  int structurals;
  int components;
  int modules;
  char structure[NUM_SS_STRUCTURALS+1];
  int fuel;
  int propulsion;
  int habitation;
  int life_support;
  int solar_panels;
  int launch_year;
  int population;
  int mass;
  float support_rate;
  float energy_rate;
  float success_rate;
  float travel_time;
};

/*********************************************************
Client does something to a spaceship:
*********************************************************/
struct packet_spaceship_action {
  int action;
  int num;
  /* meaning of num:
     SSHIP_ACT_LAUNCH:  ignored
     _PLACE_STRUCTURAL: index to sship->structure[]
     others: new value for sship->fuel etc; should be just
     one more than current value of ship->fuel etc
     (used to avoid possible problems if we send duplicate
     packets when client auto-builds?)
   */
};


/*********************************************************
  Ruleset control values: single values, some of which are
  needed before sending other ruleset data (eg,
  num_unit_types, government_count).  This is only sent
  once at the start of the game, eg unlike game_info which
  is sent again each turn.  (Terrain ruleset has enough
  info for its own "control" packet, done separately.)
*********************************************************/
struct packet_ruleset_control {
  int aqueduct_size;
  int sewer_size;
  int add_to_size_limit;
  int num_unit_types;
  int num_tech_types;
  struct {
    int get_bonus_tech;
    int cathedral_plus;
    int cathedral_minus;
    int colosseum_plus;
    int temple_plus;
    int partisan_req[MAX_NUM_TECH_LIST]; 
  } rtech;
  int government_when_anarchy;
  int default_government;
  int government_count;
  int nation_count;
  int playable_nation_count;
  int style_count;
};

/*********************************************************
Specify all the fields of a struct unit_type
*********************************************************/
struct packet_ruleset_unit {
  int id;			/* index for unit_types[] */
  char name[MAX_LEN_NAME];
  char graphic_str[MAX_LEN_NAME];
  char graphic_alt[MAX_LEN_NAME];
  int move_type;
  int build_cost;
  int attack_strength;
  int defense_strength;
  int move_rate;
  int tech_requirement;
  int vision_range;
  int transport_capacity;
  int hp;
  int firepower;
  int obsoleted_by;
  int fuel;
  int flags;
  int roles;			/* a client-side-ai might be interested */

  int happy_cost;  /* unhappy people in home city */
  int shield_cost; /* normal upkeep cost */
  int food_cost;   /* settler food cost */
  int gold_cost;   /* gold upkeep (n/a now, maybe later) */

  int paratroopers_range; /* max range of paratroopers, F_PARATROOPERS */
  int paratroopers_mr_req;
  int paratroopers_mr_sub;

  /* Following is a pointer to malloced memory; on the server, it
     points to putype->helptext, malloced earlier; on the client,
     it is malloced when packet received, and then putype->helptext
     is assigned to allocated pointer.
  */
  char *helptext;
};

struct packet_ruleset_tech {
  int id, req[2];		/* indices for advances[] */
  int flags;
  char name[MAX_LEN_NAME];
  char *helptext;		/* same as for packet_ruleset_unit, above */
};

struct packet_ruleset_building {
  int id;			/* index for improvement_types[] */
  char name[MAX_LEN_NAME];
  Tech_Type_id tech_req;
  Impr_Type_id bldg_req;
  enum tile_terrain_type *terr_gate;
  enum tile_special_type *spec_gate;
  Eff_Range_id equiv_range;
  Impr_Type_id *equiv_dupl;
  Impr_Type_id *equiv_repl;
  Tech_Type_id obsolete_by;
  int is_wonder;
  int build_cost;
  int upkeep;
  int sabotage;
  struct impr_effect *effect;
  int variant;		/* FIXME: remove when gen-impr obsoletes */
  char *helptext;		/* same as for packet_ruleset_unit, above */
};

struct packet_ruleset_terrain {
  int id;			/* index for tile_types[] */

  char terrain_name[MAX_LEN_NAME];
  char graphic_str[MAX_LEN_NAME];
  char graphic_alt[MAX_LEN_NAME];

  int movement_cost;
  int defense_bonus;

  int food;
  int shield;
  int trade;

  char special_1_name[MAX_LEN_NAME];
  int food_special_1;
  int shield_special_1;
  int trade_special_1;

  char special_2_name[MAX_LEN_NAME];
  int food_special_2;
  int shield_special_2;
  int trade_special_2;

  /* above special stuff could go in here --dwp */
  struct {
    char graphic_str[MAX_LEN_NAME];
    char graphic_alt[MAX_LEN_NAME];
  } special[2];

  int road_trade_incr;
  int road_time;

  enum tile_terrain_type irrigation_result;
  int irrigation_food_incr;
  int irrigation_time;

  enum tile_terrain_type mining_result;
  int mining_shield_incr;
  int mining_time;

  enum tile_terrain_type transform_result;
  int transform_time;
  
  char *helptext;		/* same as for packet_ruleset_unit, above */
};

struct packet_ruleset_government {
  int id;
      
  int required_tech;
  int max_rate;
  int civil_war;
  int martial_law_max;
  int martial_law_per;
  int empire_size_mod;
  int empire_size_inc;
  int rapture_size;
      
  int unit_happy_cost_factor;
  int unit_shield_cost_factor;
  int unit_food_cost_factor;
  int unit_gold_cost_factor;
      
  int free_happy;
  int free_shield;
  int free_food;
  int free_gold;
      
  int trade_before_penalty;
  int shields_before_penalty;
  int food_before_penalty;
      
  int celeb_trade_before_penalty;
  int celeb_shields_before_penalty;
  int celeb_food_before_penalty;
      
  int trade_bonus;
  int shield_bonus;
  int food_bonus;
      
  int celeb_trade_bonus;
  int celeb_shield_bonus;
  int celeb_food_bonus;
      
  int corruption_level;
  int corruption_modifier;
  int fixed_corruption_distance;
  int corruption_distance_factor;
  int extra_corruption_distance;
      
  int flags;
  int hints;
      
  int num_ruler_titles;
       
  char name[MAX_LEN_NAME];
  char graphic_str[MAX_LEN_NAME];
  char graphic_alt[MAX_LEN_NAME];
  
  char *helptext;		/* same as for packet_ruleset_unit, above */
};

struct packet_ruleset_government_ruler_title {
  int gov;
  int id;
  int nation;
  char male_title[MAX_LEN_NAME];
  char female_title[MAX_LEN_NAME];
};

struct packet_ruleset_nation {
  int id;
  char name[MAX_LEN_NAME];
  char name_plural[MAX_LEN_NAME];
  char graphic_str[MAX_LEN_NAME];
  char graphic_alt[MAX_LEN_NAME];

  int leader_count;
  char leader_name[MAX_NUM_LEADERS][MAX_LEN_NAME];
  int leader_sex[MAX_NUM_LEADERS];
  int city_style;
};

struct packet_ruleset_city {
  int style_id;
  char name[MAX_LEN_NAME];
  char graphic[MAX_LEN_NAME];
  char graphic_alt[MAX_LEN_NAME];
  int techreq;
  int replaced_by;
};

/*********************************************************
...
*********************************************************/
struct packet_game_info {
  int gold;
  int civstyle;
  int tech;
  int techlevel;
  int skill_level;
  int timeout;
  int end_year;
  int year;
  int min_players, max_players, nplayers;
  int player_idx;
  int globalwarming;
  int heating;
  int cityfactor;
  int unhappysize;
  int diplcost,freecost,conquercost;
  int global_advances[A_LAST];
  int global_wonders[B_LAST];
  int foodbox;
  int techpenalty;
  int spacerace;
  /* the following values are computed each time packet_game_info is sent */
  int seconds_to_turndone;
};

/*********************************************************
...
*********************************************************/
struct packet_map_info {
  int xsize, ysize;
  int is_earth;
};

/*********************************************************
...
*********************************************************/
struct packet_generic_values {
  int id;
  int value1,value2;
};

/*********************************************************
  For city name suggestions, client sends unit id of unit
  building the city.  The server does not use the id, but
  sends it back to the client so   that the client knows
  what to do with the suggestion when it arrives back.
  (This is for the reply; the request is sent as a generic
  integer packet with the id value.)
  (Currently, for city renaming, default is existing name;
  if wanted to suggest a new name, could do the same thing
  sending the city id as id, and only client needs to change.)
*********************************************************/
struct packet_city_name_suggestion {
  int id;
  char name[MAX_LEN_NAME];
};

/*********************************************************
this is where the data is first collected, whenever it
arrives to the client/server.
*********************************************************/
struct socket_packet_buffer {
  int ndata;
  int do_buffer_sends;
  unsigned char data[10*MAX_LEN_PACKET];
};

struct packet_sabotage_list
{
  int diplomat_id;
  int city_id;
  char improvements[B_LAST+1];
};


typedef	void	CLOSE_FUN	(struct connection *pc);


struct connection {
  int sock, used;
  int first_packet;		/* check byte order on first packet */
  int byte_swap;		/* connection uses non-network byte order */
  char *player; 
  struct socket_packet_buffer buffer;
  struct socket_packet_buffer send_buffer;
  char addr[MAX_LEN_ADDR];
  char capability[MAX_LEN_CAPSTR];
  /* "capability" gives the capability string of the executable (be it
   * a client or server) at the other end of the connection.
   */
  enum cmdlevel_id access_level;
  /* Used in the server, "access_level" stores the access granted
   * to the client corresponding to this connection.
   */
};


int read_socket_data(int sock, struct socket_packet_buffer *buffer);
int send_connection_data(struct connection *pc, unsigned char *data, int len);

/* These two are non-static for meta.c; others are now static --dwp */
unsigned char *put_uint16(unsigned char *buffer, int val);
unsigned char *put_string(unsigned char *buffer, char *mystring);

int send_packet_diplomacy_info(struct connection *pc, enum packet_type pt,
			       struct packet_diplomacy_info *packet);
struct packet_diplomacy_info *
receive_packet_diplomacy_info(struct connection *pc);

int send_packet_diplomat_action(struct connection *pc, 
				struct packet_diplomat_action *packet);
struct packet_diplomat_action *
receive_packet_diplomat_action(struct connection *pc);

int send_packet_nuke_tile(struct connection *pc, 
			  struct packet_nuke_tile *packet);
struct packet_nuke_tile *
receive_packet_nuke_tile(struct connection *pc);


int send_packet_unit_combat(struct connection *pc, 
			    struct packet_unit_combat *packet);
struct packet_unit_combat *
receive_packet_unit_combat(struct connection *pc);


int send_packet_unit_connect(struct connection *pc, 
			    struct packet_unit_connect *packet);
struct packet_unit_connect *
receive_packet_unit_connect(struct connection *pc);


int send_packet_tile_info(struct connection *pc, 
			 struct packet_tile_info *pinfo);
struct packet_tile_info *receive_packet_tile_info(struct connection *pc);

int send_packet_map_info(struct connection *pc, 
			  struct packet_map_info *pinfo);
struct packet_map_info *receive_packet_map_info(struct connection *pc);

int send_packet_game_info(struct connection *pc, 
			  struct packet_game_info *pinfo);
struct packet_game_info *receive_packet_game_info(struct connection *pc);


struct packet_player_info *receive_packet_player_info(struct connection *pc);
int send_packet_player_info(struct connection *pc, 
			    struct packet_player_info *pinfo);


int send_packet_new_year(struct connection *pc, 
			 struct packet_new_year *request);
struct packet_new_year *receive_packet_new_year(struct connection *pc);

int send_packet_move_unit(struct connection *pc, 
			  struct packet_move_unit *request);
struct packet_move_unit *receive_packet_move_unit(struct connection *pc);


int send_packet_unit_info(struct connection *pc,
			  struct packet_unit_info *req);
struct packet_unit_info *receive_packet_unit_info(struct connection *pc);

int send_packet_req_join_game(struct connection *pc, 
			      struct packet_req_join_game *request);
struct packet_req_join_game *receive_packet_req_join_game(struct 
							  connection *pc);

int send_packet_join_game_reply(struct connection *pc, 
			       struct packet_join_game_reply *reply);
struct packet_join_game_reply *receive_packet_join_game_reply(struct 
							      connection *pc);

int send_packet_alloc_nation(struct connection *pc, 
			   struct packet_alloc_nation *packet);
struct packet_alloc_nation *receive_packet_alloc_nation(struct connection *pc);


int send_packet_generic_message(struct connection *pc, int type,
				struct packet_generic_message *message);
struct packet_generic_message *receive_packet_generic_message(struct 
							      connection *pc);

int send_packet_generic_integer(struct connection *pc, int type,
				struct packet_generic_integer *packet);
struct packet_generic_integer *receive_packet_generic_integer(struct 
							      connection *pc);


int send_packet_city_info(struct connection *pc,struct packet_city_info *req);
struct packet_city_info *receive_packet_city_info(struct connection *pc);

int send_packet_city_request(struct connection *pc, 
			     struct packet_city_request *packet, enum packet_type);
struct packet_city_request *
receive_packet_city_request(struct connection *pc);


int send_packet_player_request(struct connection *pc, 
			       struct packet_player_request *packet,
			       enum packet_type req_type);
struct packet_player_request *
receive_packet_player_request(struct connection *pc);

struct packet_unit_request *
receive_packet_unit_request(struct connection *pc);
int send_packet_unit_request(struct connection *pc, 
			     struct packet_unit_request *packet,
			     enum packet_type req_type);

int send_packet_before_new_year(struct connection *pc);
struct packet_before_new_year *receive_packet_before_new_year(struct connection *pc);

int send_packet_unittype_info(struct connection *pc, int type, int action);
struct packet_unittype_info *receive_packet_unittype_info(struct connection *pc);

int send_packet_ruleset_control(struct connection *pc, 
				struct packet_ruleset_control *pinfo);
struct packet_ruleset_control *
receive_packet_ruleset_control(struct connection *pc);

int send_packet_ruleset_unit(struct connection *pc,
			     struct packet_ruleset_unit *packet);
struct packet_ruleset_unit *
receive_packet_ruleset_unit(struct connection *pc);

int send_packet_ruleset_tech(struct connection *pc,
			     struct packet_ruleset_tech *packet);
struct packet_ruleset_tech *
receive_packet_ruleset_tech(struct connection *pc);

int send_packet_ruleset_building(struct connection *pc,
			     struct packet_ruleset_building *packet);
struct packet_ruleset_building *
receive_packet_ruleset_building(struct connection *pc);

int send_packet_ruleset_terrain(struct connection *pc,
			     struct packet_ruleset_terrain *packet);
struct packet_ruleset_terrain *
receive_packet_ruleset_terrain(struct connection *pc);
int send_packet_ruleset_terrain_control(struct connection *pc,
					struct terrain_misc *packet);
struct terrain_misc *
receive_packet_ruleset_terrain_control(struct connection *pc);

int send_packet_ruleset_government(struct connection *pc,
				struct packet_ruleset_government *packet);
struct packet_ruleset_government *
receive_packet_ruleset_government(struct connection *pc);
int send_packet_ruleset_government_ruler_title(struct connection *pc,
				struct packet_ruleset_government_ruler_title *packet);
struct packet_ruleset_government_ruler_title *
receive_packet_ruleset_government_ruler_title(struct connection *pc);

int send_packet_ruleset_nation(struct connection *pc,
			       struct packet_ruleset_nation *packet);
struct packet_ruleset_nation *
receive_packet_ruleset_nation(struct connection *pc);

int send_packet_ruleset_city(struct connection *pc,
			     struct packet_ruleset_city *packet);
struct packet_ruleset_city *
receive_packet_ruleset_city(struct connection *pc);

int send_packet_before_end_year(struct connection *pc);

int send_packet_generic_values(struct connection *pc, int type,
			       struct packet_generic_values *req);
struct packet_generic_values *
receive_packet_generic_values(struct connection *pc);

int send_packet_spaceship_info(struct connection *pc,
			       struct packet_spaceship_info *packet);
struct packet_spaceship_info *
receive_packet_spaceship_info(struct connection *pc);

int send_packet_spaceship_action(struct connection *pc,
				 struct packet_spaceship_action *packet);
struct packet_spaceship_action *
receive_packet_spaceship_action(struct connection *pc);

int send_packet_city_name_suggestion(struct connection *pc,
				     struct packet_city_name_suggestion *packet);
struct packet_city_name_suggestion *
receive_packet_city_name_suggestion(struct connection *pc);

int send_packet_sabotage_list(struct connection *pc,
			      struct packet_sabotage_list *packet);
struct packet_sabotage_list *
receive_packet_sabotage_list(struct connection *pc);

void *get_packet_from_connection(struct connection *pc, int *ptype);
void remove_packet_from_buffer(struct socket_packet_buffer *buffer);
void close_socket_set_callback(CLOSE_FUN *fun);
void flush_connection_send_buffer(struct connection *pc);
void connection_do_buffer(struct connection *pc);
void connection_do_unbuffer(struct connection *pc);

#endif  /* FC__PACKETS_H */
