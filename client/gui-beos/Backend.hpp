/*	
	Backend.hpp
	Copyright 1999 Be Do Have Software. All Rights Reserved.
*/

#ifndef BACKEND_H
#define BACKEND_H
#include <app/Looper.h>
#include <app/Message.h>

class Backend : public BLooper
{
public:
			Backend();
			~Backend();
	void	MessageReceived( BMessage *msg );
	bool	QuitRequested( void );
private:
};

#endif // BACKEND_H
