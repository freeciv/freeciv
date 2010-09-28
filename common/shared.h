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

#define BUG_ADDRESS "http://gna.org/projects/freeciv/"
#define WEBSITE_URL "http://www.freeciv.org/"

#define MAX_NUM_PLAYERS  30
#define MAX_NUM_BARBARIANS   2
#define MAX_NUM_ITEMS   200	/* eg, unit_types */
#define MAX_NUM_TECH_LIST 10
#define MAX_LEN_NAME     32
#define MAX_LEN_ADDR     256	/* see also MAXHOSTNAMELEN and RFC 1123 2.1 */

/* Use FC_INFINITY to denote that a certain event will never occur or
   another unreachable condition. */
#define FC_INFINITY    	(1000 * 1000 * 1000)

#ifndef TRUE
#define TRUE (1)
#endif
#ifndef FALSE
#define FALSE (0)
#endif

typedef int bool;

#ifndef MAX
#define MAX(x,y) (((x)>(y))?(x):(y))
#define MIN(x,y) (((x)<(y))?(x):(y))
#endif
#define CLIP(lower,this,upper) \
    ((this)<(lower)?(lower):(this)>(upper)?(upper):(this))

#define BOOL_VAL(x) ((x)!=0)

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

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#define ADD_TO_POINTER(p, n) ((void *)((char *)(p)+(n)))

/* Bitvectors. */
#define _BV_BYTES(bits)		((((bits) - 1) / 8) + 1)
#define _BV_BYTE_INDEX(bits)	((bits) / 8)
#define _BV_BITMASK(bit)	(1u << ((bit) & 0x7))
#define BV_ISSET(bv, bit) \
  (((bv).vec[_BV_BYTE_INDEX(bit)] & _BV_BITMASK(bit)) != 0)
#define BV_SET(bv, bit) \
  do { (bv).vec[_BV_BYTE_INDEX(bit)] |= _BV_BITMASK(bit); } while(FALSE)
#define BV_CLR(bv, bit) \
  do { (bv).vec[_BV_BYTE_INDEX(bit)] &= ~_BV_BITMASK(bit); } while(FALSE)
#define BV_CLR_ALL(bv) \
  do { memset((bv).vec, 0, sizeof((bv).vec)); } while(FALSE)
bool bv_check_mask(unsigned char *vec1, unsigned char *vec2, size_t size1,
		   size_t size2);
#define BV_CHECK_MASK(vec1, vec2) \
  bv_check_mask((vec1).vec, (vec2).vec, sizeof((vec1).vec), sizeof((vec2).vec))

#define BV_DEFINE(name, bits) \
  typedef struct { unsigned char vec[_BV_BYTES(bits)]; } name

char *create_centered_string(const char *s);

char * get_option(const char *option_name,char **argv,int *i,int argc);
bool is_option(const char *option_name,char *option);
int get_tokens(const char *str, char **tokens, size_t num_tokens,
	       const char *delimiterset);
const char *general_int_to_text(int nr, int decade_exponent);
const char *int_to_text(int nr);
const char *population_to_text(int thousand_citizen);

const char *get_sane_name(const char *name);
const char *textyear(int year);
int compare_strings(const void *first, const void *second);
int compare_strings_ptrs(const void *first, const void *second);

char *skip_leading_spaces(char *s);
void remove_leading_spaces(char *s);
void remove_trailing_spaces(char *s);
void remove_leading_trailing_spaces(char *s);
void remove_trailing_char(char *s, char trailing);
int wordwrap_string(char *s, int len);

bool check_strlen(const char *str, size_t len, const char *errmsg);
size_t loud_strlcpy(char *buffer, const char *str, size_t len,
		   const char *errmsg);
/* Convenience macro. */
#define sz_loud_strlcpy(buffer, str, errmsg) \
    loud_strlcpy(buffer, str, sizeof(buffer), errmsg)

char *end_of_strn(char *str, int *nleft);
int cat_snprintf(char *str, size_t n, const char *format, ...)
     fc__attribute((format (printf, 3, 4)));

char *user_home_dir(void);
char *user_username(void);
const char **datafilelist(const char *suffix);
char *datafilename(const char *filename);
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

const char *freeciv_motto(void);

char *convert_data_string_malloc(const char *text);
char *get_local_charset(void);

#endif  /* FC__SHARED_H */
