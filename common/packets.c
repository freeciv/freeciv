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
#include <unistd.h>

#include <packets.h>
#include <log.h>


/**************************************************************************
...
**************************************************************************/
void *get_packet_from_connection(struct connection *pc, int *ptype)
{
  int len, type;

  if(pc->buffer.ndata<4)
    return NULL;           /* length and type not read */

  get_int16(pc->buffer.data, &len);
  get_int16(pc->buffer.data+2, &type);

  if(len > pc->buffer.ndata)
    return NULL;           /* not all data has been read */

  *ptype=type;

  switch(type) {

  case PACKET_REQUEST_JOIN_GAME:
    return recieve_packet_req_join_game(pc);

  case PACKET_REPLY_JOIN_GAME_ACCEPT:
    return recieve_packet_generic_message(pc);
    
  case PACKET_REPLY_JOIN_GAME_REJECT:
    return recieve_packet_generic_message(pc);

  case PACKET_SERVER_SHUTDOWN:
    return recieve_packet_generic_message(pc);
           
  case PACKET_UNIT_INFO:
    return recieve_packet_unit_info(pc);

   case PACKET_CITY_INFO:
    return recieve_packet_city_info(pc);

  case PACKET_MOVE_UNIT:
    return recieve_packet_move_unit(pc);

  case PACKET_TURN_DONE:
    return recieve_packet_generic_message(pc);

  case PACKET_BEFORE_NEW_YEAR:
    return recieve_packet_before_new_year(pc);
  case PACKET_NEW_YEAR:
    return recieve_packet_new_year(pc);

  case PACKET_TILE_INFO:
    return recieve_packet_tile_info(pc);

  case PACKET_SELECT_RACE:
  case PACKET_REMOVE_UNIT:
  case PACKET_REMOVE_CITY:
  case PACKET_GAME_STATE:
  case PACKET_REPORT_REQUEST:
  case PACKET_REMOVE_PLAYER:  
    return recieve_packet_generic_integer(pc);
    
  case PACKET_ALLOC_RACE:
    return recieve_packet_alloc_race(pc);

  case PACKET_SHOW_MESSAGE:
    return recieve_packet_generic_message(pc);

  case PACKET_PLAYER_INFO:
    return recieve_packet_player_info(pc);

  case PACKET_GAME_INFO:
    return recieve_packet_game_info(pc);

  case PACKET_MAP_INFO:
    return recieve_packet_map_info(pc);

  case PACKET_CHAT_MSG:
  case PACKET_PAGE_MSG:
    return recieve_packet_generic_message(pc);
    
  case PACKET_CITY_SELL:
  case PACKET_CITY_BUY:
  case PACKET_CITY_CHANGE:
  case PACKET_CITY_MAKE_SPECIALIST:
  case PACKET_CITY_MAKE_WORKER:
  case PACKET_CITY_CHANGE_SPECIALIST:
  case PACKET_CITY_RENAME:
    return recieve_packet_city_request(pc);

  case PACKET_PLAYER_RATES:
  case PACKET_PLAYER_REVOLUTION:
  case PACKET_PLAYER_GOVERNMENT:
  case PACKET_PLAYER_RESEARCH:
  case PACKET_PLAYER_TECH_GOAL:
    return recieve_packet_player_request(pc);

  case PACKET_UNIT_BUILD_CITY:
  case PACKET_UNIT_DISBAND:
  case PACKET_UNIT_CHANGE_HOMECITY:
  case PACKET_UNIT_ESTABLISH_TRADE:
  case PACKET_UNIT_HELP_BUILD_WONDER:
  case PACKET_UNIT_GOTO_TILE:
  case PACKET_UNIT_AUTO:
  case PACKET_UNIT_UNLOAD:
    return recieve_packet_unit_request(pc);
  case PACKET_UNIT_UPGRADE:
    return recieve_packet_unittype_info(pc);
  case PACKET_UNIT_COMBAT:
    return recieve_packet_unit_combat(pc);
  case PACKET_NUKE_TILE:
    return recieve_packet_nuke_tile(pc);
  case PACKET_DIPLOMAT_ACTION:
    return recieve_packet_diplomat_action(pc);

  case PACKET_DIPLOMACY_INIT_MEETING:
  case PACKET_DIPLOMACY_CREATE_CLAUSE:
  case PACKET_DIPLOMACY_REMOVE_CLAUSE:
  case PACKET_DIPLOMACY_CANCEL_MEETING:
  case PACKET_DIPLOMACY_ACCEPT_TREATY:
  case PACKET_DIPLOMACY_SIGN_TREATY:
    return recieve_packet_diplomacy_info(pc);
  };

  log(LOG_FATAL, "unknown packet type in get_packet_from_connection()");
  return NULL;
}




/**************************************************************************
...
**************************************************************************/
unsigned char *get_int16(unsigned char *buffer, int *val)
{
  if(val) {
    int myval=(*buffer)+((*(buffer+1))<<8);
    *val=myval;
  }
  return buffer+2;
}


/**************************************************************************
...
**************************************************************************/
unsigned char *put_int16(unsigned char *buffer, int val)
{
  *buffer++=val&0xff;
  *buffer++=(val&0xff00)>>8;
  return buffer;
}


/**************************************************************************
...
**************************************************************************/
unsigned char *get_int32(unsigned char *buffer, int *val)
{
  if(val) {
    int myval=(*buffer)+((*(buffer+1))<<8)+
      ((*(buffer+2))<<16)+((*(buffer+3))<<24);
    *val=myval;
  }
  return buffer+4;
}



/**************************************************************************
...
**************************************************************************/
unsigned char *put_int32(unsigned char *buffer, int val)
{
  *buffer++=val&0xff;
  *buffer++=(val&0xff00)>>8;
  *buffer++=(val&0xff0000)>>16;
  *buffer++=(val&0xff000000)>>24;
  return buffer;
}

