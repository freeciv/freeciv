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
#ifndef FC__CONSOLE_H
#define FC__CONSOLE_H

#include "attribute.h"
#include "shared.h"		/* bool type */

#define MAX_LEN_CONSOLE_LINE 512
				/* closing \0 included */
#define C_IGNORE -1		/* never print RFC-style number prefix */
#define C_COMMENT 0 		/* for human eyes only */
#define C_VERSION 1		/* version info */
#define C_DEBUG	2		/* debug info */
#define C_LOG_BASE 10		/* 10, 11, 12 depending on log level */
#define C_OK 100		/* success of requested operation */
#define C_CONNECTION 101	/* new client */
#define C_DISCONNECTED 102	/* client gone */
#define C_REJECTED 103		/* client rejected */
#define C_FAIL 200		/* failure of requested operation */ 
#define C_METAERROR 201		/* failure of meta server */
#define C_SYNTAX 300		/* syntax error or value out of range */
#define C_BOUNCE 301		/* option no longer available */
#define C_GENFAIL 400		/* failure not caused by a requested operation */
#define C_WARNING 500		/* something may be wrong */
#define C_READY 999		/* waiting for input */

/* initialize logging via console */
void con_log_init(char *log_filename, int log_level);

/* write to console without line-break, don't print prompt */
int con_dump(int i, char *message, ...)
     fc__attribute((format (printf, 2, 3)));

/* write to console and add line-break, and show prompt if required. */
void con_write(int i, char *message, ...)
     fc__attribute((format (printf, 2, 3)));

/* write to console and add line-break, and show prompt if required.
   ie, same as con_write, but without the format string stuff. */
void con_puts(int i, char *str);
     
/* ensure timely update */
void con_flush(void);

/* initialize prompt; display initial message */
void con_prompt_init(void);

/* make sure a prompt is printed, and re-printed after every message */
void con_prompt_on(void);

/* do not print a prompt after every message */
void con_prompt_off(void);

/* user pressed enter: will need a new prompt */
void con_prompt_enter(void);

/* clear "user pressed enter" state (used in special cases) */
void con_prompt_enter_clear(void);

/* set rfc-style */
void con_set_style(bool i);

/* return rfc-style */
bool con_get_style(void);

/* for rfc-specific information only */
void con_rfconly(int i, char *message, ...)
     fc__attribute((format (printf, 2, 3)));

#endif  /* FC__CONSOLE_H */
