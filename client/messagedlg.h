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
#ifndef __MESSAGEDLG_H
#define __MESSAGEDLG_H

void init_messages_where(void);
void popup_messageopt_dialog(void);

/* for specifying which event messages go where: */
#define NUM_MW 3
#define MW_OUTPUT    1		/* add to the output window */
#define MW_MESSAGES  2		/* add to the messages window */
#define MW_POPUP     4		/* popup an individual window */

extern unsigned int messages_where[];	/* OR-ed MW_ values [E_LAST] */

#endif
