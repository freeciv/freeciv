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
#ifndef FC__COMMENTS_H
#define FC__COMMENTS_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

struct section_file;

bool comments_load(void);
void comments_free(void);

void comment_file_header(struct section_file *sfile);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* FC__COMMENTS_H */
