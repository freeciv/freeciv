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
#include <stdlib.h>
#include <string.h>

#include <libraries/mui.h>
#include <mui/NList_MCC.h>

#include <clib/alib_protos.h>
#include <proto/intuition.h>

#include "fcintl.h"
#include "gui_main.h"
#include "muistuff.h"

void append_output_window(char *astring)
{
  DoMethod(main_output_listview, MUIM_NList_Insert, astring, -2, MUIV_List_Insert_Bottom);
  set(main_output_listview,MUIA_NList_First,  MUIV_NList_First_Bottom);
}

/**************************************************************************
 I have no idea what module this belongs in -- Syela
 I've decided to put output_window routines in chatline.c, because
 the are somewhat related and append_output_window is already here.  --dwp
**************************************************************************/
void log_output_window(void)
{ 
	char *str;
	int i,entries = xget(main_output_listview, MUIA_NList_Entries);
  FILE *fp;
  
  append_output_window(_("Exporting output window to civgame.log ..."));
  fp = fopen("civgame.log", "w"); /* should allow choice of name? */

  for (i=0;i<entries;i++)
  {
  	DoMethod(main_output_listview, MUIM_NList_GetEntry, i, &str);
  	fprintf(fp,"%s\n",str);
  }

  fclose(fp);
  append_output_window(_("Export complete."));
}

/**************************************************************************
...
**************************************************************************/
void clear_output_window(void)
{
  DoMethod(main_output_listview, MUIM_NList_Clear);
  append_output_window(_("Cleared output window.\n"));
}
