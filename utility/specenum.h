
 /**************************************************************************
 *                       THIS FILE WAS GENERATED                           *
 * Script: utility/generate_specenum.py                                    *
 *                       DO NOT CHANGE THIS FILE                           *
 **************************************************************************/

/********************************************************************** 
 Freeciv - Copyright (C) 2009
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
***********************************************************************/

/*
 * Include this file to define tools to manage enumerators.  First of all,
 * before including this file, you *MUST* define the following macros:
 * - SPECENUM_NAME: is the name of the enumeration (e.g. 'foo' for defining
 * 'enum foo').
 * - SPECENUM_VALUE%d: define like this all values of your enumeration type
 * (e.g. '#define SPECENUM_VALUE0 FOO_FIRST').
 *
 * The following macros *CAN* be defined:
 * - SPECENUM_INVALID: specifies a value that your 'foo_invalid()' function
 * will return.  Note it cannot be a declared value with SPECENUM_VALUE%d.
 * - SPECENUM_BITWISE: defines if the enumeration should be like
 * [1, 2, 4, 8, etc...] instead of the default of [0, 1, 2, 3, etc...].
 * - SPECENUM_ZERO: can be defined only if SPECENUM_BITWISE was also defined.
 * It defines a 0 value.  Note that if you don't declare this value, 0 passed
 * to the 'foo_is_valid()' function will return 0.
 * SPECENUM_VALUE%dNAME and SPECENUM_ZERONAME: Can be used to bind the name
 * of the particular enumerator.  If not defined, the default name for
 * 'FOO_FIRST' is '"FOO_FIRST"'.
 *
 * Assuming SPECENUM_NAME were 'foo', including this file would provide
 * the definition for the enumeration type 'enum foo', and prototypes for
 * the following functions:
 *   bool foo_is_bitwise(void);
 *   enum foo foo_min(void);
 *   enum foo foo_max(void);
 *   enum foo foo_invalid(void);
 *   bool foo_is_valid(enum foo);
 *
 *   enum foo foo_begin(void);
 *   enum foo foo_end(void);
 *   enum foo foo_next(enum foo);
 *
 *   const char *foo_name(enum foo);
 *   enum foo foo_by_name(const char *name,
 *                        int (*strcmp_func)(const char *, const char *));
 *
 * Example:
 *   #define SPECENUM_NAME test
 *   #define SPECENUM_BITWISE
 *   #define SPECENUM_VALUE0 TEST0
 *   #define SPECENUM_VALUE1 TEST1
 *   #define SPECENUM_VALUE3 TEST3
 *   #include "specenum.h"
 *
 *  {
 *    static const char *strings[] = {
 *      "TEST1", "test3", "fghdf", NULL
 *    };
 *    enum test e;
 *    int i;
 *
 *    freelog(LOG_VERBOSE, "enum test [%d; %d]%s",
 *            test_min(), test_max(), test_bitwise ? " bitwise" : "");
 *
 *    for (e = test_begin(); e != test_end(); e = test_next(e)) {
 *      freelog(LOG_VERBOSE, "Value %d is %s", e, test_name(e));
 *    }
 *
 *    for (i = 0; strings[i]; i++) {
 *      e = test_by_name(strings[i], strcasecmp);
 *      if (test_is_valid(e)) {
 *        freelog(LOG_VERBOSE, "Value is %d for %s", e, strings[i]);
 *      } else {
 *        freelog(LOG_VERBOSE, "%s is not a valid name", strings[i]);
 *      }
 *    }
 *  }
 *
 * Will output:
 *   enum test [1, 8] bitwise
 *   Value 1 is TEST0
 *   Value 2 is TEST1
 *   Value 8 is TEST3
 *   Value is 2 for TEST1
 *   Value is 8 for test3
 *   fghdf is not a valid name
 */

#include "support.h"    /* bool type. */

#ifndef SPECENUM_NAME
#error Must define a SPECENUM_NAME to use this header
#endif

#define SPECENUM_PASTE_(x, y) x ## y
#define SPECENUM_PASTE(x, y) SPECENUM_PASTE_(x, y)

#define SPECENUM_STRING_(x) #x
#define SPECENUM_STRING(x) SPECENUM_STRING_(x)

#define SPECENUM_FOO(suffix) SPECENUM_PASTE(SPECENUM_NAME, suffix)

#ifndef SPECENUM_INVALID
#define SPECENUM_INVALID (-1)
#endif

#ifdef SPECENUM_BITWISE
#define SPECENUM_VALUE(value) (1 << value)
#else /* SPECENUM_BITWISE */
#ifdef SPECENUM_ZERO
#error Cannot define SPECENUM_ZERO when SPECENUM_BITWISE is not defined.
#endif
#define SPECENUM_VALUE(value) (value)
#endif /* SPECENUM_BITWISE */

