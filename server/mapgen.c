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
#include <log.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>

#include <map.h>
#include <game.h>
#include <shared.h>

#include <aitools.h>
#include <assert.h>
void make_huts(int number);
void mapgenerator1(void);
void mapgenerator2(void);
void mapgenerator3(void);
void spread(int spr);
void smooth_map();
void add_height(int x,int y, int radius);
void adjust_map(int minval);
void init_workmap(void);
int *height_map;

int maxval=0;
int forests=0;

/**************************************************************************
 Just a wrapper function off the height_map, returns the height at x,y
**************************************************************************/

int full_map(int x, int y)
{
  return height_map[y*map.xsize+x];
}

/**************************************************************************
 is this y coordinate in the arctic zone?
**************************************************************************/
int arctic_y(int y)
{
  return (y < map.ysize*0.1 || y > map.ysize*0.9);
}

/**************************************************************************
 Is this y coordinate in the desert zone?
**************************************************************************/
int desert_y(int y)
{
  return (y > map.ysize*0.4  &&  y < map.ysize*0.6);
}

/**************************************************************************
  make_mountains will convert all squares that is heigher than thill to
  mountains and hills, notice that thill will be adjusted according to
  the map.mountains value, so increase map.mountains and you'll get more 
  hills and mountains, and vice versa  
**************************************************************************/

void make_mountains(int thill)
{
  int x,y;
  int mount;
  int j;
  for (j=0;j<10;j++) {
    mount=0;
    for (y=0;y<map.ysize;y++) 
      for (x=0;x<map.xsize;x++) 
	if (full_map(x, y)>thill) 
	    mount++;
    if (mount<((map.xsize*map.ysize)*map.mountains)/1000) 
      thill*=0.95;
    else 
      thill*=1.05;
  }
  
  for (y=0;y<map.ysize;y++) 
    for (x=0;x<map.xsize;x++) 
      if (full_map(x, y)>thill &&map_get_terrain(x,y)!=T_OCEAN) { 
	if (myrand(100)>75) 
	  map_set_terrain(x, y, T_MOUNTAINS);
	else if (myrand(100)>25) 
	  map_set_terrain(x, y, T_HILLS);
      }
}

/**************************************************************************
 add arctic and tundra squares in the arctic zone. 
 (that is the top 10%, and low 10% of the map)
**************************************************************************/
void make_polar()
{
  int y,x;

  for (y=0;y<map.ysize*0.1;y++) {
    for (x=0;x<map.xsize;x++) {
      if ((full_map(x, y)+(map.ysize*0.1-y*25)>myrand(maxval) && map_get_terrain(x,y)==T_GRASSLAND) || y==0) { 
	if (y<2)
	  map_set_terrain(x, y, T_ARCTIC);
	else
	  map_set_terrain(x, y, T_TUNDRA);
	  
      } 
    }
  }
  for (y=map.ysize*0.9;y<map.ysize;y++) {
    for (x=0;x<map.xsize;x++) {
      if ((full_map(x, y)+(map.ysize*0.1-(map.ysize-y)*25)>myrand(maxval) && map_get_terrain(x, y)==T_GRASSLAND) || y==map.ysize-1) {
	if (y>map.ysize-3)
	  map_set_terrain(x, y, T_ARCTIC);
	else
	  map_set_terrain(x, y, T_TUNDRA);
      }
    }
  }
}

/**************************************************************************
  recursively generate deserts, i use the heights of the map, to make the
  desert unregulary shaped, diff is the recursion stopper, and will be reduced
  more if desert wants to grow in the y direction, so we end up with 
  "wide" deserts. 
**************************************************************************/
void make_desert(int x, int y, int height, int diff) 
{
  if (abs(full_map(x, y)-height)<diff && map_get_terrain(x, y)==T_GRASSLAND) {
    map_set_terrain(x, y, T_DESERT);
    make_desert(x-1,y, height, diff-1);
    make_desert(x,y-1, height, diff-3);
    make_desert(x+1,y, height, diff-1);
    make_desert(x,y+1, height, diff-3);
  }
}

/**************************************************************************
  a recursive function that adds forest to the current location and try
  to spread out to the neighbours, it's called from make_forests until
  enough forest has been planted. diff is again the block function.
  if we're close to equator it will with 50% chance generate jungle instead
**************************************************************************/
void make_forest(int x, int y, int height, int diff)
{
  if (x==0 || x==map.xsize-1 ||y==0 || y==map.ysize-1)
    return;

  if (map_get_terrain(x, y)==T_GRASSLAND) {
    if (y>map.ysize*0.42 && y<map.ysize*0.58 && myrand(100)>50)
      map_set_terrain(x, y, T_JUNGLE);
    else 
      map_set_terrain(x, y, T_FOREST);
      if (abs(full_map(x, y)-height)<diff) {
	if (myrand(10)>5) make_forest(x-1,y, height, diff-5);
	if (myrand(10)>5) make_forest(x,y-1, height, diff-5);
	if (myrand(10)>5) make_forest(x+1,y, height, diff-5);
	if (myrand(10)>5) make_forest(x,y+1, height, diff-5);
      }
    forests++;
  }
}

/**************************************************************************
  makeforest calls make_forest with random grassland locations until there
  has been made enough forests. (the map.forestsize value controls this) 
**************************************************************************/
void make_forests()
{
  int x,y;
  int forestsize=25;
  forestsize=(map.xsize*map.ysize*map.forestsize)/1000;
   do {
    x=myrand(map.xsize);
    y=myrand(map.ysize);
    if (map_get_terrain(x, y)==T_GRASSLAND) {
      make_forest(x,y, full_map(x, y), 25);
    }
    if (myrand(100)>75) {
      y=(myrand((int)(map.ysize*0.2)))+map.ysize*0.4;
      x=myrand(map.xsize);
      if (map_get_terrain(x, y)==T_GRASSLAND) {
	make_forest(x,y, full_map(x, y), 25);
      }
    }
  } while (forests<forestsize);
}

