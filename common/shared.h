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
#ifndef __SHARED_H
#define __SHARED_H

/* client/server should always have the same major and minor versions */
/* different patch versions are compatible */
#define MAJOR_VERSION  1
#define MINOR_VERSION  6
#define PATCH_VERSION  4
#define VERSION_STRING "1.6.4"

#define FREECIV_NAME_VERSION "Freeciv version "##VERSION_STRING

/* Capabilities: Author: Mitch Davis (mjd@alphalink.com.au)
 *
 * Here's the string a client and server trade to find out if they can talk
 * to each other, and using which protocol version.  The string is a comma-
 * separated list of words, where each word indicates a capability that
 * this version of Freeciv understands.  If a capability word is mandatory,
 * it should start with a "+".
 *
 * eg, #define CAPABILITY "+1.6, MapScroll, +AutoSettlers"
 *
 * Note that there are separate c_capability and s_capability char[]'s in
 * the client and server, which are valid if the client has been accepted
 * by the server.  Client and server software can test these strings for
 * a capability by calling the has_capability fn in capability.c.
 */

/* The default string is really simple */
#define CAPABILITY "+1.6"

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

char* n_if_vowel(char ch);
char *create_centered_string(char *s);

char *get_option_text(char **argv, int *argcnt, int max_argcnt,
		      char short_option, char *long_option);
char *int_to_text(int nr);
char *get_sane_name(char *name);
char *textyear(int year);
char *get_dot_separated_int(unsigned val);
char *mystrdup(char *);
char *minstrdup(char *);
int mystrcasecmp(char *str0, char *str1);
int myrand(int size);
void mysrand(unsigned int seed);
int string_ptr_compare(const void *first, const void *second);

#endif