#undef SPECENUM_MIN_VALUE
#undef SPECENUM_MAX_VALUE

/* Enumeration definition. */
enum SPECENUM_NAME {
#ifdef SPECENUM_ZERO
  SPECENUM_ZERO = 0,
#endif

#ifdef SPECENUM_VALUE0
  SPECENUM_VALUE0 = SPECENUM_VALUE(0),
#ifndef SPECENUM_MIN_VALUE
#define SPECENUM_MIN_VALUE SPECENUM_VALUE0
#endif
#ifdef SPECENUM_MAX_VALUE
#undef SPECENUM_MAX_VALUE
#endif
#define SPECENUM_MAX_VALUE SPECENUM_VALUE0
#endif /* SPECENUM_VALUE0 */

#ifdef SPECENUM_VALUE1
  SPECENUM_VALUE1 = SPECENUM_VALUE(1),
#ifndef SPECENUM_MIN_VALUE
#define SPECENUM_MIN_VALUE SPECENUM_VALUE1
#endif
#ifdef SPECENUM_MAX_VALUE
#undef SPECENUM_MAX_VALUE
#endif
#define SPECENUM_MAX_VALUE SPECENUM_VALUE1
#endif /* SPECENUM_VALUE1 */

#ifdef SPECENUM_VALUE2
  SPECENUM_VALUE2 = SPECENUM_VALUE(2),
#ifndef SPECENUM_MIN_VALUE
#define SPECENUM_MIN_VALUE SPECENUM_VALUE2
#endif
#ifdef SPECENUM_MAX_VALUE
#undef SPECENUM_MAX_VALUE
#endif
#define SPECENUM_MAX_VALUE SPECENUM_VALUE2
#endif /* SPECENUM_VALUE2 */

#ifdef SPECENUM_VALUE3
  SPECENUM_VALUE3 = SPECENUM_VALUE(3),
#ifndef SPECENUM_MIN_VALUE
#define SPECENUM_MIN_VALUE SPECENUM_VALUE3
#endif
#ifdef SPECENUM_MAX_VALUE
#undef SPECENUM_MAX_VALUE
#endif
#define SPECENUM_MAX_VALUE SPECENUM_VALUE3
#endif /* SPECENUM_VALUE3 */

#ifdef SPECENUM_VALUE4
  SPECENUM_VALUE4 = SPECENUM_VALUE(4),
#ifndef SPECENUM_MIN_VALUE
#define SPECENUM_MIN_VALUE SPECENUM_VALUE4
#endif
#ifdef SPECENUM_MAX_VALUE
#undef SPECENUM_MAX_VALUE
#endif
#define SPECENUM_MAX_VALUE SPECENUM_VALUE4
#endif /* SPECENUM_VALUE4 */

#ifdef SPECENUM_VALUE5
  SPECENUM_VALUE5 = SPECENUM_VALUE(5),
#ifndef SPECENUM_MIN_VALUE
#define SPECENUM_MIN_VALUE SPECENUM_VALUE5
#endif
#ifdef SPECENUM_MAX_VALUE
#undef SPECENUM_MAX_VALUE
#endif
#define SPECENUM_MAX_VALUE SPECENUM_VALUE5
#endif /* SPECENUM_VALUE5 */

#ifdef SPECENUM_VALUE6
  SPECENUM_VALUE6 = SPECENUM_VALUE(6),
#ifndef SPECENUM_MIN_VALUE
#define SPECENUM_MIN_VALUE SPECENUM_VALUE6
#endif
#ifdef SPECENUM_MAX_VALUE
#undef SPECENUM_MAX_VALUE
#endif
#define SPECENUM_MAX_VALUE SPECENUM_VALUE6
#endif /* SPECENUM_VALUE6 */

#ifdef SPECENUM_VALUE7
  SPECENUM_VALUE7 = SPECENUM_VALUE(7),
#ifndef SPECENUM_MIN_VALUE
#define SPECENUM_MIN_VALUE SPECENUM_VALUE7
#endif
#ifdef SPECENUM_MAX_VALUE
#undef SPECENUM_MAX_VALUE
#endif
#define SPECENUM_MAX_VALUE SPECENUM_VALUE7
#endif /* SPECENUM_VALUE7 */

#ifdef SPECENUM_VALUE8
  SPECENUM_VALUE8 = SPECENUM_VALUE(8),
#ifndef SPECENUM_MIN_VALUE
#define SPECENUM_MIN_VALUE SPECENUM_VALUE8
#endif
#ifdef SPECENUM_MAX_VALUE
#undef SPECENUM_MAX_VALUE
#endif
#define SPECENUM_MAX_VALUE SPECENUM_VALUE8
#endif /* SPECENUM_VALUE8 */

#ifdef SPECENUM_VALUE9
  SPECENUM_VALUE9 = SPECENUM_VALUE(9),
#ifndef SPECENUM_MIN_VALUE
#define SPECENUM_MIN_VALUE SPECENUM_VALUE9
#endif
#ifdef SPECENUM_MAX_VALUE
#undef SPECENUM_MAX_VALUE
#endif
#define SPECENUM_MAX_VALUE SPECENUM_VALUE9
#endif /* SPECENUM_VALUE9 */
};

