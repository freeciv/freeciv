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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <unistd.h>

#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif

#if !(defined(GENERATING68K) || defined(GENERATINGPPC)) /* non mac header(s) */
#include <netinet/in.h>
#include <arpa/inet.h>
#endif

#include "capability.h"
#include "log.h"
#include "mem.h"

#include "packets.h"

/* A pack_iter is used to iterate through a packet, while ensuring that
 * we don't read off the end of the packet, by comparing (ptr-base) vs len.
 * Note that (ptr-base) gives the number of bytes read so far, and
 * len gives the total number.
 */
struct pack_iter {
  int len;			/* packet length (as claimed by packet) */
  int type;			/* packet type (only here for log output) */
  unsigned char *base;		/* start of packet */
  unsigned char *ptr;		/* address of next data to pull off */
  int swap_bytes;		/* to deal (minimally) with old versions */
  int short_packet;		/* set to 1 if try to read past end */
  int bad_string;		/* set to 1 if received too-long string */
  int bad_bit_string;		/* set to 1 if received bad bit-string */
};

static void pack_iter_init(struct pack_iter *piter, struct connection *pc);
static void pack_iter_end(struct pack_iter *piter, struct connection *pc);
static int  pack_iter_remaining(struct pack_iter *piter);

/* put_int16 and put_string are non-static for meta.c */

static unsigned char *put_int8(unsigned char *buffer, int val);
static unsigned char *put_int32(unsigned char *buffer, int val);

static unsigned char *put_bit_string(unsigned char *buffer, char *str);
static unsigned char *put_city_map(unsigned char *buffer, char *str);

/* iget = iterator versions */
static void iget_int8(struct pack_iter *piter, int *val);
static void iget_int16(struct pack_iter *piter, int *val);
static void iget_int32(struct pack_iter *piter, int *val);

static void iget_string(struct pack_iter *piter, char *mystring, int navail);
static void iget_bit_string(struct pack_iter *piter, char *str, int navail);
static void iget_city_map(struct pack_iter *piter, char *str, int navail);

/* Use the above iget versions instead of the get versions below,
 * except in special cases --dwp */
static unsigned char *get_int8(unsigned char *buffer, int *val);
static unsigned char *get_int16(unsigned char *buffer, int *val);
static unsigned char *get_int32(unsigned char *buffer, int *val);

static unsigned int swab_int16(unsigned int val);
static unsigned int swab_int32(unsigned int val);
static void swab_pint16(int *ptr);
static void swab_pint32(int *ptr);


/**************************************************************************
Swap bytes on an integer considered as 16 bits
**************************************************************************/
static unsigned int swab_int16(unsigned int val)
{
  return ((val & 0xFF) << 8) | ((val & 0xFF00) >> 8);
}

/**************************************************************************
Swap bytes on an integer considered as 32 bits
**************************************************************************/
static unsigned int swab_int32(unsigned int val)
{
  return ((val & 0xFF) << 24) | ((val & 0xFF00) << 8)
    | ((val & 0xFF0000) >> 8) | ((val & 0xFF000000) >> 24);
}

/**************************************************************************
Swap bytes on an pointed-to integer considered as 16 bits
**************************************************************************/
static void swab_pint16(int *ptr)
{
  (*ptr) = swab_int16(*ptr);
}

/**************************************************************************
Swap bytes on an pointed-to integer considered as 32 bits
**************************************************************************/
static void swab_pint32(int *ptr)
{
  (*ptr) = swab_int32(*ptr);
}

/**************************************************************************
...
**************************************************************************/
void *get_packet_from_connection(struct connection *pc, int *ptype)
{
  int len, type;

  if(pc->buffer.ndata<4)
    return NULL;           /* length and type not read */

  get_int16(pc->buffer.data, &len);
  get_int8(pc->buffer.data+2, &type);

  if(pc->first_packet) {
    /* the first packet better be short: */
    freelog(LOG_DEBUG, "first packet type %d len %d", type, len);
    if(len > 1024) {
      freelog(LOG_NORMAL, "connection from %s detected as old byte order",
	      pc->addr);
      pc->byte_swap = 1;
    } else {
      pc->byte_swap = 0;
    }
    pc->first_packet = 0;
  }

  if(pc->byte_swap) {
    len = swab_int16(len);
    /* so the packet gets processed (removed etc) properly: */
    put_int16(pc->buffer.data, len);
  }

  if(len > pc->buffer.ndata)
    return NULL;           /* not all data has been read */

  freelog(LOG_DEBUG, "packet type %d len %d", type, len);

  *ptype=type;

  switch(type) {

  case PACKET_REQUEST_JOIN_GAME:
    return receive_packet_req_join_game(pc);

  case PACKET_JOIN_GAME_REPLY:
    return receive_packet_join_game_reply(pc);

  case PACKET_SERVER_SHUTDOWN:
    return receive_packet_generic_message(pc);

  case PACKET_UNIT_INFO:
    return receive_packet_unit_info(pc);

   case PACKET_CITY_INFO:
    return receive_packet_city_info(pc);

  case PACKET_MOVE_UNIT:
    return receive_packet_move_unit(pc);

  case PACKET_TURN_DONE:
    return receive_packet_generic_message(pc);

  case PACKET_BEFORE_NEW_YEAR:
    return receive_packet_before_new_year(pc);
  case PACKET_NEW_YEAR:
    return receive_packet_new_year(pc);

  case PACKET_TILE_INFO:
    return receive_packet_tile_info(pc);

  case PACKET_SELECT_RACE:
  case PACKET_REMOVE_UNIT:
  case PACKET_REMOVE_CITY:
  case PACKET_GAME_STATE:
  case PACKET_REPORT_REQUEST:
  case PACKET_REMOVE_PLAYER:  
  case PACKET_CITY_REFRESH:
  case PACKET_INCITE_INQ:
    return receive_packet_generic_integer(pc);
    
  case PACKET_ALLOC_RACE:
    return receive_packet_alloc_race(pc);

  case PACKET_SHOW_MESSAGE:
    return receive_packet_generic_message(pc);

  case PACKET_PLAYER_INFO:
    return receive_packet_player_info(pc);

  case PACKET_GAME_INFO:
    return receive_packet_game_info(pc);

  case PACKET_MAP_INFO:
    return receive_packet_map_info(pc);

  case PACKET_CHAT_MSG:
  case PACKET_PAGE_MSG:
    return receive_packet_generic_message(pc);
    
  case PACKET_CITY_SELL:
  case PACKET_CITY_BUY:
  case PACKET_CITY_CHANGE:
  case PACKET_CITY_MAKE_SPECIALIST:
  case PACKET_CITY_MAKE_WORKER:
  case PACKET_CITY_CHANGE_SPECIALIST:
  case PACKET_CITY_RENAME:
    return receive_packet_city_request(pc);

  case PACKET_PLAYER_RATES:
  case PACKET_PLAYER_REVOLUTION:
  case PACKET_PLAYER_GOVERNMENT:
  case PACKET_PLAYER_RESEARCH:
  case PACKET_PLAYER_TECH_GOAL:
    return receive_packet_player_request(pc);

  case PACKET_UNIT_BUILD_CITY:
  case PACKET_UNIT_DISBAND:
  case PACKET_UNIT_CHANGE_HOMECITY:
  case PACKET_UNIT_ESTABLISH_TRADE:
  case PACKET_UNIT_HELP_BUILD_WONDER:
  case PACKET_UNIT_GOTO_TILE:
  case PACKET_UNIT_AUTO:
  case PACKET_UNIT_UNLOAD:
  case PACKET_UNIT_UPGRADE:
  case PACKET_UNIT_NUKE:
    return receive_packet_unit_request(pc);
  case PACKET_UNITTYPE_UPGRADE:
    return receive_packet_unittype_info(pc);
  case PACKET_UNIT_COMBAT:
    return receive_packet_unit_combat(pc);
  case PACKET_NUKE_TILE:
    return receive_packet_nuke_tile(pc);
  case PACKET_DIPLOMAT_ACTION:
    return receive_packet_diplomat_action(pc);

  case PACKET_DIPLOMACY_INIT_MEETING:
  case PACKET_DIPLOMACY_CREATE_CLAUSE:
  case PACKET_DIPLOMACY_REMOVE_CLAUSE:
  case PACKET_DIPLOMACY_CANCEL_MEETING:
  case PACKET_DIPLOMACY_ACCEPT_TREATY:
  case PACKET_DIPLOMACY_SIGN_TREATY:
    return receive_packet_diplomacy_info(pc);

  case PACKET_INCITE_COST:
  case PACKET_CITY_OPTIONS:
    return receive_packet_generic_values(pc);

  case PACKET_RULESET_TECH:
    return receive_packet_ruleset_tech(pc);
  case PACKET_RULESET_UNIT:
    return receive_packet_ruleset_unit(pc);
  case PACKET_RULESET_BUILDING:
    return receive_packet_ruleset_building(pc);
  case PACKET_RULESET_TERRAIN:
    return receive_packet_ruleset_terrain(pc);
  case PACKET_RULESET_TERRAIN_CONTROL:
    return receive_packet_ruleset_terrain_control(pc);

  case PACKET_SPACESHIP_INFO:
    return receive_packet_spaceship_info(pc);

  case PACKET_SPACESHIP_ACTION:
    return receive_packet_spaceship_action(pc);

  default:
    freelog(LOG_NORMAL, "unknown packet type received");
    remove_packet_from_buffer(&pc->buffer);
    return NULL;
  };
}

