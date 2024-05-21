/***********************************************************************
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
#include <fc_config.h>
#endif

#include "fc_prehdrs.h"

#ifdef FREECIV_HAVE_SYS_TYPES_H
/* Under Mac OS X sys/types.h must be included before dirent.h */
#include <sys/types.h>
#endif

#ifdef FREECIV_HAVE_DIRENT_H
#include <dirent.h>
#endif

#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef HAVE_LOCALE_H
#include <locale.h>
#endif

#ifdef HAVE_PWD_H
#include <pwd.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef FREECIV_MSWINDOWS
#include <windows.h>
#include <lmcons.h>     /* UNLEN */
#include <shlobj.h>
#ifdef HAVE_DIRECT_H
#include <direct.h>
#endif /* HAVE_DIRECT_H */
#endif /* FREECIV_MSWINDOWS */

/* utility */
#include "astring.h"
#include "fc_dirent.h"
#include "fciconv.h"
#include "fcintl.h"
#include "mem.h"
#include "rand.h"
#include "string_vector.h"

#include "shared.h"

/* If no default data path is defined use the default default one */
#ifndef DEFAULT_DATA_PATH
#define DEFAULT_DATA_PATH "." PATH_SEPARATOR \
                          "data" PATH_SEPARATOR \
                          FREECIV_STORAGE_DIR DIR_SEPARATOR DATASUBDIR
#endif
#ifndef DEFAULT_SAVE_PATH
#define DEFAULT_SAVE_PATH "." PATH_SEPARATOR \
                          FREECIV_STORAGE_DIR DIR_SEPARATOR "saves"
#endif
#ifndef DEFAULT_SCENARIO_PATH
#define DEFAULT_SCENARIO_PATH                          \
  "." PATH_SEPARATOR                                   \
  "data" DIR_SEPARATOR "scenarios" PATH_SEPARATOR                      \
  FREECIV_STORAGE_DIR DATASUBDIR DIR_SEPARATOR "scenarios" PATH_SEPARATOR \
  FREECIV_STORAGE_DIR DIR_SEPARATOR "scenarios"
#endif /* DEFAULT_SCENARIO_PATH */

/* Environment */
#ifndef FREECIV_DATA_PATH
#define FREECIV_DATA_PATH "FREECIV_DATA_PATH"
#endif
#ifndef FREECIV_SAVE_PATH
#define FREECIV_SAVE_PATH "FREECIV_SAVE_PATH"
#endif
#ifndef FREECIV_SCENARIO_PATH
#define FREECIV_SCENARIO_PATH "FREECIV_SCENARIO_PATH"
#endif

/* Both of these are stored in the local encoding. The grouping_sep must
 * be converted to the internal encoding when it's used. */
static char *grouping = nullptr;
static char *grouping_sep = nullptr;

/* As well as base64 functions, this string is used for checking for
 * 'safe' filenames, so should not contain / \ . */
static const char base64url[] =
  "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

static struct strvec *data_dir_names = nullptr;
static struct strvec *save_dir_names = nullptr;
static struct strvec *scenario_dir_names = nullptr;

static char *mc_group = nullptr;
static char *home_dir_user = nullptr;
static char *storage_dir_freeciv = nullptr;

static struct astring realfile = ASTRING_INIT;

static void remove_trailing_char(char *s, char trailing)
  fc__attribute((nonnull (1)));

static int compare_file_mtime_ptrs(const struct fileinfo *const *ppa,
                                   const struct fileinfo *const *ppb);

static char *expand_dir(char *tok_in, bool ok_to_free);

/************************************************************************//**
  An AND function for fc_tristate.
****************************************************************************/
enum fc_tristate fc_tristate_and(enum fc_tristate one, enum fc_tristate two)
{
  if (TRI_NO == one || TRI_NO == two) {
    return TRI_NO;
  }

  if (TRI_MAYBE == one || TRI_MAYBE == two) {
    return TRI_MAYBE;
  }

  return TRI_YES;
}

/************************************************************************//**
  An OR function for fc_tristate.
****************************************************************************/
enum fc_tristate fc_tristate_or(enum fc_tristate one, enum fc_tristate two)
{
  if (TRI_YES == one || TRI_YES == two) {
    return TRI_YES;
  }

  if (TRI_MAYBE == one || TRI_MAYBE == two) {
    return TRI_MAYBE;
  }

  return TRI_NO;
}

/************************************************************************//**
  Returns a statically allocated string containing a nicely-formatted
  version of the given number according to the user's locale. (Only
  works for numbers >= zero.) The number is given in scientific notation
  as mantissa * 10^exponent.
****************************************************************************/
const char *big_int_to_text(unsigned int mantissa, unsigned int exponent)
{
  static char buf[64]; /* Note that we'll be filling this in right to left. */
  char *grp = grouping;
  char *ptr;
  unsigned int cnt = 0;
  char sep[64];
  size_t seplen;

  /* We have to convert the encoding here (rather than when the locale
   * is initialized) because it can't be done before the charsets are
   * initialized. */
  local_to_internal_string_buffer(grouping_sep, sep, sizeof(sep));
  seplen = strlen(sep);

#if 0 /* Not needed while the values are unsigned. */
  fc_assert_ret_val(0 <= mantissa, nullptr);
  fc_assert_ret_val(0 <= exponent, nullptr);
#endif

  if (mantissa == 0) {
    return "0";
  }

  /* We fill the string in backwards, starting from the right. So the first
   * thing we do is terminate it. */
  ptr = &buf[sizeof(buf)];
  *(--ptr) = '\0';

  while (mantissa != 0) {
    int dig;

    if (ptr <= buf + seplen) {
      /* Avoid a buffer overflow. */
      fc_assert_ret_val(ptr > buf + seplen, nullptr);
      return ptr;
    }

    /* Add on another character. */
    if (exponent > 0) {
      dig = 0;
      exponent--;
    } else {
      dig = mantissa % 10;
      mantissa /= 10;
    }
    *(--ptr) = '0' + dig;

    cnt++;
    if (mantissa != 0 && cnt == *grp) {
      /* Reached count of digits in group: insert separator and reset count. */
      cnt = 0;
      if (*grp == CHAR_MAX) {
        /* This test is unlikely to be necessary since we would need at
           least 421-bit ints to break the 127 digit barrier, but why not. */
        break;
      }
      ptr -= seplen;

      fc_assert_ret_val(ptr >= buf, nullptr);

      memcpy(ptr, sep, seplen);
      if (*(grp + 1) != 0) {
        /* Zero means to repeat the present group-size indefinitely. */
        grp++;
      }
    }
  }

  return ptr;
}

/************************************************************************//**
  Return a prettily formatted string containing the given number.
****************************************************************************/
const char *int_to_text(unsigned int number)
{
  return big_int_to_text(number, 0);
}

/************************************************************************//**
  Check whether or not the given char is a valid ascii character.
  The character can be in any charset so long as it is a superset of ascii.
****************************************************************************/
static bool is_ascii(char ch)
{
  /* This works with both signed and unsigned char's. */
  return ch >= ' ' && ch <= '~';
}

/************************************************************************//**
  Check if the name is safe security-wise. This is intended to be used to
  make sure an untrusted filename is safe to be used.
****************************************************************************/
bool is_safe_filename(const char *name)
{
  int i = 0;

  /* Must not be nullptr or empty */
  if (name == nullptr || *name == '\0') {
    return FALSE;
  }

  for (; '\0' != name[i]; i++) {
    if ('.' != name[i] && strchr(base64url, name[i]) == nullptr) {
      return FALSE;
    }
  }

  /* We don't allow the filename to ascend directories */
  if (strstr(name, PARENT_DIR_OPERATOR)) {
    return FALSE;
  }

  /* Otherwise, it is okay... */
  return TRUE;
}

/************************************************************************//**
  This is used in sundry places to make sure that names of cities,
  players etc. do not contain yucky characters of various sorts.
  Returns TRUE iff the name is acceptable.
  FIXME:  Not internationalised.
****************************************************************************/
bool is_ascii_name(const char *name)
{
  const char illegal_chars[] = {'|', '%', '"', ',', '*', '<', '>', '\0'};
  int i, j;

  /* Must not be nullptr or empty */
  if (name == nullptr || *name == '\0') {
    return FALSE;
  }

  /* Must begin and end with some non-space character */
  if ((*name == ' ') || (*(strchr(name, '\0') - 1) == ' ')) {
    return FALSE;
  }

  /* Must be composed entirely of printable ascii characters,
   * and no illegal characters which can break ranking scripts. */
  for (i = 0; name[i]; i++) {
    if (!is_ascii(name[i])) {
      return FALSE;
    }
    for (j = 0; illegal_chars[j]; j++) {
      if (name[i] == illegal_chars[j]) {
        return FALSE;
      }
    }
  }

  /* Otherwise, it's okay... */
  return TRUE;
}