/**************************************************************************
...
**************************************************************************/
unsigned char *put_string(unsigned char *buffer, char *mystring)
{
  strcpy(buffer, mystring);
  return buffer+strlen(mystring)+1;
}


/**************************************************************************
...
**************************************************************************/
unsigned char *get_string(unsigned char *buffer, char *mystring)
{
  if(mystring)
    strcpy(mystring, buffer);
  return buffer+strlen(mystring)+1;
}

/*************************************************************************
...
**************************************************************************/
int send_packet_diplomacy_info(struct connection *pc, enum packet_type pt,
			       struct packet_diplomacy_info *packet)
{
  unsigned char buffer[MAX_PACKET_SIZE], *cptr;
  cptr=put_int16(buffer+2, pt);
  
  cptr=put_int32(cptr, packet->plrno0);
  cptr=put_int32(cptr, packet->plrno1);
  cptr=put_int32(cptr, packet->plrno_from);
  cptr=put_int32(cptr, packet->clause_type);
  cptr=put_int32(cptr, packet->value);
  put_int16(buffer, cptr-buffer);

  return send_connection_data(pc, buffer, cptr-buffer);
}

/*************************************************************************
...
**************************************************************************/
struct packet_diplomacy_info *
recieve_packet_diplomacy_info(struct connection *pc)
{
  unsigned char *cptr;
  struct packet_diplomacy_info *preq=
    malloc(sizeof(struct packet_diplomacy_info));

  cptr=get_int16(pc->buffer.data, NULL);
  cptr=get_int16(cptr, NULL);

  cptr=get_int32(cptr, &preq->plrno0);
  cptr=get_int32(cptr, &preq->plrno1);
  cptr=get_int32(cptr, &preq->plrno_from);
  cptr=get_int32(cptr, &preq->clause_type);
  cptr=get_int32(cptr, &preq->value);
  remove_packet_from_buffer(&pc->buffer);
  return preq;
}


/*************************************************************************
...
**************************************************************************/
int send_packet_diplomat_action(struct connection *pc, 
				struct packet_diplomat_action *packet)
{
  unsigned char buffer[MAX_PACKET_SIZE], *cptr;
  cptr=put_int16(buffer+2, PACKET_DIPLOMAT_ACTION);
  
  cptr=put_int32(cptr, packet->action_type);
  cptr=put_int32(cptr, packet->diplomat_id);
  cptr=put_int32(cptr, packet->target_id);
  put_int16(buffer, cptr-buffer);

  return send_connection_data(pc, buffer, cptr-buffer);
}

/*************************************************************************
...
**************************************************************************/
struct packet_diplomat_action *
recieve_packet_diplomat_action(struct connection *pc)
{
  unsigned char *cptr;
  struct packet_diplomat_action *preq=
    malloc(sizeof(struct packet_diplomat_action));

  cptr=get_int16(pc->buffer.data, NULL);
  cptr=get_int16(cptr, NULL);

  cptr=get_int32(cptr, &preq->action_type);
  cptr=get_int32(cptr, &preq->diplomat_id);
  cptr=get_int32(cptr, &preq->target_id);
  remove_packet_from_buffer(&pc->buffer);
  return preq;
}

/*************************************************************************
...
**************************************************************************/
int send_packet_nuke_tile(struct connection *pc, 
			  struct packet_nuke_tile *packet)
{
  unsigned char buffer[MAX_PACKET_SIZE], *cptr;
  cptr=put_int16(buffer+2, PACKET_NUKE_TILE);
  
  cptr=put_int32(cptr, packet->x);
  cptr=put_int32(cptr, packet->y);
  put_int16(buffer, cptr-buffer);

  return send_connection_data(pc, buffer, cptr-buffer);
}


/*************************************************************************
...
**************************************************************************/
struct packet_nuke_tile *
recieve_packet_nuke_tile(struct connection *pc)
{
  unsigned char *cptr;
  struct packet_nuke_tile *preq=
    malloc(sizeof(struct packet_nuke_tile));

  cptr=get_int16(pc->buffer.data, NULL);
  cptr=get_int16(cptr, NULL);

  cptr=get_int32(cptr, &preq->x);
  cptr=get_int32(cptr, &preq->y);
  remove_packet_from_buffer(&pc->buffer);
  return preq;
}




/*************************************************************************
...
**************************************************************************/
int send_packet_unit_combat(struct connection *pc, 
			    struct packet_unit_combat *packet)
{
  unsigned char buffer[MAX_PACKET_SIZE], *cptr;
  cptr=put_int16(buffer+2, PACKET_UNIT_COMBAT);
  
  cptr=put_int32(cptr, packet->attacker_unit_id);
  cptr=put_int32(cptr, packet->defender_unit_id);
  cptr=put_int32(cptr, packet->attacker_hp);
  cptr=put_int32(cptr, packet->defender_hp);
  cptr=put_int32(cptr, packet->make_winner_veteran);
  put_int16(buffer, cptr-buffer);

  return send_connection_data(pc, buffer, cptr-buffer);
}


/*************************************************************************
...
**************************************************************************/
struct packet_unit_combat *
recieve_packet_unit_combat(struct connection *pc)
{
  unsigned char *cptr;
  struct packet_unit_combat *preq=
    malloc(sizeof(struct packet_unit_combat));

  cptr=get_int16(pc->buffer.data, NULL);
  cptr=get_int16(cptr, NULL);

  cptr=get_int32(cptr, &preq->attacker_unit_id);
  cptr=get_int32(cptr, &preq->defender_unit_id);
  cptr=get_int32(cptr, &preq->attacker_hp);
  cptr=get_int32(cptr, &preq->defender_hp);
  cptr=get_int32(cptr, &preq->make_winner_veteran);
  remove_packet_from_buffer(&pc->buffer);
  return preq;
}