/**************************************************************************
  swamps, is placed on low lying locations, that will typically be close to
  the shoreline. They're put at random (where there is grassland)
  and with 50% chance each of it's neighbour squares will be converted to
  swamp aswell
**************************************************************************/
void make_swamps()
{
  int x,y,i;
  int forever=0;
  for (i=0;i<map.swampsize;) {
    forever++;
    if (forever>1000) return;
    y=myrand(map.ysize);
    x=myrand(map.xsize);
    if (map_get_terrain(x, y)==T_GRASSLAND && full_map(x, y)<(maxval*60)/100) {
      map_set_terrain(x, y, T_SWAMP);
      if (myrand(10)>5 && map_get_terrain(x-1, y)!=T_OCEAN) 
	map_set_terrain(x-1,y, T_SWAMP);
      if (myrand(10)>5 && map_get_terrain(x+1, y)!=T_OCEAN) 
	map_set_terrain(x+1,y, T_SWAMP);
      if (myrand(10)>5 && map_get_terrain(x, y-1)!=T_OCEAN) 
	map_set_terrain(x,y-1, T_SWAMP);
      if (myrand(10)>5 && map_get_terrain(x, y+1)!=T_OCEAN) 
	map_set_terrain(x, y+1, T_SWAMP);
      i++;
    }
  }
}

/*************************************************************************
  make_deserts calls make_desert until we have enough deserts actually
  there is no map setting for how many we want, what happends is that
  we choose a random coordinate in the equator zone and if it's a grassland
  square we call make_desert with this coordinate, we try this 1000 times
**************************************************************************/
void make_deserts()
{
  int x,y,i,j;
  i=map.deserts;
  j=0;
  while (i && j<1000) {
    j++;
    y=myrand((int)(map.ysize*0.1))+map.ysize*0.45;
    x=myrand(map.xsize);
    if (map_get_terrain(x, y)==T_GRASSLAND) {
      make_desert(x,y, full_map(x, y), 50);
      i--;
    }
  }
}
/*************************************************************************
 this recursive function try to make some decent rivers, that run towards
 the ocean, it does this by following a descending path on the map spiced
 with a bit of chance, if it fails to reach the ocean it rolls back.
**************************************************************************/

int make_river(int x,int y) 
{
  int mini=10000;
  int mp;
  int res=0;
  mp=-1;
  if (x==0 || x==map.xsize-1 ||y==0 || y==map.ysize-1)
    return 0;
  if (map_get_terrain(x, y)==T_OCEAN)
    return 1;
  if (is_at_coast(x, y)) {
    map_set_terrain(x, y, T_RIVER);
    return 1;
  }

  if (map_get_terrain(x, y)==T_RIVER )
    return 0;
  map_set_terrain(x, y, map_get_terrain(x,y)+16);
  if (full_map(x, y-1)<mini+myrand(10) && map_get_terrain(x, y)<16) {
    mini=full_map(x, y-1);
    mp=0;
  }
  
  if (full_map(x, y+1)<mini+myrand(11) && map_get_terrain(x, y+1)<16) {
    mini=full_map(x, y+1);
    mp=1;
  }
  if (full_map(x+1, y)<mini+myrand(12) && map_get_terrain(x+1, y)<16) {
    mini=full_map(x+1, y);
    mp=2;
  }
  if (full_map(x-1, y)<mini+myrand(13) && map_get_terrain(x-1, y)<16) {
    mp=3;
  }
  if (mp==-1) {
    map_set_terrain(x, y, map_get_terrain(x, y)-16);
    return 0;
  }
  switch(mp) {
   case 0:
    res=make_river(x,y-1);
    break;
   case 1:
    res=make_river(x,y+1);
    break;
   case 2:
    res=make_river(x+1,y);
    break;
   case 3:
    res=make_river(x-1,y);
    break;
  }
  
  if (res) {
    map_set_terrain(x, y, T_RIVER);
  }
  else
    map_set_terrain(x, y, map_get_terrain(x ,y) - 16);
  return res;
}

/**************************************************************************
  calls make_river until we have enough river tiles (map.riverlength), 
  to stop this potentially never ending loop a miss counts as a river of 
  length one 
**************************************************************************/
void make_rivers()
{
  int x,y,i;
  i=0;

  while (i<map.riverlength) {
    y=myrand(map.ysize);
    x=myrand(map.xsize);
    if (map_get_terrain(x, y)==T_OCEAN ||map_get_terrain(x, y)==T_RIVER)
      continue;
    i+=make_river(x,y);
    i+=1;
  }
}

/**************************************************************************
  make_plains converts 50% of the remaining grassland to plains, this should
  maybe be lowered to 30% or done in batches, like the swamps?
**************************************************************************/
void make_plains()
{
  int x,y;
  for (y=0;y<map.ysize;y++)
    for (x=0;x<map.xsize;x++)
      if (map_get_terrain(x, y)==T_GRASSLAND && myrand(100)>50)
	map_set_terrain(x, y, T_PLAINS);
}

/**************************************************************************
  we want the map to be sailable east-west at least at north and south pole 
  and make it a bit jagged at the edge as well.
  So this procedure converts the second line and the second last line to
  ocean, and 50% of the 3rd and 3rd last line to ocean. 
**************************************************************************/
void make_passable()
{
  int x;
  
  for (x=0;x<map.xsize;x++) {
    map_set_terrain(x, 2, T_OCEAN);
    if (myrand(100)>50) map_set_terrain(x,1,T_OCEAN);
    if (myrand(100)>50) map_set_terrain(x,3,T_OCEAN);
    map_set_terrain(x, map.ysize-3, T_OCEAN);
    if (myrand(100)>50) map_set_terrain(x,map.ysize-2,T_OCEAN);
    if (myrand(100)>50) map_set_terrain(x,map.ysize-4,T_OCEAN);
  } 
  
}