/**************************************************************************
  Initialize pack_iter at the start of receiving a packet.
  Sets all entries in piter.
  There should already be a packet, and it should have at least
  3 bytes, as checked in get_packet_from_connection().
  The length data will already be byte swapped if necessary.
**************************************************************************/
static void pack_iter_init(struct pack_iter *piter, struct connection *pc)
{
  assert(piter!=NULL && pc!=NULL);
  assert(pc->buffer.ndata >= 3);
  
  piter->swap_bytes = pc->byte_swap;
  piter->ptr = piter->base = pc->buffer.data;
  piter->ptr = get_int16(piter->ptr, &piter->len);
  piter->ptr = get_int8(piter->ptr, &piter->type);
  piter->short_packet = (piter->len < 3);
  piter->bad_string = 0;
  piter->bad_bit_string = 0;
}

/**************************************************************************
  Returns the number of bytes still unread by pack_iter.
  That is, the length minus the number already read.
  If the packet was already too short previously, returns -1.
**************************************************************************/
static int pack_iter_remaining(struct pack_iter *piter)
{
  assert(piter);
  if (piter->short_packet) {
    return -1;
  } else {
    return piter->len - (piter->ptr - piter->base);
  }
}

/**************************************************************************
  At the end of reading a packet, check if we read the right amount
  of data.  Prints a log message if not.
  Also check bad_string and bad_bit_string flags.
  Unfortunately the connection struct doesn't currently allow us to
  identify the player/connection very well...
  Note that in the client, *pc->addr is null (static mem, never set).
**************************************************************************/
static void pack_iter_end(struct pack_iter *piter, struct connection *pc)
{
  int rem;
  
  assert(piter!=NULL && pc!=NULL);
  
  if (piter->bad_string) {
    freelog(LOG_NORMAL, "received bad string in packet (type %d, len %d)%s%s",
	    piter->type, piter->len, (*pc->addr ? " from " : ""), pc->addr);
  }
  if (piter->bad_bit_string) {
    freelog(LOG_NORMAL,
	    "received bad bit string in packet (type %d, len %d)%s%s",
	    piter->type, piter->len, (*pc->addr ? " from " : ""), pc->addr);
  }
  
  rem = pack_iter_remaining(piter);
  if (rem < 0) {
    freelog(LOG_NORMAL, "received short packet (type %d, len %d)%s%s",
	    piter->type, piter->len, (*pc->addr ? " from " : ""), pc->addr);
  } else if(rem > 0) {
    /* This may be ok, eg a packet from a newer version with extra info
     * which we should just ignore */
    freelog(LOG_VERBOSE, "received long packet (type %d, len %d, rem %d)%s%s",
	    piter->type, piter->len, rem, (*pc->addr ? " from " : ""), pc->addr);
  }
}

/**************************************************************************
...
**************************************************************************/
static unsigned char *get_int8(unsigned char *buffer, int *val)
{
  if(val) {
    *val=(*buffer);
  }
  return buffer+1;
}

/**************************************************************************
...
**************************************************************************/
static unsigned char *put_int8(unsigned char *buffer, int val)
{
  *buffer++=val&0xff;
  return buffer;
}

/**************************************************************************
...
**************************************************************************/
static unsigned char *get_int16(unsigned char *buffer, int *val)
{
  if(val) {
    unsigned short x;
    memcpy(&x,buffer,2);
    *val=ntohs(x);
  }
  return buffer+2;
}


/**************************************************************************
...
**************************************************************************/
unsigned char *put_int16(unsigned char *buffer, int val)
{
  unsigned short x = htons(val);
  memcpy(buffer,&x,2);
  return buffer+2;
}


/**************************************************************************
...
**************************************************************************/
static unsigned char *get_int32(unsigned char *buffer, int *val)
{
  if(val) {
    unsigned long x;
    memcpy(&x,buffer,4);
    *val=ntohl(x);
  }
  return buffer+4;
}


/**************************************************************************
...
**************************************************************************/
unsigned char *put_int32(unsigned char *buffer, int val)
{
  unsigned long x = htonl(val);
  memcpy(buffer,&x,4);
  return buffer+4;
}

/**************************************************************************
  Like get_int8, but using a pack_iter.
  Sets *val to zero for short packets.
  val can be NULL meaning just read past.
**************************************************************************/
static void iget_int8(struct pack_iter *piter, int *val)
{
  assert(piter);
  if (pack_iter_remaining(piter) < 1) {
    piter->short_packet = 1;
    if (val) *val = 0;
    return;
  }
  piter->ptr = get_int8(piter->ptr, val);
}

/**************************************************************************
  Like get_int16, but using a pack_iter.
  Also does byte swapping if required.
  Sets *val to zero for short packets.
  val can be NULL meaning just read past.
**************************************************************************/
static void iget_int16(struct pack_iter *piter, int *val)
{
  assert(piter);
  if (pack_iter_remaining(piter) < 2) {
    piter->short_packet = 1;
    if (val) *val = 0;
    return;
  }
  piter->ptr = get_int16(piter->ptr, val);
  if (val && piter->swap_bytes) {
    swab_pint16(val);
  }
}

/**************************************************************************
  Like get_int32, but using a pack_iter.
  Also does byte swapping if required.
  Sets *val to zero for short packets.
  val can be NULL meaning just read past.
**************************************************************************/
static void iget_int32(struct pack_iter *piter, int *val)
{
  assert(piter);
  if (pack_iter_remaining(piter) < 4) {
    piter->short_packet = 1;
    if (val) *val = 0;
    return;
  }
  piter->ptr = get_int32(piter->ptr, val);
  if (val && piter->swap_bytes) {
    swab_pint32(val);
  }
}

/**************************************************************************
...
**************************************************************************/
unsigned char *put_string(unsigned char *buffer, char *mystring)
{
  int len = strlen(mystring) + 1;
  memcpy(buffer, mystring, len);
  return buffer+len;
}

/**************************************************************************
  Like old get_string, but using a pack_iter, and navail is the memory
  available in mystring.  If string in packet is too long, a truncated
  string is written into mystring (and set piter->bad_string).
  (The truncated string could be the empty string.)
  Result in mystring is guaranteed to be null-terminated.
  mystring can be NULL, in which case just advance piter, and ignore
  navail.  If mystring is non-NULL, navail must be greater than 0.
**************************************************************************/
static void iget_string(struct pack_iter *piter, char *mystring, int navail)
{
  unsigned char *c;
  int ps_len;			/* length in packet, not including null */
  int len;			/* length to copy, not including null */

  assert(piter);
  assert((navail>0) || (mystring==NULL));

  if (pack_iter_remaining(piter) < 1) {
    piter->short_packet = 1;
    if (mystring) *mystring = '\0';
    return;
  }
  
  /* avoid using strlen (or strcpy) on an (unsigned char*)  --dwp */
  for(c=piter->ptr; *c && (c-piter->base) < piter->len; c++) ;

  if ((c-piter->base) >= piter->len) {
    ps_len = pack_iter_remaining(piter);
    piter->short_packet = 1;
    piter->bad_string = 1;
  } else {
    ps_len = c - piter->ptr;
  }
  len = ps_len;
  if (mystring && navail > 0 && ps_len >= navail) {
    piter->bad_string = 1;
    len = navail-1;
  }
  if (mystring) {
    memcpy(mystring, piter->ptr, len);
    mystring[len] = '\0';
  }
  if (!piter->short_packet) {
    piter->ptr += (ps_len+1);		/* past terminator */
  }
}


/**************************************************************************
  This is to encode the pcity->city_map[]; that is, which city tiles
  are worked/available/unavailable.  str should have these values,
  for the 5x5 map, encoded as '0', '1', and '2' char values.
  But we only send the real info (not corners or centre), and
  pack it into 4 bytes.
**************************************************************************/
static unsigned char *put_city_map(unsigned char *buffer, char *str)
{
  static const int index[20]=
      {1,2,3, 5,6,7,8,9, 10,11, 13,14, 15,16,17,18,19, 21,22,23 };
  int i;

  for(i=0;i<20;i+=5)
    *buffer++ = (str[index[i]]-'0')*81 + (str[index[i+1]]-'0')*27 +
	        (str[index[i+2]]-'0')*9 + (str[index[i+3]]-'0')*3 +
	        (str[index[i+4]]-'0')*1;

  return buffer;
}

/**************************************************************************
  Like old get_city_map, but using a pack_iter, and 'navail' is the
  memory available in str.  That is, reverse the encoding of put_city_map().
  str can be NULL, meaning to just read past city_map.
**************************************************************************/
static void iget_city_map(struct pack_iter *piter, char *str, int navail)
{
  static const int index[21]=
      {1,2,3, 5,6,7,8,9, 10,11, 13,14, 15,16,17,18,19, 21,22,23 };
  int i,j;

  assert(piter);
  assert((navail>0) || (str==NULL));
  
  if (pack_iter_remaining(piter) < 4) {
    piter->short_packet = 1;
  }

  if (str == NULL) {
    if (!piter->short_packet) {
      piter->ptr += 4;
    }
    return;
  }

  assert(navail>=26);

  str[0]='2'; str[4]='2'; str[12]='1'; 
  str[20]='2'; str[24]='2'; str[25]='\0';
  
  for(i=0;i<20;)  {
    if (piter->short_packet) {
      /* put in something sensible? */
      for(j=0; j<5; j++) {
	str[index[i++]]='0';
      }
    } else {
      j=*(piter->ptr)++;
      str[index[i++]]='0'+j/81; j%=81;
      str[index[i++]]='0'+j/27; j%=27;
      str[index[i++]]='0'+j/9; j%=9;
      str[index[i++]]='0'+j/3; j%=3;
      str[index[i++]]='0'+j;
    }
  }
}

