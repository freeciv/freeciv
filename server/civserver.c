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
#include <ctype.h>
#include <string.h>

#ifdef GENERATING_MAC  /* mac header(s) */
#include <Dialogs.h>
#include <Controls.h>
#endif

#include "fcintl.h"
#include "log.h"
#include "shared.h"
#include "support.h"
#include "version.h"

#include "console.h"
#include "meta.h"
#include "sernet.h"
#include "srv_main.h"

#include "civserver.h"

#ifdef GENERATING_MAC
static void Mac_options(int argc);  /* don't need argv */
#endif

/**************************************************************************
 Entry point for Freeciv server.  Basically, does two things:
  1. Parses command-line arguments (possibly dialog, on mac).
  2. Calls the main server-loop routine.
**************************************************************************/
int main(int argc, char *argv[])
{
  int inx;
  bool showhelp = FALSE;
  bool showvers = FALSE;
  char *option = NULL;

  /* initialize server */
  srv_init();

  /* disallow running as root -- too dangerous */
  dont_run_as_root(argv[0], "freeciv_server");

  /* parse command-line arguments... */

#ifdef GENERATING_MAC
  Mac_options(argc);
#endif
  /* no  we don't use GNU's getopt or even the "standard" getopt */
  /* yes we do have reasons ;)                                   */
  inx = 1;
  while (inx < argc) {
    if ((option = get_option("--file", argv, &inx, argc)))
      srvarg.load_filename = option;
    else if (is_option("--help", argv[inx])) {
      showhelp = TRUE;
      break;
    } else if ((option = get_option("--log", argv, &inx, argc)))
      srvarg.log_filename = option;
    else if ((option = get_option("--gamelog", argv, &inx, argc)))
      srvarg.gamelog_filename = option;
    else if (is_option("--nometa", argv[inx])) {
      fprintf(stderr, _("Warning: the %s option is obsolete.  "
			"Use -m to enable the metaserver.\n"), argv[inx]);
      showhelp = TRUE;
    } else if (is_option("--meta", argv[inx]))
      srvarg.metaserver_no_send = FALSE;
    else if ((option = get_option("--Metaserver", argv, &inx, argc))) {
      sz_strlcpy(srvarg.metaserver_addr, argv[inx]);
      meta_addr_split();
      srvarg.metaserver_no_send = FALSE;	/* --Metaserver implies --meta */
    } else if ((option = get_option("--port", argv, &inx, argc)))
      srvarg.port = atoi(option);
    else if ((option = get_option("--read", argv, &inx, argc)))
      srvarg.script_filename = option;
    else if ((option = get_option("--quitidle", argv, &inx, argc)))
      srvarg.quitidle = atoi(option);
    else if ((option = get_option("--server", argv, &inx, argc)))
      sz_strlcpy(srvarg.metaserver_servername, option);
    else if ((option = get_option("--debug", argv, &inx, argc))) {
      srvarg.loglevel = log_parse_level_str(option);
      if (srvarg.loglevel == -1) {
	srvarg.loglevel = LOG_NORMAL;
	showhelp = TRUE;
	break;
      }
    } else if (is_option("--version", argv[inx]))
      showvers = TRUE;
    else {
      fprintf(stderr, _("Error: unknown option '%s'\n"), argv[inx]);
      showhelp = TRUE;
      break;
    }
    inx++;
  }

  if (showvers && !showhelp) {
    fprintf(stderr, "%s \n", freeciv_name_version());
    exit(EXIT_SUCCESS);
  }
  con_write(C_VERSION, _("This is the server for %s"), freeciv_name_version());
  con_write(C_COMMENT, _("You can learn a lot about Freeciv at %s"),
	    WEBSITE_URL);

  if (showhelp) {
    fprintf(stderr, _("Usage: %s [option ...]\nValid options are:\n"), argv[0]);
    fprintf(stderr, _("  -h, --help\t\tPrint a summary of the options\n"));
    fprintf(stderr, _("  -r, --read FILE\tRead startup script FILE\n"));
    fprintf(stderr, _("  -f, --file FILE\tLoad saved game FILE\n"));
    fprintf(stderr, _("  -p, --port PORT\tListen for clients on port PORT\n"));
    fprintf(stderr, _("  -g, --gamelog FILE\tUse FILE as game logfile\n"));
    fprintf(stderr, _("  -l, --log FILE\tUse FILE as logfile\n"));
    fprintf(stderr, _("  -m, --meta\t\tSend info to metaserver\n"));
    fprintf(stderr, _("  -M, --Metaserver ADDR\tSet ADDR as metaserver address\n"));
    fprintf(stderr, _("  -s, --server HOST\tList this server as host HOST\n"));
    fprintf(stderr, _("  -q, --quitidle TIME\tQuit if no players for TIME seconds\n"));
#ifdef DEBUG
    fprintf(stderr, _("  -d, --debug NUM\tSet debug log level (0 to 4,"
		      " or 4:file1,min,max:...)\n"));
#else
    fprintf(stderr, _("  -d, --debug NUM\tSet debug log level (0 to 3)\n"));
#endif
    fprintf(stderr, _("  -v, --version\t\tPrint the version number\n"));
    fprintf(stderr, _("Report bugs to <%s>.\n"), BUG_EMAIL_ADDRESS);
    exit(EXIT_SUCCESS);
  }

  /* have arguments, call the main server loop... */
  srv_main();

  /* suppress warnings */
  logdebug_suppress_warning;

  /* done */
  return 0;
}

