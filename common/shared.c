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
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>

#if (defined(GENERATING68K) || defined(GENERATINGPPC)) /* mac header(s) */
#include <events.h> /* for WaitNextEvent() */
#else
#include <sys/time.h>
#include <sys/types.h>
#endif
#include <unistd.h>

#include "log.h"
#include "mem.h"

#include "shared.h"

#define BASE_DATA_PATH ".:data:~/.freeciv"

#ifdef FREECIV_DATADIR       /* the configured installation directory */
#define DEFAULT_DATA_PATH BASE_DATA_PATH ":" FREECIV_DATADIR
#else
#define DEFAULT_DATA_PATH BASE_DATA_PATH
#endif

/* Random Number Generator variables */
RANDOM_TYPE RandomState[56];
int iRandJ, iRandK, iRandX; 

/**************************************************************************
... 
**************************************************************************/
char *n_if_vowel(char ch)
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
  
  r=rn=fc_malloc(nlines*(maxlen+1));
  
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

/**************************************************************************
string_ptr_compare() - fiddles with pointers to do a simple string compare
                       when passed pointers to two strings -- called from
                       qsort().
**************************************************************************/

int string_ptr_compare(const void *first, const void *second)
{
   return mystrcasecmp(*((char **)first), *((char **)second));
}

/***************************************************************
...
***************************************************************/
char *mystrerror(int errnum)
{
#if defined(HAVE_STRERROR) || !defined(HAVE_CONFIG_H)
  /* if we have real strerror() or if we're not using configure */
  return strerror(errnum);
#else
  static char buf[64];
  sprintf(buf, "error %d (compiled without strerror)", errnum);
  return buf;
#endif
}

/***************************************************************
...
***************************************************************/
void myusleep(unsigned long usec)
{
#if (defined(GENERATING68K) || defined(GENERATINGPPC)) /* mac timeshare function */
  EventRecord the_event;  /* dummy var for timesharing */
  WaitNextEvent(0, &the_event, GetCaretTime(), nil); /* this is suposed to
     give other application procseor time for the mac*/
#else
#ifndef HAVE_USLEEP
  struct timeval tv;

  tv.tv_sec=0;
  tv.tv_usec=100;
  select(0, NULL, NULL, NULL, &tv);
#else
  usleep(usec);
#endif
#endif
}


/*************************************************************************
   The following random number generator can be found in _The Art of 
   Computer Programming Vol 2._ (2nd ed) by Donald E. Knuth. (C)  1998.
   The algorithm is described in section 3.2.2 as Mitchell and Moore's
   variant of a standard additive number generator.  Note that the
   the constants 55 and 24 are not random.  Please become familiar with
   this algorithm before you mess with it.

   Since the additive number generator requires a table of numbers from
   which to generate its random sequences, we must invent a way to 
   populate that table from a single seed value.  I have chosen to do
   this with a different PRNG, known as the "linear congruential method" 
   (also found in Knuth, Vol2).  I must admit that my choices of constants
   (3, 257, and MAX_UINT32) are probably not optimal, but they seem to
   work well enough for our purposes.
*************************************************************************/

RANDOM_TYPE myrand(int size) 
{ 

    RANDOM_TYPE newRand;

    newRand = (RandomState[iRandJ] + RandomState[iRandK]) & MAX_UINT32;

    iRandX = (iRandX +1) % 56;
    iRandJ = (iRandJ +1) % 56;
    iRandK = (iRandK +1) % 56;
    RandomState[iRandX] = newRand;

    return newRand % size;
    
} 

