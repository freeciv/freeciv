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
#ifndef FC__HISTORYSTRINGCLASS_H
#define FC__HISTORYSTRINGCLASS_H

#define HistoryStringObject NewObject(CL_HistoryString->mcc_Class, NULL

extern struct MUI_CustomClass *CL_HistoryString;

BOOL create_historystring_class(void);
VOID delete_historystring_class(void);

#endif  /* FC__HISTORYSTRINGCLASS_H */
