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
#ifndef FC__DIALOGS_G_H
#define FC__DIALOGS_G_H

#include "support.h"            /* bool type */

#include "fc_types.h"
#include "featured_text.h"      /* struct text_tag_list */
#include "nation.h"		/* Nation_type_id */
#include "terrain.h"		/* enum tile_special_type */
#include "unitlist.h"

#include "gui_proto_constructor.h"

struct packet_nations_selected_info;

GUI_FUNC_PROTO(void, popup_notify_goto_dialog, const char *headline,
               const char *lines,
               const struct text_tag_list *tags,
               struct tile *ptile)
GUI_FUNC_PROTO(void, popup_notify_dialog, const char *caption,
               const char *headline, const char *lines)
GUI_FUNC_PROTO(void, popup_connect_msg, const char *headline, const char *message)

GUI_FUNC_PROTO(void, popup_races_dialog, struct player *pplayer)
GUI_FUNC_PROTO(void, popdown_races_dialog, void)

GUI_FUNC_PROTO(void, unit_select_dialog_popup, struct tile *ptile)
void unit_select_dialog_update(void); /* Defined in update_queue.c. */
GUI_FUNC_PROTO(void, unit_select_dialog_update_real, void)

GUI_FUNC_PROTO(void, races_toggles_set_sensitive, void)

GUI_FUNC_PROTO(void, popup_caravan_dialog, struct unit *punit,
               struct city *phomecity, struct city *pdestcity)
GUI_FUNC_PROTO(bool, caravan_dialog_is_open, int* unit_id, int* city_id)
GUI_FUNC_PROTO(void, caravan_dialog_update, void)

GUI_FUNC_PROTO(void, popup_diplomat_dialog, struct unit *punit,
               struct tile *ptile)
GUI_FUNC_PROTO(int, diplomat_handled_in_diplomat_dialog, void)
GUI_FUNC_PROTO(void, close_diplomat_dialog, void)
GUI_FUNC_PROTO(void, popup_incite_dialog, struct city *pcity, int cost)
GUI_FUNC_PROTO(void, popup_bribe_dialog, struct unit *punit, int cost)
GUI_FUNC_PROTO(void, popup_sabotage_dialog, struct city *pcity)
GUI_FUNC_PROTO(void, popup_pillage_dialog, struct unit *punit,
               bv_special spe, bv_bases bases, bv_roads roads)
GUI_FUNC_PROTO(void, popup_upgrade_dialog, struct unit_list *punits)
GUI_FUNC_PROTO(void, popup_disband_dialog, struct unit_list *punits)
GUI_FUNC_PROTO(void, popup_tileset_suggestion_dialog, void)
GUI_FUNC_PROTO(bool, popup_theme_suggestion_dialog, const char *theme_name)

GUI_FUNC_PROTO(void, popdown_all_game_dialogs, void)

#endif  /* FC__DIALOGS_G_H */
