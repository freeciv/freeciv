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

#include "attribute.h"

/* Note: the capability string is now in capstr.c --dwp */
/* Version stuff is now in version.h --dwp */

#define BUG_EMAIL_ADDRESS "bugs@freeciv.freeciv.org"
#define WEBSITE_URL "http://www.freeciv.org/"

#define BROADCAST_EVENT -2

#define MAX_NUM_PLAYERS  14
#define MAX_NUM_ITEMS   200	/* eg, unit_types */
#define MAX_NUM_TECH_LIST 10
#define MAX_LEN_NAME     32
#define MAX_LEN_ADDR     32

#ifndef TRUE
#define TRUE (1)
#endif
#ifndef FALSE
#define FALSE (0)
#endif

#ifndef MAX
#define MAX(x,y) (((x)>(y))?(x):(y))
#define MIN(x,y) (((x)<(y))?(x):(y))
#endif
#define CLIP(lower,this,upper) \
    ((this)<(lower)?(lower):(this)>(upper)?(upper):(this))

#define BOOL_VAL(x) ((x)!=0)

#define WIPEBIT(val, no) ( ((~(-1<<(no)))&(val)) \
                           | (( (-1<<((no)+1)) & (val)) >>1) )

/* This is duplicated in rand.h to avoid extra includes: */
#define MAX_UINT32 0xFFFFFFFF

char *n_if_vowel(char ch);
char *create_centered_string(char *s);

char * get_option(const char *option_name,char **argv,int *i,int argc);
int is_option(const char *option_name,char *option);
char *int_to_text(int nr);
char *get_sane_name(char *name);
char *textyear(int year);
int string_ptr_compare(const void *first, const void *second);

char *skip_leading_spaces(char *s);
void remove_trailing_spaces(char *s);
void remove_trailing_char(char *s, char trailing);
int wordwrap_string(char *s, int len);

char *end_of_strn(char *str, int *nleft);
int cat_snprintf(char *str, size_t n, const char *format, ...)
     fc__attribute((format (printf, 3, 4)));

char *user_home_dir(void);
char *user_username(void);
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

char *freeciv_motto(void);

/*Mac constants-resource IDs*/
enum DITL_ids{
  kChatDITL=133,
  kCityDITL=140
};

#endif  /* FC__SHARED_H */
