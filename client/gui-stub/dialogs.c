/* dialogs.c -- PLACEHOLDER */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "dialogs.h"


void
popup_notify_goto_dialog(char *headline, char *lines, int x, int y)
{
	/* PORTME */
}

void
popup_notify_dialog(char *caption, char *headline, char *lines)
{
	/* PORTME */
}

void
popup_races_dialog(void)
{
	/* PORTME */
}

void
popdown_races_dialog(void)
{
	/* PORTME */
}

void
popup_unit_select_dialog(struct tile *ptile)
{
	/* PORTME */
}

void races_toggles_set_sensitive(struct packet_nations_used *packet)
{
	/* PORTME */
}

void
popup_revolution_dialog(void)
{
	/* PORTME */
}

void
popup_government_dialog(void)
{
	/* PORTME */
}

void
popup_caravan_dialog(struct unit *punit,
                          struct city *phomecity, struct city *pdestcity)
{
	/* PORTME */
}

bool caravan_dialog_is_open(void)
{
	/* PORTME */
	return FALSE;
}

void
popup_diplomat_dialog(struct unit *punit, int dest_x, int dest_y)
{
	/* PORTME */
}

bool diplomat_dialog_is_open(void)
{
	/* PORTME */
	return FALSE;
}

void
popup_incite_dialog(struct city *pcity)
{
	/* PORTME */
}

void
popup_bribe_dialog(struct unit *punit)
{
	/* PORTME */
}

void
popup_sabotage_dialog(struct city *pcity)
{
	/* PORTME */
}

void
popup_pillage_dialog(struct unit *punit, enum tile_special_type may_pillage)
{
	/* PORTME */
}

void
popup_unit_connect_dialog(struct unit *punit, int dest_x, int dest_y)
{
	/* PORTME */
}

/********************************************************************** 
  This function is called when the client disconnects or the game is
  over.  It should close all dialog windows for that game.
***********************************************************************/
void popdown_all_game_dialogs(void)
{
  /* PORTME */
}