/**************************************************************************
...
**************************************************************************/
static unsigned char *put_bit_string(unsigned char *buffer, char *str)
{
  int i,b,n;
  int data;

  n=strlen(str);
  *buffer++=n;
  for(i=0;i<n;)  {
    data=0;
    for(b=0;b<8 && i<n;b++,i++)
      if(str[i]=='1') data|=(1<<b);
    *buffer++=data;
  }

  return buffer;
}

/**************************************************************************
  Like old get_bit_string, but using a pack_iter, and 'navail' is the
  memory available in str.
  The first byte tells us the number of bits in the bit string,
  the rest of the bytes are packed bits.  Writes bytes to str,
  as '0' and '1' characters, null terminated.
  This could be made (slightly?) more efficient (not using iget_int8(),
  but directly mapipulating piter->ptr), but I couldn't be bothered
  trying to get everything (including short_packet) correct. --dwp
**************************************************************************/
static void iget_bit_string(struct pack_iter *piter, char *str, int navail)
{
  int npack;			/* number claimed in packet */
  int i;			/* iterate the bytes */
  int data;			/* one bytes worth */
  int b;			/* one bits worth */

  assert(piter);
  assert(str!=NULL && navail>0);
  
  if (pack_iter_remaining(piter) < 1) {
    piter->short_packet = 1;
    str[0] = '\0';
    return;
  }

  iget_int8(piter, &npack);
  if (npack <= 0) {
    piter->bad_bit_string = 1;
    str[0] = '\0';
    return;
  }

  for(i=0; i<npack ; )  {
    iget_int8(piter, &data);
    for(b=0; b<8 && i<npack; b++,i++) {
      if (i < navail) {
	if(data&(1<<b)) str[i]='1'; else str[i]='0';
      }
    }
  }
  if (npack < navail) {
    str[npack] = '\0';
  } else {
    str[navail-1] = '\0';
    piter->bad_bit_string = 1;
  }

  if (piter->short_packet) {
    piter->bad_bit_string = 1;
  }
}
    

/*************************************************************************
...
**************************************************************************/
int send_packet_diplomacy_info(struct connection *pc, enum packet_type pt,
			       struct packet_diplomacy_info *packet)
{
  unsigned char buffer[MAX_LEN_PACKET], *cptr;
  cptr=put_int8(buffer+2, pt);
  
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
receive_packet_diplomacy_info(struct connection *pc)
{
  struct pack_iter iter;
  struct packet_diplomacy_info *preq=
    fc_malloc(sizeof(struct packet_diplomacy_info));

  pack_iter_init(&iter, pc);

  iget_int32(&iter, &preq->plrno0);
  iget_int32(&iter, &preq->plrno1);
  iget_int32(&iter, &preq->plrno_from);
  iget_int32(&iter, &preq->clause_type);
  iget_int32(&iter, &preq->value);

  pack_iter_end(&iter, pc);
  remove_packet_from_buffer(&pc->buffer);
  return preq;
}


/*************************************************************************
...
**************************************************************************/
int send_packet_diplomat_action(struct connection *pc, 
				struct packet_diplomat_action *packet)
{
  unsigned char buffer[MAX_LEN_PACKET], *cptr;
  cptr=put_int8(buffer+2, PACKET_DIPLOMAT_ACTION);
  
  cptr=put_int8(cptr, packet->action_type);
  cptr=put_int16(cptr, packet->diplomat_id);
  cptr=put_int16(cptr, packet->target_id);
  cptr=put_int16(cptr, packet->value);
  put_int16(buffer, cptr-buffer);

  return send_connection_data(pc, buffer, cptr-buffer);
}

/*************************************************************************
...
**************************************************************************/
struct packet_diplomat_action *
receive_packet_diplomat_action(struct connection *pc)
{
  struct pack_iter iter;
  struct packet_diplomat_action *preq=
    fc_malloc(sizeof(struct packet_diplomat_action));

  pack_iter_init(&iter, pc);

  iget_int8(&iter, &preq->action_type);
  iget_int16(&iter, &preq->diplomat_id);
  iget_int16(&iter, &preq->target_id);
  iget_int16(&iter, &preq->value);

  pack_iter_end(&iter, pc);
  remove_packet_from_buffer(&pc->buffer);
  return preq;
}

/*************************************************************************
...
**************************************************************************/
int send_packet_nuke_tile(struct connection *pc, 
			  struct packet_nuke_tile *packet)
{
  unsigned char buffer[MAX_LEN_PACKET], *cptr;
  cptr=put_int8(buffer+2, PACKET_NUKE_TILE);
  
  cptr=put_int8(cptr, packet->x);
  cptr=put_int8(cptr, packet->y);
  put_int16(buffer, cptr-buffer);

  return send_connection_data(pc, buffer, cptr-buffer);
}


/*************************************************************************
...
**************************************************************************/
struct packet_nuke_tile *
receive_packet_nuke_tile(struct connection *pc)
{
  struct pack_iter iter;
  struct packet_nuke_tile *preq=
    fc_malloc(sizeof(struct packet_nuke_tile));

  pack_iter_init(&iter, pc);

  iget_int8(&iter, &preq->x);
  iget_int8(&iter, &preq->y);

  pack_iter_end(&iter, pc);
  remove_packet_from_buffer(&pc->buffer);
  return preq;
}


/*************************************************************************
...
**************************************************************************/
int send_packet_unit_combat(struct connection *pc, 
			    struct packet_unit_combat *packet)
{
  unsigned char buffer[MAX_LEN_PACKET], *cptr;
  cptr=put_int8(buffer+2, PACKET_UNIT_COMBAT);
  
  cptr=put_int16(cptr, packet->attacker_unit_id);
  cptr=put_int16(cptr, packet->defender_unit_id);
  cptr=put_int8(cptr, packet->attacker_hp);
  cptr=put_int8(cptr, packet->defender_hp);
  cptr=put_int8(cptr, packet->make_winner_veteran);
  put_int16(buffer, cptr-buffer);

