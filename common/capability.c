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

#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "capability.h"

/* This routine returns true if the capability in cap appears
 * in the capability list in capstr.  The capabilities in capstr
 * are allowed to start with a "+", but the capability in cap must not.
 * Note that we copy capstr, because our tokenising modifies it.
 * Double note: You can't call strtok recursively, so I have to put my
 * own grokking in here.
 */
int has_capability(char *cap, char *capstr)
{
  char *capstr_, *token, *next;
  int res=0, finished=0;

  token = capstr_ = strdup(capstr);
  do {
    /* skip leading whitespace */
    while (isspace(*token))
      token++;
    
    /* skip to end of token */
    next = token;
    while (*next && !(isspace(*next) || *next == ','))
      next++;

    if (*next == 0)
      finished = 1;
    
    *next = 0;

    if (*token == '+')
      token++;
    if (strcmp(token, cap)==0)
      {
	res = 1;
	break;
      }
  } while (next++, token = next, !finished);

  free(capstr_);
  return res;
}

/* This routine returns true if all the mandatory capabilities in
 * us appear in them.  Note that we copy us, because strtok modifies it.
 */
int has_capabilities(char *us, char *them)
{
  char *us_, *token;
  int res=1;

  us_ = strdup(us);
  token = strtok(us_, ", ");
  while (token != NULL)
  {
    if (*token == '+' && !has_capability(token+1, them))
    {
      res = 0;
      break;
    }
    token = strtok(NULL, ", ");
  }

  free(us_);
  return res;
}