/************************************************************************//**
  Check for valid base64url.
****************************************************************************/
bool is_base64url(const char *s)
{
  size_t i = 0;

  /* Must not be nullptr or empty */
  if (s == nullptr || '\0' == *s) {
    return FALSE;
  }

  for (; '\0' != s[i]; i++) {
    if (strchr(base64url, s[i]) == nullptr) {
      return FALSE;
    }
  }

  return TRUE;
}

/************************************************************************//**
  Generate a random string meeting criteria such as is_ascii_name(),
  is_base64url(), and is_safe_filename().
****************************************************************************/
void randomize_base64url_string(char *s, size_t n)
{
  size_t i = 0;

  /* Must not be nullptr or too short */
  if (s == nullptr || 1 > n) {
    return;
  }

  for (; i < (n - 1); i++) {
    s[i] = base64url[fc_rand(sizeof(base64url) - 1)];
  }
  s[i] = '\0';
}

/************************************************************************//**
  Compares two strings, in the collating order of the current locale,
  given pointers to the two strings (i.e., given "char *"s).
  Case-sensitive. Designed to be called from qsort().
****************************************************************************/
int compare_strings(const void *first, const void *second)
{
  return fc_strcoll((const char *) first, (const char *) second);
}

/************************************************************************//**
  Compares two strings, in the collating order of the current locale,
  given pointers to the two string pointers (i.e., given "char **"s).
  Case-sensitive. Designed to be called from qsort().
****************************************************************************/
int compare_strings_ptrs(const void *first, const void *second)
{
  return fc_strcoll(*((const char **) first), *((const char **) second));
}

/************************************************************************//**
  Compares two strings, in the collating order of the current locale,
  given pointers to the two string pointers. Case-sensitive.
  Designed to be called from strvec_sort().
****************************************************************************/
int compare_strings_strvec(const char *const *first,
                           const char *const *second)
{
  return fc_strcoll(*first, *second);
}

/************************************************************************//**
  Returns 's' incremented to first non-space character.
****************************************************************************/
char *skip_leading_spaces(char *s)
{
  while (*s != '\0' && fc_isspace(*s)) {
    s++;
  }

  return s;
}

/************************************************************************//**
  Removes leading spaces in string pointed to by 's'.
  Note 's' must point to writeable memory!
****************************************************************************/
void remove_leading_spaces(char *s)
{
  char *t;

  t = skip_leading_spaces(s);
  if (t != s) {
    while (*t != '\0') {
      *s++ = *t++;
    }
    *s = '\0';
  }
}

/************************************************************************//**
  Terminates string pointed to by 's' to remove trailing spaces;
  Note 's' must point to writeable memory!
****************************************************************************/
void remove_trailing_spaces(char *s)
{
  char *t;
  size_t len;

  len = strlen(s);
  if (len > 0) {
    t = s + len -1;
    while (fc_isspace(*t)) {
      *t = '\0';
      if (t == s) {
        break;
      }
      t--;
    }
  }
}

/************************************************************************//**
  Removes leading and trailing spaces in string pointed to by 's'.
  Note 's' must point to writeable memory!
****************************************************************************/
void remove_leading_trailing_spaces(char *s)
{
  remove_leading_spaces(s);
  remove_trailing_spaces(s);
}

/************************************************************************//**
  As remove_trailing_spaces(), for specified char.
****************************************************************************/
static void remove_trailing_char(char *s, char trailing)
{
  char *t;

  t = s + strlen(s) -1;
  while (t>=s && (*t) == trailing) {
    *t = '\0';
    t--;
  }
}

/************************************************************************//**
  Returns pointer to '\0' at end of string 'str', and decrements
  *nleft by the length of 'str'. This is intended to be useful to
  allow strcat-ing without traversing the whole string each time,
  while still keeping track of the buffer length.
  Eg:
     char buf[128];
     int n = sizeof(buf);
     char *p = buf;

     fc_snprintf(p, n, "foo%p", p);
     p = end_of_strn(p, &n);
     fc_strlcpy(p, "yyy", n);
****************************************************************************/
char *end_of_strn(char *str, int *nleft)
{
  int len = strlen(str);

  *nleft -= len;
  fc_assert_ret_val(0 < (*nleft), nullptr); /* Space for the terminating nul */

  return str + len;
}

/************************************************************************//**
  Check the length of the given string. If the string is too long,
  log errmsg, which should be a string in printf-format taking up to
  two arguments: the string and the length.
****************************************************************************/
bool check_strlen(const char *str, size_t len, const char *errmsg)
{
  fc_assert_ret_val_msg(strlen(str) < len, TRUE, errmsg, str, len);

  return FALSE;
}

/************************************************************************//**
  Call check_strlen() on str and then strlcpy() it into buffer.
****************************************************************************/
size_t loud_strlcpy(char *buffer, const char *str, size_t len,
                    const char *errmsg)
{
  (void) check_strlen(str, len, errmsg);

  return fc_strlcpy(buffer, str, len);
}

/************************************************************************//**
  Convert 'str' to its int representation if possible. 'pint' can be nullptr,
  then it will only test 'str' only contains an integer number.
****************************************************************************/
bool str_to_int(const char *str, int *pint)
{
  const char *start;

  while (fc_isspace(*str)) {
    /* Skip leading spaces. */
    str++;
  }

  start = str;
  if ('-' == *str || '+' == *str) {
    /* Handle sign. */
    str++;
  }
  while (fc_isdigit(*str)) {
    /* Digits. */
    str++;
  }

  while (fc_isspace(*str)) {
    /* Ignore trailing spaces. */
    str++;
  }

  return ('\0' == *str && (pint == nullptr || 1 == sscanf(start, "%d", pint)));
}

/************************************************************************//**
  Convert 'str' to its unsigned int representation if possible.
  'pint' can be nullptr, then it will only test 'str' only contains
  an unsigned integer number.
****************************************************************************/
bool str_to_uint(const char *str, unsigned int *pint)
{
  const char *start;

  while (fc_isspace(*str)) {
    /* Skip leading spaces. */
    str++;
  }

  start = str;
  if ('+' == *str) {
    /* Handle sign. */
    str++;
  }
  while (fc_isdigit(*str)) {
    /* Digits. */
    str++;
  }

  while (fc_isspace(*str)) {
    /* Ignore trailing spaces. */
    str++;
  }

  return ('\0' == *str && (pint == nullptr || 1 == sscanf(start, "%u", pint)));
}

/************************************************************************//**
  Convert 'str' to its float representation if possible. 'pfloat'
  can be nullptr, then it will only test 'str' only contains
  a floating point number.
****************************************************************************/
bool str_to_float(const char *str, float *pfloat)
{
  bool dot;
  const char *start;

  while (fc_isspace(*str)) {
    /* Skip leading spaces. */
    str++;
  }

  start = str;

  if ('-' == *str || '+' == *str) {
    /* Handle sign. */
    str++;
  }
  while (fc_isdigit(*str)) {
    /* Digits. */
    str++;
  }

  if (*str == '.') {
    dot = TRUE;
    str++;

    while (fc_isdigit(*str)) {
      /* Digits. */
      str++;
    }
  } else {
    dot = FALSE;
  }

  while (fc_isspace(*str)) {
    /* Ignore trailing spaces. */
    str++;
  }

  return ('\0' == *str && dot
          && (nullptr == pfloat || 1 == sscanf(start, "%f", pfloat)));
}

/************************************************************************//**
  Returns string which gives users home dir, as specified by $HOME.
  Gets value once, and then caches result.
  If $HOME is not set, give a log message and returns nullptr.
  Note the caller should not mess with the returned string.
****************************************************************************/
char *user_home_dir(void)
{
#ifdef AMIGA
  return "PROGDIR:";
#else  /* AMIGA */

#ifdef FREECIV_MSWINDOWS
#define HOMEVAR "APPDATA"
#else
#define HOMEVAR "HOME"
#endif

  if (home_dir_user == nullptr) {
    char *env = getenv(HOMEVAR);

    if (env) {
      home_dir_user = fc_strdup(env);
      log_verbose(HOMEVAR " is %s", home_dir_user);
    } else {
      log_error("Could not find home directory (" HOMEVAR " is not set).");
      home_dir_user = nullptr;
    }
  }

  return home_dir_user;
#endif /* AMIGA */
}