  return send_connection_data(pc, buffer, cptr-buffer);
}


/*************************************************************************
...
**************************************************************************/
struct packet_unit_combat *
receive_packet_unit_combat(struct connection *pc)
{
  struct pack_iter iter;
  struct packet_unit_combat *preq=
    fc_malloc(sizeof(struct packet_unit_combat));

  pack_iter_init(&iter, pc);

  iget_int16(&iter, &preq->attacker_unit_id);
  iget_int16(&iter, &preq->defender_unit_id);
  iget_int8(&iter, &preq->attacker_hp);
  iget_int8(&iter, &preq->defender_hp);
  iget_int8(&iter, &preq->make_winner_veteran);

  pack_iter_end(&iter, pc);
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
  unsigned char buffer[MAX_LEN_PACKET], *cptr;
  cptr=put_int8(buffer+2, req_type);
  cptr=put_int16(cptr, packet->unit_id);
  cptr=put_int16(cptr, packet->city_id);
  cptr=put_int8(cptr, packet->x);
  cptr=put_int8(cptr, packet->y);
  cptr=put_string(cptr, packet->name);
  put_int16(buffer, cptr-buffer);

  return send_connection_data(pc, buffer, cptr-buffer);
}


/*************************************************************************
...
**************************************************************************/
struct packet_unit_request *
receive_packet_unit_request(struct connection *pc)
{
  struct pack_iter iter;
  struct packet_unit_request *preq=
    fc_malloc(sizeof(struct packet_unit_request));

  pack_iter_init(&iter, pc);

  iget_int16(&iter, &preq->unit_id);
  iget_int16(&iter, &preq->city_id);
  iget_int8(&iter, &preq->x);
  iget_int8(&iter, &preq->y);
  iget_string(&iter, preq->name, sizeof(preq->name));

  pack_iter_end(&iter, pc);
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
  unsigned char buffer[MAX_LEN_PACKET], *cptr;
  cptr=put_int8(buffer+2, req_type);
  cptr=put_int8(cptr, packet->tax);
  cptr=put_int8(cptr, packet->luxury);
  cptr=put_int8(cptr, packet->science);
  cptr=put_int8(cptr, packet->government);
  cptr=put_int8(cptr, packet->tech);
  put_int16(buffer, cptr-buffer);

  return send_connection_data(pc, buffer, cptr-buffer);
}


/*************************************************************************
...
**************************************************************************/
struct packet_player_request *
receive_packet_player_request(struct connection *pc)
{
  struct pack_iter iter;
  struct packet_player_request *preq=
    fc_malloc(sizeof(struct packet_player_request));

  pack_iter_init(&iter, pc);

  iget_int8(&iter, &preq->tax);
  iget_int8(&iter, &preq->luxury);
  iget_int8(&iter, &preq->science);
  iget_int8(&iter, &preq->government);
  iget_int8(&iter, &preq->tech);
  
  pack_iter_end(&iter, pc);
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
  unsigned char buffer[MAX_LEN_PACKET], *cptr;
  cptr=put_int8(buffer+2, req_type);
  cptr=put_int16(cptr, packet->city_id);
  cptr=put_int8(cptr, packet->build_id);
  cptr=put_int8(cptr, packet->is_build_id_unit_id?1:0);
  cptr=put_int8(cptr, packet->worker_x);
  cptr=put_int8(cptr, packet->worker_y);
  cptr=put_int8(cptr, packet->specialist_from);
  cptr=put_int8(cptr, packet->specialist_to);
  cptr=put_string(cptr, packet->name);
  put_int16(buffer, cptr-buffer);

  return send_connection_data(pc, buffer, cptr-buffer);
}


/*************************************************************************
...
**************************************************************************/
struct packet_city_request *
receive_packet_city_request(struct connection *pc)
{
  struct pack_iter iter;
  struct packet_city_request *preq=
    fc_malloc(sizeof(struct packet_city_request));

  pack_iter_init(&iter, pc);

  iget_int16(&iter, &preq->city_id);
  iget_int8(&iter, &preq->build_id);
  iget_int8(&iter, &preq->is_build_id_unit_id);
  iget_int8(&iter, &preq->worker_x);
  iget_int8(&iter, &preq->worker_y);
  iget_int8(&iter, &preq->specialist_from);
  iget_int8(&iter, &preq->specialist_to);
  iget_string(&iter, preq->name, sizeof(preq->name));
  
  pack_iter_end(&iter, pc);
  remove_packet_from_buffer(&pc->buffer);
  return preq;
}


/*************************************************************************
...
**************************************************************************/
int send_packet_player_info(struct connection *pc, struct packet_player_info *pinfo)
{
  unsigned char buffer[MAX_LEN_PACKET], *cptr;
  cptr=put_int8(buffer+2, PACKET_PLAYER_INFO);
  cptr=put_int8(cptr, pinfo->playerno);
  cptr=put_string(cptr, pinfo->name);
  cptr=put_int8(cptr, pinfo->government);
  cptr=put_int32(cptr, pinfo->embassy);
  cptr=put_int8(cptr, pinfo->race);
  cptr=put_int8(cptr, pinfo->turn_done?1:0);
  cptr=put_int16(cptr, pinfo->nturns_idle);
  cptr=put_int8(cptr, pinfo->is_alive?1:0);
  
  cptr=put_int32(cptr, pinfo->gold);
  cptr=put_int8(cptr, pinfo->tax);
  cptr=put_int8(cptr, pinfo->science);
  cptr=put_int8(cptr, pinfo->luxury);

  cptr=put_int32(cptr, pinfo->researched);
  cptr=put_int32(cptr, pinfo->researchpoints);
  cptr=put_int8(cptr, pinfo->researching);
  cptr=put_bit_string(cptr, (char*)pinfo->inventions);
  cptr=put_int16(cptr, pinfo->future_tech);
  
  cptr=put_int8(cptr, pinfo->is_connected?1:0);
  cptr=put_string(cptr, pinfo->addr);
  cptr=put_int8(cptr, pinfo->revolution);
  cptr=put_int8(cptr, pinfo->tech_goal);
  cptr=put_int8(cptr, pinfo->ai?1:0);

  /* if (pc && has_capability("clientcapabilities", pc->capability)) */
  cptr=put_string(cptr, pinfo->capability);

  put_int16(buffer, cptr-buffer);

  return send_connection_data(pc, buffer, cptr-buffer);
}


/*************************************************************************
...
**************************************************************************/
struct packet_player_info *
receive_packet_player_info(struct connection *pc)
{
  struct pack_iter iter;
  struct packet_player_info *pinfo=
     fc_malloc(sizeof(struct packet_player_info));

  pack_iter_init(&iter, pc);

  iget_int8(&iter,  &pinfo->playerno);
  iget_string(&iter, pinfo->name, sizeof(pinfo->name));
  iget_int8(&iter,  &pinfo->government);
  iget_int32(&iter,  &pinfo->embassy);
  iget_int8(&iter,  &pinfo->race);
  iget_int8(&iter,  &pinfo->turn_done);
  iget_int16(&iter,  &pinfo->nturns_idle);
  iget_int8(&iter,  &pinfo->is_alive);
  
  iget_int32(&iter, &pinfo->gold);
  iget_int8(&iter, &pinfo->tax);
  iget_int8(&iter, &pinfo->science);
  iget_int8(&iter, &pinfo->luxury);

  iget_int32(&iter, &pinfo->researched); /* signed */
  iget_int32(&iter, &pinfo->researchpoints);
  iget_int8(&iter, &pinfo->researching);
  iget_bit_string(&iter, (char*)pinfo->inventions, sizeof(pinfo->inventions));
  iget_int16(&iter, &pinfo->future_tech);

  iget_int8(&iter, &pinfo->is_connected);
  iget_string(&iter, pinfo->addr, sizeof(pinfo->addr));
  iget_int8(&iter, &pinfo->revolution);
  iget_int8(&iter, &pinfo->tech_goal);
  iget_int8(&iter, &pinfo->ai);

  /* if (has_capability("clientcapabilities", pc->capability)) */
  iget_string(&iter, pinfo->capability, sizeof(pinfo->capability));
  
  pack_iter_end(&iter, pc);
  remove_packet_from_buffer(&pc->buffer);
  return pinfo;
}


/*************************************************************************
...
**************************************************************************/
int send_packet_game_info(struct connection *pc, 
			  struct packet_game_info *pinfo)
{
  unsigned char buffer[MAX_LEN_PACKET], *cptr;
  int i;
  
  cptr=put_int8(buffer+2, PACKET_GAME_INFO);
  cptr=put_int16(cptr, pinfo->gold);
  cptr=put_int32(cptr, pinfo->tech);
  cptr=put_int8(cptr, pinfo->techlevel);

  cptr=put_int32(cptr, pinfo->skill_level);
  cptr=put_int16(cptr, pinfo->timeout);
  cptr=put_int32(cptr, pinfo->end_year);
  cptr=put_int32(cptr, pinfo->year);
  cptr=put_int8(cptr, pinfo->min_players);
  cptr=put_int8(cptr, pinfo->max_players);
  cptr=put_int8(cptr, pinfo->nplayers);
  cptr=put_int8(cptr, pinfo->player_idx);
  cptr=put_int32(cptr, pinfo->globalwarming);
  cptr=put_int32(cptr, pinfo->heating);
  cptr=put_int8(cptr, pinfo->cityfactor);
  cptr=put_int8(cptr, pinfo->diplcost);
  cptr=put_int8(cptr, pinfo->freecost);
  cptr=put_int8(cptr, pinfo->conquercost);
  cptr=put_int8(cptr, pinfo->unhappysize);
  cptr=put_int8(cptr, pinfo->rail_food);
  cptr=put_int8(cptr, pinfo->rail_prod);
  cptr=put_int8(cptr, pinfo->rail_trade);
  cptr=put_int8(cptr, pinfo->farmfood);
  
  for(i=0; i<A_LAST; i++)
    cptr=put_int8(cptr, pinfo->global_advances[i]);
  for(i=0; i<B_LAST; i++)
    cptr=put_int16(cptr, pinfo->global_wonders[i]);
  cptr=put_int8(cptr, pinfo->techpenalty);
  cptr=put_int8(cptr, pinfo->foodbox);
  cptr=put_int8(cptr, pinfo->civstyle);
  cptr=put_int8(cptr, pinfo->spacerace);

  cptr=put_int8(cptr, pinfo->aqueduct_size);
  cptr=put_int8(cptr, pinfo->sewer_size);
  cptr=put_int8(cptr, pinfo->rtech.get_bonus_tech);
  cptr=put_int8(cptr, pinfo->rtech.boat_fast);
  cptr=put_int8(cptr, pinfo->rtech.cathedral_plus);
  cptr=put_int8(cptr, pinfo->rtech.cathedral_minus);
  cptr=put_int8(cptr, pinfo->rtech.colosseum_plus);
  
  put_int16(buffer, cptr-buffer);

  return send_connection_data(pc, buffer, cptr-buffer);
}

/*************************************************************************
...
**************************************************************************/
struct packet_game_info *receive_packet_game_info(struct connection *pc)
{
  int i;
  struct pack_iter iter;
  struct packet_game_info *pinfo=
     fc_malloc(sizeof(struct packet_game_info));

  pack_iter_init(&iter, pc);

  iget_int16(&iter, &pinfo->gold);
  iget_int32(&iter, &pinfo->tech);
  iget_int8(&iter, &pinfo->techlevel);
  iget_int32(&iter, &pinfo->skill_level);
  iget_int16(&iter, &pinfo->timeout);
  iget_int32(&iter, &pinfo->end_year);
  iget_int32(&iter, &pinfo->year);
  iget_int8(&iter, &pinfo->min_players);
  iget_int8(&iter, &pinfo->max_players);
  iget_int8(&iter, &pinfo->nplayers);
  iget_int8(&iter, &pinfo->player_idx);
  iget_int32(&iter, &pinfo->globalwarming);
  iget_int32(&iter, &pinfo->heating);
  iget_int8(&iter, &pinfo->cityfactor);
  iget_int8(&iter, &pinfo->diplcost);
  iget_int8(&iter, &pinfo->freecost);
  iget_int8(&iter, &pinfo->conquercost);
  iget_int8(&iter, &pinfo->unhappysize);
  iget_int8(&iter, &pinfo->rail_food);
  iget_int8(&iter, &pinfo->rail_prod);
  iget_int8(&iter, &pinfo->rail_trade);
  iget_int8(&iter, &pinfo->farmfood);
  
  for(i=0; i<A_LAST; i++)
    iget_int8(&iter, &pinfo->global_advances[i]);
  for(i=0; i<B_LAST; i++)
    iget_int16(&iter, &pinfo->global_wonders[i]);
  iget_int8(&iter, &pinfo->techpenalty);
  iget_int8(&iter, &pinfo->foodbox);
  iget_int8(&iter, &pinfo->civstyle);
  iget_int8(&iter, &pinfo->spacerace);

  iget_int8(&iter, &pinfo->aqueduct_size);
  iget_int8(&iter, &pinfo->sewer_size);
  iget_int8(&iter, &pinfo->rtech.get_bonus_tech);
  iget_int8(&iter, &pinfo->rtech.boat_fast);
  iget_int8(&iter, &pinfo->rtech.cathedral_plus);
  iget_int8(&iter, &pinfo->rtech.cathedral_minus);
  iget_int8(&iter, &pinfo->rtech.colosseum_plus);