/**************************************************************************
  Returns TRUE if this enumeration is in bitwise mode.
**************************************************************************/
static inline bool SPECENUM_FOO(_is_bitwise)(void)
{
#ifdef SPECENUM_BITWISE
  return TRUE;
#else
  return FALSE;
#endif
}

/**************************************************************************
  Returns the value of the minimal enumerator.
**************************************************************************/
static inline enum SPECENUM_NAME SPECENUM_FOO(_min)(void)
{
  return SPECENUM_MIN_VALUE;
}

/**************************************************************************
  Returns the value of the maximal enumerator.
**************************************************************************/
static inline enum SPECENUM_NAME SPECENUM_FOO(_max)(void)
{
  return SPECENUM_MAX_VALUE;
}

/**************************************************************************
  Returns TRUE if this enumerator was defined.
**************************************************************************/
static inline bool SPECENUM_FOO(_is_valid)(enum SPECENUM_NAME enumerator)
{
  switch (enumerator) {
#ifdef SPECENUM_ZERO
  case SPECENUM_ZERO:
#endif

#ifdef SPECENUM_VALUE0
  case SPECENUM_VALUE0:
#endif

#ifdef SPECENUM_VALUE1
  case SPECENUM_VALUE1:
#endif

#ifdef SPECENUM_VALUE2
  case SPECENUM_VALUE2:
#endif

#ifdef SPECENUM_VALUE3
  case SPECENUM_VALUE3:
#endif

#ifdef SPECENUM_VALUE4
  case SPECENUM_VALUE4:
#endif

#ifdef SPECENUM_VALUE5
  case SPECENUM_VALUE5:
#endif

#ifdef SPECENUM_VALUE6
  case SPECENUM_VALUE6:
#endif

#ifdef SPECENUM_VALUE7
  case SPECENUM_VALUE7:
#endif

#ifdef SPECENUM_VALUE8
  case SPECENUM_VALUE8:
#endif

#ifdef SPECENUM_VALUE9
  case SPECENUM_VALUE9:
#endif

    return TRUE;
  }

  return FALSE;
}

/**************************************************************************
  Returns an invalid enumerator value.
**************************************************************************/
static inline enum SPECENUM_NAME SPECENUM_FOO(_invalid)(void)
{
  assert(!SPECENUM_FOO(_is_valid(SPECENUM_INVALID)));
  return SPECENUM_INVALID;
}

/**************************************************************************
  Beginning of the iteration of the enumerators.
**************************************************************************/
static inline enum SPECENUM_NAME SPECENUM_FOO(_begin)(void)
{
  return SPECENUM_FOO(_min)();
}

/**************************************************************************
  End of the iteration of the enumerators.
**************************************************************************/
static inline enum SPECENUM_NAME SPECENUM_FOO(_end)(void)
{
  return SPECENUM_FOO(_invalid)();
}

/**************************************************************************
  Find the next valid enumerator value.
**************************************************************************/
static inline enum SPECENUM_NAME SPECENUM_FOO(_next)(enum SPECENUM_NAME e)
{
  do {
#ifdef SPECENUM_BITWISE
    e <<= 1;
#else
    e++;
#endif

    if (e > SPECENUM_FOO(_max)()) {
      /* End of the iteration. */
      return SPECENUM_FOO(_invalid)();
    }
  } while (!SPECENUM_FOO(_is_valid)(e));

  return e;
}