/************************************************************************//**
  Free user home directory information
****************************************************************************/
void free_user_home_dir(void)
{
  if (home_dir_user != nullptr) {
    free(home_dir_user);
    home_dir_user = nullptr;
  }
}

/************************************************************************//**
  Returns string which gives freeciv storage dir.
  Gets value once, and then caches result.
  Note the caller should not mess with the returned string.
****************************************************************************/
char *freeciv_storage_dir(void)
{
  if (storage_dir_freeciv == nullptr) {
    storage_dir_freeciv = fc_malloc(strlen(FREECIV_STORAGE_DIR) + 1);

    strcpy(storage_dir_freeciv, FREECIV_STORAGE_DIR);

    storage_dir_freeciv = expand_dir(storage_dir_freeciv, TRUE);

    log_verbose(_("Storage dir is \"%s\"."), storage_dir_freeciv);
  }

  return storage_dir_freeciv;
}

/************************************************************************//**
  Free freeciv storage directory information
****************************************************************************/
void free_freeciv_storage_dir(void)
{
  if (storage_dir_freeciv != nullptr) {
    free(storage_dir_freeciv);
    storage_dir_freeciv = nullptr;
  }
}

/************************************************************************//**
  Returns string which gives user's username, as specified by $USER or
  as given in password file for this user's uid, or a made up name if
  we can't get either of the above.
  Gets value once, and then caches result.
  Note the caller should not mess with returned string.
****************************************************************************/
char *user_username(char *buf, size_t bufsz)
{
  /* This function uses a number of different methods to try to find a
   * username. This username then has to be truncated to bufsz
   * characters (including terminator) and checked for sanity. Note that
   * truncating a sane name can leave you with an insane name under some
   * charsets. */

  /* If the environment variable $USER is present and sane, use it. */
  {
    char *env = getenv("USER");

    if (env) {
      fc_strlcpy(buf, env, bufsz);
      if (is_ascii_name(buf)) {
        log_verbose("USER username is %s", buf);
        return buf;
      }
    }
  }

#ifdef HAVE_GETPWUID
  /* Otherwise if getpwuid() is available we can use it to find the true
   * username. */
  {
    struct passwd *pwent = getpwuid(getuid());

    if (pwent) {
      fc_strlcpy(buf, pwent->pw_name, bufsz);
      if (is_ascii_name(buf)) {
        log_verbose("getpwuid username is %s", buf);
        return buf;
      }
    }
  }
#endif /* HAVE_GETPWUID */

#ifdef FREECIV_MSWINDOWS
  /* On windows the GetUserName function will give us the login name. */
  {
    char name[UNLEN + 1];
    DWORD length = sizeof(name);

    if (GetUserName(name, &length)) {
      fc_strlcpy(buf, name, bufsz);
      if (is_ascii_name(buf)) {
        log_verbose("GetUserName username is %s", buf);
        return buf;
      }
    }
  }
#endif /* FREECIV_MSWINDOWS */

#ifdef ALWAYS_ROOT
  fc_strlcpy(buf, "name", bufsz);
#else
  fc_snprintf(buf, bufsz, "name%d", (int) getuid());
#endif
  log_verbose("fake username is %s", buf);
  fc_assert(is_ascii_name(buf));

  return buf;
}

/************************************************************************//**
  Return tok_in directory name with "~/" expanded as user home directory.
  The function might return tok_in, or a new string. In either case caller
  should free() the returned string eventually. Also, tok_in should be
  something expand_dir() can free itself if it decides to return newly
  created string (so caller can always free() just the returned string, not
  to care if it's same as tok_in or not). If ok_to_free is FALSE,
  expand_dir() never frees original but can still return either it or a
  newly allocated string.
****************************************************************************/
static char *expand_dir(char *tok_in, bool ok_to_free)
{
  int i; /* strlen(tok), or -1 as flag */
  char *tok;
  char **ret = &tok; /* Return tok by default */
  char *allocated;

  tok = skip_leading_spaces(tok_in);
  remove_trailing_spaces(tok);
  if (strcmp(tok, DIR_SEPARATOR) != 0) {
    remove_trailing_char(tok, DIR_SEPARATOR_CHAR);
  }

  i = strlen(tok);
  if (tok[0] == '~') {
    if (i > 1 && tok[1] != DIR_SEPARATOR_CHAR) {
      log_error("For \"%s\" in path cannot expand '~'"
                " except as '~" DIR_SEPARATOR "'; ignoring", tok);
      i = 0;  /* Skip this one */
    } else {
      char *home = user_home_dir();

      if (!home) {
        log_verbose("No HOME, skipping path component %s", tok);
        i = 0;
      } else {
        int len = strlen(home) + i;   /* +1 -1 */

        allocated = fc_malloc(len);
        ret = &allocated;

        fc_snprintf(allocated, len, "%s%s", home, tok + 1);
        i = -1;  /* Flag to free tok below */
      }
    }
  }

  if (i != 0) {
    /* We could check whether the directory exists and
     * is readable etc? Don't currently. */
    if (i == -1 && ok_to_free) {
      free(tok);
      tok = nullptr;
    }
  }

  return *ret;
}

/************************************************************************//**
  Returns a list of directory paths, in the order in which they should
  be searched. Base function for get_data_dirs(), get_save_dirs(),
  get_scenario_dirs()
****************************************************************************/
static struct strvec *base_get_dirs(const char *dir_list)
{
  struct strvec *dirs = strvec_new();
  char *path, *tok;

  path = fc_strdup(dir_list);   /* Something we can strtok() */
  tok = strtok(path, PATH_SEPARATOR);
  do {
    char *dir = expand_dir(tok, FALSE);

    if (dir != nullptr) {
      strvec_append(dirs, dir);
      if (dir != tok) {
        free(dir);
      }
    }

    tok = strtok(nullptr, PATH_SEPARATOR);
  } while (tok);

  free(path);
  return dirs;
}

/************************************************************************//**
  Free data dir name vectors.
****************************************************************************/
void free_data_dir_names(void)
{
  if (data_dir_names != nullptr) {
    strvec_destroy(data_dir_names);
    data_dir_names = nullptr;
  }
  if (save_dir_names != nullptr) {
    strvec_destroy(save_dir_names);
    save_dir_names = nullptr;
  }
  if (scenario_dir_names != nullptr) {
    strvec_destroy(scenario_dir_names);
    scenario_dir_names = nullptr;
  }
}

/************************************************************************//**
  Returns a list of data directory paths, in the order in which they should
  be searched. These paths are specified internally or may be set as the
  environment variable $FREECIV_DATA PATH (a separated list of directories,
  where the separator itself is specified internally, platform-dependent).
  '~' at the start of a component (provided followed by '/' or '\0') is
  expanded as $HOME.

  The returned pointer is static and shouldn't be modified, nor destroyed
  by the user caller.
****************************************************************************/
const struct strvec *get_data_dirs(void)
{
  /* The first time this function is called it will search and
   * allocate the directory listing. Subsequently we will already
   * know the list and can just return it. */
  if (data_dir_names == nullptr) {
    const char *path;

#ifdef FREECIV_APPIMAGE
    char default_data_path[5000];

    fc_snprintf(default_data_path, sizeof(default_data_path),
                FREECIV_STORAGE_DIR DIR_SEPARATOR DATASUBDIR
                PATH_SEPARATOR
                "%s/usr/share/freeciv",
                getenv("APPDIR"));
#else  /* FREECIV_APPIMAGE */
#define default_data_path DEFAULT_DATA_PATH
#endif /* FREECIV_APPIMAGE */

    if ((path = getenv(FREECIV_DATA_PATH)) && '\0' == path[0]) {
      /* TRANS: <FREECIV_DATA_PATH> configuration error */
      log_error(_("\"%s\" is set but empty; using default \"%s\" "
                 "data directories instead."),
                FREECIV_DATA_PATH, default_data_path);
      path = nullptr;
    }
    data_dir_names = base_get_dirs(path != nullptr ? path : default_data_path);
    strvec_remove_duplicate(data_dir_names, strcmp); /* Don't set a path both. */
    strvec_iterate(data_dir_names, dirname) {
      log_verbose("Data path component: %s", dirname);
    } strvec_iterate_end;
  }

  return data_dir_names;
}