/**************************************************************************
  we don't want huge areas of grass/plains, 
 so we put in a hill here and there, where it gets too 'clean' 
**************************************************************************/

void make_fair()
{
  int x,y;
  for (y=2;y<map.ysize-3;y++) 
    for (x=0;x<map.xsize;x++) {
      if (terrain_is_clean(x,y)) {
	map_set_terrain(x,y, T_HILLS);
	if (myrand(100)>66 && map_get_terrain(x-1, y)!=T_OCEAN)
	  map_set_terrain(x-1,y, T_HILLS);
	if (myrand(100)>66 && map_get_terrain(x+1, y)!=T_OCEAN)
	  map_set_terrain(x+1,y, T_HILLS);
	if (myrand(100)>66 && map_get_terrain(x, y-1)!=T_OCEAN) 
	  map_set_terrain(x,y-1, T_HILLS);
	if (myrand(100)>66 && map_get_terrain(x, y+1)!=T_OCEAN) 
	  map_set_terrain(x,y+1, T_HILLS);
      }
    }
}

/**************************************************************************
  make land simply does it all based on a generated heightmap
  1) with map.landpercent it generates a ocean/grassland map 
  2) it then calls the above functions to generate the different terrains
**************************************************************************/
void make_land()
{
  int x, y;
  int tres=(maxval*map.landpercent)/100;
  int count=0;
  int total=(map.xsize*map.ysize*map.landpercent)/100;
  int forever=0;
  do {
    forever++;
    if (forever>50) break; /* loop elimination */
    count=0;
    for (y=0;y<map.ysize;y++)
      for (x=0;x<map.xsize;x++) {
	if (full_map(x, y)<tres)
	  map_set_terrain(x, y, T_OCEAN);
	else {
	  map_set_terrain(x, y, T_GRASSLAND);
	  count++;
	}
      }
    if (count>total)
      tres=tres*1.1;
    else
      tres=tres*0.9;
  } while (abs(total-count)> maxval/40);
  make_passable();
  make_mountains(maxval*0.8);
  make_forests();
  make_swamps();
  make_deserts();
  make_rivers(); 
  make_plains();
  make_polar();
  make_fair();
}

/**************************************************************************
 returns if this is a 1x1 island (which we remove)
**************************************************************************/
int tiny_island(int x, int y) 
{
  if (map_get_terrain(x,y)==T_OCEAN) return 0;
  if (map_get_terrain(x-1,y)!=T_OCEAN) return 0;
  if (map_get_terrain(x+1,y)!=T_OCEAN) return 0;
  if (map_get_terrain(x,y-1)!=T_OCEAN) return 0;
  if (map_get_terrain(x,y+1)!=T_OCEAN) return 0;
  return 1;
}

/**************************************************************************
  removes all 1x1 islands
**************************************************************************/
void filter_land()
{
  int x,y;
  
  for (y=0;y<map.ysize;y++)
    for (x=0;x<map.xsize;x++) {
      /* Remove Islands that is only 1x1 */
      if (tiny_island(x,y)) {
	map_set_terrain(x,y, T_OCEAN);
      }
    }
}

/**************************************************************************
 a basic floodfill used to give every square a continent number, so we later
 on can ask which continent a given square belongs to.
**************************************************************************/
void flood_fill(int x, int y, int nr)
{
  if (x==-1) x=map.xsize-1;
  if (x==map.xsize) x=0;
  if (y==-1) return;
  if (y==map.ysize) return;
  if (map_get_continent(x, y)) 
    return;

  if (map_get_terrain(x, y)==T_OCEAN){ 
    if(is_sea_usable(x,y))
      islands[nr].goodies+= is_good_tile(x, y);
    return;
  }
  islands[nr].goodies+=is_good_tile(x, y);
  map_set_continent(x, y, nr);
  flood_fill(x-1,y,nr);
  flood_fill(x-1,y+1,nr);
  flood_fill(x-1,y-1,nr);
  flood_fill(x,y-1,nr);
  flood_fill(x,y+1,nr);
  flood_fill(x+1,y-1,nr);
  flood_fill(x+1,y,nr);
  flood_fill(x+1,y+1,nr);
}

