/*	
	App.hpp
	Copyright 1996-1997 Be Do Have Software. All Rights Reserved.
*/

#ifndef APP_HPP
#define APP_HPP
#include "Defs.hpp"

#include <Message.h>
#include <String.h>

class MainWindow;

#include <Application.h>
class App : public BApplication {
public:
			App();
			~App();
	void	ReadyToRun(void);
	void	MessageReceived(BMessage *msg);
	void	AboutRequested(void);
	bool	QuitRequested(void);
	MainWindow*	mainWindow(void) { return mainWin; }

private:
	MainWindow* mainWin;
};

extern "C" {
	void app_main( int argc, char *argv[] );
}

#endif // APP_HPP