/************************************************************************//**
  Returns a list of save directory paths, in the order in which they should
  be searched. These paths are specified internally or may be set as the
  environment variable $FREECIV_SAVE_PATH (a separated list of directories,
  where the separator itself is specified internally, platform-dependent).
  '~' at the start of a component (provided followed by '/' or '\0') is
  expanded as $HOME.

  The returned pointer is static and shouldn't be modified, nor destroyed
  by the user caller.
****************************************************************************/
const struct strvec *get_save_dirs(void)
{
  /* The first time this function is called it will search and
   * allocate the directory listing. Subsequently we will already
   * know the list and can just return it. */
  if (save_dir_names == nullptr) {
    const char *path;

    if ((path = getenv(FREECIV_SAVE_PATH)) && '\0' == path[0]) {
      /* TRANS: <FREECIV_SAVE_PATH> configuration error */
      log_error(_("\"%s\" is set but empty; using default \"%s\" "
                  "save directories instead."),
                FREECIV_SAVE_PATH, DEFAULT_SAVE_PATH);
      path = nullptr;
    }
    save_dir_names = base_get_dirs(path != nullptr ? path : DEFAULT_SAVE_PATH);
    strvec_remove_duplicate(save_dir_names, strcmp); /* Don't set a path both. */
    strvec_iterate(save_dir_names, dirname) {
      log_verbose("Save path component: %s", dirname);
    } strvec_iterate_end;
  }

  return save_dir_names;
}

/************************************************************************//**
  Returns a list of scenario directory paths, in the order in which they
  should be searched. These paths are specified internally or may be set
  as the environment variable $FREECIV_SCENARIO_PATH (a separated list of
  directories, where the separator itself is specified internally,
  platform-dependent).
  '~' at the start of a component (provided followed
  by '/' or '\0') is expanded as $HOME.

  The returned pointer is static and shouldn't be modified, nor destroyed
  by the user caller.
****************************************************************************/
const struct strvec *get_scenario_dirs(void)
{
  /* The first time this function is called it will search and
   * allocate the directory listing. Subsequently we will already
   * know the list and can just return it. */
  if (scenario_dir_names == nullptr) {
    const char *path;

#ifdef FREECIV_APPIMAGE
    char default_scenario_path[5000];

    fc_snprintf(default_scenario_path, sizeof(default_scenario_path),
                FREECIV_STORAGE_DIR DIR_SEPARATOR DATASUBDIR DIR_SEPARATOR "scenarios"
                PATH_SEPARATOR
                FREECIV_STORAGE_DIR DIR_SEPARATOR "scenarios"
                PATH_SEPARATOR
                "%s/usr/share/freeciv/scenarios",
                getenv("APPDIR"));
#else  /* FREECIV_APPIMAGE */
#define default_scenario_path DEFAULT_SCENARIO_PATH
#endif /* FREECIV_APPIMAGE */

    if ((path = getenv(FREECIV_SCENARIO_PATH)) && '\0' == path[0]) {
      /* TRANS: <FREECIV_SCENARIO_PATH> configuration error */
      log_error( _("\"%s\" is set but empty; using default \"%s\" "
                   "scenario directories instead."),
                 FREECIV_SCENARIO_PATH, default_scenario_path);
      path = nullptr;
    }
    scenario_dir_names = base_get_dirs(path != nullptr ? path : default_scenario_path);
    strvec_remove_duplicate(scenario_dir_names, strcmp); /* Don't set a path both. */
    strvec_iterate(scenario_dir_names, dirname) {
      log_verbose("Scenario path component: %s", dirname);
    } strvec_iterate_end;
  }

  return scenario_dir_names;
}

/************************************************************************//**
  Returns a string vector storing the filenames in the data directories
  matching the given suffix.

  The list is allocated when the function is called; it should either
  be stored permanently or destroyed (with strvec_destroy()).

  The suffixes are removed from the filenames before the list is
  returned.
****************************************************************************/
struct strvec *fileinfolist(const struct strvec *dirs, const char *suffix)
{
  struct strvec *files = strvec_new();
  size_t suffix_len = strlen(suffix);

  fc_assert_ret_val(!strchr(suffix, DIR_SEPARATOR_CHAR), nullptr);

  if (dirs == nullptr) {
    return files;
  }

  /* First assemble a full list of names. */
  strvec_iterate(dirs, dirname) {
    DIR *dir;
    struct dirent *entry;

    /* Open the directory for reading. */
    dir = fc_opendir(dirname);
    if (!dir) {
      if (errno == ENOENT) {
        log_verbose("Skipping non-existing data directory %s.",
                    dirname);
      } else {
        /* TRANS: "...: <externally translated error string>."*/
        log_error(_("Could not read data directory %s: %s."),
                  dirname, fc_strerror(fc_get_errno()));
      }
      continue;
    }

    /* Scan all entries in the directory. */
    while ((entry = readdir(dir))) {
      size_t len = strlen(entry->d_name);

      /* Make sure the file name matches. */
      if (len > suffix_len
          && strcmp(suffix, entry->d_name + len - suffix_len) == 0) {
        /* Strdup the entry so we can safely write to it. */
        char *match = fc_strdup(entry->d_name);

        /* Clip the suffix. */
        match[len - suffix_len] = '\0';

        strvec_append(files, match);
        free(match);
      }
    }

    closedir(dir);
  } strvec_iterate_end;

  /* Sort the list and remove duplications. */
  strvec_remove_duplicate(files, strcmp);
  strvec_sort(files, compare_strings_strvec);

  return files;
}

/************************************************************************//**
  Returns a filename to access the specified file from a
  directory by searching all specified directories for the file.

  If the specified 'filename' is nullptr, the returned string contains
  the effective path. (But this should probably only be used for
  debug output.)

  Returns nullptr if the specified filename cannot be found in any of the
  data directories. (A file is considered "found" if it can be
  read-opened.) The returned pointer points to static memory, so this
  function can only supply one filename at a time. Don't free that
  pointer.

  TODO: Make this re-entrant
****************************************************************************/
const char *fileinfoname(const struct strvec *dirs, const char *filename)
{
#ifndef DIR_SEPARATOR_IS_DEFAULT
  char fnbuf[filename != nullptr ? strlen(filename) + 1 : 1];
  int i;
#else  /* DIR_SEPARATOR_IS_DEFAULT */
  const char *fnbuf = filename;
#endif /* DIR_SEPARATOR_IS_DEFAULT */

  if (dirs == nullptr) {
    return nullptr;
  }

  if (!filename) {
    bool first = TRUE;

    astr_clear(&realfile);
    strvec_iterate(dirs, dirname) {
      if (first) {
        astr_add(&realfile, "%s%s", PATH_SEPARATOR, dirname);
        first = FALSE;
      } else {
        astr_add(&realfile, "%s", dirname);
      }
    } strvec_iterate_end;

    return astr_str(&realfile);
  }

#ifndef DIR_SEPARATOR_IS_DEFAULT
  for (i = 0; filename[i] != '\0'; i++) {
    if (filename[i] == '/') {
      fnbuf[i] = DIR_SEPARATOR_CHAR;
    } else {
      fnbuf[i] = filename[i];
    }
  }
  fnbuf[i] = '\0';
#endif /* DIR_SEPARATOR_IS_DEFAULT */

  strvec_iterate(dirs, dirname) {
    struct stat buf;    /* See if we can open the file or directory */

    astr_set(&realfile, "%s" DIR_SEPARATOR "%s", dirname, fnbuf);
    if (fc_stat(astr_str(&realfile), &buf) == 0) {
      return astr_str(&realfile);
    }
  } strvec_iterate_end;

  log_verbose("Could not find readable file \"%s\" in data path.", filename);

  return nullptr;
}

/************************************************************************//**
  Free resources allocated for fileinfoname service
****************************************************************************/
void free_fileinfo_data(void)
{
  astr_free(&realfile);
}

/************************************************************************//**
  Destroys the file info structure.
****************************************************************************/
static void fileinfo_destroy(struct fileinfo *pfile)
{
  free(pfile->name);
  free(pfile->fullname);
  free(pfile);
}

/************************************************************************//**
  Compare modification times.
****************************************************************************/
static int compare_file_mtime_ptrs(const struct fileinfo *const *ppa,
                                   const struct fileinfo *const *ppb)
{
  time_t a = (*ppa)->mtime;
  time_t b = (*ppb)->mtime;

  return ((a < b) ? 1 : (a > b) ? -1 : 0);
}

/************************************************************************//**
  Compare names.
****************************************************************************/
static int compare_file_name_ptrs(const struct fileinfo *const *ppa,
                                  const struct fileinfo *const *ppb)
{
  return fc_strcoll((*ppa)->name, (*ppb)->name);
}

/************************************************************************//**
  Compare names.
****************************************************************************/
static bool compare_fileinfo_name(const struct fileinfo *pa,
                                  const struct fileinfo *pb)
{
  return 0 == fc_strcoll(pa->name, pb->name);
}