/**************************************************************************
 find start points for continents and call flood_fill for all of them 
**************************************************************************/
void flood_it(int loaded)
{
  int x,y;
  int good, mingood, maxgood;
  int isles=3;
  int riches;
  int starters;
  int oldisles, goodisles;
  int guard1=0;

  for (y=0;y<map.ysize;y++)
    for (x=0;x<map.xsize;x++)
      map_set_continent(x, y, 0);
  flood_fill(0,0,1);                   /* want to know where the rim is */
  flood_fill(0,map.ysize-1,2);         /* ... */
  for (y=0;y<map.ysize;y++)
    for (x=0;x<map.xsize;x++) 
      if (!map_get_continent(x, y) && map_get_terrain(x, y)!=T_OCEAN) {
	islands[isles].goodies=0;
	flood_fill(x,y,isles);
	islands[isles].starters=0;
	islands[isles].x=x;
	islands[isles].y=y;
	isles++;
      }
 
/* fnord -- Syela */

  if (loaded)                 /* only make the continents if loaded game*/
    return;

  /* the arctic and the antarctic are continents 1 and 2 */

  riches=0;
  for (x=3;x<isles;x++) {
      riches+=islands[x].goodies;
  }

  starters= 100; oldisles= isles+1; goodisles= isles;
  while( starters>game.nplayers && oldisles>goodisles ){
    flog(LOG_DEBUG,"goodisles=%i",goodisles);
    oldisles= goodisles;
    maxgood= 1;
    mingood= riches;
    good=0; 
    goodisles=0;
    starters= 0;


    /* goody goody */
    for (x=3;x<isles;x++) {	  
      islands[x].starters=0;
      if ( islands[x].goodies > (riches+oldisles-1)/oldisles ) 
	{ 
	  good+=islands[x].goodies; 
	  goodisles++; 
	  if(mingood>islands[x].goodies)
	    mingood= islands[x].goodies;
	  if(maxgood<islands[x].goodies)
	    maxgood= islands[x].goodies;
	}
    }
  
    if(mingood+1<maxgood/game.nplayers)
      mingood= maxgood/game.nplayers; 

    if(goodisles>game.nplayers){
      /* bloody goodies */   
      for (x=3;x<isles;x++) {
	if (( islands[x].goodies*4 > 3*(riches+oldisles-1)/oldisles )
	    &&!(islands[x].goodies > (riches+oldisles-1)/oldisles)
	    )
	  { 
	    good+=islands[x].goodies; 
	    goodisles++; 
	    if(mingood>islands[x].goodies)
	      mingood= islands[x].goodies;
	  }
      }
  
 
      /* starters are loosers */
      for (x=3;x<isles;x++) {
	if (( islands[x].goodies*4 > 3*(riches+oldisles-1)/oldisles )
	    &&!(islands[x].goodies > (riches+oldisles-1)/oldisles)
	    )
	  { flog(LOG_DEBUG,"islands[x].goodies=%i",islands[x].goodies);
	  islands[x].starters= (islands[x].goodies+guard1)/mingood; 
	  if(!islands[x].starters)
	    islands[x].starters+= 1;/* ?PS: may not be enough, guard1(tm) */
	  starters+= islands[x].starters;
	  }
      }
    }

    /* starters are winners */
    for (x=3;x<isles;x++) {
      if ( islands[x].goodies > (riches+oldisles-1)/oldisles 
	&&(assert(!islands[x].starters),!islands[x].starters)
	   )
	{ flog(LOG_DEBUG,"islands[x].goodies=%i",islands[x].goodies);
	  islands[x].starters= (islands[x].goodies+guard1)/mingood; 
	  if(!islands[x].starters)
	    islands[x].starters+= 1;/* ?PS: may not be enough, guard1(tm) */
	  starters+= islands[x].starters;
	}
    }


    riches= good;
    if(starters<game.nplayers){
      { 
	starters= game.nplayers+1;
	goodisles=oldisles; 
	oldisles= goodisles+1;
	riches= (4*riches+3)/3;
	if(mingood/game.nplayers>5)
	  guard1+= mingood/game.nplayers;
	else
	  guard1+=5;

	flog(LOG_NORMAL,
	     "mapgen.c#flood_it, not enough start positions, fixing.");
      }
    }
  }
  flog(LOG_NORMAL,"The map has %i starting positions on %i isles."
       ,starters, goodisles);
}
/**************************************************************************
  where do the different races start on the map? well this function tries
  to spread them out on the different islands.
**************************************************************************/

void choose_start_positions(void)
{
  int nr=0;
  int dist=40;
  int x, y, j=0;

  if(dist>= map.xsize/2)
    dist= map.xsize/2;
  if(dist>= map.ysize/2)
    dist= map.ysize/2;

  { int sum,k;
  sum=0;
  for(k=0;k<99;k++){
    sum+= islands[k].starters;
    if(islands[k].starters!=0) 
      flog(LOG_DEBUG,"%i ",k);
  }
  assert(game.nplayers<=nr+sum);
  }

  while (nr<game.nplayers) {
    x=myrand(map.xsize);
    y=myrand(map.ysize); 
    if (islands[(int)map_get_continent(x, y)].starters) {
      j++;
      if (!is_starter_close(x, y, nr, dist)) {
	islands[(int)map_get_continent(x, y)].starters--;
	map.start_positions[nr].x=x;
	map.start_positions[nr].y=y;
	nr++;
      }else{
	if (j>900-dist*9) {
 	  if(dist>1)
	    dist--;	  	  
	  j=0;
	}
      }
    }
  } 
}

/**************************************************************************
  we have 2 ways of generation a map, 
  1) generates normal maps like in civ
  2) generates 7 equally sized and with equally number of specials islands
**************************************************************************/
void map_fractal_generate(void)
{
  /* random patch code just has to replace this code block */
  if (map.seed==0)
    mysrand(time(NULL));
  else 
    mysrand(map.seed);
  
  /* don't generate tiles with mapgen==0 as we've loaded them from file */
  /* also, don't delete (the handcrafted!) tiny islands in a scenario */
  if (map.generator != 0) {
    init_workmap();
    if (map.generator == 3 ){
      mapgenerator3();
    } else if( map.generator == 2 ){
      mapgenerator2();
    } else {
      mapgenerator1();
    }
    filter_land();
  }

  add_specials(map.riches); /* hvor mange promiller specials oensker vi*/
  
  /* print_map(); */
  make_huts(map.huts);
}

/**************************************************************************
  this next block is only for debug purposes and could be removed
**************************************************************************/
char terrai_chars[] = {'a','D', 'F', 'g', 'h', 'j', 'M', '*', 'p', 'R', '%','T', 'U' };

void print_map(void)
{
  int x,y;
  for (y=0;y<map.ysize;y++) {
    for (x=0;x<map.xsize;x++) {
      putchar(terrai_chars[map_get_terrain(x, y)]);
    }
    puts("");
  }
}

void print_hmap(void)
{
  int x,y;
  for (y=0;y<20;y++) {
    for (x=0;x<20;x++) {
      putchar(terrai_chars[height_map[y*20+x]]);
    }
    puts("");
  }
}

/**************************************************************************
  since the generated map will always have a positive number as minimum height
  i reduce the height so the lowest height is zero, this makes calculations
  easier
**************************************************************************/
void adjust_map(int minval)
{
  int x,y;
  for (y=0;y<map.ysize;y++) {
    for (x=0;x<map.xsize;x++) {
      height_map[y*map.xsize+x]-=minval;
    }
  }
}

