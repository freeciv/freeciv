/**********************************************************************
 Freeciv - Copyright (C) 2001 - R. Falke, M. Kaufman
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

#include <SDL/SDL.h>
#include <SDL/SDL_ttf.h>

#include <assert.h>

#include "fcintl.h"
#include "events.h"
#include "game.h"
#include "log.h"

#include "gui_mem.h"

#include "support.h"

#include "chatline_g.h"
#include "citydlg_g.h"
#include "cityrep.h"
#include "dialogs.h"
#include "inputdlg.h"
#include "gui_main.h"
#include "gui_string.h"
#include "gui_stuff.h"
#include "messagewin_g.h"
#include "cma_fec.h"

#include "cma_fe.h"

#define BUFFER_SIZE             64

#define SPECLIST_TAG cma_dialog
#define SPECLIST_TYPE struct cma_dialog
#include "speclist.h"

#define SPECLIST_TAG cma_dialog
#define SPECLIST_TYPE struct cma_dialog
#include "speclist_c.h"

#define cma_dialog_list_iterate(presetlist, ppreset) \
    TYPED_LIST_ITERATE(struct cma_dialog, presetlist, ppreset)
#define cma_dialog_list_iterate_end  LIST_ITERATE_END