/*************************************************************************
...
**************************************************************************/
int send_packet_unit_request(struct connection *pc, 
			     struct packet_unit_request *packet,
			     enum packet_type req_type)
{
  unsigned char buffer[MAX_PACKET_SIZE], *cptr;
  cptr=put_int16(buffer+2, req_type);
  cptr=put_int32(cptr, packet->unit_id);
  cptr=put_int32(cptr, packet->city_id);
  cptr=put_int32(cptr, packet->x);
  cptr=put_int32(cptr, packet->y);
  cptr=put_string(cptr, packet->name);
  put_int16(buffer, cptr-buffer);

  return send_connection_data(pc, buffer, cptr-buffer);
}


/*************************************************************************
...
**************************************************************************/
struct packet_unit_request *
recieve_packet_unit_request(struct connection *pc)
{
  unsigned char *cptr;
  struct packet_unit_request *preq=
    malloc(sizeof(struct packet_unit_request));

  cptr=get_int16(pc->buffer.data, NULL);
  cptr=get_int16(cptr, NULL);
  cptr=get_int32(cptr, &preq->unit_id);
  cptr=get_int32(cptr, &preq->city_id);
  cptr=get_int32(cptr, &preq->x);
  cptr=get_int32(cptr, &preq->y);
  cptr=get_string(cptr, preq->name);
  remove_packet_from_buffer(&pc->buffer);
  return preq;
}



/*************************************************************************
...
**************************************************************************/
int send_packet_player_request(struct connection *pc, 
			       struct packet_player_request *packet,
			       enum packet_type req_type)
{
  unsigned char buffer[MAX_PACKET_SIZE], *cptr;
  cptr=put_int16(buffer+2, req_type);
  cptr=put_int32(cptr, packet->tax);
  cptr=put_int32(cptr, packet->luxury);
  cptr=put_int32(cptr, packet->science);
  cptr=put_int32(cptr, packet->government);
  cptr=put_int32(cptr, packet->tech);
  put_int16(buffer, cptr-buffer);

  return send_connection_data(pc, buffer, cptr-buffer);
}


/*************************************************************************
...
**************************************************************************/
struct packet_player_request *
recieve_packet_player_request(struct connection *pc)
{
  unsigned char *cptr;
  struct packet_player_request *preq=
    malloc(sizeof(struct packet_player_request));

  cptr=get_int16(pc->buffer.data, NULL);
  cptr=get_int16(cptr, NULL);
  cptr=get_int32(cptr, &preq->tax);
  cptr=get_int32(cptr, &preq->luxury);
  cptr=get_int32(cptr, &preq->science);
  cptr=get_int32(cptr, &preq->government);
  cptr=get_int32(cptr, &preq->tech);
  remove_packet_from_buffer(&pc->buffer);
  return preq;
}





/*************************************************************************
...
**************************************************************************/
int send_packet_city_request(struct connection *pc, 
			     struct packet_city_request *packet,
			     enum packet_type req_type)
{
  unsigned char buffer[MAX_PACKET_SIZE], *cptr;
  cptr=put_int16(buffer+2, req_type);
  cptr=put_int32(cptr, packet->city_id);
  cptr=put_int32(cptr, packet->build_id);
  cptr=put_int32(cptr, packet->is_build_id_unit_id);
  cptr=put_int32(cptr, packet->worker_x);
  cptr=put_int32(cptr, packet->worker_y);
  cptr=put_int32(cptr, packet->specialist_from);
  cptr=put_int32(cptr, packet->specialist_to);
  cptr=put_string(cptr, packet->name);
  put_int16(buffer, cptr-buffer);

  return send_connection_data(pc, buffer, cptr-buffer);
}


/*************************************************************************
...
**************************************************************************/
struct packet_city_request *
recieve_packet_city_request(struct connection *pc)
{
  unsigned char *cptr;
  struct packet_city_request *preq=
    malloc(sizeof(struct packet_city_request));

  cptr=get_int16(pc->buffer.data, NULL);
  cptr=get_int16(cptr, NULL);

  cptr=get_int32(cptr, &preq->city_id);
  cptr=get_int32(cptr, &preq->build_id);
  cptr=get_int32(cptr, &preq->is_build_id_unit_id);
  cptr=get_int32(cptr, &preq->worker_x);
  cptr=get_int32(cptr, &preq->worker_y);
  cptr=get_int32(cptr, &preq->specialist_from);
  cptr=get_int32(cptr, &preq->specialist_to);
  cptr=get_string(cptr, preq->name);
  
  remove_packet_from_buffer(&pc->buffer);
  return preq;
}


/*************************************************************************
...
**************************************************************************/
int send_packet_player_info(struct connection *pc, struct packet_player_info *pinfo)
{
  unsigned char buffer[MAX_PACKET_SIZE], *cptr;
  cptr=put_int16(buffer+2, PACKET_PLAYER_INFO);
  cptr=put_int32(cptr, pinfo->playerno);
  cptr=put_string(cptr, pinfo->name);
  cptr=put_int32(cptr, pinfo->government);
  cptr=put_int32(cptr, pinfo->embassy);
  cptr=put_int32(cptr, pinfo->race);
  cptr=put_int32(cptr, pinfo->turn_done);
  cptr=put_int32(cptr, pinfo->nturns_idle);
  cptr=put_int32(cptr, pinfo->is_alive);
  
  cptr=put_int32(cptr, pinfo->gold);
  cptr=put_int32(cptr, pinfo->tax);
  cptr=put_int32(cptr, pinfo->science);
  cptr=put_int32(cptr, pinfo->luxury);

  cptr=put_int32(cptr, pinfo->researched);
  cptr=put_int32(cptr, pinfo->researchpoints);
  cptr=put_int32(cptr, pinfo->researching);
  cptr=put_string(cptr, (char*)pinfo->inventions);
  cptr=put_int32(cptr, pinfo->future_tech);
  
  cptr=put_int32(cptr, pinfo->is_connected);
  cptr=put_string(cptr, pinfo->addr);
  cptr=put_int32(cptr, pinfo->revolution);
  cptr=put_int32(cptr, pinfo->tech_goal);

  put_int16(buffer, cptr-buffer);

  return send_connection_data(pc, buffer, cptr-buffer);
}


