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

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_PWD_H
#include <pwd.h>
#endif

#include "astring.h"
#include "fcintl.h"
#include "log.h"
#include "mem.h"
#include "support.h"

#include "shared.h"

/* If no path separator is defined use colon */
#ifndef PATH_SEPARATOR
#define PATH_SEPARATOR ":"
#endif

/* If no default data path is defined use the default default one */
#ifndef DEFAULT_DATA_PATH
#define DEFAULT_DATA_PATH "." PATH_SEPARATOR "data" PATH_SEPARATOR \
                          "~/.freeciv"
#endif

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

/**************************************************************************
  return a char * to the parameter of the option or NULL.
  *i can be increased to get next string in the array argv[]. 
 **************************************************************************/

char * get_option(const char *option_name,char **argv,int *i,int argc)
{
  int len = strlen(option_name);

  if (!strcmp(option_name,argv[*i]) ||
      (!strncmp(option_name,argv[*i],len) && argv[*i][len]=='=') ||
      !strncmp(option_name+1,argv[*i],2)) {
    char *opt = argv[*i] + (argv[*i][1] != '-' ? 0 : len);

    if (*opt == '=') {
      opt++;
    } else {
      if (*i < argc - 1) {
	(*i)++;
	opt = argv[*i];
      }	else {
	fprintf(stderr, _("Missing argument for \"%s\".\n"), option_name);
	exit(1);
      }
    }

    return opt;
  }

  return NULL;
}

/***************************************************************
...
***************************************************************/