/************************************************************************//**
  Search for filenames with the "infix" substring in the "subpath"
  subdirectory of the data path.
  "nodups" removes duplicate names.
  The returned list will be sorted by name first and modification time
  second. Returned "name"s will be truncated starting at the "infix"
  substring. The returned list must be freed with fileinfo_list_destroy().
****************************************************************************/
struct fileinfo_list *fileinfolist_infix(const struct strvec *dirs,
                                         const char *infix, bool nodups)
{
  struct fileinfo_list *res;

  if (dirs == nullptr) {
    return nullptr;
  }

  res = fileinfo_list_new_full(fileinfo_destroy);

  /* First assemble a full list of names. */
  strvec_iterate(dirs, dirname) {
    DIR *dir;
    struct dirent *entry;

    /* Open the directory for reading. */
    dir = fc_opendir(dirname);
    if (!dir) {
      continue;
    }

    /* Scan all entries in the directory. */
    while ((entry = readdir(dir))) {
      struct fileinfo *file;
      char *ptr;
      /* Strdup the entry so we can safely write to it. */
      char *filename = fc_strdup(entry->d_name);

      /* Make sure the file name matches. */
      if ((ptr = strstr(filename, infix))) {
        struct stat buf;
        char *fullname;
        size_t len = strlen(dirname) + strlen(filename) + 2;

        fullname = fc_malloc(len);
        fc_snprintf(fullname, len, "%s" DIR_SEPARATOR "%s", dirname, filename);

        if (fc_stat(fullname, &buf) == 0) {
          file = fc_malloc(sizeof(*file));

          /* Clip the suffix. */
          *ptr = '\0';

          file->name = filename;
          file->fullname = fullname;
          file->mtime = buf.st_mtime;

          fileinfo_list_append(res, file);
        } else {
          free(fullname);
          free(filename);
        }
      } else {
        free(filename);
      }
    }

    closedir(dir);
  } strvec_iterate_end;

  /* Sort the list by name. */
  fileinfo_list_sort(res, compare_file_name_ptrs);

  if (nodups) {
    fileinfo_list_unique_full(res, compare_fileinfo_name);
  }

  /* Sort the list by last modification time. */
  fileinfo_list_sort(res, compare_file_mtime_ptrs);

  return res;
}

/************************************************************************//**
  Language environmental variable (with emulation).
****************************************************************************/
const char *setup_langname(void)
{
  const char *langname = nullptr;

#ifdef ENABLE_NLS
  langname = getenv("LANG");

#ifdef FREECIV_MSWINDOWS
  /* Set LANG by hand if it is not set */
  if (!langname) {
    switch (PRIMARYLANGID(GetUserDefaultLangID())) {
      case LANG_ARABIC:
        langname = "ar";
        break;
      case LANG_CATALAN:
        langname = "ca";
        break;
      case LANG_CZECH:
        langname = "cs";
        break;
      case LANG_DANISH:
        langname = "da";
        break;
      case LANG_GERMAN:
        langname = "de";
        break;
      case LANG_GREEK:
        langname = "el";
        break;
      case LANG_ENGLISH:
        switch (SUBLANGID(GetUserDefaultLangID())) {
          case SUBLANG_ENGLISH_UK:
            langname = "en_GB";
            break;
          default:
            langname = "en";
            break;
        }
        break;
      case LANG_SPANISH:
        langname = "es";
        break;
      case LANG_ESTONIAN:
        langname = "et";
        break;
      case LANG_FARSI:
        langname = "fa";
        break;
      case LANG_FINNISH:
        langname = "fi";
        break;
      case LANG_FRENCH:
        langname = "fr";
        break;
      case LANG_HEBREW:
        langname = "he";
        break;
      case LANG_HUNGARIAN:
        langname = "hu";
        break;
      case LANG_ITALIAN:
        langname = "it";
        break;
      case LANG_JAPANESE:
        langname = "ja";
        break;
      case LANG_KOREAN:
        langname = "ko";
        break;
      case LANG_LITHUANIAN:
        langname = "lt";
        break;
      case LANG_DUTCH:
        langname = "nl";
        break;
      case LANG_NORWEGIAN:
        langname = "nb";
        break;
      case LANG_POLISH:
        langname = "pl";
        break;
      case LANG_PORTUGUESE:
        switch (SUBLANGID(GetUserDefaultLangID())) {
          case SUBLANG_PORTUGUESE_BRAZILIAN:
            langname = "pt_BR";
            break;
          default:
            langname = "pt";
            break;
        }
        break;
      case LANG_ROMANIAN:
        langname = "ro";
        break;
      case LANG_RUSSIAN:
        langname = "ru";
        break;
      case LANG_SWEDISH:
        langname = "sv";
        break;
      case LANG_TURKISH:
        langname = "tr";
        break;
      case LANG_UKRAINIAN:
        langname = "uk";
        break;
      case LANG_CHINESE:
        langname = "zh_CN";
        break;
    }

    if (langname != nullptr) {
      static char envstr[40];

      fc_snprintf(envstr, sizeof(envstr), "LANG=%s", langname);
      putenv(envstr);
    }
  }
#endif /* FREECIV_MSWINDOWS */
#endif /* ENABLE_NLS */

  return langname;
}

#ifdef FREECIV_ENABLE_NLS
/************************************************************************//**
  Update autocap behavior to match current language.
****************************************************************************/
static void autocap_update(void)
{
  char *autocap_opt_in[] = { "fi", nullptr };
  int i;
  bool ac_enabled = FALSE;

  char *lang = getenv("LANG");

  if (lang != nullptr && lang[0] != '\0' && lang[1] != '\0') {
    for (i = 0; autocap_opt_in[i] != nullptr && !ac_enabled; i++) {
      if (lang[0] == autocap_opt_in[i][0]
          && lang[1] == autocap_opt_in[i][1]) {
        ac_enabled = TRUE;
        break;
      }
    }
  }

  capitalization_opt_in(ac_enabled);
}
#endif /* FREECIV_ENABLE_NLS */

/************************************************************************//**
  Switch to specified LANG
****************************************************************************/
void switch_lang(const char *lang)
{
#ifdef FREECIV_ENABLE_NLS
#ifdef HAVE_SETENV
  setenv("LANG", lang, TRUE);
#else  /* HAVE_SETENV */
  if (lang != nullptr) {
    static char envstr[40];

    fc_snprintf(envstr, sizeof(envstr), "LANG=%s", lang);
    putenv(envstr);
  }
#endif /* HAVE_SETENV */

  (void) setlocale(LC_ALL, "");
  (void) bindtextdomain("freeciv-core", get_locale_dir());

  autocap_update();

  log_normal("LANG set to %s", lang);
#else  /* FREECIV_ENABLE_NLS */
  fc_assert(FALSE);
#endif /* FREECIV_ENABLE_NLS */
}

/************************************************************************//**
  Setup for Native Language Support, if configured to use it.
  (Call this only once, or it may leak memory.)
****************************************************************************/
void init_nls(void)
{
  /*
   * Setup the cached locale numeric formatting information. Defaults
   * are as appropriate for the US.
   */
  grouping = fc_strdup("\3");
  grouping_sep = fc_strdup(",");

#ifdef ENABLE_NLS

#ifdef FREECIV_MSWINDOWS
  setup_langname(); /* Makes sure LANG env variable has been set */
#endif /* FREECIV_MSWINDOWS */

  (void) setlocale(LC_ALL, "");
  (void) bindtextdomain("freeciv-core", get_locale_dir());
  (void) textdomain("freeciv-core");

  /* Don't touch the defaults when LC_NUMERIC == "C".
     This is intended to cater to the common case where:
       1) The user is from North America. ;-)
       2) The user has not set the proper environment variables.
          (Most applications are (unfortunately) US-centric
          by default, so why bother?)
     This would result in the "C" locale being used, with grouping ""
     and thousands_sep "", where we really want "\3" and ",". */

  if (strcmp(setlocale(LC_NUMERIC, nullptr), "C") != 0) {
    struct lconv *lc = localeconv();

    if (lc->grouping[0] == '\0') {
      /* This actually indicates no grouping at all. */
      char *m = fc_malloc(sizeof(char));

      free(grouping);
      *m = CHAR_MAX;
      grouping = m;
    } else {
      size_t len;

      for (len = 0;
           lc->grouping[len] != '\0' && lc->grouping[len] != CHAR_MAX; len++) {
        /* Nothing */
      }
      len++;
      free(grouping);
      grouping = fc_malloc(len);
      memcpy(grouping, lc->grouping, len);
    }
    free(grouping_sep);
    grouping_sep = fc_strdup(lc->thousands_sep);
  }

  autocap_update();

#endif /* ENABLE_NLS */
}