/*************************************************************************
...
**************************************************************************/
struct packet_player_info *
recieve_packet_player_info(struct connection *pc)
{
  unsigned char *cptr;
  int length;
  struct packet_player_info *pinfo=
     malloc(sizeof(struct packet_player_info));

  cptr=get_int16(pc->buffer.data, &length);
  cptr=get_int16(cptr, NULL);

  cptr=get_int32(cptr,  &pinfo->playerno);
  cptr=get_string(cptr, pinfo->name);
  cptr=get_int32(cptr,  &pinfo->government);
  cptr=get_int32(cptr,  &pinfo->embassy);
  cptr=get_int32(cptr,  &pinfo->race);
  cptr=get_int32(cptr,  &pinfo->turn_done);
  cptr=get_int32(cptr,  &pinfo->nturns_idle);
  cptr=get_int32(cptr,  &pinfo->is_alive);
  
  cptr=get_int32(cptr, &pinfo->gold);
  cptr=get_int32(cptr, &pinfo->tax);
  cptr=get_int32(cptr, &pinfo->science);
  cptr=get_int32(cptr, &pinfo->luxury);

  cptr=get_int32(cptr, &pinfo->researched);
  cptr=get_int32(cptr, &pinfo->researchpoints);
  cptr=get_int32(cptr, &pinfo->researching);
  cptr=get_string(cptr, (char*)pinfo->inventions);
  cptr=get_int32(cptr, &pinfo->future_tech);

  cptr=get_int32(cptr,  &pinfo->is_connected);
  cptr=get_string(cptr, pinfo->addr);
  cptr=get_int32(cptr,  &pinfo->revolution);
  if(pc->buffer.data+length-cptr >= 4)  {
    cptr=get_int32(cptr, &pinfo->tech_goal);
  } else {
    pinfo->tech_goal=A_NONE;
  }

  remove_packet_from_buffer(&pc->buffer);
  return pinfo;
}




/*************************************************************************
...
**************************************************************************/
int send_packet_game_info(struct connection *pc, 
			  struct packet_game_info *pinfo)
{
  unsigned char buffer[MAX_PACKET_SIZE], *cptr;
  int i;
  
  cptr=put_int16(buffer+2, PACKET_GAME_INFO);
  cptr=put_int32(cptr, pinfo->gold);
  cptr=put_int32(cptr, pinfo->tech);
  cptr=put_int32(cptr, pinfo->techlevel);

  cptr=put_int32(cptr, pinfo->skill_level);
  cptr=put_int32(cptr, pinfo->timeout);
  cptr=put_int32(cptr, pinfo->end_year);
  cptr=put_int32(cptr, pinfo->year);
  cptr=put_int32(cptr, pinfo->min_players);
  cptr=put_int32(cptr, pinfo->max_players);
  cptr=put_int32(cptr, pinfo->nplayers);
  cptr=put_int32(cptr, pinfo->player_idx);
  cptr=put_int32(cptr, pinfo->globalwarming);
  cptr=put_int32(cptr, pinfo->heating);
  cptr=put_int32(cptr, pinfo->cityfactor);
  cptr=get_int32(cptr, &pinfo->diplcost);
  cptr=get_int32(cptr, &pinfo->freecost);
  cptr=get_int32(cptr, &pinfo->conquercost);
  cptr=put_int32(cptr, pinfo->unhappysize);
  cptr=put_int32(cptr, pinfo->rail_food);
  cptr=put_int32(cptr, pinfo->rail_prod);
  cptr=put_int32(cptr, pinfo->rail_trade);
  

  for(i=0; i<A_LAST; i++)
    cptr=put_int32(cptr, pinfo->global_advances[i]);
  for(i=0; i<B_LAST; i++)
    cptr=put_int32(cptr, pinfo->global_wonders[i]);
  cptr=put_int32(cptr, pinfo->techpenalty);
  cptr=put_int32(cptr, pinfo->foodbox);
  cptr=put_int32(cptr, pinfo->civstyle);
  put_int16(buffer, cptr-buffer);

  return send_connection_data(pc, buffer, cptr-buffer);
}

/*************************************************************************
...
**************************************************************************/
struct packet_game_info *recieve_packet_game_info(struct connection *pc)
{
  int i;
  unsigned char *cptr;
  struct packet_game_info *pinfo=
     malloc(sizeof(struct packet_game_info));

  cptr=get_int16(pc->buffer.data, NULL);
  cptr=get_int16(cptr, NULL);

  cptr=get_int32(cptr, &pinfo->gold);
  cptr=get_int32(cptr, &pinfo->tech);
  cptr=get_int32(cptr, &pinfo->techlevel);
  cptr=get_int32(cptr, &pinfo->skill_level);
  cptr=get_int32(cptr, &pinfo->timeout);
  cptr=get_int32(cptr, &pinfo->end_year);
  cptr=get_int32(cptr, &pinfo->year);
  cptr=get_int32(cptr, &pinfo->min_players);
  cptr=get_int32(cptr, &pinfo->max_players);
  cptr=get_int32(cptr, &pinfo->nplayers);
  cptr=get_int32(cptr, &pinfo->player_idx);
  cptr=get_int32(cptr, &pinfo->globalwarming);
  cptr=get_int32(cptr, &pinfo->heating);