/**************************************************************************
  Returns the name of the enumerator.
**************************************************************************/
static inline const char *SPECENUM_FOO(_name)(enum SPECENUM_NAME enumerator)
{
  switch (enumerator) {
#ifdef SPECENUM_ZERO
  case SPECENUM_ZERO:
#ifdef SPECENUM_ZERONAME
    return SPECENUM_ZERONAME;
#else
    return SPECENUM_STRING(SPECENUM_ZERO);
#endif
#endif /* SPECENUM_ZERO */

#ifdef SPECENUM_VALUE0
  case SPECENUM_VALUE0:
#ifdef SPECENUM_VALUE0NAME
    return SPECENUM_VALUE0NAME;
#else
    return SPECENUM_STRING(SPECENUM_VALUE0);
#endif
#endif /* SPECENUM_VALUE0 */

#ifdef SPECENUM_VALUE1
  case SPECENUM_VALUE1:
#ifdef SPECENUM_VALUE1NAME
    return SPECENUM_VALUE1NAME;
#else
    return SPECENUM_STRING(SPECENUM_VALUE1);
#endif
#endif /* SPECENUM_VALUE1 */

#ifdef SPECENUM_VALUE2
  case SPECENUM_VALUE2:
#ifdef SPECENUM_VALUE2NAME
    return SPECENUM_VALUE2NAME;
#else
    return SPECENUM_STRING(SPECENUM_VALUE2);
#endif
#endif /* SPECENUM_VALUE2 */

#ifdef SPECENUM_VALUE3
  case SPECENUM_VALUE3:
#ifdef SPECENUM_VALUE3NAME
    return SPECENUM_VALUE3NAME;
#else
    return SPECENUM_STRING(SPECENUM_VALUE3);
#endif
#endif /* SPECENUM_VALUE3 */

#ifdef SPECENUM_VALUE4
  case SPECENUM_VALUE4:
#ifdef SPECENUM_VALUE4NAME
    return SPECENUM_VALUE4NAME;
#else
    return SPECENUM_STRING(SPECENUM_VALUE4);
#endif
#endif /* SPECENUM_VALUE4 */

#ifdef SPECENUM_VALUE5
  case SPECENUM_VALUE5:
#ifdef SPECENUM_VALUE5NAME
    return SPECENUM_VALUE5NAME;
#else
    return SPECENUM_STRING(SPECENUM_VALUE5);
#endif
#endif /* SPECENUM_VALUE5 */

#ifdef SPECENUM_VALUE6
  case SPECENUM_VALUE6:
#ifdef SPECENUM_VALUE6NAME
    return SPECENUM_VALUE6NAME;
#else
    return SPECENUM_STRING(SPECENUM_VALUE6);
#endif
#endif /* SPECENUM_VALUE6 */

#ifdef SPECENUM_VALUE7
  case SPECENUM_VALUE7:
#ifdef SPECENUM_VALUE7NAME
    return SPECENUM_VALUE7NAME;
#else
    return SPECENUM_STRING(SPECENUM_VALUE7);
#endif
#endif /* SPECENUM_VALUE7 */

#ifdef SPECENUM_VALUE8
  case SPECENUM_VALUE8:
#ifdef SPECENUM_VALUE8NAME
    return SPECENUM_VALUE8NAME;
#else
    return SPECENUM_STRING(SPECENUM_VALUE8);
#endif
#endif /* SPECENUM_VALUE8 */

#ifdef SPECENUM_VALUE9
  case SPECENUM_VALUE9:
#ifdef SPECENUM_VALUE9NAME
    return SPECENUM_VALUE9NAME;
#else
    return SPECENUM_STRING(SPECENUM_VALUE9);
#endif
#endif /* SPECENUM_VALUE9 */

  }

  return NULL;
}

/**************************************************************************
  Returns the enumerator for the name or *_invalid() if not found.
**************************************************************************/
static inline enum SPECENUM_NAME SPECENUM_FOO(_by_name)
    (const char *name, int (*strcmp_func)(const char *, const char *))
{
  enum SPECENUM_NAME e;
  const char *enum_name;

  for (e = SPECENUM_FOO(_begin)(); e != SPECENUM_FOO(_end)();
       e = SPECENUM_FOO(_next)(e)) {
    if ((enum_name = SPECENUM_FOO(_name)(e))
        && 0 == strcmp_func(name, enum_name)) {
      return e;
    }
  }

  return SPECENUM_FOO(_invalid)();
}

#undef SPECENUM_NAME
#undef SPECENUM_PASTE_
#undef SPECENUM_PASTE
#undef SPECENUM_STRING_
#undef SPECENUM_STRING
#undef SPECENUM_FOO
#undef SPECENUM_INVALID
#undef SPECENUM_BITWISE
#undef SPECENUM_VALUE
#undef SPECENUM_ZERO
#undef SPECENUM_MIN_VALUE
#undef SPECENUM_MAX_VALUE
#undef SPECENUM_VALUE0
#undef SPECENUM_VALUE1
#undef SPECENUM_VALUE2
#undef SPECENUM_VALUE3
#undef SPECENUM_VALUE4
#undef SPECENUM_VALUE5
#undef SPECENUM_VALUE6
#undef SPECENUM_VALUE7
#undef SPECENUM_VALUE8
#undef SPECENUM_VALUE9
#undef SPECENUM_ZERONAME
#undef SPECENUM_VALUE0NAME
#undef SPECENUM_VALUE1NAME
#undef SPECENUM_VALUE2NAME
#undef SPECENUM_VALUE3NAME
#undef SPECENUM_VALUE4NAME
#undef SPECENUM_VALUE5NAME
#undef SPECENUM_VALUE6NAME
#undef SPECENUM_VALUE7NAME
#undef SPECENUM_VALUE8NAME
#undef SPECENUM_VALUE9NAME