/**************************************************************************
  this function will create a map.
**************************************************************************/
void init_workmap(void)
{
  int x,y;
  if(!(map.tiles=(struct tile*)malloc(map.xsize*map.ysize*
				      sizeof(struct tile)))) {
    fprintf(stderr, "malloc failed in mapgen\n");
    exit(1);
  }
  for(y=0;y<map.ysize;y++)
     for(x=0;x<map.xsize;x++)
       tile_init(map_get_tile(x, y));
}

/*****************************************************************************
  The rest of this file is only used in  mapgenerator 2, and should not be 
  confused with the previous stuff.
*****************************************************************************/
int ymax = 20;
int xmax = 20;
int size = 100;
int startsize = 100;
int xs; 
int ys;
int dx;
int dy;

long int landmass;/* long int so that multiplication results fit */

/* statics for fillisland, initialized by setup fill island */

long int riversbucket=0;
long int mountainsbucket=0;
long int hillsbucket=0;
long int forestbucket=0;
long int mixedbucket=0;

long int riverscapacity=0;
long int mountainscapacity=0;
long int hillscapacity=0;
long int forestcapacity=0;
long int mixedcapacity=0;

int riversstep=0;
int mountainsstep=0;
int hillsstep=0;
int foreststep=0;
int mixedstep=0;

/**************************************************************************
  This sets up a line drawer so that numbers of terrain will match
**************************************************************************/
void setup_fillisland(void)
{
  long int total=0;  
  long int swampsize, deserts, mountains, forestsize, riverlength;

  swampsize = ((map.xsize*map.ysize)*map.swampsize+500)/1000;
  deserts   = ((map.xsize*map.ysize)*map.deserts+500)/1000;
  mountains = ((map.xsize*map.ysize)*map.mountains+500)/1000;
  forestsize= ((map.xsize*map.ysize)*map.forestsize+500)/1000;
  riverlength= map.riverlength;

  total+= swampsize;
  total+= deserts;
  total+= riverlength;
  total+= mountains;
  total+= forestsize;
  /* mixed = deserts + swamps */

  if(4*total>3*landmass) /* never allow more than 3/4 strange stuff */{
    swampsize   = ( swampsize*3*landmass )/( 4*total );
    deserts     = ( deserts*3*landmass )/( 4*total );
    riverlength = ( riverlength*3*landmass )/( 4*total );
    mountains   = ( mountains*3*landmass )/( 4*total );
    forestsize  = ( forestsize*3*landmass )/( 4*total );
  }
 
  riversstep   = riverlength;
  mountainsstep= (1*mountains+2)/3;
  hillsstep    = (2*mountains+1)/3;
  foreststep   = forestsize;
  mixedstep    = swampsize+deserts;

  riverscapacity   = (landmass*3)/4;
  mountainscapacity= (landmass*3)/4;
  hillscapacity    = (landmass*3)/4;
  forestcapacity   = (landmass*3)/4;
  mixedcapacity    = (landmass*3)/4;

  /* centered regular line 
  riversbucket    = (-riverscapacity)/2;
  mountainsbucket = (-mountainscapacity)/2;
  hillsbucket     = (-hillscapacity)/2;
  forestbucket    = (-forestcapacity)/2;
  mixedbucket     = (-mixedcapacity)/2;
  */

  /* randomized line */
  riversbucket    = - myrand(riverscapacity);
  mountainsbucket = - myrand(mountainscapacity);
  hillsbucket     = - myrand(hillscapacity);
  forestbucket    = - myrand(forestcapacity);
  mixedbucket     = - myrand(mixedcapacity);
}

/**************************************************************************
  a wrapper
**************************************************************************/
int mini_map(int x, int y)
{
  return height_map[y*xmax + x];
}

