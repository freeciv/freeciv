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
#include <assert.h>

#include <map.h>
#include <game.h>
#include <unitfunc.h>
#include <unithand.h>
struct Goto_map_entry {
  int cost;
  unsigned short from_x, from_y;
};

struct Goto_map {
  struct Goto_map_entry *entries;
  int xsize, ysize;
};

struct Goto_heap_entry {
  int cost;
  unsigned short from_x, from_y, to_x, to_y;
};


struct Goto_heap {
  int maxsize;
  int size;
  struct Goto_heap_entry *entries;
};


/*************************************************************************/
struct Goto_heap goto_heap;
struct Goto_map  goto_map;


/*************************************************************************/
void goto_map_init(void);
struct Goto_map_entry *goto_map_get(int x, int y);


/*************************************************************************/
void goto_heap_init(int size);
void goto_heap_upheap(int k);
void goto_heap_downheap(int k);
void goto_heap_insert(struct Goto_heap_entry e);
struct Goto_heap_entry goto_heap_remove(void);
struct Goto_heap_entry *goto_heap_top(void);

int find_the_shortest_path(struct player *pplayer, struct unit *punit, 
			   int dest_x, int dest_y);

/**************************************************************************
return -1 if move is not legal
        x if move is legal and costs x 
***************************************************************************/
int goto_tile_cost(struct player *pplayer, struct unit *punit, 
		   int x0, int y0, int x1, int y1)
{
  struct tile *ptile=map_get_tile(x1, y1);
  int moves,c;

  moves = punit->moves_left < 3 ? 3 : punit->moves_left;

  if(map_get_known(x1, y1, pplayer)) {

    if(is_ground_unit(punit)) {
      if(ptile->terrain!=T_OCEAN) {
	struct tile *ptile0=map_get_tile(x0, y0);
	int cost=0;
	
	if(!(ptile0->special&S_RAILROAD)) {
	  if(ptile0->special&S_ROAD)
	    cost=1;
	  else
	  {
	    c = get_tile_type(ptile0->terrain)->movement_cost*3;
	    cost = c < moves ? c : moves;
	  }
	}
	if(!(ptile->special&S_RAILROAD)) {
	  if(ptile->special&S_ROAD)
	    cost++;
	  else
	  {
	    c = get_tile_type(ptile->terrain)->movement_cost*3;
	    cost += c < moves ? c : moves;
	  }
	}
	
	return (cost+1)/2;
      }
      return -1;
    }

    else if(is_sailing_unit(punit))
      return (ptile->terrain==T_OCEAN || ptile->city_id) ? 3 : -1;

    else
      return 3;

  }

  return 3; /* tile is unknown for player => assume average move cost */
}


/**************************************************************************
...
**************************************************************************/
void do_unit_goto(struct player *pplayer, struct unit *punit)
{
  struct Goto_map_entry *me;
  int x, y;

  int id=punit->id;
  if(!is_air_unit(punit) && !is_sailing_unit(punit) && map_get_continent(punit->x, punit->y) != 
     map_get_continent(punit->goto_dest_x, punit->goto_dest_y) && 
     map_get_known(punit->goto_dest_x, punit->goto_dest_y, pplayer) &&
     !map_get_city(punit->x, punit->y) &&
     !map_get_city(punit->goto_dest_x, punit->goto_dest_y)) {
    punit->activity=ACTIVITY_IDLE;
    send_unit_info(0, punit, 0);
    return;
  }

  if(find_the_shortest_path(pplayer, punit, 
			    punit->goto_dest_x, punit->goto_dest_y)) {

    x=punit->x; 
    y=punit->y;

    do {
      me=goto_map_get(x, y);
      x=me->from_x;
      y=me->from_y;

      if(!can_unit_move_to_tile(punit, x, y)) {
	punit->activity=ACTIVITY_IDLE;
	send_unit_info(0, punit, 0);
	return;
      }
      handle_unit_move_request(pplayer, punit, x, y);
      if(!unit_list_find(&pplayer->units, id))
	return; /* unit died during goto! */

      if(punit->x!=x || punit->y!=y) {
	send_unit_info(0, punit, 0);
	return; /* out of movepoints */
      }
    } while(!(x==punit->goto_dest_x && y==punit->goto_dest_y));

  }

  punit->activity=ACTIVITY_IDLE;
  send_unit_info(0, punit, 0);
}



