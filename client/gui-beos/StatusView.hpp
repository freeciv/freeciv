/*	
	StatusView.h
	Copyright 1999 Be Do Have Software. All Rights Reserved.
*/

#ifndef STATUSVIEW_H
#define STATUSVIEW_H
#include <Message.h>
#include "BdhView.h"

class StatusView
	: public BdhView
{
public:
			StatusView( int width );
			~StatusView();
	void	Draw( BRect r );
	void	AttachedToWindow(void);
	void	MessageReceived( BMessage *msg );
private:
};

#endif // STATUSVIEW_H
