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

#ifdef HAVE_CONFIG_H
#include <config.h>
#else

/* This is for the benefit of Imakefile and Makefile.noimake */
/* client/server should always have the same major and minor versions */
/* different patch versions are compatible */
#define MAJOR_VERSION  1
#define MINOR_VERSION  8 
#define PATCH_VERSION  0 
#define VERSION_STRING "1.8.0"
#define IS_BETA_VERSION 0 
#define MAILING_LIST "freeciv-dev@freeciv.org"
#define SITE "http://www.freeciv.org"

#endif /* HAVE_CONFIG_H */

#if IS_BETA_VERSION
#define FREECIV_NAME_VERSION "Freeciv version "##VERSION_STRING" (beta release)"
#define WORD_VERSION "betatest version"
#else
#define FREECIV_NAME_VERSION "Freeciv version "##VERSION_STRING
#define WORD_VERSION "version"
#endif

/* Note: the capability string is now in capstr.c --dwp */

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

#define RANDOM_TYPE unsigned int

char *n_if_vowel(char ch);
char *create_centered_string(char *s);

char *get_option_text(char **argv, int *argcnt, int max_argcnt,
		      char short_option, char *long_option);
char *int_to_text(int nr);
char *get_sane_name(char *name);
char *textyear(int year);
char *get_dot_separated_int(unsigned val);
int mystrcasecmp(char *str0, char *str1);
char *mystrerror(int errnum);
int string_ptr_compare(const void *first, const void *second);

RANDOM_TYPE myrand(int size);
void mysrand(RANDOM_TYPE seed);
void save_restore_random(void);

char *remove_leading_spaces(char *s);
void remove_trailing_spaces(char *s);
void remove_trailing_char(char *s, char trailing);

char *user_home_dir(void);
char *datafilename(char *filename);
char *datafilename_required(char *filename);

#endif  /* FC__SHARED_H */