/**************************************************************************
...
**************************************************************************/
int find_the_shortest_path(struct player *pplayer, struct unit *punit, 
			   int dest_x, int dest_y)
{
  struct Goto_heap_entry e;

  goto_map_init();

/*  printf("find path from (%d, %d) to (%d, %d)\n",
	 punit->x, punit->y, dest_x, dest_y); */

  e.cost=0;
  e.from_x=punit->x;
  e.from_y=punit->y;
  e.to_x=punit->x;
  e.to_y=punit->y;

  goto_heap_insert(e);

  while(goto_heap.size) {
    struct Goto_heap_entry e;
    struct Goto_map_entry *pe;
    int cost, nc, x, y, xm1, xp1;

    e=goto_heap_remove();

    cost=e.cost;
    x=e.to_x;   /* adjust */
    y=e.to_y;

    pe=goto_map_get(x, y);

    if(pe->cost==-1) {
      pe->from_x=e.from_x;
      pe->from_y=e.from_y;

      pe->cost=e.cost;

      if(x==dest_x && y==dest_y) { /* ok found it - now reverse the path */
	int x1, x2, y1, y2;

	x1=pe->from_x;
	y1=pe->from_y;
	
	pe=goto_map_get(x1, y1);
	
	x2=pe->from_x;
	y2=pe->from_y;
	
	do {
	  pe->from_x=x;
	  pe->from_y=y;

	  x=x1;
	  y=y1;
	  x1=x2;
	  y1=y2;
	  
	  pe=goto_map_get(x1, y1);
	  
	  x2=pe->from_x;
	  y2=pe->from_y;
	} while(!(x==x1 && y==y1));
	
	return 1;
      }

      e.from_x=x;                           /* prepare to insert e again */
      e.from_y=y;

      if((xp1=x+1)==goto_map.xsize)
	xp1=0;

      if((xm1=x-1)==-1)
	 xm1=goto_map.xsize-1;

      if(y>0) {
	pe=goto_map_get(x, y-1);
	if(pe->cost==-1 && (nc=goto_tile_cost(pplayer, punit, x, y, x, y-1))>=0) {
	  e.to_x=x;
	  e.to_y=y-1;
	  e.cost=cost+nc;
	  goto_heap_insert(e);
	}

	pe=goto_map_get(xm1, y-1);
	if(pe->cost==-1 && (nc=goto_tile_cost(pplayer, punit, x, y, xm1, y-1))>=0) {
	  e.to_x=xm1;
	  e.to_y=y-1;
	  e.cost=cost+nc+1;
	  goto_heap_insert(e);
	}


	pe=goto_map_get(xp1, y-1);
	if(pe->cost==-1 && (nc=goto_tile_cost(pplayer, punit, x, y, xp1, y-1))>=0) {
	  e.to_x=xp1;
	  e.to_y=y-1;
	  e.cost=cost+nc+1;
	  goto_heap_insert(e);
	}
      }

      pe=goto_map_get(xm1, y);
      if(pe->cost==-1 && (nc=goto_tile_cost(pplayer, punit, x, y, xm1, y))>=0) {
	e.to_x=xm1;
	e.to_y=y;
	e.cost=cost+nc;
	goto_heap_insert(e);
      }

      pe=goto_map_get(xp1, y);
      if(pe->cost==-1 && (nc=goto_tile_cost(pplayer, punit, x, y, xp1, y))>=0) {
	e.to_x=xp1;
	e.to_y=y;
	e.cost=cost+nc;
	goto_heap_insert(e);
      }

      if(y<goto_map.ysize-1) {
	pe=goto_map_get(x, y+1);
	if(pe->cost==-1 && (nc=goto_tile_cost(pplayer, punit, x, y, x, y+1))>=0) {
	  e.to_x=x;
	  e.to_y=y+1;
	  e.cost=cost+nc;
	  goto_heap_insert(e);
	}

	pe=goto_map_get(xm1, y+1);
	if(pe->cost==-1 && (nc=goto_tile_cost(pplayer, punit, x, y, xm1, y+1))>=0) {
	  e.to_x=xm1;
	  e.to_y=y+1;
	  e.cost=cost+nc+1;
	  goto_heap_insert(e);
	}


	pe=goto_map_get(xp1, y+1);
	if(pe->cost==-1 && (nc=goto_tile_cost(pplayer, punit, x, y, xp1, y+1))>=0) {
	  e.to_x=xp1;
	  e.to_y=y+1;
	  e.cost=cost+nc+1;
	  goto_heap_insert(e);
	}
      }
    }
  }

  return 0;
}