/**************************************************************************
  fill an island with different types of terrain,
**************************************************************************/
void fillisland(int xp, int yp)
{
  /* defaults reflect the setup of the original generator2 */
  int rivers = 1;
  int mountains = 3;
  int hills = 6; 
  int forest = 10;
  int mixed  = 5;
  int x,y;
  int dir=-1;
  
  int fudge= 0; /* fudge factor to compensate programming errors (if >0) */
                  
  /* what we do is draw chunks of lines */
  if(size>0) fudge=size;
  riversbucket    += riversstep*(startsize-fudge);
  mountainsbucket += mountainsstep*(startsize-fudge);
  hillsbucket     += hillsstep*(startsize-fudge);
  forestbucket    += foreststep*(startsize-fudge);
  mixedbucket     += mixedstep*(startsize-fudge);  

  if( riversbucket>0 )
    { /* is optimizer able to compress x=a%b; y=a/b; to one divmod asm ?! */
      rivers      = riversbucket/riverscapacity; 
      rivers++;
      riversbucket-= rivers*riverscapacity; 
    }
  else 
    rivers=0;

  if( mountainsbucket>0 )
    { 
      mountains      = mountainsbucket/mountainscapacity; 
      mountains++;
      mountainsbucket-= mountains*mountainscapacity; 
    }
  else 
    mountains=0;

  if( hillsbucket>0 )
    { 
      hills      = hillsbucket/hillscapacity; 
      hills++;
      hillsbucket-= hills*hillscapacity; 
    }
  else 
    hills=0;

  if( forestbucket>0 )
    { 
      forest      = forestbucket/forestcapacity; 
      forest++; 
      forestbucket-= forest*forestcapacity; 
    }
  else 
    forest=0;

  if( mixedbucket>0 )
    { 
      mixed      = mixedbucket/mixedcapacity; 
      mixed++;
      mixedbucket-= mixed*mixedcapacity; 
    }
  else 
    mixed=0;

  /* printf("line: %i/%i\th%i f%i r%i M%i m%i\n",
	 startsize,size,hills,forest,rivers,mountains,mixed); */
 
  x = xp + xmax / 2;
  y = yp + ymax / 2;

  while (rivers >0) {
    x = myrand(xmax) + xp;
    y = myrand(ymax) + yp;
    if (map_get_terrain(x,y) == T_GRASSLAND &&
	( !is_terrain_near_tile(x, y, T_OCEAN) || !myrand(6) )
	) {
      dir = myrand(4);
      map_set_terrain(x, y, T_RIVER); mixed--;
      rivers--;


      while (!is_terrain_near_tile(x, y, T_OCEAN)) {
	switch (dir) {
	case 0:
	  x--;
	  break;
	case 1:
	  y--;
	  break;
	case 2:
	  x++;
	  break;
	case 3:
	  y++;
	  break;
	}
	if (map_get_terrain(x,y) == T_GRASSLAND ){ 
	  map_set_terrain(x,y, T_RIVER); mixed--;
	  rivers--;
	}
	else 
	  break;

	if (is_terrain_near_tile(x, y, T_OCEAN)) 
	  break;

	switch ((dir + myrand(3) - 1 + 4)%4) {
	case 0:
	  x--;
	  break;
	case 1:
	  y--;
	  break;
	case 2:
	  x++;
	  break;
	case 3:
	  y++;
	}
	if (map_get_terrain(x,y) == T_GRASSLAND ){
	  map_set_terrain(x,y, T_RIVER); mixed--;
	  rivers--;
	}
	else
	  break;
      }
    }
  }


  while (mountains >0) {
    x = myrand(xmax) + xp;
    y = myrand(ymax) + yp;
    if (map_get_terrain(x,y) == T_GRASSLAND &&
	( !is_terrain_near_tile(x, y, T_OCEAN) || !myrand(6) )
       ) {
      map_set_terrain(x, y, T_MOUNTAINS);
      mountains--;
    }    
  }

  while (hills >0) {
    x = myrand(xmax) + xp;
    y = myrand(ymax) + yp;
    if (map_get_terrain(x,y) == T_GRASSLAND &&
	( !is_terrain_near_tile(x, y, T_OCEAN) || !myrand(6) )
       ) {
      map_set_terrain(x, y, T_HILLS);
      hills--;
    }    
  }

  while (forest >0) {
    x = myrand(xmax-1) + xp;
    y = myrand(ymax-1) + yp;
    if (map_get_terrain(x,y) == T_GRASSLAND) {
      map_set_terrain(x, y, T_FOREST);
      forest--;
      if (forest>0 && map_get_terrain(x+1,y) == T_GRASSLAND && myrand(3)) {
	map_set_terrain(x+1, y, T_FOREST);
	forest--;
      }
      if (forest>0 && map_get_terrain(x,y+1) == T_GRASSLAND && myrand(3)) {
	map_set_terrain(x, y+1, T_FOREST);
	forest--;
      }
      if (forest>0 && map_get_terrain(x+1,y+1) == T_GRASSLAND && myrand(3)) {
	map_set_terrain(x+1, y+1, T_FOREST);
	forest--;
      }
    }    
  }

  while (mixed > 0) {
    x = myrand(xmax) + xp;
    y = myrand(ymax) + yp;
    if (map_get_terrain(x,y) == T_GRASSLAND) {
      if (arctic_y(y)) {
	map_set_terrain(x, y, T_TUNDRA);
	mixed--;
      } else if (desert_y(y) && is_terrain_near_tile(x, y, T_OCEAN)) {
	map_set_terrain(x, y, T_DESERT);
	mixed--;
	if (mixed>0 && map_get_terrain(x+1,y) == T_GRASSLAND && myrand(3)) {
	  map_set_terrain(x+1, y, T_DESERT);
	  mixed--;
	}
	if (mixed>0 && map_get_terrain(x,y+1) == T_GRASSLAND && myrand(3)) {
	  map_set_terrain(x, y+1, T_DESERT);
	  mixed--;
	}
	if (mixed>0 && map_get_terrain(x+1,y+1) == T_GRASSLAND && myrand(3)) {
	  map_set_terrain(x+1, y+1, T_DESERT);
	  mixed--;
	}
      } else {
	if (desert_y(y))
	  map_set_terrain(x, y, T_JUNGLE);
	else 
	  map_set_terrain(x, y, T_SWAMP);
	mixed--;
      }
    }
  }

  for (y = 0; y < ymax; y++)
    for (x = 0; x < ymax; x++) {
      if (map_get_terrain(xp +x, yp+y) == T_GRASSLAND && !myrand(3)) {
	map_set_terrain(xp+x, y+yp, T_PLAINS);
      }
    }
}

/**************************************************************************
  copy the newly created island to the real map, notice that T_OCEAN is
  used as mask so other island won't get destroyed.
**************************************************************************/
void placeisland(int xp, int yp)
{
  int x,y;
  for (y = 0 ; y < dy ; y++) 
    for (x = 0 ; x < dx ; x++) {
      if (mini_map(xs+x, y+ys) != T_OCEAN) 
	map_set_terrain(xp + x, yp + y, mini_map(xs+x, ys+y));
    }
}

/**************************************************************************
  is there a neighbour square in the 4 basic directions?
**************************************************************************/
int neighbour(int x, int y)
{
  if ( x == 0 || x == xmax - 1 || y==0 || y == ymax -1) return 0;
  return (mini_map(x+1, y) + mini_map(x-1, y) + mini_map(x, y+1) + mini_map(x, y-1));
}

/**************************************************************************
  is there a neighbour square in any of the 8 directions?
**************************************************************************/
int neighbour8(int x, int y)
{
  if ( x == 0 || x == xmax - 1 || y==0 || y == ymax -1) return 0;
  return(neighbour(x,y) + mini_map(x+1, y-1) + mini_map(x-1, y-1) + mini_map(x+1, y+1) + mini_map(x-1, y+1));
}