  cptr=get_int32(cptr, &pinfo->cityfactor);
  cptr=get_int32(cptr, &pinfo->diplcost);
  cptr=get_int32(cptr, &pinfo->freecost);
  cptr=get_int32(cptr, &pinfo->conquercost);
  cptr=get_int32(cptr, &pinfo->unhappysize);
  cptr=get_int32(cptr, &pinfo->rail_food);
  cptr=get_int32(cptr, &pinfo->rail_prod);
  cptr=get_int32(cptr, &pinfo->rail_trade);
  
  for(i=0; i<A_LAST; i++)
    cptr=get_int32(cptr, &pinfo->global_advances[i]);
  for(i=0; i<B_LAST; i++)
    cptr=get_int32(cptr, &pinfo->global_wonders[i]);
  cptr=get_int32(cptr, &pinfo->techpenalty);
  cptr=get_int32(cptr, &pinfo->foodbox);
  cptr=get_int32(cptr, &pinfo->civstyle);

  remove_packet_from_buffer(&pc->buffer);
  return pinfo;
}

/*************************************************************************
...
**************************************************************************/
int send_packet_map_info(struct connection *pc, 
			 struct packet_map_info *pinfo)
{
  unsigned char buffer[MAX_PACKET_SIZE], *cptr;

  cptr=put_int16(buffer+2, PACKET_MAP_INFO);
  cptr=put_int32(cptr, pinfo->xsize);
  cptr=put_int32(cptr, pinfo->ysize);
  cptr=put_int32(cptr, pinfo->is_earth);
  put_int16(buffer, cptr-buffer);

  return send_connection_data(pc, buffer, cptr-buffer);
}

/*************************************************************************
...
**************************************************************************/
struct packet_map_info *recieve_packet_map_info(struct connection *pc)
{
  unsigned char *cptr;
  struct packet_map_info *pinfo=
     malloc(sizeof(struct packet_map_info));

  cptr=get_int16(pc->buffer.data, NULL);
  cptr=get_int16(cptr, NULL);

  cptr=get_int32(cptr, &pinfo->xsize);
  cptr=get_int32(cptr, &pinfo->ysize);
  cptr=get_int32(cptr, &pinfo->is_earth);

  remove_packet_from_buffer(&pc->buffer);
  return pinfo;
}

/*************************************************************************
...
**************************************************************************/
struct packet_tile_info *
recieve_packet_tile_info(struct connection *pc)
{
  unsigned char *cptr;
  struct packet_tile_info *packet=
    malloc(sizeof(struct packet_tile_info));

  cptr=get_int16(pc->buffer.data, NULL);
  cptr=get_int16(cptr, NULL);
  cptr=get_int32(cptr, &packet->x);
  cptr=get_int32(cptr, &packet->y);
  cptr=get_int32(cptr, &packet->type);
  cptr=get_int32(cptr, &packet->special);
  cptr=get_int32(cptr, &packet->known);
  remove_packet_from_buffer(&pc->buffer);

  return packet;
}

struct packet_unittype_info *
recieve_packet_unittype_info(struct connection *pc)
{
  unsigned char *cptr;
  struct packet_unittype_info *packet=
    malloc(sizeof(struct packet_unittype_info));

  cptr=get_int16(pc->buffer.data, NULL);
  cptr=get_int16(cptr, NULL);
  cptr=get_int32(cptr, &packet->type);
  cptr=get_int32(cptr, &packet->action);
  remove_packet_from_buffer(&pc->buffer);

  return packet;
}

/*************************************************************************
...
**************************************************************************/
int send_packet_tile_info(struct connection *pc, 
			  struct packet_tile_info *pinfo)

{
  unsigned char buffer[MAX_PACKET_SIZE], *cptr;

  cptr=put_int16(buffer+2, PACKET_TILE_INFO);
  cptr=put_int32(cptr, pinfo->x);
  cptr=put_int32(cptr, pinfo->y);
  cptr=put_int32(cptr, pinfo->type);
  cptr=put_int32(cptr, pinfo->special);
  cptr=put_int32(cptr, pinfo->known);
  put_int16(buffer, cptr-buffer);

  return send_connection_data(pc, buffer, cptr-buffer);
}

/**************************************************************************
...
**************************************************************************/
int send_packet_new_year(struct connection *pc, struct 
			 packet_new_year *request)
{
  unsigned char buffer[MAX_PACKET_SIZE], *cptr;
  cptr=put_int16(buffer+2, PACKET_NEW_YEAR);
  cptr=put_int32(cptr, request->year);
  put_int16(buffer, cptr-buffer);
  return send_connection_data(pc, buffer, cptr-buffer);
}

/**************************************************************************
...
**************************************************************************/
int send_packet_unittype_info(struct connection *pc, int type, int action)
{
  unsigned char buffer[MAX_PACKET_SIZE], *cptr;
  cptr=put_int16(buffer+2, PACKET_UNIT_UPGRADE);
  cptr=put_int32(cptr, type);
  cptr=put_int32(cptr, action);
  put_int16(buffer, cptr-buffer);
  return send_connection_data(pc, buffer, cptr-buffer);
}

/**************************************************************************
...
**************************************************************************/
struct packet_before_new_year *
recieve_packet_before_new_year(struct connection *pc)
{
  unsigned char *cptr;
  struct packet_before_new_year *packet=
    malloc(sizeof(struct packet_before_new_year));

  cptr=get_int16(pc->buffer.data, NULL);
  cptr=get_int16(cptr, NULL);
  remove_packet_from_buffer(&pc->buffer);
  return packet;
}