void mysrand(RANDOM_TYPE seed) 
{ 
    int  i; 

    RandomState[0]=(seed & MAX_UINT32);

    for(i=1;i<56;i++) {
       RandomState[i]= (3 * RandomState[i-1] + 257) & MAX_UINT32;
    }

    iRandJ=(55-55);
    iRandK=(55-24);
    iRandX=(55-0);
} 
 
 
/*************************************************************************
 save_restore_random() - Saves and restores the complete state of the 
   random number generator to a temporary set of variables.  The first 
   call to this function performs a save.  The second call performs 
   a restore.  The third call performs a save again, etc.
*************************************************************************/
void save_restore_random(void)
{
   static RANDOM_TYPE State[56];
   static int j, k, x;
   static int mode=0;

   if(mode) {
      memcpy(RandomState,State,sizeof(RANDOM_TYPE)*56);
      iRandJ=j; iRandK=k; iRandX=x;
   } else {
      memcpy(State,RandomState,sizeof(RANDOM_TYPE)*56);
      j=iRandJ; k=iRandK; x=iRandX;
   }
   mode = (mode+1) % 2;
}

/***************************************************************************
  Returns 's' incremented to first non-space character.
***************************************************************************/
char *remove_leading_spaces(char *s)
{
  assert(s!=NULL);
  while(*s && isspace(*s)) {
    s++;
  }
  return s;
}

/***************************************************************************
  Terminates string pointed to by 's' to remove traling spaces;
  Note 's' must point to writeable memory!
***************************************************************************/
void remove_trailing_spaces(char *s)
{
  char *t;
  
  assert(s!=NULL);
  t = s + strlen(s) -1;
  while(t>=s && isspace(*t)) {
    *t = '\0';
    t--;
  }
}

/***************************************************************************
  As remove_trailing_spaces(), for specified char.
***************************************************************************/
void remove_trailing_char(char *s, char trailing)
{
  char *t;
  
  assert(s!=NULL);
  t = s + strlen(s) -1;
  while(t>=s && (*t) == trailing) {
    *t = '\0';
    t--;
  }
}

/***************************************************************************
  Change spaces in s into newlines, so as to keep lines length len
  or shorter.  That is, modifies s.
  Returns number of lines in modified s.
***************************************************************************/
int wordwrap_string(char *s, int len)
{
  int num_lines = 0;
  
  /* At top of this loop, s points to the rest of string,
   * either at start or after inserted newline: */
 top:
  if (s && *s && strlen(s) > len) {
    char *c;

    num_lines++;
    
    /* check if there is already a newline: */
    for(c=s; c<s+len; c++) {
      if (*c == '\n') {
	s = c+1;
	goto top;
      }
    }
    
    /* find space and break: */
    for(c=s+len; c>s; c--) {
      if (isspace(*c)) {
	*c = '\n';
	s = c+1;
	goto top;
      }
    }

    /* couldn't find a good break; settle for a bad one... */
    for(c=s+len+1; *c; c++) {
      if (isspace(*c)) {
	*c = '\n';
	s = c+1;
	goto top;
      }
    }
  }
  return num_lines;
}

/***************************************************************************
  Returns string which gives users home dir, as specified by $HOME.
  Gets value once, and then caches result.
  If $HOME is not set, give a log message and returns NULL.
  Note the caller should not mess with the returned pointer or
  the string pointed to.
***************************************************************************/
char *user_home_dir(void)
{
  static int init = 0;
  static char *home_dir = NULL;
  
  if (!init) {
    char *env = getenv("HOME");
    if (env) {
      home_dir = mystrdup(env);	        /* never free()d */
      freelog(LOG_VERBOSE, "HOME is %s", home_dir);
    } else {
      freelog(LOG_NORMAL, "Could not find home directory (HOME is not set)");
      home_dir = NULL;
    }
    init = 1;
  }
  return home_dir;
}