int is_option(const char *option_name,char *option)
{
  if (!strcmp(option_name,option) || 
      !strncmp(option_name+1,option,2)) return 1;
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
  my_snprintf(tmpbuf, sizeof(tmpbuf), "%d", nr);
  while (*from) {
    *to++ = *from++;
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
  static char str[MAX_LEN_NAME];
  char *cp;
  
  if(!isalpha(*name))                      /* first char not a letter? */ 
    return 0; 

  for(cp=name; *cp && (isalnum(*cp) || (*cp==' ' && isalnum(*(cp+1)))); cp++);
  if(*cp)
    return 0; 

  sz_strlcpy(str, name);
  
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
  static char str[MAX_LEN_NAME];
  char *cp;
  
  for(cp=name; iso_latin1(*cp); cp++);
  if(*cp)
    return 0; 

  sz_strlcpy(str, name);
  
  return str;
}

/***************************************************************
...
***************************************************************/
char *textyear(int year)
{
  static char y[32];
  if (year<0) 
    my_snprintf(y, sizeof(y), _("%d BC"), -year);
  else
    my_snprintf(y, sizeof(y), _("%d AD"), year);
  return y;
}

/**************************************************************************
string_ptr_compare() - fiddles with pointers to do a simple string compare
                       when passed pointers to two strings -- called from
                       qsort().
**************************************************************************/

int string_ptr_compare(const void *first, const void *second)
{
   return mystrcasecmp(*((const char **)first), *((const char **)second));
}

/***************************************************************************
  Returns 's' incremented to first non-space character.
***************************************************************************/
char *skip_leading_spaces(char *s)
{
  assert(s!=NULL);
  while(*s && isspace(*s)) {
    s++;
  }
  return s;
}

/***************************************************************************
  Removes leading spaces in string pointed to by 's'.
  Note 's' must point to writeable memory!
***************************************************************************/
void remove_leading_spaces(char *s)
{
  char *t;
  
  assert(s!=NULL);
  t = skip_leading_spaces(s);
  if (t != s) {
    while (*t) {
      *s++ = *t++;
    }
    *s = '\0';
  }
}

/***************************************************************************
  Terminates string pointed to by 's' to remove traling spaces;
  Note 's' must point to writeable memory!
***************************************************************************/
void remove_trailing_spaces(char *s)
{
  char *t;
  size_t len;
  
  assert(s!=NULL);
  len = strlen(s);
  if (len) {
    t = s + len -1;
    while(isspace(*t)) {
      *t = '\0';
      if (t == s) {
	break;
      }
      t--;
    }
  }
}

/***************************************************************************
  Removes leading and trailing spaces in string pointed to by 's'.
  Note 's' must point to writeable memory!
***************************************************************************/
void remove_leading_trailing_spaces(char *s)
{
  remove_leading_spaces(s);
  remove_trailing_spaces(s);
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
  Returns pointer to '\0' at end of string 'str', and decrements
  *nleft by the length of 'str'.  This is intended to be useful to
  allow strcat-ing without traversing the whole string each time,
  while still keeping track of the buffer length.
  Eg:
     char buf[128];
     int n = sizeof(buf);
     char *p = buf;

     my_snprintf(p, n, "foo%p", p);
     p = end_of_strn(p, &n);
     mystrlcpy(p, "yyy", n);
***************************************************************************/
char *end_of_strn(char *str, int *nleft)
{
  int len = strlen(str);
  *nleft -= len;
  assert((*nleft)>0);		/* space for the terminating nul */
  return str + len;
}

/********************************************************************** 
 cat_snprintf is like a combination of my_snprintf and mystrlcat;
 it does snprintf to the end of an existing string.
 
 Like mystrlcat, n is the total length available for str, including
 existing contents and trailing nul.  If there is no extra room
 available in str, does not change the string. 

 Also like mystrlcat, returns the final length that str would have
 had without truncation.  Ie, if return is >= n, truncation occured.
**********************************************************************/ 
int cat_snprintf(char *str, size_t n, const char *format, ...)
{
  size_t len;
  int ret;
  va_list ap;

  assert(format);
  assert(str);
  assert(n>0);
  
  len = strlen(str);
  assert(len < n);
  
  va_start(ap, format);
  ret = my_vsnprintf(str+len, n-len, format, ap);
  va_end(ap);
  return (int) (ret + len);
}

/***************************************************************************
  Returns string which gives users home dir, as specified by $HOME.
  Gets value once, and then caches result.
  If $HOME is not set, give a log message and returns NULL.
  Note the caller should not mess with the returned string.
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
  Returns string which gives user's username, as specified by $USER or
  as given in password file for this user's uid, or a made up name if
  we can't get either of the above. 
  Gets value once, and then caches result.
  Note the caller should not mess with returned string.
***************************************************************************/
char *user_username(void)
{
  static char *username = NULL;	  /* allocated once, never free()d */

  if (username)
    return username;

  {
    char *env = getenv("USER");
    if (env) {
      username = mystrdup(env);
      freelog(LOG_VERBOSE, "USER username is %s", username);
      return username;
    }
  }
#ifdef HAVE_GETPWUID
  {
    struct passwd *pwent = getpwuid(getuid());
    if (pwent) {
      username = mystrdup(pwent->pw_name);
      freelog(LOG_VERBOSE, "getpwuid username is %s", username);
      return username;
    }
  }
#endif
  username = fc_malloc(MAX_LEN_NAME);
  my_snprintf(username, MAX_LEN_NAME, "name%d", (int)getuid());
  freelog(LOG_VERBOSE, "fake username is %s", username);
  return username;
}

/***************************************************************************
  Returns a filename to access the specified file from a data directory;
  The file may be in any of the directories in the data path, which is
  specified internally or may be set as the environment variable
  $FREECIV_PATH.  (A separated list of directories, where the separator
  itself can specified internally.)
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
char *datafilename(const char *filename)
{
  static int init = 0;
  static int num_dirs = 0;
  static char **dirs = NULL;
  static struct astring realfile = ASTRING_INIT;
  int i;

  if (!init) {
    char *tok;
    char *path = getenv("FREECIV_PATH");
    if (!path) {
      path = DEFAULT_DATA_PATH;
    }
    path = mystrdup(path);	/* something we can strtok */
    
    tok = strtok(path, PATH_SEPARATOR);
    do {
      int i;			/* strlen(tok), or -1 as flag */
      
      tok = skip_leading_spaces(tok);
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
	    int len = strlen(home) + i;	   /* +1 -1 */
	    char *tmp = fc_malloc(len);
	    my_snprintf(tmp, len, "%s%s", home, tok+1);
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

      tok = strtok(NULL, PATH_SEPARATOR);
    } while(tok != NULL);

    free(path);
    init = 1;
  }

  if (filename == NULL) {
    int len = 1;		/* in case num_dirs==0 */
    int seplen = strlen(PATH_SEPARATOR);
    for(i=0; i<num_dirs; i++) {
      len += strlen(dirs[i]) + MAX(1,seplen);    /* separator or '\0' */
    }
    astr_minsize(&realfile, len);
    realfile.str[0] = '\0';
    for(i=0; i<num_dirs; i++) {
      mystrlcat(realfile.str, dirs[i], len);
      if(i != num_dirs-1) {
	mystrlcat(realfile.str, PATH_SEPARATOR, len);
      }
    }
    return realfile.str;
  }
  
  for(i=0; i<num_dirs; i++) {
    FILE *fp;			/* see if we can open the file */
    int len = strlen(dirs[i]) + strlen(filename) + 2;
    
    astr_minsize(&realfile, len);
    my_snprintf(realfile.str, len, "%s/%s", dirs[i], filename);
    fp = fopen(realfile.str, "r");
    if (fp) {
      fclose(fp);
      return realfile.str;
    }
  }
  return NULL;
}

