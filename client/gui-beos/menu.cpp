/* menu.cpp */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "Defs.hpp"
#include "menu.hpp"

#include "MainWindow.hpp"

void
update_menus(void)	// HOOK
{
	ui->PostMessage( UI_UPDATE_MENUS );
}