/************************************************************************//**
  Free memory allocated by Native Language Support
****************************************************************************/
void free_nls(void)
{
  free(grouping);
  grouping = nullptr;
  free(grouping_sep);
  grouping_sep = nullptr;
}

/************************************************************************//**
  If we have root privileges, die with an error.
  (Eg, for security reasons.)
  Param argv0 should be argv[0] or similar; fallback is
  used instead if argv0 is nullptr.
  But don't die on systems where the user is always root...
  (a general test for this would be better).
  Doesn't use log_*() because gets called before logging is setup.
****************************************************************************/
void dont_run_as_root(const char *argv0, const char *fallback)
{
#if (defined(ALWAYS_ROOT) || defined(__EMX__) || defined(__BEOS__))
  return;
#else
  if (getuid() == 0 || geteuid() == 0) {
    fc_fprintf(stderr,
               _("%s: Fatal error: you're trying to run me as superuser!\n"),
               (argv0 ? argv0 : fallback ? fallback : "freeciv"));
    fc_fprintf(stderr, _("Use a non-privileged account instead.\n"));
    exit(EXIT_FAILURE);
  }
#endif /* ALWAYS_ROOT */
}

/************************************************************************//**
  Return a description string of the result.
  In English, form of description is suitable to substitute in, eg:
     prefix is <description>
  (N.B.: The description is always in English, but they have all been marked
   for translation. If you want a localized version, use _() on the return.)
****************************************************************************/
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

  fc_assert_ret_val(result >= 0 && result < ARRAY_SIZE(descriptions), nullptr);

  return descriptions[result];
}

/************************************************************************//**
  See match_prefix_full().
****************************************************************************/
enum m_pre_result match_prefix(m_pre_accessor_fn_t accessor_fn,
                               size_t n_names,
                               size_t max_len_name,
                               m_pre_strncmp_fn_t cmp_fn,
                               m_strlen_fn_t len_fn,
                               const char *prefix,
                               int *ind_result)
{
  return match_prefix_full(accessor_fn, n_names, max_len_name, cmp_fn,
                           len_fn, prefix, ind_result, nullptr, 0, nullptr);
}

/************************************************************************//**
  Given n names, with maximum length max_len_name, accessed by
  accessor_fn(0) to accessor_fn(n-1), look for matching prefix
  according to given comparison function.
  Returns type of match or fail, and for return <= M_PRE_AMBIGUOUS
  sets *ind_result with matching index (or for ambiguous, first match).
  If max_len_name == 0, treat as no maximum.
  If the int array 'matches' is not nullptr, up to 'max_matches' ambiguous
  matching names indices will be inserted into it. If 'pnum_matches' is
  not nullptr, it will be set to the number of indices inserted into 'matches'.
****************************************************************************/
enum m_pre_result match_prefix_full(m_pre_accessor_fn_t accessor_fn,
                                    size_t n_names,
                                    size_t max_len_name,
                                    m_pre_strncmp_fn_t cmp_fn,
                                    m_strlen_fn_t len_fn,
                                    const char *prefix,
                                    int *ind_result,
                                    int *matches,
                                    int max_matches,
                                    int *pnum_matches)
{
  int i, len, nmatches;

  if (len_fn == nullptr) {
    len = strlen(prefix);
  } else {
    len = len_fn(prefix);
  }
  if (len == 0) {
    return M_PRE_EMPTY;
  }
  if (len > max_len_name && max_len_name > 0) {
    return M_PRE_LONG;
  }

  nmatches = 0;
  for (i = 0; i < n_names; i++) {
    const char *name = accessor_fn(i);

    if (cmp_fn(name, prefix, len) == 0) {
      if (strlen(name) == len) {
        *ind_result = i;
        return M_PRE_EXACT;
      }
      if (nmatches == 0) {
        *ind_result = i;        /* First match */
      }
      if (matches != nullptr && nmatches < max_matches) {
        matches[nmatches] = i;
      }
      nmatches++;
    }
  }

  if (nmatches == 1) {
    return M_PRE_ONLY;
  } else if (nmatches > 1) {
    if (pnum_matches != nullptr) {
      *pnum_matches = MIN(max_matches, nmatches);
    }
    return M_PRE_AMBIGUOUS;
  } else {
    return M_PRE_FAIL;
  }
}

/************************************************************************//**
  Returns string which gives the multicast group IP address for finding
  servers on the LAN, as specified by $FREECIV_MULTICAST_GROUP.
  Gets value once, and then caches result.
****************************************************************************/
char *get_multicast_group(bool ipv6_preferred)
{
  static char *default_multicast_group_ipv4 = "225.1.1.1";
#ifdef FREECIV_IPV6_SUPPORT
  /* TODO: Get useful group (this is node local) */
  static char *default_multicast_group_ipv6 = "FF31::8000:15B4";
#endif /* IPv6 support */

  if (mc_group == nullptr) {
    char *env = getenv("FREECIV_MULTICAST_GROUP");

    if (env) {
      mc_group = fc_strdup(env);
    } else {
#ifdef FREECIV_IPV6_SUPPORT
      if (ipv6_preferred) {
        mc_group = fc_strdup(default_multicast_group_ipv6);
      } else
#endif /* IPv6 support */
      {
        mc_group = fc_strdup(default_multicast_group_ipv4);
      }
    }
  }

  return mc_group;
}

/************************************************************************//**
  Free multicast group resources
****************************************************************************/
void free_multicast_group(void)
{
  if (mc_group != nullptr) {
    free(mc_group);
    mc_group = nullptr;
  }
}

/************************************************************************//**
  Interpret ~/ in filename as home dir
  New path is returned in buf of size buf_size

  This may fail if the path is too long. It is better to use
  interpret_tilde_alloc().
****************************************************************************/
void interpret_tilde(char *buf, size_t buf_size, const char *filename)
{
  if (filename[0] == '~' && filename[1] == DIR_SEPARATOR_CHAR) {
    fc_snprintf(buf, buf_size, "%s" DIR_SEPARATOR "%s", user_home_dir(), filename + 2);
  } else if (filename[0] == '~' && filename[1] == '\0') {
    strncpy(buf, user_home_dir(), buf_size);
  } else  {
    strncpy(buf, filename, buf_size);
  }
}

/************************************************************************//**
  Interpret ~/ in filename as home dir

  The new path is returned in buf, as a newly allocated buffer. The new
  path will always be allocated and written, even if there is no ~ present.
****************************************************************************/
char *interpret_tilde_alloc(const char *filename)
{
  if (filename[0] == '~' && filename[1] == DIR_SEPARATOR_CHAR) {
    const char *home = user_home_dir();
    size_t sz;
    char *buf;

    filename += 2; /* Skip past "~/" */
    sz = strlen(home) + strlen(filename) + 2;
    buf = fc_malloc(sz);
    fc_snprintf(buf, sz, "%s/%s", home, filename);
    return buf;
  } else if (filename[0] == '~' && filename[1] == '\0') {
    return fc_strdup(user_home_dir());
  } else  {
    return fc_strdup(filename);
  }
}

/************************************************************************//**
  Return a pointer to the start of the file basename in filepath.
  If the string contains no dir separator, it is returned itself.
****************************************************************************/
char *skip_to_basename(char *filepath)
{
  int j;

  fc_assert_ret_val(filepath != nullptr, nullptr);

  for (j = strlen(filepath); j >= 0; j--) {
    if (filepath[j] == DIR_SEPARATOR_CHAR) {
      return &filepath[j+1];
    }
  }
  return filepath;
}

