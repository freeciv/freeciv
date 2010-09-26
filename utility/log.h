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
#ifndef FC__LOG_H
#define FC__LOG_H

#include <stdarg.h>
#include <stdlib.h>

#include "fcintl.h"             /* _() */
#include "support.h"            /* bool type and fc__attribute */

enum log_level {
  LOG_FATAL = 0,
  LOG_ERROR,                    /* non-fatal errors */
  LOG_NORMAL,
  LOG_VERBOSE,                  /* not shown by default */
  LOG_DEBUG                     /* suppressed unless DEBUG defined */
};


/* A function type to enable custom output of log messages other than
 * via fputs(stderr).  Eg, to the server console while handling prompts,
 * rfcstyle, client notifications; Eg, to the client window output window?
 */
typedef void (*log_callback_fn)(enum log_level, const char *, bool file_too);

/* A function type to generate a custom prefix for the log messages, e.g.
 * add the turn and/or time of the log message. */
typedef const char *(*log_prefix_fn)(void);

void log_init(const char *filename, enum log_level initial_level,
              log_callback_fn callback, log_prefix_fn prefix,
              int fatal_assertions);
bool log_parse_level_str(const char *level_str, enum log_level *ret_level);

log_callback_fn log_set_callback(log_callback_fn callback);
log_prefix_fn log_set_prefix(log_prefix_fn prefix);
void log_set_level(enum log_level level);
enum log_level log_get_level(void);
#ifdef DEBUG
bool log_do_output_for_level_at_location(enum log_level level,
                                         const char *file, int line);
#endif

void vdo_log(const char *file, const char *function, int line,
             bool print_from_where, enum log_level level,
             const char *message, va_list args);
void do_log(const char *file, const char *function, int line,
            bool print_from_where, enum log_level level,
            const char *message, ...)
            fc__attribute((__format__ (__printf__, 6, 7)));

#ifdef DEBUG
#define log_do_output_for_level(level) \
  log_do_output_for_level_at_location(level, __FILE__, __LINE__)
#else
#define log_do_output_for_level(level) (log_get_level() >= level)
#endif /* DEBUG */


/* The log macros */
#define log_base(level, message, ...)                                       \
  if (log_do_output_for_level(level)) {                                     \
    do_log(__FILE__, __FUNCTION__, __LINE__, FALSE,                         \
           level, message, ## __VA_ARGS__);                                 \
  }
/* This one doesn't need check, fatal messages are always displayed. */
#define log_fatal(message, ...)                                             \
  do_log(__FILE__, __FUNCTION__, __LINE__, FALSE,                           \
         LOG_FATAL, message, ## __VA_ARGS__);
#define log_error(message, ...)                                             \
  log_base(LOG_ERROR, message, ## __VA_ARGS__)
#define log_normal(message, ...)                                            \
  log_base(LOG_NORMAL, message, ## __VA_ARGS__)
#define log_verbose(message, ...)                                           \
  log_base(LOG_VERBOSE, message, ## __VA_ARGS__)
#ifdef DEBUG
#  define log_debug(message, ...)                                           \
  log_base(LOG_DEBUG, message, ## __VA_ARGS__)
#else
#  define log_debug(message, ...) /* Do nothing. */
#endif /* DEBUG */

/* Used by game debug command */
#define log_test log_normal
#define log_packet log_verbose
#define LOG_TEST LOG_NORMAL /* needed by citylog_*() functions */


/* Assertions. */
void fc_assert_set_fatal(int fatal_assertions);
#ifdef NDEBUG
/* Disable the assertion failures, but not the tests! */
#define fc_assert_fail(...) (void) 0
#else
void fc_assert_fail(const char *file, const char *function, int line,
                    const char *assertion, const char *message, ...)
                    fc__attribute((__format__ (__printf__, 5, 6)));
#endif /* NDEBUG */

#define fc_assert_full(file, function, line,                                \
                       condition, action, message, ...)                     \
  if (!(condition)) {                                                       \
    fc_assert_fail(file, function, line, #condition,                        \
                   message, ## __VA_ARGS__);                                \
    action;                                                                 \
  }

#ifdef NDEBUG
/* Disabled. */
#define fc_assert(...) (void) 0
#define fc_assert_msg(...) (void) 0
#else
/* Like assert(). */
#define fc_assert(condition)                                                \
  ((condition) ? (void) 0                                                   \
   : fc_assert_fail(__FILE__, __FUNCTION__, __LINE__, #condition, NULL))
/* Like assert() with extra message. */
#define fc_assert_msg(condition, message, ...)                              \
  ((condition) ? (void) 0                                                   \
   : fc_assert_fail(__FILE__, __FUNCTION__, __LINE__,                       \
                    #condition, message, ## __VA_ARGS__))
#endif /* NDEBUG */

/* Do action on failure. */
#define fc_assert_action(condition, action)                                 \
  fc_assert_full(__FILE__, __FUNCTION__, __LINE__, condition, action, NULL)
/* Return on failure. */
#define fc_assert_ret(condition)                                            \
  fc_assert_action(condition, return)
/* Return a value on failure. */
#define fc_assert_ret_val(condition, val)                                   \
  fc_assert_action(condition, return val)
/* Exit on failure. */
#define fc_assert_exit(condition)                                           \
  fc_assert_action(condition, exit(EXIT_FAILURE))

/* Do action on failure with extra message. */
#define fc_assert_action_msg(condition, action, message, ...)               \
  fc_assert_full(__FILE__, __FUNCTION__, __LINE__, condition, action,       \
                 message, ## __VA_ARGS__)
/* Return on failure with extra message. */
#define fc_assert_ret_msg(condition, message, ...)                          \
  fc_assert_action_msg(condition, return, message, ## __VA_ARGS__)
/* Return a value on failure with extra message. */
#define fc_assert_ret_val_msg(condition, val, message, ...)                 \
  fc_assert_action_msg(condition, return val, message, ## __VA_ARGS__)
/* Exit on failure with extra message. */
#define fc_assert_exit_msg(condition, message, ...)                         \
  fc_assert_action(condition,                                               \
                   log_fatal(message, ## __VA_ARGS__); exit(EXIT_FAILURE));

#endif  /* FC__LOG_H */
