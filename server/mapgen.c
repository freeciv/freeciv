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
#include <time.h>
#include <sys/time.h>

#include <log.h>
#include <map.h>
#include <game.h>
#include <shared.h>
#include <assert.h>

#define hmap(x,y) &height_map[(y)*map.xsize+map_adjust_x(x)]

void make_huts(int number);
void add_specials(int prob);
void mapgenerator1(void);
void mapgenerator2(void);
void mapgenerator3(void);
void mapgenerator4(void);
void smooth_map();
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
  make_mountains() will convert all squares that are higher than thill to
  mountains and hills. Notice that thill will be adjusted according to
  the map.mountains value, so increase map.mountains and you'll get more 
  hills and mountains (and vice versa).
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
      thill*=95;
    else 
      thill*=105;
    thill/=100;
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

  for (y=0;y<map.ysize/10;y++) {
    for (x=0;x<map.xsize;x++) {
      if ((full_map(x, y)+(map.ysize/10-y*25)>myrand(maxval) && map_get_terrain(x,y)==T_GRASSLAND) || y==0) { 
	if (y<2)
	  map_set_terrain(x, y, T_ARCTIC);
	else
	  map_set_terrain(x, y, T_TUNDRA);
	  
      } 
    }
  }
  for (y=map.ysize*9/10;y<map.ysize;y++) {
    for (x=0;x<map.xsize;x++) {
      if ((full_map(x, y)+(map.ysize/10-(map.ysize-y)*25)>myrand(maxval) && map_get_terrain(x, y)==T_GRASSLAND) || y==map.ysize-1) {
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
    if (y>map.ysize*42/100 && y<map.ysize*58/100 && myrand(100)>50)
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
      y=(myrand(map.ysize*2/10))+map.ysize*4/10;
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
    y=myrand(map.ysize/10)+map.ysize*45/100;
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
      tres*=11;
    else
      tres*=9;
    tres/=10;
  } while (abs(total-count)> maxval/40);
  make_passable();
  make_mountains(maxval*8/10);
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

  if (map.generator != 1) return;      /* 2,3 and 4 have fixed number of starters */
  /* means: goodies is not initialized for 2,3,4. doesn't matter now, since unused*/

  for (y=0;y<map.ysize;y++)
    for (x=0;x<map.xsize;x++)
      map_set_continent(x, y, 0);
  flood_fill(0,0,1);                   /* want to know where the rim is */
  flood_fill(0,map.ysize-1,2);         /* ... */
  for (y=0;y<map.ysize;y++)
    for (x=0;x<map.xsize;x++) 
      if (!map_get_continent(x, y) && map_get_terrain(x, y)!=T_OCEAN ) {
	if(isles>=MAP_NCONT) isles= MAP_NCONT-1; 
	islands[isles].goodies=0;
	flood_fill(x,y,isles);
	islands[isles].starters=0;
	islands[isles].x=x;
	islands[isles].y=y;
	isles++; 
      }
 
  if (loaded)                 /* only make the continents if loaded game*/
    return;

  /* the arctic and the antarctic are continents 1 and 2 */

  riches=0;
  for (x=3;x<isles;x++) {
      riches+=islands[x].goodies;
  }

  starters= 100; oldisles= isles+1; goodisles= isles;
  while( starters>game.nplayers && oldisles>goodisles ){
    freelog(LOG_DEBUG,"goodisles=%i",goodisles);
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
	  { freelog(LOG_DEBUG,"islands[x].goodies=%i",islands[x].goodies);
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
	{ freelog(LOG_DEBUG,"islands[x].goodies=%i",islands[x].goodies);
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

	freelog(LOG_NORMAL,
	     "mapgen.c#flood_it, not enough start positions, fixing.");
      }
    }
  }
  freelog(LOG_NORMAL,"The map has %i starting positions on %i isles."
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
      freelog(LOG_DEBUG,"starters on isle %i", k);
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
  save_restore_random();
 
  if (map.seed==0)
    map.seed = (myrand(MAX_UINT32) ^ time(NULL)) & (MAX_UINT32 >> 1);

  mysrand(map.seed);
  
  /* don't generate tiles with mapgen==0 as we've loaded them from file */
  /* also, don't delete (the handcrafted!) tiny islands in a scenario */
  if (map.generator != 0) {
    init_workmap();
    /* if one mapgenerator fails, it will choose another mapgenerator */
    /* with a lower number to try again */
    if (map.generator == 4 )
      mapgenerator4();
    if (map.generator == 3 )
      mapgenerator3();
    if( map.generator == 2 )
      mapgenerator2();
    if( map.generator == 1 )
      mapgenerator1();
    filter_land();
  }

  if(game.scenario!=1) /* type 1 scenarios already provide specials */
    add_specials(map.riches); /* hvor mange promiller specials oensker vi*/
  
  /* print_map(); */
  make_huts(map.huts);
  save_restore_random();
}


/**************************************************************************
 readjust terrain counts so that it makes sense for mapgen 1, 2, 3 and 4
 idea: input is the number of terrain
 mapgen 1 needs custom parameters, 
 mapgen 2 and 3 and 4 use percents, currently.
 Ultimately, I hope all parameters below will be weights,
 with the defaults set to a percentage;
 Placing deserts and swamps may cause some problems.

 This function needs to be called from the server everytime a
 a parameter changes.
 It will be called again at game start, too.
**************************************************************************/
void adjust_terrain_param(){
  int total;

  /*!PS: I don't have the time to have several test runs */
  /* to find a mapping from percents to generator 1 settings. */

  if(map.generator==1){
    /*map.riverlength*= 10; */
    /*I leave this out, people will get too upset 
      if they have to change all their scripts */
    return;
  }

  map.riverlength/= 10;/* left in */

    total = map.riverlength + map.mountains + map.deserts 
    + map.forestsize + map.swampsize + map.grasssize;

  if(total>100){
    total = map.riverlength + map.mountains + map.deserts 
    + map.forestsize + map.swampsize + map.grasssize;
    map.forestsize= map.forestsize*100/total;

    total = map.riverlength + map.mountains + map.deserts 
    + map.forestsize + map.swampsize + map.grasssize;
    map.riverlength= map.riverlength*100/total;

    total = map.riverlength + map.mountains + map.deserts 
    + map.forestsize + map.swampsize + map.grasssize;
    map.swampsize= map.swampsize*100/total;

    total = map.riverlength + map.mountains + map.deserts 
    + map.forestsize + map.swampsize + map.grasssize;
    map.mountains= map.mountains*100/total;

    total = map.riverlength + map.mountains + map.deserts 
    + map.forestsize + map.swampsize + map.grasssize;
    map.deserts= map.deserts*100/total;

    total = map.riverlength + map.mountains + map.deserts 
    + map.forestsize + map.swampsize + 0;
    map.grasssize= 100 - total;
  }
  /* if smaller than 100, rest goes implicitly to grass */

  map.riverlength*= 10;
}


/**************************************************************************
  this next block is only for debug purposes and could be removed
**************************************************************************/
char terrai_chars[] = {'a','D', 'F', 'g', 'h', 'j', 'M', '.', 'p', 'R', 's','T', 'U' };

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

void print_imap(void)
{ char tohex[16]="0123456789abcdef";
  int x,y,i;
  for (y=0;y<map.ysize;y++) {
    for (x=0;x<map.xsize;x++) {
      i=map_get_continent(x, y);
      putchar(tohex[i>>4]);
      putchar(tohex[i&15]);
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

/**************************************************************************
  mapgenerator1, highlevel function, that calls all the previous functions
**************************************************************************/
void mapgenerator1(void)
{
  int x,y, i;
  int minval=5000000;
  height_map=(int *) malloc (sizeof(int)*map.xsize*map.ysize);

  adjust_terrain_param();
  
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

void add_specials(int prob)
{
  int x,y;
  for (y=1;y<map.ysize-1;y++)
    for (x=0;x<map.xsize; x++) {
      if ((map_get_terrain(x, y)==T_OCEAN && is_coastline(x,y)) 
	  || (map_get_terrain(x,y)!=T_OCEAN)) {
	if (myrand(1000)<prob) {
	  if (!is_special_close(x,y))
	    map_get_tile(x,y)->special|=S_SPECIAL;
	}
      }
    }
}

/**************************************************************************
  globals for generator 2 & 3
**************************************************************************/
static int isleindex, n, e, s, w;
static long int totalmass; /* better a global than a duplicate formula */

int is_cold(int x, int y){
  return ( y * 5 < map.ysize || y * 5 > map.ysize * 4 );
}

/**************************************************************************
  fill an island with up four types of terrains, rivers have extra code
**************************************************************************/
static void fillisland(int coast, long int *bucket,
		       int warm0_weight, int warm1_weight, 
		       int cold0_weight, int cold1_weight, 
		enum tile_terrain_type warm0, enum tile_terrain_type warm1,
		enum tile_terrain_type cold0, enum tile_terrain_type cold1)
{
  int x, y, i, k, capac;
  long int failsafe;

  if (*bucket <= 0 ) return;
  capac = totalmass;
  i = *bucket / capac;
  i++;
  *bucket -= i * capac;

  k= i;
  failsafe= i*(s-n)*(e-w);
  if(failsafe<0){ failsafe= -failsafe; }

  if(warm0_weight+warm1_weight+cold0_weight+cold1_weight<=0)
    i= 0;

  while (i && failsafe--) {
    y = myrand(s - n) + n;
    x = myrand(e - w) + w;
    if (map_get_continent(x,y) == isleindex &&
		map_get_terrain(x,y) == T_GRASSLAND) {

      /* the first condition helps make terrain more contiguous,
	 the second lets it avoid the coast: */
      if ( ( i*3>k*2 
	     || is_terrain_near_tile(x,y,warm0) 
	     || is_terrain_near_tile(x,y,warm1) 
	     || myrand(100)<50 
	     || is_terrain_near_tile(x,y,cold0) 
	     || is_terrain_near_tile(x,y,cold1) 
	     )
	  &&( !is_terrain_near_tile(x, y, T_OCEAN) || myrand(100) < coast )) {
        if (cold1 != T_RIVER) {
          if ( is_cold(x,y) )
            map_set_terrain(x, y, (myrand(cold0_weight+cold1_weight)<cold0_weight) 
			    ? cold0 : cold1);
          else
            map_set_terrain(x, y, (myrand(warm0_weight+warm1_weight)<warm0_weight) 
			    ? warm0 : warm1);
        } else {
          if (is_water_adjacent_to_tile(x, y) &&
            count_terrain_near_tile(x, y, T_RIVER) < 3)
              map_set_terrain(x, y, T_RIVER);
	    }
      }
      if (map_get_terrain(x,y) != T_GRASSLAND) i--;
    }
  }
}

static long int checkmass;

/**************************************************************************
  finds a place and drop the island created when called with islemass != 0
**************************************************************************/
static int placeisland()
{
  int x, y, xo, yo, i=0;
  yo = myrand(map.ysize)+n-s;
  xo = myrand(map.xsize)+w-e;
  y = n + s / 2;
  x = w + e / 2;

  /* this helps a lot for maps with high landmass */
  for (y = n, x = w ; y < s && x < e ; y++, x++)
    if (*hmap(x, y) && is_coastline(x + xo - w, y + yo - n))
      return 0;
		       
  for (y = n ; y < s ; y++)
    for (x = w ; x < e ; x++)
      if (*hmap(x, y) && is_coastline(x + xo - w, y + yo - n))
        return 0;

  for (y = n ; y < s ; y++) 
    for (x = w ; x < e ; x++) {
      if (*hmap(x, y)) {

	checkmass--; 
	if(checkmass<=0) 
	 { freelog(LOG_NORMAL,"mapgen.c: mass doesn't sum up."); return i; }

        map_set_terrain(xo + x - w, yo + y - n, T_GRASSLAND);
        map_set_continent(xo + x - w, yo + y - n, isleindex);
        i++;
      }
    }
  s += yo - n;
  e += xo - w;
  n = yo;
  w = xo;
  return i;
}

/**************************************************************************
  finds a place and drop the island created when called with islemass != 0
**************************************************************************/
static int createisland(int islemass)
{
  int x, y, i;
  long int tries=islemass*(2+islemass/20)+99;

#include <string.h>

  memset(hmap(0,0), '\0', sizeof(int) * map.xsize * map.ysize);
  y = map.ysize / 2;
  x = map.xsize / 2;
  *hmap(x, y) = 1;
  n = y - 1; w = x - 1;
  s = y + 2; e = x + 2;
  i = islemass - 1;
  while (i && tries-->0) {
    y = myrand(s - n) + n;
    x = myrand(e - w) + w;
    if ((!*hmap(x, y)) && (
		*hmap(x+1, y) || *hmap(x-1, y) ||
		*hmap(x, y+1) || *hmap(x, y-1) )) {
      *hmap(x, y) = 1;
      i--;
      if (y >= s - 1 && s < map.ysize - 2) s++;
      if (x >= e - 1 && e < map.xsize - 2) e++;
      if (y <= n && n > 2) n--;
      if (x <= w && w > 2) w--;
    }
    if (i < islemass / 10) {
      for (y = n ; y < s ; y++)
        for (x = w ; x < e ; x++)
          if ((!*hmap(x, y)) && i && (
		*hmap(x+1, y) && *hmap(x-1, y) &&
		*hmap(x, y+1) && *hmap(x, y-1) )) {
            *hmap(x, y) = 1;
            i--; 
          }
    }
  }
  if(tries<=0)
    freelog(LOG_NORMAL,"createisland ended early with %d/%d.",islemass-i,islemass);

  
  tries= map.xsize*(long int)map.ysize/4;/* on a 40x60 map, there are 2400 places */
  while (!(i = placeisland()) && --tries);
  return i;
}

static long int totalweight;
/**************************************************************************
  make an island, fill every tile type except plains
  note: you have to create big islands first.
**************************************************************************/
static void makeisland(int islemass, int starters)
{
  static long int tilefactor, balance, lastplaced;/* int may be only 2 byte ! */
  static long int riverbuck, mountbuck, desertbuck, forestbuck, swampbuck;

  int i;

  if (!islemass) {
					/* setup initial static parameters */
    balance = 0;
    isleindex = 3; /* 0= none, 1= arctic, 2= antarctic */

    checkmass= totalmass;

    /* caveat: this should really be sent to all players */
    if(totalmass>3000)
      freelog(LOG_NORMAL,"high landmass - this may take a few seconds");

    i = map.riverlength + map.mountains
		+ map.deserts + map.forestsize + map.swampsize;
    i = i <= 90 ? 100 : i * 11 / 10;
    tilefactor = totalmass / i;
    riverbuck = -myrand(totalmass);
    mountbuck = -myrand(totalmass);
    desertbuck = -myrand(totalmass);
    forestbuck = -myrand(totalmass);
    swampbuck = -myrand(totalmass);
    lastplaced = totalmass;
  } else {

   /* makes the islands here */
    islemass = islemass - balance;

    /* don't create continents without a number */
    if(isleindex>=MAP_NCONT)
      return;

    if(islemass>lastplaced+1+lastplaced/50)/*don't create big isles we can't place*/
      islemass= lastplaced+1+lastplaced/50;

    /* isle creation does not perform well for nonsquare islands */
    if(islemass>(map.ysize-6)*(map.ysize-6))
      islemass= (map.ysize-6)*(map.ysize-6);

    if(islemass>(map.xsize-2)*(map.xsize-2))
      islemass= (map.xsize-2)*(map.xsize-2);

    i = islemass;
    if (i <= 0) return;
    islands[isleindex].starters = starters;

    freelog(LOG_DEBUG,"island %i",isleindex);

    while (!createisland(i--) && i*10>islemass );
    i++;
    lastplaced= i;
    if(i*10>islemass){
      balance = i - islemass;
    }else{
      balance = 0;
    }

    freelog(LOG_DEBUG,"ini=%d,plc=%d,bal=%d,tot=%d",islemass,i,balance,checkmass);

    i *= tilefactor;
    riverbuck += map.riverlength / 10 * i;
    fillisland(1, &riverbuck,
	       1,1,1,1,
		T_RIVER, T_RIVER, T_RIVER, T_RIVER);
    mountbuck += map.mountains * i;
    fillisland(20, &mountbuck,
	       3,1, 3,1,
		T_HILLS, T_MOUNTAINS, T_HILLS, T_MOUNTAINS);
    desertbuck += map.deserts * i;
    fillisland(40, &desertbuck,
	       map.deserts, map.deserts, map.deserts, map.deserts,
		T_DESERT, T_DESERT, T_DESERT, T_TUNDRA);
    forestbuck += map.forestsize * i;
    fillisland(60, &forestbuck,
	       map.forestsize, map.swampsize, map.forestsize, map.swampsize,
		T_FOREST, T_JUNGLE, T_FOREST, T_TUNDRA);
    swampbuck += map.swampsize * i;
    fillisland(80, &swampbuck,
	       map.swampsize, map.swampsize, map.swampsize, map.swampsize,
		T_SWAMP, T_SWAMP, T_SWAMP, T_SWAMP);
    isleindex++;
  }
}

/**************************************************************************
  fill ocean and make polar
**************************************************************************/
static void initworld()
{
  int x, y;
  height_map =(int *) malloc (sizeof(int) * map.ysize * map.xsize);
  for (y = 0 ; y < map.ysize ; y++) 
    for (x = 0 ; x < map.xsize ; x++) {
      map_set_terrain(x, y, T_OCEAN);
      map_set_continent(x, y, 0);
    }
  for (x = 0 ; x < map.xsize; x++) {
    map_set_terrain(x, 0, myrand(9) ? T_ARCTIC : T_TUNDRA);
    map_set_continent(x, 0, 1);
    if (!myrand(9)) {
      map_set_terrain(x, 1, myrand(9) ? T_TUNDRA : T_ARCTIC);
      map_set_continent(x, 1, 1);
    }
    map_set_terrain(x, map.ysize-1, myrand(9) ? T_ARCTIC : T_TUNDRA);
    map_set_continent(x, map.ysize-1, 2);
    if (!myrand(9)) {
      map_set_terrain(x, map.ysize-2, myrand(9) ? T_TUNDRA : T_ARCTIC);
      map_set_continent(x, map.ysize-2, 2);
    }
  }
  makeisland(0, 0);
  islands[2].starters = 0;
  islands[1].starters = 0;
  islands[0].starters = 0;
}  

/**************************************************************************
  island base map generators
**************************************************************************/
void mapgenerator2()
{
  int i;
  int spares= 1; 
  /* constant that makes up that an island actually needs additional space */

  if (map.landpercent > 85) {
    map.generator = 1;
    return;
  }

  adjust_terrain_param();
  totalmass = ( (map.ysize-6-spares) * map.landpercent * (map.xsize-spares) ) / 100;

  /*!PS: The weights NEED to sum up to totalweight (dammit) */
  /* copying the flow of the makeisland loops is the safest way */
  totalweight= 0;
  for (i = game.nplayers ; i ; i--)
    totalweight+= 70;
  for (i = game.nplayers ; i ; i--)
    totalweight+= 20;
  for (i = game.nplayers ; i ; i--)
    totalweight+= 10;

  initworld();

  for (i = game.nplayers ; i ; i--)
    makeisland(70*totalmass/totalweight, 1);
  for (i = game.nplayers ; i ; i--)
    makeisland(20*totalmass/totalweight, 0);
  for (i = game.nplayers ; i ; i--)
    makeisland(10*totalmass/totalweight, 0);
  make_plains();  
  free(height_map);

  if(checkmass>map.xsize+map.ysize+totalweight)
    freelog(LOG_DEBUG,"%ld mass left unplaced",checkmass);
}


/* On popular demand, this tries to mimick the generator 3 */
/* as best as possible */
void mapgenerator3()
{
  int spares= 1;
  int j=0;
  
  long int islandmass,landmass, size;
  long int maxmassdiv6=20;
  int bigislands;

  if ( map.landpercent > 80) {
    map.generator = 2;
    return;
  }

  adjust_terrain_param();
  totalmass = ( (map.ysize-6-spares) * map.landpercent * (map.xsize-spares) ) / 100;


  bigislands= game.nplayers;

  landmass= ( map.xsize * (map.ysize-6) * map.landpercent )/100;
  /* subtracting the arctics */
  if( landmass>3*map.ysize+game.nplayers*3 ){
    landmass-= 3*map.ysize;
  }


  islandmass= (landmass)/(3*bigislands);
  if(islandmass<4*maxmassdiv6 )
    islandmass= (landmass)/(2*bigislands);
  if(islandmass<3*maxmassdiv6 && game.nplayers*2<landmass )
    islandmass= (landmass)/(bigislands);

  if( map.xsize < 40 || map.ysize < 40 || map.landpercent>80 )
    { freelog(LOG_NORMAL,"falling back to generator 2"); mapgenerator2(); return; }

  if(islandmass<2)
    islandmass= 2;
  if(islandmass>maxmassdiv6*6)
    islandmass= maxmassdiv6*6;/* !PS: let's try this */

  initworld();

    while( isleindex-2<=bigislands && checkmass>islandmass && ++j<500 ) {
      makeisland(islandmass,1);
    }

  if(j==500){
    freelog(LOG_NORMAL, "generator 3 didn't place all big islands.");
  }
  
  islandmass= (islandmass*11)/8;
  /*!PS: I'd like to mult by 3/2, but starters might make trouble then*/
  if(islandmass<2)
    islandmass= 2;


    while( isleindex<=MAP_NCONT-20 && checkmass>islandmass && ++j< 1500 ) {
      if(j<1000)
	size = myrand((islandmass+1)/2+1)+islandmass/2;
      else
	size = myrand((islandmass+1)/2+1);
      if(size<2) size=2;

      makeisland(size, (isleindex-2<=game.nplayers)?1:0 );
    }

  if(j==1500)
    freelog(LOG_NORMAL, "generator 3 left %li landmass unplaced.",checkmass);
  else if(checkmass>map.xsize+map.ysize+totalweight)
    freelog(LOG_DEBUG,"%ld mass left unplaced",checkmass);

}

void mapgenerator4()
{
  int bigweight=70;
  int spares= 1;
  int i;

  /* no islands with mass >> sqr(min(xsize,ysize)) */

  i = game.nplayers / 2;
  if ( game.nplayers<2 || map.landpercent > 80) {
    map.generator = 3;
    return;
  }

  if(map.landpercent>60)
    bigweight=30;
  else if(map.landpercent>40)
    bigweight=50;
  else
    bigweight=70;

  spares= (map.landpercent-5)/30;

  adjust_terrain_param();
  totalmass = ( (map.ysize-6-spares) * map.landpercent * (map.xsize-spares) ) / 100;

  /*!PS: The weights NEED to sum up to totalweight (dammit) */
  totalweight= 0;
  if (game.nplayers % 2)
    totalweight+= bigweight*3;
  else
    i++;
  while (--i)
    totalweight+= bigweight*2;
  for (i = game.nplayers ; i ; i--)
    totalweight+= 20;
  for (i = game.nplayers ; i ; i--)
    totalweight+= 10;

  initworld();

  i = game.nplayers / 2;
  if (game.nplayers % 2)
    makeisland(bigweight*3, 3);
  else
    i++;
  while (--i)
    makeisland(bigweight*2*totalmass/totalweight, 2);
  for (i = game.nplayers ; i ; i--)
    makeisland(20*totalmass/totalweight, 0);
  for (i = game.nplayers ; i ; i--)
    makeisland(10*totalmass/totalweight, 0);
  make_plains();  
  free(height_map);

  if(checkmass>map.xsize+map.ysize+totalweight)
    freelog(LOG_DEBUG,"%ld mass left unplaced",checkmass);
}
