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
#ifndef __PACKETS_H
#define __PACKETS_H

#include "player.h"

#define MAX_PACKET_SIZE   4096
#define NAME_SIZE           10
#define MSG_SIZE          1536
#define ADDR_LENGTH         32

extern char our_capability[MSG_SIZE];

enum packet_type {
  PACKET_REQUEST_JOIN_GAME,
  PACKET_JOIN_GAME_REPLY,
  PACKET_SERVER_SHUTDOWN,
  PACKET_UNIT_INFO,
  PACKET_MOVE_UNIT,
  PACKET_TURN_DONE,
  PACKET_NEW_YEAR,
  PACKET_TILE_INFO,
  PACKET_SELECT_RACE,
  PACKET_ALLOC_RACE,
  PACKET_SHOW_MESSAGE,
  PACKET_PLAYER_INFO,
  PACKET_GAME_INFO,
  PACKET_MAP_INFO,
  PACKET_CHAT_MSG,
  PACKET_CITY_INFO,
  PACKET_CITY_SELL,
  PACKET_CITY_BUY,
  PACKET_CITY_CHANGE,
  PACKET_CITY_MAKE_SPECIALIST,
  PACKET_CITY_MAKE_WORKER,
  PACKET_CITY_CHANGE_SPECIALIST,
  PACKET_CITY_RENAME,
  PACKET_PLAYER_RATES,
  PACKET_PLAYER_REVOLUTION,
  PACKET_PLAYER_GOVERNMENT,
  PACKET_PLAYER_RESEARCH,
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
  PACKET_PLAYER_LAUNCH_SPACESHIP,
  PACKET_RULESET_TECH,
  PACKET_RULESET_UNIT,
  PACKET_RULESET_BUILDING
};

enum report_type {
  REPORT_WONDERS_OF_THE_WORLD,
  REPORT_TOP_5_CITIES,
  REPORT_DEMOGRAPHIC,
  REPORT_SERVER_OPTIONS,   /* for backward-compatibility with old servers */
  REPORT_SERVER_OPTIONS1,
  REPORT_SERVER_OPTIONS2
};

/*********************************************************
  diplomat action!
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
  char name[MAX_LENGTH_NAME];
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
  char name[MAX_LENGTH_NAME];            /* rename */
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
  int ai;
  int fuel;
  int goto_dest_x, goto_dest_y;
};



/*********************************************************
...
*********************************************************/
struct packet_city_info {
  int id;
  int owner;
  int x, y;
  char name[MAX_LENGTH_NAME];

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

  char improvements[B_LAST+1];
  char city_map[CITY_MAP_SIZE*CITY_MAP_SIZE+1];

  int did_buy, did_sell;
  int was_happy;
  int airlift;
  int diplomat_investigate;
};



/*********************************************************
 this packet is the very first packet send by the client.
 the player hasn't been accepted yet.
*********************************************************/
struct packet_req_join_game {
  char name[NAME_SIZE];
  int major_version;
  int minor_version;
  int patch_version;
  char capability[MSG_SIZE];
};


/*********************************************************
 ... and the server replies.
*********************************************************/
struct packet_join_game_reply {
  int you_can_join;             /* true/false */
  char message[MSG_SIZE];
  char capability[MSG_SIZE];
};


/*********************************************************
...
*********************************************************/
struct packet_alloc_race {
  int race_no;
  char name[MAX_LENGTH_NAME];
};


/*********************************************************
 this structure is a generic packet, which is used by a great
 number of different packets. In general it's used by all
 packets, which only requires a message(apart from the type).
 blah blah..
*********************************************************/
struct packet_generic_message {
  char message[MSG_SIZE];
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
  char name[MAX_LENGTH_NAME];
  int government;
  int embassy;
  int race;
  int turn_done, nturns_idle;
  int is_alive;
  int gold, tax, science, luxury;
  int researched;
  int researchpoints;
  int researching;
  int future_tech;
  int tech_goal;
  unsigned char inventions[A_LAST+1];
  int is_connected;
  char addr[MAX_LENGTH_ADDRESS];
  int revolution;
  int ai;
  char capability[MSG_SIZE];
  int structurals;
  int components;
  int modules;
  int sship_state;
  int arrival_year;
};

/*********************************************************
Specify all the fields of a struct unit_type
*********************************************************/
struct packet_ruleset_unit {
  int id;			/* index for unit_types[] */
  char name[MAX_LENGTH_NAME];
  int graphics;
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
};

struct packet_ruleset_tech {
  int id, req[2];		/* indices for advances[] */
  char name[MAX_LENGTH_NAME];
};

