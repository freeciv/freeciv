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
 * (Following para replaces previous c_capability and s_capability. --dwp)
 * There is a string our_capability, which gives the capabilities of
 * the running executable, be it client or server.
 * Each "struct connection" also has a capability string, which gives the
 * capability of the executable at the other end of the connection.
 * So for the client, the capability of the server is in
 * aconnection.capability, and for the server, the capabilities of 
 * connected clients are in game.players[i]->conn.capability
 *
 * Client and server software can test these strings for a capability by
 * calling the has_capability fn in capability.c.
 *
 * Note the connection struct is a parameter to the functions to send and
 * receive packets, which may be convenient for adjusting how a packet is
 * sent or interpreted based on the capabilities of the connection.
 *
 * A note to whoever increments the capability string for a new release:
 * It is your responsibility to search the Freeciv code, and look for places
 * where people are using has_capability.  If you're taking a capability out
 * of the string, because now every client and server supports it, then
 * you should take out the if(has_capability( code so that this code is
 * always executed.
 */

/* The default string is really simple */
#define CAPABILITY "+1.8 caravan1"
/* caravan1 means to server automatically established a traderoute
   when a caravan type unit moves into an enemy city.  For older
   servers the client has to explicitly ask for a trade route.
*/

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
RANDOM_TYPE myrand(int size);
void mysrand(RANDOM_TYPE seed);
int string_ptr_compare(const void *first, const void *second);
void save_restore_random(void);

char *datafilename(char *filename);    /* used to be in client/climisc */

#endif