/************************************************************************//**
  If the directory "pathname" does not exist, recursively create all
  directories until it does.

  TODO: Make errno available after a failure, preferably via fc_get_errno().
        Currently there's things potentially messing errno between
        failed mkdir() and the function return.

  @param  pathname  directory path to create
  @param  mode      directory creation mode, or negative for default mode
  @return           success or not
****************************************************************************/
bool make_dir(const char *pathname, int mode)
{
  char *dir;
  char *path = nullptr;

  if (pathname[0] == '\0') {
    return FALSE;
  }

  if (mode < 0) {
    mode = 0755;
  }

  path = interpret_tilde_alloc(pathname);
  dir = path;

  if (*dir == '/') {
    /* Don't consider root as directory separator, but skip it. */
    dir++;
  } else if (dir[0] != '\0' && dir[1] == ':' && dir[2] == '\\') {
    /* Don't consider Windows Drive a directory to create, but skip it. */
    dir += 3;
  }

  do {
    dir = strchr(dir, DIR_SEPARATOR_CHAR);
    /* We set the current / with 0, and restore it afterwards */
    if (dir) {
      *dir = '\0';
    }

#ifdef FREECIV_MSWINDOWS
#ifdef HAVE__MKDIR
    /* Prefer _mkdir() in Windows even if mkdir() would seem to be available -
     * chances are that it's wrong kind of mkdir().
     * TODO: Make a configure check for mkdir() that also makes sure that it
     *       takes two parameters, and prefer such proper mkdir() here. */
    {
      char *path_in_local_encoding = internal_to_local_string_malloc(path);
      bool failure = FALSE;

      if (_mkdir(path_in_local_encoding) == -1
          && fc_get_errno() != ERROR_ALREADY_EXISTS) {
        failure = TRUE;
      }

      free(path_in_local_encoding);

      if (failure) {
        free(path);
        return FALSE;
      }
    }
#else  /* HAVE__MKDIR */
    if (mkdir(path, mode) == -1
        && fc_get_errno() != EEXIST) {
      free(path);
      return FALSE;
    }
#endif /* HAVE__MKDIR */
#else  /* FREECIV_MSWINDOWS */
    if (mkdir(path, mode) == -1
        && fc_get_errno() != EEXIST) {
      free(path);
      return FALSE;
    }
#endif /* FREECIV_MSWINDOWS */

    if (dir) {
      *dir = DIR_SEPARATOR_CHAR;
      dir++;
    }
  } while (dir);

  free(path);

  return TRUE;
}

/************************************************************************//**
  If the directory part of the "filename" does not exist, recursively create
  all directories until it does.
****************************************************************************/
bool make_dir_for_file(char *filename)
{
  int i;

  for (i = strlen(filename) - 1 ; filename[i] != DIR_SEPARATOR_CHAR ; i--) {
    /* Nothing */
  }

  filename[i] = '\0';
  log_debug("Create directory \"%s\"", filename);

  if (!make_dir(filename, DIRMODE_DEFAULT)) {
    return FALSE;
  }
  filename[i] = DIR_SEPARATOR_CHAR;

  return TRUE;
}

/************************************************************************//**
  Returns TRUE if the filename's path is absolute.
****************************************************************************/
bool path_is_absolute(const char *filename)
{
  if (!filename) {
    return FALSE;
  }

#ifdef FREECIV_MSWINDOWS
  if (strchr(filename, ':')) {
    return TRUE;
  }
#else  /* FREECIV_MSWINDOWS */
  if (filename[0] == '/') {
    return TRUE;
  }
#endif /* FREECIV_MSWINDOWS */

  return FALSE;
}

/************************************************************************//**
  Scan in a word or set of words from start to but not including
  any of the given delimiters. The buf pointer will point past delimiter,
  or be set to nullptr if there is nothing there. Removes excess white
  space.

  This function should be safe, and dest will contain "\0" and
  *buf == nullptr on failure. We always fail gently.

  Due to the way the scanning is performed, looking for a space
  will give you the first word even if it comes before multiple
  spaces.

  Returns delimiter found.

  Pass in nullptr for dest and -1 for size to just skip ahead. Note that if
  nothing is found, dest will contain the whole string, minus leading and
  trailing whitespace. You can scan for "" to conveniently grab the
  remainder of a string.
****************************************************************************/
char scanin(const char **buf, char *delimiters, char *dest, int size)
{
  char *ptr, found = '?';

  if (*buf == nullptr || strlen(*buf) == 0 || size == 0) {
    if (dest) {
      dest[0] = '\0';
    }
    *buf = nullptr;
    return '\0';
  }

  if (dest) {
    strncpy(dest, *buf, size-1);
    dest[size-1] = '\0';
    remove_leading_trailing_spaces(dest);
    ptr = strpbrk(dest, delimiters);
  } else {
    /* Just skip ahead. */
    ptr = strpbrk(*buf, delimiters);
  }
  if (ptr != nullptr) {
    found = *ptr;
    if (dest) {
      *ptr = '\0';
    }
    if (dest) {
      remove_leading_trailing_spaces(dest);
    }
    *buf = strpbrk(*buf, delimiters);
    if (*buf != nullptr) {
      (*buf)++; /* Skip delimiter */
    } else {
    }
  } else {
    *buf = nullptr;
  }

  return found;
}

/************************************************************************//**
  Convenience function to nicely format a time_t seconds value in to a
  string with hours, minutes, etc.
****************************************************************************/
void format_time_duration(time_t t, char *buf, int maxlen)
{
  int seconds, minutes, hours, days;
  bool space = FALSE;

  seconds = t % 60;
  minutes = (t / 60) % 60;
  hours = (t / (60 * 60)) % 24;
  days = t / (60 * 60 * 24);

  if (maxlen <= 0) {
    return;
  }

  buf[0] = '\0';

  if (days > 0) {
    cat_snprintf(buf, maxlen, "%d %s", days, PL_("day", "days", days));
    space = TRUE;
  }
  if (hours > 0) {
    cat_snprintf(buf, maxlen, "%s%d %s",
                 space ? " " : "", hours, PL_("hour", "hours", hours));
    space = TRUE;
  }
  if (minutes > 0) {
    cat_snprintf(buf, maxlen, "%s%d %s",
                 space ? " " : "",
                 minutes, PL_("minute", "minutes", minutes));
    space = TRUE;
  }
  if (seconds > 0) {
    cat_snprintf(buf, maxlen, "%s%d %s",
                 space ? " " : "",
                 seconds, PL_("second", "seconds", seconds));
  }
}

/************************************************************************//**
  Randomize the elements of an array using the Fisher-Yates shuffle.

  See: http://benpfaff.org/writings/clc/shuffle.html
****************************************************************************/
void array_shuffle(int *array, int n)
{
  if (n > 1 && array != nullptr) {
    int i, j, t;

    for (i = 0; i < n - 1; i++) {
      j = i + fc_rand(n - i);
      t = array[j];
      array[j] = array[i];
      array[i] = t;
    }
  }
}

/************************************************************************//**
  Test an asterisk in the pattern against test. Returns TRUE if test fit the
  pattern. May be recursive, as it calls wildcard_fit_string() itself (if
  many asterisks).
****************************************************************************/
static bool wildcard_asterisk_fit(const char *pattern, const char *test)
{
  char jump_to;

  /* Jump over the leading asterisks. */
  pattern++;
  while (TRUE) {
    switch (*pattern) {
    case '\0':
      /* It is a leading asterisk. */
      return TRUE;
    case '*':
      pattern++;
      continue;
    case '?':
      if ('\0' == *test) {
        return FALSE;
      }
      test++;
      pattern++;
      continue;
    }

    break;
  }

  if ('[' != *pattern) {
    if ('\\' == *pattern) {
      jump_to = *(pattern + 1);
    } else {
      jump_to = *pattern;
    }
  } else {
    jump_to = '\0';
  }

  while ('\0' != *test) {
    if ('\0' != jump_to) {
      /* Jump to next matching charather. */
      test = strchr(test, jump_to);
      if (test == nullptr) {
        /* No match. */
        return FALSE;
      }
    }

    if (wildcard_fit_string(pattern, test)) {
      return TRUE;
    }

    (test)++;
  }

  return FALSE;
}

/************************************************************************//**
  Test a range in the pattern against test. Returns TRUE if **test fit the
  first range in *pattern.
****************************************************************************/
static bool wildcard_range_fit(const char **pattern, const char **test)
{
  const char *start = (*pattern + 1);
  char testc;
  bool negation;

  if ('\0' == **test) {
    /* Need one character. */
    return FALSE;
  }

  /* Find the end of the pattern. */
  while (TRUE) {
    *pattern = strchr(*pattern, ']');
    if (*pattern == nullptr) {
      /* Wildcard format error. */
      return FALSE;
    } else if (*(*pattern - 1) != '\\') {
      /* This is the end. */
      break;
    } else {
      /* Try again. */
      (*pattern)++;
    }
  }

  if ('!' == *start) {
    negation = TRUE;
    start++;
  } else {
    negation = FALSE;
  }
  testc = **test;
  (*test)++;
  (*pattern)++;

  for (; start < *pattern; start++) {
    if ('-' == *start || '!' == *start) {
      /* Wildcard format error. */
      return FALSE;
    } else if (start < *pattern - 2 && '-' == *(start + 1)) {
      /* Case range. */
      if (*start <= testc && testc <= *(start + 2)) {
        return !negation;
      }
      start += 2;
    } else if (*start == testc) {
      /* Single character. */
      return !negation;
    }
  }

  return negation;
}

