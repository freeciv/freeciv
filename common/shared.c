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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include <shared.h>
/**************************************************************************
... 
**************************************************************************/
char* n_if_vowel(char ch)
{
	if (strchr("AEIOUaeiou",ch))
		return "n";
	else
		return "";
}

/***************************************************************
...
***************************************************************/
char *create_centered_string(char *s)
{
  char *cp, *cp0, *r, *rn;
  int i, maxlen, curlen, nlines;

  
  maxlen=0;
  curlen=0;
  nlines=1;
  
  for(cp=s; *cp; cp++) {
    if(*cp!='\n')
      curlen++;
    else {
      if(maxlen<curlen)
	maxlen=curlen;
      curlen=0;
      nlines++;
    }
  }
  if(maxlen<curlen)
    maxlen=curlen;
  
  r=rn=(char *)malloc(nlines*(maxlen+1));
  
  curlen=0;
  for(cp0=cp=s; *cp; cp++) {
    if(*cp!='\n')
      curlen++;
    else {
      for(i=0; i<(maxlen-curlen)/2; i++)
	*rn++=' ';
      memcpy(rn, cp0, curlen);
      rn+=curlen;
      *rn++='\n';
      curlen=0;
      cp0=cp+1;
    }
  }
  
  for(i=0; i<(maxlen-curlen)/2; i++)
    *rn++=' ';
  strcpy(rn, cp0);
  return r;
}


/***************************************************************
...
***************************************************************/
char *get_option_text(char **argv, int *argcnt, int max_argcnt,
		      char short_option, char *long_option)
{
  char s[512];
  
  sprintf(s, "-%c", short_option);
  
  if(!strncmp(s, argv[*argcnt], 2)) {
    if(strlen(s)==strlen(argv[*argcnt])) {
      if(*argcnt<max_argcnt-1) {
	(*argcnt)++;
	return argv[(*argcnt)++];
      }
      else
	return "";
    }
    return 2+argv[(*argcnt)++];
  }
  
  sprintf(s, "--%s", long_option);
  
  if(!strncmp(s, argv[*argcnt], strlen(long_option))) {
    if(strlen(s)==strlen(argv[*argcnt])) {
      if(*argcnt<max_argcnt-1) {
	(*argcnt)++;
	return argv[(*argcnt)++];
      }
      else
	return "";
    }
    return strlen(long_option)+argv[(*argcnt)++];
  }
  
  return 0;
}




/***************************************************************
...
***************************************************************/
char *int_to_text(int nr)
{
  char tmpbuf[12];
  static char buf[20];
  char *to=buf;
  char *from=tmpbuf;
  sprintf(tmpbuf,"%d", nr);
  while (*from) {
    *to++=*from++;
    if (strlen(from)%3==0 && *from)  
      *to++=',';
  }
  *to=0;
  return buf;
}

#if 0
/***************************************************************
pedantic name enforcer - at most one space between words
***************************************************************/
char *get_sane_name(char *name)
{
  static char str[MAX_LENGTH_NAME];
  char *cp;
  
  if(!isalpha(*name))                      /* first char not a letter? */ 
    return 0; 

  for(cp=name; *cp && (isalnum(*cp) || (*cp==' ' && isalnum(*(cp+1)))); cp++);
  if(*cp)
    return 0; 

  strncpy(str, name, MAX_LENGTH_NAME-1);
  str[MAX_LENGTH_NAME-1]='\0';
  
  *str=toupper(*str);
  return str;
}
#endif

static int iso_latin1(char ch)
{
   int i=ch;
   
  /* this works with both signed and unsignd char */
  if (i>=0) {
    if (ch < ' ')  return 0;
    if (ch <= '~')  return 1;
  }
  if (ch < '¡')  return 0;
  return 1;
}

char *get_sane_name(char *name)
{
  static char str[MAX_LENGTH_NAME];
  char *cp;
  
  for(cp=name; iso_latin1(*cp); cp++);
  if(*cp)
    return 0; 

  strncpy(str, name, MAX_LENGTH_NAME-1);
  str[MAX_LENGTH_NAME-1]='\0';
  
  return str;
}



/***************************************************************
...
***************************************************************/
char *mystrdup(char *str)
{
  char *dest=malloc(strlen(str)+1);
  if(dest)
    strcpy(dest, str);
  return dest;
}

/***************************************************************
...
***************************************************************/
char *minstrdup(char *str)
{
  char *dest=malloc(strlen(str)+1);
  char *d2=dest;
  if(dest) {
    while (*str) {
      if (*str=='\\') {
	str++;
	if (*str=='\\') {
	  *dest++='\\';
	  str++;
	} else if (*str=='n') {
	  *dest++='\n';
	  str++;
	}
      } else {
	*dest++=*str++;
      }

    }

    *dest=0;
  }
  return d2;
}

/***************************************************************
...
***************************************************************/
int mystrcasecmp(char *str0, char *str1)
{
  for(; tolower(*str0)==tolower(*str1); str0++, str1++)
    if(*str0=='\0')
      return 0;

  return tolower(*str0)-tolower(*str1);
}

/***************************************************************
...
***************************************************************/
char *get_dot_separated_int(unsigned val)
{
  static char str[16];

  return str;
}

/***************************************************************
...
***************************************************************/
char *textyear(int year)
{
  static char y[10];
  if (year<0) 
    sprintf(y, "%d BC", -year);
  else
    sprintf(y, "%d AD", year);
  return y;
}

int myrand(int size)
{
  /* Is it an operating system which says lrand48 is obsoleted by SVID 3?? */
#ifdef linux
  return random()%size;
#else
  return lrand48()%size;
#endif
}

void mysrand(unsigned int seed)
{
  /* Is it an operating system which says srand48 is obsoleted by SVID 3?? */
#ifdef linux
  srandom(seed);
#else
  srand48(seed);
#endif
}
