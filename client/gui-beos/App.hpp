/*	
	App.hpp
	Copyright 1996-1997 Be Do Have Software. All Rights Reserved.
*/

#ifndef APP_HPP
#define APP_HPP
#include "Defs.hpp"

#include <Message.h>
#include <String.h>

#include "BdhApp.h"
class MainWindow;
class Backend;

class App : public BdhApp {
public:
			App();
			~App();
	void	ReadyToRun(void);
	void	MessageReceived(BMessage *msg);
	void	AboutRequested(void);
	bool	QuitRequested(void);
};

extern "C" {
	void app_main( int argc, char** argv );
}

#endif // APP_HPP

