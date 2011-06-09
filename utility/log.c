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

#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "fciconv.h"
#include "fcintl.h"
#include "mem.h"
#include "shared.h"
#include "support.h"

#include "log.h"

#define MAX_LEN_LOG_LINE 512

static char *log_filename = NULL;
static log_callback_fn log_callback = NULL;
static log_prefix_fn log_prefix = NULL;

#ifdef DEBUG
static const enum log_level max_level = LOG_DEBUG;
#else
static const enum log_level max_level = LOG_VERBOSE;
#endif /* DEBUG */

static enum log_level fc_log_level = LOG_NORMAL;
static int fc_fatal_assertions = -1;

#ifdef DEBUG
struct log_fileinfo {
  char *name;
  enum log_level level;
  int min;
  int max;
};
static int log_num_files = 0;
static struct log_fileinfo *log_files = NULL;
#endif /* DEBUG */

/**************************************************************************
  level_str should be either "0", "1", "2", "3", "4" or
  "4:filename" or "4:file1:file2" or "4:filename,100,200" etc

  If everything goes ok, returns TRUE. If there was a parsing problem,
  prints to stderr, and returns FALSE.

  Return in ret_level the requested level only if level_str is a simple
  number (like "0", "1", "2").

  Also sets up the log_files data structure. Does _not_ set fc_log_level.
**************************************************************************/
bool log_parse_level_str(const char *level_str, enum log_level *ret_level)
{
  const char *c;
  int n = 0;                    /* number of filenames */
  int level;
#ifdef DEBUG
  const char *tok;
  int i;
  char *dup;
  bool ret = TRUE;
#endif /* DEBUG */

  c = level_str;
  n = 0;
  while ((c = strchr(c, ':'))) {
    c++;
    n++;
  }
  if (n == 0) {
    /* Global log level. */
    if (!str_to_int(level_str, &level)) {
      fc_fprintf(stderr, _("Bad log level \"%s\".\n"), level_str);
      return FALSE;
    }
    if (level >= LOG_FATAL && level <= max_level) {
      if (NULL != ret_level) {
        *ret_level = level;
      }
      return TRUE;
    } else {
      fc_fprintf(stderr, _("Bad log level %d in \"%s\".\n"),
                 level, level_str);
#ifndef DEBUG
      if (level == max_level + 1) {
        fc_fprintf(stderr, _("Freeciv must be compiled with the DEBUG flag"
                             " to use debug level %d.\n"), max_level + 1);
      }
#endif /* DEBUG */
      return FALSE;
    }
  }

#ifdef DEBUG
  c = level_str;
  level = c[0] - '0';
  if (c[1] == ':') {
    if (level < LOG_FATAL || level > max_level) {
      fc_fprintf(stderr, _("Bad log level %d in \"%s\".\n"),
                 level, level_str);
      return FALSE;
    }
  } else {
    fc_fprintf(stderr, _("Badly formed log level argument \"%s\".\n"),
               level_str);
    return FALSE;
  }
  i = log_num_files;
  log_num_files += n;
  log_files = fc_realloc(log_files,
                         log_num_files * sizeof(struct log_fileinfo));

  dup = fc_strdup(c + 2);
  tok = strtok(dup, ":");

  if (!tok) {
    fc_fprintf(stderr, _("Badly formed log level argument \"%s\".\n"),
               level_str);
    ret = FALSE;
    goto out;
  }
  do {
    struct log_fileinfo *pfile = log_files + i;
    char *d = strchr(tok, ',');

    pfile->min = 0;
    pfile->max = 0;
    pfile->level = level;
    if (d) {
      char *pc = d + 1;

      d[0] = '\0';
      d = strchr(d + 1, ',');
      if (d && *pc != '\0' && d[1] != '\0') {
        d[0] = '\0';
        if (!str_to_int(pc, &pfile->min)) {
          fc_fprintf(stderr, _("Not an integer: '%s'\n"), pc);
          ret = FALSE;
          goto out;
        }
        if (!str_to_int(d + 1, &pfile->max)) {
          fc_fprintf(stderr, _("Not an integer: '%s'\n"), d + 1);
          ret = FALSE;
          goto out;
        }
      }
    }
    if (strlen(tok) == 0) {
      fc_fprintf(stderr, _("Empty filename in log level argument \"%s\".\n"),
                 level_str);
      ret = FALSE;
      goto out;
    }
    pfile->name = fc_strdup(tok);
    i++;
    tok = strtok(NULL, ":");
  } while(tok);

  if (i != log_num_files) {
    fc_fprintf(stderr, _("Badly formed log level argument \"%s\".\n"),
               level_str);
    ret = FALSE;
    goto out;
  }

out:
  free(dup);
  return ret;
#else
  fc_fprintf(stderr, _("Freeciv must be compiled with the DEBUG flag "
                       "to use advanced log levels based on files.\n"));
  return FALSE;
#endif /* DEBUG */
}