/***************************************************************************
  Returns a filename to access the specified file from a data directory;
  The file may be in any of the directories in the data path, which is
  specified internally or may be set as the environment variable
  $FREECIV_PATH.  (A colon-separated list of directories.)
  (For backward compatability the directory specified by $FREECIV_DATADIR
  (if any) is prepended to this path.  (Is this desirable?))
  '~' at the start of a component (provided followed by '/' or '\0')
  is expanded as $HOME.

  If the specified 'filename' is NULL, the returned string contains
  the effective data path.  (But this should probably only be used
  for debug output.)
  
  Returns NULL if the specified filename cannot be found in any of the
  data directories.  (A file is considered "found" if it can be read-opened.)
  The returned pointer points to static memory, so this function can
  only supply one filename at a time.
***************************************************************************/
char *datafilename(char *filename)
{
  static int init = 0;
  static int num_dirs = 0;
  static char **dirs = NULL;
  static char *realfile = NULL;
  static int n_alloc_realfile = 0;
  int i;

  if (!init) {
    char *tok;
    char *path = getenv("FREECIV_PATH");
    char *data = getenv("FREECIV_DATADIR");
    if (!path) {
      path = DEFAULT_DATA_PATH;
    }
    if (data) {
      char *tmp = fc_malloc(strlen(data) + strlen(path) + 2);
      sprintf(tmp, "%s:%s", data, path);
      path = tmp;
    } else {
      path = mystrdup(path);	/* something we can strtok */
    }
    
    tok = strtok(path, ":");
    do {
      int i;			/* strlen(tok), or -1 as flag */
      
      tok = remove_leading_spaces(tok);
      remove_trailing_spaces(tok);
      if (strcmp(tok, "/")!=0) {
	remove_trailing_char(tok, '/');
      }
      
      i = strlen(tok);
      if (tok[0] == '~') {
	if (i>1 && tok[1] != '/') {
	  freelog(LOG_NORMAL, "For \"%s\" in data path cannot expand '~'"
		              " except as '~/'; ignoring", tok);
	  i = 0;   /* skip this one */
	} else {
	  char *home = user_home_dir();
	  if (!home) {
	    freelog(LOG_VERBOSE,
		    "No HOME, skipping data path component %s", tok);
	    i = 0;
	  } else {
	    char *tmp = fc_malloc(strlen(home) + i);    /* +1 -1 */
	    sprintf(tmp, "%s%s", home, tok+1);
	    tok = tmp;
	    i = -1;		/* flag to free tok below */
	  }
	}
      }
      if (i != 0) {
	/* We could check whether the directory exists and
	 * is readable etc?  Don't currently. */
	num_dirs++;
	dirs = fc_realloc(dirs, num_dirs*sizeof(char*));
	dirs[num_dirs-1] = mystrdup(tok);
	freelog(LOG_VERBOSE, "Data path component (%d): %s", num_dirs-1, tok);
	if (i == -1) {
	  free(tok);
	}
      }

      tok = strtok(NULL, ":");
    } while(tok != NULL);

    free(path);
    init = 1;
  }

  if (filename == NULL) {
    int len = 1;		/* in case num_dirs==0 */
    for(i=0; i<num_dirs; i++) {
      len += strlen(dirs[i]) + 1; /* ':' or '\0' */
    }
    if (len > n_alloc_realfile) {
      realfile = fc_realloc(realfile, len);
      n_alloc_realfile = len;
    }
    realfile[0] = '\0';
    for(i=0; i<num_dirs; i++) {
      strcat(realfile, dirs[i]);
      if(i != num_dirs-1) {
	strcat(realfile, ":");
      }
    }
    return realfile;
  }
  
  for(i=0; i<num_dirs; i++) {
    FILE *fp;			/* see if we can open the file */
    int len = strlen(dirs[i]) + strlen(filename) + 2;
    
    if (len > n_alloc_realfile) {
      realfile = fc_realloc(realfile, len);
      n_alloc_realfile = len;
    } 
    sprintf(realfile,"%s/%s", dirs[i], filename);
    fp = fopen(realfile, "r");
    if (fp) {
      fclose(fp);
      return realfile;
    }
  }
  return NULL;
}

/***************************************************************************
  As datafilename(), above, except die with an appropriate log
  message if we can't find the file in the datapath.
***************************************************************************/
char *datafilename_required(char *filename)
{
  char *dname;
  
  assert(filename!=NULL);
  dname = datafilename(filename);

  if (dname) {
    return dname;
  } else {
    freelog(LOG_FATAL, "Could not find readable file \"%s\" in data path",
	    filename);
    freelog(LOG_FATAL, "The data path may be set via"
	               " the environment variable FREECIV_PATH");
    freelog(LOG_FATAL, "Current data path is: \"%s\"", datafilename(NULL));
    exit(1);
  }
}