  pack_iter_end(&iter, pc);
  remove_packet_from_buffer(&pc->buffer);
  return pinfo;
}

/*************************************************************************
...
**************************************************************************/
int send_packet_map_info(struct connection *pc, 
			 struct packet_map_info *pinfo)
{
  unsigned char buffer[MAX_LEN_PACKET], *cptr;

  cptr=put_int8(buffer+2, PACKET_MAP_INFO);
  cptr=put_int8(cptr, pinfo->xsize);
  cptr=put_int8(cptr, pinfo->ysize);
  cptr=put_int8(cptr, pinfo->is_earth?1:0);
  put_int16(buffer, cptr-buffer);

  return send_connection_data(pc, buffer, cptr-buffer);
}

/*************************************************************************
...
**************************************************************************/
struct packet_map_info *receive_packet_map_info(struct connection *pc)
{
  struct pack_iter iter;
  struct packet_map_info *pinfo=
     fc_malloc(sizeof(struct packet_map_info));

  pack_iter_init(&iter, pc);

  iget_int8(&iter, &pinfo->xsize);
  iget_int8(&iter, &pinfo->ysize);
  iget_int8(&iter, &pinfo->is_earth);

  pack_iter_end(&iter, pc);
  remove_packet_from_buffer(&pc->buffer);
  return pinfo;
}

/*************************************************************************
...
**************************************************************************/
struct packet_tile_info *
receive_packet_tile_info(struct connection *pc)
{
  struct pack_iter iter;
  struct packet_tile_info *packet=
    fc_malloc(sizeof(struct packet_tile_info));

  pack_iter_init(&iter, pc);

  iget_int8(&iter, &packet->x);
  iget_int8(&iter, &packet->y);
  iget_int8(&iter, &packet->type);
  iget_int16(&iter, &packet->special);
  iget_int8(&iter, &packet->known);

  pack_iter_end(&iter, pc);
  remove_packet_from_buffer(&pc->buffer);
  return packet;
}

struct packet_unittype_info *
receive_packet_unittype_info(struct connection *pc)
{
  struct pack_iter iter;
  struct packet_unittype_info *packet=
    fc_malloc(sizeof(struct packet_unittype_info));

  pack_iter_init(&iter, pc);

  iget_int8(&iter, &packet->type);
  iget_int8(&iter, &packet->action);
  
  pack_iter_end(&iter, pc);
  remove_packet_from_buffer(&pc->buffer);
  return packet;
}

/*************************************************************************
...
**************************************************************************/
int send_packet_tile_info(struct connection *pc, 
			  struct packet_tile_info *pinfo)

{
  unsigned char buffer[MAX_LEN_PACKET], *cptr;

  cptr=put_int8(buffer+2, PACKET_TILE_INFO);
  cptr=put_int8(cptr, pinfo->x);
  cptr=put_int8(cptr, pinfo->y);
  cptr=put_int8(cptr, pinfo->type);
  cptr=put_int16(cptr, pinfo->special);
  cptr=put_int8(cptr, pinfo->known);
  put_int16(buffer, cptr-buffer);

  return send_connection_data(pc, buffer, cptr-buffer);
}

/**************************************************************************
...
**************************************************************************/
int send_packet_new_year(struct connection *pc, struct 
			 packet_new_year *request)
{
  unsigned char buffer[MAX_LEN_PACKET], *cptr;
  cptr=put_int8(buffer+2, PACKET_NEW_YEAR);
  cptr=put_int32(cptr, request->year);
  put_int16(buffer, cptr-buffer);
  return send_connection_data(pc, buffer, cptr-buffer);
}

/**************************************************************************
...
**************************************************************************/
int send_packet_unittype_info(struct connection *pc, int type, int action)
{
  unsigned char buffer[MAX_LEN_PACKET], *cptr;
  cptr=put_int8(buffer+2, PACKET_UNITTYPE_UPGRADE);
  cptr=put_int8(cptr, type);
  cptr=put_int8(cptr, action);
  put_int16(buffer, cptr-buffer);
  return send_connection_data(pc, buffer, cptr-buffer);
}

/**************************************************************************
...
**************************************************************************/
struct packet_before_new_year *
receive_packet_before_new_year(struct connection *pc)
{
  struct pack_iter iter;
  struct packet_before_new_year *packet=
    fc_malloc(sizeof(struct packet_before_new_year));

  pack_iter_init(&iter, pc);
  pack_iter_end(&iter, pc);
  remove_packet_from_buffer(&pc->buffer);
  return packet;
}

int send_packet_before_end_year(struct connection *pc)
{
  unsigned char buffer[MAX_LEN_PACKET], *cptr;
  cptr=put_int8(buffer+2, PACKET_BEFORE_NEW_YEAR);
  put_int16(buffer, cptr-buffer);
  return send_connection_data(pc, buffer, cptr-buffer);
}

/*************************************************************************
...
**************************************************************************/
int send_packet_unit_info(struct connection *pc, 
			    struct packet_unit_info *req)
{
  unsigned char buffer[MAX_LEN_PACKET], *cptr;
  unsigned char pack;

  cptr=put_int8(buffer+2, PACKET_UNIT_INFO);
  cptr=put_int16(cptr, req->id);
  pack=(req->owner)|(req->veteran?0x10:0)|(req->ai?0x20:0);
  cptr=put_int8(cptr, pack);
  cptr=put_int8(cptr, req->x);
  cptr=put_int8(cptr, req->y);
  cptr=put_int16(cptr, req->homecity);
  cptr=put_int8(cptr, req->type);
  cptr=put_int8(cptr, req->movesleft);
  cptr=put_int8(cptr, req->hp);
  pack=(req->upkeep&0x3)|((req->unhappiness&0x3)<<2)|((req->activity&0xf)<<4);
  cptr=put_int8(cptr, pack);
  cptr=put_int8(cptr, req->activity_count);
  cptr=put_int8(cptr, req->goto_dest_x);
  cptr=put_int8(cptr, req->goto_dest_y);
  if(req->fuel) cptr=put_int8(cptr, req->fuel);
  