/**************************************************************************
  Initialise the log module. Either 'filename' or 'callback' may be NULL.
  If both are NULL, print to stderr. If both are non-NULL, both callback, 
  and fprintf to file.  Pass -1 for fatal_assertions to don't raise any
  signal on failed assertion.
**************************************************************************/
void log_init(const char *filename, enum log_level initial_level,
              log_callback_fn callback, log_prefix_fn prefix,
              int fatal_assertions)
{
  fc_log_level = initial_level;
  if (log_filename) {
    free(log_filename);
    log_filename = NULL;
  }
  if (filename && strlen(filename) > 0) {
    log_filename = fc_strdup(filename);
  } else {
    log_filename = NULL;
  }
  log_callback = callback;
  log_prefix = prefix;
  fc_fatal_assertions = fatal_assertions;
  log_verbose("log started");
  log_debug("LOG_DEBUG test");
}

/**************************************************************************
  Adjust the callback function after initial log_init().
**************************************************************************/
log_callback_fn log_set_callback(log_callback_fn callback)
{
  log_callback_fn old = log_callback;

  log_callback = callback;

  return old;
}

/**************************************************************************
  Adjust the prefix callback function after initial log_init().
**************************************************************************/
log_prefix_fn log_set_prefix(log_prefix_fn prefix)
{
  log_prefix_fn old = log_prefix;

  log_prefix = prefix;

  return old;
}

/**************************************************************************
  Adjust the logging level after initial log_init().
**************************************************************************/
void log_set_level(enum log_level level)
{
  fc_log_level = level;
}

/**************************************************************************
  Returns the current log level.
**************************************************************************/
enum log_level log_get_level(void)
{
  return fc_log_level;
}

#ifdef DEBUG
/**************************************************************************
  Returns wether we should do an output for this level, in this file,
  at this line.
**************************************************************************/
bool log_do_output_for_level_at_location(enum log_level level,
                                         const char *file, int line)
{
  struct log_fileinfo *pfile;
  int i;

  for (i = 0, pfile = log_files; i < log_num_files; i++, pfile++) {
    if (pfile->level >= level
        && 0 == strcmp(pfile->name, file)
        && ((0 == pfile->min && 0 == pfile->max)
            || (pfile->min <= line && pfile->max >= line))) {
      return TRUE;
    }
  }
  return (fc_log_level >= level);
}
#endif /* DEBUG */

/**************************************************************************
  Unconditionally print a simple string.
  Let the callback do its own level formating and add a '\n' if it wants.
**************************************************************************/
static void log_write(FILE *fs, enum log_level level,
                      const char *file, const char *function, int line,
                      bool print_from_where, const char *message)
{
  if (log_filename || (!log_callback)) {
    char prefix[128];

    if (log_prefix) {
      /* Get the log prefix. */
      fc_snprintf(prefix, sizeof(prefix), "[%s] ", log_prefix());
    } else {
      prefix[0] = '\0';
    }

    if (log_filename || print_from_where) {
      fc_fprintf(fs, "%d: %sin %s() [%s::%d]: %s\n",
                 level, prefix, function, file, line, message);
    } else {
      fc_fprintf(fs, "%d: %s%s\n", level, prefix, message);
    }
    fflush(fs);
  }
  if (log_callback) {
    if (print_from_where) {
      char buf[MAX_LEN_LOG_LINE];

      fc_snprintf(buf, sizeof(buf), "in %s() [%s::%d]: %s",
                  function, file, line, message);
      log_callback(level, buf, log_filename != NULL);
    } else {
      log_callback(level, message, log_filename != NULL);
    }
  }
}

