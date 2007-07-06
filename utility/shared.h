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
#ifndef FC__SHARED_H
#define FC__SHARED_H

#include <stdlib.h>		/* size_t */
#include <string.h>		/* memset */
#include <time.h>		/* time_t */

#ifdef HAVE_CONFIG_H
#ifndef FC_CONFIG_H            /* this should be defined in config.h */
#error Files including fcintl.h should also include config.h directly
#endif
#endif

#if __BEOS__
#include <posix/be_prim.h>
#define __bool_true_false_are_defined 1
#else
#ifdef HAVE_STDBOOL_H
#include <stdbool.h>
#else /* Implement <stdbool.h> ourselves */
#undef bool
#undef true
#undef false
#undef __bool_true_false_are_defined
#define bool fc_bool
#define true  1
#define false 0
#define __bool_true_false_are_defined 1
typedef unsigned int fc_bool;
#endif /* ! HAVE_STDBOOL_H */
#endif /* ! __BEOS__ */

/* Want to use GCC's __attribute__ keyword to check variadic
 * parameters to printf-like functions, without upsetting other
 * compilers: put any required defines magic here.
 * If other compilers have something equivalent, could also
 * work that out here.   Should this use configure stuff somehow?
 * --dwp
 */
#if defined(__GNUC__)
#define fc__attribute(x)  __attribute__(x)
#else
#define fc__attribute(x)
#endif


/* Note: the capability string is now in capstr.c --dwp */
/* Version stuff is now in version.h --dwp */

#define WEBSITE_URL "http://www.freeciv.org/"

/* MAX_NUM_PLAYERS + MAX_NUM_BARBARIANS <= 32 !!!! */
#define MAX_NUM_PLAYERS  30
#define MAX_NUM_BARBARIANS   2
#define MAX_NUM_CONNECTIONS (2 * (MAX_NUM_PLAYERS + MAX_NUM_BARBARIANS))
#define MAX_NUM_ITEMS   200	/* eg, unit_types */
#define MAX_NUM_TECH_LIST 10
#define MAX_NUM_BUILDING_LIST 10
#define MAX_LEN_NAME     32
#define MAX_LEN_ADDR     256	/* see also MAXHOSTNAMELEN and RFC 1123 2.1 */
#define MAX_LEN_VET_SHORT_NAME 8
#define MAX_VET_LEVELS 10
#define MAX_LEN_PATH 4095

/* Use FC_INFINITY to denote that a certain event will never occur or
   another unreachable condition. */
#define FC_INFINITY    	(1000 * 1000 * 1000)

#ifdef TRUE
#undef TRUE
#endif

#ifdef FALSE
#undef FALSE
#endif

#define TRUE true
#define FALSE false

#ifndef MAX
#define MAX(x,y) (((x)>(y))?(x):(y))
#define MIN(x,y) (((x)<(y))?(x):(y))
#endif
#define CLIP(lower,this,upper) \
    ((this)<(lower)?(lower):(this)>(upper)?(upper):(this))

/* Note: Solaris already has a WRAP macro that is completely different. */
#define FC_WRAP(value, range)                                               \
    ((value) < 0                                                            \
     ? ((value) % (range) != 0 ? (value) % (range) + (range) : 0)           \
     : ((value) >= (range) ? (value) % (range) : (value)))

#define BOOL_VAL(x) ((x) != 0)
#define XOR(p, q) (!(p) != !(q))

/*
 * DIVIDE() divides and rounds down, rather than just divides and
 * rounds toward 0.  It is assumed that the divisor is positive.
 */
#define DIVIDE(n, d) \
    ( (n) / (d) - (( (n) < 0 && (n) % (d) < 0 ) ? 1 : 0) )

/* Deletes bit no in val,
   moves all bits larger than no one down,
   and inserts a zero at the top. */
#define WIPEBIT(val, no) ( ((~(-1<<(no)))&(val)) \
                           | (( (-1<<((no)+1)) & (val)) >>1) )
/*
 * Yields TRUE iff the bit bit_no is set in val.
 */
#define TEST_BIT(val, bit_no)      	(((val) & (1u << (bit_no))) == (1u << (bit_no)))

/*
 * If cond is TRUE it yields a value where only the bit bit_no is
 * set. If cond is FALSE it yields 0.
 */
#define COND_SET_BIT(cond, bit_no) 	((cond) ? (1u << (bit_no)) : 0)

/* This is duplicated in rand.h to avoid extra includes: */
#define MAX_UINT32 0xFFFFFFFF
#define MAX_UINT16 0xFFFF
#define MAX_UINT8 0xFF

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#define ADD_TO_POINTER(p, n) ((void *)((char *)(p)+(n)))

/****************************************************************************
  Used to initialize an array 'a' of size 'size' with value 'val' in each
  element. Note that the value is evaluated for each element.
****************************************************************************/
#define INITIALIZE_ARRAY(array, size, value)				    \
  {									    \
    int _ini_index;							    \
    									    \
    for (_ini_index = 0; _ini_index < (size); _ini_index++) {		    \
      (array)[_ini_index] = (value);					    \
    }									    \
  }


/* Bitvectors. */
#define _BV_BYTES(bits)		((((bits) - 1) / 8) + 1)
#define _BV_BYTE_INDEX(bits)	((bits) / 8)
#define _BV_BITMASK(bit)	(1u << ((bit) & 0x7))
#ifdef DEBUG
#  define _BV_ASSERT(bv, bit) assert((bit) >= 0 \
                                     && (bit) < sizeof((bv).vec) * 8)