/**************************************************************************
  we need this to convert tbe 0 based ocean representation to 2 
**************************************************************************/
void floodocean(int x, int y)
{
  if (x < 0 || x == xmax) return;
  if (y < 0 || y == ymax) return;
  if (mini_map(x,y) == 0) {
    height_map[y*xmax + x] = 2;
    floodocean(x - 1, y);
    floodocean(x + 1, y);
    floodocean(x , y - 1);
    floodocean(x , y + 1);
  }
}

/**************************************************************************
  this procedure will generate 1 island
**************************************************************************/
void makeisland(void)
{
  int x,y;
  
  if( xmax*ymax/2< size )
    {    
     flog(LOG_NORMAL,"island too large(%i)\n",startsize);
     size= (xmax-1)*(ymax-1)/2;
    }

  xs = xmax/2 -1;
  ys = ymax/2 -1;
  dx = 3;
  dy = 3;
  for (y = 0 ; y < ymax ; y++) {
    for (x = 0 ; x < xmax ; x++) {
      height_map[y * xmax + x]=0;
    }
  }
  startsize= size;
  height_map[(ymax/2)*xmax + xmax/2] = 1;
  while (size > 0) {
    x = myrand(dx) + xs;
    y = myrand(dy) + ys;
    if (mini_map(x, y) == 0 && neighbour(x,y)) {
      height_map[y*xmax + x] = 1;
      size--;
      if (x <= xs) {
	xs--;
	dx++;
      }
      if (x == xs+dx - 1) dx++;
      if (y <= ys) {
	ys--;
	dy++;
      }
      if (y == ys+dy - 1) dy++;
    }
  }
  for (y = 0 ; y < ymax - 1; y++) 
    for (x = 0; x < xmax - 1; x++) {
      if (neighbour8(x, y) == 1 && mini_map(x, y) == 1) {
	height_map[y*xmax + x] = 0;
	size--;
      }
    }
  floodocean(0,0);
  for (y = 0 ; y < ymax; y++) 
    for (x = 0; x < xmax; x++) {
      if (mini_map(x, y) == 0) {
	height_map[y*xmax + x] = 1;
	size++;
      }
      if (mini_map(x, y) == 1) 
	height_map[y*xmax + x] = T_GRASSLAND;
      else 
	height_map[y*xmax + x] = T_OCEAN;
    }
}

/**************************************************************************
  this procedure finds a place to drop the current island
**************************************************************************/
int findislandspot(int *xplace, int *yplace)
{
  int x, y;
  int rx, ry;
  int taken;
  int tries;
  tries = 100;
  while( tries-->0 ) {
    rx = myrand(map.xsize);
    ry = myrand(map.ysize-dy-6) + 3;
    taken = 0;
    for (y = 0; y < dy && !taken; y++)
      for (x = 0; x < dx && !taken; x++) {
	  if (map_get_terrain(rx + x, ry + y) != T_OCEAN) {
	    taken = 1;
	  }
	}
    if (!taken) {
      *xplace = rx;
      *yplace = ry;
      return 1;
    }
  }
  return 0;
}

/**************************************************************************
  this is mapgenerator2. The highlevel routine that ties the knots together
**************************************************************************/

void mapgenerator2()
{
  int i,j=0;
  int xp,yp;
  int x,y;
  long int islandmass;
  int islands;

  if( game.nplayers<=3 )
    islands= 2*game.nplayers+1;
  else 
    islands= 7;

  landmass= ( map.xsize * (map.ysize-6) * map.landpercent+50 )/100;
  /* we need a factor of 7/10 or sth. like that because the coast
     of islands actually takes up lots of space too */
  islandmass= ( (landmass*7)/10+islands-1 )/islands;

  if( map.xsize < 40 || map.ysize < 25 || map.landpercent>50 || islandmass>350 )
    { flog(LOG_NORMAL,"falling back to generator 1\n"); mapgenerator1(); return; }

  if(islandmass<6)
    islandmass= 6;
  if(islandmass>350)
    islandmass=350;/* !PS: cough */

  while( (xmax*ymax)<3*islandmass)
    { xmax++; ymax++; }
   
  height_map =(int *) malloc (sizeof(int)*xmax*ymax);

  for (y = 0 ; y < map.ysize ; y++) 
    for (x = 0 ; x < map.xsize ; x++) {
      map_set_terrain(x,y, T_OCEAN);
    }
  for (x = 0 ; x < map.xsize; x++) {
    map_set_terrain(x,0, T_ARCTIC);
    if (myrand(100)>50) 
      map_set_terrain(x,1, T_ARCTIC);
    map_set_terrain(x, map.ysize-1, T_ARCTIC);
    if (myrand(100)>50) 
      map_set_terrain(x,map.ysize-2, T_ARCTIC);
  }
  
  setup_fillisland();

    i= islands;
    while( i>0 &&  ++j<500 ) {
      size = islandmass;
     makeisland();
      if( findislandspot(&xp, &yp) )
	{
	  placeisland(xp, yp);
	  fillisland(xp, yp);
	  i--;
	}
    }

  if(j==500)
    flog(LOG_NORMAL, "generator 2 didn't place all islands; %i islands not placed.", i );
  
    /*  print_map();*/
  free(height_map);
}