/**************************************************************************
  Unconditionally print a log message. This function is usually protected
  by do_log_for().
  For repeat message, may wait and print instead
  "last message repeated ..." at some later time.
  Calls log_callback if non-null, else prints to stderr.
**************************************************************************/
void vdo_log(const char *file, const char *function, int line,
             bool print_from_where, enum log_level level,
             const char *message, va_list args)
{
  static char bufbuf[2][MAX_LEN_LOG_LINE];
  static bool bufbuf1 = FALSE;
  static unsigned int repeated = 0; /* total times current message repeated */
  static unsigned int next = 2; /* next total to print update */
  static unsigned int prev = 0; /* total on last update */
  /* only count as repeat if same level */
  static enum log_level prev_level = -1;
  static bool recursive = FALSE;
  char buf[MAX_LEN_LOG_LINE];
  FILE *fs;

  if (recursive) {
    fc_fprintf(stderr, _("Error: recursive calls to log.\n"));
    return;
  }

  recursive = TRUE;

  if (log_filename) {
    if (!(fs = fc_fopen(log_filename, "a"))) {
      fc_fprintf(stderr,
                 _("Couldn't open logfile: %s for appending \"%s\".\n"), 
                 log_filename, message);
      exit(EXIT_FAILURE);
    }
  } else {
    fs = stderr;
  }

  fc_vsnprintf(bufbuf1 ? bufbuf[1] : bufbuf[0],
               MAX_LEN_LOG_LINE, message, args);

  if (level == prev_level && 0 == strncmp(bufbuf[0], bufbuf[1],
                                          MAX_LEN_LOG_LINE - 1)){
    repeated++;
    if (repeated == next) {
      fc_snprintf(buf, sizeof(buf),
                  PL_("last message repeated %d time",
                      "last message repeated %d times",
                      repeated-prev), repeated-prev);
      if (repeated > 2) {
        cat_snprintf(buf, sizeof(buf), 
                     PL_(" (total %d repeat)",
                         " (total %d repeats)",
                         repeated), repeated);
      }
      log_write(fs, prev_level, file, function, line,
                print_from_where, buf);
      prev = repeated;
      next *= 2;
    }
  } else {
    if (repeated > 0 && repeated != prev) {
      if (repeated == 1) {
        /* just repeat the previous message: */
        log_write(fs, prev_level, file, function, line,
                  print_from_where, bufbuf1 ? bufbuf[0] : bufbuf[1]);
      } else {
        fc_snprintf(buf, sizeof(buf),
                    PL_("last message repeated %d time", 
                        "last message repeated %d times",
                        repeated - prev), repeated - prev);
        if (repeated > 2) {
          cat_snprintf(buf, sizeof(buf), 
                       PL_(" (total %d repeat)", " (total %d repeats)",
                           repeated),  repeated);
        }
        log_write(fs, prev_level, file, function, line,
                  print_from_where, buf);
      }
    }
    prev_level = level;
    repeated = 0;
    next = 2;
    prev = 0;
    log_write(fs, level, file, function, line,
              print_from_where,bufbuf1 ? bufbuf[1] : bufbuf[0]);
  }
  bufbuf1 = !bufbuf1;
  fflush(fs);
  if (log_filename) {
    fclose(fs);
  }
  recursive = FALSE;
}

/**************************************************************************
  Unconditionally print a log message. This function is usually protected
  by do_log_for().
  For repeat message, may wait and print instead
  "last message repeated ..." at some later time.
  Calls log_callback if non-null, else prints to stderr.
**************************************************************************/
void do_log(const char *file, const char *function, int line,
            bool print_from_where, enum log_level level,
            const char *message, ...)
{
  va_list args;

  va_start(args, message);
  vdo_log(file, function, line, print_from_where, level, message, args);
  va_end(args);
}

/**************************************************************************
  Set what signal the fc_assert* macros should raise on failed assertion
  (-1 to disable).
**************************************************************************/
void fc_assert_set_fatal(int fatal_assertions)
{
  fc_fatal_assertions = fatal_assertions;
}

#ifndef NDEBUG
/**************************************************************************
  Returns wether the fc_assert* macros should raise a signal on failed
  assertion.
**************************************************************************/
void fc_assert_fail(const char *file, const char *function, int line,
                    const char *assertion, const char *message, ...)
{
  enum log_level level = (0 <= fc_fatal_assertions ? LOG_FATAL : LOG_ERROR);

  if (NULL != assertion) {
    do_log(file, function, line, TRUE, level,
           "assertion '%s' failed.", assertion);
  }

  if (NULL != message) {
    /* Additional message. */
    va_list args;

    va_start(args, message);
    vdo_log(file, function, line, FALSE, level, message, args);
    va_end(args);
  }

  do_log(file, function, line, FALSE, level,
         /* TRANS: No full stop after the URL, could cause confusion. */
         _("Please report this message at %s"), BUG_URL);

  if (0 <= fc_fatal_assertions) {
    /* Emit a signal. */
    raise(fc_fatal_assertions);
  }
}
#endif /* NDEBUG */
