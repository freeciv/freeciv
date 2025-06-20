/***********************************************************************
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
   variant of a standard additive number generator. Note that the
   the constants 55 and 24 are not random. Please become familiar with
   this algorithm before you mess with it.

   Since the additive number generator requires a table of numbers from
   which to generate its random sequences, we must invent a way to
   populate that table from a single seed value. I have chosen to do
   this with a different PRNG, known as the "linear congruential method"
   (also found in Knuth, Vol2). I must admit that my choices of constants
   (3, 257, and MAX_UINT32) are probably not optimal, but they seem to
   work well enough for our purposes.

   Original author for this code: Cedric Tefft <cedric@earthling.net>
   Modified to use rand_state struct by David Pfitzner <dwp@mso.anu.edu.au>
*************************************************************************/

#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

/* utility */
#include "log.h"
#include "shared.h"
#include "support.h"            /* TRUE, FALSE */

#include "rand.h"

#define log_rand log_debug


#define STD_RAND_VALUE_LOG(_c, _s, _nr, _f, _l)     \
  log_rand("%s(%lu) = %lu at %s:%d", _c,            \
           (unsigned long) _s, (unsigned long) _nr, \
           _f, _l);


#ifdef LOG_RAND_VALUES
static bool randlog_enabled = TRUE;

/*********************************************************************//**
  Control if generated random numbers get written to log at LOG_NORMAL
  logging level.

  @param  enable  Whether to enable or disable the feature
*************************************************************************/
void enable_randlog(bool enable)
{
  randlog_enabled = enable;
}

#define RAND_VALUE_LOG(_c, _s, _nr, _f, _l)             \
  if (randlog_enabled) {                                \
    log_normal("%lu -> %lu %s:%d",                      \
               (unsigned long) _s, (unsigned long) _nr, \
               fc_basename(_f), _l);                    \
  } else {                                              \
    STD_RAND_VALUE_LOG(_c, _s, _nr, _f, _l);            \
  }
#else
#define RAND_VALUE_LOG STD_RAND_VALUE_LOG
#endif

/* A global random state:
 * Initialized by fc_srand(), updated by fc_rand(),
 * Can be duplicated/saved/restored via fc_rand_state()
 * and fc_rand_set_state().
 */
static RANDOM_STATE rand_state;

/*********************************************************************//**
  Returns a new random value from the sequence, in the interval 0 to
  (size-1) inclusive, and updates global state for next call.
  This means that if size <= 1 the function will always return 0.

  Once we calculate new_rand below uniform (we hope) between 0 and
  MAX_UINT32 inclusive, need to reduce to required range. Using
  modulus is bad because generators like this are generally less
  random for their low-significance bits, so this can give poor
  results when 'size' is small. Instead want to divide the range
  0..MAX_UINT32 into (size) blocks, each with (divisor) values, and
  for any remainder, repeat the calculation of new_rand.
  Then:
         return_val = new_rand / divisor;
  Will repeat for new_rand > max, where:
         max = size * divisor - 1
  Then max <= MAX_UINT32 implies
         size * divisor <= (MAX_UINT32 + 1)
  thus   divisor <= (MAX_UINT32 + 1) / size

  Need to calculate this divisor. Want divisor as large as possible
  given above constraint, but it doesn't hurt us too much if it is
  a bit smaller (just have to repeat more often). Calculation exactly
  as above is complicated by fact that (MAX_UINT32 + 1) may not be
  directly representable in type RANDOM_TYPE, so we do instead:
         divisor = MAX_UINT32 / size
*************************************************************************/
RANDOM_TYPE fc_rand_debug(RANDOM_TYPE size, const char *called_as,
                          int line, const char *file)
{
  RANDOM_TYPE new_rand;

  fc_assert_ret_val(rand_state.is_init, 0);

  if (size > 1) {
    RANDOM_TYPE divisor, max;
    int bailout = 0;

    divisor = MAX_UINT32 / size;
    max = size * divisor - 1;

    do {
      new_rand = (rand_state.v[rand_state.j]
                  + rand_state.v[rand_state.k]) & MAX_UINT32;

      rand_state.x = (rand_state.x +1) % 56;
      rand_state.j = (rand_state.j +1) % 56;
      rand_state.k = (rand_state.k +1) % 56;
      rand_state.v[rand_state.x] = new_rand;

      if (++bailout > 10000) {
        log_error("%s(%lu) = %lu bailout at %s:%d",
                  called_as, (unsigned long) size,
                  (unsigned long) new_rand, file, line);
        new_rand = 0;
        break;
      }

    } while (new_rand > max);

    new_rand /= divisor;
  } else {
    new_rand = 0;
  }

  RAND_VALUE_LOG(called_as, (unsigned long) size,
                 (unsigned long) new_rand, file, line);

  return new_rand;
}