  put_int16(buffer, cptr-buffer);
  return send_connection_data(pc, buffer, cptr-buffer);
}

/*************************************************************************
...
**************************************************************************/
int send_packet_city_info(struct connection *pc, struct packet_city_info *req)
{
  unsigned char buffer[MAX_LEN_PACKET], *cptr;
  int data;
  cptr=put_int8(buffer+2, PACKET_CITY_INFO);
  cptr=put_int16(cptr, req->id);
  cptr=put_int8(cptr, req->owner);
  cptr=put_int8(cptr, req->x);
  cptr=put_int8(cptr, req->y);
  cptr=put_string(cptr, req->name);
  
  cptr=put_int8(cptr, req->size);
  cptr=put_int8(cptr, req->ppl_happy);
  cptr=put_int8(cptr, req->ppl_content);
  cptr=put_int8(cptr, req->ppl_unhappy);
  cptr=put_int8(cptr, req->ppl_elvis);
  cptr=put_int8(cptr, req->ppl_scientist);
  cptr=put_int8(cptr, req->ppl_taxman);

  cptr=put_int8(cptr, req->food_prod);
  cptr=put_int8(cptr, req->food_surplus);
  cptr=put_int16(cptr, req->shield_prod);
  cptr=put_int16(cptr, req->shield_surplus);
  cptr=put_int16(cptr, req->trade_prod);
  cptr=put_int16(cptr, req->corruption);

  cptr=put_int16(cptr, req->luxury_total);
  cptr=put_int16(cptr, req->tax_total);
  cptr=put_int16(cptr, req->science_total);

  cptr=put_int16(cptr, req->food_stock);
  cptr=put_int16(cptr, req->shield_stock);
  cptr=put_int16(cptr, req->pollution);
  cptr=put_int8(cptr, req->currently_building);

  data=req->is_building_unit?1:0;
  data|=req->did_buy?2:0;
  data|=req->did_sell?4:0;
  data|=req->was_happy?8:0;
  data|=req->airlift?16:0;
  data|=req->diplomat_investigate?32:0; /* gentler implementation -- Syela */
  cptr=put_int8(cptr, data);

  cptr=put_city_map(cptr, (char*)req->city_map);
  cptr=put_bit_string(cptr, (char*)req->improvements);

  /* if(pc && has_capability("autoattack1", pc->capability)) */
  /* only 8 options allowed before need to extend protocol */
  cptr=put_int8(cptr, req->city_options);
  
  for(data=0;data<4;data++)  {
    if(req->trade[data])  {
      cptr=put_int16(cptr, req->trade[data]);
      cptr=put_int8(cptr,req->trade_value[data]);
    }
  }

  put_int16(buffer, cptr-buffer);

  return send_connection_data(pc, buffer, cptr-buffer);
}

/*************************************************************************
...
**************************************************************************/
struct packet_city_info *
receive_packet_city_info(struct connection *pc)
{
  int data;
  struct pack_iter iter;
  struct packet_city_info *packet=
    fc_malloc(sizeof(struct packet_city_info));

  pack_iter_init(&iter, pc);

  iget_int16(&iter, &packet->id);
  iget_int8(&iter, &packet->owner);
  iget_int8(&iter, &packet->x);
  iget_int8(&iter, &packet->y);
  iget_string(&iter, packet->name, sizeof(packet->name));
  
  iget_int8(&iter, &packet->size);
  iget_int8(&iter, &packet->ppl_happy);
  iget_int8(&iter, &packet->ppl_content);
  iget_int8(&iter, &packet->ppl_unhappy);
  iget_int8(&iter, &packet->ppl_elvis);
  iget_int8(&iter, &packet->ppl_scientist);
  iget_int8(&iter, &packet->ppl_taxman);
  
  iget_int8(&iter, &packet->food_prod);
  iget_int8(&iter, &packet->food_surplus);
  if(packet->food_surplus > 127) packet->food_surplus-=256;
  iget_int16(&iter, &packet->shield_prod);
  iget_int16(&iter, &packet->shield_surplus);
  if(packet->shield_surplus > 32767) packet->shield_surplus-=65536;
  iget_int16(&iter, &packet->trade_prod);
  iget_int16(&iter, &packet->corruption);

  iget_int16(&iter, &packet->luxury_total);
  iget_int16(&iter, &packet->tax_total);
  iget_int16(&iter, &packet->science_total);
  
  iget_int16(&iter, &packet->food_stock);
  iget_int16(&iter, &packet->shield_stock);
  iget_int16(&iter, &packet->pollution);
  iget_int8(&iter, &packet->currently_building);

  iget_int8(&iter, &data);
  packet->is_building_unit = data&1;
  packet->did_buy = (data>>=1)&1;
  packet->did_sell = (data>>=1)&1;
  packet->was_happy = (data>>=1)&1;
  packet->airlift = (data>>=1)&1;
  packet->diplomat_investigate = (data>>=1)&1;
  
  iget_city_map(&iter, (char*)packet->city_map, sizeof(packet->city_map));
  iget_bit_string(&iter, (char*)packet->improvements,
		  sizeof(packet->improvements));

  /* if(has_capability("autoattack1", pc->capability)) */
  iget_int8(&iter, &packet->city_options);

  for(data=0;data<4;data++)  {
    if (pack_iter_remaining(&iter) < 3)
      break;
    iget_int16(&iter, &packet->trade[data]);
    iget_int8(&iter, &packet->trade_value[data]);
  }
  for(;data<4;data++) packet->trade_value[data]=packet->trade[data]=0;

  pack_iter_end(&iter, pc);
  remove_packet_from_buffer(&pc->buffer);
  return packet;
}

/*************************************************************************
...
**************************************************************************/
struct packet_unit_info *
receive_packet_unit_info(struct connection *pc)
{
  int pack;
  struct pack_iter iter;
  struct packet_unit_info *packet=
    fc_malloc(sizeof(struct packet_unit_info));

  pack_iter_init(&iter, pc);

  iget_int16(&iter, &packet->id);
  iget_int8(&iter, &pack);
  packet->owner=pack&0x0f;
  packet->veteran=(pack&0x10)?1:0; packet->ai=(pack&0x20)?1:0;
  iget_int8(&iter, &packet->x);
  iget_int8(&iter, &packet->y);
  iget_int16(&iter, &packet->homecity);
  iget_int8(&iter, &packet->type);
  iget_int8(&iter, &packet->movesleft);
  iget_int8(&iter, &packet->hp);
  iget_int8(&iter, &pack);
  packet->activity=pack>>4; packet->upkeep=pack&0x3;
  packet->unhappiness=(pack>>2)&0x3;
  iget_int8(&iter, &packet->activity_count);
  iget_int8(&iter, &packet->goto_dest_x);
  iget_int8(&iter, &packet->goto_dest_y);
  if (pack_iter_remaining(&iter) >= 1) {
    iget_int8(&iter, &packet->fuel);
  } else {
    packet->fuel=0;
  }

  pack_iter_end(&iter, pc);
  remove_packet_from_buffer(&pc->buffer);
  return packet;
}

/**************************************************************************
...
**************************************************************************/
struct packet_new_year *
receive_packet_new_year(struct connection *pc)
{
  struct pack_iter iter;
  struct packet_new_year *packet=
    fc_malloc(sizeof(struct packet_new_year));

  pack_iter_init(&iter, pc);

  iget_int32(&iter, &packet->year);

  pack_iter_end(&iter, pc);
  remove_packet_from_buffer(&pc->buffer);
  return packet;
}


/**************************************************************************
...
**************************************************************************/
int send_packet_move_unit(struct connection *pc, struct 
			  packet_move_unit *request)
{
  unsigned char buffer[MAX_LEN_PACKET], *cptr;

  cptr=put_int8(buffer+2, PACKET_MOVE_UNIT);
  cptr=put_int8(cptr, request->x);
  cptr=put_int8(cptr, request->y);
  cptr=put_int16(cptr, request->unid);
  put_int16(buffer, cptr-buffer);

  return send_connection_data(pc, buffer, cptr-buffer);
}



/**************************************************************************
...
**************************************************************************/
struct packet_move_unit *receive_packet_move_unit(struct connection *pc)
{
  struct pack_iter iter;
  struct packet_move_unit *packet=
    fc_malloc(sizeof(struct packet_move_unit));

  pack_iter_init(&iter, pc);

  iget_int8(&iter, &packet->x);
  iget_int8(&iter, &packet->y);
  iget_int16(&iter, &packet->unid);

  pack_iter_end(&iter, pc);
  remove_packet_from_buffer(&pc->buffer);
  return packet;
}

/**************************************************************************
...
**************************************************************************/
int send_packet_req_join_game(struct connection *pc, struct 
			      packet_req_join_game *request)
{
  unsigned char buffer[MAX_LEN_PACKET], *cptr;
  cptr=put_int8(buffer+2, PACKET_REQUEST_JOIN_GAME);
  cptr=put_string(cptr, request->name);
  cptr=put_int32(cptr, request->major_version);
  cptr=put_int32(cptr, request->minor_version);
  cptr=put_int32(cptr, request->patch_version);
  cptr=put_string(cptr, request->capability);
  put_int16(buffer, cptr-buffer);

  return send_connection_data(pc, buffer, cptr-buffer);
}

/**************************************************************************
...
**************************************************************************/
int send_packet_join_game_reply(struct connection *pc, struct 
			        packet_join_game_reply *reply)
{
  unsigned char buffer[MAX_LEN_PACKET], *cptr;
  cptr=put_int8(buffer+2, PACKET_JOIN_GAME_REPLY);
  cptr=put_int32(cptr, reply->you_can_join);
  /* if other end is byte swapped, you_can_join should be 0,
     which is 0 even if bytes are swapped! --dwp */
  cptr=put_string(cptr, reply->message);
  cptr=put_string(cptr, reply->capability);

  /* so that old clients will understand the reply: */
  if(pc->byte_swap) {
    put_int16(buffer, swab_int16(cptr-buffer));
  } else {
    put_int16(buffer, cptr-buffer);
  }

  return send_connection_data(pc, buffer, cptr-buffer);
}

/**************************************************************************
...
**************************************************************************/
int send_packet_generic_message(struct connection *pc, int type,
				struct packet_generic_message *packet)
{
  unsigned char buffer[MAX_LEN_PACKET], *cptr;
  cptr=put_int8(buffer+2, type);
  cptr=put_int8(cptr, packet->x);
  cptr=put_int8(cptr, packet->y);
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
  unsigned char buffer[MAX_LEN_PACKET], *cptr;
  cptr=put_int8(buffer+2, type);
  cptr=put_int32(cptr, packet->value);
  put_int16(buffer, cptr-buffer);
  return send_connection_data(pc, buffer, cptr-buffer);
}

/**************************************************************************
...
**************************************************************************/
struct packet_req_join_game *
receive_packet_req_join_game(struct connection *pc)
{
  struct pack_iter iter;
  struct packet_req_join_game *packet=
    fc_malloc(sizeof(struct packet_req_join_game));

  pack_iter_init(&iter, pc);

  iget_string(&iter, packet->name, sizeof(packet->name));
  iget_int32(&iter, &packet->major_version);
  iget_int32(&iter, &packet->minor_version);
  iget_int32(&iter, &packet->patch_version);
  iget_string(&iter, packet->capability, sizeof(packet->capability));

  pack_iter_end(&iter, pc);
  remove_packet_from_buffer(&pc->buffer);
  return packet;
}

/**************************************************************************
...
**************************************************************************/
struct packet_join_game_reply *
receive_packet_join_game_reply(struct connection *pc)
{
  struct pack_iter iter;
  struct packet_join_game_reply *packet=
    fc_malloc(sizeof(struct packet_join_game_reply));

  pack_iter_init(&iter, pc);

  iget_int32(&iter, &packet->you_can_join);
  iget_string(&iter, packet->message, sizeof(packet->message));
  iget_string(&iter, packet->capability, sizeof(packet->capability));

  pack_iter_end(&iter, pc);
  remove_packet_from_buffer(&pc->buffer);
  return packet;
}

/**************************************************************************
...
**************************************************************************/
struct packet_generic_message *
receive_packet_generic_message(struct connection *pc)
{
  struct pack_iter iter;
  struct packet_generic_message *packet=
    fc_malloc(sizeof(struct packet_generic_message));

  pack_iter_init(&iter, pc);

  iget_int8(&iter, &packet->x);
  iget_int8(&iter, &packet->y);
  iget_int32(&iter, &packet->event);
  iget_string(&iter, packet->message, sizeof(packet->message));
  
  pack_iter_end(&iter, pc);
  remove_packet_from_buffer(&pc->buffer);
  return packet;
}

/**************************************************************************
...
**************************************************************************/
struct packet_generic_integer *
receive_packet_generic_integer(struct connection *pc)
{
  struct pack_iter iter;
  struct packet_generic_integer *packet=
    fc_malloc(sizeof(struct packet_generic_integer));

  pack_iter_init(&iter, pc);

  iget_int32(&iter, &packet->value);

  pack_iter_end(&iter, pc);
  remove_packet_from_buffer(&pc->buffer);
  return packet;
}

/**************************************************************************
...
**************************************************************************/
int send_packet_alloc_race(struct connection *pc, 
			   struct packet_alloc_race *packet)
{
  unsigned char buffer[MAX_LEN_PACKET], *cptr;
  cptr=put_int8(buffer+2, PACKET_ALLOC_RACE);
  cptr=put_int32(cptr, packet->race_no);
  cptr=put_string(cptr, packet->name);
  put_int16(buffer, cptr-buffer);