/************************************************************************//**
  Returns TRUE if test fit the pattern. The pattern can contain special
  characters:
  * '*': to specify a substitute for any zero or more characters.
  * '?': to specify a substitute for any one character.
  * '[...]': to specify a range of characters:
    * '!': at the begenning of the range means that the matching result
      will be inverted
    * 'A-Z': means any character between 'A' and 'Z'.
    * 'agr': means 'a', 'g' or 'r'.
****************************************************************************/
bool wildcard_fit_string(const char *pattern, const char *test)
{
  while (TRUE) {
    switch (*pattern) {
    case '\0':
      /* '\0' != test. */
      return '\0' == *test;
    case '*':
      return wildcard_asterisk_fit(pattern, test); /* Maybe recursive. */
    case '[':
      if (!wildcard_range_fit(&pattern, &test)) {
        return FALSE;
      }
      continue;
    case '?':
      if ('\0' == *test) {
        return FALSE;
      }
      break;
    case '\\':
      pattern++;
      fc__fallthrough;
    default:
      if (*pattern != *test) {
        return FALSE;
      }
      break;
    }
    pattern++;
    test++;
  }

  return FALSE;
}

/************************************************************************//**
  Print a string with a custom format. sequences is a pointer to an array of
  sequences, probably defined with CF_*_SEQ(). sequences_num is the number of
  the sequences, or -1 in the case the array is terminated with CF_END.

  Example:
  static const struct cf_sequence sequences[] = {
    CF_INT_SEQ('y', 2010)
  };
  char buf[256];

  fc_vsnprintcf(buf, sizeof(buf), "%y %+06y", sequences, 1);
  // This will print "2010 +02010" into buf.
****************************************************************************/
int fc_vsnprintcf(char *buf, size_t buf_len, const char *format,
                  const struct cf_sequence *sequences, size_t sequences_num)
{
  const struct cf_sequence *pseq;
  char cformat[32];
  const char *f = format;
  char *const max = buf + buf_len - 1;
  char *b = buf, *c;
  const char *const cmax = cformat + sizeof(cformat) - 2;
  int i, j;

  if ((size_t) -1 == sequences_num) {
    /* Find the number of sequences. */
    sequences_num = 0;
    for (pseq = sequences; CF_LAST != pseq->type; pseq++) {
      sequences_num++;
    }
  }

  while ('\0' != *f) {
    if ('%' == *f) {
      /* Sequence. */

      f++;
      if ('%' == *f) {
        /* Double '%'. */
        *b++ = '%';
        f++;
        continue;
      }

      /* Make format. */
      c = cformat;
      *c++ = '%';
      for (; !fc_isalpha(*f) && '\0' != *f && '%' != *f && cmax > c; f++) {
        *c++ = *f;
      }

      if (!fc_isalpha(*f)) {
        /* Beginning of a new sequence, end of the format, or too long
         * sequence. */
        *c = '\0';
        j = fc_snprintf(b, max - b + 1, "%s", cformat);
        if (-1 == j) {
          return -1;
        }
        b += j;
        continue;
      }

      for (i = 0, pseq = sequences; i < sequences_num; i++, pseq++) {
        if (pseq->letter == *f) {
          j = -2;
          switch (pseq->type) {
          case CF_BOOLEAN:
            *c++ = 's';
            *c = '\0';
            j = fc_snprintf(b, max - b + 1, cformat,
                            pseq->bool_value ? "TRUE" : "FALSE");
            break;
          case CF_TRANS_BOOLEAN:
            *c++ = 's';
            *c = '\0';
            j = fc_snprintf(b, max - b + 1, cformat,
                            pseq->bool_value ? _("TRUE") : _("FALSE"));
            break;
          case CF_CHARACTER:
            *c++ = 'c';
            *c = '\0';
            j = fc_snprintf(b, max - b + 1, cformat, pseq->char_value);
            break;
          case CF_INTEGER:
            *c++ = 'd';
            *c = '\0';
            j = fc_snprintf(b, max - b + 1, cformat, pseq->int_value);
            break;
          case CF_HEXA:
            *c++ = 'x';
            *c = '\0';
            j = fc_snprintf(b, max - b + 1, cformat, pseq->int_value);
            break;
          case CF_FLOAT:
            *c++ = 'f';
            *c = '\0';
            j = fc_snprintf(b, max - b + 1, cformat, pseq->float_value);
            break;
          case CF_POINTER:
            *c++ = 'p';
            *c = '\0';
            j = fc_snprintf(b, max - b + 1, cformat, pseq->ptr_value);
            break;
          case CF_STRING:
            *c++ = 's';
            *c = '\0';
            j = fc_snprintf(b, max - b + 1, cformat, pseq->str_value);
            break;
          case CF_LAST:
            break;
          };
          if (-2 == j) {
            log_error("Error: unsupported sequence type: %d.", pseq->type);
            break;
          }
          if (-1 == j) {
            /* Full! */
            return -1;
          }
          f++;
          b += j;
          break;
        }
      }
      if (i >= sequences_num) {
        /* Format not supported. */
        *c = '\0';
        j = fc_snprintf(b, max - b + 1, "%s%c", cformat, *f);
        if (-1 == j) {
          return -1;
        }
        f++;
        b += j;
      }
    } else {
      /* Not a sequence. */
      *b++ = *f++;
    }
    if (max <= b) {
      /* Too long. */
      *max = '\0';
      return -1;
    }
  }
  *b = '\0';
  return b - buf;
}

/************************************************************************//**
  Print a string with a custom format. The additional arguments are a suite
  of cf_*_seq() finished by cf_end(). This return the number of printed
  characters (excluding the last '\0') or -1 if the buffer is full.

  Example:
  char buf[256];

  fc_snprintcf(buf, sizeof(buf), "%y %+06y",
               cf_int_seq('y', 2010), cf_end());
  // This will print "2010 +02010" into buf.
****************************************************************************/
int fc_snprintcf(char *buf, size_t buf_len, const char *format, ...)
{
  struct cf_sequence sequences[16];
  size_t sequences_num = 0;
  va_list args;

  /* Collect sequence array. */
  va_start(args, format);
  do {
    sequences[sequences_num] = va_arg(args, struct cf_sequence);
    if (CF_LAST == sequences[sequences_num].type) {
      break;
    } else {
      sequences_num++;
    }
  } while (ARRAY_SIZE(sequences) > sequences_num);

  if (ARRAY_SIZE(sequences) <= sequences_num
      && CF_LAST != va_arg(args, struct cf_sequence).type) {
    log_error("Too many custom sequences. Maybe did you forget cf_end() "
              "at the end of the arguments?");
    buf[0] = '\0';
    va_end(args);
    return -1;
  }
  va_end(args);

  return fc_vsnprintcf(buf, buf_len, format, sequences, sequences_num);
}

/************************************************************************//**
  Extract the sequences of a format. Returns the number of extracted
  escapes.
****************************************************************************/
static size_t extract_escapes(const char *format, char *escapes,
                              size_t max_escapes)
{
  static const char format_escapes[] = {
    '*', 'd', 'i', 'o', 'u', 'x', 'X', 'e', 'E', 'f',
    'F', 'g', 'G', 'a', 'A', 'c', 's', 'p', 'n', '\0'
  };
  bool reordered = FALSE;
  size_t num = 0;
  int idx = 0;

  memset(escapes, 0, max_escapes);
  format = strchr(format, '%');
  while (format != nullptr) {
    format++;
    if ('%' == *format) {
      /* Double, not a sequence. */
      continue;
    } else if (fc_isdigit(*format)) {
      const char *start = format;

      do {
        format++;
      } while (fc_isdigit(*format));

      if ('$' == *format) {
        /* Strings are reordered. */
        sscanf(start, "%d", &idx);
        reordered = TRUE;
      }
    }

    while ('\0' != *format
           && strchr(format_escapes, *format) == nullptr) {
      format++;
    }
    escapes[idx] = *format;

    /* Increase the read count. */
    if (reordered) {
      if (idx > num) {
        num = idx;
      }
    } else {
      idx++;
      num++;
    }

    if ('*' != *format) {
      format = strchr(format, '%');
    } /* Else we didn't have found the real sequence. */
  }
  return num;
}

/************************************************************************//**
  Returns TRUE iff both formats are compatible (if 'format1' can be used
  instead 'format2' and reciprocally).
****************************************************************************/
bool formats_match(const char *format1, const char *format2)
{
  char format1_escapes[256], format2_escapes[256];
  size_t format1_escapes_num = extract_escapes(format1, format1_escapes,
                                               sizeof(format1_escapes));
  size_t format2_escapes_num = extract_escapes(format2, format2_escapes,
                                               sizeof(format2_escapes));

  return (format1_escapes_num == format2_escapes_num
          && 0 == memcmp(format1_escapes, format2_escapes,
                         format1_escapes_num));
}