int send_packet_before_end_year(struct connection *pc)
{
  unsigned char buffer[MAX_PACKET_SIZE], *cptr;
  cptr=put_int16(buffer+2, PACKET_BEFORE_NEW_YEAR);
  put_int16(buffer, cptr-buffer);
  return send_connection_data(pc, buffer, cptr-buffer);
}

/*************************************************************************
...
**************************************************************************/
int send_packet_unit_info(struct connection *pc, 
			    struct packet_unit_info *req)
{
  unsigned char buffer[MAX_PACKET_SIZE], *cptr;
  cptr=put_int16(buffer+2, PACKET_UNIT_INFO);
  cptr=put_int32(cptr, req->id);
  cptr=put_int32(cptr, req->owner);
  cptr=put_int32(cptr, req->x);
  cptr=put_int32(cptr, req->y);
  
  cptr=put_int32(cptr, req->veteran);
  cptr=put_int32(cptr, req->homecity);
  cptr=put_int32(cptr, req->type);
  cptr=put_int32(cptr, req->movesleft);
  cptr=put_int32(cptr, req->hp);
  cptr=put_int32(cptr, req->activity);
  cptr=put_int32(cptr, req->activity_count);
  cptr=put_int32(cptr, req->upkeep);
  cptr=put_int32(cptr, req->unhappiness);
  cptr=put_int32(cptr, req->bribe_cost);
  cptr=put_int32(cptr, req->ai);
  cptr=put_int32(cptr, req->fuel);
  cptr=put_int32(cptr, req->goto_dest_x);
  cptr=put_int32(cptr, req->goto_dest_y);
  
  put_int16(buffer, cptr-buffer);
  return send_connection_data(pc, buffer, cptr-buffer);
}

/*************************************************************************
...
**************************************************************************/
int send_packet_city_info(struct connection *pc, struct packet_city_info *req)
{
  unsigned char buffer[MAX_PACKET_SIZE], *cptr;
  cptr=put_int16(buffer+2, PACKET_CITY_INFO);
  cptr=put_int32(cptr, req->id);
  cptr=put_int32(cptr, req->owner);
  cptr=put_int32(cptr, req->x);
  cptr=put_int32(cptr, req->y);
  cptr=put_string(cptr, req->name);
  
  cptr=put_int32(cptr, req->size);
  cptr=put_int32(cptr, req->ppl_happy);
  cptr=put_int32(cptr, req->ppl_content);
  cptr=put_int32(cptr, req->ppl_unhappy);
  cptr=put_int32(cptr, req->ppl_elvis);
  cptr=put_int32(cptr, req->ppl_scientist);
  cptr=put_int32(cptr, req->ppl_taxman);

  cptr=put_int32(cptr, req->food_prod);
  cptr=put_int32(cptr, req->food_surplus);
  cptr=put_int32(cptr, req->shield_prod);
  cptr=put_int32(cptr, req->shield_surplus);
  cptr=put_int32(cptr, req->trade_prod);
  cptr=put_int32(cptr, req->corruption);

  cptr=put_int32(cptr, req->luxury_total);
  cptr=put_int32(cptr, req->tax_total);
  cptr=put_int32(cptr, req->science_total);

  cptr=put_int32(cptr, req->food_stock);
  cptr=put_int32(cptr, req->shield_stock);
  cptr=put_int32(cptr, req->pollution);
  cptr=put_int32(cptr, req->incite_revolt_cost);

  cptr=put_int32(cptr, req->is_building_unit);
  cptr=put_int32(cptr, req->currently_building);

  cptr=put_int32(cptr, req->did_buy);
  cptr=put_int32(cptr, req->did_sell);

  cptr=put_int32(cptr, req->trade[0]);
  cptr=put_int32(cptr, req->trade[1]);
  cptr=put_int32(cptr, req->trade[2]);
  cptr=put_int32(cptr, req->trade[3]);
  cptr=put_int32(cptr, req->was_happy);
  cptr=put_int32(cptr, req->airlift);
  
  cptr=put_string(cptr, (char*)req->city_map);
  cptr=put_string(cptr, (char*)req->improvements);

  put_int16(buffer, cptr-buffer);

  return send_connection_data(pc, buffer, cptr-buffer);
}

/*************************************************************************
...
**************************************************************************/
struct packet_city_info *
recieve_packet_city_info(struct connection *pc)
{
  unsigned char *cptr;
  struct packet_city_info *packet=
    malloc(sizeof(struct packet_city_info));

  cptr=get_int16(pc->buffer.data, NULL);
  cptr=get_int16(cptr, NULL);
  cptr=get_int32(cptr, &packet->id);
  cptr=get_int32(cptr, &packet->owner);
  cptr=get_int32(cptr, &packet->x);
  cptr=get_int32(cptr, &packet->y);
  cptr=get_string(cptr, packet->name);
  
  cptr=get_int32(cptr, &packet->size);
  cptr=get_int32(cptr, &packet->ppl_happy);
  cptr=get_int32(cptr, &packet->ppl_content);
  cptr=get_int32(cptr, &packet->ppl_unhappy);
  cptr=get_int32(cptr, &packet->ppl_elvis);
  cptr=get_int32(cptr, &packet->ppl_scientist);
  cptr=get_int32(cptr, &packet->ppl_taxman);
  
  cptr=get_int32(cptr, &packet->food_prod);
  cptr=get_int32(cptr, &packet->food_surplus);
  cptr=get_int32(cptr, &packet->shield_prod);
  cptr=get_int32(cptr, &packet->shield_surplus);
  cptr=get_int32(cptr, &packet->trade_prod);
  cptr=get_int32(cptr, &packet->corruption);

  cptr=get_int32(cptr, &packet->luxury_total);
  cptr=get_int32(cptr, &packet->tax_total);
  cptr=get_int32(cptr, &packet->science_total);
  
  cptr=get_int32(cptr, &packet->food_stock);
  cptr=get_int32(cptr, &packet->shield_stock);
  cptr=get_int32(cptr, &packet->pollution);
  cptr=get_int32(cptr, &packet->incite_revolt_cost);
  
  cptr=get_int32(cptr, &packet->is_building_unit);
  cptr=get_int32(cptr, &packet->currently_building);

  cptr=get_int32(cptr, &packet->did_buy);
  cptr=get_int32(cptr, &packet->did_sell);

  cptr=get_int32(cptr, &packet->trade[0]);
  cptr=get_int32(cptr, &packet->trade[1]);
  cptr=get_int32(cptr, &packet->trade[2]);
  cptr=get_int32(cptr, &packet->trade[3]);
  cptr=get_int32(cptr, &packet->was_happy);
  cptr=get_int32(cptr, &packet->airlift);
  
  cptr=get_string(cptr, (char*)packet->city_map);
  cptr=get_string(cptr, (char*)packet->improvements);

  remove_packet_from_buffer(&pc->buffer);

  return packet;
}