  return send_connection_data(pc, buffer, cptr-buffer);
}

/**************************************************************************
...
**************************************************************************/
struct packet_alloc_race *
receive_packet_alloc_race(struct connection *pc)
{
  struct pack_iter iter;
  struct packet_alloc_race *packet=
    fc_malloc(sizeof(struct packet_alloc_race));

  pack_iter_init(&iter, pc);

  iget_int32(&iter, &packet->race_no);
  iget_string(&iter, packet->name, sizeof(packet->name));

  pack_iter_end(&iter, pc);
  remove_packet_from_buffer(&pc->buffer);
  return packet;
}

/**************************************************************************
...
**************************************************************************/
int send_packet_generic_values(struct connection *pc, int type,
			       struct packet_generic_values *req)
{
  unsigned char buffer[MAX_LEN_PACKET], *cptr;
  
  cptr=put_int8(buffer+2, type);
  cptr=put_int16(cptr, req->id);
  cptr=put_int32(cptr, req->value1);
  cptr=put_int32(cptr, req->value2);

  put_int16(buffer, cptr-buffer);

  return send_connection_data(pc, buffer, cptr-buffer);
}

/**************************************************************************
...
**************************************************************************/
struct packet_generic_values *
receive_packet_generic_values(struct connection *pc)
{
  struct pack_iter iter;
  struct packet_generic_values *packet=
    fc_malloc(sizeof(struct packet_generic_values));

  pack_iter_init(&iter, pc);

  iget_int16(&iter, &packet->id);
  if (pack_iter_remaining(&iter) >= 4) {
    iget_int32(&iter, &packet->value1);
  } else {
    packet->value1 = 0;
  }
  if (pack_iter_remaining(&iter) >= 4) {
    iget_int32(&iter, &packet->value2);
  } else {
    packet->value2 = 0;
  }

  pack_iter_end(&iter, pc);
  remove_packet_from_buffer(&pc->buffer);
  return packet;
}

/**************************************************************************
...
**************************************************************************/
int send_packet_ruleset_unit(struct connection *pc,
			     struct packet_ruleset_unit *packet)
{
  unsigned char buffer[MAX_LEN_PACKET], *cptr;
  cptr=put_int8(buffer+2, PACKET_RULESET_UNIT);
  
  cptr=put_int8(cptr, packet->id);
  cptr=put_int8(cptr, packet->graphics);
  cptr=put_int8(cptr, packet->move_type);
  cptr=put_int16(cptr, packet->build_cost);
  cptr=put_int8(cptr, packet->attack_strength);
  cptr=put_int8(cptr, packet->defense_strength);
  cptr=put_int8(cptr, packet->move_rate);
  cptr=put_int8(cptr, packet->tech_requirement);
  cptr=put_int8(cptr, packet->vision_range);
  cptr=put_int8(cptr, packet->transport_capacity);
  cptr=put_int8(cptr, packet->hp);
  cptr=put_int8(cptr, packet->firepower);
  cptr=put_int8(cptr, packet->obsoleted_by);
  cptr=put_int8(cptr, packet->fuel);
  cptr=put_int32(cptr, packet->flags);
  cptr=put_int32(cptr, packet->roles);
  cptr=put_string(cptr, packet->name);
  put_int16(buffer, cptr-buffer);

  return send_connection_data(pc, buffer, cptr-buffer);
}

/**************************************************************************
...
**************************************************************************/
struct packet_ruleset_unit *
receive_packet_ruleset_unit(struct connection *pc)
{
  struct pack_iter iter;
  struct packet_ruleset_unit *packet=
    fc_malloc(sizeof(struct packet_ruleset_unit));

  pack_iter_init(&iter, pc);

  iget_int8(&iter, &packet->id);
  iget_int8(&iter, &packet->graphics);
  iget_int8(&iter, &packet->move_type);
  iget_int16(&iter, &packet->build_cost);
  iget_int8(&iter, &packet->attack_strength);
  iget_int8(&iter, &packet->defense_strength);
  iget_int8(&iter, &packet->move_rate);
  iget_int8(&iter, &packet->tech_requirement);
  iget_int8(&iter, &packet->vision_range);
  iget_int8(&iter, &packet->transport_capacity);
  iget_int8(&iter, &packet->hp);
  iget_int8(&iter, &packet->firepower);
  iget_int8(&iter, &packet->obsoleted_by);
  if (packet->obsoleted_by>127) packet->obsoleted_by-=256;
  iget_int8(&iter, &packet->fuel);
  iget_int32(&iter, &packet->flags);
  iget_int32(&iter, &packet->roles);
  iget_string(&iter, packet->name, sizeof(packet->name));

  pack_iter_end(&iter, pc);
  remove_packet_from_buffer(&pc->buffer);
  return packet;
}


/**************************************************************************
...
**************************************************************************/
int send_packet_ruleset_tech(struct connection *pc,
			     struct packet_ruleset_tech *packet)
{
  unsigned char buffer[MAX_LEN_PACKET], *cptr;
  cptr=put_int8(buffer+2, PACKET_RULESET_TECH);
  
  cptr=put_int8(cptr, packet->id);
  cptr=put_int8(cptr, packet->req[0]);
  cptr=put_int8(cptr, packet->req[1]);
  cptr=put_string(cptr, packet->name);
  put_int16(buffer, cptr-buffer);

  return send_connection_data(pc, buffer, cptr-buffer);
}

/**************************************************************************
...
**************************************************************************/
struct packet_ruleset_tech *
receive_packet_ruleset_tech(struct connection *pc)
{
  struct pack_iter iter;
  struct packet_ruleset_tech *packet=
    fc_malloc(sizeof(struct packet_ruleset_tech));

  pack_iter_init(&iter, pc);

  iget_int8(&iter, &packet->id);
  iget_int8(&iter, &packet->req[0]);
  iget_int8(&iter, &packet->req[1]);
  iget_string(&iter, packet->name, sizeof(packet->name));

  pack_iter_end(&iter, pc);
  remove_packet_from_buffer(&pc->buffer);
  return packet;
}

/**************************************************************************
...
**************************************************************************/
int send_packet_ruleset_building(struct connection *pc,
			     struct packet_ruleset_building *packet)
{
  unsigned char buffer[MAX_LEN_PACKET], *cptr;
  cptr=put_int8(buffer+2, PACKET_RULESET_BUILDING);
  
  cptr=put_int8(cptr, packet->id);
  cptr=put_int8(cptr, packet->is_wonder);
  cptr=put_int8(cptr, packet->tech_requirement);
  cptr=put_int16(cptr, packet->build_cost);
  cptr=put_int8(cptr, packet->shield_upkeep);
  cptr=put_int8(cptr, packet->obsolete_by);
  cptr=put_int8(cptr, packet->variant);
  cptr=put_string(cptr, packet->name);
  put_int16(buffer, cptr-buffer);

  return send_connection_data(pc, buffer, cptr-buffer);
}

/**************************************************************************
...
**************************************************************************/
struct packet_ruleset_building *
receive_packet_ruleset_building(struct connection *pc)
{
  struct pack_iter iter;
  struct packet_ruleset_building *packet=
    fc_malloc(sizeof(struct packet_ruleset_building));

  pack_iter_init(&iter, pc);

  iget_int8(&iter, &packet->id);
  iget_int8(&iter, &packet->is_wonder);
  iget_int8(&iter, &packet->tech_requirement);
  iget_int16(&iter, &packet->build_cost);
  iget_int8(&iter, &packet->shield_upkeep);
  iget_int8(&iter, &packet->obsolete_by);
  iget_int8(&iter, &packet->variant);
  iget_string(&iter, packet->name, sizeof(packet->name));

  pack_iter_end(&iter, pc);
  remove_packet_from_buffer(&pc->buffer);
  return packet;
}

/**************************************************************************
...
**************************************************************************/
int send_packet_ruleset_terrain(struct connection *pc,
				struct packet_ruleset_terrain *packet)
{
  unsigned char buffer[MAX_LEN_PACKET], *cptr;
  cptr=put_int8(buffer+2, PACKET_RULESET_TERRAIN);

  cptr=put_int8(cptr, packet->id);
  cptr=put_string(cptr, packet->terrain_name);
  cptr=put_int16(cptr, packet->graphic_base);
  cptr=put_int16(cptr, packet->graphic_count);
  cptr=put_int8(cptr, packet->movement_cost);
  cptr=put_int8(cptr, packet->defense_bonus);
  cptr=put_int8(cptr, packet->food);
  cptr=put_int8(cptr, packet->shield);
  cptr=put_int8(cptr, packet->trade);
  cptr=put_string(cptr, packet->special_1_name);
  cptr=put_int16(cptr, packet->graphic_special_1);
  cptr=put_int8(cptr, packet->food_special_1);
  cptr=put_int8(cptr, packet->shield_special_1);
  cptr=put_int8(cptr, packet->trade_special_1);
  cptr=put_string(cptr, packet->special_2_name);
  cptr=put_int16(cptr, packet->graphic_special_2);
  cptr=put_int8(cptr, packet->food_special_2);
  cptr=put_int8(cptr, packet->shield_special_2);
  cptr=put_int8(cptr, packet->trade_special_2);
  cptr=put_int8(cptr, packet->road_trade_incr);
  cptr=put_int8(cptr, packet->road_time);
  cptr=put_int8(cptr, packet->irrigation_result);
  cptr=put_int8(cptr, packet->irrigation_food_incr);
  cptr=put_int8(cptr, packet->irrigation_time);
  cptr=put_int8(cptr, packet->mining_result);
  cptr=put_int8(cptr, packet->mining_shield_incr);
  cptr=put_int8(cptr, packet->mining_time);
  cptr=put_int8(cptr, packet->transform_result);
  cptr=put_int8(cptr, packet->transform_time);

