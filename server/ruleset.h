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
#ifndef FC__RULESET_H
#define FC__RULESET_H

struct conn_list;

/* functions */
bool load_rulesets(void);
void reload_rulesets_settings(void);
void send_rulesets(struct conn_list *dest);

void ruleset_error_real(const char *file, const char *function,
                        int line, enum log_level level,
                        const char *format, ...)
  fc__attribute((__format__ (__printf__, 5, 6)));

#define ruleset_error(level, format, ...)                               \
  if (log_do_output_for_level(level)) {                                 \
    ruleset_error_real(__FILE__, __FUNCTION__, __FC_LINE__,             \
                       level, format, ## __VA_ARGS__);                  \
  }

#endif  /* FC__RULESET_H */