#ifdef GENERATING_MAC
/*************************************************************************
  generate an option dialog if no options have been passed in
*************************************************************************/
static void Mac_options(int argc)
{
#define HARDCODED_OPT
  /*temporary hack since GetNewDialog() doesn't want to work*/
#ifdef HARDCODED_OPT
  srvarg.log_filename="log.out";
  srvarg.loglevel=LOG_DEBUG;
#else
  if (argc == 0)
  {
    OSErr err;
    DialogPtr  optptr;
    Ptr storage;
    Handle ditl;
    Handle dlog;
    short the_type;
    Handle the_handle;
    Rect the_rect;
    short the_item, old_item=16;
    int done = false;
    Str255 the_string;
    /*load/init the stuff for the dialog*/
    storage =NewPtr(sizeof(DialogRecord));
    if (storage == 0)
    {
      exit(EXIT_FAILURE);
    }
    ditl=Get1Resource('DITL',200);
    if ((ditl == 0)||(ResError()))
    {
      exit(EXIT_FAILURE);
    }
    dlog=Get1Resource('DLOG',200);
    if ((dlog == 0)||(ResError()))
    {
      exit(EXIT_FAILURE);
    }
    /*make the dialog*/
    optptr=GetNewDialog(200, storage, (WindowPtr)-1L);
    /*setup the dialog*/
    err=SetDialogDefaultItem(optptr, 1);
    if (err != 0)
    {
      exit(EXIT_FAILURE);
    }
    /*insert default highlight draw code?*/
    err=SetDialogCancelItem(optptr, 2);
    if (err != 0)
    {
      exit(EXIT_FAILURE);
    }
    err=SetDialogTracksCursor(optptr, true);
    if (err != 0)
    {
      exit(EXIT_FAILURE);
    }
    GetDItem(optptr, 16/*normal radio button*/, &the_type, &the_handle, &the_rect);
    SetCtlValue((ControlHandle)the_handle, true);

    while(!done)/*loop*/
    {
      ModalDialog(0L, &the_item);/*don't feed 0 where a upp is expected?*/
      	/*old book sugests using OL(NIL) as the first argument, but
      	It doesn't include anything about UPPs either, so...*/
      switch (the_item)
      {
        case 1:
          done = true;
        break;
        case 2:
          exit(EXIT_SUCCESS);
        break;
        case 13:
          GetDItem(optptr, 13, &the_type, &the_handle, &the_rect);
          srvarg.metaserver_no_send=GetCtlValue((ControlHandle)the_handle);
          SetCtlValue((ControlHandle)the_handle, !srvarg.metaserver_no_send);
        break;
        case 15:
        case 16:
        case 17:
          GetDItem(optptr, old_item, &the_type, &the_handle, &the_rect);
          SetCtlValue((ControlHandle)the_handle, false);
          old_item=the_item;
          GetDItem(optptr, the_item, &the_type, &the_handle, &the_rect);
          SetCtlValue((ControlHandle)the_handle, true);
        break;
      }
    }
    /*now, load the dialog items into the corect variables interpritation*/
    GetDItem( optptr, 4, &the_type, &the_handle, &the_rect);
    GetIText( the_handle, (unsigned char *)srvarg.load_filename);
    GetDItem( optptr, 6, &the_type, &the_handle, &the_rect);
    GetIText( the_handle, (unsigned char *)srvarg.gamelog_filename);
    GetDItem( optptr, 8, &the_type, &the_handle, &the_rect);
    GetIText( the_handle, (unsigned char *)srvarg.log_filename);
    GetDItem( optptr, 12, &the_type, &the_handle, &the_rect);
    GetIText( the_handle, the_string);
    if (atoi((char*)the_string)>0)
    {
      srvarg.port=atoi((char*)the_string);
    }
    GetDItem( optptr, 10, &the_type, &the_handle, &the_rect);
    GetIText( the_handle, (unsigned char *)srvarg.script_filename);
    GetDItem(optptr, 15, &the_type, &the_handle, &the_rect);
    if(GetControlValue((ControlHandle)the_handle))
    {
      srvarg.loglevel=LOG_FATAL;
    }
    GetDItem(optptr, 16, &the_type, &the_handle, &the_rect);
    if(GetControlValue((ControlHandle)the_handle))
    {
      srvarg.loglevel=LOG_NORMAL;
    }
    GetDItem(optptr, 17, &the_type, &the_handle, &the_rect);
    if(GetControlValue((ControlHandle)the_handle))
    {
      srvarg.loglevel=LOG_VERBOSE;
    }
    DisposeDialog(optptr);/*get rid of the dialog after sorting out the options*/
    DisposePtr(storage);/*clean up the allocated memory*/
  }
#endif
#undef  HARDCODED_OPT
}
#endif
