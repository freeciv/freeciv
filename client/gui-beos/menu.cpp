/* menu.cpp */

#include "Defs.hpp"
#include "menu.hpp"

#include "MainWindow.hpp"

void
update_menus(void)	// HOOK
{
	ui->PostMessage( UI_UPDATE_MENUS );
}