/*********************************************************************//**
  Initialize the generator; see comment at top of file.
*************************************************************************/
void fc_srand(RANDOM_TYPE seed)
{
  int i;

  rand_state.v[0] = (seed & MAX_UINT32);

  for (i = 1; i < 56; i++) {
    rand_state.v[i] = (3 * rand_state.v[i-1] + 257) & MAX_UINT32;
  }

  rand_state.j = (55 - 55);
  rand_state.k = (55 - 24);
  rand_state.x = (55 - 0);

  rand_state.is_init = TRUE;

  /* Heat it up a bit:
   * Using modulus in fc_rand() this was important to pass
   * test_random1(). Now using divisor in fc_rand() that particular
   * test no longer indicates problems, but this seems a good idea
   * anyway -- eg, other tests could well reveal other initial
   * problems even using divisor.
   */
  for (i = 0; i < 10000; i++) {
    (void) fc_rand(MAX_UINT32);
  }
}

/*********************************************************************//**
  Mark fc_rand state uninitialized.
*************************************************************************/
void fc_rand_uninit(void)
{
  rand_state.is_init = FALSE;
}

/*********************************************************************//**
  Return whether the current state has been initialized.
*************************************************************************/
bool fc_rand_is_init(void)
{
  return rand_state.is_init;
}

/*********************************************************************//**
  Return a copy of the current rand_state; eg for save/restore.
*************************************************************************/
RANDOM_STATE fc_rand_state(void)
{
  int i;

  log_rand("fc_rand_state J=%d K=%d X=%d",
           rand_state.j, rand_state.k, rand_state.x);
  for (i  = 0; i < 8; i++) {
    log_rand("fc_rand_state %d, %08x %08x %08x %08x %08x %08x %08x",
             i, rand_state.v[7 * i],
             rand_state.v[7 * i + 1], rand_state.v[7 * i + 2],
             rand_state.v[7 * i + 3], rand_state.v[7 * i + 4],
             rand_state.v[7 * i + 5], rand_state.v[7 * i + 6]);
  }

  return rand_state;
}

/*********************************************************************//**
  Replace current rand_state with user-supplied; eg for save/restore.
  Caller should take care to set state.is_init beforehand if necessary.
*************************************************************************/
void fc_rand_set_state(RANDOM_STATE state)
{
  int i;

  rand_state = state;

  log_rand("fc_rand_set_state J=%d K=%d X=%d",
           rand_state.j, rand_state.k, rand_state.x);
  for (i  = 0; i < 8; i++) {
    log_rand("fc_rand_set_state %d, %08x %08x %08x %08x %08x %08x %08x",
             i, rand_state.v[7 * i],
             rand_state.v[7 * i + 1], rand_state.v[7 * i + 2],
             rand_state.v[7 * i + 3], rand_state.v[7 * i + 4],
             rand_state.v[7 * i + 5], rand_state.v[7 * i + 6]);
  }
}

/*********************************************************************//**
  Test one aspect of randomness, using n numbers.
  Reports results to LOG_TEST; with good randomness, behaviorchange
  and behaviorsame should be about the same size.
  Tests current random state; saves and restores state, so can call
  without interrupting current sequence.
*************************************************************************/
void test_random1(int n)
{
  RANDOM_STATE saved_state;
  int i, old_value = 0, new_value;
  bool didchange, olddidchange = FALSE;
  int behaviorchange = 0, behaviorsame = 0;

  saved_state = fc_rand_state();
  /* fc_srand(time(nullptr)); */  /* Use current state */

  for (i = 0; i < n + 2; i++) {
    new_value = fc_rand(2);
    if (i > 0) {                /* Have old */
      didchange = (new_value != old_value);
      if (i > 1) {              /* Have olddidchange */
        if (didchange != olddidchange) {
          behaviorchange++;
        } else {
          behaviorsame++;
        }
      }
      olddidchange = didchange;
    }
    old_value = new_value;
  }
  log_test("test_random1(%d) same: %d, change: %d",
           n, behaviorsame, behaviorchange);

  /* Restore state: */
  fc_rand_set_state(saved_state);
}

/*********************************************************************//**
  Local pseudo-random function for repeatedly reaching the same result,
  instead of fc_rand(). Primarily needed for tiles.

  Use an invariant equation for seed.
  Result is 0 to (size - 1).
*************************************************************************/
RANDOM_TYPE fc_randomly_debug(RANDOM_TYPE seed, RANDOM_TYPE size,
                              const char *called_as,
                              int line, const char *file)
{
  RANDOM_TYPE result;

#define LARGE_PRIME (10007)
#define SMALL_PRIME (1009)

  /* Check for overflow and underflow */
  fc_assert_ret_val(seed < MAX_UINT32 / LARGE_PRIME, 0);
  fc_assert_ret_val(size < SMALL_PRIME, 0);
  fc_assert_ret_val(size > 0, 0);
  result = ((seed * LARGE_PRIME) % SMALL_PRIME) % size;

  log_rand("%s(%lu,%lu) = %lu at %s:%d",
           called_as, (unsigned long) seed, (unsigned long) size,
           (unsigned long) result, file, line);

  return result;
}
