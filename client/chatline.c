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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <X11/Intrinsic.h>
#include <X11/StringDefs.h>
#include <X11/Xaw/AsciiText.h>

#include <chatline.h>
#include <log.h>
#include <packets.h>
#include <clinet.h>
#include <xstuff.h>

extern Widget inputline_text, outputwindow_text;

/**************************************************************************
...
**************************************************************************/
void inputline_return(Widget w, XEvent *event, String *params,
		      Cardinal *num_params)
{
  struct packet_generic_message apacket;
  String theinput;
  String empty="";

  XtVaGetValues(w, XtNstring, &theinput, NULL);
  
  if(*theinput) {
    strncpy(apacket.message, theinput, MSG_SIZE-NAME_SIZE);
    apacket.message[MSG_SIZE-NAME_SIZE]='\0';
    send_packet_generic_message(&aconnection, PACKET_CHAT_MSG, &apacket);
  }

  XtVaSetValues(w, XtNstring, empty, NULL);
}

char *buf;
/**************************************************************************
 this is properly a bad way to append to a text widget. Using the 
 "useStringInPlace" resource and doubling mem alloc'ing would be better.  
 Nope - tried it and many other variations and it wasn't any better. 
 I'll replace this widget with a widget supportting hyperlinks later, so
 leth's forget the problem.
 **************************************************************************/
void append_output_window(char *astring)
{

  String theoutput;
  char *newout, *rmcr;
  
  XtVaGetValues(outputwindow_text, XtNstring, &theoutput, NULL);
  newout=malloc(strlen(astring)+strlen(theoutput)+2);
  strcpy(newout, theoutput);
  strcat(newout, "\n");
  strcat(newout, astring);

  /* calc carret position - last line, first pos */ 
  for(rmcr=newout+strlen(newout); rmcr>newout; rmcr--)
    if(*rmcr=='\n')
      break;

  /* shit happens when setting both values at the same time */
  XawTextDisableRedisplay(outputwindow_text);
  XtVaSetValues(outputwindow_text, XtNstring, newout, NULL);
  XtVaSetValues(outputwindow_text, XtNinsertPosition, rmcr-newout+1, NULL);
  XawTextEnableRedisplay(outputwindow_text);
  
  free(newout);
}

/**************************************************************************
 I have no idea what module this belongs in -- Syela
 I've decided to put output_window routines in chatline.c, because
 the are somewhat related and append_output_window is already here.  --dwp
**************************************************************************/
void log_output_window(void)
{ 
  String theoutput;
  FILE *freelog;
  
  append_output_window("Exporting output window to civgame.log ...");
  XtVaGetValues(outputwindow_text, XtNstring, &theoutput, NULL);
  freelog = fopen("civgame.log", "w"); /* should allow choice of name? */
  fprintf(freelog, "%s", theoutput);
  fclose(freelog);
  append_output_window("Export complete.");
}

/**************************************************************************
...
**************************************************************************/
void clear_output_window(void)
{
  XtVaSetValues(outputwindow_text, XtNstring, "Cleared output window.", NULL);
}