#else
#  define _BV_ASSERT(bv, bit) (void)0
#endif
#define BV_ISSET(bv, bit) \
  (_BV_ASSERT(bv, bit), \
   ((bv).vec[_BV_BYTE_INDEX(bit)] & _BV_BITMASK(bit)) != 0)
#define BV_SET(bv, bit) \
  do { _BV_ASSERT(bv, bit); \
       (bv).vec[_BV_BYTE_INDEX(bit)] |= _BV_BITMASK(bit); } while(FALSE)
#define BV_CLR(bv, bit) \
  do { _BV_ASSERT(bv, bit); \
       (bv).vec[_BV_BYTE_INDEX(bit)] &= ~_BV_BITMASK(bit); } while(FALSE)
#define BV_CLR_ALL(bv) \
  do { memset((bv).vec, 0, sizeof((bv).vec)); } while(FALSE)
#define BV_SET_ALL(bv) \
  do { memset((bv).vec, 0xff, sizeof((bv).vec)); } while(FALSE)
bool bv_check_mask(const unsigned char *vec1, const unsigned char *vec2,
		   size_t size1, size_t size2);
#define BV_CHECK_MASK(vec1, vec2) \
  bv_check_mask((vec1).vec, (vec2).vec, sizeof((vec1).vec), sizeof((vec2).vec))
#define BV_ISSET_ANY(vec) BV_CHECK_MASK(vec, vec)

bool bv_are_equal(const unsigned char *vec1, const unsigned char *vec2,
		  size_t size1, size_t size2);
#define BV_ARE_EQUAL(vec1, vec2) \
  bv_are_equal((vec1).vec, (vec2).vec, sizeof((vec1).vec), sizeof((vec2).vec))

#define BV_DEFINE(name, bits) \
  typedef struct { unsigned char vec[_BV_BYTES(bits)]; } name

char *create_centered_string(const char *s);

char * get_option(const char *option_name,char **argv,int *i,int argc);
bool is_option(const char *option_name,char *option);
int get_tokens(const char *str, char **tokens, size_t num_tokens,
	       const char *delimiterset);

const char *big_int_to_text(unsigned int mantissa, unsigned int exponent);
const char *int_to_text(unsigned int number);

bool is_ascii_name(const char *name);
const char *textyear(int year);
int compare_strings(const void *first, const void *second);
int compare_strings_ptrs(const void *first, const void *second);

char *skip_leading_spaces(char *s);
void remove_leading_trailing_spaces(char *s);
int wordwrap_string(char *s, int len);

bool check_strlen(const char *str, size_t len, const char *errmsg);
size_t loud_strlcpy(char *buffer, const char *str, size_t len,
		   const char *errmsg);
/* Convenience macro. */
#define sz_loud_strlcpy(buffer, str, errmsg) \
    loud_strlcpy(buffer, str, sizeof(buffer), errmsg)

char *end_of_strn(char *str, int *nleft);
int cat_snprintf(char *str, size_t n, const char *format, ...)
     fc__attribute((__format__ (__printf__, 3, 4)));

#define die(...) real_die(__FILE__, __LINE__, __VA_ARGS__)
void real_die(const char *file, int line, const char *format, ...)
      fc__attribute((__format__ (__printf__, 3, 4)));

/**************************************************************************
...
**************************************************************************/
struct datafile {
  char *name;		/* descriptive file name string */
  char *fullname;	/* full absolute filename */
  time_t mtime;		/* last modification time  */
};

#define SPECLIST_TAG datafile
#define SPECLIST_TYPE struct datafile
#include "speclist.h"
#define datafile_list_iterate(list, pnode) \
  TYPED_LIST_ITERATE(struct datafile, list, pnode)
#define datafile_list_iterate_end LIST_ITERATE_END
                                                                               
char *user_home_dir(void);
const char *user_username(void);
const char **datafilelist(const char *suffix);
struct datafile_list datafilelist_infix(const char *subpath,
    const char *infix, bool nodups);
char *datafilename(const char *filename);
char **datafilenames(const char *filename);
char *datafilename_required(const char *filename);

void init_nls(void);
void dont_run_as_root(const char *argv0, const char *fallback);

/*** matching prefixes: ***/

enum m_pre_result {
  M_PRE_EXACT,		/* matches with exact length */
  M_PRE_ONLY,		/* only matching prefix */
  M_PRE_AMBIGUOUS,	/* first of multiple matching prefixes */
  M_PRE_EMPTY,		/* prefix is empty string (no match) */
  M_PRE_LONG,		/* prefix is too long (no match) */
  M_PRE_FAIL,		/* no match at all */
  M_PRE_LAST		/* flag value */
};

const char *m_pre_description(enum m_pre_result result);

/* function type to access a name from an index: */
typedef const char *(*m_pre_accessor_fn_t)(int);

/* function type to compare prefix: */
typedef int (*m_pre_strncmp_fn_t)(const char *, const char *, size_t n);

enum m_pre_result match_prefix(m_pre_accessor_fn_t accessor_fn,
			       size_t n_names,
			       size_t max_len_name,
			       m_pre_strncmp_fn_t cmp_fn,
			       const char *prefix,
			       int *ind_result);

char *get_multicast_group(void);
void interpret_tilde(char* buf, size_t buf_size, const char* filename);

bool make_dir(const char *pathname);
bool path_is_absolute(const char *filename);
#endif  /* FC__SHARED_H */