/*************************************************************************
...
**************************************************************************/
struct packet_unit_info *
recieve_packet_unit_info(struct connection *pc)
{
  unsigned char *cptr;
  struct packet_unit_info *packet=
    malloc(sizeof(struct packet_unit_info));

  cptr=get_int16(pc->buffer.data, NULL);
  cptr=get_int16(cptr, NULL);
  cptr=get_int32(cptr, &packet->id);
  cptr=get_int32(cptr, &packet->owner);
  cptr=get_int32(cptr, &packet->x);
  cptr=get_int32(cptr, &packet->y);
  cptr=get_int32(cptr, &packet->veteran);
  cptr=get_int32(cptr, &packet->homecity);
  cptr=get_int32(cptr, &packet->type);
  cptr=get_int32(cptr, &packet->movesleft);
  cptr=get_int32(cptr, &packet->hp);
  cptr=get_int32(cptr, &packet->activity);
  cptr=get_int32(cptr, &packet->activity_count);
  cptr=get_int32(cptr, &packet->upkeep);
  cptr=get_int32(cptr, &packet->unhappiness);
  cptr=get_int32(cptr, &packet->bribe_cost);
  cptr=get_int32(cptr, &packet->ai);
  cptr=get_int32(cptr, &packet->fuel);
  cptr=get_int32(cptr, &packet->goto_dest_x);
  cptr=get_int32(cptr, &packet->goto_dest_y);

  remove_packet_from_buffer(&pc->buffer);

  return packet;
}

/**************************************************************************
...
**************************************************************************/
struct packet_new_year *
recieve_packet_new_year(struct connection *pc)
{
  unsigned char *cptr;
  struct packet_new_year *packet=
    malloc(sizeof(struct packet_new_year));

  cptr=get_int16(pc->buffer.data, NULL);
  cptr=get_int16(cptr, NULL);
  cptr=get_int32(cptr, &packet->year);
  remove_packet_from_buffer(&pc->buffer);

  return packet;
}



/**************************************************************************
...
**************************************************************************/
int send_packet_move_unit(struct connection *pc, struct 
			  packet_move_unit *request)
{
  unsigned char buffer[MAX_PACKET_SIZE], *cptr;

  cptr=put_int16(buffer+2, PACKET_MOVE_UNIT);
  cptr=put_int32(cptr, request->x);
  cptr=put_int32(cptr, request->y);
  cptr=put_int32(cptr, request->unid);
  put_int16(buffer, cptr-buffer);

  return send_connection_data(pc, buffer, cptr-buffer);
}



/**************************************************************************
...
**************************************************************************/
struct packet_move_unit *recieve_packet_move_unit(struct connection *pc)
{
  unsigned char *cptr;
  struct packet_move_unit *packet=
    malloc(sizeof(struct packet_move_unit));

  cptr=get_int16(pc->buffer.data, NULL);
  cptr=get_int16(cptr, NULL);
  cptr=get_int32(cptr, &packet->x);
  cptr=get_int32(cptr, &packet->y);
  cptr=get_int32(cptr, &packet->unid);
  remove_packet_from_buffer(&pc->buffer);

  return packet;
}

/**************************************************************************
...
**************************************************************************/
int send_packet_req_join_game(struct connection *pc, struct 
			      packet_req_join_game *request)
{
  unsigned char buffer[MAX_PACKET_SIZE], *cptr;
  cptr=put_int16(buffer+2, PACKET_REQUEST_JOIN_GAME);
  cptr=put_string(cptr, request->name);
  cptr=put_int32(cptr, request->major_version);
  cptr=put_int32(cptr, request->minor_version);
  cptr=put_int32(cptr, request->patch_version);
  put_int16(buffer, cptr-buffer);

  return send_connection_data(pc, buffer, cptr-buffer);
}

/**************************************************************************
...
**************************************************************************/
int send_packet_generic_message(struct connection *pc, int type,
				struct packet_generic_message *packet)
{
  unsigned char buffer[MAX_PACKET_SIZE], *cptr;
  cptr=put_int16(buffer+2, type);
  cptr=put_int32(cptr, packet->x);
  cptr=put_int32(cptr, packet->y);
  cptr=put_int32(cptr, packet->event);

  cptr=put_string(cptr, packet->message);
  put_int16(buffer, cptr-buffer);

  return send_connection_data(pc, buffer, cptr-buffer);
}