  put_int16(buffer, cptr-buffer);

  return send_connection_data(pc, buffer, cptr-buffer);
}

/**************************************************************************
...
**************************************************************************/
struct packet_ruleset_terrain *
receive_packet_ruleset_terrain(struct connection *pc)
{
  struct pack_iter iter;
  struct packet_ruleset_terrain *packet=
    fc_malloc(sizeof(struct packet_ruleset_terrain));

  pack_iter_init(&iter, pc);

  iget_int8(&iter, &packet->id);
  iget_string(&iter, packet->terrain_name, sizeof(packet->terrain_name));
  iget_int16(&iter, &packet->graphic_base);
  iget_int16(&iter, &packet->graphic_count);
  iget_int8(&iter, &packet->movement_cost);
  iget_int8(&iter, &packet->defense_bonus);
  iget_int8(&iter, &packet->food);
  iget_int8(&iter, &packet->shield);
  iget_int8(&iter, &packet->trade);
  iget_string(&iter, packet->special_1_name, sizeof(packet->special_1_name));
  iget_int16(&iter, &packet->graphic_special_1);
  iget_int8(&iter, &packet->food_special_1);
  iget_int8(&iter, &packet->shield_special_1);
  iget_int8(&iter, &packet->trade_special_1);
  iget_string(&iter, packet->special_2_name, sizeof(packet->special_2_name));
  iget_int16(&iter, &packet->graphic_special_2);
  iget_int8(&iter, &packet->food_special_2);
  iget_int8(&iter, &packet->shield_special_2);
  iget_int8(&iter, &packet->trade_special_2);
  iget_int8(&iter, &packet->road_trade_incr);
  iget_int8(&iter, &packet->road_time);
  iget_int8(&iter, (int*)&packet->irrigation_result);
  iget_int8(&iter, &packet->irrigation_food_incr);
  iget_int8(&iter, &packet->irrigation_time);
  iget_int8(&iter, (int*)&packet->mining_result);
  iget_int8(&iter, &packet->mining_shield_incr);
  iget_int8(&iter, &packet->mining_time);
  iget_int8(&iter, (int*)&packet->transform_result);
  iget_int8(&iter, &packet->transform_time);

  pack_iter_end(&iter, pc);
  remove_packet_from_buffer(&pc->buffer);
  return packet;
}

/**************************************************************************
...
**************************************************************************/
int send_packet_ruleset_terrain_control(struct connection *pc,
					struct terrain_misc *packet)
{
  unsigned char buffer[MAX_LEN_PACKET], *cptr;
  cptr=put_int8(buffer+2, PACKET_RULESET_TERRAIN_CONTROL);

  cptr=put_int8(cptr, packet->river_style);
  cptr=put_int8(cptr, packet->may_road);
  cptr=put_int8(cptr, packet->may_irrigate);
  cptr=put_int8(cptr, packet->may_mine);
  cptr=put_int8(cptr, packet->may_transform);
  cptr=put_int16(cptr, packet->border_base);
  cptr=put_int16(cptr, packet->corner_base);
  cptr=put_int16(cptr, packet->river_base);
  cptr=put_int16(cptr, packet->outlet_base);
  cptr=put_int16(cptr, packet->denmark_base);

  put_int16(buffer, cptr-buffer);

  return send_connection_data(pc, buffer, cptr-buffer);
}

/**************************************************************************
...
**************************************************************************/
struct terrain_misc *
receive_packet_ruleset_terrain_control(struct connection *pc)
{
  struct pack_iter iter;
  struct terrain_misc *packet=
    fc_malloc(sizeof(struct terrain_misc));

  pack_iter_init(&iter, pc);

  iget_int8(&iter, (int*)&packet->river_style);
  iget_int8(&iter, &packet->may_road);
  iget_int8(&iter, &packet->may_irrigate);
  iget_int8(&iter, &packet->may_mine);
  iget_int8(&iter, &packet->may_transform);
  iget_int16(&iter, &packet->border_base);
  iget_int16(&iter, &packet->corner_base);
  iget_int16(&iter, &packet->river_base);
  iget_int16(&iter, &packet->outlet_base);
  iget_int16(&iter, &packet->denmark_base);

  pack_iter_end(&iter, pc);
  remove_packet_from_buffer(&pc->buffer);
  return packet;
}

/**************************************************************************
...
**************************************************************************/
int send_packet_spaceship_info(struct connection *pc,
			       struct packet_spaceship_info *packet)
{
  unsigned char buffer[MAX_LEN_PACKET], *cptr;
  cptr=put_int8(buffer+2, PACKET_SPACESHIP_INFO);
  
  cptr=put_int8(cptr, packet->player_num);
  cptr=put_int8(cptr, packet->sship_state);
  cptr=put_int8(cptr, packet->structurals);
  cptr=put_int8(cptr, packet->components);
  cptr=put_int8(cptr, packet->modules);
  cptr=put_int8(cptr, packet->fuel);
  cptr=put_int8(cptr, packet->propulsion);
  cptr=put_int8(cptr, packet->habitation);
  cptr=put_int8(cptr, packet->life_support);
  cptr=put_int8(cptr, packet->solar_panels);
  cptr=put_int16(cptr, packet->launch_year);
  cptr=put_int8(cptr, (packet->population/1000));
  cptr=put_int32(cptr, packet->mass);
  cptr=put_int32(cptr, (int) (packet->support_rate*10000));
  cptr=put_int32(cptr, (int) (packet->energy_rate*10000));
  cptr=put_int32(cptr, (int) (packet->success_rate*10000));
  cptr=put_int32(cptr, (int) (packet->travel_time*10000));
  cptr=put_bit_string(cptr, (char*)packet->structure);

  put_int16(buffer, cptr-buffer);
  return send_connection_data(pc, buffer, cptr-buffer);
}

/**************************************************************************
...
**************************************************************************/
struct packet_spaceship_info *
receive_packet_spaceship_info(struct connection *pc)
{
  int tmp;
  struct pack_iter iter;
  struct packet_spaceship_info *packet=
    fc_malloc(sizeof(struct packet_spaceship_info));
  
  pack_iter_init(&iter, pc);

  iget_int8(&iter, &packet->player_num);
  iget_int8(&iter, &packet->sship_state);
  iget_int8(&iter, &packet->structurals);
  iget_int8(&iter, &packet->components);
  iget_int8(&iter, &packet->modules);
  iget_int8(&iter, &packet->fuel);
  iget_int8(&iter, &packet->propulsion);
  iget_int8(&iter, &packet->habitation);
  iget_int8(&iter, &packet->life_support);
  iget_int8(&iter, &packet->solar_panels);
  iget_int16(&iter, &packet->launch_year);
  
  if(packet->launch_year > 32767) packet->launch_year-=65536;
  
  iget_int8(&iter, &packet->population);
  packet->population *= 1000;
  iget_int32(&iter, &packet->mass);
  
  iget_int32(&iter, &tmp);
  packet->support_rate = tmp * 0.0001;
  iget_int32(&iter, &tmp);
  packet->energy_rate = tmp * 0.0001;
  iget_int32(&iter, &tmp);
  packet->success_rate = tmp * 0.0001;
  iget_int32(&iter, &tmp);
  packet->travel_time = tmp * 0.0001;

  iget_bit_string(&iter, (char*)packet->structure, sizeof(packet->structure));
  
  pack_iter_end(&iter, pc);
  remove_packet_from_buffer(&pc->buffer);
  return packet;
}

/**************************************************************************
...
**************************************************************************/
int send_packet_spaceship_action(struct connection *pc,
				 struct packet_spaceship_action *packet)
{
  unsigned char buffer[MAX_LEN_PACKET], *cptr;
  cptr=put_int8(buffer+2, PACKET_SPACESHIP_ACTION);
  
  cptr=put_int8(cptr, packet->action);
  cptr=put_int8(cptr, packet->num);

  put_int16(buffer, cptr-buffer);
  return send_connection_data(pc, buffer, cptr-buffer);
}

/**************************************************************************
...
**************************************************************************/
struct packet_spaceship_action *
receive_packet_spaceship_action(struct connection *pc)
{
  struct pack_iter iter;
  struct packet_spaceship_action *packet=
    fc_malloc(sizeof(struct packet_spaceship_action));
  
  pack_iter_init(&iter, pc);

  iget_int8(&iter, &packet->action);
  iget_int8(&iter, &packet->num);

  pack_iter_end(&iter, pc);
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
      if(write(pc->sock, (const char *)pc->send_buffer.data, pc->send_buffer.ndata)!=
	 pc->send_buffer.ndata) {
	freelog(LOG_NORMAL, "failed writing data to socket");
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
      if(10*MAX_LEN_PACKET-pc->send_buffer.ndata >= len) { /* room for more?*/
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
      if(write(pc->sock, (const char *)data, len)!=len) {
	freelog(LOG_NORMAL, "failed writing data to socket");
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

  if((didget=read(sock, (char *)(buffer->data+buffer->ndata), 
		  MAX_LEN_PACKET-buffer->ndata))<=0)
    return -1;
 
  buffer->ndata+=didget;
  
  freelog(LOG_DEBUG, "didget:%d", didget); 

  return didget;
}