/**************************************************************************
 this is mapgenerator3. Works exactly as 2, with additional small islands
**************************************************************************/
void mapgenerator3()
{
  int i,j=0;
  int xp,yp;
  int x,y;
  long int islandmass, massleft;
  long int maxmassdiv6=20;
  int bigislands;

  bigislands= game.nplayers;

  landmass= ( map.xsize * (map.ysize-6) * map.landpercent )/100;
  /* subtracting the arctics */
  if( landmass>3*map.ysize+game.nplayers*3 ){
    landmass-= 3*map.ysize;
  }

  massleft= landmass;
  islandmass= (landmass +bigislands-1 )/(3*bigislands);
  if(islandmass<4*maxmassdiv6 )
    islandmass= (landmass +bigislands-1 )/(2*bigislands);
  if(islandmass<3*maxmassdiv6 && game.nplayers*2<landmass )
    islandmass= (landmass)/(bigislands);

  if( map.xsize < 40 || map.ysize < 40 || map.landpercent>80 )
    { flog(LOG_NORMAL,"falling back to generator 2"); mapgenerator2(); return; }

  if(map.landpercent>50)
    { flog(LOG_NORMAL,"high landmass - this may take a few seconds"); }

  if(islandmass<2)
    islandmass= 2;
  if(islandmass>maxmassdiv6*6)
    islandmass= maxmassdiv6*6;/* !PS: let's try this */


  while( xmax*ymax < 3*islandmass )
    { xmax++; ymax++; }
  height_map =(int *) malloc (sizeof(int)*xmax*ymax);

  for (y = 0 ; y < map.ysize ; y++) 
    for (x = 0 ; x < map.xsize ; x++) {
      map_set_terrain(x,y, T_OCEAN);
    }
  for (x = 0 ; x < map.xsize; x++) {
    map_set_terrain(x,0, T_ARCTIC);
    if (myrand(100)>50) 
      map_set_terrain(x,1, T_ARCTIC);
    map_set_terrain(x, map.ysize-1, T_ARCTIC);
    if (myrand(100)>50) 
      map_set_terrain(x,map.ysize-2, T_ARCTIC);
  }
  
  setup_fillisland();

    i= 0;
    while( i<bigislands && massleft>0 && ++j<500 ) {
      size = islandmass;
      makeisland();
      if( findislandspot(&xp, &yp) )
	{
	  placeisland(xp, yp);
	  fillisland(xp, yp);
	  i++;
	  massleft-= islandmass;
	}
    }

  if(j==500)
    flog(LOG_NORMAL, "generator 3 didn't place all big islands.");

  free(height_map);

  islandmass= islandmass/3;
  if(islandmass<20)
    islandmass= (islandmass*11)/8;
  /*!PS: I'd like to mult by 3/2, but starters might make trouble then*/
  if(islandmass<2)
    islandmass= 2;

  xmax= 13; ymax=13;
  while( xmax*ymax < 3*islandmass )
    { xmax++; ymax++; }
  height_map =(int *) malloc (sizeof(int)*xmax*ymax);

  /* !PS: the limit for isles is 100, but I get 
     "failed writing data to socket" for i close 100 */
    while( i<80 && massleft>0 && ++j< 1500 ) {
      if(j<1000)
	size = myrand((islandmass+1)/2+1)+islandmass/2;
      else
	size = myrand((islandmass+1)/2+1);
      if(size<2) size=2;
      makeisland();
      if( findislandspot(&xp, &yp) )
	{
	  placeisland(xp, yp);
	  fillisland(xp, yp);
	  i++;
	  massleft-= startsize;
	}
    }

  if(j==1500)
    flog(LOG_NORMAL, "generator 3 left %li landmass unplaced.",massleft);

    /*  print_map();*/
  free(height_map);

  /* !PS: problem left to solve: make sure players start on big islands */
  /* make big islands further apart ? => new algorithm */
}




/**************************************************************************
  mapgenerator1, highlevel function, that calls all the previous functions
**************************************************************************/
void mapgenerator1(void)
{
  int x,y, i;
  int minval=5000000;
  height_map=(int *) malloc (sizeof(int)*map.xsize*map.ysize);

  
  for (y=0;y<map.ysize;y++) {
    for (x=0;x<map.xsize;x++) {
      height_map[y*map.xsize+x]=myrand(40)+((500-abs(map.ysize/2)-y)/10);
    }
  }
  for (i=0;i<1500;i++) {
    height_map[myrand(map.ysize*map.xsize)]+=myrand(5000);
    if (!(i%100)) {
      smooth_map(); 
    }
  }

  smooth_map(); 
  smooth_map(); 
  smooth_map(); 

  for (y=0;y<map.ysize;y++)
    for (x=0;x<map.xsize;x++) {
      if (full_map(x, y)>maxval) 
	maxval=full_map(x, y);
      if (full_map(x, y)<minval)
	minval=full_map(x, y);
    }
  maxval-=minval;
  adjust_map(minval);

  make_land();
  free(height_map);
}

/**************************************************************************
  smooth_map should be viewed  as a  corrosion function on the map, it levels
  out the differences in the heightmap.
**************************************************************************/

void smooth_map()
{
  int x,y;
  int mx,my,px,py;
  int a;
  
  for (y=0;y<map.ysize;y++) {
    my=map_adjust_y(y-1);
    py=map_adjust_y(y+1);
    for (x=0;x<map.xsize;x++) {
      mx=map_adjust_x(x-1);
      px=map_adjust_x(x+1);
      a=full_map(x, y)*2;
      
      a+=full_map(px, my);
      a+=full_map(mx, my);
      a+=full_map(mx, py);
      a+=full_map(px, py);

      a+=full_map(x, my);
      a+=full_map(mx, y);
      
      a+=full_map(x, py);
      a+=full_map(px, y);

      a+=myrand(60);
      a-=30;
      if (a<0) a=0;
      height_map[y*map.xsize +x]=a/10;
    }
  }
}

/**************************************************************************
  this function spreads out huts on the map, a position can be used for a
  hut if there isn't another hut close and if it's not on the ocean.
**************************************************************************/
void make_huts(int number)
{
  int x,y,l;
  int count=0;
  while ((number*map.xsize*map.ysize)/2000 && count++<map.xsize*map.ysize*2) {
    x=myrand(map.xsize);
    y=myrand(map.ysize);
    l=myrand(6);
    if (map_get_terrain(x, y)!=T_OCEAN && 
	( map_get_terrain(x, y)!=T_ARCTIC || l<3 )
	) {
      if (!is_hut_close(x,y)) {
	number--;
	map_get_tile(x,y)->special|=S_HUT;
	if(map_get_continent(x,y)>=2)
	  islands[(int)map_get_continent(x,y)].goodies+= 3;
      }
    }
  }
}
