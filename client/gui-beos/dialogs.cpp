/* dialogs.c -- PLACEHOLDER */

#include <Alert.h>
#include "Defs.hpp"
#include "dialogs.h"


// ----------------------------------------------

void
popup_notify_goto_dialog(char *headline, char *lines, int x, int y)
{
	NOT_FINISHED( "popup_notify_goto_dialog(char" );
}


// ----------------------------------------------

void
popup_notify_dialog(char *caption, char *headline, char *lines)
{
	NOT_FINISHED( "popup_notify_dialog(char" );
}


// ----------------------------------------------

BAlert *races_dialog = NULL;

void
popup_races_dialog(void)
{
	NOT_FINISHED( "popup_races_dialog(void)" );
}


void
popdown_races_dialog(void)
{
	if (!races_dialog) return;
	NOT_FINISHED( "popdown_races_dialog(void)" );
}


// ----------------------------------------------

void
popup_unit_select_dialog(struct tile *ptile)
{
	NOT_FINISHED( "popup_unit_select_dialog(struct" );
}


void
races_toggles_set_sensitive(int bits1, int bits2)
{
	NOT_FINISHED( "races_toggles_set_sensitive(int" );
}


// ----------------------------------------------

void
popup_revolution_dialog(void)
{
	NOT_FINISHED( "popup_revolution_dialog(void)" );
}


// ----------------------------------------------

void
popup_government_dialog(void)
{
	NOT_FINISHED( "popup_government_dialog(void)" );
}


// ----------------------------------------------

void
popup_caravan_dialog(struct unit *punit,
                          struct city *phomecity, struct city *pdestcity)
{
	NOT_FINISHED( "popup_caravan_dialog(struct" );
}


int
caravan_dialog_is_open(void)
{
	NOT_FINISHED( "caravan_dialog_is_open" );
	return 0;
}


// ----------------------------------------------

void
popup_diplomat_dialog(struct unit *punit, int dest_x, int dest_y)
{
	NOT_FINISHED( "popup_diplomat_dialog(struct" );
}


int
diplomat_dialog_is_open(void)
{
	NOT_FINISHED( "caravan_dialog_is_open" );
	return 0;
}


void
popup_incite_dialog(struct city *pcity)
{
	NOT_FINISHED( "popup_incite_dialog(struct" );
}


void
popup_bribe_dialog(struct unit *punit)
{
	NOT_FINISHED( "popup_bribe_dialog(struct" );
}


void
popup_pillage_dialog(struct unit *punit, int may_pillage)
{
	NOT_FINISHED( "popup_pillage_dialog(struct" );
}


void
process_caravan_arrival(struct unit *punit)
{
	NOT_FINISHED( "process_caravan_arrival(struct" );
}


void
popup_sabotage_dialog(struct city *pcity)
{
	NOT_FINISHED( "popup_sabotage_dialog(struct" );
}


// ----------------------------------------------

void
popup_unit_connect_dialog(struct unit *punit, int dest_x, int dest_y)
{
	NOT_FINISHED( "popup_unit_connect_dialog(struct" );
}