/***************************************************************************
  As datafilename(), above, except die with an appropriate log
  message if we can't find the file in the datapath.
***************************************************************************/
char *datafilename_required(const char *filename)
{
  char *dname;
  
  assert(filename!=NULL);
  dname = datafilename(filename);

  if (dname) {
    return dname;
  } else {
    freelog(LOG_FATAL, _("Could not find readable file \"%s\" in data path."),
	    filename);
    freelog(LOG_FATAL, _("The data path may be set via"
			 " the environment variable FREECIV_PATH."));
    freelog(LOG_FATAL, _("Current data path is: \"%s\""), datafilename(NULL));
    exit(1);
  }
}


/***************************************************************************
  Setup for Native Language Support, if configured to use it.
***************************************************************************/
void init_nls(void)
{
#ifdef ENABLE_NLS
  setlocale (LC_ALL, "");
  bindtextdomain (PACKAGE, LOCALEDIR);
  textdomain(PACKAGE);
#endif
}


/***************************************************************************
  If we have root privileges, die with an error.
  (Eg, for security reasons.)
  Param argv0 should be argv[0] or similar; fallback is
  used instead if argv0 is NULL.
  But don't die on systems where the user is always root...
  (a general test for this would be better).
  Doesn't use freelog() because gets called before logging is setup.
***************************************************************************/
void dont_run_as_root(const char *argv0, const char *fallback)
{
#if (defined(ALWAYS_ROOT) || defined(__EMX__) || defined(__BEOS__))
  return;
#else
  if (getuid()==0 || geteuid()==0) {
    fprintf(stderr,
	    _("%s: Fatal error: you're trying to run me as superuser!\n"),
	    (argv0 ? argv0 : fallback ? fallback : "freeciv"));
    fprintf(stderr, _("Use a non-privileged account instead.\n"));
    exit(1);
  }
#endif
}


/***************************************************************************
  Return a description string of the result.
  In English, form of description is suitable to substitute in, eg:
     prefix is <description>
  (N.B.: The description is always in English, but they have all been marked
   for translation.  If you want a localized version, use _() on the return.)
***************************************************************************/
const char *m_pre_description(enum m_pre_result result)
{
  static const char * const descriptions[] = {
    N_("exact match"),
    N_("only match"),
    N_("ambiguous"),
    N_("empty"),
    N_("too long"),
    N_("non-match")
  };
  assert(result >= 0 && result < sizeof(descriptions)/sizeof(descriptions[0]));
  return descriptions[result];
}

/***************************************************************************
  Given n names, with maximum length max_len_name, accessed by
  accessor_fn(0) to accessor_fn(n-1), look for matching prefix
  according to given comparison function.
  Returns type of match or fail, and for return <= M_PRE_AMBIGUOUS
  sets *ind_result with matching index (or for ambiguous, first match).
  If max_len_name==0, treat as no maximum.
***************************************************************************/
enum m_pre_result match_prefix(m_pre_accessor_fn_t accessor_fn,
			       size_t n_names,
			       size_t max_len_name,
			       m_pre_strncmp_fn_t cmp_fn,
			       const char *prefix,
			       int *ind_result)
{
  int i, len, nmatches;

  len = strlen(prefix);
  if (len == 0) {
    return M_PRE_EMPTY;
  }
  if (len > max_len_name && max_len_name > 0) {
    return M_PRE_LONG;
  }

  nmatches = 0;
  for(i=0; i<n_names; i++) {
    const char *name = accessor_fn(i);
    if (cmp_fn(name, prefix, len)==0) {
      if (strlen(name) == len) {
	*ind_result = i;
	return M_PRE_EXACT;
      }
      if (nmatches==0) {
	*ind_result = i;	/* first match */
      }
      nmatches++;
    }
  }

  if (nmatches == 1) {
    return M_PRE_ONLY;
  } else if (nmatches > 1) {
    return M_PRE_AMBIGUOUS;
  } else {
    return M_PRE_FAIL;
  }
}

/***************************************************************************
  Return the Freeciv motto.
***************************************************************************/
char *freeciv_motto(void)
{
  return _("'Cause civilization should be free!");
}
