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

/*************************************************************************
   The following random number generator can be found in _The Art of 
   Computer Programming Vol 2._ (2nd ed) by Donald E. Knuth. (C)  1998.
   The algorithm is described in section 3.2.2 as Mitchell and Moore's
   variant of a standard additive number generator.  Note that the
   the constants 55 and 24 are not random.  Please become familiar with
   this algorithm before you mess with it.

   Since the additive number generator requires a table of numbers from
   which to generate its random sequences, we must invent a way to 
   populate that table from a single seed value.  I have chosen to do
   this with a different PRNG, known as the "linear congruential method" 
   (also found in Knuth, Vol2).  I must admit that my choices of constants
   (3, 257, and MAX_UINT32) are probably not optimal, but they seem to
   work well enough for our purposes.
   
   Original author for this code: Cedric Tefft <cedric@earthling.net>
   Modified to use rand_state struct by David Pfitzner <dwp@mso.anu.edu.au>
*************************************************************************/

#include <assert.h>

#include "rand.h"


/* A global random state:
 * Initialized by mysrand(), updated by myrand(),
 * Can be duplicated/saved/restored via get_myrand_state()
 * and set_myrand_state().
 */
static RANDOM_STATE rand_state;


/*************************************************************************
  Returns a new random value from the sequence, in the interval 0 to
  (size-1) inclusive, and updates global state for next call.
  size should be non-zero, and state should be initialized.
*************************************************************************/
RANDOM_TYPE myrand(RANDOM_TYPE size) 
{
    RANDOM_TYPE new_rand;

    assert(size);
    assert(rand_state.is_init);

    new_rand = (rand_state.v[rand_state.j]
	       + rand_state.v[rand_state.k]) & MAX_UINT32;

    rand_state.x = (rand_state.x +1) % 56;
    rand_state.j = (rand_state.j +1) % 56;
    rand_state.k = (rand_state.k +1) % 56;
    rand_state.v[rand_state.x] = new_rand;

    return new_rand % size;
} 

/*************************************************************************
  Initialize the generator; see comment at top of file.
*************************************************************************/
void mysrand(RANDOM_TYPE seed) 
{ 
    int  i; 

    rand_state.v[0]=(seed & MAX_UINT32);

    for(i=1; i<56; i++) {
       rand_state.v[i] = (3 * rand_state.v[i-1] + 257) & MAX_UINT32;
    }

    rand_state.j = (55-55);
    rand_state.k = (55-24);
    rand_state.x = (55-0);

    rand_state.is_init = 1;
} 

/*************************************************************************
  Return whether the current state has been initialized.
*************************************************************************/
int myrand_is_init(void)
{
  return rand_state.is_init;
}

/*************************************************************************
  Return a copy of the current rand_state; eg for save/restore.
*************************************************************************/
RANDOM_STATE get_myrand_state(void)
{
  return rand_state;
}

/*************************************************************************
  Replace current rand_state with user-supplied; eg for save/restore.
  Caller should take care to set state.is_init beforehand if necessary.
*************************************************************************/
void set_myrand_state(RANDOM_STATE state)
{
  rand_state = state;
}
