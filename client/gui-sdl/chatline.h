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

/**********************************************************************
                          chatline.h  -  description
                             -------------------
    begin                : Sun Jun 30 2002
    copyright            : (C) 2002 by Rafa³ Bursig
    email                : Rafa³ Bursig <bursig@poczta.fm>
 **********************************************************************/

#ifndef FC__CHATLINE_H
#define FC__CHATLINE_H

#include "chatline_g.h"

#define	MAX_CHATLINE_HISTORY 20


void Init_Input_Edit(Sint16 start_x, Sint16 start_y, Uint16 w, Uint16 h);
void Init_Log_Window(Sint16 start_x, Sint16 start_y, Uint16 w, Uint16 h);
void Resize_Log_Window(Uint16 w, Uint16 h);

void Redraw_Log_Window(int bcgd);


void set_output_window_unitext(Uint16 * pUnistring);

void unicode_append_output_window(Uint16 * unistring);

#define set_output_window_text( pString )	\
		set_output_window_unitext( convert_to_utf16( pString ) )

#endif				/* FC__CHATLINE_H */