/**************************************************************************
...
**************************************************************************/
void goto_map_init(void)
{
  int i;

  if(goto_map.xsize!=map.xsize || goto_map.ysize!=map.ysize) {
    free(goto_map.entries);
    goto_map.entries=
	 malloc(map.xsize*map.ysize*sizeof(struct Goto_map_entry));
    goto_map.xsize=map.xsize;
    goto_map.ysize=map.ysize;
  }

  i=map.xsize*map.ysize;
  while(i) 
    (goto_map.entries+(--i))->cost=-1;

  goto_heap_init(map.xsize*map.ysize);
}

/**************************************************************************
...
**************************************************************************/
struct Goto_map_entry *goto_map_get(int x, int y)
{
  return goto_map.entries+x+y*goto_map.xsize;
}



/**************************************************************************
 this heap is much like Sedgewick's in >Algorithms in C<
**************************************************************************/
void goto_heap_init(int size)
{
  if(goto_heap.maxsize!=size) {
    free(goto_heap.entries);
    goto_heap.entries=malloc((size+1)*sizeof(struct Goto_heap_entry));
    goto_heap.maxsize=size;
  }
  goto_heap.size=0;
}

/**************************************************************************
...
**************************************************************************/
void goto_heap_insert(struct Goto_heap_entry e)
{
  assert(goto_heap.size<goto_heap.maxsize);

  goto_heap.size++;

  goto_heap.entries[goto_heap.size]=e;
  goto_heap_upheap(goto_heap.size);
}


/**************************************************************************
...
**************************************************************************/
struct Goto_heap_entry *goto_heap_top(void)
{
  return goto_heap.entries+1;
}
				     
/**************************************************************************
...
**************************************************************************/
struct Goto_heap_entry goto_heap_remove(void)
{
  struct Goto_heap_entry v;

  assert(goto_heap.size>0);


  v=goto_heap.entries[1];
  goto_heap.entries[1]=goto_heap.entries[goto_heap.size--];
  goto_heap_downheap(1);
  return v;
}


/**************************************************************************
...
**************************************************************************/
void goto_heap_upheap(int k)
{
  struct Goto_heap_entry v;

  v=goto_heap.entries[k];
  goto_heap.entries[0].cost=-1;

  while(goto_heap.entries[k/2].cost>v.cost) {
    goto_heap.entries[k]=goto_heap.entries[k/2];
    k/=2;
  }
	
  goto_heap.entries[k]=v;
}

/**************************************************************************
...
**************************************************************************/
void goto_heap_downheap(int k)
{
  int j;
  struct Goto_heap_entry v;

  v=goto_heap.entries[k];

  while(k<=goto_heap.size/2) {
    j=k+k;
    if(j<goto_heap.size && 
       goto_heap.entries[j].cost>goto_heap.entries[j+1].cost)
      j++;
    if(v.cost<=goto_heap.entries[j].cost)
      break;
    goto_heap.entries[k]=goto_heap.entries[j];
    k=j;
  }
  goto_heap.entries[k]=v;
}
