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
#include <stdarg.h>
#include <string.h>

#ifdef HAVE_LIBREADLINE
#include <readline/readline.h>
#endif

#include "fcintl.h"
#include "log.h"
#include "support.h"

#include "srv_main.h"

#include "console.h"

static int console_show_prompt=0;
static int console_prompt_is_showing=0;
static int console_rfcstyle=0;
#ifdef HAVE_LIBREADLINE
static int readline_received_enter = 1;
#endif

/************************************************************************
Function to handle log messages.
This must match the log_callback_fn typedef signature.
************************************************************************/
static void con_handle_log(int level, char *message)
{
  if(console_rfcstyle) {
    con_write(C_LOG_BASE+level, "%s", message);
  } else {
    con_write(C_LOG_BASE+level, "%d: %s", level, message);
  }
}

/************************************************************************
Print the prompt if it is not the last thing printed.
************************************************************************/
static void con_update_prompt(void)
{
  if (console_prompt_is_showing || !console_show_prompt)
    return;

#ifdef HAVE_LIBREADLINE
  if (readline_received_enter) {
    readline_received_enter = 0;
  } else {
    rl_forced_update_display();
  }
#else
  con_dump(C_READY,"> ");
  con_flush();
#endif

  console_prompt_is_showing = 1;
}

/************************************************************************
Initialize logging via console.
************************************************************************/
void con_log_init(char *log_filename, int log_level)
{
  log_init(log_filename, log_level, (log_filename != NULL ? NULL : con_handle_log));
  logdebug_suppress_warning;
}

/************************************************************************
Write to console without line-break, don't print prompt.
************************************************************************/
int con_dump(int i, char *message, ...)
{
  static char buf[MAX_LEN_CONSOLE_LINE];
  va_list args;
  
  va_start(args, message);
  my_vsnprintf(buf, sizeof(buf), message, args);
  va_end(args);

  if(console_prompt_is_showing) {
    printf("\n");
  }
  if ((console_rfcstyle) && (i >= 0)) {
    printf("%.3d %s", i, buf);
  } else {
    printf("%s", buf);
  }
  console_prompt_is_showing = 0;
  return (int) strlen(buf);
}

/************************************************************************
Write to console and add line-break, and show prompt if required.
************************************************************************/
void con_write(int i, char *message, ...)
{
  static char buf[MAX_LEN_CONSOLE_LINE];
  va_list args;
  
  va_start(args, message);
  my_vsnprintf(buf, sizeof(buf), message, args);
  va_end(args);

  con_puts(i, buf);
}

/************************************************************************
Write to console and add line-break, and show prompt if required.
Same as con_write, but without the format string stuff.
The real reason for this is because __attribute__ complained
with con_write(C_COMMENT,"") of "warning: zero-length format string";
this allows con_puts(C_COMMENT,"");
************************************************************************/
void con_puts(int i, char *str)
{
  if(console_prompt_is_showing) {
    printf("\n");
  }
  if ((console_rfcstyle) && (i >= 0)) {
    printf("%.3d %s\n", i, str);
  } else {
    printf("%s\n", str);
  }
  console_prompt_is_showing = 0;
  con_update_prompt();
}

/************************************************************************
For rfc-specific information only.
Adds line-break, and shows prompt if required.
************************************************************************/
void con_rfconly(int i, char *message, ...)
{
  static char buf[MAX_LEN_CONSOLE_LINE];
  va_list args;
  
  va_start(args, message);
  my_vsnprintf(buf, sizeof(buf), message, args);
  va_end(args);
  
  if ((console_rfcstyle) && (i >= 0))
    printf("%.3d %s\n", i, buf);
  console_prompt_is_showing = 0;
  con_update_prompt();
}

/************************************************************************
Ensure timely update. 
************************************************************************/
void con_flush(void)
{
  fflush(stdout);
}

/************************************************************************
Set style.
************************************************************************/
void con_set_style( int i )
{
  console_rfcstyle = i;
  if (console_rfcstyle) 
    con_puts(C_OK, _("Ok. RFC-style set."));
  else
    con_puts(C_OK, _("Ok. Standard style set."));
}

/************************************************************************
Returns rfc-style.
************************************************************************/
int con_get_style(void)
{
  return console_rfcstyle;
}

/************************************************************************
Initialize prompt; display initial message.
************************************************************************/
void con_prompt_init(void)
{
  static int first = 1;
  if (first) {
    con_puts(C_COMMENT, "");
    con_puts(C_COMMENT, _("For introductory help, type 'help'."));
    first = 0;
  }
}

/************************************************************************
Make sure a prompt is printed, and re-printed after every message.
************************************************************************/
void con_prompt_on(void)
{
  console_show_prompt = 1;
  con_update_prompt();
}

/************************************************************************
Do not print a prompt after log messages.
************************************************************************/
void con_prompt_off(void)
{
  console_show_prompt = 0;
}

/************************************************************************
User pressed enter: will need a new prompt 
************************************************************************/
void con_prompt_enter(void)
{
  console_prompt_is_showing = 0;
#ifdef HAVE_LIBREADLINE
  readline_received_enter = 1;
#endif
}

/************************************************************************
Clear "user pressed enter" state (used in special cases).
************************************************************************/
void con_prompt_enter_clear(void)
{
  console_prompt_is_showing = 1;
#ifdef HAVE_LIBREADLINE
  readline_received_enter = 0;
#endif
}