/**************************************************************************
...
**************************************************************************/
int send_packet_generic_integer(struct connection *pc, int type,
				struct packet_generic_integer *packet)
{
  unsigned char buffer[MAX_PACKET_SIZE], *cptr;
  cptr=put_int16(buffer+2, type);
  cptr=put_int32(cptr, packet->value);
  put_int16(buffer, cptr-buffer);
  return send_connection_data(pc, buffer, cptr-buffer);
}

/**************************************************************************
...
**************************************************************************/
struct packet_req_join_game *
recieve_packet_req_join_game(struct connection *pc)
{
  unsigned char *cptr;
  struct packet_req_join_game *packet=
    malloc(sizeof(struct packet_req_join_game));

  cptr=get_int16(pc->buffer.data, NULL);
  cptr=get_int16(cptr, NULL);
  cptr=get_string(cptr, packet->name);
  cptr=get_int32(cptr, &packet->major_version);
  cptr=get_int32(cptr, &packet->minor_version);
  cptr=get_int32(cptr, &packet->patch_version);

  remove_packet_from_buffer(&pc->buffer);

  return packet;
}

/**************************************************************************
...
**************************************************************************/
struct packet_generic_message *
recieve_packet_generic_message(struct connection *pc)
{
  unsigned char *cptr;
  struct packet_generic_message *packet=
    malloc(sizeof(struct packet_generic_message));

  cptr=get_int16(pc->buffer.data, NULL);
  cptr=get_int16(cptr, NULL);
  cptr=get_int32(cptr, &packet->x);
  cptr=get_int32(cptr, &packet->y);
  cptr=get_int32(cptr, &packet->event);

  cptr=get_string(cptr, packet->message);
  
  remove_packet_from_buffer(&pc->buffer);

  return packet;
}

/**************************************************************************
...
**************************************************************************/
struct packet_generic_integer *
recieve_packet_generic_integer(struct connection *pc)
{
  unsigned char *cptr;
  struct packet_generic_integer *packet=
    malloc(sizeof(struct packet_generic_integer));

  cptr=get_int16(pc->buffer.data, NULL);
  cptr=get_int16(cptr, NULL);
  cptr=get_int32(cptr, &packet->value);

  remove_packet_from_buffer(&pc->buffer);

  return packet;
}

/**************************************************************************
...
**************************************************************************/
int send_packet_alloc_race(struct connection *pc, 
			   struct packet_alloc_race *packet)
{
  unsigned char buffer[MAX_PACKET_SIZE], *cptr;
  cptr=put_int16(buffer+2, PACKET_ALLOC_RACE);
  cptr=put_int32(cptr, packet->race_no);
  cptr=put_string(cptr, packet->name);
  put_int16(buffer, cptr-buffer);

  return send_connection_data(pc, buffer, cptr-buffer);
}

/**************************************************************************
...
**************************************************************************/
struct packet_alloc_race *
recieve_packet_alloc_race(struct connection *pc)
{
  unsigned char *cptr;
  struct packet_alloc_race *packet=
    malloc(sizeof(struct packet_alloc_race));

  cptr=get_int16(pc->buffer.data, NULL);
  cptr=get_int16(cptr, NULL);
  cptr=get_int32(cptr, &packet->race_no);
  cptr=get_string(cptr, packet->name);

  remove_packet_from_buffer(&pc->buffer);

  return packet;
}

/**************************************************************************
remove the packet from the buffer
**************************************************************************/
void remove_packet_from_buffer(struct socket_packet_buffer *buffer)
{
  int len;
  get_int16(buffer->data, &len);
  memcpy(buffer->data, buffer->data+len, buffer->ndata-len);
  buffer->ndata-=len;
}

/********************************************************************
...
********************************************************************/
void connection_do_buffer(struct connection *pc)
{
  if(pc)
    pc->send_buffer.do_buffer_sends++;
}

/********************************************************************
...
********************************************************************/
void connection_do_unbuffer(struct connection *pc)
{
  if(pc) {
    pc->send_buffer.do_buffer_sends--;
    if(pc->send_buffer.do_buffer_sends==0)
      flush_connection_send_buffer(pc);
  }
}

/********************************************************************
  flush'em
********************************************************************/
void flush_connection_send_buffer(struct connection *pc)
{
  if(pc) {
    if(pc->send_buffer.ndata) {
      if(write(pc->sock, pc->send_buffer.data, pc->send_buffer.ndata)!=
	 pc->send_buffer.ndata) {
	log(LOG_NORMAL, "failed writing data to socket");
      }
      pc->send_buffer.ndata=0;
    }
  }
}


/********************************************************************
  write data to socket
********************************************************************/
int send_connection_data(struct connection *pc, unsigned char *data, int len)
{
  if(pc) {
    if(pc->send_buffer.do_buffer_sends) {
      if(10*MAX_PACKET_SIZE-pc->send_buffer.ndata >= len) { /* room for more?*/
	memcpy(pc->send_buffer.data+pc->send_buffer.ndata, data, len);
	pc->send_buffer.ndata+=len;
      }
      else {
	flush_connection_send_buffer(pc);
	memcpy(pc->send_buffer.data, data, len);
	pc->send_buffer.ndata=len;
      }
    }
    else {
      if(write(pc->sock, data, len)!=len) {
	log(LOG_NORMAL, "failed writing data to socket");
	return -1;
      }
      
    }

  }
  return 0;
}



/********************************************************************
read data from socket, and check if a packet is ready

returns: -1  an error occured - you should close the socket
          >0 : #bytes read 
********************************************************************/
int read_socket_data(int sock, struct socket_packet_buffer *buffer)
{
  int didget;

  if((didget=read(sock, buffer->data+buffer->ndata, 
		  MAX_PACKET_SIZE-buffer->ndata))<=0)
    return -1;
 
  buffer->ndata+=didget;
/*
  printf("didget:%d\n", didget); 
*/     
  return didget;
}