struct packet_ruleset_building {
  int id;			/* index for improvement_types[] */
  char name[MAX_LENGTH_NAME];
  int is_wonder;
  int tech_requirement;
  int build_cost;
  int shield_upkeep;
  int obsolete_by;
  int variant;
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
  int rail_food, rail_trade, rail_prod; 
  int global_advances[A_LAST];
  int global_wonders[B_LAST];
  int foodbox;
  int techpenalty;
  int spacerace;
  int aqueduct_size;
  int sewer_size;
  struct {
    int get_bonus_tech;
    int boat_fast;
    int cathedral_plus;
    int cathedral_minus;
    int colosseum_plus;
  } rtech;
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
this is where the data is first collected, whenever it
arrives to the client/server.
*********************************************************/
struct socket_packet_buffer {
  int ndata;
  int do_buffer_sends;
  unsigned char data[10*MAX_PACKET_SIZE];
};



struct connection {
  int sock, used;
  char *player; 
  struct socket_packet_buffer buffer;
  struct socket_packet_buffer send_buffer;
  char addr[ADDR_LENGTH];
  char capability[MSG_SIZE];
  /* "capability" gives the capability string of the executable (be it
   * a client or server) at the other end of the connection.
   */
};


int read_socket_data(int sock, struct socket_packet_buffer *buffer);
int send_connection_data(struct connection *pc, unsigned char *data, int len);

unsigned char *get_int8(unsigned char *buffer, int *val);
unsigned char *put_int8(unsigned char *buffer, int val);
unsigned char *get_int16(unsigned char *buffer, int *val);
unsigned char *put_int16(unsigned char *buffer, int val);
unsigned char *get_int32(unsigned char *buffer, int *val);
unsigned char *put_int32(unsigned char *buffer, int val);
unsigned char *put_string(unsigned char *buffer, char *mystring);
unsigned char *get_string(unsigned char *buffer, char *mystring);

int send_packet_diplomacy_info(struct connection *pc, enum packet_type pt,
			       struct packet_diplomacy_info *packet);
struct packet_diplomacy_info *
recieve_packet_diplomacy_info(struct connection *pc);




int send_packet_diplomat_action(struct connection *pc, 
				struct packet_diplomat_action *packet);
struct packet_diplomat_action *
recieve_packet_diplomat_action(struct connection *pc);

int send_packet_nuke_tile(struct connection *pc, 
			  struct packet_nuke_tile *packet);
struct packet_nuke_tile *
recieve_packet_nuke_tile(struct connection *pc);


int send_packet_unit_combat(struct connection *pc, 
			    struct packet_unit_combat *packet);
struct packet_unit_combat *
recieve_packet_unit_combat(struct connection *pc);


int send_packet_tile_info(struct connection *pc, 
			 struct packet_tile_info *pinfo);
struct packet_tile_info *recieve_packet_tile_info(struct connection *pc);

int send_packet_map_info(struct connection *pc, 
			  struct packet_map_info *pinfo);
struct packet_map_info *recieve_packet_map_info(struct connection *pc);

int send_packet_game_info(struct connection *pc, 
			  struct packet_game_info *pinfo);
struct packet_game_info *recieve_packet_game_info(struct connection *pc);


struct packet_player_info *recieve_packet_player_info(struct connection *pc);
int send_packet_player_info(struct connection *pc, 
			    struct packet_player_info *pinfo);


int send_packet_new_year(struct connection *pc, 
			 struct packet_new_year *request);
struct packet_new_year *recieve_packet_new_year(struct connection *pc);

int send_packet_move_unit(struct connection *pc, 
			  struct packet_move_unit *request);
struct packet_move_unit *recieve_packet_move_unit(struct connection *pc);


int send_packet_unit_info(struct connection *pc,
			  struct packet_unit_info *req);
struct packet_unit_info *recieve_packet_unit_info(struct connection *pc);

int send_packet_req_join_game(struct connection *pc, 
			      struct packet_req_join_game *request);
struct packet_req_join_game *recieve_packet_req_join_game(struct 
							  connection *pc);

int send_packet_join_game_reply(struct connection *pc, 
			       struct packet_join_game_reply *reply);
struct packet_join_game_reply *recieve_packet_join_game_reply(struct 
							      connection *pc);

int send_packet_alloc_race(struct connection *pc, 
			   struct packet_alloc_race *packet);
struct packet_alloc_race *recieve_packet_alloc_race(struct connection *pc);


int send_packet_generic_message(struct connection *pc, int type,
				struct packet_generic_message *message);
struct packet_generic_message *recieve_packet_generic_message(struct 
							      connection *pc);

int send_packet_generic_integer(struct connection *pc, int type,
				struct packet_generic_integer *packet);
struct packet_generic_integer *recieve_packet_generic_integer(struct 
							      connection *pc);


int send_packet_city_info(struct connection *pc,struct packet_city_info *req);
struct packet_city_info *recieve_packet_city_info(struct connection *pc);

int send_packet_city_request(struct connection *pc, 
			     struct packet_city_request *packet, enum packet_type);
struct packet_city_request *
recieve_packet_city_request(struct connection *pc);


int send_packet_player_request(struct connection *pc, 
			       struct packet_player_request *packet,
			       enum packet_type req_type);
struct packet_player_request *
recieve_packet_player_request(struct connection *pc);

struct packet_unit_request *
recieve_packet_unit_request(struct connection *pc);
int send_packet_unit_request(struct connection *pc, 
			     struct packet_unit_request *packet,
			     enum packet_type req_type);

int send_packet_before_new_year(struct connection *pc);
struct packet_before_new_year *recieve_packet_before_new_year(struct connection *pc);

int send_packet_unittype_info(struct connection *pc, int type, int action);
struct packet_unittype_info *recieve_packet_unittype_info(struct connection *pc);

int send_packet_ruleset_unit(struct connection *pc,
			     struct packet_ruleset_unit *packet);
struct packet_ruleset_unit *
recieve_packet_ruleset_unit(struct connection *pc);

int send_packet_ruleset_tech(struct connection *pc,
			     struct packet_ruleset_tech *packet);
struct packet_ruleset_tech *
recieve_packet_ruleset_tech(struct connection *pc);

int send_packet_ruleset_building(struct connection *pc,
			     struct packet_ruleset_building *packet);
struct packet_ruleset_building *
recieve_packet_ruleset_building(struct connection *pc);

int send_packet_before_end_year(struct connection *pc);

int send_packet_generic_values(struct connection *pc, int type,
			       struct packet_generic_values *req);
struct packet_generic_values *
recieve_packet_generic_values(struct connection *pc);

void *get_packet_from_connection(struct connection *pc, int *ptype);
void remove_packet_from_buffer(struct socket_packet_buffer *buffer);
void flush_connection_send_buffer(struct connection *pc);
void connection_do_buffer(struct connection *pc);
void connection_do_unbuffer(struct connection *pc);

#endif

