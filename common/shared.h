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

/* Note: the capability string is now in capstr.c --dwp */
/* Version stuff is now in version.h --dwp */

#define BUG_EMAIL_ADDRESS "bugs@cvs.freeciv.org"
#define WEBSITE_URL "http://www.freeciv.org"

#define CITY_NAMES_FONT "10x20"
#define BROADCAST_EVENT -2

#define MAX_PLAYERS 14
#define MAX_LENGTH_NAME 32
#define MAX_LENGTH_ADDRESS 32

#ifndef MAX
#define MAX(x,y) (((x)>(y))?(x):(y))
#define MIN(x,y) (((x)<(y))?(x):(y))
#endif

#define WIPEBIT(val, no) ((~(-1<<no))&val)  |   ((  (-1<<(no+1)) &val) >>1)

#define MAX_UINT32 0xFFFFFFFF

typedef unsigned int RANDOM_TYPE;

char *n_if_vowel(char ch);
char *create_centered_string(char *s);

char *get_option_text(char **argv, int *argcnt, int max_argcnt,
		      char short_option, char *long_option);
char *int_to_text(int nr);
char *get_sane_name(char *name);
char *textyear(int year);
char *get_dot_separated_int(unsigned val);
int mystrcasecmp(char *str0, char *str1);
int string_ptr_compare(const void *first, const void *second);

char *mystrerror(int errnum);
void myusleep(unsigned long usec);

RANDOM_TYPE myrand(int size);
void mysrand(RANDOM_TYPE seed);
void save_restore_random(void);

char *remove_leading_spaces(char *s);
void remove_trailing_spaces(char *s);
void remove_trailing_char(char *s, char trailing);
int wordwrap_string(char *s, int len);

char *user_home_dir(void);
char *datafilename(char *filename);
char *datafilename_required(char *filename);

/*Mac constants-resource IDs*/
enum DITL_ids{
  kChatDITL=133,
  kCityDITL=140
};

#endif  /* FC__SHARED_H */
